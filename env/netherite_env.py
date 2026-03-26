"""Netherite gym environment -- reads pixels + state from shmem, writes actions."""

import mmap
import os
import struct
import time

import gymnasium as gym
import numpy as np
from gymnasium import spaces


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
    """Memory-mapped writer for action buffer."""

    def __init__(self, path: str, size: int):
        # Create file if it doesn't exist
        if not os.path.exists(path):
            with open(path, "wb") as f:
                f.write(b"\x00" * size)
        fd = os.open(path, os.O_RDWR)
        self.mm = mmap.mmap(fd, size, access=mmap.ACCESS_WRITE)
        os.close(fd)

    def write(self, offset: int, data: bytes):
        self.mm[offset : offset + len(data)] = data

    def close(self):
        self.mm.close()


# Shmem layout constants
OBS_HEADER = 16
OBS_MAGIC = 0x4E455432
STATE_MAGIC = 0x4E455453
ACTION_MAGIC = 0x4E455441
OBS_SIZE = 8 * 1024 * 1024
STATE_SIZE = 64 * 1024
ACTION_SIZE = 4096


class NetheriteEnv(gym.Env):
    """Minecraft RL environment via shared memory.

    Reads pixels from FrameGrabber, game state from StateExporter,
    and writes actions to ActionInjector.
    """

    metadata = {"render_modes": ["rgb_array"]}

    def __init__(
        self,
        instance_id: int = 0,
        width: int = 854,
        height: int = 480,
        timeout: float = 5.0,
    ):
        super().__init__()
        self.instance_id = instance_id
        self.width = width
        self.height = height
        self.timeout = timeout
        self.tick = 0

        self.observation_space = spaces.Dict(
            {
                "pov": spaces.Box(0, 255, (height, width, 3), dtype=np.uint8),
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

    def _connect(self):
        prefix = "netherite"
        iid = self.instance_id
        self._obs_readers[0] = ShmemReader(
            _shmem_path(f"{prefix}_obs_{iid}_A"), OBS_SIZE
        )
        self._obs_readers[1] = ShmemReader(
            _shmem_path(f"{prefix}_obs_{iid}_B"), OBS_SIZE
        )
        self._state_reader = ShmemReader(
            _shmem_path(f"{prefix}_state_{iid}"), STATE_SIZE
        )
        self._action_writer = ShmemWriter(
            _shmem_path(f"{prefix}_action_{iid}"), ACTION_SIZE
        )

    def _wait_for_frame(self) -> np.ndarray:
        """Poll both obs buffers, return pixels from whichever has latest frame."""
        deadline = time.monotonic() + self.timeout
        best_frame = -1
        best_slot = 0

        while time.monotonic() < deadline:
            for slot in range(2):
                magic, frame, data_size, ready = self._obs_readers[slot].read_header()
                if magic == OBS_MAGIC and ready == 1 and frame > best_frame:
                    best_frame = frame
                    best_slot = slot
            if best_frame >= 0:
                break
            time.sleep(0.001)

        if best_frame < 0:
            # Return black frame on timeout
            return np.zeros((self.height, self.width, 3), dtype=np.uint8)

        reader = self._obs_readers[best_slot]
        _, _, data_size, _ = reader.read_header()
        pixel_bytes = reader.read_bytes(OBS_HEADER, data_size)
        rgba = np.frombuffer(pixel_bytes, dtype=np.uint8).reshape(
            self.height, self.width, 4
        )
        # GL is bottom-up, flip Y; drop alpha
        rgb = rgba[::-1, :, :3].copy()
        return rgb

    def _read_state(self) -> dict:
        """Read player state from state buffer."""
        reader = self._state_reader
        magic, tick, data_size, ready = reader.read_header()
        if magic != STATE_MAGIC or ready == 0:
            return {
                "position": np.zeros(3, dtype=np.float64),
                "health": np.zeros(1, dtype=np.float32),
                "inventory": np.zeros((9, 2), dtype=np.int32),
            }

        state_bytes = reader.read_bytes(16, 56)
        x, y, z = struct.unpack_from("<ddd", state_bytes, 0)
        yaw, pitch = struct.unpack_from("<ff", state_bytes, 24)
        health, max_health = struct.unpack_from("<ff", state_bytes, 32)
        food_level = struct.unpack_from("<i", state_bytes, 40)[0]

        inv_bytes = reader.read_bytes(72, 72)
        inventory = np.zeros((9, 2), dtype=np.int32)
        for i in range(9):
            item_id, count = struct.unpack_from("<ii", inv_bytes, i * 8)
            inventory[i] = [item_id, count]

        return {
            "position": np.array([x, y, z], dtype=np.float64),
            "health": np.array([health], dtype=np.float32),
            "inventory": inventory,
        }

    def _send_action(self, action: dict):
        """Write action to shmem."""
        self.tick += 1
        payload = struct.pack(
            "<III",
            ACTION_MAGIC,
            self.tick,
            11,  # data_size
        )
        # Will set ready=0 first, write action, then set ready=1
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
        # Set ready last
        self._action_writer.write(12, struct.pack("<I", 1))

    def _get_obs(self) -> dict:
        pov = self._wait_for_frame()
        state = self._read_state()
        return {
            "pov": pov,
            "inventory": state["inventory"],
            "health": state["health"],
            "position": state["position"],
        }

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        if self._action_writer is None:
            self._connect()
        self.tick = 0
        obs = self._get_obs()
        return obs, {}

    def step(self, action):
        self._send_action(action)
        obs = self._get_obs()
        reward = 0.0
        terminated = False
        truncated = False
        info = {}
        return obs, reward, terminated, truncated, info

    def close(self):
        for r in self._obs_readers:
            if r is not None:
                r.close()
        if self._state_reader is not None:
            self._state_reader.close()
        if self._action_writer is not None:
            self._action_writer.close()
