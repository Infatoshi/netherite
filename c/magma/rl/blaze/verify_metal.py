#!/usr/bin/env python3
"""Exact CPU vs correctness-first Metal Blaze verification and benchmark."""
import argparse
import ctypes
import hashlib
import json
import os
import resource
import struct
import sys
import tempfile
import time

import numpy as np

from blaze import CPU_SO, METAL_DYLIB, VecBlaze
from metal_test_utils import (RlSnapHead, action_tape,
                              write_dense_coal_snapshot,
                              write_overflow_snapshot,
                              write_synthetic_snapshot)


FIELDS = ("cam", "depth", "edge", "scal", "rew", "done", "pose", "status")
OP_CRAFT, OP_INTERACT, OP_SMELT = 9, 10, 11


def fail_mismatch(label, a, b):
    aa, bb = np.asarray(a), np.asarray(b)
    if aa.shape != bb.shape or aa.dtype != bb.dtype:
        raise AssertionError(
            f"{label}: layout {aa.shape}/{aa.dtype} != {bb.shape}/{bb.dtype}")
    if aa.tobytes() == bb.tobytes():
        return
    raw_a = aa.view(np.uint8).reshape(-1)
    raw_b = bb.view(np.uint8).reshape(-1)
    byte = int(np.flatnonzero(raw_a != raw_b)[0])
    elem = byte // aa.dtype.itemsize
    idx = np.unravel_index(elem, aa.shape)
    raise AssertionError(
        f"{label}: first mismatch index={idx}, cpu={aa[idx]!r}, "
        f"metal={bb[idx]!r}, byte_offset={byte}")


def compare(cpu, metal, label):
    for name in FIELDS:
        fail_mismatch(f"{label}/{name}", getattr(cpu, name), getattr(metal, name))


def raw_action(act):
    return (ctypes.c_double * 13)(
        act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
        act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
        act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
        act.get("hotbar", -1), act.get("craft", -1),
        act.get("interact", 0), act.get("smelt", 0))


def configure_raw(env):
    env.lib.blaze_obs_size.restype = ctypes.c_int
    env.lib.blaze_emit.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_void_p]
    env.lib.blaze_tick_raw.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_void_p]


def committed_chain_gate(snapshot, actions_path, device=0):
    """Replay a recorded progression chain, including all protocol actions."""
    actions = [{k: v for k, v in a.items() if k != "snapshot"}
               for a in json.load(open(actions_path))]
    requires_diamond_tools = any(int(a.get("craft", -1)) >= 8
                                 for a in actions)
    cpu, metal = setup_pair(1, snapshot, device=device)
    try:
        configure_raw(cpu); configure_raw(metal)
        size = cpu.lib.blaze_obs_size()
        if size != metal.lib.blaze_obs_size():
            raise AssertionError("CPU/Metal BOLR size differs")
        aout = ctypes.create_string_buffer(size)
        bout = ctypes.create_string_buffer(size)
        if cpu.lib.blaze_emit(cpu.h, 0, 1, aout) or \
                metal.lib.blaze_emit(metal.h, 0, 1, bout):
            raise RuntimeError("initial committed-chain emit failed")
        if aout.raw != bout.raw:
            byte = next(i for i, (a, b) in enumerate(zip(aout.raw, bout.raw))
                        if a != b)
            raise AssertionError(
                f"chain initial mismatch byte={byte}, cpu={aout.raw[byte]}, "
                f"metal={bout.raw[byte]}")
        h = hashlib.sha256(bout.raw)
        edge_offset = size - 36 * 64
        edge_diffs = 0
        edge_frames = 0
        edge_max = 0
        first_edge = None
        for tick, act in enumerate(actions):
            want = int(act.get("cam", 1))
            row = raw_action(act)
            if cpu.lib.blaze_tick_raw(cpu.h, 0, row, want, aout) or \
                    metal.lib.blaze_tick_raw(metal.h, 0, row, want, bout):
                raise RuntimeError(f"committed-chain tick failed at {tick}")
            if aout.raw != bout.raw:
                aa = np.frombuffer(aout.raw, dtype=np.uint8)
                bb = np.frombuffer(bout.raw, dtype=np.uint8)
                diff = np.flatnonzero(aa != bb)
                non_edge = diff[diff < edge_offset]
                if len(non_edge):
                    byte = int(non_edge[0])
                    raise AssertionError(
                        f"chain tick={tick} non-edge mismatch byte={byte}, "
                        f"cpu={aout.raw[byte]}, metal={bout.raw[byte]}")
                nedge = len(diff)
                edge_diffs += nedge
                edge_frames += 1
                edge_max = max(edge_max, nedge)
                if first_edge is None:
                    byte = int(diff[0])
                    first_edge = (tick, byte - edge_offset,
                                  int(aa[byte]), int(bb[byte]))
            h.update(bout.raw)
        # Metal and host float32 can land on opposite sides of the semantic
        # camera's 0.05 edge threshold by one ULP. Keep the allowance far
        # below one pixel per thousand frames and never apply it to block id,
        # depth, state, or inventory bytes.
        if edge_diffs > 32 or edge_max > 2:
            raise AssertionError(
                f"chain edge tolerance exceeded: pixels={edge_diffs}, "
                f"frames={edge_frames}, max_per_frame={edge_max}, "
                f"first={first_edge}")

        noop = np.zeros((1, 13), dtype=np.float64)
        noop[:, 9:11] = -1
        cpu.step(noop, repeat=1)
        metal.step(noop, repeat=1)
        for name in ("status", "pose", "rew", "done"):
            fail_mismatch(f"chain-final/{name}", getattr(cpu, name),
                          getattr(metal, name))
        tools = np.asarray(cpu.status)[0, 18:23]
        if requires_diamond_tools and np.any(tools < 1):
            raise AssertionError(
                f"chain ended without full diamond tool set: {tools.tolist()}")
        info = metal.backend_info()
        info["chain_edge_pixels"] = edge_diffs
        info["chain_edge_frames"] = edge_frames
        info["chain_edge_max"] = edge_max
        info["chain_first_edge"] = first_edge
        info["chain_tools"] = tools.tolist()
        info["chain_requires_diamond_tools"] = requires_diamond_tools
        return h.hexdigest(), info, len(actions)
    finally:
        cpu.close(); metal.close()


