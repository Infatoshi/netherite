/* lane/obsidian3: NETH_VEC_BPORTAL, the bucket portal (vecenv.h). Included by macros.c after obsidian.c (its
 * statics: the env, look_at, inv_count, pp_select, best_tool, and the obsidian macro's ob_solid, ob_lava, ob_step), not
 * compiled alone.
 *
 * What a speedrunner does without a diamond pickaxe: the frame cast in place, nothing mined. Held: flint and steel,
 * two buckets (any of empty, water, lava; the craft macro's IRON plans make the second with this switch on), two
 * cobblestone or dirt; the overworld, the Nether not yet entered. The water and lava sources the eye sees (obsview.h:
 * never behind a wall) are remembered; an empty bucket is filled at the nearest water, the other at the nearest lava,
 * and the frame is built beside that lava (the lava a bucket at a time from there):
 *
 *   the site    the feet's cell A on the ground, the frame's plane one cell ahead (f = 1) along the cardinal direction
 *               away from the lava, r across it (r = 0, 1 the inside, -1 and 2 the columns), h up from the feet. The
 *               frame stands in a slot dug into the ground, its top row one block over it: bottom row h -3, columns
 *               h -2..0, top row h 1 (the corners not needed in 1.7.10), so every face a bucket is aimed at is under
 *               the eye or level with it and the ground is the mould
 *   the dig     the slot's cells (r -1..2 at h 1 and 0 from the anchor row, then h -1, -2 and the bottom row's two at
 *               h -3 leaning over the slot, crouched at its edge), each with the best tool; the slot's walls then in
 *               view: no liquid beside any cell, a solid face for every pour
 *   the cast    a cell T: a water source put in W, a cell beside T or over it (the bucket's ray placed on a solid
 *               face of W's: the slot's back wall), then lava poured into T (the ray on a solid face of T's): lava
 *               put beside water turns to obsidian where it lands (BlockLiquid.func_149805_n, the lava's
 *               onBlockAdded), nothing flows; then W picked back up with the empty bucket. A cell a lava bucket,
 *               the water kept. The lava fetched again from the pool between cells (the walk crouched, never into
 *               lava's reach: the obsidian macro's ob_step)
 *   the top     two blocks put in the inside under the top row, the top row cast on them, the two dug out
 *   the end     flint and steel on an inside face of the frame (the portal shows: block 90 inside), the anchor
 *               row's cell in front of the inside dug, the player drops into it and walks into the portal
 *
 * Every pour is used only when the crosshair (neth_rl_state mo_*: the client's ray, which liquids do not stop, as they do
 * not stop a full bucket's) is on the planned face, or for a pick-up when the empty bucket's own ray (lq_*) is on W,
 * with the look held two steps (the server reads the look the client sent the tick before). What it reads besides the
 * view: the cells it digs, places, pours into and aims at (all within reach and in view as they are worked), and for
 * safety lava near the site and the walk (obsidian's rule: lava's light and sound).
 *
 * Starts (macro_start, before the obsidian macro): bp_want (above) and a step to take: a fill (an empty bucket, water
 * or lava remembered within BP_MEM_GO), the walk to the lava kept, the site, or a build to resume. Ends: the Nether
 * entered (the want gone), a site given up (BP_OFF steps rest), death, food at 6 or under with food held (the eat),
 * the lava escape. It leaves no bucket in hand (a tool, else the first slot without a bucket). */

#define BP_MEM_EVERY 20         /* the memory's look: every this many steps with the switch on */
#define BP_MEM_NEAR 3           /* a source within this (each axis) of a kept one is that one */
#define BP_MEM_GO 512           /* a remembered source this near (the eye's distance) is gone to (lane/obsidian4: any
                                   pool the eye has shown, 48 before: a site's pool ran dry with another remembered) */
/* lane/obsidian4: the lava search, the far trips and the walk */
#define BP_SEARCH_TIER 3        /* the search's strip with a pickaxe of this tier or better held (iron) */
#define BP_SEARCH_FLOOR 12      /* below OB_SEARCH_Y the search's strip runs flat where the feet are, down to this (the
                                   strip's stair down leaves a 2x2 shaft the player falls down: 3 of 8 envs below 15
                                   in 6,000 steps, where the search stopped); under it none (the lava layer, y 10) */
#define BP_SCAN_R 24            /* the memory's look and the look around: sources this far (16 before; with obsview's
                                   OV_EXPOSED, as every scan of this macro's: only sources with a face to air) */
#define BP_SEARCH_SCAN 5      /* its look (lava, water and hot lava ahead) every this many steps */
#define BP_RUN 6.0              /* a walk this far (horizontally) from its point goes upright, not crouched */
#define BP_WALK_FLOOR 12        /* a drop never lands the feet under this (the lava layer's caves, y 10 to 11) */
#define BP_RUN_LAVA 10.0        /* upright only this far (horizontally) from every lava source remembered */
#define BP_DROP 2              /* a drop this deep (blocks) is walked down; deeper ones are walked round */
#define BP_DETOUR 30            /* steps a detour holds its yaw offset */
#define BP_DETOUR_YAW 75.0      /* the detour's yaw offset (degrees, its side alternating) */
#define BP_FAR_D 6.0            /* water, lava or a deep drop ahead is walked round only this far from the point */
#define BP_FILL_CAP 600         /* a fill trip's steps, and BP_FILL_PER more a block of its start's distance */
#define BP_FILL_PER 30
#define BP_BACK_GAIN 300        /* the walk back to the site: given up after this many steps without a block's gain */
#define BP_SITE_KEEP 12000      /* a held site is given up after this many steps without lava held or found */
/* the site with both buckets full: rings of points round the centre (the lava fetched, else the feet), a site tried
 * within BP_SITE_FAR of it, for at most BP_GO_CAP steps (sites beside cave pools were refused for lava under or beside
 * them: 18 to 30 sites given up in a case, the envs left holding both buckets) */
#define BP_NRINGS 4
static const double BP_GO_RINGS[BP_NRINGS] = {6.0, 11.0, 16.0, 22.0};
#define BP_SITE_FAR 25.5
#define BP_SITE_UNDER 4         /* the lava box's depth under the feet: the slot's floor (h -4; 5 before, one under it) */
#define BP_GO_CAP 5000
#define BP_TRIP_OFF 20         /* the rest after a held site's trip found nothing to fetch (the search runs) */
#define BP_POOL_APART 6        /* a lava fill this far (each axis) from every earlier one is another pool (books) */
#define BP_SITE_R 9.0           /* the site is chosen within this (horizontally) of the lava fetched from */
#define BP_SITE_BACK 4          /* and no lava this far behind it or to either side, or 5 under the slot (the water poured
                                   at the frame runs back over the ground: a pool 3 behind turned to obsidian) */
#define BP_GO_R 5.5             /* the site search's points round the lava */
#define BP_FILL_REST 600        /* a source a fill failed at (and its pool, BP_MEM_NEAR round it) left this long
                                   (lane/obsidian4: 150 before; one env went 44 times to lava out of the bucket's reach) */
#define BP_SITE_TRIES 6         /* sites refused in a row: the lava source given up */
#define BP_HOLD 3               /* steps each aim point is held */
#define BP_SETTLE 2             /* steps an aim is held before a use (the server's look is the tick before's) */
#define BP_WAIT 8               /* steps a use's answer is waited for */
#define BP_USES 3               /* uses at one target that changed nothing: the site given up */
#define BP_REACH 4.8            /* a fill's source this near the eye (the bucket's ray is 5) */
#define BP_OFF 300              /* rest after a site given up */
#define BP_STEP_CAP 900         /* steps one plan step may take (a trip included): the site given up */
#define BP_DIG_CAP 200          /* steps one cell's dig may take */
#define BP_FOOD 6               /* food at or under this with food held: the macro yields to the eat */
/* lane/portal (bportal_fill_giveup rest): fill trips ended in a row with no bucket filled before the long rest, and its
 * length (steps). Traced on dev128's base: a stuck water trip ends every 160 to 1,000 steps (fill-stuck), so 6 is about
 * 1,000 to 6,000 steps of trips; the rest leaves the steps to the craft, mine and strip macros and the driver */
#define BP_FILL_STREAK 6
#define BP_FILL_LONG 6000
/* (sky) the steps after a give-up in which the kind's sources come from the eye alone: about the surface walk's cap
 * (BP_ROAM_CAP 36,000) over three give-ups */
#define BP_FILL_BLIND 12000

enum { BM_IDLE, BM_WATER, BM_LAVA, BM_GO, BM_BUILD, BM_LOOK, BM_CLIMB, BM_ROAM, BM_UP, BM_DUMP };
/* lane/obsidian6: BM_DUMP, the one bucket's lava poured back into a remembered pool (water wanted, no empty bucket) */
#define BP_DUMP_HOLD 40         /* steps at reach with no use sent: the pool cell given up (BP_FILL_REST) */
/* lane/obsidian6: the one-bucket cast's other numbers */
#define BP_WET_BOX 0.31         /* the player's box (half its width, 0.3, and a hair): water in a cell it stands in */
#define BP_DRIFT 0.6            /* a dig after the casts goes back to the work position past this (cells, each axis) */
#define BP_ONE_BACK 2           /* the walk back's patience (BP_BACK_GAIN) times this once a cell is cast at the site */
#define BP_WSKIP 3              /* W candidates passed over at one cast (their pour found no aim) before giving up */
/* lane/obsidian5: the walk over the surface (bportal_roam surface): the fill wanted (water, then lava) and none known.
 * Speedrunners find lava on the surface (lakes, ravines open to the sky); lava_survey over the seed set's 1,000 seeds
 * puts a castable open pool within 512 blocks of most spawns, seed 42's nearest at 282 (DEVLOG 2026-11-01 lane/obsidian5) */
#define BP_ROAM_CAP 36000       /* steps the walk and its climbs to the sky may drive an episode; the strip after */
#define BP_ROAM_SKY 8           /* the head's stored sky light (F3's sl) that counts as under the sky (13 failed under
                                   seed 42's forest canopy: 32 of 56 climbs from the surface) */
#define BP_ROAM_DROP 3          /* the run walks down drops this deep (a fall of 3 hurts nothing) */
#define BP_ROAM_LEG 48          /* the spiral's unit: legs of 48, 48, 96, 96, 144, ... blocks, a quarter turn each (the
                                   far look reaches 64, but from the ground it saw lava in 3 of 84 looks with lava in
                                   the cone: a lake is seen when the path passes near it) */
#define BP_ROAM_LOOK_D 16.0     /* a far look every this many blocks walked (horizontally) */
#define BP_FAR_R 64             /* the far look's radius (the client's chunks at render distance 4 reach about this) */
#define BP_FAR_LOOKS 6          /* its looks: six yaws 60 degrees apart */
#define BP_FAR_PITCH 15.0f      /* each looking this far down */
#define BP_ROAM_GAIN 400        /* a leg given up after this many steps without a block nearer its end */
#define BP_ROAM_UNDER 60        /* steps out of the sky's light before the walk ends (the climb takes over) */
#define BP_UP_OFF 600           /* the rest after a climb to the sky gave up (its turns ran out) */
#define BP_ROAM_FOOD 6          /* sprinting on a leg only with food over this */
#define BP_NIGHT0 13000         /* bportal_night strip: the walk yields from this day's time (mobs spawn at light 7) */
#define BP_NIGHT1 23000         /* to this one (the morning) */
#define BP_PILLAR_H 4           /* bportal_roam pillar: the blocks the far look's pillar rises (the eye 5.6 over the
                                   ground: over a rise or a shore that hides a lake 40 to 60 blocks out) */
#define BP_PILLAR_KEEP 2        /* blocks never spent on a pillar (the frame's top needs two) */
#define BP_PILLAR_CAP 160       /* steps the rise, or the way down, may take */
#define BP_CLIMB_OFF 600        /* lane/obsidian4: the climb's rest after it gave up (its turns ran out) */
#define BP_LOOK_DIST 8          /* the look around: due when the feet are this far (horizontally) from the last one's */
#define BP_LOOK_EVERY 600       /* or this many steps after it */
/* the look around's looks: yaw offsets from the first and pitches (level, down, steeply down: lava lies below) */
static const float BP_LOOKS[18][2] = {{0, 10},  {60, 10},  {120, 10}, {180, 10}, {240, 10}, {300, 10},
                                      {330, 40}, {270, 40}, {210, 40}, {150, 40}, {90, 40},  {30, 40},
                                      {0, 70},  {60, 70},  {120, 70}, {180, 70}, {240, 70}, {300, 70}};
/* why a site was given up (bp_book fail_kind) */
enum { BF_SITE, BF_DIG, BF_CHECK, BF_AIM, BF_POUR, BF_TRIP, BF_POS, BF_LIGHT };

/* the plan: a step's kind, its cell (r, h, f) and W's for a cast (wf 9: W chosen at the cast, bp_wpick); a POS
 * step's work position in hundredths of a cell (r, f) */
enum { BO_POS, BO_DIG, BO_CHECK, BO_CAST, BO_PLACE, BO_LIGHT, BO_ENTER };
struct bop { int8_t kind; int16_t r, h, f, wr, wh, wf; };
#define BDIG(r, h, f) {BO_DIG, r, h, f, 0, 0, 0}
#define BCAST(r, h, wr, wh, wf) {BO_CAST, r, h, 1, wr, wh, wf}
static const struct bop BP_PLAN[] = {
    {BO_POS, 100, 0, 50, 0, 0, 0},
    BDIG(-1, 1, 1), BDIG(0, 1, 1), BDIG(1, 1, 1), BDIG(2, 1, 1),
    BDIG(0, 0, 1), BDIG(1, 0, 1), BDIG(-1, 0, 1), BDIG(2, 0, 1),
    {BO_POS, 100, 0, 115, 0, 0, 0},
    BDIG(0, -1, 1), BDIG(1, -1, 1), BDIG(-1, -1, 1), BDIG(2, -1, 1),
    BDIG(0, -2, 1), BDIG(1, -2, 1), BDIG(-1, -2, 1), BDIG(2, -2, 1),
    BDIG(0, -3, 1), BDIG(1, -3, 1),
    {BO_CHECK, 0, 0, 0, 0, 0, 0},
    BCAST(0, -3, 0, -2, 1), BCAST(1, -3, 1, -2, 1),
    BCAST(-1, -2, 0, -2, 1), BCAST(2, -2, 1, -2, 1),
    BCAST(-1, -1, 0, -1, 1), BCAST(2, -1, 1, -1, 1),
    {BO_POS, 100, 0, 50, 0, 0, 0},
    BCAST(-1, 0, -2, 0, 9), BCAST(2, 0, 3, 0, 9),
    {BO_PLACE, 0, 0, 1, 0, 0, 0}, {BO_PLACE, 1, 0, 1, 0, 0, 0},
    BCAST(0, 1, -1, 1, 1), BCAST(1, 1, 2, 1, 1),
    BDIG(0, 0, 1), BDIG(1, 0, 1),
    {BO_LIGHT, 0, 0, 0, 0, 0, 0},
    {BO_POS, 150, 0, 60, 0, 0, 0},
    BDIG(0, -1, 0),
    {BO_ENTER, 0, 0, 0, 0, 0, 0},
};
#define BP_NPLAN ((int)(sizeof BP_PLAN / sizeof BP_PLAN[0]))

/* a face's neighbour offsets by the client's side index (0 bottom, 1 top, 2 -z, 3 +z, 4 -x, 5 +x) */
static const int BP_OX[6] = {0, 0, 0, 0, -1, 1}, BP_OY[6] = {-1, 1, 0, 0, 0, 0}, BP_OZ[6] = {0, 0, -1, 1, 0, 0};

