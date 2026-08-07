/* player_vitals: vanilla-faithful FoodStats hunger/saturation/exhaustion + natural regen + starve
 * + fall damage, ported bit-for-bit from decompiled MC 1.11.2. One MC_HD source, CPU==CUDA, and a
 * verbatim-Java golden (oracle/goldens/player_vitals/Golden.java) driven by the same tape.
 *
 * Sources (java/oracle-src/net/minecraft):
 *   util/FoodStats.java             onUpdate (foodLevel=20, saturation=5, exhaustion, foodTimer),
 *                                   addExhaustion (clamp 40), addStats.
 *   entity/EntityLivingBase.java    heal (910-920), setHealth clamp [0,max] (927-929),
 *                                   fall -> ceil((dist-3-jumpBoost)*mult) FALL damage (1389-1402).
 *   entity/player/EntityPlayer.java shouldHeal = health>0 && health<max (2244-2246).
 *   util/math/MathHelper.java       ceil (107-111).
 *
 * FIXED DIFFICULTY = NORMAL, naturalRegeneration = true (vanilla defaults).
 *   - onUpdate difficulty gate `enumdifficulty != PEACEFUL` is TRUE on NORMAL (FoodStats.java:53).
 *   - Starve floor on NORMAL: damage only if `health > 10 || HARD || (health > 1 && NORMAL)`,
 *     i.e. on NORMAL starvation stops at 1.0 HP (FoodStats.java:90-92). Floor = 1.0F.
 *
 * Simplifications (documented, faithful within scope): no feather-fall and
 * damageMultiplier=1.0F. The effect-aware fall path accepts the active Jump
 * Boost amplifier, while the legacy wrapper uses no effect. Damage
 * (STARVE/FALL) is applied directly to health with the setHealth clamp; armor,
 * absorption, and hurt-resistance cooldown are out of scope for a vitals
 * oracle. maxHealth defaults to 20 and may be replaced by the runtime's
 * active attribute value.
 *
 * All arithmetic is int/float exactly as Java, left-to-right, -ffp-contract=off / --fmad=false. */
#ifndef MC_PLAYER_VITALS_H
#define MC_PLAYER_VITALS_H

#include "mc.h"
#include "mc_gamerules.h"   /* naturalRegeneration gate (FoodStats.onUpdate flag) */

#define PV_MAX_HEALTH   20.0f
/* Starve floor on NORMAL difficulty (FoodStats.java:90-92). */
#define PV_STARVE_FLOOR 1.0f

typedef struct {
    i32   foodLevel;    /* FoodStats.foodLevel,           start 20 */
    float saturation;   /* FoodStats.foodSaturationLevel, start 5.0 */
    float exhaustion;   /* FoodStats.foodExhaustionLevel, start 0.0 */
    i32   foodTimer;    /* FoodStats.foodTimer,           start 0  */
    float health;       /* EntityLivingBase HEALTH,       start 20.0 */
    float maxHealth;    /* MAX_HEALTH attribute,           start 20.0 */
} PvStats;

MC_HD static inline void pv_init(PvStats *s) {
    s->foodLevel  = 20;
    s->saturation = 5.0f;
    s->exhaustion = 0.0f;
    s->foodTimer  = 0;
    s->health     = PV_MAX_HEALTH;
    s->maxHealth  = PV_MAX_HEALTH;
}

/* FoodStats.addExhaustion (148-151): min(exhaustion + in, 40.0F). */
MC_HD static inline void pv_add_exhaustion(PvStats *s, float exhaustion) {
    float ns = s->exhaustion + exhaustion;
    s->exhaustion = ns < 40.0f ? ns : 40.0f;
}

/* EntityPlayer.shouldHeal (2244-2246). */
MC_HD static inline int pv_should_heal(const PvStats *s) {
    return s->health > 0.0f && s->health < s->maxHealth;
}

/* EntityLivingBase.setHealth (927-929): clamp(h, 0, maxHealth). */
MC_HD static inline void pv_set_health(PvStats *s, float h) {
    float c = h < 0.0f ? 0.0f : (h > s->maxHealth ? s->maxHealth : h);
    s->health = c;
}

/* EntityLivingBase.heal (910-920): if amount>0 and health>0, setHealth(health+amount). */
MC_HD static inline void pv_heal(PvStats *s, float amount) {
    if (amount <= 0.0f) return;
    float f = s->health;
    if (f > 0.0f)
        pv_set_health(s, f + amount);
}

/* Direct damage application (STARVE/FALL): setHealth(health - amount). */
MC_HD static inline void pv_attack(PvStats *s, float amount) {
    pv_set_health(s, s->health - amount);
}

/* MathHelper.ceil(float) (107-111). */
MC_HD static inline i32 pv_ceil(float value) {
    i32 i = (i32)value;
    return value > (float)i ? i + 1 : i;
}

