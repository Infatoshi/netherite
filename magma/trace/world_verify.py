#!/usr/bin/env python3
"""world_verify.py - the HARSH worldgen verifier: every wrong cell, listed.

Ground truth = the REAL Java Minecraft 1.11.2 save (.mca region files, old numeric
block format). Magma side = trace/world_dump (cp_provide_chunk terrain + owr_run
decoration), invoked HERE in small tiles so the allocate-once light pool (D = 2R+3 = 19
chunks) never evicts a requested chunk (world_dump's centered ensure window must stay
inside the pool diameter; tiles of 6x6 ensure a 13x13 window, safe).

It auto-discovers every TerrainPopulated chunk in the save, verifies only FULLY
decorated chunks (a chunk's cells receive decoration from the populate passes of
chunks (cx-1..cx, cz-1..cz), so all four must be populated -- otherwise the java side
is legitimately missing decoration and the diff would lie), and emits:

  1. trace/out/wv_mismatch_s<seed>.csv  -- EVERY wrong cell:
       wx,wy,wz,java_id,magma_id      (world block coords, vanilla numeric ids)
  2. trace/out/wv_blobs_s<seed>.txt     -- mismatch cells clustered into connected
       blobs (26-connectivity), sorted by size: one line per blob with bbox, cell
       count, and the dominant (java,magma) id pair. A misplaced tree = one blob.
  3. stdout summary: per-chunk match %, aggregate %, top id-pair confusion, y-histogram.

Usage:
  uv run --no-project --with numpy --with nbt python3 trace/world_verify.py \
      --region <save>/region --seed 0 [--tile 6] [--limit-blobs 40]
Exit code 0 iff 100.000% match.
"""
import argparse
import glob
import io
import os
import struct
import subprocess
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MAGMA = os.path.dirname(HERE)
sys.path.insert(0, HERE)

# reuse the existing readers/mapping (single source of truth for PB->vanilla).
import importlib.util
_spec = importlib.util.spec_from_file_location("world_diff", os.path.join(HERE, "world_diff.py"))
world_diff = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(world_diff)
read_mca_chunk = world_diff.read_mca_chunk
read_magma = world_diff.read_magma
pb_to_vanilla_arr = world_diff.pb_to_vanilla_arr

BLOCK_NAMES = {
    0: "air", 1: "stone", 2: "grass", 3: "dirt", 7: "bedrock", 8: "flowing_water",
    9: "water", 10: "flowing_lava", 11: "lava", 12: "sand", 13: "gravel", 14: "gold_ore",
    15: "iron_ore", 16: "coal_ore", 17: "log", 18: "leaves", 21: "lapis_ore", 24: "sandstone",
    31: "tallgrass", 32: "deadbush", 37: "yellow_flower", 38: "red_flower", 39: "brown_mushroom",
    40: "red_mushroom", 56: "diamond_ore", 73: "redstone_ore", 78: "snow_layer", 79: "ice",
    81: "cactus", 82: "clay", 83: "reeds", 86: "pumpkin", 99: "brown_mushroom_block",
    100: "red_mushroom_block", 106: "vine", 110: "mycelium", 111: "waterlily",
    127: "cocoa", 129: "emerald_ore", 161: "leaves2", 162: "log2", 175: "double_plant",
    179: "red_sandstone",
}


def bname(i):
    return f"{i}:{BLOCK_NAMES.get(i, '?')}"


