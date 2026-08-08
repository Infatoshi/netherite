#!/usr/bin/env python3
"""Compare nonlethal ordinary Witch landings with Java 1.11.2."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
SEEDS = (0, 3, 4, 82, 95, 402, 281474976710655)


def native(seed, scenario, world_seed):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_landing_oracle"),
         str(seed), scenario, str(world_seed)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA),
         "game/test_witch_landing_oracle"], check=True)
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
        for scenario in (
                "jump_safe", "slime", "farmland", "farmland_no_grief"):
            for seed in SEEDS:
                action = {
                    "scenario": scenario,
                    "entity_seed48": seed,
                }
                if scenario.startswith("farmland"):
                    action["world_seed48"] = seed
                java = request(args.port, "witch_landing_tick_locked", action)
                cpu = native(seed, scenario, seed)
                if java != cpu:
                    keys = sorted(set(java) | set(cpu))
                    mismatch = [key for key in keys
                                if java.get(key) != cpu.get(key)]
                    raise AssertionError(
                        f"scenario={scenario} seed={seed}: {mismatch}: "
                        f"Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: Jump Boost II safe landing, visible "
          "effect RNG, slime fall immunity/bounce, seeded farmland trample, "
          "mobGriefing suppression, BLOCK_DUST, and Witch/World RNG")


if __name__ == "__main__":
    main()
