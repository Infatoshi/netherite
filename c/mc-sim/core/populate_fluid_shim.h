/* populate_fluid_shim: compose verified populate (spring placement) + fluid_flow CA spread.
 *
 * INTERNAL verify (CPU==CUDA). After pop_run places water/lava springs (PB_FLOWING_*), convert
 * the 32x256x32 world to mc_state, run ff_ca_run N steps, dump only cells changed by the CA
 * (not conversion artifacts). populate.h + fluid_flow.h are READ-ONLY deps.
 *
 * Index = populate w_index order: (x*32+z)*256+y, x in [0,32), y in [0,256), z in [0,32).
 * Delta lines: "%06x %04x %04x" = linear_index before_ca after_ca (packed mc_state u16). */
#ifndef MC_POPULATE_FLUID_SHIM_H
#define MC_POPULATE_FLUID_SHIM_H

#include "populate.h"
typedef World PopWorld;

#include "mc.h"
#include "mc_blocks.h"

/* fluid_flow.h pulls mc_world.h which also typedefs World; provide packing helpers only. */
#ifndef MC_WORLD_H
#define MC_WORLD_H
MC_HD static inline u16 mc_state(int id, int meta) { return (u16)(((id & 0xFFF) << 4) | (meta & 0xF)); }
MC_HD static inline int mc_state_id(u16 s)   { return s >> 4; }
MC_HD static inline int mc_state_meta(u16 s) { return s & 0xF; }
#endif

#include "fluid_flow.h"

#define PFS_N W_N
#define PFS_NX W_X
#define PFS_NY W_Y
#define PFS_NZ W_Z

MC_HD static inline int pfs_steps(i64 seed) { return 16 + (int)(seed % 17); }

/* Non-blocking populate blocks (plants, etc.) become air for the CA read buffer so fluids can
 * displace them; solids and liquids map to faithful BLK ids + meta 0. */
MC_HD static inline u16 pfs_pb_to_mc(int pb) {
    if (pb == PB_AIR) return mc_state(FF_BLK_AIR, 0);
    if (pb == PB_STONE || pb == PB_GRANITE || pb == PB_DIORITE || pb == PB_ANDESITE)
        return mc_state(FF_BLK_STONE, 0);
    if (pb == PB_WATER) return mc_state(FF_BLK_WATER, 0);
    if (pb == PB_GRASS) return mc_state(BLK_GRASS, 0);
    if (pb == PB_DIRT || pb == PB_PODZOL || pb == PB_COARSE_DIRT) return mc_state(BLK_DIRT, 0);
    if (pb == PB_BEDROCK) return mc_state(BLK_BEDROCK, 0);
    if (pb == PB_GRAVEL) return mc_state(BLK_GRAVEL, 0);
    if (pb == PB_SAND) return mc_state(BLK_SAND, 0);
    if (pb == PB_SANDSTONE || pb == PB_RED_SANDSTONE) return mc_state(BLK_SANDSTONE, 0);
    if (pb == PB_ICE) return mc_state(BLK_ICE, 0);
    if (pb == PB_LAVA) return mc_state(FF_BLK_LAVA, 0);
    if (pb == PB_FLOWING_LAVA) return mc_state(FF_BLK_FLOWING_LAVA, 0);
    if (pb == PB_FLOWING_WATER) return mc_state(FF_BLK_FLOWING_WATER, 0);
    if (pb == PB_COBBLESTONE || pb == PB_MOSSY_COBBLESTONE) return mc_state(FF_BLK_COBBLESTONE, 0);
    if (pb == PB_COAL_ORE) return mc_state(BLK_COAL_ORE, 0);
    if (pb == PB_IRON_ORE) return mc_state(BLK_IRON_ORE, 0);
    if (pb == PB_GOLD_ORE) return mc_state(BLK_GOLD_ORE, 0);
    if (pb == PB_REDSTONE_ORE) return mc_state(BLK_REDSTONE_ORE, 0);
    if (pb == PB_DIAMOND_ORE) return mc_state(BLK_DIAMOND_ORE, 0);
    if (pb == PB_LAPIS_ORE) return mc_state(BLK_LAPIS_ORE, 0);
    if (pb == PB_CLAY) return mc_state(BLK_CLAY, 0);
    if (pb == PB_LOG_OAK || pb == PB_LOG_BIRCH || pb == PB_LOG_SPRUCE ||
        pb == PB_LOG_OAK_X || pb == PB_LOG_OAK_Z)
        return mc_state(BLK_LOG, 0);
    if (pb == PB_LEAVES_OAK || pb == PB_LEAVES_BIRCH || pb == PB_LEAVES_SPRUCE)
        return mc_state(BLK_LEAVES, 0);
    if (pb == PB_SNOW_LAYER) return mc_state(BLK_SNOW_LAYER, 0);
    if (pb == PB_HARDENED_CLAY || pb == PB_STAINED_CLAY || cb_is_stained_clay(pb))
        return mc_state(BLK_CLAY, 0);
    if (pb == PB_MYCELIUM) return mc_state(BLK_DIRT, 0);
    if (pb == PB_WATER_LILY) return mc_state(FF_BLK_AIR, 0);
    if (pb == PB_MOB_SPAWNER || pb == PB_CHEST) return mc_state(FF_BLK_STONE, 0);
    if (pb == PB_BONE_BLOCK) return mc_state(FF_BLK_STONE, 0);
    /* plants / flowers / reeds / vines / pumpkins: pass-through for fluid CA */
    if (pb == PB_TALLGRASS || pb == PB_FERN || pb == PB_DEADBUSH ||
        pb == PB_BROWN_MUSHROOM || pb == PB_RED_MUSHROOM || pb == PB_REEDS ||
        pb == PB_YELLOW_FLOWER)
        return mc_state(FF_BLK_AIR, 0);
    if (pb >= PB_RED_FLOWER_BASE && pb < PB_RED_FLOWER_BASE + 9) return mc_state(FF_BLK_AIR, 0);
    if (pb >= PB_DPLANT_LOWER_BASE && pb <= PB_DPLANT_UPPER) return mc_state(FF_BLK_AIR, 0);
    if (pb >= PB_PUMPKIN_BASE && pb < PB_PUMPKIN_BASE + 4) return mc_state(FF_BLK_AIR, 0);
    if (pb >= PB_VINE_BASE && pb < PB_VINE_BASE + 4) return mc_state(FF_BLK_AIR, 0);
    return mc_state(FF_BLK_STONE, 0);
}

