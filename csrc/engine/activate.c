#include "comparator.h"
#include "activate.h"
#include "env.h"

#include "blockcb.h"
#include "blocks.h"
#include "explosion.h"
#include "items.h"
#include "jmath.h"
#include "ticks.h"

#include <stdio.h>

#include <stdio.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The block ids the activated cells hold, resolved from the registries once. */
enum {
    ID_AIR = 0, ID_STONE = 1, ID_BED = 26, ID_CHEST = 54, ID_FURNACE = 61,
    ID_LIT_FURNACE = 62, ID_LEVER = 69, ID_CAKE = 92, ID_NOTEBLOCK = 25,
    ID_JUKEBOX = 84, ID_REP_U = 93, ID_REP_P = 94, ID_TRAPDOOR = 96,
    ID_GATE = 107, ID_CAULDRON = 118, ID_POT = 140, ID_BTN_STONE = 77,
    ID_BTN_WOOD = 143, ID_CMP_U = 149, ID_CMP_P = 150, ID_DOOR_W = 64,
    ID_DOOR_I = 71, ID_DISPENSER = 23, ID_WORKBENCH = 58, ID_ENDER_CHEST = 130,
    ID_TRAPPED_CHEST = 146, ID_DRAGON_EGG = 122, ID_REDSTONE_ORE = 73,
    ID_LIT_REDSTONE_ORE = 74, ID_HOPPER = 154, ID_DROPPER = 158
};

/* (double)0.2f, the constant the EntityItem constructor spreads its motion
 * with, the same spelling drops.c and blockcb.c use. */
#define D02 0.20000000298023224
#define ACT_MATH_SEED 0x6d6174684f7468LL

/* The ids and classes the machine names, resolved from the registries once. */
static struct
{
    int air, bed, door_w, door_i, trapdoor, gate, btn_stone, btn_wood, lever,
        cake, note, jukebox, rep_u, rep_p, cmp_u, cmp_p, pot, cauldron, chest,
        trapped_chest, ender_chest, dragon_egg,
        furnace, furnace_lit, dispenser, workbench;
    int redstone_ore, lit_redstone_ore;
    int water_bucket, glass_bottle, potion, bucket;
    int sapling, yellow_flower, red_flower, brown_mushroom, red_mushroom,
        deadbush, cactus, tallgrass;
    int mat_air, mat_rock, mat_sand, mat_glass, mat_wood;
} ids;

static int block_named(const char *name)
{
    for (int i = 0; i < 4096; ++i)
        if (BLOCKS[i].exists && BLOCKS[i].name != NULL && !strcmp(BLOCKS[i].name, name)) return i;

    fprintf(stderr, "activate: no block %s\n", name);
    exit(2);
}

static int item_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof ITEMS / sizeof ITEMS[0]); ++i)
        if (ITEMS[i].exists && ITEMS[i].name != NULL && !strcmp(ITEMS[i].name, name)) return i;

    fprintf(stderr, "activate: no item %s\n", name);
    exit(2);
}

static int material_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
        if (MATERIALS[i].name != NULL && !strcmp(MATERIALS[i].name, name)) return i;

    fprintf(stderr, "activate: no material %s\n", name);
    exit(2);
}

/* before main: the same for every environment */
__attribute__((constructor)) static void init(void)
{
    ids.air = block_named("minecraft:air");
    ids.bed = block_named("minecraft:bed");
    ids.door_w = block_named("minecraft:wooden_door");
    ids.door_i = block_named("minecraft:iron_door");
    ids.trapdoor = block_named("minecraft:trapdoor");
    ids.gate = block_named("minecraft:fence_gate");
    ids.btn_stone = block_named("minecraft:stone_button");
    ids.btn_wood = block_named("minecraft:wooden_button");
    ids.lever = block_named("minecraft:lever");
    ids.cake = block_named("minecraft:cake");
    ids.note = block_named("minecraft:noteblock");
    ids.jukebox = block_named("minecraft:jukebox");
    ids.rep_u = block_named("minecraft:unpowered_repeater");
    ids.rep_p = block_named("minecraft:powered_repeater");
    ids.cmp_u = block_named("minecraft:unpowered_comparator");
    ids.cmp_p = block_named("minecraft:powered_comparator");
    ids.pot = block_named("minecraft:flower_pot");
    ids.cauldron = block_named("minecraft:cauldron");
    ids.chest = block_named("minecraft:chest");
    ids.trapped_chest = block_named("minecraft:trapped_chest");
    ids.ender_chest = block_named("minecraft:ender_chest");
    ids.dragon_egg = block_named("minecraft:dragon_egg");
    ids.redstone_ore = block_named("minecraft:redstone_ore");
    ids.lit_redstone_ore = block_named("minecraft:lit_redstone_ore");
    ids.furnace = block_named("minecraft:furnace");
    ids.furnace_lit = block_named("minecraft:lit_furnace");
    ids.dispenser = block_named("minecraft:dispenser");
    ids.workbench = block_named("minecraft:crafting_table");
    ids.water_bucket = item_named("minecraft:water_bucket");
    ids.glass_bottle = item_named("minecraft:glass_bottle");
    ids.potion = item_named("minecraft:potion");
    ids.bucket = item_named("minecraft:bucket");
    ids.sapling = block_named("minecraft:sapling");
    ids.yellow_flower = block_named("minecraft:yellow_flower");
    ids.red_flower = block_named("minecraft:red_flower");
    ids.brown_mushroom = block_named("minecraft:brown_mushroom");
    ids.red_mushroom = block_named("minecraft:red_mushroom");
    ids.deadbush = block_named("minecraft:deadbush");
    ids.cactus = block_named("minecraft:cactus");
    ids.tallgrass = block_named("minecraft:tallgrass");
    ids.mat_air = material_named("air");
    ids.mat_rock = material_named("rock");
    ids.mat_sand = material_named("sand");
    ids.mat_glass = material_named("glass");
    ids.mat_wood = material_named("wood");
}

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

/* The metadata-only write; the listener row carries id -1, the way
 * Rows.onBlock sees the patched setBlockMetadataWithNotify. */
static void set_meta(act_env *d, int x, int y, int z, int meta, int flags)
{
    world_set_meta(d->w, x, y, z, meta, flags);

    if (d->on_write != NULL) d->on_write(d->on_write_ctx, x, y, z, -1, meta);
}

/* WorldServer.func_147452_c: addBlockEvent's dedupe and append. */
static void add_event(act_env *d, int x, int y, int z, int block, int event, int param)
{
    if (d->live)
    {
        world_block_event(d->w, x, y, z, block, event, param);
        env_block_event(d->w, x, y, z, block, event, param);
        return;
    }

    for (int i = 0; i < d->events.n; ++i)
        if (d->events.v[i][0] == x && d->events.v[i][1] == y && d->events.v[i][2] == z
            && d->events.v[i][3] == block && d->events.v[i][4] == event
            && d->events.v[i][5] == param) return;

    env_block_event(d->w, x, y, z, block, event, param);
    if (d->events.n >= ACT_MAX_EVENTS) return;

    int32_t *v = d->events.v[d->events.n++];
    v[0] = x; v[1] = y; v[2] = z; v[3] = block; v[4] = event; v[5] = param;
}

/* The bed's foot-to-head offset, BlockBed.field_149981_a. */
static void bed_head_offset(int dir, int *dx, int *dz)
{
    static const int off[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};

    *dx = off[dir & 3][0];
    *dz = off[dir & 3][1];
}

