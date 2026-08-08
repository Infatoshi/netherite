#!/usr/bin/env python3
"""Stage a rich Armor Stand at a real 1.11.2 save boundary."""

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


class StageError(RuntimeError):
    pass


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _only_stand(response: dict, label: str) -> dict:
    entities = (response.get("authoritative") or {}).get("entities", [])
    stands = [row for row in entities
              if row.get("type") == "EntityArmorStand"]
    if len(stands) != 1 or stands[0].get("armor_stand_exact") is not True:
        raise StageError(f"{label} did not expose one exact Armor Stand: {stands}")
    return stands[0]


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
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
        # Loaded hosts can spend several minutes rebuilding the integrated
        # server's spawn area even after the qrl socket starts accepting.
        deadline = time.monotonic() + 300.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if observation.get("ok") and "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise StageError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        save_fork.request(port, "clear_entities_locked")
        state = normalized.get("authoritative") or observation
        px = math.floor(float(state["x"]))
        py = math.floor(float(state["y"]))
        pz = math.floor(float(state["z"]))
        x, y, z = px + 4.5, py + 4.0, pz + 0.5
        blocks = []
        for bx in range(px + 2, px + 7):
            for bz in range(pz - 2, pz + 3):
                for by in range(py, py + 8):
                    blocks.append([bx, by, bz, 0, 0])
        save_fork.request(port, "setblocks_locked", {"blocks": blocks})
        save_fork.request(port, "clear_entities_locked")

        pose = [
            [11.25, -22.5, 33.75],
            [-4.5, 5.25, -6.75],
            [-35.0, 15.0, -20.0],
            [40.0, -10.0, 25.0],
            [-12.0, 3.0, -8.0],
            [14.0, -5.0, 9.0],
        ]
        summoned = save_fork.request(port, "summon_locked", {
            "type": "armor_stand",
            "x": x, "y": y, "z": z,
            "mx": 0.125, "my": -0.0625, "mz": 0.03125,
            "health": 12.5, "air": 300, "fall_distance": 0.5,
            "max_health": 24.0, "absorption": 2.5,
            "revenge_timer": 31, "portal_cooldown": 7,
            "custom_name": "Sentinel", "custom_name_visible": True,
            "silent": True, "glowing": True, "invulnerable": True,
            "update_blocked": False, "fall_flying": False,
            "tags": ["guard", "west"],
            "effects": [
                {"id": 10, "amp": 1, "dur": 80,
                 "ambient": False, "show_particles": True},
                {"id": 21, "amp": 1, "dur": 80,
                 "ambient": False, "show_particles": True},
            ],
            "on_ground": True, "small": True, "show_arms": True,
            "no_base_plate": True, "marker": False,
            "invisible": False, "no_gravity": True,
            "disabled_slots": 0x10204,
            "pose": pose,
            "punch_cooldown": 29, "last_damage": 0.375,
            "ticks_existed": 37,
            "entity_seed48": 0x123456789ABC,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.375,
            "equipment": [
                {"slot": 0, "item": 280, "count": 1, "meta": 0},
                {"slot": 1, "item": 442, "count": 1, "meta": 0},
                {"slot": 2, "item": 313, "count": 1, "meta": 7},
                {"slot": 3, "item": 312, "count": 1, "meta": 3},
                {"slot": 4, "item": 299, "count": 1, "meta": 11},
                {"slot": 5, "item": 86, "count": 1, "meta": 0},
            ],
            "eid": 6901,
            "uuid": "9c4a1dc0-a599-4d35-8000-000000006901",
        })
        before = _only_stand(summoned, "live fixture")
        if before.get("armor_stand_ticks_existed") != 37 \
                or before.get("armor_stand_punch_cooldown") != 29 \
                or before.get("armor_stand_last_damage") != 0.375:
            raise StageError(f"Armor Stand transient staging failed: {before}")

        roundtripped = save_fork.request(
            port, "entity_nbt_roundtrip_locked")
        if roundtripped.get("count") != 1:
            raise StageError(
                f"Armor Stand NBT roundtrip count changed: {roundtripped}")
        after = _only_stand(roundtripped, "NBT roundtrip")
        persistent = (
            "eid", "type", "uuid_most", "uuid_least",
            "x", "y", "z", "vx", "vy", "vz", "yaw", "pitch",
            "health", "armor_stand_small", "armor_stand_show_arms",
            "armor_stand_no_base_plate", "armor_stand_marker",
            "armor_stand_no_gravity", "armor_stand_invisible",
            "armor_stand_disabled_slots", "armor_stand_air",
            "armor_stand_on_ground", "armor_stand_fall_distance",
            "armor_stand_absorption", "armor_stand_max_health",
            "armor_stand_max_health_base",
            "armor_stand_revenge_timer", "armor_stand_portal_cooldown",
            "armor_stand_custom_name",
            "armor_stand_custom_name_visible", "armor_stand_silent",
            "armor_stand_glowing", "armor_stand_invulnerable",
            "armor_stand_update_blocked", "armor_stand_fall_flying",
            "armor_stand_tags", "armor_stand_effects",
            "armor_stand_equipment", "armor_stand_pose",
        )
        changed = [key for key in persistent if before.get(key) != after.get(key)]
        if changed:
            raise StageError(
                f"Armor Stand NBT roundtrip lost persistent fields: {changed}")
        if after.get("armor_stand_ticks_existed") != 0 \
                or after.get("armor_stand_punch_cooldown") != 0 \
                or after.get("armor_stand_last_damage") != 0.0:
            raise StageError(
                f"Armor Stand NBT reset semantics changed: {after}")

        save_fork.capture_locked(port, output)
        horizons = [1, 2, 3, 4, 8, 20]
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "ent04-armor-stand-save",
            "todo": "ENT-04",
            "fixture": "armor-stand-save",
            "paired_boundary": {
                "left": "live-memory-armor-stand",
                "right": "cold-anvil-armor-stand",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "change the saved right-arm X rotation",
                "expected_path":
                    "$/entities/EntityArmorStand/Pose/RightArm[0]",
                "before": 40.0,
                "after": 41.0,
            },
            "horizons": horizons,
            "inputs": [{} for _ in range(max(horizons))],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues",
                    "order", "events")
            ],
            "source_entities": ["EntityArmorStand"],
            "java_reload_semantics": "armor-stand-save",
        }
        (output / "fixture_armor_stand.json").write_text(
            json.dumps(contract, indent=2, sort_keys=True) + "\n")
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
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(),
          args.instance, args.seed)
    print(f"PASS staged Java Armor Stand boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage Armor Stand fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
