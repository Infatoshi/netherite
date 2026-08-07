/* potion_throwable.h - vanilla 1.11.2 lingering-cloud scalar lifecycle.
 *
 * Spatial entity selection stays with the product runtime. This shared state
 * machine owns EntityAreaEffectCloud's exact age, wait, radius shrink, five-
 * tick scan, reapplication delay, and radius-on-use transitions.
 */
#ifndef MC_POTION_THROWABLE_H
#define MC_POTION_THROWABLE_H

#include "mc.h"

typedef struct {
    int active;
    int age;
    int duration;
    int wait_time;
    int reapplication_delay;
    int next_application;
    float radius;
    float radius_on_use;
    float radius_per_tick;
} PtAreaEffectCloud;

typedef struct {
    int id;
    int duration;
    int amplifier;
} PtMobEffect;

/* PotionEffect.combine for two effects with the same Potion key. */
MC_HD static inline void pt_effect_combine(
        PtMobEffect *effect, int duration, int amplifier) {
    if (amplifier > effect->amplifier) {
        effect->amplifier = amplifier;
        effect->duration = duration;
    } else if (amplifier == effect->amplifier
            && duration > effect->duration) {
        effect->duration = duration;
    }
}

/* Potion.isReady. Java masks the right-shift count to five bits. */
MC_HD static inline int pt_effect_is_ready(
        int potion_id, int duration, int amplifier) {
    int interval;
    if (potion_id == 10) interval = 50 >> (amplifier & 31);
    else if (potion_id == 19) interval = 25 >> (amplifier & 31);
    else if (potion_id == 20) interval = 40 >> (amplifier & 31);
    else return potion_id == 17;
    return interval > 0 ? duration % interval == 0 : 1;
}

MC_HD static inline int pt_splash_effect_duration(
        int duration, double factor) {
    return (int)(factor * (double)duration + 0.5);
}

/* Potion attribute modifiers. Movement uses operation 2 (multiply total),
 * Strength/Weakness use operation 0, and jump is a direct float impulse. */
MC_HD static inline double pt_effect_movement_multiplier(
        int potion_id, int amplifier) {
    if (potion_id == 1)
        return 1.0 + 0.20000000298023224 * (double)(amplifier + 1);
    if (potion_id == 2)
        return 1.0 - 0.15000000596046448 * (double)(amplifier + 1);
    return 1.0;
}

MC_HD static inline double pt_effect_attack_bonus(
        int potion_id, int amplifier) {
    if (potion_id == 5) return 3.0 * (double)(amplifier + 1);
    if (potion_id == 18) return -4.0 * (double)(amplifier + 1);
    return 0.0;
}

MC_HD static inline float pt_effect_jump_bonus(int amplifier) {
    return (float)(amplifier + 1) * 0.1F;
}

/* EntityLivingBase.applyPotionDamageCalculations. The integer scale is
 * evaluated first, followed by two float operations. */
MC_HD static inline float pt_effect_resistance_damage(
        float damage, int amplifier) {
    int scale = 25 - (amplifier + 1) * 5;
    float reduced = damage * (float)scale / 25.0F;
    return reduced > 0.0F ? reduced : 0.0F;
}

MC_HD static inline double pt_effect_levitation_motion(
        double motion_y, int amplifier) {
    double target = 0.05 * (double)(amplifier + 1);
    return (motion_y + (target - motion_y) * 0.2)
        * 0.9800000190734863;
}

MC_HD static inline float pt_effect_health_boost(
        float base_health, int amplifier) {
    return base_health + 4.0F * (float)(amplifier + 1);
}

/* EntityLivingBase.damageEntity absorption step. The amount is consumed
 * after armor and potion calculations and before health. */
MC_HD static inline float pt_effect_absorb_damage(
        float damage, float *absorption) {
    float available = absorption && *absorption > 0.0F
        ? *absorption : 0.0F;
    float health_damage = damage > available ? damage - available : 0.0F;
    if (absorption) {
        *absorption = available - (damage - health_damage);
        if (*absorption < 0.0F) *absorption = 0.0F;
    }
    return health_damage;
}

/* EntityLivingBase.onEntityUpdate air counter. The caller owns the eye-water
 * material test and the 48 bubble-particle RNG draws on a drown pulse. */
MC_HD static inline int pt_effect_air_step(
        int air, int eye_in_water, int water_breathing, int *drown_pulse) {
    if (drown_pulse) *drown_pulse = 0;
    if (!eye_in_water) return 300;
    if (water_breathing) return air;
    --air;
    if (air == -20) {
        if (drown_pulse) *drown_pulse = 1;
        return 0;
    }
    return air;
}

/* Potion.affectEntity for instant health/damage. Positive heals, negative
 * damages. The undead reversal and +0.5 truncation are vanilla-exact. */
MC_HD static inline int pt_instant_health_delta(
        int potion_id, int amplifier, double factor, int undead) {
    int healing;
    int amount;
    if (potion_id != 6 && potion_id != 7) return 0;
    healing = (potion_id == 6 && !undead)
        || (potion_id == 7 && undead);
    amount = (int)(factor * (double)((healing ? 4 : 6) << amplifier)
        + 0.5);
    return healing ? amount : -amount;
}

MC_HD static inline void pt_cloud_init(PtAreaEffectCloud *cloud) {
    cloud->active = 1;
    cloud->age = 0;
    cloud->duration = 600;
    cloud->wait_time = 10;
    cloud->reapplication_delay = 20;
    cloud->next_application = 0;
    cloud->radius = 3.0F;
    cloud->radius_on_use = -0.5F;
    cloud->radius_per_tick = -cloud->radius / (float)cloud->duration;
}

/* Returns 1 only when the server performs its five-tick living-entity scan. */
MC_HD static inline int pt_cloud_tick(PtAreaEffectCloud *cloud) {
    if (!cloud->active) return 0;
    ++cloud->age;
    if (cloud->age >= cloud->wait_time + cloud->duration) {
        cloud->active = 0;
        return 0;
    }
    if (cloud->age < cloud->wait_time) return 0;
    cloud->radius += cloud->radius_per_tick;
    if (cloud->radius < 0.5F) {
        cloud->active = 0;
        return 0;
    }
    return cloud->age % 5 == 0;
}

MC_HD static inline int pt_cloud_target_ready(
        const PtAreaEffectCloud *cloud) {
    return cloud->active && cloud->age >= cloud->next_application;
}

/* Call after one selected target receives all cloud effects. Each entity owns
 * its reapplication deadline; radius-on-use remains cloud-global. */
MC_HD static inline void pt_cloud_apply_target(
        PtAreaEffectCloud *cloud, int *next_application) {
    *next_application = cloud->age + cloud->reapplication_delay;
    cloud->radius += cloud->radius_on_use;
    if (cloud->radius < 0.5F)
        cloud->active = 0;
}

MC_HD static inline void pt_cloud_apply(PtAreaEffectCloud *cloud) {
    pt_cloud_apply_target(cloud, &cloud->next_application);
}

#endif /* MC_POTION_THROWABLE_H */
