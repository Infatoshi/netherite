"""Record a live play session, then replay it and compare every frame hash."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
from pathlib import Path

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "hide")
import pygame

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from pygame_recorder import QUIT_KEY, _frame_surface, _pressed_symbols, build_env_action, build_minerl_action
from recording_utils import (
    FULL_CHUNK_SAMPLE_COUNT,
    canonicalize_initial_frame,
    clone_action,
    frame_digest,
    pose_from_debug_state,
    sample_debug_state,
)
from run_config import clone_netherite_config, load_run_config


ROOT = Path(__file__).resolve().parents[1]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        required=True,
        help="Path to a YAML run config, for example config/v1.yaml.",
    )
    return parser.parse_args(argv)


def _update_caption(*, phase: str, seed: int, step: int, total_steps: int, rate: float, mismatches: int, first_mismatch: int | None):
    mismatch_text = "none" if first_mismatch is None else str(first_mismatch)
    pygame.display.set_caption(
        f"Netherite {phase} | seed {seed} | step {step}/{total_steps} | "
        f"{rate:.0f} steps/s | mismatches {mismatches} | first {mismatch_text} | {QUIT_KEY} stop"
    )


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    run = load_run_config(args.config)

    Launcher(ROOT).cleanup_shmem()
    world_dir = ROOT / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        shutil.rmtree(world_dir)

    cfg = clone_netherite_config(run.netherite)
    cfg.instance_id = 0
    cfg.seed = run.seed

    inst = MCInstance(cfg, ROOT)
    inst.start()
    print(f"Launching fresh world with seed={run.seed}, pid={inst.process.pid}", file=sys.stderr)
    if not inst.wait_for_ready(timeout=120.0):
        raise SystemExit("Minecraft client did not become ready")

    env = NetheriteEnv(config=cfg, timeout=run.timeout)
    env.reset()
    try:
        obs = env.wait_for_start_latch()
        initial_state = sample_debug_state(env)
        initial_pose = pose_from_debug_state(initial_state)
        canonical_steps = 0
        env.release_start_latch()
    except TimeoutError:
        obs, initial_pose, canonical_steps = canonicalize_initial_frame(
            env,
            min_loaded_chunks=FULL_CHUNK_SAMPLE_COUNT,
        )
        initial_state = sample_debug_state(env)

    pygame.init()
    display_size = (cfg.width * run.display_scale, cfg.height * run.display_scale)
    screen = pygame.display.set_mode(display_size)

    recorded_actions: list[dict[str, int | list[int]]] = []
    recorded_hashes = [frame_digest(obs["pov"])]
    last_display = time.perf_counter()
    display_interval = 1.0 / run.display_fps
    record_start = last_display
    recording = True

    try:
        while recording and len(recorded_actions) < run.steps:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    recording = False

            pressed, stop_requested = _pressed_symbols()
            if stop_requested:
                recording = False
                continue

            action = build_minerl_action(pressed)
            obs, _, _, _, _ = env.step_sync(build_env_action(action))
            recorded_actions.append(clone_action(action))
            recorded_hashes.append(frame_digest(obs["pov"]))

            now = time.perf_counter()
            if now - last_display >= display_interval:
                screen.blit(_frame_surface(obs["pov"], display_size), (0, 0))
                elapsed = now - record_start
                _update_caption(
                    phase="Record",
                    seed=run.seed,
                    step=len(recorded_actions),
                    total_steps=run.steps,
                    rate=len(recorded_actions) / elapsed if elapsed > 0 else 0.0,
                    mismatches=0,
                    first_mismatch=None,
                )
                pygame.display.flip()
                last_display = now

        recorded_steps = len(recorded_actions)
        if recorded_steps == 0:
            print("No steps were recorded.", file=sys.stderr)
            return

        print(
            f"Recorded {recorded_steps} steps after {canonical_steps} warmup steps. Starting replay.",
            file=sys.stderr,
        )
        env.reset()
        try:
            obs = env.wait_for_start_latch()
            replay_canonical_steps = 0
            env.release_start_latch()
        except TimeoutError:
            obs, _, replay_canonical_steps = canonicalize_initial_frame(
                env,
                target_pose=initial_pose,
                target_world_fingerprint=int(initial_state["world_fingerprint"]),
                target_chunk_mask=int(initial_state["chunk_mask"]),
                min_loaded_chunks=max(int(initial_state["loaded_chunks"]), FULL_CHUNK_SAMPLE_COUNT),
            )
        initial_match = frame_digest(obs["pov"]) == recorded_hashes[0]
        mismatch_count = 0 if initial_match else 1
        first_mismatch = None if initial_match else 0
        replay_start = time.perf_counter()
        last_display = replay_start

        for step, action in enumerate(recorded_actions, start=1):
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    return
            keys = pygame.key.get_pressed()
            if keys[pygame.K_F12]:
                return

            obs, _, _, _, _ = env.step_sync(build_env_action(action))
            if frame_digest(obs["pov"]) != recorded_hashes[step]:
                mismatch_count += 1
                if first_mismatch is None:
                    first_mismatch = step

            now = time.perf_counter()
            if now - last_display >= display_interval:
                screen.blit(_frame_surface(obs["pov"], display_size), (0, 0))
                elapsed = now - replay_start
                _update_caption(
                    phase="Replay",
                    seed=run.seed,
                    step=step,
                    total_steps=recorded_steps,
                    rate=step / elapsed if elapsed > 0 else 0.0,
                    mismatches=mismatch_count,
                    first_mismatch=first_mismatch,
                )
                pygame.display.flip()
                last_display = now

        print(
            f"Replay done. recorded_steps={recorded_steps} initial_match={initial_match} "
            f"canonical_steps={replay_canonical_steps} frame_mismatches={mismatch_count} "
            f"first_mismatch={first_mismatch}",
            file=sys.stderr,
        )
    finally:
        env.close()
        inst.stop()
        pygame.quit()


if __name__ == "__main__":
    main()
