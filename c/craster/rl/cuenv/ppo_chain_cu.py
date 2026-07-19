"""Batched-GPU PPO for the FULL spawn-to-torch chain, zero scripted actions.

Env: VecCuEnv on the 13 fresh-spawn t0 snapshots (128^3 regions), full
[N,13] action rows built from 9 discrete heads (dyaw, dpitch, forward, jump,
attack, use, craft, interact, hotbar; +smelt with IRON_CHAIN=1). The env
fires +10/done=1 on the FIRST
TORCH (cuenv_set_success_item(50)); mining coal no longer ends the episode.

Reward is computed by reward_chain.ChainReward (spec-driven, GPU tensors)
from the cuenv_step_full status readout (9 inv counts + hotbar_sel + held
item + container): one-time milestone bonuses, death/out penalty, per-decision
time cost, and stage-gated dense shaping. The resolved spec prints at start
and is saved as a JSON sidecar next to checkpoints. Override weights via
REWARD_JSON (COAL_CHEW/HUNT_DESC envs still override the v2 touchstones).
Rewards may use snapshot oracles (static log lists); the POLICY inputs are
strictly transferable (camera planes, coal scalars, inv/status, pose) and
computable from the real env's BOLR obs.

Curriculum: self-generated start states. When a lane crosses a milestone
(3 logs / pick / 3 cobble / coal), its live state is captured into a fixed
(seed, stage) snapshot slot (cuenv_capture); resets sample per-seed frontier
stages (the earliest stage whose next-milestone success is still low) plus a
fixed share of true t0 starts. No scripted actions anywhere - every captured
state was produced by the policy itself.

Run (anvil, GPU0 - preflight nvidia-smi):
  cd c/craster && uv run --no-project --with numpy,torch,matplotlib \
      python rl/cuenv/ppo_chain_cu.py
"""
import os
import sys
import struct
import time
from collections import deque

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, RL)

from cuenv import VecCuEnv, CUDA_SO, CAM_H, CAM_W          # noqa: E402
from reward_chain import ChainReward, ChainRewardSpec      # noqa: E402

OUT = os.path.join(RL, "out")
SNAPS = os.path.join(OUT, "snaps")

# ---- run config ----
TRAIN_SEEDS = [int(s) for s in os.environ.get(
    "TRAIN_SEEDS", "2,3,10,14,16,20,27,29,32,44,46").split(",")]
N_ENVS = int(os.environ.get("N_ENVS", 4096))
DEVICE = int(os.environ.get("CUENV_DEV", 0))       # 0 = PRO 6000
REPEAT = 4
STACK = 2
T_CHUNK = int(os.environ.get("T_CHUNK", 32))
EP_DEC = int(os.environ.get("EP_DEC", 1500))       # decisions (x4 ticks)
MAX_TICKS = float(os.environ.get("MAX_TICKS", 3e9))
MAX_WALL = float(os.environ.get("MAX_WALL", 6.5 * 3600))
GAMMA = float(os.environ.get("GAMMA", 0.995))
LAM, CLIP = 0.95, 0.2
EPOCHS = int(os.environ.get("EPOCHS", 2))
MB = int(os.environ.get("MB", 8192))
LR = float(os.environ.get("LR", 3e-4))
LR_FLOOR = float(os.environ.get("LR_FLOOR", 1e-4))
LR_DECAY_TICKS = float(os.environ.get("LR_DECAY_TICKS", 1.5e9))
ENT = float(os.environ.get("ENT", 0.01))
GRAD_CLIP = 0.5
CKPT_TICKS = 2_000_000
TRAIL = 200
T0_SHARE = float(os.environ.get("T0_SHARE", 0.30))
CAP_REFRESH = int(os.environ.get("CAP_REFRESH", 25))   # chunks between
                                                       # re-captures per cell
WARM = os.environ.get("WARM")
OUT_NAME = os.environ.get("OUT_NAME", "chain_net_cu")   # checkpoint stem
CURVE_NAME = os.environ.get("CURVE_NAME", "chain_curve_cu")
# Episode success item id: 50=torch (default), 274=stone pick, 257=iron pick
# (IRON_CHAIN=1 required for craft:6/7 + smelt head).
SUCCESS_ITEM = int(os.environ.get("SUCCESS_ITEM", 50))

