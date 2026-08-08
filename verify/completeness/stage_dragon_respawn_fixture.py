#!/usr/bin/env python3
"""Stage a real 1.11.2 End-dragon respawn at a save boundary."""

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


def _wait_player(port: int, dimension: int, timeout: float = 180.0) -> dict:
    deadline = time.monotonic() + timeout
    while True:
        try:
            observation = save_fork.request(port, "obs")
            if observation.get("ok") and "x" in observation \
                    and observation.get("dim") == dimension \
                    and (observation.get("authoritative") or {}).get("dim") \
                        == dimension:
                return observation
        except save_fork.SaveForkError:
            pass
        if time.monotonic() >= deadline:
            raise StageError(
                f"Java player did not settle in dimension {dimension}")
        time.sleep(0.1)


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
        _wait_player(port, 0)
        # Pool instances boot with the server parked. Let its scheduled-task
        # queue run so the asynchronous dimension-transfer command can execute.
        save_fork.request(port, "server_step_unlock")
        changed = save_fork.request(port, "dim", {"id": 1})
        if not changed.get("ok"):
            raise StageError(f"End transfer was rejected: {changed}")
        _wait_player(port, 1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        save_fork.request(port, "normalize_reload_locked")
        parked = save_fork.request(
            port, "authoritative_state_locked").get("authoritative") or {}
        if parked.get("dim") != 1:
            raise StageError(
                f"parked server player is not in the End: {parked}")
        staged = save_fork.request(
            port, "stage_dragon_respawn_locked", {"first_eid": 5801})
        state = staged.get("authoritative") or {}
        if not staged.get("ok") or state.get("dragon_respawn_state") != 0 \
                or state.get("dragon_respawn_ticks") != 0:
            raise StageError(f"dragon ritual did not start: {staged}")
        for _ in range(20):
            tick = save_fork.request(port, "server_tick_locked")
            if not tick.get("ok"):
                raise StageError(f"dragon ritual tick failed: {tick}")
        pruned = save_fork.request(port, "prune_dragon_respawn_locked", {
            "first_eid": 5801, "count": 4,
        })
        boundary = pruned.get("authoritative") or {}
        crystals = [entity for entity in boundary.get("entities", [])
                    if entity.get("type") == "EntityEnderCrystal"]
        if boundary.get("dragon_respawn_state") != 1 \
                or boundary.get("dragon_respawn_ticks") != 19 \
                or len(crystals) != 4 \
                or any(entity.get("has_beam") != 1 for entity in crystals):
            raise StageError(
                "real ritual did not reach the expected PREPARING boundary: "
                f"state={boundary.get('dragon_respawn_state')}/"
                f"{boundary.get('dragon_respawn_ticks')} crystals={crystals}")
        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-end-dragon-mid-respawn",
            "todo": "SAVE-08",
            "fixture": "dragon-ritual",
            "paired_boundary": {
                "left": "preparing-to-summon-pillars-tick-19",
                "right": "cold-reload-cancels-unsaved-fight-phase",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "retain the unsaved respawn phase and phase tick",
                "expected_path": "$/dragon_respawn_state",
                "before": 1,
                "after": -1,
            },
            # Keep this fixture on the cancelled ritual boundary itself. The
            # later cold DragonFightManager player rescan/new-dragon lifecycle
            # is the distinct arbitrary-fight-state work owned by DIM-03.
            "horizons": [1, 2, 4, 8],
            "inputs": [{} for _ in range(8)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source": {
                "dragon_respawn_state": boundary["dragon_respawn_state"],
                "dragon_respawn_ticks": boundary["dragon_respawn_ticks"],
                "crystal_eids": [entity["eid"] for entity in crystals],
            },
            "java_reload_semantics": (
                "DragonFightManager.getCompound omits the active respawn "
                "phase and phase tick in 1.11.2, cancelling the ritual while "
                "the four End-crystal entities remain persisted"),
        }
        (output / "fixture_dragon_respawn.json").write_text(
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
    print(f"PASS staged Java dragon-respawn boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage dragon respawn fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
