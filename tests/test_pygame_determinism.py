"""Tests for shared recording utilities used by the determinism tools."""

# ruff: noqa: E402

import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from recording_utils import (
    FULL_CHUNK_SAMPLE_COUNT,
    canonicalize_initial_frame,
    clone_action,
    compare_physics_states,
    frame_digest,
    physics_debug_state,
    pose_matches,
    pose_from_debug_state,
    sample_debug_state,
    sample_state_path,
    sample_step_path,
    save_state_sample,
    world_signature_matches,
    write_sample_index,
    zero_action,
)


def test_frame_digest_is_stable_for_same_pixels_and_changes_on_difference():
    frame = np.zeros((2, 2, 3), dtype=np.uint8)
    same = frame.copy()
    changed = frame.copy()
    changed[0, 0, 0] = 1

    digest = frame_digest(frame)

    assert digest == frame_digest(same)
    assert digest != frame_digest(changed)


def test_clone_action_copies_camera_list_and_zero_action_is_full_shape():
    action = {
        "forward": 1,
        "camera": [4, -2],
    }

    cloned = clone_action(action)
    cloned["camera"][0] = 99

    assert action["camera"] == [4, -2]
    assert cloned["camera"] == [99, -2]
    assert zero_action()["camera"] == [0, 0]
    assert zero_action()["forward"] == 0
    assert zero_action()["use"] == 0


def test_pose_matches_uses_small_position_and_angle_tolerances():
    pose = {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0}

    assert pose_matches(pose, {"x": 1.005, "y": 2.005, "z": 2.995, "yaw": 4.005, "pitch": 4.995})
    assert not pose_matches(pose, {"x": 1.02, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0})
    assert not pose_matches(pose, {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.02, "pitch": 5.0})


def test_canonicalize_initial_frame_waits_for_stable_hashes():
    class FakeEnv:
        def __init__(self):
            self.index = 0
            self.frames = [
                np.full((1, 1, 3), 1, dtype=np.uint8),
                np.full((1, 1, 3), 2, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
                np.full((1, 1, 3), 3, dtype=np.uint8),
            ]
            self.debug_state = {
                "position": [1.0, 2.0, 3.0],
                "yaw": 4.0,
                "pitch": 5.0,
                "world_fingerprint": 123,
                "loaded_chunks": FULL_CHUNK_SAMPLE_COUNT,
                "chunk_mask": 0x1FFFFFF,
            }

        def _get_obs(self, **_kwargs):
            return {"pov": self.frames[self.index]}

        def step(self, _action):
            self.index += 1
            return {"pov": self.frames[self.index]}, 0.0, False, False, {}

        def get_debug_state(self):
            return dict(self.debug_state)

        def align_to_pose(self, pose):
            self.debug_state.update(
                {
                    "position": [pose["x"], pose["y"], pose["z"]],
                    "yaw": pose["yaw"],
                    "pitch": pose["pitch"],
                }
            )
            return self._get_obs()

    env = FakeEnv()

    obs, pose, warmup_steps = canonicalize_initial_frame(
        env,
        stable_frames=4,
        max_steps=16,
        target_world_fingerprint=123,
        min_loaded_chunks=FULL_CHUNK_SAMPLE_COUNT,
    )

    assert frame_digest(obs["pov"]) == frame_digest(np.full((1, 1, 3), 3, dtype=np.uint8))
    assert pose == {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0}
    assert warmup_steps == 5


def test_canonicalize_initial_frame_skips_align_when_pose_already_matches():
    class FakeEnv:
        def __init__(self):
            self.index = 0
            self.align_calls = 0
            self.frames = [
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
                np.full((1, 1, 3), 7, dtype=np.uint8),
            ]
            self.debug_state = {
                "position": [1.0, 2.0, 3.0],
                "yaw": 4.0,
                "pitch": 5.0,
                "world_fingerprint": 123,
                "loaded_chunks": FULL_CHUNK_SAMPLE_COUNT,
                "chunk_mask": 0x1FFFFFF,
            }

        def _get_obs(self, **_kwargs):
            return {"pov": self.frames[self.index]}

        def step(self, _action):
            self.index += 1
            return {"pov": self.frames[self.index]}, 0.0, False, False, {}

        def get_debug_state(self):
            return dict(self.debug_state)

        def align_to_pose(self, _pose):
            self.align_calls += 1
            return self._get_obs()

    env = FakeEnv()
    target_pose = {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0}

    canonicalize_initial_frame(
        env,
        target_pose=target_pose,
        stable_frames=4,
        max_steps=16,
        target_world_fingerprint=123,
        min_loaded_chunks=FULL_CHUNK_SAMPLE_COUNT,
    )

    assert env.align_calls == 0


def test_sample_step_path_uses_phase_and_zero_padded_step(tmp_path: Path):
    path = sample_step_path(tmp_path, "record", 42)

    assert path == tmp_path / "record" / "step_000042.png"


def test_write_sample_index_lists_record_and_replay_images(tmp_path: Path):
    record_path = sample_step_path(tmp_path, "record", 0)
    replay_path = sample_step_path(tmp_path, "replay", 0)
    record_path.parent.mkdir(parents=True)
    replay_path.parent.mkdir(parents=True)
    record_path.write_bytes(b"record")
    replay_path.write_bytes(b"replay")

    index_path = write_sample_index(tmp_path)
    html = index_path.read_text(encoding="utf-8")

    assert "step_000000.png" in html
    assert "record/step_000000.png" in html
    assert "replay/step_000000.png" in html


def test_save_state_sample_and_sample_debug_state(tmp_path: Path):
    class FakeEnv:
        def get_debug_state(self):
            return {"state_tick": 12, "position": [1.0, 2.0, 3.0]}

    state = sample_debug_state(FakeEnv())
    path = save_state_sample(
        root_dir=tmp_path,
        phase="record",
        step=200,
        state=state,
        every=200,
    )

    assert path == sample_state_path(tmp_path, "record", 200)
    assert path.read_text(encoding="utf-8").strip() == '{"position": [1.0, 2.0, 3.0], "state_tick": 12}'


def test_world_signature_matches_and_pose_from_debug_state():
    state = {
        "position": [1.0, 2.0, 3.0],
        "yaw": 4.0,
        "pitch": 5.0,
        "world_fingerprint": 123,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
    }

    assert pose_from_debug_state(state) == {
        "x": 1.0,
        "y": 2.0,
        "z": 3.0,
        "yaw": 4.0,
        "pitch": 5.0,
    }
    assert world_signature_matches(
        state,
        target_world_fingerprint=123,
        target_chunk_mask=0x1FFFFFF,
        min_loaded_chunks=25,
    )
    assert not world_signature_matches(state, target_world_fingerprint=999)


def test_physics_debug_state_and_compare_physics_states():
    expected = {
        "position": [1.0, 2.0, 3.0],
        "yaw": 4.0,
        "pitch": 5.0,
        "health": 20.0,
        "max_health": 20.0,
        "food": 20,
        "saturation": 5.0,
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 123,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "state_tick": 100,
    }
    close = {
        **expected,
        "position": [1.0, 2.0, 3.0000001],
        "yaw": 4.0000001,
    }
    different = {
        **expected,
        "position": [1.0, 2.0, 3.1],
        "food": 19,
        "state_tick": 101,
    }

    assert physics_debug_state(expected)["food"] == 20
    assert compare_physics_states(expected, close) == {}
    assert compare_physics_states(expected, different) == {
        "position": ([1.0, 2.0, 3.0], [1.0, 2.0, 3.1]),
        "food": (20, 19),
        "state_tick": (100, 101),
    }
