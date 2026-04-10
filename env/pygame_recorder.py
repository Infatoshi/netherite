"""Pygame controller that records every step for exact replay."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
from pathlib import Path

import numpy as np

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "hide")
import pygame

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from recording_utils import (
    FULL_CHUNK_SAMPLE_COUNT,
    canonicalize_initial_frame,
    clone_action,
    frame_digest,
    pose_from_debug_state,
    save_frame_sample,
    save_state_sample,
    sample_debug_state,
    write_sample_index,
)
from run_config import config_to_dict, load_run_config


ROOT = Path(__file__).resolve().parents[1]
LOOK_SPEED = 4
QUIT_KEY = "F12"


def build_minerl_action(
    pressed: set[str],
    *,
    look_speed: int = LOOK_SPEED,
) -> dict[str, int | list[int]]:
    dx = 0
    dy = 0
    if "look_left" in pressed:
        dx -= look_speed
    if "look_right" in pressed:
        dx += look_speed
    if "look_up" in pressed:
        dy -= look_speed
    if "look_down" in pressed:
        dy += look_speed

    return {
        "ESC": int("escape" in pressed),
        "attack": int("attack" in pressed),
        "back": int("back" in pressed),
        "camera": [dx, dy],
        "drop": int("drop" in pressed),
        "forward": int("forward" in pressed),
        "hotbar.1": int("hotbar.1" in pressed),
        "hotbar.2": int("hotbar.2" in pressed),
        "hotbar.3": int("hotbar.3" in pressed),
        "hotbar.4": int("hotbar.4" in pressed),
        "hotbar.5": int("hotbar.5" in pressed),
        "hotbar.6": int("hotbar.6" in pressed),
        "hotbar.7": int("hotbar.7" in pressed),
        "hotbar.8": int("hotbar.8" in pressed),
        "hotbar.9": int("hotbar.9" in pressed),
        "inventory": int("inventory" in pressed),
        "jump": int("jump" in pressed),
        "left": int("left" in pressed),
        "pickItem": int("pickItem" in pressed),
        "right": int("right" in pressed),
        "sneak": int("sneak" in pressed),
        "sprint": int("sprint" in pressed),
        "swapHands": int("swapHands" in pressed),
        "use": int("use" in pressed),
    }


def build_env_action(action: dict[str, int | list[int]]) -> dict[str, object]:
    return {
        "forward": action["forward"],
        "back": action["back"],
        "left": action["left"],
        "right": action["right"],
        "jump": action["jump"],
        "sneak": action["sneak"],
        "sprint": action["sprint"],
        "attack": action["attack"],
        "use": action["use"],
        "camera": np.array(action["camera"], dtype=np.float32),
    }


def format_meta_record(
    *,
    seed: int,
    config: dict[str, object],
    initial_frame_hash: str,
    initial_state_tick: int,
    initial_pose: dict[str, float],
    initial_debug_state: dict[str, object],
    initial_world_fingerprint: int,
    initial_chunk_mask: int,
    initial_loaded_chunks: int,
) -> str:
    return json.dumps(
        {
            "type": "meta",
            "version": 7,
            "seed": seed,
            "config": config,
            "initial_frame_hash": initial_frame_hash,
            "initial_state_tick": initial_state_tick,
            "initial_pose": initial_pose,
            "initial_debug_state": initial_debug_state,
            "initial_world_fingerprint": initial_world_fingerprint,
            "initial_chunk_mask": initial_chunk_mask,
            "initial_loaded_chunks": initial_loaded_chunks,
        },
        separators=(",", ":"),
    )


def format_step_record(
    *,
    step: int,
    elapsed: float,
    action: dict[str, int | list[int]],
    frame_hash: str,
    send_state_tick: int,
    state_tick: int,
    debug_state: dict[str, object],
) -> str:
    steps_per_sec = step / elapsed if elapsed > 0 else 0.0
    return json.dumps(
        {
            "type": "step",
            "step": step,
            "steps_per_sec": steps_per_sec,
            "action": action,
            "frame_hash": frame_hash,
            "send_state_tick": send_state_tick,
            "state_tick": state_tick,
            "debug_state": debug_state,
        },
        separators=(",", ":"),
    )


def format_summary_record(*, steps: int, elapsed: float) -> str:
    return json.dumps(
        {
            "type": "summary",
            "steps": steps,
            "duration_sec": elapsed,
            "steps_per_sec": steps / elapsed if elapsed > 0 else 0.0,
        },
        separators=(",", ":"),
    )


def controls_text() -> str:
    return (
        "WASD move | arrows/IJKL look | space jump | shift sneak | ctrl sprint | "
        "mouse1 attack | mouse2 use | mouse3 pick | E inventory | Q drop | "
        "1-9 hotbar | F swapHands | F12 stop"
    )


def _pressed_symbols() -> tuple[set[str], bool]:
    keys = pygame.key.get_pressed()
    mouse = pygame.mouse.get_pressed(3)
    pressed: set[str] = set()

    key_map = (
        (pygame.K_ESCAPE, "escape"),
        (pygame.K_w, "forward"),
        (pygame.K_s, "back"),
        (pygame.K_a, "left"),
        (pygame.K_d, "right"),
        (pygame.K_SPACE, "jump"),
        (pygame.K_LSHIFT, "sneak"),
        (pygame.K_RSHIFT, "sneak"),
        (pygame.K_LCTRL, "sprint"),
        (pygame.K_RCTRL, "sprint"),
        (pygame.K_e, "inventory"),
        (pygame.K_q, "drop"),
        (pygame.K_f, "swapHands"),
        (pygame.K_LEFT, "look_left"),
        (pygame.K_RIGHT, "look_right"),
        (pygame.K_UP, "look_up"),
        (pygame.K_DOWN, "look_down"),
        (pygame.K_j, "look_left"),
        (pygame.K_l, "look_right"),
        (pygame.K_i, "look_up"),
        (pygame.K_k, "look_down"),
        (pygame.K_1, "hotbar.1"),
        (pygame.K_2, "hotbar.2"),
        (pygame.K_3, "hotbar.3"),
        (pygame.K_4, "hotbar.4"),
        (pygame.K_5, "hotbar.5"),
        (pygame.K_6, "hotbar.6"),
        (pygame.K_7, "hotbar.7"),
        (pygame.K_8, "hotbar.8"),
        (pygame.K_9, "hotbar.9"),
    )
    for key_code, symbol in key_map:
        if keys[key_code]:
            pressed.add(symbol)

    if mouse[0]:
        pressed.add("attack")
    if mouse[1]:
        pressed.add("pickItem")
    if mouse[2]:
        pressed.add("use")

    return pressed, bool(keys[pygame.K_F12])


def _frame_surface(frame: np.ndarray, display_size: tuple[int, int]) -> pygame.Surface:
    surface = pygame.surfarray.make_surface(np.transpose(frame, (1, 0, 2)))
    if surface.get_size() != display_size:
        surface = pygame.transform.scale(surface, display_size)
    return surface


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        required=True,
        help="Path to a YAML run config, for example config/v1.yaml.",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="No Pygame window; send idle (empty) actions each step for unattended runs.",
    )
    parser.add_argument(
        "--steps",
        type=int,
        default=None,
        help="Override 'steps' from the YAML config (useful for short smoke tests).",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    run = load_run_config(args.config)
    seed = run.seed
    step_limit = args.steps if args.steps is not None else run.steps
    if step_limit <= 0:
        raise SystemExit("--steps / config steps must be positive")

    Launcher(ROOT).cleanup_shmem()
    world_dir = ROOT / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        shutil.rmtree(world_dir)

    cfg = run.netherite
    cfg.instance_id = 0
    cfg.seed = seed
    sample_root = run.frame_sample_dir
    if sample_root is not None:
        if (sample_root / "record").exists():
            shutil.rmtree(sample_root / "record")
        (sample_root / "record").mkdir(parents=True, exist_ok=True)

    run.recording_path.parent.mkdir(parents=True, exist_ok=True)
    output = run.recording_path.open("w", encoding="utf-8")

    inst = MCInstance(cfg, ROOT)
    inst.start()
    print(
        f"Launching fresh world with seed={seed}, pid={inst.process.pid}",
        file=sys.stderr,
        flush=True,
    )
    if not inst.wait_for_ready(timeout=120.0):
        raise SystemExit("Minecraft client did not become ready")

    env = NetheriteEnv(config=cfg, timeout=run.timeout)
    print("Recorder: env.reset() …", file=sys.stderr, flush=True)
    env.reset()
    print(
        "Recorder: waiting for start latch (game must arm control shmem) …",
        file=sys.stderr,
        flush=True,
    )
    startup_mode = "latched"
    try:
        obs = env.wait_for_start_latch()
        initial_state = sample_debug_state(env)
        initial_pose = pose_from_debug_state(initial_state)
        canonical_steps = 0
    except TimeoutError:
        startup_mode = "canonicalized fallback"
        obs, initial_pose, canonical_steps = canonicalize_initial_frame(
            env,
            min_loaded_chunks=FULL_CHUNK_SAMPLE_COUNT,
        )
        initial_state = sample_debug_state(env)
    else:
        env.release_start_latch()

    if args.headless:
        print(
            "Headless recorder: idle actions only, no Pygame window "
            f"({step_limit} steps).",
            file=sys.stderr,
        )
    else:
        pygame.init()
        display_size = (cfg.width * run.display_scale, cfg.height * run.display_scale)
        screen = pygame.display.set_mode(display_size)
        pygame.display.set_caption(
            f"Netherite Recorder | seed {seed} | {QUIT_KEY} stop"
        )
        print(controls_text(), file=sys.stderr)
    if startup_mode == "latched":
        print("Latched start frame captured.", file=sys.stderr)
    else:
        print(
            f"Canonical start locked after {canonical_steps} warmup steps.",
            file=sys.stderr,
        )
    if sample_root is not None and run.frame_sample_every > 0:
        print(
            f"Saving record samples every {run.frame_sample_every} steps under {sample_root}",
            file=sys.stderr,
        )
    save_frame_sample(
        root_dir=sample_root,
        phase="record",
        step=0,
        frame=obs["pov"],
        every=run.frame_sample_every,
    )
    save_state_sample(
        root_dir=sample_root,
        phase="record",
        step=0,
        state=initial_state,
        every=run.frame_sample_every,
    )
    print(
        format_meta_record(
            seed=seed,
            config=config_to_dict(cfg),
            initial_frame_hash=frame_digest(obs["pov"]),
            initial_state_tick=int(initial_state["state_tick"]),
            initial_pose=initial_pose,
            initial_debug_state=initial_state,
            initial_world_fingerprint=int(initial_state["world_fingerprint"]),
            initial_chunk_mask=int(initial_state["chunk_mask"]),
            initial_loaded_chunks=int(initial_state["loaded_chunks"]),
        ),
        file=output,
        flush=True,
    )

    step = 0
    t0 = time.perf_counter()
    last_display = t0
    display_interval = 1.0 / run.display_fps
    running = True
    noop_env_action = build_env_action(build_minerl_action(set()))
    idle_minerl = build_minerl_action(set())
    progress_every = max(1, min(200, step_limit // 10 or 1))

    try:
        while running and step < step_limit:
            if args.headless:
                action = idle_minerl
            else:
                for event in pygame.event.get():
                    if event.type == pygame.QUIT:
                        running = False

                pressed, stop_requested = _pressed_symbols()
                if stop_requested:
                    running = False
                    continue

                action = build_minerl_action(pressed)

            env_action = noop_env_action if args.headless else build_env_action(action)
            obs, _, _, _, info = env.step_sync(env_action)
            step += 1

            elapsed = time.perf_counter() - t0
            print(
                format_step_record(
                    step=step,
                    elapsed=elapsed,
                    action=clone_action(action),
                    frame_hash=frame_digest(obs["pov"]),
                    send_state_tick=int(info["send_state_tick"]),
                    state_tick=int(info["state_tick"]),
                    debug_state=info["debug_state"],
                ),
                file=output,
                flush=True,
            )
            save_frame_sample(
                root_dir=sample_root,
                phase="record",
                step=step,
                frame=obs["pov"],
                every=run.frame_sample_every,
            )
            save_state_sample(
                root_dir=sample_root,
                phase="record",
                step=step,
                state=info["debug_state"],
                every=run.frame_sample_every,
            )

            if args.headless:
                if step % progress_every == 0 or step == step_limit:
                    rate = step / elapsed if elapsed > 0 else 0
                    print(
                        f"  headless {step}/{step_limit} steps ({rate:.1f} steps/s)",
                        file=sys.stderr,
                    )
            else:
                now = time.perf_counter()
                if now - last_display >= display_interval:
                    screen.blit(_frame_surface(obs["pov"], display_size), (0, 0))
                    pygame.display.set_caption(
                        f"Netherite Recorder | seed {seed} | "
                        f"{step / elapsed:.0f} steps/s | step {step}/{step_limit} | {QUIT_KEY} stop"
                    )
                    pygame.display.flip()
                    last_display = now
    finally:
        env.close()
        inst.stop()
        if not args.headless:
            pygame.quit()
        elapsed = time.perf_counter() - t0
        if elapsed > 0:
            print(
                format_summary_record(steps=step, elapsed=elapsed),
                file=output,
                flush=True,
            )
            print(
                f"Done. {step} steps in {elapsed:.1f}s = {step / elapsed:.1f} steps/s | {run.recording_path}",
                file=sys.stderr,
            )
        output.close()
        index_path = write_sample_index(sample_root)
        if index_path is not None and run.frame_sample_every > 0:
            print(f"Frame sample index: {index_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
