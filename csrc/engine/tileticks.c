/* The tile entity pass of World.updateEntities and each tick class's
 * updateEntity. See tileticks.h. */
#include "comparator.h"
#include "tileticks.h"
#include "env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockcb.h"
#include "blocks.h"
#include "jmath.h"
#include "smath.h"
#include "items.h"
#include "nbtjson.h"
#include "smelting.h"
#include "terrain.h"

/* Math.PI, as the double the Java field holds. */
#define PI_D 3.141592653589793

/* The tick's world state, what the pass reads. */
#define tt_world_time (nw_env->tileticks.world_time)
#define tt_total_time (nw_env->tileticks.total_time)
#define tt_skylight_subtracted (nw_env->tileticks.skylight_subtracted)
#define tt_cos_table (nw_env->tileticks.cos_table)
#define tt_world_rand (nw_env->tileticks.world_rand)

/* BlockFurnace.field_149934_M: the lit/unlit swap in progress. */
#define tt_furnace_swapping (nw_env->tileticks.furnace_swapping)

/* The beacon layers' blocks (BlockBeacon.func_146004_e's materials), which
 * terrain.h does not carry; the tile entity blocks' ids are tileentity.h's. */
enum { BLK_EMERALD_BLOCK = 133, BLK_GOLD_BLOCK = 41, BLK_DIAMOND_BLOCK = 57, BLK_IRON_BLOCK = 42 };

/* The furnace's fuel values (TileEntityFurnace.func_145952_a), in the order
 * the Java if-chain tests them. */
int furnace_fuel_value(int item)
{
    const struct item_def *it = &ITEMS[item & 4095];

    if (!it->exists) return 0;

    if (it->kind == ITEM_BLOCK && it->block_id > 0)
    {
        int block = it->block_id & 4095;

        if (block == 126) return 150;   /* Blocks.wooden_slab */
        if (MATERIALS[BLOCKS[block].material].name[0] && strcmp(MATERIALS[BLOCKS[block].material].name, "wood") == 0)
            return 300;
        if (block == 173) return 16000; /* Blocks.coal_block */
    }

    if (it->kind == ITEM_TOOL && TOOL_MATERIALS[it->tool_material].name[0]
        && strcmp(TOOL_MATERIALS[it->tool_material].name, "WOOD") == 0)
        return 200;
    if (it->kind == ITEM_SWORD && TOOL_MATERIALS[it->tool_material].name[0]
        && strcmp(TOOL_MATERIALS[it->tool_material].name, "WOOD") == 0)
        return 200;
    if (it->class_name[0] && strcmp(it->class_name, "ItemHoe") == 0
        && TOOL_MATERIALS[it->tool_material].name[0]
        && strcmp(TOOL_MATERIALS[it->tool_material].name, "WOOD") == 0)
        return 200;
    if (it->name[0] && strcmp(it->name, "minecraft:stick") == 0) return 100;
    if (it->name[0] && strcmp(it->name, "minecraft:coal") == 0) return 1600;
    if (it->name[0] && strcmp(it->name, "minecraft:lava_bucket") == 0) return 20000;
    if (it->name[0] && strcmp(it->name, "minecraft:sapling") == 0) return 100;
    if (it->name[0] && strcmp(it->name, "minecraft:blaze_rod") == 0) return 2400;

    return 0;
}

/* Item.getContainerItem for the furnace's fuel slot: the buckets give back an
 * empty bucket, everything else null (-1). */
static int stack_container_item(int item)
{
    const char *cls = ITEMS[item & 4095].class_name;

    return cls[0] && strcmp(cls, "ItemBucket") == 0 ? 325 : -1;
}

static void stack_clear(struct te_stack *s)
{
    s->item = -1;
    s->damage = 0;
    s->count = 0;
    s->tag = 0;
}

/* TileEntityFurnace.func_145948_k (canSmelt): an input, a recipe, and output
 * room (the same item while below the stack limit). */
