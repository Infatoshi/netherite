#!/usr/bin/env python3
"""Stage AI-05's 103-entity opposite-order campaign in real Java 1.11.2."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time
import uuid
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class StageError(RuntimeError):
    pass


def _oracle(
    action: str, instance: int, seed: int, environment: dict[str, str],
) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _position(
    index: int, origin_x: float, origin_y: float, origin_z: float,
) -> tuple[float, float, float]:
    return (
        origin_x + (index % 11) * 2.5,
        origin_y,
        origin_z + (index // 11) * 2.5,
    )


def _definitions(x: float, y: float, z: float) -> list[dict[str, Any]]:
    definitions: list[dict[str, Any]] = []
    eid = 62000

    # Classes supported by Recorder's exact no-AI constructor fixture.  The
    # numeric argument is fixture-local; the captured class name owns capsule
    # translation, so no product enum is inferred here.
    no_ai_types = (
        2, 3, 4, 5, 6, 7, 23, 26, 41, 51, 52, 53, 55, 56, 57,
        10, 11, 13, 14, 27, 32, 35, 36, 39, 12, 15, 58, 59, 60,
        61, 62, 24, 63, 64, 65,
    )
    for entity_type in no_ai_types:
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "no_ai_mob", "entity_type": entity_type,
            "x": px, "y": py, "z": pz, "no_ai": True,
            "entity_seed48": 0x240000000000 + len(definitions),
            "eid": eid,
        })
        eid += 1

    for kind in ("pig", "villager"):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": kind, "x": px, "y": py, "z": pz,
            "no_ai": 1, "entity_seed48": 0x250000000000 + eid,
            "eid": eid,
        })
        eid += 1

    for horse_kind in (
            "horse", "donkey", "mule", "skeleton", "zombie", "llama"):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "horse", "horse_kind": horse_kind,
            "x": px, "y": py, "z": pz, "no_ai": True,
            "entity_seed48": 0x260000000000 + eid, "eid": eid,
        })
        eid += 1

    for index in range(15):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "armor_stand", "x": px, "y": py, "z": pz,
            "yaw": float(index * 15), "on_ground": False,
            "entity_seed48": 0x270000000000 + index, "eid": eid,
        })
        eid += 1

    for _ in range(15):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "minecart", "x": px, "y": py, "z": pz,
            "eid": eid,
        })
        eid += 1

    for index in range(15):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "end_crystal", "x": px, "y": py, "z": pz,
            "inner_rotation": index, "show_bottom": index & 1,
            "has_beam": 0, "eid": eid,
        })
        eid += 1

    for index in range(15):
        px, py, pz = _position(len(definitions), x, y, z)
        definitions.append({
            "type": "item", "x": px, "y": py, "z": pz,
            "item": 1 + index % 5, "count": 1, "meta": 0,
            "pickup_delay": 32767, "eid": eid,
        })
        eid += 1

    if len(definitions) != 103:
        raise AssertionError(f"mixed Java definition count is {len(definitions)}")
    for definition in definitions:
        definition["mx"] = definition["my"] = definition["mz"] = 0.0
        definition["uuid"] = str(uuid.UUID(int=int(definition["eid"])))
    return definitions


def _hidden_order(hidden: dict[str, Any], dimension: int,
                  expected: set[int]) -> list[int]:
    world = next(
        (row for row in hidden.get("worlds", [])
         if int(row.get("dim", -9999)) == dimension), None)
    if world is None:
        raise StageError(f"hidden state has no dimension {dimension}")
    return [
        int(row["eid"]) for row in sorted(
            world.get("entities", []), key=lambda row: int(row["order"]))
        if int(row.get("eid", -1)) in expected
    ]


def stage(
    output: pathlib.Path, instance: int, seed: int, reverse: bool,
) -> None:
    if output.exists():
        raise StageError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
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
                raise StageError("fresh Java oracle did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        boundary = save_fork.request(port, "server_step_lock")
        locked = True
        if not boundary.get("authoritative"):
            raise StageError("fresh Java oracle has no authoritative boundary")
        save_fork.request(port, "normalize_reload_locked")
        save_fork.request(port, "set_mob_spawning_locked", {"enabled": False})
        random_ticks = save_fork.request(
            port, "set_random_tick_speed_locked", {"speed": 0})
        if random_ticks.get("speed") != 0:
            raise StageError(
                f"could not isolate random block ticks: {random_ticks}")
        save_fork.request(port, "clear_entities_locked")
        boundary = save_fork.request(port, "authoritative_state_locked")
        authoritative = boundary.get("authoritative") or {}
        # Keep the entire 11x10 campaign inside the recorder's active entity
        # observation radius, not merely inside the server's loaded chunks.
        origin_x = math.floor(float(authoritative["x"])) - 12.0
        origin_y = math.floor(float(authoritative["y"])) + 1.0
        origin_z = math.floor(float(authoritative["z"])) - 11.0
        definitions = _definitions(origin_x, origin_y, origin_z)
        ordered = list(reversed(definitions)) if reverse else definitions
        for index, definition in enumerate(ordered):
            result = save_fork.request(port, "summon_locked", definition)
            if not result.get("ok"):
                raise StageError(
                    f"spawn {index}/{len(ordered)} failed: {result}")
            authoritative = result.get("authoritative") or {}
        expected = [int(row["eid"]) for row in ordered]
        expected_set = set(expected)
        hidden_before = save_fork.request(port, "hidden_state_locked")
        actual = _hidden_order(
            hidden_before, int(authoritative.get("dim", 0)), expected_set)
        if actual != expected:
            mismatch = next(
                (index for index, pair in enumerate(zip(actual, expected))
                 if pair[0] != pair[1]), min(len(actual), len(expected)))
            raise StageError(
                "Java loadedEntityList does not match spawn order: "
                f"actual={len(actual)} expected={len(expected)} "
                f"first_mismatch={mismatch} "
                f"actual_tail={actual[max(0, mismatch - 2):mismatch + 3]} "
                f"expected_tail={expected[max(0, mismatch - 2):mismatch + 3]}")
        # Villager's no-AI continuation schema begins at ticksExisted=2; tick
        # twice so this is a mature persisted boundary for every class.
        advanced = save_fork.request(port, "server_tick_locked")
        advanced = save_fork.request(port, "server_tick_locked")
        authoritative = advanced.get("authoritative") or {}
        oracle = save_fork.request(port, "save_world_locked")
        hidden_state = save_fork.request(port, "hidden_state_locked")
        advanced_order = _hidden_order(
            hidden_state, int(authoritative.get("dim", 0)), expected_set)
        if advanced_order != expected:
            raise StageError(
                "mixed fixture lost or reordered an entity during "
                "persistence tick")
        save_fork.write_snapshot(
            pathlib.Path(oracle["world_directory"]), output,
            oracle, hidden_state)
        fixture_name = "reverse" if reverse else "forward"
        (output / "fixture_mixed_order_campaign.json").write_text(
            json.dumps({
                "schema": "netherite.completeness_fixture",
                "version": 1,
                "id": f"ai05-mixed-order-1200-{fixture_name}",
                "todo": "AI-05",
                "paired_boundary": {
                    "left": "mixed-order-forward",
                    "right": "mixed-order-reverse",
                    "same_tick": True,
                },
                "negative_control": {
                    "mutation": "reverse 103-entity spawn insertion order",
                    "expected_path": "$/loaded_entity_order[0]",
                    "before": expected[-1] if reverse else expected[0],
                    "after": expected[0] if reverse else expected[-1],
                },
                "horizons": [1, 20, 200, 1200],
                "inputs": [{} for _ in range(1200)],
                "comparisons": [
                    {"family": family, "required": True,
                     "minimum_observations": 1}
                    for family in ("numeric", "blocks", "light", "order")
                ],
                "fixture": fixture_name,
                "entity_count": len(expected),
                "loaded_entity_order": expected,
                "raw_box": [
                    math.floor(origin_x) - 4, 0,
                    math.floor(origin_z) - 4,
                    math.floor(origin_x + 25.0) + 4, 16,
                    math.floor(origin_z + 22.5) + 4,
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
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=68)
    parser.add_argument("--seed", type=int, default=904771)
    parser.add_argument("--reverse", action="store_true")
    args = parser.parse_args()
    stage(args.output.resolve(), args.instance, args.seed, args.reverse)
    print(f"PASS staged Java mixed-order campaign -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, StageError, save_fork.SaveForkError) as exc:
        print(f"FAIL stage mixed-order campaign: {exc}", file=sys.stderr)
        raise SystemExit(1)
