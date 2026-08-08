#!/usr/bin/env python3
"""Scan native-checkpoint blocks through the same RL world probe as recovery."""
import argparse
import json
import os
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", type=pathlib.Path, required=True)
    parser.add_argument("--save-root", type=pathlib.Path, required=True)
    parser.add_argument("--slot", required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--block", type=int, required=True)
    parser.add_argument("--x", type=int, nargs=2, required=True)
    parser.add_argument("--y", type=int, nargs=2, required=True)
    parser.add_argument("--z", type=int, nargs=2, required=True)
    args = parser.parse_args()
    env = os.environ.copy()
    env["MAGMA_NATIVE_WORLD_ROOT"] = str(args.save_root.resolve())
    env["MAGMA_RL_LOAD_SLOT"] = args.slot
    command = [
        str(args.game.resolve()), "--seed", str(args.seed),
        "--world", "default", "--view-distance", "6", "--mobs", "off",
        "--rl", "--set", "vanilla_spawn=1", "--set", "spawn_offset_x=0",
        "--set", "spawn_offset_z=0",
    ]
    proc = subprocess.Popen(command, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, bufsize=1, env=env)
    if not proc.stdout.readline():
        raise RuntimeError(proc.stderr.read())
    found = []
    for x in range(args.x[0], args.x[1] + 1):
        for z in range(args.z[0], args.z[1] + 1):
            for y in range(args.y[0], args.y[1] + 1):
                proc.stdin.write(json.dumps({
                    "probe_x": x, "probe_y": y, "probe_z": z, "cam": 0,
                }, separators=(",", ":")) + "\n")
                proc.stdin.flush()
                row = json.loads(proc.stdout.readline())
                if row["probe"][0] == args.block:
                    found.append([x, y, z])
    proc.stdin.close()
    proc.wait(timeout=30)
    print(json.dumps(found))


if __name__ == "__main__":
    main()
