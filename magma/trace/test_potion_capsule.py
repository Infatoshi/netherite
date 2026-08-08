#!/usr/bin/env python3
"""Real-Java/native one-tick continuation from a potion/cloud capsule."""

import argparse
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile


TRACE = pathlib.Path(__file__).resolve().parent
JAVA = TRACE.parents[1] / "java"
sys.path.insert(0, str(JAVA))
sys.path.insert(0, str(TRACE))

from qrl_client import NetheriteEnv  # noqa: E402
import state_capsule  # noqa: E402
import trace_java  # noqa: E402


def entity(values, entity_type):
    matches = [value for value in values if value.get("type") == entity_type]
    if len(matches) != 1:
        raise AssertionError(
            f"expected one {entity_type}, got {len(matches)}: {values}")
    return matches[0]


def double_bits(value):
    return struct.pack(">d", float(value)).hex()


def float_bits(value):
    return struct.pack(">f", float(value)).hex()


def run_native_continuation(initial, before, native_path):
    observation = dict(initial)
    observation["authoritative"] = before
    for field in ("x", "y", "z", "vx", "vy", "vz", "fall_distance"):
        if field in before:
            observation[field] = before[field]
    state = trace_java.canonicalize(-1, observation)
    potion = entity(before["entities"], "EntityPotion")
    with tempfile.TemporaryDirectory(
            prefix="netherite_potion_capsule_") as temp:
        root = pathlib.Path(temp)
        state_path = root / "state.json"
        state_path.write_text(json.dumps(state), encoding="utf-8")
        blocks_path = root / "blocks.u16le"
        blocks_path.write_bytes(struct.pack("<H", 0))
        x = math.floor(potion["x"])
        y = math.floor(potion["y"])
        z = math.floor(potion["z"])
        capsule = root / "capsule"
        state_capsule.create_capsule(
            state_path, blocks_path, [x, y, z, x, y, z], capsule,
            seed=101, source_engine="minecraft-java-1.11.2",
            source_version="1.11.2",
        )
        events = state_capsule.magma_events(capsule)
        events.append({
            "tick": 0, "type": "action",
            "forward": 0, "strafe": 0, "jump": 0, "sneak": 0,
            "sprint": 0, "attack": 0, "use": 0,
            "do_break": 0, "do_place": 0,
            "dyaw": 0, "dpitch": 0, "hotbar": -1,
            "close_container": 0,
        })
        script_path = root / "script.jsonl"
        script_path.write_text(
            "".join(json.dumps(row, separators=(",", ":")) + "\n"
                    for row in events),
            encoding="utf-8",
        )
        state_out = root / "native.jsonl"
        native_env = os.environ.copy()
        native_env["MAGMA_CAPSULE_DIR"] = str(capsule)
        subprocess.run([
            str(native_path.resolve()),
            "--seed", "101", "--world", "superflat",
            "--view-distance", "1", "--headless", "--ticks", "1",
            "--script", str(script_path), "--state-out", str(state_out),
            "--render", "off", "--pace", "unlimited",
            "--weather", "off", "--daylight", "off", "--mobs", "off",
        ], cwd=native_path.resolve().parent, env=native_env, check=True)
        return json.loads(
            state_out.read_text(encoding="utf-8").splitlines()[0])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()

    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 101,
        "mode": "survival",
        "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        result = env._cmd({
            "cmd": "potion_capsule_locked",
            "action": {
                "next_entity_id": 680000,
                "player_effects": [
                    {"id": 16, "amp": 1, "dur": 41,
                     "ambient": True, "show_particles": False},
                    {"id": 24, "amp": 0, "dur": 29,
                     "ambient": False, "show_particles": True},
                ],
            },
        })
        if not result.get("ok"):
            raise AssertionError(result)
        blocked_result = env._cmd({
            "cmd": "potion_capsule_locked",
            "action": {
                "next_entity_id": 680100,
                "cloud_update_blocked": True,
            },
        })
        if not blocked_result.get("ok"):
            raise AssertionError(blocked_result)
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")

    before = result["before"]
    after = result["after"]
    java_potion_before = entity(before["entities"], "EntityPotion")
    java_cloud_before = entity(
        before["entities"], "EntityAreaEffectCloud")
    java_cow_before = entity(before["entities"], "EntityCow")
    expected_player_before = [
        {"id": 16, "amp": 1, "dur": 41,
         "ambient": True, "show_particles": False},
        {"id": 24, "amp": 0, "dur": 29,
         "ambient": False, "show_particles": True},
    ]
    expected_player_after = [
        {**effect, "dur": effect["dur"] - 1}
        for effect in expected_player_before
    ]
    if before["potions"] != expected_player_before:
        raise AssertionError(
            f"Java player potion fixture mismatch: {before['potions']!r}")
    if after["potions"] != expected_player_after:
        raise AssertionError(
            f"Java player potion tick mismatch: {after['potions']!r}")
    if java_potion_before.get("potion_exact") is not True:
        raise AssertionError(f"Java potion was not exact: {java_potion_before}")
    if java_potion_before.get("throwable_exact") is not True:
        raise AssertionError(
            f"Java potion lacked exact throwable state: "
            f"{java_potion_before}")
    if java_cloud_before.get("cloud_exact") is not True:
        raise AssertionError(f"Java cloud was not exact: {java_cloud_before}")
    if java_cloud_before.get("cloud_common_exact") is not True:
        raise AssertionError(
            f"Java cloud lacked common Entity state: {java_cloud_before}")
    expected_potion_effects = [
        {"id": 12, "amp": 0, "dur": 401, "flags": 1},
        {"id": 19, "amp": 1, "dur": 433, "flags": 2},
    ]
    expected_cloud_effects = [
        {"id": 14, "amp": 0, "dur": 377, "flags": 1},
        {"id": 22, "amp": 1, "dur": 289, "flags": 2},
    ]
    if java_potion_before.get("potion_effects") != expected_potion_effects \
            or java_potion_before.get("potion_color") != 0x315A7D \
            or java_potion_before.get("potion_custom_color") is not True \
            or "stack_payload" not in java_potion_before:
        raise AssertionError(
            f"Java custom potion payload mismatch: {java_potion_before}")
    if java_cloud_before.get("potion_effects") != expected_cloud_effects \
            or java_cloud_before.get("potion_color") != 0x6A2C91 \
            or java_cloud_before.get("potion_custom_color") is not True \
            or java_cloud_before.get("duration_on_use") != -37 \
            or java_cloud_before.get("ignore_radius") is not True \
            or java_cloud_before.get("particle") != 37 \
            or java_cloud_before.get("particle_param1") != 1 \
            or java_cloud_before.get("particle_param2") != 73 \
            or double_bits(java_cloud_before.get("vx")) != \
                double_bits(0.375) \
            or double_bits(java_cloud_before.get("vy")) != \
                double_bits(-0.0625) \
            or double_bits(java_cloud_before.get("vz")) != \
                double_bits(-0.21875) \
            or float_bits(java_cloud_before.get("yaw")) != \
                float_bits(31.25) \
            or float_bits(java_cloud_before.get("pitch")) != \
                float_bits(-12.5) \
            or float_bits(java_cloud_before.get("prev_yaw")) != \
                float_bits(29.75) \
            or float_bits(java_cloud_before.get("prev_pitch")) != \
                float_bits(-11.0) \
            or java_cloud_before.get("next_application") != 12 \
            or java_cloud_before.get("player_owner") is not False \
            or java_cloud_before.get("owner_present") is not True \
            or java_cloud_before.get("owner_eid") != \
                java_cow_before["eid"] \
            or java_cloud_before.get("owner_uuid_most") != \
                java_cow_before["uuid_most"] \
            or java_cloud_before.get("owner_uuid_least") != \
                java_cow_before["uuid_least"] \
            or java_cloud_before.get("reapplication_deadlines") != [
                {"eid": before["player_eid"], "deadline": 12},
                {"eid": java_cow_before["eid"], "deadline": 33},
            ] \
            or java_cloud_before.get("air") != 123 \
            or java_cloud_before.get("fire") != 17 \
            or java_cloud_before.get("portal_cooldown") != 7 \
            or java_cloud_before.get("fall_distance") != 3.25 \
            or java_cloud_before.get("first_update") is not True \
            or java_cloud_before.get("server_entity_seed48") != \
                0x1234ABCD5678 \
            or java_cloud_before.get("server_entity_have_gaussian") \
                is not True \
            or java_cloud_before.get("server_entity_gaussian") != \
                -0.78125:
        raise AssertionError(
            f"Java custom cloud payload mismatch: {java_cloud_before}")

    java_potion_after = entity(after["entities"], "EntityPotion")
    java_cloud_after = entity(after["entities"], "EntityAreaEffectCloud")
    expected_cloud_deadlines_after = [
        {"eid": java_cow_before["eid"], "deadline": 33},
    ]
    if java_cloud_after.get("reapplication_deadlines") != \
            expected_cloud_deadlines_after:
        raise AssertionError(
            "Java cloud deadline pruning mismatch: "
            f"{java_cloud_after.get('reapplication_deadlines')!r}")

    native = run_native_continuation(initial, before, args.native)

    native_potion = next(
        value for value in native["entities"]
        if value.get("kind") == "projectile" and value.get("type") == 6)
    native_cloud = next(
        value for value in native["entities"]
        if value.get("kind") == "area_effect_cloud")
    if native["potions"] != expected_player_after:
        raise AssertionError(
            f"player potion continuation: Java={expected_player_after!r} "
            f"native={native['potions']!r}")

    potion_scalars = (
        "eid", "potion_item", "potion_type", "age", "ticks_in_air",
        "player_thrower", "ignore_player", "ignore_player_time",
        "pearl_private_thrower", "portal_cooldown", "entity_seed48",
        "last_portal_pos_valid", "last_portal_x", "last_portal_y",
        "last_portal_z", "teleport_direction",
        "entity_have_gaussian", "uuid_most", "uuid_least",
        "potion_color", "potion_custom_color",
    )
    for field in potion_scalars:
        expected = int(java_potion_after[field])
        if native_potion[field] != expected:
            raise AssertionError(
                f"potion {field}: Java={expected} native={native_potion[field]}")
    for field in (
            "x", "y", "z", "vx", "vy", "vz",
            "last_portal_vec_x", "last_portal_vec_y"):
        if double_bits(native_potion[field]) != double_bits(
                java_potion_after[field]):
            raise AssertionError(
                f"potion {field}: Java={java_potion_after[field]!r} "
                f"native={native_potion[field]!r}")
    for field in ("yaw", "pitch", "prev_yaw", "prev_pitch"):
        if float_bits(native_potion[field]) != float_bits(
                java_potion_after[field]):
            raise AssertionError(
                f"potion {field}: Java={java_potion_after[field]!r} "
                f"native={native_potion[field]!r}")
    if double_bits(native_potion["entity_gaussian"]) != double_bits(
            java_potion_after["entity_gaussian"]):
        raise AssertionError(
            "potion entity_gaussian: "
            f"Java={java_potion_after['entity_gaussian']!r} "
            f"native={native_potion['entity_gaussian']!r}")
    for field in ("potion_effects", "stack_payload"):
        native_value = native_potion.get(field)
        java_value = java_potion_after.get(field)
        if field == "stack_payload":
            native_value = trace_java.canonical_stack_payload(native_value)
            java_value = trace_java.canonical_stack_payload(java_value)
        if native_value != java_value:
            raise AssertionError(
                f"potion {field}: Java={java_value!r} "
                f"native={native_value!r}")

    cloud_scalars = (
        "eid", "potion_type", "age", "duration", "wait_time",
        "reapplication_delay", "next_application", "player_owner",
        "potion_color", "potion_custom_color", "duration_on_use",
        "ignore_radius", "particle", "particle_param1", "particle_param2",
        "uuid_most", "uuid_least", "owner_present", "owner_eid",
        "owner_uuid_most", "owner_uuid_least",
        "dimension", "air", "fire", "portal_cooldown", "on_ground",
        "no_gravity", "invulnerable", "silent", "glowing",
        "update_blocked", "in_water", "first_update",
        "server_entity_seed48", "server_entity_have_gaussian",
    )
    for field in cloud_scalars:
        expected = int(java_cloud_after[field])
        if native_cloud[field] != expected:
            raise AssertionError(
                f"cloud {field}: Java={expected} native={native_cloud[field]}")
    for field in (
            "x", "y", "z", "vx", "vy", "vz",
            "prev_x", "prev_y", "prev_z",
            "last_tick_x", "last_tick_y", "last_tick_z",
            "server_entity_gaussian"):
        if double_bits(native_cloud[field]) != double_bits(
                java_cloud_after[field]):
            raise AssertionError(
                f"cloud {field}: Java={java_cloud_after[field]!r} "
                f"native={native_cloud[field]!r}")
    for field in ("yaw", "pitch", "prev_yaw", "prev_pitch"):
        if float_bits(native_cloud[field]) != float_bits(
                java_cloud_after[field]):
            raise AssertionError(
                f"cloud {field}: Java={java_cloud_after[field]!r} "
                f"native={native_cloud[field]!r}")
    for field in (
            "radius", "radius_on_use", "radius_per_tick",
            "fall_distance"):
        if float_bits(native_cloud[field]) != float_bits(
                java_cloud_after[field]):
            raise AssertionError(
                f"cloud {field}: Java={java_cloud_after[field]!r} "
                f"native={native_cloud[field]!r}")
    if native_cloud.get("potion_effects") != \
            java_cloud_after.get("potion_effects"):
        raise AssertionError(
            f"cloud potion_effects: Java="
            f"{java_cloud_after.get('potion_effects')!r} native="
            f"{native_cloud.get('potion_effects')!r}")
    if native_cloud.get("reapplication_deadlines") != \
            expected_cloud_deadlines_after:
        raise AssertionError(
            f"cloud reapplication_deadlines: Java="
            f"{expected_cloud_deadlines_after!r} native="
            f"{native_cloud.get('reapplication_deadlines')!r}")

    blocked_before = blocked_result["before"]
    blocked_after = blocked_result["after"]
    java_blocked_before = entity(
        blocked_before["entities"], "EntityAreaEffectCloud")
    java_blocked_after = entity(
        blocked_after["entities"], "EntityAreaEffectCloud")
    if java_blocked_before.get("cloud_common_exact") is not True \
            or java_blocked_before.get("update_blocked") is not True \
            or java_blocked_before.get("age") != 14 \
            or java_blocked_after.get("age") != 15 \
            or java_blocked_after.get("fire") != 17 \
            or java_blocked_after.get("portal_cooldown") != 7 \
            or java_blocked_after.get("first_update") is not True \
            or java_blocked_after.get("ignore_radius") is not True \
            or float_bits(java_blocked_after.get("radius")) != \
                float_bits(java_blocked_before.get("radius")) \
            or java_blocked_after.get("reapplication_deadlines") != \
                java_blocked_before.get("reapplication_deadlines"):
        raise AssertionError(
            "Java UpdateBlocked cloud boundary mismatch: "
            f"before={java_blocked_before!r} after={java_blocked_after!r}")
    native_blocked = run_native_continuation(
        initial, blocked_before, args.native)
    native_blocked_cloud = next(
        value for value in native_blocked["entities"]
        if value.get("kind") == "area_effect_cloud")
    for field in cloud_scalars:
        expected = int(java_blocked_after[field])
        if native_blocked_cloud[field] != expected:
            raise AssertionError(
                f"blocked cloud {field}: Java={expected} "
                f"native={native_blocked_cloud[field]}")
    for field in (
            "x", "y", "z", "vx", "vy", "vz",
            "prev_x", "prev_y", "prev_z",
            "last_tick_x", "last_tick_y", "last_tick_z",
            "server_entity_gaussian"):
        if double_bits(native_blocked_cloud[field]) != double_bits(
                java_blocked_after[field]):
            raise AssertionError(
                f"blocked cloud {field}: "
                f"Java={java_blocked_after[field]!r} "
                f"native={native_blocked_cloud[field]!r}")
    for field in (
            "yaw", "pitch", "prev_yaw", "prev_pitch",
            "radius", "radius_on_use", "radius_per_tick",
            "fall_distance"):
        if float_bits(native_blocked_cloud[field]) != float_bits(
                java_blocked_after[field]):
            raise AssertionError(
                f"blocked cloud {field}: "
                f"Java={java_blocked_after[field]!r} "
                f"native={native_blocked_cloud[field]!r}")
    for field in ("potion_effects", "reapplication_deadlines"):
        if native_blocked_cloud.get(field) != java_blocked_after.get(field):
            raise AssertionError(
                f"blocked cloud {field}: "
                f"Java={java_blocked_after.get(field)!r} "
                f"native={native_blocked_cloud.get(field)!r}")

    print("potion capsule oracle: PASS "
          "(custom payloads + real-Java capture -> native restore -> one "
          "exact full-state tick, including nonzero cloud kinematics and "
          "multi-living reapplication deadlines plus non-player owner "
          "identity, common Entity continuation state, and UpdateBlocked "
          "world-wrapper behavior)")


if __name__ == "__main__":
    main()
