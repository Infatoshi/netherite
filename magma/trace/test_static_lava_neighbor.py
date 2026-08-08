#!/usr/bin/env python3
"""Compare static-lava neighbor and mixing effects to Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


CENTER = 62
EAST = 63
DOWN = 37
DOWN_TWO = 12
NORTH_DOWN = 32
NORTH_TWO_DOWN = 27
UP = 87
NORTH = 57
NORTH_TWO = 52
WEST = 61
SOUTH = 67
LEVELS = (
    0, 4, 5, 15, 0, 1, 4, 5, 15, 0, 0, 4, 2, 0, 0, 0, 0, 0, 0,
    1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
)
MIX_CASES = frozenset((4, 5, 6, 10, 11, 12))
DYNAMIC_CASES = frozenset((
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
))
HORIZONTAL_REPLACEMENTS = {
    24: (51, 0),
    25: (51, 0),
    26: (9, 0),
    27: (9, 0),
    30: (8, 4),
    31: (8, 4),
    32: (6, 0),
    33: (30, 0),
    34: (50, 5),
    35: (78, 0),
    36: (106, 4),
    37: (171, 0),
    39: (66, 0),
    40: (27, 0),
    41: (55, 0),
    42: (31, 1),
    43: (59, 3),
    44: (32, 0),
    45: (39, 0),
    46: (76, 5),
    48: (37, 0),
    49: (38, 0),
    50: (40, 0),
    51: (104, 4),
    52: (105, 6),
    53: (111, 0),
    54: (115, 2),
    55: (141, 5),
    56: (142, 7),
    57: (207, 2),
}
BLOCKED_MATERIAL_CASES = {38: (65, 3), 47: (83, 0)}
SUPPORT_BLOCKS = {
    32: 3,
    42: 2,
    43: 60,
    44: 12,
    45: 110,
    47: 12,
    48: 2,
    49: 2,
    50: 110,
    51: 60,
    52: 60,
    53: 9,
    54: 88,
    55: 60,
    56: 60,
    57: 60,
}
DOWN_WATER_CASES = frozenset((28, 29))
EFFECT_CASES = MIX_CASES | frozenset(HORIZONTAL_REPLACEMENTS) \
    | DOWN_WATER_CASES


def expected_blocks(fixture, after):
    values = [0] * 125
    if fixture in DYNAMIC_CASES:
        values[25:50] = [16] * 25
        values[CENTER] = 10 * 16 + LEVELS[fixture]
        if fixture in SUPPORT_BLOCKS:
            support = 8 if fixture == 53 and after \
                else SUPPORT_BLOCKS[fixture]
            values[NORTH_DOWN] = support * 16
        if fixture == 47:
            values[NORTH_TWO_DOWN] = 9 * 16
        if fixture in HORIZONTAL_REPLACEMENTS:
            target, target_meta = HORIZONTAL_REPLACEMENTS[fixture]
            values[NORTH] = (10 * 16
                + (1 if fixture >= 32 or fixture % 2 == 0 else 2)) \
                if after else target * 16 + target_meta
            for index in (SOUTH, WEST, EAST):
                values[index] = 16
            if after and target in (8, 9):
                values[CENTER] = 49 * 16
            if fixture == 36:
                values[NORTH_TWO] = 1 * 16
            return values
        if fixture in BLOCKED_MATERIAL_CASES:
            target, target_meta = BLOCKED_MATERIAL_CASES[fixture]
            values[NORTH] = target * 16 + target_meta
            if fixture == 38:
                values[NORTH_TWO] = 1 * 16
            for index in (SOUTH, WEST, EAST):
                values[index] = 1 * 16
            if after:
                values[CENTER] = 11 * 16
            return values
        if fixture in DOWN_WATER_CASES:
            values[DOWN_TWO] = 16
            values[DOWN] = 16 if after else 9 * 16
            return values
        if fixture in (22, 23):
            values[DOWN] = 10 * 16 + 8 if after else 0
            return values
        if fixture in (19, 20):
            values[NORTH] = 10 * 16 + 2
            for index in (SOUTH, WEST, EAST):
                values[index] = 16
            if after:
                values[CENTER] = 10 * 16 + (3 if fixture == 19 else 4)
            return values
        if fixture == 21:
            values[NORTH] = (10 if after else 11) * 16
            values[SOUTH] = (10 if after else 11) * 16
            values[WEST] = values[EAST] = 16
            if after:
                values[CENTER] = 10 * 16 + 1
            return values
        if after:
            level = 1 if fixture in (15, 17) else 2
            directions = (NORTH,) if fixture == 17 \
                else (NORTH, SOUTH, WEST, EAST)
            for index in directions:
                values[index] = 10 * 16 + level
        return values
    values[CENTER] = (10 if after and fixture not in MIX_CASES else 11) \
        * 16 + LEVELS[fixture]
    if after and fixture in MIX_CASES:
        values[CENTER] = 49 * 16 if LEVELS[fixture] == 0 else 4 * 16
    if fixture in (4, 5, 7):
        values[UP] = 8 * 16 if after and fixture in MIX_CASES else 9 * 16
    elif fixture == 8:
        values[UP] = 8 * 16
    elif fixture == 6:
        values[NORTH] = 8 * 16
    elif fixture == 9:
        values[DOWN] = 9 * 16
    elif fixture == 10:
        values[NORTH] = 8 * 16 if after else 9 * 16
    elif fixture == 11:
        values[WEST] = 8 * 16
    elif fixture == 12:
        values[UP] = 8 * 16 if after else 9 * 16
        values[NORTH] = 8 * 16 if after else 9 * 16
    if after:
        values[EAST] = 1 * 16
    return values


def expected_scheduled(fixture):
    if fixture in DYNAMIC_CASES:
        if fixture in HORIZONTAL_REPLACEMENTS:
            delay = 10 if fixture >= 32 or fixture % 2 == 0 else 30
            target, _ = HORIZONTAL_REPLACEMENTS[fixture]
            if fixture == 40:
                return (
                    (0, 0, 0, 10, delay, 0, 0),
                    (0, 0, -1, 10, delay, 0, 1),
                )
            if fixture == 44:
                return (
                    (0, -1, -1, 12, 2, 0, 0),
                    (0, 0, -1, 10, delay, 0, 1),
                    (0, 0, 0, 10, delay, 0, 2),
                )
            if fixture == 53:
                return (
                    (0, -1, -1, 8, 5, 0, 0),
                    (0, 0, -1, 10, delay, 0, 1),
                    (0, 0, 0, 10, delay, 0, 2),
                )
            if target == 9:
                return (
                    (0, 0, -1, 8, 5, 0, 0),
                    (0, 0, -1, 10, delay, 0, 1),
                )
            if target == 8:
                return ((0, 0, -1, 10, delay, 0, 0),)
            return (
                (0, 0, -1, 10, delay, 0, 0),
                (0, 0, 0, 10, delay, 0, 1),
            )
        if fixture in BLOCKED_MATERIAL_CASES:
            return ()
        if fixture in DOWN_WATER_CASES:
            delay = 10 if fixture == 28 else 30
            return ((0, 0, 0, 10, delay, 0, 0),)
        if fixture in (22, 23):
            delay = 10 if fixture == 22 else 30
            return (
                (0, -1, 0, 10, delay, 0, 0),
                (0, 0, 0, 10, delay, 0, 1),
            )
        if fixture in (19, 20):
            delay = 40 if fixture == 19 else 120
            return ((0, 0, 0, 10, delay, 0, 0),)
        if fixture == 21:
            return (
                (0, 0, 0, 10, 10, 0, 0),
                (0, 0, -1, 10, 10, 0, 1),
                (0, 0, 1, 10, 10, 0, 2),
            )
        delay = 10 if fixture in (15, 17) else 30
        rows = [
            (0, 0, -1, 10, delay, 0, 0),
            (0, 0, 0, 10, delay, 0, 1),
        ]
        if fixture == 17:
            return tuple(rows)
        rows.extend((
            (0, 0, 1, 10, delay, 0, 2),
            (-1, 0, 0, 10, delay, 0, 3),
            (1, 0, 0, 10, delay, 0, 4),
        ))
        return tuple(rows)
    if fixture in (0, 1, 2, 3, 7, 8, 9, 13, 14):
        delay = 10 if fixture == 13 else 30
        return ((0, 0, 0, 10, delay, 0, 0),)
    if fixture in (4, 5):
        return ((0, 1, 0, 8, 5, 0, 0),)
    if fixture == 10:
        return ((0, 0, -1, 8, 5, 0, 0),)
    if fixture == 12:
        return (
            (0, 1, 0, 8, 5, 0, 0),
            (0, 0, -1, 8, 5, 0, 1),
        )
    return ()


def validate_fixture(row, fixture, label):
    if row["before_blocks"] != expected_blocks(fixture, False):
        raise AssertionError(f"case {fixture} {label}: contaminated prestate")
    if row["after_blocks"] != expected_blocks(fixture, True):
        raise AssertionError(f"case {fixture} {label}: wrong block outcome")
    before_scheduled = tuple(map(tuple, row["before_scheduled"]))
    expected_before = ((0, 0, 0, 10, 1, 0, 0),) \
        if fixture in DYNAMIC_CASES else ()
    if before_scheduled != expected_before:
        raise AssertionError(
            f"case {fixture} {label}: wrong pre-edit schedule "
            f"{before_scheduled}")
    scheduled = tuple(map(tuple, row["after_scheduled"]))
    if scheduled != expected_scheduled(fixture):
        raise AssertionError(
            f"case {fixture} {label}: wrong schedule {scheduled}")
    sounds = row["sounds"]
    particles = row["particles"]
    if fixture in EFFECT_CASES:
        effect_count = 2 if fixture in (26, 27, 30, 31) else 1
        if len(sounds) != effect_count:
            raise AssertionError(
                f"case {fixture} {label}: expected {effect_count} mixing sounds")
        if any((sound["sound"], sound["category"],
                sound["volume_bits"]) != (
                    "minecraft:block.lava.extinguish", "block", "3f000000")
                for sound in sounds):
            raise AssertionError(
                f"case {fixture} {label}: wrong mixing sound {sounds}")
        if len(particles) != 8 * effect_count or any(
                particle["id"] != 12
                or particle["ignore_range"]
                or particle["parameters"]
                for particle in particles):
            raise AssertionError(
                f"case {fixture} {label}: wrong smoke stream {particles}")
    elif sounds or particles:
        raise AssertionError(
            f"case {fixture} {label}: effects without a mixing result")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    if args.native_only:
        for fixture in range(58):
            native = json.loads(subprocess.check_output(
                [args.native, str(fixture)], text=True))
            validate_fixture(native, fixture, "native")
        print("PASS native: 58 lava lifecycle rows")
        return
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
        for fixture in range(58):
            java = request(
                args.port, "static_lava_neighbor_locked", {"case": fixture})
            native = json.loads(subprocess.check_output(
                [args.native, str(fixture),
                 str(java["origin_x"]), str(java["origin_z"]),
                 str(java["dimension"])], text=True))
            validate_fixture(java, fixture, "Java")
            validate_fixture(native, fixture, "native")
            if java != native:
                differences = {
                    field: (java.get(field), native.get(field))
                    for field in sorted(set(java) | set(native))
                    if java.get(field) != native.get(field)
                }
                raise AssertionError(
                    f"case {fixture}: Java/native mismatch\n"
                    f"{json.dumps(differences, sort_keys=True)}")
        print("PASS real Java/native: 58 lava lifecycle rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
