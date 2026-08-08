#!/usr/bin/env python3
"""Lock the real 1.11.2 jukebox start/stop world-event contract."""

import argparse
import time

from test_dragon_crystal_notification import request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
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
        for item in range(2256, 2268):
            result = request(
                args.port, "jukebox_record_locked", {"item": item})
            if result["item"] != item \
                    or result["insert_result"] != "SUCCESS" \
                    or result["insert_meta"] != 1 \
                    or result["insert_record"] != item \
                    or result["held_after_insert"] != 0 \
                    or result["ejected"] is not True \
                    or result["eject_meta"] != 0 \
                    or result["eject_empty"] is not True:
                raise AssertionError(f"record {item}: {result}")
            events = result["world_events"]
            if len(events) != 2:
                raise AssertionError(f"record {item}: events={events}")
            expected = [(0, item), (1, 0)]
            for event, (seq, data) in zip(events, expected):
                if event["seq"] != seq or event["id"] != 1010 \
                        or event["data"] != data \
                        or (event["x"], event["y"], event["z"]) \
                        != (events[0]["x"], events[0]["y"],
                            events[0]["z"]):
                    raise AssertionError(
                        f"record {item}: event={event}, expected={expected}")
        print("PASS real Java: 12 jukebox records emit exact 1010 start/stop")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
