#!/usr/bin/env python3
"""Exact native save/reload continuation gate.

The world column store and runtime checkpoint deliberately have separate wire
formats.  This test writes both at one locked boundary, continues the source
process, restores them into a fresh process, and compares every observable
state row plus the raw world surfaces after the same future ticks.
"""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MAGMA = ROOT / "magma"
GAME = MAGMA / "magma_game"
sys.path.insert(0, str(ROOT / "magma" / "trace"))
import nbt_codec  # noqa: E402


class NativeCheckpointError(RuntimeError):
    pass


def _write_events(path: pathlib.Path, events: list[dict[str, object]]) -> None:
    path.write_text("".join(
        json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n"
        for event in events))


def _run(
    root: pathlib.Path, name: str, events: list[dict[str, object]],
    ticks: int = 24,
) -> tuple[subprocess.CompletedProcess[str], dict[str, bytes]]:
    script = root / f"{name}.jsonl"
    _write_events(script, events)
    outputs = {
        "state": root / f"{name}.state.jsonl",
        "blocks": root / f"{name}.blocks.u16le",
        "sky": root / f"{name}.sky.u8",
        "block_light": root / f"{name}.block_light.u8",
        "biomes": root / f"{name}.biomes.u8",
        "heights": root / f"{name}.heights.u16le",
    }
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(root),
        "MAGMA_NATIVE_SAVE_DIR": str(root),
        "MAGMA_BLOCKS_OUT": str(outputs["blocks"]),
        "MAGMA_SKY_LIGHT_OUT": str(outputs["sky"]),
        "MAGMA_BLOCK_LIGHT_OUT": str(outputs["block_light"]),
        "MAGMA_BIOMES_OUT": str(outputs["biomes"]),
        "MAGMA_HEIGHTS_OUT": str(outputs["heights"]),
        "MAGMA_BLOCKS_BOX": "-2,0,-2,18,90,18",
    })
    result = subprocess.run([
        str(GAME), "--world", "superflat", "--seed", "771109",
        "--headless", "--ticks", str(ticks), "--view-distance", "1",
        "--mobs", "off", "--script", str(script),
        "--state-out", str(outputs["state"]), "--render", "off",
        "--pace", "unlimited",
    ], cwd=MAGMA, env=environment, stdout=subprocess.PIPE,
       stderr=subprocess.STDOUT, text=True, check=False)
    materialized = {
        key: path.read_bytes() for key, path in outputs.items()
        if path.exists()
    }
    return result, materialized


