#!/usr/bin/env python3
"""Bit-compare bounded pig boost, ridden-travel, and dismount transitions."""

import argparse
import json
import os
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
NATIVE = pathlib.Path(os.environ.get(
    "PIG_RIDE_NATIVE_BIN", MAGMA / "game" / "test_pig_ride_oracle"))
EID = 673000
SEED = 0x123456789ABC

BOOST_CASES = [
    ("main_survival_fresh", {}),
    ("offhand_creative_fresh", {"hand": "offhand", "creative": True}),
    ("meta18_accepts", {"meta": 18}),
    ("meta19_rejects", {"meta": 19}),
    ("already_boosting", {
        "boosting": True, "boost_time": 17, "boost_total": 264}),
    ("wrong_item", {"item": 280}),
    ("second_seed", {"entity_seed48": 1}),
]

TICK_CASES = [
    ("main_normal", {"rider_pitch": 20.0}),
    ("offhand_turn", {
        "main_item": 0, "off_item": 398,
        "rider_yaw": 90.0, "rider_pitch": -20.0}),
    ("negative_turn", {"rider_yaw": -90.0}),
    ("no_stick_motion", {
        "main_item": 0, "motion_x": 0.125,
        "motion_y": -0.01, "motion_z": -0.0625,
        "ai_speed": 0.13, "limb_amount": 0.2, "limb_swing": 0.75}),
    ("boost_first", {
        "boosting": True, "boost_time": 0, "boost_total": 400}),
    ("boost_mid", {
        "boosting": True, "boost_time": 199, "boost_total": 400,
        "rider_yaw": 37.0}),
    ("boost_total", {
        "boosting": True, "boost_time": 400, "boost_total": 400}),
    ("boost_expiry", {
        "boosting": True, "boost_time": 401, "boost_total": 400}),
    ("dormant_boost_without_stick", {
        "main_item": 0, "boosting": True,
        "boost_time": 17, "boost_total": 264}),
]

TRACE_CASES = [
    ("travel_one_block_step", {"layout": "one_block_step"}),
    ("travel_two_block_wall", {"layout": "two_block_wall"}),
    ("travel_two_cell_gap", {"layout": "two_cell_gap"}),
    ("travel_bottom_slab", {"layout": "bottom_slab"}),
    ("travel_stone_floor", {"layout": "stone_floor", "ticks": 6}),
    ("travel_soul_sand_floor", {
        "layout": "soul_sand_floor", "ticks": 6}),
    ("travel_web_corridor", {"layout": "web_corridor", "ticks": 6}),
    ("travel_ladder_clear", {"layout": "ladder_clear", "ticks": 6}),
    ("travel_ladder_north_wall", {
        "layout": "ladder_north_wall", "ticks": 6}),
    ("travel_stone_bounce", {
        "layout": "stone_bounce", "ticks": 6, "motion_y": -0.6}),
    ("travel_slime_bounce", {
        "layout": "slime_bounce", "ticks": 6, "motion_y": -0.6}),
    ("travel_stone_low_landing", {
        "layout": "stone_low_landing", "ticks": 6, "motion_y": -0.05}),
    ("travel_slime_low_landing", {
        "layout": "slime_low_landing", "ticks": 6, "motion_y": -0.05}),
    ("travel_still_water", {"layout": "still_water", "ticks": 6}),
    ("travel_water_entry", {"layout": "water_entry", "ticks": 4}),
    ("travel_water_entry_flow", {
        "layout": "water_entry_flow", "ticks": 4}),
    ("travel_water_fall_entry", {
        "layout": "water_fall_entry", "ticks": 4, "motion_y": -1.0}),
    ("travel_water_edge_climb", {
        "layout": "water_edge_climb", "ticks": 1,
        "motion_y": 0.6, "motion_z": 0.1}),
    ("travel_water_edge_blocked", {
        "layout": "water_edge_blocked", "ticks": 1,
        "motion_y": 0.6, "motion_z": 0.1}),
    ("travel_still_lava", {"layout": "still_lava", "ticks": 6}),
    ("travel_lava_entry", {"layout": "lava_entry", "ticks": 4}),
    ("travel_lava_edge_climb", {
        "layout": "lava_edge_climb", "ticks": 1,
        "motion_y": 0.6, "motion_z": 0.1}),
    ("travel_lava_edge_blocked", {
        "layout": "lava_edge_blocked", "ticks": 1,
        "motion_y": 0.6, "motion_z": 0.1}),
    ("travel_water_lava_overlap", {
        "layout": "water_lava_overlap", "ticks": 1}),
]

