/* The player-dependent half of 1.7.10 block harvesting. See harvestdrops.h for
 * what is modelled and what is not. The per-block drop table is not repeated
 * here: the block's own dropBlockAsItem call goes through drops.c. That call is
 * always the last thing a case draws from the world Random (checked against the
 * probe's world_draws), so drops.c's seed parameter carries the state in; the
 * one drop that runs before it, BlockDoublePlant's other half, is spelled out
 * below and draws from this file's own stream. */
#include "harvestdrops.h"

#include <string.h>

#include "harvest.h"
#include "items.h"

/* (double)0.2f and (double)0.1f, the constants EntityItem uses; the same pair
 * drops.c spells, because the entity constructor is the same code. */
#define HD_D02 0.20000000298023224
#define HD_D01 0.10000000149011612
#define HD_PI 3.141592653589793

/* Everything the overrides name, resolved from the two registries once. */
struct hd_ids {
    int double_plant, leaves, leaves2, tallgrass, vine, deadbush, ice, snow_layer, skull,
        web, tripwire, tripwire_hook, dirt, grass, farmland, quartz, redstone_lamp, redstone_ore,
        stone_slab, wooden_slab, flower_pot, red_flower, yellow_flower,
        sapling, red_mushroom, brown_mushroom, cactus;
    int ender_chest, glass, iron_bars, glass_pane, stained_glass_pane, stained_glass;
    int shears, snowball, skull_item, flowing_water, material_leaves;
};

static struct hd_ids ids;

static int block_named(const char *name)
{
    return harvest_block_id(name);
}

static int item_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof ITEMS / sizeof ITEMS[0]); ++i)
        if (ITEMS[i].exists && !strcmp(ITEMS[i].name, name)) return i;
    return -1;
}

static int material_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
        if (!strcmp(MATERIALS[i].name, name)) return i;
    return -1;
}

/* before main: the same for every environment */
__attribute__((constructor)) static void init(void)
{
    ids.double_plant = block_named("minecraft:double_plant");
    ids.leaves = block_named("minecraft:leaves");
    ids.leaves2 = block_named("minecraft:leaves2");
    ids.tallgrass = block_named("minecraft:tallgrass");
    ids.vine = block_named("minecraft:vine");
    ids.deadbush = block_named("minecraft:deadbush");
    ids.ice = block_named("minecraft:ice");
    ids.snow_layer = block_named("minecraft:snow_layer");
    ids.skull = block_named("minecraft:skull");
    ids.web = block_named("minecraft:web");
    ids.tripwire = block_named("minecraft:tripwire");
    ids.tripwire_hook = block_named("minecraft:tripwire_hook");
    ids.dirt = block_named("minecraft:dirt");
    ids.grass = block_named("minecraft:grass");
    ids.farmland = block_named("minecraft:farmland");
    ids.quartz = block_named("minecraft:quartz_block");
    ids.redstone_lamp = block_named("minecraft:redstone_lamp");
    ids.redstone_ore = block_named("minecraft:redstone_ore");
    ids.stone_slab = block_named("minecraft:stone_slab");
    ids.wooden_slab = block_named("minecraft:wooden_slab");
    ids.flower_pot = block_named("minecraft:flower_pot");
    ids.red_flower = block_named("minecraft:red_flower");
    ids.yellow_flower = block_named("minecraft:yellow_flower");
    ids.sapling = block_named("minecraft:sapling");
    ids.red_mushroom = block_named("minecraft:red_mushroom");
    ids.brown_mushroom = block_named("minecraft:brown_mushroom");
    ids.cactus = block_named("minecraft:cactus");
    ids.ender_chest = block_named("minecraft:ender_chest");
    ids.glass = block_named("minecraft:glass");
    ids.iron_bars = block_named("minecraft:iron_bars");
    ids.glass_pane = block_named("minecraft:glass_pane");
    ids.stained_glass_pane = block_named("minecraft:stained_glass_pane");
    ids.stained_glass = block_named("minecraft:stained_glass");
    ids.shears = item_named("minecraft:shears");
    ids.snowball = item_named("minecraft:snowball");
    ids.skull_item = item_named("minecraft:skull");
    ids.flowing_water = block_named("minecraft:flowing_water");
    ids.material_leaves = material_named("leaves");
}

