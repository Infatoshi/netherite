"""Multiprocessing vector env for ppo_coal: workers own K magma envs each.

The threaded path caps at ~17k env steps/s no matter the thread count: one
Python interpreter does every binary decode + reward on one GIL while the C
sim ticks in tens of us. Here each worker process does ALL per-tick work
locally (send/recv, decode, action repeat with camera skipped on middle
repeat ticks, dense reward, done detection, pipelined prefix replay,
scripted curriculum help) and the parent sees only per-DECISION data
through shared memory: planes [N,5,36,64] f32, scalars [N,6] f32,
reward [N] f32, done [N].

Start method is 'fork': ppo_coal (and chain_probe via walk_break) import
torch, workers inherit the already-loaded module and never touch it, so
there is no per-worker torch init and no torch threading in workers.
"""
import multiprocessing as mp
import os
from multiprocessing import shared_memory

import numpy as np

import chain_probe as cp
from ppo_coal import COAL_ID, HEADS, IX_COAL, REPEAT, act_dict, make_env, \
    nearest_coal, planes
from walk_break import CAM_W, CAM_H


def _views(shm, n):
    off = 0

    def take(shape, dtype):
        nonlocal off
        off = (off + 7) // 8 * 8
        a = np.ndarray(shape, dtype=dtype, buffer=shm.buf, offset=off)
        off += a.nbytes
        return a

    return {"planes": take((n, 5, CAM_H, CAM_W), np.float32),
            "scalars": take((n, 6), np.float32),
            "reward": take((n,), np.float32),
            "done": take((n,), np.uint8),
            "idx": take((n, len(HEADS)), np.int64)}


def _shm_size(n):
    per = 5 * CAM_H * CAM_W * 4 + 6 * 4 + 4 + 1 + len(HEADS) * 8
    return n * per + 64


def _attach(name):
    try:    # track=False (3.13+): the parent owns unlink, silence the tracker
        return shared_memory.SharedMemory(name=name, track=False)
    except TypeError:
        return shared_memory.SharedMemory(name=name)


def _worker(conn, shm_name, n_total, lo, seeds, prefixes):
    shm = _attach(shm_name)
    v = _views(shm, n_total)
    envs = [None] * len(seeds)
    st = [{"base": 0, "prev": None, "done": True} for _ in seeds]
    try:
        while True:
            cmd = conn.recv()
            if cmd[0] == "reset":
                help_budget = cmd[1]
                for j, seed in enumerate(seeds):
                    if envs[j] is not None:
                        envs[j].close()
                    envs[j] = make_env(seed, prefixes[j])
                    st[j]["base"] = int(cp.inv(envs[j], IX_COAL))
                if help_budget > 0:
                    for e in envs:
                        cp.stage_coal(e, budget=help_budget, stop_dist=3.0)
                for j, env in enumerate(envs):
                    st[j]["done"] = bool(cp.inv(env, IX_COAL) > st[j]["base"])
                    nc = nearest_coal(env.obs)
                    st[j]["prev"] = nc[2] if nc else None
                    p, s = planes(env.obs)
                    v["planes"][lo + j] = p
                    v["scalars"][lo + j] = s
                    v["reward"][lo + j] = 0.0
                    v["done"][lo + j] = st[j]["done"]
                conn.send("ok")
            elif cmd[0] == "step":
                live = [j for j in range(len(envs)) if not st[j]["done"]]
                acc = {j: 0.0 for j in live}
                idxs = {j: tuple(int(x) for x in v["idx"][lo + j])
                        for j in live}
                for rep in range(REPEAT):
                    for j in live:              # send all, then recv all:
                        if st[j]["done"]:       # local envs tick in parallel
                            continue
                        a = act_dict(idxs[j])
                        if rep > 0:
                            a = dict(a)
                            a["dyaw"] = 0.0
                            a["dpitch"] = 0.0
                            # planes are consumed once per decision: skip
                            # the raycast on middle repeat ticks, render on
                            # the final rep (its obs feeds the next decision)
                            if rep < REPEAT - 1:
                                a["cam"] = 0
                        envs[j].send(a)
                    for j in live:
                        if st[j]["done"]:
                            continue
                        obs = envs[j].recv()
                        r = -0.005
                        nc = nearest_coal(obs)
                        if nc is not None and st[j]["prev"] is not None:
                            r += 0.5 * (st[j]["prev"] - nc[2])
                        st[j]["prev"] = nc[2] if nc else st[j]["prev"]
                        if (idxs[j][4]          # this env's attack head
                                and obs["cam"][18 * 64 + 32] == COAL_ID):
                            r += 0.03
                        if cp.inv(envs[j], IX_COAL) > st[j]["base"]:
                            r += 10.0
                            st[j]["done"] = True
                        acc[j] += r
                for j in live:
                    p, s = planes(envs[j].obs)
                    v["planes"][lo + j] = p
                    v["scalars"][lo + j] = s
                    v["reward"][lo + j] = acc[j]
                    v["done"][lo + j] = st[j]["done"]
                conn.send("ok")
            elif cmd[0] == "actions":
                conn.send(list(envs[cmd[1]].actions))
            elif cmd[0] == "close":
                break
    finally:
        for e in envs:
            if e is not None:
                e.close()
        shm.close()
        conn.close()


