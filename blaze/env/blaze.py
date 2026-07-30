"""ctypes wrapper for the batched mine-coal env (blaze/env).

VecBlaze(n, device=0, so_path=None) auto-detects the backend: blaze_cuda.so
(if present and torch sees a GPU) else blaze_cpu.so - both export the same C
ABI (blaze_step takes double actions[N][13] since the iron extension).

Action tensor: EITHER
  - legacy 5-head int [N,5] act_dict indices (dyaw, dpitch, forward, jump,
    attack) - expanded here to the full layout with the exact old numeric
    decode (bit-identical trainer semantics), OR
  - full float64 [N,13] raw action rows in blaze_tick_raw order:
    {forward, strafe, dyaw(deg), dpitch(deg), jump, sneak, sprint, attack,
     use, hotbar(0..8 or -1), craft(rl_crafts 0..7 or -1), interact(0/1),
    smelt(0/1)}. dyaw/dpitch apply on sub-tick 0 only; craft/interact/smelt
    fire once, pre-tick. Legacy [N,12] rows (pre-iron) are zero-padded to 13.
Buffers:
  cuda backend: torch tensors on cuda:<device>; step() passes .data_ptr()
                straight into the kernels (zero-copy). actions must be a
                tensor on the same device.
  cpu backend : numpy arrays (torch tensors are converted).
Shapes: cam int16 [N,36,64] (u16 block ids reinterpreted; ids < 4096 so the
sign bit never sets), depth/edge uint8 [N,36,64], scal float32 [N,6],
rew float32 [N], done uint8 [N], pose float32 [N,5] (x,y,z,yaw,pitch).
Chain extras (blaze_step_full / capture / success-item, 2026-07-15):
  env.status int32 [N,17] = {9 rl_inv_ids counts (log, planks, stick,
    cobble, table, w.pick, s.pick, coal, torch), hotbar_sel, held item id,
    container, dig permille, furnace, iron ore, iron ingot, iron pick},
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

HERE = os.path.dirname(os.path.abspath(__file__))
CUDA_SO = os.path.join(HERE, "blaze_cuda.so")
CPU_SO = os.path.join(HERE, "blaze_cpu.so")

CAM_H, CAM_W, NPIX = 36, 64, 36 * 64
NSCAL, NPOSE, NHEADS = 6, 5, 5   # NHEADS = legacy trainer head count
NACT = 13                        # full raw action row (blaze_tick_raw order)
NACT_LEGACY = 12                 # pre-iron rows, zero-padded (smelt=0)
NSTATUS = 17                     # CU_STATUS_K (blaze_fill_status layout)
YAW_STEPS = (-15.0, 0.0, 15.0)   # act_dict head decode (cu_yaw_step)
PITCH_STEPS = (-10.0, 0.0, 10.0)
OP_NAMES = (                     # blaze_core.h CU_OP_* order (op_trace cols)
    "world_load", "world_edit", "recenter", "fill_cell", "raycast",
    "ray_step", "dig_tick", "dig_break", "item_tick", "craft", "interact",
    "smelt", "furnace_tick", "phys_tick", "coal_call", "coal_rebuild",
    "coal_sweep", "inv_scan", "subtick")


class VecBlaze:
    def __init__(self, n, device=0, so_path=None):
        if so_path is None:
            so_path = CPU_SO
            if os.path.exists(CUDA_SO):
                try:
                    import torch
                    if torch.cuda.is_available():
                        so_path = CUDA_SO
                except ImportError:
                    pass
        self.so_path = so_path
        self.backend = "cuda" if "cuda" in os.path.basename(so_path) else "cpu"
        self.n = n
        self.device = device
        self.n_snaps = 0
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
        self.h = self.lib.blaze_create(device, n)
        if not self.h:
            raise RuntimeError(
                f"blaze_create(device={device}, n={n}) failed [{so_path}]")

        if self.backend == "cuda":
            import torch
            dev = torch.device(f"cuda:{device}")
            self.torch = torch
            self.cam = torch.zeros((n, CAM_H, CAM_W), dtype=torch.int16,
                                   device=dev)
            self.depth = torch.zeros((n, CAM_H, CAM_W), dtype=torch.uint8,
                                     device=dev)
            self.edge = torch.zeros((n, CAM_H, CAM_W), dtype=torch.uint8,
                                    device=dev)
            self.scal = torch.zeros((n, NSCAL), dtype=torch.float32,
                                    device=dev)
            self.rew = torch.zeros(n, dtype=torch.float32, device=dev)
            self.done = torch.zeros(n, dtype=torch.uint8, device=dev)
            self.pose = torch.zeros((n, NPOSE), dtype=torch.float32,
                                    device=dev)
            self.status = torch.zeros((n, NSTATUS), dtype=torch.int32,
                                      device=dev)
        else:
            import numpy as np
            self.np = np
            self.cam = np.zeros((n, CAM_H, CAM_W), dtype=np.int16)
            self.depth = np.zeros((n, CAM_H, CAM_W), dtype=np.uint8)
            self.edge = np.zeros((n, CAM_H, CAM_W), dtype=np.uint8)
            self.scal = np.zeros((n, NSCAL), dtype=np.float32)
            self.rew = np.zeros(n, dtype=np.float32)
            self.done = np.zeros(n, dtype=np.uint8)
            self.pose = np.zeros((n, NPOSE), dtype=np.float32)
            self.status = np.zeros((n, NSTATUS), dtype=np.int32)

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
        arr = (ctypes.c_int * self.n)(*list(snap_idx))
        if self.lib.blaze_assign(self.h, arr) != 0:
            raise RuntimeError("blaze_assign failed")

    def reset(self, mask=None):
        """mask: host iterable/array of n 0/1 bytes, or None for all."""
        m = None
        if mask is not None:
            b = bytes(bytearray(int(x) for x in mask))
            m = ctypes.c_char_p(b)
        if self.lib.blaze_reset(self.h, m) != 0:
            raise RuntimeError("blaze_reset failed")

    def _expand_heads(self, a5):
        """Legacy [N,5] act_dict head indices -> full float64 [N,13] rows,
        with the exact numeric decode the pre-port C core used (values are
        bit-identical: -15/0/15 deg yaw, -10/0/10 pitch, 0/1 flags,
        hotbar/craft -1, everything else 0)."""
        if self.backend == "cuda":
            t = self.torch
            a5 = a5.to(self.cam.device).long()
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
            full = np.zeros((a5.shape[0], NACT), dtype=np.float64)
            full[:, 2] = np.array(YAW_STEPS)[a5[:, 0]]
            full[:, 3] = np.array(PITCH_STEPS)[a5[:, 1]]
            full[:, 0] = a5[:, 2]
            full[:, 4] = a5[:, 3]
            full[:, 7] = a5[:, 4]
        full[:, 9] = -1.0    # hotbar: unchanged
        full[:, 10] = -1.0   # craft: none
        return full

    def step(self, actions, repeat=4):
        """actions: legacy int [N,5] head indices OR full float64 [N,12/13]
        raw action rows (see module docstring; 12-wide rows are zero-padded
        with smelt=0). cuda backend: torch tensor on this device; cpu
        backend: numpy array (contiguous)."""
        if self.backend == "cuda":
            t = self.torch
            if not isinstance(actions, t.Tensor):
                actions = t.as_tensor(actions)
            if actions.shape[-1] == NHEADS:
                actions = self._expand_heads(actions)
            elif actions.shape[-1] == NACT_LEGACY:
                actions = t.cat([actions.double(), t.zeros(
                    (actions.shape[0], NACT - NACT_LEGACY),
                    dtype=t.float64, device=actions.device)], dim=1)
            actions = actions.to(self.cam.device, t.float64).contiguous()
            act_ptr = ctypes.c_void_p(actions.data_ptr())
        else:
            actions = self.np.asarray(actions)
            if actions.shape[-1] == NHEADS:
                actions = self._expand_heads(actions)
            elif actions.shape[-1] == NACT_LEGACY:
                actions = self.np.concatenate(
                    [actions, self.np.zeros(
                        (actions.shape[0], NACT - NACT_LEGACY))], axis=1)
            actions = self.np.ascontiguousarray(actions,
                                                dtype=self.np.float64)
            act_ptr = ctypes.c_void_p(actions.ctypes.data)
        r = self.lib.blaze_step_full(
            self.h, act_ptr, repeat, self._ptr(self.cam), self._ptr(self.depth),
            self._ptr(self.edge), self._ptr(self.scal), self._ptr(self.rew),
            self._ptr(self.done), self._ptr(self.pose), self._ptr(self.status))
        if r != 0:
            raise RuntimeError("blaze_step failed")
        self._act_keepalive = actions   # kernels read it until the sync
        return (self.cam, self.depth, self.edge, self.scal, self.rew,
                self.done, self.pose)

    def close(self):
        if getattr(self, "h", None):
            self.lib.blaze_destroy(self.h)
            self.h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
