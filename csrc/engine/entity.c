/* Entity.moveEntity, the collision physics every mob and the player run
 * through. The Java body is one long straight-line routine: the in-web
 * damping, one sweep along Y, X then Z against the boxes World returns for
 * the swept box, an optional step-up retry, the derived position and collision
 * flags, the walking distance counters, the per-cell block callback pass
 * (func_145775_I) and the fire/water state at the end.
 *
 * Every field the probe records is carried here. The parts of the Java routine
 * that only make noise or particles (playSound, spawnParticle) are dropped;
 * they draw from the entity's own Random, which the probe never records and
 * which never feeds a recorded field.
 *
 * Java evaluates operands left to right; the collisions here draw from no RNG,
 * but the block callback pass does read the entity state the sweep just wrote,
 * so the statements stay in vanilla's order. */
#include "entity.h"
#include "jmath.h"
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include "trace.h"
#include "blocks.h"
#include "collide.h"
#include "world.h"
#include "biomes.h"
#include "features_lakes.h" /* biome_at */

#include <math.h>
#include <string.h>

/* MathHelper.sqrt_double: the square root, rounded to float and widened back.
 * The rounding matters: the walking-distance accumulation multiplies this as a
 * double, so a full-precision double here drifts a ULP from Java. */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

void entity_init(struct entity *e, struct world *w)
{
    memset(e, 0, sizeof *e);
    e->world = w;
    e->width = 0.6F;
    e->height = 1.8F;
    e->y_offset = 0.0F;
    e->step_height = 0.0F;
    e->fire_resistance = 1;
    e->first_update = 1;
    e->next_step_distance = 1; /* Entity's constructor */
    e->field_70135_K = 1; /* Entity's constructor */
    e->can_trigger_walking = 1;
    e->self = NULL;
    e->attack_from = NULL;
    e->fizz = NULL;
    e->bounding_box = aabb_make(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

void entity_set_size(struct entity *e, float width, float height)
{
    e->width = width;
    e->height = height;
}

void entity_set_position(struct entity *e, double x, double y, double z)
{
    e->pos_x = x;
    e->pos_y = y;
    e->pos_z = z;
    float var7 = e->width / 2.0F;
    float var8 = e->height;
    e->bounding_box = aabb_make(x - (double)var7, y - (double)e->y_offset + (double)e->y_size, z - (double)var7,
                                x + (double)var7, y - (double)e->y_offset + (double)e->y_size + (double)var8,
                                z + (double)var7);
}

void entity_set_in_web(struct entity *e)
{
    /* Entity.setInWeb is virtual: EntitySpider's override is empty, so a
     * spider's flag and fall distance stay untouched. */
    if (e->set_in_web)
    {
        e->set_in_web(e->self);
        return;
    }

    e->is_in_web = 1;
    e->fall_distance = 0.0F;
}

/* World.isRaining for the world whose entities move now: the replay sets it
 * per world tick (entity_set_raining); the probes never rain. */
#define entity_raining (nw_env->entity.raining)

void entity_set_raining(int raining)
{
    entity_raining = raining;
}

/* World.canLightningStrikeAt: rain, the sky, the precipitation height, a
 * biome without snow where it cannot snow and that lets lightning strike. */
static int can_lightning_strike_at(struct world *w, int x, int y, int z)
{
    if (!entity_raining || w == NULL) return 0;
    if (!world_can_block_see_the_sky(w, x, y, z)) return 0;
    if (world_get_precipitation_height(w, x, z) > y) return 0;

    int biome = biome_at(w, x, z);

    if (BIOMES[biome].snow) return 0;
    if (world_can_snow_at(w, x, y, z, 0)) return 0;

    return BIOMES[biome].lightning;
}

/* Entity.isWet: inWater, or an open column at the feet or the head. */
static int entity_is_wet(struct entity *e)
{
    struct world *w = e->world;

    if (e->in_water) return 1;
    /* can_lightning_strike_at's first test, before the floors it takes */
    if (!entity_raining || w == NULL) return 0;
    if (can_lightning_strike_at(w, mh_floor(e->pos_x), mh_floor(e->pos_y), mh_floor(e->pos_z))) return 1;
    if (can_lightning_strike_at(w, mh_floor(e->pos_x), mh_floor(e->pos_y + (double)e->height),
                                mh_floor(e->pos_z)))
        return 1;
    return 0;
}

int entity_copy_is_wet(struct world *w, int in_water, double x, double y, double z, float height)
{
    if (in_water) return 1;
    if (can_lightning_strike_at(w, mh_floor(x), mh_floor(y), mh_floor(z))) return 1;
    return can_lightning_strike_at(w, mh_floor(x), mh_floor(y + (double)height), mh_floor(z));
}

void entity_move(struct entity *e, double dx, double dy, double dz)
{
    entity_move_ex(e, dx, dy, dz, 0, 0);
}

void entity_move_ex(struct entity *e, double dx, double dy, double dz, int sneak_player,
                    entity_fall_state_fn *fall_state)
{
    entity_move_memo(e, dx, dy, dz, sneak_player, fall_state, NULL);
}

/* a == b bit for bit over n bytes (n a multiple of 4), word by word: the
 * constant sizes unroll inline where memcmp was a call */
static inline int bits_same(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    uint64_t d = 0;

    for (size_t i = 0; i + 8 <= n; i += 8)
    {
        uint64_t x, y;
        memcpy(&x, p + i, 8);
        memcpy(&y, q + i, 8);
        d |= x ^ y;
    }
    if (n & 4)
    {
        uint32_t x, y;
        memcpy(&x, p + (n & ~(size_t)7), 4);
        memcpy(&y, q + (n & ~(size_t)7), 4);
        d |= x ^ y;
    }
    return d == 0;
}

/* The chunks under the columns within a block of bb, their wseq summed into
 * *out: 0 when one of them is not loaded or the columns span more than two
 * chunks a side. A chunk's wseq only grows, so for the same chunks the same
 * sum is the same cells. */
static int cells_stamp(struct world *w, const struct aabb *bb, uint64_t *out)
{
    int cx0 = (mh_floor(bb->min_x) - 1) >> 4, cx1 = (mh_floor(bb->max_x) + 1) >> 4;
    int cz0 = (mh_floor(bb->min_z) - 1) >> 4, cz1 = (mh_floor(bb->max_z) + 1) >> 4;
    uint64_t sum = 0;

    if (cx1 - cx0 > 1 || cz1 - cz0 > 1) return 0;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            struct chunk *c = world_chunk_near(w, cx, cz);

            if (c == NULL && (c = world_chunk(w, cx, cz)) == NULL) return 0;
            sum += c->wseq;
        }
    }

    *out = sum;
    return 1;
}