/* EntityLivingBase.fall (1389-1402), no feather-fall and
 * damageMultiplier=1.0F. amplifier < 0 means no Jump Boost. */
MC_HD static inline void pv_fall_damage_effect(
        PvStats *s, float distance, i32 jump_boost_amplifier) {
    float boost = jump_boost_amplifier < 0
        ? 0.0f : (float)(jump_boost_amplifier + 1);
    i32 i = pv_ceil(distance - 3.0f - boost);
    if (i > 0)
        pv_attack(s, (float)i);
}

MC_HD static inline void pv_fall_damage(PvStats *s, float distance) {
    pv_fall_damage_effect(s, distance, -1);
}

/* FoodStats.onUpdate (40-102) at difficulty NORMAL, gamerule-driven naturalRegeneration.
 * gr threads the GameRules struct (mc_gamerules.h); pass mc_gamerules_default() for the
 * vanilla default (naturalRegeneration=true), which is bit-identical to the prior hardcode. */
MC_HD static inline void pv_on_update_gr(PvStats *s, const McGameRules *gr) {
    if (s->exhaustion > 4.0f) {
        s->exhaustion -= 4.0f;
        if (s->saturation > 0.0f) {
            float ns = s->saturation - 1.0f;           /* Math.max(sat - 1, 0) */
            s->saturation = ns > 0.0f ? ns : 0.0f;
        } else {                                       /* NORMAL != PEACEFUL */
            i32 nl = s->foodLevel - 1;                 /* Math.max(food - 1, 0) */
            s->foodLevel = nl > 0 ? nl : 0;
        }
    }

    /* flag = naturalRegeneration (FoodStats.java:59); gates the two regen branches. */
    int flag = gr->naturalRegeneration;
    if (flag && s->saturation > 0.0f && pv_should_heal(s) && s->foodLevel >= 20) {
        ++s->foodTimer;
        if (s->foodTimer >= 10) {
            float f = s->saturation < 6.0f ? s->saturation : 6.0f;  /* Math.min(sat, 6.0F) */
            pv_heal(s, f / 6.0f);
            pv_add_exhaustion(s, f);
            s->foodTimer = 0;
        }
    } else if (flag && s->foodLevel >= 18 && pv_should_heal(s)) {
        ++s->foodTimer;
        if (s->foodTimer >= 80) {
            pv_heal(s, 1.0f);
            pv_add_exhaustion(s, 6.0f);
            s->foodTimer = 0;
        }
    } else if (s->foodLevel <= 0) {
        ++s->foodTimer;
        if (s->foodTimer >= 80) {
            /* NORMAL: health > 10 || (health > 1) -> starve down to 1.0F floor */
            if (s->health > 10.0f || (s->health > 1.0f))
                pv_attack(s, 1.0f);
            s->foodTimer = 0;
        }
    } else {
        s->foodTimer = 0;
    }
}

/* Default-rules wrapper: naturalRegeneration=true, bit-identical to the prior hardcode. */
MC_HD static inline void pv_on_update(PvStats *s) {
    McGameRules gr = mc_gamerules_default();
    pv_on_update_gr(s, &gr);
}

/* ---- deterministic action/exhaustion tape --------------------------------------------------- */
/* splitmix64 hash so the tape is reproducible across CPU/CUDA/Java from (seed, tick). */
MC_HD static inline u64 pv_hash(u64 x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x =  x ^ (x >> 31);
    return x;
}

/* Per-tick exhaustion menu drawn from real EntityPlayer addExhaustion call sites:
 *   0.0F idle, 0.05F jump (1926), 0.1F attack/sprint-move (1539), 0.2F sprint-jump (1922). */
MC_HD static inline float pv_tape_exhaustion(u64 h) {
    switch ((u32)(h & 3u)) {
        case 0:  return 0.0f;
        case 1:  return 0.05f;
        case 2:  return 0.1f;
        default: return 0.2f;
    }
}

/* One deterministic tick of the tape: derive exhaustion + occasional fall, then run vitals. */
MC_HD static inline void pv_tape_tick_effect(
        PvStats *s, i64 seed, i32 tick, i32 jump_boost_amplifier) {
    u64 h = pv_hash((u64)seed * 0x100000001b3ULL + (u64)(u32)tick);
    pv_add_exhaustion(s, pv_tape_exhaustion(h));
    if (((h >> 8) & 255u) == 0u) {                       /* ~1/256 ticks: take a survivable fall */
        float distance = (float)(4u + (u32)((h >> 12) % 4u));   /* 4..7 blocks -> 1..4 dmg */
        pv_fall_damage_effect(s, distance, jump_boost_amplifier);
    }
    pv_on_update(s);
}

MC_HD static inline void pv_tape_tick(PvStats *s, i64 seed, i32 tick) {
    pv_tape_tick_effect(s, seed, tick, -1);
}

#endif /* MC_PLAYER_VITALS_H */
