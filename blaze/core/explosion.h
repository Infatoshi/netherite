/* explosion: MC 1.11.2 Explosion.doExplosionA crater + entity damage + knockback.
 *
 * PORT TARGET: net/minecraft/world/Explosion.java doExplosionA (block rays + entity
 * damage + motion). Synthetic cubic grid (EX_DIM^3 packed states). Resistance =
 * mc_bpt_props hardness (air = 0 / Material.AIR skips the resistance subtract).
 *
 * Face-ray density: vanilla uses world.rand.nextFloat() per face ray
 *   f = size * (0.7F + rand * 0.6F) (Explosion.java:102). Live sim consumes
 *   that stream when a JavaRandom is passed. NULL rand keeps the old 0.5F
 *   battery path. getBlockDensity / knockback / blast-prot use no Random.
 * getBlockDensity (World.java:2456-2494) samples the 1/((len*2)+1) grid and
 * rayTraceBlocks(stopOnLiquid=false) against movement collision AABBs, not
 * full cubes. doExplosionB drops: HashSet order of affectedBlockPositions then
 * dropBlockAsItemWithChance (explosion_drops.h). explosionRNG (new Random()
 * in Explosion.java:65) is the flaming nextInt(3) in doExplosionB:253.
 *
 * READ-ONLY deps: block_props_table.h (hardness), mc_math.h (floor).
 * Build: -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_EXPLOSION_H
#define MC_EXPLOSION_H

#include <math.h>
#include "mc.h"
#include "mc_blocks.h"
#include "mc_world.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "java_hashset.h"
#include "physics_collision_math.h"

#define EX_DIM 16
#define EX_VOL (EX_DIM * EX_DIM * EX_DIM)
#define EX_FACE 16
/* Max face-ray samples: 16^3 - 14^3 = 1352 face cells; step budget ~ size/0.225. */
#define EX_MAX_DESTROYED EX_VOL
#define EX_NUM_SCENARIOS 5
#define EX_NUM_ENTITIES 3

/* Packed block-pos key for sorted emit: x,y,z in [0,15] -> 12-bit (x<<8)|(y<<4)|z. */
#define EX_PACK(x, y, z) (((u32)(x) << 8) | ((u32)(y) << 4) | (u32)(z))

typedef void (*ExEmitFn)(u64 bits, void *ctx);

MC_HD static inline int ex_idx(int x, int y, int z) {
    return (y * EX_DIM + z) * EX_DIM + x;
}

MC_HD static inline int ex_in(int x, int y, int z) {
    return x >= 0 && x < EX_DIM && y >= 0 && y < EX_DIM && z >= 0 && z < EX_DIM;
}

