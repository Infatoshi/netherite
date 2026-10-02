/* The springs lane: WorldGenLiquids (the rock-pocket water and lava spring of
 * BiomeDecorator's genDecorations) and the liquid flow its one immediate
 * updateTick runs.
 *
 * WorldGenLiquids.generate places a flowing liquid in a pocket with three
 * stone walls and one open side, sets World.scheduledUpdatesAreImmediate and
 * calls updateTick directly. Under the immediate flag every tick the flow
 * schedules (scheduleBlockUpdate from updateTick, from onBlockAdded of a
 * block the flow writes, from BlockStaticLiquid.setNotStationary) runs at
 * once, so a spring's whole cascade is one call and the pending list the
 * cascade would otherwise feed stays empty.
 *
 * The Java class keeps per-instance scratch (field_149815_a, the two arrays
 * func_149808_o fills), and updateTick recurses through the immediate
 * schedule while those are live: func_149813_h's setBlock can re-enter
 * updateTick and refill the arrays the outer frame is still reading. Here
 * that recursion runs on the block callbacks' worklist (blockwl.h), and the
 * environment's arrays with the lazy reads reproduce it exactly. */
#include "features_springs.h"
#include "blockwl.h"
#include "env.h"

#include <string.h>

#include "blocks.h"
#include "randomtick.h"
#include "ticks.h"
#include "tileentity.h"

/* Block ids, Blocks.registerBlock. Blocks.water is the static one (id 9) and
 * Blocks.flowing_water the flowing one (8); Blocks.lava is 11 and
 * Blocks.flowing_lava 10. */
enum {
    SP_AIR = 0, SP_STONE = 1, SP_COBBLESTONE = 4, SP_FLOWING_WATER = 8,
    SP_WATER = 9, SP_FLOWING_LAVA = 10, SP_LAVA = 11, SP_STANDING_SIGN = 63,
    SP_WOODEN_DOOR = 64, SP_LADDER = 65, SP_REEDS = 83, SP_IRON_DOOR = 71,
    SP_FIRE = 51, SP_SKULL = 144
};

/* Material indices, looked up by name so a regenerated blocks.h cannot change
 * what they mean (the same pattern features_lakes.c uses). */
static int MAT_AIR = -1, MAT_LAVA = -1, MAT_PORTAL = -1, MAT_WATER = -1;

/* SP_CLASS[id]: bit 1 the block stops a flow (func_149807_p), bit 2 its
 * material is water, bit 4 lava (built with the material indices) */
static uint8_t SP_CLASS[sizeof BLOCKS / sizeof BLOCKS[0]];
static int blocks_flow(int id);

/* before main: the same for every environment */
__attribute__((constructor)) static void mat_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
    {
        const char *n = MATERIALS[i].name;

        if (n == NULL) continue;

        if (strcmp(n, "air") == 0) MAT_AIR = (int)i;
        else if (strcmp(n, "lava") == 0) MAT_LAVA = (int)i;
        else if (strcmp(n, "Portal") == 0) MAT_PORTAL = (int)i;
        else if (strcmp(n, "water") == 0) MAT_WATER = (int)i;
    }
    for (size_t id = 0; id < sizeof BLOCKS / sizeof BLOCKS[0]; ++id)
    {
        int m = BLOCKS[id].material;

        SP_CLASS[id] = (uint8_t)(blocks_flow((int)id) | (m == MAT_WATER) << 1 | (m == MAT_LAVA) << 2);
    }
}

/* Block.getMaterial(): the material row of a block id. */
static int material_of(int id)
{
    return BLOCKS[id & 4095].material;
}

/* World.getBlock/getBlockMetadata in world coordinates. */
static int block_at(struct world *w, int x, int y, int z)
{
    bwl_quiet();
    return world_get_block(w, x, y, z) & 4095;
}

static int meta_at(struct world *w, int x, int y, int z)
{
    bwl_quiet();
    return world_get_meta(w, x, y, z);
}

/* World.isAirBlock. */
static int air_at(struct world *w, int x, int y, int z)
{
    return material_of(block_at(w, x, y, z)) == MAT_AIR;
}

