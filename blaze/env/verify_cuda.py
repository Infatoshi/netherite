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
craft:N + interact) via the raw-tick path (blaze_tick_raw env=-1 broadcast).
EVERY tick, EVERY lane's full BOLR record must match the batch-of-1 CPU
blaze record byte-for-byte (the CPU env is itself byte-exact vs the real
game per verify_cpu.py --chain) - one loop covers both the CPU==CUDA chain
gate and the 64-identical-lanes cross-env-interference check.

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
from blaze import VecBlaze, CPU_SO, CUDA_SO, NPIX  # noqa: E402
from verify_cpu import BIN_SIZE, first_diff_field, fmt_field  # noqa: E402

RL = os.path.join(os.path.dirname(HERE), "rl")
SNAPS = os.path.join(RL, "out", "snaps")
REL_GATE = 1e-12


def snap_paths(t0=False):
    """Curriculum d-stage snapshots (64x128x64) by default; t0=True selects
    the fresh-spawn 128^3 *_t0.bsnp set. The two have different region dims
    and cannot share one env's region pools."""
    paths = sorted(glob.glob(os.path.join(SNAPS, "*.bsnp")))
    # the t0 family (128^3) includes the iron-stage bake s*_t0_iron.bsnp
    paths = [p for p in paths
             if ("_t0" in os.path.basename(p)) == bool(t0)]
    if not paths:
        raise SystemExit(f"no {'t0 ' if t0 else 'curriculum '}snapshots in "
                         f"{SNAPS} (run make_snapshots.py)")
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


def outputs(env):
    return {"cam": to_np(env.cam), "depth": to_np(env.depth),
            "edge": to_np(env.edge), "scal": to_np(env.scal),
            "rew": to_np(env.rew), "done": to_np(env.done),
            "pose": to_np(env.pose)}


def make_envs(n_cpu, n_cuda, paths, cpu_assign, cuda_assign, device):
    cpu = VecBlaze(n_cpu, device=0, so_path=CPU_SO)
    cuda = VecBlaze(n_cuda, device=device, so_path=CUDA_SO)
    for e, asn in ((cpu, cpu_assign), (cuda, cuda_assign)):
        e.load_snapshots(paths)
        e.assign(asn)
        e.reset()
    return cpu, cuda


def run_gate(args):
    import torch
    paths = snap_paths()
    n = 64
    assign = [i % len(paths) for i in range(n)]
    print(f"gate: N={n}, {len(paths)} snapshots round-robin, "
          f"{args.decisions} decisions x repeat {args.repeat}")
    cpu, cuda = make_envs(n, n, paths, assign, assign, args.device)
    streams = [ActStream(i) for i in range(n)]
    cmp = Cmp()
    ok = True
    for d in range(args.decisions):
        acts = np.array([s.next_action() for s in streams], dtype=np.int32)
        cpu.step(acts, repeat=args.repeat)
        cuda.step(torch.as_tensor(acts).to(f"cuda:{args.device}"),
                  repeat=args.repeat)
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
    import torch
    paths = snap_paths()
    nbig, nsub = args.n, 64
    lanes = sorted(random.Random(1234).sample(range(nbig), nsub))
    assign_big = [i % len(paths) for i in range(nbig)]
    assign_sub = [assign_big[l] for l in lanes]
    print(f"big-N spot check: CUDA N={nbig}, {args.decisions} decisions; "
          f"CPU exact-replica of {nsub} random lanes (seed 1234)")
    cpu, cuda = make_envs(nsub, nbig, paths, assign_sub, assign_big,
                          args.device)
    big_streams = [ActStream(i) for i in range(nbig)]
    cmp = Cmp()
    ok = True
    for d in range(args.decisions):
        acts = np.array([s.next_action() for s in big_streams],
                        dtype=np.int32)
        cpu.step(acts[lanes], repeat=args.repeat)
        cuda.step(torch.as_tensor(acts).to(f"cuda:{args.device}"),
                  repeat=args.repeat)
        a, b = outputs(cpu), outputs(cuda)
        if not cmp.compare(d, a, b, lanes=np.array(lanes)):
            print(f"FAIL {cmp.fail}")
            ok = False
            break
        if (d + 1) % 50 == 0:
            print(f"  decision {d+1}: {nsub} lanes identical so far "
                  f"({int(b['done'][lanes].sum())} of them done)")
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
    env.lib.blaze_emit.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_void_p]
    env.lib.blaze_obs_size.restype = ctypes.c_int
    assert env.lib.blaze_obs_size() == BIN_SIZE


