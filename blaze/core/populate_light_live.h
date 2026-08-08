/* populate_light_live: full pop_run (2x2-chunk world) with propagated skylight+block-light
 * fixpoint CA instead of populate.h w_light stub.
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY: populate.h, light_propagation.h, populate_light_shim.h.
 *
 * Scene: chunk_provider + populate with pls_propagate before every w_light query (lake grass
 * conversion, mushroom canBlockStay). Dump full 262144 block array (%04x), one per line. */
#ifndef MC_POPULATE_LIGHT_LIVE_H
#define MC_POPULATE_LIGHT_LIVE_H

#include "populate_light_shim.h"

typedef struct {
    u8 *sky;
    u8 *blk;
    u8 *tmp_sky;
    u8 *tmp_blk;
    u16 *pop_sky_height;
    u8 *pop_sky_stale;   /* optional [W_N]: vanilla STALE populate skylight (magma replay) */
} PllLight;

/* WorldGenLakes with propagated sky light for dirt->grass (replaces w_light stub). */
MC_HD static inline int pll_wg_lakes(World *w, JavaRandom *r, int posX, int posY, int posZ,
                                     int liquid, PllLight *lt) {
    int px = posX - 8, py = posY, pz = posZ - 8;
    for (; py > 5 && w_isAir(w, px, py, pz); --py) ;
    if (py <= 4) return 0;
    py -= 4;
    char ab[2048];
    for (int t = 0; t < 2048; ++t) ab[t] = 0;
    int i = jrand_int_bound(r, 4) + 4;
    for (int jj = 0; jj < i; ++jj) {
        double d0 = jrand_double(r) * 6.0 + 3.0;
        double d1 = jrand_double(r) * 4.0 + 2.0;
        double d2 = jrand_double(r) * 6.0 + 3.0;
        double d3 = jrand_double(r) * (16.0 - d0 - 2.0) + 1.0 + d0 / 2.0;
        double d4 = jrand_double(r) * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
        double d5 = jrand_double(r) * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;
        for (int l = 1; l < 15; ++l)
            for (int i1 = 1; i1 < 15; ++i1)
                for (int j1 = 1; j1 < 7; ++j1) {
                    double d6 = ((double)l - d3) / (d0 / 2.0);
                    double d7 = ((double)j1 - d4) / (d1 / 2.0);
                    double d8 = ((double)i1 - d5) / (d2 / 2.0);
                    if (d6 * d6 + d7 * d7 + d8 * d8 < 1.0) ab[(l * 16 + i1) * 8 + j1] = 1;
                }
    }
    for (int k1 = 0; k1 < 16; ++k1)
        for (int l2 = 0; l2 < 16; ++l2)
            for (int kk = 0; kk < 8; ++kk) {
                int flag = !ab[(k1 * 16 + l2) * 8 + kk] &&
                    (k1 < 15 && ab[((k1 + 1) * 16 + l2) * 8 + kk] ||
                     k1 > 0 && ab[((k1 - 1) * 16 + l2) * 8 + kk] ||
                     l2 < 15 && ab[(k1 * 16 + l2 + 1) * 8 + kk] ||
                     l2 > 0 && ab[(k1 * 16 + (l2 - 1)) * 8 + kk] ||
                     kk < 7 && ab[(k1 * 16 + l2) * 8 + kk + 1] ||
                     kk > 0 && ab[(k1 * 16 + l2) * 8 + (kk - 1)]);
                if (flag) {
                    int state = w_get(w, px + k1, py + kk, pz + l2);
                    if ((kk >= 4 && pb_isLiquid(state)) ||
                        (kk < 4 && !lake_isSolidW(state) && state != liquid))
                        return 0;
                }
            }
    for (int l1 = 0; l1 < 16; ++l1)
        for (int i3 = 0; i3 < 16; ++i3)
            for (int i4 = 0; i4 < 8; ++i4)
                if (ab[(l1 * 16 + i3) * 8 + i4])
                    w_set(w, px + l1, py + i4, pz + i3, i4 >= 4 ? PB_AIR : liquid);
    for (int i2 = 0; i2 < 16; ++i2)
        for (int j3 = 0; j3 < 16; ++j3)
            for (int j4 = 4; j4 < 8; ++j4)
                if (ab[(i2 * 16 + j3) * 8 + j4]) {
                    int bx = px + i2, by = py + (j4 - 1), bz = pz + j3;
                    if (w_get(w, bx, by, bz) == PB_DIRT &&
                        w_pop_sky_light(w, px + i2, py + j4, pz + j3) > 0)
                        w_set(w, bx, by, bz, PB_GRASS);
                }
    if (liquid == PB_LAVA || liquid == PB_FLOWING_LAVA) {
        for (int j2 = 0; j2 < 16; ++j2)
            for (int k3 = 0; k3 < 16; ++k3)
                for (int k4 = 0; k4 < 8; ++k4) {
                    int flag1 = !ab[(j2 * 16 + k3) * 8 + k4] &&
                        (j2 < 15 && ab[((j2 + 1) * 16 + k3) * 8 + k4] ||
                         j2 > 0 && ab[((j2 - 1) * 16 + k3) * 8 + k4] ||
                         k3 < 15 && ab[(j2 * 16 + k3 + 1) * 8 + k4] ||
                         k3 > 0 && ab[(j2 * 16 + (k3 - 1)) * 8 + k4] ||
                         k4 < 7 && ab[(j2 * 16 + k3) * 8 + k4 + 1] ||
                         k4 > 0 && ab[(j2 * 16 + k3) * 8 + (k4 - 1)]);
                    if (flag1 && (k4 < 4 || jrand_int_bound(r, 2) != 0) &&
                        lake_isSolidW(w_get(w, px + j2, py + k4, pz + k3)))
                        w_set(w, px + j2, py + k4, pz + k3, PB_STONE);
                }
    }
    return 1;
}