/* Block.func_149804_e: the liquid depth a position holds, -1 when its block
 * is not this liquid's material. Static and dynamic share a material, so the
 * static terrain water counts for a dynamic liquid's search. */
static int depth_at(struct world *w, int id, int x, int y, int z)
{
    int b = block_at(w, x, y, z);
    return material_of(b) == material_of(id) ? meta_at(w, x, y, z) : -1;
}

/* BlockDynamicLiquid.func_149810_a, over the shared side-source count. */
#define side_sources (nw_env->features_springs.side_sources)

static int depth_of_neighbor(struct world *w, int id, int x, int y, int z, int prev)
{
    int d = depth_at(w, id, x, y, z);

    if (d < 0) return prev;

    if (d == 0) ++side_sources;

    if (d >= 8) d = 0;

    return prev >= 0 && d >= prev ? prev : d;
}

/* BlockDynamicLiquid.func_149807_p: what stops a horizontal flow. */
static int blocks_flow(int id)
{
    int b = id & 4095;

    if (b == SP_WOODEN_DOOR || b == SP_IRON_DOOR || b == SP_STANDING_SIGN || b == SP_LADDER ||
        b == SP_REEDS)
        return 1;

    int m = material_of(b);
    return m == MAT_PORTAL ? 1 : MATERIALS[m].blocks_movement;
}

/* The Random the flow's tick jitter draws from: the one the tick was handed,
 * unless an immediate recursion runs the update with World.rand (the
 * populate stage publishes its World.rand stream; the servertick's ticks run
 * under ticks_set_rand, whose stream ticks_set_rand itself). */
static jrand *tick_rand_stream(jrand *r)
{
    jrand *wr = randomtick_tick_rand_stream();

    return wr != NULL ? wr : r;
}

/* BlockDynamicLiquid.func_149809_q: what a flow can replace. */
static int can_flow_into(struct world *w, int id, int x, int y, int z)
{
    int b = block_at(w, x, y, z);
    int m = material_of(b);

    if (m == material_of(id)) return 0;
    if (m == MAT_LAVA) return 0;

    return !blocks_flow(b);
}

/* One cell of the sideways search, as func_149812_c and func_149808_o test
 * it: FC_BLOCKED when the flow cannot enter it (func_149807_p, or a source of
 * its own liquid), FC_ON when it can and the cell below blocks, FC_FALL when
 * it can and the flow would fall there. The search reads the world only, so
 * each cell's answer is kept for the one func_149808_o call (fc_memo, 0 not
 * read yet): the first test reads what Java's first test reads, in the same
 * order, and a repeated test would read the same cells again. A read that
 * loaded a chunk (map_ver moved: its population may have written anywhere)
 * forgets every answer. */
enum { FC_BLOCKED = 1, FC_ON, FC_FALL };
/* the cells within 5 of the flowing block (a search four deep tests the
 * neighbors of its deepest cells), 11 a side */
#define FC_SIDE 11
#define FC_AT(ox, oz, x, z) (((x) - (ox) + 5) * FC_SIDE + (z) - (oz) + 5)

struct fc_memo {
    uint8_t cell[FC_SIDE * FC_SIDE];
    uint32_t map_ver;
};

static int fc_cell(struct world *w, int id, int x, int y, int z, struct fc_memo *m, int at)
{
    if (m->map_ver != w->map_ver)
    {
        memset(m->cell, 0, sizeof m->cell);
        m->map_ver = w->map_ver;
    }
    if (m->cell[at]) return m->cell[at];

    int b = block_at(w, x, y, z), v;

    if (blocks_flow(b) || (material_of(b) == material_of(id) && meta_at(w, x, y, z) == 0)) v = FC_BLOCKED;
    else v = blocks_flow(block_at(w, x, y - 1, z)) ? FC_ON : FC_FALL;
    /* kept only while no read loaded a chunk */
    if (m->map_ver == w->map_ver) m->cell[at] = (uint8_t)v;
    return v;
}

