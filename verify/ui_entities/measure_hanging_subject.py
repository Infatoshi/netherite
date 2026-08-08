#!/usr/bin/env python3
"""Strict real-Java/native subject gate for hanging entities and leashes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


CASES = {
    "hanging_painting_kebab": ("hanging_wall_background", 0),
    "hanging_painting_pointer": ("hanging_wall_background", 3),
    "hanging_frame_empty": ("hanging_wall_background", 0),
    "hanging_frame_stick": ("hanging_wall_background", 1),
    "hanging_frame_dirt": ("hanging_wall_background", 17),
    "hanging_frame_map": ("hanging_wall_background", 0),
    "hanging_leash_knot": ("hanging_fence_background", 0),
    "hanging_leashed_llama": ("hanging_fence_background", 2),
}
OWNED_THRESHOLD = 2
HARD_THRESHOLD = 25


def pixels(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def candidate_path(root: Path, state: str) -> Path:
    for suffix in (".ppm", ".png"):
        path = root / (state + suffix)
        if path.is_file():
            return path
    raise FileNotFoundError(f"missing candidate frame for {state}")


def measure(java: np.ndarray, java_bg: np.ndarray,
            candidate: np.ndarray, candidate_bg: np.ndarray) -> dict:
    if not (java.shape == java_bg.shape == candidate.shape
            == candidate_bg.shape):
        raise ValueError("frame dimensions disagree")
    java_owned = np.max(np.abs(java - java_bg), axis=2) > OWNED_THRESHOLD
    candidate_owned = (
        np.max(np.abs(candidate - candidate_bg), axis=2) > OWNED_THRESHOLD)
    owned = java_owned | candidate_owned
    delta = np.max(np.abs(java - candidate), axis=2)
    hard = owned & (delta > HARD_THRESHOLD)
    hard_samples = []
    for y, x in zip(*np.where(hard)):
        hard_samples.append({
            "x": int(x), "y": int(y),
            "java": [int(v) for v in java[y, x]],
            "candidate": [int(v) for v in candidate[y, x]],
            "delta": int(delta[y, x]),
        })
    if np.any(owned):
        ys, xs = np.where(owned)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
        max_delta = int(delta[owned].max())
        mean_ch = float(np.abs(java - candidate)[owned].mean())
    else:
        bbox = None
        max_delta = 0
        mean_ch = 0.0
    return {
        "java_owned_px": int(java_owned.sum()),
        "candidate_owned_px": int(candidate_owned.sum()),
        "owned_union_px": int(owned.sum()),
        "hard_px": int(hard.sum()),
        "hard_samples": hard_samples,
        "max_channel_delta": max_delta,
        "mean_channel_delta": mean_ch,
        "bbox": bbox,
    }


def mutation_selftest() -> None:
    bg = np.zeros((4, 4, 3), dtype=np.int16)
    java = bg.copy()
    java[1, 1] = (80, 90, 100)
    same = measure(java, bg, java.copy(), bg)
    assert same["owned_union_px"] == 1 and same["hard_px"] == 0
    mutated = java.copy()
    mutated[1, 1, 0] += HARD_THRESHOLD + 1
    caught = measure(java, bg, mutated, bg)
    assert caught["hard_px"] == 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--goldens", type=Path, required=True)
    parser.add_argument("--c-frames", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        mutation_selftest()

    report = {
        "owned_threshold": OWNED_THRESHOLD,
        "hard_threshold": HARD_THRESHOLD,
        "states": {},
    }
    failed = []
    for state, (background, budget) in CASES.items():
        java_a = pixels(args.goldens / (state + "_a.png"))
        java_b = pixels(args.goldens / (state + "_b.png"))
        if not np.array_equal(java_a, java_b):
            raise RuntimeError(f"{state}: Java A/B is not byte-exact")
        row = measure(
            java_a,
            pixels(args.goldens / (background + "_a.png")),
            pixels(candidate_path(args.c_frames, state)),
            pixels(candidate_path(args.c_frames, background)),
        )
        row["hard_budget"] = budget
        row["pass"] = row["hard_px"] <= budget
        report["states"][state] = row
        if not row["pass"]:
            failed.append(state)
        print(f"{state}: hard={row['hard_px']}/{budget} "
              f"owned={row['owned_union_px']} max={row['max_channel_delta']} "
              f"mean/ch={row['mean_channel_delta']:.6f}")

    report["pass"] = not failed
    report["failed"] = failed
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if failed:
        print("hanging subject: FAIL " + ", ".join(failed))
        return 1
    print("hanging subject: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
