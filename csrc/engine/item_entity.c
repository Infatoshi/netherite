/* EntityItem and EntityXPOrb, ticked the way World.updateEntities ticks them:
 * the entity pass over the spawn-order list, each entity through
 * updateEntityWithOptionalForce (the tick bookkeeping, the update, the chunk
 * membership), the dead ones out of the list and the chunk. The per-chunk
 * y-section lists are Chunk.entityLists, the insertion-ordered lists
 * EntityItem's merge search walks.
 *
 * Java evaluates operands left to right; the tick paths here draw from the
 * per-entity Random (pushOutOfBlocks, the lava hop, the fizz) and from Det's
 * OTHER streams (the constructors), so every draw is a statement of its own in
 * vanilla's order. */
#include "item_entity.h"
#include "jmath.h"
#include "env.h"
#include "portal.h"
#include "blocks.h"
#include "drops.h"
#include "items.h"
#include "collide.h"
#include "world.h"
#include "projectile.h"
#include "living.h"
#include "blockcb.h"

#include <math.h>
#include "smath.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>


static void ie_extra_boxes(void *self, struct aabb query, struct collide_list *out);

/* BlockLiquid.func_149801_b: the liquid height fraction of a cell. */
static float liquid_height(int meta)
{
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0F;
}

static int liquid_solid(struct world *w, int x, int y, int z, int side);

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* BlockLiquid.func_149798_e, the cell's liquid depth the flow vector reads:
 * the metadata when the block is the same liquid (bit 8 folded to 0), -1
 * otherwise. */
static int liquid_meta(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;

    if (BLOCKS[id].material != 6 /* Material.water */) return -1;

    int meta = world_get_meta(w, x, y, z);
    if (meta >= 8) meta = 0;
    return meta;
}

/* Vec3.normalize: over MathHelper.sqrt_double, the square root rounded to
 * float and widened back; the zero vector below 1.0E-4. */
static void vec_normalize(double *vx, double *vy, double *vz)
{
    double len = (double)(float)sqrt(*vx * *vx + *vy * *vy + *vz * *vz);

    if (len < 1.0E-4)
    {
        *vx = 0.0;
        *vy = 0.0;
        *vz = 0.0;
        return;
    }

    *vx /= len;
    *vy /= len;
    *vz /= len;
}

/* BlockLiquid.func_149800_f, the flow vector of one liquid cell, in the order
 * the decompile runs it: the four horizontal neighbors, then the bit-8 drop
 * toward solid neighbors, then the normalize. */
static void flow_vector(struct world *w, int x, int y, int z, double *ox, double *oy, double *oz)
{
    double vx = 0.0, vy = 0.0, vz = 0.0;
    int var6 = liquid_meta(w, x, y, z);

    for (int var7 = 0; var7 < 4; ++var7)
    {
        int var8 = x;
        int var10 = z;

        if (var7 == 0) var8 = x - 1;
        if (var7 == 1) var10 = z - 1;
        if (var7 == 2) ++var8;
        if (var7 == 3) ++var10;

        int var11 = liquid_meta(w, var8, y, var10);

        if (var11 < 0)
        {
            int nid = world_get_block(w, var8, y, var10) & 4095;
            if (!MATERIALS[BLOCKS[nid].material].blocks_movement)
            {
                var11 = liquid_meta(w, var8, y - 1, var10);
                if (var11 >= 0)
                {
                    int var12 = var11 - (var6 - 8);
                    vx += (double)((var8 - x) * var12);
                    vz += (double)((var10 - z) * var12);
                }
            }
        }
        else
        {
            int var12 = var11 - var6;
            vx += (double)((var8 - x) * var12);
            vz += (double)((var10 - z) * var12);
        }
    }

    if (world_get_meta(w, x, y, z) >= 8)
    {
        int var13 = 0;

        if (var13 || liquid_solid(w, x, y, z - 1, 2)) var13 = 1;
        if (var13 || liquid_solid(w, x, y, z + 1, 3)) var13 = 1;
        if (var13 || liquid_solid(w, x - 1, y, z, 4)) var13 = 1;
        if (var13 || liquid_solid(w, x + 1, y, z, 5)) var13 = 1;
        if (var13 || liquid_solid(w, x, y + 1, z - 1, 2)) var13 = 1;
        if (var13 || liquid_solid(w, x, y + 1, z + 1, 3)) var13 = 1;
        if (var13 || liquid_solid(w, x - 1, y + 1, z, 4)) var13 = 1;
        if (var13 || liquid_solid(w, x + 1, y + 1, z, 5)) var13 = 1;

        if (var13)
        {
            vec_normalize(&vx, &vy, &vz);
            vy -= 6.0;
        }
    }

    vec_normalize(&vx, &vy, &vz);
    *ox = vx;
    *oy = vy;
    *oz = vz;
}

/* BlockLiquid.isBlockSolid for the water block the flow vector runs on: not
 * the same material, side 1 always solid, ice never, else the material's
 * isSolid (Block.isBlockSolid). */
static int liquid_solid(struct world *w, int x, int y, int z, int side)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int m = BLOCKS[id].material;

    if (m == 6 /* Material.water */) return 0;
    if (side == 1) return 1;
    if (m == 21 /* Material.ice */) return 0;
    return MATERIALS[m].is_solid;
}

/* World.handleMaterialAcceleration, the body of
 * EntityItem.handleWaterMovement and EntityXPOrb.handleWaterMovement (the
 * override passes the plain box, no expand): the water cells push a 0.014
 * flow into the motion when there is one. */
static int water_accelerate(struct world *w, struct entity *e, int is_item_or_orb)
{
    struct aabb box = is_item_or_orb ? e->bounding_box
        : aabb_expand(e->bounding_box, -0.001, -0.4000000059604645 - 0.001, -0.001);
    int x0 = mh_floor(box.min_x);
    int x1 = mh_floor(box.max_x + 1.0);
    int y0 = mh_floor(box.min_y);
    int y1 = mh_floor(box.max_y + 1.0);
    int z0 = mh_floor(box.min_z);
    int z1 = mh_floor(box.max_z + 1.0);

    if (!world_check_chunks_exist(w, x0, y0, z0, x1, y1, z1)) return 0;

    int found = 0;
    double vx = 0.0, vy = 0.0, vz = 0.0;

    for (int x = x0; x < x1; ++x)
    {
        for (int y = y0; y < y1; ++y)
        {
            for (int z = z0; z < z1; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;

                if (BLOCKS[id].material == 6 /* Material.water */)
                {
                    double var16 = (double)((float)(y + 1) - liquid_height(world_get_meta(w, x, y, z)));

                    if ((double)y1 >= var16)
                    {
                        found = 1;
                        double fx, fy, fz;
                        flow_vector(w, x, y, z, &fx, &fy, &fz);
                        vx += fx;
                        vy += fy;
                        vz += fz;
                    }
                }
            }
        }
    }

    /* Entity.isPushedByWater is true on both kinds */
    if (vx * vx + vy * vy + vz * vz > 0.0)
    {
        vec_normalize(&vx, &vy, &vz);
        e->motion_x += vx * 0.014;
        e->motion_y += vy * 0.014;
        e->motion_z += vz * 0.014;
    }

    return found;
}

/* -------------------------------------------------------------- the lists */