static int furnace_can_smelt(const struct tile_entity *te)
{
    const struct te_furnace *f = &te->u.furnace;

    if (f->slots[0].item < 0) return 0;

    struct craft_stack out;

    if (!smelt_result(f->slots[0].item, f->slots[0].damage, &out)) return 0;

    if (f->slots[2].item < 0) return 1;
    if (f->slots[2].item != out.item || f->slots[2].damage != out.damage) return 0;

    int limit = ITEMS[f->slots[2].item & 4095].max_stack_size;

    return f->slots[2].count < 64 && f->slots[2].count < limit ? 1
         : f->slots[2].count < out.count && f->slots[2].count < limit ? 0
         : f->slots[2].count < limit;
}

/* TileEntityFurnace.func_145949_j (smelt): the result in, the input down. */
static void furnace_smelt(struct tile_entity *te)
{
    struct te_furnace *f = &te->u.furnace;

    if (!furnace_can_smelt(te)) return;

    struct craft_stack out;

    (void)smelt_result(f->slots[0].item, f->slots[0].damage, &out);

    if (f->slots[2].item < 0)
    {
        f->slots[2].item = out.item;
        f->slots[2].damage = out.damage;
        f->slots[2].count = out.count;
    }
    else if (f->slots[2].item == out.item)
    {
        ++f->slots[2].count;
    }

    if (--f->slots[0].count <= 0) stack_clear(&f->slots[0]);
}

/* TileEntityFurnace.updateEntity, server half. */
static void furnace_update(struct world *w, struct tile_entity *te)
{
    struct te_furnace *f = &te->u.furnace;
    int was_burning = f->burn_time > 0;
    int changed = 0;

    if (f->burn_time > 0) --f->burn_time;

    if (f->burn_time != 0 || (f->slots[1].item >= 0 && f->slots[0].item >= 0))
    {
        if (f->burn_time == 0 && furnace_can_smelt(te))
        {
            f->fuel_total = f->burn_time = furnace_fuel_value(f->slots[1].item);

            if (f->burn_time > 0)
            {
                changed = 1;

                if (f->slots[1].item >= 0)
                {
                    if (--f->slots[1].count == 0)
                    {
                        int container = stack_container_item(f->slots[1].item);
                        stack_clear(&f->slots[1]);

                        if (container >= 0)
                        {
                            f->slots[1].item = container;
                            f->slots[1].count = 1;
                        }
                    }
                }
            }
        }

        if (f->burn_time > 0 && furnace_can_smelt(te))
        {
            if (++f->cook_time == 200)
            {
                f->cook_time = 0;
                furnace_smelt(te);
                changed = 1;
            }
        }
        else
        {
            f->cook_time = 0;
        }
    }

    if (was_burning != (f->burn_time > 0))
    {
        changed = 1;

        /* BlockFurnace.func_149931_a: save the metadata and the entity, swap
         * the block with the flag up (breakBlock's drops skip, its
         * removeTileEntity still runs), restore the metadata, then put the
         * saved entity back (validate + setTileEntity). */
        int saved_meta = world_get_meta(w, te->x, te->y, te->z);
        struct tile_entity *saved = world_tile_entity(w, te->x, te->y, te->z);

        tt_furnace_swapping = 1;
        world_set_block(w, te->x, te->y, te->z, f->burn_time > 0 ? BLK_LIT_FURNACE : BLK_FURNACE, 0, 3);
        tt_furnace_swapping = 0;

        world_set_meta(w, te->x, te->y, te->z, saved_meta, 2);

        if (saved != NULL)
        {
            saved->invalid = 0;
            world_set_tile_entity(w, te->x, te->y, te->z, saved);
        }

    }

    if (changed)
    {
        /* TileEntity.onInventoryChanged: the block metadata re-read, the
         * chunk mark and World.func_147453_f (comparator.c) */
        te->block_metadata = world_get_meta(w, te->x, te->y, te->z);
        comparator_notify(w, te->x, te->y, te->z, world_get_block(w, te->x, te->y, te->z));
    }
}

/* BlockDaylightDetector.func_149957_e through TileEntityDaylightDetector.
 * updateEntity: the metadata follows the sky light through the celestial
 * angle every 20th tick (the cos table is the recorded per-tick one). */
