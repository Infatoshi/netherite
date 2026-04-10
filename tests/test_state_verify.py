"""Tests for state-only replay verification helpers."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from pygame_replay import Recording, RecordingEvent
from state_verify import TickCheckpoint, build_tick_checkpoints, parse_tick_checkpoints


def test_parse_tick_checkpoints_sorts_and_deduplicates():
    assert parse_tick_checkpoints("100,0,100,50") == [0, 50, 100]


def test_build_tick_checkpoints_maps_tick_offsets_to_recorded_steps():
    recording = Recording(
        seed=424242,
        config={},
        initial_frame_hash="initial",
        initial_state_tick=200,
        initial_pose=None,
        initial_debug_state={"position": [0.5, 112.0, 0.5], "state_tick": 200},
        initial_world_fingerprint=None,
        initial_chunk_mask=None,
        initial_loaded_chunks=None,
        events=[
            RecordingEvent(
                step=1,
                action={},
                frame_hash="f1",
                send_state_tick=201,
                state_tick=210,
                debug_state={"position": [0.5, 112.0, 0.5], "state_tick": 210},
            ),
            RecordingEvent(
                step=2,
                action={},
                frame_hash="f2",
                send_state_tick=211,
                state_tick=260,
                debug_state={"position": [1.5, 112.0, 0.5], "state_tick": 260},
            ),
        ],
        total_steps=2,
        duration_sec=1.0,
    )

    checkpoints = build_tick_checkpoints(recording, [0, 10, 50])

    assert checkpoints == [
        TickCheckpoint(tick_offset=0, step_index=None, step_number=0),
        TickCheckpoint(tick_offset=10, step_index=0, step_number=1),
        TickCheckpoint(tick_offset=50, step_index=1, step_number=2),
    ]
