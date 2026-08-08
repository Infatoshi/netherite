#!/usr/bin/env python3
"""Sub-pixel registration probe: golden vs candidate terrain crop.

If best fractional shift drops MAE by >=2, residual is projection/viewport,
not shade. Fable speed-plan item #3.

Usage (from repo root or magma):
  uv run --no-project --with numpy --with pillow --with scipy \\
    python verify/mc_capture/subpixel_reg.py \\
    --golden verify/mc_capture/mc_frame.png \\
    --cand /tmp/hard_scene_seed0/cand_default.png \\
    --crop 180:479,180:674
"""
from __future__ import annotations

import argparse
import json
import sys

import numpy as np
from PIL import Image


def parse_crop(s: str):
    # "180:479,180:674" -> rows, cols
    rs, cs = s.split(",")
    r0, r1 = map(int, rs.split(":"))
    c0, c1 = map(int, cs.split(":"))
    return r0, r1 + 1, c0, c1 + 1  # inclusive end in our crops → exclusive


def sobel_mag(gray: np.ndarray) -> np.ndarray:
    g = gray.astype(np.float64)
    gx = np.zeros_like(g)
    gy = np.zeros_like(g)
    gx[:, 1:-1] = g[:, 2:] - g[:, :-2]
    gy[1:-1, :] = g[2:, :] - g[:-2, :]
    return np.hypot(gx, gy)


def shift_frac(img: np.ndarray, dy: float, dx: float) -> np.ndarray:
    """Fourier shift (sub-pixel). img HxWxC float."""
    from scipy.ndimage import fourier_shift

    out = np.empty_like(img)
    for c in range(img.shape[2]):
        f = np.fft.fftn(img[:, :, c])
        f = fourier_shift(f, shift=(dy, dx))
        out[:, :, c] = np.fft.ifftn(f).real
    return out


def mae(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.mean(np.abs(a - b)))


def gcorr(a: np.ndarray, b: np.ndarray) -> float:
    ga, gb = a[:, :, 1].ravel(), b[:, :, 1].ravel()
    if ga.std() < 1e-6 or gb.std() < 1e-6:
        return float("nan")
    return float(np.corrcoef(ga, gb)[0, 1])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--golden", required=True)
    ap.add_argument("--cand", required=True)
    ap.add_argument("--crop", default="180:479,180:674")
    ap.add_argument("--max-shift", type=float, default=4.0, help="search ±N px")
    ap.add_argument("--step", type=float, default=0.25, help="fractional step")
    ap.add_argument("--out", default="/tmp/hard_scene_agents/subpixel_reg.json")
    args = ap.parse_args()

    gold = np.asarray(Image.open(args.golden).convert("RGB"), dtype=np.float64)
    cand = np.asarray(Image.open(args.cand).convert("RGB"), dtype=np.float64)
    if gold.shape != cand.shape:
        print(f"shape mismatch gold={gold.shape} cand={cand.shape}", file=sys.stderr)
        return 2
    r0, r1, c0, c1 = parse_crop(args.crop)
    g = gold[r0:r1, c0:c1]
    c = cand[r0:r1, c0:c1]
    base_mae = mae(g, c)
    base_gc = gcorr(g, c)

    # Integer peak via phase correlation on edge maps
    eg, ec = sobel_mag(g.mean(2)), sobel_mag(c.mean(2))
    eg0, ec0 = eg - eg.mean(), ec - ec.mean()
    R = np.fft.fft2(eg0) * np.conj(np.fft.fft2(ec0))
    R /= np.abs(R) + 1e-9
    r = np.fft.ifft2(R)
    peak = np.unravel_index(np.argmax(np.abs(r)), r.shape)
    shifty = int(peak[0] if peak[0] < r.shape[0] // 2 else peak[0] - r.shape[0])
    shiftx = int(peak[1] if peak[1] < r.shape[1] // 2 else peak[1] - r.shape[1])

    # Coarse integer grid then refine around best
    best = (base_mae, 0.0, 0.0, base_gc)
    for dy in np.arange(-args.max_shift, args.max_shift + 1e-9, 1.0):
        for dx in np.arange(-args.max_shift, args.max_shift + 1e-9, 1.0):
            cs = shift_frac(c, dy, dx)
            m = mae(g, cs)
            if m < best[0]:
                best = (m, float(dy), float(dx), gcorr(g, cs))

    # Fine grid around best integer-ish
    cy, cx = best[1], best[2]
    for dy in np.arange(cy - 1.0, cy + 1.0 + 1e-9, args.step):
        for dx in np.arange(cx - 1.0, cx + 1.0 + 1e-9, args.step):
            cs = shift_frac(c, dy, dx)
            m = mae(g, cs)
            if m < best[0]:
                best = (m, float(dy), float(dx), gcorr(g, cs))

    drop = base_mae - best[0]
    verdict = (
        "PROJECTION/VIEWPORT_LIKELY"
        if drop >= 2.0
        else ("SMALL_REG" if drop >= 0.5 else "NO_REG_WIN_RENDERER")
    )
    report = {
        "golden": args.golden,
        "cand": args.cand,
        "crop": args.crop,
        "base_mae": base_mae,
        "base_gcorr": base_gc,
        "phase_corr_int_shift_dy_dx": [shifty, shiftx],
        "best_mae": best[0],
        "best_dy": best[1],
        "best_dx": best[2],
        "best_gcorr": best[3],
        "mae_drop": drop,
        "verdict": verdict,
        "fable_threshold_drop": 2.0,
        "note": "drop>=2 => residual is registration/projection; else renderer/texture",
    }
    out = open(args.out, "w")
    json.dump(report, out, indent=2)
    out.close()
    print(json.dumps(report, indent=2))
    print(f"\nVERDICT: {verdict} (MAE {base_mae:.3f} -> {best[0]:.3f}, drop {drop:.3f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