/* BlockDynamicLiquid.func_149812_c, the sideways path cost search: from each
 * side but the one it came from, a cell the liquid can enter with a hole
 * below costs the depth reached, else the search goes one deeper while the
 * depth is under 4; the cheapest wins, 1000 for none. Java recurses; this
 * walks the same calls in the same order on a stack of at most four frames
 * (the depth runs from 1 to 4). (ox, oz) is the flowing block, memo its
 * fc_memo. */
static int flow_cost(struct world *w, int id, int x, int y, int z, int depth, int from, int ox, int oz,
                     struct fc_memo *memo)
{
    struct fc_frame { int x, z, depth, from, d, best; } st[4];
    int sp = 0, ret;

    st[0].x = x;
    st[0].z = z;
    st[0].depth = depth;
    st[0].from = from;
    st[0].d = 0;
    st[0].best = 1000;

    for (;;)
    {
        struct fc_frame *fr = &st[sp];

        if (fr->d == 4)
        {
            ret = fr->best;
        }
        else
        {
            int d = fr->d++;

            if ((d == 0 && fr->from == 1) || (d == 1 && fr->from == 0) || (d == 2 && fr->from == 3) ||
                (d == 3 && fr->from == 2))
                continue;

            int nx = fr->x, nz = fr->z;

            if (d == 0) nx = fr->x - 1;

            if (d == 1) ++nx;

            if (d == 2) nz = fr->z - 1;

            if (d == 3) ++nz;

            int cell = fc_cell(w, id, nx, y, nz, memo, FC_AT(ox, oz, nx, nz));

            if (cell == FC_BLOCKED) continue;

            if (cell == FC_ON)
            {
                if (fr->depth >= 4) continue;

                /* the call one deeper */
                struct fc_frame *in = &st[++sp];

                WL_DEPTH_NOTE(flow_cost, sp + 1);

                in->x = nx;
                in->z = nz;
                in->depth = fr->depth + 1;
                in->from = d;
                in->d = 0;
                in->best = 1000;
                continue;
            }

            ret = fr->depth;
        }

        /* the frame returns ret to its caller's minimum */
        if (sp == 0) return ret;

        --sp;

        if (ret < st[sp].best) st[sp].best = ret;
    }
}

/* BlockDynamicLiquid.func_149808_o: which horizontal sides win the flow
 * search. Returns the shared flag array; a recursive updateTick between the
 * reads refills it, as the Java instance field does. */
#define flow_dir (nw_env->features_springs.flow_dir)
#define cost_array (nw_env->features_springs.cost_array)

/* The search where every cell it can read is in a loaded chunk, so no read
 * loads one and a read has no effect: the cells are read straight from the
 * chunks, each once, in whatever order, and each side's search runs
 * breadth first. func_149812_c's value from a side is the least depth at
 * which some walk of FC_ON cells from it, never turning straight back, stands
 * beside an FC_FALL cell it may step to (a frame returns its depth on such a
 * neighbor, and every deeper frame returns more), 1000 for none within four:
 * so the search stops at the first layer that finds one, and a (cell,
 * incoming side) met again later is not walked again (its walks from the
 * later meeting are its earlier ones, deeper). */
struct fc_fast {
    uint8_t cell[FC_SIDE * FC_SIDE];
    const struct chunk *ch[4];   /* the 2x2 chunks from (cx0, cz0) */
    int cx0, cz0, ox, oz, y, same;   /* same: id's material's SP_CLASS bit */
};

/* the answer of the cell at grid index at (FC_AT), read at its first test */
static __attribute__((noinline)) int fcf_read(struct fc_fast *m, int at)
{
    int x = m->ox - 5 + at / FC_SIDE, z = m->oz - 5 + at % FC_SIDE;
    const struct chunk *c = m->ch[((x >> 4) - m->cx0) * 2 + (z >> 4) - m->cz0];
    int lx = x & 15, lz = z & 15;
    int cls = SP_CLASS[chunk_xyz_id(c, lx, m->y, lz)], v;

    if ((cls & 1) || ((cls & m->same) && chunk_xyz_meta(c, lx, m->y, lz) == 0)) v = FC_BLOCKED;
    else v = SP_CLASS[chunk_xyz_id(c, lx, m->y - 1, lz)] & 1 ? FC_ON : FC_FALL;
    m->cell[at] = (uint8_t)v;
    return v;
}

