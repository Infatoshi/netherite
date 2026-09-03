"""Multi-process sharded CPU replica for blaze verification gates.

Shards N independent CPU environment lanes across W worker processes using
multiprocessing (spawn start method). Each worker manages a contiguous slice
of lanes [i*k, (i+1)*k) via VecBlaze. Outputs are gathered in lane order so
the resulting arrays match single-process VecBlaze bitwise.
"""
import multiprocessing as mp
import os
import sys
import traceback
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from blaze import (
    CPU_SO,
    CAM_H,
    CAM_W,
    NSCAL,
    NPOSE,
    NSTATUS,
    VecBlaze,
)


def resolve_cpu_so(so_path=None):
    return so_path if so_path else CPU_SO


def _worker_loop(conn, n_lanes, so_path, kw):
    # blaze_cpu.so OpenMP-parallelizes over env index. Each worker already has
    # a slice of lanes, so a default OMP_NUM_THREADS=nproc inside every
    # process oversubscribes the box (16 workers x 32 threads). Pin to 1
    # before ctypes.CDLL so libgomp sees it at init. Parent is unchanged.
    os.environ["OMP_NUM_THREADS"] = "1"
    try:
        env = VecBlaze(n_lanes, device=0, so_path=so_path, **kw)
        conn.send(("ok", None))
    except Exception:
        conn.send(("err", traceback.format_exc()))
        conn.close()
        return

    while True:
        try:
            msg = conn.recv()
        except (EOFError, KeyboardInterrupt):
            break
        except Exception:
            break

        if msg is None:
            break

        cmd, args = msg
        try:
            if cmd == "step":
                acts, repeat = args
                env.step(acts, repeat=repeat)
                conn.send(("ok", (
                    env.cam, env.depth, env.edge, env.scal,
                    env.rew, env.done, env.pose, env.status
                )))
            elif cmd == "reset":
                env.reset(args)
                conn.send(("ok", None))
            elif cmd == "assign":
                env.assign(args)
                conn.send(("ok", None))
            elif cmd == "load_snapshots":
                r = env.load_snapshots(args)
                conn.send(("ok", r))
            elif cmd == "close":
                env.close()
                conn.send(("ok", None))
                break
            elif cmd == "op_trace":
                conn.send(("ok", env.op_trace()))
            elif cmd == "snapshot_has_liquid":
                conn.send(("ok", env.snapshot_has_liquid(args)))
            elif cmd == "set_success_item":
                env.set_success_item(args)
                conn.send(("ok", None))
            elif cmd == "set_reward_gate":
                env.set_reward_gate(args)
                conn.send(("ok", None))
            else:
                conn.send(("err", f"unknown command: {cmd}"))
        except Exception:
            conn.send(("err", traceback.format_exc()))

    try:
        env.close()
    except Exception:
        pass
    try:
        conn.close()
    except Exception:
        pass


