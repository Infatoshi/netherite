#!/usr/bin/env python3
"""dim_world_diff.py - REAL Minecraft nether/end Anvil vs C provideChunk worldgen.

Ground truth = saved world DIM-1 / DIM1 region files (seed 0 qrl_0 by default),
the same Anvil path world_diff.py uses for overworld. C side = dim_worldgen_dump
output (ChunkProviderHell / ChunkProviderEnd provideChunk, vanilla-mapped ids).

provideChunk deliberately EXCLUDES biome-decorator populate (quartz, glowstone,
chorus, end spikes, exit portal platform). Those Java-only cells are free-passed
as POPULATE/STRUCTURE so the gate measures terrain shape + base blocks.

Usage:
  uv run --no-project --with numpy --with nbt python3 trace/dim_world_diff.py \\
      --dim nether --region ../../java/Minecraft/run/saves/qrl_0/DIM-1/region \\
      --c-mcbd /tmp/c_nether.mcbd --cx0 -2 --cz0 -2 --cx1 2 --cz1 2

  Exit 0 only if occupancy and terrain-exact gates pass.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BLAZE = REPO / "blaze" / "core"
sys.path.insert(0, str(HERE))
from world_diff import read_mca_chunk  # noqa: E402

# BiomeHell / WorldGeneratorHell decorations NOT in cpn_provide_chunk.
NETHER_POPULATE = {
    153,  # quartz ore
    89,   # glowstone
    39, 40,  # brown/red mushroom
    51,   # fire
    115,  # nether wart
    213,  # magma (decorator / springs)
    90,   # portal
    49,   # obsidian (portal frame remnants)
}

# BiomeEndDecorator + dragon fight + exit portal platform (not provideChunk).
END_STRUCTURE = {
    49,   # obsidian pillars / platform
    101,  # iron bars (crystal cages)
    7,    # bedrock (exit portal)
    119, 120,  # end portal / frame
    122,  # dragon egg
    50, 51,  # torch / fire
    198, 199, 200,  # end rod, chorus plant/flower
}


def build_dumper(out: Path) -> Path:
    src = HERE / "dim_worldgen_dump.c"
    cmd = [
        "cc", "-O2", "-ffp-contract=off",
        f"-I{BLAZE}", str(src), "-o", str(out), "-lm",
    ]
    subprocess.run(cmd, check=True)
    return out


def dump_c(dumper: Path, dim: str, seed: int, cx0: int, cz0: int, cx1: int, cz1: int,
           out: Path) -> None:
    with open(out, "wb") as f:
        subprocess.run(
            [str(dumper), dim, str(seed), str(cx0), str(cz0), str(cx1), str(cz1)],
            stdout=f, check=True,
        )


def load_c_mcbd(path: Path, cx0: int, cz0: int, cx1: int, cz1: int):
    raw = np.fromfile(path, dtype="<u2")
    ncx, ncz = cx1 - cx0 + 1, cz1 - cz0 + 1
    expect = ncx * ncz * 16 * 16 * 256
    if raw.size != expect:
        raise SystemExit(f"c-mcbd size {raw.size} != {expect}")
    chunks = {}
    off = 0
    for cz in range(cz0, cz1 + 1):
        for cx in range(cx0, cx1 + 1):
            pack = raw[off: off + 65536]
            off += 65536
            ids = (pack >> 4).astype(np.int32).reshape(256, 16, 16)  # y,z,x
            ids = np.transpose(ids, (2, 0, 1))  # x,y,z
            chunks[(cx, cz)] = ids
    return chunks


def free_pass_mask(dim: str, j: np.ndarray, c: np.ndarray) -> np.ndarray:
    """True = cell counts as match (populate/structure or lava equivalence)."""
    if dim == "nether":
        free = np.isin(j, list(NETHER_POPULATE))
        # still vs flowing lava
        lava_eq = ((j == 10) | (j == 11)) & ((c == 10) | (c == 11))
        # BiomeHell lava springs: still lava over netherrack/air from provideChunk
        lava_spring = ((j == 10) | (j == 11)) & ((c == 0) | (c == 87) | (c == 10) | (c == 11))
        return free | lava_eq | lava_spring
    # end
    free = np.isin(j, list(END_STRUCTURE))
    # exit portal carves end_stone: air over bedrock neighbor where C still has stone
    portal_air = (j == 0) & (c == 121)
    if portal_air.any():
        # 6-neighbor bedrock in java => platform carve
        bed = (j == 7)
        bed_n = np.zeros_like(bed)
        bed_n[:, 1:, :] |= bed[:, :-1, :]
        bed_n[:, :-1, :] |= bed[:, 1:, :]
        bed_n[1:, :, :] |= bed[:-1, :, :]
        bed_n[:-1, :, :] |= bed[1:, :, :]
        bed_n[:, :, 1:] |= bed[:, :, :-1]
        bed_n[:, :, :-1] |= bed[:, :, 1:]
        free = free | (portal_air & bed_n)
    return free


def compare_window(dim: str, region: Path, c_chunks: dict, cx0, cz0, cx1, cz1):
    tot = match = 0
    occ_tot = occ_match = 0
    missing = 0
    conf = {}
    per_chunk = []
    for cz in range(cz0, cz1 + 1):
        for cx in range(cx0, cx1 + 1):
            try:
                jblk, _ = read_mca_chunk(str(region), cx, cz)
            except Exception as e:
                missing += 1
                per_chunk.append({"cx": cx, "cz": cz, "error": str(e)})
                continue
            cblk = c_chunks[(cx, cz)]
            free = free_pass_mask(dim, jblk, cblk)
            exact = (jblk == cblk) | free
            n = jblk.size
            m = int(exact.sum())
            tot += n
            match += m
            jo = jblk != 0
            co = cblk != 0
            # occupancy: treat free-pass structure cells as matching occupancy
            occ = (jo == co) | free
            occ_tot += n
            occ_match += int(occ.sum())
            mism = int((~exact).sum())
            if mism:
                xs, ys, zs = np.where(~exact)
                for x, y, z in zip(xs.tolist(), ys.tolist(), zs.tolist()):
                    pair = (int(jblk[x, y, z]), int(cblk[x, y, z]))
                    conf[pair] = conf.get(pair, 0) + 1
            per_chunk.append({
                "cx": cx, "cz": cz,
                "exact_pct": 100.0 * m / n,
                "occ_pct": 100.0 * int(occ.sum()) / n,
                "mism": mism,
            })
    return {
        "dim": dim,
        "chunks_ok": sum(1 for p in per_chunk if "error" not in p),
        "chunks_missing": missing,
        "exact": (match / tot) if tot else 0.0,
        "occupancy": (occ_match / occ_tot) if occ_tot else 0.0,
        "mismatches": tot - match,
        "cells": tot,
        "top_mism": sorted(conf.items(), key=lambda p: -p[1])[:20],
        "per_chunk": per_chunk,
    }


def gates_ok(dim: str, rep: dict) -> list[str]:
    """Vigorous gates: provideChunk must match Anvil after populate free-pass."""
    fails = []
    if rep["chunks_ok"] < 1:
        fails.append("no chunks compared")
        return fails
    # Occupancy (solid/air) after free-pass: essentially perfect
    if rep["occupancy"] < 0.9995:
        fails.append(f"occupancy {rep['occupancy']:.6f} < 0.9995")
    # Terrain+base exact after free-pass. Allow a handful of cells for live-save
    # teleporter pads / player-touched blocks (portal E2E may leave 87→0 at 0,0).
    # Allow small live-save residue (teleporter pads / E2E tower near spawn).
    if rep["exact"] < 0.9999 or rep["mismatches"] > 64:
        fails.append(
            f"exact {rep['exact']:.6f} mism={rep['mismatches']} "
            f"(need exact>=0.9999 and mism<=64) top={rep['top_mism'][:5]}"
        )
    if rep["chunks_missing"] > 0:
        fails.append(f"missing {rep['chunks_missing']} MC chunks")
    return fails


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dim", required=True, choices=("nether", "end"))
    ap.add_argument("--region", type=Path, required=True)
    ap.add_argument("--c-mcbd", type=Path, default=None,
                    help="prebuilt C dump; if omitted, build+dump on the fly")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--cx0", type=int, default=-2)
    ap.add_argument("--cz0", type=int, default=-2)
    ap.add_argument("--cx1", type=int, default=2)
    ap.add_argument("--cz1", type=int, default=2)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--gate", action="store_true", default=True)
    ap.add_argument("--no-gate", action="store_false", dest="gate")
    args = ap.parse_args(argv)

    if not args.region.is_dir():
        print(f"FAIL region dir missing: {args.region}", file=sys.stderr)
        return 2

    tmp = Path(tempfile.mkdtemp(prefix="dim_wg_"))
    dumper = build_dumper(tmp / "dim_worldgen_dump")
    c_mcbd = args.c_mcbd
    if c_mcbd is None:
        c_mcbd = tmp / f"c_{args.dim}.mcbd"
        dump_c(dumper, args.dim, args.seed, args.cx0, args.cz0, args.cx1, args.cz1, c_mcbd)

    c_chunks = load_c_mcbd(c_mcbd, args.cx0, args.cz0, args.cx1, args.cz1)
    rep = compare_window(args.dim, args.region, c_chunks,
                         args.cx0, args.cz0, args.cx1, args.cz1)
    print(f"== {args.dim} seed={args.seed} window=({args.cx0},{args.cz0})..({args.cx1},{args.cz1}) ==")
    print(f"  chunks_ok={rep['chunks_ok']} missing={rep['chunks_missing']}")
    print(f"  exact-match {100*rep['exact']:.4f}%  occupancy {100*rep['occupancy']:.4f}%  "
          f"mism={rep['mismatches']} / {rep['cells']}")
    if rep["top_mism"]:
        print("  residual (java->c) pairs:")
        for (j, c), n in rep["top_mism"][:10]:
            print(f"    {j:4d} -> {c:4d} : {n}")
    for ch in rep["per_chunk"]:
        if "error" in ch:
            print(f"  ({ch['cx']},{ch['cz']}) MISSING {ch['error']}")
        elif ch["mism"]:
            print(f"  ({ch['cx']:3d},{ch['cz']:3d}) exact={ch['exact_pct']:7.3f}% "
                  f"occ={ch['occ_pct']:7.3f}% mism={ch['mism']}")

    if args.json:
        # jsonify top_mism keys
        out = dict(rep)
        out["top_mism"] = [{"java": a, "c": b, "n": n} for (a, b), n in rep["top_mism"]]
        args.json.write_text(json.dumps(out, indent=2))

    if args.gate:
        fails = gates_ok(args.dim, rep)
        if fails:
            print("FAIL gates:", "; ".join(fails))
            return 1
        print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
