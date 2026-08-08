#!/usr/bin/env python3
"""Stage saved repeater and observer pulses in a real Java world."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import struct
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
          seed: int) -> None:
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
        state = normalized.get("authoritative") or observation
        if state.get("scheduled_ticks"):
            raise StageError("baseline contains a pre-existing scheduled tick")
        x = math.floor(float(state["x"])) + 4
        y = math.floor(float(state["y"])) + 2
        z = math.floor(float(state["z"]))
        repeater = [x, y, z - 3]
        observer = [x, y, z + 3]

        # The reusable baseline can retain an older copy of this fixture.  An
        # observer's 1.11.2 getStateFromMeta intentionally drops POWERED, so
        # assigning an unpowered observer over stale metadata can be an
        # in-memory no-op and leave bit 3 in the region file.  Remove the old
        # blocks first so every state below is a real, dirty transition.
        fixture_positions = [
            [repeater[0], repeater[1] - 1, repeater[2]],
            [repeater[0] + 1, repeater[1], repeater[2]],
            [repeater[0] - 1, repeater[1], repeater[2]],
            repeater,
            [observer[0] - 1, observer[1], observer[2]],
            [observer[0] + 1, observer[1], observer[2]],
            observer,
        ]
        cleared = save_fork.request(port, "setblocks_locked", {
            "blocks": [[*position, 0, 0]
                       for position in fixture_positions],
        })
        if not cleared.get("ok"):
            raise StageError(f"could not clear saved pulse fixtures: {cleared}")

        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [repeater[0], repeater[1] - 1, repeater[2], 1, 0],
                [repeater[0] + 1, repeater[1], repeater[2], 123, 0],
                [repeater[0] - 1, repeater[1], repeater[2], 152, 0],
                [*repeater, 93, 1],
                [observer[0] - 1, observer[1], observer[2], 0, 0],
                [observer[0] + 1, observer[1], observer[2], 123, 0],
                [*observer, 218, 4],
            ],
        })
        if not placed.get("ok"):
            raise StageError(f"could not place saved pulse fixtures: {placed}")

        # Neighbor callbacks during the batch can start the observer before
        # the explicit queue boundary below.  Force the persisted half-cycle
        # to the unpowered state; setting the same observer block does not run
        # onBlockAdded, and schedule_locked then owns its sole pending entry.
        reset_observer = save_fork.request(port, "setblocks_locked", {
            "blocks": [[*observer, 218, 4]],
        })
        if not reset_observer.get("ok"):
            raise StageError(
                f"could not reset observer pulse fixture: {reset_observer}")
        observer_dump = run_root / "observer.u16le"
        dumped = save_fork.request(port, "getblocks_locked", {
            "x0": observer[0], "y0": observer[1], "z0": observer[2],
            "x1": observer[0], "y1": observer[1], "z1": observer[2],
            "file": str(observer_dump),
        })
        if not dumped.get("ok"):
            raise StageError(f"could not inspect observer fixture: {dumped}")
        observer_state = struct.unpack("<H", observer_dump.read_bytes())[0]
        if observer_state != (218 << 4 | 4):
            raise StageError(
                f"observer fixture is not unpowered: {observer_state >> 4}:"
                f"{observer_state & 15}")

        # Replace any onBlockAdded callback with explicitly controlled real
        # WorldServer entries at the same +2 boundary.
        for position, block, priority in (
                (repeater, 93, -1), (observer, 218, 0)):
            save_fork.request(port, "schedule_locked", {
                "x": position[0], "y": position[1], "z": position[2],
                "block": block, "delay": 2, "priority": priority,
                "replace": True,
            })
        boundary = save_fork.request(port, "authoritative_state_locked")
        state = boundary.get("authoritative") or {}
        scheduled = state.get("scheduled_ticks") or []
        expected = {
            (repeater[0], repeater[1], repeater[2], 93, -1),
            (observer[0], observer[1], observer[2], 218, 0),
        }
        actual = {
            (row.get("x"), row.get("y"), row.get("z"),
             row.get("block"), row.get("priority"))
            for row in scheduled
        }
        due_times = {row.get("time") for row in scheduled}
        if state.get("scheduled_ticks_complete") is not True \
                or actual != expected or len(due_times) != 1:
            raise StageError(
                f"Java did not stage the exact saved pulse pair: {scheduled}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-repeater-observer-pending-pulses",
            "todo": "SAVE-08",
            "fixture": "repeater-observer-pulses",
            "paired_boundary": {
                "left": "repeater-pending", "right": "observer-pending",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "remove the observer pending activation",
                "expected_path": "$/scheduled_ticks/observer",
                "before": 1, "after": 0,
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "repeater": repeater,
            "observer": observer,
            "java_reload_semantics": "persist-and-dispatch",
        }
        (output / "fixture_redstone_pulses.json").write_text(
            json.dumps(contract, indent=2, sort_keys=True) + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed)
    print(f"PASS staged Java repeater/observer pulse boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage redstone pulse fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
