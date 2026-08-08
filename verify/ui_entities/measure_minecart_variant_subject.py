#!/usr/bin/env python3
"""Strict real-Java/native subject gate for the six non-TNT minecarts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = (
    "minecart_empty_model",
    "minecart_chest_model",
    "minecart_furnace_model",
    "minecart_hopper_model",
    "minecart_spawner_model",
    "minecart_command_model",
)
BACKGROUND = "minecart_tnt_background"
ROI = (350, 110, 505, 240)
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25

# Fixed from the canonical Java/native lane after pxdiff classification. Empty,
# chest, furnace, and command are exact apart from isolated fixed-function
# shading samples. Hopper's 31 low-amplitude pixels are two measured
# shading-offset strips on the narrow rim; spawner has two isolated
# shading-offset samples. New geometry, UV, or subtype-dispatch errors are loud.
BUDGETS = {
    "minecart_empty_model": {
        "minimum_java_owned": 3400, "minimum_native_owned": 3650,
        "hard_px": 0, "over_4_px": 1, "max_channel": 15,
    },
    "minecart_chest_model": {
        "minimum_java_owned": 5300, "minimum_native_owned": 5600,
        "hard_px": 0, "over_4_px": 1, "max_channel": 12,
    },
    "minecart_furnace_model": {
        "minimum_java_owned": 4800, "minimum_native_owned": 5150,
        "hard_px": 0, "over_4_px": 0, "max_channel": 1,
    },
    "minecart_hopper_model": {
        "minimum_java_owned": 4150, "minimum_native_owned": 4400,
        "hard_px": 0, "over_4_px": 31, "max_channel": 6,
    },
    "minecart_spawner_model": {
        "minimum_java_owned": 4800, "minimum_native_owned": 5150,
        "hard_px": 1, "over_4_px": 2, "max_channel": 123,
    },
    "minecart_command_model": {
        "minimum_java_owned": 5600, "minimum_native_owned": 5900,
        "hard_px": 0, "over_4_px": 0, "max_channel": 1,
    },
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
    out = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = ROI
    out[y0:y1, x0:x1] = True
    return out


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray) -> dict:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("minecart frame dimensions disagree")
    roi = roi_mask(java.shape)
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    direct = np.max(np.abs(java - native), axis=2)
    hard = owned & (direct > HARD_THRESHOLD)
    over_4 = owned & (direct > 4)
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
        "max_channel": int(direct[owned].max()) if np.any(owned) else 0,
        "bbox": bbox,
        "hard_locations_yx": np.argwhere(hard)[:64].astype(int).tolist(),
    }


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[150, 425] = (80, 90, 100)
    exact = measure(java, bg, java.copy(), bg)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("minecart exact control failed")
    mutated = java.copy()
    mutated[150, 425, 0] += HARD_THRESHOLD + 1
    if measure(java, bg, mutated, bg)["hard_px"] != 1:
        raise RuntimeError("minecart mutation was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--debug-out", type=Path,
                        help="write background-normalized pairs for pxdiff")
    parser.add_argument("--only", choices=STATES, action="append",
                        help="measure one or more states (repeatable)")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    java_bg_a = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java_bg_a, java_bg_b):
        raise RuntimeError("minecart background Java A/B is not exact")

    report = {
        "rule": "stable_subject_bounded_raster_tail",
        "roi_xyxy": list(ROI),
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
    }
    failed = []
    for state in (tuple(args.only) if args.only else STATES):
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        native = pixels(candidate_path(args.c_frames, state))
        row = measure(java_a, java_bg_a, native, native_bg)
        if args.debug_out:
            args.debug_out.mkdir(parents=True, exist_ok=True)
            roi = roi_mask(java_a.shape)
            java_owned = (
                np.max(np.abs(java_a - java_bg_a), axis=2)
                > OWNED_THRESHOLD) & roi
            native_owned = (
                np.max(np.abs(native - native_bg), axis=2)
                > OWNED_THRESHOLD) & roi
            owned = java_owned | native_owned
            java_debug = np.zeros_like(java_a)
            native_debug = np.zeros_like(native)
            java_debug[owned] = java_a[owned]
            native_debug[owned] = native[owned]
            Image.fromarray(java_debug.astype(np.uint8)).save(
                args.debug_out / (state + "_java.png"))
            Image.fromarray(native_debug.astype(np.uint8)).save(
                args.debug_out / (state + "_native.png"))
        budget = BUDGETS.get(state)
        row["budget"] = budget
        row["pass"] = bool(budget) and (
            row["java_owned_px"] >= budget["minimum_java_owned"]
            and row["native_owned_px"] >= budget["minimum_native_owned"]
            and row["hard_px"] <= budget["hard_px"]
            and row["over_4_px"] <= budget["over_4_px"]
            and row["max_channel"] <= budget["max_channel"])
        if not row["pass"]:
            failed.append(state)
        report["states"][state] = row
        print(
            f"{state}: Java A/B=0 java/native/union="
            f"{row['java_owned_px']}/{row['native_owned_px']}/"
            f"{row['owned_union_px']} >4={row['over_4_px']} "
            f"hard={row['hard_px']} max={row['max_channel']} "
            f"{'PASS' if row['pass'] else 'UNGATED'}")

    report["pass"] = not failed
    report["failed"] = failed
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if failed and not args.report_only:
        raise RuntimeError(
            "minecart variant subject contract failed: " + ", ".join(failed))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL minecart variants: {error}")
        raise SystemExit(1)