#ifdef BP_TRACE
#define BPT(e, r, what) fprintf(stderr, "BPT %d %ld %s step %d sub %d at %.2f %.2f %.2f hp %.1f mode %d t %d %d %d\n", \
                                (int)((e) - v->e), (long)(e)->steps, what, (e)->bp.step, (e)->bp.sub, (r)->x, (r)->y, \
                                (r)->z, (r)->health, (e)->bp.mode, (e)->bp.tx, (e)->bp.ty, (e)->bp.tz)
#else
#define BPT(e, r, what) ((void)0)
#endif

static int bp_blk(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z, int *meta)
{
    return ob_block(v, e, x, y, z, meta);   /* lane/macroopts: lava as lava_sense allows (obsidian.c) */
}

static int bp_water(int b) { return b == 8 || b == 9; }

/* lane/obsidian6: a plant a hit breaks (sapling, tall grass, dead bush, flowers, mushrooms, snow layer, reeds, vines,
 * double plants) */
static int bp_plant(int b)
{
    return b == 6 || b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 || b == 78 || b == 83 ||
           b == 106 || b == 175;
}

/* a plan cell (r, h, f) in the world */
static void bp_cell(const struct bp_env *b, int r, int h, int f, int *x, int *y, int *z)
{
    *x = b->ax + f * b->fx + r * b->rx;
    *y = b->ay + h;
    *z = b->az + f * b->fz + r * b->rz;
}

/* the player's place in the site's frame (r, f in cells, the anchor cell spanning 0..1 each way) */
static void bp_local(const struct bp_env *b, const struct vec_last *r, double *lr, double *lf)
{
    double dx = r->x - (b->ax + 0.5), dz = r->z - (b->az + 0.5);
    *lr = 0.5 + dx * b->rx + dz * b->rz;
    *lf = 0.5 + dx * b->fx + dz * b->fz;
}

static int bp_buckets(const struct neth_rl_state *s)
{
    return (int)(inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET));
}

static int bp_blocks(const struct neth_rl_state *s) { return (int)(inv_count(s, COBBLE) + inv_count(s, 3)); }

/* the switch on and the portal still wanted: the overworld, never the Nether, alive, flint and steel and two buckets
 * (lane/obsidian6: bportal_buckets 1 or any, one bucket; with one, not while a diamond pickaxe is held and the obsidian
 * macro is on: its case, the obsidian mined with one bucket) */
static int bp_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_BPORTAL) || r->dim != 0 || (e->dims & 1) || (r->flags & PF_DEAD)) return 0;
    if (carry_furnace_gate(v, e, r, s)) return 0;
    if (pg_nether_done(v, s)) return 0;     /* lane/pearlgo: nether_done (macros.c) */
    int n = bp_buckets(s);
    if (inv_count(s, FLINT_STEEL) == 0 || n < (MOPT(MO_BP_BUCKETS) == BB_TWO ? 2 : 1)) return 0;
    if (st_dia_wait(v, s)) return 0;    /* lane/diamond: dia_first (strip2.c) */
    if (n < 2 && (v->macros & NETH_VEC_OBSIDIAN) && inv_count(s, DIAMOND_PICK) > 0 && MOPT(MO_BP_DPICK) != BD_BPORTAL)
        return 0;
    /* lane/obsid (bportal_dpick always): two buckets too (the smelt fixed, the second bucket is made after the pickaxe
     * and the bucket portal took 31% of the kit's steps from spawn, lighting none) */
    if (MOPT(MO_BP_DPICK) == BD_ALWAYS && (v->macros & NETH_VEC_OBSIDIAN) && inv_count(s, DIAMOND_PICK) > 0) return 0;
    return 1;
}

/* lane/obsidian6: the one-bucket cast in use: bportal_buckets 1, or any with one bucket held. A cell's water is poured
 * beside it (W) before its lava is fetched, the one bucket then empty; the lava poured, W picked back up */
static int bp_one(const struct neth_vec *v, const struct neth_rl_state *s)
{
    return MOPT(MO_BP_BUCKETS) == BB_ONE || (MOPT(MO_BP_BUCKETS) == BB_ANY && bp_buckets(s) < 2);
}

/* a source seen remembered (kind 0 water, 1 lava) */
static void bp_mem_add(struct bp_env *b, int k, int x, int y, int z)
{
    for (int i = 0; i < b->nmem[k] && i < BP_MEM; ++i)
        if (abs(b->mem[k][i].x - x) <= BP_MEM_NEAR && abs(b->mem[k][i].y - y) <= BP_MEM_NEAR &&
            abs(b->mem[k][i].z - z) <= BP_MEM_NEAR)
            return;
    /* lane/obsidian4: a full ring overwrites a forgotten entry (a source taken) before the oldest */
    int i = -1;
    if (b->nmem[k] < BP_MEM) i = b->nmem[k]++;
    for (int j = 0; i < 0 && j < BP_MEM; ++j)
        if (b->mem[k][j].y == -1000) i = j;
    if (i < 0) i = b->memn[k]++ % BP_MEM;
    b->mem[k][i].x = x, b->mem[k][i].y = y, b->mem[k][i].z = z;
}

/* lane/obsidian4: a lava bucket filled at (x, y, z): the books' pools */
static void bp_pool_used(struct bp_env *b, int x, int y, int z)
{
    b->bk.fills_lava += 1;
    for (int i = 0; i < b->nused && i < BP_MEM; ++i)
        if (abs(b->used[i].x - x) <= BP_POOL_APART && abs(b->used[i].y - y) <= BP_POOL_APART &&
            abs(b->used[i].z - z) <= BP_POOL_APART)
            return;
    int i = b->nused++ % BP_MEM;
    b->used[i].x = x, b->used[i].y = y, b->used[i].z = z;
    b->bk.pools += 1;
}

static int bp_bad(const struct vec_env *e, int x, int y, int z)
{
    for (int i = 0; i < e->bp.nbad && i < BP_BAD; ++i)
        if (abs(e->bp.bad[i].x - x) <= BP_MEM_NEAR && abs(e->bp.bad[i].y - y) <= BP_MEM_NEAR &&
            abs(e->bp.bad[i].z - z) <= BP_MEM_NEAR && e->bp.bad[i].until > e->steps)
            return 1;
    return 0;
}

static void bp_mark_bad(struct vec_env *e, int x, int y, int z, int64_t len)
{
    int i = e->bp.nbad % BP_BAD;
    e->bp.bad[i].x = x, e->bp.bad[i].y = y, e->bp.bad[i].z = z, e->bp.bad[i].until = e->steps + len;
    e->bp.nbad += 1;
    if (e->bp.nbad >= 2 * BP_BAD) e->bp.nbad -= BP_BAD;
}

/* the nearest remembered source of kind k not given up, within BP_MEM_GO: its index, -1 none */
static int bp_mem_pick(const struct vec_env *e, int k)
{
    const struct bp_env *b = &e->bp;
    double bd = BP_MEM_GO, ey = e->last.y + 1.62;
    int best = -1;
    for (int i = 0; i < b->nmem[k] && i < BP_MEM; ++i)
    {
        if (bp_bad(e, b->mem[k][i].x, b->mem[k][i].y, b->mem[k][i].z)) continue;
        double d = py_dist3(e->last.x, ey, e->last.z, b->mem[k][i].x + 0.5, b->mem[k][i].y + 0.5, b->mem[k][i].z + 0.5);
        if (d < bd) bd = d, best = i;
    }
    return best;
}

/* decode's line, the switch on: the memory's look every BP_MEM_EVERY steps (sources at the surface the eye sees) */
static void bp_observe(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct bp_env *b = &e->bp;
    if (b->on) b->bk.steps_on += 1;
    /* lane/obsidian4: who walked into the water (the air first under its full 300: the eyes under), for a drowning's
     * books (the surface macro drives every drowning's last steps: air under 120 starts it) */
    if (r->air < 300 && b->lair >= 300) b->bk.wet_who = e->ob.bk.who_last;
    b->lair = (r->flags & PF_DEAD) ? 300 : r->air;
    if (r->dim != 0 || (e->dims & 1) || (r->flags & PF_DEAD) || r->screen != PS_NONE) return;
    if (e->steps - b->scan_at < BP_MEM_EVERY) return;
    b->scan_at = e->steps;
    struct obsview_hit h[OV_KINDS];
    if (obsview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, BP_SCAN_R,
                     1 << OV_WATER | 1 << OV_LAVA | OV_EXPOSED, NULL, NULL, h) != 0)
        return;
#ifdef BP_TRACE
    if (e->steps % 200 < BP_MEM_EVERY)
        fprintf(stderr, "BPT %d %ld look water %d (%.1f) lava %d (%.1f) at %.1f %.1f %.1f\n", (int)(e - v->e),
                (long)e->steps, h[OV_WATER].n, h[OV_WATER].d, h[OV_LAVA].n, h[OV_LAVA].d, r->x, r->y, r->z);
    if (h[OV_WATER].d > 0.0f)
        fprintf(stderr, "BPO %d %ld eye 0 %d %d %d\n", (int)(e - v->e), (long)e->steps, h[OV_WATER].x, h[OV_WATER].y,
                h[OV_WATER].z);
    if (h[OV_LAVA].d > 0.0f)
        fprintf(stderr, "BPO %d %ld eye 1 %d %d %d\n", (int)(e - v->e), (long)e->steps, h[OV_LAVA].x, h[OV_LAVA].y,
                h[OV_LAVA].z);
#endif
    if (h[OV_WATER].d > 0.0f) bp_mem_add(b, 0, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z);
    if (h[OV_LAVA].d > 0.0f) bp_mem_add(b, 1, h[OV_LAVA].x, h[OV_LAVA].y, h[OV_LAVA].z);
    if (MOPT(MO_BP_ORACLE) && (b->oracle_at <= 0 || e->steps - b->oracle_at >= OB_ORACLE_EVERY))
    {
        /* lane/chain (bportal_oracle on, PRIVILEGED): obsidian.c oracle_sources' water and lava sources, as if seen */
        b->oracle_at = e->steps > 0 ? e->steps : 1;
        int bx[OB_ORACLE_N], by[OB_ORACLE_N], bz[OB_ORACLE_N];
        for (int k = 0; k < 2; ++k)
        {
            if (e->steps < b->fill_blind[k]) continue;     /* lane/portal (bportal_fill_giveup sky): the eye's alone */
            int n = oracle_sources(v, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01), (int)floor(r->z), k,
                                   bx, by, bz);
            for (int j = 0; j < n; ++j)
            {
#ifdef BP_TRACE
                fprintf(stderr, "BPO %d %ld oracle %d %d %d %d\n", (int)(e - v->e), (long)e->steps, k, bx[j], by[j], bz[j]);
#endif
                bp_mem_add(b, k, bx[j], by[j], bz[j]);
            }
        }
    }
}

/* a site given up: the books, the lava it stood by rested, the macro rests. Its return: the macro over */
static int bp_abort(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int why)
{
    (void)v, (void)r;
    struct bp_env *b = &e->bp;
    BPT(e, r, "abort");
#ifdef BP_TRACE
    fprintf(stderr, "BPT %d abort why %d\n", (int)(e - v->e), why);
#endif
    b->bk.aborts += 1;
    b->bk.fail_kind[why & 7] += 1;
    if (why == BF_TRIP && b->site)
    {
        /* no lava (or water) to fetch now: the frame kept, the macro rests and resumes it once a source is seen */
        b->mode = BM_IDLE;
        b->off = e->steps + BP_TRIP_OFF;    /* lane/obsidian4: the search goes on meanwhile (BP_OFF before) */
        return 1;
    }
    if (b->site)
    {
        /* the site given up (its anchor kept off a while; the lava it stood by only when no frame cell could be cast
         * there, a fill or the walk having failed) */
        int i = b->nbs % BP_BAD;
        b->bs[i].x = b->ax, b->bs[i].y = b->ay, b->bs[i].z = b->az, b->bs[i].until = e->steps + 3000;
        b->nbs = b->nbs + 1 >= 2 * BP_BAD ? b->nbs + 1 - BP_BAD : b->nbs + 1;
        if (why == BF_POS || why == BF_AIM) bp_mark_bad(e, b->lx, b->ly, b->lz, 600);
    }
    else if (b->mode == BM_LAVA || b->mode == BM_WATER) bp_mark_bad(e, b->tx, b->ty, b->tz, 1200);
    b->site = 0;
    b->mode = BM_IDLE;
    b->off = e->steps + BP_OFF;
    return 1;
}

/* the site's frame yaw (looking along f) */
static float bp_yaw(const struct bp_env *b)
{
    /* + 0.0: atan2(-0.0, 1) is -0.0, which the oracle's script writes as -0 and Java reads as 0 (the arena's tape
     * differed in the yaw's sign at its first look along +z) */
    return (float)(atan2(-(double)b->fx, (double)b->fz) * 180.0 / M_PI + 0.0);
}

/* a site at the feet facing (fx, fz): the cells a player sees from there (the ground stood on, the cells over the slot)
 * and lava near it (safety: lava's light) */
/* lane/obsidian4: why a site is refused (BS_*; the books count the first direction's, bp_book site_why) */
enum { BS_OK, BS_GIVEN_UP, BS_GROUND, BS_FEET, BS_PLANE, BS_LAVA, BS_WATER, BS_KINDS };
static int bp_site_why(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, int fx, int fz)
{
    struct bp_env t = e->bp;
    t.ax = (int)floor(r->x), t.ay = (int)floor(r->y + 0.01), t.az = (int)floor(r->z);
    for (int i = 0; i < e->bp.nbs && i < BP_BAD; ++i)
        if (e->bp.bs[i].until > e->steps && abs(e->bp.bs[i].x - t.ax) <= 2 && abs(e->bp.bs[i].y - t.ay) <= 2 &&
            abs(e->bp.bs[i].z - t.az) <= 2)
            return BS_GIVEN_UP;
    t.fx = fx, t.fz = fz, t.rx = -fz, t.rz = fx;
    int x, y, z;
    for (int rr = 0; rr <= 1; ++rr)
    {
        bp_cell(&t, rr, -1, 0, &x, &y, &z);
        int g = bp_blk(v, e, x, y, z, NULL);
        if (!ob_solid(g) || g == 12 || g == 13) return BS_GROUND;     /* the anchor row stood on (no sand or gravel) */
        for (int h = 0; h <= 1; ++h)
        {
            bp_cell(&t, rr, h, 0, &x, &y, &z);
            if (ob_solid(bp_blk(v, e, x, y, z, NULL))) return BS_FEET;
        }
    }
    for (int rr = -1; rr <= 2; ++rr)
        for (int h = 0; h <= 1; ++h)
        {
            bp_cell(&t, rr, h, 1, &x, &y, &z);
            int c = bp_blk(v, e, x, y, z, NULL);
            if (c < 0 || c == 7 || c == OBSIDIAN || bp_water(c) || ob_lava(c)) return BS_PLANE;
        }
    /* lava behind too: the water poured at the frame runs back over the ground the player stands on (a pool 3 blocks
     * behind the anchor turned to obsidian before the last cell's lava was fetched) */
    for (int rr = 1 - BP_SITE_BACK; rr <= BP_SITE_BACK; ++rr)
        for (int ff = -BP_SITE_BACK; ff <= 3; ++ff)
            for (int h = -BP_SITE_UNDER; h <= 2; ++h)
            {
                bp_cell(&t, rr, h, ff, &x, &y, &z);
                int c = bp_blk(v, e, x, y, z, NULL);
                if (c < 0 || ob_lava(c)) return BS_LAVA;
                if (bp_water(c) && ff >= 0 && ff <= 2 && rr >= -2 && rr <= 3 && h <= 1) return BS_WATER;
            }
    return BS_OK;
}

