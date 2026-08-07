/* chunk_provider: REAL-CHUNK overworld pipeline = MC 1.11.2
 * ChunkProviderOverworld.provideChunk (net/minecraft/world/gen/ChunkProviderOverworld.java),
 * MINUS structures and MINUS populate/decoration. Composes the already-verified isolated kernels
 * into one ChunkPrimer for chunk (chunkX,chunkZ), bitwise-matching verbatim Java.
 *
 * Pipeline (provideChunk order, settings = ChunkProviderSettings.Factory defaults, WorldType.DEFAULT):
 *   1. this.rand.setSeed(x*341873128712 + z*132897987541)
 *   2. setBlocksInChunk: getBiomesForGeneration (genBiomes/rivermix, 10x10) -> generateHeightmap
 *      (REAL per-biome baseHeight/heightVariation blend) -> density -> STONE / oceanBlock(WATER).
 *   3. getBiomes (biomeIndexLayer/voronoi, 16x16, full-res) -> replaceBiomeBlocks: surfaceNoise
 *      depthBuffer + per-column genTerrainBlocks (REAL per-biome top/filler/temperature, with the
 *      Biome.generateBiomeTerrain layering and the Hills/Taiga/Swamp genTerrainBlocks overrides).
 *   4. caveGenerator.generate (MapGenCaves), 5. ravineGenerator.generate (MapGenRavine) - carve the
 *      REAL primer with REAL per-column biomes.
 *
 * REUSES the verified kernels: core/mc_noise.h (octaves), core/mc_math.h (sin table), core/
 * terrain_shape.h (TerrainNoise + ctor draw order + mc_clamped_lerp), core/genlayer_biomes.h
 * (full GenLayer stack: genBiomes=rivermix=nodes[voronoi].parent, biomeIndexLayer=voronoi).
 *
 * THE KEY NEW PIECE: a REAL biome property table keyed by biome id (cb_* below), built verbatim
 * from net/minecraft/world/biome/Biome.registerBiomes literals + the Biome subclass constructors.
 * It replaces the Plains-only shortcut every isolated kernel used. IDENTICAL table + dispatch in
 * the Java golden. Block-state ids are the sanctioned small-int substitution (CB_* below), unified
 * across the whole pipeline (one primer flows through all stages), identical in golden + candidate.
 *
 * SCOPE EXCLUDED (next integration increment, documented): all structure generators (mapFeatures
 * disabled -> those branches skipped exactly as vanilla skips them), `new Chunk(...)`,
 * generateSkylightMap, the biome byte array, and populate()/decoration (ores/trees/lakes).
 *
 * Vanilla quirk replicated faithfully: Biome.topBlock/fillerBlock are MUTABLE per-biome singleton
 * fields; BiomeHills.genTerrainBlocks rewrites them per column, so after replaceBiomeBlocks each
 * biome's top/filler hold the LAST processed column's value, which caves/ravines digBlock/isTopBlock
 * then read. Modeled here by per-biome-id curTop[]/curFiller[] arrays (same in golden + candidate).
 *
 * C-vs-Java traps: ordered temporaries around repeated RNG; no a[i]=i++; float vs double (the
 * heightmap blend is float, MathHelper.sin is the table float); 32/64-bit widths; the per-chunk
 * MapGenBase setSeed 64-bit wrap; ChunkPrimer writes guarded in-range. Build -ffp-contract=off /
 * --fmad=false. */
#ifndef MC_CHUNK_PROVIDER_H
#define MC_CHUNK_PROVIDER_H

#include <math.h>
#ifndef __CUDA_ARCH__
#include <stdio.h>
#include <stdlib.h>
#endif
#include "mc.h"
#include "mc_rng.h"
#include "mc_noise.h"
#include "mc_math.h"
#include "terrain_shape.h"     /* TerrainNoise, terrain_noise_init, mc_clamped_lerp */
#include "genlayer_biomes.h"   /* gl_build, gl_getInts; genBiomes = nodes[voronoi].parent */

/* ===== unified block-state id substitution (identical in golden + candidate) ===== */
enum {
    CB_AIR = 0, CB_STONE = 1, CB_WATER = 2, CB_GRASS = 3, CB_DIRT = 4, CB_BEDROCK = 5,
    CB_GRAVEL = 6, CB_SAND = 7, CB_SANDSTONE = 8, CB_RED_SANDSTONE = 9, CB_ICE = 10,
    CB_LAVA = 11,           /* still lava (Blocks.LAVA) - cave digBlock */
    CB_FLOWING_LAVA = 12,   /* Blocks.FLOWING_LAVA - ravine digBlock */
    CB_FLOWING_WATER = 13,  /* predicate only (not produced) */
    CB_WATER_LILY = 14,     /* swamp */
    CB_MYCELIUM = 15, CB_SNOW_LAYER = 16, CB_HARDENED_CLAY = 17, CB_STAINED_HARDENED_CLAY = 18,
    CB_PODZOL = 19, CB_COARSE_DIRT = 20,  /* mega-taiga tops */
    CB_STAINED_CLAY_BASE = 120            /* + EnumDyeColor meta, for Mesa bands */
};

#define CB_SEA_LEVEL 63

