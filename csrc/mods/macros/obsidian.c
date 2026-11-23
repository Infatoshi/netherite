/* lane/obsidian: NETH_VEC_OBSIDIAN, the portal's missing middle (vecenv.h). Included by macros.c (vecenv.c's statics: the
 * env, the look helpers, the inventory counts, fetch_item and pp_select), not compiled alone.
 *
 * What a speedrunner does for the bucket portal, from what the camera shows (obsview.h: the water and lava sources and
 * the obsidian the eye sees, never a block behind a wall), once the portal is still wanted: in the overworld, never in
 * the Nether this episode, under 10 obsidian, flint and steel, a diamond pickaxe and a bucket (empty or of water) held.
 * Each decision takes the first job that has a target in view:
 *
 *   mine   obsidian within 10 blocks, safe to dig (no lava beside it or over it, a solid block under it: its drop
 *          lands there): gone to, dug with the diamond pickaxe (188 ticks), its pit walked into for the drop
 *   fill   an empty bucket and a water source: gone to until the bucket's own ray (neth_rl_state lq_*) lands on a water
 *          source, the bucket used
 *   cast   a water bucket and a lava source below the feet, two cells or more from their column, a solid block under
 *          it: gone to until the bucket's ray lands on such a source within 4 blocks, the water poured (its ray passes
 *          the lava and places the water on the floor: the lava sources beside it turn to obsidian), 20 steps still
 *
 * so a lake goes cast, fill (the poured water taken back), mine, cast again, until 10 are held; NETH_VEC_PORTAL then
 * builds the frame. With nothing in view the strip mine searches (strip_want's line: the tunnel at y 12 crosses the
 * caves where lava pools lie at y 10 and water springs run from the walls). The safety: the walk is crouched and on
 * the ground (no edge walked off; a jump only up a one-block step), a step with lava in its cell, beside it, over it
 * or in the two cells under it is not taken, lava beside the player's own cells ends the macro, a burning player with
 * a water bucket pours it at the feet (the douse, ahead of every macro and the lava escape), and lane/lavaesc's
 * escape ends this macro as it ends any other. A target is given up (left alone 1200 steps, the macro resting 100) on
 * no half-block gain in 120 steps, three uses that changed nothing, or after 600 steps. The hotbar goes back to the
 * diamond pickaxe when a job ends with a bucket in hand (the policy's own use key would pour it).
 *
 * What it reads besides the view: the cells around a target, the walk's next cell and the player's own cells in the
 * client world (obsview_block), for lava and floors only; lava only as the run file's lava_sense allows (ob_block,
 * lane/macroopts: view by default, never sound).
 * -DOB_TRACE (a VARIANT build: make -C csrc/runtime rlbind VARIANT=obt XFLAGS=-DOB_TRACE) prints each start, use,
 * douse, give-up and death to stderr.
 *
 * lane/obsidian2, round two (each from a traced death in the kit case, out/runtime/obsidian_case):
 *   - the search above the lava layer: the strip it drives runs with the feet at OB_SEARCH_Y (15) or above, staired
 *     down to it from above (strip's ob_strip_y line); below it, with nothing in view, the climb (OJ_CLIMB) stairs up
 *     to it. Hot lava (OV_HOT: any lava cell in view, ahead along the walk within OB_HOT_R, at the head's level or
 *     below) turns the strip a quarter (strip's ob_hot line) and the climb; a lava touch or a burn reverses the
 *     strip's direction (the trace: the strip went on into the same wall after the escape and the douse)
 *   - the look around (OJ_LOOK): with nothing in view, before the strip, twelve looks (six yaws at 15 degrees down,
 *     six at 50) every OB_LOOK_DIST blocks; a target seen begins its job on the spot
 *   - the memory: the water and lava sources the scans see (the macro's own, and with the bit on before the kit a look
 *     every OB_MEM_EVERY steps: the surface's lakes and pools before any dig) kept, OB_MEM a kind; with nothing in
 *     view the nearest the bucket wants within OB_MEM_GO is gone to, the walk digging the blocks in the line to it
 *   - the cast from the bucket's reach (OB_CAST_REACH) on any lava cell its ray lands on, and a staircase down toward
 *     lava out of reach below (ob_descend)
 * Starts: as round one (the kit held, under 10 obsidian, the overworld, the Nether not yet), the screen closed, on the
 * ground, out of water, not resting (OB_CLIMB_OFF, 100 after a give-up) and not calm (240 after a douse). Ends: the
 * job's want met (the drop picked up, the bucket filled, poured), given up, or the want gone (10 obsidian held: the
 * portal macro builds; the Nether entered). It leaves the diamond pickaxe in hand. The books (ob_observe, with the bit
 * off too) are vecenv.h neth_vec_obsidian's. */

/* lane/obsidian2's constants. Above the lava layer: in 1.7.10 the cave carver fills what it carves below y 10 with lava
 * and the lava lakes underground lie there too; the three runs of round one that burned under the search's strip died
 * at y 7 to 10, where the policy had dug down to and the strip (which only stairs down) stayed */
#ifndef OB_SEARCH_Y
#define OB_SEARCH_Y 15          /* the search's strip runs with the feet at this height or above (it stairs down to it,
                                   the climb up to it): its floor 4 over the lava layer, and below y 16, where the
                                   policy's own attack on the block under the feet is dropped (lane/lavaesc) */
#endif
#define OB_HOT_R 8              /* lava in view (OV_HOT) this far ahead along the strip, at the feet's level or below, */
#define OB_HOT_SIDE 2           /* and within this of its line: the strip turns a quarter, as at a liquid ahead */
#define OB_CLIMB_TURNS 4        /* a climb turned this often with no stair taken: it rests */
#define OB_CLIMB_STAIR 80       /* steps a stair may take (its three cells cleared and the step up) before a turn */
#define OB_CLIMB_OFF 400        /* the rest */
#define OB_LAVA_BURN 300        /* the books: burning this long after the last lava touch is the lava's */

#define OB_MEM_NEAR 3           /* the memory: a source within this (each axis) of a kept one is that one */
#define OB_MEM_GO 48            /* a remembered source this near (the eye's distance) is gone to when none is in view */
#define OB_MEM_UP 3             /* remembered water at most this far over the feet (the walk does not climb) */
#define OB_MEM_EVERY 20         /* the memory's own look (the bit on, the kit not yet held): every this many steps */
#define OB_CAST_REACH 4.8       /* the water poured on lava the bucket's ray lands on this near (its ray is 5 long) */
/* lane/macroopts: the variants that were -D builds are run-file options (vecenv.c MOPTS), read where v is */
#define OB_NODESCEND (!MOPT(MO_OB_DESCEND))      /* 1: no stair down toward a cast's lava (the variant without it) */
#define OB_DESCEND_H 7          /* the stair down only toward lava this near horizontally */
#define OB_DESCEND_H2 10        /* obsidian_cast_way strip: the stair down's reach, and the strip's toward lava past it */
#define OB_STEER_OFF 200        /* obsidian_cast_way strip: no steer toward the lava this long after a hazard's turn */
#define OB_FOOD_LEVEL 16       /* obsidian_food hunt: the food level at or under which an animal in view is hunted */
#define OB_RIM_WAIT 60          /* obsidian_cast_rim on: steps still after the pour (water runs a block in 5 ticks, 7 out) */
#define OB_CAST_SOURCE MOPT(MO_OB_CAST_SOURCE)   /* 1: the cast only where the ray lands on a lava source (round one's) */
#define OB_DEEP MOPT(MO_OB_DEEP)                 /* 1: round one's search at any height, none of lane/obsidian2's rules */
#define OB_EARLY_FILL MOPT(MO_OB_EARLY_FILL)     /* 1: an empty bucket filled at water in view as soon as it is made */
#define OB_NOLOOK (!MOPT(MO_OB_LOOK))            /* 1: no look around */
#define OB_NOSEARCH (!MOPT(MO_OB_SEARCH))        /* 1: no strip search */
/* lane/obsidian7's constants (obsidian_water sky: the water trip) */
#define OB_SKY_WATER (MOPT(MO_OB_WATER) >= 1)    /* 1: the water trip to the sky (obsidian_water sky or scout) */
#define OB_SCOUT (MOPT(MO_OB_WATER) == 2)        /* lane/obsid: obsidian_water scout: and the walk over the surface */
/* lane/obsid's constants (obsidian_water scout): of the 26 from-spawn diamond pickaxe runs (lane/dbench's eval) 3 saw a
 * water source while holding an empty bucket before the pickaxe and 1 after; lane/obsidian7's water trip reached the sky
 * 110 times in the kit case and saw water at none of them (its look reaches 16 blocks) */
#define OB_SCOUT_LEG 24.0       /* the spiral's first legs (blocks); the k-th is LEG * (1 + k / 2) (lane/food's scout) */
#define OB_SCOUT_LOOK_D 16.0    /* a far look every this many blocks walked */
#define OB_SCOUT_FAR 64         /* its radius (obsview_far: columns' tops; bportal's BP_FAR_R) */
#define OB_SCOUT_LOOKS 6        /* its looks: six yaws 60 degrees apart, */
#define OB_SCOUT_PITCH 15.0f    /* each this far down */
#define OB_SCOUT_NEAR 5.0       /* the walk to the water seen ends this near (horizontally): the fill's from there */
#define OB_SCOUT_MAX 6000       /* a scout's steps at most (5 minutes) */
#define OB_SCOUT_OFF 2400       /* no scout for this many steps after one that ended without water */
#define OB_SCOUT_N 6            /* scouts an episode at most */
#define OB_SCOUT_Y 50.0         /* the scout starts with the feet at this or over, and ends under it, */
#define OB_SCOUT_DROP 10.0      /* or this far under where it began (a cave's mouth, a ravine): at 58 flat, the first
                                   scouts ended at the first dip, 230 steps in, 79 of 79 with no far look made */
#define OB_SCOUT_STUCK 20       /* a leg that moved under a block in this many steps turns 45 degrees */
#define OB_SCOUT_AIR 150        /* swimming, the scout ends under this much air */
#define OJ_SCOUT OJ_GO          /* the job's slot (OJ_GO had none: its books' "go") */
#define OB_SCOUT_PILLAR 8       /* each far look from a pillar this many blocks over the ground (cobblestone; dug back
                                   down after): from the ground the far look's cone held up to 232 water cells within 64
                                   blocks and saw none (seed 42's runs walk in forest: the leaves stop the line) */
#define OB_SCOUT_KEEP 16        /* cobblestone never spent on the pillar (the portal's 7, the climbs' steps) */
#define OB_SCOUT_PCAP 240       /* steps the rise, or the way down, may take */
#define OB_SKY_LIGHT 8          /* the head's stored sky light (F3's sl) that is under the sky (bportal's BP_ROAM_SKY) */
#define OB_SKY_OFF 3000         /* no trip again for this many steps after one ends (the strip search between) */
#define OB_SEARCH_STALL 600     /* the search's strip in one cell this many steps: a quarter turn */
#define OB_SKY_BED 9000         /* obsidian_sea stop: no water trip this long after one met a sea floor over its stair */
#define OB_SKY_AGE 4000       /* a trip's steps at most (60 stairs; the other jobs give up at 600) */
#define OB_HANDOFF_EVERY 5      /* lane/obsid: obsidian_early_fill on: the strip's look for water to hand off to (steps) */
#define OB_TRIP (MOPT(MO_OB_TRIP) >= 1)   /* lane/obsid: obsidian_water_trip on: the walk to the surface water kept */
#define OB_TRIP_BK (MOPT(MO_OB_TRIP) == 2) /* bucket: and the climb to the sky for it as the bucket is made */
#define OB_TRIP_CLIMBS 2        /* bucket: its climbs an episode at most */
#define OB_TRIP_DAY 9000        /* bucket: a climb begun by this time of day (the trip ends at night: 12500) */
#define OB_TRIP_Y 56            /* water kept: a source at this height or over (the surface's) */
#define OB_TRIP_R0 128.0        /* the trip: the water this near across */
#define OB_TRIP_UP0 8           /* and this near up or down (the scout ends 10 under its start) */
/* lane/obsid2 (obsidian_trip_reach far): from spawn 12 of 47 kit runs at their first sky after the pickaxe had the kept
 * water 129 to 322 blocks across, 4 over 8 up or down (the census, at the 128 and 8 above) */
#define OB_TRIP_R (MOPT(MO_OB_TRIP_REACH) ? 336.0 : OB_TRIP_R0)
#define OB_TRIP_UP (MOPT(MO_OB_TRIP_REACH) ? 16 : OB_TRIP_UP0)
#define OB_TRIP_NIGHT MOPT(MO_OB_TRIP_NIGHT)     /* obsidian_trip_night on: the trip by night too (5 of 47 at night) */
#define OB_TRIP_N 6             /* trips an episode at most */
/* lane/obsid (obsidian_strip_cast on): the diamond pickaxe and a water bucket held: the search's strip at the diamond
 * strip's height, no look around nor climb, the cast only at lava this near (40 of 63 diamond strips from spawn had lava
 * within 6 of the tunnel; casts at lava 5 to 46 away gave up on the walk) */
#define OB_SC_ON(s) (MOPT(MO_OB_STRIP_CAST) && inv_count((s), DIAMOND_PICK) > 0 && inv_count((s), WATER_BUCKET) > 0)
#define OB_SC_NEAR 6.0f
#define OB_POOL_CAP 64          /* lane/obsid: a pool's cells walked at most (ob_pool) */
#define OB_POOL_MIN 4           /* obsidian_pool_min on: a cast's pool at least this many sources in view */
/* lane/obsid2 (obsidian_glance walk): the search's walk into each cell looks this far off the heading, to each side in
 * turn (45: the forward and strafe keys together walk 45 degrees off the look, so the walk stays on the tunnel) */
#define OB_GLANCE_YAW 45.0
#define OB_GLANCE_PITCH 35.0f   /* and this far down (a pool in a side cave's floor 2 to 5 blocks off) */
#define OB_GLANCE_SETTLE 8.0    /* the look within this (degrees) of the glance's: scanned once a cell */
#define OB_GLANCE (MOPT(MO_OB_GLANCE))
#define OB_AROUND_STUCK 8       /* obsidian_walk around: a walk this many steps without moving takes a side step */
#define OB_AROUND_AGE 30        /* the side step's steps at most */
#define OB_RL_AGE 600           /* obsidian_resume line: the return to the strip's line, its steps at most */
#define OB_LOOK_DIST 10     /* the look around: due when the feet are this far (horizontally) from the last one's */
#define OB_LOOK_HOLD 3          /* steps each of its looks is held (the scan reads the last) */
#define OB_DROP_DEEP 6          /* obsidian_drop on: a floor at most this far under the stair's (a fall of 8 at most) */
#define OB_DROP_HP 12.0f        /* a fall over three blocks (a heart a block past three) only at this health or more */
#define OB_DROP_EDGE 0.2        /* the crouch to the edge: the feet this far past it (the crouch holds them at 0.3) */
#define OB_DROP_CREEP 30        /* steps the crouch may take before the look down */

enum { OJ_MINE, OJ_FILL, OJ_CAST, OJ_DOUSE, OJ_CLIMB, OJ_GO, OJ_LOOK };

/* the look around's looks: yaw offsets from the start's, and pitches (down a little, then down more: lava and water
 * lie below a tunnel's or a hill's eye) */
static const float OB_LOOKS[12][2] = {{0, 15},   {60, 15},  {120, 15}, {180, 15}, {240, 15}, {300, 15},
                                      {330, 50}, {270, 50}, {210, 50}, {150, 50}, {90, 50},  {30, 50}};

static const int OB_DX[4] = {0, -1, 0, 1}, OB_DZ[4] = {1, 0, -1, 0};   /* the strip's directions (yaw 0, 90, 180, 270) */

#ifdef OB_TRACE
#define OBT(e, r, what) fprintf(stderr, "OBT %d %ld %s@%d job %d t %d %d %d at %.1f %.1f %.1f hp %.1f fire %d obs %d\n", \
                                (int)((e) - v->e), (long)(e)->steps, what, __LINE__, (e)->ob.job, (e)->ob.tx, (e)->ob.ty, (e)->ob.tz, \
                                (r)->x, (r)->y, (r)->z, (r)->health, (r)->fire, (int)inv_count(s, OBSIDIAN))
#else
#define OBT(e, r, what) ((void)0)
#endif

/* lane/obsid: the funnel's step k reached (counting only; after the kit) */
static void ob_fn(const struct neth_vec *v, struct vec_env *e, int k)
{
    struct obs_book *b = &e->ob.bk;
    if (b->kit < 0 || b->fn[k] >= 0) return;
    b->fn[k] = e->steps;
#ifdef OB_FUNNEL_TRACE
    static const char *const FN[OF_KINDS] = {"sky", "water", "fill", "lava", "cast", "obs1", "obs10", "nether"};
    fprintf(stderr, "OBF %d %ld %s kit %ld\n", (int)(e - v->e), (long)e->steps, FN[k], (long)b->kit);
#else
    (void)v;
#endif
}

static int ob_bad(const struct vec_env *e, int x, int y, int z)
{
    for (int i = 0; i < e->ob.nbad && i < OB_BAD; ++i)
        if (e->ob.bad[i].x == x && e->ob.bad[i].y == y && e->ob.bad[i].z == z && e->ob.bad[i].until > e->steps)
            return 1;
    return 0;
}

/* lava a cast may take: under the feet's height and two cells or more from the feet's column (lava at the head beside
 * the tunnel ran onto the player when cast at: three runs of sixteen burned) */
