"""ctypes wrapper for the batched mine-coal env (rl/blaze).

VecBlaze(n, device=0, backend="auto", output_device="host") selects an
explicit cpu/cuda/metal backend (BLAZE_BACKEND is also honored). The Metal
backend is correctness-first hybrid execution: exact host double simulation,
Metal semantic camera, then host finalization after a command-buffer fence.
Passing so_path remains supported and selects the backend from the filename.

Action tensor: EITHER
  - legacy 5-head int [N,5] act_dict indices (dyaw, dpitch, forward, jump,
    attack) - expanded here to the full layout with the exact old numeric
    decode (bit-identical trainer semantics), OR
  - full float64 [N,13] raw action rows in blaze_tick_raw order:
    {forward, strafe, dyaw(deg), dpitch(deg), jump, sneak, sprint, attack,
     use, hotbar(0..8 or -1), craft(rl_crafts 0..12 or -1), interact(0/1),
    smelt(0/1)}. dyaw/dpitch apply on sub-tick 0 only; craft/interact/smelt
    fire once, pre-tick. Legacy [N,12] rows (pre-iron) are zero-padded to 13.
Buffers:
  cuda backend: torch tensors on cuda:<device>; step() passes .data_ptr()
                straight into the kernels (zero-copy). actions must be a
                tensor on the same device.
  metal backend: persistent MTLStorageModeShared NumPy views. With
                 output_device="mps", step explicitly copies those views
                 into persistent MPS tensors and records transfer timings.
                 synchronize_mps=False queues the copies; a policy action
                 copied back to the host provides the normal step fence.
                 Shared NumPy views are valid only while the VecBlaze handle
                 remains open; do not retain or access them after close().
  cpu backend : numpy arrays (torch tensors are converted).
Shapes: cam int16 [N,36,64] (u16 block ids reinterpreted; ids < 4096 so the
sign bit never sets), depth/edge uint8 [N,36,64], scal float32 [N,6],
rew float32 [N], done uint8 [N], pose float32 [N,5] (x,y,z,yaw,pitch).
Chain extras (blaze_step_full / capture / success-item, 2026-07-15):
  env.status int32 [N,23] = {9 rl_inv_ids counts (log, planks, stick,
    cobble, table, w.pick, s.pick, coal, torch), hotbar_sel, held item id,
    container, dig permille, furnace, iron ore, iron ingot, iron pick,
    diamond, diamond pick/shovel/axe/hoe/sword},
    refreshed by every step().
  env.set_success_item(id): which item count increase fires +10/done=1
    (263 default = legacy coal, 50 = torches, 0 = never; applies at reset).
  env.capture(env_idx, slot): snapshot a live env into snapshot slot
    (self-generated start-state curriculum); slot appends at n_snaps or
    overwrites - keep a fixed slot->seed discipline (see blaze_capture).

Usage:
    env = VecBlaze(1024, device=0)
    env.load_snapshots(sorted(glob.glob(".../rl/out/snaps/*.bsnp")))
    env.assign([i % env.n_snaps for i in range(env.n)])
    env.reset()
    cam, depth, edge, scal, rew, done, pose = env.step(actions, repeat=4)
"""
import ctypes
import os
import platform
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CUDA_SO = os.path.join(HERE, "blaze_cuda.so")
CPU_SO = os.path.join(HERE, "blaze_cpu.so")
METAL_DYLIB = os.path.join(HERE, "blaze_metal.dylib")

CAM_H, CAM_W, NPIX = 36, 64, 36 * 64
NSCAL, NPOSE, NHEADS = 6, 5, 5   # NHEADS = legacy trainer head count
NACT = 13                        # full raw action row (blaze_tick_raw order)
NACT_LEGACY = 12                 # pre-iron rows, zero-padded (smelt=0)
NSTATUS = 23                     # CU_STATUS_K (blaze_fill_status layout)
YAW_STEPS = (-15.0, 0.0, 15.0)   # act_dict head decode (cu_yaw_step)
PITCH_STEPS = (-10.0, 0.0, 10.0)
OP_NAMES = (                     # blaze_core.h CU_OP_* order (op_trace cols)
    "world_load", "world_edit", "recenter", "fill_cell", "raycast",
    "ray_step", "dig_tick", "dig_break", "item_tick", "craft", "interact",
    "smelt", "furnace_tick", "phys_tick", "coal_call", "coal_rebuild",
    "coal_sweep", "inv_scan", "subtick")