class ShardedCpuVec:
    def __init__(self, n, workers=1, device=0, so_path=None, **kw):
        self.n = n
        self.workers = max(1, min(int(workers), n))
        self.device = device
        self.so_path = resolve_cpu_so(so_path)
        self.backend = "cpu"
        self.kw = kw
        self.n_snaps = 0
        self.closed = False

        # Output buffers matching VecBlaze shapes and dtypes:
        self.cam = np.zeros((n, CAM_H, CAM_W), dtype=np.int16)
        self.depth = np.zeros((n, CAM_H, CAM_W), dtype=np.uint8)
        self.edge = np.zeros((n, CAM_H, CAM_W), dtype=np.uint8)
        self.scal = np.zeros((n, NSCAL), dtype=np.float32)
        self.rew = np.zeros(n, dtype=np.float32)
        self.done = np.zeros(n, dtype=np.uint8)
        self.pose = np.zeros((n, NPOSE), dtype=np.float32)
        self.status = np.zeros((n, NSTATUS), dtype=np.int32)

        # Contiguous slices for each worker:
        base = n // self.workers
        rem = n % self.workers
        self.slices = []
        start = 0
        for i in range(self.workers):
            size = base + (1 if i < rem else 0)
            end = start + size
            self.slices.append((start, end))
            start = end

        ctx = mp.get_context("spawn")
        self.pipes = []
        self.procs = []
        for i, (s, e) in enumerate(self.slices):
            parent_conn, child_conn = ctx.Pipe(duplex=True)
            p = ctx.Process(
                target=_worker_loop,
                args=(child_conn, e - s, self.so_path, self.kw),
                daemon=True,
            )
            p.start()
            child_conn.close()
            self.pipes.append(parent_conn)
            self.procs.append(p)

        # Wait for all workers to initialize:
        for i, pipe in enumerate(self.pipes):
            status, err = pipe.recv()
            if status != "ok":
                self.close()
                raise RuntimeError(f"worker {i} init failed:\n{err}")

    def load_snapshots(self, paths):
        for pipe in self.pipes:
            pipe.send(("load_snapshots", paths))
        results = []
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} load_snapshots failed:\n{res}")
            results.append(res)
        self.n_snaps = results[0] if results else 0
        return self.n_snaps

    def assign(self, snap_idx):
        snap_list = list(snap_idx)
        for i, pipe in enumerate(self.pipes):
            s, e = self.slices[i]
            pipe.send(("assign", snap_list[s:e]))
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} assign failed:\n{res}")

    def reset(self, mask=None):
        if mask is not None:
            mask_arr = np.asarray(mask)
            for i, pipe in enumerate(self.pipes):
                s, e = self.slices[i]
                pipe.send(("reset", mask_arr[s:e]))
        else:
            for pipe in self.pipes:
                pipe.send(("reset", None))
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} reset failed:\n{res}")

    def step(self, actions, repeat=4):
        acts = np.asarray(actions)
        for i, pipe in enumerate(self.pipes):
            s, e = self.slices[i]
            pipe.send(("step", (acts[s:e], repeat)))
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} step failed:\n{res}")
            s, e = self.slices[i]
            (self.cam[s:e], self.depth[s:e], self.edge[s:e],
             self.scal[s:e], self.rew[s:e], self.done[s:e],
             self.pose[s:e], self.status[s:e]) = res
        return (self.cam, self.depth, self.edge, self.scal, self.rew,
                self.done, self.pose)

    def op_trace(self):
        for pipe in self.pipes:
            pipe.send(("op_trace", None))
        traces = []
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok" or res is None:
                return None
            traces.append(res)
        return np.concatenate(traces, axis=0) if traces else None

    def snapshot_has_liquid(self, i):
        if not self.pipes:
            return 0
        self.pipes[0].send(("snapshot_has_liquid", i))
        status, res = self.pipes[0].recv()
        if status != "ok":
            raise RuntimeError(f"snapshot_has_liquid failed:\n{res}")
        return res

    def set_success_item(self, item):
        for pipe in self.pipes:
            pipe.send(("set_success_item", item))
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} set_success_item failed:\n{res}")

    def set_reward_gate(self, dist):
        for pipe in self.pipes:
            pipe.send(("set_reward_gate", dist))
        for i, pipe in enumerate(self.pipes):
            status, res = pipe.recv()
            if status != "ok":
                raise RuntimeError(f"worker {i} set_reward_gate failed:\n{res}")

    def close(self):
        if self.closed:
            return
        self.closed = True
        for pipe in self.pipes:
            try:
                pipe.send(("close", None))
            except Exception:
                pass
        for p in self.procs:
            try:
                p.join(timeout=1.0)
                if p.is_alive():
                    p.terminate()
                    p.join(timeout=1.0)
            except Exception:
                pass
        for pipe in self.pipes:
            try:
                pipe.close()
            except Exception:
                pass
        self.pipes.clear()
        self.procs.clear()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
