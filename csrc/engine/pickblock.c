/* Pick block, the block half of Minecraft.func_147112_ai:
 *
 *   Block var8 = world.getBlock(x, y, z);  air: nothing
 *   Item var2 = var8.getItem(world, x, y, z);  null: nothing
 *   var4 = var2.getHasSubtypes();
 *   Block var9 = var2 instanceof ItemBlock && !var8.isFlowerPot()
 *              ? Block.getBlockFromItem(var2) : var8;
 *   var3 = var9.getDamageValue(world, x, y, z);
 *
 * getItem's overrides are per block class below; the default is
 * Item.getItemFromBlock (the ItemBlock registered at the block's id).
 * getDamageValue's default is damageDropped of the metadata at (x, y, z)
 * (pick_table.h, generated); the overrides read the world, the tile entity
 * or a constant. The metadata is always the looked-at cell's, even when
 * var9 is another block (a farmland answers through BlockDirt). */
#include "pickblock.h"

#include "items.h"
#include "pick_table.h"
#include "tileentity.h"
#include "world.h"

/* Item.getItemFromBlock: the ItemBlock at the block's id, -1 for none. */
static int item_from_block(int block)
{
    if (block <= 0 || block >= 4096) return -1;
    const struct item_def *d = &ITEMS[block];
    return d->exists && d->kind == ITEM_BLOCK && d->block_id == block ? block : -1;
}

/* Block.getItem(World, x, y, z) per class. */
static int block_get_item(struct world *w, int id, int meta, int x, int y, int z)
{
    switch (id)
    {
    case 26:  return 355;                   /* BlockBed: Items.bed */
    case 34:  return item_from_block((meta & 8) ? 29 : 33);  /* BlockPistonExtension */
    case 36:  return -1;                    /* BlockPistonMoving: getItemById(0) */
    case 43:  return item_from_block(44);   /* BlockSlab: the double's single */
    case 44:  return item_from_block(44);
    case 125: return item_from_block(126);
    case 126: return item_from_block(126);
    case 52:  return -1;                    /* BlockMobSpawner */
    case 55:  return 331;                   /* BlockRedstoneWire: Items.redstone */
    case 59:  return 295;                   /* BlockCrops: func_149866_i, wheat_seeds */
    case 141: return 391;                   /* BlockCarrot: Items.carrot */
    case 142: return 392;                   /* BlockPotato: Items.potato */
    case 60:  return item_from_block(3);    /* BlockFarmland: dirt */
    case 61: case 62: return item_from_block(61);  /* BlockFurnace: furnace */
    case 63: case 68: return 323;           /* BlockSign: Items.sign */
    case 64:  return 324;                   /* BlockDoor, wood: Items.wooden_door */
    case 71:  return 330;                   /* BlockDoor, iron: Items.iron_door */
    case 75: case 76: return item_from_block(76);  /* BlockRedstoneTorch */
    case 83:  return 338;                   /* BlockReed: Items.reeds */
    case 90:  return -1;                    /* BlockPortal */
    case 92:  return 354;                   /* BlockCake: Items.cake */
    case 93: case 94: return 356;           /* BlockRedstoneRepeater */
    case 99:  return item_from_block(39);   /* BlockHugeMushroom: brown_mushroom + type 0 */
    case 100: return item_from_block(40);   /* type 1: red_mushroom */
    case 104: return 361;                   /* BlockStem, pumpkin: pumpkin_seeds */
    case 105: return 362;                   /* BlockStem, melon: melon_seeds */
    case 115: return 372;                   /* BlockNetherWart: Items.nether_wart */
    case 117: return 379;                   /* BlockBrewingStand: Items.brewing_stand */
    case 118: return 380;                   /* BlockCauldron: Items.cauldron */
    case 119: return -1;                    /* BlockEndPortal */
    case 122: return -1;                    /* BlockDragonEgg */
    case 123: case 124: return item_from_block(123);  /* BlockRedstoneLight */
    case 127: return 351;                   /* BlockCocoa: Items.dye */
    case 132: return 287;                   /* BlockTripWire: Items.string */
    case 140:                               /* BlockFlowerPot: the pot's item */
    {
        struct tile_entity *te = world_tile_entity(w, x, y, z);
        return te != NULL && te->kind == TE_FLOWER_POT && te->u.pot.item > 0 ? te->u.pot.item : 390;
    }
    case 144: return 397;                   /* BlockSkull: Items.skull */
    case 149: case 150: return 404;         /* BlockRedstoneComparator */
    default:  return item_from_block(id);
    }
}

/* var9.getDamageValue(World, x, y, z): the metadata is (x, y, z)'s. */
static int block_get_damage(struct world *w, int block, int meta, int x, int y, int z)
{
    switch (block)
    {
    case 3:                                 /* BlockDirt: 1 reads as 0 */
        return meta == 1 ? 0 : meta;
    case 31:                                /* BlockTallGrass */
    case 97:                                /* BlockSilverfish */
        return meta;
    case 43: case 44: case 125: case 126:   /* BlockSlab: damageDropped & 7 */
        return PICK_DAMAGE_DROPPED[block][meta & 15] & 7;
    case 127:                               /* BlockCocoa */
        return 3;
    case 140:                               /* BlockFlowerPot: the pot's data */
    {
        struct tile_entity *te = world_tile_entity(w, x, y, z);
        return te != NULL && te->kind == TE_FLOWER_POT && te->u.pot.item > 0 ? te->u.pot.data : 0;
    }
    case 144:                               /* BlockSkull: the skull's type */
    {
        struct tile_entity *te = world_tile_entity(w, x, y, z);
        return te != NULL && te->kind == TE_SKULL ? te->skull_type : PICK_DAMAGE_DROPPED[144][meta & 15];
    }
    case 161:                               /* BlockNewLeaf */
        return meta & 3;
    case 175:                               /* BlockDoublePlant: the lower half's */
        return (meta & 8) ? world_get_meta(w, x, y - 1, z) & 7 : meta & 7;
    default:
        return block >= 0 && block < 256 ? PICK_DAMAGE_DROPPED[block][meta & 15] : 0;
    }
}

int pick_block_item(struct world *w, int x, int y, int z, int *item, int *damage, int *has_subtypes)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z) & 15;

    /* getMaterial() == Material.air: only air itself */
    if (id == 0) return 0;

    int it = block_get_item(w, id, meta, x, y, z);
    if (it <= 0 || it >= 4096 || !ITEMS[it].exists) return 0;

    const struct item_def *d = &ITEMS[it];
    int var9 = d->kind == ITEM_BLOCK && id != 140 ? d->block_id : id;

    *item = it;
    *has_subtypes = d->has_subtypes != 0;
    *damage = block_get_damage(w, var9, meta, x, y, z);
    return 1;
}