/* ===== ChunkPrimer (char[65536], index = x<<12 | z<<8 | y) ===== */
typedef struct { u16 data[65536]; } ChunkPrimer;
MC_HD static inline int  cb_index(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD static inline int  cb_get(const ChunkPrimer *p, int x, int y, int z) { return (int)p->data[cb_index(x, y, z)]; }
MC_HD static inline void cb_set(ChunkPrimer *p, int x, int y, int z, int v) { p->data[cb_index(x, y, z)] = (u16)v; }

/* octave-perlin noise state (moved up so CpScratch can embed it; def used to sit lower). */
typedef struct { int p[512]; double xo, yo, zo; } CpSimplex;
typedef struct { CpSimplex levels[4]; int n; } CpPerlin;

/* Opaque, 8-byte-aligned storage for the mineshaft+stronghold gen structs (MSGen+SHGen) used by
 * structures.h st_run / structures_placement.h stp_run. Kept opaque (not the real types) so this
 * widely-included header need NOT pull in map_gen_mineshaft.h, whose ms_ symbols clash with
 * mob_spawning.h. Cast to the MSGen / SHGen pointers at the use site (size checked there).
 * Replaces the two per-chunk device mallocs. The exact 160-piece mineshaft tree
 * plus the stronghold generator and deferred structure events require less
 * than this fixed aligned capacity. */
#define CP_STGEN_BYTES 196608

/* off-stack scratch for the heightmap noise (kept out of the device thread stack). Also holds the
 * GenLayer bump arena + the per-chunk noise structs that used to be device-malloc'd inside
 * cp_provide_chunk (no in-kernel malloc; allocated once with CpScratch, per-thread for batch). */
typedef struct {
    double heightMap[825];
    double depthBuffer[256];
    double mainNoiseRegion[825];
    double minLimitRegion[825];
    double maxLimitRegion[825];
    double depthRegion[25];
    TerrainNoise tnoise;      /* was malloc(sizeof(TerrainNoise)) in cp_provide_chunk */
    CpPerlin surfaceNoise;    /* was malloc(sizeof(CpPerlin)) */
    CpPerlin grassNoise;      /* was malloc(sizeof(CpPerlin)) */
    GlArena arena;            /* GenLayer IntCache substitute (bump; reset per top-level tree) */
    long long stgen[CP_STGEN_BYTES / 8];   /* MSGen+SHGen storage (opaque; cast in structures*.h) */
} CpScratch;

/* ===== REAL biome property table (verbatim from Biome.registerBiomes + subclass constructors) =====
 * Defaults (BiomeProperties): baseHeight 0.1, heightVariation 0.2, temperature 0.5. */
MC_HD MC_NOINLINE static float cb_baseHeight(int id) {
    switch (id) {
        case 0: return -1.0f;        case 1: return 0.125f;       case 2: return 0.125f;
        case 3: return 1.0f;         case 4: return 0.1f;         case 5: return 0.2f;
        case 6: return -0.2f;        case 7: return -0.5f;        case 8: return 0.1f;
        case 9: return 0.1f;         case 10: return -1.0f;       case 11: return -0.5f;
        case 12: return 0.125f;      case 13: return 0.45f;       case 14: return 0.2f;
        case 15: return 0.0f;        case 16: return 0.0f;        case 17: return 0.45f;
        case 18: return 0.45f;       case 19: return 0.45f;       case 20: return 0.8f;
        case 21: return 0.1f;        case 22: return 0.45f;       case 23: return 0.1f;
        case 24: return -1.8f;       case 25: return 0.1f;        case 26: return 0.0f;
        case 27: return 0.1f;        case 28: return 0.45f;       case 29: return 0.1f;
        case 30: return 0.2f;        case 31: return 0.45f;       case 32: return 0.2f;
        case 33: return 0.45f;       case 34: return 1.0f;        case 35: return 0.125f;
        case 36: return 1.5f;        case 37: return 0.1f;        case 38: return 1.5f;
        case 39: return 1.5f;        case 127: return 0.1f;       case 129: return 0.125f;
        case 130: return 0.225f;     case 131: return 1.0f;       case 132: return 0.1f;
        case 133: return 0.3f;       case 134: return -0.1f;      case 140: return 0.425f;
        case 149: return 0.2f;       case 151: return 0.2f;       case 155: return 0.2f;
        case 156: return 0.55f;      case 157: return 0.2f;       case 158: return 0.3f;
        case 160: return 0.2f;       case 161: return 0.2f;       case 162: return 1.0f;
        case 163: return 0.3625f;    case 164: return 1.05f;      case 165: return 0.1f;
        case 166: return 0.45f;      case 167: return 0.45f;
        default: return 0.1f;
    }
}
MC_HD MC_NOINLINE static float cb_heightVar(int id) {
    switch (id) {
        case 0: return 0.1f;         case 1: return 0.05f;        case 2: return 0.05f;
        case 3: return 0.5f;         case 4: return 0.2f;         case 5: return 0.2f;
        case 6: return 0.1f;         case 7: return 0.0f;         case 8: return 0.2f;
        case 9: return 0.2f;         case 10: return 0.1f;        case 11: return 0.0f;
        case 12: return 0.05f;       case 13: return 0.3f;        case 14: return 0.3f;
        case 15: return 0.025f;      case 16: return 0.025f;      case 17: return 0.3f;
        case 18: return 0.3f;        case 19: return 0.3f;        case 20: return 0.3f;
        case 21: return 0.2f;        case 22: return 0.3f;        case 23: return 0.2f;
        case 24: return 0.1f;        case 25: return 0.8f;        case 26: return 0.025f;
        case 27: return 0.2f;        case 28: return 0.3f;        case 29: return 0.2f;
        case 30: return 0.2f;        case 31: return 0.3f;        case 32: return 0.2f;
        case 33: return 0.3f;        case 34: return 0.5f;        case 35: return 0.05f;
        case 36: return 0.025f;      case 37: return 0.2f;        case 38: return 0.025f;
        case 39: return 0.025f;      case 127: return 0.2f;       case 129: return 0.05f;
        case 130: return 0.25f;      case 131: return 0.5f;       case 132: return 0.4f;
        case 133: return 0.4f;       case 134: return 0.3f;       case 140: return 0.45000002f;
        case 149: return 0.4f;       case 151: return 0.4f;       case 155: return 0.4f;
        case 156: return 0.5f;       case 157: return 0.4f;       case 158: return 0.4f;
        case 160: return 0.2f;       case 161: return 0.2f;       case 162: return 0.5f;
        case 163: return 1.225f;     case 164: return 1.2125001f; case 165: return 0.2f;
        case 166: return 0.3f;       case 167: return 0.3f;
        default: return 0.2f;
    }
}
MC_HD MC_NOINLINE static float cb_temperature(int id) {
    switch (id) {
        case 0: return 0.5f;         case 1: return 0.8f;         case 2: return 2.0f;
        case 3: return 0.2f;         case 4: return 0.7f;         case 5: return 0.25f;
        case 6: return 0.8f;         case 7: return 0.5f;         case 8: return 2.0f;
        case 9: return 0.5f;         case 10: return 0.0f;        case 11: return 0.0f;
        case 12: return 0.0f;        case 13: return 0.0f;        case 14: return 0.9f;
        case 15: return 0.9f;        case 16: return 0.8f;        case 17: return 2.0f;
        case 18: return 0.7f;        case 19: return 0.25f;       case 20: return 0.2f;
        case 21: return 0.95f;       case 22: return 0.95f;       case 23: return 0.95f;
        case 24: return 0.5f;        case 25: return 0.2f;        case 26: return 0.05f;
        case 27: return 0.6f;        case 28: return 0.6f;        case 29: return 0.7f;
        case 30: return -0.5f;       case 31: return -0.5f;       case 32: return 0.3f;
        case 33: return 0.3f;        case 34: return 0.2f;        case 35: return 1.2f;
        case 36: return 1.0f;        case 37: return 2.0f;        case 38: return 2.0f;
        case 39: return 2.0f;        case 127: return 0.5f;       case 129: return 0.8f;
        case 130: return 2.0f;       case 131: return 0.2f;       case 132: return 0.7f;
        case 133: return 0.25f;      case 134: return 0.8f;       case 140: return 0.0f;
        case 149: return 0.95f;      case 151: return 0.95f;      case 155: return 0.6f;
        case 156: return 0.6f;       case 157: return 0.7f;       case 158: return -0.5f;
        case 160: return 0.25f;      case 161: return 0.25f;      case 162: return 0.2f;
        case 163: return 1.1f;       case 164: return 1.0f;       case 165: return 2.0f;
        case 166: return 2.0f;       case 167: return 2.0f;
        default: return 0.5f;
    }
}
/* default topBlock/fillerBlock (Biome ctor: GRASS/DIRT, except the few overrides below). */
MC_HD static inline int cb_stained_clay(int meta) { return CB_STAINED_CLAY_BASE + meta; }
MC_HD static inline int cb_is_stained_clay(int id) {
    return id == CB_STAINED_HARDENED_CLAY ||
           (id >= CB_STAINED_CLAY_BASE && id < CB_STAINED_CLAY_BASE + 16);
}
MC_HD static inline int cb_is_dirt_block(int id) {
    return id == CB_DIRT || id == CB_PODZOL || id == CB_COARSE_DIRT;
}
MC_HD static inline int cb_same_block_identity(int a, int b) {
    if (cb_is_stained_clay(a) && cb_is_stained_clay(b)) return 1;
    if (cb_is_dirt_block(a) && cb_is_dirt_block(b)) return 1;
    return a == b;
}

MC_HD MC_NOINLINE static int cb_defTop(int id) {
    switch (id) {
        case 2: case 17: case 130: return CB_SAND;        /* deserts */
        case 16: case 26: return CB_SAND;                 /* beaches */
        case 25: return CB_STONE;                         /* stone beach */
        case 14: case 15: return CB_MYCELIUM;             /* mushroom island(/shore) */
        case 37: case 38: case 39:
        case 165: case 166: case 167: return CB_SAND;     /* Mesa ctor: RED_SAND (id 12) */
        default: return CB_GRASS;
    }
}
MC_HD MC_NOINLINE static int cb_defFiller(int id) {
    switch (id) {
        case 2: case 17: case 130: return CB_SAND;        /* deserts */
        case 16: case 26: return CB_SAND;                 /* beaches */
        case 25: return CB_STONE;                         /* stone beach */
        case 37: case 38: case 39:
        case 165: case 166: case 167: return cb_stained_clay(0);
        default: return CB_DIRT;
    }
}

/* genTerrainBlocks dispatch type (which Biome subclass overrides genTerrainBlocks). */
enum { SURF_BASE, SURF_HILLS, SURF_TAIGA, SURF_SWAMP, SURF_MESA };
enum { HILLS_NORMAL, HILLS_EXTRA_TREES, HILLS_MUTATED };
enum { TAIGA_NORMAL, TAIGA_MEGA, TAIGA_MEGA_SPRUCE };

MC_HD MC_NOINLINE static int cb_surfType(int id) {
    switch (id) {
        case 3: case 20: case 34: case 131: case 162: return SURF_HILLS;
        case 5: case 19: case 30: case 31: case 32: case 33:
        case 133: case 158: case 160: case 161: return SURF_TAIGA;
        case 6: case 134: return SURF_SWAMP;
        case 37: case 38: case 39: case 165: case 166: case 167: return SURF_MESA;
        default: return SURF_BASE;
    }
}
MC_HD MC_NOINLINE static int cb_hillsType(int id) {
    switch (id) {
        case 20: case 34: return HILLS_EXTRA_TREES;
        case 131: case 162: return HILLS_MUTATED;
        default: return HILLS_NORMAL;  /* id 3 */
    }
}
MC_HD MC_NOINLINE static int cb_taigaType(int id) {
    switch (id) {
        case 32: case 33: return TAIGA_MEGA;
        case 160: case 161: return TAIGA_MEGA_SPRUCE;
        default: return TAIGA_NORMAL;  /* 5,19,30,31,133,158 */
    }
}
MC_HD static inline int cb_mesaBryce(int id) { return id == 165; }
MC_HD static inline int cb_mesaHasForest(int id) { return id == 38 || id == 166; }

/* MapGenCaves.isExceptionBiome: BEACH, DESERT only. */
MC_HD static inline int cb_caveException(int id) { return id == 16 || id == 2; }
/* MapGenRavine.isExceptionBiome: BEACH, DESERT, MUSHROOM_ISLAND, MUSHROOM_ISLAND_SHORE. */
MC_HD static inline int cb_ravineException(int id) { return id == 16 || id == 2 || id == 14 || id == 15; }

/* ===== NoiseGeneratorSimplex (verbatim: add for surfaceNoise getRegion, getValue for GRASS_COLOR) =====
 * (CpSimplex/CpPerlin typedefs moved up above CpScratch so it can embed the noise state.) */
MC_HD MC_NOINLINE static void cp_simplex_init(CpSimplex *s, JavaRandom *r) {
    s->xo = jrand_double(r) * 256.0;
    s->yo = jrand_double(r) * 256.0;
    s->zo = jrand_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) s->p[i] = i;
    for (int l = 0; l < 256; ++l) {
        int j = jrand_int_bound(r, 256 - l) + l;
        int k = s->p[l];
        s->p[l] = s->p[j];
        s->p[j] = k;
        s->p[l + 256] = s->p[l];
    }
}
/* advance the Random as one NoiseGeneratorImproved/Simplex ctor would (no stored result). */
MC_HD MC_NOINLINE static void cp_advance_noise_ctor(JavaRandom *r) {
    jrand_double(r); jrand_double(r); jrand_double(r);
    for (int l = 0; l < 256; ++l) jrand_int_bound(r, 256 - l);
}
MC_HD static inline int cp_fastfloor(double v) { return v > 0.0 ? mc_d2i(v) : mc_d2i(v) - 1; }

