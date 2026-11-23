/* lane/stronghold: NETH_VEC_STRONGHOLD (vecenv.h), included by macros.c (it uses the strip's helpers).
 *
 * Starts (sh_want): the switch on, the overworld, no screen, and the macro latched this episode: latched the first
 * time SH_START_EYES eyes of ender are held there, then wanted while any eye is held (or every frame is filled) until
 * it gives up. It runs as the mine macro's kind (sh.on) and gives the step back every SH_YIELD steps and whenever the
 * food macro would eat, so eating, the lava escape, drowning and fights run between; it starts again at once. Over for
 * good in the End (dim 1, lane/dragon's from there), when it gives up (no way down at the column, the dig at y 8), at
 * a death (what it held lies there) and when out of eyes short of the twelfth frame. It leaves held what it was given
 * and did not use: the eyes not put in, the tools; never the eyes selected outside a throw or the frames (a use by
 * whatever runs next would throw one).
 *
 * lane/stronghold3 (the library's eye starts, golden-g24 to g26, at vanilla fidelity): the eyes in the pack fetched
 * with its own E press (the inventory screen its own while it fetches), a hole for the night (stronghold_night), a
 * climb to the sky from under a roof (stronghold_roof), a stone pickaxe made for the way when under SH_PICK_NEED uses
 * are held (the step left to the craft macro: table, sticks, pickaxe, the table taken back), the sprint only while fed
 * (stronghold_sprint), stone walls climbed on cobblestone (stronghold_wall). It still starts and ends as above.
 *
 * What it reads is what a player sees (shview.h): the thrown eye in flight, its drop, the stronghold's bricks and
 * frames in view, the crosshair (neth_rl_state mo_*), the liquid look (lq_*), its own position and look (F3). From the
 * flight: EntityEnderEye moves each tick by speed + (remaining - speed) * 0.0025 toward its target (12 blocks ahead
 * and 8 up when the stronghold is farther than 12, else the portal room's middle), so three positions a tick apart
 * give the remaining distance and the heading, and the target: a line toward the stronghold, or the room's column
 * itself when the eye flies down. Two lines thrown 24 blocks apart cross at the target (the stronghold's ring cell far
 * away, the room once its chunks are near). Never a block, structure or entity the player has not seen. */

#if defined(SH_TRACE) && !defined(SH_TRACE_ENV)
#define SH_TRACE_ENV 0          /* debugging only: the env the trace follows */
#endif
#define SH_START_EYES 12        /* eyes held that latch the macro */
#define SH_YIELD 200            /* steps before giving the step back once */
#define SH_PERP 24.0            /* the second throw of a pair this far across the first one's line */
#define SH_NEAR_PAIR 160.0      /* a pair thrown this close to its crossing crossed at the room (its chunks generated) */
#define SH_ARRIVE 2.0           /* a throw's goal reached within this (blocks, horizontal) */
#define SH_WATCH_MAX 110        /* steps watching a flight (the eye lives 80 ticks) */
#define SH_PICK_MAX 400         /* steps going to a drop */
#define SH_PICK_WAIT 40         /* steps waiting at the drop's place past the eye's end */
#define SH_STALL 10             /* steps a travel may gain under SH_STALL_D blocks before digging ahead */
#define SH_STREAK 2.0           /* a stall this far from the streak's first is a new obstacle (lane/stronghold2) */
#define SH_SIDE_KEEP 300        /* steps a detour's side is kept for the next detour */
#define SH_STALL_D 1.5
#define SH_DETOUR 50            /* steps of a detour (a drop or lava ahead, a wall the dig did not pass) */
#define SH_DROP_PROBE 5         /* a fall ahead deeper than this is a cliff */
#define SH_SURF_DROP 6.0        /* a landing this far under the fall's top: fallen in, the staircase up */
#define SH_CAVE_Y 50.0          /* the feet under this with a roof over the head: in a cave, the staircase up */
#define SH_SKY_RAY 48.0         /* the look up that finds the roof */
#define SH_DIG_TRIES 6          /* ring cells refused in a row (lava, a cave, water under them) before the dig gives up */
#define SH_BLAME 200           /* a death this many steps after giving up is still the macro's (the books) */
#define SH_PICK_KEEP 70        /* a pickaxe's uses kept for the dig down (about 60 blocks of the shaft and the roof) */
#define SH_ASC_STEPS 1500       /* the staircase given up after this many steps */
#define SH_MIN_Y 8              /* the dig gives up with the feet this low */
#define SH_REACH 4.45           /* a frame's top within this of the eye is filled from where the player stands (the
                                 * client's block reach is 4.5) */
#define SH_SCAN_R 16            /* the block scan's radius */
#define SH_RSTART_D 12.0        /* lane/proom (stronghold_room_start on): a portal frame this near the eye latches */
#define SH_RSTART_PAD 2.0       /* ... or (stronghold_oracle=locate) the feet this near the located room's box */
#define SH_RSTART_EVERY 20      /* ... read every this many steps */
/* lane/stronghold2: one far throw places the stronghold. Before it is generated an eye flies toward the ring's chunk
 * centre ((chunk << 4) + 8: MapGenStructure.func_151545_a's getCoordList fallback), and its heading is the float angle
 * atan2 gives (about 1e-7 radians, so 1e-4 blocks across at 1,000): the one chunk centre on the line inside the ring's
 * band ((1.25 + rand) * 32 chunks from the world's middle, moved up to 112 blocks by the biome search) is the start's
 * chunk, as a speedrunner's one-eye calculator reads it. Ambiguous (a second centre as near the line), the pair. */
/* lane/macroopts: how the stronghold is placed is the run file's stronghold_locate (vecenv.c MOPTS): follow (the
 * simplest way, the fewest Ends), pair (two throws crossed) or snap (this one far throw, the pair its fallback: the
 * default, the most Ends on 12 eyes) */
#define SH_FOLLOW_STRIDE 200.0  /* follow: blocks walked along a line before the next throw */
#define SH_FOLLOW_MIN 24.0      /* ... halved at each reversal down to this (an eye flies down within 12 of the room) */
#define SH_GRID_TOL 0.02       /* a chunk centre this close to the line (blocks across) is on it */
#define SH_GRID_SEP 0.25        /* ... and alone when the next one is farther than this */
#define SH_RING_MIN 500.0       /* the band of the ring's chunk centres from (0, 0) */
#define SH_RING_MAX 1300.0
#define SH_GRID_FAR 2200.0      /* the line searched this far */
#define SH_VIA_PERP 24.0        /* the walk to the start's chunk by way of a point this far left of the line (0: none) */
#define SH_LEVEL_MAX 8          /* level moves round the ring with no step down before the dig gives up */
#define SH_CANOPY_MAX 500       /* steps clearing the canopy over a drop */
#define SH_SITE_LEAVES 4        /* leaves seen round a throw's site that move it */
#define SH_SITE_STEP 10.0       /* ... this far away from them */
#define SH_SITE_MOVES 2         /* ... at most this many times a throw */
#define SH_CAVE_DROP 9          /* a cave's floor this far under a dig cell's: dropped into */
#define SH_FALL_HP 10.0         /* ... when the fall leaves this much health (half hearts) */
#define SH_ROOM_MAX 900        /* steps in the room before it gives up (and steps away) */
#define SH_RPICK_MAX 240        /* steps going to eyes lying in the room */
#define SH_SKIP 200             /* steps an out-of-reach frame is left before it is tried again */
#define SH_ENTER_MAX 300        /* steps walking onto the lit portal */
#define SH_LEAVE_D 7.0          /* stopped in the room: this far from the ring's middle */
#define SH_LEAVE_MAX 120        /* ... or after this many steps */
#define SH_FETCH_HOLD 20        /* lane/stronghold3: the inventory screen the macro's own this many steps after it
                                 * fetched (its E press, its click): the eyes moved into the hotbar from the pack */
#define SH_FETCH_PRESS 10       /* steps between E presses while the screen has not opened */
/* lane/stronghold3: the night (stronghold_night hide). From golden-g24 to g26 at vanilla fidelity 23 of 32 envs died
 * walking, at steps 6,000 to 10,600: the first dusk. A hole SH_HIDE_DEEP deep with a block over the head against the
 * inner face of the rim (lane/pearl2's hide, PEARL_HIDE_*), held from SH_NIGHT0 to SH_NIGHT1 of the day's time (the
 * sky's: mobs spawn at light 7 and under from about 13,200; zombies and skeletons burn from about 23,500) */
#define SH_NIGHT0 12800
#define SH_NIGHT1 23500
#define SH_HIDE_DEEP 3          /* the hole's depth */
#define SH_HIDE_DIG 240         /* steps digging it at most before it is given up */
#define SH_HIDE_COVER 64        /* steps putting the block over the head at most (held open after: 3 deep is cover) */
#define SH_HIDE_WAIT 12000      /* steps held at most */
#define SH_HIDE_OPEN 200        /* steps mining the cover at most */
#define SH_HIDE_UP 160          /* steps of the pillar back up at most */
#define SH_HIDE_NEAR 64.0       /* stronghold_night_near on: no hide with the goal (the start's chunk, a near throw's
                                 * place, the dig) this near: golden-g26's envs with 12 eyes 80 to 120 blocks short
                                 * hid through the second night to the episode's end */
#define SH_HURT_HP 10.0f       /* stronghold_night hurt: the hide at this health or under */
#define SH_HEALED_HP 18.0f      /* ... left when healed to this */
#define SH_HIDE_OFF 400        /* no hide this many steps after one given up */
#define SH_STUCK 600            /* lane/stronghold3 (stronghold_stuck turn): steps the walk may go without coming
                                 * SH_STUCK_GAIN nearer its goal (golden-g26 under the frozen policy: 12,000 steps
                                 * at one stone wall, its stall rules taking turns) */
#define SH_STUCK_GAIN 4.0
#define SH_STUCK_GAP 300        /* ... begun again after this many steps the walk did not drive (a hide; not the
                                 * yields, eats and failed hunts between: at 20 golden-g26's env 4 never got there) */
#define SH_STUCK_TURN 200       /* ... then a detour square to the way this long, the side taken in turn */
#define SH_HUNT_MEAT 4          /* lane/stronghold3 (stronghold_hunt low): an animal in view hunted under this many meats */
#define SH_HUNT_D 16.0          /* ... within this */
#define FLESH 367               /* rotten flesh */
#define SH_FLESH_FOOD 4         /* lane/stronghold3 (stronghold_flesh starving): rotten flesh eaten at this food or under */
#define SH_FLESH_MAX 60         /* ... the use held at most this many steps (the eating takes 32 ticks) */
#define SH_STUCK_NEAR 8.0      /* ... stuck again within this of the last time: the same place (the sky, the reserve) */
#define SH_PICK_NEED 200        /* lane/stronghold3: the pickaxes' uses held that the way needs (the dig down about 60,
                                 * the walk's stone and a climb out); under it a stone pickaxe is made */
#define SH_CRAFTS 16            /* ... the craft macro given the step this many times at most an episode */
#define SH_SPRINT_MEAT 8        /* lane/stronghold3: stronghold_sprint fed: meats held for the walk to sprint */
#define SH_SKY_Y 62.0           /* ... the walk's climb to the sky only with the feet under this (over it a roof is mostly
                                 * the walk's own tunnel through a hill: 658 and 848 climbs in 32 envs without it) */
#define SH_RGO_MAX 400          /* lane/stronghold3: stronghold_room_walk outside: steps on the room's floor at most */
#define SH_RGO_OUT 1.3          /* ... the goal this far past the frame, away from the ring's middle */
#define SH_SKY_CHECK 10     /* stronghold_roof sky: steps between the looks up while climbing to the sky */
#define SH_PILLAR_MAX 6        /* cobblestone blocks it stands up on under a canopy, at most (a fall back of 6: 3
                                 * hearts' damage at worst, none from 3) */

/* lane/shold: stronghold_oracle locate (PRIVILEGED: the world's structure data and the server's blocks; for lane/chain's
 * search, never in the benchmark's numbers). Latched as above, it throws no eye: the phase SH_ORACLE (sh_oracle) */
#define SHO_LEG 40              /* the walk's leg: its target this far ahead toward the room (the server holds 4 chunks
                                 * round the player at render distance 4) */
#define SHO_DESCEND_D 48.0      /* the staircase down planned once the room's middle is this near (horizontally) */
#define SHO_RETRY 20            /* steps before another plan when none was found */
#define SHO_NOPATH_MAX 30       /* plans with nothing found in a row: given up */
#define SHO_OFF 30              /* steps off the way before it is made again */
#define SHO_AGE 400             /* steps a way is walked before it is made again */
#define SHO_DIG_MAX 240         /* steps on one block before another way */
#define SHO_DIG_BARE_FURNACE 480 /* seed 3's route: a furnace takes longer than 240 attacks by hand */
#define SHO_REACH 4.3           /* a frame's top within this of the eye is filled (the reach is 4.5) */
#define SHO_READ 10             /* steps between reads of the room's blocks while near */
#define SHO_GAIN_D 4.0          /* nearer the room's middle by this: progress */
#define SHO_STALL 6000          /* steps with no progress on the way there before it gives up */
#define SHO_GAP 100             /* ... a gap this long in its driving (a hide) begins the count again */
#define SHO_SPAWNER_MAX 600     /* steps breaking the spawner at most */
#define SHO_FILL_MAX 2400       /* steps filling the frames at most */
#define SHO_USE_WAIT 6          /* steps after a use before the frames are read again */
#define SHO_FR_TRIES 3          /* a frame whose stand cell failed this often is left */
#define SHO_FR_WALK 300         /* steps walking to one frame's stand cell, or aiming at it */
#define SHO_TURN_D 3.4          /* a hostile out of view this near: the look turned to it (the fight's lock is at 3.5) */
#define SHO_TURN_MAX 40         /* ... turned to it this long with no fight begun (behind a wall): left alone */
#define SHO_TURN_OFF 200        /* ... for this many steps */
#define SHO_BRIDGE_MAX 200      /* steps a way's bridge block may take before the way is made again */
#define SHO_SNEAK_F 64        /* sho_follow: sneak (the room: off no edge); not shoracle_path's */

enum { SH_THROW, SH_WATCH, SH_PICK, SH_PLAN, SH_GO, SH_DIG, SH_ROOF, SH_ROOM, SH_ENTER, SH_LEAVE, SH_DONE, SH_HIDE,
       SH_ORACLE };
/* lane/shold: the phase SH_ORACLE's steps: the room located, the walk there, the staircase down into it, the spawner,
 * the frames, onto the portal */
enum { SO_LOCATE, SO_TRAVEL, SO_DOWN, SO_SPAWNER, SO_FILL, SO_ENTER };
enum { SHH_DIG, SHH_COVER, SHH_WAIT, SHH_OPEN, SHH_UP };   /* lane/stronghold3: the hide's steps */
enum { SG_THROW, SG_DIG, SG_VIA };
enum { SC_LAVA, SC_FALL, SC_DROWN, SC_STARVE, SC_MOB, SC_OTHER };

static int sh_frame_open(const struct sh_env *h)
{
    int n = 0;
    for (int i = 0; i < h->nfr; ++i) n += !h->fr[i].eye;
    return n;
}

/* the macro wanted now (and latched on its first want) */
/* lane/chain2 (stronghold_food wait, stronghold_armor full): the latch held back while food or armor is short (the End's
 * fight here is an hour of crystals and sword; s42k3's End states held 0 to 21 points and died to the dragon's hits),
 * for at most SH_FOOD_MAX steps; the strip and the seek stand down meanwhile (sh_owns), so the food macro's hunt and
 * the craft macro's cook are what run */
#define SH_FOOD_FLOOR 160
#define SH_FOOD_FLOOR_80 80    /* lane/chain3: the measured wait80 option */
#define SH_FOOD_MAX 36000
#define SH_ARMOR_FLOOR 15
#define SH_ARMOR_MAX 72000
#define SH_SWORD_MAX 72000
static int sh_food_holds(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    struct sh_env *h = &e->sh;
    h->fwait = 0;
    if (h->go || r->dim != 0 || (r->flags & PF_DEAD) || inv_count(s, EYE) < SH_START_EYES)
        return 0;
    int food_floor = MOPT(MO_SH_FOOD) == SHW_WAIT80 ? SH_FOOD_FLOOR_80 : SH_FOOD_FLOOR;
    int food_wait = MOPT(MO_SH_FOOD) != SHW_OFF && food_points(s) < food_floor;
    const struct neth_rl_far *f = MOPT(MO_SH_ARMOR) ? neth_rl_far(v->h, (int)(e - v->e)) : NULL;
    int armor_wait = MOPT(MO_SH_ARMOR) && (!f || f->armor_worn < SH_ARMOR_FLOOR);
    int sword_need = sword_stock_need(v);
    int sword_wait = sword_need && sword_stock_fresh(s) < sword_need;
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV && e->steps % 200 == 0)
    {
        int free_slots = 0;
        for (int i = 0; i < 36; ++i) free_slots += s->inv[i].item < 0 || s->inv[i].count <= 0;
        fprintf(stderr, "shwait t=%lld food=%lld raw=%lld furnace=%lld coal=%lld free=%d armor=%d swords=%d "
                        "holds=%d,%d,%d waited=%lld\n",
                (long long)e->steps, (long long)food_points(s), (long long)raw_meat(s),
                (long long)inv_count(s, FURNACE), (long long)inv_count(s, COAL), free_slots,
                f ? f->armor_worn : -1, sword_stock_fresh(s), food_wait, armor_wait, sword_wait,
                (long long)(h->fw1 ? e->steps + 1 - h->fw1 : 0));
        if (e->steps % 1000 == 0)
        {
            fprintf(stderr, "shinv t=%lld", (long long)e->steps);
            for (int i = 0; i < 36; ++i)
                if (s->inv[i].count > 0)
                    fprintf(stderr, " %d:%dx%d/%d", i, s->inv[i].item, s->inv[i].count, s->inv[i].damage);
            fprintf(stderr, "\n");
        }
    }
#endif
    if (!food_wait && !armor_wait && !sword_wait) return 0;
    if (h->fw1 == 0) h->fw1 = e->steps + 1;
    h->fwait = e->steps + 1 - h->fw1 <= (armor_wait ? SH_ARMOR_MAX : sword_wait ? SH_SWORD_MAX : SH_FOOD_MAX);
    return h->fwait;
}

static int sh_want(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV && e->steps % 200 == 1)
        fprintf(stderr, "want t=%lld dim=%d screen=%d flags=%d eyes=%lld go=%d phase=%d\n", (long long)e->steps, r->dim,
                r->screen, r->flags, (long long)inv_count(s, EYE), e->sh.go, e->sh.phase);
#endif
    struct sh_env *h = &e->sh;
    /* lane/stronghold3: the inventory it opened itself to fetch the eyes is no other screen (refusing it gave the
     * step back on the E press, and the eyes in the pack never reached the hotbar: golden-g24 to g26, 0 throws) */
    int own = r->screen == PS_INVENTORY && h->go && h->fetch > 0 && e->steps - h->fetch <= SH_FETCH_HOLD;
    if (!(v->macros & NETH_VEC_STRONGHOLD) || r->dim != 0 || (r->screen != PS_NONE && !own) || (r->flags & PF_DEAD))
        return 0;
    int64_t eyes = inv_count(s, EYE);
    if (!h->go && eyes >= SH_START_EYES && h->phase != SH_DONE && !sn_holds(v, e, r, s) &&   /* lane/snowleg */
        !sh_food_holds(v, e, r, s))
    {
        h->go = 1;
        h->phase = MOPT(MO_SH_ORACLE) == SHOR_LOCATE ? SH_ORACLE : SH_THROW;     /* lane/shold: the oracle's own */
        h->pstart = e->steps;
        if (h->book.started < 0) h->book.started = (int32_t)e->steps;
    }
    return h->go && h->phase != SH_DONE &&
           (eyes > 0 || h->phase == SH_ENTER || h->phase == SH_ROOM || h->phase == SH_LEAVE ||
            (h->phase == SH_ORACLE && h->osub >= SO_FILL));
}

/* the strip, the seek and the craft plans' starts stand down while the macro is latched (its own want or not) */
static int sh_owns(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    return ((v->macros & NETH_VEC_STRONGHOLD) && e->sh.go && e->sh.phase != SH_DONE && r->dim == 0) ||
           sn_waits(v, e, r) ||  /* lane/snowleg: snow_first's wait holds them down too */
           (e->sh.fwait && r->dim == 0 && !e->sh.go);   /* lane/chain2: and stronghold_food's */
}

/* lane/stronghold3 (stronghold_throw_key macro): a step the policy's keys reach the game (no macro, or the one that
 * gave the step back on it) with the macro latched and an eye in the hand: its use (a throw, nothing watching it)
 * and its drop let go, unless the crosshair is on an open frame (the eye goes in) */
