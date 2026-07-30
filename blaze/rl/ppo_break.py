"""PPO + conv net + curriculum for walk-to-tree + break-log.

Upgrades over walk_break.py (REINFORCE, hand-built grid features):
  - obs: raw camera planes (5, 36, 64) - tree mask, log mask, solid mask,
    depth/255, block-edge - plus sin/cos pitch into the fc layer. No
    hand-designed grids; the conv net reads the retina directly.
  - PPO (clip 0.2, GAE 0.99/0.95, 4 epochs per batch) instead of one
    REINFORCE step per episode.
  - curriculum: a scripted greedy approach phase (the controller that
    provably reaches trees) runs the first ticks of each episode; its
    budget anneals 120 ticks -> 0 over CURR_EPS episodes, so early
    episodes start adjacent+aligned to a trunk and late episodes start
    from spawn. Greedy ticks are recorded for replay but never trained on
    (and never attack, so they cannot score the break).

Same factored heads, same reward, same REPEAT=4 as walk_break.py.

Run (anvil): cd magma && uv run --no-project --with numpy,torch,matplotlib python rl/ppo_break.py
"""
import json
import math
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import torch
import torch.nn as nn

from look_at_tree import MagmaEnv
from walk_break import (SEEDS, N_ENVS, EP_LEN, REPEAT, GAMMA, HEADS,
                        LOG_ID, TREE_IDS, CAM_W, CAM_H,
                        nearest_log, log_set, act_dict, step_reward)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

N_EPISODES = int(os.environ.get("N_EPISODES", 400))
STACK = 3             # frame stack, one frame per DECISION (12 ticks of
                      # history at REPEAT=4): lets the policy remember a
                      # tree it turned past instead of re-scanning blind
CURR_EPS = 200        # curriculum anneal horizon (episodes)
CURR_TICKS = 120      # max greedy approach ticks at episode 0
LAM = 0.95            # GAE lambda
CLIP = 0.2
EPOCHS = 4
LR = 3e-4
ENT = 0.01


def planes(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.float32).reshape(CAM_H, CAM_W)
    edg = np.asarray(obs["edge"], dtype=np.float32).reshape(CAM_H, CAM_W)
    p = np.stack([np.isin(cam, TREE_IDS).astype(np.float32),
                  (cam == LOG_ID).astype(np.float32),
                  (cam != 0).astype(np.float32),
                  dep / 255.0, edg])
    s = np.array([math.sin(math.radians(obs["pitch"])),
                  math.cos(math.radians(obs["pitch"]))], dtype=np.float32)
    return p, s


def greedy_action(obs):
    nl = nearest_log(obs)
    if nl is None:
        return {"dyaw": 15.0}
    ry, rp, dist = nl
    a = {"dyaw": float(np.clip(ry, -15, 15)),
         "dpitch": float(np.clip(rp, -10, 10))}
    if dist > 2.5:
        a["forward"] = 1
    return a, dist


class ConvPolicy(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(5 * STACK, 16, 5, stride=2), nn.ReLU(),
            nn.Conv2d(16, 32, 3, stride=2), nn.ReLU(), nn.Flatten())
        with torch.no_grad():
            nflat = self.conv(
                torch.zeros(1, 5 * STACK, CAM_H, CAM_W)).shape[1]
        self.fc = nn.Sequential(nn.Linear(nflat + 2, 128), nn.ReLU())
        self.heads = nn.ModuleList([nn.Linear(128, n) for n in HEADS])
        self.value = nn.Linear(128, 1)

    def forward(self, p, s):
        h = self.fc(torch.cat([self.conv(p), s], dim=1))
        return [head(h) for head in self.heads], self.value(h).squeeze(-1)