def _raw_act(act):
    return (ctypes.c_double * 13)(
        act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
        act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
        act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
        act.get("hotbar", -1), act.get("craft", -1), act.get("interact", 0),
        act.get("smelt", 0))


def run_chain(args, iron=False):
    seed = args.chain_seed
    tag = "iron" if iron else "chain"
    snap = os.path.join(SNAPS, f"s{seed}_t0_iron.bsnp" if iron
                        else f"s{seed}_t0.bsnp")
    acts_path = os.path.join(RL, "out", f"{tag}_actions_s{seed}.json")
    if not os.path.exists(snap) or not os.path.exists(acts_path):
        print(f"{tag} gate: missing {snap} or {acts_path}")
        return False
    acts = [{k: v for k, v in a.items() if k != "snapshot"}
            for a in json.load(open(acts_path))]
    nl = args.chain_lanes
    print(f"{tag} gate: {os.path.basename(snap)} x {len(acts)} actions, "
          f"CUDA batch of "
          f"{nl} identical lanes vs batch-of-1 CPU, byte-exact every tick")
    cpu = VecBlaze(1, device=0, so_path=CPU_SO)
    cuda = VecBlaze(nl, device=args.device, so_path=CUDA_SO)
    for e, n in ((cpu, 1), (cuda, nl)):
        _raw_abi(e)
        e.load_snapshots([snap])
        e.assign([0] * n)
        e.reset()
    buf_c = ctypes.create_string_buffer(BIN_SIZE)
    buf_g = ctypes.create_string_buffer(BIN_SIZE)
    ok = True

    def lanes_match(t, want):
        nonlocal ok
        for lane in range(nl):
            assert cuda.lib.blaze_emit(cuda.h, lane, want, buf_g) == 0
            if buf_g.raw != buf_c.raw:
                f = first_diff_field(buf_c.raw, buf_g.raw) or "blocks/logs"
                print(f"FAIL tick {t} lane {lane}: field {f}")
                if f != "blocks/logs":
                    print(f"    cpu:  {fmt_field(buf_c.raw, f)}")
                    print(f"    cuda: {fmt_field(buf_g.raw, f)}")
                ok = False
                return False
        return True

    assert cpu.lib.blaze_emit(cpu.h, 0, 1, buf_c) == 0
    if lanes_match("initial", 1):
        for t, act in enumerate(acts):
            want = act.get("cam", 1)
            a = _raw_act(act)
            assert cpu.lib.blaze_tick_raw(cpu.h, 0, a, want, buf_c) == 0
            assert cuda.lib.blaze_tick_raw(cuda.h, -1, a, 0, None) == 0
            if not lanes_match(t, want):
                break
            if (t + 1) % 500 == 0:
                print(f"  tick {t+1}: {nl} lanes byte-exact so far")
    print(("PASS" if ok else "FAIL") +
          f": {tag} s{seed} x {len(acts)} ticks, {nl} CUDA lanes vs CPU "
          f"byte-exact (full BOLR record, every tick)")
    cpu.close(); cuda.close()
    return ok


