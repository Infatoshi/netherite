#!/usr/bin/env python3
"""Strict real-Java/native subject gate for all six Boat wood variants."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = (
    "boat_oak_model", "boat_spruce_model", "boat_birch_model",
    "boat_jungle_model", "boat_acacia_model", "boat_darkoak_model",
)
BACKGROUND = "boat_background"
ROI = (280, 80, 575, 280)
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25
# The silhouette and ownership masks are identical. These seven variant-only
# samples are measured minification tie pixels at the same three screen
# coordinates; contrast makes one Acacia sample exceed the hard threshold.
# Any added geometry, registration drift, or texture dispatch error is loud.
BUDGETS = {
    "boat_oak_model": (0, 0, 1),
    "boat_spruce_model": (0, 0, 4),
    "boat_birch_model": (0, 1, 10),
    "boat_jungle_model": (0, 2, 18),
    "boat_acacia_model": (1, 3, 61),
    "boat_darkoak_model": (0, 3, 24),
}


def pixels(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def candidate_path(root: Path, state: str) -> Path:
    for suffix in (".ppm", ".png"):
        path = root / (state + suffix)
        if path.is_file():
            return path
    raise FileNotFoundError(f"missing candidate frame for {state}")


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray) -> dict:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("Boat frame dimensions disagree")
    roi = np.zeros(java.shape[:2], dtype=bool)
    x0, y0, x1, y1 = ROI
    roi[y0:y1, x0:x1] = True
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    direct = np.max(np.abs(java - native), axis=2)
    return {
        "java_owned_px": int(java_owned.sum()),
        "native_owned_px": int(native_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "over_4_px": int((owned & (direct > 4)).sum()),
        "hard_px": int((owned & (direct > HARD_THRESHOLD)).sum()),
        "max_channel": int(direct[owned].max()) if np.any(owned) else 0,
    }


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[150, 425] = (80, 90, 100)
    if measure(java, bg, java.copy(), bg)["hard_px"] != 0:
        raise RuntimeError("Boat exact control failed")
    changed = java.copy()
    changed[150, 425, 0] += HARD_THRESHOLD + 1
    if measure(java, bg, changed, bg)["hard_px"] != 1:
        raise RuntimeError("Boat mutation was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    java_bg_a = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java_bg_a, java_bg_b):
        raise RuntimeError("Boat background Java A/B is not exact")

    report = {"rule": "boat_variant_subject", "states": {}, "pass": True}
    for state in STATES:
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        row = measure(
            java_a, java_bg_a,
            pixels(candidate_path(args.c_frames, state)), native_bg)
        hard_budget, over_4_budget, max_budget = BUDGETS[state]
        row["budget"] = {
            "hard_px": hard_budget,
            "over_4_px": over_4_budget,
            "max_channel": max_budget,
        }
        row["pass"] = (
            row["java_owned_px"] >= 14000
            and row["native_owned_px"] >= 14000
            and row["hard_px"] <= hard_budget
            and row["over_4_px"] <= over_4_budget
            and row["max_channel"] <= max_budget)
        report["states"][state] = row
        report["pass"] = report["pass"] and row["pass"]
        print(
            f"{state}: Java A/B=0 owned={row['java_owned_px']}/"
            f"{row['native_owned_px']} >4={row['over_4_px']} "
            f"hard={row['hard_px']} max={row['max_channel']} "
            f"{'PASS' if row['pass'] else 'FAIL'}")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if not report["pass"]:
        raise RuntimeError("Boat variant subject contract failed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Boat variants: {error}")
        raise SystemExit(1)