LAVA_CONTACT_CASES = [
    ("lava_contact_dry", {"layout": "dry", "ticks": 12}),
    ("lava_contact_sustained", {"layout": "lava", "ticks": 12}),
    ("lava_contact_fire_resistance_expiry", {
        "layout": "lava", "ticks": 2, "fire_resistance_ticks": 1}),
]

PACKET_CONTACT_CASES = [
    ("packet_contact_dry", {"layout": "dry", "ticks": 12}),
    ("packet_contact_cactus", {"layout": "cactus", "ticks": 12}),
    ("packet_contact_fire", {"layout": "fire", "ticks": 12}),
    ("packet_contact_cactus_fire", {
        "layout": "cactus_fire", "ticks": 12}),
    ("packet_contact_wet_extinguish", {
        "layout": "water", "ticks": 2}),
    ("packet_contact_fire_resistance_expiry", {
        "layout": "fire", "ticks": 3, "fire_resistance_ticks": 2}),
    ("packet_contact_cactus_fire_resistance", {
        "layout": "cactus_fire", "ticks": 1,
        "fire_resistance_ticks": 2}),
    ("packet_contact_lava", {"layout": "lava", "ticks": 12}),
    ("packet_contact_lava_fire_resistance_expiry", {
        "layout": "lava", "ticks": 2, "fire_resistance_ticks": 1}),
]

VEHICLE_MOVE_CASES = [
    ("vehicle_move_accept_open", {
        "layout": "dry", "packet_dx": 0.25, "packet_dz": -0.125}),
    ("vehicle_move_accept_up", {
        "layout": "dry", "packet_dy": 0.25}),
    ("vehicle_move_correct_floor", {
        "layout": "dry", "packet_dy": -0.25}),
    ("vehicle_move_correct_ceiling", {
        "layout": "ceiling", "packet_dy": 1.0}),
    ("vehicle_move_correct_wall", {
        "layout": "wall", "packet_dx": 1.0}),
    ("vehicle_move_correct_speed", {
        "layout": "dry", "packet_dx": 11.0}),
]

RUNTIME_VEHICLE_MOVE_CASES = [
    ("runtime_vehicle_accept_emitted", {
        "layout": "dry", "packet_dz": 0.056249987334012985,
        "packet_yaw": 0.0, "packet_pitch": 0.0,
        "source": "emitted_client"}),
    ("runtime_vehicle_correct_wall", {
        "layout": "wall", "packet_dx": 1.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_speed", {
        "layout": "dry", "packet_dx": 11.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_accept_up", {
        "layout": "dry", "packet_dy": 0.25,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_floor", {
        "layout": "dry", "packet_dy": -0.25,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_ceiling", {
        "layout": "ceiling", "packet_dy": 1.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_accept_fire", {
        "layout": "move_fire", "packet_dx": 0.75,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_cactus", {
        "layout": "move_cactus", "packet_dx": 1.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_accept_lava", {
        "layout": "move_lava", "packet_dx": 0.75,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_wall_beyond_fire", {
        "layout": "wall_beyond_fire", "packet_dx": 2.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_correct_wall_beyond_cactus", {
        "layout": "wall_beyond_cactus", "packet_dx": 2.0,
        "source": "injected_packet"}),
    ("runtime_vehicle_accept_dry_to_water", {
        "layout": "dry_to_water", "packet_dx": 0.75,
        "source": "injected_packet"}),
    ("runtime_vehicle_accept_water_to_fire", {
        "layout": "water_to_fire", "packet_dx": 1.0,
        "source": "injected_packet"}),
]

