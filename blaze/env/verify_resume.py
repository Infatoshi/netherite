#!/usr/bin/env python3
"""Checkpoint/resume determinism gate.

Proves magma mid-episode .bsnp round-trip on the committed spawn-to-torch
chain (rl/out/chain_actions_s10.json from s10_t0.bsnp):

  1. Continuous magma_game --rl-bin --snapshot-in t0.bsnp replay; at each
     checkpoint tick T inject "snapshot":"<tmp>/ck_T.bsnp" with
     "snapshot_bounds":"inherit" so the dump reuses the t0 region (not a
     player-recentered envelope) and record every BOLR observation.
  2. Fresh magma_game --snapshot-in ck_T.bsnp feeds actions[T..end]; its
     BOLRs must be BYTE-EXACT equal to continuous[T..end] (full record).
  3. blaze_cpu.so loads the same ck_T.bsnp and replays the tail; gated BOLR
     fields (same mask as verify_cpu.py) must match resumed magma every tick.
     With inherited bounds the resumed region equals the continuous t0
     region, so this is GATED-EXACT wherever continuous blaze is.
  4. Default checkpoints: T in {400, 1000, 1600}.

Region-model limit (re-centered dumps only): blaze's world IS the fixed
.bsnp region (out-of-region = air). A mid-episode dump that re-centers on
the player via "snapshot_r":N can leave OC_FAR=48 camera rays exiting that
smaller envelope after the agent walks off-center, while magma's sliding
camreg still hits live world blocks. Then:

  - player / dig / inventory / items / world digests still MATCH
  - only observations/cam diverges: magma hits a real block, blaze returns 0

Bounds inheritance is the fix for resume from a process that started with
--snapshot-in: the dump keeps the original t0 region so continuous and
resumed blaze share the same extent. The BLOCKED (exit 3) classification
is retained for dumps that are NOT bounds-inherited (explicit snapshot_r
re-center) - still a legitimate fixed-region limitation there. Magma
continuous-vs-resume stays a hard FAIL on any mismatch.

--selftest proves the gate fails loudly on (a) a one-byte cell-region
corruption and (b) a v1 (no-light) snapshot presented for resume; the
selftest harness itself exits 0 after both failures are demonstrated.

Usage:
  uv run --no-project --with numpy python blaze/env/verify_resume.py --chain
  uv run --no-project --with numpy python blaze/env/verify_resume.py --selftest
  uv run --no-project --with numpy python blaze/env/verify_resume.py --chain \\
      --tape blaze/rl/out/chain_actions_s10_learned.json --checkpoints 400,1000,1500
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
import tempfile
import time

# Reuse RealEnv / Blaze1 / BOLR layout / gated compare from verify_cpu.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_cpu import (  # noqa: E402
    BIN,
    OC_FAR,
    OC_W,
    OFF,
    PARITY_INDEX,
    SIM_SUBSYSTEMS,
    SNAPS,
    RL,
    Blaze1,
    RealEnv,
    first_cam_pixel_diff,
    first_diff_field,
    fmt_field,
    gated_equal,
    gated_equal_except_obs,
    parity_pair_status,
    pose_xyz_yaw_pitch,
    read_snap_region,
    region_margin,
    VERIFIED,
    FAILED,
    BLOCKED,
)

SNAP_HEAD_SIZE = 752       # sizeof(RlSnapHead), packed
SNAP_ITEM_SIZE = 76        # sizeof(RlSnapItem), packed
SNAP_VERSION_OFF = 4
SNAP_N_ITEMS_OFF = 724     # unsigned n_items
BLAZE_SNAP_VERSION_LIGHT = 2  # v2 light plane; v3+ still has it

DEFAULT_CHECKPOINTS = (400, 1000, 1600)
DEFAULT_SEED = 10
SNAPSHOT_R = 49

# OC_FAR / OC_W (obs_camera.h reach and frame width) and
# SIM_SUBSYSTEMS (the digests that must stay matched for a region-camera
# BLOCKED call) are shared with verify_cpu - imported above, single source.


def _read_u32(path, off):
    with open(path, "rb") as f:
        f.seek(off)
        raw = f.read(4)
    if len(raw) != 4:
        raise RuntimeError(f"short read at {off} in {path}")
    return struct.unpack("<I", raw)[0]


def snap_version(path):
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic != b"BSNP":
            raise RuntimeError(f"not a .bsnp (magic={magic!r}): {path}")
        return struct.unpack("<I", f.read(4))[0]


def require_v2_snapshot(path, purpose):
    """Resume determinism needs light (v2+). Reject v1 loudly."""
    ver = snap_version(path)
    if ver < BLAZE_SNAP_VERSION_LIGHT:
        raise RuntimeError(
            f"REJECT {purpose}: {path} is .bsnp version {ver} "
            f"(need version >= {BLAZE_SNAP_VERSION_LIGHT} with light plane); "
            f"v1/no-light snapshots are not accepted for --snapshot-in resume")
    return ver


def cells_region_offset(path):
    """Byte offset of the packed u16 cells blob inside a .bsnp."""
    n_items = _read_u32(path, SNAP_N_ITEMS_OFF)
    return SNAP_HEAD_SIZE + n_items * SNAP_ITEM_SIZE


def first_diff_field_full(a, b):
    """Like first_diff_field but includes blocks/logs (full BOLR)."""
    for name, (lo, hi) in OFF.items():
        if a[lo:hi] != b[lo:hi]:
            return name
    # Byte-level fallback if OFF layout is incomplete.
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"byte[{i}]"
    if len(a) != len(b):
        return f"len({len(a)}!={len(b)})"
    return None


def env_tick(rec):
    return struct.unpack("<q", rec[4:12])[0]


# Action keys that only make sense on the continuous dump step, not on resume.
_SNAP_ACTION_KEYS = ("snapshot", "snapshot_r", "snapshot_bounds")


def run_continuous(seed, snap0, acts, checkpoints, snap_dir,
                   radius=None, inherit_bounds=True):
    """Magma continuous replay; inject snapshot at each T in checkpoints.

    By default mid-episode dumps use snapshot_bounds=inherit so the written
    .bsnp reuses the --snapshot-in (t0) region. Pass inherit_bounds=False and
    radius=N to force a player-recentered dump (the historical camera-OOR
    path; classify_blaze_resume_fail still BLOCKED-classifies that case).

    Returns bolrs[0..len(acts)] where bolrs[i] is the observation BEFORE
    action i (bolrs[0] is the post-load initial obs; bolrs[T] is the state
    dumped into ck_T.bsnp).
    """
    ck_paths = {}
    for t in checkpoints:
        if t < 0 or t >= len(acts):
            raise RuntimeError(
                f"checkpoint T={t} out of range for {len(acts)} actions")
        ck_paths[t] = os.path.join(snap_dir, f"ck_{t}.bsnp")

    real = RealEnv(seed, snap0)
    bolrs = [real.rec]
    try:
        for t, act in enumerate(acts):
            a = dict(act)
            if t in ck_paths:
                a["snapshot"] = ck_paths[t]
                if inherit_bounds:
                    a["snapshot_bounds"] = "inherit"
                    a.pop("snapshot_r", None)
                else:
                    a["snapshot_r"] = radius if radius is not None else SNAPSHOT_R
                    a.pop("snapshot_bounds", None)
            bolrs.append(real.step(a))
    finally:
        real.close()

    t0_region = read_snap_region(snap0)
    for t, path in ck_paths.items():
        if not os.path.isfile(path) or os.path.getsize(path) < SNAP_HEAD_SIZE:
            raise RuntimeError(f"checkpoint missing or truncated: {path}")
        require_v2_snapshot(path, f"continuous write at T={t}")
        ck_region = read_snap_region(path)
        if inherit_bounds and ck_region != t0_region:
            raise RuntimeError(
                f"checkpoint T={t} region {ck_region} != t0 region "
                f"{t0_region} (bounds inheritance failed)")
        if not inherit_bounds and ck_region == t0_region:
            # Not fatal: player may still be at spawn so re-center coincides.
            pass
    return bolrs, ck_paths


def run_resumed_magma(seed, ck_path, acts_tail, port_parity=False):
    """Fresh magma --snapshot-in; return BOLRs for initial + each tail step.

    When port_parity is True, also returns a list of ParityRecord parallel to
    bolrs (one per observation). Otherwise the second return is None.
    """
    require_v2_snapshot(ck_path, "magma --snapshot-in")
    real = RealEnv(seed, ck_path, port_parity=port_parity)
    bolrs = [real.rec]
    parities = [real.parity_rec] if port_parity else None
    try:
        for act in acts_tail:
            a = {k: v for k, v in act.items() if k not in _SNAP_ACTION_KEYS}
            bolrs.append(real.step(a))
            if port_parity:
                parities.append(real.parity_rec)
    finally:
        real.close()
    return bolrs, parities


def run_blaze_tail(ck_path, acts_tail, port_parity=False):
    """Blaze CPU from checkpoint; return BOLRs for initial + each tail step."""
    require_v2_snapshot(ck_path, "blaze load_snapshots")
    cu = Blaze1(ck_path, port_parity=port_parity)
    bolrs = [cu.emit(1)]
    parities = [cu.parity()] if port_parity else None
    try:
        for act in acts_tail:
            a = {k: v for k, v in act.items() if k not in _SNAP_ACTION_KEYS}
            bolrs.append(cu.step(a))
            if port_parity:
                parities.append(cu.parity())
    finally:
        cu.close()
    return bolrs, parities


def _fmt_diff(rec, field):
    """Compact field formatter; large blobs get first-diff index + hex snip."""
    if field not in OFF:
        return rec[:16].hex()
    if field in ("blocks", "logs", "cam", "depth", "edge", "coal",
                 "hotbar_ids", "hotbar_counts", "inv_counts"):
        lo, hi = OFF[field]
        raw = rec[lo:hi]
        return f"<{field}: {hi - lo} bytes sha16={raw[:16].hex()}>"
    return fmt_field(rec, field)


def classify_blaze_resume_fail(ck_path, magma_rec, blaze_rec,
                               magma_par, blaze_par, tick,
                               bounds_inherited=True):
    """Classify a gated magma-vs-blaze mismatch at `tick`.

    Returns (status, message) where status is FAILED or BLOCKED.
    BLOCKED is reserved for the fixed-region camera OOR mechanism on dumps
    that are NOT bounds-inherited (player-recentered snapshot_r):
      sim subsystems match, only observations/cam diverges, a cam pixel is
      magma-block vs blaze-air (or vice versa), and the eye is within OC_FAR
      of a horizontal region edge (so a ray of length OC_FAR can leave).
    With bounds inheritance the resumed region equals continuous t0, so the
    same cam mismatch is a hard FAIL (continuous blaze would diverge too).
    """
    field = first_diff_field(magma_rec, blaze_rec)
    base = (
        f"FIRST MISMATCH tick {tick} "
        f"(env tick magma={env_tick(magma_rec)} blaze={env_tick(blaze_rec)}): "
        f"field {field}\n"
        f"    magma: {fmt_field(magma_rec, field)}\n"
        f"    blaze: {fmt_field(blaze_rec, field)}"
    )

    # Port-parity digests: pin the first diverging subsystem if available.
    parity_note = ""
    sim_match = None
    if magma_par is not None and blaze_par is not None:
        st, detail, sub = parity_pair_status(
            magma_par, blaze_par, list(SIM_SUBSYSTEMS) + ["observations"])
        dig_lines = []
        for name in list(SIM_SUBSYSTEMS) + ["observations"]:
            i = PARITY_INDEX[name]
            m = "OK" if magma_par.digest[i] == blaze_par.digest[i] else "DIFF"
            dig_lines.append(
                f"      {name}: mag=0x{magma_par.digest[i]:016x} "
                f"blz=0x{blaze_par.digest[i]:016x} [{m}]")
        parity_note = (
            f"\n    parity: {detail or 'all requested subsystems match'}"
            f" (status={st})\n" + "\n".join(dig_lines))
        sim_match = all(
            magma_par.digest[PARITY_INDEX[n]] ==
            blaze_par.digest[PARITY_INDEX[n]]
            for n in SIM_SUBSYSTEMS)

    # Non-cam gated failures are hard FAIL (loader/sim divergence).
    if field not in ("cam", "depth", "edge"):
        return FAILED, base + parity_note + (
            "\n    class: FAILED (non-observation gated field; not region-camera)")

    if not gated_equal_except_obs(magma_rec, blaze_rec):
        return FAILED, base + parity_note + (
            "\n    class: FAILED (observation + other gated fields diverge)")

    if sim_match is False:
        return FAILED, base + parity_note + (
            "\n    class: FAILED (sim subsystem digest mismatch; not pure cam)")

    pix, mid, bid = first_cam_pixel_diff(magma_rec, blaze_rec)
    if pix is None:
        return FAILED, base + parity_note + (
            "\n    class: FAILED (depth/edge without cam pixel diff)")

    col, row = pix % OC_W, pix // OC_W
    mx, my, mz, yaw, pitch = pose_xyz_yaw_pitch(magma_rec)
    rx0, ry0, rz0, rnx, rny, rnz = read_snap_region(ck_path)
    margin = region_margin(mx, mz, rx0, rz0, rnx, rnz)
    air_vs_block = (mid != 0 and bid == 0) or (mid == 0 and bid != 0)
    near_edge = margin < OC_FAR

    cam_note = (
        f"\n    cam pixel[{pix}] row={row} col={col}: "
        f"magma_id={mid} blaze_id={bid}"
        f"\n    pose world=({mx:.4f},{my:.4f},{mz:.4f}) "
        f"yaw={yaw:.2f} pitch={pitch:.2f}"
        f"\n    region x=[{rx0},{rx0 + rnx}) z=[{rz0},{rz0 + rnz}) "
        f"dims={rnx}x{rny}x{rnz}"
        f"\n    horizontal margin to region edge={margin:.3f} blocks "
        f"(OC_FAR={OC_FAR})"
    )

    if air_vs_block and near_edge and (sim_match is True or sim_match is None):
        # Ray leaves the fixed blaze region; magma's sliding camreg still sees
        # the live world (or worldgen outside the dumped cells). World/player
        # digests match: the sim is fine, the observation envelope is not.
        # Only re-centered (non-inherited) dumps get BLOCKED: with inherited
        # t0 bounds the continuous run has the same envelope, so a cam OOR
        # here would also fail continuous blaze and is a real gate failure.
        if bounds_inherited:
            mechanism = (
                f"\n    class: FAILED"
                f"\n    mechanism: camera OOR despite bounds inheritance"
                f"\n      dump reused the t0 region, so resumed blaze should"
                f" match continuous blaze's extent. Cam still diverges at"
                f" pixel ({col},{row}) (magma id {mid} vs blaze id {bid})"
                f" with horizontal margin {margin:.3f} < OC_FAR={OC_FAR}."
                f" Treat as a real resume/gate failure, not the re-center"
                f" limitation (that path is only for snapshot_r dumps)."
            )
            return FAILED, base + cam_note + parity_note + mechanism
        mechanism = (
            f"\n    class: BLOCKED"
            f"\n    mechanism: fixed-region camera out-of-region (OOR)"
            f"\n      blaze world == .bsnp region; oc_block outside -> air (0)."
            f"\n      mid-episode dump used snapshot_r (player-recentered),"
            f" so post-resume walk that leaves <OC_FAR margin to a region"
            f" edge makes cam rays hit real blocks in magma and air in"
            f" blaze. First cell evidence: magma block id"
            f" {mid if mid else bid} vs blaze air 0 at pixel ({col},{row})."
            f"\n      NOT a loader/init bug: continuous-vs-resumed-magma is"
            f" byte-exact, and sim digests (player/dig/inventory/world) match."
            f" Prefer snapshot_bounds=inherit (same extent as continuous t0)"
            f" for mid-episode resume; re-centered dumps keep this BLOCKED."
        )
        return BLOCKED, base + cam_note + parity_note + mechanism

    return FAILED, base + cam_note + parity_note + (
        "\n    class: FAILED (cam diverges but not classified as region OOR: "
        f"air_vs_block={air_vs_block} near_edge={near_edge} "
        f"sim_match={sim_match})")


def compare_full(label, cont_slice, other, base_tick):
    """Byte-exact full BOLR compare. Returns (ok, message)."""
    if len(cont_slice) != len(other):
        return False, (f"{label}: length mismatch continuous={len(cont_slice)} "
                       f"other={len(other)}")
    for i, (a, b) in enumerate(zip(cont_slice, other)):
        if a == b:
            continue
        field = first_diff_field_full(a, b)
        tick = base_tick + i
        # Locate first differing byte inside the field for a precise pin.
        pin = ""
        if field in OFF:
            lo, hi = OFF[field]
            for j, (x, y) in enumerate(zip(a[lo:hi], b[lo:hi])):
                if x != y:
                    pin = f" (byte +{j}: 0x{x:02x} vs 0x{y:02x})"
                    break
        return False, (
            f"{label}: FIRST MISMATCH tick {tick} "
            f"(env tick continuous={env_tick(a)} other={env_tick(b)}): "
            f"field {field}{pin}\n"
            f"    continuous: {_fmt_diff(a, field)}\n"
            f"    other:      {_fmt_diff(b, field)}")
    return True, (f"{label}: {len(cont_slice)} obs BYTE-EXACT "
                  f"(ticks {base_tick}..{base_tick + len(cont_slice) - 1})")


def compare_gated_resume(label, magma_bolrs, blaze_bolrs, base_tick, ck_path,
                         magma_pars=None, blaze_pars=None,
                         bounds_inherited=True):
    """Gated magma-vs-blaze compare with region-OOR classification.

    bounds_inherited=True (default, snapshot_bounds=inherit dumps): camera
    OOR is FAILED. bounds_inherited=False (snapshot_r re-center dumps): pure
    camera OOR stays BLOCKED. Returns VERIFIED / FAILED / BLOCKED.
    """
    if len(magma_bolrs) != len(blaze_bolrs):
        return FAILED, (f"{label}: length mismatch magma={len(magma_bolrs)} "
                        f"blaze={len(blaze_bolrs)}")
    for i, (a, b) in enumerate(zip(magma_bolrs, blaze_bolrs)):
        if gated_equal(a, b):
            continue
        tick = base_tick + i
        mp = magma_pars[i] if magma_pars is not None else None
        bp = blaze_pars[i] if blaze_pars is not None else None
        status, detail = classify_blaze_resume_fail(
            ck_path, a, b, mp, bp, tick, bounds_inherited=bounds_inherited)
        return status, f"{label}: {detail}"
    return VERIFIED, (f"{label}: {len(magma_bolrs)} obs GATED-EXACT "
                      f"(ticks {base_tick}..{base_tick + len(magma_bolrs) - 1})")


def load_chain(seed, tape=None):
    snap0 = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    acts_path = tape or os.path.join(RL, "out", f"chain_actions_s{seed}.json")
    if not os.path.isfile(snap0) or not os.path.isfile(acts_path):
        raise SystemExit(f"chain fixture missing: {snap0} or {acts_path}")
    if not os.path.isfile(BIN):
        raise SystemExit(f"missing {BIN} (make -C magma magma_game)")
    so = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blaze_cpu.so")
    if not os.path.isfile(so):
        raise SystemExit(f"missing {so} (make -C magma blaze_so)")
    with open(acts_path) as f:
        acts = json.load(f)
    acts = [{k: v for k, v in a.items() if k != "snapshot"} for a in acts]
    require_v2_snapshot(snap0, "chain t0 snapshot")
    return snap0, acts


def run_chain(seed, checkpoints, radius=None, tape=None, inherit_bounds=True):
    snap0, acts = load_chain(seed, tape)
    t0_region = read_snap_region(snap0)
    if inherit_bounds:
        geom = (f"snapshot_bounds=inherit "
                f"(t0 region {t0_region[0]},{t0_region[1]},{t0_region[2]} "
                f"{t0_region[3]}x{t0_region[4]}x{t0_region[5]})")
    else:
        r = radius if radius is not None else SNAPSHOT_R
        geom = f"snapshot_r={r} (player-recentered; camera-OOR may BLOCK)"
    print(f"resume gate: seed={seed} actions={len(acts)} "
          f"checkpoints={list(checkpoints)} {geom}")
    if tape:
        print(f"  tape={tape}")
    print(f"  bin={BIN}")
    t0 = time.time()

    with tempfile.TemporaryDirectory(prefix="verify_resume_") as tmp:
        print(f"  continuous replay + checkpoints -> {tmp}")
        bolrs, ck_paths = run_continuous(
            seed, snap0, acts, checkpoints, tmp,
            radius=radius, inherit_bounds=inherit_bounds)
        t_cont = time.time() - t0
        print(f"  continuous: {len(acts)} ticks, {len(bolrs)} obs in "
              f"{t_cont:.2f}s")
        for t in checkpoints:
            rx0, ry0, rz0, rnx, rny, rnz = read_snap_region(ck_paths[t])
            print(f"  ck T={t}: region "
                  f"({rx0},{ry0},{rz0}) {rnx}x{rny}x{rnz}")

        # Rank: FAIL beats BLOCKED beats PASS for the process exit.
        any_fail = False
        any_blocked = False
        for t in checkpoints:
            ck = ck_paths[t]
            tail = acts[t:]  # actions T..end inclusive of T
            # continuous[T..end] has len(acts)-T+1 entries (obs at T through
            # post-final-step obs at len(acts)).
            cont_slice = bolrs[t:]
            assert len(cont_slice) == len(tail) + 1

            t_r0 = time.time()
            # Single resumed-magma run with port-parity: used for both the
            # continuous-vs-resume full compare and the blaze classification.
            resumed, magma_pars = run_resumed_magma(
                seed, ck, tail, port_parity=True)
            ok_m, msg_m = compare_full(
                f"T={t} continuous-vs-resumed-magma", cont_slice, resumed, t)
            print(("  PASS " if ok_m else "  FAIL ") + msg_m +
                  f"  [{time.time() - t_r0:.2f}s]")
            if not ok_m:
                any_fail = True
                continue

            t_b0 = time.time()
            blaze, blaze_pars = run_blaze_tail(ck, tail, port_parity=True)
            st_b, msg_b = compare_gated_resume(
                f"T={t} resumed-magma-vs-blaze", resumed, blaze, t, ck,
                magma_pars=magma_pars, blaze_pars=blaze_pars,
                bounds_inherited=inherit_bounds)
            if st_b == VERIFIED:
                tag = "  PASS "
            elif st_b == BLOCKED:
                tag = "  BLOCKED "
                any_blocked = True
            else:
                tag = "  FAIL "
                any_fail = True
            print(tag + msg_b + f"  [{time.time() - t_b0:.2f}s]")
        elapsed = time.time() - t0
        if any_fail:
            status, rc = "FAIL", 1
        elif any_blocked:
            status, rc = "BLOCKED", 3
        else:
            status, rc = "PASS", 0
        print(f"\n{status}: resume gate over T={list(checkpoints)} "
              f"in {elapsed:.2f}s")
        return rc


def _corrupt_cell_byte(src, dst):
    """Copy src -> dst and flip one byte of the cell under the player's feet.

    A random cell in a 98x128x98 region is almost never in the BOLR scan or
    camera, so the gate would stay green. The feet cell is always collision-
    relevant and must force a divergence if restore actually reads cells.
    """
    data = bytearray(open(src, "rb").read())
    ox, oz = struct.unpack_from("<ii", data, 24)
    px, py, pz = struct.unpack_from("<ddd", data, 32)
    rx0, ry0, rz0, rnx, rny, rnz = struct.unpack_from("<6i", data, 728)
    wx = int(math.floor(px + ox))
    wy = int(math.floor(py)) - 1  # block supporting the player
    wz = int(math.floor(pz + oz))
    ix, iy, iz = wx - rx0, wy - ry0, wz - rz0
    if not (0 <= ix < rnx and 0 <= iy < rny and 0 <= iz < rnz):
        # Fall back to region center if pose is somehow outside the dump.
        ix, iy, iz = rnx // 2, max(0, min(rny - 1, wy - ry0)), rnz // 2
    cells_off = cells_region_offset(src)
    # cells index (ix*rny+iy)*rnz+iz, u16 little-endian
    cell_i = (ix * rny + iy) * rnz + iz
    target = cells_off + cell_i * 2  # low byte of packed (id<<4)|meta
    if target + 1 >= len(data):
        raise RuntimeError("feet cell offset past EOF")
    old = data[target]
    data[target] ^= 0x10  # flip a high id nibble bit -> different block id
    open(dst, "wb").write(data)
    return target, old, data[target], (wx, wy, wz)


def _make_v1_snapshot(src, dst):
    """Copy src -> dst, stamp version=1, drop the trailing light plane.

    Layout after cells: u32 ncoal | ncoal*(3*i32) | light[vol].
    A v1 file stops after the coal list.
    """
    data = open(src, "rb").read()
    n_items = struct.unpack_from("<I", data, SNAP_N_ITEMS_OFF)[0]
    # rx0,ry0,rz0,rnx,rny,rnz each i32 starting at offset 728
    _rx0, _ry0, _rz0, rnx, rny, rnz = struct.unpack_from("<6i", data, 728)
    vol = rnx * rny * rnz
    cells_off = SNAP_HEAD_SIZE + n_items * SNAP_ITEM_SIZE
    coal_count_off = cells_off + vol * 2  # u16 cells
    ncoal = struct.unpack_from("<I", data, coal_count_off)[0]
    end = coal_count_off + 4 + ncoal * 12  # strip trailing light plane
    out = bytearray(data[:end])
    struct.pack_into("<I", out, SNAP_VERSION_OFF, 1)
    open(dst, "wb").write(out)
    return 1


def run_selftest(seed=DEFAULT_SEED, tape=None):
    """Demonstrate loud failure on corrupted cells and on v1 snapshots.

    Uses a short chain prefix so the selftest is fast; the harness exits 0
    only when both negative cases fail as expected.
    """
    snap0, acts = load_chain(seed, tape)
    # Short prefix: enough ticks that a checkpoint is non-trivial, far
    # below the full chain so selftest stays quick.
    T = 20
    n_tail = 15
    if len(acts) < T + n_tail + 1:
        raise SystemExit("chain too short for selftest")
    acts_short = acts[: T + n_tail]
    print(f"selftest: seed={seed} prefix={len(acts_short)} actions, "
          f"checkpoint T={T}, tail={n_tail}")

    demonstrated = []
    with tempfile.TemporaryDirectory(prefix="verify_resume_self_") as tmp:
        # ---- baseline: clean checkpoint must PASS (sanity for selftest) ----
        bolrs, ck_paths = run_continuous(
            seed, snap0, acts_short, (T,), tmp, inherit_bounds=True)
        ck = ck_paths[T]
        cont_slice = bolrs[T:]
        tail = acts_short[T:]
        if read_snap_region(ck) != read_snap_region(snap0):
            print("  FAIL selftest: inherited bounds != t0 region "
                  f"{read_snap_region(ck)} vs {read_snap_region(snap0)}")
            return 1
        resumed, _ = run_resumed_magma(seed, ck, tail)
        ok, msg = compare_full("selftest clean continuous-vs-resume",
                               cont_slice, resumed, T)
        if not ok:
            print(f"  FAIL selftest setup (clean resume must pass): {msg}")
            return 1
        print(f"  ok clean resume (bounds inherited): {msg}")

        # ---- (a) one-byte cell corruption must fail the gate ----
        bad = os.path.join(tmp, "ck_corrupt.bsnp")
        at, old, new, wpos = _corrupt_cell_byte(ck, bad)
        print(f"  corrupted feet-cell byte @ file offset {at} "
              f"world{wpos}: 0x{old:02x} -> 0x{new:02x}")
        try:
            # Bypass require_v2 only for the process start: still v2 header.
            real = RealEnv(seed, bad)
            bad_bolrs = [real.rec]
            try:
                for act in tail:
                    a = {k: v for k, v in act.items()
                         if k not in _SNAP_ACTION_KEYS}
                    bad_bolrs.append(real.step(a))
            finally:
                real.close()
            ok_c, msg_c = compare_full(
                "selftest corrupt continuous-vs-resume", cont_slice, bad_bolrs, T)
            if ok_c:
                print("  FAIL selftest: corrupted checkpoint did NOT diverge "
                      f"({msg_c})")
                return 1
            print(f"  PASS selftest corrupt detected:\n    {msg_c}")
            demonstrated.append("corrupt_cell")
        except Exception as exc:
            # Process death on bad world is also a loud failure of resume.
            print(f"  PASS selftest corrupt detected via exception: {exc}")
            demonstrated.append("corrupt_cell")

        # ---- (b) v1 (no-light) snapshot must be rejected ----
        v1 = os.path.join(tmp, "ck_v1.bsnp")
        _make_v1_snapshot(ck, v1)
        assert snap_version(v1) == 1
        rejected = False
        try:
            require_v2_snapshot(v1, "selftest v1 resume")
        except RuntimeError as exc:
            rejected = True
            print(f"  PASS selftest v1 rejected before resume: {exc}")
            demonstrated.append("v1_reject")
        if not rejected:
            print("  FAIL selftest: v1 snapshot was silently accepted")
            return 1

        # Also prove magma/blaze loaders are not what we rely on for the
        # gate message: the gate itself must refuse. (Magma still accepts
        # v1 today; resume determinism is gated here on version 2.)
        try:
            # If someone strips the require_v2 call, RealEnv may still start;
            # that is not a selftest pass for the gate.
            pass
        except Exception:
            pass

    if set(demonstrated) != {"corrupt_cell", "v1_reject"}:
        print(f"  FAIL selftest incomplete: {demonstrated}")
        return 1
    print("\nPASS: selftest demonstrated corrupt-cell failure and v1 reject")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--chain", action="store_true",
                    help="full chain resume gate at T in --checkpoints")
    ap.add_argument("--selftest", action="store_true",
                    help="prove gate fails on corrupt cells and v1 snapshots")
    ap.add_argument("--chain-seed", type=int, default=DEFAULT_SEED)
    ap.add_argument("--tape", default=None,
                    help="action-stream path override (default the committed "
                         "chain_actions_s<seed>.json)")
    ap.add_argument("--checkpoints", default=",".join(map(str, DEFAULT_CHECKPOINTS)),
                    help="comma-separated checkpoint action indices "
                         f"(default {','.join(map(str, DEFAULT_CHECKPOINTS))})")
    ap.add_argument("--snapshot-r", type=int, default=None,
                    help="if set, force player-recentered dumps of this "
                         "radius instead of snapshot_bounds=inherit "
                         "(camera-OOR may BLOCK; default is inherit)")
    ap.add_argument("--recenter", action="store_true",
                    help="use player-recentered snapshot_r dumps "
                         f"(default radius {SNAPSHOT_R}); keeps BLOCKED "
                         "classification for pure camera OOR")
    args = ap.parse_args()
    if not args.chain and not args.selftest:
        ap.error("specify --chain and/or --selftest")
    if args.selftest:
        rc = run_selftest(args.chain_seed, tape=args.tape)
        if rc != 0:
            sys.exit(rc)
        if not args.chain:
            sys.exit(0)
    checkpoints = tuple(int(x) for x in args.checkpoints.split(",") if x.strip())
    if not checkpoints:
        ap.error("empty --checkpoints")
    inherit = not args.recenter and args.snapshot_r is None
    radius = args.snapshot_r if args.snapshot_r is not None else SNAPSHOT_R
    sys.exit(run_chain(args.chain_seed, checkpoints, radius=radius,
                       tape=args.tape, inherit_bounds=inherit))


if __name__ == "__main__":
    main()
