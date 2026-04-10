#!/usr/bin/env python3
"""
Visual sync verification demo.
Shows frame with action overlay proving frame_tick == state_tick at each step.
"""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

import cv2
import numpy as np

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from run_config import load_run_config

ROOT = Path(__file__).resolve().parents[1]


def generate_action_sequence(step: int) -> dict:
    """Generate varied actions to make demo interesting."""
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
        "camera": np.array([0.0, 0.0], dtype=np.float32),
    }

    phase = (step // 25) % 6
    sub = step % 25

    if phase == 0:
        # Pan camera right
        action["camera"] = np.array([5.0, 0.0], dtype=np.float32)
    elif phase == 1:
        # Walk forward
        action["forward"] = 1
        action["camera"] = np.array([1.0, 0.0], dtype=np.float32)
    elif phase == 2:
        # Sprint + occasional jump
        action["forward"] = 1
        action["sprint"] = 1
        if sub % 5 == 0:
            action["jump"] = 1
    elif phase == 3:
        # Strafe left/right
        action["left"] = 1 if sub < 12 else 0
        action["right"] = 0 if sub < 12 else 1
        action["camera"] = np.array([-2.0, 0.0], dtype=np.float32)
    elif phase == 4:
        # Look around while walking
        action["forward"] = 1
        action["camera"] = np.array([3.0 * np.sin(sub * 0.4), 1.0], dtype=np.float32)
    elif phase == 5:
        # Walk backward
        action["back"] = 1
        action["camera"] = np.array([-3.0, -0.5], dtype=np.float32)

    return action


def action_to_keys(action: dict) -> str:
    """Convert action to key string."""
    parts = []
    if action.get("forward"):
        parts.append("W")
    if action.get("back"):
        parts.append("S")
    if action.get("left"):
        parts.append("A")
    if action.get("right"):
        parts.append("D")
    if action.get("jump"):
        parts.append("SPACE")
    if action.get("sprint"):
        parts.append("SPRINT")
    if action.get("attack"):
        parts.append("ATK")

    cam = action.get("camera", [0, 0])
    if isinstance(cam, np.ndarray):
        cam = cam.tolist()
    if abs(cam[0]) > 0.1 or abs(cam[1]) > 0.1:
        parts.append(f"CAM({cam[0]:+.0f},{cam[1]:+.0f})")

    return " ".join(parts) if parts else "(idle)"


def draw_overlay(frame: np.ndarray, step: int, action: dict, info: dict) -> np.ndarray:
    """Draw sync verification overlay on frame."""
    scale = 4
    h, w = frame.shape[:2]
    frame = cv2.resize(frame, (w * scale, h * scale), interpolation=cv2.INTER_NEAREST)
    frame = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

    state_tick = info.get("state_tick", -1)
    frame_tick = info.get("frame_state_tick", -1)
    synced = state_tick == frame_tick

    # Semi-transparent overlay background
    overlay = frame.copy()
    cv2.rectangle(overlay, (0, 0), (340, 130), (0, 0, 0), -1)
    frame = cv2.addWeighted(overlay, 0.65, frame, 0.35, 0)

    font = cv2.FONT_HERSHEY_SIMPLEX
    y = 28

    # Step number
    cv2.putText(
        frame, f"Step: {step}", (12, y), font, 0.7, (255, 255, 255), 1, cv2.LINE_AA
    )
    y += 28

    # State tick
    cv2.putText(
        frame,
        f"State Tick: {state_tick}",
        (12, y),
        font,
        0.6,
        (200, 200, 255),
        1,
        cv2.LINE_AA,
    )
    y += 24

    # Frame tick
    cv2.putText(
        frame,
        f"Frame Tick: {frame_tick}",
        (12, y),
        font,
        0.6,
        (200, 255, 200),
        1,
        cv2.LINE_AA,
    )
    y += 28

    # Sync status
    if synced:
        cv2.putText(frame, "SYNC: OK", (12, y), font, 0.8, (0, 255, 0), 2, cv2.LINE_AA)
    else:
        cv2.putText(
            frame, "SYNC: MISMATCH!", (12, y), font, 0.8, (0, 0, 255), 2, cv2.LINE_AA
        )

    # Action keys at bottom
    keys_text = f"Action: {action_to_keys(action)}"
    cv2.putText(
        frame, keys_text, (12, h * scale - 15), font, 0.5, (255, 255, 0), 1, cv2.LINE_AA
    )

    return frame


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", type=Path, default=ROOT / "config" / "smoke_headless.yaml"
    )
    parser.add_argument("--steps", type=int, default=200)
    parser.add_argument(
        "--output", type=Path, default=ROOT / "recordings" / "sync_demo.mp4"
    )
    parser.add_argument("--fps", type=int, default=20)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    run = load_run_config(args.config)

    cfg = run.netherite
    cfg.instance_id = 0
    cfg.seed = run.seed

    # Clean up
    Launcher(ROOT).cleanup_shmem()
    world_dir = ROOT / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        shutil.rmtree(world_dir)

    # Launch
    inst = MCInstance(cfg, ROOT)
    inst.start()
    print(f"Launching with seed={run.seed}, pid={inst.process.pid}", file=sys.stderr)

    if not inst.wait_for_ready(timeout=120.0):
        raise SystemExit("Minecraft did not become ready")

    env = NetheriteEnv(config=cfg, timeout=run.timeout)
    env.reset()

    try:
        env.wait_for_start_latch()
        env.release_start_latch()
    except TimeoutError as exc:
        print(f"Warning: {exc}", file=sys.stderr)

    # Setup video
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    frame_size = (cfg.width * 4, cfg.height * 4)
    video = cv2.VideoWriter(str(args.output), fourcc, args.fps, frame_size)

    print(f"Recording {args.steps} steps...", file=sys.stderr)

    sync_ok = 0
    sync_fail = 0
    start_time = time.monotonic()

    for step in range(args.steps):
        action = generate_action_sequence(step)
        obs, _, _, _, info = env.step_sync(action)

        state_tick = info.get("state_tick", -1)
        frame_tick = info.get("frame_state_tick", -1)

        if state_tick == frame_tick:
            sync_ok += 1
        else:
            sync_fail += 1
            print(
                f"  SYNC FAIL step={step}: state={state_tick} frame={frame_tick}",
                file=sys.stderr,
            )

        frame = draw_overlay(obs["pov"], step, action, info)
        video.write(frame)

        if (step + 1) % 50 == 0:
            elapsed = time.monotonic() - start_time
            fps = (step + 1) / elapsed
            print(f"  {step + 1}/{args.steps} ({fps:.1f} steps/s)", file=sys.stderr)

    video.release()
    env.close()
    inst.stop()

    elapsed = time.monotonic() - start_time
    print(file=sys.stderr)
    print(f"Done! {args.steps} steps in {elapsed:.1f}s", file=sys.stderr)
    print(f"Sync OK: {sync_ok} | Sync Fail: {sync_fail}", file=sys.stderr)
    print(f"Video: {args.output}", file=sys.stderr)

    if sync_fail > 0:
        print(f"\nFAIL: {sync_fail} sync errors", file=sys.stderr)
        return 1

    print("\nPASS: All frames synced with state ticks!", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