/* InventoryPlayer.addItemStackToInventory over the machine's 36 main slots,
 * the not-damaged, not-creative path, 1 when the whole stack fit (0 when the
 * spill spawns). The potion the bottle fills has no tag compound. */
static int inv_player_add(act_env *d, const struct craft_stack *in)
{
    struct inv *p = &d->player;
    int item = in->item, damage = in->damage;

    if (ITEMS[item].max_stack_size == 1)
    {
        int slot = -1;

        for (int i = 0; i < 36 && slot < 0; ++i)
            if (craft_slot_empty(&p->slot[i])) slot = i;

        if (slot < 0) return 0;

        p->slot[slot].item = item;
        p->slot[slot].damage = damage;
        p->slot[slot].count = in->count;
        return 1;
    }

    int size = in->count;
    int before;

    do
    {
        before = size;
        int slot = -1;

        /* storeItemStack: a matching, stackable, unfull slot */
        for (int i = 0; i < 36 && slot < 0; ++i)
        {
            struct craft_stack *s = &p->slot[i];

            if (craft_slot_empty(s) || s->item != item || s->count >= ITEMS[item].max_stack_size
                || s->count >= p->limit) continue;
            if (ITEMS[item].has_subtypes && s->damage != damage) continue;
            slot = i;
        }

        /* getFirstEmptyStack */
        if (slot < 0)
            for (int i = 0; i < 36 && slot < 0; ++i)
                if (craft_slot_empty(&p->slot[i])) slot = i;

        if (slot < 0) break;

        struct craft_stack *s = &p->slot[slot];

        if (craft_slot_empty(s))
        {
            s->item = item;
            s->damage = damage;
            s->count = 0;
        }

        int var5 = size;

        if (var5 > ITEMS[item].max_stack_size - s->count) var5 = ITEMS[item].max_stack_size - s->count;
        if (var5 > p->limit - s->count) var5 = p->limit - s->count;

        if (var5 == 0) continue;

        size -= var5;
        s->count += var5;
    }
    while (size > 0 && size < before);

    return size < in->count;
}

/* FoodStats.addStats: the min caps, the add as Java spells it. */
static void food_add(act_env *d, int level, float sat)
{
    d->food_level = level + d->food_level < 20 ? level + d->food_level : 20;
    float add = (float)level * sat * 2.0F;
    d->food_sat = d->food_sat + add < (float)d->food_level ? d->food_sat + add : (float)d->food_level;
}

/* The live path's dropBlockAsItem_do sink, defined at the tail. */
static void act_live_env_drop(void *ctx, int entity_id, uint64_t rand_state,
                              int64_t uuid_msb, int64_t uuid_lsb,
                              double x, double y, double z, float yaw, float hover,
                              double motion_x, double motion_z, int item, int damage, int count);

/* The EntityItem(World, x, y, z, stack) the block code builds directly, into
 * the env's entity list. The live path's env carries no iew of its own: the
 * spawn goes through live_iew, the caller's entity list. */
#define live_iew (nw_env->activate.live_iew)

static ie_ent *act_spawn_item(act_env *d, double x, double y, double z, int item, int damage, int count)
{
    ie_ent *en = live_iew ? ie_spawn_item(live_iew, x, y, z, item, damage, count)
                          : ie_spawn_item(&d->iew, x, y, z, item, damage, count);

    if (en == NULL) return NULL;

    /* World.spawnEntityInWorld: the chunk's list too, so the entity walk
     * ticks it from the next update on; the caller's list owns the record */
    if (live_iew)
    {
        ie_added_to_world(live_iew, en);
        return en;
    }

    if (d->nents >= ACT_MAX_ENTS) return en;

    act_ent *e = &d->ents[d->nents++];
    e->entity_id = en->entity_id;
    e->item = item;
    e->damage = damage;
    e->count = count;
    e->x = en->e.pos_x;
    e->y = en->e.pos_y;
    e->z = en->e.pos_z;
    e->mx = en->e.motion_x;
    e->my = en->e.motion_y;
    e->mz = en->e.motion_z;
    e->yaw = en->rotation_yaw;
    return en;
}

/* TileEntityDispenser's constructor Random: one seeder draw when the block
 * callback environment is live (a demand creation outside one answers no
 * draw, as the Java construction outside a det role's machine never runs). */
static void act_dispenser_random(void *ctx)
{
    (void)ctx;

    if (nw_env->blockcb.env.det != NULL) det_new_random_role(nw_env->blockcb.env.det, DET_OTHER);
}

/* blockcb_env's item_drop sink: dropBlockAsItem_do's entity, built with the
 * det draws already spent (blockcb.c's drop_item_stack drew them). */
static void act_item_drop(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                          double x, double y, double z, float yaw, float hover,
                          double motion_x, double motion_z, int item, int damage, int count)
{
    act_env *d = (act_env *)ctx;
    (void)rand_state;
    (void)uuid_msb;
    (void)uuid_lsb;

    if (d->nents >= ACT_MAX_ENTS) return;

    /* the setup's pops spawn without a record (the probe's capture is not
     * attached during the setup); they stay in the entity world like the
     * oracle's, which never removes them */
    if (!d->setup)
    {
        act_ent *e = &d->ents[d->nents++];
        e->entity_id = entity_id;
        e->item = item;
        e->damage = damage;
        e->count = count;
        e->x = x; e->y = y; e->z = z;
        e->mx = motion_x;
        e->my = D02;
        e->mz = motion_z;
        e->yaw = yaw;
    }

    /* the same entity into the env's world, for the explosion's entity sweep;
     * no det draw here, the boilerplate already ran in drop_item_stack */
    ie_ent *en = ie_ent_alloc();

    if (en == NULL) return;

    entity_init(&en->e, d->iew.w);
    en->e.can_trigger_walking = 0;
    en->e.self = en;
    en->e.first_update = 1;
    en->entity_id = entity_id;
    en->health = 5;
    en->kind = IE_ITEM;

    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->rotation_yaw = yaw;
    en->hover_start = 0.0F;   /* carried on the record only */
    en->e.motion_x = motion_x;
    en->e.motion_y = D02;
    en->e.motion_z = motion_z;
    en->stack_item = item;
    en->stack_damage = damage;
    en->stack_count = count;

    /* spawnEntityInWorld: the chunk-map entry the explosion's entity query
     * walks (the knockback of a dropped bed item lands in the record) */
    ie_added_to_world(&d->iew, en);

    ie_list_push(&d->iew, en);
}

/* TileEntityChest.openInventory. The event's block id is getBlockType(),
 * the same chest or trapped chest the tile stands under. */
static void chest_open(act_env *d, struct tile_entity *te)
{
    int id = world_get_block(d->w, te->x, te->y, te->z) & 4095;

    if (te->u.chest.players_using < 0) te->u.chest.players_using = 0;
    ++te->u.chest.players_using;
    add_event(d, te->x, te->y, te->z, id, 1, te->u.chest.players_using);
    world_notify_neighbors(d->w, te->x, te->y, te->z, id);
    world_notify_neighbors(d->w, te->x, te->y - 1, te->z, id);
}