static inline int fcf_cell(struct fc_fast *m, int at)
{
    int v = m->cell[at];

    return v ? v : fcf_read(m, at);
}

/* Every cell within 5 of the flowing block answered, a row of a chunk at a
 * time; 1 when one is FC_FALL */
static int fcf_diamond(struct fc_fast *m)
{
    int falls = 0, y = m->y;

    for (int dx = -5; dx <= 5; ++dx)
    {
        int cx = m->ox + dx, lx = cx & 15, r = dx < 0 ? 5 + dx : 5 - dx;
        int cz = m->oz - r, zend = m->oz + r;
        uint8_t *out = &m->cell[FC_AT(m->ox, m->oz, cx, cz)];

        while (cz <= zend)
        {
            int end = (cz | 15) < zend ? (cz | 15) : zend;
            const struct chunk *c = m->ch[((cx >> 4) - m->cx0) * 2 + (cz >> 4) - m->cz0];
            const struct chunk_sec *here = c->mask >> (y >> 4) & 1 ? chunk_sec_at(c, y >> 4) : NULL;
            const struct chunk_sec *below = c->mask >> ((y - 1) >> 4) & 1 ? chunk_sec_at(c, (y - 1) >> 4) : NULL;
            int k = SEC_XYZ(lx, y, cz & 15), kb = SEC_XYZ(lx, y - 1, cz & 15);

            for (; cz <= end; ++cz, ++out, k += 16, kb += 16)
            {
                if (*out)
                {
                    falls |= *out == FC_FALL;
                    continue;
                }

                int cls = SP_CLASS[here != NULL ? chunk_sec_id(here, k) : 0], v;

                if ((cls & 1) || ((cls & m->same) && (here != NULL ? nibble_get(chunk_sec_metas(here), k) : 0) == 0)) v = FC_BLOCKED;
                else v = SP_CLASS[below != NULL ? chunk_sec_id(below, kb) : 0] & 1 ? FC_ON : FC_FALL;
                *out = (uint8_t)v;
                falls |= v == FC_FALL;
            }
        }
    }
    return falls;
}

/* a step to side d (0 -x, 1 +x, 2 -z, 3 +z) in grid indices */
static const int8_t FC_STEP[4] = {-FC_SIDE, FC_SIDE, -1, 1};

static int fast_cost(struct fc_fast *m, int start, int from)
{
    /* a state: its cell's grid index times 4 plus the side it came in by */
    uint16_t lay[2][32];
    uint8_t seen[FC_SIDE * FC_SIDE];
    int n = 1;

    memset(seen, 0, sizeof seen);
    lay[0][0] = (uint16_t)(start * 4 + from);
    seen[start] = (uint8_t)(1 << from);
    for (int depth = 1; depth <= 4; ++depth)
    {
        const uint16_t *cur = lay[(depth - 1) & 1];
        uint16_t *next = lay[depth & 1];
        int nn = 0;

        for (int i = 0; i < n; ++i)
        {
            int at = cur[i] >> 2, back = (cur[i] & 3) ^ 1;

            for (int d = 0; d < 4; ++d)
            {
                if (d == back) continue;

                int nat = at + FC_STEP[d];
                int cell = fcf_cell(m, nat);

                if (cell == FC_FALL) return depth;
                if (cell == FC_ON && depth < 4 && !(seen[nat] >> d & 1))
                {
                    seen[nat] |= (uint8_t)(1 << d);
                    next[nn++] = (uint16_t)(nat * 4 + d);
                }
            }
        }
        n = nn;
    }
    return 1000;
}

/* the fast path when the chunks of the 11x11 cells around (x, z) are loaded
 * and rows y and y - 1 are in the world; 0 otherwise */
