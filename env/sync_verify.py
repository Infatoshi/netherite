#!/usr/bin/env python3
"""Verify that frames and state ticks are properly synchronized.

This test sends actions with camera rotation and verifies:
1. frame_state_tick == state_tick for each step
2. The frame visually changes when we rotate the camera
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

import numpy as np

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from run_config import load_run_config

ROOT = Path(__file__).resolve().parents[1]


def frame_hash(frame: np.ndarray) -> str:
    """Quick hash of frame for change detection."""
    import hashlib

    return hashlib.blake2b(frame.tobytes(), digest_size=8).hexdigest()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "config" / "smoke_headless.yaml",
        help="Path to a YAML run config.",
    )
    parser.add_argument(
        "--steps",
        type=int,
        default=100,
        help="Number of steps to test.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    run = load_run_config(args.config)

    Launcher(ROOT).cleanup_shmem()
    world_dir = ROOT / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        shutil.rmtree(world_dir)

    cfg = run.netherite
    cfg.instance_id = 0
    cfg.seed = run.seed

    inst = MCInstance(cfg, ROOT)
    inst.start()
    print(f"Launching with seed={run.seed}, pid={inst.process.pid}", file=sys.stderr)
    if not inst.wait_for_ready(timeout=120.0):
        raise SystemExit("Minecraft client did not become ready")

    env = NetheriteEnv(config=cfg, timeout=run.timeout)
    env.reset()

    try:
        env.wait_for_start_latch()
        env.release_start_latch()
    except TimeoutError as exc:
        print(f"Warning: {exc}", file=sys.stderr)

    print(f"Running {args.steps} sync verification steps...", file=sys.stderr)

    sync_errors = 0
    frame_changes = 0
    last_hash = None
    camera_actions = [
        {"camera": np.array([5.0, 0.0], dtype=np.float32)},  # rotate right
        {"camera": np.array([-5.0, 0.0], dtype=np.float32)},  # rotate left
    ]

    tick_history = []
    for step in range(args.steps):
        action = {
            "forward": 0,
            "back": 0,
            "left": 0,
            "right": 0,
            "jump": 0,
            "sneak": 0,
            "sprint": 0,
            "attack": 0,
            "use": 0,
            **camera_actions[step % 2],
        }

        obs, _, _, _, info = env.step_sync(action)
        send_tick = info["send_state_tick"]
        state_tick = info["state_tick"]
        frame_tick = info["frame_state_tick"]
        tick_delta = state_tick - send_tick

        tick_history.append((step, send_tick, state_tick, frame_tick, tick_delta))

        current_hash = frame_hash(obs["pov"])
        if current_hash != last_hash:
            frame_changes += 1
        last_hash = current_hash

        if frame_tick != state_tick:
            sync_errors += 1
            print(
                f"  SYNC ERROR step={step}: send={send_tick} state={state_tick} "
                f"frame={frame_tick} delta={tick_delta}",
                file=sys.stderr,
            )
        elif step % 20 == 0:
            print(
                f"  step={step}: send={send_tick} state={state_tick} "
                f"frame={frame_tick} delta={tick_delta} OK",
                file=sys.stderr,
            )

    env.close()
    inst.stop()

    print(file=sys.stderr)
    print("Results:", file=sys.stderr)
    print(f"  Steps: {args.steps}", file=sys.stderr)
    print(f"  Sync errors: {sync_errors}", file=sys.stderr)
    print(f"  Frame changes: {frame_changes}", file=sys.stderr)

    if sync_errors > 0:
        print(f"\nFAIL: {sync_errors} sync errors detected", file=sys.stderr)
        raise SystemExit(1)

    if frame_changes < args.steps // 2:
        print(
            f"\nWARNING: Only {frame_changes} frame changes - "
            "camera rotation may not be working",
            file=sys.stderr,
        )

    print("\nPASS: All frames synced with state ticks", file=sys.stderr)


if __name__ == "__main__":
    main()