# reward spec (weights + v2 touchstones; REWARD_JSON / COAL_CHEW / HUNT_DESC)
REWARD_SPEC = ChainRewardSpec.resolve()
COAL_CHEW = REWARD_SPEC.coal_chew      # kept for the launch line echo
HUNT_DESC = REWARD_SPEC.hunt_desc

# ---- action heads ----
# dyaw{-15,0,15} dpitch{-10,0,10} fwd{-1,0,1} jump attack use
# craft{none,0..5} interact hotbar{none,0..8}
# IRON_CHAIN=1 widens craft to {none,0..7} (furnace, iron pick) and appends
# a smelt{0,1} head. Default off: HEADS (and any saved net, e.g.
# chain_net_cu_v2.pt) stay byte-compatible with the stone chain.
IRON_CHAIN = int(os.environ.get("IRON_CHAIN", "0")) != 0
HEADS = [3, 3, 3, 2, 2, 2, (9 if IRON_CHAIN else 7), 2, 10] \
    + ([2] if IRON_CHAIN else [])
NHEAD = len(HEADS)
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)
FWD = (-1.0, 0.0, 1.0)

# ---- obs ----
NPLANES = 9        # log leaves coal stone dirt table solid depth edge
NCH = NPLANES * STACK
# status columns (cuenv_fill_status): 9 inv counts + hotbar_sel + held +
# container; inv order = rl_inv_ids
IX_LOG, IX_PLANK, IX_STICK, IX_COBBLE, IX_TABLE, IX_WPICK, IX_SPICK, \
    IX_COAL, IX_TORCH = range(9)
SEL_ITEMS = (17, 5, 280, 4, 58, 270, 274, 263, 50)   # one-hot categories
NSCAL = 6 + 9 + 1 + len(SEL_ITEMS) + 1 + 1           # 27
# milestone stages: 0=t0 1=logs3 2=wpick 3=cobble3 4=coal
# IRON_CHAIN extends: 5=torch 6=furnace 7=ironore 8=ingot  (success=ipick ends)
N_STAGES = 9 if IRON_CHAIN else 5
MILE_NAMES = (("t0", "logs3", "pick", "cobble3", "coal", "torch",
               "furnace", "ironore", "ingot") if IRON_CHAIN
              else ("t0", "logs3", "pick", "cobble3", "coal"))

CX, CY = 32, 18    # crosshair pixel


def build_frame(cam, depth, edge):
    """[N,9,36,64] uint8 planes. Transferable: pure function of the BOLR
    cam/depth/edge fields (identical in cuenv and the real env)."""
    return torch.stack([
        (cam == 17).to(torch.uint8),                    # log
        (cam == 18).to(torch.uint8),                    # leaves
        (cam == 16).to(torch.uint8),                    # coal ore
        ((cam == 1) | (cam == 4)).to(torch.uint8),      # stone/cobble
        ((cam == 2) | (cam == 3)).to(torch.uint8),      # grass/dirt
        (cam == 58).to(torch.uint8),                    # crafting table
        (cam != 0).to(torch.uint8),                     # solid
        depth, edge], dim=1)


def build_scal(scal, status, pose, tfrac):
    """[N,28] float32 policy scalars: env coal scalars, inv counts,
    container, held-item one-hot, hotbar flag-free extras. All fields exist
    identically in the real env's BOLR record."""
    inv = status[:, :9].float().clamp(max=10.0) / 10.0
    held = status[:, 10]
    onehot = torch.stack([(held == i).float() for i in SEL_ITEMS], dim=1)
    cont = (status[:, 11] > 0).float().unsqueeze(1)
    y = (pose[:, 1] / 64.0).unsqueeze(1)
    return torch.cat([scal, inv, cont, onehot, y,
                      tfrac.unsqueeze(1)], dim=1)


_OF_BUF = {}


def obs_float(u8):
    """uint8 planes -> float32, depth scaled /255. ONE growable buffer per
    device (only dim0 varies across calls); a [:n] view is returned. A
    per-shape cache is a LEAK: ragged last-minibatch sizes mint a new
    multi-GB buffer per distinct n (v1 of this pinned ~10GB/25 chunks and
    OOMed the pin run). Safe because fwd consumes the view before the next
    copy_ overwrites (in-order on one stream)."""
    n = u8.shape[0]
    key = (u8.device, u8.shape[1:])
    buf = _OF_BUF.get(key)
    if buf is None or buf.shape[0] < n:
        buf = torch.empty((n,) + u8.shape[1:], dtype=torch.float32,
                          device=u8.device)
        _OF_BUF[key] = buf
    out = buf[:n]
    out.copy_(u8, non_blocking=True)
    for k in range(STACK):
        out[:, NPLANES * k + 7] /= 255.0      # depth plane
    return out