/* Full vanilla Biome.decorate dispatch (per-biome cfg + pre/post passes, SHROOM in place).
 * (bx0,bz0) = world block coords of window local (0,0) for the noise-driven plains gates. */
MC_HD static inline void pll_biome_decorate2(World *w, JavaRandom *r, int biome,
                                             FoliageCoord *fol, PllLight *lt,
                                             int bx0, int bz0, i64 seed) {
    pls_biome_decorate_full(w, r, biome, fol, bx0, bz0,
                            seed,
                            lt->sky, lt->blk, lt->tmp_sky, lt->tmp_blk);
}

MC_HD static inline void pll_biome_decorate(World *w, JavaRandom *r, int biome,
                                            FoliageCoord *fol, PllLight *lt, i64 seed) {
    pll_biome_decorate2(w, r, biome, fol, lt, 0, 0, seed);
}

MC_HD static inline void pll_populate(World *w, JavaRandom *r, i64 seed, FoliageCoord *fol,
                                      PllLight *lt) {
    int biome = w_getBiome(w, 16, 16);
    w_reset_loaded_chunks(w, seed, 0, 0);
    w_pop_sky_seed(w, lt->pop_sky_height);
    w->activeRand = NULL;  /* cascade clobber hook DISARMED: trigger needs java global load order, see DEVLOG */
    jrand_set(r, seed);
    {
        i64 k = jrand_long(r) / 2 * 2 + 1;
        i64 l = jrand_long(r) / 2 * 2 + 1;
        (void)k;
        (void)l;
        jrand_set(r, (i64)0 * k + (i64)0 * l ^ seed);
    }
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        pll_wg_lakes(w, r, i1, j1, k1, PB_WATER, lt);
    }
    if (jrand_int_bound(r, 8) == 0) {
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            pll_wg_lakes(w, r, i2, l2, k3, PB_LAVA, lt);
    }
    {
        int j2;
        for (j2 = 0; j2 < 8; ++j2) {
            int i3 = jrand_int_bound(r, 16) + 8;
            int l3 = jrand_int_bound(r, 256);
            int l1 = jrand_int_bound(r, 16) + 8;
            wg_dungeons(w, r, i3, l3, l1);
        }
    }
    pll_biome_decorate(w, r, biome, fol, lt, seed);
    w->activeRand = NULL;
}

MC_HD static inline void pll_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u8 *sky, u8 *blk,
                                 u8 *tmp_sky, u8 *tmp_blk) {
    u16 pop_sky_height[W_X * W_Z];
    PllLight lt = { sky, blk, tmp_sky, tmp_blk, pop_sky_height };
    int i;

    for (i = 0; i < W_N; ++i) {
        w->blocks[i] = (u16)PB_AIR;
        sky[i] = 0;
        blk[i] = 0;
    }
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);

    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        {
            sc->arena.off = 0;   /* reset bump arena at top-level tree */
            int *fb = gl_getInts(nodes, &sc->arena, voronoi, 0, 0, W_X, W_Z);
            int x, z;
            for (x = 0; x < W_X; ++x)
                for (z = 0; z < W_Z; ++z)
                    w->fullBiome[x * W_Z + z] = fb[z + x * W_Z];
        }
    }

    {
        int cx, cz;
        for (cx = 0; cx < 2; ++cx) {
            for (cz = 0; cz < 2; ++cz) {
                cp_provide_chunk(primer, sc, w->st, seed, cx, cz);
                {
                    int lx, lz, y;
                    for (lx = 0; lx < 16; ++lx)
                        for (lz = 0; lz < 16; ++lz)
                            for (y = 0; y < 256; ++y)
                                w_set(w, cx * 16 + lx, y, cz * 16 + lz,
                                      cb_get(primer, lx, y, lz));
                }
            }
        }
    }

    pll_populate(w, r, seed, fol, &lt);
}

#endif /* MC_POPULATE_LIGHT_LIVE_H */
