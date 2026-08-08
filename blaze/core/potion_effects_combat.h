/* potion_effects_combat: resistance/absorption/fire in EntityLivingBase damage path (MC 1.11.2).
 *
 * PORT TARGETS:
 *   net/minecraft/entity/EntityLivingBase.java
 *     attackEntityFrom (fire resistance early return)
 *     applyArmorCalculations, applyPotionDamageCalculations, damageEntity
 *   net/minecraft/util/CombatRules.java getDamageAfterMagicAbsorb
 *
 * READ-ONLY deps: combat_math.h (mc_combat_damage_after_absorb, mc_combat_armor_set,
 * mc_combat_damage_after_magic_absorb). CUT: Forge onLivingHurt/ISpecialArmor, shields,
 * i-frames, combat tracker, anvil helmet 0.75x, EntityPlayer.damageEntity variant.
 * Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_POTION_EFFECTS_COMBAT_H
#define MC_POTION_EFFECTS_COMBAT_H

#include "mc.h"
#include "combat_math.h"

typedef struct {
    int is_absolute;
    int is_out_of_world;
    int resistance_active;
    int resistance_amp;
    int prot_sum;
    float damage_in;
} PecPotionScenario;

typedef struct {
    float raw;
    int armor_idx;
    int resistance_active;
    int resistance_amp;
    float absorption;
    float health;
    int is_absolute;
    int is_out_of_world;
} PecFullScenario;

/* verbatim applyPotionDamageCalculations (eval-pure: prot_sum injected) */
MC_HD static inline float pec_apply_potion_damage_calcs(int is_absolute, int is_out_of_world,
                                                         int resistance_active, int resistance_amp,
                                                         int prot_sum, float damage) {
    if (is_absolute)
        return damage;
    if (resistance_active && !is_out_of_world) {
        int i = (resistance_amp + 1) * 5;
        int j = 25 - i;
        float f = damage * (float)j;
        damage = f / 25.0F;
    }
    if (damage <= 0.0F)
        return 0.0F;
    if (prot_sum > 0)
        damage = mc_combat_damage_after_magic_absorb(damage, (float)prot_sum);
    return damage;
}

/* attackEntityFrom: fire + fire_resistance -> false (no damage) */
MC_HD static inline int pec_fire_resist_blocks(int is_fire, int fire_resist_active) {
    return (is_fire && fire_resist_active) ? 0 : 1;
}

MC_HD static inline float pec_apply_armor(float raw, int armor_pts, float toughness,
                                           int is_unblockable) {
    if (is_unblockable)
        return raw;
    return mc_combat_damage_after_absorb(raw, (float)armor_pts, toughness);
}

/* verbatim EntityLivingBase.damageEntity absorb + health (post armor+potion) */
MC_HD static inline void pec_damage_entity_absorb(float potion_damage, float *absorption,
                                                   float *health, float *health_damage_out) {
    float f = potion_damage;
    float dmg = potion_damage;
    if (dmg > *absorption)
        dmg -= *absorption;
    else
        dmg = 0.0F;
    {
        float na = *absorption - (f - dmg);
        *absorption = na < 0.0F ? 0.0F : na;
    }
    if (dmg != 0.0F) {
        /* EntityLivingBase.setHealth clamps to [0, maxHealth]; damage only reduces health
         * (start <= max) so the upper bound is a no-op here, but the 0 floor is faithful
         * (a lethal hit lands at 0, not a negative health). */
        float nh = *health - dmg;
        *health = nh < 0.0F ? 0.0F : nh;
        {
            float na = *absorption - dmg;
            *absorption = na < 0.0F ? 0.0F : na;
        }
    }
    *health_damage_out = dmg;
}

MC_HD static inline void pec_run_full(PecFullScenario s, float *potion_out,
                                       float *health_dmg_out, float *absorption_out,
                                       float *health_out) {
    McCombatArmor armor = mc_combat_armor_set(s.armor_idx);
    float after_armor = pec_apply_armor(s.raw, armor.armor_pts, armor.toughness, 0);
    float after_potion = pec_apply_potion_damage_calcs(
        s.is_absolute, s.is_out_of_world, s.resistance_active, s.resistance_amp,
        armor.prot_sum, after_armor);
    *potion_out = after_potion;
    float abs_amt = s.absorption;
    float hp = s.health;
    pec_damage_entity_absorb(after_potion, &abs_amt, &hp, health_dmg_out);
    *absorption_out = abs_amt;
    *health_out = hp;
}

#define MC_PEC_NUM_RAW 6
#define MC_PEC_NUM_ARMOR MC_CM_NUM_ARMORS
#define MC_PEC_NUM_RESIST 5
#define MC_PEC_NUM_MATRIX (MC_PEC_NUM_RAW * MC_PEC_NUM_ARMOR * MC_PEC_NUM_RESIST)

#define MC_PEC_NUM_POTION_EDGE 12
#define MC_PEC_NUM_FIRE 8
#define MC_PEC_NUM_ABSORB 10

MC_HD static inline float pec_raw_damage(int idx) {
    static const float raw[] = { 1.0F, 5.0F, 10.0F, 20.0F, 50.0F, 100.0F };
    return raw[idx];
}

