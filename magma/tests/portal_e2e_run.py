#!/usr/bin/env python3
"""Portal E2E oracle capture + C dim mesh pixel compare.

Flow (live qrl on :25575, world seed 0):
  1. Overworld: build obsidian frame, light with fire, assert portal (id 90).
  2. Enter naturally -> dim=-1; dump Nether; tower +7 look 360@pitch0; C vs oracle.
  3. Build 11-eyed End frame ring, place final eye through real interaction, enter End.
  4. Dump End; fixed-pose 360 look; C terrain/sky vs oracle.

Portal construction/entry is a mechanics gate. The visual pixel gates cover the
dimension look dumps; they do not claim parity for the End-portal tile renderer.

Writes artifacts under /tmp/portal_e2e/ and a results.json for pytest gates.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "java"))
import qrl_client  # noqa: E402

OUT = Path(os.environ.get("PORTAL_E2E_OUT", "/tmp/portal_e2e"))
MAGMA = REPO / "magma"
BLAZE = REPO / "blaze" / "core"
DIFF = REPO / "java" / "render-opt" / "wholeframe" / "diff_frame.py"
W, H = 854, 480
EYE_H = 1.62
ACTIVE_RESULTS = None


def write_results(results: dict) -> None:
    """Atomically checkpoint verifier state for crashes and scoring failures."""
    path = OUT / "results.json"
    tmp = OUT / ".results.json.tmp"
    tmp.write_text(json.dumps(results, indent=2, default=str))
    os.replace(tmp, path)


def checkpoint_capture(results: dict) -> None:
    results["capture_status"] = "running"
    results["ok"] = False
    write_results(results)


def checked_file_response(response: dict, path: Path, expected_bytes: int) -> None:
    """Reject failed or stale QRL binary artifacts before they reach the scorer."""
    if response.get("ok") is not True:
        raise RuntimeError(f"QRL did not write {path}: {response}")
    if not path.is_file():
        raise RuntimeError(f"QRL reported success but {path} is missing: {response}")
    actual = path.stat().st_size
    if actual != expected_bytes:
        raise RuntimeError(
            f"QRL wrote {actual} bytes to {path}, expected {expected_bytes}: {response}"
        )


def checked_dump_response(response: dict, path: Path) -> None:
    required = ("cx0", "cz0", "cx1", "cz1")
    if not all(k in response for k in required):
        raise RuntimeError(f"dump response lacks chunk bounds for {path}: {response}")
    chunks = ((response["cx1"] - response["cx0"] + 1)
              * (response["cz1"] - response["cz0"] + 1))
    checked_file_response(response, path, chunks * 16 * 16 * 256 * 2)


def verify_dump_stable(e, original: Path, original_meta: dict, label: str) -> None:
    """Prove the client-world input did not change during the oracle views."""
    check = OUT / f".{label}.stability.mcbd"
    try:
        response = e._cmd({"cmd": "dumpblocks", "action": {
            "radius": 9, "file": str(check),
        }})
        checked_dump_response(response, check)
        bounds = tuple(response[k] for k in ("cx0", "cz0", "cx1", "cz1"))
        original_bounds = tuple(
            original_meta[k] for k in ("cx0", "cz0", "cx1", "cz1")
        )
        if bounds != original_bounds or file_sha256(check) != file_sha256(original):
            raise RuntimeError(
                f"{label} client-world dump changed during capture: "
                f"{original_bounds} -> {bounds}"
            )
    finally:
        check.unlink(missing_ok=True)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture_input_paths() -> list[Path]:
    """Enumerate every live-oracle input, excluding reproducible C outputs."""
    patterns = (
        "oracle_*.png", "*_camera.json", "*_views.json", "*.mcbd", "*.bin",
        "capture_metadata.json",
    )
    return sorted({p for pattern in patterns for p in OUT.glob(pattern)})


def required_capture_names() -> set[str]:
    required = {
        "capture_metadata.json", "portal_frame.bin", "end_portal_probe.bin",
        "ow_portal.mcbd", "nether_look.mcbd", "end_look.mcbd",
        "portal_pre_camera.json", "portal_lit_camera.json", "end_portal_camera.json",
        "oracle_portal_pre.png", "oracle_portal_lit.png", "oracle_end_portal.png",
        "nether_look_views.json", "end_look_views.json",
    }
    for prefix in ("nether", "end"):
        for i in range(8):
            required.add(f"oracle_{prefix}_look_{i:02d}.png")
            required.add(f"{prefix}_look_{i:02d}_camera.json")
    return required


def make_capture_manifest() -> dict:
    """Hash live-oracle inputs, excluding reproducible C outputs."""
    paths = capture_input_paths()
    required = required_capture_names()
    present = {p.name for p in paths}
    if missing := sorted(required - present):
        raise RuntimeError(f"capture is missing required live inputs: {missing}")
    return {
        p.name: {"bytes": p.stat().st_size, "sha256": file_sha256(p)}
        for p in paths
    }


def validate_capture_manifest(manifest: dict) -> None:
    if not manifest:
        raise RuntimeError("capture manifest is empty")
    paths = capture_input_paths()
    present = {p.name for p in paths}
    if missing := sorted(required_capture_names() - present):
        raise RuntimeError(f"capture is missing required live inputs: {missing}")
    if set(manifest) != present:
        raise RuntimeError(
            "capture manifest membership changed: "
            f"missing={sorted(present - set(manifest))} "
            f"unexpected={sorted(set(manifest) - present)}"
        )
    for name, expected in manifest.items():
        path = OUT / name
        if not path.is_file():
            raise RuntimeError(f"captured input is missing: {path}")
        actual_bytes = path.stat().st_size
        actual_hash = file_sha256(path)
        if actual_bytes != expected.get("bytes") or actual_hash != expected.get("sha256"):
            raise RuntimeError(
                f"captured input changed since capture: {path} "
                f"bytes={actual_bytes} sha256={actual_hash}"
            )


def grab(path: Path) -> None:
    env = os.environ.copy()
    env["DISPLAY"] = env.get("DISPLAY", ":1")
    geom = subprocess.check_output(
        ['bash', '-c', 'xwininfo -root -tree 2>/dev/null | grep -i "Minecraft 1.11.2" | head -1'],
        env=env, text=True,
    )
    m = re.search(r"(\d+)x(\d+)\+(\d+)\+(\d+)", geom)
    abs_m = re.search(r"\+(\d+)\+(\d+)\s*$", geom.strip())
    if not m or not abs_m:
        raise RuntimeError(f"MC window not found: {geom!r}")
    w, h = int(m.group(1)), int(m.group(2))
    ax, ay = int(abs_m.group(1)), int(abs_m.group(2))
    subprocess.check_call(
        [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "x11grab", "-draw_mouse", "0",
            "-video_size", f"{w}x{h}", "-i", f":1.0+{ax},{ay}",
            "-frames:v", "1", str(path),
        ],
        env=env,
    )


def wait_dim(e, target, n=120, stable_ticks=3):
    """Require the target dimension across enough real ticks to reject a portal bounce."""
    stable = 0
    o = e.obs()
    for _ in range(n):
        if o.get("dim") == target and o.get("ok") and o.get("y") is not None:
            stable += 1
            if stable >= stable_ticks:
                return o
        else:
            stable = 0
        o = e.step({})
    raise RuntimeError(f"dimension never reached {target}: {o}")


def request_dim(e, target):
    """Run a scripted setup transfer and require its command acknowledgement."""
    response = e._cmd({"cmd": "dim", "action": {"id": target}})
    if response.get("ok") is not True or response.get("dim") != target:
        raise RuntimeError(f"dimension command {target} failed: {response}")
    return wait_dim(e, target)


def settle(e, x, y, z, yaw, pitch, ticks=40, expected_dim=None):
    """Set and verify a no-gravity capture pose; never grab a drifting camera."""
    target = {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch}
    cam = None
    stable = 0
    for _ in range(ticks):
        # Reassert before checking so an older /tp or dimension-sync packet cannot
        # win the command race. Real ticks also settle fog interpolation and chunks.
        placed = e._cmd({"cmd": "set_pose", "action": target})
        if placed.get("ok") is not True or placed.get("no_gravity") is not True:
            raise RuntimeError(f"set_pose failed: {placed}")
        e.step({})
        cam = e._cmd({"cmd": "camera", "action": {}})
        pose_ok = (
            (expected_dim is None or cam.get("world", {}).get("dim") == expected_dim)
            and abs(float(cam.get("feet_x", 1e9)) - x) <= 0.05
            and abs(float(cam.get("feet_y", 1e9)) - y) <= 0.05
            and abs(float(cam.get("feet_z", 1e9)) - z) <= 0.05
            and abs(float(cam.get("yaw", 1e9)) - yaw) <= 0.05
            and abs(float(cam.get("pitch", 1e9)) - pitch) <= 0.05
            and cam.get("no_gravity") is True
        )
        if pose_ok:
            stable += 1
            time.sleep(0.01)
        else:
            # Dimension sync can deliver one stale motion packet after the
            # authoritative placement. Keep reasserting, then require a stable
            # tail rather than accepting any transient sample.
            stable = 0
    if cam is None:
        raise RuntimeError(f"settle requires at least one tick: {ticks}")
    if stable < min(5, ticks):
        raise RuntimeError(
            f"camera did not stabilize at deterministic pose {target}: {cam}"
        )


def drop_to_ground(e, x, y, z, expected_dim):
    target = {
        "x": x, "y": y, "z": z, "yaw": 0.0, "pitch": 0.0,
        "no_gravity": False,
    }
    placed = e._cmd({"cmd": "set_pose", "action": target})
    if placed.get("ok") is not True:
        raise RuntimeError(f"drop set_pose failed: {placed}")
    for _ in range(240):
        o = e.step({})
        if o.get("dim") != expected_dim:
            raise RuntimeError(f"dimension changed while finding ground: {o}")
        if o.get("on_ground") is True:
            return o
    raise RuntimeError(f"player never reached ground from {target}: {o}")


def wait_render_ready(e, expected_dim, ticks=600, stable_ticks=5):
    """Wait for the client chunk set and async render queues, not wall-clock time."""
    stable = 0
    last_chunks = None
    cam = None
    for _ in range(ticks):
        e.step({})
        cam = e._cmd({"cmd": "camera", "action": {}})
        ready = (
            cam.get("world", {}).get("dim") == expected_dim
            and cam.get("render_chunks_ready") is True
            and isinstance(cam.get("client_chunks_loaded_radius"), int)
            and cam.get("client_chunks_loaded_radius") > 0
            and (m := re.match(r"C: (\d+)/", cam.get("render_debug", "")))
                is not None
            and int(m.group(1)) > 0
        )
        chunks = cam.get("client_chunks_loaded_radius")
        stable = stable + 1 if ready and chunks == last_chunks else (1 if ready else 0)
        last_chunks = chunks
        if stable >= stable_ticks:
            return cam
    raise RuntimeError(
        f"client renderer did not become ready in dimension {expected_dim}: {cam}"
    )


def build_dim_mesh() -> Path:
    out = Path("/tmp/dim_mesh_candidate")
    objs = []
    flags = ["-O2", "-ffp-contract=off", "-Wall", "-Icore", "-I.", f"-I{BLAZE}"]
    units = [
        "world/mesh_mc", "world/light", "world/populate_mc", "assets/blockmodels",
        "renderkernels/rk_31_facebakery_make_quad", "game/sky", "game/caps",
        "core/math", "core/shade", "cpu/raster_cpu", "transform",
    ]
    # always rebuild blockmodels after atlas change
    for u in units:
        o = MAGMA / f"{u}.o"
        subprocess.run(
            ["gcc", *flags, "-c", f"{u}.c", "-o", str(o)],
            cwd=str(MAGMA), check=True,
        )
        objs.append(str(o))
    subprocess.run(
        ["gcc", *flags,
         str(REPO / "verify/mc_capture/dim_mesh_candidate.c"),
         *objs, "-o", str(out), "-lm"],
        cwd=str(MAGMA), check=True,
    )
    return out


def read_rgb(path: Path) -> np.ndarray:
    image = Image.open(path).convert("RGB")
    if image.size != (W, H):
        raise ValueError(f"{path}: image size {image.size}, want {(W, H)}")
    return np.asarray(image, np.float32)


def world_mask() -> np.ndarray:
    """Exclude vanilla HUD/hand pixels while retaining the rendered world."""
    mask = np.ones((H, W), dtype=bool)
    mask[370:, :] = False
    mask[330:370, 580:] = False  # top of first-person arm
    mask[232:249, 418:437] = False  # crosshair
    return mask


def image_metrics(oracle: np.ndarray, candidate: np.ndarray, mask: np.ndarray) -> dict:
    if oracle.shape != (H, W, 3) or candidate.shape != oracle.shape:
        raise ValueError(f"metric shape mismatch: {oracle.shape} vs {candidate.shape}")
    if mask.shape != (H, W) or not np.any(mask):
        raise ValueError("metric mask is empty or has the wrong shape")
    a = oracle[mask]
    b = candidate[mask]
    x = a.mean(axis=1)
    y = b.mean(axis=1)
    x -= x.mean()
    y -= y.mean()
    den = float(np.linalg.norm(x) * np.linalg.norm(y))
    corr = float(np.dot(x, y) / den) if den > 1e-12 else 0.0

    # Coarse 80px cells reject per-face netherrack UV phase while retaining
    # silhouettes, occlusion, and light/fog support. A flat clear remains zero.
    block = 80
    bh, bw = H // block, W // block
    hh, ww = bh * block, bw * block
    valid_mask = mask[:hh, :ww]
    counts = valid_mask.reshape(bh, block, bw, block).sum(axis=(1, 3))
    valid = counts >= block * block * 0.5
    if not np.any(valid):
        valid = counts > 0
    lum_a = oracle[:hh, :ww].mean(axis=2)
    lum_b = candidate[:hh, :ww].mean(axis=2)
    coarse_a = ((lum_a * valid_mask).reshape(bh, block, bw, block)
                .sum(axis=(1, 3))[valid] / counts[valid])
    coarse_b = ((lum_b * valid_mask).reshape(bh, block, bw, block)
                .sum(axis=(1, 3))[valid] / counts[valid])
    if coarse_a.size == 0:
        coarse_corr = 0.0
    else:
        coarse_a -= coarse_a.mean()
        coarse_b -= coarse_b.mean()
        norm_a = float(np.linalg.norm(coarse_a))
        norm_b = float(np.linalg.norm(coarse_b))
        # A constant RGB image accumulates about 1.5e-5 norm from masked
        # float reductions. Treat that measured numerical residue as flat.
        coarse_corr = float(np.dot(coarse_a, coarse_b) / (norm_a * norm_b)) \
            if norm_a > 1e-4 and norm_b > 1e-4 else 0.0
    lum_oracle = oracle.mean(axis=2)
    lum_candidate = candidate.mean(axis=2)
    mx = mask[:, 1:] & mask[:, :-1]
    my = mask[1:, :] & mask[:-1, :]
    def edge_energy(image):
        parts = []
        if np.any(mx):
            parts.append(float(np.abs(np.diff(image, axis=1))[mx].mean()))
        if np.any(my):
            parts.append(float(np.abs(np.diff(image, axis=0))[my].mean()))
        return float(np.mean(parts)) if parts else 0.0

    oracle_edge = edge_energy(lum_oracle)
    candidate_edge = edge_energy(lum_candidate)
    return {
        "pixels": int(mask.sum()),
        "mae": float(np.abs(a - b).mean()),
        "gcorr": corr,
        "coarse_gcorr": coarse_corr,
        "oracle_edge_energy": oracle_edge,
        "candidate_edge_energy": candidate_edge,
        "edge_energy_ratio": candidate_edge / oracle_edge if oracle_edge > 1e-12 else 1.0,
        "exact_fraction": float(np.all(a == b, axis=1).mean()),
        "oracle_mean_rgb": [float(v) for v in a.mean(axis=0)],
        "candidate_mean_rgb": [float(v) for v in b.mean(axis=0)],
    }


def project_aabb_mask(cam: dict, bounds: tuple[float, ...], pad: int = 2) -> np.ndarray:
    """Project a world AABB with magma's exact first-person camera convention."""
    x0, y0, z0, x1, y1, z1 = bounds
    yaw = math.radians(float(cam["magma_yaw_deg"]))
    pitch = math.radians(float(cam["magma_pitch_deg"]))
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    tan_half = math.tan(math.radians(float(cam["fov_effective"])) * 0.5)
    aspect = W / H
    points = []
    for x in (x0, x1):
        for y in (y0, y1):
            for z in (z0, z1):
                dx = x - float(cam["eye_x"])
                dy = y - float(cam["eye_y"])
                dz = z - float(cam["eye_z"])
                rx = cy * dx - sy * dz
                rz = sy * dx + cy * dz
                ey = cp * dy + sp * rz
                ez = -sp * dy + cp * rz + 0.05
                if ez >= -0.05:
                    continue
                sx = (rx / (-ez * tan_half * aspect) * 0.5 + 0.5) * W
                sy_screen = (0.5 - ey / (-ez * tan_half) * 0.5) * H
                points.append((sx, sy_screen))
    mask = np.zeros((H, W), dtype=bool)
    if not points:
        return mask
    left = max(0, int(math.floor(min(p[0] for p in points))) - pad)
    right = min(W, int(math.ceil(max(p[0] for p in points))) + pad + 1)
    top = max(0, int(math.floor(min(p[1] for p in points))) - pad)
    bottom = min(H, int(math.ceil(max(p[1] for p in points))) + pad + 1)
    if left < right and top < bottom:
        mask[top:bottom, left:right] = True
    return mask


