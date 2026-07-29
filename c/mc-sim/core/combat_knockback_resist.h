/* combat_knockback_resist: knockBack + setBeenAttacked resistance gate + CombatRules
 * toughness edge cases (MC 1.11.2 EntityLivingBase / CombatRules).
 *
 * PORT TARGETS:
 *   net/minecraft/entity/EntityLivingBase.java knockBack, setBeenAttacked
 *   net/minecraft/util/CombatRules.java getDamageAfterAbsorb
 *   net/minecraft/util/math/MathHelper.java sqrt(double)
 *
 * READ-ONLY deps: combat_math.h (mc_combat_damage_after_absorb), items_tools_armor.h
 * (ita_armor_set_points / ita_armor_set_toughness for full-set integration rows).
 * Deterministic battery: knockback RNG draw is injected per scenario (eval-pure golden).
 * Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_COMBAT_KNOCKBACK_RESIST_H
#define MC_COMBAT_KNOCKBACK_RESIST_H

#include <math.h>
#include "mc.h"
#include "combat_math.h"
#include "items_tools_armor.h"

typedef struct {
    double motionX, motionY, motionZ;
    int onGround;
} McCkrEntity;

typedef struct {
    float strength;
    double x_ratio, z_ratio;
    double kb_resist, rng_draw;
    double motionX, motionY, motionZ;
    int onGround;
} McCkrKbScenario;

typedef struct {
    double kb_resist, rng_draw;
} McCkrVcScenario;

typedef struct {
    float damage, armor, toughness;
} McCkrToughScenario;

/* MathHelper.sqrt(double) -> (float)Math.sqrt */
MC_HD static inline float mc_ckr_sqrt(double v) {
    return (float)sqrt(v);
}

/* verbatim EntityLivingBase.knockBack with injected rand.nextDouble() */
MC_HD static inline int mc_ckr_knockback(McCkrEntity *e, float strength,
                                          double x_ratio, double z_ratio,
                                          double kb_resist, double rng_draw) {
    if (rng_draw < kb_resist)
        return 0;
    float f = mc_ckr_sqrt(x_ratio * x_ratio + z_ratio * z_ratio);
    e->motionX /= 2.0;
    e->motionZ /= 2.0;
    e->motionX -= x_ratio / (double)f * (double)strength;
    e->motionZ -= z_ratio / (double)f * (double)strength;
    if (e->onGround) {
        e->motionY /= 2.0;
        e->motionY += (double)strength;
        if (e->motionY > 0.4000000059604645)
            e->motionY = 0.4000000059604645;
    }
    return 1;
}

/* verbatim setBeenAttacked velocityChanged gate */
MC_HD static inline int mc_ckr_velocity_changed(double kb_resist, double rng_draw) {
    return rng_draw >= kb_resist ? 1 : 0;
}

#define MC_CKR_NUM_KB 12
#define MC_CKR_NUM_VC 6
#define MC_CKR_NUM_TOUGH 18

MC_HD static inline McCkrKbScenario mc_ckr_kb_scenario(int idx) {
    static const McCkrKbScenario tbl[MC_CKR_NUM_KB] = {
        /* 0 basic +X on ground */
        { 0.5f, 1.0, 0.0, 0.0, 0.25, 0.0, 0.0, 0.0, 1 },
        /* 1 resisted (iron golem resist=1) */
        { 0.5f, 1.0, 0.0, 1.0, 0.99, 0.0, 0.0, 0.0, 1 },
        /* 2 boundary apply: rng == resist */
        { 0.4f, 0.0, 1.0, 0.5, 0.5, 1.0, 0.2, 0.1, 1 },
        /* 3 boundary resist: rng just below */
        { 0.4f, 0.0, 1.0, 0.5, 0.49999999999999994, 1.0, 0.2, 0.1, 1 },
        /* 4 airborne: no Y branch */
        { 1.0f, 1.0, 1.0, 0.0, 0.1, 0.5, 0.3, 0.0, 0 },
        /* 5 motionY cap after halve+add */
        { 1.0f, 1.0, 0.0, 0.0, 0.0, 0.0, 0.35, 0.0, 1 },
        /* 6 motionY already above cap before kb */
        { 0.5f, -1.0, 0.0, 0.0, 0.0, 0.0, 0.9, 0.0, 1 },
        /* 7 3-4-5 normalized direction */
        { 0.8f, 3.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1 },
        /* 8 negative ratios */
        { 0.6f, -2.0, -1.0, 0.25, 0.8, 0.1, 0.0, 0.1, 1 },
        /* 9 pure-Z ratio (x=0; avoids sqrt(0) NaN - non-portable on CUDA) */
        { 0.5f, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1 },
        /* 10 sprint knockback strength */
        { 1.5f, 0.0, -1.0, 0.0, 0.5, -0.2, 0.0, 0.4, 1 },
        /* 11 partial resist 0.2 */
        { 0.4f, 1.0, 0.0, 0.2, 0.15, 0.0, 0.0, 0.0, 0 }
    };
    return tbl[idx];
}

