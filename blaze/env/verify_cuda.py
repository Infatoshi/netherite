#!/usr/bin/env python3
"""M2 gate: CPU blaze vs CUDA blaze, identical snapshots and action streams.

Default (gate): N=64 envs, the curriculum .bsnp snapshots in rl/out/snaps
(64x128x64 d-stage bakes; the 128^3 *_t0.bsnp fresh-spawn snapshots have
different region dims and load into a separate env) assigned round-robin,
250 decisions x repeat 4 = 1000 ticks of per-env seeded xorshift32 random
actions (stream is a function of the GLOBAL env index). Every decision
compares cam/depth/edge/pose/done BITWISE and rew/scal bitwise-first with a
<=1e-12-relative fallback (device libm atan2/asin/sin/cos are 1-2 ulp
double, not correctly rounded). Every 50 decisions the done envs are
mask-reset on both backends (exercises k_reset).

--big: N=4096 on CUDA, 250 decisions. Envs are fully independent and the
action stream depends only on the global env index, so the CPU reference for
64 randomly chosen lanes is an exact replica: run CPU N=64 with those lanes'
snapshot assignments + action streams and compare bitwise per decision (a
full CPU N=4096 run costs ~190s/1000 ticks; the subsample is exact, not
approximate). Also prints a sha256 over the final full-batch outputs for
reproducibility. No mid-run resets in --big (done envs idle).

--chain: full spawn-to-torch chain gate on CUDA. Batch of --chain-lanes
(default 64) envs, ALL assigned the fresh-spawn tick-0 snapshot s10_t0.bsnp,
replaying the committed 2058-action chain (movement + hotbar + use/place +
craft:N + interact). --m2-kernel raw (default) uses blaze_tick_raw
(k_tick_raw, env=-1 broadcast). --m2-kernel warp uses blaze_tick with
create opts.warp_tick=1 (k_tick_warp, 32 lanes per env; blaze.conf /
ppo.conf / blaze_abi.h default). --m2-kernel scalar uses blaze_tick with
warp_tick=0 (k_tick, one thread per env, warp-cooperative recenter). k_tick
is a build option: build the .so with BLAZE_SCALAR_TICK=1 for scalar rows,
otherwise blaze_create refuses warp_tick=0 and says so.
EVERY tick, EVERY lane's full BOLR record must match the batch-of-1 CPU
blaze record byte-for-byte (the CPU env is itself byte-exact vs the real
game per verify_cpu.py --chain) - one loop covers both the CPU==CUDA chain
gate and the 64-identical-lanes cross-env-interference check. Raw mode
stays. Production kernels compare against CPU blaze_tick (decision path).

--mixed: big-N FULL-action gate: N (default 2048) on the 13 *_t0.bsnp
snapshots round-robin, 250 decisions of seeded random full 12-double action
rows (continuous dyaw/dpitch, fractional forward, strafe/sneak/sprint, and
occasional use/hotbar/craft/interact), 64 exact CPU replica lanes compared
bitwise per decision. No mid-run resets (done envs idle).

--bench: M3 throughput (see report): random GPU actions, no comparisons.
--t0 switches the bench to the 128^3 t0 snapshots + full action decode.

Usage (anvil, GPU0 must be idle - check nvidia-smi first):
  cd magma && uv run --no-project --with numpy,torch python \
      blaze/env/verify_cuda.py [--big|--chain|--mixed] \
      [--bench --t0 --n 4096] [--decisions 250]
"""
import argparse
import ctypes
import glob
import hashlib
import json
import os
import random
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from verify_cpu import (
    BIN_SIZE,
    BLOCKED,
    PARITY_INDEX,
    PARITY_NAMES,
    PARITY_SIZE,
    PARITY_SUPPORTED,
    ParityRecord,
    VERIFIED,
    first_diff_field,
    fmt_field,
    parity_mask_names,
    parity_pair_status,
    snapshot_dynamics_blocker,
)

from blaze import CPU_SO, CUDA_SO, VecBlaze
from sharded_vec import ShardedCpuVec

RL = os.path.join(os.path.dirname(HERE), "rl")
SNAPS = os.path.join(RL, "out", "snaps")
REL_GATE = 1e-12


def snap_paths(t0=False, snaps=None):
    """Curriculum d-stage snapshots (64x128x64) by default; t0=True selects
    the fresh-spawn 128^3 *_t0.bsnp set. The two have different region dims
    and cannot share one env's region pools. snaps overrides the directory."""
    root = snaps or SNAPS
    paths = sorted(glob.glob(os.path.join(root, "*.bsnp")))
    # the t0 family (128^3) includes the iron-stage bake s*_t0_iron.bsnp
    paths = [p for p in paths
             if ("_t0" in os.path.basename(p)) == bool(t0)]
    if not paths:
        raise SystemExit(f"no {'t0 ' if t0 else 'curriculum '}snapshots in "
                         f"{root} (run make_snapshots.py)")
    return paths


def xs32(s):
    s ^= (s << 13) & 0xFFFFFFFF
    s ^= s >> 17
    s ^= (s << 5) & 0xFFFFFFFF
    return s


class ActStream:
    """Per-env xorshift32 action stream, keyed by GLOBAL env index (matches
    blaze_verify.c's generator shape: 5 draws per decision)."""
    def __init__(self, idx):
        s = (0xC0A1 ^ (idx * 2654435761)) & 0xFFFFFFFF
        self.s = s if s else 1

    def _d(self):
        self.s = xs32(self.s)
        return self.s

    def next_action(self):
        return [self._d() % 3, self._d() % 3, int(self._d() % 4 != 0),
                int(self._d() % 8 == 0), int(self._d() % 4 != 3)]

    def next_full(self):
        """Full 12-double raw action row (--mixed): continuous dyaw/dpitch,
        fractional forward, strafe, all flags, occasional use/hotbar/craft/
        interact. Fixed 12 draws per decision - the stream is a pure
        function of the global env index, so CPU replica lanes are exact."""
        fwd = (0.0, 0.5, 1.0, 1.0)[self._d() % 4]
        strafe = (-1.0, 0.0, 0.0, 1.0)[self._d() % 4]
        dyaw = ((self._d() % 61) - 30) * 0.5     # [-15, 15] deg, 0.5 steps
        dpitch = ((self._d() % 41) - 20) * 0.5   # [-10, 10] deg
        jump = float(self._d() % 8 == 0)
        sneak = float(self._d() % 16 == 0)
        sprint = float(self._d() % 8 == 0)
        attack = float(self._d() % 4 != 3)
        use = float(self._d() % 8 == 0)
        hv = self._d() % 27
        hotbar = float(hv) if hv < 9 else -1.0   # 1/3 of decisions
        cv = self._d() % 96
        craft = float(cv) if cv < 6 else -1.0    # ~6% of decisions
        interact = float(self._d() % 32 == 0)
        return [fwd, strafe, dyaw, dpitch, jump, sneak, sprint, attack,
                use, hotbar, craft, interact]