/* TileEntityChest.closeInventory: only while the block is still a chest. */
static void chest_close(act_env *d, struct tile_entity *te)
{
    int id = world_get_block(d->w, te->x, te->y, te->z) & 4095;
    if (id != ids.chest && id != ids.trapped_chest) return;   /* BlockChest */

    --te->u.chest.players_using;
    add_event(d, te->x, te->y, te->z, id, 1, te->u.chest.players_using);
    world_notify_neighbors(d->w, te->x, te->y, te->z, id);
    world_notify_neighbors(d->w, te->x, te->y - 1, te->z, id);
}

/* BlockChest.func_149953_o: the ocelot check. The region holds no ocelots. */
static int ocelot_on_chest(struct world *w, int x, int y, int z)
{
    (void)w; (void)x; (void)y; (void)z;
    return 0;
}

/* BlockChest.func_149951_m: 0 none, 1 the single chest over (x,y,z), 2 the
 * large chest over upper and partner. The pair checks compare against the
 * SAME block id (Java's == this): a trapped chest pairs only with trapped
 * chests. */
static int chest_inventory(act_env *d, int x, int y, int z, int self,
                           int *ux, int *uz, int *px, int *pz)
{
    struct world *w = d->w;

    if (world_tile_entity(w, x, y, z) == NULL) return 0;
    if (BLOCKS[block_at(w, x, y + 1, z)].normal_cube) return 0;
    if (ocelot_on_chest(w, x, y, z)) return 0;

    if (block_at(w, x - 1, y, z) == self
        && (BLOCKS[block_at(w, x - 1, y + 1, z)].normal_cube || ocelot_on_chest(w, x - 1, y, z))) return 0;

    if (block_at(w, x + 1, y, z) == self
        && (BLOCKS[block_at(w, x + 1, y + 1, z)].normal_cube || ocelot_on_chest(w, x + 1, y, z))) return 0;

    if (block_at(w, x, y, z - 1) == self
        && (BLOCKS[block_at(w, x, y + 1, z - 1)].normal_cube || ocelot_on_chest(w, x, y, z - 1))) return 0;

    if (block_at(w, x, y, z + 1) == self
        && (BLOCKS[block_at(w, x, y + 1, z + 1)].normal_cube || ocelot_on_chest(w, x, y, z + 1))) return 0;

    if (block_at(w, x - 1, y, z) == self)
    {
        *ux = x - 1; *uz = z; *px = x; *pz = z;
        return 2;   /* InventoryLargeChest(te(x-1), te(x)): the left half opens first */
    }

    if (block_at(w, x + 1, y, z) == self)
    {
        *ux = x; *uz = z; *px = x + 1; *pz = z;
        return 2;
    }

    if (block_at(w, x, y, z - 1) == self)
    {
        *ux = x; *uz = z - 1; *px = x; *pz = z;
        return 2;
    }

    if (block_at(w, x, y, z + 1) == self)
    {
        *ux = x; *uz = z; *px = x; *pz = z + 1;
        return 2;
    }

    return 1;
}

/* BlockCauldron.func_150024_a: the level write, then World.func_147453_f
 * (comparator.c). */
static void cauldron_level(act_env *d, int x, int y, int z, int level)
{
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    set_meta(d, x, y, z, level, 2);
    comparator_notify(d->w, x, y, z, ids.cauldron);
}

/* BlockFlowerPot.func_149928_a: what a pot accepts. */
static int pot_allows(int block, int damage)
{
    if (block == ids.yellow_flower || block == ids.red_flower || block == ids.cactus
        || block == ids.brown_mushroom || block == ids.red_mushroom || block == ids.sapling
        || block == ids.deadbush) return 1;
    return block == ids.tallgrass && damage == 2;
}

/* The bed's branch: the occupied search, the refusals, sleepInBedAt, and the
 * wrong-dimension explosion. */
static int bed_activate(act_env *d, const act_case *c)
{
    struct world *w = d->w;
    int x = c->x, y = c->y, z = c->z;
    int var10 = world_get_meta(w, x, y, z);

    if ((var10 & 8) == 0)
    {
        int dx, dz;

        bed_head_offset(var10 & 3, &dx, &dz);
        x += dx;
        z += dz;

        if (block_at(w, x, y, z) != ids.bed) return 1;

        var10 = world_get_meta(w, x, y, z);
    }

    /* the provider's canRespawnHere gates the sleep; the overworld answers
     * true, the wrong-dimension run's false */
    if (d->can_respawn)
    {
        if ((var10 & 4) != 0)
        {
            if (c->dummy_sleeping)
            {
                d->chat = 1;   /* tile.bed.occupied */
                return 1;
            }

            int raw = world_get_meta(w, x, y, z);

            set_meta(d, x, y, z, raw & -5, 4);   /* func_149979_a(false), flag 4 */
        }

        /* EntityPlayer.sleepInBedAt, in order: the probe's server is parked, so
         * skylightSubtracted stays at its world-start value and isDaytime is
         * true in every case, whatever the drawn day flag; the sleep itself is
         * the sleeping lane's machine */
        d->chat = 2;   /* tile.bed.noSleep */
        return 1;
    }

    /* the wrong-dimension branch */
    double var18 = (double)x + 0.5;   /* the decompile's midpoint shape, unused */
    (void)var18;

    world_set_block(w, x, y, z, ID_AIR, 0, 3);   /* setBlockToAir: the foot pops */

    int dx, dz;

    bed_head_offset(var10 & 3, &dx, &dz);
    x += dx;
    z += dz;

    if (block_at(w, x, y, z) == ids.bed) world_set_block(w, x, y, z, ID_AIR, 0, 3);


    expl_run(w, d->det, DET_OTHER, &d->wr, &d->iew, NULL,
             (double)((float)x + 0.5F), (double)((float)y + 0.5F), (double)((float)z + 0.5F),
             5.0F, 1, 1, NULL);
    return 1;
}

/* BlockDoor.onBlockActivated through its func_150012_g lookup. */
static int door_activate(act_env *d, const act_case *c)
{
    struct world *w = d->w;
    int x = c->x, y = c->y, z = c->z;
    int id = block_at(w, x, y, z);

    if (id == ids.door_i) return 1;   /* the iron door refuses, true */

    int m = world_get_meta(w, x, y, z);
    int upper = (m & 8) != 0;
    int lower = upper ? world_get_meta(w, x, y - 1, z) : m;
    int var11 = (lower & 7) ^ 4;

    if (!upper)
    {
        set_meta(d, x, y, z, var11, 2);
    }
    else
    {
        set_meta(d, x, y - 1, z, var11, 2);
    }

    /* markBlockRangeForRenderUpdate, playAuxSFXAtEntity(1003): no state */
    return 1;
}

/* BlockDragonEgg.func_150019_m: up to 1000 tries for an air-material cell
 * six draws apart, the teleport writes with flag 2 (no notify) and the
 * origin's setBlockToAir with flag 3. The world Random supplies the draws. */
static void dragon_egg_teleport(struct world *w, jrand *wr, int x, int y, int z)
{
    int egg = world_get_block(w, x, y, z) & 4095;

    if (egg != ID_DRAGON_EGG) return;   /* == this */

    int meta = world_get_meta(w, x, y, z);

    for (int i = 0; i < 1000; ++i)
    {
        int var6 = x + jr_int_n(wr, 16) - jr_int_n(wr, 16);
        int var7 = y + jr_int_n(wr, 8) - jr_int_n(wr, 8);
        int var8 = z + jr_int_n(wr, 16) - jr_int_n(wr, 16);

        if (BLOCKS[block_at(w, var6, var7, var8)].material == 0)   /* Material.air */
        {
            world_set_block(w, var6, var7, var8, egg, meta, 2);
            world_set_block(w, x, y, z, 0, 0, 3);
            return;
        }
    }
}

