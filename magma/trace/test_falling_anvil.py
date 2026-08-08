#!/usr/bin/env python3
"""Bit-compare the scheduled Minecraft 1.11.2 anvil path to magma.

The fixture covers scheduling, the entity-local impact RNG, ordinary landing,
damage-tier progression, terminal breakage, and one controlled fresh-player
impact family including plain chest and helmet armor, plus the exact anvil
landing/break world-event payload. It deliberately uses
normal placement, neighbor notification, and
scheduler ticks rather than a direct falling-entity spawn.
"""
import argparse
import json
import math
import pathlib
import struct
import subprocess
import time

from test_dragon_crystal_notification import request

MATH_SEED48 = 0x123456789ABC
WORLD_SEED48 = 0x23456789ABCD
NEXT_ENTITY_ID = 520000
ENTITY_SEED_NO_DAMAGE = 0x23456789ABCD
ENTITY_SEED_DAMAGE = 0
PIG_ENTITY_SEED48 = 0x3456789ABCDE
PIG_ENTITY_SEED48_2 = 0x456789ABCDEF
COW_ENTITY_SEED48 = 0x56789ABCDEF0
PLAYER_ENTITY_SEED48 = 0x6789ABCDEF01
SHEEP_ENTITY_SEED48 = 0x789ABCDEF012
CHICKEN_ENTITY_SEED48 = 0x89ABCDEF0123
CHICKEN_LOOT_ENTITY_SEED48 = 0x23456789ABCD
CHICKEN_LOOT_ZERO_SEED48 = 0x2
CHICKEN_LOOT_ONE_SEED48 = 0x3
CHICKEN_XP_MODE = "damage_chicken_loot_cooked_xp"
CHICKEN_XP_EXPIRED_MODE = "damage_chicken_loot_cooked_xp_expired"
PIG_XP_MODE = "damage_pig_loot_xp"
COW_XP_MODE = "damage_cow_loot_xp"
COW_LOOT_STACKS = [(334, 1), (363, 1)]
SHEEP_XP_MODE = "damage_sheep_loot_xp"
SHEEP_RED_XP_MODE = "damage_sheep_red_loot_xp"
SHEEP_SHEARED_XP_MODE = "damage_sheep_sheared_loot_xp"
SHEEP_LOOT_SPECS = {
    SHEEP_XP_MODE: (0, False, [(35, 1, 0), (423, 2, 0)], 8, 224),
    SHEEP_RED_XP_MODE: (14, False, [(35, 1, 14), (423, 2, 0)], 8, 224),
    SHEEP_SHEARED_XP_MODE: (14, True, [(423, 2, 0)], 6, 222),
}
SHEEP_XP_MODES = tuple(SHEEP_LOOT_SPECS)
CHICKEN_LOOT_SPECS = {
    "damage_chicken_loot": (
        CHICKEN_LOOT_ENTITY_SEED48, [(288, 2), (365, 1)], -1),
    "damage_chicken_loot_zero": (
        CHICKEN_LOOT_ZERO_SEED48, [(365, 1)], -1),
    "damage_chicken_loot_one": (
        CHICKEN_LOOT_ONE_SEED48, [(288, 1), (365, 1)], -1),
    "damage_chicken_loot_cooked": (
        CHICKEN_LOOT_ENTITY_SEED48, [(288, 2), (366, 1)], 100),
    CHICKEN_XP_MODE: (
        CHICKEN_LOOT_ENTITY_SEED48, [(288, 2), (366, 1)], 100),
    CHICKEN_XP_EXPIRED_MODE: (
        CHICKEN_LOOT_ENTITY_SEED48, [(288, 2), (366, 1)], 100),
}
CHICKEN_LOOT_MODES = tuple(CHICKEN_LOOT_SPECS)
CHICKEN_MODES = ("damage_chicken",) + CHICKEN_LOOT_MODES
CHICKEN_LIFECYCLE_MODES = (
    "damage_chicken_loot_cooked", CHICKEN_XP_MODE,
    CHICKEN_XP_EXPIRED_MODE)
MOB_LIFECYCLE_MODES = CHICKEN_LIFECYCLE_MODES + (
    PIG_XP_MODE, COW_XP_MODE, *SHEEP_XP_MODES)
PASSIVE_EVENT_SPECS = {
    "damage_pig": [(1, "pig", PIG_ENTITY_SEED48, False, 1.0)],
    PIG_XP_MODE: [(1, "pig", PIG_ENTITY_SEED48, True, 1.0)],
    "damage_pigs": [
        (1, "pig", PIG_ENTITY_SEED48, False, 1.0),
        (2, "pig", PIG_ENTITY_SEED48_2, False, 1.0),
    ],
    "damage_cow": [(1, "cow", COW_ENTITY_SEED48, False, 0.4)],
    COW_XP_MODE: [(1, "cow", COW_ENTITY_SEED48, True, 0.4)],
    "damage_sheep": [(1, "sheep", SHEEP_ENTITY_SEED48, False, 1.0)],
}
PASSIVE_EVENT_SPECS.update({
    mode: [(1, "sheep", SHEEP_ENTITY_SEED48, True, 1.0)]
    for mode in SHEEP_XP_MODES
})
PASSIVE_EVENT_SPECS.update({
    mode: [(1, "chicken",
            CHICKEN_LOOT_SPECS[mode][0]
            if mode in CHICKEN_LOOT_MODES else CHICKEN_ENTITY_SEED48,
            True, 1.0)]
    for mode in CHICKEN_MODES
})
HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent


def close(label, java, magma, tolerance=1e-15):
    if not math.isclose(float(java), float(magma), rel_tol=0.0,
                        abs_tol=tolerance):
        raise AssertionError(f"{label}: java={java!r} magma={magma!r}")


def blocks(result):
    ox, oz, by = (int(result[k]) for k in ("origin_x", "origin_z", "base_y"))
    return sorted((int(x) - ox, int(y) - by, int(z) - oz, int(b), int(m))
                  for x, y, z, b, m in result["final_blocks"])


def schedule(result, field="scheduled"):
    ox, oz, by = (int(result[k]) for k in ("origin_x", "origin_z", "base_y"))
    return sorted((int(x) - ox, int(y) - by, int(z) - oz, int(b), int(delay),
                   int(priority), int(rank))
                  for x, y, z, b, delay, priority, rank in result[field])


def c_result(mode, meta, origin_x, origin_z, entity_seed):
    raw = subprocess.check_output(
        [str(MAGMA / "game" / "test_falling_anvil_oracle"), mode, str(meta),
         str(origin_x), str(origin_z), str(entity_seed)], text=True)
    return json.loads(raw)


def advanced_seed(seed):
    return (seed * 0x5DEECE66D + 0xB) & ((1 << 48) - 1)


def advanced_n(seed, steps):
    for _ in range(steps):
        seed = advanced_seed(seed)
    return seed


def java_next_double(seed):
    seed = advanced_seed(seed)
    high = seed >> 22
    seed = advanced_seed(seed)
    low = seed >> 21
    return seed, ((high << 27) + low) / float(1 << 53)


def float_bits(value):
    return struct.pack("!f", f32(value)).hex()


def double_bits(value):
    return struct.pack("!d", float(value)).hex()