static int best_directions_fast(struct world *w, int id, int x, int y, int z, int *cost)
{
    /* World.getBlock's bounds (world.c XZ_IN_RANGE) */
    if (y < 1 || y > 255 || x - 5 < -30000000 || x + 5 >= 30000000 || z - 5 < -30000000 || z + 5 >= 30000000)
        return 0;

    struct fc_fast m;
    int cx0 = (x - 5) >> 4, cz0 = (z - 5) >> 4;

    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
        {
            int cx = cx0 + i, cz = cz0 + j;

            m.ch[i * 2 + j] = NULL;
            if (cx > (x + 5) >> 4 || cz > (z + 5) >> 4) continue;
            m.ch[i * 2 + j] = world_chunk_near(w, cx, cz);
            if (m.ch[i * 2 + j] == NULL && (m.ch[i * 2 + j] = world_chunk(w, cx, cz)) == NULL) return 0;
        }

    bwl_quiet();
    memset(m.cell, 0, sizeof m.cell);
    m.cx0 = cx0;
    m.cz0 = cz0;
    m.ox = x;
    m.oz = z;
    m.y = y;
    m.same = material_of(id) == MAT_LAVA ? 4 : material_of(id) == MAT_WATER ? 2 : 0;

    int origin = FC_AT(x, z, x, z), side[4], ons = 0;

    for (int d = 0; d < 4; ++d)
    {
        side[d] = fcf_cell(&m, origin + FC_STEP[d]);
        ons |= side[d] == FC_ON;
    }

    /* a side to search: every cell the searches can test, the diamond
     * within 5, answered at once; with no fall among them every search
     * finds none */
    if (ons && !fcf_diamond(&m)) ons = 0;
    for (int d = 0; d < 4; ++d)
        cost[d] = side[d] == FC_FALL ? 0 : side[d] == FC_BLOCKED || !ons ? 1000 : fast_cost(&m, origin + FC_STEP[d], d);
    return 1;
}

static const int *best_directions(struct world *w, int id, int x, int y, int z)
{
    int mi = material_of(id) == MAT_LAVA;
    int *cost = cost_array[mi];
    int best = 1000;
    struct fc_memo memo;

    if (best_directions_fast(w, id, x, y, z, cost))
    {
        best = cost[0];
        for (int d = 1; d < 4; ++d)
            if (cost[d] < best) best = cost[d];
        for (int d = 0; d < 4; ++d) flow_dir[mi][d] = cost[d] == best;
        return flow_dir[mi];
    }

    memset(memo.cell, 0, sizeof memo.cell);
    memo.map_ver = w->map_ver;
    for (int d = 0; d < 4; ++d)
    {
        cost[d] = 1000;
        int nx = x, nz = z;

        if (d == 0) nx = x - 1;

        if (d == 1) ++nx;

        if (d == 2) nz = z - 1;

        if (d == 3) ++nz;

        int cell = fc_cell(w, id, nx, y, nz, &memo, FC_AT(x, z, nx, nz));

        if (cell != FC_BLOCKED) cost[d] = cell == FC_ON ? flow_cost(w, id, nx, y, nz, 1, d, x, z, &memo) : 0;
    }

    best = cost[0];

    for (int d = 1; d < 4; ++d)
        if (cost[d] < best) best = cost[d];

    for (int d = 0; d < 4; ++d) flow_dir[mi][d] = cost[d] == best;

    return flow_dir[mi];
}

/* BlockLiquid.func_149799_m: the fizz, in randomtick.c with the rest of the
 * tick's World.rand and Det draws (two nextFloat and sixteen Math.random for
 * the eight smoke particles). It draws from World.rand, not from the Random
 * updateTick was handed: those are one stream in a server tick, but a
 * feature or structure running the update at once hands it the population
 * Random. Outside a tick no World.rand stream is published and nothing is
 * drawn, as the feature and structure probes expect. */
static void fizz(struct world *w, jrand *r, int x, int y, int z)
{
    (void)w;
    (void)r;
    jrand *wr = randomtick_tick_rand_stream();

    if (wr != NULL) randomtick_fizz(wr, x, y, z);
}