static int ob_lava_far(const struct vec_env *e, int x, int y, int z)
{
    int fx = (int)floor(e->last.x), fy = (int)floor(e->last.y + 0.01), fz = (int)floor(e->last.z);
    return y < fy && !(abs(x - fx) <= 1 && abs(z - fz) <= 1);
}

/* lane/macroopts: a cell of the client world as the macro may know it: lava only as lava_sense allows (vecenv.c
 * lava_seen); every other block as the client world has it (floors and walls within reach, as the macro's own digs
 * and steps meet them) */
static int ob_block(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z, int *meta)
{
    int b = obsview_block(v->p, (int)(e - v->e), x, y, z, meta);
    int k = lava_seen(v, e, x, y, z, b);
    if (k != b && meta) *meta = 0;
    return k;
}

struct ob_ctx { const struct neth_vec *v; const struct vec_env *e; };

static int ob_solid(int b);

/* ... and a solid block under it (the water bucket's ray passes lava and places the water on the first block it meets:
 * over a deep pool none within its 5 blocks, three uses that did nothing a lake); the scan leaves out lava that is
 * not, and a target given up */
static int ob_castable(const struct ob_ctx *c, int x, int y, int z)
{
    return ob_lava_far(c->e, x, y, z) && ob_solid(ob_block(c->v, c->e, x, y - 1, z, NULL));
}

/* lane/obsidian2: the hot lava that turns the walk: at the feet's level or below, ahead along the strip's (or the
 * climb's) direction within OB_HOT_R and OB_HOT_SIDE of its line; none while neither walks */
static int ob_hot_cell(const struct vec_env *e, int x, int y, int z)
{
    int d = e->strip ? e->sdir : e->ob.on && e->ob.job == OJ_CLIMB ? e->ob.cdir : -1;
    if (d < 0) return 0;
    int fx = (int)floor(e->last.x), fy = (int)floor(e->last.y + 0.01), fz = (int)floor(e->last.z);
    int along = (x - fx) * OB_DX[d] + (z - fz) * OB_DZ[d], side = abs((x - fx) * OB_DZ[d] - (z - fz) * OB_DX[d]);
    /* the head's level too: round two's traces, lava flowing out of the tunnel's wall beside the head */
    return y <= fy + 1 && along >= 0 && along <= OB_HOT_R && side <= OB_HOT_SIDE;
}

static int ob_diggable(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z);

static int ob_skip(void *ctx, int kind, int x, int y, int z)
{
    const struct ob_ctx *c = ctx;
    if (kind == OV_HOT) return !ob_hot_cell(c->e, x, y, z);
    /* lane/chain (obsidian_dig_skip on): obsidian the macro may not dig passed over for the next in view */
    /* lane/bportal (obsidian_dig_skip bad): the targets given up passed over too, as they are with the skip off (on,
     * this return skipped ob_bad: s42k3f0's 10-to-20, five attempts gave up one overhead cell, a block in the line,
     * and picked it again every 101 steps, 72 times each) */
    if (kind == OV_OBSIDIAN && c->v->mo[MO_OB_DIG_SKIP])
        return !ob_diggable(c->v, c->e, x, y, z) || (c->v->mo[MO_OB_DIG_SKIP] == 2 && ob_bad(c->e, x, y, z));
    return (kind == OV_LAVA && !ob_castable(c, x, y, z)) || ob_bad(c->e, x, y, z);
}

static void ob_give_up(struct vec_env *e)
{
    int i = e->ob.nbad % OB_BAD;
    e->ob.bad[i].x = e->ob.tx, e->ob.bad[i].y = e->ob.ty, e->ob.bad[i].z = e->ob.tz;
    e->ob.bad[i].until = e->steps + 1200;
    e->ob.off = e->steps + 100;             /* a lake has hundreds of cells: the strip moves on before the next */
    if (e->last.dim == -1) e->ob.off = e->steps;   /* lane/bportal (obsidian_carry frame): the frame's next cell at once */
    e->ob.nbad += 1;
    if (e->ob.nbad >= 2 * OB_BAD) e->ob.nbad -= OB_BAD;
}

/* ------------------------------------------------------------------------------------------------------------------
 * obsidian_carry frame (lane/bportal): the overworld's frame built at 10 held (as off), and on the first arrival in the
 * Nether, before the Nether macro goes on, 10 mined from the arrival portal's own frame (the game builds the Nether
 * side's of obsidian; the kills never used it again, they left by nether_portal_home 131 to 210 blocks off). The cells
 * are read from the client world around the arrival's portal cell (the Nether macro's books: e->nt px, py, pz): any
 * obsidian in the box OB_FR_R across, OB_FR_DOWN under to OB_FR_UP over it, that the dig may take (ob_frame_dig: no
 * lava on any face), within OB_FR_REACH of the eye, not given up, the nearest first; obsidian in the dig's line is taken
 * as the next cell. Never begun while the feet or the head stand in a portal block. The mining is the MINE job's. Over at
 * 10 held, with no cell left in the box, or OB_FR_STEPS after the arrival */
#define OB_FR_R 3
#define OB_FR_DOWN 2
#define OB_FR_UP 4
#define OB_FR_REACH 4.5
#define OB_FR_STEPS 3000
/* obsidian_fallback_gap's values: steps with no obsidian gained (10 or more held) before obsidian_carry fallback builds
 * the first frame at 10 (320 about the 90th centile of the gaps between gains past 10 held under carry on, in the
 * attempts that reached 20 from the four obsidian starts: 667 gaps, p50 210, p90 325, p95 574) */
static const int OB_FB_GAP[] = {320, 640, 1280, 2560};
/* a frame cell the dig may take: no lava (or an unloaded cell) on any of its six faces (no floor wanted: its drop falls
 * to the Nether's floor beside the frame, not into a pool; the floor rule left the side columns' upper cells over a
 * dug lower one) */
static int ob_lava(int b);
static int ob_frame_dig(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    for (int k = 0; k < 6; ++k)
    {
        int b = ob_block(v, e, x + D[k][0], y + D[k][1], z + D[k][2], NULL);
        if (b < 0 || ob_lava(b)) return 0;
    }
    return 1;
}
/* lava (or an unloaded cell) in the 3x3x3 box about the cell's own and the one under it: its pit is not walked into */
static int ob_frame_pit_hot(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
            {
                int b = ob_block(v, e, x + dx, y + dy - 1, z + dz, NULL);
                if (b < 0 || ob_lava(b)) return 1;
            }
    return 0;
}
static int ob_frame_cell(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, int *ox, int *oy,
                         int *oz, int *left)
{
    const struct nt_env *n = &e->nt;
    double ey = r->y + 1.62, bd = 1e9;
    int best = -1;
    *left = 0;
    for (int dy = OB_FR_UP; dy >= -OB_FR_DOWN; --dy)
        for (int dx = -OB_FR_R; dx <= OB_FR_R; ++dx)
            for (int dz = -OB_FR_R; dz <= OB_FR_R; ++dz)
            {
                int x = n->px + dx, y = n->py + dy, z = n->pz + dz;
                if (ob_block(v, e, x, y, z, NULL) != OBSIDIAN) continue;
                *left += 1;
                /* (one under the feet's level only with no lava in the box about the pit it leaves: the bottom row's
                 * pit, the drop's walk into it, put the player beside the lava sea; s1's arrival, 37 lava give-ups at
                 * its corner and 38 deaths; none under the feet at all cost the other legs most of their gain) */
                if ((y < (int)floor(r->y + 0.01) && ob_frame_pit_hot(v, e, x, y, z)) || ob_bad(e, x, y, z) ||
                    !ob_frame_dig(v, e, x, y, z))
                    continue;
                double d = py_dist3(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5);
                if (d > OB_FR_REACH || d >= bd) continue;
                bd = d, best = 1, *ox = x, *oy = y, *oz = z;
            }
    return best;
}

/* the arrival frame's mining wanted: the option, the first Nether visit's window, the diamond pickaxe, under 10 held */
static int ob_frame_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    return MOPT(MO_OB_CARRY) >= 2 && (v->macros & NETH_VEC_OBSIDIAN) && r->dim == -1 && !(r->flags & PF_DEAD) &&
           !e->ob.fr_done && e->nt.arrived && e->nt.has_portal && e->steps - e->nt.t_arrive <= OB_FR_STEPS &&
           inv_count(s, DIAMOND_PICK) > 0 && inv_count(s, OBSIDIAN) < 10;
}

/* the feet or the head in a portal block */
static int ob_in_portal(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    return ob_block(v, e, fx, fy, fz, NULL) == 90 || ob_block(v, e, fx, fy + 1, fz, NULL) == 90;
}

/* the arrival frame's mining would start now (the Nether macro yields to it: nether.c): wanted, out of the portal, on
 * the ground, a cell to take */
static int ob_frame_ready(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    int x, y, z, left;
    return ob_frame_want(v, e, r, s) && r->screen == PS_NONE && r->on_ground && !s->eye_water &&
           e->steps >= e->ob.off && !ob_in_portal(v, e, r) && ob_frame_cell(v, e, r, &x, &y, &z, &left) >= 0;
}

/* the portal still wanted and the kit held: the macro's condition (the screen aside) */
static int ob_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s)
{
    if (r->dim == -1) return ob_frame_want(v, e, r, s);   /* lane/bportal (obsidian_carry frame; 0 without it) */
#ifdef OB_TRACE
    if (r->flags & PF_DIED) OBT(e, r, e->strip ? "died-strip" : e->ob.on ? "died-ob" : "died");
#endif
    if (!(v->macros & NETH_VEC_OBSIDIAN) || r->dim != 0 || (e->dims & 1) || (r->flags & PF_DEAD)) return 0;
    if (inv_count(s, OBSIDIAN) >= OB_KEEP_N || pg_nether_done(v, s)) return 0;   /* (lane/pearlgo: nether_done, macros.c) */
    /* lane/obsid (obsidian_water_trip on): before the diamond pickaxe the empty bucket's fill alone, flint or none */
    if (OB_TRIP && inv_count(s, DIAMOND_PICK) == 0) return inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0;
    /* lane/obsidian7: obsidian_flint later: the obsidian mined before the flint and steel (a diamond pickaxe held) */
    if (inv_count(s, FLINT_STEEL) == 0 && !(MOPT(MO_OB_FLINT) && inv_count(s, DIAMOND_PICK) > 0)) return 0;
    if (inv_count(s, DIAMOND_PICK) > 0)
    {
        int64_t nb = inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET);
        /* lane/obsidian7: bportal_dpick bportal, one bucket: the bucket portal's (bp_want) */
        if (nb + inv_count(s, LAVA_BUCKET) == 1 && MOPT(MO_BP_DPICK) == BD_BPORTAL && (v->macros & NETH_VEC_BPORTAL) &&
            MOPT(MO_BP_BUCKETS) != BB_TWO)
            return 0;
        return nb > 0;
    }
    /* obsidian_early_fill (a -DOB_EARLY_FILL build before lane/macroopts): the water carried down: an empty bucket
     * filled at water in view as soon as it is made, the diamond pickaxe not yet held (the fill job alone, no search);
     * the douse then has water too */
    if (OB_EARLY_FILL) return inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0;
    return 0;
}

/* lane/obsidian2: a source seen remembered (kind 0 water, 1 lava): one within OB_MEM_NEAR of a kept one is that one */
static void ob_mem_add(struct vec_env *e, int k, int x, int y, int z)
{
    for (int i = 0; i < e->ob.nmem[k] && i < OB_MEM; ++i)
        if (abs(e->ob.mem[k][i].x - x) <= OB_MEM_NEAR && abs(e->ob.mem[k][i].y - y) <= OB_MEM_NEAR &&
            abs(e->ob.mem[k][i].z - z) <= OB_MEM_NEAR)
            return;
    int i = e->ob.nmem[k] < OB_MEM ? e->ob.nmem[k]++ : (int)(e->ob.mem[k][0].n++ % OB_MEM);
    e->ob.mem[k][i].x = x, e->ob.mem[k][i].y = y, e->ob.mem[k][i].z = z;
}

/* lane/chain (obsidian_oracle on, PRIVILEGED: the search's hint, never a benchmark's): the client world's sources of the
 * kind the bucket wants (water for an empty one, lava for a water bucket) within OB_ORACLE_R blocks across and
 * OB_ORACLE_DOWN under to OB_ORACLE_UP over the feet, read whether the eye saw them or not, the OB_ORACLE_N nearest put
 * in the memory as if seen (ob_mem_pick then walks to one, digging toward it) */
#define OB_ORACLE_EVERY 200
#define OB_ORACLE_R 40
#define OB_ORACLE_DOWN 32
#define OB_ORACLE_UP 3
#define OB_ORACLE_N 4
/* the OB_ORACLE_N nearest sources of lava (lava 1) or water (0) in the client world round the feet cell, seen or not */
static int oracle_sources(const struct neth_vec *v, int id, int fx, int fy, int fz, int lava, int bx[OB_ORACLE_N],
                          int by[OB_ORACLE_N], int bz[OB_ORACLE_N])
{
    /* the section scan (obsview.c), in the same (y, z, x) order as the cell walk below, so the same slots win */
    int nb = obsview_find_sources(v->p, id, fx, fy, fz, OB_ORACLE_R, OB_ORACLE_DOWN, OB_ORACLE_UP, lava, OB_ORACLE_N,
                                  bx, by, bz);
    if (nb >= 0) return nb;
    nb = 0;
    double bd[OB_ORACLE_N];
    for (int y = fy - OB_ORACLE_DOWN; y <= fy + OB_ORACLE_UP; ++y)
    {
        if (y < 1 || y > 254) continue;
        for (int dz = -OB_ORACLE_R; dz <= OB_ORACLE_R; ++dz)
            for (int dx = -OB_ORACLE_R; dx <= OB_ORACLE_R; ++dx)
            {
                int meta = -1, b = obsview_block(v->p, id, fx + dx, y, fz + dz, &meta);
                if (meta != 0 || !(lava ? (b == 10 || b == 11) : (b == 8 || b == 9))) continue;
                double d = (double)dx * dx + (double)dz * dz + (double)(y - fy) * (y - fy);
                int k = nb < OB_ORACLE_N ? nb++ : -1;
                if (k < 0)
                {
                    for (int j = 0; j < OB_ORACLE_N; ++j)
                        if (d < bd[j] && (k < 0 || bd[j] > bd[k])) k = j;
                    if (k < 0) continue;
                }
                bx[k] = fx + dx, by[k] = y, bz[k] = fz + dz, bd[k] = d;
            }
    }
    return nb;
}

static void ob_oracle(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      int water)
{
    if (e->ob.oracle_at > 0 && e->steps - e->ob.oracle_at < OB_ORACLE_EVERY) return;
    e->ob.oracle_at = e->steps > 0 ? e->steps : 1;
    if (!water && inv_count(s, BUCKET) == 0) return;
    int bx[OB_ORACLE_N], by[OB_ORACLE_N], bz[OB_ORACLE_N];
    int nb = oracle_sources(v, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01), (int)floor(r->z), water, bx,
                            by, bz);
    for (int k = 0; k < nb; ++k) ob_mem_add(e, water ? 1 : 0, bx[k], by[k], bz[k]);
}

/* the look, every 5 steps while wanted (the strip's check, the start and the drive read the same one) */
static void ob_look(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (e->ob.scan_at >= 0 && e->steps - e->ob.scan_at < 5) return;
    e->ob.scan_at = e->steps;
    int water = inv_count(s, WATER_BUCKET) > 0;
    int mask = 1 << OV_OBSIDIAN | 1 << OV_HOT | (water ? 1 << OV_LAVA : 1 << OV_WATER);
    if (MOPT(MO_OB_FACES)) mask |= OV_FACES;              /* lane/obsid2: obsidian_faces on */
    struct ob_ctx c = {v, e};
    if (obsview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, 16, mask, ob_skip, &c,
                     e->ob.hit) != 0)
        memset(e->ob.hit, 0, sizeof e->ob.hit);
    if (!water) e->ob.hit[OV_LAVA].d = -1.0f;
    else e->ob.hit[OV_WATER].d = -1.0f;
    if (e->ob.hit[OV_WATER].d > 0.0f && inv_count(s, BUCKET) > 0) ob_fn(v, e, OF_WATER);   /* lane/obsid's funnel */
    if (e->ob.hit[OV_LAVA].d > 0.0f) ob_fn(v, e, OF_LAVA);
#ifdef OB_LOOK_TRACE
    if (e->ob.hit[OV_LAVA].d > 0.0f)
        fprintf(stderr, "OBLV %d %ld d %.1f at %d %d %d from %.1f %.1f %.1f yaw %.0f on %d job %d search %d sc %d off %d "
                "calm %d\n", (int)(e - v->e), (long)e->steps, e->ob.hit[OV_LAVA].d, e->ob.hit[OV_LAVA].x,
                e->ob.hit[OV_LAVA].y, e->ob.hit[OV_LAVA].z, r->x, r->y, r->z, r->yaw, e->ob.on, e->ob.job,
                e->ob.search, e->ob.sc_strip, e->steps < e->ob.off, e->steps < e->ob.calm);
#endif
    if (!OB_DEEP && e->ob.hit[OV_WATER].d > 0.0f)
        ob_mem_add(e, 0, e->ob.hit[OV_WATER].x, e->ob.hit[OV_WATER].y, e->ob.hit[OV_WATER].z);
    if (!OB_DEEP && e->ob.hit[OV_LAVA].d > 0.0f)
        ob_mem_add(e, 1, e->ob.hit[OV_LAVA].x, e->ob.hit[OV_LAVA].y, e->ob.hit[OV_LAVA].z);
    if (MOPT(MO_OB_ORACLE)) ob_oracle(v, e, r, s, water);
}