static void sh_policy_keys(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (!sh_owns(v, e, r) || MOPT(MO_SH_THROW_KEY) != SHK_MACRO) return;
    int cur = a->hotbar >= 0 && a->hotbar < 9 ? a->hotbar : s->current;
    if (cur < 0 || cur >= 9 || s->inv[cur].item != EYE || s->inv[cur].count <= 0) return;
    if (s->mo_hit && s->mo_block == END_FRAME && !(s->mo_meta & 4)) return;
    a->hold &= ~(1u << PK_USE);
    a->press[PK_USE] = 0;
    a->press[PK_DROP] = 0;
}

static void sh_phase(struct vec_env *e, int p)
{
    e->sh.phase = p;
    e->sh.pstart = e->steps;
    e->sh.slook = 0;
}

static int sh_age(const struct vec_env *e) { return (int)(e->steps - e->sh.pstart); }

static int sh_hotbar(const struct neth_rl_state *s, int item)
{
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item == item && s->inv[i].count > 0) return i;
    return -1;
}

/* a pickaxe's uses left (the durability bar a player sees): wood 59, stone 131, iron 250, diamond 1561, gold 32 */
static int sh_uses_left(int item, int damage)
{
    int max = item == 270 ? 59 : item == 274 ? 131 : item == 257 ? 250 : item == 278 ? 1561 : item == 285 ? 32 : 0;
    return max - damage;
}

/* stone brick and the rest a pickaxe breaks that PICKY lacks */
/* lane/stronghold3: the pickaxes' uses left, summed (stone and better: what the walk's stone and the dig take) */
static int sh_pick_uses(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int n = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it] >= 1)
            n += sh_uses_left(it, s->inv[i].damage) > 0 ? sh_uses_left(it, s->inv[i].damage) : 0;
    }
    return n;
}

/* lane/stronghold3: a stone pickaxe wanted for the way (craft_plan's): latched, under SH_PICK_NEED uses held, the
 * cobblestone for one */
static int sh_spick_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    return sh_owns(v, e, r) && inv_count(s, COBBLE) >= 3 && sh_pick_uses(v, s) < SH_PICK_NEED;
}

/* lane/stronghold3: the stone pickaxe's next craft (C_PLANKS, C_TABLE, C_STICK, C_SPICK), -1 none: what craft_plan
 * makes first while sh_spick_want (its own order would make the liquid's or the iron's plans first) */
static int sh_spick_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    (void)v;
    int64_t logs = inv_count(s, LOG) + inv_count(s, LOG2), planks = inv_count(s, PLANKS), sticks = inv_count(s, STICK),
            tables = inv_count(s, TABLE);
    int near = near_table(e, r);
    if (!sh_spick_want(v, e, r, s))
    {
        /* the reserve met beside the table it placed: the table taken back (the next pickaxe's; golden-g26's 9 planks
         * make one table and its sticks, so the second pickaxe needs the first table) */
        if ((v->macros & NETH_VEC_CARRY) && sh_owns(v, e, r) && tables == 0 && near && e->steps >= e->take_retry &&
            r->screen == PS_NONE)
            return C_TAKE;
        return -1;
    }
    if (sticks >= 2 && (tables > 0 || near)) return C_SPICK;
    if (sticks < 2 && planks >= 2 && (tables > 0 || near || planks >= 6)) return C_STICK;
    if (tables == 0 && !near && planks >= (sticks >= 2 ? 4 : 6)) return C_TABLE;
    if (logs > 0) return C_PLANKS;
    return -1;
}

static int sh_picky(const struct neth_vec *v, int b)
{
    return (b >= 0 && b < 4096 && v->T.tool_of[b] == 2) || b == 98 || b == 97 || b == 109 || b == 1 || b == 4 ||
           b == 48 || b == 67 || b == 101 || b == 61 || b == 62;
}

/* the tool for block b: a sword for leaves, an axe or a shovel where held (_best_tool's), a pickaxe only for what
 * needs one (in the travel, TRAVEL, only while it keeps SH_PICK_KEEP uses for the dig down), else the hand (a slot
 * holding no tool); -2: nothing breaks it in time (a pick block with no pickaxe to spare) */
static int sh_tool_for(const struct neth_vec *v, const struct neth_rl_state *s, int b, int travel)
{
    if (b == 18 || b == 161)
        for (int i = 0; i < 9; ++i)             /* leaves: a sword's 1.5 */
            if (s->inv[i].item == 267 || s->inv[i].item == 276 || s->inv[i].item == 272 || s->inv[i].item == 268 ||
                s->inv[i].item == 283)
                return i;
    if (sh_picky(v, b))
    {
        int best = -1, bt = 0;
        for (int i = 0; i < 9; ++i)
        {
            int it = s->inv[i].item, k = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
            if (k > bt && (!travel || sh_uses_left(it, s->inv[i].damage) > SH_PICK_KEEP)) bt = k, best = i;
        }
        return best >= 0 ? best : -2;
    }
    int t = best_tool(&v->T, s, b);
    if (t >= 0 && v->T.pick[s->inv[t].item & 4095] == 0) return t;
    for (int i = 0; i < 9; ++i)                 /* the hand: an empty slot, else one holding no tool */
        if (s->inv[i].item <= 0) return i;
    for (int i = 0; i < 9; ++i)
    {
        int it = s->inv[i].item;
        if (it < 256 || it == 364 || it == 320 || it == 297) return i;   /* never the eyes (sh_drive's rule) */
    }
    return -1;
}

/* lane/stronghold3: a pickaxe in the pack (slots 9 to 35) with more than SH_PICK_KEEP uses left: its item, else -1
 * (the stone pickaxe the reserve made lands there with a full hotbar, where sh_tool_for does not look) */
static int sh_pack_pick_over(const struct neth_vec *v, const struct neth_rl_state *s, int min)
{
    for (int i = 9; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it] >= 1 &&
            sh_uses_left(it, s->inv[i].damage) > min)
            return it;
    }
    return -1;
}

static int sh_pack_pick(const struct neth_vec *v, const struct neth_rl_state *s)
{
    return sh_pack_pick_over(v, s, SH_PICK_KEEP);
}

static int sh_tool(const struct neth_vec *v, const struct neth_rl_state *s, int b)
{
    return sh_tool_for(v, s, b, 0);
}

static int sh_fetch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, int item);

/* lane/stronghold3: sh_tool, and with no pickaxe in the hotbar for a block that needs one, any pickaxe the pack holds
 * fetched (the hotbar's worn out on the way down): -3 while it fetches */
static int sh_tool_fetch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, int b)
{
    int t = sh_tool(v, s, b);
    if (t != -2) return t;
    int pk = sh_pack_pick_over(v, s, 0);
    if (pk < 0) return t;
    a->hold = 0;
    sh_fetch(v, e, r, s, a, pk);
    return -3;
}

/* lane/stronghold3: ITEM (the eyes, cobblestone) from the pack into the hotbar (fetch_item's click and close), the
 * inventory opened by E and held as the macro's own screen (sh_want) while it fetches; 0 */
static int sh_fetch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, int item)
{
    struct sh_env *h = &e->sh;
    h->fetch = e->steps;
    e->placed = 0;
    if (r->screen == PS_INVENTORY)
    {
        fetch_item(v, e, r, s, a, item);
        return 0;
    }
    still(a);
    if (r->screen == PS_NONE && e->steps - h->fpress >= SH_FETCH_PRESS)
    {
        a->press[PK_INVENTORY] = 1;
        h->fpress = e->steps;
    }
    return 0;
}

static int sh_roofed(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r);
static void sh_sky_begin(struct vec_env *e, const struct vec_last *r);

static void sh_look(struct neth_rl_act *a, double yaw, double pitch)
{
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
}

static void sh_scan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int mask, struct shview *o)
{
    shview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, SH_SCAN_R, mask, o);
    struct sh_env *h = &e->sh;
    if (o->brick_n && h->book.brick < 0) h->book.brick = (int32_t)e->steps;
    if (o->nframe && h->book.frame < 0) h->book.frame = (int32_t)e->steps;
    /* the frames remembered, their eyes as last seen */
    for (int i = 0; i < o->nframe; ++i)
    {
        int j = 0;
        while (j < h->nfr && !(h->fr[j].x == o->frame[i].x && h->fr[j].y == o->frame[i].y && h->fr[j].z == o->frame[i].z))
            ++j;
        if (j == h->nfr)
        {
            if (h->nfr == 12) continue;
            h->nfr += 1;
            h->fr[j].x = o->frame[i].x, h->fr[j].y = o->frame[i].y, h->fr[j].z = o->frame[i].z;
        }
        h->fr[j].eye = (o->frame[i].meta & 4) != 0;
    }
}

/* ---------------------------------------------------------------- the throw and the flight */

/* the flight's estimate from its last three samples a tick apart: est 1 a line (rising: 12 ahead and 8 up), 2 the room's
 * column (flying down) */
static void sh_estimate(struct sh_env *h)
{
    if (h->ns < 3) return;
    /* thrown over the room itself: it falls with next to no way across (under a block in 4 ticks, the target's
     * remaining distance under 1 slows it more): its column is where it falls */
    if (h->ns >= 5 && h->ft[h->ns - 1] - h->ft[0] == h->ns - 1 && h->fy[h->ns - 1] < h->fy[0] - 0.05 &&
        hypot(h->fx[h->ns - 1] - h->fx[0], h->fz[h->ns - 1] - h->fz[0]) < 0.25 * (h->ns - 1) * 0.02 + 0.05)
    {
        h->tx = h->fx[h->ns - 1], h->tz = h->fz[h->ns - 1];
        h->est = 2;
        return;
    }
    int c = h->ns - 1, b = c - 1, a0 = c - 2;
    if (h->ft[c] != h->ft[b] + 1 || h->ft[b] != h->ft[a0] + 1) return;
    double db = hypot(h->fx[b] - h->fx[a0], h->fz[b] - h->fz[a0]), dc = hypot(h->fx[c] - h->fx[b], h->fz[c] - h->fz[b]);
    if (dc < 1e-4) return;
    double rem = db + (dc - db) / 0.0025;     /* the remaining distance at b, toward the target */
    if (rem < 1.0 || rem > 13.0) return;
    double ux = (h->fx[c] - h->fx[b]) / dc, uz = (h->fz[c] - h->fz[b]) / dc;
    double acc = (h->fy[c] - h->fy[b]) - (h->fy[b] - h->fy[a0]);   /* > 0 below its target: rising */
    h->ux = ux, h->uz = uz;
    h->lsx = h->fx[b], h->lsz = h->fz[b];
    h->tx = h->fx[b] + rem * ux, h->tz = h->fz[b] + rem * uz;
    h->est = acc < 0.0 ? 2 : 1;
}

static int sh_throw(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    /* lane/stronghold3 (stronghold_roof sky): under a roof (golden-g25 begins in a tunnel at y 53: its throw hit the
     * ceiling and was lost) the staircase up to the sky first, toward a point ahead, then the throw */
    if (MOPT(MO_SH_ROOF) == SHR_SKY && sh_age(e) < 2 && sh_roofed(v, e, r))
    {
        double yr = r->yaw * M_PI / 180.0;
        h->gx = r->x - sin(yr) * 8.0, h->gz = r->z + cos(yr) * 8.0, h->gk = SG_THROW;
        sh_sky_begin(e, r);
        sh_phase(e, SH_GO);
        return 0;
    }
    int hb = sh_hotbar(s, EYE);
    if (hb < 0)
    {
        return sh_fetch(v, e, r, s, a, EYE);
    }
    /* lane/stronghold2: the site first. A thrown eye hovers 12 out and 8 up and drops there: over a tree it lies on
     * the canopy, out of reach (a third of the drops were lost so). Four quarter looks for leaves SHV_SITE_IN to
     * SH_SCAN_R across, up from the eye; SH_SITE_LEAVES or more seen, SH_SITE_STEP away from them and the site looked
     * at again, SH_SITE_MOVES times at most a throw */
    int age = sh_age(e), site = (h->snapped || (MOPT(MO_SH_LOCATE) != SHL_SNAP && h->nlines > 0)) &&
                                h->relocs < SH_SITE_MOVES;   /* the near throws' (the far one's was
                                                                              * picked up in 31 of 32) */
    if (site && age < 2 * 4 + 1)
    {
        if (age == 0) h->syaw = r->yaw, h->sitec = 0, h->sitex = h->sitez = 0.0;
        if (age % 2 == 1)
        {
            struct shview o;
            shview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, SH_SCAN_R, SHV_LEAVES, &o);
            h->sitec += o.leaves_n, h->sitex += o.leaves_x * o.leaves_n, h->sitez += o.leaves_z * o.leaves_n;
        }
        if (age < 2 * 4)
        {
            sh_look(a, h->syaw + 90.0 * (age / 2), -15.0);
            return 0;
        }
        if (h->sitec >= SH_SITE_LEAVES)
        {
            double cx = h->sitex / h->sitec, cz = h->sitez / h->sitec, dx = r->x - cx, dz = r->z - cz,
                   dd = hypot(dx, dz);
            if (dd < 0.5) dx = 1.0, dz = 0.0, dd = 1.0;
            h->relocs += 1;
            h->book.relocs += 1;
            h->gx = r->x + dx / dd * SH_SITE_STEP, h->gz = r->z + dz / dd * SH_SITE_STEP;
            h->gk = SG_THROW;
            sh_phase(e, SH_GO);
            return 0;
        }
    }
    /* the look: the heading known (else where the player looks), up from the ground and its blocks */
    double yaw = h->nlines ? degrees(atan2(h->line[h->nlines - 1].uz, h->line[h->nlines - 1].ux)) - 90.0 : r->yaw;
    sh_look(a, yaw, -40.0);
    a->hotbar = hb;
    if (s->current != hb || sh_age(e) < (site ? 2 * 4 + 3 : 2)) return 0;
    if (s->mo_hit && s->mo_block == END_FRAME) return 0;
    a->press[PK_USE] = 1;
    h->ox = r->x, h->oy = r->y + 1.62, h->oz = r->z;
    h->eid = 0, h->ns = 0, h->est = 0, h->died = 0, h->search = 0, h->hseen = 0, h->dseen = 0, h->canopy = 0, h->cdone = 0;
    h->eyes_after = (int)inv_count(s, EYE) - 1;
    h->tstep = e->steps;
    h->relocs = 0;
    h->pdn = 0;
    h->book.throws += 1;
    sh_phase(e, SH_WATCH);
    return 0;
}

static int sh_watch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    struct shview o;
    sh_scan(v, e, r, SHV_EYE, &o);
    int age = sh_age(e);
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV)
        fprintf(stderr, "watch t=%lld age=%d eye=%d (%.3f %.3f %.3f) ns=%d est=%d me=(%.2f %.2f %.2f) yaw=%.1f pitch=%.1f\n",
                (long long)e->steps, age, o.eye_id, o.eye_x, o.eye_y, o.eye_z, h->ns, h->est, r->x, r->y, r->z, r->yaw,
                r->pitch);
#endif
    if (age == 4 && inv_count(s, EYE) > h->eyes_after && h->eid == 0)
    {
        sh_phase(e, SH_THROW);                /* the use did not throw: again */
        h->book.throws -= 1;
        return 0;
    }
    if (o.eye_id && (h->eid == 0 || o.eye_id == h->eid))
    {
#ifdef SH_TRACE
        if (h->eid == 0) fprintf(stderr, "eyeflag env=%d t=%lld id=%d drop=%d\n", (int)(e - v->e), (long long)e->steps,
                                 o.eye_id, o.trace_drop);
#endif
        h->eid = o.eye_id;
        if (h->ns == SH_EYE_SAMPLES)
        {
            memmove(h->fx, h->fx + 1, (SH_EYE_SAMPLES - 1) * sizeof h->fx[0]);
            memmove(h->fy, h->fy + 1, (SH_EYE_SAMPLES - 1) * sizeof h->fy[0]);
            memmove(h->fz, h->fz + 1, (SH_EYE_SAMPLES - 1) * sizeof h->fz[0]);
            memmove(h->ft, h->ft + 1, (SH_EYE_SAMPLES - 1) * sizeof h->ft[0]);
            h->ns -= 1;
        }
        h->fx[h->ns] = o.eye_x, h->fy[h->ns] = o.eye_y, h->fz[h->ns] = o.eye_z, h->ft[h->ns] = e->steps;
        h->ns += 1;
        h->hx = o.eye_x, h->hy = o.eye_y, h->hz = o.eye_z, h->hseen = e->steps;
        sh_estimate(h);
        /* the look on it once it is clear of the player's eye (it starts there), else the throw's look kept */
        if (py_dist3(r->x, r->y + 1.62, r->z, o.eye_x, o.eye_y, o.eye_z) > 0.5)
        {
            double yaw, pitch;
            look_at(r->x, r->y + 1.62, r->z, o.eye_x, o.eye_y, o.eye_z, &yaw, &pitch);
            sh_look(a, yaw, pitch);
        }
        else
            sh_look(a, r->yaw, r->pitch);
    }
    else if (h->est == 0 && age < 40)
    {
        /* not in view (it left the spot it was thrown from out of the look): a third of a turn every two steps */
        if (age % 2 == 0) h->search += 1;
        sh_look(a, r->yaw + (age % 2 == 0 ? 120.0 : 0.0), -20.0);
    }
    else if (h->hseen && e->steps - h->hseen > 3 && !h->died)
        h->died = e->steps;                   /* out of view (or gone): the flight is over for the watch */
    int over = age >= SH_WATCH_MAX || (h->est && h->died) || (h->est == 2 && h->ns >= 4) ||
               (h->est == 1 && age >= 30);
    if (!over) return 0;
    if (h->est == 2)
    {
        /* flying down: within 12 blocks of the room, its column the target; the eye is lost underground */
        h->book.near += 1;
        h->book.lost += 1;
        /* the room's middle is a block's corner (StructureComponent.func_151553_a's integers): rounded, not floored,
         * so that a crossing a hair either side of it names the same column */
        h->cx = (int)lround(h->tx), h->cz = (int)lround(h->tz);
        h->gx = h->cx + 0.5, h->gz = h->cz + 0.5, h->gk = SG_DIG;
        sh_phase(e, SH_GO);
        return 0;
    }
    if (h->est == 1)
    {
        if (h->nlines == SH_LINES)
        {
            memmove(h->line, h->line + 1, (SH_LINES - 1) * sizeof h->line[0]);
            h->nlines -= 1;
        }
        h->line[h->nlines].sx = h->lsx, h->line[h->nlines].sz = h->lsz;
        h->line[h->nlines].ux = h->ux, h->line[h->nlines].uz = h->uz;
        h->nlines += 1;
        h->pair += 1;
        sh_phase(e, SH_PICK);
        return 0;
    }
    /* never in view (or its flight unread): a line along where the player looks, the pair begun again */
    h->book.lost += 1;
    h->pair = 0;
    sh_phase(e, SH_PLAN);
    return 0;
}

/* ---------------------------------------------------------------- walking */

/* one step toward (gx, gz) over the surface: sprinting, jumping when held up, swimming, digging ahead after a stall,
 * a detour round a drop or lava ahead. Returns the horizontal distance left */