def setup_pair(n, snapshot, device=0):
    cpu = VecBlaze(n, device=0, backend="cpu")
    metal = VecBlaze(n, device=device, backend="metal", output_device="host")
    if cpu.backend != "cpu" or metal.backend != "metal":
        cpu.close(); metal.close()
        raise AssertionError(
            f"explicit backend selection lost: {cpu.backend}/{metal.backend}")
    for env in (cpu, metal):
        env.load_snapshots([snapshot])
        env.assign(np.zeros(n, dtype=np.int32))
        env.reset()
    return cpu, metal


def parity_run(n, decisions, snapshot, stream="mixed", capture=True, device=0):
    cpu, metal = setup_pair(n, snapshot, device=device)
    try:
        if stream == "chain":
            cpu.set_success_item(50); metal.set_success_item(50)
            cpu.reset(); metal.reset()
        elif stream == "mixed":
            cpu.set_reward_gate(3.2); metal.set_reward_gate(3.2)
        allocations = metal.backend_info()["allocation_count"]
        for decision in range(decisions):
            if capture and decision == 3:
                # Overwrite a slot currently referenced by lane 0. The capture
                # implementation must commit atomically and rebind live CSR/
                # ore pointers before the following step uses them.
                cpu.capture(0, 1); metal.capture(0, 1)
            use_full = stream == "chain" or (stream == "mixed" and
                                               decision % 2 == 1)
            actions = action_tape(n, decision, full=use_full)
            repeat = 1 + decision % 4
            cpu.step(actions, repeat=repeat)
            metal.step(actions, repeat=repeat)
            compare(cpu, metal, f"decision={decision},repeat={repeat},n={n}")
            now = metal.backend_info()["allocation_count"]
            if now != allocations:
                raise AssertionError(
                    f"per-step Metal allocation count changed {allocations} -> {now}")
            if decision == 1 and n > 1:
                mask = ((np.arange(n) & 1) == 0).astype(np.uint8)
                cpu.reset(mask); metal.reset(mask)
            if capture and decision == 2:
                cpu.capture(0, 1); metal.capture(0, 1)
                assign = np.zeros(n, dtype=np.int32)
                assign[0] = 1
                cpu.assign(assign); metal.assign(assign)
                mask = np.zeros(n, dtype=np.uint8); mask[0] = 1
                cpu.reset(mask); metal.reset(mask)
        return digest_env(metal), metal.backend_info()
    finally:
        cpu.close(); metal.close()


