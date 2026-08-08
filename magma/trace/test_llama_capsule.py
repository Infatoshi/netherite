#!/usr/bin/env python3
"""Real-Java llama leash/caravan/spit capsule continuation boundary."""

import argparse
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from state_capsule import create_capsule, emit_magma
from test_dragon_crystal_notification import request
from trace_java import canonicalize


TICKS = 4


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def by_eid(values, eid):
    matches = [value for value in values if value.get("eid") == eid]
    if len(matches) != 1:
        raise AssertionError(f"expected eid {eid}, got {matches!r}")
    return matches[0]


def lead_items(state):
    return [value for value in state["entities"]
            if value.get("item") == 420]


def compare_llama(java, native, tick):
    mismatches = []
    for field in (
            "x", "y", "z", "vx", "vy", "vz",
            "base_box_min_x", "base_box_min_y", "base_box_min_z",
            "base_box_max_x", "base_box_max_y", "base_box_max_z",
            "horse_max_health_base", "horse_movement_speed_base",
            "horse_jump_strength", "llama_caravan_speed",
            "base_entity_gaussian"):
        if dbits(java[field]) != dbits(native[field]):
            mismatches.append(field)
    for field in (
            "yaw", "pitch", "health", "fall_distance",
            "base_last_damage", "horse_jump_power", "horse_head_lean",
            "horse_prev_head_lean", "horse_rearing_amount",
            "horse_prev_rearing_amount", "horse_mouth_openness",
            "horse_prev_mouth_openness", "horse_prev_limb_amount",
            "horse_limb_amount", "horse_limb_swing"):
        if fbits(java[field]) != fbits(native[field]):
            mismatches.append(field)
    exact_fields = (
        "uuid_most", "uuid_least", "hurt_time", "death_time",
        "hurt_resistant_time", "no_ai", "air", "fire", "on_ground",
        "in_water", "ticks_existed", "base_living_sound_time",
        "base_entity_seed48", "base_entity_have_gaussian",
        "horse_subtype", "horse_growing_age", "horse_forced_age",
        "horse_forced_age_timer", "horse_in_love", "horse_tame",
        "horse_saddled", "horse_bred", "horse_eating", "horse_rearing",
        "horse_mouth_open", "horse_temper", "horse_owner_present",
        "horse_owner_uuid_most", "horse_owner_uuid_least",
        "horse_variant", "horse_armor", "horse_chested", "horse_trap",
        "horse_trap_time", "horse_eating_counter",
        "horse_open_mouth_counter", "horse_jump_rearing_counter",
        "horse_tail_counter", "horse_sprint_counter", "horse_gallop_time",
        "horse_jumping", "horse_allow_stand_sliding", "horse_inventory",
        "llama_strength", "llama_decor", "llama_did_spit",
        "llama_leashed", "llama_leash_holder_kind",
        "llama_leash_holder_eid", "llama_leash_holder_uuid_most",
        "llama_leash_holder_uuid_least", "llama_leash_pending",
        "llama_leash_pending_x", "llama_leash_pending_y",
        "llama_leash_pending_z", "llama_caravan_head_eid",
        "llama_caravan_tail_eid", "llama_caravan_dist_counter",
    )
    for field in exact_fields:
        if java[field] != native[field]:
            mismatches.append(field)
    if mismatches:
        raise AssertionError(
            f"llama {java['eid']} tick {tick} mismatch {mismatches}: "
            f"Java={java!r} native={native!r}")


