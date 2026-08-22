#!/usr/bin/env python3
"""CPU==Metal bit-exact gate for blaze observation camera (k_obs / oc_pixel).

Replays the chain tape through blaze_cpu.so (same path as verify_cpu.py --chain)
and at every tick also renders cam/depth/edge through blaze_metal_obs.so.
Acceptance: all ticks bit-exact on all three planes, exit 0.

Usage (MacBook, from repo root, after `make -C magma blaze_so blaze_metal_obs`):

  uv run --no-project --with numpy python blaze/env/verify_metal_obs.py --chain
  uv run --no-project --with numpy python blaze/env/verify_metal_obs.py --selftest

Darwin / Metal only. The Metal path is obs-only; tick stays on the CPU backend.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.join(os.path.dirname(HERE), "rl")
SNAPS = os.path.join(RL, "out", "snaps")
CPU_SO = os.path.join(HERE, "blaze_cpu.so")
METAL_SO = os.path.join(HERE, "blaze_metal_obs.so")

OC_W, OC_H = 64, 36
OC_NPIX = OC_W * OC_H

# BOLR offsets (same as verify_cpu.py)
OFF = {}
o = 0
for name, sz in [
    ("magic", 4), ("tick", 8), ("x", 8), ("y", 8), ("z", 8),
    ("yaw", 4), ("pitch", 4), ("dead", 4),
    ("hotbar_ids", 36), ("hotbar_counts", 36),
    ("hotbar_sel", 4), ("container", 4), ("inv_counts", 36),
    ("blocks", 256 * 16), ("logs", 64 * 12), ("coal", 32 * 12),
    ("cam", 2304 * 2), ("depth", 2304), ("edge", 2304),
]:
    OFF[name] = (o, o + sz)
    o += sz
BIN_SIZE = o


def _load_cpu():
    if not os.path.exists(CPU_SO):
        raise RuntimeError(f"missing {CPU_SO}; run: make -C magma blaze_so")
    from blaze import BlazeCreateOpts
    lib = ctypes.CDLL(CPU_SO)
    lib.blaze_create.restype = ctypes.c_void_p
    lib.blaze_create.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.POINTER(BlazeCreateOpts)]
    lib.blaze_destroy.argtypes = [ctypes.c_void_p]
    lib._BlazeCreateOpts = BlazeCreateOpts
    lib.blaze_load_snapshots.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int,
        ctypes.c_char_p, ctypes.c_int,
    ]
    lib.blaze_load_snapshots.restype = ctypes.c_int
    lib.blaze_assign.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    lib.blaze_assign.restype = ctypes.c_int
    lib.blaze_reset.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.blaze_reset.restype = ctypes.c_int
    lib.blaze_obs_size.restype = ctypes.c_int
    lib.blaze_emit.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
    ]
    lib.blaze_emit.restype = ctypes.c_int
    lib.blaze_tick_raw.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_void_p,
    ]
    lib.blaze_tick_raw.restype = ctypes.c_int
    lib.blaze_obs_cam_inputs.argtypes = [
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
    lib.blaze_obs_cam_inputs.restype = ctypes.c_int
    return lib


def _load_metal():
    if not os.path.exists(METAL_SO):
        raise RuntimeError(
            f"missing {METAL_SO}; run: make -C magma blaze_metal_obs")
    lib = ctypes.CDLL(METAL_SO)
    lib.blaze_metal_obs_create.restype = ctypes.c_void_p
    lib.blaze_metal_obs_create.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.blaze_metal_obs_destroy.argtypes = [ctypes.c_void_p]
    lib.blaze_metal_obs_render.restype = ctypes.c_int
    lib.blaze_metal_obs_render.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint16),
        ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_double, ctypes.c_double, ctypes.c_double,
        ctypes.c_float, ctypes.c_float,
        ctypes.POINTER(ctypes.c_uint16),
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.POINTER(ctypes.c_uint8),
    ]
    return lib


class BlazeCpu:
    def __init__(self, snap):
        self.lib = _load_cpu()
        opts = self.lib._BlazeCreateOpts.defaults()
        self.h = self.lib.blaze_create(0, 1, ctypes.byref(opts))
        if not self.h:
            raise RuntimeError("blaze_create failed")
        err = ctypes.create_string_buffer(256)
        paths = (ctypes.c_char_p * 1)(snap.encode())
        r = self.lib.blaze_load_snapshots(
            ctypes.c_void_p(self.h), paths, 1, err, 256)
        if r < 0:
            raise RuntimeError(f"load_snapshots: {err.value.decode()}")
        assign = (ctypes.c_int * 1)(0)
        if self.lib.blaze_assign(ctypes.c_void_p(self.h), assign) != 0:
            raise RuntimeError("blaze_assign failed")
        if self.lib.blaze_reset(ctypes.c_void_p(self.h), None) != 0:
            raise RuntimeError("blaze_reset failed")
        if self.lib.blaze_obs_size() != BIN_SIZE:
            raise RuntimeError(
                f"CuBinObs {self.lib.blaze_obs_size()} != {BIN_SIZE}")
        self.buf = ctypes.create_string_buffer(BIN_SIZE)

    def emit(self, want_cam=1):
        assert self.lib.blaze_emit(
            ctypes.c_void_p(self.h), 0, want_cam, self.buf) == 0
        return self.buf.raw

    def step(self, act):
        a = (ctypes.c_double * 17)(
            act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
            act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
            act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
            act.get("hotbar", -1), act.get("craft", -1),
            act.get("interact", 0), act.get("smelt", 0),
            act.get("inv_click", 0), act.get("inv_slot", 0),
            act.get("inv_button", 0), act.get("inv_type", 0))
        r = self.lib.blaze_tick_raw(
            ctypes.c_void_p(self.h), 0, a, act.get("cam", 1), self.buf)
        assert r == 0, "blaze_tick_raw failed"
        return self.buf.raw

    def cam_inputs(self):
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
        r = self.lib.blaze_obs_cam_inputs(
            ctypes.c_void_p(self.h), 0,
            ctypes.byref(ex), ctypes.byref(ey), ctypes.byref(ez),
            ctypes.byref(yaw), ctypes.byref(pitch),
            ctypes.byref(x0), ctypes.byref(y0), ctypes.byref(z0),
            ctypes.byref(nx), ctypes.byref(ny), ctypes.byref(nz),
            ctypes.byref(cells))
        if r != 0:
            raise RuntimeError("blaze_obs_cam_inputs failed")
        return {
            "ex": ex.value, "ey": ey.value, "ez": ez.value,
            "yaw": yaw.value, "pitch": pitch.value,
            "x0": x0.value, "y0": y0.value, "z0": z0.value,
            "nx": nx.value, "ny": ny.value, "nz": nz.value,
            "cells": cells,
        }

    def close(self):
        self.lib.blaze_destroy(ctypes.c_void_p(self.h))


class MetalObs:
    def __init__(self, max_cells, metallib=None):
        self.lib = _load_metal()
        path = metallib.encode() if metallib else None
        self.h = self.lib.blaze_metal_obs_create(max_cells, path)
        if not self.h:
            raise RuntimeError("blaze_metal_obs_create failed")
        self.cam = (ctypes.c_uint16 * OC_NPIX)()
        self.depth = (ctypes.c_uint8 * OC_NPIX)()
        self.edge = (ctypes.c_uint8 * OC_NPIX)()

    def render(self, inp, yaw_override=None, pitch_override=None):
        yaw = inp["yaw"] if yaw_override is None else yaw_override
        pitch = inp["pitch"] if pitch_override is None else pitch_override
        r = self.lib.blaze_metal_obs_render(
            ctypes.c_void_p(self.h),
            inp["cells"],
            inp["x0"], inp["y0"], inp["z0"],
            inp["nx"], inp["ny"], inp["nz"],
            inp["ex"], inp["ey"], inp["ez"],
            float(yaw), float(pitch),
            self.cam, self.depth, self.edge)
        if r != 0:
            raise RuntimeError("blaze_metal_obs_render failed")
        return bytes(self.cam), bytes(self.depth), bytes(self.edge)

    def close(self):
        self.lib.blaze_metal_obs_destroy(ctypes.c_void_p(self.h))


def plane_from_bolr(rec):
    clo, chi = OFF["cam"]
    dlo, dhi = OFF["depth"]
    elo, ehi = OFF["edge"]
    return rec[clo:chi], rec[dlo:dhi], rec[elo:ehi]


def first_mismatch(cpu_plane, metal_plane, name):
    if cpu_plane == metal_plane:
        return None
    n = min(len(cpu_plane), len(metal_plane))
    for i in range(n):
        if cpu_plane[i] != metal_plane[i]:
            if name == "cam":
                # cam is u16 little-endian pairs
                pi = i // 2
                cv = struct.unpack_from("<H", cpu_plane, pi * 2)[0]
                mv = struct.unpack_from("<H", metal_plane, pi * 2)[0]
                return (name, pi, cv, mv, i)
            return (name, i, cpu_plane[i], metal_plane[i], i)
    return (name, n, None, None, n)


def planes_equal(rec, metal_cam, metal_dep, metal_edg):
    """Return (ok, mismatch_tuple_or_None). mismatch = (plane, pix, cpu, metal)."""
    cpu_cam, cpu_dep, cpu_edg = plane_from_bolr(rec)
    for name, a, b in (
        ("cam", cpu_cam, metal_cam),
        ("depth", cpu_dep, metal_dep),
        ("edge", cpu_edg, metal_edg),
    ):
        m = first_mismatch(a, b, name)
        if m:
            plane, pix, cv, mv, _raw = m
            return False, (plane, pix, cv, mv)
    return True, None


def compare_obs(rec, metal_cam, metal_dep, metal_edg, tick_label):
    ok, m = planes_equal(rec, metal_cam, metal_dep, metal_edg)
    if not ok:
        plane, pix, cv, mv = m
        print(f"FAIL: first mismatch tick={tick_label} plane={plane} "
              f"pixel_index={pix} cpu={cv} metal={mv}")
    return ok


def run_chain(seed, expected_actions=None):
    snap = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    acts_path = os.path.join(RL, "out", f"chain_actions_s{seed}.json")
    if not os.path.exists(snap) or not os.path.exists(acts_path):
        print(f"FAIL: missing {snap} or {acts_path}")
        return 1
    acts = json.load(open(acts_path))
    acts = [{k: v for k, v in a.items() if k != "snapshot"} for a in acts]
    if expected_actions is not None and len(acts) != expected_actions:
        print(f"FAIL: chain has {len(acts)} actions, expected {expected_actions}")
        return 1

    cpu = BlazeCpu(snap)
    try:
        inp0 = cpu.cam_inputs()
        max_cells = inp0["nx"] * inp0["ny"] * inp0["nz"]
        metal = MetalObs(max_cells)
    except Exception as e:
        cpu.close()
        print(f"FAIL: setup: {e}")
        return 1

    t0 = time.perf_counter()
    n_checked = 0
    try:
        rec = cpu.emit(1)
        mcam, mdep, medg = metal.render(cpu.cam_inputs())
        if not compare_obs(rec, mcam, mdep, medg, "INITIAL"):
            return 1
        n_checked += 1

        for t, act in enumerate(acts):
            rec = cpu.step(act)
            if act.get("cam", 1) == 0:
                # CPU reuses the previous frame; Metal would re-render fresh.
                # Chain fixture has no cam:0 rows; skip if ever present.
                continue
            mcam, mdep, medg = metal.render(cpu.cam_inputs())
            if not compare_obs(rec, mcam, mdep, medg, t):
                return 1
            n_checked += 1
    finally:
        metal.close()
        cpu.close()

    dt = time.perf_counter() - t0
    print(f"PASS: CPU==Metal obs bit-exact over {n_checked} frames "
          f"({len(acts)} chain ticks + initial), {dt:.3f}s")
    return 0


def run_selftest(seed):
    """Perturb one input; the gate must fail loudly.

    Prefer the smallest yaw change that moves at least one pixel (walk ulps
    from 1). If the sin LUT is coarse at this pose, fall back to a 1-degree
    yaw kick so the detector still proves the compare path is live.
    """
    snap = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    if not os.path.exists(snap):
        print(f"FAIL: missing {snap}")
        return 1
    cpu = BlazeCpu(snap)
    try:
        rec = cpu.emit(1)
        inp = cpu.cam_inputs()
        metal = MetalObs(inp["nx"] * inp["ny"] * inp["nz"])
        try:
            # Sanity: unperturbed must match.
            mcam, mdep, medg = metal.render(inp)
            ok, _ = planes_equal(rec, mcam, mdep, medg)
            if not ok:
                print("FAIL: selftest baseline CPU==Metal already diverges")
                return 1

            yaw_bits = struct.unpack("<I", struct.pack("<f", inp["yaw"]))[0]
            pert_desc = None
            mismatch = None
            # Walk up to 2^16 ulps of yaw — enough to hop a sin-LUT bin for
            # most poses; 1 ulp alone is often a no-op on the coarse LUT.
            for nulp in range(1, 65536):
                yaw_pert = struct.unpack(
                    "<f", struct.pack("<I", (yaw_bits + nulp) & 0xFFFFFFFF))[0]
                if yaw_pert != yaw_pert:  # NaN skip
                    continue
                mcam, mdep, medg = metal.render(inp, yaw_override=yaw_pert)
                ok, mismatch = planes_equal(rec, mcam, mdep, medg)
                if not ok:
                    pert_desc = (
                        f"yaw +{nulp} ulp ({inp['yaw']!r} -> {yaw_pert!r})")
                    break
            if mismatch is None:
                mcam, mdep, medg = metal.render(
                    inp, yaw_override=inp["yaw"] + 1.0)
                ok, mismatch = planes_equal(rec, mcam, mdep, medg)
                if not ok:
                    pert_desc = (
                        f"yaw +1.0 deg ({inp['yaw']!r} -> {inp['yaw']+1.0!r})")
            if mismatch is None:
                print("FAIL: selftest could not produce a mismatch via yaw "
                      "perturbation (compare path may be dead)")
                return 1
            plane, pix, cv, mv = mismatch
            print(f"PASS: selftest — perturbation ({pert_desc}) fails the "
                  f"gate loudly: plane={plane} pixel_index={pix} "
                  f"cpu={cv} metal={mv}")
            return 0
        finally:
            metal.close()
    finally:
        cpu.close()


def main():
    if sys.platform != "darwin":
        print("FAIL: verify_metal_obs.py requires macOS (Metal)")
        return 1
    ap = argparse.ArgumentParser()
    ap.add_argument("--chain", action="store_true",
                    help="full chain gate (s10_t0 + chain_actions_s10.json)")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument("--expected-chain-actions", type=int, default=None)
    ap.add_argument("--selftest", action="store_true",
                    help="corruption selftest: 1-ulp yaw must fail loudly")
    args = ap.parse_args()
    if not args.chain and not args.selftest:
        ap.error("pass --chain and/or --selftest")
    rc = 0
    if args.selftest:
        rc = run_selftest(args.chain_seed) or rc
    if args.chain:
        rc = run_chain(args.chain_seed, args.expected_chain_actions) or rc
    return rc


if __name__ == "__main__":
    sys.exit(main())