MC_HD static inline u16 ex_get(const u16 *grid, int x, int y, int z) {
    return ex_in(x, y, z) ? grid[ex_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void ex_set(u16 *grid, int x, int y, int z, u16 s) {
    if (ex_in(x, y, z)) grid[ex_idx(x, y, z)] = s;
}

MC_HD static inline void ex_fill(u16 *grid, u16 s) {
    for (int i = 0; i < EX_VOL; ++i) grid[i] = s;
}

MC_HD static inline void ex_fill_box(u16 *grid, int x0, int y0, int z0,
                                     int x1, int y1, int z1, u16 s) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                ex_set(grid, x, y, z, s);
}

/* Explosion resistance from hardness table. Air/id0 -> 0 (Material.AIR path).
 * Vanilla getExplosionResistance is blockResistance/5; for blocks that only setHardness
 * this equals hardness. Task: use mc_bpt_props hardness directly. */
MC_HD static inline float ex_resistance(u16 st) {
    int id = mc_state_id(st);
    if (id <= 0) return 0.0F;
    return mc_bpt_props(id).hardness;
}

MC_HD static inline int ex_is_air(u16 st) {
    return mc_state_id(st) <= 0;
}

/* MathHelper.sqrt(double) -> (float)Math.sqrt, then widened back for getDistance. */
MC_HD static inline double ex_sqrt_dist(double d0, double d1, double d2) {
    return (double)(float)sqrt(d0 * d0 + d1 * d1 + d2 * d2);
}

/* Face-cell count of the 16^3 doExplosionA loop: 16^3 - 14^3 = 1352.
 * Explosion.java:88-94: j,k,l in [0,16), face when any coord is 0 or 15. */
MC_HD static inline int ex_face_ray_count(void) {
    return EX_FACE * EX_FACE * EX_FACE
         - (EX_FACE - 2) * (EX_FACE - 2) * (EX_FACE - 2);
}

/* Fixed 0.5F density scale used when no World.rand is supplied. */
MC_HD static inline float ex_density_scale(void) {
    return 0.7F + 0.5F * 0.6F;
}

/* Explosion.java:102 f = size * (0.7F + world.rand.nextFloat() * 0.6F). */
MC_HD static inline float ex_ray_strength(float size, JavaRandom *rand) {
    float u = rand ? jrand_float(rand) : 0.5F;
    return size * (0.7F + u * 0.6F);
}

/* doExplosionA block-destroy rays on synthetic grid. Marks bitset[vol] for
 * in-bounds non-air. When hs is non-NULL, HashSet.add every BlockPos with
 * f>0 (air and out-of-grid included: Explosion.java:118-121). World coords
 * are (ox+bx, oy+by, oz+bz). hs==NULL keeps the battery path. */
MC_HD static inline void ex_do_explosion_blocks(const u16 *grid,
                                                double ex, double ey, double ez,
                                                float size,
                                                u8 *bitset,
                                                JavaRandom *rand,
                                                JavaHashSet *hs,
                                                int ox, int oy, int oz) {
    for (int i = 0; i < EX_VOL; ++i) bitset[i] = 0;

    /* step decrement and advance match oracle float/double literals exactly */
    const float step_dec = 0.22500001F;
    const double step_adv = 0.30000001192092896;

    for (int j = 0; j < EX_FACE; ++j) {
        for (int k = 0; k < EX_FACE; ++k) {
            for (int l = 0; l < EX_FACE; ++l) {
                if (j == 0 || j == 15 || k == 0 || k == 15 || l == 0 || l == 15) {
                    double d0 = (double)((float)j / 15.0F * 2.0F - 1.0F);
                    double d1 = (double)((float)k / 15.0F * 2.0F - 1.0F);
                    double d2 = (double)((float)l / 15.0F * 2.0F - 1.0F);
                    double d3 = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
                    d0 = d0 / d3;
                    d1 = d1 / d3;
                    d2 = d2 / d3;
                    float f = ex_ray_strength(size, rand);
                    double d4 = ex;
                    double d6 = ey;
                    double d8 = ez;

                    /* f1 init 0.3F is oracle dead local; loop decrements f only */
                    for (; f > 0.0F; f -= step_dec) {
                        int bx = mc_floor(d4);
                        int by = mc_floor(d6);
                        int bz = mc_floor(d8);
                        u16 st = ex_get(grid, bx, by, bz);

                        if (!ex_is_air(st)) {
                            float f2 = ex_resistance(st);
                            f -= (f2 + 0.3F) * 0.3F;
                        }

                        if (f > 0.0F) {
                            /* Sets.newHashSet add, including air (Explosion.java:118-121). */
                            if (hs)
                                jhs_add(hs, ox + bx, oy + by, oz + bz);
                            if (ex_in(bx, by, bz) && !ex_is_air(st))
                                bitset[ex_idx(bx, by, bz)] = 1;
                        }

                        d4 += d0 * step_adv;
                        d6 += d1 * step_adv;
                        d8 += d2 * step_adv;
                    }
                }
            }
        }
    }
}

/* Entity damage from Explosion.doExplosionA (no blast-prot, no immune, exposure open=1).
 * damage = (int)((d10*d10 + d10)/2 * 7 * f3 + 1) with d10 = (1 - dist/f3) * exposure. */
MC_HD static inline float ex_entity_damage(double ent_x, double ent_y, double ent_z,
                                           double ex, double ey, double ez,
                                           float size, float exposure) {
    float f3 = size * 2.0F;
    if (f3 <= 0.0F) return 0.0F;
    double dist = ex_sqrt_dist(ent_x - ex, ent_y - ey, ent_z - ez);
    double d12 = dist / (double)f3;
    if (d12 > 1.0) return 0.0F;
    double d10 = (1.0 - d12) * (double)exposure;
    /* cast to int truncates toward zero (damage is non-negative) */
    float dmg = (float)((int)((d10 * d10 + d10) / 2.0 * 7.0 * (double)f3 + 1.0));
    return dmg;
}

/* IBlockState.isFullBlock for doExplosionB fire (Explosion.java:253).
 * Subset: opaque cubes. Slabs/fences/stairs/chests/soul sand/cactus are not. */
MC_HD static inline int ex_is_full_block(int id) {
    if (id <= 0 || id == BLK_WEB) return 0;
    if (id == BLK_FLOWING_WATER || id == BLK_WATER ||
        id == BLK_FLOWING_LAVA || id == BLK_LAVA) return 0;
    if (id == BLK_STONE_SLAB || id == BLK_WOODEN_SLAB ||
        id == BLK_RED_SANDSTONE_SLAB) return 0;
    if (id == BLK_FENCE || id == BLK_NETHER_BRICK_FENCE ||
        id == BLK_COBBLESTONE_WALL) return 0;
    if (id == BLK_OAK_STAIRS || id == BLK_STONE_STAIRS) return 0;
    if (id == BLK_TRAPDOOR || id == BLK_LADDER || id == BLK_CACTUS ||
        id == BLK_SOUL_SAND) return 0;
    if (id == 51 || id == 54 || id == 130 || id == 146) return 0;
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}

MC_HD static inline int ex_world_id(const u16 *grid, int ox, int oy, int oz,
                                    int wx, int wy, int wz) {
    int lx = wx - ox, ly = wy - oy, lz = wz - oz;
    if (!ex_in(lx, ly, lz)) return 0;
    return mc_state_id(grid[ex_idx(lx, ly, lz)]);
}

MC_HD static inline int ex_world_meta(const u16 *grid, int ox, int oy, int oz,
                                      int wx, int wy, int wz) {
    int lx = wx - ox, ly = wy - oy, lz = wz - oz;
    if (!ex_in(lx, ly, lz)) return 0;
    return mc_state_meta(grid[ex_idx(lx, ly, lz)]);
}

MC_HD static inline int ex_fence_connects_id(int id, int neighbor) {
    if (id == BLK_NETHER_BRICK_FENCE && neighbor == BLK_NETHER_BRICK_FENCE)
        return 1;
    if (id == BLK_FENCE && neighbor == BLK_FENCE) return 1;
    if (neighbor == 107) return 1;
    if (neighbor <= 0 || neighbor == BLK_WEB) return 0;
    if (!(mc_bpt_props(neighbor).flags & BF_SOLID)) return 0;
    return neighbor != BLK_FENCE && neighbor != BLK_NETHER_BRICK_FENCE &&
           neighbor != BLK_COBBLESTONE_WALL;
}

MC_HD static inline int ex_wall_connects_id(int neighbor) {
    if (neighbor == BLK_COBBLESTONE_WALL || neighbor == 107) return 1;
    if (neighbor <= 0 || neighbor == BLK_WEB) return 0;
    if (!(mc_bpt_props(neighbor).flags & BF_SOLID)) return 0;
    return neighbor != BLK_FENCE && neighbor != BLK_NETHER_BRICK_FENCE;
}

#define EX_MAX_BOXES 6

MC_HD static inline int ex_push_box(McAABB *out, int n, int max, McAABB box) {
    if (n < max) out[n++] = box;
    return n;
}

/* Movement collision AABBs (player_survival.h psv_collect_blocks) on the
 * 16^3 sample. Neighbors off-grid are air. */
MC_HD static inline int ex_cell_boxes(const u16 *grid, int ox, int oy, int oz,
                                      int wx, int wy, int wz,
                                      McAABB *out, int max) {
    int id, meta, n = 0;
    int x = wx, y = wy, z = wz;
    if (max <= 0) return 0;
    id = ex_world_id(grid, ox, oy, oz, wx, wy, wz);
    if (id <= 0 || id == BLK_WEB) return 0;
    if (mc_bpt_props(id).flags & BF_LIQUID) return 0;
    if (!(mc_bpt_props(id).flags & BF_SOLID)) return 0;
    meta = ex_world_meta(grid, ox, oy, oz, wx, wy, wz);
    if (id == BLK_CACTUS) {
        return ex_push_box(out, n, max,
            mc_aabb_make(x + 0.0625, y, z + 0.0625,
                         x + 0.9375, y + 0.9375, z + 0.9375));
    }
    if (id == 54 || id == 130 || id == 146) {
        double min_x = 0.0625, max_x = 0.9375;
        double min_z = 0.0625, max_z = 0.9375;
        if (id != 130) {
            if (ex_world_id(grid, ox, oy, oz, x - 1, y, z) == id) min_x = 0.0;
            if (ex_world_id(grid, ox, oy, oz, x + 1, y, z) == id) max_x = 1.0;
            if (ex_world_id(grid, ox, oy, oz, x, y, z - 1) == id) min_z = 0.0;
            if (ex_world_id(grid, ox, oy, oz, x, y, z + 1) == id) max_z = 1.0;
        }
        return ex_push_box(out, n, max,
            mc_aabb_make(x + min_x, y, z + min_z,
                         x + max_x, y + 0.875, z + max_z));
    }
    if (id == BLK_TRAPDOOR) {
        double min_x = 0.0, min_y = 0.0, min_z = 0.0;
        double max_x = 1.0, max_y = 1.0, max_z = 1.0;
        if (meta & 4) {
            switch (meta & 3) {
                case 0: min_z = 0.8125; break;
                case 1: max_z = 0.1875; break;
                case 2: min_x = 0.8125; break;
                default: max_x = 0.1875; break;
            }
        } else if (meta & 8) {
            min_y = 0.8125;
        } else {
            max_y = 0.1875;
        }
        return ex_push_box(out, n, max,
            mc_aabb_make(x + min_x, y + min_y, z + min_z,
                         x + max_x, y + max_y, z + max_z));
    }
    if (id == BLK_STONE_SLAB || id == BLK_WOODEN_SLAB ||
        id == BLK_RED_SANDSTONE_SLAB) {
        double min_y = (meta & 8) ? y + 0.5 : (double)y;
        double max_y = (meta & 8) ? y + 1.0 : y + 0.5;
        return ex_push_box(out, n, max,
            mc_aabb_make(x, min_y, z, x + 1.0, max_y, z + 1.0));
    }
    if (id == BLK_OAK_STAIRS || id == BLK_STONE_STAIRS) {
        int top = (meta & 4) != 0;
        double slab_y0 = top ? 0.5 : 0.0;
        double slab_y1 = top ? 1.0 : 0.5;
        double step_y0 = top ? 0.0 : 0.5;
        double step_y1 = top ? 0.5 : 1.0;
        double x0 = 0.0, x1 = 1.0, z0 = 0.0, z1 = 1.0;
        switch (meta & 3) {
            case 0: x0 = 0.5; break;
            case 1: x1 = 0.5; break;
            case 2: z0 = 0.5; break;
            default: z1 = 0.5; break;
        }
        n = ex_push_box(out, n, max,
            mc_aabb_make(x, y + slab_y0, z, x + 1.0, y + slab_y1, z + 1.0));
        return ex_push_box(out, n, max,
            mc_aabb_make(x + x0, y + step_y0, z + z0,
                         x + x1, y + step_y1, z + z1));
    }
    if (id == BLK_LADDER) {
        double x0 = 0.0, x1 = 1.0, z0 = 0.0, z1 = 1.0;
        switch (meta) {
            case 2: z0 = 0.8125; break;
            case 3: z1 = 0.1875; break;
            case 4: x0 = 0.8125; break;
            default: x1 = 0.1875; break;
        }
        return ex_push_box(out, n, max,
            mc_aabb_make(x + x0, y, z + z0, x + x1, y + 1.0, z + z1));
    }
    if (id == BLK_SOUL_SAND) {
        return ex_push_box(out, n, max,
            mc_aabb_make(x, y, z, x + 1.0, y + 0.875, z + 1.0));
    }
    if (id == BLK_FENCE || id == BLK_NETHER_BRICK_FENCE) {
        n = ex_push_box(out, n, max,
            mc_aabb_make(x + 0.375, y, z + 0.375,
                         x + 0.625, y + 1.5, z + 0.625));
        if (ex_fence_connects_id(id, ex_world_id(grid, ox, oy, oz, x, y, z - 1)))
            n = ex_push_box(out, n, max,
                mc_aabb_make(x + 0.375, y, z, x + 0.625, y + 1.5, z + 0.375));
        if (ex_fence_connects_id(id, ex_world_id(grid, ox, oy, oz, x + 1, y, z)))
            n = ex_push_box(out, n, max,
                mc_aabb_make(x + 0.625, y, z + 0.375,
                             x + 1.0, y + 1.5, z + 0.625));
        if (ex_fence_connects_id(id, ex_world_id(grid, ox, oy, oz, x, y, z + 1)))
            n = ex_push_box(out, n, max,
                mc_aabb_make(x + 0.375, y, z + 0.625,
                             x + 0.625, y + 1.5, z + 1.0));
        if (ex_fence_connects_id(id, ex_world_id(grid, ox, oy, oz, x - 1, y, z)))
            n = ex_push_box(out, n, max,
                mc_aabb_make(x, y, z + 0.375, x + 0.375, y + 1.5, z + 0.625));
        return n;
    }
    if (id == BLK_COBBLESTONE_WALL) {
        int north = ex_wall_connects_id(ex_world_id(grid, ox, oy, oz, x, y, z - 1));
        int east  = ex_wall_connects_id(ex_world_id(grid, ox, oy, oz, x + 1, y, z));
        int south = ex_wall_connects_id(ex_world_id(grid, ox, oy, oz, x, y, z + 1));
        int west  = ex_wall_connects_id(ex_world_id(grid, ox, oy, oz, x - 1, y, z));
        double x0 = west ? 0.0 : 0.25, x1 = east ? 1.0 : 0.75;
        double z0 = north ? 0.0 : 0.25, z1 = south ? 1.0 : 0.75;
        if (north && south && !east && !west) { x0 = 0.3125; x1 = 0.6875; }
        if (east && west && !north && !south) { z0 = 0.3125; z1 = 0.6875; }
        return ex_push_box(out, n, max,
            mc_aabb_make(x + x0, y, z + z0, x + x1, y + 1.5, z + z1));
    }
    return ex_push_box(out, n, max,
        mc_aabb_make(x, y, z, x + 1.0, y + 1.0, z + 1.0));
}

/* AxisAlignedBB.calculateIntercept hit/miss (projectile_motion.h pm_*). */
MC_HD static inline int ex_aabb_is_closest(double ax, double ay, double az,
                                           int have, double bx, double by, double bz,
                                           double cx, double cy, double cz) {
    double dx1, dy1, dz1, dx2, dy2, dz2;
    if (!have) return 1;
    dx1 = cx - ax; dy1 = cy - ay; dz1 = cz - az;
    dx2 = bx - ax; dy2 = by - ay; dz2 = bz - az;
    return (dx1 * dx1 + dy1 * dy1 + dz1 * dz1) < (dx2 * dx2 + dy2 * dy2 + dz2 * dz2);
}
MC_HD static inline int ex_aabb_mid_x(double ax, double ay, double az,
                                      double bx, double by, double bz, double x,
                                      double *ox, double *oy, double *oz) {
    double d0 = bx - ax, d1 = by - ay, d2 = bz - az, d3;
    if (d0 * d0 < 1.0000000116860974E-7) return 0;
    d3 = (x - ax) / d0;
    if (d3 < 0.0 || d3 > 1.0) return 0;
    *ox = ax + d0 * d3; *oy = ay + d1 * d3; *oz = az + d2 * d3;
    return 1;
}
MC_HD static inline int ex_aabb_mid_y(double ax, double ay, double az,
                                      double bx, double by, double bz, double y,
                                      double *ox, double *oy, double *oz) {
    double d0 = bx - ax, d1 = by - ay, d2 = bz - az, d3;
    if (d1 * d1 < 1.0000000116860974E-7) return 0;
    d3 = (y - ay) / d1;
    if (d3 < 0.0 || d3 > 1.0) return 0;
    *ox = ax + d0 * d3; *oy = ay + d1 * d3; *oz = az + d2 * d3;
    return 1;
}
MC_HD static inline int ex_aabb_mid_z(double ax, double ay, double az,
                                      double bx, double by, double bz, double z,
                                      double *ox, double *oy, double *oz) {
    double d0 = bx - ax, d1 = by - ay, d2 = bz - az, d3;
    if (d2 * d2 < 1.0000000116860974E-7) return 0;
    d3 = (z - az) / d2;
    if (d3 < 0.0 || d3 > 1.0) return 0;
    *ox = ax + d0 * d3; *oy = ay + d1 * d3; *oz = az + d2 * d3;
    return 1;
}
MC_HD static inline int ex_aabb_hit(const McAABB *bb,
                                    double ax, double ay, double az,
                                    double bx, double by, double bz) {
    int have = 0, face = 4;
    double cx = 0.0, cy = 0.0, cz = 0.0, tx, ty, tz;
    if (ex_aabb_mid_x(ax, ay, az, bx, by, bz, bb->minX, &tx, &ty, &tz) &&
        ty >= bb->minY && ty <= bb->maxY && tz >= bb->minZ && tz <= bb->maxZ) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 4;
    }
    if (ex_aabb_mid_x(ax, ay, az, bx, by, bz, bb->maxX, &tx, &ty, &tz) &&
        ty >= bb->minY && ty <= bb->maxY && tz >= bb->minZ && tz <= bb->maxZ &&
        ex_aabb_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 5;
    }
    if (ex_aabb_mid_y(ax, ay, az, bx, by, bz, bb->minY, &tx, &ty, &tz) &&
        tx >= bb->minX && tx <= bb->maxX && tz >= bb->minZ && tz <= bb->maxZ &&
        ex_aabb_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 0;
    }
    if (ex_aabb_mid_y(ax, ay, az, bx, by, bz, bb->maxY, &tx, &ty, &tz) &&
        tx >= bb->minX && tx <= bb->maxX && tz >= bb->minZ && tz <= bb->maxZ &&
        ex_aabb_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 1;
    }
    if (ex_aabb_mid_z(ax, ay, az, bx, by, bz, bb->minZ, &tx, &ty, &tz) &&
        tx >= bb->minX && tx <= bb->maxX && ty >= bb->minY && ty <= bb->maxY &&
        ex_aabb_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 2;
    }
    if (ex_aabb_mid_z(ax, ay, az, bx, by, bz, bb->maxZ, &tx, &ty, &tz) &&
        tx >= bb->minX && tx <= bb->maxX && ty >= bb->minY && ty <= bb->maxY &&
        ex_aabb_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        have = 1; face = 3;
    }
    (void)face;
    return have;
}