void ie_init(ie_world *iew, struct world *w, det_state *det)
{
    memset(iew, 0, sizeof *iew);
    iew->w = w;
    iew->det = det;
    iew->role = DET_OTHER;
}

int ie_chunk_drop_empty(ie_world *iew, int cx, int cz)
{
    for (int i = 0; i < iew->nchunks; ++i)
    {
        ie_chunk *c = &iew->chunks[i];
        if (!c->used || c->cx != cx || c->cz != cz) continue;
        for (int s = 0; s < IE_SECTIONS; ++s)
            if (c->sec[s].n) return 0;
        for (int s = 0; s < IE_SECTIONS; ++s) sec_release(&c->sec[s]);
        if (i != iew->nchunks - 1) *c = iew->chunks[iew->nchunks - 1];
        --iew->nchunks;
        return 1;
    }
    return 0;
}

void ie_free(ie_world *iew)
{
    for (int i = 0; i < iew->nchunks; ++i)
        for (int s = 0; s < IE_SECTIONS; ++s) sec_release(&iew->chunks[i].sec[s]);
    slab_table_free(iew->chunks, iew->capchunks, sizeof *iew->chunks);
    iew->chunks = NULL;
    iew->nchunks = iew->capchunks = 0;
}

static ie_chunk *chunk_find(ie_world *iew, int cx, int cz)
{
    for (int i = 0; i < iew->nchunks; ++i)
    {
        if (iew->chunks[i].used && iew->chunks[i].cx == cx && iew->chunks[i].cz == cz) return &iew->chunks[i];
    }

    return NULL;
}

/* Chunk.addEntity: the chunk's section list takes the entity in insertion
 * order and the chunk coords are recorded on it. The y is clamped to a
 * section, exactly Chunk.addEntity's clamp. */
static void chunk_add(ie_world *iew, ie_ent *en, int cx, int cy, int cz)
{
    iew->chunks = slab_table_room(iew->chunks, iew->nchunks, &iew->capchunks, sizeof *iew->chunks);

    ie_chunk *c = chunk_find(iew, cx, cz);

    if (!c)
    {
        c = &iew->chunks[iew->nchunks++];
        /* the section lists are made as they fill (sec_push) */
        memset(c, 0, sizeof *c);
        c->cx = cx;
        c->cz = cz;
        c->used = 1;
    }

    if (cy < 0) cy = 0;
    if (cy >= IE_SECTIONS) cy = IE_SECTIONS - 1;

    en->e.chunk_stamp = ++entity_chunk_stamp;
    en->added_to_chunk = 1;
    en->chunk_x = cx;
    en->chunk_y = cy;
    en->chunk_z = cz;

    sec_push(&c->sec[cy], ie_ent_index(en), IE_MAX_ENTITIES);
}

/* Chunk.removeEntityAtIndex: first occurrence out of the y section. */
static void chunk_remove_at(ie_world *iew, ie_ent *en, int cy)
{
    if (cy < 0) cy = 0;
    if (cy >= IE_SECTIONS) cy = IE_SECTIONS - 1;

    ie_chunk *c = chunk_find(iew, en->chunk_x, en->chunk_z);

    if (!c) return;

    sec_remove_first(&c->sec[cy], ie_ent_index(en));
}

/* ------------------------------------------------------------ the entities */


/* ------------------------------------------------------------ the entities */

/* EntityItem.attackEntityFrom and EntityXPOrb.attackEntityFrom: the damage
 * sources the entity pass reaches are the ones both kinds answer the same way
 * (health down by the amount, dead at or below 0); nether_star explosions and
 * invulnerability do not apply here. The other kinds that reach it (a primed
 * TNT in a fire, a projectile on a cactus) keep Entity.attackEntityFrom:
 * setBeenAttacked and nothing more; EntitySmallFireball's override does not
 * even set that, and EntityFireball's needs an attacker none of these
 * sources has. */
static void ie_attack_from(void *self, int source, float amount)
{
    (void)source;
    ie_ent *en = self;

    if (en->kind == IE_SMALL_FIREBALL) return;

    en->e.velocity_changed = 1; /* setBeenAttacked */

    if (en->kind != IE_ITEM && en->kind != IE_ORB) return;

    en->health = (int)((float)en->health - amount);

    if (en->health <= 0) en->is_dead = 1;
}

/* The fizz sound's two rand draws, reached only when the entity is wet and
 * burning; neither kind is ever wet (inWater stays false on the override, and
 * the probe world never rains), so the branch is dead in the probe. */
static void ie_fizz(void *self)
{
    ie_ent *en = self;

    (void)det_rng_float(&en->rand);
    (void)det_rng_float(&en->rand);
}

/* The Entity constructor: the per-role ID, the per-entity Random from
 * Det.newRandom (one seeder draw) and the UUID (two more). The walking flag
 * goes off: both kinds override canTriggerWalking to false. */
static void entity_common(ie_world *iew, ie_ent *en)
{
    entity_init(&en->e, iew->w);
    en->e.can_trigger_walking = 0;
    en->e.self = en;
    en->e.attack_from = ie_attack_from;
    en->e.fizz = ie_fizz;
    en->e.set_in_portal = ie_set_in_portal;
    en->e.end_portal = ie_end_portal;
    en->e.first_update = 1; en->first_update = 1; /* Entity's constructor sets it true */
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);

    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb); /* the UUID, recorded on nothing */
    en->uuid_msb = msb;
    en->uuid_lsb = lsb;
    en->health = 5;
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->in_tile = 0;
}

ie_ent *ie_spawn_item(ie_world *iew, double x, double y, double z, int item, int damage, int count)
{
    ie_ent *en = ie_ent_alloc();
    entity_common(iew, en);
    en->kind = IE_ITEM;
    en->iew = iew;
    en->e.extra_boxes = ie_extra_boxes;

    /* the EntityItem(World, x, y, z) constructor, in order */
    en->hover_start = (float)(det_math_random_role(iew->det, iew->role) * 3.141592653589793 * 2.0);
    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->rotation_yaw = (float)(det_math_random_role(iew->det, iew->role) * 360.0);
    double m = det_math_random_role(iew->det, iew->role);
    en->e.motion_x = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);
    en->e.motion_y = 0.20000000298023224;
    m = det_math_random_role(iew->det, iew->role);
    en->e.motion_z = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);

    en->stack_item = item;
    en->stack_damage = damage;
    en->stack_count = count;
    en->stack_count_link = stack_link_item(en);

    ie_list_push(iew, en);
    return en;
}

