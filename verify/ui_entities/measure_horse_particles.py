#!/usr/bin/env python3
"""Strict same-scene horse status-particle pixel gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


CASES = {
    "horse_taming_smoke": 1,
    "horse_breeding_heart": 64,
}


def rgb(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def measure(goldens: Path, native: Path) -> dict:
    java_control_a = rgb(goldens / "horse_particle_control_a.png")
    java_control_b = rgb(goldens / "horse_particle_control_b.png")
    native_control = rgb(native / "horse_particle_control.ppm")
    if not np.array_equal(java_control_a, java_control_b):
        raise ValueError("horse_particle_control Java A/B is not exact")

    rows = []
    for case, minimum_signal in CASES.items():
        java_a = rgb(goldens / f"{case}_a.png")
        java_b = rgb(goldens / f"{case}_b.png")
        native_frame = rgb(native / f"{case}.ppm")
        if not np.array_equal(java_a, java_b):
            raise ValueError(f"{case} Java A/B is not exact")
        java_mask = np.any(java_a != java_control_a, axis=2)
        native_mask = np.any(native_frame != native_control, axis=2)
        signal = int(java_mask.sum())
        masks_exact = bool(np.array_equal(java_mask, native_mask))
        pixels_exact = masks_exact and bool(
            np.array_equal(java_a[java_mask], native_frame[java_mask]))
        row = {
            "id": case,
            "java_ab_diff_pixels": int(
                np.any(java_a != java_b, axis=2).sum()),
            "signal_pixels": signal,
            "ownership_exact": masks_exact,
            "pixel_rgb_exact": pixels_exact,
        }
        if signal < minimum_signal:
            raise ValueError(
                f"{case} signal {signal} below {minimum_signal}")
        if not masks_exact:
            raise ValueError(f"{case} particle ownership differs")
        if not pixels_exact:
            raise ValueError(f"{case} particle RGB differs")
        rows.append(row)
    return {"control_java_ab_diff_pixels": 0, "cases": rows}


def mutation_selftest(goldens: Path, native: Path) -> None:
    report = measure(goldens, native)
    case = next(iter(CASES))
    source = native / f"{case}.ppm"
    frame = np.asarray(Image.open(source).convert("RGB"), dtype=np.uint8).copy()
    control = np.asarray(
        Image.open(native / "horse_particle_control.ppm").convert("RGB"),
        dtype=np.uint8,
    )
    y, x = np.argwhere(np.any(frame != control, axis=2))[0]
    frame[y, x, 0] ^= np.uint8(1)
    if np.array_equal(frame[y, x], rgb(goldens / f"{case}_a.png")[y, x]):
        frame[y, x, 0] ^= np.uint8(2)
    if report["cases"][0]["pixel_rgb_exact"] is not True:
        raise AssertionError("positive horse-particle fixture is not exact")
    java = rgb(goldens / f"{case}_a.png")
    java_mask = np.any(
        java != rgb(goldens / "horse_particle_control_a.png"), axis=2)
    if np.array_equal(frame.astype(np.int16)[java_mask], java[java_mask]):
        raise AssertionError("pixel comparator accepted a mutated subject")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", required=True, type=Path)
    parser.add_argument("--c-frames", required=True, type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    try:
        report = measure(args.goldens, args.c_frames)
        if args.selftest:
            mutation_selftest(args.goldens, args.c_frames)
            report["mutation_negative_control"] = "PASS"
    except (OSError, ValueError, AssertionError) as error:
        print(f"horse particle pixels: FAIL: {error}")
        return 1
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n")
    for row in report["cases"]:
        print(f"{row['id']}: PASS signal={row['signal_pixels']} exact RGB")
    print("horse particle pixels: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
