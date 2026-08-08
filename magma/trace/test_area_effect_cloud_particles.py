#!/usr/bin/env python3
"""Compare real 1.11.2 and native lingering-cloud particle calls."""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


TRACE = Path(__file__).resolve().parent
JAVA = TRACE.parents[1] / "java"
sys.path.insert(0, str(JAVA))

from qrl_client import NetheriteEnv  # noqa: E402


def from_double_bits(value):
    return struct.unpack(">d", bytes.fromhex(value))[0]


def double_bits(value):
    return struct.pack(">d", float(value)).hex()


def native_case(binary, java, waiting, seed48, particle, param1, param2):
    x = from_double_bits(java["x_bits"])
    y = from_double_bits(java["y_bits"])
    z = from_double_bits(java["z_bits"])
    event = {
        "tick": 0,
        "type": "spawn_area_effect_cloud_fixture",
        "eid": 681000 + int(waiting),
        "potion_type": 16,
        "x": x,
        "y": y,
        "z": z,
        "age": 5 if waiting else 20,
        "duration": 600,
        "wait_time": 10,
        "reapplication_delay": 20,
        "radius": 3.0,
        "radius_on_use": -0.5,
        "radius_per_tick": -0.005,
        "next_application": 0,
        "entity_seed48": seed48,
        "ignore_radius": int(waiting),
        "particle": particle,
        "particle_param1": param1,
        "particle_param2": param2,
    }
    action = {
        "tick": 0, "type": "action",
        "forward": 0, "strafe": 0, "jump": 0, "sneak": 0,
        "sprint": 0, "attack": 0, "use": 0,
        "do_break": 0, "do_place": 0,
        "dyaw": 0, "dpitch": 0, "hotbar": -1,
        "close_container": 0,
    }
    with tempfile.TemporaryDirectory(
            prefix="netherite_cloud_particles_") as temp:
        root = Path(temp)
        script = root / "script.jsonl"
        state = root / "state.jsonl"
        script.write_text(
            json.dumps(event, separators=(",", ":")) + "\n"
            + json.dumps(action, separators=(",", ":")) + "\n",
            encoding="utf-8")
        subprocess.run([
            str(binary.resolve()),
            "--seed", "101", "--world", "superflat",
            "--view-distance", "1", "--headless", "--ticks", "1",
            "--script", str(script), "--state-out", str(state),
            "--render", "off", "--pace", "unlimited",
            "--weather", "off", "--daylight", "off", "--mobs", "off",
        ], cwd=binary.resolve().parent, env=os.environ.copy(), check=True)
        return json.loads(state.read_text(encoding="utf-8").splitlines()[0])


def compare_case(binary, java, waiting, seed48, particle, param1, param2):
    native = native_case(
        binary, java, waiting, seed48, particle, param1, param2)
    expected = java["particles"]
    actual = native["particle_events"]
    if len(actual) != len(expected):
        raise AssertionError(
            f"waiting={waiting}: Java emitted {len(expected)}, "
            f"native emitted {len(actual)}")
    for index, (java_row, native_row) in enumerate(zip(expected, actual)):
        if java_row["id"] != particle or java_row["ignore_range"]:
            raise AssertionError(f"unexpected Java particle {index}: {java_row}")
        if (native_row["kind"], native_row["count"],
                native_row["entity_eid"]) != (particle, 0, -1):
            raise AssertionError(
                f"unexpected native descriptor {index}: {native_row}")
        native_payload = [
            native_row["x"], native_row["y"], native_row["z"],
            native_row["motion_x"], native_row["motion_y"],
            native_row["motion_z"],
        ]
        native_payload_bits = [double_bits(value) for value in native_payload]
        if native_payload_bits != java_row["payload_bits"]:
            raise AssertionError(
                f"waiting={waiting} particle {index} mismatch\n"
                f"Java:  {java_row['payload_bits']}\n"
                f"native:{native_payload_bits}")
        native_parameters = native_row["parameters"][
            :native_row["parameter_count"]]
        if native_parameters != java_row["parameters"]:
            raise AssertionError(
                f"waiting={waiting} particle {index} parameters: "
                f"Java={java_row['parameters']} native={native_parameters}")
    clouds = [row for row in native["entities"]
              if row.get("kind") == "area_effect_cloud"]
    if len(clouds) != 1:
        raise AssertionError(f"native cloud missing: {clouds}")
    if clouds[0]["entity_seed48"] != java["entity_seed48"]:
        raise AssertionError(
            f"waiting={waiting} RNG cursor: Java={java['entity_seed48']} "
            f"native={clouds[0]['entity_seed48']}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=Path, required=True)
    args = parser.parse_args()

    cases = (
        (False, 0x123456789ABC, 15, 0, 0),
        (True, 0x23456789ABCD, 15, 0, 0),
        (False, 0x3456789ABCDE, 36, 322, 5),
        (True, 0x23456789ABCD, 37, 1, 73),
    )
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 101, "mode": "survival", "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    java_cases = []
    try:
        for waiting, seed48, particle, param1, param2 in cases:
            result = env._cmd({
                "cmd": "area_effect_cloud_particles_locked",
                "action": {
                    "waiting": waiting,
                    "radius": 3.0,
                    "entity_seed48": seed48,
                    "particle": particle,
                    "particle_param1": param1,
                    "particle_param2": param2,
                },
            })
            if not result.get("ok"):
                raise AssertionError(result)
            java_cases.append(result)
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")

    for case, java in zip(cases, java_cases):
        compare_case(args.native, java, *case)
    if len(java_cases[0]["particles"]) != 29:
        raise AssertionError("radius-three active cloud did not emit 29 particles")
    if len(java_cases[1]["particles"]) != 2:
        raise AssertionError("seeded waiting cloud did not emit its two particles")
    print("area-effect-cloud particles: PASS "
          "(spell and parameterized particle branches -> exact native call "
          "stream and RNG cursor)")


if __name__ == "__main__":
    main()
