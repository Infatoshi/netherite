#!/usr/bin/env python3
"""predict_ores.py - pure-Python vanilla populate-RNG oracle (seed replay).

Replays the EXACT vanilla ChunkProviderOverworld.populate RNG stream for a chunk
(JavaRandom LCG, k/l seeding, water/lava lake gates, 8 dungeon attempts) and then
predicts the dirt + gravel vein cells from BiomeDecorator.generateOres /
WorldGenMinable (MathHelper float32 sin table + toward-zero float->int casts).

Then checks the prediction against BOTH worlds:
  - the REAL java save (region .mca)
  - the magma dump (world_dump CRWD)

Whichever side matches the prediction is running the vanilla stream; the other
side's first divergent call is upstream of the ores.

Usage:
  uv run --no-project --with numpy --with nbt python3 trace/predict_ores.py \
      --region <save>/region --seed 0 --chunks "1,2 0,8 -2,1"
"""
import argparse
import math
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MAGMA = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import importlib.util
_spec = importlib.util.spec_from_file_location("world_diff", os.path.join(HERE, "world_diff.py"))
world_diff = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(world_diff)
read_mca_chunk = world_diff.read_mca_chunk
read_magma = world_diff.read_magma

MASK48 = (1 << 48) - 1
MULT = 0x5DEECE66D
ADD = 0xB


def _i32(x):
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x >= (1 << 31) else x


def _i64(x):
    x &= 0xFFFFFFFFFFFFFFFF
    return x - (1 << 64) if x >= (1 << 63) else x


class JavaRandom:
    def __init__(self, seed):
        self.set_seed(seed)

    def set_seed(self, seed):
        self.seed = (seed ^ MULT) & MASK48

    def next(self, bits):
        self.seed = (self.seed * MULT + ADD) & MASK48
        return _i32(self.seed >> (48 - bits))

    def next_int(self, bound):
        if bound <= 0:
            raise ValueError("bound<=0")
        if (bound & -bound) == bound:
            return _i32((bound * (self.next(31) & 0xFFFFFFFF)) >> 31)
        while True:
            bits = self.next(31)
            val = bits % bound
            if bits - val + (bound - 1) >= 0:
                return val

    def next_long(self):
        hi = self.next(32)
        lo = self.next(32)
        return _i64((hi << 32) + lo)

    def next_float(self):
        return np.float32(self.next(24)) / np.float32(1 << 24)

    def next_double(self):
        hi = self.next(26)
        lo = self.next(27)
        return float((hi << 27) + lo) * (2.0 ** -53)


# MathHelper float32 sin table
SIN_TABLE = np.array([math.sin(i * math.pi * 2.0 / 65536.0) for i in range(65536)],
                     dtype=np.float32)


def mh_sin(f):
    idx = int(np.float32(f) * np.float32(10430.378)) & 65535  # C-truncation like Java (int) cast
    return SIN_TABLE[idx]


def mh_cos(f):
    idx = int(np.float32(f) * np.float32(10430.378) + np.float32(16384.0)) & 65535
    return SIN_TABLE[idx]


def mh_floor(d):
    i = int(d)  # trunc
    return i - 1 if d < i else i


FPI = np.float32(math.pi)


