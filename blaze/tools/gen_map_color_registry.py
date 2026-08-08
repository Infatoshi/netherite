#!/usr/bin/env python3
"""Generate the native 1.11.2 raw-state MapColor table from Java rows."""

from __future__ import annotations

import argparse
import pathlib


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    color = [-1] * (256 * 16)
    liquid = [0] * (256 * 16)
    seen = set()
    for line in args.input.read_text().splitlines():
        fields = line.split()
        if len(fields) != 6 or fields[0] != "M":
            raise SystemExit(f"invalid map color row: {line!r}")
        block, meta, valid, value, is_liquid = map(int, fields[1:])
        if not 0 <= block < 256 or not 0 <= meta < 16:
            raise SystemExit(f"out-of-range map color row: {line!r}")
        key = block * 16 + meta
        if key in seen:
            raise SystemExit(f"duplicate map color row: {line!r}")
        seen.add(key)
        if valid:
            if not 0 <= value <= 35 or is_liquid not in (0, 1):
                raise SystemExit(f"invalid map color payload: {line!r}")
            color[key] = value
            liquid[key] = is_liquid
    if len(seen) != 236 * 16:
        raise SystemExit(f"expected 3776 rows, found {len(seen)}")

    def array(name: str, values: list[int], ctype: str) -> str:
        rows = []
        for offset in range(0, len(values), 16):
            rows.append("    " + ",".join(
                str(value) for value in values[offset:offset + 16]) + ",")
        return f"static const {ctype} {name}[256 * 16] = {{\n" \
            + "\n".join(rows) + "\n};\n"

    text = """/* Generated from qrl.MapColorGolden. Do not edit. */
#ifndef MC_MAP_COLOR_REGISTRY_H
#define MC_MAP_COLOR_REGISTRY_H

"""
    text += array("MC_MAP_COLOR", color, "signed char") + "\n"
    text += array("MC_MAP_LIQUID", liquid, "unsigned char")
    text += "\n#endif\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
