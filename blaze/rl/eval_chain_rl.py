"""Evaluate the learned FULL-CHAIN policy on the REAL magma env, cold
spawn, ZERO scripted actions: the net produces every tick's action (movement,
look, attack, use, hotbar, craft, interact) from spawn to torches.

Feature pipeline mirrors blaze/env/ppo_chain_cu.py exactly, computed from the
real env's BOLR obs (cam/depth/edge planes, coal scalars via the rl_mode coal
list, inv_counts/hotbar/container, pose). Sampled policy (never greedy),
best-of-TRIES per seed.

Run (anvil):
  cd magma && CHAIN_NET=chain_net_cu.pt uv run --no-project \
      --with numpy,torch python rl/eval_chain_rl.py [seeds...]
Env: TRIES (default 5), EP_TICKS (default 6000), SAVE_ACTIONS=1 to dump the
first fully successful episode's action stream for make_chain_video.py.
"""
import hashlib
import json
import math
import os
import subprocess
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "blaze"))

from look_at_tree import EYE, MagmaEnv, wrap180
from ppo_chain_cu import (  # noqa
    FWD,
    HEADS,
    IX_TORCH,
    MILE_NAMES,
    N_STAGES,
    NPLANES,
    PITCHES,
    REPEAT,
    STACK,
    YAWS,
    ChainPolicy,
    build_frame,
    build_scal,
    obs_float,
    stage_of_best,
)

OUT = os.path.join(HERE, "out")
EP_TICKS = int(os.environ.get("EP_TICKS", "6000"))
TRIES = int(os.environ.get("TRIES", "5"))
ALL_SEEDS = [2, 3, 10, 11, 14, 16, 20, 27, 29, 32, 33, 44, 46]
HELD_OUT = {11, 33}
RESULT_JSON = os.environ.get("RESULT_JSON")


def nearest_coal_scal(obs):
    """The 6 env scalars, exactly as blaze_decision_finalize computes them."""
    best = None
    ex, ey, ez = obs["x"], obs["y"] + EYE, obs["z"]
    for c in obs["coal"]:
        if c == [0, 0, 0]:
            break
        dx, dy, dz = c[0] + 0.5 - ex, c[1] + 0.5 - ey, c[2] + 0.5 - ez
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if best is None or dist < best[2]:
            ry = wrap180(math.degrees(math.atan2(-dx, dz)) - obs["yaw"])
            rp = math.degrees(-math.asin(dy / max(dist, 1e-9))) \
                - obs["pitch"]
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
    hb_sel = obs["hotbar_sel"]
    held = int(obs["hotbar_ids"][hb_sel]) \
        if int(obs["hotbar_counts"][hb_sel]) > 0 else 0
    status = torch.zeros((1, 12), dtype=torch.int32)
    status[0, :9] = torch.as_tensor(
        np.asarray(obs["inv_counts"], dtype=np.int32))
    status[0, 9] = hb_sel
    status[0, 10] = held
    status[0, 11] = obs["container"]
    scal6 = torch.as_tensor(nearest_coal_scal(obs)).unsqueeze(0)
    pose = torch.tensor([[obs["x"], obs["y"], obs["z"]]], dtype=torch.float32)
    pose = torch.cat([pose, torch.zeros(1, 2)], dim=1)
    tfrac = torch.tensor([dec / ep_dec], dtype=torch.float32)
    return frame, build_scal(scal6, status, pose, tfrac), status


def run_episode(seed, net, rng_seed):
    torch.manual_seed(rng_seed)
    env = MagmaEnv(seed)
    ep_dec = EP_TICKS // REPEAT
    try:
        frame, scal, status = obs_tensors(env.obs, 0, ep_dec)
        stack = frame.repeat(1, STACK, 1, 1)
        best = status.clone()
        for dec in range(ep_dec):
            with torch.no_grad():
                logits, _ = net(obs_float(stack), scal)
            a = [int(torch.distributions.Categorical(logits=lg).sample())
                 for lg in logits]
            act = {"dyaw": YAWS[a[0]], "dpitch": PITCHES[a[1]],
                   "forward": FWD[a[2]], "jump": a[3], "attack": a[4],
                   "use": a[5]}
            if a[6] > 0:
                act["craft"] = a[6] - 1
            if a[7]:
                act["interact"] = 1
            if a[8] > 0:
                act["hotbar"] = a[8] - 1
            for rep in range(REPEAT):
                s = dict(act)
                if rep > 0:
                    s["dyaw"] = 0.0
                    s["dpitch"] = 0.0
                    s.pop("craft", None)     # pre-tick primitives fire once
                    s.pop("interact", None)
                    s.pop("hotbar", None)
                    if rep < REPEAT - 1:
                        s["cam"] = 0
                env.send(s)
                env.recv()
            frame, scal, status = obs_tensors(env.obs, dec + 1, ep_dec)
            stack = torch.cat([stack[:, NPLANES:], frame], dim=1)
            best = torch.maximum(best, status)
            if int(status[0, IX_TORCH]) >= 1:
                return episode_result(seed, rng_seed, N_STAGES, best,
                                      list(env.actions))
            if env.obs["dead"]:
                break
        return episode_result(seed, rng_seed,
                              int(stage_of_best(best[:, :9])[0]), best,
                              list(env.actions))
    finally:
        env.close()