/* Block's three fields for the drop the break leaves behind. */
struct hd_stack {
    int item, count, damage;
};

/* The cells, offsets in -1..1. A cell outside the cube reads as air. */
static int cell_index(int dx, int dy, int dz)
{
    return ((dx + 1) * 3 + (dy + 1)) * 3 + (dz + 1);
}

static int in_range(int dx, int dy, int dz)
{
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && dz >= -1 && dz <= 1;
}

static int id_at(const struct hd_cell *g, int dx, int dy, int dz)
{
    return in_range(dx, dy, dz) ? g[cell_index(dx, dy, dz)].id : 0;
}

static int meta_at(const struct hd_cell *g, int dx, int dy, int dz)
{
    return in_range(dx, dy, dz) ? g[cell_index(dx, dy, dz)].meta : 0;
}

/* One break in progress: the cell grid, the player's held stack, the world
 * Random and the shared Math.random stream. */
struct hd_state {
    struct hd_cell g[HD_CELLS];
    int x, y, z;
    int held_item, held_damage, held_count;
    int silk, fortune;
    float exhaustion;
    jrand w;
    jrand *math;
    struct drop_ent *ents;
    int n, cap;
    int bad;
    int stat_mine, stat_use, stat_break;
};

static void set_cell(struct hd_state *s, int dx, int dy, int dz, int id, int meta)
{
    if (!in_range(dx, dy, dz)) return;
    s->g[cell_index(dx, dy, dz)].id = id;
    s->g[cell_index(dx, dy, dz)].meta = meta;
}

static int room(struct hd_state *s)
{
    return s->n < s->cap && s->n < HD_MAX_ENTS;
}

static struct drop_ent *push(struct hd_state *s)
{
    struct drop_ent *e = &s->ents[s->n++];
    memset(e, 0, sizeof *e);
    e->item = -1;
    return e;
}

/* FoodStats.addExhaustion: a float add capped at 40.0F, the way Java's
 * Math.min(float, float) does it. */
static void add_exhaustion(struct hd_state *s, float amount)
{
    float v = s->exhaustion + amount;
    s->exhaustion = v <= 40.0f ? v : 40.0f;
}

/* Block.dropBlockAsItem_do: the three world-Random offsets, then the
 * EntityItem constructor's Math.random draws (hoverStart, rotationYaw,
 * motionX, motionZ). Mirrors drops.c's emit_item, with the stack's count. */
static void drop_item(struct hd_state *s, int dx, int dy, int dz, int item, int count, int damage)
{
    float f = 0.7f;
    double ox, oy, oz;
    struct drop_ent *e;

    if (!room(s)) return;

    ox = (double)(jr_float(&s->w) * f) + (double)(1.0f - f) * 0.5;
    oy = (double)(jr_float(&s->w) * f) + (double)(1.0f - f) * 0.5;
    oz = (double)(jr_float(&s->w) * f) + (double)(1.0f - f) * 0.5;
    e = push(s);

    (void)(float)(jr_double(s->math) * HD_PI * 2.0);   /* hoverStart */

    e->kind = DROP_ITEM;
    e->item = item;
    e->damage = damage;
    e->count = count;
    e->x = (double)(s->x + dx) + ox;
    e->y = (double)(s->y + dy) + oy;
    e->z = (double)(s->z + dz) + oz;
    e->yaw = (float)(jr_double(s->math) * 360.0);
    e->mx = (double)(float)(jr_double(s->math) * HD_D02 - HD_D01);
    e->my = HD_D02;
    e->mz = (double)(float)(jr_double(s->math) * HD_D02 - HD_D01);
}

/* Block.dropBlockAsItem: the per-block table in drops.c. This call is the last
 * thing a case draws from the world Random, so the current state goes in as
 * drops_break's seed and is never read back. */
static void general_drop(struct hd_state *s, int dx, int dy, int dz, int block, int meta, int fortune)
{
    struct drop_case cs;
    memset(&cs, 0, sizeof cs);
    int n;

    /* BlockAir.dropBlockAsItemWithChance is empty: air drops nothing and draws
     * nothing, whatever fortune says. drops.c answers -1 for it, because
     * DropsProbe never reaches it. */
    if (block == 0) return;

    cs.block = block;
    cs.meta = meta;
    cs.fortune = fortune;
    cs.x = s->x + dx;
    cs.y = s->y + dy;
    cs.z = s->z + dz;
    cs.opseed = (int64_t)(s->w.seed ^ 0x5DEECE66DULL);
    cs.math = s->math;

    n = drops_break(&cs, s->ents + s->n, s->cap - s->n);

    if (n < 0)
    {
        s->bad = 1;
        return;
    }

    s->n += n;
}