ie_ent *ie_spawn_orb(ie_world *iew, double x, double y, double z, int xp)
{
    ie_ent *en = ie_ent_alloc();
    entity_common(iew, en);
    en->kind = IE_ORB;
    en->iew = iew;
    en->e.extra_boxes = ie_extra_boxes;

    entity_set_size(&en->e, 0.5F, 0.5F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->rotation_yaw = (float)(det_math_random_role(iew->det, iew->role) * 360.0);
    double m = det_math_random_role(iew->det, iew->role);
    en->e.motion_x = (double)((float)(m * 0.20000000298023224 - 0.10000000149011612) * 2.0F);
    m = det_math_random_role(iew->det, iew->role);
    en->e.motion_y = (double)((float)(m * 0.2) * 2.0F);
    m = det_math_random_role(iew->det, iew->role);
    en->e.motion_z = (double)((float)(m * 0.20000000298023224 - 0.10000000149011612) * 2.0F);
    en->xp_value = xp;

    ie_list_push(iew, en);
    return en;
}

/* EntityTNTPrimed's size, offset and the fields after the Entity part. */
static void tnt_fields(ie_ent *en, double x, double y, double z)
{
    en->kind = IE_TNT;
    en->e.can_trigger_walking = 0;
    entity_set_size(&en->e, 0.98F, 0.98F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->prev_x = x;
    en->prev_y = y;
    en->prev_z = z;
}

ie_ent *ie_spawn_tnt(ie_world *iew, double x, double y, double z, int by_player)
{
    ie_ent *en = ie_ent_alloc();
    entity_common(iew, en);
    en->iew = iew;
    en->e.extra_boxes = ie_extra_boxes;
    tnt_fields(en, x, y, z);

    float var9 = (float)(det_math_random_role(iew->det, iew->role) * 3.141592653589793 * 2.0);
    en->e.motion_x = (double)(-((float)fd_sin((double)var9)) * 0.02F);
    en->e.motion_y = 0.20000000298023224;
    en->e.motion_z = (double)(-((float)fd_cos((double)var9)) * 0.02F);
    en->fuse = 80;
    en->tnt_by_player = by_player;
    en->tnt_placer = 0;

    ie_list_push(iew, en);
    return en;
}

/* The same constructors over a drop_ent whose Math.random draws the drop
 * engine already spent. */
ie_ent *ie_spawn_item_state(ie_world *iew, const struct drop_ent *ent)
{
    ie_ent *en = ie_ent_alloc();
    entity_common(iew, en);
    en->kind = IE_ITEM;

    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, ent->x, ent->y, ent->z);
    en->hover_start = ent->hover;
    en->rotation_yaw = ent->yaw;
    en->e.motion_x = ent->mx;
    en->e.motion_y = ent->my;
    en->e.motion_z = ent->mz;
    en->stack_item = ent->item;
    en->stack_damage = ent->damage;
    en->stack_count = ent->count;

    ie_list_push(iew, en);
    return en;
}

ie_ent *ie_spawn_orb_state(ie_world *iew, const struct drop_ent *ent)
{
    ie_ent *en = ie_ent_alloc();
    entity_common(iew, en);
    en->kind = IE_ORB;

    entity_set_size(&en->e, 0.5F, 0.5F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, ent->x, ent->y, ent->z);
    en->rotation_yaw = ent->yaw;
    en->e.motion_x = ent->mx;
    en->e.motion_y = ent->my;
    en->e.motion_z = ent->mz;
    en->xp_value = ent->xp;

    ie_list_push(iew, en);
    return en;
}

void ie_added_to_world(ie_world *iew, ie_ent *en)
{
    /* the world whose lists hold it (its portal and post-update hooks) */
    en->iew = iew;
    int cx = mh_floor(en->e.pos_x / 16.0);
    int cz = mh_floor(en->e.pos_z / 16.0);

    if (world_chunk_loaded(iew->w, cx, cz))
    {
        chunk_add(iew, en, cx, mh_floor(en->e.pos_y / 16.0), cz);
    }
}

/* Chunk.addEntity for an entity a chunk's NBT brings back: the reload adds
 * it to the chunk being read, before that chunk joins the world. */
void ie_add_to_chunk(ie_world *iew, ie_ent *en)
{
    if (en->added_to_chunk) return;
    chunk_add(iew, en, mh_floor(en->e.pos_x / 16.0), mh_floor(en->e.pos_y / 16.0),
              mh_floor(en->e.pos_z / 16.0));
}

/* ------------------------------------------------- the collision queries */

/* The box list Java keeps as World.collidingBoundingBoxes: append only. */
static void box_add(struct collide_list *l, const struct aabb *b)
{
    collide_list_push(l, b);
}

/* World.getCollidingBoundingBoxes' entity half: Java queries every chunk the
 * 0.25-expanded box reaches (getEntitiesWithinAABBExcludingEntity takes the
 * chunk range from the box expanded by 2.0), walks each chunk's y sections from
 * (minY - 2) to (maxY + 2), and adds every entity box that intersects the
 * unexpanded query. Its own box is skipped. */
static void ie_extra_boxes(void *self, struct aabb query, struct collide_list *out)
{
    ie_ent *en = self;
    ie_world *iew = en->iew;

    if (!iew->collide_entities) return;

    /* Entity.getBoundingBox returns null for items, orbs and projectiles.
     * Their boxes never join World.getCollidingBoundingBoxes' entity half. */

    for (int i = 0; i < iew->nother; ++i)
        if (iew->other_boxes[i] != NULL && aabb_intersects(iew->other_boxes[i], &query))
            box_add(out, iew->other_boxes[i]);
}

/* ------------------------------------------------------------- the adopts */

/* The Entity base an adopted entity shares with a constructed one: the fields
 * the constructor's Entity part sets, with the id, the Random state and the
 * UUID handed in (the tick's recorder already drew them). */
static ie_ent *adopt_common(ie_world *iew, int id, int64_t msb, int64_t lsb, uint64_t rand_state)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->iew = iew;
    en->e.extra_boxes = ie_extra_boxes;
    en->e.attack_from = ie_attack_from;
    en->e.fizz = ie_fizz;
    en->e.set_in_portal = ie_set_in_portal;
    en->e.end_portal = ie_end_portal;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = id;
    en->uuid_msb = msb;
    en->uuid_lsb = lsb;
    en->rand.r.seed = rand_state;
    en->health = 5;
    ie_list_push(iew, en);
    return en;
}

ie_ent *ie_adopt_item(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                      double x, double y, double z, double mx, double my, double mz, float yaw, float hover,
                      int item, int damage, int count, int tag)
{
    ie_ent *en = adopt_common(iew, id, uuid_msb, uuid_lsb, rand_state);

    if (en == NULL) return NULL;

    en->kind = IE_ITEM;
    en->e.can_trigger_walking = 0; /* EntityItem's override */
    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    /* the constructor's own draws, already spent by the tick's recorder */
    en->hover_start = hover;
    en->rotation_yaw = yaw;
    en->e.motion_x = mx;
    en->e.motion_y = my;
    en->e.motion_z = mz;
    en->stack_item = item;
    en->stack_damage = damage;
    en->stack_count = count;
    en->stack_tag = tag;
    en->delay = 10;
    return en;
}

