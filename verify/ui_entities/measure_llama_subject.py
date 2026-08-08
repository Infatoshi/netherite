#!/usr/bin/env python3
"""Measure llama-family pixels using same-renderer background ownership."""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


COATS = (
    "llama_creamy_idle", "llama_white_idle",
    "llama_brown_idle", "llama_gray_idle",
)
DECOR = tuple("llama_decor_" + name for name in (
    "white", "orange", "magenta", "light_blue",
    "yellow", "lime", "pink", "gray", "silver", "cyan", "purple",
    "blue", "brown", "green", "red", "black",
))
STATES = COATS + DECOR + (
    "llama_gait", "llama_gray_decor_chest", "llama_child_decor",
    "llama_spit",
)
OWNERSHIP_THRESHOLD = 25
DIAGNOSTIC_THRESHOLDS = (0, 1, 2, 4, 8, 16, 25)
FIXED_FUNCTION_HARD_BUDGET = {
    "llama_creamy_idle": 3,
    "llama_white_idle": 2,
    "llama_brown_idle": 3,
    "llama_gray_idle": 3,
    "llama_decor_white": 4,
    "llama_decor_orange": 4,
    "llama_decor_magenta": 4,
    "llama_decor_light_blue": 4,
    "llama_decor_yellow": 4,
    "llama_decor_lime": 3,
    "llama_decor_pink": 4,
    "llama_decor_gray": 4,
    "llama_decor_silver": 4,
    "llama_decor_cyan": 4,
    "llama_decor_purple": 4,
    "llama_decor_blue": 4,
    "llama_decor_brown": 4,
    "llama_decor_green": 4,
    "llama_decor_red": 4,
    "llama_decor_black": 4,
    "llama_gait": 2,
    "llama_gray_decor_chest": 4,
    "llama_child_decor": 1,
    "llama_spit": 0,
}
FIXED_FUNCTION_NATIVE_BUDGET = {
    "llama_creamy_idle": 11814,
    "llama_white_idle": 11339,
    "llama_brown_idle": 7837,
    "llama_gray_idle": 10379,
    "llama_decor_white": 11201,
    "llama_decor_orange": 10607,
    "llama_decor_magenta": 11228,
    "llama_decor_light_blue": 12218,
    "llama_decor_yellow": 11794,
    "llama_decor_lime": 10818,
    "llama_decor_pink": 11953,
    "llama_decor_gray": 10232,
    "llama_decor_silver": 11205,
    "llama_decor_cyan": 10665,
    "llama_decor_purple": 10345,
    "llama_decor_blue": 11401,
    "llama_decor_brown": 10988,
    "llama_decor_green": 10186,
    "llama_decor_red": 11016,
    "llama_decor_black": 11253,
    "llama_gait": 7506,
    "llama_gray_decor_chest": 9808,
    "llama_child_decor": 2804,
    "llama_spit": 340,
}


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", required=True)
    parser.add_argument("--c-frames", required=True)
    parser.add_argument("--max-native-px", type=int, default=0)
    parser.add_argument("--max-hard-px", type=int, default=0)
    parser.add_argument("--bounded-fixed-function", action="store_true")
    parser.add_argument("--json-out")
    parser.add_argument("--diagnostic-dir")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    goldens = Path(args.goldens)
    c_frames = Path(args.c_frames)
    java_bg_a = load(goldens / "llama_background_a.png")
    java_bg_b = load(goldens / "llama_background_b.png")
    native_bg = load(c_frames / "llama_background.ppm")
    if java_bg_a.shape != java_bg_b.shape or java_bg_a.shape != native_bg.shape:
        raise SystemExit("FAIL: llama background dimensions differ")
    bg_ab = np.max(np.abs(java_bg_a - java_bg_b), axis=2)
    report = {
        "rule": "same_renderer_background_owned_pixels",
        "ownership_threshold": OWNERSHIP_THRESHOLD,
        "max_native_px": args.max_native_px,
        "max_hard_px": args.max_hard_px,
        "bounded_fixed_function": args.bounded_fixed_function,
        "background_java_ab_px": int(np.count_nonzero(bg_ab)),
        "background_java_ab_max": int(bg_ab.max()),
        "states": {},
    }
    failed = report["background_java_ab_px"] != 0
    mutated_rejected = False
    for state in STATES:
        java_a = load(goldens / (state + "_a.png"))
        java_b = load(goldens / (state + "_b.png"))
        native = load(c_frames / (state + ".ppm"))
        if java_a.shape != java_bg_a.shape or java_b.shape != java_bg_a.shape \
                or native.shape != java_bg_a.shape:
            raise SystemExit("FAIL: %s dimensions differ" % state)

        java_owned = np.max(np.abs(java_a - java_bg_a), axis=2) \
            > OWNERSHIP_THRESHOLD
        java_b_owned = np.max(np.abs(java_b - java_bg_b), axis=2) \
            > OWNERSHIP_THRESHOLD
        native_owned = np.max(np.abs(native - native_bg), axis=2) \
            > OWNERSHIP_THRESHOLD
        owned = java_owned | java_b_owned | native_owned
        java_ab = np.max(np.abs(java_a - java_b), axis=2)
        native_diff = np.max(np.abs(java_a - native), axis=2)
        java_ab[~owned] = 0
        native_diff[~owned] = 0
        hard = native_diff > OWNERSHIP_THRESHOLD
        entry = {
            "owned_px": int(np.count_nonzero(owned)),
            "java_owned_px": int(np.count_nonzero(java_owned)),
            "native_owned_px": int(np.count_nonzero(native_owned)),
            "ownership_xor_px": int(np.count_nonzero(java_owned ^ native_owned)),
            "java_ab_px": int(np.count_nonzero(java_ab)),
            "java_ab_max": int(java_ab.max()),
            "native_px": int(np.count_nonzero(native_diff)),
            "native_hard_px": int(np.count_nonzero(hard)),
            "native_max": int(native_diff.max()),
            "native_px_by_threshold": {
                str(t): int(np.count_nonzero(native_diff > t))
                for t in DIAGNOSTIC_THRESHOLDS
            },
            "native_hard_locations_yx": np.argwhere(hard)[:64]
                .astype(int).tolist(),
        }
        report["states"][state] = entry
        native_budget = FIXED_FUNCTION_NATIVE_BUDGET[state] \
            if args.bounded_fixed_function else args.max_native_px
        hard_budget = FIXED_FUNCTION_HARD_BUDGET[state] \
            if args.bounded_fixed_function else args.max_hard_px
        entry["native_budget_px"] = native_budget
        entry["native_hard_budget_px"] = hard_budget
        print("%s: owned=%d xor=%d Java A/B=%d native=%d hard=%d max=%d" % (
            state, entry["owned_px"], entry["ownership_xor_px"],
            entry["java_ab_px"], entry["native_px"],
            entry["native_hard_px"], entry["native_max"]))
        if entry["owned_px"] == 0 or entry["java_ab_px"] != 0 \
                or entry["native_px"] > native_budget \
                or entry["native_hard_px"] > hard_budget:
            failed = True

        if args.diagnostic_dir:
            diagnostic = Path(args.diagnostic_dir)
            diagnostic.mkdir(parents=True, exist_ok=True)
            java_diag = java_bg_a.copy()
            native_diag = java_bg_a.copy()
            java_diag[owned] = java_a[owned]
            native_diag[owned] = native[owned]
            Image.fromarray(java_diag.astype(np.uint8), "RGB").save(
                diagnostic / (state + "_java.png"))
            Image.fromarray(native_diag.astype(np.uint8), "RGB").save(
                diagnostic / (state + "_native.png"))
            Image.fromarray((owned.astype(np.uint8) * 255), "L").save(
                diagnostic / (state + "_mask.png"))

        if args.selftest and state == "llama_creamy_idle":
            points = np.argwhere(owned & ~hard)
            if not len(points):
                raise SystemExit("FAIL: llama mutation has no soft owned pixel")
            y, x = points[0]
            mutated = native_diff.copy()
            mutated[y, x] = max(OWNERSHIP_THRESHOLD + 1,
                                int(mutated[y, x]))
            mutated_rejected = int(np.count_nonzero(
                mutated > OWNERSHIP_THRESHOLD)) > hard_budget

    if args.selftest:
        if not mutated_rejected:
            raise SystemExit("FAIL: llama hard-pixel mutation was accepted")
        report["negative_control"] = "PASS"
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise SystemExit("FAIL: llama subject contract")
    print("PASS: llama subject contract")


if __name__ == "__main__":
    main()