class ChainPolicy(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(NCH, 32, 5, stride=2), nn.ReLU(),
            nn.Conv2d(32, 64, 3, stride=2), nn.ReLU(), nn.Flatten())
        with torch.no_grad():
            nflat = self.conv(torch.zeros(1, NCH, CAM_H, CAM_W)).shape[1]
        self.fc = nn.Sequential(nn.Linear(nflat + NSCAL, 256), nn.ReLU())
        self.heads = nn.ModuleList([nn.Linear(256, n) for n in HEADS])
        self.value = nn.Linear(256, 1)

    def forward(self, p, s):
        h = self.fc(torch.cat([self.conv(p), s], dim=1))
        return [head(h) for head in self.heads], self.value(h).squeeze(-1)


def acts_to_rows(a, dev):
    """[N,NHEAD] int64 head samples -> [N,13] float64 raw action rows."""
    n = a.shape[0]
    rows = torch.zeros((n, 13), dtype=torch.float64, device=dev)
    rows[:, 2] = torch.tensor(YAWS, dtype=torch.float64, device=dev)[a[:, 0]]
    rows[:, 3] = torch.tensor(PITCHES, dtype=torch.float64,
                              device=dev)[a[:, 1]]
    rows[:, 0] = torch.tensor(FWD, dtype=torch.float64, device=dev)[a[:, 2]]
    rows[:, 4] = a[:, 3].double()                    # jump
    rows[:, 7] = a[:, 4].double()                    # attack
    rows[:, 8] = a[:, 5].double()                    # use
    rows[:, 10] = a[:, 6].double() - 1.0             # craft: 0->-1
    rows[:, 11] = a[:, 7].double()                   # interact
    rows[:, 9] = a[:, 8].double() - 1.0              # hotbar: 0->-1
    if IRON_CHAIN:
        rows[:, 12] = a[:, 9].double()               # smelt
    return rows


# ---- static log oracle (reward only, never a policy input) ----
HEAD_SIZE, N_ITEMS_OFF, RDIMS_OFF, ITEM_SIZE = 752, 724, 728, 76


def snapshot_logs(path):
    """World coords of every log (id 17) in a .bsnp region."""
    with open(path, "rb") as f:
        buf = f.read()
    assert buf[:4] == b"BSNP", path
    n_items = struct.unpack_from("<I", buf, N_ITEMS_OFF)[0]
    rx0, ry0, rz0, rnx, rny, rnz = struct.unpack_from("<6i", buf, RDIMS_OFF)
    cells = np.frombuffer(buf, "<u2", rnx * rny * rnz,
                          HEAD_SIZE + ITEM_SIZE * n_items)
    ids = (cells >> 4).reshape(rnx, rny, rnz)
    ix, iy, iz = np.nonzero(ids == 17)
    return np.stack([ix + rx0 + 0.5, iy + ry0 + 0.5, iz + rz0 + 0.5],
                    axis=1).astype(np.float32)


def stage_of_best(best, best_iron=None):
    """[N] int64 milestone stage from the BEST-SO-FAR counters (instantaneous
    counts drop when crafts consume items; milestones must not regress).
    best_iron: optional [N,4] furnace/ironore/ingot/ipick bests when IRON_CHAIN."""
    st = torch.zeros(best.shape[0], dtype=torch.int64, device=best.device)
    st[(best[:, IX_LOG] >= 3) | (best[:, IX_PLANK] >= 1)] = 1
    st[best[:, IX_WPICK] >= 1] = 2
    st[(best[:, IX_WPICK] >= 1) & (best[:, IX_COBBLE] >= 3)] = 3
    st[best[:, IX_COAL] >= 1] = 4
    if IRON_CHAIN and best_iron is not None:
        st[best[:, IX_TORCH] >= 1] = 5
        st[best_iron[:, 0] >= 1] = 6          # furnace item
        st[best_iron[:, 1] >= 1] = 7          # iron ore
        st[best_iron[:, 2] >= 1] = 8          # ingot
    return st