def minable_cells(r, px, py, pz, nblocks):
    """WorldGenMinable.generate: consume rand EXACTLY like vanilla, return the
    ellipsoid cell set (world coords) it would try to place into natural stone."""
    f = np.float32(r.next_float() * FPI)
    nb = np.float32(nblocks)
    d0 = float(np.float32(px + 8) + mh_sin(f) * nb / np.float32(8.0))
    d1 = float(np.float32(px + 8) - mh_sin(f) * nb / np.float32(8.0))
    d2 = float(np.float32(pz + 8) + mh_cos(f) * nb / np.float32(8.0))
    d3 = float(np.float32(pz + 8) - mh_cos(f) * nb / np.float32(8.0))
    d4 = float(py + r.next_int(3) - 2)
    d5 = float(py + r.next_int(3) - 2)
    cells = set()
    for i in range(nblocks):
        f1 = float(np.float32(i) / np.float32(nblocks))
        d6 = d0 + (d1 - d0) * f1
        d7 = d4 + (d5 - d4) * f1
        d8 = d2 + (d3 - d2) * f1
        d9 = r.next_double() * nblocks / 16.0
        d10 = float(mh_sin(np.float32(FPI * np.float32(f1))) + np.float32(1.0)) * d9 + 1.0
        d11 = d10
        j = mh_floor(d6 - d10 / 2.0)
        k = mh_floor(d7 - d11 / 2.0)
        l = mh_floor(d8 - d10 / 2.0)
        i1 = mh_floor(d6 + d10 / 2.0)
        j1 = mh_floor(d7 + d11 / 2.0)
        k1 = mh_floor(d8 + d10 / 2.0)
        for l1 in range(j, i1 + 1):
            d12 = (l1 + 0.5 - d6) / (d10 / 2.0)
            if d12 * d12 < 1.0:
                for i2 in range(k, j1 + 1):
                    d13 = (i2 + 0.5 - d7) / (d11 / 2.0)
                    if d12 * d12 + d13 * d13 < 1.0:
                        for j2 in range(l, k1 + 1):
                            d14 = (j2 + 0.5 - d8) / (d10 / 2.0)
                            if d12 * d12 + d13 * d13 + d14 * d14 < 1.0 and 0 <= i2 < 256:
                                cells.add((l1, i2, j2))
    return cells


ORES = [  # (name, vanilla_id, count, size, minH, maxH) in generateOres order
    ("dirt", 3, 10, 33, 0, 256),
    ("gravel", 13, 8, 33, 0, 256),
    ("diorite", 1, 10, 33, 0, 80),
    ("granite", 1, 10, 33, 0, 80),
    ("andesite", 1, 10, 33, 0, 80),
    ("coal", 16, 20, 17, 0, 128),
    ("iron", 15, 20, 9, 0, 64),
    ("gold", 14, 2, 9, 0, 32),
    ("redstone", 73, 8, 8, 0, 16),
    ("diamond", 56, 1, 8, 0, 16),
]