/* BlockDynamicLiquid.func_149813_h: pour into one position. The replaced block
 * learns it was broken (its dropBlockAsItemWithChance, the water branch) or
 * the lava branch plays the fizz; both spend World.rand, and the items a drop
 * makes are the tick's spawn recorder's business. */
static void flow_into(struct world *w, int id, int x, int y, int z, int meta, jrand *r)
{
    if (!can_flow_into(w, id, x, y, z)) return;

    int replaced = block_at(w, x, y, z);
    int replaced_meta = meta_at(w, x, y, z);

    if (material_of(id) == MAT_LAVA) fizz(w, r, x, y, z);
    else randomtick_drop(w, x, y, z, replaced, replaced_meta);

    /* BlockSkull.breakBlock, which the write runs for either liquid: a skull
     * without the creative bit drops its item at the tile entity's type
     * through dropBlockAsItem_do on the tick's World.rand (its
     * dropBlockAsItemWithChance is empty, so the water's drop above spent
     * nothing). The write's own tail (world.c block_break_tail) leaves the
     * drop to the paths that harvest a skull themselves (dig.c's
     * skull_drops), so the flow makes it here. */
    if ((replaced & 4095) == SP_SKULL && (replaced_meta & 8) == 0)
    {
        const struct tile_entity *te = world_tile_entity(w, x, y, z);

        randomtick_drop_stack_do(x, y, z, 397, te != NULL && te->kind == TE_SKULL ? te->skull_type : 0);
    }

    BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, id, meta, 3);
}

/* BlockDynamicLiquid.func_149811_n: settle into the static form, same meta,
 * flags 2 (no notification). */
static void turn_static(struct world *w, int id, int x, int y, int z)
{
    BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, id + 1, meta_at(w, x, y, z), 2);
}

/* BlockDynamicLiquid.updateTick as a worklist frame. The first step settles
 * this cell (its level, the static switch, the tick and the notification),
 * the second pours down or picks the sideways directions, and the third
 * pours sideways, one direction per step, reading the shared direction array
 * as each turn comes (a nested update of the same liquid refills it, as the
 * Java instance field is refilled). The locals: */
enum { LQ_DEPTH, LQ_LAVA, LQ_SPREAD, LQ_NEXT };

