"""Netherite gym environment -- reads pixels + state from shmem, writes actions."""

import mmap
import os
import struct
import sys
import time
import hashlib

import gymnasium as gym
import numpy as np
from gymnasium import spaces

from config import NetheriteConfig
from startup_trace import startup_trace_enabled, trace_event
from sync import SemaphoreSync


def _shmem_path(name: str) -> str:
    if os.uname().sysname == "Darwin":
        return f"/tmp/{name}"
    return f"/dev/shm/{name}"


class ShmemReader:
    """Memory-mapped reader for observation and state buffers."""

    def __init__(self, path: str, size: int):
        fd = os.open(path, os.O_RDONLY)
        self.mm = mmap.mmap(fd, size, access=mmap.ACCESS_READ)
        os.close(fd)

    def read_header(self) -> tuple[int, int, int, int]:
        data = self.mm[:16]
        return struct.unpack("<IIII", data)

    def read_bytes(self, offset: int, length: int) -> bytes:
        return self.mm[offset : offset + length]

    def close(self):
        self.mm.close()


class ShmemWriter:
    """Memory-mapped writer for action and control buffers."""

    def __init__(self, path: str, size: int):
        if not os.path.exists(path):
            with open(path, "wb") as f:
                f.write(b"\x00" * size)
        fd = os.open(path, os.O_RDWR)
        self.mm = mmap.mmap(fd, size, access=mmap.ACCESS_WRITE)
        os.close(fd)

    def write(self, offset: int, data: bytes):
        self.mm[offset : offset + len(data)] = data

    def read_bytes(self, offset: int, length: int) -> bytes:
        return self.mm[offset : offset + length]

    def close(self):
        self.mm.close()