class StageCurriculum:
    """Per-seed frontier sampling over the milestone stages.

    Availability: stage 0 (t0) always; stages 1..4 once the policy has
    captured a state there. Sampling: T0_SHARE of lanes start at t0; the
    rest go to the seed's frontier stage (earliest available stage whose
    next-milestone success is < MASTER) with rehearsal spread."""
    MASTER = 0.6
    MIN_EPS = 15
    WIN = 60

    def __init__(self, nseeds, rng):
        self.rng = rng
        self.nseeds = nseeds
        self.avail = np.zeros((nseeds, N_STAGES), dtype=bool)
        self.avail[:, 0] = True
        self.hist = [[deque(maxlen=self.WIN) for _ in range(N_STAGES)]
                     for _ in range(nseeds)]

    def record(self, seed_i, stage, ok):
        self.hist[seed_i][stage].append(float(ok))

    def succ(self, seed_i, stage):
        h = self.hist[seed_i][stage]
        return (sum(h) / len(h)) if h else 0.0

    def sample(self, k):
        seeds = self.rng.integers(0, self.nseeds, size=k)
        stages = np.zeros(k, dtype=np.int64)
        for j in range(k):
            si = seeds[j]
            if self.rng.random() < T0_SHARE:
                continue                      # t0
            av = [s for s in range(N_STAGES) if self.avail[si, s]]
            frontier = av[-1]
            for s in av:
                h = self.hist[si][s]
                if len(h) < self.MIN_EPS or \
                        sum(h) / len(h) < self.MASTER:
                    frontier = s
                    break
            w = np.full(len(av), 0.15)
            w[av.index(frontier)] += 1.0 - 0.15 * len(av)
            stages[j] = self.rng.choice(av, p=w / w.sum())
        return seeds, stages

    def matrix(self):
        return " | ".join(
            "".join(f"{self.succ(si, s):.2f} " if self.avail[si, s]
                    else " --  " for s in range(N_STAGES))
            for si in range(self.nseeds))


