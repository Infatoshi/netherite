/* genlayer_biomes: exact C port of MC 1.11.2 world/gen/layer/* (the GenLayer biome stack).
 * PORT TARGET: net/minecraft/world/gen/layer/GenLayer.java + all subclasses wired by
 * GenLayer.initializeAllBiomeGenerators(seed, WorldType.DEFAULT, null) and
 * WorldType.DEFAULT.getBiomeLayer(...). Produces the full-resolution biome id per column.
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA. This unit is pure integer (+ a few doubles in VoronoiZoom) math; no java.util.Random.
 * GenLayer has its OWN 64-bit LCG seed mixing (worldGenSeed/baseSeed/chunkSeed). Build with
 * -ffp-contract=off / --fmad=false.
 *
 * C-vs-Java traps handled here:
 *  - 64-bit overflow: all seed mixing done in u64 (defined wrap); the only signed-sensitive ops
 *    are (chunkSeed >> 24) [arithmetic] and % bound [trunc toward zero], done on i64 reinterpret.
 *  - Java int (32-bit) widths matched with i32; long with i64/u64.
 *  - Side-effecting nextInt() called more than once per statement -> ordered temporaries
 *    (GenLayerZoom's selectRandom/selectModeOrRandom assignments use l1++ + i1 with RNG RHS).
 *  - GenLayerFuzzyZoom overrides selectModeOrRandom -> ALWAYS selectRandom(4) (consumes one
 *    nextInt(4) every cell, unlike the mode path which only draws on the fallback). Branched.
 *
 * IntCache substitution: vanilla IntCache only POOLS int[] arrays (no logic). Every GenLayer
 * getInts fully overwrites its output array before any read, so pooled stale data is never
 * observed. We replace it with a pre-allocated bump arena (GlArena below): gl_alloc bumps an
 * offset, free is a no-op, and callers reset off=0 at each top-level getInts. This keeps worldgen
 * free of in-kernel malloc (required on sm_86, where a raised device heap OOMs) => behavior
 * identical, bitwise-verified. (Documented.)
 *
 * Biome registry substitution: the layers call Biome.getIdForBiome/getBiomeForId/getBiomeClass/
 * getTempCategory/isSnowyBiome/isMutation/getMutationForBiome and BiomeManager.oceanBiomes. The
 * Biome OBJECT graph is replaced by the EXACT vanilla 1.11.2 integer ids + metadata tables below
 * (extracted from Biome.registerBiomes / the Biome subclasses / BiomeManager). The golden uses
 * the SAME tables; the GenLayer ALGORITHM in the golden is verbatim decompiled MC. */
#ifndef MC_GENLAYER_BIOMES_H
#define MC_GENLAYER_BIOMES_H

#include <stdlib.h>
#include <assert.h>
#include "mc.h"

#ifndef MC_ASSERT
#define MC_ASSERT(x) assert(x)
#endif

/* ---- GenLayer LCG mixing constants (GenLayer.java) ---- */
#define GL_M 6364136223846793005ULL
#define GL_A 1442695040888963407ULL

/* ---- layer type tags ---- */
enum {
    GL_ISLAND, GL_FUZZYZOOM, GL_ZOOM, GL_ADDISLAND, GL_REMOVEOCEAN, GL_ADDSNOW,
    GL_EDGE_COOLWARM, GL_EDGE_HEATICE, GL_EDGE_SPECIAL, GL_ADDMUSHROOM, GL_DEEPOCEAN,
    GL_RIVERINIT, GL_BIOME, GL_BIOMEEDGE, GL_HILLS, GL_RIVER, GL_SMOOTH, GL_RAREBIOME,
    GL_SHORE, GL_RIVERMIX, GL_VORONOIZOOM
};

typedef struct {
    int type;
    int parent;       /* -1 if none. For HILLS: biome chain. For RIVERMIX: biomePatternChain. */
    int parent2;      /* HILLS: riverLayer (NOT seeded). RIVERMIX: riverPatternChain. else -1.  */
    u64 baseSeed;
    u64 worldGenSeed; /* 0 until initWorldGenSeed reaches this node */
    u64 chunkSeed;
} GLNode;

/* ===== vanilla 1.11.2 biome ids (from Biome.registerBiomes) ===== */
#define B_OCEAN 0
#define B_PLAINS 1
#define B_DESERT 2
#define B_EXTREME_HILLS 3
#define B_FOREST 4
#define B_TAIGA 5
#define B_SWAMP 6
#define B_RIVER 7
#define B_FROZEN_OCEAN 10
#define B_FROZEN_RIVER 11
#define B_ICE_PLAINS 12
#define B_ICE_MOUNTAINS 13
#define B_MUSHROOM 14
#define B_MUSHROOM_SHORE 15
#define B_BEACH 16
#define B_DESERT_HILLS 17
#define B_FOREST_HILLS 18
#define B_TAIGA_HILLS 19
#define B_EXTREME_HILLS_EDGE 20
#define B_JUNGLE 21
#define B_JUNGLE_HILLS 22
#define B_JUNGLE_EDGE 23
#define B_DEEP_OCEAN 24
#define B_STONE_BEACH 25
#define B_COLD_BEACH 26
#define B_BIRCH_FOREST 27
#define B_BIRCH_FOREST_HILLS 28
#define B_ROOFED_FOREST 29
#define B_COLD_TAIGA 30
#define B_COLD_TAIGA_HILLS 31
#define B_REDWOOD_TAIGA 32
#define B_REDWOOD_TAIGA_HILLS 33
#define B_EXTREME_HILLS_WITH_TREES 34
#define B_SAVANNA 35
#define B_SAVANNA_PLATEAU 36
#define B_MESA 37
#define B_MESA_ROCK 38
#define B_MESA_CLEAR_ROCK 39
#define B_MUTATED_PLAINS 129

/* biome class groups (== getBiomeClass() identity; BiomeForestMutated extends BiomeForest and
 * BiomeSavannaMutated extends BiomeSavanna, both inheriting the hardcoded getBiomeClass() return,
 * so they share the parent class group). */
enum {
    CLS_NONE = -1, CLS_OCEAN, CLS_PLAINS, CLS_DESERT, CLS_HILLS, CLS_FOREST, CLS_TAIGA,
    CLS_SWAMP, CLS_RIVER, CLS_HELL, CLS_END, CLS_SNOW, CLS_MUSHROOM, CLS_BEACH, CLS_JUNGLE,
    CLS_STONEBEACH, CLS_SAVANNA, CLS_MESA, CLS_VOID
};
/* temp categories (Biome.TempCategory): BiomeOcean overrides to OCEAN, else from temperature. */
enum { TC_OCEAN, TC_COLD, TC_MEDIUM, TC_WARM };

