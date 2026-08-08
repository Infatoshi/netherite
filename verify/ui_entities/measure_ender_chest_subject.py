#!/usr/bin/env python3
"""Real-Java/native subject gate for the Ender Chest TESR keyframes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = {
    "ender_chest_closed": {"over_4_px": 2474, "max_channel": 19},
    "ender_chest_open": {"over_4_px": 1721, "max_channel": 19},
}
BACKGROUND = "ender_chest_background"
# This fixture projects the complete tile inside this rect. Restrict ownership
# so unrelated far-horizon fixed-function differences cannot join the mask.
ROI = (350, 115, 500, 290)
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25


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
    return mask


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray) -> dict:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("Ender Chest frame dimensions disagree")
    roi = roi_mask(java.shape)
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    direct = np.max(np.abs(java - native), axis=2)
    hard = owned & (direct > HARD_THRESHOLD)
    over_4 = owned & (direct > 4)
    if np.any(owned):
        ys, xs = np.where(owned)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
        maximum = int(direct[owned].max())
        mean_max = float(direct[owned].mean())
    else:
        bbox = None
        maximum = 0
        mean_max = 0.0
    return {
        "java_owned_px": int(java_owned.sum()),
        "native_owned_px": int(native_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "over_4_px": int(over_4.sum()),
        "hard_px": int(hard.sum()),
        "max_channel": maximum,
        "mean_max_channel": mean_max,
        "bbox": bbox,
        "hard_locations_yx": np.argwhere(hard)[:64].astype(int).tolist(),
    }


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[200, 425] = (80, 90, 100)
    exact = measure(java, bg, java.copy(), bg)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("Ender Chest subject exact control failed")
    mutated = java.copy()
    mutated[200, 425, 0] += HARD_THRESHOLD + 1
    caught = measure(java, bg, mutated, bg)
    if caught["hard_px"] != 1:
        raise RuntimeError("Ender Chest subject mutation was not detected")


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
        raise RuntimeError("Ender Chest background Java A/B is not exact")

    report = {
        "rule": "stable_subject_zero_hard_px_bounded_fixed_function_tail",
        "roi_xyxy": list(ROI),
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
    }
    failed = []
    for state, budget in STATES.items():
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        row = measure(
            java_a, java_bg_a,
            pixels(candidate_path(args.c_frames, state)), native_bg)
        row["budget"] = budget
        row["pass"] = (
            row["java_owned_px"] >= 7000
            and row["native_owned_px"] >= 7000
            and row["hard_px"] == 0
            and row["over_4_px"] <= budget["over_4_px"]
            and row["max_channel"] <= budget["max_channel"])
        report["states"][state] = row
        if not row["pass"]:
            failed.append(state)
        print(
            f"{state}: Java A/B=0 owned={row['owned_union_px']} "
            f">4={row['over_4_px']}/{budget['over_4_px']} "
            f"hard={row['hard_px']}/0 "
            f"max={row['max_channel']}/{budget['max_channel']} "
            f"{'PASS' if row['pass'] else 'FAIL'}")

    report["pass"] = not failed
    report["failed"] = failed
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if failed:
        raise RuntimeError(
            "Ender Chest TESR subject contract failed: " + ", ".join(failed))
    print("PASS Ender Chest TESR: closed/open Java A/B exact, zero hard pixels")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Ender Chest TESR: {error}")
        raise SystemExit(1)