static int liquid_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z, id = f->op.a[0];
    jrand *r = f->op.p;
    int *L = f->u.l;

    if (f->pc == 0)
    {
        int is_lava = material_of(id) == MAT_LAVA;
        int depth = depth_at(w, id, x, y, z);
        int spread = is_lava && w->dim != -1 ? 2 : 1;   /* water 1; lava 2 outside the nether
                                                         * (BlockDynamicLiquid's var7: 1 in a
                                                         * provider.isHellWorld world) */
        int delay = ticks_liquid_tick_rate(w, id);
        int next;

        if (depth > 0)
        {
            side_sources = 0;
            int d = depth_of_neighbor(w, id, x - 1, y, z, -100);
            d = depth_of_neighbor(w, id, x + 1, y, z, d);
            d = depth_of_neighbor(w, id, x, y, z - 1, d);
            d = depth_of_neighbor(w, id, x, y, z + 1, d);
            next = d + spread;

            if (next >= 8 || d < 0) next = -1;

            int above = depth_at(w, id, x, y + 1, z);

            if (above >= 0) next = above >= 8 ? above : above + 8;

            if (side_sources >= 2 && !is_lava)
            {
                int below = block_at(w, x, y - 1, z);

                if (MATERIALS[material_of(below)].is_solid)
                    next = 0;
                else if (material_of(below) == material_of(id) && meta_at(w, x, y - 1, z) == 0)
                    next = 0;
            }

            /* the flow's tick-rate jitter: a consumed pending tick's draw
             * lands on the Random the tick was handed; the immediate path's
             * recursion runs updateTick with World.rand
             * (WorldServer.func_147454_a's inline updateTick passes
             * this.rand), so when a World.rand stream is published the
             * jitter draws from it */
            if (is_lava && depth < 8 && next < 8 && next > depth &&
                jr_int_n(tick_rand_stream(r), 4) != 0) delay *= 4;

            if (next == depth)
            {
                turn_static(w, id, x, y, z);
            }
            else
            {
                depth = next;

                if (next < 0)
                    BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, SP_AIR, 0, 3);
                else
                {
                    world_set_meta_quiet(w, x, y, z, next);
                    ticks_bwl_sched(w, x, y, z, id, delay);
                    BWL_EMIT(BWL_NOTIFY, w, x, y, z, id, -1);
                }
            }
        }
        else
        {
            turn_static(w, id, x, y, z);
        }

        L[LQ_DEPTH] = depth;
        L[LQ_LAVA] = is_lava;
        L[LQ_SPREAD] = spread;
        f->pc = 1;
        return 0;
    }

    int depth = L[LQ_DEPTH];
    int mi = L[LQ_LAVA];

    if (f->pc == 1)
    {
        if (can_flow_into(w, id, x, y - 1, z))
        {
            if (L[LQ_LAVA] && material_of(block_at(w, x, y - 1, z)) == MAT_WATER)
            {
                BWL_EMIT(BWL_SET_BLOCK, w, x, y - 1, z, SP_STONE, 0, 3);
                bwl_emit_(BWL_SPRING_FIZZ, w, x, y - 1, z, (int[BWL_ARGS]){0}, r);
                return 1;
            }

            flow_into(w, id, x, y - 1, z, depth >= 8 ? depth : depth + 8, r);
            return 1;
        }

        if (!(depth >= 0 && (depth == 0 || blocks_flow(block_at(w, x, y - 1, z))))) return 1;

        (void)best_directions(w, id, x, y, z);
        int next = depth + L[LQ_SPREAD];

        if (depth >= 8) next = 1;

        if (next >= 8) return 1;

        L[LQ_NEXT] = next;
        f->pc = 2;
    }

    /* the shared array best_directions filled (flow_dir[lava]) */
    static const int off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    while (f->i < 4)
    {
        int d = f->i++;

        if (flow_dir[mi][d])
        {
            flow_into(w, id, x + off[d][0], y, z + off[d][1], L[LQ_NEXT], r);

            if (bwl_pending()) return f->i >= 4;
        }
    }

    return 1;
}

void liquid_update_tick(struct world *w, int x, int y, int z, int id, jrand *r)
{
    bwl_call(BWL_LIQUID_TICK, w, x, y, z, (int[BWL_ARGS]){id}, r);
}

/* BlockStaticLiquid.isFlammable: the material burns. */
static int flammable_at(struct world *w, int fx, int fy, int fz)
{
    return MATERIALS[material_of(block_at(w, fx, fy, fz))].can_burn;
}

/* BlockStaticLiquid.updateTick: the lava fire spread. The fire it writes
 * (flag 3) would run BlockFire.onBlockAdded, which is not ported; the probe
 * world never consumes a pending tick, so the path stays cold until the
 * server tick lane takes it. */
/* BlockStaticLiquid.updateTick as a worklist frame: the upward walk ends at
 * its one write; the three tries of a zero-step walk write one fire each, a
 * step per try. The locals: */
enum { SQ_X0, SQ_Z0 };