CLIENT_VEHICLE_CORRECTION_CASES = [
    ("client_vehicle_correction_collision", {
        "predicted_x": 10.75, "predicted_y": 220.25,
        "predicted_z": -3.125,
        "correction_x": 10.5, "correction_y": 220.0,
        "correction_z": -3.5,
        "predicted_yaw": 73.0, "predicted_pitch": 21.0,
        "correction_yaw": 37.0, "correction_pitch": -11.0,
        "motion_x": 0.125, "motion_y": -0.0625,
        "motion_z": 0.03125, "on_ground": False}),
    ("client_vehicle_correction_speed", {
        "predicted_x": -24.25, "predicted_y": 96.0,
        "predicted_z": 19.75,
        "correction_x": -25.0, "correction_y": 96.0,
        "correction_z": 20.0,
        "predicted_yaw": -91.0, "predicted_pitch": 33.0,
        "correction_yaw": 0.0, "correction_pitch": 0.0,
        "motion_x": -0.25, "motion_y": 0.5,
        "motion_z": -0.125, "on_ground": True}),
]

PACKET_CHAIN_CASES = [
    ("vehicle_chain_same_epoch", {"layout": "chain_same_epoch"}),
    ("vehicle_chain_vertical_epoch", {
        "layout": "chain_vertical_epoch"}),
    ("vehicle_chain_mixed_rejections", {
        "layout": "chain_mixed_rejections", "ticks": 4}),
    ("vehicle_chain_epoch_reseed", {"layout": "chain_epoch_reseed"}),
    ("vehicle_chain_later_water", {"layout": "chain_later_water"}),
    ("vehicle_chain_preticked_water", {
        "layout": "chain_preticked_water"}),
]

DISMOUNT_CASES = [
    ("dismount_flat_yaw_0", {"layout": "flat"}),
    ("dismount_flat_yaw_90", {"layout": "flat", "yaw": 90.0}),
    ("dismount_first_blocked_yaw_0", {"layout": "first_blocked"}),
    ("dismount_first_blocked_yaw_90", {
        "layout": "first_blocked", "yaw": 90.0}),
    ("dismount_water_yaw_0", {"layout": "water"}),
    ("dismount_all_blocked_yaw_0", {"layout": "all_blocked"}),
    ("dismount_isolated_stone", {"layout": "support_stone"}),
    ("dismount_top_slab", {"layout": "support_top_slab"}),
    ("dismount_bottom_slab", {"layout": "support_bottom_slab"}),
    ("dismount_snow_8_layers", {"layout": "support_snow8"}),
    ("dismount_snow_7_layers", {"layout": "support_snow7"}),
    ("dismount_isolated_water", {"layout": "support_water"}),
]

DEATH_DISMOUNT_CASES = [
    ("death_dismount_flat_yaw_0", {"layout": "flat"}),
    ("death_dismount_flat_yaw_90", {"layout": "flat", "yaw": 90.0}),
    ("death_dismount_all_blocked_yaw_0", {"layout": "all_blocked"}),
    ("death_dismount_all_blocked_yaw_90", {
        "layout": "all_blocked", "yaw": 90.0}),
]


def boost_case(overrides):
    case = {
        "mode": "boost", "hand": "main", "item": 398, "meta": 0,
        "other_item": 0, "other_meta": 0, "creative": False,
        "entity_seed48": SEED, "eid": EID, "boosting": False,
        "boost_time": 0, "boost_total": 0,
    }
    case.update(overrides)
    return case


