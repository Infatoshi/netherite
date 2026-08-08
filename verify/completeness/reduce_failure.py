#!/usr/bin/env python3
"""Reduce a strict fork failure while retaining its first-divergence fingerprint."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import subprocess
import tempfile
from collections.abc import Callable
from typing import Any


class ReductionError(RuntimeError):
    pass


Predicate = Callable[[dict[str, Any]], bool]


def _ddmin(
    case: dict[str, Any], key: str, predicate: Predicate
) -> dict[str, Any]:
    values = list(case.get(key, []))
    granularity = 2
    while len(values) >= 1:
        chunk = max(1, (len(values) + granularity - 1) // granularity)
        reduced = False
        for start in range(0, len(values), chunk):
            candidate_values = values[:start] + values[start + chunk:]
            candidate = copy.deepcopy(case)
            candidate[key] = candidate_values
            if predicate(candidate):
                case, values = candidate, candidate_values
                granularity = max(2, granularity - 1)
                reduced = True
                break
        if reduced:
            continue
        if granularity >= len(values):
            break
        granularity = min(len(values), granularity * 2)
    return case


def _inside(cell: dict[str, Any], bounds: tuple[int, int, int, int, int, int]) -> bool:
    x0, x1, y0, y1, z0, z1 = bounds
    return (
        x0 <= cell["x"] <= x1
        and y0 <= cell["y"] <= y1
        and z0 <= cell["z"] <= z1
    )


def _shrink_cuboid(case: dict[str, Any], predicate: Predicate) -> dict[str, Any]:
    cells = case.get("cells", [])
    if not cells:
        return case
    for cell in cells:
        if (not isinstance(cell, dict)
                or any(not isinstance(cell.get(axis), int) for axis in "xyz")):
            raise ReductionError("every cell needs integer x/y/z coordinates")
    changed = True
    while changed and case.get("cells"):
        changed = False
        cells = case["cells"]
        bounds = [
            min(cell[axis] for cell in cells)
            for axis in ("x", "y", "z")
        ] + [
            max(cell[axis] for cell in cells)
            for axis in ("x", "y", "z")
        ]
        # Reorder into x0,x1,y0,y1,z0,z1.
        current = (
            bounds[0], bounds[3], bounds[1], bounds[4], bounds[2], bounds[5])
        for edge in range(6):
            low = edge % 2 == 0
            lo_index = (edge // 2) * 2
            hi_index = lo_index + 1
            lo, hi = current[lo_index], current[hi_index]
            if lo >= hi:
                continue
            trial = list(current)
            midpoint = (lo + hi) // 2
            trial[lo_index if low else hi_index] = midpoint + 1 if low else midpoint
            kept = [cell for cell in cells if _inside(cell, tuple(trial))]
            if not kept:
                continue
            candidate = copy.deepcopy(case)
            candidate["cells"] = kept
            if predicate(candidate):
                case = candidate
                changed = True
                break
    return case


def _shrink_horizon(case: dict[str, Any], predicate: Predicate) -> dict[str, Any]:
    horizon = case.get("horizon")
    if not isinstance(horizon, int) or horizon < 1:
        raise ReductionError("case horizon must be a positive integer")
    low, high = 1, horizon
    best = case
    while low <= high:
        middle = (low + high) // 2
        candidate = copy.deepcopy(case)
        candidate["horizon"] = middle
        candidate["inputs"] = list(candidate.get("inputs", []))[:middle]
        if predicate(candidate):
            best = candidate
            high = middle - 1
        else:
            low = middle + 1
    return best


def reduce_case(case: dict[str, Any], predicate: Predicate) -> dict[str, Any]:
    case = copy.deepcopy(case)
    if not predicate(case):
        raise ReductionError("initial case does not reproduce the fingerprint")
    case = _shrink_horizon(case, predicate)
    case = _shrink_cuboid(case, predicate)
    for key in ("cells", "entities", "tiles"):
        case = _ddmin(case, key, predicate)
    # Input contents can be reduced after the earliest required prefix is fixed.
    inputs = list(case.get("inputs", []))
    for index in range(len(inputs)):
        if not inputs[index]:
            continue
        candidate = copy.deepcopy(case)
        candidate["inputs"][index] = {}
        if predicate(candidate):
            case = candidate
    return case


def _command_predicate(
    command: list[str], fingerprint: str
) -> Predicate:
    if not command or "{case}" not in command:
        raise ReductionError("external command must contain a {case} argument")

    def predicate(case: dict[str, Any]) -> bool:
        with tempfile.TemporaryDirectory(prefix="netherite-reduce-") as raw:
            path = pathlib.Path(raw) / "case.json"
            path.write_text(json.dumps(case, sort_keys=True) + "\n")
            argv = [str(path) if value == "{case}" else value for value in command]
            result = subprocess.run(
                argv, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, check=False)
            return result.returncode != 0 and fingerprint in result.stdout

    return predicate


def selftest() -> None:
    case = {
        "horizon": 12,
        "inputs": [{}, {"arm": 1}, {"trigger": 1}] + [{}] * 9,
        "cells": [
            {"x": x, "y": 4, "z": 0, "block": 1}
            for x in range(12)
        ],
        "entities": [{"id": value} for value in (3, 7, 11)],
        "tiles": [{"id": value} for value in (2, 4)],
    }

    def reproduces(value: dict[str, Any]) -> bool:
        # The retained earliest failure needs x=3, entity=7 and input tick 2.
        # A second cause at x=10/horizon=8 must not distract the reducer.
        primary = (
            value["horizon"] >= 3
            and len(value["inputs"]) >= 3
            and value["inputs"][2].get("trigger") == 1
            and any(cell["x"] == 3 for cell in value["cells"])
            and any(entity["id"] == 7 for entity in value["entities"])
        )
        return primary

    reduced = reduce_case(case, reproduces)
    if reduced["horizon"] != 3 or len(reduced["inputs"]) != 3:
        raise ReductionError(f"horizon/prefix did not reduce: {reduced}")
    if [cell["x"] for cell in reduced["cells"]] != [3]:
        raise ReductionError(f"cuboid did not reduce: {reduced['cells']}")
    if [entity["id"] for entity in reduced["entities"]] != [7]:
        raise ReductionError(f"entities did not reduce: {reduced['entities']}")
    if reduced["tiles"]:
        raise ReductionError(f"irrelevant tiles survived: {reduced['tiles']}")
    print("PASS first-divergence reducer: cuboid, entities, tiles, prefix, horizon")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    reduce_parser = sub.add_parser("reduce")
    reduce_parser.add_argument("case", type=pathlib.Path)
    reduce_parser.add_argument("output", type=pathlib.Path)
    reduce_parser.add_argument("--fingerprint", required=True)
    reduce_parser.add_argument("command", nargs=argparse.REMAINDER)
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.action == "selftest":
        selftest()
        return 0
    if args.output.exists():
        raise ReductionError(f"output already exists: {args.output}")
    try:
        case = json.loads(args.case.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ReductionError(f"invalid input case: {exc}") from exc
    if not isinstance(case, dict):
        raise ReductionError("input case must be an object")
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    reduced = reduce_case(
        case, _command_predicate(command, args.fingerprint))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(reduced, indent=2, sort_keys=True) + "\n")
    print(f"PASS reduced first divergence -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ReductionError, ValueError) as exc:
        print(f"FAIL reduction: {exc}")
        raise SystemExit(1)
