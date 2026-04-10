"""Tests for loading and validating replay recordings."""

# ruff: noqa: E402

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

import pygame_replay
from pygame_recorder import build_minerl_action, format_meta_record, format_step_record, format_summary_record
from pygame_replay import (
    Recording,
    RecordingEvent,
    active_ticks_for_event,
    has_tick_timing,
    has_precise_tick_timing,
    idle_ticks_after_event,
    load_recording,
    pose_only_replay_start,
    pre_action_ticks,
    state_tick_delta,
    strict_replay_start,
    validate_replay_config,
)
from recording_utils import FULL_CHUNK_SAMPLE_COUNT
from run_config import config_to_dict
from config import NetheriteConfig


def test_load_recording_parses_meta_steps_and_summary(tmp_path: Path):
    cfg = NetheriteConfig(seed=123, width=160, height=90, render_distance=8, simulation_distance=8)
    path = tmp_path / "recording.jsonl"
    path.write_text(
        "\n".join(
            [
                format_meta_record(
                    seed=123,
                    config=config_to_dict(cfg),
                    initial_frame_hash="initial",
                    initial_state_tick=70,
                    initial_pose={"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0},
                    initial_debug_state={"position": [1.0, 2.0, 3.0], "state_tick": 70},
                    initial_world_fingerprint=99,
                    initial_chunk_mask=31,
                    initial_loaded_chunks=25,
                ),
                format_step_record(
                    step=1,
                    elapsed=0.01,
                    action=build_minerl_action({"forward"}, look_speed=4),
                    frame_hash="frame1",
                    send_state_tick=71,
                    state_tick=72,
                    debug_state={"position": [1.1, 2.0, 3.0], "state_tick": 72},
                ),
                format_summary_record(steps=1, elapsed=0.5),
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    recording = load_recording(path)

    assert recording.seed == 123
    assert recording.config["width"] == 160
    assert recording.initial_frame_hash == "initial"
    assert recording.initial_state_tick == 70
    assert recording.initial_pose == {
        "x": 1.0,
        "y": 2.0,
        "z": 3.0,
        "yaw": 4.0,
        "pitch": 5.0,
    }
    assert recording.initial_debug_state == {"position": [1.0, 2.0, 3.0], "state_tick": 70}
    assert recording.initial_world_fingerprint == 99
    assert recording.initial_chunk_mask == 31
    assert recording.initial_loaded_chunks == 25
    assert recording.total_steps == 1
    assert recording.duration_sec == 0.5
    assert len(recording.events) == 1
    assert recording.events[0].step == 1
    assert recording.events[0].action["forward"] == 1
    assert recording.events[0].frame_hash == "frame1"
    assert recording.events[0].send_state_tick == 71
    assert recording.events[0].state_tick == 72
    assert recording.events[0].debug_state == {"position": [1.1, 2.0, 3.0], "state_tick": 72}


def test_validate_replay_config_requires_exact_match():
    cfg = NetheriteConfig(seed=123, width=160, height=90, render_distance=8, simulation_distance=8)
    recording = type("Recording", (), {"config": config_to_dict(cfg)})()

    validate_replay_config(recording, config_to_dict(cfg))

    mismatch_cfg = NetheriteConfig(seed=123, width=320, height=180, render_distance=8, simulation_distance=8)
    try:
        validate_replay_config(recording, config_to_dict(mismatch_cfg))
    except ValueError as exc:
        assert "does not match" in str(exc)
    else:
        raise AssertionError("Expected replay config mismatch to raise ValueError")


def test_load_recording_rejects_mismatched_summary_count(tmp_path: Path):
    cfg = NetheriteConfig(seed=123, width=160, height=90, render_distance=8, simulation_distance=8)
    path = tmp_path / "broken.jsonl"
    path.write_text(
        "\n".join(
            [
                format_meta_record(
                    seed=123,
                    config=config_to_dict(cfg),
                    initial_frame_hash="initial",
                    initial_state_tick=70,
                    initial_pose={"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0},
                    initial_debug_state={"position": [1.0, 2.0, 3.0], "state_tick": 70},
                    initial_world_fingerprint=99,
                    initial_chunk_mask=31,
                    initial_loaded_chunks=25,
                ),
                format_step_record(
                    step=1,
                    elapsed=0.01,
                    action=build_minerl_action({"forward"}, look_speed=4),
                    frame_hash="frame1",
                    send_state_tick=71,
                    state_tick=72,
                    debug_state={"position": [1.1, 2.0, 3.0], "state_tick": 72},
                ),
                format_summary_record(steps=2, elapsed=0.5),
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    try:
        load_recording(path)
    except ValueError as exc:
        assert "does not match summary" in str(exc)
    else:
        raise AssertionError("Expected mismatched recording summary to raise ValueError")


def test_load_recording_accepts_legacy_meta_without_initial_pose(tmp_path: Path):
    cfg = NetheriteConfig(seed=123, width=160, height=90, render_distance=8, simulation_distance=8)
    path = tmp_path / "legacy.jsonl"
    path.write_text(
        "\n".join(
            [
                '{"type":"meta","version":2,"seed":123,'
                f'"config":{json.dumps(config_to_dict(cfg), separators=(",", ":"))},'
                '"initial_frame_hash":"initial"}',
                json.dumps(
                    {
                        "type": "step",
                        "step": 1,
                        "steps_per_sec": 100.0,
                        "action": build_minerl_action({"forward"}, look_speed=4),
                        "frame_hash": "frame1",
                    },
                    separators=(",", ":"),
                ),
                format_summary_record(steps=1, elapsed=0.5),
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    recording = load_recording(path)

    assert recording.initial_pose is None
    assert recording.initial_state_tick is None
    assert recording.initial_debug_state is None
    assert recording.initial_world_fingerprint is None
    assert recording.initial_chunk_mask is None
    assert recording.initial_loaded_chunks is None
    assert recording.events[0].send_state_tick is None
    assert recording.events[0].state_tick is None
    assert recording.events[0].debug_state is None


def test_strict_replay_start_uses_full_signature(monkeypatch):
    calls: list[dict[str, object]] = []

    def fake_canonicalize_initial_frame(env, **kwargs):
        calls.append(kwargs)
        return {"pov": "frame"}, {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0}, 11

    monkeypatch.setattr(pygame_replay, "canonicalize_initial_frame", fake_canonicalize_initial_frame)
    recording = Recording(
        seed=123,
        config={},
        initial_frame_hash="initial",
        initial_state_tick=70,
        initial_pose={"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0},
        initial_debug_state=None,
        initial_world_fingerprint=99,
        initial_chunk_mask=31,
        initial_loaded_chunks=25,
        events=[
            RecordingEvent(
                step=1,
                action={},
                frame_hash="frame1",
                send_state_tick=71,
                state_tick=72,
                debug_state=None,
            )
        ],
        total_steps=1,
        duration_sec=0.5,
    )

    obs, pose, canonical_steps = strict_replay_start(object(), recording)

    assert obs["pov"] == "frame"
    assert pose["x"] == 1.0
    assert canonical_steps == 11
    assert calls == [
        {
            "target_pose": recording.initial_pose,
            "target_world_fingerprint": 99,
            "target_chunk_mask": 31,
            "min_loaded_chunks": FULL_CHUNK_SAMPLE_COUNT,
        }
    ]


def test_pose_only_replay_start_uses_pose_only_signature(monkeypatch):
    calls: list[dict[str, object]] = []

    def fake_canonicalize_initial_frame(env, **kwargs):
        calls.append(kwargs)
        return {"pov": "frame"}, {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0}, 9

    monkeypatch.setattr(pygame_replay, "canonicalize_initial_frame", fake_canonicalize_initial_frame)
    recording = Recording(
        seed=123,
        config={},
        initial_frame_hash="initial",
        initial_state_tick=70,
        initial_pose={"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0},
        initial_debug_state=None,
        initial_world_fingerprint=99,
        initial_chunk_mask=31,
        initial_loaded_chunks=25,
        events=[
            RecordingEvent(
                step=1,
                action={},
                frame_hash="frame1",
                send_state_tick=71,
                state_tick=72,
                debug_state=None,
            )
        ],
        total_steps=1,
        duration_sec=0.5,
    )

    obs, pose, canonical_steps = pose_only_replay_start(object(), recording)

    assert obs["pov"] == "frame"
    assert pose["x"] == 1.0
    assert canonical_steps == 9
    assert calls == [
        {
            "target_pose": recording.initial_pose,
            "min_loaded_chunks": FULL_CHUNK_SAMPLE_COUNT,
        }
    ]


def test_tick_timing_helpers_compute_state_deltas():
    recording = Recording(
        seed=123,
        config={},
        initial_frame_hash="initial",
        initial_state_tick=100,
        initial_pose=None,
        initial_debug_state=None,
        initial_world_fingerprint=None,
        initial_chunk_mask=None,
        initial_loaded_chunks=None,
        events=[
            RecordingEvent(step=1, action={}, frame_hash="f1", send_state_tick=102, state_tick=103, debug_state=None),
            RecordingEvent(step=2, action={}, frame_hash="f2", send_state_tick=104, state_tick=105, debug_state=None),
            RecordingEvent(step=3, action={}, frame_hash="f3", send_state_tick=105, state_tick=106, debug_state=None),
        ],
        total_steps=3,
        duration_sec=0.5,
    )

    assert has_tick_timing(recording) is True
    assert has_precise_tick_timing(recording) is True
    assert state_tick_delta(recording, 0) == 3
    assert state_tick_delta(recording, 1) == 2
    assert state_tick_delta(recording, 2) == 1
    assert pre_action_ticks(recording) == 2
    assert active_ticks_for_event(recording, 0) == 1
    assert active_ticks_for_event(recording, 1) == 1
    assert active_ticks_for_event(recording, 2) == 1
    assert idle_ticks_after_event(recording, 0) == 1
    assert idle_ticks_after_event(recording, 1) == 0
    assert idle_ticks_after_event(recording, 2) == 0