def compare_item(java, native, tick):
    mismatches = []
    for field in ("x", "y", "z", "vx", "vy", "vz"):
        if dbits(java[field]) != dbits(native[field]):
            mismatches.append(field)
    for field in (
            "eid", "uuid_most", "uuid_least", "item", "count", "meta",
            "age", "pickup_delay", "health", "ticks_existed", "fire",
            "in_water", "first_update"):
        if java[field] != native[field]:
            mismatches.append(field)
    if mismatches:
        raise AssertionError(
            f"lead item tick {tick} mismatch {mismatches}: "
            f"Java={java!r} native={native!r}")


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
            prefix="llama_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        deadline = time.monotonic() + 120.0
        while True:
            try:
                obs = request(args.port, "obs")
                break
            except (OSError, RuntimeError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)
        rule = request(args.port, "runcmds", {
            "cmds": ["gamerule doMobSpawning false"],
        })
        if not rule.get("ok") or rule.get("failed"):
            raise AssertionError(f"could not disable natural spawning: {rule}")
        request(args.port, "killentities")
        try:
            locked_state = request(args.port, "server_step_lock")
            locked = True
            player = locked_state["authoritative"]
            x = float(player["x"])
            y = float(player["y"])
            z = float(player["z"])
            common = {
                "type": "horse", "horse_kind": "llama", "no_ai": True,
                "y": y, "z": z, "chested": True,
            }
            leader = request(args.port, "summon_locked", dict(common,
                x=x + 4.0, variant=3, llama_strength=5, llama_decor=14,
                llama_leash_player=True, llama_caravan_speed=2.52,
                llama_caravan_dist_counter=17,
                entity_seed48=0x123456789ABC))
            follower = request(args.port, "summon_locked", dict(common,
                x=x + 8.0, variant=2, llama_strength=3, llama_decor=4,
                llama_caravan_head_eid=leader["eid"],
                llama_caravan_speed=3.024,
                llama_caravan_dist_counter=11,
                entity_seed48=0x123456789ABD))
            breaker = request(args.port, "summon_locked", dict(common,
                x=x + 12.0, z=z + 3.0, variant=1, llama_strength=2,
                llama_decor=9, llama_leash_player=True,
                llama_caravan_speed=2.1,
                llama_caravan_dist_counter=0,
                entity_seed48=0x123456789ABE))
            llama_eids = [leader["eid"], follower["eid"], breaker["eid"]]
            target = request(args.port, "summon_locked", {
                "type": "item", "x": x + 18.0, "y": y + 4.0, "z": z,
                "item": 1, "count": 1, "meta": 0,
                "pickup_delay": 32767,
            })
            spit = request(args.port, "summon_locked", {
                "type": "llama_spit", "owner_eid": leader["eid"],
                "x": x + 16.0, "y": y + 4.1, "z": z,
                "mx": 3.0, "my": 0.0, "mz": 0.0,
                "no_gravity": True,
            })
            hidden = request(args.port, "hidden_state_locked")
            request(args.port, "isolate_server_globals_locked", hidden)
            authoritative = request(
                args.port, "authoritative_state_locked")["authoritative"]
            for eid in llama_eids:
                value = by_eid(authoritative["entities"], eid)
                if value.get("type") != "EntityLlama" \
                        or value.get("horse_exact") is not True:
                    raise AssertionError(
                        f"Java llama is not capsule-exact: {value!r}")
            selected = [by_eid(authoritative["entities"], eid)
                        for eid in llama_eids]
            target_initial = by_eid(
                authoritative["entities"], target["eid"])
            spit_initial = by_eid(authoritative["entities"], spit["eid"])
            if spit_initial.get("type") != "EntityLlamaSpit" \
                    or spit_initial.get("llama_spit_exact") is not True \
                    or spit_initial.get("llama_spit_owner_eid") \
                    != leader["eid"]:
                raise AssertionError(
                    f"Java llama spit is not capsule-exact: {spit_initial!r}")
            selected.extend([target_initial, spit_initial])
            box = [
                math.floor(min(value["x"] for value in selected + [player])) - 5,
                max(0, math.floor(
                    min(value["y"] for value in selected + [player])) - 5),
                math.floor(min(value["z"] for value in selected + [player])) - 5,
                math.floor(max(value["x"] for value in selected + [player])) + 5,
                math.floor(max(value["y"] for value in selected + [player])) + 6,
                math.floor(max(value["z"] for value in selected + [player])) + 5,
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
            java_states = [request(args.port, "step")["authoritative"]
                           for _ in range(TICKS)]
        finally:
            if locked:
                request(args.port, "server_step_unlock")
            request(args.port, "runcmds", {
                "cmds": ["gamerule doMobSpawning true"],
            })

        native_file = temp / "native.jsonl"
        subprocess.run([
            str(MAGMA / "magma_game"), "--world", "superflat",
            "--headless", "--ticks", str(TICKS), "--mobs", "off",
            "--script", str(script), "--state-out", str(native_file),
            "--render", "off", "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL)
        native_states = [json.loads(line) for line in
                         native_file.read_text(encoding="utf-8").splitlines()]
        if len(native_states) != TICKS:
            raise AssertionError(
                f"native emitted {len(native_states)} continuation ticks")
        lead_eid = None
        for tick, (java, native) in enumerate(
                zip(java_states, native_states), start=1):
            java_llamas = [by_eid(java["entities"], eid)
                           for eid in llama_eids]
            for value in java_llamas:
                compare_llama(value, by_eid(native["entities"], value["eid"]),
                              tick)
            java_leads = lead_items(java)
            native_leads = lead_items(native)
            if len(java_leads) != 1 or len(native_leads) != 1:
                raise AssertionError(
                    f"tick {tick} expected one lead drop: "
                    f"Java={java_leads!r} native={native_leads!r}")
            if lead_eid is None:
                lead_eid = java_leads[0]["eid"]
            if java_leads[0]["eid"] != lead_eid:
                raise AssertionError("Java lead identity changed")
            compare_item(java_leads[0], native_leads[0], tick)
            java_target = by_eid(java["entities"], target["eid"])
            native_target = by_eid(native["entities"], target["eid"])
            compare_item(java_target, native_target, tick)
            if tick == 1 and java_target["health"] != 4:
                raise AssertionError(
                    f"Java llama spit did not damage item: {java_target!r}")
            if any(value.get("eid") == spit["eid"]
                   for value in java["entities"] + native["entities"]):
                raise AssertionError(
                    f"llama spit survived impact at tick {tick}")
            expected_loaded = [value["eid"] for value in sorted(
                java_llamas + java_leads + [java_target],
                key=lambda value: value["loaded_order"])]
            if native.get("loaded_entity_order") != expected_loaded:
                raise AssertionError(
                    f"tick {tick} loaded order mismatch: "
                    f"Java={expected_loaded!r} "
                    f"native={native.get('loaded_entity_order')!r}")
            expected_mobs = [value["eid"] for value in sorted(
                java_llamas, key=lambda value: value["loaded_order"])]
            if native.get("mob_update_order") != expected_mobs:
                raise AssertionError(
                    f"tick {tick} mob order mismatch: Java={expected_mobs!r} "
                    f"native={native.get('mob_update_order')!r}")
    print("PASS real Java/capsule/native: llama player leash, reciprocal "
          "caravan graph/task clocks, exact break/drop, item spit impact, "
          "and loaded order over "
          f"{TICKS} ticks")


if __name__ == "__main__":
    main()
