#!/usr/bin/env python3
"""Cold-reload an S0 and advance exact Java/native comparison horizons."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import struct
import subprocess
import sys
import time
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START_ORACLE = ROOT / "java" / "start_oracle_instance.sh"
DEFAULT_HORIZONS = (1, 20, 200)
MAGMA_GAME = ROOT / "magma" / "magma_game"

sys.path.insert(0, str(HERE))
try:
    import anvil_semantic
    import anvil_to_capsule
    import fixture_contract
    import save_fork
finally:
    sys.path.pop(0)
sys.path.insert(0, str(ROOT / "magma" / "trace"))
try:
    import nbt_codec
    import state_capsule
finally:
    sys.path.pop(0)


class ForkRunnerError(RuntimeError):
    pass


def _actions(path: pathlib.Path | None, maximum: int) -> list[dict[str, Any]]:
    if path is None:
        return [{} for _ in range(maximum)]
    rows = []
    try:
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if not line.strip():
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ForkRunnerError(f"input line {number} is not an object")
            rows.append(value)
    except (OSError, json.JSONDecodeError) as exc:
        raise ForkRunnerError(f"invalid input sequence: {exc}") from exc
    if len(rows) < maximum:
        raise ForkRunnerError(
            f"input sequence has {len(rows)} actions, needs {maximum}")
    return rows[:maximum]


def _oracle_env(
    run_root: pathlib.Path,
    save_dir: pathlib.Path,
    username: str,
    world_type: str,
) -> dict[str, str]:
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(save_dir),
        "ORACLE_POOL_USERNAME": username,
        "ORACLE_POOL_WORLD_TYPE": world_type,
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    return environment


def _oracle_command(
    action: str, instance: int, seed: int, environment: dict[str, str]
) -> None:
    command = ["bash", str(START_ORACLE), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise ForkRunnerError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _capture_java_raw(
    port: int, snapshot: pathlib.Path, box: list[int],
) -> dict[str, pathlib.Path]:
    """Capture live parked block/light state, not lossy Anvil metadata."""
    x0, y0, z0, x1, y1, z1 = box
    paths = {
        "blocks": snapshot / "live_blocks.u16le",
        "sky_light": snapshot / "live_sky_light.u8",
        "block_light": snapshot / "live_block_light.u8",
    }
    commands = {
        "blocks": "getblocks_locked",
        "sky_light": "getskylight_locked",
        "block_light": "getblocklight_locked",
    }
    for surface, command in commands.items():
        result = save_fork.request(port, command, {
            "x0": x0, "y0": y0, "z0": z0,
            "x1": x1, "y1": y1, "z1": z1,
            "file": str(paths[surface]),
        })
        if not result.get("ok") or not paths[surface].is_file():
            raise ForkRunnerError(
                f"could not capture live Java {surface}: {result}")
    return paths


def _run_java(
    source: pathlib.Path,
    output: pathlib.Path,
    instance: int,
    seed: int,
    username: str,
    world_type: str,
    horizons: tuple[int, ...],
    actions: list[dict[str, Any]],
    restore_hidden: dict[str, Any] | None = None,
    raw_box: list[int] | None = None,
) -> dict[str, Any]:
    run_root = output / "oracle"
    environment = _oracle_env(run_root, source / "save", username, world_type)
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    trace: list[dict[str, Any]] = []
    snapshots: dict[int, pathlib.Path] = {}
    raw_snapshots: dict[int, dict[str, pathlib.Path]] = {}
    hidden_snapshots: dict[int, dict[str, Any]] = {}
    try:
        _oracle_command("start", instance, seed, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                ready = save_fork.request(port, "obs")
                if ready.get("ok") and "x" in ready:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise ForkRunnerError("cold Java reload did not produce a player")
            time.sleep(0.1)
        client_lock = save_fork.request(
            port, "step_lock", {"wait_ms": 60000})
        if client_lock.get("wait_ms") != 60000:
            raise ForkRunnerError("could not arm the client lockstep window")
        boundary = save_fork.request(port, "server_step_lock")
        locked = True
        if not boundary.get("authoritative"):
            raise ForkRunnerError("cold Java reload has no authoritative t=0")
        normalized = save_fork.request(port, "normalize_reload_locked")
        (output / "normalized_pre_save.json").write_text(
            json.dumps(normalized, indent=2, sort_keys=True) + "\n")
        if normalized.get("players") != 1 or normalized.get("watched_chunks", 0) < 1:
            raise ForkRunnerError(
                f"cold Java reload watcher normalization failed: {normalized}")
        if restore_hidden is not None:
            restored = save_fork.request(
                port, "restore_hidden_state_locked", restore_hidden)
            if not restored.get("authoritative"):
                raise ForkRunnerError("hidden cursor restore returned no state")
            boundary = restored
        hidden = save_fork.request(port, "hidden_state_locked")
        hidden_snapshots[0] = hidden
        (output / "hidden_state.json").write_text(
            json.dumps(hidden, indent=2, sort_keys=True) + "\n")
        isolated = save_fork.request(
            port, "isolate_server_globals_locked", hidden)
        if isolated.get("next_entity_id", 0) < 1:
            raise ForkRunnerError("could not isolate shared Java RNG/entity cursors")
        boundary = save_fork.request(port, "authoritative_state_locked")
        if not boundary.get("authoritative"):
            raise ForkRunnerError("could not refresh normalized t=0 state")
        trace.append({"tick": 0, "authoritative": boundary["authoritative"]})
        t0 = output / "t000"
        save_fork.capture_locked(port, t0)
        snapshots[0] = t0
        box = raw_box if raw_box is not None else anvil_to_capsule._default_box(t0)
        raw_snapshots[0] = _capture_java_raw(port, t0, box)
        # saveAllChunks marks non-watched provider chunks for unload. The
        # native fork must restore topology after that real save boundary,
        # not the normalization topology from just before it.
        normalized = save_fork.request(port, "chunk_topology_locked")
        if normalized.get("players") != 1 \
                or normalized.get("watched_chunks", 0) < 1:
            raise ForkRunnerError(
                f"post-save Java chunk topology failed: {normalized}")
        (output / "normalized_reload.json").write_text(
            json.dumps(normalized, indent=2, sort_keys=True) + "\n")
        for tick in range(1, max(horizons) + 1):
            result = save_fork.request(
                port,
                "server_tick_locked" if not actions[tick - 1] else "step",
                None if not actions[tick - 1] else actions[tick - 1],
            )
            authoritative = result.get("authoritative")
            if not authoritative:
                raise ForkRunnerError(
                    f"Java step {tick} has no parked authoritative state")
            trace.append({"tick": tick, "authoritative": authoritative})
            if tick in horizons:
                snapshot = output / f"t{tick:03d}"
                save_fork.capture_locked(port, snapshot)
                snapshots[tick] = snapshot
                raw_snapshots[tick] = _capture_java_raw(port, snapshot, box)
                checkpoint_hidden = save_fork.request(
                    port, "hidden_state_locked")
                hidden_snapshots[tick] = checkpoint_hidden
                (snapshot / "hidden_state.json").write_text(
                    json.dumps(checkpoint_hidden, indent=2, sort_keys=True)
                    + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception as exc:
                print(f"warning: could not unlock fork oracle: {exc}", file=sys.stderr)
        if started:
            try:
                _oracle_command("stop", instance, seed, environment)
            except ForkRunnerError as exc:
                print(f"warning: could not stop fork oracle: {exc}", file=sys.stderr)
    (output / "authoritative_trace.jsonl").write_text("".join(
        json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n"
        for row in trace
    ))
    return {
        "trace": trace,
        "hidden": hidden,
        "normalized": normalized,
        "hidden_snapshots": hidden_snapshots,
        "snapshots": {tick: path for tick, path in snapshots.items()},
        "raw_snapshots": raw_snapshots,
        "box": box,
    }


def _native_capability(
    java_run: dict[str, Any], output: pathlib.Path,
    horizons: tuple[int, ...], actions: list[dict[str, Any]],
    world_type: str,
    raw_box: list[int] | None = None,
) -> dict[str, Any]:
    t0 = java_run["snapshots"][0]
    native_import = output / "native_import"
    box = raw_box if raw_box is not None else anvil_to_capsule._default_box(t0)
    report = anvil_to_capsule.import_snapshot(
        t0, native_import, box, bounded=True,
        normalized=java_run["normalized"])
    world_probe = _native_world_probe(native_import, report, output)
    rejected = report["limitations"]
    capsule = report.get("native_capsule")
    if not isinstance(capsule, dict) or capsule.get("status") != "exact":
        return {
            "status": "rejected",
            "rejected_fields": len(rejected),
            "first": rejected[0] if rejected else capsule,
            "world_probe": world_probe,
            "import_report": str(native_import / anvil_to_capsule.REPORT_FILE),
        }
    native = _run_native(
        java_run, native_import, report, output, horizons, actions, world_type)
    native.update({
        "rejected_fields": len(rejected),
        "capsule": str(native_import / "capsule"),
        "world_probe": world_probe,
        "import_report": str(native_import / anvil_to_capsule.REPORT_FILE),
    })
    if rejected:
        capability_first = dict(rejected[0])
        capability_first["tick"] = 0
        native["represented_first"] = native.get("first")
        native["first"] = capability_first
        native["status"] = "diverged"
    return native


_ACTION_FIELDS = {
    "forward", "strafe", "dyaw", "dpitch", "jump", "sneak", "sprint",
    "attack", "use", "do_break", "do_place", "hotbar",
    "close_container", "inv_slot", "inv_button", "inv_type",
}


def _native_script(
    restore: pathlib.Path, destination: pathlib.Path,
    actions: list[dict[str, Any]], ticks: int,
) -> None:
    lines = restore.read_text().splitlines()
    for index, action in enumerate(actions[:ticks]):
        unknown = sorted(set(action) - _ACTION_FIELDS)
        if unknown:
            raise ForkRunnerError(
                "native input mapping lacks fields: " + ",".join(unknown))
        event = {"tick": index, "type": "action"}
        if action:
            event.update(action)
        else:
            event["server_only"] = 1
        lines.append(json.dumps(event, sort_keys=True, separators=(",", ":")))
    destination.write_text("\n".join(lines) + "\n")


def _native_process(
    native_import: pathlib.Path, report: dict[str, Any], root: pathlib.Path,
    actions: list[dict[str, Any]], ticks: int, world_type: str,
    restore_only: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, bytes]]:
    root.mkdir()
    script = root / "events.jsonl"
    _native_script(
        native_import / "magma_restore.jsonl", script, actions,
        0 if restore_only else ticks)
    state_path = root / "state.jsonl"
    paths = {
        "blocks": root / "blocks.u16le",
        "sky_light": root / "sky_light.u8",
        "block_light": root / "block_light.u8",
        "height": root / "height.u16le",
    }
    manifest = json.loads(
        (native_import / "capsule" / "manifest.json").read_text())
    seed = manifest.get("source", {}).get("seed")
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ForkRunnerError("native capsule has no integer world seed")
    bundle = report.get("active_chunk_bundle") or {}
    radius = bundle.get("radius", 1)
    if not isinstance(radius, int) or radius < 0 or radius > 32:
        raise ForkRunnerError("native capsule has invalid active radius")
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(native_import / "capsule"),
        "MAGMA_NATIVE_SAVE_DIR": str(root),
        "MAGMA_BLOCKS_OUT": str(paths["blocks"]),
        "MAGMA_SKY_LIGHT_OUT": str(paths["sky_light"]),
        "MAGMA_BLOCK_LIGHT_OUT": str(paths["block_light"]),
        "MAGMA_HEIGHTS_OUT": str(paths["height"]),
        "MAGMA_BLOCKS_BOX": ",".join(str(value) for value in report["box"]),
    })
    if restore_only:
        environment["MAGMA_RESTORE_ONLY"] = "1"
    result = subprocess.run([
        str(MAGMA_GAME), "--seed", str(seed),
        "--world", "superflat" if world_type == "flat" else "default",
        "--headless", "--ticks", str(max(1, ticks)),
        "--view-distance", str(max(1, radius)), "--mobs", "on",
        *(["--villages", "on"] if world_type != "flat" else []),
        "--enchanting", "on", "--brewing", "on",
        "--script", str(script), "--state-out", str(state_path),
        "--render", "off", "--pace", "unlimited",
    ], cwd=ROOT / "magma", env=environment, text=True,
       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise ForkRunnerError(
            "native horizon run failed: " + result.stdout[-4000:])
    states = [json.loads(line) for line in state_path.read_text().splitlines()
              if line.strip()]
    expected_rows = 1 if restore_only else ticks
    if len(states) != expected_rows:
        raise ForkRunnerError(
            f"native emitted {len(states)} states, expected {expected_rows}")
    return states, {name: path.read_bytes() for name, path in paths.items()}


def _normalized_inventory(rows: Any) -> Any:
    if not isinstance(rows, list):
        return rows
    normalized = []
    for source in rows:
        if not isinstance(source, dict) or int(source.get("count", 0)) <= 0:
            continue
        row = dict(source)
        if "id" not in row and "item" in row:
            row["id"] = row.pop("item")
        row.pop("nbt_subset_exact", None)
        row.setdefault("repair_cost", 0)
        row.setdefault("custom_name", "")
        row.setdefault("enchants", [])
        if "stack_payload" in row:
            row["stack_payload"] = _normalized_stack_payload(
                row["stack_payload"])
        normalized.append(row)
    return normalized


def _normalized_stack_payload(payload: Any) -> Any:
    if not isinstance(payload, dict) or payload.get("kind") != "item_tag":
        return payload
    return {
        "kind": "item_tag",
        "nbt": nbt_codec.canonical_hex(payload.get("nbt")),
    }


def _normalized_entities(rows: Any) -> Any:
    if not isinstance(rows, list):
        return rows
    result = []
    for source in rows:
        if not isinstance(source, dict):
            result.append(source)
            continue
        row = dict(source)
        row.pop("loaded_order", None)
        if row.get("type") == "EntityXPOrb":
            row["kind"] = "xp_orb"
            row["type"] = 21
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
        elif row.get("type") == "EntityItem":
            row.pop("type", None)
            row["kind"] = "item"
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            row.pop("item_exact", None)
        elif row.get("type") == "EntityFallingBlock":
            row["kind"] = "falling_block"
            row["type"] = 38
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            row.pop("falling_exact", None)
        elif row.get("type") == "EntityTNTPrimed":
            row["kind"] = "primed_tnt"
            row["type"] = 39
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            row.pop("primed_tnt_exact", None)
        elif row.get("type") == "EntityEnderCrystal":
            row["kind"] = "end_crystal"
            row["type"] = 40
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            row.pop("end_crystal_exact", None)
        elif row.get("type") in {
                "EntityMinecartEmpty", "EntityMinecartChest",
                "EntityMinecartFurnace", "EntityMinecartTNT",
                "EntityMinecartMobSpawner", "EntityMinecartHopper"}:
            row["kind"] = "minecart"
            row["type"] = 28
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            for field in (
                    "reverse", "hopper_enabled", "entity_have_gaussian"):
                if isinstance(row.get(field), bool):
                    row[field] = int(row[field])
            if row.get("minecart_kind") == 4:
                entity_id = row.pop("spawner_entity_id", None)
                if entity_id is not None:
                    if ":" not in entity_id:
                        entity_id = "minecraft:" + entity_id.lower()
                    row["spawner_entity"] = (
                        state_capsule.SPAWNER_ENTITY_TYPES[entity_id])
                if row.get("spawner_spawn_data_nbt") is not None:
                    row["spawner_spawn_data_nbt"] = (
                        state_capsule.nbt_codec.canonical_hex(
                            row["spawner_spawn_data_nbt"]))
                potentials = []
                for source in row.get("spawner_potentials", []):
                    potential = dict(source)
                    potential_id = potential.pop("entity_id", None)
                    if potential_id is not None:
                        if ":" not in potential_id:
                            potential_id = (
                                "minecraft:" + potential_id.lower())
                        potential["entity"] = (
                            state_capsule.SPAWNER_ENTITY_TYPES[
                                potential_id])
                    if potential.get("entity_nbt") is not None:
                        potential["entity_nbt"] = (
                            state_capsule.nbt_codec.canonical_hex(
                                potential["entity_nbt"]))
                    potentials.append(potential)
                row["spawner_potentials"] = potentials
        elif row.get("kind") == "minecart":
            for field in (
                    "reverse", "hopper_enabled", "entity_have_gaussian"):
                if isinstance(row.get(field), bool):
                    row[field] = int(row[field])
            if row.get("minecart_kind") == 4:
                if row.get("spawner_spawn_data_nbt") is not None:
                    row["spawner_spawn_data_nbt"] = (
                        state_capsule.nbt_codec.canonical_hex(
                            row["spawner_spawn_data_nbt"]))
                row["spawner_potentials"] = [
                    {
                        **potential,
                        "entity_nbt": state_capsule.nbt_codec.canonical_hex(
                            potential["entity_nbt"]),
                    }
                    for potential in row.get("spawner_potentials", [])
                ]
        elif row.get("kind") == "item":
            row.pop("type", None)
        if row.get("type") == "EntityArmorStand":
            row["kind"] = "armor_stand"
            row["type"] = 34
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
        if row.get("type") == "EntityWither":
            row["kind"] = "wither"
            row["type"] = 66
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            for field in (
                    "wither_exact", "no_ai_plain_exact",
                    "living_base_exact", "no_ai_base_exact",
                    "no_ai_pig_exact", "mob_equipment_empty",
                    "mob_potions_empty"):
                row.pop(field, None)
        elif row.get("type") == "EntityWitherSkull":
            row["kind"] = "projectile"
            row["type"] = 10
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            row.pop("wither_skull_exact", None)
        living_types = {
            "EntityZombie": 2, "EntitySkeleton": 3,
            "EntityWitherSkeleton": 32, "EntityCreeper": 4,
            "EntitySpider": 5, "EntityCaveSpider": 39,
            "EntityEnderman": 6, "EntityBlaze": 7, "EntityGhast": 26,
            "EntityWitch": 23, "EntityZombieVillager": 41,
            "EntityVindicator": 51, "EntityEvoker": 52, "EntityVex": 53,
            "EntityGuardian": 55, "EntityElderGuardian": 56,
            "EntitySheep": 10, "EntityCow": 12, "EntityChicken": 13,
            "EntityPig": 11,
            "EntitySquid": 14, "EntityPigZombie": 15,
            "EntityWolf": 16, "EntityOcelot": 17,
            "EntityMagmaCube": 27, "EntitySlime": 35,
            "EntitySilverfish": 36, "EntityVillager": 40,
            "EntityIronGolem": 57, "EntityStray": 58, "EntityHusk": 59,
            "EntityMooshroom": 60, "EntityRabbit": 61,
            "EntityPolarBear": 62, "EntityBat": 24,
            "EntityEndermite": 63, "EntitySnowman": 64,
            "EntityGiantZombie": 65,
            "EntityHorse": 68, "EntityDonkey": 69, "EntityMule": 70,
            "EntitySkeletonHorse": 71, "EntityZombieHorse": 72,
            "EntityLlama": 25,
        }
        if row.get("type") in living_types:
            row["kind"] = "mob"
            row["type"] = living_types[row["type"]]
            row.pop("dx", None)
            row.pop("dy", None)
            row.pop("dz", None)
            for field in (
                    "active_fresh_villager_exact", "villager_exact",
                    "horse_exact",
                    "no_ai_pig_exact", "no_ai_plain_exact",
                    "living_base_exact", "no_ai_base_exact",
                    "mob_equipment_empty", "mob_potions_empty"):
                row.pop(field, None)
        elif row.get("kind") == "mob":
            for field in (
                    "active_fresh_villager_exact", "villager_exact",
                    "horse_exact",
                    "no_ai_pig_exact", "no_ai_plain_exact",
                    "living_base_exact", "no_ai_base_exact",
                    "mob_equipment_empty", "mob_potions_empty"):
                row.pop(field, None)
        for field in (
                "yaw", "pitch", "hover_start", "health", "fall_distance",
                "base_last_damage", "render_yaw_offset",
                "prev_render_yaw_offset", "rotation_yaw_head",
                "prev_rotation_yaw_head", "body_prev_render_yaw_head",
                "head_yaw_0", "head_yaw_1", "head_pitch_0",
                "head_pitch_1", "head_prev_yaw_0", "head_prev_yaw_1",
                "head_prev_pitch_0", "head_prev_pitch_1",
                "chicken_wing_rotation", "chicken_dest_pos",
                "chicken_old_flap_speed", "chicken_old_flap",
                "chicken_wing_rot_delta",
                "squid_pitch", "squid_prev_pitch", "squid_yaw",
                "squid_prev_yaw", "squid_rotation",
                "squid_prev_rotation", "squid_tentacle_angle",
                "squid_last_tentacle_angle",
                "squid_random_motion_speed",
                "squid_rotation_velocity", "squid_rotate_speed",
                "squid_random_motion_x", "squid_random_motion_y",
                "squid_random_motion_z", "squid_render_yaw_offset",
                "squid_head_yaw", "squid_body_prev_head_yaw",
                "slime_squish_amount", "slime_squish_factor",
                "slime_prev_squish_factor",
                "bat_head_yaw", "bat_render_yaw_offset",
                "bat_body_prev_head_yaw", "bat_move_forward",
                "bat_move_strafing",
                "armor_stand_fall_distance",
                "armor_stand_last_damage", "armor_stand_absorption",
                "armor_stand_max_health", "armor_stand_max_health_base",
                "horse_jump_power", "horse_head_lean",
                "horse_prev_head_lean", "horse_rearing_amount",
                "horse_prev_rearing_amount", "horse_mouth_openness",
                "horse_prev_mouth_openness", "horse_prev_limb_amount",
                "horse_limb_amount", "horse_limb_swing"):
            if isinstance(row.get(field), (int, float)) \
                    and not isinstance(row[field], bool):
                row[field] = struct.unpack(
                    "<f", struct.pack("<f", row[field]))[0]
        if "stack_payload" in row:
            row["stack_payload"] = _normalized_stack_payload(
                row["stack_payload"])
        if isinstance(row.get("items"), list):
            row["items"] = [
                ({
                    **item,
                    "stack_payload": _normalized_stack_payload(
                        item["stack_payload"]),
                } if isinstance(item, dict) and "stack_payload" in item
                 else item)
                for item in row["items"]
            ]
        if "horse_inventory" in row:
            row["horse_inventory"] = _normalized_inventory(
                row["horse_inventory"])
        if row.get("type") == 25 and row.get("no_ai") is True:
            for field in (
                    "llama_attack_target_eid", "llama_entity_seed48",
                    "llama_fall_distance", "llama_navigation",
                    "llama_on_ground", "llama_ranged_attack_time",
                    "llama_ranged_see_time", "llama_task_mask"):
                row.pop(field, None)
        if "mob_effects" in row:
            row["potions"] = row.pop("mob_effects")
        if row.get("kind") == "mob":
            row.pop("stack_payload", None)
        # ``*_exact`` fields are capture/import capability predicates, not
        # simulated entity state.  Recorder emits false predicates for every
        # living entity while the native trace only emits predicates relevant
        # to that class, so retaining them creates false tick-0 divergences.
        # state_capsule has already enforced every required predicate before
        # this behavioral comparison is reached.
        for field in tuple(row):
            if field.endswith("_exact"):
                row.pop(field)
        result.append(row)
    # Entity payload enumeration is an observation/capture detail. Java's
    # causal World.loadedEntityList order is compared independently below, so
    # canonicalize payload identity here instead of conflating the two lists.
    if all(isinstance(row, dict) and isinstance(row.get("eid"), int)
           for row in result):
        return sorted(result, key=lambda row: row["eid"])
    return result


def _normalized_containers(rows: Any) -> Any:
    if not isinstance(rows, list):
        return rows
    result = []
    for source in rows:
        if not isinstance(source, dict):
            result.append(source)
            continue
        row = dict(source)
        row.pop("loaded_order", None)
        items = []
        for item_source in row.get("items", []):
            item = dict(item_source)
            if "stack_payload" in item:
                item["stack_payload"] = _normalized_stack_payload(
                    item["stack_payload"])
            items.append(item)
        row["items"] = items
        result.append(row)
    return sorted(
        result,
        key=lambda row: (
            row.get("x"), row.get("y"), row.get("z"), row.get("type")
        ) if isinstance(row, dict) else (0, 0, 0, ""),
    )


def _coerce_json_numbers(expected: Any, actual: Any) -> Any:
    """Recover semantic numeric types lost by JSON's ``0`` spelling.

    Native state output uses enough significant digits for exact values, but
    JSON has no float/int tag and serializes an exact 0.0 as ``0``. Raw bit
    surfaces remain separately comparator-gated; this projection prevents the
    JSON parser itself from inventing a t=0 type divergence.
    """
    if isinstance(expected, dict) and isinstance(actual, dict):
        return {key: _coerce_json_numbers(expected[key], actual[key])
                if key in actual else actual.get(key) for key in expected}
    if isinstance(expected, list) and isinstance(actual, list) \
            and len(expected) == len(actual):
        return [_coerce_json_numbers(left, right)
                for left, right in zip(expected, actual)]
    if isinstance(expected, float) and isinstance(actual, (int, float)) \
            and not isinstance(actual, bool):
        return float(actual)
    if isinstance(expected, int) and not isinstance(expected, bool) \
            and isinstance(actual, (int, float)) and not isinstance(actual, bool) \
            and float(actual).is_integer():
        return int(actual)
    return actual


def _state_vector(authoritative: dict[str, Any], native: dict[str, Any]) \
        -> tuple[dict[str, Any], dict[str, Any]]:
    direct = (
        "x", "y", "z", "vx", "vy", "vz", "yaw", "pitch",
        "fall_distance", "fire", "air", "health", "max_health",
        "absorption", "food", "saturation", "food_exhaustion", "food_timer",
        "attack_ticks", "attack_cooldown", "hurt_time",
        "hurt_resistant_time", "death_time", "deaths", "dim", "held_slot",
        "stat_play_one_minute", "stat_time_since_death",
        "held_id", "held_count", "held_meta", "xp_frac", "xp_total",
        "active_hand", "active_use_remaining", "active_use_elapsed",
        "active_use_action",
        "player_riding_eid",
        "world_time", "total_time", "world_rand_seed48",
        "world_rand_gaussian", "math_rand_seed48",
        "entity_seed_generator_seed48", "block_rand_seed48",
        "world_update_lcg", "random_tick_speed", "scheduled_ticks",
        "dragon_respawn_state", "dragon_respawn_ticks",
        "comparators", "containers",
        "moving_pistons", "item_frames", "flower_pots", "note_blocks",
        "skulls", "entities",
    )
    booleans = (
        "on_ground", "sprinting", "sneaking", "jumping", "dead",
        "do_entity_drops", "do_mob_spawning", "do_mob_loot",
        "world_rand_have_gaussian", "hand_active",
        "dragon_fight_present",
    )
    expected: dict[str, Any] = {}
    actual: dict[str, Any] = {}
    float32_fields = {
        "yaw", "pitch", "fall_distance", "health", "max_health",
        "absorption", "saturation", "food_exhaustion", "attack_cooldown",
        "xp_frac",
    }
    for field in direct:
        if field in authoritative and field in native:
            value = authoritative[field]
            if field in float32_fields:
                value = struct.unpack("<f", struct.pack("<f", value))[0]
            expected[field] = value
            actual_value = native[field]
            if field in float32_fields:
                actual_value = struct.unpack(
                    "<f", struct.pack("<f", actual_value))[0]
            actual[field] = actual_value
    for field in booleans:
        if field in authoritative and field in native:
            expected[field] = (bool(authoritative[field])
                               if isinstance(native[field], bool)
                               else int(bool(authoritative[field])))
            actual[field] = native[field]
    if "next_entity_id" in authoritative and "entity_id_cursor" in native:
        expected["entity_id_cursor"] = authoritative["next_entity_id"]
        actual["entity_id_cursor"] = native["entity_id_cursor"]
    if "xp" in authoritative and "xp_level" in native:
        expected["xp_level"] = authoritative["xp"]
        actual["xp_level"] = native["xp_level"]
    if "inventory" in authoritative and "inventory" in native:
        expected["inventory"] = _normalized_inventory(authoritative["inventory"])
        actual["inventory"] = _normalized_inventory(native["inventory"])
    if "entities" in authoritative and "entities" in native:
        expected["entities"] = _normalized_entities(authoritative["entities"])
        actual["entities"] = _normalized_entities(native["entities"])
    if "containers" in authoritative and "containers" in native:
        expected["containers"] = _normalized_containers(
            authoritative["containers"])
        actual["containers"] = _normalized_containers(native["containers"])
    if "spawners" in authoritative and "spawners" in native:
        normalized_spawners = []
        for spawner in authoritative["spawners"]:
            entity_id = spawner["entity_id"]
            if ":" not in entity_id:
                entity_id = "minecraft:" + entity_id.lower()
            potentials = []
            for potential in spawner["potentials"]:
                potential_id = potential["entity_id"]
                if ":" not in potential_id:
                    potential_id = "minecraft:" + potential_id.lower()
                potentials.append({
                    "entity": state_capsule.SPAWNER_ENTITY_TYPES[
                        potential_id],
                    "weight": potential["weight"],
                    "entity_nbt": state_capsule.nbt_codec.canonical_hex(
                        potential["entity_nbt"]),
                    "default_entity_nbt":
                        potential["default_entity_nbt"],
                })
            normalized_spawners.append({
                "x": spawner["x"], "y": spawner["y"],
                "z": spawner["z"],
                "entity": state_capsule.SPAWNER_ENTITY_TYPES[entity_id],
                "delay": spawner["delay"],
                "min_delay": spawner["min_delay"],
                "max_delay": spawner["max_delay"],
                "spawn_count": spawner["spawn_count"],
                "max_nearby": spawner["max_nearby"],
                "activate_range": spawner["activate_range"],
                "spawn_range": spawner["spawn_range"],
                "spawn_data_nbt": state_capsule.nbt_codec.canonical_hex(
                    spawner["spawn_data_nbt"]),
                "default_entity_nbt": spawner["default_entity_nbt"],
                "potentials": potentials,
            })
        expected["spawners"] = sorted(
            normalized_spawners,
            key=lambda row: (row["x"], row["y"], row["z"]))
        actual_spawners = []
        for source in native["spawners"]:
            row = dict(source)
            row["spawn_data_nbt"] = state_capsule.nbt_codec.canonical_hex(
                row["spawn_data_nbt"])
            row["potentials"] = [
                {
                    **potential,
                    "entity_nbt": state_capsule.nbt_codec.canonical_hex(
                        potential["entity_nbt"]),
                }
                for potential in row["potentials"]
            ]
            actual_spawners.append(row)
        actual["spawners"] = sorted(
            actual_spawners,
            key=lambda row: (row["x"], row["y"], row["z"]))
    if "loaded_entity_order" in native \
            and isinstance(authoritative.get("entities"), list) \
            and all(isinstance(row, dict) and "loaded_order" in row
                    for row in authoritative["entities"]):
        expected["loaded_entity_order"] = [
            row["eid"] for row in sorted(
                authoritative["entities"],
                key=lambda row: row["loaded_order"])
        ]
        actual["loaded_entity_order"] = native["loaded_entity_order"]
    if "loaded_tile_order" in native \
            and isinstance(authoritative.get("loaded_tiles"), list) \
            and authoritative["loaded_tiles"] \
            and all(isinstance(row, dict) and "loaded_order" in row
                    for row in authoritative["loaded_tiles"]):
        expected["loaded_tile_order"] = [
            [row["x"], row["y"], row["z"]]
            for row in sorted(
                authoritative["loaded_tiles"],
                key=lambda row: row["loaded_order"])
        ]
        actual["loaded_tile_order"] = native["loaded_tile_order"]
        if "tickable_tile_order" in native \
                and all(isinstance(row.get("update_order"), int)
                        for row in authoritative["loaded_tiles"]):
            expected["tickable_tile_order"] = [
                [row["x"], row["y"], row["z"]]
                for row in sorted(
                    (row for row in authoritative["loaded_tiles"]
                     if row.get("tickable")),
                    key=lambda row: row["update_order"])
            ]
            actual["tickable_tile_order"] = \
                native["tickable_tile_order"]
    elif "loaded_tile_order" in native \
            and isinstance(authoritative.get("containers"), list) \
            and all(isinstance(row, dict) and "loaded_order" in row
                    for row in authoritative["containers"]):
        expected["loaded_tile_order"] = [
            [row["x"], row["y"], row["z"]]
            for row in sorted(
                authoritative["containers"],
                key=lambda row: row["loaded_order"])
        ]
        actual["loaded_tile_order"] = native["loaded_tile_order"]
    weather_map = {
        "raining": "raining", "thundering": "thundering",
        "rain_time": "rain_time", "thunder_time": "thunder_time",
        "clean_weather_time": "clean_weather_time",
        "do_weather_cycle": "weather_cycle",
        "do_daylight_cycle": "daylight_cycle",
        "prev_rain_strength": "prev_rain_strength",
        "rain_strength": "rain_strength",
        "prev_thunder_strength": "prev_thunder_strength",
        "thunder_strength": "thunder_strength",
    }
    if isinstance(native.get("weather"), dict):
        for java_field, native_field in weather_map.items():
            if java_field not in authoritative \
                    or native_field not in native["weather"]:
                continue
            key = "weather." + native_field
            value = authoritative[java_field]
            if native_field in {
                "prev_rain_strength", "rain_strength",
                "prev_thunder_strength", "thunder_strength",
            }:
                value = struct.unpack("<f", struct.pack("<f", value))[0]
            expected[key] = int(value) if isinstance(value, bool) else value
            actual_value = native["weather"][native_field]
            if native_field in {
                "prev_rain_strength", "rain_strength",
                "prev_thunder_strength", "thunder_strength",
            }:
                actual_value = struct.unpack(
                    "<f", struct.pack("<f", actual_value))[0]
            actual[key] = actual_value
    return expected, _coerce_json_numbers(expected, actual)


def _loaded_chunk_order(
    normalized: dict[str, Any], dimension: int,
) -> list[list[int]]:
    worlds = normalized.get("worlds")
    if not isinstance(worlds, list):
        raise ForkRunnerError("normalized Java topology has no worlds array")
    matching = [world for world in worlds if isinstance(world, dict)
                and world.get("dim") == dimension]
    if len(matching) != 1:
        raise ForkRunnerError(
            f"normalized Java topology has {len(matching)} worlds for "
            f"dimension {dimension}")
    chunks = matching[0].get("loaded_chunks")
    if not isinstance(chunks, list):
        raise ForkRunnerError(
            "normalized Java topology has no loaded_chunks array")
    result: list[list[int]] = []
    seen: set[tuple[int, int]] = set()
    for index, chunk in enumerate(chunks):
        if not isinstance(chunk, dict) or chunk.get("order") != index \
                or isinstance(chunk.get("x"), bool) \
                or not isinstance(chunk.get("x"), int) \
                or isinstance(chunk.get("z"), bool) \
                or not isinstance(chunk.get("z"), int):
            raise ForkRunnerError(
                f"normalized Java loaded chunk {index} has invalid order/coords")
        key = (chunk["x"], chunk["z"])
        if key in seen:
            raise ForkRunnerError(
                f"normalized Java topology duplicates loaded chunk {key}")
        seen.add(key)
        result.append([key[0], key[1]])
    return result


def _raw_expected(
    snapshot: pathlib.Path, box: list[int], dimension: int,
    live: dict[str, pathlib.Path] | None = None,
) -> dict[str, bytes]:
    semantic = anvil_semantic.read_save(snapshot / "save")
    blocks, sky, block_light, _ = anvil_to_capsule.extract_cuboid(
        semantic, dimension, box)
    result = {
        "blocks": blocks, "sky_light": sky, "block_light": block_light,
        "height": anvil_to_capsule.extract_height_cuboid(
            semantic, dimension, box),
    }
    if live is not None:
        for surface in ("blocks", "sky_light", "block_light"):
            result[surface] = live[surface].read_bytes()
    return result


def _run_native(
    java_run: dict[str, Any], native_import: pathlib.Path,
    report: dict[str, Any], output: pathlib.Path,
    horizons: tuple[int, ...], actions: list[dict[str, Any]], world_type: str,
) -> dict[str, Any]:
    native_root = output / "native"
    native_root.mkdir(parents=True)
    t0_states, t0_raw = _native_process(
        native_import, report, native_root / "t000", actions, 0,
        world_type, restore_only=True)
    first: dict[str, Any] | None = None
    t0_expected, t0_actual = _state_vector(
        java_run["trace"][0]["authoritative"], t0_states[0])
    if "loaded_chunk_order" in t0_states[0]:
        t0_expected["loaded_chunk_order"] = _loaded_chunk_order(
            java_run["normalized"], int(report["dimension"]))
        t0_actual["loaded_chunk_order"] = t0_states[0]["loaded_chunk_order"]
    difference = anvil_semantic.first_difference(
        t0_expected, t0_actual, "$.represented")
    if difference is not None:
        first = {"tick": 0, "surface": "state", "difference": difference}
    expected_t0 = _raw_expected(
        java_run["snapshots"][0], report["box"], int(report["dimension"]),
        java_run["raw_snapshots"][0])
    for surface in expected_t0:
        if first is None and expected_t0[surface] != t0_raw[surface]:
            first = {"tick": 0, "surface": surface, "reason": "raw_mismatch"}
    statistics = report.get("player_statistics") or {}
    if first is None and (
        t0_states[0].get("statistics_present") != 1
        or t0_states[0].get("statistics_bytes") != statistics.get("bytes")
        or t0_states[0].get("statistics_fnv64") != statistics.get("fnv64")
    ):
        first = {
            "tick": 0,
            "surface": "player.statistics",
            "reason": "opaque_statistics_payload_mismatch",
        }

    max_horizon = max(horizons)
    states, final_raw = _native_process(
        native_import, report, native_root / f"t{max_horizon:03d}",
        actions, max_horizon, world_type)
    for tick, native_state in enumerate(states, 1):
        expected, actual = _state_vector(
            java_run["trace"][tick]["authoritative"], native_state)
        difference = anvil_semantic.first_difference(
            expected, actual, "$.represented")
        if difference is not None and first is None:
            first = {"tick": tick, "surface": "state",
                     "difference": difference}

    raw_horizons: dict[str, str] = {"0": "exact"}
    dimension = int(report["dimension"])
    for horizon in horizons:
        if horizon == max_horizon:
            actual_raw = final_raw
        else:
            _, actual_raw = _native_process(
                native_import, report, native_root / f"t{horizon:03d}",
                actions, horizon, world_type)
        expected_raw = _raw_expected(
            java_run["snapshots"][horizon], report["box"], dimension,
            java_run["raw_snapshots"][horizon])
        mismatch = next((surface for surface in expected_raw
                         if expected_raw[surface] != actual_raw[surface]), None)
        raw_horizons[str(horizon)] = "exact" if mismatch is None else mismatch
        if mismatch is not None and first is None:
            first = {"tick": horizon, "surface": mismatch,
                     "reason": "raw_mismatch"}

    return {
        "status": "exact" if first is None else "diverged",
        "represented_t0": "exact" if first is None or first["tick"] > 0
            else "diverged",
        "first": first,
        "state_ticks_compared": max_horizon + 1,
        "raw_horizons": raw_horizons,
    }


def _native_world_probe(
    native_import: pathlib.Path, report: dict[str, Any], output: pathlib.Path
) -> dict[str, Any]:
    bundle = report.get("active_chunk_bundle")
    if not isinstance(bundle, dict):
        raise ForkRunnerError("native import emitted no active chunk bundle")
    bundle_path = native_import / anvil_to_capsule.CHUNK_BUNDLE_FILE
    if save_fork.sha256(bundle_path) != bundle.get("sha256"):
        raise ForkRunnerError("active chunk bundle checksum mismatch")
    cold_stores = report.get("cold_chunk_stores")
    if not isinstance(cold_stores, list) or not cold_stores:
        raise ForkRunnerError("native import emitted no persisted chunk stores")
    for store in cold_stores:
        if not isinstance(store, dict) or not isinstance(store.get("file"), str):
            raise ForkRunnerError("invalid persisted chunk store report")
        if save_fork.sha256(native_import / store["file"]) != store.get("sha256"):
            raise ForkRunnerError(
                f"persisted chunk store checksum mismatch: {store['file']}")
    radius = bundle.get("radius")
    if not isinstance(radius, int) or radius < 0 or radius > 8:
        return {
            "status": "rejected", "todo": "SAVE-03",
            "reason": f"active chunk radius {radius!r} exceeds native hot pool 8",
        }
    probe = output / "native_world_probe"
    probe.mkdir()
    script = probe / "restore.jsonl"
    events = [{
        "tick": 0, "type": "attach_chunk_store",
        "file": store["file"], "dim": store["dimension"],
    } for store in cold_stores]
    events.append({
        "tick": 0, "type": "set_dimension",
        "dimension": bundle["dimension"],
    })
    events.append({
        "tick": 0, "type": "snapshot_region",
        "dim": bundle["dimension"], "cx": bundle["center_cx"],
        "cz": bundle["center_cz"], "radius": radius,
    })
    script.write_text("".join(
        json.dumps(event, separators=(",", ":")) + "\n"
        for event in events))
    actual = {
        "blocks": probe / "blocks.u16le",
        "sky_light": probe / "sky_light.u8",
        "block_light": probe / "block_light.u8",
        "height": probe / "height.u16le",
    }
    expected = {
        "blocks": native_import / anvil_to_capsule.BLOCK_FILE,
        "sky_light": native_import / anvil_to_capsule.SKY_FILE,
        "block_light": native_import / anvil_to_capsule.BLOCK_LIGHT_FILE,
        "height": native_import / anvil_to_capsule.HEIGHT_FILE,
    }
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(native_import),
        "MAGMA_RESTORE_ONLY": "1",
        "MAGMA_BLOCKS_OUT": str(actual["blocks"]),
        "MAGMA_SKY_LIGHT_OUT": str(actual["sky_light"]),
        "MAGMA_BLOCK_LIGHT_OUT": str(actual["block_light"]),
        "MAGMA_HEIGHTS_OUT": str(actual["height"]),
        "MAGMA_BLOCKS_BOX": ",".join(str(value) for value in report["box"]),
    })
    started = time.monotonic()
    result = subprocess.run([
        str(MAGMA_GAME), "--seed", "0", "--world", "superflat",
        "--headless", "--ticks", "1", "--view-distance", str(max(1, radius)),
        "--mobs", "off", "--script", str(script), "--render", "off",
        "--pace", "unlimited",
    ], cwd=ROOT / "magma", env=environment, text=True,
       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    elapsed_ms = round((time.monotonic() - started) * 1000)
    if result.returncode:
        raise ForkRunnerError(
            "native persisted-world restore failed: " + result.stdout[-2000:])
    for surface in ("blocks", "sky_light", "block_light", "height"):
        expected_raw = expected[surface].read_bytes()
        actual_raw = actual[surface].read_bytes()
        if expected_raw != actual_raw:
            width = 2 if surface in ("blocks", "height") else 1
            cells = min(len(expected_raw), len(actual_raw)) // width
            index = next((cell for cell in range(cells)
                          if expected_raw[cell * width:(cell + 1) * width]
                          != actual_raw[cell * width:(cell + 1) * width]), cells)
            coordinate = (
                anvil_to_capsule.CAPSULE.coordinate(index, report["box"])
                if index < cells else None)
            raise ForkRunnerError(
                f"native t=0 {surface} first differs at {coordinate}; "
                f"expected_bytes={len(expected_raw)} actual_bytes={len(actual_raw)}")
    return {
        "status": "exact", "active_chunks": bundle["chunks"],
        "persisted_chunks": sum(store["chunks"] for store in cold_stores),
        "dimensions": [store["dimension"] for store in cold_stores],
        "cells_checked": anvil_to_capsule.CAPSULE.cell_count(report["box"]),
        "elapsed_ms": elapsed_ms,
    }


def run(
    source: pathlib.Path,
    output: pathlib.Path,
    instance: int,
    seed: int,
    username: str,
    world_type: str,
    horizons: tuple[int, ...],
    actions: list[dict[str, Any]],
    repeat_java: bool,
    allow_native_reject: bool,
    fixture_id: str | None,
    restore_source_hidden: bool = False,
    keep_reload_topology: bool = False,
    raw_box: list[int] | None = None,
) -> int:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise ForkRunnerError(f"output already exists: {output}")
    output.mkdir(parents=True)
    source_hidden = None
    if restore_source_hidden:
        source_hidden_path = source / "hidden_state.json"
        try:
            source_hidden = json.loads(source_hidden_path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise ForkRunnerError(
                f"invalid source hidden state: {exc}") from exc
        if not isinstance(source_hidden, dict) \
                or source_hidden.get("schema") != "qrl.hidden_state.v1":
            raise ForkRunnerError("source hidden state has invalid schema")
        if keep_reload_topology:
            worlds = source_hidden.get("worlds")
            if not isinstance(worlds, list):
                raise ForkRunnerError("source hidden state has no worlds")
            for world in worlds:
                if not isinstance(world, dict) or "chunks" not in world:
                    raise ForkRunnerError(
                        "source hidden state has no captured chunk topology")
                # A real cold reload deliberately reconstructs provider cache
                # membership from the watcher. Restore entity/tile/RNG
                # transients, but retain that Java reload topology instead of
                # trying to resurrect chunks that were merely cached before
                # the source save.
                del world["chunks"]
    first = _run_java(
        source, output / "java_a", instance, seed, username, world_type,
        horizons, actions, source_hidden, raw_box,
    )
    java_repeat = {"status": "not_run"}
    if repeat_java:
        second = _run_java(
            source, output / "java_b", instance, seed, username, world_type,
            horizons, actions, first["hidden"], raw_box,
        )
        for tick, (left_row, right_row) in enumerate(zip(
                first["trace"], second["trace"])):
            trace_difference = anvil_semantic.first_difference(
                left_row["authoritative"], right_row["authoritative"])
            if trace_difference is not None:
                raise ForkRunnerError(
                    "Java A/B authoritative state first diverged at tick "
                    f"{tick}: {trace_difference}")
        for tick in (0, *horizons):
            difference = anvil_semantic.compare_saves(
                first["snapshots"][tick] / "save",
                second["snapshots"][tick] / "save",
            )
            if difference is not None:
                raise ForkRunnerError(
                    f"Java A/B first diverged at horizon {tick}: {difference}")
            hidden_difference = anvil_semantic.first_difference(
                first["hidden_snapshots"][tick],
                second["hidden_snapshots"][tick],
            )
            if hidden_difference is not None:
                raise ForkRunnerError(
                    "Java A/B hidden state first diverged at horizon "
                    f"{tick}: {hidden_difference}")
            for surface in ("blocks", "sky_light", "block_light"):
                left_raw = first["raw_snapshots"][tick][surface].read_bytes()
                right_raw = second["raw_snapshots"][tick][surface].read_bytes()
                if left_raw != right_raw:
                    raise ForkRunnerError(
                        "Java A/B live raw state first diverged at horizon "
                        f"{tick}: {surface}")
        java_repeat = {"status": "exact", "horizons": [0, *horizons]}

    native = _native_capability(
        first, output, horizons, actions, world_type, raw_box)
    report = {
        "schema": "netherite.save_fork_run",
        "version": 2,
        "source": str(source),
        "horizons": list(horizons),
        "actions": len(actions),
        "java_repeat": java_repeat,
        "native": native,
        "fixture": fixture_id,
        "boundary": (
            "captured_hidden_resume" if restore_source_hidden
            else "vanilla_cold_reload"),
    }
    (output / "fork_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    if native["status"] == "diverged" and fixture_id is not None:
        print(json.dumps(native, indent=2, sort_keys=True), file=sys.stderr)
        return 2
    if native["status"] == "rejected" and not allow_native_reject:
        print(json.dumps(native, indent=2, sort_keys=True), file=sys.stderr)
        return 2
    print(
        "PASS save fork horizons " + ",".join(str(value) for value in horizons)
        + f"; Java repeat={java_repeat['status']}; native={native['status']}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=5)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--username", default="PoolPlayer0")
    parser.add_argument("--world-type", choices=("default", "flat"), default="flat")
    parser.add_argument("--horizons", default="1,20,200")
    parser.add_argument(
        "--box",
        help="raw/native comparison cuboid x0,y0,z0,x1,y1,z1")
    parser.add_argument("--inputs", type=pathlib.Path)
    parser.add_argument("--no-java-repeat", action="store_true")
    parser.add_argument("--allow-native-reject", action="store_true")
    parser.add_argument("--fixture", type=pathlib.Path)
    parser.add_argument("--allow-uncontracted", action="store_true")
    parser.add_argument(
        "--restore-source-hidden", action="store_true",
        help="restore the source capture's non-Anvil continuation sidecar")
    parser.add_argument(
        "--keep-reload-topology", action="store_true",
        help="with --restore-source-hidden, retain cold watcher chunk topology")
    args = parser.parse_args()
    if args.keep_reload_topology and not args.restore_source_hidden:
        raise ForkRunnerError(
            "--keep-reload-topology requires --restore-source-hidden")
    contract = None
    if args.fixture is not None:
        contract = fixture_contract.load(args.fixture)
        if args.inputs is not None:
            raise ForkRunnerError("--inputs cannot be combined with --fixture")
        horizons = tuple(contract["horizons"])
    else:
        if not args.allow_uncontracted:
            raise ForkRunnerError(
                "strict fork runs require --fixture metadata; "
                "use --allow-uncontracted only for diagnostics")
        try:
            horizons = tuple(
                sorted({int(value) for value in args.horizons.split(",")}))
        except ValueError as exc:
            raise ForkRunnerError(
                "horizons must be comma-separated integers") from exc
    if not horizons or horizons[0] < 1 or horizons[-1] > 100000:
        raise ForkRunnerError("horizons must be within 1..100000")
    actions = (
        list(contract["inputs"][:max(horizons)]) if contract is not None
        else _actions(args.inputs, max(horizons))
    )
    raw_box = None
    if args.box is not None:
        try:
            raw_box = [int(value) for value in args.box.split(",")]
        except ValueError as exc:
            raise ForkRunnerError("--box must contain six integers") from exc
        anvil_to_capsule.CAPSULE.cell_count(raw_box)
    return run(
        args.source.resolve(), args.output.resolve(), args.instance, args.seed,
        args.username, args.world_type, horizons, actions,
        not args.no_java_repeat, args.allow_native_reject,
        None if contract is None else contract["id"],
        args.restore_source_hidden, args.keep_reload_topology, raw_box,
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        ForkRunnerError, fixture_contract.FixtureContractError,
        save_fork.SaveForkError,
        anvil_semantic.AnvilSemanticError, OSError, ValueError,
        anvil_to_capsule.AnvilImportError,
    ) as exc:
        print(f"FAIL save fork runner: {exc}", file=sys.stderr)
        raise SystemExit(1)
