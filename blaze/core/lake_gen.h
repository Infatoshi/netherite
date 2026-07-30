/* lake_gen: exact C port of MC 1.11.2 WorldGenLakes.generate
 * (net/minecraft/world/gen/feature/WorldGenLakes.java + WorldGenerator base). Carves a lake: a
 * union of random ellipsoids inside a 16(x) x 8(y) x 16(z) neighborhood -> liquid (lower half) +
 * air pocket (upper half), then a dirt->grass rim conversion, then (water only) a freeze pass.
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. Build C with -ffp-contract=off / CUDA with --fmad=false.
 *
 * DEPENDENCY DECOUPLING (identical in this header and oracle/goldens/lake_gen/Golden.java):
 *  - Synthetic world: a mutable cube (edge `dim`), packed block-states (mc_state, id<<4|meta, meta
 *    always 0 here). Deterministic fill: STONE for y<=12, DIRT for 13<=y<=16, AIR for y>=17 -> a
 *    flat solid surface at y=16 with material to carve and air above (same idea as ore_gen's stone
 *    cube / caves' all-stone primer). generate() READS the cube mid-pass (the fill writes are seen
 *    by the later dirt->grass pass), so it MUST run single-threaded (one env / one thread).
 *  - Block-material classification WorldGenLakes uses (Material.isLiquid/.isSolid, Blocks.* ids) ->
 *    integer block ids + a tiny classifier (sanctioned substitution). Material facts (from
 *    block/material/Material.java + MaterialLiquid + MaterialTransparent): AIR isSolid=0 isLiquid=0;
 *    STONE/DIRT/GRASS/ICE isSolid=1 isLiquid=0; WATER/LAVA isSolid=0 isLiquid=1. So here
 *    lk_is_solid = "not air and not liquid", lk_is_liquid = water/lava.
 *  - Liquid fixed to WATER (the common `new WorldGenLakes(Blocks.WATER)` ctor). The lava branch is
 *    kept verbatim but never executes (block material is WATER), drawing no RNG.
 *  - Biome fixed to PLAINS: getBiome().topBlock == GRASS (not MYCELIUM); getLightFor(SKY,...) = 15
 *    (synthetic full skylight) so the dirt->grass rim fires; canBlockFreezeWater == false (Plains
 *    temp 0.8 > 0.15 -> no ice). All documented, deterministic.
 *
 * C-vs-Java traps: WorldGenLakes draws rand per blob as six separate `nextDouble()*..` STATEMENTS
 * (one RNG call each, so already sequenced) preceded by one nextInt(4); jrand_double/jrand_int_bound
 * are the verified ordered helpers. boolean[2048] index = (x*16 + z)*8 + y. Guard all cube writes
 * against OOB; OOB reads = AIR (vanilla unloaded/out-of-height behavior). */
#ifndef MC_LAKE_GEN_H
#define MC_LAKE_GEN_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "mc_world.h"

/* Mycelium (vanilla numeric block id 110) is not in the trunk KEEP enum; the Plains topBlock branch
 * never selects it here, but the constant is needed verbatim. Defined locally (no trunk edit). */
#ifndef BLK_MYCELIUM
#define BLK_MYCELIUM 110
#endif

/* cube index order matches the drivers' print order: (y*dim + z)*dim + x. */
MC_HD static inline int lk_idx(int dim, int x, int y, int z) { return (y * dim + z) * dim + x; }

MC_HD MC_NOINLINE static int lk_in_bounds(int dim, int x, int y, int z) {
    return x >= 0 && x < dim && y >= 0 && y < dim && z >= 0 && z < dim;
}
/* World.getBlockState: OOB (outside the loaded cube / world height) reads as AIR in vanilla. */
MC_HD MC_NOINLINE static u16 lk_get(const u16 *cube, int dim, int x, int y, int z) {
    return lk_in_bounds(dim, x, y, z) ? cube[lk_idx(dim, x, y, z)] : mc_state(BLK_AIR, 0);
}
MC_HD MC_NOINLINE static void lk_set(u16 *cube, int dim, int x, int y, int z, u16 s) {
    if (lk_in_bounds(dim, x, y, z)) cube[lk_idx(dim, x, y, z)] = s;
}
/* World.isAirBlock. */
MC_HD MC_NOINLINE static int lk_is_air(const u16 *cube, int dim, int x, int y, int z) {
    return mc_state_id(lk_get(cube, dim, x, y, z)) == BLK_AIR;
}

