/* items_tools_armor: ItemTool harvest/canHarvest + ItemArmor constants + durability decrement +
 * protection/unbreaking enchant apply subset + CombatRules armor absorb (internal CPU==CUDA).
 * PORT: ItemTool.getHarvestLevel/canHarvestBlock/onBlockDestroyed/hitEntity, ItemArmor.ArmorMaterial,
 * ItemStack.attemptDamageItem, EnchantmentProtection.calcModifierDamage, CombatRules.
 * READ-ONLY: items_core.h (ICStack ids), block_props_table.h (hardness), combat_math.h (stub).
 * Subset: wood/stone/iron/diamond/gold pickaxes; leather/chain/iron/gold/diamond armor slots;
 * protection types ALL/FIRE/FALL/EXPLOSION/PROJECTILE; unbreaking; no creative/NBT/leather dye. */
#ifndef MC_ITEMS_TOOLS_ARMOR_H
#define MC_ITEMS_TOOLS_ARMOR_H

#include "mc.h"
#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "items_core.h"
#include "combat_math.h"

enum {
    ITA_SLOT_FEET  = 0,
    ITA_SLOT_LEGS  = 1,
    ITA_SLOT_CHEST = 2,
    ITA_SLOT_HEAD  = 3
};

enum {
    ITA_MAT_WOOD    = 0,
    ITA_MAT_STONE   = 1,
    ITA_MAT_IRON    = 2,
    ITA_MAT_DIAMOND = 3,
    ITA_MAT_GOLD    = 4
};

enum {
    ITA_ARM_LEATHER = 0,
    ITA_ARM_CHAIN   = 1,
    ITA_ARM_IRON    = 2,
    ITA_ARM_GOLD    = 3,
    ITA_ARM_DIAMOND = 4
};

enum {
    ITA_PROT_ALL       = 0,
    ITA_PROT_FIRE      = 1,
    ITA_PROT_FALL      = 2,
    ITA_PROT_EXPLOSION = 3,
    ITA_PROT_PROJECTILE = 4
};

enum {
    ITA_DS_GENERIC   = 1,
    ITA_DS_FIRE      = 2,
    ITA_DS_FALL      = 4,
    ITA_DS_EXPLOSION = 8,
    ITA_DS_PROJECTILE = 16
};

typedef struct {
    i32 item;
    i32 count;
    i32 meta;
    i32 damage;
    i32 prot_all;
    i32 prot_fire;
    i32 prot_fall;
    i32 prot_blast;
    i32 prot_projectile;
    i32 unbreaking;
    i32 efficiency;
} ITAStack;

MC_HD static inline ITAStack ita_mk(i32 item, i32 dmg) {
    ITAStack s;
    s.item = item; s.count = 1; s.meta = 0; s.damage = dmg;
    s.prot_all = s.prot_fire = s.prot_fall = s.prot_blast = s.prot_projectile = 0;
    s.unbreaking = s.efficiency = 0;
    return s;
}

MC_HD static inline float ita_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* CombatRules.getDamageAfterAbsorb / getDamageAfterMagicAbsorb (combat_math stub; local copy). */
MC_HD static inline float ita_damage_after_absorb(float damage, float total_armor, float toughness) {
    float f = 2.0f + toughness / 4.0f;
    float f1 = ita_clampf(total_armor - damage / f, total_armor * 0.2f, 20.0f);
    return damage * (1.0f - f1 / 25.0f);
}

MC_HD static inline float ita_damage_after_magic_absorb(float damage, float prot_pts) {
    float f = ita_clampf(prot_pts, 0.0f, 20.0f);
    return damage * (1.0f - f / 25.0f);
}

MC_HD static inline int ita_tool_material(i32 item_id) {
    switch (item_id) {
        case 268: case 269: case 270: case 271: case 290:
            return ITA_MAT_WOOD;
        case 272: case 273: case 274: case 275: case 291:
            return ITA_MAT_STONE;
        case 256: case 257: case 258: case 267: case 292:
            return ITA_MAT_IRON;
        case 276: case 277: case 278: case 279: case 293:
            return ITA_MAT_DIAMOND;
        case 283: case 284: case 285: case 286: case 294:
            return ITA_MAT_GOLD;
        default: return -1;
    }
}