static int ob_lava(int b) { return b == 10 || b == 11; }

/* obsidian at (x, y, z) safe to dig and its drop kept: no lava beside it or over it, a solid block under it (with
 * lava or a hole under it the drop fell in and burned: mined obsidian held by none of the runs that mined it) */
static int ob_diggable(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}};
    for (int k = 0; k < 5; ++k)
    {
        int b = ob_block(v, e,x + D[k][0], y + D[k][1], z + D[k][2], NULL);
        if (b < 0 || ob_lava(b)) return 0;
    }
    return ob_solid(ob_block(v, e,x, y - 1, z, NULL));
}

/* lane/obsidian2: none in view: the nearest remembered source the bucket wants (water for an empty one, at most
 * OB_MEM_UP over the feet; lava for a water bucket, under the feet and off their column), OB_MEM_GO near, not given
 * up; its job (FILL, CAST), its cell into e->ob's when take, -1 none */
static int ob_mem_pick(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, int take)
{
    if (OB_DEEP) return -1;
    int k = inv_count(s, WATER_BUCKET) > 0 ? 1 : inv_count(s, BUCKET) > 0 ? 0 : -1, best = -1;
    /* lane/obsid: obsidian_strip_cast: no lava gone back to (lane/flint: unless obsidian_walk dig, whose walk arrives) */
    if (k == 1 && e->ob.sc_strip && MOPT(MO_OB_WALK) != 2) return -1;
    double bd = OB_MEM_GO, ey = e->last.y + 1.62;
    int fy = (int)floor(e->last.y + 0.01);
    for (int i = 0; k >= 0 && i < e->ob.nmem[k] && i < OB_MEM; ++i)
    {
        int x = e->ob.mem[k][i].x, y = e->ob.mem[k][i].y, z = e->ob.mem[k][i].z;
        if (ob_bad(e, x, y, z) || (k == 0 ? y > fy + OB_MEM_UP : !ob_lava_far(e, x, y, z))) continue;
        double d = py_dist3(e->last.x, ey, e->last.z, x + 0.5, y + 0.5, z + 0.5);
        if (d < bd) bd = d, best = i;
    }
    if (best < 0) return -1;
    if (take)
    {
        e->ob.tx = e->ob.mem[k][best].x, e->ob.ty = e->ob.mem[k][best].y, e->ob.tz = e->ob.mem[k][best].z;
        e->ob.remem = 1;
    }
    e->ob.mpx = e->ob.mem[k][best].x, e->ob.mpy = e->ob.mem[k][best].y, e->ob.mpz = e->ob.mem[k][best].z;
    return k == 0 ? OJ_FILL : OJ_CAST;
}

/* ------------------------------------------------------------------------------------------------------------------
 * obsidian_walk dig (lane/flint): the walk to a remembered source by the strip's own tunnel and stairs. Traced on the
 * flint_and_steel starts' base rows (tfl-, dts-): the fill's straight crouched walk to remembered water (4 to 44 across,
 * mostly below) gave up in 916 of 964 jobs on k50 and 767 of 814 on c150, pressed into a wall in its line or held by
 * its crouch at a drop (it has no stair down and digs only a block within 4 of the eye); and with the strip cast on, the
 * obsidian macro refused remembered lava outright in 98% of its no-job rows with a water bucket (lifting that gate alone,
 * obsidian_strip_cast off, sent the same straight walk to it: first obsidian 95 -> 49). With dig, a remembered source
 * past OB_WK_NEAR is no job: it becomes the strip's target (steered at it as diamond_oracle steers to an ore, its level
 * the target's plus one, the strip's cell, floor, lava, liquid and gravel tests as they are), and the job begins once
 * the feet arrive, one over the target's level, its column within OB_WK_NEAR (lava's cast needs it two or more off the
 * feet's column, ob_lava_far). The straight walk's side step (around) stays on with it */
#define OB_WK (MOPT(MO_OB_WALK) == 2 || MOPT(MO_OB_WALK) == 3)
/* fill: the walk for the fill's water alone; the cast's remembered lava as without it (gwd-: dig's walk to remembered
 * lava, with the strip cast's gate lifted for it, looped pour, fill and look at a cell the pour never hardened, 36 of
 * 448 attempts for the rest of their run, first obsidian 113 -> 69) */
#define OB_WK_FILL (MOPT(MO_OB_WALK) == 3)
#define OB_WK_NEAR 3            /* arrived: the target's column at most this many cells off the feet's (each axis) */
#define OB_WK_GIVEUP 3000       /* steps one walk may take (diamond_oracle's DO_GIVEUP) */
#define OB_WK_TURNS 12          /* the strip's own turns one walk may take (DO_TURNS 8: a longer way down and round) */
#define OB_WK_HOLD 30           /* after the strip's own turn its new way walked this long before the steering */

static int ob_wk_near(const struct vec_last *r, int x, int y, int z)
{
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    return fy == y + 1 && abs(fx - x) <= OB_WK_NEAR && abs(fz - z) <= OB_WK_NEAR;
}

/* the remembered target (x, y, z) for job: past OB_WK_NEAR with dig on, the strip's walk's target (kept, or set anew):
 * 1 (no job yet); 0 the job may begin */
/* (bp: the bucket portal's water trip's, bportal_walk dig) */
static int ob_wk_take2(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int x, int y, int z,
                       int job, int bp)
{
    if (!(bp ? MOPT(MO_BP_WALK) != 0 : OB_WK) || r->dim != 0 || ob_wk_near(r, x, y, z) ||
        (!bp && OB_WK_FILL && job != OJ_FILL))
        return 0;
    struct obs_env *o = &e->ob;
    if (!o->wk_on || o->wk_x != x || o->wk_y != y || o->wk_z != z)
    {
        o->wk_on = 1, o->wk_x = x, o->wk_y = y, o->wk_z = z, o->wk_job = job, o->wk_bp = bp;
        o->wk_t0 = e->steps, o->wk_turns = e->sturns, o->wk_last = e->sturns, o->wk_hold = 0;
        o->wk_n += 1;
#ifdef OB_TRACE
        fprintf(stderr, "OBW %d %ld begin job %d t %d %d %d at %.1f %.1f %.1f\n", (int)(e - v->e), (long)e->steps, job, x,
                y, z, r->x, r->y, r->z);
#endif
    }
    return 1;
}
static int ob_wk_take(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int x, int y, int z,
                      int job)
{
    return ob_wk_take2(v, e, r, x, y, z, job, 0);
}

static void bp_mark_bad(struct vec_env *e, int x, int y, int z, int64_t len);   /* bportal.c */

/* strip.c's line at each new set: the walk kept, arrived or given up, and the direction toward its target */
static void ob_wk_steer(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct obs_env *o = &e->ob;
    if (!o->wk_on) return;
    const char *end = NULL;
    if (!(o->wk_bp ? MOPT(MO_BP_WALK) != 0 : OB_WK) || !o->search || r->dim != 0) end = "off";
    else if (e->steps - o->wk_t0 > OB_WK_GIVEUP || e->sturns - o->wk_turns > OB_WK_TURNS)
    {
        int i = o->nbad % OB_BAD;                          /* given up: the cell passed over (ob_give_up's mark) */
        o->bad[i].x = o->wk_x, o->bad[i].y = o->wk_y, o->bad[i].z = o->wk_z, o->bad[i].until = e->steps + 1200;
        o->nbad += 1;
        if (o->nbad >= 2 * OB_BAD) o->nbad -= OB_BAD;
        if (o->wk_bp) bp_mark_bad(e, o->wk_x, o->wk_y, o->wk_z, 1200);   /* and the bucket portal's */
        o->wk_gave += 1;
        end = "giveup";
    }
    else
    {
        int b = ob_block(v, e, o->wk_x, o->wk_y, o->wk_z, NULL);
        if (b != 0 && !(o->wk_job == OJ_FILL ? (b == 8 || b == 9) : ob_lava(b))) end = "gone";
        else if (ob_wk_near(r, o->wk_x, o->wk_y, o->wk_z)) o->wk_arr += 1, end = "arrive";
    }
    if (end)
    {
#ifdef OB_TRACE
        fprintf(stderr, "OBW %d %ld %s job %d t %d %d %d at %.1f %.1f %.1f\n", (int)(e - v->e), (long)e->steps, end,
                o->wk_job, o->wk_x, o->wk_y, o->wk_z, r->x, r->y, r->z);
#endif
        o->wk_on = 0;
        return;
    }
    if (e->sturns != o->wk_last)
    {
        o->wk_last = e->sturns;
        o->wk_hold = e->steps + OB_WK_HOLD;                /* the strip's own turn: its new way walked first */
    }
    if (e->steps < o->wk_hold) return;
    int dx = o->wk_x - (int)floor(r->x), dz = o->wk_z - (int)floor(r->z);
    if (dx == 0 && dz == 0) return;
    if (abs(dx) >= abs(dz)) e->sdir = dx > 0 ? 3 : 1;
    else e->sdir = dz > 0 ? 0 : 2;
}

/* the job a target in view gives (its cell in e->ob's), -1 none */
static int ob_pick(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    const struct obsview_hit *h = e->ob.hit;
    int job = -1;
    /* lane/obsidian7 (obsidian_cast_rim on): the poured water taken back before the dig (the rim's water ran over the
     * obsidian: the dig under it gave up with the eye in water, and obsidian under water digs five times slower) */
    if (MOPT(MO_OB_CAST_RIM) && h[OV_WATER].d > 0.0f && inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0)
        job = OJ_FILL;
    else if (h[OV_OBSIDIAN].d > 0.0f && h[OV_OBSIDIAN].d <= 10.0f && inv_count(s, DIAMOND_PICK) > 0 &&
        ob_diggable(v, e, h[OV_OBSIDIAN].x, h[OV_OBSIDIAN].y, h[OV_OBSIDIAN].z))
        job = OJ_MINE;
#ifdef OB_TRACE
    else if (h[OV_OBSIDIAN].d > 0.0f && h[OV_OBSIDIAN].d <= 10.0f)
        fprintf(stderr, "OBT %d %ld nodig t %d %d %d under %d\n", (int)(e - v->e), (long)e->steps, h[OV_OBSIDIAN].x,
                h[OV_OBSIDIAN].y, h[OV_OBSIDIAN].z,
                obsview_block(v->p, (int)(e - v->e), h[OV_OBSIDIAN].x, h[OV_OBSIDIAN].y - 1, h[OV_OBSIDIAN].z, NULL));
#endif
    if (job < 0 && h[OV_WATER].d > 0.0f && inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0)
        job = OJ_FILL;
    else if (job < 0 && h[OV_LAVA].d > 0.0f && inv_count(s, WATER_BUCKET) > 0 &&
             (!e->ob.sc_strip || h[OV_LAVA].d <= OB_SC_NEAR))  /* lane/obsid: obsidian_strip_cast, the tunnel's lava */
        job = OJ_CAST;
    e->ob.remem = 0;
    if (job < 0) return -1;
    int k = job == OJ_MINE ? OV_OBSIDIAN : job == OJ_FILL ? OV_WATER : OV_LAVA;
    e->ob.tx = h[k].x, e->ob.ty = h[k].y, e->ob.tz = h[k].z;
    return job;
}

/* lane/obsidian7 (obsidian_cast_way strip): the cast's lava (e->ob's tx, tz, as the pick set them) farther across
 * than the stair down takes (OB_DESCEND_H2): the search's strip goes toward it instead */
static int ob_cast_far(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!MOPT(MO_OB_CAST_WAY)) return 0;
    return py_hypot2(e->ob.tx + 0.5 - r->x, e->ob.tz + 0.5 - r->z) > OB_DESCEND_H2;
}

/* lane/obsidian2: the look around is due: none yet this episode, or the feet OB_LOOK_DIST from the last one's place */
static int ob_look_due(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (OB_NOLOOK) return 0;
    if (e->ob.sc_strip) return e->ob.pc_look > 0;          /* lane/obsid: obsidian_strip_cast: only after a cast */
    return !e->ob.lk_set || py_hypot2(r->x - e->ob.lkx, r->z - e->ob.lkz) > OB_LOOK_DIST;
}

/* lane/obsidian4: bportal.c's (included after this file): the bucket portal wants lava with none to go to, and
 * its search's step */
static int bp_lava_search(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s);
static int bp_search(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s);

/* strip_want's line: wanted with nothing in view to go to, the strip searches */
static int ob_search(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    e->ob.yield = 0;
    /* obsidian_search off: no strip search, the macro only takes what the policy's own walk brings in view */
    if (OB_NOSEARCH) return 0;
    /* lane/obsidian4: the search entered without the diamond pickaxe when the bucket portal wants lava */
    if ((v->macros & NETH_VEC_BPORTAL) && bp_lava_search(v, e, r, s)) return bp_search(v, e, r, s);
    /* lane/flint (bportal_walk dig): the bucket portal's water trip walked as the strip, while its empty bucket waits */
    if ((v->macros & NETH_VEC_BPORTAL) && MOPT(MO_BP_WALK) && e->ob.wk_on && e->ob.wk_bp)
    {
        if (r->screen == PS_NONE && inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0)
            return e->ob.search = 1;
        e->ob.wk_on = 0;                                   /* the bucket filled or gone: the walk over */
    }
    if (!ob_want(v, e, r, s) || r->screen != PS_NONE || inv_count(s, DIAMOND_PICK) == 0) return 0;
    e->ob.sc_strip = OB_SC_ON(s);                         /* lane/obsid: obsidian_strip_cast (ob_strip_y reads it) */
    if (!OB_DEEP)
    {
    /* lane/obsidian2: never below the lava layer (obsidian_deep on: round one's search at any height; lane/obsid's
     * obsidian_strip_cast on, with the water bucket: the diamond strip's own height) */
    if ((int)floor(r->y + 0.01) < OB_SEARCH_Y && !e->ob.sc_strip) return 0;
    /* a lava touch or a burn since the last: the strip's direction reversed, back along the tunnel it dug (the trace:
     * the strip went on the same way after the escape, into the same lava, the water bucket already poured) */
    int64_t hurt = e->ob.bk.lava_at > e->ob.bk.burn_at ? e->ob.bk.lava_at : e->ob.bk.burn_at;
    if (hurt > 0 && hurt > e->ob.burn_seen + 100)
    {
        e->ob.burn_seen = hurt;
        e->sdir = (e->sdir + 2) & 3;
        e->sk = -1;
    }
    /* the look around first (ob_start), when ob_start may begin it: resting or calm, the strip goes on (the policy
     * had walked into a lava lake beside the tunnel while neither ran) */
    if (e->steps >= e->ob.off && e->steps >= e->ob.calm && r->on_ground && ob_look_due(v, e, r)) return 0;
    }
    if (e->steps < e->ob.off) return e->ob.search = 1;     /* resting after a give-up: the strip moves on */
    if (OB_SKY_WATER)
    {
        /* lane/obsidian7 (with the water trip): the strip stalled in one cell OB_SEARCH_STALL steps turns a quarter
         * (traced: s42's start, the stair down at y 42 over a hole looked from cell to cell beyond it for 20,000 steps) */
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        if (fx != e->ob.stx || fy != e->ob.sty || fz != e->ob.stz || e->ob.st_at <= 0)
            e->ob.stx = fx, e->ob.sty = fy, e->ob.stz = fz, e->ob.st_at = e->steps;
        else if (e->steps - e->ob.st_at > OB_SEARCH_STALL)
        {
            e->sdir = (e->sdir + 1) & 3;
            e->sk = -1;
            e->ob.st_at = e->steps;
            e->ob.stalls += 1;
        }
    }
    ob_look(v, e, r, s);
    const struct obsview_hit *h = e->ob.hit;
    int none = !(h[OV_OBSIDIAN].d > 0.0f && h[OV_OBSIDIAN].d <= 10.0f) && !(h[OV_WATER].d > 0.0f) &&
               !(h[OV_LAVA].d > 0.0f && (!e->ob.sc_strip || h[OV_LAVA].d <= OB_SC_NEAR)) && ob_mem_pick(v, e, s, 0) < 0;
    /* lane/flint (obsidian_walk dig): a remembered source past the walk's reach: the strip walks to it (none) */
    if (!none && OB_WK && !(h[OV_OBSIDIAN].d > 0.0f && h[OV_OBSIDIAN].d <= 10.0f) && !(h[OV_WATER].d > 0.0f) &&
        !(h[OV_LAVA].d > 0.0f && (!e->ob.sc_strip || h[OV_LAVA].d <= OB_SC_NEAR)))
    {
        int j = ob_mem_pick(v, e, s, 0);
        if (j >= 0 && r->on_ground && ob_wk_take(v, e, r, e->ob.mpx, e->ob.mpy, e->ob.mpz, j)) none = 1;
    }
    if (!none && MOPT(MO_OB_CAST_WAY) && inv_count(s, WATER_BUCKET) > 0)
    {
        /* lane/obsidian7 (obsidian_cast_way strip): the water bucket's lava farther across than the stair down takes:
         * the strip steered toward it (between sets, never within OB_STEER_OFF of a hazard's turn); nothing ob_start
         * would begin (obsidian in view it may not dig): the strip goes on */
        int job = ob_pick(v, e, s);
        if (job < 0 && r->on_ground) job = ob_mem_pick(v, e, s, 1);
        int far = job == OJ_CAST && ob_cast_far(v, e, r);
        if (far || job < 0) none = 1;
        if (e->sturns != e->ob.steer_turns) e->ob.steer_turns = e->sturns, e->ob.steer_off = e->steps + OB_STEER_OFF;
        if (far && e->sk < 0 && e->steps >= e->ob.steer_off)
        {
            int dx = e->ob.tx - (int)floor(r->x), dz = e->ob.tz - (int)floor(r->z);
            int want = abs(dx) >= abs(dz) ? (dx > 0 ? 3 : 1) : (dz > 0 ? 0 : 2);
            if (want != e->sdir) e->sdir = want, e->ob.steers += 1;
        }
    }
    if (none && e->ob.rl_on && MOPT(MO_OB_RESUME))
    {
        /* lane/obsid2 (obsidian_resume line): nothing left in view after a cast: the strip turned back onto the line it
         * was on (a quarter turn toward it until the feet are on it), then its heading again; at most OB_RL_AGE steps
         * (the 3 + 3 + 5 case: the strip went on from inside the first pool's cave, on a line beside the tunnel, and
         * passed the next pool on the tunnel's other side behind a wall) */
        int d = e->ob.rl_dir, fx = (int)floor(r->x), fz = (int)floor(r->z);
        int perp = OB_DX[d] == 0 ? fx - e->ob.rl_x : fz - e->ob.rl_z;
        int want = perp == 0 ? d : OB_DX[d] == 0 ? (perp > 0 ? 1 : 3) : (perp > 0 ? 2 : 0);
        if (e->ob.rl_at >= 0) e->ob.rl_at = -1 - e->steps;          /* the return's first step (negative: begun) */
        if (perp == 0 || e->steps + 1 + e->ob.rl_at > OB_RL_AGE)
            e->ob.rl_on = 0, e->ob.rl_n += perp == 0;
        if (want != e->sdir) e->sdir = want, e->sk = -1;
    }
    e->ob.search = none;
    /* lane/obsid2 (obsidian_yield on): a target in view: the strip gives the step up (strip_want0's line) so this
     * macro starts on it; without it the strip's own want (iron, diamonds) went on digging past lava 4 to 6 blocks off
     * (the x4 case: lava in view in 31 of 32 envs, a cast in 15) */
    /* (yielding only on a job ob_start takes, ob_pick's, measured worse: the 10-source case's Nether 21 and 24 of 32 to 20
     * and 21 at the same ten obsidian) */
    e->ob.yield = !none && MOPT(MO_OB_YIELD) && e->steps >= e->ob.off && e->steps >= e->ob.calm && !s->eye_water;
    return none;
}