static double sh_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double gx, double gz)
{
    struct sh_env *h = &e->sh;
    double ey = r->y + 1.62, dist = hypot(gx - r->x, gz - r->z);
    double yaw = degrees(atan2(gz - r->z, gx - r->x)) - 90.0;
    still(a);
    /* lane/stronghold3 (stronghold_stuck turn): no nearer the goal for SH_STUCK steps (a staircase up and a pillar
     * aside): whatever the walk was doing dropped, a long detour square to the way */
    if (MOPT(MO_SH_STUCK) == SHU_TURN)
    {
        /* begun again on a new goal and after a break the walk did not drive (a hide); a staircase or a pillar
         * climbing only holds the turn back (the wall pillar's tries reset it every 100 steps: golden-g26's env 4
         * at one block from step 18,230 to the end) */
        if (h->wbest_t == 0 || h->wgx != gx || h->wgz != gz || e->steps - h->wlast > SH_STUCK_GAP ||
            dist < h->wbest - SH_STUCK_GAIN)
            h->wbest = dist, h->wbest_t = e->steps, h->wgx = gx, h->wgz = gz;
        else if (e->steps - h->wbest_t > SH_STUCK && r->on_ground && !h->asc && !h->climb)
        {
            h->tunnel = 0, h->stalls = 0, h->tdug = 0;
            h->dside = !h->dside;
            h->dyaw = h->dside ? 90.0 : -90.0;
            h->dend = e->steps + SH_STUCK_TURN;
            h->dlast = e->steps;
            h->wbest = dist, h->wbest_t = e->steps;
            h->book.stuck += 1;
            /* the same place again (a pocket of stone round a staircase's top, golden-g26 under the frozen policy:
             * the detours turned inside it for 12,000 steps): the second time the climb to the sky (it spends the
             * pickaxe), from the third the walk's tunnel spends the dig's reserve too for the detour's length */
            h->nstuck = hypot(r->x - h->stx, r->z - h->stz) < SH_STUCK_NEAR ? h->nstuck + 1 : 1;
            h->stx = r->x, h->stz = r->z;
            if (h->nstuck == 2) sh_sky_begin(e, r);
            if (h->nstuck >= 3) h->spend = e->steps + SH_STUCK_TURN;
            /* (and every other time from the third, a pillar up on cobblestone: golden-g26's env 4 under the frozen
             * policy jumped at one block from step 18,230 to the end, through the detours, the sky and the reserve) */
            if (h->nstuck >= 3 && (h->nstuck & 1) && inv_count(s, COBBLE) > 0)
                h->climb = (int)e->steps, h->cy0 = r->y, h->book.pillars += 1;
        }
        h->wlast = e->steps;
    }
#ifdef SH_TRACE
#ifndef SH_TRACE_MAP_AT
#define SH_TRACE_MAP_AT 700     /* debugging only: the step the block map round the feet is printed */
#endif
    if (e == v->e + SH_TRACE_ENV && e->steps == SH_TRACE_MAP_AT)
    {
        int shview_block_trace(struct pipe *p, int id, int x, int y, int z);
        int fx = (int)floor(r->x), fy = (int)floor(r->y), fz = (int)floor(r->z);
        for (int y = fy + 3; y >= fy - 1; --y)
        {
            fprintf(stderr, "y=%d:", y);
            for (int z = fz - 2; z <= fz + 2; ++z)
            {
                fprintf(stderr, " ");
                for (int x = fx - 2; x <= fx + 2; ++x) fprintf(stderr, "%3d", shview_block_trace(v->p, 0, x, y, z));
                fprintf(stderr, " |");
            }
            fprintf(stderr, "\n");
        }
    }
    if (e == v->e + SH_TRACE_ENV && e->steps % 10 == 0)
        fprintf(stderr, "walk t=%lld ph=%d me=(%.2f %.2f %.2f) goal=(%.1f %.1f) d=%.1f tun=%d tk=%d dend=%lld stalls=%d "
                        "ground=%d water=%d mo=%d:%d(%d %d %d) asc=%d ak=%d a=(%d %d %d) d=%d ytop=%.1f\n",
                (long long)e->steps, h->phase, r->x, r->y, r->z, gx, gz, dist, h->tunnel, h->tk, (long long)h->dend,
                h->stalls, r->on_ground, s->eye_water, s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z, h->asc, h->ak,
                h->acx, h->acy, h->acz, h->ad, h->ytop);
#endif
    /* the way walked now: the goal's, turned by a detour while one lasts (the tunnel digs along it) */
    double wyaw = yaw + (e->steps < h->dend ? h->dyaw : 0.0);
    if (e->steps - h->astep > SH_STALL || h->astep == 0 || h->astep > e->steps)
    {
        /* lane/stronghold2: a stall in a detour counts too (a detour into a wall stood there for good, its probe
         * renewing it), and a stall SH_STREAK past the streak's first one is a new obstacle (a hill climbed a step
         * at a time), not the third at one place that turns the walk aside */
        if (h->astep > 0 && h->astep <= e->steps && hypot(r->x - h->ax, r->z - h->az) < SH_STALL_D && dist > 1.5 &&
            !h->tunnel)
        {
            /* the streak's progress is toward the goal (sliding along a wall is none) */
            if (h->stalls == 0 || h->std - dist >= SH_STREAK) h->stalls = 0, h->std = dist;
            h->stalls += 1;
            yaw = wyaw;
            /* in water a dig is slow (5 times, 25 off the ground) and the water follows: a detour instead, wider each
             * time */
            if ((s->eye_water || r->air < 300) && h->stalls >= 2 && inv_count(s, COBBLE) > 0)
            {
                h->climb = (int)e->steps, h->stalls = 0;     /* the banks too high to swim out: a pillar up */
                h->cy0 = r->y;
            }
            else if (h->stalls >= 3 || s->eye_water || r->air < 300)
            {
                if (e->steps - h->dlast > SH_SIDE_KEEP) h->dside = !h->dside;
                h->dlast = e->steps;
                h->dyaw = (h->dside ? 1.0 : -1.0) * (h->stalls >= 3 ? 70.0 : 60.0 + 30.0 * (h->stalls % 4));
                h->dend = e->steps + SH_DETOUR;
                if (h->stalls >= 3) h->stalls = 0;
            }
            else
            {
                /* the cells ahead along the nearest axis that the player's box (0.6 wide) sweeps: one or two columns,
                 * the feet's and the head's */
                h->tunnel = 1, h->tk = 0, h->tlook = (int)e->steps;
                int d = ((int)floor((yaw + 45.0) / 90.0) % 4 + 4) % 4;
                static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
                /* the rows the box covers (two on the ground; floating in water or a pit, three, and the one over
                 * them for the climb out) */
                /* the first stall at a spot: the room to step up (the rows over the feet: a 1-block step under leaves
                 * or a ledge); the second: the flat way through (the feet's row and the head's) */
                h->tcy = (int)floor(r->y + 0.01) + (h->stalls == 1 ? 1 : 0);
                h->tnh = 2;
                if (DX[d])
                {
                    h->tcx = h->tc2x = (int)floor(r->x) + DX[d];
                    h->tcz = (int)floor(r->z - 0.3), h->tc2z = (int)floor(r->z + 0.3);
                }
                else
                {
                    h->tcz = h->tc2z = (int)floor(r->z) + DZ[d];
                    h->tcx = (int)floor(r->x - 0.3), h->tc2x = (int)floor(r->x + 0.3);
                }
            }
        }
        else if (h->astep > 0 && hypot(r->x - h->ax, r->z - h->az) >= SH_STALL_D)
            h->stalls = 0;
        h->ax = r->x, h->az = r->z, h->astep = e->steps;
        yaw = degrees(atan2(gz - r->z, gx - r->x)) - 90.0;
    }
    /* the surface's height: the highest feet on the ground over the last SH_SURF_STEPS of the walk */
    /* fallen in: a landing SH_SURF_DROP or more under where the fall began (sh_books' fall, the step it landed), or
     * the feet under SH_CAVE_Y with a roof over the head (a look straight up meets a block within SH_SKY_RAY): the
     * staircase up to the fall's top (a cave: to SH_CAVE_Y + 14) */
    if (!h->asc && r->on_ground && !s->eye_water && (h->phase == SH_GO || h->phase == SH_PICK))
    {
        if (h->fall_step > h->ytop_t && h->fall >= SH_SURF_DROP)
            h->asc = 1, h->ytop = r->y + h->fall, h->ytop_t = h->fall_step;
        else if (MOPT(MO_SH_ROOF) == SHR_SKY && r->y < SH_SKY_Y && e->steps % 20 == 0)
        {
            if (sh_roofed(v, e, r)) sh_sky_begin(e, r);   /* lane/stronghold3: under the sea's height, to the sky */
        }
        else if (MOPT(MO_SH_ROOF) == SHR_CAVE && r->y < SH_CAVE_Y && e->steps % 20 == 0)
        {
            int bx, by, bz, b, m;
            if (shview_ray(v->p, (int)(e - v->e), r->x, ey, r->z, r->x, ey + SH_SKY_RAY, r->z, &bx, &by, &bz, &b, &m))
                h->asc = 1, h->ytop = SH_CAVE_Y + 14.0, h->ytop_t = e->steps;
        }
        if (h->asc) h->ak = 0, h->alook = 0, h->asc_t = e->steps;
    }
    if (h->asc && e->steps - h->asc_t > SH_ASC_STEPS) h->asc = h->sky = 0;   /* the walk's own way from here */
    if (h->asc)
    {
        /* fallen into a ravine or a cave: an upward staircase toward the goal, the cells over the head, ahead at the
         * head's and over it dug top-down, then a jump onto the cell ahead; over back near the surface's height
         * (lane/stronghold3's climb to the sky: over when the look up is clear) */
        if (r->y >= h->ytop - 1.5 || s->eye_water ||
            (h->sky && (e->steps - h->asc_t) % SH_SKY_CHECK == 0 && !sh_roofed(v, e, r)))
        {
            h->asc = h->sky = 0;
            h->ax = r->x, h->az = r->z, h->astep = e->steps;
            return dist;
        }
        static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
        int d = ((int)floor((yaw + 45.0) / 90.0) % 4 + 4) % 4;
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        if (h->ak == 0) h->acx = fx, h->acy = fy, h->acz = fz, h->ad = d, h->ak = 1, h->alook = 0;
        int ax = h->acx + DX[h->ad], az = h->acz + DZ[h->ad];
        if (h->ak <= 3)
        {
            int cx = h->ak == 1 ? h->acx : ax, cy = h->ak == 3 ? h->acy + 1 : h->acy + 2, cz = h->ak == 1 ? h->acz : az;
            double ly, lp;
            look_at(r->x, ey, r->z, cx + 0.5, cy + 0.5, cz + 0.5, &ly, &lp);
            sh_look(a, ly, lp);
            if (h->alook == 0) h->alook = (int)e->steps;
            if (e->steps - h->alook < 2) return dist;
            /* lane/stronghold3: any of the step's three cells under the crosshair is dug as it comes (the eye a third of
             * a block from the cell ahead sees its head-height block before the one over it: every level waited out
             * the 120 steps, golden-g25's climb from y 53 one block in 200 to 500 steps) */
            int any = s->mo_hit && ((s->mo_x == h->acx && s->mo_y == h->acy + 2 && s->mo_z == h->acz) ||
                                    (s->mo_x == ax && s->mo_z == az && (s->mo_y == h->acy + 1 || s->mo_y == h->acy + 2)));
            if (any && e->steps - h->alook < 120)
            {
                int t = sh_tool_for(v, s, s->mo_block, !h->sky);   /* the sky's climb spends the pickaxe */
                int pk = t == -2 ? sh_pack_pick(v, s) : -1;
                if (pk >= 0)
                {
                    h->alook = (int)e->steps;    /* lane/stronghold3: the pack's pickaxe into the hotbar first */
                    sh_fetch(v, e, r, s, a, pk);
                    return dist;
                }
                if (s->mo_block == 7 || t == -2 || (s->lq_hit && (s->lq_block == 10 || s->lq_block == 11)))
                {
                    h->asc = h->sky = 0;        /* bedrock, lava or stone with no pickaxe to spare: the walk's own detour */
                    h->dside = !h->dside, h->dyaw = h->dside ? 90.0 : -90.0, h->dend = e->steps + SH_DETOUR;
                    return dist;
                }
                a->hold = 1u << PK_ATTACK;
                if (t >= 0 && t != s->current) a->hotbar = t;
                return dist;
            }
            h->ak += 1, h->alook = 0;
            return dist;
        }
        /* the step up onto the cell ahead (or on along a cave's floor) */
        double ly, lp;
        look_at(r->x, ey, r->z, ax + 0.5, ey, az + 0.5, &ly, &lp);
        sh_look(a, ly, 0.0);
        a->hold = (1u << PK_FORWARD) | (1u << PK_JUMP);
        if (h->alook == 0) h->alook = (int)e->steps;
        if (fx != h->acx || fz != h->acz || fy != h->acy || e->steps - h->alook > 30) h->ak = 0;
        return dist;
    }
    if (h->climb)
    {
        /* the pillar: cobblestone selected, the look straight down, jumping, a use every third step (refused while
         * the box is in the cell; the first lands on the floor under the water); over once standing out of the water
         * 1.5 higher, or after 80 steps */
        int hb = sh_hotbar(s, COBBLE);
        /* lane/stronghold3: the pack's cobblestone into the hotbar first (the pillar ended at once without it: golden-g26
         * under the frozen policy, 12,000 steps at one stone wall with 257 cobblestone in the pack) */
        if (hb < 0 && inv_count(s, COBBLE) > 0 && e->steps - h->climb <= SH_FETCH_HOLD)
            return sh_fetch(v, e, r, s, a, COBBLE), dist;
        if ((r->on_ground && !s->eye_water && r->y >= h->cy0 + 1.5) || e->steps - h->climb > 80 || hb < 0)
        {
            h->climb = 0;
            h->ax = r->x, h->az = r->z, h->astep = e->steps;
            return dist;
        }
        a->hotbar = hb;
        sh_look(a, r->yaw, 90.0);
        a->hold = 1u << PK_JUMP;
        if (s->current == hb && (e->steps - h->climb) % 3 == 2) a->press[PK_USE] = 1;
        return dist;
    }
    if (h->tunnel)
    {
        /* each column's rows from the top down, judged by the crosshair and dug */
        int nh = h->tnh < 2 ? 2 : h->tnh;
        if (h->tk >= nh && h->tc2x == h->tcx && h->tc2z == h->tcz) h->tk = 2 * nh;
        if (h->tk >= 2 * nh)
        {
            h->tunnel = 0;
            /* lane/stronghold3 (stronghold_tunnel straight): a tunnel that broke a block is progress, the stall streak
             * begun again (else every third stall in a stone hill turned the walk aside: short side tunnels, round
             * and round at one place, golden-g26's envs at 95 to 181 blocks of 590) */
            if (h->tdug && MOPT(MO_SH_TUNNEL) == SHT_STRAIGHT) h->stalls = 0;
            h->tdug = 0;
            h->ax = r->x, h->az = r->z, h->astep = e->steps;
            return dist;
        }
        int cy = h->tcy + nh - 1 - h->tk % nh;
        int cx = h->tk < nh ? h->tcx : h->tc2x, cz = h->tk < nh ? h->tcz : h->tc2z;
        double ly, lp;
        look_at(r->x, ey, r->z, cx + 0.5, cy + 0.5, cz + 0.5, &ly, &lp);
        sh_look(a, ly, lp);
        if (e->steps - h->tlook < 2) return dist;
#ifdef SH_TRACE
        if (e == v->e + SH_TRACE_ENV)
            fprintf(stderr, "tun t=%lld tk=%d nh=%d cell=(%d %d %d) mo=%d:%d(%d %d %d) tool=%d cur=%d age=%lld\n",
                    (long long)e->steps, h->tk, nh, cx, cy, cz, s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z,
                    s->mo_hit ? sh_tool_for(v, s, s->mo_block, 1) : -9, s->current, (long long)(e->steps - h->tlook));
#endif
        if (e->steps - h->tlook > 80 || (s->lq_hit && (s->lq_block == 10 || s->lq_block == 11)))
        {
            h->tunnel = 0;
            h->dside = !h->dside;
            h->dyaw = h->dside ? 70.0 : -70.0;
            h->dend = e->steps + SH_DETOUR;
            return dist;
        }
        if (s->mo_hit && s->mo_x == cx && s->mo_y == cy && s->mo_z == cz)
        {
            /* (stuck a third time at one place: the dig's reserve spent too, stronghold_stuck) */
            int t = sh_tool_for(v, s, s->mo_block, e->steps >= h->spend);
            int pk = t == -2 ? sh_pack_pick(v, s) : -1;
            if (pk >= 0)
            {
                h->tlook = (int)e->steps;        /* lane/stronghold3: the pack's pickaxe into the hotbar first */
                sh_fetch(v, e, r, s, a, pk);
                return dist;
            }
            if (t == -2 && MOPT(MO_SH_WALL) == SHW_PILLAR && r->on_ground && !s->eye_water &&
                inv_count(s, COBBLE) > 0)
            {
                /* lane/stronghold3 (stronghold_wall pillar): stone with no pickaxe to spare: up it on cobblestone (the
                 * pillar's two blocks, then the walk on over the top; golden-g26's way crosses stone hills at y 89) */
                h->tunnel = 0;
                h->climb = (int)e->steps, h->cy0 = r->y, h->book.pillars += 1;
                return dist;
            }
            if (s->mo_block == 7 || s->mo_block == OBSIDIAN || t == -2) h->tlook = -1000;
            else h->tdug = 1;
            a->hold = (1u << PK_ATTACK) | (s->eye_water ? 1u << PK_JUMP : 0);
            if (t >= 0 && t != s->current) a->hotbar = t;
            return dist;
        }
        h->tk += 1;
        h->tlook = (int)e->steps;
        return dist;
    }
    if (e->steps < h->dend) yaw += h->dyaw;
    /* a drop deeper than SH_DROP_PROBE or lava 2 to 3 blocks ahead, seen down the look: a detour */
    /* lane/stronghold2: probed in a detour too (its own way: a detour walked off the cliff it went round), the side
     * kept for SH_SIDE_KEEP (round the obstacle one way, not back and forth), turned back when the detour's way is
     * the bad one */
    if (r->on_ground && !s->eye_water && e->steps % 2 == 0)
    {
        double yr = yaw * M_PI / 180.0, fx = r->x - sin(yr) * 2.5, fz = r->z + cos(yr) * 2.5;
        int bx, by, bz, b, m;
        int hit = shview_ray(v->p, (int)(e - v->e), r->x, ey, r->z, fx, r->y - SH_DROP_PROBE, fz, &bx, &by, &bz, &b, &m);
        int pit = hit == 1 && (b == 8 || b == 9) && by <= (int)floor(r->y + 0.01) - 2;   /* water in a pit: no way out */
        if (hit == 0 || pit || (hit == 1 && (b == 10 || b == 11 || b == 51)))
        {
            int in = e->steps < h->dend, quick = in && e->steps - h->dlast < 12;   /* both sides bad: back */
            if (in) yaw -= h->dyaw;
            if (in || e->steps - h->dlast > SH_SIDE_KEEP) h->dside = !h->dside;
            h->dlast = e->steps;
            h->dyaw = quick ? 180.0 : h->dside ? 90.0 : -90.0;
            h->dend = e->steps + SH_DETOUR;
            yaw += h->dyaw;
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV)
                fprintf(stderr, "probe t=%lld hit=%d b=%d at (%d %d %d) in=%d side=%d\n", (long long)e->steps, hit,
                        hit == 1 ? b : -1, bx, by, bz, in, h->dside);
#endif
        }
    }
    sh_look(a, yaw, s->eye_water ? -20.0 : 25.0);
    a->hold = 1u << PK_FORWARD;
    /* lane/stronghold3 (stronghold_sprint fed): the sprint only while SH_SPRINT_MEAT meats are held (0.1 exhaustion a
     * metre and its jumps' 0.8: from golden-g24 13 of 19 deaths while it ran starved on the second day) */
    if (r->food > 6 && !s->eye_water && (MOPT(MO_SH_SPRINT) == SHS_ALWAYS || meat_count(s) >= SH_SPRINT_MEAT))
        a->hold |= 1u << PK_SPRINT;
    double moved = hypot(r->x - h->lx, r->z - h->lz);
    /* held against anything (a step up on the ground, a bank swimming with the head out: the water's jump out) */
    if (s->eye_water || moved < 0.08) a->hold |= 1u << PK_JUMP;
    h->lx = r->x, h->lz = r->z;
    return dist;
}

/* lane/stronghold2: a drop on the canopy over the head (a tree's leaves 3 to 10 up, where a thrown eye lands when its
 * column is wooded): what is over the head broken from below, a block of cobblestone placed under the feet when the
 * next is out of reach (the water pillar's way: the look down, jumping, a use every third step), up to SH_PILLAR_MAX.
 * 1 while at it, 0 when the line up to ytop is clear (or nothing more to do) */
static int sh_canopy(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ytop)
{
    struct sh_env *h = &e->sh;
    double ey = r->y + 1.62;
    still(a);
    if (h->cmode == 1)
    {
        int hb = sh_hotbar(s, COBBLE);
        if (hb < 0 || (r->on_ground && r->y >= h->cpy + 0.9) || e->steps - h->cstep > 30)
        {
            h->cmode = 0;
            return 1;
        }
        a->hotbar = hb;
        sh_look(a, r->yaw, 90.0);
        a->hold = 1u << PK_JUMP;
        if (s->current == hb && (e->steps - h->cstep) % 3 == 2) a->press[PK_USE] = 1;
        return 1;
    }
    sh_look(a, r->yaw, -90.0);
    if (r->pitch > -80.0) return 1;              /* the look reaching straight up first */
    if (s->mo_hit && s->mo_y > (int)floor(ey))
    {
        int b = s->mo_block;
        if (b == 7 || b == OBSIDIAN) return 0;
        a->hold = 1u << PK_ATTACK;
        int t = sh_tool_fetch(v, e, r, s, a, b);
        if (t >= 0 && t != s->current) a->hotbar = t;
        return 1;
    }
    int bx, by, bz, b, m;
    if (shview_ray(v->p, (int)(e - v->e), r->x, ey, r->z, r->x, ytop, r->z, &bx, &by, &bz, &b, &m) != 1) return 0;
    if (sh_hotbar(s, COBBLE) < 0 || r->y >= h->ccy0 + SH_PILLAR_MAX || !r->on_ground) return r->on_ground ? 0 : 1;
    h->cmode = 1, h->cstep = e->steps, h->cpy = r->y;
    return 1;
}

