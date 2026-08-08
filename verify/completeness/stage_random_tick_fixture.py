#!/usr/bin/env python3
"""Stage selected/missed natural random-tick save-fork boundaries."""

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


def _signed_i32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >= (1 << 31) else value


def stage(source: pathlib.Path, output: pathlib.Path, instance: int,
          seed: int, missed: bool) -> None:
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
        authoritative = normalized.get("authoritative") or observation
        x = math.floor(float(authoritative["x"])) + 4
        y = math.floor(float(authoritative["y"])) + 2
        z = math.floor(float(authoritative["z"]))

        # Let the setup sand callback drain before arming the saved boundary.
        # The cactus is then reset to age zero so selected/missed differ on the
        # first natural callback, not on a setup artifact.
        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [x, y - 2, z, 1, 0],
                [x, y - 1, z, 12, 0],
                [x, y, z, 81, 0],
                [x - 1, y, z, 0, 0],
                [x + 1, y, z, 0, 0],
                [x, y, z - 1, 0, 0],
                [x, y, z + 1, 0, 0],
                [x, y + 1, z, 0, 0],
            ],
        })
        if not placed.get("ok"):
            raise StageError("could not place the natural random-tick fixture")
        for _ in range(2):
            stepped = save_fork.request(port, "server_tick_locked")
            if not stepped.get("authoritative"):
                raise StageError("could not drain fixture setup callbacks")
        reset = save_fork.request(port, "setblocks_locked", {
            "blocks": [[x, y, z, 81, 0]],
        })
        if not reset.get("ok"):
            raise StageError("could not reset cactus age")

        prepared = save_fork.request(port, "random_selection_locked", {
            "x": x, "y": y, "z": z, "block": 81,
            "seed": 0, "promote": False,
        })
        if not prepared.get("ok") \
                or prepared.get("eligible_sections") != 1 \
                or prepared.get("random_blocks") != 1 \
                or prepared.get("target_promoted") != 0:
            raise StageError(
                f"Java did not isolate the natural selector: {prepared}")
        selected_lcg = prepared["authoritative"]["world_update_lcg"]
        if missed:
            hidden = save_fork.request(port, "hidden_state_locked")
            worlds = [world for world in hidden.get("worlds", [])
                      if world.get("dim") == 0]
            if len(worlds) != 1:
                raise StageError("hidden state has no unique Overworld")
            worlds[0]["update_lcg"] = _signed_i32(
                int(worlds[0]["update_lcg"]) + 1)
            restored = save_fork.request(
                port, "restore_hidden_state_locked", hidden)
            if not restored.get("authoritative"):
                raise StageError("could not arm missed random-tick cursor")
        save_fork.capture_locked(port, output)

        branch = "missed" if missed else "selected"
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save07-natural-random-tick-membership",
            "todo": "SAVE-07",
            "fixture": branch,
            "paired_boundary": {
                "left": "selected", "right": "missed", "same_tick": True,
            },
            "negative_control": {
                "mutation": "increment saved updateLCG by one",
                "expected_path": f"$/blocks/{x},{y},{z}/meta",
                "before": 0, "after": 1,
            },
            "horizons": [1, 20, 200],
            "inputs": [{} for _ in range(200)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "target": [x, y, z],
            "selector": {
                "branch": branch,
                "target_chunk_rank": prepared["target_chunk_rank"],
                "lcg_advances_before": prepared["lcg_advances_before"],
                "selected_update_lcg": selected_lcg,
            },
        }
        (output / "fixture_random_tick.json").write_text(
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
    parser.add_argument("--instance", type=int, default=35)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--missed", action="store_true")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.missed)
    print(f"PASS staged Java random-tick boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage random-tick fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