def expected_terminal_xp_orb(result, mode=CHICKEN_XP_MODE):
    one_item = mode == PIG_XP_MODE or mode == SHEEP_SHEARED_XP_MODE
    cursor = advanced_n(MATH_SEED48, 16 if one_item else 24)
    values = []
    for _ in range(4):
        cursor, value = java_next_double(cursor)
        values.append(value)
    yaw = f32(values[0] * 360.0)
    motion_x = f32(f32(
        values[1] * 0.20000000298023224 - 0.10000000149011612)
        * f32(2.0))
    motion_y = f32(f32(values[2] * 0.2) * f32(2.0))
    motion_z = f32(f32(
        values[3] * 0.20000000298023224 - 0.10000000149011612)
        * f32(2.0))
    # The source is deliberately outside the fixture player's 160-block XP
    # tracking radius.  The first orb tick therefore has gravity/move/drag but
    # no attraction, and no integrated-client mirror can consume Math.random.
    tick_motion_y = float(motion_y) - 0.029999999329447746
    drag = 0.9800000190734863
    return {
        "eid": NEXT_ENTITY_ID + (3 if one_item else 4),
        "dimension": 0,
        "value": 3,
        "health": 5,
        "age": 1,
        "pickup_delay": 0,
        "color": 1,
        "target_color": 0,
        "dead": False,
        "yaw_bits": float_bits(yaw),
        "payload_bits": [
            double_bits(result["origin_x"] + .5 + float(motion_x)),
            double_bits(result["base_y"] - 3.0 + tick_motion_y),
            double_bits(result["origin_z"] + .5 + float(motion_z)),
            double_bits(float(motion_x) * drag),
            double_bits(tick_motion_y * drag),
            double_bits(float(motion_z) * drag),
        ],
    }


def validate_world_events(mode, meta, entity_seed, result):
    events = result["world_events"]
    if mode in ("supported", "instant", "drop", "capacity"):
        expected = []
    else:
        next_seed = advanced_seed(entity_seed)
        roll = (next_seed >> 24) / float(1 << 24)
        broken = ((meta & 15) >> 2) == 2 and roll < 0.15
        expected = [{
            "seq": 0,
            "id": 1029 if broken else 1031,
            "x": int(result["origin_x"]),
            "y": int(result["base_y"]) - 3,
            "z": int(result["origin_z"]),
            "data": 0,
        }]
    if events != expected:
        raise AssertionError(
            f"anvil world events: {events!r} != {expected!r}")


def validate_terminal_particles(mode, result):
    batches = result["terminal_particles"]
    if mode not in MOB_LIFECYCLE_MODES:
        if batches:
            raise AssertionError("non-terminal fixture emitted particles")
        return
    if len(batches) != 1:
        raise AssertionError(
            f"terminal particle batch count: {len(batches)} != 1")
    batch = batches[0]
    for field, expected in (
            ("seq", 0), ("eid", NEXT_ENTITY_ID + 1),
            ("dimension", 0), ("particle_id", 0),
            ("ignore_range", True), ("parameters", [])):
        if batch[field] != expected:
            raise AssertionError(
                f"terminal particle {field}: "
                f"{batch[field]!r} != {expected!r}")
    particles = batch["particles"]
    if len(particles) != 20:
        raise AssertionError(
            f"terminal particle payload count: {len(particles)} != 20")
    for index, payload in enumerate(particles):
        if len(payload) != 6 or any(
                not isinstance(bits, str) or len(bits) != 16
                or any(ch not in "0123456789abcdef" for ch in bits)
                for bits in payload):
            raise AssertionError(
                f"terminal particle {index} bit payload: {payload!r}")


def f32(value):
    return struct.unpack("!f", struct.pack("!f", float(value)))[0]


def chicken_sound_pitch(seed48):
    seed48 = advanced_n(seed48, 2)
    seed48 = advanced_seed(seed48)
    a = f32((seed48 >> 24) / float(1 << 24))
    seed48 = advanced_seed(seed48)
    b = f32((seed48 >> 24) / float(1 << 24))
    delta = f32(a - b)
    return f32(f32(1.0) + f32(delta * f32(0.2)))


def validate_mob_events(mode, result):
    specs = PASSIVE_EVENT_SPECS.get(mode, [])
    events = result["mob_events"]
    expected_count = sum(3 if lethal else 2
                         for _, _, _, lethal, _ in specs)
    if len(events) != expected_count:
        raise AssertionError(
            f"passive event count: {len(events)} != {expected_count}")
    cursor = 0
    for eid_offset, mob, seed, lethal, volume in specs:
        eid = NEXT_ENTITY_ID + eid_offset
        expected_status = {"kind": "status", "eid": eid, "status": 2}
        if events[cursor] != expected_status:
            raise AssertionError(
                f"passive event {cursor}: {events[cursor]!r} "
                f"!= {expected_status!r}")
        cursor += 1
        sound = events[cursor]
        for field, expected in (
                ("kind", "sound"), ("eid", eid),
                ("sound", f"minecraft:entity.{mob}."
                          f"{'death' if lethal else 'hurt'}"),
                ("category", "neutral")):
            if sound[field] != expected:
                raise AssertionError(
                    f"passive sound {field}: "
                    f"{sound[field]!r} != {expected!r}")
        for field, expected in (
                ("x", result["origin_x"] + .5),
                ("y", result["base_y"] - 3.0),
                ("z", result["origin_z"] + .5)):
            close(f"passive sound {field}", sound[field], expected)
        for field, expected in (
                ("volume", volume),
                ("pitch", chicken_sound_pitch(seed))):
            if struct.pack("!f", float(sound[field])) != struct.pack(
                    "!f", expected):
                raise AssertionError(
                    f"passive sound {field}: "
                    f"{sound[field]!r} != {expected!r}")
        cursor += 1
        if lethal:
            expected_status = {"kind": "status", "eid": eid, "status": 3}
            if events[cursor] != expected_status:
                raise AssertionError(
                    f"passive event {cursor}: {events[cursor]!r} "
                    f"!= {expected_status!r}")
            cursor += 1


def common(mode, meta, entity_seed, result):
    if result["mode"] != mode or int(result["meta"]) != meta:
        raise AssertionError("fixture selector changed")
    expected_math = (advanced_n(
                         MATH_SEED48,
                         8 + 8 * len(CHICKEN_LOOT_SPECS[mode][1]))
                     if mode in CHICKEN_LOOT_MODES else
                     advanced_n(MATH_SEED48, 24)
                     if mode == COW_XP_MODE or (
                         mode in SHEEP_XP_MODES
                         and len(SHEEP_LOOT_SPECS[mode][2]) == 2) else
                     advanced_n(MATH_SEED48, 16)
                     if mode in ("damage_pigs", PIG_XP_MODE,
                                 SHEEP_SHEARED_XP_MODE) else
                     advanced_n(MATH_SEED48, 8)
                     if mode in ("drop", "damage_pig", "damage_cow",
                                 "damage_sheep", "damage_chicken") else
                     advanced_n(MATH_SEED48, 2)
                     if mode in ("damage", "damage_absorption",
                                 "damage_resistance",
                                 "damage_armor_chest",
                                 "damage_armor_helmet") else
                     MATH_SEED48)
    if mode in (CHICKEN_XP_MODE, PIG_XP_MODE, COW_XP_MODE,
                *SHEEP_XP_MODES):
        expected_math = advanced_n(expected_math, 8)
    expected_world = (advanced_seed(WORLD_SEED48)
                      if mode in (CHICKEN_XP_MODE, PIG_XP_MODE, COW_XP_MODE,
                                  *SHEEP_XP_MODES)
                      else WORLD_SEED48)
    if result["math_seed48"] != expected_math \
            or result["world_seed48"] != expected_world:
        raise AssertionError(
            "anvil fixture advanced an RNG cursor: "
            f"math={result['math_seed48']} expected={expected_math}, "
            f"world={result['world_seed48']} expected={expected_world}")
    expected = [(0, 0, 0, 145, 2, 0, 0)]
    if schedule(result, "on_added_scheduled") != expected:
        raise AssertionError("anvil placement did not schedule +2")
    after = schedule(result, "after_support_loss_scheduled")
    if after != ([] if mode == "supported" else expected):
        raise AssertionError("anvil neighbor callback duplicated or postponed +2")
    expected_seed = (advanced_seed(entity_seed)
                     if mode in ("fall", "drop", "damage", "damage_reject",
                                 "damage_delta", "damage_absorption",
                                 "damage_resistance", "damage_armor_chest",
                                 "damage_armor_helmet",
                                 "damage_pig",
                                 PIG_XP_MODE,
                                 "damage_pigs", "damage_cow", COW_XP_MODE,
                                 "damage_sheep", *SHEEP_XP_MODES,
                                 "damage_chicken",
                                 *CHICKEN_LOOT_MODES)
                     else entity_seed)
    if result["entity_seed48"] != expected_seed:
        raise AssertionError("anvil entity Random cursor")
    validate_world_events(mode, meta, entity_seed, result)
    validate_terminal_particles(mode, result)


