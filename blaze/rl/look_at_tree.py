"""Look-at-nearest-tree REINFORCE on N parallel magma --rl envs.

Env protocol (game/rl_mode.c): one JSON action line in -> one tick -> one JSON
obs line out. All obs fields are STATIC shapes: "blocks" 256 nearest non-air
[id,x,y,z] (zero pad), "logs" 64 nearest log blocks, "cam"/"depth" a fixed
64x36 semantic camera (block id + depth per pixel, DDA raycast from the eye,
70 deg FOV - occlusion-honest, the agent only sees what a camera would).

Task: rotate the camera (dyaw/dpitch in {-15,0,+15} deg, 9 discrete actions,
no movement) so the look vector points at the nearest log block. The POLICY
INPUT is the semantic camera only (8x6 grid of tree-pixel fractions); the
oracle "logs" list is used for the REWARD only (cos angle to nearest log,
-1 if none within obs range). When the tree is out of frame the retina is
empty and the policy must learn to scan. Pure numpy linear-softmax policy
gradient, advantage = one-step reward improvement.

Every action sent to an env is appended to a per-env stream and dumped as a
magma script (type "action" events) so any episode replays deterministically
with rendering for the mp4s (same seed, same tick order as rl mode).

Run (anvil):
  cd magma && uv run --no-project --with numpy,matplotlib python rl/look_at_tree.py
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MAGMA = os.path.join(os.path.dirname(os.path.dirname(HERE)), "magma")
BIN = os.path.join(MAGMA, "magma_game")
OUT = os.path.join(HERE, "out")

# seeds screened for >=10 log blocks visible from spawn (8 of seeds 0-15 see
# ZERO logs within the r=16 obs -> constant -1 reward, pure batch noise)
SEEDS = [0, 2, 3, 10, 11, 12, 14, 17, 19, 20, 21, 22, 29, 30, 35, 47]
N_ENVS = len(SEEDS)
N_EPISODES = int(os.environ.get("N_EPISODES", 400))
EP_LEN = 60          # rewarded steps per episode (3 s of game time)
LR = 5.0
LOG_ID = 17
TREE_IDS = (17, 18)  # log + leaves: the visible signature of a tree
EYE = 1.62

# retina: camera tree-fraction grid, 8x6 cells over the 64x36 id frame
CAM_W, CAM_H = 64, 36
GRID_X, GRID_Y = 8, 6
N_FEAT = GRID_X * GRID_Y + 3  # + no-tree flag, sin/cos pitch
ACTIONS = [(dy, dp) for dy in (-15.0, 0.0, 15.0) for dp in (-15.0, 0.0, 15.0)]


# --rl-bin record layout (game/rl_mode.c RlBinObs, packed little-endian):
# u32 magic 'BOLR' | i64 tick | f64 x,y,z | f32 yaw,pitch | i32 dead |
# i32 hotbar_ids[9] hotbar_counts[9] hotbar_sel container inv_counts[9] |
# i32 blocks[256][4] | i32 logs[64][3] | i32 coal[32][3] |
# u16 cam[2304] | u8 depth[2304] | u8 edge[2304]
BIN_MAGIC = b"BOLR"
BIN_HEAD = 4 + 8 + 24 + 8 + 4 + (9 + 9 + 1 + 1 + 9) * 4
BIN_SIZE = (BIN_HEAD + 256 * 4 * 4 + 64 * 3 * 4 + 32 * 3 * 4
            + 2304 * 2 + 2304 + 2304)
# inv_counts index -> item (game/rl_mode.c rl_inv_ids)
INV_IDS = (17, 5, 280, 4, 58, 270, 274, 263, 50)


class MagmaEnv:
    def __init__(self, seed, bin=True):
        self.seed = seed
        self.bin = bin
        self.proc = subprocess.Popen(
            [BIN, "--rl-bin" if bin else "--rl", "--render", "off",
             "--pace", "unlimited", "--seed", str(seed), "--mobs", "off"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            **({} if bin else {"text": True, "bufsize": 1}))
        self.actions = []          # every action ever sent, in tick order
        self.obs = self._read_obs()

    def _read_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.proc.stdout.read(n - len(buf))
            if not chunk:
                raise RuntimeError(f"env seed={self.seed} died")
            buf += chunk
        return buf

    def _read_obs_bin(self):
        # resync on magic (tolerates stray text before the first record)
        win = self._read_exact(4)
        while win != BIN_MAGIC:
            win = win[1:] + self._read_exact(1)
        rec = win + self._read_exact(BIN_SIZE - 4)
        import struct
        t, x, y, z, yaw, pitch, dead = struct.unpack_from("<qdddffi", rec, 4)
        o = 4 + 8 + 24 + 8 + 4
        hb = np.frombuffer(rec, "<i4", 29, o)
        o += 29 * 4
        blocks = np.frombuffer(rec, "<i4", 256 * 4, o).reshape(256, 4)
        o += 256 * 4 * 4
        logs = np.frombuffer(rec, "<i4", 64 * 3, o).reshape(64, 3)
        o += 64 * 3 * 4
        coal = np.frombuffer(rec, "<i4", 32 * 3, o).reshape(32, 3)
        o += 32 * 3 * 4
        # zero-copy views into the record; no astype/tolist - per-tick decode
        # cost holds the client GIL and was the vec-env scaling bottleneck.
        # logs/coal stay lists (small, and callers compare rows to [0,0,0],
        # which is ambiguous on np rows); blocks/cam/depth/edge stay np.
        cam = np.frombuffer(rec, "<u2", 2304, o)
        o += 2304 * 2
        dep = np.frombuffer(rec, "<u1", 2304, o)
        o += 2304
        edge = np.frombuffer(rec, "<u1", 2304, o)
        return {"t": t, "x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch,
                "dead": dead,
                "hotbar_ids": hb[0:9], "hotbar_counts": hb[9:18],
                "hotbar_sel": int(hb[18]), "container": int(hb[19]),
                "inv_counts": hb[20:29],
                "blocks": blocks,
                "logs": logs.tolist(), "coal": coal.tolist(),
                "cam": cam, "depth": dep, "edge": edge}

    def _read_obs(self):
        if self.bin:
            return self._read_obs_bin()
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError(f"env seed={self.seed} died")
            if line.startswith("{"):
                return json.loads(line)

    def send(self, act):
        self.actions.append(act)
        line = json.dumps(act) + "\n"
        self.proc.stdin.write(line.encode() if self.bin else line)
        self.proc.stdin.flush()

    def recv(self):
        self.obs = self._read_obs()
        return self.obs

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def log_dirs(obs):
    """(rel_yaw, rel_pitch, dist) per log block, relative to current look."""
    ex, ey, ez = obs["x"], obs["y"] + EYE, obs["z"]
    out = []
    for b in obs["logs"]:
        if b == [0, 0, 0]:
            break  # zero padding (no logs live at y=0)
        dx, dy, dz = b[0] + 0.5 - ex, b[1] + 0.5 - ey, b[2] + 0.5 - ez
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if dist < 1e-6:
            continue
        tyaw = math.degrees(math.atan2(-dx, dz))      # MC yaw convention
        tpitch = math.degrees(-math.asin(dy / dist))
        out.append((wrap180(tyaw - obs["yaw"]),
                    tpitch - obs["pitch"], dist))
    return out


def featurize(obs):
    """Camera-only retina: per-cell fraction of tree pixels (log or leaves)."""
    f = np.zeros(N_FEAT, dtype=np.float64)
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    tree = np.isin(cam, TREE_IDS).astype(np.float64)
    cw, ch = CAM_W // GRID_X, CAM_H // GRID_Y
    cells = tree.reshape(GRID_Y, ch, GRID_X, cw).mean(axis=(1, 3))
    if cells.sum() < 1e-9:
        f[-3] = 1.0
    else:
        f[:GRID_X * GRID_Y] = cells.reshape(-1)
    f[-2] = math.sin(math.radians(obs["pitch"]))
    f[-1] = math.cos(math.radians(obs["pitch"]))
    return f


def reward_exact(obs):
    """Unit-look-vector dot unit-eye->nearest-log; -1 when no log in the obs."""
    logs = log_dirs(obs)
    if not logs:
        return -1.0
    ry, rp, _ = min(logs, key=lambda t: t[2])
    p0 = math.radians(obs["pitch"]); y0 = math.radians(obs["yaw"])
    p1 = math.radians(obs["pitch"] + rp); y1 = math.radians(obs["yaw"] + ry)
    def vec(yaw, pitch):
        return (-math.sin(yaw) * math.cos(pitch),
                -math.sin(pitch),
                math.cos(yaw) * math.cos(pitch))
    a, b = vec(y0, p0), vec(y1, p1)
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(0)
    envs = [MagmaEnv(seed) for seed in SEEDS]
    W = np.zeros((N_FEAT, len(ACTIONS)))

    ep_mean = []          # mean total reward across envs, per episode
    ep_env0 = []          # env 0 total reward per episode (for sample picking)
    env0_marks = []       # (episode, start_tick, end_tick) for env 0

    for ep in range(N_EPISODES):
        # temperature anneal 1.0 -> 0.25: exploration early, a crisp argmax-
        # like policy late (the flat-tau run plateaued at mean ~+0.45 because
        # a 9-way softmax with modest logit gaps keeps sampling bad turns)
        tau = max(0.25, 1.0 - 0.75 * ep / max(N_EPISODES - 1, 1))
        # scramble look (recorded, not rewarded)
        for e in envs:
            dyaw = float(rng.uniform(-180, 180))
            dpitch = float(rng.uniform(-45, 45) - e.obs["pitch"])
            e.send({"dyaw": round(dyaw, 3), "dpitch": round(dpitch, 3)})
        for e in envs:
            e.recv()

        start_tick = int(envs[0].obs["t"])
        feats = [[] for _ in envs]
        acts = [[] for _ in envs]
        rews = [[] for _ in envs]
        prevs = [[] for _ in envs]   # reward BEFORE the action, for the delta

        for _ in range(EP_LEN):
            probs_all = []
            for i, e in enumerate(envs):
                f = featurize(e.obs)
                prevs[i].append(reward_exact(e.obs))
                z = (f @ W) / tau
                z -= z.max()
                p = np.exp(z); p /= p.sum()
                a = int(rng.choice(len(ACTIONS), p=p))
                feats[i].append(f); acts[i].append(a); probs_all.append(p)
                dy, dp = ACTIONS[a]
                e.send({"dyaw": dy, "dpitch": dp})
            for i, e in enumerate(envs):
                e.recv()
                rews[i].append(reward_exact(e.obs))

        # Policy gradient, advantage = one-step reward IMPROVEMENT (reward
        # after the action minus reward before it). With continuous camera
        # features there is no clean tabular state for a per-state baseline;
        # the delta is state-independent by construction: an action is good
        # iff it turned the camera closer to the tree than standing still.
        grad = np.zeros_like(W)
        for i in range(N_ENVS):
            for t in range(EP_LEN):
                f = feats[i][t]
                a = rews[i][t] - prevs[i][t]
                z = (f @ W) / tau
                z -= z.max()
                p = np.exp(z); p /= p.sum()
                dlogp = -p
                dlogp[acts[i][t]] += 1.0
                grad += np.outer(f, dlogp) * (a / tau)
        W += LR * grad / (N_ENVS * EP_LEN)

        totals = [sum(r) / EP_LEN for r in rews]
        ep_mean.append(float(np.mean(totals)))
        ep_env0.append(float(totals[0]))
        env0_marks.append((ep, start_tick, int(envs[0].obs["t"])))
        if ep % 10 == 0 or ep == N_EPISODES - 1:
            print(f"ep {ep:4d}  mean_align {ep_mean[-1]:+.3f}  "
                  f"env0 {totals[0]:+.3f}", flush=True)

    # persist: curve, env-0 action stream as a magma script, episode marks
    np.save(os.path.join(OUT, "ep_mean.npy"), np.array(ep_mean))
    np.save(os.path.join(OUT, "ep_env0.npy"), np.array(ep_env0))
    with open(os.path.join(OUT, "env0_script.jsonl"), "w") as f:
        for t, a in enumerate(envs[0].actions):
            ev = {"tick": t, "type": "action"}
            ev.update(a)
            f.write(json.dumps(ev) + "\n")
    with open(os.path.join(OUT, "env0_marks.json"), "w") as f:
        json.dump({"seed": envs[0].seed, "ep_len": EP_LEN,
                   "marks": env0_marks}, f)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.figure(figsize=(8, 4.5))
    plt.plot(ep_mean, label="mean of 16 envs")
    plt.plot(ep_env0, alpha=0.35, label="env 0 (video env)")
    plt.axhline(1.0, ls="--", c="gray", lw=0.8)
    plt.xlabel("episode"); plt.ylabel("mean per-step alignment (cos angle)")
    plt.title("REINFORCE: look at nearest tree (16 magma envs, 9 look actions)")
    plt.legend(); plt.tight_layout()
    plt.savefig(os.path.join(OUT, "reward_curve.png"), dpi=140)
    print("saved", os.path.join(OUT, "reward_curve.png"))

    for e in envs:
        e.close()


if __name__ == "__main__":
    main()