def to_np(x):
    return x.cpu().numpy() if hasattr(x, "cpu") else np.asarray(x)


class Cmp:
    def __init__(self):
        self.exact_fields = ("cam", "depth", "edge", "pose", "done")
        self.rew_bitwise = True
        self.scal_bitwise = True
        self.rew_maxrel = 0.0
        self.rew_maxulp = 0
        self.scal_maxrel = 0.0
        self.scal_maxulp = 0
        self.fail = None

    def _float_cmp(self, name, a, b):
        bits_a = a.view(np.uint32).astype(np.int64)
        bits_b = b.view(np.uint32).astype(np.int64)
        if np.array_equal(bits_a, bits_b):
            return True
        ulp = int(np.abs(bits_a - bits_b).max())
        denom = np.maximum(np.maximum(np.abs(a), np.abs(b)), 1e-30)
        rel = float((np.abs(a.astype(np.float64) - b.astype(np.float64)) /
                     denom).max())
        if name == "rew":
            self.rew_bitwise = False
            self.rew_maxrel = max(self.rew_maxrel, rel)
            self.rew_maxulp = max(self.rew_maxulp, ulp)
        else:
            self.scal_bitwise = False
            self.scal_maxrel = max(self.scal_maxrel, rel)
            self.scal_maxulp = max(self.scal_maxulp, ulp)
        return rel <= REL_GATE

    def compare(self, d, cpu, cuda, lanes=None):
        """cpu/cuda: dicts of numpy arrays. lanes: cuda lane indices matching
        cpu rows (None = 1:1). Returns False and records first failure."""
        for name in ("cam", "depth", "edge", "pose", "done", "rew", "scal"):
            a = cpu[name]
            b = cuda[name] if lanes is None else cuda[name][lanes]
            if name in self.exact_fields:
                if not np.array_equal(a, b):
                    idx = np.argwhere(a != b)[0]
                    env = int(idx[0])
                    self.fail = (f"decision {d}: field {name} env "
                                 f"{env if lanes is None else lanes[env]} "
                                 f"first mismatch at {tuple(idx)}: "
                                 f"cpu={a[tuple(idx)]} cuda={b[tuple(idx)]}")
                    return False
            else:
                if not self._float_cmp(name, a, b):
                    self.fail = (f"decision {d}: field {name} exceeds "
                                 f"{REL_GATE} relative (see summary)")
                    return False
        return True

    def summary(self):
        r = []
        r.append("rew:  bitwise" if self.rew_bitwise else
                 f"rew:  NOT bitwise, max ulp {self.rew_maxulp}, "
                 f"max rel {self.rew_maxrel:.3e} (gate {REL_GATE})")
        r.append("scal: bitwise" if self.scal_bitwise else
                 f"scal: NOT bitwise, max ulp {self.scal_maxulp}, "
                 f"max rel {self.scal_maxrel:.3e} (gate {REL_GATE})")
        return "; ".join(r)


def outputs(env, lanes=None):
    """Host copies of env outputs. lanes indexes on device first so a CUDA
    N=4096 gather for a 64-lane replica does not DtoH the full batch."""
    def grab(x):
        if lanes is not None:
            x = x[lanes]
        return to_np(x)
    return {"cam": grab(env.cam), "depth": grab(env.depth),
            "edge": grab(env.edge), "scal": grab(env.scal),
            "rew": grab(env.rew), "done": grab(env.done),
            "pose": grab(env.pose)}


def _step_cpu_cuda(cpu, cuda, cpu_acts, cuda_acts, repeat, device, pool):
    """CPU replica and CUDA batch are independent. Overlap the replica
    (other processes, or OpenMP in a thread) with the GPU tick."""
    import torch
    fut = pool.submit(cpu.step, cpu_acts, repeat)
    if not isinstance(cuda_acts, torch.Tensor):
        cuda_acts = torch.as_tensor(cuda_acts, device=f"cuda:{device}")
    elif cuda_acts.device.type != "cuda":
        cuda_acts = cuda_acts.to(f"cuda:{device}")
    cuda.step(cuda_acts, repeat=repeat)
    fut.result()


def _create_kw(args=None):
    """VecBlaze create kwargs from parsed args (historical unset defaults)."""
    kw = {}
    if args is None:
        return kw
    if getattr(args, "ktime", False):
        kw["ktime"] = True
    if getattr(args, "op_trace", False):
        kw["op_trace"] = True
    if getattr(args, "stage_time", False):
        kw["stage_time"] = True
    if getattr(args, "legacy_recenter", False):
        kw["legacy_recenter"] = True
    if getattr(args, "no_ore_xy", False):
        kw["no_ore_xy"] = True
    if getattr(args, "stack_kib", None) is not None:
        kw["stack_kib"] = int(args.stack_kib)
    if getattr(args, "warp_tick", None) is not None:
        kw["warp_tick"] = int(args.warp_tick)
    else:
        kernel = getattr(args, "m2_kernel", "raw")
        if kernel == "scalar":
            kw["warp_tick"] = 0
        elif kernel == "warp":
            kw["warp_tick"] = 1
    return kw


