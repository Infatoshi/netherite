#!/usr/bin/env python3
"""Fail-closed metadata contract for strict save-fork fixtures."""

from __future__ import annotations

import argparse
import json
import pathlib
import tempfile
from typing import Any


SCHEMA = "netherite.completeness_fixture"
VERSION = 1
COMPARATOR_FAMILIES = {
    "nbt", "numeric", "blocks", "light", "queues", "order", "events",
    "pixels",
}


class FixtureContractError(RuntimeError):
    pass


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FixtureContractError(f"{path} must be an object")
    return value


def _nonempty_string(value: Any, path: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise FixtureContractError(f"{path} must be a nonempty string")
    return value


def validate(document: dict[str, Any]) -> dict[str, Any]:
    if document.get("schema") != SCHEMA or document.get("version") != VERSION:
        raise FixtureContractError("unsupported fixture schema/version")
    _nonempty_string(document.get("id"), "id")
    _nonempty_string(document.get("todo"), "todo")

    boundary = _object(document.get("paired_boundary"), "paired_boundary")
    left = _nonempty_string(boundary.get("left"), "paired_boundary.left")
    right = _nonempty_string(boundary.get("right"), "paired_boundary.right")
    if left == right:
        raise FixtureContractError("paired boundary branches must be distinct")
    if boundary.get("same_tick") is not True:
        raise FixtureContractError("paired boundary must require the same tick")

    control = _object(document.get("negative_control"), "negative_control")
    _nonempty_string(control.get("mutation"), "negative_control.mutation")
    _nonempty_string(control.get("expected_path"), "negative_control.expected_path")
    if "before" not in control or "after" not in control:
        raise FixtureContractError("negative control needs before and after values")
    if control["before"] == control["after"]:
        raise FixtureContractError("negative control mutation is inert")

    horizons = document.get("horizons")
    if (not isinstance(horizons, list) or not horizons
            or any(not isinstance(value, int) or value < 1 for value in horizons)
            or horizons != sorted(set(horizons))):
        raise FixtureContractError(
            "horizons must be sorted unique positive integers")

    comparisons = document.get("comparisons")
    if not isinstance(comparisons, list) or not comparisons:
        raise FixtureContractError("comparisons must be a nonempty list")
    seen: set[str] = set()
    for index, raw in enumerate(comparisons):
        row = _object(raw, f"comparisons[{index}]")
        family = _nonempty_string(row.get("family"), f"comparisons[{index}].family")
        if family not in COMPARATOR_FAMILIES:
            raise FixtureContractError(f"unknown comparator family {family}")
        if family in seen:
            raise FixtureContractError(f"duplicate comparator family {family}")
        seen.add(family)
        if row.get("required") is not True:
            raise FixtureContractError(f"comparator {family} is not required")
        observations = row.get("minimum_observations")
        if not isinstance(observations, int) or observations < 1:
            raise FixtureContractError(
                f"comparator {family} permits an empty comparison")

    inputs = document.get("inputs")
    if not isinstance(inputs, list):
        raise FixtureContractError("inputs must be an array")
    if len(inputs) < max(horizons):
        raise FixtureContractError(
            "input sequence is shorter than the largest horizon")
    if any(not isinstance(value, dict) for value in inputs):
        raise FixtureContractError("every input must be an object")
    return document


def load(path: pathlib.Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise FixtureContractError(f"invalid fixture metadata: {exc}") from exc
    return validate(_object(document, "$"))


def _valid_fixture() -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "id": "contract-selftest",
        "todo": "HAR-07",
        "paired_boundary": {
            "left": "S0a",
            "right": "S0b",
            "same_tick": True,
        },
        "negative_control": {
            "mutation": "set one block",
            "expected_path": "$/chunks/0/Blocks[3]",
            "before": 1,
            "after": 2,
        },
        "horizons": [1, 2],
        "inputs": [{}, {}],
        "comparisons": [
            {"family": "nbt", "required": True, "minimum_observations": 1},
            {"family": "pixels", "required": True, "minimum_observations": 1},
        ],
    }


def selftest() -> None:
    valid = _valid_fixture()
    validate(valid)
    mutations = [
        ("missing alternate branch", lambda d: d["paired_boundary"].pop("right")),
        ("inert control", lambda d: d["negative_control"].update(after=1)),
        ("zero frames", lambda d: d["comparisons"][1].update(minimum_observations=0)),
        ("short inputs", lambda d: d.update(inputs=[{}])),
    ]
    for label, mutate in mutations:
        candidate = json.loads(json.dumps(valid))
        mutate(candidate)
        try:
            validate(candidate)
        except FixtureContractError:
            continue
        raise FixtureContractError(f"negative control passed: {label}")
    with tempfile.TemporaryDirectory(prefix="netherite-fixture-contract-") as raw:
        path = pathlib.Path(raw) / "fixture.json"
        path.write_text(json.dumps(valid))
        load(path)
    print("PASS fixture contract: paired boundary, mutation, nonempty observations")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("fixture", type=pathlib.Path)
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.command == "check":
        document = load(args.fixture)
        print(f"PASS fixture contract: {document['id']}")
    else:
        selftest()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FixtureContractError, OSError, ValueError) as exc:
        print(f"FAIL fixture contract: {exc}")
        raise SystemExit(1)
