"""PPO for the mine-coal skill: burrow to the scanned coal ore and mine it.

Episode = scripted prefix (chop -> craft -> pick -> dig, replayed from
rl/out/coal_prefixes.json, produced by PREFIX=1 chain_probe.py) + learned
phase starting underground with a wooden pick and coal ore in the scan.

Obs: 5 camera planes (coal-ore mask, tree/nothing -> here stone mask, solid,
depth, edge) x STACK, plus a 6-scalar oracle direction to the nearest coal
(sin/cos rel yaw, rel pitch, dist, sin/cos pitch) - the same coal list the
scripted burrow uses, so learned vs scripted is an apples comparison.

Curriculum: the scripted burrow (chain_probe.stage_coal) runs the first
help_budget ticks of each episode, annealing to zero - early episodes start
adjacent to the ore, late episodes start at the shaft bottom.

Run (anvil): cd magma && uv run --no-project --with numpy,torch,matplotlib python rl/ppo_coal.py
"""
import json
import math
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import torch
import torch.nn as nn

from look_at_tree import MagmaEnv, EYE, wrap180
from walk_break import CAM_W, CAM_H, GAMMA
import chain_probe as cp

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

N_EPISODES = int(os.environ.get("N_EPISODES", 300))
EP_LEN = int(os.environ.get("EP_LEN", 1000))  # learned ticks after prefix;
                      # the scripted burrow needs ~800-1000t from the 6-block
                      # handoff (each cell is ~60t/block x 2-3 blocks), so a
                      # shorter episode caps success below the skill ceiling
REPEAT = 4
STACK = 3
CURR_EPS = 150
CURR_TICKS = int(os.environ.get("CURR_TICKS", 700))
LAM, CLIP, EPOCHS, LR, ENT = 0.95, 0.2, 4, 3e-4, 0.01
HEADS = [3, 3, 2, 2, 2]
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)
COAL_ID = 16
IX_COAL = 7


def act_dict(idx):
    y, p, f, j, a = idx
    return {"dyaw": YAWS[y], "dpitch": PITCHES[p],
            "forward": int(f), "jump": int(j), "attack": int(a)}


def nearest_coal(obs):
    """(rel_yaw, rel_pitch, dist) to the nearest scanned coal ore, or None."""
    best = None
    ex, ey, ez = obs["x"], obs["y"] + EYE, obs["z"]
    for c in obs["coal"]:
        if c == [0, 0, 0]:
            break
        dx, dy, dz = c[0] + 0.5 - ex, c[1] + 0.5 - ey, c[2] + 0.5 - ez
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        ry = wrap180(math.degrees(math.atan2(-dx, dz)) - obs["yaw"])
        rp = math.degrees(-math.asin(dy / max(dist, 1e-9))) - obs["pitch"]
        if best is None or dist < best[2]:
            best = (ry, rp, dist)
    return best


def planes(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.float32).reshape(CAM_H, CAM_W)
    edg = np.asarray(obs["edge"], dtype=np.float32).reshape(CAM_H, CAM_W)
    p = np.stack([(cam == COAL_ID).astype(np.float32),
                  np.isin(cam, (1, 4)).astype(np.float32),   # stone/cobble
                  (cam != 0).astype(np.float32),
                  dep / 255.0, edg])
    nc = nearest_coal(obs)
    if nc is None:
        s = np.array([0, 0, 0, 1,
                      math.sin(math.radians(obs["pitch"])),
                      math.cos(math.radians(obs["pitch"]))], dtype=np.float32)
    else:
        ry, rp, dist = nc
        s = np.array([math.sin(math.radians(ry)), math.cos(math.radians(ry)),
                      rp / 90.0, min(dist, 24.0) / 24.0,
                      math.sin(math.radians(obs["pitch"])),
                      math.cos(math.radians(obs["pitch"]))], dtype=np.float32)
    return p, s


class ConvPolicy(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(5 * STACK, 16, 5, stride=2), nn.ReLU(),
            nn.Conv2d(16, 32, 3, stride=2), nn.ReLU(), nn.Flatten())
        with torch.no_grad():
            nflat = self.conv(
                torch.zeros(1, 5 * STACK, CAM_H, CAM_W)).shape[1]
        self.fc = nn.Sequential(nn.Linear(nflat + 6, 128), nn.ReLU())
        self.heads = nn.ModuleList([nn.Linear(128, n) for n in HEADS])
        self.value = nn.Linear(128, 1)

    def forward(self, p, s):
        h = self.fc(torch.cat([self.conv(p), s], dim=1))
        return [head(h) for head in self.heads], self.value(h).squeeze(-1)


