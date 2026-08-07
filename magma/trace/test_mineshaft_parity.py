#!/usr/bin/env python3
"""Real-1.11.2 topology and isolated placement gate for mineshafts."""
import argparse
import json
import re
import socket
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / ".tmp"
DRIVER = TMP / "test_mineshaft_parity"
LOOT_DRIVER = TMP / "test_mineshaft_loot"


def request(stream, command):
    stream.write((json.dumps(command) + "\n").encode())
    reply = json.loads(stream.readline())
    if not reply.get("ok", False):
        raise RuntimeError(str(reply))
    return reply


def build_driver():
    TMP.mkdir(exist_ok=True)
    subprocess.run([
        "cc", "-O2", "-std=c99", "-I", str(ROOT / "blaze/core"),
        str(ROOT / "blaze/cpu/map_gen_mineshaft.c"), "-lm", "-o", str(DRIVER),
    ], check=True)
    subprocess.run([
        "cc", "-O2", "-ffp-contract=off",
        str(ROOT / "blaze/cpu/abandoned_mineshaft_loot.c"),
        "-lm", "-o", str(LOOT_DRIVER),
    ], check=True)


def c_run(*args):
    return subprocess.run([str(DRIVER), *map(str, args)], text=True,
                          capture_output=True, check=True)


def parse_c_events(stderr):
    carts = []
    spawners = []
    for line in stderr.splitlines():
        if line.startswith("cart="):
            x, y, z, seed = map(int, line[5:].split(","))
            carts.append({"x": x, "y": y, "z": z, "loot_seed": seed})
        elif line.startswith("spawner="):
            x, y, z, kind = line[8:].split(",")
            spawners.append({"x": int(x), "y": int(y), "z": int(z),
                             "entity": "minecraft:" + kind})
    return carts, spawners


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25629)
    args = parser.parse_args()
    build_driver()
    loot_native = [int(line) for line in subprocess.run(
        [str(LOOT_DRIVER)], text=True, capture_output=True, check=True
    ).stdout.splitlines()]

    placement_cases = [
        (143, 0, 0, 0, 987654321, 3, 8, 0),
        (143, 0, 0, 0, 987654321, 2, 9, 0),  # cave-spider spawner
        (143, 0, 0, 0, 0, 3, 8, 0),          # chest minecart
        (143, 0, 0, 0, 2, 2, 4, 0),
        (143, 0, 0, 0, 3, 4, 10, 0),         # east/west rail metadata
        (143, 0, 0, 0, 4, 6, 5, 0),          # rail slope and isolated rail
        (143, 0, 0, 1, 6, 3, 3, 0),
        (143, 0, 0, 1, 8, 1, 5, 0),
        (143, 0, 0, 0, 987654321, 3, 8, 1),  # mesa dark-oak supports
        (143, 0, 0, 0, 4, 6, 5, 1),          # mesa rail/floor metadata
    ]

    with socket.create_connection(("127.0.0.1", args.port), timeout=30) as sock:
        stream = sock.makefile("rwb", buffering=0)
        request(stream, {"cmd": "server_step_lock"})

        pieces = 0
        for seed, cx, cz, mine_type in [
                (143, 0, 0, 0), (310, 0, 0, 0), (3, 0, 0, 0),
                (143, 0, 0, 1)]:
            java = request(stream, {"cmd": "mineshaft_topology_locked", "action": {
                "seed": seed, "cx": cx, "cz": cz, "mine_type": mine_type,
            }})
            command = "--topology-type" if mine_type else "--topology"
            native_args = (command, seed, cx, cz, mine_type) if mine_type else (
                command, seed, cx, cz)
            native = json.loads(c_run(*native_args).stdout)
            if java["starts"] != native["starts"]:
                raise AssertionError("topology mismatch for seed %d" % seed)
            pieces += sum(len(start["pieces"]) for start in java["starts"])

        cells = 0
        for index, case in enumerate(placement_cases):
            seed, cx, cz, start, placement_seed, dx, dz, mine_type = case
            output = TMP / ("mineshaft_java_%d.bin" % index)
            java = request(stream, {"cmd": "mineshaft_placement_locked", "action": {
                "seed": seed, "cx": cx, "cz": cz, "start": start,
                "placement_seed": placement_seed, "clip_dx": dx, "clip_dz": dz,
                "mine_type": mine_type,
                "file": str(output),
            }})
            if mine_type:
                native = c_run("--placement-type", seed, cx, cz, start,
                               placement_seed, dx, dz, mine_type)
            else:
                native = c_run("--placement", *case[:-1])
            raw = output.read_bytes()
            java_states = [int.from_bytes(raw[i:i + 2], "little")
                           for i in range(0, len(raw), 2)]
            c_states = [int(value, 16) for value in native.stdout.split()]
            # y=0 in a default oracle save can retain pre-fixture bedrock/stone
            # variants. Every mineshaft piece is y>=15, so compare its full domain.
            bad = [i for i, (a, b) in enumerate(zip(java_states, c_states))
                   if i // 256 >= 10 and a != b]
            if bad:
                raise AssertionError("placement mismatch case %d, cells=%r" %
                                     (index, bad[:16]))
            match = re.search(r"rng_seed48_after=(\d+)", native.stderr)
            if not match or int(match.group(1)) != java["rng_seed48_after"]:
                raise AssertionError("placement RNG mismatch case %d" % index)
            c_carts, c_spawners = parse_c_events(native.stderr)
            j_carts = [{k: event[k] for k in ("x", "y", "z", "loot_seed")}
                       for event in java["carts"]]
            if c_carts != j_carts or c_spawners != java["spawners"]:
                raise AssertionError("placement event mismatch case %d" % index)
            cells += len(java_states) - 10 * 256

        loot_seeds = [0, 42, 7230402065820649518, -7074434463822813898]
        loot_fields = 27 * 8 + 1
        if len(loot_native) != len(loot_seeds) * loot_fields:
            raise AssertionError("mineshaft loot output length mismatch")
        for index, seed in enumerate(loot_seeds):
            java = request(stream, {"cmd": "mineshaft_loot_locked", "action": {
                "seed": seed,
            }})
            native = loot_native[index * loot_fields:(index + 1) * loot_fields]
            if java["values"] != native:
                bad = [i for i, (a, b) in enumerate(zip(java["values"], native))
                       if a != b]
                raise AssertionError("loot mismatch seed %d fields=%r" %
                                     (seed, bad[:16]))

        request(stream, {"cmd": "server_step_unlock"})
    print("PASS mineshaft parity: %d topology pieces, %d placement states, "
          "%d placement cases, %d loot fields" %
          (pieces, cells, len(placement_cases), len(loot_native)))


if __name__ == "__main__":
    main()