static void detector_update(struct world *w, struct tile_entity *te)
{
    if (tt_total_time % 20L != 0L) return;

    int meta = world_get_meta(w, te->x, te->y, te->z);
    int light = world_get_light(w, LIGHT_SKY, te->x, te->y, te->z) - tt_skylight_subtracted;

    /* func_149957_e calls getCelestialAngleRadians(1.0F); the probe's table
     * is that call with the loop index as partialTicks, so the entry the
     * detector wants is the one at 1.0F */
    float radians = tt_cos_table[1];

    if (radians < (float)PI_D) radians += (0.0F - radians) * 0.2F;
    else radians += (((float)PI_D * 2.0F) - radians) * 0.2F;

    float cos_a = mh_cos(radians);

    float v = (float)light * cos_a;
    int rounded = (int)floorf(v + 0.5F); /* Math.round(float) */

    if (rounded < 0) rounded = 0;
    if (rounded > 15) rounded = 15;

    if (meta != rounded)
        world_set_meta(w, te->x, te->y, te->z, rounded, 3);
}

/* TileEntityHopper.updateEntity with config.yaml redstone off: the cooldown
 * ticks down and, once non-positive, resets to 0 (func_145887_i returns
 * false before touching anything). */
static void hopper_update(struct world *w, struct tile_entity *te)
{
    (void)w;
    struct te_hopper *h = &te->u.hopper;

    --h->cooldown;

    if (h->cooldown <= 0) h->cooldown = 0;
}

/* TileEntityChest.updateEntity: func_145979_i (the neighbor scan), the
 * recount every 200 ticks over the players whose open window shows this
 * chest (tt_env), then the lid fields and its open and close sounds. */
/* TileEntityChest.func_145978_a on a neighbour the scan found: its cached
 * side toward the scanning chest must hold a chest, else its scan reruns. */
static void chest_neighbor_check(struct world *w, int x, int y, int z, int side, int scanner_invalid)
{
    struct chunk *ch = world_chunk(w, x >> 4, z >> 4);
    struct tile_entity *n = ch ? te_find(&ch->tes, x, y, z) : NULL;
    if (!n || n->kind != TE_CHEST) return;
    if (scanner_invalid) { n->u.chest.adjacent_checked = 0; return; }
    if (!n->u.chest.adjacent_checked) return;
    if (!n->u.chest.adj[side]) n->u.chest.adjacent_checked = 0;
}

/* TileEntityChest.func_145979_i: with the flag clear, the four neighbours in
 * Java's order (x-1, x+1, z-1, z+1; World.getBlock loads a missing chunk), a
 * neighbour of this chest's kind remembered per side, then each found
 * neighbour's func_145978_a. */
static void chest_scan(struct world *w, struct tile_entity *te, int invalid)
{
    struct te_chest *c = &te->u.chest;
    if (c->adjacent_checked) return;
    c->adjacent_checked = 1;
    int id = world_get_block(w, te->x, te->y, te->z) & 4095;
    c->adj[3] = (world_get_block(w, te->x - 1, te->y, te->z) & 4095) == id;
    c->adj[2] = (world_get_block(w, te->x + 1, te->y, te->z) & 4095) == id;
    c->adj[0] = (world_get_block(w, te->x, te->y, te->z - 1) & 4095) == id;
    c->adj[1] = (world_get_block(w, te->x, te->y, te->z + 1) & 4095) == id;
    if (c->adj[0]) chest_neighbor_check(w, te->x, te->y, te->z - 1, 1, invalid);
    if (c->adj[1]) chest_neighbor_check(w, te->x, te->y, te->z + 1, 0, invalid);
    if (c->adj[2]) chest_neighbor_check(w, te->x + 1, te->y, te->z, 3, invalid);
    if (c->adj[3]) chest_neighbor_check(w, te->x - 1, te->y, te->z, 2, invalid);
}