/* The tile-entity and container blocks' bodies, one per Java method. */
static int on_block_activated(act_env *d, const act_case *c)
{
    struct world *w = d->w;
    int x = c->x, y = c->y, z = c->z;
    int id = block_at(w, x, y, z);

    switch (id)
    {
        case ID_DOOR_W:
        case ID_DOOR_I:
            return door_activate(d, c);

        case ID_TRAPDOOR:
        {
            int var10 = world_get_meta(w, x, y, z);
            set_meta(d, x, y, z, var10 ^ 4, 2);
            /* playAuxSFXAtEntity(1003): no state */
            return 1;
        }

        case ID_GATE:
        {
            int var10 = world_get_meta(w, x, y, z);

            if ((var10 & 4) != 0)
            {
                set_meta(d, x, y, z, var10 & -5, 2);
            }
            else
            {
                int var11 = mh_floor((double)(c->yaw * 4.0F / 360.0F) + 0.5) & 3;
                int var12 = var10 & 3;

                if (var12 == (var11 + 2) % 4) var10 = var11;

                set_meta(d, x, y, z, var10 | 4, 2);
            }

            /* playAuxSFXAtEntity(1003): no state */
            return 1;
        }

        case ID_BTN_STONE:
        case ID_BTN_WOOD:
        {
            int var10 = world_get_meta(w, x, y, z);
            int var11 = var10 & 7;
            int var12 = 8 - (var10 & 8);

            if (var12 == 0) return 1;

            set_meta(d, x, y, z, var11 + var12, 3);
            /* playSoundEffect, markBlockRangeForRenderUpdate: no state */
            blockcb_button_notify(d->w, x, y, z, var11, id);
            ticks_schedule_block_update(w, x, y, z, id, id == ID_BTN_STONE ? 20 : 30);
            return 1;
        }

        case ID_LEVER:
        {
            int var10 = world_get_meta(w, x, y, z);
            int var11 = var10 & 7;
            int var12 = 8 - (var10 & 8);

            set_meta(d, x, y, z, var11 + var12, 3);
            /* playSoundEffect: no state */
            world_notify_neighbors(w, x, y, z, ids.lever);

            if (var11 == 1) world_notify_neighbors(w, x - 1, y, z, ids.lever);
            else if (var11 == 2) world_notify_neighbors(w, x + 1, y, z, ids.lever);
            else if (var11 == 3) world_notify_neighbors(w, x, y, z - 1, ids.lever);
            else if (var11 == 4) world_notify_neighbors(w, x, y, z + 1, ids.lever);
            else if (var11 == 5 || var11 == 6) world_notify_neighbors(w, x, y - 1, z, ids.lever);
            else world_notify_neighbors(w, x, y + 1, z, ids.lever);

            return 1;
        }

        case ID_NOTEBLOCK:
        {
            struct tile_entity *te = world_tile_entity(w, x, y, z);

            if (te == NULL) return 1;

            te->u.note.note = (te->u.note.note + 1) % 25;   /* func_145877_a */
            /* onInventoryChanged: no state */

            /* func_145878_a: the note plays only into air above */
            if (BLOCKS[block_at(w, x, y + 1, z)].material != ids.mat_air) return 1;

            int below = BLOCKS[block_at(w, x, y - 1, z)].material;
            int var6 = 0;

            if (below == ids.mat_rock) var6 = 1;
            if (below == ids.mat_sand) var6 = 2;
            if (below == ids.mat_glass) var6 = 3;
            if (below == ids.mat_wood) var6 = 4;

            add_event(d, x, y, z, ids.note, var6, te->u.note.note);
            return 1;
        }

        case ID_JUKEBOX:
        {
            if (world_get_meta(w, x, y, z) == 0) return 0;

            /* func_149925_e, the eject */
            struct tile_entity *te = world_tile_entity(w, x, y, z);

            if (te == NULL) return 1;

            if (te->u.jukebox.item >= 0)
            {
                int item = te->u.jukebox.item, damage = te->u.jukebox.damage, count = te->u.jukebox.count;
                te->u.jukebox.item = -1;
                te->u.jukebox.damage = 0;
                te->u.jukebox.count = 0;
                /* TileEntityJukebox.func_145857_a(null)'s markDirty */
                comparator_notify(w, x, y, z, ids.jukebox);
                set_meta(d, x, y, z, 0, 2);
                /* playAuxSFX(1005), playRecord: no state */

                float var7 = 0.7F;
                double var8 = (double)((float)jr_float(&d->wr) * var7) + (double)(1.0F - var7) * 0.5;
                double var10b = (double)((float)jr_float(&d->wr) * var7) + (double)(1.0F - var7) * 0.2 + 0.6;
                double var12 = (double)((float)jr_float(&d->wr) * var7) + (double)(1.0F - var7) * 0.5;

                ie_ent *rec = act_spawn_item(d, (double)x + var8, (double)y + var10b, (double)z + var12,
                                             item, damage, count);

                if (rec != NULL) rec->delay = 10;   /* delayBeforeCanPickup */
            }

            return 1;
        }

        case ID_REP_U:
        case ID_REP_P:
        {
            int var10 = world_get_meta(w, x, y, z);
            int var11 = (((var10 & 12) >> 2) + 1) << 2 & 12;

            set_meta(d, x, y, z, (var11 | (var10 & 3)), 3);
            return 1;
        }

        case ID_CMP_U:
        case ID_CMP_P:
        {
            int var10 = world_get_meta(w, x, y, z);
            int var11 = id == ID_CMP_P || (var10 & 8) != 0;   /* field_149914_a: the
                                                               * powered comparator's
                                                               * constructor flag */
            int var12 = !((var10 & 4) == 4);
            int var13 = (var12 ? 4 : 0) | (var11 ? 8 : 0);

            /* playSoundEffect: no state */
            set_meta(d, x, y, z, (var13 | (var10 & 3)), 2);

            /* func_149972_c: the input strength (comparator.c: the container
             * behind, or 0), stored as the output */
            struct tile_entity *te = world_tile_entity(w, x, y, z);

            if (te != NULL)
            {
                int var6 = world_get_meta(w, x, y, z);   /* after the mode write */
                int var7 = comparator_strength(w, x, y, z, var6);   /* func_149970_j */
                int var8 = te->u.comparator.out_signal;

                te->u.comparator.out_signal = var7;

                if (var8 != var7 || !((var6 & 4) == 4))
                {
                    int var9 = comparator_should_power(w, x, y, z, var6);   /* func_149900_a */
                    int var10 = id == ID_CMP_P || (var6 & 8) != 0;

                    if (var10 && !var9) set_meta(d, x, y, z, var6 & -9, 2);
                    else if (!var10 && var9) set_meta(d, x, y, z, var6 | 8, 2);

                    /* func_149911_e: the faced neighbour and its own six */
                    blockcb_diode_notify(w, x, y, z, id);
                }
            }

            return 1;
        }

        case ID_POT:
        {
            struct craft_stack *held = &d->player.slot[0];

            if (craft_slot_empty(held) || !ITEMS[held->item].exists
                || ITEMS[held->item].kind != ITEM_BLOCK) return 0;

            struct tile_entity *te = world_tile_entity(w, x, y, z);

            if (te == NULL) return 0;
            if (te->u.pot.item >= 0) return 0;

            if (!pot_allows(ITEMS[held->item].block_id, held->damage)) return 0;

            te->u.pot.item = held->item;
            te->u.pot.data = held->damage;
            /* onInventoryChanged: no state */

            if (!world_set_meta(w, x, y, z, held->damage, 2)) { /* func_147471_g: no state */ }
            if (d->on_write != NULL) d->on_write(d->on_write_ctx, x, y, z, -1, held->damage);

            if (--held->count <= 0) craft_stack_free(held);

            return 1;
        }

        case ID_CAULDRON:
        {
            struct craft_stack *held = &d->player.slot[0];

            if (craft_slot_empty(held)) return 1;

            int var12 = world_get_meta(w, x, y, z);

            if (held->item == ids.water_bucket)
            {
                if (var12 < 3)
                {
                    held->item = ids.bucket;
                    held->damage = 0;
                    held->count = 1;

                    cauldron_level(d, x, y, z, 3);
                }

                return 1;
            }

            if (held->item == ids.glass_bottle)
            {
                if (var12 > 0)
                {
                    struct craft_stack potion;

                    potion.item = ids.potion;
                    potion.count = 1;
                    potion.damage = 0;
                    potion.tag = 0;

                    if (!inv_player_add(d, &potion))
                    {
                        act_spawn_item(d, (double)x + 0.5, (double)y + 1.5, (double)z + 0.5,
                                       potion.item, potion.damage, potion.count);
                    }
                    /* sendContainerToPlayer: packets only */

                    if (--held->count <= 0) craft_stack_free(held);

                    cauldron_level(d, x, y, z, var12 - 1);
                }
            }

            /* the armor wash is out of scope, and this branch answers false */
            return 0;
        }

        case ID_CHEST:
        case ID_TRAPPED_CHEST:
        case ID_ENDER_CHEST:
        {
            /* BlockEnderChest.onBlockActivated: the tile and the above-block
             * check first; opening ends in a per-player inventory window */
            if (id == ID_ENDER_CHEST)
            {
                struct tile_entity *te = world_tile_entity(w, x, y, z);

                if (te == NULL) return 1;   /* var10/var11 null guard */

                if (BLOCKS[block_at(w, x, y + 1, z)].normal_cube) return 1;

                /* func_146031_a + displayGUIChest: the tile's players_using
                 * count and its block event (no neighbour notify), the
                 * window is per-player; live, survival.c gui_open_ender
                 * runs them after displayGUIChest's closeScreen */
                if (!d->live)
                {
                    if (te->u.chest.players_using < 0) te->u.chest.players_using = 0;
                    ++te->u.chest.players_using;
                    add_event(d, x, y, z, id, 1, te->u.chest.players_using);
                }
                d->open_gui = 4;
                return 1;
            }

            int ux, uz, px, pz;
            int kind = chest_inventory(d, x, y, z, id, &ux, &uz, &px, &pz);

            if (kind == 0) return 1;   /* the ocelot check and the blocked GUI */

            /* live, survival.c gui_open_chest opens the tiles after
             * displayGUIChest's closeScreen */
            if (kind == 1)
            {
                if (!d->live) chest_open(d, world_tile_entity(w, x, y, z));
                d->open_gui = 1;   /* the single chest */
                d->open_x = x;
                d->open_y = y;
                d->open_z = z;
            }
            else
            {
                if (!d->live)
                {
                    chest_open(d, world_tile_entity(w, ux, y, uz));   /* the upper first */
                    chest_open(d, world_tile_entity(w, px, y, pz));
                }
                d->open_gui = 2;
                d->open_y = y;
                d->open_ux = ux;
                d->open_uz = uz;
                d->open_px = px;
                d->open_pz = pz;
            }

            /* displayGUIChest: the window id and the packets reach no world state */
            return 1;
        }

        case ID_FURNACE:
        case ID_LIT_FURNACE:
        {
            if (world_tile_entity(w, x, y, z) != NULL) d->open_gui = 3;
            return 1;
        }

        case ID_DISPENSER:
        case ID_DROPPER:
        {
            /* BlockDispenser.onBlockActivated (BlockDropper inherits it):
             * func_146102_a over the tile */
            if (world_tile_entity(w, x, y, z) != NULL) d->open_gui = 3;
            return 1;
        }

        case ID_HOPPER:
        {
            /* BlockHopper.onBlockActivated: func_146093_a over the tile */
            if (world_tile_entity(w, x, y, z) != NULL) d->open_gui = 3;
            return 1;
        }

        case ID_DRAGON_EGG:
            dragon_egg_teleport(w, &d->wr, x, y, z);
            return 1;

        case ID_CAKE:
        {
            /* canEat(false): needFood and not creative */
            if (d->food_level < 20)
            {
                food_add(d, 2, 0.1F);

                int var6 = world_get_meta(w, x, y, z) + 1;

                if (var6 >= 6) world_set_block(w, x, y, z, ID_AIR, 0, 3);
                else set_meta(d, x, y, z, var6, 2);
            }

            return 1;
        }

        case ID_BED:
            return bed_activate(d, c);

        case ID_REDSTONE_ORE:
        case ID_LIT_REDSTONE_ORE:
        {
            /* BlockRedstoneOre.onBlockActivated: func_150185_e, whose
             * func_150186_m draws three world-Random floats for each of its
             * six particle rows (the opacity checks only move a particle,
             * which leaves no server state), then the light-up write; the
             * base Block answers false, so a held block still places */
            for (int i = 0; i < 6; ++i)
            {
                jr_float(&d->wr);
                jr_float(&d->wr);
                jr_float(&d->wr);
            }

            if (id == ID_REDSTONE_ORE) world_set_block(w, x, y, z, ID_LIT_REDSTONE_ORE, 0, 3);

            return 0;
        }

        case ID_WORKBENCH:
            d->open_gui = 3;   /* displayGUIWorkbench: no state */
            return 1;

        default:
            return 0;   /* the base Block.onBlockActivated */
    }
}

