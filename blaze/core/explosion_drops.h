/* explosion_drops.h - Explosion.doExplosionB block drops (server).
 *
 * Java 1.11.2:
 *   Explosion.doExplosionB                 Explosion.java:209-246
 *     canDropFromExplosion                 Block.java:1069 / BlockTNT.java:147
 *     dropBlockAsItemWithChance            Block.java:688-703 (Forge)
 *       getDrops                           Block.java:1505-1520
 *       quantityDropped(state,fortune,rand) Block.java:1491-1493
 *         -> quantityDroppedWithBonus      Block.java:922-924 (fortune 0)
 *       getItemDropped / damageDropped     per Block subclass
 *       world.rand.nextFloat() <= chance   Block.java:698
 *     spawnAsEntity                        Block.java:709-725
 *       three nextFloat * 0.5F + 0.25D     :719-721
 *       EntityItem ctor                    EntityItem.java:51-62
 *         xz motion Math.random()          :59-61  CLASS C: live table
 *           zeros mx/my/mz (cu_spawn_item / live_fill_ent memset)
 *       setDefaultPickupDelay 10           EntityItem.java:564-566
 *     onBlockExploded                      Block.java:1730-1733
 *       setBlockToAir then
 *       onBlockDestroyedByExplosion        BlockTNT.java:68-74 (chain fuse)
 *
 * XP from BlockOre.getExpDrop / dropXpOnBlockBreak is harvestBlock only
 * (Block.java:731, BlockOre.java:76-99). doExplosionB does not call it.
 *
 * EntityItem table cap 48 (GM_LIVE_MAX / CU_MAX_ITEMS) is a shared sim cap:
 * spawnAsEntity draws the three offset floats then skips if the table is
 * full. Java has no such cap.
 */
#ifndef MC_EXPLOSION_DROPS_H
#define MC_EXPLOSION_DROPS_H

#include "mc.h"
#include "mc_blocks.h"
#include "mc_rng.h"

#define EXL_ITEM_FLINT 318
#define EXL_ITEM_APPLE 260
#define EXL_ITEM_COAL 263
#define EXL_ITEM_DIAMOND 264
#define EXL_ITEM_REDSTONE 331
#define EXL_ITEM_SNOWBALL 332
#define EXL_ITEM_DYE 351
#define EXL_ITEM_EMERALD 388
#define EXL_LEAVES2 161
#define EXL_REDSTONE_ORE_LIT 74
#define EXL_DROP_STACKS 16
#define EXL_PICKUP_DELAY 10 /* EntityItem.setDefaultPickupDelay :564-566 */

typedef struct {
    int item, count, meta;
} ExlStack;

MC_HD static inline int exl_can_drop_from_explosion(int id) {
    /* BlockTNT.java:147 false; Block.java:1069 true. */
    return id != BLK_TNT; /* BlockTNT.java:147; id 46 */
}

MC_HD static inline int exl_is_air_id(int id) { return id <= 0; }

/* BlockOldLeaf.getSaplingDropChance :46 jungle 40 else 20. */
MC_HD static inline int exl_sapling_chance(int id, int meta) {
    if (id == BLK_LEAVES && (meta & 3) == 3) return 40;
    return 20;
}

MC_HD static inline int exl_quantity_dropped(int id, int meta, JavaRandom *r) {
    /* Block.quantityDropped(state, 0, rand) -> quantityDroppedWithBonus(0,). */
    switch (id) {
    case BLK_GLASS:
        return 0; /* BlockGlass.java:23-24 */
    case BLK_SNOW_LAYER:
        /* Forge BlockSnow.java:178 LAYERS+1; LAYERS=(meta&7)+1. */
        return (meta & 7) + 2;
    case BLK_LAPIS_ORE:
        return 4 + jrand_int_bound(r, 5); /* BlockOre.java:43 */
    case BLK_REDSTONE_ORE:
    case EXL_REDSTONE_ORE_LIT:
        /* quantityDropped 4+nextInt(2) then + nextInt(fortune+1)=nextInt(1)
         * (BlockRedstoneOre.java:97-107). */
        return 4 + jrand_int_bound(r, 2) + jrand_int_bound(r, 1);
    case 89: /* glowstone: 2+nextInt(3) + nextInt(1), clamp 1..4 */
    {
        int n = 2 + jrand_int_bound(r, 3) + jrand_int_bound(r, 1);
        if (n < 1) n = 1;
        if (n > 4) n = 4;
        return n;
    }
    default:
        return 1;
    }
}

