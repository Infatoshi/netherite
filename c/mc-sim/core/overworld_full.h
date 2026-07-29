/* overworld_full: integration kernel composing verified chunk_provider + structures +
 * populate for the 2x2-chunk region around chunk (0,0). Same call order as vanilla
 * ChunkProviderOverworld: provideChunk (with mapFeaturesEnabled=true) for each of
 * (0,0),(1,0),(0,1),(1,1), then populate(0,0). READ-ONLY compose of chunk_provider.h,
 * structures.h, populate.h - no edits to those headers.
 *
 * Pipeline per chunk: st_run (= cp_provide_chunk + mineshaft + stronghold structure gen).
 * Then pop_populate on the assembled World (262144 block dump, seeds 12345/0/7). */
#ifndef MC_OVERWORLD_FULL_H
#define MC_OVERWORLD_FULL_H

#include "populate.h"
#include "structures.h"

MC_HD MC_NOINLINE static void owf_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                  FoliageCoord *fol, i64 seed) {
    for (int i = 0; i < W_N; ++i) w->blocks[i] = (u16)PB_AIR;
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);

    /* precompute full-res voronoi biome over [0,32)^2 (idx = x*32 + z). */
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

    /* provide chunks (0,0),(1,0),(0,1),(1,1) with structures into the world. */
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

    pop_populate(w, r, seed, fol);
}

#endif