def binary_overlap(oracle: np.ndarray, candidate: np.ndarray, mask: np.ndarray) -> dict:
    def hot(a):
        return (
            (a[:, :, 0] >= 140)
            & (a[:, :, 1] >= 40)
            & (a[:, :, 0] >= 1.2 * a[:, :, 1])
            & (a[:, :, 1] >= 1.05 * a[:, :, 2])
            & mask
        )

    a = hot(oracle)
    b = hot(candidate)
    intersection = int((a & b).sum())
    union = int((a | b).sum())
    na, nb = int(a.sum()), int(b.sum())
    return {
        "roi_pixels": int(mask.sum()),
        "oracle_pixels": na,
        "candidate_pixels": nb,
        "intersection": intersection,
        "iou": float(intersection / union) if union else 0.0,
        "recall": float(intersection / na) if na else 0.0,
        "precision": float(intersection / nb) if nb else 0.0,
    }


def mask_overlap(oracle_mask: np.ndarray, candidate_mask: np.ndarray) -> dict:
    intersection = int((oracle_mask & candidate_mask).sum())
    union = int((oracle_mask | candidate_mask).sum())
    no, nc = int(oracle_mask.sum()), int(candidate_mask.sum())
    return {
        "oracle_pixels": no,
        "candidate_pixels": nc,
        "intersection": intersection,
        "iou": float(intersection / union) if union else 1.0,
        "recall": float(intersection / no) if no else 1.0,
        "precision": float(intersection / nc) if nc else 1.0,
    }