int cell_memo_hold(struct cell_memo *m, struct world *w, const struct aabb *bb)
{
    uint64_t stamp;
    int have = 0;

    if (m->w == w && m->map_ver == w->map_ver && bits_same(&m->bb, bb, sizeof *bb))
    {
        /* no write anywhere in the world since it last held */
        if (m->ws == w->wseq) return 1;
        have = cells_stamp(w, bb, &stamp);
        if (have && stamp == m->stamp)
        {
            m->ws = w->wseq;
            return 1;
        }
        if (!have)
        {
            m->w = NULL;
            return 0;
        }
    }

    if (!have && !cells_stamp(w, bb, &stamp))
    {
        m->w = NULL;
        return 0;
    }
    m->w = w;
    m->map_ver = w->map_ver;
    m->bb = *bb;
    m->stamp = stamp;
    m->ws = w->wseq;
    m->known = 0;
    m->move = 0;
    return 1;
}

int cell_memo_hold_at(struct cell_memo *m, struct world *w, const struct aabb *bb, double x, double y, double z,
                      float width)
{
    double pos[3] = {x, y, z};

    if (!(width <= 2.5F && pos[0] >= bb->min_x && pos[0] <= bb->max_x && pos[2] >= bb->min_z && pos[2] <= bb->max_z))
        return 0;
    if (!cell_memo_hold(m, w, bb)) return 0;
    if (!bits_same(m->pos, pos, sizeof m->pos))
    {
        m->known &= (uint16_t)~CM_AT;
        memcpy(m->pos, pos, sizeof m->pos);
    }
    return 1;
}