def main():
    os.makedirs(OUT, exist_ok=True)
    torch.manual_seed(0)
    rng = np.random.default_rng(0)
    dev = torch.device(f"cuda:{DEVICE}")
    print(f"device cuda:{DEVICE} = {torch.cuda.get_device_name(DEVICE)}",
          flush=True)
    nseeds = len(TRAIN_SEEDS)

    paths = [os.path.join(SNAPS, f"s{s}_t0.bsnp") for s in TRAIN_SEEDS]
    for p in paths:
        assert os.path.exists(p), p

    env = VecCuEnv(N_ENVS, device=DEVICE, so_path=CUDA_SO)
    env.set_success_item(SUCCESS_ITEM)        # default 50 torch; 274=spick abuse
    print(f"success_item={SUCCESS_ITEM}  REWARD_JSON={os.environ.get('REWARD_JSON')}",
          flush=True)
    env.load_snapshots(paths)

    # capture slots: seed_i x stage(1..4) -> fixed slot id; pre-seed each
    # slot with the seed's t0 state so slot ids exist before first capture
    def cap_slot(si, stage):
        return nseeds + si * (N_STAGES - 1) + (stage - 1)

    env.assign([i % nseeds for i in range(N_ENVS)])
    env.reset()
    for si in range(nseeds):
        for stg in range(1, N_STAGES):
            env.capture(si, cap_slot(si, stg))   # lane si holds seed si's t0

    # static log oracle per seed, padded [S, Lmax, 3]
    logs = [snapshot_logs(p) for p in paths]
    lmax = max(len(x) for x in logs)
    log_pad = np.full((nseeds, lmax, 3), 1e9, dtype=np.float32)
    for i, x in enumerate(logs):
        log_pad[i, :len(x)] = x
    log_t = torch.as_tensor(log_pad, device=dev)
    print(f"log oracle: {[len(x) for x in logs]} logs/seed", flush=True)

    net = ChainPolicy().to(dev)
    if WARM:
        net.load_state_dict(torch.load(os.path.join(OUT, WARM),
                                       weights_only=True, map_location=dev))
        print(f"warm start from {WARM}", flush=True)
    opt = torch.optim.Adam(net.parameters(), lr=LR)

    curr = StageCurriculum(nseeds, rng)
    lane_seed = np.array([i % nseeds for i in range(N_ENVS)])
    lane_snap = np.array(lane_seed)                   # snapshot slot per lane
    lane_stage_start = np.zeros(N_ENVS, dtype=np.int64)
    lane_stage = np.zeros(N_ENVS, dtype=np.int64)     # max stage this episode
    env.assign(list(lane_snap))
    env.reset()

    # rolling per-lane state
    stack_u8 = torch.zeros((N_ENVS, NCH, CAM_H, CAM_W), dtype=torch.uint8,
                           device=dev)
    scal_cur = torch.zeros((N_ENVS, NSCAL), dtype=torch.float32, device=dev)
    burnin = torch.ones(N_ENVS, dtype=torch.bool, device=dev)
    ep_dec = torch.randint(0, EP_DEC, (N_ENVS,), dtype=torch.int32,
                           device=dev,
                           generator=torch.Generator(device=dev)
                           .manual_seed(1))
    ep_ret = torch.zeros(N_ENVS, dtype=torch.float32, device=dev)
    lane_seed_t = torch.as_tensor(lane_seed, device=dev)

    # reward bookkeeping (per lane, reset on episode start)
    rew = ChainReward(N_ENVS, dev, REWARD_SPEC)

    # chunk buffers
    obs_b = torch.zeros((T_CHUNK, N_ENVS, NCH, CAM_H, CAM_W),
                        dtype=torch.uint8, device=dev)
    scal_b = torch.zeros((T_CHUNK, N_ENVS, NSCAL), device=dev)
    act_b = torch.zeros((T_CHUNK, N_ENVS, NHEAD), dtype=torch.int64,
                        device=dev)
    logp_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    val_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    rew_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    term_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    cut_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    valid_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)

    # curriculum capture bookkeeping
    cap_last = np.full((nseeds, N_STAGES), -10**9, dtype=np.int64)
    trail_t0 = deque(maxlen=TRAIL)         # full-chain success from t0
    mile_hist = np.zeros(N_STAGES + 1, dtype=np.int64)  # episodes by reached
    n_eps = 0
    curve = []
    ticks = 0
    next_ckpt = CKPT_TICKS
    chunk = 0
    t_start = time.perf_counter()
    stop_reason = None
    best_t0 = {"v": -1.0, "ticks": 0}

    def save_all():
        v = (sum(trail_t0) / len(trail_t0)) if trail_t0 else 0.0
        if len(trail_t0) >= 50 and v > best_t0["v"]:
            best_t0["v"], best_t0["ticks"] = v, ticks
            torch.save({k: t.cpu() for k, t in net.state_dict().items()},
                       os.path.join(OUT, f"{OUT_NAME}.pt"))
        torch.save({k: t.cpu() for k, t in net.state_dict().items()},
                   os.path.join(OUT, f"{OUT_NAME}_last.pt"))
        np.save(os.path.join(OUT, f"{CURVE_NAME}.npy"),
                np.array(curve, dtype=np.float64))
        REWARD_SPEC.dump(os.path.join(OUT, f"{OUT_NAME}_reward.json"))

    def save_png():
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            return
        c = np.array(curve)
        if len(c) < 2:
            return
        fig, ax = plt.subplots(figsize=(9, 5))
        for col, lab in ((2, "t0 full chain"), (3, "mean stage succ")):
            ax.plot(c[:, 0] / 1e6, c[:, col], label=lab)
        ax.set_xlabel("env ticks (M)")
        ax.set_ylabel("success")
        ax.set_ylim(-0.02, 1.02)
        ax.legend()
        ax.set_title("PPO spawn-to-torch chain (cuenv)")
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, f"{CURVE_NAME}.png"), dpi=140)
        plt.close(fig)

    print(f"N={N_ENVS} lanes, {nseeds} seeds {TRAIN_SEEDS}, "
          f"chunk {T_CHUNK} decisions, EP_DEC {EP_DEC}, device cuda:{DEVICE}",
          flush=True)
    print(f"reward spec: {REWARD_SPEC}", flush=True)

    noop9 = torch.zeros(NHEAD, dtype=torch.int64, device=dev)
    noop9[0] = noop9[1] = 1                  # dyaw 0, dpitch 0
    noop9[2] = 1                             # fwd 0

    while True:
        for t in range(T_CHUNK):
            with torch.no_grad():
                logits, vals = net(obs_float(stack_u8), scal_cur)
                dists = [torch.distributions.Categorical(logits=lg)
                         for lg in logits]
                acts = torch.stack([d.sample() for d in dists], dim=1)
                acts[burnin] = noop9
                logp = sum(d.log_prob(acts[:, h])
                           for h, d in enumerate(dists))

            obs_b[t] = stack_u8
            scal_b[t] = scal_cur
            act_b[t] = acts
            logp_b[t] = logp
            val_b[t] = vals
            valid_b[t] = ~burnin

            cam, depth, edge, scal, _env_rew, done, pose = env.step(
                acts_to_rows(acts, dev), repeat=REPEAT)
            status = env.status

            # ---- reward (reward_chain.ChainReward; see reward_chain.py) ----
            r = rew.step(status, cam, acts, pose, scal, done, lane_seed_t,
                         log_t)

            term = done > 0
            success = done == 1
            ep_dec += (~burnin).int()
            trunc = (~term) & (ep_dec >= EP_DEC)
            ended = term | trunc
            rew_b[t] = r
            term_b[t] = term
            cut_b[t] = ended
            ep_ret += r

            frame = build_frame(cam, depth, edge)
            stack_u8[:, :-NPLANES] = stack_u8[:, NPLANES:].clone()
            stack_u8[:, -NPLANES:] = frame
            if burnin.any():
                bi = burnin
                stack_u8[bi] = frame[bi].repeat(1, STACK, 1, 1)
            tfrac = ep_dec.float() / EP_DEC
            scal_cur = build_scal(scal, status, pose, tfrac)
            burnin = torch.zeros_like(burnin)

            # ---- milestone captures (policy-generated start states) ----
            stg_now = stage_of_best(
                rew.best, getattr(rew, "best_iron", None))
            live = ~term
            for stg in range(1, N_STAGES):
                cand = (stg_now == stg) & live & \
                    torch.as_tensor(lane_stage < stg, device=dev)
                if stg == N_STAGES - 1:      # torch needs a stick in hand
                    cand &= status[:, IX_STICK] >= 1
                if not cand.any():
                    continue
                lanes = cand.nonzero(as_tuple=True)[0].cpu().numpy()
                for lane in lanes[:8]:
                    si = int(lane_seed[lane])
                    if chunk - cap_last[si, stg] < CAP_REFRESH:
                        continue
                    env.capture(int(lane), cap_slot(si, stg))
                    cap_last[si, stg] = chunk
                    curr.avail[si, stg] = True
                # only capture-once per (seed,stage) per decision: cheap
                # enough, and lane_stage bump stops repeat triggers
                lane_stage[lanes] = np.maximum(lane_stage[lanes], stg)

            if ended.any():
                idx = ended.nonzero(as_tuple=True)[0]
                idx_h = idx.cpu().numpy()
                stg_h = stg_now[idx].cpu().numpy()
                suc_h = success[idx].cpu().numpy()
                for lane, reach, suc in zip(idx_h, stg_h, suc_h):
                    si = int(lane_seed[lane])
                    st0 = int(lane_stage_start[lane])
                    reached = N_STAGES if suc else int(reach)
                    mile_hist[min(reached, N_STAGES)] += 1
                    # per-(seed,startstage): did it reach the NEXT milestone
                    nxt = st0 + 1
                    ok = bool(suc) if st0 >= N_STAGES - 1 else reached >= nxt
                    curr.record(si, st0, ok)
                    if st0 == 0:
                        trail_t0.append(float(suc))
                n_eps += len(idx_h)

                seeds_n, stages_n = curr.sample(len(idx_h))
                lane_seed[idx_h] = seeds_n
                lane_stage_start[idx_h] = stages_n
                lane_stage[idx_h] = stages_n
                slot = np.where(
                    stages_n == 0, seeds_n,
                    nseeds + seeds_n * (N_STAGES - 1) + (stages_n - 1))
                lane_snap[idx_h] = slot
                env.assign(list(lane_snap))
                mask = np.zeros(N_ENVS, dtype=np.uint8)
                mask[idx_h] = 1
                env.reset(mask)
                lane_seed_t = torch.as_tensor(lane_seed, device=dev)
                ep_dec[idx] = 0
                ep_ret[idx] = 0.0
                burnin[idx] = True
                rew.reset(idx)

            ticks += N_ENVS * REPEAT

        # ---- GAE ----
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

        # ---- PPO ----
        sel = valid_b.flatten().nonzero(as_tuple=True)[0]
        n_tr = sel.numel()
        ent_sum = kl_sum = clip_sum = 0.0
        upd_n = 0
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
            for _ in range(EPOCHS):
                perm = sel[torch.randperm(n_tr, device=dev)]
                order = torch.searchsorted(sel, perm)
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
                    with torch.no_grad():
                        # collapse telemetry: policy entropy, approx KL
                        # (E[r-1-log r]), clip fraction; tensor sums, no
                        # host sync until the print
                        ent_sum = ent_sum + entr.mean().detach()
                        kl_sum = kl_sum + \
                            (ratio - 1.0 - torch.log(ratio)).mean().detach()
                        clip_sum = clip_sum + \
                            ((ratio - 1.0).abs() > CLIP).float().mean()
                        upd_n += 1

        chunk += 1
        wall = time.perf_counter() - t_start
        t0succ = (sum(trail_t0) / len(trail_t0)) if trail_t0 else float("nan")
        stage_means = [np.mean([curr.succ(si, s) for si in range(nseeds)
                                if curr.avail[si, s]] or [0.0])
                       for s in range(N_STAGES)]
        curve.append((ticks, wall, t0succ, float(np.mean(stage_means)),
                      *stage_means))
        if chunk % 5 == 0 or chunk == 1:
            mh = "/".join(str(int(x)) for x in mile_hist)
            # live best-so-far inventory rates (fraction of lanes that ever
            # held each milestone this episode) — abuse runs watch spick climb
            b = rew.best.float()
            live_rates = (
                f"log3={(b[:, IX_LOG] >= 3).float().mean().item():.2f} "
                f"wpick={(b[:, IX_WPICK] > 0).float().mean().item():.2f} "
                f"cob3={(b[:, IX_COBBLE] >= 3).float().mean().item():.2f} "
                f"spick={(b[:, IX_SPICK] > 0).float().mean().item():.2f} "
                f"coal={(b[:, IX_COAL] > 0).float().mean().item():.2f} "
                f"torch={(b[:, IX_TORCH] > 0).float().mean().item():.2f}")
            if IRON_CHAIN and getattr(rew, "best_iron", None) is not None:
                bi = rew.best_iron.float()
                live_rates += (
                    f" furn={(bi[:, 0] > 0).float().mean().item():.2f}"
                    f" ore={(bi[:, 1] > 0).float().mean().item():.2f}"
                    f" ing={(bi[:, 2] > 0).float().mean().item():.2f}"
                    f" ipick={(bi[:, 3] > 0).float().mean().item():.2f}")
            print(f"chunk {chunk:4d}  ticks {ticks/1e6:7.2f}M  wall "
                  f"{wall:7.1f}s  {ticks/max(wall,1e-9)/1e6:.3f}M t/s  "
                  f"t0 {t0succ:.2f} (n {len(trail_t0)})  stage-succ "
                  + " ".join(f"{m:.2f}" for m in stage_means)
                  + f"  eps {n_eps}  reached {mh}  live {live_rates}"
                  + (f"  ent {float(ent_sum)/upd_n:.3f} "
                     f"kl {float(kl_sum)/upd_n:.4f} "
                     f"clip {float(clip_sum)/upd_n:.2f}" if upd_n else "")
                  + f"  memg {torch.cuda.memory_allocated()/1e9:.2f}",
                  flush=True)
        if chunk % 25 == 0:
            print(f"  cells (per seed {TRAIN_SEEDS}, stages "
                  f"{MILE_NAMES}): {curr.matrix()}", flush=True)
        if ticks >= next_ckpt:
            save_all()
            next_ckpt = (ticks // CKPT_TICKS + 1) * CKPT_TICKS
        if chunk % 20 == 0:
            save_png()

        if ticks >= MAX_TICKS:
            stop_reason = f"tick budget ({ticks/1e6:.1f}M)"
        elif wall >= MAX_WALL:
            stop_reason = f"wall budget ({wall:.0f}s)"
        if stop_reason:
            break

    save_all()
    save_png()
    print(f"stop: {stop_reason}", flush=True)
    print(f"episodes {n_eps}; reached-milestone histogram "
          f"{list(mile_hist)}", flush=True)
    print(f"trailing t0 full-chain: {t0succ:.3f}  best {best_t0['v']:.3f} "
          f"at {best_t0['ticks']/1e6:.1f}M", flush=True)
    env.close()


if __name__ == "__main__":
    main()
