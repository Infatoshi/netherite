"""JVM chain demo: the cuenv-trained spawn-to-torch policy driving the REAL
Java Minecraft 1.11.2 game over the qrl bridge, zero scripted actions.

Feature pipeline mirrors c/craster/rl/eval_chain_rl.py exactly, computed from
the bridge's protocol-v2 obs (semantic camera cam/depth/edge, coal list,
inv_counts/held/container, pose). Action decode = ppo_chain_cu acts_to_rows
mapped onto the bridge action keys (dyaw/dpitch float deltas, craft 0..5,
interact, hotbar), REPEAT=4 game ticks per decision, camera only on the last
repeat tick. Sampled policy (never greedy), best-of-TRIES fresh cold spawns.

Prereq: the Run B headless client (start_vnc_client.sh / mc_cli.py --vnc)
with the rebuilt qrl mod, nothing else on port 25575.

Run (anvil):
  cd ~/dev/minecraft/mc-1.11.2-env && uv run --no-project --with torch,numpy \
      python java/qrl_chain_demo.py [seeds...]
Env: TRIES (default 5), EP_TICKS (default 6000), FRAME_EVERY (default 1
decision; 0 = no frames), FRAMES_ROOT (default /tmp/qrl_chain_demo).
"""
import json
import math
import os
import shutil
import sys
import time

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))            # java/
ROOT = os.path.dirname(HERE)
RL = os.path.join(ROOT, "c", "craster", "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)
sys.path.insert(0, os.path.join(RL, "cuenv"))

from qrl_client import QRLEnv                                 # noqa: E402
from ppo_chain_cu import (ChainPolicy, build_frame, build_scal,  # noqa: E402
                          obs_float, stage_of_best, NPLANES, STACK,
                          REPEAT, YAWS, PITCHES, FWD, IX_TORCH,
                          MILE_NAMES, N_STAGES)

OUT = os.path.join(RL, "out")
EYE = 1.62
EP_TICKS = int(os.environ.get("EP_TICKS", 6000))
TRIES = int(os.environ.get("TRIES", 5))
FRAME_EVERY = int(os.environ.get("FRAME_EVERY", 1))
FRAMES_ROOT = os.environ.get("FRAMES_ROOT", "/tmp/qrl_chain_demo")


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def nearest_coal_scal(obs):
    """The 6 env scalars, exactly as eval_chain_rl/cuenv compute them."""
    best = None
    ex, ey, ez = obs["x"], obs["y"] + EYE, obs["z"]
    for c in obs.get("coal", []):
        if c == [0, 0, 0]:
            break
        dx, dy, dz = c[0] + 0.5 - ex, c[1] + 0.5 - ey, c[2] + 0.5 - ez
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if best is None or dist < best[2]:
            ry = wrap180(math.degrees(math.atan2(-dx, dz)) - obs["yaw"])
            rp = math.degrees(-math.asin(dy / max(dist, 1e-9))) - obs["pitch"]
            best = (ry, rp, dist)
    pr = math.radians(obs["pitch"])
    if best is None:
        return np.array([0, 0, 0, 1, math.sin(pr), math.cos(pr)],
                        dtype=np.float32)
    ry, rp, dist = best
    return np.array([math.sin(math.radians(ry)), math.cos(math.radians(ry)),
                     rp / 90.0, min(dist, 24.0) / 24.0,
                     math.sin(pr), math.cos(pr)], dtype=np.float32)


def obs_tensors(obs, dec, ep_dec):
    cam = torch.as_tensor(np.asarray(obs["cam"], dtype=np.int16)
                          .reshape(1, 36, 64))
    dep = torch.as_tensor(np.asarray(obs["depth"], dtype=np.uint8)
                          .reshape(1, 36, 64))
    edg = torch.as_tensor(np.asarray(obs["edge"], dtype=np.uint8)
                          .reshape(1, 36, 64))
    frame = build_frame(cam, dep, edg)
    held = int(obs["held_id"]) if int(obs["held_count"]) > 0 else 0
    status = torch.zeros((1, 12), dtype=torch.int32)
    status[0, :9] = torch.as_tensor(
        np.asarray(obs["inv_counts"], dtype=np.int32))
    status[0, 9] = int(obs["hotbar_sel"])
    status[0, 10] = held
    status[0, 11] = 1 if int(obs["container"]) > 0 else 0
    scal6 = torch.as_tensor(nearest_coal_scal(obs)).unsqueeze(0)
    pose = torch.tensor([[obs["x"], obs["y"], obs["z"]]], dtype=torch.float32)
    pose = torch.cat([pose, torch.zeros(1, 2)], dim=1)
    tfrac = torch.tensor([dec / ep_dec], dtype=torch.float32)
    return frame, build_scal(scal6, status, pose, tfrac), status


def run_episode(env, seed, net, rng_seed, frames_dir=None):
    torch.manual_seed(rng_seed)
    print(f"  fresh reset seed {seed} ...", flush=True)
    o = env.reset({"seed": seed, "fresh": True}, timeout=600)
    if not o.get("ok"):
        raise RuntimeError(f"reset failed: {o}")
    try:
        env.overclock(1)   # uncap the server tick; steps stay tick-synced
    except Exception:
        pass
    ep_dec = EP_TICKS // REPEAT
    if frames_dir:
        os.makedirs(frames_dir, exist_ok=True)
    obs = env._cmd({"cmd": "obs", "action": {"cam": 1}})
    frame, scal, status = obs_tensors(obs, 0, ep_dec)
    stack = frame.repeat(1, STACK, 1, 1)
    best = status.clone()
    t0 = time.time()
    for dec in range(ep_dec):
        with torch.no_grad():
            logits, _ = net(obs_float(stack), scal)
        a = [int(torch.distributions.Categorical(logits=lg).sample())
             for lg in logits]
        fwd = FWD[a[2]]
        keys = {"forward": 1 if fwd > 0 else 0, "back": 1 if fwd < 0 else 0,
                "jump": a[3], "attack": a[4], "use": a[5]}
        act0 = dict(keys)
        if YAWS[a[0]]:
            act0["dyaw"] = YAWS[a[0]]
        if PITCHES[a[1]]:
            act0["dpitch"] = PITCHES[a[1]]
        if a[6] > 0:
            act0["craft"] = a[6] - 1
        if a[7]:
            act0["interact"] = 1
        if a[8] > 0:
            act0["hotbar"] = a[8] - 1
        for rep in range(REPEAT):
            s = act0 if rep == 0 else dict(keys)
            if rep == REPEAT - 1:
                s = dict(s)
                s["cam"] = 1
            obs = env.step(s)
        if not obs.get("ok"):
            raise RuntimeError(f"step failed: {obs}")
        frame, scal, status = obs_tensors(obs, dec + 1, ep_dec)
        stack = torch.cat([stack[:, NPLANES:], frame], dim=1)
        best = torch.maximum(best, status)
        if frames_dir and FRAME_EVERY and dec % FRAME_EVERY == 0:
            env._cmd({"cmd": "frame", "action":
                      {"file": os.path.join(frames_dir, f"f{dec:05d}.png")}})
        if dec % 100 == 0:
            inv = [int(v) for v in best[0, :9]]
            print(f"    dec {dec:5d}  {(dec+1)*REPEAT/(time.time()-t0):5.1f} "
                  f"t/s  y {obs['y']:6.1f}  best inv {inv}  "
                  f"cont {obs['container']}", flush=True)
        if int(status[0, IX_TORCH]) >= 1:
            return N_STAGES, best
        if obs.get("dead"):
            print("    died", flush=True)
            break
    return int(stage_of_best(best[:, :9])[0]), best


def main():
    seeds = [int(s) for s in sys.argv[1:]] or [0]
    net = ChainPolicy()
    net_file = os.environ.get("CHAIN_NET", "chain_net_cu.pt")
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location="cpu"))
    net.eval()
    print(f"net {net_file}, sampled, {TRIES} tries x {EP_TICKS} ticks, "
          f"seeds {seeds}", flush=True)

    env = QRLEnv()
    results = {}
    best_run = (-1, None)   # (milestone, frames_dir)
    for seed in seeds:
        reach_best = 0
        for att in range(TRIES):
            fdir = os.path.join(FRAMES_ROOT, f"s{seed}_a{att}")
            shutil.rmtree(fdir, ignore_errors=True)
            print(f"seed {seed} attempt {att}", flush=True)
            reached, best = run_episode(env, seed, net, seed * 100 + att,
                                        frames_dir=fdir)
            inv = [int(v) for v in best[0, :9]]
            name = "TORCHES" if reached >= N_STAGES \
                else MILE_NAMES[min(reached, N_STAGES - 1)]
            print(f"  -> milestone {name} ({reached}/{N_STAGES})  "
                  f"best inv {inv}", flush=True)
            if reached > best_run[0]:
                best_run = (reached, fdir)
            reach_best = max(reach_best, reached)
            if reached >= N_STAGES:
                break
        results[seed] = reach_best
        name = "TORCHES" if reach_best >= N_STAGES \
            else MILE_NAMES[min(reach_best, N_STAGES - 1)]
        print(f"seed {seed}: best milestone = {name} "
              f"({reach_best}/{N_STAGES})", flush=True)
    env.close()

    print("\nJVM transfer results: " + ", ".join(
        f"s{s}:{MILE_NAMES[min(v, N_STAGES - 1)] if v < N_STAGES else 'TORCH'}"
        for s, v in results.items()), flush=True)
    print(f"deepest run frames: {best_run[1]} (milestone {best_run[0]})",
          flush=True)


if __name__ == "__main__":
    main()