def predict_chunk(world_seed, cx, cz, biome_id):
    """Replay populate for (cx,cz). Returns (ok, veins) where veins is a list of
    (ore_name, expect_id, cells). ok=False means a data-dependent stage fired
    (lake generated / possible dungeon) so the prediction past it is unreliable."""
    r = JavaRandom(world_seed)
    k = r.next_long()
    l = r.next_long()
    # Java long division truncates toward zero (NOT float division - 63-bit precision)
    tz2 = lambda v: -((-v) // 2) if v < 0 else v // 2
    k = _i64(tz2(k) * 2 + 1)
    l = _i64(tz2(l) * 2 + 1)
    r.set_seed(_i64((cx * k + cz * l) ^ world_seed))

    notes = []
    ok = True
    if biome_id not in (2, 17):
        if r.next_int(4) == 0:
            notes.append("WATER LAKE fires")
            ok = False
    if ok and r.next_int(8) == 0:
        r.next_int(16)
        inner = r.next_int(248) + 8
        l2 = r.next_int(inner)
        r.next_int(16)
        if l2 < 63:
            notes.append("LAVA LAKE fires (below sea)")
            ok = False
        elif r.next_int(10) == 0:
            notes.append("LAVA LAKE fires (above sea)")
            ok = False
    if ok:
        for _ in range(8):  # dungeon attempts: 3 pos + 2 size ints, no dungeon assumed
            r.next_int(16)
            r.next_int(256)
            r.next_int(16)
            r.next_int(2)
            r.next_int(2)

    veins = []
    if ok:
        bx0, bz0 = cx * 16, cz * 16
        for (name, vid, count, size, mn, mx) in ORES:
            for _ in range(count):
                x = r.next_int(16)
                y = r.next_int(mx - mn) + mn
                z = r.next_int(16)
                cells = minable_cells(r, bx0 + x, y, bz0 + z, size)
                veins.append((name, vid, cells))
    return ok, notes, veins


def load_java(region_dir, chunk_set):
    out = {}
    for (ccx, ccz) in chunk_set:
        try:
            blk, _ = read_mca_chunk(region_dir, ccx, ccz)
            out[(ccx, ccz)] = blk
        except Exception:
            pass
    return out


def get_id(world, wx, wy, wz):
    ch = world.get((wx >> 4, wz >> 4))
    if ch is None or not (0 <= wy < 256):
        return None
    return int(ch[wx & 15, wy, wz & 15])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", required=True)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--chunks", required=True, help='e.g. "1,2 0,8 -2,1"')
    args = ap.parse_args()

    targets = []
    for tok in args.chunks.split():
        a, b = tok.split(",")
        targets.append((int(a), int(b)))

    # magma dump: one tile per target chunk (small 4x4 window centered-ish)
    dump_bin = os.path.join(MAGMA, "trace", "world_dump")
    for (cx, cz) in targets:
        # biome for the water-lake gate: block (cx*16+16, cz*16+16) = chunk(cx+1,cz+1) local(0,0)
        try:
            _, bio = read_mca_chunk(args.region, cx + 1, cz + 1)
            biome_id = int(bio[0, 0])
        except Exception:
            biome_id = 1
        ok, notes, veins = predict_chunk(args.seed, cx, cz, biome_id)
        print(f"\n=== chunk ({cx},{cz}) biome={biome_id} ===")
        for n in notes:
            print(f"  note: {n}")
        if not ok:
            print("  SKIP (data-dependent stage fired; prediction unreliable)")
            continue

        # world data around the chunk (veins reach +-2 chunks at most)
        need = {(cx + dx, cz + dz) for dx in (-1, 0, 1, 2) for dz in (-1, 0, 1, 2)}
        java = load_java(args.region, need)
        out = os.path.join(HERE, "out", f"po_tile_{cx}_{cz}.bin")
        cmd = [dump_bin, "--seed", str(args.seed), "--cx0", str(cx - 1), "--cz0", str(cz - 1),
               "--ncx", "4", "--ncz", "4", "--out", out]
        rr = subprocess.run(cmd, cwd=MAGMA, capture_output=True, text=True)
        if rr.returncode != 0:
            sys.exit(f"world_dump failed: {rr.stderr}")
        _, cch = read_magma(out)
        cras = {}
        for key, (blk, bio2) in cch.items():
            cras[key] = world_diff.pb_to_vanilla_arr(blk)
        os.remove(out)

        per_ore = {}
        for (name, vid, cells) in veins:
            jm = jt = cm = ct = 0
            for (wx, wy, wz) in cells:
                jb = get_id(java, wx, wy, wz)
                cb = get_id(cras, wx, wy, wz)
                # a predicted cell "counts" if the world has stone there (vein missed
                # into non-stone is legal) OR the ore itself
                if jb is not None and jb in (1, vid):
                    jt += 1
                    if jb == vid or (vid == 1 and jb == 1):
                        jm += 1
                if cb is not None and cb in (1, vid):
                    ct += 1
                    if cb == vid or (vid == 1 and cb == 1):
                        cm += 1
            a = per_ore.setdefault(name, [0, 0, 0, 0])
            a[0] += jm; a[1] += jt; a[2] += cm; a[3] += ct
        print(f"  {'ore':10s} {'java hit':>12s} {'magma hit':>14s}")
        for name, (jm, jt, cm, ct) in per_ore.items():
            if name in ("diorite", "granite", "andesite"):
                continue  # id-indistinguishable from stone in the readers
            jp = f"{jm}/{jt}" if jt else "n/a"
            cp = f"{cm}/{ct}" if ct else "n/a"
            print(f"  {name:10s} {jp:>12s} {cp:>14s}")


if __name__ == "__main__":
    main()
