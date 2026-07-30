"""Batched-GPU PPO for the mine-coal skill (M4): VecBlaze on the 3090.

Same net (ppo_coal.ConvPolicy), same PPO hyperparameters and reward/done
semantics (reward is computed in-kernel, bitwise-gated vs the real env), but
N lanes step in lockstep on the GPU instead of 8 subprocess envs.

Curriculum = snapshot-stage annealing (DESIGN.md Part 6): every lane reset
samples a (seed, stage) snapshot; early in training most lanes start at
d3.0 (burrow already within 3 blocks of the ore), annealing toward d6.0
(the true scripted-prefix handoff). This replaces ppo_coal's in-episode
scripted help. Seeds 14,16,20,27,29,32,44,46 only; 11 etc stay held out.

Rollout scheme: continuous batched stepping with per-lane episode
bookkeeping. A lane that terminates (done=1 coal mined, done=2 dead/out of
region) or exceeds EP_LEN ticks is masked-reset to a freshly sampled
snapshot; the decision right after a reset is a forced no-op "burn-in"
(renders the first frame of the new episode - reset alone does not render)
and is excluded from the PPO loss. PPO updates run on fixed chunks of
T_CHUNK decisions x N lanes with GAE over per-lane episode boundaries:
terminal steps get V_next=0 and a cut GAE chain; truncated (EP_LEN) steps
bootstrap gamma*V(s_next) - the post-step obs is forwarded before the lane
is reset, so V(s_next) is the true successor value - with the chain cut.

Run (anvil, GPU1/3090 - check nvidia-smi first):
  cd magma && uv run --no-project --with numpy,torch,matplotlib \
      python blaze/env/ppo_coal_cu.py
"""
import os
import sys
import time
from collections import deque

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.join(os.path.dirname(HERE), "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)

from blaze import VecBlaze, CUDA_SO, CAM_H, CAM_W          # noqa: E402
from ppo_coal import (ConvPolicy, STACK, REPEAT, HEADS, EP_LEN,   # noqa: E402
                      LAM, CLIP, EPOCHS, LR, ENT, COAL_ID)
from walk_break import GAMMA                                # noqa: E402

OUT = os.path.join(RL, "out")

TRAIN_SEEDS = [int(s) for s in os.environ.get(
    "TRAIN_SEEDS", "14,16,20,27,29,32,44,46").split(",")]
STAGES = ("3.0", "4.5", "6.0")
N_ENVS = int(os.environ.get("N_ENVS", 2048))
T_CHUNK = int(os.environ.get("T_CHUNK", 32))       # decisions per PPO chunk
# training episode cap; eval_coal always uses EP_LEN=1000, but a longer
# training horizon lets hard (seed,stage) cells reach the +10 at all (the
# time penalty and distance shaping still push toward finishing fast)
EP_TICKS = int(os.environ.get("EP_TICKS", EP_LEN))
EP_DEC = EP_TICKS // REPEAT                        # decisions per episode
DEVICE = int(os.environ.get("BLAZE_DEV", 1))       # 1=3090, 0=PRO 6000
# OPT-IN training-reward gate (blaze_set_reward_gate): nearest-coal dist
# must be <= REW_GATE for the +0.03 crosshair-attack bonus. 0 = off (exact
# ppo_coal semantics). 3.2 = chain_probe's mine-safely radius - beyond it
# the drop strands out of pickup range, and the ungated bonus taught
# mine-on-sight from afar (the M4 transfer-gap root cause).
REW_GATE = float(os.environ.get("REW_GATE", 0.0))
MAX_TICKS = float(os.environ.get("MAX_TICKS", 50e6))
MAX_WALL = float(os.environ.get("MAX_WALL", 45 * 60))
ANNEAL_TICKS = float(os.environ.get("ANNEAL_TICKS", 12e6))
# Stability deltas vs ppo_coal (which does 4 full-batch steps/update on ~1-2k
# transitions): a 65k-transition chunk at MB=8192 x 4 epochs = 32 optimizer
# steps/update collapsed the policy (d6.0 0.75 -> 0.26). Fewer, larger,
# clipped steps with a decaying lr hold the learned policy.
MB = int(os.environ.get("MB", 16384))              # PPO minibatch
EPOCHS_CU = int(os.environ.get("EPOCHS_CU", 2))    # ppo_coal uses 4
GRAD_CLIP = float(os.environ.get("GRAD_CLIP", 0.5))
LR_DECAY_TICKS = float(os.environ.get("LR_DECAY_TICKS", 20e6))
LR_FLOOR = float(os.environ.get("LR_FLOOR", 1e-4))
CKPT_TICKS = 200_000
TRAIL = 200                                        # trailing-episode window
TARGET = 0.60                                      # d6.0 success gate
NOOP = (1, 1, 0, 0, 0)                             # dyaw 0, dpitch 0, idle
NCH = 5 * STACK


class Curriculum:
    """Adaptive per-(seed,stage) frontier sampling over per-seed chains.

    A global d3.0->d6.0 anneal plateaued at ~0.45 d6.0: per-seed
    heterogeneity is large (some seeds solve d6.0 in 20 chunks, others sit
    at 0.00 with a cliff between stages), so solved cells soaked up most of
    the batch. Instead, each seed trains mostly at its FRONTIER - the
    easiest stage not yet mastered (trailing success < MASTER) - with a
    small rehearsal share on mastered stages (forgetting guard) and a small
    lookahead share on later ones. Fully mastered seeds train mostly the
    last (hardest = true handoff) stage. Chains may differ in length per
    seed (burrow seeds get extra m<k> slice snapshots between d3.0 and
    d4.5, easiest first).

    chains: list (per seed) of lists of global cell/snapshot indices,
    ordered easiest -> hardest; the last entry is the d6.0 handoff.
    """
    MASTER = 0.7
    MIN_EPS = 20
    WIN = 60

    def __init__(self, chains, rng):
        self.rng = rng
        self.chains = chains
        ncells = sum(len(c) for c in chains)
        self.hist = [deque(maxlen=self.WIN) for _ in range(ncells)]
        self.cell_pos = {}       # global cell -> (seed_i, pos)
        for si, ch in enumerate(chains):
            for pos, cell in enumerate(ch):
                self.cell_pos[cell] = (si, pos)

    def record(self, cell, ok):
        self.hist[cell].append(ok)

    def cell_succ_at(self, cell):
        h = self.hist[cell]
        return (sum(h) / len(h)) if h else 0.0

    def hard_succ(self, si):
        """Success on the seed's hardest (handoff) stage."""
        return self.cell_succ_at(self.chains[si][-1])

    def sample(self, k):
        out = np.empty(k, dtype=np.int32)
        ws = []
        for ch in self.chains:
            frontier = len(ch) - 1
            for pos, cell in enumerate(ch):
                h = self.hist[cell]
                if len(h) < self.MIN_EPS or \
                        sum(h) / len(h) < self.MASTER:
                    frontier = pos
                    break
            w = np.full(len(ch), 0.1)
            w[frontier] += 0.7
            ws.append(w / w.sum())
        seeds = self.rng.integers(0, len(self.chains), size=k)
        for j in range(k):
            si = seeds[j]
            pos = self.rng.choice(len(self.chains[si]), p=ws[si])
            out[j] = self.chains[si][pos]
        return out

    def matrix(self):
        return "  ".join(
            "/".join(f"{self.cell_succ_at(c):.2f}" for c in ch)
            for ch in self.chains)


def build_frame(cam, depth, edge):
    """[N,5,36,64] uint8: coal/stone/solid masks, raw depth, edge.
    Mirrors ppo_coal.planes(); depth is divided by 255 at float time."""
    coal = (cam == COAL_ID)
    stone = (cam == 1) | (cam == 4)
    solid = (cam != 0)
    return torch.stack([coal.to(torch.uint8), stone.to(torch.uint8),
                        solid.to(torch.uint8), depth, edge], dim=1)


def obs_float(u8):
    """uint8 stacked planes -> float, depth channels scaled to [0,1]."""
    f = u8.float()
    for k in range(STACK):
        f[:, 5 * k + 3] /= 255.0
    return f


def main():
    os.makedirs(OUT, exist_ok=True)
    torch.manual_seed(0)
    rng = np.random.default_rng(0)
    dev = torch.device(f"cuda:{DEVICE}")
    print(f"device cuda:{DEVICE} = {torch.cuda.get_device_name(DEVICE)}",
          flush=True)

    import glob as _glob
    import re as _re
    paths, stage_of, chains = [], [], []
    for s in TRAIN_SEEDS:
        chain_files = [os.path.join(OUT, "snaps", f"s{s}_d3.0.bsnp")]
        # m<k> burrow-slice snapshots (make_slices.py), closest to the ore
        # (largest k) first = easiest first
        ms = _glob.glob(os.path.join(OUT, "snaps", f"s{s}_m*.bsnp"))
        ms.sort(key=lambda p: -int(_re.search(r"_m(\d+)", p).group(1)))
        chain_files += ms
        chain_files += [os.path.join(OUT, "snaps", f"s{s}_d4.5.bsnp"),
                        os.path.join(OUT, "snaps", f"s{s}_d6.0.bsnp")]
        ch = []
        for pos, p in enumerate(chain_files):
            assert os.path.exists(p), p
            ch.append(len(paths))
            paths.append(p)
            stage_of.append(0 if pos == 0 else
                            (2 if pos == len(chain_files) - 1 else 1))
        chains.append(ch)
    stage_of = np.array(stage_of)

    env = VecBlaze(N_ENVS, device=DEVICE, so_path=CUDA_SO)
    env.load_snapshots(paths)
    if REW_GATE > 0:
        env.set_reward_gate(REW_GATE)
        print(f"training-reward gate ON: +0.03 requires dist <= {REW_GATE}",
              flush=True)

    net = ConvPolicy().to(dev)
    if os.environ.get("WARM"):
        net.load_state_dict(torch.load(
            os.path.join(OUT, os.environ["WARM"]), weights_only=True,
            map_location=dev))
        print(f"warm start from {os.environ['WARM']}", flush=True)
    opt = torch.optim.Adam(net.parameters(), lr=LR)

    curr = Curriculum(chains, rng)
    lane_snap = curr.sample(N_ENVS)
    env.assign(lane_snap)
    env.reset()

    # rolling per-lane state
    stack_u8 = torch.zeros((N_ENVS, NCH, CAM_H, CAM_W), dtype=torch.uint8,
                           device=dev)
    scal_cur = torch.zeros((N_ENVS, 6), dtype=torch.float32, device=dev)
    burnin = torch.ones(N_ENVS, dtype=torch.bool, device=dev)
    # stagger first-episode lengths so truncations don't arrive in lockstep
    # waves (which made the trailing success window oscillate wildly)
    ep_dec = torch.randint(0, EP_DEC, (N_ENVS,), dtype=torch.int32,
                           device=dev,
                           generator=torch.Generator(device=dev)
                           .manual_seed(1))
    ep_ret = torch.zeros(N_ENVS, dtype=torch.float32, device=dev)
    noop_t = torch.tensor(NOOP, dtype=torch.int32, device=dev)

    # chunk buffers (obs stored stacked as uint8: T*N*15*2304 ~ 2.3 GB)
    obs_b = torch.zeros((T_CHUNK, N_ENVS, NCH, CAM_H, CAM_W),
                        dtype=torch.uint8, device=dev)
    scal_b = torch.zeros((T_CHUNK, N_ENVS, 6), device=dev)
    act_b = torch.zeros((T_CHUNK, N_ENVS, 5), dtype=torch.int64, device=dev)
    logp_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    val_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    rew_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    term_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    cut_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    valid_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)

    # trailing per-stage episode logs
    trail = [deque(maxlen=TRAIL) for _ in STAGES]
    n_eps = [0, 0, 0]
    n_suc = [0, 0, 0]
    lane_stage = torch.as_tensor(stage_of[lane_snap], device=dev)

    curve = []          # (ticks, wall, s3, s45, s6, overall, n6)
    ticks = 0
    next_ckpt = CKPT_TICKS
    t_start = time.perf_counter()
    chunk = 0
    stop_reason = None
    best = {"s6": -1.0, "ticks": 0}

    def trailing(si):
        d = trail[si]
        return (sum(d) / len(d)) if d else float("nan")

    def cell6():
        """Unbiased handoff success: mean over seeds of each seed's own
        d6.0 cell window (the raw trailing-200 stream is biased toward
        whichever seeds the curriculum currently sends to the handoff)."""
        return float(np.mean([curr.hard_succ(si)
                              for si in range(len(TRAIN_SEEDS))]))

    def save_all():
        # coal_net_cu.pt = net with the BEST unbiased d6.0 so far (PPO
        # oscillates; the latest net is not necessarily the transfer one)
        s6 = cell6()
        if len(trail[2]) >= TRAIL and s6 > best["s6"]:
            best["s6"], best["ticks"] = s6, ticks
            torch.save({k: v.cpu() for k, v in net.state_dict().items()},
                       os.path.join(OUT, "coal_net_cu.pt"))
        torch.save({k: v.cpu() for k, v in net.state_dict().items()},
                   os.path.join(OUT, "coal_net_cu_last.pt"))
        np.save(os.path.join(OUT, "coal_curve_cu.npy"),
                np.array(curve, dtype=np.float64))

    def save_png():
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        c = np.array(curve)
        if len(c) < 2:
            return
        fig, ax = plt.subplots(figsize=(8, 5))
        for col, lab in ((2, "easy"), (3, "mid"), (4, "handoff"),
                         (5, "overall"), (7, "handoff unbiased (cell mean)")):
            ax.plot(c[:, 0] / 1e6, c[:, col], label=lab,
                    lw=2.5 if col == 7 else 1.2)
        ax.axhline(TARGET, color="k", ls="--", lw=0.8)
        ax.set_xlabel("env ticks (M)")
        ax.set_ylabel(f"success (trailing {TRAIL} episodes)")
        ax.set_ylim(-0.02, 1.02)
        ax.legend()
        ax.set_title("batched PPO mine-coal (blaze, 3090): success by stage")
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, "coal_curve_cu.png"), dpi=140)
        plt.close(fig)

    print(f"N={N_ENVS} lanes, {len(paths)} snapshots "
          f"({len(TRAIN_SEEDS)} seeds x {STAGES}), chunk {T_CHUNK} decisions "
          f"({N_ENVS * T_CHUNK} transitions/update), device cuda:{DEVICE}",
          flush=True)

    while True:
        for t in range(T_CHUNK):
            with torch.no_grad():
                logits, vals = net(obs_float(stack_u8), scal_cur)
                dists = [torch.distributions.Categorical(logits=lg)
                         for lg in logits]
                acts = torch.stack([d.sample() for d in dists], dim=1)
                acts[burnin] = noop_t.long()
                logp = sum(d.log_prob(acts[:, h])
                           for h, d in enumerate(dists))

            obs_b[t] = stack_u8
            scal_b[t] = scal_cur
            act_b[t] = acts
            logp_b[t] = logp
            val_b[t] = vals
            valid_b[t] = ~burnin

            cam, depth, edge, scal, rew, done, pose = env.step(
                acts.to(torch.int32), repeat=REPEAT)

            term = done > 0
            success = done == 1
            ep_dec += (~burnin).int()
            trunc = (~term) & (ep_dec >= EP_DEC)
            ended = term | trunc
            rew_b[t] = rew
            term_b[t] = term
            cut_b[t] = ended
            ep_ret += rew

            # roll frame stacks; burn-in lanes fill all 3 slots
            frame = build_frame(cam, depth, edge)
            stack_u8[:, :-5] = stack_u8[:, 5:].clone()
            stack_u8[:, -5:] = frame
            if burnin.any():
                bi = burnin
                stack_u8[bi] = frame[bi].repeat(1, STACK, 1, 1)
            scal_cur = scal.clone()
            burnin = torch.zeros_like(burnin)

            if ended.any():
                idx = ended.nonzero(as_tuple=True)[0]
                st_l = lane_stage[idx].tolist()
                suc_l = success[idx].tolist()
                idx_h = idx.cpu().numpy()
                for lane, s_i, ok in zip(idx_h, st_l, suc_l):
                    trail[s_i].append(float(ok))
                    n_eps[s_i] += 1
                    n_suc[s_i] += int(ok)
                    curr.record(int(lane_snap[lane]), float(ok))
                lane_snap[idx_h] = curr.sample(len(idx_h))
                env.assign(lane_snap)
                mask = np.zeros(N_ENVS, dtype=np.uint8)
                mask[idx_h] = 1
                env.reset(mask)
                lane_stage[idx] = torch.as_tensor(
                    stage_of[lane_snap[idx_h]], device=dev)
                ep_dec[idx] = 0
                ep_ret[idx] = 0.0
                burnin[idx] = True

            ticks += N_ENVS * REPEAT

        # ---- GAE over the chunk (per-lane episode boundaries) ----
        with torch.no_grad():
            _, next_val = net(obs_float(stack_u8), scal_cur)
            adv = torch.zeros_like(rew_b)
            gae = torch.zeros(N_ENVS, device=dev)
            nextv = next_val
            for t in reversed(range(T_CHUNK)):
                nonterm = (~term_b[t]).float()
                keep = (~cut_b[t]).float()
                delta = rew_b[t] + GAMMA * nextv * nonterm - val_b[t]
                gae = delta + GAMMA * LAM * keep * gae
                adv[t] = gae
                nextv = val_b[t]
            ret = adv + val_b

        # ---- PPO update on valid transitions ----
        sel = valid_b.flatten().nonzero(as_tuple=True)[0]
        n_tr = sel.numel()
        if n_tr:
            fO = obs_b.flatten(0, 1)
            fS = scal_b.flatten(0, 1)
            fA = act_b.flatten(0, 1)
            fLP = logp_b.flatten()
            fADV = adv.flatten()[sel]
            fRET = ret.flatten()[sel]
            fADV = (fADV - fADV.mean()) / (fADV.std() + 1e-8)
            lr_now = max(LR_FLOOR,
                         LR * (1.0 - min(1.0, ticks / LR_DECAY_TICKS)
                               * (1.0 - LR_FLOOR / LR)))
            for g in opt.param_groups:
                g["lr"] = lr_now
            for _ in range(EPOCHS_CU):
                perm = sel[torch.randperm(n_tr, device=dev)]
                order = torch.searchsorted(
                    sel, perm)      # map into fADV/fRET rows
                for k in range(0, n_tr, MB):
                    mb = perm[k:k + MB]
                    mo = order[k:k + MB]
                    logits, vals = net(obs_float(fO[mb]), fS[mb])
                    dists = [torch.distributions.Categorical(logits=lg)
                             for lg in logits]
                    logp = sum(d.log_prob(fA[mb][:, h])
                               for h, d in enumerate(dists))
                    ratio = torch.exp(logp - fLP[mb])
                    a_mb = fADV[mo]
                    pg = -torch.min(
                        ratio * a_mb,
                        torch.clamp(ratio, 1 - CLIP, 1 + CLIP) * a_mb)
                    entr = sum(d.entropy() for d in dists)
                    loss = pg.mean() + 0.5 * ((fRET[mo] - vals) ** 2).mean() \
                        - ENT * entr.mean()
                    opt.zero_grad()
                    loss.backward()
                    torch.nn.utils.clip_grad_norm_(net.parameters(),
                                                   GRAD_CLIP)
                    opt.step()

        chunk += 1
        wall = time.perf_counter() - t_start
        s3, s45, s6 = (trailing(i) for i in range(3))
        allq = [x for d in trail for x in d]
        s_all = sum(allq) / len(allq) if allq else float("nan")
        c6 = cell6()
        curve.append((ticks, wall, s3, s45, s6, s_all, len(trail[2]), c6))
        if chunk % 5 == 0 or chunk == 1:
            print(f"chunk {chunk:4d}  ticks {ticks/1e6:6.2f}M  "
                  f"wall {wall:6.1f}s  {ticks/max(wall,1e-9)/1e6:.3f}M t/s  "
                  f"succ easy {s3:.2f} mid {s45:.2f} hand {s6:.2f} "
                  f"(nh {n_eps[2]})  all {s_all:.2f}  cell6 {c6:.2f}",
                  flush=True)
        if chunk % 25 == 0:
            print(f"  cells (easy->handoff per seed "
                  f"{TRAIN_SEEDS}): {curr.matrix()}", flush=True)
        if ticks >= next_ckpt:
            save_all()
            next_ckpt = (ticks // CKPT_TICKS + 1) * CKPT_TICKS
        if chunk % 10 == 0:
            save_png()

        if (len(trail[2]) >= TRAIL and ticks >= ANNEAL_TICKS
                and c6 >= TARGET + 0.05):
            stop_reason = f"target reached (unbiased cell d6.0 {c6:.2f})"
        elif ticks >= MAX_TICKS:
            stop_reason = f"tick budget ({ticks/1e6:.1f}M)"
        elif wall >= MAX_WALL:
            stop_reason = f"wall budget ({wall:.0f}s)"
        if stop_reason:
            break

    save_all()
    save_png()
    print(f"stop: {stop_reason}", flush=True)
    print(f"episodes by stage: d3.0 {n_suc[0]}/{n_eps[0]}  "
          f"d4.5 {n_suc[1]}/{n_eps[1]}  d6.0 {n_suc[2]}/{n_eps[2]}",
          flush=True)
    print(f"final trailing-{TRAIL}: d3.0 {trailing(0):.3f}  "
          f"d4.5 {trailing(1):.3f}  d6.0 {trailing(2):.3f}", flush=True)
    print(f"best d6.0 trailing-{TRAIL}: {best['s6']:.3f} at "
          f"{best['ticks']/1e6:.2f}M ticks -> coal_net_cu.pt", flush=True)
    print(f"total {ticks/1e6:.2f}M ticks in {time.perf_counter()-t_start:.0f}s"
          f" -> saved {os.path.join(OUT, 'coal_net_cu.pt')}", flush=True)
    env.close()


if __name__ == "__main__":
    main()