class VecCoalEnv:
    """Parent-side handle. reset(help_budget) -> (planes, scalars, done);
    step(idx [N,len(HEADS)]) -> (planes, scalars, reward, done). Workers are
    persistent; each reset respawns the magma processes inside them (the
    env has no reset command) and replays the per-seed prefix."""

    def __init__(self, seeds, prefixes, n_workers=None):
        n = len(seeds)
        if n_workers is None:   # ~half the threads, minus headroom for the
            n_workers = max(1, (os.cpu_count() or 16) // 2 - 4)  # trainer
        n_workers = max(1, min(n_workers, n))
        self.n = n
        self.shm = shared_memory.SharedMemory(create=True,
                                              size=_shm_size(n))
        self.v = _views(self.shm, n)
        ctx = mp.get_context("fork")
        self.procs, self.conns = [], []
        self._loc = [None] * n          # env i -> (worker, local index)
        lo = 0
        for w in range(n_workers):
            k = n // n_workers + (1 if w < n % n_workers else 0)
            chunk = seeds[lo:lo + k]
            pref = [prefixes[str(s)] for s in chunk]
            parent, child = ctx.Pipe()
            proc = ctx.Process(target=_worker, daemon=True,
                               args=(child, self.shm.name, n, lo, chunk,
                                     pref))
            proc.start()
            child.close()
            for j in range(k):
                self._loc[lo + j] = (w, j)
            self.procs.append(proc)
            self.conns.append(parent)
            lo += k

    def _all(self, cmd):
        for c in self.conns:
            c.send(cmd)
        for c in self.conns:
            c.recv()

    def reset(self, help_budget=0):
        self._all(("reset", int(help_budget)))
        return (self.v["planes"].copy(), self.v["scalars"].copy(),
                self.v["done"].astype(bool))

    def step(self, idx):
        self.v["idx"][:] = np.asarray(idx, dtype=np.int64)
        self._all(("step",))
        return (self.v["planes"].copy(), self.v["scalars"].copy(),
                self.v["reward"].copy(), self.v["done"].astype(bool))

    def actions(self, i):
        w, j = self._loc[i]
        self.conns[w].send(("actions", j))
        return self.conns[w].recv()

    def close(self):
        for c in self.conns:
            try:
                c.send(("close",))
            except (BrokenPipeError, OSError):
                pass
        for p in self.procs:
            p.join(timeout=10)
            if p.is_alive():
                p.terminate()
        for c in self.conns:
            c.close()
        self.shm.close()
        self.shm.unlink()