MC_HD static inline int ita_tool_harvest_level_mat(int mat) {
    switch (mat) {
        case ITA_MAT_WOOD:  return 0;
        case ITA_MAT_STONE: return 1;
        case ITA_MAT_IRON:  return 2;
        case ITA_MAT_DIAMOND: return 3;
        case ITA_MAT_GOLD:  return 0;
        default: return -1;
    }
}

MC_HD static inline int ita_tool_max_uses(int mat) {
    switch (mat) {
        case ITA_MAT_WOOD:    return 59;
        case ITA_MAT_STONE:   return 131;
        case ITA_MAT_IRON:    return 250;
        case ITA_MAT_DIAMOND: return 1561;
        case ITA_MAT_GOLD:    return 32;
        default: return 0;
    }
}

MC_HD static inline float ita_tool_efficiency(int mat) {
    switch (mat) {
        case ITA_MAT_WOOD:    return 2.0f;
        case ITA_MAT_STONE:   return 4.0f;
        case ITA_MAT_IRON:    return 6.0f;
        case ITA_MAT_DIAMOND: return 8.0f;
        case ITA_MAT_GOLD:    return 12.0f;
        default: return 1.0f;
    }
}

MC_HD static inline int ita_is_pickaxe(i32 item_id) {
    return item_id == 270 || item_id == 274 || item_id == 257 || item_id == 278 || item_id == 285;
}

MC_HD static inline int ita_block_harvest_tool(int block_id) {
    if (block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_IRON_ORE
        || block_id == BLK_COAL_ORE || block_id == BLK_OBSIDIAN || block_id == BLK_GOLD_ORE
        || block_id == BLK_DIAMOND_ORE || block_id == BLK_LAPIS_ORE)
        return 1; /* pickaxe */
    if (block_id == BLK_DIRT || block_id == BLK_GRASS || block_id == BLK_SAND)
        return 2; /* shovel */
    return 0;
}

MC_HD static inline int ita_block_harvest_level(int block_id) {
    if (block_id == BLK_OBSIDIAN) return 3;
    if (block_id == BLK_IRON_ORE || block_id == BLK_LAPIS_ORE) return 1;
    if (block_id == BLK_GOLD_ORE || block_id == BLK_DIAMOND_ORE) return 2;
    if (block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_COAL_ORE) return 0;
    return 0;
}

MC_HD static inline int ita_block_is_tool_effective(int block_id, const char *tool_class) {
    if (tool_class[0] != 'p') return 0;
    if (block_id == BLK_OBSIDIAN || block_id == 73 || block_id == 74) return 0; /* obsidian/redstone ore */
    return ita_block_harvest_tool(block_id) == 1;
}

MC_HD static inline int ita_pickaxe_can_harvest(int mat, int block_id) {
    int hl = ita_tool_harvest_level_mat(mat);
    if (block_id == BLK_OBSIDIAN) return hl == 3;
    if (block_id == BLK_DIAMOND_ORE || block_id == 57) return hl >= 2;
    if (block_id == BLK_GOLD_ORE || block_id == 41) return hl >= 2;
    if (block_id == BLK_IRON_ORE || block_id == 42) return hl >= 1;
    if (block_id == BLK_LAPIS_ORE || block_id == 21) return hl >= 1;
    if (block_id == 73 || block_id == 74) return hl >= 2;
    if (block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_COAL_ORE
        || block_id == BLK_IRON_ORE)
        return 1;
    return 0;
}

MC_HD static inline int ita_tool_get_harvest_level(i32 tool_id, int tool_kind, int block_id) {
    (void)block_id;
    if (tool_kind != ita_block_harvest_tool(block_id)) return -1;
    int mat = ita_tool_material(tool_id);
    if (mat < 0) return -1;
    if (ita_is_pickaxe(tool_id) && tool_kind == 1)
        return ita_tool_harvest_level_mat(mat);
    return -1;
}

MC_HD static inline int ita_is_shovel(i32 item_id) {
    return item_id == 269 || item_id == 273 || item_id == 256 || item_id == 277 || item_id == 284;
}

/* ItemSpade.EFFECTIVE_ON (item/ItemSpade.java): CLAY, DIRT, FARMLAND, GRASS,
 * GRAVEL, MYCELIUM, SAND, SNOW, SNOW_LAYER, SOUL_SAND, GRASS_PATH. */
