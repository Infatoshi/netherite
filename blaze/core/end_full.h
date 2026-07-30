/* end_full: integration kernel composing verified chunk_provider_end + end_portal.
 * Pipeline: cpe_provide_chunk(seed, 0, 0) for End island terrain, then ep_run on the
 * synthetic obsidian portal frame slice (end_portal.h). READ-ONLY compose of
 * chunk_provider_end.h and end_portal.h - no edits to those headers.
 *
 * Portal seed is derived from the terrain seed so a single argv seed drives both paths. */
#ifndef MC_END_FULL_H
#define MC_END_FULL_H

#include "chunk_provider_end.h"
#include "end_portal.h"

#define EF_CHUNK_N 65536

MC_HD MC_NOINLINE static u64 ef_portal_seed(i64 terrain_seed) {
    return mc_hash_seed((u64)terrain_seed, 0, 0, 0, 0, 0xEF00u);
}

MC_HD MC_NOINLINE static void ef_run(CpePrimer *primer, CpeScratch *sc, EpWorld *ep, i64 terrain_seed) {
    cpe_provide_chunk(primer, sc, terrain_seed, 0, 0);
    ep->seed = ef_portal_seed(terrain_seed);
    ep_run(ep);
}

#endif /* MC_END_FULL_H */