def validate_java(mode, meta, entity_seed, landed_meta, drop_meta, result):
    common(mode, meta, entity_seed, result)
    damage_mode = mode in ("damage", "damage_reject", "damage_delta",
                           "damage_absorption", "damage_resistance",
                           "damage_armor_chest", "damage_armor_helmet")
    if not damage_mode and result["player_damage"] is not None:
        raise AssertionError("non-damage anvil changed the fixture player")
    if mode not in ("damage_pig", PIG_XP_MODE,
                    "damage_cow", COW_XP_MODE, "damage_sheep",
                    *SHEEP_XP_MODES,
                    *CHICKEN_MODES) \
            and result["mob_damage"] is not None:
        raise AssertionError("non-single-mob anvil exposed mob damage")
    if mode != "damage_pigs" and result["mob_damages"]:
        raise AssertionError("non-pair anvil exposed mob damage array")
    if mode not in (PIG_XP_MODE, COW_XP_MODE, *SHEEP_XP_MODES, "damage_pigs",
                    "damage_cow", "damage_sheep",
                    *CHICKEN_MODES) \
            and result["impact_order"]:
        raise AssertionError("unexpected anvil target order")
    if mode not in MOB_LIFECYCLE_MODES and result["mob_post_rows"]:
        raise AssertionError("unexpected controlled-mob post-death rows")
    validate_mob_events(mode, result)
    if mode == "supported":
        if result["rows"] or result["fixture_entities"]:
            raise AssertionError("supported anvil spawned an entity")
        if result["constructor_item"] is not None or result["ticked_item"] is not None:
            raise AssertionError("supported anvil created an item")
        if (result["source_block"], result["source_meta"]) != (145, meta):
            raise AssertionError("supported anvil changed state")
        if result["next_entity_id"] != NEXT_ENTITY_ID or schedule(result):
            raise AssertionError("supported anvil consumed state")
        return
    if mode == "instant":
        if result["rows"] or result["fixture_entities"]:
            raise AssertionError("instant anvil spawned an entity")
        if result["constructor_item"] is not None or result["ticked_item"] is not None:
            raise AssertionError("instant anvil created an item")
        if (result["source_block"], result["source_meta"]) != (0, 0):
            raise AssertionError("instant anvil did not remove its source")
        if result["next_entity_id"] != NEXT_ENTITY_ID:
            raise AssertionError("instant anvil consumed entity ID")
        landed = [x for x in blocks(result) if x[3] == 145]
        if landed != [(0, -3, 0, 145, meta)]:
            raise AssertionError(f"instant anvil landing/meta: {landed!r}")
        if schedule(result) != [(0, -3, 0, 145, 2, 0, 0)]:
            raise AssertionError("instant anvil did not schedule landed +2")
        return
    rows = result["rows"]
    expected_rows = 14 if mode == "drop" else 13
    if len(rows) != expected_rows:
        raise AssertionError(
            f"expected {expected_rows} anvil rows, got {len(rows)}")
    for n, row in enumerate(rows, 1):
        if row[0] != n or row[1] != n:
            raise AssertionError(f"fallTime row {n}: {row!r}")
    if (result["source_block"], result["source_meta"]) != (0, 0):
        raise AssertionError("falling anvil did not remove its source")
    if result["next_entity_id"] != NEXT_ENTITY_ID + (
            3 if mode == "damage_pigs" else
            4 if mode == PIG_XP_MODE else
            3 + len(SHEEP_LOOT_SPECS[mode][2])
                if mode in SHEEP_XP_MODES else
            5 if mode == COW_XP_MODE else
            2 + len(CHICKEN_LOOT_SPECS[mode][1])
                + (1 if mode == CHICKEN_XP_MODE else 0)
            if mode in CHICKEN_LOOT_MODES else
            2 if mode in ("drop", "damage_pig", "damage_cow",
                          "damage_sheep", "damage_chicken") else 1):
        raise AssertionError("anvil entity-id cursor")
    expected_entity = [[NEXT_ENTITY_ID, True, result["origin_x"] + .5,
                        result["base_y"] - (3.5 if mode == "drop" else 3.0),
                        result["origin_z"] + .5]]
    if result["fixture_entities"] != expected_entity:
        raise AssertionError("retired anvil entity state")
    if not rows[-1][2] or not rows[-1][9] or not rows[-1][11]:
        raise AssertionError("anvil landing lifecycle")
    landed = [x for x in blocks(result) if x[3] == 145]
    expected_landed = ([] if landed_meta is None
                       else [(0, -3, 0, 145, landed_meta)])
    if landed != expected_landed:
        raise AssertionError(f"anvil landing/meta: {landed!r}")
    expected_scheduled = ([] if landed_meta is None
                          else [(0, -3, 0, 145, 2, 0, 0)])
    if schedule(result) != expected_scheduled:
        raise AssertionError("landed anvil did not schedule +2")
    for row in rows[:-1]:
        if row[13] != entity_seed:
            raise AssertionError("anvil Random advanced before impact")
    if rows[-1][13] != advanced_seed(entity_seed):
        raise AssertionError("anvil impact did not consume one nextFloat")
    if damage_mode:
        damage = result["player_damage"]
        if damage is None:
            raise AssertionError("damage anvil did not expose player state")
        health = {"damage": 16.0, "damage_reject": 20.0,
                  "damage_delta": 18.0,
                  "damage_absorption": 20.0,
                  "damage_resistance": 16.8,
                  "damage_armor_chest": 17.02400016784668,
                  "damage_armor_helmet": 17.215999603271484}[mode]
        exhaustion = (0.0 if mode in ("damage_reject",
                                      "damage_absorption") else 0.1)
        last_damage = 3.0 if mode == "damage_armor_helmet" else 4.0
        for field, expected in (
                ("health", health), ("absorption", 0.0),
                ("hurt_resistant_time", 20), ("hurt_time", 10),
                ("max_hurt_time", 10), ("last_damage", last_damage),
                ("food_exhaustion", exhaustion)):
            if isinstance(expected, int):
                if damage[field] != expected:
                    raise AssertionError(
                        f"player {field}: {damage[field]!r} != {expected!r}")
            elif struct.pack("!f", float(damage[field])) != struct.pack(
                    "!f", expected):
                raise AssertionError(
                    f"player {field}: {damage[field]!r} != {expected!r}")
        expected_player_seed = (advanced_n(PLAYER_ENTITY_SEED48, 4)
                                if mode in ("damage", "damage_absorption",
                                            "damage_resistance",
                                            "damage_armor_chest")
                                else PLAYER_ENTITY_SEED48)
        if mode == "damage_armor_helmet":
            expected_player_seed = advanced_n(PLAYER_ENTITY_SEED48, 5)
        if damage["entity_seed48"] != expected_player_seed:
            raise AssertionError(
                "player Entity.rand cursor: "
                f"{damage['entity_seed48']!r} != {expected_player_seed!r}")
        expected_armor = (
            [{"slot": 38, "id": 311, "count": 1, "meta": 1}]
            if mode == "damage_armor_chest" else
            [{"slot": 39, "id": 310, "count": 1, "meta": 20}]
            if mode == "damage_armor_helmet" else [])
        if damage["armor"] != expected_armor:
            raise AssertionError(
                f"player armor: {damage['armor']!r} != {expected_armor!r}")
    if mode == "damage_pig":
        # Immediate falling.onUpdate() impact boundary: the pig itself is not
        # ticked, so hurt timers are the attack result, not one tick older.
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError("pig damage anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "pig"),
                ("health", 6.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0),
                # setBeenAttacked consumes one nextDouble before the pig's
                # hurt-sound pitch consumes two nextFloat calls.
                ("entity_seed48", advanced_n(PIG_ENTITY_SEED48, 4))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"pig {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"pig {field}: {damage[field]!r} != {expected!r}")
    if mode == "damage_pigs":
        expected_order = [NEXT_ENTITY_ID + 1, NEXT_ENTITY_ID + 2]
        if result["impact_order"] != expected_order:
            raise AssertionError(
                f"pig target order: {result['impact_order']!r}")
        damages = result["mob_damages"]
        if len(damages) != 2:
            raise AssertionError("two-pig anvil did not expose two targets")
        for index, (damage, seed) in enumerate(zip(
                damages, (PIG_ENTITY_SEED48, PIG_ENTITY_SEED48_2)), 1):
            for field, expected in (
                    ("eid", NEXT_ENTITY_ID + index), ("type", "pig"),
                    ("health", 6.0), ("hurt_resistant_time", 20),
                    ("hurt_time", 10), ("max_hurt_time", 10),
                    ("last_damage", 4.0),
                    ("entity_seed48", advanced_n(seed, 4))):
                if isinstance(expected, float):
                    if struct.pack("!f", float(damage[field])) != \
                            struct.pack("!f", expected):
                        raise AssertionError(
                            f"pig {index} {field}: "
                            f"{damage[field]!r} != {expected!r}")
                elif damage[field] != expected:
                    raise AssertionError(
                        f"pig {index} {field}: "
                        f"{damage[field]!r} != {expected!r}")
    if mode == "damage_cow":
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"cow target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError("cow damage anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "cow"),
                ("health", 6.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0),
                ("entity_seed48", advanced_n(COW_ENTITY_SEED48, 4))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"cow {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"cow {field}: {damage[field]!r} != {expected!r}")
    if mode == "damage_sheep":
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"sheep target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError("sheep damage anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "sheep"),
                ("health", 4.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0),
                ("entity_seed48", advanced_n(SHEEP_ENTITY_SEED48, 4))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"sheep {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"sheep {field}: {damage[field]!r} != {expected!r}")
        aabb = damage["aabb"]
        if len(aabb) != 6:
            raise AssertionError("sheep AABB shape")
        close("sheep AABB width", aabb[3] - aabb[0],
              0.8999999761581421)
        close("sheep AABB height", aabb[4] - aabb[1],
              1.2999999523162842)
        close("sheep AABB depth", aabb[5] - aabb[2],
              0.8999999761581421)
    if mode == PIG_XP_MODE:
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"pig target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError("pig loot/XP anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "pig"),
                ("health", 0.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0), ("death_time", 0),
                ("living_dead", True), ("entity_is_dead", False),
                ("drop_entity_count", 1), ("xp_entity_count", 0),
                ("recently_hit", 20), ("attacking_player", True),
                ("fire_ticks", -1), ("burning", False),
                ("entity_seed48", advanced_n(PIG_ENTITY_SEED48, 6))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"pig {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"pig {field}: {damage[field]!r} != {expected!r}")
        aabb = damage["aabb"]
        if len(aabb) != 6:
            raise AssertionError("pig AABB shape")
        close("pig AABB width", aabb[3] - aabb[0],
              0.8999999761581421)
        close("pig AABB height", aabb[4] - aabb[1],
              0.8999999761581421)
        close("pig AABB depth", aabb[5] - aabb[2],
              0.8999999761581421)
        drops = result["mob_drops"]
        if len(drops) != 1:
            raise AssertionError(f"pig loot stack count: {len(drops)}")
        item = drops[0]
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 2), ("item", 319),
                ("count", 3), ("meta", 0), ("age", 0),
                ("pickup_delay", 10), ("health", 5),
                ("lifespan", 6000), ("on_ground", False),
                ("is_dead", False)):
            if item[field] != expected:
                raise AssertionError(
                    f"pig drop {field}: {item[field]!r} != {expected!r}")
        for field, expected in (
                ("x", result["origin_x"] + .5),
                ("y", result["base_y"] - 3.0),
                ("z", result["origin_z"] + .5),
                ("vy", 0.20000000298023224)):
            close(f"pig drop {field}", item[field], expected)
        post_rows = result["mob_post_rows"]
        if len(post_rows) != 20:
            raise AssertionError(
                f"pig post-death row count: {len(post_rows)}")
        for tick, row in enumerate(post_rows, 1):
            xp_spawned = tick == 20
            for field, expected in (
                    ("tick", tick), ("death_time", tick),
                    ("hurt_resistant_time", 20 - tick),
                    ("hurt_time", max(10 - tick, 0)),
                    ("living_dead", True),
                    ("entity_is_dead", xp_spawned),
                    ("loaded", not xp_spawned),
                    ("fire_ticks", -1), ("burning", False),
                    ("entity_seed48", advanced_n(
                        PIG_ENTITY_SEED48, 206 if xp_spawned else 6)),
                    ("recently_hit", 20 - tick),
                    ("attacking_player", True),
                    ("world_seed48", advanced_seed(WORLD_SEED48)
                     if xp_spawned else WORLD_SEED48),
                    ("math_seed48", advanced_n(
                        MATH_SEED48, 24 if xp_spawned else 16)),
                    ("xp_entity_count", 1 if xp_spawned else 0),
                    ("terminal_particle_count", 20 if xp_spawned else 0)):
                if row[field] != expected:
                    raise AssertionError(
                        f"pig post tick {tick} {field}: "
                        f"{row[field]!r} != {expected!r}")
            expected_items = [[NEXT_ENTITY_ID + 2, tick,
                               max(10 - tick, 0), False, True]]
            if row["items"] != expected_items:
                raise AssertionError(
                    f"pig post tick {tick} items: "
                    f"{row['items']!r} != {expected_items!r}")
        expected_xp = [expected_terminal_xp_orb(result, PIG_XP_MODE)]
        if result["xp_orbs"] != expected_xp:
            raise AssertionError(
                f"terminal pig XP orb: {result['xp_orbs']!r} "
                f"!= {expected_xp!r}")
    elif mode == COW_XP_MODE:
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"cow target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError("cow loot/XP anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "cow"),
                ("health", 0.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0), ("death_time", 0),
                ("living_dead", True), ("entity_is_dead", False),
                ("drop_entity_count", 2), ("xp_entity_count", 0),
                ("recently_hit", 20), ("attacking_player", True),
                ("fire_ticks", -1), ("burning", False),
                ("entity_seed48", advanced_n(COW_ENTITY_SEED48, 8))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"cow {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"cow {field}: {damage[field]!r} != {expected!r}")
        aabb = damage["aabb"]
        if len(aabb) != 6:
            raise AssertionError("cow AABB shape")
        close("cow AABB width", aabb[3] - aabb[0],
              0.8999999761581421)
        close("cow AABB height", aabb[4] - aabb[1],
              1.399999976158142)
        close("cow AABB depth", aabb[5] - aabb[2],
              0.8999999761581421)
        drops = result["mob_drops"]
        if len(drops) != len(COW_LOOT_STACKS):
            raise AssertionError(f"cow loot stack count: {len(drops)}")
        for index, (item, expected_stack) in enumerate(zip(
                drops, COW_LOOT_STACKS)):
            for field, expected in (
                    ("eid", NEXT_ENTITY_ID + 2 + index),
                    ("item", expected_stack[0]),
                    ("count", expected_stack[1]), ("meta", 0),
                    ("age", 0), ("pickup_delay", 10),
                    ("health", 5), ("lifespan", 6000),
                    ("on_ground", False), ("is_dead", False)):
                if item[field] != expected:
                    raise AssertionError(
                        f"cow drop {index} {field}: "
                        f"{item[field]!r} != {expected!r}")
            for field, expected in (
                    ("x", result["origin_x"] + .5),
                    ("y", result["base_y"] - 3.0),
                    ("z", result["origin_z"] + .5),
                    ("vy", 0.20000000298023224)):
                close(f"cow drop {index} {field}", item[field], expected)
        post_rows = result["mob_post_rows"]
        if len(post_rows) != 20:
            raise AssertionError(
                f"cow post-death row count: {len(post_rows)}")
        for tick, row in enumerate(post_rows, 1):
            xp_spawned = tick == 20
            for field, expected in (
                    ("tick", tick), ("death_time", tick),
                    ("hurt_resistant_time", 20 - tick),
                    ("hurt_time", max(10 - tick, 0)),
                    ("living_dead", True),
                    ("entity_is_dead", xp_spawned),
                    ("loaded", not xp_spawned),
                    ("fire_ticks", -1), ("burning", False),
                    ("entity_seed48", advanced_n(
                        COW_ENTITY_SEED48, 216 if xp_spawned else 8)),
                    ("recently_hit", 20 - tick),
                    ("attacking_player", True),
                    ("world_seed48", advanced_seed(WORLD_SEED48)
                     if xp_spawned else WORLD_SEED48),
                    ("math_seed48", advanced_n(
                        MATH_SEED48, 32 if xp_spawned else 24)),
                    ("xp_entity_count", 1 if xp_spawned else 0),
                    ("terminal_particle_count", 20 if xp_spawned else 0)):
                if row[field] != expected:
                    raise AssertionError(
                        f"cow post tick {tick} {field}: "
                        f"{row[field]!r} != {expected!r}")
            expected_items = [
                [NEXT_ENTITY_ID + 2 + index, tick,
                 max(10 - tick, 0), False, True]
                for index in range(len(COW_LOOT_STACKS))]
            if row["items"] != expected_items:
                raise AssertionError(
                    f"cow post tick {tick} items: "
                    f"{row['items']!r} != {expected_items!r}")
        expected_xp = [expected_terminal_xp_orb(result, COW_XP_MODE)]
        if result["xp_orbs"] != expected_xp:
            raise AssertionError(
                f"terminal cow XP orb: {result['xp_orbs']!r} "
                f"!= {expected_xp!r}")
    elif mode in SHEEP_XP_MODES:
        fleece, sheared, loot_stacks, impact_steps, terminal_steps = \
            SHEEP_LOOT_SPECS[mode]
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"sheep target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError(
                "sheep loot/XP anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "sheep"),
                ("health", 0.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0), ("death_time", 0),
                ("living_dead", True), ("entity_is_dead", False),
                ("drop_entity_count", len(loot_stacks)),
                ("xp_entity_count", 0),
                ("recently_hit", 20), ("attacking_player", True),
                ("fire_ticks", -1), ("burning", False),
                ("fleece_color", fleece), ("sheared", sheared),
                ("entity_seed48", advanced_n(
                    SHEEP_ENTITY_SEED48, impact_steps))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"sheep {field}: {damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"sheep {field}: {damage[field]!r} != {expected!r}")
        aabb = damage["aabb"]
        if len(aabb) != 6:
            raise AssertionError("sheep AABB shape")
        close("sheep AABB width", aabb[3] - aabb[0],
              0.8999999761581421)
        close("sheep AABB height", aabb[4] - aabb[1],
              1.2999999523162842)
        close("sheep AABB depth", aabb[5] - aabb[2],
              0.8999999761581421)
        drops = result["mob_drops"]
        if len(drops) != len(loot_stacks):
            raise AssertionError(f"sheep loot stack count: {len(drops)}")
        for index, (item, expected_stack) in enumerate(zip(
                drops, loot_stacks)):
            for field, expected in (
                    ("eid", NEXT_ENTITY_ID + 2 + index),
                    ("item", expected_stack[0]),
                    ("count", expected_stack[1]),
                    ("meta", expected_stack[2]),
                    ("age", 0), ("pickup_delay", 10),
                    ("health", 5), ("lifespan", 6000),
                    ("on_ground", False), ("is_dead", False)):
                if item[field] != expected:
                    raise AssertionError(
                        f"sheep drop {index} {field}: "
                        f"{item[field]!r} != {expected!r}")
            for field, expected in (
                    ("x", result["origin_x"] + .5),
                    ("y", result["base_y"] - 3.0),
                    ("z", result["origin_z"] + .5),
                    ("vy", 0.20000000298023224)):
                close(f"sheep drop {index} {field}", item[field], expected)
        post_rows = result["mob_post_rows"]
        if len(post_rows) != 20:
            raise AssertionError(
                f"sheep post-death row count: {len(post_rows)}")
        for tick, row in enumerate(post_rows, 1):
            xp_spawned = tick == 20
            for field, expected in (
                    ("tick", tick), ("death_time", tick),
                    ("hurt_resistant_time", 20 - tick),
                    ("hurt_time", max(10 - tick, 0)),
                    ("living_dead", True),
                    ("entity_is_dead", xp_spawned),
                    ("loaded", not xp_spawned),
                    ("fire_ticks", -1), ("burning", False),
                    ("entity_seed48", advanced_n(
                        SHEEP_ENTITY_SEED48,
                        terminal_steps if xp_spawned else impact_steps)),
                    ("recently_hit", 20 - tick),
                    ("attacking_player", True),
                    ("world_seed48", advanced_seed(WORLD_SEED48)
                     if xp_spawned else WORLD_SEED48),
                    ("math_seed48", advanced_n(
                        MATH_SEED48,
                        16 + 8 * len(loot_stacks)
                        if xp_spawned else 8 + 8 * len(loot_stacks))),
                    ("xp_entity_count", 1 if xp_spawned else 0),
                    ("terminal_particle_count", 20 if xp_spawned else 0)):
                if row[field] != expected:
                    raise AssertionError(
                        f"sheep post tick {tick} {field}: "
                        f"{row[field]!r} != {expected!r}")
            expected_items = [
                [NEXT_ENTITY_ID + 2 + index, tick,
                 max(10 - tick, 0), False, True]
                for index in range(len(loot_stacks))]
            if row["items"] != expected_items:
                raise AssertionError(
                    f"sheep post tick {tick} items: "
                    f"{row['items']!r} != {expected_items!r}")
        expected_xp = [expected_terminal_xp_orb(result, mode)]
        if result["xp_orbs"] != expected_xp:
            raise AssertionError(
                f"terminal sheep XP orb: {result['xp_orbs']!r} "
                f"!= {expected_xp!r}")
    elif mode in CHICKEN_MODES:
        if result["impact_order"] != [NEXT_ENTITY_ID + 1]:
            raise AssertionError(
                f"chicken target order: {result['impact_order']!r}")
        damage = result["mob_damage"]
        if damage is None:
            raise AssertionError(
                "chicken damage anvil did not expose mob state")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("type", "chicken"),
                ("health", 0.0), ("hurt_resistant_time", 20),
                ("hurt_time", 10), ("max_hurt_time", 10),
                ("last_damage", 4.0), ("death_time", 0),
                ("living_dead", True), ("entity_is_dead", False),
                ("drop_entity_count",
                 len(CHICKEN_LOOT_SPECS[mode][1])
                 if mode in CHICKEN_LOOT_MODES else 0),
                ("xp_entity_count", 0),
                ("fire_ticks",
                 CHICKEN_LOOT_SPECS[mode][2]
                 if mode in CHICKEN_LOOT_MODES else -1),
                ("burning",
                 CHICKEN_LOOT_SPECS[mode][2] > 0
                 if mode in CHICKEN_LOOT_MODES else False),
                ("entity_seed48", advanced_n(
                    CHICKEN_LOOT_SPECS[mode][0]
                    if mode in CHICKEN_LOOT_MODES
                    else CHICKEN_ENTITY_SEED48,
                    7 if mode in CHICKEN_LOOT_MODES else 4))):
            if isinstance(expected, float):
                if struct.pack("!f", float(damage[field])) != struct.pack(
                        "!f", expected):
                    raise AssertionError(
                        f"chicken {field}: "
                        f"{damage[field]!r} != {expected!r}")
            elif damage[field] != expected:
                raise AssertionError(
                    f"chicken {field}: "
                    f"{damage[field]!r} != {expected!r}")
        expected_recent = (20 if mode == CHICKEN_XP_MODE else
                           19 if mode == CHICKEN_XP_EXPIRED_MODE else 0)
        if damage["recently_hit"] != expected_recent:
            raise AssertionError(
                f"chicken recently_hit: {damage['recently_hit']!r} "
                f"!= {expected_recent!r}")
        if damage["attacking_player"] != (expected_recent > 0):
            raise AssertionError("chicken attacking-player credit boundary")
        aabb = damage["aabb"]
        if len(aabb) != 6:
            raise AssertionError("chicken AABB shape")
        close("chicken AABB width", aabb[3] - aabb[0],
              0.4000000059604645)
        close("chicken AABB height", aabb[4] - aabb[1],
              0.699999988079071)
        close("chicken AABB depth", aabb[5] - aabb[2],
              0.4000000059604645)
        drops = result["mob_drops"]
        expected_stacks = (CHICKEN_LOOT_SPECS[mode][1]
                           if mode in CHICKEN_LOOT_MODES else [])
        if len(drops) != len(expected_stacks):
            raise AssertionError(
                f"chicken loot stack count: {len(drops)}")
        for index, (item, expected_stack) in enumerate(zip(
                drops, expected_stacks)):
            for field, expected in (
                    ("eid", NEXT_ENTITY_ID + 2 + index),
                    ("item", expected_stack[0]),
                    ("count", expected_stack[1]), ("meta", 0),
                    ("age", 0), ("pickup_delay", 10),
                    ("health", 5), ("lifespan", 6000),
                    ("on_ground", False), ("is_dead", False)):
                if item[field] != expected:
                    raise AssertionError(
                        f"chicken drop {index} {field}: "
                        f"{item[field]!r} != {expected!r}")
            for field, expected in (
                    ("x", result["origin_x"] + .5),
                    ("y", result["base_y"] - 3.0),
                    ("z", result["origin_z"] + .5),
                    ("vy", 0.20000000298023224)):
                close(f"chicken drop {index} {field}",
                      item[field], expected)
        post_rows = result["mob_post_rows"]
        if mode in CHICKEN_LIFECYCLE_MODES:
            if len(post_rows) != 20:
                raise AssertionError(
                    f"chicken post-death row count: {len(post_rows)}")
            for tick, row in enumerate(post_rows, 1):
                expected_seed = advanced_n(
                    CHICKEN_LOOT_ENTITY_SEED48,
                    207 if tick == 20 else 7)
                initial_recent = (20 if mode == CHICKEN_XP_MODE else
                                  19 if mode == CHICKEN_XP_EXPIRED_MODE
                                  else 0)
                expected_recent = max(initial_recent - tick, 0)
                expected_attacker = (
                    mode == CHICKEN_XP_MODE
                    or (mode == CHICKEN_XP_EXPIRED_MODE and tick <= 19))
                xp_spawned = mode == CHICKEN_XP_MODE and tick == 20
                expected_world = (advanced_seed(WORLD_SEED48)
                                  if xp_spawned else WORLD_SEED48)
                expected_math = (advanced_n(MATH_SEED48, 32)
                                 if xp_spawned else
                                 advanced_n(MATH_SEED48, 24))
                for field, expected in (
                        ("tick", tick), ("death_time", tick),
                        ("hurt_resistant_time", 20 - tick),
                        ("hurt_time", max(10 - tick, 0)),
                        ("living_dead", True),
                        ("entity_is_dead", tick == 20),
                        ("loaded", tick < 20),
                        ("fire_ticks", 100 - tick),
                        ("burning", True),
                        ("entity_seed48", expected_seed),
                        ("recently_hit", expected_recent),
                        ("attacking_player", expected_attacker),
                        ("world_seed48", expected_world),
                        ("math_seed48", expected_math),
                        ("xp_entity_count", 1 if xp_spawned else 0),
                        ("terminal_particle_count",
                         20 if tick == 20 else 0)):
                    if row[field] != expected:
                        raise AssertionError(
                            f"chicken post tick {tick} {field}: "
                            f"{row[field]!r} != {expected!r}")
                expected_items = [
                    [NEXT_ENTITY_ID + 2 + index, tick,
                     max(10 - tick, 0), False, True]
                    for index in range(len(expected_stacks))]
                if row["items"] != expected_items:
                    raise AssertionError(
                        f"chicken post tick {tick} items: "
                        f"{row['items']!r} != {expected_items!r}")
        expected_xp = ([expected_terminal_xp_orb(result)]
                       if mode == CHICKEN_XP_MODE else [])
        if result["xp_orbs"] != expected_xp:
            raise AssertionError(
                f"terminal XP orbs: {result['xp_orbs']!r} "
                f"!= {expected_xp!r}")
    elif result["mob_drops"]:
        raise AssertionError("non-loot impact exposed mob loot")
    if mode == "drop":
        item = result["constructor_item"]
        ticked = result["ticked_item"]
        if item is None or ticked is None:
            raise AssertionError("failed-placement anvil did not expose item")
        for field, expected in (
                ("eid", NEXT_ENTITY_ID + 1), ("item", 145), ("count", 1),
                ("meta", drop_meta), ("age", 0), ("pickup_delay", 10)):
            if item[field] != expected:
                raise AssertionError(
                    f"constructor {field}: {item[field]!r} != {expected!r}")
        for field, expected in (
                ("x", result["origin_x"] + .5),
                ("y", result["base_y"] - 3.5),
                ("z", result["origin_z"] + .5),
                ("vx", 0.012723377905786037),
                ("vy", 0.20000000298023224),
                ("vz", 0.06541676074266434)):
            close(f"constructor {field}", item[field], expected, 1e-15)
        if struct.pack("!f", float(item["yaw"])) != struct.pack("!f", 346.55627):
            raise AssertionError(f"constructor yaw: {item['yaw']!r}")
    elif result["constructor_item"] is not None or result["ticked_item"] is not None:
        raise AssertionError("non-drop anvil unexpectedly created an item")


