/* player_break: progressive block break (SURVIVAL dig progress + harvest) for MC 1.11.2.
 * One MC_HD source, CPU==CUDA, verbatim-Java golden (oracle/goldens/player_break/Golden.java).
 *
 * Sources (java/oracle-src/net/minecraft + forge):
 *   server/management/PlayerInteractionManager.java
 *     onBlockClicked (159-247): creative instant tryHarvest; survival f>=1.0F instant harvest,
 *       else isDestroyingBlock + sendBlockBreakProgress from f.
 *     updateBlockRemoving (97-153): curblockDamage++, progress =
 *       getPlayerRelativeBlockHardness * (ticksHeld + 1); harvest when f>=1.0F.
 *   block/Block.java getPlayerRelativeBlockHardness (672-675) -> ForgeHooks.blockStrength.
 *   net/minecraftforge/common/ForgeHooks.java blockStrength (199-215):
 *     hardness<0 -> 0; else digSpeed/hardness/(canHarvest?30:100).
 *   entity/player/EntityPlayer.java getDigSpeed (887-942):
 *     inventory.getStrVsBlock; efficiency (lvl*lvl+1) if f>1; haste * (1+(amp+1)*0.2);
 *     mining fatigue * {0.3,0.09,0.0027,8.1E-4}; water w/o aqua /5; !onGround /5.
 *   entity/player/InventoryPlayer.java getStrVsBlock (574-584) / canHarvestBlock (751-762).
 *   item/ItemPickaxe.java getStrVsBlock (76-80) ROCK/IRON/ANVIL -> efficiencyOnProperMaterial;
 *     canHarvestBlock (22-74) harvest-level gates for ores + Material.ROCK true.
 *   item/Item.ToolMaterial efficiencyOnProperMaterial (WOOD2 STONE4 IRON6 DIAMOND8 GOLD12).
 *   block/material/Material.java ROCK requires tool; GROUND/GRASS tool-not-required.
 *
 * Model (client-equivalent accumulation; constant hardness => same as server hardness*(k+1)):
 *   start progress=0; each tick progress += relative_hardness; harvest when progress>=1.
 *   Creative: one tick sets progress=1 and harvest=1 (PlayerInteractionManager creative path).
 *
 * READ-ONLY deps: items_tools_armor.h (tool mat/efficiency/canHarvest), block_props_table.h
 * (hardness). Efficiency/haste/fatigue/water/airborne are explicit int inputs (not live potion map). */
#ifndef MC_PLAYER_BREAK_H
#define MC_PLAYER_BREAK_H

#include "mc.h"
#include "mc_blocks.h"
#include "block_props_table.h"
#include "items_tools_armor.h"

/* Default ticks per scenario (hand-vs-stone needs 150 ticks at 1/150 per tick). */
#ifndef PB_NTICKS
#define PB_NTICKS 160
#endif

/* Item ids (1.11.2). */
#define PB_HAND           0
#define PB_WOODEN_PICKAXE 270
#define PB_IRON_PICKAXE   257

/* Scenario input: everything the hardness formula needs is explicit (no World/player objects). */
typedef struct {
    i32 block_id;
    i32 block_meta;      /* carried for API parity; hardness path ignores meta for battery blocks */
    i32 tool_id;         /* 0 = empty hand */
    i32 tool_meta;
    i32 efficiency;      /* EnchantmentHelper.getEfficiencyModifier level; 0 = none */
    i32 haste_amp;       /* -1 = inactive; else MobEffects.HASTE amplifier (0-indexed) */
    i32 fatigue_amp;     /* -1 = inactive; else MINING_FATIGUE amplifier */
    i32 in_water;        /* isInsideOfMaterial(WATER) */
    i32 aqua_affinity;   /* EnchantmentHelper.getAquaAffinityModifier; only matters in water */
    i32 on_ground;
    i32 creative;        /* GameType creative: instant harvest, no hardness math */
} PbInput;

/* ---- canHarvest (ForgeHooks.canHarvestBlock + InventoryPlayer.canHarvestBlock subset) ---- */

/* Material.ROCK / ore blocks require a tool; dirt/grass/sand do not (isToolNotRequired). */
MC_HD static inline int pb_requires_tool(int block_id) {
    if (block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_IRON_ORE
        || block_id == BLK_COAL_ORE || block_id == BLK_GOLD_ORE || block_id == BLK_DIAMOND_ORE
        || block_id == BLK_OBSIDIAN || block_id == BLK_LAPIS_ORE || block_id == 73 || block_id == 74)
        return 1;
    return 0;
}

/* Survival canHarvest for battery blocks: tool-not-required always true; rock/ore needs pickaxe
 * harvest level via ita_pickaxe_can_harvest (ItemPickaxe.canHarvestBlock + Forge harvest level). */
MC_HD static inline int pb_can_harvest(i32 tool_id, int block_id) {
    if (!pb_requires_tool(block_id)) return 1;
    if (tool_id == PB_HAND) return 0;
    if (!ita_is_pickaxe(tool_id)) return 0;
    int mat = ita_tool_material(tool_id);
    if (mat < 0) return 0;
    return ita_pickaxe_can_harvest(mat, block_id);
}

/* InventoryPlayer.getStrVsBlock / ItemPickaxe.getStrVsBlock: hand=1; pick on rock/ore = tool eff. */
MC_HD static inline float pb_str_vs_block(i32 tool_id, int block_id) {
    if (tool_id == PB_HAND) return 1.0f;
    return ita_tool_dig_speed(tool_id, block_id);
}

