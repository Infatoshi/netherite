#!/usr/bin/env python3
"""fogcurve_probe.py - t(d) fog-curve probe for the slime_bounce horizon band.

Separates the two survivors after H1/H2/H3 (screen-space) were refuted:

  A) Capture GL fog evaluation deviates from t=(d-96)/32 at large d
     → t_gold(d) bends off the ramp IDENTICALLY for ground and vertical faces.
  B) Golden's row-to-distance mapping at grazing incidence differs
     → vertical faces ON the ramp; grazing ground offset by ~Δt 0.18
       (≈ 5.8 blocks on the 32-block ramp).

Sources (prefer exact same-frame camera+fog records):
  - mc_capture seed7 / pose0 goldens + camera_*.json
  - slime_bounce tape goldens + magma fog/nofog replay frames (flat ground)

Usage (from this directory):
  uv run --no-project --with numpy,scipy,pillow python fogcurve_probe.py
  uv run --no-project --with numpy,scipy,pillow python fogcurve_probe.py --scene seed7
  uv run --no-project --with numpy,scipy,pillow python fogcurve_probe.py --scene slime

Scratch defaults under ~/dev/nw/.tmp/fogcurve/ (not /tmp).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
MAGMA = HERE.parents[1] / "magma"
MC_CAPTURE = MAGMA / "raster" / "verify" / "mc_capture"
REPO = HERE.parents[1]  # netherite monorepo root

SCRATCH = Path(os.environ.get("FOGCURVE_SCRATCH", os.path.expanduser("~/dev/nw/.tmp/fogcurve")))
DEFAULT_SLIME_FOG = Path(os.path.expanduser("~/dev/nw/.tmp/hfog_out/magma_frames.npy"))
DEFAULT_SLIME_NOFOG = Path(os.path.expanduser("~/dev/nw/.tmp/hfog_nofog/magma_frames.npy"))
DEFAULT_SLIME_TICKS_FOG = Path(os.path.expanduser("~/dev/nw/.tmp/hfog_out/magma_frames.ticks.npy"))
DEFAULT_SLIME_TICKS_NOFOG = Path(
    os.path.expanduser("~/dev/nw/.tmp/hfog_nofog/magma_frames.ticks.npy")
)
DEFAULT_SLIME_GOLDEN_DIR = (
    REPO / "verify/tapes/scenario_slime_bounce_20260723T001527Z_frames"
)
# fogcurve worktree may only symlink the tape; resolve either home.
_alt_frames = (
    MAGMA
    / "raster"
    / "verify"
    / "tapes"
    / "scenario_slime_bounce_20260723T001527Z_frames"
)


# ---------------------------------------------------------------------------
# camera / ray geometry (magma conventions, matches core/math.c + game/sky.c)
# ---------------------------------------------------------------------------


@dataclass
class Cam:
    eye: np.ndarray  # (3,)
    yaw_rad: float  # magma yaw
    pitch_rad: float  # magma pitch (positive = up)
    fov_deg: float
    w: int
    h: int
    znear: float = 0.05
    zfar: float = 181.01933
    fog_start: float = 96.0
    fog_end: float = 128.0
    fog_rgb8: tuple = (179, 207, 255)

    @property
    def aspect(self) -> float:
        return self.w / float(self.h)

    def basis(self):
        """Return unit F, R, U and tan(half_vFOV)."""
        cy, sy = math.cos(self.yaw_rad), math.sin(self.yaw_rad)
        cp, sp = math.cos(self.pitch_rad), math.sin(self.pitch_rad)
        F = np.array([-sy * cp, sp, -cy * cp], dtype=np.float64)
        R = np.array([cy, 0.0, -sy], dtype=np.float64)
        U = np.array([sy * sp, cp, cy * sp], dtype=np.float64)
        half = math.radians(self.fov_deg) * 0.5
        tanH = math.tan(half)
        return F, R, U, tanH

    def pixel_ray_dirs(self) -> np.ndarray:
        """(H,W,3) unnormalized ray directions in world space (pixel centres)."""
        F, R, U, tanH = self.basis()
        xs = (np.arange(self.w, dtype=np.float64) + 0.5) / self.w
        ys = (np.arange(self.h, dtype=np.float64) + 0.5) / self.h
        ndc_x = 2.0 * xs - 1.0  # (W,)
        ndc_y = 1.0 - 2.0 * ys  # (H,)  y=0 top → ndc_y = +1
        # dir = ndc_x * tanH * aspect * R + ndc_y * tanH * U + F
        # broadcast: (H,W,3)
        dx = ndc_x[None, :, None] * (tanH * self.aspect) * R[None, None, :]
        dy = ndc_y[:, None, None] * tanH * U[None, None, :]
        return dx + dy + F[None, None, :]

    def plane_hit_dist(self, plane_y: float) -> np.ndarray:
        """Radial eye distance to horizontal plane y=plane_y, or nan if miss."""
        dirs = self.pixel_ray_dirs()
        O = self.eye
        dy = dirs[..., 1]
        # O.y + t * dy = plane_y  →  t = (plane_y - O.y) / dy
        with np.errstate(divide="ignore", invalid="ignore"):
            t = (plane_y - O[1]) / dy
        hit = O[None, None, :] + t[..., None] * dirs
        # only forward hits (t>0) with finite t
        d = np.linalg.norm(hit - O[None, None, :], axis=-1)
        bad = ~(np.isfinite(t) & (t > 1e-6) & np.isfinite(d))
        d = d.astype(np.float64)
        d[bad] = np.nan
        return d

    def depth_to_eye_radial(self, depth: np.ndarray) -> np.ndarray:
        """Approximate radial distance from NDC depth buffer.

        Depth is GL-style [0,1] from transform.c. Reconstructs eye-space
        position after the view matrix (incl. 0.05 Z nudge), then takes
        length. This is not bit-identical to fog's world radial, but tracks
        it within a few cm over the fog band - enough for classification and
        a second d axis. Prefer fog-inverted d_magma when available.
        """
        znear, zfar = self.znear, self.zfar
        # ndc_z in [-1,1]
        ndc_z = depth.astype(np.float64) * 2.0 - 1.0
        # From cr_perspective: ndc_z = -m10 - m14/ez  with ez eye-space z (<0)
        m10 = (zfar + znear) / (znear - zfar)
        m14 = (2.0 * zfar * znear) / (znear - zfar)
        # ndc_z = -m10 - m14/ez  →  m14/ez = -m10 - ndc_z  → ez = m14 / (-m10 - ndc_z)
        denom = -m10 - ndc_z
        with np.errstate(divide="ignore", invalid="ignore"):
            ez = m14 / denom  # negative
        f = 1.0 / math.tan(math.radians(self.fov_deg) * 0.5)
        m00 = f / self.aspect
        m11 = f
        xs = (np.arange(self.w, dtype=np.float64) + 0.5) / self.w
        ys = (np.arange(self.h, dtype=np.float64) + 0.5) / self.h
        ndc_x = 2.0 * xs - 1.0
        ndc_y = 1.0 - 2.0 * ys
        # clip.w = -ez; ndc = clip.xy / clip.w → clip.x = ndc_x*(-ez)
        # clip.x = m00 * ex → ex = ndc_x*(-ez)/m00
        ex = (ndc_x[None, :] * (-ez)) / m00
        ey = (ndc_y[:, None] * (-ez)) / m11
        dist = np.sqrt(ex * ex + ey * ey + ez * ez)
        sky = depth >= 0.999999
        dist = dist.astype(np.float64)
        dist[sky] = np.nan
        return dist


def load_json(p: Path) -> dict:
    return json.loads(p.read_text())


def cam_from_seed7(path: Path = MC_CAPTURE / "camera_seed7.json") -> Cam:
    j = load_json(path)
    return Cam(
        eye=np.array([j["eye_x"], j["eye_y"], j["eye_z"]], dtype=np.float64),
        yaw_rad=math.radians(float(j["magma_yaw_deg"])),
        pitch_rad=math.radians(float(j["magma_pitch_deg"])),
        fov_deg=float(j.get("fov_effective", j.get("fov_setting", 70.0))),
        w=int(j.get("display_w", 854)),
        h=int(j.get("display_h", 480)),
        znear=float(j.get("znear", 0.05)),
        zfar=float(j.get("zfar", 181.01933)),
        fog_start=float(j.get("fog_start", 96.0)),
        fog_end=float(j.get("fog_end", 128.0)),
        fog_rgb8=tuple(j.get("fog_rgb8", [179, 206, 255])),
    )


def cam_from_camera_json(path: Path) -> Cam:
    j = load_json(path)
    # magma_yaw may be 360 for "0 looking -Z equivalent"
    my = float(j.get("magma_yaw_deg", 180.0 - float(j.get("yaw", 0.0))))
    if my >= 360.0:
        my -= 360.0
    mp = float(j.get("magma_pitch_deg", -float(j.get("pitch", 0.0))))
    fog8 = j.get("fog_rgb8")
    if fog8 is None and "fog_rgb" in j:
        fog8 = [int(round(c * 255)) for c in j["fog_rgb"]]
    if fog8 is None:
        fog8 = [179, 207, 255]
    return Cam(
        eye=np.array([j["eye_x"], j["eye_y"], j["eye_z"]], dtype=np.float64),
        yaw_rad=math.radians(my),
        pitch_rad=math.radians(mp),
        fov_deg=float(j.get("fov_effective", j.get("fov_setting", 70.0))),
        w=int(j.get("display_w", 854)),
        h=int(j.get("display_h", 480)),
        znear=float(j.get("znear", 0.05)),
        zfar=float(j.get("zfar", 181.01933)),
        fog_start=float(j.get("fog_start", 96.0)),
        fog_end=float(j.get("fog_end", 128.0)),
        fog_rgb8=tuple(fog8),
    )


def cam_slime_t80() -> Cam:
    # feet (0.5,4,0.5), eyeHeight 1.62, MC yaw/pitch 0 → magma yaw 180, pitch 0
    return Cam(
        eye=np.array([0.5, 4.0 + 1.62, 0.5], dtype=np.float64),
        yaw_rad=math.radians(180.0),
        pitch_rad=0.0,
        fov_deg=70.0,
        w=854,
        h=480,
        fog_start=96.0,
        fog_end=128.0,
        fog_rgb8=(179, 207, 255),
    )


# ---------------------------------------------------------------------------
# fog factor recovery
# ---------------------------------------------------------------------------


def fog_factor(P: np.ndarray, T: np.ndarray, F: np.ndarray) -> np.ndarray:
    """t = (P - T) / (F - T) per channel, median over channels with |F-T|>=4.

    P,T: (...,3) uint8 or float. F: (3,) fog colour.
    Returns float t in roughly [0,1]; nan where denom too small.
    """
    P = P.astype(np.float64)
    T = T.astype(np.float64)
    F = np.asarray(F, dtype=np.float64)
    denom = F - T
    num = P - T
    with np.errstate(divide="ignore", invalid="ignore"):
        tc = num / denom
    ok = np.abs(denom) >= 4.0
    # median over channels that are valid
    tc = np.where(ok, tc, np.nan)
    t = np.nanmedian(tc, axis=-1)
    return t


def ramp_t(d: np.ndarray, fog_start: float, fog_end: float) -> np.ndarray:
    denom = fog_end - fog_start
    t = (d - fog_start) / denom
    return np.clip(t, 0.0, 1.0)


def d_from_t(t: np.ndarray, fog_start: float, fog_end: float) -> np.ndarray:
    return fog_start + t * (fog_end - fog_start)


# ---------------------------------------------------------------------------
# build / render helpers
# ---------------------------------------------------------------------------


def ensure_game_candidate(bin_path: Path) -> Path:
    if bin_path.exists() and bin_path.stat().st_mtime > (
        MC_CAPTURE / "game_candidate.c"
    ).stat().st_mtime:
        # still rebuild if any of the core objs are newer - keep simple: always
        # rebuild when FOGCURVE_FORCE_BUILD=1, else use existing if present.
        if os.environ.get("FOGCURVE_FORCE_BUILD") != "1":
            return bin_path
    mcsim = MAGMA.parent / "blaze" / "core"
    flags = [
        "-O2",
        "-ffp-contract=off",
        "-Wall",
        f"-I{MAGMA / 'core'}",
        f"-I{MAGMA}",
        f"-I{mcsim}",
    ]
    units = [
        "world/mesh_mc",
        "world/light",
        "world/populate_mc",
        "assets/blockmodels",
        "renderkernels/rk_31_facebakery_make_quad",
        "game/sky",
        "game/caps",
        "core/math",
        "core/shade",
        "cpu/raster_cpu",
        "transform",
    ]
    objs = []
    for u in units:
        o = SCRATCH / f"{u.replace('/', '_')}.o"
        o.parent.mkdir(parents=True, exist_ok=True)
        src = MAGMA / f"{u}.c"
        subprocess.check_call(
            ["gcc", *flags, "-c", str(src), "-o", str(o)],
            cwd=str(MAGMA),
        )
        objs.append(str(o))
    bin_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            "gcc",
            *flags,
            str(MC_CAPTURE / "game_candidate.c"),
            *objs,
            "-o",
            str(bin_path),
            "-lm",
        ],
        cwd=str(MAGMA),
    )
    return bin_path


def render_game_candidate(
    bin_path: Path,
    cam: Cam,
    seed: int,
    ppm: Path,
    depth: Optional[Path],
    fog: bool,
) -> None:
    env = os.environ.copy()
    env["MAGMA_FOG"] = "1" if fog else "0"
    # game_candidate caches getenv on first call in-process; fresh process each time.
    cmd = [
        str(bin_path),
        "--eye",
        f"{cam.eye[0]}",
        f"{cam.eye[1]}",
        f"{cam.eye[2]}",
        "--yaw",
        f"{math.degrees(cam.yaw_rad)}",
        "--pitch",
        f"{math.degrees(cam.pitch_rad)}",
        "--fov",
        f"{cam.fov_deg}",
        "--w",
        str(cam.w),
        "--h",
        str(cam.h),
        "--seed",
        str(seed),
        "--ppm",
        str(ppm),
    ]
    if depth is not None:
        cmd += ["--depth", str(depth)]
    print("RUN", "MAGMA_FOG=" + env["MAGMA_FOG"], " ".join(cmd), flush=True)
    subprocess.check_call(cmd, env=env, cwd=str(MAGMA))


def ppm_to_rgb(ppm: Path) -> np.ndarray:
    # minimal P6 reader via PIL
    return np.array(Image.open(ppm).convert("RGB"))


def load_depth(path: Path, w: int, h: int) -> np.ndarray:
    raw = np.fromfile(path, dtype=np.float32)
    if raw.size != w * h:
        raise RuntimeError(f"depth size {raw.size} != {w*h}")
    return raw.reshape(h, w)


# ---------------------------------------------------------------------------
# analysis core
# ---------------------------------------------------------------------------


@dataclass
class BinStats:
    d_lo: float
    d_hi: float
    n: int
    t_gold_mean: float
    t_gold_std: float
    t_magma_mean: float
    t_ramp_mean: float
    residual_gold: float  # mean (t_gold - t_ramp)
    residual_magma: float


def bin_td(
    d: np.ndarray,
    t_gold: np.ndarray,
    t_magma: np.ndarray,
    mask: np.ndarray,
    fog_start: float,
    fog_end: float,
    edges: np.ndarray,
) -> list[BinStats]:
    out = []
    for i in range(len(edges) - 1):
        lo, hi = edges[i], edges[i + 1]
        m = mask & np.isfinite(d) & np.isfinite(t_gold) & (d >= lo) & (d < hi)
        n = int(m.sum())
        if n < 8:
            continue
        tg = t_gold[m]
        tm = t_magma[m]
        tr = ramp_t(d[m], fog_start, fog_end)
        out.append(
            BinStats(
                d_lo=float(lo),
                d_hi=float(hi),
                n=n,
                t_gold_mean=float(np.mean(tg)),
                t_gold_std=float(np.std(tg)),
                t_magma_mean=float(np.mean(tm)),
                t_ramp_mean=float(np.mean(tr)),
                residual_gold=float(np.mean(tg - tr)),
                residual_magma=float(np.mean(tm - tr)),
            )
        )
    return out


def fit_d_shift(
    d: np.ndarray,
    t_gold: np.ndarray,
    mask: np.ndarray,
    fog_start: float,
    fog_end: float,
) -> dict:
    """Fit constant distance shift δ such that t_gold ≈ ramp(d - δ).

    Only uses pixels with t_gold in (0.05, 0.95) and d in fog band.
    """
    m = (
        mask
        & np.isfinite(d)
        & np.isfinite(t_gold)
        & (t_gold > 0.05)
        & (t_gold < 0.95)
        & (d > fog_start - 4)
        & (d < fog_end + 4)
    )
    if m.sum() < 50:
        return {"n": int(m.sum()), "delta_blocks": float("nan"), "rmse": float("nan")}
    dd = d[m]
    tg = t_gold[m]
    # ideal d_gold = fog_start + tg*(fog_end-fog_start); δ = d - d_gold
    d_implied = fog_start + tg * (fog_end - fog_start)
    delta = dd - d_implied  # positive → magma/analytic d farther than gold's t implies
    return {
        "n": int(m.sum()),
        "delta_blocks": float(np.median(delta)),
        "delta_mean": float(np.mean(delta)),
        "delta_std": float(np.std(delta)),
        "rmse_t": float(
            np.sqrt(np.mean((tg - ramp_t(dd - np.median(delta), fog_start, fog_end)) ** 2))
        ),
        "rmse_t_noshift": float(
            np.sqrt(np.mean((tg - ramp_t(dd, fog_start, fog_end)) ** 2))
        ),
    }


def print_bins(title: str, bins: list[BinStats]) -> None:
    print(f"\n=== {title} ===")
    print(
        f"{'d_lo':>6} {'d_hi':>6} {'n':>7} {'t_gold':>8} {'±':>6} "
        f"{'t_mag':>8} {'t_ramp':>8} {'g-ramp':>8} {'m-ramp':>8}"
    )
    for b in bins:
        print(
            f"{b.d_lo:6.1f} {b.d_hi:6.1f} {b.n:7d} "
            f"{b.t_gold_mean:8.3f} {b.t_gold_std:6.3f} "
            f"{b.t_magma_mean:8.3f} {b.t_ramp_mean:8.3f} "
            f"{b.residual_gold:8.3f} {b.residual_magma:8.3f}"
        )


def classify_ground_vs_vertical(
    d_geom: np.ndarray,
    d_plane: Optional[np.ndarray],
    depth: Optional[np.ndarray],
    terrain_mask: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """Return (ground_mask, vertical_mask) within terrain_mask.

    Prefer plane comparison when d_plane is available (flat worlds).
    Else use depth-gradient normal: |n_y| high → ground top; low → vertical.
    """
    if d_plane is not None:
        # hit before plane by >2 blocks → vertical/object; near plane → ground
        ratio_ok = np.isfinite(d_geom) & np.isfinite(d_plane)
        ground = terrain_mask & ratio_ok & (np.abs(d_geom - d_plane) < 2.0)
        vertical = terrain_mask & ratio_ok & (d_geom < d_plane - 3.0)
        return ground, vertical

    if depth is None:
        # no geometry cue: empty vertical, all terrain as "ground-ish"
        return terrain_mask.copy(), np.zeros_like(terrain_mask, dtype=bool)

    # finite-difference normal from depth (eye-space z proxy)
    z = depth.astype(np.float64)
    sky = z >= 0.999999
    z = np.where(sky, np.nan, z)
    dzdx = np.gradient(z, axis=1)
    dzdy = np.gradient(z, axis=0)
    # rough: small |grad| + not sky → facing camera (vertical walls often have
    # smaller screen-y gradient of depth than foreshortened ground)
    gmag = np.hypot(dzdx, dzdy)
    # ground tops at range have LARGE dz/dy (depth races with screen y);
    # vertical faces have SMALLER depth gradient along screen y for a given d.
    # Use relative: high |dzdy| → ground; low |dzdy| with solid depth → vertical.
    med = np.nanmedian(gmag[terrain_mask]) if terrain_mask.any() else 1e-3
    ground = terrain_mask & (gmag >= med)
    vertical = terrain_mask & (gmag < med * 0.35) & np.isfinite(z)
    return ground, vertical


def analyze_pair(
    name: str,
    golden: np.ndarray,
    magma_fog: np.ndarray,
    magma_nofog: np.ndarray,
    cam: Cam,
    d_geom: np.ndarray,
    d_plane: Optional[np.ndarray] = None,
    depth: Optional[np.ndarray] = None,
    exclude_hud: bool = False,
) -> dict:
    H, W, _ = golden.shape
    F = np.array(cam.fog_rgb8, dtype=np.float64)
    # Terrain: not fully fogged-to-F and not sky-like; nofog T differs from F
    T = magma_nofog.astype(np.float64)
    P_g = golden.astype(np.float64)
    P_m = magma_fog.astype(np.float64)

    t_gold = fog_factor(P_g, T, F)
    t_magma = fog_factor(P_m, T, F)

    # mask: both have usable fog channel separation; in/near fog band; not HUD
    denom_ok = np.min(np.abs(F - T), axis=-1) >= 4.0
    # not pure sky: nofog not ≈ F and depth not far (if available)
    not_sky = np.max(np.abs(T - F), axis=-1) >= 8.0
    in_band = (
        np.isfinite(d_geom)
        & (d_geom >= cam.fog_start - 2)
        & (d_geom <= cam.fog_end + 2)
    )
    t_ok = np.isfinite(t_gold) & np.isfinite(t_magma)
    terrain = denom_ok & not_sky & in_band & t_ok
    if exclude_hud:
        # bottom hotbar / hand: drop lower 40 rows and side margins lightly
        terrain[:20, :] = False
        terrain[-50:, :] = False

    ground, vertical = classify_ground_vs_vertical(
        d_geom, d_plane, depth, terrain
    )

    edges = np.arange(cam.fog_start, cam.fog_end + 0.01, 2.0)
    bins_all = bin_td(d_geom, t_gold, t_magma, terrain, cam.fog_start, cam.fog_end, edges)
    bins_g = bin_td(d_geom, t_gold, t_magma, ground, cam.fog_start, cam.fog_end, edges)
    bins_v = bin_td(
        d_geom, t_gold, t_magma, vertical, cam.fog_start, cam.fog_end, edges
    )

    fit_g = fit_d_shift(d_geom, t_gold, ground, cam.fog_start, cam.fog_end)
    fit_v = fit_d_shift(d_geom, t_gold, vertical, cam.fog_start, cam.fog_end)
    fit_a = fit_d_shift(d_geom, t_gold, terrain, cam.fog_start, cam.fog_end)

    # summary residuals over shared d bins that exist for both classes
    def mean_resid(bins: list[BinStats]) -> float:
        if not bins:
            return float("nan")
        # weight by n
        num = sum(b.residual_gold * b.n for b in bins)
        den = sum(b.n for b in bins)
        return num / den if den else float("nan")

    res_g = mean_resid(bins_g)
    res_v = mean_resid(bins_v)

    print(f"\n######## SCENE {name} ########")
    print(
        f"cam eye={cam.eye} yaw_deg={math.degrees(cam.yaw_rad):.2f} "
        f"pitch_deg={math.degrees(cam.pitch_rad):.2f} fov={cam.fov_deg} "
        f"fog={cam.fog_start}/{cam.fog_end} F={cam.fog_rgb8}"
    )
    print(
        f"pixels: terrain={terrain.sum()} ground={ground.sum()} "
        f"vertical={vertical.sum()}"
    )
    print_bins(f"{name} ALL terrain", bins_all)
    print_bins(f"{name} GRAZING GROUND", bins_g)
    print_bins(f"{name} VERTICAL FACES", bins_v)
    print(f"\nfit δ (d_geom - d_implied_by_t_gold), blocks:")
    print(f"  all     : {fit_a}")
    print(f"  ground  : {fit_g}")
    print(f"  vertical: {fit_v}")
    print(
        f"mean (t_gold - t_ramp): ground={res_g:.4f}  vertical={res_v:.4f}  "
        f"diff(g-v)={res_g - res_v if np.isfinite(res_g) and np.isfinite(res_v) else float('nan'):.4f}"
    )

    # verdict local
    # A: both residuals similar and ~ -0.18
    # B: ground residual ~ -0.18, vertical ~ 0
    verdict = "inconclusive"
    if np.isfinite(res_g) and np.isfinite(res_v):
        if abs(res_g - res_v) < 0.05 and abs(res_g + 0.18) < 0.08:
            verdict = "A_fog_evaluation"
        elif abs(res_v) < 0.06 and res_g < -0.10:
            verdict = "B_grazing_distance_mapping"
        elif abs(res_g - res_v) < 0.05:
            verdict = "A_like_shared_curve" if abs(res_g) > 0.05 else "on_ramp"
        elif res_g < res_v - 0.05:
            verdict = "B_like_ground_offset"
        else:
            verdict = "other"
    print(f"LOCAL VERDICT: {verdict}")

    return {
        "name": name,
        "n_terrain": int(terrain.sum()),
        "n_ground": int(ground.sum()),
        "n_vertical": int(vertical.sum()),
        "res_ground": res_g,
        "res_vertical": res_v,
        "fit_ground": fit_g,
        "fit_vertical": fit_v,
        "fit_all": fit_a,
        "bins_ground": [b.__dict__ for b in bins_g],
        "bins_vertical": [b.__dict__ for b in bins_v],
        "bins_all": [b.__dict__ for b in bins_all],
        "verdict": verdict,
        "F": list(cam.fog_rgb8),
        "fog_start": cam.fog_start,
        "fog_end": cam.fog_end,
    }


# ---------------------------------------------------------------------------
# scene runners
# ---------------------------------------------------------------------------


def run_seed7(bin_path: Path, out_dir: Path) -> dict:
    """Seed7: vertical (trunk) vs ground (grass) at same d from depth buffer.

    Absolute t_gold is biased by lighting/T mismatch vs the golden; the
    discriminator is grass_residual - trunk_residual (B predicts -0.18).
    """
    cam = cam_from_seed7()
    golden = np.array(Image.open(MC_CAPTURE / "mc_seed7.png").convert("RGB"))
    ppm_fog = out_dir / "seed7_fog.ppm"
    ppm_nofog = out_dir / "seed7_nofog.ppm"
    depth_path = out_dir / "seed7_depth.f32"
    png_fog = out_dir / "seed7_fog.png"
    png_nofog = out_dir / "seed7_nofog.png"

    need = not (
        png_fog.exists()
        and png_nofog.exists()
        and depth_path.exists()
        and os.environ.get("FOGCURVE_RERENDER") != "1"
    )
    if need:
        render_game_candidate(bin_path, cam, seed=7, ppm=ppm_fog, depth=depth_path, fog=True)
        render_game_candidate(
            bin_path, cam, seed=7, ppm=ppm_nofog, depth=None, fog=False
        )
        Image.open(ppm_fog).convert("RGB").save(png_fog)
        Image.open(ppm_nofog).convert("RGB").save(png_nofog)

    magma_fog = np.array(Image.open(png_fog).convert("RGB")).astype(np.float64)
    magma_nofog = np.array(Image.open(png_nofog).convert("RGB")).astype(np.float64)
    golden_f = golden.astype(np.float64)
    depth = load_depth(depth_path, cam.w, cam.h)
    d_depth = cam.depth_to_eye_radial(depth)
    F = np.array(cam.fog_rgb8, dtype=np.float64)
    T = magma_nofog
    t_gold = fog_factor(golden_f, T, F)
    t_magma = fog_factor(magma_fog, T, F)

    trunk = (
        (T[..., 0] > 60)
        & (T[..., 1] < T[..., 0] + 15)
        & (T[..., 1] < 120)
        & (T[..., 2] < T[..., 0])
    )
    grass = (
        (T[..., 1] > T[..., 0] + 8)
        & (T[..., 1] > T[..., 2])
        & (T[..., 2] < 100)
    )
    band = np.isfinite(d_depth) & (d_depth >= 96) & (d_depth <= 128)
    orth = _orth_to_fog(golden_f, T, F)
    base = (
        band
        & (orth < 10)
        & (np.min(np.abs(F - T), axis=-1) > 8)
        & np.isfinite(t_gold)
        & np.isfinite(t_magma)
    )
    mid = (d_depth >= 104) & (d_depth <= 120)
    m_trunk = base & trunk & mid
    m_grass = base & grass & mid

    def _stats(mask: np.ndarray) -> dict:
        if mask.sum() < 20:
            return {"n": int(mask.sum())}
        tr = ramp_t(d_depth[mask], 96, 128)
        return {
            "n": int(mask.sum()),
            "tg_minus_ramp": float(np.mean(t_gold[mask] - tr)),
            "tm_minus_ramp": float(np.mean(t_magma[mask] - tr)),
            "tg_minus_tm": float(np.mean(t_gold[mask] - t_magma[mask])),
        }

    st_t = _stats(m_trunk)
    st_g = _stats(m_grass)
    diff = (
        st_g.get("tg_minus_ramp", float("nan"))
        - st_t.get("tg_minus_ramp", float("nan"))
    )
    print(f"\n######## SCENE seed7 (material / depth-d) ########")
    print(f"trunk midband: {st_t}")
    print(f"grass midband: {st_g}")
    print(
        f"grass_minus_trunk residual: {diff:+.4f} "
        f"(B predicts -0.18; A predicts ~0)"
    )
    edges = np.arange(96, 128.01, 4.0)
    bins_g = bin_td(d_depth, t_gold, t_magma, base & grass, 96, 128, edges)
    bins_v = bin_td(d_depth, t_gold, t_magma, base & trunk, 96, 128, edges)
    print_bins("seed7 GRASS (ground tops)", bins_g)
    print_bins("seed7 TRUNK (vertical)", bins_v)

    if abs(diff) < 0.06:
        verdict = "A_like_shared_curve"
    elif diff < -0.10:
        verdict = "B_like_ground_offset"
    else:
        verdict = "other"
    print(f"LOCAL VERDICT: {verdict}")
    return {
        "name": "seed7",
        "n_ground": int(m_grass.sum()),
        "n_vertical": int(m_trunk.sum()),
        "res_ground": st_g.get("tg_minus_ramp", float("nan")),
        "res_vertical": st_t.get("tg_minus_ramp", float("nan")),
        "diff_grass_minus_trunk": float(diff) if np.isfinite(diff) else float("nan"),
        "trunk_midband": st_t,
        "grass_midband": st_g,
        "bins_ground": [b.__dict__ for b in bins_g],
        "bins_vertical": [b.__dict__ for b in bins_v],
        "verdict": verdict,
        "F": list(cam.fog_rgb8),
        "fog_start": 96.0,
        "fog_end": 128.0,
        "note": "absolute t_gold biased by T/lighting mismatch; use relative residual",
    }


def run_pose0(bin_path: Path, out_dir: Path) -> dict:
    cam_path = MC_CAPTURE / "camera.json"
    if not cam_path.exists():
        print("skip pose0: no camera.json")
        return {"name": "pose0", "verdict": "skipped"}
    cam = cam_from_camera_json(cam_path)
    # camera.json may lack fog_rgb8 - fill from seed7-like noon if missing
    golden_path = MC_CAPTURE / "mc_frame.png"
    if not golden_path.exists():
        return {"name": "pose0", "verdict": "skipped"}
    golden = np.array(Image.open(golden_path).convert("RGB"))
    # Prefer recorded fog colour from golden sky top if not in camera
    sky_f = golden[0, cam.w // 2].astype(np.float64)
    if cam.fog_rgb8 == (179, 207, 255) and "fog_rgb8" not in load_json(cam_path):
        cam.fog_rgb8 = (int(sky_f[0]), int(sky_f[1]), int(sky_f[2]))

    ppm_fog = out_dir / "pose0_fog.ppm"
    ppm_nofog = out_dir / "pose0_nofog.ppm"
    depth_path = out_dir / "pose0_depth.f32"
    png_fog = out_dir / "pose0_fog.png"
    png_nofog = out_dir / "pose0_nofog.png"
    need = not (
        png_fog.exists()
        and png_nofog.exists()
        and depth_path.exists()
        and os.environ.get("FOGCURVE_RERENDER") != "1"
    )
    if need:
        render_game_candidate(bin_path, cam, seed=0, ppm=ppm_fog, depth=depth_path, fog=True)
        render_game_candidate(
            bin_path, cam, seed=0, ppm=ppm_nofog, depth=None, fog=False
        )
        Image.open(ppm_fog).convert("RGB").save(png_fog)
        Image.open(ppm_nofog).convert("RGB").save(png_nofog)

    magma_fog = np.array(Image.open(png_fog).convert("RGB"))
    magma_nofog = np.array(Image.open(png_nofog).convert("RGB"))
    depth = load_depth(depth_path, cam.w, cam.h)
    F = np.array(cam.fog_rgb8, dtype=np.float64)
    t_m = fog_factor(magma_fog, magma_nofog, F)
    d_fog = d_from_t(t_m, cam.fog_start, cam.fog_end)
    d_depth = cam.depth_to_eye_radial(depth)
    d_geom = np.where(
        np.isfinite(t_m) & (t_m > 0.02) & (t_m < 0.98), d_fog, d_depth
    )
    return analyze_pair(
        "pose0",
        golden,
        magma_fog,
        magma_nofog,
        cam,
        d_geom=d_geom,
        depth=depth,
        exclude_hud=False,
    )


def _orth_to_fog(P: np.ndarray, T: np.ndarray, F: np.ndarray) -> np.ndarray:
    """|| (P-T) - proj_(F-T)(P-T) || per pixel — small when only fog differs from T."""
    FT = F - T
    num = np.sum((P - T) * FT, axis=-1)
    den = np.sum(FT * FT, axis=-1)
    with np.errstate(invalid="ignore", divide="ignore"):
        tproj = num / den
    par = T + FT * tproj[..., None]
    return np.linalg.norm(P - par, axis=-1)


def run_slime(
    out_dir: Path,
    tick: int = 80,
    fog_npy: Path = DEFAULT_SLIME_FOG,
    nofog_npy: Path = DEFAULT_SLIME_NOFOG,
    ticks_fog: Path = DEFAULT_SLIME_TICKS_FOG,
    ticks_nofog: Path = DEFAULT_SLIME_TICKS_NOFOG,
    golden_dir: Optional[Path] = None,
) -> dict:
    if golden_dir is None:
        golden_dir = (
            DEFAULT_SLIME_GOLDEN_DIR
            if DEFAULT_SLIME_GOLDEN_DIR.exists()
            else _alt_frames
        )
    if not fog_npy.exists() or not nofog_npy.exists():
        print(
            "slime_bounce magma frames missing; replaying is expensive. "
            f"Expected {fog_npy} and {nofog_npy}. Skip or set FOGCURVE_SLIME_*."
        )
        return {"name": "slime_bounce", "verdict": "skipped_missing_frames"}

    tf = np.load(ticks_fog)
    tn = np.load(ticks_nofog)
    if tick not in tf or tick not in tn:
        raise SystemExit(f"tick {tick} not in magma frames")
    fi = int(np.where(tf == tick)[0][0])
    ni = int(np.where(tn == tick)[0][0])
    magma_fog = np.load(fog_npy)[fi]
    magma_nofog = np.load(nofog_npy)[ni]
    gpath = golden_dir / f"f_{tick:06d}.png"
    if not gpath.exists():
        cands = list(golden_dir.glob(f"f_*{tick}.png")) + list(
            golden_dir.glob(f"*{tick}*.png")
        )
        if not cands:
            raise SystemExit(f"no golden for tick {tick} in {golden_dir}")
        gpath = cands[0]
    golden = np.array(Image.open(gpath).convert("RGB"))

    cam = cam_slime_t80()
    print(f"slime golden sky mean {golden[0].mean(0)}, using F={cam.fog_rgb8}")

    d_plane = cam.plane_hit_dist(plane_y=4.0)
    F = np.array(cam.fog_rgb8, dtype=np.float64)
    T = magma_nofog.astype(np.float64)
    t_gold = fog_factor(golden, T, F)
    t_magma = fog_factor(magma_fog, T, F)

    # Clean grass tops in the fog band (flat-world ground). Flat has no far
    # vertical faces; orientation discrimination uses seed7 trunks.
    grass = (
        (T[..., 1] > T[..., 0] + 5)
        & (T[..., 1] > T[..., 2] + 10)
        & (T[..., 1] > 70)
        & (T[..., 2] < 120)
    )
    inband = np.isfinite(d_plane) & (d_plane >= 96) & (d_plane <= 128)
    orth = _orth_to_fog(golden.astype(np.float64), T, F)
    hc = grass & inband & (orth < 8) & np.isfinite(t_gold) & np.isfinite(t_magma)
    hc[430:, :] = False  # HUD / hand

    both = hc & (d_plane >= 100) & (d_plane <= 122)
    if both.sum() > 50:
        tr = ramp_t(d_plane[both], 96, 128)
        print(
            f"slime bulk 100..122 n={both.sum()} "
            f"mean(t_gold-ramp)={np.mean(t_gold[both]-tr):+.4f} "
            f"mean(t_magma-ramp)={np.mean(t_magma[both]-tr):+.4f} "
            f"rmse_magma={np.sqrt(np.mean((t_magma[both]-tr)**2)):.4f}"
        )

    edges = np.arange(96, 128.01, 2.0)
    bins_g = bin_td(d_plane, t_gold, t_magma, hc, 96, 128, edges)
    print_bins(f"slime_bounce_t{tick} CLEAN GRASS (plane d)", bins_g)
    fit_g = fit_d_shift(
        d_plane, t_gold, hc & (d_plane >= 100) & (d_plane <= 122), 96, 128
    )
    print(f"plane-d grass fit δ: {fit_g}")

    # Free (start,end) fit for gold on bulk band
    d = d_plane[both]
    tg = t_gold[both]
    best = None
    if both.sum() > 50:
        for s in np.linspace(96, 110, 29):
            for e in np.linspace(s + 16, 150, 30):
                r = float(
                    np.sqrt(np.mean((tg - np.clip((d - s) / (e - s), 0, 1)) ** 2))
                )
                if best is None or r < best[0]:
                    best = (r, float(s), float(e))
        print(f"best free (start,end) for gold: {best}")

    res_g = (
        float(np.mean(t_gold[both] - ramp_t(d_plane[both], 96, 128)))
        if both.sum()
        else float("nan")
    )
    result = {
        "name": f"slime_bounce_t{tick}",
        "n_hc_grass": int(hc.sum()),
        "n_bulk": int(both.sum()),
        "res_ground": res_g,
        "res_vertical": float("nan"),  # none on flat world at fog range
        "fit_ground": fit_g,
        "best_free_se": (
            {"rmse": best[0], "start": best[1], "end": best[2]} if best else None
        ),
        "bins_ground": [b.__dict__ for b in bins_g],
        "verdict": "ground_only",
        "F": list(cam.fog_rgb8),
        "fog_start": 96.0,
        "fog_end": 128.0,
        "note": "flat world: no far vertical faces; orientation test is seed7",
    }
    print(f"LOCAL: slime ground residual {res_g:+.4f} (target ~-0.18)")
    return result


def global_verdict(results: list[dict]) -> str:
    """Combine slime ground residual with seed7 orientation differential.

    A: slime ground residual ~-0.18 AND seed7 grass-trunk residual ~0
    B: slime ground residual ~-0.18 AND seed7 grass-trunk residual ~-0.18
    """
    slime = next((r for r in results if str(r.get("name", "")).startswith("slime")), None)
    seed7 = next((r for r in results if r.get("name") == "seed7"), None)
    res_g = slime.get("res_ground") if slime else None
    diff = seed7.get("diff_grass_minus_trunk") if seed7 else None
    if res_g is not None and np.isfinite(res_g) and res_g < -0.10:
        if diff is not None and np.isfinite(diff):
            if abs(diff) < 0.06:
                return "A_fog_evaluation"
            if diff < -0.10:
                return "B_grazing_distance_mapping"
        # ground-only evidence of the gap; orientation unresolved
        return "ground_gap_orientation_unresolved"
    if seed7 and str(seed7.get("verdict", "")).startswith("A"):
        return "A_fog_evaluation"
    if seed7 and str(seed7.get("verdict", "")).startswith("B"):
        return "B_grazing_distance_mapping"
    return "inconclusive"


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--scene",
        choices=["all", "seed7", "pose0", "slime"],
        default="all",
    )
    ap.add_argument("--out", type=Path, default=SCRATCH)
    ap.add_argument("--slime-tick", type=int, default=80)
    args = ap.parse_args(argv)

    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)
    bin_path = out_dir / "game_candidate"

    results = []
    need_build = args.scene in ("all", "seed7", "pose0")
    if need_build:
        print("building game_candidate →", bin_path)
        ensure_game_candidate(bin_path)

    if args.scene in ("all", "seed7"):
        results.append(run_seed7(bin_path, out_dir))
    if args.scene in ("all", "pose0"):
        results.append(run_pose0(bin_path, out_dir))
    if args.scene in ("all", "slime"):
        results.append(run_slime(out_dir, tick=args.slime_tick))

    gv = global_verdict(results)
    print("\n==============================")
    print("GLOBAL VERDICT:", gv)
    print("Δt target from horizon band: -0.18 (gold less fogged)")
    print("implied d-shift if pure B: 0.18 * 32 = 5.76 blocks")
    print("==============================")

    report = {
        "global_verdict": gv,
        "results": results,
        "notes": {
            "delta_t_band": -0.18,
            "implied_d_shift_blocks": 0.18 * 32.0,
            "ramp": "t=(d-96)/32 linear EYE_RADIAL",
        },
    }
    report_path = out_dir / "fogcurve_report.json"
    report_path.write_text(json.dumps(report, indent=2, default=float))
    print("wrote", report_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
