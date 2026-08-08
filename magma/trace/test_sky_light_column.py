#!/usr/bin/env python3
"""Compare mixed-opacity sky-light column edits to live Minecraft 1.11.2."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


CENTER = 1028
LOWER = 907
COLUMN = tuple(60 + 121 * index for index in range(9))
BLOCK_TRANSITIONS = (
    ((CENTER, 0, 16),),
    ((CENTER, 16, 0),),
    ((CENTER, 0, 288),),
    ((CENTER, 288, 0),),
    ((CENTER, 0, 144),),
    ((CENTER, 144, 0),),
    ((CENTER, 0, 320),),
    ((LOWER, 0, 144), (CENTER, 144, 128)),
    ((LOWER, 144, 0),),
    ((CENTER, 0, 16),),
    ((CENTER, 0, 144),),
    ((LOWER, 0, 288),),
    ((LOWER, 0, 288), (CENTER, 144, 128)),
)
SKY_TRANSITIONS = (
    tuple((index, 15, 0 if index == CENTER else 14) for index in COLUMN),
    tuple((index, 0 if index == CENTER else 14, 15) for index in COLUMN),
    tuple((index, 15, 14) for index in COLUMN),
    tuple((index, 14, 15) for index in COLUMN),
    tuple((index, 15, 12 if index == CENTER else 14) for index in COLUMN),
    tuple((index, 12 if index == CENTER else 14, 15) for index in COLUMN),
    (),
    ((LOWER, 14, 12),),
    ((LOWER, 12, 14),),
    tuple((index, 15, 0 if index == CENTER else 14) for index in COLUMN),
    tuple((index, 15, 12 if index == CENTER else 14) for index in COLUMN),
    (),
    (),
)
BEFORE_SCHEDULED = tuple(
    ((0, 0, 0, 8, 5, 0, 0),) if fixture == 8 else ()
    for fixture in range(13)
)
AFTER_SCHEDULED = tuple(
    ((0, 0, 0, 8, 5, 0, 0),)
    if fixture in (7, 8, 12) else ()
    for fixture in range(13)
)


def transitions(before, after):
    return tuple(
        (index, old, new)
        for index, (old, new) in enumerate(zip(before, after))
        if old != new)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    args = parser.parse_args()
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
        for fixture in range(13):
            java = request(
                args.port, "sky_light_column_locked", {"case": fixture})
            native = json.loads(subprocess.check_output(
                [args.native, str(fixture)], text=True))
            java_block_transitions = transitions(
                java["before_blocks"], java["after_blocks"])
            java_sky_transitions = transitions(
                java["before_sky"], java["after_sky"])
            if java_block_transitions != BLOCK_TRANSITIONS[fixture]:
                raise AssertionError(
                    f"case {fixture}: contaminated block fixture "
                    f"{java_block_transitions}")
            if java_sky_transitions != SKY_TRANSITIONS[fixture]:
                raise AssertionError(
                    f"case {fixture}: unexpected Java sky transition "
                    f"{java_sky_transitions}")
            if tuple(map(tuple, java["before_scheduled"])) \
                    != BEFORE_SCHEDULED[fixture]:
                raise AssertionError(
                    f"case {fixture}: unexpected pre-edit schedule "
                    f"{java['before_scheduled']}")
            if tuple(map(tuple, java["after_scheduled"])) \
                    != AFTER_SCHEDULED[fixture]:
                raise AssertionError(
                    f"case {fixture}: unexpected post-edit schedule "
                    f"{java['after_scheduled']}")
            if java != native:
                field_diffs = {}
                for field in (
                        "before_blocks", "before_sky",
                        "after_blocks", "after_sky",
                        "before_scheduled", "after_scheduled"):
                    java_values = java.pop(field, [])
                    native_values = native.pop(field, [])
                    diffs = [
                        (index, a, b)
                        for index, (a, b) in enumerate(
                            zip(java_values, native_values))
                        if a != b
                    ]
                    field_diffs[field] = {
                        "lengths": (len(java_values), len(native_values)),
                        "count": len(diffs),
                        "first": diffs[:24],
                    }
                raise AssertionError(
                    f"case {fixture}:\n"
                    f"java={json.dumps(java, sort_keys=True)}\n"
                    f"native={json.dumps(native, sort_keys=True)}\n"
                    f"field_diffs={field_diffs}")
        print("PASS real Java/native: 13 mixed-opacity sky-light rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
