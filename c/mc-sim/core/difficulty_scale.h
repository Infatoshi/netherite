/* difficulty_scale: MC 1.11.2 player difficulty-scaled damage + regional difficulty + starve /
 * hunger-poison rates. Pre-armor only (do not fold combat_math absorb here).
 *
 * PORT TARGETS (vanilla 1.11.2):
 *   entity/player/EntityPlayer.java     attackEntityFrom difficultyScaled branch (1100-1118)
 *   world/EnumDifficulty.java           PEACEFUL=0 EASY=1 NORMAL=2 HARD=3
 *   world/DifficultyInstance.java       calculateAdditionalDifficulty, getClampedAdditionalDifficulty
 *   world/World.java                    getDifficultyForLocation (inputs only; world shell omitted)
 *   util/FoodStats.java                 starve gate (90-92) by EnumDifficulty
 *   potion/Potion.java                  HUNGER exhaustion rate + POISON isReady period
 *
 * CUT: armor/prot (combat_math), Forge, shields, i-frames. Build -ffp-contract=off / --fmad=false. */
#ifndef MC_DIFFICULTY_SCALE_H
#define MC_DIFFICULTY_SCALE_H

#include "mc.h"

/* EnumDifficulty difficultyId */
#define MC_DS_PEACEFUL 0
#define MC_DS_EASY     1
#define MC_DS_NORMAL   2
#define MC_DS_HARD     3

/* MathHelper.clamp(float) */
MC_HD static inline float ds_clampf(float num, float min, float max) {
    return num < min ? min : (num > max ? max : num);
}

/* EntityPlayer.attackEntityFrom when source.isDifficultyScaled():
 *   PEACEFUL -> 0
 *   EASY     -> min(amount/2 + 1, amount)
 *   NORMAL   -> amount
 *   HARD     -> amount * 3/2
 * Non-scaled sources return amount unchanged. regional_difficulty is accepted for API parity with
 * DifficultyInstance callers but is NOT used by the vanilla player scale path. */
MC_HD static inline float ds_scale_player_damage(float raw_damage, int difficulty_id,
                                                  int is_difficulty_scaled,
                                                  float regional_difficulty) {
    (void)regional_difficulty;
    float amount = raw_damage;
    if (!is_difficulty_scaled)
        return amount;
    if (difficulty_id == MC_DS_PEACEFUL)
        amount = 0.0F;
    if (difficulty_id == MC_DS_EASY) {
        float half_plus = amount / 2.0F + 1.0F;
        amount = half_plus < amount ? half_plus : amount; /* Math.min */
    }
    if (difficulty_id == MC_DS_HARD)
        amount = amount * 3.0F / 2.0F;
    return amount;
}

/* attackEntityFrom returns false when amount == 0 after scale (peaceful cancel). */
MC_HD static inline int ds_damage_applies(float scaled_amount) {
    return scaled_amount == 0.0F ? 0 : 1;
}

/* DifficultyInstance.calculateAdditionalDifficulty (28-51) verbatim operator order. */
MC_HD static inline float ds_additional_difficulty(int difficulty_id, i64 world_time,
                                                    i64 chunk_inhabited_time,
                                                    float moon_phase_factor) {
    if (difficulty_id == MC_DS_PEACEFUL)
        return 0.0F;
    {
        int flag = (difficulty_id == MC_DS_HARD) ? 1 : 0;
        float f = 0.75F;
        float f1 = ds_clampf(((float)world_time + -72000.0F) / 1440000.0F, 0.0F, 1.0F) * 0.25F;
        f = f + f1;
        float f2 = 0.0F;
        f2 = f2 + ds_clampf((float)chunk_inhabited_time / 3600000.0F, 0.0F, 1.0F)
                     * (flag ? 1.0F : 0.75F);
        f2 = f2 + ds_clampf(moon_phase_factor * 0.25F, 0.0F, f1);
        if (difficulty_id == MC_DS_EASY)
            f2 *= 0.5F;
        f = f + f2;
        return (float)difficulty_id * f;
    }
}

