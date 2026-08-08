#!/usr/bin/env python3
"""Same-scene subject gate for the remaining VIS-04 visual-tail fixtures."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


CASES = {
    # Stop above the end-stone horizon. Presence of the dragon intentionally
    # changes vanilla's boss-fog treatment of the whole world; that global
    # scene contract belongs to VIS-01, while this gate owns model/death rays.
    "dragon_death_50": ("dragon_background", (80, 35, 774, 208)),
    "dragon_death_100": ("dragon_background", (80, 35, 774, 208)),
    "dragon_death_190": ("dragon_background", (80, 35, 774, 208)),
    "dig_stone": ("dig_background", (307, 160, 547, 340)),
    "dig_grass": ("dig_background", (307, 160, 547, 340)),
    "fireball_small": ("projectile_background", (327, 50, 527, 260)),
    "fireball_dragon": ("projectile_background", (327, 50, 527, 260)),
    "xp_orb": ("projectile_background", (390, 130, 465, 205)),
}
OWNED_THRESHOLD = 25
HARD_THRESHOLD = 25

# Stable same-scene ceilings. Every nonzero hard tail was classified with
# verify/trace/pxdiff.py before admission:
# - dragon 50/100: one center-body dissolve cutout/shading patch;
# - dragon 190: three isolated additive-ray edge pixels (no >=50px cluster);
# - stone dig: twenty isolated particle-edge pixels (no >=50px cluster);
# - dragon fireball: one top raster row, registration with best shift (-1,0).
# XP is a uniform <=17-channel fixed-function lighting offset; grass dig and
# the small fireball have no hard pixels. Presence floors prevent empty-frame
# or missing-subject passes.
BUDGETS = {
    "dragon_death_50": {
        "minimum_java_owned": 5300, "minimum_native_owned": 5200,
        "hard_px": 150, "over_4_px": 750, "max_channel": 255,
    },
    "dragon_death_100": {
        "minimum_java_owned": 12700, "minimum_native_owned": 12600,
        "hard_px": 170, "over_4_px": 550, "max_channel": 255,
    },
    "dragon_death_190": {
        "minimum_java_owned": 32100, "minimum_native_owned": 32100,
        "hard_px": 5, "over_4_px": 15, "max_channel": 255,
    },
    "dig_stone": {
        "minimum_java_owned": 550, "minimum_native_owned": 570,
        "hard_px": 25, "over_4_px": 375, "max_channel": 64,
    },
    "dig_grass": {
        "minimum_java_owned": 70, "minimum_native_owned": 65,
        "hard_px": 0, "over_4_px": 32, "max_channel": 24,
    },
    "fireball_small": {
        "minimum_java_owned": 875, "minimum_native_owned": 875,
        "hard_px": 0, "over_4_px": 0, "max_channel": 4,
    },
    "fireball_dragon": {
        "minimum_java_owned": 12400, "minimum_native_owned": 12400,
        "hard_px": 280, "over_4_px": 285, "max_channel": 170,
    },
    "xp_orb": {
        "minimum_java_owned": 1240, "minimum_native_owned": 1240,
        "hard_px": 0, "over_4_px": 1250, "max_channel": 20,
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


def roi_mask(shape: tuple[int, ...], roi_xyxy: tuple[int, int, int, int]) \
        -> np.ndarray:
    out = np.zeros(shape[:2], dtype=bool)
    x0, y0, x1, y1 = roi_xyxy
    out[y0:y1, x0:x1] = True
    return out


def measure(java: np.ndarray, java_bg: np.ndarray,
            native: np.ndarray, native_bg: np.ndarray,
            roi_xyxy: tuple[int, int, int, int]) \
        -> tuple[dict, np.ndarray, np.ndarray, np.ndarray]:
    if not (java.shape == java_bg.shape == native.shape == native_bg.shape):
        raise ValueError("visual-tail frame dimensions disagree")
    roi = roi_mask(java.shape, roi_xyxy)
    java_owned = (
        np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD) & roi
    native_owned = (
        np.max(np.abs(native - native_bg), axis=2) > OWNED_THRESHOLD) & roi
    owned = java_owned | native_owned
    # These fixtures deliberately keep their real world behind translucent
    # subjects. Compare each renderer's state-minus-own-background response,
    # not the two absolute frames: the latter prices unrelated terrain-light
    # residuals into every transparent or depth-touched subject pixel.
    java_delta = java - java_bg
    native_delta = native - native_bg
    direct = np.max(np.abs(java_delta - native_delta), axis=2)
    hard = owned & (direct > HARD_THRESHOLD)
    over_4 = owned & (direct > 4)
    bbox = None
    if np.any(owned):
        ys, xs = np.where(owned)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    return ({
        "java_owned_px": int(java_owned.sum()),
        "native_owned_px": int(native_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "over_4_px": int(over_4.sum()),
        "hard_px": int(hard.sum()),
        "max_channel": int(direct[owned].max()) if np.any(owned) else 0,
        "bbox": bbox,
        "hard_locations_yx": np.argwhere(hard)[:64].astype(int).tolist(),
    }, owned, java_delta, native_delta)


def mutation_selftest() -> None:
    bg = np.zeros((480, 854, 3), dtype=np.int16)
    java = bg.copy()
    java[200, 425] = (80, 90, 100)
    row, _, _, _ = measure(
        java, bg, java.copy(), bg, (300, 100, 550, 350))
    if row["hard_px"] != 0:
        raise RuntimeError("visual-tail exact control failed")
    mutated = java.copy()
    mutated[200, 425, 0] += HARD_THRESHOLD + 1
    row, _, _, _ = measure(
        java, bg, mutated, bg, (300, 100, 550, 350))
    if row["hard_px"] != 1:
        raise RuntimeError("visual-tail mutation was not detected")


def validate_metadata(meta_root: Path, state: str) -> None:
    """Reject stable but semantically wrong Java fixtures."""
    meta = json.loads((meta_root / (state + ".json")).read_text(
        encoding="utf-8"))
    for name in ("frame_a", "frame_b"):
        frame = meta.get(name, {})
        expected_pin = state.startswith("dragon_death_") or state == "xp_orb"
        if bool(frame.get("render_pin")) != expected_pin:
            raise RuntimeError(
                f"{state}: {name} render_pin={frame.get('render_pin')} "
                f"expected {int(expected_pin)}")
        if expected_pin and frame.get("render_pin_count") != 1:
            raise RuntimeError(f"{state}: {name} did not render one pinned entity")
    if state.startswith("dragon_death_"):
        expected = int(state.rsplit("_", 1)[1])
        if int(meta.get("entity", {}).get("death_ticks", -1)) != expected:
            raise RuntimeError(f"{state}: wrong entity death_ticks")
        for name in ("pin_reply_a", "pin_reply_b"):
            reply = meta.get(name, {})
            if (not reply.get("render_pin_armed")
                    or int(reply.get("render_pin_death_ticks", -1)) != expected):
                raise RuntimeError(f"{state}: {name} did not pin death tick {expected}")
        for name in ("frame_a", "frame_b"):
            if int(meta.get(name, {}).get("boss_fog", -1)) != 0:
                raise RuntimeError(f"{state}: {name} boss fog was not measured off")
    elif state.startswith("dig_"):
        dig = meta.get("dig", {})
        for name in ("pin_reply_a", "pin_reply_b"):
            reply = meta.get(name, {})
            if (reply.get("kind") != "dig_hit"
                    or int(reply.get("count", -1)) != int(dig.get("count", -2))
                    or [reply.get(k) for k in ("bx", "by", "bz", "face")]
                    != [dig.get(k) for k in ("bx", "by", "bz", "face")]):
                raise RuntimeError(f"{state}: {name} dig pin mismatch")
    elif state == "xp_orb":
        for name in ("pin_reply_a", "pin_reply_b"):
            reply = meta.get(name, {})
            if (reply.get("kind") != "xp_orb"
                    or not reply.get("render_pin_armed")
                    or int(reply.get("value", -1)) != 17
                    or int(reply.get("age", -1)) != 0
                    or int(reply.get("color", -1)) != 0):
                raise RuntimeError(f"{state}: {name} XP render pin mismatch")
    elif state.startswith("fireball_"):
        expected = "small_fireball" if state.endswith("small") \
            else "dragon_fireball"
        for name in ("pin_reply_a", "pin_reply_b"):
            reply = meta.get(name, {})
            if reply.get("kind") != expected or reply.get("render_pin_armed"):
                raise RuntimeError(f"{state}: {name} projectile contract mismatch")


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

    backgrounds = {}
    native_backgrounds = {}
    for background in sorted({row[0] for row in CASES.values()}):
        java_a = pixels(args.goldens / (background + "_a.png"))
        java_b = pixels(args.goldens / (background + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{background}: Java A/B is not exact")
        backgrounds[background] = java_a
        native_backgrounds[background] = pixels(
            candidate_path(args.c_frames, background))

    report = {
        "rule": "stable_same_scene_subject",
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
    }
    failed = []
    for state, (background, roi_xyxy) in CASES.items():
        validate_metadata(args.goldens / "meta", state)
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not exact")
        native = pixels(candidate_path(args.c_frames, state))
        row, owned, java_delta, native_delta = measure(
            java_a, backgrounds[background], native,
            native_backgrounds[background], roi_xyxy)
        row["background"] = background
        row["roi_xyxy"] = list(roi_xyxy)
        if args.debug_out:
            args.debug_out.mkdir(parents=True, exist_ok=True)
            java_debug = np.zeros_like(java_a, dtype=np.int16)
            native_debug = np.zeros_like(native, dtype=np.int16)
            java_debug[owned] = np.clip(128 + java_delta[owned], 0, 255)
            native_debug[owned] = np.clip(128 + native_delta[owned], 0, 255)
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
            "visual-tail subject contract failed: " + ", ".join(failed))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL visual-tail: {error}")
        raise SystemExit(1)