/* The still move holds for this move: the same move and the step's inputs
 * (the box and the cells are the key's). */
static int move_same(const struct cell_memo *m, const struct entity *e, double dx, double dy, double dz)
{
    double d[3] = {dx, dy, dz};

    return m->move && m->on_ground == e->on_ground && bits_same(&m->y_size, &e->y_size, sizeof m->y_size) &&
           bits_same(&m->step_height, &e->step_height, sizeof m->step_height) && bits_same(m->d, d, sizeof d);
}

#ifdef NETHERITE_MOVE_MEMO_CHECK
/* The memo's check build (make move-memo-check): every move the memo would
 * have skipped runs in full and must agree with it. */
static void memo_fail(const char *what)
{
    fprintf(stderr, "move memo: %s differs from the full move\n", what);
    abort();
}
#endif

void entity_move_memo(struct entity *e, double dx, double dy, double dz, int sneak_player,
                      entity_fall_state_fn *fall_state, struct cell_memo *memo)
{
    struct world *w = e->world;

    if (e->no_clip)
    {
        e->bounding_box = aabb_offset(e->bounding_box, dx, dy, dz);
        e->pos_x = (e->bounding_box.min_x + e->bounding_box.max_x) / 2.0;
        e->pos_y = e->bounding_box.min_y + (double)e->y_offset - (double)e->y_size;
        e->pos_z = (e->bounding_box.min_z + e->bounding_box.max_z) / 2.0;
        return;
    }

    struct collide_list *boxes COLLIDE_SCRATCH = collide_scratch_begin();
    e->y_size *= 0.4F;

    double var7 = e->pos_x;
    double var9 = e->pos_y;
    double var11 = e->pos_z;

    if (e->is_in_web)
    {
        e->is_in_web = 0;
        dx *= 0.25;
        dy *= 0.05000000074505806;
        dz *= 0.25;
        e->motion_x = 0.0;
        e->motion_y = 0.0;
        e->motion_z = 0.0;
    }

    double var13 = dx;
    double var15 = dy;
    double var17 = dz;
    struct aabb var19 = aabb_copy(e->bounding_box);
    /* var20 is "on the ground, sneaking, and an EntityPlayer" */
    int var20 = sneak_player && e->on_ground;

    /* The var20 sneak clamp: walk off the ledge at 0.05 steps before the
     * collision pass, exactly vanilla's three loops. */
    if (var20)
    {
        double var21 = 0.05;

        while (dx != 0.0 && world_colliding_boxes_empty(w, aabb_offset_box(e->bounding_box, dx, -1.0, 0.0)))
        {
            if (dx < var21 && dx >= -var21) dx = 0.0;
            else if (dx > 0.0) dx -= var21;
            else dx += var21;
            var13 = dx;
        }

        while (dz != 0.0 && world_colliding_boxes_empty(w, aabb_offset_box(e->bounding_box, 0.0, -1.0, dz)))
        {
            if (dz < var21 && dz >= -var21) dz = 0.0;
            else if (dz > 0.0) dz -= var21;
            else dz += var21;
            var17 = dz;
        }

        while (dx != 0.0 && dz != 0.0 && world_colliding_boxes_empty(w, aabb_offset_box(e->bounding_box, dx, -1.0, dz)))
        {
            if (dx < var21 && dx >= -var21) dx = 0.0;
            else if (dx > 0.0) dx -= var21;
            else dx += var21;

            if (dz < var21 && dz >= -var21) dz = 0.0;
            else if (dz > 0.0) dz -= var21;
            else dz += var21;

            var13 = dx;
            var17 = dz;
        }
    }

    struct aabb var19q = aabb_add_coord(e->bounding_box, dx, dy, dz);

    /* the still move (struct cell_memo): not over the sneak clamp, the entity
     * list's boxes, the flag that stops a blocked move, or a move of more
     * than a block sideways (the scans would leave the key's columns) */
    int held = memo != NULL && !var20 && e->field_70135_K && e->extra_boxes == NULL && fabs(dx) <= 1.0 &&
               fabs(dz) <= 1.0 && cell_memo_hold(memo, w, &e->bounding_box);
    int matched = held && move_same(memo, e, dx, dy, dz), impure = 0, stored = 0;
#ifdef NETHERITE_MOVE_MEMO_CHECK
    int hit = 0;
#else
    int hit = matched;
#endif

    if (hit)
    {
        dx = memo->o[0];
        dy = memo->o[1];
        dz = memo->o[2];
    }
    else
    {
        collide_list_clear(boxes);
        world_get_colliding_bounding_boxes(w, var19q, boxes);
        if (e->extra_boxes) e->extra_boxes(e->self, var19q, boxes);
        impure |= boxes->impure;

        for (int i = 0; i < boxes->n; ++i) dy = aabb_calculate_y_offset(&boxes->box[i], &e->bounding_box, dy);

        e->bounding_box = aabb_offset(e->bounding_box, 0.0, dy, 0.0);

        if (!e->field_70135_K && var15 != dy)
        {
            dz = 0.0;
            dy = 0.0;
            dx = 0.0;
        }

        int var37 = e->on_ground || (var15 != dy && var15 < 0.0);

        for (int i = 0; i < boxes->n; ++i)
        {
            dx = aabb_calculate_x_offset(&boxes->box[i], &e->bounding_box, dx);
        }

        e->bounding_box = aabb_offset(e->bounding_box, dx, 0.0, 0.0);

        if (!e->field_70135_K && var13 != dx)
        {
            dz = 0.0;
            dy = 0.0;
            dx = 0.0;
        }

        for (int i = 0; i < boxes->n; ++i) dz = aabb_calculate_z_offset(&boxes->box[i], &e->bounding_box, dz);

        e->bounding_box = aabb_offset(e->bounding_box, 0.0, 0.0, dz);

        if (!e->field_70135_K && var17 != dz)
        {
            dz = 0.0;
            dy = 0.0;
            dx = 0.0;
        }

        if (e->step_height > 0.0F && var37 && (var20 || e->y_size < 0.05F) && (var13 != dx || var17 != dz))
        {
            double var38 = dx;
            double var25 = dy;
            double var27 = dz;
            dx = var13;
            dy = (double)e->step_height;
            dz = var17;
            struct aabb var29 = aabb_copy(e->bounding_box);
            e->bounding_box = aabb_copy(var19);
            collide_list_clear(boxes);
            struct aabb var29q = aabb_add_coord(e->bounding_box, var13, dy, var17);
            world_get_colliding_bounding_boxes(w, var29q, boxes);
            if (e->extra_boxes) e->extra_boxes(e->self, var29q, boxes);
            impure |= boxes->impure;

            for (int i = 0; i < boxes->n; ++i) dy = aabb_calculate_y_offset(&boxes->box[i], &e->bounding_box, dy);

            e->bounding_box = aabb_offset(e->bounding_box, 0.0, dy, 0.0);

            if (!e->field_70135_K && var15 != dy)
            {
                dz = 0.0;
                dy = 0.0;
                dx = 0.0;
            }

            for (int i = 0; i < boxes->n; ++i) dx = aabb_calculate_x_offset(&boxes->box[i], &e->bounding_box, dx);

            e->bounding_box = aabb_offset(e->bounding_box, dx, 0.0, 0.0);

            if (!e->field_70135_K && var13 != dx)
            {
                dz = 0.0;
                dy = 0.0;
                dx = 0.0;
            }

            for (int i = 0; i < boxes->n; ++i) dz = aabb_calculate_z_offset(&boxes->box[i], &e->bounding_box, dz);

            e->bounding_box = aabb_offset(e->bounding_box, 0.0, 0.0, dz);

            if (!e->field_70135_K && var17 != dz)
            {
                dz = 0.0;
                dy = 0.0;
                dx = 0.0;
            }

            if (!e->field_70135_K && var15 != dy)
            {
                dz = 0.0;
                dy = 0.0;
                dx = 0.0;
            }
            else
            {
                dy = (double)(-e->step_height);

                for (int i = 0; i < boxes->n; ++i) dy = aabb_calculate_y_offset(&boxes->box[i], &e->bounding_box, dy);

                e->bounding_box = aabb_offset(e->bounding_box, 0.0, dy, 0.0);
            }

            if (var38 * var38 + var27 * var27 >= dx * dx + dz * dz)
            {
                dx = var38;
                dy = var25;
                dz = var27;
                e->bounding_box = aabb_copy(var29);
            }
        }
    }

#ifdef NETHERITE_MOVE_MEMO_CHECK
    if (matched)
    {
        double o[3] = {dx, dy, dz};
        if (!bits_same(o, memo->o, sizeof o)) memo_fail("the move");
        if (!bits_same(&e->bounding_box, &memo->bb, sizeof memo->bb)) memo_fail("the box");
    }
#endif
    if (held && !matched)
    {
        memo->move = 0;
        if (!impure && bits_same(&e->bounding_box, &var19, sizeof var19))
        {
            memo->d[0] = var13;
            memo->d[1] = var15;
            memo->d[2] = var17;
            memo->o[0] = dx;
            memo->o[1] = dy;
            memo->o[2] = dz;
            memo->y_size = e->y_size;
            memo->step_height = e->step_height;
            memo->on_ground = e->on_ground;
            memo->calm = 0;
            memo->fire = 2;
            memo->move = 1;
            stored = 1;
        }
    }

    e->pos_x = (e->bounding_box.min_x + e->bounding_box.max_x) / 2.0;
    e->pos_y = e->bounding_box.min_y + (double)e->y_offset - (double)e->y_size;
    e->pos_z = (e->bounding_box.min_z + e->bounding_box.max_z) / 2.0;
    e->is_collided_horizontally = var13 != dx || var17 != dz;
    e->is_collided_vertically = var15 != dy;
    e->on_ground = var15 != dy && var15 < 0.0;
    e->is_collided = e->is_collided_horizontally || e->is_collided_vertically;

    /* this.updateFallState(dy, onGround): virtual. The probe entity and the
     * fallback keep Entity's body; the players pass their own. Entity.fall
     * runs before the pending fall clears. */
    if (fall_state) fall_state(e, dy, e->on_ground);
    else if (e->on_ground)
    {
        if (e->fall_distance > 0.0F)
        {
            if (e->fall) e->fall(e->self, e->fall_distance);
            e->fall_distance = 0.0F;
        }
    }
    else if (dy < 0.0)
    {
        e->fall_distance = (float)((double)e->fall_distance - dy);
    }

    if (var13 != dx) e->motion_x = 0.0;
    if (var15 != dy) e->motion_y = 0.0;
    if (var17 != dz) e->motion_z = 0.0;

    double var39 = e->pos_x - var7;
    double var25b = e->pos_y - var9;
    double var27b = e->pos_z - var11;

    /* canTriggerWalking() && !sneaking && ridingEntity == null: the probe
     * entity and the item and orb kinds split on the flag, the player adds
     * !var20 (a sneaking step does not count), a riding player's own living
     * update walks nothing */
    if (e->can_trigger_walking && !var20 && !e->riding)
    {
        int x0 = mh_floor(e->pos_x);
        int y0 = mh_floor(e->pos_y - 0.20000000298023224 - (double)e->y_offset);
        int z0 = mh_floor(e->pos_z);
        int base_id = -1;

        /* the two cells answer only the ladder test of a vertical step and
         * the step's block: an entity that did not move vertically, over a
         * loaded chunk (so the reads load nothing), reads them only when
         * the step fires */
        if (var25b != 0.0 || world_chunk_near(w, x0 >> 4, z0 >> 4) == NULL)
        {
            base_id = world_get_block(w, x0, y0, z0) & 4095;
            int below_id = world_get_block(w, x0, y0 - 1, z0) & 4095;
            int render = BLOCKS[below_id].render_type;
            int moving = 0;

            if (render == 11 || render == 32 || render == 21) base_id = below_id;
            if (base_id != 65) moving = 0; /* Blocks.ladder */
            else moving = 1;

            if (!moving) var25b = 0.0;
        }

        e->distance_walked_modified =
            (float)((double)e->distance_walked_modified + sqrt_double(var39 * var39 + var27b * var27b) * 0.6);
        e->distance_walked_on_step_modified =
            (float)((double)e->distance_walked_on_step_modified +
                    sqrt_double(var39 * var39 + var25b * var25b + var27b * var27b) * 0.6);

        if (e->distance_walked_on_step_modified > (float)e->next_step_distance && base_id < 0)
        {
            base_id = world_get_block(w, x0, y0, z0) & 4095;
            int below_id = world_get_block(w, x0, y0 - 1, z0) & 4095;
            int render = BLOCKS[below_id].render_type;

            if (render == 11 || render == 32 || render == 21) base_id = below_id;
        }

        if (e->distance_walked_on_step_modified > (float)e->next_step_distance &&
            MATERIALS[BLOCKS[base_id].material].name != NULL && BLOCKS[base_id].material != 0)
        {
            e->next_step_distance = (int)e->distance_walked_on_step_modified + 1;

            if (e->kind_in_water ? e->kind_in_water(e->self) : e->in_water)
            {
                /* the in-water swim sound: playSound(getSwimSound(), var34,
                 * 1.0F + (rand.nextFloat() - rand.nextFloat()) * 0.4F) draws two
                 * floats from the entity's Random. Every block the probe's
                 * table places has the vanilla no-op onEntityWalking body. */
                double var34 = sqrt_double(e->motion_x * e->motion_x * 0.20000000298023224
                                           + e->motion_y * e->motion_y
                                           + e->motion_z * e->motion_z * 0.20000000298023224) * 0.35;

                (void)var34;

                if (e->swim_sound) e->swim_sound(e->self);
            }

            /* var32.onEntityWalking: the redstone ore's light-up is the
             * one override; the base block's body is empty */
            if (e->walking_block) e->walking_block(e->self, x0, y0, z0, base_id);
        }
    }

    /* Entity.func_145775_I: the per-cell block collision callback, which
     * a still move over calm cells skips (nothing written or loaded since
     * the memo held, the box the same) */
    int skip_cells = matched && memo->calm && w == memo->w && w->wseq == memo->ws && w->map_ver == memo->map_ver &&
                     bits_same(&e->bounding_box, &memo->bb, sizeof memo->bb);
    int calm = 1;
#ifdef NETHERITE_MOVE_MEMO_CHECK
    int verify_cells = skip_cells;
    skip_cells = 0;
#endif

    if (!skip_cells)
    {
        int var1 = mh_floor(e->bounding_box.min_x + 0.001);
        int var2 = mh_floor(e->bounding_box.min_y + 0.001);
        int var3 = mh_floor(e->bounding_box.min_z + 0.001);
        int var4 = mh_floor(e->bounding_box.max_x - 0.001);
        int var5 = mh_floor(e->bounding_box.max_y - 0.001);
        int var6 = mh_floor(e->bounding_box.max_z - 0.001);

        if (world_check_chunks_exist(w, var1, var2, var3, var4, var5, var6))
        {
            for (int x = var1; x <= var4; ++x)
            {
                for (int y = var2; y <= var5; ++y)
                {
                    for (int z = var3; z <= var6; ++z)
                    {
                        int id = world_get_block(w, x, y, z) & 4095;
                        int meta = world_get_meta(w, x, y, z);
                        int effect = collide_entity_effect(w, x, y, z, id, meta);

                        if (effect || id == 90 || id == 119) calm = 0;

                        if (effect & COLLIDE_EFFECT_WEB) entity_set_in_web(e);
                        if (id == 90 && e->set_in_portal) e->set_in_portal(e->self);
                        if ((effect & COLLIDE_EFFECT_PLATE) && w->on_plate)
                            w->on_plate(w->on_plate_ctx, x, y, z, id);
                        /* travelToDimension runs inside the walk, which
                         * reads the rest of its cells from the new world */
                        if (id == 119 && e->end_portal)
                        {
                            e->end_portal(e->self);
                            w = e->world;
                        }
                        if (effect & COLLIDE_EFFECT_SOUL_SAND)
                        {
                            e->motion_x *= 0.4;
                            e->motion_z *= 0.4;
                        }
                        if (effect & COLLIDE_EFFECT_CACTUS && e->attack_from)
                            e->attack_from(e->self, ENTITY_SRC_CACTUS, 1.0F);
                        if ((effect & COLLIDE_EFFECT_CAULDRON) && !w->is_remote)
                        {
                            /* BlockCauldron.onEntityCollidedWithBlock: a
                             * burning entity whose box bottom is at or
                             * below the water line is put out, and
                             * func_150024_a takes one level (flag 2; no
                             * comparator is ever powered) */
                            int level = meta;
                            float var7 = (float)y + (6.0F + (float)(3 * level)) / 16.0F;

                            if (e->fire > 0 && level > 0 && e->bounding_box.min_y <= (double)var7)
                            {
                                e->fire = 0;
                                world_set_meta(w, x, y, z, level - 1 > 3 ? 3 : level - 1, 2);
                            }
                        }
                    }
                }
            }
        }
    }

#ifdef NETHERITE_MOVE_MEMO_CHECK
    if (verify_cells && !calm) memo_fail("the cell pass");
#endif
    int keep = stored && calm && w == memo->w && w->wseq == memo->ws && w->map_ver == memo->map_ver;
    if (keep) memo->calm = 1;

    int var40 = entity_is_wet(e);

    /* World.func_147470_e over the box shrunk by 0.001: the memo's answer
     * when the cell pass was skipped */
    int in_fire;

    if (skip_cells && memo->fire != 2) in_fire = memo->fire;
    else
    {
        in_fire = world_is_in_fire(w, aabb_expand(e->bounding_box, -0.001, -0.001, -0.001));
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (verify_cells && memo->fire != 2 && in_fire != memo->fire) memo_fail("the fire test");
#endif
        if (keep || (skip_cells && memo->fire == 2)) memo->fire = (uint8_t)in_fire;
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (verify_cells && memo->fire == 2) memo->fire = (uint8_t)in_fire;
#endif
    }

    if (in_fire)
    {
        /* Entity.dealFireDamage(1): attackEntityFrom(inFire, 1.0F), which the
         * plain Entity leaves unrecorded and the item and orb kinds carry. */
        if (e->attack_from) e->attack_from(e->self, ENTITY_SRC_IN_FIRE, 1.0F);

        if (!var40)
        {
            ++e->fire;

            /* setFire(8): 8 * 20 through getFireTimeForEntity, and it
             * only raises the value */
            if (e->fire == 0)
            {
                int v = e->fire_time != NULL ? e->fire_time(e->self, 160) : 160;
                if (e->fire < v) e->fire = v;
            }
        }
    }
    else if (e->fire <= 0)
    {
        e->fire = -e->fire_resistance;
    }

    if (var40 && e->fire > 0)
    {
        /* the fizz sound's two rand draws, when the entity carries them */
        if (e->fizz) e->fizz(e->self);
        e->fire = -e->fire_resistance;
    }

}

