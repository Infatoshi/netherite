#!/usr/bin/env python3
"""Real-Java/native firework continuation through the neutral capsule."""

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
import nbt_codec  # noqa: E402
import state_capsule  # noqa: E402
import trace_java  # noqa: E402


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def firework(authoritative, eid):
    matches = [
        entity for entity in authoritative["entities"]
        if entity.get("eid") == eid
        and entity.get("type") == "EntityFireworkRocket"
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"expected one Java firework {eid}, got {matches}")
    return matches[0]


def capsule_tick(env, args, root, label, initial, authoritative, box,
                 blocks, eids, terminal_eids=()):
    observation = dict(initial)
    observation["authoritative"] = authoritative
    # canonicalize keeps the client pose at the top level while taking most
    # server fields from authoritative.  Locked fixtures do not return a new
    # client observation, so use the captured server pose as the exact resume
    # pose instead of carrying reset-time coordinates into later capsules.
    for field in (
            "x", "y", "z", "vx", "vy", "vz", "yaw", "pitch",
            "fall_distance", "sprinting", "sneaking", "jumping",
            "dead", "deaths"):
        if field in authoritative:
            observation[field] = authoritative[field]
    state = trace_java.canonicalize(-1, observation, box)
    state_path = root / f"{label}_state.json"
    state_path.write_text(json.dumps(state), encoding="utf-8")
    capsule = root / f"{label}_capsule"
    state_capsule.create_capsule(
        state_path, blocks, box, capsule,
        seed=7331, source_engine="minecraft-java-1.11.2",
        source_version="1.11.2",
    )
    script = root / f"{label}_script.jsonl"
    state_capsule.emit_magma(capsule, script)

    java_result = env._cmd({"cmd": "server_tick_locked"})
    if not java_result.get("ok"):
        raise AssertionError(java_result)

    native_state = root / f"{label}_native.jsonl"
    native_env = os.environ.copy()
    native_env["MAGMA_CAPSULE_DIR"] = str(capsule)
    subprocess.run([
        str(args.native.resolve()),
        "--seed", "7331", "--world", "superflat",
        "--view-distance", "1", "--headless", "--ticks", "1",
        "--script", str(script), "--state-out", str(native_state),
        "--render", "off", "--pace", "unlimited",
        "--weather", "off", "--daylight", "off", "--mobs", "off",
    ], cwd=args.native.resolve().parent, env=native_env, check=True,
       stdout=subprocess.DEVNULL)
    native_raw = json.loads(
        native_state.read_text(encoding="utf-8").splitlines()[0])
    native_matches = {
        entity["eid"]: entity for entity in native_raw["entities"]
        if entity.get("kind") == "firework"
        and entity.get("eid") in eids
    }
    if set(native_matches) != set(eids):
        raise AssertionError(
            f"native firework survivor mismatch: {native_matches}")
    native_terminal = {
        entity.get("eid") for entity in native_raw["entities"]
        if entity.get("kind") == "firework"
        and entity.get("eid") in terminal_eids
    }
    if native_terminal:
        raise AssertionError(
            f"native terminal fireworks survived: {native_terminal}")
    java_matches = {
        eid: firework(java_result["authoritative"], eid)
        for eid in eids
    }
    java_terminal = {
        entity.get("eid")
        for entity in java_result["authoritative"]["entities"]
        if entity.get("type") == "EntityFireworkRocket"
        and entity.get("eid") in terminal_eids
    }
    if java_terminal:
        raise AssertionError(
            f"Java terminal fireworks survived: {java_terminal}")
    return java_result, java_matches, native_matches, native_raw


