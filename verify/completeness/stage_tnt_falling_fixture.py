#!/usr/bin/env python3
"""Stage persistent primed-TNT and falling-sand save states."""

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
        if state.get("entities"):
            raise StageError("baseline contains a pre-existing entity")
        px = math.floor(float(state["x"]))
        py = math.floor(float(state["y"]))
        pz = math.floor(float(state["z"]))
        tnt_pos = [px - 4.5, py + 6.0, pz + 0.5]
        falling_pos = [px + 4.5, py + 6.0, pz + 0.5]

        blocks = []
        for center_x in (math.floor(tnt_pos[0]),
                         math.floor(falling_pos[0])):
            for x in range(center_x - 1, center_x + 2):
                for z in range(pz - 1, pz + 2):
                    blocks.append([x, py - 1, z, 1, 0])
                    for y in range(py, py + 9):
                        blocks.append([x, y, z, 0, 0])
        prepared = save_fork.request(port, "setblocks_locked", {
            "blocks": blocks,
        })
        if not prepared.get("ok"):
            raise StageError(f"could not prepare entity landing columns: {prepared}")

        tnt = save_fork.request(port, "summon_locked", {
            "type": "primed_tnt", "fuse": 40,
            "x": tnt_pos[0], "y": tnt_pos[1], "z": tnt_pos[2],
            "mx": 0.0625, "my": -0.125, "mz": -0.03125,
            "eid": 5601,
            "uuid": "9c4a1dc0-a599-4d35-8000-000000005601",
        })
        falling = save_fork.request(port, "summon_locked", {
            "type": "falling_block", "block": 12, "meta": 0,
            "fall_time": 5,
            "x": falling_pos[0], "y": falling_pos[1], "z": falling_pos[2],
            "mx": -0.0625, "my": -0.125, "mz": 0.03125,
            "eid": 5602,
            "uuid": "9c4a1dc0-a599-4d35-8000-000000005602",
        })
        boundary = falling.get("authoritative") or {}
        entities = boundary.get("entities") or []
        types = [row.get("type") for row in entities]
        exact = {
            row.get("type"): row.get(
                "falling_exact" if row.get("type") == "EntityFallingBlock"
                else "primed_tnt_exact")
            for row in entities
            if row.get("type") in ("EntityFallingBlock", "EntityTNTPrimed")
        }
        if not tnt.get("ok") or not falling.get("ok") \
                or types.count("EntityTNTPrimed") != 1 \
                or types.count("EntityFallingBlock") != 1 \
                or exact != {
                    "EntityTNTPrimed": True,
                    "EntityFallingBlock": True,
                }:
            raise StageError(
                f"Java did not stage exact TNT/falling pair: "
                f"types={types} exact={exact}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-primed-tnt-falling-sand-mid-flight",
            "todo": "SAVE-08",
            "fixture": "primed-tnt-falling-sand",
            "paired_boundary": {
                "left": "primed-tnt-fuse-40",
                "right": "falling-sand-age-5",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "decrement the persisted TNT fuse by one",
                "expected_path": "$/entities/EntityTNTPrimed/fuse",
                "before": 40,
                "after": 39,
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source_entities": ["EntityTNTPrimed", "EntityFallingBlock"],
            "java_reload_semantics": "persist-move-and-land",
        }
        (output / "fixture_tnt_falling.json").write_text(
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
    print(f"PASS staged Java TNT/falling boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage TNT/falling fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