static int bp_site_ok(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int fx, int fz, int book)
{
    int why = bp_site_why(v, e, r, fx, fz);
    if (book) e->bp.bk.site_why[why] += 1;
    return why == BS_OK;
}

/* lane/obsidian4: no lava remembered within BP_RUN_LAVA (horizontally) of the feet, the feet at BP_WALK_FLOOR or over:
 * the walk may go upright */
static int bp_lava_clear(const struct vec_env *e, const struct vec_last *r)
{
    const struct bp_env *b = &e->bp;
    if ((int)floor(r->y + 0.01) < BP_WALK_FLOOR) return 0;
    for (int i = 0; i < b->nmem[1] && i < BP_MEM; ++i)
        if (b->mem[1][i].y != -1000 && py_hypot2(b->mem[1][i].x + 0.5 - r->x, b->mem[1][i].z + 0.5 - r->z) < BP_RUN_LAVA)
            return 0;
    return 1;
}

/* lane/obsidian6: water in a cell the player's box stands in at the feet (its push: 0.014 a tick away from the source) */
static int bp_wet_box(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    for (int k = 0; k < 4; ++k)
        if (bp_water(bp_blk(v, e, (int)floor(r->x + (k & 1 ? BP_WET_BOX : -BP_WET_BOX)), (int)floor(r->y + 0.01),
                            (int)floor(r->z + (k & 2 ? BP_WET_BOX : -BP_WET_BOX)), NULL)))
            return 1;
    return 0;
}

/* the walk to (x, z) (a point; y the target's for the look): crouched, on the ground, never into lava's reach. 1 when
 * within `near` horizontally */
static int bp_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey,
                   double x, double y, double z, double near)
{
    struct bp_env *b = &e->bp;
    double hd = py_hypot2(x - r->x, z - r->z), yaw, pitch;
    if (hd <= near) return 1;
    look_at(r->x, ey, r->z, x, y, z, &yaw, &pitch);
    /* lane/obsidian4: a detour round water, lava or a deep drop ahead (far from the point only: near it the pool is
     * the point) */
    if (b->det_until > e->steps) yaw += b->det_off;
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)(pitch > 30.0 ? 30.0 : pitch < -10.0 ? -10.0 : pitch);
    /* lane/obsidian4: crouched while it stands too (a stop with no key held stood a player up at a ledge over a pool:
     * the fall to y 11, beside the lava, in a traced case) */
    a->hold = 1u << PK_SNEAK;
    int st = ob_step(v, e, r, yaw);
    /* lane/obsidian5: the surface walk's run (b->rm_fast: no lava known, under the sky): upright, the forward key
     * held in the air too (a jump up a block carried over it), drops of BP_ROAM_DROP walked down */
    /* lane/obsidian6: one bucket, every walk farther than BP_FAR_D runs so too (its fills and the walk to the site are
     * long: the water may be far from the lava; crouched, a 38-block walk to the site dug through a step it could jump
     * and ran out of BM_GO's steps) */
    int one = bp_one(v, s);
    int fast = (b->rm_fast || (one && hd > BP_FAR_D)) && bp_lava_clear(e, r);
    int wet_feet = fast && bp_water(bp_blk(v, e, (int)floor(r->x), (int)floor(r->y + 0.01), (int)floor(r->z), NULL));
    if (fast && st != 0 && (!r->on_ground || wet_feet))
    {
        a->hold = 1u << PK_FORWARD | (wet_feet ? 1u << PK_JUMP : 0);
        return 0;
    }
    double mv = py_hypot2(r->x - b->px, r->z - b->pz);
    b->stuck = mv < 0.02 ? b->stuck + 1 : 0;
#ifdef BP_STEP_FROM
    if ((int)(e - v->e) == BP_ENV && e->steps >= BP_STEP_FROM && e->steps <= BP_STEP_TO)
        fprintf(stderr, "BPW %ld st %d hd %.2f stuck %d det %d mv %.3f fast %d\n", (long)e->steps, st, hd, b->stuck,
                b->det_until > e->steps, mv, fast);
#endif
    b->px = r->x, b->pz = r->z;
    int down = 0;
    if (r->on_ground && hd > BP_FAR_D)
    {
        double yr = yaw * M_PI / 180.0;
        int nx = (int)floor(r->x - sin(yr) * 0.8), nz = (int)floor(r->z + cos(yr) * 0.8), fy = (int)floor(r->y + 0.01);
        int wet = bp_water(bp_blk(v, e, nx, fy, nz, NULL)) || bp_water(bp_blk(v, e, nx, fy - 1, nz, NULL));
        int deep = 0;
        if (st == 3)
        {
            /* the drop's floor: walked down when within BP_DROP and dry, without lava beside where it lands */
            int land = 0;
            for (int d = 2; d <= (fast ? BP_ROAM_DROP : BP_DROP) + 1 && !land; ++d)
            {
                int c = bp_blk(v, e, nx, fy - d, nz, NULL);
                if (ob_solid(c)) land = d;
                else if (c < 0 || bp_water(c) || ob_lava(c)) break;
            }
            /* never down to the lava layer: every lava death of the macro's in a traced case (5 of 5) came after a walk
             * dropped the feet to y 10.9 */
            if (land && fy - land + 1 >= BP_WALK_FLOOR && !ob_lava_near(v, e, nx, fy - land + 1, nz) &&
                !ob_lava_near(v, e, nx, fy - land + 2, nz))
                down = 1;
            else deep = 1;
        }
        if (st == 0 || wet || deep || (b->stuck > 12 && b->det_until <= e->steps))
        {
            /* a new detour takes the other side from the last one; one blocked too goes on round (its time renewed),
             * and after 30 steps blocked on it turns to the other side */
            if (b->det_until <= e->steps)
            {
                b->det_sign = (b->ndet++ & 1) ? -1 : 1;
                b->det_off = b->det_sign * BP_DETOUR_YAW;
            }
            else if (fabs(b->det_off) < 165.0)
                b->det_off += b->det_sign * 15.0;    /* blocked on the detour too: turned further */
            else
                b->det_sign = -b->det_sign, b->det_off = b->det_sign * BP_DETOUR_YAW;
            b->det_until = e->steps + BP_DETOUR;
            return 0;
        }
    }
    if ((st == 1 || st == 2) && bp_one(v, s) && bp_wet_box(v, e, r))
    {
        /* lane/obsidian6: one bucket: upright through water (W's flow, standing through the lava trip, spreads over
         * the ground round the site; crouched, its push, 0.014 a tick, outran the walk's 0.006 and the walk back from
         * the pool gave up), a jump up a step */
        a->hold = 1u << PK_FORWARD | (st == 2 ? 1u << PK_JUMP : 0);
        return 0;
    }
    if (st == 0 || !r->on_ground) return 0;
    a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
    /* lane/obsidian4: upright when far and the step is flat, up or a shallow drop (crouched, a walk of 100 blocks took
     * 1,500 steps) */
    if (hd > BP_RUN && (st == 1 || st == 2 || down) && bp_lava_clear(e, r)) a->hold &= ~(1u << PK_SNEAK);
    if (fast) a->hold &= ~(1u << PK_SNEAK);
    if (st == 2 && (b->stuck > 2 || fast)) a->hold |= 1u << PK_JUMP;
    if (b->stuck > 4)
    {
        /* a wall in the way (a cave's turns): the cell ahead at the feet, then at the head, dug (the best tool; never
         * bedrock or obsidian, never a block with lava beside it) */
        double yr = yaw * M_PI / 180.0;
        int nx = (int)floor(r->x - sin(yr) * 0.8), nz = (int)floor(r->z + cos(yr) * 0.8), fy = (int)floor(r->y + 0.01);
        for (int dy = 1; dy >= 0; --dy)
        {
            int c = bp_blk(v, e, nx, fy + dy, nz, NULL);
            if (!ob_solid(c) || c == 7 || c == OBSIDIAN || ob_lava_near(v, e, nx, fy + dy, nz)) continue;
            double yw, pt;
            look_at(r->x, ey, r->z, nx + 0.5, fy + dy + 0.5, nz + 0.5, &yw, &pt);
            a->look[0] = (float)yw;
            a->look[1] = (float)pt;
            a->hold = 1u << PK_SNEAK;
            if (s->mo_hit && s->mo_x == nx && s->mo_y == fy + dy && s->mo_z == nz)
            {
                a->hold |= 1u << PK_ATTACK;
                int best = best_tool(&v->T, s, c);
                if (best >= 0 && best != s->current) a->hotbar = best;
            }
            b->stuck = 5;               /* digging: no other give-up while it digs */
            break;
        }
        /* lane/obsidian6: one bucket: the crosshair on another block beside the player at the feet or the head is dug
         * too (a snow layer on the step held one walk 1,600 steps: the aim at the cell under it met the snow; in a
         * corner, the aim at the diagonal cell met its side neighbour first and a lava trip stood 160 steps) */
        int px0 = (int)floor(r->x), pz0 = (int)floor(r->z);
        if (one && s->mo_hit && abs(s->mo_x - px0) <= 1 && abs(s->mo_z - pz0) <= 1 &&
            (s->mo_x != px0 || s->mo_z != pz0) && s->mo_y >= fy && s->mo_y <= fy + 1 &&
            s->mo_block != 7 && s->mo_block != OBSIDIAN && !ob_lava_near(v, e, nx, s->mo_y, nz) &&
            !(a->hold & (1u << PK_ATTACK)))
        {
            a->hold |= 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, s->mo_block);
            if (best >= 0 && best != s->current) a->hotbar = best;
        }
    }
    return 0;
}

/* the work position (lr, lf) in the site's frame: the look along the frame, the keys in its axes, crouched. 1 there */
static int bp_goto(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey,
                   double tr, double tf)
{
    struct bp_env *b = &e->bp;
    double lr, lf;
    bp_local(b, r, &lr, &lf);
    double dr = tr - lr, df = tf - lf;
    /* lane/obsidian4: the feet under the anchor's level (a trip came back a block lower): walked, the step jumped (the
     * fine keys never jump: a site of 9 cells cast was given up 2 blocks from its work position) */
    if (fabs(dr) > 1.6 || fabs(df) > 1.6 || (r->on_ground && r->y < b->ay - 0.4))
    {
        /* far: walked to, then the fine keys */
        double wx = b->ax + 0.5 + (tr - 0.5) * b->rx + (tf - 0.5) * b->fx;
        double wz = b->az + 0.5 + (tr - 0.5) * b->rz + (tf - 0.5) * b->fz;
        bp_walk(v, e, r, s, a, ey, wx, b->ay + 0.5, wz, 0.0);
        return 0;
    }
    a->look_mode = 1;
    a->look[0] = bp_yaw(b);
    a->look[1] = 45.0f;
    if (fabs(dr) < 0.12 && fabs(df) < 0.08 && r->on_ground) return 1;
    a->hold = 1u << PK_SNEAK;
    /* lane/obsidian6: one bucket, in W's flow, the inside covered (the top row's casts): upright (crouched, the flow
     * held the player off the work position) */
    if (bp_one(v, s) && b->step > 0 && BP_PLAN[b->step].kind == BO_CAST && BP_PLAN[b->step].h == 1 &&
        bp_wet_box(v, e, r))
        a->hold = 0;
    if (df > 0.04) a->hold |= 1u << PK_FORWARD;
    else if (df < -0.04) a->hold |= 1u << PK_BACK;
    if (dr > 0.06) a->hold |= 1u << PK_RIGHT;
    else if (dr < -0.06) a->hold |= 1u << PK_LEFT;
    return 0;
}

/* the aim: point i of the candidates for a pour into cell (x, y, z) (the faces of its solid neighbours that face the
 * eye, the back wall's first, each at five points), 0 when i is past them */
static int bp_aim_point(const struct neth_vec *v, const struct vec_env *e, double ey, int x, int y, int z, int i,
                        double *px, double *py, double *pz)
{
    const struct bp_env *b = &e->bp;
    /* the faces' order: back (+f), bottom, the two sides, front (-f), top */
    int dirs[6][3] = {{b->fx, 0, b->fz}, {0, -1, 0}, {b->rx, 0, b->rz}, {-b->rx, 0, -b->rz}, {-b->fx, 0, -b->fz},
                      {0, 1, 0}};
    static const double J[5][2] = {{0, 0}, {0.3, 0}, {-0.3, 0}, {0, 0.3}, {0, -0.3}};
    int n = 0;
    for (int k = 0; k < 6; ++k)
    {
        int dx = dirs[k][0], dy = dirs[k][1], dz = dirs[k][2];
        if (!ob_solid(bp_blk(v, e, x + dx, y + dy, z + dz, NULL))) continue;
        double cx = x + 0.5 + 0.5 * dx, cy = y + 0.5 + 0.5 * dy, cz = z + 0.5 + 0.5 * dz;
        /* the eye on the cell's side of the face */
        if ((e->last.x - cx) * dx + (ey - cy) * dy + (e->last.z - cz) * dz >= -0.05) continue;
        for (int j = 0; j < 5; ++j, ++n)
            if (n == i)
            {
                /* the face's two axes: a top or bottom face x and z, an x face z and y, a z face x and y */
                double u = J[j][0], w = J[j][1];
                *px = cx, *py = cy, *pz = cz;
                if (dy) *px += u, *pz += w;
                else if (dx) *pz += u, *py += w;
                else *px += u, *py += w;
                return 1;
            }
    }
    return 0;
}

/* the item's slot current: pp_select (1 once current; *rc the macro's return while it is fetched) */
static int bp_hold(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int item, int *rc)
{
    int hb;
    return pp_select(v, e, r, s, a, item, &hb, rc);
}

/* the pour (PL) or the pick-up (TK) aim and use. PL: item poured into (x, y, z) through a solid neighbour's face,
 * used when the crosshair's block and side name that cell. TK: the empty bucket's ray on the cell, a source of
 * `liq` (8 water, 10 lava). LT: flint and steel on a frame face whose cell is inside the frame. 0 going on (or a use
 * just sent: b->waited counts), 1 the use sent, -1 no aim worked */
