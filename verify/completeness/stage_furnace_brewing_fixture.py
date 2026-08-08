#!/usr/bin/env python3
"""Stage active furnace and brewing-stand save boundaries."""

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
sys.path.insert(0, str(ROOT / "magma" / "trace"))
import nbt_codec  # noqa: E402


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


def _water_potion_tag() -> str:
    return nbt_codec.encode_hex({
        "name": "",
        "tag": {"type": "compound", "value": {
            "Potion": {"type": "string", "value": "minecraft:water"},
        }},
    })


def stage(source: pathlib.Path, output: pathlib.Path, instance: int,
          seed: int) -> None:
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
        deadline = time.monotonic() + 120.0
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
        state = normalized.get("authoritative") or observation
        if state.get("loaded_tiles"):
            raise StageError("baseline contains a pre-existing tile entity")
        x = math.floor(float(state["x"])) + 4
        y = math.floor(float(state["y"]))
        z = math.floor(float(state["z"]))
        furnace = [x, y, z - 3]
        brewing = [x, y, z + 3]

        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [*furnace, 62, 2],
                [*brewing, 117, 1],
            ],
        })
        if not placed.get("ok"):
            raise StageError(f"could not place active tile pair: {placed}")

        for slot, item, count, meta in (
                (0, 15, 1, 0), (1, 263, 1, 0)):
            result = save_fork.request(port, "set_container_slot_locked", {
                "x": furnace[0], "y": furnace[1], "z": furnace[2],
                "slot": slot, "item": item, "count": count, "meta": meta,
            })
            if not result.get("ok"):
                raise StageError(f"could not seed furnace slot {slot}: {result}")
        furnace_result = save_fork.request(
            port, "set_container_slot_locked", {
                "x": furnace[0], "y": furnace[1], "z": furnace[2],
                "slot": 2, "item": 0, "count": 0, "meta": 0,
                "burn_time": 5, "current_burn_time": 1600,
                "cook_time": 198, "total_cook_time": 200,
                "custom_name": "Oracle Furnace",
            })
        if not furnace_result.get("ok"):
            raise StageError(f"could not stage furnace progress: {furnace_result}")

        water_tag = _water_potion_tag()
        bottle = save_fork.request(port, "set_container_slot_locked", {
            "x": brewing[0], "y": brewing[1], "z": brewing[2],
            "slot": 0, "item": 373, "count": 1, "meta": 0,
            "nbt": water_tag,
        })
        wart = save_fork.request(port, "set_container_slot_locked", {
            "x": brewing[0], "y": brewing[1], "z": brewing[2],
            "slot": 3, "item": 372, "count": 1, "meta": 0,
        })
        brewing_result = save_fork.request(
            port, "set_container_slot_locked", {
                "x": brewing[0], "y": brewing[1], "z": brewing[2],
                "slot": 4, "item": 0, "count": 0, "meta": 0,
                "brew_time": 2, "fuel": 5, "ingredient_id": 372,
            })
        if not bottle.get("ok") or not wart.get("ok") \
                or not brewing_result.get("ok"):
            raise StageError(
                "could not stage brewing progress: "
                f"{bottle} {wart} {brewing_result}")
        containers = brewing_result.get("authoritative", {}).get(
            "containers", [])
        by_position = {
            (row.get("x"), row.get("y"), row.get("z")): row
            for row in containers
        }
        furnace_state = by_position.get(tuple(furnace), {})
        brewing_state = by_position.get(tuple(brewing), {})
        if any(furnace_state.get(field) != value for field, value in (
                ("burn_time", 5), ("current_burn_time", 1600),
                ("cook_time", 198), ("total_cook_time", 200),
                ("custom_name", "Oracle Furnace"))) \
                or brewing_state.get("brew_time") != 2 \
                or brewing_state.get("fuel") != 5 \
                or brewing_state.get("ingredient_id") != 372:
            raise StageError(
                "Java did not expose exact tile progress: "
                f"furnace={furnace_state} brewing={brewing_state}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-furnace-brewing-mid-progress",
            "todo": "SAVE-08",
            "fixture": "furnace-brewing",
            "paired_boundary": {
                "left": "furnace-cook-198-of-200",
                "right": "brewing-brew-2-with-latched-ingredient",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "advance persisted furnace cook time by one",
                "expected_path": "$/containers/furnace/cook_time",
                "before": 198,
                "after": 199,
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "positions": {"furnace": furnace, "brewing": brewing},
            "java_reload_semantics": {
                "furnace": "persist-and-complete",
                "brewing": "transient-ingredient-clears-and-cancels",
            },
        }
        (output / "fixture_furnace_brewing.json").write_text(
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
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed)
    print(f"PASS staged Java furnace/brewing boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage furnace/brewing fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
