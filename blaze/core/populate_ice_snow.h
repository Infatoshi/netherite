/* populate_ice_snow: pll_run (propagated light) + ChunkProviderOverworld ICE populate pass.
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY: populate_light_live.h, biome_props_full.h, populate.h.
 *
 * Scene: pop_run with pls_propagate light, then 16x16 freeze/snow over chunk (0,0) local
 * [8,24)^2 (blockpos.add(8,0,8) loop). canBlockFreezeWater/canSnowAt use mc_bpf_temperature +
 * propagated EnumSkyBlock.BLOCK light (not w_light stub). Dump full 262144 block array (%04x).
 * Seeds 12345/0/7. */
#ifndef MC_POPULATE_ICE_SNOW_H
#define MC_POPULATE_ICE_SNOW_H

#include "populate_light_live.h"
#include "biome_props_full.h"

#define PIS_OX 8
#define PIS_OZ 8
#define PIS_W  16

MC_HD MC_NOINLINE static int pis_block_light(const u8 *blk, int x, int y, int z) {
    if (!w_inb(x, y, z)) return 0;
    return (int)blk[w_index(x, y, z)];
}

/* Biome.TEMPERATURE_NOISE = new NoiseGeneratorPerlin(new Random(1234L), 1) (static init). */
MC_HD MC_NOINLINE static void pis_temperature_noise_init(CpPerlin *tn) {
    JavaRandom r; jrand_set(&r, 1234LL);
    tn->n = 1;
    cp_simplex_init(&tn->levels[0], &r);
}

/* Biome.getFloatTemperature(pos): altitude-adjusted above y=64 via TEMPERATURE_NOISE at
 * WORLD coords /8 (float division, promoted to double like the JVM call site).
 * (wx0,wz0) = world block coords of window local (0,0). */
MC_HD MC_NOINLINE static float pis_float_temperature(const World *w, int wx0, int wz0,
                                                     int x, int y, int z) {
    float temp = mc_bpf_temperature(w_getBiome(w, x, z));
    if (y > 64) {
        CpPerlin tn;
        pis_temperature_noise_init(&tn);
        float f = (float)(cp_perlin_getValue(&tn, (double)((float)(wx0 + x) / 8.0f),
                                             (double)((float)(wz0 + z) / 8.0f)) * 4.0);
        return temp - (f + (float)y - 64.0f) * 0.05f / 30.0f;
    }
    return temp;
}

/* World.canBlockFreezeBody(pos, false): cold biome, SOURCE water (LEVEL==0), block light < 10. */
MC_HD MC_NOINLINE static int pis_can_block_freeze_water(const World *w, const u8 *blk,
                                                    int wx0, int wz0, int x, int y, int z) {
    if (y < 0 || y >= 256) return 0;
    if (pis_float_temperature(w, wx0, wz0, x, y, z) >= 0.15f) return 0;
    if (pis_block_light(blk, x, y, z) >= 10) return 0;
    return w_get(w, x, y, z) == PB_WATER;
}

/* BlockSnow.canPlaceBlockAt (PB subset; no packed ice / barrier; a single snow layer is
 * NOT full-height 8, so below==SNOW_LAYER falls through to the opaque check -> false). */
MC_HD MC_NOINLINE static int pis_snow_can_place(const World *w, int x, int y, int z) {
    if (y < 1) return 0;
    {
        int below = w_get(w, x, y - 1, z);
        if (below == PB_ICE) return 0;
        if (pb_isLeaves(below)) return 1;
        if (pb_blocksMovement(below) && pb_opacity(below) >= 15) return 1;
    }
    return 0;
}

/* World.canSnowAtBody(pos, checkLight=true). */
MC_HD MC_NOINLINE static int pis_can_snow_at(const World *w, const u8 *blk,
                                             int wx0, int wz0, int x, int y, int z) {
    if (pis_float_temperature(w, wx0, wz0, x, y, z) >= 0.15f) return 0;
    if (y < 0 || y >= 256) return 0;
    if (pis_block_light(blk, x, y, z) >= 10) return 0;
    if (!w_isAir(w, x, y, z)) return 0;
    return pis_snow_can_place(w, x, y, z);
}

/* ChunkProviderOverworld populate ICE event (no RNG). */
MC_HD MC_NOINLINE static void pis_ice_snow_pass(World *w, const u8 *blk, int wx0, int wz0) {
    int k2, j3;
    for (k2 = 0; k2 < PIS_W; ++k2) {
        for (j3 = 0; j3 < PIS_W; ++j3) {
            int wx = PIS_OX + k2;
            int wz = PIS_OZ + j3;
            int py = w_precip(w, wx, wz);
            int fy = py - 1;
            if (pis_can_block_freeze_water(w, blk, wx0, wz0, wx, fy, wz))
                w_set(w, wx, fy, wz, PB_ICE);
            if (pis_can_snow_at(w, blk, wx0, wz0, wx, py, wz))
                w_set(w, wx, py, wz, PB_SNOW_LAYER);
        }
    }
}

MC_HD MC_NOINLINE static void pis_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u8 *sky, u8 *blk,
                                 u8 *tmp_sky, u8 *tmp_blk) {
    pll_run(w, sc, primer, r, fol, seed, sky, blk, tmp_sky, tmp_blk);
    pis_ice_snow_pass(w, blk, 0, 0);
}

#endif /* MC_POPULATE_ICE_SNOW_H */
