/* nether_full: integration kernel composing chunk_provider_nether terrain + nether fortress
 * placement (structures_placement pattern). READ-ONLY on chunk_provider_nether.h,
 * map_gen_fortress.h, structures_placement.h. Output: vanilla 1.11.2 block ids (%04x).
 * Pipeline: prepareHeights + buildSurfaces + MapGenCavesHell -> nf_to_vanilla remap ->
 * MapGenNetherBridge generateMap + generateStructure for chunk (cx,cz). */
#ifndef MC_NETHER_FULL_H
#define MC_NETHER_FULL_H

#include <stdlib.h>
#include <string.h>
#include "chunk_provider_nether.h"
#include "map_gen_fortress.h"

MC_HD MC_NOINLINE static u16 nf_to_vanilla(u16 id) {
    switch (id) {
        case CPN_AIR: return 0;
        case CPN_STONE: return 1;
        case CPN_GRASS: return 2;
        case CPN_DIRT: return 3;
        case CPN_BEDROCK: return 7;
        /* Block.java 1.11.2: 10=flowing_lava, 11=lava. ChunkProviderHell's
         * terrain constant is Blocks.LAVA (still); only populate springs use
         * Blocks.FLOWING_LAVA. */
        case CPN_LAVA: return 11;
        case CPN_FLOWING_LAVA: return 10;
        case CPN_GRAVEL: return 13;
        case CPN_NETHERRACK: return 87;
        case CPN_SOUL_SAND: return 88;
        default: return id;
    }
}

MC_HD MC_NOINLINE static void nf_run(CpnPrimer *primer, CpnHellScratch *sc, const McSinTable *st,
                                 CpnHellNoise *noise, i64 seed, int cx, int cz) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)CPN_AIR;

    JavaRandom rand;
    jrand_set(&rand, (i64)cx * 341873128712LL + (i64)cz * 132897987541LL);

    cpn_prepare_heights(sc, noise, cx, cz, primer);
    cpn_build_surfaces(sc, noise, &rand, cx, cz, primer);

    CpnHellCaveCtx cctx;
    cctx.primer = primer;
    cctx.st = st;
    cpn_hell_cave_generate(&cctx, seed, cx, cz);

    for (int i = 0; i < 65536; ++i)
        primer->data[i] = nf_to_vanilla(primer->data[i]);

    ChunkPrimer *fp = (ChunkPrimer *)primer;
    FtGen *g = &sc->ftgen;   /* preallocated (no in-kernel malloc) */
    FtWorld w;
    w.primer = fp;
    w.chunkX = cx;
    w.chunkZ = cz;
    w.worldSeed = seed;
    memset(g, 0, sizeof(*g));
    ft_generate_map(g, seed, cx, cz);
    ft_generate_structure(&w, g, cx, cz);
}

#endif
