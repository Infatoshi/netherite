#!/usr/bin/env python3
"""Stage arbitrary ItemStack NBT in a real parked Java save snapshot."""

from __future__ import annotations

import argparse
import json
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


def _tag_hex() -> str:
    return nbt_codec.encode_hex({
        "name": "", "tag": {"type": "compound", "value": {
            "RepairCost": {"type": "int", "value": 17},
            "display": {"type": "compound", "value": {
                "Name": {"type": "string", "value": "Save NBT Probe"},
                "Lore": {"type": "list", "element_type": "string", "value": [
                    {"type": "string", "value": "unknown fields remain"},
                    {"type": "string", "value": "\u0000 modified UTF-8"},
                ]},
            }},
            "ench": {"type": "list", "element_type": "compound", "value": [
                {"type": "compound", "value": {
                    "id": {"type": "short", "value": 16},
                    "lvl": {"type": "short", "value": 5},
                }},
            ]},
            "AttributeModifiers": {
                "type": "list", "element_type": "compound", "value": [
                    {"type": "compound", "value": {
                        "AttributeName": {"type": "string",
                                          "value": "generic.attackDamage"},
                        "Name": {"type": "string", "value": "probe"},
                        "Amount": {"type": "double",
                                   "value": "4004000000000000"},
                        "Operation": {"type": "int", "value": 0},
                        "UUIDMost": {"type": "long", "value": 1234},
                        "UUIDLeast": {"type": "long", "value": -5678},
                    }},
                ],
            },
            "Potion": {"type": "string", "value": "minecraft:long_swiftness"},
            "CustomPotionEffects": {
                "type": "list", "element_type": "compound", "value": [
                    {"type": "compound", "value": {
                        "Id": {"type": "byte", "value": 1},
                        "Amplifier": {"type": "byte", "value": 2},
                        "Duration": {"type": "int", "value": 1234},
                        "Ambient": {"type": "byte", "value": 1},
                        "ShowParticles": {"type": "byte", "value": 0},
                    }},
                ],
            },
            "Fireworks": {"type": "compound", "value": {
                "Flight": {"type": "byte", "value": 3},
                "Explosions": {
                    "type": "list", "element_type": "compound", "value": [
                        {"type": "compound", "value": {
                            "Type": {"type": "byte", "value": 4},
                            "Flicker": {"type": "byte", "value": 1},
                            "Trail": {"type": "byte", "value": 1},
                            "Colors": {"type": "int_array",
                                       "value": [0x112233, 0xFFEEDD]},
                            "FadeColors": {"type": "int_array",
                                           "value": [0x010203]},
                        }},
                    ],
                },
            }},
            "Explosion": {"type": "compound", "value": {
                "Type": {"type": "byte", "value": 2},
                "Colors": {"type": "int_array", "value": [0xABCDEF]},
            }},
            "pages": {"type": "list", "element_type": "string", "value": [
                {"type": "string", "value": "{\"text\":\"page one\"}"},
                {"type": "string", "value": "page two"},
            ]},
            "title": {"type": "string", "value": "Probe Book"},
            "author": {"type": "string", "value": "Netherite"},
            "resolved": {"type": "byte", "value": 1},
            "generation": {"type": "int", "value": 2},
            "map": {"type": "int", "value": 32767},
            "Decorations": {
                "type": "list", "element_type": "compound", "value": [
                    {"type": "compound", "value": {
                        "id": {"type": "string", "value": "probe-marker"},
                        "type": {"type": "byte", "value": 1},
                        "x": {"type": "double",
                              "value": "4028800000000000"},
                        "z": {"type": "double",
                              "value": "c01e000000000000"},
                        "rot": {"type": "double",
                                "value": "4056800000000000"},
                    }},
                ],
            },
            "BlockEntityTag": {"type": "compound", "value": {
                "id": {"type": "string", "value": "minecraft:chest"},
                "Items": {
                    "type": "list", "element_type": "compound", "value": [
                        {"type": "compound", "value": {
                            "Slot": {"type": "byte", "value": 0},
                            "id": {"type": "string", "value": "minecraft:stone"},
                            "Count": {"type": "byte", "value": 64},
                            "Damage": {"type": "short", "value": 0},
                        }},
                    ],
                },
            }},
            "ForgeCaps": {"type": "compound", "value": {
                "netherite:opaque": {"type": "compound", "value": {
                    "value": {"type": "long", "value": 9223372036854770000},
                }},
            }},
            "netherite_unknown": {"type": "compound", "value": {
                "byte_min": {"type": "byte", "value": -128},
                "short_min": {"type": "short", "value": -32768},
                "long_value": {"type": "long", "value": -918273645546372819},
                "byte_array": {"type": "byte_array", "value": [-128, 0, 127]},
                "int_array": {"type": "int_array", "value": [
                    -2147483648, 0, 2147483647]},
                "nested": {"type": "list", "element_type": "compound", "value": [
                    {"type": "compound", "value": {
                        "answer": {"type": "int", "value": 42},
                    }},
                ]},
            }},
        }},
    })


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
                response = save_fork.request(port, "obs")
                if response.get("ok") and "x" in response:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise StageError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        boundary = save_fork.request(port, "server_step_lock")
        locked = True
        save_fork.request(port, "normalize_reload_locked")
        authoritative = boundary.get("authoritative") or {}
        x = float(authoritative.get("x", response["x"]))
        y = float(authoritative.get("y", response["y"]))
        z = float(authoritative.get("z", response["z"]))
        tag_hex = _tag_hex()
        player = save_fork.request(port, "setplayer_locked", {
            "inventory_slot": 10, "inventory_item": 1,
            "inventory_count": 7, "inventory_meta": 0,
            "inventory_nbt": tag_hex,
        })
        inventory = player.get("authoritative", {}).get("inventory", [])
        expected_tag = nbt_codec.canonical_hex(tag_hex)
        if not any(row.get("slot") == 10
                   and nbt_codec.canonical_hex(
                       row.get("stack_payload", {}).get("nbt")) == expected_tag
                   for row in inventory):
            raise StageError("Java player ItemStack NBT did not stage exactly")
        dropped = save_fork.request(port, "summon_locked", {
            "type": "item", "x": x + 2.0, "y": y + 1.0, "z": z,
            "item": 1, "count": 5, "meta": 0,
            "pickup_delay": 32767, "nbt": tag_hex,
        })
        entities = dropped.get("authoritative", {}).get("entities", [])
        if not any(row.get("type") == "EntityItem"
                   and nbt_codec.canonical_hex(
                       row.get("stack_payload", {}).get("nbt")) == expected_tag
                   and row.get("item_exact") is True for row in entities):
            raise StageError("Java EntityItem NBT did not stage as exact")
        chest_x = int(x // 1) + 4
        chest_y = int(y // 1)
        chest_z = int(z // 1) + 4
        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [[chest_x, chest_y, chest_z, 54, 2]],
        })
        if not placed.get("ok"):
            raise StageError("Java tagged chest block did not stage")
        chest = save_fork.request(port, "set_container_slot_locked", {
            "x": chest_x, "y": chest_y, "z": chest_z,
            "slot": 0, "item": 1, "count": 9, "meta": 0,
            "nbt": tag_hex,
        })
        containers = chest.get("authoritative", {}).get("containers", [])
        if not any(
                row.get("x") == chest_x and row.get("y") == chest_y
                and row.get("z") == chest_z
                and any(item.get("slot") == 0
                        and nbt_codec.canonical_hex(
                            item.get("stack_payload", {}).get("nbt"))
                            == expected_tag
                        for item in row.get("items", []))
                for row in containers):
            raise StageError("Java chest ItemStack NBT did not stage exactly")
        save_fork.capture_locked(port, output)
        (output / "fixture_item_tag.json").write_text(json.dumps({
            "nbt": tag_hex,
            "inventory_slot": 10,
            "entity_id": dropped["eid"],
            "chest": [chest_x, chest_y, chest_z],
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
    parser.add_argument("--instance", type=int, default=22)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance, args.seed)
    print(f"PASS staged real Java arbitrary ItemStack NBT -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError, nbt_codec.NbtError) as exc:
        print(f"FAIL stage ItemStack fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