def tick_case(overrides):
    case = {
        "mode": "tick", "main_item": 398, "main_meta": 0,
        "off_item": 0, "off_meta": 0, "rider_yaw": 0.0,
        "rider_pitch": 0.0, "motion_x": 0.0, "motion_y": 0.0,
        "motion_z": 0.0, "ai_speed": 0.0, "limb_amount": 0.0,
        "limb_swing": 0.0, "boosting": False, "boost_time": 0,
        "boost_total": 0, "entity_seed48": SEED, "eid": EID,
    }
    case.update(overrides)
    return case


def trace_case(overrides):
    case = tick_case({})
    case.update({"mode": "trace", "layout": "one_block_step", "ticks": 48})
    case.update(overrides)
    return case


def lava_contact_case(overrides):
    case = {
        "mode": "lava_contact", "layout": "lava", "ticks": 12,
        "entity_seed48": SEED, "math_seed48": 0x23456789ABCD,
        "eid": EID + 1000, "fire_resistance_ticks": 0,
    }
    case.update(overrides)
    return case


def packet_contact_case(overrides):
    case = {
        "mode": "packet_contact", "layout": "cactus", "ticks": 12,
        "entity_seed48": SEED, "math_seed48": 0x23456789ABCD,
        "eid": EID + 1100, "fire_resistance_ticks": 0,
    }
    case.update(overrides)
    return case


def runtime_packet_contact_case(overrides):
    case = packet_contact_case(overrides)
    case["mode"] = "runtime_packet_contact"
    return case


def vehicle_move_case(overrides):
    case = {
        "mode": "packet_move", "layout": "dry", "ticks": 1,
        "packet_dx": 0.0, "packet_dy": 0.0, "packet_dz": 0.0,
        "packet_yaw": 37.0, "packet_pitch": -11.0,
        "entity_seed48": SEED, "math_seed48": 0x23456789ABCD,
        "eid": EID + 1200, "fire_resistance_ticks": 0,
    }
    case.update(overrides)
    return case


def runtime_vehicle_move_case(overrides):
    case = vehicle_move_case(overrides)
    case["mode"] = "runtime_packet_move"
    return case


def client_vehicle_correction_case(overrides):
    case = {
        "mode": "client_vehicle_correction",
        "predicted_x": 10.75, "predicted_y": 220.25,
        "predicted_z": -3.125,
        "correction_x": 10.5, "correction_y": 220.0,
        "correction_z": -3.5,
        "predicted_yaw": 73.0, "predicted_pitch": 21.0,
        "correction_yaw": 37.0, "correction_pitch": -11.0,
        "motion_x": 0.125, "motion_y": -0.0625,
        "motion_z": 0.03125, "on_ground": False,
        "eid": EID + 1225,
    }
    case.update(overrides)
    return case


def packet_chain_case(overrides):
    case = {
        "mode": "packet_chain", "layout": "chain_same_epoch", "ticks": 2,
        "packet_yaw": 37.0, "packet_pitch": -11.0,
        "entity_seed48": SEED, "math_seed48": 0x23456789ABCD,
        "eid": EID + 1250, "fire_resistance_ticks": 0,
    }
    case.update(overrides)
    return case


def dismount_case(overrides):
    case = {
        "mode": "dismount", "layout": "flat", "yaw": 0.0,
        "entity_seed48": SEED, "next_entity_id": 681000,
    }
    case.update(overrides)
    return case


def death_dismount_case(overrides):
    case = {
        "mode": "death_dismount", "layout": "flat", "yaw": 0.0,
        "entity_seed48": 0x23456789ABCD, "next_entity_id": 682000,
    }
    case.update(overrides)
    return case


def bits_double(text):
    return struct.unpack(">d", bytes.fromhex(text))[0]


def native_boost(case):
    raw = subprocess.check_output([
        str(NATIVE), "boost",
        "0" if case["hand"] == "main" else "1",
        str(case["item"]), str(case["meta"]),
        str(case["other_item"]), str(case["other_meta"]),
        str(int(case["creative"])), str(case["entity_seed48"]),
        str(case["eid"]), str(int(case["boosting"])),
        str(case["boost_time"]), str(case["boost_total"]),
    ], text=True)
    return json.loads(raw)