/* Block.collisionRayTrace vs movement collision AABBs. */
MC_HD static inline int ex_ray_hits_cell(const u16 *grid, int ox, int oy, int oz,
                                         int wx, int wy, int wz,
                                         double sx, double sy, double sz,
                                         double tx, double ty, double tz) {
    McAABB boxes[EX_MAX_BOXES];
    int n, i;
    n = ex_cell_boxes(grid, ox, oy, oz, wx, wy, wz, boxes, EX_MAX_BOXES);
    for (i = 0; i < n; ++i) {
        if (ex_aabb_hit(&boxes[i], sx, sy, sz, tx, ty, tz))
            return 1;
    }
    return 0;
}

/* World.rayTraceBlocks(start, end, false, false, false) World.java:998-1014
 * on the 16^3 sample. stopOnLiquid false: liquids skipped (no collision box).
 * Returns 1 if the ray hits a collision AABB (Java RayTraceResult != null). */
MC_HD MC_NOINLINE static inline int ex_ray_blocked(const u16 *grid, int ox, int oy, int oz,
                                       double sx, double sy, double sz,
                                       double tx, double ty, double tz) {
    int i, j, k, l, i1, j1, k1;
    double curX, curY, curZ;
    if (sx != sx || sy != sy || sz != sz) return 0;
    if (tx != tx || ty != ty || tz != tz) return 0;
    i = mc_floor(tx);
    j = mc_floor(ty);
    k = mc_floor(tz);
    l = mc_floor(sx);
    i1 = mc_floor(sy);
    j1 = mc_floor(sz);
    if (ex_ray_hits_cell(grid, ox, oy, oz, l, i1, j1, sx, sy, sz, tx, ty, tz))
        return 1;
    curX = sx;
    curY = sy;
    curZ = sz;
    k1 = 200;
    while (k1-- >= 0) {
        int flag2 = 1, flag = 1, flag1 = 1, nf;
        double d0 = 999.0, d1 = 999.0, d2 = 999.0;
        double d3 = 999.0, d4 = 999.0, d5 = 999.0;
        double d6, d7, d8;
        if (curX != curX || curY != curY || curZ != curZ) return 0;
        if (l == i && i1 == j && j1 == k) return 0;
        if (i > l) d0 = (double)l + 1.0;
        else if (i < l) d0 = (double)l + 0.0;
        else flag2 = 0;
        if (j > i1) d1 = (double)i1 + 1.0;
        else if (j < i1) d1 = (double)i1 + 0.0;
        else flag = 0;
        if (k > j1) d2 = (double)j1 + 1.0;
        else if (k < j1) d2 = (double)j1 + 0.0;
        else flag1 = 0;
        d6 = tx - curX;
        d7 = ty - curY;
        d8 = tz - curZ;
        if (flag2) d3 = (d0 - curX) / d6;
        if (flag) d4 = (d1 - curY) / d7;
        if (flag1) d5 = (d2 - curZ) / d8;
        if (d3 == -0.0) d3 = -1.0E-4;
        if (d4 == -0.0) d4 = -1.0E-4;
        if (d5 == -0.0) d5 = -1.0E-4;
        if (d3 < d4 && d3 < d5) {
            nf = (i > l) ? 4 : 5;
            curX = d0;
            curY = curY + d7 * d3;
            curZ = curZ + d8 * d3;
        } else if (d4 < d5) {
            nf = (j > i1) ? 0 : 1;
            curX = curX + d6 * d4;
            curY = d1;
            curZ = curZ + d8 * d4;
        } else {
            nf = (k > j1) ? 2 : 3;
            curX = curX + d6 * d5;
            curY = curY + d7 * d5;
            curZ = d2;
        }
        l = mc_floor(curX) - (nf == 5 ? 1 : 0);
        i1 = mc_floor(curY) - (nf == 1 ? 1 : 0);
        j1 = mc_floor(curZ) - (nf == 3 ? 1 : 0);
        /* World.java:1167 collisionRayTrace(blockpos, vec31, vec32) after
         * DDA rewrites vec31 to the current point. */
        if (ex_ray_hits_cell(grid, ox, oy, oz, l, i1, j1,
                             curX, curY, curZ, tx, ty, tz))
            return 1;
    }
    return 0;
}

