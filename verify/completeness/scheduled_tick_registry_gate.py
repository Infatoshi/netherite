#!/usr/bin/env python3
"""Fail closed when a Java block update callback cannot enter native queues."""

from __future__ import annotations

import json
import pathlib
import re


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    census = json.loads(
        (HERE / "block_callback_census_manifest.json").read_text())
    expected = {
        block["id"]
        for family in census["families"]
        if family["callback"] == "updateTick"
        for block in family["blocks"]
    }
    source = (ROOT / "magma/game/runtime.c").read_text()
    match = re.search(
        r"static int runtime_scheduled_callback_implemented\(int block\) "
        r"\{(.*?)\n\}", source, re.S)
    require(match is not None, "native scheduled-callback registry is missing")
    implemented = {int(value) for value in re.findall(r"case (\d+):", match.group(1))}
    require(expected == implemented - {1},
            f"scheduled callback drift: missing={sorted(expected-implemented)}, "
            f"extra={sorted(implemented-expected-{1})}")
    require(implemented == expected | {1},
            "only the inherited empty stone callback may supplement overrides")
    require(len(expected) == 81 and len(implemented) == 82,
            "checked scheduled callback cardinality changed")
    for token in (
            "runtime_tick_command_block", "runtime_command_block_propagate",
            "gm_runtime_command_block_set_execution_state",
            "gm_fluid_forget_near"):
        require(token in source, f"native scheduled dispatch lost {token}")
    test = (ROOT / "magma/game/test_command_block_tick.c").read_text()
    for token in (
            "first unconditional impulse callback",
            "repeating callback schedules itself and its chain successor",
            "only the repeating callback remains pending"):
        require(token in test, f"scheduled callback regression lost {token!r}")
    print("PASS scheduled callback registry: all 81 Java updateTick block "
          "identities plus inherited empty Block callback are admitted; "
          "command execution/chain and dynamic-fluid ownership are gated")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL scheduled callback registry: {exc}")
        raise SystemExit(1)