def compare(mode, meta, entity_seed, landed_meta, drop_meta, java, magma):
    validate_java(mode, meta, entity_seed, landed_meta, drop_meta, java)
    common(mode, meta, entity_seed, magma)
    for field in ("mode", "meta", "origin_x", "origin_z", "base_y", "source_block",
                  "source_meta", "math_seed48", "world_seed48",
                  "next_entity_id", "entity_seed48"):
        if java[field] != magma[field]:
            raise AssertionError(f"{field}: java={java[field]!r} magma={magma[field]!r}")
    for field in ("on_added_scheduled", "after_support_loss_scheduled", "scheduled"):
        if schedule(java, field) != schedule(magma, field):
            raise AssertionError(f"{field}: java and magma differ")
    if java["mob_post_rows"] != magma["mob_post_rows"]:
        raise AssertionError("controlled-mob post-death rows differ")
    if java["terminal_particles"] != magma["terminal_particles"]:
        raise AssertionError("terminal particle payload bits differ")
    if java["xp_orbs"] != magma["xp_orbs"]:
        raise AssertionError("terminal XP-orb payload bits differ")
    jevents, cevents = java["mob_events"], magma["mob_events"]
    if len(jevents) != len(cevents):
        raise AssertionError("controlled-mob event count differs")
    for index, (jevent, cevent) in enumerate(zip(jevents, cevents)):
        if jevent["kind"] != cevent["kind"]:
            raise AssertionError(f"mob event {index} kind differs")
        exact_fields = (("eid", "status") if jevent["kind"] == "status"
                        else ("eid", "sound", "category"))
        for field in exact_fields:
            if jevent[field] != cevent[field]:
                raise AssertionError(
                    f"mob event {index} {field}: "
                    f"java={jevent[field]!r} magma={cevent[field]!r}")
        if jevent["kind"] == "sound":
            for field in ("x", "y", "z"):
                close(f"mob event {index} {field}",
                      jevent[field], cevent[field])
            for field in ("volume", "pitch"):
                if struct.pack("!f", float(jevent[field])) != struct.pack(
                        "!f", float(cevent[field])):
                    raise AssertionError(
                        f"mob event {index} {field}: "
                        f"java={jevent[field]!r} magma={cevent[field]!r}")
    if len(java["rows"]) != len(magma["rows"]):
        raise AssertionError("falling row count differs")
    for n, (j, c) in enumerate(zip(java["rows"], magma["rows"]), 1):
        for field in (0, 1, 2, 9, 10, 11, 13):
            if j[field] != c[field]:
                raise AssertionError(f"row {n} field {field}: java={j[field]!r} magma={c[field]!r}")
        close(f"row {n} x", float(j[3]) - float(java["origin_x"]),
              float(c[3]) - float(magma["origin_x"]), 1e-14)
        close(f"row {n} y", j[4], c[4], 1e-13)
        close(f"row {n} z", float(j[5]) - float(java["origin_z"]),
              float(c[5]) - float(magma["origin_z"]), 1e-14)
        for field, name in ((6, "vx"), (7, "vy"), (8, "vz")):
            close(f"row {n} {name}", j[field], c[field])
        if struct.pack("!f", float(j[12])) != struct.pack("!f", float(c[12])):
            raise AssertionError(f"row {n} fallDistance differs")
    if blocks(java) != blocks(magma) or java["fixture_entities"] != magma["fixture_entities"]:
        raise AssertionError("final anvil state differs")
    if mode in ("damage", "damage_reject", "damage_delta",
                "damage_absorption", "damage_resistance",
                "damage_armor_chest", "damage_armor_helmet"):
        if magma["player_damage"] is None:
            raise AssertionError("magma damage anvil did not expose player state")
        for field in ("hurt_resistant_time", "hurt_time", "max_hurt_time"):
            if java["player_damage"][field] != magma["player_damage"][field]:
                raise AssertionError(
                    f"player {field}: java={java['player_damage'][field]!r} "
                    f"magma={magma['player_damage'][field]!r}")
        for field in ("entity_seed48", "armor"):
            if java["player_damage"][field] != magma["player_damage"][field]:
                raise AssertionError(
                    f"player {field}: java={java['player_damage'][field]!r} "
                    f"magma={magma['player_damage'][field]!r}")
        for field in ("health", "absorption", "last_damage", "food_exhaustion"):
            if struct.pack("!f", float(java["player_damage"][field])) != \
                    struct.pack("!f", float(magma["player_damage"][field])):
                raise AssertionError(
                    f"player {field}: java={java['player_damage'][field]!r} "
                    f"magma={magma['player_damage'][field]!r}")
    if mode in ("damage_pig", PIG_XP_MODE, "damage_cow", COW_XP_MODE,
                "damage_sheep", *SHEEP_XP_MODES, *CHICKEN_MODES):
        jmob, cmob = java["mob_damage"], magma["mob_damage"]
        if jmob is None or cmob is None:
            raise AssertionError("single-mob damage state missing")
        for field in ("eid", "type", "hurt_resistant_time", "hurt_time",
                      "max_hurt_time", "death_time", "living_dead",
                      "entity_is_dead", "drop_entity_count",
                      "xp_entity_count", "recently_hit",
                      "attacking_player", "fire_ticks", "burning"):
            if jmob[field] != cmob[field]:
                raise AssertionError(
                    f"mob {field}: java={jmob[field]!r} magma={cmob[field]!r}")
        for field in ("health", "last_damage"):
            if struct.pack("!f", float(jmob[field])) != struct.pack(
                    "!f", float(cmob[field])):
                raise AssertionError(
                    f"mob {field}: java={jmob[field]!r} magma={cmob[field]!r}")
        if jmob["entity_seed48"] != cmob["entity_seed48"]:
            raise AssertionError("mob Entity.rand cursor differs")
        if mode == "damage_sheep" or mode in SHEEP_XP_MODES:
            for field in ("fleece_color", "sheared"):
                if jmob[field] != cmob[field]:
                    raise AssertionError(
                        f"mob {field}: java={jmob[field]!r} "
                        f"magma={cmob[field]!r}")
        if len(jmob["aabb"]) != 6 or len(cmob["aabb"]) != 6:
            raise AssertionError("mob AABB state missing")
        for index, (jvalue, cvalue) in enumerate(zip(
                jmob["aabb"], cmob["aabb"])):
            close(f"mob AABB field {index}", jvalue, cvalue, 1e-14)
        if java.get("mob_damage_post_runtime_tick", False) or magma.get(
                "mob_damage_post_runtime_tick", False):
            raise AssertionError("pig observation boundary marker changed")
        if mode in (PIG_XP_MODE, COW_XP_MODE, *SHEEP_XP_MODES,
                    "damage_cow", "damage_sheep",
                    *CHICKEN_MODES) \
                and java["impact_order"] != \
                magma["impact_order"]:
            raise AssertionError("single passive target order differs")
        jdrops, cdrops = java["mob_drops"], magma["mob_drops"]
        if len(jdrops) != len(cdrops):
            raise AssertionError("controlled mob drop count differs")
        for index, (jitem, citem) in enumerate(zip(jdrops, cdrops)):
            for field in ("eid", "item", "count", "meta", "age",
                          "pickup_delay", "health", "lifespan",
                          "on_ground", "is_dead"):
                if jitem[field] != citem[field]:
                    raise AssertionError(
                        f"mob drop {index} {field}: "
                        f"java={jitem[field]!r} magma={citem[field]!r}")
            for field in ("x", "y", "z", "vx", "vy", "vz"):
                close(f"mob drop {index} {field}",
                      jitem[field], citem[field], 1e-15)
            for field in ("yaw", "hover_start"):
                if struct.pack("!f", float(jitem[field])) != struct.pack(
                        "!f", float(citem[field])):
                    raise AssertionError(
                        f"mob drop {index} {field}: "
                        f"java={jitem[field]!r} magma={citem[field]!r}")
    if mode == "damage_pigs":
        if java["impact_order"] != magma["impact_order"]:
            raise AssertionError("two-pig target order differs")
        if java.get("mob_damage_post_runtime_tick", False) or magma.get(
                "mob_damage_post_runtime_tick", False):
            raise AssertionError("two-pig observation boundary changed")
        if len(java["mob_damages"]) != 2 or len(magma["mob_damages"]) != 2:
            raise AssertionError("two-pig damage state missing")
        for index, (jmob, cmob) in enumerate(zip(
                java["mob_damages"], magma["mob_damages"]), 1):
            for field in ("eid", "type", "hurt_resistant_time", "hurt_time",
                          "max_hurt_time", "entity_seed48"):
                if jmob[field] != cmob[field]:
                    raise AssertionError(
                        f"pig {index} {field}: java={jmob[field]!r} "
                        f"magma={cmob[field]!r}")
            for field in ("health", "last_damage"):
                if struct.pack("!f", float(jmob[field])) != struct.pack(
                        "!f", float(cmob[field])):
                    raise AssertionError(
                        f"pig {index} {field}: java={jmob[field]!r} "
                        f"magma={cmob[field]!r}")
            if len(jmob["aabb"]) != 6 or len(cmob["aabb"]) != 6:
                raise AssertionError(f"pig {index} AABB state missing")
            for field, (jvalue, cvalue) in enumerate(zip(
                    jmob["aabb"], cmob["aabb"])):
                close(f"pig {index} AABB field {field}",
                      jvalue, cvalue, 1e-14)
    if mode != "drop":
        if magma["ticked_item"] is not None:
            raise AssertionError("magma non-drop anvil unexpectedly created an item")
        return
    jitem, citem = java["ticked_item"], magma["ticked_item"]
    if citem is None:
        raise AssertionError("magma failed-placement anvil did not create item")
    for field in ("eid", "item", "count", "meta", "age", "pickup_delay"):
        if jitem[field] != citem[field]:
            raise AssertionError(
                f"ticked item {field}: java={jitem[field]!r} magma={citem[field]!r}")
    for field in ("x", "y", "z", "vx", "vy", "vz"):
        close(f"ticked item {field}", jitem[field], citem[field], 1e-13)
    if struct.pack("!f", float(jitem["yaw"])) != struct.pack(
            "!f", float(citem["yaw"])):
        raise AssertionError(
            f"ticked item yaw: java={jitem['yaw']!r} magma={citem['yaw']!r}")