#define CP_SQRT3      1.7320508075688772     /* Math.sqrt(3.0) */
#define CP_F2         (0.5 * (CP_SQRT3 - 1.0))
#define CP_G2         ((3.0 - CP_SQRT3) / 6.0)

MC_HD MC_NOINLINE static double cp_simplex_getValue(const CpSimplex *s, double x1, double x3) {
    const int CP_GRAD3[12][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},
        {1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    const double SQRT_3 = CP_SQRT3;
    double d3 = 0.5 * (SQRT_3 - 1.0);
    double d4 = (x1 + x3) * d3;
    int i = cp_fastfloor(x1 + d4);
    int j = cp_fastfloor(x3 + d4);
    double d5 = (3.0 - SQRT_3) / 6.0;
    double d6 = (double)(i + j) * d5;
    double d7 = (double)i - d6;
    double d8 = (double)j - d6;
    double d9 = x1 - d7;
    double d10 = x3 - d8;
    int k, l;
    if (d9 > d10) { k = 1; l = 0; } else { k = 0; l = 1; }
    double d11 = d9 - (double)k + d5;
    double d12 = d10 - (double)l + d5;
    double d13 = d9 - 1.0 + 2.0 * d5;
    double d14 = d10 - 1.0 + 2.0 * d5;
    int i1 = i & 255;
    int j1 = j & 255;
    int k1 = s->p[i1 + s->p[j1]] % 12;
    int l1 = s->p[i1 + k + s->p[j1 + l]] % 12;
    int i2 = s->p[i1 + 1 + s->p[j1 + 1]] % 12;
    double d15 = 0.5 - d9 * d9 - d10 * d10;
    double d0;
    if (d15 < 0.0) d0 = 0.0;
    else { d15 = d15 * d15; d0 = d15 * d15 * ((double)CP_GRAD3[k1][0] * d9 + (double)CP_GRAD3[k1][1] * d10); }
    double d16 = 0.5 - d11 * d11 - d12 * d12;
    double d1;
    if (d16 < 0.0) d1 = 0.0;
    else { d16 = d16 * d16; d1 = d16 * d16 * ((double)CP_GRAD3[l1][0] * d11 + (double)CP_GRAD3[l1][1] * d12); }
    double d17 = 0.5 - d13 * d13 - d14 * d14;
    double d2;
    if (d17 < 0.0) d2 = 0.0;
    else { d17 = d17 * d17; d2 = d17 * d17 * ((double)CP_GRAD3[i2][0] * d13 + (double)CP_GRAD3[i2][1] * d14); }
    return 70.0 * (d0 + d1 + d2);
}

MC_HD MC_NOINLINE static void cp_simplex_add(const CpSimplex *s, double *out,
        double p2, double p4, int p6, int p7, double p8, double p10, double p12) {
    const int CP_GRAD3[12][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},
        {1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    int i = 0;
    for (int j = 0; j < p7; ++j) {
        double d0 = (p4 + (double)j) * p10 + s->yo;
        for (int k = 0; k < p6; ++k) {
            double d1 = (p2 + (double)k) * p8 + s->xo;
            double d5 = (d1 + d0) * CP_F2;
            int l = cp_fastfloor(d1 + d5);
            int i1 = cp_fastfloor(d0 + d5);
            double d6 = (double)(l + i1) * CP_G2;
            double d7 = (double)l - d6;
            double d8 = (double)i1 - d6;
            double d9 = d1 - d7;
            double d10 = d0 - d8;
            int j1, k1;
            if (d9 > d10) { j1 = 1; k1 = 0; } else { j1 = 0; k1 = 1; }
            double d11 = d9 - (double)j1 + CP_G2;
            double d12 = d10 - (double)k1 + CP_G2;
            double d13 = d9 - 1.0 + 2.0 * CP_G2;
            double d14 = d10 - 1.0 + 2.0 * CP_G2;
            int l1 = l & 255;
            int i2 = i1 & 255;
            int j2 = s->p[l1 + s->p[i2]] % 12;
            int k2 = s->p[l1 + j1 + s->p[i2 + k1]] % 12;
            int l2 = s->p[l1 + 1 + s->p[i2 + 1]] % 12;
            double d15 = 0.5 - d9 * d9 - d10 * d10;
            double d2;
            if (d15 < 0.0) d2 = 0.0;
            else { d15 = d15 * d15; d2 = d15 * d15 * ((double)CP_GRAD3[j2][0] * d9 + (double)CP_GRAD3[j2][1] * d10); }
            double d16 = 0.5 - d11 * d11 - d12 * d12;
            double d3;
            if (d16 < 0.0) d3 = 0.0;
            else { d16 = d16 * d16; d3 = d16 * d16 * ((double)CP_GRAD3[k2][0] * d11 + (double)CP_GRAD3[k2][1] * d12); }
            double d17 = 0.5 - d13 * d13 - d14 * d14;
            double d4;
            if (d17 < 0.0) d4 = 0.0;
            else { d17 = d17 * d17; d4 = d17 * d17 * ((double)CP_GRAD3[l2][0] * d13 + (double)CP_GRAD3[l2][1] * d14); }
            int i3 = i; ++i;
            out[i3] += 70.0 * (d2 + d3 + d4) * p12;
        }
    }
}

/* NoiseGeneratorPerlin.getRegion (8-arg overload -> p14 = 0.5). */
MC_HD MC_NOINLINE static void cp_perlin_getRegion(const CpPerlin *pn, double *out,
        double p2, double p4, int p6, int p7, double p8, double p10, double p12, double p14) {
    int total = p6 * p7;
    for (int i = 0; i < total; ++i) out[i] = 0.0;
    double d1 = 1.0, d0 = 1.0;
    for (int j = 0; j < pn->n; ++j) {
        cp_simplex_add(&pn->levels[j], out, p2, p4, p6, p7, p8 * d0 * d1, p10 * d0 * d1, 0.55 / d1);
        d0 *= p12;
        d1 *= p14;
    }
}
/* NoiseGeneratorPerlin.getValue. */
MC_HD MC_NOINLINE static double cp_perlin_getValue(const CpPerlin *pn, double x1, double x3) {
    double d0 = 0.0, d1 = 1.0;
    for (int i = 0; i < pn->n; ++i) {
        d0 += cp_simplex_getValue(&pn->levels[i], x1 * d1, x3 * d1) / d1;
        d1 /= 2.0;
    }
    return d0;
}

/* surfaceNoise = ChunkProviderOverworld's 4th generator: built from new Random(seed) AFTER
 * minLimit(16)+maxLimit(16)+main(8) = 40 NoiseGeneratorImproved. */
MC_HD MC_NOINLINE static void cp_surface_noise_init(CpPerlin *sn, i64 seed) {
    JavaRandom r; jrand_set(&r, seed);
    for (int i = 0; i < 16 + 16 + 8; ++i) cp_advance_noise_ctor(&r);
    sn->n = 4;
    for (int i = 0; i < 4; ++i) cp_simplex_init(&sn->levels[i], &r);
}
/* GRASS_COLOR_NOISE = new NoiseGeneratorPerlin(new Random(2345L), 1) (Biome static init). */
MC_HD MC_NOINLINE static void cp_grass_noise_init(CpPerlin *gn) {
    JavaRandom r; jrand_set(&r, 2345LL);
    gn->n = 1;
    cp_simplex_init(&gn->levels[0], &r);
}

typedef struct {
    int valid;
    int pillarValid;
    i64 worldSeed;
    int clayBands[64];
    CpPerlin pillarNoise;
    CpPerlin pillarRoofNoise;
    CpPerlin clayBandsOffsetNoise;
} CpMesaCache;

#if !defined(__CUDA_ARCH__)
static CpMesaCache cp_mesa_cache;
#endif

MC_HD MC_NOINLINE static void cp_perlin_init_levels(CpPerlin *pn, JavaRandom *r, int levels) {
    pn->n = levels;
    for (int i = 0; i < levels; ++i) cp_simplex_init(&pn->levels[i], r);
}

MC_HD MC_NOINLINE static void cp_mesa_generateBands(CpMesaCache *m, i64 seed) {
    JavaRandom random;
    jrand_set(&random, seed);
    for (int i = 0; i < 64; ++i) m->clayBands[i] = CB_HARDENED_CLAY;
    cp_perlin_init_levels(&m->clayBandsOffsetNoise, &random, 1);

    for (int l1 = 0; l1 < 64; ++l1) {
        l1 += jrand_int_bound(&random, 5) + 1;
        if (l1 < 64) m->clayBands[l1] = cb_stained_clay(1);  /* ORANGE */
    }

    int i2 = jrand_int_bound(&random, 4) + 2;
    for (int i = 0; i < i2; ++i) {
        int j = jrand_int_bound(&random, 3) + 1;
        int k = jrand_int_bound(&random, 64);
        for (int l = 0; k + l < 64 && l < j; ++l)
            m->clayBands[k + l] = cb_stained_clay(4);        /* YELLOW */
    }

    int j2 = jrand_int_bound(&random, 4) + 2;
    for (int k2 = 0; k2 < j2; ++k2) {
        int i3 = jrand_int_bound(&random, 3) + 2;
        int l3 = jrand_int_bound(&random, 64);
        for (int i1 = 0; l3 + i1 < 64 && i1 < i3; ++i1)
            m->clayBands[l3 + i1] = cb_stained_clay(12);     /* BROWN */
    }

    int l2 = jrand_int_bound(&random, 4) + 2;
    for (int j3 = 0; j3 < l2; ++j3) {
        int i4 = jrand_int_bound(&random, 3) + 1;
        int k4 = jrand_int_bound(&random, 64);
        for (int j1 = 0; k4 + j1 < 64 && j1 < i4; ++j1)
            m->clayBands[k4 + j1] = cb_stained_clay(14);     /* RED */
    }

    int k3 = jrand_int_bound(&random, 3) + 3;
    int j4 = 0;
    for (int l4 = 0; l4 < k3; ++l4) {
        j4 += jrand_int_bound(&random, 16) + 4;
        for (int k1 = 0; j4 + k1 < 64 && k1 < 1; ++k1) {
            m->clayBands[j4 + k1] = cb_stained_clay(0);      /* WHITE */
            if (j4 + k1 > 1 && jrand_next(&random, 1) != 0)
                m->clayBands[j4 + k1 - 1] = cb_stained_clay(8);  /* SILVER */
            if (j4 + k1 < 63 && jrand_next(&random, 1) != 0)
                m->clayBands[j4 + k1 + 1] = cb_stained_clay(8);  /* SILVER */
        }
    }

    m->valid = 1;
}

MC_HD MC_NOINLINE static void cp_mesa_prepare(CpMesaCache *m, i64 seed) {
    if (!m->valid || m->worldSeed != seed)
        cp_mesa_generateBands(m, seed);
    if (!m->pillarValid || m->worldSeed != seed) {
        JavaRandom pillarRandom;
        jrand_set(&pillarRandom, m->worldSeed);
        cp_perlin_init_levels(&m->pillarNoise, &pillarRandom, 4);
        cp_perlin_init_levels(&m->pillarRoofNoise, &pillarRandom, 1);
        m->pillarValid = 1;
    }
    m->worldSeed = seed;
}

MC_HD MC_NOINLINE static CpMesaCache *cp_mesa_cache_for_seed(i64 seed, CpMesaCache *local) {
#if defined(__CUDA_ARCH__)
    local->valid = 0;
    local->pillarValid = 0;
    local->worldSeed = 0;
    cp_mesa_prepare(local, seed);
    return local;
#else
    (void)local;
    cp_mesa_prepare(&cp_mesa_cache, seed);
    return &cp_mesa_cache;
#endif
}

MC_HD MC_NOINLINE static int cp_mesa_getBand(const CpMesaCache *m, int x, int y, int z) {
    (void)z;
    double v = cp_perlin_getValue(&m->clayBandsOffsetNoise, (double)x / 512.0, (double)x / 512.0);
    int i = mc_d2i(floor(v * 2.0 + 0.5));
    return m->clayBands[(y + i + 64) % 64];
}

/* ===== generateHeightmap (verbatim, but REAL per-biome baseHeight/heightVariation) ===== */
MC_HD MC_NOINLINE static void cp_generateHeightmap(CpScratch *sc, const TerrainNoise *t,
        const int *lowBiome, int p_1, int p_2, int p_3) {
    const float coordinateScale = 684.412f, heightScale = 684.412f;
    const float upperLimitScale = 512.0f, lowerLimitScale = 512.0f;
    const float depthNoiseScaleX = 200.0f, depthNoiseScaleZ = 200.0f;
    const float mainNoiseScaleX = 80.0f, mainNoiseScaleY = 160.0f, mainNoiseScaleZ = 80.0f;
    const float baseSize = 8.5f, stretchY = 12.0f;
    const float biomeDepthWeight = 1.0f, biomeDepthOffSet = 0.0f;
    const float biomeScaleWeight = 1.0f, biomeScaleOffset = 0.0f;

    float biomeWeights[25];
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j) {
            float f = 10.0f / (float)sqrt((double)((float)(i * i + j * j) + 0.2f));
            biomeWeights[i + 2 + (j + 2) * 5] = f;
        }

    mc_oct_generate(&t->depth, sc->depthRegion, p_1, 10, p_3, 5, 1, 5,
                    (double)depthNoiseScaleX, 1.0, (double)depthNoiseScaleZ);
    float f = coordinateScale, f1 = heightScale;
    mc_oct_generate(&t->mainP, sc->mainNoiseRegion, p_1, p_2, p_3, 5, 33, 5,
                    (double)(f / mainNoiseScaleX), (double)(f1 / mainNoiseScaleY), (double)(f / mainNoiseScaleZ));
    mc_oct_generate(&t->minLimit, sc->minLimitRegion, p_1, p_2, p_3, 5, 33, 5, (double)f, (double)f1, (double)f);
    mc_oct_generate(&t->maxLimit, sc->maxLimitRegion, p_1, p_2, p_3, 5, 33, 5, (double)f, (double)f1, (double)f);
    int i = 0, j = 0;
    for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 5; ++l) {
            float f2 = 0.0f, f3 = 0.0f, f4 = 0.0f;
            int biome = lowBiome[k + 2 + (l + 2) * 10];
            for (int j1 = -2; j1 <= 2; ++j1) {
                for (int k1 = -2; k1 <= 2; ++k1) {
                    int biome1 = lowBiome[k + j1 + 2 + (l + k1 + 2) * 10];
                    float f5 = biomeDepthOffSet + cb_baseHeight(biome1) * biomeDepthWeight;
                    float f6 = biomeScaleOffset + cb_heightVar(biome1) * biomeScaleWeight;
                    /* terrainType != AMPLIFIED -> no boost */
                    float f7 = biomeWeights[j1 + 2 + (k1 + 2) * 5] / (f5 + 2.0f);
                    if (cb_baseHeight(biome1) > cb_baseHeight(biome)) f7 /= 2.0f;
                    f2 += f6 * f7;
                    f3 += f5 * f7;
                    f4 += f7;
                }
            }
            f2 = f2 / f4;
            f3 = f3 / f4;
            f2 = f2 * 0.9f + 0.1f;
            f3 = (f3 * 4.0f - 1.0f) / 8.0f;
            double d7 = sc->depthRegion[j] / 8000.0;
            if (d7 < 0.0) d7 = -d7 * 0.3;
            d7 = d7 * 3.0 - 2.0;
            if (d7 < 0.0) {
                d7 = d7 / 2.0;
                if (d7 < -1.0) d7 = -1.0;
                d7 = d7 / 1.4;
                d7 = d7 / 2.0;
            } else {
                if (d7 > 1.0) d7 = 1.0;
                d7 = d7 / 8.0;
            }
            ++j;
            double d8 = (double)f3;
            double d9 = (double)f2;
            d8 = d8 + d7 * 0.2;
            d8 = d8 * (double)baseSize / 8.0;
            double d0 = (double)baseSize + d8 * 4.0;
            for (int l1 = 0; l1 < 33; ++l1) {
                double d1 = ((double)l1 - d0) * (double)stretchY * 128.0 / 256.0 / d9;
                if (d1 < 0.0) d1 *= 4.0;
                double d2 = sc->minLimitRegion[i] / (double)lowerLimitScale;
                double d3 = sc->maxLimitRegion[i] / (double)upperLimitScale;
                double d4 = (sc->mainNoiseRegion[i] / 10.0 + 1.0) / 2.0;
                double d5 = mc_clamped_lerp(d2, d3, d4) - d1;
                if (l1 > 29) {
                    double d6 = (double)((float)(l1 - 29) / 3.0f);
                    d5 = d5 * (1.0 - d6) + -10.0 * d6;
                }
                sc->heightMap[i] = d5;
                ++i;
            }
        }
    }
}

