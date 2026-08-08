#!/usr/bin/env python3
"""Stage a real Java save while a powered piston is moving."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import struct
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
        player = normalized.get("authoritative") or observation
        x = math.floor(float(player["x"])) + 4
        y = math.floor(float(player["y"])) + 2
        z = math.floor(float(player["z"]))

        # Match the existing moving-piston specialist fixture: a stable
        # south-side redstone block powers an east-facing normal piston, with
        # one stone in front and air at the destination. Place the piston only
        # at the controlled tick boundary so setup cannot consume its motion.
        prepared = save_fork.request(port, "setblocks_locked", {
            "blocks": [
                [x, y - 1, z, 1, 0],
                [x, y, z, 0, 0],
                [x, y, z + 1, 152, 0],
                [x + 1, y, z, 1, 0],
                [x + 2, y, z, 0, 0],
                [x, y + 1, z, 0, 0],
                [x + 1, y + 1, z, 0, 0],
                [x + 2, y + 1, z, 0, 0],
            ],
        })
        if not prepared.get("ok"):
            raise StageError(f"could not prepare piston fixture: {prepared}")
        queued = save_fork.request(port, "setblock_tick_locked", {
            "x": x, "y": y, "z": z, "block": 33, "meta": 5,
        })
        if queued.get("queued") is not True:
            raise StageError(f"could not queue piston placement: {queued}")
        moved = save_fork.request(port, "server_tick_locked")
        state = moved.get("authoritative") or {}
        pistons = state.get("moving_pistons") or []
        expected_positions = [[x + 1, y, z], [x + 2, y, z]]
        actual_positions = sorted(
            [row["x"], row["y"], row["z"]] for row in pistons)
        if state.get("moving_pistons_complete") is not True \
                or len(pistons) != 2 \
                or actual_positions != expected_positions:
            raise StageError(
                "Java did not stage the two moving-piston tiles: "
                f"complete={state.get('moving_pistons_complete')} "
                f"tiles={pistons}")

        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-moving-piston-mid-extension",
            "todo": "SAVE-08",
            "fixture": "moving-piston",
            "paired_boundary": {
                "left": "moving-head", "right": "moving-stone",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "advance one moving tile by half a tick",
                "expected_path": "$/moving_pistons/0/progress_bits",
                "before": pistons[0]["progress_bits"],
                "after": struct.unpack(
                    "<I", struct.pack("<f", min(
                        1.0, struct.unpack(
                            "<f", struct.pack(
                                "<I", pistons[0]["progress_bits"]))[0] + 0.5
                    )))[0],
            },
            "horizons": [1, 2, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source_positions": actual_positions,
            "java_reload_semantics": "persist-and-complete",
        }
        (output / "fixture_moving_piston.json").write_text(
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
    print(f"PASS staged Java moving-piston boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage moving-piston fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