enum { BA_PL, BA_TK, BA_LT };
static int bp_aim_use(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey, int mode, int item, int x, int y, int z, int liq)
{
    struct bp_env *b = &e->bp;
    double px = x + 0.5, py = y + 0.5, pz = z + 0.5;
    int idx = b->aim;
    if (mode == BA_PL)
    {
        if (!bp_aim_point(v, e, ey, x, y, z, idx, &px, &py, &pz))
        {
            if (idx == 0) return -1;
            b->aim = 0, b->held = 0;
            b->tries += 1;
            if (b->tries > 2) return -1;
            return 0;
        }
    }
    else if (mode == BA_TK)
    {
        /* the cell's faces that face the eye, just inside them, each at five points (a cast cell beside it hides its
         * middle from where the player stands) */
        static const double J[5][2] = {{0, 0}, {0.35, 0.3}, {-0.35, 0.3}, {0.35, -0.3}, {-0.35, -0.3}};
        int n = 0, found = 0;
        for (int k = 0; k < 6 && !found; ++k)
        {
            double cx = x + 0.5 + 0.5 * BP_OX[k], cy = y + 0.5 + 0.5 * BP_OY[k], cz = z + 0.5 + 0.5 * BP_OZ[k];
            if ((r->x - cx) * BP_OX[k] + (ey - cy) * BP_OY[k] + (r->z - cz) * BP_OZ[k] <= 0.05) continue;
            for (int j = 0; j < 5 && !found; ++j, ++n)
                if (n == idx)
                {
                    px = cx - 0.03 * BP_OX[k], py = cy - 0.03 * BP_OY[k], pz = cz - 0.03 * BP_OZ[k];
                    if (BP_OY[k]) px += J[j][0], pz += J[j][1];
                    else if (BP_OX[k]) pz += J[j][0], py += J[j][1];
                    else px += J[j][0], py += J[j][1];
                    found = 1;
                }
        }
        if (!found)
        {
            b->aim = 0, b->held = 0;
            if (idx == 0 || ++b->tries > 2) return -1;
            return 0;
        }
    }
    else
    {
        /* LT: the inside's cells (r 0..1, h -2..0) and their frame neighbours' faces */
        int n = 0, found = 0;
        for (int h = -1; h >= -2 && !found; --h)
            for (int rr = 0; rr <= 1 && !found; ++rr)
            {
                int ix, iy, iz;
                bp_cell(b, rr, h, 1, &ix, &iy, &iz);
                for (int k = 0; k < 6 && !found; ++k)
                {
                    int nx = ix + BP_OX[k], ny = iy + BP_OY[k], nz = iz + BP_OZ[k];
                    if (bp_blk(v, e, nx, ny, nz, NULL) != OBSIDIAN) continue;
                    double cx = ix + 0.5 + 0.5 * BP_OX[k], cy = iy + 0.5 + 0.5 * BP_OY[k], cz = iz + 0.5 + 0.5 * BP_OZ[k];
                    if ((r->x - cx) * BP_OX[k] + (ey - cy) * BP_OY[k] + (r->z - cz) * BP_OZ[k] >= -0.05) continue;
                    if (n++ == idx) px = cx, py = cy, pz = cz, found = 1;
                }
            }
        if (!found)
        {
            if (idx == 0) return -1;
            b->aim = 0, b->held = 0;
            if (++b->tries > 2) return -1;
            return 0;
        }
    }
    double yaw, pitch;
    look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    /* upright and still: the crosshair's eye is then the server's (crouched, the client's camera sits lower than the
     * feet + 1.62 the bucket's ray starts from on the server, and a pour landed a face off: an inside cell cast) */
    int rc = 0;
    int sel = bp_hold(v, e, r, s, a, item, &rc);
    b->held += 1;
    int ok = 0;
    if (mode == BA_PL)
    {
        int sd = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
        ok = s->mo_hit && s->mo_x + BP_OX[sd] == x && s->mo_y + BP_OY[sd] == y && s->mo_z + BP_OZ[sd] == z &&
             !blocks_use(s);
    }
    else if (mode == BA_TK)
        ok = s->lq_hit && s->lq_x == x && s->lq_y == y && s->lq_z == z && s->lq_meta == 0 &&
             (s->lq_block == liq || s->lq_block == liq + 1);
    else
    {
        int sd = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
        int cx = s->mo_x + BP_OX[sd], cy = s->mo_y + BP_OY[sd], cz = s->mo_z + BP_OZ[sd];
        double clr = (cx + 0.5 - (b->ax + 0.5)) * b->rx + (cz + 0.5 - (b->az + 0.5)) * b->rz;
        double clf = (cx + 0.5 - (b->ax + 0.5)) * b->fx + (cz + 0.5 - (b->az + 0.5)) * b->fz;
        int ch = cy - b->ay;
        ok = s->mo_hit && s->mo_block == OBSIDIAN && fabs(clf - 1.0) < 0.1 && clr > -0.1 && clr < 1.1 && ch >= -2 &&
             ch <= 0 && !ob_solid(bp_blk(v, e, cx, cy, cz, NULL));
    }
    if (ok && sel && b->held > BP_SETTLE && r->screen == PS_NONE)
    {
        /* the look the crosshair was read with, kept for the use (the server's ray is the look sent before) */
        a->look[0] = r->yaw;
        a->look[1] = r->pitch;
        a->press[PK_USE] = 1;
        b->bk.uses += 1;
        b->held = 0;
        return 1;
    }
    if (!sel && rc) return -1;
    if (b->held > BP_HOLD + BP_SETTLE)
    {
        b->aim += 1;
        b->held = 0;
    }
    return 0;
}

/* the fill's scan leaves out the cell it just failed to take (exactly that cell, a while) */
static int bp_skip(void *ctx, int kind, int x, int y, int z)
{
    const struct vec_env *e = ctx;
    (void)kind;
    return e->bp.skuntil > e->steps && x == e->bp.skx && y == e->bp.sky && z == e->bp.skz;
}

/* a fill trip (mode BM_WATER, BM_LAVA): the remembered source walked to until the empty bucket's ray lands on a source
 * of its kind within reach, the bucket used. 1 over (filled or given up) */
static int bp_fill(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    int lava = b->mode == BM_LAVA, liq = lava ? 10 : 8, full = lava ? LAVA_BUCKET : WATER_BUCKET;
    if ((int)inv_count(s, full) > b->before)
    {
        if (lava)
        {
            bp_pool_used(b, b->lx, b->ly, b->lz);      /* lane/obsidian4: the books' pools */
            b->site_lava_at = e->steps;
        }
        return 1;
    }
    if (inv_count(s, BUCKET) == 0) return 1;
    double px = b->tx + 0.5, py = b->ty + (lava ? 0.6 : 0.85), pz = b->tz + 0.5;
    double d = py_dist3(r->x, ey, r->z, px, py, pz);
    /* lane/obsidian4: the cap grows with the trip's distance at its start (a pool any distance off) */
    if (b->best > 1e8 && !b->fd0set) b->fd0 = d, b->fd0set = 1;
    if (e->age - b->at > BP_FILL_CAP + BP_FILL_PER * (b->fd0set ? b->fd0 : 0.0))
    {
        BPT(e, r, "fill-cap");
        bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
        return 1;
    }
    if (d < b->best - 0.5) b->best = d, b->gain = e->age;
    if (e->age - b->gain > 160)
    {
        BPT(e, r, "fill-stuck");
        bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
        return 1;
    }
    if (b->phase > 0)
    {
        /* a use sent: its answer */
        if (++b->waited < BP_WAIT) return 0;
        b->phase = 0;
        if (++b->uses >= BP_USES)
        {
            bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
            return 1;
        }
    }
    int lq = s->lq_hit && s->lq_meta == 0 && (s->lq_block == liq || s->lq_block == liq + 1);
    double ld = lq ? py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) : 99.0;
    if (lq && ld <= BP_REACH)
    {
        /* the source the ray lands on: the bucket, the look held */
        double yaw, pitch;
        look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = r->pitch;
        int rc = 0;
        if (!bp_hold(v, e, r, s, a, BUCKET, &rc)) return rc;
        if (blocks_use(s)) return 1;
        a->press[PK_USE] = 1;
        b->bk.uses += 1;
        b->phase = 1;
        b->waited = 0;
        b->gain = e->age;
        if (lava)
        {
            b->lx = s->lq_x, b->ly = s->lq_y, b->lz = s->lq_z;   /* the site goes beside the lava fetched */
        }
        return 0;
    }
    int tm = 0, tb = bp_blk(v, e, b->tx, b->ty, b->tz, &tm);
    int tsrc = tm == 0 && (tb == liq || tb == liq + 1);
    if (!tsrc && tb >= 0)
    {
        /* the target no longer a source (taken, or flowing now): forgotten, and the nearest other source remembered
         * made the target at once (the sweep from beside the frame stood 160 steps a cell in a cave, the lake out of
         * its view) */
        for (int i = 0; i < b->nmem[lava] && i < BP_MEM; ++i)
            if (b->mem[lava][i].x == b->tx && b->mem[lava][i].y == b->ty && b->mem[lava][i].z == b->tz)
                b->mem[lava][i].y = -1000;
        int m = bp_mem_pick(e, lava);
        if (m >= 0)
        {
            b->tx = b->mem[lava][m].x, b->ty = b->mem[lava][m].y, b->tz = b->mem[lava][m].z;
            b->best = 1e9;
            b->gain = e->age;
            b->held = 0;
            b->at = e->age, b->fd0set = 0;      /* lane/obsidian4: a new trip (the next pool, any distance) */
            return 0;
        }
    }
    if (tsrc && d <= BP_REACH - 0.2)
    {
        /* the target a source within reach: the look on its top */
        double yaw, pitch;
        look_at(r->x, ey, r->z, b->tx + 0.5, b->ty + 0.9, b->tz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        a->hold = 1u << PK_SNEAK;
        if (++b->held > 8)
        {
            /* the ray lands elsewhere from here: another source sought (this cell skipped a while) */
            b->skx = b->tx, b->sky = b->ty, b->skz = b->tz, b->skuntil = e->steps + 60;
            tsrc = 0;
            b->held = 0;
        }
        else
            return 0;
    }
    if (bp_walk(v, e, r, s, a, ey, px, py, pz, 1.5) || b->stuck > 40 || (!tsrc && d < 4.0))
    {
        /* there and no source under the ray (a pool's near sources taken: flowing lava fills them, and the ray passes
         * it): the look swept round the feet, low then lower, and the nearest source the sweep shows made the target */
        int k = (e->age / BP_HOLD) % 16;
        a->look_mode = 1;
        a->look[0] = r->yaw + (e->age % BP_HOLD == 0 ? 45.0f : 0.0f);
        a->look[1] = k < 8 ? 35.0f : 60.0f;
        a->hold = 1u << PK_SNEAK;       /* lane/obsidian4: crouched at the pool's edge (0 before) */
        if (e->age % BP_HOLD == BP_HOLD - 1)
        {
            struct obsview_hit h[OV_KINDS];
            int kind = lava ? OV_LAVA : OV_WATER;
            if (obsview_scan(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, 16, 1 << kind | OV_EXPOSED, bp_skip,
                             e, h) == 0 &&
                h[kind].d > 0.0f && !bp_bad(e, h[kind].x, h[kind].y, h[kind].z) &&
                (h[kind].x != b->tx || h[kind].y != b->ty || h[kind].z != b->tz))
            {
                b->tx = h[kind].x, b->ty = h[kind].y, b->tz = h[kind].z;
                b->best = 1e9;
                b->gain = e->age;
                bp_mem_add(b, lava, h[kind].x, h[kind].y, h[kind].z);
            }
        }
    }
    else if (r->on_ground && d > 3.0 && b->stuck < 5)
    {
        /* the look along the walk; the ray low enough to land on the pool on the way (lane/obsidian6: not while the
         * walk digs a wall, stuck 5: the pitch put here moved its aim off the cell, and the trip stood at the wall
         * until fill-stuck; seed 393's one-bucket kit lost 4 of 7 lava trips so) */
        a->look[1] = (float)(lava ? 35.0 : 30.0);
    }
    return 0;
}

/* lane/obsidian6: BM_DUMP, 1 over: the one bucket's lava poured back into the remembered pool (tx, ty, tz) when water
 * is wanted and no bucket is empty. The pool walked to as a fill's (bp_walk, crouched, never into lava's reach), the
 * look on the source cell low (the full bucket's ray, like the crosshair, passes the lava: it lands on the pool's floor
 * or a shore face), the bucket used only when the cell beside that face is lava or the source's own column (under it) */
