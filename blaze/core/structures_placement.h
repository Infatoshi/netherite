/* structures_placement: chunk_provider + mineshaft/stronghold block placement on chunk (cx,cz).
 * READ-ONLY deps: chunk_provider.h, map_gen_mineshaft.h, map_gen_stronghold.h, structures.h pattern.
 * Output: vanilla 1.11.2 block ids (%04x) so bedrock (7) never collides with planks (5).
 * Pipeline mirrors ChunkProviderOverworld.provideChunk (mapFeatures mineshaft+stronghold generate)
 * then populate generateStructure for those two generators only. */
#ifndef MC_STRUCTURES_PLACEMENT_H
#define MC_STRUCTURES_PLACEMENT_H

#include "chunk_provider.h"
#include "map_gen_mineshaft.h"
#include "map_gen_stronghold.h"

/* MSGen+SHGen live in CpScratch.stgen (opaque aligned buffer; no in-kernel malloc). */
typedef char stp_stgen_fits[(sizeof(MSGen) + sizeof(SHGen) <= CP_STGEN_BYTES) ? 1 : -1];

MC_HD MC_NOINLINE static u16 stp_cb_to_vanilla(u16 cb) {
    if (cb_is_stained_clay((int)cb)) return 159;
    switch (cb) {
        case CB_AIR: return 0;
        case CB_STONE: return 1;
        case CB_WATER: return 9;
        case CB_GRASS: return 2;
        case CB_DIRT: return 3;
        case CB_BEDROCK: return 7;
        case CB_GRAVEL: return 13;
        case CB_SAND: return 12;
        case CB_SANDSTONE: return 24;
        case CB_RED_SANDSTONE: return 179;
        case CB_ICE: return 79;
        case CB_LAVA: return 11;
        case CB_FLOWING_LAVA: return 10;
        case CB_FLOWING_WATER: return 8;
        case CB_WATER_LILY: return 111;
        case CB_MYCELIUM: return 110;
        case CB_SNOW_LAYER: return 78;
        case CB_HARDENED_CLAY: return 172;
        case CB_STAINED_HARDENED_CLAY: return 159;
        case CB_PODZOL: return 3;
        case CB_COARSE_DIRT: return 3;
        default: return cb;
    }
}

MC_HD MC_NOINLINE static void stp_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                                 i64 seed, int cx, int cz) {
    cp_provide_chunk(primer, sc, st, seed, cx, cz);

    for (int i = 0; i < 65536; ++i)
        primer->data[i] = stp_cb_to_vanilla(primer->data[i]);

    MSGen *msg = (MSGen *)sc->stgen;   /* preallocated CpScratch storage (no in-kernel malloc) */
    SHGen *shg = (SHGen *)((char *)sc->stgen + sizeof(MSGen));

    ms_generate_map(msg, seed, cx, cz);
    sh_generate_map(shg, seed, cx, cz);

    MSWorld mw;
    memset(&mw, 0, sizeof(mw));
    mw.primer = primer;
    mw.chunkX = cx;
    mw.chunkZ = cz;
    mw.worldSeed = seed;
    mw.seaLevel = CB_SEA_LEVEL;

    SHWorld sw;
    sw.primer = primer;
    sw.chunkX = cx;
    sw.chunkZ = cz;
    sw.worldSeed = seed;
    sw.seaLevel = CB_SEA_LEVEL;

    ms_generate_structure(&mw, msg, cx, cz);
    sh_generate_structure(&sw, shg, cx, cz);
}

#endif