static void chest_update(struct world *w, struct tile_entity *te)
{
    struct te_chest *c = &te->u.chest;

    chest_scan(w, te, 0);
    ++c->tick_counter;

    if (c->players_using != 0
        && (c->tick_counter + te->x + te->y + te->z) % 200 == 0)
    {
        c->players_using = nw_env->tileticks.env.chest_viewers ? nw_env->tileticks.env.chest_viewers(nw_env->tileticks.env.ctx, w, te) : 0;
    }

    /* the lid angle: opens while a player holds it, else closes */
    c->prev_lid = c->lid;

    /* the sounds play from the half with no chest of its kind at z-1 or x-1
     * (func_145979_i's adjacentChestZNeg and adjacentChestXNeg), read from
     * the scan's cache: the neighbours are read once, when the flag is clear */
    int lead = !c->adj[0] && !c->adj[3];

    if (c->players_using > 0 && c->lid == 0.0F && lead && tt_world_rand)
        (void)jr_float(tt_world_rand); /* random.chestopen pitch */

    if (c->players_using == 0 && c->lid > 0.0F)
    {
        c->lid -= 0.1F;
        if (c->lid < 0.0F) c->lid = 0.0F;
    }
    else if (c->players_using > 0 && c->lid < 1.0F)
    {
        c->lid += 0.1F;
        if (c->lid > 1.0F) c->lid = 1.0F;
    }

    if (c->players_using == 0 && c->lid < 0.5F && c->prev_lid >= 0.5F && lead && tt_world_rand)
        (void)jr_float(tt_world_rand); /* random.chestclosed pitch */
}

/* TileEntityEnderChest.updateEntity: the same lid bookkeeping and the
 * 20-tick block event. The event carries the count as it is now; the next
 * tick's func_147488_Z sets the count back to it after any open or close
 * queued between (world_block_events_run). */
static void ender_chest_update(struct world *w, struct tile_entity *te)
{
    struct te_chest *c = &te->u.chest;

    if (++c->tick_counter % 20 * 4 == 0)
    {
        /* func_147452_c: the event joins the world's queue (the count set
         * back at func_147488_Z) and the clients' S24 */
        world_block_event(w, te->x, te->y, te->z, 130, 1, c->players_using);
        env_block_event(w, te->x, te->y, te->z, 130, 1, c->players_using);
    }

    c->prev_lid = c->lid;

    /* the lid sounds, unlike the chest's with no z-1/x-1 lead check; the open
     * sound's check runs before the lid step (Java's order) */
    if (c->players_using > 0 && c->lid == 0.0F && tt_world_rand)
        (void)jr_float(tt_world_rand); /* random.chestopen pitch */

    if (c->players_using > 0 && c->lid < 1.0F)
    {
        c->lid += 0.1F;
        if (c->lid > 1.0F) c->lid = 1.0F;
    }
    else if (c->players_using == 0 && c->lid > 0.0F)
    {
        c->lid -= 0.1F;
        if (c->lid < 0.0F) c->lid = 0.0F;
    }

    if (c->lid < 0.5F && c->prev_lid >= 0.5F && tt_world_rand)
        (void)jr_float(tt_world_rand); /* random.chestclosed pitch */
}

/* TileEntityNote.updateEntity is TileEntity's empty body. */
static void note_update(struct world *w, struct tile_entity *te)
{
    (void)w;
    (void)te;
}

/* MobSpawnerBaseLogic.updateSpawner, the whole-server replay's (tt_env). The
 * probe world holds no player within any spawner's range, so there it
 * returns before touching anything, exactly as the oracle's run does. */
static void spawner_update(struct world *w, struct tile_entity *te)
{
    if (nw_env->tileticks.env.spawner) nw_env->tileticks.env.spawner(nw_env->tileticks.env.ctx, w, te);
}