/* World.getBlockDensity World.java:2456-2494. d3/d4 use java.lang.Math.floor.
 * Sample increment is (float)((double)f + d0). Return (float)i / (float)j. */
MC_HD MC_NOINLINE static inline float ex_block_density(const u16 *grid, int ox, int oy, int oz,
                                           double vx, double vy, double vz,
                                           double minx, double miny, double minz,
                                           double maxx, double maxy, double maxz) {
    double d0 = 1.0 / ((maxx - minx) * 2.0 + 1.0);
    double d1 = 1.0 / ((maxy - miny) * 2.0 + 1.0);
    double d2 = 1.0 / ((maxz - minz) * 2.0 + 1.0);
    double d3, d4;
    float f, f1, f2;
    int i = 0, j = 0;
    if (d0 < 0.0 || d1 < 0.0 || d2 < 0.0) return 0.0F;
    d3 = (1.0 - floor(1.0 / d0) * d0) / 2.0;
    d4 = (1.0 - floor(1.0 / d2) * d2) / 2.0;
    for (f = 0.0F; f <= 1.0F; f = (float)((double)f + d0)) {
        for (f1 = 0.0F; f1 <= 1.0F; f1 = (float)((double)f1 + d1)) {
            for (f2 = 0.0F; f2 <= 1.0F; f2 = (float)((double)f2 + d2)) {
                double d5 = minx + (maxx - minx) * (double)f;
                double d6 = miny + (maxy - miny) * (double)f1;
                double d7 = minz + (maxz - minz) * (double)f2;
                if (!ex_ray_blocked(grid, ox, oy, oz,
                                    d5 + d3, d6, d7 + d4, vx, vy, vz))
                    ++i;
                ++j;
            }
        }
    }
    if (j == 0) return 0.0F;
    return (float)i / (float)j;
}

