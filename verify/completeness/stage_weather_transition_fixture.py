#!/usr/bin/env python3
"""Stage a real 1.11.2 weather interpolation save boundary."""

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
                if observation.get("ok") and "x" in observation \
                        and observation.get("dim") == 0:
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
        staged = save_fork.request(
            port, "stage_weather_transition_locked")
        boundary = staged.get("authoritative") or {}
        expected = {
            "raining": True,
            "thundering": False,
            "rain_time": 80,
            "thunder_time": 100,
            "clean_weather_time": 0,
            "prev_rain_strength": 0.34,
            "rain_strength": 0.35,
            "prev_thunder_strength": 0.15,
            "thunder_strength": 0.14,
        }
        if not staged.get("ok") or any(
                abs(float(boundary.get(key, -9)) - value) > 1.0e-6
                if isinstance(value, float)
                else boundary.get(key) != value
                for key, value in expected.items()):
            raise StageError(
                f"weather interpolation boundary was not retained: {boundary}")
        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save08-weather-interpolation-reload",
            "todo": "SAVE-08",
            "fixture": "weather-transition",
            "paired_boundary": {
                "left": "raining-strength-0.35",
                "right": "cold-reload-reconstructs-strength",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": (
                    "cold-load the persisted raining flag without the "
                    "non-persisted interpolation strength"),
                "expected_path": "$/rain_strength",
                "before": 0.35,
                "after": 1.0,
            },
            "horizons": [1, 2, 4, 8, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "source_weather": {
                key: boundary[key] for key in expected
            },
            "java_reload_semantics": (
                "WorldInfo persists flags and timers but not the four World "
                "interpolation strengths; calculateInitialWeather rebuilds "
                "those strengths from the persisted flags"),
        }
        (output / "fixture_weather_transition.json").write_text(
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
    print(f"PASS staged Java weather boundary -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage weather fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