MC_HD MC_NOINLINE static int gl_valid(int id) {
    if (id >= 0 && id <= 39 && id != 1 + 8 /*9 is sky, still valid*/) {} /* placeholder removed below */
    switch (id) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
        case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27:
        case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36:
        case 37: case 38: case 39: case 127:
        case 129: case 130: case 131: case 132: case 133: case 134: case 140:
        case 149: case 151: case 155: case 156: case 157: case 158:
        case 160: case 161: case 162: case 163: case 164: case 165: case 166: case 167:
            return 1;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int gl_class(int id) {
    switch (id) {
        case 0: case 10: case 24: return CLS_OCEAN;
        case 1: case 129: return CLS_PLAINS;
        case 2: case 17: case 130: return CLS_DESERT;
        case 3: case 20: case 34: case 131: case 162: return CLS_HILLS;
        case 4: case 18: case 27: case 28: case 29: case 132: case 155: case 156: case 157:
            return CLS_FOREST;
        case 5: case 19: case 30: case 31: case 32: case 33: case 133: case 158: case 160: case 161:
            return CLS_TAIGA;
        case 6: case 134: return CLS_SWAMP;
        case 7: case 11: return CLS_RIVER;
        case 8: return CLS_HELL;
        case 9: return CLS_END;
        case 12: case 13: case 140: return CLS_SNOW;
        case 14: case 15: return CLS_MUSHROOM;
        case 16: case 26: return CLS_BEACH;
        case 21: case 22: case 23: case 149: case 151: return CLS_JUNGLE;
        case 25: return CLS_STONEBEACH;
        case 35: case 36: case 163: case 164: return CLS_SAVANNA;
        case 37: case 38: case 39: case 165: case 166: case 167: return CLS_MESA;
        case 127: return CLS_VOID;
        default: return CLS_NONE;
    }
}

MC_HD MC_NOINLINE static int gl_temp(int id) {
    switch (id) {
        case 0: case 10: case 24: return TC_OCEAN;                 /* BiomeOcean override */
        case 11: case 12: case 13: case 26: case 30: case 31: case 140: case 158:
            return TC_COLD;                                        /* temperature < 0.2 */
        case 2: case 8: case 17: case 35: case 36: case 37: case 38: case 39:
        case 130: case 163: case 164: case 165: case 166: case 167:
            return TC_WARM;                                        /* temperature >= 1.0 */
        default: return TC_MEDIUM;
    }
}

MC_HD MC_NOINLINE static int gl_snowy(int id) {
    switch (id) {
        case 10: case 11: case 12: case 13: case 26: case 30: case 31: case 140: case 158:
            return 1;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int gl_is_mutation(int id) {
    /* baseBiomeRegName != null: ids 129..167 in the mutated_* range */
    switch (id) {
        case 129: case 130: case 131: case 132: case 133: case 134: case 140:
        case 149: case 151: case 155: case 156: case 157: case 158:
        case 160: case 161: case 162: case 163: case 164: case 165: case 166: case 167:
            return 1;
        default: return 0;
    }
}

/* getMutationForBiome(base): MUTATION_TO_BASE_ID_MAP.getByValue(baseId) -> mutation id or -1.
 * (reverse of the mutated_* registrations' setBaseBiome). */
MC_HD MC_NOINLINE static int gl_mutation_for(int baseId) {
    switch (baseId) {
        case 1:  return 129; case 2:  return 130; case 3:  return 131; case 4:  return 132;
        case 5:  return 133; case 6:  return 134; case 12: return 140; case 21: return 149;
        case 23: return 151; case 27: return 155; case 28: return 156; case 29: return 157;
        case 30: return 158; case 32: return 160; case 33: return 161; case 34: return 162;
        case 35: return 163; case 36: return 164; case 37: return 165; case 38: return 166;
        case 39: return 167;
        default: return -1;
    }
}

MC_HD static inline int gl_is_ocean(int id) { return id == 0 || id == 10 || id == 24; }
MC_HD MC_NOINLINE static int gl_is_mesa(int id) {
    return id == 37 || id == 38 || id == 39 || id == 165 || id == 166 || id == 167;
}
MC_HD static inline int gl_is_jungle_class(int id) { return gl_class(id) == CLS_JUNGLE; }

/* GenLayer.biomesEqualOrMesaPlateau */
MC_HD MC_NOINLINE static int gl_eq_or_mesa(int a, int b) {
    if (a == b) return 1;
    if (!gl_valid(a) || !gl_valid(b)) return 0;
    if (a == B_MESA_ROCK || a == B_MESA_CLEAR_ROCK)
        return (b == B_MESA_ROCK || b == B_MESA_CLEAR_ROCK);
    return gl_class(a) == gl_class(b);
}

/* ===== GenLayer base seeding ===== */
MC_HD MC_NOINLINE static u64 gl_basemix(i64 arg) {
    u64 bs = (u64)arg;
    bs = bs * (bs * GL_M + GL_A); bs += (u64)arg;
    bs = bs * (bs * GL_M + GL_A); bs += (u64)arg;
    bs = bs * (bs * GL_M + GL_A); bs += (u64)arg;
    return bs;
}

MC_HD MC_NOINLINE static void gl_iwgs(GLNode *nodes, int idx, i64 seed) {
    if (idx < 0) return;
    GLNode *n = &nodes[idx];
    if (n->type == GL_RIVERMIX) {
        gl_iwgs(nodes, n->parent, seed);    /* biomePatternGeneratorChain */
        gl_iwgs(nodes, n->parent2, seed);   /* riverPatternGeneratorChain */
        /* super.initWorldGenSeed: RiverMix.parent is null */
        u64 w = (u64)seed;
        w = w * (w * GL_M + GL_A); w += n->baseSeed;
        w = w * (w * GL_M + GL_A); w += n->baseSeed;
        w = w * (w * GL_M + GL_A); w += n->baseSeed;
        n->worldGenSeed = w;
        return;
    }
    /* normal: recurse this.parent only (HILLS does NOT seed its riverLayer parent2) */
    u64 w = (u64)seed;
    if (n->parent >= 0) gl_iwgs(nodes, n->parent, seed);
    w = w * (w * GL_M + GL_A); w += n->baseSeed;
    w = w * (w * GL_M + GL_A); w += n->baseSeed;
    w = w * (w * GL_M + GL_A); w += n->baseSeed;
    n->worldGenSeed = w;
}

MC_HD MC_NOINLINE static void gl_initChunkSeed(GLNode *n, i64 x, i64 z) {
    u64 cs = n->worldGenSeed;
    cs = cs * (cs * GL_M + GL_A); cs += (u64)x;
    cs = cs * (cs * GL_M + GL_A); cs += (u64)z;
    cs = cs * (cs * GL_M + GL_A); cs += (u64)x;
    cs = cs * (cs * GL_M + GL_A); cs += (u64)z;
    n->chunkSeed = cs;
}

MC_HD MC_NOINLINE static i32 gl_nextInt(GLNode *n, i32 bound) {
    i64 cs = (i64)n->chunkSeed;
    i32 i = (i32)((cs >> 24) % (i64)bound);
    if (i < 0) i += bound;
    u64 c = n->chunkSeed;
    c = c * (c * GL_M + GL_A); c += n->worldGenSeed;
    n->chunkSeed = c;
    return i;
}

MC_HD MC_NOINLINE static i32 gl_selectRandom2(GLNode *n, i32 a, i32 b) {
    i32 arr[2]; arr[0] = a; arr[1] = b;
    return arr[gl_nextInt(n, 2)];
}
MC_HD MC_NOINLINE static i32 gl_selectRandom4(GLNode *n, i32 a, i32 b, i32 c, i32 d) {
    i32 arr[4]; arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
    return arr[gl_nextInt(n, 4)];
}
/* GenLayer.selectModeOrRandom (the verbatim nested ternary, RNG only on the final fallback) */
MC_HD MC_NOINLINE static i32 gl_selModeOrRandom(GLNode *n, i32 a, i32 b, i32 c, i32 d) {
    if (b == c && c == d) return b;
    if (a == b && a == c) return a;
    if (a == b && a == d) return a;
    if (a == c && a == d) return a;
    if (a == b && c != d) return a;
    if (a == c && b != d) return a;
    if (a == d && b != c) return a;
    if (b == c && a != d) return b;
    if (b == d && a != c) return b;
    if (c == d && a != b) return c;
    return gl_selectRandom4(n, a, b, c, d);
}

/* ===== pre-allocated bump arena (IntCache substitute; NO in-kernel malloc) =====
 * gl_alloc bumps a->off; free is a no-op (arena reclaims by reset). The arena must hold the SUM
 * of every allocation within ONE top-level tree evaluation (nothing is freed mid-tree); callers
 * reset a->off = 0 ONLY at each top-level gl_getInts call site. GL_ARENA_INTS is sized by
 * measurement (see WORKQUEUE/report): max off over a seed/coord sweep of a 32x32 voronoi region,
 * rounded up. Instrument with -DGL_ARENA_INSTRUMENT to re-measure. */
#ifndef GL_ARENA_INTS
#define GL_ARENA_INTS 65536   /* measured max off = 16655 ints over a seed/coord sweep incl. 32x32 */
#endif
typedef struct { int buf[GL_ARENA_INTS]; int off; } GlArena;

#ifdef GL_ARENA_INSTRUMENT
static int g_gl_arena_max = 0;
#endif

/* ===== getInts dispatch (forward decl for mutual recursion) ===== */
MC_HD MC_NOINLINE static int *gl_getInts(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY,
                                    int areaWidth, int areaHeight);

MC_HD static inline int *gl_alloc(GlArena *a, int n) {
    int *p = &a->buf[a->off];
    a->off += n;
#ifdef GL_ARENA_INSTRUMENT
    if (a->off > g_gl_arena_max) g_gl_arena_max = a->off;
#endif
    MC_ASSERT(a->off <= GL_ARENA_INTS);
    return p;
}

/* ---- GenLayerIsland ---- */
MC_HD MC_NOINLINE static int *gl_island(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(areaX + j), (i64)(areaY + i));
            aint[j + i * aw] = gl_nextInt(n, 10) == 0 ? 1 : 0;
        }
    }
    if (areaX > -aw && areaX <= 0 && areaY > -ah && areaY <= 0)
        aint[-areaX + -areaY * aw] = 1;
    return aint;
}