MC_HD static inline McCkrVcScenario mc_ckr_vc_scenario(int idx) {
    static const McCkrVcScenario tbl[MC_CKR_NUM_VC] = {
        { 0.0, 0.0 },
        { 0.0, 0.9999999999999999 },
        { 1.0, 0.5 },
        { 0.5, 0.5 },
        { 0.5, 0.49999999999999994 },
        { 0.75, 0.75 }
    };
    return tbl[idx];
}

MC_HD static inline McCkrToughScenario mc_ckr_tough_scenario(int idx) {
    static const McCkrToughScenario tbl[MC_CKR_NUM_TOUGH] = {
        { 0.0f, 20.0f, 8.0f },
        { 1.0f, 0.0f, 0.0f },
        { 20.0f, 0.0f, 0.0f },
        { 20.0f, 15.0f, 0.0f },
        { 20.0f, 20.0f, 0.0f },
        { 20.0f, 20.0f, 8.0f },
        { 100.0f, 20.0f, 0.0f },
        { 100.0f, 20.0f, 8.0f },
        { 50.0f, 5.0f, 0.0f },
        { 200.0f, 20.0f, 8.0f },
        { 10.0f, 20.0f, 20.0f },
        { 4.0f, 20.0f, 8.0f },
        { 80.0f, 20.0f, 8.0f },
        { 1.0f, 20.0f, 8.0f },
        { 999.0f, 20.0f, 8.0f },
        { 6.6666665f, 12.0f, 3.0f },
        { 3.1415927f, 7.0f, 2.0f },
        { 0.5f, 1.0f, 0.0f }
    };
    return tbl[idx];
}

#define MC_CKR_LINES_PER_KB 4
#define MC_CKR_NOUT (MC_CKR_NUM_KB * MC_CKR_LINES_PER_KB + MC_CKR_NUM_VC + MC_CKR_NUM_TOUGH + 3)

typedef void (*McCkrEmitFn)(u64 bits, void *ctx);

MC_HD static inline void mc_ckr_emit_u32(McCkrEmitFn emit, void *ctx, u32 v) {
    emit((u64)v, ctx);
}

MC_HD static inline void mc_ckr_emit_f32(McCkrEmitFn emit, void *ctx, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    emit((u64)u.u, ctx);
}

MC_HD static inline void mc_ckr_emit_f64(McCkrEmitFn emit, void *ctx, double v) {
    union { double d; u64 u; } u;
    u.d = v;
    emit(u.u, ctx);
}

MC_HD static inline void mc_ckr_run_battery(McCkrEmitFn emit, void *ctx) {
    for (int i = 0; i < MC_CKR_NUM_KB; ++i) {
        McCkrKbScenario s = mc_ckr_kb_scenario(i);
        McCkrEntity e;
        e.motionX = s.motionX;
        e.motionY = s.motionY;
        e.motionZ = s.motionZ;
        e.onGround = s.onGround;
        int applied = mc_ckr_knockback(&e, s.strength, s.x_ratio, s.z_ratio,
                                       s.kb_resist, s.rng_draw);
        mc_ckr_emit_u32(emit, ctx, (u32)applied);
        mc_ckr_emit_f64(emit, ctx, e.motionX);
        mc_ckr_emit_f64(emit, ctx, e.motionY);
        mc_ckr_emit_f64(emit, ctx, e.motionZ);
    }
    for (int i = 0; i < MC_CKR_NUM_VC; ++i) {
        McCkrVcScenario s = mc_ckr_vc_scenario(i);
        mc_ckr_emit_u32(emit, ctx, (u32)mc_ckr_velocity_changed(s.kb_resist, s.rng_draw));
    }
    for (int i = 0; i < MC_CKR_NUM_TOUGH; ++i) {
        McCkrToughScenario s = mc_ckr_tough_scenario(i);
        mc_ckr_emit_f32(emit, ctx,
            mc_combat_damage_after_absorb(s.damage, s.armor, s.toughness));
    }
    /* items_tools_armor integration: iron + diamond full sets at high damage */
    {
        ITAStack iron[4] = { ita_mk(306, 0), ita_mk(307, 0), ita_mk(308, 0), ita_mk(309, 0) };
        float ap = (float)ita_armor_set_points(iron);
        mc_ckr_emit_f32(emit, ctx, mc_combat_damage_after_absorb(80.0f, ap, 0.0f));
    }
    {
        ITAStack dia[4] = { ita_mk(310, 0), ita_mk(311, 0), ita_mk(312, 0), ita_mk(313, 0) };
        float ap = (float)ita_armor_set_points(dia);
        float th = ita_armor_set_toughness(dia);
        mc_ckr_emit_f32(emit, ctx, mc_combat_damage_after_absorb(80.0f, ap, th));
    }
    {
        ITAStack dia[4] = { ita_mk(310, 0), ita_mk(311, 0), ita_mk(312, 0), ita_mk(313, 0) };
        mc_ckr_emit_u32(emit, ctx, (u32)ita_armor_set_points(dia));
    }
}

#endif /* MC_COMBAT_KNOCKBACK_RESIST_H */