/* ===== setBlocksInChunk (verbatim density -> STONE / oceanBlock(WATER)) ===== */
MC_HD MC_NOINLINE static void cp_setBlocksInChunk(ChunkPrimer *primer, const double *heightMap) {
    for (int i = 0; i < 4; ++i) {
        int j = i * 5;
        int k = (i + 1) * 5;
        for (int l = 0; l < 4; ++l) {
            int i1 = (j + l) * 33;
            int j1 = (j + l + 1) * 33;
            int k1 = (k + l) * 33;
            int l1 = (k + l + 1) * 33;
            for (int i2 = 0; i2 < 32; ++i2) {
                double d1 = heightMap[i1 + i2];
                double d2 = heightMap[j1 + i2];
                double d3 = heightMap[k1 + i2];
                double d4 = heightMap[l1 + i2];
                double d5 = (heightMap[i1 + i2 + 1] - d1) * 0.125;
                double d6 = (heightMap[j1 + i2 + 1] - d2) * 0.125;
                double d7 = (heightMap[k1 + i2 + 1] - d3) * 0.125;
                double d8 = (heightMap[l1 + i2 + 1] - d4) * 0.125;
                for (int j2 = 0; j2 < 8; ++j2) {
                    double d10 = d1;
                    double d11 = d2;
                    double d12 = (d3 - d1) * 0.25;
                    double d13 = (d4 - d2) * 0.25;
                    for (int k2 = 0; k2 < 4; ++k2) {
                        double d16 = (d11 - d10) * 0.25;
                        double lvt = d10 - d16;
                        for (int l2 = 0; l2 < 4; ++l2) {
                            if ((lvt += d16) > 0.0) {
                                cb_set(primer, i * 4 + k2, i2 * 8 + j2, l * 4 + l2, CB_STONE);
                            } else if (i2 * 8 + j2 < CB_SEA_LEVEL) {
                                cb_set(primer, i * 4 + k2, i2 * 8 + j2, l * 4 + l2, CB_WATER);
                            }
                        }
                        d10 += d12;
                        d11 += d13;
                    }
                    d1 += d5; d2 += d6; d3 += d7; d4 += d8;
                }
            }
        }
    }
}

