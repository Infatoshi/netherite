#!/usr/bin/env python3
"""trace_java.py - GROUND-TRUTH side of the tick-trace oracle.

Replay a fixed action tape through the REAL Java Minecraft 1.11.2 client via the
existing qrl bridge (java/qrl_client.py) and write, per tick:
  - a COMPACT physics CSV (`java_phys.csv`) with the legacy columns the C tracer and
    frame_oracle already consume (kept for back-compat), AND
  - the FULL per-tick STATE VECTOR as JSONL (`java_state.jsonl`) in the canonical schema
    shared with the C tracer (app/trace_main.c). See canonicalize() for the schema.

SPAWN ALIGNMENT (critical): the C tracer spawns at the magma worldgen origin column
while Java spawns at the REAL world spawn, so a raw tick-0 diff is meaningless. Pass
`--spawn "X Y Z YAW PITCH"` (or `--spawn-file trace/out/c_spawn.txt`, written by the C
tracer) and this script teleports the Java player to that EXACT pose (via a runcmds `tp`)
AFTER reset and BEFORE the first tape tick. Then both sides start from the same tick-0
state and the per-tick diff is a fair test of physics/state evolution.

REQUIRES the Java client running with the qrl bridge on 127.0.0.1:25575 (root CLAUDE.md,
Run B/C). Typical launch on anvil (headless, display :1):

    cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
    # wait until a TCP connect to 127.0.0.1:25575 succeeds, then run this script.

Usage:
    python trace_java.py --tape trace/out/tape.txt --seed 0 \
        --out trace/out/java_phys.csv --state trace/out/java_state.jsonl \
        --spawn-file trace/out/c_spawn.txt
"""
import argparse
import json
import math
import os
import pathlib
import re
import sys
from pathlib import Path

import block_edit_sequence
import nbt_codec

# qrl_client.py lives in java/
_JAVA = str(Path(__file__).resolve().parents[2] / "java")


def canonical_stack_payload(payload):
    if not isinstance(payload, dict) or payload.get("kind") != "item_tag":
        return payload
    return {
        "kind": "item_tag",
        "nbt": nbt_codec.canonical_hex(payload.get("nbt")),
    }


def canonical_container(entry):
    value = {
        **entry,
        "items": sorted(
            entry.get("items", []), key=lambda item: item.get("slot")),
    }
    if value.get("type") == "shulker_box" \
            and "item_tag_nbt" in value:
        value["item_tag_nbt"] = nbt_codec.canonical_hex(
            value["item_tag_nbt"])
    return value

# Vanilla 1.11.2 applies gravity and drag after a grounded collision, so a
# motionless player standing on a normal block carries this vertical-motion
# tail between ticks.  A server teleport temporarily reports vy=0; accepting
# that correction frame as tape tick 0 creates a false Java-vs-C divergence.
VANILLA_GROUNDED_VY = (0.0 - 0.08) * 0.9800000190734863

if _JAVA not in sys.path:
    sys.path.insert(0, _JAVA)


def load_tape(path):
    rows = []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            v = [int(x) for x in s.split()]
            if len(v) not in (11, 12):
                raise ValueError(f"bad tape line ({len(v)} fields): {line!r}")
            if len(v) == 11:
                v.append(0)
            rows.append(v)
    return rows


def action_dict(v):
    (forward, back, left, right, jump, sneak, sprint,
     attack, use, yaw, pitch, close) = v
    return {
        "forward": forward, "back": back, "left": left, "right": right,
        "jump": jump, "sneak": sneak, "sprint": sprint,
        "attack": attack, "use": use, "yaw": yaw, "pitch": pitch,
        "close_container": close,
    }


def _g(ob, k, default=None):
    v = ob.get(k, default)
    return v