/* Block.dropBlockAsItem(world, x, y, z, meta, 0) for the double plant:
 * quantityDroppedWithBonus is the inherited 1, so one chance roll, then
 * getItemDropped, which is null for the grass and fern variants. Mirrors
 * drops.c's case 175 without handing the world Random over, because this drop
 * runs before the break's own. */
static void dp_drop(struct hd_state *s, int dx, int dy, int dz, int meta)
{
    int variant = meta & 7;

    jr_float(&s->w);

    if (variant != 2 && variant != 3)
        drop_item(s, dx, dy, dz, drops_block_item(ids.double_plant), 1, variant);
}

/* BlockBush.func_149854_a: the soil a bush sits on. */
static int bush_soil(int block)
{
    return block == ids.grass || block == ids.dirt || block == ids.farmland;
}

/* Block.onNeighborBlockChange for the blocks this context can hold. Only the
 * double plant reacts: BlockBush.onNeighborBlockChange runs func_149855_e,
 * which removes the block when it can no longer stay, and
 * BlockDoublePlant.func_149855_e drops the lower half on the way out. */
static void neighbour_change(struct hd_state *s, int dx, int dy, int dz)
{
    int block = id_at(s->g, dx, dy, dz);
    int meta, stay;

    if (block != ids.double_plant) return;

    meta = meta_at(s->g, dx, dy, dz);

    if ((meta & 8) != 0)
    {
        stay = id_at(s->g, dx, dy - 1, dz) == ids.double_plant;
    }
    else
    {
        stay = id_at(s->g, dx, dy + 1, dz) == ids.double_plant && bush_soil(id_at(s->g, dx, dy - 1, dz));
    }

    if (stay) return;

    if ((meta & 8) == 0)
    {
        dp_drop(s, dx, dy, dz, meta);

        if (id_at(s->g, dx, dy + 1, dz) == ids.double_plant) set_cell(s, dx, dy + 1, dz, 0, 0);
    }

    set_cell(s, dx, dy, dz, 0, 0);
}

/* World.notifyBlocksOfNeighborChange, in its own order. */
static void notify_around(struct hd_state *s, int dx, int dy, int dz)
{
    neighbour_change(s, dx - 1, dy, dz);
    neighbour_change(s, dx + 1, dy, dz);
    neighbour_change(s, dx, dy - 1, dz);
    neighbour_change(s, dx, dy + 1, dz);
    neighbour_change(s, dx, dy, dz - 1);
    neighbour_change(s, dx, dy, dz + 1);
}

/* BlockFlowerPot.createNewTileEntity's switch: the plant a pot carries, by its
 * placement meta, and the damage that plant's item takes. Metas 0, 14 and 15
 * carry nothing. */
static int pot_content(int meta)
{
    switch (meta)
    {
        case 1: return ids.red_flower;
        case 2: return ids.yellow_flower;
        case 3: case 4: case 5: case 6: return ids.sapling;
        case 7: return ids.red_mushroom;
        case 8: return ids.brown_mushroom;
        case 9: return ids.cactus;
        case 10: return ids.deadbush;
        case 11: return ids.tallgrass;
        case 12: case 13: return ids.sapling;
        default: return -1;
    }
}

static int pot_damage(int meta)
{
    switch (meta)
    {
        case 4: return 1;
        case 5: return 2;
        case 6: return 3;
        case 11: return 2;
        case 12: return 4;
        case 13: return 5;
        default: return 0;
    }
}

/* Block.breakBlock, which Chunk.func_150807_a runs when the block is replaced.
 * Only BlockSkull, BlockFlowerPot and BlockTripWireHook leave anything behind
 * here. BlockTripWireHook's 41-block walk meets only air and stone (the probe
 * loads three chunks around the block, see HarvestDropsProbe.RADIUS), so all it
 * does is one world-Random draw for a sound. BlockLeaves and
 * BlockLog only re-mark leaves within a one- or four-wide cube and the context
 * holds none; BlockChest, BlockJukebox, BlockFurnace, BlockHopper,
 * BlockDispenser, BlockBrewingStand and BlockPistonExtension spill or move
 * something they do not have here; the rest only notify neighbours that are air
 * or stone. */
