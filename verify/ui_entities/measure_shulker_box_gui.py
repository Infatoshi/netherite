#!/usr/bin/env python3
"""Strict real-Java pixel gate for the Shulker Box inventory panel."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


# GuiShulkerBox at 854x480, GUI scale 2. ySize is 167, so GuiContainer
# centers at scaled y=36 -> framebuffer y=72. Every alpha-zero texel in the
# real 1.11.2 shulker_box.png exposes the live world and is not owned by this
# focused panel gate. These spans are the exact 176x167 source alpha mask.
PANEL = (250, 72, 602, 406)
SCALE = 2
TRANSPARENT_SPANS = {
    0: ((0, 1), (173, 175)),
    1: ((0, 0), (174, 175)),
    2: ((175, 175),),
    163: ((0, 0),),
    164: ((0, 1), (175, 175)),
    165: ((0, 2), (174, 175)),
    166: ((0, 175),),
}


def load(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(shape: tuple[int, ...]) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = PANEL
    mask[y0:y1, x0:x1] = True
    for source_y, spans in TRANSPARENT_SPANS.items():
        fy = y0 + source_y * SCALE
        for source_x0, source_x1 in spans:
            fx0 = x0 + source_x0 * SCALE
            fx1 = x0 + (source_x1 + 1) * SCALE
            mask[fy:fy + SCALE, fx0:fx1] = False
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
        raise RuntimeError("Shulker Box GUI negative control was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", default="verify/ui_hud/goldens")
    parser.add_argument("--c-frame", required=True)
    parser.add_argument("--json-out")
    args = parser.parse_args()

    mutation_selftest()
    goldens = Path(args.goldens)
    java_a = load(goldens / "gui_shulker_box_a.png")
    java_b = load(goldens / "gui_shulker_box_b.png")
    native = load(Path(args.c_frame))
    if java_a.shape != java_b.shape or java_a.shape != native.shape:
        raise RuntimeError("Shulker Box GUI frame dimensions differ")

    mask = panel_mask(java_a.shape)
    java_ab = metrics(java_a, java_b, mask)
    native_result = metrics(java_a, native, mask)
    report = {
        "rule": "stable_owned_panel_pixel_exact",
        "panel_xyxy": list(PANEL),
        "excluded": "all 194 alpha-zero source texels exposing the world",
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
        "gui_shulker_box: "
        f"compared={native_result['compared_px']} "
        f"Java A/B={java_ab['different_px']} "
        f"native={native_result['different_px']} "
        f"max={native_result['max_channel']} "
        f"{'PASS' if stable and exact else 'FAIL'}")
    if not stable:
        raise RuntimeError("real-Java Shulker Box GUI capture is unstable")
    if not exact:
        raise RuntimeError("native Shulker Box GUI panel differs from Java")
    print("PASS Shulker Box GUI: 116792 owned pixels exact vs real Java")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Shulker Box GUI: {error}")
        raise SystemExit(1)