def main():
    os.makedirs(OUT, exist_ok=True)
    torch.manual_seed(0)
    torch.set_num_threads(8)
    rng = np.random.default_rng(0)
    net = ConvPolicy()
    opt = torch.optim.Adam(net.parameters(), lr=LR)
    pool = ThreadPoolExecutor(max_workers=N_ENVS)

    ep_return, ep_success, ep_help = [], [], []
    env0_episodes = []

    for ep in range(N_EPISODES):
        envs = list(pool.map(MagmaEnv, SEEDS))
        for e in envs:
            e.send({"dyaw": round(float(rng.uniform(-180, 180)), 3),
                    "dpitch": round(float(10.0 - e.obs["pitch"]), 3)})
        for e in envs:
            e.recv()

        # ---- curriculum: scripted greedy approach, annealed budget ----
        help_budget = int(CURR_TICKS * max(0.0, 1.0 - ep / CURR_EPS))
        used = 0
        if help_budget > 0:
            for _ in range(help_budget):
                live = []
                for e in envs:
                    g = greedy_action(e.obs)
                    if isinstance(g, tuple) and g[1] <= 3.0:
                        continue  # this env is delivered
                    e.send(g[0] if isinstance(g, tuple) else g)
                    live.append(e)
                if not live:
                    break
                for e in live:
                    e.recv()
                used += 1

        prev_dist = [None] * N_ENVS
        prev_logs = [log_set(e.obs) for e in envs]
        for i, e in enumerate(envs):
            nl = nearest_log(e.obs)
            prev_dist[i] = nl[2] if nl else None

        done = [False] * N_ENVS
        success = [False] * N_ENVS
        traj = [[] for _ in envs]   # [(planes, scal), idx, reward, logp, val]
        # frame stacks seeded with the post-curriculum view
        stacks = [[planes(e.obs)[0]] * STACK for e in envs]

        for _ in range((EP_LEN - used) // REPEAT):
            live = [i for i in range(N_ENVS) if not done[i]]
            if not live:
                break
            for i in live:
                stacks[i].pop(0)
                stacks[i].append(planes(envs[i].obs)[0])
            ps = torch.from_numpy(np.stack(
                [np.concatenate(stacks[i]) for i in live]))
            ss = torch.from_numpy(np.stack(
                [planes(envs[i].obs)[1] for i in live]))
            with torch.no_grad():
                logits, vals = net(ps, ss)
                dists = [torch.distributions.Categorical(logits=lg)
                         for lg in logits]
                acts = [d.sample() for d in dists]
                logp = sum(d.log_prob(a) for d, a in zip(dists, acts))
            for k, i in enumerate(live):
                idx = tuple(int(h[k]) for h in acts)
                traj[i].append([(ps[k].numpy(), ss[k].numpy()), idx, 0.0,
                                float(logp[k]), float(vals[k])])
            for rep in range(REPEAT):
                for i in live:
                    if done[i]:
                        continue
                    a = act_dict(traj[i][-1][1])
                    if rep > 0:
                        a = dict(a)
                        a["dyaw"] = 0.0
                        a["dpitch"] = 0.0
                    envs[i].send(a)
                for i in live:
                    if done[i]:
                        continue
                    step_reward(envs[i], i, traj, prev_dist, prev_logs,
                                done, success)

        # ---- PPO update over the whole batch ----
        P, S, A, LP, ADV, RET = [], [], [], [], [], []
        rets_all = []
        for i in range(N_ENVS):
            if not traj[i]:
                rets_all.append(0.0)
                continue
            rews = [s[2] for s in traj[i]]
            vals = [s[4] for s in traj[i]] + [0.0]
            gae, advs = 0.0, []
            for t in reversed(range(len(rews))):
                delta = rews[t] + GAMMA * vals[t + 1] - vals[t]
                gae = delta + GAMMA * LAM * gae
                advs.append(gae)
            advs.reverse()
            for t, s in enumerate(traj[i]):
                P.append(s[0][0]); S.append(s[0][1]); A.append(s[1])
                LP.append(s[3]); ADV.append(advs[t])
                RET.append(advs[t] + s[4])
            rets_all.append(sum(rews))
        P = torch.from_numpy(np.stack(P)); S = torch.from_numpy(np.stack(S))
        A = torch.tensor(A); LP = torch.tensor(LP)
        ADV = torch.tensor(ADV, dtype=torch.float32)
        RET = torch.tensor(RET, dtype=torch.float32)
        ADV = (ADV - ADV.mean()) / (ADV.std() + 1e-8)
        for _ in range(EPOCHS):
            logits, vals = net(P, S)
            dists = [torch.distributions.Categorical(logits=lg)
                     for lg in logits]
            logp = sum(d.log_prob(A[:, h]) for h, d in enumerate(dists))
            ratio = torch.exp(logp - LP)
            pg = -torch.min(ratio * ADV,
                            torch.clamp(ratio, 1 - CLIP, 1 + CLIP) * ADV)
            ent = sum(d.entropy() for d in dists)
            loss = pg.mean() + 0.5 * ((RET - vals) ** 2).mean() \
                - ENT * ent.mean()
            opt.zero_grad()
            loss.backward()
            opt.step()

        ep_return.append(float(np.mean(rets_all)))
        ep_success.append(float(np.mean(success)))
        ep_help.append(used)
        env0_episodes.append((ep, list(envs[0].actions), success[0],
                              float(rets_all[0])))
        for e in envs:
            e.close()
        if ep % 10 == 0 or ep == N_EPISODES - 1:
            print(f"ep {ep:4d}  return {ep_return[-1]:+7.2f}  "
                  f"success {ep_success[-1]:.2f}  help {used:3d}",
                  flush=True)

    np.save(os.path.join(OUT, "ppo_return.npy"), np.array(ep_return))
    np.save(os.path.join(OUT, "ppo_success.npy"), np.array(ep_success))
    np.save(os.path.join(OUT, "ppo_help.npy"), np.array(ep_help))
    with open(os.path.join(OUT, "ppo_env0_episodes.jsonl"), "w") as f:
        for ep, acts, suc, ret in env0_episodes:
            f.write(json.dumps({"ep": ep, "success": suc, "return": ret,
                                "actions": acts}) + "\n")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(3, 1, figsize=(8, 8), sharex=True)
    ax[0].plot(ep_return); ax[0].set_ylabel("mean episode return")
    w = min(10, len(ep_success))
    sm = np.convolve(ep_success, np.ones(w) / w, mode="valid")
    ax[1].plot(ep_success, alpha=0.3)
    ax[1].plot(range(w - 1, len(ep_success)), sm)
    ax[1].set_ylabel("success rate (log broken)")
    ax[2].plot(ep_help); ax[2].set_ylabel("greedy help ticks")
    ax[2].set_xlabel("episode")
    fig.suptitle("PPO + conv retina + curriculum: walk to tree, break a log")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "ppo_break_curve.png"), dpi=140)
    print("saved", os.path.join(OUT, "ppo_break_curve.png"))


if __name__ == "__main__":
    main()