/* getFloatTemperature: only y<=64 is reachable here (callers use y<seaLevel 63), so it returns the
 * flat biome temperature; the y>64 TEMPERATURE_NOISE branch is dead and not ported (documented). */
MC_HD MC_NOINLINE static float cb_getFloatTemperature(float temp, int y) {
    (void)y;
    return temp;
}

/* Biome.generateBiomeTerrain (verbatim; top/filler/temp passed in from the per-biome state). */
MC_HD MC_NOINLINE static void cp_generateBiomeTerrain(JavaRandom *rand, ChunkPrimer *primer,
        int x, int z, double noiseVal, int topBlock, int fillerBlock, float temp) {
    int i = CB_SEA_LEVEL;
    int iblockstate = topBlock;
    int iblockstate1 = fillerBlock;
    int j = -1;
    int k = mc_d2i(noiseVal / 3.0 + 3.0 + jrand_double(rand) * 0.25);
    int l = x & 15;
    int i1 = z & 15;
    for (int j1 = 255; j1 >= 0; --j1) {
        if (j1 <= jrand_int_bound(rand, 5)) {
            cb_set(primer, i1, j1, l, CB_BEDROCK);
        } else {
            int s2 = cb_get(primer, i1, j1, l);
            if (s2 == CB_AIR) {
                j = -1;
            } else if (s2 == CB_STONE) {
                if (j == -1) {
                    if (k <= 0) {
                        iblockstate = CB_AIR;
                        iblockstate1 = CB_STONE;
                    } else if (j1 >= i - 4 && j1 <= i + 1) {
                        iblockstate = topBlock;
                        iblockstate1 = fillerBlock;
                    }
                    if (j1 < i && iblockstate == CB_AIR) {
                        if (cb_getFloatTemperature(temp, j1) < 0.15f) iblockstate = CB_ICE;
                        else iblockstate = CB_WATER;
                    }
                    j = k;
                    if (j1 >= i - 1) {
                        cb_set(primer, i1, j1, l, iblockstate);
                    } else if (j1 < i - 7 - k) {
                        iblockstate = CB_AIR;
                        iblockstate1 = CB_STONE;
                        cb_set(primer, i1, j1, l, CB_GRAVEL);
                    } else {
                        cb_set(primer, i1, j1, l, iblockstate1);
                    }
                } else if (j > 0) {
                    --j;
                    cb_set(primer, i1, j1, l, iblockstate1);
                    if (j == 0 && iblockstate1 == CB_SAND && k > 1) {
                        int mx = j1 - 63; if (mx < 0) mx = 0;
                        j = jrand_int_bound(rand, 4) + mx;
                        iblockstate1 = CB_SANDSTONE;   /* regular sand filler -> sandstone */
                    }
                }
            }
        }
    }
}

