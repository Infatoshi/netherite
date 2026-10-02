/* EntityLightningBolt.onUpdate. The constructor's fire placement is in
 * servertick.c, at WorldServer.updateBlocks' thunder strike. */
#include "lightning.h"
#include "blocks.h"
#include "fire.h"
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
    /* EntityWeatherEffect inherits Entity.onUpdate: the firstUpdate flag is
     * cleared, but no Random is drawn when the bolt occupies air. */
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