MC_HD static inline void exl_item_dropped(int id, int meta, JavaRandom *r,
                                          int *item, int *imeta) {
    *item = 0;
    *imeta = 0;
    switch (id) {
    case BLK_STONE:
        /* BlockStone.java:50-52, :59-61 */
        if ((meta & 7) == 0) {
            *item = BLK_COBBLESTONE;
            *imeta = 0;
        } else {
            *item = BLK_STONE;
            *imeta = meta;
        }
        break;
    case BLK_GRASS:
        *item = BLK_DIRT; /* BlockGrass.java:78-80 */
        break;
    case BLK_DIRT:
        *item = BLK_DIRT;
        /* BlockDirt.java:98-107 podzol -> dirt meta 0 */
        *imeta = ((meta & 3) == 2) ? 0 : (meta & 3);
        break;
    case BLK_GRAVEL:
        /* BlockGravel.java:16-23 fortune 0: nextInt(10)==0 flint. */
        *item = (jrand_int_bound(r, 10) == 0) ? EXL_ITEM_FLINT : BLK_GRAVEL;
        break;
    case BLK_SAND:
        *item = BLK_SAND;
        *imeta = meta & 1;
        break;
    case BLK_COAL_ORE:
        *item = EXL_ITEM_COAL;
        break;
    case BLK_IRON_ORE:
        *item = BLK_IRON_ORE;
        break;
    case BLK_GOLD_ORE:
        *item = BLK_GOLD_ORE;
        break;
    case BLK_DIAMOND_ORE:
        *item = EXL_ITEM_DIAMOND;
        break;
    case BLK_LAPIS_ORE:
        *item = EXL_ITEM_DYE;
        *imeta = 4; /* EnumDyeColor.BLUE */
        break;
    case BLK_EMERALD_ORE:
        *item = EXL_ITEM_EMERALD;
        break;
    case BLK_REDSTONE_ORE:
    case EXL_REDSTONE_ORE_LIT:
        *item = EXL_ITEM_REDSTONE;
        break;
    case BLK_SNOW_LAYER:
        *item = EXL_ITEM_SNOWBALL;
        break;
    case BLK_LEAVES:
        *item = BLK_SAPLING;
        *imeta = meta & 3; /* BlockOldLeaf.damageDropped :110-112 */
        break;
    case EXL_LEAVES2:
        *item = BLK_SAPLING;
        *imeta = (meta & 3) + 4; /* BlockNewLeaf.damageDropped :49-51 */
        break;
    case BLK_LOG:
        *item = BLK_LOG;
        *imeta = meta & 3; /* BlockOldLog.damageDropped :139-141 */
        break;
    case BLK_PLANKS:
        *item = BLK_PLANKS;
        *imeta = meta & 3;
        break;
    case BLK_GLASS:
        *item = 0;
        break;
    default:
        *item = id;
        *imeta = 0;
        break;
    }
}

/* Block.getDrops / BlockLeaves.getDrops. Returns stack count. */
MC_HD static inline int exl_get_drops(int id, int meta, JavaRandom *r,
                                      ExlStack *out, int maxn) {
    int n = 0, i, count, item, imeta;
    if (!r || !out || maxn <= 0) return 0;
    if (id == BLK_LEAVES || id == EXL_LEAVES2) {
        /* BlockLeaves.java:275-305. fortune 0. */
        int chance = exl_sapling_chance(id, meta);
        if (jrand_int_bound(r, chance) == 0) {
            exl_item_dropped(id, meta, r, &item, &imeta);
            if (item > 0 && n < maxn) {
                out[n].item = item;
                out[n].count = 1;
                out[n].meta = imeta;
                ++n;
            }
        }
        chance = 200;
        /* dropApple: oak (old variant 0) or dark oak (new variant 1). */
        if (((id == BLK_LEAVES && (meta & 3) == 0) ||
             (id == EXL_LEAVES2 && (meta & 3) == 1)) &&
            jrand_int_bound(r, chance) == 0) {
            if (n < maxn) {
                out[n].item = EXL_ITEM_APPLE;
                out[n].count = 1;
                out[n].meta = 0;
                ++n;
            }
        }
        return n;
    }
    count = exl_quantity_dropped(id, meta, r);
    for (i = 0; i < count && n < maxn; ++i) {
        exl_item_dropped(id, meta, r, &item, &imeta);
        if (item <= 0) continue;
        out[n].item = item;
        out[n].count = 1;
        out[n].meta = imeta;
        ++n;
    }
    return n;
}

#endif /* MC_EXPLOSION_DROPS_H */
