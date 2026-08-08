/* Exact bounded EntityWitch.onLivingUpdate self-potion state machine. */
#ifndef MC_ENTITY_WITCH_H
#define MC_ENTITY_WITCH_H

#include "mc_rng.h"

enum {
    EWITCH_SELF_NONE = 0,
    EWITCH_SELF_WATER_BREATHING = 1,
    EWITCH_SELF_FIRE_RESISTANCE = 2,
    EWITCH_SELF_HEALING = 3,
    EWITCH_SELF_SWIFTNESS = 4,
};

enum {
    EWITCH_THROW_NONE = 0,
    EWITCH_THROW_HARMING = 1,
    EWITCH_THROW_SLOWNESS = 2,
    EWITCH_THROW_POISON = 3,
    EWITCH_THROW_WEAKNESS = 4,
};

typedef struct {
    int drinking;
    int timer;
    int potion;
} EwitchSelfState;

typedef struct {
    int in_water;
    int burning;
    int water_breathing;
    int fire_resistance;
    int speed;
    int has_target;
    double target_distance_sq;
    float health;
    float max_health;
} EwitchSelfConditions;

typedef struct {
    int started;
    int completed;
    int effect_id;
    int effect_duration;
    int effect_amplifier;
    int status_particles;
    float drink_pitch;
} EwitchSelfOutcome;

typedef struct {
    int count;
    int value[12];
    u64 seed48[12];
} EwitchRngTrace;

enum { EWITCH_LOOT_MAX = 3 };

typedef struct {
    int count;
    int item[EWITCH_LOOT_MAX];
    int quantity[EWITCH_LOOT_MAX];
} EwitchLootOutcome;

typedef struct {
    int drinking;
    int target_slowness;
    int target_poison;
    int target_weakness;
    double witch_x, witch_y, witch_z;
    double target_x, target_y, target_z;
    double target_motion_x, target_motion_z;
    float target_eye_height;
    float target_health;
} EwitchRangedConditions;

typedef struct {
    int thrown;
    int potion;
    double spawn_x, spawn_y, spawn_z;
    double aim_x, aim_y, aim_z;
    float horizontal_distance;
    float sound_pitch;
} EwitchRangedOutcome;

MC_HD static inline float ewitch_next_float(
        JavaRandom *random, EwitchRngTrace *trace) {
    int value = jrand_next(random, 24);
    if (trace && trace->count < 12) {
        int index = trace->count++;
        trace->value[index] = value;
        trace->seed48[index] = random->seed;
    }
    return (float)value / 16777216.0F;
}

/* entities/witch.json in 1.11.2: one pool, 1..3 rolls, total weight eight.
 * Every selected entry applies SetCount(0..2), then LootingEnchantBonus(0..1).
 * Zero-count ItemStacks are discarded; separate rolls remain separate stacks. */
MC_HD static inline void ewitch_generate_loot(
        JavaRandom *random, int looting_level, EwitchLootOutcome *outcome) {
    *outcome = (EwitchLootOutcome){0};
    if (!random || looting_level < 0) return;
    int rolls = 1 + jrand_int_bound(random, 3);
    for (int roll = 0; roll < rolls; ++roll) {
        int choice = jrand_int_bound(random, 8);
        int item = choice == 0 ? 348 : choice == 1 ? 353
            : choice == 2 ? 331 : choice == 3 ? 375
            : choice == 4 ? 374 : choice == 5 ? 289 : 280;
        int count = jrand_int_bound(random, 3);
        if (looting_level > 0) {
            float bonus = (float)looting_level * jrand_float(random);
            count += (int)floorf(bonus + 0.5F);
        }
        if (count > 0 && outcome->count < EWITCH_LOOT_MAX) {
            int index = outcome->count++;
            outcome->item[index] = item;
            outcome->quantity[index] = count;
        }
    }
}

/* EntityLiving.dropEquipment's main-hand roll. The subtraction is float in
 * Java and inventoryHandsDropChances stores the float 0.085F, widened only
 * for the final comparison. */
MC_HD static inline int ewitch_equipped_drop(
        JavaRandom *random, int looting_level) {
    if (!random || looting_level < 0) return 0;
    float roll = jrand_float(random) - (float)looting_level * 0.01F;
    return (double)roll < (double)0.085F;
}

/* EntityLiving.getExperiencePoints adds one bounded draw for each equipped
 * hand/armor stack whose drop chance is at most one. A drinking Witch owns
 * exactly one such main-hand potion in the represented state. */
MC_HD static inline int ewitch_experience_points(
        JavaRandom *random, int equipped) {
    return 5 + (equipped ? 1 + jrand_int_bound(random, 3) : 0);
}

