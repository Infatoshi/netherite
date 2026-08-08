#!/usr/bin/env python3
"""Real-Java/native subject report and gate for Shulker Box TESR keyframes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from measure_chest_subject import (
    OWNED_THRESHOLD, ROI, candidate_path, measure, pixels,
)


STATES = (
    "shulker_box_white_up_closed",
    "shulker_box_white_up_open",
    "shulker_box_orange_down_open",
    "shulker_box_purple_north_half",
    "shulker_box_blue_south_open",
    "shulker_box_red_west_open",
    "shulker_box_black_east_open",
)
BACKGROUND = "shulker_box_background"


def mutation_selftest() -> None:
    background = np.zeros((480, 854, 3), dtype=np.int16)
    java = background.copy()
    java[200, 425] = (80, 90, 100)
    exact = measure(java, background, java.copy(), background)
    if exact["owned_union_px"] != 1 or exact["hard_px"] != 0:
        raise RuntimeError("Shulker Box subject exact control failed")
    mutated = java.copy()
    mutated[200, 425, 0] += 26
    if measure(java, background, mutated, background)["hard_px"] != 1:
        raise RuntimeError("Shulker Box subject mutation was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--diagnostic-dir", type=Path,
                        help="write owned-subject pairs for pxdiff")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--require", action="store_true",
                        help="apply the checked-in strict budgets")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    java_bg_a = pixels(args.goldens / (BACKGROUND + "_a.png"))
    java_bg_b = pixels(args.goldens / (BACKGROUND + "_b.png"))
    native_bg = pixels(candidate_path(args.c_frames, BACKGROUND))
    if not np.array_equal(java_bg_a, java_bg_b):
        raise RuntimeError("Shulker Box background Java A/B is not exact")

    report = {
        "rule": "stable_subject_zero_hard_px_bounded_fixed_function_tail",
        "states": {},
    }
    failed = []
    for state in STATES:
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        row = measure(
            java_a, java_bg_a,
            pixels(candidate_path(args.c_frames, state)), native_bg)
        if args.diagnostic_dir:
            native = pixels(candidate_path(args.c_frames, state))
            roi = np.zeros(java_a.shape[:2], dtype=bool)
            x0, y0, x1, y1 = ROI
            roi[y0:y1, x0:x1] = True
            owned = roi & (
                (np.max(np.abs(java_a - java_bg_a), axis=2)
                    > OWNED_THRESHOLD)
                | (np.max(np.abs(native - native_bg), axis=2)
                    > OWNED_THRESHOLD))
            java_subject = np.zeros_like(java_a, dtype=np.uint8)
            native_subject = np.zeros_like(native, dtype=np.uint8)
            java_subject[owned] = java_a[owned].astype(np.uint8)
            native_subject[owned] = native[owned].astype(np.uint8)
            args.diagnostic_dir.mkdir(parents=True, exist_ok=True)
            Image.fromarray(java_subject, "RGB").save(
                args.diagnostic_dir / (state + "_java.png"))
            Image.fromarray(native_subject, "RGB").save(
                args.diagnostic_dir / (state + "_native.png"))
        report["states"][state] = row
        print(
            f"{state}: owned={row['owned_union_px']} "
            f">4={row['over_4_px']} hard={row['hard_px']} "
            f"max={row['max_channel']} bbox={row['bbox']}")

    # Reviewed fixed-function tail from the first clean Java A/B exact capture.
    # Every residual above four channels is an isolated pxdiff-measured
    # shading-offset, never missing content or registration.
    budgets = {
        "shulker_box_white_up_closed": {
            "minimum_owned": 5000, "over_4_px": 3,
            "hard_px": 2, "max_channel": 34,
            "hard_locations_yx": [[234, 385], [234, 468]],
        },
        "shulker_box_white_up_open": {
            "minimum_owned": 7900, "over_4_px": 1,
            "hard_px": 0, "max_channel": 23,
        },
        "shulker_box_orange_down_open": {
            "minimum_owned": 6600, "over_4_px": 0,
            "hard_px": 0, "max_channel": 1,
        },
        "shulker_box_purple_north_half": {
            "minimum_owned": 10800, "over_4_px": 0,
            "hard_px": 0, "max_channel": 3,
        },
        "shulker_box_blue_south_open": {
            "minimum_owned": 10000, "over_4_px": 2,
            "hard_px": 0, "max_channel": 13,
        },
        "shulker_box_red_west_open": {
            "minimum_owned": 11600, "over_4_px": 3,
            "hard_px": 1, "max_channel": 42,
            "hard_locations_yx": [[238, 521]],
        },
        "shulker_box_black_east_open": {
            "minimum_owned": 11600, "over_4_px": 0,
            "hard_px": 0, "max_channel": 3,
        },
    }
    if args.require:
        for state in STATES:
            budget = budgets.get(state)
            if budget is None:
                failed.append(state + ":unreviewed")
                continue
            row = report["states"][state]
            row["budget"] = budget
            row["pass"] = (
                row["java_owned_px"] >= budget["minimum_owned"]
                and row["owned_union_px"] >= budget["minimum_owned"]
                and row["native_owned_px"]
                    >= int(row["java_owned_px"] * 0.95)
                and row["hard_px"] == budget["hard_px"]
                and row["hard_locations_yx"]
                    == budget.get("hard_locations_yx", [])
                and row["over_4_px"] <= budget["over_4_px"]
                and row["max_channel"] <= budget["max_channel"])
            if not row["pass"]:
                failed.append(state)

    report["pass"] = not failed
    report["failed"] = failed
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if failed:
        raise RuntimeError(
            "Shulker Box TESR subject contract failed: "
            + ", ".join(failed))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL Shulker Box TESR: {error}")
        raise SystemExit(1)