ie_ent *ie_adopt_orb(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                     double x, double y, double z, double mx, double my, double mz, float yaw, int xp)
{
    ie_ent *en = adopt_common(iew, id, uuid_msb, uuid_lsb, rand_state);

    if (en == NULL) return NULL;

    en->kind = IE_ORB;
    en->e.can_trigger_walking = 0;
    entity_set_size(&en->e, 0.5F, 0.5F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->rotation_yaw = yaw;
    en->e.motion_x = mx;
    en->e.motion_y = my;
    en->e.motion_z = mz;
    en->xp_value = xp;
    return en;
}

void ie_orb_nbt_size(ie_ent *en)
{
    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
}

ie_ent *ie_adopt_tnt(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                     double x, double y, double z, double mx, double my, double mz, int fuse, int by_player)
{
    ie_ent *en = adopt_common(iew, id, uuid_msb, uuid_lsb, rand_state);

    if (en == NULL) return NULL;

    tnt_fields(en, x, y, z);
    en->e.motion_x = mx;
    en->e.motion_y = my;
    en->e.motion_z = mz;
    en->fuse = fuse;
    en->tnt_by_player = by_player;
    en->tnt_placer = 0;
    return en;
}

/* ------------------------------------------------------------- the ticking */

/* Entity.setFire: the enchantment scan finds nothing on these kinds, so the
 * value only rises to seconds * 20. */
static void set_fire(ie_ent *en, int seconds)
{
    int var2 = seconds * 20;
    if (en->e.fire < var2) en->e.fire = var2;
}

/* Entity.setOnFireFromLava, for the kinds that carry health. */
static void set_on_fire_from_lava(ie_ent *en)
{
    if (en->kind <= IE_ORB) ie_attack_from(en, IE_SRC_LAVA, 4.0F);
    set_fire(en, 15);
}

/* World.isMaterialInBB over one material index. */
static int material_in_bb(struct world *w, struct aabb box, int material)
{
    int x0 = mh_floor(box.min_x);
    int x1 = mh_floor(box.max_x + 1.0);
    int y0 = mh_floor(box.min_y);
    int y1 = mh_floor(box.max_y + 1.0);
    int z0 = mh_floor(box.min_z);
    int z1 = mh_floor(box.max_z + 1.0);

    for (int x = x0; x < x1; ++x)
    {
        for (int y = y0; y < y1; ++y)
        {
            for (int z = z0; z < z1; ++z)
            {
                if (BLOCKS[world_get_block(w, x, y, z) & 4095].material == material) return 1;
            }
        }
    }

    return 0;
}

/* World.func_147469_q: the cell's pool box is a box and averages a full block
 * on an edge. */
static int solid_cell(struct world *w, int x, int y, int z)
{
    struct aabb b;

    if (!collide_pool_box(w, x, y, z, &b)) return 0;

    return (b.max_x - b.min_x + b.max_y - b.min_y + b.max_z - b.min_z) / 3.0 >= 1.0;
}

/* Entity.pushOutOfBlocks (func_145771_j), with the result the item keeps in
 * noClip and the orb drops. The float draw runs after the direction is picked,
 * exactly as the decompile orders it. */
static int push_out_of_blocks(ie_ent *en, double px, double py, double pz)
{
    struct entity *e = &en->e;
    struct world *w = e->world;
    int var7 = mh_floor(px);
    int var8 = mh_floor(py);
    int var9 = mh_floor(pz);
    double var10 = px - (double)var7;
    double var12 = py - (double)var8;
    double var14 = pz - (double)var9;

    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(l);
    world_get_colliding_bounding_boxes(w, e->bounding_box, l); /* World.func_147461_a */

    int center_solid = solid_cell(w, var7, var8, var9);
    if (l->n == 0 && !center_solid) return 0;

    int var17 = !solid_cell(w, var7 - 1, var8, var9);
    int var18 = !solid_cell(w, var7 + 1, var8, var9);
    int var19 = !solid_cell(w, var7, var8 - 1, var9); /* computed, never picked */
    int var20 = !solid_cell(w, var7, var8 + 1, var9);
    int var21 = !solid_cell(w, var7, var8, var9 - 1);
    int var22 = !solid_cell(w, var7, var8, var9 + 1);
    (void)var19;

    int var23 = 3;
    double var24 = 9999.0;

    if (var17 && var10 < var24) { var24 = var10; var23 = 0; }
    if (var18 && 1.0 - var10 < var24) { var24 = 1.0 - var10; var23 = 1; }
    if (var20 && 1.0 - var12 < var24) { var24 = 1.0 - var12; var23 = 3; }
    if (var21 && var14 < var24) { var24 = var14; var23 = 4; }
    if (var22 && 1.0 - var14 < var24) { var24 = 1.0 - var14; var23 = 5; }

    float var26 = det_rng_float(&en->rand) * 0.2F + 0.1F;

    if (var23 == 0) e->motion_x = (double)(-var26);
    if (var23 == 1) e->motion_x = (double)var26;
    if (var23 == 2) e->motion_y = (double)(-var26);
    if (var23 == 3) e->motion_y = (double)var26;
    if (var23 == 4) e->motion_z = (double)(-var26);
    if (var23 == 5) e->motion_z = (double)var26;

    return 1;
}

/* EntityItem.combineItems, from this to the other. */
static int combine_items(ie_ent *en, ie_ent *other)
{
    if (other == en) return 0;

    if (!other->is_dead && !en->is_dead)
    {
        if (other->stack_item != en->stack_item) return 0;
        /* hasTagCompound on one side only, or unequal compounds */
        if (other->stack_tag != en->stack_tag) return 0;
        if (ITEMS[other->stack_item].has_subtypes && other->stack_damage != en->stack_damage) return 0;

        if (other->stack_count < en->stack_count) return combine_items(other, en);
        if (other->stack_count + en->stack_count > ITEMS[other->stack_item].max_stack_size) return 0;

        other->stack_count += en->stack_count;
        if (other->stack_count_link.owner) *stack_link_count(other->stack_count_link) = other->stack_count;
        other->delay = other->delay > en->delay ? other->delay : en->delay;
        other->age = other->age < en->age ? other->age : en->age;
        other->stack_watch_dirty = 1;   /* setEntityItem(itemstack1) */
        en->is_dead = 1;
        return 1;
    }

    return 0;
}

/* EntityItem.searchForOtherItemsNearby: World.getEntitiesWithinAABB(EntityItem,
 * boundingBox.expand(0.5, 0, 0.5)), the chunk map walk in Chunk.getEntitiesOfTypeWithinAAAB's
 * order, combine against every item the box touches. A section's list is
 * this world's and the peer's items together, in the order they joined the
 * chunk (chunk_stamp). */
/* the hits by chunk stamp, ascending, in place (heapsort: qsort may
 * allocate; the stamps are distinct) */
static void sort_by_stamp(ie_ent **a, int n)
{
#define STAMP_(k) (a[k]->e.chunk_stamp)
    for (int start = n / 2; start-- > 0;)
        for (int i = start;;)
        {
            int c = 2 * i + 1, m = i;
            if (c < n && STAMP_(c) > STAMP_(m)) m = c;
            if (c + 1 < n && STAMP_(c + 1) > STAMP_(m)) m = c + 1;
            if (m == i) break;
            ie_ent *t = a[i]; a[i] = a[m]; a[m] = t;
            i = m;
        }
    for (int end = n; end-- > 1;)
    {
        ie_ent *t = a[0]; a[0] = a[end]; a[end] = t;
        for (int i = 0;;)
        {
            int c = 2 * i + 1, m = i;
            if (c < end && STAMP_(c) > STAMP_(m)) m = c;
            if (c + 1 < end && STAMP_(c + 1) > STAMP_(m)) m = c + 1;
            if (m == i) break;
            t = a[i]; a[i] = a[m]; a[m] = t;
            i = m;
        }
    }
#undef STAMP_
}

static void search_other_items(ie_world *iew, ie_ent *en)
{
    struct aabb box = aabb_expand(en->e.bounding_box, 0.5, 0.0, 0.5);
    int cx0 = mh_floor((box.min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box.max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box.min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box.max_z + 2.0) / 16.0);
    int y0 = clamp_int(mh_floor((box.min_y - 2.0) / 16.0), 0, IE_SECTIONS - 1);
    int y1 = clamp_int(mh_floor((box.max_y + 2.0) / 16.0), 0, IE_SECTIONS - 1);
    ie_world *peer = iew->peer != NULL && iew->peer->w == iew->w ? iew->peer : NULL;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(iew->w, cx, cz)) continue;

            ie_chunk *c = chunk_find(iew, cx, cz);
            ie_chunk *pc = peer != NULL ? chunk_find(peer, cx, cz) : NULL;

            if (!c && !pc) continue;

            for (int y = y0; y <= y1; ++y)
            {
                int nh = 0;
                /* the section's items in both pools, however many */
                ie_ent **hits ENV_LOCAL = envstack_take(
                    ((size_t)(c != NULL ? c->sec[y].n : 0) + (size_t)(pc != NULL ? pc->sec[y].n : 0) + 1) * sizeof *hits);

                for (int k = 0; k < 2; ++k)
                {
                    ie_chunk *ch = k == 0 ? c : pc;
                    int n = ch != NULL ? ch->sec[y].n : 0;

                    for (int i = 0; i < n; ++i)
                    {
                        ie_ent *other = ie_ent_at(sec_items(&ch->sec[y])[i]);

                        if (other->kind != IE_ITEM) continue;
                        if (!aabb_intersects(&other->e.bounding_box, &box)) continue;
                        hits[nh++] = other;
                    }
                }
                if (pc != NULL && nh > 1) sort_by_stamp(hits, nh);
                for (int i = 0; i < nh; ++i) combine_items(en, hits[i]);
            }
        }
    }
}