/* genTerrainBlocks dispatch: base / Hills / Taiga / Swamp, updating per-biome curTop/curFiller. */
MC_HD MC_NOINLINE static void cp_genTerrainBlocks(int biome, JavaRandom *rand, ChunkPrimer *primer,
        int x, int z, double noiseVal, int *curTop, int *curFiller, const CpPerlin *grassNoise,
        i64 worldSeed) {
    int type = cb_surfType(biome);
    if (type == SURF_HILLS) {
        curTop[biome] = CB_GRASS;
        curFiller[biome] = CB_DIRT;
        int ht = cb_hillsType(biome);
        if ((noiseVal < -1.0 || noiseVal > 2.0) && ht == HILLS_MUTATED) {
            curTop[biome] = CB_GRAVEL;
            curFiller[biome] = CB_GRAVEL;
        } else if (noiseVal > 1.0 && ht != HILLS_EXTRA_TREES) {
            curTop[biome] = CB_STONE;
            curFiller[biome] = CB_STONE;
        }
    } else if (type == SURF_TAIGA) {
        int tt = cb_taigaType(biome);
        if (tt == TAIGA_MEGA || tt == TAIGA_MEGA_SPRUCE) {
            curTop[biome] = CB_GRASS;
            curFiller[biome] = CB_DIRT;
            if (noiseVal > 1.75) curTop[biome] = CB_COARSE_DIRT;
            else if (noiseVal > -0.95) curTop[biome] = CB_PODZOL;
        }
        /* TAIGA_NORMAL leaves curTop/curFiller unchanged (constructor default grass/dirt) */
    } else if (type == SURF_SWAMP) {
        double d0 = cp_perlin_getValue(grassNoise, (double)x * 0.25, (double)z * 0.25);
        if (d0 > 0.0) {
            int ii = x & 15;
            int jj = z & 15;
            for (int kk = 255; kk >= 0; --kk) {
                if (cb_get(primer, jj, kk, ii) != CB_AIR) {
                    if (kk == 62 && cb_get(primer, jj, kk, ii) != CB_WATER) {
                        cb_set(primer, jj, kk, ii, CB_WATER);
                        if (d0 < 0.12) cb_set(primer, jj, kk + 1, ii, CB_WATER_LILY);
                    }
                    break;
                }
            }
        }
    } else if (type == SURF_MESA) {
        CpMesaCache mesaLocal;
        CpMesaCache *mesa = cp_mesa_cache_for_seed(worldSeed, &mesaLocal);
        double d4 = 0.0;
        int brycePillars = cb_mesaBryce(biome);
        int hasForest = cb_mesaHasForest(biome);
        if (brycePillars) {
            int i = (x & -16) + (z & 15);
            int j = (z & -16) + (x & 15);
            double pv = cp_perlin_getValue(&mesa->pillarNoise, (double)i * 0.25, (double)j * 0.25);
            double d0 = fmin(fabs(noiseVal), pv);
            if (d0 > 0.0) {
                double d2 = fabs(cp_perlin_getValue(&mesa->pillarRoofNoise,
                                                     (double)i * 0.001953125,
                                                     (double)j * 0.001953125));
                d4 = d0 * d0 * 2.5;
                double d3 = ceil(d2 * 50.0) + 14.0;
                if (d4 > d3) d4 = d3;
                d4 = d4 + 64.0;
            }
        }

        int k1 = x & 15;
        int l1 = z & 15;
        int i2 = CB_SEA_LEVEL;
        int iblockstate = cb_stained_clay(0);
        int iblockstate3 = curFiller[biome];
        int k = mc_d2i(noiseVal / 3.0 + 3.0 + jrand_double(rand) * 0.25);
        int flag = cos(noiseVal / 3.0 * MC_PI) > 0.0;
        int l = -1;
        int flag1 = 0;
        int i1 = 0;

        for (int j1 = 255; j1 >= 0; --j1) {
            if (cb_get(primer, l1, j1, k1) == CB_AIR && j1 < mc_d2i(d4))
                cb_set(primer, l1, j1, k1, CB_STONE);

            if (j1 <= jrand_int_bound(rand, 5)) {
                cb_set(primer, l1, j1, k1, CB_BEDROCK);
            } else if (i1 < 15 || brycePillars) {
                int iblockstate1 = cb_get(primer, l1, j1, k1);
                if (iblockstate1 == CB_AIR) {
                    l = -1;
                } else if (iblockstate1 == CB_STONE) {
                    if (l == -1) {
                        flag1 = 0;
                        if (k <= 0) {
                            iblockstate = CB_AIR;
                            iblockstate3 = CB_STONE;
                        } else if (j1 >= i2 - 4 && j1 <= i2 + 1) {
                            iblockstate = cb_stained_clay(0);
                            iblockstate3 = curFiller[biome];
                        }
                        if (j1 < i2 && iblockstate == CB_AIR) iblockstate = CB_WATER;
                        int mx = j1 - i2;
                        if (mx < 0) mx = 0;
                        l = k + mx;
                        if (j1 >= i2 - 1) {
                            if (hasForest && j1 > 86 + k * 2) {
                                cb_set(primer, l1, j1, k1, flag ? CB_COARSE_DIRT : CB_GRASS);
                            } else if (j1 > i2 + 3 + k) {
                                int iblockstate2;
                                if (j1 >= 64 && j1 <= 127) {
                                    iblockstate2 = flag ? CB_HARDENED_CLAY
                                                        : cp_mesa_getBand(mesa, x, j1, z);
                                } else {
                                    iblockstate2 = cb_stained_clay(1);
                                }
                                cb_set(primer, l1, j1, k1, iblockstate2);
                            } else {
                                cb_set(primer, l1, j1, k1, curTop[biome]);
                                flag1 = 1;
                            }
                        } else {
                            cb_set(primer, l1, j1, k1, iblockstate3);
                            if (cb_is_stained_clay(iblockstate3))
                                cb_set(primer, l1, j1, k1, cb_stained_clay(1));
                        }
                    } else if (l > 0) {
                        --l;
                        if (flag1) cb_set(primer, l1, j1, k1, cb_stained_clay(1));
                        else cb_set(primer, l1, j1, k1, cp_mesa_getBand(mesa, x, j1, z));
                    }
                    ++i1;
                }
            }
        }
        return;
    }
    cp_generateBiomeTerrain(rand, primer, x, z, noiseVal, curTop[biome], curFiller[biome],
                            cb_temperature(biome));
}

/* ===== caves (MapGenCaves over the REAL primer + REAL per-column biomes) ===== */
typedef struct {
    ChunkPrimer *primer;
    const McSinTable *st;
    const int *fullBiome;   /* 16x16, index x + z*16 = world(x,z) within chunk (0,0) */
    const int *curTop;
    const int *curFiller;
} CaveCtx;

MC_HD static inline int cp_is_water_mat(int id) { return id == CB_WATER || id == CB_FLOWING_WATER; }
MC_HD MC_NOINLINE static int cp_cave_canReplace(int s, int up) {
    return s == CB_STONE ? 1 : (s == CB_DIRT ? 1 : (s == CB_GRASS ? 1 : (s == CB_HARDENED_CLAY ? 1 :
           (cb_is_stained_clay(s) ? 1 : (s == CB_SANDSTONE ? 1 : (s == CB_RED_SANDSTONE ? 1 :
           (s == CB_MYCELIUM ? 1 : (s == CB_SNOW_LAYER ? 1 :
           ((s == CB_SAND || s == CB_GRAVEL) && !cp_is_water_mat(up))))))))));
}
MC_HD MC_NOINLINE static int cp_cave_isOcean(const ChunkPrimer *p, int x, int y, int z) {
    int b = cb_get(p, x, y, z);
    return b == CB_FLOWING_WATER || b == CB_WATER;
}
MC_HD MC_NOINLINE static int cp_cave_isTop(const CaveCtx *c, int x, int y, int z) {
    int biome = c->fullBiome[x + z * 16];
    int state = cb_get(c->primer, x, y, z);
    if (cb_caveException(biome)) return state == CB_GRASS;
    return state == c->curTop[biome];
}
MC_HD MC_NOINLINE static void cp_cave_digBlock(const CaveCtx *c, int x, int y, int z, int foundTop,
                                          int state, int up) {
    int biome = c->fullBiome[x + z * 16];
    int top = c->curTop[biome];
    int filler = c->curFiller[biome];
    if (cp_cave_canReplace(state, up) || cb_same_block_identity(state, top) ||
        cb_same_block_identity(state, filler)) {
        if (y - 1 < 10) {
            cb_set(c->primer, x, y, z, CB_LAVA);
        } else {
            cb_set(c->primer, x, y, z, CB_AIR);
            if (foundTop && cb_same_block_identity(cb_get(c->primer, x, y - 1, z), filler))
                cb_set(c->primer, x, y - 1, z, top);
        }
    }
}

