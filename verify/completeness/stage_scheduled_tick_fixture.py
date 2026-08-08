#!/usr/bin/env python3
"""Stage a causal pending-block-tick boundary in a real Java save."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class StageError(RuntimeError):
    pass


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, delay: int) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(source / "save"),
        "ORACLE_POOL_USERNAME": "PoolPlayer0",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    try:
        _oracle("start", instance, seed, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if observation.get("ok") and "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise StageError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        authoritative = normalized.get("authoritative") or {}
        x = math.floor(float(authoritative.get("x", observation["x"])))
        y = math.floor(float(authoritative.get("y", observation["y"])))
        z = math.floor(float(authoritative.get("z", observation["z"])))
        old_tiles = authoritative.get("loaded_tiles", [])
        if old_tiles:
            cleared = save_fork.request(port, "setblocks_locked", {
                "blocks": [
                    [row["x"], row["y"], row["z"], 0, 0]
                    for row in old_tiles
                ],
            })
            if not cleared.get("ok"):
                raise StageError("could not remove pre-existing local tiles")
        position = [x + 3, y + 2, z + 1]
        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [position[0], y, position[2], 1, 0],
                [position[0], y + 1, position[2], 0, 0],
                [*position, 12, 0],
            ],
        })
        if not placed.get("ok"):
            raise StageError("could not place falling-sand callback source")
        scheduled = save_fork.request(port, "schedule_locked", {
            "x": position[0], "y": position[1], "z": position[2],
            "block": 12, "delay": delay, "priority": 0,
            "replace": True,
        })
        authoritative = scheduled.get("authoritative") or {}
        matching = [
            row for row in authoritative.get("scheduled_ticks", [])
            if [row.get("x"), row.get("y"), row.get("z")] == position
            and row.get("block") == 12
        ]
        if not scheduled.get("ok") or len(matching) != 1:
            raise StageError("Java did not retain exactly one sand callback")
        save_fork.capture_locked(port, output)
        (output / "fixture_scheduled_tick.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save07-falling-sand-due-boundary",
            "todo": "SAVE-07",
            "fixture": f"falling_sand_delay_{delay}",
            "paired_boundary": {
                "left": "delay-one", "right": "delay-two",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "move callback due time from t+1 to t+2",
                "expected_path": "$/scheduled_ticks[0]/time",
                "before": 1, "after": 2,
            },
            "horizons": [1, 2],
            "inputs": [{}, {}],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "numeric", "blocks", "light", "queues", "order")
            ],
            "position": position,
            "block": 12,
            "delay": delay,
        }, indent=2, sort_keys=True) + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=35)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--delay", type=int, choices=(1, 2), required=True)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.delay)
    print(f"PASS staged real Java scheduled tick -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage scheduled-tick fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
