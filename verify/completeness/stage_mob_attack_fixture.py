#!/usr/bin/env python3
"""Stage a real iron-golem attack at a vanilla save boundary."""

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


def _entity(state: dict, eid: int) -> dict:
    return next(row for row in state.get("entities", [])
                if row.get("eid") == eid)


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
        if state.get("entities"):
            raise StageError("baseline contains a pre-existing entity")

        px = math.floor(float(state["x"])) + 0.5
        py = math.floor(float(state["y"]))
        pz = math.floor(float(state["z"])) + 0.5
        blocks = []
        for z in range(math.floor(pz) - 5, math.floor(pz) + 6):
            for x in range(math.floor(px) - 5, math.floor(px) + 6):
                blocks.append([x, py - 1, z, 1, 0])
                for y in range(py, py + 5):
                    blocks.append([x, y, z, 0, 0])
        prepared = save_fork.request(
            port, "setblocks_locked", {"blocks": blocks})
        player = save_fork.request(port, "setplayer_locked", {
            "x": px, "y": py, "z": pz,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "on_ground": True, "fall_distance": 0.0,
            "attack_ticks": 0, "hurt_time": 0,
            "hurt_resistant_time": 0, "death_time": 0,
            "clear_hurt": True, "normalize_move_packets": True,
        })
        if not prepared.get("ok") or not player.get("ok"):
            raise StageError(
                f"could not prepare attack arena: {prepared} {player}")

        eid = 5701
        identity = "9c4a1dc0-a599-4d35-8000-000000005701"
        attacked = save_fork.request(port, "summon_locked", {
            "type": "no_ai_mob", "entity_type": 57,
            "player_created": True, "entity_seed48": 0x0FEDCBA98765,
            "ticks_existed": 2, "world_time": 6000,
            "x": px - 2.0, "y": py, "z": pz,
            "mx": 0.0, "my": 0.0, "mz": 0.0,
            "eid": eid, "uuid": identity,
            "attack_player": True,
        })
        attack_boundary = attacked.get("authoritative") or {}
        attacker = _entity(attack_boundary, eid)
        if not attacked.get("ok") or not attacked.get("attacked_player") \
                or attack_boundary.get("health", 20.0) >= 20.0 \
                or attack_boundary.get("hurt_time", 0) <= 0 \
                or attacker.get("golem_attack_timer") != 10:
            raise StageError(
                "real golem attack did not produce damage/timers: "
                f"attack={attacked} attacker={attacker}")
        # Preserve the attack result but isolate the subsequent NoAI cold
        # continuation from entity collision, which is covered separately.
        moved = save_fork.request(port, "set_entity_position_locked", {
            "eid": eid, "x": px + 6.0, "y": py, "z": pz,
        })
        boundary = moved.get("authoritative") or {}
        attacker = _entity(boundary, eid)
        if not moved.get("ok") or attacker.get("golem_attack_timer") != 10:
            raise StageError(f"could not isolate attacked golem: {moved}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-iron-golem-mid-attack",
            "todo": "SAVE-08",
            "fixture": "mob-attack",
            "paired_boundary": {
                "left": "golem-attack-timer-10",
                "right": "cold-reload-transients-reset",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "retain the unsaved attack animation timer",
                "expected_path": "$/entities/EntityIronGolem/attack_timer",
                "before": 10,
                "after": 0,
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source": {
                "player_health": boundary["health"],
                "player_hurt_time": boundary["hurt_time"],
                "player_motion": [boundary["vx"], boundary["vy"],
                                  boundary["vz"]],
                "golem_attack_timer": attacker["golem_attack_timer"],
            },
            "java_reload_semantics": (
                "persist player damage/motion; reset hurt and attack timers"),
        }
        (output / "fixture_mob_attack.json").write_text(
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
    print(f"PASS staged Java golem attack boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage mob attack fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