static int bp_dump(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    if (inv_count(s, LAVA_BUCKET) == 0)
    {
        b->bk.dumps += 1;
        return 1;
    }
    double px = b->tx + 0.5, py = b->ty + 0.5, pz = b->tz + 0.5;
    double d = py_dist3(r->x, ey, r->z, px, py, pz);
    if (!b->fd0set) b->fd0 = d, b->fd0set = 1;
    if (e->age - b->at > BP_FILL_CAP + BP_FILL_PER * b->fd0)
    {
        bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
        return 1;
    }
    if (b->phase > 0)
    {
        /* a use sent: its answer */
        if (++b->waited < BP_WAIT) return 0;
        b->phase = 0;
        if (++b->uses >= BP_USES)
        {
            bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
            return 1;
        }
    }
    if (d > BP_REACH - 0.2)
    {
        b->held = 0;
        bp_walk(v, e, r, s, a, ey, px, py, pz, 1.5);
        if (b->stuck > 40)
        {
            bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
            return 1;
        }
        return 0;
    }
    double yaw, pitch;
    look_at(r->x, ey, r->z, px, b->ty + 0.3, pz, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    a->hold = 1u << PK_SNEAK;
    int rc = 0;
    if (!bp_hold(v, e, r, s, a, LAVA_BUCKET, &rc)) return rc;
    int sd = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
    int cx = s->mo_x + BP_OX[sd], cy = s->mo_y + BP_OY[sd], cz = s->mo_z + BP_OZ[sd];
    int ok = s->mo_hit && (ob_lava(bp_blk(v, e, cx, cy, cz, NULL)) || (cx == b->tx && cz == b->tz && cy <= b->ty)) &&
             py_dist3(r->x, ey, r->z, cx + 0.5, cy + 0.5, cz + 0.5) <= BP_REACH && !blocks_use(s) &&
             r->screen == PS_NONE;
    if (ok && ++b->held > BP_SETTLE)
    {
        a->look[0] = r->yaw;
        a->look[1] = r->pitch;
        a->press[PK_USE] = 1;
        b->bk.uses += 1;
        b->phase = 1;
        b->waited = 0;
        b->held = 0;
        return 0;
    }
    if (++b->stuck > BP_DUMP_HOLD)
    {
        bp_mark_bad(e, b->tx, b->ty, b->tz, BP_FILL_REST);
        return 1;
    }
    return 0;
}

/* a cast's W chosen (wf 9): the cell beside T outside the slot (r outward), in front of T or behind it, whichever is
 * free (no solid block, no liquid) over a solid one */
static int bp_wpick(const struct neth_vec *v, const struct vec_env *e, const struct bop *op, int one, int skip, int *wr,
                    int *wh, int *wf)
{
    /* lane/obsidian6: skip, the acceptable candidates passed over (their pour found no aim: one bucket only) */
    const struct bp_env *b = &e->bp;
    int out = op->r < 0 ? -1 : 1;
    /* in front first: the cell cast beside the outside one hides it from the work position at its pick-up */
    int cand[3][3] = {{op->r, op->h, 0}, {op->r + out, op->h, 1}, {op->r, op->h, 2}};
    /* lane/obsidian6: one bucket, behind first: W stands through the lava trip, and from in front its flow runs to the
     * inside's drop across the anchor row, under the work position (the arena's first ground cast lost its site so) */
    static const int ONE[3] = {2, 1, 0};
    for (int i = 0; i < 3; ++i)
    {
        int k = one ? ONE[i] : i;
        int x, y, z;
        bp_cell(b, cand[k][0], cand[k][1], cand[k][2], &x, &y, &z);
        int c = bp_blk(v, e, x, y, z, NULL), u = bp_blk(v, e, x, y - 1, z, NULL);
        if (!ob_solid(c) && !ob_lava(c) && !bp_water(c) && ob_solid(u) && skip-- <= 0)
        {
            *wr = cand[k][0], *wh = cand[k][1], *wf = cand[k][2];
            return 1;
        }
    }
    if (one && skip <= 0)
    {
        /* lane/obsidian6: one bucket, none free (hilly ground: all three solid, the site given up at its column's top
         * cast 3 times in one idle env): W over T (water above a lava source mixes too: BlockLiquid.func_149805_n's
         * y + 1), poured on a solid side of it */
        int x, y, z;
        bp_cell(b, op->r, op->h + 1, 1, &x, &y, &z);
        int c = bp_blk(v, e, x, y, z, NULL), side = 0;
        for (int k = 2; k < 6; ++k) side |= ob_solid(bp_blk(v, e, x + BP_OX[k], y, z + BP_OZ[k], NULL));
        if (!ob_solid(c) && !ob_lava(c) && !bp_water(c) && side)
        {
            *wr = op->r, *wh = op->h + 1, *wf = 1;
            return 1;
        }
    }
    return 0;
}

/* the slot's walls in view after the dig: every cast's T and W with a solid face to pour on, the inside's cells and
 * the slot's free of solid blocks, no lava beside any cell */
static int bp_check(const struct neth_vec *v, const struct vec_env *e)
{
    const struct bp_env *b = &e->bp;
    int x, y, z;
    for (int rr = -1; rr <= 2; ++rr)
        for (int h = -3; h <= 1; ++h)
        {
            if (h == -3 && (rr == -1 || rr == 2)) continue;
            bp_cell(b, rr, h, 1, &x, &y, &z);
            if (ob_solid(bp_blk(v, e, x, y, z, NULL))) return 0;
            for (int k = 0; k < 6; ++k)
            {
                int c = bp_blk(v, e, x + BP_OX[k], y + BP_OY[k], z + BP_OZ[k], NULL);
                if (c < 0 || ob_lava(c)) return 0;
            }
        }
    /* the bottom corners stand under the columns (each column's first cell is poured on any face, but the corner
     * keeps the inside's water off the outside) */
    for (int rr = -1; rr <= 2; rr += 3)
    {
        bp_cell(b, rr, -3, 1, &x, &y, &z);
        if (!ob_solid(bp_blk(v, e, x, y, z, NULL))) return 0;
    }
    /* the cells under the bottom row and the back wall behind the slot's lower cells: the pours' faces */
    for (int rr = 0; rr <= 1; ++rr)
    {
        bp_cell(b, rr, -4, 1, &x, &y, &z);
        int c = bp_blk(v, e, x, y, z, NULL);
        bp_cell(b, rr, -3, 2, &x, &y, &z);
        if (!ob_solid(c) && !ob_solid(bp_blk(v, e, x, y, z, NULL))) return 0;
        for (int h = -2; h <= -1; ++h)
        {
            bp_cell(b, rr, h, 2, &x, &y, &z);
            if (!ob_solid(bp_blk(v, e, x, y, z, NULL))) return 0;    /* W's face */
        }
    }
    return 1;
}

/* the site chosen at the feet (its direction away from the lava fetched, else either side of that); 1 chosen */
static int bp_site(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct bp_env *b = &e->bp;
    double dx = r->x - (b->lx + 0.5), dz = r->z - (b->lz + 0.5);
    int k = fabs(dx) >= fabs(dz) ? (dx > 0 ? 3 : 1) : (dz > 0 ? 0 : 2);   /* yaw 0, 90, 180, 270: +z, -x, -z, +x */
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    int order[3] = {k, (k + 1) & 3, (k + 3) & 3};
    for (int i = 0; i < 3; ++i)
        if (bp_site_ok(v, e, r, DX[order[i]], DZ[order[i]], i == 0))
        {
            b->ax = (int)floor(r->x), b->ay = (int)floor(r->y + 0.01), b->az = (int)floor(r->z);
            b->fx = DX[order[i]], b->fz = DZ[order[i]], b->rx = -b->fz, b->rz = b->fx;
            b->site = 1;
            b->step = 0;
            b->sub = 0;
            b->sat = e->age;
            b->aim = b->held = b->tries = b->waited = 0;
            b->bk.sites += 1;
            b->site_tries = b->site_casts = 0;
            b->site_lava_at = e->steps;
            b->bwalk = 0;
            return 1;
        }
    return 0;
}

static void bp_next(struct vec_env *e)
{
    struct bp_env *b = &e->bp;
    b->step += 1;
    b->sub = 0;
    b->sat = e->age;
    b->aim = b->held = b->tries = b->waited = b->uses = b->wset = 0;
    b->here = 0;
    b->wskip = 0;
}

/* the last POS step at or before step i: its work position */
static void bp_workpos(int i, double *tr, double *tf)
{
    *tr = 1.0, *tf = 0.6;
    for (int j = i; j >= 0; --j)
        if (BP_PLAN[j].kind == BO_POS)
        {
            *tr = BP_PLAN[j].r / 100.0, *tf = BP_PLAN[j].f / 100.0;
            return;
        }
}

/* the build's step (mode BM_BUILD): 1 over */
static int bp_build(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    if (b->step >= BP_NPLAN) return 1;
    const struct bop *op = &BP_PLAN[b->step];
    if (b->sub < 0)
    {
        /* lane/obsidian4: the walk back from a far pool: the step's clock held while it gains a block in
         * BP_BACK_GAIN steps (the cap counts from where it arrives) */
        double tr, tf, lr, lf;
        bp_workpos(b->step, &tr, &tf);
        bp_local(b, r, &lr, &lf);
        double bd = py_hypot2(lr - tr, lf - tf);
        if (!b->bwalk) b->bwalk = 1, b->back_best = 1e9, b->back_gain = e->age, b->bk.back_walks += bd > 3.0;
        if (bd < b->back_best - 1.0) b->back_best = bd, b->back_gain = e->age;
        if (bd > 1.6)
        {
            /* lane/obsidian6: one bucket, a cell cast here: twice the patience (an idle env lost a site of 9 cells
             * on the walk back from its last lava trip) */
            int gain = bp_one(v, s) && b->site_casts > 0 ? BP_ONE_BACK * BP_BACK_GAIN : BP_BACK_GAIN;
            if (e->age - b->back_gain > gain) return bp_abort(v, e, r, BF_POS);
            b->sat = e->age;
            b->held = 0;
        }
    }
    else b->bwalk = 0;
    if (e->age - b->sat > BP_STEP_CAP) return bp_abort(v, e, r, op->kind == BO_POS ? BF_POS : BF_AIM);
    int x, y, z;
    if (b->sub < 0)
    {
        /* back to the step's work position (after a trip, a resume or a drift), then the sub-step it left */
        double tr, tf;
        bp_workpos(b->step, &tr, &tf);
        /* (a water flow round the feet can hold the player off it: after 60 steps it works from where it stands) */
        if (bp_goto(v, e, r, s, a, ey, tr, tf)) b->sub = b->resub, b->aim = b->held = 0;
        else if (++b->held > 60)
        {
            b->sub = b->resub, b->aim = b->held = 0;
            /* lane/obsidian6: one bucket: the step stays where it stands (W's flow, parked through the lava trip,
             * held the player off: the step went back and forth to its cap) */
            if (bp_one(v, s)) b->here = 1;
        }
        return 0;
    }
    if ((op->kind == BO_PLACE || op->kind == BO_LIGHT || (op->kind == BO_CAST && b->sub <= 2)) && b->waited == 0 &&
        b->tries == 0 && b->aim == 0 && b->held == 0 && !b->here)
    {
        /* pushed off the work position (water flowing round the feet): back to it before the next aim */
        double tr, tf, lr, lf;
        bp_workpos(b->step, &tr, &tf);
        bp_local(b, r, &lr, &lf);
        if (fabs(lr - tr) > 0.25 || fabs(lf - tf) > 0.2)
        {
            b->resub = b->sub;
            b->sub = -1;
            return 0;
        }
    }
    int cast_before = 0;
    for (int j = 0; j < b->step && op->kind == BO_DIG && !cast_before; ++j) cast_before = BP_PLAN[j].kind == BO_CAST;
    if (cast_before && !b->here && bp_one(v, s))
    {
        /* lane/obsidian6: one bucket, a dig after the casts (the top row's blocks, the front): the last W's flow, still
         * draining, pushed the player off it mid-dig (3 blocks: the dig ran out of steps out of reach), so back to the
         * work position whenever it drifts */
        double tr, tf, lr, lf;
        bp_workpos(b->step, &tr, &tf);
        bp_local(b, r, &lr, &lf);
        if (fabs(lr - tr) > BP_DRIFT || fabs(lf - tf) > BP_DRIFT)
        {
            b->resub = 0;
            b->sub = -1;
            b->aim = b->held = 0;
            return 0;
        }
    }
    switch (op->kind)
    {
    case BO_POS:
        if (bp_goto(v, e, r, s, a, ey, op->r / 100.0, op->f / 100.0)) bp_next(e);
        return 0;
    case BO_CHECK:
        if (!bp_check(v, e)) return bp_abort(v, e, r, BF_CHECK);
        bp_next(e);
        return 0;
    case BO_DIG:
    {
        bp_cell(b, op->r, op->h, op->f, &x, &y, &z);
        int c = bp_blk(v, e, x, y, z, NULL);
        if (ob_lava(c)) return bp_abort(v, e, r, BF_DIG);
        if (!ob_solid(c))
        {
            /* dug (or free): lava now beside it ends the site */
            for (int k = 0; k < 6; ++k)
                if (ob_lava(bp_blk(v, e, x + BP_OX[k], y + BP_OY[k], z + BP_OZ[k], NULL))) return bp_abort(v, e, r, BF_DIG);
            bp_next(e);
            return 0;
        }
        if (c == 7 || c == OBSIDIAN || e->age - b->sat > BP_DIG_CAP)
        {
#ifdef BP_TRACE
            fprintf(stderr, "BPT %d dig-fail cell %d %d %d block %d age %d\n", (int)(e - v->e), x, y, z, c, e->age - b->sat);
#endif
            return bp_abort(v, e, r, BF_DIG);
        }
        /* the face toward the eye: the cell's centre, then its faces' */
        static const double F[4][3] = {{0.5, 0.5, 0.5}, {0.5, 0.95, 0.5}, {0.5, 0.5, 0.5}, {0.5, 0.9, 0.5}};
        /* the aim kept while the crosshair is on the cell (a dig's progress is lost when it leaves), the next one after
         * 6 steps off it */
        int k = b->aim % 4;
        double yaw, pitch;
        double qx = x + F[k][0], qy = y + F[k][1], qz = z + F[k][2];
        if (k == 2) qx -= 0.45 * b->fx, qz -= 0.45 * b->fz;     /* the face toward the anchor row */
        look_at(r->x, ey, r->z, qx, qy, qz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (++b->held > 6) b->aim += 1, b->held = 0;
        if (s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z)
        {
            a->hold = 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, c);
            if (best >= 0 && best != s->current) a->hotbar = best;
            b->held = 0;
        }
        else if (s->mo_hit && bp_plant(s->mo_block) &&
                 py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) < 4.5)
        {
            /* lane/obsidian6: a plant in the line (tall grass on the cell under it: the slot's dig took its cell for
             * free and left it, and the crosshair met the grass for the dig's 200 steps), broken at one hit */
            a->hold = 1u << PK_ATTACK;
        }
        else if (s->mo_hit && b->aim >= 4 && ob_solid(s->mo_block) && s->mo_block != 7 && s->mo_block != OBSIDIAN &&
                 py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) < 4.0)
        {
            /* a block in the line: a cell of the slot (its plane, r -2..3, h 1 down) dug out of the way */
            double clf = (s->mo_x + 0.5 - (b->ax + 0.5)) * b->fx + (s->mo_z + 0.5 - (b->az + 0.5)) * b->fz;
            if (fabs(clf - 1.0) < 0.1 && s->mo_y - b->ay <= 1 && s->mo_y - b->ay >= -3)
            {
                a->hold = 1u << PK_ATTACK;
                int best = best_tool(&v->T, s, s->mo_block);
                if (best >= 0 && best != s->current) a->hotbar = best;
            }
        }
        return 0;
    }
    case BO_PLACE:
    {
        bp_cell(b, op->r, op->h, op->f, &x, &y, &z);
        int c = bp_blk(v, e, x, y, z, NULL);
        if (ob_solid(c))
        {
            bp_next(e);
            return 0;
        }
        int item = inv_count(s, COBBLE) > 0 ? COBBLE : 3;
        if (b->waited > 0)
        {
            if (++b->waited < BP_WAIT) return 0;
            b->waited = 0;
            if (++b->uses >= BP_USES) return bp_abort(v, e, r, BF_POUR);
            return 0;
        }
        if (bp_blocks(s) == 0) return bp_abort(v, e, r, BF_POUR);
        int u = bp_aim_use(v, e, r, s, a, ey, BA_PL, item, x, y, z, 0);
        if (u < 0) return bp_abort(v, e, r, BF_AIM);
        if (u > 0) b->waited = 1;
        return 0;
    }
    case BO_LIGHT:
    {
        int ix, iy, iz;
        bp_cell(b, 0, -1, 1, &ix, &iy, &iz);
        int p0 = bp_blk(v, e, ix, iy, iz, NULL);
        bp_cell(b, 1, -2, 1, &ix, &iy, &iz);
        if (p0 == 90 || bp_blk(v, e, ix, iy, iz, NULL) == 90)
        {
            if (b->bk.lit_step < 0) b->bk.lit_step = e->steps;
            b->bk.lit += 1;
            BPT(e, r, "lit");
            bp_next(e);
            return 0;
        }
        if (b->waited > 0)
        {
            if (++b->waited < BP_WAIT) return 0;
            b->waited = 0;
            if (++b->uses >= BP_USES) return bp_abort(v, e, r, BF_LIGHT);
            return 0;
        }
        int u = bp_aim_use(v, e, r, s, a, ey, BA_LT, FLINT_STEEL, 0, 0, 0, 0);
        if (u < 0) return bp_abort(v, e, r, BF_LIGHT);
        if (u > 0) b->waited = 1;
        return 0;
    }
    case BO_ENTER:
    {
        double lr, lf;
        bp_local(b, r, &lr, &lf);
        a->look_mode = 1;
        a->look[0] = bp_yaw(b);
        a->look[1] = 20.0f;
        if (e->age - b->sat > 500) return bp_abort(v, e, r, BF_LIGHT);
        if (nt_entry_fill(v, e, r, s, a)) return 0;     /* lane/rod (nether_entry_fill, nether.c) */
        if (lr > 0.5 && lf < 1.0) a->hold = 1u << PK_LEFT;
        else a->hold = 1u << PK_FORWARD;
        return 0;
    }
    case BO_CAST:
    {
        int tx, ty, tz, wx, wy, wz;
        bp_cell(b, op->r, op->h, op->f, &tx, &ty, &tz);
        int wr = op->wr, wh = op->wh, wf = op->wf;
        if (wf == 9)
        {
            /* chosen once a step (wset, cleared by bp_next) */
            if (!b->wset)
            {
                if (!bp_wpick(v, e, op, bp_one(v, s), b->wskip, &wr, &wh, &wf)) return bp_abort(v, e, r, BF_CHECK);
                b->wr = wr, b->wh = wh, b->wf = wf, b->wset = 1;
            }
            wr = b->wr, wh = b->wh, wf = b->wf;
        }
        bp_cell(b, wr, wh, wf, &wx, &wy, &wz);
        int tb = bp_blk(v, e, tx, ty, tz, NULL), wm = 0, wb = bp_blk(v, e, wx, wy, wz, &wm);
        if (b->sub == 0)
        {
            /* the supplies: a lava bucket and a water bucket (or W already a source) */
            if (tb == OBSIDIAN)
            {
                b->sub = 3;
                return 0;
            }
            int water_ok = inv_count(s, WATER_BUCKET) > 0 || (bp_water(wb) && wm == 0);
            int one = bp_one(v, s), w_src = bp_water(wb) && wm == 0;
            if (one && inv_count(s, LAVA_BUCKET) == 0 && !w_src && inv_count(s, WATER_BUCKET) > 0)
            {
                /* lane/obsidian6: one bucket: W poured first (sub 1), the bucket emptied for the lava; sub 2 sends the
                 * trip from here, W left standing beside T while the lava is fetched */
                b->bk.parks += 1;
                b->sub = 1;
                b->aim = b->held = b->tries = b->waited = b->uses = 0;
                return 0;
            }
            if (inv_count(s, LAVA_BUCKET) == 0 || !water_ok)
            {
                if (inv_count(s, BUCKET) == 0)
                {
                    /* lane/obsidian6: one bucket, full of lava, and W gone: the lava poured back into a pool, then the
                     * water fetched */
                    int m = one && inv_count(s, LAVA_BUCKET) > 0 ? bp_mem_pick(e, 1) : -1;
                    if (m < 0) return bp_abort(v, e, r, BF_TRIP);
                    b->mode = BM_DUMP;
                    b->tx = b->mem[1][m].x, b->ty = b->mem[1][m].y, b->tz = b->mem[1][m].z;
                    b->phase = b->uses = b->waited = b->held = 0;
                    b->at = b->gain = e->age;
                    b->stuck = 0;
                    b->fd0set = 0;
                    return 0;
                }
                /* lane/obsidian6: one bucket, the water first (W a source, then the lava) */
                int k = one ? (inv_count(s, LAVA_BUCKET) == 0 && water_ok ? 1 : 0) : inv_count(s, LAVA_BUCKET) == 0 ? 1 : 0;
                int m = bp_mem_pick(e, k);
                if (m < 0) return bp_abort(v, e, r, BF_TRIP);
                b->mode = k ? BM_LAVA : BM_WATER;
                b->tx = b->mem[k][m].x, b->ty = b->mem[k][m].y, b->tz = b->mem[k][m].z;
                b->before = (int)inv_count(s, k ? LAVA_BUCKET : WATER_BUCKET);
                b->phase = b->uses = b->waited = b->held = 0;
                b->at = b->gain = e->age;
                b->best = 1e9;
                b->stuck = 0;
                b->fd0set = 0;
                if (k) b->bk.trips_lava += 1;
                else b->bk.trips_water += 1;
                BPT(e, r, k ? "trip-lava" : "trip-water");
                return 0;
            }
            b->sub = bp_water(wb) && wm == 0 ? 2 : 1;
            b->aim = b->held = b->tries = b->waited = b->uses = 0;
            return 0;
        }
        if (b->sub == 1)
        {
            /* W: the water poured */
            if (b->waited > 0)
            {
                if (bp_water(wb) && wm == 0)
                {
                    b->sub = 2, b->aim = b->held = b->tries = b->waited = b->uses = 0;
                    return 0;
                }
                if (++b->waited < BP_WAIT) return 0;
                b->waited = 0;
                if (++b->uses >= BP_USES) return bp_abort(v, e, r, BF_POUR);
            }
            int u = bp_aim_use(v, e, r, s, a, ey, BA_PL, WATER_BUCKET, wx, wy, wz, 0);
            if (u < 0 && op->wf == 9 && bp_one(v, s) && b->wskip < BP_WSKIP)
            {
                /* lane/obsidian6: one bucket: W's pour found no aim (behind the column, hidden on hilly ground: an idle
                 * env lost two sites at that cast so): the next candidate */
                b->wskip += 1;
                b->wset = 0;
                b->sub = 0;
                b->aim = b->held = b->tries = b->waited = b->uses = 0;
                return 0;
            }
            if (u < 0) return bp_abort(v, e, r, BF_AIM);
            if (u > 0) b->waited = 1;
            return 0;
        }
        if (b->sub == 2)
        {
            /* T: the lava poured beside the water */
            if (tb == OBSIDIAN)
            {
                b->bk.casts += 1;
                if (bp_one(v, s)) b->bk.one_casts += 1;
                if (++b->site_casts > b->bk.best_casts) b->bk.best_casts = b->site_casts;
                b->sub = 3, b->aim = b->held = b->tries = b->waited = b->uses = 0;
                BPT(e, r, "cast");
                return 0;
            }
            if (ob_lava(tb) && b->waited > 2)
            {
                /* lava, not obsidian: taken back with the empty bucket (no water beside it after all) */
                b->sub = 4, b->aim = b->held = b->tries = b->waited = b->uses = 0;
                BPT(e, r, "lava-stood");
                return 0;
            }
            if (b->waited > 0)
            {
                if (++b->waited < BP_WAIT) return 0;
                b->waited = 0;
                if (++b->uses >= BP_USES) return bp_abort(v, e, r, BF_POUR);
            }
            if (!(bp_water(wb) && wm == 0))
            {
                b->sub = 0;                 /* W gone: poured again */
                return 0;
            }
            if (inv_count(s, LAVA_BUCKET) == 0 && bp_one(v, s))
            {
                /* lane/obsidian6: one bucket, W poured: the lava's trip (sub 0) */
                b->sub = 0;
                return 0;
            }
            int u = bp_aim_use(v, e, r, s, a, ey, BA_PL, LAVA_BUCKET, tx, ty, tz, 0);
            if (u < 0) return bp_abort(v, e, r, BF_AIM);
            if (u > 0) b->waited = 1;
            return 0;
        }
        if (b->sub == 3)
        {
            /* W picked back up */
            if (!(bp_water(wb) && wm == 0) || inv_count(s, BUCKET) == 0)
            {
                bp_next(e);
                return 0;
            }
            if (b->waited > 0)
            {
                if (++b->waited < BP_WAIT) return 0;
                b->waited = 0;
                if (++b->uses >= BP_USES)
                {
                    bp_next(e);             /* left: the next pour fetches water if none is held */
                    return 0;
                }
            }
            int u = bp_aim_use(v, e, r, s, a, ey, BA_TK, BUCKET, wx, wy, wz, 8);
            if (u < 0)
            {
                bp_next(e);
                return 0;
            }
            if (u > 0) b->waited = 1;
            return 0;
        }
        /* sub 4: lava in T taken back */
        if (!ob_lava(tb)) return bp_abort(v, e, r, BF_POUR);
        if (b->waited > 0)
        {
            if (++b->waited < BP_WAIT) return 0;
            b->waited = 0;
            if (++b->uses >= BP_USES) return bp_abort(v, e, r, BF_POUR);
        }
        int u = bp_aim_use(v, e, r, s, a, ey, BA_TK, BUCKET, tx, ty, tz, 10);
        if (u < 0) return bp_abort(v, e, r, BF_POUR);
        if (u > 0) b->waited = 1;
        return 0;
    }
    }
    return 1;
}

