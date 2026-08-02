"""JVM chain demo: the blaze-trained spawn-to-torch policy driving the REAL
Java Minecraft 1.11.2 game over the NetheriteMod bridge, zero scripted actions.

Feature pipeline mirrors blaze/rl/eval_chain_rl.py exactly, computed from
the bridge's protocol-v2 obs (semantic camera cam/depth/edge, coal list,
inv_counts/held/container, pose). Action decode = ppo_chain_cu acts_to_rows
mapped onto the bridge action keys (dyaw/dpitch float deltas, craft 0..5,
interact, hotbar), REPEAT=4 game ticks per decision, camera only on the last
repeat tick. Sampled policy (never greedy), best-of-TRIES fresh cold spawns.

Prereq: the Run B headless client (start_vnc_client.sh / mc_cli.py --vnc)
with the rebuilt NetheriteMod, nothing else on port 25575.

Run (anvil):
  cd ~/dev/minecraft/mc-1.11.2-env && uv run --no-project --with torch,numpy \
      python java/qrl_chain_demo.py [seeds...]
Env: TRIES (default 5), EP_TICKS (default 6000), FRAME_EVERY (default 1
decision; 0 = no frames), FRAMES_ROOT (default /tmp/qrl_chain_demo).
"""
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))            # java/
ROOT = os.path.dirname(HERE)
RL = os.path.join(ROOT, "blaze", "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)
sys.path.insert(0, os.path.join(RL, "blaze"))

from ppo_chain_cu import (
    FWD,
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
from qrl_client import NetheriteEnv

OUT = os.path.join(RL, "out")
EYE = 1.62
EP_TICKS = int(os.environ.get("EP_TICKS", "6000"))
TRIES = int(os.environ.get("TRIES", "5"))
FRAME_EVERY = int(os.environ.get("FRAME_EVERY", "1"))
FRAMES_ROOT = os.environ.get("FRAMES_ROOT", "/tmp/qrl_chain_demo")
RESULT_JSON = os.environ.get("RESULT_JSON")
ALL_SEEDS = [2, 3, 10, 11, 14, 16, 20, 27, 29, 32, 33, 44, 46]
HELD_OUT = {11, 33}
FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def fnv1a64_update(digest, action):
    """Mirror Recorder.applyAction's digest of Gson's compact action JSON."""
    encoded = json.dumps(action, separators=(",", ":")).encode()
    for byte in encoded:
        digest ^= byte
        digest = (digest * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return digest


def nearest_coal_scal(obs):
    """The 6 env scalars, exactly as eval_chain_rl/blaze compute them."""
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
    except (ConnectionError, TimeoutError) as exc:
        print(f"  warning: could not enable bridge overclock: {exc}", flush=True)
    ep_dec = EP_TICKS // REPEAT
    if frames_dir:
        os.makedirs(frames_dir, exist_ok=True)
    obs = env._cmd({"cmd": "obs", "action": {"cam": 1}})
    if "world_seed" not in obs:
        raise RuntimeError("NetheriteMod bridge did not report the live world seed")
    if int(obs["world_seed"]) != seed:
        raise RuntimeError(f"fresh reset requested seed {seed}, bridge reports "
                           f"world_seed={obs.get('world_seed')}")
    if "policy_action_seq" not in obs or "policy_action_fnv64" not in obs:
        raise RuntimeError("NetheriteMod bridge lacks policy action acknowledgements; "
                           "rebuild the Java client from this commit")
    action_seq_start = int(obs["policy_action_seq"])
    if action_seq_start != 0:
        raise RuntimeError(f"fresh seed {seed} started at policy action seq "
                           f"{action_seq_start}, expected 0")
    frame, scal, status = obs_tensors(obs, 0, ep_dec)
    stack = frame.repeat(1, STACK, 1, 1)
    best = status.clone()
    t0 = time.time()
    actions_sent = 0
    non_noop_steps = 0
    local_action_hash = hashlib.sha256()
    local_action_fnv = FNV64_OFFSET
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
            local_action_hash.update(
                (json.dumps(s, sort_keys=True, separators=(",", ":")) + "\n")
                .encode())
            local_action_fnv = fnv1a64_update(local_action_fnv, s)
            actions_sent += 1
            non_noop_steps += int(any(v for k, v in s.items() if k != "cam"))
            obs = env.step(s)
            ack = int(obs.get("policy_action_seq", -1))
            expected = action_seq_start + actions_sent
            if ack != expected:
                raise RuntimeError(f"policy action acknowledgement mismatch: "
                                   f"got {ack}, expected {expected}")
            remote_fnv = obs.get("policy_action_fnv64")
            expected_fnv = format(local_action_fnv, "x")
            if remote_fnv != expected_fnv:
                raise RuntimeError("policy action digest mismatch: "
                                   f"got {remote_fnv}, expected {expected_fnv}")
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
            return episode_result(seed, rng_seed, N_STAGES, best, obs,
                                  actions_sent, non_noop_steps,
                                  local_action_hash.hexdigest(),
                                  format(local_action_fnv, "x"))
        if obs.get("dead"):
            print("    died", flush=True)
            break
    return episode_result(seed, rng_seed,
                          int(stage_of_best(best[:, :9])[0]), best, obs,
                          actions_sent, non_noop_steps,
                          local_action_hash.hexdigest(),
                          format(local_action_fnv, "x"))


def episode_result(seed, rng_seed, reached, best, obs, actions_sent,
                   non_noop_steps, local_action_sha256, local_action_fnv64):
    inv = [int(v) for v in best[0, :9]]
    observed_torches = inv[IX_TORCH]
    success = observed_torches >= 1
    if success != (reached >= N_STAGES):
        raise RuntimeError("success/milestone mismatch in observed Java inventory")
    return {
        "seed": seed,
        "policy_rng_seed": rng_seed,
        "reached": reached,
        "success": success,
        "success_source": "live_java_obs.inv_counts[torch]",
        "observed_torches": observed_torches,
        "best_inv_counts": inv,
        "actions_sent": actions_sent,
        "non_noop_steps": non_noop_steps,
        "bridge_action_seq": int(obs["policy_action_seq"]),
        "bridge_action_fnv64": obs["policy_action_fnv64"],
        "local_action_fnv64": local_action_fnv64,
        "local_action_sha256": local_action_sha256,
        "world_seed": int(obs["world_seed"]),
        "save_folder": obs.get("save_folder"),
    }


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_provenance():
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    clean = subprocess.run(
        ["git", "diff-index", "--quiet", "HEAD", "--"], cwd=ROOT,
        check=False).returncode == 0
    return commit, clean


def main():
    seeds = [int(s) for s in sys.argv[1:]] or ALL_SEEDS
    net = ChainPolicy()
    net_file = os.environ.get("CHAIN_NET", "chain_net_cu.pt")
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location="cpu"))
    net.eval()
    print(f"net {net_file}, sampled, {TRIES} tries x {EP_TICKS} ticks, "
          f"seeds {seeds}", flush=True)

    env = NetheriteEnv()
    results = {}
    attempts = []
    best_run = (-1, None)   # (milestone, frames_dir)
    for seed in seeds:
        reach_best = 0
        for att in range(TRIES):
            fdir = os.path.join(FRAMES_ROOT, f"s{seed}_a{att}")
            shutil.rmtree(fdir, ignore_errors=True)
            print(f"seed {seed} attempt {att}", flush=True)
            result = run_episode(
                env, seed, net, seed * 100 + att,
                frames_dir=fdir if FRAME_EVERY > 0 else None)
            result["attempt"] = att
            attempts.append(result)
            reached = result["reached"]
            inv = result["best_inv_counts"]
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
    if RESULT_JSON:
        checkpoint = os.path.join(OUT, net_file)
        commit, tracked_clean = git_provenance()
        artifact = {
            "schema": "netherite.sim2real.v1",
            "environment": "java-1.11.2",
            "commit": commit,
            "tracked_clean": tracked_clean,
            "checkpoint": os.path.relpath(checkpoint, ROOT),
            "checkpoint_sha256": sha256_file(checkpoint),
            "seeds": seeds,
            "tries": TRIES,
            "ep_ticks": EP_TICKS,
            "repeat": REPEAT,
            "sampling": "categorical",
            "rng_protocol": "torch.manual_seed(seed*100+attempt)",
            "success_source": "live_java_obs.inv_counts[torch]",
            "attempts": attempts,
            "per_seed_reached": {str(k): v for k, v in results.items()},
        }
        parent = os.path.dirname(os.path.abspath(RESULT_JSON))
        os.makedirs(parent, exist_ok=True)
        with open(RESULT_JSON, "w") as f:
            json.dump(artifact, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"wrote {RESULT_JSON}", flush=True)


if __name__ == "__main__":
    main()
