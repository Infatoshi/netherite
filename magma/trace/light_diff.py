#!/usr/bin/env python3
"""Strict raw block- or sky-light diff for parked Java and magma cuboids."""

import argparse
import collections
import csv
import hashlib
import pathlib


def cell_count(box):
    x0, y0, z0, x1, y1, z1 = box
    if x1 < x0 or y0 < 0 or y1 < y0 or y1 > 255 or z1 < z0:
        raise ValueError(f"invalid inclusive light box: {box}")
    return (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1)


def coord_for(index, box):
    x0, y0, z0, x1, _y1, z1 = box
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    return (
        x0 + index % nx,
        y0 + index // (nx * nz),
        z0 + (index // nx) % nz,
    )


def read_light(path, cells):
    raw = pathlib.Path(path).read_bytes()
    if len(raw) != cells:
        raise ValueError(
            f"{path}: expected {cells} one-byte light cells, got {len(raw)}")
    invalid = [index for index, value in enumerate(raw) if value > 15]
    if invalid:
        raise ValueError(
            f"{path}: light {raw[invalid[0]]} outside 0..15 at cell "
            f"{invalid[0]}")
    return raw


def compare(java, magma):
    wrong = [
        index for index, pair in enumerate(zip(java, magma))
        if pair[0] != pair[1]
    ]
    return {
        "wrong": wrong,
        "max_abs": max(
            (abs(java[index] - magma[index]) for index in wrong),
            default=0,
        ),
        "confusion": collections.Counter(
            (java[index], magma[index]) for index in wrong),
    }


def write_mismatches(path, java, magma, wrong, box, kind):
    output = pathlib.Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        rows = csv.writer(stream)
        rows.writerow(("x", "y", "z", f"java_{kind}_light",
                       f"magma_{kind}_light", "absolute_error"))
        for index in wrong:
            rows.writerow((*coord_for(index, box), java[index], magma[index],
                           abs(java[index] - magma[index])))


def selftest():
    box = (10, 20, 30, 11, 21, 31)
    java = bytes(range(8))
    assert cell_count(box) == 8
    assert compare(java, java)["wrong"] == []
    magma = bytearray(java)
    magma[5] = 1
    result = compare(java, magma)
    assert result["wrong"] == [5]
    assert result["max_abs"] == 4
    assert coord_for(5, box) == (11, 21, 30)
    print("light_diff selftest: PASS (identity + value-negative)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java")
    parser.add_argument("--c")
    parser.add_argument("--box", type=int, nargs=6,
                        metavar=("X0", "Y0", "Z0", "X1", "Y1", "Z1"))
    parser.add_argument("--out")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--kind", choices=("block", "sky"), default="block")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.java or not args.c or args.box is None:
        parser.error("--java, --c, and --box are required")
    try:
        cells = cell_count(args.box)
        java = read_light(args.java, cells)
        magma = read_light(args.c, cells)
    except (OSError, ValueError) as exc:
        print(f"light_diff: {exc}")
        return 1

    result = compare(java, magma)
    wrong = result["wrong"]
    exact = cells - len(wrong)
    label = f"{args.kind.upper()}-LIGHT"
    print(f"{label} DIFF after input tape over {cells} cells")
    print(f"  box  = {tuple(args.box)} (inclusive, y/z/x serialization)")
    print(f"  java = {args.java}  "
          f"sha256={hashlib.sha256(java).hexdigest()[:16]}")
    print(f"  c    = {args.c}  "
          f"sha256={hashlib.sha256(magma).hexdigest()[:16]}")
    print(f"  exact: {exact}/{cells} = {100.0 * exact / cells:.6f}%")
    print(f"  max absolute light error: {result['max_abs']}")
    if not wrong:
        print(f"{label} VERDICT: EXACT MATCH")
        return 0

    first = wrong[0]
    print(f"FIRST {label} DIVERGENCE: index={first} "
          f"coord={coord_for(first, args.box)} "
          f"java={java[first]} magma={magma[first]}")
    print("TOP LIGHT CONFUSIONS (java -> magma):")
    for (java_value, magma_value), count in result["confusion"].most_common(
            max(0, args.top)):
        print(f"  {count:8d}  {java_value} -> {magma_value}")
    if args.out:
        write_mismatches(args.out, java, magma, wrong, args.box, args.kind)
        print(f"  every mismatch -> {args.out}")
    print(f"{label} VERDICT: MISMATCH ({len(wrong)} cells)")
    return 4


if __name__ == "__main__":
    raise SystemExit(main())
