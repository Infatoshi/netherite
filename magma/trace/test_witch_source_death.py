#!/usr/bin/env python3
"""Compare ordinary source-less Witch deaths with Java 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    (0, 0, 0, 100, True),
    (3, 1, 2, 101, True),
    (4, 281474976710655, 3, 102, True),
    (82, 67890, 123456789, 103, True),
    (95, 424242, 987654321, 104, True),
    (402, 999999, 7777777, 105, True),
    (12345, 281474976710654, 281474976710655, 106, False),
)


def native(case, source):
    entity_seed, math_seed, world_seed, next_id, enabled = case
    command = [str(MAGMA / "game" / "test_witch_source_death_oracle"),
               str(entity_seed), str(math_seed), str(world_seed), str(next_id),
               "1" if enabled else "0"]
    if source == "burning":
        command.append("on_fire")
    elif source in ("lava", "lava_burning", "lava_water_flow"):
        command.append(source)
    elif source in ("in_fire", "in_fire_tick", "in_fire_water_tick",
                    "in_fire_rain_tick", "in_fire_rain_roof_tick",
                    "in_wall_tick", "fall_tick", "fall_big_tick",
                    "fall_hay_tick",
                    "cactus", "cactus_tick"):
        command.append(source)
    proc = subprocess.run(
        command,
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_witch_source_death_oracle"], check=True)
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
        for source in (
                "anvil", "burning", "lava", "lava_burning",
                "lava_water_flow", "in_fire",
                "in_fire_tick", "in_fire_water_tick", "in_fire_rain_tick",
                "in_fire_rain_roof_tick", "in_wall_tick",
                "fall_tick", "fall_big_tick", "fall_hay_tick",
                "cactus", "cactus_tick"):
            for case in CASES:
                entity_seed, math_seed, world_seed, next_id, enabled = case
                java = request(args.port, f"witch_{source}_death_locked", {
                    "entity_seed48": entity_seed,
                    "math_seed48": math_seed,
                    "world_seed48": world_seed,
                    "next_entity_id": next_id,
                    "do_mob_loot": enabled,
                })
                cpu = native(case, source)
                if java != cpu:
                    keys = sorted(set(java) | set(cpu))
                    mismatch = [key for key in keys
                                if java.get(key) != cpu.get(key)]
                    raise AssertionError(
                        f"source={source} case={case}: {mismatch}: "
                        f"Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: falling-anvil, ON_FIRE, lava, "
          "ON_FIRE-before-LAVA, water-current-before-LAVA, "
          "direct/full-tick IN_FIRE, and "
          "water/open-rain/roofed-rain IN_FIRE, plus direct/full-tick "
          "CACTUS, full-tick IN_WALL, and small/big stone plus hay FALL Witch "
          "Looting-0 drops, "
          "no equipment/XP, death feedback, particles, and all RNG cursors")


if __name__ == "__main__":
    main()
