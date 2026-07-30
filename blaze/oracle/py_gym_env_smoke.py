#!/usr/bin/env python3
"""Schema + determinism smoke for py_gym_env_smoke.

Runs the CPU binary twice (must match), validates obs schema via pybind11 module
when built, else validates schema fields from a ctypes replay of core logic via
subprocess hash lines only.

  uv run --no-project python oracle/py_gym_env_smoke.py [seed]
"""
from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEED = 12345
N_EXPECTED_LINES = 17  # reset obs + 16 steps

REQUIRED_OBS_KEYS = frozenset({
    "x", "y", "z", "yaw", "pitch",
    "vx", "vy", "vz", "on_ground", "look", "tick", "combined_hash",
})

LOOK_KEYS = frozenset({"type", "bx", "by", "bz"})


def build_cpu(tmp: str) -> str:
    out = os.path.join(tmp, "py_gym_env_smoke_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-o", out,
         os.path.join(ROOT, "cpu", "py_gym_env_smoke.c"), "-lm"],
        check=True,
    )
    return out


def run_binary(path: str, seed: int) -> list[str]:
    p = subprocess.run([path, str(seed)], capture_output=True, text=True, check=True)
    return p.stdout.splitlines()


def check_determinism(lines_a: list[str], lines_b: list[str]) -> None:
    if len(lines_a) != N_EXPECTED_LINES:
        raise SystemExit(f"FAIL schema: expected {N_EXPECTED_LINES} hash lines, got {len(lines_a)}")
    if lines_a != lines_b:
        for i, (a, b) in enumerate(zip(lines_a, lines_b)):
            if a != b:
                raise SystemExit(f"FAIL determinism: line {i}: {a!r} vs {b!r}")
    print(f"  ok  determinism replay ({len(lines_a)} obs hashes)")


def try_import_mcsim() -> object | None:
    mod_path = os.path.join(ROOT, "py", "build", "mcsim_gym.cpython-312-x86_64-linux-gnu.so")
    if not os.path.isfile(mod_path):
        # glob any cpython version
        build_dir = os.path.join(ROOT, "py", "build")
        if os.path.isdir(build_dir):
            for name in os.listdir(build_dir):
                if name.startswith("mcsim_gym.") and name.endswith(".so"):
                    mod_path = os.path.join(build_dir, name)
                    break
        else:
            return None
    if not os.path.isfile(mod_path):
        return None
    spec = importlib.util.spec_from_file_location("mcsim_gym", mod_path)
    if spec is None or spec.loader is None:
        return None
    try:
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    except ImportError:
        return None


def check_schema_pybind(mod: object, seed: int) -> None:
    env = mod.McSimEnv()
    obs = env.reset(seed)
    _validate_obs_dict(obs, label="reset")

    actions = mod.replay_actions()
    for i, act in enumerate(actions):
        obs, reward, done, info = env.step(act)
        _validate_obs_dict(obs, label=f"step[{i}]")
        if not isinstance(reward, float):
            raise SystemExit(f"FAIL schema: step[{i}] reward type {type(reward)}")
        if not isinstance(done, bool):
            raise SystemExit(f"FAIL schema: step[{i}] done type {type(done)}")
        if not isinstance(info, dict):
            raise SystemExit(f"FAIL schema: step[{i}] info type {type(info)}")
    print(f"  ok  pybind11 obs schema ({len(actions)} steps)")


def _validate_obs_dict(obs: dict, label: str) -> None:
    missing = REQUIRED_OBS_KEYS - obs.keys()
    if missing:
        raise SystemExit(f"FAIL schema {label}: missing keys {sorted(missing)}")
    look = obs.get("look")
    if not isinstance(look, dict):
        raise SystemExit(f"FAIL schema {label}: look not dict")
    missing_look = LOOK_KEYS - look.keys()
    if missing_look:
        raise SystemExit(f"FAIL schema {label}: look missing {sorted(missing_look)}")


def main() -> None:
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else SEED
    with tempfile.TemporaryDirectory() as tmp:
        cpu = build_cpu(tmp)
        run1 = run_binary(cpu, seed)
        run2 = run_binary(cpu, seed)
    check_determinism(run1, run2)

    mod = try_import_mcsim()
    if mod is not None:
        check_schema_pybind(mod, seed)
    else:
        print("  skip pybind11 schema (module not built; see py/README.md)")

    print("PASS py_gym_env_smoke oracle")


if __name__ == "__main__":
    main()