/* DifficultyInstance.getClampedAdditionalDifficulty (23-26). */
MC_HD static inline float ds_clamped_additional_difficulty(float additional) {
    return additional < 2.0F ? 0.0F
         : (additional > 4.0F ? 1.0F : (additional - 2.0F) / 2.0F);
}

/* FoodStats.onUpdate starve branch condition (90-92), amount is always 1.0F when true. */
MC_HD static inline int ds_starve_applies(float health, int difficulty_id) {
    /* PEACEFUL still evaluates the same boolean if foodLevel<=0 path is reached. */
    return (health > 10.0F
            || difficulty_id == MC_DS_HARD
            || (health > 1.0F && difficulty_id == MC_DS_NORMAL))
               ? 1
               : 0;
}

/* Potion HUNGER performEffect: addExhaustion(0.005F * (amplifier + 1)); isReady always. */
MC_HD static inline float ds_hunger_exhaustion(int amplifier) {
    return 0.005F * (float)(amplifier + 1);
}

/* Potion POISON isReady: j = 25 >> amplifier; j > 0 ? duration % j == 0 : true. */
MC_HD static inline int ds_poison_is_ready(int duration, int amplifier) {
    int j = 25 >> amplifier;
    return j > 0 ? ((duration % j) == 0 ? 1 : 0) : 1;
}

/* ---- deterministic battery --------------------------------------------------------------- */

#define MC_DS_NUM_RAW 12
#define MC_DS_NUM_DIFF 4
#define MC_DS_NUM_SCALED 2
#define MC_DS_NUM_DAMAGE (MC_DS_NUM_RAW * MC_DS_NUM_DIFF * MC_DS_NUM_SCALED)

#define MC_DS_NUM_REGIONAL 16
#define MC_DS_NUM_STARVE_HP 8
#define MC_DS_NUM_STARVE (MC_DS_NUM_STARVE_HP * MC_DS_NUM_DIFF)
#define MC_DS_NUM_HUNGER_AMP 5
#define MC_DS_NUM_POISON_DUR 10
#define MC_DS_NUM_POISON_AMP 4
#define MC_DS_NUM_POISON (MC_DS_NUM_POISON_DUR * MC_DS_NUM_POISON_AMP)

/* per damage scenario: scaled amount (f32) + applies flag (u32) */
#define MC_DS_LINES_DAMAGE 2
/* per regional: additional + clamped (2 f32) */
#define MC_DS_LINES_REGIONAL 2
#define MC_DS_LINES_STARVE 1
#define MC_DS_LINES_HUNGER 1
#define MC_DS_LINES_POISON 1

#define MC_DS_NOUT (MC_DS_NUM_DAMAGE * MC_DS_LINES_DAMAGE \
    + MC_DS_NUM_REGIONAL * MC_DS_LINES_REGIONAL \
    + MC_DS_NUM_STARVE * MC_DS_LINES_STARVE \
    + MC_DS_NUM_HUNGER_AMP * MC_DS_LINES_HUNGER \
    + MC_DS_NUM_POISON * MC_DS_LINES_POISON)

MC_HD static inline float ds_raw_damage(int idx) {
    static const float raw[MC_DS_NUM_RAW] = {
        0.0F, 0.5F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 8.0F, 10.0F, 15.0F, 20.0F, 50.0F
    };
    return raw[idx];
}

MC_HD static inline float ds_starve_health(int idx) {
    static const float hp[MC_DS_NUM_STARVE_HP] = {
        20.0F, 11.0F, 10.0F, 9.0F, 2.0F, 1.0F, 0.5F, 0.0F
    };
    return hp[idx];
}

/* Fixed regional-difficulty input table (worldTime, inhabited, moon) x difficulty. */
typedef struct {
    int difficulty_id;
    i64 world_time;
    i64 inhabited;
    float moon;
} DsRegionalIn;

