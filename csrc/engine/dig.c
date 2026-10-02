#include "dig.h"
#include "env.h"
#include "spill.h"

#include "activate.h"
#include "blockcb.h"
#include "blocks.h"
#include "harvest.h"
#include "harvestdrops.h"
#include "items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIG_MATH_SEED 0x6d6174684f7468LL

/* (double)0.2f and (double)0.1f, the constants the EntityItem constructor
 * spreads its motion with; the same pair drops.c and blockcb.c spell. */
#define D02 0.20000000298023224
#define D01 0.10000000149011612

/* The ids and classes the machine names, resolved from the registries once. */
static struct
{
    int stone, dirt, sand, gold_ore, iron_ore, coal_ore, lapis_ore, diamond_ore, redstone_ore,
        lit_redstone_ore, log, log2, leaves, leaves2, glass, ice, tallgrass, wheat, farmland,
        torch, door, bed, double_plant, chest, spawner, bedrock, flowing_water, air;
    int furnace, lit_furnace;
    int shears, web, vine, tripwire;
    int snow_layer, snowball, deadbush;
    int material_leaves;
} ids;

static int block_named(const char *name)
{
    int b = harvest_block_id(name);

    if (b < 0)
    {
        fprintf(stderr, "dig: no block %s\n", name);
        exit(2);
    }

    return b;
}

static int item_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof ITEMS / sizeof ITEMS[0]); ++i)
        if (ITEMS[i].exists && !strcmp(ITEMS[i].name, name)) return i;

    fprintf(stderr, "dig: no item %s\n", name);
    exit(2);
}

/* before main: the same for every environment */
__attribute__((constructor)) static void init(void)
{
    ids.stone = block_named("minecraft:stone");
    ids.snow_layer = block_named("minecraft:snow_layer");
    ids.snowball = item_named("minecraft:snowball");
    ids.deadbush = block_named("minecraft:deadbush");
    ids.dirt = block_named("minecraft:dirt");
    ids.sand = block_named("minecraft:sand");
    ids.gold_ore = block_named("minecraft:gold_ore");
    ids.iron_ore = block_named("minecraft:iron_ore");
    ids.coal_ore = block_named("minecraft:coal_ore");
    ids.lapis_ore = block_named("minecraft:lapis_ore");
    ids.diamond_ore = block_named("minecraft:diamond_ore");
    ids.redstone_ore = block_named("minecraft:redstone_ore");
    ids.lit_redstone_ore = block_named("minecraft:lit_redstone_ore");
    ids.log = block_named("minecraft:log");
    ids.log2 = block_named("minecraft:log2");
    ids.leaves = block_named("minecraft:leaves");
    ids.leaves2 = block_named("minecraft:leaves2");
    ids.glass = block_named("minecraft:glass");
    ids.ice = block_named("minecraft:ice");
    ids.tallgrass = block_named("minecraft:tallgrass");
    ids.wheat = block_named("minecraft:wheat");
    ids.farmland = block_named("minecraft:farmland");
    ids.torch = block_named("minecraft:torch");
    ids.door = block_named("minecraft:wooden_door");
    ids.bed = block_named("minecraft:bed");
    ids.double_plant = block_named("minecraft:double_plant");
    ids.chest = block_named("minecraft:chest");
    ids.furnace = block_named("minecraft:furnace");
    ids.lit_furnace = block_named("minecraft:lit_furnace");
    ids.spawner = block_named("minecraft:mob_spawner");
    ids.bedrock = block_named("minecraft:bedrock");
    ids.flowing_water = block_named("minecraft:flowing_water");
    ids.air = block_named("minecraft:air");
    ids.shears = item_named("minecraft:shears");
    ids.web = block_named("minecraft:web");
    ids.vine = block_named("minecraft:vine");
    ids.tripwire = block_named("minecraft:tripwire");
    ids.material_leaves = BLOCKS[ids.leaves].material;
}

static void destroy_partial(dig_env *d, int x, int y, int z, int stage)
{
    if (d->partial != NULL) d->partial(d->partial_ctx, d->case_index, d->p.entity_id, x, y, z, stage);
}

/* FoodStats.addExhaustion: the float add capped at 40.0F. */
static void add_exhaustion(dig_env *d, float amount)
{
    float v = d->p.exhaustion + amount;

    if (v > 40.0f) v = 40.0f;
    d->p.exhaustion = v;
}

/* One item entity: the EntityItem constructor's Math.random draws (hoverStart,
 * rotationYaw, motionX, motionZ; motionY is the constant 0.2f) plus the Entity
 * boilerplate (per-role id, the entity's Random seed and UUID). When
 * world_offsets is on, the three position floats come from the world Random,
 * the shape dropBlockAsItem_do and the double plant's sheared pair share. */