def scan_populated(region_dir):
    """Return {(cx,cz): populated_bool} for every chunk present in the save."""
    out = {}
    for rf in glob.glob(os.path.join(region_dir, "r.*.mca")):
        parts = os.path.basename(rf).split(".")
        rx, rz = int(parts[1]), int(parts[2])
        with open(rf, "rb") as f:
            data = f.read()
        for i in range(1024):
            off = struct.unpack(">I", b"\0" + data[i * 4:i * 4 + 3])[0]
            if off == 0:
                continue
            cx, cz = rx * 32 + (i % 32), rz * 32 + (i // 32)
            try:
                sec = data[off * 4096:]
                ln = struct.unpack(">I", sec[:4])[0]
                raw = zlib.decompress(sec[5:4 + ln])
                from nbt import nbt as _nbt
                n = _nbt.NBTFile(buffer=io.BytesIO(raw))
                lvl = n["Level"]
                tp = int(lvl["TerrainPopulated"].value) if "TerrainPopulated" in lvl else 0
                out[(cx, cz)] = bool(tp)
            except Exception:
                out[(cx, cz)] = False
    return out


def fully_decorated(pop):
    """Chunks whose cells are FINAL in the save: (cx,cz) and the three populate
    neighbors (cx-1,cz),(cx,cz-1),(cx-1,cz-1) AND (cx+1,*),(*,cz+1) donors:
    a cell in chunk C receives decoration from populate passes of chunks
    (C.x-1..C.x, C.z-1..C.z). All four of those must be populated."""
    full = set()
    for (cx, cz), p in pop.items():
        if not p:
            continue
        if all(pop.get((cx + dx, cz + dz), False) for dx in (-1, 0) for dz in (-1, 0)):
            full.add((cx, cz))
    return full


def build_prep_list(region_dir, pop, outdir, java_log=None):
    """Write the base-chunk list in VANILLA POPULATE ORDER for --prep-list.

    Spawn prep square: MinecraftServer.initialWorldChunkLoad loads chunks
    (spawn +-192 blocks) ascending x-outer z-inner; Chunk.populate fires for (x,z)
    once (x+1,z),(x,z+1),(x+1,z+1) exist, so square populate order == raster.
    Later player-loaded chunks: approximated by distance to spawn (PlayerChunkMap
    orders by distance to the player, who starts at spawn)."""
    populated_set = {c for c, p in pop.items() if p}
    # Preferred: the EXACT populate order recorded live by qrl/WorldGenProbe.java
    # during this save's world creation (PRE lines, first occurrence per chunk).
    probe_log = java_log or os.path.join(outdir, "java_genprobe.log")
    if os.path.exists(probe_log):
        order, seen = [], set()
        with open(probe_log) as f:
            for line in f:
                p = line.split()
                # len >= 5: newer probe logs append a trailing T<ticks> stamp
                if len(p) >= 5 and p[0] == "PRE":
                    key = (int(p[1]), int(p[2]))
                    if key not in seen and key in populated_set:
                        seen.add(key)
                        order.append(key)
        missing = populated_set - seen
        if not missing:
            path = os.path.join(outdir, "wv_prep_list.txt")
            with open(path, "w") as f:
                for cx, cz in order:
                    f.write(f"{cx} {cz}\n")
            print(f"  prep list: {len(order)} bases in RECORDED populate order (genprobe log)")
            return path, build_cascade_events(probe_log, outdir)
        print(f"  genprobe log misses {len(missing)} populated chunks; falling back to derived order")
    lvl = os.path.join(os.path.dirname(os.path.abspath(region_dir)), "level.dat")
    if not os.path.exists(lvl):
        print("  (no level.dat next to region dir; skipping cumulative prep)")
        return None, None
    from nbt import nbt as _nbt
    d = _nbt.NBTFile(lvl)["Data"]
    scx, scz = int(d["SpawnX"].value) >> 4, int(d["SpawnZ"].value) >> 4
    populated = {c for c, p in pop.items() if p}
    order, seen = [], set()
    for cx in range(scx - 12, scx + 12):
        for cz in range(scz - 12, scz + 12):
            if (cx, cz) in populated:
                order.append((cx, cz))
                seen.add((cx, cz))
    order += sorted(populated - seen,
                    key=lambda c: ((c[0] - scx) ** 2 + (c[1] - scz) ** 2, c))
    path = os.path.join(outdir, "wv_prep_list.txt")
    with open(path, "w") as f:
        for cx, cz in order:
            f.write(f"{cx} {cz}\n")
    print(f"  prep list: {len(order)} bases in populate order (spawn chunk ({scx},{scz}))")
    return path, None


POPULATE_TAGS = {"PRE", "POP", "DECPRE", "DEC", "ORE", "DECPOST", "POST"}


def build_cascade_events(probe_log, outdir):
    """Mine population-cascade RNG-clobber events from the recorded genprobe log.

    A GEN line (proxy around ChunkProviderOverworld.provideChunk) inside another
    chunk's PRE..POST window means a decorate-stage block touch generated a chunk
    mid-populate, reseeding the SHARED provider rand. The parent resumes with the
    rand state of the LAST log line before its own next line (covers chained GENs
    and nested populates). Emits "pcx pcz cursorBefore cursorResume" per event."""
    lines = []
    with open(probe_log) as f:
        for line in f:
            p = line.split()
            if len(p) >= 5:
                lines.append(p)
    events, stack = [], []
    for i, p in enumerate(lines):
        tag = p[0]
        key = (int(p[1]), int(p[2]))
        if tag == "PRE":
            stack.append(key)
        elif tag == "POST":
            if stack and stack[-1] == key:
                stack.pop()
        elif tag == "GEN" and stack:
            # The interruption ends when, back at the parent's stack depth, a
            # decorator/populate event fires: java's DecorateBiomeEvent pos comes
            # from the per-biome SHARED BiomeDecorator.chunkPos, so the parent's
            # resumed events are MIS-TAGGED with the nested chunk's coords - only
            # depth tracking identifies the boundary (never trust the tag here).
            parent = stack[-1]
            resume = int(p[4])
            nested = (-9999999, -9999999)   # POPMC_CASCADE_NONE
            depth = 0
            for q in lines[i + 1:]:
                if q[0] == "PRE":
                    depth += 1
                    if depth == 1 and nested[0] == -9999999:
                        nested = (int(q[1]), int(q[2]))   # cascade-populated chunk
                elif q[0] == "POST":
                    if depth == 0:
                        break                 # parent's own POST: interruption over
                    depth -= 1
                elif depth == 0 and q[0] in ("POP", "DECPRE", "DEC", "ORE", "DECPOST",
                                             "TREEATT", "TREEOK", "TREENO"):
                    break                     # parent resumed decorating
                if q[0] != "LOAD":
                    resume = int(q[4])
            events.append((parent[0], parent[1], int(p[3]), resume, nested[0], nested[1]))
    events = list(dict.fromkeys(events))
    if not events:
        return None
    path = os.path.join(outdir, "wv_cascade.txt")
    with open(path, "w") as f:
        for e in events:
            f.write(f"{e[0]} {e[1]} {e[2]} {e[3]} {e[4]} {e[5]}\n")
    print(f"  cascade: {len(events)} mid-populate provider-rand clobber events (GEN lines)")
    return path


def dump_tiles(seed, chunks, tile, outdir, preplist=None, cascade=None):
    """Run world_dump covering `chunks`; return {(cx,cz): (blk,bio)}.
    With a prep list, ONE invocation dumps the whole bounding box so the cumulative
    window pool is built exactly once (world_dump enlarges its pools to fit)."""
    dump_bin = os.path.join(MAGMA, "trace", "world_dump")
    if not os.path.exists(dump_bin):
        sys.exit(f"missing {dump_bin} (build it: see trace/world_dump.c header)")
    if preplist:
        xs = [c[0] for c in chunks]
        zs = [c[1] for c in chunks]
        cx0, cz0 = min(xs), min(zs)
        ncx, ncz = max(xs) - cx0 + 1, max(zs) - cz0 + 1
        out = os.path.join(outdir, "wv_full.bin")
        cmd = [dump_bin, "--seed", str(seed), "--cx0", str(cx0), "--cz0", str(cz0),
               "--ncx", str(ncx), "--ncz", str(ncz), "--out", out,
               "--prep-list", preplist]
        if cascade:
            cmd += ["--cascade", cascade]
        print(f"  [dump] single cumulative dump ({cx0},{cz0})+{ncx}x{ncz}", flush=True)
        r = subprocess.run(cmd, cwd=MAGMA, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"world_dump failed:\n{r.stderr}")
        for line in r.stderr.splitlines():
            print("  " + line)
        _, ch = read_magma(out)
        os.remove(out)
        return ch
    tiles = {}
    for (cx, cz) in chunks:
        tiles.setdefault((cx // tile, cz // tile), []).append((cx, cz))
    merged = {}
    for n, ((tx, tz), members) in enumerate(sorted(tiles.items())):
        cx0, cz0 = tx * tile, tz * tile
        out = os.path.join(outdir, f"wv_tile_{tx}_{tz}.bin")
        cmd = [dump_bin, "--seed", str(seed), "--cx0", str(cx0), "--cz0", str(cz0),
               "--ncx", str(tile), "--ncz", str(tile), "--out", out]
        r = subprocess.run(cmd, cwd=MAGMA, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"world_dump failed for tile ({tx},{tz}):\n{r.stderr}")
        _, ch = read_magma(out)
        merged.update(ch)
        os.remove(out)
        print(f"  [dump {n+1}/{len(tiles)}] tile ({tx},{tz}) chunks ({cx0},{cz0})+{tile}x{tile}", flush=True)
    return merged


def cluster_blobs(cells):
    """Union-find 26-connectivity clustering of mismatch cells.
    cells: list of (wx,wy,wz,ji,ci). Returns list of blobs (list of cells)."""
    index = {(c[0], c[1], c[2]): k for k, c in enumerate(cells)}
    parent = list(range(len(cells)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for k, (x, y, z, _, _) in enumerate(cells):
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx == dy == dz == 0:
                        continue
                    j = index.get((x + dx, y + dy, z + dz))
                    if j is not None:
                        union(k, j)
    groups = {}
    for k in range(len(cells)):
        groups.setdefault(find(k), []).append(cells[k])
    return sorted(groups.values(), key=len, reverse=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", required=True)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--tile", type=int, default=6)
    ap.add_argument("--limit-blobs", type=int, default=40)
    ap.add_argument("--out-dir", default=os.path.join(MAGMA, "trace", "out"))
    ap.add_argument("--max-chunks", type=int, default=0, help="0 = all")
    ap.add_argument("--no-prep", action="store_true",
                    help="disable the cumulative populate-order prep (old per-tile mode)")
    ap.add_argument("--java-log", default=None,
                    help="recorded genprobe log for THIS save (populate order + cascade GEN "
                         "events); default: <out-dir>/java_genprobe.log")
    args = ap.parse_args()

    print(f"[1/4] scan save {args.region}")
    pop = scan_populated(args.region)
    full = fully_decorated(pop)
    print(f"  chunks in save: {len(pop)}  populated: {sum(pop.values())}  "
          f"fully-decorated (verifiable): {len(full)}")
    chunks = sorted(full)
    if args.max_chunks:
        chunks = chunks[: args.max_chunks]

    print(f"[2/4] magma dump ({len(chunks)} chunks, tile={args.tile})")
    prep, casc = (None, None) if args.no_prep else build_prep_list(
        args.region, pop, args.out_dir, java_log=args.java_log)
    cr = dump_tiles(args.seed, chunks, args.tile, args.out_dir, preplist=prep, cascade=casc)

    print(f"[3/4] per-cell diff")
    mism = []           # (wx,wy,wz,ji,ci)
    biome_bad = []      # (wx,wz,jb,cb)
    tot = mat = 0
    per_chunk = []
    for (cx, cz) in chunks:
        jblk, jbio = read_mca_chunk(args.region, cx, cz)
        crblk = pb_to_vanilla_arr(cr[(cx, cz)][0])
        crbio = cr[(cx, cz)][1]
        eq = (crblk == jblk)
        tot += eq.size
        nm = int(eq.sum())
        mat += nm
        per_chunk.append((cx, cz, 100.0 * nm / eq.size))
        xs, ys, zs = np.where(~eq)
        for x, y, z in zip(xs.tolist(), ys.tolist(), zs.tolist()):
            mism.append((cx * 16 + x, y, cz * 16 + z, int(jblk[x, y, z]), int(crblk[x, y, z])))
        if jbio is not None:
            bx, bz = np.where(crbio != jbio)
            for x, z in zip(bx.tolist(), bz.tolist()):
                biome_bad.append((cx * 16 + x, cz * 16 + z, int(jbio[x, z]), int(crbio[x, z])))

    csv_path = os.path.join(args.out_dir, f"wv_mismatch_s{args.seed}.csv")
    with open(csv_path, "w") as f:
        f.write("wx,wy,wz,java_id,magma_id\n")
        for wx, wy, wz, ji, ci in sorted(mism):
            f.write(f"{wx},{wy},{wz},{ji},{ci}\n")

    print(f"[4/4] cluster + report")
    blobs = cluster_blobs(mism)
    blob_path = os.path.join(args.out_dir, f"wv_blobs_s{args.seed}.txt")
    with open(blob_path, "w") as f:
        for b in blobs:
            xs = [c[0] for c in b]; ys = [c[1] for c in b]; zs = [c[2] for c in b]
            pairs = {}
            for c in b:
                pairs[(c[3], c[4])] = pairs.get((c[3], c[4]), 0) + 1
            dom = max(pairs.items(), key=lambda p: p[1])[0]
            f.write(f"{len(b):5d} cells  bbox x[{min(xs)},{max(xs)}] y[{min(ys)},{max(ys)}] "
                    f"z[{min(zs)},{max(zs)}]  dominant java={bname(dom[0])} magma={bname(dom[1])}  "
                    f"pairs={{{', '.join(f'{bname(j)}->{bname(c)}:{n}' for (j,c),n in sorted(pairs.items(), key=lambda p:-p[1]))}}}\n")

    pct = 100.0 * mat / tot if tot else 0.0
    print("=" * 78)
    print(f"VERDICT seed={args.seed}: {mat}/{tot} cells exact = {pct:.4f}%   "
          f"mismatches={tot-mat}  blobs={len(blobs)}  biome_bad_cols={len(biome_bad)}/{len(chunks)*256}")
    worst = sorted(per_chunk, key=lambda p: p[2])[:8]
    print("worst chunks: " + ", ".join(f"({cx},{cz}) {p:.2f}%" for cx, cz, p in worst))

    confusion = {}
    yhist = {}
    for _, wy, _, ji, ci in mism:
        confusion[(ji, ci)] = confusion.get((ji, ci), 0) + 1
        yhist[wy] = yhist.get(wy, 0) + 1
    print("\ntop id-pair confusion (java -> magma):")
    for (ji, ci), n in sorted(confusion.items(), key=lambda p: -p[1])[:15]:
        print(f"  {bname(ji):>24} -> {bname(ci):<24} : {n}")
    if yhist:
        lo, hi = min(yhist), max(yhist)
        print(f"\nmismatch y-range: [{lo},{hi}]  "
              f"(top y levels: {sorted(yhist.items(), key=lambda p:-p[1])[:6]})")
    print(f"\nlargest blobs (first {args.limit_blobs}; full list in {blob_path}):")
    with open(blob_path) as f:
        for i, line in enumerate(f):
            if i >= args.limit_blobs:
                break
            print("  " + line.rstrip())
    if biome_bad:
        print(f"\nbiome mismatches (first 10): {biome_bad[:10]}")
    print(f"\nfull cell list: {csv_path}")
    return 0 if mat == tot else 1


if __name__ == "__main__":
    sys.exit(main())