MC_HD MC_NOINLINE static void cp_cave_addTunnel(CaveCtx *c, i64 p_1, int p_3, int p_4,
        double p_6, double p_8, double p_10, float p_12, float p_13, float p_14,
        int p_15, int p_16, double p_17) {
    double d0 = (double)(p_3 * 16 + 8);
    double d1 = (double)(p_4 * 16 + 8);
    float f = 0.0f, f1 = 0.0f;
    JavaRandom random; jrand_set(&random, p_1);
    if (p_16 <= 0) {
        int i = 8 * 16 - 16;   /* MapGenBase.range = 8 */
        int t = jrand_int_bound(&random, i / 4);
        p_16 = i - t;
    }
    int flag2 = 0;
    if (p_15 == -1) { p_15 = p_16 / 2; flag2 = 1; }
    int jj = jrand_int_bound(&random, p_16 / 2) + p_16 / 4;
    int flag = (jrand_int_bound(&random, 6) == 0);
    for (; p_15 < p_16; ++p_15) {
        double d2 = 1.5 + (double)(mc_sin(c->st, (float)p_15 * (float)MC_PI / (float)p_16) * p_12);
        double d3 = d2 * p_17;
        float f2 = mc_cos(c->st, p_14);
        float f3 = mc_sin(c->st, p_14);
        p_6 += (double)(mc_cos(c->st, p_13) * f2);
        p_8 += (double)f3;
        p_10 += (double)(mc_sin(c->st, p_13) * f2);
        if (flag) p_14 = p_14 * 0.92f; else p_14 = p_14 * 0.7f;
        p_14 = p_14 + f1 * 0.1f;
        p_13 += f * 0.1f;
        f1 = f1 * 0.9f;
        f = f * 0.75f;
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random); f1 = f1 + (a - b) * cc * 2.0f; }
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random); f = f + (a - b) * cc * 4.0f; }
        if (!flag2 && p_15 == jj && p_12 > 1.0f && p_16 > 0) {
            i64 s1 = jrand_long(&random); float z1 = jrand_float(&random) * 0.5f + 0.5f;
            cp_cave_addTunnel(c, s1, p_3, p_4, p_6, p_8, p_10, z1, p_13 - ((float)MC_PI / 2.0f), p_14 / 3.0f, p_15, p_16, 1.0);
            i64 s2 = jrand_long(&random); float z2 = jrand_float(&random) * 0.5f + 0.5f;
            cp_cave_addTunnel(c, s2, p_3, p_4, p_6, p_8, p_10, z2, p_13 + ((float)MC_PI / 2.0f), p_14 / 3.0f, p_15, p_16, 1.0);
            return;
        }
        if (flag2 || jrand_int_bound(&random, 4) != 0) {
            double d4 = p_6 - d0;
            double d5 = p_10 - d1;
            double d6 = (double)(p_16 - p_15);
            double d7 = (double)(p_12 + 2.0f + 16.0f);
            if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7) return;
            if (p_6 >= d0 - 16.0 - d2 * 2.0 && p_10 >= d1 - 16.0 - d2 * 2.0 &&
                p_6 <= d0 + 16.0 + d2 * 2.0 && p_10 <= d1 + 16.0 + d2 * 2.0) {
                int k2 = mc_floor(p_6 - d2) - p_3 * 16 - 1;
                int k = mc_floor(p_6 + d2) - p_3 * 16 + 1;
                int l2 = mc_floor(p_8 - d3) - 1;
                int l = mc_floor(p_8 + d3) + 1;
                int i3 = mc_floor(p_10 - d2) - p_4 * 16 - 1;
                int i1 = mc_floor(p_10 + d2) - p_4 * 16 + 1;
                if (k2 < 0) k2 = 0;
                if (k > 16) k = 16;
                if (l2 < 1) l2 = 1;
                if (l > 248) l = 248;
                if (i3 < 0) i3 = 0;
                if (i1 > 16) i1 = 16;
                int flag3 = 0;
                for (int j1 = k2; !flag3 && j1 < k; ++j1)
                    for (int k1 = i3; !flag3 && k1 < i1; ++k1)
                        for (int l1 = l + 1; !flag3 && l1 >= l2 - 1; --l1)
                            if (l1 >= 0 && l1 < 256) {
                                if (cp_cave_isOcean(c->primer, j1, l1, k1)) flag3 = 1;
                                if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                            }
                if (!flag3) {
                    for (int j3 = k2; j3 < k; ++j3) {
                        double d10 = ((double)(j3 + p_3 * 16) + 0.5 - p_6) / d2;
                        for (int i2 = i3; i2 < i1; ++i2) {
                            double d8 = ((double)(i2 + p_4 * 16) + 0.5 - p_10) / d2;
                            int flag1 = 0;
                            if (d10 * d10 + d8 * d8 < 1.0) {
                                for (int j2 = l; j2 > l2; --j2) {
                                    double d9 = ((double)(j2 - 1) + 0.5 - p_8) / d3;
                                    if (d9 > -0.7 && d10 * d10 + d9 * d9 + d8 * d8 < 1.0) {
                                        int s = cb_get(c->primer, j3, j2, i2);
                                        int up = cb_get(c->primer, j3, j2 + 1, i2);
                                        if (cp_cave_isTop(c, j3, j2, i2)) flag1 = 1;
                                        cp_cave_digBlock(c, j3, j2, i2, flag1, s, up);
                                    }
                                }
                            }
                        }
                    }
                    if (flag2) break;
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void cp_cave_addRoom(CaveCtx *c, JavaRandom *rand, i64 seed,
        int p_3, int p_4, double x, double y, double z) {
    float sz = 1.0f + jrand_float(rand) * 6.0f;
    cp_cave_addTunnel(c, seed, p_3, p_4, x, y, z, sz, 0.0f, 0.0f, -1, -1, 0.5);
}

MC_HD MC_NOINLINE static void cp_cave_recursive(CaveCtx *c, JavaRandom *rand, int chunkX, int chunkZ,
        int p_4, int p_5) {
    int n0 = jrand_int_bound(rand, 15);
    int n1 = jrand_int_bound(rand, n0 + 1);
    int i = jrand_int_bound(rand, n1 + 1);
    if (jrand_int_bound(rand, 7) != 0) i = 0;
    for (int j = 0; j < i; ++j) {
        int r0 = jrand_int_bound(rand, 16);
        double d0 = (double)(chunkX * 16 + r0);
        int nn = jrand_int_bound(rand, 120);
        double d1 = (double)jrand_int_bound(rand, nn + 8);
        int r2 = jrand_int_bound(rand, 16);
        double d2 = (double)(chunkZ * 16 + r2);
        int k = 1;
        if (jrand_int_bound(rand, 4) == 0) {
            i64 roomSeed = jrand_long(rand);
            cp_cave_addRoom(c, rand, roomSeed, p_4, p_5, d0, d1, d2);
            k += jrand_int_bound(rand, 4);
        }
        for (int l = 0; l < k; ++l) {
            float ff = jrand_float(rand) * ((float)MC_PI * 2.0f);
            float f1 = (jrand_float(rand) - 0.5f) * 2.0f / 8.0f;
            float fa = jrand_float(rand);
            float fb = jrand_float(rand);
            float f2 = fa * 2.0f + fb;
            if (jrand_int_bound(rand, 10) == 0) {
                float fc = jrand_float(rand);
                float fd = jrand_float(rand);
                f2 *= fc * fd * 3.0f + 1.0f;
            }
            i64 tunSeed = jrand_long(rand);
            cp_cave_addTunnel(c, tunSeed, p_4, p_5, d0, d1, d2, f2, ff, f1, 0, 0, 1.0);
        }
    }
}

MC_HD MC_NOINLINE static void cp_cave_generate(CaveCtx *c, i64 worldSeed, int x, int z) {
    int i = 8;   /* MapGenBase.range = 8 */
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand);
    i64 k = jrand_long(&rand);
    for (int l = x - i; l <= x + i; ++l) {
        for (int i1 = z - i; i1 <= z + i; ++i1) {
            i64 j1 = (i64)l * j;
            i64 k1 = (i64)i1 * k;
            jrand_set(&rand, j1 ^ k1 ^ worldSeed);
            cp_cave_recursive(c, &rand, l, i1, x, z);
        }
    }
}

/* ===== ravines (MapGenRavine over the REAL primer + REAL per-column biomes) ===== */
typedef struct {
    ChunkPrimer *primer;
    const McSinTable *st;
    const int *fullBiome;
    const int *curTop;
    const int *curFiller;
    float rs[1024];
    JavaRandom rand;
    i64 worldSeed;
} RavineCtx;

MC_HD MC_NOINLINE static int cp_rav_isOcean(const ChunkPrimer *p, int x, int y, int z) {
    int b = cb_get(p, x, y, z);
    return b == CB_FLOWING_WATER || b == CB_WATER;
}
MC_HD MC_NOINLINE static int cp_rav_isTop(const RavineCtx *c, int x, int y, int z) {
    int biome = c->fullBiome[x + z * 16];
    int state = cb_get(c->primer, x, y, z);
    if (cb_ravineException(biome)) return state == CB_GRASS;
    return state == c->curTop[biome];
}
MC_HD MC_NOINLINE static void cp_rav_digBlock(RavineCtx *c, int x, int y, int z, int foundTop) {
    int biome = c->fullBiome[x + z * 16];
    int state = cb_get(c->primer, x, y, z);
    /* vanilla MapGenRavine.digBlock: exception biomes carve with GRASS/DIRT, not the biome top/filler. */
    int top = cb_ravineException(biome) ? CB_GRASS : c->curTop[biome];
    int filler = cb_ravineException(biome) ? CB_DIRT : c->curFiller[biome];
    if (state == CB_STONE || cb_same_block_identity(state, top) ||
        cb_same_block_identity(state, filler)) {
        if (y - 1 < 10) {
            cb_set(c->primer, x, y, z, CB_FLOWING_LAVA);
        } else {
            cb_set(c->primer, x, y, z, CB_AIR);
            if (foundTop && cb_same_block_identity(cb_get(c->primer, x, y - 1, z), filler))
                cb_set(c->primer, x, y - 1, z, top);
        }
    }
}

MC_HD MC_NOINLINE static void cp_rav_addTunnel(RavineCtx *c, i64 p1, int p3, int p4,
        double p6, double p8, double p10, float p12, float p13, float p14,
        int p15, int p16, double p17) {
    JavaRandom random; jrand_set(&random, p1);
    double d0 = (double)(p3 * 16 + 8);
    double d1 = (double)(p4 * 16 + 8);
    float f = 0.0f, f1 = 0.0f;
    if (p16 <= 0) {
        int i = 8 * 16 - 16;
        p16 = i - jrand_int_bound(&random, i / 4);
    }
    int flag1 = 0;
    if (p15 == -1) { p15 = p16 / 2; flag1 = 1; }
    float f2 = 1.0f;
    for (int j = 0; j < 256; ++j) {
        if (j == 0 || jrand_int_bound(&random, 3) == 0) {
            float a = jrand_float(&random);
            float b = jrand_float(&random);
            f2 = 1.0f + a * b;
        }
        c->rs[j] = f2 * f2;
    }
    for (; p15 < p16; ++p15) {
        double d9 = 1.5 + (double)(mc_sin(c->st, (float)p15 * (float)MC_PI / (float)p16) * p12);
        double d2 = d9 * p17;
        d9 = d9 * ((double)jrand_float(&random) * 0.25 + 0.75);
        d2 = d2 * ((double)jrand_float(&random) * 0.25 + 0.75);
        float f3 = mc_cos(c->st, p14);
        float f4 = mc_sin(c->st, p14);
        p6 += (double)(mc_cos(c->st, p13) * f3);
        p8 += (double)f4;
        p10 += (double)(mc_sin(c->st, p13) * f3);
        p14 = p14 * 0.7f;
        p14 = p14 + f1 * 0.05f;
        p13 += f * 0.05f;
        f1 = f1 * 0.8f;
        f = f * 0.5f;
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random); f1 = f1 + (a - b) * cc * 2.0f; }
        { float a = jrand_float(&random); float b = jrand_float(&random); float cc = jrand_float(&random); f = f + (a - b) * cc * 4.0f; }
        if (flag1 || jrand_int_bound(&random, 4) != 0) {
            double d3 = p6 - d0;
            double d4 = p10 - d1;
            double d5 = (double)(p16 - p15);
            double d6 = (double)(p12 + 2.0f + 16.0f);
            if (d3 * d3 + d4 * d4 - d5 * d5 > d6 * d6) return;
            if (p6 >= d0 - 16.0 - d9 * 2.0 && p10 >= d1 - 16.0 - d9 * 2.0 &&
                p6 <= d0 + 16.0 + d9 * 2.0 && p10 <= d1 + 16.0 + d9 * 2.0) {
                int k2 = mc_floor(p6 - d9) - p3 * 16 - 1;
                int k = mc_floor(p6 + d9) - p3 * 16 + 1;
                int l2 = mc_floor(p8 - d2) - 1;
                int l = mc_floor(p8 + d2) + 1;
                int i3 = mc_floor(p10 - d9) - p4 * 16 - 1;
                int i1 = mc_floor(p10 + d9) - p4 * 16 + 1;
                if (k2 < 0) k2 = 0;
                if (k > 16) k = 16;
                if (l2 < 1) l2 = 1;
                if (l > 248) l = 248;
                if (i3 < 0) i3 = 0;
                if (i1 > 16) i1 = 16;
                int flag2 = 0;
                for (int j1 = k2; !flag2 && j1 < k; ++j1)
                    for (int k1 = i3; !flag2 && k1 < i1; ++k1)
                        for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1)
                            if (l1 >= 0 && l1 < 256) {
                                if (cp_rav_isOcean(c->primer, j1, l1, k1)) flag2 = 1;
                                if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                            }
                if (!flag2) {
                    for (int j3 = k2; j3 < k; ++j3) {
                        double d10 = ((double)(j3 + p3 * 16) + 0.5 - p6) / d9;
                        for (int i2 = i3; i2 < i1; ++i2) {
                            double d7 = ((double)(i2 + p4 * 16) + 0.5 - p10) / d9;
                            int flag = 0;
                            if (d10 * d10 + d7 * d7 < 1.0) {
                                for (int j2 = l; j2 > l2; --j2) {
                                    double d8 = ((double)(j2 - 1) + 0.5 - p8) / d2;
                                    if ((d10 * d10 + d7 * d7) * (double)c->rs[j2 - 1] + d8 * d8 / 6.0 < 1.0) {
                                        if (cp_rav_isTop(c, j3, j2, i2)) flag = 1;
                                        cp_rav_digBlock(c, j3, j2, i2, flag);
                                    }
                                }
                            }
                        }
                    }
                    if (flag1) break;
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void cp_rav_recursive(RavineCtx *c, int chunkX, int chunkZ, int p4, int p5) {
    if (jrand_int_bound(&c->rand, 50) == 0) {
        double d0 = (double)(chunkX * 16 + jrand_int_bound(&c->rand, 16));
        int inner = jrand_int_bound(&c->rand, 40);
        double d1 = (double)(jrand_int_bound(&c->rand, inner + 8) + 20);
        double d2 = (double)(chunkZ * 16 + jrand_int_bound(&c->rand, 16));
        for (int j = 0; j < 1; ++j) {
            float ff = jrand_float(&c->rand) * ((float)MC_PI * 2.0f);
            float f1 = (jrand_float(&c->rand) - 0.5f) * 2.0f / 8.0f;
            float a = jrand_float(&c->rand);
            float b = jrand_float(&c->rand);
            float f2 = (a * 2.0f + b) * 2.0f;
            i64 tunnelSeed = jrand_long(&c->rand);
            cp_rav_addTunnel(c, tunnelSeed, p4, p5, d0, d1, d2, f2, ff, f1, 0, 0, 3.0);
        }
    }
}

MC_HD MC_NOINLINE static void cp_rav_generate(RavineCtx *c, int x, int z) {
    int i = 8;
    jrand_set(&c->rand, c->worldSeed);
    i64 j = jrand_long(&c->rand);
    i64 k = jrand_long(&c->rand);
    for (int l = x - i; l <= x + i; ++l) {
        for (int i1 = z - i; i1 <= z + i; ++i1) {
            i64 j1 = (i64)l * j;
            i64 k1 = (i64)i1 * k;
            jrand_set(&c->rand, j1 ^ k1 ^ c->worldSeed);
            cp_rav_recursive(c, l, i1, x, z);
        }
    }
}

/* ===== the full provideChunk-minus-structures pipeline ===== */
MC_HD MC_NOINLINE static void cp_provide_chunk(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
        i64 seed, int chunkX, int chunkZ) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)CB_AIR;   /* new ChunkPrimer() = all AIR */

    /* GenLayer stack: voronoi = biomeIndexLayer (full-res); rivermix = genBiomes (low-res). */
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);
    int rivermix = nodes[voronoi].parent;

    /* TerrainNoise (~130KB) and CpPerlin must not live on the CUDA device stack: preallocated
     * CpScratch fields (no in-kernel malloc). */
    TerrainNoise *tnoise = &sc->tnoise;
    CpPerlin *surfaceNoise = &sc->surfaceNoise;
    CpPerlin *grassNoise = &sc->grassNoise;
    terrain_noise_init(tnoise, seed);
    cp_surface_noise_init(surfaceNoise, seed);
    cp_grass_noise_init(grassNoise);

    int curTop[256], curFiller[256];
    for (int b = 0; b < 256; ++b) { curTop[b] = cb_defTop(b); curFiller[b] = cb_defFiller(b); }

    JavaRandom rand;
    jrand_set(&rand, (i64)chunkX * 341873128712LL + (i64)chunkZ * 132897987541LL);

    /* setBlocksInChunk: low-res biomes -> heightmap -> density. */
    sc->arena.off = 0;   /* reset bump arena at top-level tree */
    int *lowBiome = gl_getInts(nodes, &sc->arena, rivermix, chunkX * 4 - 2, chunkZ * 4 - 2, 10, 10);
    cp_generateHeightmap(sc, tnoise, lowBiome, chunkX * 4, 0, chunkZ * 4);
    cp_setBlocksInChunk(primer, sc->heightMap);

    /* getBiomes (full-res, via cache == biomeIndexLayer.getInts(x*16,z*16,16,16)). */
    sc->arena.off = 0;   /* reset bump arena at top-level tree */
    int *fullBiome = gl_getInts(nodes, &sc->arena, voronoi, chunkX * 16, chunkZ * 16, 16, 16);

    /* replaceBiomeBlocks. */
    cp_perlin_getRegion(surfaceNoise, sc->depthBuffer, (double)(chunkX * 16), (double)(chunkZ * 16),
                        16, 16, 0.0625, 0.0625, 1.0, 0.5);
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            int biome = fullBiome[j + i * 16];
            cp_genTerrainBlocks(biome, &rand, primer, chunkX * 16 + i, chunkZ * 16 + j,
                                sc->depthBuffer[j + i * 16], curTop, curFiller, grassNoise, seed);
        }
    }

    /* caves (useCaves) then ravines (useRavines). */
    CaveCtx cctx; cctx.primer = primer; cctx.st = st; cctx.fullBiome = fullBiome;
    cctx.curTop = curTop; cctx.curFiller = curFiller;
    cp_cave_generate(&cctx, seed, chunkX, chunkZ);

    RavineCtx rctx; rctx.primer = primer; rctx.st = st; rctx.fullBiome = fullBiome;
    rctx.curTop = curTop; rctx.curFiller = curFiller; rctx.worldSeed = seed;
    cp_rav_generate(&rctx, chunkX, chunkZ);
}

#endif /* MC_CHUNK_PROVIDER_H */
