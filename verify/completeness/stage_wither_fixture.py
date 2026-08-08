#!/usr/bin/env python3
"""Stage persistent Wither and Wither-skull boundaries for ENT-01."""

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
          seed: int, mode: str) -> None:
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
        cleared = save_fork.request(port, "clear_entities_locked")
        if cleared.get("authoritative", {}).get("entities"):
            raise StageError("locked entity clearing left a live entity")
        state = normalized.get("authoritative") or observation
        px = math.floor(float(state["x"]))
        py = math.floor(float(state["y"]))
        pz = math.floor(float(state["z"]))
        active_birth = mode == "invul1"
        target_pig = mode in ("target-pig", "armor-blue", "block-break")
        armor_blue = mode == "armor-blue"
        block_break = mode == "block-break"
        death = mode == "death"
        death_input = mode == "death-input"
        skull_mode = mode.startswith("skull-")
        skull_impact = mode.startswith("skull-block")
        skull_invulnerable = mode.endswith("-invulnerable")
        x = px + (0.5 if death_input else 4.5)
        y = py + (0.0 if death_input else 20.0 if skull_mode else
                  40.0 if active_birth or target_pig else 8.0)
        z = pz + (2.5 if death_input else 0.5)
        blocks = []
        for bx in range(math.floor(x) - 2, math.floor(x) + 3):
            for bz in range(math.floor(z) - 2, math.floor(z) + 3):
                for by in range(py, py + (28 if skull_mode else 14)):
                    blocks.append([bx, by, bz, 0, 0])
        if skull_impact:
            blocks.append([
                math.floor(x) + 1, math.floor(y), math.floor(z), 1, 0])
        save_fork.request(port, "setblocks_locked", {"blocks": blocks})
        if block_break:
            save_fork.request(port, "setblocks_locked", {"blocks": [
                [math.floor(x) - 1, math.floor(y) + 1,
                 math.floor(z) - 1, 1, 0],
                [math.floor(x) + 1, math.floor(y) + 1,
                 math.floor(z) + 1, 7, 0],
            ]})
        if death_input:
            player = save_fork.request(port, "setplayer_locked", {
                "x": px + 0.5, "y": py, "z": pz + 0.5,
                "vx": 0.0, "vy": 0.0, "vz": 0.0,
                "on_ground": True, "fall_distance": 0.0,
                "attack_ticks": 100, "hurt_time": 0,
                "hurt_resistant_time": 0, "death_time": 0,
                "clear_hurt": True, "normalize_move_packets": True,
            })
            if not player.get("ok"):
                raise StageError(
                    f"could not stage lethal player input: {player}")
        cleared = save_fork.request(port, "clear_entities_locked")
        if cleared.get("authoritative", {}).get("entities"):
            raise StageError(
                "post-terrain entity clearing left a live entity")
        if skull_mode:
            summoned = save_fork.request(port, "summon_locked", {
                "type": "wither_skull",
                "x": x, "y": y, "z": z,
                "mx": 1.0 if skull_impact else 0.125,
                "my": 0.0 if skull_impact else 0.25,
                "mz": 0.0 if skull_impact else -0.0625,
                "ax": 0.01, "ay": -0.005, "az": 0.02,
                "invulnerable": skull_invulnerable,
                "life": 3, "ticks_in_air": 3,
                "ticks_existed": 4, "eid": 6602,
                "uuid": "9c4a1dc0-a599-4d35-8000-000000006602",
            })
        else:
            summoned = save_fork.request(port, "summon_locked", {
                "type": "wither",
                "x": x, "y": y, "z": z,
                "mx": 0.0 if active_birth else 0.125,
                "my": 0.0 if active_birth else 0.25,
                "mz": 0.0 if active_birth else -0.0625,
                "health": 0.05 if death_input else 0.5 if death else
                          150.0 if armor_blue else
                          300.0 if target_pig else 100.0,
                "invul_time": 1 if active_birth else 0 \
                    if target_pig or death or death_input else 8,
                "no_ai": not (active_birth or target_pig),
                "no_gravity": True,
                "render_yaw_offset": 7.0, "ticks_existed": 4,
                "entity_seed48": 1234567,
                "eid": 6601,
                "uuid": "9c4a1dc0-a599-4d35-8000-000000006601",
                **({
                    "head_next_0": 5,
                    "head_idle_0": 16,
                    "head_next_1": 1000,
                    "head_idle_1": 0,
                } if armor_blue else {}),
                **({"block_break_counter": 1} if block_break else {}),
                **({"damage_amount": 1.0} if death else {}),
            })
            if death and not summoned.get("damaged_wither"):
                raise StageError(
                    f"Java rejected the lethal Wither fixture: {summoned}")
            if target_pig:
                summoned = save_fork.request(port, "summon_locked", {
                    "type": "pig", "x": x + 4.0, "y": y, "z": z,
                    "health": 10.0, "no_ai": 1,
                    "eid": 6603,
                    "uuid": "9c4a1dc0-a599-4d35-8000-000000006603",
                })
        entities = summoned.get("authoritative", {}).get("entities", [])
        withers = [row for row in entities
                   if row.get("type") == "EntityWither"]
        skulls = [row for row in entities
                  if row.get("type") == "EntityWitherSkull"]
        if skull_mode and (len(skulls) != 1
                or skulls[0].get("wither_skull_exact") is not True):
            raise StageError(
                f"Java did not stage an exact Wither skull: {skulls}")
        if not skull_mode and (len(withers) != 1
                or withers[0].get("wither_exact") is not True):
            raise StageError(
                f"Java did not stage an exact Wither: {withers}")
        save_fork.capture_locked(port, output)
        if skull_mode:
            fixture_id = "ent01-wither-skull-{}-{}".format(
                "block-impact" if skull_impact else "flight",
                "invulnerable" if skull_invulnerable else "normal")
            fixture_name = "wither-skull-{}-{}".format(
                "block-impact" if skull_impact else "flight",
                "invulnerable" if skull_invulnerable else "normal")
            horizons = [1] if skull_impact else [1, 2, 4, 8]
            negative = {
                "mutation": "toggle the persisted Invulnerable flag",
                "expected_path": "$/entities/EntityWitherSkull/Invulnerable",
                "before": skull_invulnerable,
                "after": not skull_invulnerable,
            }
        else:
            fixture_id = ("ent01-wither-player-death" if death_input else
                "ent01-wither-death-save" if death else
                "ent01-wither-block-break-save" if block_break else
                "ent01-wither-armor-blue-save" if armor_blue else
                "ent01-wither-target-pig-save" if target_pig else
                "ent01-wither-invul1-active-save" if active_birth else
                "ent01-wither-noai-invulnerability-save")
            fixture_name = ("wither-player-death" if death_input else
                "wither-death" if death else
                "wither-block-break" if block_break else
                "wither-armor-blue" if armor_blue else
                "wither-target-pig" if target_pig else
                "wither-invul1-active" if active_birth else
                "wither-noai-invulnerability")
            horizons = ([1, 3, 4, 5, 6, 14, 23, 24, 25, 26]
                if death_input else
                [1, 2, 4, 10, 19, 20, 21] if death else
                [1, 2, 4] if block_break else
                [1, 2, 4, 8] if armor_blue else
                [1, 2, 3, 4, 10, 20, 40, 41, 42]
                if target_pig else [1] if active_birth
                else [1, 2, 4, 8, 20])
            negative = ({
                "mutation": "raise the Wither above lethal empty-hand health",
                "expected_path": "$/entities/EntityWither/Health",
                "before": 0.05,
                "after": 1.0,
            } if death_input else {
                "mutation": "restore transient player kill credit",
                "expected_path":
                    "$/entities/EntityWither/recentlyHit",
                "before": 0,
                "after": 100,
            } if death else {
                "mutation": "delay the persisted block-break clock",
                "expected_path":
                    "$/entities/EntityWither/blockBreakCounter",
                "before": 1,
                "after": 2,
            } if block_break else {
                "mutation": "raise persisted health across armor threshold",
                "expected_path": "$/entities/EntityWither/Health",
                "before": 150.0,
                "after": 151.0,
            } if armor_blue else {
                "mutation": "decrement the persisted Invul counter",
                "expected_path": "$/entities/EntityWither/Invul",
                "before": 0 if target_pig else 1 if active_birth else 8,
                "after": 1 if target_pig else 0 if active_birth else 7,
            })
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": fixture_id,
            "todo": "ENT-01",
            "fixture": fixture_name,
            "paired_boundary": {
                "left": "live-memory-wither",
                "right": "cold-anvil-wither",
                "same_tick": True,
            },
            "negative_control": negative,
            "horizons": horizons,
            "inputs": ([
                {"attack": 0, "do_break": 0},
                {"attack": 0, "do_break": 0},
                {"attack": 0, "do_break": 0}, {
                "attack": 1, "do_break": 1,
                "dyaw": -float(state.get("yaw", 0.0)),
                "dpitch": -float(state.get("pitch", 0.0)),
            }, {"attack": 0, "do_break": 0}]
                + [{} for _ in range(max(horizons) - 5)]
                if death_input else
                [{} for _ in range(max(horizons))]),
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order",
                    *(("events",)
                      if active_birth or skull_mode or death or death_input
                      else ()))
            ],
            "source_entities": (["EntityWither", "EntityPig"]
                if target_pig else [
                    "EntityWitherSkull" if skull_mode else "EntityWither",
                    *(["EntityItem"] if death else [])]),
            "java_reload_semantics": fixture_name,
        }
        (output / "fixture_wither.json").write_text(
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
    parser.add_argument(
        "--mode", choices=(
            "noai", "invul1", "target-pig", "armor-blue", "block-break",
            "death", "death-input",
            "skull-flight",
            "skull-flight-invulnerable", "skull-block",
            "skull-block-invulnerable"), default="noai")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.mode)
    print(f"PASS staged Java Wither boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage Wither fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