/* lane/obsid: strip's stop line (obsidian_early_fill on): the strip, digging for iron or diamonds with an empty bucket
 * held, hands the step to this macro when a water source is in view, every OB_HANDOFF_EVERY steps of its drive (its
 * runs are 300 steps long and this macro's start is tried only between macros: the water a tunnel opened was passed
 * by). After the diamond pickaxe the strip is this macro's search and already ends on a target in view (ob_search) */
/* lane/obsid (obsidian_water_trip on): the trip to the surface water kept is due: an empty bucket, no water bucket,
 * on the ground out of water under the sky by day, the water within OB_TRIP_R across and OB_TRIP_UP up or down */
static int ob_trip_due(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    const struct obs_env *o = &e->ob;
    if (!OB_TRIP || !o->sw_ok || o->sw_trips >= OB_TRIP_N || e->steps < o->sc_off || !r->on_ground || s->eye_water ||
        r->y < OB_SCOUT_Y)
        return 0;
    if (inv_count(s, BUCKET) == 0 || inv_count(s, WATER_BUCKET) > 0 || (r->wtime % 24000 >= 12500 && !OB_TRIP_NIGHT))
        return 0;
    if (py_hypot2(o->sw_x + 0.5 - r->x, o->sw_z + 0.5 - r->z) > OB_TRIP_R || abs(o->sw_y - (int)floor(r->y + 0.01)) > OB_TRIP_UP)
        return 0;
    return obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z)) >=
           OB_SKY_LIGHT;
}

/* lane/obsid (obsidian_water_trip bucket): the climb to the sky for the surface water kept, the bucket just made: no
 * diamond pickaxe, an empty bucket and no water bucket, the water kept, by day, the head not under the sky, nothing
 * remembered underground, at most OB_TRIP_CLIMBS an episode (the sky trip's spacing between) */
static int ob_bk_climb_due(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s)
{
    const struct obs_env *o = &e->ob;
    if (!OB_TRIP_BK || !o->sw_ok || o->sky_trips >= OB_TRIP_CLIMBS || e->steps < o->sky_off || !r->on_ground ||
        s->eye_water || inv_count(s, DIAMOND_PICK) > 0 || inv_count(s, BUCKET) == 0 || inv_count(s, WATER_BUCKET) > 0 ||
        r->wtime % 24000 >= OB_TRIP_DAY)
        return 0;
    if (ob_mem_pick(v, (struct vec_env *)e, s, 0) >= 0) return 0;
    return obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z)) <
           OB_SKY_LIGHT;
}

static int ob_handoff(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (!(OB_EARLY_FILL || OB_TRIP) || !(v->macros & NETH_VEC_OBSIDIAN) || e->age % OB_HANDOFF_EVERY != 0) return 0;
    if (inv_count(s, DIAMOND_PICK) > 0 || !ob_want(v, e, r, s) || r->screen != PS_NONE || s->eye_water ||
        !r->on_ground || e->steps < e->ob.off || e->steps < e->ob.calm)
        return 0;
    if (ob_trip_due(v, e, r, s) || ob_bk_climb_due(v, e, r, s)) return 1;   /* lane/obsid: obsidian_water_trip */
    ob_look(v, e, r, s);
    return e->ob.hit[OV_WATER].d > 0.0f;
}

/* lane/obsidian2: strip's line: the feet's height the strip stairs down to: OB_SEARCH_Y while it is the search's (its
 * tunnel at y 12 had its floor one block over the lava lakes at y 10: 18 of 45 first deaths in two kit cases were at two
 * such lakes), NETH_VEC_STRIP_Y otherwise */
static int ob_strip_y(const struct neth_vec *v, const struct vec_env *e)
{
    if (OB_DEEP) return NETH_VEC_STRIP_Y;
    return e->ob.search && !e->ob.sc_strip ? OB_SEARCH_Y : NETH_VEC_STRIP_Y;
}

/* lane/obsidian2: strip's line: the search's strip turns at hot lava ahead (OV_HOT, on the look that saw it) */
static int ob_hot(const struct neth_vec *v, const struct vec_env *e)
{
    if (OB_DEEP) return 0;
    return e->ob.search && e->ob.scan_at == e->steps && e->ob.hit[OV_HOT].d > 0.0f;
}

/* lane/obsid2 (obsidian_glance walk): strip's line, its walk into the next cell: under the search's strip with the
 * water held, each cell walked into with the look yawed OB_GLANCE_YAW off the heading, left and right in turn, and the
 * strafe key that keeps the walk on the heading (forward and left walk 45 degrees left of the look); once the look has
 * turned, a scan from it. The side cave's lava beside a tunnel is out of the forward look's cone (the t10 case: in view
 * in 15 of 32, cast in 8); a separate glance job made it worse (it ended the strip's step, lane/obsid) */
static void ob_glance(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, struct neth_rl_act *a,
                      double yaw)
{
    if (!MOPT(MO_OB_GLANCE) || !e->ob.search || !e->ob.sc_strip) return;
    int side = e->ob.gl_n & 1 ? 1 : -1;
    float gy = (float)(yaw + side * OB_GLANCE_YAW);
    a->look[0] = gy;
    a->look[1] = OB_GLANCE_PITCH;
    a->hold = (1u << PK_FORWARD) | (1u << (side > 0 ? PK_LEFT : PK_RIGHT));
    if (!e->ob.gl_done && fabs(remainder((double)r->yaw - gy, 360.0)) < OB_GLANCE_SETTLE &&
        fabs((double)r->pitch - OB_GLANCE_PITCH) < OB_GLANCE_SETTLE)
    {
        e->ob.scan_at = -1;                                /* the next ob_look (the strip's want line) scans */
        e->ob.gl_done = 1;
    }
}

/* the strip walked into its cell: the next one's glance to the other side */
static void ob_glance_walked(struct vec_env *e)
{
    e->ob.gl_n += 1;
    e->ob.gl_done = 0;
}

/* a job begun (its target in e->ob's tx, ty, tz) */
/* lane/obsid: a cast's pool: the lava sources joined to (x, y, z) as the client's world shows them (the lava the eye has
 * seen), at most OB_POOL_CAP cells walked; *cells the lava cells */
static int ob_pool(const struct neth_vec *v, const struct vec_env *e, int x0, int y0, int z0, int *cells)
{
    int32_t q[OB_POOL_CAP][3];
    int nq = 0, n = 0;
    q[nq][0] = x0, q[nq][1] = y0, q[nq][2] = z0, ++nq;
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    for (int i = 0; i < nq; ++i)
    {
        int m = -1, b = ob_block(v, e, q[i][0], q[i][1], q[i][2], &m);
        if (!ob_lava(b)) continue;
        if (m == 0) ++n;
        for (int k = 0; k < 6 && nq < OB_POOL_CAP; ++k)
        {
            int x = q[i][0] + D[k][0], y = q[i][1] + D[k][1], z = q[i][2] + D[k][2], seen = 0;
            for (int j = 0; j < nq && !seen; ++j) seen = q[j][0] == x && q[j][1] == y && q[j][2] == z;
            if (!seen && ob_lava(ob_block(v, e, x, y, z, NULL))) q[nq][0] = x, q[nq][1] = y, q[nq][2] = z, ++nq;
        }
    }
    if (cells) *cells = nq;
    return n;
}

/* lane/obsid (obsidian_pool_min on): a cast's pool too small (fewer than OB_POOL_MIN sources in view): given up (its
 * cell bad a while, the strip moves on) */
static int ob_pool_small(const struct neth_vec *v, struct vec_env *e, int at_pour)
{
    /* lane/obsid2 (obsidian_pool_min near): judged at the pour, from where the cast stands, not where the lava was first
     * seen (from a tunnel's edge 1 to 3 of a side cave's 10 sources show: the pool given up, the strip walked on) */
    int m = MOPT(MO_OB_POOL_MIN);
    if (!m || (m == 2) != at_pour || ob_pool(v, e, e->ob.tx, e->ob.ty, e->ob.tz, NULL) >= OB_POOL_MIN) return 0;
    e->ob.pool_skip += 1;
    ob_give_up(e);
    return 1;
}

static void ob_begin(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     int job)
{
    (void)v;
    e->ob.bk.jobs[OW_MINE + job] += 1;
    if (job == OJ_CAST) ob_fn(v, e, OF_CAST);              /* lane/obsid's funnel */
    if (job == OJ_CAST && MOPT(MO_OB_RESUME) && !e->ob.rl_on && e->sset)
    {
        /* lane/obsid2 (obsidian_resume line): the strip's line kept for after the pool */
        e->ob.rl_on = 1, e->ob.rl_x = (int)floor(r->x), e->ob.rl_z = (int)floor(r->z), e->ob.rl_dir = e->sdir;
        e->ob.rl_at = e->steps;
    }
    e->kind = M_MINE;
    e->ob.on = 1;
    e->ob.job = job;
    e->ob.phase = 0;
    e->ob.at = e->age;
    e->ob.uses = 0;
    e->ob.gain = e->age;
    e->ob.stuck = 0;
    e->ob.ar_until = 0;                                    /* lane/obsid2: no side step carried over */
    if (job != OJ_CLIMB) e->ob.cy = -1;                   /* the stair down's state (the climb sets its own) */
    e->ob.sky = 0;                                         /* lane/obsidian7: ob_start sets it for the water trip */
    e->ob.best = 1e9;
    e->ob.px = r->x, e->ob.pz = r->z;
    e->ob.before = (int)inv_count(s, job == OJ_MINE ? OBSIDIAN : WATER_BUCKET);
    e->placed = e->pressed = 0;
    OBT(e, r, "start");
#ifdef OB_FUNNEL_TRACE
    if (job == OJ_CAST)
    {
        int nq = 0, n = ob_pool(v, e, e->ob.tx, e->ob.ty, e->ob.tz, &nq);
        fprintf(stderr, "OBPOOL %d %ld sources %d cells %d at %d %d %d d %.1f strip %d dpick %d\n", (int)(e - v->e),
                (long)e->steps, n, nq, e->ob.tx, e->ob.ty, e->ob.tz,
                py_dist3(r->x, r->y + 1.62, r->z, e->ob.tx + 0.5, e->ob.ty + 0.5, e->ob.tz + 0.5), e->ob.sc_strip,
                (int)inv_count(s, DIAMOND_PICK));
    }
#endif
}

/* macro_start's line: 1 when the macro starts (the mine macro's kind) */
static int ob_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    /* (lane/bportal: e->ob.why, the answer's books, written at each return; nothing reads it) */
    e->ob.why_step = e->steps;
    if (!ob_want(v, e, r, s)) return e->ob.why = OBS_NOWANT, 0;
    if (r->screen != PS_NONE) return e->ob.why = OBS_SCREEN, 0;
    if (s->eye_water) return e->ob.why = OBS_WATER, 0;
    if (e->steps < e->ob.off) return e->ob.why = OBS_REST, 0;
    if (e->steps < e->ob.calm) return e->ob.why = OBS_CALM, 0;
    if (r->dim == -1)
    {
        /* lane/bportal (obsidian_carry frame): the arrival frame's next cell, the MINE job on it; none left: over */
        int x, y, z, left;
        if (!r->on_ground || ob_in_portal(v, e, r)) return e->ob.why = OBS_NOJOB, 0;
        if (ob_frame_cell(v, e, r, &x, &y, &z, &left) < 0)
        {
            if (left == 0) e->ob.fr_done = 1;
            return e->ob.why = OBS_NOJOB, 0;
        }
        e->ob.sc_strip = 0;
        e->ob.tx = x, e->ob.ty = y, e->ob.tz = z;
        e->ob.fr_jobs += 1;
        e->ob.why = OBS_GO;
        ob_begin(v, e, r, s, OJ_MINE);
        return 1;
    }
    e->ob.sc_strip = OB_SC_ON(s);                         /* lane/obsid: obsidian_strip_cast */
    /* lane/obsidian7 (obsidian_food hunt): a food animal in view and the food level low: the hunt's step */
    if (MOPT(MO_OB_FOOD) && r->food <= OB_FOOD_LEVEL && hunt_want(v, e, s) >= 0) return e->ob.why = OBS_HUNT, 0;
    ob_look(v, e, r, s);
    int job = ob_pick(v, e, s), sky = 0;
    /* lane/obsidian7: obsidian_water sky: an empty bucket, no water in view or remembered, the head not under the sky:
     * the water trip, the climb with no height to stop at (ob_climb ends it at water in view or the sky) */
    /* (lane/obsid: the diamond pickaxe held: with obsidian_early_fill on the macro wants an empty bucket filled before
     * it, and a trip then took the strip off the diamonds: the kit in 1 and 2 of 32 envs against 5 and 3) */
    if (((OB_SKY_WATER && inv_count(s, DIAMOND_PICK) > 0 && job < 0 && r->on_ground && e->steps >= e->ob.sky_off &&
          inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0 && ob_mem_pick(v, e, s, 0) < 0 &&
          obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z)) <
              OB_SKY_LIGHT)) ||
        (job < 0 && ob_bk_climb_due(v, e, r, s)))
    {
        job = OJ_CLIMB, sky = 1;
        e->ob.cdir = ((int)floor(((double)r->yaw + 45.0) / 90.0) % 4 + 4) % 4;
        e->ob.turns = 0;
        e->ob.cy = -1;
        e->ob.climb_to = 256;
    }
    /* lane/obsidian2: nothing in view below the lava layer, the diamond pickaxe held: the climb, a stair at a time,
     * to OB_SEARCH_Y, where the search's strip takes over */
    if (!OB_DEEP && job < 0 && inv_count(s, DIAMOND_PICK) > 0 && (int)floor(r->y + 0.01) < OB_SEARCH_Y && r->on_ground &&
        !e->ob.sc_strip)
    {
        job = OJ_CLIMB;
        e->ob.cdir = ((int)floor(((double)r->yaw + 45.0) / 90.0) % 4 + 4) % 4;
        e->ob.turns = 0;
        e->ob.cy = -1;
        e->ob.climb_to = 0;     /* lane/obsidian5: to OB_SEARCH_Y (the bucket portal's climb to the sky sets 256) */
    }
    /* lane/obsidian2: nothing in view and the look around due: it goes first (the view is a 75-degree cone; a player
     * turns around) */
    if (!OB_DEEP && job < 0 && inv_count(s, DIAMOND_PICK) > 0 && ob_look_due(v, e, r) && r->on_ground)
    {
        job = OJ_LOOK;
        if (e->ob.pc_look > 0) e->ob.pc_look -= 1, e->ob.lk_set = e->ob.pc_look == 0;   /* lane/obsid */
        else e->ob.lk_set = 1;
        e->ob.lkx = r->x, e->ob.lkz = r->z;
        e->ob.lk_yaw = r->yaw;
    }
    if (job < 0 && r->on_ground) job = ob_mem_pick(v, e, s, 1);   /* lane/obsidian2: a remembered source */
    /* lane/flint (obsidian_walk dig): past the walk's reach the strip's walk goes to it, no job yet */
    if (job >= 0 && e->ob.remem && ob_wk_take(v, e, r, e->ob.tx, e->ob.ty, e->ob.tz, job)) job = -1;
    /* lane/obsid (obsidian_water scout): an empty bucket, nothing in view or remembered, the head under the sky by day:
     * the walk over the surface for water */
    if (OB_SCOUT && job < 0 && r->on_ground && !s->eye_water && inv_count(s, DIAMOND_PICK) > 0 &&
        inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0 && e->steps >= e->ob.sc_off &&
        e->ob.sc_n < OB_SCOUT_N && r->wtime % 24000 < 12500 && r->y >= OB_SCOUT_Y &&
        obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z)) >=
            OB_SKY_LIGHT)
    {
        job = OJ_SCOUT;
        e->ob.sc_n += 1;
        e->ob.sc_k = 0, e->ob.sc_leg = 0, e->ob.sc_have = 0, e->ob.sc_det = 0, e->ob.sc_stuck = 0, e->ob.sc_pil = 0;
        e->ob.sc_yaw = r->yaw;
        e->ob.sc_y0 = r->y;
        e->ob.sc_x = e->ob.sc_lx = e->ob.sc_mx = e->ob.sc_gx = r->x;
        e->ob.sc_z = e->ob.sc_lz = e->ob.sc_mz = e->ob.sc_gz = r->z;
        e->ob.sc_mt = e->steps;
    }
    /* lane/obsid (obsidian_water_trip on): nothing in view, the surface water kept in reach: the walk to it (the scout's
     * walk to the water it found) */
    if (job < 0 && ob_trip_due(v, e, r, s))
    {
        job = OJ_SCOUT;
        e->ob.sw_trips += 1, e->ob.sc_trip = 1;
        e->ob.sc_k = -1, e->ob.sc_leg = 0, e->ob.sc_have = 1, e->ob.sc_det = 0, e->ob.sc_stuck = 0, e->ob.sc_pil = 0;
        e->ob.sc_wx = e->ob.sw_x, e->ob.sc_wy = e->ob.sw_y, e->ob.sc_wz = e->ob.sw_z;
        e->ob.sc_yaw = r->yaw;
        e->ob.sc_y0 = r->y;
        e->ob.sc_x = e->ob.sc_lx = e->ob.sc_mx = e->ob.sc_gx = r->x;
        e->ob.sc_z = e->ob.sc_lz = e->ob.sc_mz = e->ob.sc_gz = r->z;
        e->ob.sc_mt = e->steps;
#ifdef OB_FUNNEL_TRACE
        fprintf(stderr, "OBTRIP %d %ld start water %d %d %d at %.1f %.1f %.1f dpick %d\n", (int)(e - v->e),
                (long)e->steps, e->ob.sw_x, e->ob.sw_y, e->ob.sw_z, r->x, r->y, r->z, (int)inv_count(s, DIAMOND_PICK));
#endif
    }