def make_envs(n_cpu, n_cuda, paths, cpu_assign, cuda_assign, device, args=None):
    kw = _create_kw(args)
    cpu_workers = getattr(args, "cpu_workers", 1) if args is not None else 1
    if cpu_workers > 1 and n_cpu > 1:
        workers = min(cpu_workers, n_cpu)
        cpu = ShardedCpuVec(n_cpu, workers=workers, device=0, so_path=CPU_SO, **kw)
    else:
        cpu = VecBlaze(n_cpu, device=0, so_path=CPU_SO, **kw)
    cuda = VecBlaze(n_cuda, device=device, so_path=CUDA_SO, **kw)
    for e, asn in ((cpu, cpu_assign), (cuda, cuda_assign)):
        e.load_snapshots(paths)
        e.assign(asn)
        e.reset()
    return cpu, cuda


def run_gate(args):
    paths = snap_paths()
    n = 64
    assign = [i % len(paths) for i in range(n)]
    print(f"gate: N={n}, {len(paths)} snapshots round-robin, "
          f"{args.decisions} decisions x repeat {args.repeat}")
    cpu, cuda = make_envs(n, n, paths, assign, assign, args.device, args)
    streams = [ActStream(i) for i in range(n)]
    cmp = Cmp()
    ok = True
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=1) as pool:
        for d in range(args.decisions):
            acts = np.array([s.next_action() for s in streams], dtype=np.int32)
            _step_cpu_cuda(cpu, cuda, acts, acts, args.repeat, args.device,
                           pool)
            a, b = outputs(cpu), outputs(cuda)
            if not cmp.compare(d, a, b):
                print(f"FAIL {cmp.fail}")
                ok = False
                break
            if (d + 1) % 50 == 0:
                mask = a["done"] != 0
                if d + 1 == 100:
                    # force a partial reset even if nothing is done, so the
                    # masked k_reset path is exercised mid-run on both backends
                    mask = mask | (np.arange(n) % 3 == 0)
                if mask.any():
                    cpu.reset(mask.astype(np.uint8))
                    cuda.reset(mask.astype(np.uint8))
                print(f"  decision {d+1}: identical so far "
                      f"({int(mask.sum())} envs mask-reset)")
    print(f"float gate: {cmp.summary()}")
    ticks = args.decisions * args.repeat * n
    if ok:
        print(f"PASS: gate N={n} x {args.decisions} decisions "
              f"({ticks} env-ticks): cam/depth/edge/pose/done bitwise "
              f"zero diffs")
    else:
        print("FAIL: gate")
    cpu.close(); cuda.close()
    return ok


def run_big(args):
    paths = snap_paths()
    nbig, nsub = args.n, 64
    lanes = sorted(random.Random(1234).sample(range(nbig), nsub))
    lanes_np = np.array(lanes)
    assign_big = [i % len(paths) for i in range(nbig)]
    assign_sub = [assign_big[l] for l in lanes]
    print(f"big-N spot check: CUDA N={nbig}, {args.decisions} decisions; "
          f"CPU exact-replica of {nsub} random lanes (seed 1234)")
    cpu, cuda = make_envs(nsub, nbig, paths, assign_sub, assign_big,
                          args.device, args)
    big_streams = [ActStream(i) for i in range(nbig)]
    cmp = Cmp()
    ok = True
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=1) as pool:
        for d in range(args.decisions):
            acts = np.array([s.next_action() for s in big_streams],
                            dtype=np.int32)
            _step_cpu_cuda(cpu, cuda, acts[lanes], acts, args.repeat,
                           args.device, pool)
            a = outputs(cpu)
            b = outputs(cuda, lanes=lanes_np)
            if not cmp.compare(d, a, b):
                print(f"FAIL {cmp.fail}")
                ok = False
                break
            if (d + 1) % 50 == 0:
                print(f"  decision {d+1}: {nsub} lanes identical so far "
                      f"({int(b['done'].sum())} of them done)")
    if ok:
        h = hashlib.sha256()
        b = outputs(cuda)
        for name in ("cam", "depth", "edge", "scal", "rew", "done", "pose"):
            h.update(b[name].tobytes())
        ndone = int((b["done"] != 0).sum())
        print(f"final full-batch sha256: {h.hexdigest()}  "
              f"({ndone}/{nbig} done)")
    print(f"float gate: {cmp.summary()}")
    print(("PASS" if ok else "FAIL") +
          f": big-N N={nbig}, {nsub} lanes exact vs CPU over "
          f"{args.decisions} decisions")
    cpu.close(); cuda.close()
    return ok


def _raw_abi(env):
    """Declare the raw-tick/emit verify-helper ABI on a VecBlaze's lib."""
    env.lib.blaze_tick_raw.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_void_p]
    env.has_blaze_tick = hasattr(env.lib, "blaze_tick")
    if env.has_blaze_tick:
        env.lib.blaze_tick.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
            ctypes.c_int, ctypes.c_void_p]
        env.lib.blaze_tick.restype = ctypes.c_int
    env.lib.blaze_emit.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_void_p]
    env.lib.blaze_obs_size.restype = ctypes.c_int
    env.lib.blaze_parity_size.restype = ctypes.c_int
    env.lib.blaze_parity_state.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    env.lib.blaze_parity_state.restype = ctypes.c_int
    # Batched all-lanes PARY (CUDA only; absent from blaze_cpu.so). Same
    # no_emit_all opt-out pattern is reused as no_parity_all so the serial
    # path stays A/B-testable.
    env.has_parity_all = (hasattr(env.lib, "blaze_parity_state_all")
                          and not getattr(env, "no_parity_all", False))
    if env.has_parity_all:
        env.lib.blaze_parity_state_all.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p]
        env.lib.blaze_parity_state_all.restype = ctypes.c_int
    env.lib.blaze_capabilities.argtypes = []
    env.lib.blaze_capabilities.restype = ctypes.c_ulonglong
    env.lib.blaze_snapshot_requirements.argtypes = [
        ctypes.c_void_p, ctypes.c_int]
    env.lib.blaze_snapshot_requirements.restype = ctypes.c_ulonglong
    assert env.lib.blaze_obs_size() == BIN_SIZE


def _raw_act(act):
    return (ctypes.c_double * 17)(
        act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
        act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
        act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
        act.get("hotbar", -1), act.get("craft", -1), act.get("interact", 0),
        act.get("smelt", 0), act.get("inv_click", 0), act.get("inv_slot", 0),
        act.get("inv_button", 0), act.get("inv_type", 0))