def full_action_op_gate(snapshot, device=0):
    """Prove the three rare protocol heads reach both backends, not just ABI."""
    old_trace = os.environ.get("BLAZE_OP_TRACE")
    os.environ["BLAZE_OP_TRACE"] = "1"
    cpu = metal = None
    try:
        cpu, metal = setup_pair(1, snapshot, device=device)
        actions = np.zeros((1, 13), dtype=np.float64)
        actions[:, 9:11] = -1.0
        actions[0, 10] = 0.0
        actions[0, 11] = 1.0
        actions[0, 12] = 1.0
        cpu.step(actions, repeat=1)
        metal.step(actions, repeat=1)
        compare(cpu, metal, "full-action-protocol")
        cpu_ops, metal_ops = cpu.op_trace(), metal.op_trace()
        if cpu_ops is None or metal_ops is None:
            raise AssertionError("BLAZE_OP_TRACE did not expose counters")
        fail_mismatch("full-action/op-trace", cpu_ops, metal_ops)
        for index, name in ((OP_CRAFT, "craft"),
                            (OP_INTERACT, "interact"),
                            (OP_SMELT, "smelt")):
            if int(cpu_ops[:, index].sum()) == 0:
                raise AssertionError(f"full-action gate did not execute {name}")
        print("full actions: craft/interact/smelt CPU==Metal op-trace PASS")
    finally:
        if cpu is not None:
            cpu.close()
        if metal is not None:
            metal.close()
        if old_trace is None:
            os.environ.pop("BLAZE_OP_TRACE", None)
        else:
            os.environ["BLAZE_OP_TRACE"] = old_trace


def long_horizon_regression_gates(temp_dir, device=0):
    dense = write_dense_coal_snapshot(os.path.join(temp_dir, "dense-coal.bsnp"))
    cpu, metal = setup_pair(1, dense, device=device)
    try:
        configure_raw(cpu); configure_raw(metal)
        size = cpu.lib.blaze_obs_size()
        a = ctypes.create_string_buffer(size)
        b = ctypes.create_string_buffer(size)
        if cpu.lib.blaze_emit(cpu.h, 0, 0, a) or \
                metal.lib.blaze_emit(metal.h, 0, 0, b):
            raise RuntimeError("dense-coal emit failed")
        if a.raw != b.raw:
            raise AssertionError("dense-coal CPU/Metal mismatch")
        coal_off = 5028
        got = np.frombuffer(a.raw, dtype="<i4", count=32 * 3,
                            offset=coal_off).reshape(32, 3)
        scan = [(x, y, z) for x in range(4, 37)
                for z in range(4, 37) for y in range(64, -1, -1)][:512]
        scan.sort(key=lambda q: ((q[0] + 0.5 - 20.5) ** 2 +
                                 (q[1] + 0.5 - 24.0) ** 2 +
                                 (q[2] + 0.5 - 20.5) ** 2, *q))
        expected = np.asarray(scan[:32], dtype=np.int32)
        fail_mismatch("dense-coal/real-scan-order", expected, got)
    finally:
        cpu.close(); metal.close()

    overflow = write_overflow_snapshot(
        os.path.join(temp_dir, "item-overflow.bsnp"))
    cpu, metal = setup_pair(1, overflow, device=device)
    try:
        action = np.zeros((1, 13), dtype=np.float64)
        action[:, 9:11] = -1
        action[:, 0] = 1
        action[:, 7] = 1
        for tick in range(40):
            cpu.step(action, repeat=1); metal.step(action, repeat=1)
            compare(cpu, metal, f"item-overflow/{tick}")
            if tick == 14:
                cpu.capture(0, 1); metal.capture(0, 1)
                cpu.assign([1]); metal.assign([1])
                cpu.reset(); metal.reset()
        if int(cpu.status[0, 7]) != 1:
            raise AssertionError(
                "queued coal was not recovered across overflow capture/reset")
    finally:
        cpu.close(); metal.close()
    print("long horizon: 512-coal scan order and 48+32 item overflow PASS")


def digest_env(env):
    h = hashlib.sha256()
    for name in FIELDS:
        h.update(np.asarray(getattr(env, name)).tobytes())
    return h.hexdigest()


def metal_digest(n, decisions, snapshot, device=0):
    env = VecBlaze(n, device=device, backend="metal", output_device="host")
    try:
        env.load_snapshots([snapshot])
        env.assign(np.zeros(n, dtype=np.int32))
        env.reset()
        for d in range(decisions):
            env.step(action_tape(n, d, full=(d & 1) != 0), repeat=1 + d % 4)
        return digest_env(env)
    finally:
        env.close()


