#!/usr/bin/env python3
"""Stage a causal same-time scheduled-tick insertion-order boundary."""

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


def stage(source: pathlib.Path, output: pathlib.Path, instance: int,
          seed: int, upper_first: bool) -> None:
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
        authoritative = normalized.get("authoritative") or observation
        x = math.floor(float(authoritative["x"]))
        y = math.floor(float(authoritative["y"]))
        z = math.floor(float(authoritative["z"]))
        bottom = [x + 6, y + 2, z + 3]
        top = [bottom[0], bottom[1] + 1, bottom[2]]
        cleared = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [top[0], top[1], top[2], 0, 0],
                [bottom[0], bottom[1], bottom[2], 0, 0],
                [bottom[0], bottom[1] - 1, bottom[2], 0, 0],
                [bottom[0], bottom[1] - 2, bottom[2], 1, 0],
            ],
        })
        if not cleared.get("ok"):
            raise StageError("could not clear the stacked-sand fixture")
        order = (top, bottom) if upper_first else (bottom, top)
        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [[*position, 12, 0] for position in order],
        })
        authoritative = placed.get("authoritative") or {}
        matching = [
            row for row in authoritative.get("scheduled_ticks", [])
            if [row.get("x"), row.get("y"), row.get("z")]
                in (bottom, top)
            and row.get("block") == 12
        ]
        expected_positions = [list(position) for position in order]
        observed_positions = [
            [row["x"], row["y"], row["z"]] for row in matching]
        if not placed.get("ok") or len(matching) != 2 \
                or len({(row["time"], row["priority"])
                        for row in matching}) != 1 \
                or observed_positions != expected_positions:
            raise StageError(
                "Java did not retain the requested same-time tie order")
        save_fork.capture_locked(port, output)
        branch = "upper-first" if upper_first else "lower-first"
        (output / "fixture_scheduled_tie.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save07-stacked-sand-tie-order",
            "todo": "SAVE-07",
            "fixture": branch,
            "paired_boundary": {
                "left": "lower-first", "right": "upper-first",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "reverse equal-time equal-priority insertion",
                "expected_path": "$/scheduled_ticks/*/order",
                "before": "lower-first", "after": "upper-first",
            },
            "horizons": [1, 2],
            "inputs": [{}, {}],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "numeric", "blocks", "light", "queues", "order")
            ],
            "bottom": bottom,
            "top": top,
            "insertion_order": branch,
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
    parser.add_argument("--upper-first", action="store_true")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.upper_first)
    print(f"PASS staged Java scheduled tie -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage scheduled-tie fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