/* EnchantmentProtection.getBlastDamageReduction EnchantmentProtection.java:99-108.
 * MathHelper.floor of damage * (float)level * 0.15F. level 0 is identity. */
MC_HD static inline double ex_blast_reduction(double damage, int prot) {
    if (prot > 0)
        damage -= (double)mc_floor(damage * (double)((float)prot * 0.15F));
    return damage;
}

typedef struct {
    int hit;
    float damage;
    double d10;
    double mapx, mapy, mapz; /* playerKnockbackMap: d5*d10 Explosion.java:184 */
    double addx, addy, addz; /* motion += d5*d11 Explosion.java:174-176 */
} ExBlast;

/* Explosion.doExplosionA entity loop Explosion.java:144-188.
 * d12 = getDistance(feet) / f3 (Entity.java getDistance + MathHelper.sqrt).
 * d7 uses posY + (double)eyeHeight. exposure is getBlockDensity (float->double).
 * Damage d2i then i2f. d11 = blast-prot on living; map uses unreduced d10. */
MC_HD static inline void ex_entity_blast(double posx, double posy, double posz,
                                         float eye,
                                         double ex, double ey, double ez,
                                         float size, float exposure, int prot,
                                         ExBlast *out) {
    float f3;
    double d12, d5, d7, d9, d13, d10, d11;
    out->hit = 0;
    out->damage = 0.0f;
    out->d10 = 0.0;
    out->mapx = out->mapy = out->mapz = 0.0;
    out->addx = out->addy = out->addz = 0.0;
    f3 = size * 2.0F;
    if (f3 <= 0.0F) return;
    d12 = ex_sqrt_dist(posx - ex, posy - ey, posz - ez) / (double)f3;
    if (d12 > 1.0) return;
    d5 = posx - ex;
    d7 = posy + (double)eye - ey;
    d9 = posz - ez;
    d13 = (double)(float)sqrt(d5 * d5 + d7 * d7 + d9 * d9);
    if (d13 == 0.0) return;
    d5 = d5 / d13;
    d7 = d7 / d13;
    d9 = d9 / d13;
    d10 = (1.0 - d12) * (double)exposure;
    out->hit = 1;
    out->d10 = d10;
    /* javap Explosion.doExplosionA: d2i then i2f into attackEntityFrom */
    out->damage = (float)((int)((d10 * d10 + d10) / 2.0 * 7.0 * (double)f3 + 1.0));
    d11 = ex_blast_reduction(d10, prot);
    out->addx = d5 * d11;
    out->addy = d7 * d11;
    out->addz = d9 * d11;
    out->mapx = d5 * d10;
    out->mapy = d7 * d10;
    out->mapz = d9 * d10;
}