static void spawn_item(dig_env *d, double x, double y, double z, int item, int damage, int count,
                       int world_offsets)
{
    if (d->nents >= DIG_MAX_ENTS) return;

    double jx = 0.0, jy = 0.0, jz = 0.0;

    if (world_offsets)
    {
        float f = 0.7f;

        jx = (double)(jr_float(&d->wr) * f) + (double)(1.0f - f) * 0.5;
        jy = (double)(jr_float(&d->wr) * f) + (double)(1.0f - f) * 0.5;
        jz = (double)(jr_float(&d->wr) * f) + (double)(1.0f - f) * 0.5;
    }

    dig_ent *e = &d->ents[d->nents++];
    e->spill = 0;
    e->kind = 1;   /* the record kind: 1 item, 2 xp orb */
    e->item = item >= 0 ? item : 0xffff;
    e->damage = damage;
    e->count = count;
    e->xp = 0;
    e->x = x + jx;
    e->y = y + jy;
    e->z = z + jz;

    e->entity_id = det_next_entity_id_role(d->det, d->role);
    det_rng entity_rand = det_new_random_role(d->det, d->role);
    e->rand_state = entity_rand.r.seed;
    int64_t msb, lsb;
    det_uuid_role(d->det, d->role, &msb, &lsb);
    e->uuid_msb = msb;
    e->uuid_lsb = lsb;

    e->hover = (float)(det_math_random_role(d->det, d->role) * 3.141592653589793 * 2.0);
    e->yaw = (float)(det_math_random_role(d->det, d->role) * 360.0);
    e->mx = (double)(float)(det_math_random_role(d->det, d->role) * D02 - D01);
    e->my = D02;
    e->mz = (double)(float)(det_math_random_role(d->det, d->role) * D02 - D01);
}

/* blockcb.c's item_drop callback: a pop's drop (the door item) lands here with
 * its draws already spent in the world Random, the Math stream and the Det
 * boilerplate. */
static void pop_item_drop(void *ctx, int entity_id, uint64_t rand_state, int64_t msb, int64_t lsb, double x, double y,
                          double z, float yaw, float hover, double mx, double mz, int item,
                          int damage, int count)
{
    dig_env *d = (dig_env *)ctx;

    if (d->nents >= DIG_MAX_ENTS) return;

    dig_ent *e = &d->ents[d->nents++];
    e->spill = 0;
    e->kind = 1;
    e->item = item;
    e->damage = damage;
    e->count = count;
    e->xp = 0;
    e->x = x;
    e->y = y;
    e->z = z;
    e->mx = mx;
    e->my = D02;
    e->mz = mz;
    e->yaw = yaw;
    e->entity_id = entity_id;
    e->rand_state = rand_state;
    e->uuid_msb = msb;
    e->uuid_lsb = lsb;
    e->hover = hover;
}

/* The world Random's state, handed to drops_break as a seed and read back. */
static void drop_case_for(dig_env *d, int block, int meta, int fortune, int x, int y, int z)
{
    struct drop_case cs;

    memset(&cs, 0, sizeof cs);
    cs.block = block;
    cs.meta = meta;
    cs.fortune = fortune;
    cs.x = x;
    cs.y = y;
    cs.z = z;
    cs.opseed = (int64_t)(d->wr.seed ^ 0x5DEECE66DULL);
    cs.math = &d->det->math[d->role].r;
    cs.out_seed = (uint64_t *)&d->wr.seed;

    struct drop_ent *out ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *out);
    int n = drops_break(&cs, out, DROPS_MAX_ENTS);

    for (int i = 0; i < n && d->nents < DIG_MAX_ENTS; ++i)
    {
        dig_ent *e = &d->ents[d->nents++];
        e->spill = 0;
        e->kind = out[i].kind + 1;
        e->item = out[i].kind == DROP_XP ? 0 : out[i].item;
        e->damage = out[i].damage;
        e->count = out[i].count;
        e->xp = out[i].xp;
        e->x = out[i].x;
        e->y = out[i].y;
        e->z = out[i].z;
        e->mx = out[i].mx;
        e->my = out[i].my;
        e->mz = out[i].mz;
        e->yaw = out[i].yaw;
        e->entity_id = det_next_entity_id_role(d->det, d->role);
        det_rng entity_rand = det_new_random_role(d->det, d->role);
        e->rand_state = entity_rand.r.seed;
        int64_t msb, lsb;
        det_uuid_role(d->det, d->role, &msb, &lsb);
        e->uuid_msb = msb;
        e->uuid_lsb = lsb;
        e->hover = out[i].kind == DROP_ITEM ? out[i].hover : 0.0f;
    }
}

