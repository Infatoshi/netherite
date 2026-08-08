/* populate_dungeon_golden: WorldGenDungeons on real overworld terrain (2x2 chunk region).
 * Builds the world via owf_run terrain path (chunk_provider + structures, READ-ONLY
 * overworld_full.h), snapshots pre-populate blocks, runs the populate dungeon loop
 * (lakes consume RNG first, identical to pop_populate), dumps dungeon delta.
 *
 * Delta line format: %06x%04x (world index, PB_* block id) for each block that changed
 * and is a dungeon room block (cobblestone, mossy cobblestone, chest, mob spawner).
 * Empty dungeon output (seeds with no successful placement): dump full 262144 %04x lines
 * (same as populate/overworld_full harness).
 *
 * READ-ONLY compose: populate.h, overworld_full.h. */
#ifndef MC_POPULATE_DUNGEON_GOLDEN_H
#define MC_POPULATE_DUNGEON_GOLDEN_H

#include "overworld_full.h"

MC_HD MC_NOINLINE static int pdg_is_dungeon_block(u16 b) {
    return b == (u16)PB_COBBLESTONE || b == (u16)PB_MOSSY_COBBLESTONE ||
           b == (u16)PB_CHEST || b == (u16)PB_MOB_SPAWNER;
}

/* Build overworld terrain (structures included) without pop_populate. */
MC_HD MC_NOINLINE static void pdg_build_terrain(World *w, CpScratch *sc, ChunkPrimer *primer,
                                           i64 seed) {
    for (int i = 0; i < W_N; ++i) w->blocks[i] = (u16)PB_AIR;
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);

    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        sc->arena.off = 0;   /* reset bump arena at top-level tree */
        int *fb = gl_getInts(nodes, &sc->arena, voronoi, 0, 0, W_X, W_Z);
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z)
                w->fullBiome[x * W_Z + z] = fb[z + x * W_Z];
    }

    for (int cx = 0; cx < 2; ++cx) {
        for (int cz = 0; cz < 2; ++cz) {
            st_run(primer, sc, w->st, seed, cx, cz);
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 256; ++y)
                        w_set(w, cx * 16 + lx, y, cz * 16 + lz,
                              cb_get(primer, lx, y, lz));
        }
    }
}

/* Verbatim populate(0,0) lake + dungeon RNG (lakes may mutate terrain before dungeons). */
MC_HD MC_NOINLINE static void pdg_run_dungeons(World *w, JavaRandom *r, i64 seed) {
    int biome = w_getBiome(w, 16, 16);
    jrand_set(r, seed);
    i64 k = jrand_long(r) / 2 * 2 + 1;
    i64 l = jrand_long(r) / 2 * 2 + 1;
    (void)k;
    (void)l;
    jrand_set(r, (i64)0 * k + (i64)0 * l ^ seed);
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        wg_lakes(w, r, i1, j1, k1, PB_WATER);
    }
    if (jrand_int_bound(r, 8) == 0) {
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            wg_lakes(w, r, i2, l2, k3, PB_LAVA);
    }
    for (int j2 = 0; j2 < 8; ++j2) {
        int i3 = jrand_int_bound(r, 16) + 8;
        int l3 = jrand_int_bound(r, 256);
        int l1 = jrand_int_bound(r, 16) + 8;
        wg_dungeons(w, r, i3, l3, l1);
    }
}

typedef struct {
    int n;
    int *idx;
    u16 *blk;
} PdgOutBuf;

typedef void (*PdgEmitFn)(int idx, u16 block, void *ctx);

MC_HD MC_NOINLINE static void pdg_emit_buf(int idx, u16 block, void *ctx) {
    PdgOutBuf *b = (PdgOutBuf *)ctx;
    if (b->n < W_N) {
        b->idx[b->n] = idx;
        b->blk[b->n] = block;
        b->n++;
    }
}

MC_HD MC_NOINLINE static void pdg_run(i64 seed, PdgEmitFn emit, void *ctx,
                                 McSinTable *st, u16 *before, u16 *after,
                                 CpScratch *sc, ChunkPrimer *primer, JavaRandom *r) {
    World w;
    int i, n_delta = 0;

    w.st = st;
    w.blocks = before;
    pdg_build_terrain(&w, sc, primer, seed);

    for (i = 0; i < W_N; ++i) after[i] = before[i];
    w.blocks = after;
    pdg_run_dungeons(&w, r, seed);

    for (i = 0; i < W_N; ++i) {
        if (before[i] != after[i] && pdg_is_dungeon_block(after[i]))
            n_delta++;
    }

    if (n_delta == 0) {
        for (i = 0; i < W_N; ++i)
            emit(i, after[i], ctx);
    } else {
        for (i = 0; i < W_N; ++i) {
            if (before[i] != after[i] && pdg_is_dungeon_block(after[i]))
                emit(i, after[i], ctx);
        }
    }
}

#endif /* MC_POPULATE_DUNGEON_GOLDEN_H */
