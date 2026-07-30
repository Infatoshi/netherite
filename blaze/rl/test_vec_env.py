"""Bit-exactness check: VecCoalEnv vs the threaded-path per-env semantics
(ppo_coal inner loop, incl. camera-on-demand) on seeds 14/16, 50 fixed
decisions, help_budget=0.

Run (anvil): cd magma && uv run --no-project --with numpy,torch python rl/test_vec_env.py
"""
import json
import os

import numpy as np

import chain_probe as cp
from ppo_coal import COAL_ID, HEADS, IX_COAL, OUT, REPEAT, act_dict, \
    make_env, nearest_coal, planes
from vec_env import VecCoalEnv

SEEDS = [14, 16]
N_DEC = 50


def ref_rollout(seed, prefix, idx_seq):
    """Replicates the ppo_coal episode loop for ONE env."""
    env = make_env(seed, prefix)
    base = int(cp.inv(env, IX_COAL))
    done = bool(cp.inv(env, IX_COAL) > base)
    nc = nearest_coal(env.obs)
    prev = nc[2] if nc else None
    p, s = planes(env.obs)
    out = [(p, s, np.float32(0.0), done)]
    for idx in idx_seq:
        if done:
            out.append(out[-1])
            continue
        acc = 0.0
        for rep in range(REPEAT):
            if done:
                continue
            a = act_dict(idx)
            if rep > 0:
                a = dict(a)
                a["dyaw"] = 0.0
                a["dpitch"] = 0.0
                if rep < REPEAT - 1:
                    a["cam"] = 0
            env.send(a)
            obs = env.recv()
            r = -0.005
            nc = nearest_coal(obs)
            if nc is not None and prev is not None:
                r += 0.5 * (prev - nc[2])
            prev = nc[2] if nc else prev
            if idx[4] and obs["cam"][18 * 64 + 32] == COAL_ID:
                r += 0.03
            if cp.inv(env, IX_COAL) > base:
                r += 10.0
                done = True
            acc += r
        p, s = planes(env.obs)
        out.append((p, s, np.float32(acc), done))
    env.close()
    return out


def main():
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    rng = np.random.default_rng(7)
    idx_seqs = [[tuple(int(rng.integers(h)) for h in HEADS)
                 for _ in range(N_DEC)] for _ in SEEDS]

    refs = [ref_rollout(s, prefixes[str(s)], idx_seqs[i])
            for i, s in enumerate(SEEDS)]

    vec = VecCoalEnv(SEEDS, prefixes, n_workers=2)
    p, s, d = vec.reset(help_budget=0)
    got = [[(p[i], s[i], np.float32(0.0), bool(d[i]))] for i in range(2)]
    for t in range(N_DEC):
        idx = np.array([idx_seqs[i][t] for i in range(2)], dtype=np.int64)
        p, s, r, d = vec.step(idx)
        for i in range(2):
            got[i].append((p[i], s[i], np.float32(r[i]), bool(d[i])))
    vec.close()

    for i, seed in enumerate(SEEDS):
        for t in range(N_DEC + 1):
            rp, rs, rr, rd = refs[i][t]
            gp, gs, gr, gd = got[i][t]
            assert rp.tobytes() == gp.tobytes(), f"seed {seed} t {t} planes"
            assert rs.tobytes() == gs.tobytes(), f"seed {seed} t {t} scalars"
            assert rr.tobytes() == gr.tobytes(), \
                f"seed {seed} t {t} reward {rr} != {gr}"
            assert rd == gd, f"seed {seed} t {t} done"
        print(f"seed {seed}: {N_DEC} decisions bit-exact "
              f"(final reward {refs[i][-1][2]:+.4f}, done {refs[i][-1][3]})")
    print("PASS")


if __name__ == "__main__":
    main()