/* spill.c's sink: one spilled stack as a record, delayBeforeCanPickup 0. */
static void spill_record(void *ctx, const struct spill_item *s)
{
    dig_env *d = (dig_env *)ctx;

    if (d->nents >= DIG_MAX_ENTS) return;

    dig_ent *e = &d->ents[d->nents++];
    e->spill = 0;
    e->kind = 1;   /* the record kind: 1 item, 2 xp orb */
    e->item = s->item;
    e->damage = s->damage;
    e->count = s->count;
    e->xp = 0;
    e->x = s->x;
    e->y = s->y;
    e->z = s->z;
    e->mx = s->mx;
    e->my = s->my;
    e->mz = s->mz;
    e->hover = s->hover;
    e->yaw = s->yaw;
    e->entity_id = s->entity_id;
    e->rand_state = s->rand_state;
    e->uuid_msb = s->uuid_msb;
    e->uuid_lsb = s->uuid_lsb;
    e->tag = s->tag;
    e->spill = 1;
}

/* BlockSkull.breakBlock: unless the creative bit (8) is set, one skull whose
 * damage is getDamageValue, the tile entity's type (a player head's owner
 * tag is not modelled), through dropBlockAsItem_do. Its
 * dropBlockAsItemWithChance is empty, so the harvest adds nothing. */
static void skull_drops(dig_env *d, int x, int y, int z, int meta)
{
    if ((meta & 8) != 0) return;

    struct tile_entity *te = world_tile_entity(d->w, x, y, z);
    int damage = te != NULL && te->kind == TE_SKULL ? te->skull_type : meta;

    spawn_item(d, (double)x, (double)y, (double)z, 397, damage, 1, 1);
}

/* breakBlock's inventory spill (spill.c) for the chest, the furnace and the
 * other containers. The dig probe hands the chest's Random over as a state
 * (d->chest); everything else, and the chest in the live replay
 * (d->block_rands), draws the block singleton's own (block_rand). BlockFurnace.breakBlock's field_149934_M (set only while
 * func_149931_a swaps furnace and lit furnace) is false on a break;
 * func_147453_f then tells the comparators beside it, which no dig site
 * holds. */
static void container_drops(dig_env *d, int block, int x, int y, int z)
{
    det_rng *r = !d->block_rands && (block & 4095) == ids.chest ? &d->chest : NULL;

    container_spill(d->w, x, y, z, block, r, d->det, d->role, spill_record, d);
}

/* The player the hardness queries see. */
static struct harvest_player hp_of(dig_env *d)
{
    struct harvest_player hp;

    hp.held_item = d->p.held_item;
    hp.held_enchant = d->p.eff;
    hp.haste = d->p.haste;
    hp.fatigue = d->p.fatigue;
    hp.on_ground = d->p.on_ground;
    hp.in_water = d->p.in_water;
    return hp;
}

/* The held stack's damage: ItemStack.attemptDamageItem and damageItem, with
 * the Unbreaking negation drawing from the player's own Random. No armor is
 * ever held, so negateDamage's ItemArmor half is never entered. */
static void damage_held(dig_env *d, int amount)
{
    if (d->p.held_item == 0) return;

    int max = ITEMS[d->p.held_item].max_damage;

    if (max <= 0) return;



    if (amount > 0 && d->p.unbr > 0)
    {
        int negated = 0;

        for (int i = 0; i < amount; ++i)
        {
            if (jr_int_n(&d->p.prand, d->p.unbr + 1) > 0) ++negated;
        }

        amount -= negated;
        if (amount <= 0) return;
    }


    d->p.held_damage += amount;

    if (d->p.held_damage > max)
    {
        /* EntityLivingBase.renderBrokenItemStack, called before the stack
         * size drops: the break sound's pitch draws one world-Random float
         * at the call site (Entity.playSound fans out to the access list,
         * which records no sound), then five iconcrack particles draw three
         * floats each from the player's Random and one Det.math draw each
         * and reach no world state. */
        (void)jr_float(&d->wr);

        for (int i = 0; i < 5; ++i)
        {
            (void)jr_float(&d->p.prand);
            (void)det_math_random_role(d->det, d->role);
            (void)jr_float(&d->p.prand);
            (void)jr_float(&d->p.prand);
        }

        --d->p.held_count;
        d->p.stat_break = 1;
        d->p.held_damage = 0;

        if (d->p.held_count < 0) d->p.held_count = 0;
    }
}