#ifdef OB_TRACE
    /* lane/flint: a trace build's no-job line, every 50 steps with a water bucket held: the nearest remembered lava (its
     * cell, its distance from the eye, the client world's blocks from its cell up to the feet's level) and the lava in
     * view's distance (-1 none) */
    if (job < 0 && e->steps % 50 == 0 && inv_count(s, WATER_BUCKET) > 0)
    {
        int bi = -1, fy = (int)floor(r->y + 0.01);
        double bd = 1e9;
        for (int i = 0; i < e->ob.nmem[1] && i < OB_MEM; ++i)
        {
            double d = py_dist3(r->x, r->y + 1.62, r->z, e->ob.mem[1][i].x + 0.5, e->ob.mem[1][i].y + 0.5,
                                e->ob.mem[1][i].z + 0.5);
            if (d < bd) bd = d, bi = i;
        }
        fprintf(stderr, "OBN %d %ld at %.1f %.1f %.1f yaw %.0f sc %d inview %.1f mem %d", (int)(e - v->e), (long)e->steps,
                r->x, r->y, r->z, (double)r->yaw, e->ob.sc_strip, e->ob.hit[OV_LAVA].d, e->ob.nmem[1]);
        if (bi >= 0)
        {
            int x = e->ob.mem[1][bi].x, y = e->ob.mem[1][bi].y, z = e->ob.mem[1][bi].z;
            fprintf(stderr, " near %d %d %d d %.1f dy %d col", x, y, z, bd, y - fy);
            for (int k = y; k <= fy + 1 && k <= y + 6; ++k)
                fprintf(stderr, " %d", obsview_block(v->p, (int)(e - v->e), x, k, z, NULL));
        }
        fprintf(stderr, "\n");
    }
#endif
    if (job < 0) return e->ob.why = OBS_NOJOB, 0;
    if (job == OJ_CAST && ob_cast_far(v, e, r)) return e->ob.why = OBS_CAST_FAR, 0;  /* lane/obsidian7: the search's
                                                                                         strip goes to it */
    if (job == OJ_CAST && ob_pool_small(v, e, 0)) return e->ob.why = OBS_POOL_SMALL, 0;  /* obsidian_pool_min */
    e->ob.why = OBS_GO;
    ob_begin(v, e, r, s, job);
    if (sky) e->ob.sky = 1, e->ob.sky_trips += 1;
    return 1;
}

/* a block a player stands on or bumps into (not air, a liquid, a plant, fire, snow or a torch) */
static int ob_solid(int b)
{
    return b > 0 && !(b >= 8 && b <= 11) && b != 6 && b != 31 && b != 32 && b != 37 && b != 38 && b != 39 && b != 40 &&
           b != 50 && b != 51 && b != 78 && b != 83 && b != 106;
}

/* the walk's next cell (the feet's, a step along the look's yaw): 0 unsafe (lava in it, beside it, over it or in the
 * two cells under it), 1 a flat step (its floor solid), 2 a one-block step up (a block at the feet's height, the two
 * cells over it free), 3 a drop (no floor: the crouch holds the player at the edge) */
static int ob_step(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double yaw)
{
    double yr = yaw * M_PI / 180.0;
    int nx = (int)floor(r->x - sin(yr) * 0.8), nz = (int)floor(r->z + cos(yr) * 0.8), fy = (int)floor(r->y + 0.01);
    static const int D[8][3] = {{0, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, -2, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int k = 0; k < 8; ++k)
        if (ob_lava(ob_block(v, e,nx + D[k][0], fy + D[k][1], nz + D[k][2], NULL))) return 0;
    if (ob_lava(ob_block(v, e,nx, fy + 2, nz, NULL))) return 0;
    int feet = ob_block(v, e,nx, fy, nz, NULL), floor_ = ob_block(v, e,nx, fy - 1, nz, NULL);
    if (ob_solid(feet))
        return !ob_solid(ob_block(v, e,nx, fy + 1, nz, NULL)) &&
                       !ob_solid(ob_block(v, e,nx, fy + 2, nz, NULL)) &&
                       !ob_solid(ob_block(v, e,(int)floor(r->x), fy + 2, (int)floor(r->z), NULL))
                   ? 2
                   : 1;
    return ob_solid(floor_) ? 1 : 3;
}

/* lane/obsid2 (obsidian_walk around): a walk toward (px, pz) held OB_AROUND_STUCK steps without moving (the straight
 * line crosses a corner: from a 1x2 tunnel to a pool in a side cave, the mine's walk stood at the tunnel's wall for 100
 * steps a target and gave up, and the obsidian it had dug lay in its pit unpicked, 4 mined and 0 held): a side step along
 * a cardinal direction toward the target whose cell is free (the feet's and the head's cells open, a floor, no lava in
 * reach of it: ob_step's rule), the larger of the two first, walked with the keys that move that way under the look (the
 * look stays on the target), until the feet's cell changes or OB_AROUND_AGE steps. 1 when it set the keys */
static int ob_around(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, struct neth_rl_act *a,
                     double look_yaw, double px, double pz, int sneak)
{
    struct obs_env *o = &e->ob;
    if (!MOPT(MO_OB_WALK) || !r->on_ground) return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (o->ar_until > e->age && (fx != o->ar_cx || fz != o->ar_cz)) o->ar_until = 0;   /* the side step made */
    if (o->ar_until <= e->age)
    {
        if (o->stuck < OB_AROUND_STUCK) return 0;
        double dx = px - r->x, dz = pz - r->z;
        int cand[2], nc = 0;
        int ddx = dx > 0.3 ? 3 : dx < -0.3 ? 1 : -1, ddz = dz > 0.3 ? 0 : dz < -0.3 ? 2 : -1;   /* OB_DX/OB_DZ's */
        if (fabs(dx) >= fabs(dz)) { if (ddx >= 0) cand[nc++] = ddx; if (ddz >= 0) cand[nc++] = ddz; }
        else { if (ddz >= 0) cand[nc++] = ddz; if (ddx >= 0) cand[nc++] = ddx; }
        int pick = -1;
        for (int i = 0; i < nc && pick < 0; ++i)
        {
            int d = cand[i], nx = fx + OB_DX[d], nz = fz + OB_DZ[d];
            if (ob_solid(ob_block(v, e, nx, fy, nz, NULL)) || ob_solid(ob_block(v, e, nx, fy + 1, nz, NULL)) ||
                !ob_solid(ob_block(v, e, nx, fy - 1, nz, NULL)) || ob_step(v, e, r, 90.0 * d) == 0)
                continue;
            pick = d;
        }
        if (pick < 0) return 0;
        o->ar_dir = pick, o->ar_cx = fx, o->ar_cz = fz, o->ar_until = e->age + OB_AROUND_AGE, o->ar_n += 1;
        o->stuck = 0;
    }
    /* the keys whose walk is nearest the side step's direction under the look: forward 0, forward and right 45, right
     * 90, back and right 135, back 180, and the left side's the negatives (forward and left walk 45 left of the look) */
    double rel = remainder(90.0 * o->ar_dir - look_yaw, 360.0);
    int k = (int)lround(rel / 45.0);
    static const uint32_t KEYS[9] = {
        (1u << PK_BACK) | 0, (1u << PK_BACK) | (1u << PK_LEFT), 1u << PK_LEFT, (1u << PK_FORWARD) | (1u << PK_LEFT),
        1u << PK_FORWARD, (1u << PK_FORWARD) | (1u << PK_RIGHT), 1u << PK_RIGHT, (1u << PK_BACK) | (1u << PK_RIGHT),
        1u << PK_BACK};
    a->hold = KEYS[k + 4 < 0 ? 0 : k + 4 > 8 ? 8 : k + 4] | (sneak ? 1u << PK_SNEAK : 0);
    return 1;
}

/* the item's hotbar slot (fetched from the main inventory when it is not there): 1 once it is current */
static int ob_hold(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int item, int *rc)
{
    int hb;
    return pp_select(v, e, r, s, a, item, &hb, rc);
}

/* the douse: burning in the overworld with a water bucket held, out of water: the water poured at the feet (the look
 * straight down, the bucket's ray passing any lava to the floor), as a speedrunner does; the fire goes out in it and
 * the lava sources it touches turn to obsidian. decode's line: it ends any macro, the lava escape too (a burning player
 * the strip had walked on burned to death 15 hearts after leaving the lava) */
static int ob_douse_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    return (v->macros & (NETH_VEC_OBSIDIAN | NETH_VEC_BPORTAL)) && r->fire && r->dim == 0 && !s->eye_water && r->screen == PS_NONE &&
           !(r->flags & PF_DEAD) && inv_count(s, WATER_BUCKET) > 0 && !(e->ob.on && e->ob.job == OJ_DOUSE);
}

static void ob_douse_begin(struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    e->kind = M_MINE;
    e->ob.on = 1;
    e->ob.job = OJ_DOUSE;
    e->ob.bk.jobs[OW_DOUSE] += 1;
    e->ob.phase = 0;
    e->ob.at = 0;
    e->ob.uses = (int)lround((double)r->yaw);
    e->ob.before = (int)inv_count(s, WATER_BUCKET);
    e->ob.calm = e->steps + 240;
    e->placed = e->pressed = 0;
}

/* lane/obsidian2: lava in a cell or beside it (its six neighbours) */
static int ob_lava_near(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int k = 0; k < 7; ++k)
        if (ob_lava(ob_block(v, e,x + D[k][0], y + D[k][1], z + D[k][2], NULL))) return 1;
    return 0;
}

/* lane/obsidian7: water in a cell, over it or beside it (the climb's next cell, read as its lava rule reads lava) */
static int ob_water_over(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int k = 0; k < 7; ++k)
    {
        int b = ob_block(v, e, x + D[k][0], y + D[k][1], z + D[k][2], NULL);
        if (b == 8 || b == 9) return 1;
    }
    return 0;
}

/* lane/obsidian7 (obsidian_drop on): strip's lines: the deeper floor look is the search's (its strip, the option on),
 * and a landing whose feet cell or a cell beside it holds lava is not taken */
static int ob_drop_ok(const struct neth_vec *v, const struct vec_env *e)
{
    return MOPT(MO_OB_DROP) && e->ob.search;
}

static int ob_drop_lava(const struct neth_vec *v, const struct vec_env *e, int x, int floor_y, int z)
{
    return ob_lava_near(v, e, x, floor_y + 1, z);
}

static void ob_climb_turn(struct vec_env *e)
{
    e->ob.cdir = (e->ob.cdir + 1) & 3;
    e->ob.turns += 1;
    e->ob.cy = -1;
}

/* lane/obsidian2: the climb (OJ_CLIMB): a staircase up along a cardinal direction until the feet stand at
 * OB_SEARCH_Y. A stair from the feet's cell B, the next column N along the direction: the cell over the head (B + 2,
 * the jump's room), N + 2 and N + 1 cleared with the best tool, N's floor (N + 0) solid, then the step up (forward and
 * a jump). A quarter turn on: lava in or beside a cell to clear or the floor (never opened), bedrock or obsidian in
 * the way, no floor, hot lava ahead in view, or a stair over OB_CLIMB_STAIR steps; OB_CLIMB_TURNS turns with no stair
 * taken: the macro rests OB_CLIMB_OFF steps. 1 when over */
static int ob_climb(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct obs_env *o = &e->ob;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (fy >= (o->climb_to ? o->climb_to : OB_SEARCH_Y)) return 1;
    if (o->turns >= OB_CLIMB_TURNS)
    {
        o->off = e->steps + OB_CLIMB_OFF;
        if (o->sky) o->sky_off = e->steps + OB_SKY_OFF, o->sky = 0;
        OBT(e, r, "climb-rest");
        return 1;
    }
    ob_look(v, e, r, s);
    if (o->sky)
    {
        /* lane/obsidian7: the water trip: over with water in view (the fill's), or the head under the sky (the look
         * around's) */
        int sky = obsview_sky(v->p, (int)(e - v->e), fx, fy + 1, fz) >= OB_SKY_LIGHT && r->on_ground;
        if (o->hit[OV_WATER].d > 0.0f || sky)
        {
            o->sky_water += o->hit[OV_WATER].d > 0.0f;
            o->sky = 0, o->climb_to = 0;
            o->sky_off = e->steps + OB_SKY_OFF;
            o->lk_set = 0;                                  /* the look around due where it stands */
            OBT(e, r, "sky-over");
            return 1;
        }
    }
    if (o->scan_at == e->steps && o->hit[OV_HOT].d > 0.0f && o->cy < 3)
    {
        OBT(e, r, "climb-hot");
        ob_climb_turn(e);
        return 0;
    }
    if (o->cy < 0)
    {
        if (!r->on_ground) return 0;
        o->bx = fx, o->by = fy, o->bz = fz;
        o->cy = 0;
        o->at = e->age;
        o->phase = e->age;                                  /* the look's settle */
    }
    int nx = o->bx + OB_DX[o->cdir], nz = o->bz + OB_DZ[o->cdir];
    if (e->age - o->at > OB_CLIMB_STAIR)
    {
        OBT(e, r, "climb-slow");
        ob_climb_turn(e);
        return 0;
    }
    a->look_mode = 1;
    double yaw, pitch;
    if (o->cy < 3)
    {
        int cx = o->cy == 0 ? o->bx : nx, cz = o->cy == 0 ? o->bz : nz, cyy = o->cy == 2 ? o->by + 1 : o->by + 2;
        if (ob_lava_near(v, e, cx, cyy, cz) || ob_lava_near(v, e, nx, o->by, nz))
        {
            OBT(e, r, "climb-lava");
            ob_climb_turn(e);
            return 0;
        }
        /* lane/obsidian7: the water trip never opens a cell under water (a sea's floor: it floods the stair) */
        if (o->sky && (ob_water_over(v, e, cx, cyy, cz) || ob_water_over(v, e, nx, o->by, nz)))
        {
            OBT(e, r, "climb-water");
            ob_climb_turn(e);
            return 0;
        }
        look_at(r->x, ey, r->z, cx + 0.5, cyy + 0.5, cz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->age - o->phase < 2) return 0;
        if (s->mo_hit && s->mo_x == cx && s->mo_y == cyy && s->mo_z == cz)
        {
            int b = s->mo_block;
            /* lane/obsidian7: the water trip never digs sand, gravel or clay over the stair (a sea's or a lake's floor:
             * seed 13's start lies under an ocean; strip3's st3_lakebed rule) */
            if (b == 7 || b == OBSIDIAN || (o->sky && st3_lakebed(b)))
            {
                OBT(e, r, "climb-bed");
                /* lane/obsidian7 (obsidian_water sky, obsidian_sea stop): a sea floor over the stair ends the trip (s13's:
                 * the turn dug the next column's head cell, under the same gravel, and the sea came in), no trip again
                 * for OB_SKY_BED steps (the search's strip moves out from under it) */
                if (o->sky && st3_lakebed(b) && MOPT(MO_OB_SEA))
                {
                    o->sky = 0, o->climb_to = 0, o->sky_off = e->steps + OB_SKY_BED;
                    o->off = e->steps + OB_CLIMB_OFF;
                    return 1;
                }
                ob_climb_turn(e);
                return 0;
            }
            a->hold = 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, b);
            if (best >= 0 && best != s->current) a->hotbar = best;
            return 0;
        }
        o->cy += 1;                                         /* the crosshair passes the cell: clear */
        o->phase = e->age;
        if (o->cy == 3 && !ob_solid(ob_block(v, e,nx, o->by, nz, NULL)))
        {
            OBT(e, r, "climb-nofloor");
            ob_climb_turn(e);
        }
        return 0;
    }
    /* the step up */
    if (fx == nx && fz == nz && fy == o->by + 1 && r->on_ground)
    {
        o->turns = 0;
        o->cy = -1;
        return 0;
    }
    look_at(r->x, ey, r->z, nx + 0.5, o->by + 1.6, nz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    a->hold = 1u << PK_FORWARD;
    if (r->on_ground) a->hold |= 1u << PK_JUMP;
    return 0;
}