/* Material classifier (sanctioned substitution; see header doc). */
MC_HD MC_NOINLINE static int lk_is_liquid(u16 s) {
    int id = mc_state_id(s);
    return id == BLK_WATER || id == BLK_FLOWING_WATER || id == BLK_LAVA || id == BLK_FLOWING_LAVA;
}
MC_HD MC_NOINLINE static int lk_is_solid(u16 s) {
    if (mc_state_id(s) == BLK_AIR) return 0;
    if (lk_is_liquid(s)) return 0;
    return 1;
}
MC_HD MC_NOINLINE static int lk_block_material_is_lava(int blockId) {
    return blockId == BLK_LAVA || blockId == BLK_FLOWING_LAVA;
}
MC_HD MC_NOINLINE static int lk_block_material_is_water(int blockId) {
    return blockId == BLK_WATER || blockId == BLK_FLOWING_WATER;
}

/* getLightFor(EnumSkyBlock.SKY, ...): synthetic fixed full skylight (Plains, open surface). */
MC_HD static inline int lk_sky_light(int x, int y, int z) { (void)x; (void)y; (void)z; return 15; }
/* getBiome().topBlock for fixed PLAINS = GRASS (not MYCELIUM). */
MC_HD static inline int lk_biome_top_block(int x, int y, int z) { (void)x; (void)y; (void)z; return BLK_GRASS; }
/* canBlockFreezeWater for fixed PLAINS (temp 0.8 > 0.15): never freezes. */
MC_HD static inline int lk_can_block_freeze_water(int x, int y, int z) { (void)x; (void)y; (void)z; return 0; }

/* WorldGenLakes.generate(world, rand, position) over the synthetic cube. position = (posX,posY,posZ).
 * liquidState = this.block.getDefaultState(); liquidBlockId = block id of this.block. Returns 1 if a
 * lake was placed, 0 if generate() aborts (no-op still validates the RNG/predicate path bitwise). */
