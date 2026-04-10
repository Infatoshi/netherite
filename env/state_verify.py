"""Compare recorded physics state against replay at fixed tick checkpoints."""

from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv
from pygame_recorder import build_env_action
from pygame_replay import (
    active_ticks_for_event,
    has_precise_tick_timing,
    has_tick_timing,
    idle_ticks_after_event,
    load_recording,
    pose_only_replay_start,
    pre_action_ticks,
    state_tick_delta,
    strict_replay_start,
    validate_replay_config,
)
from recording_utils import (
    compare_physics_states,
    compare_server_world_samples,
    compare_world_samples,
    physics_debug_state,
    sample_debug_state,
)
from run_config import clone_netherite_config, config_to_dict, load_run_config


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class TickCheckpoint:
    tick_offset: int
    step_index: int | None
    step_number: int


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        required=True,
        help="Path to a YAML run config, for example config/v1.yaml.",
    )
    parser.add_argument(
        "--tick-checkpoints",
        default="0,100,500,1000",
        help="Comma-separated replay-relative state_tick offsets to compare.",
    )
    parser.add_argument(
        "--reuse-world",
        action="store_true",
        help="Keep run/saves/netherite_<id> on disk (use after a recorder run on the same config).",
    )
    return parser.parse_args(argv)


def parse_tick_checkpoints(raw: str) -> list[int]:
    checkpoints: set[int] = set()
    for part in raw.split(","):
        text = part.strip()
        if not text:
            continue
        value = int(text)
        if value < 0:
            raise ValueError("tick checkpoints must be non-negative")
        checkpoints.add(value)
    if not checkpoints:
        raise ValueError("at least one tick checkpoint is required")
    return sorted(checkpoints)


def build_tick_checkpoints(recording, tick_offsets: list[int]) -> list[TickCheckpoint]:
    if recording.initial_state_tick is None:
        raise ValueError("Recording is missing initial_state_tick")
    if recording.initial_debug_state is None:
        raise ValueError(
            "Recording is missing initial_debug_state. Re-record with the current recorder."
        )
    if any(
        event.debug_state is None or event.state_tick is None
        for event in recording.events
    ):
        raise ValueError(
            "Recording is missing per-step debug_state/state_tick. Re-record with the current recorder."
        )

    checkpoints: list[TickCheckpoint] = []
    for tick_offset in tick_offsets:
        if tick_offset == 0:
            checkpoints.append(
                TickCheckpoint(tick_offset=0, step_index=None, step_number=0)
            )
            continue

        found = None
        for index, event in enumerate(recording.events):
            relative_tick = int(event.state_tick) - int(recording.initial_state_tick)
            if relative_tick >= tick_offset:
                found = TickCheckpoint(
                    tick_offset=tick_offset,
                    step_index=index,
                    step_number=int(event.step),
                )
                break
        if found is None:
            raise ValueError(f"Recording does not reach tick checkpoint {tick_offset}")
        checkpoints.append(found)
    return checkpoints


def _format_state(label: str, state: dict[str, object]) -> str:
    position = state["position"]
    server_fp = state.get("server_world_fingerprint", 0)
    return (
        f"{label}: pos=({position[0]:.3f}, {position[1]:.3f}, {position[2]:.3f}) "
        f"yaw={state['yaw']:.3f} pitch={state['pitch']:.3f} "
        f"hp={state['health']:.1f}/{state['max_health']:.1f} "
        f"food={state['food']} sat={state['saturation']:.1f} "
        f"ground={state['on_ground']} water={state['in_water']} "
        f"world={state['world_fingerprint']:016x} "
        f"server={server_fp:016x} "
        f"chunks={state['loaded_chunks']} mask={state['chunk_mask']:08x} "
        f"seed={state['actual_world_seed']} tick={state['state_tick']}"
    )