/* The onBlockDestroyed overrides of the held item, then the useItem stat. */
static void item_on_block_destroyed(dig_env *d, int block)
{
    int used = 0;

    if (d->p.held_item == 0) return;

    const char *cls = ITEMS[d->p.held_item].class_name;

    if (!strcmp(cls, "ItemShears"))
    {
        if (BLOCKS[block].material == ids.material_leaves || block == ids.web
            || block == ids.tallgrass || block == ids.vine || block == ids.tripwire)
        {
            damage_held(d, 1);
            used = 1;
        }
    }
    else if (!strcmp(cls, "ItemSword"))
    {
        if (block_hardness(block) != 0.0f) damage_held(d, 2);
        used = 1;
    }
    else if (ITEMS[d->p.held_item].kind == ITEM_TOOL)
    {
        if (block_hardness(block) != 0.0f) damage_held(d, 1);
        used = 1;
    }

    if (used && ITEMS[d->p.held_item].exists) d->p.stat_use = 1;
}

/* Block.canSilkHarvest: renderAsNormalBlock && !isBlockContainer, with the
 * overrides that answer true outright. */
static int can_silk_harvest(int block)
{
    /* BlockGlass, BlockStainedGlass, BlockPane (glass and stained panes,
     * iron bars), BlockEnderChest and BlockWeb answer true outright */
    if (block == ids.glass || block == 95 || block == 101 || block == 102 || block == 160 ||
        block == 130 || block == 30)
        return 1;

    return BLOCKS[block].normal_block && !BLOCKS[block].tile_entity;
}

/* Block.harvestBlock's own body, against the real world: the stat, the
 * exhaustion, then the silk split or the general drop. */
static void dig_fish(dig_env *d, double x, double y, double z);

static void base_harvest(dig_env *d, int block, int meta)
{
    if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
    add_exhaustion(d, 0.025f);

    if (can_silk_harvest(block) && d->p.silk > 0)
    {
        int item, count, damage;

        harvestdrops_create_stacked_block(block, meta, &item, &count, &damage);
        spawn_item(d, (double)d->x, (double)d->y, (double)d->z, item, damage, count, 1);
    }
    else
    {
        /* BlockSilverfish.dropBlockAsItemWithChance (reached through the
         * non-silk dropBlockAsItem): the harvest's own fish, in place of the
         * (empty) drop */
        if (block == 97)
        {
            dig_fish(d, (double)d->x + 0.5, (double)d->y, (double)d->z + 0.5);
        }
        else
        {
            drop_case_for(d, block, meta, d->p.fortune, d->x, d->y, d->z);
        }
    }
}

/* BlockDoublePlant.func_149886_b: shearing the grass or fern double plant
 * drops two tall grass where the block stands, and returns true. */
static int dp_shear(dig_env *d, int x, int y, int z, int meta)
{
    int variant = meta & 7;

    if (variant != 3 && variant != 2) return 0;

    if (harvestdrops_has_mine_stat(ids.double_plant)) d->p.stat_mine = 1, d->p.stat_mine_block = ids.double_plant;
    spawn_item(d, (double)x, (double)y, (double)z, drops_block_item(ids.tallgrass),
               variant == 3 ? 2 : 1, 2, 1);
    return 1;
}

/* One BlockSilverfish fish spawn's identity draws (EntitySilverfish(World)'s
 * entity id, Random seed and UUID, all on the machine's role); the living
 * itself is constructed by the caller's adopt (survival.c) from the record. */
static void dig_fish(dig_env *d, double x, double y, double z)
{
    if (d->nents >= DIG_MAX_ENTS) return;

    dig_ent *e = &d->ents[d->nents++];
    e->spill = 0;
    e->kind = 3;
    e->item = 0;
    e->damage = 0;
    e->count = 0;
    e->xp = 0;
    e->x = x;
    e->y = y;
    e->z = z;
    e->mx = 0.0;
    e->my = 0.0;
    e->mz = 0.0;
    e->yaw = 0.0F;
    e->hover = 0.0F;
    e->entity_id = det_next_entity_id_role(d->det, d->role);
    /* the record carries the EXTERNAL seed (the seeder long Java's Born
     * ctor takes); det_rng_set_seed scrambles it again on adopt */
    int64_t entity_seed = det_seeder_next_long(d->det, d->role);
    det_rng entity_rand;
    det_rng_set_seed(&entity_rand, entity_seed);
    e->rand_state = entity_seed;
    int64_t msb, lsb;
    det_uuid_role(d->det, d->role, &msb, &lsb);
    e->uuid_msb = msb;
    e->uuid_lsb = lsb;
    /* EntityLivingBase's constructor draws (field_70770_ap, field_70769_ao,
     * rotationYaw) come before a silk-touch drop's Math.random motion */
    e->fish_math = d->det->math[d->role];
    for (int i = 0; i < 3; ++i) (void)det_math_random_role(d->det, d->role);
}


