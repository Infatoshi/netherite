#!/usr/bin/env python3
"""Strict real-Java pixel gate for the Ender Chest inventory panel."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


# GuiChest at 854x480, GUI scale 2. The six-pixel corner cutouts expose the
# live world and are therefore not owned by this focused panel gate.
PANEL = (250, 74, 602, 406)


def load(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = PANEL
    mask[y0:y1, x0:x1] = True
    for x, y in ((x0, y0), (x1 - 6, y0),
                 (x0, y1 - 6), (x1 - 6, y1 - 6)):
        mask[y:y + 6, x:x + 6] = False
    return mask


def metrics(first: np.ndarray, second: np.ndarray,
            mask: np.ndarray) -> dict[str, int | float | list[list[int]]]:
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
    candidate[100, 300, 0] = 1
    result = metrics(baseline, candidate, panel_mask(baseline.shape))
    if result["different_px"] != 1 or result["max_channel"] != 1:
        raise RuntimeError("Ender Chest GUI negative control was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", default="verify/ui_hud/goldens")
    parser.add_argument("--c-frame", required=True)
    parser.add_argument("--json-out")
    args = parser.parse_args()

    mutation_selftest()
    goldens = Path(args.goldens)
    java_a = load(goldens / "gui_ender_chest_a.png")
    java_b = load(goldens / "gui_ender_chest_b.png")
    native = load(Path(args.c_frame))
    if java_a.shape != java_b.shape or java_a.shape != native.shape:
        raise RuntimeError("Ender Chest GUI frame dimensions differ")

    mask = panel_mask(java_a.shape)
    java_ab = metrics(java_a, java_b, mask)
    native_result = metrics(java_a, native, mask)
    report = {
        "rule": "stable_owned_panel_pixel_exact",
        "panel_xyxy": list(PANEL),
        "excluded": "four 6x6 transparent world-background corners",
        "java_ab": java_ab,
        "native": native_result,
    }
    if args.json_out:
        path = Path(args.json_out)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")

    stable = java_ab["different_px"] == 0
    exact = native_result["different_px"] == 0
    print(
        "gui_ender_chest: "
        f"compared={native_result['compared_px']} "
        f"Java A/B={java_ab['different_px']} "
        f"native={native_result['different_px']} "
        f"max={native_result['max_channel']} "
        f"{'PASS' if stable and exact else 'FAIL'}")
    if not stable:
        raise RuntimeError("real-Java Ender Chest GUI capture is unstable")
    if not exact:
        raise RuntimeError("native Ender Chest GUI panel differs from Java")
    print("PASS Ender Chest GUI: 116720 owned pixels exact vs real Java")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Ender Chest GUI: {error}")
        raise SystemExit(1)
