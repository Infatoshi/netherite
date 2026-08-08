#!/usr/bin/env python3
"""Bit-compare one real full-world horse taming boundary with magma."""

import argparse
import json
import pathlib
import re
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
CASES = (
    ("horse_success", "horse", 100, 5),
    ("donkey_success", "donkey", 100, 5),
    ("mule_success", "mule", 100, 5),
    ("horse_failure", "horse", 0, 5),
    ("donkey_temper_cap_failure", "donkey", 99, 3690),
    ("mule_failure", "mule", 0, 5),
    ("horse_no_trigger", "horse", 40, 1),
    ("donkey_no_trigger", "donkey", 40, 1),
    ("mule_no_trigger", "mule", 40, 1),
)
HEX8 = re.compile(r"^[0-9a-f]{8}$")
HEX16 = re.compile(r"^[0-9a-f]{16}$")
TOP_KEYS = {
    "ok", "horse_kind", "eid", "temper", "tame", "rearing",
    "ridden", "player_riding", "owner_present", "owner_matches_player",
    "entity_seed48", "entity_have_next_gaussian",
    "entity_next_gaussian_bits", "ticks_existed", "entity_age",
    "living_sound_time", "task_tick_count", "tail_counter",
    "path_present", "on_ground", "fall_distance_bits", "yaw_bits",
    "pitch_bits", "position_bits", "motion_bits",
    "last_tick_position_bits", "previous_position_bits",
    "player_position_bits", "player_motion_bits", "update_order", "events",
    "next_entity_id",
}
STATUS_KEYS = {"eid", "kind", "status"}
SOUND_KEYS = {
    "category", "eid", "kind", "pitch_bits", "sound", "volume_bits",
    "x", "y", "z",
}


def validate(value, name, kind, eid):
    expected = set(TOP_KEYS)
    if value.get("owner_present"):
        expected.update(("owner_most", "owner_least"))
    if set(value) != expected or value.get("ok") is not True:
        raise AssertionError(f"{name}: invalid top-level schema")
    if value["horse_kind"] != kind or value["eid"] != eid:
        raise AssertionError(f"{name}: wrong staged identity")
    for field in ("fall_distance_bits", "yaw_bits", "pitch_bits"):
        if not isinstance(value[field], str) or not HEX8.fullmatch(value[field]):
            raise AssertionError(f"{name}: malformed {field}")
    if not HEX16.fullmatch(value["entity_next_gaussian_bits"]):
        raise AssertionError(f"{name}: malformed gaussian")
    for field in (
            "position_bits", "motion_bits", "last_tick_position_bits",
            "previous_position_bits", "player_position_bits",
            "player_motion_bits"):
        vector = value[field]
        if not isinstance(vector, list) or len(vector) != 3 \
                or not all(isinstance(bits, str) and HEX16.fullmatch(bits)
                           for bits in vector):
            raise AssertionError(f"{name}: malformed {field}")
    for index, event in enumerate(value["events"]):
        keys = STATUS_KEYS if event.get("kind") == "status" else SOUND_KEYS
        if set(event) != keys:
            raise AssertionError(f"{name}: event {index} schema changed")
        for field in ("volume_bits", "pitch_bits"):
            if field in event and not HEX8.fullmatch(event[field]):
                raise AssertionError(f"{name}: malformed event {field}")


def native(case, owner_most, owner_least):
    args = [
        MAGMA / "game" / "test_horse_tame_tick_oracle",
        case["horse_kind"], case["temper"], case["entity_seed48"],
        case["x"], case["y"], case["z"], case["next_entity_id"],
        owner_most, owner_least,
    ]
    return json.loads(subprocess.check_output(
        [str(value) for value in args], text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [case for case in CASES if not args.case or case[0] == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")

    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_horse_tame_tick_oracle",
        ], check=True, stdout=subprocess.DEVNULL)
        owner = None
        for index, (name, kind, temper, seed48) in enumerate(cases):
            case = {
                "horse_kind": kind,
                "temper": temper,
                "entity_seed48": seed48,
                "x": observation["x"],
                "y": 220.0,
                "z": observation["z"],
                "next_entity_id": 694000 + index,
            }
            java = request(args.port, "horse_tame_tick_locked", case)
            if java["owner_present"]:
                owner = (java["owner_most"], java["owner_least"])
            if owner is None:
                raise AssertionError("first taming fixture did not expose player UUID")
            magma = native(case, *owner)
            validate(java, name, kind, case["next_entity_id"])
            validate(magma, name, kind, case["next_entity_id"])
            if java != magma:
                differing = {
                    key: (java.get(key), magma.get(key))
                    for key in sorted(set(java) | set(magma))
                    if java.get(key) != magma.get(key)
                }
                raise AssertionError(f"{name}: differing={differing!r}")
        print(
            f"PASS java==magma: {len(cases)} full horse taming ticks, exact "
            "AI/RNG, motion, passenger, owner and event state")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