/* ---------------------------------------------------------------- the plan */

/* the crossing of the pair's two lines: 1 with (x, z) and the throws' distance from it, 0 none (parallel, behind) */
static int sh_cross(const struct sh_env *h, double *x, double *z, double *near)
{
    if (h->nlines < 2) return 0;
    const double *p = &h->line[h->nlines - 2].sx, *q = &h->line[h->nlines - 1].sx;
    /* p: sx, sz, ux, uz */
    double det = p[2] * (-q[3]) - p[3] * (-q[2]);
    if (fabs(det) < 0.005) return 0;      /* under half a degree apart */
    double rx = q[0] - p[0], rz = q[1] - p[1];
    double ta = (rx * (-q[3]) - rz * (-q[2])) / det, tb = (p[2] * rz - p[3] * rx) / det;
    if (ta < 0.0 || tb < 0.0 || ta > 4000.0) return 0;
    *x = p[0] + ta * p[2], *z = p[1] + ta * p[3];
    *near = ta > tb ? ta : tb;
    return 1;
}

/* the newest line's chunk centres in the ring's band ahead of it: 1 with the start's (x, z) when one is on the line and
 * the next is clear of it (the books keep both distances across), else 0 */
static int sh_snap(struct sh_env *h, double *x, double *z)
{
    if (h->nlines < 1) return 0;
    const double sx = h->line[h->nlines - 1].sx, sz = h->line[h->nlines - 1].sz;
    const double ux = h->line[h->nlines - 1].ux, uz = h->line[h->nlines - 1].uz;
    double d1 = 1e9, d2 = 1e9;
    int b1x = 0, b1z = 0, b2x = 1 << 30, b2z = 0;
    for (double t = 0.0; t < SH_GRID_FAR; t += 4.0)
    {
        double px = sx + t * ux, pz = sz + t * uz;
        int i0 = (int)floor((px - 8.0) / 16.0), j0 = (int)floor((pz - 8.0) / 16.0);
        for (int di = 0; di < 2; ++di)
            for (int dj = 0; dj < 2; ++dj)
            {
                int gx = (i0 + di) * 16 + 8, gz = (j0 + dj) * 16 + 8;
                double rr = hypot((double)gx, (double)gz);
                if (rr < SH_RING_MIN || rr > SH_RING_MAX) continue;
                double along = (gx - sx) * ux + (gz - sz) * uz;
                if (along < 16.0 || (gx == b1x && gz == b1z && d1 < 1e9) || (gx == b2x && gz == b2z)) continue;
                double across = fabs((gx - sx) * uz - (gz - sz) * ux);
                if (across < d1)
                    d2 = d1, b2x = b1x, b2z = b1z, d1 = across, b1x = gx, b1z = gz;
                else if (across < d2)
                    d2 = across, b2x = gx, b2z = gz;
            }
    }
    h->book.snap_d = (float)d1, h->book.snap_d2 = (float)d2;
    if (d1 > SH_GRID_TOL || d2 < SH_GRID_SEP) return 0;
    *x = b1x, *z = b1z;
    return 1;
}

static void sh_plan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct sh_env *h = &e->sh;
    double x, z, near;
    int how = MOPT(MO_SH_LOCATE);
    if (how == SHL_FOLLOW && h->nlines > 0)
    {
        /* lane/macroopts, stronghold_locate follow (the simplest way, a player's without a calculator): along the newest
         * line SH_FOLLOW_STRIDE blocks, then another throw; a line that turned back past the last one's (the
         * stronghold walked past) halves the stride, down to SH_FOLLOW_MIN. Over the room an eye flies down (est 2,
         * sh_throw): the dig there */
        const double *p = &h->line[h->nlines - 1].sx;
        if (h->fstride <= 0.0) h->fstride = SH_FOLLOW_STRIDE;
        else if (h->nlines >= 2 && p[2] * h->line[h->nlines - 2].ux + p[3] * h->line[h->nlines - 2].uz < 0.0)
            h->fstride = fmax(SH_FOLLOW_MIN, h->fstride * 0.5);
        h->pair = 0;
        h->gx = r->x + p[2] * h->fstride, h->gz = r->z + p[3] * h->fstride;
        h->gk = SG_THROW;
        sh_phase(e, SH_GO);
        return;
    }
    if (how == SHL_SNAP && h->pair == 1 && !h->snapped && sh_snap(h, &x, &z))
    {
        /* one far line: the start's chunk centre, a near pair thrown there (the room generated by then) */
        h->snapped = 1;
        h->book.snaps += 1;
        h->pair = 0;
        h->gx = x, h->gz = z;
        h->gk = SG_THROW;
        if (SH_VIA_PERP > 0.0)
        {
            /* by way of a point SH_VIA_PERP left of the line (lane/stronghold's far pair walked so, its second throw's
             * place; the straight line met two walls that the walk could not pass in half of the envs) */
            const double *p = &h->line[h->nlines - 1].sx;
            h->vx = x, h->vz = z;
            h->gx = r->x - p[3] * SH_VIA_PERP, h->gz = r->z + p[2] * SH_VIA_PERP;
            h->gk = SG_VIA;
        }
        sh_phase(e, SH_GO);
        return;
    }
    if (h->pair >= 2 && sh_cross(h, &x, &z, &near))
    {
        h->pair = 0;
        h->gx = x, h->gz = z;
        h->gnear = near;
        h->gk = near < SH_NEAR_PAIR ? SG_DIG : SG_THROW;
        if (h->gk == SG_DIG) h->cx = (int)lround(x), h->cz = (int)lround(z), h->gx = h->cx + 0.5, h->gz = h->cz + 0.5;
        sh_phase(e, SH_GO);
        return;
    }
    if (h->pair == 1 && h->nlines > 0)
    {
        /* the pair's second throw from SH_PERP across the first line (left of it; right after a parallel pair) */
        const double *p = &h->line[h->nlines - 1].sx;
        double side = (h->nlines & 2) ? -1.0 : 1.0;
        h->gx = r->x - p[3] * SH_PERP * side, h->gz = r->z + p[2] * SH_PERP * side;
        h->gk = SG_THROW;
        sh_phase(e, SH_GO);
        return;
    }
    if (h->pair >= 2 && h->nlines > 0)
    {
        /* parallel or behind: along the newest line 200 blocks, then a new pair */
        const double *p = &h->line[h->nlines - 1].sx;
        h->pair = 0;
        h->gx = r->x + p[2] * 200.0, h->gz = r->z + p[3] * 200.0;
        h->gk = SG_THROW;
        sh_phase(e, SH_GO);
        return;
    }
    h->pair = 0;
    sh_phase(e, SH_THROW);
}

/* ---------------------------------------------------------------- the dig */

/* the spiral's cells round the column, each a step down from the last (a 1x2 staircase in a 3x3 ring) */
static const int SPX[8] = {-1, 0, 1, 1, 1, 0, -1, -1}, SPZ[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

static int sh_isbrick(int b) { return b == 98 || b == 97 || b == 101 || b == END_FRAME; }

/* the crosshair judges cell (x, y, z): 1 clear (the crosshair passes it), 0 being dug (or looked at), -1 bedrock or
 * obsidian, 2 a stronghold block (not dug) */
static int sh_cell(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int x, int y, int z, int dig_brick)
{
    struct sh_env *h = &e->sh;
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, x + 0.5, y + 0.5, z + 0.5, &yaw, &pitch);
    sh_look(a, yaw, pitch);
    if (h->slook == 0) h->slook = (int)e->steps;
    if (e->steps - h->slook < 2) return 0;
    if (s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z)
    {
        int b = s->mo_block;
        if (b == 7 || b == OBSIDIAN) return -1;
        if (sh_isbrick(b))
        {
            if (h->book.brick < 0) h->book.brick = (int32_t)e->steps;
            if (!dig_brick) return 2;
        }
        a->hold = 1u << PK_ATTACK;
        int t = sh_tool_fetch(v, e, r, s, a, b);
        if (t >= 0 && t != s->current) a->hotbar = t;
        return 0;
    }
    if (s->lq_hit && s->lq_x == x && s->lq_y == y && s->lq_z == z && (s->lq_block == 10 || s->lq_block == 11)) return -1;
    h->slook = 0;
    return 1;
}

static int sh_dig(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                  struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV && e->steps % 20 == 0)
        fprintf(stderr, "dig t=%lld me=(%.2f %.2f %.2f) col=(%d %d) sk=%d sidx=%d next=(%d %d %d) sfail=%d mo=%d:%d(%d %d %d)\n",
                (long long)e->steps, r->x, r->y, r->z, h->cx, h->cz, h->sk, h->sidx, h->scx, h->scy, h->scz, h->sfail,
                s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z);
#endif
    if (fy <= SH_MIN_Y || h->sfail >= SH_DIG_TRIES || h->slevel > SH_LEVEL_MAX) return -1;
    if (e->steps % 5 == 0)
    {
        struct shview o;
        sh_scan(v, e, r, SHV_BLOCKS, &o);
        /* a brick under the feet's level within the ring: the room's roof (no other piece is over the room) */
        if (o.brick_n && o.by < fy && abs(o.bx - h->cx) <= 2 && abs(o.bz - h->cz) <= 2)
        {
            h->roof = o.by;
            h->rk = 0;
            sh_phase(e, SH_ROOF);
            return 0;
        }
    }
    /* the next cell: the spiral's from the one the feet stand in (the nearest ring cell first); picked again when the
     * feet left the cell it was picked from before the walk */
    int kk = h->sk >= 10 ? h->sk - 10 : h->sk;
    if (h->sk < 0 || (kk < (h->sk >= 10 ? 3 : 4) && (fx != h->sfx || fz != h->sfz || fy != h->scy)))
    {
        h->sfx = fx, h->sfz = fz;
        int cur = -1;
        for (int i = 0; i < 8; ++i)
            if (fx == h->cx + SPX[i] && fz == h->cz + SPZ[i]) cur = i;
        if (cur < 0)
        {
            /* off the ring: walk onto its nearest cell first (the column's own cell: any) */
            int best = 0;
            double bd = 1e9;
            for (int i = 0; i < 8; ++i)
            {
                double d = hypot(h->cx + SPX[i] + 0.5 - r->x, h->cz + SPZ[i] + 0.5 - r->z);
                if (d < bd) bd = d, best = i;
            }
            if (fx == h->cx && fz == h->cz) best = 7;
            h->sidx = best;
            h->scx = h->cx + SPX[best], h->scz = h->cz + SPZ[best], h->scy = fy;
            h->sk = 10;                       /* a level walk: head and feet only */
            h->slook = 0;
        }
        else
        {
            /* the next ring cell, always a neighbour of the feet's (lane/stronghold2: one farther round was two cells
             * off, and the walk in met the cell between): the way round turned back at a refused cell (lava or a
             * cave under it); both neighbours refused, a level move onto one (head and feet, no step down) */
            int dir = h->sdir ? h->sdir : 1;
            h->sidx = (cur + dir + 8) & 7;
            h->scx = h->cx + SPX[h->sidx], h->scz = h->cz + SPZ[h->sidx], h->scy = fy;
            h->sk = h->sfail >= 2 ? 10 : 0;
            h->slook = 0;
        }
    }
    int level = h->sk >= 10, k = level ? h->sk - 10 : h->sk;
    int ncell = level ? 2 : 3;
    if (k < ncell)
    {
        int cy = k == 0 ? h->scy + 1 : k == 1 ? h->scy : h->scy - 1;
        int c = sh_cell(v, e, r, s, a, h->scx, cy, h->scz, 0);
        if (c == -1)
        {
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV)
                fprintf(stderr, "refuse t=%lld cell (%d %d %d) k=%d level=%d mo=%d:%d lq=%d:%d\n", (long long)e->steps,
                        h->scx, cy, h->scz, k, level, s->mo_hit, s->mo_block, s->lq_hit, s->lq_block);
#endif
            h->sfail += 1;
            h->sdir = -(h->sdir ? h->sdir : 1);
            h->sk = -1;
            return 0;
        }
        if (c == 2)
        {
            /* the roof (or a wall) in the next cell */
            h->roof = cy < fy ? cy : fy - 1;
            h->rk = 0;
            sh_phase(e, SH_ROOF);
            return 0;
        }
        if (c == 1) h->sk += 1;
        return 0;
    }
    if (k == ncell)
    {
        /* the floor the feet will stand on: a block, no liquid down to it */
        int fl = level ? h->scy - 1 : h->scy - 2;
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, h->scx + 0.5, fl + 0.5, h->scz + 0.5, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        if (h->slook == 0) h->slook = (int)e->steps;
        if (e->steps - h->slook < 2) return 0;
        h->slook = 0;
        /* a block there (or at most two lower in its column), or the line to it met another block first (the ground
         * the staircase is cut in); nothing within reach is an open cave under it */
        int incol = s->mo_hit && s->mo_x == h->scx && s->mo_z == h->scz;
        int solid = s->mo_hit && (!incol || (s->mo_y <= fl && s->mo_y >= fl - 2));
        if (incol && s->mo_y <= fl && s->mo_y >= fl - 2 && s->mo_y < fl) solid = 1;
        int wet = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && s->lq_x == h->scx && s->lq_z == h->scz;
        if (solid && sh_isbrick(s->mo_block))
        {
            h->roof = fl;
            h->rk = 0;
            sh_phase(e, SH_ROOF);
            return 0;
        }
        if (!solid && !wet)
        {
            /* open under it: the look down the column (the screen's middle past the crosshair's reach); a landing
             * within 3 blocks, not a liquid, is a step down a little farther; lane/stronghold2: a cave's floor up to
             * SH_CAVE_DROP under it too, when the fall leaves SH_FALL_HP (the dig dropped into it, on from its floor;
             * a cave at y 47 under seed 42's room refused every cell round the column) */
            int bx, by, bz, b, m;
            int hit = shview_ray(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, h->scx + 0.5, fl - 3.5, h->scz + 0.5,
                                 &bx, &by, &bz, &b, &m);
            /* deeper, the line from the cell's middle crosses the feet's own floor first: the column looked down
             * from over its brink (the eye to the floor cell, clear to the crosshair, then down the column) */
            if (!(hit == 1 && bx == h->scx && bz == h->scz) &&
                shview_ray(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, h->scx + 0.5, fl + 0.5, h->scz + 0.5, &bx,
                           &by, &bz, &b, &m) == 0)
                hit = shview_ray(v->p, (int)(e - v->e), h->scx + 0.5, fl + 0.5, h->scz + 0.5, h->scx + 0.5,
                                 fl - SH_CAVE_DROP - 0.5, h->scz + 0.5, &bx, &by, &bz, &b, &m);
            double fall = h->scy - (by + 1.0);
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV)
                fprintf(stderr, "cave t=%lld hit=%d at (%d %d %d) b=%d fall=%.1f hp=%.1f\n", (long long)e->steps, hit,
                        hit == 1 ? bx : 0, hit == 1 ? by : 0, hit == 1 ? bz : 0, hit == 1 ? b : -1, fall, r->health);
#endif
            if (hit == 1 && bx == h->scx && bz == h->scz && by >= fl - SH_CAVE_DROP && !(b >= 8 && b <= 11) &&
                b != 51 && (by >= fl - 3 || r->health - (fall > 3.0 ? ceil(fall - 3.0) : 0.0) >= SH_FALL_HP))
                solid = 1;
        }
        if (!solid || wet)
        {
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV)
                fprintf(stderr, "refuse-floor t=%lld cell (%d %d %d) fl=%d level=%d solid=%d wet=%d mo=%d:%d(%d %d %d)\n",
                        (long long)e->steps, h->scx, h->scy, h->scz, fl, level, solid, wet, s->mo_hit, s->mo_block,
                        s->mo_x, s->mo_y, s->mo_z);
#endif
            h->sfail += 1;
            h->sdir = -(h->sdir ? h->sdir : 1);
            h->sk = -1;                       /* the other neighbour instead (a cave or water under this one) */
            return 0;
        }
        h->sk += 1;
        return 0;
    }
    /* the walk in (a step down, or a level move) */
    if (fx == h->scx && fz == h->scz)
    {
        h->slevel = level ? h->slevel + 1 : 0;
        h->sk = -1;
        h->sfail = 0;
        return 0;
    }
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, h->scx + 0.5, r->y + 1.32, h->scz + 0.5, &yaw, &pitch);
    sh_look(a, yaw, pitch);
    a->hold = 1u << PK_FORWARD;
    if (h->slook == 0) h->slook = (int)e->steps;
    if (e->steps - h->slook > 40) h->sk = level ? 10 : 0, h->slook = 0;
    return 0;
}

/* the roof found: into the room's column at the roof's height plus one, then down through the roof */
static int sh_roof(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (sh_age(e) > 600) return -1;
    if (r->y < h->roof)
    {
        /* through: in the room */
        if (h->book.room < 0) h->book.room = (int32_t)e->steps, h->book.room_eyes = (int32_t)inv_count(s, EYE);
        h->reyes = inv_count(s, EYE), h->rpick = 0;
        sh_phase(e, SH_ROOM);
        h->rlook = 0;
        return 0;
    }
    if (fx == h->cx && fz == h->cz)
    {
        /* in the column: the cell under the feet dug (down to the roof, then the roof) */
        if (!r->on_ground) return 0;
        int c = sh_cell(v, e, r, s, a, fx, fy - 1, fz, 1);
        if (c == -1) return -1;
        if (c == 1) h->slook = 0;
        return 0;
    }
    /* beside it: the step into the column (a corner of the ring goes by its side first) */
    if (fx != h->rfx || fz != h->rfz) h->rfx = fx, h->rfz = fz, h->rk = 0, h->slook = 0;
    int tx = h->cx, tz = h->cz;
    if (fx != h->cx && fz != h->cz) tx = fx, tz = h->cz;   /* the orthogonal neighbour first */
    if (abs(tx - fx) + abs(tz - fz) != 1)
    {
        /* farther off: walk to the column */
        double d = sh_walk(v, e, r, s, a, h->cx + 0.5, h->cz + 0.5);
        (void)d;
        return 0;
    }
    if (h->rk < 2)
    {
        int cy = h->rk == 0 ? fy + 1 : fy;
        int c = sh_cell(v, e, r, s, a, tx, cy, tz, 1);
        if (c == -1) return -1;
        if (c == 1) h->rk += 1;
        return 0;
    }
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, tx + 0.5, r->y + 1.32, tz + 0.5, &yaw, &pitch);
    sh_look(a, yaw, pitch);
    a->hold = 1u << PK_FORWARD;
    if (sh_age(e) % 60 == 59) h->rk = 0;
    return 0;
}

/* ---------------------------------------------------------------- the room */

/* (x, z) over the ring's inside (the 3x3 the frames close: lava two under the portal), with four or more frames
 * remembered */
static int sh_inring(const struct sh_env *h, double x, double z)
{
    if (h->nfr < 4) return 0;
    int mnx = 1 << 30, mxx = -(1 << 30), mnz = 1 << 30, mxz = -(1 << 30);
    for (int i = 0; i < h->nfr; ++i)
    {
        mnx = h->fr[i].x < mnx ? h->fr[i].x : mnx, mxx = h->fr[i].x > mxx ? h->fr[i].x : mxx;
        mnz = h->fr[i].z < mnz ? h->fr[i].z : mnz, mxz = h->fr[i].z > mxz ? h->fr[i].z : mxz;
    }
    return x >= mnx + 1 && x < mxx && z >= mnz + 1 && z < mxz;
}

static int sh_room(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    double ey = r->y + 1.62;
    struct shview o;
    /* the frames: looked for round the player (down at the ring), remembered; an eye lying in view */
    sh_scan(v, e, r, SHV_BLOCKS | SHV_DROP, &o);
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV)
    {
        fprintf(stderr, "room t=%lld age=%d me=(%.2f %.2f %.2f) yaw=%.0f pitch=%.0f eyes=%lld nfr=%d open=%d mo=%d:%d(%d %d %d) m%d fr:",
                (long long)e->steps, sh_age(e), r->x, r->y, r->z, r->yaw, r->pitch, (long long)inv_count(s, EYE), h->nfr,
                sh_frame_open(h), s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z, s->mo_meta);
        for (int i = 0; i < h->nfr; ++i) fprintf(stderr, " (%d %d %d)%d", h->fr[i].x, h->fr[i].y, h->fr[i].z, h->fr[i].eye);
        fprintf(stderr, "\n");
    }
