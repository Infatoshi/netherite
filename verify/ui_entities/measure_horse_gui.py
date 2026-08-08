#!/usr/bin/env python3
"""Bounded real-Java pixel gate for the horse inventory containers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = {
    "gui_horse_armor": {
        "different_px": 241, "over_1_px": 0,
        "hard_px": 0, "max_channel": 1,
    },
    "gui_horse_donkey_chest": {
        "different_px": 291, "over_1_px": 34,
        "hard_px": 22, "max_channel": 104,
    },
    "gui_horse_llama_chest": {
        "different_px": 622, "over_1_px": 4,
        "hard_px": 0, "max_channel": 10,
    },
}
PANEL = (250, 74, 602, 406)
HARD_THRESHOLD = 25


def load(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = PANEL
    mask[y0:y1, x0:x1] = True
    # GuiContainer has four transparent rounded corners. They expose the live
    # world behind the screen and therefore are not owned by this GUI gate.
    for x, y in ((x0, y0), (x1 - 6, y0),
                 (x0, y1 - 6), (x1 - 6, y1 - 6)):
        mask[y:y + 6, x:x + 6] = False
    return mask


def metrics(first: np.ndarray, second: np.ndarray,
            mask: np.ndarray) -> dict[str, int | float | list[list[int]]]:
    frame_delta = np.max(np.abs(first - second), axis=2)
    delta = frame_delta[mask]
    hard = np.argwhere((frame_delta > HARD_THRESHOLD) & mask)
    return {
        "different_px": int(np.count_nonzero(delta)),
        "over_1_px": int(np.count_nonzero(delta > 1)),
        "hard_px": int(np.count_nonzero(delta > HARD_THRESHOLD)),
        "max_channel": int(delta.max(initial=0)),
        "mean_max_channel": float(delta.mean()),
        "hard_locations_yx": hard[:64].astype(int).tolist(),
    }


def within_budget(result: dict[str, int | float], budget: dict[str, int]) -> bool:
    return (result["different_px"] <= budget["different_px"]
            and result["over_1_px"] <= budget["over_1_px"]
            and result["hard_px"] <= budget["hard_px"]
            and result["max_channel"] <= budget["max_channel"])


def mutation_selftest() -> None:
    baseline = np.zeros((8, 8, 3), dtype=np.int16)
    candidate = baseline.copy()
    candidate[2, 2, 0] = HARD_THRESHOLD + 1
    mask = np.ones((8, 8), dtype=bool)
    result = metrics(baseline, candidate, mask)
    if result["hard_px"] != 1 or within_budget(
            result, {"different_px": 0, "over_1_px": 0,
                     "hard_px": 0, "max_channel": HARD_THRESHOLD}):
        raise RuntimeError("horse GUI negative control was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", default="verify/ui_hud/goldens")
    parser.add_argument("--c-frames", required=True)
    parser.add_argument("--json-out")
    parser.add_argument("--diagnostic-dir")
    args = parser.parse_args()

    mutation_selftest()
    goldens = Path(args.goldens)
    candidates = Path(args.c_frames)
    report = {
        "rule": "stable_panel_exact_java_ab_bounded_native_raster_edges",
        "hard_threshold": HARD_THRESHOLD,
        "panel_xyxy": list(PANEL),
        "states": {},
    }
    failed = False
    for state, budget in STATES.items():
        java_a = load(goldens / f"{state}_a.png")
        java_b = load(goldens / f"{state}_b.png")
        native = load(candidates / f"{state}.ppm")
        if java_a.shape != java_b.shape or java_a.shape != native.shape:
            raise RuntimeError(f"{state}: frame dimensions differ")
        mask = panel_mask(java_a.shape)
        java_ab = metrics(java_a, java_b, mask)
        native_result = metrics(java_a, native, mask)
        entry = {
            "java_ab": java_ab,
            "native": native_result,
            "native_budget": budget,
        }
        report["states"][state] = entry
        if args.diagnostic_dir:
            diagnostic_dir = Path(args.diagnostic_dir)
            diagnostic_dir.mkdir(parents=True, exist_ok=True)
            for suffix, frame in (("java", java_a), ("native", native)):
                cut = np.zeros_like(frame, dtype=np.uint8)
                cut[mask] = np.clip(frame[mask], 0, 255).astype(np.uint8)
                Image.fromarray(cut, "RGB").save(
                    diagnostic_dir / f"{state}_{suffix}.png")
        stable = java_ab["different_px"] == 0
        passed = stable and within_budget(native_result, budget)
        failed |= not passed
        print(
            f"{state}: Java A/B={java_ab['different_px']} "
            f"native px={native_result['different_px']}/{budget['different_px']} "
            f">1={native_result['over_1_px']}/{budget['over_1_px']} "
            f"native hard={native_result['hard_px']}/{budget['hard_px']} "
            f"max={native_result['max_channel']}/{budget['max_channel']} "
            f"{'PASS' if passed else 'FAIL'}")

    if args.json_out:
        path = Path(args.json_out)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise RuntimeError("horse GUI pixel contract failed")
    print("PASS horse GUI: real containers stable; bounded raster edge residual locked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL horse GUI: {error}")
        raise SystemExit(1)