/* lane/obsidian4: a held site's plan has a cell still to cast (lava still wanted for it) */
static int bp_site_needs_lava(const struct neth_vec *v, const struct vec_env *e)
{
    const struct bp_env *b = &e->bp;
    if (!b->site) return 0;
    for (int i = b->step; i < BP_NPLAN; ++i)
    {
        if (BP_PLAN[i].kind != BO_CAST) continue;
        int x, y, z;
        bp_cell(b, BP_PLAN[i].r, BP_PLAN[i].h, BP_PLAN[i].f, &x, &y, &z);
        if (bp_blk(v, e, x, y, z, NULL) != OBSIDIAN) return 1;
    }
    return 0;
}

/* lane/obsidian5: the walk over the surface may run: bportal_roam not off, its steps not spent, and with bportal_night
 * strip not at night (the day's time the sky shows, vec_last.wtime: lane/pearl's reading) */
static int bp_roam_on(const struct neth_vec *v, const struct vec_env *e)
{
    if (MOPT(MO_BP_ROAM) == BR_OFF || e->bp.rm_used >= BP_ROAM_CAP) return 0;
    int64_t t = ((e->last.wtime % 24000) + 24000) % 24000;
    return !(MOPT(MO_BP_NIGHT) == BN_STRIP && t >= BP_NIGHT0 && t < BP_NIGHT1);
}

/* lane/obsidian4: the search's want: the bucket portal wants lava (an empty bucket held, no lava bucket; a held site
 * with a cell to cast, or none yet) and has none to go to (no lava remembered that is not given up), a pickaxe of
 * BP_SEARCH_TIER or better held. obsidian.c's ob_search enters the search's strip on it */
static int bp_pick_tier(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    return tier;
}

static int bp_lava_search(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    struct bp_env *b = &e->bp;
    if (!bp_want(v, e, r, s) || r->screen != PS_NONE) return 0;
    /* lane/obsidian6: with one bucket, also while it holds water (the site's water, the lava searched for) */
    if (inv_count(s, LAVA_BUCKET) > 0 ||
        (inv_count(s, BUCKET) == 0 && !(bp_one(v, s) && inv_count(s, WATER_BUCKET) > 0)))
        return 0;
    if (b->site && !bp_site_needs_lava(v, e)) return 0;
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    if (tier < BP_SEARCH_TIER) return 0;
    if (b->site && e->steps - b->site_lava_at > BP_SITE_KEEP)
    {
        /* no lava for this long: the site given up (its cells cast stay) */
        b->bk.site_lost += 1;
        b->site = 0;
        b->mode = BM_IDLE;
    }
    /* lane/obsidian5: the walk over the surface first, while its steps last (and by day, bportal_night strip) */
    return bp_mem_pick(e, 1) < 0 && !bp_roam_on(v, e);
}

/* lane/obsidian5: the fill bp_start would take: 0 water (an empty bucket, no water bucket), 1 lava, -1 none
 * (lane/obsidian6: one bucket holding water: lava, the water carried to the site) */
static int bp_fill_kind(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int water = inv_count(s, WATER_BUCKET) > 0, lava = inv_count(s, LAVA_BUCKET) > 0, empty = inv_count(s, BUCKET) > 0;
    if (!water && empty) return 0;
    if (!lava && empty) return 1;
    if (water && !lava && bp_one(v, s)) return 1;
    return -1;
}

/* lane/obsidian5: the walk's want: bportal_roam surface, its steps not spent, not resting, and the fill wanted has
 * no source remembered (lava: and a held site still has a cell to cast) */
static int bp_roam_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    const struct bp_env *b = &e->bp;
    if (!bp_roam_on(v, e) || e->steps < b->rm_off) return 0;
    int k = bp_fill_kind(v, s);
    if (k < 0 || bp_mem_pick(e, k) >= 0) return 0;
    if (k == 1 && b->site && !bp_site_needs_lava(v, e)) return 0;
    return 1;
}

/* lane/obsidian5: the head under the sky: its cell's stored sky light (what F3's sl shows) at BP_ROAM_SKY or more */
static int bp_sky(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    int sl = obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z));
    return sl >= BP_ROAM_SKY;
}

static int bp_skip_far(void *ctx, int kind, int x, int y, int z)
{
    const struct vec_env *e = ctx;
    return kind == OV_LAVA && bp_bad(e, x, y, z);
}

/* lane/obsidian5: the pillar's block (cobblestone, else dirt), -1 none to spare (BP_PILLAR_KEEP kept for the frame) */
static int bp_pillar_item(const struct neth_rl_state *s)
{
    if (inv_count(s, COBBLE) >= BP_PILLAR_H + BP_PILLAR_KEEP) return COBBLE;
    if (inv_count(s, 3) >= BP_PILLAR_H + BP_PILLAR_KEEP) return 3;
    return -1;
}

/* a pillar here: blocks to spare, solid ground under the feet and BP_PILLAR_H + 2 cells of air over the head */
static int bp_pillar_ok(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s)
{
    if (bp_pillar_item(s) < 0 || !r->on_ground) return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (!ob_solid(bp_blk(v, e, fx, fy - 1, fz, NULL))) return 0;
    for (int dy = 0; dy < BP_PILLAR_H + 3; ++dy)
        if (bp_blk(v, e, fx, fy + dy, fz, NULL) != 0) return 0;
    return 1;
}

/* the rise: the look straight down, a jump, and the block put on the floor's top face once the feet are a block over
 * it. 1 at the top, -1 given up (BP_PILLAR_CAP), 0 rising */
static int bp_pillar_up(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a)
{
    struct bp_env *b = &e->bp;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (r->on_ground) b->rm_stand = fy;
    if (r->on_ground && fy >= b->rm_py + BP_PILLAR_H) return 1;
    if (e->steps - b->rm_pat > BP_PILLAR_CAP) return -1;
    a->look_mode = 1;
    a->look[0] = r->yaw;
    a->look[1] = 90.0f;
    int item = bp_pillar_item(s), hb, rc;
    if (item < 0 && inv_count(s, COBBLE) + inv_count(s, 3) > 0) item = inv_count(s, COBBLE) > 0 ? COBBLE : 3;
    if (item < 0) return -1;
    if (!pp_select(v, e, r, s, a, item, &hb, &rc)) return rc ? -1 : 0;
    if (r->on_ground)
    {
        a->hold = 1u << PK_JUMP;
        return 0;
    }
    if (r->y >= b->rm_stand + 1.0 && s->mo_hit && s->mo_side == 1 && s->mo_x == fx && s->mo_z == fz &&
        s->mo_y == b->rm_stand - 1)
        a->press[PK_USE] = 1;
    return 0;
}

/* the way down: the block under the feet dug (the best tool), the drop picked up as the feet land on the next. 1 at
 * the base (or given up) */
static int bp_pillar_down(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a)
{
    struct bp_env *b = &e->bp;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if ((fy <= b->rm_py && r->on_ground) || e->steps - b->rm_pat > BP_PILLAR_CAP) return 1;
    a->look_mode = 1;
    a->look[0] = r->yaw;
    a->look[1] = 90.0f;
    if (r->on_ground && s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz)
    {
        a->hold = 1u << PK_ATTACK;
        int best = best_tool(&v->T, s, s->mo_block);
        if (best >= 0 && best != s->current) a->hotbar = best;
    }
    return 0;
}

/* lane/obsidian5: BM_ROAM's drive, 1 when over: the far look every BP_ROAM_LOOK_D blocks (BP_FAR_LOOKS yaws, a
 * surface scan each: obsview_far), else the spiral's leg walked (upright and sprinting where bp_walk stands up) */
