#!/usr/bin/env python3
"""Replay an ordinary tape into a full native save using bounded processes."""
import argparse
import json
import os
import pathlib
import subprocess


def launch(args, load):
    command = [
        str(args.game.resolve()), "--seed", str(args.seed), "--world", "default",
        "--view-distance", str(args.view_distance), "--mobs", "on", "--rl",
        "--set", "vanilla_spawn=1",
        "--set", f"spawn_offset_x={args.spawn_offset_x}",
        "--set", f"spawn_offset_z={args.spawn_offset_z}",
    ]
    env = os.environ.copy()
    env["MAGMA_NATIVE_WORLD_ROOT"] = str(args.save_root.resolve())
    if load:
        env["MAGMA_RL_LOAD_SLOT"] = args.slot
    proc = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, bufsize=1, env=env)
    first = proc.stdout.readline()
    if not first:
        raise RuntimeError(proc.stderr.read().strip() or "RL launch failed")
    return proc, json.loads(first)


def finish(proc):
    proc.stdin.close()
    proc.wait(timeout=60)
    stderr = proc.stderr.read()
    if proc.returncode:
        raise RuntimeError(stderr.strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tape", type=pathlib.Path)
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--save-root", type=pathlib.Path, required=True)
    parser.add_argument("--slot", default="video-recovery")
    parser.add_argument("--chunk", type=int, default=1000)
    parser.add_argument("--view-distance", type=int, default=2)
    parser.add_argument("--spawn-offset-x", type=int, default=-7)
    parser.add_argument("--spawn-offset-z", type=int, default=-10)
    args = parser.parse_args()
    if args.chunk < 1:
        parser.error("--chunk must be positive")

    with args.tape.open(encoding="utf-8") as stream:
        rows = [json.loads(line) for line in stream if line.strip()]
    offset = 0
    load = False
    final = None
    while offset < len(rows):
        proc, initial = launch(args, load)
        if initial["t"] != offset:
            raise RuntimeError(
                f"checkpoint tick {initial['t']} != tape offset {offset}")
        stop = min(offset + args.chunk, len(rows))
        for index in range(offset, stop):
            action = {key: value for key, value in rows[index].items()
                      if key not in ("tick", "type")}
            action["cam"] = 0
            if index + 1 == stop:
                action["save_slot"] = args.slot
            proc.stdin.write(json.dumps(action, separators=(",", ":")) + "\n")
            proc.stdin.flush()
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError(proc.stderr.read().strip() or
                                   f"RL process ended at action {index}")
            final = json.loads(line)
        finish(proc)
        offset = stop
        load = True
        print(json.dumps({"saved_tick": offset, "x": final["x"],
                          "y": final["y"], "z": final["z"]}), flush=True)


if __name__ == "__main__":
    main()
