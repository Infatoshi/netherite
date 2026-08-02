#!/usr/bin/env python3
"""Convert magma world_dump CRWD/CRWS binary to sparse x,y,z,state lines.

CRWD: block[] is compact PB model keys (matches blaze owr_policy_dump state).
CRWS: block[] is packed vanilla state (id<<4|meta); not used by wrapper_diff
      under matched PB policy (world_dump without --states).

Output: one "x,y,z,state" line per non-air cell, sorted by (x,y,z,state).
Air is state 0 for both formats.
"""
from __future__ import annotations

import argparse
import struct
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("bin", help="world_dump .bin path (CRWD or CRWS)")
    ap.add_argument("-o", "--out", default="-", help="output path or - for stdout")
    args = ap.parse_args()

    with open(args.bin, "rb") as f:
        magic = f.read(4)
        if magic not in (b"CRWD", b"CRWS"):
            print(f"bad magic {magic!r}", file=sys.stderr)
            return 1
        seed = struct.unpack("<q", f.read(8))[0]
        cx0, cz0, ncx, ncz = struct.unpack("<4i", f.read(16))
        cells: list[tuple[int, int, int, int]] = []
        for ix in range(ncx):
            for iz in range(ncz):
                cx, cz = cx0 + ix, cz0 + iz
                # 16*256*16 uint16 then 16*16 int32 biome (ignored)
                raw = f.read(16 * 256 * 16 * 2)
                if len(raw) != 16 * 256 * 16 * 2:
                    print("truncated block payload", file=sys.stderr)
                    return 1
                f.read(16 * 16 * 4)  # biome
                blk = struct.unpack(f"<{16 * 256 * 16}H", raw)
                # index: lx*4096 + lz*256 + y  (world_dump / CB_INDEX)
                for lx in range(16):
                    for lz in range(16):
                        base = lx * 4096 + lz * 256
                        for y in range(256):
                            st = blk[base + y]
                            if st == 0:
                                continue
                            cells.append((cx * 16 + lx, y, cz * 16 + lz, int(st)))
        cells.sort()

    out = sys.stdout if args.out == "-" else open(args.out, "w")
    try:
        for x, y, z, st in cells:
            out.write(f"{x},{y},{z},{st}\n")
    finally:
        if out is not sys.stdout:
            out.close()

    print(
        f"crwd_to_sparse: seed={seed} chunks=({cx0},{cz0})+{ncx}x{ncz} "
        f"non_air={len(cells)} magic={magic.decode()}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