/* lane/obsidian2: the cast's stair down toward its lava (OJ_CAST, out of reach below): from the feet's cell B, the next
 * column N along the cardinal direction nearest the target, N + 1, N and N - 1 cleared, N - 2 solid (the new floor),
 * then the step forward and down. Never a cell with lava in it or beside it, nor a floor with lava beside it: then
 * the target is given up. 0 driving, 1 over (given up), -1 not now (the target's column reached: the walk's) */
static int ob_descend(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey)
{
    struct obs_env *o = &e->ob;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (o->cy < 0 || o->cy > 3 || (o->bx == fx && o->bz == fz && o->by != fy) ||
        !((o->bx == fx && o->bz == fz) || (o->bx + OB_DX[o->cdir] == fx && o->bz + OB_DZ[o->cdir] == fz)))
    {
        if (!r->on_ground) return 0;
        int dx = o->tx - fx, dz = o->tz - fz;
        if (dx == 0 && dz == 0) return -1;
        o->cdir = abs(dx) >= abs(dz) ? (dx > 0 ? 3 : 1) : (dz > 0 ? 0 : 2);
        o->bx = fx, o->by = fy, o->bz = fz;
        o->cy = 0;
        o->ds_at = e->age;      /* lane/obsid2: its own settle (it was the cast's phase: the rim lost, a source poured over) */
        o->gain = e->age;
    }
    int nx = o->bx + OB_DX[o->cdir], nz = o->bz + OB_DZ[o->cdir];
    if (nx == o->tx && nz == o->tz) return -1;              /* never into the lava's own column */
    double yaw, pitch;
    a->look_mode = 1;
    if (o->cy < 3)
    {
        int cyy = o->by + 1 - o->cy;
        if (ob_lava_near(v, e, nx, cyy, nz) || ob_lava_near(v, e, nx, o->by - 2, nz))
        {
            OBT(e, r, "descend-lava"), ob_give_up(e);
            return 1;
        }
        look_at(r->x, ey, r->z, nx + 0.5, cyy + 0.5, nz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->age - o->ds_at < 2) return 0;
        if (s->mo_hit && s->mo_x == nx && s->mo_y == cyy && s->mo_z == nz)
        {
            int b = s->mo_block;
            if (b == 7 || b == OBSIDIAN)
            {
                OBT(e, r, "descend-hard"), ob_give_up(e);
                return 1;
            }
            a->hold = 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, b);
            if (best >= 0 && best != s->current) a->hotbar = best;
            o->gain = e->age;
            return 0;
        }
        o->cy += 1;
        o->ds_at = e->age;
        if (o->cy == 3 && !ob_solid(ob_block(v, e,nx, o->by - 2, nz, NULL)))
        {
            OBT(e, r, "descend-nofloor"), ob_give_up(e);
            return 1;
        }
        return 0;
    }
    if (fx == nx && fz == nz && fy == o->by - 1 && r->on_ground)
    {
        o->cy = -1;                                         /* the next stair */
        o->gain = e->age;
        return 0;
    }
    look_at(r->x, ey, r->z, nx + 0.5, o->by - 0.5, nz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    a->hold = 1u << PK_FORWARD;
    return 0;
}

/* lane/obsidian7 (obsidian_cast_rim on): the cast's rim, the nearest of the four blocks beside its lava that is solid
 * with an open cell over it (the water's), into rx, ry, rz; 0 none (the cast as before: into the lava's cell) */
static int ob_rim(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, int *rx, int *ry, int *rz)
{
    const struct obs_env *o = &e->ob;
    double best = 1e9, ey = r->y + 1.62;
    int found = 0;
    for (int k = 0; k < 4; ++k)
    {
        int x = o->tx + OB_DX[k], z = o->tz + OB_DZ[k];
        int b = ob_block(v, e, x, o->ty, z, NULL), up = ob_block(v, e, x, o->ty + 1, z, NULL);
        if (!ob_solid(b) || up != 0) continue;
        double d = py_dist3(r->x, ey, r->z, x + 0.5, o->ty + 1.0, z + 0.5);
        if (d < best) best = d, *rx = x, *ry = o->ty, *rz = z, found = 1;
    }
    return found;
}

/* the macro's drive: 1 when over */
/* lane/obsid (obsidian_water scout): OJ_SCOUT's step, 1 when over. Under the sky by day: a far look (OB_SCOUT_LOOKS
 * yaws, obsview_far: the columns' tops in OB_SCOUT_FAR) on the spot, then a leg of the square spiral walked upright
 * (sprinting with food over 6, a jump where a step does not move, 45 degrees off where a leg is blocked), a far look
 * every OB_SCOUT_LOOK_D blocks; water seen: remembered and walked to until OB_SCOUT_NEAR, where the fill takes it (the
 * memory's pick or the look). Over at night, under OB_SCOUT_Y, short of air, or after OB_SCOUT_MAX steps */
/* the scout's pillar here: cobblestone to spare, a solid floor, OB_SCOUT_PILLAR + 2 cells of air over the head */
static int ob_pillar_ok(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s)
{
    if (inv_count(s, COBBLE) < OB_SCOUT_PILLAR + OB_SCOUT_KEEP || !r->on_ground) return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (!ob_solid(ob_block(v, e, fx, fy - 1, fz, NULL))) return 0;
    for (int dy = 0; dy < OB_SCOUT_PILLAR + 3; ++dy)
    {
        int b = ob_block(v, e, fx, fy + dy, fz, NULL);
        if (b != 0 && b != 18 && b != 161) return 0;          /* air, or leaves (dug through on the way up) */
    }
    return 1;
}

/* the rise (bportal.c bp_pillar_up's): the look straight down, a jump, the cobblestone put on the floor's top face once
 * the feet are a block over it; leaves over the head dug. 1 at the top, -1 given up, 0 rising */
static int ob_pillar_up(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a)
{
    struct obs_env *o = &e->ob;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (r->on_ground) o->sc_stand = fy;
    if (r->on_ground && fy >= o->sc_py + OB_SCOUT_PILLAR) return 1;
    if (e->steps - o->sc_pat > OB_SCOUT_PCAP) return -1;
    a->look_mode = 1;
    int lb = ob_block(v, e, fx, fy + 2, fz, NULL);
    if (lb == 18 || lb == 161)
    {
        a->look[0] = r->yaw, a->look[1] = -90.0f;               /* a leaf over the head: dug */
        a->hold = 1u << PK_ATTACK;
        return 0;
    }
    a->look[0] = r->yaw;
    a->look[1] = 90.0f;
    int hb, rc;
    if (!pp_select(v, e, r, s, a, COBBLE, &hb, &rc)) return rc ? -1 : 0;
    if (r->on_ground)
    {
        a->hold = 1u << PK_JUMP;
        return 0;
    }
    if (r->y >= o->sc_stand + 1.0 && s->mo_hit && s->mo_side == 1 && s->mo_x == fx && s->mo_z == fz &&
        s->mo_y == o->sc_stand - 1)
        a->press[PK_USE] = 1;
    return 0;
}