MC_HD MC_NOINLINE static int mc_lake_gen(u16 *cube, int dim, JavaRandom *rand,
                                    int posX, int posY, int posZ,
                                    u16 liquidState, int liquidBlockId) {
    int px = posX - 8, py = posY, pz = posZ - 8;   /* position = position.add(-8, 0, -8) */

    for (; py > 5 && lk_is_air(cube, dim, px, py, pz); --py) {
        ;
    }

    if (py <= 4) {
        return 0;
    } else {
        py -= 4;                                    /* position = position.down(4) */
        char aboolean[2048];
        for (int t = 0; t < 2048; ++t) aboolean[t] = 0;
        int i = jrand_int_bound(rand, 4) + 4;

        for (int j = 0; j < i; ++j) {
            double d0 = jrand_double(rand) * 6.0 + 3.0;
            double d1 = jrand_double(rand) * 4.0 + 2.0;
            double d2 = jrand_double(rand) * 6.0 + 3.0;
            double d3 = jrand_double(rand) * (16.0 - d0 - 2.0) + 1.0 + d0 / 2.0;
            double d4 = jrand_double(rand) * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
            double d5 = jrand_double(rand) * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;

            for (int l = 1; l < 15; ++l) {
                for (int i1 = 1; i1 < 15; ++i1) {
                    for (int j1 = 1; j1 < 7; ++j1) {
                        double d6 = ((double)l - d3) / (d0 / 2.0);
                        double d7 = ((double)j1 - d4) / (d1 / 2.0);
                        double d8 = ((double)i1 - d5) / (d2 / 2.0);
                        double d9 = d6 * d6 + d7 * d7 + d8 * d8;

                        if (d9 < 1.0) {
                            aboolean[(l * 16 + i1) * 8 + j1] = 1;
                        }
                    }
                }
            }
        }

        for (int k1 = 0; k1 < 16; ++k1) {
            for (int l2 = 0; l2 < 16; ++l2) {
                for (int k = 0; k < 8; ++k) {
                    int flag = !aboolean[(k1 * 16 + l2) * 8 + k] &&
                               (k1 < 15 && aboolean[((k1 + 1) * 16 + l2) * 8 + k] ||
                                k1 > 0 && aboolean[((k1 - 1) * 16 + l2) * 8 + k] ||
                                l2 < 15 && aboolean[(k1 * 16 + l2 + 1) * 8 + k] ||
                                l2 > 0 && aboolean[(k1 * 16 + (l2 - 1)) * 8 + k] ||
                                k < 7 && aboolean[(k1 * 16 + l2) * 8 + k + 1] ||
                                k > 0 && aboolean[(k1 * 16 + l2) * 8 + (k - 1)]);

                    if (flag) {
                        u16 state = lk_get(cube, dim, px + k1, py + k, pz + l2);

                        if (k >= 4 && lk_is_liquid(state)) {
                            return 0;
                        }

                        if (k < 4 && !lk_is_solid(state) && mc_state_id(state) != liquidBlockId) {
                            return 0;
                        }
                    }
                }
            }
        }

        for (int l1 = 0; l1 < 16; ++l1) {
            for (int i3 = 0; i3 < 16; ++i3) {
                for (int i4 = 0; i4 < 8; ++i4) {
                    if (aboolean[(l1 * 16 + i3) * 8 + i4]) {
                        lk_set(cube, dim, px + l1, py + i4, pz + i3,
                               i4 >= 4 ? mc_state(BLK_AIR, 0) : liquidState);
                    }
                }
            }
        }

        for (int i2 = 0; i2 < 16; ++i2) {
            for (int j3 = 0; j3 < 16; ++j3) {
                for (int j4 = 4; j4 < 8; ++j4) {
                    if (aboolean[(i2 * 16 + j3) * 8 + j4]) {
                        int bx = px + i2, by = py + (j4 - 1), bz = pz + j3;

                        if (mc_state_id(lk_get(cube, dim, bx, by, bz)) == BLK_DIRT &&
                            lk_sky_light(px + i2, py + j4, pz + j3) > 0) {
                            if (lk_biome_top_block(bx, by, bz) == BLK_MYCELIUM) {
                                lk_set(cube, dim, bx, by, bz, mc_state(BLK_MYCELIUM, 0));
                            } else {
                                lk_set(cube, dim, bx, by, bz, mc_state(BLK_GRASS, 0));
                            }
                        }
                    }
                }
            }
        }

        if (lk_block_material_is_lava(liquidBlockId)) {
            for (int j2 = 0; j2 < 16; ++j2) {
                for (int k3 = 0; k3 < 16; ++k3) {
                    for (int k4 = 0; k4 < 8; ++k4) {
                        int flag1 = !aboolean[(j2 * 16 + k3) * 8 + k4] &&
                                    (j2 < 15 && aboolean[((j2 + 1) * 16 + k3) * 8 + k4] ||
                                     j2 > 0 && aboolean[((j2 - 1) * 16 + k3) * 8 + k4] ||
                                     k3 < 15 && aboolean[(j2 * 16 + k3 + 1) * 8 + k4] ||
                                     k3 > 0 && aboolean[(j2 * 16 + (k3 - 1)) * 8 + k4] ||
                                     k4 < 7 && aboolean[(j2 * 16 + k3) * 8 + k4 + 1] ||
                                     k4 > 0 && aboolean[(j2 * 16 + k3) * 8 + (k4 - 1)]);

                        if (flag1 && (k4 < 4 || jrand_int_bound(rand, 2) != 0) &&
                            lk_is_solid(lk_get(cube, dim, px + j2, py + k4, pz + k3))) {
                            lk_set(cube, dim, px + j2, py + k4, pz + k3, mc_state(BLK_STONE, 0));
                        }
                    }
                }
            }
        }

        if (lk_block_material_is_water(liquidBlockId)) {
            for (int k2 = 0; k2 < 16; ++k2) {
                for (int l3 = 0; l3 < 16; ++l3) {
                    int l4 = 4;
                    (void)l4;

                    if (lk_can_block_freeze_water(px + k2, py + 4, pz + l3)) {
                        lk_set(cube, dim, px + k2, py + 4, pz + l3, mc_state(BLK_ICE, 0));
                    }
                }
            }
        }

        return 1;
    }
}

#endif /* MC_LAKE_GEN_H */