def render_c(
    binp: Path,
    mcbd: Path,
    cam: dict,
    ppm: Path,
    clear,
    dimension: int,
    cx0: int,
    cz0: int,
    cx1: int,
    cz1: int,
    disable_fire: bool = False,
) -> dict:
    # magma yaw/pitch from camera dump
    cyaw = float(cam.get("magma_yaw_deg", 180.0 - float(cam["yaw"])))
    cpitch = float(cam.get("magma_pitch_deg", -float(cam["pitch"])))
    fov = float(cam.get("fov_effective", 77.0))
    eye = (float(cam["eye_x"]), float(cam["eye_y"]), float(cam["eye_z"]))
    far_plane = float(cam.get("far_plane", 128.0))
    fog_start = float(cam["fog_start"])
    fog_end = float(cam["fog_end"])
    cmd = [
        str(binp), "--mcbd", str(mcbd),
        "--cx0", str(cx0), "--cz0", str(cz0), "--cx1", str(cx1), "--cz1", str(cz1),
        "--eye", str(eye[0]), str(eye[1]), str(eye[2]),
        "--yaw", str(cyaw), "--pitch", str(cpitch), "--fov", str(fov),
        "--clear", str(clear[0]), str(clear[1]), str(clear[2]),
        "--dimension", str(dimension),
        "--torch-flicker", "0",
        "--gamma", str(cam.get("options", {}).get("gamma", 0.0)),
        "--far-plane", str(far_plane),
        "--mesh-radius", str(int(cam.get("render_distance_chunks", 8))),
        "--fog-start", str(fog_start), "--fog-end", str(fog_end),
        "--mask", str(ppm.with_name(ppm.stem + "_terrain.pgm")),
        "--background-ppm", str(ppm.with_name(ppm.stem + "_background.ppm")),
        "--ppm", str(ppm),
    ]
    if disable_fire:
        cmd.append("--disable-fire")
    run = subprocess.run(cmd, check=True, capture_output=True, text=True)
    png = ppm.with_suffix(".png")
    Image.open(ppm).convert("RGB").save(png)
    mask = ppm.with_name(ppm.stem + "_terrain.pgm")
    match = re.search(
        r"fallback=(\d+) meta_supported=(\d+) meta_unsupported=(\d+)",
        run.stderr,
    )
    if not match:
        raise RuntimeError(f"candidate omitted coverage counters:\n{run.stderr}")
    return {
        "png": png,
        "terrain_mask": mask,
        "background": ppm.with_name(ppm.stem + "_background.ppm"),
        "fallback_nonair": int(match.group(1)),
        "supported_nonzero_meta": int(match.group(2)),
        "unsupported_nonzero_meta": int(match.group(3)),
        "stderr": run.stderr,
    }


def count_ids(path: Path, expect_shape=None) -> dict:
    raw = np.fromfile(path, dtype="<u2")
    ids = raw >> 4
    from collections import Counter
    return dict(Counter(ids.tolist()))