/* the way down: the block under the feet dug with the best tool. 1 at the base (or given up) */
static int ob_pillar_down(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a)
{
    struct obs_env *o = &e->ob;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if ((fy <= o->sc_py && r->on_ground) || e->steps - o->sc_pat > OB_SCOUT_PCAP) return 1;
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

static int ob_scout(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct obs_env *o = &e->ob;
    /* the pillar: up before the far look, down after it (or once water is seen) */
    if (o->sc_pil == 1)
    {
        int u = ob_pillar_up(v, e, r, s, a);
        if (u != 0) o->sc_pil = 2, o->at = e->age;
        if (u == 0) return 0;
    }
    if (o->sc_pil == 3)
    {
        if (!ob_pillar_down(v, e, r, s, a)) return 0;
        o->sc_pil = 0;
        o->sc_mx = o->sc_gx = r->x, o->sc_mz = o->sc_gz = r->z, o->sc_mt = e->steps;
        o->sc_y0 = r->y < o->sc_y0 ? r->y : o->sc_y0;
    }
    if (o->sc_pil == 2 && (o->sc_have || o->sc_k < 0))
    {
        o->sc_pil = 3, o->sc_pat = e->steps;
        return 0;
    }
    int day = r->wtime % 24000 < 12500 || (o->sc_trip && OB_TRIP_NIGHT);   /* lane/obsid2: obsidian_trip_night */
    int over = r->y < OB_SCOUT_Y || r->y < o->sc_y0 - OB_SCOUT_DROP || !day || (s->eye_water && r->air < OB_SCOUT_AIR) ||
               e->age > OB_SCOUT_MAX;
    if (over && (o->sc_pil == 1 || o->sc_pil == 2) && e->age <= OB_SCOUT_MAX + OB_SCOUT_PCAP)
    {
        o->sc_pil = 3, o->sc_pat = e->steps;               /* down off the pillar first */
        return 0;
    }
    if (over && !(o->sc_pil == 3 && e->age <= OB_SCOUT_MAX + 2 * OB_SCOUT_PCAP))
    {
        /* lane/obsid2's books (counting only): why a trip ended short of its water: under y 50, 10 under its start,
         * night, out of air swimming, its length */
        if (o->sc_trip)
            o->te[r->y < OB_SCOUT_Y ? 0 : r->y < o->sc_y0 - OB_SCOUT_DROP ? 1 : !day ? 2 : e->age > OB_SCOUT_MAX ? 4 : 3] += 1;
        if (o->sc_trip) o->sw_ok = 0, o->sc_trip = 0;      /* lane/obsid: a trip that did not reach its water forgets it */
        o->sc_off = e->steps + OB_SCOUT_OFF;
        o->sc_have = 0;
        OBT(e, r, "scout-end");
        return 1;
    }
    a->hotbar = -1;
    a->look_mode = 1;
    a->look[1] = 0.0f;
    double mv = py_hypot2(r->x - o->sc_gx, r->z - o->sc_gz);
    o->sc_stuck = mv < 0.05 ? o->sc_stuck + 1 : 0;
    o->sc_gx = r->x, o->sc_gz = r->z;
    if (py_hypot2(r->x - o->sc_mx, r->z - o->sc_mz) >= 1.0) o->sc_mx = r->x, o->sc_mz = r->z, o->sc_mt = e->steps;
    double yaw;
    if (o->sc_have)
    {
        double dx = o->sc_wx + 0.5 - r->x, dz = o->sc_wz + 0.5 - r->z;
        if (py_hypot2(dx, dz) <= OB_SCOUT_NEAR)
        {
            if (o->sc_trip) o->sw_fill += 1, o->sc_trip = 0;   /* lane/obsid: the trip at its water */
            o->sc_have = 0;
            o->lk_set = 0;                          /* the look around due here (the fill's from what it sees) */
            return 1;
        }
        yaw = atan2(-dx, dz) * 180.0 / M_PI;
        if (e->steps - o->sc_mt > OB_SCOUT_STUCK) o->sc_det += 1, o->sc_mt = e->steps, o->sc_at = e->steps;
        if (o->sc_det > 0 && e->steps - o->sc_at < 2 * OB_SCOUT_STUCK) yaw += 45.0 * (double)(1 + (o->sc_det - 1) % 3);
    }
    else if (o->sc_k >= 0)
    {
        /* the far look: from a pillar where one stands (OB_SCOUT_PILLAR), each yaw held OB_LOOK_HOLD steps, then the
         * scan from it */
        if (o->sc_k == 0 && o->sc_pil == 0 && ob_pillar_ok(v, e, r, s))
        {
            o->sc_pil = 1, o->sc_pat = e->steps;
            o->sc_py = (int)floor(r->y + 0.01), o->sc_stand = o->sc_py;
            return 0;
        }
        a->look[0] = (float)(o->sc_yaw + (360.0 / OB_SCOUT_LOOKS) * o->sc_k);
        a->look[1] = OB_SCOUT_PITCH;
        if (s->eye_water) a->hold = 1u << PK_JUMP;
        if (e->age - o->at < OB_LOOK_HOLD) return 0;
        struct obsview_hit h[OV_KINDS];
        struct ob_ctx c = {v, e};
        int fr = obsview_far(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, OB_SCOUT_FAR, 1 << OV_WATER, ob_skip,
                             &c, h);
#ifdef OB_TRACE
        fprintf(stderr, "OBT %d %ld far-look k %d rc %d water n %d d %.1f yaw %.0f pitch %.0f at %.1f %.1f %.1f\n",
                (int)(e - v->e), (long)e->steps, o->sc_k, fr, h[OV_WATER].n, h[OV_WATER].d, r->yaw, r->pitch, r->x,
                r->y, r->z);
#endif
        if (fr == 0 && h[OV_WATER].d > 0.0f)
        {
            o->sc_have = 1, o->sc_found += 1;
            o->sc_wx = h[OV_WATER].x, o->sc_wy = h[OV_WATER].y, o->sc_wz = h[OV_WATER].z;
            ob_mem_add(e, 0, o->sc_wx, o->sc_wy, o->sc_wz);
            o->sc_det = 0, o->sc_mt = e->steps;
            OBT(e, r, "scout-water");
            return 0;
        }
        o->at = e->age;
        if (++o->sc_k < OB_SCOUT_LOOKS) return 0;
        o->sc_k = -1;
        o->sc_lx = r->x, o->sc_lz = r->z;
        return 0;
    }
    else
    {
        /* the leg: its end a quarter turn; a far look every OB_SCOUT_LOOK_D blocks */
        if (py_hypot2(r->x - o->sc_x, r->z - o->sc_z) >= OB_SCOUT_LEG * (1.0 + (double)(o->sc_leg / 2)))
        {
            o->sc_leg += 1;
            o->sc_yaw += 90.0;
            o->sc_x = r->x, o->sc_z = r->z;
        }
        if (py_hypot2(r->x - o->sc_lx, r->z - o->sc_lz) >= OB_SCOUT_LOOK_D && r->on_ground)
        {
            o->sc_k = 0;
            o->at = e->age;
            return 0;
        }
        if (e->steps - o->sc_mt > OB_SCOUT_STUCK) o->sc_yaw += 45.0, o->sc_mt = e->steps;   /* blocked: 45 off */
        yaw = o->sc_yaw;
    }
    a->look[0] = (float)yaw;
    a->hold = 1u << PK_FORWARD | (r->food > 6 ? 1u << PK_SPRINT : 0u);
    if (o->sc_stuck > 2 || s->eye_water) a->hold |= 1u << PK_JUMP;
    return 0;
}

static int ob_drive1(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey)
{
    still(a);
    struct obs_env *o = &e->ob;
    if (r->fire) OBT(e, r, "fire");
    if (o->job == OJ_DOUSE)
    {
        if (!r->fire || s->eye_water || r->dim != 0 || inv_count(s, WATER_BUCKET) < o->before || e->age > 44) return 1;
        /* the look: straight down, then each side 45 degrees down (in a deep pool the ray finds no floor within its
         * 5 blocks below, a wall or the pool's edge beside); each held 4 steps, the use on its last */
        int k = o->phase % 5;
        a->look_mode = 1;
        a->look[0] = k == 0 ? r->yaw : (float)(o->uses + 90 * k);
        a->look[1] = k == 0 ? 90.0f : 45.0f;
        if (k == 0) o->uses = (int)lround((double)r->yaw);
        int rc;
        if (!ob_hold(v, e, r, s, a, WATER_BUCKET, &rc)) return rc;
        if (e->age - o->at >= 4)
        {
            a->press[PK_USE] = 1;
            OBT(e, r, "douse");
            o->at = e->age;
            o->phase += 1;
        }
        return 0;
    }
    int fetching = r->screen == PS_INVENTORY;
    if (!ob_want(v, e, r, s) || (r->screen != PS_NONE && !fetching)) return 1;
    {
        /* lava beside the feet or the head, or over the head: the macro gives the target up (the escape rule takes
         * a lava hit) */
        static const int D[9][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {1, 1, 0}, {-1, 1, 0}, {0, 1, 1},
                                    {0, 1, -1}, {0, 2, 0}};
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        for (int k = 0; k < 9; ++k)
            if (ob_lava(ob_block(v, e,fx + D[k][0], fy + D[k][1], fz + D[k][2], NULL)))
            {
                OBT(e, r, "giveup-lava"), ob_give_up(e);
                return 1;
            }
    }
    /* lane/obsidian7: with the water trip on, the fill too (its bucket's ray from under water met the pit's floor) */
    if (o->job == OJ_SCOUT) return ob_scout(v, e, r, s, a, ey);   /* lane/obsid: its own ends (a swim, its length) */
    if (s->eye_water && (o->job != OJ_FILL || OB_SKY_WATER))
    {
        OBT(e, r, "giveup"), ob_give_up(e);
        return 1;
    }
    if (e->age > (o->job == OJ_CLIMB && o->sky ? OB_SKY_AGE : 600))
    {
        if (o->sky) o->sky = 0, o->climb_to = 0, o->sky_off = e->steps + OB_SKY_OFF;
        OBT(e, r, "giveup"), ob_give_up(e);
        return 1;
    }
    if (o->job == OJ_CLIMB) return ob_climb(v, e, r, s, a, ey);
    if (o->job == OJ_LOOK)
    {
        /* the look around: each of OB_LOOKS held OB_LOOK_HOLD steps, then looked (the scan from it); a target in view
         * begins its job on the spot; after the last, over (the search's strip, or the climb) */
        if (o->phase >= 12) return 1;
        a->look_mode = 1;
        a->look[0] = o->lk_yaw + OB_LOOKS[o->phase][0];
        a->look[1] = OB_LOOKS[o->phase][1];
        if (e->age - o->at < OB_LOOK_HOLD) return 0;
        o->scan_at = -1;
        ob_look(v, e, r, s);
#ifdef OB_LOOK_TRACE
        {
            /* lane/obsid2: what the look saw, and the cast target's cell and the four beside it in the client world */
            const struct obsview_hit *hh = o->hit;
            int b0 = obsview_block(v->p, (int)(e - v->e), o->tx, o->ty, o->tz, NULL);
            int b1 = obsview_block(v->p, (int)(e - v->e), o->tx, o->ty + 1, o->tz, NULL);
            fprintf(stderr, "OBLOOK %d %ld ph %d yaw %.0f pitch %.0f obs n %d d %.1f at %d %d %d lava n %d d %.1f water n %d "
                    "t %d %d %d blk %d over %d\n", (int)(e - v->e), (long)e->steps, o->phase, r->yaw, r->pitch,
                    hh[OV_OBSIDIAN].n, hh[OV_OBSIDIAN].d, hh[OV_OBSIDIAN].x, hh[OV_OBSIDIAN].y, hh[OV_OBSIDIAN].z,
                    hh[OV_LAVA].n, hh[OV_LAVA].d, hh[OV_WATER].n, o->tx, o->ty, o->tz, b0, b1);
        }
#endif
        int job = ob_pick(v, e, s);
        o->phase += 1;
        o->at = e->age;
        if (job == OJ_CAST && ob_cast_far(v, e, r)) job = -1;   /* lane/obsidian7: the search's strip goes to it */
        if (job == OJ_CAST && ob_pool_small(v, e, 0)) job = -1; /* lane/obsid: obsidian_pool_min */
        if (job >= 0) ob_begin(v, e, r, s, job);
        return 0;
    }
    int job = o->job, item = job == OJ_MINE ? DIAMOND_PICK : job == OJ_FILL ? BUCKET : WATER_BUCKET;
    int now = (int)inv_count(s, job == OJ_MINE ? OBSIDIAN : WATER_BUCKET);
    if ((job == OJ_MINE || job == OJ_FILL) && now > o->before) return 1;   /* the drop picked up, the bucket filled */
    if (job == OJ_CAST && o->phase < 2 && now < o->before)
    {
        o->phase = 2;                                          /* poured: still while the lava turns */
        o->at = e->age;
    }
    /* the look: obsidian's centre, water's top, lava low in its cell (the floor the water lands on is under it) */
    double px = o->tx + 0.5, py = o->ty + (job == OJ_MINE ? 0.5 : job == OJ_FILL ? 0.85 : 0.3), pz = o->tz + 0.5, yaw, pitch;
    /* lane/obsidian7 (obsidian_cast_rim on): the cast's look on the top of a solid block beside the lava, open over it
     * (the pool's rim): the water poured there stands a block over the pool and runs across it, every lava source it
     * covers hardened (poured into the lava's own cell, it hardened the four beside it, each over more lava in a deep
     * pool: 261 of 261 obsidian refused in s42's case had lava under them) */
    int rx = 0, ry = 0, rz = 0, rim = job == OJ_CAST && o->phase < 2 && MOPT(MO_OB_CAST_RIM) && ob_rim(v, e, r, &rx, &ry, &rz);
    if (rim) px = rx + 0.5, py = ry + 0.98, pz = rz + 0.5;
    look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    if (o->phase == 2)
    {
        if (e->age - o->at > (MOPT(MO_OB_CAST_RIM) ? OB_RIM_WAIT : 20))
        {
            /* lane/obsid (obsidian_strip_cast on): two looks around due on the spot after a cast (the obsidian under
             * the flowing water is not in view until it drains: 22 of 32 envs at a 10-source pool walked off on the
             * strip with none mined) */
            if (o->sc_strip) o->pc_look = 2, o->lk_set = 0;
            return 1;
        }
        return 0;
    }
    int rc = 0;
#ifdef OB_TRACE
    if (r->dim == -1 && e->age % 50 == 0)
        fprintf(stderr, "OBF %d %ld age %d ph %d gain %d best %.2f t %d %d %d hit %d mo %d %d %d blk %d ground %d cur %d "
                "item %d yaw %.1f pitch %.1f at %.2f %.2f %.2f\n", (int)(e - v->e), (long)e->steps, e->age, o->phase,
                o->gain, o->best, o->tx, o->ty, o->tz, s->mo_hit, s->mo_x, s->mo_y, s->mo_z, s->mo_block, r->on_ground,
                s->current, s->inv[s->current].item, r->yaw, r->pitch, r->x, r->y, r->z);
#endif
    if (job == OJ_MINE && o->phase == 1)
    {
        /* dug: the walk into the cell (upright: a crouch stops at the pit the dig left, and the drop on its floor is
         * out of the pickup's reach from its edge; no lava is beside it or under it) until the drop is picked up */
        if (e->age - o->at > 60) return 1;
        double dx = px - r->x, dz = pz - r->z;
        int st = ob_step(v, e, r, yaw);
        if (MOPT(MO_OB_WALK))
        {
            /* lane/obsid2 (obsidian_walk around): the stuck count kept here too (the pit's walk stood at a corner) */
            double mv = py_hypot2(r->x - o->px, r->z - o->pz);
            o->stuck = mv < 0.02 ? o->stuck + 1 : 0;
            o->px = r->x, o->pz = r->z;
            if (dx * dx + dz * dz > 0.09 && ob_around(v, e, r, a, yaw, px, pz, 0)) return 0;
        }
        if (dx * dx + dz * dz > 0.09 && st != 0 && r->on_ground) a->hold |= 1u << PK_FORWARD;
        if (st == 2 && r->on_ground) a->hold |= 1u << PK_JUMP;
        return 0;
    }
    if (o->phase == 1)
    {
        /* a use sent: the server's answer a few steps on (a count that did not move: used again, three at most) */
        if (e->age - o->at < 8) return 0;
        if (o->uses >= 3)
        {
            OBT(e, r, "giveup"), ob_give_up(e);
            return 1;
        }
        o->phase = 0;
    }
    double d = py_dist3(r->x, ey, r->z, px, py, pz);
    if (d < o->best - 0.5)
    {
        o->best = d;
        o->gain = e->age;
    }
    if (e->age - o->gain > 120)
    {
#ifdef OB_TRACE
        fprintf(stderr, "OBG %d %ld job %d d %.1f hd %.1f dy %d stuck %d st %d mo %d %d ground %d best %.1f\n",
                (int)(e - v->e), (long)e->steps, job, d, py_hypot2(px - r->x, pz - r->z),
                o->ty - (int)floor(r->y + 0.01), o->stuck, ob_step(v, e, r, yaw), s->mo_hit ? s->mo_block : -1,
                s->mo_hit ? s->mo_y - (int)floor(r->y + 0.01) : 99, r->on_ground, o->best);
#endif
        OBT(e, r, "giveup"), ob_give_up(e);
        return 1;
    }
    if (job == OJ_MINE)
    {
        int on = s->mo_hit && s->mo_x == o->tx && s->mo_y == o->ty && s->mo_z == o->tz;
        if ((on && s->mo_block != OBSIDIAN) || (o->uses && !on))
        {
            /* dug (the crosshair off the cell it was digging, or the cell no longer obsidian): its drop walked to */
            o->phase = 1;
            o->at = e->age;
            return 0;
        }
        if (s->mo_hit && py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) <= 4.0)
        {
            int mb = s->mo_block;
            /* lane/bportal (obsidian_carry frame): in the Nether the frame's obsidian in the line is dug where it is (the
             * give-up's rest let the Nether macro walk off with 1 of 10: s1's arrivals, 43 give-ups so; moving the
             * target to it fought the look's own pick of the target, step after step, until the age ran out) */
            if (!on && mb == OBSIDIAN && r->dim == -1 &&
                (s->mo_y >= (int)floor(r->y + 0.01) || !ob_frame_pit_hot(v, e, s->mo_x, s->mo_y, s->mo_z)) &&
                ob_frame_dig(v, e, s->mo_x, s->mo_y, s->mo_z))
            {
                if (!ob_hold(v, e, r, s, a, item, &rc)) return rc;
                a->hold = 1u << PK_ATTACK;
                o->gain = e->age;
                return 0;
            }
            if (!on && (mb == 7 || mb == OBSIDIAN))
            {
                /* bedrock or other obsidian in the way */
                OBT(e, r, "giveup"), ob_give_up(e);
                return 1;
            }
            if (on)
            {
                if (!ob_hold(v, e, r, s, a, item, &rc)) return rc;
                a->hold = 1u << PK_ATTACK;
                o->gain = e->age;                              /* the dig is progress */
                o->uses = 1;
                return 0;
            }
            /* a block in the line within reach: dug */
            a->hold = 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, mb);
            if (best >= 0 && best != s->current) a->hotbar = best;
            return 0;
        }
    }
    else
    {
        /* lane/obsidian7 (obsidian_cast_rim on): the crosshair on the rim's block from over it, within reach: poured */
        if (rim)
        {
            if (s->mo_hit && s->mo_x == rx && s->mo_y == ry && s->mo_z == rz && ey > ry + 1.0 &&
                py_dist3(r->x, ey, r->z, px, py, pz) <= OB_CAST_REACH)
            {
                if (o->uses == 0 && ob_pool_small(v, e, 1)) return 1;   /* lane/obsid2: obsidian_pool_min near */
                if (!ob_hold(v, e, r, s, a, item, &rc)) return rc;
                if (blocks_use(s))
                {
                    OBT(e, r, "giveup"), ob_give_up(e);
                    return 1;
                }
                a->press[PK_USE] = 1;
                OBT(e, r, "use-rim");
                if (o->sc_strip) o->pc_look = 2, o->lk_set = 0;   /* lane/obsid: the looks after the cast */
                o->rims += 1;
                o->uses += 1;
                o->phase = 1;
                o->at = e->age;
                o->gain = e->age;
                return 0;
            }
        }
        else
        {
        /* the bucket's ray (the look's, liquids stop it) on a source of the target's liquid: used */
        int want8 = job == OJ_FILL ? 8 : 10;
        /* lane/obsidian2: for the cast any lava cell (a lake's edge shows flowing lava; the water bucket's own ray passes
         * lava to the floor either way): 56 of 57 casts in a kit case gave up looking at one */
        int lq = s->lq_hit && (s->lq_meta == 0 || (job == OJ_CAST && !OB_CAST_SOURCE)) &&
                 (s->lq_block == want8 || s->lq_block == want8 + 1);
        double ld = lq ? py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) : 99.0;
        struct ob_ctx c = {v, e};
        if (lq && job == OJ_CAST && !ob_castable(&c, s->lq_x, s->lq_y, s->lq_z))
            lq = 0;                                            /* the ray's lava not one the cast may take */
        if (lq && (job == OJ_FILL || ld <= OB_CAST_REACH))
        {
            if (job == OJ_CAST && o->uses == 0 && ob_pool_small(v, e, 1)) return 1;   /* lane/obsid2: pool_min near */
#ifdef OB_LOOK_TRACE
            if (job == OJ_CAST)
            {
                int qx, qy, qz;
                fprintf(stderr, "OBDIRECT %d %ld t %d %d %d ray %d %d %d phase %d mopt %d rim %d:", (int)(e - v->e),
                        (long)e->steps, o->tx, o->ty, o->tz, s->lq_x, s->lq_y, s->lq_z, o->phase, MOPT(MO_OB_CAST_RIM),
                        ob_rim(v, e, r, &qx, &qy, &qz));
                for (int k = 0; k < 4; ++k)
                    fprintf(stderr, " %d/%d", ob_block(v, e, o->tx + OB_DX[k], o->ty, o->tz + OB_DZ[k], NULL),
                            ob_block(v, e, o->tx + OB_DX[k], o->ty + 1, o->tz + OB_DZ[k], NULL));
                fprintf(stderr, "\n");
            }
#endif
            if (!ob_hold(v, e, r, s, a, item, &rc)) return rc;
            if (blocks_use(s))
            {
                /* a use there would open the block instead */
                OBT(e, r, "giveup"), ob_give_up(e);
                return 1;
            }
            a->press[PK_USE] = 1;
            OBT(e, r, "use");
            if (job == OJ_CAST && o->sc_strip) o->pc_look = 2, o->lk_set = 0;   /* lane/obsid: the looks after it */
            o->uses += 1;
            o->phase = 1;
            o->at = e->age;
            o->gain = e->age;
            return 0;
        }
        }
    }
    if (fetching) return 0;
    if (!OB_DEEP && job == OJ_CAST && !OB_NODESCEND)
    {
        /* lane/obsidian2: lava out of the bucket's reach below: a stair down toward it (the casts had stood at a
         * ledge 5 over a lake, out of reach, until they gave up) */
        double hd = py_hypot2(px - r->x, pz - r->z), hmax = MOPT(MO_OB_CAST_WAY) ? OB_DESCEND_H2 : OB_DESCEND_H;
        /* lane/obsid (obsidian_descend far): the stair's reach grows with the drop (a block across a block down: the
         * stair ends within hmax of the lava); on the 21 real starts 44 casts gave up on no gain, the lava 5 to 46
         * across and 1 to 40 below, the crouched walk held at a ledge or a wall */
        if (MOPT(MO_OB_DESCEND) == 2) hmax += (int)floor(r->y + 0.01) - o->ty;
        if (o->ty < (int)floor(r->y + 0.01) - 1 && d > OB_CAST_REACH && hd <= hmax)
        {
            int rc2 = ob_descend(v, e, r, s, a, ey);
            if (rc2 >= 0) return rc2;
        }
    }
    if (!OB_DEEP && job != OJ_MINE && s->mo_hit && py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) <= 4.0 &&
        ob_solid(s->mo_block) && s->mo_block != 7 && s->mo_block != OBSIDIAN &&
        (s->mo_y >= (int)floor(r->y + 0.01) || !(abs(s->mo_x - (int)floor(r->x)) <= 0 && abs(s->mo_z - (int)floor(r->z)) <= 0)) &&
        !ob_lava_near(v, e, s->mo_x, s->mo_y, s->mo_z) && o->remem)
    {
        /* lane/obsidian2: a remembered source behind a block: the block in the line dug (never one with lava in it or
         * beside it, never the one under the feet) */
        int best = best_tool(&v->T, s, s->mo_block);
        if (best >= 0 && best != s->current) a->hotbar = best;
        a->hold = 1u << PK_ATTACK;
        o->gain = e->age;
        return 0;
    }
    /* the walk: crouched (no edge walked off: a jump in the air walks off any), on the ground, never into lava's
     * reach; a jump only up a one-block step that holds the walk (a jump anywhere else had carried the player off a
     * pool's edge into the lava) */
    int st = ob_step(v, e, r, yaw);
    if (st == 0) return 0;                                     /* the edge: still, the look on (no gain gives up) */
    double mv = py_hypot2(r->x - o->px, r->z - o->pz);
    o->stuck = mv < 0.02 ? o->stuck + 1 : 0;
    o->px = r->x, o->pz = r->z;
    if (!r->on_ground) return 0;
    if (ob_around(v, e, r, a, yaw, px, pz, 1)) return 0;      /* lane/obsid2: obsidian_walk around */
    a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
    if (st == 2 && o->stuck > 2) a->hold |= 1u << PK_JUMP;
    return 0;
}

/* the macro's drive (macro_drive's line): 1 when over, the diamond pickaxe then back in hand where a bucket is (the
 * policy's own use key had poured a filled bucket left in hand, the macro filled it again, 60 times an hour in a run;
 * an empty one would take up lava) */
static int ob_drive(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    if (!ob_drive1(v, e, r, s, a, ey)) return 0;
    if (e->ob.sky) e->ob.sky = 0, e->ob.climb_to = 0;     /* lane/obsidian7: a water trip ended any way */
    int cur = s->current >= 0 && s->current < 9 ? s->inv[s->current].item : -1;
    if (a->hotbar < 0 && (cur == BUCKET || cur == WATER_BUCKET || cur == LAVA_BUCKET))
        for (int i = 0; i < 9 && a->hotbar < 0; ++i)
            if (s->inv[i].item == DIAMOND_PICK && s->inv[i].count > 0) a->hotbar = i;
    /* lane/obsid (obsidian_early_fill on): no diamond pickaxe yet: the best pickaxe in the hotbar (the water carried) */
    if (OB_EARLY_FILL && a->hotbar < 0 && (cur == BUCKET || cur == WATER_BUCKET || cur == LAVA_BUCKET))
    {
        int best = -1, bt = 0;
        for (int i = 0; i < 9; ++i)
        {
            int it = s->inv[i].item, t = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
            if (t > bt) bt = t, best = i;
        }
        a->hotbar = best;
    }
    return 1;
}

/* lane/obsidian2: the kit (the macro's items, the obsidian count aside) */
static int ob_kit(const struct neth_rl_state *s)
{
    return inv_count(s, FLINT_STEEL) > 0 && inv_count(s, DIAMOND_PICK) > 0 &&
           inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) > 0;
}

