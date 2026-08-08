#!/usr/bin/env python3
"""Stable real-Java/native owned-pixel measurement for the Beacon world TESR."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


SUBJECT = "beacon_world_colored"
BACKGROUND = "beacon_world_background"
ROI = (320, 0, 480, 380)
# The crosshair is a common HUD overlay, not Beacon-owned. Its invert blend
# changes when the subject changes the pixel behind it, so background
# subtraction alone cannot remove it.
CROSSHAIR = (416, 229, 439, 252)
OWNED_THRESHOLD = 4
HARD_THRESHOLD = 25
BOUNDED_BUDGET = {
    "ownership_xor_px": 60,
    "over_4_px": 3425,
    "hard_px": 4,
    "max_channel": 136,
}


def pixels(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def candidate_path(root: Path, state: str) -> Path:
    for suffix in (".ppm", ".png"):
        path = root / (state + suffix)
        if path.is_file():
            return path
    raise FileNotFoundError(f"missing candidate frame for {state}")


def roi_mask(shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = ROI
    mask[y0:y1, x0:x1] = True
    x0, y0, x1, y1 = CROSSHAIR
    mask[y0:y1, x0:x1] = False
    return mask


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray) -> tuple[dict, np.ndarray]:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("Beacon world frame dimensions disagree")
    roi = roi_mask(java.shape)
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    direct = np.max(np.abs(java - native), axis=2)
    hard = owned & (direct > HARD_THRESHOLD)
    over_4 = owned & (direct > 4)
    different = owned & (direct > 0)
    if np.any(owned):
        ys, xs = np.where(owned)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
        maximum = int(direct[owned].max())
        mean_max = float(direct[owned].mean())
    else:
        bbox = None
        maximum = 0
        mean_max = 0.0
    return ({
        "java_owned_px": int(java_owned.sum()),
        "native_owned_px": int(native_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "ownership_xor_px": int(np.count_nonzero(java_owned ^ native_owned)),
        "different_px": int(different.sum()),
        "over_4_px": int(over_4.sum()),
        "hard_px": int(hard.sum()),
        "max_channel": maximum,
        "mean_max_channel": mean_max,
        "bbox": bbox,
        "hard_locations_yx": np.argwhere(hard)[:64].astype(int).tolist(),
    }, owned)


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[200, 425] = (80, 90, 100)
    exact, _ = measure(java, bg, java.copy(), bg)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("Beacon world exact control failed")
    mutated = java.copy()
    mutated[200, 425, 0] += HARD_THRESHOLD + 1
    caught, _ = measure(java, bg, mutated, bg)
    if caught["hard_px"] != 1:
        raise RuntimeError("Beacon world mutation was not detected")


def save_owned(path: Path, frame: np.ndarray, owned: np.ndarray) -> None:
    result = np.zeros(frame.shape, dtype=np.uint8)
    result[owned] = np.clip(frame[owned], 0, 255).astype(np.uint8)
    Image.fromarray(result, "RGB").save(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--strict", action="store_true",
                        help="require zero differing owned pixels")
    args = parser.parse_args()
    mutation_selftest()

    java = pixels(args.goldens / (SUBJECT + "_a.png"))
    java_b = pixels(args.goldens / (SUBJECT + "_b.png"))
    java_bg = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native = pixels(candidate_path(args.c_frames, SUBJECT))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java, java_b):
        raise RuntimeError("Beacon subject Java A/B is not exact")
    if not np.array_equal(java_bg, java_bg_b):
        raise RuntimeError("Beacon background Java A/B is not exact")

    row, owned = measure(java, java_bg, native, native_bg)
    within_budget = all(
        row[key] <= limit for key, limit in BOUNDED_BUDGET.items())
    if args.strict:
        within_budget = row["different_px"] == 0
    report = {
        "rule": "stable_background_subtracted_owned_pixels",
        "roi_xyxy": list(ROI),
        "excluded_crosshair_xyxy": list(CROSSHAIR),
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "java_ab_px": 0,
        "budget": BOUNDED_BUDGET,
        "metrics": row,
        "strict": args.strict,
        "pass": within_budget,
    }
    if args.out_dir:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        save_owned(args.out_dir / "java_owned.png", java, owned)
        save_owned(args.out_dir / "native_owned.png", native, owned)
        Image.fromarray((owned.astype(np.uint8) * 255), "L").save(
            args.out_dir / "owned_mask.png")
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")

    print(
        f"{SUBJECT}: Java A/B=0 owned={row['owned_union_px']} "
        f"xor={row['ownership_xor_px']} diff={row['different_px']} "
        f">4={row['over_4_px']} hard={row['hard_px']} "
        f"max={row['max_channel']} mean={row['mean_max_channel']:.3f}")
    if row["owned_union_px"] < 1000:
        raise RuntimeError("Beacon world ownership mask is vacuous")
    if not within_budget and not args.diagnostic:
        raise RuntimeError("Beacon world owned-pixel budget exceeded")
    if within_budget and not args.diagnostic:
        print("PASS Beacon world: stable Java A/B and bounded native residual")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Beacon world: {error}")
        raise SystemExit(1)