/* TileEntityBeacon.func_146003_y (the levels), every 80th tick. */
static void beacon_update(struct world *w, struct tile_entity *te)
{
    if (tt_total_time % 80L != 0L) return;

    struct te_beacon *b = &te->u.beacon;

    if (!world_can_block_see_the_sky(w, te->x, te->y + 1, te->z))
    {
        b->levels = 0;
        return;
    }

    int levels = 0;

    for (int level = 1; level <= 4; ++level)
    {
        int y = te->y - level;

        if (y < 0) break;

        int ok = 1;

        for (int dz = -level; dz <= level && ok; ++dz)
        {
            for (int dx = -level; dx <= level; ++dx)
            {
                int block = world_get_block(w, te->x + dx, y, te->z + dz) & 4095;

                if (block != BLK_EMERALD_BLOCK && block != BLK_GOLD_BLOCK
                    && block != BLK_DIAMOND_BLOCK && block != BLK_IRON_BLOCK)
                {
                    ok = 0;
                    break;
                }
            }
        }

        if (!ok) break;

        levels = level;
    }

    b->levels = levels;
}

/* TileEntityBrewingStand.updateEntity with config.yaml brewing off: the
 * ingredient checks answer false, so a running brew collapses to 0 and the
 * metadata recompute runs. */
static void brewing_update(struct world *w, struct tile_entity *te)
{
    struct te_brewing *b = &te->u.brewing;

    if (b->brew_time > 0)
    {
        /* func_145940_l brews nothing (func_145934_k is false with brewing
         * off); either way the brew stops and onInventoryChanged runs */
        --b->brew_time;
        b->brew_time = 0;
        te->block_metadata = world_get_meta(w, te->x, te->y, te->z);
        comparator_notify(w, te->x, te->y, te->z, world_get_block(w, te->x, te->y, te->z));
    }

    /* func_145939_j: the filled-bottle bitmask, against the mask this entity
     * last wrote (field_145943_l), not the block's metadata */
    int mask = 0;

    for (int i = 0; i < 3; ++i)
        if (b->slots[i].item >= 0) mask |= 1 << i;

    if (mask != b->filled_slots)
    {
        b->filled_slots = mask;
        world_set_meta(w, te->x, te->y, te->z, mask, 2);
    }
}

/* TileEntityEnchantmentTable.updateEntity: the float bookkeeping, no NBT.
 * The closest-player branch (a player within 3, tt_env) turns the book to
 * them and draws the class's static Random (field_145923_r, the server
 * role's stream) to pick the next page; the probe has no player. */
static int enchant_rand_int_n(det_state *det, int role, int n)
{
    static const char *const name = "./net/minecraft/tileentity/TileEntityEnchantmentTable.java:field_145923_r";
    det_split *sp = det_split_find(det, name);

    if (sp == NULL) sp = det_split_random(det, name);
    return det_split_int_n_role(det, sp, role, n);
}

static void enchant_update(struct world *w, struct tile_entity *te)
{
    const struct tt_env *env = &nw_env->tileticks.env;
    double cx = (double)((float)te->x + 0.5F), cy = (double)((float)te->y + 0.5F), cz = (double)((float)te->z + 0.5F);
    double px = 0.0, pz = 0.0;
    int near = env->closest_player != NULL && env->det != NULL &&
               env->closest_player(env->ctx, w, cx, cy, cz, 3.0, &px, &pz);
    tileticks_enchant_step(&te->u.enchant, te->x, te->z, near, px, pz, env->det, DET_SERVER);
}

