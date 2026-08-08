"""Walk to the nearest tree and break a log: factored-action policy gradient
on N parallel magma --rl envs.

Actions are FACTORED heads sampled independently each tick (the joint space
would be 3*3*2*2*2 = 72; factored heads scale to the full game action set):
  yaw   {-15, 0, +15} deg     pitch {-10, 0, +10} deg
  forward {0, 1}              jump {0, 1}              attack {0, 1}

Obs is the 64x36 semantic camera only (plus pitch): an 8x6 grid of
tree-pixel fractions, an 8x6 grid of tree proximity (1 - depth/255 over tree
pixels), center-tree flag + center proximity, no-tree flag, sin/cos pitch.
The oracle logs list feeds the REWARD only:
  +progress (prev nearest-log dist - current, clipped)
  +0.03 alignment
  +0.15 per tick holding attack with a log pixel at the crosshair in reach
  +5.0 on break (a tracked log coord within 6 blocks vanishes) -> episode ends
Policy/value: tiny torch MLP (shared trunk, 5 categorical heads + value
baseline), REINFORCE with reward-to-go minus value. Fresh env processes every
episode (world edits persist otherwise - the policy would eat the tree).

Run (anvil): cd magma && uv run --no-project --with numpy,torch,matplotlib python rl/walk_break.py
"""
import json
import math
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import torch
import torch.nn as nn

from look_at_tree import MagmaEnv, log_dirs

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

SEEDS = [0, 2, 3, 10, 11, 12, 14, 17, 19, 20, 21, 22, 29, 30, 35, 47]
N_ENVS = len(SEEDS)
N_EPISODES = int(os.environ.get("N_EPISODES", 250))
EP_LEN = 240          # game ticks; policy decides every REPEAT ticks
REPEAT = 4            # action repeat: 60 decisions/episode - a log break
                      # needs 15 consecutive attack DECISIONS instead of 60
                      # consecutive ticks (mining progress resets on release)
GAMMA = 0.99
LR = 3e-3
LOG_ID = 17
TREE_IDS = (17, 18)
CAM_W, CAM_H = 64, 36
GRID_X, GRID_Y = 8, 6
N_FEAT = 2 * GRID_X * GRID_Y + 5
HEADS = [3, 3, 2, 2, 2]  # yaw, pitch, forward, jump, attack
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)
BREAK_BONUS = 5.0
REACH = 4.5


def featurize(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.float64).reshape(CAM_H, CAM_W)
    tree = np.isin(cam, TREE_IDS)
    prox = np.where(tree, 1.0 - dep / 255.0, 0.0)
    cw, ch = CAM_W // GRID_X, CAM_H // GRID_Y
    tcells = tree.reshape(GRID_Y, ch, GRID_X, cw).mean(axis=(1, 3))
    pcells = prox.reshape(GRID_Y, ch, GRID_X, cw).mean(axis=(1, 3))
    cy, cx = CAM_H // 2, CAM_W // 2
    center_log = 1.0 if cam[cy, cx] == LOG_ID else 0.0
    center_prox = 1.0 - dep[cy, cx] / 255.0
    f = np.concatenate([
        tcells.reshape(-1), pcells.reshape(-1),
        [center_log, center_prox,
         0.0 if tree.any() else 1.0,
         math.sin(math.radians(obs["pitch"])),
         math.cos(math.radians(obs["pitch"]))]])
    return f.astype(np.float32)


def nearest_log(obs):
    logs = log_dirs(obs)
    if not logs:
        return None
    return min(logs, key=lambda t: t[2])


def log_set(obs):
    return {tuple(b) for b in obs["logs"] if b != [0, 0, 0]}


def crosshair_on_log(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.int32).reshape(CAM_H, CAM_W)
    cy, cx = CAM_H // 2, CAM_W // 2
    return cam[cy, cx] == LOG_ID and dep[cy, cx] <= int(REACH * 4)


def step_reward(env, i, traj, prev_dist, prev_logs, done, success):
    """recv one tick's obs, accumulate reward onto the CURRENT decision.

    With action repeat, all REPEAT ticks of one decision add into the same
    traj entry, so per-decision reward magnitudes stay comparable to the
    per-tick trainer this replaced."""
    obs = env.recv()
    r = 0.0
    nl = nearest_log(obs)
    if nl is None:
        r -= 0.05
    else:
        ry, rp, dist = nl
        if prev_dist[i] is not None:
            r += float(np.clip(prev_dist[i] - dist, -0.5, 0.5))
        prev_dist[i] = dist
        r += 0.03 * math.cos(math.radians(ry)) * math.cos(math.radians(rp))
    if traj[i][-1][1][4] and crosshair_on_log(obs):
        r += 0.15
    cur = log_set(obs)
    gone = prev_logs[i] - cur
    if gone and cur:
        # the logs list is the 64 NEAREST: moving can push a still-standing
        # log off the end ("gone" but not broken). A truly broken log must be
        # NEARER than the farthest log still listed - truncation cannot
        # explain that absence.
        ex, ey, ez = obs["x"], obs["y"] + 1.62, obs["z"]
        dmax = max(math.dist((b[0] + .5, b[1] + .5, b[2] + .5),
                             (ex, ey, ez)) for b in cur)
        for (bx, by, bz) in gone:
            d = math.dist((bx + .5, by + .5, bz + .5), (ex, ey, ez))
            if d <= REACH + 1.0 and d < dmax - 1e-9:
                r += BREAK_BONUS
                done[i] = True
                success[i] = True
                break
    prev_logs[i] = cur
    traj[i][-1][2] += r


