/* projectile_entity_hit: EntityArrow.onHit entity branch (MC 1.11.2).
 *
 * INTERNAL verify (CPU==CUDA). Synthetic arrow-on-entity scenarios; applies velocity-based
 * arrow damage + combat_math armor/protection reduction + knockback subset.
 *
 * PORT TARGETS:
 *   net/minecraft/entity/projectile/EntityArrow.java onHit(entity!=null)
 *   net/minecraft/entity/EntityLivingBase.java damageEntity / applyArmorCalculations
 *
 * READ-ONLY deps: projectile_motion.h (pm_sqrt, pm_aabb_intercept), combat_math.h.
 * Hash RNG replaces rand.nextInt for critical bonus (SPEC rule 1). CUT: thorns/bane/arthropod
 * enchants, fire-on-hit, setDead side effects, pickup bounce-dead, PvP team check,
 * arrowCountInEntity, sound/particles. Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_PROJECTILE_ENTITY_HIT_H
#define MC_PROJECTILE_ENTITY_HIT_H

#include "mc.h"
#include "mc_rng.h"
#include "projectile_motion.h"
#include "combat_math.h"

#define PEH_NUM_SCENARIOS 12
#define PEH_PURPOSE_CRIT  0x50454801u

typedef struct {
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    double damage;
    int    critical;
    int    knockbackStrength;
    int    ticksInAir;
    int    alive;
} PehArrow;

typedef struct {
    double posX, posY, posZ;
    float  health;
    int    armor_idx;
    double velX, velY, velZ;
} PehTarget;

typedef struct {
    float  raw_damage;
    float  final_damage;
    float  hp_after;
    u32    flags; /* bit0 hit_success, bit1 arrow_alive */
    float  kb_x, kb_y, kb_z;
    double arrow_mx, arrow_my, arrow_mz;
} PehHitOut;

typedef void (*PehEmitFn)(u64 bits, void *ctx);

/* MathHelper.ceil(double) */
MC_HD static inline int peh_ceil(double v) {
    int i = mc_d2i(v);
    return v > (double)i ? i + 1 : i;
}

MC_HD static inline void peh_emit_u32(u32 v, PehEmitFn emit, void *ctx) {
    emit((u64)v, ctx);
}

MC_HD static inline void peh_emit_float(float v, PehEmitFn emit, void *ctx) {
    u32 bits;
    union { float f; u32 u; } u;
    u.f = v;
    bits = u.u;
    emit((u64)bits, ctx);
}

MC_HD static inline void peh_emit_double(double v, PehEmitFn emit, void *ctx) {
    union { double d; u64 u; } u;
    u.d = v;
    emit(u.u, ctx);
}

MC_HD static inline void peh_emit_out(const PehHitOut *o, PehEmitFn emit, void *ctx) {
    peh_emit_float(o->raw_damage, emit, ctx);
    peh_emit_float(o->final_damage, emit, ctx);
    peh_emit_float(o->hp_after, emit, ctx);
    peh_emit_u32(o->flags, emit, ctx);
    peh_emit_float(o->kb_x, emit, ctx);
    peh_emit_float(o->kb_y, emit, ctx);
    peh_emit_float(o->kb_z, emit, ctx);
    peh_emit_double(o->arrow_mx, emit, ctx);
    peh_emit_double(o->arrow_my, emit, ctx);
    peh_emit_double(o->arrow_mz, emit, ctx);
}

/* EntityLivingBase 0.6 x 1.8 AABB at feet, expanded 0.3 (findEntityOnPath). */
MC_HD static inline McAABB peh_target_aabb(const PehTarget *t) {
    const double w = 0.30000001192092896;
    const double h = 1.800000011920929;
    McAABB bb = mc_aabb_make(-w, 0.0, -w, w, h, w);
    bb = mc_aabb_offset(&bb, t->posX, t->posY, t->posZ);
    bb.minX -= 0.30000001192092896;
    bb.minY -= 0.30000001192092896;
    bb.minZ -= 0.30000001192092896;
    bb.maxX += 0.30000001192092896;
    bb.maxY += 0.30000001192092896;
    bb.maxZ += 0.30000001192092896;
    return bb;
}

MC_HD static inline int peh_find_entity_on_path(const PehArrow *a, const PehTarget *t,
                                                double *hx, double *hy, double *hz) {
    if (a->ticksInAir < 5) return 0; /* shootingEntity skip not modeled; always pass for our scenes */
    double sx = a->posX, sy = a->posY, sz = a->posZ;
    double ex = a->posX + a->motionX;
    double ey = a->posY + a->motionY;
    double ez = a->posZ + a->motionZ;
    McAABB bb = peh_target_aabb(t);
    int side;
    return pm_aabb_intercept(&bb, sx, sy, sz, ex, ey, ez, hx, hy, hz, &side);
}

MC_HD static inline float peh_arrow_raw_damage(i64 seed, int scen_idx, const PehArrow *a,
                                               const PehTarget *t) {
    float f = pm_sqrt(a->motionX * a->motionX + a->motionY * a->motionY +
                      a->motionZ * a->motionZ);
    int i = peh_ceil((double)f * a->damage);
    if (a->critical) {
        u64 h = mc_hash_seed((u64)seed, scen_idx,
                             mc_floor(t->posX), mc_floor(t->posY), mc_floor(t->posZ),
                             PEH_PURPOSE_CRIT);
        i += mc_hash_bound(h, i / 2 + 2);
    }
    return (float)i;
}