MC_HD static inline int ita_shovel_effective_on(int block_id) {
    return block_id == 82 || block_id == BLK_DIRT || block_id == 60
        || block_id == BLK_GRASS || block_id == 13 || block_id == 110
        || block_id == BLK_SAND || block_id == 80 || block_id == 78
        || block_id == 88 || block_id == 208;
}

MC_HD static inline int ita_is_axe(i32 item_id) {
    return item_id == 271 || item_id == 275 || item_id == 258 || item_id == 279 || item_id == 286;
}

/* ItemAxe.getStrVsBlock (item/ItemAxe.java): material WOOD/PLANTS/VINE ->
 * efficiencyOnProperMaterial, else super (EFFECTIVE_ON: PLANKS, BOOKSHELF,
 * LOG, LOG2, CHEST, PUMPKIN, LIT_PUMPKIN, MELON_BLOCK, LADDER, WOODEN_BUTTON,
 * WOODEN_PRESSURE_PLATE). Route-relevant union of both paths. */
MC_HD static inline int ita_axe_effective_on(int block_id) {
    return block_id == 5 || block_id == 47 || block_id == 17 || block_id == 162
        || block_id == 54 || block_id == 86 || block_id == 91 || block_id == 103
        || block_id == 65 || block_id == 143 || block_id == 72
        /* material WOOD extras a player actually chops */
        || block_id == 58 || block_id == 85 || block_id == 107 || block_id == 96
        || block_id == 64 || block_id == 63 || block_id == 68;
}

MC_HD static inline float ita_tool_dig_speed(i32 tool_id, int block_id) {
    int mat = ita_tool_material(tool_id);
    if (mat < 0) return 1.0f;
    /* ItemTool.getStrVsBlock: EFFECTIVE_ON.contains(block) ? efficiencyOnProperMaterial : 1.0F */
    if (ita_is_shovel(tool_id))
        return ita_shovel_effective_on(block_id) ? ita_tool_efficiency(mat) : 1.0f;
    if (ita_is_axe(tool_id))
        return ita_axe_effective_on(block_id) ? ita_tool_efficiency(mat) : 1.0f;
    if (!ita_is_pickaxe(tool_id)) return 1.0f;
    if (ita_block_is_tool_effective(block_id, "pickaxe"))
        return ita_tool_efficiency(mat);
    /* ItemPickaxe EFFECTIVE_ON subset used by battery blocks */
    if (block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_IRON_ORE
        || block_id == BLK_COAL_ORE)
        return ita_tool_efficiency(mat);
    return 1.0f;
}

MC_HD static inline float ita_tool_dig_speed_enchant(i32 tool_id, int block_id, int eff_lvl) {
    float base = ita_tool_dig_speed(tool_id, block_id);
    if (eff_lvl <= 0 || base <= 1.0f) return base;
    return base + (float)(eff_lvl * eff_lvl + 1);
}

MC_HD static inline int ita_armor_material(i32 item_id) {
    if (item_id >= 298 && item_id <= 301) return ITA_ARM_LEATHER;
    if (item_id >= 302 && item_id <= 305) return ITA_ARM_CHAIN;
    if (item_id >= 306 && item_id <= 309) return ITA_ARM_IRON;
    if (item_id >= 314 && item_id <= 317) return ITA_ARM_GOLD;
    if (item_id >= 310 && item_id <= 313) return ITA_ARM_DIAMOND;
    return -1;
}

MC_HD static inline int ita_armor_slot(i32 item_id) {
    switch (item_id) {
        case 298: case 302: case 306: case 310: case 314: return ITA_SLOT_HEAD;
        case 299: case 303: case 307: case 311: case 315: return ITA_SLOT_CHEST;
        case 300: case 304: case 308: case 312: case 316: return ITA_SLOT_LEGS;
        case 301: case 305: case 309: case 313: case 317: return ITA_SLOT_FEET;
        default: return -1;
    }
}

MC_HD static inline int ita_arm_max_damage_factor(int arm_mat) {
    switch (arm_mat) {
        case ITA_ARM_LEATHER: return 5;
        case ITA_ARM_CHAIN:   return 15;
        case ITA_ARM_IRON:    return 15;
        case ITA_ARM_GOLD:    return 7;
        case ITA_ARM_DIAMOND: return 33;
        default: return 0;
    }
}