/* World.getEntitiesWithinAABB(EntityItem.class, box) over this pool and its
 * peer: the chunks x outer and z inner, each section bottom to top, a
 * section's items in the order they joined the chunk (chunk_stamp), as
 * search_other_items walks them. Up to max_out in out; returns how many. */
static int within_aabb_peer(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out, int items_only);

int ie_items_within_aabb(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out)
{
    return within_aabb_peer(iew, box, out, max_out, 1);
}

/* World.getEntitiesWithinAABBExcludingEntity's walk over this pool and its
 * peer, every kind: the chunks x outer and z inner, each section bottom to
 * top, a section's entities in the order they joined the chunk
 * (chunk_stamp). */
int ie_entities_within_aabb_peer(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out)
{
    return within_aabb_peer(iew, box, out, max_out, 0);
}

static int within_aabb_peer(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out, int items_only)
{
    int cx0 = mh_floor((box->min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box->max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box->min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box->max_z + 2.0) / 16.0);
    int y0 = clamp_int(mh_floor((box->min_y - 2.0) / 16.0), 0, IE_SECTIONS - 1);
    int y1 = clamp_int(mh_floor((box->max_y + 2.0) / 16.0), 0, IE_SECTIONS - 1);
    ie_world *peer = iew->peer != NULL && iew->peer->w == iew->w ? iew->peer : NULL;
    int count = 0;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(iew->w, cx, cz)) continue;

            ie_chunk *c = chunk_find(iew, cx, cz);
            ie_chunk *pc = peer != NULL ? chunk_find(peer, cx, cz) : NULL;

            if (!c && !pc) continue;

            for (int y = y0; y <= y1; ++y)
            {
                int first = count;

                for (int k = 0; k < 2; ++k)
                {
                    ie_chunk *ch = k == 0 ? c : pc;
                    int n = ch != NULL ? ch->sec[y].n : 0;

                    for (int i = 0; i < n && count < max_out; ++i)
                    {
                        ie_ent *other = ie_ent_at(sec_items(&ch->sec[y])[i]);

                        if (items_only && other->kind != IE_ITEM) continue;
                        if (!aabb_intersects(&other->e.bounding_box, box)) continue;
                        out[count++] = other;
                    }
                }
                if (pc != NULL && count - first > 1) sort_by_stamp(out + first, count - first);
            }
        }
    }
    return count;
}

/* BlockEndPortal.onEntityCollidedWithBlock: travelToDimension(1) at once,
 * inside the move's block walk. */
void ie_end_portal(void *self)
{
    ie_ent *en = self;
    if (en->is_dead || en->iew == NULL || en->iew->portal_travel == NULL || en->e.world->is_remote) return;
    en->iew->portal_travel(en->iew, en, 1);
}

/* ServerConfigurationManager.respawnPlayer replaces the EntityPlayerMP: an
 * orb whose closestPlayer is the old object keeps it, and pulls toward where
 * it died, until a rescan finds it over 8 blocks away. */
void ie_player_left(ie_world *iew, const struct entity *p)
{
    for (int i = 0; i < iew->n; i++)
    {
        ie_ent *en = ie_ent_at(iew->slot[i]);
        if (en->kind != IE_ORB || !en->xp_has_target || en->xp_target_left) continue;
        en->xp_target_left = 1;
        en->xp_tx = p->pos_x;
        en->xp_ty = p->pos_y;
        en->xp_tz = p->pos_z;
    }
}

void ie_set_in_portal(void *self)
{
    ie_ent *en = self;

    if (en->time_until_portal > 0)
    {
        en->time_until_portal = 300;
        return;
    }

    if (!en->in_portal)
        en->teleport_direction = direction_get_movement(en->prev_x - en->e.pos_x, en->prev_z - en->e.pos_z);
    en->in_portal = 1;
}

void ie_chunk_leave(ie_world *iew, ie_ent *en)
{
    if (en->added_to_chunk) chunk_remove_at(iew, en, en->chunk_y);
    en->added_to_chunk = 0;
}

/* Entity.onEntityUpdate, the server-world half: the portal counter (none of
 * these kinds rides; getMaxInPortalTime is 0, so an entity travels on the
 * first update after it touched a portal), the water override, the fire
 * decay, the lava, the void. Nothing sprints, and the dataWatcher flag the
 * last line writes reaches nothing the record carries. */
