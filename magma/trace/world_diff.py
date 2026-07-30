#!/usr/bin/env python3
"""world_diff.py - WORLDGEN INTEGRATION VERIFIER for magma vs the REAL Minecraft.

Ground truth = the REAL Java Minecraft 1.11.2 game's SAVED world (seed 0, default
generator), parsed BLOCK-FOR-BLOCK from its Anvil .mca region files. NOT a blaze
self-golden. Magma's side = trace/world_dump.c output (blaze cp_provide_chunk base
terrain + owr_run decoration, exactly what world/light.c gen_chunk feeds the mesher).

For chunks (cx,cz) in a range around origin it reports, per chunk:
  - exact-match % of vanilla block ids over all 16x256x16 cells,
  - a mismatch classification: BASE TERRAIN / SURFACE / DECORATION / (tree subset),
  - biome-per-column agreement (magma voronoi vs the game's stored Biomes),
  - TREE-block counts (logs=17, leaves=18/161) MC vs magma.

The real 1.11.2 save uses the OLD numeric block format (Sections[].Blocks + Data +
optional Add nibble), which anvil-parser (1.13+ palette) can NOT read; we decode it
directly from the NBT via the `nbt` package.

Usage (uv brings numpy + nbt):
  uv run --no-project --with numpy --with nbt python3 trace/world_diff.py \
      --region <save>/region --magma trace/out/magma_world_s0.bin \
      --cx0 0 --cz0 0 --ncx 3 --ncz 3
"""
import argparse
import struct
import sys
import numpy as np

# ---- PB/CB small-int code -> TRUE vanilla numeric block id ------------------
# CB (0..20) == PB (0..20); PB 21+ are feature blocks. Values are vanilla ids.
PB2VANILLA = {
    0: 0, 1: 1, 2: 9, 3: 2, 4: 3, 5: 7, 6: 13, 7: 12, 8: 24, 9: 179, 10: 79,
    11: 11, 12: 10, 13: 8, 14: 111, 15: 110, 16: 78, 17: 172, 18: 159, 19: 3, 20: 3,
    21: 1, 22: 1, 23: 1,                       # granite/diorite/andesite -> stone
    24: 16, 25: 15, 26: 14, 27: 73, 28: 56, 29: 21, 30: 82,   # ores + clay
    31: 17, 32: 17, 33: 17, 37: 17, 38: 17,    # logs -> 17
    34: 18, 35: 18, 36: 18,                     # leaves -> 18
    39: 31, 40: 31, 41: 32,                     # tallgrass/fern -> 31, deadbush -> 32
    42: 39, 43: 40, 44: 83,                     # mushrooms, reeds(sugarcane 83)
    45: 4, 46: 48, 47: 52, 48: 216,             # cobble/mossy/spawner/bone
    49: 54, 50: 37,                             # chest, dandelion(yellow flower 37)
    75: 129, 76: 97,                            # emerald ore, monster egg (silverfish)
    77: 162, 78: 161,                           # dark oak log (log2), dark oak leaves (leaves2)
    79: 99, 80: 100,                            # brown/red mushroom block
    81: 81, 82: 162, 83: 161, 84: 44,           # cactus, acacia log/leaves, sandstone slab
    85: 17, 86: 18, 87: 103, 88: 127,           # jungle log/leaves, melon, cocoa
    89: 49,                                     # obsidian (spring lava-water mixing)
}
for m in range(51, 60):   # PB_RED_FLOWER_BASE + meta -> poppy family (id 38)
    PB2VANILLA[m] = 38
for m in range(60, 67):   # double plant lower+upper (id 175)
    PB2VANILLA[m] = 175
for m in range(67, 71):   # pumpkin (id 86)
    PB2VANILLA[m] = 86
for m in range(71, 75):   # vine (id 106)
    PB2VANILLA[m] = 106
for m in range(120, 136):  # CB_STAINED_CLAY_BASE + EnumDyeColor meta -> stained clay (id 159)
    PB2VANILLA[m] = 159


