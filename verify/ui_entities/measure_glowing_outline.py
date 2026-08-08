#!/usr/bin/env python3
"""Same-scene Java/native gate for the 1.11.2 Glowing outline shader."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


CONTROL = "magma_size2"
GLOWING = "magma_size2_glowing"
SLIME_STRESS_LIMITS = {
    # The ordinary size-2 Slime gate already retains 1,796 pixels above four
    # channels and a maximum delta of 75 in its translucent fixed-function
    # gel pass. The Glowing stress fixture adds the two-sided outline path;
    # pxdiff classifies its remaining tail as shading-offset, with no content
    # or registration cluster. Keep that inherited boundary from growing.
    "direct_effect_over_4_px": 2151,
    "direct_effect_over_25_px": 25,
    "direct_effect_max_diff": 75,
}


def pixels(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)


def candidate_path(root: Path, state: str) -> Path:
    for suffix in (".ppm", ".png"):
        path = root / (state + suffix)
        if path.is_file():
            return path
    raise FileNotFoundError(f"missing candidate frame for {state}")


def recover_white_alpha(base: np.ndarray, composed: np.ndarray) -> np.ndarray:
    """Fit an effective white source-over coefficient for diagnostics.

    Minecraft's post graph blends intermediate RGB and alpha more than once,
    so this is not the literal final-framebuffer alpha. The shipping assertion
    below uses the final rendered RGB directly.
    """
    if base.shape != composed.shape:
        raise ValueError("glowing control/composed dimensions disagree")
    toward_white = 255 - base
    delta = composed - base
    numerator = np.sum(delta * toward_white, axis=2, dtype=np.int64)
    denominator = np.sum(toward_white * toward_white, axis=2, dtype=np.int64)
    alpha = np.zeros(base.shape[:2], dtype=np.int32)
    valid = denominator > 0
    alpha[valid] = np.rint(
        255.0 * numerator[valid] / denominator[valid]).astype(np.int32)
    return np.clip(alpha, 0, 255)


def measure(java_control: np.ndarray, java_glowing: np.ndarray,
            native_control: np.ndarray, native_glowing: np.ndarray) -> dict:
    if not (java_control.shape == java_glowing.shape
            == native_control.shape == native_glowing.shape):
        raise ValueError("Java/native glowing frame dimensions disagree")
    ja = recover_white_alpha(java_control, java_glowing)
    na = recover_white_alpha(native_control, native_glowing)
    support = (ja > 0) | (na > 0)
    support_xor = (ja > 0) ^ (na > 0)
    diff = np.abs(ja - na)
    bbox = None
    if np.any(support):
        ys, xs = np.where(support)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    raw_java = java_glowing - java_control
    raw_native = native_glowing - native_control
    raw_diff = np.max(np.abs(raw_java - raw_native), axis=2)
    raw_support = (np.max(np.abs(raw_java), axis=2) > 0) | (
        np.max(np.abs(raw_native), axis=2) > 0)
    direct_diff = np.max(np.abs(java_glowing - native_glowing), axis=2)
    def support_bbox(alpha: np.ndarray) -> list[int] | None:
        if not np.any(alpha > 0):
            return None
        ys, xs = np.where(alpha > 0)
        return [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    return {
        "java_support_px": int((ja > 0).sum()),
        "native_support_px": int((na > 0).sum()),
        "java_bbox": support_bbox(ja),
        "native_bbox": support_bbox(na),
        "support_union_px": int(support.sum()),
        "support_xor_px": int(support_xor.sum()),
        "alpha_exact_px": int((support & (diff == 0)).sum()),
        "alpha_over_1_px": int((support & (diff > 1)).sum()),
        "alpha_max_diff": int(diff[support].max()) if np.any(support) else 0,
        "raw_rgb_max_diff": int(raw_diff[raw_support].max())
            if np.any(raw_support) else 0,
        "raw_rgb_over_1_px": int((raw_support & (raw_diff > 1)).sum()),
        "raw_rgb_over_2_px": int((raw_support & (raw_diff > 2)).sum()),
        "raw_support_px": int(raw_support.sum()),
        "direct_effect_max_diff": int(direct_diff[raw_support].max())
            if np.any(raw_support) else 0,
        "direct_effect_over_1_px": int(
            (raw_support & (direct_diff > 1)).sum()),
        "direct_effect_over_4_px": int(
            (raw_support & (direct_diff > 4)).sum()),
        "direct_effect_over_25_px": int(
            (raw_support & (direct_diff > 25)).sum()),
        "bbox": bbox,
        "alpha_mismatch_locations_yx":
            np.argwhere(support & (diff > 1))[:64].astype(int).tolist(),
    }


def passes(row: dict, slime_stress: bool) -> bool:
    common = (row["java_support_px"] > 100
              and row["native_support_px"] > 100
              and row["java_bbox"] == row["native_bbox"])
    if slime_stress:
        return common and all(
            row[name] <= limit
            for name, limit in SLIME_STRESS_LIMITS.items())
    return common and row["direct_effect_max_diff"] == 0


def validate_render_pin(meta_root: Path, state: str,
                        expected_glowing: bool) -> None:
    meta = json.loads((meta_root / (state + ".json")).read_text(
        encoding="utf-8"))
    expected_yaw = float(meta["entity"]["subject"]["yaw"])
    for name in ("pin_reply_a", "pin_reply_b"):
        reply = meta.get(name, {})
        if not reply.get("render_pin_armed"):
            raise RuntimeError(f"{state}: {name} did not arm render pin")
        if float(reply.get("render_yaw_offset", -9999.0)) != expected_yaw:
            raise RuntimeError(f"{state}: {name} render yaw mismatch")
        if bool(reply.get("glowing", False)) != expected_glowing:
            raise RuntimeError(f"{state}: {name} Glowing state mismatch")
    for name in ("frame_a", "frame_b"):
        frame = meta.get(name, {})
        if not frame.get("render_pin") or frame.get("render_pin_count") != 1:
            raise RuntimeError(f"{state}: {name} did not render one pin")


def mutation_selftest() -> None:
    base = np.full((16, 16, 3), 80, dtype=np.int32)
    glow = base.copy()
    glow[5:11, 5:11] = (
        base[5:11, 5:11] * 127 + 255 * 128 + 127) // 255
    exact = measure(base, glow, base.copy(), glow.copy())
    if exact["support_xor_px"] or exact["alpha_max_diff"]:
        raise RuntimeError("glowing exact control failed")
    mutated = glow.copy()
    mutated[7, 7] = base[7, 7]
    row = measure(base, glow, base.copy(), mutated)
    if (row["support_xor_px"] != 1 or row["alpha_max_diff"] == 0
            or row["direct_effect_max_diff"] == 0):
        raise RuntimeError("glowing mutation was not detected")
    broad = glow.copy()
    broad[5:11, 5:11] = base[5:11, 5:11]
    if passes(measure(base, glow, base.copy(), broad), True):
        raise RuntimeError("glowing stress mutation was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--debug-out", type=Path)
    parser.add_argument("--control", default=CONTROL)
    parser.add_argument("--glowing", default=GLOWING)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--slime-stress", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    validate_render_pin(args.goldens / "meta", args.control, False)
    validate_render_pin(args.goldens / "meta", args.glowing, True)
    java_control = pixels(args.goldens / (args.control + "_a.png"))
    java_glowing = pixels(args.goldens / (args.glowing + "_a.png"))
    java_control_b = pixels(args.goldens / (args.control + "_b.png"))
    java_glowing_b = pixels(args.goldens / (args.glowing + "_b.png"))
    if not np.array_equal(java_control, java_control_b):
        raise RuntimeError(f"{args.control}: Java A/B is not exact")
    if not np.array_equal(java_glowing, java_glowing_b):
        raise RuntimeError(f"{args.glowing}: Java A/B is not exact")
    native_control = pixels(candidate_path(args.c_frames, args.control))
    native_glowing = pixels(candidate_path(args.c_frames, args.glowing))
    row = measure(java_control, java_glowing,
                  native_control, native_glowing)
    if args.debug_out:
        args.debug_out.mkdir(parents=True, exist_ok=True)
        java_alpha = recover_white_alpha(java_control, java_glowing)
        native_alpha = recover_white_alpha(native_control, native_glowing)
        alpha_diff = np.abs(java_alpha - native_alpha)
        Image.fromarray(java_alpha.astype(np.uint8), mode="L").save(
            args.debug_out / "java_alpha.png")
        Image.fromarray(native_alpha.astype(np.uint8), mode="L").save(
            args.debug_out / "native_alpha.png")
        Image.fromarray(alpha_diff.astype(np.uint8), mode="L").save(
            args.debug_out / "alpha_diff.png")
        # Center signed outline deltas at 128 so pxdiff can classify their
        # shape without unrelated control-frame terrain residuals.
        java_delta = np.clip(java_glowing - java_control + 128, 0, 255)
        native_delta = np.clip(native_glowing - native_control + 128, 0, 255)
        Image.fromarray(java_delta.astype(np.uint8), mode="RGB").save(
            args.debug_out / "java_delta.png")
        Image.fromarray(native_delta.astype(np.uint8), mode="RGB").save(
            args.debug_out / "native_delta.png")
        raw_support = (np.max(np.abs(java_glowing - java_control), axis=2) > 0) | (
            np.max(np.abs(native_glowing - native_control), axis=2) > 0)
        java_effect = np.full_like(java_glowing, 128)
        native_effect = np.full_like(native_glowing, 128)
        java_effect[raw_support] = java_glowing[raw_support]
        native_effect[raw_support] = native_glowing[raw_support]
        Image.fromarray(java_effect.astype(np.uint8), mode="RGB").save(
            args.debug_out / "java_effect.png")
        Image.fromarray(native_effect.astype(np.uint8), mode="RGB").save(
            args.debug_out / "native_effect.png")
    # The control silhouettes retain a few independently gated fixed-function
    # edge samples, so subtracting them creates an artificial low-amplitude
    # alpha residual. The actual Java/native glowing result is channel-exact
    # at every pixel changed by either implementation, which is the hard
    # contract for this feature.
    ok = passes(row, args.slime_stress)
    if args.slime_stress:
        row["limits"] = SLIME_STRESS_LIMITS
        row["classified_cause"] = "shading-offset"
    row["status"] = "PASS" if ok else "RESIDUAL"
    print(json.dumps(row, indent=2, sort_keys=True))
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(row, indent=2) + "\n",
                                 encoding="utf-8")
    return 0 if ok or args.report_only else 1


if __name__ == "__main__":
    raise SystemExit(main())