def tail_gate(snapshot, sizes, device=0):
    for n in sizes:
        parity_run(n, 1, snapshot, stream="mixed", capture=False,
                   device=device)
        print(f"tail exact: n={n}")


def benchmark(snapshot, n, decisions, device=0):
    env = VecBlaze(n, device=device, backend="metal", output_device="host")
    try:
        env.load_snapshots([snapshot])
        env.assign(np.zeros(n, dtype=np.int32)); env.reset()
        env.step(action_tape(n, 0), repeat=1)
        t0 = time.perf_counter()
        for d in range(decisions):
            env.step(action_tape(n, d), repeat=4)
        elapsed = time.perf_counter() - t0
        info = env.backend_info()
        rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        rss_mib = rss / 2**20 if sys.platform == "darwin" else rss / 1024.0
        print(f"bench: {n * decisions / elapsed:.1f} env-decisions/s, "
              f"{n * decisions * 4 / elapsed:.1f} env-ticks/s, "
              f"{n * decisions * 2304 / elapsed:.1f} semantic-rays/s, "
              f"tick={info['last_tick_ms']:.3f} ms, "
              f"camera={info['last_camera_ms']:.3f} ms, "
              f"shared={info['metal_buffer_bytes'] / 2**20:.1f} MiB, "
              f"host-snapshots={info['host_snapshot_bytes'] / 2**20:.1f} MiB, "
              f"peak-rss={rss_mib:.1f} MiB")
    finally:
        env.close()


def write_header_only_snapshot(path, dims, region_origin=(0, 0, 0),
                               world_origin=(0, 0)):
    head = RlSnapHead()
    head.magic = b"BSNP"
    head.version = 1
    head.rx0, head.ry0, head.rz0 = region_origin
    head.rnx, head.rny, head.rnz = dims
    head.ox, head.oz = world_origin
    with open(path, "wb") as f:
        f.write(bytes(head))
    return path


def write_bad_coal_snapshot(path):
    head = RlSnapHead()
    head.magic = b"BSNP"
    head.version = 1
    head.rx0, head.ry0, head.rz0 = -(2 ** 31), 0, 0
    head.rnx = head.rny = head.rnz = 1
    with open(path, "wb") as f:
        f.write(bytes(head))
        f.write(struct.pack("<H", 0))
        f.write(struct.pack("<Iiii", 1, 2 ** 31 - 1, 0, 0))
    return path


def expect_load_error(env, path, fragment):
    try:
        env.load_snapshots([path])
    except RuntimeError as exc:
        if fragment not in str(exc):
            raise AssertionError(
                f"unexpected snapshot error for {os.path.basename(path)}: {exc}")
    else:
        raise AssertionError(
            f"invalid snapshot unexpectedly loaded: {os.path.basename(path)}")


