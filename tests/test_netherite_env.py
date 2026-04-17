"""Tests for the Python shared-memory reset protocol."""

# ruff: noqa: E402

from __future__ import annotations

import struct
import sys
import threading
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from config import NetheriteConfig
from netherite_env import (
    CONTROL_MAGIC,
    CONTROL_SIZE,
    CTRL_OP_RELEASE_START,
    CTRL_OP_RESET_WORLD,
    CTRL_OP_SET_POSE,
    CTRL_STATUS_BUSY,
    CTRL_STATUS_DONE,
    REWARD_MAGIC,
    REWARD_OFFSET,
    STATE_SIZE,
    NetheriteEnv,
    ShmemWriter,
)


def _control_snapshot(writer: ShmemWriter) -> dict[str, int]:
    buf = writer.read_bytes(0, 48)
    return {
        "magic": struct.unpack_from("<I", buf, 0)[0],
        "request_id": struct.unpack_from("<I", buf, 4)[0],
        "ack_id": struct.unpack_from("<I", buf, 8)[0],
        "status": struct.unpack_from("<I", buf, 12)[0],
        "opcode": struct.unpack_from("<I", buf, 16)[0],
        "requested_seed": struct.unpack_from("<q", buf, 24)[0],
        "active_seed": struct.unpack_from("<q", buf, 32)[0],
        "episode_id": struct.unpack_from("<I", buf, 40)[0],
        "start_latched": struct.unpack_from("<I", buf, 44)[0],
    }


def _make_control_writer(tmp_path: Path, *, active_seed: int = 12345) -> ShmemWriter:
    writer = ShmemWriter(str(tmp_path / "netherite_control_0"), CONTROL_SIZE)
    writer.write(0, struct.pack("<I", CONTROL_MAGIC))
    writer.write(8, struct.pack("<I", 0))
    writer.write(12, struct.pack("<I", 0))
    writer.write(32, struct.pack("<q", active_seed))
    writer.write(40, struct.pack("<I", 0))
    writer.write(44, struct.pack("<I", 0))
    return writer


def _ack_reset(
    writer: ShmemWriter,
    *,
    expected_seed: int,
    result_seed: int | None = None,
) -> tuple[threading.Thread, list[str]]:
    errors: list[str] = []

    def target():
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            control = _control_snapshot(writer)
            if (
                control["request_id"] == 1
                and control["status"] == CTRL_STATUS_BUSY
                and control["opcode"] == CTRL_OP_RESET_WORLD
                and control["requested_seed"] == expected_seed
            ):
                writer.write(
                    32,
                    struct.pack(
                        "<q", expected_seed if result_seed is None else result_seed
                    ),
                )
                writer.write(40, struct.pack("<I", 1))
                writer.write(8, struct.pack("<I", control["request_id"]))
                writer.write(12, struct.pack("<I", CTRL_STATUS_DONE))
                return
            time.sleep(0.001)
        errors.append("reset request was never observed")

    thread = threading.Thread(target=target, daemon=True)
    thread.start()
    return thread, errors


def test_reset_without_options_does_not_request_world_reset():
    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    env._action_writer = object()

    expected = {
        "pov": np.zeros((env.config.height, env.config.width, 3), dtype=np.uint8),
        "inventory": np.zeros((9, 2), dtype=np.int32),
        "health": np.zeros(1, dtype=np.float32),
        "position": np.zeros(3, dtype=np.float64),
    }
    env._get_obs = lambda **_: expected

    obs, info = env.reset(seed=999)

    assert obs is expected
    assert info == {}
    assert env.config.seed == 12345


def test_reset_world_uses_seed_from_options_and_waits_for_fresh_obs(tmp_path: Path):
    writer = _make_control_writer(tmp_path)
    thread, errors = _ack_reset(writer, expected_seed=4242)

    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.2)
    env._action_writer = object()
    env._control_writer = writer
    captured: dict[str, bool] = {}

    def fake_get_obs(**kwargs):
        captured.update(kwargs)
        return {"ok": True}

    env._get_obs = fake_get_obs

    obs, info = env.reset(options={"reset_world": True, "seed": 4242})
    thread.join(timeout=1.0)

    assert errors == []
    assert obs == {"ok": True}
    assert info == {}
    assert captured == {"wait_for_new_state": True, "wait_for_new_frame": True}
    assert env.config.seed == 4242

    writer.close()