def rollout_mp(vec, net, n_envs, help_budget):
    """One episode on the multiprocessing vector env (VEC=mp): the workers
    own send/recv/decode/reward/help, the parent only samples actions."""
    cur_p, cur_s, done = vec.reset(help_budget)
    done = [bool(d) for d in done]      # scripted help finished it
    helped_done = list(done)
    success = [False] * n_envs
    traj = [[] for _ in range(n_envs)]
    stacks = [[cur_p[i]] * STACK for i in range(n_envs)]
    idx_arr = np.zeros((n_envs, len(HEADS)), dtype=np.int64)
    for _ in range(EP_LEN // REPEAT):
        live = [i for i in range(n_envs) if not done[i]]
        if not live:
            break
        for i in live:
            stacks[i].pop(0)
            stacks[i].append(cur_p[i])
        ps = torch.from_numpy(np.stack(
            [np.concatenate(stacks[i]) for i in live]))
        ss = torch.from_numpy(np.stack([cur_s[i] for i in live]))
        with torch.no_grad():
            logits, vals = net(ps, ss)
            dists = [torch.distributions.Categorical(logits=lg)
                     for lg in logits]
            acts = [d.sample() for d in dists]
            logp = sum(d.log_prob(a) for d, a in zip(dists, acts))
        for k, i in enumerate(live):
            idx = tuple(int(h[k]) for h in acts)
            idx_arr[i] = idx
            traj[i].append([(ps[k].numpy(), ss[k].numpy()), idx, 0.0,
                            float(logp[k]), float(vals[k])])
        cur_p, cur_s, rew, dn = vec.step(idx_arr)
        for i in live:
            traj[i][-1][2] = float(rew[i])
            if dn[i]:
                done[i] = True
                success[i] = True
    return traj, success, helped_done


def make_env(seed, prefix):
    """Replay the scripted prefix. The replay consumes no obs, so it runs
    pipelined with a small in-flight window instead of lockstep - the
    roundtrip latency, not the sim, dominates replay time. The window must
    stay under the pipe capacity (~4 records of ~13KB in 64KB) or send()
    deadlocks against the env's blocked stdout. Camera renders only on the
    final (handoff) tick."""
    env = MagmaEnv(seed)
    window, inflight = 3, 0
    last = len(prefix) - 1
    for i, a in enumerate(prefix):
        if i != last:
            a = dict(a)
            a["cam"] = 0
        env.send(a)
        inflight += 1
        if inflight > window:
            env.recv()
            inflight -= 1
    for _ in range(inflight):
        env.recv()
    return env


def main():
    os.makedirs(OUT, exist_ok=True)
    torch.manual_seed(0)
    torch.set_num_threads(4)    # leave cores for the env subprocesses
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = sorted(int(s) for s in prefixes)[:16]
    if os.environ.get("TRAIN_SEEDS"):   # e.g. the scripted-completable set
        keep = {int(s) for s in os.environ["TRAIN_SEEDS"].split(",")}
        seeds = [s for s in seeds if s in keep]
    n_envs = len(seeds)
    print(f"training on {n_envs} seeds: {seeds}", flush=True)
    net = ConvPolicy()
    opt = torch.optim.Adam(net.parameters(), lr=LR)
    use_mp = os.environ.get("VEC") == "mp"
    if use_mp:      # process-per-worker vector env: the threaded path GILs
        from vec_env import VecCoalEnv     # out at ~17k steps/s aggregate
        vec = VecCoalEnv(seeds, prefixes)
    else:
        pool = ThreadPoolExecutor(max_workers=n_envs)

    ep_return, ep_success, ep_help = [], [], []
    env0_episodes = []

    for ep in range(N_EPISODES):
        help_budget = int(CURR_TICKS * max(0.0, 1.0 - ep / CURR_EPS))
        used = help_budget
        if use_mp:
            traj, success, helped_done = rollout_mp(vec, net, n_envs,
                                                    help_budget)
        else:
            envs = list(pool.map(lambda s: make_env(s, prefixes[str(s)]),
                                 seeds))
            base_coal = [int(cp.inv(e, IX_COAL)) for e in envs]

            if help_budget > 0:
                # scripted burrow toward the ore but stop at reach: the
                # policy always owns the mine+collect endgame, and
                # progressively more of the tunneling as the budget anneals
                list(pool.map(lambda e: cp.stage_coal(e, budget=help_budget,
                                                      stop_dist=3.0), envs))

            done = [bool(cp.inv(e, IX_COAL) > base_coal[i])
                    for i, e in enumerate(envs)]    # scripted help finished it
            helped_done = list(done)
            success = [False] * n_envs
            prev_dist = [None] * n_envs
            for i, e in enumerate(envs):
                nc = nearest_coal(e.obs)
                prev_dist[i] = nc[2] if nc else None
            traj = [[] for _ in envs]
            stacks = [[planes(e.obs)[0]] * STACK for e in envs]

            for _ in range(EP_LEN // REPEAT):
                live = [i for i in range(n_envs) if not done[i]]
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
                            # planes are consumed once per decision: skip the
                            # raycast on middle repeat ticks, but render on the
                            # final rep - its obs is what the next decision's
                            # frame stack and scalars read. The dense crosshair
                            # check on skipped ticks reads the decision-tick
                            # frame, which is already this decision's aim (dyaw
                            # only moves on rep 0).
                            if rep < REPEAT - 1:
                                a["cam"] = 0
                        envs[i].send(a)
                    for i in live:
                        if done[i]:
                            continue
                        obs = envs[i].recv()
                        r = -0.005
                        nc = nearest_coal(obs)
                        if nc is not None and prev_dist[i] is not None:
                            r += 0.5 * (prev_dist[i] - nc[2])
                        prev_dist[i] = nc[2] if nc else prev_dist[i]
                        # dense endgame signal: attacking with the crosshair on
                        # coal ore is mining progress the sparse +10 cannot
                        # bootstrap on its own (a break needs ~15 consecutive
                        # aligned attack decisions)
                        if (traj[i][-1][1][4]      # this env's attack head ("a"
                                # here is the send loop's leftover: LAST env's
                                # action, a cross-env leak)
                                and obs["cam"][18 * 64 + 32] == COAL_ID):
                            r += 0.03
                        if cp.inv(envs[i], IX_COAL) > base_coal[i]:
                            r += 10.0
                            done[i] = True
                            success[i] = True
                        traj[i][-1][2] += r

        # ---- PPO update ----
        P, S, A, LP, ADV, RET = [], [], [], [], [], []
        rets_all = []
        for i in range(n_envs):
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
        if P:
            P = torch.from_numpy(np.stack(P))
            S = torch.from_numpy(np.stack(S))
            A = torch.tensor(A); LP = torch.tensor(LP)
            ADV = torch.tensor(ADV, dtype=torch.float32)
            RET = torch.tensor(RET, dtype=torch.float32)
            ADV = (ADV - ADV.mean()) / (ADV.std() + 1e-8)
            for _ in range(EPOCHS):
                logits, vals = net(P, S)
                dists = [torch.distributions.Categorical(logits=lg)
                         for lg in logits]
                logp = sum(d.log_prob(A[:, hd]) for hd, d in enumerate(dists))
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
        env0_acts = vec.actions(0) if use_mp else list(envs[0].actions)
        env0_episodes.append((ep, env0_acts, success[0],
                              helped_done[0], float(rets_all[0])))
        if not use_mp:
            for e in envs:
                e.close()
        if ep % 10 == 0 or ep == N_EPISODES - 1:
            print(f"ep {ep:4d}  return {ep_return[-1]:+7.2f}  "
                  f"success {ep_success[-1]:.2f}  help {used:3d}", flush=True)
        if ep % 10 == 9:    # crash/timeout-safe partial artifacts
            torch.save(net.state_dict(), os.path.join(OUT, "coal_net.pt"))
            np.save(os.path.join(OUT, "coal_return.npy"), np.array(ep_return))
            np.save(os.path.join(OUT, "coal_success.npy"),
                    np.array(ep_success))
            np.save(os.path.join(OUT, "coal_help.npy"), np.array(ep_help))
            with open(os.path.join(OUT, "coal_env0_episodes.jsonl"), "w") as f:
                for e_, acts, suc, hd, ret in env0_episodes:
                    f.write(json.dumps({"ep": e_, "success": suc,
                                        "helped_done": hd, "return": ret,
                                        "actions": acts}) + "\n")

    if use_mp:
        vec.close()
    torch.save(net.state_dict(), os.path.join(OUT, "coal_net.pt"))
    np.save(os.path.join(OUT, "coal_return.npy"), np.array(ep_return))
    np.save(os.path.join(OUT, "coal_success.npy"), np.array(ep_success))
    np.save(os.path.join(OUT, "coal_help.npy"), np.array(ep_help))
    with open(os.path.join(OUT, "coal_env0_episodes.jsonl"), "w") as f:
        for ep, acts, suc, hd, ret in env0_episodes:
            f.write(json.dumps({"ep": ep, "success": suc, "helped_done": hd,
                                "return": ret, "actions": acts}) + "\n")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(3, 1, figsize=(8, 8), sharex=True)
    ax[0].plot(ep_return); ax[0].set_ylabel("mean episode return")
    w = min(10, len(ep_success))
    sm = np.convolve(ep_success, np.ones(w) / w, mode="valid")
    ax[1].plot(ep_success, alpha=0.3)
    ax[1].plot(range(w - 1, len(ep_success)), sm)
    ax[1].set_ylabel("success (coal mined, unassisted)")
    ax[2].plot(ep_help); ax[2].set_ylabel("scripted help ticks")
    ax[2].set_xlabel("episode")
    fig.suptitle("PPO mine-coal: burrow to scanned coal ore and mine it")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "coal_curve.png"), dpi=140)
    print("saved", os.path.join(OUT, "coal_curve.png"), flush=True)


if __name__ == "__main__":
    main()
