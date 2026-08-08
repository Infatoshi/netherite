#!/usr/bin/env python3
"""Strict real-Java pixel gate for one standard 176x166 container panel."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


SCALE = 2
PANEL = (250, 74, 602, 406)
TRANSPARENT = {
    0: ((0, 1), (173, 175)),
    1: ((0, 0), (174, 175)),
    2: ((175, 175),),
    163: ((0, 0),),
    164: ((0, 1), (175, 175)),
    165: ((0, 2), (174, 175)),
}


def load(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = PANEL
    mask[y0:y1, x0:x1] = True
    for source_y, spans in TRANSPARENT.items():
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
    candidate = baseline.copy()
    candidate[150, 300, 0] = 1
    result = metrics(baseline, candidate, panel_mask(baseline.shape))
    if result["different_px"] != 1 or result["max_channel"] != 1:
        raise RuntimeError("standard-container negative control was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state", required=True)
    parser.add_argument("--goldens", default="verify/ui_hud/goldens")
    parser.add_argument("--c-frame", required=True)
    parser.add_argument("--json-out")
    args = parser.parse_args()

    mutation_selftest()
    state = args.state
    goldens = Path(args.goldens)
    java_a = load(goldens / (state + "_a.png"))
    java_b = load(goldens / (state + "_b.png"))
    native = load(Path(args.c_frame))
    if java_a.shape != java_b.shape or java_a.shape != native.shape:
        raise RuntimeError("%s frame dimensions differ" % state)
    mask = panel_mask(java_a.shape)
    java_ab = metrics(java_a, java_b, mask)
    native_result = metrics(java_a, native, mask)
    passed = (java_ab["different_px"] == 0
              and native_result["different_px"] == 0)
    report = {
        "rule": "stable_owned_panel_pixel_exact",
        "excluded": "18 alpha-zero source texels in the real jar panel",
        "panel_xyxy": list(PANEL),
        "state": state,
        "java_ab": java_ab,
        "native": native_result,
        "passed": passed,
    }
    print(
        "%s: compared=%d Java A/B=%d native=%d max=%d %s"
        % (state, native_result["compared_px"], java_ab["different_px"],
           native_result["different_px"], native_result["max_channel"],
           "PASS" if passed else "FAIL"))
    if args.json_out:
        path = Path(args.json_out)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if not passed:
        raise RuntimeError("%s panel differs" % state)
    print("PASS %s: every owned pixel exact vs real Java" % state)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print("FAIL standard container: %s" % error)
        raise SystemExit(1)