/* ---- scenarios (battery) ----
 * 0 empty air size 4.0 at center; entity damages only (open exposure)
 * 1 solid stone cube size 4.0 TNT-like
 * 2 solid stone cube size 2.0
 * 3 layered dirt (y=0..7) / stone (y=8..15) size 4.0
 * 4 solid dirt cube size 1.0
 */
MC_HD static inline float ex_scenario_size(int idx) {
    switch (idx) {
        case 0: return 4.0F;
        case 1: return 4.0F;
        case 2: return 2.0F;
        case 3: return 4.0F;
        default: return 1.0F;
    }
}

MC_HD static inline void ex_scenario_origin(int idx, double *ox, double *oy, double *oz) {
    (void)idx;
    /* geometric center of the 16^3 volume (block [8,8,8] corner at 8,8,8) */
    *ox = 8.0;
    *oy = 8.0;
    *oz = 8.0;
}

MC_HD static inline void ex_scenario_grid(int idx, u16 *grid) {
    u16 air = mc_state(BLK_AIR, 0);
    u16 stone = mc_state(BLK_STONE, 0);
    u16 dirt = mc_state(BLK_DIRT, 0);
    switch (idx) {
        case 0:
            ex_fill(grid, air);
            break;
        case 1:
        case 2:
            ex_fill(grid, stone);
            break;
        case 3:
            ex_fill(grid, dirt);
            ex_fill_box(grid, 0, 8, 0, EX_DIM - 1, EX_DIM - 1, EX_DIM - 1, stone);
            break;
        default:
            ex_fill(grid, dirt);
            break;
    }
}