/* Block.harvestBlock and the overrides the dig region reaches. */
static void harvest_block(dig_env *d, int block, int meta)
{
    int shears = d->p.held_item == ids.shears;

    if (block == ids.double_plant)
    {
        if (!(shears && (meta & 8) == 0 && dp_shear(d, d->x, d->y, d->z, meta)))
            base_harvest(d, block, meta);

        return;
    }

    if (block == ids.leaves || block == ids.leaves2)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
            spawn_item(d, (double)d->x, (double)d->y, (double)d->z, drops_block_item(block),
                       meta & 3, 1, 1);
            return;
        }

        base_harvest(d, block, meta);
        return;
    }

    if (block == ids.tallgrass)
    {
        if (shears)
        {
            if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
            spawn_item(d, (double)d->x, (double)d->y, (double)d->z,
                       drops_block_item(ids.tallgrass), meta, 1, 1);
            return;
        }

        base_harvest(d, block, meta);
        return;
    }

    /* BlockVine.harvestBlock and BlockDeadBush.harvestBlock: shears keep
     * the block (the vine at damage 0, the bush at its metadata) with the
     * stat and no exhaustion; anything else is Block.harvestBlock */
    if ((block == ids.vine || block == ids.deadbush) && shears)
    {
        if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
        spawn_item(d, (double)d->x, (double)d->y, (double)d->z, drops_block_item(block), block == ids.vine ? 0 : meta,
                   1, 1);
        return;
    }

    if (block == ids.snow_layer)
    {
        /* BlockSnow.harvestBlock: the (meta & 7) + 1 snowballs through
         * dropBlockAsItem_do, setBlockToAir on the cell tryHarvestBlock has
         * already cleared, the stat; it never reaches Block.harvestBlock, so
         * no exhaustion */
        spawn_item(d, (double)d->x, (double)d->y, (double)d->z, ids.snowball, 0, (meta & 7) + 1, 1);
        world_set_block(d->w, d->x, d->y, d->z, 0, 0, 3);
        if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
        return;
    }

    if (block == ids.ice)
    {
        /* BlockIce: the stat and the exhaustion come first, exactly as in
         * Block.harvestBlock; in a hell world (the Nether) the rest is
         * setBlockToAir on the cell removeBlock already emptied, a write of
         * nothing; elsewhere it drops and then turns the ice to water when
         * the cell below can hold it. */
        if (harvestdrops_has_mine_stat(block)) d->p.stat_mine = 1, d->p.stat_mine_block = block;
        add_exhaustion(d, 0.025f);

        if (can_silk_harvest(block) && d->p.silk > 0)
        {
            int item, count, damage;
            harvestdrops_create_stacked_block(block, meta, &item, &count, &damage);
            spawn_item(d, (double)d->x, (double)d->y, (double)d->z, item, damage, count, 1);
            return;
        }

        if (d->w->dim == -1) return;

        drop_case_for(d, block, meta, d->p.fortune, d->x, d->y, d->z);

        int below = world_get_block(d->w, d->x, d->y - 1, d->z) & 4095;
        int m = BLOCKS[below].material;

        if (MATERIALS[m].blocks_movement || MATERIALS[m].is_liquid)
            world_set_block(d->w, d->x, d->y, d->z, ids.flowing_water, 0, 3);

        return;
    }

    base_harvest(d, block, meta);
}