def compare_dimension_views(
    binp: Path,
    prefix: str,
    dimension: int,
    dump_meta: dict,
) -> dict:
    views = json.loads((OUT / f"{prefix}_look_views.json").read_text())
    if len(views) != 8 or [v.get("i") for v in views] != list(range(8)):
        raise ValueError(f"{prefix}: need camera records 0..7, got {len(views)}")
    expected_options = {
        "gamma": 0.0, "renderDistance": 8, "fancyGraphics": False,
        "ao": 0, "mipmapLevels": 0, "particles": 2,
    }
    fixed_pose = None
    fixed_client_chunks = None
    for view in views:
        i = view["i"]
        cam = view["camera"]
        standalone = json.loads((OUT / f"{prefix}_look_{i:02d}_camera.json").read_text())
        if standalone != cam:
            raise ValueError(f"{prefix} view {i}: camera files disagree")
        world = cam.get("world", {})
        if world.get("seed") != 0 or world.get("dim") != dimension:
            raise ValueError(f"{prefix} view {i}: wrong world provenance: {world}")
        if cam.get("display_w") != W or cam.get("display_h") != H:
            raise ValueError(f"{prefix} view {i}: wrong capture size")
        if cam.get("schema") != "qrl.camera.v1":
            raise ValueError(f"{prefix} view {i}: wrong camera schema")
        if not isinstance(cam.get("boss_fog"), bool):
            raise ValueError(f"{prefix} view {i}: boss_fog is not a boolean")
        if (cam.get("fog_mode") != 9729
                or cam.get("nv_fog_distance") is not True
                or cam.get("fog_distance_mode_nv") != 34139):
            raise ValueError(
                f"{prefix} view {i}: capture did not use linear radial fog: "
                f"mode={cam.get('fog_mode')} nv={cam.get('nv_fog_distance')} "
                f"distance={cam.get('fog_distance_mode_nv')}"
            )
        if cam.get("render_distance_chunks") != 8 or cam.get("far_plane") != 128.0:
            raise ValueError(f"{prefix} view {i}: wrong render distance")
        client_chunks = cam.get("client_chunks_loaded_radius")
        render_match = re.match(r"C: (\d+)/", cam.get("render_debug", ""))
        if (not isinstance(client_chunks, int) or client_chunks <= 0
                or cam.get("client_chunk_window_slots") != 17 * 17
                or cam.get("render_chunks_ready") is not True
                or render_match is None or int(render_match.group(1)) <= 0):
            raise ValueError(
                f"{prefix} view {i}: client renderer was not ready: "
                f"{client_chunks}/{cam.get('client_chunk_window_slots')} "
                f"{cam.get('render_debug')}"
            )
        if fixed_client_chunks is None:
            fixed_client_chunks = client_chunks
        elif client_chunks != fixed_client_chunks:
            raise ValueError(
                f"{prefix} view {i}: client chunk set changed "
                f"{client_chunks} vs {fixed_client_chunks}"
            )
        expected_yaw = float(i * 45)
        if view.get("yaw") != i * 45:
            raise ValueError(f"{prefix} view {i}: mislabeled yaw {view.get('yaw')}")
        yaw_delta = (float(cam.get("yaw", 1e9)) - expected_yaw + 180.0) % 360.0 - 180.0
        expected_cr_yaw = (180.0 - expected_yaw + 180.0) % 360.0 - 180.0
        cr_delta = (float(cam.get("magma_yaw_deg", 1e9))
                    - expected_cr_yaw + 180.0) % 360.0 - 180.0
        if abs(yaw_delta) > 1e-6 or abs(cr_delta) > 1e-6:
            raise ValueError(
                f"{prefix} view {i}: camera yaw mismatch "
                f"mc={cam.get('yaw')} cr={cam.get('magma_yaw_deg')}"
            )
        pitch = float(cam.get("pitch", 1e9))
        cr_pitch = float(cam.get("magma_pitch_deg", 1e9))
        if abs(pitch) > 1e-6 or abs(cr_pitch + pitch) > 1e-6:
            raise ValueError(
                f"{prefix} view {i}: camera pitch mismatch "
                f"mc={pitch} cr={cr_pitch}"
            )
        eye_height = float(cam.get("eye_height", 1e9))
        if (abs(eye_height - EYE_H) > 1e-6
                or abs(float(cam.get("eye_x", 1e9))
                       - float(cam.get("feet_x", -1e9))) > 1e-6
                or abs(float(cam.get("eye_z", 1e9))
                       - float(cam.get("feet_z", -1e9))) > 1e-6
                or abs(float(cam.get("eye_y", 1e9))
                       - float(cam.get("feet_y", -1e9)) - eye_height) > 1e-6):
            raise ValueError(f"{prefix} view {i}: inconsistent eye/feet pose")
        options = cam.get("options", {})
        if any(options.get(k) != v for k, v in expected_options.items()):
            raise ValueError(f"{prefix} view {i}: wrong graphics options: {options}")
        pose = tuple(float(cam[k]) for k in ("feet_x", "feet_y", "feet_z", "pitch"))
        if fixed_pose is None:
            fixed_pose = pose
        elif any(abs(a - b) > 1e-6 for a, b in zip(pose, fixed_pose)):
            raise ValueError(f"{prefix} view {i}: capture pose drifted: {pose} vs {fixed_pose}")
        if cam.get("no_gravity") is not True:
            raise ValueError(f"{prefix} view {i}: capture pose is not locked")
        fog_rgb8 = cam.get("fog_rgb8")
        if (not isinstance(fog_rgb8, list) or len(fog_rgb8) != 3
                or any(not isinstance(v, int) or v < 0 or v > 255
                       for v in fog_rgb8)):
            raise ValueError(f"{prefix} view {i}: missing captured fog RGB: {fog_rgb8}")
        fog_start = cam.get("fog_start")
        fog_end = cam.get("fog_end")
        if (not isinstance(fog_start, (int, float))
                or not isinstance(fog_end, (int, float))
                or not math.isfinite(fog_start) or not math.isfinite(fog_end)
                or fog_start < 0 or fog_end <= fog_start):
            raise ValueError(
                f"{prefix} view {i}: invalid captured fog range "
                f"{fog_start}..{fog_end}"
            )
    center_cx = math.floor(fixed_pose[0] / 16.0)
    center_cz = math.floor(fixed_pose[2] / 16.0)
    apron_radius = int(views[0]["camera"]["render_distance_chunks"]) + 1
    expected_bounds = (
        center_cx - apron_radius, center_cz - apron_radius,
        center_cx + apron_radius, center_cz + apron_radius,
    )
    actual_bounds = tuple(dump_meta[k] for k in ("cx0", "cz0", "cx1", "cz1"))
    if actual_bounds != expected_bounds:
        raise ValueError(
            f"{prefix}: dump bounds {actual_bounds} do not cover RD8 plus apron "
            f"{expected_bounds}"
        )
    source = np.fromfile(OUT / f"{prefix}_look.mcbd", dtype="<u2")
    source_nonzero_meta = int(((source & 15) != 0).sum())
    per_view = []
    analysis = world_mask()
    pinned = True
    fire_anchor_specs = dump_meta.get("fire_anchors", []) if prefix == "nether" else []
    if prefix == "nether" and len(fire_anchor_specs) != 4:
        raise ValueError(f"nether: expected four controlled fire observations: {fire_anchor_specs}")
    if prefix == "nether":
        anchor_pairs = [
            (tuple(spec.get("block", ())), int(spec.get("view", -1)))
            for spec in fire_anchor_specs
        ]
        anchor_blocks = {block for block, _ in anchor_pairs}
        anchor_views = {view for _, view in anchor_pairs}
        if (len(set(anchor_pairs)) != 4 or len(anchor_blocks) != 2
                or anchor_views != {1, 2}
                or set(anchor_pairs) != {
                    (block, view) for block in anchor_blocks for view in anchor_views
                }):
            raise ValueError(
                "nether: fire anchors must be two distinct blocks observed in "
                f"both views 1 and 2: {fire_anchor_specs}"
            )
    fire_anchor_blocks = [tuple(spec["block"]) for spec in fire_anchor_specs]
    fire_anchor_views = {int(spec["view"]) for spec in fire_anchor_specs}
    if fire_anchor_specs:
        ncx = dump_meta["cx1"] - dump_meta["cx0"] + 1
        chunk_cells = 16 * 16 * 256
        for x, y, z in sorted(set(fire_anchor_blocks)):
            cx, cz = x // 16, z // 16
            if not (dump_meta["cx0"] <= cx <= dump_meta["cx1"]
                    and dump_meta["cz0"] <= cz <= dump_meta["cz1"]):
                raise ValueError(f"fire anchor {(x, y, z)} outside dump")
            chunk_i = (cz - dump_meta["cz0"]) * ncx + (cx - dump_meta["cx0"])
            cell_i = (y * 16 + (z & 15)) * 16 + (x & 15)
            packed = int(source[chunk_i * chunk_cells + cell_i])
            support_i = ((y - 1) * 16 + (z & 15)) * 16 + (x & 15)
            support = int(source[chunk_i * chunk_cells + support_i])
            if packed >> 4 != 51 or packed & 15 != 0:
                raise ValueError(f"fire anchor {(x, y, z)} is not meta-0 vanilla fire")
            if support >> 4 != 87:
                raise ValueError(f"fire anchor {(x, y, z)} lacks netherrack support")

    for view in views:
        i = view["i"]
        cam = view["camera"]
        view_clear = tuple(cam["fog_rgb8"])
        required = ("eye_x", "eye_y", "eye_z", "magma_yaw_deg",
                    "magma_pitch_deg", "fov_effective")
        if not all(k in cam for k in required):
            raise ValueError(f"{prefix} view {i}: incomplete camera record")
        pinned = (
            pinned
            and cam.get("texture_animations_pinned") is True
            and cam.get("fire_layer_0_physical_frame") == 0
            and cam.get("fire_layer_1_physical_frame") == 0
        )
        rendered = render_c(
            binp, OUT / f"{prefix}_look.mcbd", cam,
            OUT / f"c_{prefix}_look_{i:02d}.ppm", view_clear, dimension,
            dump_meta["cx0"], dump_meta["cz0"],
            dump_meta["cx1"], dump_meta["cz1"],
        )
        oracle = read_rgb(OUT / f"oracle_{prefix}_look_{i:02d}.png")
        candidate = read_rgb(rendered["png"])
        expected_background = read_rgb(rendered["background"])
        terrain = np.asarray(Image.open(rendered["terrain_mask"]).convert("L")) > 0
        if terrain.shape != (H, W):
            raise ValueError(f"{prefix} view {i}: terrain mask shape {terrain.shape}")
        oracle_nonbackground = (
            np.any(np.abs(oracle - expected_background) > 1.0, axis=2) & analysis
        )
        flat_rgb = np.asarray(view_clear, np.float32)
        oracle_visible_nonfog = (
            np.any(np.abs(oracle - flat_rgb) > 1.0, axis=2) & analysis
        )
        candidate_visible_nonfog = (
            np.any(np.abs(candidate - flat_rgb) > 1.0, axis=2) & analysis
        )
        world = image_metrics(oracle, candidate, analysis)
        terrain_metrics = image_metrics(oracle, candidate, terrain & analysis) \
            if np.any(terrain & analysis) else None
        oracle_nonbackground_metrics = image_metrics(
            oracle, candidate, oracle_nonbackground,
        ) if np.any(oracle_nonbackground) else None
        background_metrics = image_metrics(oracle, candidate, ~terrain & analysis) \
            if np.any(~terrain & analysis) else None
        flat = np.empty_like(candidate)
        flat[:, :, :] = np.asarray(view_clear, np.float32)
        flat_metrics = image_metrics(oracle, flat, analysis)

        record = {
            "i": i,
            "world": world,
            "terrain": terrain_metrics,
            "oracle_nonbackground": oracle_nonbackground_metrics,
            "background": background_metrics,
            "flat_clear_ablation": flat_metrics,
            "terrain_fraction": float(terrain[analysis].mean()),
            "oracle_nonbackground_fraction": float(
                oracle_nonbackground[analysis].mean()
            ),
            "visible_nonfog_silhouette": mask_overlap(
                oracle_visible_nonfog, candidate_visible_nonfog,
            ),
            "fallback_nonair": rendered["fallback_nonair"],
            "supported_nonzero_meta": rendered["supported_nonzero_meta"],
            "unsupported_nonzero_meta": rendered["unsupported_nonzero_meta"],
        }
        record["visible_nonfog_silhouette"]["symmetric_difference_fraction"] = (
            float((oracle_visible_nonfog ^ candidate_visible_nonfog).sum())
            / int(analysis.sum())
        )
        if prefix == "end":
            # The live client does not expose its depth buffer here. With all
            # non-player entities removed, this is an explicit non-background
            # block mask, not an independently captured terrain/depth oracle.
            record["nonbackground_silhouette"] = mask_overlap(
                oracle_nonbackground, terrain & analysis,
            )
            record["missing_nonbackground_ablation"] = (
                image_metrics(oracle, expected_background, oracle_nonbackground)
                if np.any(oracle_nonbackground) else None
            )
            record["missing_nonbackground_silhouette"] = mask_overlap(
                oracle_nonbackground, np.zeros((H, W), dtype=bool),
            )
        if prefix == "nether" and i in fire_anchor_views:
            no_fire_rendered = render_c(
                binp, OUT / f"{prefix}_look.mcbd", cam,
                OUT / f"c_{prefix}_look_{i:02d}_no_fire.ppm", view_clear, dimension,
                dump_meta["cx0"], dump_meta["cz0"],
                dump_meta["cx1"], dump_meta["cz1"], disable_fire=True,
            )
            no_fire_candidate = read_rgb(no_fire_rendered["png"])
            fire_roi = np.zeros((H, W), dtype=bool)
            record["fire_anchors"] = []
            for spec in fire_anchor_specs:
                if int(spec["view"]) != i:
                    continue
                x, y, z = spec["block"]
                # Supported-from-below fire stays within the cell and reaches 22.4/16.
                block_roi = project_aabb_mask(
                    cam, (x, y, z, x + 1.0, y + 1.4, z + 1.0),
                )
                fire_roi |= block_roi
                score = binary_overlap(oracle, candidate, block_roi & analysis)
                score["no_fire_ablation"] = binary_overlap(
                    oracle, no_fire_candidate, block_roi & analysis,
                )
                score["block"] = [x, y, z]
                record["fire_anchors"].append(score)
            record["fire_anchor"] = binary_overlap(
                oracle, candidate, fire_roi & analysis,
            )
        if prefix == "nether":
            def hot(a):
                return ((a[:, :, 0] >= 140) & (a[:, :, 1] >= 40)
                        & (a[:, :, 0] >= 1.2 * a[:, :, 1])
                        & (a[:, :, 1] >= 1.05 * a[:, :, 2]) & analysis)
            oracle_hot = hot(oracle)
            candidate_hot = hot(candidate)
            union = int((oracle_hot | candidate_hot).sum())
            record["hot_emitter"] = {
                "oracle_pixels": int(oracle_hot.sum()),
                "candidate_pixels": int(candidate_hot.sum()),
                "intersection_pixels": int((oracle_hot & candidate_hot).sum()),
                "false_positive_pixels": int((candidate_hot & ~oracle_hot).sum()),
                "false_negative_pixels": int((oracle_hot & ~candidate_hot).sum()),
                "iou": float((oracle_hot & candidate_hot).sum() / union) if union else 1.0,
            }
        per_view.append(record)
        print(f"  {prefix} look {i}: MAE={world['mae']:.3f} Gcorr={world['gcorr']:.3f}")

    maes = [v["world"]["mae"] for v in per_view]
    corrs = [v["world"]["gcorr"] for v in per_view]
    background_maes = [v["background"]["mae"] for v in per_view if v["background"]]
    coverage_ok = (
        all(v["fallback_nonair"] == 0 for v in per_view)
        and all(v["unsupported_nonzero_meta"] == 0 for v in per_view)
        and all(v["supported_nonzero_meta"] == source_nonzero_meta for v in per_view)
    )
    finite = all(math.isfinite(v) for v in maes + corrs)

    if prefix == "nether":
        thresholds = {
            "max_per_view_mae": 10.0,
            "min_per_view_coarse_gcorr": 0.20,
            "min_oracle_edge_energy_for_corr": 0.10,
            "min_informative_views": 7,
            "max_visible_nonfog_symmetric_difference_fraction": 0.08,
            "max_background_mae": 0.60,
            "max_hot_false_positive_pixels": 1,
            "min_fire_anchor_iou": 0.65,
            "min_fire_anchor_recall": 0.85,
            "min_fire_anchor_precision": 0.68,
        }
        fire_scores = [score for v in per_view for score in v.get("fire_anchors", [])]
        informative_views = [
            v for v in per_view
            if v["world"]["oracle_edge_energy"]
                >= thresholds["min_oracle_edge_energy_for_corr"]
        ]
        passed = (
            finite and coverage_ok and pinned
            and max(maes) <= thresholds["max_per_view_mae"]
            and len(informative_views) >= thresholds["min_informative_views"]
            and min(v["world"]["coarse_gcorr"] for v in informative_views)
                >= thresholds["min_per_view_coarse_gcorr"]
            and max(v["visible_nonfog_silhouette"]
                    ["symmetric_difference_fraction"] for v in per_view)
                <= thresholds["max_visible_nonfog_symmetric_difference_fraction"]
            and max(background_maes) <= thresholds["max_background_mae"]
            and max(v["hot_emitter"]["false_positive_pixels"] for v in per_view)
                <= thresholds["max_hot_false_positive_pixels"]
            and len(fire_scores) == len(fire_anchor_specs)
            and all(v["roi_pixels"] > 0 and v["oracle_pixels"] > 0
                    and v["candidate_pixels"] > 0 for v in fire_scores)
            and min(v["iou"] for v in fire_scores) >= thresholds["min_fire_anchor_iou"]
            and min(v["recall"] for v in fire_scores)
                >= thresholds["min_fire_anchor_recall"]
            and min(v["precision"] for v in fire_scores)
                >= thresholds["min_fire_anchor_precision"]
            and all(v["no_fire_ablation"]["iou"]
                    < thresholds["min_fire_anchor_iou"] for v in fire_scores)
            and all(v["flat_clear_ablation"]["coarse_gcorr"]
                    < thresholds["min_per_view_coarse_gcorr"]
                    for v in informative_views)
        )
        anchor = None
    else:
        # View 6 top 200 is unobstructed End sky in the deterministic seed-0 pose.
        oracle = read_rgb(OUT / "oracle_end_look_06.png")
        candidate = read_rgb(OUT / "c_end_look_06.png")
        sky_mask = np.zeros((H, W), dtype=bool)
        sky_mask[:200, :] = True
        anchor = image_metrics(oracle, candidate, sky_mask)
        anchor_terrain = np.asarray(
            Image.open(OUT / "c_end_look_06_terrain.pgm").convert("L")
        ) > 0
        anchor["terrain_pixels"] = int((anchor_terrain & sky_mask).sum())
        flat = np.empty_like(candidate)
        flat[:, :, :] = np.asarray(views[6]["camera"]["fog_rgb8"], np.float32)
        anchor["flat_clear_ablation"] = image_metrics(oracle, flat, sky_mask)
        thresholds = {
            "max_per_view_mae": 5.5,
            "min_mean_gcorr": 0.70,
            "max_per_view_nonbackground_mae": 5.0,
            "min_nonbackground_views": 6,
            "min_per_view_nonbackground_silhouette_iou": 0.90,
            "min_per_view_nonbackground_edge_ratio": 0.15,
            "max_sky_anchor_mae": 0.01,
            "min_sky_anchor_exact_fraction": 0.999,
        }
        nonbackground_views = [
            v for v in per_view if v["oracle_nonbackground_fraction"] >= 0.01
        ]
        passed = (
            finite and coverage_ok and pinned
            and max(maes) <= thresholds["max_per_view_mae"]
            and float(np.mean(corrs)) >= thresholds["min_mean_gcorr"]
            and len(nonbackground_views) >= thresholds["min_nonbackground_views"]
            and max(v["oracle_nonbackground"]["mae"]
                    for v in nonbackground_views)
                <= thresholds["max_per_view_nonbackground_mae"]
            and min(v["nonbackground_silhouette"]["iou"]
                    for v in nonbackground_views)
                >= thresholds["min_per_view_nonbackground_silhouette_iou"]
            and min(v["oracle_nonbackground"]["edge_energy_ratio"]
                    for v in nonbackground_views)
                >= thresholds["min_per_view_nonbackground_edge_ratio"]
            and all(v["missing_nonbackground_ablation"]["mae"]
                    > thresholds["max_per_view_nonbackground_mae"]
                    for v in nonbackground_views)
            and anchor["mae"] <= thresholds["max_sky_anchor_mae"]
            and anchor["terrain_pixels"] == 0
            and anchor["exact_fraction"] >= thresholds["min_sky_anchor_exact_fraction"]
            and anchor["flat_clear_ablation"]["mae"]
                > thresholds["max_sky_anchor_mae"]
            and anchor["flat_clear_ablation"]["exact_fraction"]
                < thresholds["min_sky_anchor_exact_fraction"]
        )

    return {
        "dimension": dimension,
        "captured_fog": [
            {
                "i": view["i"],
                "rgb8": view["camera"]["fog_rgb8"],
                "start": view["camera"]["fog_start"],
                "end": view["camera"]["fog_end"],
                "boss": view["camera"]["boss_fog"],
            }
            for view in views
        ],
        "n_views": len(per_view),
        "per_view": per_view,
        "mean_mae": float(np.mean(maes)),
        "max_mae": max(maes),
        "mean_gcorr": float(np.mean(corrs)),
        "min_gcorr": min(corrs),
        "source_nonzero_meta": source_nonzero_meta,
        "texture_animations_pinned": pinned,
        "coverage_pass": coverage_ok,
        "sky_anchor": anchor,
        "thresholds": thresholds,
        "pass": bool(passed),
    }