/* Fixed entity sample points for damage (relative to origin; used on all scenes). */
MC_HD static inline void ex_entity_pos(int ei, double *x, double *y, double *z) {
    /* feet positions around center blast at (8,8,8) */
    switch (ei) {
        case 0: *x = 8.0; *y = 8.0; *z = 8.0; break;   /* at blast center */
        case 1: *x = 8.0; *y = 8.0; *z = 4.0; break;   /* 4 blocks away */
        default: *x = 8.0; *y = 8.0; *z = 1.0; break;  /* 7 blocks away */
    }
}

MC_HD static inline void ex_emit_u32(u32 v, ExEmitFn emit, void *ctx) {
    emit((u64)v, ctx);
}

MC_HD static inline void ex_emit_float(float v, ExEmitFn emit, void *ctx) {
    union { float f; u32 u; } u;
    u.f = v;
    emit((u64)u.u, ctx);
}

/* Run one scenario: emit count, packed destroyed coords (x,y,z order), then entity damages. */
MC_HD static inline void ex_run_scenario(int idx, u16 *grid, u8 *bitset,
                                         ExEmitFn emit, void *ctx) {
    double ox, oy, oz;
    float size = ex_scenario_size(idx);
    ex_scenario_origin(idx, &ox, &oy, &oz);
    ex_scenario_grid(idx, grid);
    ex_do_explosion_blocks(grid, ox, oy, oz, size, bitset, NULL, NULL, 0, 0, 0);

    /* count + emit sorted by x, then y, then z */
    u32 count = 0;
    for (int x = 0; x < EX_DIM; ++x)
        for (int y = 0; y < EX_DIM; ++y)
            for (int z = 0; z < EX_DIM; ++z)
                if (bitset[ex_idx(x, y, z)]) ++count;
    ex_emit_u32(count, emit, ctx);
    for (int x = 0; x < EX_DIM; ++x)
        for (int y = 0; y < EX_DIM; ++y)
            for (int z = 0; z < EX_DIM; ++z)
                if (bitset[ex_idx(x, y, z)])
                    ex_emit_u32(EX_PACK(x, y, z), emit, ctx);

    /* entity damage with open exposure (1.0); valid for air scene; still defined on solids */
    float exposure = 1.0F;
    for (int ei = 0; ei < EX_NUM_ENTITIES; ++ei) {
        double ex_, ey_, ez_;
        ex_entity_pos(ei, &ex_, &ey_, &ez_);
        float dmg = ex_entity_damage(ex_, ey_, ez_, ox, oy, oz, size, exposure);
        ex_emit_float(dmg, emit, ctx);
    }
}

MC_HD static inline void ex_run_all(ExEmitFn emit, void *ctx) {
    u16 grid[EX_VOL];
    u8 bitset[EX_VOL];
    for (int i = 0; i < EX_NUM_SCENARIOS; ++i)
        ex_run_scenario(i, grid, bitset, emit, ctx);
}

#endif /* MC_EXPLOSION_H */