/* ItemInWorldManager.tryHarvestBlock, the survival body. */
static void try_harvest_block(dig_env *d, int x, int y, int z)
{
    struct world *w = d->w;
    int block = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);

    /* the client's own prediction emptied the cell before this server-side
     * harvest: the drop reads the block as the machine clicked it, and the
     * cell's write counts as done (the oracle's server write of air is the
     * cell's end state) */
    int predicted = 0;

    if (block == ids.air && d->m.initial_id != ids.air
        && x == d->m.part_x && y == d->m.part_y && z == d->m.part_z)
    {
        block = d->m.initial_id;
        meta = d->m.initial_meta;
        d->m.initial_id = ids.air;
        predicted = 1;
    }

    /* playAuxSFXAtEntity leaves no state. */

    /* removeBlock's onBlockHarvested. Survival leaves only BlockDoublePlant's;
     * the bed and the door handle their other half in creative mode alone. */
    if (block == ids.double_plant && (meta & 8) != 0
        && world_get_block(w, x, y - 1, z) == ids.double_plant)
    {
        int var7 = world_get_meta(w, x, y - 1, z);
        int var8 = var7 & 7;

        if (var8 != 3 && var8 != 2)
        {
            /* World.func_147480_a(x, y - 1, z, true): the drop, then air. */
            env_aux_sfx(w, 2001, x, y - 1, z, ids.double_plant + (var7 << 12));
            drop_case_for(d, ids.double_plant, var7, 0, x, y - 1, z);
            world_set_block(w, x, y - 1, z, 0, 0, 3);
        }
        else
        {
            if (d->p.held_item == ids.shears) dp_shear(d, x, y, z, var7);
            world_set_block(w, x, y - 1, z, 0, 0, 3);
        }
    }

    /* BlockTripWire.onBlockHarvested: shears disarm the wire before it
     * goes (setBlockMetadataWithNotify(meta | 8, 4)), so breakBlock's hook
     * update reads it disarmed */
    if (block == ids.tripwire && !predicted && d->p.held_item == ids.shears && d->p.held_count > 0)
    {
        meta |= 8;
        world_set_meta(w, x, y, z, meta, 4);
    }

    /* BlockChest.breakBlock (and the other containers') runs inside the
     * write in vanilla, between the store and the metadata; nothing else on
     * this path draws from Math.random or from the world Random, so the
     * spill goes first. It empties the slots, so the write's own breakBlock
     * (blockcb.c) finds nothing left to spill. */
    if (block_rand_index(block) >= 0) container_drops(d, block, x, y, z);

    if (block == 144)
    {
        skull_drops(d, x, y, z, meta);
    }

    int changed = predicted ? 1 : world_set_block(w, x, y, z, 0, 0, 3);

    /* BlockSilverfish.onBlockDestroyedByPlayer (removeBlock's post-air call,
     * the one override on this path): the fish spawns before the harvest's
     * drops, so its identity draws come first. The living itself is
     * constructed by the caller's adopt (survival.c) from the record. */
    if (changed && block == 97) dig_fish(d, (double)x + 0.5, (double)y, (double)z + 0.5);

    /* onBlockDestroyedByPlayer: empty for every block the dig region holds
     * (BlockSilverfish, the one override, is not there). */

    /* canHarvestBlock reads the held stack before the break's damage runs:
     * a stack that breaks here still harvests, but its enchantments are gone
     * by the time harvestBlock reads them through the emptied hand. */
    struct harvest_player hp = hp_of(d);
    int harvestable = can_harvest_block(&hp, block);

    if (d->p.held_item != 0) item_on_block_destroyed(d, block);

    /* EntityPlayer.destroyCurrentEquippedItem when the stack broke. */
    if (d->p.held_item != 0 && d->p.held_count == 0)
    {
        d->p.held_was = d->p.held_item;
        d->p.held_item = 0;
        d->p.held_damage = 0;
        d->p.silk = 0;
        d->p.fortune = 0;
    }

    /* Block.harvestBlock(world, player, x, y, z, meta): the drops fall in
     * the broken cell (a delayed break's is the machine's old target, not
     * the one it now aims at) */
    if (changed && harvestable)
    {
        int ax = d->x, ay = d->y, az = d->z;
        d->x = x;
        d->y = y;
        d->z = z;
        harvest_block(d, block, meta);
        d->x = ax;
        d->y = ay;
        d->z = az;
    }
}

/* ItemInWorldManager.updateBlockRemoving, called once per dig tick. */
static void update_block_removing(dig_env *d)
{
    ++d->m.curblock_damage;
    float var3;
    int var4;

    if (d->m.finish)
    {
        int var1 = d->m.curblock_damage - d->m.initial_block;
        int id = world_get_block(d->w, d->m.pos_x, d->m.pos_y, d->m.pos_z) & 4095;

        if (id == ids.air)
        {
            d->m.finish = 0;
        }
        else
        {
            struct harvest_player hp = hp_of(d);
            var3 = player_relative_block_hardness(&hp, id) * (float)(var1 + 1);
            var4 = (int)(var3 * 10.0f);

            if (var4 != d->m.durability)
            {
                destroy_partial(d, d->m.pos_x, d->m.pos_y, d->m.pos_z, var4);
                d->m.durability = var4;
            }

            if (var3 >= 1.0f)
            {
                d->m.finish = 0;
                try_harvest_block(d, d->m.pos_x, d->m.pos_y, d->m.pos_z);
            }
        }
    }
    else if (d->m.is_destroying)
    {
        int id = world_get_block(d->w, d->m.part_x, d->m.part_y, d->m.part_z) & 4095;

        /* the replay's client emptied its own target before the server's
         * break (the oracle's WorldClient prediction): the machine reads
         * the block as it clicked it */
        if (id == ids.air && d->m.initial_id != ids.air)
            id = d->m.initial_id;

        if (id == ids.air)
        {
            destroy_partial(d, d->m.part_x, d->m.part_y, d->m.part_z, -1);
            d->m.durability = -1;
            d->m.is_destroying = 0;
        }
        else
        {
            int var6 = d->m.curblock_damage - d->m.initial_damage;
            struct harvest_player hp = hp_of(d);
            var3 = player_relative_block_hardness(&hp, id) * (float)(var6 + 1);
            var4 = (int)(var3 * 10.0f);

            if (var4 != d->m.durability)
            {
                destroy_partial(d, d->m.part_x, d->m.part_y, d->m.part_z, var4);
                d->m.durability = var4;
            }
        }
    }
}