def canonicalize(tick, ob, scheduled_box=None, scheduled_blocks=(1,)):
    """Map a raw qrl obs dict into the canonical per-tick state schema (shared with the C
    tracer). A value of None means the field is present but unknown; the C side uses null
    for whole categories it does not simulate. Java simulates all of them, so nothing here
    is null under normal play."""
    authoritative = _g(ob, "authoritative", {}) or {}
    p = {
        "x": _g(ob, "x"), "y": _g(ob, "y"), "z": _g(ob, "z"),
        "yaw": _g(ob, "yaw"), "pitch": _g(ob, "pitch"),
        "vx": _g(ob, "vx"), "vy": _g(ob, "vy"), "vz": _g(ob, "vz"),
        "on_ground": 1 if authoritative.get(
            "on_ground", _g(ob, "on_ground")) else 0,
        "health": authoritative.get("health", _g(ob, "health")),
        "max_health": authoritative.get(
            "max_health", _g(ob, "max_health")),
        "absorption": authoritative.get(
            "absorption", _g(ob, "absorption")),
        "food": authoritative.get("food", _g(ob, "food")),
        "saturation": authoritative.get("saturation", _g(ob, "saturation")),
        "food_exhaustion": authoritative.get(
            "food_exhaustion", _g(ob, "food_exhaustion")),
        "food_timer": authoritative.get("food_timer", _g(ob, "food_timer")),
        "air": authoritative.get("air", _g(ob, "air")),
        "fire": authoritative.get("fire", _g(ob, "fire")),
        "position_update_ticks": authoritative.get(
            "position_update_ticks",
            _g(ob, "client_position_update_ticks")),
        "position_packet_pending": (
            1 if authoritative.get("position_packet_pending") else 0
        ),
        "xp_level": authoritative.get("xp", _g(ob, "xp")),
        "xp_frac": authoritative.get("xp_frac", _g(ob, "xp_frac")),
        "xp_total": authoritative.get("xp_total", _g(ob, "xp_total")),
        "fall_distance": _g(ob, "fall_distance"),
        "sprinting": 1 if _g(ob, "sprinting") else 0,
        "sneaking": 1 if _g(ob, "sneaking") else 0,
        "jumping": 1 if _g(ob, "jumping") else 0,
        "held_slot": authoritative.get("held_slot", _g(ob, "held_slot")),
        "held_id": authoritative.get("held_id", _g(ob, "held_id")),
        "held_count": authoritative.get("held_count", _g(ob, "held_count")),
        "held_meta": authoritative.get("held_meta", _g(ob, "held_meta")),
        "attack_cooldown": authoritative.get(
            "attack_cooldown", _g(ob, "attack_cooldown")),
        "attack_ticks": authoritative.get(
            "attack_ticks", _g(ob, "attack_ticks")),
        "hurt_time": authoritative.get("hurt_time", _g(ob, "hurt_time")),
        "hurt_resistant_time": authoritative.get(
            "hurt_resistant_time", _g(ob, "hurt_resistant_time")),
        "death_time": authoritative.get("death_time", _g(ob, "death_time")),
        "dead": 1 if _g(ob, "dead") else 0,
        "deaths": _g(ob, "deaths"),
        "dim": authoritative.get("dim", _g(ob, "dim")),
        "potions": authoritative.get("potions", _g(ob, "potions", [])),
    }
    inv = authoritative.get("inventory", _g(ob, "inventory", []))
    ents = [
        dict(entity)
        for entity in authoritative.get(
            "entities", _g(ob, "entities", []))
    ]
    for entity in ents:
        entity["stack_payload"] = canonical_stack_payload(
            entity.get("stack_payload"))
        if entity.get("type") == "EntityFallingBlock" and all(
                key in entity
                for key in ("origin_x", "origin_y", "origin_z",
                            "block", "meta")):
            entity["identity"] = (
                f"falling:{entity['origin_x']}:{entity['origin_y']}:"
                f"{entity['origin_z']}:{entity['block']}:{entity['meta']}"
            )
    time = dict(_g(ob, "time", {}) or {})
    if "world_time" in authoritative:
        time["world_time"] = authoritative["world_time"]
    if "total_time" in authoritative:
        time["total_time"] = authoritative["total_time"]
    if "raining" in authoritative:
        time["raining"] = bool(authoritative["raining"])
    if "thundering" in authoritative:
        time["thundering"] = bool(authoritative["thundering"])
    for weather_field in (
        "rain_time", "thunder_time", "clean_weather_time",
        "do_weather_cycle", "do_daylight_cycle", "prev_rain_strength",
        "rain_strength", "prev_thunder_strength", "thunder_strength",
    ):
        if weather_field in authoritative:
            time[weather_field] = authoritative[weather_field]
    diagnostics = {
        "client_in_water": _g(ob, "in_water"),
        "client_bb_min_y": _g(ob, "bb_min_y"),
        "client_bb_max_y": _g(ob, "bb_max_y"),
        "client_on_ground": _g(ob, "on_ground"),
        "client_position_update_ticks": _g(
            ob, "client_position_update_ticks"),
        "client_last_reported_x": _g(ob, "client_last_reported_x"),
        "client_last_reported_y": _g(ob, "client_last_reported_y"),
        "client_last_reported_z": _g(ob, "client_last_reported_z"),
        "client_prev_on_ground": _g(ob, "client_prev_on_ground"),
        "client_look": _g(ob, "look"),
        "client_potions": _g(ob, "potions", []),
        "client_movement_speed_attr": _g(ob, "movement_speed_attr"),
        "server_x": authoritative.get("x"),
        "server_y": authoritative.get("y"),
        "server_z": authoritative.get("z"),
        "server_vx": authoritative.get("vx"),
        "server_vy": authoritative.get("vy"),
        "server_vz": authoritative.get("vz"),
        "server_sprinting": authoritative.get("sprinting"),
        "server_on_ground": authoritative.get("on_ground"),
        "server_in_water": authoritative.get("in_water"),
        "server_bb_min_y": authoritative.get("bb_min_y"),
        "server_bb_max_y": authoritative.get("bb_max_y"),
        "server_fall_distance": authoritative.get("fall_distance"),
        "server_movement_speed_attr": authoritative.get(
            "movement_speed_attr"),
        "server_net_last_good_x": authoritative.get("net_last_good_x"),
        "server_net_last_good_y": authoritative.get("net_last_good_y"),
        "server_net_last_good_z": authoritative.get("net_last_good_z"),
        "server_net_move_packet_counter": authoritative.get(
            "net_move_packet_counter"),
        "server_armor": authoritative.get("armor", []),
        "tnt_detonation": authoritative.get("tnt_detonation"),
    }
    # Java's TreeSet iteration rank is the only public, save/reload-safe
    # representation of the private insertion id.  Normalize it after
    # selecting the exact inert-stone slice: unrelated pending updates may
    # drain between observations and change the absolute TreeSet rank, while
    # the relative order of the represented entries remains stable.
    scheduled_ticks = []
    scheduled_tick_context = []
    for entry in authoritative.get("scheduled_ticks", []):
        if entry.get("block") not in scheduled_blocks:
            continue
        if scheduled_box is not None:
            x0, y0, z0, x1, y1, z1 = scheduled_box
            if not (
                x0 <= entry.get("x", x0 - 1) <= x1
                and y0 <= entry.get("y", y0 - 1) <= y1
                and z0 <= entry.get("z", z0 - 1) <= z1
            ):
                continue
        normalized = {
            field: entry.get(field)
            for field in (
                "x", "y", "z", "block", "time", "priority", "order"
            )
        }
        normalized["order"] = len(scheduled_ticks)
        scheduled_ticks.append(normalized)
        if entry.get("block") == 51:
            scheduled_tick_context.append({
                "x": entry.get("x"),
                "y": entry.get("y"),
                "z": entry.get("z"),
                "block": 51,
                "high_humidity": entry.get("fire_high_humidity"),
                "difficulty": entry.get("fire_difficulty"),
                "do_fire_tick": entry.get("fire_tick_enabled"),
                "raining": entry.get("fire_raining"),
                "rain_time": entry.get("fire_rain_time"),
                "thunder_time": entry.get("fire_thunder_time"),
                "raining_at": entry.get("fire_raining_at"),
                "raining_at_west": entry.get("fire_raining_at_west"),
                "raining_at_east": entry.get("fire_raining_at_east"),
                "raining_at_north": entry.get("fire_raining_at_north"),
                "raining_at_south": entry.get("fire_raining_at_south"),
                "rain_can_die_west_candidate": entry.get(
                    "fire_rain_can_die_west_candidate"),
            })
    comparators = sorted(
        authoritative.get("comparators", []),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    containers = sorted(
        (canonical_container(entry)
         for entry in authoritative.get("containers", [])),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z"),
            entry.get("type"),
        ),
    )
    flower_pots = sorted(
        (
            {
                field: entry.get(field)
                for field in ("x", "y", "z", "item", "meta")
            }
            for entry in authoritative.get("flower_pots", [])
        ),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    skulls = sorted(
        (
            {
                field: entry.get(field)
                for field in (
                    "x", "y", "z", "type", "rotation", "has_owner"
                )
            } | ({"owner_nbt": entry.get("owner_nbt")}
                 if entry.get("has_owner") is True else {})
            for entry in authoritative.get("skulls", [])
        ),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    moving_pistons = sorted(
        (
            {
                field: entry.get(field)
                for field in (
                    "x", "y", "z", "moved_block", "moved_meta",
                    "facing", "extending", "source",
                    "progress_bits", "last_progress_bits",
                )
            }
            for entry in authoritative.get("moving_pistons", [])
        ),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    item_frames = sorted(
        (
            {
                field: entry.get(field)
                for field in (
                    "eid", "x", "y", "z",
                    "hanging_x", "hanging_y", "hanging_z",
                    "facing", "item", "count", "meta", "rotation",
                )
            }
            for entry in authoritative.get("item_frames", [])
        ),
        key=lambda entry: (
            entry.get("hanging_x"), entry.get("hanging_y"),
            entry.get("hanging_z"), entry.get("eid"),
        ),
    )
    return {
        "tick": tick,
        "do_entity_drops": authoritative.get("do_entity_drops", True),
        "entity_id_cursor": authoritative.get("next_entity_id"),
        "world_rng": {
            "java_seed48": authoritative.get("world_rand_seed48"),
            "java_have_gaussian": authoritative.get(
                "world_rand_have_gaussian", False),
            "java_gaussian": authoritative.get(
                "world_rand_gaussian", 0.0),
            "math_seed48": authoritative.get("math_rand_seed48"),
            "block_seed48": authoritative.get("block_rand_seed48"),
            "update_lcg": authoritative.get("world_update_lcg"),
        },
        "controlled_input": (
            {
                "before": {
                    "entity_id_cursor": (
                        authoritative["controlled_input"].get("before")
                        or {}).get("next_entity_id"),
                    "world_rng": {
                        "java_seed48": (
                            authoritative["controlled_input"].get("before")
                            or {}).get("world_rand_seed48"),
                        "math_seed48": (
                            authoritative["controlled_input"].get("before")
                            or {}).get("math_rand_seed48"),
                        "block_seed48": (
                            authoritative["controlled_input"].get("before")
                            or {}).get("block_rand_seed48"),
                        "update_lcg": (
                            authoritative["controlled_input"].get("before")
                            or {}).get("world_update_lcg"),
                    },
                },
                "entity_id_cursor": authoritative["controlled_input"].get(
                    "next_entity_id"),
                "world_rng": {
                    "java_seed48": authoritative["controlled_input"].get(
                        "world_rand_seed48"),
                    "math_seed48": authoritative["controlled_input"].get(
                        "math_rand_seed48"),
                    "block_seed48": authoritative["controlled_input"].get(
                        "block_rand_seed48"),
                    "update_lcg": authoritative["controlled_input"].get(
                        "world_update_lcg"),
                },
            }
            if isinstance(authoritative.get("controlled_input"), dict)
            else None
        ),
        "scheduled_callback": (
            {
                field: authoritative["scheduled_callback"].get(field)
                for field in (
                    "x", "y", "z", "block", "public_seed",
                    "due_time", "total_time",
                    "before_seed48", "after_seed48",
                )
            }
            if isinstance(authoritative.get("scheduled_callback"), dict)
            else None
        ),
        "player": p,
        "inventory": inv,
        "entities": ents,
        "scheduled_ticks": scheduled_ticks,
        "scheduled_ticks_complete": authoritative.get(
            "scheduled_ticks_complete", False),
        "scheduled_tick_context": scheduled_tick_context,
        "comparators": comparators,
        "comparators_complete": authoritative.get(
            "comparators_complete", False),
        "containers": containers,
        "containers_complete": authoritative.get(
            "containers_complete", False),
        "flower_pots": flower_pots,
        "flower_pots_complete": authoritative.get(
            "flower_pots_complete", False),
        "skulls": skulls,
        "skulls_complete": authoritative.get(
            "skulls_complete", False),
        "moving_pistons": moving_pistons,
        "moving_pistons_complete": authoritative.get(
            "moving_pistons_complete", False),
        "item_frames": item_frames,
        "item_frames_complete": authoritative.get(
            "item_frames_complete", False),
        "redstone_torch_toggles": authoritative.get(
            "redstone_torch_toggles", []),
        "redstone_torch_toggles_complete": authoritative.get(
            "redstone_torch_toggles_complete", False),
        "time": time,
        "diagnostics": diagnostics,
    }


def phys_row(tick, ob):
    """Legacy java_phys.csv row (unchanged columns)."""
    og = 1 if ob.get("on_ground") else 0
    return [
        tick,
        repr(float(ob["x"])), repr(float(ob["y"])), repr(float(ob["z"])),
        repr(float(ob["yaw"])), repr(float(ob["pitch"])),
        repr(float(ob["vx"])), repr(float(ob["vy"])), repr(float(ob["vz"])),
        og,
        repr(float(ob["health"])), repr(float(ob["food"])),
        int(ob.get("air", -1)),
        0,  # frame_hash: not grabbed per tick (disk efficiency)
    ]


def parse_spawn(args):
    if args.spawn:
        parts = args.spawn.split()
    elif args.spawn_file and os.path.exists(args.spawn_file):
        with open(args.spawn_file) as f:
            parts = f.read().split()
    else:
        return None
    if len(parts) < 5:
        raise ValueError(f"spawn needs 5 numbers (X Y Z YAW PITCH); got {parts!r}")
    # New sidecars carry the settled motion/on-ground/fall tail as well. Older
    # five-field files remain valid and use the ordinary grounded expectation.
    return [float(x) for x in parts[:10]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", default="trace/out/tape.txt")
    ap.add_argument("--out", default="trace/out/java_phys.csv")
    ap.add_argument("--state", default="trace/out/java_state.jsonl")
    ap.add_argument(
        "--state-before-out",
        help="write the final aligned canonical pre-tick state as one JSON object "
             "(paired with --blocks-before-out for a state capsule)",
    )
    ap.add_argument(
        "--checkpoint-tick",
        type=int,
        help="after this tape observation, dump a parked checkpoint state "
             "and block cuboid",
    )
    ap.add_argument("--checkpoint-state-out")
    ap.add_argument("--checkpoint-blocks-out")
    ap.add_argument("--checkpoint-block-light-out")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument(
        "--dimension",
        type=int,
        choices=(-1, 0, 1),
        default=0,
        help="transfer the controlled player to this dimension before staging",
    )
    ap.add_argument("--fresh", action="store_true",
                    help="delete/recreate qrl_<seed> before tracing (reproducible default in run_oracle)")
    ap.add_argument("--empty-inventory", action="store_true",
                    help="clear the Java player inventory before tick 0")
    ap.add_argument(
        "--held-item-fixture", nargs=3,
        metavar=("ITEM", "COUNT", "META"),
        help="put one named item stack in selected hotbar slot 0 before tick 0",
    )
    ap.add_argument(
        "--offhand-item-fixture", nargs=3,
        metavar=("ITEM", "COUNT", "META"),
        help="put one named item stack in offhand slot 40 before tick 0",
    )
    ap.add_argument(
        "--armor-fixture", type=int, nargs=5,
        metavar=("SLOT", "ITEM", "META", "ENCHANTMENT", "LEVEL"),
        help="one exact armor stack; ENCHANTMENT=-1 and LEVEL=0 means plain",
    )
    ap.add_argument("--clean-entities", action="store_true",
                    help="disable natural spawning and remove every non-player entity "
                         "before tick 0 (controlled full-runtime entity comparisons)")
    ap.add_argument(
        "--allow-falling-entities",
        action="store_true",
        help="retain ordinary EntityFallingBlock spawning; the controlled "
             "default forces BlockFalling's immediate path so unrelated "
             "remote scheduled sand cannot consume tick-0 RNG/entity IDs",
    )
    ap.add_argument("--freeze-time", action="store_true",
                    help="freeze world time 6000 and stage the selected weather before tick 0")
    ap.add_argument(
        "--weather-mode", choices=("clear", "rain", "thunder"),
        default="clear",
        help="weather staged by --freeze-time; precipitation keeps vanilla "
             "weather-cycle ticking",
    )
    ap.add_argument(
        "--do-fire-tick", choices=("on", "off"), default="on",
        help="set the doFireTick gamerule before fixture staging",
    )
    ap.add_argument(
        "--do-entity-drops", choices=("on", "off"), default="on",
        help="set the doEntityDrops gamerule before fixture staging",
    )
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--spawn", default=None,
                    help='align tick 0: teleport Java to "X Y Z YAW PITCH" before the tape')
    ap.add_argument("--spawn-file", default="trace/out/c_spawn.txt",
                    help="read the spawn pose from a file (written by the C tracer)")
    ap.add_argument("--platform", type=int, default=0,
                    help="stage an NxN solid stone platform under the spawn and clear six "
                         "blocks above it so the Java player is GROUNDED with unobstructed "
                         "headroom at the same y as C (isolates the physics MODEL from the "
                         "two worlds' terrain mismatch). 0 = off (raw terrain).")
    ap.add_argument(
        "--platform-clear-height",
        type=int,
        default=6,
        help="number of air blocks staged above the optional platform",
    )
    ap.add_argument("--blocks-out", default=None,
                    help="write the post-tick raw block cuboid in qrl getblocks format")
    ap.add_argument("--blocks-before-out", default=None,
                    help="also write the same cuboid immediately before tape tick 0")
    ap.add_argument("--block-light-out", default=None,
                    help="write one post-tick block-light byte per --blocks-box cell")
    ap.add_argument("--block-light-before-out", default=None,
                    help="write the parked pre-tick block-light cuboid")
    ap.add_argument("--sky-light-out", default=None,
                    help="write one post-tick skylight byte per --blocks-box cell")
    ap.add_argument("--sky-light-before-out", default=None,
                    help="write the parked pre-tick skylight cuboid")
    ap.add_argument("--blocks-box", type=int, nargs=6,
                    metavar=("X0", "Y0", "Z0", "X1", "Y1", "Z1"),
                    help="inclusive cuboid paired with --blocks-out")
    ap.add_argument("--set-block", type=int, nargs=5, action="append", default=[],
                    metavar=("X", "Y", "Z", "ID", "META"),
                    help="stage an identical numeric block fixture before tape tick 0")
    ap.add_argument("--late-set-block", type=int, nargs=5, action="append", default=[],
                    metavar=("X", "Y", "Z", "ID", "META"),
                    help="stage a numeric block fixture at the parked pre-tick server "
                         "boundary, after spawn alignment and all setup ticks")
    ap.add_argument("--final-set-block", type=int, nargs=5, action="append", default=[],
                    metavar=("X", "Y", "Z", "ID", "META"),
                    help="drain two queued movement-packet ticks, then stage a "
                         "numeric block fixture at the final parked pre-tick "
                         "boundary without advancing its scheduled work")
    ap.add_argument(
        "--fixture-drain-ticks", type=int, default=0,
        help="controlled setup ticks to drain after early fixture staging",
    )
    ap.add_argument("--initial-fire", type=int,
                    help="set the authoritative signed Entity.fire counter at the "
                         "parked pre-tick server boundary")
    ap.add_argument("--initial-food", type=int, choices=range(21),
                    help="set the authoritative food level at the parked "
                         "pre-tick server boundary")
    ap.add_argument("--clear-hurt", action="store_true",
                    help="clear hurt resistance and post-respawn invulnerability at "
                         "the parked pre-tick server boundary")
    ap.add_argument("--normalize-move-packets", action="store_true",
                    help="reset the client movement-packet cursor to a quiescent "
                         "pre-tick state")
    ap.add_argument(
        "--potion-fixture",
        type=int,
        nargs=3,
        metavar=("ID", "AMPLIFIER", "DURATION"),
        help="replace active effects with one exact potion at the parked "
             "pre-tick boundary",
    )
    ap.add_argument(
        "--second-potion-fixture",
        type=int,
        nargs=3,
        metavar=("ID", "AMPLIFIER", "DURATION"),
        help="add a second exact active effect to --potion-fixture",
    )
    ap.add_argument(
        "--player-xp-fixture",
        nargs=3,
        metavar=("LEVEL", "FRACTION", "TOTAL"),
        help="set the complete authoritative player XP state at the parked "
             "pre-tick boundary",
    )
    ap.add_argument(
        "--player-combat-fixture",
        type=int,
        nargs=4,
        metavar=("ATTACK_TICKS", "HURT_TIME", "HURT_RESISTANT", "DEATH_TIME"),
        help="set exact player combat cursors at the parked pre-tick boundary",
    )
    ap.add_argument(
        "--xp-orb-offset",
        type=float,
        nargs=4,
        metavar=("DX", "DY", "DZ", "VALUE"),
        help="spawn one deterministic XP orb at a player-relative offset while "
             "the authoritative server is parked before tick 0",
    )
    ap.add_argument(
        "--mob-offset",
        type=float,
        nargs=4,
        metavar=("DX", "DY", "DZ", "HEALTH"),
        help="spawn one deterministic NoAI pig at a player-relative offset while "
             "the authoritative server is parked before tick 0",
    )
    ap.add_argument(
        "--villager-offset",
        type=float,
        nargs=5,
        metavar=("DX", "DY", "DZ", "PROFESSION", "ENTITY_SEED48"),
        help="spawn one unopened NoAI villager with an exact private RNG "
             "cursor at a player-relative offset before tick 0",
    )
    ap.add_argument(
        "--mob-collision",
        action="store_true",
        help="use a taskless, gravity-free but collision-updating pig fixture",
    )
    ap.add_argument(
        "--item-offset",
        type=float,
        nargs=6,
        metavar=("DX", "DY", "DZ", "ITEM", "COUNT", "META"),
        help="spawn one stationary, gravity-free EntityItem at a "
             "player-relative offset while the authoritative server is "
             "parked before tick 0",
    )
    ap.add_argument(
        "--arrow-offset",
        type=float,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="spawn one stationary, gravity-free EntityTippedArrow at a "
             "player-relative offset while the authoritative server is "
             "parked before tick 0",
    )
    ap.add_argument(
        "--arrow-fire-seconds",
        type=int,
        default=0,
        help="set the stationary arrow on fire for this many seconds",
    )
    ap.add_argument(
        "--primed-tnt-offset",
        type=float,
        nargs=7,
        metavar=("DX", "DY", "DZ", "VX", "VY", "VZ", "FUSE"),
        help="spawn one exact EntityTNTPrimed save-state fixture while the "
             "authoritative server is parked before tick 0",
    )
    ap.add_argument(
        "--second-primed-tnt-offset",
        type=float,
        nargs=7,
        metavar=("DX", "DY", "DZ", "VX", "VY", "VZ", "FUSE"),
        help="spawn a second exact EntityTNTPrimed save-state fixture after "
             "the first primed-TNT fixture",
    )
    ap.add_argument(
        "--end-crystal-offset",
        type=float,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="spawn one exact EntityEnderCrystal save-state fixture after "
             "the primary primed-TNT fixture",
    )
    ap.add_argument(
        "--end-crystal-beam-target-offset",
        type=int,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="give the exact End-crystal fixture a beam target relative to "
             "the settled player block",
    )
    ap.add_argument(
        "--boat-offset",
        type=float,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="spawn one stationary, gravity-free EntityBoat at a "
             "player-relative offset while the authoritative server is "
             "parked before tick 0",
    )
    ap.add_argument(
        "--small-fireball-offset",
        type=float,
        nargs=9,
        metavar=("DX", "DY", "DZ", "VX", "VY", "VZ", "AX", "AY", "AZ"),
        help="spawn one exact EntitySmallFireball trajectory fixture while "
             "the authoritative server is parked before tick 0",
    )
    ap.add_argument(
        "--entity-fixture-out",
        help="write the exact locked entity fixture, including server eid, for magma",
    )
    ap.add_argument(
        "--primed-tnt-fixture-out",
        help="write the primed-TNT fixture when paired with another fixture",
    )
    ap.add_argument(
        "--second-primed-tnt-fixture-out",
        help="write the second primed-TNT fixture in a bounded two-TNT row",
    )
    ap.add_argument(
        "--end-crystal-fixture-out",
        help="write the exact locked End-crystal fixture, including server eid",
    )
    ap.add_argument(
        "--item-fixture-out",
        help="write the item fixture when paired with a primed-TNT fixture",
    )
    ap.add_argument(
        "--boat-fixture-out",
        help="write the boat fixture when paired with a primed-TNT fixture",
    )
    ap.add_argument(
        "--mob-fixture-out",
        help="write the mob fixture when paired with a primed-TNT fixture",
    )
    ap.add_argument(
        "--villager-fixture-out",
        help="write the exact unopened villager fixture for audit",
    )
    ap.add_argument(
        "--mob-after-primed-tnt",
        action="store_true",
        help="spawn the bounded paired mob after primed TNT to preserve the "
             "opposite loaded-entity update order",
    )
    ap.add_argument(
        "--item-after-primed-tnt",
        action="store_true",
        help="spawn the bounded paired item after primed TNT to preserve the "
             "opposite loaded-entity update order",
    )
    ap.add_argument(
        "--scheduled-tick-offset",
        type=int,
        nargs=6,
        metavar=("DX", "DY", "DZ", "BLOCK", "DELAY", "PRIORITY"),
        help="schedule one exact pending block update at an integer "
             "player-relative offset while the server is parked",
    )
    ap.add_argument(
        "--scheduled-tick-seed",
        type=int,
        help="replace any same-cell pending update and reseed World.rand "
             "immediately before the controlled scheduled callback",
    )
    ap.add_argument(
        "--comparator-output-offset",
        type=int,
        nargs=4,
        metavar=("DX", "DY", "DZ", "OUTPUT"),
        help="restore one comparator TileEntity output at a player-relative "
             "parked pre-tick position",
    )
    ap.add_argument(
        "--post-comparator-set-block-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "META"),
        help="stage one relative block after restoring comparator tile output",
    )
    ap.add_argument(
        "--container-slot-offset",
        type=int,
        nargs=7,
        metavar=("DX", "DY", "DZ", "SLOT", "ITEM", "COUNT", "META"),
        help="restore one supported container slot at a player-relative "
             "parked pre-tick position",
    )
    ap.add_argument(
        "--container-fill-offset",
        type=int,
        nargs=7,
        metavar=("DX", "DY", "DZ", "SLOTS", "ITEM", "COUNT", "META"),
        help="restore slots 0..SLOTS-1 of one supported container at a "
             "player-relative parked pre-tick position",
    )
    ap.add_argument(
        "--shulker-nbt-offset",
        nargs=4,
        metavar=("DX", "DY", "DZ", "NBT_JSON"),
        help="restore a canonical shulker saveToNbt document from typed JSON "
             "at a player-relative parked pre-tick position",
    )
    ap.add_argument(
        "--flower-pot-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "ITEM", "META"),
        help="restore one flower-pot item payload at a player-relative "
             "parked pre-tick position",
    )
    ap.add_argument(
        "--skull-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "TYPE", "ROTATION"),
        help="restore one skull tile at a player-relative parked "
             "pre-tick position",
    )
    ap.add_argument(
        "--skull-owner",
        nargs=5,
        metavar=("NAME", "UUID", "PROPERTY", "VALUE", "SIGNATURE"),
        help="attach a complete signed GameProfile to --skull-offset type 3",
    )
    ap.add_argument(
        "--command-success-offset",
        type=int,
        nargs=4,
        metavar=("DX", "DY", "DZ", "SUCCESS"),
        help="restore one inert command-block SuccessCount at a "
             "player-relative parked pre-tick position",
    )
    ap.add_argument(
        "--item-frame-offset",
        type=int,
        nargs=7,
        metavar=("DX", "DY", "DZ", "FACING", "ITEM", "META", "ROTATION"),
        help="spawn one exact item-frame comparator source at a "
             "player-relative parked pre-tick hanging cell",
    )
    ap.add_argument(
        "--scheduled-capture-block",
        type=int,
        action="append",
        default=[],
        metavar="BLOCK",
        help="include naturally-created pending updates for this block ID in "
             "the canonical scheduled-tick slice",
    )
    ap.add_argument(
        "--random-tick-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "SEED"),
        help="queue one real Block.randomTick callback at an integer "
             "player-relative offset for tape tick 0; SEED is passed to "
             "java.util.Random.setSeed before the callback",
    )
    ap.add_argument(
        "--random-selection-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "SEED"),
        help="isolate one loaded random-tick section and let the ordinary "
             "WorldServer selector choose this player-relative block on "
             "tape tick 0",
    )
    ap.add_argument(
        "--random-selection-fixture-out",
        help="write the isolated selector's exact loaded-order/LCG pre-advance "
             "metadata for magma",
    )
    ap.add_argument(
        "--world-random-seed48",
        type=int,
        help="set World.rand's internal 48-bit java.util.Random cursor at "
             "the final parked pre-tick boundary",
    )
    ap.add_argument(
        "--block-random-seed48",
        type=int,
        help="set Block.RANDOM's internal 48-bit java.util.Random cursor at "
             "the final parked pre-tick boundary",
    )
    ap.add_argument(
        "--tick0-set-block-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "META"),
        help="queue one real server-thread setBlockState at an integer "
             "player-relative position for tape tick 0",
    )
    ap.add_argument(
        "--tick0-harvest-offset",
        type=int,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="queue one real server PlayerInteractionManager harvest at an "
             "integer player-relative position for tape tick 0",
    )
    ap.add_argument(
        "--block-edit-sequence",
        help="queue player-relative block edits before selected tape ticks; "
             "rows are TICK DX DY DZ BLOCK META",
    )
    args = ap.parse_args()
    if (args.blocks_out is None) != (args.blocks_box is None):
        ap.error("--blocks-out and --blocks-box must be supplied together")
    if args.blocks_before_out and args.blocks_box is None:
        ap.error("--blocks-before-out requires --blocks-box")
    if bool(args.block_light_out) != bool(args.block_light_before_out):
        ap.error("--block-light-out and --block-light-before-out must be "
                 "supplied together")
    if args.block_light_out and args.blocks_box is None:
        ap.error("block-light outputs require --blocks-box")
    if bool(args.sky_light_out) != bool(args.sky_light_before_out):
        ap.error("--sky-light-out and --sky-light-before-out must be "
                 "supplied together")
    if args.sky_light_out and args.blocks_box is None:
        ap.error("sky-light outputs require --blocks-box")
    if args.state_before_out and not args.blocks_before_out:
        ap.error("--state-before-out requires --blocks-before-out")
    checkpoint_outputs = (
        args.checkpoint_state_out,
        args.checkpoint_blocks_out,
    )
    if (args.checkpoint_tick is not None) != (
            all(checkpoint_outputs)):
        ap.error("--checkpoint-tick, --checkpoint-state-out, and "
                 "--checkpoint-blocks-out must be supplied together")
    if any(checkpoint_outputs) and not all(checkpoint_outputs):
        ap.error("--checkpoint-state-out and --checkpoint-blocks-out "
                 "must be supplied together")
    if args.checkpoint_blocks_out and args.blocks_box is None:
        ap.error("checkpoint outputs require --blocks-box")
    if args.checkpoint_block_light_out and args.checkpoint_tick is None:
        ap.error("--checkpoint-block-light-out requires --checkpoint-tick")
    if args.initial_fire is not None and not -20 <= args.initial_fire <= 32767:
        ap.error("--initial-fire must be in -20..32767")
    if args.potion_fixture:
        potion_id, amplifier, duration = args.potion_fixture
        if not (1 <= potion_id <= 255 and 0 <= amplifier <= 255
                and duration > 0):
            ap.error("--potion-fixture requires ID 1..255, amplifier 0..255, "
                     "and positive duration")
    if args.second_potion_fixture:
        potion_id, amplifier, duration = args.second_potion_fixture
        if not args.potion_fixture:
            ap.error("--second-potion-fixture requires --potion-fixture")
        if not (1 <= potion_id <= 255 and 0 <= amplifier <= 255
                and duration > 0
                and potion_id != args.potion_fixture[0]):
            ap.error("--second-potion-fixture requires a distinct valid effect")
    if args.player_xp_fixture:
        try:
            level = int(args.player_xp_fixture[0])
            fraction = float(args.player_xp_fixture[1])
            total = int(args.player_xp_fixture[2])
        except ValueError:
            ap.error("--player-xp-fixture requires integer LEVEL/TOTAL and "
                     "numeric FRACTION")
        if not (0 <= level <= 21863 and math.isfinite(fraction)
                and 0 <= fraction < 1 and total >= 0):
            ap.error("--player-xp-fixture values are out of range")
        args.player_xp_fixture = (level, fraction, total)
    if args.player_combat_fixture:
        attack_ticks, hurt_time, hurt_resistant, death_time = \
            args.player_combat_fixture
        if not (0 <= attack_ticks <= 1000000000
                and 0 <= hurt_time <= 20
                and 0 <= hurt_resistant <= 20
                and 0 <= death_time <= 20):
            ap.error("--player-combat-fixture values are out of range")
    if args.armor_fixture:
        slot, item, meta, enchantment, level = args.armor_fixture
        if (not 36 <= slot <= 39 or not 1 <= item <= 4095
                or not 0 <= meta <= 32767
                or not ((enchantment == -1 and level == 0)
                        or (0 <= enchantment <= 32767
                            and 1 <= level <= 32767))):
            ap.error("--armor-fixture requires SLOT 36..39, ITEM 1..4095, "
                     "META 0..32767, and either ENCHANTMENT=-1 LEVEL=0 "
                     "or a non-negative enchantment with positive level")
    if sum(bool(value) for value in (
            args.set_block, args.late_set_block, args.final_set_block)) > 1:
        ap.error("--set-block, --late-set-block, and --final-set-block "
                 "are mutually exclusive")
    if not 0 <= args.fixture_drain_ticks <= 1000:
        ap.error("--fixture-drain-ticks must be in 0..1000")
    if not 1 <= args.platform_clear_height <= 32:
        ap.error("--platform-clear-height must be in 1..32")
    if args.fixture_drain_ticks and not args.set_block:
        ap.error("--fixture-drain-ticks requires an early --set-block fixture")
    entity_offsets = (
        args.xp_orb_offset,
        args.mob_offset,
        args.villager_offset,
        args.item_offset,
        args.arrow_offset,
        args.primed_tnt_offset,
        args.end_crystal_offset,
        args.boat_offset,
        args.small_fireball_offset,
    )
    entity_count = sum(bool(value) for value in entity_offsets)
    paired_mob_tnt = bool(
        entity_count == 2 and args.mob_offset and args.primed_tnt_offset)
    paired_item_tnt = bool(
        entity_count == 2 and args.item_offset and args.primed_tnt_offset)
    paired_boat_tnt = bool(
        entity_count == 2 and args.boat_offset and args.primed_tnt_offset)
    paired_arrow_tnt = bool(
        entity_count == 2 and args.arrow_offset and args.primed_tnt_offset)
    paired_xp_tnt = bool(
        entity_count == 2 and args.xp_orb_offset and args.primed_tnt_offset)
    paired_small_fireball_tnt = bool(
        entity_count == 2
        and args.small_fireball_offset and args.primed_tnt_offset)
    paired_tnt_tnt = bool(
        entity_count == 1 and args.primed_tnt_offset
        and args.second_primed_tnt_offset)
    paired_end_crystal_tnt = bool(
        entity_count == 2
        and args.end_crystal_offset and args.primed_tnt_offset)
    if entity_count > 1 and not (
            paired_mob_tnt or paired_item_tnt or paired_boat_tnt
            or paired_arrow_tnt or paired_xp_tnt
            or paired_small_fireball_tnt or paired_end_crystal_tnt):
        ap.error("--xp-orb-offset, --mob-offset, --villager-offset, "
                 "--item-offset, "
                 "--arrow-offset, --primed-tnt-offset, --end-crystal-offset, "
                 "--boat-offset, and --small-fireball-offset are mutually "
                 "exclusive except for the bounded "
                 "mob/item/boat/arrow/XP/small-fireball/End-crystal + "
                 "primed-TNT pairs")
    if args.second_primed_tnt_offset and not paired_tnt_tnt:
        ap.error("--second-primed-tnt-offset requires a bounded two-TNT row")
    if args.mob_collision and not args.mob_offset:
        ap.error("--mob-collision requires --mob-offset")
    if args.villager_offset:
        _dx, _dy, _dz, profession, seed48 = args.villager_offset
        if (not profession.is_integer() or not 0 <= profession <= 5
                or not seed48.is_integer() or not 0 <= seed48 < (1 << 48)):
            ap.error("--villager-offset requires integer PROFESSION 0..5 "
                     "and ENTITY_SEED48 0..2^48-1")
    if args.mob_after_primed_tnt and not paired_mob_tnt:
        ap.error("--mob-after-primed-tnt requires the bounded mob + "
                 "primed-TNT pair")
    if args.item_after_primed_tnt and not paired_item_tnt:
        ap.error("--item-after-primed-tnt requires the bounded item + "
                 "primed-TNT pair")
    if args.arrow_fire_seconds and not args.arrow_offset:
        ap.error("--arrow-fire-seconds requires --arrow-offset")
    if not 0 <= args.arrow_fire_seconds <= 1638:
        ap.error("--arrow-fire-seconds must be in 0..1638")
    if (args.primed_tnt_offset
            and (not args.primed_tnt_offset[6].is_integer()
                 or not 1 <= args.primed_tnt_offset[6] <= 32767)):
        ap.error("--primed-tnt-offset requires integer FUSE in 1..32767")
    if (args.second_primed_tnt_offset
            and (not args.second_primed_tnt_offset[6].is_integer()
                 or not 1 <= args.second_primed_tnt_offset[6] <= 32767)):
        ap.error(
            "--second-primed-tnt-offset requires integer FUSE in 1..32767")
    if paired_tnt_tnt:
        if (args.entity_fixture_out
                or not args.primed_tnt_fixture_out
                or not args.second_primed_tnt_fixture_out):
            ap.error("the two-TNT row requires distinct first and second "
                     "primed-TNT fixture output paths")
    elif paired_end_crystal_tnt:
        if (args.entity_fixture_out
                or not args.primed_tnt_fixture_out
                or not args.end_crystal_fixture_out):
            ap.error("the End-crystal + primed-TNT row requires distinct "
                     "End-crystal and primed-TNT fixture output paths")
    elif paired_mob_tnt:
        if (args.entity_fixture_out
                or not args.primed_tnt_fixture_out
                or not args.mob_fixture_out):
            ap.error("the mob + primed-TNT pair requires distinct "
                     "--mob-fixture-out and --primed-tnt-fixture-out paths")
    elif paired_item_tnt:
        if (args.entity_fixture_out
                or not args.primed_tnt_fixture_out
                or not args.item_fixture_out):
            ap.error("the item + primed-TNT pair requires distinct "
                     "--item-fixture-out and --primed-tnt-fixture-out paths")
    elif paired_boat_tnt:
        if (args.entity_fixture_out
                or not args.primed_tnt_fixture_out
                or not args.boat_fixture_out):
            ap.error("the boat + primed-TNT pair requires distinct "
                     "--boat-fixture-out and --primed-tnt-fixture-out paths")
    elif paired_arrow_tnt:
        if (not args.entity_fixture_out
                or not args.primed_tnt_fixture_out):
            ap.error("the arrow + primed-TNT pair requires distinct "
                     "--entity-fixture-out and "
                     "--primed-tnt-fixture-out paths")
    elif paired_xp_tnt:
        if (not args.entity_fixture_out
                or not args.primed_tnt_fixture_out):
            ap.error("the XP orb + primed-TNT pair requires distinct "
                     "--entity-fixture-out and "
                     "--primed-tnt-fixture-out paths")
    elif paired_small_fireball_tnt:
        if (not args.entity_fixture_out
                or not args.primed_tnt_fixture_out):
            ap.error("the small fireball + primed-TNT pair requires distinct "
                     "--entity-fixture-out and "
                     "--primed-tnt-fixture-out paths")
    elif entity_count == 1:
        matching_specific = (
            args.primed_tnt_fixture_out if args.primed_tnt_offset else
            args.end_crystal_fixture_out if args.end_crystal_offset else
            args.mob_fixture_out if args.mob_offset else
            args.villager_fixture_out if args.villager_offset else
            args.item_fixture_out if args.item_offset else
            args.boat_fixture_out if args.boat_offset else None
        )
        if sum(bool(value) for value in (
                args.entity_fixture_out, matching_specific)) != 1:
            ap.error("an entity offset requires exactly one matching fixture "
                     "output path")
    elif (args.entity_fixture_out or args.primed_tnt_fixture_out
            or args.second_primed_tnt_fixture_out
            or args.end_crystal_fixture_out
            or args.mob_fixture_out or args.villager_fixture_out
            or args.item_fixture_out
            or args.boat_fixture_out):
        ap.error("entity fixture outputs require a matching entity offset")
    if args.primed_tnt_fixture_out and not args.primed_tnt_offset:
        ap.error("--primed-tnt-fixture-out requires --primed-tnt-offset")
    if (args.second_primed_tnt_fixture_out
            and not args.second_primed_tnt_offset):
        ap.error("--second-primed-tnt-fixture-out requires "
                 "--second-primed-tnt-offset")
    if args.end_crystal_fixture_out and not args.end_crystal_offset:
        ap.error("--end-crystal-fixture-out requires --end-crystal-offset")
    if (args.end_crystal_beam_target_offset
            and not args.end_crystal_offset):
        ap.error("--end-crystal-beam-target-offset requires "
                 "--end-crystal-offset")
    if args.mob_fixture_out and not args.mob_offset:
        ap.error("--mob-fixture-out requires --mob-offset")
    if args.villager_fixture_out and not args.villager_offset:
        ap.error("--villager-fixture-out requires --villager-offset")
    if args.item_fixture_out and not args.item_offset:
        ap.error("--item-fixture-out requires --item-offset")
    if args.boat_fixture_out and not args.boat_offset:
        ap.error("--boat-fixture-out requires --boat-offset")
    if args.scheduled_tick_offset:
        _dx, _dy, _dz, block, delay, priority = args.scheduled_tick_offset
        if not (1 <= block <= 4095 and 0 <= delay <= 1000000
                and -128 <= priority <= 127):
            ap.error("--scheduled-tick-offset requires BLOCK 1..4095, "
                     "DELAY 0..1000000, PRIORITY -128..127")
    if args.scheduled_tick_seed is not None \
            and not args.scheduled_tick_offset:
        ap.error("--scheduled-tick-seed requires --scheduled-tick-offset")
    if (args.post_comparator_set_block_offset
            and not args.comparator_output_offset):
        ap.error("--post-comparator-set-block-offset requires "
                 "--comparator-output-offset")
    if args.post_comparator_set_block_offset:
        _dx, _dy, _dz, block, meta = args.post_comparator_set_block_offset
        if not (0 <= block <= 4095 and 0 <= meta <= 15):
            ap.error("--post-comparator-set-block-offset requires "
                     "BLOCK 0..4095 and META 0..15")
    if any(not 1 <= block <= 4095
           for block in args.scheduled_capture_block):
        ap.error("--scheduled-capture-block requires BLOCK 1..4095")
    if args.random_tick_offset:
        _dx, _dy, _dz, block, _seed = args.random_tick_offset
        if not 1 <= block <= 4095:
            ap.error("--random-tick-offset requires BLOCK 1..4095")
    if args.random_selection_offset:
        _dx, _dy, _dz, block, _seed = args.random_selection_offset
        if not 1 <= block <= 4095:
            ap.error("--random-selection-offset requires BLOCK 1..4095")
    if args.random_tick_offset and args.random_selection_offset:
        ap.error("--random-tick-offset and --random-selection-offset "
                 "are mutually exclusive")
    if args.block_random_seed48 is not None \
            and not 0 <= args.block_random_seed48 < (1 << 48):
        ap.error("--block-random-seed48 must be in 0..2^48-1")
    if args.world_random_seed48 is not None \
            and not 0 <= args.world_random_seed48 < (1 << 48):
        ap.error("--world-random-seed48 must be in 0..2^48-1")
    if bool(args.random_selection_offset) != bool(
            args.random_selection_fixture_out):
        ap.error("--random-selection-offset and "
                 "--random-selection-fixture-out must be supplied together")
    if args.tick0_set_block_offset:
        _dx, _dy, _dz, block, meta = args.tick0_set_block_offset
        if not (0 <= block <= 4095 and 0 <= meta <= 15):
            ap.error("--tick0-set-block-offset requires BLOCK 0..4095 and "
                     "META 0..15")
    if args.tick0_set_block_offset and args.block_edit_sequence:
        ap.error("--tick0-set-block-offset and --block-edit-sequence are "
                 "mutually exclusive")
    if args.tick0_harvest_offset and (
            args.tick0_set_block_offset or args.block_edit_sequence):
        ap.error("--tick0-harvest-offset is mutually exclusive with block edits")

    import qrl_client

    tape = load_tape(args.tape)
    if args.checkpoint_tick is not None \
            and not 0 <= args.checkpoint_tick < len(tape):
        ap.error(
            f"--checkpoint-tick must be in 0..{len(tape) - 1}"
        )
    try:
        block_edit_rows = (
            block_edit_sequence.load(args.block_edit_sequence, len(tape))
            if args.block_edit_sequence else []
        )
    except (OSError, ValueError) as exc:
        ap.error(str(exc))
    scheduled_blocks = {1}
    scheduled_blocks.update(args.scheduled_capture_block)
    if args.scheduled_tick_offset:
        # Capture the explicitly requested block and its same-block
        # descendants. Keeping the default at inert stone prevents unrelated
        # natural pending work from contaminating ordinary matrix cases.
        scheduled_block = args.scheduled_tick_offset[3]
        scheduled_blocks.add(scheduled_block)
        if scheduled_block == 10:
            # Lava-reaction fixtures can contain an independently pending
            # water cell whose earlier state transition is causally relevant.
            scheduled_blocks.add(8)
    if args.random_tick_offset:
        # A callback may schedule itself or descendants. Capture that exact
        # queue alongside the controlled callback instead of silently
        # filtering it from the canonical state.
        scheduled_blocks.add(args.random_tick_offset[3])
    print(f"loaded {len(tape)} ticks from {args.tape}")

    env = qrl_client.NetheriteEnv(host=args.host, port=args.port)
    print(f"reset(seed={args.seed}) ...")
    o = env.reset({"seed": args.seed, "mode": "survival", "type": "default",
                   "fresh": args.fresh})
    if not o.get("ok"):
        print("reset FAILED:", o, file=sys.stderr)
        return 1
    if args.dimension != 0:
        changed = env._cmd({
            "cmd": "dim", "action": {"id": args.dimension},
        })
        if not changed.get("ok"):
            print("dimension transfer FAILED:", changed, file=sys.stderr)
            env.close()
            return 1
        for dimension_ticks in range(1, 201):
            o = env.step({})
            if int(o.get("dim", 999)) == args.dimension:
                print(
                    f"dimension {args.dimension} visible after "
                    f"{dimension_ticks} tick(s)"
                )
                break
        else:
            print(
                f"dimension {args.dimension} did not become visible "
                "within 200 ticks",
                file=sys.stderr,
            )
            env.close()
            return 1
    print(f"spawn ~ ({o.get('x'):.2f},{o.get('y'):.2f},{o.get('z'):.2f})")
    lock = env._cmd({"cmd": "step_lock", "action": {"wait_ms": 1000}})
    if not lock.get("ok") or lock.get("wait_ms") != 1000:
        print("oracle step lock FAILED:", lock, file=sys.stderr)
        env.close()
        return 1
    print("oracle step lock armed (1000 ms bounded host-response window)")
    falling_mode = env._cmd({
        "cmd": "set_falling_instant",
        "action": {"instant": not args.allow_falling_entities},
    })
    if (not falling_mode.get("ok")
            or bool(falling_mode.get("instant"))
                == bool(args.allow_falling_entities)):
        print("oracle falling mode FAILED:", falling_mode, file=sys.stderr)
        env.close()
        return 1
    print(
        "background falling blocks use "
        + ("ordinary entities" if args.allow_falling_entities
           else "the immediate non-entity path")
    )
    game_rules = env._cmd({"cmd": "runcmds", "action": {"cmds": [
        f"gamerule doFireTick {'true' if args.do_fire_tick == 'on' else 'false'}",
        "gamerule doEntityDrops "
        + ("true" if args.do_entity_drops == "on" else "false"),
    ]}})
    # /gamerule reports command result zero even after applying a boolean
    # value, so runcmds marks it in `failed`; `ok` is the transport/execution
    # contract and the captured scheduled context verifies the actual value.
    if not game_rules.get("ok"):
        print("gamerule setup FAILED:", game_rules, file=sys.stderr)
        env.close()
        return 1
    print(f"doFireTick={args.do_fire_tick}")
    print(f"doEntityDrops={args.do_entity_drops}")
    if args.freeze_time:
        raining = args.weather_mode in ("rain", "thunder")
        thundering = args.weather_mode == "thunder"
        fr = env._cmd({"cmd": "runcmds", "action": {"cmds": [
            "gamerule doDaylightCycle false",
            "gamerule doWeatherCycle " + ("true" if raining else "false"),
            "time set 6000",
            ("weather thunder 1000000" if thundering
             else "weather rain 1000000" if raining
             else "weather clear 1000000"),
        ]}})
        if fr.get("failed"):
            print("time/weather freeze FAILED:", fr, file=sys.stderr)
            env.close()
            return 1
        sync_limit = 141 if thundering else 41
        for sync_ticks in range(1, sync_limit):
            synced = env.step({})
            synced_time = synced.get("time") or {}
            if (
                synced_time.get("world_time") == 6000
                and bool(synced_time.get("raining")) == raining
                and bool(synced_time.get("thundering")) == thundering
            ):
                print(f"time/weather freeze visible client-side after {sync_ticks} tick(s)")
                break
        else:
            print(
                "time/weather freeze did not reach the client within "
                f"{sync_limit - 1} ticks",
                file=sys.stderr,
            )
            env.close()
            return 1
    if args.empty_inventory:
        cr = env._cmd({"cmd": "runcmds", "action": {"cmds": ["clear @a"]}})
        # Vanilla /clear returns command count 0 when the inventory was already
        # empty; runcmds labels that "failed" even though the desired state holds.
        if not cr.get("ok"):
            print("inventory clear FAILED:", cr, file=sys.stderr)
            env.close()
            return 1
        print(f"inventory clear ran={cr.get('ran')} zero-result={cr.get('failed')}")
    if args.held_item_fixture:
        item, count_text, meta_text = args.held_item_fixture
        if re.fullmatch(r"[a-z0-9_.-]+:[a-z0-9_/.-]+", item) is None:
            print(
                "held item must be a namespaced registry name "
                "(for example minecraft:bow); numeric command IDs can "
                "silently produce an empty stack in 1.11.2",
                file=sys.stderr,
            )
            env.close()
            return 1
        try:
            count = int(count_text)
            meta = int(meta_text)
        except ValueError:
            print("held item count/meta must be integers", file=sys.stderr)
            env.close()
            return 1
        if count <= 0 or count > 64 or meta < 0 or meta > 32767:
            print("held item count/meta is out of range", file=sys.stderr)
            env.close()
            return 1
        hr = env._cmd({"cmd": "runcmds", "action": {"cmds": [
            f"replaceitem entity @a slot.hotbar.0 {item} {count} {meta}",
        ]}})
        if not hr.get("ok") or hr.get("failed"):
            print("held item fixture FAILED:", hr, file=sys.stderr)
            env.close()
            return 1
        print(f"held item fixture slot=0 item={item} count={count} meta={meta}")
    if args.offhand_item_fixture:
        item, count_text, meta_text = args.offhand_item_fixture
        if re.fullmatch(r"[a-z0-9_.-]+:[a-z0-9_/.-]+", item) is None:
            print("offhand item must be a namespaced registry name",
                  file=sys.stderr)
            env.close()
            return 1
        try:
            count = int(count_text)
            meta = int(meta_text)
        except ValueError:
            print("offhand item count/meta must be integers", file=sys.stderr)
            env.close()
            return 1
        if count <= 0 or count > 64 or meta < 0 or meta > 32767:
            print("offhand item count/meta is out of range", file=sys.stderr)
            env.close()
            return 1
        offhand_result = env._cmd({"cmd": "runcmds", "action": {"cmds": [
            f"replaceitem entity @a slot.weapon.offhand {item} {count} {meta}",
        ]}})
        if not offhand_result.get("ok") or offhand_result.get("failed"):
            print("offhand item fixture FAILED:", offhand_result,
                  file=sys.stderr)
            env.close()
            return 1
        print(
            f"offhand item fixture slot=40 item={item} "
            f"count={count} meta={meta}"
        )
    if args.armor_fixture:
        slot, item, meta, enchantment, level = args.armor_fixture
        slot_name = {
            36: "feet", 37: "legs", 38: "chest", 39: "head",
        }[slot]
        item_name = {
            311: "minecraft:diamond_chestplate",
        }.get(item)
        if item_name is None:
            print(f"armor fixture item {item} has no command name",
                  file=sys.stderr)
            env.close()
            return 1
        nbt = (
            "" if enchantment < 0
            else f" {{ench:[{{id:{enchantment}s,lvl:{level}s}}]}}"
        )
        ar = env._cmd({"cmd": "runcmds", "action": {"cmds": [
            f"replaceitem entity @a slot.armor.{slot_name} "
            f"{item_name} 1 {meta}{nbt}",
        ]}})
        if not ar.get("ok") or ar.get("failed"):
            print("armor fixture FAILED:", ar, file=sys.stderr)
            env.close()
            return 1
        print("armor fixture=" + ":".join(
            str(value) for value in args.armor_fixture))
    if args.clean_entities:
        mr = env._cmd({"cmd": "runcmds", "action": {
            # The C physics/runtime trace does not advance Java's background
            # random block ticks. Pin them off as part of the controlled base
            # scenario or unrelated leaf decay can create item entities midway
            # through an otherwise exact player/mining trace.
            "cmds": [
                "gamerule doMobSpawning false",
                "gamerule randomTickSpeed 0",
            ],
        }})
        if not mr.get("ok"):
            print("doMobSpawning disable FAILED:", mr, file=sys.stderr)
            env.close()
            return 1
        kr = env._cmd({"cmd": "killentities", "action": {}})
        if not kr.get("ok"):
            print("entity cleanup FAILED:", kr, file=sys.stderr)
            env.close()
            return 1
        print(f"disabled natural spawning and removed {kr.get('killed')} "
              "non-player entity/entities")

    # ---- spawn alignment: teleport to the C spawn pose so tick 0 MATCHES ----
    spawn = parse_spawn(args)
    if (args.xp_orb_offset or args.mob_offset
            or args.scheduled_tick_offset
            or args.random_tick_offset
            or args.random_selection_offset) and not spawn:
        ap.error("entity/world fixtures require an aligned "
                 "--spawn/--spawn-file")
    if spawn:
        sx, sy, sz, syaw, spitch = spawn[:5]
        settled_vx, settled_vy, settled_vz, settled_og = (
            spawn[5:9]
            if len(spawn) >= 9
            else (0.0, VANILLA_GROUNDED_VY, 0.0, 1.0)
        )
        settled_fall_distance = spawn[9] if len(spawn) >= 10 else 0.0
        # A final fixture is intentionally absent during alignment. Its C
        # snapshot may already contain fixture-induced motion (for example,
        # -0.02 inside water), so align Java on the dry platform first and
        # restore the exact C tail only after the parked fixture write.
        align_vx, align_vy, align_vz, align_og = (
            (0.0, VANILLA_GROUNDED_VY, 0.0, 1.0)
            if args.final_set_block
            else (settled_vx, settled_vy, settled_vz, settled_og)
        )
        tp = f"tp @a {sx:.6f} {sy:.6f} {sz:.6f} {syaw:.4f} {spitch:.4f}"
        # Optional flat platform so the Java player stands on solid ground at the
        # SAME y as C. The two worldgens produce different terrain at a shared column, so a bare
        # pose tp leaves Java airborne -> free-fall + death, which cascades into every downstream
        # feature. Numeric staging can load the destination chunk directly; do it BEFORE the
        # first teleport so existing Java terrain cannot suffocate the player during setup.
        if args.platform > 0:
            fy = int(round(sy)) - 1
            h = args.platform // 2
            cx, cz = int(round(sx)), int(round(sz))
            platform_blocks = [
                [x, y, z, 1 if y == fy else 0, 0]
                for y in range(fy, fy + args.platform_clear_height + 1)
                for z in range(cz - h, cz + h + 1)
                for x in range(cx - h, cx + h + 1)
            ]
            rr = env._cmd({
                "cmd": "setblocks",
                "action": {"blocks": platform_blocks},
            })
            if not rr.get("ok") or rr.get("set") != len(platform_blocks):
                print("platform setblocks FAILED:", rr, file=sys.stderr)
                env.close()
                return 1

            # A command can report success before the intended setup is useful
            # to the player (unloaded-chunk / wrong-dimension mistakes have
            # occurred here). Read back the support cell from the authoritative
            # server world before admitting this run to the oracle.
            probe_path = os.path.abspath(args.out) + ".platform_probe.bin"
            os.makedirs(os.path.dirname(probe_path), exist_ok=True)
            pr = env._cmd({"cmd": "getblocks", "action": {
                "x0": cx, "y0": fy, "z0": cz,
                "x1": cx, "y1": fy + 3, "z1": cz,
                "file": probe_path,
            }})
            try:
                with open(probe_path, "rb") as probe_file:
                    probe = probe_file.read()
            finally:
                if os.path.exists(probe_path):
                    os.remove(probe_path)
            if not pr.get("ok") or probe != b"\x10\x00" + b"\x00\x00" * 3:
                print(f"platform read-back FAILED: response={pr} raw={probe!r}",
                      file=sys.stderr)
                env.close()
                return 1
            print(f"staged and verified {len(platform_blocks)} platform block(s) "
                  f"at server tick {rr.get('num_ticks')}")
            # Replacing terrain and clearing headroom can notify natural liquid
            # at the pad boundary. Those updates belong to harness setup, not
            # tape tick 0; depending on alignment latency, seed 1 otherwise
            # begins with hundreds of flowing-water blocks that settle during
            # the test. Forty controlled ticks covers the 30-tick Overworld
            # lava cadence plus two water generations before any explicit
            # fixture is staged.
            for _setup_tick in range(40):
                o = env.step({})
            print("drained 40 post-platform setup ticks before fixture staging")
        else:
            # Raw-terrain mode still needs the destination chunk client-side
            # before fixture staging and the final aligned teleport.
            env._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
            env.step({})
        if args.clean_entities:
            # Chunk loading and setup writes can leave delayed natural
            # BlockFalling/liquid callbacks outside the compared capsule.
            # A piston block event runs after those callbacks, so an entity or
            # item created there would consume the process-global entity-ID
            # cursor and RNG before the controlled drop. Require a full
            # 40-server-tick quiet window (past the 30-tick lava cadence),
            # removing any setup-only entities that appear, before staging a
            # fixture. The exact boundary is captured only after this drain.
            quiet_start_tick = int(kr.get("num_ticks", 0))
            last_busy_tick = quiet_start_tick
            quiet_now_tick = quiet_start_tick
            quiet_killed = 0
            for _quiet_probe in range(1000):
                env.step({})
                qr = env._cmd({"cmd": "killentities", "action": {}})
                if not qr.get("ok"):
                    print("background quiescence FAILED:", qr,
                          file=sys.stderr)
                    env.close()
                    return 1
                quiet_now_tick = int(
                    qr.get("num_ticks", quiet_now_tick + 1)
                )
                killed_now = int(qr.get("killed", 0))
                quiet_killed += killed_now
                if killed_now:
                    last_busy_tick = quiet_now_tick
                if quiet_now_tick - last_busy_tick >= 40:
                    break
            else:
                print(
                    "background setup did not reach a 40-tick "
                    "entity-free window",
                    file=sys.stderr,
                )
                env.close()
                return 1
            print(
                "background setup quiescent for 40 server ticks "
                f"after removing {quiet_killed} delayed entity/entities "
                f"across {quiet_now_tick - quiet_start_tick} tick(s)"
            )
        if args.set_block:
            sr = env._cmd({"cmd": "setblocks", "action": {"blocks": args.set_block}})
            if not sr.get("ok") or sr.get("set") != len(args.set_block):
                print("fixture setblocks FAILED:", sr, file=sys.stderr)
                env.close()
                return 1
            print(f"staged {sr.get('set')} identical fixture block(s) "
                  f"at server tick {sr.get('num_ticks')}")
            for _fixture_tick in range(args.fixture_drain_ticks):
                o = env.step({})
            if args.fixture_drain_ticks:
                print(
                    f"drained {args.fixture_drain_ticks} controlled early-"
                    "fixture setup tick(s)"
                )
        def align_spawn():
            # Settle past the server position-correction frame. These ticks are
            # not counted in the tape. On a platform, do not accept merely
            # on_ground: teleport correction can report on_ground with vy=0
            # even though the next vanilla tick restores the grounded tail.
            env._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
            aligned = None
            for ticks in range(1, 41):
                aligned = env.step({})
                pose_ok = (
                    abs(float(aligned.get("x")) - sx) <= 2e-5
                    and abs(float(aligned.get("y")) - sy) <= 2e-5
                    and abs(float(aligned.get("z")) - sz) <= 2e-5
                )
                if args.platform > 0:
                    motion_ok = (
                        bool(aligned.get("on_ground")) == bool(align_og)
                        and abs(float(aligned.get("vx")) - align_vx) <= 1e-12
                        and abs(float(aligned.get("vy")) - align_vy) <= 1e-12
                        and abs(float(aligned.get("vz")) - align_vz) <= 1e-12
                    )
                    if pose_ok and motion_ok:
                        return aligned, ticks
                elif ticks >= 2 and pose_ok:
                    return aligned, ticks
            raise RuntimeError(
                "spawn alignment did not reach an uncontaminated vanilla state "
                f"within 40 ticks: pose=({aligned.get('x')},{aligned.get('y')},"
                f"{aligned.get('z')}) motion=({aligned.get('vx')},"
                f"{aligned.get('vy')},{aligned.get('vz')}) "
                f"on_ground={aligned.get('on_ground')}"
            )

        try:
            o2, settle_ticks = align_spawn()
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            env.close()
            return 1
        print(f"aligned spawn -> now at ({o2.get('x'):.2f},{o2.get('y'):.2f},{o2.get('z'):.2f}) "
              f"yaw={o2.get('yaw'):.1f} pitch={o2.get('pitch'):.1f} "
              f"motion=({o2.get('vx'):.9g},{o2.get('vy'):.9g},{o2.get('vz'):.9g}) "
              f"on_ground={o2.get('on_ground')} settled={settle_ticks} "
              f"deaths_baseline={o2.get('deaths')}")
    else:
        print("NO spawn alignment (raw world spawn) -- tick 0 will diverge on pose; "
              "pass --spawn-file trace/out/c_spawn.txt")

    def dump_blocks(path, phase, locked=False):
        x0, y0, z0, x1, y1, z1 = args.blocks_box
        block_path = os.path.abspath(path)
        os.makedirs(os.path.dirname(block_path), exist_ok=True)
        br = env._cmd({"cmd": "getblocks_locked" if locked else "getblocks", "action": {
            "x0": x0, "y0": y0, "z0": z0,
            "x1": x1, "y1": y1, "z1": z1,
            "file": block_path,
        }})
        if not br.get("ok"):
            raise RuntimeError(f"{phase} getblocks failed: {br}")
        print(f"wrote {br.get('nx')}x{br.get('ny')}x{br.get('nz')} {phase} "
              f"block states at server tick {br.get('num_ticks')} -> {block_path}")

    def dump_light(path, phase, command, label):
        x0, y0, z0, x1, y1, z1 = args.blocks_box
        light_path = os.path.abspath(path)
        os.makedirs(os.path.dirname(light_path), exist_ok=True)
        result = env._cmd({"cmd": command, "action": {
            "x0": x0, "y0": y0, "z0": z0,
            "x1": x1, "y1": y1, "z1": z1,
            "file": light_path,
        }})
        if not result.get("ok"):
            raise RuntimeError(f"{phase} {command} failed: {result}")
        print(
            f"wrote {result.get('nx')}x{result.get('ny')}x"
            f"{result.get('nz')} {phase} {label} cells "
            f"(gate completed={result.get('gate_completed')}) -> {light_path}"
        )

    if (args.blocks_before_out
            and not args.late_set_block
            and not args.final_set_block):
        try:
            dump_blocks(args.blocks_before_out, "pre-tick")
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            env.close()
            return 1
        if spawn:
            # getblocks completes asynchronously on the server thread. Re-anchor
            # after it so the last observation immediately precedes tape tick 0;
            # otherwise one client physics tick can occur while the host waits
            # for the block file even with the response window armed.
            try:
                o2, settle_ticks = align_spawn()
            except RuntimeError as exc:
                print(exc, file=sys.stderr)
                env.close()
                return 1
            print(f"re-anchored after pre-tick snapshot in {settle_ticks} tick(s): "
                  f"player_ticks_existed={o2.get('player_ticks_existed')}")

    initial_observation = o2 if spawn else o
    server_gate_armed = False

    def release_server_gate():
        nonlocal server_gate_armed
        if not server_gate_armed:
            return
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        if not unlocked.get("ok"):
            print("warning: failed to release authoritative server tick gate:",
                  unlocked, file=sys.stderr)
        server_gate_armed = False

    server_lock = env._cmd({"cmd": "server_step_lock"})
    if not server_lock.get("ok"):
        print("oracle authoritative server lock FAILED:", server_lock,
              file=sys.stderr)
        env.close()
        return 1
    server_gate_armed = True
    initial_observation = dict(initial_observation)
    initial_observation["authoritative"] = server_lock.get("authoritative", {})
    if args.late_set_block:
        staged = env._cmd({
            "cmd": "setblocks_locked",
            "action": {"blocks": args.late_set_block},
        })
        if (not staged.get("ok")
                or staged.get("set") != len(args.late_set_block)):
            print("oracle late fixture staging FAILED:", staged, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        if staged.get("authoritative"):
            initial_observation["authoritative"] = staged["authoritative"]
        print(f"staged {staged.get('set')} fixture block(s) at the parked "
              f"pre-tick boundary (gate completed={staged.get('gate_completed')})")
        # Spawn alignment can leave one or more CPacketPlayer tasks queued for
        # the server. Drain them while the fixture is present, then restore
        # short player counters below. Otherwise launch timing, not game state,
        # decides how many Entity.move contact callbacks run at tape tick 0.
        for _flush_tick in range(2):
            flushed = env.step({})
            if not flushed.get("authoritative"):
                print("oracle late-fixture packet drain FAILED:", flushed,
                      file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            o2 = flushed
            initial_observation = dict(flushed)
        print("drained late-fixture setup packets across 2 controlled server ticks")
        if args.initial_fire is not None and args.normalize_move_packets:
            # ServerTickEvent.START parks before MinecraftServer drains its
            # futureTaskQueue. A movement packet emitted by the last setup
            # client tick can therefore arrive after that tick's server permit
            # and remain invisible until tape tick 0. With fire at the player's
            # feet, that stale processPlayer task adds a second Entity.move
            # contact callback inside one authorized world tick. Reset the
            # client cursor first, then grant two packet-silent server ticks so
            # every prior local-network task drains before the cold fire/counter
            # state is seeded below.
            normalized = env._cmd({
                "cmd": "setplayer_locked",
                "action": {"normalize_move_packets": True},
            })
            if (not normalized.get("ok")
                    or not normalized.get("authoritative")):
                print("oracle fire packet normalization FAILED:", normalized,
                      file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            initial_observation["authoritative"] = normalized["authoritative"]
            for _flush_tick in range(2):
                flushed = env.step({})
                if not flushed.get("authoritative"):
                    print("oracle fire packet quiescence FAILED:", flushed,
                          file=sys.stderr)
                    release_server_gate()
                    env.close()
                    return 1
                o2 = flushed
                initial_observation = dict(flushed)
            print("drained 2 packet-silent fire-fixture ticks before "
                  "seeding counters")
    if args.final_set_block:
        # Falling blocks schedule themselves only two ticks after placement.
        # Drain alignment-era movement packets first, then place this fixture
        # without consuming either scheduled tick before the capsule boundary.
        for _flush_tick in range(2):
            flushed = env.step({})
            if not flushed.get("authoritative"):
                print("oracle final-fixture packet drain FAILED:", flushed,
                      file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            o2 = flushed
            initial_observation = dict(flushed)
        staged = env._cmd({
            "cmd": "setblocks_locked",
            "action": {"blocks": args.final_set_block},
        })
        if (not staged.get("ok")
                or staged.get("set") != len(args.final_set_block)):
            print("oracle final fixture staging FAILED:", staged,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        if staged.get("authoritative"):
            initial_observation["authoritative"] = staged["authoritative"]
        seeded_motion = env._cmd({
            "cmd": "setplayer_locked",
            "action": {
                "vx": settled_vx,
                "vy": settled_vy,
                "vz": settled_vz,
                "on_ground": bool(settled_og),
                "fall_distance": settled_fall_distance,
            },
        })
        if (not seeded_motion.get("ok")
                or not seeded_motion.get("authoritative")):
            print("oracle final fixture motion restore FAILED:",
                  seeded_motion, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = \
            seeded_motion["authoritative"]
        print("drained 2 packet ticks, then staged "
              f"{staged.get('set')} final fixture block(s) at the parked "
              f"boundary and restored exact motion "
              f"(gate completed={staged.get('gate_completed')})")
    if args.potion_fixture:
        # EntityPlayerMP.clearActivePotions queues remove-effect packets.
        # A reused oracle may therefore deliver an old removal after a new
        # effect added at the same parked boundary, making the first few
        # movement ticks depend on packet scheduling. Clear first, drain both
        # integrated sides while no effect exists, then seed the exact duration
        # below so setup ticks cannot consume it.
        cleared = env._cmd({
            "cmd": "setplayer_locked",
            "action": {"clear_effects": True},
        })
        if not cleared.get("ok") or not cleared.get("authoritative"):
            print("oracle potion clear FAILED:", cleared, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        for _flush_tick in range(2):
            flushed = env.step({})
            if not flushed.get("authoritative"):
                print("oracle potion packet drain FAILED:", flushed,
                      file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            o2 = flushed
            initial_observation = dict(flushed)
        print("drained potion removal packets across 2 controlled server ticks")
    if (args.initial_fire is not None or args.initial_food is not None
            or args.clear_hurt
            or args.potion_fixture
            or args.player_xp_fixture
            or args.player_combat_fixture
            or args.normalize_move_packets):
        player_fixture = {}
        if args.initial_fire is not None:
            player_fixture["fire"] = args.initial_fire
            player_fixture["clear_hurt"] = True
        elif args.clear_hurt:
            player_fixture["clear_hurt"] = True
        if args.initial_food is not None:
            player_fixture["food"] = args.initial_food
        if args.potion_fixture:
            potion_id, amplifier, duration = args.potion_fixture
            player_fixture["effects"] = [{
                "id": potion_id,
                "amplifier": amplifier,
                "duration": duration,
            }]
            if args.second_potion_fixture:
                potion_id, amplifier, duration = args.second_potion_fixture
                player_fixture["effects"].append({
                    "id": potion_id,
                    "amplifier": amplifier,
                    "duration": duration,
                })
        if args.player_xp_fixture:
            level, fraction, total = args.player_xp_fixture
            player_fixture.update(
                xp_level=level, xp_frac=fraction, xp_total=total)
        if args.player_combat_fixture:
            attack_ticks, hurt_time, hurt_resistant, death_time = \
                args.player_combat_fixture
            player_fixture.update(
                attack_ticks=attack_ticks,
                hurt_time=hurt_time,
                hurt_resistant_time=hurt_resistant,
                death_time=death_time,
            )
        if args.normalize_move_packets:
            player_fixture["normalize_move_packets"] = True
        seeded = env._cmd({
            "cmd": "setplayer_locked",
            "action": player_fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print("oracle locked player fixture FAILED:", seeded, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        details = []
        if args.initial_fire is not None:
            details.append(f"Entity.fire={args.initial_fire}")
        if args.initial_food is not None:
            details.append(f"food={args.initial_food}")
        if args.initial_fire is not None or args.clear_hurt:
            details.append("damage immunity cleared")
        if args.normalize_move_packets:
            details.append("movement-packet cursor=0")
        if args.potion_fixture:
            potion_id, amplifier, duration = args.potion_fixture
            details.append(
                f"potion={potion_id}:{amplifier}:{duration}")
            if args.second_potion_fixture:
                potion_id, amplifier, duration = args.second_potion_fixture
                details.append(
                    f"potion2={potion_id}:{amplifier}:{duration}")
        if args.player_xp_fixture:
            level, fraction, total = args.player_xp_fixture
            details.append(f"xp={level}:{fraction}:{total}")
        if args.player_combat_fixture:
            details.append(
                "combat=" + ":".join(
                    str(value) for value in args.player_combat_fixture))
        print(f"seeded authoritative player fixture ({', '.join(details)}) "
              "at the parked pre-tick boundary")
    if args.comparator_output_offset:
        dx, dy, dz, output_signal = args.comparator_output_offset
        if not 0 <= output_signal <= 15:
            print(
                "comparator output fixture must be in 0..15",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "output_signal": output_signal,
        }
        seeded = env._cmd({
            "cmd": "set_comparator_output_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle comparator output fixture FAILED:",
                seeded,
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked comparator output "
            f"{output_signal} at ({fixture['x']},{fixture['y']},"
            f"{fixture['z']})"
        )
        if args.post_comparator_set_block_offset:
            bdx, bdy, bdz, block, meta = (
                args.post_comparator_set_block_offset
            )
            block_fixture = [[
                math.floor(sx) + bdx,
                math.floor(sy) + bdy,
                math.floor(sz) + bdz,
                block,
                meta,
            ]]
            staged = env._cmd({
                "cmd": "setblocks_locked",
                "action": {"blocks": block_fixture},
            })
            if not staged.get("ok") or staged.get("set") != 1:
                print("oracle post-comparator block fixture FAILED:",
                      staged, file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            initial_observation["authoritative"] = staged["authoritative"]
            print(
                "staged post-comparator block fixture "
                f"{block}:{meta} at ({block_fixture[0][0]},"
                f"{block_fixture[0][1]},{block_fixture[0][2]})"
            )
    if args.command_success_offset:
        dx, dy, dz, success_count = args.command_success_offset
        if not 0 <= success_count <= 15:
            print(
                "command success fixture must be in 0..15",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "success_count": success_count,
        }
        seeded = env._cmd({
            "cmd": "set_command_success_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle command success fixture FAILED:",
                seeded,
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked inert command success "
            f"{success_count} at ({fixture['x']},{fixture['y']},"
            f"{fixture['z']})"
        )
    if args.item_frame_offset:
        dx, dy, dz, facing, item, meta, rotation = (
            args.item_frame_offset
        )
        if not (
            2 <= facing <= 5
            and (
                (item == 0 and meta == 0 and rotation == 0)
                or (
                    item == 1 and meta == 0
                    and 0 <= rotation <= 7
                )
            )
        ):
            print(
                "item-frame fixture requires horizontal FACING 2..5 and "
                "either empty 0:0 rotation 0 or plain stone 1:0 rotation "
                "0..7",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "facing": facing,
            "item": item,
            "meta": meta,
            "rotation": rotation,
        }
        seeded = env._cmd({
            "cmd": "spawn_item_frame_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle item-frame fixture FAILED:",
                seeded,
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "spawned locked item-frame source "
            f"eid={seeded.get('eid')} at ({fixture['x']},"
            f"{fixture['y']},{fixture['z']}) facing={facing} "
            f"item={item}:{meta} rotation={rotation}"
        )
    if args.container_slot_offset:
        dx, dy, dz, slot, item, count, meta = args.container_slot_offset
        if not (
            0 <= slot < 27
            and 0 <= item <= 4095
            and 0 <= count <= 64
            and 0 <= meta <= 32767
            and ((item == 0) == (count == 0))
        ):
            print(
                "container fixture requires SLOT 0..26, ITEM 0..4095, "
                "COUNT 0..64, META 0..32767, with ITEM/COUNT both zero "
                "or both nonzero",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "slot": slot,
            "item": item,
            "count": count,
            "meta": meta,
        }
        seeded = env._cmd({
            "cmd": "set_container_slot_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle container slot fixture FAILED:",
                seeded,
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked container slot "
            f"{slot}={item}x{count}:{meta} at "
            f"({fixture['x']},{fixture['y']},{fixture['z']})"
        )
    if args.container_fill_offset:
        dx, dy, dz, slots, item, count, meta = \
            args.container_fill_offset
        if not (
            1 <= slots <= 27
            and 1 <= item <= 4095
            and 1 <= count <= 64
            and 0 <= meta <= 32767
        ):
            print(
                "container fill fixture requires SLOTS 1..27, "
                "ITEM 1..4095, COUNT 1..64, META 0..32767",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        for slot in range(slots):
            fixture = {
                "x": math.floor(sx) + dx,
                "y": math.floor(sy) + dy,
                "z": math.floor(sz) + dz,
                "slot": slot,
                "item": item,
                "count": count,
                "meta": meta,
            }
            seeded = env._cmd({
                "cmd": "set_container_slot_locked",
                "action": fixture,
            })
            if not seeded.get("ok") or not seeded.get("authoritative"):
                print(
                    "oracle container fill fixture FAILED:",
                    seeded,
                    file=sys.stderr,
                )
                release_server_gate()
                env.close()
                return 1
            initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked container slots "
            f"0..{slots - 1}={item}x{count}:{meta} at "
            f"({fixture['x']},{fixture['y']},{fixture['z']})"
        )
    if args.shulker_nbt_offset:
        raw_dx, raw_dy, raw_dz, nbt_json_path = args.shulker_nbt_offset
        try:
            dx, dy, dz = int(raw_dx), int(raw_dy), int(raw_dz)
            document = json.loads(
                pathlib.Path(nbt_json_path).read_text(encoding="utf-8"))
            nbt_hex = nbt_codec.encode_hex(document)
        except (OSError, ValueError, json.JSONDecodeError,
                nbt_codec.NbtError) as exc:
            print(f"invalid shulker NBT fixture: {exc}", file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "nbt": nbt_hex,
        }
        seeded = env._cmd({
            "cmd": "set_shulker_nbt_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print("oracle shulker NBT fixture FAILED:", seeded,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked shulker NBT at "
            f"({fixture['x']},{fixture['y']},{fixture['z']})"
        )
    if args.flower_pot_offset:
        dx, dy, dz, item, meta = args.flower_pot_offset
        if not (
            0 <= item <= 4095
            and 0 <= meta <= 32767
            and (item != 0 or meta == 0)
        ):
            print(
                "flower-pot fixture requires ITEM 0..4095 and "
                "META 0..32767; empty item 0 requires metadata 0",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "item": item,
            "meta": meta,
        }
        seeded = env._cmd({
            "cmd": "set_flower_pot_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle flower-pot fixture FAILED:",
                seeded,
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked flower pot "
            f"{item}:{meta} at "
            f"({fixture['x']},{fixture['y']},{fixture['z']})"
        )
    if args.skull_offset:
        dx, dy, dz, skull_type, rotation = args.skull_offset
        if not (0 <= skull_type <= 5 and 0 <= rotation <= 15):
            print(
                "skull fixture requires TYPE 0..5 and ROTATION 0..15",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "type": skull_type,
            "rotation": rotation,
        }
        if args.skull_owner:
            if skull_type != 3:
                print("--skull-owner requires --skull-offset TYPE 3",
                      file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
            owner_name, owner_id, property_name, value, signature = (
                args.skull_owner)
            fixture.update({
                "owner_name": owner_name,
                "owner_id": owner_id,
                "owner_property_name": property_name,
                "owner_property_value": value,
                "owner_property_signature": signature,
            })
        seeded = env._cmd({
            "cmd": "set_skull_locked",
            "action": fixture,
        })
        if not seeded.get("ok") or not seeded.get("authoritative"):
            print(
                "oracle skull fixture FAILED:", seeded, file=sys.stderr
            )
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = seeded["authoritative"]
        print(
            "seeded locked "
            f"{'player-profile' if args.skull_owner else 'ownerless'} skull "
            f"type={skull_type} rotation={rotation} at "
            f"({fixture['x']},{fixture['y']},{fixture['z']})"
        )
    elif args.skull_owner:
        print("--skull-owner requires --skull-offset", file=sys.stderr)
        release_server_gate()
        env.close()
        return 1
    if args.xp_orb_offset:
        dx, dy, dz, raw_value = args.xp_orb_offset
        value = int(raw_value)
        if raw_value != value or value <= 0 or value > 32767:
            print("XP fixture value must be an integer in 1..32767",
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "value": value,
            "age": 0,
            "pickup_delay": 0,
            "color": 0,
            "target_color": -100,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {"type": "xporb", **fixture},
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked XP fixture FAILED:", spawned, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked XP fixture "
            f"eid={fixture['eid']} value={value} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
    if args.villager_offset:
        dx, dy, dz, raw_profession, raw_seed48 = args.villager_offset
        profession = int(raw_profession)
        seed48 = int(raw_seed48)
        fixture = {
            "type": "villager",
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "health": 20.0,
            "yaw": 0.0,
            "pitch": 0.0,
            "no_ai": 1,
            "hurt_time": 0,
            "death_time": 0,
            "hurt_resistant_time": 0,
            "profession": profession,
            "growing_age": 0,
            "career": 0,
            "career_level": 0,
            "living_sound_time": 0,
            "offers_initialized": False,
            "entity_seed48": seed48,
            "entity_have_gaussian": False,
            "entity_gaussian": 0.0,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "villager",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": fixture["vx"],
                "my": fixture["vy"],
                "mz": fixture["vz"],
                "profession": profession,
                "entity_seed48": seed48,
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked villager fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        fixture_path = os.path.abspath(args.villager_fixture_out)
        os.makedirs(os.path.dirname(fixture_path), exist_ok=True)
        with open(fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked unopened NoAI villager fixture "
            f"eid={fixture['eid']} profession={profession} "
            f"seed48={seed48} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},"
            f"{fixture['z']:.3f})"
        )
    def spawn_locked_item_fixture():
        dx, dy, dz, raw_item, raw_count, raw_meta = args.item_offset
        item = int(raw_item)
        count = int(raw_count)
        meta = int(raw_meta)
        if (raw_item != item or raw_count != count or raw_meta != meta
                or not 1 <= item <= 4095
                or not 1 <= count <= 64
                or not 0 <= meta <= 32767):
            print(
                "item fixture requires integral ITEM 1..4095, COUNT 1..64, "
                "and META 0..32767",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return False
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "item": item,
            "count": count,
            "meta": meta,
            "age": 0,
            "pickup_delay": 32767,
            "controlled_stationary": 1,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "item",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": fixture["vx"],
                "my": fixture["vy"],
                "mz": fixture["vz"],
                "item": item,
                "count": count,
                "meta": meta,
                "pickup_delay": fixture["pickup_delay"],
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked item fixture FAILED:", spawned, file=sys.stderr)
            release_server_gate()
            env.close()
            return False
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(
            args.item_fixture_out or args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked stationary item fixture "
            f"eid={fixture['eid']} item={item}:{meta}x{count} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
        return True

    if args.item_offset and not args.item_after_primed_tnt:
        if not spawn_locked_item_fixture():
            return 1
    if args.arrow_offset:
        dx, dy, dz = args.arrow_offset
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "yaw": 0.0,
            "pitch": 0.0,
            "controlled_stationary": 1,
            "fire_ticks": args.arrow_fire_seconds * 20,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "arrow",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": fixture["vx"],
                "my": fixture["vy"],
                "mz": fixture["vz"],
                "fire_seconds": args.arrow_fire_seconds,
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked arrow fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked stationary arrow fixture "
            f"eid={fixture['eid']} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
    def spawn_locked_mob_fixture():
        dx, dy, dz, raw_health = args.mob_offset
        health = float(raw_health)
        if not 0.0 < health <= 10.0:
            print("pig fixture health must be in (0,10]", file=sys.stderr)
            release_server_gate()
            env.close()
            return False
        fixture = {
            "type": "pig",
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "health": health,
            "yaw": 0.0,
            "pitch": 0.0,
            "no_ai": 0 if args.mob_collision else 1,
            "knockback_resistance": 1.0,
            "mob_loot": 0,
            "hurt_time": 0,
            "death_time": 0,
            "hurt_resistant_time": 0,
        }
        summon_action = {"type": "pig", **fixture}
        if args.mob_collision:
            summon_action["taskless_collision"] = True
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": summon_action,
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked pig fixture FAILED:", spawned, file=sys.stderr)
            release_server_gate()
            env.close()
            return False
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(
            args.mob_fixture_out or args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            f"spawned locked {'collision-enabled' if args.mob_collision else 'NoAI'} pig fixture "
            f"eid={fixture['eid']} health={health:g} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
        return True

    if args.mob_offset and not args.mob_after_primed_tnt:
        if not spawn_locked_mob_fixture():
            return 1

    def spawn_locked_primed_tnt_fixture(offset, output_path, label):
        dx, dy, dz, vx, vy, vz, fuse_value = offset
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": vx,
            "vy": vy,
            "vz": vz,
            "fuse": int(fuse_value),
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "primed_tnt",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": fixture["vx"],
                "my": fixture["vy"],
                "mz": fixture["vz"],
                "fuse": fixture["fuse"],
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print(f"oracle locked {label} primed-TNT fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return False
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(output_path)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            f"spawned locked {label} primed-TNT fixture "
            f"eid={fixture['eid']} fuse={fixture['fuse']} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
        return True

    if args.primed_tnt_offset:
        if not spawn_locked_primed_tnt_fixture(
                args.primed_tnt_offset,
                args.primed_tnt_fixture_out or args.entity_fixture_out,
                "first"):
            return 1
    if args.second_primed_tnt_offset:
        if not spawn_locked_primed_tnt_fixture(
                args.second_primed_tnt_offset,
                args.second_primed_tnt_fixture_out,
                "second"):
            return 1
    if args.end_crystal_offset:
        dx, dy, dz = args.end_crystal_offset
        beam_offset = args.end_crystal_beam_target_offset
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "inner_rotation": 0,
            "show_bottom": 1,
            "has_beam": 1 if beam_offset else 0,
            "beam_x": (math.floor(sx) + beam_offset[0]
                       if beam_offset else 0),
            "beam_y": (math.floor(sy) + beam_offset[1]
                       if beam_offset else 0),
            "beam_z": (math.floor(sz) + beam_offset[2]
                       if beam_offset else 0),
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "end_crystal",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "inner_rotation": fixture["inner_rotation"],
                "show_bottom": fixture["show_bottom"],
                "has_beam": fixture["has_beam"],
                "beam_x": fixture["beam_x"],
                "beam_y": fixture["beam_y"],
                "beam_z": fixture["beam_z"],
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked End-crystal fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(
            args.end_crystal_fixture_out or args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked End-crystal fixture "
            f"eid={fixture['eid']} rotation=0 bottom=1 "
            f"beam={fixture['has_beam']} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
    if args.mob_offset and args.mob_after_primed_tnt:
        if not spawn_locked_mob_fixture():
            return 1
    if args.item_offset and args.item_after_primed_tnt:
        if not spawn_locked_item_fixture():
            return 1
    if args.boat_offset:
        dx, dy, dz = args.boat_offset
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": 0.0,
            "vy": 0.0,
            "vz": 0.0,
            "yaw": 0.0,
            "pitch": 0.0,
            "controlled_stationary": 1,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "boat",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": fixture["vx"],
                "my": fixture["vy"],
                "mz": fixture["vz"],
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked boat fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(
            args.boat_fixture_out or args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked stationary boat fixture "
            f"eid={fixture['eid']} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
    if args.small_fireball_offset:
        dx, dy, dz, vx, vy, vz, ax, ay, az = args.small_fireball_offset
        fixture = {
            "x": sx + dx,
            "y": sy + dy,
            "z": sz + dz,
            "vx": vx,
            "vy": vy,
            "vz": vz,
            "ax": ax,
            "ay": ay,
            "az": az,
        }
        spawned = env._cmd({
            "cmd": "summon_locked",
            "action": {
                "type": "small_fireball",
                "x": fixture["x"],
                "y": fixture["y"],
                "z": fixture["z"],
                "mx": vx,
                "my": vy,
                "mz": vz,
                "ax": ax,
                "ay": ay,
                "az": az,
            },
        })
        if not spawned.get("ok") or not spawned.get("authoritative"):
            print("oracle locked small-fireball fixture FAILED:", spawned,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        fixture["eid"] = int(spawned["eid"])
        initial_observation["authoritative"] = spawned["authoritative"]
        entity_fixture_path = os.path.abspath(args.entity_fixture_out)
        os.makedirs(os.path.dirname(entity_fixture_path), exist_ok=True)
        with open(entity_fixture_path, "w", encoding="utf-8") as fixture_file:
            json.dump(fixture, fixture_file, indent=2, sort_keys=True)
            fixture_file.write("\n")
        print(
            "spawned locked small-fireball fixture "
            f"eid={fixture['eid']} at "
            f"({fixture['x']:.3f},{fixture['y']:.3f},{fixture['z']:.3f})"
        )
    if args.scheduled_tick_offset:
        dx, dy, dz, block, delay, priority = args.scheduled_tick_offset
        scheduled = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "block": block,
            "delay": delay,
            "priority": priority,
        }
        if args.scheduled_tick_seed is not None:
            scheduled["replace"] = True
            scheduled["seed"] = args.scheduled_tick_seed
        seeded = env._cmd({
            "cmd": "schedule_locked",
            "action": scheduled,
        })
        authoritative = seeded.get("authoritative") or {}
        pending = authoritative.get("scheduled_ticks") or []
        expected = [
            row for row in pending
            if row.get("x") == scheduled["x"]
            and row.get("y") == scheduled["y"]
            and row.get("z") == scheduled["z"]
            and row.get("block") == block
        ]
        if (not seeded.get("ok")
                or not authoritative.get("scheduled_ticks_complete")
                or len(expected) != 1):
            print("oracle locked scheduled tick FAILED:", seeded,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = authoritative
        print(
            "seeded locked scheduled tick "
            f"block={block} pos=({scheduled['x']},{scheduled['y']},"
            f"{scheduled['z']}) delay={delay} priority={priority} "
            f"due={expected[0]['time']} order={expected[0]['order']}"
            + (
                f" callback_seed={args.scheduled_tick_seed}"
                if args.scheduled_tick_seed is not None else ""
            )
        )
    if args.random_tick_offset:
        dx, dy, dz, block, public_seed = args.random_tick_offset
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "block": block,
            "seed": public_seed,
        }
        queued = env._cmd({
            "cmd": "random_tick_locked",
            "action": fixture,
        })
        authoritative = queued.get("authoritative") or {}
        internal_seed = authoritative.get("world_rand_seed48")
        if (not queued.get("ok")
                or queued.get("queued") is not True
                or not isinstance(internal_seed, int)
                or not 0 <= internal_seed < (1 << 48)):
            print("oracle locked random tick FAILED:", queued,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = authoritative
        print(
            "queued locked random tick "
            f"block={block} pos=({fixture['x']},{fixture['y']},"
            f"{fixture['z']}) public_seed={public_seed} "
            f"internal_seed48={internal_seed}"
        )
    if args.random_selection_offset:
        dx, dy, dz, block, public_seed = args.random_selection_offset
        fixture = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
            "block": block,
            "seed": public_seed,
        }
        prepared = env._cmd({
            "cmd": "random_selection_locked",
            "action": fixture,
        })
        authoritative = prepared.get("authoritative") or {}
        required = (
            "loaded_chunks", "iterator_chunks", "target_chunk_rank",
            "eligible_sections", "random_blocks", "sanitized_blocks",
            "lcg_advances_before", "selection_lcg_value",
            "target_promoted",
        )
        if (not prepared.get("ok")
                or any(not isinstance(prepared.get(key), int)
                       for key in required)
                or prepared.get("eligible_sections") != 1
                or prepared.get("random_blocks") != 1
                or prepared.get("target_chunk_rank") != 0
                or prepared.get("target_promoted") != 1
                or not isinstance(
                    authoritative.get("world_update_lcg"), int)):
            print("oracle locked random selection FAILED:", prepared,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = authoritative
        sidecar = {
            **fixture,
            **{key: int(prepared[key]) for key in required},
            "update_lcg": int(authoritative["world_update_lcg"]),
            "world_rand_seed48": int(
                authoritative["world_rand_seed48"]),
        }
        sidecar_path = os.path.abspath(
            args.random_selection_fixture_out)
        os.makedirs(os.path.dirname(sidecar_path), exist_ok=True)
        with open(sidecar_path, "w", encoding="utf-8") as sidecar_file:
            json.dump(sidecar, sidecar_file, indent=2, sort_keys=True)
            sidecar_file.write("\n")
        print(
            "prepared natural random selection "
            f"block={block} pos=({fixture['x']},{fixture['y']},"
            f"{fixture['z']}) chunks={prepared['iterator_chunks']} "
            f"sanitized={prepared['sanitized_blocks']} "
            f"pre_lcg_advances={prepared['lcg_advances_before']} "
            f"update_lcg={authoritative['world_update_lcg']}"
        )
    if args.world_random_seed48 is not None:
        seeded = env._cmd({
            "cmd": "set_world_random_seed_locked",
            "action": {"seed48": args.world_random_seed48},
        })
        authoritative = seeded.get("authoritative") or {}
        if (not seeded.get("ok")
                or seeded.get("seed48") != args.world_random_seed48
                or authoritative.get("world_rand_seed48")
                    != args.world_random_seed48):
            print("oracle locked World.rand seed FAILED:", seeded,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = authoritative
        print(
            "seeded parked World.rand internal cursor "
            f"seed48={args.world_random_seed48}"
        )
    if args.block_random_seed48 is not None:
        seeded = env._cmd({
            "cmd": "set_block_random_seed_locked",
            "action": {"seed48": args.block_random_seed48},
        })
        authoritative = seeded.get("authoritative") or {}
        if (not seeded.get("ok")
                or seeded.get("seed48") != args.block_random_seed48
                or authoritative.get("block_rand_seed48")
                    != args.block_random_seed48):
            print("oracle locked Block.RANDOM seed FAILED:", seeded,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = authoritative
        print(
            "seeded parked Block.RANDOM internal cursor "
            f"seed48={args.block_random_seed48}"
        )
    if args.tick0_set_block_offset:
        dx, dy, dz, block, meta = args.tick0_set_block_offset
        block_edit_rows = [
            block_edit_sequence.BlockEdit(0, dx, dy, dz, block, meta)
        ]
    block_edits = {
        edit.tick: {
            "x": math.floor(sx) + edit.dx,
            "y": math.floor(sy) + edit.dy,
            "z": math.floor(sz) + edit.dz,
            "block": edit.block,
            "meta": edit.meta,
        }
        for edit in block_edit_rows
    }
    harvest_edit = None
    if args.tick0_harvest_offset:
        dx, dy, dz = args.tick0_harvest_offset
        harvest_edit = {
            "x": math.floor(sx) + dx,
            "y": math.floor(sy) + dy,
            "z": math.floor(sz) + dz,
        }

    def queue_block_edit(tick):
        fixture = block_edits[tick]
        queued = env._cmd({
            "cmd": "setblock_tick_locked",
            "action": fixture,
        })
        if not queued.get("ok") or queued.get("queued") is not True:
            print(f"oracle tape-tick-{tick} block mutation FAILED:", queued,
                  file=sys.stderr)
            raise RuntimeError("could not queue locked block mutation")
        print(
            f"queued tape-tick-{tick} server block mutation "
            f"block={fixture['block']}:{fixture['meta']} "
            f"pos=({fixture['x']},{fixture['y']},"
            f"{fixture['z']})"
        )

    if 0 in block_edits:
        try:
            queue_block_edit(0)
        except RuntimeError:
            release_server_gate()
            env.close()
            return 1
    if harvest_edit is not None:
        queued = env._cmd({
            "cmd": "harvestblock_tick_locked",
            "action": harvest_edit,
        })
        if (not queued.get("ok") or queued.get("queued") is not True
                or queued.get("harvest") is not True):
            print("oracle tape-tick-0 harvest FAILED:", queued,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        print(
            "queued tape-tick-0 server harvest "
            f"pos=({harvest_edit['x']},{harvest_edit['y']},"
            f"{harvest_edit['z']})"
        )
    if (args.blocks_before_out
            and (args.late_set_block or args.final_set_block)):
        try:
            dump_blocks(args.blocks_before_out, "pre-tick", locked=True)
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
    if args.block_light_before_out:
        try:
            dump_light(
                args.block_light_before_out, "pre-tick",
                "getblocklight_locked", "block-light")
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
    if args.sky_light_before_out:
        try:
            dump_light(
                args.sky_light_before_out, "pre-tick",
                "getskylight_locked", "sky-light")
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
    if not args.allow_falling_entities:
        falling_mode = env._cmd({
            "cmd": "set_falling_instant_locked",
            "action": {"instant": True},
        })
        if (not falling_mode.get("ok")
                or falling_mode.get("instant") is not True
                or not falling_mode.get("authoritative")):
            print("oracle locked falling mode FAILED:", falling_mode,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = \
            falling_mode["authoritative"]
        print("reasserted immediate falling-block mode at the parked boundary")
    if args.arrow_fire_seconds > 0:
        armed = env._cmd({
            "cmd": "arm_tnt_arrow_collision_cursors_locked",
            "action": {},
        })
        if (not armed.get("ok") or armed.get("armed") is not True
                or not armed.get("authoritative")):
            print("oracle TNT arrow-collision cursor arm FAILED:", armed,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = armed["authoritative"]
        print("armed saved cursors for burning-arrow TNT contact")
    if args.primed_tnt_offset:
        armed = env._cmd({
            "cmd": "arm_tnt_detonation_cursors_locked",
            "action": {},
        })
        if (not armed.get("ok") or armed.get("armed") is not True
                or not armed.get("authoritative")):
            print("oracle TNT detonation cursor arm FAILED:", armed,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = armed["authoritative"]
        print("armed saved World.rand cursor for primed-TNT detonation")
    if any(row[8] for row in tape):
        armed = env._cmd({
            "cmd": "arm_player_use_cursors_locked",
            "action": {},
        })
        if (not armed.get("ok") or armed.get("armed") is not True
                or not armed.get("authoritative")):
            print("oracle physical-use cursor arm FAILED:", armed,
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        initial_observation["authoritative"] = armed["authoritative"]
        print("armed saved cursors for the first physical server right-click")
    print("oracle authoritative server lock armed at "
          f"player_ticks_existed="
          f"{initial_observation['authoritative'].get('player_ticks_existed')}")

    pre_tick_state = canonicalize(
        -1, initial_observation, args.blocks_box, scheduled_blocks
    )
    if args.held_item_fixture:
        _item, count_text, meta_text = args.held_item_fixture
        expected_count = int(count_text)
        expected_meta = int(meta_text)
        held = pre_tick_state["player"]
        inventory_slot = next(
            (stack for stack in pre_tick_state["inventory"]
             if stack.get("slot") == 0),
            None,
        )
        if (
            held.get("held_slot") != 0
            or int(held.get("held_id") or 0) <= 0
            or held.get("held_count") != expected_count
            or held.get("held_meta") != expected_meta
            or inventory_slot is None
            or inventory_slot.get("id") != held.get("held_id")
            or inventory_slot.get("count") != expected_count
            or inventory_slot.get("meta") != expected_meta
        ):
            print(
                "held item fixture read-back FAILED at the canonical "
                f"pre-tick boundary: player={held!r} slot0={inventory_slot!r}",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        print(
            "held item fixture read-back exact "
            f"id={held['held_id']} count={expected_count} meta={expected_meta}"
        )
    if args.offhand_item_fixture:
        _item, count_text, meta_text = args.offhand_item_fixture
        expected_count = int(count_text)
        expected_meta = int(meta_text)
        offhand = next(
            (stack for stack in pre_tick_state["inventory"]
             if stack.get("slot") == 40),
            None,
        )
        if (
            offhand is None
            or int(offhand.get("id") or 0) <= 0
            or offhand.get("count") != expected_count
            or offhand.get("meta") != expected_meta
        ):
            print(
                "offhand item fixture read-back FAILED at the canonical "
                f"pre-tick boundary: slot40={offhand!r}",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        print(
            "offhand item fixture read-back exact "
            f"id={offhand['id']} count={expected_count} meta={expected_meta}"
        )
    if args.armor_fixture:
        slot, item_id, meta, enchantment, level = args.armor_fixture
        expected_enchantments = (
            [] if enchantment < 0 else [[enchantment, level]]
        )
        armor = next(
            (stack for stack in pre_tick_state["inventory"]
             if stack.get("slot") == slot),
            None,
        )
        if (
            armor is None
            or armor.get("id") != item_id
            or armor.get("count") != 1
            or armor.get("meta") != meta
            or armor.get("enchants") != expected_enchantments
        ):
            print(
                "armor fixture read-back FAILED at the canonical pre-tick "
                f"boundary: slot{slot}={armor!r}",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        print(
            f"armor fixture read-back exact slot={slot} id={item_id} "
            f"meta={meta} enchants={expected_enchantments!r}"
        )

    if args.state_before_out:
        state_before_path = os.path.abspath(args.state_before_out)
        os.makedirs(os.path.dirname(state_before_path), exist_ok=True)
        with open(state_before_path, "w", encoding="utf-8") as before_file:
            json.dump(
                pre_tick_state,
                before_file,
                indent=2,
                sort_keys=True,
            )
            before_file.write("\n")
        print(f"wrote canonical pre-tick state -> {state_before_path}")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    cols = ["tick", "x", "y", "z", "yaw", "pitch", "vx", "vy", "vz",
            "on_ground", "health", "food", "air", "frame_hash"]
    csv_f = open(args.out, "w")
    st_f = open(args.state, "w")
    csv_f.write(",".join(cols) + "\n")
    previous_player_tick = (
        int(o2["player_ticks_existed"])
        if spawn and o2 is not None and "player_ticks_existed" in o2
        else None
    )
    authoritative0 = initial_observation.get("authoritative") or {}
    previous_server_tick = authoritative0.get("player_ticks_existed")
    if previous_server_tick is None:
        csv_f.close()
        st_f.close()
        print(
            "oracle observation is missing authoritative.player_ticks_existed; "
            "rebuild/restart the qrl bridge with server-state support",
            file=sys.stderr,
        )
        release_server_gate()
        env.close()
        return 1
    previous_server_tick = int(previous_server_tick)
    for t, v in enumerate(tape):
        if t and t in block_edits:
            try:
                queue_block_edit(t)
            except RuntimeError:
                csv_f.close()
                st_f.close()
                release_server_gate()
                env.close()
                return 1
        ob = env.step(action_dict(v))
        player_tick = ob.get("player_ticks_existed")
        if player_tick is None:
            csv_f.close()
            st_f.close()
            print("oracle observation is missing player_ticks_existed; "
                  "the qrl bridge must be rebuilt with exact-step support",
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        if (previous_player_tick is not None
                and int(player_tick) != previous_player_tick + 1):
            csv_f.close()
            st_f.close()
            print(f"oracle lockstep FAILED at tape tick {t}: player_ticks_existed "
                  f"advanced {previous_player_tick}->{player_tick}, expected exactly +1",
                  file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
        previous_player_tick = int(player_tick)
        authoritative = ob.get("authoritative") or {}
        server_tick = authoritative.get("player_ticks_existed")
        if server_tick is None or int(server_tick) != previous_server_tick + 1:
            csv_f.close()
            st_f.close()
            print(
                f"oracle server lockstep FAILED at tape tick {t}: "
                f"authoritative player_ticks_existed advanced "
                f"{previous_server_tick}->{server_tick}, expected exactly +1",
                file=sys.stderr,
            )
            release_server_gate()
            env.close()
            return 1
        previous_server_tick = int(server_tick)
        csv_f.write(",".join(str(c) for c in phys_row(t, ob)) + "\n")
        canonical = canonicalize(
            t, ob, args.blocks_box, scheduled_blocks)
        st_f.write(json.dumps(canonical, separators=(",", ":")) + "\n")
        if t == args.checkpoint_tick:
            checkpoint_state_path = os.path.abspath(
                args.checkpoint_state_out)
            os.makedirs(
                os.path.dirname(checkpoint_state_path), exist_ok=True)
            with open(
                    checkpoint_state_path, "w",
                    encoding="utf-8") as checkpoint_file:
                json.dump(
                    canonical, checkpoint_file, indent=2, sort_keys=True)
                checkpoint_file.write("\n")
            try:
                dump_blocks(
                    args.checkpoint_blocks_out,
                    f"checkpoint-{t}", locked=True)
                if args.checkpoint_block_light_out:
                    dump_light(
                        args.checkpoint_block_light_out,
                        f"checkpoint-{t}",
                        "getblocklight_locked", "block-light")
            except RuntimeError as exc:
                csv_f.close()
                st_f.close()
                print(exc, file=sys.stderr)
                release_server_gate()
                env.close()
                return 1
    csv_f.close()
    st_f.close()

    if args.blocks_out:
        try:
            dump_blocks(args.blocks_out, "post-tick", locked=True)
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
    if args.block_light_out:
        try:
            dump_light(
                args.block_light_out, "post-tick",
                "getblocklight_locked", "block-light")
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1
    if args.sky_light_out:
        try:
            dump_light(
                args.sky_light_out, "post-tick",
                "getskylight_locked", "sky-light")
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            release_server_gate()
            env.close()
            return 1

    release_server_gate()
    unlock = env._cmd({"cmd": "step_lock", "action": {"wait_ms": 20}})
    if not unlock.get("ok"):
        print("warning: failed to restore ordinary bridge step wait:", unlock,
              file=sys.stderr)
    env.close()
    print(f"wrote {len(tape)} rows -> {args.out}  (+ state {args.state})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