MC_HD static inline int pec_resist_amp(int idx) {
    static const int amp[] = { -1, 0, 1, 2, 4 };
    return amp[idx];
}

MC_HD static inline PecPotionScenario pec_potion_edge_scenario(int idx) {
    static const PecPotionScenario tbl[MC_PEC_NUM_POTION_EDGE] = {
        { 0, 0, 0, 0, 0, 10.0F },
        { 0, 0, 1, 0, 0, 10.0F },
        { 0, 0, 1, 1, 0, 10.0F },
        { 0, 0, 1, 4, 0, 100.0F },
        { 0, 1, 1, 0, 0, 20.0F },
        { 1, 0, 1, 4, 0, 50.0F },
        { 0, 0, 1, 2, 16, 30.0F },
        { 0, 0, 0, 0, 16, 30.0F },
        { 0, 0, 1, 0, 16, 5.0F },
        { 0, 0, 1, 4, 16, 200.0F },
        { 0, 0, 1, 0, 0, 0.0F },
        { 0, 0, 1, 4, 0, 1.0F }
    };
    return tbl[idx];
}

MC_HD static inline PecFullScenario pec_absorb_scenario(int idx) {
    static const PecFullScenario tbl[MC_PEC_NUM_ABSORB] = {
        { 10.0F, 3, 0, 0, 0.0F, 20.0F, 0, 0 },
        { 10.0F, 3, 0, 0, 4.0F, 20.0F, 0, 0 },
        { 10.0F, 3, 0, 0, 10.0F, 20.0F, 0, 0 },
        { 3.0F, 0, 0, 0, 4.0F, 20.0F, 0, 0 },
        { 4.0F, 0, 0, 0, 4.0F, 20.0F, 0, 0 },
        { 10.0F, 4, 1, 1, 8.0F, 20.0F, 0, 0 },
        { 80.0F, 4, 0, 0, 16.0F, 20.0F, 0, 0 },
        { 50.0F, 5, 1, 2, 10.0F, 20.0F, 0, 0 },
        { 20.0F, 2, 1, 0, 0.0F, 20.0F, 0, 0 },
        { 100.0F, 0, 0, 0, 20.0F, 20.0F, 1, 0 }
    };
    return tbl[idx];
}

#define MC_PEC_LINES_MATRIX 1
#define MC_PEC_LINES_ABSORB 4
#define MC_PEC_NOUT (MC_PEC_NUM_POTION_EDGE + MC_PEC_NUM_FIRE \
    + MC_PEC_NUM_MATRIX * MC_PEC_LINES_MATRIX \
    + MC_PEC_NUM_ABSORB * MC_PEC_LINES_ABSORB)

typedef void (*PecEmitFn)(u64 bits, void *ctx);

MC_HD static inline void pec_emit_u32(PecEmitFn emit, void *ctx, u32 v) {
    emit((u64)v, ctx);
}

MC_HD static inline void pec_emit_f32(PecEmitFn emit, void *ctx, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    emit((u64)u.u, ctx);
}

MC_HD static inline void pec_run_battery(PecEmitFn emit, void *ctx) {
    int i, ri, ai, raw_i;

    for (i = 0; i < MC_PEC_NUM_POTION_EDGE; ++i) {
        PecPotionScenario s = pec_potion_edge_scenario(i);
        pec_emit_f32(emit, ctx, pec_apply_potion_damage_calcs(
            s.is_absolute, s.is_out_of_world, s.resistance_active, s.resistance_amp,
            s.prot_sum, s.damage_in));
    }

    for (i = 0; i < MC_PEC_NUM_FIRE; ++i) {
        int is_fire = i & 1;
        int fire_resist = (i >> 1) & 1;
        pec_emit_u32(emit, ctx, (u32)pec_fire_resist_blocks(is_fire, fire_resist));
    }

    for (raw_i = 0; raw_i < MC_PEC_NUM_RAW; ++raw_i) {
        for (ai = 0; ai < MC_PEC_NUM_ARMOR; ++ai) {
            for (ri = 0; ri < MC_PEC_NUM_RESIST; ++ri) {
                McCombatArmor armor = mc_combat_armor_set(ai);
                float after_armor = pec_apply_armor(
                    pec_raw_damage(raw_i), armor.armor_pts, armor.toughness, 0);
                int amp = pec_resist_amp(ri);
                int resist_on = (amp >= 0) ? 1 : 0;
                int use_amp = (amp >= 0) ? amp : 0;
                pec_emit_f32(emit, ctx, pec_apply_potion_damage_calcs(
                    0, 0, resist_on, use_amp, armor.prot_sum, after_armor));
            }
        }
    }

    for (i = 0; i < MC_PEC_NUM_ABSORB; ++i) {
        PecFullScenario s = pec_absorb_scenario(i);
        float potion_out, health_dmg, abs_out, health_out;
        pec_run_full(s, &potion_out, &health_dmg, &abs_out, &health_out);
        pec_emit_f32(emit, ctx, potion_out);
        pec_emit_f32(emit, ctx, health_dmg);
        pec_emit_f32(emit, ctx, abs_out);
        pec_emit_f32(emit, ctx, health_out);
    }
}

#endif /* MC_POTION_EFFECTS_COMBAT_H */
