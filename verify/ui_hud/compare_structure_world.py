#!/usr/bin/env python3
"""Strict Java-vs-native gate for Structure Block in-world rendering."""
from __future__ import print_function

import argparse
import os

import numpy as np
from PIL import Image


STATES = {
    "save_air": {
        "min_owned": 7000, "min_iou": 0.977,
        "max_diff_pixels": 720, "max_mean_channel": 0.280,
    },
    "load_transform": {
        "min_owned": 2000, "min_iou": 0.980,
        "max_diff_pixels": 60, "max_mean_channel": 0.020,
    },
    "load_hidden": {
        "min_owned": 0, "min_iou": 1.0,
        "max_diff_pixels": 0, "max_mean_channel": 0.0,
    },
}
CHROMA = np.array([13, 17, 19], dtype=np.uint8)
JAVA_COLORS = {
    (0, 0, 0), (223, 223, 223),
    (223, 127, 127), (127, 223, 127), (127, 127, 223),
    (127, 127, 255), (255, 63, 63),
}
NATIVE_COLORS = JAVA_COLORS
ROI = (slice(70, 271), slice(250, 611))


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def palette_mask(image, colors):
    mask = np.zeros(image.shape[:2], dtype=bool)
    for color in colors:
        mask |= np.all(image == color, axis=2)
    roi_mask = np.zeros_like(mask)
    roi_mask[ROI] = True
    return mask & roi_mask


def check_arrays(name, on, on_2, native, limits):
    errors = []
    arrays = (on, on_2, native)
    if any(a.shape != (480, 854, 3) for a in arrays):
        return None, ["%s: expected three 854x480 RGB frames" % name]
    oracle_mask = palette_mask(on, JAVA_COLORS)
    oracle_mask_2 = palette_mask(on_2, JAVA_COLORS)
    if not np.array_equal(oracle_mask, oracle_mask_2):
        errors.append("%s: independent Java owned masks differ by %d pixels"
                      % (name, np.count_nonzero(oracle_mask != oracle_mask_2)))
    elif not np.array_equal(on[oracle_mask], on_2[oracle_mask]):
        errors.append("%s: independent Java line colors are not stable" % name)
    native_mask = np.any(native != CHROMA, axis=2)
    native_outside = native_mask.copy()
    native_outside[ROI] = False
    if np.any(native_outside):
        errors.append("%s: native geometry escaped the Structure-only ROI"
                      % name)
    native_colors = set(map(tuple, native[native_mask].tolist()))
    unexpected_native = native_colors - NATIVE_COLORS
    if unexpected_native:
        errors.append("%s: unexpected native line colors %r"
                      % (name, sorted(unexpected_native)[:8]))
    oracle = np.empty_like(on)
    oracle[:] = CHROMA
    oracle[oracle_mask] = on[oracle_mask]
    delta = np.abs(native.astype(np.int16) - oracle.astype(np.int16))
    diff_pixels = int(np.count_nonzero(np.any(delta, axis=2)))
    mean_channel = float(delta.mean())
    intersection = int(np.count_nonzero(oracle_mask & native_mask))
    union = int(np.count_nonzero(oracle_mask | native_mask))
    iou = float(intersection) / union if union else 1.0
    owned = int(np.count_nonzero(oracle_mask))
    native_owned = int(np.count_nonzero(native_mask))
    if owned < limits["min_owned"]:
        errors.append("%s: Java owned only %d pixels" % (name, owned))
    if name == "load_hidden" and (owned or native_owned):
        errors.append("load_hidden: hidden bounds emitted pixels")
    if iou + 1e-12 < limits["min_iou"]:
        errors.append("%s: mask IoU %.6f < %.6f"
                      % (name, iou, limits["min_iou"]))
    if diff_pixels > limits["max_diff_pixels"]:
        errors.append("%s: %d differing pixels > %d"
                      % (name, diff_pixels, limits["max_diff_pixels"]))
    if mean_channel > limits["max_mean_channel"] + 1e-12:
        errors.append("%s: mean %.6f/ch > %.6f/ch"
                      % (name, mean_channel, limits["max_mean_channel"]))
    metrics = {
        "owned": owned, "native_owned": native_owned, "iou": iou,
        "diff_pixels": diff_pixels, "mean_channel": mean_channel,
        "oracle": oracle,
    }
    return metrics, errors


def state_paths(oracle, candidate, name):
    return (
        os.path.join(oracle, "structure_world_%s_on.png" % name),
        os.path.join(oracle, "structure_world_%s_on_2.png" % name),
        os.path.join(candidate, "structure_world_%s.ppm" % name),
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--out")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    failures = []
    loaded = {}
    for name, limits in STATES.items():
        paths = state_paths(args.oracle, args.candidate, name)
        missing = [p for p in paths if not os.path.isfile(p)]
        if missing:
            failures.append("%s: missing %s" % (name, ", ".join(missing)))
            continue
        arrays = tuple(load_rgb(path) for path in paths)
        loaded[name] = arrays
        metrics, errors = check_arrays(name, *arrays, limits)
        failures.extend(errors)
        if metrics:
            print("%s owned=%d native=%d iou=%.6f diff=%d mean=%.6f/ch"
                  % (name, metrics["owned"], metrics["native_owned"],
                     metrics["iou"], metrics["diff_pixels"],
                     metrics["mean_channel"]))
            if args.out:
                Image.fromarray(metrics["oracle"]).save(os.path.join(
                    args.out, "structure_world_%s_oracle.png" % name))
    if args.selftest and not failures:
        on, on_2, native = loaded["load_transform"]
        bad_on_2 = on_2.copy()
        mask = palette_mask(on, JAVA_COLORS)
        y, x = np.argwhere(mask)[0]
        bad_on_2[y, x, 0] ^= 1
        _, caught = check_arrays(
            "load_transform", on, bad_on_2, native,
            STATES["load_transform"])
        if not caught:
            failures.append("selftest: failed to catch one-channel oracle drift")
        hidden = loaded["load_hidden"]
        bad_native = hidden[2].copy()
        bad_native[100, 100] = (0, 0, 0)
        _, caught = check_arrays(
            "load_hidden", hidden[0], hidden[1], bad_native,
            STATES["load_hidden"])
        if not caught:
            failures.append("selftest: failed to catch a hidden-bound pixel")
        if not failures:
            print("selftest: PASS (oracle drift and hidden pixel caught)")
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        raise SystemExit(1)
    print("structure world gate: PASS")


if __name__ == "__main__":
    main()
