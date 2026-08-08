#!/usr/bin/env python3
"""Compare shared living slime travel with real Minecraft 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
TYPES = ("zombie", "sheep", "witch")
SCENARIOS = ("bounce", "walk")
HOSTILE_STONE_TYPES = (
    "zombie", "zombie_villager", "skeleton", "wither_skeleton",
    "creeper", "spider", "cave_spider", "pigman", "silverfish",
    "enderman",
)
GENERIC_STONE_TYPES = ("sheep", "pig", "cow", "villager")
LANDING_SCENARIOS = ("stone", "stone_big", "hay", "stone_jump")
ENTITY_SEED48 = 3
MATH_SEED48 = 0
ENDERMAN_SEEDS = (0, 2, 4, 82, 95, 402, (1 << 48) - 1)


def native(entity_type, scenario, base_x, base_z,
           entity_seed48=ENTITY_SEED48, math_seed48=MATH_SEED48):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_living_slime_oracle"),
         entity_type, scenario, str(base_x), str(base_z),
         str(entity_seed48), str(math_seed48)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_living_slime_oracle"], check=True)
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
        for entity_type in TYPES:
            for scenario in SCENARIOS:
                action = {"type": entity_type, "scenario": scenario}
                java = request(
                    args.port, "living_slime_travel_locked", action)
                cpu = native(
                    entity_type, scenario, java["base_x"], java["base_z"])
                if java != cpu:
                    keys = sorted(set(java) | set(cpu))
                    mismatch = [key for key in keys
                                if java.get(key) != cpu.get(key)]
                    raise AssertionError(
                        f"type={entity_type} scenario={scenario}: "
                        f"{mismatch}: Java={java} CPU={cpu}")
        for entity_type in HOSTILE_STONE_TYPES + GENERIC_STONE_TYPES:
            for scenario in LANDING_SCENARIOS:
                action = {
                    "type": entity_type,
                    "scenario": scenario,
                    "entity_seed48": ENTITY_SEED48,
                    "math_seed48": MATH_SEED48,
                }
                java = request(
                    args.port, "living_slime_travel_locked", action)
                cpu = native(
                    entity_type, scenario,
                    java["base_x"], java["base_z"])
                if java != cpu:
                    keys = sorted(set(java) | set(cpu))
                    mismatch = [key for key in keys
                                if java.get(key) != cpu.get(key)]
                    raise AssertionError(
                        f"type={entity_type} scenario={scenario}: "
                        f"{mismatch}: Java={java} CPU={cpu}")
        for entity_seed48 in ENDERMAN_SEEDS:
            for scenario in LANDING_SCENARIOS:
                action = {
                    "type": "enderman",
                    "scenario": scenario,
                    "entity_seed48": entity_seed48,
                    "math_seed48": MATH_SEED48,
                }
                java = request(
                    args.port, "living_slime_travel_locked", action)
                cpu = native(
                    "enderman", scenario,
                    java["base_x"], java["base_z"], entity_seed48)
                if java != cpu:
                    keys = sorted(set(java) | set(cpu))
                    mismatch = [key for key in keys
                                if java.get(key) != cpu.get(key)]
                    raise AssertionError(
                        f"type=enderman scenario={scenario} "
                        f"seed={entity_seed48}: {mismatch}: "
                        f"Java={java} CPU={cpu}")
        for entity_seed48 in (3,) + ENDERMAN_SEEDS:
            action = {
                "type": "enderman",
                "scenario": "drown",
                "entity_seed48": entity_seed48,
                "math_seed48": MATH_SEED48,
            }
            java = request(
                args.port, "living_slime_travel_locked", action)
            cpu = native(
                "enderman", "drown",
                java["base_x"], java["base_z"], entity_seed48)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"type=enderman scenario=drown seed={entity_seed48}: "
                    f"{mismatch}: Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: zombie, sheep, and Witch slime bounce, "
          "low-speed living walk damping, 52 ordinary nonlethal stone/hay/"
          "Jump Boost falls, Enderman teleport/no-teleport RNG tails, and "
          "BLOCK_DUST (98 exact rows)")


if __name__ == "__main__":
    main()