def validate_capacity(result, meta, entity_seed):
    common("capacity", meta, entity_seed, result)
    if (result["source_block"], result["source_meta"]) != (145, meta):
        raise AssertionError("full falling pool removed anvil source")
    if result["next_entity_id"] != NEXT_ENTITY_ID or result["rows"] or result["fixture_entities"]:
        raise AssertionError("full falling pool advanced fixture state")
    if schedule(result):
        raise AssertionError("capacity callback did not drain")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--native-capacity", action="store_true")
    parser.add_argument("--case", help="run only one named fixture case")
    args = parser.parse_args()
    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs"); break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline: raise
                time.sleep(.5)
        request(args.port, "server_step_lock"); locked = True
        # The client half of the integrated JVM shares Entity's static ID
        # cursor. Let startup or the preceding run's player status response
        # settle before resetting that cursor for the first constructor.
        time.sleep(4.0)
        subprocess.run(["make", "-C", str(MAGMA), "game/test_falling_anvil_oracle"],
                       check=True, stdout=subprocess.DEVNULL)
        updates = 0
        cases = [
            ("supported", 8, ENTITY_SEED_NO_DAMAGE, 8, None),
            ("fall", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("fall", 1, ENTITY_SEED_NO_DAMAGE, 1, None),
            ("fall", 4, ENTITY_SEED_NO_DAMAGE, 4, None),
            ("fall", 8, ENTITY_SEED_NO_DAMAGE, 8, None),
            ("fall", 0, ENTITY_SEED_DAMAGE, 4, None),
            ("fall", 8, ENTITY_SEED_DAMAGE, None, None),
            ("drop", 0, ENTITY_SEED_NO_DAMAGE, None, 0),
            ("drop", 1, ENTITY_SEED_NO_DAMAGE, None, 0),
            ("drop", 4, ENTITY_SEED_NO_DAMAGE, None, 1),
            ("drop", 8, ENTITY_SEED_NO_DAMAGE, None, 2),
            ("instant", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("instant", 1, ENTITY_SEED_NO_DAMAGE, 1, None),
            ("instant", 4, ENTITY_SEED_NO_DAMAGE, 4, None),
            ("instant", 8, ENTITY_SEED_NO_DAMAGE, 8, None),
            # Keep real-player damage last: the integrated client can perform
            # ambient Entity-ID work after that response, racing a following
            # controlled constructor in the same JVM.
            ("damage_reject", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_delta", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_absorption", 4, ENTITY_SEED_NO_DAMAGE, 4, None),
            ("damage_resistance", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_armor_chest", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_armor_helmet", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_pig", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            (PIG_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            ("damage_pigs", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_cow", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            (COW_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            ("damage_sheep", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            (SHEEP_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            (SHEEP_RED_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            (SHEEP_SHEARED_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            ("damage_chicken", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_chicken_loot", 0, ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_chicken_loot_zero", 0,
             ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_chicken_loot_one", 0,
             ENTITY_SEED_NO_DAMAGE, 0, None),
            ("damage_chicken_loot_cooked", 0,
             ENTITY_SEED_NO_DAMAGE, 0, None),
            (CHICKEN_XP_MODE, 8, ENTITY_SEED_DAMAGE, None, None),
            (CHICKEN_XP_EXPIRED_MODE, 8,
             ENTITY_SEED_DAMAGE, None, None),
        ]
        if args.case:
            cases = [case for case in cases if case[0] == args.case]
            if not cases:
                parser.error(f"unknown case: {args.case}")
        for mode, meta, entity_seed, landed_meta, drop_meta in cases:
            if mode.startswith("damage"):
                time.sleep(4.0)
            java = request(args.port, "falling_anvil_locked", {
                "mode": mode, "meta": meta, "math_seed48": MATH_SEED48,
                "world_seed48": WORLD_SEED48,
                "entity_seed48": entity_seed,
                "player_entity_seed48": PLAYER_ENTITY_SEED48,
                "next_entity_id": NEXT_ENTITY_ID,
                "pig_entity_seed48": PIG_ENTITY_SEED48,
                "pig_entity_seed48_2": PIG_ENTITY_SEED48_2,
                "cow_entity_seed48": COW_ENTITY_SEED48,
                "sheep_entity_seed48": SHEEP_ENTITY_SEED48,
                "chicken_entity_seed48":
                    CHICKEN_LOOT_SPECS[mode][0]
                    if mode in CHICKEN_LOOT_MODES
                    else CHICKEN_ENTITY_SEED48})
            try:
                compare(mode, meta, entity_seed, landed_meta, drop_meta, java,
                        c_result(mode, meta, java["origin_x"],
                                 java["origin_z"], entity_seed))
            except AssertionError as exc:
                raise AssertionError(
                    f"{mode} meta={meta} entity_seed48={entity_seed}: {exc}") \
                    from exc
            updates += len(java["rows"]) + len(java["mob_post_rows"])
        if args.native_capacity:
            validate_capacity(c_result("capacity", 8, 26, 8,
                                       ENTITY_SEED_NO_DAMAGE),
                              8, ENTITY_SEED_NO_DAMAGE)
        print(f"PASS java==magma: {updates} anvil falling updates and "
              f"{len(cases)} cases")
    finally:
        if locked: request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
