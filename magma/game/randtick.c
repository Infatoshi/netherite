/* game/randtick.c - live random block ticks (see randtick.h). */
#include "game/randtick.h"

#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "mc_gamerules.h"

static int gm_rt_light(GmWorld *w, int x, int y, int z) {
    int sky = gm_world_sky_light(w, x, y, z);
    int blk = gm_world_block_light(w, x, y, z);
    return sky > blk ? sky : blk;
}

#define RT_W GmWorld
#define rt_live_id(w, x, y, z) gm_world_block((w), (x), (y), (z))
#define rt_live_meta(w, x, y, z) (gm_world_meta((w), (x), (y), (z)) & 15)
#define rt_live_light(w, x, y, z) gm_rt_light((w), (x), (y), (z))
#define rt_live_set(w, x, y, z, id, meta) \
    gm_world_set_block_meta((w), (x), (y), (z), (id), (meta))
#include "randtick_live.h"

static int gm_rt_surr[RT_LIVE_SURR];

void gm_randtick_block(GmWorld *w, int wx, int wy, int wz,
                       long long seed, long long tick, const McGameRules *gr) {
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    rt_live_tick_block(w, wx, wy, wz, seed, tick, gr, gm_rt_surr);
}

void gm_randtick_pass(GmWorld *w, long long seed, long long tick,
                      int ccx, int ccz, int radius, const McGameRules *gr) {
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    rt_live_pass(w, seed, tick, ccx, ccz, radius, gr, gm_rt_surr);
}