def native_tick(case, java):
    start = java["start_position_bits"]
    x, z = bits_double(start[0]), bits_double(start[2])
    raw = subprocess.check_output([
        str(NATIVE), "tick",
        repr(x), repr(z), str(case["main_item"]), str(case["main_meta"]),
        str(case["off_item"]), str(case["off_meta"]),
        repr(case["rider_yaw"]), repr(case["rider_pitch"]),
        repr(case["motion_x"]), repr(case["motion_y"]),
        repr(case["motion_z"]), repr(case["ai_speed"]),
        repr(case["limb_amount"]), repr(case["limb_swing"]),
        str(int(case["boosting"])), str(case["boost_time"]),
        str(case["boost_total"]), str(case["entity_seed48"]),
        str(case["eid"]),
    ], text=True)
    return json.loads(raw)


def native_trace(case, java):
    start = java["start_position_bits"]
    x, z = bits_double(start[0]), bits_double(start[2])
    raw = subprocess.check_output([
        str(NATIVE), "trace",
        repr(x), repr(z), str(case["main_item"]), str(case["main_meta"]),
        str(case["off_item"]), str(case["off_meta"]),
        repr(case["rider_yaw"]), repr(case["rider_pitch"]),
        repr(case["motion_x"]), repr(case["motion_y"]),
        repr(case["motion_z"]), repr(case["ai_speed"]),
        repr(case["limb_amount"]), repr(case["limb_swing"]),
        str(int(case["boosting"])), str(case["boost_time"]),
        str(case["boost_total"]), str(case["entity_seed48"]),
        str(case["eid"]), case["layout"], str(case["ticks"]),
    ], text=True)
    return json.loads(raw)


def native_lava_contact(case, java):
    start = java["start_position_bits"]
    x, z = bits_double(start[0]), bits_double(start[2])
    raw = subprocess.check_output([
        str(NATIVE), "lava_contact",
        case["layout"], str(case["ticks"]), repr(x), repr(z),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]), str(case["fire_resistance_ticks"]),
    ], text=True)
    return json.loads(raw)


def native_packet_contact(case, java):
    x, y, z = [
        bits_double(value) for value in java["start_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "packet_contact",
        case["layout"], str(case["ticks"]), repr(x), repr(y), repr(z),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]), str(case["fire_resistance_ticks"]),
    ], text=True)
    return json.loads(raw)


def native_runtime_packet_contact(case, java):
    x, y, z = [
        bits_double(value) for value in java["start_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE),
        "runtime_packet_contact",
        case["layout"], str(case["ticks"]), repr(x), repr(y), repr(z),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]), str(case["fire_resistance_ticks"]),
    ], text=True)
    return json.loads(raw)


def native_vehicle_move(case, java):
    x, y, z = [
        bits_double(value) for value in java["start_position_bits"]]
    target_x, target_y, target_z = [
        bits_double(value) for value in java["target_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "packet_move", case["layout"],
        repr(x), repr(y), repr(z),
        repr(target_x), repr(target_y), repr(target_z),
        repr(case["packet_yaw"]), repr(case["packet_pitch"]),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]),
    ], text=True)
    return json.loads(raw)


def native_runtime_vehicle_move(case, java):
    x, y, z = [
        bits_double(value) for value in java["start_position_bits"]]
    target_x, target_y, target_z = [
        bits_double(value) for value in java["target_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "runtime_packet_move", case["layout"],
        repr(x), repr(y), repr(z),
        repr(target_x), repr(target_y), repr(target_z),
        repr(case["packet_yaw"]), repr(case["packet_pitch"]),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]), case["source"],
    ], text=True)
    return json.loads(raw)