void tileticks_enchant_step(struct te_enchant *e, int x, int z, int near, double px, double pz, det_state *det,
                            int role)
{
    double cx = (double)((float)x + 0.5F), cz = (double)((float)z + 0.5F);

    e->ot = e->tRot;

    if (near)
    {
        double var2 = px - cx;
        double var4 = pz - cz;

        e->otilde = (float)fd_atan2(var4, var2);
        e->tRot += 0.1F;

        if (e->tRot < 0.5F || enchant_rand_int_n(det, role, 40) == 0)
        {
            float var6 = e->flip;

            do
            {
                int a = enchant_rand_int_n(det, role, 4);
                int b = enchant_rand_int_n(det, role, 4);
                e->flip += (float)(a - b);
            }
            while (var6 == e->flip);
        }
    }
    else
    {
        e->otilde += 0.02F;
        e->tRot -= 0.1F;
    }

    while (e->book_rotation >= (float)PI_D) e->book_rotation -= (float)PI_D * 2.0F;
    while (e->book_rotation < -(float)PI_D) e->book_rotation += (float)PI_D * 2.0F;
    while (e->otilde >= (float)PI_D) e->otilde -= (float)PI_D * 2.0F;
    while (e->otilde < -(float)PI_D) e->otilde += (float)PI_D * 2.0F;

    float delta = e->otilde - e->book_rotation;
    while (delta >= (float)PI_D) delta -= (float)PI_D * 2.0F;
    while (delta < -(float)PI_D) delta += (float)PI_D * 2.0F;

    e->book_rotation += delta * 0.4F;

    if (e->tRot < 0.0F) e->tRot = 0.0F;
    if (e->tRot > 1.0F) e->tRot = 1.0F;

    ++e->book_spread;
    e->prev_book_rotation = e->book_rotation_2;
    float f3 = (e->flip - e->book_rotation_2) * 0.4F;
    float f8 = 0.2F;

    if (f3 < -f8) f3 = -f8;
    if (f3 > f8) f3 = f8;

    e->flipT += (f3 - e->flipT) * 0.9F;
    e->book_rotation_2 += e->flipT;
}

/* TileEntityPiston.updateEntity: the progress advance and the finish. */
static void piston_update(struct world *w, struct tile_entity *te)
{
    struct te_piston *p = &te->u.piston;

    p->last_progress = p->progress;

    if (p->last_progress >= 1.0F)
    {
        /* func_145863_a(1.0F, 0.25F): the moving box, and the entity push
         * inside it. The probe world holds no entities, so the push list is
         * empty and the box computation has no effect to record. */
        world_remove_tile_entity(w, te->x, te->y, te->z);
        te->invalid = 1;

        if (world_get_block(w, te->x, te->y, te->z) == BLK_PISTON_MOVING)
        {
            world_set_block(w, te->x, te->y, te->z, p->block_id, p->block_meta, 3);

            /* func_147460_e: the standing block's onNeighborBlockChange with
             * the pushed block as the argument */
            block_on_neighbor_changed(w, world_get_block(w, te->x, te->y, te->z) & 4095,
                world_get_meta(w, te->x, te->y, te->z), te->x, te->y, te->z, p->block_id);
        }

        return;
    }

    p->progress += 0.5F;
    if (p->progress >= 1.0F) p->progress = 1.0F;

    /* func_145863_a only runs while extending; the probe's entity is not */
}

/* One tile entity's updateEntity, the dispatch by kind. */
static void te_update(struct world *w, struct tile_entity *te)
{
    switch (te->kind)
    {
    case TE_FURNACE: furnace_update(w, te); break;
    case TE_DAYLIGHT_DETECTOR: detector_update(w, te); break;
    case TE_HOPPER: hopper_update(w, te); break;
    case TE_CHEST: chest_update(w, te); break;
    case TE_ENDER_CHEST: ender_chest_update(w, te); break;
    case TE_NOTE: note_update(w, te); break;
    case TE_MOB_SPAWNER: spawner_update(w, te); break;
    case TE_BEACON: beacon_update(w, te); break;
    case TE_BREWING_STAND: brewing_update(w, te); break;
    case TE_ENCHANT_TABLE: enchant_update(w, te); break;
    case TE_PISTON: piston_update(w, te); break;
    default: break;
    }
}

void tileticks_set_time(int64_t world_time, int64_t total_time, int skylight_subtracted, const float *cos_table)
{
    tt_world_time = world_time;
    tt_total_time = total_time;
    tt_skylight_subtracted = skylight_subtracted;
    tt_cos_table = cos_table;
}

void tileticks_set_env(const struct tt_env *env)
{
    if (env) nw_env->tileticks.env = *env;
    else memset(&nw_env->tileticks.env, 0, sizeof nw_env->tileticks.env);
}

void tileticks_set_world_rand(jrand *world_rand)
{
    tt_world_rand = world_rand;
}

int tileticks_furnace_swapping(void)
{
    return tt_furnace_swapping;
}