/* ItemInWorldManager.uncheckedTryHarvestBlock, the finish packet's path. */
static void unchecked_try_harvest_block(dig_env *d, int x, int y, int z)
{
    if (x != d->m.part_x || y != d->m.part_y || z != d->m.part_z) return;

    int var4 = d->m.curblock_damage - d->m.initial_damage;
    int id = world_get_block(d->w, x, y, z) & 4095;

    /* the client's own prediction emptied the target before the server's
     * finish lands (the oracle's WorldClient.setBlockToAir); the harvest
     * reads the block as the machine clicked it */
    if (id == ids.air && d->m.is_destroying && d->m.initial_id != ids.air)
        id = d->m.initial_id;

    if (id == ids.air) return;

    struct harvest_player hp = hp_of(d);
    float var6 = player_relative_block_hardness(&hp, id) * (float)(var4 + 1);

    if (var6 >= 0.7f)
    {
        d->m.is_destroying = 0;
        destroy_partial(d, x, y, z, -1);
        try_harvest_block(d, x, y, z);
    }
    else if (!d->m.finish)
    {
        d->m.is_destroying = 0;
        d->m.finish = 1;
        d->m.pos_x = x;
        d->m.pos_y = y;
        d->m.pos_z = z;
        d->m.initial_block = d->m.initial_damage;
    }
}

/* World.extinguishFire(null, x, y, z, side): a fire in the cell on the
 * clicked side goes out, with its 1004 effect to the players. */
static void extinguish_fire(struct world *w, int x, int y, int z, int side)
{
    if (side == 0) --y;
    if (side == 1) ++y;
    if (side == 2) --z;
    if (side == 3) ++z;
    if (side == 4) --x;
    if (side == 5) ++x;
    if ((world_get_block(w, x, y, z) & 4095) == 51)
    {
        env_aux_sfx(w, 1004, x, y, z, 0);
        world_set_block(w, x, y, z, 0, 0, 3);
    }
}

void dig_op_click(dig_env *d, int x, int y, int z, int side)
{
    extinguish_fire(d->w, x, y, z, side);

    int id = world_get_block(d->w, x, y, z) & 4095;

    d->m.initial_damage = d->m.curblock_damage;
    d->m.initial_id = id;
    d->m.initial_meta = world_get_meta(d->w, x, y, z);

    float var5 = 1.0f;

    if (id != ids.air)
    {
        /* BlockDragonEgg.onBlockClicked: func_150019_m, the teleport, before
         * the hardness read (the egg's 3.0F never gets there when it moves).
         * The world Random supplies the draws. */
        if (id == 122) act_live_dragon_egg(d->w, &d->wr, x, y, z);

        /* Block.onBlockClicked, the overrides the dig region's blocks carry:
         * the redstone ore draws its six particle rows from the world Random
         * (three nextFloats per row; the particles themselves have no world
         * state) and lights. */
        if (id == ids.redstone_ore || id == ids.lit_redstone_ore)
        {
            for (int i = 0; i < 6; ++i)
            {
                jr_float(&d->wr);
                jr_float(&d->wr);
                jr_float(&d->wr);
            }

            if (id == ids.redstone_ore) world_set_block(d->w, x, y, z, ids.lit_redstone_ore, 0, 3);
        }

        struct harvest_player hp = hp_of(d);
        var5 = player_relative_block_hardness(&hp, id);
    }

    if (id != ids.air && var5 >= 1.0f)
    {
        try_harvest_block(d, x, y, z);
    }
    else
    {
        d->m.is_destroying = 1;
        d->m.part_x = x;
        d->m.part_y = y;
        d->m.part_z = z;
        int var7 = (int)(var5 * 10.0f);
        destroy_partial(d, x, y, z, var7);
        d->m.durability = var7;
    }

    if (!d->net_ops) update_block_removing(d);
    if ((world_get_block(d->w, x, y, z) & 4095) == ids.air) d->broke = 1;
}