static int static_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z, id = f->op.a[0];
    jrand *r = f->op.p;
    int *L = f->u.l;

    if (f->pc == 0)
    {
        if (material_of(id) != MAT_LAVA) return 1;

        int steps = jr_int_n(r, 3);

        for (int i = 0; i < steps; ++i)
        {
            x += jr_int_n(r, 3) - 1;
            ++y;
            z += jr_int_n(r, 3) - 1;
            int b = block_at(w, x, y, z);

            if (material_of(b) == MAT_AIR)
            {
                if (flammable_at(w, x - 1, y, z) || flammable_at(w, x + 1, y, z) ||
                    flammable_at(w, x, y, z - 1) || flammable_at(w, x, y, z + 1) ||
                    flammable_at(w, x, y - 1, z) || flammable_at(w, x, y + 1, z))
                {
                    BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, SP_FIRE, 0, 3);
                    return 1;
                }
            }
            else if (MATERIALS[material_of(b)].blocks_movement)
            {
                return 1;
            }
        }

        if (steps != 0) return 1;

        L[SQ_X0] = x;
        L[SQ_Z0] = z;
        f->pc = 1;
    }

    while (f->i < 3)
    {
        ++f->i;
        x = L[SQ_X0] + jr_int_n(r, 3) - 1;
        z = L[SQ_Z0] + jr_int_n(r, 3) - 1;

        if (air_at(w, x, y + 1, z) && flammable_at(w, x, y, z))
        {
            BWL_EMIT(BWL_SET_BLOCK, w, x, y + 1, z, SP_FIRE, 0, 3);
            return f->i >= 3;
        }
    }

    return 1;
}

void liquid_static_update_tick(struct world *w, int x, int y, int z, int id, jrand *r)
{
    bwl_call(BWL_STATIC_TICK, w, x, y, z, (int[BWL_ARGS]){id}, r);
}

int springs_bwl_step(struct bwl_frame *f)
{
    switch (f->op.kind)
    {
    case BWL_LIQUID_TICK:
        return liquid_step(f);

    case BWL_STATIC_TICK:
        return static_step(f);

    case BWL_SPRING_FIZZ:
        fizz(f->op.w, f->op.p, f->op.x, f->op.y, f->op.z);
        return 1;

    default:
        return 1;
    }
}

/* WorldGenLiquids.generate. `block` is field_150521_a: Blocks.flowing_water
 * (8) or Blocks.flowing_lava (10). */
static int feature_spring(struct world *w, jrand *r, int x, int y, int z, int block, int unused)
{
    (void)unused;

    if (block_at(w, x, y + 1, z) != SP_STONE) return 0;
    if (block_at(w, x, y - 1, z) != SP_STONE) return 0;

    int here = block_at(w, x, y, z);

    if (material_of(here) != MAT_AIR && here != SP_STONE) return 0;

    int stone_sides = 0, air_sides = 0;

    if (block_at(w, x - 1, y, z) == SP_STONE) ++stone_sides;
    if (block_at(w, x + 1, y, z) == SP_STONE) ++stone_sides;
    if (block_at(w, x, y, z - 1) == SP_STONE) ++stone_sides;
    if (block_at(w, x, y, z + 1) == SP_STONE) ++stone_sides;

    if (air_at(w, x - 1, y, z)) ++air_sides;
    if (air_at(w, x + 1, y, z)) ++air_sides;
    if (air_at(w, x, y, z - 1)) ++air_sides;
    if (air_at(w, x, y, z + 1)) ++air_sides;

    if (stone_sides == 3 && air_sides == 1)
    {
        world_set_block(w, x, y, z, block, 0, 2);
        ticks_set_immediate(1);
        liquid_update_tick(w, x, y, z, block, r);
        ticks_set_immediate(0);
    }

    return 1;
}

/* The probe's per-feature table for this lane: one row per config, in the
 * same order as the rows appended to FeatureProbe.FEATURES, so a config index
 * means the same thing on both sides. The band fields are unused (the probe
 * draws a spring's y itself, the placement modes 6 and 7) and the margin is
 * the flow's reach, not the generator's one-block pocket. */
static const struct feature SPRINGS[] = {
    {"springwater", 0, 0, 0, 9, SP_FLOWING_WATER, 0, feature_spring},
    {"springlava", 0, 0, 0, 9, SP_FLOWING_LAVA, 0, feature_spring},
};

const struct feature *features_springs_for(const char *name, int *n)
{
    *n = 0;

    if (name == NULL) return NULL;

    if (strcmp(name, "springwater") == 0)
    {
        *n = 1;
        return &SPRINGS[0];
    }

    if (strcmp(name, "springlava") == 0)
    {
        *n = 1;
        return &SPRINGS[1];
    }

    return NULL;
}