MC_HD static inline void ewitch_self_potion_step(
        JavaRandom *random, EwitchSelfState *state,
        const EwitchSelfConditions *conditions,
        EwitchSelfOutcome *outcome, EwitchRngTrace *trace) {
    int potion = EWITCH_SELF_NONE;
    *outcome = (EwitchSelfOutcome){0};
    if (trace) *trace = (EwitchRngTrace){0};
    if (state->drinking) {
        int timer = state->timer--;
        if (timer <= 0) {
            outcome->completed = 1;
            if (state->potion == EWITCH_SELF_WATER_BREATHING) {
                outcome->effect_id = 13;
                outcome->effect_duration = 3600;
            } else if (state->potion == EWITCH_SELF_FIRE_RESISTANCE) {
                outcome->effect_id = 12;
                outcome->effect_duration = 3600;
            } else if (state->potion == EWITCH_SELF_HEALING) {
                outcome->effect_id = 6;
                outcome->effect_duration = 1;
            } else if (state->potion == EWITCH_SELF_SWIFTNESS) {
                outcome->effect_id = 1;
                outcome->effect_duration = 3600;
            }
            outcome->effect_amplifier = 0;
            state->drinking = 0;
            state->potion = EWITCH_SELF_NONE;
        }
    } else {
        if (ewitch_next_float(random, trace) < 0.15F
                && conditions->in_water && !conditions->water_breathing) {
            potion = EWITCH_SELF_WATER_BREATHING;
        } else if (ewitch_next_float(random, trace) < 0.15F
                && conditions->burning && !conditions->fire_resistance) {
            potion = EWITCH_SELF_FIRE_RESISTANCE;
        } else if (ewitch_next_float(random, trace) < 0.05F
                && conditions->health < conditions->max_health) {
            potion = EWITCH_SELF_HEALING;
        } else if (ewitch_next_float(random, trace) < 0.5F
                && conditions->has_target && !conditions->speed
                && conditions->target_distance_sq > 121.0) {
            potion = EWITCH_SELF_SWIFTNESS;
        }
        if (potion != EWITCH_SELF_NONE) {
            state->drinking = 1;
            state->timer = 32;
            state->potion = potion;
            outcome->started = 1;
            outcome->drink_pitch = 0.8F
                + ewitch_next_float(random, trace) * 0.4F;
        }
    }
    outcome->status_particles =
        ewitch_next_float(random, trace) < 7.5E-4F;
}

/* EntityWitch.attackEntityWithRangedAttack up to EntityPotion construction.
 * The new projectile owns a separate Random; callers apply
 * EntityThrowable.setThrowableHeading to aim_* with velocity .75 and
 * inaccuracy 8 after allocating that projectile's entity ID. */
MC_HD static inline void ewitch_ranged_attack(
        JavaRandom *witch_random, const EwitchRangedConditions *conditions,
        EwitchRangedOutcome *outcome, EwitchRngTrace *trace) {
    *outcome = (EwitchRangedOutcome){0};
    if (trace) *trace = (EwitchRngTrace){0};
    if (conditions->drinking) return;

    double y = conditions->target_y
        + (double)conditions->target_eye_height
        - 1.100000023841858;
    double x = conditions->target_x + conditions->target_motion_x
        - conditions->witch_x;
    y -= conditions->witch_y;
    double z = conditions->target_z + conditions->target_motion_z
        - conditions->witch_z;
    float horizontal = (float)sqrt(x * x + z * z);
    int potion = EWITCH_THROW_HARMING;
    if (horizontal >= 8.0F && !conditions->target_slowness) {
        potion = EWITCH_THROW_SLOWNESS;
    } else if (conditions->target_health >= 8.0F
            && !conditions->target_poison) {
        potion = EWITCH_THROW_POISON;
    } else if (horizontal <= 3.0F && !conditions->target_weakness
            && ewitch_next_float(witch_random, trace) < 0.25F) {
        potion = EWITCH_THROW_WEAKNESS;
    }

    outcome->thrown = 1;
    outcome->potion = potion;
    outcome->spawn_x = conditions->witch_x;
    outcome->spawn_y = conditions->witch_y
        + (double)1.62F - 0.10000000149011612;
    outcome->spawn_z = conditions->witch_z;
    outcome->aim_x = x;
    outcome->aim_y = y + (double)(horizontal * 0.2F);
    outcome->aim_z = z;
    outcome->horizontal_distance = horizontal;
    outcome->sound_pitch = 0.8F
        + ewitch_next_float(witch_random, trace) * 0.4F;
}

#endif
