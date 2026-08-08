/* explosion: MC 1.11.2 Explosion.doExplosionA crater + entity damage math.
 *
 * PORT TARGET: net/minecraft/world/Explosion.java doExplosionA (block rays + entity damage).
 * Synthetic cubic grid (EX_DIM^3 packed states). Resistance = mc_bpt_props hardness
 * (air = 0 / Material.AIR skips the resistance subtract). The synthetic
 * battery has no particles, sound, drops, or fire.
 *
 * The synthetic battery keeps its historical rand=0.5F density and
 * hardness-only resistance table. Live callers use World.rand for all 1,352
 * face-ray draws and a separately promoted resistance table. Live callers can
 * retain the complete affected-position set for the bounded flaming path.
 * Drops and broad doExplosionB ordering remain CUT.
 *
 * Output: sorted destroyed non-air block coords + entity damage floats (open exposure=1).
 * READ-ONLY deps: block_props_table.h (hardness), mc_math.h (floor).
 * Build: -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_EXPLOSION_H
#define MC_EXPLOSION_H

#include <math.h>
#include <stddef.h>
#include "mc.h"
#include "mc_blocks.h"
#include "mc_world.h"
#include "mc_math.h"
#include "mc_rng.h"
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

/* Live Block#getExplosionResistance values promoted by strict runtime cases.
 * The synthetic battery above intentionally retains its hardness-only table. */
MC_HD static inline float ex_live_resistance(u16 st) {
    int id = mc_state_id(st);
    if (id == BLK_STONE) return 6.0F;
    return ex_resistance(st);
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
MC_HD static inline void ex_do_explosion_blocks_impl(
        const u16 *grid, double ex, double ey, double ez, float size,
        u8 *bitset, u8 *affected, JavaRandom *random, int live_resistance) {
    for (int i = 0; i < EX_VOL; ++i) bitset[i] = 0;
    if (affected)
        for (int i = 0; i < EX_VOL; ++i) affected[i] = 0;

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
                    float dens = random
                        ? 0.7F + jrand_float(random) * 0.6F
                        : ex_density_scale();
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
                            float f2 = live_resistance
                                ? ex_live_resistance(st)
                                : ex_resistance(st);
                            f -= (f2 + 0.3F) * 0.3F;
                        }

                        if (f > 0.0F && ex_in(bx, by, bz)) {
                            int index = ex_idx(bx, by, bz);
                            if (affected) affected[index] = 1;
                            if (!ex_is_air(st)) bitset[index] = 1;
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

MC_HD static inline void ex_do_explosion_blocks(const u16 *grid,
                                                double ex, double ey, double ez,
                                                float size,
                                                u8 *bitset) {
    ex_do_explosion_blocks_impl(
        grid, ex, ey, ez, size, bitset, NULL, NULL, 0);
}

/* Live World.rand counterpart used by exact saved-state explosion fixtures. */
MC_HD static inline void ex_do_explosion_blocks_random(
        const u16 *grid, double ex, double ey, double ez, float size,
        u8 *bitset, JavaRandom *random) {
    ex_do_explosion_blocks_impl(
        grid, ex, ey, ez, size, bitset, NULL, random, 1);
}

/* Live flaming explosions revisit every affected position, including cells
 * that were air during doExplosionA. Destruction remains a separate non-air
 * bitset because doExplosionB's fire pass runs after block removal. */
MC_HD static inline void ex_do_explosion_blocks_random_affected(
        const u16 *grid, double ex, double ey, double ez, float size,
        u8 *bitset, u8 *affected, JavaRandom *random) {
    ex_do_explosion_blocks_impl(
        grid, ex, ey, ez, size, bitset, affected, random, 1);
}

MC_HD static inline void ex_do_explosion_blocks_affected(
        const u16 *grid, double ex, double ey, double ez, float size,
        u8 *bitset, u8 *affected) {
    ex_do_explosion_blocks_impl(
        grid, ex, ey, ez, size, bitset, affected, NULL, 0);
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
