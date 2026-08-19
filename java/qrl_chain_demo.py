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
Flags: --tries 5, --ep-ticks 6000, --frame-every 1 (0 = no frames),
--frames-root /tmp/qrl_chain_demo, --overclock-ms 50 (1 free-runs ~100x),
--result-json PATH, --envmatch/--no-envmatch, --chain-net chain_net_cu.pt.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))            # java/
ROOT = os.path.dirname(HERE)
RL = os.path.join(ROOT, "blaze", "rl")
OUT = os.path.join(RL, "out")
EYE = 1.62
# Module defaults (= historical unset-env); main() overrides from CLI.
EP_TICKS = 6000
TRIES = 5
# server tick length in ms (50 = vanilla realtime, 1 = uncapped free-run)
OVERCLOCK_MS = 50
FRAME_EVERY = 1
FRAMES_ROOT = "/tmp/qrl_chain_demo"
RESULT_JSON = None
# --- eval-side training-env match (--no-envmatch restores the vanilla world) ---
# The blaze training env simulates NO mobs and NO world clock/weather
# (blaze/env/blaze_core.h:27 + :2133; snapshots are baked with `--mobs off`,
# blaze/env/make_snapshots.py:114/138) and pins vitals to difficulty NORMAL
# with naturalRegeneration=true (blaze/core/player_vitals.h:13). A Java
# `fresh` reset builds a plain vanilla world instead (Recorder.launchWorld
# passes only seed/mode/type/structures; applyLaunchSettings' gamerules are
# one-shot at first join, Recorder.java:1652), so mobs spawn and the clock
# runs. These commands close that gap from the eval side only - no training
# and no bridge-protocol change.
ENVMATCH = True
ENVMATCH_CMDS = [
    "gamerule doMobSpawning false",   # blaze: no mobs at all
    "gamerule doMobLoot false",       # so the cull below drops no items
    "gamerule doDaylightCycle false",  # blaze: world clock not ticked
    "gamerule doWeatherCycle false",  # blaze: no weather
    "gamerule naturalRegeneration true",   # player_vitals.h:13 (vanilla default)
    "difficulty normal",              # player_vitals.h:13 FIXED DIFFICULTY
    "time set 6000",                  # frozen at noon (no clock in blaze)
    "weather clear",
    "kill @e[type=!Player]",          # cull worldgen mobs (t=0: no items yet)
    # NB: runcmds counts a command that returns 0 as failed, and CommandKill
    # returns 0 when the selector matches nothing - so `ran:8 failed:1` in the
    # ack below is the benign "world had no entities to cull yet" case, not a
    # gamerule that did not apply (verified by running each command alone).
]