/* ---- GenLayerZoom / GenLayerFuzzyZoom ---- */
MC_HD MC_NOINLINE static int *gl_zoom(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah,
                                 int isFuzzy) {
    GLNode *n = &nodes[idx];
    int i = areaX >> 1;
    int j = areaY >> 1;
    int k = (aw >> 1) + 2;
    int l = (ah >> 1) + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int i1 = (k - 1) << 1;
    int j1 = (l - 1) << 1;
    int *aint1 = gl_alloc(a, i1 * j1);
    for (int k1 = 0; k1 < l - 1; ++k1) {
        int l1 = (k1 << 1) * i1;
        int i2 = 0;
        int j2 = aint[i2 + 0 + (k1 + 0) * k];
        int k2 = aint[i2 + 0 + (k1 + 1) * k];
        for (; i2 < k - 1; ++i2) {
            gl_initChunkSeed(n, (i64)(i32)((i2 + i) << 1), (i64)(i32)((k1 + j) << 1));
            int l2 = aint[i2 + 1 + (k1 + 0) * k];
            int i3 = aint[i2 + 1 + (k1 + 1) * k];
            aint1[l1] = j2;
            { i32 v = gl_selectRandom2(n, j2, k2); aint1[l1 + i1] = v; l1++; }
            { i32 v = gl_selectRandom2(n, j2, l2); aint1[l1] = v; }
            { i32 v = isFuzzy ? gl_selectRandom4(n, j2, l2, k2, i3)
                              : gl_selModeOrRandom(n, j2, l2, k2, i3);
              aint1[l1 + i1] = v; l1++; }
            j2 = l2;
            k2 = i3;
        }
    }
    int *aint2 = gl_alloc(a, aw * ah);
    for (int j3 = 0; j3 < ah; ++j3) {
        int src = (j3 + (areaY & 1)) * i1 + (areaX & 1);
        for (int c = 0; c < aw; ++c) aint2[j3 * aw + c] = aint1[src + c];
    }
     
    return aint2;
}

/* ---- GenLayerAddIsland ---- */
MC_HD MC_NOINLINE static int *gl_addisland(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 0 + (i1 + 0) * k];
            int l1 = aint[j1 + 2 + (i1 + 0) * k];
            int i2 = aint[j1 + 0 + (i1 + 2) * k];
            int j2 = aint[j1 + 2 + (i1 + 2) * k];
            int k2 = aint[j1 + 1 + (i1 + 1) * k];
            gl_initChunkSeed(n, (i64)(j1 + areaX), (i64)(i1 + areaY));
            if (k2 != 0 || (k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0)) {
                if (k2 > 0 && (k1 == 0 || l1 == 0 || i2 == 0 || j2 == 0)) {
                    if (gl_nextInt(n, 5) == 0)
                        aint1[j1 + i1 * aw] = (k2 == 4) ? 4 : 0;
                    else
                        aint1[j1 + i1 * aw] = k2;
                } else {
                    aint1[j1 + i1 * aw] = k2;
                }
            } else {
                int l2 = 1;
                int i3 = 1;
                if (k1 != 0 && gl_nextInt(n, l2++) == 0) i3 = k1;
                if (l1 != 0 && gl_nextInt(n, l2++) == 0) i3 = l1;
                if (i2 != 0 && gl_nextInt(n, l2++) == 0) i3 = i2;
                if (j2 != 0 && gl_nextInt(n, l2++) == 0) i3 = j2;
                if (gl_nextInt(n, 3) == 0) aint1[j1 + i1 * aw] = i3;
                else if (i3 == 4) aint1[j1 + i1 * aw] = 4;
                else aint1[j1 + i1 * aw] = 0;
            }
        }
    }
    
    return aint1;
}