MC_HD static inline int ita_max_dmg_slot(int slot) {
    switch (slot) {
        case ITA_SLOT_FEET:  return 13;
        case ITA_SLOT_LEGS:  return 15;
        case ITA_SLOT_CHEST: return 16;
        case ITA_SLOT_HEAD:  return 11;
        default: return 0;
    }
}

MC_HD static inline int ita_arm_max_damage(int arm_mat, int slot) {
    return ita_max_dmg_slot(slot) * ita_arm_max_damage_factor(arm_mat);
}

MC_HD static inline int ita_arm_damage_reduce(int arm_mat, int slot) {
    switch (arm_mat) {
        case ITA_ARM_LEATHER:
            switch (slot) { case ITA_SLOT_HEAD: return 1; case ITA_SLOT_CHEST: return 3;
                case ITA_SLOT_LEGS: return 2; case ITA_SLOT_FEET: return 1; default: return 0; }
        case ITA_ARM_CHAIN:
            switch (slot) { case ITA_SLOT_HEAD: return 1; case ITA_SLOT_CHEST: return 4;
                case ITA_SLOT_LEGS: return 5; case ITA_SLOT_FEET: return 2; default: return 0; }
        case ITA_ARM_IRON:
            switch (slot) { case ITA_SLOT_HEAD: return 2; case ITA_SLOT_CHEST: return 6;
                case ITA_SLOT_LEGS: return 5; case ITA_SLOT_FEET: return 2; default: return 0; }
        case ITA_ARM_GOLD:
            switch (slot) { case ITA_SLOT_HEAD: return 1; case ITA_SLOT_CHEST: return 3;
                case ITA_SLOT_LEGS: return 5; case ITA_SLOT_FEET: return 2; default: return 0; }
        case ITA_ARM_DIAMOND:
            switch (slot) { case ITA_SLOT_HEAD: return 3; case ITA_SLOT_CHEST: return 8;
                case ITA_SLOT_LEGS: return 6; case ITA_SLOT_FEET: return 3; default: return 0; }
        default: return 0;
    }
}

MC_HD static inline float ita_arm_toughness(int arm_mat) {
    return arm_mat == ITA_ARM_DIAMOND ? 2.0f : 0.0f;
}

MC_HD static inline int ita_stack_max_damage(const ITAStack *s) {
    int mat = ita_tool_material(s->item);
    if (mat >= 0) return ita_tool_max_uses(mat);
    int am = ita_armor_material(s->item);
    if (am >= 0) return ita_arm_max_damage(am, ita_armor_slot(s->item));
    if (s->item == 359) return 238; /* Items.SHEARS maxDamage */
    if (s->item == 346) return 64;  /* Items.FISHING_ROD maxDamage */
    if (s->item == 398) return 25;  /* Items.CARROT_ON_A_STICK maxDamage */
    if (s->item == 443) return 432; /* Items.ELYTRA maxDamage */
    return 0;
}

MC_HD static inline int ita_stack_is_damageable(const ITAStack *s) {
    return ita_stack_max_damage(s) > 0;
}

MC_HD static inline int ita_negate_unbreaking(const ITAStack *s, int unbreaking_lvl, JavaRandom *rng) {
    if (unbreaking_lvl <= 0) return 0;
    if (ita_armor_material(s->item) >= 0) {
        if (jrand_float(rng) < 0.6f) return 0;
        return jrand_int_bound(rng, unbreaking_lvl + 1) > 0 ? 1 : 0;
    }
    return jrand_int_bound(rng, unbreaking_lvl + 1) > 0 ? 1 : 0;
}

MC_HD static inline int ita_attempt_damage(ITAStack *s, int amount, JavaRandom *rng) {
    if (!ita_stack_is_damageable(s) || amount <= 0) return 0;
    int ub = s->unbreaking;
    int neg = 0;
    for (int k = 0; ub > 0 && k < amount; ++k)
        neg += ita_negate_unbreaking(s, ub, rng);
    amount -= neg;
    if (amount <= 0) return 0;
    s->damage += amount;
    return s->damage > ita_stack_max_damage(s);
}