static void break_block(struct hd_state *s, int dx, int dy, int dz, int block, int meta)
{
    if (block == ids.skull)
    {
        /* BlockSkull.breakBlock. The place-and-harvest cycle leaves the tile
         * entity at its default type 0, so getDamageValue is 0 and the
         * SkullOwner branch (type 3) is never taken. */
        if ((meta & 8) == 0)
            drop_item(s, dx, dy, dz, ids.skull_item, 1, 0);

        return;
    }

    if (block == ids.tripwire_hook)
    {
        /* BlockTripWireHook.breakBlock -> func_150136_a, whose 41-block walk
         * meets only air and stone in this context: its var18 stays 0 and its
         * var13 stays false, so it writes no metadata. Its last step,
         * func_150135_a, plays the "random.bowhit" sound (whose pitch is one
         * world-Random nextFloat) exactly when the hook is connected (bit 2) and
         * not powered (bit 3). */
        if ((meta & 4) == 4 && (meta & 8) != 8) jr_float(&s->w);

        return;
    }

    if (block == ids.flower_pot)
    {
        /* BlockFlowerPot.breakBlock: the pot's tile entity is built by
         * createNewTileEntity from the placement meta, so the cell carries the
         * plant the pot was placed with. */
        int content = pot_content(meta);

        if (content >= 0 && ITEMS[content].exists)
            drop_item(s, dx, dy, dz, content, 1, pot_damage(meta));

        return;
    }
}

/* World.setBlockToAir: the old block's breakBlock, then air, then the six
 * neighbours are notified (flag 3). False when the cell is already air with
 * meta 0; air that still carries a meta does change, exactly as
 * Chunk.func_150807_a's id-and-meta comparison says. */
static int set_air_notify(struct hd_state *s, int dx, int dy, int dz)
{
    int old = id_at(s->g, dx, dy, dz);
    int old_meta = meta_at(s->g, dx, dy, dz);

    if (old == 0 && old_meta == 0) return 0;

    break_block(s, dx, dy, dz, old, old_meta);
    set_cell(s, dx, dy, dz, 0, 0);
    notify_around(s, dx, dy, dz);
    return 1;
}

/* World.setBlock(x, y, z, block, 0, 3), the ice-to-water write. */
static void set_block_notify(struct hd_state *s, int dx, int dy, int dz, int block)
{
    break_block(s, dx, dy, dz, id_at(s->g, dx, dy, dz), meta_at(s->g, dx, dy, dz));
    set_cell(s, dx, dy, dz, block, 0);
    notify_around(s, dx, dy, dz);
}

/* Block.canSilkHarvest: renderAsNormalBlock && !isBlockContainer, with the five
 * overrides that answer true outright. blocks.h carries normal_block and
 * tile_entity (the ITileEntityProvider flag BlockContainer sets). */
static int can_silk_harvest(int block)
{
    if (block == ids.ender_chest || block == ids.glass || block == ids.iron_bars
        || block == ids.glass_pane || block == ids.stained_glass_pane
        || block == ids.stained_glass || block == ids.web)
        return 1;

    return BLOCKS[block].normal_block && !BLOCKS[block].tile_entity;
}

/* Block.createStackedBlock and its overrides: the one stack a silk-touch break
 * leaves. */