def validate_mechanic_probes(steps: dict) -> None:
    """Re-derive hashed block-probe facts instead of trusting summary fields."""
    portal = np.fromfile(OUT / "portal_frame.bin", dtype="<u2")
    if portal.size != 20:
        raise RuntimeError(f"portal frame probe has {portal.size} cells, expected 20")
    portal_blocks = int(((portal >> 4) == 90).sum())
    portal_step = steps.get("portal_light", {})
    if (portal_step.get("portal_blocks") != portal_blocks
            or portal_step.get("pass") is not (portal_blocks >= 6)):
        raise RuntimeError(
            f"portal mechanic summary disagrees with probe: {portal_step}, "
            f"probe_blocks={portal_blocks}"
        )

    end = np.fromfile(OUT / "end_portal_probe.bin", dtype="<u2")
    if end.size != 25:
        raise RuntimeError(f"End portal probe has {end.size} cells, expected 25")
    end_ids = end >> 4
    end_meta = end & 15
    frame_n = int((end_ids == 120).sum())
    portal_n = int((end_ids == 119).sum())
    center_exact = bool(np.all(end_ids.reshape(5, 5)[1:4, 1:4] == 119))
    eye_meta = sorted(end_meta[end_ids == 120].tolist())
    want_meta = sorted([4] * 3 + [5] * 3 + [6] * 3 + [7] * 3)
    end_step = steps.get("end_portal_build", {})
    probe_pass = (
        frame_n == 12 and portal_n == 9 and center_exact and eye_meta == want_meta
    )
    activation = end_step.get("activation", {})
    summary_pass = (
        probe_pass and activation.get("ok") is True
        and activation.get("result") == "SUCCESS"
        and activation.get("before_eye") is False
        and activation.get("after_eye") is True
    )
    if (end_step.get("frames") != frame_n
            or end_step.get("portal_blocks") != portal_n
            or end_step.get("portal_center_exact") is not center_exact
            or end_step.get("eye_meta") != eye_meta
            or end_step.get("pass") is not summary_pass):
        raise RuntimeError(
            f"End mechanic summary disagrees with probe: {end_step}, "
            f"probe={{'frames': {frame_n}, 'portal_blocks': {portal_n}, "
            f"'center': {center_exact}, 'eye_meta': {eye_meta}}}"
        )


