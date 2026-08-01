/* overworld_full_live: compose overworld_full (st_run x4 + populate) with propagated light
 * (pll_populate / pll_run pattern) and post-populate fluid CA (pfl pattern).
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY: overworld_full.h, populate_light_live.h,
 * populate_fluid_live.h. Dump full 262144 block array (%04x), seeds 12345/0/7. */
#ifndef MC_OVERWORLD_FULL_LIVE_H
#define MC_OVERWORLD_FULL_LIVE_H

#include "overworld_full.h"
#include "populate_light_live.h"
#include "populate_fluid_live.h"

MC_HD static inline void owfl_fluid_pass(World *w, i64 seed, u16 *mc_cur, u16 *mc_tmp,
                                         u16 *before_ca) {
    pfs_world_to_mc(w->blocks, mc_cur);
    {
        int i;
        for (i = 0; i < PFS_N; ++i) before_ca[i] = mc_cur[i];
    }
    ff_ca_run(mc_cur, mc_tmp, PFS_NX, PFS_NY, PFS_NZ, pfs_steps(seed));
    pfl_apply_ca_deltas(w, mc_cur, before_ca);
}

MC_HD static inline void owfl_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                  FoliageCoord *fol, i64 seed, u8 *sky, u8 *blk,
                                  u8 *tmp_sky, u8 *tmp_blk, u16 *mc_cur, u16 *mc_tmp,
                                  u16 *before_ca) {
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
                st_run(primer, sc, w->st, seed, cx, cz);
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
    owfl_fluid_pass(w, seed, mc_cur, mc_tmp, before_ca);
}

#endif /* MC_OVERWORLD_FULL_LIVE_H */