def native_client_vehicle_correction(case):
    raw = subprocess.check_output([
        str(NATIVE), "client_vehicle_correction",
        repr(case["predicted_x"]), repr(case["predicted_y"]),
        repr(case["predicted_z"]), repr(case["correction_x"]),
        repr(case["correction_y"]), repr(case["correction_z"]),
        repr(case["predicted_yaw"]), repr(case["predicted_pitch"]),
        repr(case["correction_yaw"]), repr(case["correction_pitch"]),
        repr(case["motion_x"]), repr(case["motion_y"]),
        repr(case["motion_z"]), str(int(case["on_ground"])),
        str(case["eid"]),
    ], text=True)
    return json.loads(raw)


def native_packet_chain(case, java):
    x, y, z = [
        bits_double(value) for value in java["start_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "packet_chain", case["layout"],
        repr(x), repr(y), repr(z),
        repr(case["packet_yaw"]), repr(case["packet_pitch"]),
        str(case["eid"]), str(case["entity_seed48"]),
        str(case["math_seed48"]),
    ], text=True)
    return json.loads(raw)


def runtime_vehicle_expected(case, java):
    return {
        "ok": True,
        "mode": "runtime_packet_move",
        "layout": java["layout"],
        "source": case["source"],
        "start_position_bits": java["start_position_bits"],
        "target_position_bits": java["target_position_bits"],
        "target_yaw_bits": java["target_yaw_bits"],
        "target_pitch_bits": java["target_pitch_bits"],
        "packet_seq": 1,
        "packet_pending": True,
        "next_packet_seq": 2,
        "client_position_bits": java["target_position_bits"],
        "server_packet_state": java["trace"][0]["packet_state"],
        "server_post_state": java["trace"][0]["post_state"],
    }


def native_dismount(case, java):
    pig_x, pig_y, pig_z = [
        bits_double(value) for value in java["pig_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "dismount",
        case["layout"], repr(case["yaw"]),
        repr(pig_x), repr(pig_y), repr(pig_z),
        str(java["player_eid"]), str(case["entity_seed48"]),
        str(case["next_entity_id"]),
    ], text=True)
    return json.loads(raw)


def native_death_dismount(case, java):
    pig_x, pig_y, pig_z = [
        bits_double(value) for value in java["pig_position_bits"]]
    raw = subprocess.check_output([
        str(NATIVE), "death_dismount",
        case["layout"], repr(case["yaw"]),
        repr(pig_x), repr(pig_y), repr(pig_z),
        str(java["player_eid"]), str(case["entity_seed48"]),
        str(case["next_entity_id"]),
    ], text=True)
    return json.loads(raw)


