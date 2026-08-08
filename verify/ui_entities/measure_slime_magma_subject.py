#!/usr/bin/env python3
"""Same-scene real-Java/native subject gate for Slime and Magma Cube."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = (
    "slime_size1", "slime_size2", "slime_size4", "slime_squish",
    "magma_size1", "magma_size2", "magma_size4", "magma_squish",
)
BACKGROUND = "entity_gallery_background"
ROI = (330, 100, 525, 310)
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25
# The Magma Cube body is channel-exact for every state except two isolated
# size-1 edge samples. Slime's translucent gel pass retains a bounded legacy
# fixed-function shading/texel tail, classified with pxdiff before blessing.
BUDGETS = {
    "slime_size1": {
        "minimum_java_owned": 2200, "minimum_native_owned": 2240,
        "hard_px": 45, "over_4_px": 256, "max_channel": 78,
    },
    "slime_size2": {
        "minimum_java_owned": 9900, "minimum_native_owned": 9900,
        "hard_px": 16, "over_4_px": 1796, "max_channel": 75,
    },
    "slime_size4": {
        "minimum_java_owned": 25800, "minimum_native_owned": 25700,
        "hard_px": 4, "over_4_px": 3504, "max_channel": 27,
    },
    "slime_squish": {
        "minimum_java_owned": 7000, "minimum_native_owned": 6900,
        "hard_px": 0, "over_4_px": 870, "max_channel": 18,
    },
    "magma_size1": {
        "minimum_java_owned": 2250, "minimum_native_owned": 2250,
        "hard_px": 2, "over_4_px": 2, "max_channel": 139,
    },
    "magma_size2": {
        "minimum_java_owned": 9900, "minimum_native_owned": 9900,
        "hard_px": 0, "over_4_px": 0, "max_channel": 1,
    },
    "magma_size4": {
        "minimum_java_owned": 31000, "minimum_native_owned": 31000,
        "hard_px": 0, "over_4_px": 0, "max_channel": 1,
    },
    "magma_squish": {
        "minimum_java_owned": 6000, "minimum_native_owned": 6000,
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
        raise ValueError("slime/magma frame dimensions disagree")
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
    java[180, 425] = (80, 90, 100)
    if measure(java, bg, java.copy(), bg)["hard_px"] != 0:
        raise RuntimeError("slime/magma exact control failed")
    mutated = java.copy()
    mutated[180, 425, 0] += HARD_THRESHOLD + 1
    if measure(java, bg, mutated, bg)["hard_px"] != 1:
        raise RuntimeError("slime/magma mutation was not detected")


def validate_render_pin(meta_root: Path, state: str) -> None:
    """Reject a stable but wrong oracle pose before evaluating pixels."""
    meta = json.loads((meta_root / (state + ".json")).read_text(
        encoding="utf-8"))
    expected_yaw = float(meta["entity"]["subject"]["yaw"])
    for name in ("pin_reply_a", "pin_reply_b"):
        reply = meta.get(name, {})
        if not reply.get("render_pin_armed"):
            raise RuntimeError(f"{state}: {name} did not arm the render pin")
        if float(reply.get("render_yaw_offset", -9999.0)) != expected_yaw:
            raise RuntimeError(
                f"{state}: {name} render yaw does not match subject yaw")
    for name in ("frame_a", "frame_b"):
        frame = meta.get(name, {})
        if not frame.get("render_pin") or frame.get("render_pin_count") != 1:
            raise RuntimeError(f"{state}: {name} did not render one pinned entity")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--debug-out", type=Path)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    java_bg_a = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java_bg_a, java_bg_b):
        raise RuntimeError("entity gallery background Java A/B is not exact")

    report = {
        "rule": "stable_same_scene_subject",
        "roi_xyxy": list(ROI),
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
    }
    failed = []
    for state in STATES:
        validate_render_pin(args.goldens / "meta", state)
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
            "slime/magma subject contract failed: " + ", ".join(failed))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL slime/magma: {error}")
        raise SystemExit(1)