#endif
    if (h->used_at && e->steps - h->used_at < 4) return 0;
    if (h->used_at)
    {
        h->used_at = 0;
        for (int i = 0; i < h->nfr; ++i)
            if (h->fr[i].x == h->ulx && h->fr[i].y == h->uly && h->fr[i].z == h->ulz && s->mo_hit &&
                s->mo_x == h->ulx && s->mo_y == h->uly && s->mo_z == h->ulz && (s->mo_meta & 4))
                h->fr[i].eye = 1;
    }
    /* lit: the portal seen (lane/stronghold2: every frame filled, the twelfth perhaps never remembered), or the 12
     * remembered with their eyes */
    if (o.portal_n || (h->nfr == 12 && sh_frame_open(h) == 0))
    {
        if (o.portal_n) h->ptseen = 1, h->ptx = o.px, h->pty = o.py, h->ptz = o.pz;
        if (h->book.lit < 0) h->book.lit = (int32_t)e->steps;
        sh_phase(e, SH_ENTER);
        return 0;
    }
    int64_t eyes = inv_count(s, EYE);
    if (eyes > h->reyes && !h->used_at) h->book.picked += (int32_t)(eyes - h->reyes);
    h->reyes = eyes;
    /* lane/stronghold2: an eye lying in the room (the throw that flew down here falls into it) picked up first, never
     * from the ring's middle (the lava under the portal), sneaking (off no edge) */
    if (o.drop_n && h->rpick < SH_RPICK_MAX && !sh_inring(h, o.drop_x, o.drop_z) && fabs(o.drop_y - r->y) < 3.0 &&
        !h->used_at)
    {
        h->rpick += 1;
        double yaw, pitch;
        look_at(r->x, ey, r->z, o.drop_x, o.drop_y + 0.125, o.drop_z, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
        if (hypot(r->x - h->lx, r->z - h->lz) < 0.03) a->hold |= 1u << PK_JUMP;
        h->lx = r->x, h->lz = r->z;
        return 0;
    }
    if (sh_age(e) > SH_ROOM_MAX) return -1;
    if (eyes == 0) return -1;                /* out of eyes short of the twelfth frame (and no portal seen) */
    if (sh_age(e) < 16 && h->nfr < 12)
    {
        /* a look round, down at the floor: four quarters */
        sh_look(a, r->yaw + (sh_age(e) % 4 == 0 ? 90.0 : 0.0), 45.0);
        return 0;
    }
    /* the nearest open frame within reach: the crosshair onto its top, an eye put in */
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < h->nfr; ++i)
    {
        if (h->fr[i].eye || h->fskip[i] > e->steps) continue;
        double d = py_dist3(r->x, ey, r->z, h->fr[i].x + 0.5, h->fr[i].y + 0.8125, h->fr[i].z + 0.5);
        if (d < bd) bd = d, best = i;
    }
    if (best < 0)
    {
        /* every frame remembered has its eye (fewer than 12 seen), or the open ones were left for now: look round
         * again (the left ones tried again after SH_SKIP) */
        sh_look(a, r->yaw + 45.0, 50.0);
        return 0;
    }
    int x = h->fr[best].x, y = h->fr[best].y, z = h->fr[best].z;
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV)
        fprintf(stderr, "rbest t=%lld best=(%d %d %d) bd=%.2f rgo=%d cur=%d hb=%d\n", (long long)e->steps, x, y, z, bd,
                h->rgo, s->current, sh_hotbar(s, EYE));
#endif
    if (bd > SH_REACH)
    {
        /* out of reach: a block toward the ring's middle (onto the frame row from the top step) */
        int mnx = 1 << 30, mxx = -(1 << 30), mnz = 1 << 30, mxz = -(1 << 30);
        for (int i = 0; i < h->nfr; ++i)
        {
            mnx = h->fr[i].x < mnx ? h->fr[i].x : mnx, mxx = h->fr[i].x > mxx ? h->fr[i].x : mxx;
            mnz = h->fr[i].z < mnz ? h->fr[i].z : mnz, mxz = h->fr[i].z > mxz ? h->fr[i].z : mxz;
        }
        double mx = (mnx + mxx) / 2.0 + 0.5, mz = (mnz + mxz) / 2.0 + 0.5;
        int fx = (int)floor(r->x), fz = (int)floor(r->z);
        int sx = fx + (mx - (fx + 0.5) > 0.6 ? 1 : mx - (fx + 0.5) < -0.6 ? -1 : 0);
        int sz = fz + (mz - (fz + 0.5) > 0.6 ? 1 : mz - (fz + 0.5) < -0.6 ? -1 : 0);
        int onframe = 0, standing = -1;
        for (int i = 0; i < h->nfr; ++i)
        {
            onframe |= h->fr[i].x == sx && h->fr[i].z == sz;
            if (h->fr[i].x == fx && h->fr[i].z == fz && fabs(r->y - (h->fr[i].y + 0.8125)) < 0.3) standing = i;
        }
        if (standing >= 0)
        {
            /* lane/stronghold2: on the ring's tops (fallen through the roof onto them): along the ring, sneaking (off
             * no edge: the lava inside), frame to neighbouring frame, toward the nearest one with this frame in reach */
            int goal = -1;
            double gd = 1e9;
            for (int i = 0; i < h->nfr; ++i)
            {
                double reach = py_dist3(h->fr[i].x + 0.5, h->fr[i].y + 0.8125 + 1.62, h->fr[i].z + 0.5, x + 0.5,
                                        y + 0.8125, z + 0.5);
                double dd = hypot(h->fr[i].x - fx, h->fr[i].z - fz);
                if (reach < SH_REACH - 0.3 && dd < gd) gd = dd, goal = i;
            }
            int next = -1;
            double nd = 1e9;
            for (int i = 0; goal >= 0 && i < h->nfr; ++i)
            {
                int ddx = abs(h->fr[i].x - fx), ddz = abs(h->fr[i].z - fz);
                if ((ddx > 1 || ddz > 1) || (ddx == 0 && ddz == 0)) continue;
                double dd = hypot(h->fr[i].x - h->fr[goal].x, h->fr[i].z - h->fr[goal].z);
                if (dd < nd) nd = dd, next = i;
            }
            if (next >= 0 && nd < hypot(fx - h->fr[goal].x, fz - h->fr[goal].z))
            {
                double yaw, pitch;
                look_at(r->x, ey, r->z, h->fr[next].x + 0.5, ey - 0.5, h->fr[next].z + 0.5, &yaw, &pitch);
                sh_look(a, yaw, pitch);
                a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
                return 0;
            }
        }
        if (standing < 0 && MOPT(MO_SH_ROOM_WALK) == SHM_OUTSIDE && r->y < y + 0.5 && h->rgo < SH_RGO_MAX)
        {
            /* lane/stronghold3 (stronghold_room_walk outside): on the room's floor under the ring (fallen off its
             * tops), to the frame's outside, SH_RGO_OUT past it away from the ring's middle, where its top is in reach
             * (golden-g26 seed 1 env 27: 9 frames filled from the tops, then 700 steps on the floor 6 blocks from the
             * last 3 and given up) */
            double ox = x + 0.5 - mx, oz = z + 0.5 - mz, on = hypot(ox, oz);
            if (on < 0.5) ox = 1.0, oz = 0.0, on = 1.0;
            double gx = x + 0.5 + ox / on * SH_RGO_OUT, gz = z + 0.5 + oz / on * SH_RGO_OUT, yaw, pitch;
            h->rgo += 1;
            look_at(r->x, ey, r->z, gx, ey, gz, &yaw, &pitch);
            sh_look(a, yaw, 10.0);
            a->hold = 1u << PK_FORWARD;
            if (hypot(r->x - h->lx, r->z - h->lz) < 0.03) a->hold |= 1u << PK_JUMP;   /* a step, the stairs */
            h->lx = r->x, h->lz = r->z;
            return 0;
        }
        if (!onframe || (sx == fx && sz == fz))
        {
            h->fskip[best] = e->steps + SH_SKIP;   /* unreachable from here: left for now (never taken as filled) */
            return 0;
        }
        double yaw, pitch;
        look_at(r->x, ey, r->z, sx + 0.5, ey - 0.5, sz + 0.5, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
        return 0;
    }
    double yaw, pitch;
    look_at(r->x, ey, r->z, x + 0.5, y + 0.8125, z + 0.5, &yaw, &pitch);
    sh_look(a, yaw, pitch);
    int hb = sh_hotbar(s, EYE);
    if (hb < 0)
    {
        return sh_fetch(v, e, r, s, a, EYE);
    }
    a->hotbar = hb;
    /* lane/stronghold3: the crosshair on another remembered frame on the way (the row's nearer one, left before by
     * SH_SKIP, hides the chosen one: golden-g26 seed 1 env 27, 3 open frames in reach for 600 steps): that one */
    if (s->mo_hit && s->mo_block == END_FRAME && !(s->mo_x == x && s->mo_y == y && s->mo_z == z))
        for (int i = 0; i < h->nfr; ++i)
            if (h->fr[i].x == s->mo_x && h->fr[i].y == s->mo_y && h->fr[i].z == s->mo_z && !h->fr[i].eye)
            {
                best = i, x = s->mo_x, y = s->mo_y, z = s->mo_z;
                break;
            }
    if (s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z && s->mo_block == END_FRAME)
    {
        if (s->mo_meta & 4)
        {
            h->fr[best].eye = 1;
            return 0;
        }
        if (s->current == hb && r->on_ground)
        {
            /* lane/stronghold3: the look held where it is on the press (the aim at the frame's top edge moved the
             * crosshair onto the block beside it, and an eye used on any other block is thrown: golden-g26 envs with
             * 12 eyes used and the portal unlit) */
            sh_look(a, r->yaw, r->pitch);
            a->press[PK_USE] = 1;
            h->used_at = e->steps;
            h->ulx = x, h->uly = y, h->ulz = z;
            h->book.filled += 1;
        }
    }
    else if (sh_age(e) % 40 == 39)
        h->fskip[best] = e->steps + SH_SKIP;  /* the crosshair never reached it: another first */
    return 0;
}

/* the frames' ring's middle (the remembered frames' box) */
static void sh_ring_mid(const struct sh_env *h, double *mx, double *mz)
{
    int mnx = 1 << 30, mxx = -(1 << 30), mnz = 1 << 30, mxz = -(1 << 30);
    for (int i = 0; i < h->nfr; ++i)
    {
        mnx = h->fr[i].x < mnx ? h->fr[i].x : mnx, mxx = h->fr[i].x > mxx ? h->fr[i].x : mxx;
        mnz = h->fr[i].z < mnz ? h->fr[i].z : mnz, mxz = h->fr[i].z > mxz ? h->fr[i].z : mxz;
    }
    *mx = (mnx + mxx) / 2.0 + 0.5, *mz = (mnz + mxz) / 2.0 + 0.5;
}

static int sh_enter(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    if (sh_age(e) > SH_ENTER_MAX) return -1;
    if (e->steps % 4 == 0)
    {
        struct shview o;
        sh_scan(v, e, r, SHV_BLOCKS, &o);
        if (o.portal_n) h->ptseen = 1, h->ptx = o.px, h->pty = o.py, h->ptz = o.pz;
    }
    /* onto the portal: the block seen (lane/stronghold2), else the ring's middle; over the frame in the way with a jump */
    double mx, mz;
    sh_ring_mid(h, &mx, &mz);
    if (h->ptseen) mx = h->ptx + 0.5, mz = h->ptz + 0.5;
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, mx, r->y, mz, &yaw, &pitch);
    sh_look(a, yaw, pitch);
    a->hold = 1u << PK_FORWARD;
    if (hypot(r->x - h->lx, r->z - h->lz) < 0.03) a->hold |= 1u << PK_JUMP;
    h->lx = r->x, h->lz = r->z;
    (void)v, (void)s;
    return 0;
}

/* lane/stronghold2: stopped in the room (given up, out of eyes): walked off the frame row and away from the ring's
 * middle, the lava under the portal (a fall off the ring's outer side is under 3 blocks), until SH_LEAVE_D away;
 * 1 when done */
static int sh_leave(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    still(a);
    double mx, mz;
    sh_ring_mid(h, &mx, &mz);
    double d = hypot(r->x - mx, r->z - mz);
    if (d >= SH_LEAVE_D || sh_age(e) > SH_LEAVE_MAX)
    {
        if (d >= SH_LEAVE_D) h->book.stepped += 1;
        return 1;
    }
    double yaw = d > 0.05 ? degrees(atan2(r->z - mz, r->x - mx)) - 90.0 : r->yaw;
    sh_look(a, yaw, 20.0);
    a->hold = 1u << PK_FORWARD;
    if (hypot(r->x - h->lx, r->z - h->lz) < 0.03 && sh_age(e) > 3) a->hold |= 1u << PK_JUMP;
    h->lx = r->x, h->lz = r->z;
    (void)v, (void)s;
    return 0;
}

/* ---------------------------------------------------------------- lane/shold: stronghold_oracle locate (PRIVILEGED) */


static int sho_id(const struct neth_vec *v, const struct vec_env *e) { return (int)(e - v->e); }
static int sho_feet_y(const struct vec_last *r) { return (int)floor(r->y + 0.6); }
static int sho_water_at(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    int b = shoracle_block(v->p, sho_id(v, e), x, y, z, NULL);
    return b == 8 || b == 9;
}

/* a block dug without letting a liquid in: no lava, fire or water on any of its six sides, no sand or gravel over it */
static int sho_safe_dig(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int k = 0; k < 6; ++k)
    {
        int b = shoracle_block(v->p, sho_id(v, e), x + D[k][0], y + D[k][1], z + D[k][2], NULL);
        if (b < 0 || shoracle_hot(b) || b == 8 || b == 9 || (k == 2 && (b == 12 || b == 13))) return 0;
    }
    return 1;
}

/* the room's middle (the frames' ring's) */
static void sho_mid(const struct sh_env *h, double *mx, double *mz)
{
    *mx = (h->oroom.box[0] + h->oroom.box[3]) / 2.0 + 0.5, *mz = (h->oroom.box[2] + h->oroom.box[5]) / 2.0 + 0.5;
    if (h->oroom.nfr >= 4)
    {
        double sx = 0, sz = 0;
        for (int i = 0; i < h->oroom.nfr; ++i) sx += h->oroom.fr[i][0] + 0.5, sz += h->oroom.fr[i][2] + 0.5;
        *mx = sx / h->oroom.nfr, *mz = sz / h->oroom.nfr;
    }
}

static int sho_in_room(const struct sh_env *h, const struct vec_last *r)
{
    const int32_t *b = h->oroom.box;
    return r->x >= b[0] && r->x < b[3] + 1 && r->z >= b[2] && r->z < b[5] + 1 && r->y >= b[1] && r->y < b[4] + 1;
}

/* the walk along shoracle_path's cells to (tx, ty, tz): each block in the next move's way dug (the top first, the one
 * over the head before a step up), then the move; the way made again when walked off it, when old, when its end (the
 * reach of the server's chunks) is reached. 1 there, 0 going, -1 no way (SHO_NOPATH_MAX tries in a row) */
static int sho_follow(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, int tx, int ty, int tz, double near, int flags)
{
    struct sh_env *h = &e->sh;
    int id = sho_id(v, e);
    int fx = (int)floor(r->x), fy = sho_feet_y(r), fz = (int)floor(r->z);
    int wet = sho_water_at(v, e, fx, (int)floor(r->y + 0.1), fz);
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV && e->steps % 500 == 0 && !r->on_ground)
    {
        fprintf(stderr, "ofaf t=%lld y=%.4f g=%d flags=%x screen=%d", (long long)e->steps, r->y, r->on_ground, r->flags,
                r->screen);
        for (int ax = (int)floor(r->x - 0.3); ax <= (int)floor(r->x + 0.3); ++ax)
            for (int az = (int)floor(r->z - 0.3); az <= (int)floor(r->z + 0.3); ++az)
                for (int ay = fy - 2; ay <= fy + 2; ++ay)
                {
                    int sm = 0, cm = 0;
                    int sb = shoracle_block(v->p, id, ax, ay, az, &sm), cb = obsview_block(v->p, id, ax, ay, az, &cm);
                    fprintf(stderr, " %d,%d,%d=%d:%d/%d:%d", ax, ay, az, sb, sm, cb, cm);
                }
        fprintf(stderr, "\n");
    }
#endif
    still(a);
    a->look_mode = 1;
    a->look[0] = r->yaw, a->look[1] = 10.0f;
    int gdy = (flags & SHO_EXACT) ? 0 : (flags & SHO_ANYY) ? 256 : SHO_GOAL_DY;
    double wkd = hypot(tx + 0.5 - r->x, tz + 0.5 - r->z);   /* (lane/pdig: WK_STAMP's, counting only) */
#define SHO_WK(C) WK_STAMP(e, WK_SHO + (C), wkd, ty - fy, h->nop)
    if (wkd <= near && abs(fy - ty) <= gdy &&
        (r->on_ground || wet))
        return SHO_WK(1), 1;
    if (tx != h->otgt[0] || ty != h->otgt[1] || tz != h->otgt[2] || flags != h->oflags)
    {
        h->otgt[0] = tx, h->otgt[1] = ty, h->otgt[2] = tz, h->oflags = flags;
        h->nop = 0, h->onew = 1;
    }
    /* sneak_drop: a box wedged in a block (out of a dug hole, the feet under a step's top: off the ground, every step
     * the same place; s42-eye attempt 28 held 13,000 steps at y 59.7, then at 59.07): the block the feet's level
     * overlaps is dug after 20 such steps */
    if (MOPT(MO_SNEAK_DROP) && !r->on_ground && !wet)
    {
        if (r->x == h->owp[0] && r->y == h->owp[1] && r->z == h->owp[2]) h->owst += 1;
        else h->owst = 0, h->owp[0] = r->x, h->owp[1] = r->y, h->owp[2] = r->z;
        if (h->owst >= 20)
        {
            int wy = (int)floor(r->y);
            for (int ax = (int)floor(r->x - 0.3); ax <= (int)floor(r->x + 0.3); ++ax)
                for (int az = (int)floor(r->z - 0.3); az <= (int)floor(r->z + 0.3); ++az)
                {
                    int b = shoracle_block(v->p, id, ax, wy, az, NULL);
                    if (b <= 0 || (b >= 8 && b <= 11)) continue;
                    if (sh_cell(v, e, r, s, a, ax, wy, az, 0) == 0) return SHO_WK(14), 0;
                }
        }
    }
    else h->owst = 0;
    int k = -1;
    for (int j = h->opk - 1 < 0 ? 0 : h->opk - 1; j < h->nop && j <= h->opk + 3; ++j)
        if (h->op[j][0] == fx && h->op[j][2] == fz && abs(h->op[j][1] - fy) <= 1) k = j;
    if (k >= 0) h->opk = k + 1, h->ooff = 0;
    else if (h->nop > 0 && (r->on_ground || wet)) h->ooff += 1;
    if (h->nop > 0 && h->opk >= h->nop && (r->on_ground || wet)) h->nop = 0, h->onew = 1;   /* a partial way's end */
    if (h->nop == 0 && !h->onew && e->steps - h->ot < SHO_RETRY)
        return SHO_WK(2), h->ofail >= SHO_NOPATH_MAX ? -1 : 0;
    if (h->nop == 0 || h->ooff > SHO_OFF || e->steps - h->ot > SHO_AGE)
    {
        if (!r->on_ground && !wet)
        {
            a->hold = 1u << PK_FORWARD;
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV && e->steps % 20 == 0)
                fprintf(stderr, "ofa t=%lld me=(%.3f %.3f %.3f) yaw=%.1f nop=%d off=%d age=%lld b=%d,%d,%d,%d\n",
                        (long long)e->steps, r->x, r->y, r->z, r->yaw, h->nop, h->ooff,
                        (long long)(e->steps - h->ot), shoracle_block(v->p, id, fx, fy - 2, fz, NULL),
                        shoracle_block(v->p, id, fx, fy - 1, fz, NULL), shoracle_block(v->p, id, fx, fy, fz, NULL),
                        shoracle_block(v->p, id, fx, fy + 1, fz, NULL));
#endif
            return SHO_WK(3), 0;
        }
        h->onew = 0, h->ot = e->steps, h->ooff = 0, h->opk = 0;
        h->nop = shoracle_path(v->p, id, fx, fy, fz, tx, ty, tz, near, flags, h->op, SH_OPATH);
        h->book.opaths += 1;
