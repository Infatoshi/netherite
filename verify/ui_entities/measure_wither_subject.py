#!/usr/bin/env python3
"""Measure the pinned Wither render after removing the same-scene background.

The Wither changes the scene fog even when its model is outside the camera, so
raw full-frame Java-vs-native pixels conflate model pixels with the two world
renderers.  The paired ``wither_background`` capture keeps that fog state and
moves only the model behind the camera, so its deltas define subject ownership.
Normal and invulnerable bodies use their composed pixels as the contract.  The
transparent armored aura uses armored-minus-normal within each renderer; this
isolates the layer without importing the two world renderers' background
residual.  Direct armored composed-pixel differences remain a diagnostic.
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


STATES = ("wither_normal", "wither_invul", "wither_armored")
THRESH = 25
# The HUD crosshair is not part of the entity renderer and differs between the
# Java GL and software-composed world frames.
CROSSHAIR = (416, 230, 438, 251)


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def load_ppm(path):
    return load_rgb(path)


def delta(frame, background):
    return frame.astype(np.int16) - background.astype(np.int16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", default="verify/ui_entities/goldens")
    ap.add_argument("--c-frames", required=True)
    ap.add_argument("--states", default=",".join(STATES))
    ap.add_argument("--max-native-px", type=int, default=0)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    goldens = Path(args.goldens)
    c_frames = Path(args.c_frames)
    ga_bg = load_rgb(goldens / "baselines/wither_background_a.png")
    gb_bg = load_rgb(goldens / "baselines/wither_background_b.png")
    c_bg = load_ppm(c_frames / "wither_background.ppm")
    if ga_bg.shape != gb_bg.shape or ga_bg.shape != c_bg.shape:
        raise SystemExit("FAIL: Wither background dimensions differ")

    bg_ab = np.max(np.abs(ga_bg - gb_bg), axis=2)
    report = {"threshold": THRESH, "crosshair_exclusive": list(CROSSHAIR),
              "max_native_px": args.max_native_px,
              "background_java_ab_px": int(np.count_nonzero(bg_ab > THRESH)),
              "background_java_ab_max": int(bg_ab.max()), "states": {}}
    failed = False
    if report["background_java_ab_px"]:
        failed = True
    x0, y0, x1, y1 = CROSSHAIR
    states = tuple(s for s in args.states.split(",") if s)
    unknown = sorted(set(states) - set(STATES))
    if unknown:
        raise SystemExit("FAIL: unknown Wither states: " + ",".join(unknown))
    ga_normal = load_rgb(goldens / "wither_normal_a.png")
    gb_normal = load_rgb(goldens / "wither_normal_b.png")
    c_normal = load_ppm(c_frames / "wither_normal.ppm")
    for state in states:
        ga = load_rgb(goldens / (state + "_a.png"))
        gb = load_rgb(goldens / (state + "_b.png"))
        cand = load_ppm(c_frames / (state + ".ppm"))
        if ga.shape != ga_bg.shape or gb.shape != ga_bg.shape \
                or cand.shape != ga_bg.shape:
            raise SystemExit("FAIL: %s dimensions differ" % state)

        ga_fg = np.max(np.abs(delta(ga, ga_bg)), axis=2) > THRESH
        gb_fg = np.max(np.abs(delta(gb, gb_bg)), axis=2) > THRESH
        c_fg = np.max(np.abs(delta(cand, c_bg)), axis=2) > THRESH
        owned = ga_fg | gb_fg | c_fg
        if state == "wither_armored":
            ga_contract = delta(ga, ga_normal)
            gb_contract = delta(gb, gb_normal)
            c_contract = delta(cand, c_normal)
        else:
            ga_contract = ga
            gb_contract = gb
            c_contract = cand
        java_ab = np.max(np.abs(ga_contract - gb_contract), axis=2)
        native = np.max(np.abs(ga_contract - c_contract), axis=2)
        composed = np.max(np.abs(ga - cand), axis=2)
        isolated = np.max(np.abs(delta(ga, ga_bg) - delta(cand, c_bg)),
                          axis=2)
        java_ab[~owned] = 0
        native[~owned] = 0
        composed[~owned] = 0
        java_ab[y0:y1, x0:x1] = 0
        native[y0:y1, x0:x1] = 0
        composed[y0:y1, x0:x1] = 0
        isolated[y0:y1, x0:x1] = 0
        ab_pts = np.argwhere(java_ab > THRESH)
        native_pts = np.argwhere(native > THRESH)
        composed_pts = np.argwhere(composed > THRESH)
        background_explained = (composed > THRESH) & (isolated <= THRESH)
        entry = {
            "java_ab_px": int(ab_pts.shape[0]),
            "owned_px": int(np.count_nonzero(owned)),
            "native_px": int(native_pts.shape[0]),
            "native_max": int(native.max()),
            "native_locations_yx": native_pts[:16].astype(int).tolist(),
            "composed_px": int(composed_pts.shape[0]),
            "composed_max": int(composed.max()),
            "background_explained_composed_px": int(
                np.count_nonzero(background_explained)),
        }
        report["states"][state] = entry
        print("%s: Java A/B=%d native=%d max=%d composed=%d/%d" %
              (state, entry["java_ab_px"], entry["native_px"],
               entry["native_max"], entry["composed_px"],
               entry["composed_max"]))
        if entry["java_ab_px"]:
            failed = True
        if entry["native_px"] > args.max_native_px:
            failed = True

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        raise SystemExit("FAIL: Wither subject contract")
    print("PASS: Wither subject contract")


if __name__ == "__main__":
    main()
