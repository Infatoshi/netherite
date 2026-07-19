/* enchant_protection_full: verbatim EnchantmentProtection type matrix (MC 1.11.2).
 *
 * PORT TARGETS:
 *   net/minecraft/enchantment/EnchantmentProtection.java
 *     calcModifierDamage, getMinEnchantability, getMaxEnchantability, getMaxLevel,
 *     canApplyTogether, getFireTimeForEntity, getBlastDamageReduction
 *
 * Matrix: 5 protection types x 4 levels x 6 DamageSource scenarios for calcModifierDamage;
 * enchantability rows per type/level; canApplyTogether 5x5; integration through combat_math
 * and items_tools_armor.
 *
 * READ-ONLY deps: combat_math.h, items_tools_armor.h.
 * Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_ENCHANT_PROTECTION_FULL_H
#define MC_ENCHANT_PROTECTION_FULL_H

#include "mc.h"
#include "combat_math.h"
#include "items_tools_armor.h"

enum {
    EPF_TYPE_ALL       = 0,
    EPF_TYPE_FIRE      = 1,
    EPF_TYPE_FALL      = 2,
    EPF_TYPE_EXPLOSION = 3,
    EPF_TYPE_PROJECTILE = 4
};

/* DamageSource scenario flags (subset for calcModifierDamage matrix). */
enum {
    EPF_DS_GENERIC   = 0,
    EPF_DS_FIRE      = 1,
    EPF_DS_FALL      = 2,
    EPF_DS_EXPLOSION = 3,
    EPF_DS_PROJECTILE = 4,
    EPF_DS_CREATIVE  = 5
};

#define EPF_NUM_TYPES  5
#define EPF_NUM_LEVELS 4
#define EPF_NUM_DS     6

MC_HD static inline int epf_type_min_enchantability(int prot_type) {
    static const int min[] = { 1, 10, 5, 5, 3 };
    return min[prot_type];
}

MC_HD static inline int epf_type_level_cost(int prot_type) {
    static const int cost[] = { 11, 8, 6, 8, 6 };
    return cost[prot_type];
}

MC_HD static inline int epf_min_enchantability(int prot_type, int level) {
    return epf_type_min_enchantability(prot_type)
         + (level - 1) * epf_type_level_cost(prot_type);
}

MC_HD static inline int epf_max_enchantability(int prot_type, int level) {
    return epf_min_enchantability(prot_type, level)
         + epf_type_level_cost(prot_type);
}

MC_HD static inline int epf_max_level(int prot_type) {
    (void)prot_type;
    return 4;
}

/* verbatim EnchantmentProtection.calcModifierDamage (DamageSource subset). */
MC_HD static inline int epf_calc_modifier_damage(int prot_type, int level, int ds_scenario) {
    if (ds_scenario == EPF_DS_CREATIVE)
        return 0;
    if (prot_type == EPF_TYPE_ALL)
        return level;
    if (prot_type == EPF_TYPE_FIRE && ds_scenario == EPF_DS_FIRE)
        return level * 2;
    if (prot_type == EPF_TYPE_FALL && ds_scenario == EPF_DS_FALL)
        return level * 3;
    if (prot_type == EPF_TYPE_EXPLOSION && ds_scenario == EPF_DS_EXPLOSION)
        return level * 2;
    if (prot_type == EPF_TYPE_PROJECTILE && ds_scenario == EPF_DS_PROJECTILE)
        return level * 2;
    return 0;
}

/* verbatim EnchantmentProtection.canApplyTogether (both EnchantmentProtection). */
MC_HD static inline int epf_can_apply_together(int type_a, int type_b) {
    if (type_a == type_b)
        return 0;
    if (type_a == EPF_TYPE_FALL || type_b == EPF_TYPE_FALL)
        return 1;
    return 0;
}