def pb_to_vanilla_arr(a):
    out = np.zeros_like(a, dtype=np.int32)
    for k, v in PB2VANILLA.items():
        out[a == k] = v
    # any unmapped nonzero code -> stone-ish fallback (shouldn't happen at origin)
    unmapped = (out == 0) & (a != 0)
    if unmapped.any():
        out[unmapped] = 1
    return out


# ---- block-id category sets (vanilla numeric) ------------------------------
TREE_IDS = {17, 18, 161}                        # log, leaves, leaves2
DECOR_IDS = TREE_IDS | {
    31, 32, 37, 38, 39, 40, 83, 86, 106, 111, 175, 78, 6, 81, 54, 52, 216,
    103, 127,  # melon, cocoa
}
# blocks that legitimately sit at/near the surface (top layer disagreements)
SURFACE_IDS = {2, 3, 12, 13, 24, 179, 110, 82, 159, 172, 1}  # grass/dirt/sand/gravel/sandstone/mycelium/clay


def classify(java_id, cr_id):
    if java_id in TREE_IDS or cr_id in TREE_IDS:
        return "TREE"
    if java_id in DECOR_IDS or cr_id in DECOR_IDS:
        return "DECORATION"
    # water/lava presence differences are lakes/springs -> decoration-ish
    if java_id in (8, 9, 10, 11) or cr_id in (8, 9, 10, 11):
        return "FLUID"
    if java_id in SURFACE_IDS or cr_id in SURFACE_IDS:
        return "SURFACE"
    return "BASE"


# ---- OLD-format Anvil .mca reader (1.11.2) ---------------------------------
def read_mca_chunk(region_dir, cx, cz):
    """Return (blocks[16,256,16] vanilla id int32 indexed [x,y,z], biomes[16,16]
    indexed [x,z]) for chunk (cx,cz), or (None,None) if not present."""
    from nbt.region import RegionFile
    rx, rz = cx >> 5, cz >> 5
    rf = RegionFile(f"{region_dir}/r.{rx}.{rz}.mca")
    ch = rf.get_chunk(cx & 31, cz & 31)  # raises InconceivedChunk if absent
    level = ch["Level"]
    blocks = np.zeros((16, 256, 16), dtype=np.int32)  # [x,y,z]
    for sec in level["Sections"]:
        secY = int(sec["Y"].value)
        raw = np.frombuffer(bytes(sec["Blocks"].value), dtype=np.uint8).astype(np.int32)
        ids = raw.copy()
        if "Add" in sec:
            add = np.frombuffer(bytes(sec["Add"].value), dtype=np.uint8)
            hi = np.zeros(4096, dtype=np.int32)
            hi[0::2] = (add & 0x0F)
            hi[1::2] = (add >> 4)
            ids += (hi << 8)
        # section layout is YZX: idx = y*256 + z*16 + x
        ids = ids.reshape(16, 16, 16)          # [y,z,x]
        ids = np.transpose(ids, (2, 0, 1))     # -> [x,y,z]
        y0 = secY * 16
        blocks[:, y0:y0 + 16, :] = ids
    biomes = None
    if "Biomes" in level:
        bio = np.frombuffer(bytes(level["Biomes"].value), dtype=np.uint8).astype(np.int32)
        biomes = bio.reshape(16, 16).T  # stored z*16+x -> [x,z]
    return blocks, biomes