MC_HD static inline void ita_on_block_destroyed(ITAStack *s, int block_id) {
    BptProps p = mc_bpt_props(block_id);
    if (s->item == 359 || p.hardness != 0.0f)
        s->damage += 1;
}

MC_HD static inline void ita_hit_entity(ITAStack *s) {
    s->damage += 2;
}

MC_HD static inline int ita_armor_set_points(const ITAStack slots[4]) {
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        int am = ita_armor_material(slots[i].item);
        if (am >= 0) sum += ita_arm_damage_reduce(am, ita_armor_slot(slots[i].item));
    }
    return sum;
}

MC_HD static inline float ita_armor_set_toughness(const ITAStack slots[4]) {
    float t = 0.0f;
    for (int i = 0; i < 4; ++i) {
        int am = ita_armor_material(slots[i].item);
        if (am >= 0) t += ita_arm_toughness(am);
    }
    return t;
}

/* InventoryPlayer.damageArmor: raw/4 (min 1) per ItemArmor piece; elytra excluded. */
MC_HD static inline void ita_damage_armor_set(ITAStack slots[4], float raw_damage) {
    float d = raw_damage / 4.0f;
    int amount;
    if (d < 1.0f) d = 1.0f;
    amount = (int)d;
    for (int i = 0; i < 4; ++i) {
        if (ita_armor_material(slots[i].item) < 0) continue;
        if (!ita_stack_is_damageable(&slots[i]) || amount <= 0) continue;
        slots[i].damage += amount;
        if (slots[i].damage > ita_stack_max_damage(&slots[i])) {
            slots[i].item = 0;
            slots[i].count = 0;
            slots[i].damage = 0;
        }
    }
}

/* CombatRules absorb when source is not unblockable. */
MC_HD static inline float ita_apply_armor_absorb(float damage, const ITAStack slots[4],
                                                  int unblockable) {
    if (unblockable || damage <= 0.0f) return damage;
    return ita_damage_after_absorb(damage, (float)ita_armor_set_points(slots),
                                   ita_armor_set_toughness(slots));
}

MC_HD static inline int ita_prot_calc_modifier(int level, int prot_type, int ds_flags) {
    if (level <= 0) return 0;
    if (prot_type == ITA_PROT_ALL) return level;
    if (prot_type == ITA_PROT_FIRE && (ds_flags & ITA_DS_FIRE)) return level * 2;
    if (prot_type == ITA_PROT_FALL && (ds_flags & ITA_DS_FALL)) return level * 3;
    if (prot_type == ITA_PROT_EXPLOSION && (ds_flags & ITA_DS_EXPLOSION)) return level * 2;
    if (prot_type == ITA_PROT_PROJECTILE && (ds_flags & ITA_DS_PROJECTILE)) return level * 2;
    return 0;
}

MC_HD static inline int ita_enchant_prot_modifier(const ITAStack slots[4], int ds_flags) {
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        const ITAStack *s = &slots[i];
        sum += ita_prot_calc_modifier(s->prot_all, ITA_PROT_ALL, ds_flags);
        sum += ita_prot_calc_modifier(s->prot_fire, ITA_PROT_FIRE, ds_flags);
        sum += ita_prot_calc_modifier(s->prot_fall, ITA_PROT_FALL, ds_flags);
        sum += ita_prot_calc_modifier(s->prot_blast, ITA_PROT_EXPLOSION, ds_flags);
        sum += ita_prot_calc_modifier(s->prot_projectile, ITA_PROT_PROJECTILE, ds_flags);
    }
    return sum;
}

#define ITA_NOUT 19

MC_HD static inline void ita_emit_u32(u64 *out, int *k, u32 v) {
    out[(*k)++] = (u64)v;
}

MC_HD static inline void ita_emit_f32(u64 *out, int *k, float v) {
    union { float f; u32 u; } u;
    u.f = v;
    out[(*k)++] = (u64)u.u;
}