def run_chain(args, iron=False):
    seed = args.chain_seed
    tag = "iron" if iron else "chain"
    snap = args.snapshot or os.path.join(
        SNAPS, f"s{seed}_t0_iron.bsnp" if iron else f"s{seed}_t0.bsnp")
    acts_path = (args.tape if getattr(args, "tape", None) is not None
                 else os.path.join(RL, "out", f"{tag}_actions_s{seed}.json"))
    if not os.path.exists(snap) or not os.path.exists(acts_path):
        print(f"{tag} gate: missing {snap} or {acts_path}")
        return False
    blocker = snapshot_dynamics_blocker(snap)
    if blocker:
        print(f"BLOCKED before stepping: {blocker}")
        args.parity_blocked = True
        return False
    acts = [{k: v for k, v in a.items() if k != "snapshot"}
            for a in json.load(open(acts_path))]
    if (args.expected_chain_actions is not None and
            len(acts) != args.expected_chain_actions):
        print(f"BLOCKED: {tag} fixture has {len(acts)} actions, expected "
              f"{args.expected_chain_actions}")
        args.parity_blocked = True
        return False
    nl = args.chain_lanes
    kernel = getattr(args, "m2_kernel", "raw") or "raw"
    prod = kernel in ("warp", "scalar")
    # Default-on state digests: every tick, every CUDA lane's PARY is compared
    # against the batch-of-1 CPU record for all BP_IMPLEMENTED subsystems.
    # --no-state-digest reverts to BOLR-only. Explicit --port-parity --features
    # still selects a subset and enforces the end-of-run evidence requirement.
    state_digest = not args.no_state_digest
    if args.port_parity and args.parity_features:
        features = list(args.parity_features)
        require_evidence = True
    elif state_digest:
        features = list(PARITY_SUPPORTED)
        require_evidence = False
    else:
        features = []
        require_evidence = False
    digest_note = (
        f" + state digests ({','.join(features)})" if features else "")
    print(f"{tag} gate: {os.path.basename(snap)} x {len(acts)} actions, "
          f"CUDA batch of "
          f"{nl} identical lanes vs batch-of-1 CPU, byte-exact every tick"
          f"{digest_note}  kernel={kernel}")
    kw = _create_kw(args)
    cpu = VecBlaze(1, device=0, so_path=CPU_SO, **kw)
    cuda = VecBlaze(nl, device=args.device, so_path=CUDA_SO, **kw)
    if getattr(args, "no_parity_all", False):
        cpu.no_parity_all = True
        cuda.no_parity_all = True
    for e, n in ((cpu, 1), (cuda, nl)):
        _raw_abi(e)
        e.load_snapshots([snap])
        e.assign([0] * n)
        e.reset()
    if prod and (not cpu.has_blaze_tick or not cuda.has_blaze_tick):
        print("BLOCKED: blaze_tick missing from CPU or CUDA .so "
              "(rebuild blaze_cpu.so / blaze_cuda.so)")
        args.parity_blocked = True
        cpu.close()
        cuda.close()
        return False
    cpu_tick = cpu.lib.blaze_tick if prod else cpu.lib.blaze_tick_raw
    cuda_tick = cuda.lib.blaze_tick if prod else cuda.lib.blaze_tick_raw
    if getattr(args, "mobs_on", False):
        for e in (cpu, cuda):
            e.lib.blaze_set_mobs_enabled.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            e.lib.blaze_set_mobs_enabled.restype = ctypes.c_int
            if e.lib.blaze_set_mobs_enabled(e.h, 1) != 0:
                print("BLOCKED: blaze_set_mobs_enabled failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
    if getattr(args, "natural_spawn", False):
        for e in (cpu, cuda):
            e.lib.blaze_set_natural_spawn.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            e.lib.blaze_set_natural_spawn.restype = ctypes.c_int
            if e.lib.blaze_set_natural_spawn(e.h, 1) != 0:
                print("BLOCKED: blaze_set_natural_spawn failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
            e.lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            e.lib.blaze_set_world_time.restype = ctypes.c_int
            if e.lib.blaze_set_world_time(e.h, 18000) != 0:
                print("BLOCKED: blaze_set_world_time failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
    if getattr(args, "natural_spawn_passive", False):
        for e in (cpu, cuda):
            e.lib.blaze_set_natural_spawn_passive.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            e.lib.blaze_set_natural_spawn_passive.restype = ctypes.c_int
            if e.lib.blaze_set_natural_spawn_passive(e.h, 1) != 0:
                print("BLOCKED: blaze_set_natural_spawn_passive failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
            e.lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            e.lib.blaze_set_world_time.restype = ctypes.c_int
            if e.lib.blaze_set_world_time(e.h, 6000) != 0:
                print("BLOCKED: blaze_set_world_time failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
    if args.parity_features and "elytra" in args.parity_features:
        for e in (cpu, cuda):
            e.lib.blaze_set_elytra_enabled.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            e.lib.blaze_set_elytra_enabled.restype = ctypes.c_int
            if e.lib.blaze_set_elytra_enabled(e.h, 1) != 0:
                print("BLOCKED: blaze_set_elytra_enabled failed")
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
    requested = sum(1 << PARITY_INDEX[name] for name in features)
    if args.strict_capabilities and features:
        for source, env in (("CPU", cpu), ("CUDA", cuda)):
            capabilities = env.lib.blaze_capabilities()
            requirements = env.lib.blaze_snapshot_requirements(env.h, 0)
            unsupported = requested & ~capabilities
            missing_requirements = requirements & ~capabilities
            if unsupported or missing_requirements:
                details = []
                if unsupported:
                    details.append("lacks requested capabilities "
                                   + parity_mask_names(unsupported))
                if missing_requirements:
                    details.append("snapshot requires unsupported capabilities "
                                   + parity_mask_names(missing_requirements))
                print(f"BLOCKED before stepping: {source} " + "; ".join(details))
                args.parity_blocked = True
                cpu.close()
                cuda.close()
                return False
    buf_c = ctypes.create_string_buffer(BIN_SIZE)
    buf_g = ctypes.create_string_buffer(BIN_SIZE)
    # One block for all nl lanes, filled by a single blaze_emit_all call. The
    # per-lane blaze_emit loop below costs a launch+sync+memcpy round trip per
    # lane per tick and dominated this gate's wall clock (2058 ticks x 64
    # lanes); the block lets the common all-match case be one memcmp.
    buf_all = ctypes.create_string_buffer(BIN_SIZE * nl)
    # Zero-copy views for that memcmp. Materialising .raw twice per tick
    # (buf_all plus the tiled CPU record, 2 x nl x BIN_SIZE bytes) cost more
    # than the batched emit it was checking; these views alias the ctypes
    # buffers in place, and the comparison broadcasts the single CPU record
    # across all nl lanes.
    view_all = np.frombuffer(buf_all, dtype=np.uint8).reshape(nl, BIN_SIZE)
    view_c = np.frombuffer(buf_c, dtype=np.uint8)
    parity_c = ctypes.create_string_buffer(PARITY_SIZE)
    parity_g = ctypes.create_string_buffer(PARITY_SIZE)
    # Batched all-lanes PARY: one launch + one DtoH per tick (not 64 serial
    # blaze_parity_state calls). Same layout as buf_all: lane i at offset
    # i * PARITY_SIZE.
    parity_all = ctypes.create_string_buffer(PARITY_SIZE * nl) if features \
        else None
    view_parity_all = (
        np.frombuffer(parity_all, dtype=np.uint8).reshape(nl, PARITY_SIZE)
        if parity_all is not None else None)
    view_parity_c = np.frombuffer(parity_c, dtype=np.uint8)
    ok = True
    cpu_evidence = {name: False for name in features}
    cuda_evidence = {name: [False] * nl for name in features}

    def report_state_fail(t, lane, cpu_record, cuda_record, subsystem, detail):
        nonlocal ok
        print(f"FAIL tick {t} lane {lane}: state digest feature "
              f"{subsystem or 'record'} ({detail}) kernel={kernel}")
        if subsystem is not None:
            idx = PARITY_INDEX[subsystem]
            print(f"    cpu:  digest=0x{cpu_record.digest[idx]:016x} "
                  f"evidence={cpu_record.evidence[idx]} "
                  f"active={int(bool(cpu_record.active & (1 << idx)))}")
            print(f"    cuda: digest=0x{cuda_record.digest[idx]:016x} "
                  f"evidence={cuda_record.evidence[idx]} "
                  f"active={int(bool(cuda_record.active & (1 << idx)))}")
        ok = False
        return False

    def lanes_match(t, want):
        nonlocal ok
        cpu_record = None
        if features:
            assert cpu.lib.blaze_parity_size() == PARITY_SIZE
            assert cuda.lib.blaze_parity_size() == PARITY_SIZE
            assert cpu.lib.blaze_parity_state(
                cpu.h, 0, parity_c) == 0
            cpu_record = ParityRecord(parity_c.raw, "CPU")
            missing = requested & ~(cpu_record.implemented &
                                    cpu_record.measured)
            if missing:
                print("BLOCKED: CPU PARY does not implement and measure "
                      + parity_mask_names(missing))
                args.parity_blocked = True
                ok = False
                return False
            for name in features:
                cpu_evidence[name] |= bool(
                    cpu_record.evidence[PARITY_INDEX[name]])
        # Fast path: one batched emit, one memcmp of the whole lane block
        # against the CPU record tiled nl times (every lane must equal it).
        # This decides the BOLR verdict; it never changes it. On any mismatch
        # we fall through to the per-lane loop below, which re-emits lane by
        # lane and produces the byte-identical first-diff report it always did.
        fast_ok = False
        if cuda.has_emit_all:
            assert cuda.lib.blaze_emit_all(cuda.h, want, buf_all) == 0
            fast_ok = bool((view_all == view_c).all())

        # Batched state-digest path: one blaze_parity_state_all + one memcmp of
        # the whole PARY block against the CPU record tiled nl times. Only on
        # a mismatch do we walk lanes to name the first lane + feature.
        # Valid only after a batched emit: the observations digest hashes the
        # env's cam/dep/edg buffers, which blaze_emit_all just rendered. In
        # the per-lane path lanes render inside the loop below, so batched
        # parity here would digest stale frames.
        parity_fast_ok = False
        if features and getattr(cuda, "has_parity_all", False) \
                and cuda.has_emit_all:
            assert cuda.lib.blaze_parity_state_all(cuda.h, parity_all) == 0
            parity_fast_ok = bool((view_parity_all == view_parity_c).all())

        if fast_ok and (not features or parity_fast_ok):
            if features and parity_fast_ok:
                # All lanes matched the CPU PARY; still accumulate evidence
                # from the single CPU record (lanes are identical on PASS).
                for name in features:
                    hit = bool(cpu_record.evidence[PARITY_INDEX[name]])
                    if hit:
                        for lane in range(nl):
                            cuda_evidence[name][lane] = True
            return True

        for lane in range(nl):
            if not fast_ok:
                assert cuda.lib.blaze_emit(cuda.h, lane, want, buf_g) == 0
                if buf_g.raw != buf_c.raw:
                    f = first_diff_field(buf_c.raw, buf_g.raw) or "blocks/logs"
                    print(f"FAIL tick {t} lane {lane}: field {f} "
                          f"kernel={kernel}")
                    if f != "blocks/logs":
                        print(f"    cpu:  {fmt_field(buf_c.raw, f)}")
                        print(f"    cuda: {fmt_field(buf_g.raw, f)}")
                    ok = False
                    return False
            if features:
                if parity_fast_ok:
                    # Already know every lane matches; no per-lane work.
                    continue
                if getattr(cuda, "has_parity_all", False) \
                        and parity_all is not None and cuda.has_emit_all:
                    off = lane * PARITY_SIZE
                    raw_g = bytes(parity_all.raw[off:off + PARITY_SIZE])
                else:
                    assert cuda.lib.blaze_parity_state(
                        cuda.h, lane, parity_g) == 0
                    raw_g = parity_g.raw
                cuda_record = ParityRecord(raw_g, f"CUDA lane {lane}")
                missing = requested & ~(cuda_record.implemented &
                                        cuda_record.measured)
                if missing:
                    print(f"BLOCKED: CUDA lane {lane} PARY does not implement "
                          "and measure " + parity_mask_names(missing))
                    args.parity_blocked = True
                    ok = False
                    return False
                for name in features:
                    cuda_evidence[name][lane] |= bool(
                        cuda_record.evidence[PARITY_INDEX[name]])
                status, detail, subsystem = parity_pair_status(
                    cpu_record, cuda_record, features)
                if status == BLOCKED:
                    print(f"BLOCKED tick {t} CUDA lane {lane}: {detail}")
                    args.parity_blocked = True
                    ok = False
                    return False
                if status != VERIFIED:
                    return report_state_fail(
                        t, lane, cpu_record, cuda_record, subsystem, detail)
        return True

    assert cpu.lib.blaze_emit(cpu.h, 0, 1, buf_c) == 0
    if lanes_match("initial", 1):
        for t, act in enumerate(acts):
            want = act.get("cam", 1)
            a = _raw_act(act)
            assert cpu_tick(cpu.h, 0, a, want, buf_c) == 0
            assert cuda_tick(cuda.h, -1, a, 0, None) == 0
            if not lanes_match(t, want):
                break
            if (t + 1) % 500 == 0:
                print(f"  tick {t+1}: {nl} lanes byte-exact so far")
    if ok and features and require_evidence:
        no_evidence = [
            name for name in features
            if not cpu_evidence[name] or not all(cuda_evidence[name])
        ]
        if no_evidence:
            for name in no_evidence:
                observed = sum(cuda_evidence[name])
                print(f"BLOCKED: subsystem {name} has zero fixture evidence "
                      f"(CPU={int(cpu_evidence[name])}, "
                      f"CUDA lanes={observed}/{nl})")
            args.parity_blocked = True
            ok = False
    print(("PASS" if ok else "FAIL") +
          f": {tag} s{seed} x {len(acts)} ticks, {nl} CUDA lanes vs CPU "
          f"byte-exact (full BOLR record{digest_note}, every tick, "
          f"kernel={kernel})")
    cpu.close(); cuda.close()
    return ok


def run_mixed(args):
    paths = snap_paths(t0=True)
    nbig, nsub = args.n, 64
    lanes = sorted(random.Random(1234).sample(range(nbig), nsub))
    lanes_np = np.array(lanes)
    assign_big = [i % len(paths) for i in range(nbig)]
    assign_sub = [assign_big[l] for l in lanes]
    print(f"mixed big-N FULL-action gate: CUDA N={nbig} on {len(paths)} t0 "
          f"snapshots, {args.decisions} decisions; CPU exact-replica of "
          f"{nsub} random lanes (seed 1234)")
    cpu, cuda = make_envs(nsub, nbig, paths, assign_sub, assign_big,
                          args.device, args)
    big_streams = [ActStream(i) for i in range(nbig)]
    cmp = Cmp()
    ok = True
    phase = getattr(args, "phase_time", False)
    acc = {"actgen": 0.0, "step": 0.0, "obs": 0.0, "cmp": 0.0, "sha": 0.0}
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=1) as pool:
        for d in range(args.decisions):
            t0 = time.perf_counter()
            acts = np.array([s.next_full() for s in big_streams],
                            dtype=np.float64)
            t1 = time.perf_counter()
            _step_cpu_cuda(cpu, cuda, acts[lanes], acts, args.repeat,
                           args.device, pool)
            t2 = time.perf_counter()
            a = outputs(cpu)
            b = outputs(cuda, lanes=lanes_np)
            t3 = time.perf_counter()
            if not cmp.compare(d, a, b):
                print(f"FAIL {cmp.fail}")
                ok = False
                break
            t4 = time.perf_counter()
            if phase:
                acc["actgen"] += t1 - t0
                acc["step"] += t2 - t1
                acc["obs"] += t3 - t2
                acc["cmp"] += t4 - t3
            if (d + 1) % 50 == 0:
                print(f"  decision {d+1}: {nsub} lanes identical so far "
                      f"({int(b['done'].sum())} of them done)")
    if ok:
        t_s0 = time.perf_counter()
        h = hashlib.sha256()
        b = outputs(cuda)
        for name in ("cam", "depth", "edge", "scal", "rew", "done", "pose"):
            h.update(b[name].tobytes())
        acc["sha"] = time.perf_counter() - t_s0
        ndone = int((b["done"] != 0).sum())
        print(f"final full-batch sha256: {h.hexdigest()}  "
              f"({ndone}/{nbig} done)")
    print(f"float gate: {cmp.summary()}")
    print(("PASS" if ok else "FAIL") +
          f": mixed N={nbig}, {nsub} lanes exact vs CPU over "
          f"{args.decisions} decisions (full action set)")
    if phase:
        print("phase: " + " ".join(f"{k}={v:.3f}s" for k, v in acc.items()))
    cpu.close(); cuda.close()
    return ok


def dump_op_trace(env, ops0, args, write=True):
    """Print the op histogram + per-env activity fractions and dump the full
    per-env counter matrix (bit-level zoom traces: single-env histogram at
    --n 1, cross-env activity correlation input at big N)."""
    from blaze import OP_NAMES
    ops = env.op_trace()
    if ops is None:
        print("op-trace: unavailable (op_trace was not set at create)")
        return
    d = (ops - ops0).astype(np.int64) if ops0 is not None \
        else ops.astype(np.int64)
    tot = d.sum(axis=0)
    sub = int(tot[OP_NAMES.index("subtick")])
    active = (d > 0).mean(axis=0)
    print(f"op-trace (timed loop only, {sub} executed env-subticks):")
    for i, name in enumerate(OP_NAMES):
        print(f"  {name:12s} {int(tot[i]):>14,}  "
              f"{tot[i]/max(sub, 1):>10.3f}/subtick  "
              f"active {100.0 * active[i]:5.1f}% of envs")
    if not write:
        return
    path = os.path.join(RL, "out",
                        f"op_trace_{'t0' if args.t0 else 'curr'}"
                        f"_n{args.n}.json")
    with open(path, "w") as f:
        json.dump({"n": args.n, "decisions": args.decisions,
                   "repeat": args.repeat, "t0": bool(args.t0),
                   "op_names": list(OP_NAMES),
                   "totals": [int(x) for x in tot],
                   "per_subtick": [float(t / max(sub, 1)) for t in tot],
                   "active_frac": [float(x) for x in active],
                   "per_env": d.tolist()}, f)
    print(f"op-trace written: {path}")


def dump_phase_window(env, ph0, wall_s, n, ndec, tag):
    """Print mean-env cycle share of each k_tick phase for one window."""
    from blaze import PHASE_NAMES
    ph = env.copy_phase()
    if ph is None:
        print(f"phase {tag}: unavailable (stage_time was not set at create)")
        return ph
    d = (ph - ph0).astype(np.int64) if ph0 is not None else ph.astype(np.int64)
    tot = d.sum(axis=0)
    s = int(tot.sum())
    print(f"phase {tag} wall={wall_s*1000:.1f}ms "
          f"({wall_s*1000/max(ndec,1):.2f} ms/dec, N={n}):")
    if s <= 0:
        print("  (empty)")
        return ph
    for i, name in enumerate(PHASE_NAMES):
        if i >= len(tot):
            break
        share = tot[i] / s
        print(f"  {name:10s} {share*100:6.1f}%  "
              f"{wall_s*1000*share:8.1f} ms  cycles={int(tot[i])}")
    return ph


def run_bench(args):
    import torch
    paths = snap_paths(t0=args.t0, snaps=args.snaps)
    n = args.n
    free_b, total_b = torch.cuda.mem_get_info(args.device)
    print(f"bench: N={n}, repeat {args.repeat}, {args.decisions} decisions "
          f"(+{args.warmup} warmup), device cuda:{args.device}, "
          f"{'t0 (full action decode)' if args.t0 else 'curriculum'} "
          f"snapshots, VRAM free {free_b/1e9:.1f}/{total_b/1e9:.1f} GB")
    env = VecBlaze(n, device=args.device, so_path=CUDA_SO, **_create_kw(args))
    env.load_snapshots(paths)
    env.assign([i % len(paths) for i in range(n)])
    env.reset()
    dev = torch.device(f"cuda:{args.device}")
    g = torch.Generator(device="cpu").manual_seed(7)

    def rand_actions_5h():
        a = torch.empty((n, 5), dtype=torch.int32)
        a[:, 0] = torch.randint(0, 3, (n,), generator=g)
        a[:, 1] = torch.randint(0, 3, (n,), generator=g)
        a[:, 2] = (torch.randint(0, 4, (n,), generator=g) != 0).int()
        a[:, 3] = (torch.randint(0, 8, (n,), generator=g) == 0).int()
        a[:, 4] = (torch.randint(0, 4, (n,), generator=g) != 3).int()
        return a.to(dev)

    def rand_actions_full():
        def ri(hi):
            return torch.randint(0, hi, (n,), generator=g)
        a = torch.zeros((n, 13), dtype=torch.float64)
        a[:, 0] = torch.tensor((0.0, 0.5, 1.0, 1.0))[ri(4)]
        a[:, 1] = torch.tensor((-1.0, 0.0, 0.0, 1.0))[ri(4)]
        a[:, 2] = (ri(61) - 30) * 0.5
        a[:, 3] = (ri(41) - 20) * 0.5
        a[:, 4] = (ri(8) == 0).double()
        a[:, 5] = (ri(16) == 0).double()
        a[:, 6] = (ri(8) == 0).double()
        a[:, 7] = (ri(4) != 3).double()
        a[:, 8] = (ri(8) == 0).double()
        hv = ri(27)
        a[:, 9] = torch.where(hv < 9, hv.double(), torch.tensor(-1.0))
        cv = ri(96)
        a[:, 10] = torch.where(cv < 6, cv.double(), torch.tensor(-1.0))
        a[:, 11] = (ri(32) == 0).double()
        return a.to(dev)

    rand_actions = rand_actions_full if args.t0 else rand_actions_5h

    for _ in range(args.warmup):
        env.step(rand_actions(), repeat=args.repeat)
    ops0 = env.op_trace() if args.op_trace else None
    ops_win = ops0
    ph_win = env.copy_phase() if args.stage_time else None
    # pre-generate every decision's actions on-device so the timed loop
    # measures the env, not torch's host RNG + H2D copies (the trainer
    # produces its actions on the GPU)
    acts = [rand_actions() for _ in range(args.decisions)]
    # periodic masked resets keep envs live (a done env's tick is a no-op,
    # which would flatter the numbers)
    win = 8
    t0 = time.perf_counter()
    t_win = t0
    for d in range(args.decisions):
        env.step(acts[d], repeat=args.repeat)
        if (d + 1) % 25 == 0:
            done = env.done.cpu().numpy()
            if done.any():
                env.reset(done.astype(np.uint8))
        if (d + 1) % win == 0 or d + 1 == args.decisions:
            t_now = time.perf_counter()
            nd = (d % win) + 1 if (d + 1) % win else win
            if d + 1 == args.decisions and (d + 1) % win:
                nd = (d + 1) % win
            tag = f"d{d+1-nd+1}-{d+1}"
            if args.stage_time:
                ph_win = dump_phase_window(env, ph_win, t_now - t_win, n, nd,
                                           tag)
            if args.op_trace:
                print(f"op-trace {tag}:")
                dump_op_trace(env, ops_win, args, write=False)
                ops_win = env.op_trace()
            t_win = t_now
    t1 = time.perf_counter()
    dt = t1 - t0
    ticks = n * args.decisions * args.repeat
    print(f"N={n}: {ticks/dt/1e6:.2f}M env-ticks/s  "
          f"{n*args.decisions/dt:.0f} decisions/s  "
          f"({dt:.2f}s for {args.decisions} decisions, "
          f"{int((env.done.cpu().numpy() != 0).sum())}/{n} done at end)")
    if args.op_trace:
        dump_op_trace(env, ops0, args)
    env.close()
    return True


def build_parser():
    """The CLI, split out of main() so tests can build a default args
    namespace without going through sys.argv."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--big", action="store_true")
    ap.add_argument("--chain", action="store_true",
                    help="full spawn-to-torch chain gate (CUDA lanes vs CPU)")
    ap.add_argument(
        "--tape",
        help="with --chain/--iron: override the action-stream JSON path "
             "(default: rl/out/{chain,iron}_actions_s{seed}.json)")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument(
        "--expected-chain-actions", type=int,
        help="fail closed unless --chain loads exactly this many actions")
    ap.add_argument("--snapshot",
                    help="explicit chain-compatible .bsnp parity fixture")
    ap.add_argument("--chain-lanes", type=int, default=64)
    ap.add_argument("--iron", action="store_true",
                    help="iron-stage gate (furnace/smelt/craft:6,7; "
                         "make_iron_actions.py artifacts) - CUDA lanes vs CPU")
    ap.add_argument("--mixed", action="store_true",
                    help="big-N full-action gate on t0 snapshots")
    ap.add_argument(
        "--port-parity", action="store_true",
        help="with --chain/--iron, also compare the full PARY state record "
             "for an explicit --features list and require fixture evidence")
    ap.add_argument(
        "--features",
        help="comma-separated PARY subsystems whose evidence is required "
             "(implies state digests; requires --port-parity)")
    ap.add_argument(
        "--no-state-digest", action="store_true",
        help="opt out of the default-on per-tick PARY state-digest pass "
             "on --chain/--iron (BOLR-only)")
    ap.add_argument(
        "--strict-capabilities", action="store_true",
        help="BLOCKED if requested or snapshot-required capabilities are absent")
    ap.add_argument(
        "--mobs-on", action="store_true",
        help="enable blaze hostile AI (mobs row; magma --set mobs=1 is CPU-only)")
    ap.add_argument(
        "--natural-spawn", action="store_true",
        help="enable WorldEntitySpawner + pin worldTime=18000")
    ap.add_argument(
        "--natural-spawn-passive", action="store_true",
        help="enable CREATURE WorldEntitySpawner + pin worldTime=6000")
    ap.add_argument("--bench", action="store_true")
    ap.add_argument("--t0", action="store_true",
                    help="bench on t0 snapshots with full action decode")
    ap.add_argument("--snaps", default=None,
                    help="snapshot directory for --bench (default RL/out/snaps)")
    ap.add_argument("--n", type=int, default=4096)
    ap.add_argument("--decisions", type=int, default=250)
    ap.add_argument("--repeat", type=int, default=4)
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument(
        "--cpu-workers", type=int,
        default=min(16, os.cpu_count() or 1),
        help="number of CPU worker processes for replica lanes (default: "
             "min(16, os.cpu_count()), 1=single-process VecBlaze)")
    ap.add_argument(
        "--phase-time", action="store_true",
        help="with --mixed: print actgen/step/obs/cmp/sha wall seconds")
    ap.add_argument("--ktime", action="store_true",
                    help="print per-kernel timings at destroy")
    ap.add_argument("--op-trace", action="store_true",
                    help="bench only: per-env op activity counters -> "
                         "histogram + rl/out/op_trace_*.json "
                         "(passes op_trace=1 to blaze_create)")
    ap.add_argument("--stage-time", action="store_true",
                    help="enable k_tick stage cycle counters at create")
    ap.add_argument("--legacy-recenter", action="store_true",
                    help="REMOVED: k_tick_legacy is deleted. blaze_create "
                         "fails when this is set. Kept so an old command "
                         "line reports the removal instead of a parse error.")
    ap.add_argument("--no-ore-xy", action="store_true",
                    help="skip ore spatial index at snapshot load")
    ap.add_argument("--warp-tick", type=int, default=None,
                    help="1=warp-per-env (default), 0=flat k_tick")
    ap.add_argument("--stack-kib", type=int, default=None,
                    help="CUDA per-thread stack limit in KiB (default 128)")
    ap.add_argument(
        "--m2-kernel", choices=("raw", "warp", "scalar"), default="raw",
        help="focused M2 tick kernel: raw=blaze_tick_raw/k_tick_raw "
             "(default), warp=blaze_tick+k_tick_warp, scalar=blaze_tick+k_tick. "
             "Create-time warp_tick comes from blaze_abi.h / blaze.conf "
             "(default 1). No env vars. scalar needs a .so built with "
             "BLAZE_SCALAR_TICK=1; the default build refuses warp_tick=0.")
    ap.add_argument("--no-parity-all", action="store_true",
                    help="force per-lane blaze_parity_state (A/B vs batched)")
    return ap


def build_args(argv=None):
    """Parsed args plus the derived fields main() sets up, so a test can call
    run_chain() directly with exactly the namespace the CLI would produce."""
    ap = build_parser()
    args = ap.parse_args(argv)
    args.parity_blocked = False
    args.parity_features = []
    if args.port_parity and not (args.chain or args.iron):
        ap.error("--port-parity requires --chain or --iron")
    if args.snapshot and not (args.chain or args.iron):
        ap.error("--snapshot requires --chain or --iron")
    if args.expected_chain_actions is not None and not args.chain:
        ap.error("--expected-chain-actions requires --chain")
    if args.tape is not None and not (args.chain or args.iron):
        ap.error("--tape requires --chain or --iron")
    if args.features is not None:
        if not args.port_parity:
            ap.error("--features requires --port-parity")
        args.parity_features = [
            name.strip() for name in args.features.split(",") if name.strip()
        ]
        unknown = sorted(set(args.parity_features) - set(PARITY_NAMES))
        if unknown:
            ap.error("unknown parity feature(s): " + ",".join(unknown))
        args.parity_features = list(dict.fromkeys(args.parity_features))
    if args.strict_capabilities and not args.parity_features:
        ap.error("--strict-capabilities requires --features")
    return args


def main():
    args = build_args()
    t0 = time.perf_counter()
    if args.bench:
        ok = run_bench(args)
    elif args.iron:
        ok = run_chain(args, iron=True)
    elif args.chain:
        ok = run_chain(args)
    elif args.mixed:
        ok = run_mixed(args)
    elif args.big:
        ok = run_big(args)
    else:
        ok = run_gate(args)
    print(f"runtime: {time.perf_counter() - t0:.2f}s")
    sys.exit(3 if args.parity_blocked else (0 if ok else 1))


if __name__ == "__main__":
    main()