/* ---- GenLayerRemoveTooMuchOcean ---- */
MC_HD MC_NOINLINE static int *gl_removeocean(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (aw + 2)];
            int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (aw + 2)];
            int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (aw + 2)];
            int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (aw + 2)];
            int k2 = aint[j1 + 1 + (i1 + 1) * k];
            aint1[j1 + i1 * aw] = k2;
            gl_initChunkSeed(n, (i64)(j1 + areaX), (i64)(i1 + areaY));
            if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && gl_nextInt(n, 2) == 0)
                aint1[j1 + i1 * aw] = 1;
        }
    }
    
    return aint1;
}

/* ---- GenLayerAddSnow ---- */
MC_HD MC_NOINLINE static int *gl_addsnow(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 1 + (i1 + 1) * k];
            gl_initChunkSeed(n, (i64)(j1 + areaX), (i64)(i1 + areaY));
            if (k1 == 0) {
                aint1[j1 + i1 * aw] = 0;
            } else {
                int l1 = gl_nextInt(n, 6);
                if (l1 == 0) l1 = 4;
                else if (l1 <= 1) l1 = 3;
                else l1 = 1;
                aint1[j1 + i1 * aw] = l1;
            }
        }
    }
    
    return aint1;
}

/* ---- GenLayerEdge COOL_WARM ---- */
MC_HD MC_NOINLINE static int *gl_edge_coolwarm(GLNode *nodes, GlArena *a, int idx, int p1, int p2, int p3, int p4) {
    GLNode *n = &nodes[idx];
    int i = p1 - 1, j = p2 - 1, k = 1 + p3 + 1, l = 1 + p4 + 1;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, p3 * p4);
    for (int i1 = 0; i1 < p4; ++i1) {
        for (int j1 = 0; j1 < p3; ++j1) {
            gl_initChunkSeed(n, (i64)(j1 + p1), (i64)(i1 + p2));
            int k1 = aint[j1 + 1 + (i1 + 1) * k];
            if (k1 == 1) {
                int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                int flag = (l1 == 3 || i2 == 3 || j2 == 3 || k2 == 3);
                int flag1 = (l1 == 4 || i2 == 4 || j2 == 4 || k2 == 4);
                if (flag || flag1) k1 = 2;
            }
            aint1[j1 + i1 * p3] = k1;
        }
    }
    
    return aint1;
}

/* ---- GenLayerEdge HEAT_ICE ---- */
MC_HD MC_NOINLINE static int *gl_edge_heatice(GLNode *nodes, GlArena *a, int idx, int p1, int p2, int p3, int p4) {
    GLNode *n = &nodes[idx];
    int i = p1 - 1, j = p2 - 1, k = 1 + p3 + 1, l = 1 + p4 + 1;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, p3 * p4);
    for (int i1 = 0; i1 < p4; ++i1) {
        for (int j1 = 0; j1 < p3; ++j1) {
            int k1 = aint[j1 + 1 + (i1 + 1) * k];
            if (k1 == 4) {
                int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                int flag = (l1 == 2 || i2 == 2 || j2 == 2 || k2 == 2);
                int flag1 = (l1 == 1 || i2 == 1 || j2 == 1 || k2 == 1);
                if (flag1 || flag) k1 = 3;
            }
            aint1[j1 + i1 * p3] = k1;
        }
    }
    
    return aint1;
}

/* ---- GenLayerEdge SPECIAL ---- */
MC_HD MC_NOINLINE static int *gl_edge_special(GLNode *nodes, GlArena *a, int idx, int p1, int p2, int p3, int p4) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, p1, p2, p3, p4);
    int *aint1 = gl_alloc(a, p3 * p4);
    for (int i = 0; i < p4; ++i) {
        for (int j = 0; j < p3; ++j) {
            gl_initChunkSeed(n, (i64)(j + p1), (i64)(i + p2));
            int k = aint[j + i * p3];
            if (k != 0 && gl_nextInt(n, 13) == 0)
                k |= ((1 + gl_nextInt(n, 15)) << 8) & 3840;
            aint1[j + i * p3] = k;
        }
    }
    
    return aint1;
}

/* ---- GenLayerAddMushroomIsland ---- */
MC_HD MC_NOINLINE static int *gl_addmushroom(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 0 + (i1 + 0) * k];
            int l1 = aint[j1 + 2 + (i1 + 0) * k];
            int i2 = aint[j1 + 0 + (i1 + 2) * k];
            int j2 = aint[j1 + 2 + (i1 + 2) * k];
            int k2 = aint[j1 + 1 + (i1 + 1) * k];
            gl_initChunkSeed(n, (i64)(j1 + areaX), (i64)(i1 + areaY));
            if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && gl_nextInt(n, 100) == 0)
                aint1[j1 + i1 * aw] = B_MUSHROOM;
            else
                aint1[j1 + i1 * aw] = k2;
        }
    }
    
    return aint1;
}

/* ---- GenLayerDeepOcean ---- */
MC_HD MC_NOINLINE static int *gl_deepocean(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (aw + 2)];
            int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (aw + 2)];
            int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (aw + 2)];
            int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (aw + 2)];
            int k2 = aint[j1 + 1 + (i1 + 1) * k];
            int l2 = 0;
            if (k1 == 0) ++l2;
            if (l1 == 0) ++l2;
            if (i2 == 0) ++l2;
            if (j2 == 0) ++l2;
            if (k2 == 0 && l2 > 3) aint1[j1 + i1 * aw] = B_DEEP_OCEAN;
            else aint1[j1 + i1 * aw] = k2;
        }
    }
    
    return aint1;
}

/* ---- GenLayerRiverInit ---- */
MC_HD MC_NOINLINE static int *gl_riverinit(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX, areaY, aw, ah);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            aint1[j + i * aw] = aint[j + i * aw] > 0 ? gl_nextInt(n, 299999) + 2 : 0;
        }
    }
    
    return aint1;
}

/* ---- GenLayerBiome weighted lists (vanilla default, unmodded -> isTypeListModded == false) ---- */
/* type: 0=DESERT,1=WARM,2=COOL,3=ICY. Returns chosen biome id. */
MC_HD MC_NOINLINE static int gl_weighted_biome(GLNode *n, int type) {
    int ids[8]; int wts[8]; int cnt = 0; int total = 0;
    if (type == 0) {        /* DESERT(WARM-ish) */
        ids[0]=B_DESERT; wts[0]=30; ids[1]=B_SAVANNA; wts[1]=20; ids[2]=B_PLAINS; wts[2]=10; cnt=3;
    } else if (type == 1) { /* WARM */
        ids[0]=B_FOREST; wts[0]=10; ids[1]=B_ROOFED_FOREST; wts[1]=10; ids[2]=B_EXTREME_HILLS; wts[2]=10;
        ids[3]=B_PLAINS; wts[3]=10; ids[4]=B_BIRCH_FOREST; wts[4]=10; ids[5]=B_SWAMP; wts[5]=10; cnt=6;
    } else if (type == 2) { /* COOL */
        ids[0]=B_FOREST; wts[0]=10; ids[1]=B_EXTREME_HILLS; wts[1]=10; ids[2]=B_TAIGA; wts[2]=10;
        ids[3]=B_PLAINS; wts[3]=10; cnt=4;
    } else {                /* ICY */
        ids[0]=B_ICE_PLAINS; wts[0]=30; ids[1]=B_COLD_TAIGA; wts[1]=10; cnt=2;
    }
    for (int z = 0; z < cnt; ++z) total += wts[z];
    int weight = gl_nextInt(n, total / 10) * 10;   /* unmodded path */
    for (int z = 0; z < cnt; ++z) {
        weight -= wts[z];
        if (weight < 0) return ids[z];
    }
    return ids[cnt - 1];   /* unreachable for these totals */
}

