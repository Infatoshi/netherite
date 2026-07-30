/* superflat_populate: integration kernel composing verified chunk_provider_flat +
 * populate for the 2x2-chunk flat preset region around chunk (0,0). Same call order as vanilla
 * ChunkProviderFlat: provideChunk (structures excluded) for each of (0,0),(1,0),(0,1),(1,1),
 * then populate(0,0). READ-ONLY compose of chunk_provider_flat.h, populate.h.
 *
 * Pipeline per chunk: cpf_provide_chunk (default flat preset when settings is null).
 * Then pop_populate on the assembled World (262144 block dump, seeds 12345/0/7). */
#ifndef MC_SUPERFLAT_POPULATE_H
#define MC_SUPERFLAT_POPULATE_H

#include "chunk_provider_flat.h"
#include "populate.h"

MC_HD static inline void sfp_run(World *w, CpfPrimer *primer, JavaRandom *r,
                                  FoliageCoord *fol, i64 seed, const char *settings,
                                  GlArena *arena) {
    for (int i = 0; i < W_N; ++i) w->blocks[i] = (u16)PB_AIR;
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);

    /* precompute full-res voronoi biome over [0,32)^2 (idx = x*32 + z). */
    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        arena->off = 0;   /* reset bump arena at top-level tree */
        int *fb = gl_getInts(nodes, arena, voronoi, 0, 0, W_X, W_Z);
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z)
                w->fullBiome[x * W_Z + z] = fb[z + x * W_Z];
    }

    /* provide flat chunks (0,0),(1,0),(0,1),(1,1) into the world. */
    for (int cx = 0; cx < 2; ++cx) {
        for (int cz = 0; cz < 2; ++cz) {
            cpf_provide_chunk(primer, settings);
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 256; ++y)
                        w_set(w, cx * 16 + lx, y, cz * 16 + lz,
                              (int)primer->data[cpf_index(lx, y, lz)]);
        }
    }

    pop_populate(w, r, seed, fol);
}

#endif