/* verbatim getFireTimeForEntity: fireTime -= floor(fireTime * level * 0.15F). */
MC_HD static inline int epf_fire_time_for_entity(int fire_time, int fire_prot_level) {
    if (fire_prot_level <= 0)
        return fire_time;
    {
        float scaled = (float)fire_time * (float)fire_prot_level * 0.15F;
        int reduction = (int)scaled; /* toward zero, same as MathHelper.floor for non-negative */
        return fire_time - reduction;
    }
}

/* verbatim getBlastDamageReduction: damage -= floor(damage * level * 0.15F). */
MC_HD static inline float epf_blast_damage_reduction(float damage, int blast_prot_level) {
    if (blast_prot_level <= 0)
        return damage;
    {
        float scaled = damage * (float)blast_prot_level * 0.15F;
        float reduction = (float)(int)scaled;
        return damage - reduction;
    }
}

#define EPF_NUM_INTEGRATION 6
#define EPF_NOUT (EPF_NUM_TYPES * EPF_NUM_LEVELS * EPF_NUM_DS \
                + EPF_NUM_TYPES * EPF_NUM_LEVELS * 2 \
                + EPF_NUM_TYPES \
                + EPF_NUM_TYPES * EPF_NUM_TYPES \
                + EPF_NUM_INTEGRATION)

MC_HD static inline void epf_put_u32(u32 *out, int *k, u32 v) {
    out[(*k)++] = v;
}

MC_HD static inline void epf_put_f32(u32 *out, int *k, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    epf_put_u32(out, k, u.u);
}

MC_HD static inline void epf_run_battery(u32 *out, int *k) {
    int t, l, d;

    for (t = 0; t < EPF_NUM_TYPES; ++t)
        for (l = 1; l <= EPF_NUM_LEVELS; ++l)
            for (d = 0; d < EPF_NUM_DS; ++d)
                epf_put_u32(out, k, (u32)epf_calc_modifier_damage(t, l, d));

    for (t = 0; t < EPF_NUM_TYPES; ++t)
        for (l = 1; l <= EPF_NUM_LEVELS; ++l)
            epf_put_u32(out, k, (u32)epf_min_enchantability(t, l));

    for (t = 0; t < EPF_NUM_TYPES; ++t)
        for (l = 1; l <= EPF_NUM_LEVELS; ++l)
            epf_put_u32(out, k, (u32)epf_max_enchantability(t, l));

    for (t = 0; t < EPF_NUM_TYPES; ++t)
        epf_put_u32(out, k, (u32)epf_max_level(t));

    for (t = 0; t < EPF_NUM_TYPES; ++t)
        for (d = 0; d < EPF_NUM_TYPES; ++d)
            epf_put_u32(out, k, (u32)epf_can_apply_together(t, d));

    /* integration: prot IV chest + fire II boots via items_tools_armor */
    {
        ITAStack prot_set[4] = { ita_mk(0, 0), ita_mk(0, 0), ita_mk(0, 0), ita_mk(0, 0) };
        prot_set[2] = ita_mk(307, 0);
        prot_set[2].prot_all = 4;
        prot_set[0] = ita_mk(309, 0);
        prot_set[0].prot_fire = 2;
        int mod_gen = ita_enchant_prot_modifier(prot_set, ITA_DS_GENERIC);
        int mod_fire = ita_enchant_prot_modifier(prot_set, ITA_DS_FIRE);
        epf_put_u32(out, k, (u32)mod_gen);
        epf_put_u32(out, k, (u32)mod_fire);
        epf_put_f32(out, k, mc_combat_damage_after_magic_absorb(10.0F, (float)mod_fire));
    }
    /* integration: diamond full set prot IV through combat_math final damage */
    {
        float raw = mc_combat_weapon_raw(4); /* diamond sword, no sharp */
        McCombatArmor armor = mc_combat_armor_set(5); /* diamond + prot IV all slots */
        epf_put_f32(out, k, mc_combat_final_damage(raw, &armor));
    }
    epf_put_u32(out, k, (u32)epf_fire_time_for_entity(100, 4));
    epf_put_f32(out, k, epf_blast_damage_reduction(20.0F, 4));
}

#endif /* MC_ENCHANT_PROTECTION_FULL_H */