MC_HD static inline DsRegionalIn ds_regional_scenario(int idx) {
    static const DsRegionalIn tbl[MC_DS_NUM_REGIONAL] = {
        /* PEACEFUL always 0 */
        { 0, 0LL, 0LL, 0.0F },
        { 0, 24000LL * 100, 3600000LL, 1.0F },
        /* EASY: early / late / inhabited / full moon */
        { 1, 0LL, 0LL, 0.0F },
        { 1, 72000LL, 0LL, 0.0F },
        { 1, 1440000LL, 0LL, 0.5F },
        { 1, 2880000LL, 3600000LL, 1.0F },
        /* NORMAL */
        { 2, 0LL, 0LL, 0.0F },
        { 2, 72000LL, 0LL, 0.0F },
        { 2, 720000LL, 1800000LL, 0.25F },
        { 2, 1440000LL, 3600000LL, 1.0F },
        { 2, 2880000LL, 7200000LL, 1.0F },
        /* HARD */
        { 3, 0LL, 0LL, 0.0F },
        { 3, 72000LL, 0LL, 0.0F },
        { 3, 720000LL, 900000LL, 0.5F },
        { 3, 1440000LL, 3600000LL, 1.0F },
        { 3, 2880000LL, 7200000LL, 1.0F }
    };
    return tbl[idx];
}

MC_HD static inline int ds_poison_duration(int idx) {
    static const int d[MC_DS_NUM_POISON_DUR] = {
        0, 1, 12, 25, 26, 50, 75, 100, 125, 200
    };
    return d[idx];
}

typedef void (*DsEmitFn)(u64 bits, void *ctx);

MC_HD static inline void ds_emit_u32(DsEmitFn emit, void *ctx, u32 v) {
    emit((u64)v, ctx);
}

MC_HD static inline void ds_emit_f32(DsEmitFn emit, void *ctx, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    emit((u64)u.u, ctx);
}

MC_HD static inline void ds_run_battery(DsEmitFn emit, void *ctx) {
    int ri, di, si, i, ai, pi;

    /* A: raw x difficulty x is_scaled -> scaled amount + applies */
    for (ri = 0; ri < MC_DS_NUM_RAW; ++ri) {
        float raw = ds_raw_damage(ri);
        for (di = 0; di < MC_DS_NUM_DIFF; ++di) {
            for (si = 0; si < MC_DS_NUM_SCALED; ++si) {
                /* regional unused on player path; pass 0 so the arg is exercised as zero */
                float scaled = ds_scale_player_damage(raw, di, si, 0.0F);
                ds_emit_f32(emit, ctx, scaled);
                ds_emit_u32(emit, ctx, (u32)ds_damage_applies(scaled));
            }
        }
    }

    /* B: regional additional + clamped */
    for (i = 0; i < MC_DS_NUM_REGIONAL; ++i) {
        DsRegionalIn s = ds_regional_scenario(i);
        float add = ds_additional_difficulty(s.difficulty_id, s.world_time, s.inhabited, s.moon);
        ds_emit_f32(emit, ctx, add);
        ds_emit_f32(emit, ctx, ds_clamped_additional_difficulty(add));
    }

    /* C: starve eligibility */
    for (ri = 0; ri < MC_DS_NUM_STARVE_HP; ++ri) {
        float hp = ds_starve_health(ri);
        for (di = 0; di < MC_DS_NUM_DIFF; ++di)
            ds_emit_u32(emit, ctx, (u32)ds_starve_applies(hp, di));
    }

    /* D: hunger exhaustion by amplifier */
    for (ai = 0; ai < MC_DS_NUM_HUNGER_AMP; ++ai)
        ds_emit_f32(emit, ctx, ds_hunger_exhaustion(ai));

    /* E: poison isReady duration x amp */
    for (pi = 0; pi < MC_DS_NUM_POISON_DUR; ++pi) {
        int dur = ds_poison_duration(pi);
        for (ai = 0; ai < MC_DS_NUM_POISON_AMP; ++ai)
            ds_emit_u32(emit, ctx, (u32)ds_poison_is_ready(dur, ai));
    }
}

#endif /* MC_DIFFICULTY_SCALE_H */
