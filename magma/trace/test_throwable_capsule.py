#!/usr/bin/env python3
"""Real-Java/native continuation for all non-potion EntityThrowables."""

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
MAGMA = TRACE.parent
JAVA = TRACE.parents[1] / "java"
sys.path.insert(0, str(JAVA))
sys.path.insert(0, str(TRACE))

from qrl_client import NetheriteEnv  # noqa: E402
import state_capsule  # noqa: E402
import trace_java  # noqa: E402


CASES = (
    ("egg", "EntityEgg", 7, 0.25, 0.125, -0.125),
    ("snowball", "EntitySnowball", 8, -0.125, 0.25, 0.375),
    ("xp_bottle", "EntityExpBottle", 9, 0.375, -0.05, 0.125),
    ("ender_pearl", "EntityEnderPearl", 12, -0.25, 0.175, -0.25),
)


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def projectile_entities(authoritative, eids):
    return {
        entity["type"]: entity
        for entity in authoritative["entities"]
        if entity.get("eid") in eids.values()
    }


def capsule_tick(env, args, root, label, observation, before, box, blocks):
    state = trace_java.canonicalize(-1, observation, box)
    state_path = root / f"{label}_state.json"
    state_path.write_text(json.dumps(state), encoding="utf-8")
    capsule = root / f"{label}_capsule"
    state_capsule.create_capsule(
        state_path, blocks, box, capsule,
        seed=303, source_engine="minecraft-java-1.11.2",
        source_version="1.11.2",
    )
    script = root / f"{label}_script.jsonl"
    state_capsule.emit_magma(capsule, script)
    after_result = env._cmd({"cmd": "step"})
    if not after_result.get("ok"):
        raise AssertionError(after_result)
    native_state = root / f"{label}_native.jsonl"
    native_env = os.environ.copy()
    native_env["MAGMA_CAPSULE_DIR"] = str(capsule)
    subprocess.run([
        str(args.native.resolve()),
        "--seed", "303", "--world", "superflat",
        "--view-distance", "1", "--headless", "--ticks", "1",
        "--script", str(script), "--state-out", str(native_state),
        "--render", "off", "--pace", "unlimited",
        "--weather", "off", "--daylight", "off", "--mobs", "off",
    ], cwd=args.native.resolve().parent,
       env=native_env, check=True, stdout=subprocess.DEVNULL)
    native_raw = json.loads(
        native_state.read_text(encoding="utf-8").splitlines()[0])
    native = {
        value["type"]: value
        for value in native_raw["entities"]
        if value.get("kind") == "projectile"
        and value.get("type") in {7, 8, 9, 12}
    }
    return after_result, projectile_entities(
        after_result["authoritative"], {
            entity_type: entity["eid"] for entity_type, entity
            in before.items()
        }), native