# Shmem layout constants
OBS_HEADER = 28  # magic(4) + frame(4) + size(4) + ready(4) + width(4) + height(4) + state_tick(4)
OBS_MAGIC = 0x4E455432
STATE_MAGIC = 0x4E455453
ACTION_MAGIC = 0x4E455441
CONTROL_MAGIC = 0x4E455443
OBS_SIZE = 8 * 1024 * 1024
STATE_SIZE = 64 * 1024
ACTION_SIZE = 4096
CONTROL_SIZE = 4096
WORLD_SAMPLE_RADIUS_XZ = 8
WORLD_SAMPLE_STEP_XZ = 2
WORLD_SAMPLE_MIN_DY = -2
WORLD_SAMPLE_MAX_DY = 6
WORLD_SAMPLE_COUNT_XZ = ((WORLD_SAMPLE_RADIUS_XZ * 2) // WORLD_SAMPLE_STEP_XZ) + 1
WORLD_SAMPLE_COUNT_Y = WORLD_SAMPLE_MAX_DY - WORLD_SAMPLE_MIN_DY + 1
WORLD_SAMPLE_COUNT = (
    WORLD_SAMPLE_COUNT_XZ * WORLD_SAMPLE_COUNT_XZ * WORLD_SAMPLE_COUNT_Y
)

CTRL_STATUS_IDLE = 0
CTRL_STATUS_BUSY = 1
CTRL_STATUS_DONE = 2
CTRL_STATUS_ERROR = 3
CTRL_OP_RESET_WORLD = 1
CTRL_OP_SET_POSE = 2
CTRL_OP_RELEASE_START = 3

# Task reward protocol -- mirrors TaskReward.java. Reward block is written
# into the state shmem at a fixed offset by the Java side; Python reads it on
# every step.
REWARD_OFFSET = 32768
REWARD_SIZE = 32
REWARD_MAGIC = 0x4E455252  # "NERR"


class NetheriteEnv(gym.Env):
    """Minecraft RL environment via shared memory.

    All game settings controlled through NetheriteConfig.
    Pass a config to control resolution, game rules, graphics,
    render distance, etc.
    """

    metadata = {"render_modes": ["rgb_array"]}

    def __init__(
        self,
        config: NetheriteConfig | None = None,
        timeout: float = 5.0,
    ):
        super().__init__()
        self.config = config or NetheriteConfig()
        self.timeout = timeout
        self.tick = 0

        w, h = self.config.width, self.config.height

        self.observation_space = spaces.Dict(
            {
                "pov": spaces.Box(0, 255, (h, w, 3), dtype=np.uint8),
                "inventory": spaces.Box(0, 64, (9, 2), dtype=np.int32),
                "health": spaces.Box(0, 20, (1,), dtype=np.float32),
                "position": spaces.Box(-1e6, 1e6, (3,), dtype=np.float64),
            }
        )
        self.action_space = spaces.Dict(
            {
                "forward": spaces.Discrete(2),
                "back": spaces.Discrete(2),
                "left": spaces.Discrete(2),
                "right": spaces.Discrete(2),
                "jump": spaces.Discrete(2),
                "sneak": spaces.Discrete(2),
                "sprint": spaces.Discrete(2),
                "attack": spaces.Discrete(2),
                "use": spaces.Discrete(2),
                "camera": spaces.Box(-180, 180, (2,), dtype=np.float32),
            }
        )

        self._obs_readers = [None, None]
        self._state_reader = None
        self._action_writer = None
        self._control_writer = None
        self._last_state_tick = -1
        self._last_frame_number = -1
        self._last_frame_state_tick = -1
        self._next_control_request_id = 0
        self._resize_index_cache: dict[
            tuple[int, int], tuple[np.ndarray, np.ndarray]
        ] = {}
        self._semaphore: SemaphoreSync | None = None

    def _debug_state_from_state(
        self,
        state: dict[str, object],
        *,
        control: dict[str, int] | None = None,
        state_tick: int | None = None,
    ) -> dict[str, object]:
        position = state.get("position", np.zeros(3, dtype=np.float64))
        health = state.get("health", np.zeros(1, dtype=np.float32))
        max_health = state.get("max_health", np.zeros(1, dtype=np.float32))
        saturation = state.get("saturation", np.zeros(1, dtype=np.float32))
        inventory = state.get("inventory", np.zeros((9, 2), dtype=np.int32))
        return {
            "position": [float(v) for v in position],
            "yaw": float(state.get("yaw", 0.0)),
            "pitch": float(state.get("pitch", 0.0)),
            "health": float(health[0]),
            "max_health": float(max_health[0]),
            "food": int(state.get("food", 0)),
            "saturation": float(saturation[0]),
            "on_ground": int(state.get("on_ground", 0)),
            "in_water": int(state.get("in_water", 0)),
            "world_fingerprint": int(state.get("world_fingerprint", 0)),
            "loaded_chunks": int(state.get("loaded_chunks", 0)),
            "chunk_mask": int(state.get("chunk_mask", 0)),
            "actual_world_seed": int(state.get("actual_world_seed", 0)),
            "completed_render_chunks": int(state.get("completed_render_chunks", 0)),
            "total_render_chunks": int(state.get("total_render_chunks", 0)),
            "world_sample": [int(v) for v in state.get("world_sample", [])],
            "server_world_fingerprint": int(state.get("server_world_fingerprint", 0)),
            "server_world_sample": [
                int(v) for v in state.get("server_world_sample", [])
            ],
            "inventory": inventory.tolist(),
            "state_tick": self.get_state_tick()
            if state_tick is None
            else int(state_tick),
            "control": control,
        }

    def _latched_start_signature(
        self,
        frame: np.ndarray,
        state: dict[str, object],
    ) -> tuple[tuple[float, float, float], float, float, int, int, int, int]:
        snapshot = self._latched_start_snapshot(frame, state)
        return self._latched_start_signature_from_snapshot(snapshot)

    def _latched_start_snapshot(
        self,
        frame: np.ndarray,
        state: dict[str, object],
    ) -> dict[str, object]:
        position = state.get("position", np.zeros(3, dtype=np.float64))
        return {
            "frame_hash": hashlib.blake2b(frame.tobytes(), digest_size=16).hexdigest(),
            "position": tuple(round(float(v), 4) for v in position),
            "yaw": round(float(state.get("yaw", 0.0)), 4),
            "pitch": round(float(state.get("pitch", 0.0)), 4),
            "world_fingerprint": int(state.get("world_fingerprint", 0)),
            "loaded_chunks": int(state.get("loaded_chunks", 0)),
            "chunk_mask": int(state.get("chunk_mask", 0)),
            "actual_world_seed": int(state.get("actual_world_seed", 0)),
        }

    @staticmethod
    def _latched_start_signature_from_snapshot(
        snapshot: dict[str, object],
    ) -> tuple[tuple[float, float, float], float, float, int, int, int, int]:
        return (
            tuple(snapshot["position"]),  # type: ignore[arg-type]
            float(snapshot["yaw"]),
            float(snapshot["pitch"]),
            int(snapshot["world_fingerprint"]),
            int(snapshot["loaded_chunks"]),
            int(snapshot["chunk_mask"]),
            int(snapshot["actual_world_seed"]),
        )

    @staticmethod
    def _latched_start_changed_fields(
        previous: dict[str, object],
        current: dict[str, object],
    ) -> list[str]:
        return [key for key in previous if previous[key] != current[key]]

    def _connect(self):
        iid = self.config.instance_id
        trace_event("env.reset.connect.begin", instance_id=iid)
        self._obs_readers[0] = ShmemReader(
            _shmem_path(f"netherite_obs_{iid}_A"), OBS_SIZE
        )
        self._obs_readers[1] = ShmemReader(
            _shmem_path(f"netherite_obs_{iid}_B"), OBS_SIZE
        )
        self._state_reader = ShmemReader(
            _shmem_path(f"netherite_state_{iid}"), STATE_SIZE
        )
        self._action_writer = ShmemWriter(
            _shmem_path(f"netherite_action_{iid}"), ACTION_SIZE
        )
        self._control_writer = ShmemWriter(
            _shmem_path(f"netherite_control_{iid}"), CONTROL_SIZE
        )
        # Initialize semaphore if enabled
        if self.config.use_semaphore:
            self._semaphore = SemaphoreSync(iid)
            self._semaphore.open()
        trace_event("env.reset.connect.done", instance_id=iid)

    def _wait_for_frame(self, wait_for_new: bool = False) -> np.ndarray:
        """Poll both obs buffers, return pixels from whichever has latest frame."""
        target_w, target_h = self.config.width, self.config.height
        deadline = time.monotonic() + self.timeout
        best_frame = -1
        best_slot = 0
        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print(
                f"_wait_for_frame: timeout={self.timeout!r} wait_for_new={wait_for_new}",
                file=sys.stderr,
                flush=True,
            )

        loop_start = time.monotonic()
        last_log = loop_start
        while time.monotonic() < deadline:
            if os.environ.get("NETHERITE_DEBUG_RESET"):
                now = time.monotonic()
                if now - last_log >= 15.0:
                    print(
                        f"_wait_for_frame: poll {now - loop_start:.1f}s (magic/ready never matched)",
                        file=sys.stderr,
                        flush=True,
                    )
                    last_log = now
            for slot in range(2):
                magic, frame, data_size, ready, _, _, _ = self._read_obs_header(
                    self._obs_readers[slot]
                )
                if (
                    magic == OBS_MAGIC
                    and ready == 1
                    and frame > best_frame
                    and (not wait_for_new or frame > self._last_frame_number)
                ):
                    best_frame = frame
                    best_slot = slot
            if best_frame >= 0:
                break
            time.sleep(0.001)  # 1ms

        if best_frame < 0:
            if os.environ.get("NETHERITE_DEBUG_RESET"):
                print(
                    "_wait_for_frame: no frame, returning zeros",
                    file=sys.stderr,
                    flush=True,
                )
            return np.zeros((target_h, target_w, 3), dtype=np.uint8)

        reader = self._obs_readers[best_slot]
        _, _, data_size, _, frame_w, frame_h, frame_state_tick = self._read_obs_header(
            reader
        )
        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print(
                f"_wait_for_frame: got frame best={best_frame} size={data_size} "
                f"wh={frame_w}x{frame_h} state_tick={frame_state_tick}",
                file=sys.stderr,
                flush=True,
            )
        pixel_bytes = reader.read_bytes(OBS_HEADER, data_size)
        frame = self._decode_frame(pixel_bytes, frame_w, frame_h)
        self._last_frame_number = best_frame
        self._last_frame_state_tick = frame_state_tick
        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print("_wait_for_frame: decode done", file=sys.stderr, flush=True)
        return frame

    def _wait_for_frame_at_tick(self, target_tick: int) -> np.ndarray:
        """Wait for a frame whose embedded state_tick >= target_tick."""
        deadline = time.monotonic() + self.timeout

        while time.monotonic() < deadline:
            best_frame = -1
            best_slot = 0
            best_tick = -1
            for slot in range(2):
                magic, frame, data_size, ready, _, _, state_tick = (
                    self._read_obs_header(self._obs_readers[slot])
                )
                if (
                    magic == OBS_MAGIC
                    and ready == 1
                    and state_tick >= target_tick
                    and frame > best_frame
                ):
                    best_frame = frame
                    best_slot = slot
                    best_tick = state_tick
            if best_frame >= 0:
                reader = self._obs_readers[best_slot]
                _, _, data_size, _, frame_w, frame_h, _ = self._read_obs_header(reader)
                pixel_bytes = reader.read_bytes(OBS_HEADER, data_size)
                frame = self._decode_frame(pixel_bytes, frame_w, frame_h)
                self._last_frame_number = best_frame
                self._last_frame_state_tick = best_tick
                return frame
            time.sleep(0.001)  # 1ms

        frame = self._wait_for_frame(wait_for_new=True)
        return frame

    def get_last_frame_state_tick(self) -> int:
        """Return the state tick embedded in the last frame read."""
        return self._last_frame_state_tick

    def _read_obs_header(
        self, reader: ShmemReader
    ) -> tuple[int, int, int, int, int, int, int]:
        """Returns (magic, frame, data_size, ready, width, height, state_tick)."""
        data = reader.read_bytes(0, OBS_HEADER)
        if len(data) < OBS_HEADER:
            return 0, 0, 0, 0, 0, 0, 0
        return struct.unpack("<IIIIIII", data)

    def _decode_frame(
        self, pixel_bytes: bytes, frame_w: int, frame_h: int
    ) -> np.ndarray:
        target_w, target_h = self.config.width, self.config.height
        expected = frame_w * frame_h * 4
        if frame_w <= 0 or frame_h <= 0 or len(pixel_bytes) < expected:
            return np.zeros((target_h, target_w, 3), dtype=np.uint8)

        rgba = np.frombuffer(pixel_bytes[:expected], dtype=np.uint8).reshape(
            frame_h, frame_w, 4
        )
        rgb = rgba[::-1, :, :3]
        if frame_w == target_w and frame_h == target_h:
            return rgb.copy()

        y_idx, x_idx = self._resize_indices(frame_w, frame_h)
        return rgb[y_idx][:, x_idx, :].copy()

    def _resize_indices(
        self, frame_w: int, frame_h: int
    ) -> tuple[np.ndarray, np.ndarray]:
        key = (frame_w, frame_h)
        cached = self._resize_index_cache.get(key)
        if cached is not None:
            return cached

        x_idx = np.linspace(0, frame_w - 1, self.config.width, dtype=np.int32)
        y_idx = np.linspace(0, frame_h - 1, self.config.height, dtype=np.int32)
        self._resize_index_cache[key] = (y_idx, x_idx)
        return y_idx, x_idx

    def _read_state(self, wait_for_new: bool = False) -> dict:
        """Read player state from state buffer."""
        reader = self._state_reader
        deadline = time.monotonic() + self.timeout
        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print("_read_state: start", file=sys.stderr, flush=True)

        while True:
            magic, tick, data_size, ready = reader.read_header()
            if magic == STATE_MAGIC and ready == 1:
                if not wait_for_new or tick > self._last_state_tick:
                    self._last_state_tick = tick
                    break
            if time.monotonic() > deadline:
                return {
                    "position": np.zeros(3, dtype=np.float64),
                    "yaw": 0.0,
                    "pitch": 0.0,
                    "health": np.zeros(1, dtype=np.float32),
                    "max_health": np.zeros(1, dtype=np.float32),
                    "food": 0,
                    "saturation": np.zeros(1, dtype=np.float32),
                    "on_ground": 0,
                    "in_water": 0,
                    "world_fingerprint": 0,
                    "loaded_chunks": 0,
                    "chunk_mask": 0,
                    "actual_world_seed": 0,
                    "completed_render_chunks": 0,
                    "total_render_chunks": 0,
                    "world_sample": [0] * WORLD_SAMPLE_COUNT,
                    "server_world_fingerprint": 0,
                    "server_world_sample": [0] * WORLD_SAMPLE_COUNT,
                    "inventory": np.zeros((9, 2), dtype=np.int32),
                }
            time.sleep(0.001)

        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print("_read_state: got packet", file=sys.stderr, flush=True)
        state_bytes = reader.read_bytes(16, 56)
        x, y, z = struct.unpack_from("<ddd", state_bytes, 0)
        yaw = struct.unpack_from("<f", state_bytes, 24)[0]
        pitch = struct.unpack_from("<f", state_bytes, 28)[0]
        health = struct.unpack_from("<f", state_bytes, 32)[0]
        max_health = struct.unpack_from("<f", state_bytes, 36)[0]
        food = struct.unpack_from("<i", state_bytes, 40)[0]
        saturation = struct.unpack_from("<f", state_bytes, 44)[0]
        on_ground = struct.unpack_from("<i", state_bytes, 48)[0]
        in_water = struct.unpack_from("<i", state_bytes, 52)[0]

        inv_bytes = reader.read_bytes(72, 72)
        inventory = np.zeros((9, 2), dtype=np.int32)
        for i in range(9):
            item_id, count = struct.unpack_from("<ii", inv_bytes, i * 8)
            inventory[i] = [item_id, count]

        world_bytes = reader.read_bytes(144, 32)
        world_fingerprint = struct.unpack_from("<Q", world_bytes, 0)[0]
        loaded_chunks = struct.unpack_from("<i", world_bytes, 8)[0]
        chunk_mask = struct.unpack_from("<i", world_bytes, 12)[0]
        actual_world_seed = struct.unpack_from("<q", world_bytes, 16)[0]
        completed_render_chunks = struct.unpack_from("<i", world_bytes, 24)[0]
        total_render_chunks = struct.unpack_from("<i", world_bytes, 28)[0]
        sample_count = struct.unpack_from("<i", reader.read_bytes(176, 4), 0)[0]
        sample_count = max(0, min(sample_count, WORLD_SAMPLE_COUNT))
        world_sample_offset = 180
        world_sample_bytes = reader.read_bytes(world_sample_offset, sample_count * 4)
        world_sample = (
            list(struct.unpack(f"<{sample_count}i", world_sample_bytes))
            if sample_count
            else []
        )

        # Server-side sample starts after client sample
        server_sample_offset = world_sample_offset + (sample_count * 4)
        server_bytes = reader.read_bytes(server_sample_offset, 8 + (sample_count * 4))
        server_world_fingerprint = struct.unpack_from("<Q", server_bytes, 0)[0]
        server_world_sample = (
            list(struct.unpack_from(f"<{sample_count}i", server_bytes, 8))
            if sample_count
            else []
        )

        return {
            "position": np.array([x, y, z], dtype=np.float64),
            "yaw": float(yaw),
            "pitch": float(pitch),
            "health": np.array([health], dtype=np.float32),
            "max_health": np.array([max_health], dtype=np.float32),
            "food": int(food),
            "saturation": np.array([saturation], dtype=np.float32),
            "on_ground": int(on_ground),
            "in_water": int(in_water),
            "world_fingerprint": int(world_fingerprint),
            "loaded_chunks": int(loaded_chunks),
            "chunk_mask": int(chunk_mask),
            "actual_world_seed": int(actual_world_seed),
            "completed_render_chunks": int(completed_render_chunks),
            "total_render_chunks": int(total_render_chunks),
            "world_sample": world_sample,
            "server_world_fingerprint": int(server_world_fingerprint),
            "server_world_sample": server_world_sample,
            "inventory": inventory,
        }

    def _send_action(self, action: dict):
        """Write action to shmem."""
        self.tick += 1
        payload = struct.pack(
            "<III",
            ACTION_MAGIC,
            self.tick,
            11,
        )
        payload += struct.pack("<I", 0)  # ready=0

        keys = struct.pack(
            "<BBBBBBBBB",
            int(action.get("forward", 0)),
            int(action.get("back", 0)),
            int(action.get("left", 0)),
            int(action.get("right", 0)),
            int(action.get("jump", 0)),
            int(action.get("sneak", 0)),
            int(action.get("sprint", 0)),
            int(action.get("attack", 0)),
            int(action.get("use", 0)),
        )
        camera = action.get("camera", np.zeros(2, dtype=np.float32))
        camera_bytes = struct.pack(
            "<bb",
            max(-127, min(127, int(camera[0]))),
            max(-127, min(127, int(camera[1]))),
        )

        self._action_writer.write(0, payload)
        self._action_writer.write(16, keys + camera_bytes)
        self._action_writer.write(12, struct.pack("<I", 1))

    @staticmethod
    def _pose_matches(
        current_pose: dict[str, float],
        target_pose: dict[str, float],
        *,
        pos_tol: float = 0.01,
        ang_tol: float = 0.01,
    ) -> bool:
        return (
            abs(current_pose["x"] - target_pose["x"]) < pos_tol
            and abs(current_pose["y"] - target_pose["y"]) < pos_tol
            and abs(current_pose["z"] - target_pose["z"]) < pos_tol
            and abs(current_pose["yaw"] - target_pose["yaw"]) < ang_tol
            and abs(current_pose["pitch"] - target_pose["pitch"]) < ang_tol
        )

    def get_player_pose(self) -> dict[str, float]:
        state = self._read_state()
        return {
            "x": float(state["position"][0]),
            "y": float(state["position"][1]),
            "z": float(state["position"][2]),
            "yaw": float(state["yaw"]),
            "pitch": float(state["pitch"]),
        }

    def get_state_tick(self) -> int:
        if self._state_reader is None:
            return -1
        _, tick, _, _ = self._state_reader.read_header()
        return int(tick)

    def get_debug_state(self) -> dict[str, object]:
        state = self._read_state()
        control = self._read_control() if self._control_writer is not None else None
        return self._debug_state_from_state(state, control=control)

    def align_to_pose(self, pose: dict[str, float]) -> dict:
        self._request_pose(pose)

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            obs = self._get_obs(wait_for_new_state=True, wait_for_new_frame=True)
            if self._pose_matches(self.get_player_pose(), pose):
                return obs

        raise TimeoutError("Timed out waiting for Netherite pose alignment")

    def _read_reward_block(self) -> dict[str, float | int] | None:
        """Return the TaskReward shmem block, or None if the Java side is not
        writing it (magic mismatch -- task=none)."""
        if self._state_reader is None:
            return None
        data = self._state_reader.read_bytes(REWARD_OFFSET, REWARD_SIZE)
        if len(data) < REWARD_SIZE:
            return None
        magic = struct.unpack_from("<I", data, 0)[0]
        if magic != REWARD_MAGIC:
            return None
        reward_delta = struct.unpack_from("<f", data, 4)[0]
        reward_cumulative = struct.unpack_from("<f", data, 8)[0]
        done = struct.unpack_from("<I", data, 12)[0]
        truncated = struct.unpack_from("<I", data, 16)[0]
        logs_broken = struct.unpack_from("<I", data, 20)[0]
        episode_id = struct.unpack_from("<I", data, 24)[0]
        steps_this_episode = struct.unpack_from("<I", data, 28)[0]
        return {
            "reward_delta": float(reward_delta),
            "reward_cumulative": float(reward_cumulative),
            "done": int(done),
            "truncated": int(truncated),
            "logs_broken": int(logs_broken),
            "episode_id": int(episode_id),
            "steps_this_episode": int(steps_this_episode),
        }

    def _read_control(self) -> dict[str, int]:
        buf = self._control_writer.read_bytes(0, 48)
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

    def _next_request_id(self, control: dict[str, int]) -> int:
        request_id = (
            max(
                self._next_control_request_id,
                control["request_id"],
                control["ack_id"],
            )
            + 1
        )
        self._next_control_request_id = request_id
        return request_id

    def _wait_for_control_ready(self) -> dict[str, int]:
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            control = self._read_control()
            if control["magic"] == CONTROL_MAGIC:
                return control
            time.sleep(0.001)
        raise TimeoutError("Timed out waiting for Netherite control buffer")

    def _request_world_reset(self, world_seed: int):
        control = self._wait_for_control_ready()
        request_id = self._next_request_id(control)
        trace_event(
            "control.reset.request.sent",
            instance_id=self.config.instance_id,
            request_id=request_id,
            seed=world_seed,
        )

        self._control_writer.write(16, struct.pack("<I", CTRL_OP_RESET_WORLD))
        self._control_writer.write(24, struct.pack("<q", int(world_seed)))
        self._control_writer.write(12, struct.pack("<I", CTRL_STATUS_BUSY))
        self._control_writer.write(4, struct.pack("<I", request_id))

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            control = self._read_control()
            if control["ack_id"] == request_id:
                if control["status"] == CTRL_STATUS_DONE:
                    self.config.seed = control["active_seed"]
                    trace_event(
                        "control.reset.ack_done",
                        instance_id=self.config.instance_id,
                        request_id=request_id,
                        active_seed=control["active_seed"],
                    )
                    return
                if control["status"] == CTRL_STATUS_ERROR:
                    trace_event(
                        "control.reset.ack_error",
                        instance_id=self.config.instance_id,
                        request_id=request_id,
                    )
                    raise RuntimeError("Netherite world reset failed")
            time.sleep(0.001)

        trace_event(
            "control.reset.timeout",
            instance_id=self.config.instance_id,
            request_id=request_id,
            seed=world_seed,
        )
        raise TimeoutError("Timed out waiting for Netherite world reset")

    def _request_pose(self, pose: dict[str, float]):
        control = self._wait_for_control_ready()
        request_id = self._next_request_id(control)

        self._control_writer.write(16, struct.pack("<I", CTRL_OP_SET_POSE))
        self._control_writer.write(48, struct.pack("<d", float(pose["x"])))
        self._control_writer.write(56, struct.pack("<d", float(pose["y"])))
        self._control_writer.write(64, struct.pack("<d", float(pose["z"])))
        self._control_writer.write(72, struct.pack("<f", float(pose["yaw"])))
        self._control_writer.write(76, struct.pack("<f", float(pose["pitch"])))
        self._control_writer.write(12, struct.pack("<I", CTRL_STATUS_BUSY))
        self._control_writer.write(4, struct.pack("<I", request_id))

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            control = self._read_control()
            if control["ack_id"] == request_id:
                if control["status"] == CTRL_STATUS_DONE:
                    return
                if control["status"] == CTRL_STATUS_ERROR:
                    raise RuntimeError("Netherite pose request failed")
            time.sleep(0.001)

        raise TimeoutError("Timed out waiting for Netherite pose request")

    def _release_start_latch(self):
        control = self._wait_for_control_ready()
        if control["start_latched"] == 0:
            return

        request_id = self._next_request_id(control)
        trace_event(
            "start_latch.release.request.sent",
            instance_id=self.config.instance_id,
            request_id=request_id,
        )
        self._control_writer.write(16, struct.pack("<I", CTRL_OP_RELEASE_START))
        self._control_writer.write(12, struct.pack("<I", CTRL_STATUS_BUSY))
        self._control_writer.write(4, struct.pack("<I", request_id))

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            control = self._read_control()
            if control["ack_id"] == request_id:
                if control["status"] == CTRL_STATUS_DONE:
                    trace_event(
                        "start_latch.release.ack_done",
                        instance_id=self.config.instance_id,
                        request_id=request_id,
                    )
                    return
                if control["status"] == CTRL_STATUS_ERROR:
                    trace_event(
                        "start_latch.release.ack_error",
                        instance_id=self.config.instance_id,
                        request_id=request_id,
                    )
                    raise RuntimeError("Netherite start latch release failed")
            time.sleep(0.001)

        trace_event(
            "start_latch.release.timeout",
            instance_id=self.config.instance_id,
            request_id=request_id,
        )
        raise TimeoutError("Timed out waiting for Netherite start latch release")

    def release_start_latch(self):
        self._release_start_latch()

    def wait_for_start_latch(
        self,
        *,
        stable_frames: int = 8,
        max_frames: int = 2048,
    ) -> dict:
        deadline = time.monotonic() + self.timeout
        next_status = time.monotonic() + 10.0
        trace_event(
            "start_latch.wait.begin",
            instance_id=self.config.instance_id,
            stable_frames=stable_frames,
            max_frames=max_frames,
        )
        while time.monotonic() < deadline:
            if self._read_control()["start_latched"] == 1:
                trace_event(
                    "start_latch.armed",
                    instance_id=self.config.instance_id,
                )
                break
            now = time.monotonic()
            if now >= next_status:
                print(
                    "NetheriteEnv: still waiting for start_latched (Java WorldController must arm latch)…",
                    file=sys.stderr,
                    flush=True,
                )
                next_status = now + 10.0
            time.sleep(0.001)
        else:
            trace_event("start_latch.timeout", instance_id=self.config.instance_id)
            raise TimeoutError("Timed out waiting for Netherite start latch")

        last_hash = None
        last_snapshot = None
        stable_count = 0
        obs = None
        change_counts: dict[str, int] = {}
        change_examples: list[dict[str, tuple[object, object]]] = []
        trace_event(
            "start_latch.stable_frame.wait.begin",
            instance_id=self.config.instance_id,
            stable_frames=stable_frames,
            max_frames=max_frames,
        )
        for _ in range(max_frames):
            frame = self._wait_for_frame(wait_for_new=False)
            state = self._read_state(wait_for_new=False)
            obs = {
                "pov": frame,
                "inventory": state["inventory"],
                "health": state["health"],
                "position": state["position"],
            }
            snapshot = self._latched_start_snapshot(frame, state)
            signature = self._latched_start_signature_from_snapshot(snapshot)
            if last_snapshot is not None and startup_trace_enabled():
                changed_fields = self._latched_start_changed_fields(
                    last_snapshot, snapshot
                )
                if changed_fields:
                    for field in changed_fields:
                        change_counts[field] = change_counts.get(field, 0) + 1
                    if len(change_examples) < 8:
                        change_examples.append(
                            {
                                field: (last_snapshot[field], snapshot[field])
                                for field in changed_fields
                            }
                        )
            if signature == last_hash:
                stable_count += 1
            else:
                last_hash = signature
                last_snapshot = snapshot
                stable_count = 1
            if stable_count >= stable_frames:
                trace_event(
                    "start_latch.stable_frame.ready",
                    instance_id=self.config.instance_id,
                    stable_count=stable_count,
                )
                return obs
            time.sleep(0.01)

        trace_event(
            "start_latch.stable_frame.timeout",
            instance_id=self.config.instance_id,
            stable_frames=stable_frames,
            max_frames=max_frames,
            change_counts=change_counts,
            change_examples=change_examples,
        )
        raise TimeoutError("Timed out waiting for a stable latched start frame")

    def _get_obs(
        self,
        *,
        wait_for_new_state: bool = False,
        wait_for_new_frame: bool = False,
    ) -> dict:
        pov = self._wait_for_frame(wait_for_new=wait_for_new_frame)
        state = self._read_state(wait_for_new=wait_for_new_state)
        return {
            "pov": pov,
            "inventory": state["inventory"],
            "health": state["health"],
            "position": state["position"],
        }

    def _wait_for_state_ticks(self, ticks: int) -> dict:
        state = None
        for _ in range(max(0, int(ticks))):
            state = self._read_state(wait_for_new=True)
        if state is None:
            state = self._read_state()
        return state

    def _wait_until_state_tick(self, target_tick: int) -> dict:
        deadline = time.monotonic() + self.timeout
        last_tick = self._last_state_tick
        if startup_trace_enabled():
            last_tick = self.get_state_tick()
            trace_event(
                "state_tick.wait.begin",
                instance_id=self.config.instance_id,
                start_tick=last_tick,
                target_tick=target_tick,
                semaphore_enabled=self._semaphore is not None,
            )

        # Fast path: use semaphore if enabled
        if self._semaphore is not None:
            while time.monotonic() < deadline:
                current_tick = self.get_state_tick()
                last_tick = current_tick
                if current_tick >= target_tick:
                    trace_event(
                        "state_tick.wait.reached",
                        instance_id=self.config.instance_id,
                        current_tick=current_tick,
                        target_tick=target_tick,
                    )
                    return self._read_state()
                # Wait for Java to signal (blocks until sem_post)
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    self._semaphore.wait(timeout=min(remaining, 0.1))
            trace_event(
                "state_tick.wait.timeout",
                instance_id=self.config.instance_id,
                target_tick=target_tick,
                last_tick=last_tick,
            )
            raise TimeoutError(f"Timed out waiting for state_tick >= {target_tick}")

        # Fallback: polling
        while time.monotonic() < deadline:
            current_tick = self.get_state_tick()
            last_tick = current_tick
            if current_tick >= target_tick:
                trace_event(
                    "state_tick.wait.reached",
                    instance_id=self.config.instance_id,
                    current_tick=current_tick,
                    target_tick=target_tick,
                )
                return self._read_state()
            if target_tick - current_tick > 8:
                time.sleep(0.0002)  # 200μs when far behind

        trace_event(
            "state_tick.wait.timeout",
            instance_id=self.config.instance_id,
            target_tick=target_tick,
            last_tick=last_tick,
        )
        raise TimeoutError(f"Timed out waiting for state_tick >= {target_tick}")

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        trace_event(
            "env.reset.begin",
            instance_id=self.config.instance_id,
            requested_seed=seed,
            options=options,
        )
        if self._action_writer is None:
            self._connect()
        self.tick = 0
        options = options or {}

        reset_world = bool(options.get("reset_world", False))
        world_seed = options.get("seed")
        if world_seed is None and reset_world and seed is not None:
            world_seed = seed
        if world_seed is not None:
            reset_world = True

        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print(
                "NetheriteEnv.reset: fetching first obs …", file=sys.stderr, flush=True
            )
        if reset_world:
            target_seed = self.config.seed if world_seed is None else int(world_seed)
            trace_event(
                "env.reset.world_reset.begin",
                instance_id=self.config.instance_id,
                target_seed=target_seed,
            )
            self._request_world_reset(target_seed)
            trace_event(
                "env.reset.world_reset.done",
                instance_id=self.config.instance_id,
                target_seed=target_seed,
            )
            obs = self._get_obs(wait_for_new_state=True, wait_for_new_frame=True)
        else:
            obs = self._get_obs()
        if os.environ.get("NETHERITE_DEBUG_RESET"):
            print("NetheriteEnv.reset: first obs ok", file=sys.stderr, flush=True)
        trace_event("env.reset.first_obs.done", instance_id=self.config.instance_id)
        return obs, {}

    def _step_impl(self, action, *, wait_for_new_frame: bool, state_delta: int):
        if (
            self._control_writer is not None
            and self._read_control()["start_latched"] == 1
        ):
            self._release_start_latch()
        start_tick = self.get_state_tick()
        self._send_action(action)
        target_tick = start_tick + max(1, int(state_delta))
        state = self._wait_until_state_tick(target_tick)
        end_tick = self.get_state_tick()

        # Skip frame reading in voxels-only mode
        needs_pixels = self.config.obs_mode in ("pixels", "both")
        if needs_pixels:
            if wait_for_new_frame:
                pov = self._wait_for_frame_at_tick(end_tick)
            else:
                pov = self._wait_for_frame(wait_for_new=False)
            frame_state_tick = self._last_frame_state_tick
        else:
            # Voxels-only mode: return empty frame
            pov = np.zeros((self.config.height, self.config.width, 3), dtype=np.uint8)
            frame_state_tick = end_tick

        obs = {
            "pov": pov,
            "inventory": state["inventory"],
            "health": state["health"],
            "position": state["position"],
        }
        reward = 0.0
        terminated = False
        truncated = False
        reward_block = self._read_reward_block()
        if reward_block is not None:
            reward = reward_block["reward_delta"]
            # A done==1 tick with truncated==0 is a real terminal (death).
            terminated = bool(reward_block["done"]) and not bool(
                reward_block["truncated"]
            )
            truncated = bool(reward_block["truncated"])
        control = self._read_control() if self._control_writer is not None else None
        info = {
            "send_state_tick": start_tick,
            "state_tick": end_tick,
            "frame_state_tick": frame_state_tick,
            "debug_state": self._debug_state_from_state(
                state,
                control=control,
                state_tick=end_tick,
            ),
        }
        if reward_block is not None:
            info["reward_cumulative"] = reward_block["reward_cumulative"]
            info["logs_broken"] = reward_block["logs_broken"]
            info["episode_id"] = reward_block["episode_id"]
            info["steps_this_episode"] = reward_block["steps_this_episode"]
        return obs, reward, terminated, truncated, info

    def step(self, action):
        return self._step_impl(
            action,
            wait_for_new_frame=False,
            state_delta=self.config.step_ticks,
        )

    def step_sync(self, action):
        return self._step_impl(
            action,
            wait_for_new_frame=True,
            state_delta=self.config.step_ticks,
        )

    def step_for_ticks(self, action, ticks: int, *, wait_for_new_frame: bool = True):
        return self._step_impl(
            action,
            wait_for_new_frame=wait_for_new_frame,
            state_delta=max(1, int(ticks)),
        )

    def advance_ticks(self, ticks: int):
        if (
            self._control_writer is not None
            and self._read_control()["start_latched"] == 1
        ):
            self._release_start_latch()
        if int(ticks) <= 0:
            tick = self.get_state_tick()
            return {"send_state_tick": tick, "state_tick": tick}
        self._wait_for_state_ticks(int(ticks))
        tick = self.get_state_tick()
        return {"send_state_tick": tick, "state_tick": tick}

    def close(self):
        for r in self._obs_readers:
            if r is not None:
                r.close()
        if self._state_reader is not None:
            self._state_reader.close()
        if self._action_writer is not None:
            self._action_writer.close()
        if self._control_writer is not None:
            self._control_writer.close()
        if self._semaphore is not None:
            self._semaphore.close()