static int bp_roam(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    if (!bp_roam_want(v, e, s))
    {
        /* what it wanted is in view (or its steps spent): down from a pillar first */
        if (b->rm_pil == 0) return 1;
        if (b->rm_pil != 3) b->rm_phase = 0, b->rm_pil = 3, b->rm_pat = e->steps;
    }
    b->rm_used += 1;
    b->bk.roam_steps += 1;
    if (!bp_sky(v, e, r))
    {
        if (++b->rm_under > BP_ROAM_UNDER) return 1;
    }
    else
        b->rm_under = 0;
    double far = py_hypot2(r->x - b->rm_cx, r->z - b->rm_cz);
    if (far > b->bk.roam_far) b->bk.roam_far = (float)far;
    if (b->rm_phase == 0 && b->rm_pil == 0 && r->on_ground &&
        py_hypot2(r->x - b->rm_kx, r->z - b->rm_kz) >= BP_ROAM_LOOK_D)
    {
        if (MOPT(MO_BP_ROAM) == BR_PILLAR && bp_pillar_ok(v, e, r, s))
        {
            b->rm_pil = 1;
            b->rm_py = b->rm_stand = (int)floor(r->y + 0.01);
            b->rm_pat = e->steps;
            b->bk.pillars += 1;
        }
        else
            b->rm_phase = 1, b->at = e->age, b->rm_kyaw = r->yaw;
    }
    if (b->rm_pil == 1)
    {
        int up = bp_pillar_up(v, e, r, s, a);
        if (up == 0) return 0;
        b->rm_pil = 2;                  /* at the top (or given up: the looks from where it got) */
        if (up > 0) b->bk.pillar_tops += 1;
        b->rm_phase = 1, b->at = e->age, b->rm_kyaw = r->yaw;
    }
    if (b->rm_pil == 3)
    {
        if (!bp_pillar_down(v, e, r, s, a)) return 0;
        b->rm_pil = 0;
    }
    if (b->rm_phase > 0)
    {
        int i = b->rm_phase - 1;
        a->hold = 1u << PK_SNEAK;
        a->look_mode = 1;
        a->look[0] = b->rm_kyaw + (360.0f / BP_FAR_LOOKS) * (float)i;
        a->look[1] = BP_FAR_PITCH;
        if (e->age - b->at < BP_HOLD) return 0;
        struct obsview_hit h[OV_KINDS];
        if (obsview_far(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, BP_FAR_R, 1 << OV_WATER | 1 << OV_LAVA,
                        bp_skip_far, e, h) == 0)
        {
#ifdef BP_RDEBUG
            fprintf(stderr, "RD %d %ld far look %d at %.1f %.1f %.1f yaw %.0f lava n %d d %.1f water n %d d %.1f\n",
                    (int)(e - v->e), (long)e->steps, i, r->x, r->y, r->z, r->yaw, h[OV_LAVA].n, h[OV_LAVA].d,
                    h[OV_WATER].n, h[OV_WATER].d);
#endif
            if (h[OV_WATER].d > 0.0f)
            {
                if (bp_mem_pick(e, 0) < 0) b->bk.roam_water += 1;
                bp_mem_add(b, 0, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z);
            }
            if (h[OV_LAVA].d > 0.0f)
            {
                b->bk.roam_lava += 1;
                b->bk.pillar_lava += b->rm_pil == 2;
                if (b->bk.roam_lava_step < 0) b->bk.roam_lava_step = e->steps, b->bk.roam_lava_d = h[OV_LAVA].d;
                bp_mem_add(b, 1, h[OV_LAVA].x, h[OV_LAVA].y, h[OV_LAVA].z);
                BPT(e, r, "roam-lava");
            }
        }
        b->at = e->age;
        if (++b->rm_phase > BP_FAR_LOOKS)
        {
            b->rm_phase = 0;
            b->rm_kx = r->x, b->rm_kz = r->z;
            b->bk.roam_looks += 1;
            if (b->rm_pil == 2) b->rm_pil = 3, b->rm_pat = e->steps;   /* the pillar dug back down */
        }
        return 0;
    }
    /* the leg: legs of BP_ROAM_LEG, BP_ROAM_LEG, 2 BP_ROAM_LEG, ... from where the last one ended, a quarter turn each */
    double yaw = b->rm_yaw0 + 90.0 * b->rm_leg, len = BP_ROAM_LEG * (double)(b->rm_leg / 2 + 1);
    double yr = yaw * M_PI / 180.0, gx = b->rm_lx - sin(yr) * len, gz = b->rm_lz + cos(yr) * len;
    double d = py_hypot2(gx - r->x, gz - r->z);
    if (d < b->rm_best - 1.0) b->rm_best = d, b->rm_gain_at = e->steps;
    b->rm_fast = 1;
    int reached = bp_walk(v, e, r, s, a, ey, gx, r->y + 1.0, gz, 3.0);
    b->rm_fast = 0;
    if (reached || e->steps - b->rm_gain_at > BP_ROAM_GAIN)
    {
        b->rm_leg += 1;
        b->bk.roam_legs += 1;
        b->rm_lx = r->x, b->rm_lz = r->z;
        b->rm_best = 1e9;
        b->rm_gain_at = e->steps;
        return 0;
    }
    if ((a->hold & (1u << PK_FORWARD)) && !(a->hold & (1u << PK_SNEAK)) && r->food > BP_ROAM_FOOD)
        a->hold |= 1u << PK_SPRINT;
    return 0;
}

/* lane/obsidian5: BM_UP's drive, 1 when over: the obsidian macro's climb (a stair at a time, turned by lava) with no
 * height to stop at, over where the head's cell is under the sky; given up (BP_UP_OFF's rest) when its turns run out */
static int bp_up(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    if (!bp_roam_want(v, e, s)) return 1;
    b->rm_used += 1;
    b->bk.up_steps += 1;
    if (bp_sky(v, e, r) && r->on_ground)
    {
        b->bk.ups_done += 1;
        e->ob.climb_to = 0;
        return 1;
    }
    e->ob.climb_to = 256;
    if (!ob_climb(v, e, r, s, a, ey)) return 0;
    e->ob.climb_to = 0;
    b->rm_off = e->steps + BP_UP_OFF;
    return 1;
}

/* lane/obsidian4: the search's scan leaves out hot lava not ahead of the strip (obsidian.c ob_hot_cell) and
 * lava sources given up */
static int bp_skip_search(void *ctx, int kind, int x, int y, int z)
{
    const struct vec_env *e = ctx;
    if (kind == OV_HOT) return !ob_hot_cell(e, x, y, z);
    if (kind == OV_LAVA) return bp_bad(e, x, y, z);
    return 0;
}

/* lane/obsidian4: ob_search's line for the bucket portal (bp_lava_search said 1): the search's strip (the obsidian
 * macro's: the feet at OB_SEARCH_Y or above, turned by hot lava ahead, reversed after a lava touch or a burn). Its look
 * every BP_SEARCH_SCAN steps: lava (and water) sources seen are remembered, and a lava source ends the strip (the
 * macro's trip goes to it); hot lava ahead fills the obsidian macro's hit, which the strip turns at. 1 the strip
 * searches this step */
static int bp_search(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    (void)s;
    struct bp_env *b = &e->bp;
    if ((int)floor(r->y + 0.01) < BP_SEARCH_FLOOR) return 0;
    int64_t hurt = e->ob.bk.lava_at > e->ob.bk.burn_at ? e->ob.bk.lava_at : e->ob.bk.burn_at;
    if (hurt > 0 && hurt > e->ob.burn_seen + 100)
    {
        e->ob.burn_seen = hurt;
        e->sdir = (e->sdir + 2) & 3;
        e->sk = -1;
    }
    if (e->steps - b->srch_scan >= BP_SEARCH_SCAN || b->srch_scan > e->steps)
    {
        b->srch_scan = e->steps;
        e->ob.scan_at = e->steps;
        if (obsview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, 16,
                         1 << OV_WATER | 1 << OV_LAVA | 1 << OV_HOT | OV_EXPOSED, bp_skip_search, e, e->ob.hit) != 0)
            memset(e->ob.hit, 0, sizeof e->ob.hit);
#ifdef BP_SDEBUG
        if (e->ob.hit[OV_LAVA].n > 0 || e->ob.hit[OV_HOT].n > 0)
            fprintf(stderr, "SL %d %ld lava n %d d %.1f hot n %d d %.1f at %.1f %.1f %.1f pitch %.0f\n", (int)(e - v->e),
                    (long)e->steps, e->ob.hit[OV_LAVA].n, e->ob.hit[OV_LAVA].d, e->ob.hit[OV_HOT].n, e->ob.hit[OV_HOT].d,
                    r->x, r->y, r->z, r->pitch);
#endif
        if (e->ob.hit[OV_WATER].d > 0.0f)
            bp_mem_add(b, 0, e->ob.hit[OV_WATER].x, e->ob.hit[OV_WATER].y, e->ob.hit[OV_WATER].z);
        if (e->ob.hit[OV_LAVA].d > 0.0f && !bp_bad(e, e->ob.hit[OV_LAVA].x, e->ob.hit[OV_LAVA].y, e->ob.hit[OV_LAVA].z))
        {
            bp_mem_add(b, 1, e->ob.hit[OV_LAVA].x, e->ob.hit[OV_LAVA].y, e->ob.hit[OV_LAVA].z);
            b->bk.search_found += 1;
            BPT(e, r, "search-found");
            return 0;
        }
    }
    if (b->srch_last != e->steps) b->srch_last = e->steps, b->bk.search_steps += 1;   /* once a step (called thrice) */
#ifdef BP_SDEBUG
    if ((int)(e - v->e) == BP_SDEBUG && e->steps % 10 == 0)
        fprintf(stderr, "SD %ld strip %d sdir %d sk %d sturns %d sc %d %d %d hot %.1f at %.1f %.1f %.1f age %d\n",
                (long)e->steps, e->strip, e->sdir, e->sk, e->sturns, e->scx, e->scy, e->scz, e->ob.hit[OV_HOT].d, r->x,
                r->y, r->z, e->age);
#endif
    e->ob.search = 1;
    return 1;
}

/* macro_start's line: 1 when the macro starts (the mine macro's kind) */
static int bp_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    struct bp_env *b = &e->bp;
    if (!bp_want(v, e, r, s) || r->screen != PS_NONE || s->eye_water || e->steps < b->off || !r->on_ground) return 0;
    if (r->food <= BP_FOOD && eat_want(v, r, s)) return 0;
    if (prov_want(v, e, r, s)) return 0;    /* lane/chain (food_oracle): the provisions before any portal (food.c) */
    {
        /* lane/obsidian4: never begun in lava or burning (the escape's): it began there and ended the next step, and
         * the books named it for a burn it had not walked into (beside lava is a fill's end: the site's walk begins) */
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        if (ob_lava(bp_blk(v, e, fx, fy, fz, NULL)) || ob_lava(bp_blk(v, e, fx, fy + 1, fz, NULL)) || r->fire) return 0;
    }
    int mode = BM_IDLE;
    /* lane/obsidian4: a held site whose next cells want lava, none held and none remembered: the search, not the
     * build (the build's trip gave up at once, the site kept: 58 such give-ups in one case of 32 envs) */
    int climb = bp_lava_search(v, e, r, s) && (int)floor(r->y + 0.01) < BP_SEARCH_FLOOR && e->steps >= b->climb_off;
    int roam = bp_roam_want(v, e, s);   /* lane/obsidian5: the walk over the surface, before the search */
    if (b->site && inv_count(s, LAVA_BUCKET) == 0 && bp_mem_pick(e, 1) < 0 && bp_site_needs_lava(v, e) && !climb &&
        !roam)
        return 0;
    if (roam)
    {
        /* under the sky: the walk (a far look first); under ground: the climb to the sky first */
        if (bp_sky(v, e, r))
        {
            mode = BM_ROAM;
            if (!b->rm_set)
            {
                /* the spiral's centre and first heading: a heading an env (the envs of one world spread out) */
                b->rm_set = 1;
                b->rm_cx = b->rm_lx = r->x, b->rm_cz = b->rm_lz = r->z;
                b->rm_yaw0 = (float)(((int)(e - v->e) * 137 + 45) % 360);
                b->rm_leg = 0;
            }
            b->rm_phase = 1;
            b->rm_kyaw = r->yaw;
            if (b->rm_pil != 0) b->rm_phase = 0, b->rm_pil = 3, b->rm_pat = e->steps;   /* down from a pillar left */
            b->rm_best = 1e9;
            b->rm_gain_at = e->steps;
            b->rm_under = 0;
        }
        else
        {
            mode = BM_UP;
            e->ob.cdir = ((int)floor(((double)r->yaw + 45.0) / 90.0) % 4 + 4) % 4;
            e->ob.turns = 0;
            e->ob.cy = -1;
            e->ob.climb_to = 256;
            b->bk.ups += 1;
        }
    }
    else if (climb)
    {
        /* lane/obsidian4: under the search's floor, lava wanted and none known: the obsidian macro's climb (a stair at
         * a time to OB_SEARCH_Y, turned by lava; ob_climb on e->ob's climb state), then the search's strip. Below it
         * the search had stopped for good (the player fell down the strip's 2x2 shaft, or the policy dug down) */
        mode = BM_CLIMB;
        e->ob.cdir = ((int)floor(((double)r->yaw + 45.0) / 90.0) % 4 + 4) % 4;
        e->ob.turns = 0;
        e->ob.cy = -1;
        e->ob.climb_to = 0;
        b->bk.climbs += 1;
    }
    else if (b->site)
    {
        mode = BM_BUILD;
        b->sub = -1;                    /* back to the work position first; a cast's sub 0 reads where it stood */
        b->resub = 0;
    }
    else
    {
        int water = inv_count(s, WATER_BUCKET) > 0, lava = inv_count(s, LAVA_BUCKET) > 0, empty = inv_count(s, BUCKET) > 0;
        int k = -1;
        if (!water && empty) k = 0;
        else if (!lava && empty) k = 1;
        int due = !b->lk_set || py_hypot2(r->x - b->lkx, r->z - b->lkz) > BP_LOOK_DIST ||
                  e->steps - b->lk_step > BP_LOOK_EVERY;
        if (k >= 0)
        {
            int m = bp_mem_pick(e, k);
            /* lane/flint (bportal_walk dig): water out of the trip's reach walked to as the strip first (obsidian.c
             * ob_search; traced, the base's rows: s42k50's straight water trips gave up in 1,224 of 1,262) */
            if (m >= 0 && k == 0 && MOPT(MO_BP_WALK) && bp_pick_tier(v, s) >= 2 &&
                ob_wk_take2(v, e, r, b->mem[0][m].x, b->mem[0][m].y, b->mem[0][m].z, OJ_FILL, 1))
                return 0;
            if (m >= 0)
            {
                mode = k ? BM_LAVA : BM_WATER;
                b->tx = b->mem[k][m].x, b->ty = b->mem[k][m].y, b->tz = b->mem[k][m].z;
                b->before = (int)inv_count(s, k ? LAVA_BUCKET : WATER_BUCKET);
                if (k) b->bk.trips_lava += 1;
                else b->bk.trips_water += 1;
            }
            else if (due)
                mode = BM_LOOK;
            else
                return 0;
        }
        else if (water && lava && bp_blocks(s) >= 2)
        {
            if ((b->lx == 0 && b->ly == 0 && b->lz == 0) || bp_bad(e, b->lx, b->ly, b->lz))
            {
                int m = bp_mem_pick(e, 1);
                if (m >= 0) b->lx = b->mem[1][m].x, b->ly = b->mem[1][m].y, b->lz = b->mem[1][m].z;
                else b->lx = b->ly = b->lz = 0;
            }
            if (b->lx == 0 && b->ly == 0 && b->lz == 0)
            {
                /* lane/obsidian4: both buckets full, no pool to go round: the rings round the feet (the lava is
                 * carried; the envs that filled a bucket and found no site by its pool held both to the end) */
                b->lx = (int)floor(r->x), b->ly = (int)floor(r->y + 0.01), b->lz = (int)floor(r->z);
                if (bp_bad(e, b->lx, b->ly, b->lz))
                {
                    b->lx = b->ly = b->lz = 0;
                    if (!due) return 0;
                    mode = BM_LOOK;
                }
                else
                    mode = BM_GO;
            }
            else
                mode = BM_GO;
        }
        else if (bp_one(v, s) && water && !lava && bp_blocks(s) >= 2)
        {
            /* lane/obsidian6: one bucket, full of water: the site beside the nearest lava remembered (every cell's
             * lava fetched from there), else the look around */
            int m = bp_mem_pick(e, 1);
            if (m >= 0)
            {
                b->lx = b->mem[1][m].x, b->ly = b->mem[1][m].y, b->lz = b->mem[1][m].z;
                mode = BM_GO;
            }
            else if (due)
                mode = BM_LOOK;
            else
                return 0;
        }
        else if (bp_one(v, s) && lava && !water && !empty)
        {
            /* lane/obsidian6: one bucket, full of lava: poured back into the nearest pool remembered (the site's water
             * comes first) */
            int m = bp_mem_pick(e, 1);
            if (m < 0) return 0;
            mode = BM_DUMP;
            b->tx = b->mem[1][m].x, b->ty = b->mem[1][m].y, b->tz = b->mem[1][m].z;
        }
        else
            return 0;
        if (mode == BM_LOOK)
        {
            b->lk_set = 1;
            b->lkx = r->x, b->lkz = r->z, b->lk_step = e->steps, b->lk_yaw = r->yaw;
        }
    }
    b->bk.starts += 1;
    e->kind = M_MINE;
    b->on = 1;
    b->mode = mode;
    b->phase = b->uses = b->waited = b->held = 0;
    b->at = b->gain = e->age;
    b->best = 1e9;
    b->stuck = 0;
    b->fd0set = 0;
    b->bwalk = 0;
    b->px = r->x, b->pz = r->z;
    e->placed = e->pressed = 0;
    if (mode == BM_BUILD) b->sat = e->age;
    BPT(e, r, mode == BM_BUILD ? "start-build" : mode == BM_GO ? "start-go" : "start-fill");
    return 1;
}