def compare(label, before, java, native):
    scalar_fields = (
        "eid", "firework_age", "lifetime", "ticks_existed",
        "attached_player", "flight", "explosion_count", "large_blast",
        "twinkle", "firework_item_present", "firework_item",
        "firework_count", "firework_meta", "entity_seed48",
        "entity_have_gaussian", "uuid_most", "uuid_least",
    )
    for field in scalar_fields:
        if field not in java or field not in native:
            raise AssertionError(
                f"{label} missing {field}: Java={java!r} "
                f"native={native!r}")
        if int(native[field]) != int(java[field]):
            raise AssertionError(
                f"{label} {field}: Java={java[field]!r} "
                f"native={native[field]!r}")
    for field in ("x", "y", "z", "vx", "vy", "vz",
                  "entity_gaussian"):
        if dbits(native[field]) != dbits(java[field]):
            raise AssertionError(
                f"{label} {field}: Java={java[field]!r} "
                f"native={native[field]!r}")
    for field in ("yaw", "pitch", "prev_yaw", "prev_pitch"):
        if fbits(native[field]) != fbits(java[field]):
            raise AssertionError(
                f"{label} {field}: Java={java[field]!r} "
                f"native={native[field]!r}")
    if native.get("firework_exact") is not True:
        raise AssertionError(f"{label} native firework lost exact marker")
    java_payload = java.get("stack_payload")
    native_payload = native.get("stack_payload")
    if not isinstance(java_payload, dict) \
            or not isinstance(native_payload, dict) \
            or nbt_codec.canonical_hex(java_payload["nbt"]) \
            != nbt_codec.canonical_hex(native_payload["nbt"]):
        raise AssertionError(
            f"{label} payload mismatch: Java={java_payload!r} "
            f"native={native_payload!r}")
    if nbt_codec.canonical_hex(before["stack_payload"]["nbt"]) \
            != nbt_codec.canonical_hex(native_payload["nbt"]):
        raise AssertionError(f"{label} changed the captured firework tag")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    args = parser.parse_args()

    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 7331,
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
    eids = (750001, 750002)
    terminal_eid = 750003
    comparisons = []
    try:
        cleared = env._cmd({"cmd": "clear_entities_locked", "action": {}})
        if not cleared.get("ok"):
            raise AssertionError(cleared)
        x = math.floor(float(initial["x"])) + 8
        z = math.floor(float(initial["z"])) + 8
        summoned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "firework",
                "eid": eids[0],
                "uuid": "00000000-0000-4000-8000-000000750001",
                "x": x + 0.5, "y": 100.0, "z": z + 0.5,
                "mx": 0.125, "my": 0.2, "mz": -0.25,
                "yaw": 37.0, "pitch": -14.0,
                "prev_yaw": 29.0, "prev_pitch": -11.0,
                "firework_age": 4, "lifetime": 30,
                "ticks_existed": 7, "flight": 2,
                "explosion_count": 4,
                "entity_seed48": 0x123456789ABC,
            },
        })
        if not summoned.get("ok"):
            raise AssertionError(summoned)
        attached = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "firework",
                "eid": eids[1],
                "uuid": "00000000-0000-4000-8000-000000750002",
                "x": x + 1.5, "y": 100.0, "z": z + 0.5,
                "mx": -0.0625, "my": 0.125, "mz": 0.25,
                "yaw": -143.0, "pitch": 31.0,
                "prev_yaw": -137.0, "prev_pitch": 27.0,
                "firework_age": 3, "lifetime": 29,
                "ticks_existed": 5, "flight": 1,
                "explosion_count": 1, "attached_player": True,
                "entity_seed48": 0x223456789ABC,
            },
        })
        if not attached.get("ok"):
            raise AssertionError(attached)
        terminal = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "firework",
                "eid": terminal_eid,
                "uuid": "00000000-0000-4000-8000-000000750003",
                "x": x - 0.5, "y": 100.0, "z": z + 0.5,
                "mx": 0.03125, "my": 0.2, "mz": -0.015625,
                "yaw": 7.0, "pitch": 61.0,
                "prev_yaw": 5.0, "prev_pitch": 59.0,
                "firework_age": 28, "lifetime": 28,
                "ticks_existed": 9, "flight": 1,
                "explosion_count": 2,
                "entity_seed48": 0x323456789ABC,
            },
        })
        if not terminal.get("ok"):
            raise AssertionError(terminal)
        before = {
            eid: firework(terminal["authoritative"], eid) for eid in eids
        }
        terminal_before = firework(
            terminal["authoritative"], terminal_eid)
        for entity in before.values():
            if entity.get("firework_exact") is not True:
                raise AssertionError(
                    f"Java firework was not exact: {entity}")
        if terminal_before.get("firework_exact") is not True:
            raise AssertionError(
                f"Java terminal firework was not exact: {terminal_before}")

        with tempfile.TemporaryDirectory(
                prefix="netherite_firework_capsule_",
                dir=temp_root) as raw_temp:
            root = pathlib.Path(raw_temp)
            box = [x - 2, 98, z - 2, x + 2, 103, z + 2]
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
            step_result, java_after, native_after, _live_native = capsule_tick(
                env, args, root, "live", initial,
                terminal["authoritative"], box, blocks, eids,
                (terminal_eid,))
            for eid in eids:
                comparisons.append((
                    f"live {eid}", before[eid], java_after[eid],
                    native_after[eid]))

            # The attached rocket followed the grounded player during the
            # live tick.  Move both rockets back into the airborne capture
            # volume before exercising vanilla's cold NBT path; attachment
            # itself is intentionally not persisted by 1.11.2.
            staged = None
            for offset, eid in enumerate(eids):
                staged = env._cmd({
                    "cmd": "set_entity_position_locked",
                    "action": {
                        "eid": eid,
                        "x": x + 0.5 + offset,
                        "y": 100.0,
                        "z": z + 0.5,
                    },
                })
                if not staged.get("ok"):
                    raise AssertionError(staged)
            reloaded = env._cmd({
                "cmd": "entity_nbt_roundtrip_locked", "action": {},
            })
            if not reloaded.get("ok") or reloaded.get("count", 0) < 1:
                raise AssertionError(reloaded)
            nbt_before = {
                eid: firework(reloaded["authoritative"], eid)
                for eid in eids
            }
            for eid, entity in nbt_before.items():
                if entity.get("firework_exact") is not True:
                    raise AssertionError(
                        f"NBT-reloaded Java firework was not exact: {entity}")
                if entity.get("attached_player"):
                    raise AssertionError(
                        f"NBT unexpectedly retained firework attachment: "
                        f"{entity}")
            _nbt_result, nbt_java_after, nbt_native_after, _nbt_native = \
                capsule_tick(
                env, args, root, "nbt", step_result,
                reloaded["authoritative"], box, blocks, eids)
            for eid in eids:
                comparisons.append((
                    f"NBT reload {eid}", nbt_before[eid],
                    nbt_java_after[eid], nbt_native_after[eid]))

            cleared = env._cmd({
                "cmd": "clear_entities_locked", "action": {},
            })
            if not cleared.get("ok"):
                raise AssertionError(cleared)
            staged_player = env._cmd({
                "cmd": "setplayer_locked",
                "action": {
                    "attack_ticks": 0,
                    "hurt_time": 0,
                    "hurt_resistant_time": 0,
                    "death_time": 0,
                    "clear_hurt": True,
                },
            })
            if not staged_player.get("ok"):
                raise AssertionError(staged_player)
            player = staged_player["authoritative"]
            damage_eid = 750004
            damage = env._cmd({
                "cmd": "summon_locked",
                "action": {
                    "type": "firework",
                    "eid": damage_eid,
                    "uuid": "00000000-0000-4000-8000-000000750004",
                    "x": player["x"], "y": player["y"],
                    "z": player["z"],
                    "mx": 0.0, "my": 0.05, "mz": 0.0,
                    "yaw": 0.0, "pitch": 90.0,
                    "prev_yaw": 0.0, "prev_pitch": 90.0,
                    "firework_age": 12, "lifetime": 12,
                    "ticks_existed": 4, "flight": 0,
                    "explosion_count": 1,
                    "attached_player": True,
                    "entity_seed48": 0x423456789ABC,
                },
            })
            if not damage.get("ok"):
                raise AssertionError(damage)
            damage_before = firework(
                damage["authoritative"], damage_eid)
            if damage_before.get("firework_exact") is not True:
                raise AssertionError(
                    f"Java damage firework was not exact: {damage_before}")
            damage_x = math.floor(player["x"])
            damage_y = math.floor(player["y"])
            damage_z = math.floor(player["z"])
            damage_box = [
                damage_x - 2, damage_y - 2, damage_z - 2,
                damage_x + 2, damage_y + 3, damage_z + 2,
            ]
            damage_blocks = root / "damage_blocks.u16le"
            got = env._cmd({
                "cmd": "getblocks_locked",
                "action": {
                    "x0": damage_box[0], "y0": damage_box[1],
                    "z0": damage_box[2], "x1": damage_box[3],
                    "y1": damage_box[4], "z1": damage_box[5],
                    "file": str(damage_blocks),
                },
            })
            if not got.get("ok"):
                raise AssertionError(got)
            damage_result, _java_none, _native_none, damage_native = \
                capsule_tick(
                    env, args, root, "damage", staged_player,
                    damage["authoritative"], damage_box, damage_blocks,
                    (), (damage_eid,))
            damage_java = damage_result["authoritative"]
            for field in ("health", "absorption"):
                if fbits(damage_native[field]) != fbits(
                        damage_java[field]):
                    raise AssertionError(
                        f"terminal damage {field}: "
                        f"Java={damage_java[field]!r} "
                        f"native={damage_native[field]!r}")
            for field in ("hurt_time", "hurt_resistant_time"):
                if int(damage_native[field]) != int(damage_java[field]):
                    raise AssertionError(
                        f"terminal damage {field}: "
                        f"Java={damage_java[field]!r} "
                        f"native={damage_native[field]!r}")
            if not damage_java["health"] < player["health"]:
                raise AssertionError(
                    "terminal attached firework did not damage Java player")
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")

    for comparison in comparisons:
        compare(*comparison)
    print("firework capsule oracle: PASS "
          "(free/attached live, terminal damage, and NBT-reloaded arbitrary "
          "payloads -> native restore -> exact full-state tick)")


if __name__ == "__main__":
    main()
