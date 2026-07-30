/* populate_fluid_live: populate + inline fluid_flow CA after spring placement, merged back into
 * the 2x2-chunk world and dumped as 262144 x %04x (populate w_index order).
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY deps: populate.h, fluid_flow.h, populate_fluid_shim.h.
 * Reuses pfs_fluid_pass (pop_run -> mc_state convert -> ff_ca_run N steps); applies CA deltas
 * back to PB world storage (unchanged cells keep original blocks including plants). Seeds 12345/0/7. */
#ifndef MC_POPULATE_FLUID_LIVE_H
#define MC_POPULATE_FLUID_LIVE_H

#include "populate_fluid_shim.h"

MC_HD static inline u16 pfl_mc_to_pb(u16 s) {
    int id = mc_state_id(s);
    if (id == FF_BLK_AIR) return (u16)PB_AIR;
    if (id == FF_BLK_STONE) return (u16)PB_STONE;
    if (id == BLK_GRASS) return (u16)PB_GRASS;
    if (id == BLK_DIRT) return (u16)PB_DIRT;
    if (id == BLK_BEDROCK) return (u16)PB_BEDROCK;
    if (id == BLK_GRAVEL) return (u16)PB_GRAVEL;
    if (id == BLK_SAND) return (u16)PB_SAND;
    if (id == BLK_SANDSTONE) return (u16)PB_SANDSTONE;
    if (id == BLK_ICE) return (u16)PB_ICE;
    if (id == FF_BLK_WATER) return (u16)PB_WATER;
    if (id == FF_BLK_LAVA) return (u16)PB_LAVA;
    if (id == FF_BLK_FLOWING_LAVA) return (u16)PB_FLOWING_LAVA;
    if (id == FF_BLK_FLOWING_WATER) return (u16)PB_FLOWING_WATER;
    if (id == FF_BLK_COBBLESTONE) return (u16)PB_COBBLESTONE;
    if (id == FF_BLK_OBSIDIAN) return (u16)PB_STONE;
    if (id == BLK_COAL_ORE) return (u16)PB_COAL_ORE;
    if (id == BLK_IRON_ORE) return (u16)PB_IRON_ORE;
    if (id == BLK_GOLD_ORE) return (u16)PB_GOLD_ORE;
    if (id == BLK_REDSTONE_ORE) return (u16)PB_REDSTONE_ORE;
    if (id == BLK_DIAMOND_ORE) return (u16)PB_DIAMOND_ORE;
    if (id == BLK_LAPIS_ORE) return (u16)PB_LAPIS_ORE;
    if (id == BLK_CLAY) return (u16)PB_CLAY;
    if (id == BLK_LOG) return (u16)PB_LOG_OAK;
    if (id == BLK_LEAVES) return (u16)PB_LEAVES_OAK;
    if (id == BLK_SNOW_LAYER) return (u16)PB_SNOW_LAYER;
    return (u16)PB_STONE;
}

MC_HD static inline void pfl_apply_ca_deltas(PopWorld *w, const u16 *mc_cur, const u16 *before_ca) {
    int wi;
    for (wi = 0; wi < PFS_N; ++wi) {
        int fi = pfs_wi_to_fi(wi);
        u16 b = before_ca[fi];
        u16 a = mc_cur[fi];
        if (b != a)
            w->blocks[wi] = pfl_mc_to_pb(a);
    }
}

MC_HD static inline void pfl_run(PopWorld *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u16 *mc_cur, u16 *mc_tmp,
                                 u16 *before_ca) {
    pfs_fluid_pass(w, sc, primer, r, fol, seed, mc_cur, mc_tmp, before_ca);
    pfl_apply_ca_deltas(w, mc_cur, before_ca);
}

#endif /* MC_POPULATE_FLUID_LIVE_H */