static void create_stacked_block(int block, int meta, struct hd_stack *out)
{
    switch (block)
    {
        case 3:   /* BlockDirt: the podzol-less meta folds back to 0 */
            if (meta == 1) meta = 0;
            break;

        case 18:  /* BlockLeaves */
        case 161:
            out->item = drops_block_item(block);
            out->count = 1;
            out->damage = meta & 3;
            return;

        case 101: /* BlockPane */
        case 102:
        case 160:
            out->item = drops_block_item(block);
            out->count = 1;
            out->damage = meta;
            return;

        case 155: /* BlockQuartz */
            if (meta == 3 || meta == 4)
            {
                out->item = drops_block_item(ids.quartz);
                out->count = 1;
                out->damage = 2;
                return;
            }
            break;

        case 123: /* BlockRedstoneLight */
        case 124:
            out->item = drops_block_item(ids.redstone_lamp);
            out->count = 1;
            out->damage = 0;
            return;

        case 73:  /* BlockRedstoneOre */
        case 74:
            out->item = drops_block_item(ids.redstone_ore);
            out->count = 1;
            out->damage = 0;
            return;

        case 17:  /* BlockRotatedPillar: func_150162_k is meta & 3 */
        case 162:
        case 170:
            out->item = drops_block_item(block);
            out->count = 1;
            out->damage = meta & 3;
            return;

        case 97:  /* BlockSilverfish: the block the egg imitates */
            out->count = 1;
            out->damage = 0;
            switch (meta)
            {
                case 1: out->item = drops_block_item(4); break;
                case 2: out->item = drops_block_item(98); break;
                case 3: out->item = drops_block_item(98); out->damage = 1; break;
                case 4: out->item = drops_block_item(98); out->damage = 2; break;
                case 5: out->item = drops_block_item(98); out->damage = 3; break;
                default: out->item = drops_block_item(1); break;
            }
            return;

        case 43:  /* BlockStoneSlab: two of the single slab, not one double */
        case 44:
            out->item = drops_block_item(ids.stone_slab);
            out->count = 2;
            out->damage = meta & 7;
            return;

        case 125: /* BlockWoodSlab */
        case 126:
            out->item = drops_block_item(ids.wooden_slab);
            out->count = 2;
            out->damage = meta & 7;
            return;

        default:
            break;
    }

    out->item = drops_block_item(block);
    out->count = 1;
    out->damage = (out->item >= 0 && ITEMS[out->item].has_subtypes) ? meta : 0;
}

void harvestdrops_create_stacked_block(int block, int meta, int *item, int *count, int *damage)
{
    struct hd_stack st;

    create_stacked_block(block, meta, &st);
    *item = st.item;
    *count = st.count;
    *damage = st.damage;
}

/* Block.harvestBlock's own body: the stat, the exhaustion and the silk-touch
 * split. */
static void base_harvest(struct hd_state *s, int dx, int dy, int dz, int block, int meta)
{
    struct hd_stack st;

    if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
    add_exhaustion(s, 0.025f);

    if (can_silk_harvest(block) && s->silk > 0)
    {
        create_stacked_block(block, meta, &st);
        drop_item(s, dx, dy, dz, st.item, st.count, st.damage);
    }
    else
    {
        general_drop(s, dx, dy, dz, block, meta, s->fortune);
    }
}

/* BlockDoublePlant.func_149886_b: shearing the grass or fern double plant drops
 * two tall grass where the caller stands, and returns true. */
static int dp_shear(struct hd_state *s, int dx, int dy, int dz, int meta)
{
    int variant = meta & 7;

    if (variant != 3 && variant != 2) return 0;

    if (harvestdrops_has_mine_stat(ids.double_plant)) s->stat_mine = 1;
    drop_item(s, dx, dy, dz, drops_block_item(ids.tallgrass), 2, variant == 3 ? 2 : 1);
    return 1;
}

/* Block.harvestBlock and its overrides. `block` is the block the break read
 * before it removed it: the cell is air by now. */
