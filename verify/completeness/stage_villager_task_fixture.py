#!/usr/bin/env python3
"""Stage a real Java villager while one of its AI tasks is executing."""

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


def _world_entity(hidden: dict, dimension: int, identity: str) -> dict:
    world = next(row for row in hidden["worlds"] if row["dim"] == dimension)
    return next(row for row in world["entities"]
                if row.get("uuid") == identity)


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int) -> None:
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
        boundary = save_fork.request(
            port, "authoritative_state_locked")["authoritative"]
        if boundary.get("entities"):
            raise StageError("source has pre-existing represented entities")
        dimension = int(boundary["dim"])
        base_x = math.floor(float(boundary["x"])) + 5
        base_y = math.floor(float(boundary["y"]))
        base_z = math.floor(float(boundary["z"])) + 5
        blocks = []
        for z in range(base_z - 6, base_z + 7):
            for x in range(base_x - 6, base_x + 7):
                blocks.append([x, base_y - 1, z, 1, 0])
                for y in range(base_y, base_y + 4):
                    blocks.append([x, y, z, 0, 0])
        changed = save_fork.request(
            port, "setblocks_locked", {"blocks": blocks})
        if not changed.get("ok"):
            raise StageError(f"could not stage villager platform: {changed}")
        eid = 5301
        identity = "00000000-0000-0000-0000-000000005301"
        summoned = save_fork.request(port, "summon_locked", {
            "type": "villager", "profession": 1, "no_ai": 0,
            "entity_seed48": 0x123456789ABC,
            "x": base_x + 0.5, "y": base_y, "z": base_z + 0.5,
            "mx": 0.0, "my": 0.0, "mz": 0.0,
            "eid": eid, "uuid": identity,
        })
        if not summoned.get("ok") or summoned.get("eid") != eid:
            raise StageError(f"could not spawn villager: {summoned}")
        executing = []
        task_tick = -1
        hidden = None
        for tick in range(1, 201):
            save_fork.request(port, "server_tick_locked")
            hidden = save_fork.request(port, "hidden_state_locked")
            entity = _world_entity(hidden, dimension, identity)
            executing = [row for row in entity.get("ai_tasks", [])
                         if row.get("executing") is True]
            if executing:
                task_tick = tick
                break
        if hidden is None or not executing:
            raise StageError("villager did not enter an AI task in 200 ticks")
        oracle = save_fork.request(port, "save_world_locked")
        hidden = save_fork.request(port, "hidden_state_locked")
        entity = _world_entity(hidden, dimension, identity)
        executing = [row for row in entity.get("ai_tasks", [])
                     if row.get("executing") is True]
        if not executing:
            raise StageError("villager task ended before the save boundary")
        task_names = [row["class"] for row in executing]
        save_fork.write_snapshot(
            pathlib.Path(oracle["world_directory"]), output, oracle, hidden)
        (output / "fixture_villager_task.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-villager-executing-task",
            "todo": "SAVE-08",
            "paired_boundary": {
                "left": "source-task-executing",
                "right": "cold-reload-task-set-rebuilt",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "clear the source executing-task set",
                "expected_path": "$/hidden/entities/villager/ai_tasks",
                "before": task_names,
                "after": [],
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": "numeric", "required": True,
                 "minimum_observations": 21},
                {"family": "blocks", "required": True,
                 "minimum_observations": 6},
                {"family": "light", "required": True,
                 "minimum_observations": 6},
                {"family": "order", "required": True,
                 "minimum_observations": 21},
            ],
            "fixture": "villager_task_reload",
            "eid": eid,
            "uuid": identity,
            "source_task_tick": task_tick,
            "source_executing_tasks": task_names,
            "reload_semantics": "executing tasks and path are transient",
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
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed)
    print(f"PASS staged Java villager task boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage villager task fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
