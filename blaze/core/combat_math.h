/* combat_math: exact C port of MC 1.11.2 melee damage reduction math.
 *
 * PORT TARGETS (vanilla 1.11.2, EntityLivingBase path - no Forge hooks):
 *   net/minecraft/util/CombatRules.java
 *   net/minecraft/entity/EntityLivingBase.java applyArmorCalculations + applyPotionDamageCalculations
 *   net/minecraft/enchantment/EnchantmentDamage.java calcDamageByCreature (Sharpness, type 0)
 *   net/minecraft/enchantment/EnchantmentProtection.java calcModifierDamage (Protection ALL)
 *   net/minecraft/entity/player/EntityPlayer.java (ATTACK_DAMAGE base 1.0)
 *   net/minecraft/item/ItemSword.java + Item.java ToolMaterial (weapon base damage)
 *   net/minecraft/item/ItemArmor.java ArmorMaterial (armor points + toughness)
 *
 * Matrix: weapon (7) x armor set (6) = 42 deterministic scenarios. Each cell emits one float: final
 * damage after armor absorb + Protection enchant (generic melee, not unblockable/absolute, no
 * resistance potion, no absorption/shields/i-frames). Weapon column uses player ATTACK_DAMAGE=1.0
 * + sword modifier at full attack cooldown; Sharpness adds calcDamageByCreature bonus.
 *
 * block_props_table.h is a READ-ONLY dep (not included here; armor values are baked from ItemArmor).
 * Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_COMBAT_MATH_H
#define MC_COMBAT_MATH_H

#include "mc.h"

/* MathHelper.clamp(float) */
MC_HD static inline float mc_clampf(float num, float min, float max) {
    return num < min ? min : (num > max ? max : num);
}

/* ---- verbatim CombatRules.java ---- */
MC_HD static inline float mc_combat_damage_after_absorb(float damage, float total_armor, float toughness_attr) {
    float f = 2.0F + toughness_attr / 4.0F;
    float f1 = mc_clampf(total_armor - damage / f, total_armor * 0.2F, 20.0F);
    return damage * (1.0F - f1 / 25.0F);
}

MC_HD static inline float mc_combat_damage_after_magic_absorb(float damage, float prot_pts) {
    float f = mc_clampf(prot_pts, 0.0F, 20.0F);
    return damage * (1.0F - f / 25.0F);
}

/* EnchantmentDamage.calcDamageByCreature (damageType==0, Sharpness vs UNDEFINED) */
MC_HD static inline float mc_combat_sharpness_bonus(int level) {
    if (level <= 0) return 0.0F;
    return 1.0F + (float)(level - 1) * 0.5F;
}

/* EnchantmentProtection.calcModifierDamage (Type.ALL, generic melee) */
MC_HD static inline int mc_combat_prot_modifier(int prot_level) {
    return prot_level; /* Type.ALL: level per piece; caller sums across armor slots */
}

/* Player sword attack at full cooldown: ATTACK_DAMAGE(1) + (3 + toolDamage) + sharpness. */
MC_HD static inline float mc_combat_weapon_raw(int weapon_idx) {
    static const float tool_dmg[] = { 0.0F, 0.0F, 1.0F, 2.0F, 3.0F, 3.0F, 3.0F };
    static const int   sharp[]    = { 0,     0,     0,     0,     0,     1,     5     };
    /* idx 0 fist (no sword), 1 wood, 2 stone, 3 iron, 4 diamond, 5 diamond+sharp1, 6 diamond+sharp5 */
    float base = 1.0F; /* EntityPlayer.applyEntityAttributes override */
    float sword = (weapon_idx == 0) ? 0.0F : (3.0F + tool_dmg[weapon_idx]);
    return base + sword + mc_combat_sharpness_bonus(sharp[weapon_idx]);
}

typedef struct {
    int   armor_pts;    /* MathHelper.floor(ARMOR attribute), full set total */
    float toughness;    /* ARMOR_TOUGHNESS attribute sum */
    int   prot_sum;     /* sum of Protection ALL calcModifierDamage across 4 slots */
} McCombatArmor;

MC_HD static inline McCombatArmor mc_combat_armor_set(int armor_idx) {
    /* Leather {1,2,3,1}=7; Chain {1,4,5,2}=12; Iron {2,5,6,2}=15; Diamond {3,6,8,3}=20 tough 2/piece */
    static const int   ap[]  = { 0,  7, 12, 15, 20, 20 };
    static const float th[]  = { 0.0F, 0.0F, 0.0F, 0.0F, 8.0F, 8.0F };
    static const int   pr[]  = { 0,  0,  0,  0,  0, 16 }; /* prot IV on all 4 diamond pieces */
    McCombatArmor a;
    a.armor_pts = ap[armor_idx];
    a.toughness = th[armor_idx];
    a.prot_sum  = pr[armor_idx];
    return a;
}

#define MC_CM_NUM_WEAPONS 7
#define MC_CM_NUM_ARMORS  6
#define MC_CM_NUM_SCENARIOS (MC_CM_NUM_WEAPONS * MC_CM_NUM_ARMORS)

/* EntityLivingBase.applyArmorCalculations + applyPotionDamageCalculations (no resistance). */
MC_HD static inline float mc_combat_final_damage(float raw, const McCombatArmor *armor) {
    float damage = raw;
    /* applyArmorCalculations: source is blockable generic melee */
    damage = mc_combat_damage_after_absorb(damage, (float)armor->armor_pts, armor->toughness);
    /* applyPotionDamageCalculations: not absolute, no resistance potion */
    if (armor->prot_sum > 0)
        damage = mc_combat_damage_after_magic_absorb(damage, (float)armor->prot_sum);
    return damage;
}

MC_HD static inline void mc_combat_scenario(int idx, float *raw_out, McCombatArmor *armor_out) {
    int wi = idx / MC_CM_NUM_ARMORS;
    int ai = idx % MC_CM_NUM_ARMORS;
    *raw_out = mc_combat_weapon_raw(wi);
    *armor_out = mc_combat_armor_set(ai);
}

MC_HD static inline float mc_combat_scenario_damage(int idx) {
    float raw;
    McCombatArmor armor;
    mc_combat_scenario(idx, &raw, &armor);
    return mc_combat_final_damage(raw, &armor);
}

#endif /* MC_COMBAT_MATH_H */
