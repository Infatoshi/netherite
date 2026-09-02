/* game/randtick.c - live random block ticks (see randtick.h). */
#include "game/randtick.h"

#include "mc_blocks.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "mc_gamerules.h"

#define RT_W GmWorld
#define rt_live_id(w, x, y, z) gm_world_rt_block((w), (x), (y), (z))
#define rt_live_meta(w, x, y, z) (gm_world_rt_meta((w), (x), (y), (z)) & 15)
#define rt_live_light(w, x, y, z) gm_world_rt_light((w), (x), (y), (z))
#define rt_live_block_light(w, x, y, z) gm_world_rt_block_light((w), (x), (y), (z))
#define rt_live_set(w, x, y, z, id, meta) \
    gm_world_rt_set((w), (x), (y), (z), (id), (meta))
static int gm_rt_biome(GmWorld *w, int x, int z) {
    int b = gm_world_biome(w, x, z);
    return b < 0 ? 1 : b;
}
#define rt_live_biome(w, x, z) gm_rt_biome((w), (x), (z))
#define RT_SECTION_NEEDS(w, cx, sec, cz) \
    gm_world_section_needs_randtick((w), (cx), (sec), (cz))
#include "randtick_live.h"

static int gm_rt_surr[RT_LIVE_SURR];

void gm_randtick_block(GmWorld *w, int wx, int wy, int wz,
                       JavaRandom *world_rand, const McGameRules *gr) {
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    rt_live_tick_block(w, wx, wy, wz, world_rand, gr, gm_rt_surr, 0);
}

void gm_randtick_pass(GmWorld *w, JavaRandom *world_rand, i32 *update_lcg,
                      int raining, int thundering,
                      int ccx, int ccz, int radius, const McGameRules *gr) {
    McGameRules def;
    if (!w) return;
    if (!gr) { def = mc_gamerules_default(); gr = &def; }
    rt_live_pass(w, world_rand, update_lcg, raining, thundering,
                 ccx, ccz, radius, gr, gm_rt_surr);
}
