#!/usr/bin/env python3
"""Measure real 1.11.2 skeleton-horse trap activation ticks."""

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


def _pin_generator(hidden: dict, key: str, seed48: int) -> None:
    for row in hidden["seed_helper"]["generators"]:
        if row.get("key") == key:
            row["seed48"] = seed48
            row["have_gaussian"] = False
            row["gaussian_bits"] = "0000000000000000"
            return
    raise ProbeError(f"hidden state has no SeedHelper generator {key!r}")


def probe(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, ticks: int) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise ProbeError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
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
        save_fork.request(port, "normalize_reload_locked")
        boundary = save_fork.request(
            port, "authoritative_state_locked")["authoritative"]
        if boundary.get("entities"):
            raise ProbeError("source has pre-existing represented entities")
        x = math.floor(float(boundary["x"])) + 4.5
        y = float(boundary["y"])
        z = math.floor(float(boundary["z"])) + 0.5
        floor_y = math.floor(y) - 1
        blocks = []
        for block_x in range(math.floor(x) - 4, math.floor(x) + 5):
            for block_z in range(math.floor(z) - 4, math.floor(z) + 5):
                blocks.append([block_x, floor_y, block_z, 2, 0])
                for block_y in range(floor_y + 1, floor_y + 5):
                    blocks.append([block_x, block_y, block_z, 0, 0])
        staged = save_fork.request(
            port, "setblocks_locked", {"blocks": blocks})
        if staged.get("set") != len(blocks):
            raise ProbeError(f"could not stage flat trap arena: {staged}")
        horse_eid = 8399
        summoned = save_fork.request(port, "summon_locked", {
            "type": "horse", "horse_kind": "skeleton",
            "x": x, "y": y, "z": z,
            "mx": 0.0, "my": 0.0, "mz": 0.0,
            "yaw": 0.0, "health": 15.0, "no_ai": False,
            "max_health": 15.0,
            "movement_speed": 0.20000000298023224,
            "jump_strength": 0.5,
            "tame": False, "growing_age": 0,
            "trap": True, "trap_time": 123,
            "entity_seed48": 0x23456789ABCD,
            "eid": horse_eid,
            "uuid": "00000000-0000-0000-0000-000000008399",
        })
        if not summoned.get("ok"):
            raise ProbeError(f"could not summon trap horse: {summoned}")

        # Constructor RNG and UUIDs are process globals in vanilla. Pin the
        # exact hidden cursors after staging the original horse, then promote
        # them into the isolated server tick so only the seven AI-created
        # entities consume this fixture's known streams.
        hidden = save_fork.request(port, "hidden_state_locked")
        hidden["next_entity_id"] = 8400
        hidden["math_seed48"] = 0
        hidden["server_uuid_seed48"] = 0x3456789ABCDE
        _pin_generator(hidden, "entity", 0x123456789ABC)
        restored = save_fork.request(
            port, "restore_hidden_state_locked", hidden)
        if restored.get("restored_entities", 0) <= 0:
            raise ProbeError("hidden cursor restore did not cover entities")
        save_fork.request(port, "isolate_server_globals_locked", hidden)

        trace = []
        for _ in range(ticks):
            tick = save_fork.request(port, "server_tick_locked")
            observed = save_fork.request(
                port, "skeleton_trap_observe_locked")
            trace.append({
                "completed_tick": tick["completed"],
                "observed": observed,
            })
        first_observed = trace[0]["observed"]
        horses = [row for row in first_observed["entities"]
                  if row["class"] == "EntitySkeletonHorse"]
        skeletons = [row for row in first_observed["entities"]
                     if row["class"] == "EntitySkeleton"]
        if len(horses) != 4 or len(skeletons) != 4:
            raise ProbeError(
                "trap did not produce four pairs: "
                f"{first_observed['entities']}")
        if len(first_observed["weather_effects"]) != 1 \
                or not first_observed["weather_effects"][0]["effect_only"]:
            raise ProbeError("trap lightning was not one effect-only bolt")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps({
            "schema": "netherite.skeleton_trap_oracle",
            "version": 1,
            "source_version": "Minecraft Java 1.11.2",
            "todo": "ENT-02",
            "fixture": {
                "horse_eid": horse_eid,
                "horse_seed48": 0x23456789ABCD,
                "entity_seed_generator_seed48": 0x123456789ABC,
                "server_uuid_seed48": 0x3456789ABCDE,
                "math_seed48": 0,
                "next_entity_id": 8400,
                "x": x, "y": y, "z": z,
            },
            "completed_tick": tick["completed"],
            "observed": observed,
            "trace": trace,
        }, indent=2, sort_keys=True) + "\n")
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
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--ticks", type=int, default=1)
    args = parser.parse_args()
    if args.ticks <= 0 or args.ticks > 1200:
        parser.error("--ticks must be in 1..1200")
    probe(args.source.resolve(), args.output.resolve(),
          args.instance, args.seed, args.ticks)
    print(f"PASS skeleton-trap Java oracle -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, ProbeError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL skeleton-trap probe: {exc}", file=sys.stderr)
        raise SystemExit(1)
