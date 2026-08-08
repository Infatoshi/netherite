#!/usr/bin/env python3
"""Measure Armor Stand interaction/damage rows in real Minecraft 1.11.2."""

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
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class ProbeError(RuntimeError):
    pass


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise ProbeError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _summon(port: int, x: float, y: float, z: float,
            overrides: dict | None = None) -> dict:
    request = {
        "type": "armor_stand",
        "x": x, "y": y, "z": z,
        "mx": 0.0, "my": 0.0, "mz": 0.0,
        "health": 20.0, "air": 300, "on_ground": True,
        "show_arms": True, "world_time": 100,
        "entity_seed48": 0x123456789ABC,
        "eid": 6901,
        "uuid": "9c4a1dc0-a599-4d35-8000-000000006901",
    }
    request.update(overrides or {})
    response = save_fork.request(port, "summon_locked", request)
    if not response.get("ok"):
        raise ProbeError(f"Armor Stand summon failed: {response}")
    return response


def probe(source: pathlib.Path, instance: int, seed: int) -> dict:
    save_fork.validate_snapshot(source)
    run_root = ROOT / ".tmp" / f"armor-stand-probe-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(source / "save"),
        "ORACLE_POOL_USERNAME": "PoolPlayer0",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    rows = []
    try:
        _oracle("start", instance, seed, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if observation.get("ok") and "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise ProbeError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        state = normalized.get("authoritative") or observation
        x = math.floor(float(state["x"])) + 4.5
        y = math.floor(float(state["y"])) + 4.0
        z = math.floor(float(state["z"])) + 0.5

        cases = [
            ("equip_two_helmets", {}, {
                "operation": "interact", "item": 310, "count": 2,
                "meta": 7, "hit_y": 1.8,
            }),
            ("disabled_head", {"disabled_slots": 1 << 4}, {
                "operation": "interact", "item": 310, "count": 1,
                "meta": 0, "hit_y": 1.8,
            }),
            ("hidden_arms", {"show_arms": False}, {
                "operation": "interact", "item": 280, "count": 1,
                "meta": 0, "hit_y": 1.0,
            }),
            ("remove_boots", {"equipment": [
                {"slot": 2, "item": 313, "count": 1, "meta": 4},
            ]}, {
                "operation": "interact", "item": 0, "count": 0,
                "meta": 0, "hit_y": 0.2,
            }),
            ("name_tag_pass", {}, {
                "operation": "interact", "item": 421, "count": 1,
                "meta": 0, "hit_y": 1.0,
            }),
            ("first_punch", {}, {
                "operation": "player", "amount": 1.0, "punch_age": 6,
            }),
            ("second_punch", {
                "equipment": [
                    {"slot": 5, "item": 310, "count": 1, "meta": 2},
                ],
            }, {"operation": "player", "amount": 1.0, "punch_age": 5}),
            ("arrow_break", {"equipment": [
                {"slot": 0, "item": 280, "count": 1, "meta": 0},
            ]}, {"operation": "arrow", "amount": 3.0}),
            ("explosion_break", {"equipment": [
                {"slot": 2, "item": 313, "count": 1, "meta": 1},
            ]}, {"operation": "explosion", "amount": 6.0}),
            ("creative_break", {"equipment": [
                {"slot": 0, "item": 280, "count": 1, "meta": 0},
            ]}, {"operation": "creative", "amount": 1.0}),
            ("in_fire_first", {}, {
                "operation": "in_fire", "amount": 1.0,
            }),
            ("in_fire_repeat", {"fire_seconds": 5}, {
                "operation": "in_fire", "amount": 1.0,
            }),
            ("on_fire", {}, {
                "operation": "on_fire", "amount": 1.0,
            }),
            ("void", {}, {
                "operation": "void", "amount": 1.0,
            }),
        ]
        for case_id, summon_overrides, action in cases:
            save_fork.request(port, "clear_entities_locked")
            _summon(port, x, y, z, summon_overrides)
            result = save_fork.request(port, "armor_stand_action_locked", {
                "eid": 6901, **action,
            })
            if not result.get("ok"):
                raise ProbeError(f"{case_id} failed: {result}")
            result["id"] = case_id
            rows.append(result)
        return {
            "schema": "netherite.armor_stand_oracle",
            "version": 1,
            "rows": rows,
        }
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    result = probe(args.source.resolve(), args.instance, args.seed)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(encoded, end="")
    else:
        args.output.write_text(encoded)
        print(f"PASS Armor Stand oracle -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, ProbeError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL Armor Stand oracle: {exc}", file=sys.stderr)
        raise SystemExit(1)
