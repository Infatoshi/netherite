/* structures: integration kernel composing verified chunk_provider + mineshaft + stronghold
 * for chunk (0,0). Same call order as ChunkProviderOverworld.provideChunk (structure generate)
 * plus populate's generateStructure (block placement). mapFeaturesEnabled=true here only.
 * Dumps full ChunkPrimer (%04x x65536). Seeds 12345/0/7. */
#ifndef MC_STRUCTURES_H
#define MC_STRUCTURES_H

#include "chunk_provider.h"
#include "map_gen_mineshaft.h"
#include "map_gen_stronghold.h"

/* MSGen+SHGen live in CpScratch.stgen (opaque aligned buffer; no in-kernel malloc). */
typedef char st_stgen_fits[(sizeof(MSGen) + sizeof(SHGen) <= CP_STGEN_BYTES) ? 1 : -1];

/* mapFeaturesEnabled for the world pipelines (per-TU host static, mc_probe_fn pattern):
 * the qrl verification saves and the playable qrl worlds run structures OFF, so the
 * replay pipeline (magma/world/populate_mc.c) clears this. Golden structure tests
 * call st_run (fixed 1). CUDA builds compile fixed 1 (structure goldens only). */
#ifndef __CUDA_ARCH__
static int st_map_features_host = 1;
#define ST_MAP_FEATURES st_map_features_host
#else
#define ST_MAP_FEATURES 1
#endif

MC_HD MC_NOINLINE static void st_run_features(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                                              i64 seed, int cx, int cz, int mapFeatures) {
    cp_provide_chunk(primer, sc, st, seed, cx, cz);
    if (!mapFeatures) return;   /* vanilla mapFeaturesEnabled=false: terrain only */

    /* Negative product modes tag canonical structure states so they cannot
     * alias PB render keys. -1 is strongholds only; -2 adds mineshafts. */
    int mineshafts = mapFeatures > 0 || mapFeatures == -2;

    /* MapGenBase.generate for mineshaft + stronghold (range-8 structure registration).
     * Preallocated CpScratch storage (no in-kernel malloc). */
    MSGen *msg = (MSGen *)sc->stgen;
    SHGen *shg = (SHGen *)((char *)sc->stgen + sizeof(MSGen));
    if (mineshafts) ms_generate_map(msg, seed, cx, cz);
    sh_generate_map(shg, seed, cx, cz);

    /* Copy CB block ids to MS/SH world view (same numeric mapping for terrain blocks). */
    MSWorld mw; memset(&mw,0,sizeof(mw)); mw.primer=primer; mw.chunkX=cx; mw.chunkZ=cz; mw.worldSeed=seed; mw.seaLevel=CB_SEA_LEVEL; mw.storeMeta=mapFeatures==-2?2:0;
    SHWorld sw; sw.primer = primer; sw.chunkX = cx; sw.chunkZ = cz; sw.worldSeed = seed; sw.seaLevel = CB_SEA_LEVEL;

    /* Remap chunk_provider ids -> vanilla ids for structure placement predicates.
     * chunk_provider uses CB_* small ints; structure code expects vanilla ids for air/stone/dirt/water.
     * For integration we remap in-place before/after structure placement. */
    for (int i = 0; i < 65536; ++i) {
        u16 v = primer->data[i];
        switch (v) {
            case CB_AIR: primer->data[i] = MS_AIR; break;
            case CB_STONE: primer->data[i] = MS_STONE; break;
            case CB_DIRT: primer->data[i] = MS_DIRT; break;
            case CB_GRASS: primer->data[i] = MS_GRASS; break; /* grass -> vanilla grass (id 2); ms_is_solid treats it as solid ground */
            case CB_WATER: primer->data[i] = MS_WATER; break;
            default: break;
        }
    }

    if (mineshafts) ms_generate_structure(&mw, msg, cx, cz);
    sh_generate_structure(&sw, shg, cx, cz);

    /* Remap back to CB_* for dump (structure blocks use vanilla ids -> map to extended ST codes). */
    for (int i = 0; i < 65536; ++i) {
        u16 v = primer->data[i];
        if (v == MS_AIR) primer->data[i] = CB_AIR;
        else if (v == MS_STONE) primer->data[i] = CB_STONE;
        else if (v == MS_GRASS) primer->data[i] = CB_GRASS; /* preserve grass round-trip (was lossily -> CB_STONE) */
        else if (v == MS_DIRT) primer->data[i] = CB_DIRT;
        else if (v == MS_WATER) primer->data[i] = CB_WATER;
        else if (v & 0x4000u) primer->data[i] = v;
        else if (v >= 20) primer->data[i] = mapFeatures < 0 ? (u16)(0x8000u | v) : v;
        /* The live stronghold-only path tags canonical structure ids so magma's
         * overlapping PB model-key namespace cannot reinterpret them. */
    }
}

MC_HD MC_NOINLINE static void st_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st, i64 seed, int cx, int cz) {
    st_run_features(primer, sc, st, seed, cx, cz, 1);
}

#endif