def _print_checkpoint_result(
    checkpoint: TickCheckpoint,
    expected: dict[str, object],
    actual: dict[str, object],
) -> bool:
    diffs = compare_physics_states(expected, actual)
    sample_diffs = compare_world_samples(expected, actual)
    server_sample_diffs = compare_server_world_samples(expected, actual)

    # Server fingerprint comparison
    expected_server_fp = physics_debug_state(expected).get(
        "server_world_fingerprint", 0
    )
    actual_server_fp = physics_debug_state(actual).get("server_world_fingerprint", 0)
    if expected_server_fp != actual_server_fp:
        diffs["server_world_fingerprint"] = (expected_server_fp, actual_server_fp)

    match = not diffs and not sample_diffs and not server_sample_diffs
    print(
        f"tick+{checkpoint.tick_offset} step={checkpoint.step_number} "
        f"{'MATCH' if match else 'MISMATCH'}"
    )
    print(_format_state("  record", physics_debug_state(expected)))
    print(_format_state("  replay", physics_debug_state(actual)))
    if not match:
        for key, values in diffs.items():
            print(f"  diff {key}: {values[0]} != {values[1]}")
        if sample_diffs:
            print("  client_world_sample_diffs:")
            for diff in sample_diffs:
                if diff["index"] < 0:
                    print(
                        f"    sample_length: record={diff['expected']} replay={diff['actual']}"
                    )
                else:
                    print(
                        f"    idx={diff['index']} rel=({diff['dx']}, {diff['dy']}, {diff['dz']}) "
                        f"record={diff['expected']} replay={diff['actual']}"
                    )
        if server_sample_diffs:
            print("  server_world_sample_diffs:")
            for diff in server_sample_diffs:
                if diff["index"] < 0:
                    print(
                        f"    sample_length: record={diff['expected']} replay={diff['actual']}"
                    )
                else:
                    print(
                        f"    idx={diff['index']} rel=({diff['dx']}, {diff['dy']}, {diff['dz']}) "
                        f"record={diff['expected']} replay={diff['actual']}"
                    )
    return match


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    tick_offsets = parse_tick_checkpoints(args.tick_checkpoints)
    run = load_run_config(args.config)
    recording = load_recording(run.recording_path)
    checkpoints = build_tick_checkpoints(recording, tick_offsets)

    cfg = clone_netherite_config(run.netherite)
    cfg.instance_id = 0
    cfg.seed = run.seed
    validate_replay_config(recording, config_to_dict(cfg))

    world_dir = ROOT / "run" / "saves" / "netherite_0"

    def launch_replay_world(*, announce: str):
        Launcher(ROOT).cleanup_shmem()
        if world_dir.exists() and not args.reuse_world:
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

    inst, env = launch_replay_world(announce="State-verifying")
    try:
        try:
            env.wait_for_start_latch()
            env.release_start_latch()
        except TimeoutError as exc:
            print(
                f"Latched startup unavailable during state verify: {exc}. Falling back to canonical startup.",
                file=sys.stderr,
            )
            try:
                strict_replay_start(env, recording)
            except RuntimeError as strict_exc:
                print(
                    f"Strict canonical startup unavailable during state verify: {strict_exc}. "
                    "Falling back to pose-only startup.",
                    file=sys.stderr,
                )
                env.close()
                inst.stop()
                inst, env = launch_replay_world(announce="Relaunching state verify")
                pose_only_replay_start(env, recording)

        if recording.initial_state_tick is not None:
            current_tick = env.get_state_tick()
            target_tick = int(recording.initial_state_tick)
            if current_tick < target_tick:
                info = env.advance_ticks(target_tick - current_tick)
                current_state = sample_debug_state(env)
                current_state["state_tick"] = int(info["state_tick"])
            else:
                current_state = sample_debug_state(env)
        else:
            current_state = sample_debug_state(env)

        all_match = True
        remaining = list(checkpoints)
        if remaining and remaining[0].step_index is None:
            all_match &= _print_checkpoint_result(
                remaining[0],
                recording.initial_debug_state,
                current_state,
            )
            remaining = remaining[1:]

        precise_timing = has_precise_tick_timing(recording)
        tick_timed_replay = has_tick_timing(recording)
        if precise_timing and pre_action_ticks(recording) > 0:
            info = env.advance_ticks(pre_action_ticks(recording))
            current_state = sample_debug_state(env)
            current_state["state_tick"] = int(info["state_tick"])

        for index, event in enumerate(recording.events):
            if precise_timing:
                _, _, _, _, info = env.step_for_ticks(
                    build_env_action(event.action),
                    active_ticks_for_event(recording, index),
                    wait_for_new_frame=False,  # Skip frame sync for speed
                )
            elif tick_timed_replay:
                _, _, _, _, info = env.step_for_ticks(
                    build_env_action(event.action),
                    state_tick_delta(recording, index),
                )
            else:
                _, _, _, _, info = env.step_sync(build_env_action(event.action))
            current_state = info["debug_state"]

            while remaining and remaining[0].step_index == index:
                all_match &= _print_checkpoint_result(
                    remaining[0],
                    event.debug_state,
                    current_state,
                )
                remaining = remaining[1:]

            if precise_timing:
                idle_ticks = idle_ticks_after_event(recording, index)
                if idle_ticks > 0:
                    idle_info = env.advance_ticks(idle_ticks)
                    current_state = sample_debug_state(env)
                    current_state["state_tick"] = int(idle_info["state_tick"])

            if not remaining:
                break

        if remaining:
            missing = ", ".join(str(item.tick_offset) for item in remaining)
            raise RuntimeError(
                f"Replay finished before verifying checkpoints: {missing}"
            )

        if not all_match:
            raise SystemExit(1)
    finally:
        env.close()
        inst.stop()


if __name__ == "__main__":
    main()
