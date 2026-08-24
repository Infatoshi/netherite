#!/usr/bin/env python3
"""Continuous-vs-resume BP_ digest gate.

Takes a chain (or random-tick) row: run N ticks, dump a mid-episode .bsnp,
restore into a fresh env, run M more ticks, and compare every BP_ digest at
ticks N..N+M against the continuous run at the same ticks.

Backends:
  magma  - magma_game --snapshot-in dump via the snapshot action key
  blaze  - blaze_cpu.so load of the same dump
  cuda   - blaze_cuda.so load of the same dump (--cuda)

The dump is magma's pre-tick write at action index N (snapshot_bounds=inherit).
Blaze has no live .bsnp writer on the verify ABI; magma dump is the format.

Usage:
  uv run --no-project --with numpy python blaze/env/verify_resume_parity.py \\
      --chain --snapshot PATH --tape PATH --features player,mobs --mobs-on
  uv run --no-project --with numpy python blaze/env/verify_resume_parity.py \\
      --snapshot PATH --seeds 14 --ticks 1000 --features player,dig,inventory
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blaze import CUDA_SO  # noqa: E402
from verify_cpu import (  # noqa: E402
    BIN,
    BLOCKED,
    FAILED,
    PARITY_INDEX,
    PARITY_NAMES,
    SNAPS,
    SO,
    VERIFIED,
    Blaze1,
    Rng,
    RealEnv,
    rand_action,
    snapshot_dynamics_blocker,
)
from verify_resume import (  # noqa: E402
    _SNAP_ACTION_KEYS,
    require_v2_snapshot,
    snap_version,
)

DEFAULT_TAIL = 32


def _strip_snap(act):
    return {k: v for k, v in act.items() if k not in _SNAP_ACTION_KEYS}


def magma_extras(features, mobs_on, natural_spawn, natural_spawn_passive,
                 pin_time=True):
    extra = []
    if "weather" in features:
        extra.extend(["--weather", "on"])
    if "elytra" in features:
        extra.extend(["--set", "elytra=1"])
    if mobs_on:
        extra.extend(["--set", "mobs=1"])
    if natural_spawn:
        extra.extend(["--set", "natural_spawn=1"])
        if pin_time:
            extra.extend(["--set", "set_time=18000"])
    if natural_spawn_passive:
        extra.extend(["--set", "natural_spawn_passive=1"])
        if pin_time:
            extra.extend(["--set", "set_time=6000"])
    return extra


def configure_blaze(cu, features, mobs_on, natural_spawn,
                    natural_spawn_passive, pin_time=True):
    """Match verify_cpu.py run_seed_parity flag application (post-reset).

    pin_time=False on resume: natural_spawn stays on so spawn still runs,
    but worldTime is not reset to the t0 night/day pin.
    """
    lib = cu.lib
    h = ctypes.c_void_p(cu.h)
    if "elytra" in features:
        lib.blaze_set_elytra_enabled.argtypes = [
            ctypes.c_void_p, ctypes.c_int]
        lib.blaze_set_elytra_enabled.restype = ctypes.c_int
        if lib.blaze_set_elytra_enabled(h, 1) != 0:
            raise RuntimeError("blaze_set_elytra_enabled failed")
    if mobs_on:
        lib.blaze_set_mobs_enabled.argtypes = [
            ctypes.c_void_p, ctypes.c_int]
        lib.blaze_set_mobs_enabled.restype = ctypes.c_int
        if lib.blaze_set_mobs_enabled(h, 1) != 0:
            raise RuntimeError("blaze_set_mobs_enabled failed")
    if natural_spawn:
        lib.blaze_set_natural_spawn.argtypes = [
            ctypes.c_void_p, ctypes.c_int]
        lib.blaze_set_natural_spawn.restype = ctypes.c_int
        if lib.blaze_set_natural_spawn(h, 1) != 0:
            raise RuntimeError("blaze_set_natural_spawn failed")
        if pin_time:
            lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            lib.blaze_set_world_time.restype = ctypes.c_int
            if lib.blaze_set_world_time(h, 18000) != 0:
                raise RuntimeError("blaze_set_world_time failed")
    if natural_spawn_passive:
        lib.blaze_set_natural_spawn_passive.argtypes = [
            ctypes.c_void_p, ctypes.c_int]
        lib.blaze_set_natural_spawn_passive.restype = ctypes.c_int
        if lib.blaze_set_natural_spawn_passive(h, 1) != 0:
            raise RuntimeError("blaze_set_natural_spawn_passive failed")
        if pin_time:
            lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            lib.blaze_set_world_time.restype = ctypes.c_int
            if lib.blaze_set_world_time(h, 6000) != 0:
                raise RuntimeError("blaze_set_world_time failed")


def make_blaze(snap, features, mobs_on, natural_spawn, natural_spawn_passive,
               so_path=None, device=0, pin_time=True):
    cu = Blaze1(snap, port_parity=True, so_path=so_path, device=device)
    configure_blaze(cu, features, mobs_on, natural_spawn,
                    natural_spawn_passive, pin_time=pin_time)
    return cu


def pick_nm(n_acts, n_arg, m_arg):
    if n_acts < 2:
        raise SystemExit("need at least 2 actions for resume")
    if m_arg is not None and n_arg is not None:
        n, m = n_arg, m_arg
    elif m_arg is not None:
        m = m_arg
        n = n_acts - m
    elif n_arg is not None:
        n = n_arg
        m = min(DEFAULT_TAIL, n_acts - n)
    else:
        m = min(DEFAULT_TAIL, max(1, n_acts // 4))
        n = n_acts - m
    if n < 1 or m < 1 or n + m > n_acts:
        raise SystemExit(
            f"bad N={n} M={m} for {n_acts} actions "
            f"(need 1 <= N, 1 <= M, N+M <= n_acts)")
    return n, m


def digest_diffs(a, b):
    out = []
    for name in PARITY_NAMES:
        i = PARITY_INDEX[name]
        if a.digest[i] != b.digest[i]:
            out.append((name, i))
    return out


def compare_parity_slice(label, cont, other, base_tick, start_i=0):
    """Compare other[k] vs cont[k] for k in start_i..len-1.

    start_i=1 is the spec window N+1..N+M (skip the restore observation).
    """
    if len(other) != len(cont):
        return FAILED, (
            f"{label}: length mismatch continuous={len(cont)} "
            f"other={len(other)}")
    for k in range(start_i, len(cont)):
        a, b = cont[k], other[k]
        diffs = digest_diffs(a, b)
        if not diffs:
            continue
        tick = base_tick + k
        name, idx = diffs[0]
        extra = ""
        if len(diffs) > 1:
            extra = " also " + ",".join(n for n, _ in diffs[1:])
        lines = [
            f"{label}: FIRST DIFF tick {tick} "
            f"(env continuous={a.tick} resumed={b.tick}) "
            f"subsystem {name}{extra}",
        ]
        for dname, didx in diffs:
            lines.append(
                f"    {dname}: continuous=0x{a.digest[didx]:016x} "
                f"ev={a.evidence[didx]} resumed=0x{b.digest[didx]:016x} "
                f"ev={b.evidence[didx]}")
        return FAILED, "\n".join(lines)
    return VERIFIED, (
        f"{label}: {len(cont) - start_i} ticks BP_ exact "
        f"(ticks {base_tick + start_i}..{base_tick + len(cont) - 1})")


def load_actions(args):
    seed = args.chain_seed
    snap = args.snapshot
    if args.chain:
        seed = args.chain_seed
        snap = args.snapshot or os.path.join(SNAPS, f"s{seed}_t0.bsnp")
        if args.tape:
            acts_path = args.tape
        else:
            acts_path = os.path.join(
                os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                "rl", "out", f"chain_actions_s{seed}.json")
        if not os.path.isfile(snap) or not os.path.isfile(acts_path):
            print(f"BLOCKED: missing {snap} or {acts_path}")
            return None, None, None, BLOCKED
        with open(acts_path) as handle:
            acts = json.load(handle)
        acts = [_strip_snap(a) for a in acts]
        if (args.expected_chain_actions is not None and
                len(acts) != args.expected_chain_actions):
            print("BLOCKED: chain has "
                  f"{len(acts)} actions, expected "
                  f"{args.expected_chain_actions}")
            return None, None, None, BLOCKED
    else:
        if not snap:
            print("BLOCKED: --snapshot required without --chain")
            return None, None, None, BLOCKED
        seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
        if len(seeds) != 1:
            print("BLOCKED: resume gate needs exactly one --seeds value")
            return None, None, None, BLOCKED
        seed = seeds[0]
        rng = Rng(0xC0A1 ^ (seed * 2654435761 & 0xFFFFFFFF))
        acts = [rand_action(rng) for _ in range(args.ticks)]
    blocker = snapshot_dynamics_blocker(snap)
    if blocker:
        print(f"BLOCKED: {blocker}")
        return None, None, None, BLOCKED
    require_v2_snapshot(snap, "resume t0")
    return seed, snap, acts, None


def run_magma_continuous(seed, snap, acts, n, m, extra, dump_path):
    real = RealEnv(seed, snap, port_parity=True, magma_args=extra)
    pars = [real.parity_rec]
    try:
        for t, act in enumerate(acts[: n + m]):
            a = dict(act)
            if t == n:
                a["snapshot"] = dump_path
                a["snapshot_bounds"] = "inherit"
            real.step(a)
            pars.append(real.parity_rec)
    finally:
        real.close()
    if not os.path.isfile(dump_path) or os.path.getsize(dump_path) < 8:
        raise RuntimeError(f"magma dump missing: {dump_path}")
    require_v2_snapshot(dump_path, f"continuous write at N={n}")
    return pars


def run_magma_resume(seed, dump_path, acts_tail, extra):
    real = RealEnv(seed, dump_path, port_parity=True, magma_args=extra)
    pars = [real.parity_rec]
    try:
        for act in acts_tail:
            real.step(_strip_snap(act))
            pars.append(real.parity_rec)
    finally:
        real.close()
    return pars


def dump_blaze(cu, path):
    lib = cu.lib
    lib.blaze_dump_snapshot.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
        ctypes.c_char_p, ctypes.c_int]
    lib.blaze_dump_snapshot.restype = ctypes.c_int
    err = ctypes.create_string_buffer(256)
    r = lib.blaze_dump_snapshot(
        ctypes.c_void_p(cu.h), 0, path.encode(), err, 256)
    if r != 0:
        raise RuntimeError(
            f"blaze_dump_snapshot: {err.value.decode() or 'rc=' + str(r)}")


def run_blaze_continuous(snap, acts, n_plus_m, features, mobs_on,
                         natural_spawn, natural_spawn_passive, so_path=None,
                         dump_path=None, dump_at=None):
    cu = make_blaze(snap, features, mobs_on, natural_spawn,
                    natural_spawn_passive, so_path=so_path)
    pars = [cu.parity()]
    try:
        for t, act in enumerate(acts[:n_plus_m]):
            if dump_path is not None and dump_at is not None and t == dump_at:
                dump_blaze(cu, dump_path)
            cu.step(_strip_snap(act))
            pars.append(cu.parity())
    finally:
        cu.close()
    return pars


def run_blaze_resume(dump_path, acts_tail, features, mobs_on,
                     natural_spawn, natural_spawn_passive, so_path=None):
    # Spawn knobs stay on; do not re-pin worldTime to the t0 night/day value.
    cu = make_blaze(dump_path, features, mobs_on, natural_spawn,
                    natural_spawn_passive, so_path=so_path, pin_time=False)
    pars = [cu.parity()]
    try:
        for act in acts_tail:
            cu.step(_strip_snap(act))
            pars.append(cu.parity())
    finally:
        cu.close()
    return pars


def run_one(args):
    features = [
        name.strip() for name in (args.features or "").split(",")
        if name.strip()
    ]
    seed, snap, acts, blocked = load_actions(args)
    if blocked is not None:
        return blocked
    n, m = pick_nm(len(acts), args.n, args.m)
    extra = magma_extras(
        features, args.mobs_on, args.natural_spawn,
        args.natural_spawn_passive, pin_time=True)
    extra_resume = magma_extras(
        features, args.mobs_on, args.natural_spawn,
        args.natural_spawn_passive, pin_time=False)
    backends = []
    if args.cuda:
        backends.append("cuda")
    else:
        backends.append("magma")
        backends.append("blaze")
    print(f"resume parity: seed={seed} actions={len(acts)} N={n} M={m} "
          f"backends={','.join(backends)} features={','.join(features) or '-'}")
    print(f"  snap={snap}")
    t0 = time.time()
    any_fail = False
    with tempfile.TemporaryDirectory(prefix="resume_parity_") as tmp:
        dump_path = os.path.join(tmp, f"ck_{n}.bsnp")
        if "magma" in backends or "blaze" in backends or "cuda" in backends:
            print(f"  magma continuous + dump N={n} -> {dump_path}")
            magma_cont = run_magma_continuous(
                seed, snap, acts, n, m, extra, dump_path)
            print(f"    dump version={snap_version(dump_path)} "
                  f"size={os.path.getsize(dump_path)}")
        if "magma" in backends:
            magma_res = run_magma_resume(
                seed, dump_path, acts[n:n + m], extra_resume)
            st, msg = compare_parity_slice(
                "magma continuous-vs-resume",
                magma_cont[n:], magma_res, n, start_i=1)
            print(("  PASS " if st == VERIFIED else "  FAIL ") + msg)
            if st != VERIFIED:
                any_fail = True
        if "blaze" in backends:
            if not os.path.isfile(SO):
                print(f"  FAIL missing {SO}")
                return FAILED
            blaze_ck = os.path.join(tmp, f"ck_{n}_blaze.bsnp")
            print("  blaze-cpu continuous + dump")
            blaze_cont = run_blaze_continuous(
                snap, acts, n + m, features, args.mobs_on,
                args.natural_spawn, args.natural_spawn_passive, so_path=SO,
                dump_path=blaze_ck, dump_at=n)
            if not os.path.isfile(blaze_ck):
                print(f"  FAIL missing blaze dump {blaze_ck}")
                return FAILED
            print("  blaze-cpu resume from blaze dump")
            blaze_res = run_blaze_resume(
                blaze_ck, acts[n:n + m], features, args.mobs_on,
                args.natural_spawn, args.natural_spawn_passive, so_path=SO)
            st, msg = compare_parity_slice(
                "blaze-cpu continuous-vs-resume",
                blaze_cont[n:], blaze_res, n, start_i=1)
            print(("  PASS " if st == VERIFIED else "  FAIL ") + msg)
            if st != VERIFIED:
                any_fail = True
        if "cuda" in backends:
            if not os.path.isfile(CUDA_SO):
                print(f"  FAIL missing {CUDA_SO}")
                return FAILED
            print("  blaze-cuda continuous + dump")
            cuda_ck = os.path.join(tmp, f"ck_{n}_cuda.bsnp")
            cuda_cont = run_blaze_continuous(
                snap, acts, n + m, features, args.mobs_on,
                args.natural_spawn, args.natural_spawn_passive,
                so_path=CUDA_SO, dump_path=cuda_ck, dump_at=n)
            print("  blaze-cuda resume from cuda dump")
            cuda_res = run_blaze_resume(
                cuda_ck, acts[n:n + m], features, args.mobs_on,
                args.natural_spawn, args.natural_spawn_passive,
                so_path=CUDA_SO)
            st, msg = compare_parity_slice(
                "blaze-cuda continuous-vs-resume",
                cuda_cont[n:], cuda_res, n, start_i=1)
            print(("  PASS " if st == VERIFIED else "  FAIL ") + msg)
            if st != VERIFIED:
                any_fail = True
    elapsed = time.time() - t0
    if any_fail:
        print(f"\nFAIL: resume parity N={n} M={m} in {elapsed:.2f}s")
        return FAILED
    print(f"\nPASS: resume parity N={n} M={m} in {elapsed:.2f}s")
    return VERIFIED


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--chain", action="store_true")
    ap.add_argument("--snapshot")
    ap.add_argument("--tape")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument("--expected-chain-actions", type=int)
    ap.add_argument("--seeds", default="14")
    ap.add_argument("--ticks", type=int, default=1000)
    ap.add_argument("--features", default="")
    ap.add_argument("--mobs-on", action="store_true")
    ap.add_argument("--natural-spawn", action="store_true")
    ap.add_argument("--natural-spawn-passive", action="store_true")
    ap.add_argument("--n", type=int, default=None,
                    help="checkpoint action index (default: n_acts-M)")
    ap.add_argument("--m", type=int, default=None,
                    help=f"tail ticks after restore (default min({DEFAULT_TAIL}, n/4))")
    ap.add_argument("--cuda", action="store_true",
                    help="CUDA continuous-vs-resume only (M2)")
    ap.add_argument("--strict-capabilities", action="store_true",
                    help="accepted for port_matrix argv copy; unused")
    ap.add_argument("--port-parity", action="store_true",
                    help="accepted for port_matrix argv copy; always on")
    args = ap.parse_args()
    if not os.path.isfile(BIN):
        print(f"FAIL: missing {BIN}")
        sys.exit(FAILED)
    sys.exit(run_one(args))


if __name__ == "__main__":
    main()
