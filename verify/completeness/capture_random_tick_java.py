#!/usr/bin/env python3
"""Capture the direct real-WorldServer random-tick ownership receipt."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java/start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


NO_OP_CASES = [
    (28, 0), (50, 5), (70, 0), (72, 0), (75, 5), (76, 5),
    (77, 5), (86, 0), (91, 0), (92, 0), (131, 0), (132, 0),
    (143, 5), (147, 0), (148, 0), (171, 0),
]


def oracle(action: str, instance: int, environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append("0")
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise RuntimeError(f"oracle {action} failed with rc={result.returncode}")


def capture(instance: int) -> dict:
    run_root = ROOT / ".tmp" / f"random-tick-java-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache/uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = locked = False
    try:
        oracle("start", instance, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise RuntimeError("real Java client did not become ready")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        authoritative = save_fork.request(
            port, "normalize_reload_locked").get("authoritative", observation)
        x = math.floor(float(authoritative["x"])) + 10
        y = math.floor(float(authoritative["y"])) + 3
        z = math.floor(float(authoritative["z"]))
        support = [[x + dx, y + dy, z + dz, 1, 0] for dx, dy, dz in (
            (0, -1, 0), (0, 1, 0), (-1, 0, 0), (1, 0, 0),
            (0, 0, -1), (0, 0, 1),
        )]
        no_ops = []
        for block, meta in NO_OP_CASES:
            save_fork.request(port, "setblocks_locked", {
                "blocks": support + [[x, y, z, block, meta]],
            })
            save_fork.request(port, "random_tick_locked", {
                "x": x, "y": y, "z": z, "block": block, "seed": 42,
            })
            controlled = save_fork.request(
                port, "server_tick_locked")["authoritative"]["controlled_input"]
            no_ops.append({
                "block": block, "meta": meta,
                "before_world_rng48": controlled["before"]["world_rand_seed48"],
                "after_world_rng48": controlled["world_rand_seed48"],
            })
        active = []
        cases = (
            ("lit_redstone_ore", 74, support + [[x, y, z, 74, 0]]),
            ("magma_static_water", 213,
             support + [[x, y, z, 213, 0], [x, y + 1, z, 9, 0]]),
            ("supported_flower", 37,
             support + [[x, y - 1, z, 3, 0], [x, y, z, 37, 0]]),
            ("unsupported_flower", 37,
             support + [[x, y - 1, z, 0, 0], [x, y, z, 37, 0]]),
        )
        raw_path = run_root / "callback.bin"
        for name, block, blocks in cases:
            save_fork.request(port, "setblocks_locked", {"blocks": blocks})
            save_fork.request(port, "random_tick_locked", {
                "x": x, "y": y, "z": z, "block": block, "seed": 42,
            })
            controlled = save_fork.request(
                port, "server_tick_locked")["authoritative"]["controlled_input"]
            save_fork.request(port, "getblocks_locked", {
                "x0": x, "y0": y, "z0": z,
                "x1": x, "y1": y + 1, "z1": z,
                "file": str(raw_path),
            })
            active.append({
                "case": name, "block": block, "meta": 0,
                "raw_y_y1": list(raw_path.read_bytes()),
                "before_world_rng48": controlled["before"]["world_rand_seed48"],
                "after_world_rng48": controlled["world_rand_seed48"],
            })
        for name, block, radius in (
                ("dynamic_water_flat", 8, 4),
                ("dynamic_lava_flat", 10, 5)):
            blocks = []
            for oz in range(-radius, radius + 1):
                for ox in range(-radius, radius + 1):
                    if abs(ox) + abs(oz) > radius:
                        continue
                    blocks.extend((
                        [x + ox, y - 1, z + oz, 1, 0],
                        [x + ox, y, z + oz, 0, 0],
                        [x + ox, y + 1, z + oz, 0, 0],
                        [x + ox, y + 2, z + oz, 0, 0],
                    ))
            blocks.append([x, y, z, block, 0])
            save_fork.request(port, "setblocks_locked", {"blocks": blocks})
            save_fork.request(port, "random_tick_locked", {
                "x": x, "y": y, "z": z, "block": block, "seed": 42,
            })
            controlled = save_fork.request(
                port, "server_tick_locked")["authoritative"]["controlled_input"]
            save_fork.request(port, "getblocks_locked", {
                "x0": x - 1, "y0": y, "z0": z - 1,
                "x1": x + 1, "y1": y, "z1": z + 1,
                "file": str(raw_path),
            })
            raw = raw_path.read_bytes()
            plane = [raw[i] | raw[i + 1] << 8
                     for i in range(0, len(raw), 2)]
            active.append({
                "case": name, "block": block, "meta": 0,
                "raw_plane_3x3": plane,
                "before_world_rng48": controlled["before"]["world_rand_seed48"],
                "after_world_rng48": controlled["world_rand_seed48"],
            })
        replacements = (
            ("water_replaces_flower", 8, 37),
            ("water_replaces_snow", 8, 78),
            ("lava_replaces_flower", 10, 37),
        )
        for name, block, target in replacements:
            blocks = []
            for oz in range(-1, 2):
                for ox in range(-1, 2):
                    blocks.extend((
                        [x + ox, y - 1, z + oz, 1, 0],
                        [x + ox, y, z + oz, 1, 0],
                        [x + ox, y + 1, z + oz, 0, 0],
                    ))
            blocks.extend(([x, y, z, block, 0],
                           [x + 1, y, z, target, 0]))
            save_fork.request(port, "setblocks_locked", {"blocks": blocks})
            save_fork.request(port, "random_tick_locked", {
                "x": x, "y": y, "z": z, "block": block, "seed": 42,
            })
            controlled = save_fork.request(
                port, "server_tick_locked")["authoritative"]["controlled_input"]
            save_fork.request(port, "getblocks_locked", {
                "x0": x, "y0": y, "z0": z,
                "x1": x + 1, "y1": y, "z1": z,
                "file": str(raw_path),
            })
            raw = raw_path.read_bytes()
            active.append({
                "case": name, "block": block, "meta": 0,
                "raw_source_target": [
                    raw[i] | raw[i + 1] << 8
                    for i in range(0, len(raw), 2)],
                "before_world_rng48": controlled["before"]["world_rand_seed48"],
                "after_world_rng48": controlled["world_rand_seed48"],
            })
        blocks = [
            [x, y - 2, z, 1, 0], [x, y - 1, z, 9, 0],
            [x, y, z, 10, 0],
        ]
        save_fork.request(port, "setblocks_locked", {"blocks": blocks})
        save_fork.request(port, "random_tick_locked", {
            "x": x, "y": y, "z": z, "block": 10, "seed": 42,
        })
        controlled = save_fork.request(
            port, "server_tick_locked")["authoritative"]["controlled_input"]
        save_fork.request(port, "getblocks_locked", {
            "x0": x, "y0": y - 1, "z0": z,
            "x1": x, "y1": y, "z1": z, "file": str(raw_path),
        })
        raw = raw_path.read_bytes()
        active.append({
            "case": "lava_down_into_water", "block": 10, "meta": 0,
            "raw_below_source": [raw[i] | raw[i + 1] << 8
                                 for i in range(0, len(raw), 2)],
            "before_world_rng48": controlled["before"]["world_rand_seed48"],
            "after_world_rng48": controlled["world_rand_seed48"],
        })
        return {
            "schema": "netherite.random_tick_java_receipt", "version": 1,
            "source": "real Minecraft Java 1.11.2 WorldServer Block.randomTick",
            "public_seed": 42, "no_op_cases": no_ops, "active_cases": active,
        }
    finally:
        if locked:
            save_fork.request(port, "server_step_unlock")
        if started:
            oracle("stop", instance, environment)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--instance", type=int, default=49)
    parser.add_argument("--output", type=pathlib.Path, default=HERE /
                        "random_tick_java_receipt.json")
    args = parser.parse_args()
    args.output.write_text(json.dumps(
        capture(args.instance), indent=2, sort_keys=True) + "\n")
    print(f"PASS direct real-Java random-tick receipt -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