def test_reset_world_can_fall_back_to_gym_seed_argument(tmp_path: Path):
    writer = _make_control_writer(tmp_path)
    thread, errors = _ack_reset(writer, expected_seed=777)

    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.2)
    env._action_writer = object()
    env._control_writer = writer
    env._get_obs = lambda **_: {"ok": True}

    env.reset(seed=777, options={"reset_world": True})
    thread.join(timeout=1.0)

    assert errors == []
    assert env.config.seed == 777

    writer.close()


def test_align_to_pose_sends_pose_request_and_waits_for_fresh_obs(tmp_path: Path):
    writer = _make_control_writer(tmp_path)
    errors: list[str] = []

    def target():
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            control = _control_snapshot(writer)
            if (
                control["request_id"] == 1
                and control["status"] == CTRL_STATUS_BUSY
                and control["opcode"] == CTRL_OP_SET_POSE
            ):
                payload = writer.read_bytes(48, 32)
                x = struct.unpack_from("<d", payload, 0)[0]
                y = struct.unpack_from("<d", payload, 8)[0]
                z = struct.unpack_from("<d", payload, 16)[0]
                yaw = struct.unpack_from("<f", payload, 24)[0]
                pitch = struct.unpack_from("<f", payload, 28)[0]
                if (x, y, z, yaw, pitch) != (1.0, 2.0, 3.0, 4.0, 5.0):
                    errors.append("pose payload mismatch")
                    return
                writer.write(8, struct.pack("<I", control["request_id"]))
                writer.write(12, struct.pack("<I", CTRL_STATUS_DONE))
                return
            time.sleep(0.001)
        errors.append("pose request was never observed")

    thread = threading.Thread(target=target, daemon=True)
    thread.start()

    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.2)
    env._action_writer = object()
    env._control_writer = writer
    env._read_state = lambda **_: {
        "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
        "yaw": 4.0,
        "pitch": 5.0,
        "health": np.zeros(1, dtype=np.float32),
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }
    captured: dict[str, bool] = {}

    def fake_get_obs(**kwargs):
        captured.update(kwargs)
        return {"ok": True}

    env._get_obs = fake_get_obs

    obs = env.align_to_pose({"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 4.0, "pitch": 5.0})
    thread.join(timeout=1.0)

    assert errors == []
    assert obs == {"ok": True}
    assert captured == {"wait_for_new_state": True, "wait_for_new_frame": True}

    writer.close()


def test_release_start_latch_sends_release_request(tmp_path: Path):
    writer = _make_control_writer(tmp_path)
    writer.write(44, struct.pack("<I", 1))
    errors: list[str] = []

    def target():
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            control = _control_snapshot(writer)
            if (
                control["request_id"] == 1
                and control["status"] == CTRL_STATUS_BUSY
                and control["opcode"] == CTRL_OP_RELEASE_START
            ):
                writer.write(44, struct.pack("<I", 0))
                writer.write(8, struct.pack("<I", control["request_id"]))
                writer.write(12, struct.pack("<I", CTRL_STATUS_DONE))
                return
            time.sleep(0.001)
        errors.append("release request was never observed")

    thread = threading.Thread(target=target, daemon=True)
    thread.start()

    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.2)
    env._action_writer = object()
    env._control_writer = writer

    env.release_start_latch()
    thread.join(timeout=1.0)

    assert errors == []

    writer.close()


def test_decode_frame_uses_producer_dimensions_and_resizes_to_config():
    env = NetheriteEnv(config=NetheriteConfig(width=2, height=2), timeout=0.05)

    src_rgb = np.array(
        [
            [[10, 11, 12], [20, 21, 22], [30, 31, 32], [40, 41, 42]],
            [[50, 51, 52], [60, 61, 62], [70, 71, 72], [80, 81, 82]],
            [[90, 91, 92], [100, 101, 102], [110, 111, 112], [120, 121, 122]],
            [[130, 131, 132], [140, 141, 142], [150, 151, 152], [160, 161, 162]],
        ],
        dtype=np.uint8,
    )
    rgba_storage = np.concatenate(
        [
            src_rgb[::-1],
            np.full((4, 4, 1), 255, dtype=np.uint8),
        ],
        axis=2,
    )

    decoded = env._decode_frame(rgba_storage.tobytes(), frame_w=4, frame_h=4)

    expected = src_rgb[np.array([0, 3])][:, np.array([0, 3]), :]
    np.testing.assert_array_equal(decoded, expected)


def test_step_sync_waits_for_a_fresh_frame():
    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    calls: list[bool] = []
    env._send_action = lambda _action: None
    current_tick = {"value": 10}

    def fake_read_state(**_kwargs):
        return {
            "position": np.zeros(3, dtype=np.float64),
            "yaw": 0.0,
            "pitch": 0.0,
            "health": np.zeros(1, dtype=np.float32),
            "inventory": np.zeros((9, 2), dtype=np.int32),
        }

    env._read_state = fake_read_state

    def fake_get_state_tick():
        current_tick["value"] += 1
        return current_tick["value"]

    env.get_state_tick = fake_get_state_tick

    def fake_wait_for_frame(*, wait_for_new: bool = False):
        calls.append(wait_for_new)
        return np.zeros((env.config.height, env.config.width, 3), dtype=np.uint8)

    def fake_wait_for_frame_at_tick(target_tick: int):
        calls.append(True)
        env._last_frame_state_tick = target_tick
        return np.zeros((env.config.height, env.config.width, 3), dtype=np.uint8)

    env._wait_for_frame = fake_wait_for_frame
    env._wait_for_frame_at_tick = fake_wait_for_frame_at_tick

    _, _, _, _, info0 = env.step({"camera": [0, 0]})
    _, _, _, _, info1 = env.step_sync({"camera": [0, 0]})

    assert calls == [False, True]
    assert info0["send_state_tick"] == 11
    assert info0["state_tick"] == 13
    assert info0["debug_state"]["state_tick"] == 13
    assert info1["send_state_tick"] == 14
    assert info1["state_tick"] == 16
    assert info1["debug_state"]["state_tick"] == 16


def test_advance_ticks_waits_exact_count():
    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    observed: list[bool] = []
    tick_values = iter([21, 22, 23])

    def fake_read_state(**kwargs):
        observed.append(kwargs.get("wait_for_new", False))
        env._last_state_tick = next(tick_values)
        return {
            "position": np.zeros(3, dtype=np.float64),
            "yaw": 0.0,
            "pitch": 0.0,
            "health": np.zeros(1, dtype=np.float32),
            "inventory": np.zeros((9, 2), dtype=np.int32),
        }

    env._read_state = fake_read_state
    env.get_state_tick = lambda: env._last_state_tick

    info = env.advance_ticks(3)

    assert observed == [True, True, True]
    assert info == {"send_state_tick": 23, "state_tick": 23}


def test_step_for_ticks_waits_until_target_tick():
    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    env._send_action = lambda _action: None
    wait_for_frame_calls: list[bool] = []
    current_tick = {"value": 100}

    def fake_get_state_tick():
        if current_tick["value"] < 107:
            current_tick["value"] += 2
        return current_tick["value"]

    def fake_read_state(*, wait_for_new: bool = False):
        return {
            "position": np.zeros(3, dtype=np.float64),
            "yaw": 0.0,
            "pitch": 0.0,
            "health": np.zeros(1, dtype=np.float32),
            "inventory": np.zeros((9, 2), dtype=np.int32),
        }

    def fake_wait_for_frame(*, wait_for_new: bool = False):
        wait_for_frame_calls.append(wait_for_new)
        return np.zeros((env.config.height, env.config.width, 3), dtype=np.uint8)

    def fake_wait_for_frame_at_tick(target_tick: int):
        wait_for_frame_calls.append(True)
        env._last_frame_state_tick = target_tick
        return np.zeros((env.config.height, env.config.width, 3), dtype=np.uint8)

    env.get_state_tick = fake_get_state_tick
    env._read_state = fake_read_state
    env._wait_for_frame = fake_wait_for_frame
    env._wait_for_frame_at_tick = fake_wait_for_frame_at_tick

    _, _, _, _, info = env.step_for_ticks({"camera": [0, 0]}, 5)

    assert current_tick["value"] == 108
    assert wait_for_frame_calls == [True]
    assert info["send_state_tick"] == 102
    assert info["state_tick"] == 108
    assert info["debug_state"]["state_tick"] == 108


def test_wait_for_start_latch_accepts_stable_frames_even_if_render_counts_are_incomplete():
    env = NetheriteEnv(config=NetheriteConfig(width=2, height=2), timeout=0.05)
    env._control_writer = object()
    env._read_control = lambda: {"start_latched": 1}
    frames = iter(
        [
            np.full((2, 2, 3), 7, dtype=np.uint8),
            np.full((2, 2, 3), 7, dtype=np.uint8),
            np.full((2, 2, 3), 7, dtype=np.uint8),
        ]
    )
    env._wait_for_frame = lambda **_: next(frames)
    env._read_state = lambda **_: {
        "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
        "yaw": 0.0,
        "pitch": 0.0,
        "health": np.array([6.0], dtype=np.float32),
        "max_health": np.array([20.0], dtype=np.float32),
        "food": 20,
        "saturation": np.array([5.0], dtype=np.float32),
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 123,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "completed_render_chunks": 265,
        "total_render_chunks": 6936,
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }

    obs = env.wait_for_start_latch(stable_frames=3, max_frames=3)

    np.testing.assert_array_equal(obs["pov"], np.full((2, 2, 3), 7, dtype=np.uint8))


def test_wait_for_start_latch_requires_state_signature_to_stabilize():
    env = NetheriteEnv(config=NetheriteConfig(width=2, height=2), timeout=0.05)
    env._control_writer = object()
    env._read_control = lambda: {"start_latched": 1}
    frame = np.full((2, 2, 3), 9, dtype=np.uint8)
    env._wait_for_frame = lambda **_: frame

    states = iter(
        [
            {
                "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
                "yaw": 0.0,
                "pitch": 0.0,
                "health": np.array([6.0], dtype=np.float32),
                "max_health": np.array([20.0], dtype=np.float32),
                "food": 20,
                "saturation": np.array([5.0], dtype=np.float32),
                "on_ground": 1,
                "in_water": 0,
                "world_fingerprint": 111,
                "loaded_chunks": 25,
                "chunk_mask": 0x1FFFFFF,
                "actual_world_seed": 424242,
                "completed_render_chunks": 128,
                "total_render_chunks": 128,
                "inventory": np.zeros((9, 2), dtype=np.int32),
            },
            {
                "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
                "yaw": 0.0,
                "pitch": 0.0,
                "health": np.array([6.0], dtype=np.float32),
                "max_health": np.array([20.0], dtype=np.float32),
                "food": 20,
                "saturation": np.array([5.0], dtype=np.float32),
                "on_ground": 1,
                "in_water": 0,
                "world_fingerprint": 222,
                "loaded_chunks": 25,
                "chunk_mask": 0x1FFFFFF,
                "actual_world_seed": 424242,
                "completed_render_chunks": 128,
                "total_render_chunks": 128,
                "inventory": np.zeros((9, 2), dtype=np.int32),
            },
            {
                "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
                "yaw": 0.0,
                "pitch": 0.0,
                "health": np.array([6.0], dtype=np.float32),
                "max_health": np.array([20.0], dtype=np.float32),
                "food": 20,
                "saturation": np.array([5.0], dtype=np.float32),
                "on_ground": 1,
                "in_water": 0,
                "world_fingerprint": 222,
                "loaded_chunks": 25,
                "chunk_mask": 0x1FFFFFF,
                "actual_world_seed": 424242,
                "completed_render_chunks": 128,
                "total_render_chunks": 128,
                "inventory": np.zeros((9, 2), dtype=np.int32),
            },
        ]
    )
    read_count = {"value": 0}

    def fake_read_state(**_kwargs):
        read_count["value"] += 1
        return next(states)

    env._read_state = fake_read_state

    obs = env.wait_for_start_latch(stable_frames=2, max_frames=3)

    assert read_count["value"] == 3
    np.testing.assert_array_equal(obs["pov"], frame)
    np.testing.assert_array_equal(
        obs["position"], np.array([1.0, 2.0, 3.0], dtype=np.float64)
    )


def test_wait_for_start_latch_ignores_frame_only_drift_when_state_is_stable():
    env = NetheriteEnv(config=NetheriteConfig(width=2, height=2), timeout=0.05)
    env._control_writer = object()
    env._read_control = lambda: {"start_latched": 1}
    frames = iter(
        [
            np.full((2, 2, 3), 1, dtype=np.uint8),
            np.full((2, 2, 3), 2, dtype=np.uint8),
            np.full((2, 2, 3), 3, dtype=np.uint8),
        ]
    )
    env._wait_for_frame = lambda **_: next(frames)
    env._read_state = lambda **_: {
        "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
        "yaw": 0.0,
        "pitch": 0.0,
        "health": np.array([6.0], dtype=np.float32),
        "max_health": np.array([20.0], dtype=np.float32),
        "food": 20,
        "saturation": np.array([5.0], dtype=np.float32),
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 123,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "completed_render_chunks": 128,
        "total_render_chunks": 128,
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }

    obs = env.wait_for_start_latch(stable_frames=3, max_frames=3)

    np.testing.assert_array_equal(obs["position"], np.array([1.0, 2.0, 3.0]))


def test_latched_start_changed_fields_reports_only_modified_components():
    env = NetheriteEnv(config=NetheriteConfig(width=2, height=2), timeout=0.05)
    frame_a = np.full((2, 2, 3), 1, dtype=np.uint8)
    frame_b = np.full((2, 2, 3), 2, dtype=np.uint8)
    base_state = {
        "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
        "yaw": 0.0,
        "pitch": 0.0,
        "health": np.array([6.0], dtype=np.float32),
        "max_health": np.array([20.0], dtype=np.float32),
        "food": 20,
        "saturation": np.array([5.0], dtype=np.float32),
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 111,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }

    previous = env._latched_start_snapshot(frame_a, base_state)
    current = env._latched_start_snapshot(
        frame_b,
        {
            **base_state,
            "yaw": 1.5,
            "world_fingerprint": 222,
        },
    )

    assert env._latched_start_changed_fields(previous, current) == [
        "frame_hash",
        "yaw",
        "world_fingerprint",
    ]


def test_step_sync_releases_start_latch_before_sending_action():
    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    release_calls: list[str] = []
    env._control_writer = object()
    env._read_control = lambda: {"start_latched": 1}
    env._release_start_latch = lambda: release_calls.append("released")
    env._send_action = lambda _action: None
    env.get_state_tick = lambda: 10
    env._read_state = lambda **_: {
        "position": np.zeros(3, dtype=np.float64),
        "yaw": 0.0,
        "pitch": 0.0,
        "health": np.zeros(1, dtype=np.float32),
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }
    env._wait_for_frame = lambda **_: np.zeros(
        (env.config.height, env.config.width, 3), dtype=np.uint8
    )
    env._wait_for_frame_at_tick = lambda _tick: np.zeros(
        (env.config.height, env.config.width, 3), dtype=np.uint8
    )
    env._wait_until_state_tick = lambda _target_tick: {
        "position": np.zeros(3, dtype=np.float64),
        "yaw": 0.0,
        "pitch": 0.0,
        "health": np.zeros(1, dtype=np.float32),
        "inventory": np.zeros((9, 2), dtype=np.int32),
    }

    env.step_sync({"camera": [0, 0]})

    assert release_calls == ["released"]


def _write_reward_block(
    writer: ShmemWriter,
    *,
    magic: int = REWARD_MAGIC,
    reward_delta: float = 0.0,
    reward_cumulative: float = 0.0,
    done: int = 0,
    truncated: int = 0,
    logs_broken: int = 0,
    episode_id: int = 1,
    steps_this_episode: int = 0,
) -> None:
    payload = struct.pack(
        "<IffIIIII",
        magic,
        float(reward_delta),
        float(reward_cumulative),
        int(done),
        int(truncated),
        int(logs_broken),
        int(episode_id),
        int(steps_this_episode),
    )
    writer.write(REWARD_OFFSET, payload)


def test_read_reward_block_returns_none_when_magic_missing(tmp_path: Path):
    path = tmp_path / "netherite_state_0"
    writer = ShmemWriter(str(path), STATE_SIZE)
    try:
        env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
        env._state_reader = writer  # ShmemWriter exposes read_bytes too
        assert env._read_reward_block() is None

        _write_reward_block(writer, magic=0xDEADBEEF)
        assert env._read_reward_block() is None
    finally:
        writer.close()


def test_read_reward_block_decodes_task_reward_fields(tmp_path: Path):
    path = tmp_path / "netherite_state_0"
    writer = ShmemWriter(str(path), STATE_SIZE)
    try:
        _write_reward_block(
            writer,
            reward_delta=1.0,
            reward_cumulative=3.5,
            done=1,
            truncated=0,
            logs_broken=3,
            episode_id=4,
            steps_this_episode=127,
        )

        env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
        env._state_reader = writer
        block = env._read_reward_block()
        assert block is not None
        assert block["reward_delta"] == 1.0
        assert block["reward_cumulative"] == 3.5
        assert block["done"] == 1
        assert block["truncated"] == 0
        assert block["logs_broken"] == 3
        assert block["episode_id"] == 4
        assert block["steps_this_episode"] == 127
    finally:
        writer.close()


def test_step_maps_reward_block_to_terminated_and_truncated(tmp_path: Path):
    path = tmp_path / "netherite_state_0"
    writer = ShmemWriter(str(path), STATE_SIZE)
    try:
        env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
        env._state_reader = writer
        env._send_action = lambda _action: None
        tick_val = {"value": 5}

        def fake_get_state_tick():
            tick_val["value"] += 1
            return tick_val["value"]

        env.get_state_tick = fake_get_state_tick
        env._read_state = lambda **_: {
            "position": np.zeros(3, dtype=np.float64),
            "yaw": 0.0,
            "pitch": 0.0,
            "health": np.zeros(1, dtype=np.float32),
            "inventory": np.zeros((9, 2), dtype=np.int32),
        }
        env._wait_for_frame = lambda **_: np.zeros(
            (env.config.height, env.config.width, 3), dtype=np.uint8
        )
        env._wait_for_frame_at_tick = lambda _t: np.zeros(
            (env.config.height, env.config.width, 3), dtype=np.uint8
        )

        # Death tick: done=1 truncated=0 => terminated
        _write_reward_block(
            writer,
            reward_delta=-0.0,
            reward_cumulative=2.0,
            done=1,
            truncated=0,
            logs_broken=2,
            episode_id=3,
            steps_this_episode=57,
        )
        _, r, terminated, truncated, info = env.step({"camera": [0, 0]})
        assert r == 0.0
        assert terminated is True
        assert truncated is False
        assert info["logs_broken"] == 2
        assert info["episode_id"] == 3
        assert info["steps_this_episode"] == 57

        # Truncation tick: done=1 truncated=1 => truncated only
        _write_reward_block(
            writer,
            reward_delta=1.0,
            reward_cumulative=5.0,
            done=1,
            truncated=1,
            logs_broken=5,
            episode_id=4,
            steps_this_episode=1000,
        )
        _, r, terminated, truncated, _ = env.step({"camera": [0, 0]})
        assert r == 1.0
        assert terminated is False
        assert truncated is True

        # Live tick: done=0 => neither flag set
        _write_reward_block(writer, reward_delta=0.25)
        _, r, terminated, truncated, _ = env.step({"camera": [0, 0]})
        assert r == 0.25
        assert terminated is False
        assert truncated is False
    finally:
        writer.close()


def test_get_debug_state_returns_extended_state_and_control(tmp_path: Path):
    writer = _make_control_writer(tmp_path, active_seed=424242)
    writer.write(40, struct.pack("<I", 7))

    env = NetheriteEnv(config=NetheriteConfig(), timeout=0.05)
    env._control_writer = writer
    env._read_state = lambda **_: {
        "position": np.array([1.0, 2.0, 3.0], dtype=np.float64),
        "yaw": 4.0,
        "pitch": 5.0,
        "health": np.array([6.0], dtype=np.float32),
        "max_health": np.array([20.0], dtype=np.float32),
        "food": 18,
        "saturation": np.array([3.5], dtype=np.float32),
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 123456,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "completed_render_chunks": 128,
        "total_render_chunks": 128,
        "world_sample": [7, 8, 9],
        "server_world_fingerprint": 0,
        "server_world_sample": [0, 0, 0],
        "inventory": np.array([[1, 2]] * 9, dtype=np.int32),
    }
    env.get_state_tick = lambda: 99

    debug_state = env.get_debug_state()

    assert debug_state == {
        "position": [1.0, 2.0, 3.0],
        "yaw": 4.0,
        "pitch": 5.0,
        "health": 6.0,
        "max_health": 20.0,
        "food": 18,
        "saturation": 3.5,
        "on_ground": 1,
        "in_water": 0,
        "world_fingerprint": 123456,
        "loaded_chunks": 25,
        "chunk_mask": 0x1FFFFFF,
        "actual_world_seed": 424242,
        "completed_render_chunks": 128,
        "total_render_chunks": 128,
        "world_sample": [7, 8, 9],
        "server_world_fingerprint": 0,
        "server_world_sample": [0, 0, 0],
        "inventory": [[1, 2]] * 9,
        "state_tick": 99,
        "control": {
            "magic": CONTROL_MAGIC,
            "request_id": 0,
            "ack_id": 0,
            "status": 0,
            "opcode": 0,
            "requested_seed": 0,
            "active_seed": 424242,
            "episode_id": 7,
            "start_latched": 0,
        },
    }

    writer.close()
