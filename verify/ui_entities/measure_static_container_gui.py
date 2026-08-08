#!/usr/bin/env python3
"""Strict real-Java pixel gate for dispenser, dropper, and hopper panels."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


SCALE = 2
PANELS = {
    "gui_dispenser": (250, 74, 602, 406),
    "gui_dropper": (250, 74, 602, 406),
    "gui_hopper": (250, 106, 602, 372),
}
# Both real jar textures have the same 18 transparent corner texels. The
# bottom rows differ only by panel height, so express them relative to it.
TOP_TRANSPARENT = {
    0: ((0, 1), (173, 175)),
    1: ((0, 0), (174, 175)),
    2: ((175, 175),),
}
BOTTOM_TRANSPARENT = {
    3: ((0, 0),),
    2: ((0, 1), (175, 175)),
    1: ((0, 2), (174, 175)),
}


def load(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(state: str, shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = PANELS[state]
    mask[y0:y1, x0:x1] = True
    height = (y1 - y0) // SCALE
    spans_by_row = dict(TOP_TRANSPARENT)
    for from_bottom, spans in BOTTOM_TRANSPARENT.items():
        spans_by_row[height - from_bottom] = spans
    for source_y, spans in spans_by_row.items():
        fy = y0 + source_y * SCALE
        for source_x0, source_x1 in spans:
            fx0 = x0 + source_x0 * SCALE
            fx1 = x0 + (source_x1 + 1) * SCALE
            mask[fy:fy + SCALE, fx0:fx1] = False
    return mask


def metrics(first: np.ndarray, second: np.ndarray,
            mask: np.ndarray) -> dict[str, object]:
    frame_delta = np.max(np.abs(first - second), axis=2)
    delta = frame_delta[mask]
    changed = np.argwhere((frame_delta != 0) & mask)
    return {
        "compared_px": int(np.count_nonzero(mask)),
        "different_px": int(np.count_nonzero(delta)),
        "max_channel": int(delta.max(initial=0)),
        "mean_max_channel": float(delta.mean()),
        "different_locations_yx": changed[:64].astype(int).tolist(),
    }


def mutation_selftest() -> None:
    baseline = np.zeros((480, 854, 3), dtype=np.int16)
    for state in PANELS:
        candidate = baseline.copy()
        candidate[150, 300, 0] = 1
        result = metrics(
            baseline, candidate, panel_mask(state, baseline.shape))
        if result["different_px"] != 1 or result["max_channel"] != 1:
            raise RuntimeError(
                f"{state} GUI negative control was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", default="verify/ui_hud/goldens")
    parser.add_argument("--c-frames", required=True)
    parser.add_argument("--json-out")
    args = parser.parse_args()

    mutation_selftest()
    goldens = Path(args.goldens)
    c_frames = Path(args.c_frames)
    report = {
        "rule": "stable_owned_panel_pixel_exact",
        "excluded": "18 alpha-zero source texels per real jar panel",
        "states": {},
    }
    failed = False
    for state, panel in PANELS.items():
        java_a = load(goldens / f"{state}_a.png")
        java_b = load(goldens / f"{state}_b.png")
        native = load(c_frames / f"{state}.ppm")
        if java_a.shape != java_b.shape or java_a.shape != native.shape:
            raise RuntimeError(f"{state} frame dimensions differ")
        mask = panel_mask(state, java_a.shape)
        java_ab = metrics(java_a, java_b, mask)
        native_result = metrics(java_a, native, mask)
        stable = java_ab["different_px"] == 0
        exact = native_result["different_px"] == 0
        passed = stable and exact
        failed |= not passed
        report["states"][state] = {
            "panel_xyxy": list(panel),
            "java_ab": java_ab,
            "native": native_result,
            "passed": passed,
        }
        print(
            f"{state}: compared={native_result['compared_px']} "
            f"Java A/B={java_ab['different_px']} "
            f"native={native_result['different_px']} "
            f"max={native_result['max_channel']} "
            f"{'PASS' if passed else 'FAIL'}")

    if args.json_out:
        path = Path(args.json_out)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise RuntimeError("one or more static-container GUI panels differ")
    print("PASS static-container GUIs: every owned pixel exact vs real Java")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL static-container GUIs: {error}")
        raise SystemExit(1)
