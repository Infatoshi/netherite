#!/usr/bin/env python3
"""Strict real-Java/native subject gate for TNT minecart render phases."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = (
    "minecart_tnt_fuse80_flash",
    "minecart_tnt_fuse79_dark",
    "minecart_tnt_fuse4_flash",
    "minecart_tnt_fuse5_dark",
    "minecart_tnt_unprimed",
)
FLASH_DARK = (
    ("minecart_tnt_fuse80_flash", "minecart_tnt_fuse79_dark"),
    ("minecart_tnt_fuse4_flash", "minecart_tnt_fuse5_dark"),
)
BACKGROUND = "minecart_tnt_background"
ROI = (350, 110, 505, 240)
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25
WHITE_THRESHOLD = 252

# Fixed from the recaptured canonical Java/native lane. The +90-degree block
# transform and FaceBakery UV contraction make the unscaled dark states exact.
# The two swelled late-fuse states retain a measured one-pixel texel-selection
# tail, and their flash redraw adds the separately measured registration/
# content tail. Fixed ceilings keep either residual from growing silently.
BUDGETS = {
    "minecart_tnt_fuse80_flash": {
        "minimum_owned": 5700, "hard_px": 1,
        "over_4_px": 301, "max_channel": 87,
    },
    "minecart_tnt_fuse79_dark": {
        "minimum_owned": 5700, "hard_px": 0,
        "over_4_px": 0, "max_channel": 1,
    },
    "minecart_tnt_fuse4_flash": {
        "minimum_owned": 5700, "hard_px": 199,
        "over_4_px": 521, "max_channel": 215,
    },
    "minecart_tnt_fuse5_dark": {
        "minimum_owned": 5700, "hard_px": 327,
        "over_4_px": 562, "max_channel": 215,
    },
    "minecart_tnt_unprimed": {
        "minimum_owned": 5700, "hard_px": 0,
        "over_4_px": 0, "max_channel": 1,
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
        raise ValueError("TNT minecart frame dimensions disagree")
    roi = roi_mask(java.shape)
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


def flash_measure(flash: np.ndarray, dark: np.ndarray) -> dict:
    roi = roi_mask(flash.shape)
    delta = np.max(np.abs(flash - dark), axis=2)
    white = np.all(flash >= WHITE_THRESHOLD, axis=2) & roi
    dark_white = np.all(dark >= WHITE_THRESHOLD, axis=2) & roi
    white_bbox = None
    if np.any(white):
        ys, xs = np.where(white)
        white_bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    return {
        "changed_px": int((roi & (delta > HARD_THRESHOLD)).sum()),
        "white_px": int(white.sum()),
        "white_bbox": white_bbox,
        "dark_white_px": int(dark_white.sum()),
    }


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[150, 425] = (80, 90, 100)
    exact = measure(java, bg, java.copy(), bg)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("TNT minecart exact control failed")
    mutated = java.copy()
    mutated[150, 425, 0] += HARD_THRESHOLD + 1
    if measure(java, bg, mutated, bg)["hard_px"] != 1:
        raise RuntimeError("TNT minecart mutation was not detected")
    flash = java.copy()
    flash[140:160, 410:440] = 255
    phase = flash_measure(flash, java)
    if phase["changed_px"] != 600 or phase["white_px"] != 600:
        raise RuntimeError("TNT minecart flash control failed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--debug-out", type=Path,
                        help="write background-normalized pairs for pxdiff")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    java_bg_a = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java_bg_a, java_bg_b):
        raise RuntimeError("TNT minecart background Java A/B is not exact")

    report = {
        "rule": "stable_subject_bounded_raster_tail_and_exact_flash_phases",
        "roi_xyxy": list(ROI),
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
        "phases": {},
    }
    failed = []
    java_frames = {}
    native_frames = {}
    for state in STATES:
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        native = pixels(candidate_path(args.c_frames, state))
        java_frames[state] = java_a
        native_frames[state] = native
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
        budget = BUDGETS[state]
        row["budget"] = budget
        row["pass"] = (
            row["java_owned_px"] >= budget["minimum_owned"]
            and row["native_owned_px"] >= budget["minimum_owned"]
            and row["hard_px"] <= budget["hard_px"]
            and row["over_4_px"] <= budget["over_4_px"]
            and row["max_channel"] <= budget["max_channel"])
        if not row["pass"]:
            failed.append(state)
        report["states"][state] = row
        print(
            f"{state}: Java A/B=0 owned={row['owned_union_px']} "
            f">4={row['over_4_px']} hard={row['hard_px']} "
            f"max={row['max_channel']} "
            f"{'PASS' if row['pass'] else 'FAIL'}")

    for flash, dark in FLASH_DARK:
        java_phase = flash_measure(java_frames[flash], java_frames[dark])
        native_phase = flash_measure(native_frames[flash], native_frames[dark])
        phase_ok = (
            java_phase["changed_px"] >= 2000
            and native_phase["changed_px"] >= 2000
            and java_phase["white_px"] >= 2000
            and native_phase["white_px"] >= 2000
            and java_phase["dark_white_px"] == 0
            and native_phase["dark_white_px"] == 0)
        report["phases"][flash] = {
            "dark": dark, "java": java_phase, "native": native_phase,
            "pass": phase_ok,
        }
        if not phase_ok:
            failed.append(flash + ":phase")
        print(
            f"{flash} vs {dark}: "
            f"Java changed/white={java_phase['changed_px']}/"
            f"{java_phase['white_px']} native={native_phase['changed_px']}/"
            f"{native_phase['white_px']} "
            f"{'PASS' if phase_ok else 'FAIL'}")

    report["pass"] = not failed
    report["failed"] = failed
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if failed and not args.report_only:
        raise RuntimeError(
            "TNT minecart subject contract failed: " + ", ".join(failed))
    if not failed:
        print("PASS TNT minecart: five stable Java states and two flash phases")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL TNT minecart: {error}")
        raise SystemExit(1)