#ifdef SH_TRACE
        if (e == v->e + SH_TRACE_ENV)
        {
            fprintf(stderr, "opath t=%lld from %d %d %d to %d %d %d len %d:", (long long)e->steps, fx, fy, fz, tx, ty, tz,
                    h->nop);
            for (int j = 0; j < h->nop && j < 8; ++j) fprintf(stderr, " %d,%d,%d", h->op[j][0], h->op[j][1], h->op[j][2]);
            if (h->nop > 8) fprintf(stderr, " .. %d,%d,%d", h->op[h->nop - 1][0], h->op[h->nop - 1][1], h->op[h->nop - 1][2]);
            fprintf(stderr, "\n");
        }
#endif
        if (h->nop <= 0)
        {
            h->nop = 0;
            h->ofail += 1;
            return SHO_WK(4), h->ofail >= SHO_NOPATH_MAX ? -1 : 0;
        }
        h->ofail = 0;
    }
    if (h->opk >= h->nop) h->opk = h->nop - 1;
    const int16_t *c = h->op[h->opk];
#ifdef SH_TRACE
    if (e == v->e + SH_TRACE_ENV && e->steps % 20 == 0)
        fprintf(stderr, "ofw t=%lld me=(%.2f %.2f %.2f) g=%d wet=%d k=%d/%d next=(%d %d %d) off=%d dig=(%d %d %d) mo=%d:%d\n",
                (long long)e->steps, r->x, r->y, r->z, r->on_ground, wet, h->opk, h->nop, c[0], c[1], c[2], h->ooff,
                h->odx, h->ody, h->odz, s->mo_hit, s->mo_block);
    if (e == v->e + SH_TRACE_ENV && e->steps % 200 == 0 && h->ooff > 3)
    {
        /* lane/pdig: the 3 x 3 columns around the feet, the floor, feet and head (server/client, :meta) */
        fprintf(stderr, "ofb t=%lld hold=%x yaw=%.1f", (long long)e->steps, a->hold, r->yaw);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int m0 = 0, m1 = 0;
                    int b0 = shoracle_block(v->p, id, fx + dx, fy + dy, fz + dz, &m0);
                    int b1 = obsview_block(v->p, id, fx + dx, fy + dy, fz + dz, &m1);
                    fprintf(stderr, " %d,%d,%d=%d:%d/%d:%d", dx, dy, dz, b0, m0, b1, m1);
                }
        fprintf(stderr, "\n");
    }
#endif
    /* the blocks in the move's way, the top first (the one over the head before a step up) */
    int cl[8][3], n = 0;
    if (c[1] > fy) cl[n][0] = fx, cl[n][1] = fy + 2, cl[n][2] = fz, ++n;
    int top = fy + 1 > c[1] + 1 ? fy + 1 : c[1] + 1;
    for (int y = top; y >= c[1] && n < 8; --y) cl[n][0] = c[0], cl[n][1] = y, cl[n][2] = c[2], ++n;
    for (int i = 0; i < n; ++i)
    {
        int b = shoracle_block(v->p, id, cl[i][0], cl[i][1], cl[i][2], NULL);
        if (b < 0 || shoracle_open(b) || b == 8 || b == 9) continue;
        /* dig it: the crosshair on it, the attack held with its tool */
        if (cl[i][0] != h->odx || cl[i][1] != h->ody || cl[i][2] != h->odz)
            h->odx = cl[i][0], h->ody = cl[i][1], h->odz = cl[i][2], h->odig_t = e->steps;
        int dig_max = MOPT(MO_FOOD_WOOD_ROUTE) && r->dim == 0 &&
                      (b == FURNACE || b == FURNACE_LIT) && best_tool(&v->T, s, FURNACE) < 0 ?
                      SHO_DIG_BARE_FURNACE : SHO_DIG_MAX;
        if (e->steps - h->odig_t > dig_max)
        {
            h->nop = 0, h->onew = 1, h->odx = -(1 << 20);   /* stuck on it: another way */
            return SHO_WK(5), 0;
        }
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, cl[i][0] + 0.5, cl[i][1] + 0.5, cl[i][2] + 0.5, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        if (s->mo_hit && e->steps - h->odig_t >= 1)
        {
            int mb = s->mo_block;
            if (mb == END_FRAME || mb == 119 || mb == 97 || mb == 7 || mb == 54)
            {
                if (!(s->mo_x == cl[i][0] && s->mo_y == cl[i][1] && s->mo_z == cl[i][2])) return SHO_WK(6), 0;
                h->nop = 0, h->onew = 1;
                return SHO_WK(6), 0;
            }
            /* a block in front of the one wanted: dug only when nothing liquid or hot touches it (its six sides) */
            int mine = 0;
            for (int j = 0; j < n; ++j) mine |= s->mo_x == cl[j][0] && s->mo_y == cl[j][1] && s->mo_z == cl[j][2];
            if (!mine && !sho_safe_dig(v, e, s->mo_x, s->mo_y, s->mo_z))
            {
                if (e->steps - h->odig_t > 40) h->nop = 0, h->onew = 1, h->odx = -(1 << 20);
                return SHO_WK(7), 0;
            }
            a->hold = 1u << PK_ATTACK;
            int t = sh_tool_fetch(v, e, r, s, a, mb);
            if (t >= 0 && t != s->current) a->hotbar = t;
            if (t == -3) return SHO_WK(8), 0;
        }
        if (!r->on_ground && !wet) a->hold &= ~(1u << PK_ATTACK);
        return SHO_WK(8), 0;
    }
    /* SHO_BRIDGE: the next cell's floor is air: a cobblestone placed against its solid side (sneaking at the edge) */
    if ((flags & SHO_BRIDGE) && c[1] == fy && shoracle_block(v->p, id, c[0], c[1] - 1, c[2], NULL) == 0)
    {
        int nb[3];
        if (!shoracle_bridge_face(v->p, id, c[0], c[1] - 1, c[2], fx, fz, nb))
        {
            h->nop = 0, h->onew = 1;
            return SHO_WK(9), 0;
        }
        int hb = sh_hotbar(s, COBBLE);
        if (hb < 0) return SHO_WK(10), inv_count(s, COBBLE) > 0 ? sh_fetch(v, e, r, s, a, COBBLE) : -1;
        a->hotbar = hb;
        a->hold = 1u << PK_SNEAK;
        /* the shared face's middle, a hair inside the solid block */
        double px = (nb[0] + c[0]) / 2.0 + 0.5 + 0.02 * (nb[0] - c[0]);
        double py = (nb[1] + c[1] - 1) / 2.0 + 0.5 + 0.02 * (nb[1] - (c[1] - 1));
        double pz = (nb[2] + c[2]) / 2.0 + 0.5 + 0.02 * (nb[2] - c[2]);
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, px, py, pz, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        static const int SD[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        if (s->mo_hit && s->mo_side >= 0 && s->mo_side < 6 && s->current == hb && e->steps - h->ouse >= 4 &&
            s->mo_x + SD[s->mo_side][0] == c[0] && s->mo_y + SD[s->mo_side][1] == c[1] - 1 &&
            s->mo_z + SD[s->mo_side][2] == c[2])
        {
            sh_look(a, r->yaw, r->pitch);
            a->press[PK_USE] = 1;
            h->ouse = e->steps;
            h->book.bridges += 1;
        }
        else if (e->steps - h->ot > SHO_BRIDGE_MAX)
            h->nop = 0, h->onew = 1;
        return SHO_WK(11), 0;
    }
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, c[0] + 0.5, r->y + 1.62, c[2] + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = 10.0f;
    a->hold = 1u << PK_FORWARD;
    if (c[1] > fy || wet || s->eye_water) a->hold |= 1u << PK_JUMP;
    /* A finished wood bridge may descend one block onto a solid floor. Sneaking at that edge never steps off it. */
    int wood_drop = MOPT(MO_FOOD_WOOD_ROUTE) && (flags & SHO_BRIDGE) && c[1] == fy - 1 &&
                    shoracle_floor(shoracle_block(v->p, id, c[0], c[1] - 1, c[2], NULL));
    /* lane/pdig (sneak_drop on): the way's next cell one to three below, its landing safe (a floor or water under it,
     * nothing hot in it, over it or under it, the column down to it open): sneak let go, so the step off the ledge is
     * taken (sneak refuses any step off an edge: s42-eye's wood walk stood at ledges on 49.7 in 100 of its still steps) */
    if (!wood_drop && MOPT(MO_SNEAK_DROP) && (flags & SHO_SNEAK_F) && c[1] < fy && c[1] >= fy - 3)
    {
        int under = shoracle_block(v->p, id, c[0], c[1] - 1, c[2], NULL), safe = 1;
        safe = under >= 0 && (shoracle_floor(under) || under == 8 || under == 9);
        for (int y = c[1] - 1; y <= fy + 1 && safe; ++y)
        {
            int b = shoracle_block(v->p, id, c[0], y, c[2], NULL);
            if (b < 0 || shoracle_hot(b)) safe = 0;
            else if (y >= c[1] && !shoracle_open(b) && b != 8 && b != 9) safe = 0;
        }
        wood_drop = safe;
    }
    /* ... and the feet over a hole (the cell under the feet's own open: the player perched on its rim by a hair of the
     * blocks beside, where sneak refuses every move but back; lane/pdig's trace: all 171 of one attempt's long stands
     * off the way), its bottom within three and safe: sneak let go, the player drops in and the way goes on from there */
    if (!wood_drop && MOPT(MO_SNEAK_DROP) && (flags & SHO_SNEAK_F) && r->on_ground &&
        shoracle_open(shoracle_block(v->p, id, fx, fy - 1, fz, NULL)))
    {
        int safe = 0;
        for (int y = fy - 1; y >= fy - 3 && !safe; --y)
        {
            int b = shoracle_block(v->p, id, fx, y, fz, NULL), u = shoracle_block(v->p, id, fx, y - 1, fz, NULL);
            if (b < 0 || u < 0 || shoracle_hot(b) || shoracle_hot(u) || !(shoracle_open(b) || b == 8 || b == 9)) break;
            if (shoracle_floor(u) || u == 8 || u == 9) safe = 1;
        }
        wood_drop = safe;
    }
    if ((flags & SHO_SNEAK_F) && !wood_drop) a->hold |= 1u << PK_SNEAK;
    else if (c[1] == fy && !wet && r->food > 6 && h->opk + 2 < h->nop && h->op[h->opk + 1][1] == fy &&
             h->op[h->opk + 2][1] == fy)
        a->hold |= 1u << PK_SPRINT;
    SHO_WK((a->hold & (1u << PK_SNEAK)) ? 13 : 12), WK_FRONT(e, yaw);
    {
        int ndy = c[1] - fy, man = abs(c[0] - fx) + abs(c[2] - fz);
        e->wkc = (ndy < -4 ? 0 : ndy > 4 ? 8 : ndy + 4) + 16 * (man > 3 ? 3 : man) + 64 * (wood_drop != 0);
    }
    return 0;
#undef SHO_WK
}

/* a cell to fill frame f from: the feet standing (a floor under, the two cells open: the server's blocks), the eye
 * within SHO_REACH of its top and over it, no lava beside; the nearest to the player into out; 1 found */
static int sho_stand(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const int32_t *f,
                     int out[3])
{
    int id = sho_id(v, e), found = 0;
    double bd = 1e9;
    for (int dx = -3; dx <= 3; ++dx)
        for (int dz = -3; dz <= 3; ++dz)
            for (int dy = 0; dy <= 2; ++dy)
            {
                int x = f[0] + dx, y = f[1] + dy, z = f[2] + dz;
                if (dx == 0 && dz == 0 && dy == 0) continue;
                int fb = shoracle_block(v->p, id, x, y, z, NULL), hb = shoracle_block(v->p, id, x, y + 1, z, NULL),
                    lb = shoracle_block(v->p, id, x, y - 1, z, NULL);
                if (!shoracle_open(fb) || !shoracle_open(hb) || !shoracle_floor(lb)) continue;
                int hot = 0;
                for (int k = 0; k < 4 && !hot; ++k)
                    for (int yy = y - 1; yy <= y + 1; ++yy)
                        hot |= shoracle_hot(shoracle_block(v->p, id, x + (k == 0) - (k == 1), yy,
                                                           z + (k == 2) - (k == 3), NULL));
                if (hot) continue;
                double fy = y + (lb == END_FRAME ? -0.1875 : 0.0);
                double d = py_dist3(x + 0.5, fy + 1.62, z + 0.5, f[0] + 0.5, f[1] + 0.8125, f[2] + 0.5);
                if (d > SHO_REACH || fy + 1.62 < f[1] + 1.2) continue;
                /* the frame's top in sight from there (sneaking: the eye 1.54 up) */
                if (!shoracle_sees(v->p, id, x + 0.5, fy + 1.54, z + 0.5, f[0] + 0.5, f[1] + 0.8125, f[2] + 0.5, f[0],
                                   f[1], f[2]))
                    continue;
                double dp = hypot(x + 0.5 - r->x, z + 0.5 - r->z) + 0.5 * abs(y - sho_feet_y(r)) + 0.3 * d;
                if (dp < bd) bd = dp, found = 1, out[0] = x, out[1] = y, out[2] = z;
            }
    return found;
}

static int sh_oracle(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    int id = sho_id(v, e);
    still(a);
    if (h->ostart == 0) h->ostart = e->steps;
    if (h->osub == SO_LOCATE)
    {
        if (shoracle_locate(v->p, id, r->x, r->z, &h->oroom) < 0 || !h->oroom.ok) return -1;
        h->oread = e->steps;
        h->obest = 1e9, h->obest_t = e->steps;
        h->osub = SO_TRAVEL;
#ifdef SH_TRACE
        if (e == v->e + SH_TRACE_ENV)
            fprintf(stderr, "oracle t=%lld room box %d %d %d .. %d %d %d start chunk %d %d loaded %d frames %d sp %d\n",
                    (long long)e->steps, h->oroom.box[0], h->oroom.box[1], h->oroom.box[2], h->oroom.box[3],
                    h->oroom.box[4], h->oroom.box[5], h->oroom.scx, h->oroom.scz, h->oroom.loaded, h->oroom.nfr,
                    h->oroom.has_sp);
#endif
        return 0;
    }
    double mx, mz;
    sho_mid(h, &mx, &mz);
    double dh = hypot(mx - r->x, mz - r->z);
    /* the room's blocks read again while near (frames, eyes, the spawner, the portal) */
    if (dh < SHO_DESCEND_D + 16 && (e->steps - h->oread >= SHO_READ || !h->oroom.loaded))
        shoracle_read(v->p, id, &h->oroom), h->oread = e->steps;
    if (sho_in_room(h, r) && h->book.room < 0)
        h->book.room = (int32_t)e->steps, h->book.room_eyes = (int32_t)inv_count(s, EYE);
    if (h->oroom.nfr && h->book.frame < 0 && dh < 24) h->book.frame = (int32_t)e->steps;
    if (h->oroom.portal && h->book.lit < 0) h->book.lit = (int32_t)e->steps;
    if (e->steps - h->odrive > SHO_GAP) h->obest_t = e->steps;     /* steps it did not drive (the hide) */
    h->odrive = e->steps;
    if (dh < h->obest - SHO_GAIN_D) h->obest = dh, h->obest_t = e->steps;
    if (e->steps - h->obest_t > SHO_STALL && h->osub <= SO_DOWN) return -1;     /* no nearer in that long */
    int fy = sho_feet_y(r);
    switch (h->osub)
    {
    case SO_TRAVEL:
    {
        if (dh < SHO_DESCEND_D && h->oroom.loaded && h->oroom.nfr >= 12)
        {
            h->osub = SO_DOWN, h->nop = 0, h->onew = 1, h->ofail = 0;
            return 0;
        }
        /* the leg: SHO_LEG ahead toward the room, at the feet's height */
        double ux = (mx - r->x) / dh, uz = (mz - r->z) / dh, leg = dh < SHO_LEG ? dh : SHO_LEG;
        int lx = (int)floor(r->x + ux * leg), lz = (int)floor(r->z + uz * leg);
        if (h->nop == 0 || abs(lx - h->olx) + abs(lz - h->olz) > 12) h->olx = lx, h->oly = fy, h->olz = lz;
        int rc = sho_follow(v, e, r, s, a, h->olx, h->oly, h->olz, 3.0, SHO_ANYY);
        if (rc != 0) h->nop = 0, h->onew = 1, h->olx = lx, h->oly = fy, h->olz = lz;
        if (rc < 0) h->ofail = 0;   /* (the stall rule above gives up) */
        return 0;
    }
    case SO_DOWN:
    {
        /* lane/proom (stronghold_room_start room): latched standing in the room it saw, the way down is behind it: the
         * spawner's and the frames' steps walk from here (seed 2's library start: the stand cell beside the spawner
         * on the dais, the dig down from the entrance knocked to the floor, no way back up found from there in 64 of
         * 64 attempts, SHO_NOPATH_MAX plans, given up) */
        if (MOPT(MO_SH_ROOM_START) == SHRS_ROOM && h->rstart && sho_in_room(h, r) && r->on_ground)
        {
            h->osub = h->oroom.has_sp ? SO_SPAWNER : SO_FILL;
            h->ostand_ok = 0, h->nop = 0, h->onew = 1, h->ofr = -1, h->pstart = e->steps;
            return 0;
        }
        /* the room's floor: the cell standing nearest the spawner's side (the stairs), else the middle's nearest */
        if (!h->ostand_ok)
        {
            int best[3] = {0, 0, 0}, found = 0;
            double bd = 1e9;
            const int32_t *b = h->oroom.box;
            for (int x = b[0] + 1; x < b[3]; ++x)
                for (int z = b[2] + 1; z < b[5]; ++z)
                    for (int y = b[1] + 1; y < b[4] - 1; ++y)
                    {
                        int f0 = shoracle_block(v->p, id, x, y, z, NULL), f1 = shoracle_block(v->p, id, x, y + 1, z, NULL),
                            fl = shoracle_block(v->p, id, x, y - 1, z, NULL);
                        if (!shoracle_open(f0) || !shoracle_open(f1) || !shoracle_floor(fl) || fl == END_FRAME) continue;
                        int hot = 0;
                        for (int k = 0; k < 4; ++k)
                            for (int yy = y - 1; yy <= y + 1; ++yy)
                                hot |= shoracle_hot(shoracle_block(v->p, id, x + (k == 0) - (k == 1), yy,
                                                                   z + (k == 2) - (k == 3), NULL));
                        if (hot) continue;
                        double tx = h->oroom.has_sp ? h->oroom.sp[0] + 0.5 : mx, tz = h->oroom.has_sp ? h->oroom.sp[2] + 0.5 : mz;
                        double d = hypot(x + 0.5 - tx, z + 0.5 - tz) + 0.01 * hypot(x + 0.5 - r->x, z + 0.5 - r->z);
                        if (h->oroom.has_sp && d < 1.0) continue;
                        if (d < bd) bd = d, found = 1, best[0] = x, best[1] = y, best[2] = z;
                    }
            if (!found) return -1;
            memcpy(h->ostand, best, sizeof best);
            h->ostand_ok = 1;
        }
        int rc = sho_follow(v, e, r, s, a, h->ostand[0], h->ostand[1], h->ostand[2], 0.6, SHO_EXACT);
        if (rc < 0) return -1;
        if (rc > 0)
        {
            h->osub = h->oroom.has_sp ? SO_SPAWNER : SO_FILL;
            h->ostand_ok = 0, h->nop = 0, h->onew = 1, h->ofr = -1, h->pstart = e->steps;
        }
        return 0;
    }
    case SO_SPAWNER:
    {
        /* the silverfish spawner broken (the crosshair on it from where it is in reach, the pickaxe held) */
        if (!h->oroom.has_sp || sh_age(e) > SHO_SPAWNER_MAX)
        {
            h->osub = SO_FILL, h->ostand_ok = 0, h->ofr = -1;
            return 0;
        }
        const int32_t *p = h->oroom.sp;
        double d = py_dist3(r->x, r->y + 1.62, r->z, p[0] + 0.5, p[1] + 0.5, p[2] + 0.5);
        if (d > 3.8)
        {
            if (!h->ostand_ok)
            {
                int f[4] = {p[0], p[1] - 1, p[2], 0};
                if (!sho_stand(v, e, r, f, h->ostand)) h->ostand[0] = p[0], h->ostand[1] = p[1], h->ostand[2] = p[2] + 2;
                h->ostand_ok = 1;
            }
            if (sho_follow(v, e, r, s, a, h->ostand[0], h->ostand[1], h->ostand[2], 0.6, SHO_EXACT | SHO_SNEAK_F | SHO_BRIDGE) < 0)
                h->osub = SO_FILL, h->ostand_ok = 0, h->ofr = -1;
            return 0;
        }
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, p[0] + 0.5, p[1] + 0.5, p[2] + 0.5, &yaw, &pitch);
        sh_look(a, yaw, pitch);
        if (s->mo_hit && s->mo_x == p[0] && s->mo_y == p[1] && s->mo_z == p[2])
        {
            a->hold = 1u << PK_ATTACK;
            int t = sh_tool_fetch(v, e, r, s, a, 52);
            if (t >= 0 && t != s->current) a->hotbar = t;
        }
        else if (s->mo_hit && s->mo_block != END_FRAME && s->mo_block != 119)
        {
            a->hold = 1u << PK_ATTACK;      /* what hides it (an iron bar, a stair) */
            int t = sh_tool_fetch(v, e, r, s, a, s->mo_block);
            if (t >= 0 && t != s->current) a->hotbar = t;
        }
        return 0;
    }
    case SO_FILL:
    {
        if (h->oroom.portal)
        {
            h->osub = SO_ENTER, h->pstart = e->steps;
            return 0;
        }
        if (sh_age(e) > SHO_FILL_MAX) return -1;
        if (h->ouse && e->steps - h->ouse < SHO_USE_WAIT)
        {
            sh_look(a, r->yaw, r->pitch);
            return 0;
        }
        if (h->ouse)
        {
            h->ouse = 0;
            shoracle_read(v->p, id, &h->oroom), h->oread = e->steps;
        }
        /* the open frame nearest the player whose stand cell is known or found */
        if (h->ofr < 0 || h->ofr >= h->oroom.nfr || h->oroom.fr[h->ofr][3])
        {
            h->ofr = -1, h->ostand_ok = 0, h->nop = 0, h->onew = 1;
            double bd = 1e9;
            for (int i = 0; i < h->oroom.nfr; ++i)
            {
                if (h->oroom.fr[i][3] || h->ofr_skip[i] >= SHO_FR_TRIES) continue;
                double d = hypot(h->oroom.fr[i][0] + 0.5 - r->x, h->oroom.fr[i][2] + 0.5 - r->z);
                if (d < bd) bd = d, h->ofr = i;
            }
            if (h->ofr < 0) return -1;
            h->odig_t = e->steps;
        }
        const int32_t *f = h->oroom.fr[h->ofr];
        if (!h->ostand_ok)
        {
            if (!sho_stand(v, e, r, f, h->ostand))
            {
                h->ofr_skip[h->ofr] = SHO_FR_TRIES, h->ofr = -1;
                return 0;
            }
            h->ostand_ok = 1, h->odig_t = e->steps;
        }
        double fd = py_dist3(r->x, r->y + 1.62, r->z, f[0] + 0.5, f[1] + 0.8125, f[2] + 0.5);
        int at = (int)floor(r->x) == h->ostand[0] && (int)floor(r->z) == h->ostand[2] && fy == h->ostand[1];
#ifdef SH_TRACE
        if (e == v->e + SH_TRACE_ENV && e->steps % 20 == 0)
        {
            fprintf(stderr, "ofill t=%lld me=(%.2f %.2f %.2f) fr %d (%d %d %d) stand (%d %d %d) at %d fd %.2f mo %d:%d (%d %d %d) "
                            "eyes %lld open:",
                    (long long)e->steps, r->x, r->y, r->z, h->ofr, f[0], f[1], f[2], h->ostand[0], h->ostand[1],
                    h->ostand[2], at, fd, s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z, (long long)inv_count(s, EYE));
            for (int i = 0; i < h->oroom.nfr; ++i)
                if (!h->oroom.fr[i][3])
                    fprintf(stderr, " (%d %d %d)%d", h->oroom.fr[i][0], h->oroom.fr[i][1], h->oroom.fr[i][2], h->ofr_skip[i]);
            fprintf(stderr, "\n");
        }
#endif
        if (!at && !(fd < SHO_REACH - 0.2 && r->on_ground && r->y + 1.62 > f[1] + 1.2))
        {
            int rc = sho_follow(v, e, r, s, a, h->ostand[0], h->ostand[1], h->ostand[2], 0.45,
                                SHO_EXACT | SHO_SNEAK_F | SHO_BRIDGE);
            if (rc < 0 || e->steps - h->odig_t > SHO_FR_WALK)
                h->ofr_skip[h->ofr] += 1, h->ostand_ok = 0, h->ofr = -1, h->ofail = 0;
            return 0;
        }
        /* in reach: the eyes in the hand, the crosshair on the frame's top, the use pressed */
        int hb = sh_hotbar(s, EYE);
        if (hb < 0) return inv_count(s, EYE) > 0 ? sh_fetch(v, e, r, s, a, EYE) : -1;
        a->hotbar = hb;
        double yaw, pitch;
        look_at(r->x, r->y + 1.54, r->z, f[0] + 0.5, f[1] + 0.8125, f[2] + 0.5, &yaw, &pitch);   /* sneaking */
        sh_look(a, yaw, pitch);
        a->hold = 1u << PK_SNEAK;
        if (s->mo_hit && s->mo_x == f[0] && s->mo_y == f[1] && s->mo_z == f[2] && s->mo_block == END_FRAME &&
            s->current == hb)
        {
            if (s->mo_meta & 4)
            {
                h->oroom.fr[h->ofr][3] = 1;
                return 0;
            }
            sh_look(a, r->yaw, r->pitch);
            a->press[PK_USE] = 1;
            h->ouse = e->steps;
            h->book.filled += 1;
        }
        else if (e->steps - h->odig_t > SHO_FR_WALK)
            h->ofr_skip[h->ofr] += 1, h->ostand_ok = 0, h->ofr = -1;
        return 0;
    }
    case SO_ENTER:
    {
        /* onto the portal: the nearest portal block's middle, forward with a jump where stopped */
        if (sh_age(e) > SH_ENTER_MAX) return -1;
        int bx = h->oroom.pt[0], bz = h->oroom.pt[2];
        double bd = 1e9;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                int x = h->oroom.pt[0] + i - 1, z = h->oroom.pt[2] + j - 1;
                if (shoracle_block(v->p, id, x, h->oroom.pt[1], z, NULL) != 119) continue;
                double d = hypot(x + 0.5 - r->x, z + 0.5 - r->z);
                if (d < bd) bd = d, bx = x, bz = z;
            }
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, bx + 0.5, r->y + 1.0, bz + 0.5, &yaw, &pitch);
        sh_look(a, yaw, 30.0);
        a->hold = 1u << PK_FORWARD;
        if (hypot(r->x - h->lx, r->z - h->lz) < 0.03 && sh_age(e) > 3) a->hold |= 1u << PK_JUMP;
        h->lx = r->x, h->lz = r->z;
        return 0;
    }
    }
    return 0;
}