/* EntityPlayer.getDigSpeed (887-942), potions/enchants/water/air as explicit ints. */
MC_HD static inline float pb_get_dig_speed(const PbInput *in) {
    float f = pb_str_vs_block(in->tool_id, in->block_id);

    /* efficiency only when base dig speed > 1 (proper tool) and held item non-empty */
    if (f > 1.0f) {
        i32 i = in->efficiency;
        if (i > 0 && in->tool_id != PB_HAND)
            f += (float)(i * i + 1);
    }

    if (in->haste_amp >= 0)
        f *= 1.0f + (float)(in->haste_amp + 1) * 0.2f;

    if (in->fatigue_amp >= 0) {
        float f1;
        switch (in->fatigue_amp) {
            case 0:  f1 = 0.3f;     break;
            case 1:  f1 = 0.09f;    break;
            case 2:  f1 = 0.0027f;  break;
            default: f1 = 8.1e-4f;  break; /* case 3+ */
        }
        f *= f1;
    }

    if (in->in_water && !in->aqua_affinity)
        f /= 5.0f;

    if (!in->on_ground)
        f /= 5.0f;

    return (f < 0.0f) ? 0.0f : f;
}

/* ForgeHooks.blockStrength / getPlayerRelativeBlockHardness. Creative bypasses this. */
MC_HD static inline float pb_relative_hardness(const PbInput *in) {
    float hardness = mc_bpt_props(in->block_id).hardness;
    if (hardness < 0.0f) return 0.0f;
    float dig = pb_get_dig_speed(in);
    if (!pb_can_harvest(in->tool_id, in->block_id))
        return dig / hardness / 100.0f;
    return dig / hardness / 30.0f;
}

/* One progressive-dig tick step. If already harvested, freezes progress (no further add). */
MC_HD static inline void pb_tick(const PbInput *in, float *progress, int *harvested) {
    if (*harvested) return;
    if (in->creative) {
        *progress = 1.0f;
        *harvested = 1;
        return;
    }
    *progress += pb_relative_hardness(in);
    if (*progress >= 1.0f)
        *harvested = 1;
}

/* ---- scenario battery ------------------------------------------------------- */
/* Fixed table of scenarios (task list). Each runs PB_NTICKS ticks of progressive dig. */
#define PB_NSCENARIOS 12

MC_HD static inline PbInput pb_scenario(int si) {
    /* defaults: survival, grounded, dry, no potions, no enchants */
    PbInput in;
    in.block_id = BLK_STONE;
    in.block_meta = 0;
    in.tool_id = PB_HAND;
    in.tool_meta = 0;
    in.efficiency = 0;
    in.haste_amp = -1;
    in.fatigue_amp = -1;
    in.in_water = 0;
    in.aqua_affinity = 0;
    in.on_ground = 1;
    in.creative = 0;

    switch (si) {
        case 0: /* hand vs stone */
            break;
        case 1: /* wooden pick vs stone */
            in.tool_id = PB_WOODEN_PICKAXE;
            break;
        case 2: /* iron pick vs stone */
            in.tool_id = PB_IRON_PICKAXE;
            break;
        case 3: /* hand vs dirt */
            in.block_id = BLK_DIRT;
            break;
        case 4: /* wrong tool: wooden pick vs iron ore (harvest level 0 < 1) */
            in.tool_id = PB_WOODEN_PICKAXE;
            in.block_id = BLK_IRON_ORE;
            break;
        case 5: /* underwater, no aqua: iron pick vs stone */
            in.tool_id = PB_IRON_PICKAXE;
            in.in_water = 1;
            in.aqua_affinity = 0;
            break;
        case 6: /* airborne: iron pick vs stone, !onGround */
            in.tool_id = PB_IRON_PICKAXE;
            in.on_ground = 0;
            break;
        case 7: /* haste amplifier 0 (Haste I): iron pick vs stone */
            in.tool_id = PB_IRON_PICKAXE;
            in.haste_amp = 0;
            break;
        case 8: /* haste amplifier 1 (Haste II) */
            in.tool_id = PB_IRON_PICKAXE;
            in.haste_amp = 1;
            break;
        case 9: /* mining fatigue amplifier 0 */
            in.tool_id = PB_IRON_PICKAXE;
            in.fatigue_amp = 0;
            break;
        case 10: /* mining fatigue amplifier 1 */
            in.tool_id = PB_IRON_PICKAXE;
            in.fatigue_amp = 1;
            break;
        case 11: /* creative instant-break (one tick harvest) */
            in.creative = 1;
            break;
        default:
            break;
    }
    return in;
}

/* Emit helpers: progress float raw bits + harvest 0/1 as u64 for hex dump. */
MC_HD static inline u32 pb_float_bits(float v) {
    union { float f; u32 u; } u;
    u.f = v;
    return u.u;
}

/* Run full battery: for each scenario, nticks of (progress, harvest). out length = NS * nticks * 2. */
MC_HD static inline void pb_run_battery(int nticks, u64 *out) {
    int k = 0;
    for (int si = 0; si < PB_NSCENARIOS; ++si) {
        PbInput in = pb_scenario(si);
        float progress = 0.0f;
        int harvested = 0;
        for (int t = 0; t < nticks; ++t) {
            pb_tick(&in, &progress, &harvested);
            out[k++] = (u64)pb_float_bits(progress);
            out[k++] = (u64)(u32)harvested;
        }
    }
}

#define PB_NOUT(nticks) (PB_NSCENARIOS * (nticks) * 2)

#endif /* MC_PLAYER_BREAK_H */
