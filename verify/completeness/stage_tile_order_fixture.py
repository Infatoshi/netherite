#!/usr/bin/env python3
"""Stage cross-type TileEntity insertion order in a real parked Java save."""

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
          instance: int, seed: int, fixture: str) -> None:
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
        save_fork.request(port, "normalize_reload_locked")
        boundary = save_fork.request(port, "authoritative_state_locked")
        authoritative = boundary.get("authoritative") or {}
        x = math.floor(float(authoritative.get("x", observation["x"])))
        y = math.floor(float(authoritative.get("y", observation["y"])))
        z = math.floor(float(authoritative.get("z", observation["z"])))

        old_tiles = authoritative.get("loaded_tiles", [])
        cleared = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [row["x"], row["y"], row["z"], 0, 0]
                for row in old_tiles
            ],
        })
        if not cleared.get("ok"):
            raise StageError("could not remove pre-existing local tiles")

        if fixture == "cross_type":
            # Deliberately non-coordinate, cross-type insertion order. The
            # first five implement ITickable; dispenser is the control.
            blocks = [
                [x + 5, y, z + 1, 154, 0],
                [x + 1, y, z + 1, 61, 2],
                [x + 4, y, z + 1, 54, 2],
                [x + 2, y, z + 1, 117, 0],
                [x + 3, y, z + 1, 151, 0],
                [x + 6, y, z + 1, 23, 3],
            ]
        else:
            chest = [x + 3, y, z + 1, 54, 2]
            west_hopper = [x + 2, y, z + 1, 154, 5]
            east_hopper = [x + 4, y, z + 1, 154, 4]
            hoppers = [west_hopper, east_hopper]
            if fixture == "hopper_reverse":
                hoppers.reverse()
            blocks = [chest, *hoppers]
        staged = save_fork.request(
            port, "setblocks_locked", {"blocks": blocks})
        authoritative = staged.get("authoritative") or {}
        if not staged.get("ok") \
                or authoritative.get("loaded_tiles_complete") is not True:
            raise StageError("Java tile-order fixture is not complete")
        if fixture != "cross_type":
            for hopper, item in (
                    ([x + 2, y, z + 1], 1),
                    ([x + 4, y, z + 1], 4)):
                staged = save_fork.request(
                    port, "set_container_slot_locked", {
                        "x": hopper[0], "y": hopper[1], "z": hopper[2],
                        "slot": 0, "item": item, "count": 1, "meta": 0,
                    })
                authoritative = staged.get("authoritative") or {}
                if not staged.get("ok"):
                    raise StageError("could not seed competing hopper")
        positions = {(row[0], row[1], row[2]) for row in blocks}
        actual = [
            [row["x"], row["y"], row["z"]]
            for row in sorted(
                authoritative.get("loaded_tiles", []),
                key=lambda row: row["loaded_order"])
            if (row["x"], row["y"], row["z"]) in positions
        ]
        expected = [row[:3] for row in blocks]
        if actual != expected:
            raise StageError(
                f"Java tile insertion order mismatch: {actual!r} != {expected!r}")
        save_fork.capture_locked(port, output)
        paired = (
            {"left": "hopper-forward", "right": "hopper-reverse",
             "same_tick": True}
            if fixture != "cross_type"
            else {"left": "cross-type-insertion",
                  "right": "coordinate-sorted-negative-control",
                  "same_tick": True}
        )
        negative = (
            {"mutation": "reverse competing hopper insertion order",
             "expected_path": "$/loaded_tile_order[0]",
             "before": "west-hopper-first", "after": "east-hopper-first"}
            if fixture != "cross_type"
            else {"mutation": "sort inserted tiles by coordinate",
                  "expected_path": "$/loaded_tile_order[0]",
                  "before": "insertion-order", "after": "coordinate-order"}
        )
        (output / "fixture_tile_order.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": (
                "save03-hopper-order" if fixture != "cross_type"
                else "save03-cross-tile-order"),
            "todo": "SAVE-03",
            "fixture": fixture,
            "paired_boundary": paired,
            "negative_control": negative,
            "horizons": [1],
            "inputs": [{}],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in ("numeric", "blocks", "light", "order")
            ],
            "positions": expected,
            "classes": [
                row["class"] for row in sorted(
                    authoritative["loaded_tiles"],
                    key=lambda row: row["loaded_order"])
                if (row["x"], row["y"], row["z"]) in positions
            ],
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
    parser.add_argument(
        "--fixture",
        choices=("cross_type", "hopper_forward", "hopper_reverse"),
        default="cross_type")
    args = parser.parse_args()
    stage(
        args.source.resolve(), args.output.resolve(), args.instance,
        args.seed, args.fixture)
    print(f"PASS staged real Java cross-type tile order -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage tile-order fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