def episode_result(seed, rng_seed, reached, best, actions):
    inv = [int(v) for v in best[0, :9]]
    observed_torches = inv[IX_TORCH]
    success = observed_torches >= 1
    if success != (reached >= N_STAGES):
        raise RuntimeError("success/milestone mismatch in observed magma inventory")
    h = hashlib.sha256()
    for action in actions:
        h.update((json.dumps(action, sort_keys=True, separators=(",", ":"))
                  + "\n").encode())
    return {
        "seed": seed,
        "policy_rng_seed": rng_seed,
        "reached": reached,
        "success": success,
        "success_source": "magma_bolr.inv_counts[torch]",
        "observed_torches": observed_torches,
        "best_inv_counts": inv,
        "actions_sent": len(actions),
        "non_noop_steps": sum(
            int(any(v for k, v in action.items() if k != "cam"))
            for action in actions),
        "local_action_sha256": h.hexdigest(),
        "world_seed": seed,
        "actions": actions,
    }


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_provenance():
    root = os.path.dirname(os.path.dirname(HERE))
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    clean = subprocess.run(
        ["git", "diff-index", "--quiet", "HEAD", "--"], cwd=root,
        check=False).returncode == 0
    return commit, clean


def main():
    seeds = [int(s) for s in sys.argv[1:]] or ALL_SEEDS
    net = ChainPolicy()
    net_file = os.environ.get("CHAIN_NET", "chain_net_cu.pt")
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location="cpu"))
    net.eval()
    print(f"net {net_file}, sampled, {TRIES} tries x {EP_TICKS} ticks",
          flush=True)

    def save_tape(seed, actions):
        p = os.path.join(OUT, f"chain_actions_s{seed}_learned.json")
        with open(p, "w") as f:
            # drop "cam":0 hints: state-neutral, but the video replay
            # needs the cam field in every tick's obs
            json.dump([{k: v for k, v in a.items() if k != "cam"}
                       for a in actions], f)
        print(f"  saved learned stream -> {p}", flush=True)

    results = {}
    attempts = []
    saved = False
    best_tape = (0, None, None)          # (milestone, seed, actions)
    for seed in seeds:
        reach_best, ok = 0, False
        for att in range(TRIES):
            result = run_episode(seed, net, seed * 100 + att)
            result["attempt"] = att
            actions = result.pop("actions")
            attempts.append(result)
            reached = result["reached"]
            reach_best = max(reach_best, reached)
            if actions and reached > best_tape[0]:
                best_tape = (reached, seed, actions)
            if reached >= N_STAGES:
                ok = True
                if actions and os.environ.get("SAVE_ACTIONS") and not saved:
                    save_tape(seed, actions)
                    saved = True
                break
        tag = " HELD-OUT" if seed in HELD_OUT else ""
        name = "TORCHES" if ok else MILE_NAMES[min(reach_best,
                                                   N_STAGES - 1)]
        print(f"seed {seed:3d}{tag}: best milestone = {name} "
              f"({reach_best}/{N_STAGES})", flush=True)
        results[seed] = reach_best

    if not saved and os.environ.get("SAVE_ACTIONS") and best_tape[1]:
        print(f"no full-chain success; saving deepest run (milestone "
              f"{best_tape[0]}, seed {best_tape[1]})", flush=True)
        save_tape(best_tape[1], best_tape[2])

    n_ok = sum(1 for v in results.values() if v >= N_STAGES)
    print(f"\nfull chain (torches): {n_ok}/{len(results)} seeds; milestone "
          "histogram: " + ", ".join(
          f"{MILE_NAMES[min(v, N_STAGES - 1)] if v < N_STAGES else 'TORCH'}"
          f":{sum(1 for x in results.values() if x == v)}"
          for v in sorted(set(results.values()))), flush=True)
    if RESULT_JSON:
        checkpoint = os.path.join(OUT, net_file)
        commit, tracked_clean = git_provenance()
        artifact = {
            "schema": "netherite.sim2real.v1",
            "environment": "magma",
            "commit": commit,
            "tracked_clean": tracked_clean,
            "checkpoint": os.path.relpath(
                checkpoint, os.path.dirname(os.path.dirname(HERE))),
            "checkpoint_sha256": sha256_file(checkpoint),
            "seeds": seeds,
            "tries": TRIES,
            "ep_ticks": EP_TICKS,
            "repeat": REPEAT,
            "sampling": "categorical",
            "rng_protocol": "torch.manual_seed(seed*100+attempt)",
            "success_source": "magma_bolr.inv_counts[torch]",
            "attempts": attempts,
            "per_seed_reached": {str(k): v for k, v in results.items()},
        }
        parent = os.path.dirname(os.path.abspath(RESULT_JSON))
        os.makedirs(parent, exist_ok=True)
        with open(RESULT_JSON, "w") as f:
            json.dump(artifact, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"wrote {RESULT_JSON}", flush=True)
    return 0 if n_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
