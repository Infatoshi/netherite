#!/usr/bin/env python3
"""Stage a real-Java player active-use save boundary."""

from __future__ import annotations

import argparse
import json
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
          seed: int, kind: str) -> None:
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
        save_fork.request(port, "normalize_reload_locked")

        if kind == "eat":
            staged = save_fork.request(port, "setplayer_locked", {
                "food": 10,
                "inventory_slot": 0, "inventory_item": 260,
                "inventory_count": 1, "inventory_meta": 0,
            })
            remaining = 22
            expected_action = 1
        else:
            staged = save_fork.request(port, "setplayer_locked", {
                "inventory_slot": 0, "inventory_item": 261,
                "inventory_count": 1, "inventory_meta": 0,
            })
            save_fork.request(port, "setplayer_locked", {
                "inventory_slot": 1, "inventory_item": 262,
                "inventory_count": 2, "inventory_meta": 0,
            })
            remaining = 71993
            expected_action = 4
        if not staged.get("ok"):
            raise StageError(f"could not stage {kind} inventory")
        armed = save_fork.request(port, "setplayer_locked", {
            "active_hand": 0, "active_use_remaining": remaining,
        })
        state = armed.get("authoritative") or {}
        if not armed.get("ok") or state.get("hand_active") is not True \
                or state.get("active_hand") != 0 \
                or state.get("active_use_remaining") != remaining \
                or state.get("active_use_action") != expected_action:
            raise StageError(f"Java did not retain the staged {kind} use: {state}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": f"save08-player-{kind}-reload-cancel",
            "todo": "SAVE-08",
            "fixture": kind,
            "paired_boundary": {
                "left": f"mid-{kind}", "right": "cold-reload",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "reload the active player from vanilla NBT",
                "expected_path": "$/hand_active",
                "before": True, "after": False,
            },
            "horizons": [1, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source_active_use": {
                "hand": 0, "remaining": remaining,
                "elapsed": 32 - remaining if kind == "eat"
                    else 72000 - remaining,
                "action": expected_action,
            },
            "java_reload_semantics": "cancel",
        }
        (output / f"fixture_player_{kind}.json").write_text(
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
    parser.add_argument("--kind", choices=("eat", "bow"), required=True)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.kind)
    print(f"PASS staged real Java mid-{args.kind} boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage player-use fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
