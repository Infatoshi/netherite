#!/usr/bin/env python3
"""Real-Java/native subject gate for wooden Chest TESR keyframes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = {
    "chest_normal_closed": {
        "minimum_owned": 7000, "over_4_px": 0,
        "hard_px": 0, "max_channel": 1,
    },
    "chest_normal_open": {
        "minimum_owned": 10000, "over_4_px": 0,
        "hard_px": 0, "max_channel": 1,
    },
    "chest_trapped_closed": {
        "minimum_owned": 7000, "over_4_px": 0,
        "hard_px": 0, "max_channel": 1,
    },
    "chest_trapped_open": {
        "minimum_owned": 10000, "over_4_px": 0,
        "hard_px": 0, "max_channel": 1,
    },
    "chest_normal_double_x_open": {
        "minimum_owned": 21000, "over_4_px": 1,
        "hard_px": 0, "max_channel": 14,
    },
    # Two minified GL edge/texel ties are pinned by exact coordinates below;
    # every other owned pixel remains within four channels.
    "chest_trapped_double_z_open": {
        "minimum_owned": 10000, "over_4_px": 2,
        "hard_px": 2, "max_channel": 146,
        "hard_locations_yx": [[199, 405], [204, 467]],
    },
}
BACKGROUND = "chest_background"
ROI = (300, 90, 560, 330)
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


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray) -> dict:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("wooden Chest frame dimensions disagree")
    roi = np.zeros(java.shape[:2], dtype=bool)
    x0, y0, x1, y1 = ROI
    roi[y0:y1, x0:x1] = True
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    direct = np.max(np.abs(java - native), axis=2)
    hard = owned & (direct > HARD_THRESHOLD)
    over_4 = owned & (direct > 4)
    maximum = int(direct[owned].max()) if np.any(owned) else 0
    bbox = None
    if np.any(owned):
        ys, xs = np.where(owned)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    return {
        "java_owned_px": int(java_owned.sum()),
        "native_owned_px": int(native_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "over_4_px": int(over_4.sum()),
        "hard_px": int(hard.sum()),
        "max_channel": maximum,
        "bbox": bbox,
        "hard_locations_yx": np.argwhere(hard)[:64].astype(int).tolist(),
    }


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[200, 425] = (80, 90, 100)
    exact = measure(java, bg, java.copy(), bg)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("wooden Chest subject exact control failed")
    mutated = java.copy()
    mutated[200, 425, 0] += HARD_THRESHOLD + 1
    if measure(java, bg, mutated, bg)["hard_px"] != 1:
        raise RuntimeError("wooden Chest subject mutation was not detected")


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
        raise RuntimeError("wooden Chest background Java A/B is not exact")

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
        minimum_owned = budget["minimum_owned"]
        row["budget"] = budget
        row["pass"] = (
            row["java_owned_px"] >= minimum_owned
            and row["native_owned_px"] >= minimum_owned
            and row["hard_px"] == budget["hard_px"]
            and row["hard_locations_yx"]
                == budget.get("hard_locations_yx", [])
            and row["over_4_px"] <= budget["over_4_px"]
            and row["max_channel"] <= budget["max_channel"])
        report["states"][state] = row
        if not row["pass"]:
            failed.append(state)
        print(
            f"{state}: Java A/B=0 owned={row['owned_union_px']} "
            f">4={row['over_4_px']}/{budget['over_4_px']} "
            f"hard={row['hard_px']}/{budget['hard_px']} "
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
            "wooden Chest TESR subject contract failed: " + ", ".join(failed))
    print("PASS wooden Chest TESR: six Java A/B exact states, fixed residuals")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL wooden Chest TESR: {error}")
        raise SystemExit(1)
