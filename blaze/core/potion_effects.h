/* Player potion effects, drink/milk finish, splash apply, shield block.
 *
 * Vanilla 1.11.2. One MC_HD source for magma and blaze (CPU+CUDA).
 *
 *   PotionEffect.onUpdate / combine          PotionEffect.java:68-90,123-149
 *   Potion.performEffect / isReady / affect  Potion.java:89-182
 *   PotionHealth.isInstant / isReady         PotionHealth.java:13-24
 *   PotionAttackDamage amount                PotionAttackDamage.java:16-18
 *   EntityLivingBase.updatePotionEffects     EntityLivingBase.java:656-726
 *   addPotionEffect / combine                EntityLivingBase.java:810-826
 *   applyPotionDamageCalculations            EntityLivingBase.java:1461-1492
 *   canBlockDamageSource                     EntityLivingBase.java:1175-1195
 *   isActiveItemStackBlocking                EntityLivingBase.java:3006-3016
 *   ItemPotion.onItemUseFinish duration 32   ItemPotion.java:40-91
 *   ItemBucketMilk (Forge cure)              ItemBucketMilk.java:25-52
 *   EntityPotion.applySplash                 EntityPotion.java:169-213
 *   PotionType.registerPotionTypes           PotionType.java:51-90
 *   EntityPlayer.damageShield / disableShield EntityPlayer.java:1145-1170,1555-1569
 *
 * Cap 32: Potion.registerPotions ids 1..27 (Potion.java:397-426). The Java
 * map is keyed by Potion so each id appears at most once. 32 > 27 is safe.
 * Iteration is insertion order (not HashMap bucket order); magma and blaze
 * share this so M1 stays bitwise. */
#ifndef MC_POTION_EFFECTS_H
#define MC_POTION_EFFECTS_H

#include "player_vitals.h"
#include "inventory_stack_rules.h"

#ifndef PSV_POTION_MAX
#define PSV_POTION_MAX 32
#endif

/* Potion.getIdFromPotion (Potion.java:397-426). */
#define PSV_POT_SPEED 1
#define PSV_POT_SLOWNESS 2
#define PSV_POT_HASTE 3
#define PSV_POT_MINING_FATIGUE 4
#define PSV_POT_STRENGTH 5
#define PSV_POT_INSTANT_HEALTH 6
#define PSV_POT_INSTANT_DAMAGE 7
#define PSV_POT_JUMP_BOOST 8
#define PSV_POT_NAUSEA 9
#define PSV_POT_REGENERATION 10
#define PSV_POT_RESISTANCE 11
#define PSV_POT_FIRE_RESISTANCE 12
#define PSV_POT_WATER_BREATHING 13
#define PSV_POT_INVISIBILITY 14
#define PSV_POT_BLINDNESS 15
#define PSV_POT_NIGHT_VISION 16
#define PSV_POT_HUNGER 17
#define PSV_POT_WEAKNESS 18
#define PSV_POT_POISON 19
#define PSV_POT_WITHER 20
#define PSV_POT_HEALTH_BOOST 21
#define PSV_POT_ABSORPTION 22
#define PSV_POT_SATURATION 23
#define PSV_POT_GLOWING 24
#define PSV_POT_LEVITATION 25
#define PSV_POT_LUCK 26
#define PSV_POT_UNLUCK 27