def mismatch(name, java, magma):
    if isinstance(java.get("trace"), list) and isinstance(
            magma.get("trace"), list):
        top_java = {key: value for key, value in java.items()
                    if key != "trace"}
        top_magma = {key: value for key, value in magma.items()
                     if key != "trace"}
        if top_java != top_magma:
            mismatch(name, top_java, top_magma)
        if len(java["trace"]) != len(magma["trace"]):
            raise AssertionError(
                f"{name}: trace lengths java={len(java['trace'])} "
                f"magma={len(magma['trace'])}")
        for tick, (java_row, magma_row) in enumerate(
                zip(java["trace"], magma["trace"])):
            if java_row != magma_row:
                keys = sorted(set(java_row) | set(magma_row))
                diff = {
                    key: {"java": java_row.get(key),
                          "magma": magma_row.get(key)}
                    for key in keys
                    if java_row.get(key) != magma_row.get(key)}
                raise AssertionError(
                    f"{name}: earliest tick {tick}: "
                    f"{json.dumps(diff, sort_keys=True)}")
        raise AssertionError(f"{name}: trace mismatch without differing row")
    keys = sorted(set(java) | set(magma))
    diff = {key: {"java": java.get(key), "magma": magma.get(key)}
            for key in keys if java.get(key) != magma.get(key)}
    raise AssertionError(f"{name}: {json.dumps(diff, sort_keys=True)}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    all_cases = [(name, boost_case(case)) for name, case in BOOST_CASES]
    all_cases += [(name, tick_case(case)) for name, case in TICK_CASES]
    all_cases += [(name, trace_case(case)) for name, case in TRACE_CASES]
    all_cases += [
        (name, lava_contact_case(case))
        for name, case in LAVA_CONTACT_CASES]
    all_cases += [
        (name, packet_contact_case(case))
        for name, case in PACKET_CONTACT_CASES]
    all_cases += [
        ("runtime_" + name,
         runtime_packet_contact_case(dict(case, ticks=1)))
        for name, case in PACKET_CONTACT_CASES]
    all_cases += [
        (name, vehicle_move_case(case))
        for name, case in VEHICLE_MOVE_CASES]
    all_cases += [
        (name, runtime_vehicle_move_case(case))
        for name, case in RUNTIME_VEHICLE_MOVE_CASES]
    all_cases += [
        (name, client_vehicle_correction_case(case))
        for name, case in CLIENT_VEHICLE_CORRECTION_CASES]
    all_cases += [
        (name, packet_chain_case(case))
        for name, case in PACKET_CHAIN_CASES]
    all_cases += [
        (name, dismount_case(case)) for name, case in DISMOUNT_CASES]
    all_cases += [
        (name, death_dismount_case(case))
        for name, case in DEATH_DISMOUNT_CASES]
    cases = [row for row in all_cases if not args.case or row[0] == args.case]
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
        # The QRL socket binds before the initial client chunks necessarily
        # settle.  Probe the real tick fixture and keep the server free while
        # waiting, rather than accepting an EmptyChunk as a physics arena.
        ready_deadline = time.monotonic() + 30.0
        while True:
            request(args.port, "server_step_lock")
            locked = True
            try:
                request(args.port, "pig_ride_locked", tick_case({}))
                break
            except RuntimeError as exc:
                request(args.port, "server_step_unlock")
                locked = False
                if ("loaded client chunk" not in str(exc)
                        or time.monotonic() >= ready_deadline):
                    raise
                time.sleep(0.5)
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_pig_ride_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, case in cases:
            try:
                command = (
                    "pig_dismount_locked" if case["mode"] == "dismount"
                    else "pig_death_dismount_tick_locked"
                    if case["mode"] == "death_dismount"
                    else "pig_ride_locked")
                oracle_case = dict(case)
                if oracle_case["mode"] == "runtime_packet_contact":
                    oracle_case["mode"] = "packet_contact"
                elif oracle_case["mode"] == "runtime_packet_move":
                    oracle_case["mode"] = "packet_move"
                    oracle_case.pop("source", None)
                java = request(args.port, command, oracle_case)
            except Exception as exc:
                raise RuntimeError(
                    f"{name}: Java oracle request failed") from exc
            if case["mode"] == "boost":
                magma = native_boost(case)
            elif case["mode"] == "tick":
                magma = native_tick(case, java)
            elif case["mode"] == "trace":
                magma = native_trace(case, java)
            elif case["mode"] == "lava_contact":
                magma = native_lava_contact(case, java)
            elif case["mode"] == "packet_contact":
                magma = native_packet_contact(case, java)
            elif case["mode"] == "runtime_packet_contact":
                magma = native_runtime_packet_contact(case, java)
            elif case["mode"] == "packet_move":
                magma = native_vehicle_move(case, java)
            elif case["mode"] == "runtime_packet_move":
                magma = native_runtime_vehicle_move(case, java)
                java = runtime_vehicle_expected(case, java)
            elif case["mode"] == "client_vehicle_correction":
                magma = native_client_vehicle_correction(case)
            elif case["mode"] == "packet_chain":
                magma = native_packet_chain(case, java)
            elif case["mode"] == "dismount":
                magma = native_dismount(case, java)
            else:
                magma = native_death_dismount(case, java)
            if java != magma:
                mismatch(name, java, magma)
        print(f"PASS java==magma: {len(cases)} pig boost/travel/packet/"
              "dismount/death transitions, raw movement, terminal update "
              "order, pose, collision order, passenger, inventory, and RNG")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