static ie_world *base_update(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;
    struct world *w = e->world;

    en->prev_x = e->pos_x;
    en->prev_y = e->pos_y;
    en->prev_z = e->pos_z;
    en->prev_yaw = en->rotation_yaw;
    en->prev_pitch = en->rotation_pitch;

    if (!w->is_remote)
    {
        if (en->in_portal)
        {
            if (en->portal_counter++ >= 0)
            {
                en->portal_counter = 0;
                en->time_until_portal = 300;
                if (iew->portal_travel != NULL)
                {
                    iew->portal_travel(iew, en, w->dim == -1 ? 0 : -1);
                    iew = en->iew;
                }
            }
            en->in_portal = 0;
        }
        else
        {
            if (en->portal_counter > 0) en->portal_counter -= 4;
            if (en->portal_counter < 0) en->portal_counter = 0;
        }
        if (en->time_until_portal > 0) --en->time_until_portal;
        /* a traveller's update goes on in the new world */
        w = e->world;
    }

    int accel = water_accelerate(w, e, en->kind <= IE_ORB);
    if (accel)
    {
        if (en->kind >= IE_ARROW)
        {
            if (!en->in_water && !en->first_update)
            {
                det_rng_float(&en->rand);
                det_rng_float(&en->rand);
                float limit = 1.0F + en->e.width * 20.0F;
                for (int k = 0; (float)k < limit; ++k)
                {
                    det_rng_float(&en->rand);
                    det_rng_float(&en->rand);
                    det_rng_float(&en->rand);
                }
                for (int k = 0; (float)k < limit; ++k)
                {
                    det_rng_float(&en->rand);
                    det_rng_float(&en->rand);
                }
            }
            en->in_water = 1;
            e->fall_distance = 0.0F;
            e->fire = 0;
        }
    }
    else if (en->kind >= IE_ARROW)
    {
        en->in_water = 0;
    }

    if (e->fire > 0)
    {
        if (e->fire % 20 == 0 && en->kind <= IE_ORB) ie_attack_from(en, IE_SRC_ON_FIRE, 1.0F);
        /* EntityLargeFireball's attackEntityFrom answers the onFire damage
         * with setBeenAttacked: every update while setFire(1) holds fire at
         * 20, every twentieth once lava set it to 300 (the tracker's S12) */
        else if (e->fire % 20 == 0 && en->kind == IE_LARGE_FIREBALL) en->velocity_changed = 1;
        --e->fire;
    }

    /* handleLavaMovement: the box shrunk by (-0.1, -0.4, -0.1) holds lava */
    if (material_in_bb(w, aabb_expand(e->bounding_box, -0.10000000149011612, -0.4000000059604645,
                                      -0.10000000149011612), 7 /* Material.lava */))
    {
        set_on_fire_from_lava(en);
        e->fall_distance *= 0.5F;
    }

    if (e->pos_y < -64.0) en->is_dead = 1;

    en->first_update = 0;
    return iew;
}

/* EntityItem.onUpdate after the base tick. */
static void item_update(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;

    if (en->delay > 0) --en->delay;

    en->prev_x = e->pos_x;
    en->prev_y = e->pos_y;
    en->prev_z = e->pos_z;
    e->motion_y -= 0.03999999910593033;
    en->e.no_clip = (uint8_t)push_out_of_blocks(en, e->pos_x,
                                                (e->bounding_box.min_y + e->bounding_box.max_y) / 2.0,
                                                e->pos_z);
    entity_move(e, e->motion_x, e->motion_y, e->motion_z);

    /* the crossed-a-block test casts, it does not floor */
    int crossed = (int)en->prev_x != (int)e->pos_x || (int)en->prev_y != (int)e->pos_y
               || (int)en->prev_z != (int)e->pos_z;

    if (crossed || en->ticks_existed % 25 == 0)
    {
        int bx = mh_floor(e->pos_x);
        int by = mh_floor(e->pos_y);
        int bz = mh_floor(e->pos_z);

        if (BLOCKS[world_get_block(e->world, bx, by, bz) & 4095].material == 7 /* Material.lava */)
        {
            e->motion_y = 0.20000000298023224;
            float a = det_rng_float(&en->rand);
            float b = det_rng_float(&en->rand);
            e->motion_x = (double)((a - b) * 0.2F);
            float c = det_rng_float(&en->rand);
            float d = det_rng_float(&en->rand);
            e->motion_z = (double)((c - d) * 0.2F);
            (void)det_rng_float(&en->rand); /* the fizz pitch */
        }

        search_other_items(iew, en);
    }

    float f2 = 0.98F;

    if (e->on_ground)
    {
        int bx = mh_floor(e->pos_x);
        int by = mh_floor(e->bounding_box.min_y) - 1;
        int bz = mh_floor(e->pos_z);
        f2 = BLOCKS[world_get_block(e->world, bx, by, bz) & 4095].slipperiness * 0.98F;
    }

    e->motion_x *= (double)f2;
    e->motion_y *= 0.9800000190734863;
    e->motion_z *= (double)f2;

    if (e->on_ground) e->motion_y *= -0.5;

    ++en->age;

    if (en->age >= 6000) en->is_dead = 1;
}

/* EntityTNTPrimed.onUpdate, which replaces Entity.onUpdate outright (no
 * base tick). */
static void tnt_update(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;

    en->prev_x = e->pos_x;
    en->prev_y = e->pos_y;
    en->prev_z = e->pos_z;
    e->motion_y -= 0.03999999910593033;
    entity_move(e, e->motion_x, e->motion_y, e->motion_z);
    e->motion_x *= 0.9800000190734863;
    e->motion_y *= 0.9800000190734863;
    e->motion_z *= 0.9800000190734863;

    if (e->on_ground)
    {
        e->motion_x *= 0.699999988079071;
        e->motion_z *= 0.699999988079071;
        e->motion_y *= -0.5;
    }

    if (en->fuse-- <= 0)
    {
        en->is_dead = 1;

        if (iew->on_tnt_explode != NULL) iew->on_tnt_explode(iew->tnt_ctx, en);
    }
    /* the smoke particle is the client's */
}

/* EntityXPOrb.onUpdate after the base tick. */
static void orb_update(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;

    if (en->delay > 0) --en->delay; /* field_70532_c */

    en->prev_x = e->pos_x;
    en->prev_y = e->pos_y;
    en->prev_z = e->pos_z;
    e->motion_y -= 0.029999999329447746;

    int bx = mh_floor(e->pos_x);
    int by = mh_floor(e->pos_y);
    int bz = mh_floor(e->pos_z);

    if (BLOCKS[world_get_block(e->world, bx, by, bz) & 4095].material == 7 /* Material.lava */)
    {
        e->motion_y = 0.20000000298023224;
        float a = det_rng_float(&en->rand);
        float b = det_rng_float(&en->rand);
        e->motion_x = (double)((a - b) * 0.2F);
        float c = det_rng_float(&en->rand);
        float d = det_rng_float(&en->rand);
        e->motion_z = (double)((c - d) * 0.2F);
        (void)det_rng_float(&en->rand); /* the fizz pitch */
    }

    (void)push_out_of_blocks(en, e->pos_x, (e->bounding_box.min_y + e->bounding_box.max_y) / 2.0,
                             e->pos_z);

    if (en->xp_target_color < en->xp_color - 20 + en->entity_id % 100)
    {
        int requery = !en->xp_has_target;
        if (!requery && en->xp_target_left)
            requery = (en->xp_tx - e->pos_x) * (en->xp_tx - e->pos_x) +
                      (en->xp_ty - e->pos_y) * (en->xp_ty - e->pos_y) +
                      (en->xp_tz - e->pos_z) * (en->xp_tz - e->pos_z) > 64.0;
        else if (!requery && iew->player != NULL)
            requery = (iew->player->pos_x - e->pos_x) * (iew->player->pos_x - e->pos_x) +
                      (iew->player->pos_y - e->pos_y) * (iew->player->pos_y - e->pos_y) +
                      (iew->player->pos_z - e->pos_z) * (iew->player->pos_z - e->pos_z) > 64.0;
        if (requery)
        {
            double px = iew->player ? iew->player->pos_x - e->pos_x : 0.0;
            double py = iew->player ? iew->player->pos_y - e->pos_y : 0.0;
            double pz = iew->player ? iew->player->pos_z - e->pos_z : 0.0;
            en->xp_has_target = iew->player && !iew->player_gone && px*px + py*py + pz*pz < 64.0;
            en->xp_target_left = 0;
        }
        en->xp_target_color = en->xp_color;
    }

    if (en->xp_has_target && (en->xp_target_left || iew->player != NULL))
    {
        double tx = en->xp_target_left ? en->xp_tx : iew->player->pos_x;
        double ty = en->xp_target_left ? en->xp_ty : iew->player->pos_y;
        double tz = en->xp_target_left ? en->xp_tz : iew->player->pos_z;
        double vx = (tx - e->pos_x) / 8.0;
        double vy = (ty + (double)1.62F - e->pos_y) / 8.0;
        double vz = (tz - e->pos_z) / 8.0;
        double distance = sqrt(vx*vx + vy*vy + vz*vz);
        double pull = 1.0 - distance;
        if (pull > 0.0)
        {
            pull *= pull;
            e->motion_x += vx / distance * pull * 0.1;
            e->motion_y += vy / distance * pull * 0.1;
            e->motion_z += vz / distance * pull * 0.1;
        }
    }

    entity_move(e, e->motion_x, e->motion_y, e->motion_z);

    float f2 = 0.98F;

    if (e->on_ground)
    {
        int fx = mh_floor(e->pos_x);
        int fy = mh_floor(e->bounding_box.min_y) - 1;
        int fz = mh_floor(e->pos_z);
        f2 = BLOCKS[world_get_block(e->world, fx, fy, fz) & 4095].slipperiness * 0.98F;
    }

    e->motion_x *= (double)f2;
    e->motion_y *= 0.9800000190734863;
    e->motion_z *= (double)f2;

    if (e->on_ground) e->motion_y *= -0.8999999761581421;

    ++en->xp_color;
    ++en->age;

    if (en->age >= 6000) en->is_dead = 1;
}