class _BackendInfo(ctypes.Structure):
    _fields_ = [
        ("version", ctypes.c_uint32), ("backend", ctypes.c_uint32),
        ("n_envs", ctypes.c_uint32), ("n_snapshots", ctypes.c_uint32),
        ("recommended_working_set", ctypes.c_uint64),
        ("memory_budget", ctypes.c_uint64),
        ("metal_buffer_bytes", ctypes.c_uint64),
        ("host_snapshot_bytes", ctypes.c_uint64),
        ("max_buffer_length", ctypes.c_uint64),
        ("allocation_count", ctypes.c_uint64),
        ("last_tick_ms", ctypes.c_double),
        ("last_camera_ms", ctypes.c_double),
        ("device_name", ctypes.c_char * 128),
    ]


_METAL_OUTPUT = {
    "cam": 0, "depth": 1, "edge": 2, "scal": 3, "rew": 4,
    "done": 5, "pose": 6, "status": 7,
}


class VecBlaze:
    def __init__(self, n, device=0, so_path=None, backend="auto",
                 output_device=None, synchronize_mps=True):
        if int(n) <= 0:
            raise ValueError(f"n must be positive, got {n}")
        requested = (backend or "auto").lower()
        if so_path is None:
            if requested == "auto":
                requested = os.environ.get("BLAZE_BACKEND", "auto").lower()
            if requested not in ("auto", "cpu", "cuda", "metal"):
                raise ValueError(
                    "backend/BLAZE_BACKEND must be auto, cpu, cuda, or metal")
            if requested == "auto":
                requested = self._auto_backend(device)
            so_path = {"cpu": CPU_SO, "cuda": CUDA_SO,
                       "metal": METAL_DYLIB}[requested]
        else:
            inferred = self._backend_from_path(so_path)
            if requested not in ("auto", inferred):
                raise ValueError(
                    f"backend={requested!r} conflicts with so_path={so_path!r}")
            requested = inferred
        if not os.path.exists(so_path):
            raise RuntimeError(
                f"{requested} backend library is missing: {so_path}")
        self.so_path = so_path
        self.backend = requested
        self.n = int(n)
        self.device = device
        self.n_snaps = 0
        self.output_device = output_device or "host"
        self.synchronize_mps = bool(synchronize_mps)
        if self.backend == "metal" and self.output_device not in ("host", "mps"):
            raise ValueError("Metal output_device must be 'host' or 'mps'")
        if self.backend != "metal" and self.output_device not in ("host", None):
            raise ValueError("output_device is only configurable for Metal")
        self.transfer_stats = {
            "steps": 0, "action_seconds": 0.0, "action_bytes": 0,
            "observation_seconds": 0.0, "observation_bytes": 0,
            "observation_syncs": 0,
        }
        self._mps_pending = False
        self.lib = ctypes.CDLL(so_path)
        self.lib.blaze_create.restype = ctypes.c_void_p
        self.lib.blaze_create.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.blaze_destroy.argtypes = [ctypes.c_void_p]
        self.lib.blaze_load_snapshots.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int,
            ctypes.c_char_p, ctypes.c_int]
        self.lib.blaze_snapshot_has_liquid.argtypes = [ctypes.c_void_p,
                                                       ctypes.c_int]
        self.lib.blaze_assign.argtypes = [ctypes.c_void_p,
                                          ctypes.POINTER(ctypes.c_int)]
        self.lib.blaze_set_reward_gate.argtypes = [ctypes.c_void_p,
                                                   ctypes.c_double]
        self.lib.blaze_reset.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.lib.blaze_step.argtypes = [ctypes.c_void_p] + \
            [ctypes.c_void_p, ctypes.c_int] + [ctypes.c_void_p] * 7
        # step arg order: (h, actions, repeat, cam, depth, edge, scal, rew,
        #                  done, pose)
        self.lib.blaze_step.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_void_p, ctypes.c_void_p]
        self.lib.blaze_step_full.argtypes = \
            self.lib.blaze_step.argtypes + [ctypes.c_void_p]
        self.lib.blaze_set_success_item.argtypes = [ctypes.c_void_p,
                                                    ctypes.c_int]
        self.lib.blaze_capture.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                           ctypes.c_int]
        self.lib.blaze_op_count.restype = ctypes.c_int
        self.lib.blaze_op_trace.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        if hasattr(self.lib, "blaze_last_create_error"):
            self.lib.blaze_last_create_error.restype = ctypes.c_char_p
        if hasattr(self.lib, "blaze_last_error"):
            self.lib.blaze_last_error.argtypes = [ctypes.c_void_p]
            self.lib.blaze_last_error.restype = ctypes.c_char_p
        if self.backend == "metal":
            self.lib.blaze_output_ptr.argtypes = [ctypes.c_void_p, ctypes.c_int]
            self.lib.blaze_output_ptr.restype = ctypes.c_void_p
            self.lib.blaze_output_bytes.argtypes = [ctypes.c_void_p,
                                                    ctypes.c_int]
            self.lib.blaze_output_bytes.restype = ctypes.c_size_t
            self.lib.blaze_get_backend_info.argtypes = [
                ctypes.c_void_p, ctypes.POINTER(_BackendInfo)]
        self.h = self.lib.blaze_create(device, self.n)
        if not self.h:
            detail = ""
            if hasattr(self.lib, "blaze_last_create_error"):
                raw = self.lib.blaze_last_create_error()
                detail = f": {raw.decode(errors='replace')}" if raw else ""
            raise RuntimeError(
                f"blaze_create(device={device}, n={self.n}) failed "
                f"[{so_path}]{detail}")

        if self.backend == "cuda":
            import torch
            dev = torch.device(f"cuda:{device}")
            self.torch = torch
            self.cam = torch.zeros((self.n, CAM_H, CAM_W), dtype=torch.int16,
                                   device=dev)
            self.depth = torch.zeros((self.n, CAM_H, CAM_W), dtype=torch.uint8,
                                     device=dev)
            self.edge = torch.zeros((self.n, CAM_H, CAM_W), dtype=torch.uint8,
                                    device=dev)
            self.scal = torch.zeros((self.n, NSCAL), dtype=torch.float32,
                                    device=dev)
            self.rew = torch.zeros(self.n, dtype=torch.float32, device=dev)
            self.done = torch.zeros(self.n, dtype=torch.uint8, device=dev)
            self.pose = torch.zeros((self.n, NPOSE), dtype=torch.float32,
                                    device=dev)
            self.status = torch.zeros((self.n, NSTATUS), dtype=torch.int32,
                                      device=dev)
        elif self.backend == "metal":
            import numpy as np
            self.np = np
            self._shared_keepalive = []
            self._host_cam = self._metal_array(
                "cam", ctypes.c_uint16, np.uint16,
                (self.n, CAM_H, CAM_W)).view(np.int16)
            self._host_depth = self._metal_array(
                "depth", ctypes.c_uint8, np.uint8,
                (self.n, CAM_H, CAM_W))
            self._host_edge = self._metal_array(
                "edge", ctypes.c_uint8, np.uint8,
                (self.n, CAM_H, CAM_W))
            self._host_scal = self._metal_array(
                "scal", ctypes.c_float, np.float32, (self.n, NSCAL))
            self._host_rew = self._metal_array(
                "rew", ctypes.c_float, np.float32, (self.n,))
            self._host_done = self._metal_array(
                "done", ctypes.c_uint8, np.uint8, (self.n,))
            self._host_pose = self._metal_array(
                "pose", ctypes.c_float, np.float32, (self.n, NPOSE))
            self._host_status = self._metal_array(
                "status", ctypes.c_int32, np.int32, (self.n, NSTATUS))
            self.host_outputs = {
                "cam": self._host_cam, "depth": self._host_depth,
                "edge": self._host_edge, "scal": self._host_scal,
                "rew": self._host_rew, "done": self._host_done,
                "pose": self._host_pose, "status": self._host_status,
            }
            if self.output_device == "host":
                self.cam, self.depth, self.edge = (
                    self._host_cam, self._host_depth, self._host_edge)
                self.scal, self.rew, self.done = (
                    self._host_scal, self._host_rew, self._host_done)
                self.pose, self.status = self._host_pose, self._host_status
            else:
                import torch
                if not torch.backends.mps.is_available():
                    self.close()
                    raise RuntimeError(
                        "output_device='mps' requested but PyTorch MPS is unavailable")
                self.torch = torch
                dev = torch.device("mps")
                self.cam = torch.empty((self.n, CAM_H, CAM_W),
                                       dtype=torch.int16, device=dev)
                self.depth = torch.empty((self.n, CAM_H, CAM_W),
                                         dtype=torch.uint8, device=dev)
                self.edge = torch.empty((self.n, CAM_H, CAM_W),
                                        dtype=torch.uint8, device=dev)
                self.scal = torch.empty((self.n, NSCAL),
                                        dtype=torch.float32, device=dev)
                self.rew = torch.empty(self.n, dtype=torch.float32, device=dev)
                self.done = torch.empty(self.n, dtype=torch.uint8, device=dev)
                self.pose = torch.empty((self.n, NPOSE),
                                        dtype=torch.float32, device=dev)
                self.status = torch.empty((self.n, NSTATUS),
                                          dtype=torch.int32, device=dev)
                self._mps_copies = (
                    (self.cam, torch.from_numpy(self._host_cam)),
                    (self.depth, torch.from_numpy(self._host_depth)),
                    (self.edge, torch.from_numpy(self._host_edge)),
                    (self.scal, torch.from_numpy(self._host_scal)),
                    (self.rew, torch.from_numpy(self._host_rew)),
                    (self.done, torch.from_numpy(self._host_done)),
                    (self.pose, torch.from_numpy(self._host_pose)),
                    (self.status, torch.from_numpy(self._host_status)),
                )
                self._observation_bytes = sum(src.numel() * src.element_size()
                                              for _, src in self._mps_copies)
        else:
            import numpy as np
            self.np = np
            self.cam = np.zeros((self.n, CAM_H, CAM_W), dtype=np.int16)
            self.depth = np.zeros((self.n, CAM_H, CAM_W), dtype=np.uint8)
            self.edge = np.zeros((self.n, CAM_H, CAM_W), dtype=np.uint8)
            self.scal = np.zeros((self.n, NSCAL), dtype=np.float32)
            self.rew = np.zeros(self.n, dtype=np.float32)
            self.done = np.zeros(self.n, dtype=np.uint8)
            self.pose = np.zeros((self.n, NPOSE), dtype=np.float32)
            self.status = np.zeros((self.n, NSTATUS), dtype=np.int32)

    @staticmethod
    def _backend_from_path(path):
        base = os.path.basename(os.fspath(path)).lower()
        if "metal" in base or base.endswith(".dylib"):
            return "metal"
        if "cuda" in base:
            return "cuda"
        return "cpu"

    @staticmethod
    def _auto_backend(device):
        if platform.system() == "Darwin" and os.path.exists(METAL_DYLIB):
            try:
                lib = ctypes.CDLL(METAL_DYLIB)
                lib.blaze_metal_available.argtypes = [
                    ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
                err = ctypes.create_string_buffer(256)
                if lib.blaze_metal_available(int(device), err, len(err)):
                    return "metal"
            except (OSError, AttributeError):
                pass
        if os.path.exists(CUDA_SO):
            try:
                import torch
                if torch.cuda.is_available():
                    return "cuda"
            except ImportError:
                pass
        return "cpu"

    def _metal_array(self, name, ctype, dtype, shape):
        kind = _METAL_OUTPUT[name]
        ptr = self.lib.blaze_output_ptr(self.h, kind)
        expected = int(self.np.prod(shape)) * self.np.dtype(dtype).itemsize
        actual = self.lib.blaze_output_bytes(self.h, kind)
        if not ptr or actual != expected:
            self.close()
            raise RuntimeError(
                f"invalid Metal shared output {name}: ptr={ptr}, "
                f"bytes={actual}, expected={expected}")
        raw = (ctype * int(self.np.prod(shape))).from_address(ptr)
        self._shared_keepalive.append(raw)
        return self.np.ctypeslib.as_array(raw).reshape(shape)

    def _ptr(self, buf):
        if self.backend == "cuda":
            return ctypes.c_void_p(buf.data_ptr())
        return ctypes.c_void_p(buf.ctypes.data)

    def load_snapshots(self, paths):
        err = ctypes.create_string_buffer(512)
        arr = (ctypes.c_char_p * len(paths))(
            *[p.encode() for p in paths])
        r = self.lib.blaze_load_snapshots(self.h, arr, len(paths), err, 512)
        if r < 0:
            raise RuntimeError(f"blaze_load_snapshots: {err.value.decode()}")
        self.n_snaps = r
        return r

    def snapshot_has_liquid(self, i):
        return self.lib.blaze_snapshot_has_liquid(self.h, i)

    def set_success_item(self, item):
        """Which item count increase fires the in-kernel +10/done=1: 263
        (default) = legacy coal, 50 = torches (full chain), 0 = never.
        Applies to envs at their NEXT reset."""
        if self.lib.blaze_set_success_item(self.h, int(item)) != 0:
            raise RuntimeError("blaze_set_success_item failed")

    def capture(self, env_idx, slot):
        """Snapshot live env `env_idx` into snapshot slot `slot` (appends at
        n_snaps or overwrites; see blaze_capture's slot discipline note)."""
        r = self.lib.blaze_capture(self.h, int(env_idx), int(slot))
        if r != 0:
            raise RuntimeError(f"blaze_capture({env_idx}, {slot}) failed")
        if slot == self.n_snaps:
            self.n_snaps += 1

    def op_trace(self):
        """Cumulative per-env op-trace counters as uint64 [N, CU_OP_N]
        (see blaze_core.h CU_OP_* order). Requires BLAZE_OP_TRACE=1 in the
        environment BEFORE this VecBlaze was created; returns None when
        tracing is off. Counters accumulate across steps AND resets."""
        import numpy as np
        nop = self.lib.blaze_op_count()
        out = np.zeros((self.n, nop), dtype=np.uint64)
        r = self.lib.blaze_op_trace(self.h,
                                    ctypes.c_void_p(out.ctypes.data))
        return out if r == 0 else None

    def set_reward_gate(self, dist):
        """OPT-IN training-reward mode: require nearest-coal dist <= dist
        for the +0.03 crosshair-attack bonus. dist <= 0 = off (default,
        exact ppo_coal reward semantics)."""
        if self.lib.blaze_set_reward_gate(self.h, float(dist)) != 0:
            raise RuntimeError("blaze_set_reward_gate failed")

    def assign(self, snap_idx):
        if hasattr(snap_idx, "detach"):
            snap_idx = snap_idx.detach().cpu().tolist()
        values = list(snap_idx)
        if len(values) != self.n:
            raise ValueError(
                f"snapshot assignments must have length {self.n}, got "
                f"{len(values)}")
        arr = (ctypes.c_int * self.n)(*values)
        if self.lib.blaze_assign(self.h, arr) != 0:
            raise RuntimeError("blaze_assign failed")

    def reset(self, mask=None):
        """mask: host iterable/array of n 0/1 bytes, or None for all."""
        m = None
        if mask is not None:
            if hasattr(mask, "detach"):
                mask = mask.detach().cpu().tolist()
            values = list(mask)
            if len(values) != self.n:
                raise ValueError(
                    f"reset mask must have length {self.n}, got {len(values)}")
            if any(x != 0 and x != 1 for x in values):
                raise ValueError("reset mask values must be 0 or 1")
            b = bytes(bytearray(int(x) for x in values))
            m = ctypes.c_char_p(b)
        if self.lib.blaze_reset(self.h, m) != 0:
            detail = ""
            if hasattr(self.lib, "blaze_last_error"):
                raw = self.lib.blaze_last_error(self.h)
                detail = f": {raw.decode(errors='replace')}" if raw else ""
            raise RuntimeError(f"blaze_reset failed{detail}")

    def _expand_heads(self, a5):
        """Legacy [N,5] act_dict head indices -> full float64 [N,13] rows,
        with the exact numeric decode the pre-port C core used (values are
        bit-identical: -15/0/15 deg yaw, -10/0/10 pitch, 0/1 flags,
        hotbar/craft -1, everything else 0)."""
        if self.backend == "cuda":
            t = self.torch
            a5 = a5.to(self.cam.device)
            self._validate_cuda_heads(a5)
            a5 = a5.long()
            full = t.zeros((a5.shape[0], NACT), dtype=t.float64,
                           device=self.cam.device)
            full[:, 2] = t.tensor(YAW_STEPS, dtype=t.float64,
                                  device=self.cam.device)[a5[:, 0]]
            full[:, 3] = t.tensor(PITCH_STEPS, dtype=t.float64,
                                  device=self.cam.device)[a5[:, 1]]
            full[:, 0] = a5[:, 2].double()
            full[:, 4] = a5[:, 3].double()
            full[:, 7] = a5[:, 4].double()
        else:
            np = self.np
            a5 = np.asarray(a5)
            self._validate_host_heads(a5)
            a5 = a5.astype(np.int64, copy=False)
            full = np.zeros((a5.shape[0], NACT), dtype=np.float64)
            full[:, 2] = np.array(YAW_STEPS)[a5[:, 0]]
            full[:, 3] = np.array(PITCH_STEPS)[a5[:, 1]]
            full[:, 0] = a5[:, 2]
            full[:, 4] = a5[:, 3]
            full[:, 7] = a5[:, 4]
        full[:, 9] = -1.0    # hotbar: unchanged
        full[:, 10] = -1.0   # craft: none
        return full

    def _validate_host_heads(self, actions):
        np = self.np
        try:
            finite = np.isfinite(actions).all()
        except TypeError as exc:
            raise ValueError("legacy action heads must be numeric") from exc
        if not finite:
            raise ValueError("legacy action heads must contain only finite values")
        if not np.isin(actions[:, :2], (0, 1, 2)).all():
            raise ValueError("dyaw and dpitch heads must be integers in 0..2")
        if not np.isin(actions[:, 2:], (0, 1)).all():
            raise ValueError("forward, jump, and attack heads must be 0 or 1")

    def _validate_cuda_heads(self, actions):
        t = self.torch
        if actions.is_floating_point() and not t.isfinite(actions).all().item():
            raise ValueError("legacy action heads must contain only finite values")
        turns = actions[:, :2]
        binary = actions[:, 2:]
        if not (((turns == 0) | (turns == 1) | (turns == 2)).all().item()):
            raise ValueError("dyaw and dpitch heads must be integers in 0..2")
        if not (((binary == 0) | (binary == 1)).all().item()):
            raise ValueError("forward, jump, and attack heads must be 0 or 1")

    def _validate_host_actions(self, actions):
        np = self.np
        if not np.isfinite(actions).all():
            raise ValueError("actions must contain only finite values")
        if ((actions[:, 0] < -1.0).any() or
                (actions[:, 0] > 1.0).any() or
                (actions[:, 1] < -1.0).any() or
                (actions[:, 1] > 1.0).any()):
            raise ValueError("forward and strafe actions must be in [-1, 1]")
        fmax = np.finfo(np.float32).max
        if ((np.abs(actions[:, 2]) > fmax).any() or
                (np.abs(actions[:, 3]) > fmax).any()):
            raise ValueError("dyaw and dpitch actions must fit finite float32")
        binary = actions[:, (4, 5, 6, 7, 8, 11, 12)]
        if not ((binary == 0.0) | (binary == 1.0)).all():
            raise ValueError(
                "jump/sneak/sprint/attack/use/interact/smelt must be 0 or 1")
        if not np.isin(actions[:, 9], np.arange(-1, 9)).all():
            raise ValueError("hotbar actions must be -1 or an integer in 0..8")
        if not np.isin(actions[:, 10], np.arange(-1, 13)).all():
            raise ValueError("craft actions must be -1 or an integer in 0..12")

    def _validate_cuda_actions(self, actions):
        t = self.torch
        if not t.isfinite(actions).all().item():
            raise ValueError("actions must contain only finite values")
        if (((actions[:, :2] < -1.0) | (actions[:, :2] > 1.0)).any().item()):
            raise ValueError("forward and strafe actions must be in [-1, 1]")
        fmax = t.finfo(t.float32).max
        if (t.abs(actions[:, 2:4]) > fmax).any().item():
            raise ValueError("dyaw and dpitch actions must fit finite float32")
        binary = actions[:, (4, 5, 6, 7, 8, 11, 12)]
        if not (((binary == 0.0) | (binary == 1.0)).all().item()):
            raise ValueError(
                "jump/sneak/sprint/attack/use/interact/smelt must be 0 or 1")
        hotbar = actions[:, 9]
        hotbar_ok = ((hotbar == -1.0) |
                     ((hotbar >= 0.0) & (hotbar <= 8.0) &
                      (hotbar == t.trunc(hotbar))))
        if not hotbar_ok.all().item():
            raise ValueError("hotbar actions must be -1 or an integer in 0..8")
        craft = actions[:, 10]
        craft_ok = ((craft == -1.0) |
                    ((craft >= 0.0) & (craft <= 12.0) &
                     (craft == t.trunc(craft))))
        if not craft_ok.all().item():
            raise ValueError("craft actions must be -1 or an integer in 0..12")

    def step(self, actions, repeat=4):
        """actions: legacy int [N,5] head indices OR full float64 [N,12/13]
        raw action rows (see module docstring; 12-wide rows are zero-padded
        with smelt=0). cuda backend: torch tensor on this device; cpu
        backend: numpy array (contiguous)."""
        if self.backend == "cuda":
            t = self.torch
            if not isinstance(actions, t.Tensor):
                actions = t.as_tensor(actions)
            if actions.ndim != 2 or actions.shape[0] != self.n:
                raise ValueError(
                    f"actions must have shape [{self.n},5/12/13], got "
                    f"{tuple(actions.shape)}")
            if actions.shape[-1] == NHEADS:
                actions = self._expand_heads(actions)
            elif actions.shape[-1] == NACT_LEGACY:
                actions = t.cat([actions.double(), t.zeros(
                    (actions.shape[0], NACT - NACT_LEGACY),
                    dtype=t.float64, device=actions.device)], dim=1)
            elif actions.shape[-1] != NACT:
                raise ValueError(
                    f"actions width must be {NHEADS}, {NACT_LEGACY}, or "
                    f"{NACT}, got {actions.shape[-1]}")
            actions = actions.to(self.cam.device, t.float64).contiguous()
            self._validate_cuda_actions(actions)
            act_ptr = ctypes.c_void_p(actions.data_ptr())
        else:
            transfer_start = time.perf_counter()
            accelerator_input = hasattr(actions, "detach")
            if accelerator_input:
                action_was_mps = getattr(actions.device, "type", None) == "mps"
                actions = actions.detach().to("cpu").numpy()
                if action_was_mps:
                    self._mps_pending = False
            actions = self.np.asarray(actions)
            if actions.ndim != 2 or actions.shape[0] != self.n:
                raise ValueError(
                    f"actions must have shape [{self.n},5/12/13], got "
                    f"{actions.shape}")
            if actions.shape[-1] == NHEADS:
                actions = self._expand_heads(actions)
            elif actions.shape[-1] == NACT_LEGACY:
                actions = self.np.concatenate(
                    [actions, self.np.zeros(
                        (actions.shape[0], NACT - NACT_LEGACY))], axis=1)
            elif actions.shape[-1] != NACT:
                raise ValueError(
                    f"actions width must be {NHEADS}, {NACT_LEGACY}, or "
                    f"{NACT}, got {actions.shape[-1]}")
            actions = self.np.ascontiguousarray(actions,
                                                dtype=self.np.float64)
            self._validate_host_actions(actions)
            act_ptr = ctypes.c_void_p(actions.ctypes.data)
            if self.backend == "metal":
                if self._mps_pending:
                    self.torch.mps.synchronize()
                    self.transfer_stats["observation_syncs"] += 1
                    self._mps_pending = False
                self.transfer_stats["action_seconds"] += (
                    time.perf_counter() - transfer_start)
                if accelerator_input:
                    self.transfer_stats["action_bytes"] += actions.nbytes
        if self.backend == "metal":
            out = (self._host_cam, self._host_depth, self._host_edge,
                   self._host_scal, self._host_rew, self._host_done,
                   self._host_pose, self._host_status)
        else:
            out = (self.cam, self.depth, self.edge, self.scal, self.rew,
                   self.done, self.pose, self.status)
        r = self.lib.blaze_step_full(
            self.h, act_ptr, repeat, *[self._ptr(x) for x in out])
        if r != 0:
            detail = ""
            if hasattr(self.lib, "blaze_last_error"):
                raw = self.lib.blaze_last_error(self.h)
                detail = f": {raw.decode(errors='replace')}" if raw else ""
            raise RuntimeError(f"blaze_step failed{detail}")
        if self.backend == "metal":
            self.transfer_stats["steps"] += 1
            if self.output_device == "mps":
                transfer_start = time.perf_counter()
                with self.torch.no_grad():
                    for dst, src in self._mps_copies:
                        dst.copy_(src, non_blocking=not self.synchronize_mps)
                if self.synchronize_mps:
                    self.torch.mps.synchronize()
                    self.transfer_stats["observation_syncs"] += 1
                    self._mps_pending = False
                else:
                    self._mps_pending = True
                self.transfer_stats["observation_seconds"] += (
                    time.perf_counter() - transfer_start)
                self.transfer_stats["observation_bytes"] += (
                    self._observation_bytes)
        self._act_keepalive = actions   # kernels read it until the sync
        return (self.cam, self.depth, self.edge, self.scal, self.rew,
                self.done, self.pose)

    def sync_outputs(self):
        """Fence queued Metal-to-MPS observation copies, if any."""
        if self.backend == "metal" and self.output_device == "mps":
            self.torch.mps.synchronize()
            self.transfer_stats["observation_syncs"] += 1
            self._mps_pending = False

    def backend_info(self):
        """Metal allocation/timing metadata, or a small generic summary."""
        if self.backend != "metal":
            return {"backend": self.backend, "n_envs": self.n}
        info = _BackendInfo()
        if self.lib.blaze_get_backend_info(self.h, ctypes.byref(info)) != 0:
            raise RuntimeError("blaze_get_backend_info failed")
        return {
            "backend": "metal-hybrid", "n_envs": info.n_envs,
            "n_snapshots": info.n_snapshots,
            "device_name": bytes(info.device_name).split(b"\0", 1)[0].decode(),
            "recommended_working_set": info.recommended_working_set,
            "memory_budget": info.memory_budget,
            "metal_buffer_bytes": info.metal_buffer_bytes,
            "host_snapshot_bytes": info.host_snapshot_bytes,
            "max_buffer_length": info.max_buffer_length,
            "allocation_count": info.allocation_count,
            "last_tick_ms": info.last_tick_ms,
            "last_camera_ms": info.last_camera_ms,
        }

    def close(self):
        """Release native state; Metal shared NumPy views are invalid after this."""
        if getattr(self, "h", None):
            if getattr(self, "_mps_pending", False):
                self.sync_outputs()
            self.lib.blaze_destroy(self.h)
            self.h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