/* PotionType.registerPotionTypes order PotionType.java:53-90. */
#define PSV_PTYPE_EMPTY 0
#define PSV_PTYPE_WATER 1
#define PSV_PTYPE_MUNDANE 2
#define PSV_PTYPE_THICK 3
#define PSV_PTYPE_AWKWARD 4
#define PSV_PTYPE_NIGHT_VISION 5
#define PSV_PTYPE_LONG_NIGHT_VISION 6
#define PSV_PTYPE_INVISIBILITY 7
#define PSV_PTYPE_LONG_INVISIBILITY 8
#define PSV_PTYPE_LEAPING 9
#define PSV_PTYPE_LONG_LEAPING 10
#define PSV_PTYPE_STRONG_LEAPING 11
#define PSV_PTYPE_FIRE_RESISTANCE 12
#define PSV_PTYPE_LONG_FIRE_RESISTANCE 13
#define PSV_PTYPE_SWIFTNESS 14
#define PSV_PTYPE_LONG_SWIFTNESS 15
#define PSV_PTYPE_STRONG_SWIFTNESS 16
#define PSV_PTYPE_SLOWNESS 17
#define PSV_PTYPE_LONG_SLOWNESS 18
#define PSV_PTYPE_WATER_BREATHING 19
#define PSV_PTYPE_LONG_WATER_BREATHING 20
#define PSV_PTYPE_HEALING 21
#define PSV_PTYPE_STRONG_HEALING 22
#define PSV_PTYPE_HARMING 23
#define PSV_PTYPE_STRONG_HARMING 24
#define PSV_PTYPE_POISON 25
#define PSV_PTYPE_LONG_POISON 26
#define PSV_PTYPE_STRONG_POISON 27
#define PSV_PTYPE_REGENERATION 28
#define PSV_PTYPE_LONG_REGENERATION 29
#define PSV_PTYPE_STRONG_REGENERATION 30
#define PSV_PTYPE_STRENGTH 31
#define PSV_PTYPE_LONG_STRENGTH 32
#define PSV_PTYPE_STRONG_STRENGTH 33
#define PSV_PTYPE_WEAKNESS 34
#define PSV_PTYPE_LONG_WEAKNESS 35
#define PSV_PTYPE_LUCK 36

#define PSV_ITEM_POTION 373
#define PSV_ITEM_SPLASH_POTION 438
#define PSV_ITEM_LINGERING_POTION 441
#define PSV_ITEM_MILK 335
#define PSV_ITEM_GLASS_BOTTLE 374
#define PSV_ITEM_BUCKET 325
#define PSV_ITEM_SHIELD 442
#define PSV_SHIELD_MAX_DAMAGE 336 /* ItemShield.java:27 setMaxDamage(336) */

#define PSV_USE_NONE 0
#define PSV_USE_DRINK 1
#define PSV_USE_BLOCK 2
#define PSV_USE_DRINK_TICKS 32   /* ItemPotion.java:90 / ItemBucketMilk.java:52 */
#define PSV_SHIELD_USE_TICKS 72000 /* ItemShield.java:93 */
#define PSV_SHIELD_RAISE_TICKS 5 /* EntityLivingBase.java:3011 */
#define PSV_SHIELD_COOLDOWN 100  /* EntityPlayer.java:1566 */

#define PSV_HURT_BYPASS 1
#define PSV_HURT_FIRE 2
#define PSV_HURT_PROJECTILE 4
#define PSV_HURT_AXE 8
#define PSV_HURT_ABSOLUTE 16
#define PSV_HURT_VOID 32

/* Night vision / invisibility / blindness are render-only. No sim branch. */

struct PsvPlayer;

MC_HD static inline void psv_potion_clear(struct PsvPlayer *pl);
MC_HD static inline int psv_potion_is_active(const struct PsvPlayer *pl, int id);
MC_HD static inline int psv_potion_amplifier(const struct PsvPlayer *pl, int id);

MC_HD static inline int psv_potion_is_instant(int id) {
    /* PotionHealth.java:13-16: instant_health, instant_damage, saturation. */
    return id == PSV_POT_INSTANT_HEALTH || id == PSV_POT_INSTANT_DAMAGE ||
           id == PSV_POT_SATURATION;
}