MC_HD static inline void ita_run_battery(u64 *out) {
    int k = 0;

    /* harvest levels */
    ita_emit_u32(out, &k, (u32)ita_tool_get_harvest_level(274, 1, BLK_STONE));
    ita_emit_u32(out, &k, (u32)ita_tool_get_harvest_level(278, 1, BLK_OBSIDIAN));
    ita_emit_u32(out, &k, (u32)ita_pickaxe_can_harvest(ITA_MAT_STONE, BLK_OBSIDIAN));
    ita_emit_u32(out, &k, (u32)ita_pickaxe_can_harvest(ITA_MAT_DIAMOND, BLK_OBSIDIAN));
    ita_emit_u32(out, &k, (u32)ita_pickaxe_can_harvest(ITA_MAT_WOOD, BLK_IRON_ORE));

    /* dig speed + efficiency enchant */
    ita_emit_f32(out, &k, ita_tool_dig_speed(274, BLK_STONE));
    ita_emit_f32(out, &k, ita_tool_dig_speed(274, BLK_OBSIDIAN));
    ita_emit_f32(out, &k, ita_tool_dig_speed_enchant(274, BLK_STONE, 3));

    /* armor constants: iron chest max damage + reduce, full iron set armor */
    ita_emit_u32(out, &k, (u32)ita_arm_max_damage(ITA_ARM_IRON, ITA_SLOT_CHEST));
    ita_emit_u32(out, &k, (u32)ita_arm_damage_reduce(ITA_ARM_IRON, ITA_SLOT_CHEST));
    {
        ITAStack iron_set[4] = {
            ita_mk(306, 0), ita_mk(307, 0), ita_mk(308, 0), ita_mk(309, 0)
        };
        ita_emit_u32(out, &k, (u32)ita_armor_set_points(iron_set));
        ita_emit_f32(out, &k, ita_damage_after_absorb(20.0f, (float)ita_armor_set_points(iron_set), 0.0f));
    }

    /* diamond set + toughness absorb */
    {
        ITAStack dia_set[4] = {
            ita_mk(310, 0), ita_mk(311, 0), ita_mk(312, 0), ita_mk(313, 0)
        };
        float armor = (float)ita_armor_set_points(dia_set);
        float tough = ita_armor_set_toughness(dia_set);
        ita_emit_f32(out, &k, ita_damage_after_absorb(20.0f, armor, tough));
    }

    /* protection enchant: prot IV chest + fire II boots, generic vs fire */
    {
        ITAStack prot_set[4] = { ita_mk(0, 0), ita_mk(0, 0), ita_mk(0, 0), ita_mk(0, 0) };
        prot_set[2] = ita_mk(307, 0);
        prot_set[2].prot_all = 4;
        prot_set[0] = ita_mk(309, 0);
        prot_set[0].prot_fire = 2;
        int mod_gen = ita_enchant_prot_modifier(prot_set, ITA_DS_GENERIC);
        int mod_fire = ita_enchant_prot_modifier(prot_set, ITA_DS_FIRE);
        ita_emit_u32(out, &k, (u32)mod_gen);
        ita_emit_u32(out, &k, (u32)mod_fire);
        ita_emit_f32(out, &k, ita_damage_after_magic_absorb(10.0f, (float)mod_fire));
    }

    /* durability: block break + entity hit */
    {
        ITAStack pick = ita_mk(274, 0);
        ita_on_block_destroyed(&pick, BLK_STONE);
        ita_hit_entity(&pick);
        ita_emit_u32(out, &k, (u32)pick.damage);
    }

    /* unbreaking III stone pick: 10 block breaks with seed 12345 */
    {
        ITAStack pick = ita_mk(274, 0);
        pick.unbreaking = 3;
        JavaRandom rng;
        jrand_set(&rng, 12345LL);
        for (int i = 0; i < 10; ++i)
            (void)ita_attempt_damage(&pick, 1, &rng);
        ita_emit_u32(out, &k, (u32)pick.damage);
    }

    /* iron chest unbreaking II: 5 hits (armor 60% no-negate gate uses rng) */
    {
        ITAStack chest = ita_mk(307, 0);
        chest.unbreaking = 2;
        JavaRandom rng;
        jrand_set(&rng, 7LL);
        for (int i = 0; i < 5; ++i)
            (void)ita_attempt_damage(&chest, 1, &rng);
        ita_emit_u32(out, &k, (u32)chest.damage);
    }
}

#endif /* MC_ITEMS_TOOLS_ARMOR_H */