void dig_op(dig_env *d, int op, int x, int y, int z)
{
    if (op == DIG_OP_CANCEL)
    {
        /* cancelDestroyingBlock: the packet's coords are ignored and the
         * progress ends; the -1 stage goes out whatever the durability held.
         * The finish packet stays armed and the same tick's update completes
         * the dig (the delayed break), as vanilla does. */
        d->m.is_destroying = 0;
        destroy_partial(d, d->m.part_x, d->m.part_y, d->m.part_z, -1);
        if (!d->net_ops) update_block_removing(d);
        return;
    }

    if (op == DIG_OP_FINISH)
    {
        unchecked_try_harvest_block(d, x, y, z);

        /* the drive checks air after the finish packet, as the oracle does */
        if ((world_get_block(d->w, x, y, z) & 4095) == ids.air)
        {
            d->broke = 1;
            return;
        }

        if (!d->net_ops) update_block_removing(d);
        return;
    }

    update_block_removing(d);
    /* the drive's own air check, not a vanilla read: it must not load the
     * chunk of a long-finished dig target (updateBlockRemoving reads the
     * target only while a dig is armed) */
    if (world_chunk_loaded(d->w, x >> 4, z >> 4) && (world_get_block(d->w, x, y, z) & 4095) == ids.air) d->broke = 1;
}

void dig_case_begin(dig_env *d, int casei, int x, int y, int z, int held_item, int held_damage,
                    int held_count, int eff, int unbr, int silk, int fortune, int on_ground,
                    int in_water, int haste, int fatigue, int64_t opseed, int64_t prand_seed)
{
    d->case_index = casei;
    d->x = x;
    d->y = y;
    d->z = z;
    d->nents = 0;

    d->p.held_item = held_item;
    d->p.held_damage = held_damage;
    d->p.held_count = held_count;
    d->p.eff = eff;
    d->p.unbr = unbr;
    d->p.silk = silk;
    d->p.fortune = fortune;
    d->p.on_ground = on_ground;
    d->p.in_water = in_water;
    d->p.haste = haste;
    d->p.fatigue = fatigue;
    d->p.exhaustion = 0.0f;
    d->p.stat_mine = 0;
    d->p.stat_use = 0;
    d->p.stat_break = 0;
    jr_seed(&d->p.prand, prand_seed);

    jr_seed(&d->wr, opseed);

    /* the probe's own math stream, one seed per case; the tape replay's
     * dig runs on the server role, whose stream continues */
    if (d->role == DET_OTHER) det_rng_set_seed(&d->det->math[d->role], DIG_MATH_SEED);

    d->m.is_destroying = 0;
    d->m.initial_damage = 0;
    d->m.part_x = -1;
    d->m.part_y = -1;
    d->m.part_z = -1;
    d->m.curblock_damage = 0;
    d->m.finish = 0;
    d->m.pos_x = -1;
    d->m.pos_y = -1;
    d->m.pos_z = -1;
    d->m.initial_block = 0;
    d->m.durability = -1;
    d->broke = 0;
}

void dig_case_end(dig_env *d, dig_out *out)
{
    out->held_item = d->p.held_item;
    out->held_damage = d->p.held_damage;
    out->held_count = d->p.held_count;
    out->exhaustion = d->p.exhaustion;
    out->stat_mine = d->p.stat_mine;
    out->stat_use = d->p.stat_use;
    out->stat_break = d->p.stat_break;
    out->wr_state = d->wr.seed;
    out->math_state = det_rng_state(&d->det->math[d->role]);
    out->seeder_state = det_rng_state(&d->det->seeder[d->role]);
    out->next_id = d->det->next_id[d->role];
    out->chest_state = det_rng_state(&d->chest);
    out->broke = d->broke;

    d->p.stat_mine = 0;
    d->p.stat_use = 0;
    d->p.stat_break = 0;
}

void dig_init(dig_env *d, struct world *w, det_state *det, uint64_t chest_seed)
{
    memset(d, 0, sizeof *d);
    d->w = w;
    d->det = det;
    d->role = DET_OTHER;
    /* the chest's Random comes over as a state, not a seed */
    d->chest.r.seed = (uint64_t)chest_seed;
    d->chest.have_next_next_gaussian = 0;

    /* the pop drops (the door) draw through blockcb's environment */
    nw_env->blockcb.env.world_rand = &d->wr;
    nw_env->blockcb.env.det = det;
    nw_env->blockcb.env.item_drop = pop_item_drop;
    nw_env->blockcb.env.ctx = d;
    nw_env->blockcb.env.item_spill = NULL;
}

void dig_bind_blockcb(dig_env *d)
{
    nw_env->blockcb.env.world_rand = &d->wr;
    nw_env->blockcb.env.det = d->det;
    nw_env->blockcb.env.role = d->role;
    nw_env->blockcb.env.item_drop = pop_item_drop;
    nw_env->blockcb.env.ctx = d;
    nw_env->blockcb.env.item_spill = NULL;
}