MC_HD static inline int psv_potion_is_ready(int id, int duration, int amplifier) {
    /* Potion.java:161-181. PotionHealth.java:21-24 duration >= 1. */
    int k;
    if (id == PSV_POT_REGENERATION) {
        k = 50 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    if (id == PSV_POT_POISON) {
        k = 25 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    if (id == PSV_POT_WITHER) {
        k = 40 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    if (id == PSV_POT_HUNGER)
        return 1;
    if (psv_potion_is_instant(id))
        return duration >= 1;
    return 0;
}

MC_HD static inline int psv_ptype_effect(int type, int *id, int *duration,
                                         int *amplifier) {
    /* PotionType.java:58-89. Empty/water/mundane/thick/awkward have no effect. */
    *amplifier = 0;
    switch (type) {
    case PSV_PTYPE_NIGHT_VISION: *id = PSV_POT_NIGHT_VISION; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_NIGHT_VISION: *id = PSV_POT_NIGHT_VISION; *duration = 9600; return 1;
    case PSV_PTYPE_INVISIBILITY: *id = PSV_POT_INVISIBILITY; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_INVISIBILITY: *id = PSV_POT_INVISIBILITY; *duration = 9600; return 1;
    case PSV_PTYPE_LEAPING: *id = PSV_POT_JUMP_BOOST; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_LEAPING: *id = PSV_POT_JUMP_BOOST; *duration = 9600; return 1;
    case PSV_PTYPE_STRONG_LEAPING: *id = PSV_POT_JUMP_BOOST; *duration = 1800; *amplifier = 1; return 1;
    case PSV_PTYPE_FIRE_RESISTANCE: *id = PSV_POT_FIRE_RESISTANCE; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_FIRE_RESISTANCE: *id = PSV_POT_FIRE_RESISTANCE; *duration = 9600; return 1;
    case PSV_PTYPE_SWIFTNESS: *id = PSV_POT_SPEED; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_SWIFTNESS: *id = PSV_POT_SPEED; *duration = 9600; return 1;
    case PSV_PTYPE_STRONG_SWIFTNESS: *id = PSV_POT_SPEED; *duration = 1800; *amplifier = 1; return 1;
    case PSV_PTYPE_SLOWNESS: *id = PSV_POT_SLOWNESS; *duration = 1800; return 1;
    case PSV_PTYPE_LONG_SLOWNESS: *id = PSV_POT_SLOWNESS; *duration = 4800; return 1;
    case PSV_PTYPE_WATER_BREATHING: *id = PSV_POT_WATER_BREATHING; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_WATER_BREATHING: *id = PSV_POT_WATER_BREATHING; *duration = 9600; return 1;
    case PSV_PTYPE_HEALING: *id = PSV_POT_INSTANT_HEALTH; *duration = 1; return 1;
    case PSV_PTYPE_STRONG_HEALING: *id = PSV_POT_INSTANT_HEALTH; *duration = 1; *amplifier = 1; return 1;
    case PSV_PTYPE_HARMING: *id = PSV_POT_INSTANT_DAMAGE; *duration = 1; return 1;
    case PSV_PTYPE_STRONG_HARMING: *id = PSV_POT_INSTANT_DAMAGE; *duration = 1; *amplifier = 1; return 1;
    case PSV_PTYPE_POISON: *id = PSV_POT_POISON; *duration = 900; return 1;
    case PSV_PTYPE_LONG_POISON: *id = PSV_POT_POISON; *duration = 1800; return 1;
    case PSV_PTYPE_STRONG_POISON: *id = PSV_POT_POISON; *duration = 432; *amplifier = 1; return 1;
    case PSV_PTYPE_REGENERATION: *id = PSV_POT_REGENERATION; *duration = 900; return 1;
    case PSV_PTYPE_LONG_REGENERATION: *id = PSV_POT_REGENERATION; *duration = 1800; return 1;
    case PSV_PTYPE_STRONG_REGENERATION: *id = PSV_POT_REGENERATION; *duration = 450; *amplifier = 1; return 1;
    case PSV_PTYPE_STRENGTH: *id = PSV_POT_STRENGTH; *duration = 3600; return 1;
    case PSV_PTYPE_LONG_STRENGTH: *id = PSV_POT_STRENGTH; *duration = 9600; return 1;
    case PSV_PTYPE_STRONG_STRENGTH: *id = PSV_POT_STRENGTH; *duration = 1800; *amplifier = 1; return 1;
    case PSV_PTYPE_WEAKNESS: *id = PSV_POT_WEAKNESS; *duration = 1800; return 1;
    case PSV_PTYPE_LONG_WEAKNESS: *id = PSV_POT_WEAKNESS; *duration = 4800; return 1;
    case PSV_PTYPE_LUCK: *id = PSV_POT_LUCK; *duration = 6000; return 1;
    default: *id = 0; *duration = 0; return 0;
    }
}

MC_HD static inline void psv_sync_health(struct PsvPlayer *pl, PvStats *vit) {
    if (!pl) return;
    if (vit) {
        pl->health = vit->health;
        pl->food = (float)vit->foodLevel;
    }
}

MC_HD static inline void psv_potion_heal(struct PsvPlayer *pl, PvStats *vit, float amt) {
    if (vit) {
        pv_heal(vit, amt);
        psv_sync_health(pl, vit);
    } else if (pl && pl->health > 0.0f) {
        pl->health += amt;
        if (pl->health > PSV_MAX_HEALTH) pl->health = PSV_MAX_HEALTH;
    }
}

MC_HD static inline void psv_potion_hurt(struct PsvPlayer *pl, PvStats *vit, float amt) {
    if (amt <= 0.0f) return;
    if (vit) {
        pv_attack(vit, amt);
        psv_sync_health(pl, vit);
    } else if (pl) {
        pl->health -= amt;
        if (pl->health < 0.0f) pl->health = 0.0f;
    }
}

MC_HD static inline float psv_health_now(const struct PsvPlayer *pl, const PvStats *vit) {
    if (vit) return vit->health;
    return pl ? pl->health : 0.0f;
}

MC_HD static inline void psv_potion_perform(struct PsvPlayer *pl, PvStats *vit,
                                            int id, int amplifier) {
    /* Potion.java:89-130. Player is not undead. */
    float hp;
    if (!pl) return;
    if (id == PSV_POT_REGENERATION) {
        hp = psv_health_now(pl, vit);
        if (hp < PSV_MAX_HEALTH)
            psv_potion_heal(pl, vit, 1.0f);
        return;
    }
    if (id == PSV_POT_POISON) {
        /* never kills below 1.0 Potion.java:100-103 */
        hp = psv_health_now(pl, vit);
        if (hp > 1.0f)
            psv_potion_hurt(pl, vit, 1.0f);
        return;
    }
    if (id == PSV_POT_WITHER) {
        psv_potion_hurt(pl, vit, 1.0f);
        return;
    }
    if (id == PSV_POT_HUNGER) {
        /* Potion.java:111 addExhaustion(0.005F * (amp+1)) */
        if (vit)
            pv_add_exhaustion(vit, 0.005f * (float)(amplifier + 1));
        return;
    }
    if (id == PSV_POT_SATURATION) {
        /* Potion.java:117 FoodStats.addStats(amp+1, 1.0F) */
        if (vit) {
            int add = amplifier + 1;
            int nl = vit->foodLevel + add;
            if (nl > 20) nl = 20;
            vit->foodLevel = nl;
            {
                float sat = vit->saturation + (float)add * 1.0f * 2.0f;
                if (sat > (float)vit->foodLevel) sat = (float)vit->foodLevel;
                vit->saturation = sat;
            }
            psv_sync_health(pl, vit);
        }
        return;
    }
    if (id == PSV_POT_INSTANT_HEALTH) {
        /* Potion.java:129 heal(max(4 << amp, 0)) */
        int h = 4 << amplifier;
        if (h < 0) h = 0;
        psv_potion_heal(pl, vit, (float)h);
        return;
    }
    if (id == PSV_POT_INSTANT_DAMAGE) {
        /* Potion.java:124 MAGIC (6 << amp) */
        psv_potion_hurt(pl, vit, (float)(6 << amplifier));
        return;
    }
}

MC_HD static inline void psv_potion_affect(struct PsvPlayer *pl, PvStats *vit,
                                           int id, int amplifier, double health) {
    /* Potion.affectEntity Potion.java:133-156. Player is not undead.
     * Instant health heals (int)(health * (4 << amp) + 0.5).
     * Instant damage MAGIC (int)(health * (6 << amp) + 0.5). */
    int n;
    if (id == PSV_POT_INSTANT_HEALTH) {
        n = (int)(health * (double)(4 << amplifier) + 0.5);
        psv_potion_heal(pl, vit, (float)n);
        return;
    }
    if (id == PSV_POT_INSTANT_DAMAGE) {
        n = (int)(health * (double)(6 << amplifier) + 0.5);
        psv_potion_hurt(pl, vit, (float)n);
        return;
    }
    /* Saturation isInstant but affectEntity does not handle it. */
    (void)pl;
    (void)vit;
    (void)amplifier;
}

MC_HD static inline int psv_potion_find(const struct PsvPlayer *pl, int id) {
    int i;
    if (!pl || id <= 0) return -1;
    for (i = 0; i < pl->n_potions; ++i)
        if (pl->potions[i].id == id) return i;
    return -1;
}

MC_HD static inline int psv_potion_is_active(const struct PsvPlayer *pl, int id) {
    return psv_potion_find(pl, id) >= 0;
}

MC_HD static inline int psv_potion_amplifier(const struct PsvPlayer *pl, int id) {
    int i = psv_potion_find(pl, id);
    return i >= 0 ? pl->potions[i].amplifier : 0;
}

MC_HD static inline void psv_potion_clear(struct PsvPlayer *pl) {
    int i;
    if (!pl) return;
    pl->n_potions = 0;
    for (i = 0; i < PSV_POTION_MAX; ++i) {
        pl->potions[i].id = 0;
        pl->potions[i].amplifier = 0;
        pl->potions[i].duration = 0;
        pl->potions[i].ambient = 0;
        pl->potions[i].show_particles = 1;
    }
}

MC_HD static inline void psv_potion_combine(PsvPotionEffect *dst,
                                            const PsvPotionEffect *src) {
    /* PotionEffect.combine PotionEffect.java:68-90. */
    if (src->amplifier > dst->amplifier) {
        dst->amplifier = src->amplifier;
        dst->duration = src->duration;
    } else if (src->amplifier == dst->amplifier && dst->duration < src->duration) {
        dst->duration = src->duration;
    } else if (!src->ambient && dst->ambient) {
        dst->ambient = src->ambient;
    }
    dst->show_particles = src->show_particles;
}

MC_HD static inline int psv_potion_add(struct PsvPlayer *pl, int id, int duration,
                                       int amplifier, int ambient, int show_particles) {
    /* EntityLivingBase.addPotionEffect EntityLivingBase.java:810-826. */
    PsvPotionEffect pe;
    int i;
    if (!pl || id <= 0 || duration <= 0) return 0;
    pe.id = id;
    pe.duration = duration;
    pe.amplifier = amplifier < 0 ? 0 : amplifier;
    pe.ambient = ambient ? 1 : 0;
    pe.show_particles = show_particles ? 1 : 0;
    i = psv_potion_find(pl, id);
    if (i >= 0) {
        psv_potion_combine(&pl->potions[i], &pe);
        return 1;
    }
    if (pl->n_potions >= PSV_POTION_MAX) return 0;
    pl->potions[pl->n_potions++] = pe;
    return 1;
}

MC_HD static inline void psv_update_potion_effects(struct PsvPlayer *pl, PvStats *vit) {
    /* EntityLivingBase.updatePotionEffects EntityLivingBase.java:656-674
     * called from onEntityUpdate :389, after air/drown and before
     * onLivingUpdate travel. onUpdate performs then decrements. */
    int i, o = 0;
    if (!pl) return;
    if (pl->shield_cooldown > 0) --pl->shield_cooldown;
    for (i = 0; i < pl->n_potions; ++i) {
        PsvPotionEffect *e = &pl->potions[i];
        if (e->id <= 0) continue;
        if (e->duration > 0) {
            if (psv_potion_is_ready(e->id, e->duration, e->amplifier))
                psv_potion_perform(pl, vit, e->id, e->amplifier);
            --e->duration;
        }
        if (e->duration > 0)
            pl->potions[o++] = *e;
    }
    for (i = o; i < pl->n_potions; ++i) {
        pl->potions[i].id = 0;
        pl->potions[i].duration = 0;
        pl->potions[i].amplifier = 0;
    }
    pl->n_potions = o;
}

MC_HD static inline void psv_potion_apply_one(struct PsvPlayer *pl, PvStats *vit,
                                              int id, int duration, int amplifier,
                                              int ambient, int show_particles,
                                              double intensity) {
    /* EntityPotion.applySplash Potion.java isInstant vs duration scale. */
    if (psv_potion_is_instant(id)) {
        psv_potion_affect(pl, vit, id, amplifier, intensity);
        return;
    }
    {
        int d = (int)(intensity * (double)duration + 0.5);
        if (d > 20)
            psv_potion_add(pl, id, d, amplifier, ambient, show_particles);
    }
}

MC_HD static inline void psv_potion_apply_type(struct PsvPlayer *pl, PvStats *vit,
                                               int type, double intensity) {
    int id, dur, amp;
    if (!psv_ptype_effect(type, &id, &dur, &amp)) return;
    psv_potion_apply_one(pl, vit, id, dur, amp, 0, 1, intensity);
}

MC_HD static inline void psv_potion_drink_finish(struct PsvPlayer *pl, PvStats *vit,
                                                 int slot, int creative) {
    /* ItemPotion.onItemUseFinish ItemPotion.java:40-82. */
    ICStack s;
    int id, dur, amp;
    if (!pl) return;
    s = isr_get_stack(&pl->inv, slot);
    if (s.item != PSV_ITEM_POTION) return;
    if (psv_ptype_effect(s.meta, &id, &dur, &amp)) {
        if (psv_potion_is_instant(id))
            psv_potion_affect(pl, vit, id, amp, 1.0);
        else
            psv_potion_add(pl, id, dur, amp, 0, 1);
    }
    if (!creative) {
        (void)isr_decr_stack_size(&pl->inv, slot, 1);
        s = isr_get_stack(&pl->inv, slot);
        if (isr_is_empty(&s))
            isr_set_stack(&pl->inv, slot, ic_mk(PSV_ITEM_GLASS_BOTTLE, 1, 0));
        else {
            ICStack bottle = ic_mk(PSV_ITEM_GLASS_BOTTLE, 1, 0);
            (void)isr_add_item_stack_to_inventory(&pl->inv, &bottle);
        }
    }
}

MC_HD static inline void psv_potion_milk_finish(struct PsvPlayer *pl, int slot,
                                                int creative) {
    /* ItemBucketMilk.java:25-44. Forge curePotionEffects = milk for vanilla. */
    ICStack s;
    if (!pl) return;
    psv_potion_clear(pl);
    if (creative) return;
    s = isr_get_stack(&pl->inv, slot);
    (void)isr_decr_stack_size(&pl->inv, slot, 1);
    s = isr_get_stack(&pl->inv, slot);
    if (isr_is_empty(&s))
        isr_set_stack(&pl->inv, slot, ic_mk(PSV_ITEM_BUCKET, 1, 0));
}

MC_HD static inline float psv_potion_move_mul(const struct PsvPlayer *pl) {
    /* Operation 2 MULTIPLY_TOTAL Potion.java:399-400, getAttributeModifierAmount
     * Potion.java:307-309 amount * (amp+1). Base 0.1 * product of (1+amt). */
    float m = 1.0f;
    int i, amp;
    i = psv_potion_find(pl, PSV_POT_SPEED);
    if (i >= 0) {
        amp = pl->potions[i].amplifier;
        m *= (1.0f + 0.20000000298023224f * (float)(amp + 1));
    }
    i = psv_potion_find(pl, PSV_POT_SLOWNESS);
    if (i >= 0) {
        amp = pl->potions[i].amplifier;
        m *= (1.0f + (-0.15000000596046448f) * (float)(amp + 1));
    }
    if (m < 0.0f) m = 0.0f;
    return m;
}

MC_HD static inline float psv_jump_boost_extra(const struct PsvPlayer *pl) {
    /* EntityLivingBase.jump EntityLivingBase.java:1909-1911
     * motionY += (amp+1) * 0.1F */
    int i = psv_potion_find(pl, PSV_POT_JUMP_BOOST);
    if (i < 0) return 0.0f;
    return (float)(pl->potions[i].amplifier + 1) * 0.1f;
}

MC_HD static inline float psv_jump_boost_fall(const struct PsvPlayer *pl) {
    /* EntityLivingBase.fall EntityLivingBase.java:1395-1397 f = amp+1 */
    int i = psv_potion_find(pl, PSV_POT_JUMP_BOOST);
    if (i < 0) return 0.0f;
    return (float)(pl->potions[i].amplifier + 1);
}

MC_HD static inline float psv_attack_damage_bonus(const struct PsvPlayer *pl) {
    /* PotionAttackDamage.java:16-18 bonusPerLevel * (amp+1).
     * Strength 3.0 Potion.java:403; weakness -4.0 Potion.java:417. */
    float b = 0.0f;
    int i;
    i = psv_potion_find(pl, PSV_POT_STRENGTH);
    if (i >= 0) b += 3.0f * (float)(pl->potions[i].amplifier + 1);
    i = psv_potion_find(pl, PSV_POT_WEAKNESS);
    if (i >= 0) b += -4.0f * (float)(pl->potions[i].amplifier + 1);
    return b;
}

MC_HD static inline float psv_resistance_scale(const struct PsvPlayer *pl, int flags) {
    /* EntityLivingBase.applyPotionDamageCalculations :1461-1475.
     * Skip if absolute. Skip OUT_OF_WORLD. i=(amp+1)*5, j=25-i, dmg*j/25. */
    int i, amp, j;
    if (!pl) return 1.0f;
    if (flags & (PSV_HURT_ABSOLUTE | PSV_HURT_VOID)) return 1.0f;
    i = psv_potion_find(pl, PSV_POT_RESISTANCE);
    if (i < 0) return 1.0f;
    amp = pl->potions[i].amplifier;
    j = 25 - (amp + 1) * 5;
    return (float)j / 25.0f;
}

MC_HD static inline int psv_is_blocking(const struct PsvPlayer *pl) {
    /* EntityLivingBase.isActiveItemStackBlocking :3006-3016. */
    int elapsed;
    if (!pl) return 0;
    if (pl->shield_cooldown > 0) return 0;
    if (pl->use_action != PSV_USE_BLOCK) return 0;
    if (pl->use_max <= 0) return 0;
    elapsed = pl->use_max - pl->use_remaining;
    return elapsed >= PSV_SHIELD_RAISE_TICKS;
}

MC_HD static inline int psv_can_block_damage(const struct PsvPlayer *pl, int flags,
                                             double src_x, double src_z) {
    /* EntityLivingBase.canBlockDamageSource :1175-1195.
     * Unblockable sources (setDamageBypassesArmor) cannot block. */
    double dx, dz, len, lx, lz, dot;
    float yaw, pitch, f, f1, f2;
    if (!pl) return 0;
    if (flags & (PSV_HURT_BYPASS | PSV_HURT_ABSOLUTE | PSV_HURT_VOID))
        return 0;
    if (!psv_is_blocking(pl)) return 0;
    dx = pl->ent.posX - src_x;
    dz = pl->ent.posZ - src_z;
    len = sqrt(dx * dx + dz * dz);
    if (len < 1.0e-4) return 0;
    dx /= len;
    dz /= len;
    yaw = pl->yaw;
    pitch = pl->pitch;
    /* Entity.getLook Vec3d (yaw/pitch degrees). */
    f = cosf(-yaw * 0.017453292f - (float)MC_PI);
    f1 = sinf(-yaw * 0.017453292f - (float)MC_PI);
    f2 = -cosf(-pitch * 0.017453292f);
    lx = (double)(f1 * f2);
    lz = (double)(f * f2);
    dot = dx * lx + dz * lz;
    return dot < 0.0;
}

MC_HD static inline int psv_shield_slot(const struct PsvPlayer *pl) {
    ICStack s;
    if (!pl) return -1;
    if (pl->active_hand == 1) {
        s = isr_get_stack(&pl->inv, ISR_OFFHAND_SLOT);
        if (s.item == PSV_ITEM_SHIELD) return ISR_OFFHAND_SLOT;
    }
    s = isr_get_stack(&pl->inv, pl->inv.current_item);
    if (s.item == PSV_ITEM_SHIELD) return pl->inv.current_item;
    s = isr_get_stack(&pl->inv, ISR_OFFHAND_SLOT);
    if (s.item == PSV_ITEM_SHIELD) return ISR_OFFHAND_SLOT;
    return -1;
}

MC_HD static inline void psv_reset_active_hand(struct PsvPlayer *pl) {
    if (!pl) return;
    pl->use_action = PSV_USE_NONE;
    pl->use_remaining = 0;
    pl->use_max = 0;
    pl->active_hand = 0;
}

MC_HD static inline void psv_disable_shield(struct PsvPlayer *pl) {
    /* EntityPlayer.disableShield :1555-1569 after the rand check. */
    if (!pl) return;
    pl->shield_cooldown = PSV_SHIELD_COOLDOWN;
    psv_reset_active_hand(pl);
}

MC_HD static inline void psv_damage_shield(struct PsvPlayer *pl, float damage) {
    /* EntityPlayer.damageShield :1145-1170. Only if damage >= 3.0F.
     * durability 1 + floor(damage). Break empties the stack. */
    int slot, i;
    ICStack s;
    if (!pl || damage < 3.0f) return;
    slot = psv_shield_slot(pl);
    if (slot < 0) return;
    s = isr_get_stack(&pl->inv, slot);
    if (s.item != PSV_ITEM_SHIELD) return;
    i = 1 + (int)floorf(damage);
    s.meta += i;
    if (s.meta > PSV_SHIELD_MAX_DAMAGE) {
        isr_set_stack(&pl->inv, slot, ic_empty());
        psv_reset_active_hand(pl);
    } else {
        isr_set_stack(&pl->inv, slot, s);
    }
}

MC_HD static inline int psv_hurt_pre(struct PsvPlayer *pl, float *amount, int flags,
                                     double src_x, double src_z, int *blocked) {
    /* Fire resistance: attackEntityFrom :954-957 returns false.
     * Shield: :968-983 zeros amount. Resistance scales remaining. */
    if (blocked) *blocked = 0;
    if (!pl || !amount || *amount <= 0.0f) return 0;
    if ((flags & PSV_HURT_FIRE) && psv_potion_is_active(pl, PSV_POT_FIRE_RESISTANCE))
        return 0;
    if (psv_can_block_damage(pl, flags, src_x, src_z)) {
        psv_damage_shield(pl, *amount);
        *amount = 0.0f;
        if (blocked) *blocked = 1;
        if (flags & PSV_HURT_AXE)
            psv_disable_shield(pl);
        return 1;
    }
    *amount *= psv_resistance_scale(pl, flags);
    return 1;
}

MC_HD static inline int psv_use_item_kind(const struct PsvPlayer *pl, int *slot_out) {
    /* Main hand first, then offhand shield. ItemPotion/ItemBucketMilk/ItemFood
     * / ItemShield onItemRightClick. */
    ICStack main, off;
    if (!pl) return 0;
    main = isr_get_stack(&pl->inv, pl->inv.current_item);
    off = isr_get_stack(&pl->inv, ISR_OFFHAND_SLOT);
    if (main.item == PSV_ITEM_POTION || main.item == PSV_ITEM_MILK) {
        if (slot_out) *slot_out = pl->inv.current_item;
        return PSV_USE_DRINK;
    }
    if (main.item == PSV_ITEM_SHIELD) {
        if (slot_out) *slot_out = pl->inv.current_item;
        return PSV_USE_BLOCK;
    }
    if (off.item == PSV_ITEM_SHIELD) {
        if (slot_out) *slot_out = ISR_OFFHAND_SLOT;
        return PSV_USE_BLOCK;
    }
    return 0;
}

#endif /* MC_POTION_EFFECTS_H */