struct act_live_result act_live_block(struct world *w, const struct act_live_env *in,
                                      struct act_live_env *out,
                                      int x, int y, int z, int side,
                                      float hx, float hy, float hz,
                                      float yaw, int sneaking)
{
    struct act_live_result result = {0};
    if (sneaking && in->held_count > 0)
    {
        *out = *in;
        return result;
    }

    act_env *d ACT_SCRATCH = act_scratch_begin();
    act_case c = {0};
    d->w = w;
    d->live = 1;
    d->det = in->det;
    d->food_level = in->food_level;
    d->food_sat = in->food_sat;
    d->player.kind = INV_PLAYER;
    d->player.size = 40;
    d->player.limit = 64;
    d->player.slot[0] = (struct craft_stack){in->held_count > 0 ? in->held_item : -1,
                                             in->held_count, in->held_damage, 0};

    /* the rest of the machine's inventory mirrors the caller's, so an add
     * lands in the right slot */
    for (int i = 1; i < 40; ++i)
    {
        int count = in->inv_count ? in->inv_count[i] : 0;
        d->player.slot[i] = (struct craft_stack){count > 0 ? in->inv_item[i] : -1,
                                                 count, count > 0 ? in->inv_damage[i] : 0,
                                                 0};
    }
    d->wr.seed = in->wr ? in->wr->seed : 0;
    c.x = x; c.y = y; c.z = z; c.side = side;
    c.vx = hx; c.vy = hy; c.vz = hz; c.yaw = yaw;
    c.held_item = in->held_count > 0 ? in->held_item : 0;

    /* the callbacks the block writes reach: one world Random stream, the
     * local copy seeded from the caller's (both the machine's own draws and
     * a drop's draw through blockcb_env read it), the Det streams drawing
     * with the SERVER role, and a drop spawning into the caller's entity
     * list. A caller already inside serverreplay_action_begin's window
     * keeps its role and item_drop sink. */
    struct blockcb_env prev_env = nw_env->blockcb.env;
    if (in->wr != NULL) nw_env->blockcb.env.world_rand = &d->wr;
    if (in->det != NULL && nw_env->blockcb.env.det == NULL)
    {
        nw_env->blockcb.env.det = in->det;
        nw_env->blockcb.env.role = DET_SERVER;
    }
    if (in->iew != NULL && nw_env->blockcb.env.item_drop == NULL)
        nw_env->blockcb.env.item_drop = act_live_env_drop, nw_env->blockcb.env.ctx = d, nw_env->blockcb.env.item_spill = NULL;

    /* the EntityItem the machine builds itself (the cauldron's overflow
     * bottle) spawns into the caller's list */
    live_iew = in->iew;

    result.activated = on_block_activated(d, &c);

    live_iew = NULL;
    nw_env->blockcb.env = prev_env;

    /* the cake's food and the pot's and cauldron's held stacks come back */
    out->food_level = d->food_level;
    out->food_sat = d->food_sat;
    struct craft_stack *held = &d->player.slot[0];
    out->held_item = craft_slot_empty(held) ? 0 : held->item;
    out->held_damage = craft_slot_empty(held) ? 0 : held->damage;
    out->held_count = craft_slot_empty(held) ? 0 : held->count;

    /* the adds that landed outside the held slot: the cauldron's overflow
     * potion into the first empty inventory slot. The machine's slots started
     * as the caller's, so a diff against the seed finds what moved. */
    out->add_slot = 0;
    out->add_count = 0;

    for (int i = 1; i < 40; ++i)
    {
        int item = d->player.slot[i].item, count = d->player.slot[i].count;
        int seed_count = in->inv_count ? in->inv_count[i] : 0;

        if (count == seed_count && (count == 0 || (item == in->inv_item[i] &&
                                    d->player.slot[i].damage == in->inv_damage[i])))
            continue;

        out->add_slot = i;
        out->add_item = craft_slot_empty(&d->player.slot[i]) ? 0 : item;
        out->add_damage = craft_slot_empty(&d->player.slot[i]) ? 0 : d->player.slot[i].damage;
        out->add_count = craft_slot_empty(&d->player.slot[i]) ? 0 : count;
        break;
    }

    if (in->wr != NULL) in->wr->seed = d->wr.seed;

    result.gui = d->open_gui;
    result.x = d->open_gui == 1 ? d->open_x : x;
    result.y = d->open_gui == 1 || d->open_gui == 2 ? d->open_y : y;
    result.z = d->open_gui == 1 ? d->open_z : z;
    result.ux = d->open_ux; result.uz = d->open_uz;
    result.px = d->open_px; result.pz = d->open_pz;
    return result;
}