# ---- magma binary reader -------------------------------------------------
def read_magma(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"CRWD", "bad magic"
        seed = struct.unpack("<q", f.read(8))[0]
        cx0, cz0, ncx, ncz = struct.unpack("<iiii", f.read(16))
        chunks = {}
        for ix in range(ncx):
            for iz in range(ncz):
                blk = np.frombuffer(f.read(16 * 256 * 16 * 2), dtype=np.uint16)
                bio = np.frombuffer(f.read(16 * 16 * 4), dtype=np.int32)
                # blk index lx*4096 + lz*256 + y -> [x,y,z]
                blk = blk.reshape(16, 16, 256)      # [x,z,y]
                blk = np.transpose(blk, (0, 2, 1))  # -> [x,y,z]
                bio = bio.reshape(16, 16)           # [x,z] (index lx*16+lz)
                chunks[(cx0 + ix, cz0 + iz)] = (blk.astype(np.int32), bio)
    return seed, chunks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", required=True, help="save/region dir with r.*.mca")
    ap.add_argument("--magma", required=True, help="world_dump.c .bin output")
    ap.add_argument("--cx0", type=int, default=0)
    ap.add_argument("--cz0", type=int, default=0)
    ap.add_argument("--ncx", type=int, default=3)
    ap.add_argument("--ncz", type=int, default=3)
    args = ap.parse_args()

    seed, cr_chunks = read_magma(args.magma)
    print(f"magma seed={seed}, {len(cr_chunks)} chunks loaded")

    totcells = 0
    totmatch = 0
    cat_tot = {}
    confusion = {}
    tree_mc_total = 0
    tree_cr_total = 0
    per_chunk = []

    for cz in range(args.cz0, args.cz0 + args.ncz):
        for cx in range(args.cx0, args.cx0 + args.ncx):
            try:
                jblk, jbio = read_mca_chunk(args.region, cx, cz)
            except Exception as e:
                print(f"  chunk ({cx},{cz}): MC chunk MISSING ({e})")
                continue
            if (cx, cz) not in cr_chunks:
                print(f"  chunk ({cx},{cz}): magma missing")
                continue
            crblk_raw, crbio = cr_chunks[(cx, cz)]
            crblk = pb_to_vanilla_arr(crblk_raw)

            match = (crblk == jblk)
            ncells = crblk.size
            nmatch = int(match.sum())
            totcells += ncells
            totmatch += nmatch

            tree_mc = int(np.isin(jblk, list(TREE_IDS)).sum())
            tree_cr = int(np.isin(crblk, list(TREE_IDS)).sum())
            tree_mc_total += tree_mc
            tree_cr_total += tree_cr

            # biome column agreement
            bmatch = int((crbio == jbio).sum()) if jbio is not None else -1
            bio_mc_set = sorted(set(jbio.flatten().tolist())) if jbio is not None else []
            bio_cr_set = sorted(set(crbio.flatten().tolist()))

            # classify mismatches
            cat = {}
            xs, ys, zs = np.where(~match)
            for x, y, z in zip(xs.tolist(), ys.tolist(), zs.tolist()):
                ji = int(jblk[x, y, z]); ci = int(crblk[x, y, z])
                c = classify(ji, ci)
                cat[c] = cat.get(c, 0) + 1
                cat_tot[c] = cat_tot.get(c, 0) + 1
                confusion[(ji, ci)] = confusion.get((ji, ci), 0) + 1

            per_chunk.append((cx, cz, ncells, nmatch, tree_mc, tree_cr,
                              bmatch, bio_mc_set, bio_cr_set, cat))
            pct = 100.0 * nmatch / ncells
            print(f"  chunk ({cx},{cz}): exact-match {pct:6.2f}%  "
                  f"trees MC={tree_mc:5d} magma={tree_cr:5d}  "
                  f"biome cols match={bmatch}/256 "
                  f"MC_biomes={bio_mc_set} magma_biomes={bio_cr_set}")
            if cat:
                cats = ", ".join(f"{k}={v}" for k, v in sorted(cat.items(), key=lambda p:-p[1]))
                print(f"              mismatch cats: {cats}")

    print("\n" + "=" * 78)
    print("AGGREGATE over %d chunks" % len(per_chunk))
    if totcells:
        print(f"  total cells={totcells}  exact-match={100.0*totmatch/totcells:.3f}%  "
              f"mismatches={totcells-totmatch}")
    print(f"  TREE blocks (17/18/161): MC={tree_mc_total}  magma={tree_cr_total}")
    print("  mismatch categories:", ", ".join(
        f"{k}={v}" for k, v in sorted(cat_tot.items(), key=lambda p:-p[1])))
    print("\n  top (java_id -> magma_id) mismatch pairs:")
    for (ji, ci), n in sorted(confusion.items(), key=lambda p:-p[1])[:20]:
        print(f"    java {ji:4d} -> magma {ci:4d} : {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
