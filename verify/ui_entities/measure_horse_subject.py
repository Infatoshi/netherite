#!/usr/bin/env python3
"""Strict horse-family pixels with same-renderer background ownership.

The paired background decides which pixels belong to the horse without making
the two independently rendered terrain colors part of the subject contract.
Horse bodies are opaque, so compare their composed pixels directly.  Comparing
``entity - background`` across renderers is invalid at occluded pixels: it
imports the otherwise irrelevant Java/native terrain difference underneath
the horse.
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = (
    "horse_marked_armor", "horse_iron_idle", "horse_saddled_idle",
    "horse_eating", "horse_rearing", "horse_mouth", "horse_gait",
    "horse_tail", "horse_saddled_pose", "horse_child",
    "donkey_chested_saddled", "mule_base", "skeleton_horse",
    "zombie_horse", "skeleton_trap_rider", "skeleton_trap_group",
)
OWNERSHIP_THRESHOLD = 25
DIAGNOSTIC_THRESHOLDS = (0, 1, 2, 4, 8, 16, 25)
# Mesa's fixed-function entity path and the software rasterizer still disagree
# at a finite set of one-sample model/alpha boundaries.  These ceilings are a
# regression lock, not an exact-pixel completion claim.  Keep the ordinary
# --max-native-px=0 mode for callers that require exact RGB.
FIXED_FUNCTION_HARD_BUDGET = {
    "horse_marked_armor": 1,
    "horse_iron_idle": 1,
    "horse_saddled_idle": 1,
    "horse_eating": 0,
    "horse_rearing": 0,
    "horse_mouth": 1,
    "horse_gait": 0,
    "horse_tail": 1,
    "horse_saddled_pose": 7,
    "horse_child": 0,
    "donkey_chested_saddled": 42,
    "mule_base": 1,
    "skeleton_horse": 0,
    "zombie_horse": 1,
    "skeleton_trap_rider": 77,
    "skeleton_trap_group": 2989,
}
# A four-mount lineup naturally owns more pixels than any isolated case.  Keep
# its complete differing-pixel ceiling separate while retaining the shared
# 5,000-pixel ceiling for every single-entity fixture.
FIXED_FUNCTION_NATIVE_BUDGET = {
    "skeleton_trap_group": 9885,
}


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", default="verify/ui_entities/goldens")
    parser.add_argument("--c-frames", required=True)
    parser.add_argument("--max-native-px", type=int, default=0)
    parser.add_argument("--bounded-fixed-function", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--json-out")
    parser.add_argument("--diagnostic-dir")
    args = parser.parse_args()

    goldens = Path(args.goldens)
    c_frames = Path(args.c_frames)
    java_bg_a = load(goldens / "baselines/horse_background_a.png")
    java_bg_b = load(goldens / "baselines/horse_background_b.png")
    native_bg = load(c_frames / "horse_background.ppm")
    if java_bg_a.shape != java_bg_b.shape or java_bg_a.shape != native_bg.shape:
        raise SystemExit("FAIL: horse background dimensions differ")
    bg_ab = np.max(np.abs(java_bg_a - java_bg_b), axis=2)
    report = {
        "rule": "exact_same_renderer_background_differential",
        "max_native_px": args.max_native_px,
        "bounded_fixed_function": args.bounded_fixed_function,
        "background_java_ab_px": int(np.count_nonzero(bg_ab)),
        "background_java_ab_max": int(bg_ab.max()),
        "states": {},
    }
    failed = report["background_java_ab_px"] != 0
    for state in STATES:
        if state == "skeleton_trap_group":
            state_java_bg_a = load(
                goldens / "skeleton_trap_group_background_a.png")
            state_java_bg_b = load(
                goldens / "skeleton_trap_group_background_b.png")
            state_native_bg = load(
                c_frames / "skeleton_trap_group_background.ppm")
        else:
            state_java_bg_a = java_bg_a
            state_java_bg_b = java_bg_b
            state_native_bg = native_bg
        java_a = load(goldens / (state + "_a.png"))
        java_b = load(goldens / (state + "_b.png"))
        native = load(c_frames / (state + ".ppm"))
        if java_a.shape != state_java_bg_a.shape \
                or java_b.shape != state_java_bg_a.shape \
                or native.shape != state_java_bg_a.shape:
            raise SystemExit("FAIL: %s dimensions differ" % state)
        delta_a = java_a - state_java_bg_a
        delta_b = java_b - state_java_bg_b
        delta_native = native - state_native_bg
        java_owned_any = np.max(np.abs(delta_a), axis=2) > 0
        java_b_owned_any = np.max(np.abs(delta_b), axis=2) > 0
        native_owned_any = np.max(np.abs(delta_native), axis=2) > 0
        java_owned = np.max(np.abs(delta_a), axis=2) > OWNERSHIP_THRESHOLD
        java_b_owned = np.max(np.abs(delta_b), axis=2) > OWNERSHIP_THRESHOLD
        native_owned = (np.max(np.abs(delta_native), axis=2)
                        > OWNERSHIP_THRESHOLD)
        owned = java_owned | java_b_owned | native_owned
        owned_any = java_owned_any | java_b_owned_any | native_owned_any
        common_any = java_owned_any & native_owned_any
        java_ab = np.max(np.abs(java_a - java_b), axis=2)
        native_diff = np.max(np.abs(java_a - native), axis=2)
        java_ab[~owned] = 0
        native_diff[~owned] = 0
        native_points = np.argwhere(native_diff > 0)
        hard_points = np.argwhere(native_diff > OWNERSHIP_THRESHOLD)
        entry = {
            "owned_px": int(np.count_nonzero(owned)),
            "java_owned_px": int(np.count_nonzero(java_owned)),
            "native_owned_px": int(np.count_nonzero(native_owned)),
            "ownership_xor_px": int(np.count_nonzero(java_owned
                                                       ^ native_owned)),
            "ownership_any_px": int(np.count_nonzero(owned_any)),
            "ownership_any_xor_px": int(np.count_nonzero(java_owned_any
                                                           ^ native_owned_any)),
            "java_ab_px": int(np.count_nonzero(java_ab)),
            "java_ab_max": int(java_ab.max()),
            "native_px": int(native_points.shape[0]),
            "native_max": int(native_diff.max()),
            "native_locations_yx": native_points[:16].astype(int).tolist(),
            "native_hard_locations_yx": hard_points[:64].astype(int).tolist(),
            "native_px_by_threshold": {
                str(t): int(np.count_nonzero(native_diff > t))
                for t in DIAGNOSTIC_THRESHOLDS
            },
            "native_common_px_by_threshold": {
                str(t): int(np.count_nonzero((native_diff > t) & common_any))
                for t in DIAGNOSTIC_THRESHOLDS
            },
        }
        native_budget = (FIXED_FUNCTION_NATIVE_BUDGET.get(
            state, args.max_native_px) if args.bounded_fixed_function
                         else args.max_native_px)
        entry["native_budget_px"] = native_budget
        report["states"][state] = entry
        if args.diagnostic_dir:
            diagnostic_dir = Path(args.diagnostic_dir)
            diagnostic_dir.mkdir(parents=True, exist_ok=True)
            common = java_owned & native_owned
            java_cut = np.zeros_like(java_a, dtype=np.uint8)
            native_cut = np.zeros_like(native, dtype=np.uint8)
            java_cut[common] = np.clip(java_a[common], 0, 255).astype(np.uint8)
            native_cut[common] = np.clip(native[common], 0, 255).astype(np.uint8)
            Image.fromarray(java_cut, "RGB").save(
                diagnostic_dir / (state + "_java.png"))
            Image.fromarray(native_cut, "RGB").save(
                diagnostic_dir / (state + "_native.png"))
            Image.fromarray((java_owned.astype(np.uint8) * 255), "L").save(
                diagnostic_dir / (state + "_java_mask.png"))
            Image.fromarray((native_owned.astype(np.uint8) * 255), "L").save(
                diagnostic_dir / (state + "_native_mask.png"))
        print("%s: owned=%d xor=%d/%d Java A/B=%d native=%d max=%d "
              "hist=%s common=%s" % (
            state, entry["owned_px"], entry["ownership_xor_px"],
            entry["ownership_any_xor_px"], entry["java_ab_px"],
            entry["native_px"],
            entry["native_max"], entry["native_px_by_threshold"],
            entry["native_common_px_by_threshold"]))
        if entry["owned_px"] == 0 or entry["java_ab_px"] != 0 \
                or entry["native_px"] > native_budget:
            failed = True
        if args.bounded_fixed_function:
            hard = entry["native_px_by_threshold"][str(OWNERSHIP_THRESHOLD)]
            if hard > FIXED_FUNCTION_HARD_BUDGET[state]:
                failed = True
        if args.selftest and state in ("horse_child", "skeleton_trap_group"):
            ys, xs = np.where(java_owned)
            if ys.size == 0:
                raise SystemExit("FAIL: horse subject mutation has no target")
            soft = np.where(java_owned & (native_diff <= OWNERSHIP_THRESHOLD))
            if soft[0].size == 0:
                raise SystemExit("FAIL: horse subject mutation has no soft target")
            y, x = int(soft[0][0]), int(soft[1][0])
            mutated = native_diff.copy()
            mutated[y, x] = OWNERSHIP_THRESHOLD + 1
            mutated_hard = int(np.count_nonzero(
                mutated > OWNERSHIP_THRESHOLD))
            if mutated_hard <= FIXED_FUNCTION_HARD_BUDGET[state]:
                raise SystemExit("FAIL: horse subject mutation was accepted")
            report["negative_control"] = "PASS"
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise SystemExit("FAIL: horse subject contract")
    print("PASS: horse subject contract")


if __name__ == "__main__":
    main()