static void harvest_block(struct hd_state *s, int dx, int dy, int dz, int block, int meta)
{
    int shears = s->held_item == ids.shears;

    if (block == ids.double_plant)
    {
        /* BlockDoublePlant: the shears keep their own drop, everything else
         * goes to Block.harvestBlock. */
        if (!(shears && (meta & 8) == 0 && dp_shear(s, dx, dy, dz, meta)))
            base_harvest(s, dx, dy, dz, block, meta);

        return;
    }

    if (block == ids.leaves || block == ids.leaves2)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
            drop_item(s, dx, dy, dz, drops_block_item(block), 1, meta & 3);
            return;
        }

        base_harvest(s, dx, dy, dz, block, meta);
        return;
    }

    if (block == ids.tallgrass)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
            drop_item(s, dx, dy, dz, drops_block_item(ids.tallgrass), 1, meta);
            return;
        }

        base_harvest(s, dx, dy, dz, block, meta);
        return;
    }

    if (block == ids.vine)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
            drop_item(s, dx, dy, dz, drops_block_item(ids.vine), 1, 0);
            return;
        }

        base_harvest(s, dx, dy, dz, block, meta);
        return;
    }

    if (block == ids.deadbush)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
            drop_item(s, dx, dy, dz, drops_block_item(ids.deadbush), 1, meta);
            return;
        }

        base_harvest(s, dx, dy, dz, block, meta);
        return;
    }

    if (block == ids.ice)
    {
        /* BlockIce: the stat and the exhaustion come first, exactly as in
         * Block.harvestBlock; the overworld is not a hell world, so the
         * remaining branch drops and then turns the ice to water when the cell
         * below can hold it. */
        if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
        add_exhaustion(s, 0.025f);

        if (can_silk_harvest(block) && s->silk > 0)
        {
            struct hd_stack st;
            create_stacked_block(block, meta, &st);
            drop_item(s, dx, dy, dz, st.item, st.count, st.damage);
            return;
        }

        general_drop(s, dx, dy, dz, block, meta, s->fortune);

        {
            int below = id_at(s->g, dx, dy - 1, dz);
            int m = BLOCKS[below].material;

            if (MATERIALS[m].blocks_movement || MATERIALS[m].is_liquid)
                set_block_notify(s, dx, dy, dz, ids.flowing_water);
        }

        return;
    }

    if (block == ids.snow_layer)
    {
        /* BlockSnow: the snowballs, the block gone, the stat. No exhaustion:
         * the override never calls Block.harvestBlock. */
        int var7 = meta & 7;
        drop_item(s, dx, dy, dz, ids.snowball, var7 + 1, 0);
        set_air_notify(s, dx, dy, dz);

        if (harvestdrops_has_mine_stat(block)) s->stat_mine = 1;
        return;
    }

    base_harvest(s, dx, dy, dz, block, meta);
}

/* ItemStack.attemptDamageItem / damageItem with no Unbreaking enchantment and
 * an unbreakable-free stack: the damage is straight, and passing maxDamage
 * breaks one off the stack and resets the damage. */
static void damage_held(struct hd_state *s, int amount)
{
    int max;

    if (s->held_item == 0) return;

    max = ITEMS[s->held_item].max_damage;
    if (max <= 0) return;

    s->held_damage += amount;

    if (s->held_damage > max)
    {
        s->held_count -= 1;

        if (max > 0) s->stat_break = 1;
        if (s->held_count < 0) s->held_count = 0;
        s->held_damage = 0;
    }
}

/* ItemStack.func_150999_a: the held item's onBlockDestroyed, then the useItem
 * stat when it returned true. */
static void item_on_block_destroyed(struct hd_state *s, int block)
{
    const char *cls;
    double hardness;
    int used = 0;

    if (s->held_item == 0) return;

    cls = ITEMS[s->held_item].class_name;
    hardness = (double)block_hardness(block);

    if (!strcmp(cls, "ItemShears"))
    {
        int mat = BLOCKS[block].material;

        if (mat == ids.material_leaves || block == ids.web || block == ids.tallgrass
            || block == ids.vine || block == ids.tripwire)
        {
            damage_held(s, 1);
            used = 1;
        }
    }
    else if (!strcmp(cls, "ItemSword"))
    {
        if (hardness != 0.0) damage_held(s, 2);
        used = 1;
    }
    else if (ITEMS[s->held_item].kind == ITEM_TOOL)
    {
        if (hardness != 0.0) damage_held(s, 1);
        used = 1;
    }

    if (used && ITEMS[s->held_item].exists) s->stat_use = 1;
}

/* The onBlockHarvested overrides. Creative mode is out of scope (the probe is a
 * survival player), which leaves BlockDoublePlant's other-half handling as the
 * only one in this context that touches the world; BlockTripWire's meta write
 * lands on a block that is removed immediately after and flag 4 notifies
 * nobody, so it is invisible here. */
static void on_block_harvested(struct hd_state *s, int dx, int dy, int dz, int meta)
{
    int block = id_at(s->g, dx, dy, dz);
    int var7, variant;

    if (block != ids.double_plant) return;
    if ((meta & 8) == 0) return;                              /* the lower half does nothing */
    if (id_at(s->g, dx, dy - 1, dz) != ids.double_plant) return;

    var7 = meta_at(s->g, dx, dy - 1, dz);
    variant = var7 & 7;

    if (variant != 3 && variant != 2)
    {
        /* World.func_147480_a(x, y - 1, z, true) */
        dp_drop(s, dx, dy - 1, dz, var7);
        set_air_notify(s, dx, dy - 1, dz);
    }
    else
    {
        if (s->held_item == ids.shears) dp_shear(s, dx, dy, dz, var7);
        set_air_notify(s, dx, dy - 1, dz);
    }
}

