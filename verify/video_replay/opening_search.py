#!/usr/bin/env python3
"""Search simple fresh-spawn movement primitives toward a video landmark."""
import argparse
import concurrent.futures
import json
import math
import pathlib
import subprocess


def rollout(args, candidate):
    dx, dz, yaw, move_ticks = candidate
    command = [
        str(args.game), "--seed", str(args.seed), "--world", "default",
        "--view-distance", "1", "--mobs", "off", "--rl",
        "--ticks", str(args.ticks), "--set", "vanilla_spawn=1",
        "--set", f"spawn_offset_x={dx}", "--set", f"spawn_offset_z={dz}",
    ]
    actions = []
    for tick in range(args.ticks):
        moving = args.move_start <= tick < args.move_start + move_ticks
        action = {"cam": 0}
        if tick == 0:
            action["dyaw"] = yaw - 180.0
        if moving:
            action["forward"] = 1
            action["sprint"] = int(args.sprint)
            action["jump"] = int(args.jump)
        actions.append(json.dumps(action, separators=(",", ":")))
    proc = subprocess.run(command, input="\n".join(actions) + "\n",
                          text=True, capture_output=True, check=True)
    last = json.loads(proc.stdout.splitlines()[-1])
    distance = math.hypot(last["x"] - args.target_x,
                          last["z"] - args.target_z)
    return {"spawn_offset": [dx, dz], "yaw": yaw,
            "move_start": args.move_start, "move_ticks": move_ticks,
            "ticks": args.ticks, "sprint": bool(args.sprint),
            "jump": bool(args.jump),
            "end": {key: last[key] for key in ("x", "y", "z", "yaw", "pitch")},
            "target": [args.target_x, args.target_z], "distance": distance}


def script_actions(result, final_yaw):
    rows = []
    for tick in range(result["ticks"]):
        moving = (result["move_start"] <= tick
                  < result["move_start"] + result["move_ticks"])
        row = {"tick": tick, "type": "action"}
        if tick == 0:
            row["dyaw"] = result["yaw"] - 180.0
        if moving:
            row["forward"] = 1
            row["sprint"] = int(result["sprint"])
            row["jump"] = int(result["jump"])
        if final_yaw is not None and tick == result["ticks"] - 1:
            row["dyaw"] = final_yaw - result["yaw"]
        rows.append(json.dumps(row, separators=(",", ":")))
    return "\n".join(rows) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--target-x", type=float, required=True)
    parser.add_argument("--target-z", type=float, required=True)
    parser.add_argument("--ticks", type=int, default=160)
    parser.add_argument("--move-start", type=int, default=40)
    parser.add_argument("--move-ticks", type=int, nargs="+",
                        default=[50, 60, 70, 80, 90])
    parser.add_argument("--yaws", type=float, nargs="+", default=[180.0])
    parser.add_argument("--sprint", action="store_true")
    parser.add_argument("--jump", action="store_true")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--out", required=True)
    parser.add_argument("--emit-actions")
    parser.add_argument("--final-yaw", type=float)
    args = parser.parse_args()
    args.game = args.game.resolve()
    candidates = [(dx, dz, yaw, move_ticks)
                  for dx in range(-10, -5) for dz in range(-10, -5)
                  for yaw in args.yaws for move_ticks in args.move_ticks]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(lambda value: rollout(args, value), candidates))
    results.sort(key=lambda value: value["distance"])
    pathlib.Path(args.out).write_text(
        json.dumps({"schema": 1, "results": results}, indent=2) + "\n",
        encoding="utf-8")
    if args.emit_actions:
        pathlib.Path(args.emit_actions).write_text(
            script_actions(results[0], args.final_yaw), encoding="utf-8")
    print(json.dumps(results[0], sort_keys=True))


if __name__ == "__main__":
    main()
