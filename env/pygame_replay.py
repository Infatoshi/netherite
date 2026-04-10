"""Replay a recorded action log and compare frame hashes on every step."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
from dataclasses import dataclass
from pathlib import Path

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "hide")
import pygame

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from pygame_recorder import QUIT_KEY, _frame_surface, build_env_action
from recording_utils import (
    FULL_CHUNK_SAMPLE_COUNT,
    canonicalize_initial_frame,
    frame_digest,
    pose_from_debug_state,
    save_frame_sample,
    save_state_sample,
    sample_debug_state,
    write_sample_index,
)
from run_config import clone_netherite_config, config_to_dict, load_run_config


ROOT = Path(__file__).resolve().parents[1]


@dataclass
class RecordingEvent:
    step: int
    action: dict[str, int | list[int]]
    frame_hash: str
    send_state_tick: int | None
    state_tick: int | None
    debug_state: dict[str, object] | None


@dataclass
class Recording:
    seed: int
    config: dict[str, object]
    initial_frame_hash: str
    initial_state_tick: int | None
    initial_pose: dict[str, float] | None
    initial_debug_state: dict[str, object] | None
    initial_world_fingerprint: int | None
    initial_chunk_mask: int | None
    initial_loaded_chunks: int | None
    events: list[RecordingEvent]
    total_steps: int
    duration_sec: float


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
        help="Verify frames without opening a pygame window.",
    )
    return parser.parse_args(argv)


def load_recording(path: Path) -> Recording:
    meta: dict[str, object] | None = None
    summary: dict[str, object] | None = None
    events: list[RecordingEvent] = []

    with path.open("r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line:
                continue
            record = json.loads(line)
            kind = record.get("type")
            if kind == "meta":
                meta = record
                continue
            if kind == "step":
                events.append(
                    RecordingEvent(
                        step=int(record["step"]),
                        action=record["action"],
                        frame_hash=str(record["frame_hash"]),
                        send_state_tick=(
                            int(record["send_state_tick"])
                            if record.get("send_state_tick") is not None
                            else None
                        ),
                        state_tick=(
                            int(record["state_tick"])
                            if record.get("state_tick") is not None
                            else None
                        ),
                        debug_state=(
                            dict(record["debug_state"])
                            if record.get("debug_state") is not None
                            else None
                        ),
                    )
                )
                continue
            if kind == "summary":
                summary = record
                continue
            raise ValueError(f"Unknown recording record type: {kind}")

    if meta is None:
        raise ValueError("Recording is missing meta record")
    if summary is None:
        raise ValueError("Recording is missing summary record")
    if len(events) != int(summary["steps"]):
        raise ValueError("Recording step count does not match summary")

    return Recording(
        seed=int(meta["seed"]),
        config=dict(meta["config"]),
        initial_frame_hash=str(meta["initial_frame_hash"]),
        initial_state_tick=(
            int(meta["initial_state_tick"])
            if meta.get("initial_state_tick") is not None
            else None
        ),
        initial_pose=(
            {key: float(value) for key, value in meta["initial_pose"].items()}
            if meta.get("initial_pose") is not None
            else None
        ),
        initial_debug_state=(
            dict(meta["initial_debug_state"])
            if meta.get("initial_debug_state") is not None
            else None
        ),
        initial_world_fingerprint=(
            int(meta["initial_world_fingerprint"])
            if meta.get("initial_world_fingerprint") is not None
            else None
        ),
        initial_chunk_mask=(
            int(meta["initial_chunk_mask"])
            if meta.get("initial_chunk_mask") is not None
            else None
        ),
        initial_loaded_chunks=(
            int(meta["initial_loaded_chunks"])
            if meta.get("initial_loaded_chunks") is not None
            else None
        ),
        events=events,
        total_steps=int(summary["steps"]),
        duration_sec=float(summary["duration_sec"]),
    )


def validate_replay_config(recording: Recording, expected_config: dict[str, object]):
    if recording.config != expected_config:
        raise ValueError("Replay config does not match the config stored in the recording")


def has_tick_timing(recording: Recording) -> bool:
    return recording.initial_state_tick is not None and all(
        event.state_tick is not None for event in recording.events
    )


def has_precise_tick_timing(recording: Recording) -> bool:
    return recording.initial_state_tick is not None and all(
        event.send_state_tick is not None and event.state_tick is not None
        for event in recording.events
    )


def state_tick_delta(recording: Recording, index: int) -> int:
    if not has_tick_timing(recording):
        return 1
    current_tick = int(recording.events[index].state_tick)
    previous_tick = (
        int(recording.initial_state_tick)
        if index == 0
        else int(recording.events[index - 1].state_tick)
    )
    return max(1, current_tick - previous_tick)


def pre_action_ticks(recording: Recording) -> int:
    if not has_precise_tick_timing(recording) or not recording.events:
        return 0
    return max(0, int(recording.events[0].send_state_tick) - int(recording.initial_state_tick))


def active_ticks_for_event(recording: Recording, index: int) -> int:
    if not has_precise_tick_timing(recording):
        return state_tick_delta(recording, index)
    event = recording.events[index]
    return max(1, int(event.state_tick) - int(event.send_state_tick))


def idle_ticks_after_event(recording: Recording, index: int) -> int:
    if not has_precise_tick_timing(recording):
        return 0
    if index + 1 >= len(recording.events):
        return 0
    current = recording.events[index]
    nxt = recording.events[index + 1]
    return max(0, int(nxt.send_state_tick) - int(current.state_tick))


def strict_replay_start(env: NetheriteEnv, recording: Recording) -> tuple[dict, dict[str, float], int]:
    required_loaded_chunks = max(recording.initial_loaded_chunks or 0, FULL_CHUNK_SAMPLE_COUNT)
    return canonicalize_initial_frame(
        env,
        target_pose=recording.initial_pose,
        target_world_fingerprint=recording.initial_world_fingerprint,
        target_chunk_mask=recording.initial_chunk_mask,
        min_loaded_chunks=required_loaded_chunks,
    )


def pose_only_replay_start(env: NetheriteEnv, recording: Recording) -> tuple[dict, dict[str, float], int]:
    required_loaded_chunks = max(recording.initial_loaded_chunks or 0, FULL_CHUNK_SAMPLE_COUNT)
    return canonicalize_initial_frame(
        env,
        target_pose=recording.initial_pose,
        min_loaded_chunks=required_loaded_chunks,
    )


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    run = load_run_config(args.config)
    recording = load_recording(run.recording_path)

    cfg = clone_netherite_config(run.netherite)
    cfg.instance_id = 0
    cfg.seed = run.seed
    validate_replay_config(recording, config_to_dict(cfg))
    sample_root = run.frame_sample_dir
    if sample_root is not None:
        if (sample_root / "replay").exists():
            shutil.rmtree(sample_root / "replay")
        (sample_root / "replay").mkdir(parents=True, exist_ok=True)

    world_dir = ROOT / "run" / "saves" / "netherite_0"

    def launch_replay_world(*, announce: str):
        Launcher(ROOT).cleanup_shmem()
        if world_dir.exists():
            shutil.rmtree(world_dir)
        inst = MCInstance(cfg, ROOT)
        inst.start()
        print(
            f"{announce} {run.recording_path} on seed={run.seed}, pid={inst.process.pid}",
            file=sys.stderr,
        )
        if not inst.wait_for_ready(timeout=120.0):
            raise SystemExit("Minecraft client did not become ready")
        env = NetheriteEnv(config=cfg, timeout=run.timeout)
        env.reset()
        return inst, env

    inst, env = launch_replay_world(announce="Replaying")
    if recording.initial_pose is None:
        print(
            "Recording has no canonical initial_pose. Replay will run, but exact frame matching is unlikely.",
            file=sys.stderr,
        )

    used_fallback = False
    startup_mode = "latched"
    try:
        obs = env.wait_for_start_latch()
        pose_from_debug_state(sample_debug_state(env))
        canonical_steps = 0
        env.release_start_latch()
    except TimeoutError as exc:
        print(
            "Latched replay start timed out; falling back to canonical startup so replay can continue for inspection.",
            file=sys.stderr,
        )
        print(f"Latched startup error: {exc}", file=sys.stderr)
        try:
            obs, _, canonical_steps = strict_replay_start(env, recording)
        except RuntimeError as strict_exc:
            print(
                "Strict canonical replay start timed out; falling back to pose-only startup so replay can continue for visual inspection.",
                file=sys.stderr,
            )
            print(f"Strict canonicalization error: {strict_exc}", file=sys.stderr)
            env.close()
            inst.stop()
            inst, env = launch_replay_world(announce="Relaunching replay")
            obs, _, canonical_steps = pose_only_replay_start(env, recording)
            used_fallback = True
        startup_mode = "pose-only fallback" if used_fallback else "strict fallback"
    if startup_mode == "latched":
        print("Latched replay start frame captured.", file=sys.stderr)
    else:
        print(
            f"Canonical replay start locked after {canonical_steps} warmup steps ({startup_mode}).",
            file=sys.stderr,
        )
    tick_timed_replay = has_tick_timing(recording)
    precise_timing = has_precise_tick_timing(recording)
    if precise_timing:
        print("Replay timing mode: recorded send/end state_tick timing.", file=sys.stderr)
    elif tick_timed_replay:
        print("Replay timing mode: exact recorded state_tick deltas.", file=sys.stderr)
    else:
        print(
            "Replay timing mode: per-step fallback only. Re-record to capture state_tick timing.",
            file=sys.stderr,
        )
    if recording.initial_state_tick is not None:
        current_tick = env.get_state_tick()
        target_tick = int(recording.initial_state_tick)
        if current_tick < target_tick:
            env.advance_ticks(target_tick - current_tick)
            obs = env._get_obs(wait_for_new_state=True, wait_for_new_frame=True)
        elif current_tick > target_tick:
            print(
                f"Replay start tick {current_tick} already exceeds recorded initial_state_tick {target_tick}.",
                file=sys.stderr,
            )
    if sample_root is not None and run.frame_sample_every > 0:
        print(
            f"Saving replay samples every {run.frame_sample_every} steps under {sample_root}",
            file=sys.stderr,
        )
    save_frame_sample(
        root_dir=sample_root,
        phase="replay",
        step=0,
        frame=obs["pov"],
        every=run.frame_sample_every,
    )
    save_state_sample(
        root_dir=sample_root,
        phase="replay",
        step=0,
        state=sample_debug_state(env),
        every=run.frame_sample_every,
    )

    if not args.headless:
        pygame.init()
        display_size = (cfg.width * run.display_scale, cfg.height * run.display_scale)
        screen = pygame.display.set_mode(display_size)
        pygame.display.set_caption(f"Netherite Replay | seed {run.seed} | {QUIT_KEY} stop")
    else:
        display_size = (0, 0)
        screen = None

    initial_match = frame_digest(obs["pov"]) == recording.initial_frame_hash
    mismatch_count = 0 if initial_match else 1
    first_mismatch = None if initial_match else 0
    step = 0
    t0 = time.perf_counter()
    last_display = t0
    display_interval = 1.0 / run.display_fps
    running = True

    try:
        while running and step < recording.total_steps:
            if not args.headless:
                for event in pygame.event.get():
                    if event.type == pygame.QUIT:
                        running = False

                keys = pygame.key.get_pressed()
                if keys[pygame.K_F12]:
                    running = False
                    continue

            event = recording.events[step]
            if precise_timing:
                obs, _, _, _, _ = env.step_for_ticks(
                    build_env_action(event.action),
                    active_ticks_for_event(recording, step),
                )
            elif tick_timed_replay:
                obs, _, _, _, _ = env.step_for_ticks(
                    build_env_action(event.action),
                    state_tick_delta(recording, step),
                )
            else:
                obs, _, _, _, _ = env.step_sync(build_env_action(event.action))
            step += 1
            save_frame_sample(
                root_dir=sample_root,
                phase="replay",
                step=step,
                frame=obs["pov"],
                every=run.frame_sample_every,
            )
            save_state_sample(
                root_dir=sample_root,
                phase="replay",
                step=step,
                state=sample_debug_state(env),
                every=run.frame_sample_every,
            )

            if frame_digest(obs["pov"]) != event.frame_hash:
                mismatch_count += 1
                if first_mismatch is None:
                    first_mismatch = step

            if precise_timing:
                idle_ticks = idle_ticks_after_event(recording, step - 1)
                if idle_ticks > 0:
                    env.advance_ticks(idle_ticks)

            elapsed = time.perf_counter() - t0
            now = time.perf_counter()
            if not args.headless and now - last_display >= display_interval:
                screen.blit(_frame_surface(obs["pov"], display_size), (0, 0))
                mismatch_text = "none" if first_mismatch is None else str(first_mismatch)
                pygame.display.set_caption(
                    f"Netherite Replay | seed {run.seed} | {step / elapsed:.0f} steps/s | "
                    f"step {step}/{recording.total_steps} | mismatches {mismatch_count} | "
                    f"first {mismatch_text} | {QUIT_KEY} stop"
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
                f"Replay done. {step} steps in {elapsed:.1f}s = {step / elapsed:.1f} steps/s | "
                f"canonical_steps={canonical_steps} | initial_match={initial_match} | "
                f"frame_mismatches={mismatch_count} | first_mismatch={first_mismatch}",
                file=sys.stderr,
            )
        index_path = write_sample_index(sample_root)
        if index_path is not None and run.frame_sample_every > 0:
            print(f"Frame sample index: {index_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
