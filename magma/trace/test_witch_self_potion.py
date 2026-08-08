#!/usr/bin/env python3
"""Compare isolated Witch self-potion start/completion to shared CPU."""
import argparse
import json
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    ("start", "water", 0),
    ("start", "fire", 0),
    ("start", "heal", 0),
    ("start", "speed", 0),
    ("start", "none", 5588),
    ("start", "none", 3846),
    ("finish", "water", 5588),
    ("finish", "fire", 5588),
    ("finish", "heal", 5588),
    ("finish", "speed", 5588),
)


def native(phase, scenario, seed48):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_witch_self_potion_oracle"),
         phase, scenario, str(seed48)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_witch_self_potion_oracle"],
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
        for phase, scenario, seed48 in CASES:
            java = request(args.port, "witch_self_potion_locked", {
                "phase": phase,
                "scenario": scenario,
                "entity_seed48": seed48,
            })
            cpu = native(phase, scenario, seed48)
            if java != cpu:
                keys = sorted(set(java) | set(cpu))
                mismatch = [key for key in keys if java.get(key) != cpu.get(key)]
                raise AssertionError(
                    f"{phase}/{scenario}/{seed48}: {mismatch}: "
                    f"Java={java} CPU={cpu}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print("PASS real Java/shared CPU: Witch self-potion selection, direct RNG, "
          "drink audio, held state, timer, speed penalty, and completion effects")


if __name__ == "__main__":
    main()