void act_live_chest_open(struct world *w, struct tile_entity *te)
{
    act_env *d ACT_SCRATCH = act_scratch_begin();
    d->w = w;
    d->live = 1;
    chest_open(d, te);
}

void act_live_chest_close(struct world *w, struct tile_entity *te)
{
    act_env *d ACT_SCRATCH = act_scratch_begin();
    d->w = w;
    d->live = 1;
    chest_close(d, te);
}

void act_live_dragon_egg(struct world *w, jrand *wr, int x, int y, int z)
{
    dragon_egg_teleport(w, wr, x, y, z);
}

/* The live path's dropBlockAsItem_do sink: the boilerplate's draws already
 * ran in blockcb's drop_item_spawn; the entity joins the caller's list. */
static void act_live_env_drop(void *ctx, int entity_id, uint64_t rand_state,
                              int64_t uuid_msb, int64_t uuid_lsb,
                              double x, double y, double z, float yaw, float hover,
                              double motion_x, double motion_z, int item, int damage, int count)
{
    act_env *d = (act_env *)ctx;
    (void)rand_state; (void)uuid_msb; (void)uuid_lsb; (void)hover;

    if (d->iew.w == NULL) return;

    ie_ent *en = ie_ent_alloc();

    if (en == NULL) return;

    entity_init(&en->e, d->iew.w);
    en->e.can_trigger_walking = 0;
    en->e.self = en;
    en->e.first_update = 1;
    en->entity_id = entity_id;
    en->health = 5;
    en->kind = IE_ITEM;

    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->rotation_yaw = yaw;
    en->hover_start = hover;
    en->e.motion_x = motion_x;
    en->e.motion_y = D02;
    en->e.motion_z = motion_z;
    en->stack_item = item;
    en->stack_damage = damage;
    en->stack_count = count;

    ie_added_to_world(&d->iew, en);
    ie_list_push(&d->iew, en);
}



/* ------------------------------------------------------- the case lifecycle */

void act_init(act_env *d, struct world *w, det_state *det, int64_t total_time,
              int64_t next_tick_entry)
{
    memset(d, 0, sizeof *d);
    d->w = w;
    d->det = det;
    d->can_respawn = 1;   /* the test clears it for the wrong-dimension run */
    ie_init(&d->iew, w, det);
    d->iew.role = DET_OTHER;
    d->player.kind = INV_PLAYER;
    d->player.size = 40;
    d->player.limit = 64;
    for (int s = 0; s < 40; ++s) craft_stack_free(&d->player.slot[s]);

    /* TileEntityDispenser's constructor Random: one seeder draw per fresh
     * construction, spent through this env's det when the block callback
     * environment is live (the setup's writes and the case's calls) */
    te_dispenser_random = act_dispenser_random;
    te_dispenser_random_ctx = d;

    ticks_reset(next_tick_entry);
    ticks_set_total_time(total_time);
    ticks_set_immediate(0);
    ticks_set_fall_instantly(0);
}

/* The single-cell kinds' block id, the oracle's KINDS rows. */
static int kind_block_id(int kind)
{
    switch (kind)
    {
        case ACT_DOOR_W: return ids.door_w;
        case ACT_DOOR_I: return ids.door_i;
        case ACT_TRAPDOOR: return ids.trapdoor;
        case ACT_GATE: return ids.gate;
        case ACT_BTN_STONE: return ids.btn_stone;
        case ACT_BTN_WOOD: return ids.btn_wood;
        case ACT_LEVER: return ids.lever;
        case ACT_CAKE: return ids.cake;
        case ACT_REP_U: return ids.rep_u;
        case ACT_REP_P: return ids.rep_p;
        case ACT_CMP_U: return ids.cmp_u;
        case ACT_CMP_P: return ids.cmp_p;
        case ACT_CAULDRON: return ids.cauldron;
        case ACT_FURNACE: return ids.furnace;
        case ACT_FURNACE_LIT: return ids.furnace_lit;
        case ACT_DISPENSER: return ids.dispenser;
        case ACT_WORKBENCH: return ids.workbench;
        default: return ids.air;
    }
}

