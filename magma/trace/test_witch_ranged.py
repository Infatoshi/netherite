#!/usr/bin/env python3
"""Compare real Witch ranged selection/launch math with shared CPU code."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    ("far_slowness", 5588, 0),
    ("moving_slowness", 5588, 1),
    ("far_slow_poison", 5588, 2),
    ("mid_poison", 5588, 12345),
    ("mid_poison_active", 5588, 999999),
    ("close_weakness", 0, 12345),
    ("close_weakness_fail", 5588, 12345),
    ("close_weakness_active", 0, 281474976710655),
    ("drinking", 0, 777),
)


def native(scenario, seed48, projectile_seed48):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_ranged_oracle"),
         scenario, str(seed48), str(projectile_seed48)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_witch_ranged_oracle"],
        check=True)
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
        for scenario, seed48, projectile_seed48 in CASES:
            java = request(args.port, "witch_ranged_locked", {
                "scenario": scenario,
                "entity_seed48": seed48,
                "projectile_seed48": projectile_seed48,
            })
            cpu = native(scenario, seed48, projectile_seed48)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys
                            if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{scenario}/{seed48}/{projectile_seed48}: {mismatch}: "
                    f"Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: Witch ranged potion selection, direct "
          "RNG, projectile spawn, throw sound, and exact heading")


if __name__ == "__main__":
    main()