/* ---- GenLayerBiome ---- */
MC_HD MC_NOINLINE static int *gl_biome(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX, areaY, aw, ah);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            int k = aint[j + i * aw];
            int l = (k & 3840) >> 8;
            k = k & -3841;
            /* settings == null for DEFAULT overworld -> no fixedBiome branch */
            if (gl_is_ocean(k)) {
                aint1[j + i * aw] = k;
            } else if (k == B_MUSHROOM) {
                aint1[j + i * aw] = k;
            } else if (k == 1) {
                if (l > 0)
                    aint1[j + i * aw] = (gl_nextInt(n, 3) == 0) ? B_MESA_CLEAR_ROCK : B_MESA_ROCK;
                else
                    aint1[j + i * aw] = gl_weighted_biome(n, 0);
            } else if (k == 2) {
                if (l > 0) aint1[j + i * aw] = B_JUNGLE;
                else aint1[j + i * aw] = gl_weighted_biome(n, 1);
            } else if (k == 3) {
                if (l > 0) aint1[j + i * aw] = B_REDWOOD_TAIGA;
                else aint1[j + i * aw] = gl_weighted_biome(n, 2);
            } else if (k == 4) {
                aint1[j + i * aw] = gl_weighted_biome(n, 3);
            } else {
                aint1[j + i * aw] = B_MUSHROOM;
            }
        }
    }
    
    return aint1;
}

/* ---- GenLayerBiomeEdge helpers ---- */
MC_HD MC_NOINLINE static int gl_canNeighbors(int a, int b) {
    if (gl_eq_or_mesa(a, b)) return 1;
    if (!gl_valid(a) || !gl_valid(b)) return 0;
    int ta = gl_temp(a), tb = gl_temp(b);
    return ta == tb || ta == TC_MEDIUM || tb == TC_MEDIUM;
}

MC_HD MC_NOINLINE static int gl_replaceEdgeIfNec(const int *p, int *o, int j, int i, int w,
                                            int center, int from, int to) {
    if (!gl_eq_or_mesa(center, from)) return 0;
    int a = p[j + 1 + (i + 1 - 1) * (w + 2)];
    int b = p[j + 1 + 1 + (i + 1) * (w + 2)];
    int c = p[j + 1 - 1 + (i + 1) * (w + 2)];
    int d = p[j + 1 + (i + 1 + 1) * (w + 2)];
    if (gl_canNeighbors(a, from) && gl_canNeighbors(b, from) &&
        gl_canNeighbors(c, from) && gl_canNeighbors(d, from))
        o[j + i * w] = center;
    else
        o[j + i * w] = to;
    return 1;
}

MC_HD MC_NOINLINE static int gl_replaceEdge(const int *p, int *o, int j, int i, int w,
                                       int center, int from, int to) {
    if (center != from) return 0;
    int a = p[j + 1 + (i + 1 - 1) * (w + 2)];
    int b = p[j + 1 + 1 + (i + 1) * (w + 2)];
    int c = p[j + 1 - 1 + (i + 1) * (w + 2)];
    int d = p[j + 1 + (i + 1 + 1) * (w + 2)];
    if (gl_eq_or_mesa(a, from) && gl_eq_or_mesa(b, from) &&
        gl_eq_or_mesa(c, from) && gl_eq_or_mesa(d, from))
        o[j + i * w] = center;
    else
        o[j + i * w] = to;
    return 1;
}

/* ---- GenLayerBiomeEdge ---- */
MC_HD MC_NOINLINE static int *gl_biomeedge(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX - 1, areaY - 1, aw + 2, ah + 2);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            int k = aint[j + 1 + (i + 1) * (aw + 2)];
            if (!gl_replaceEdgeIfNec(aint, aint1, j, i, aw, k, B_EXTREME_HILLS, B_EXTREME_HILLS_EDGE)
                && !gl_replaceEdge(aint, aint1, j, i, aw, k, B_MESA_ROCK, B_MESA)
                && !gl_replaceEdge(aint, aint1, j, i, aw, k, B_MESA_CLEAR_ROCK, B_MESA)
                && !gl_replaceEdge(aint, aint1, j, i, aw, k, B_REDWOOD_TAIGA, B_TAIGA)) {
                if (k == B_DESERT) {
                    int l1 = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                    int i2 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                    int j2 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                    int k2 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                    if (l1 != B_ICE_PLAINS && i2 != B_ICE_PLAINS && j2 != B_ICE_PLAINS && k2 != B_ICE_PLAINS)
                        aint1[j + i * aw] = k;
                    else
                        aint1[j + i * aw] = B_EXTREME_HILLS_WITH_TREES;
                } else if (k == B_SWAMP) {
                    int l = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                    int i1 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                    int j1 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                    int k1 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                    if (l != B_DESERT && i1 != B_DESERT && j1 != B_DESERT && k1 != B_DESERT
                        && l != B_COLD_TAIGA && i1 != B_COLD_TAIGA && j1 != B_COLD_TAIGA && k1 != B_COLD_TAIGA
                        && l != B_ICE_PLAINS && i1 != B_ICE_PLAINS && j1 != B_ICE_PLAINS && k1 != B_ICE_PLAINS) {
                        if (l != B_JUNGLE && k1 != B_JUNGLE && i1 != B_JUNGLE && j1 != B_JUNGLE)
                            aint1[j + i * aw] = k;
                        else
                            aint1[j + i * aw] = B_JUNGLE_EDGE;
                    } else {
                        aint1[j + i * aw] = B_PLAINS;
                    }
                } else {
                    aint1[j + i * aw] = k;
                }
            }
        }
    }
    
    return aint1;
}