def score_artifacts(results: dict, run_id: str) -> dict:
    # run_id remains the score invocation ID for compatibility. The immutable
    # live capture identity is capture_run_id and is never rewritten on rescore.
    results["run_id"] = run_id
    results["score_run_id"] = run_id
    results["scored_at_ns"] = time.time_ns()
    results["ok"] = False
    results["score_status"] = "running"
    results.pop("score_error", None)
    results.pop("pixel", None)
    write_results(results)
    try:
        validate_capture_manifest(results.get("capture_manifest", {}))
        capture_metadata = json.loads((OUT / "capture_metadata.json").read_text())
        if capture_metadata.get("capture_run_id") != results.get("capture_run_id"):
            raise RuntimeError("capture identity disagrees with hashed capture metadata")
        if not isinstance(capture_metadata.get("steps"), dict):
            raise RuntimeError("hashed capture metadata lacks live mechanic evidence")
        # On rescore, discard any mutable copy from results.json and restore the
        # mechanics evidence that was hashed with the live capture.
        results["steps"] = capture_metadata["steps"]
        validate_mechanic_probes(results["steps"])
        dumps = capture_metadata.get("dumps", {})
        for name, dimension in (("nether", -1), ("end", 1)):
            dump = dumps.get(name, {})
            if dump.get("dimension") != dimension:
                raise RuntimeError(f"hashed {name} dump has wrong dimension: {dump}")
            expected_path = (OUT / f"{name}_look.mcbd").resolve()
            if Path(dump.get("file", "")).resolve() != expected_path:
                raise RuntimeError(f"hashed {name} dump points at the wrong file: {dump}")
        print("[e2e] building dim_mesh_candidate...")
        binary = build_dim_mesh()
        results["pixel"] = {}
        results["pixel"]["nether"] = compare_dimension_views(
            binary, "nether", -1, dumps["nether"],
        )
        write_results(results)
        results["pixel"]["end"] = compare_dimension_views(
            binary, "end", 1, dumps["end"],
        )
        required_steps = (
            "portal_light", "portal_enter", "nether_scene", "nether_look",
            "end_portal_build", "end_enter", "end_scene", "end_look",
        )
        results["ok"] = (
            all(results["steps"].get(k, {}).get("pass") is True
                for k in required_steps)
            and all(v["pass"] for v in results["pixel"].values())
        )
        results["score_status"] = "complete"
        results.pop("score_error", None)
    except Exception as exc:
        results["score_status"] = "failed"
        results["score_error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        write_results(results)
    return results


def main() -> int:
    global ACTIVE_RESULTS
    if OUT.exists() and any(OUT.iterdir()):
        raise SystemExit(
            f"fresh capture requires a unique empty output directory: {OUT}"
        )
    OUT.mkdir(parents=True, exist_ok=True)
    run_id = os.environ.get("PORTAL_E2E_RUN_ID", str(time.time_ns()))
    results = {
        "ok": False, "run_id": run_id, "capture_run_id": run_id,
        "capture_status": "running", "score_status": "pending", "steps": {},
    }
    ACTIVE_RESULTS = results
    write_results(results)

    e = qrl_client.NetheriteEnv()
    if hasattr(e, "s"):
        e.s.settimeout(300)
    o = e.obs()
    print("[e2e] attach", {k: o.get(k) for k in ("dim", "x", "y", "z", "ok")})

    # ---- ensure overworld ----
    if o.get("dim") != 0:
        o = request_dim(e, 0)
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        "gamemode creative @a",
        "gamerule doFireTick true",
        "gamerule sendCommandFeedback false",
        "gamerule doMobSpawning false",
        "time set 6000",
        "weather clear 1000000",
    ]}})

    # ========== 1) NETHER PORTAL LIGHT ==========
    OX, OY, OZ = 200, 70, 200
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        f"tp @a {OX + 1.5} {OY + 2} {OZ - 3} 0 10",
        f"fill {OX - 4} {OY - 1} {OZ - 4} {OX + 6} {OY + 8} {OZ + 4} air",
        f"fill {OX - 4} {OY - 1} {OZ - 4} {OX + 6} {OY - 1} {OZ + 4} stone",
    ]}})
    for _ in range(15):
        e.step({})

    blocks = []
    for x in range(OX, OX + 4):
        blocks.append([x, OY, OZ, 49, 0])
        blocks.append([x, OY + 4, OZ, 49, 0])
    for y in range(OY + 1, OY + 4):
        blocks.append([OX, y, OZ, 49, 0])
        blocks.append([OX + 3, y, OZ, 49, 0])
    for y in range(OY + 1, OY + 4):
        for x in range(OX + 1, OX + 3):
            blocks.append([x, y, OZ, 0, 0])
    e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    for _ in range(5):
        e.step({})
    # pre-light capture
    settle(e, OX + 1.5, OY + 1.5, OZ - 4, 0, 5, ticks=30, expected_dim=0)
    cam = e._cmd({"cmd": "camera", "action": {}})
    (OUT / "portal_pre_camera.json").write_text(json.dumps(cam, indent=2))
    grab(OUT / "oracle_portal_pre.png")

    # light
    e._cmd({"cmd": "setblocks", "action": {"blocks": [[OX + 1, OY + 1, OZ, 51, 0]]}})
    for _ in range(25):
        e.step({})
        time.sleep(0.03)
    portal_probe = e._cmd({"cmd": "getblocks", "action": {
        "x0": OX, "y0": OY, "z0": OZ, "x1": OX + 3, "y1": OY + 4, "z1": OZ,
        "file": str(OUT / "portal_frame.bin"),
    }})
    checked_file_response(portal_probe, OUT / "portal_frame.bin", 4 * 5 * 2)
    ids = np.fromfile(OUT / "portal_frame.bin", dtype="<u2") >> 4
    portal_n = int((ids == 90).sum())
    print(f"[e2e] portal blocks after light: {portal_n}")
    results["steps"]["portal_light"] = {
        "portal_blocks": portal_n,
        "pass": portal_n >= 6,
    }
    if portal_n < 6:
        results["ok"] = False
    checkpoint_capture(results)

    settle(e, OX + 1.5, OY + 1.5, OZ - 4, 0, 5, ticks=40, expected_dim=0)
    cam = e._cmd({"cmd": "camera", "action": {}})
    (OUT / "portal_lit_camera.json").write_text(json.dumps(cam, indent=2))
    grab(OUT / "oracle_portal_lit.png")

    # Dump around portal for provenance. Portal visuals are not scored here.
    db = e._cmd({"cmd": "dumpblocks", "action": {
        "radius": 3, "file": str(OUT / "ow_portal.mcbd"),
    }})
    checked_dump_response(db, OUT / "ow_portal.mcbd")
    print("[e2e] dump ow portal", db)

    # ========== 2) ENTER NETHER ==========
    # Creative maxInPortalTime=1. Pulse setPortal each tick via portal_touch
    # (headless collision does not reliably set inPortal). No force-dim.
    e._cmd({"cmd": "runcmds", "action": {"cmds": ["gamemode creative @a"]}})
    portal_pose = e._cmd({"cmd": "set_pose", "action": {
        "x": OX + 1.5, "y": OY + 1.0, "z": OZ + 0.5,
        "yaw": 0.0, "pitch": 0.0,
    }})
    if portal_pose.get("ok") is not True:
        raise RuntimeError(f"could not place player in Nether portal: {portal_pose}")
    entered = False
    forced_enter = False
    entry_method = None
    entry_assisted = False
    entry_evidence = None
    first_step = e.step({})
    if first_step.get("dim") == -1:
        o = wait_dim(e, -1)
        entered = True
        entry_method = "automatic_block_collision"
        print("[e2e] entered nether by automatic BlockPortal collision")
    for i in range(120 if not entered else 0):
        touch = e._cmd({"cmd": "portal_touch", "action": {}})
        touch_valid = (
            touch.get("ok") is True and touch.get("portal_touch") is True
            and touch.get("kind") == "nether_collision_handler"
            and touch.get("intersects") is True and touch.get("pre_dim") == 0
        )
        stepped = e.step({})
        if stepped.get("dim") == -1:
            o = wait_dim(e, -1)
            print("[e2e] entered nether NATURAL at step", i, touch,
                  o.get("x"), o.get("y"), o.get("z"))
            entered = True
            # A rejected helper cannot have caused the transfer; the already
            # armed automatic BlockPortal collision completed on this tick.
            entry_method = touch.get("kind") if touch_valid \
                else "automatic_block_collision"
            entry_assisted = touch_valid and touch.get("assisted") is True
            entry_evidence = touch
            break
        time.sleep(0.05)
        o = e.obs()
        if o.get("dim") == -1:
            o = wait_dim(e, -1)
            print("[e2e] entered nether NATURAL at step", i, touch, o.get("x"), o.get("y"), o.get("z"))
            entered = True
            entry_method = touch.get("kind") if touch_valid \
                else "automatic_block_collision"
            entry_assisted = touch_valid and touch.get("assisted") is True
            entry_evidence = touch
            break
        if not touch_valid:
            raise RuntimeError(f"Nether collision helper rejected contact: {touch}")
    if not entered:
        print("[e2e] portal_touch enter failed; force dim -1 (FAIL path)")
        o = request_dim(e, -1)
        forced_enter = True
        entry_method = "forced_dimension_command"
        entered = o.get("dim") == -1
    o = e.obs()
    entry_ok = (
        o.get("dim") == -1
        and not forced_enter
        and entry_method in {"automatic_block_collision", "nether_collision_handler"}
        and entry_assisted is (entry_method == "nether_collision_handler")
    )
    results["steps"]["portal_enter"] = {
        "dim": o.get("dim"),
        "forced": forced_enter,
        "method": entry_method,
        "assisted": entry_assisted,
        "evidence": entry_evidence,
        "pass": entry_ok,
        "pos": [o.get("x"), o.get("y"), o.get("z")],
    }
    if not entry_ok:
        results["ok"] = False
    checkpoint_capture(results)
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        "gamemode creative @a",
        "gamerule doFireTick false",
        "gamerule doMobSpawning false",
    ]}})

    # The visual oracle is terrain-only. Remove mobs that may have spawned
    # during the first Nether ticks and prove they stay absent before capture.
    nether_cleared = e._cmd({"cmd": "killentities", "action": {}})
    nether_scene_obs = None
    for _ in range(20):
        nether_scene_obs = e.step({})
        if (nether_scene_obs.get("dim") == -1
                and nether_scene_obs.get("entity_count") == 0):
            break
    nether_scene_ok = (
        nether_cleared.get("ok") is True
        and nether_scene_obs is not None
        and nether_scene_obs.get("dim") == -1
        and nether_scene_obs.get("entity_count") == 0
    )
    results["steps"]["nether_scene"] = {
        "killed_nonplayers": nether_cleared.get("killed"),
        "remaining_nonplayers": (
            nether_scene_obs.get("entity_count") if nether_scene_obs else None
        ),
        "pass": nether_scene_ok,
    }
    if not nether_scene_ok:
        results["ok"] = False
    checkpoint_capture(results)

    # spiral load then climb ~7 blocks
    nx, ny, nz = float(o["x"]), float(o["y"]), float(o["z"])
    # find solid feet y
    o = drop_to_ground(e, nx, 80, nz, expected_dim=-1)
    nx, ny, nz = float(o["x"]), float(o["y"]), float(o["z"])
    # tower of netherrack under player for stable look platform
    tower_x = math.floor(nx)
    tower_y = math.floor(ny)
    tower_z = math.floor(nz)
    blocks = []
    for dy in range(0, 7):
        blocks.append([tower_x, tower_y + dy, tower_z, 87, 0])  # netherrack pillar
    e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    for _ in range(10):
        e.step({})
    look_y = tower_y + 7.0  # stand on top of the seven-block stack (feet)
    # Two isolated, live-Minecraft fire anchors at eye level. The earlier world
    # fires sit below the fixed camera and disappear under the HUD mask. These
    # face opposite directions, so each has its own non-overlapping view gate.
    fire_y = int(look_y)
    cavity = e._cmd({"cmd": "runcmds", "action": {"cmds": [
        f"fill {tower_x - 13} {fire_y} {tower_z - 1} "
        f"{tower_x + 1} {fire_y + 2} {tower_z + 9} air",
    ]}})
    if cavity.get("ok") is not True:
        raise RuntimeError(f"failed to clear controlled Nether capture gallery: {cavity}")
    fire_positions = [
        [tower_x - 9, fire_y, tower_z + 8],
        [tower_x - 12, fire_y, tower_z + 1],
    ]
    fire_anchor_specs = [
        {"block": block, "view": view}
        for view in (1, 2) for block in fire_positions
    ]
    fire_blocks = []
    for x, y, z in fire_positions:
        fire_blocks.extend(([x, y - 1, z, 87, 0], [x, y, z, 51, 0]))
    placed_fire = e._cmd({"cmd": "setblocks", "action": {"blocks": fire_blocks}})
    if placed_fire.get("ok") is not True:
        raise RuntimeError(f"failed to place controlled Nether fires: {placed_fire}")
    for _ in range(5):
        e.step({})
    settle(e, tower_x + 0.5, look_y, tower_z + 0.5, 0.0, 0.0,
           ticks=20, expected_dim=-1)
    reloaded = e._cmd({"cmd": "reload_renderers", "action": {}})
    if reloaded.get("ok") is not True:
        raise RuntimeError(f"failed to reload Nether renderers: {reloaded}")
    wait_render_ready(e, -1)
    db = e._cmd({"cmd": "dumpblocks", "action": {
        "radius": 9, "file": str(OUT / "nether_look.mcbd"),
    }})
    checked_dump_response(db, OUT / "nether_look.mcbd")
    db["fire_anchors"] = fire_anchor_specs
    # 360 look at pitch 0 — 8 yaw samples
    nether_views = []
    for i, yaw in enumerate(range(0, 360, 45)):
        settle(e, tower_x + 0.5, look_y, tower_z + 0.5, float(yaw), 0.0,
               ticks=35, expected_dim=-1)
        cam = e._cmd({"cmd": "camera", "action": {}})
        (OUT / f"nether_look_{i:02d}_camera.json").write_text(json.dumps(cam, indent=2))
        grab(OUT / f"oracle_nether_look_{i:02d}.png")
        nether_views.append({"i": i, "yaw": yaw, "camera": cam})
        print(f"[e2e] nether look {i} yaw={yaw}")
    verify_dump_stable(e, OUT / "nether_look.mcbd", db, "nether")
    print("[e2e] nether dump", db)
    (OUT / "nether_look_views.json").write_text(json.dumps(nether_views, indent=2, default=str))
    results["steps"]["nether_look"] = {
        "n_views": len(nether_views),
        "pass": len(nether_views) == 8,
        "dump": db,
    }
    checkpoint_capture(results)

    # ========== 3) END PORTAL WITH EYES ==========
    o = request_dim(e, 0)
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        "gamemode creative @a",
        "gamerule sendCommandFeedback false",
    ]}})
    # Build classic 12-frame ring at 250,70,250
    EX, EY, EZ = 250, 70, 250
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        f"tp @a {EX} {EY + 3} {EZ - 6} 0 20",
        f"fill {EX - 6} {EY - 1} {EZ - 6} {EX + 6} {EY + 4} {EZ + 6} air",
        f"fill {EX - 6} {EY - 1} {EZ - 6} {EX + 6} {EY - 1} {EZ + 6} stone",
    ]}})
    for _ in range(15):
        e.step({})

    # Exact inward-facing 5x5 ring. Bits 0-1 are facing and bit 2 is EYE:
    # SOUTH=0, WEST=1, NORTH=2, EAST=3, EYE=4. Leave north-center empty
    # and activate it through PlayerInteractionManager -> ItemEnderEye.onItemUse.
    cells = [
        (-1, -2, 0), (0, -2, 0), (1, -2, 0),  # north
        (2, -1, 1), (2, 0, 1), (2, 1, 1),      # east
        (1, 2, 2), (0, 2, 2), (-1, 2, 2),      # south
        (-2, 1, 3), (-2, 0, 3), (-2, -1, 3),  # west
    ]
    click = (0, -2)
    frames = [
        [EX + dx, EY, EZ + dz, 120, face | (0 if (dx, dz) == click else 4)]
        for dx, dz, face in cells
    ]
    e._cmd({"cmd": "setblocks", "action": {"blocks": frames}})
    for _ in range(5):
        e.step({})
        time.sleep(0.05)
    activation = e._cmd({
        "cmd": "use_end_eye",
        "action": {"x": EX, "y": EY, "z": EZ - 2},
    })
    for _ in range(10):
        e.step({})
        time.sleep(0.05)

    # Exact postcondition of the vanilla completed-ring pattern.
    end_probe = e._cmd({"cmd": "getblocks", "action": {
        "x0": EX - 2, "y0": EY, "z0": EZ - 2, "x1": EX + 2, "y1": EY, "z1": EZ + 2,
        "file": str(OUT / "end_portal_probe.bin"),
    }})
    checked_file_response(end_probe, OUT / "end_portal_probe.bin", 5 * 5 * 2)
    packed = np.fromfile(OUT / "end_portal_probe.bin", dtype="<u2")
    eids = packed >> 4
    emeta = packed & 15
    end_portal_n = int((eids == 119).sum())
    frame_n = int((eids == 120).sum())
    id_grid = eids.reshape(5, 5)
    portal_center_exact = bool(np.all(id_grid[1:4, 1:4] == 119))
    eye_meta = sorted(emeta[eids == 120].tolist())
    want_meta = sorted([4] * 3 + [5] * 3 + [6] * 3 + [7] * 3)
    build_ok = (
        activation.get("ok") is True
        and activation.get("result") == "SUCCESS"
        and activation.get("before_eye") is False
        and activation.get("after_eye") is True
        and frame_n == 12
        and end_portal_n == 9
        and portal_center_exact
        and eye_meta == want_meta
    )
    print(f"[e2e] end activation={activation} frames={frame_n} portal_blocks={end_portal_n}")

    # Keep the 0.6-wide player fully inside the cleared arena. At EZ-6 exactly,
    # its bounding box overlaps untouched terrain outside the fill boundary.
    settle(e, EX + 0.5, EY + 2, EZ - 4.5, 0, 25,
           ticks=40, expected_dim=0)
    cam = e._cmd({"cmd": "camera", "action": {}})
    (OUT / "end_portal_camera.json").write_text(json.dumps(cam, indent=2))
    grab(OUT / "oracle_end_portal.png")
    results["steps"]["end_portal_build"] = {
        "frames": frame_n, "portal_blocks": end_portal_n,
        "portal_center_exact": portal_center_exact,
        "activation": activation, "eye_meta": eye_meta,
        "pass": build_ok,
    }
    if not results["steps"]["end_portal_build"]["pass"]:
        results["ok"] = False
    checkpoint_capture(results)

    # enter end: stand in END_PORTAL blocks; portal_touch triggers changeDimension(1)
    e._cmd({"cmd": "runcmds", "action": {"cmds": [
        "gamemode creative @a",
        f"tp @a {EX + 0.5} {EY + 0.1} {EZ + 0.5} 0 0",
    ]}})
    for _ in range(3):
        e.step({})
    o = e.obs()
    entered_end = o.get("dim") == 1
    forced_end = False
    end_entry_method = "automatic_block_collision" if entered_end else None
    end_entry_assisted = False
    end_entry_evidence = None
    if entered_end:
        print("[e2e] entered end by vanilla block collision")
    for i in range(40 if not entered_end else 0):
        touch = e._cmd({"cmd": "portal_touch", "action": {}})
        touch_valid = (
            touch.get("ok") is True and touch.get("portal_touch") is True
            and touch.get("kind") == "end_collision_handler"
            and touch.get("assisted") is True
            and touch.get("intersects") is True and touch.get("pre_dim") == 0
        )
        stepped = e.step({})
        time.sleep(0.05)
        o = e.obs()
        if stepped.get("dim") == 1 or o.get("dim") == 1:
            print("[e2e] entered end via BlockEndPortal collision helper", i, touch)
            entered_end = True
            end_entry_method = touch.get("kind") if touch_valid \
                else "automatic_block_collision"
            end_entry_assisted = touch_valid
            end_entry_evidence = touch
            break
        if not touch_valid:
            raise RuntimeError(f"End collision helper rejected contact: {touch}")
    if not entered_end:
        print("[e2e] end portal_touch failed; force dim 1 (FAIL path)")
        o = request_dim(e, 1)
        forced_end = True
        end_entry_method = "forced_dimension_command"
        entered_end = o.get("dim") == 1
    o = e.obs()
    end_entry_ok = (
        o.get("dim") == 1
        and not forced_end
        and end_entry_method in {"automatic_block_collision", "end_collision_handler"}
        and end_entry_assisted is (end_entry_method == "end_collision_handler")
    )
    results["steps"]["end_enter"] = {
        "dim": o.get("dim"), "forced": forced_end,
        "method": end_entry_method,
        "assisted": end_entry_assisted,
        "evidence": end_entry_evidence,
        "pass": end_entry_ok,
        "pos": [o.get("x"), o.get("y"), o.get("z")],
    }
    if not end_entry_ok:
        results["ok"] = False
    checkpoint_capture(results)

    e._cmd({"cmd": "runcmds", "action": {"cmds": ["gamemode creative @a"]}})
    cleared = e._cmd({"cmd": "killentities", "action": {}})
    scene_obs = None
    for _ in range(20):
        scene_obs = e.step({})
        if scene_obs.get("dim") == 1 and scene_obs.get("entity_count") == 0:
            break
    end_scene_ok = (
        cleared.get("ok") is True
        and scene_obs is not None
        and scene_obs.get("dim") == 1
        and scene_obs.get("entity_count") == 0
    )
    results["steps"]["end_scene"] = {
        "killed_nonplayers": cleared.get("killed"),
        "remaining_nonplayers": scene_obs.get("entity_count") if scene_obs else None,
        "pass": end_scene_ok,
    }
    if not end_scene_ok:
        results["ok"] = False
    checkpoint_capture(results)
    # End lookaround at island: climb 7 if possible
    ex, ey, ez = float(o["x"]), float(o["y"]), float(o["z"])
    end_x = math.floor(ex) + 0.5
    end_z = math.floor(ez) + 0.5
    ground = drop_to_ground(e, end_x, max(ey + 32.0, 96.0), end_z, expected_dim=1)
    end_x = float(ground["x"])
    end_z = float(ground["z"])
    end_y = float(ground["y"]) + 7.0
    settle(e, end_x, end_y, end_z, 0.0, 0.0, ticks=20, expected_dim=1)
    reloaded = e._cmd({"cmd": "reload_renderers", "action": {}})
    if reloaded.get("ok") is not True:
        raise RuntimeError(f"failed to reload End renderers: {reloaded}")
    wait_render_ready(e, 1)
    db = e._cmd({"cmd": "dumpblocks", "action": {
        "radius": 9, "file": str(OUT / "end_look.mcbd"),
    }})
    checked_dump_response(db, OUT / "end_look.mcbd")
    end_views = []
    for i, yaw in enumerate(range(0, 360, 45)):
        settle(e, end_x, end_y, end_z, float(yaw), 0.0,
               ticks=35, expected_dim=1)
        cam = e._cmd({"cmd": "camera", "action": {}})
        (OUT / f"end_look_{i:02d}_camera.json").write_text(json.dumps(cam, indent=2))
        grab(OUT / f"oracle_end_look_{i:02d}.png")
        end_views.append({"i": i, "yaw": yaw, "camera": cam})
        print(f"[e2e] end look {i} yaw={yaw}")
    verify_dump_stable(e, OUT / "end_look.mcbd", db, "end")
    print("[e2e] end dump", db)
    (OUT / "end_look_views.json").write_text(json.dumps(end_views, indent=2, default=str))
    results["steps"]["end_look"] = {
        "n_views": len(end_views), "pass": len(end_views) == 8, "dump": db,
    }
    checkpoint_capture(results)

    # return OW
    request_dim(e, 0)

    # ========== fresh C renders + feature-level metrics ==========
    capture_metadata = {
        "schema": "portal_e2e.capture.v1",
        "capture_run_id": run_id,
        "steps": results["steps"],
        "dumps": {
            "nether": {"dimension": -1, **results["steps"]["nether_look"]["dump"]},
            "end": {"dimension": 1, **results["steps"]["end_look"]["dump"]},
        },
    }
    (OUT / "capture_metadata.json").write_text(
        json.dumps(capture_metadata, indent=2, default=str)
    )
    results["capture_manifest"] = make_capture_manifest()
    results["capture_status"] = "complete"
    results.pop("capture_error", None)
    write_results(results)
    results = score_artifacts(results, run_id)
    print(json.dumps(results, indent=2, default=str)[:2000])
    print("[e2e] DONE ok=", results["ok"])
    return 0 if results["ok"] else 1


def rescore() -> int:
    result_path = OUT / "results.json"
    if not result_path.is_file():
        raise SystemExit(f"no captured result to rescore: {result_path}")
    results = json.loads(result_path.read_text())
    run_id = os.environ.get("PORTAL_E2E_RUN_ID", str(time.time_ns()))
    score_artifacts(results, run_id)
    print(f"[e2e] RESCORE ok={results['ok']} run_id={run_id}")
    return 0 if results["ok"] else 1


if __name__ == "__main__":
    try:
        code = rescore() if "--rescore" in sys.argv[1:] else main()
    except BaseException as exc:
        if ACTIVE_RESULTS is not None and ACTIVE_RESULTS.get("capture_status") == "running":
            ACTIVE_RESULTS["ok"] = False
            ACTIVE_RESULTS["capture_status"] = "failed"
            ACTIVE_RESULTS["capture_error"] = f"{type(exc).__name__}: {exc}"
            write_results(ACTIVE_RESULTS)
        raise
    sys.exit(code)
