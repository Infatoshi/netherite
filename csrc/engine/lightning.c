/* EntityLightningBolt.onUpdate. The constructor's fire placement is in
 * servertick.c, at WorldServer.updateBlocks' thunder strike. */
#include "lightning.h"
#include "blocks.h"
#include "fire.h"
#include "item_entity.h"
#include "player.h"
#include "world.h"

#include <math.h>
#include <string.h>

void lightning_adopt(struct lightning_bolt *bolt, double x, double y, double z,
                     uint64_t rand_state, int64_t vertex, int living_time)
{
    memset(bolt, 0, sizeof *bolt);
    bolt->x = x;
    bolt->y = y;
    bolt->z = z;
    bolt->rand.r.seed = rand_state;
    bolt->state = 2;
    bolt->living_time = living_time;
    bolt->vertex = vertex;
    bolt->first_update = 1;
}

/* Entity.onEntityUpdate's world reads for a bolt (lane/divfix): the box is
 * the Entity constructor's 0.6 x 1.8 set by setLocationAndAngles.
 * handleWaterMovement tests the chunks before it reads, and a water entry
 * after the first update splashes on the bolt's Random (width 0.6: 13
 * bubbles and 13 splashes). handleLavaMovement's World.isMaterialInBB reads
 * with getBlock and no chunk test, so a box that reaches past a loaded edge
 * (a strike on its chunk's x = 0 or z = 0 column) loads the chunk there, as
 * provideChunk does in Java (pground-rd8-bolt-t99743). The lava answer sets
 * only the bolt's own fire, which no world state reads. */
static void bolt_entity_update(struct lightning_bolt *bolt, struct world *w)
{
    const float width = 0.6F, height = 1.8F;
    const double half = (double)(width / 2.0F);
    struct aabb bb = aabb_make(bolt->x - half, bolt->y, bolt->z - half,
                               bolt->x + half, bolt->y + (double)height, bolt->z + half);
    int was = bolt->in_water;
    float fall = 0.0F;
    int hit = handle_water_movement(w, bb, &bolt->mx, &bolt->my, &bolt->mz, &bolt->in_water, &fall);

    if (hit && !was && !bolt->first_update)
    {
        (void)det_rng_float(&bolt->rand);   /* the splash sound's pitch */
        (void)det_rng_float(&bolt->rand);
        for (int i = 0; (float)i < 1.0F + width * 20.0F; ++i)
        {
            (void)det_rng_float(&bolt->rand);   /* bubble: x, z, then motionY's */
            (void)det_rng_float(&bolt->rand);
            (void)det_rng_float(&bolt->rand);
        }
        for (int i = 0; (float)i < 1.0F + width * 20.0F; ++i)
        {
            (void)det_rng_float(&bolt->rand);   /* splash: x, z */
            (void)det_rng_float(&bolt->rand);
        }
    }
    (void)ie_material_in_bb(w, aabb_expand(bb, -0.10000000149011612, -0.4000000059604645, -0.10000000149011612),
                            7);
    bolt->first_update = 0;
}

void lightning_constructor_fire(struct world *w, det_rng *rand, double x, double y,
                                double z, int difficulty, int do_fire_tick)
{
    int bx = (int)floor(x), by = (int)floor(y), bz = (int)floor(z);
    if (!do_fire_tick || difficulty < 2 ||
        !world_do_chunks_near_chunk_exist(w, bx, by, bz, 10)) return;
    if (BLOCKS[world_get_block(w, bx, by, bz) & 4095].material == 0 &&
        fire_can_place(w, bx, by, bz))
        world_set_block(w, bx, by, bz, 51, 0, 3);
    for (int i = 0; i < 4; ++i)
    {
        int fx = bx + det_rng_int_n(rand, 3) - 1;
        int fy = by + det_rng_int_n(rand, 3) - 1;
        int fz = bz + det_rng_int_n(rand, 3) - 1;
        if (BLOCKS[world_get_block(w, fx, fy, fz) & 4095].material == 0 &&
            fire_can_place(w, fx, fy, fz))
            world_set_block(w, fx, fy, fz, 51, 0, 3);
    }
}

void lightning_tick(struct lightning_bolt *bolt, struct world *w, int do_fire_tick,
                    void (*strike)(void *ctx, const struct lightning_bolt *bolt), void *ctx)
{
    ++bolt->ticks_existed;
    /* EntityWeatherEffect inherits Entity.onUpdate */
    bolt_entity_update(bolt, w);
    if (bolt->state == 2)
    {
        (void)det_rng_float(&bolt->rand);
        (void)det_rng_float(&bolt->rand);
    }

    --bolt->state;
    if (bolt->state < 0)
    {
        if (bolt->living_time == 0)
        {
            bolt->dead = 1;
        }
        else if (bolt->state < -det_rng_int_n(&bolt->rand, 10))
        {
            --bolt->living_time;
            bolt->state = 1;
            bolt->vertex = det_rng_long(&bolt->rand);
            int x = (int)floor(bolt->x), y = (int)floor(bolt->y), z = (int)floor(bolt->z);
            if (do_fire_tick && world_do_chunks_near_chunk_exist(w, x, y, z, 10) &&
                BLOCKS[world_get_block(w, x, y, z) & 4095].material == 0 &&
                fire_can_place(w, x, y, z))
                world_set_block(w, x, y, z, 51, 0, 3);
        }
    }

    if (bolt->state >= 0 && strike) strike(ctx, bolt);
}