/* EntityArrow.onHit entity branch (trimmed). */
MC_HD static inline void peh_on_hit_entity(i64 seed, int scen_idx,
                                           PehArrow *a, PehTarget *t, PehHitOut *o) {
    o->raw_damage = peh_arrow_raw_damage(seed, scen_idx, a, t);
    o->final_damage = 0.0f;
    o->kb_x = o->kb_y = o->kb_z = 0.0f;
    o->flags = 0;

    if (t->health <= 0.0f) {
        a->motionX *= -0.10000000149011612;
        a->motionY *= -0.10000000149011612;
        a->motionZ *= -0.10000000149011612;
        o->hp_after = t->health;
        o->arrow_mx = a->motionX;
        o->arrow_my = a->motionY;
        o->arrow_mz = a->motionZ;
        if (a->motionX * a->motionX + a->motionY * a->motionY + a->motionZ * a->motionZ
            < 0.0010000000474974513) {
            a->alive = 0;
        } else {
            o->flags |= 2u; /* arrow_alive after bounce */
        }
        return;
    }

    McCombatArmor armor = mc_combat_armor_set(t->armor_idx);
    float final = mc_combat_final_damage(o->raw_damage, &armor);
    o->final_damage = final;

    if (final > 0.0f) {
        t->health -= final;
        if (t->health < 0.0f) t->health = 0.0f;
    }

    o->hp_after = t->health;
    o->flags |= 1u; /* hit_success */

    if (a->knockbackStrength > 0) {
        float f1 = pm_sqrt(a->motionX * a->motionX + a->motionZ * a->motionZ);
        if (f1 > 0.0f) {
            o->kb_x = (float)(a->motionX * (double)a->knockbackStrength *
                              0.6000000238418579 / (double)f1);
            o->kb_y = 0.1f;
            o->kb_z = (float)(a->motionZ * (double)a->knockbackStrength *
                              0.6000000238418579 / (double)f1);
            t->velX += (double)o->kb_x;
            t->velY += (double)o->kb_y;
            t->velZ += (double)o->kb_z;
        }
    }

    a->alive = 0;
    o->arrow_mx = a->motionX;
    o->arrow_my = a->motionY;
    o->arrow_mz = a->motionZ;
}

MC_HD static inline void peh_scenario_params(int idx, PehArrow *a, PehTarget *t) {
    a->alive = 1;
    a->critical = 0;
    a->knockbackStrength = 0;
    a->ticksInAir = 10;
    a->damage = 2.0;
    a->motionX = 3.0;
    a->motionY = 0.0;
    a->motionZ = 0.0;
    a->posX = 2.0;
    a->posY = 5.0;
    a->posZ = 8.0;

    t->posX = 8.0;
    t->posY = 5.0;
    t->posZ = 8.0;
    t->health = 20.0f;
    t->armor_idx = 0;
    t->velX = t->velY = t->velZ = 0.0;

    switch (idx) {
        case 0: t->armor_idx = 0; break;
        case 1: t->armor_idx = 1; break;
        case 2: t->armor_idx = 2; break;
        case 3: t->armor_idx = 3; break;
        case 4: t->armor_idx = 4; break;
        case 5: t->armor_idx = 5; break;
        case 6:
            a->critical = 1;
            t->armor_idx = 0;
            break;
        case 7:
            a->motionX = 5.5;
            a->motionY = 1.0;
            a->motionZ = 0.5;
            t->armor_idx = 3;
            break;
        case 8:
            a->knockbackStrength = 2;
            t->armor_idx = 0;
            break;
        case 9:
            t->health = 0.0f;
            break;
        case 10:
            a->posX = 2.5;
            a->posY = 5.0;
            a->posZ = 8.5;
            a->motionX = 8.0;
            a->motionY = 0.0;
            a->motionZ = 0.0;
            t->posX = 9.0;
            t->posY = 5.0;
            t->posZ = 8.5;
            t->armor_idx = 2;
            break;
        default: /* 11: low speed */
            a->motionX = 0.5;
            a->motionY = 0.0;
            a->motionZ = 0.0;
            t->armor_idx = 4;
            break;
    }
}

MC_HD static inline void peh_run_scenario(int idx, i64 seed, PehEmitFn emit, void *ctx) {
    PehArrow arrow;
    PehTarget target;
    PehHitOut out;

    peh_scenario_params(idx, &arrow, &target);

    if (idx == 10) {
        double hx, hy, hz;
        if (!peh_find_entity_on_path(&arrow, &target, &hx, &hy, &hz))
            return;
    }

    peh_on_hit_entity(seed, idx, &arrow, &target, &out);
    peh_emit_out(&out, emit, ctx);
}

MC_HD static inline void peh_run_all(i64 seed, PehEmitFn emit, void *ctx) {
    for (int i = 0; i < PEH_NUM_SCENARIOS; ++i)
        peh_run_scenario(i, seed, emit, ctx);
}

#endif /* MC_PROJECTILE_ENTITY_HIT_H */