/* ------------------------------------------------- updateEntityWithOptionalForce */

/* World.updateEntityWithOptionalForce(e, true): the bookkeeping, the update
 * when the entity sits in a chunk, then the chunk membership. */
static void update_entity(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;
    struct world *w = e->world;

    en->last_tick_x = e->pos_x;
    en->last_tick_y = e->pos_y;
    en->last_tick_z = e->pos_z;
    en->prev_yaw = en->rotation_yaw;
    en->prev_pitch = en->rotation_pitch;

    if (en->added_to_chunk)
    {
        ++en->ticks_existed;

        if (en->kind == IE_LARGE_FIREBALL || en->kind == IE_SMALL_FIREBALL)
        {
            int bx = (int)e->pos_x;
            int by = (int)e->pos_y;
            int bz = (int)e->pos_z;
            int block_exists = (by >= 0 && by < 256 && world_chunk_loaded(w, bx >> 4, bz >> 4));
            if ((en->shooter && !en->shooter_is_player && lv_get(en->shooter)->is_dead) || !block_exists)
            {
                en->is_dead = 1;
                goto chunk_update;
            }
        }

        if (en->kind == IE_TNT)
        {
            tnt_update(iew, en);
            goto chunk_update;
        }

        /* a portal traveller's update goes on in the destination's pool */
        ie_world *uw = base_update(iew, en);

        if (en->kind == IE_ITEM) item_update(uw, en);
        else if (en->kind == IE_ORB) orb_update(uw, en);
        else projectile_update(uw, en);
        if (iew->post_update != NULL) iew->post_update(iew);
    }
chunk_update:

    if (en->e.pos_x != en->e.pos_x || en->e.pos_x == 1.0 / 0.0 || en->e.pos_x == -1.0 / 0.0) en->e.pos_x = en->last_tick_x;
    if (en->e.pos_y != en->e.pos_y || en->e.pos_y == 1.0 / 0.0 || en->e.pos_y == -1.0 / 0.0) en->e.pos_y = en->last_tick_y;
    if (en->e.pos_z != en->e.pos_z || en->e.pos_z == 1.0 / 0.0 || en->e.pos_z == -1.0 / 0.0) en->e.pos_z = en->last_tick_z;

    int var6 = mh_floor(e->pos_x / 16.0);
    int var7 = mh_floor(e->pos_y / 16.0);
    int var8 = mh_floor(e->pos_z / 16.0);

    if (!en->added_to_chunk || en->chunk_x != var6 || en->chunk_y != var7 || en->chunk_z != var8)
    {
        if (en->added_to_chunk && world_chunk_loaded(w, en->chunk_x, en->chunk_z))
        {
            chunk_remove_at(iew, en, en->chunk_y);
        }

        if (world_chunk_loaded(w, var6, var8)) chunk_add(iew, en, var6, var7, var8);
        else en->added_to_chunk = 0;
    }
}

/* Chunk.onChunkUnload over this pool: every entity the chunk's y-section lists
 * hold comes out, in the caller's array and out of the pool's list. Java hands
 * each section list to World.unloadEntities, and World.updateEntities removes
 * those entities from loadedEntityList before its walk, so they are never
 * ticked again. The caller frees them (it owns the pass's order too). */
int ie_unload_chunk(ie_world *iew, int cx, int cz, ie_ent **out, int cap)
{
    int n = 0;

    for (int i = 0; i < iew->n; ++i)
    {
        ie_ent *en = ie_ent_at(iew->slot[i]);

        if (!en->added_to_chunk || en->chunk_x != cx || en->chunk_z != cz) continue;

        chunk_remove_at(iew, en, en->chunk_y);
        en->added_to_chunk = 0;

        if (n < cap) out[n] = en;
        ++n;

        memmove(&iew->slot[i], &iew->slot[i + 1], (size_t)(iew->n - i - 1) * sizeof *iew->slot);
        --iew->n;
        --i;
    }

    return n;
}

/* World.updateEntities' entity pass for one entity: updateEntityWithOptionalForce,
 * then the dead entity out of this pool's list and its chunk. */
int ie_tick_one(ie_world *iew, ie_ent *en, int tick, ie_removal *out, int max_out, int *n_out)
{
    if (n_out) *n_out = 0;

    if (!en->is_dead)
    {
        update_entity(iew, en);
        if (!en->is_dead) return 0;
    }

    if (en->added_to_chunk && world_chunk_loaded(iew->w, en->chunk_x, en->chunk_z))
    {
        chunk_remove_at(iew, en, en->chunk_y);
    }

    int reason;

    if (en->kind <= IE_ORB && en->health <= 0) reason = IE_REMOVAL_BURNED;
    else if (en->age >= 6000) reason = IE_REMOVAL_DESPAWNED;
    else if (en->ticks_in_ground >= 1200) reason = IE_REMOVAL_GROUND_DESPAWN;
    else if (en->e.pos_y < -64.0) reason = IE_REMOVAL_VOID;
    else if (en->kind >= IE_ARROW) reason = IE_REMOVAL_IMPACT;
    else reason = IE_REMOVAL_MERGED;

    if (out && *n_out < max_out)
    {
        ie_removal *r = &out[(*n_out)++];
        r->tick = tick;
        r->spawn_index = en->spawn_index;
        r->entity_id = en->entity_id;
        r->reason = reason;
        r->health = en->health;
        r->fire = en->e.fire;
        r->age = en->age;
        r->pos_y = en->e.pos_y;
        r->added_to_chunk = en->added_to_chunk;
        r->chunk_x = en->chunk_x;
        r->chunk_y = en->chunk_y;
        r->chunk_z = en->chunk_z;
    }

    int at = -1;

    for (int i = 0; i < iew->n; ++i)
        if (ie_ent_at(iew->slot[i]) == en) { at = i; break; }

    if (at >= 0)
    {
        memmove(&iew->slot[at], &iew->slot[at + 1], (size_t)(iew->n - at - 1) * sizeof *iew->slot);
        --iew->n;
    }

    ie_ent_release(en);
    return 1;
}