/* ---------------------------------------------------------------- the drive */

/* ---------------------------------------------------------------- lane/stronghold3: the night, the roof */

/* the night by the sky (the day's time, as pearl_night reads it) */
static int sh_night(const struct vec_last *r)
{
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    return t >= SH_NIGHT0 && t < SH_NIGHT1;
}

/* a roof over the head: the look straight up meets a block within SH_SKY_RAY that is not a tree's (leaves, logs) or a
 * liquid's (stronghold_roof sky's test, at any height) */
static int sh_roofed(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    int bx, by, bz, b, m;
    double ey = r->y + 1.62;
    if (shview_ray(v->p, (int)(e - v->e), r->x, ey, r->z, r->x, ey + SH_SKY_RAY, r->z, &bx, &by, &bz, &b, &m) != 1)
        return 0;
    return !(b == 18 || b == 161 || b == 17 || b == 162 || (b >= 8 && b <= 11));
}

/* the staircase up begun as a climb to the sky (its own end: the look up clear) */
static void sh_sky_begin(struct vec_env *e, const struct vec_last *r)
{
    struct sh_env *h = &e->sh;
    h->asc = 1, h->sky = 1, h->ytop = r->y + 256.0, h->ytop_t = e->steps;
    h->ak = 0, h->alook = 0, h->asc_t = e->steps;
    h->book.skies += 1;
}

static void sh_hide_end(struct vec_env *e, int given_up)
{
    struct sh_env *h = &e->sh;
    h->book.hid += (int32_t)(e->steps - h->hbeg);
    if (given_up) h->hoff = e->steps + SH_HIDE_OFF;
    int prev = h->hprev;
    sh_phase(e, prev);
    h->gx = h->hgx, h->gz = h->hgz, h->gk = h->hgk;
    h->astep = 0, h->stalls = 0, h->tunnel = 0, h->dend = 0;
}

static void sh_hs(struct sh_env *h, int64_t step, int hs)
{
    h->hs = hs;
    h->hstep = step;
}

/* the hide: down SH_HIDE_DEEP in the cell under the feet, a block over the head, held until the morning, the cover
 * mined, a pillar back up to where it began (pearl's PP_HIDE, HS_*: its comments say why each step is as it is) */
static int sh_hide(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a)
{
    struct sh_env *h = &e->sh;
    double ey = r->y + 1.62;
    int fy = (int)floor(r->y + 1e-3), cx = (int)floor(r->x), cz = (int)floor(r->z);
    int age = (int)(e->steps - h->hstep), top = (int)lround(h->hy0) - 1;
    still(a);
    a->look_mode = 1;
    a->look[0] = r->yaw;
    if (s->eye_water && h->hs != SHH_UP)
    {
        /* water came in: up and out where it began */
        sh_hs(h, e->steps, SHH_UP);
        h->hoff = e->steps + SH_HIDE_OFF;
    }
    if (h->hs == SHH_DIG)
    {
        a->look[1] = 90.0f;
        if (r->on_ground && r->y <= h->hy0 - SH_HIDE_DEEP + 0.01)
        {
            sh_hs(h, e->steps, SHH_COVER);
            h->htry = 0;
            return 0;
        }
        int below = s->mo_hit && (s->mo_y == fy - 1 || s->mo_y == fy) && s->mo_x == cx && s->mo_z == cz;
        int bad = below && (s->mo_block == 7 || s->mo_block == 49 || (s->mo_block >= 8 && s->mo_block <= 11) ||
                            s->mo_block == 79);
        int hollow = s->mo_hit && s->mo_y == fy - 2 && s->mo_x == cx && s->mo_z == cz;
        if (bad || age > SH_HIDE_DIG || (r->on_ground && !below && !hollow && age > 10) ||
            (s->lq_hit && (s->lq_block == 10 || s->lq_block == 11)))
        {
            h->hoff = e->steps + SH_HIDE_OFF;
            if (r->y < h->hy0 - 0.5) sh_hs(h, e->steps, SHH_UP);
            else sh_hide_end(e, 1);
            return 0;
        }
        if (below)
        {
            int t = sh_tool(v, s, s->mo_block);
            if (t >= 0 && t != s->current) a->hotbar = t;
            a->hold = 1u << PK_ATTACK;
        }
        double ox = cx + 0.5 - r->x, oz = cz + 0.5 - r->z;
        if ((fabs(ox) > 0.18 || fabs(oz) > 0.18) && age % 2 == 0)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, cx + 0.5, ey, cz + 0.5, &yaw, &pitch);
            a->look[0] = (float)yaw;
            a->hold |= 1u << PK_FORWARD;
        }
        return 0;
    }
    if (h->hs == SHH_COVER)
    {
        int hb = sh_hotbar(s, COBBLE);
        if (hb < 0 && inv_count(s, COBBLE) > 0 && age < SH_HIDE_COVER) return sh_fetch(v, e, r, s, a, COBBLE);
        if (hb >= 0 && hb != s->current) a->hotbar = hb;
        if (age % 8 >= 6)
        {
            a->look[1] = -90.0f;
            if (s->mo_hit && s->mo_y == top && s->mo_x == cx && s->mo_z == cz) sh_hs(h, e->steps, SHH_WAIT);
            else if (age % 8 == 7 && (++h->htry >= 8 || hb < 0)) sh_hs(h, e->steps, SHH_WAIT);   /* held open */
            return 0;
        }
        int q = h->htry % 4;
        double fd = q == 0 ? cz + 1 - r->z : q == 1 ? r->x - cx : q == 2 ? r->z - cz : cx + 1 - r->x;
        a->look[0] = (float)(90.0 * q);
        a->look[1] = (float)(-atan2(top + 0.5 - ey, fd < 0.05 ? 0.05 : fd) * 180.0 / M_PI);
        if (hb >= 0 && s->current == hb && s->mo_hit && s->mo_y == top && abs(s->mo_x - cx) + abs(s->mo_z - cz) == 1 &&
            age % 8 == 3)
            a->press[PK_USE] = 1;
        return 0;
    }
    if (h->hs == SHH_WAIT)
    {
        a->look[1] = -90.0f;
        if (!sh_night(r) || e->steps - h->hbeg > SH_HIDE_WAIT ||
            (MOPT(MO_SH_NIGHT) == SHN_HURT && r->health >= SH_HEALED_HP))
            sh_hs(h, e->steps, SHH_OPEN);
        return 0;
    }
    if (h->hs == SHH_OPEN)
    {
        a->look[1] = -90.0f;
        if (s->mo_hit && s->mo_y <= top + 1 && s->mo_y > fy + 1 && s->mo_x == cx && s->mo_z == cz &&
            age < SH_HIDE_OPEN)
        {
            int t = sh_tool(v, s, s->mo_block);
            if (t >= 0 && t != s->current) a->hotbar = t;
            a->hold = 1u << PK_ATTACK;
            return 0;
        }
        sh_hs(h, e->steps, SHH_UP);
        return 0;
    }
    /* SHH_UP: a pillar (cobblestone under the feet at the jump's top) back to where it began */
    int hb = sh_hotbar(s, COBBLE);
    if ((r->on_ground && !s->eye_water && r->y >= h->hy0 - 0.01) || age > SH_HIDE_UP ||
        (hb < 0 && inv_count(s, COBBLE) == 0))
    {
        sh_hide_end(e, 0);
        return 0;
    }
    if (hb < 0) return sh_fetch(v, e, r, s, a, COBBLE);
    a->hotbar = hb;
    a->look[1] = 90.0f;
    a->hold = 1u << PK_JUMP;
    if (s->current == hb && age % 3 == 2) a->press[PK_USE] = 1;
    return 0;
}

