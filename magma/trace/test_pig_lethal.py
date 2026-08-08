#!/usr/bin/env python3
"""Bit-compare an ordinary mounted pig's lethal hit through death tick 19."""

import argparse
import json
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent

PIG_SEED = 0x23456789ABCD
WORLD_SEED = 0x3456789ABCDE
MATH_SEED = 0x456789ABCDEF
NEXT_ID = 683000

CASES = [
    ("raw_yaw_0", {}),
    ("raw_yaw_90_second_seeds", {
        "yaw": 90.0, "pig_entity_seed48": 1,
        "world_seed48": 2, "math_seed48": 3,
    }),
    ("cooked_yaw_90", {"burning": True, "yaw": 90.0}),
    ("no_mob_loot_yaw_0", {"do_mob_loot": False}),
]

TICK_FIELDS = [
    "tick", "player_position_bits", "player_motion_bits",
    "player_yaw_bits", "player_pitch_bits", "player_on_ground",
    "player_fall_distance_bits", "player_riding_eid",
    "player_aabb_min_bits", "player_aabb_max_bits", "pig_death_time",
    "pig_living_dead", "pig_entity_is_dead", "pig_loaded",
    "pig_health_bits", "pig_hurt_time", "pig_hurt_resistant_time",
    "pig_recently_hit", "pig_saddled", "pig_on_ground",
    "pig_yaw_bits", "pig_pitch_bits", "pig_passenger_eids",
    "pig_position_bits", "pig_motion_bits", "pig_aabb_min_bits",
    "pig_aabb_max_bits", "pig_rng_seed48", "world_seed48",
    "math_seed48", "next_entity_id",
]

ITEM_FIELDS = [
    "eid", "item", "count", "meta", "age", "pickup_delay", "health",
    "lifespan", "on_ground", "is_dead", "position_bits", "motion_bits",
    "yaw_bits", "hover_start_bits",
]


def fixture(overrides):
    case = {
        "do_mob_loot": True,
        "burning": False,
        "yaw": 0.0,
        "pig_entity_seed48": PIG_SEED,
        "world_seed48": WORLD_SEED,
        "math_seed48": MATH_SEED,
        "next_entity_id": NEXT_ID,
    }
    case.update(overrides)
    return case


def bits_double(value):
    return struct.pack("!d", value).hex()


def bits_float(value):
    return struct.pack("!f", value).hex()


def double_from_bits(value):
    return struct.unpack("!d", bytes.fromhex(value))[0]


def normalize_events(result):
    pig_position = result["ticks"][0]["pig_position_bits"]
    events = []
    for event in result["events"]:
        if isinstance(event["kind"], int):
            events.append({key: event[key] for key in (
                "kind", "eid", "data", "position_bits", "volume_bits",
                "pitch_bits")})
        elif event["kind"] == "status":
            events.append({
                "kind": 1, "eid": event["eid"], "data": event["status"],
                "position_bits": pig_position,
                "volume_bits": "00000000", "pitch_bits": "00000000",
            })
        elif event["kind"] == "sound":
            if event["sound"] != "minecraft:entity.pig.death":
                raise AssertionError(f"unexpected pig sound: {event}")
            events.append({
                "kind": 2, "eid": event["eid"], "data": 4,
                "position_bits": [bits_double(event[axis])
                                  for axis in ("x", "y", "z")],
                "volume_bits": bits_float(event["volume"]),
                "pitch_bits": bits_float(event["pitch"]),
            })
        else:
            raise AssertionError(f"unexpected pig event: {event}")
    return events


def normalize_tick(tick, player_eid):
    result = {key: tick[key] for key in TICK_FIELDS}
    result["update_order"] = [
        row if isinstance(row, int) else row["eid"]
        for row in tick["update_order"]]
    if "pig_attacking_player" in tick:
        result["pig_attacking_player"] = tick["pig_attacking_player"]
    else:
        result["pig_attacking_player"] = (
            tick["pig_attacking_player_eid"] == player_eid)
    result["items"] = [
        {key: item[key] for key in ITEM_FIELDS} for item in tick["items"]]
    result["xp_orbs"] = tick.get("xp_orbs", [])
    return result


def normalize(result):
    player_eid = result["player_eid"]
    ticks = {tick["tick"]: tick for tick in result["ticks"]}
    return {
        "ok": result["ok"],
        "do_mob_loot": result["do_mob_loot"],
        "burning": result["burning"],
        "pig_eid": result["pig_eid"],
        "player_eid": player_eid,
        "events": normalize_events(result),
        "ticks": [normalize_tick(ticks[number], player_eid)
                  for number in (0, 1, 19)],
    }


def native(case, java):
    pig_x, pig_y, pig_z = [
        double_from_bits(value)
        for value in java["ticks"][0]["pig_position_bits"]]
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_pig_lethal_oracle"),
        str(int(case["do_mob_loot"])),
        str(int(case["burning"])),
        repr(case["yaw"]),
        str(case["pig_entity_seed48"]),
        str(case["world_seed48"]),
        str(case["math_seed48"]),
        str(case["next_entity_id"]),
        str(java["player_eid"]),
        repr(pig_x), repr(pig_y), repr(pig_z),
    ], text=True)
    return json.loads(raw)


def mismatch(name, java, magma):
    keys = sorted(set(java) | set(magma))
    diff = {key: {"java": java.get(key), "magma": magma.get(key)}
            for key in keys if java.get(key) != magma.get(key)}
    raise AssertionError(f"{name}: {json.dumps(diff, sort_keys=True)}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [(name, fixture(values)) for name, values in CASES
             if not args.case or name == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")

    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_pig_lethal_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, case in cases:
            try:
                java_raw = request(
                    args.port, "pig_death_lethal_tick_locked", case)
            except Exception as exc:
                raise RuntimeError(
                    f"{name}: Java oracle request failed") from exc
            magma_raw = native(case, java_raw)
            java = normalize(java_raw)
            magma = normalize(magma_raw)
            if java != magma:
                mismatch(name, java, magma)
        print(f"PASS java==magma: {len(cases)} ordinary mounted lethal-pig "
              "traces through tick 19, exact hit/death/drop events, corpse "
              "travel, passenger pose, item physics, update order, and RNG")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