def expect_value_error(label, call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError(f"unsafe {label} unexpectedly accepted")


def expect_runtime_error(label, call, fragment=None):
    try:
        call()
    except RuntimeError as exc:
        if fragment is not None and fragment not in str(exc):
            raise AssertionError(f"unexpected {label} error: {exc}")
        return
    raise AssertionError(f"unsafe {label} unexpectedly accepted")


def error_gate(snapshot, temp_dir, device=0):
    try:
        invalid = VecBlaze(1, device=9999, backend="metal", output_device="host")
    except RuntimeError as exc:
        if "unavailable" not in str(exc) or "device 9999" not in str(exc):
            raise AssertionError(f"non-actionable invalid-device error: {exc}")
    else:
        invalid.close()
        raise AssertionError("invalid Metal device unexpectedly accepted")

    old_limit = os.environ.get("BLAZE_METAL_MEMORY_LIMIT_MB")
    os.environ["BLAZE_METAL_MEMORY_LIMIT_MB"] = "1"
    try:
        try:
            unexpected = VecBlaze(2, device=device, backend="metal",
                                  output_device="host")
        except RuntimeError as exc:
            message = str(exc)
            if not all(x in message for x in
                       ("required", "budget", "approximate max N=")):
                raise AssertionError(f"non-actionable memory error: {exc}")
        else:
            unexpected.close()
            raise AssertionError("1 MiB Metal budget unexpectedly accepted N=2")
    finally:
        if old_limit is None:
            os.environ.pop("BLAZE_METAL_MEMORY_LIMIT_MB", None)
        else:
            os.environ["BLAZE_METAL_MEMORY_LIMIT_MB"] = old_limit

    malformed = os.path.join(temp_dir, "malformed.bsnp")
    with open(malformed, "wb") as f:
        f.write(b"not-a-snapshot")
    overflow_dims = write_header_only_snapshot(
        os.path.join(temp_dir, "overflow-dims.bsnp"),
        (2 ** 30, 2 ** 30, 16))
    bad_extent = write_header_only_snapshot(
        os.path.join(temp_dir, "bad-extent.bsnp"), (2, 1, 1),
        region_origin=(2 ** 31 - 1, 0, 0))
    bad_origin = write_header_only_snapshot(
        os.path.join(temp_dir, "bad-origin.bsnp"), (1, 1, 1),
        world_origin=(2 ** 31 - 1, 0))
    bad_coal = write_bad_coal_snapshot(
        os.path.join(temp_dir, "bad-coal.bsnp"))
    bad_snapshots = (
        (malformed, "bad .bsnp header"),
        (overflow_dims, "implausible .bsnp counts"),
        (bad_extent, "implausible .bsnp coordinates"),
        (bad_origin, "implausible .bsnp coordinates"),
        (bad_coal, "coal coordinate outside .bsnp region"),
    )

    for backend in ("cpu", "metal"):
        kwargs = {"device": device, "output_device": "host"} \
            if backend == "metal" else {"device": 0}
        env = VecBlaze(1, backend=backend, **kwargs)
        try:
            configure_raw(env)
            expect_runtime_error(
                f"{backend} reset before snapshot", lambda: env.reset())
            expect_runtime_error(
                f"{backend} step before snapshot",
                lambda: env.step(np.zeros((1, 13), dtype=np.float64),
                                 repeat=1),
                "loaded snapshot" if backend == "metal" else None)
            out = ctypes.create_string_buffer(env.lib.blaze_obs_size())
            if env.lib.blaze_emit(env.h, 0, 1, out) == 0:
                raise AssertionError(
                    f"{backend} emit before snapshot unexpectedly accepted")
            if env.lib.blaze_tick_raw(
                    env.h, 0, raw_action({}), 0, out) == 0:
                raise AssertionError(
                    f"{backend} raw tick before snapshot unexpectedly accepted")
            for path, fragment in bad_snapshots:
                expect_load_error(env, path, fragment)
            env.load_snapshots([snapshot])
            expect_runtime_error(
                f"{backend} step before reset",
                lambda: env.step(np.zeros((1, 13), dtype=np.float64),
                                 repeat=1),
                "reset env" if backend == "metal" else None)
            if env.lib.blaze_emit(env.h, 0, 1, out) == 0:
                raise AssertionError(
                    f"{backend} emit before reset unexpectedly accepted")
            if env.lib.blaze_tick_raw(
                    env.h, 0, raw_action({}), 0, out) == 0:
                raise AssertionError(
                    f"{backend} raw tick before reset unexpectedly accepted")
            expect_value_error("short snapshot assignment", lambda: env.assign([]))
            expect_value_error("long snapshot assignment", lambda: env.assign([0, 0]))
            env.assign([0])
            expect_value_error("short reset mask", lambda: env.reset([]))
            expect_value_error("long reset mask", lambda: env.reset([1, 0]))
            expect_value_error("non-binary reset mask", lambda: env.reset([2]))
            env.reset([1])

            bad_actions = []
            bad_actions.append(np.zeros((1, 7), dtype=np.float64))
            for column, value in ((0, np.nan), (2, np.inf), (0, 1.5),
                                  (4, 0.5), (9, 9.0), (10, 13.0),
                                  (2, float(np.finfo(np.float32).max) * 2.0)):
                row = np.zeros((1, 13), dtype=np.float64)
                row[:, 9:11] = -1.0
                row[0, column] = value
                bad_actions.append(row)
            bad_actions.extend((
                np.array([[3, 1, 0, 0, 0]], dtype=np.int64),
                np.array([[1, 1, 2, 0, 0]], dtype=np.int64),
            ))
            for i, actions in enumerate(bad_actions):
                expect_value_error(
                    f"{backend} action case {i}",
                    lambda actions=actions: env.step(actions, repeat=1))

            raw = np.zeros((1, 13), dtype=np.float64)
            raw[:, 9:11] = -1.0
            raw[0, 4] = np.nan
            rc = env.lib.blaze_step_full(
                env.h, ctypes.c_void_p(raw.ctypes.data), 1,
                None, None, None, None, None, None, None, None)
            if rc == 0:
                raise AssertionError(
                    f"{backend} C ABI accepted non-finite raw action")
            env.step(action_tape(1, 0, full=True), repeat=1)
        finally:
            env.close()
    print("error paths: device, budget/max-N, pre-load, snapshot counts/coords, "
          "vector lengths, action shape/values PASS")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--snapshot")
    p.add_argument("--chain-snapshot")
    p.add_argument("--chain-actions")
    p.add_argument("--n", type=int, default=7)
    p.add_argument("--decisions", type=int, default=8)
    p.add_argument("--device", type=int, default=0)
    p.add_argument("--camera-only", action="store_true")
    p.add_argument("--stream", choices=("all", "default", "chain", "mixed"),
                   default="all")
    p.add_argument("--tails", action="store_true")
    p.add_argument("--bench", action="store_true")
    p.add_argument("--bench-only", action="store_true",
                   help="skip parity/determinism and run only the requested benchmark")
    p.add_argument("--quick", action="store_true")
    p.add_argument("--no-error-paths", action="store_true")
    args = p.parse_args()
    if not os.path.exists(CPU_SO):
        p.error(f"missing {CPU_SO}; run `make -C c/magma blaze_so`")
    if not os.path.exists(METAL_DYLIB):
        p.error(f"missing {METAL_DYLIB}; run `make -C c/magma blaze_metal_dylib`")

    with tempfile.TemporaryDirectory(prefix="blaze-metal-verify-") as td:
        snapshot = args.snapshot or write_synthetic_snapshot(
            os.path.join(td, "synthetic.bsnp"))
        if args.bench_only:
            benchmark(snapshot, args.n, args.decisions, device=args.device)
            return
        decisions = 1 if args.camera_only else (4 if args.quick else args.decisions)
        n = min(args.n, 3) if args.quick else args.n
        streams = ("default",) if args.camera_only else (
            ("default", "chain", "mixed") if args.stream == "all" else
            (args.stream,))
        digest = None
        info = None
        for stream in streams:
            if stream == "chain":
                out = os.path.join(os.path.dirname(os.path.dirname(__file__)),
                                   "out")
                chain_snapshot = args.chain_snapshot or os.path.join(
                    out, "snaps", "s10_t0.bsnp")
                chain_actions = args.chain_actions or os.path.join(
                    out, "chain_actions_s10.json")
                if not os.path.exists(chain_snapshot) or not os.path.exists(
                        chain_actions):
                    raise FileNotFoundError(
                        "chain gate needs an existing snapshot and action file")
                digest, info, ticks = committed_chain_gate(
                    chain_snapshot, chain_actions, device=args.device)
                print(f"CPU==Metal chain: state/cam/depth exact, "
                      f"edge_pixels={info['chain_edge_pixels']}, "
                      f"edge_frames={info['chain_edge_frames']}, "
                      f"edge_max={info['chain_edge_max']}, "
                      f"tools={info['chain_tools']}, "
                      f"actions={os.path.basename(chain_actions)}, "
                      f"ticks={ticks}, sha256={digest}")
            else:
                digest, info = parity_run(
                    n, decisions, snapshot, stream=stream,
                    capture=not args.camera_only, device=args.device)
                print(f"CPU==Metal exact: stream={stream}, n={n}, "
                      f"decisions={decisions}, sha256={digest}")
        if not args.camera_only:
            full_action_op_gate(snapshot, device=args.device)
        repeats = 3
        digests = [metal_digest(n, decisions, snapshot, device=args.device)
                   for _ in range(repeats)]
        if len(set(digests)) != 1:
            raise AssertionError(f"nondeterministic Metal digests: {digests}")
        print(f"deterministic: repeats={repeats}, sha256={digests[0]}")
        if args.tails:
            tail_gate(snapshot, (1, 33) if args.quick else
                      (1, 31, 32, 33, 127, 129), device=args.device)
        if not args.camera_only and not args.no_error_paths:
            long_horizon_regression_gates(td, device=args.device)
            error_gate(snapshot, td, device=args.device)
        print(f"backend: {info['device_name']}, allocations={info['allocation_count']}, "
              f"budget={info['memory_budget'] / 2**20:.1f} MiB")
        if args.bench:
            benchmark(snapshot, n, max(3, decisions), device=args.device)


if __name__ == "__main__":
    main()