/* The probe's lid.jsonl.gz line for one chest or ender chest: the three
 * fields updateEntity moves, the position and the tick, as canonical NBT. */
char *tileticks_lid_render(const struct tile_entity *te, int tick)
{
    const char *id;

    if (te->kind == TE_CHEST) id = "Chest";
    else if (te->kind == TE_ENDER_CHEST) id = "EnderChest";
    else return NULL;

    const struct te_chest *c = &te->u.chest;
    nbt *root = nbt_new_compound();

    nbt_put(root, "id", nbt_new_string(id));
    nbt_put(root, "lid", nbt_new_float(c->lid));
    nbt_put(root, "players", nbt_new_int(c->players_using));
    nbt_put(root, "prev", nbt_new_float(c->prev_lid));
    nbt_put(root, "counter", nbt_new_int(c->tick_counter));
    nbt_put(root, "tick", nbt_new_int(tick));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);

    nbt_free(root);
    return out;
}

/* The tile entity half of World.updateEntities, verbatim. */
void tileticks_pass(struct world *w)
{
    w->te_ticking = 1;

    for (int i = 0; i < w->te_n; ++i)
    {
        struct tile_entity *te = w->te_list[i];

        if (!te->invalid && world_chunk_loaded(w, te->x >> 4, te->z >> 4))
            te_update(w, te);

        if (te->invalid)
        {
            if (world_chunk_loaded(w, te->x >> 4, te->z >> 4))
            {
                struct chunk *c = world_chunk(w, te->x >> 4, te->z >> 4);
                te_remove(&c->tes, te->x, te->y, te->z);
            }

            /* the iterator removal: everything after shifts down one */
            for (int j = i + 1; j < w->te_n; ++j) w->te_list[j - 1] = w->te_list[j];

            --w->te_n;
            --i;
        }
    }

    w->te_ticking = 0;

    /* field_147483_b: nothing adds to it in this world */

    /* the addedTileEntityList merge */
    for (int i = 0; i < w->te_added_n; ++i)
    {
        struct tile_entity *te = w->te_added[i];

        if (te->invalid) continue;

        int present = 0;

        for (int j = 0; j < w->te_n; ++j)
            if (w->te_list[j] == te) { present = 1; break; }

        if (!present)
        {
            if (w->te_n == w->te_cap)
            {
                w->te_cap = w->te_cap ? w->te_cap * 2 : 64;
                w->te_list = realloc(w->te_list, (size_t)w->te_cap * sizeof *w->te_list);
            }

            w->te_list[w->te_n++] = te;
        }

        if (world_chunk_loaded(w, te->x >> 4, te->z >> 4))
        {
            /* Chunk.func_150812_a: whatever sat at the position invalidates,
             * then the entity validates and takes the slot. When the stored
             * entity is this one (the furnace swap's case), the fields are
             * already in place and nothing moves. */
            struct chunk *c = world_chunk(w, te->x >> 4, te->z >> 4);
            struct tile_entity *old = te_find(&c->tes, te->x, te->y, te->z);

            /* TileEntityChest.invalidate on the stored entity (the entity
             * itself, for a chunk loaded during the walk): the flag clears and
             * the neighbour scan runs, its reads loading what is missing */
            if (old != NULL && old->kind == TE_CHEST)
            {
                old->u.chest.adjacent_checked = 0;
                chest_scan(w, old, 1);
            }

            if (old != NULL && old != te) old->invalid = 1;

            te->invalid = 0;

            if (old != te)
            {
                /* the old one (a fresh demand entity) leaves the store and
                 * the entity itself takes the slot, as Java's map put does */
                te_remove(&c->tes, te->x, te->y, te->z);

                if (c->tes.n == c->tes.cap)
                {
                    c->tes.cap = c->tes.cap ? c->tes.cap * 2 : 4;
                    c->tes.v = realloc(c->tes.v, (size_t)c->tes.cap * sizeof *c->tes.v);
                }

                c->tes.v[c->tes.n++] = te;
            }
        }

        /* func_147471_g: the client-side render mark, no server effect */
    }

    w->te_added_n = 0;
}