MC_HD static inline int pfs_wi_to_fi(int wi) {
    int y = wi % W_Y;
    int t = wi / W_Y;
    int z = t % W_Z;
    int x = t / W_Z;
    return ff_idx(PFS_NX, PFS_NY, PFS_NZ, x, y, z);
}

MC_HD static inline void pfs_world_to_mc(const u16 *pb_blocks, u16 *mc_buf) {
    int wi;
    for (wi = 0; wi < PFS_N; ++wi)
        mc_buf[pfs_wi_to_fi(wi)] = pfs_pb_to_mc((int)pb_blocks[wi]);
}

MC_HD static inline void pfs_fluid_pass(PopWorld *w, CpScratch *sc, ChunkPrimer *primer,
                                        JavaRandom *r, FoliageCoord *fol, i64 seed,
                                        u16 *mc_cur, u16 *mc_tmp, u16 *before_ca) {
    pop_run(w, sc, primer, r, fol, seed);
    pfs_world_to_mc(w->blocks, mc_cur);
    {
        int i;
        for (i = 0; i < PFS_N; ++i) before_ca[i] = mc_cur[i];
    }
    ff_ca_run(mc_cur, mc_tmp, PFS_NX, PFS_NY, PFS_NZ, pfs_steps(seed));
}

MC_HD static inline void pfs_emit_deltas(const u16 *mc_cur, const u16 *before_ca,
                                         void (*emit)(int idx, u16 before, u16 after, void *ctx),
                                         void *ctx) {
    int wi;
    for (wi = 0; wi < PFS_N; ++wi) {
        int fi = pfs_wi_to_fi(wi);
        u16 b = before_ca[fi];
        u16 a = mc_cur[fi];
        if (b != a) emit(wi, b, a, ctx);
    }
}

MC_HD static inline void pfs_run(PopWorld *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u16 *mc_cur, u16 *mc_tmp,
                                 u16 *before_ca, void (*emit_delta)(int idx, u16 before, u16 after,
                                                                    void *ctx),
                                 void *emit_ctx) {
    pfs_fluid_pass(w, sc, primer, r, fol, seed, mc_cur, mc_tmp, before_ca);
    pfs_emit_deltas(mc_cur, before_ca, emit_delta, emit_ctx);
}

#endif /* MC_POPULATE_FLUID_SHIM_H */
