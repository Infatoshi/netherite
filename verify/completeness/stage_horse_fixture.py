#!/usr/bin/env python3
"""Stage all five AbstractHorse subtypes at a strict save boundary."""

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
        normalized = save_fork.request(port, "normalize_reload_locked")
        save_fork.request(port, "clear_entities_locked")
        state = normalized.get("authoritative") or observation
        px = math.floor(float(state["x"]))
        py = math.floor(float(state["y"]))
        pz = math.floor(float(state["z"]))
        blocks = []
        for x in range(px + 2, px + 25):
            for z in range(pz - 2, pz + 3):
                for y in range(py, py + 8):
                    blocks.append([x, y, z, 0, 0])
        save_fork.request(port, "setblocks_locked", {"blocks": blocks})
        save_fork.request(port, "clear_entities_locked")

        fixtures = [
            {
                "horse_kind": "horse", "max_health": 31.0,
                "health": 28.0, "movement_speed": 0.2875,
                "jump_strength": 0.91, "growing_age": 0,
                "tame": True, "bred": True, "temper": 87,
                "variant": 0x304, "saddled": True, "armor": 3,
                "mouth_open": True, "open_mouth_counter": 14,
                "head_lean": 0.25, "prev_head_lean": 0.125,
                "mouth_openness": 0.75,
                "prev_mouth_openness": 0.625,
            },
            {
                "horse_kind": "donkey", "max_health": 22.0,
                "health": 17.0, "movement_speed": 0.175,
                "jump_strength": 0.5, "growing_age": -1234,
                "tame": True, "temper": 41, "chested": True,
                "saddled": True, "storage_item": 264,
                "storage_count": 5, "storage_slot": 16,
                "eating": True, "eating_counter": 37,
                "head_lean": 0.8, "prev_head_lean": 0.7,
            },
            {
                "horse_kind": "mule", "max_health": 24.0,
                "health": 24.0, "movement_speed": 0.18,
                "jump_strength": 0.55, "growing_age": 4321,
                "tame": True, "bred": True, "temper": 63,
                "chested": True, "storage_item": 260,
                "storage_count": 7, "storage_slot": 2,
                "rearing": True, "jump_rearing_counter": 5,
                "rearing_amount": 0.5,
                "prev_rearing_amount": 0.375,
            },
            {
                "horse_kind": "skeleton", "max_health": 15.0,
                "health": 13.0, "movement_speed": 0.2,
                "jump_strength": 0.61, "growing_age": 0,
                "tame": True, "temper": 57, "saddled": True,
                "trap": True, "trap_time": 17997,
                "tail_counter": 1, "sprint_counter": 22,
                "gallop_time": 6,
            },
            {
                "horse_kind": "zombie", "max_health": 19.0,
                "health": 16.0, "movement_speed": 0.2,
                "jump_strength": 0.64, "growing_age": -321,
                "tame": True, "bred": True, "temper": 72,
                "saddled": True, "horse_jumping": True,
                "jump_power": 0.8, "allow_stand_sliding": True,
            },
        ]
        latest: dict = {}
        for index, fixture in enumerate(fixtures):
            latest = save_fork.request(port, "summon_locked", {
                "type": "horse", **fixture,
                "x": px + 4.5 + index * 4.0,
                "y": py + 1.0, "z": pz + 0.5,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
                "no_ai": True, "ticks_existed": 40 + index,
                "owner_player": True,
                "entity_seed48": 1234567 + index * 1000,
                "eid": 6801 + index,
                "uuid": (
                    "9c4a1dc0-a599-4d35-8000-0000000068"
                    f"{index + 1:02d}"
                ),
            })
        entities = latest.get("authoritative", {}).get("entities", [])
        horses = [row for row in entities
                  if row.get("horse_exact") is True]
        if len(horses) != 5:
            raise StageError(
                f"Java did not stage all five exact horses: {horses}")
        if not all(row.get("horse_owner_present") for row in horses):
            raise StageError(
                f"Java did not retain all five horse owners: {horses}")
        save_fork.capture_locked(port, output)
        horizons = [1, 2, 3, 4, 8, 20]
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "ent02-horse-family-save",
            "todo": "ENT-02",
            "fixture": "horse-family-save",
            "paired_boundary": {
                "left": "live-memory-horse-family",
                "right": "cold-anvil-horse-family",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "decrement the skeleton trap expiry clock",
                "expected_path":
                    "$/entities/EntitySkeletonHorse/SkeletonTrapTime",
                "before": 17997,
                "after": 17996,
            },
            "horizons": horizons,
            "inputs": [{} for _ in range(max(horizons))],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues",
                    "order", "events")
            ],
            "source_entities": [
                "EntityHorse", "EntityDonkey", "EntityMule",
                "EntitySkeletonHorse", "EntityZombieHorse",
            ],
            "java_reload_semantics": "horse-family-save",
        }
        (output / "fixture_horse.json").write_text(
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
    stage(args.source.resolve(), args.output.resolve(),
          args.instance, args.seed)
    print(f"PASS staged Java horse boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage horse fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
