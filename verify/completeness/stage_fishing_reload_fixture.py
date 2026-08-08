#!/usr/bin/env python3
"""Stage Java's deliberately non-persistent live fishing-hook boundary."""

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
        player = normalized.get("authoritative") or {}
        x = math.floor(float(player.get("x", observation["x"]))) + 4.5
        y = math.floor(float(player.get("y", observation["y"]))) + 2.0
        z = math.floor(float(player.get("z", observation["z"]))) + 0.5
        inventory = save_fork.request(port, "setplayer_locked", {
            "inventory_slot": 0, "inventory_item": 346,
            "inventory_count": 1, "inventory_meta": 0,
        })
        if not inventory.get("ok"):
            raise StageError("could not stage the fishing rod")
        summoned = save_fork.request(port, "summon_locked", {
            "type": "fish_hook", "x": x, "y": y, "z": z,
            "mx": 0.125, "my": 0.0, "mz": -0.0625,
            "eid": 5301,
            "uuid": "9c4a1dc0-a599-4d35-8000-000000005301",
        })
        state = summoned.get("authoritative") or {}
        hooks = [row for row in state.get("entities", [])
                 if row.get("type") == "EntityFishHook"]
        if not summoned.get("ok") or len(hooks) != 1:
            raise StageError(f"Java did not stage one live hook: {hooks}")
        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-fishing-hook-reload-drop",
            "todo": "SAVE-08",
            "fixture": "live-fishing-hook",
            "paired_boundary": {
                "left": "live-hook", "right": "cold-reload",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "reload the save containing EntityFishHook",
                "expected_path": "$/entities/EntityFishHook",
                "before": 1, "after": 0,
            },
            "horizons": [1, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source_hook_eid": 5301,
            "java_reload_semantics": "drop",
        }
        (output / "fixture_fishing_hook.json").write_text(
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
    print(f"PASS staged real Java fishing-hook boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage fishing-hook fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