/* The case's setup: the site's cells back to the drawn state, with the
 * write listener off, exactly the oracle's setupCase writes. base is the
 * site base cell, the support cell. */
static void setup_case(act_env *d, const act_case *c, int base_x, int base_y, int base_z)
{
    struct world *w = d->w;
    int x = base_x, y = base_y + 1, z = base_z;
    switch (c->kind)
    {
        case ACT_DOOR_W:
        case ACT_DOOR_I:
            world_set_block(w, x, y, z, c->kind == ACT_DOOR_W ? ids.door_w : ids.door_i, c->meta, 2);
            world_set_block(w, x, y + 1, z, c->kind == ACT_DOOR_W ? ids.door_w : ids.door_i, (c->meta & 3) | 8, 2);
            break;

        case ACT_BED:
            /* the nether run's support rewrite (the probe's setupCase does
             * this unrecorded for dim != 0) */
            if (!d->can_respawn) world_set_block(w, x, y - 1, z, 87, 0, 2);

            world_set_block(w, x, y, z, ids.bed, c->bed_dir | (c->foot_occ ? 4 : 0), 2);
            world_set_block(w, x + 1, y, z, ids.bed, c->bed_dir | 8 | (c->head_occ ? 4 : 0), 2);
            break;

        case ACT_CHEST:
            world_set_block(w, x, y, z, ids.chest, 0, 2);
            world_set_block(w, x, y + 1, z, c->chest_blocked ? 1 : 0, 0, 2);
            break;

        case ACT_CHEST2:
            world_set_block(w, x, y, z, ids.chest, 0, 2);
            world_set_block(w, x + 1, y, z, ids.chest, 0, 2);
            break;

        case ACT_POT:
        {
            int meta = c->pot_full ? c->pot_data : 0;

            world_set_block(w, x, y, z, ids.pot, meta, 2);
            {
                struct tile_entity *te = world_tile_entity(w, x, y, z);
                te->u.pot.item = c->pot_full ? c->pot_item : -1;
                te->u.pot.data = c->pot_full ? c->pot_data : 0;
            }
            break;
        }

        case ACT_JUKEBOX:
            world_set_block(w, x, y, z, ids.jukebox, c->jmeta, 2);
            {
                struct tile_entity *te = world_tile_entity(w, x, y, z);
                te->u.jukebox.item = c->jdisc ? c->jdisc : -1;
                te->u.jukebox.damage = 0;
                te->u.jukebox.count = c->jdisc ? 1 : 0;
            }
            break;

        case ACT_NOTE:
            world_set_block(w, x, y, z, ids.note, 0, 2);
            {
                struct tile_entity *te = world_tile_entity(w, x, y, z);
                te->u.note.note = c->note_pitch;
            }
            break;

        default:
            world_set_block(w, x, y, z, kind_block_id(c->kind), c->meta, 2);
    }
}

void act_case_begin(act_env *d, const act_case *c)
{
    /* the setup's writes are not recorded */
    d->saved_write = d->w->on_block;
    d->saved_write_ctx = d->w->on_block_ctx;
    d->w->on_block = NULL;
    d->w->on_block_ctx = NULL;

    /* the player */
    d->pos_x = c->px;
    d->pos_y = c->py;
    d->pos_z = c->pz;
    d->sleeping = 0;
    d->food_level = c->food_level;
    d->food_sat = c->food_sat;
    d->food_exh = 0.0F;
    d->chat = 0;
    d->stat_use = 0;
    d->open_gui = 0;
    d->events.n = 0;
    d->case_kind = c->kind;
    d->case_x = c->x;
    d->case_y = c->y;
    d->case_z = c->z;
    d->case_half = c->half;
    for (int s = 0; s < 40; ++s) craft_stack_free(&d->player.slot[s]);

    if (c->held_item != 0)
    {
        d->player.slot[0].item = c->held_item;
        d->player.slot[0].damage = c->held_damage;
        d->player.slot[0].count = c->held_count;
    }

    if (c->inv_full)
    {
        for (int s = 1; s < 36; ++s)
        {
            d->player.slot[s].item = 4;   /* cobblestone */
            d->player.slot[s].damage = 0;
            d->player.slot[s].count = 64;
        }
    }

    /* the entities and the block-callback environment: the setup runs with
     * the probe's capture detached, so a pop it makes (a neighbour repeater
     * losing its floor, a bed losing its pair) spawns into the entity world
     * without joining the case's ents record; the streams it draws from are
     * the previous case's, and the case's reseed happens after, as the
     * probe's loop does */
    ie_free(&d->iew);
    ie_init(&d->iew, d->w, d->det);
    d->iew.role = DET_OTHER;
    d->nents = 0;
    d->events.n = 0;
    d->setup = 1;

    nw_env->blockcb.env.world_rand = &d->wr;
    nw_env->blockcb.env.det = d->det;
    nw_env->blockcb.env.item_drop = act_item_drop;
    nw_env->blockcb.env.ctx = d;
    nw_env->blockcb.env.item_spill = NULL;

    /* the site, then the listener back on */
    {
        int base_x = c->x, base_y = c->y - 1;

        if ((c->kind == ACT_DOOR_W || c->kind == ACT_DOOR_I) && c->half == 1) base_y = c->y - 2;
        else if (c->half == 1) base_x = c->x - 1;

        setup_case(d, c, base_x, base_y, c->z);
    }

    d->w->on_block = d->saved_write;
    d->w->on_block_ctx = d->saved_write_ctx;
    d->setup = 0;

    /* the streams, reseeded after the setup like the probe's loop */
    jr_seed(&d->wr, c->opseed);
    det_rng_set_seed(&d->det->math[DET_OTHER], ACT_MATH_SEED);

    /* the entities the call spawns start after the setup's own pops, which
     * the probe's capture never sees */
    d->iew_call_start = d->iew.n;
}

/* The EntityPlayer.sleepInBedAt statuses. */
enum { SLEEP_OK = 0, SLEEP_NOT_POSSIBLE_NOW = 1 };

