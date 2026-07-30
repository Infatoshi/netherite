#!/usr/bin/env python3
"""spawns.py - observe ORACLE mob spawns over a night at the seed-0 spawn.

Ground-truth capture only (magma is NOT asked to reproduce spawns yet): set
midnight, enable doMobSpawning, park the player at spawn in CREATIVE (counts
for the mob-spawning player radius, but hostiles do not target creative
players, so the 2400-tick watch is not interrupted by combat/death), and
record per tick:
  - obs entities[8]  (nearest-8 view the RL env sees)   -> obs_entities.jsonl
  - full loaded-entity list every --full-every ticks via qrl getentities
    (server-thread atomic, doubles decoded)              -> entities_full.jsonl

Summary: spawn count by type, first-seen tick, distance from player at first
sight -> spawn_summary.md + spawn_summary.json.
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_lib as ol  # noqa: E402

PASSIVE_PREFIXES = ("EntityItem", "EntityXPOrb", "EntityArrow")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--ticks", type=int, default=2400)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--time", type=int, default=18000, help="world time (18000=midnight)")
    ap.add_argument("--pose", default="44.5 68.0 176.5 0.0 5.0")
    ap.add_argument("--full-every", type=int, default=20)
    ap.add_argument("--overclock", type=int, default=5,
                    help="server ms/tick during the watch (50=realtime)")
    ap.add_argument("--out", default=os.path.join(here, "out", "spawns"))
    ap.add_argument("--report", default=os.path.join(here, "report"))
    args = ap.parse_args()
    pose = [float(v) for v in args.pose.split()]
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.report, exist_ok=True)

    if not ol.bridge_up():
        raise SystemExit("qrl bridge is down; use run_trace.sh")
    env = ol.connect()
    o = ol.ensure_world(env, seed=args.seed)
    print(f"[spawns] world ready ({o['x']:.1f},{o['y']:.1f},{o['z']:.1f})")
    # kill leftovers so the trace starts from a clean slate
    print("[spawns] killentities:", env._cmd({"cmd": "killentities", "action": {}}))
    ol.freeze_scene(env, world_time=args.time, gamemode="creative",
                    mob_spawning=True)
    obs = ol.hold_pose(env, *pose, settle_ticks=30)
    print(f"[spawns] parked at ({obs['x']:.2f},{obs['y']:.2f},{obs['z']:.2f}), "
          f"time={args.time}, watching {args.ticks} ticks")
    if args.overclock != 50:
        env.overclock(args.overclock)

    first_seen = {}
    counts = {}
    seen_eids = {}
    px, py, pz = pose[0], pose[1], pose[2]
    f_obs = open(os.path.join(args.out, "obs_entities.jsonl"), "w")
    f_full = open(os.path.join(args.out, "entities_full.jsonl"), "w")
    t0 = time.time()
    try:
        for t in range(args.ticks):
            ob = env.step({})
            ents = ob.get("entities", [])
            f_obs.write(json.dumps({"tick": t, "entities": ents}) + "\n")
            if t % args.full_every == 0 or t == args.ticks - 1:
                full = ol.get_entities(env)
                f_full.write(json.dumps({"tick": t, **full}) + "\n")
                for e in full["ents"]:
                    typ = e["type"]
                    if e["eid"] not in seen_eids:
                        seen_eids[e["eid"]] = typ
                        counts[typ] = counts.get(typ, 0) + 1
                        if typ not in first_seen:
                            d = math.sqrt((e["x"] - px) ** 2 + (e["y"] - py) ** 2 +
                                          (e["z"] - pz) ** 2)
                            first_seen[typ] = {"tick": t, "dist": round(d, 1),
                                               "pos": [round(e["x"], 1),
                                                       round(e["y"], 1),
                                                       round(e["z"], 1)]}
            if t % 400 == 0:
                print(f"[spawns] tick {t}: {len(seen_eids)} unique entities, "
                      f"types={sorted(counts)} ({time.time()-t0:.0f}s)")
    finally:
        f_obs.close()
        f_full.close()
        env.overclock(50)
        # restore the quiet baseline scene
        ol.freeze_scene(env, world_time=6000, gamemode="survival",
                        mob_spawning=False)
        env.close()

    summary = {
        "ticks": args.ticks, "world_time": args.time, "pose": pose,
        "unique_entities": len(seen_eids),
        "by_type": {k: {"count": counts[k], **first_seen.get(k, {})}
                    for k in sorted(counts)},
    }
    with open(os.path.join(args.out, "spawn_summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    md = os.path.join(args.report, "spawns.md")
    with open(md, "w") as f:
        f.write("# Oracle mob-spawn observation (ground truth for future parity)\n\n")
        f.write(f"Seed {args.seed}, world time frozen at {args.time}, "
                f"doMobSpawning on, player parked (creative) at "
                f"({pose[0]:.1f}, {pose[1]:.1f}, {pose[2]:.1f}), "
                f"{args.ticks} ticks observed. Raw traces: "
                f"out/spawns/obs_entities.jsonl (nearest-8 per tick), "
                f"out/spawns/entities_full.jsonl (full list every "
                f"{args.full_every} ticks).\n\n")
        f.write("| entity type | spawned (unique) | first-seen tick | "
                "first-seen dist (blocks) | first-seen pos |\n|---|---|---|---|---|\n")
        for k in sorted(counts, key=lambda k: -counts[k]):
            fs = first_seen.get(k, {})
            f.write(f"| {k} | {counts[k]} | {fs.get('tick','-')} | "
                    f"{fs.get('dist','-')} | {fs.get('pos','-')} |\n")
        f.write(f"\nTotal unique non-player entities: {len(seen_eids)}\n")
    print(f"[spawns] summary -> {md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