/* who drove the last step (the macro state before this step's start) */
static int ob_who(const struct vec_env *e)
{
    if (e->kind == M_NONE) return OW_POLICY;
    if (e->kind == M_SURFACE && e->esc) return OW_ESC;
    if (e->kind == M_MINE && e->ob.on) return OW_MINE + e->ob.job;
    if (e->kind == M_MINE && e->bp.on) return OW_BPORTAL;          /* lane/obsidian3 */
    if (e->kind == M_MINE && e->strip) return e->ob.search ? OW_SEARCH : OW_STRIP;
    if (e->kind == M_MINE && e->seek) return OW_SEEK;
    if (e->kind == M_SURFACE) return OW_SURF;
    if (e->kind == M_CRAFT) return OW_CRAFT;
    return OW_OTHER;
}

/* decode's line, every step with the switch on or off (before the macros): the books (vecenv.h neth_vec_obsidian) */
static void ob_observe(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    struct obs_book *b = &e->ob.bk;
    int dead = (r->flags & PF_DEAD) != 0, kit = ob_kit(s), who = ob_who(e);
    e->ob.search = 0;                                     /* set again by this step's search */
    if (r->dim == -1 && b->nether < 0) b->nether = e->steps;
    if (kit && b->kit < 0) b->kit = e->steps;
    int obs = (int)inv_count(s, OBSIDIAN);
    if (obs > b->obs_max) b->obs_max = obs;
    /* lane/bportal (obsidian_carry fallback): with 10 or more held in the overworld and none gained for
     * obsidian_fallback_gap steps, the first frame at 10 (OB_KEEP_N), latched for the episode */
    if (obs > e->ob.fb_n) e->ob.fb_at = e->steps;
    e->ob.fb_n = obs;
    if (MOPT(MO_OB_CARRY) == 3 && !e->ob.fb_on && r->dim == 0 && obs >= 10 &&
        e->steps - e->ob.fb_at >= OB_FB_GAP[MOPT(MO_OB_FB_GAP)])
        e->ob.fb_on = 1;
    b->obs_now = obs;
    b->mined = s->mined_obsidian;
    if (kit && !dead) b->steps[who] += 1;
    {
        int wb = (int)inv_count(s, WATER_BUCKET);       /* lane/obsid: the water carried (counting only) */
        if (!dead && wb > b->wb_last) b->wb_fills += wb - b->wb_last;
        if (wb > 0 && b->wb_first < 0) b->wb_first = e->steps;
        if (wb > 0) b->wb_steps += 1;
        b->wb_last = wb;
        /* the funnel (lane/obsid): the head under the sky, a water bucket, obsidian, the Nether (the looks mark the rest) */
        if (!dead && b->kit >= 0)
        {
            if (b->fn[OF_SKY] < 0 && r->dim == 0 &&
                obsview_sky(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 0.01) + 1, (int)floor(r->z)) >=
                    OB_SKY_LIGHT)
            {
                ob_fn(v, e, OF_SKY);
                /* lane/obsid2's water census (counting only): at the head's first sky after the kit, what the trip
                 * would make of it (OSW_*) and the kept water's distance across (-1 none kept) */
                const struct obs_env *o = &e->ob;
                double d = o->sw_ok ? py_hypot2(o->sw_x + 0.5 - r->x, o->sw_z + 0.5 - r->z) : -1.0;
                b->sky_d = (float)d;
                b->sky_why = wb > 0 ? OSW_HELD : inv_count(s, BUCKET) == 0 ? OSW_NOBUCKET : !o->sw_ok ? OSW_NONE
                             : d > OB_TRIP_R0 ? OSW_FAR : abs(o->sw_y - (int)floor(r->y + 0.01)) > OB_TRIP_UP0 ? OSW_UPDOWN
                             : r->wtime % 24000 >= 12500 ? OSW_NIGHT : OSW_DUE;
            }
            if (wb > 0) ob_fn(v, e, OF_FILL);
            if (obs >= 1) ob_fn(v, e, OF_OBS1);
            if (obs >= 10) ob_fn(v, e, OF_OBS10);
            if (r->dim == -1) ob_fn(v, e, OF_NETHER);
        }
    }
#ifdef OB_STEP_ENV
    /* -DOB_STEP_ENV=E -DOB_STEP_FROM=A -DOB_STEP_TO=B (a VARIANT build): env E's steps A..B on stderr, the cells
     * around the feet (x and z -1..1, y -1..2: the block ids, 10/11 lava) */
    if ((int)(e - v->e) == OB_STEP_ENV && e->steps >= OB_STEP_FROM && e->steps <= OB_STEP_TO)
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z), id = (int)(e - v->e);
        fprintf(stderr, "OBS %ld who %d kind %d strip %d sdir %d sk %d sc %d %d %d esc %d ob %d/%d at %.2f %.2f %.2f "
                        "yaw %.0f pitch %.0f hp %.1f food %d fire %d air %d mo %d:%d,%d,%d lq %d:%d,%d,%d |",
                (long)e->steps, who, e->kind, e->strip, e->sdir, e->sk, e->scx, e->scy, e->scz, e->esc, e->ob.on,
                e->ob.job, r->x, r->y, r->z, r->yaw, r->pitch, r->health, (int)r->food, r->fire, r->air,
                s->mo_hit ? s->mo_block : -1,
                s->mo_x, s->mo_y, s->mo_z, s->lq_hit ? s->lq_block : -1, s->lq_x, s->lq_y, s->lq_z);
        for (int dy = -1; dy <= 2; ++dy)
        {
            fprintf(stderr, " y%+d", dy);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    fprintf(stderr, "%s%d", dx == -1 ? (dz == -1 ? " " : "/") : ",",
                            obsview_block(v->p, id, fx + dx, fy + dy, fz + dz, NULL));
        }
        fprintf(stderr, "\n");
    }
#endif
    if (!dead)
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z), id = (int)(e - v->e);
        /* the books: what the player stood in, whatever the lava_sense (no act reads them) */
        int fb = obsview_block(v->p, id, fx, fy, fz, NULL), hb = obsview_block(v->p, id, fx, fy + 1, fz, NULL);
        int in_lava = ob_lava(fb) || ob_lava(hb);
        /* an incident's start (a lava touch or the flames after 100 steps of neither): who walked into it */
        if ((in_lava || (r->fire && !b->lfire)) && e->steps - (b->lava_at > b->burn_at ? b->lava_at : b->burn_at) > 100)
            b->who_hurt = who;
        if (in_lava) b->lava_at = e->steps;
        if (r->fire && !b->lfire) b->burn_at = e->steps;
        float drop = b->lhp - r->health;
        if (r->on_ground && !b->lground && b->fall_top - r->y > 3.5 && drop > 0) b->cause_last = OC_FALL;
        else if (drop > 0)
        {
            int near = 0;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
                if (fight_kind(s->vis[i].kind) &&
                    py_dist3(r->x, r->y, r->z, s->vis[i].x, s->vis[i].y, s->vis[i].z) < 16.0)
                    near = 1;
            int burn_lava = r->fire && b->lava_at > 0 && e->steps - b->lava_at <= OB_LAVA_BURN;
            b->cause_last = in_lava || burn_lava ? OC_LAVA
                            : r->fire            ? OC_FIRE
                            : near               ? OC_MOB
                            : s->eye_water && r->air <= 0 ? OC_DROWN
                            : r->food == 0       ? OC_STARVE
                                                 : OC_OTHER;
        }
        if (r->on_ground) b->fall_top = (float)r->y;
        else if (r->y > b->fall_top) b->fall_top = (float)r->y;
        b->lground = r->on_ground;
        b->lfire = r->fire;
        b->lhp = r->health;
        b->who_last = who;
    }
    if (dead && !b->was_dead)
    {
        b->deaths += 1;
        b->cause[b->cause_last] += 1;
        if (b->d_step < 0)
        {
            /* who: for a burn, who walked into the lava (the incident's start); else who drove the last step */
            int burnt = b->cause_last == OC_LAVA || b->cause_last == OC_FIRE;
            b->d_step = e->steps, b->d_cause = b->cause_last, b->d_who = burnt ? b->who_hurt : b->who_last;
            b->d_kit = b->kit >= 0;
            b->d_pos[0] = (float)r->x, b->d_pos[1] = (float)r->y, b->d_pos[2] = (float)r->z;
        }
#ifdef OB_TRACE
        fprintf(stderr, "OBT %d %ld death cause %d who %d at %.1f %.1f %.1f kit %d obs %d\n", (int)(e - v->e),
                (long)e->steps, b->cause_last, b->who_last, r->x, r->y, r->z, b->kit >= 0, obs);
#endif
    }
    if (dead) b->lhp = 20.0f, b->lground = 1, b->cause_last = OC_OTHER;
    b->was_dead = dead;
    /* lane/obsidian2: the memory's own look, the bit on, the portal not yet made: water and lava sources the eye sees
     * (the surface's lakes and pools before any dig) remembered every OB_MEM_EVERY steps the macro's look did not run */
    if (!OB_DEEP && (v->macros & NETH_VEC_OBSIDIAN) && !dead && r->dim == 0 && b->nether < 0 && r->screen == PS_NONE && obs < 10 &&
        e->steps - e->ob.scan_at >= OB_MEM_EVERY && e->steps - e->ob.mem_at >= OB_MEM_EVERY)
    {
        e->ob.mem_at = e->steps;
        struct obsview_hit h[OV_KINDS];
        if (obsview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, 16,
                         1 << OV_WATER | 1 << OV_LAVA, NULL, NULL, h) == 0)
        {
            if (h[OV_WATER].d > 0.0f) ob_mem_add(e, 0, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z);
            /* lane/obsid (obsidian_water_trip on): the surface water kept, the nearest to the player of the kept one and
             * the one seen now */
            if (OB_TRIP && h[OV_WATER].d > 0.0f && h[OV_WATER].y >= OB_TRIP_Y &&
                (!e->ob.sw_ok || py_hypot2(h[OV_WATER].x + 0.5 - r->x, h[OV_WATER].z + 0.5 - r->z) <
                                     py_hypot2(e->ob.sw_x + 0.5 - r->x, e->ob.sw_z + 0.5 - r->z)))
                e->ob.sw_ok = 1, e->ob.sw_x = h[OV_WATER].x, e->ob.sw_y = h[OV_WATER].y, e->ob.sw_z = h[OV_WATER].z;
            if (h[OV_WATER].d > 0.0f && inv_count(s, BUCKET) > 0) ob_fn(v, e, OF_WATER);   /* lane/obsid's funnel */
            if (h[OV_LAVA].d > 0.0f && inv_count(s, WATER_BUCKET) > 0) ob_fn(v, e, OF_LAVA);
#ifdef OB_WATER_TRACE
            /* lane/obsid (a VARIANT build): a water source in view while an empty bucket is held, the water bucket not */
            if (h[OV_WATER].d > 0.0f && inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0)
                fprintf(stderr, "OWT %d %ld water %d %d %d d %.1f at %.1f %.1f %.1f dpick %d fs %d\n", (int)(e - v->e),
                        (long)e->steps, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z, h[OV_WATER].d, r->x, r->y, r->z,
                        (int)inv_count(s, DIAMOND_PICK), (int)inv_count(s, FLINT_STEEL));
#endif
            if (h[OV_LAVA].d > 0.0f) ob_mem_add(e, 1, h[OV_LAVA].x, h[OV_LAVA].y, h[OV_LAVA].z);
#ifdef OB_CENSUS
            /* lane/obsid (a VARIANT build): every water source seen (OCW), and the first lava within 6 while the
             * diamond strip drives, before and after the diamond pickaxe (OCL) */
            int dp = inv_count(s, DIAMOND_PICK) > 0;
            if (h[OV_WATER].d > 0.0f)
                fprintf(stderr, "OCW %d %ld water %d %d %d d %.1f at %.1f %.1f %.1f bucket %d dpick %d\n", (int)(e - v->e),
                        (long)e->steps, h[OV_WATER].x, h[OV_WATER].y, h[OV_WATER].z, h[OV_WATER].d, r->x, r->y, r->z,
                        (int)(inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET)), dp);
            if (h[OV_LAVA].d > 0.0f && h[OV_LAVA].d <= 6.0f && who == OW_STRIP && !e->ob.cz_l[dp])
            {
                e->ob.cz_l[dp] = 1;
                fprintf(stderr, "OCL %d %ld lava %d %d %d d %.1f at %.1f %.1f %.1f dpick %d wb %d\n", (int)(e - v->e),
                        (long)e->steps, h[OV_LAVA].x, h[OV_LAVA].y, h[OV_LAVA].z, h[OV_LAVA].d, r->x, r->y, r->z, dp,
                        (int)inv_count(s, WATER_BUCKET));
            }
#endif
        }
    }
#ifdef OB_CENSUS
    /* the bucket first held (OCB) and the diamond pickaxe (OCD), where; water's distance is the reader's (OCW lines) */
    if (!dead && !e->ob.cz_bk && inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) > 0)
    {
        e->ob.cz_bk = 1;
        fprintf(stderr, "OCB %d %ld at %.1f %.1f %.1f\n", (int)(e - v->e), (long)e->steps, r->x, r->y, r->z);
    }
    if (!dead && !e->ob.cz_dp && inv_count(s, DIAMOND_PICK) > 0)
    {
        e->ob.cz_dp = 1;
        fprintf(stderr, "OCD %d %ld at %.1f %.1f %.1f wb %d\n", (int)(e - v->e), (long)e->steps, r->x, r->y, r->z,
                (int)inv_count(s, WATER_BUCKET));
    }
#endif
}

int neth_vec_obsidian(const struct neth_vec *v, int id, struct neth_vec_obsidian *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL) return -1;
    const struct vec_env *e = &v->e[id];
    const struct obs_book *b = &e->ob.bk;
    memset(out, 0, sizeof *out);
    out->nether = b->nether, out->kit = b->kit, out->d_step = b->d_step;
    out->obs_max = b->obs_max, out->mined = b->mined, out->deaths = b->deaths;
    out->d_cause = b->d_cause, out->d_who = b->d_who, out->d_kit = b->d_kit;
    for (int k = 0; k < OW_KINDS && k < 16; ++k) out->jobs[k] = b->jobs[k], out->steps[k] = b->steps[k];
    for (int k = 0; k < OC_KINDS && k < 7; ++k) out->cause[k] = b->cause[k];
    for (int k = 0; k < 3; ++k) out->d_pos[k] = b->d_pos[k];
    out->dim = e->last.dim;
    out->pos[0] = e->last.x, out->pos[1] = e->last.y, out->pos[2] = e->last.z;
    out->obsidian = b->obs_now;
    out->who_last = b->who_last, out->who_hurt = b->who_hurt;     /* lane/obsidian4 */
    out->sky_trips = e->ob.sky_trips, out->sky_water = e->ob.sky_water;   /* lane/obsidian7 */
    out->drops = e->ob.drops, out->steers = e->ob.steers, out->rims = e->ob.rims;
    out->wb_fills = b->wb_fills, out->wb_first = b->wb_first, out->wb_steps = b->wb_steps;   /* lane/obsid */
    out->scouts = e->ob.sc_n, out->scout_found = e->ob.sc_found;
    for (int k = 0; k < OF_KINDS && k < 8; ++k) out->fn[k] = b->fn[k];
    /* lane/obsid2: the water trip's books */
    out->sw_ok = e->ob.sw_ok, out->sw_trips = e->ob.sw_trips, out->sw_fill = e->ob.sw_fill;
    out->sky_why = b->sky_why, out->sky_d = b->sky_d;
    out->glances = e->ob.gl_n, out->arounds = e->ob.ar_n;
    for (int k = 0; k < 5; ++k) out->trip_end[k] = e->ob.te[k];
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static void obsidian_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    ob_observe(c->v, c->e, c->r, c->s);     /* lane/obsidian2: the obsidian books (no act, reward or scalar) */
}

static int obsidian_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    return (v->macros & NETH_VEC_OBSIDIAN) && (ob_want(v, c->e, c->r, c->s) || ob_douse_want(v, c->e, c->r, c->s));
}

/* lane/obsidian: burning with a water bucket, it is poured at the feet */
static int obsidian_m_interrupt(struct mac_step *c)
{
    if (ob_douse_want(c->v, c->e, c->r, c->s))
    {
        mac_end(c->e);
        ob_douse_begin(c->e, c->r, c->s);
    }
    return 0;
}

/* lane/obsidian: before the strip; lane/obsidian3: not while a bucket portal's frame is cast (its mine job dug the cells
 * cast: 22 obsidian in 6 envs from the kit) */
static int obsidian_m_start(struct mac_step *c)
{
    return (c->v->macros & NETH_VEC_OBSIDIAN) && !c->e->bp.site && ob_start(c->v, c->e, c->r, c->s);
}

static int obsidian_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->ob.on) return -1;
    return ob_drive(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t obsidian_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->ob.on ? NETH_VEC_OBSIDIAN : 0; }

static void obsidian_m_end(struct vec_env *e) { e->ob.on = 0; }

static void obsidian_m_reset(struct vec_env *e) { ob_reset(&e->ob); }

static const struct mac_switch OBSIDIAN_SW[] = {{"obsidian", NETH_VEC_OBSIDIAN}, {NULL, 0}};

static const struct vec_macro MAC_OBSIDIAN = {
    .name = "obsidian", .bit = NETH_VEC_OBSIDIAN, .sw = OBSIDIAN_SW, .observe = obsidian_m_observe, .cand = obsidian_m_cand,
    .interrupt = obsidian_m_interrupt, .start = obsidian_m_start, .drive = obsidian_m_drive, .label = obsidian_m_label,
    .end = obsidian_m_end, .reset = obsidian_m_reset,
};
