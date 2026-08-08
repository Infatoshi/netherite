#!/usr/bin/env python3
"""Stage an active vanilla mob-spawner save boundary."""

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
sys.path.insert(0, str(ROOT / "magma" / "trace"))
import nbt_codec  # noqa: E402


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
          instance: int, seed: int, fresh: bool = False,
          custom: bool = False, cart: bool = False) -> None:
    if not fresh:
        save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_USERNAME": "PoolPlayer0",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_MOB_SPAWNING": "0",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    if not fresh:
        environment["ORACLE_POOL_SAVE_SOURCE"] = str(source / "save")
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
        gamerule = save_fork.request(
            port, "set_mob_spawning_locked", {"enabled": False})
        if gamerule.get("authoritative", {}).get(
                "do_mob_spawning") is not False:
            raise StageError("could not disable unrelated natural spawning")
        cleared_entities = save_fork.request(port, "clear_entities_locked")
        if cleared_entities.get("authoritative", {}).get("entities"):
            raise StageError("could not remove fixture-irrelevant entities")
        state = cleared_entities.get("authoritative") or \
            normalized.get("authoritative") or observation
        if state.get("loaded_tiles"):
            raise StageError("baseline contains a pre-existing tile entity")
        x = math.floor(float(state["x"])) + 4
        y = math.floor(float(state["y"]))
        z = math.floor(float(state["z"]))
        position = [x, y, z]

        if cart:
            rail_blocks = [
                [x + offset, y, z, 66, 1] for offset in (-1, 0, 1)
            ]
            placed = save_fork.request(
                port, "setblocks_locked", {"blocks": rail_blocks})
            if not placed.get("ok"):
                raise StageError(
                    f"could not install spawner-cart rail: {placed}")
            pig = {
                "id": {"type": "string", "value": "minecraft:pig"},
                "Pos": {"type": "list", "element_type": "double",
                        "value": [
                            {"type": "double", "value":
                             struct.pack(">d", x + 6.5).hex()},
                            {"type": "double", "value":
                             struct.pack(">d", float(y)).hex()},
                            {"type": "double", "value":
                             struct.pack(">d", z + 0.5).hex()},
                        ]},
                "Motion": {"type": "list", "element_type": "double",
                           "value": [
                               {"type": "double", "value":
                                "0000000000000000"},
                               {"type": "double", "value":
                                "0000000000000000"},
                               {"type": "double", "value":
                                "0000000000000000"},
                           ]},
                "Air": {"type": "short", "value": 123},
                "OnGround": {"type": "byte", "value": 0},
                "Health": {"type": "float", "value": "40c00000"},
                "NoAI": {"type": "byte", "value": 1},
                "Saddle": {"type": "byte", "value": 1},
                "InLove": {"type": "int", "value": 40},
            }
            zombie = {
                "id": {"type": "string", "value": "minecraft:zombie"},
            }
            logic_nbt = {
                "name": "",
                "tag": {"type": "compound", "value": {
                    "Delay": {"type": "short", "value": 0},
                    "MinSpawnDelay": {"type": "short", "value": 7},
                    "MaxSpawnDelay": {"type": "short", "value": 11},
                    "SpawnCount": {"type": "short", "value": 1},
                    "MaxNearbyEntities": {"type": "short", "value": 6},
                    "RequiredPlayerRange": {"type": "short", "value": 16},
                    "SpawnRange": {"type": "short", "value": 4},
                    "SpawnData": {"type": "compound", "value": pig},
                    "SpawnPotentials": {
                        "type": "list", "element_type": "compound",
                        "value": [
                            {"type": "compound", "value": {
                                "Entity": {"type": "compound",
                                           "value": pig},
                                "Weight": {"type": "int", "value": 1},
                            }},
                            {"type": "compound", "value": {
                                "Entity": {"type": "compound",
                                           "value": zombie},
                                "Weight": {"type": "int", "value": 2},
                            }},
                        ],
                    },
                }},
            }
            changed = save_fork.request(
                port, "spawn_spawner_minecart_locked", {
                    "x": x + 0.5, "y": y + 0.0625, "z": z + 0.5,
                    "nbt": nbt_codec.encode_hex(logic_nbt),
                })
            authoritative = changed.get("authoritative") or {}
            carts = [
                entity for entity in authoritative.get("entities", [])
                if entity.get("type") == "EntityMinecartMobSpawner"
            ]
            if not changed.get("ok") or len(carts) != 1 \
                    or carts[0].get("spawner_delay") != 0 \
                    or carts[0].get("spawner_default_entity_nbt") is not False \
                    or len(carts[0].get("spawner_potentials", [])) != 2:
                raise StageError(
                    f"custom spawner cart was not exposed exactly: {changed}")
            save_fork.capture_locked(port, output)
            contract = {
                "schema": "netherite.completeness_fixture",
                "version": 1,
                "id": "ent07-custom-spawner-minecart-spawn",
                "todo": "ENT-07",
                "fixture": "active-custom-pig-spawner-minecart",
                "paired_boundary": {
                    "left": "Java EntityMinecartMobSpawner save NBT",
                    "right": "native capsule minecart continuation",
                    "same_tick": True,
                },
                "negative_control": {
                    "mutation": "flip custom SpawnData default marker",
                    "expected_path":
                        "$/entities/*/spawner_default_entity_nbt",
                    "before": False,
                    "after": True,
                },
                "horizons": [1, 2, 3],
                "inputs": [{}, {}, {}],
                "comparisons": [
                    {"family": family, "required": True,
                     "minimum_observations": 1}
                    for family in (
                        "nbt", "numeric", "blocks", "light", "order")
                ],
                "position": position,
                "initial": carts[0],
            }
            (output / "fixture_spawner_cart.json").write_text(
                json.dumps(contract, indent=2, sort_keys=True) + "\n")
            return

        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [[x, y, z, 52, 0]],
        })
        authoritative = placed.get("authoritative") or {}
        spawners = authoritative.get("spawners") or []
        if not placed.get("ok") \
                or authoritative.get("spawners_complete") is not True \
                or len(spawners) != 1:
            raise StageError(
                f"Java did not expose one complete spawner: {placed}")
        spawner = spawners[0]
        expected = {
            "x": x, "y": y, "z": z,
            "delay": 20, "min_delay": 200, "max_delay": 800,
            "spawn_count": 4, "max_nearby": 6,
            "activate_range": 16, "spawn_range": 4,
            "entity_id": "minecraft:pig", "default_entity_nbt": True,
        }
        if any(spawner.get(field) != value
               for field, value in expected.items()):
            raise StageError(
                f"Java default spawner state is unexpected: {spawner}")

        if custom:
            pig = {
                "id": {"type": "string", "value": "minecraft:pig"},
                "Motion": {"type": "list", "element_type": "double",
                           "value": [
                               {"type": "double", "value": "0000000000000000"},
                               {"type": "double", "value": "0000000000000000"},
                               {"type": "double", "value": "0000000000000000"},
                           ]},
                "Air": {"type": "short", "value": 123},
                "OnGround": {"type": "byte", "value": 0},
                "Health": {"type": "float", "value": "40c00000"},
                "NoAI": {"type": "byte", "value": 1},
                "Saddle": {"type": "byte", "value": 1},
                "InLove": {"type": "int", "value": 40},
            }
            tile_nbt = {
                "name": "",
                "tag": {"type": "compound", "value": {
                    "id": {"type": "string",
                           "value": "minecraft:mob_spawner"},
                    "x": {"type": "int", "value": x},
                    "y": {"type": "int", "value": y},
                    "z": {"type": "int", "value": z},
                    "Delay": {"type": "short", "value": 0},
                    "MinSpawnDelay": {"type": "short", "value": 7},
                    "MaxSpawnDelay": {"type": "short", "value": 11},
                    "SpawnCount": {"type": "short", "value": 1},
                    "MaxNearbyEntities": {"type": "short", "value": 6},
                    "RequiredPlayerRange": {"type": "short", "value": 16},
                    "SpawnRange": {"type": "short", "value": 4},
                    "SpawnData": {"type": "compound", "value": pig},
                    "SpawnPotentials": {
                        "type": "list", "element_type": "compound",
                        "value": [{"type": "compound", "value": {
                            "Entity": {"type": "compound", "value": pig},
                            "Weight": {"type": "int", "value": 1},
                        }}],
                    },
                }},
            }
            changed = save_fork.request(port, "set_spawner_nbt_locked", {
                "x": x, "y": y, "z": z,
                "nbt": nbt_codec.encode_hex(tile_nbt),
            })
            if not changed.get("ok"):
                raise StageError(f"could not install custom SpawnData: {changed}")
            authoritative = changed.get("authoritative") or {}
            spawners = authoritative.get("spawners") or []
            if len(spawners) != 1 \
                    or spawners[0].get("default_entity_nbt") is not False \
                    or spawners[0].get("delay") != 0:
                raise StageError(
                    f"custom spawner was not exposed exactly: {changed}")
            spawner = spawners[0]
            expected = dict(spawner)

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": ("world06-custom-spawner-spawn" if custom
                   else "world06-active-spawner-countdown"),
            "todo": "WORLD-06",
            "fixture": ("active-custom-pig-spawner" if custom
                        else "active-default-pig-spawner"),
            "paired_boundary": {
                "left": "Java TileEntityMobSpawner save NBT",
                "right": "native capsule spawner continuation",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "replace SpawnData with custom entity NBT",
                "expected_path": "$/spawners[0]/default_entity_nbt",
                "before": True,
                "after": False,
            },
            "horizons": ([1, 2, 3] if custom else [1, 19, 20, 21]),
            "inputs": [{} for _ in range(3 if custom else 21)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in ("nbt", "numeric", "blocks", "light", "order")
            ],
            "position": position,
            "initial": expected,
        }
        (output / "fixture_spawner.json").write_text(
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
    parser.add_argument("--instance", type=int, default=98)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument(
        "--fresh", action="store_true",
        help="stage in a newly generated world instead of reloading SOURCE")
    parser.add_argument(
        "--custom", action="store_true",
        help="replace the default pig choice with bounded custom SpawnData")
    parser.add_argument(
        "--cart", action="store_true",
        help="stage a custom EntityMinecartMobSpawner instead of a block tile")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.fresh, args.custom, args.cart)
    print(f"PASS staged Java mob-spawner boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage spawner fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
