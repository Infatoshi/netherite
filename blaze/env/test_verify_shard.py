#!/usr/bin/env python3
"""Self-consistency test for ShardedCpuVec: 64 lanes x 20 decisions of mixed
full-action stream compared bitwise between workers=1 (single-process VecBlaze)
and workers=8 (ShardedCpuVec).
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1")
import sys
import time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from blaze import CPU_SO, VecBlaze
from sharded_vec import ShardedCpuVec, resolve_cpu_so
from verify_cuda import ActStream, outputs, snap_paths


def main():
    so_path = resolve_cpu_so(CPU_SO)
    if not os.path.exists(so_path):
        print(f"missing fixture path: {so_path}")
        sys.exit(1)

    try:
        paths = snap_paths(t0=True)
    except SystemExit as e:
        print(f"missing fixture path: {e}")
        sys.exit(1)

    for p in paths:
        if not os.path.exists(p):
            print(f"missing fixture path: {p}")
            sys.exit(1)

    n = 64
    decisions = 20
    repeat = 4
    assign = [i % len(paths) for i in range(n)]

    # Pre-generate 20 decisions of full 12-double actions from ActStream
    streams = [ActStream(i) for i in range(n)]
    actions_seq = [
        np.array([s.next_full() for s in streams], dtype=np.float64)
        for _ in range(decisions)
    ]

    print(f"test_verify_shard: N={n}, {len(paths)} t0 snapshots, {decisions} decisions x repeat {repeat}")
    print("running workers=1 (single-process VecBlaze)...")
    env1 = VecBlaze(n, device=0, so_path=so_path)
    env1.load_snapshots(paths)
    env1.assign(assign)
    env1.reset()

    outputs_w1 = []
    t0 = time.perf_counter()
    for d in range(decisions):
        env1.step(actions_seq[d], repeat=repeat)
        out = outputs(env1)
        outputs_w1.append({k: v.copy() for k, v in out.items()})
    t1_time = time.perf_counter() - t0
    env1.close()
    print(f"workers=1 stepping time: {t1_time:.2f}s ({decisions / t1_time:.1f} decisions/s)")

    print("running workers=8 (ShardedCpuVec)...")
    env8 = ShardedCpuVec(n, workers=8, device=0, so_path=so_path)
    env8.load_snapshots(paths)
    env8.assign(assign)
    env8.reset()

    t0 = time.perf_counter()
    for d in range(decisions):
        env8.step(actions_seq[d], repeat=repeat)
        out8 = outputs(env8)
        out1 = outputs_w1[d]
        for key in ("cam", "depth", "edge", "scal", "rew", "done", "pose"):
            a = out1[key]
            b = out8[key]
            assert a.shape == b.shape, f"decision {d} field {key} shape mismatch: {a.shape} vs {b.shape}"
            assert a.dtype == b.dtype, f"decision {d} field {key} dtype mismatch: {a.dtype} vs {b.dtype}"
            if not np.array_equal(a, b):
                idx = np.argwhere(a != b)[0]
                raise AssertionError(
                    f"decision {d}: field {key} mismatch at {tuple(idx)}: "
                    f"workers=1: {a[tuple(idx)]}, workers=8: {b[tuple(idx)]}"
                )
    t8_time = time.perf_counter() - t0
    env8.close()
    print(f"workers=8 stepping time: {t8_time:.2f}s ({decisions / t8_time:.1f} decisions/s)")

    speedup = t1_time / t8_time if t8_time > 0 else float("inf")
    print(f"speedup: {speedup:.2f}x")
    print(f"PASS: all {decisions} decisions bitwise identical across all {n} lanes (cam/depth/edge/scal/rew/done/pose)")


if __name__ == "__main__":
    main()
