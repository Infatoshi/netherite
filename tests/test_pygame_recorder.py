"""Tests for the pygame recorder helpers."""

# ruff: noqa: E402

import json
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from pygame_recorder import (
    build_env_action,
    build_minerl_action,
    format_meta_record,
    format_step_record,
    format_summary_record,
)
from run_config import config_to_dict
from config import NetheriteConfig


def test_build_minerl_action_has_fixed_minerl_style_shape():
    action = build_minerl_action(
        {
            "forward",
            "right",
            "jump",
            "sprint",
            "attack",
            "use",
            "hotbar.3",
            "look_left",
            "look_down",
        },
        look_speed=6,
    )

    assert list(action) == [
        "ESC",
        "attack",
        "back",
        "camera",
        "drop",
        "forward",
        "hotbar.1",
        "hotbar.2",
        "hotbar.3",
        "hotbar.4",
        "hotbar.5",
        "hotbar.6",
        "hotbar.7",
        "hotbar.8",
        "hotbar.9",
        "inventory",
        "jump",
        "left",
        "pickItem",
        "right",
        "sneak",
        "sprint",
        "swapHands",
        "use",
    ]
    assert action["camera"] == [-6, 6]


def test_build_env_action_keeps_supported_subset_only():
    minerl_action = build_minerl_action({"back", "left", "sneak", "pickItem"}, look_speed=4)

    env_action = build_env_action(minerl_action)

    assert env_action["back"] == 1
    assert env_action["left"] == 1
    assert env_action["sneak"] == 1
    assert env_action["attack"] == 0
    assert env_action["use"] == 0
    np.testing.assert_array_equal(
        env_action["camera"],
        np.array([0.0, 0.0], dtype=np.float32),
    )


def test_meta_step_and_summary_records_encode_expected_fields():
    cfg = NetheriteConfig(seed=4242, width=160, height=90, render_distance=8, simulation_distance=8)
    meta = json.loads(
        format_meta_record(
            seed=4242,
            config=config_to_dict(cfg),
            initial_frame_hash="abc123",
            initial_state_tick=77,
            initial_pose={"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0},
            initial_debug_state={"position": [1.0, 2.0, 3.0], "state_tick": 77},
            initial_world_fingerprint=123456789,
            initial_chunk_mask=0x1FFFFFF,
            initial_loaded_chunks=25,
        )
    )
    step = json.loads(
        format_step_record(
            step=50,
            elapsed=2.0,
            action=build_minerl_action({"forward"}, look_speed=4),
            frame_hash="deadbeef",
            send_state_tick=122,
            state_tick=123,
            debug_state={"position": [1.1, 2.0, 3.0], "state_tick": 123},
        )
    )
    summary = json.loads(format_summary_record(steps=120, elapsed=3.0))

    assert meta["type"] == "meta"
    assert meta["seed"] == 4242
    assert meta["version"] == 7
    assert meta["config"]["render_distance"] == 8
    assert meta["initial_frame_hash"] == "abc123"
    assert meta["initial_state_tick"] == 77
    assert meta["initial_pose"] == {
        "x": 1.0,
        "y": 2.0,
        "z": 3.0,
        "yaw": 4.0,
        "pitch": 5.0,
    }
    assert meta["initial_debug_state"] == {"position": [1.0, 2.0, 3.0], "state_tick": 77}
    assert meta["initial_world_fingerprint"] == 123456789
    assert meta["initial_chunk_mask"] == 0x1FFFFFF
    assert meta["initial_loaded_chunks"] == 25
    assert step == {
        "type": "step",
        "step": 50,
        "steps_per_sec": 25.0,
        "action": build_minerl_action({"forward"}, look_speed=4),
        "frame_hash": "deadbeef",
        "send_state_tick": 122,
        "state_tick": 123,
        "debug_state": {"position": [1.1, 2.0, 3.0], "state_tick": 123},
    }
    assert summary == {
        "type": "summary",
        "steps": 120,
        "duration_sec": 3.0,
        "steps_per_sec": 40.0,
    }
