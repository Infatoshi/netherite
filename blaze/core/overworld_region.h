/* overworld_region: generalized overworld_full_live re-compose anchored at an ARBITRARY base
 * chunk (bcx,bcz). Fills the same 2x2-chunk World (local coords [0,32)x[0,256)x[0,32)) as owfl_run,
 * but the base chunk is a PARAMETER threaded into (a) the biome voronoi window, (b) st_run's real
 * chunk coords, and (c) the per-chunk populate RNG seed - exactly as vanilla
 * ChunkProviderOverworld.populate(cx,cz) does `rand.setSeed(cx*k + cz*l ^ worldSeed)` with the REAL
 * chunk coords. At (bcx,bcz)==(0,0) this is BYTE-IDENTICAL to owfl_run (origin regression lock).
 *
 * READ-ONLY re-compose: calls the SAME verified sub-kernels (st_run, pll_wg_lakes, wg_dungeons,
 * pll_biome_decorate, owfl_fluid_pass) in the SAME order as owfl_run/pll_populate. The ONLY deltas
 * vs owfl_run are the three (bcx,bcz)-threaded lines noted below. No edits to any included header. */
#ifndef MC_OVERWORLD_REGION_H
#define MC_OVERWORLD_REGION_H

#include "mc.h"

#include "overworld_full_live.h"
#include "populate_ice_snow.h"

/* pll_populate re-compose with the per-chunk seed anchored at the REAL base chunk (bcx,bcz).
 * Verbatim copy of pll_populate() EXCEPT the setSeed formula uses (bcx,bcz) instead of (0,0). */
MC_HD static inline void owr_populate(World *w, JavaRandom *r, i64 seed, FoliageCoord *fol,
                                      PllLight *lt, int bcx, int bcz) {
    int biome = w_getBiome(w, 16, 16);   /* local (16,16) == chunk (bcx,bcz) center block */
    w_reset_loaded_chunks(w, seed, bcx, bcz);
    w_pop_sky_seed2(w, lt->pop_sky_height, lt->pop_sky_stale);
    w->activeRand = NULL;  /* cascade clobber hook DISARMED: trigger needs java global load order, see DEVLOG */
#ifndef __CUDA_ARCH__
    /* flywheel debug: the decoration biome this window will use (differ skips BIO lines) */
    if (mc_probe_fn) mc_probe_fn(bcx, bcz, "BIO", "-", (unsigned long long)biome);
#endif
    jrand_set(r, seed);
    {
        i64 k = jrand_long(r) / 2 * 2 + 1;
        i64 l = jrand_long(r) / 2 * 2 + 1;
        jrand_set(r, (i64)bcx * k + (i64)bcz * l ^ seed);   /* REAL chunk coords (vs 0*k+0*l) */
    }
    MC_PROBE("PRE", "-", r);
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        MC_PROBE("POP", "LAKE", r);
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        pll_wg_lakes(w, r, i1, j1, k1, PB_WATER, lt);
    }
    if (jrand_int_bound(r, 8) == 0) {
        MC_PROBE("POP", "LAVA", r);
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            pll_wg_lakes(w, r, i2, l2, k3, PB_LAVA, lt);
    }
    MC_PROBE("POP", "DUNGEON", r);
    {
        int j2;
        for (j2 = 0; j2 < 8; ++j2) {
            int i3 = jrand_int_bound(r, 16) + 8;
            int l3 = jrand_int_bound(r, 256);
            int l1 = jrand_int_bound(r, 16) + 8;
            wg_dungeons(w, r, i3, l3, l1);
        }
    }
    pll_biome_decorate2(w, r, biome, fol, lt, bcx * 16, bcz * 16, seed);
    w->activeRand = NULL;
    /* vanilla populate tail (after decorate + animals): per-column freeze/snow at
     * blockpos.add(8,0,8) - consumes NO rand, so it is checkpoint-neutral. */
    pis_ice_snow_pass(w, lt->blk, bcx * 16, bcz * 16);
}

/* owfl_run re-compose parametrized by base chunk (bcx,bcz). Deltas vs owfl_run:
 *   1. biome voronoi window sampled at world offset (bcx*16, bcz*16).
 *   2. st_run generates chunk (bcx+cx, bcz+cz) with REAL coords.
 *   3. owr_populate seeds the per-chunk RNG from the REAL (bcx,bcz). */
MC_HD static inline void owr_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u8 *sky, u8 *blk,
                                 u8 *tmp_sky, u8 *tmp_blk, u16 *mc_cur, u16 *mc_tmp,
                                 u16 *before_ca, int bcx, int bcz) {
    u16 pop_sky_height[W_X * W_Z];
    PllLight lt = { sky, blk, tmp_sky, tmp_blk, pop_sky_height };
    int i;

    for (i = 0; i < W_N; ++i) {
        w->blocks[i] = (u16)PB_AIR;
        sky[i] = 0;
        blk[i] = 0;
    }
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, bcx, bcz);

    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        {
            sc->arena.off = 0;   /* reset bump arena at top-level tree */
            int *fb = gl_getInts(nodes, &sc->arena, voronoi, bcx * 16, bcz * 16, W_X, W_Z);  /* delta 1 */
            int x, z;
            for (x = 0; x < W_X; ++x)
                for (z = 0; z < W_Z; ++z)
                    w->fullBiome[x * W_Z + z] = fb[x + z * W_X];  /* getInts = fb[dx + dz*width] */
        }
    }

    {
        int cx, cz;
        for (cx = 0; cx < 2; ++cx) {
            for (cz = 0; cz < 2; ++cz) {
                st_run_features(primer, sc, w->st, seed, bcx + cx, bcz + cz, ST_MAP_FEATURES);  /* delta 2 */
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

    owr_populate(w, r, seed, fol, &lt, bcx, bcz);   /* delta 3 */
    owfl_fluid_pass(w, seed, mc_cur, mc_tmp, before_ca);
}

#endif /* MC_OVERWORLD_REGION_H */