static int sh_drive(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct sh_env *h = &e->sh;
    (void)ey;
    /* the eyes held only to throw one or to fill the frames: otherwise the sword (or a pickaxe), so that a use by
     * the policy or another macro after this one gives back never throws an eye */
    if (h->phase != SH_THROW && h->phase != SH_ROOM && !(h->phase == SH_ORACLE && h->osub == SO_FILL) &&
        s->current >= 0 && s->current < 9 && s->inv[s->current].item == EYE)
    {
        int k = sh_hotbar(s, 267);
        if (k < 0) k = sh_tool(v, s, 1);
        if (k >= 0) a->hotbar = k;
    }
    /* the step given back: the want gone, every SH_YIELD steps, to eat, and to the surface macro short of air */
    if (!sh_want(v, e, r, s) || e->age > SH_YIELD || eat_want(v, r, s) ||
        ((v->macros & NETH_VEC_SURFACE) && s->eye_water && r->air < 120))
        return 1;
    /* underground (the dig, the roof, the room: the silverfish and the spawner's): a hostile in view within reach is
     * the fight lock's (it walks only past 2.6 blocks); this macro starts again after it */
    if ((h->phase >= SH_DIG && h->phase <= SH_LEAVE) || h->phase == SH_HIDE || h->phase == SH_ORACLE)
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (fight_kind(s->vis[i].kind) &&
                py_dist3(r->x, r->y + 1.62, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z) < 3.5)
            {
                h->on = 0;
                e->fid = s->vis[i].id;
                e->flost = 0;
                e->flast = -100;
                e->age = 0;
                return fight(v, e, r, s, a, r->y + 1.62);
            }
    /* lane/shold (stronghold_oracle, PRIVILEGED): a hostile within reach out of view (behind, round a corner) from the
     * client's entity list: the look turned to it, the fight lock's once it is in view */
    if (h->phase == SH_ORACLE)
    {
        float hx[3];
        if (e->steps - h->oturn_last > 1) h->oturn0 = e->steps;      /* a new turn */
        if (e->steps >= h->oturn_off &&
            shoracle_hostile(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, SHO_TURN_D, hx))
        {
            h->oturn_last = e->steps;
            if (e->steps - h->oturn0 > SHO_TURN_MAX) h->oturn_off = e->steps + SHO_TURN_OFF;   /* behind a wall */
            double yaw, pitch;
            still(a);
            look_at(r->x, r->y + 1.62, r->z, hx[0], hx[1], hx[2], &yaw, &pitch);
            sh_look(a, yaw, pitch);
            return 0;
        }
    }
    /* lane/stronghold3 (stronghold_flesh starving): at SH_FLESH_FOOD food or under with no meat held, rotten flesh
     * eaten (the food macro's meats leave it out; golden-g26 holds 5 and starved on the second day under the frozen
     * policy, 6 of 23 deaths at seed 1001): fetched, the use held until the food level rises */
    if (MOPT(MO_SH_FLESH) == SHF_STARVING && (h->fl_t || (r->food <= SH_FLESH_FOOD && meat_count(s) == 0 &&
                                                         inv_count(s, FLESH) > 0 && e->steps >= h->fl_off)))
    {
        if (!h->fl_t) h->fl_t = e->steps, h->fl_food = r->food, h->book.flesh += 1;
        if (r->food > h->fl_food || e->steps - h->fl_t > SH_FLESH_MAX || inv_count(s, FLESH) == 0)
        {
            if (r->food <= h->fl_food) h->fl_off = e->steps + 200;
            h->fl_t = 0;
        }
        else
        {
            int hb = sh_hotbar(s, FLESH);
            if (hb < 0) return sh_fetch(v, e, r, s, a, FLESH);
            still(a);
            a->hotbar = hb;
            if (s->current == hb && r->screen == PS_NONE) a->hold = 1u << PK_USE;
            return 0;
        }
    }
    /* lane/stronghold3 (stronghold_hunt low): on the surface's phases by day, an animal in view with fewer than
     * SH_HUNT_MEAT meats held: the food macro's hunt (it started only between this macro's yields, on a step that
     * is a multiple of 5 with the animal in view then: from golden-g26 6 of 8 deaths at seed 2 starved first) */
    if (MOPT(MO_SH_HUNT) == SHH2_LOW && (h->phase == SH_GO || h->phase == SH_PLAN || h->phase == SH_THROW) &&
        !h->asc && !h->climb && !h->tunnel && r->on_ground && meat_count(s) < SH_HUNT_MEAT && !sh_night(r) &&
        e->steps % 5 == 0)
    {
        int k = hunt_want(v, e, s);
        if (k >= 0 && py_dist3(r->x, r->y, r->z, s->vis[k].x, s->vis[k].y, s->vis[k].z) < SH_HUNT_D)
        {
            h->on = 0;
            h->book.hunts += 1;
            e->fid = s->vis[k].id;
            e->flost = 0;
            e->flast = -100;
            e->hunt = 1;
            e->hmeat = (int)meat_count(s);
            e->hx = s->vis[k].x, e->hy = s->vis[k].y, e->hz = s->vis[k].z;
            e->age = 0;
            return fight(v, e, r, s, a, r->y + 1.62);
        }
    }
    int rc = 0;
#ifdef SH_TRACE
    static __thread int tph = -1;
    if (e == v->e + SH_TRACE_ENV && h->phase != tph)
    {
        fprintf(stderr, "phase t=%lld %d -> %d me=(%.1f %.1f %.1f) goal=(%.1f %.1f) gk=%d lines=%d pair=%d est=%d t=(%.1f %.1f) "
                        "eyes=%lld col=(%d %d) roof=%d\n",
                (long long)e->steps, tph, h->phase, r->x, r->y, r->z, h->gx, h->gz, h->gk, h->nlines, h->pair, h->est,
                h->tx, h->tz, (long long)inv_count(s, EYE), h->cx, h->cz, h->roof);
        tph = h->phase;
    }
#endif
    /* lane/stronghold3: dusk on the surface's phases (stronghold_night hide): the hide, then the phase it took over */
    /* (stronghold_night hurt: only at SH_HURT_HP health or under, held until healed to SH_HEALED_HP or the morning) */
    if ((MOPT(MO_SH_NIGHT) == SHN_HIDE || (MOPT(MO_SH_NIGHT) == SHN_HURT && r->health <= SH_HURT_HP)) &&
        (h->phase == SH_THROW || h->phase == SH_PLAN || h->phase == SH_GO ||
         (h->phase == SH_ORACLE && h->osub == SO_TRAVEL)) &&
        sh_night(r) && e->steps >= h->hoff && r->on_ground && !s->eye_water && !h->climb &&
        !(MOPT(MO_SH_NIGHT_NEAR) == SHNN_ON && h->snapped + h->nlines >= 2 && h->gk != SG_VIA &&
          hypot(h->gx - r->x, h->gz - r->z) < SH_HIDE_NEAR))
    {
        h->hprev = h->phase, h->hgx = h->gx, h->hgz = h->gz, h->hgk = h->gk;
        h->hy0 = r->y, h->hbeg = e->steps;
        h->asc = h->sky = h->tunnel = 0;
        h->book.hides += 1;
        sh_phase(e, SH_HIDE);
        sh_hs(h, e->steps, SHH_DIG);
    }
    switch (h->phase)
    {
    case SH_HIDE:
        rc = sh_hide(v, e, r, s, a);
        break;
    case SH_THROW:
        rc = sh_throw(v, e, r, s, a);
        break;
    case SH_WATCH:
        rc = sh_watch(v, e, r, s, a);
        break;
    case SH_PICK:
    {
        /* the drop: in view, else where the eye was last seen (it drops where it ends) */
        struct shview o;
        sh_scan(v, e, r, SHV_DROP | SHV_EYE, &o);
        if (o.eye_id && o.eye_id == h->eid) h->hx = o.eye_x, h->hy = o.eye_y, h->hz = o.eye_z;
        /* the drop in view, else the flight's own end: its target 12 ahead (where it hovers 8 up and drops; the eye's
         * last place seen is often early in its flight) */
        int since = (int)(e->steps - h->tstep);  /* the eye's life (80 ticks), its fall, then SH_PICK_WAIT */
        /* lane/stronghold3: the drop once seen is remembered (out of view it was the flight's target again, a block
         * or two off: golden-g26 under the frozen policy, a drop seen on leaves 7 up, walked away from, lost) */
        if (o.drop_n) h->pdn = 1, h->pdx = o.drop_x, h->pdy = o.drop_y, h->pdz = o.drop_z;
        double gx = o.drop_n ? o.drop_x : h->pdn ? h->pdx : h->tx, gz = o.drop_n ? o.drop_z : h->pdn ? h->pdz : h->tz;
        double hover = h->oy + 8.0 > h->hy ? h->oy + 8.0 : h->hy;
        if (h->pdn && !o.drop_n) hover = h->pdy;
        int got = inv_count(s, EYE) > h->eyes_after;
        double d;
        /* lane/stronghold3: the remembered drop over the head past the reach (on a canopy): cleared as below */
        if (!h->canopy && !o.drop_n && !got && h->pdn && hypot(h->pdx - r->x, h->pdz - r->z) < 1.5 &&
            h->pdy >= r->y + 1.62 + SH_REACH && r->on_ground)
            h->canopy = e->steps, h->cmode = 0, h->ccy0 = r->y, h->cdone = 0, h->book.canopies += 1;
        /* lane/stronghold2: its column roofed over the head when it ended unseen (a tree's leaves: the drop lies on
         * the canopy): the canopy cleared from below, up a pillar */
        int fine = 0;
        if (!h->canopy && !o.drop_n && !got && !o.eye_id && since > 86 && hypot(gx - r->x, gz - r->z) < 1.5)
        {
            /* onto its column's cell first (its middle, sneaking: no overshoot), then the look up it */
            double cx = floor(gx) + 0.5, cz = floor(gz) + 0.5;
            if ((floor(r->x) != floor(gx) || floor(r->z) != floor(gz) || hypot(cx - r->x, cz - r->z) > 0.25) &&
                since < 130)
            {
                double yaw, pitch;
                still(a);
                look_at(r->x, r->y + 1.62, r->z, cx, r->y + 1.62, cz, &yaw, &pitch);
                sh_look(a, yaw, 0.0);
                a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
                fine = 1;
            }
            else
            {
                int bx, by, bz, b, m;
                if (shview_ray(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->x, hover + 1.0, r->z, &bx, &by, &bz,
                               &b, &m) == 1)
                    h->canopy = e->steps, h->cmode = 0, h->ccy0 = r->y, h->cdone = 0, h->book.canopies += 1;
            }
        }
        int over = o.drop_n && hypot(o.drop_x - r->x, o.drop_z - r->z) < 2.0 && o.drop_y > r->y + 1.3;
        int reach = over && o.drop_y < r->y + 1.62 + SH_REACH;
        if (fine)
            d = hypot(gx - r->x, gz - r->z);
        else if (h->canopy && !got && !h->cdone && !reach && (!o.drop_n || over))
        {
            if (sh_canopy(v, e, r, s, a, (o.drop_n ? o.drop_y : hover) + 1.0) == 0) h->cdone = e->steps;
            d = hypot(gx - r->x, gz - r->z);
        }
        else if (reach)
        {
            /* lying on a tree's leaves over the head: the block under it broken, it falls to the player */
            int bx = (int)floor(o.drop_x), by = (int)floor(o.drop_y - 0.05) - 1 + (o.drop_y - floor(o.drop_y) > 0.2),
                bz = (int)floor(o.drop_z);
            double yaw, pitch;
            still(a);
            look_at(r->x, r->y + 1.62, r->z, bx + 0.5, by + 0.5, bz + 0.5, &yaw, &pitch);
            sh_look(a, yaw, pitch);
            if (s->mo_hit)
            {
                a->hold = 1u << PK_ATTACK;
                int t = sh_tool(v, s, s->mo_block);
                if (t >= 0 && t != s->current) a->hotbar = t;
            }
            d = hypot(gx - r->x, gz - r->z);
        }
        else
            d = sh_walk(v, e, r, s, a, gx, gz);
#ifdef SH_TRACE
        if (e == v->e + SH_TRACE_ENV && e->steps % 10 == 0)
            fprintf(stderr, "pick t=%lld age=%d me=(%.2f %.2f %.2f) goal=(%.2f %.2f) d=%.2f drop=%d(%.2f %.2f %.2f) eye=%d(%.2f %.2f %.2f) eyes=%lld after=%d\n",
                    (long long)e->steps, sh_age(e), r->x, r->y, r->z, gx, gz, d, o.drop_n, o.drop_x, o.drop_y, o.drop_z,
                    o.eye_id, o.eye_x, o.eye_y, o.eye_z, (long long)inv_count(s, EYE), h->eyes_after);
#endif
        /* near its place: the look on the eye while it hovers (8 up until its 80 ticks are out), then down at where
         * it lands */
        if (d < 1.5 && !o.drop_n && !fine && !(h->canopy && !h->cdone))
        {
            double yaw, pitch;
            if (o.eye_id && o.eye_id == h->eid)
                look_at(r->x, r->y + 1.62, r->z, o.eye_x, o.eye_y, o.eye_z, &yaw, &pitch);
            else if (since < 84)
                look_at(r->x, r->y + 1.62, r->z, h->tx, hover, h->tz, &yaw, &pitch);
            else
            {
                /* lane/stronghold2: down round the feet a quarter turn every 3 steps (it drifts a block or so as it
                 * falls, past the pickup's reach) */
                yaw = r->yaw + ((since - 84) % 3 == 0 ? 90.0 : 0.0);
                pitch = 55.0;
            }
            sh_look(a, yaw, pitch);
        }
        if (o.drop_n) h->dseen = 1;
        int waited = h->canopy ? (h->cdone && e->steps - h->cdone > SH_PICK_WAIT) || e->steps - h->canopy > SH_CANOPY_MAX
                               : d < 1.5 && !o.drop_n && !o.eye_id && since > 100 + SH_PICK_WAIT;
        if (got || sh_age(e) > SH_PICK_MAX + (h->canopy ? SH_CANOPY_MAX : 0) || waited)
        {
            if (!got) h->book.lost += 1, h->book.dropseen += h->dseen;
            else h->book.picked += 1;
#ifdef SH_TRACE
            fprintf(stderr, "pickend env=%d t=%lld got=%d dseen=%d age=%d d=%.1f me=(%.1f %.1f %.1f) eye last (%.1f %.1f %.1f) col:",
                    (int)(e - v->e), (long long)e->steps, got, h->dseen, sh_age(e), d, r->x, r->y, r->z, h->hx, h->hy,
                    h->hz);
            {
                int shview_block_trace(struct pipe *p, int id, int x, int y, int z);
                for (int y = (int)floor(r->y) - 1; y <= (int)floor(h->hy) + 1; ++y)
                    fprintf(stderr, " %d", shview_block_trace(v->p, (int)(e - v->e), (int)floor(h->tx), y, (int)floor(h->tz)));
                fprintf(stderr, "\n");
            }
#endif
            sh_phase(e, SH_PLAN);
        }
        break;
    }
    case SH_PLAN:
        sh_plan(v, e, r);
        still(a);
        break;
    case SH_GO:
    {
        if (e->steps % 10 == 0)
        {
            struct shview o;
            sh_scan(v, e, r, SHV_BLOCKS, &o);
        }
        double d = sh_walk(v, e, r, s, a, h->gx, h->gz);
        if (!(h->asc && h->sky) && d < (h->gk == SG_DIG ? 0.6 : SH_ARRIVE))
        {
            if (h->gk == SG_DIG)
            {
                h->sk = -1, h->sfail = 0, h->roof = 0, h->sdir = 1, h->slevel = 0;
                sh_phase(e, SH_DIG);
            }
            else if (h->gk == SG_VIA)
                h->gx = h->vx, h->gz = h->vz, h->gk = SG_THROW;
            else
                sh_phase(e, SH_THROW);
        }
        break;
    }
    case SH_DIG:
        rc = sh_dig(v, e, r, s, a);
        break;
    case SH_ROOF:
        rc = sh_roof(v, e, r, s, a);
        break;
    case SH_ROOM:
        rc = sh_room(v, e, r, s, a);
        break;
    case SH_ORACLE:
        rc = sh_oracle(v, e, r, s, a);
        h->book.osub = h->osub;
        break;
    case SH_ENTER:
        rc = sh_enter(v, e, r, s, a);
        break;
    case SH_LEAVE:
        if (sh_leave(v, e, r, s, a))
        {
            sh_phase(e, SH_DONE);
            return 1;
        }
        return 0;
    default:
        return 1;
    }
    if (rc < 0)
    {
        /* given up: the books say where; the rest of the episode is the other macros' and the policy's (in the room:
         * off the frame row first, away from the lava under the portal) */
        h->book.gave_up = (int32_t)e->steps;
        h->book.gave_y = (int32_t)floor(r->y);
        h->book.phase = h->phase;
        if ((h->phase == SH_ROOM || h->phase == SH_ENTER) && h->nfr > 0)
        {
            sh_phase(e, SH_LEAVE);
            return 0;
        }
        sh_phase(e, SH_DONE);
        return 1;
    }
    return 0;
}

/* macro_start's: the stronghold begun (1) */
static int sh_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (!sh_want(v, e, r, s)) return 0;
    /* lane/stronghold3: short of the pickaxe's uses for the way (the library's eye starts hold a stone pickaxe with
     * 72, 54 and 15 uses left): the step left to the craft macro while it has a stone pickaxe's plan (planks, a table,
     * sticks, the pickaxe), on the surface's phases by day, SH_CRAFTS times at most */
    struct sh_env *h = &e->sh;
    if ((v->macros & NETH_VEC_CRAFT) && h->crafts < SH_CRAFTS && sh_owns(v, e, r) &&
        (h->phase == SH_THROW || h->phase == SH_PLAN || h->phase == SH_GO ||
         (h->phase == SH_ORACLE && h->osub == SO_TRAVEL)) && !sh_night(r))
    {
        if (sh_spick_plan(v, e, r, s) >= 0)
        {
#ifdef SH_TRACE
            if (e == v->e + SH_TRACE_ENV)
                fprintf(stderr, "craft t=%lld plan=%d uses=%d planks=%lld sticks=%lld tables=%lld near=%d cobble=%lld "
                                "kind=%d cplan=%d has_table=%d\n",
                        (long long)e->steps, sh_spick_plan(v, e, r, s), sh_pick_uses(v, s),
                        (long long)inv_count(s, PLANKS), (long long)inv_count(s, STICK),
                        (long long)inv_count(s, TABLE), near_table(e, r), (long long)inv_count(s, COBBLE), e->kind,
                        e->cplan, e->has_table);
#endif
            if (e->steps % 5 == 0) h->crafts += 1, h->book.crafts += 1;
            return 0;
        }
    }
    e->kind = M_MINE;
    e->sh.on = 1;
    e->sh.slook = 0;
    e->sh.astep = 0;
    e->sh.tunnel = 0;
    if (e->sh.phase == SH_DIG) e->sh.sk = -1;
    return 1;
}

/* the books after each step (neth_vec_step_hook) */
static void sh_books(struct vec_env *e, const struct pool_result *pr, const struct neth_rl_state *s)
{
    struct sh_env *h = &e->sh;
    const struct vec_last *r = &e->last;
    if (!h->armed)
    {
        h->armed = 1;
        h->x0 = r->x, h->z0 = r->z;
        h->ymax = r->y;
    }
    if (r->dim == 1 && h->book.end < 0 && h->book.started >= 0)
    {
        h->book.end = (int32_t)e->steps;
        if (h->book.lit < 0 && h->book.room >= 0) h->book.lit = (int32_t)e->steps;   /* lit by then (walked in first) */
        if (h->phase != SH_DONE) sh_phase(e, SH_DONE);
    }
    if (r->on_ground || s->eye_water)
    {
        if (h->ymax - r->y > 3.0) h->fall = h->ymax - r->y, h->fall_step = e->steps;
        h->ymax = r->y;
    }
    else if (r->y > h->ymax)
        h->ymax = r->y;
    if (r->fire) h->lava_step = e->steps;
    /* a death is the macro's while it runs and within SH_BLAME steps of its giving up (it stops where it was: in
     * lava, under water); after the End or later, the policy's */
    int mine = h->go && h->book.end < 0 &&
               (h->phase != SH_DONE || (h->book.gave_up >= 0 && e->steps - h->book.gave_up <= SH_BLAME));
    if ((pr->flags & PF_DIED) && !mine)
        h->book.deaths_after += 1;
    else if (pr->flags & PF_DIED)
    {
        int c = e->steps - h->lava_step <= 40 ? SC_LAVA
                : e->steps - h->fall_step <= 2 && h->fall > 4.0 ? SC_FALL
                : h->prev_air <= 0 && s->eye_water ? SC_DROWN
                : h->prev_food == 0 ? SC_STARVE
                : h->prev_nvis > 0 ? SC_MOB
                                   : SC_OTHER;
        h->book.cause[c] += 1;
#ifdef SH_TRACE
        fprintf(stderr, "died t=%lld at (%.2f %.2f %.2f) cause %d fall %.1f phase %d\n", (long long)e->steps, r->x, r->y,
                r->z, c, h->fall, h->phase);
#endif
        if (h->book.deaths == 0) h->book.death_step = (int32_t)e->steps;
        h->book.deaths += 1;
        /* the eyes and the tools lie where it died: over (a respawn holds nothing to go on with) */
        if (h->go && h->phase != SH_DONE)
        {
            if (h->book.gave_up < 0) h->book.gave_up = (int32_t)e->steps, h->book.gave_y = (int32_t)floor(r->y);
            sh_phase(e, SH_DONE);
        }
    }
    h->prev_air = r->air, h->prev_food = r->food, h->prev_nvis = s->nvis;
    h->book.eyes = (int32_t)inv_count(s, EYE);
    /* out of eyes short of the twelfth frame: over (sh_want's end), booked as given up */
    if (h->go && h->phase != SH_DONE && h->phase != SH_ENTER && h->phase != SH_ROOM && h->phase != SH_LEAVE &&
        h->book.eyes == 0 &&
        r->dim == 0 &&
        !(pr->flags & PF_DIED))
    {
        if (h->book.gave_up < 0) h->book.gave_up = (int32_t)e->steps, h->book.gave_y = (int32_t)floor(r->y);
        h->book.phase = h->phase;
        sh_phase(e, SH_DONE);
    }
    h->book.dist = (float)hypot(r->x - h->x0, r->z - h->z0);
    if (h->phase != SH_DONE) h->book.phase = h->phase;
}

int neth_vec_stronghold_book(const struct neth_vec *v, int id, int last, struct neth_vec_sh_book *out)
{
    if (id < 0 || id >= v->n || out == NULL) return -1;
    *out = last ? v->e[id].sh.last : v->e[id].sh.book;
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static int stronghold_m_cand(struct mac_step *c)
{
    return (c->v->macros & NETH_VEC_STRONGHOLD) && sh_want(c->v, c->e, c->r, c->s);
}

static void stronghold_m_policy(struct mac_step *c, struct neth_rl_act *a) { sh_policy_keys(c->v, c->e, c->r, c->s, a); }

/* lane/proom (stronghold_room_start on): standing in a portal room it sees, latched at once. Each step in the
 * overworld with SH_START_EYES eyes held and no latch yet, the view's blocks are scanned (shview, as the macro's own
 * scans: the screen's cone, never through a wall); an end portal frame within SH_RSTART_D of the eye latches the macro
 * past sh_want's holds (snow_first, stronghold_food, stronghold_armor, sword_stock: each serves the way to the room,
 * which is behind it), and provision_mode stands down while it runs (provision.c). Runs in the observe phase, before
 * provision_mode's early return, so the latch's test is reached on every step */
static void stronghold_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    (void)a;
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    struct sh_env *h = &e->sh;
    if (!MOPT(MO_SH_ROOM_START) || !(v->macros & NETH_VEC_STRONGHOLD) || h->go || h->phase == SH_DONE ||
        r->dim != 0 || r->screen != PS_NONE || (r->flags & PF_DEAD) || inv_count(c->s, EYE) < SH_START_EYES)
        return;
    struct shview o;
    sh_scan(v, e, r, SHV_BLOCKS, &o);
    int near = 0;
    for (int i = 0; i < o.nframe && !near; ++i)
        near = py_dist3(r->x, r->y + 1.62, r->z, o.frame[i].x + 0.5, o.frame[i].y + 0.5, o.frame[i].z + 0.5) <=
               SH_RSTART_D;
    /* with stronghold_oracle=locate (privileged: the room the oracle's own phase walks to), the feet within
     * SH_RSTART_PAD of the located room's box: the library's room starts stand in its wall (s42: inside the box's west
     * wall and ceiling; s2: 0.8 outside the west wall's face) where no frame is in view */
    if (!near && MOPT(MO_SH_ORACLE) == SHOR_LOCATE && e->steps % SH_RSTART_EVERY == 1)
    {
        struct shoracle_room rm;
        if (shoracle_locate(v->p, sho_id(v, e), r->x, r->z, &rm) == 0 && rm.ok)
        {
            const int32_t *b = rm.box;
            near = r->x >= b[0] - SH_RSTART_PAD && r->x < b[3] + 1 + SH_RSTART_PAD && r->z >= b[2] - SH_RSTART_PAD &&
                   r->z < b[5] + 1 + SH_RSTART_PAD && r->y >= b[1] - SH_RSTART_PAD && r->y < b[4] + 1 + SH_RSTART_PAD;
        }
    }
    if (!near) return;
    h->go = 1;
    h->phase = MOPT(MO_SH_ORACLE) == SHOR_LOCATE ? SH_ORACLE : SH_THROW;
    h->pstart = e->steps;
    h->rstart = e->steps + 1;
    if (h->book.started < 0) h->book.started = (int32_t)e->steps;
}

static int stronghold_m_start(struct mac_step *c) { return sh_start(c->v, c->e, c->r, c->s); }   /* before the strip */

static int stronghold_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->sh.on) return -1;
    return sh_drive(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t stronghold_m_label(const struct vec_env *e)
{
    return e->kind == M_MINE && e->sh.on ? NETH_VEC_STRONGHOLD : 0;
}

static void stronghold_m_end(struct vec_env *e) { e->sh.on = 0; }

static void stronghold_m_reset(struct vec_env *e) { sh_reset(&e->sh); }

static void stronghold_m_books(struct mac_books *b)
{
    if (b->at == MB_RESULT0 && (b->v->macros & NETH_VEC_STRONGHOLD) && !b->e->reset_step)
        sh_books(b->e, b->r, b->s);     /* lane/stronghold */
}

static const struct mac_switch STRONGHOLD_SW[] = {{"stronghold", NETH_VEC_STRONGHOLD}, {NULL, 0}};

static const struct vec_macro MAC_STRONGHOLD = {
    .name = "stronghold", .bit = NETH_VEC_STRONGHOLD, .sw = STRONGHOLD_SW, .cand = stronghold_m_cand, .policy = stronghold_m_policy,
    .observe = stronghold_m_observe,
    .start = stronghold_m_start, .drive = stronghold_m_drive, .label = stronghold_m_label, .end = stronghold_m_end,
    .reset = stronghold_m_reset, .books = stronghold_m_books,
};