/* the macro's drive (macro_drive's line): 1 when over */
static int bp_drive1(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey)
{
    struct bp_env *b = &e->bp;
    still(a);
    int fetching = r->screen == PS_INVENTORY;
    if (!bp_want(v, e, r, s) || (r->screen != PS_NONE && !fetching)) return 1;
    if (r->food <= BP_FOOD && eat_want(v, r, s)) return 1;
    if (s->eye_water && b->mode != BM_BUILD) return 1;
    {
        /* lava beside the feet or the head, or over the head (not the cast's own cell: it turns at once) */
        static const int D[9][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {1, 1, 0}, {-1, 1, 0}, {0, 1, 1},
                                    {0, 1, -1}, {0, 2, 0}};
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        for (int k = 0; k < 9; ++k)
            if (ob_lava(bp_blk(v, e, fx + D[k][0], fy + D[k][1], fz + D[k][2], NULL)) && b->mode != BM_BUILD)
            {
                BPT(e, r, "lava-near");
                if (b->mode == BM_LAVA || b->mode == BM_WATER) bp_mark_bad(e, b->tx, b->ty, b->tz, 1200);
                b->mode = BM_IDLE;
                b->off = e->steps + 100;
                return 1;
            }
    }
    if (b->mode == BM_WATER || b->mode == BM_LAVA)
    {
        if (!bp_fill(v, e, r, s, a, ey)) return 0;
        if (MOPT(MO_BP_FILL_GIVEUP))
        {
            /* lane/portal: the trip's answer counted (the bucket of its kind held more than at its start: a fill) */
            if ((int)inv_count(s, b->mode == BM_LAVA ? LAVA_BUCKET : WATER_BUCKET) > b->before)
                b->fill_fail = 0;
            else if (++b->fill_fail >= BP_FILL_STREAK)
            {
                BPT(e, r, "fill-giveup");
                int k = b->mode == BM_LAVA;
                b->fill_fail = 0;
                b->fill_rests += 1;
                b->mode = BM_IDLE;
                if (MOPT(MO_BP_FILL_GIVEUP) == BFG_REST)
                    b->off = e->steps + BP_FILL_LONG;
                else
                {
                    /* sky: the kind's sources forgotten (as a source taken), the eye's alone kept for a while */
                    for (int i = 0; i < BP_MEM; ++i) b->mem[k][i].y = -1000;
                    b->fill_blind[k] = e->steps + BP_FILL_BLIND;
                }
                return 1;
            }
        }
        b->mode = b->site ? BM_BUILD : BM_IDLE;
        if (b->site)
        {
            b->sub = -1;
            b->resub = 0;
            b->sat = e->age;
            return 0;
        }
        return 1;
    }
    if (b->mode == BM_DUMP)
    {
        /* lane/obsidian6: as a fill's end: back to the held site's build, else over */
        if (!bp_dump(v, e, r, s, a, ey)) return 0;
        b->mode = b->site ? BM_BUILD : BM_IDLE;
        if (b->site)
        {
            b->sub = -1;
            b->resub = 0;
            b->sat = e->age;
            return 0;
        }
        return 1;
    }
    if (b->mode == BM_ROAM) return bp_roam(v, e, r, s, a, ey);     /* lane/obsidian5 */
    if (b->mode == BM_UP) return bp_up(v, e, r, s, a, ey);
    if (b->mode == BM_CLIMB)
    {
        /* lane/obsidian4: over at OB_SEARCH_Y, at lava known, or when its turns ran out (then a rest) */
        if (inv_count(s, LAVA_BUCKET) == 0 && bp_mem_pick(e, 1) >= 0) return 1;
        if (!ob_climb(v, e, r, s, a, ey)) return 0;
        if ((int)floor(r->y + 0.01) < OB_SEARCH_Y) b->climb_off = e->steps + BP_CLIMB_OFF;
        return 1;
    }
    if (b->mode == BM_LOOK)
    {
        /* each of BP_LOOKS held BP_HOLD steps, then looked (the memory's scan from it) */
        if (b->phase >= 18) return 1;
        a->hold = 1u << PK_SNEAK;       /* lane/obsidian4: crouched (the look around may stand at a ledge) */
        a->look_mode = 1;
        a->look[0] = b->lk_yaw + BP_LOOKS[b->phase][0];
        a->look[1] = BP_LOOKS[b->phase][1];
        if (e->age - b->at < BP_HOLD) return 0;
        struct obsview_hit h[OV_KINDS];
        if (obsview_scan(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, BP_SCAN_R,
                         1 << OV_WATER | 1 << OV_LAVA | OV_EXPOSED, NULL, NULL, h) == 0)
        {
            if (h[OV_WATER].d > 0.0f) bp_mem_add(b, 0, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z);
            if (h[OV_LAVA].d > 0.0f) bp_mem_add(b, 1, h[OV_LAVA].x, h[OV_LAVA].y, h[OV_LAVA].z);
        }
        b->phase += 1;
        b->at = e->age;
        return 0;
    }
    if (b->mode == BM_GO)
    {
        /* to the lava fetched (or remembered): a site within BP_SITE_R of it, facing away */
        double hd = py_hypot2(b->lx + 0.5 - r->x, b->lz + 0.5 - r->z);
        /* lane/obsidian4: with both buckets full the site need not be beside the pool (the lava is carried): rings of
         * points out to BP_GO_RINGS' last, a site tried at every feet cell within BP_SITE_R of the centre */
        int full = inv_count(s, LAVA_BUCKET) > 0 && inv_count(s, WATER_BUCKET) > 0;
        /* lane/obsidian6: one bucket of water: the rings too (within BP_SITE_R of the pool, seed 393's kit refused 1,003
         * of 1,007 site tries for lava near or a site given up, and cast nothing in 8 idle envs); each cell's lava trip
         * is the longer for it */
        full |= bp_one(v, s) && inv_count(s, WATER_BUCKET) > 0;
        double site_r = full ? BP_SITE_FAR : BP_SITE_R;
        if (hd <= site_r && r->on_ground && b->stuck < 3)
        {
            if (bp_site(v, e, r))
            {
                if (bp_one(v, s)) b->bk.one_sites += 1;     /* lane/obsidian6 */
                b->mode = BM_BUILD;
                BPT(e, r, "site");
                return 0;
            }
            if (!full && ++b->site_tries > BP_SITE_TRIES * 40)
            {
                b->bk.fail_kind[BF_SITE] += 1;
                bp_mark_bad(e, b->lx, b->ly, b->lz, 3000);
                b->lx = b->ly = b->lz = 0;
                b->off = e->steps + 100;
                return 1;
            }
        }
        /* lane/obsidian6: one bucket, BP_GO_CAP too (the water it carries may be far from the lava) */
        if (e->age > (full || bp_one(v, s) ? BP_GO_CAP : 2000))
        {
            b->bk.fail_kind[BF_SITE] += 1;
            bp_mark_bad(e, b->lx, b->ly, b->lz, 3000);
            b->lx = b->ly = b->lz = 0;
            b->off = e->steps + 100;
            return 1;
        }
        /* points BP_GO_R blocks out from the lava, the first on the player's side, then 45 degrees round it each time
         * one is reached (or the walk sticks), the site tried at every feet cell on the way */
        if (b->phase == 0)
        {
            b->lk_yaw = (float)atan2(r->z - (b->lz + 0.5), r->x - (b->lx + 0.5));
            b->phase = 1;
        }
        double th = b->lk_yaw + (b->phase - 1) * (M_PI / 4.0);
        int ring = (b->phase - 1) / 8;
        double gr = full ? BP_GO_RINGS[ring < BP_NRINGS ? ring : BP_NRINGS - 1] : BP_GO_R;
        double gx = b->lx + 0.5 + gr * cos(th), gz = b->lz + 0.5 + gr * sin(th);
        if (bp_walk(v, e, r, s, a, ey, gx, r->y + 1.0, gz, 1.0) || b->stuck > 20)
        {
            b->stuck = 0;
            if (++b->phase > (full ? 8 * BP_NRINGS : 9))
            {
                b->bk.fail_kind[BF_SITE] += 1;
                bp_mark_bad(e, b->lx, b->ly, b->lz, 3000);
                b->lx = b->ly = b->lz = 0;
                b->off = e->steps + 100;
                return 1;
            }
        }
        return 0;
    }
    if (b->mode == BM_BUILD)
    {
        if (!b->site) return 1;
        return bp_build(v, e, r, s, a, ey);
    }
    return 1;
}

static int bp_drive(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    int over = bp_drive1(v, e, r, s, a, ey);
    /* no negative zero in a look sent: the oracle's script writes it as -0, which Java reads as 0 */
    a->look[0] += 0.0f, a->look[1] += 0.0f;
#ifdef BP_STEP_FROM
    /* -DBP_STEP_FROM=A -DBP_STEP_TO=B [-DBP_ENV=I] (with BP_TRACE): env I's (0) steps A..B, a line each */
#ifndef BP_ENV
#define BP_ENV 0
#endif
    if ((int)(e - v->e) == BP_ENV && e->steps >= BP_STEP_FROM && e->steps <= BP_STEP_TO)
    {
        double lr = 0, lf = 0;
        if (e->bp.site) bp_local(&e->bp, r, &lr, &lf);
        fprintf(stderr, "BPS %ld mode %d step %d sub %d aim %d held %d w %d at %.2f %.2f %.2f l %.2f %.2f yaw %.0f %.0f "
                        "hold %x use %d look %.1f %.1f mo %d:%d,%d,%d/%d lq %d:%d,%d,%d cur %d\n",
                (long)e->steps, e->bp.mode, e->bp.step, e->bp.sub, e->bp.aim, e->bp.held, e->bp.waited, r->x, r->y,
                r->z, lr, lf, r->yaw, r->pitch, a->hold, a->press[PK_USE], a->look[0], a->look[1],
                s->mo_hit ? s->mo_block : -1, s->mo_x, s->mo_y, s->mo_z, s->mo_side, s->lq_hit ? s->lq_block : -1,
                s->lq_x, s->lq_y, s->lq_z, s->current >= 0 && s->current < 9 ? s->inv[s->current].item : -1);
    }
#endif
    if (!over) return 0;
    e->bp.on = 0;
    /* no bucket left in hand (the policy's use key would pour it): a tool's slot, else the first without a bucket */
    int cur = s->current >= 0 && s->current < 9 ? s->inv[s->current].item : -1;
    if (a->hotbar < 0 && (cur == BUCKET || cur == WATER_BUCKET || cur == LAVA_BUCKET))
    {
        for (int i = 0; i < 9 && a->hotbar < 0; ++i)
        {
            int it = s->inv[i].item;
            if (s->inv[i].count > 0 && it >= 0 && it < 4096 && (v->T.pick[it] || it == 267 || it == 272 || it == 276))
                a->hotbar = i;
        }
        for (int i = 0; i < 9 && a->hotbar < 0; ++i)
        {
            int it = s->inv[i].item;
            if (it != BUCKET && it != WATER_BUCKET && it != LAVA_BUCKET) a->hotbar = i;
        }
    }
    return 1;
}

int neth_vec_bportal(const struct neth_vec *v, int id, struct neth_vec_bportal *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL) return -1;
    const struct vec_env *e = &v->e[id];
    const struct bp_book *k = &e->bp.bk;
    memset(out, 0, sizeof *out);
    out->starts = k->starts, out->sites = k->sites, out->aborts = k->aborts, out->casts = k->casts;
    out->lit = k->lit, out->trips_lava = k->trips_lava, out->trips_water = k->trips_water, out->uses = k->uses;
    for (int i = 0; i < 8; ++i) out->fail_kind[i] = k->fail_kind[i];
    out->lit_step = k->lit_step, out->steps_on = k->steps_on, out->best_casts = k->best_casts;
    out->step = e->bp.site ? e->bp.step : -1;
    out->mode = e->bp.mode;
    out->nmem_water = e->bp.nmem[0], out->nmem_lava = e->bp.nmem[1];
    out->pools = k->pools, out->fills_lava = k->fills_lava, out->search_found = k->search_found;
    out->back_walks = k->back_walks, out->site_lost = k->site_lost, out->search_steps = k->search_steps;
    out->wet_who = k->wet_who;
    for (int i = 0; i < 8; ++i) out->site_why[i] = k->site_why[i];
    out->climbs = k->climbs;
    out->roam_steps = k->roam_steps, out->up_steps = k->up_steps, out->roam_legs = k->roam_legs;
    out->roam_looks = k->roam_looks, out->ups = k->ups, out->ups_done = k->ups_done, out->roam_lava = k->roam_lava;
    out->roam_water = k->roam_water, out->roam_lava_d = k->roam_lava_d, out->roam_lava_step = k->roam_lava_step;
    out->roam_far = k->roam_far;
    out->pillars = k->pillars, out->pillar_tops = k->pillar_tops, out->pillar_lava = k->pillar_lava;
    out->one_sites = k->one_sites, out->one_casts = k->one_casts, out->dumps = k->dumps, out->parks = k->parks;
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static void bportal_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->v->macros & NETH_VEC_BPORTAL) bp_observe(c->v, c->e, c->r);   /* lane/obsidian3: the sources seen remembered */
}

static int bportal_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    return (v->macros & NETH_VEC_BPORTAL) && (bp_want(v, c->e, c->r, c->s) || ob_douse_want(v, c->e, c->r, c->s));
}

static int bportal_m_start(struct mac_step *c)      /* lane/obsidian3: before the obsidian */
{
    return (c->v->macros & NETH_VEC_BPORTAL) && bp_start(c->v, c->e, c->r, c->s);
}

static int bportal_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->bp.on) return -1;
    return bp_drive(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t bportal_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->bp.on ? NETH_VEC_BPORTAL : 0; }

static void bportal_m_end(struct vec_env *e) { e->bp.on = 0; }

static void bportal_m_reset(struct vec_env *e) { bp_reset(&e->bp); }

static const struct mac_switch BPORTAL_SW[] = {{"bportal", NETH_VEC_BPORTAL}, {NULL, 0}};

static const struct vec_macro MAC_BPORTAL = {
    .name = "bportal", .bit = NETH_VEC_BPORTAL, .sw = BPORTAL_SW, .observe = bportal_m_observe, .cand = bportal_m_cand,
    .start = bportal_m_start, .drive = bportal_m_drive, .label = bportal_m_label, .end = bportal_m_end,
    .reset = bportal_m_reset,
};