def compare_continuation(label, before, after, native, eids):
    if set(after) != set(eids) or set(native) != {7, 8, 9, 12}:
        raise AssertionError(
            f"{label} survivor mismatch: Java={after} native={native}")
    scalar_fields = (
        "eid", "age", "ticks_in_air", "player_thrower",
        "thrower_player_pending",
        "ignore_player", "ignore_player_time", "pearl_private_thrower",
        "throwable_shake", "in_ground", "ticks_in_ground",
        "tile_x", "tile_y", "tile_z", "tile_block",
        "portal_counter", "in_portal", "portal_cooldown",
        "last_portal_pos_valid", "last_portal_x", "last_portal_y",
        "last_portal_z", "teleport_direction",
        "entity_seed48", "entity_have_gaussian",
        "uuid_most", "uuid_least",
    )
    for _kind, java_type, native_type, _vx, _vy, _vz in CASES:
        expected = after[java_type]
        actual = native[native_type]
        for field in scalar_fields:
            if int(actual[field]) != int(expected[field]):
                raise AssertionError(
                    f"{label} {java_type} {field}: "
                    f"Java={expected[field]!r} native={actual[field]!r}")
        for field in (
                "x", "y", "z", "vx", "vy", "vz",
                "last_portal_vec_x", "last_portal_vec_y",
                "entity_gaussian"):
            if dbits(actual[field]) != dbits(expected[field]):
                raise AssertionError(
                    f"{label} {java_type} {field}: "
                    f"Java={expected[field]!r} native={actual[field]!r}")
        for field in ("yaw", "pitch", "prev_yaw", "prev_pitch"):
            if fbits(actual[field]) != fbits(expected[field]):
                raise AssertionError(
                    f"{label} {java_type} {field}: "
                    f"Java={expected[field]!r} native={actual[field]!r}")
        for field in ("client_random_valid", "client_entity_seed48"):
            captured = before[java_type][field]
            if int(actual[field]) != int(captured):
                raise AssertionError(
                    f"{label} {java_type} captured {field}: "
                    f"Java={captured!r} native={actual[field]!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 303,
        "mode": "survival",
        "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    temp_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA.parent / ".tmp")))
    temp_root.mkdir(parents=True, exist_ok=True)
    try:
        cleared = env._cmd({"cmd": "clear_entities_locked", "action": {}})
        if not cleared.get("ok"):
            raise AssertionError(cleared)
        base_x = math.floor(float(initial["x"])) + 20
        base_z = math.floor(float(initial["z"])) + 20
        staged = env._cmd({
            "cmd": "setblocks_locked",
            "action": {"blocks": [[base_x, 200, base_z, 1, 0]]},
        })
        if not staged.get("ok"):
            raise AssertionError(staged)
        authoritative = None
        eids = {}
        for index, (kind, java_type, _native_type, vx, vy, vz) in enumerate(
                CASES):
            eid = 740000 + index
            result = env._cmd({
                "cmd": "summon_locked",
                "action": {
                    "type": kind,
                    "eid": eid,
                    "uuid": f"00000000-0000-4000-8000-{eid:012d}",
                    "entity_seed48": 0x123450000000 + index,
                    "x": base_x + index * 4.0 + 0.5,
                    "y": 200.0,
                    "z": base_z + 0.5,
                    "mx": vx, "my": vy, "mz": vz,
                    "yaw": -130.0 + index * 71.25,
                    "pitch": -33.0 + index * 18.5,
                    "ticks_existed": 3 + index,
                    "throwable_shake": 2 if index == 2 else 0,
                    "in_ground": index == 0,
                    "ticks_in_ground": 119 if index == 0 else 0,
                    "ticks_in_air": 7 + index,
                    "tile_x": base_x if index == 0 else -1,
                    "tile_y": 200 if index == 0 else -1,
                    "tile_z": base_z if index == 0 else -1,
                    "tile_block": 1 if index == 0 else 0,
                    "portal_counter": 1 if index == 1 else 0,
                    "portal_cooldown": 13 if index == 3 else 0,
                },
            })
            if not result.get("ok"):
                raise AssertionError(result)
            authoritative = result["authoritative"]
            eids[java_type] = eid
        before = projectile_entities(authoritative, eids)
        if set(before) != set(eids):
            raise AssertionError(f"missing Java throwables: {before}")
        for java_type, entity in before.items():
            if entity.get("throwable_exact") is not True:
                raise AssertionError(
                    f"{java_type} was not capsule-exact: {entity}")
            expected_private = java_type == "EntityEnderPearl"
            if entity.get("pearl_private_thrower") is not expected_private:
                raise AssertionError(
                    f"{java_type} pearl thrower state: {entity}")

        with tempfile.TemporaryDirectory(
                prefix="netherite_throwable_capsule_",
                dir=temp_root) as raw_temp:
            root = pathlib.Path(raw_temp)
            x0 = math.floor(min(entity["x"] for entity in before.values())) - 2
            x1 = math.floor(max(entity["x"] for entity in before.values())) + 2
            z0 = math.floor(min(entity["z"] for entity in before.values())) - 2
            z1 = math.floor(max(entity["z"] for entity in before.values())) + 2
            box = [x0, 198, z0, x1, 202, z1]
            blocks = root / "blocks.u16le"
            got = env._cmd({
                "cmd": "getblocks_locked",
                "action": {
                    "x0": box[0], "y0": box[1], "z0": box[2],
                    "x1": box[3], "y1": box[4], "z1": box[5],
                    "file": str(blocks),
                },
            })
            if not got.get("ok"):
                raise AssertionError(got)
            observation = dict(initial)
            observation["authoritative"] = authoritative
            after_result, after, native = capsule_tick(
                env, args, root, "live", observation, before, box, blocks)
            comparisons = [("live", before, after, native)]

            reloaded = env._cmd({
                "cmd": "entity_nbt_roundtrip_locked", "action": {},
            })
            if not reloaded.get("ok") or reloaded.get("count", 0) < 4:
                raise AssertionError(reloaded)
            nbt_before = projectile_entities(
                reloaded["authoritative"], eids)
            if set(nbt_before) != set(eids):
                raise AssertionError(
                    f"missing NBT-reloaded throwables: {nbt_before}")
            for java_type, entity in nbt_before.items():
                if entity.get("throwable_exact") is not True:
                    raise AssertionError(
                        f"NBT-reloaded {java_type} was not exact: {entity}")
            nbt_observation = dict(after_result)
            nbt_observation["authoritative"] = reloaded["authoritative"]
            _nbt_result, nbt_after, nbt_native = capsule_tick(
                env, args, root, "nbt", nbt_observation,
                nbt_before, box, blocks)
            comparisons.append(
                ("NBT reload", nbt_before, nbt_after, nbt_native))
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")

    for comparison in comparisons:
        compare_continuation(*comparison, eids)
    print("throwable capsule oracle: PASS "
          "(" + str(len(comparisons) * 4)
          + " live/NBT-reloaded Java captures -> native restore -> "
          "exact full-state tick)")


if __name__ == "__main__":
    main()
