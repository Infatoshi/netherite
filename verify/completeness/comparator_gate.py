#!/usr/bin/env python3
"""Strict comparison vector with mutation gates for every fork family."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
try:
    import anvil_semantic
finally:
    sys.path.pop(0)


FAMILIES = (
    "nbt", "numeric", "blocks", "light", "queues", "order", "events",
    "pixels",
)


class ComparatorError(RuntimeError):
    pass


def _observations(value: Any) -> int:
    if isinstance(value, dict):
        return sum(_observations(child) for child in value.values())
    if isinstance(value, list):
        return sum(_observations(child) for child in value)
    return 1


def _pixel_difference(
    left: list[dict[str, Any]], right: list[dict[str, Any]]
) -> dict[str, Any] | None:
    if not left or not right:
        raise ComparatorError("pixel comparison has zero frames")
    if len(left) != len(right):
        return {
            "path": "$/pixels", "left": len(left), "right": len(right),
            "reason": "frame_count",
        }
    compared = 0
    for frame_index, (a, b) in enumerate(zip(left, right)):
        for key in ("width", "height", "rgb_hex"):
            if key not in a or key not in b:
                raise ComparatorError(f"pixel frame {frame_index} lacks {key}")
        if a["width"] != b["width"] or a["height"] != b["height"]:
            return {
                "path": f"$/pixels[{frame_index}]", "left": [a["width"], a["height"]],
                "right": [b["width"], b["height"]], "reason": "dimensions",
            }
        try:
            left_rgb = bytes.fromhex(a["rgb_hex"])
            right_rgb = bytes.fromhex(b["rgb_hex"])
            mask = bytes.fromhex(a.get("owned_mask_hex", ""))
        except ValueError as exc:
            raise ComparatorError(f"invalid pixel hex in frame {frame_index}") from exc
        expected = a["width"] * a["height"] * 3
        if len(left_rgb) != expected or len(right_rgb) != expected:
            raise ComparatorError(f"pixel frame {frame_index} has invalid RGB length")
        if mask and len(mask) != a["width"] * a["height"]:
            raise ComparatorError(f"pixel frame {frame_index} has invalid mask length")
        for pixel in range(a["width"] * a["height"]):
            if mask and mask[pixel] == 0:
                continue
            compared += 1
            for channel in range(3):
                offset = pixel * 3 + channel
                if left_rgb[offset] != right_rgb[offset]:
                    return {
                        "path": (
                            f"$/pixels[{frame_index}]/"
                            f"y={pixel // a['width']},x={pixel % a['width']},c={channel}"
                        ),
                        "left": left_rgb[offset], "right": right_rgb[offset],
                        "reason": "pixel",
                    }
    if compared == 0:
        raise ComparatorError("pixel comparison owns zero pixels")
    return None


def compare_family(family: str, left: Any, right: Any) -> dict[str, Any] | None:
    if family not in FAMILIES:
        raise ComparatorError(f"unknown comparator family {family}")
    if family == "pixels":
        if not isinstance(left, list) or not isinstance(right, list):
            raise ComparatorError("pixels must be frame arrays")
        return _pixel_difference(left, right)
    if _observations(left) < 1 or _observations(right) < 1:
        raise ComparatorError(f"{family} comparison has zero observations")
    difference = anvil_semantic.first_difference(left, right, f"$/{family}")
    return difference


def compare_bundle(
    left: dict[str, Any], right: dict[str, Any]
) -> dict[str, Any] | None:
    if set(left) != set(FAMILIES) or set(right) != set(FAMILIES):
        raise ComparatorError(
            "comparison bundle must contain every family exactly once")
    for family in FAMILIES:
        difference = compare_family(family, left[family], right[family])
        if difference is not None:
            return {"family": family, **difference}
    return None


def _baseline() -> dict[str, Any]:
    return {
        "nbt": {"tag": {"type": "int", "value": 7}},
        "numeric": {"motion_x_bits": "3ff0000000000000"},
        "blocks": {"ids": [1, 2], "meta": [0, 3]},
        "light": {"sky": [15, 14], "block": [0, 2]},
        "queues": [{"time": 4, "priority": 1, "tie": 0}],
        "order": {"entities": [9, 3], "tiles": [[1, 2, 3]]},
        "events": [{"tick": 1, "kind": "sound", "id": 4}],
        "pixels": [{"width": 2, "height": 1, "rgb_hex": "010203040506"}],
    }


def selftest() -> None:
    baseline = _baseline()
    if compare_bundle(baseline, copy.deepcopy(baseline)) is not None:
        raise ComparatorError("identical bundle did not compare equal")
    mutations = {
        "nbt": lambda value: value["nbt"]["tag"].update(value=8),
        "numeric": lambda value: value["numeric"].update(
            motion_x_bits="3ff0000000000001"),
        "blocks": lambda value: value["blocks"]["ids"].__setitem__(1, 5),
        "light": lambda value: value["light"]["sky"].__setitem__(0, 14),
        "queues": lambda value: value["queues"][0].update(tie=1),
        "order": lambda value: value["order"]["entities"].reverse(),
        "events": lambda value: value["events"][0].update(id=5),
        "pixels": lambda value: value["pixels"][0].update(
            rgb_hex="010203040507"),
    }
    for family, mutate in mutations.items():
        changed = copy.deepcopy(baseline)
        mutate(changed)
        difference = compare_bundle(baseline, changed)
        if difference is None or difference.get("family") != family:
            raise ComparatorError(
                f"{family} mutation was not localized: {difference}")
        reverted = copy.deepcopy(changed)
        reverted[family] = copy.deepcopy(baseline[family])
        if compare_bundle(baseline, reverted) is not None:
            raise ComparatorError(f"{family} mutation did not reverse cleanly")
    empty_pixels = copy.deepcopy(baseline)
    empty_pixels["pixels"] = []
    try:
        compare_bundle(baseline, empty_pixels)
    except ComparatorError as exc:
        if "zero frames" not in str(exc):
            raise
    else:
        raise ComparatorError("zero-frame pixel comparison passed")
    print("PASS comparator mutations: " + ",".join(FAMILIES))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    compare = sub.add_parser("compare")
    compare.add_argument("left", type=pathlib.Path)
    compare.add_argument("right", type=pathlib.Path)
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.command == "selftest":
        selftest()
        return 0
    try:
        left = json.loads(args.left.read_text())
        right = json.loads(args.right.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ComparatorError(f"invalid comparison bundle: {exc}") from exc
    difference = compare_bundle(left, right)
    if difference is not None:
        print(json.dumps(difference, indent=2, sort_keys=True))
        return 1
    print("PASS complete comparison bundle")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ComparatorError, OSError, ValueError) as exc:
        print(f"FAIL comparator: {exc}")
        raise SystemExit(1)