/* ---- GenLayerHills ---- */
MC_HD MC_NOINLINE static int *gl_hills(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX - 1, areaY - 1, aw + 2, ah + 2);
    int *aint1 = gl_getInts(nodes, a, n->parent2, areaX - 1, areaY - 1, aw + 2, ah + 2);
    int *aint2 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            int k = aint[j + 1 + (i + 1) * (aw + 2)];
            int l = aint1[j + 1 + (i + 1) * (aw + 2)];
            int flag = ((l - 2) % 29 == 0);
            int biomeValid = gl_valid(k);
            int flag1 = biomeValid && gl_is_mutation(k);
            if (k != 0 && l >= 2 && (l - 2) % 29 == 1 && !flag1) {
                int m = gl_mutation_for(k);
                aint2[j + i * aw] = (m < 0) ? k : m;
            } else if (gl_nextInt(n, 3) != 0 && !flag) {
                aint2[j + i * aw] = k;
            } else {
                int biome1 = k;
                if (k == B_DESERT) biome1 = B_DESERT_HILLS;
                else if (k == B_FOREST) biome1 = B_FOREST_HILLS;
                else if (k == B_BIRCH_FOREST) biome1 = B_BIRCH_FOREST_HILLS;
                else if (k == B_ROOFED_FOREST) biome1 = B_PLAINS;
                else if (k == B_TAIGA) biome1 = B_TAIGA_HILLS;
                else if (k == B_REDWOOD_TAIGA) biome1 = B_REDWOOD_TAIGA_HILLS;
                else if (k == B_COLD_TAIGA) biome1 = B_COLD_TAIGA_HILLS;
                else if (k == B_PLAINS) biome1 = (gl_nextInt(n, 3) == 0) ? B_FOREST_HILLS : B_FOREST;
                else if (k == B_ICE_PLAINS) biome1 = B_ICE_MOUNTAINS;
                else if (k == B_JUNGLE) biome1 = B_JUNGLE_HILLS;
                else if (k == B_OCEAN) biome1 = B_DEEP_OCEAN;
                else if (k == B_EXTREME_HILLS) biome1 = B_EXTREME_HILLS_WITH_TREES;
                else if (k == B_SAVANNA) biome1 = B_SAVANNA_PLATEAU;
                else if (gl_eq_or_mesa(k, B_MESA_ROCK)) biome1 = B_MESA;
                else if (k == B_DEEP_OCEAN && gl_nextInt(n, 3) == 0) {
                    int i1 = gl_nextInt(n, 2);
                    biome1 = (i1 == 0) ? B_PLAINS : B_FOREST;
                }
                int j2 = biome1;
                if (flag && j2 != k) {
                    int m = gl_mutation_for(biome1);
                    j2 = (m < 0) ? k : m;
                }
                if (j2 == k) {
                    aint2[j + i * aw] = k;
                } else {
                    int k2 = aint[j + 1 + (i + 0) * (aw + 2)];
                    int j1 = aint[j + 2 + (i + 1) * (aw + 2)];
                    int k1 = aint[j + 0 + (i + 1) * (aw + 2)];
                    int l1 = aint[j + 1 + (i + 2) * (aw + 2)];
                    int i2 = 0;
                    if (gl_eq_or_mesa(k2, k)) ++i2;
                    if (gl_eq_or_mesa(j1, k)) ++i2;
                    if (gl_eq_or_mesa(k1, k)) ++i2;
                    if (gl_eq_or_mesa(l1, k)) ++i2;
                    if (i2 >= 3) aint2[j + i * aw] = j2;
                    else aint2[j + i * aw] = k;
                }
            }
        }
    }
     
    return aint2;
}

/* ---- GenLayerRiver ---- */
MC_HD static inline int gl_riverFilter(int v) { return v >= 2 ? 2 + (v & 1) : v; }
MC_HD MC_NOINLINE static int *gl_river(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = gl_riverFilter(aint[j1 + 0 + (i1 + 1) * k]);
            int l1 = gl_riverFilter(aint[j1 + 2 + (i1 + 1) * k]);
            int i2 = gl_riverFilter(aint[j1 + 1 + (i1 + 0) * k]);
            int j2 = gl_riverFilter(aint[j1 + 1 + (i1 + 2) * k]);
            int k2 = gl_riverFilter(aint[j1 + 1 + (i1 + 1) * k]);
            if (k2 == k1 && k2 == i2 && k2 == l1 && k2 == j2)
                aint1[j1 + i1 * aw] = -1;
            else
                aint1[j1 + i1 * aw] = B_RIVER;
        }
    }
    
    return aint1;
}

/* ---- GenLayerSmooth ---- */
MC_HD MC_NOINLINE static int *gl_smooth(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int i = areaX - 1, j = areaY - 1, k = aw + 2, l = ah + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i1 = 0; i1 < ah; ++i1) {
        for (int j1 = 0; j1 < aw; ++j1) {
            int k1 = aint[j1 + 0 + (i1 + 1) * k];
            int l1 = aint[j1 + 2 + (i1 + 1) * k];
            int i2 = aint[j1 + 1 + (i1 + 0) * k];
            int j2 = aint[j1 + 1 + (i1 + 2) * k];
            int k2 = aint[j1 + 1 + (i1 + 1) * k];
            if (k1 == l1 && i2 == j2) {
                gl_initChunkSeed(n, (i64)(j1 + areaX), (i64)(i1 + areaY));
                if (gl_nextInt(n, 2) == 0) k2 = k1;
                else k2 = i2;
            } else {
                if (k1 == l1) k2 = k1;
                if (i2 == j2) k2 = i2;
            }
            aint1[j1 + i1 * aw] = k2;
        }
    }
    
    return aint1;
}

/* ---- GenLayerRareBiome ---- */
MC_HD MC_NOINLINE static int *gl_rarebiome(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX - 1, areaY - 1, aw + 2, ah + 2);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            int k = aint[j + 1 + (i + 1) * (aw + 2)];
            if (gl_nextInt(n, 57) == 0 && k == B_PLAINS)
                aint1[j + i * aw] = B_MUTATED_PLAINS;
            else
                aint1[j + i * aw] = k;
        }
    }
    
    return aint1;
}