/* ItemInWorldManager.activateBlockOrUseItem: 1 when the call returned true. */
int act_run(act_env *d, const act_case *c)
{
    int ret;


    /* (!isSneaking() || getHeldItem() == null) && onBlockActivated */
    ret = 0;

    if (!c->sneak || c->held_item == 0)
    {
        if (on_block_activated(d, c)) ret = 1;
    }
    /* sneaking with a held stack skips the activation, the item half's case
     * (lane itemuse) */

    if (ret == 0 && c->held_item != 0)
    {
        /* ItemStack.tryPlaceItemIntoWorld, the paths the held pools reach: only the
         * record's onItemUse inserts (the glass bottle and the rest answer
         * false from their own Item.onItemUse on a jukebox) */
        if (block_at(d->w, c->x, c->y, c->z) == ids.jukebox
            && world_get_meta(d->w, c->x, c->y, c->z) == 0
            && ITEMS[c->held_item].exists && strcmp(ITEMS[c->held_item].class_name, "ItemRecord") == 0)
        {
            /* ItemRecord.onItemUse */
            struct tile_entity *te = world_tile_entity(d->w, c->x, c->y, c->z);

            te->u.jukebox.item = c->held_item;
            te->u.jukebox.damage = c->held_damage;
            te->u.jukebox.count = c->held_count;
            /* TileEntityJukebox.func_145857_a's markDirty: World.func_147453_f */
            comparator_notify(d->w, c->x, c->y, c->z, ids.jukebox);
            set_meta(d, c->x, c->y, c->z, 1, 2);   /* func_149926_b's write */
            /* playAuxSFXAtEntity(1005): no state */

            /* --stackSize: ItemRecord's tail, no null-at-zero (the count 0
             * stack stays in the slot) */
            --d->player.slot[0].count;
            d->stat_use = 1;
            ret = 1;
        }
        else if (ITEMS[c->held_item].kind == ITEM_BLOCK)
        {
            /* ItemStack.tryPlaceItemIntoWorld's ItemBlock.onItemUse: the plant
             * a full pot's refusal reaches (the pot's held pool is the plants
             * only, all bushes, so their null collision box keeps the entity
             * check out and the base canReplace passes on a replaceable
             * cell) */
            int px = c->x, py = c->y, pz = c->z;

            /* the face shift: the clicked cell is the full pot here, never
             * the snow layer, vine, tall grass or dead bush the shift skips */
            switch (c->side)
            {
                case 0: --py; break;
                case 1: ++py; break;
                case 2: --pz; break;
                case 3: ++pz; break;
                case 4: --px; break;
                case 5: ++px; break;
            }

            if (py < 256 && MATERIALS[BLOCKS[block_at(d->w, px, py, pz) & 4095].material].replaceable)
            {
                /* the Bush's canPlaceBlockAt: the soil under the placed cell must be
                 * grass, dirt or farmland. canPlayerEdit is true, the pots sit far
                 * below y 255, and Item.getMetadata is 0, so the plant places with
                 * meta 0 */
                if (blockcb_can_stay(ITEMS[c->held_item].block_id, d->w, px, py, pz))
                {
                    /* ItemBlock.onItemUse's meta: ItemBlock.getMetadata is the
                     * held stack's damage (the saplings carry theirs) */
                    world_set_block(d->w, px, py, pz, ITEMS[c->held_item].block_id, c->held_damage, 3);
                    --d->player.slot[0].count;
                    d->stat_use = 1;
                    ret = 1;
                }
            }
        }
        /* the base Item.onItemUse answers false for the rest */
    }

    d->ret = ret;
    return ret;
}

/* The case's records; the entities are read from the env's world. */
void act_case_end(act_env *d, act_out *out)
{
    /* the GUI the call opened, the way a client closing the window does */
    out->gui = d->open_gui != 0 ? 1 : 0;

    if (d->open_gui == 1)
    {
        chest_close(d, world_tile_entity(d->w, d->open_x, d->open_y, d->open_z));
    }
    else if (d->open_gui == 2)
    {
        chest_close(d, world_tile_entity(d->w, d->open_ux, d->open_y, d->open_uz));
        chest_close(d, world_tile_entity(d->w, d->open_px, d->open_y, d->open_pz));
    }
    d->open_gui = 0;

    nw_env->blockcb.env.world_rand = NULL;
    nw_env->blockcb.env.det = NULL;
    nw_env->blockcb.env.item_drop = NULL;
    nw_env->blockcb.env.ctx = NULL;

    /* the spawned entities, in spawn order, with their final state: the
     * call's own spawns, after the setup's unrecorded pops */
    d->nents = 0;

    for (int i = d->iew_call_start; i < d->iew.n && d->nents < ACT_MAX_ENTS; ++i)
    {
        ie_ent *en = ie_ent_at(d->iew.slot[i]);
                act_ent *e = &d->ents[d->nents++];
        e->entity_id = en->entity_id;
        e->item = en->stack_item;
        e->damage = en->stack_damage;
        e->count = en->stack_count;
        e->x = en->e.pos_x;
        e->y = en->e.pos_y;
        e->z = en->e.pos_z;
        e->mx = en->e.motion_x;
        e->my = en->e.motion_y;
        e->mz = en->e.motion_z;
        e->yaw = en->rotation_yaw;
    }

    ie_free(&d->iew);
    ie_init(&d->iew, d->w, d->det);
    d->iew.role = DET_OTHER;

    out->ret = d->ret;
    out->stat_use = d->stat_use;
    out->chat = d->chat;
    out->sleeping = d->sleeping;
    out->px = d->pos_x;
    out->py = d->pos_y;
    out->pz = d->pos_z;
    out->food_level = d->food_level;
    out->food_sat = d->food_sat;
    out->food_exh = d->food_exh;

    struct craft_stack *held = &d->player.slot[0];
    out->held_item = craft_slot_empty(held) ? 0 : held->item;
    out->held_damage = craft_slot_empty(held) ? 0 : held->damage;
    out->held_count = craft_slot_empty(held) ? 0 : held->count;

    out->wr_state = d->wr.seed;
    out->math_state = det_rng_state(&d->det->math[DET_OTHER]);
    out->seeder_state = det_rng_state(&d->det->seeder[DET_OTHER]);
    out->next_id = d->det->next_id[DET_OTHER];
    out->tick_entry = ticks_next_entry_id();
    out->n_new_ticks = 0;

    /* the tile entities the case's kind reads */
    struct tile_entity *te = world_tile_entity(d->w, d->case_x, d->case_y, d->case_z);

    if (d->case_kind == ACT_CHEST)
    {
        out->chest_num = te->u.chest.players_using;
        out->chest_pair_num = -1;
    }
    else if (d->case_kind == ACT_CHEST2)
    {
        /* the oracle reads the site's left chest and its right partner,
         * whichever half the case clicked */
        int left = d->case_x - (d->case_half == 1 ? 1 : 0);

        out->chest_num = world_tile_entity(d->w, left, d->case_y, d->case_z)->u.chest.players_using;
        out->chest_pair_num = world_tile_entity(d->w, left + 1, d->case_y, d->case_z)->u.chest.players_using;
    }
    else
    {
        out->chest_num = -1;
        out->chest_pair_num = -1;
    }

    if (d->case_kind == ACT_POT)
    {
        out->pot_item = te->u.pot.item;
        out->pot_data = te->u.pot.data;
    }
    else
    {
        out->pot_item = -1;
        out->pot_data = 0;
    }

    if (d->case_kind == ACT_NOTE) out->note_pitch = te->u.note.note;
    else out->note_pitch = -1;

    if (d->case_kind == ACT_JUKEBOX)
    {
        out->disc_item = te->u.jukebox.item;
        out->disc_count = te->u.jukebox.count;
    }
    else
    {
        out->disc_item = -1;
        out->disc_count = 0;
    }

    if (d->case_kind == ACT_CMP_U || d->case_kind == ACT_CMP_P)
        out->cmp_out = te->u.comparator.out_signal;
    else out->cmp_out = -1;
}

/* The live paths' act_env, too large for a frame: the environment's scratch,
 * zeroed as the old `act_env d = {0}` was, given back at scope exit. */
act_env *act_scratch_begin(void)
{
    if (nw_scratch->act_depth == ACT_SCRATCH_DEPTH) abort();
    act_env *d = &nw_scratch->act[nw_scratch->act_depth++];
    memset(d, 0, sizeof *d);
    return d;
}

void act_scratch_end(act_env **d)
{
    (void)d;
    --nw_scratch->act_depth;
}