class Policy(nn.Module):
    def __init__(self):
        super().__init__()
        self.trunk = nn.Sequential(nn.Linear(N_FEAT, 64), nn.Tanh())
        self.heads = nn.ModuleList([nn.Linear(64, n) for n in HEADS])
        self.value = nn.Linear(64, 1)

    def forward(self, x):
        h = self.trunk(x)
        return [head(h) for head in self.heads], self.value(h).squeeze(-1)


def act_dict(idx):
    a = {"dyaw": YAWS[idx[0]], "dpitch": PITCHES[idx[1]]}
    if idx[2]:
        a["forward"] = 1
    if idx[3]:
        a["jump"] = 1
    if idx[4]:
        a["attack"] = 1
    return a


def main():
    os.makedirs(OUT, exist_ok=True)
    torch.manual_seed(0)
    torch.set_num_threads(4)
    rng = np.random.default_rng(0)
    net = Policy()
    opt = torch.optim.Adam(net.parameters(), lr=LR)
    pool = ThreadPoolExecutor(max_workers=N_ENVS)

    ep_return, ep_success = [], []
    env0_episodes = []   # (episode, actions, success, ep_return)

    for ep in range(N_EPISODES):
        envs = list(pool.map(MagmaEnv, SEEDS))
        # scramble yaw, level pitch slightly down (recorded, not rewarded)
        for e in envs:
            e.send({"dyaw": round(float(rng.uniform(-180, 180)), 3),
                    "dpitch": round(float(10.0 - e.obs["pitch"]), 3)})
        for e in envs:
            e.recv()

        prev_dist = [None] * N_ENVS
        prev_logs = [log_set(e.obs) for e in envs]
        for i, e in enumerate(envs):
            nl = nearest_log(e.obs)
            prev_dist[i] = nl[2] if nl else None

        done = [False] * N_ENVS
        success = [False] * N_ENVS
        traj = [[] for _ in envs]   # (feat, idx_tuple, reward)

        for _ in range(EP_LEN // REPEAT):
            live = [i for i in range(N_ENVS) if not done[i]]
            if not live:
                break
            feats = np.stack([featurize(envs[i].obs) for i in live])
            with torch.no_grad():
                logits, _ = net(torch.from_numpy(feats))
                idxs = [torch.distributions.Categorical(logits=lg).sample()
                        for lg in logits]
            for k, i in enumerate(live):
                idx = tuple(int(h[k]) for h in idxs)
                traj[i].append([feats[k], idx, 0.0])
            for rep in range(REPEAT):
                for k, i in enumerate(live):
                    if done[i]:
                        continue
                    a = act_dict(traj[i][-1][1])
                    if rep > 0:
                        a = dict(a)
                        a["dyaw"] = 0.0   # turn once per decision, not x4
                        a["dpitch"] = 0.0
                    envs[i].send(a)
                for k, i in enumerate(live):
                    if done[i]:
                        continue
                    step_reward(envs[i], i, traj, prev_dist, prev_logs,
                                done, success)

        # update: REINFORCE with reward-to-go minus value baseline
        loss = torch.tensor(0.0)
        n_steps = 0
        rets_all = []
        for i in range(N_ENVS):
            if not traj[i]:
                continue
            feats = torch.from_numpy(np.stack([s[0] for s in traj[i]]))
            rews = [s[2] for s in traj[i]]
            g, rets = 0.0, []
            for r in reversed(rews):
                g = r + GAMMA * g
                rets.append(g)
            rets = torch.tensor(list(reversed(rets)), dtype=torch.float32)
            logits, values = net(feats)
            adv = (rets - values).detach()
            logp = torch.tensor(0.0)
            for h, lg in enumerate(logits):
                dist = torch.distributions.Categorical(logits=lg)
                acts = torch.tensor([s[1][h] for s in traj[i]])
                logp = logp + (dist.log_prob(acts) * adv).sum()
                loss = loss - 0.01 * dist.entropy().sum()
            loss = loss - logp + ((rets - values) ** 2).sum()
            n_steps += len(traj[i])
            rets_all.append(sum(rews))
        opt.zero_grad()
        (loss / max(n_steps, 1)).backward()
        opt.step()

        ep_return.append(float(np.mean(rets_all)))
        ep_success.append(float(np.mean(success)))
        env0_episodes.append((ep, list(envs[0].actions), success[0],
                              float(rets_all[0])))
        for e in envs:
            e.close()
        if ep % 10 == 0 or ep == N_EPISODES - 1:
            print(f"ep {ep:4d}  return {ep_return[-1]:+7.2f}  "
                  f"success {ep_success[-1]:.2f}", flush=True)

    np.save(os.path.join(OUT, "wb_return.npy"), np.array(ep_return))
    np.save(os.path.join(OUT, "wb_success.npy"), np.array(ep_success))
    with open(os.path.join(OUT, "wb_env0_episodes.jsonl"), "w") as f:
        for ep, acts, suc, ret in env0_episodes:
            f.write(json.dumps({"ep": ep, "success": suc, "return": ret,
                                "actions": acts}) + "\n")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(2, 1, figsize=(8, 6), sharex=True)
    ax[0].plot(ep_return); ax[0].set_ylabel("mean episode return")
    w = min(10, len(ep_success))
    sm = np.convolve(ep_success, np.ones(w) / w, mode="valid")
    ax[1].plot(ep_success, alpha=0.3)
    ax[1].plot(range(w - 1, len(ep_success)), sm)
    ax[1].set_ylabel("success rate (log broken)")
    ax[1].set_xlabel("episode")
    fig.suptitle("walk to tree + break a log (16 envs, factored heads)")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "walk_break_curve.png"), dpi=140)
    print("saved", os.path.join(OUT, "walk_break_curve.png"))


if __name__ == "__main__":
    main()
