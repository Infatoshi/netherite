#!/usr/bin/env python3
"""Strict same-scene Squid model gate for swimming and dry-fall poses."""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = ("squid_swim_pose", "squid_dry_pose")
OWNERSHIP_THRESHOLD = 25
# The swimming fixture has one measured internal tentacle intersection where
# Java's fixed-function depth path selects the dark face and the software
# 24-bit path selects the adjacent light face. The coordinate is A/B stable;
# the dry fixture separates those faces and has no hard residual.
HARD_BUDGET = {"squid_swim_pose": 1, "squid_dry_pose": 0}


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", required=True)
    parser.add_argument("--c-frames", required=True)
    parser.add_argument("--json-out")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    goldens = Path(args.goldens)
    c_frames = Path(args.c_frames)
    java_bg_a = load(goldens / "squid_background_a.png")
    java_bg_b = load(goldens / "squid_background_b.png")
    native_bg = load(c_frames / "squid_background.ppm")
    if not (java_bg_a.shape == java_bg_b.shape == native_bg.shape):
        raise SystemExit("FAIL: Squid background dimensions differ")

    background_ab = np.max(np.abs(java_bg_a - java_bg_b), axis=2)
    report = {
        "rule": "same_scene_union_owned_max_channel_25_bounded",
        "ownership_threshold": OWNERSHIP_THRESHOLD,
        "background_java_ab_px": int(np.count_nonzero(background_ab)),
        "background_java_ab_max": int(background_ab.max()),
        "states": {},
    }
    failed = report["background_java_ab_px"] != 0
    mutation_rejected = False

    for state in STATES:
        java_a = load(goldens / (state + "_a.png"))
        java_b = load(goldens / (state + "_b.png"))
        native = load(c_frames / (state + ".ppm"))
        if not (java_a.shape == java_b.shape == java_bg_a.shape
                == native.shape):
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
            "ownership_xor_px": int(np.count_nonzero(java_owned
                                                       ^ native_owned)),
            "java_ab_px": int(np.count_nonzero(java_ab)),
            "java_ab_max": int(java_ab.max()),
            "native_px": int(np.count_nonzero(native_diff)),
            "native_hard_px": int(np.count_nonzero(hard)),
            "native_hard_budget": HARD_BUDGET[state],
            "native_max": int(native_diff.max()),
            "native_px_by_threshold": {
                str(value): int(np.count_nonzero(native_diff > value))
                for value in (0, 1, 2, 4, 8, 16, 25)
            },
            "native_hard_locations_yx": np.argwhere(hard)[:64]
                .astype(int).tolist(),
        }
        report["states"][state] = entry
        print("%s: owned=%d xor=%d Java A/B=%d native=%d hard=%d/%d max=%d" % (
            state, entry["owned_px"], entry["ownership_xor_px"],
            entry["java_ab_px"], entry["native_px"],
            entry["native_hard_px"], entry["native_hard_budget"],
            entry["native_max"]))
        if (entry["owned_px"] == 0 or entry["java_ab_px"] != 0
                or entry["native_hard_px"] > entry["native_hard_budget"]):
            failed = True

        if args.selftest and state == "squid_swim_pose":
            points = np.argwhere(owned)
            if not len(points):
                raise SystemExit("FAIL: Squid mutation has no owned pixel")
            y, x = points[0]
            mutated = native_diff.copy()
            mutated[y, x] = OWNERSHIP_THRESHOLD + 1
            mutation_rejected = np.count_nonzero(
                mutated > OWNERSHIP_THRESHOLD) > HARD_BUDGET[state]

    if args.selftest:
        if not mutation_rejected:
            raise SystemExit("FAIL: Squid hard-pixel mutation was accepted")
        report["negative_control"] = "PASS"
    if args.json_out:
        output = Path(args.json_out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise SystemExit("FAIL: Squid subject contract")
    print("PASS: Squid subject contract")


if __name__ == "__main__":
    main()
