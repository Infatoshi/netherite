"""Metal observation part of the Blaze Metal backend."""

from __future__ import annotations

import ctypes
import os

HERE = os.path.dirname(os.path.abspath(__file__))
METAL_SO = os.path.join(HERE, "blaze_metal_obs.so")


class MetalObservationBackend:
    def __init__(self, cpu_lib, max_cells, metallib=""):
        if not os.path.exists(METAL_SO):
            raise RuntimeError(
                f"missing {METAL_SO}; run: make -C magma blaze_metal_obs")
        self.cpu_lib = cpu_lib
        self.lib = ctypes.CDLL(METAL_SO)
        self.lib.blaze_metal_obs_create.restype = ctypes.c_void_p
        self.lib.blaze_metal_obs_create.argtypes = [ctypes.c_int, ctypes.c_char_p]
        self.lib.blaze_metal_obs_destroy.argtypes = [ctypes.c_void_p]
        self.lib.blaze_metal_obs_render.restype = ctypes.c_int
        self.lib.blaze_metal_obs_render.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint16),
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_float, ctypes.c_float,
            ctypes.POINTER(ctypes.c_uint16),
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
        ]
        cpu_lib.blaze_obs_cam_inputs.restype = ctypes.c_int
        cpu_lib.blaze_obs_cam_inputs.argtypes = [
            ctypes.c_void_p, ctypes.c_int,
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16)),
        ]
        path = metallib.encode() if metallib else None
        self.h = self.lib.blaze_metal_obs_create(int(max_cells), path)
        if not self.h:
            raise RuntimeError("blaze_metal_obs_create failed")

    def render_all(self, blaze_handle, cam, depth, edge):
        for env in range(cam.shape[0]):
            ex = ctypes.c_double()
            ey = ctypes.c_double()
            ez = ctypes.c_double()
            yaw = ctypes.c_float()
            pitch = ctypes.c_float()
            x0 = ctypes.c_int()
            y0 = ctypes.c_int()
            z0 = ctypes.c_int()
            nx = ctypes.c_int()
            ny = ctypes.c_int()
            nz = ctypes.c_int()
            cells = ctypes.POINTER(ctypes.c_uint16)()
            rc = self.cpu_lib.blaze_obs_cam_inputs(
                blaze_handle, env, ctypes.byref(ex), ctypes.byref(ey),
                ctypes.byref(ez), ctypes.byref(yaw), ctypes.byref(pitch),
                ctypes.byref(x0), ctypes.byref(y0), ctypes.byref(z0),
                ctypes.byref(nx), ctypes.byref(ny), ctypes.byref(nz),
                ctypes.byref(cells))
            if rc != 0:
                raise RuntimeError(f"blaze_obs_cam_inputs failed for env {env}")
            rc = self.lib.blaze_metal_obs_render(
                self.h, cells, x0.value, y0.value, z0.value,
                nx.value, ny.value, nz.value, ex.value, ey.value, ez.value,
                yaw.value, pitch.value,
                cam[env].ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)),
                depth[env].ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)),
                edge[env].ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)))
            if rc != 0:
                raise RuntimeError(f"Metal observation failed for env {env}")

    def close(self):
        if getattr(self, "h", None):
            self.lib.blaze_metal_obs_destroy(self.h)
            self.h = None
