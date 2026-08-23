/* explosion: MC 1.11.2 Explosion.doExplosionA crater + entity damage + knockback.
 *
 * PORT TARGET: net/minecraft/world/Explosion.java doExplosionA (block rays + entity
 * damage + motion). Synthetic cubic grid (EX_DIM^3 packed states). Resistance =
 * mc_bpt_props hardness (air = 0 / Material.AIR skips the resistance subtract).
 *
 * RAND-FREE ray density: vanilla uses world.rand.nextFloat() per face ray
 *   f = size * (0.7F + rand * 0.6F). We fix rand = 0.5F so
 *   f = size * (0.7F + 0.5F * 0.6F). world.rand is not consumed.
 * getBlockDensity / knockback / blast-prot use no Random.
 * Drop/flame RNG paths CUT (doExplosionB not ported). explosionRNG (new Random()
 * in Explosion.java:65) is only the flaming nextInt(3) in doExplosionB:253.
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
#include "block_props_table.h"

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

/* Fixed density scale: 0.7F + 0.5F * 0.6F (rand fixed at 0.5). */
MC_HD static inline float ex_density_scale(void) {
    return 0.7F + 0.5F * 0.6F;
}

/* doExplosionA block-destroy rays on synthetic grid. Marks bitset[vol] for non-air
 * in-bounds positions that would be added to affectedBlockPositions. */
MC_HD static inline void ex_do_explosion_blocks(const u16 *grid,
                                                double ex, double ey, double ez,
                                                float size,
                                                u8 *bitset) {
    for (int i = 0; i < EX_VOL; ++i) bitset[i] = 0;

    float dens = ex_density_scale();
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
                    float f = size * dens;
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

                        if (f > 0.0F && ex_in(bx, by, bz) && !ex_is_air(st)) {
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

/* Full-cube BF_SOLID for World.rayTraceBlocks. Web is NULL_AABB (player_survival). */
MC_HD static inline int ex_cell_solid(const u16 *grid, int ox, int oy, int oz,
                                      int wx, int wy, int wz) {
    int lx = wx - ox, ly = wy - oy, lz = wz - oz;
    int id;
    if (!ex_in(lx, ly, lz)) return 0;
    id = mc_state_id(grid[ex_idx(lx, ly, lz)]);
    if (id <= 0 || id == BLK_WEB) return 0;
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}

/* World.rayTraceBlocks(start, end, false, false, false) World.java:998-1014
 * on the 16^3 sample. Magma extra: full-cube BF_SOLID only. Returns 1 if the
 * ray hits a solid (Java RayTraceResult != null). */
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
    if (ex_cell_solid(grid, ox, oy, oz, l, i1, j1)) return 1;
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
        if (ex_cell_solid(grid, ox, oy, oz, l, i1, j1)) return 1;
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
    ex_do_explosion_blocks(grid, ox, oy, oz, size, bitset);

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
