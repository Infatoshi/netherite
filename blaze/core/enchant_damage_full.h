/* enchant_damage_full: verbatim EnchantmentDamage type matrix (MC 1.11.2).
 *
 * PORT TARGETS:
 *   net/minecraft/enchantment/EnchantmentDamage.java
 *     calcDamageByCreature, getMinEnchantability, getMaxEnchantability, getMaxLevel,
 *     canApplyTogether
 *   net/minecraft/enchantment/EnchantmentHelper.ModifierLiving (livingModifier sum)
 *
 * Matrix: 3 damage types x 5 levels x 4 EnumCreatureAttribute values for calcDamageByCreature;
 * enchantability rows per type/level; canApplyTogether 3x3; integration rows through combat_math.
 *
 * READ-ONLY deps: combat_math.h (mc_combat_sharpness_bonus, mc_combat_final_damage),
 * items_tools_armor.h (ita_armor_set_points / ita_armor_set_toughness for armor rows).
 * Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_ENCHANT_DAMAGE_FULL_H
#define MC_ENCHANT_DAMAGE_FULL_H

#include "mc.h"
#include "combat_math.h"
#include "items_tools_armor.h"

enum {
    EDF_TYPE_SHARPNESS = 0,
    EDF_TYPE_SMITE     = 1,
    EDF_TYPE_BANE      = 2
};

enum {
    EDF_CREATURE_UNDEFINED = 0,
    EDF_CREATURE_UNDEAD    = 1,
    EDF_CREATURE_ARTHROPOD = 2,
    EDF_CREATURE_ILLAGER   = 3
};

#define EDF_NUM_TYPES     3
#define EDF_NUM_LEVELS    5
#define EDF_NUM_CREATURES 4

/* verbatim EnchantmentDamage.calcDamageByCreature */
MC_HD static inline float edf_calc_damage_by_creature(int damage_type, int level, int creature) {
    if (damage_type == EDF_TYPE_SHARPNESS)
        return mc_combat_sharpness_bonus(level);
    if (damage_type == EDF_TYPE_SMITE && creature == EDF_CREATURE_UNDEAD)
        return (float)level * 2.5F;
    if (damage_type == EDF_TYPE_BANE && creature == EDF_CREATURE_ARTHROPOD)
        return (float)level * 2.5F;
    return 0.0F;
}

MC_HD static inline int edf_base_enchantability(int damage_type) {
    static const int base[] = { 1, 5, 5 };
    return base[damage_type];
}

MC_HD static inline int edf_level_enchantability(int damage_type) {
    static const int lvl[] = { 11, 8, 8 };
    return lvl[damage_type];
}

MC_HD static inline int edf_threshold_enchantability(int damage_type) {
    static const int th[] = { 20, 20, 20 };
    return th[damage_type];
}

MC_HD static inline int edf_min_enchantability(int damage_type, int level) {
    return edf_base_enchantability(damage_type)
         + (level - 1) * edf_level_enchantability(damage_type);
}

MC_HD static inline int edf_max_enchantability(int damage_type, int level) {
    return edf_min_enchantability(damage_type, level)
         + edf_threshold_enchantability(damage_type);
}

MC_HD static inline int edf_max_level(int damage_type) {
    (void)damage_type;
    return 5;
}

/* verbatim EnchantmentDamage.canApplyTogether (both are EnchantmentDamage) */
MC_HD static inline int edf_can_apply_together(int type_a, int type_b) {
    (void)type_a;
    (void)type_b;
    return 0;
}

/* Player sword at full cooldown: ATTACK_DAMAGE(2) + (3 + toolDamage) + enchant bonus. */
MC_HD static inline float edf_weapon_raw_enchant(float tool_damage, int damage_type,
                                                   int level, int creature) {
    return 2.0F + 3.0F + tool_damage
         + edf_calc_damage_by_creature(damage_type, level, creature);
}

#define EDF_NUM_INTEGRATION 6
#define EDF_NOUT (EDF_NUM_TYPES * EDF_NUM_LEVELS * EDF_NUM_CREATURES \
                + EDF_NUM_TYPES * EDF_NUM_LEVELS * 2 \
                + EDF_NUM_TYPES \
                + EDF_NUM_TYPES * EDF_NUM_TYPES \
                + EDF_NUM_INTEGRATION)

MC_HD static inline void edf_put_u32(u32 *out, int *k, u32 v) {
    out[(*k)++] = v;
}

MC_HD static inline void edf_put_f32(u32 *out, int *k, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    edf_put_u32(out, k, u.u);
}

MC_HD static inline void edf_run_battery(u32 *out, int *k) {
    int t, l, c;

    for (t = 0; t < EDF_NUM_TYPES; ++t)
        for (l = 1; l <= EDF_NUM_LEVELS; ++l)
            for (c = 0; c < EDF_NUM_CREATURES; ++c)
                edf_put_f32(out, k, edf_calc_damage_by_creature(t, l, c));

    for (t = 0; t < EDF_NUM_TYPES; ++t)
        for (l = 1; l <= EDF_NUM_LEVELS; ++l)
            edf_put_u32(out, k, (u32)edf_min_enchantability(t, l));

    for (t = 0; t < EDF_NUM_TYPES; ++t)
        for (l = 1; l <= EDF_NUM_LEVELS; ++l)
            edf_put_u32(out, k, (u32)edf_max_enchantability(t, l));

    for (t = 0; t < EDF_NUM_TYPES; ++t)
        edf_put_u32(out, k, (u32)edf_max_level(t));

    for (t = 0; t < EDF_NUM_TYPES; ++t)
        for (c = 0; c < EDF_NUM_TYPES; ++c)
            edf_put_u32(out, k, (u32)edf_can_apply_together(t, c));

    /* integration: iron/diamond sword + smite/bane/sharp through armor absorb */
    edf_put_f32(out, k, edf_weapon_raw_enchant(2.0F, EDF_TYPE_SMITE, 5, EDF_CREATURE_UNDEAD));
    edf_put_f32(out, k, edf_weapon_raw_enchant(2.0F, EDF_TYPE_SMITE, 5, EDF_CREATURE_UNDEFINED));
    edf_put_f32(out, k, edf_weapon_raw_enchant(2.0F, EDF_TYPE_BANE, 5, EDF_CREATURE_ARTHROPOD));
    edf_put_f32(out, k, edf_weapon_raw_enchant(2.0F, EDF_TYPE_BANE, 5, EDF_CREATURE_UNDEAD));
    {
        float raw = edf_weapon_raw_enchant(3.0F, EDF_TYPE_SHARPNESS, 5, EDF_CREATURE_UNDEFINED);
        McCombatArmor armor = mc_combat_armor_set(4); /* diamond, no prot */
        edf_put_f32(out, k, mc_combat_final_damage(raw, &armor));
    }
    {
        float raw = edf_weapon_raw_enchant(2.0F, EDF_TYPE_SMITE, 5, EDF_CREATURE_UNDEAD);
        McCombatArmor armor = mc_combat_armor_set(3); /* iron full set */
        edf_put_f32(out, k, mc_combat_final_damage(raw, &armor));
    }
}

#endif /* MC_ENCHANT_DAMAGE_FULL_H */