def run_mixed(args):
    import torch
    paths = snap_paths(t0=True)
    nbig, nsub = args.n, 64
    lanes = sorted(random.Random(1234).sample(range(nbig), nsub))
    assign_big = [i % len(paths) for i in range(nbig)]
    assign_sub = [assign_big[l] for l in lanes]
    print(f"mixed big-N FULL-action gate: CUDA N={nbig} on {len(paths)} t0 "
          f"snapshots, {args.decisions} decisions; CPU exact-replica of "
          f"{nsub} random lanes (seed 1234)")
    cpu, cuda = make_envs(nsub, nbig, paths, assign_sub, assign_big,
                          args.device)
    big_streams = [ActStream(i) for i in range(nbig)]
    cmp = Cmp()
    ok = True
    for d in range(args.decisions):
        acts = np.array([s.next_full() for s in big_streams],
                        dtype=np.float64)
        cpu.step(acts[lanes], repeat=args.repeat)
        cuda.step(torch.as_tensor(acts).to(f"cuda:{args.device}"),
                  repeat=args.repeat)
        a, b = outputs(cpu), outputs(cuda)
        if not cmp.compare(d, a, b, lanes=np.array(lanes)):
            print(f"FAIL {cmp.fail}")
            ok = False
            break
        if (d + 1) % 50 == 0:
            print(f"  decision {d+1}: {nsub} lanes identical so far "
                  f"({int(b['done'][lanes].sum())} of them done)")
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
          f": mixed N={nbig}, {nsub} lanes exact vs CPU over "
          f"{args.decisions} decisions (full action set)")
    cpu.close(); cuda.close()
    return ok


def dump_op_trace(env, ops0, args):
    """Print the op histogram + per-env activity fractions and dump the full
    per-env counter matrix (bit-level zoom traces: single-env histogram at
    --n 1, cross-env activity correlation input at big N)."""
    from blaze import OP_NAMES
    ops = env.op_trace()
    if ops is None:
        print("op-trace: unavailable (BLAZE_OP_TRACE was not set at create)")
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


def run_bench(args):
    import torch
    paths = snap_paths(t0=args.t0)
    n = args.n
    free_b, total_b = torch.cuda.mem_get_info(args.device)
    print(f"bench: N={n}, repeat {args.repeat}, {args.decisions} decisions "
          f"(+{args.warmup} warmup), device cuda:{args.device}, "
          f"{'t0 (full action decode)' if args.t0 else 'curriculum'} "
          f"snapshots, VRAM free {free_b/1e9:.1f}/{total_b/1e9:.1f} GB")
    env = VecBlaze(n, device=args.device, so_path=CUDA_SO)
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
    # pre-generate every decision's actions on-device so the timed loop
    # measures the env, not torch's host RNG + H2D copies (the trainer
    # produces its actions on the GPU)
    acts = [rand_actions() for _ in range(args.decisions)]
    # periodic masked resets keep envs live (a done env's tick is a no-op,
    # which would flatter the numbers)
    t0 = time.perf_counter()
    for d in range(args.decisions):
        env.step(acts[d], repeat=args.repeat)
        if (d + 1) % 25 == 0:
            done = env.done.cpu().numpy()
            if done.any():
                env.reset(done.astype(np.uint8))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--big", action="store_true")
    ap.add_argument("--chain", action="store_true",
                    help="full spawn-to-torch chain gate (CUDA lanes vs CPU)")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument("--chain-lanes", type=int, default=64)
    ap.add_argument("--iron", action="store_true",
                    help="iron-stage gate (furnace/smelt/craft:6,7; "
                         "make_iron_actions.py artifacts) - CUDA lanes vs CPU")
    ap.add_argument("--mixed", action="store_true",
                    help="big-N full-action gate on t0 snapshots")
    ap.add_argument("--bench", action="store_true")
    ap.add_argument("--t0", action="store_true",
                    help="bench on t0 snapshots with full action decode")
    ap.add_argument("--n", type=int, default=4096)
    ap.add_argument("--decisions", type=int, default=250)
    ap.add_argument("--repeat", type=int, default=4)
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument("--ktime", action="store_true",
                    help="print per-kernel timings at destroy")
    ap.add_argument("--op-trace", action="store_true",
                    help="bench only: per-env op activity counters -> "
                         "histogram + rl/out/op_trace_*.json "
                         "(implies BLAZE_OP_TRACE=1)")
    args = ap.parse_args()
    if args.ktime:
        os.environ["BLAZE_KTIME"] = "1"
    if os.environ.get("BLAZE_OP_TRACE", "0") not in ("", "0"):
        args.op_trace = True
    if args.op_trace:
        os.environ["BLAZE_OP_TRACE"] = "1"
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
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