/* World.updateEntities' entity pass for one tick. */
void ie_tick(ie_world *iew, int tick, ie_removal *out, int max_out, int *n_out)
{
    if (n_out) *n_out = 0;
    nw_env->blockcb.env.world_rand = &iew->world_rand.r;

    for (int i = 0; i < iew->n; ++i)
    {
        ie_ent *en = ie_ent_at(iew->slot[i]);

        if (en->is_dead) continue;

        update_entity(iew, en);

        if (en->is_dead)
        {
            if (en->added_to_chunk && world_chunk_loaded(iew->w, en->chunk_x, en->chunk_z))
            {
                chunk_remove_at(iew, en, en->chunk_y);
            }

            int reason;

            if (en->kind <= IE_ORB && en->health <= 0) reason = IE_REMOVAL_BURNED;
            else if (en->age >= 6000) reason = IE_REMOVAL_DESPAWNED;
            else if (en->ticks_in_ground >= 1200) reason = IE_REMOVAL_GROUND_DESPAWN;
            else if (en->e.pos_y < -64.0) reason = IE_REMOVAL_VOID;
            else if (en->kind >= IE_ARROW) reason = IE_REMOVAL_IMPACT;
            else reason = IE_REMOVAL_MERGED;

            if (out && *n_out < max_out)
            {
                ie_removal *r = &out[(*n_out)++];
                r->tick = tick;
                r->spawn_index = en->spawn_index;
                r->entity_id = en->entity_id;
                r->reason = reason;
                r->health = en->health;
                r->fire = en->e.fire;
                r->age = en->age;
                r->pos_y = en->e.pos_y;
            }

            ie_ent_release(en);
            memmove(&iew->slot[i], &iew->slot[i + 1], (size_t)(iew->n - i - 1) * sizeof *iew->slot);
            --iew->n;
            --i;
        }
    }
    nw_env->blockcb.env.world_rand = NULL;
}

/* World.isMaterialInBB and BlockLiquid.func_149801_b, for the living entity's
 * water paths (Entity.handleWaterMovement, isInsideOfMaterial, isAnyLiquid). */
int ie_material_in_bb(struct world *w, struct aabb box, int material)
{
    return material_in_bb(w, box, material);
}

float ie_liquid_height_meta(int meta)
{
    return liquid_height(meta);
}

int ie_water_accelerate(struct world *w, struct entity *e, int is_item_or_orb)
{
    return water_accelerate(w, e, is_item_or_orb);
}

int ie_water_accelerate_memo(struct world *w, struct entity *e, struct cell_memo *m)
{
    int held = m != NULL && cell_memo_hold(m, w, &e->bounding_box);

    if (held && (m->known & CM_DRY))
    {
#ifdef NETHERITE_MOVE_MEMO_CHECK
        double mo[3] = {e->motion_x, e->motion_y, e->motion_z};
        if (water_accelerate(w, e, 0) || mo[0] != e->motion_x || mo[1] != e->motion_y || mo[2] != e->motion_z)
        {
            fprintf(stderr, "cell memo: the water test differs\n");
            abort();
        }
#endif
        return 0;
    }

    /* no water cell answered: no flow reached the motion either */
    int r = water_accelerate(w, e, 0);
    if (held && !r) m->known |= CM_DRY;
    return r;
}

int ie_lava_memo(struct world *w, const struct aabb *bb, struct cell_memo *m)
{
    int held = m != NULL && cell_memo_hold(m, w, bb);

    if (held && (m->known & CM_NO_LAVA))
    {
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (material_in_bb(w, aabb_expand(*bb, -0.10000000149011612, -0.4000000059604645, -0.10000000149011612), 7))
        {
            fprintf(stderr, "cell memo: the lava test differs\n");
            abort();
        }
#endif
        return 0;
    }

    int r = material_in_bb(w, aabb_expand(*bb, -0.10000000149011612, -0.4000000059604645, -0.10000000149011612), 7);
    if (held && !r) m->known |= CM_NO_LAVA;
    return r;
}

/* The same acceleration over a caller's box: EntitySquid.isInWater's
 * handleMaterialAcceleration over the box expanded 0.6 down (no contract).
 * Entity.isPushedByWater is true for the squid too. */
int ie_water_accelerate_box(struct world *w, struct aabb box, struct entity *e)
{
    int x0 = mh_floor(box.min_x);
    int x1 = mh_floor(box.max_x + 1.0);
    int y0 = mh_floor(box.min_y);
    int y1 = mh_floor(box.max_y + 1.0);
    int z0 = mh_floor(box.min_z);
    int z1 = mh_floor(box.max_z + 1.0);

    if (!world_check_chunks_exist(w, x0, y0, z0, x1, y1, z1)) return 0;

    int found = 0;
    double vx = 0.0, vy = 0.0, vz = 0.0;

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;

                if (BLOCKS[id].material == 6)
                {
                    double var16 = (double)((float)(y + 1) - liquid_height(world_get_meta(w, x, y, z)));

                    if ((double)y1 >= var16)
                    {
                        found = 1;
                        double fx, fy, fz;
                        flow_vector(w, x, y, z, &fx, &fy, &fz);
                        vx += fx;
                        vy += fy;
                        vz += fz;
                    }
                }
            }

    if (vx * vx + vy * vy + vz * vz > 0.0)
    {
        vec_normalize(&vx, &vy, &vz);
        e->motion_x += vx * 0.014;
        e->motion_y += vy * 0.014;
        e->motion_z += vz * 0.014;
    }

    return found;
}

int ie_can_be_collided_with(const ie_ent *en)
{
    return en->kind == IE_LARGE_FIREBALL || (en->kind == IE_TNT && !en->is_dead);
}

int ie_get_entities_within_aabb(ie_world *iew, const struct aabb *box, const ie_ent *exclude, ie_ent **out, int max_out)
{
    int count = 0;
    int cx0 = mh_floor((box->min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box->max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box->min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box->max_z + 2.0) / 16.0);
    int y0 = clamp_int(mh_floor((box->min_y - 2.0) / 16.0), 0, IE_SECTIONS - 1);
    int y1 = clamp_int(mh_floor((box->max_y + 2.0) / 16.0), 0, IE_SECTIONS - 1);

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(iew->w, cx, cz)) continue;

            ie_chunk *c = chunk_find(iew, cx, cz);
            if (!c) continue;

            for (int y = y0; y <= y1; ++y)
            {
                int n = c->sec[y].n;
                for (int i = 0; i < n; ++i)
                {
                    ie_ent *other = ie_ent_at(sec_items(&c->sec[y])[i]);
                    if (other == exclude) continue;
                    if (!aabb_intersects(&other->e.bounding_box, box)) continue;

                    if (out && count < max_out) out[count] = other;
                    ++count;
                }
            }
        }
    }
    return count;
}