def _setup_events() -> list[dict[str, object]]:
    return [
        {"tick": 0, "type": "restore_player_statistics",
         "file": "player_statistics.json", "play_one_minute": 7,
         "time_since_death": 11},
        {"tick": 0, "type": "snapshot_region", "dim": 0,
         "cx": 0, "cz": 0, "radius": 1},
        {"tick": 0, "type": "set_pose_state", "x": 1.25, "y": 5.0,
         "z": 1.75, "yaw": 32.5, "pitch": -11.25,
         "vx": 0.03125, "vy": 0.0, "vz": -0.015625,
         "on_ground": 1, "fall": 0.0},
        {"tick": 0, "type": "set_vitals", "health": 17.0, "food": 16},
        {"tick": 0, "type": "set_time", "value": 7312},
        {"tick": 0, "type": "set_total_time", "value": 99123},
        {"tick": 0, "type": "set_inventory", "slot": 0,
         "item": 276, "count": 1, "meta": 47, "repair_cost": 3,
         "custom_name": "Checkpoint Blade", "n_ench": 1,
         "e0": (16 << 16) | 2, "nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "set_selected_slot", "slot": 0},
        {"tick": 0, "type": "set_block", "x": 4, "y": 4, "z": 4,
         "id": 57, "meta": 0},
        {"tick": 0, "type": "set_block", "x": 17, "y": 5, "z": 1,
         "id": 89, "meta": 0},
        {"tick": 0, "type": "set_block", "x": 5, "y": 4, "z": 4,
         "id": 54, "meta": 0},
        {"tick": 0, "type": "set_chest_slot", "dim": 0,
         "x": 5, "y": 4, "z": 4, "slot": 0,
         "item": 1, "count": 11, "meta": 0,
         "stack_nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "set_block", "x": 6, "y": 4, "z": 4,
         "id": 61, "meta": 2},
        {"tick": 0, "type": "set_furnace_slot", "dim": 0,
         "x": 6, "y": 4, "z": 4, "slot": 0,
         "item": 15, "count": 2, "meta": 0,
         "burn_time": 0, "current_burn_time": 0,
         "cook_time": 0, "total_cook_time": 200,
         "custom_name": "Checkpoint Furnace",
         "stack_nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "set_block", "x": 7, "y": 4, "z": 4,
         "id": 23, "meta": 2},
        {"tick": 0, "type": "set_static_container_slot", "dim": 0,
         "x": 7, "y": 4, "z": 4, "slot": 0,
         "item": 4, "count": 9, "meta": 0,
         "stack_nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "schedule_tick", "x": 5, "y": 4, "z": 5,
         "block": 1, "time": 99200, "priority": 2, "order": 81},
        # Restore World.loadedEntityList identity before materializing the
        # payloads. Fixture spawn helpers append only when the row is absent,
        # which preserves this captured order instead of their staging order.
        {"tick": 0, "type": "restore_loaded_entity_order",
         "order": 0, "eid": 4502},
        {"tick": 0, "type": "restore_loaded_entity_order",
         "order": 1, "eid": 4501},
        {"tick": 0, "type": "restore_loaded_entity_order",
         "order": 2, "eid": 4401},
        {"tick": 0, "type": "spawn_item_state_fixture", "eid": 4401,
         "x": 3.5, "y": 5.25, "z": 3.5,
         "vx": 0.0125, "vy": 0.08, "vz": -0.00625,
         "yaw": 0.0, "hover_start": 1.25,
         "item": 264, "count": 3, "meta": 0, "age": 41,
         "ticks_existed": 19, "pickup_delay": 80,
         "health": 5, "lifespan": 6000, "on_ground": 0,
         "no_gravity": 0, "fire": -1, "in_water": 0,
         "first_update": 0, "entity_seed48": 0x123456789ABC,
         "nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "restore_item_entity_uuid", "eid": 4401,
         "most": -7712985251953070000, "least": 20737},
        {"tick": 0, "type": "spawn_minecart_fixture", "kind": 1,
         "eid": 4501, "x": 8.5, "y": 5.0, "z": 8.5,
         "vx": 0.0, "vy": 0.0, "vz": 0.0,
         "yaw": 0.0, "pitch": 0.0, "reverse": 0,
         "rolling_amplitude": 0, "rolling_direction": 1,
         "damage": 0.0, "fuel": 0, "push_x": 0.0, "push_z": 0.0,
         "tnt_fuse": -1, "hopper_enabled": 1,
         "transfer_cooldown": -1,
         "entity_seed48": 0x223456789ABC,
         "entity_have_gaussian": 0, "entity_gaussian": 0.0},
        {"tick": 0, "type": "set_minecart_slot", "eid": 4501,
         "slot": 3, "item": 5, "count": 6, "meta": 2,
         "stack_nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "spawn_minecart_fixture", "kind": 0,
         "eid": 4502, "x": 1.25, "y": 5.35, "z": 1.75,
         "vx": 0.0, "vy": 0.0, "vz": 0.0,
         "yaw": 0.0, "pitch": 0.0, "reverse": 0,
         "rolling_amplitude": 0, "rolling_direction": 1,
         "damage": 0.0, "fuel": 0, "push_x": 0.0, "push_z": 0.0,
         "tnt_fuse": -1, "hopper_enabled": 1,
         "transfer_cooldown": -1,
         "entity_seed48": 0x323456789ABC,
         "entity_have_gaussian": 0, "entity_gaussian": 0.0},
        {"tick": 0, "type": "restore_minecart_uuid", "eid": 4502,
         "most": -7712985251953070001, "least": 20994},
        {"tick": 0, "type": "spawn_firework_state_fixture", "eid": 4601,
         "x": 12.5, "y": 10.0, "z": 12.5,
         "vx": 0.03125, "vy": 0.125, "vz": -0.0625,
         "yaw": 14.0, "pitch": -9.0,
         "prev_yaw": 12.0, "prev_pitch": -7.0,
         "age": 4, "lifetime": 80, "ticks_existed": 9,
         "attached_player": 0, "flight": 2, "explosion_count": 4,
         "large_blast": 1, "twinkle": 1,
         "firework_item_present": 1, "firework_item": 401,
         "firework_count": 1, "firework_meta": 0,
         "entity_seed48": 0x423456789ABC,
         "entity_have_gaussian": 0, "entity_gaussian": 0.0,
         "nbt_file": "item_tag_a.nbt"},
        {"tick": 0, "type": "restore_transient_entity_uuid", "eid": 4601,
         "most": -7712985251953070002, "least": 21251},
        {"tick": 0, "type": "restore_player_riding", "eid": 4502},
        {"tick": 0, "type": "restore_loaded_tile_order",
         "order": 0, "x": 7, "y": 4, "z": 4},
        {"tick": 0, "type": "restore_loaded_tile_order",
         "order": 1, "x": 6, "y": 4, "z": 4},
        {"tick": 0, "type": "restore_loaded_tile_order",
         "order": 2, "x": 5, "y": 4, "z": 4},
        {"tick": 0, "type": "restore_tickable_tile_order",
         "order": 0, "x": 6, "y": 4, "z": 4},
        {"tick": 0, "type": "restore_tickable_tile_order",
         "order": 1, "x": 5, "y": 4, "z": 4},
        {"tick": 0, "type": "write_chunk_store",
         "file": "world_dim0.bin", "dim": 0},
        {"tick": 0, "type": "write_runtime_checkpoint",
         "file": "runtime.bin"},
    ]


def _reload_events(
    runtime_file: str = "runtime.bin", world_file: str = "world_dim0.bin",
) -> list[dict[str, object]]:
    return [
        {"tick": 0, "type": "attach_chunk_store",
         "file": world_file, "dim": 0},
        {"tick": 0, "type": "load_runtime_checkpoint", "file": runtime_file},
    ]


def _require_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise NativeCheckpointError(
            f"{label} failed (rc={result.returncode}):\n{result.stdout}")


def _inventory_count(state: dict[str, object], item_id: int) -> int:
    inventory = state.get("inventory")
    if not isinstance(inventory, list):
        raise NativeCheckpointError("state has no inventory array")
    return sum(
        int(row.get("count", 0))
        for row in inventory
        if isinstance(row, dict)
        and row.get("id", row.get("item")) == item_id
    )


def _entity_count(state: dict[str, object], kind: str) -> int:
    entities = state.get("entities")
    if not isinstance(entities, list):
        raise NativeCheckpointError("state has no entities array")
    return sum(
        1 for row in entities
        if isinstance(row, dict) and row.get("kind") == kind
    )


def main() -> int:
    if not GAME.exists():
        raise NativeCheckpointError(f"missing {GAME}; run make -C magma game")
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="netherite-native-checkpoint-",
            dir=temporary_root) as raw:
        root = pathlib.Path(raw)
        statistics = {
            "stat.playOneMinute": 7,
            "stat.timeSinceDeath": 11,
            "achievement.exploreAllBiomes": {
                "value": 0, "progress": ["Plains", "Desert"],
            },
            "netherite.unknown": {"nested": [1, True, "kept"]},
        }
        (root / "player_statistics.json").write_text(
            json.dumps(statistics, separators=(",", ":")))
        (root / "item_tag_a.nbt").write_bytes(nbt_codec.encode({
            "name": "", "tag": {"type": "compound", "value": {
                "RepairCost": {"type": "int", "value": 3},
                "netherite_unknown": {"type": "compound", "value": {
                    "signed": {"type": "long", "value": -918273645},
                    "bytes": {"type": "byte_array", "value": [-128, 0, 127]},
                    "nested": {"type": "list", "element_type": "string",
                               "value": [
                                   {"type": "string", "value": "alpha"},
                                   {"type": "string", "value": "beta"},
                               ]},
                }},
            }},
        }))
        source_result, source = _run(root, "source", _setup_events())
        _require_success(source_result, "source continuation")
        first_state = json.loads(source["state"].splitlines()[0])
        expected_payload = {
            "kind": "item_tag", "nbt": (root / "item_tag_a.nbt").read_bytes().hex(),
        }
        inventory_zero = next(
            row for row in first_state["inventory"] if row["slot"] == 0)
        entity_4401 = next(
            row for row in first_state["entities"] if row["eid"] == 4401)
        if inventory_zero.get("stack_payload") != expected_payload \
                or entity_4401.get("stack_payload") != expected_payload:
            raise NativeCheckpointError(
                "arbitrary ItemStack/EntityItem NBT was not emitted losslessly")
        if entity_4401.get("uuid_most") != -7712985251953070000 \
                or entity_4401.get("uuid_least") != 20737:
            raise NativeCheckpointError(
                "EntityItem UUID was not emitted losslessly")
        minecart_4501 = next(
            row for row in first_state["entities"] if row["eid"] == 4501)
        minecart_stack = next(
            row for row in minecart_4501["items"] if row["slot"] == 3)
        if minecart_stack.get("id") != 5 \
                or minecart_stack.get("stack_payload") != expected_payload:
            raise NativeCheckpointError(
                "minecart ItemStack NBT was not emitted losslessly")
        ridden_cart = next(
            row for row in first_state["entities"] if row["eid"] == 4502)
        if first_state.get("player_riding_eid") != 4502 \
                or ridden_cart.get("uuid_most") \
                    != -7712985251953070001 \
                or ridden_cart.get("uuid_least") != 20994:
            raise NativeCheckpointError(
                "player/minecart relationship or UUID was not retained")
        if first_state.get("loaded_entity_order") \
                != [4502, 4501, 4401, 4601] \
                or first_state.get("loaded_tile_order") != [
                    [7, 4, 4], [6, 4, 4], [5, 4, 4]] \
                or first_state.get("tickable_tile_order") != [
                    [6, 4, 4], [5, 4, 4]]:
            raise NativeCheckpointError(
                "explicit loaded entity/tile order was not retained")
        expected_container_items = {(5, 4, 4): 1, (6, 4, 4): 15,
                                    (7, 4, 4): 4}
        for position, item_id in expected_container_items.items():
            container = next(
                row for row in first_state["containers"]
                if (row["x"], row["y"], row["z"]) == position)
            stack = next(row for row in container["items"] if row["slot"] == 0)
            if stack.get("id") != item_id \
                    or stack.get("stack_payload") != expected_payload:
                raise NativeCheckpointError(
                    f"container ItemStack NBT was lost at {position}")
            if position == (6, 4, 4) \
                    and container.get("custom_name") \
                        != "Checkpoint Furnace":
                raise NativeCheckpointError(
                    "furnace custom name was not checkpointed losslessly")
        for required in ("runtime.bin", "world_dim0.bin"):
            if not (root / required).is_file() or not (root / required).stat().st_size:
                raise NativeCheckpointError(f"save did not create {required}")

        reload_result, reloaded = _run(root, "reloaded", _reload_events())
        _require_success(reload_result, "reloaded continuation")
        if source.keys() != reloaded.keys():
            raise NativeCheckpointError(
                f"output set differs: {sorted(source)} != {sorted(reloaded)}")
        for surface in source:
            if source[surface] != reloaded[surface]:
                raise NativeCheckpointError(
                    f"{surface} differs across save/reload continuation")

        # Java 1.11.2 deliberately does not persist EntityLivingBase's active
        # ItemStack, remaining use count, or HAND_STATES data parameter. Prove
        # the two important consequences at a real disk boundary: an existing
        # bow charge cannot fire after reload, and eating restarts from tick
        # zero instead of completing from the pre-save partial duration.
        bow_events: list[dict[str, object]] = [
            {"tick": 0, "type": "set_inventory", "slot": 0,
             "item": 261, "count": 1, "meta": 0},
            {"tick": 0, "type": "set_inventory", "slot": 1,
             "item": 262, "count": 2, "meta": 0},
            {"tick": 0, "type": "set_selected_slot", "slot": 0},
        ]
        bow_events.extend(
            {"tick": tick, "type": "action", "use": 1}
            for tick in range(7)
        )
        bow_events.extend([
            {"tick": 7, "type": "write_chunk_store",
             "file": "bow_world.bin", "dim": 0},
            {"tick": 7, "type": "write_runtime_checkpoint",
             "file": "bow_runtime.bin"},
        ])
        bow_source_result, bow_source = _run(
            root, "bow_source", bow_events, ticks=8)
        _require_success(bow_source_result, "mid-draw bow source")
        bow_source_rows = [
            json.loads(line) for line in bow_source["state"].splitlines()
        ]
        if _entity_count(bow_source_rows[7], "projectile") != 1 \
                or _inventory_count(bow_source_rows[7], 262) != 1:
            raise NativeCheckpointError(
                "bow negative control did not release the saved seven-tick draw")
        bow_reload_result, bow_reload = _run(
            root, "bow_reload",
            _reload_events("bow_runtime.bin", "bow_world.bin"), ticks=1)
        _require_success(bow_reload_result, "mid-draw bow reload")
        bow_reload_state = json.loads(bow_reload["state"].splitlines()[0])
        if _entity_count(bow_reload_state, "projectile") != 0 \
                or _inventory_count(bow_reload_state, 262) != 2:
            raise NativeCheckpointError(
                "reload retained a bow draw that Java player NBT cancels")

        eat_events: list[dict[str, object]] = [
            {"tick": 0, "type": "set_vitals", "health": 20.0, "food": 10},
            {"tick": 0, "type": "set_inventory", "slot": 0,
             "item": 260, "count": 1, "meta": 0},
            {"tick": 0, "type": "set_selected_slot", "slot": 0},
        ]
        eat_events.extend(
            {"tick": tick, "type": "action", "use": 1}
            for tick in range(32)
        )
        eat_events.extend([
            {"tick": 10, "type": "write_chunk_store",
             "file": "eat_world.bin", "dim": 0},
            {"tick": 10, "type": "write_runtime_checkpoint",
             "file": "eat_runtime.bin"},
        ])
        eat_events.sort(key=lambda event: int(event["tick"]))
        eat_source_result, eat_source = _run(
            root, "eat_source", eat_events, ticks=32)
        _require_success(eat_source_result, "mid-eat source")
        eat_source_state = json.loads(eat_source["state"].splitlines()[-1])
        if _inventory_count(eat_source_state, 260) != 0 \
                or int(eat_source_state.get("food", 0)) <= 10:
            raise NativeCheckpointError(
                "eating negative control did not finish after 32 held ticks")
        eat_reload_events = _reload_events(
            "eat_runtime.bin", "eat_world.bin")
        eat_reload_events.extend(
            {"tick": tick, "type": "action", "use": 1}
            for tick in range(22)
        )
        eat_reload_result, eat_reload = _run(
            root, "eat_reload", eat_reload_events, ticks=22)
        _require_success(eat_reload_result, "mid-eat reload")
        eat_reload_state = json.loads(eat_reload["state"].splitlines()[-1])
        if _inventory_count(eat_reload_state, 260) != 1 \
                or int(eat_reload_state.get("food", 0)) != 10:
            raise NativeCheckpointError(
                "reload retained eating progress that Java player NBT cancels")

        # The writer is deterministic at an identical loaded boundary.
        repeat_events = _reload_events() + [
            {"tick": 0, "type": "write_runtime_checkpoint",
             "file": "runtime_repeat.bin"},
            {"tick": 0, "type": "write_chunk_store",
             "file": "world_repeat.bin", "dim": 0},
        ]
        repeat_result, _ = _run(root, "repeat", repeat_events, ticks=1)
        _require_success(repeat_result, "repeat serialization")
        if (root / "runtime.bin").read_bytes() != \
                (root / "runtime_repeat.bin").read_bytes():
            raise NativeCheckpointError("runtime checkpoint is not deterministic")
        if (root / "world_dim0.bin").read_bytes() != \
                (root / "world_repeat.bin").read_bytes():
            raise NativeCheckpointError("world checkpoint is not deterministic")

        # Empty generated worlds are valid saves too. This exercises the
        # zero-index cold-store case, independent of the edited source world.
        empty_result, _ = _run(root, "empty", [
            {"tick": 0, "type": "write_chunk_store",
             "file": "empty_world.bin", "dim": 0},
            {"tick": 0, "type": "write_runtime_checkpoint",
             "file": "empty_runtime.bin"},
        ], ticks=1)
        _require_success(empty_result, "empty-world save")
        empty_reload, _ = _run(root, "empty_reload", [
            {"tick": 0, "type": "attach_chunk_store",
             "file": "empty_world.bin", "dim": 0},
            {"tick": 0, "type": "load_runtime_checkpoint",
             "file": "empty_runtime.bin"},
        ], ticks=1)
        _require_success(empty_reload, "empty-world reload")

        # Both truncation and a bit flip must fail before mutating the runtime.
        original = (root / "runtime.bin").read_bytes()
        (root / "runtime_truncated.bin").write_bytes(original[:-1])
        flipped = bytearray(original)
        flipped[len(flipped) // 2] ^= 0x80
        (root / "runtime_flipped.bin").write_bytes(flipped)
        for corrupt in ("runtime_truncated.bin", "runtime_flipped.bin"):
            bad_result, _ = _run(
                root, corrupt.removesuffix(".bin"), _reload_events(corrupt),
                ticks=1)
            if bad_result.returncode == 0 \
                    or "invalid load_runtime_checkpoint" not in bad_result.stdout:
                raise NativeCheckpointError(
                    f"corrupt checkpoint did not fail closed: {corrupt}")

        # Make accidental dependence on one pathname apparent in failures.
        shutil.copyfile(root / "runtime.bin", root / "runtime_copy.bin")
        copy_result, copy_state = _run(
            root, "copy", _reload_events("runtime_copy.bin"))
        _require_success(copy_result, "copied checkpoint")
        if copy_state != source:
            raise NativeCheckpointError("checkpoint behavior depends on pathname")

        # The outward statistics writer updates the two vanilla per-player
        # clocks and retains every unknown/achievement JSON value. The event at
        # loop tick 3 observes three completed continuation ticks.
        stats_write_result, _ = _run(root, "stats_write", _reload_events() + [
            {"tick": 3, "type": "write_player_statistics",
             "file": "statistics_written.json"},
        ], ticks=4)
        _require_success(stats_write_result, "statistics write")
        written_statistics = json.loads(
            (root / "statistics_written.json").read_text())
        if written_statistics.get("stat.playOneMinute") != 10 \
                or written_statistics.get("stat.timeSinceDeath") != 14 \
                or written_statistics.get("achievement.exploreAllBiomes") \
                    != statistics["achievement.exploreAllBiomes"] \
                or written_statistics.get("netherite.unknown") \
                    != statistics["netherite.unknown"]:
            raise NativeCheckpointError(
                "statistics writer changed opaque values or counter timing")

        # Zero-valued counters may be absent from a fresh Java JSON file. They
        # must be inserted, and malformed payloads must fail at restore.
        (root / "empty_statistics.json").write_text(
            '{"achievement.openInventory":{"value":0}}')
        empty_stats_result, _ = _run(root, "empty_stats", [
            {"tick": 0, "type": "restore_player_statistics",
             "file": "empty_statistics.json", "play_one_minute": 0,
             "time_since_death": 0},
            {"tick": 0, "type": "write_player_statistics",
             "file": "empty_statistics_written.json"},
        ], ticks=1)
        _require_success(empty_stats_result, "empty statistics write")
        empty_written = json.loads(
            (root / "empty_statistics_written.json").read_text())
        if empty_written.get("stat.playOneMinute") != 0 \
                or empty_written.get("stat.timeSinceDeath") != 0:
            raise NativeCheckpointError(
                "statistics writer did not insert absent counters")
        (root / "malformed_statistics.json").write_text('{"broken":]')
        malformed_result, _ = _run(root, "malformed_stats", [
            {"tick": 0, "type": "restore_player_statistics",
             "file": "malformed_statistics.json", "play_one_minute": 0,
             "time_since_death": 0},
        ], ticks=1)
        if malformed_result.returncode == 0 \
                or "invalid restore_player_statistics" \
                    not in malformed_result.stdout:
            raise NativeCheckpointError(
                "malformed statistics JSON did not fail closed")

        duplicate_order_result, _ = _run(root, "duplicate_order", [
            {"tick": 0, "type": "restore_loaded_entity_order",
             "order": 0, "eid": 7001},
            {"tick": 0, "type": "restore_loaded_entity_order",
             "order": 1, "eid": 7001},
        ], ticks=1)
        if duplicate_order_result.returncode == 0 \
                or "invalid restore_loaded_entity_order" \
                    not in duplicate_order_result.stdout:
            raise NativeCheckpointError(
                "duplicate loaded-entity order did not fail closed")

        # Repeat the fork from a nonzero runtime tick and feed an identical
        # future input/edit sequence to both branches. Source loop tick 7 and
        # reloaded loop tick 0 enter simulation with runtime tick 7, so state
        # row 7 on the source aligns with row 0 after reload.
        mid_source_events = _setup_events()
        for event in mid_source_events:
            if event["type"] == "write_chunk_store":
                event.update(tick=7, file="mid_world.bin")
            elif event["type"] == "write_runtime_checkpoint":
                event.update(tick=7, file="mid_runtime.bin")
        mid_source_events.extend([
            {"tick": 8, "type": "action", "forward": 0.75,
             "strafe": -0.25, "sprint": 1},
            {"tick": 9, "type": "action", "forward": 0.5,
             "strafe": 0.125, "dyaw": 3.25},
            {"tick": 10, "type": "set_block", "x": 6, "y": 4, "z": 6,
             "id": 41, "meta": 0},
        ])
        mid_source_result, mid_source = _run(
            root, "mid_source", mid_source_events, ticks=18)
        _require_success(mid_source_result, "mid-run source continuation")
        mid_reload_events = _reload_events(
            "mid_runtime.bin", "mid_world.bin") + [
            {"tick": 1, "type": "action", "forward": 0.75,
             "strafe": -0.25, "sprint": 1},
            {"tick": 2, "type": "action", "forward": 0.5,
             "strafe": 0.125, "dyaw": 3.25},
            {"tick": 3, "type": "set_block", "x": 6, "y": 4, "z": 6,
             "id": 41, "meta": 0},
        ]
        mid_reload_result, mid_reload = _run(
            root, "mid_reload", mid_reload_events, ticks=11)
        _require_success(mid_reload_result, "mid-run reloaded continuation")
        source_rows = mid_source.pop("state").splitlines(keepends=True)[7:]
        reload_rows = mid_reload.pop("state").splitlines(keepends=True)
        if source_rows != reload_rows:
            raise NativeCheckpointError(
                "nonzero-tick state differs under future input sequence")
        if mid_source != mid_reload:
            raise NativeCheckpointError(
                "nonzero-tick raw world differs under future input sequence")

    print(
        "PASS native checkpoint: exact t0/nonzero continuation, future inputs, "
        "state/world, Java-correct eating/bow cancellation, "
        "deterministic serialization, empty world, "
        "statistics round-trip, corrupt fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (NativeCheckpointError, OSError, ValueError) as exc:
        print(f"FAIL native checkpoint: {exc}", file=os.sys.stderr)
        raise SystemExit(1)