/* ---- GenLayerShore ---- */
MC_HD MC_NOINLINE static int gl_isJungleCompatible(int id) {
    if (gl_valid(id) && gl_class(id) == CLS_JUNGLE) return 1;
    return id == B_JUNGLE_EDGE || id == B_JUNGLE || id == B_JUNGLE_HILLS
        || id == B_FOREST || id == B_TAIGA || gl_is_ocean(id);
}
MC_HD MC_NOINLINE static void gl_replaceIfNeighborOcean(const int *p, int *o, int x, int y, int w,
                                                   int center, int repl) {
    if (gl_is_ocean(center)) {
        o[x + y * w] = center;
    } else {
        int a = p[x + 1 + (y + 1 - 1) * (w + 2)];
        int b = p[x + 1 + 1 + (y + 1) * (w + 2)];
        int c = p[x + 1 - 1 + (y + 1) * (w + 2)];
        int d = p[x + 1 + (y + 1 + 1) * (w + 2)];
        if (!gl_is_ocean(a) && !gl_is_ocean(b) && !gl_is_ocean(c) && !gl_is_ocean(d))
            o[x + y * w] = center;
        else
            o[x + y * w] = repl;
    }
}
MC_HD MC_NOINLINE static int *gl_shore(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX - 1, areaY - 1, aw + 2, ah + 2);
    int *aint1 = gl_alloc(a, aw * ah);
    for (int i = 0; i < ah; ++i) {
        for (int j = 0; j < aw; ++j) {
            gl_initChunkSeed(n, (i64)(j + areaX), (i64)(i + areaY));
            int k = aint[j + 1 + (i + 1) * (aw + 2)];
            int isJungle = gl_valid(k) && gl_class(k) == CLS_JUNGLE;
            if (k == B_MUSHROOM) {
                int j2 = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                int i3 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                int l3 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                int k4 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                if (j2 != B_OCEAN && i3 != B_OCEAN && l3 != B_OCEAN && k4 != B_OCEAN)
                    aint1[j + i * aw] = k;
                else
                    aint1[j + i * aw] = B_MUSHROOM_SHORE;
            } else if (isJungle) {
                int i2 = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                int l2 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                int k3 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                int j4 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                if (gl_isJungleCompatible(i2) && gl_isJungleCompatible(l2)
                    && gl_isJungleCompatible(k3) && gl_isJungleCompatible(j4)) {
                    if (!gl_is_ocean(i2) && !gl_is_ocean(l2) && !gl_is_ocean(k3) && !gl_is_ocean(j4))
                        aint1[j + i * aw] = k;
                    else
                        aint1[j + i * aw] = B_BEACH;
                } else {
                    aint1[j + i * aw] = B_JUNGLE_EDGE;
                }
            } else if (k != B_EXTREME_HILLS && k != B_EXTREME_HILLS_WITH_TREES && k != B_EXTREME_HILLS_EDGE) {
                int snowy = gl_valid(k) && gl_snowy(k);
                if (snowy) {
                    gl_replaceIfNeighborOcean(aint, aint1, j, i, aw, k, B_COLD_BEACH);
                } else if (k != B_MESA && k != B_MESA_ROCK) {
                    if (k != B_OCEAN && k != B_DEEP_OCEAN && k != B_RIVER && k != B_SWAMP) {
                        int l1 = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                        int k2 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                        int j3 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                        int i4 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                        if (!gl_is_ocean(l1) && !gl_is_ocean(k2) && !gl_is_ocean(j3) && !gl_is_ocean(i4))
                            aint1[j + i * aw] = k;
                        else
                            aint1[j + i * aw] = B_BEACH;
                    } else {
                        aint1[j + i * aw] = k;
                    }
                } else {
                    int l = aint[j + 1 + (i + 1 - 1) * (aw + 2)];
                    int i1 = aint[j + 1 + 1 + (i + 1) * (aw + 2)];
                    int j1 = aint[j + 1 - 1 + (i + 1) * (aw + 2)];
                    int k1 = aint[j + 1 + (i + 1 + 1) * (aw + 2)];
                    if (!gl_is_ocean(l) && !gl_is_ocean(i1) && !gl_is_ocean(j1) && !gl_is_ocean(k1)) {
                        if (gl_is_mesa(l) && gl_is_mesa(i1) && gl_is_mesa(j1) && gl_is_mesa(k1))
                            aint1[j + i * aw] = k;
                        else
                            aint1[j + i * aw] = B_DESERT;
                    } else {
                        aint1[j + i * aw] = k;
                    }
                }
            } else {
                gl_replaceIfNeighborOcean(aint, aint1, j, i, aw, k, B_STONE_BEACH);
            }
        }
    }
    
    return aint1;
}

/* ---- GenLayerRiverMix ---- */
MC_HD MC_NOINLINE static int *gl_rivermix(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    int *aint = gl_getInts(nodes, a, n->parent, areaX, areaY, aw, ah);
    int *aint1 = gl_getInts(nodes, a, n->parent2, areaX, areaY, aw, ah);
    int *aint2 = gl_alloc(a, aw * ah);
    for (int i = 0; i < aw * ah; ++i) {
        if (aint[i] != B_OCEAN && aint[i] != B_DEEP_OCEAN) {
            if (aint1[i] == B_RIVER) {
                if (aint[i] == B_ICE_PLAINS)
                    aint2[i] = B_FROZEN_RIVER;
                else if (aint[i] != B_MUSHROOM && aint[i] != B_MUSHROOM_SHORE)
                    aint2[i] = aint1[i] & 255;
                else
                    aint2[i] = B_MUSHROOM_SHORE;
            } else {
                aint2[i] = aint[i];
            }
        } else {
            aint2[i] = aint[i];
        }
    }
     
    return aint2;
}

/* ---- GenLayerVoronoiZoom ---- */
MC_HD MC_NOINLINE static int *gl_voronoi(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    GLNode *n = &nodes[idx];
    areaX = areaX - 2;
    areaY = areaY - 2;
    int i = areaX >> 2;
    int j = areaY >> 2;
    int k = (aw >> 2) + 2;
    int l = (ah >> 2) + 2;
    int *aint = gl_getInts(nodes, a, n->parent, i, j, k, l);
    int i1 = (k - 1) << 2;
    int j1 = (l - 1) << 2;
    int *aint1 = gl_alloc(a, i1 * j1);
    for (int k1 = 0; k1 < l - 1; ++k1) {
        int l1 = 0;
        int i2 = aint[l1 + 0 + (k1 + 0) * k];
        int j2 = aint[l1 + 0 + (k1 + 1) * k];
        for (; l1 < k - 1; ++l1) {
            gl_initChunkSeed(n, (i64)(i32)(((l1 + i) << 2)), (i64)(i32)(((k1 + j) << 2)));
            double d1 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6;
            double d2 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6;
            gl_initChunkSeed(n, (i64)(i32)(((l1 + i + 1) << 2)), (i64)(i32)(((k1 + j) << 2)));
            double d3 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6 + 4.0;
            double d4 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6;
            gl_initChunkSeed(n, (i64)(i32)(((l1 + i) << 2)), (i64)(i32)(((k1 + j + 1) << 2)));
            double d5 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6;
            double d6 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6 + 4.0;
            gl_initChunkSeed(n, (i64)(i32)(((l1 + i + 1) << 2)), (i64)(i32)(((k1 + j + 1) << 2)));
            double d7 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6 + 4.0;
            double d8 = ((double)gl_nextInt(n, 1024) / 1024.0 - 0.5) * 3.6 + 4.0;
            int k2 = aint[l1 + 1 + (k1 + 0) * k] & 255;
            int l2 = aint[l1 + 1 + (k1 + 1) * k] & 255;
            for (int i3 = 0; i3 < 4; ++i3) {
                int j3 = ((k1 << 2) + i3) * i1 + (l1 << 2);
                for (int k3 = 0; k3 < 4; ++k3) {
                    double d9 = ((double)i3 - d2) * ((double)i3 - d2) + ((double)k3 - d1) * ((double)k3 - d1);
                    double d10 = ((double)i3 - d4) * ((double)i3 - d4) + ((double)k3 - d3) * ((double)k3 - d3);
                    double d11 = ((double)i3 - d6) * ((double)i3 - d6) + ((double)k3 - d5) * ((double)k3 - d5);
                    double d12 = ((double)i3 - d8) * ((double)i3 - d8) + ((double)k3 - d7) * ((double)k3 - d7);
                    if (d9 < d10 && d9 < d11 && d9 < d12) aint1[j3++] = i2;
                    else if (d10 < d9 && d10 < d11 && d10 < d12) aint1[j3++] = k2;
                    else if (d11 < d9 && d11 < d10 && d11 < d12) aint1[j3++] = j2;
                    else aint1[j3++] = l2;
                }
            }
            i2 = k2;
            j2 = l2;
        }
    }
    int *aint2 = gl_alloc(a, aw * ah);
    for (int l3 = 0; l3 < ah; ++l3) {
        int src = (l3 + (areaY & 3)) * i1 + (areaX & 3);
        for (int c = 0; c < aw; ++c) aint2[l3 * aw + c] = aint1[src + c];
    }
     
    return aint2;
}