def apply_envmatch(env):
    """Run the env-match commands and report the resulting world state."""
    ack = env._cmd({"cmd": "runcmds", "action": {"cmds": ENVMATCH_CMDS}})
    o = env.obs()
    ents = {}
    for e in o.get("entities", []):
        ents[e.get("type")] = ents.get(e.get("type"), 0) + 1
    tw = o.get("time", {})
    print(f"  envmatch {ack} -> world_time={tw.get('world_time')} "
          f"raining={tw.get('raining')} health={o.get('health')} "
          f"food={o.get('food')} entity_count={o.get('entity_count')} {ents}",
          flush=True)
    return ack, o
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
    # Drop back to the realtime server tick BEFORE tearing the world down.
    # With overclock(1) the integrated server free-runs at ~1000 TPS; once
    # mc.loadWorld(null) nulls the client world, every in-flight
    # SPacketEntityVelocity NPEs in NetHandlerPlayClient.handleEntityVelocity
    # (~36k/s, 30 log lines each) and starves the client thread until the
    # bridge's 120s request poll expires -> {"ok":false,"error":"timeout"}
    # (observed 12:21:22-27, 180k stacks / 300MB of runclient.log).
    try:
        env.overclock(50)
    except (ConnectionError, TimeoutError) as exc:
        print(f"  warning: could not restore realtime tick: {exc}", flush=True)
    o = None
    for tryn in range(3):
        o = env.reset({"seed": seed, "fresh": True}, timeout=600)
        if o.get("ok"):
            break
        print(f"  reset attempt {tryn} failed: {o}; retrying", flush=True)
        time.sleep(10.0)
    if not o.get("ok"):
        raise RuntimeError(f"reset failed: {o}")
    try:
        # Server tick length in ms. NB: the STEP loop is client-tick synced,
        # the integrated server is NOT - it free-runs on its own timer. With
        # overclock(1) it does ~420 server ticks per policy decision (measured
        # on gamer: 12514 SyncManager ticks / 30 decisions), i.e. mobs, hunger,
        # drowning and the day/night cycle race ~100x ahead of the policy's
        # control rate, which blaze (4 ticks per decision, no day/night, mobs
        # off) cannot represent. 50 = vanilla realtime keeps the drift at ~2x.
        env.overclock(OVERCLOCK_MS)
    except (ConnectionError, TimeoutError) as exc:
        print(f"  warning: could not set bridge server tick: {exc}", flush=True)
    envmatch_ack = None
    if ENVMATCH:
        envmatch_ack, _ = apply_envmatch(env)
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
    wt0 = obs.get("time", {}).get("world_time")
    min_health = float(obs.get("health", 20.0))
    ent_near = {}          # entity type -> closest |d| ever observed

    def note_entities(o):
        for e in o.get("entities", []):
            t = e.get("type")
            d = math.sqrt(e.get("dx", 0.0) ** 2 + e.get("dy", 0.0) ** 2
                          + e.get("dz", 0.0) ** 2)
            if t not in ent_near or d < ent_near[t]:
                ent_near[t] = d

    def diag(o):
        tw = o.get("time", {})
        return {
            "envmatch": bool(ENVMATCH),
            "envmatch_ack": envmatch_ack,
            "world_time_start": wt0,
            "world_time_end": tw.get("world_time"),
            "raining_end": tw.get("raining"),
            "health_end": o.get("health"),
            "food_end": o.get("food"),
            "min_health": min_health,
            "died": bool(o.get("dead")),
            "entity_min_dist": {k: round(v, 2) for k, v in
                                sorted(ent_near.items(), key=lambda kv: kv[1])},
        }

    note_entities(obs)
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
        note_entities(obs)
        min_health = min(min_health, float(obs.get("health", 20.0)))
        if frames_dir and FRAME_EVERY and dec % FRAME_EVERY == 0:
            env._cmd({"cmd": "frame", "action":
                      {"file": os.path.join(frames_dir, f"f{dec:05d}.png")}})
        if dec % 100 == 0:
            inv = [int(v) for v in best[0, :9]]
            print(f"    dec {dec:5d}  {(dec+1)*REPEAT/(time.time()-t0):5.1f} "
                  f"t/s  y {obs['y']:6.1f}  best inv {inv}  "
                  f"cont {obs['container']}  hp {obs.get('health'):.1f}  "
                  f"wt {obs.get('time', {}).get('world_time')}  "
                  f"ents {obs.get('entity_count')}", flush=True)
        if int(status[0, IX_TORCH]) >= 1:
            res = episode_result(seed, rng_seed, N_STAGES, best, obs,
                                 actions_sent, non_noop_steps,
                                 local_action_hash.hexdigest(),
                                 format(local_action_fnv, "x"))
            res.update(diag(obs))
            return res
        if obs.get("dead"):
            print("    died", flush=True)
            break
    res = episode_result(seed, rng_seed,
                         int(stage_of_best(best[:, :9])[0]), best, obs,
                         actions_sent, non_noop_steps,
                         local_action_hash.hexdigest(),
                         format(local_action_fnv, "x"))
    res.update(diag(obs))
    return res


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
    global EP_TICKS, TRIES, OVERCLOCK_MS, FRAME_EVERY, FRAMES_ROOT
    global RESULT_JSON, ENVMATCH
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("seeds", nargs="*", type=int,
                    help="world seeds (default: built-in ALL_SEEDS list)")
    ap.add_argument("--tries", type=int, default=5,
                    help="attempts per seed (default 5)")
    ap.add_argument("--ep-ticks", type=int, default=6000,
                    help="episode length in game ticks (default 6000)")
    ap.add_argument("--overclock-ms", type=int, default=50,
                    help="server tick length ms (default 50; 1 free-runs)")
    ap.add_argument("--frame-every", type=int, default=1,
                    help="save a frame every N decisions (0 = none)")
    ap.add_argument("--frames-root", default="/tmp/qrl_chain_demo",
                    help="directory for per-attempt frame dumps")
    ap.add_argument("--result-json", default=None,
                    help="optional path for the result artifact JSON")
    ap.add_argument("--chain-net", default="chain_net_cu.pt",
                    help="checkpoint filename under blaze/rl/out/")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--envmatch", dest="envmatch", action="store_true",
                   default=True,
                   help="apply training-env match commands (default)")
    g.add_argument("--no-envmatch", dest="envmatch", action="store_false",
                   help="skip env-match (vanilla world)")
    args = ap.parse_args()
    seeds = list(args.seeds) or ALL_SEEDS
    TRIES = args.tries
    EP_TICKS = args.ep_ticks
    OVERCLOCK_MS = args.overclock_ms
    FRAME_EVERY = args.frame_every
    FRAMES_ROOT = args.frames_root
    RESULT_JSON = args.result_json
    ENVMATCH = bool(args.envmatch)

    sys.path.insert(0, HERE)
    sys.path.insert(0, RL)
    sys.path.insert(0, os.path.join(RL, "blaze"))
    import numpy as np  # noqa: F401
    import torch
    from ppo_chain_cu import (  # noqa: F401
        FWD, IX_TORCH, MILE_NAMES, N_STAGES, NPLANES, PITCHES, REPEAT, STACK,
        YAWS, ChainPolicy, build_frame, build_scal, obs_float, stage_of_best,
    )
    from qrl_client import NetheriteEnv
    # re-bind module globals used by run_episode / rest of main
    g = globals()
    g.update({
        "np": np, "torch": torch, "ChainPolicy": ChainPolicy,
        "NetheriteEnv": NetheriteEnv, "FWD": FWD, "IX_TORCH": IX_TORCH,
        "MILE_NAMES": MILE_NAMES, "N_STAGES": N_STAGES, "NPLANES": NPLANES,
        "PITCHES": PITCHES, "REPEAT": REPEAT, "STACK": STACK, "YAWS": YAWS,
        "build_frame": build_frame, "build_scal": build_scal,
        "obs_float": obs_float, "stage_of_best": stage_of_best,
    })

    net = ChainPolicy()
    net_file = args.chain_net
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
            "envmatch": bool(ENVMATCH),
            "envmatch_cmds": ENVMATCH_CMDS if ENVMATCH else [],
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