/* The stat a vanilla mineBlockStatArray[block] would hold from the block's own
 * item and enableStats alone, before the similar-block pass. */
static int own_mine_stat(int block_id)
{
    static const int off[] = {
        7, 8, 9, 10, 11, 26, 51, 52, 55, 59, 63, 64, 68, 71, 83, 92, 93, 94, 96,
        141, 142, 149, 150
    };

    if (block_id < 0 || block_id >= 4096 || !ITEMS[block_id].exists) return 0;

    for (int i = 0; i < (int)(sizeof off / sizeof off[0]); ++i)
        if (off[i] == block_id) return 0;

    return 1;
}

int harvestdrops_has_mine_stat(int block_id)
{
    /* StatList.replaceAllSimilarBlocks: of each lit/unlit pair whichever has a
     * stat passes it to the other, so the two always answer the same. */
    static const int pair_a[] = {9, 11, 91, 62, 74, 94, 150, 76, 124, 40, 43, 125, 2, 60};
    static const int pair_b[] = {8, 10, 86, 61, 73, 93, 149, 75, 123, 39, 44, 126, 3, 3};

    if (own_mine_stat(block_id)) return 1;
    if (block_id < 0 || block_id >= 4096) return 0;

    for (int i = 0; i < (int)(sizeof pair_a / sizeof pair_a[0]); ++i)
    {
        if (pair_b[i] == block_id && own_mine_stat(pair_a[i])) return 1;
        if (pair_a[i] == block_id && own_mine_stat(pair_b[i])) return 1;
    }

    return 0;
}

int harvestdrops_break(const struct hd_case *cs, struct hd_result *out)
{
    struct hd_state s;
    struct harvest_player hp;
    int block, meta, changed, can_harvest;

    memset(out, 0, sizeof *out);
    memset(&s, 0, sizeof s);
    memcpy(s.g, cs->cells, sizeof s.g);
    s.x = cs->x;
    s.y = cs->y;
    s.z = cs->z;
    s.held_item = cs->held_item;
    s.held_damage = cs->held_damage;
    s.held_count = cs->held_count;
    s.silk = cs->silk;
    s.fortune = cs->fortune;
    s.exhaustion = cs->exhaustion;
    jr_seed(&s.w, cs->opseed);
    s.math = cs->math;
    s.ents = out->ents;
    s.cap = HD_MAX_ENTS;

    block = id_at(s.g, 0, 0, 0);
    meta = meta_at(s.g, 0, 0, 0);

    /* ItemInWorldManager.tryHarvestBlock, survival branch. playAuxSFXAtEntity
     * leaves no state, and onBlockDestroyedByPlayer is empty for every block
     * the probe runs (BlockSilverfish, the one override, is excluded). */
    on_block_harvested(&s, 0, 0, 0, meta);
    changed = set_air_notify(&s, 0, 0, 0);

    hp.held_item = s.held_item;
    hp.held_enchant = 0;
    hp.haste = 0;
    hp.fatigue = 0;
    hp.on_ground = 1;
    can_harvest = can_harvest_block(&hp, block);

    if (s.held_item != 0) item_on_block_destroyed(&s, block);

    /* EntityPlayer.destroyCurrentEquippedItem when the stack broke. */
    if (s.held_item != 0 && s.held_count == 0)
    {
        s.held_item = 0;
        s.held_damage = 0;
    }

    if (changed && can_harvest) harvest_block(&s, 0, 0, 0, block, meta);

    memcpy(out->cells, s.g, sizeof out->cells);
    out->held_item = s.held_item;
    out->held_damage = s.held_damage;
    out->held_count = s.held_count;
    out->exhaustion = (float)s.exhaustion;
    out->stat_mine = s.stat_mine;
    out->stat_use = s.stat_use;
    out->stat_break = s.stat_break;
    out->nents = s.n;
    out->bad = s.bad;
    return s.bad ? -1 : 0;
}