MC_HD MC_NOINLINE static int *gl_getInts(GLNode *nodes, GlArena *a, int idx, int areaX, int areaY, int aw, int ah) {
    switch (nodes[idx].type) {
        case GL_ISLAND:       return gl_island(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_FUZZYZOOM:    return gl_zoom(nodes, a, idx, areaX, areaY, aw, ah, 1);
        case GL_ZOOM:         return gl_zoom(nodes, a, idx, areaX, areaY, aw, ah, 0);
        case GL_ADDISLAND:    return gl_addisland(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_REMOVEOCEAN:  return gl_removeocean(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_ADDSNOW:      return gl_addsnow(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_EDGE_COOLWARM:return gl_edge_coolwarm(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_EDGE_HEATICE: return gl_edge_heatice(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_EDGE_SPECIAL: return gl_edge_special(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_ADDMUSHROOM:  return gl_addmushroom(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_DEEPOCEAN:    return gl_deepocean(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_RIVERINIT:    return gl_riverinit(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_BIOME:        return gl_biome(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_BIOMEEDGE:    return gl_biomeedge(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_HILLS:        return gl_hills(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_RIVER:        return gl_river(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_SMOOTH:       return gl_smooth(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_RAREBIOME:    return gl_rarebiome(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_SHORE:        return gl_shore(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_RIVERMIX:     return gl_rivermix(nodes, a, idx, areaX, areaY, aw, ah);
        case GL_VORONOIZOOM:  return gl_voronoi(nodes, a, idx, areaX, areaY, aw, ah);
        default: return gl_alloc(a, aw * ah);
    }
}

/* ===== build the DEFAULT overworld biome stack (GenLayer.initializeAllBiomeGenerators +
 * WorldType.DEFAULT.getBiomeLayer), settings == null, biomeSize = riverSize = 4. Returns node
 * count; sets *biomeIndex to the full-resolution layer (genlayer3 = VoronoiZoom). ===== */
#define GL_MAX_NODES 64

MC_HD MC_NOINLINE static int gl_add(GLNode *nodes, int *n, int type, i64 basearg, int parent, int parent2) {
    int idx = *n;
    nodes[idx].type = type;
    nodes[idx].parent = parent;
    nodes[idx].parent2 = parent2;
    nodes[idx].baseSeed = gl_basemix(basearg);
    nodes[idx].worldGenSeed = 0;
    nodes[idx].chunkSeed = 0;
    (*n)++;
    return idx;
}
MC_HD MC_NOINLINE static int gl_magnify(GLNode *nodes, int *n, i64 base, int start, int count) {
    int cur = start;
    for (int i = 0; i < count; ++i) cur = gl_add(nodes, n, GL_ZOOM, base + (i64)i, cur, -1);
    return cur;
}

MC_HD MC_NOINLINE static int gl_build(GLNode *nodes, i64 seed, int *biomeIndex) {
    int n = 0;
    int g = gl_add(nodes, &n, GL_ISLAND, 1, -1, -1);
    g = gl_add(nodes, &n, GL_FUZZYZOOM, 2000, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 1, g, -1);
    g = gl_add(nodes, &n, GL_ZOOM, 2001, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 2, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 50, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 70, g, -1);
    g = gl_add(nodes, &n, GL_REMOVEOCEAN, 2, g, -1);
    g = gl_add(nodes, &n, GL_ADDSNOW, 2, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 3, g, -1);
    g = gl_add(nodes, &n, GL_EDGE_COOLWARM, 2, g, -1);
    g = gl_add(nodes, &n, GL_EDGE_HEATICE, 2, g, -1);
    g = gl_add(nodes, &n, GL_EDGE_SPECIAL, 3, g, -1);
    g = gl_add(nodes, &n, GL_ZOOM, 2002, g, -1);
    g = gl_add(nodes, &n, GL_ZOOM, 2003, g, -1);
    g = gl_add(nodes, &n, GL_ADDISLAND, 4, g, -1);
    g = gl_add(nodes, &n, GL_ADDMUSHROOM, 5, g, -1);
    g = gl_add(nodes, &n, GL_DEEPOCEAN, 4, g, -1);
    int genlayer4 = g;                              /* magnify(1000, ., 0) == identity */
    int sz = 4;                                     /* biomeSize (DEFAULT, settings null) */
    int riverSize = 4;
    int lvt_7_1 = genlayer4;                        /* magnify(1000, genlayer4, 0) */
    int riverinit = gl_add(nodes, &n, GL_RIVERINIT, 100, lvt_7_1, -1);
    int lvt_9_1 = gl_magnify(nodes, &n, 1000, riverinit, 2);   /* riverLayer for Hills (unseeded) */
    int bi = gl_add(nodes, &n, GL_BIOME, 200, genlayer4, -1);
    int be = gl_magnify(nodes, &n, 1000, bi, 2);
    be = gl_add(nodes, &n, GL_BIOMEEDGE, 1000, be, -1);
    int hills = gl_add(nodes, &n, GL_HILLS, 1000, be, lvt_9_1);
    int g5 = gl_magnify(nodes, &n, 1000, riverinit, 2);
    g5 = gl_magnify(nodes, &n, 1000, g5, riverSize);
    int river = gl_add(nodes, &n, GL_RIVER, 1, g5, -1);
    int smooth = gl_add(nodes, &n, GL_SMOOTH, 1000, river, -1);
    hills = gl_add(nodes, &n, GL_RAREBIOME, 1001, hills, -1);
    for (int k = 0; k < sz; ++k) {
        hills = gl_add(nodes, &n, GL_ZOOM, (i64)(1000 + k), hills, -1);
        if (k == 0) hills = gl_add(nodes, &n, GL_ADDISLAND, 3, hills, -1);
        if (k == 1 || sz == 1) hills = gl_add(nodes, &n, GL_SHORE, 1000, hills, -1);
    }
    int smooth1 = gl_add(nodes, &n, GL_SMOOTH, 1000, hills, -1);
    int rivermix = gl_add(nodes, &n, GL_RIVERMIX, 100, smooth1, smooth);
    int voronoi = gl_add(nodes, &n, GL_VORONOIZOOM, 10, rivermix, -1);
    gl_iwgs(nodes, rivermix, seed);   /* genlayerrivermix.initWorldGenSeed(seed) */
    gl_iwgs(nodes, voronoi, seed);    /* genlayer3.initWorldGenSeed(seed) */
    *biomeIndex = voronoi;
    return n;
}

#endif /* MC_GENLAYER_BIOMES_H */
