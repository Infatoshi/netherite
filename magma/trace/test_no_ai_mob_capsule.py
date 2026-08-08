#!/usr/bin/env python3
"""Real-Java/NBT/capsule continuation and living/XP loaded order."""

import argparse
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(MAGMA.parent / "java"))

from state_capsule import create_capsule, emit_magma
from test_dragon_crystal_notification import request
from trace_java import canonicalize
from qrl_client import NetheriteEnv


FIXTURES = (
    (2, "EntityZombie"),
    (3, "EntitySkeleton"),
    (32, "EntityWitherSkeleton"),
    (4, "EntityCreeper"),
    (5, "EntitySpider"),
    (39, "EntityCaveSpider"),
    (6, "EntityEnderman"),
    (7, "EntityBlaze"),
    (23, "EntityWitch"),
    (26, "EntityGhast"),
    (41, "EntityZombieVillager"),
    (51, "EntityVindicator"),
    (52, "EntityEvoker"),
    (53, "EntityVex"),
    (55, "EntityGuardian"),
    (56, "EntityElderGuardian"),
    (10, "EntitySheep"),
    (13, "EntityChicken"),
    (14, "EntitySquid"),
    (35, "EntitySlime"),
    (27, "EntityMagmaCube"),
    (15, "EntityPigZombie"),
    (36, "EntitySilverfish"),
    (12, "EntityCow"),
    (57, "EntityIronGolem"),
    (58, "EntityStray"),
    (59, "EntityHusk"),
    (60, "EntityMooshroom"),
    (61, "EntityRabbit"),
    (62, "EntityPolarBear"),
    (24, "EntityBat"),
    (63, "EntityEndermite"),
    (64, "EntitySnowman"),
    (65, "EntityGiantZombie"),
)
CONTINUATION_TICKS = 20


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def by_eid(values, eid):
    matches = [value for value in values if value.get("eid") == eid]
    if len(matches) != 1:
        raise AssertionError(f"expected eid {eid}, got {matches!r}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game"], check=True,
        stdout=subprocess.DEVNULL,
    )
    temp_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA.parent / ".tmp")))
    temp_root.mkdir(parents=True, exist_ok=True)
    locked = False
    with tempfile.TemporaryDirectory(
            prefix="no_ai_mob_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        reset_env = NetheriteEnv("127.0.0.1", args.port)
        try:
            obs = reset_env.reset({
                "seed": 0, "mode": "survival", "type": "flat",
                "structures": False,
            })
        finally:
            reset_env.close()
        if not obs.get("ok"):
            raise AssertionError(f"oracle reset failed: {obs!r}")
        rule = request(args.port, "runcmds", {
            "cmds": ["gamerule doMobSpawning false"],
        })
        if not rule.get("ok") or rule.get("failed"):
            raise AssertionError(f"could not disable natural spawning: {rule}")
        request(args.port, "killentities")
        spawned = []
        orb_limit = max(0, 64 - len(FIXTURES) - 1)
        try:
            locked_state = request(args.port, "server_step_lock")
            locked = True
            base_x = float(locked_state["authoritative"]["x"])
            base_y = float(locked_state["authoritative"]["y"]) + 32.0
            base_z = float(locked_state["authoritative"]["z"])
            orbs = []
            sentinels = []
            for index, (entity_type, name) in enumerate(FIXTURES):
                x = base_x + (index % 8 - 3.5) * 8.0
                z = base_z + (index // 8 + 1) * 8.0
                fixture = {
                    "type": "no_ai_mob", "entity_type": entity_type,
                    "x": x, "y": base_y, "z": z,
                    "mx": 0.125, "my": 0.25, "mz": -0.0625,
                    "entity_seed48": 0x123456789ABC + index,
                }
                if entity_type in (27, 35):
                    fixture.update({
                        "size": 4,
                        "squish_amount": 0.8,
                        "squish_factor": 0.4,
                        "prev_squish_factor": 0.2,
                        "was_on_ground": False,
                    })
                elif entity_type == 24:
                    fixture["bat_hanging"] = False
                elif entity_type == 63:
                    fixture.update({
                        "endermite_lifetime": 237,
                        "endermite_player_spawned": True,
                        "endermite_persistence_required": False,
                    })
                elif entity_type == 64:
                    fixture["snowman_pumpkin"] = False
                elif entity_type == 12:
                    fixture["effects"] = [
                        {"id": 1, "amp": 1, "dur": 401,
                         "ambient": True, "show_particles": False},
                        {"id": 21, "amp": 1, "dur": 389,
                         "ambient": False, "show_particles": True},
                        {"id": 22, "amp": 1, "dur": 379,
                         "ambient": False, "show_particles": True},
                        {"id": 24, "amp": 0, "dur": 367,
                         "ambient": False, "show_particles": True},
                    ]
                value = request(args.port, "summon_locked", fixture)
                spawned.append((value["eid"], entity_type, name))
                # Interleave a second entity class in loadedEntityList. XP
                # has a different NBT reset surface and native storage, so
                # this turns the continuation into a causal ordering proof
                # rather than a homogeneous insertion-order check.
                # The authoritative recorder has a 64-entity observation
                # cap. Reserve one slot for the interleaved Item sentinel.
                if index < orb_limit:
                    orb = request(args.port, "summon_locked", {
                        "type": "xporb", "x": x + 4.0,
                        "y": base_y + 1.0, "z": z,
                        "mx": -0.03125, "my": 0.125,
                        "mz": 0.0625, "value": index % 7 + 1,
                        "pickup_delay": index % 5,
                        "target_color": -100 - index,
                    })
                    orbs.append(orb["eid"])
                if index == min(12, len(FIXTURES) // 2):
                    sentinel = request(args.port, "summon_locked", {
                        "type": "item", "x": x + 6.0,
                        "y": base_y + 2.0, "z": z,
                        "mx": 0.015625, "my": 0.0, "mz": -0.015625,
                        "item": 1, "count": 1, "meta": 0,
                        "pickup_delay": 32767,
                    })
                    sentinels.append(sentinel["eid"])
            authoritative = None
            for _ in range(3):
                authoritative = request(args.port, "step")["authoritative"]
            if authoritative is None:
                raise AssertionError("oracle did not advance")
            pre_roundtrip_order = {
                entity["eid"]: entity["loaded_order"]
                for entity in authoritative["entities"]
            }
            roundtrip = request(args.port, "entity_nbt_roundtrip_locked")
            if roundtrip.get("count") != len(FIXTURES) + len(orbs):
                raise AssertionError(
                    "NBT round-trip covered "
                    f"{roundtrip.get('count')} entities")
            post_roundtrip_order = {
                entity["eid"]: entity["loaded_order"]
                for entity in roundtrip["authoritative"]["entities"]
            }
            preserved_eids = [eid for eid, _, _ in spawned] + orbs + sentinels
            missing_before = [
                eid for eid in preserved_eids
                if eid not in pre_roundtrip_order
            ]
            missing_after = [
                eid for eid in preserved_eids
                if eid not in post_roundtrip_order
            ]
            if missing_before or missing_after or any(
                    pre_roundtrip_order[eid] != post_roundtrip_order[eid]
                    for eid in preserved_eids
                    if eid in pre_roundtrip_order
                    and eid in post_roundtrip_order):
                raise AssertionError(
                    "fresh-object replacement changed the complete captured "
                    f"loaded order; missing before={missing_before!r} "
                    f"after={missing_after!r}; spawned={spawned!r}; "
                    f"orbs={orbs!r}; sentinels={sentinels!r}")
            # A real fresh NBT load resets Entity.ticksExisted. Let the new
            # instances cross the recorder's two-tick stable-state fence
            # before making the continuation checkpoint.
            for _ in range(2):
                authoritative = request(args.port, "step")["authoritative"]
            entities = authoritative["entities"]
            for eid, _entity_type, name in spawned:
                entity = by_eid(entities, eid)
                if entity.get("type") != name \
                        or entity.get("no_ai_plain_exact") is not True:
                    raise AssertionError(
                        f"Java {name} is not capsule-exact: {entity!r}")

            selected = [by_eid(entities, eid) for eid, _, _ in spawned]
            selected.extend(by_eid(entities, eid) for eid in orbs)
            selected.extend(by_eid(entities, eid) for eid in sentinels)
            box = [
                math.floor(min(value["x"] for value in selected)) - 5,
                math.floor(min(value["y"] for value in selected)) - 5,
                math.floor(min(value["z"] for value in selected)) - 5,
                math.floor(max(value["x"] for value in selected)) + 5,
                math.floor(max(value["y"] for value in selected)) + 6,
                math.floor(max(value["z"] for value in selected)) + 5,
            ]
            blocks = temp / "blocks.bin"
            request(args.port, "getblocks_locked", {
                "x0": box[0], "y0": box[1], "z0": box[2],
                "x1": box[3], "y1": box[4], "z1": box[5],
                "file": str(blocks),
            })
            observation = dict(obs)
            observation["authoritative"] = authoritative
            state = canonicalize(-1, observation, box)
            state_file = temp / "state.json"
            state_file.write_text(json.dumps(state), encoding="utf-8")
            capsule = temp / "capsule"
            create_capsule(
                state_file, blocks, box, capsule, seed=42,
                source_engine="minecraft-java", source_version="1.11.2",
            )
            script = temp / "load.jsonl"
            emit_magma(capsule, script)
            java_states = [
                request(args.port, "step")["authoritative"]
                for _ in range(CONTINUATION_TICKS)
            ]
        finally:
            if locked:
                request(args.port, "server_step_unlock")
            request(args.port, "runcmds", {
                "cmds": ["gamerule doMobSpawning true"],
            })

        native_state = temp / "native.jsonl"
        subprocess.run([
            str(MAGMA / "magma_game"),
            "--world", "superflat", "--headless", "--ticks",
            str(CONTINUATION_TICKS),
            "--mobs", "off", "--script", str(script),
            "--state-out", str(native_state),
            "--render", "off", "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL)
        native_states = [
            json.loads(line) for line in
            native_state.read_text(encoding="utf-8").splitlines()
        ]
        if len(native_states) != CONTINUATION_TICKS:
            raise AssertionError(
                f"native emitted {len(native_states)} continuation ticks")
        for tick, (java_state, native) in enumerate(
                zip(java_states, native_states), start=1):
            expected_loaded_order = [
                value["eid"] for value in sorted(
                    [by_eid(java_state["entities"], eid)
                     for eid, _, _ in spawned]
                    + [by_eid(java_state["entities"], eid)
                       for eid in orbs]
                    + [by_eid(java_state["entities"], eid)
                       for eid in sentinels],
                    key=lambda value: value["loaded_order"],
                )
            ]
            if native.get("loaded_entity_order") != expected_loaded_order:
                raise AssertionError(
                    f"tick {tick} loaded entity order mismatch: "
                    f"Java={expected_loaded_order!r} "
                    f"native={native.get('loaded_entity_order')!r}")
            expected_order = [
                value["eid"] for value in sorted(
                    [by_eid(java_state["entities"], eid)
                     for eid, _, _ in spawned]
                    + [by_eid(java_state["entities"], eid)
                       for eid in orbs],
                    key=lambda value: value["loaded_order"],
                )
            ]
            if native.get("mob_update_order") != expected_order:
                raise AssertionError(
                    f"tick {tick} loaded update order mismatch: "
                    f"Java={expected_order!r} "
                    f"native={native.get('mob_update_order')!r}")
            for eid, entity_type, name in spawned:
                java = by_eid(java_state["entities"], eid)
                restored = by_eid(native["entities"], eid)
                mismatches = []
                if restored.get("type") != entity_type:
                    mismatches.append("type")
                for field in (
                        "x", "y", "z", "vx", "vy", "vz",
                        "base_box_min_x", "base_box_min_y",
                        "base_box_min_z", "base_box_max_x",
                        "base_box_max_y", "base_box_max_z"):
                    if dbits(java[field]) != dbits(restored[field]):
                        mismatches.append(field)
                for field in ("yaw", "pitch", "health", "fall_distance"):
                    if fbits(java[field]) != fbits(restored[field]):
                        mismatches.append(field)
                for field in (
                        "air", "fire", "on_ground", "ticks_existed",
                        "base_living_sound_time", "hurt_time", "death_time",
                        "hurt_resistant_time", "base_entity_seed48"):
                    if java[field] != restored[field]:
                        mismatches.append(field)
                if java.get("mob_effects", []) != restored.get("potions", []):
                    mismatches.append("mob_effects")
                for field in ("max_health", "absorption"):
                    if fbits(java[field]) != fbits(restored[field]):
                        mismatches.append(field)
                if entity_type == 10:
                    for field in ("sheep_fleece_color", "sheep_sheared"):
                        if java[field] != restored[field]:
                            mismatches.append(field)
                elif entity_type == 14:
                    for field in (
                            "squid_pitch", "squid_prev_pitch",
                            "squid_yaw", "squid_prev_yaw",
                            "squid_rotation", "squid_prev_rotation",
                            "squid_tentacle_angle",
                            "squid_last_tentacle_angle",
                            "squid_random_motion_speed",
                            "squid_rotation_velocity",
                            "squid_rotate_speed", "squid_random_motion_x",
                            "squid_random_motion_y",
                            "squid_random_motion_z",
                            "squid_render_yaw_offset"):
                        if fbits(java[field]) != fbits(restored[field]):
                            mismatches.append(field)
                elif entity_type == 64:
                    if java["snowman_pumpkin"] \
                            != restored["snowman_pumpkin"]:
                        mismatches.append("snowman_pumpkin")
                elif entity_type == 13:
                    for field in (
                            "chicken_egg_time", "chicken_jockey"):
                        if java.get(field, False) != restored[field]:
                            mismatches.append(field)
                    for field in (
                            "chicken_wing_rotation", "chicken_dest_pos",
                            "chicken_old_flap_speed", "chicken_old_flap",
                            "chicken_wing_rot_delta"):
                        if fbits(java[field]) != fbits(restored[field]):
                            mismatches.append(field)
                elif entity_type in (27, 35):
                    for field in ("slime_size", "slime_was_on_ground"):
                        if java[field] != restored[field]:
                            mismatches.append(field)
                    for field in (
                            "slime_squish_amount", "slime_squish_factor",
                            "slime_prev_squish_factor"):
                        if fbits(java[field]) != fbits(restored[field]):
                            mismatches.append(field)
                elif entity_type == 57:
                    for field in (
                            "golem_player_created", "golem_home_timer",
                            "golem_attack_timer", "golem_rose_timer"):
                        if java[field] != restored[field]:
                            mismatches.append(field)
                elif entity_type == 24:
                    if java["bat_hanging"] != restored["bat_hanging"]:
                        mismatches.append("bat_hanging")
                elif entity_type == 63:
                    for field in (
                            "endermite_lifetime",
                            "endermite_player_spawned",
                            "endermite_persistence_required"):
                        if java[field] != restored[field]:
                            mismatches.append(field)
                if mismatches:
                    raise AssertionError(
                        f"{name} tick {tick} continuation mismatch "
                        f"{mismatches}: Java={java!r} native={restored!r}")
            for eid in orbs:
                java = by_eid(java_state["entities"], eid)
                restored = by_eid(native["entities"], eid)
                mismatches = []
                for field in ("x", "y", "z", "vx", "vy", "vz"):
                    if dbits(java[field]) != dbits(restored[field]):
                        mismatches.append(field)
                for field in (
                        "health", "value", "age", "pickup_delay", "color",
                        "target_color"):
                    if java[field] != restored[field]:
                        mismatches.append(field)
                if mismatches:
                    raise AssertionError(
                        f"EntityXPOrb {eid} tick {tick} continuation "
                        f"mismatch {mismatches}: Java={java!r} "
                        f"native={restored!r}")
            for eid in sentinels:
                java = by_eid(java_state["entities"], eid)
                restored = by_eid(native["entities"], eid)
                mismatches = []
                for field in ("x", "y", "z", "vx", "vy", "vz"):
                    if dbits(java[field]) != dbits(restored[field]):
                        mismatches.append(field)
                for field in (
                        "item", "count", "meta", "age", "pickup_delay",
                        "health", "ticks_existed", "fire", "in_water",
                        "first_update"):
                    if java[field] != restored[field]:
                        mismatches.append(field)
                if mismatches:
                    raise AssertionError(
                        f"EntityItem sentinel {eid} tick {tick} "
                        f"continuation mismatch {mismatches}: "
                        f"Java={java!r} native={restored!r}")
    print(f"PASS real Java/NBT/capsule/native: all {len(FIXTURES)} plain "
          f"native mob classes plus {len(orbs)} interleaved XP orbs and "
          "a preserved Item across "
          f"{CONTINUATION_TICKS} ticks with exact living/XP update order")


if __name__ == "__main__":
    main()
