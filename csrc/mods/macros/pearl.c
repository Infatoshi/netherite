/* lane/pearl: NETH_VEC_PEARL (vecenv.h), the ender pearls a speedrunner hunts at night. Included by macros.c (its
 * statics: look_at, fight, best_weapon, best_tool, eat_want); the state is pearl.h's struct pearl_env.
 *
 * What it reads is the player's: its own state, the stats screen's kill count and death cause, the sky's time, the
 * crosshair, and the livings in view (neth_rl_state vis within 16 blocks, rlbind.h neth_rl_far's endermen out to 64: never
 * through a wall). Why a pillar: EntityMob.attackEntity hits only with the feet within 2.0 blocks of the target's and
 * the boxes overlapping in height, EntityAICreeperSwell lights the fuse within 3.0; from 3 blocks up neither reaches
 * from the ground, and the player's 3-block reach still meets an enderman's 2.9-high box beside the pillar. Why its
 * top: EntityEnderman.shouldAttackPlayer is the look within acos(1 - 0.025 / d) of the box middle; an angry one so
 * looked at within 4 blocks teleports away (onLivingUpdate), and farther than 16 it stops teleporting to the player */
#include <math.h>

#include "../../engine/survival.h"

/* the constants tuned (lane/pearl's case, DEVLOG) */
#define PEARL_WANT 12           /* pearls and eyes wanted (the stronghold's 12 frames, none spare) */
#define PEARL_NIGHT0 13000      /* the night: the day's time (mobs spawn at light 7 and under) */
#define PEARL_NIGHT1 23000      /* to dawn (an enderman in daylight teleports away) */
#define PEARL_PILLAR 3          /* the pillar's blocks */
#define PEARL_COBBLE 3          /* cobblestone held to build one */
#ifndef PEARL_WALK              /* (a -D on a variant build: rlbind VARIANT=NAME XFLAGS=-DPEARL_WALK=N) */
#define PEARL_WALK 300          /* steps walked before the next pillar's scan */
#endif
#ifndef PEARL_SCAN
#define PEARL_SCAN 360          /* steps on the pillar with nothing in view before walking on */
#endif
#define PEARL_TURN 9.0          /* the scan's yaw a step (degrees: a turn in 40 steps) */
#define PEARL_PITCH 12.0        /* the scan's pitch (a little down: from 3 up, the ground 15 blocks out) */
#define PEARL_SWEEP 35.0        /* the walk's look sweep either side of the heading (degrees) */
#define PEARL_SWEEP_T 80        /* its period (steps) */
#define PEARL_STARE 80          /* steps a target is stared at for its scream before it is given up */
#define PEARL_LOST 100          /* steps a target is out of view before it is let go */
#define PEARL_NEAR 10.0         /* an angry target nearer (horizontally) is watched by its top; farther, below its feet */
#define PEARL_TOP 0.15          /* the top's look: this far under the box's top */
#define PEARL_LOW 2.0           /* the far look: this far under its feet (out of the stare's cone, still in view) */
#define PEARL_HIT 10            /* steps between hits (the target's hurt time) */
#define PEARL_REACH 3.2         /* a hostile's middle within this of the eye is hit from the pillar */
#define PEARL_KILL 6.0          /* a kill counts as the target's when it was last seen this near the eye */
#define PEARL_DROP 70           /* steps at most for one drop (walking there, the pickup) */
#define PEARL_DROP_WAIT 12      /* steps stood on a drop */
#define PEARL_QUIET 100         /* steps with no enderman in view before the pillar is left for the drops */
#define PEARL_DROP_AGE 4800     /* steps a drop waits at most (an item despawns at 6000 ticks) */
#define PEARL_GUARD 10.0        /* on the ground: a hostile this near fought (NETH_VEC_FIGHT's lock) */
#define PEARL_CREEPER 7.0       /* on the ground: a creeper this near fled */
#define PEARL_FLEE 50           /* steps fleeing */
#define PEARL_STUCK 30          /* steps without moving before the walk turns a quarter */
#define PEARL_WALK_PITCH 25.0   /* the walk's pitch: the bucket's 5-block ray meets the ground 3 ahead (water, lava) */
#define PEARL_SWIM_TURN 80      /* steps swimming before another turn */
#define PEARL_UP_OFF 60        /* steps walked on after a pillar's jump came down short twice */
#define PEARL_FAR_WAIT 200      /* steps an angry target farther than 16 blocks is waited for on the pillar */
#define PEARL_UP_NEAR 0.0       /* on the ground, a target farther (horizontally) than this: the pillar first (an enderman
                                 * fought on the ground killed in 8 of 8 arena envs) */
#define PEARL_DOWN_R 32.0       /* no digging down while an enderman is in view this near */
#define PEARL_KILL_STEPS 4     /* a kill this many steps old is the target's when it has left the view */
#define PEARL_LOW_HP 10.0f     /* on the ground with a hostile near and this hurt (or worse): up the pillar, not a fight */
#define PEARL_HEAL_HP 16.0f     /* on the pillar with nothing in view: held there until healed to this */
/* lane/pearl2: the hurt response. The first deaths of lane/pearl's case (28 of 64 envs): 19 on the pillar, most at 1
 * to 3 HP ten steps before, shot by a skeleton out of view every 70 to 170 steps while held there to heal; on the
 * ground a zombie behind the player (out of the 75-degree view) hit it every 10 steps and was never fought */
#define PEARL_LOOK_BACK 16      /* steps of the look back after a hit from out of view: 4 at the hit's side, then
                                 * around at PEARL_LOOK_TURN a step */
#define PEARL_LOOK_TURN 30.0
#define PEARL_PILLAR_HURTS 1    /* hits from out of reach on a pillar before it is left */
#define PEARL_ENDER_ABOUT 200   /* steps since an enderman was in view: the pillar is not left for a hit */
/* lane/pearl2: open ground. lane/pearl's census (pearl_check --census): 0.5 endermen a step within 16 to 64 blocks,
 * 5 to 20% of them under the sky, the line from the eye clear for 2% (the spawn's forest): the walk goes where the
 * frame's depth (rlbind.h neth_rl_far depth) is long, a pillar goes up where it is, and a scan where it is not is cut */
#define PEARL_SECTORS 16        /* the view's memory: yaw sectors of 22.5 degrees */
#define PEARL_REHEAD 60         /* steps between the walk's heading choices */
#define PEARL_OPEN 24.0         /* a long view: the level rays' mean reach (blocks) over the sectors seen */
#define PEARL_OPEN_WALK 600     /* steps walked at most looking for a long view before a pillar anyway */
#define PEARL_SCAN_SHORT 48     /* a scan's steps (one turn and a little) before a short view cuts it */

/* lane/pearl2: the hide. A pillar is no cover from arrows (lane/pearl's case: 6 of 19 first deaths on a pillar were a
 * skeleton's, and the low-health rule sent a hurt player up one); a 1x1 hole 3 deep with a block put over the head
 * (against the inner face of the rim's block) is: nothing reaches into it, and health comes back (foodLevel 18 and
 * over: 1 HP each 80 ticks) */
#define PEARL_HIDE_HP 8.0f      /* on the ground at this health or under, nothing within PEARL_HIDE_NEAR: the hide */
#define PEARL_HIDE_NEAR 4.0     /* a hostile in view this near is fought first */
#define PEARL_HIDE_OUT 18.0f    /* held in the hole until healed to this */
#define PEARL_HIDE_DEEP 3       /* the hole's depth */
#define PEARL_HIDE_MIN_Y 66     /* the feet at this height or over (the hole's cells over the sea's 62: 14 of 77 first
                                 * deaths at 96 envs drowned at y 60, holes dug from 63 beside a frozen river) */
#define PEARL_DOWN_CREEPER 8.0  /* no digging down while a creeper is in view this near (7 of 77 blown up doing it) */
#define PEARL_HIDE_WAIT 2400    /* steps held at most */
#define PEARL_HIDE_DIG 240      /* steps digging at most before it is given up */
#define PEARL_HIDE_OFF 400      /* no hide this many steps after one failed */
#define PEARL_HIDE_QUIET 30     /* steps without a hit before a hide (a hit in the dig's first block gives it up) */
#ifndef PEARL_TALL
#define PEARL_TALL 5            /* the scan's pillar: the census's lines to endermen within 64 from 4 blocks over the eye
                                 * were clear 1.5 to 3.4 times as often as from the eye */
#endif
#define PEARL_LOWER_R 8.0       /* a provoked target this near (horizontally): the tall pillar dug down to PEARL_PILLAR,
                                 * the height a hit reaches its box from */
#define PEARL_LOST_LOOK 20     /* steps a provoked target out of view is looked for at its last place, then around */
#define PEARL_LOST_TURN 15.0    /* the look around's yaw a step */
/* lane/pearl3: zombies from the pillar. The seed sweep's deaths (299 seeds, 16 envs each, 4,277 deaths): zombies 1,117,
 * 769 of them in the ground fight (the guard); a zombie's attack needs the boxes level in height, so from a 3-high
 * pillar nothing it does reaches, and the player's reach meets its head at the foot */
/* (the four settings marked "tune" are neth_vec_pearl_tune's defaults: pearl_check --tune NAME=VALUE) */
#define PEARL_ZOMBIE_PILLAR 1   /* tune zombie_pillar; 0: lane/pearl2's ground fight */
#define PEARL_ZOMBIE_UP 6.0     /* a zombie in view farther than this (and within 16): the pillar (it walks 4 blocks in
                                 * about 17 ticks; two blocks up, about 20, are out of its reach) */
#define PEARL_DROP_CALM 1       /* tune drop_calm: the drops' quiet counts only screaming endermen within 32 */
#define PEARL_HEAD_REACH 2.9   /* its box's top within this of the eye: looked at and hit from the pillar */
#define PEARL_DOWN_ZOMBIE 6.0   /* one this near (horizontally) at night: the pillar not dug down */
/* lane/pearl3: the cave hunt (NETH_VEC_PEARLCAVE) */
#define PEARL_CAVE_Y 40         /* tune cave_y: the staircase's feet height, then the tunnel's */
#define PEARL_CAVE_OPEN 10.0    /* a level ray of the frame's depth this long from inside the tunnel: a cave opened */
#define PEARL_CAVE_BACK 1       /* tune cave_back: cells backed into the tunnel from the cell the opening showed from
                                 * (the player's middle 2.5 from the opening's face: an enderman's feet 2.8 off, out of
                                 * its 2.0) */
#define PEARL_CAVE_HOLD 600     /* tune cave_hold: steps a hold is kept with no enderman in view in the last
                                 * PEARL_CAVE_QUIET */
#define PEARL_CAVE_QUIET 300
#define PEARL_CAVE_MIN 6        /* cells a branch digs before a new opening is held (one held is not held again) */
#define PEARL_CAVE_SWEEP 25.0   /* the hold's look sweep either side of the long view (degrees) */
#define PEARL_CAVE_SWEEP_T 90   /* its period (steps) */
#define PEARL_CAVE_STALL 60     /* steps without a cell's progress: a quarter turn */
#define PEARL_CAVE_TIER 1       /* lane/brutall: the cave hunt only with a pickaxe of this tier or better held (1 wood:
                                 * stone dug). Without one its staircase dug by hand for every step of a 180 s run
                                 * from spawn (pearl_check, 8 envs: 3,598 of 3,600 steps in PP_STAIR), and with the
                                 * night gate brutetab's `all` row got 0 logs in 384 2-hour runs */
#define PEARL_CAVE_LEGS 0.4     /* a provoked one near is looked at this far over its feet (its top is behind the
                                 * tunnel's ceiling from 2.5 in, and the box middle is the stare's) */
#define PEARL_CAVE_SURFACE 56   /* the staircase's bad floors all round over this height: a walk on, then again */
#define PEARL_CAVE_WALK 40      /* its steps */
#define PEARL_CAVE_HAZ 4      /* floors judged bad in a row on the staircase: the tunnel from the height it reached */
/* lane/pearlsee: sight (each an option, vecopts.c; the brief's arms) */
#define PEARL_TURN_EVERY 120    /* pearl_look turn: walk steps between the level full turns */
#define PEARL_TURN_STEPS 40     /* a turn's steps (9 degrees a step, PEARL_TURN) */
#define PEARL_TURN_PITCH 2.0    /* its pitch (a little down: level ground 46 blocks out from the eye on flat land) */
#define PEARL_TOWER (MOPT(MO_PEARL_HEIGHT) ? PEARL_TOWER_HIGH : 10)  /* pearl_tower on: the scan's pillar's blocks
                                                                      * (PEARL_TALL is 5; lane/pearlsee's 3rd pass:
                                                                      * pearl_height 20) */
#define PEARL_TOWER_HIGH 20
/* lane/pearlsee (3rd pass): pearl_oracle on, the tall tower off (the walk to the census's enderman, the short pillar) */
#define PP_TOWER_ON (MOPT(MO_PEARL_TOWER) && !MOPT(MO_PEARL_ORACLE))
#define PEARL_ORACLE_UP_D 24.0  /* pearl_oracle: within this (horizontally), PEARL_ORACLE_UP_DY over the eye and hidden: */
#define PEARL_ORACLE_UP_DY 3.0  /* the pillar up, to 2 over its middle's height (PEARL_ORACLE_UP_MAX at most) */
#define PEARL_ORACLE_UP_MAX 16
#define PEARL_ORACLE_REACH 3.0  /* pearl_oracle: its middle within this of the eye: hit */
#define PEARL_REACH_BLIND 3     /* pearl_reach_aim: in-reach steps with no enderman under the crosshair before the
                                 * oracle's path takes the step (the look set on one step shows on the next) */
#define PEARL_ORACLE_LEVEL 4.0  /* its target: the nearest whose middle is within this of the eye's height first */
#define PEARL_ORACLE_DROP 400   /* steps a drop is walked to on the path at most */
#define PEARL_ORACLE_PILLAR_D 8.0   /* this near (horizontally) on its level: the fight's pillar, then the stare */
#define PEARL_ORACLE_BAD_T 400      /* steps a target with no path is skipped */
#define PEARL_ORACLE_SKY_OFF 100    /* steps walked before the next look up when nothing over the head was dug */
#define PEARL_ORACLE_ITEM_T 600     /* steps a pearl item is walked to at most */
#define PEARL_ORACLE_NODROP 40      /* steps a kill's drop phase waits for a pearl item in the census */
#define PEARL_ORACLE_BAD_R 4.0      /* a census entry this near (each axis) a mark is the marked one */
#define PEARL_ORACLE_BAD_D 40.0     /* marked only this near (walk_to's range 48: farther has no path by range) */
#define PEARL_ORACLE_KILL_T 40  /* a kill this many steps after our last hit is ours */
#define PEARL_ORACLE_SPRINT 24.0    /* sprinted while farther (horizontally) than this */
#define PEARL_ORACLE_DIG 40     /* steps digging ahead once the walk stopped (pp_stuck's PEARL_STUCK) */
#define PEARL_RESTOCK_LOW 24    /* pearl_restock: the hotbar's cobblestone under this, a stack in the pack: swapped in */
#define PEARL_RESTOCK_WAIT 10   /* steps for the inventory screen to open before the restock is given up */
#define PEARL_RESTOCK_OFF 200   /* steps after a restock (or a failed one) before another */
#define PEARL_TOWER_PITCH 8.0   /* the scan's pitch from the tower's top (the ground 80 blocks out at 11 over it) */
#define PEARL_TOWER_SCAN (MOPT(MO_PEARL_HOLD) ? PEARL_TOWER_SCAN_SHORT : 3000)  /* steps on the tower with nothing in view
                                                                             * before it is left (the walk, then another) */
#define PEARL_TOWER_SCAN_SHORT 1000     /* lane/pearlsee (3rd pass): pearl_hold 1000 */
#define PEARL_TOWER_JUMP 4.5    /* off the tower for a hit only from this high over its base or under (a fall from
                                 * PEARL_TOWER costs 7 HP) */
#define PEARL_DUSK_LEAD 3000    /* pearl_dusk on: ticks before PEARL_NIGHT0 the climb out begins */
#define PEARL_SEE_R 32.0        /* pearl_see frame: blocks (an enderman 2.9 high: 64 * 2.9 / (2 tan 35 * d) pixels tall
                                 * at 64x64, 8.3 at 16, 4.1 at 32, 2.1 at 64; 0.6 wide: 1.7, 0.9, 0.4) */
#define PEARL_SEE_HALF 35.0     /* the frame's half view (the 70-degree fov, square) */
#define PEARL_DIP_Y 56.0        /* pearl_dip turn: the feet under PEARL_SURF_Y down to this walked back out of */
#define PEARL_DIP_TURN 60       /* steps between its turns */
#define PEARL_SKY_WALK 30       /* pearl_sky on: steps walked on from a roofed cell before the next look up */
#define PEARL_SKY_DIG 24.0      /* pearl_sky on: a ground roof (not leaves, a log, sand, gravel or a liquid) this near
                                 * over the eye: the tower dug up through it */
#define PEARL_DIG_CAP 1200      /* the dug tower's PP_UP steps at most (PEARL_TOWER's 400 otherwise) */
#define PEARL_FETCH_STEPS 400   /* pearl_fetch stay: steps at most for one drop */
#define PEARL_FETCH_WAIT 60     /* steps stood on it with no pearl picked up (the item's 10-tick pickup delay, its fall) */
#define PEARL_FETCH_STUCK 8     /* steps without moving before the walk strafes */
#define PEARL_FETCH_SIDE 40     /* the strafe's side changes every this many steps */
enum { PFE_CAP, PFE_STAY };     /* pearl_fetch's words */
#define PEARL_HIGH_PITCH 10.0   /* pearl_high on: the look's pitch while it turns and walks (the hill row in the frame) */
#define PEARL_HIGH_MIN 3.0      /* a face seen this high over the feet is higher ground to walk to */
#define PEARL_HIGH_WALK 600     /* steps walked toward it at most a try */
#define PEARL_HIGH_FRESH 120    /* steps a sector's face is kept */
#define PEARL_LURE_HOLD 600     /* pearl_lure on: steps after a provoked one last showed screaming the tower is kept */
#define PEARL_LURE_FAR 800     /* pearl_lure on: steps a provoked one farther than 16 is waited for on the tower */
#define PEARL_LURE_PITCH 50.0 /* pearl_lure on: a lost provoked one looked for round the pillar's foot this far down */
enum { PLK_WALK, PLK_TURN };    /* pearl_look's words */
enum { PDP_HAND, PDP_TURN };    /* pearl_dip's words */
enum { PSE_CONE, PSE_FRAME, PSE_PIXEL };    /* pearl_see's words */
#define PEARL_SEE_ROWS 64.0     /* pearl_see pixel: the policy's frame's rows (configs' 64x64 observation) */
#define PEARL_SEE_PX 2.0        /* the box's height in rows at least (an enderman 2.9 high: 2 rows out to 66 blocks
                                 * along the look, 3 to 44, 4 to 33) */
enum { HS_NONE, HS_DIG, HS_COVER, HS_WAIT, HS_OPEN };

enum { PP_WALK, PP_UP, PP_SCAN, PP_DOWN, PP_DROP, PP_FLEE, PP_GUARD, PP_HIDE, PP_STAIR, PP_TUNNEL, PP_HOLD, PP_TRAVEL,
       PP_NPHASE };
enum { PTG_SKY, PTG_ANY };      /* lane/pearlhome: pearl_target's words */
/* lane/pearlhome (pearl_home, PRIVILEGED): the day's travel leg toward the stronghold and the stand on land for the night */
#define PH_SHORE 4              /* land: the feet on the ground and no water within this many blocks (across), from the
                                 * feet's floor to the feet's cell */
#define PH_NEAR 48.0            /* the leg ends this near the portal room's middle (horizontally): stood on land there */
#define PH_LAND_EVERY 10        /* steps between the land checks (the books' step) */
#define PH_LEG 40               /* the leg's target this far ahead toward the goal (lane/shold's SHO_LEG) */
#define PH_LEG_MOVE 12          /* the leg's target moved on when the new one is this far from it (blocks, across) */
#define PH_STRAIGHT 100         /* steps walked or swum straight at the goal when no way is found, then a new plan */
#define PH_WET_N 60             /* at night the leg to land only after this many steps in a row with wet feet (the
                                 * hunt's walk wades a shore: it flipped hunt and leg every few hundred steps) */
#define PH_DIG_DY 3.0           /* pearl_target any: a target this far under the eye with no walking path is dug to */
#define PH_AIR 150              /* the leg's swim: under this much air (300 full) with the eye under water, up first */
#define PH_DEEP_Y 40.0         /* pearl_target any: at night under ground and under this, never yet under the sky this
                                 * episode (a mine): the leg up first, not a hunt there */
#define PH_DIG_MOVE 3          /* ... its cell kept as the dig's goal until it moves this far (blocks, across or up) */
/* lane/pearlgo: the census's words (vecenv.h neth_vec_pearl why and first; counting only) */
enum { PG_WHY_ON, PG_WHY_DEAD, PG_WHY_NETHER, PG_WHY_END, PG_WHY_SCREEN, PG_WHY_NOBLAZE, PG_WHY_FULL, PG_WHY_DAY,
       PG_WHY_YIELD, PG_WHY_OTHER, PG_WHY_POLICY, PG_WHY_N };
enum { PG_F_GATE, PG_F_NIGHT, PG_F_ON, PG_F_SURFACE, PG_F_SEEN, PG_F_PROVOKED, PG_F_HIT, PG_F_KILLED, PG_F_PEARL,
       PG_F_TWELVE, PG_F_N };
#define PG_SURFACE_Y 60.0       /* the census's surface: the feet at this height or over (the sea is 62) */

static int pc_dir_of(double yaw);

static int64_t pearl_held(const struct neth_rl_state *s) { return inv_count(s, PEARL) + inv_count(s, EYE); }

static int pearl_night(const struct vec_last *r)
{
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    return t >= PEARL_NIGHT0 && t < PEARL_NIGHT1;
}

/* lane/pearlgate: the chain's pearl step (pearl_when, vecenv.c MOPTS; PW_* its words). lane/brutetab and lane/stagelen
 * measured the night alone (PW_NIGHT, lane/pearl's) taking every night of every run: from spawn the deaths a 2-hour
 * run 1.91 -> 6.07 at seed 42, from the library's late starts iron pickaxe -> bucket, bucket -> flint and steel and
 * obsidian -> the Nether all 0. The step is the chain's once the Nether is done: PW_NETHER (the default), a blaze rod
 * or blaze powder held (the inventory screen's: no other macro holds them, and an eye made of them leaves the powder
 * count it used in the eyes); PW_POWDER, the blaze held covering every eye short of the stronghold's 12 (2 powder a
 * rod) */
enum { PW_NIGHT, PW_NETHER, PW_POWDER };
enum { PWK_SWEEP, PWK_SPRINT, PWK_STRAIGHT };   /* lane/pearlgo: pearl_walk's words (vecenv.c MOPTS) */
#define PEARL_YIELD_FIGHT 100   /* pearl_yield: steps after a fight (NETH_VEC_FIGHT's lock) the hunt is not begun */
/* lane/pground (pearl_march, vecopts.c MOPTS) */
#define PEARL_MARCH_NEAR 64.0   /* a census target nearer than this (across) is the oracle walk's, past it the march */
#define PEARL_MARCH_HALF 500.0  /* the line's ends, this far either side of its middle */
#define PEARL_MARCH_END 8.0     /* an end reached within this (across): the other end walked to */
#define PEARL_MARCH_CHECK 600   /* the progress check's period (steps) ... */
#define PEARL_MARCH_GAIN 8.0    /* ... and the blocks nearer the end it wants, else the other end (300 and 12 flipped
                                 * the smoke's s2-eye walks back and forth: path 550 a night, net 8 to 28) */
#define PEARL_MARCH_FED 14      /* fed: the food bar at least this, */
#define PEARL_MARCH_FOOD 12     /* ... and at least these food points held */

static int pearl_gate(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int64_t rods = inv_count(s, BLAZE_ROD), powder = inv_count(s, BLAZE_POWDER);
    switch (MOPT(MO_PEARL_WHEN))
    {
    case PW_NETHER: return rods + powder > 0;
    case PW_POWDER: return 2 * rods + powder + inv_count(s, EYE) >= PEARL_WANT;
    default: return 1;
    }
}

/* lane/brutall: the cave hunt is on (NETH_VEC_PEARLCAVE and a pickaxe of PEARL_CAVE_TIER or better held); without the
 * pickaxe the hunt is the night's surface one, and by day not wanted */
static int pearl_cave(const struct neth_vec *v, const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_PEARLCAVE)) return 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it] >= PEARL_CAVE_TIER) return 1;
    }
    return 0;
}

static int pearl_want(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return (v->macros & NETH_VEC_PEARL) && r->dim == 0 && !(r->flags & PF_DEAD) && r->screen == PS_NONE &&
           (pearl_night(r) || pearl_cave(v, s)) && pearl_held(s) < PEARL_WANT && pearl_gate(v, s);
}

/* lane/pearlgo: the pearl step at any hour (pearl_want without the night): pearl_night_first day's surface */
static int pearl_stage(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return (v->macros & NETH_VEC_PEARL) && r->dim == 0 && !(r->flags & PF_DEAD) && pearl_held(s) < PEARL_WANT &&
           pearl_gate(v, s);
}

/* lane/pearlsee: pearl_dusk on, the day's last PEARL_DUSK_LEAD ticks with the pearl step held: pearl_night_first's
 * surface wanted already (the strip's climb out begun before the night, not in it) */
static int pearl_dusk(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (!MOPT(MO_PEARL_DUSK)) return 0;
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    return t >= PEARL_NIGHT0 - PEARL_DUSK_LEAD && t < PEARL_NIGHT0 && pearl_stage(v, r, s);
}

/* lane/pearlgate (pearl_yield): a step the run is mid-way through keeps the hunt from beginning (macro_start runs only
 * between macros, so these are the steps that span several): a bucket portal's frame being cast (bportal.c's
 * site), the stronghold's search (sh_owns), the strip's climb out of the depth (lane/strip3's latch), a fight ended
 * in the last PEARL_YIELD_FIGHT steps (the hostile may still be about), and the late macro's powder or eye (the craft
 * macro's next plan C_POWDER or C_EYE: the 30-minute frozen eval's ender pearl starts, 2 and 3 pearls with 6 rods,
 * reached the eye 0.833 and 0.917 with the night's hunt first against 1.000 without it). A pillar to come down from or
 * the hide is never held back (pearl_start's other starts) */
/* lane/pearlhome (pearl_home): land under the feet (the server's blocks: PRIVILEGED), on the ground with no water within
 * PH_SHORE blocks across from the feet's floor to the feet's cell; the books' step keeps it (pearl_env ph_land) */
static int ph_land_now(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!r->on_ground || r->dim != 0) return 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fz = (int)floor(r->z), fy = sho_feet_y(r);
    for (int dx = -PH_SHORE; dx <= PH_SHORE; ++dx)
        for (int dz = -PH_SHORE; dz <= PH_SHORE; ++dz)
            for (int y = fy - 1; y <= fy; ++y)
            {
                int b = shoracle_block(v->p, id, fx + dx, y, fz + dz, NULL);
                if (b == 8 || b == 9) return 0;
            }
    return 1;
}

/* the feet in water (the server's block at the feet) */
static int ph_wet(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    return sho_water_at(v, e, (int)floor(r->x), (int)floor(r->y + 0.1), (int)floor(r->z));
}

/* the food macro first by day (eating, a food animal in view to hunt, the scout) */
static int ph_food_first(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s)
{
    return eat_want(v, r, s) || hunt_want(v, e, s) >= 0 || scout_want(v, e, r, s);
}

/* under ground: no sky over the feet (the census's, PRIVILEGED) and under the land's height */
static int ph_under(const struct vec_env *e, const struct vec_last *r)
{
    return e->pearl.far.census.on && !e->pearl.far.census.pl_sky && r->y < PEARL_SURF_Y;
}

/* the travel leg (or the stand) wanted: the pearl step held, by day always (the food macro first), at night with the
 * feet in water or under ground (on land the night is the hunt's) */
static int ph_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s)
{
    if (!MOPT(MO_PEARL_HOME) || !pearl_stage(v, r, s) || r->screen != PS_NONE || (r->flags & PF_DEAD)) return 0;
    /* (pearl_target any: under ground is where its targets are: the dug way down to one handed the night to the leg
     * back up, 53% of steps, out/logs/f1-h4a.log) */
    /* (... but only once it has stood under the sky this episode: from the home node in a mine at y 12, the hunt went
     * for cave endermen there and 30 of 32 envs died, out/logs/w1-h5a.log) */
    /* (and only that deep: from e43f's node 247 at y 57 under a roof the leg held env 5 stood at y 53 the whole night,
     * out/logs/g247.log) */
    if (pearl_night(r))
        return e->pearl.ph_wet_n >= PH_WET_N ||
               ((MOPT(MO_PEARL_TARGET) != PTG_ANY || (!e->pearl.ph_sky_seen && r->y < PH_DEEP_Y)) && ph_under(e, r));
    return !ph_food_first(v, e, r, s);
}

/* lane/pground (pearl_march): the night is the march's: on, with pearl_oracle, at night in the overworld, fed */
static int pm_night(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return MOPT(MO_PEARL_MARCH) && MOPT(MO_PEARL_ORACLE) && r->dim == 0 && !(r->flags & PF_DEAD) && pearl_night(r) &&
           r->food >= PEARL_MARCH_FED && food_points(s) >= PEARL_MARCH_FOOD;
}

static int pearl_busy(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    if (!MOPT(MO_PEARL_YIELD)) return 0;
    /* lane/pground (pearl_march): at night, fed, the hunt gives way only to a recent fight and the strip's climb out */
    if (pm_night(v, r, s))
        return ((v->macros & NETH_VEC_STRIP2) && e->st3_out && !shore_night_yield(v, e, r, s)) ||
               e->te - e->pearl.fight_te < PEARL_YIELD_FIGHT;
    /* lane/chain: armor_kit's pieces first (the armor macro starts last of all, and at the chain's home states the
     * hunt took every step: e43d and e43e, no piece in 8 minutes; the orchestrator: the armor by day before the
     * pearl nights) */
    /* (with pearl_home, on land only: the leg takes the player to land first, armor.c's start waits for it) */
    if (MOPT(MO_ARMOR_KIT) && r->dim == 0 && arm_tier(v, s) >= 2 && (arm_need(v, e, s) > 0 || arm_equip_want(e, s) >= 0) &&
        (!MOPT(MO_PEARL_HOME) || e->pearl.ph_land))
        return 1;
    /* A stone sword and spare iron in normal difficulty's pearl nodes: let the craft plan upgrade it on land. */
    if (MOPT(MO_SWORD_KIT) == 2 && r->dim == 0 && (!MOPT(MO_PEARL_HOME) || e->pearl.ph_land) &&
        craft_plan(v, e, r, s) == C_ISWORD)
        return 1;
    /* The ordinary food_cook plan cannot start while the pearl hunt owns the day. Yield only with a real stock,
     * fuel and a cook plan, so a gathered pack is turned into cooked meals before another night consumes it raw. */
    int finish_cook = food_cooked_meat(s) > 0 && raw_meat(s) >= FOOD_COOK_N &&
                      (inv_count(s, FURNACE) > 0 || near_furnace(e, r));
    if (MOPT(MO_FOOD_COOK) == FK_PEARL && food_pearl_stage(v, r, s) &&
        (!MOPT(MO_PEARL_HOME) || e->pearl.ph_land) &&
        ((food_day(r) && raw_meat(s) >= FOOD_PEARL_COOK_START) || finish_cook) &&
        (inv_count(s, COAL) > 0 || inv_count(s, PLANKS) >= 2))
    {
        int plan = craft_plan(v, e, r, s);
        if (plan == C_SMELT || plan == C_FURNACE) return 1;
    }
    /* Restock in daylight between pearl nights once the first four scouts have been spent. */
    if (MOPT(MO_FOOD_TRIPS) == FT_PEARL && food_pearl_stage(v, r, s) && food_day(r) &&
        (!MOPT(MO_PEARL_HOME) || e->pearl.ph_land) &&
        (r->food < 18 || food_cooked_meat(s) < FOOD_PEARL_COOKED))
    {
        int plan = craft_plan(v, e, r, s);
        if (scout_want(v, e, r, s) || hunt_want(v, e, s) >= 0 ||
            (cook_want(v, s) && (plan == C_FURNACE || plan == C_SMELT)))
            return 1;
    }
    if (e->bp.site || sh_owns(v, e, r) || ((v->macros & NETH_VEC_STRIP2) && e->st3_out &&
                                          !shore_night_yield(v, e, r, s)) ||
        e->te - e->pearl.fight_te < PEARL_YIELD_FIGHT)
        return 1;
    /* lane/peargo (lane/pearlhunt's): pearl_night_first, the strip handed back at depth before its climb latched: the
     * climb first, not a hunt begun at the strip's y (there it walked into the lava lakes) */
    /* (lane/pearlsee: pearl_dip turn, once the hunt has stood on the land this episode only under PEARL_DIP_Y) */
    if (MOPT(MO_PEARL_NIGHT_FIRST) && (v->macros & NETH_VEC_STRIP2) && !MOPT(MO_PEARL_HOME) &&
        r->y < (MOPT(MO_PEARL_DIP) == PDP_TURN && e->pearl.surfaced ? PEARL_DIP_Y : PEARL_SURF_Y))
        return 1;
    if ((v->macros & NETH_VEC_CRAFT) && (v->macros & NETH_VEC_LATE) && !v->picks)
    {
        int plan = craft_plan(v, e, r, s);
        return plan == C_POWDER || plan == C_EYE;
    }
    return 0;
}

/* the ground a pillar stands on (lane/pearlsee: a dug tower's, where the sky first showed over its shaft) */
static double pp_land(const struct pearl_env *p) { return p->base + (p->dig ? p->open_placed : 0); }

/* the height a provoked one is fought from: PEARL_PILLAR over the pillar's ground; lane/pearlsee: with pearl_lure on, also
 * PEARL_PILLAR over the target's own feet as last seen (on hills it stood higher than the tower's ground and hit the
 * player as the lure dug down: the dusk case's env 0, out/logs/t6.log) */
static double pp_floor(const struct neth_vec *v, const struct pearl_env *p)
{
    double f = pp_land(p) + PEARL_PILLAR;
    if (MOPT(MO_PEARL_LURE) && p->tid && p->ty + PEARL_PILLAR > f) f = p->ty + PEARL_PILLAR;
    return f;
}

static int pearl_up(const struct pearl_env *p, const struct vec_last *r)
{
    return !isnan(p->base) && r->y > p->base + 0.5 && !p->climb;
}

/* decode's candidate test: the macro may start (its want, or a pillar to come down from) */
static int pearl_cand(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    return (v->macros & NETH_VEC_PEARL) && (pearl_want(v, r, s) || ph_want(v, e, r, s) ||
                                            ((pearl_up(&e->pearl, r) || e->pearl.hs || e->pearl.climb) &&
                                             !(r->flags & PF_DEAD)));
}

/* lane/macroperf: the step hook's (before pearl_books' copy): the depth rows are read by pearl_drive alone, which the
 * next act runs only with the macro on or pearl_cand true (pearl_start), so they are made only then, from the step's
 * end as before (rlbind.h neth_rl_far_depth_now); a stale row is never read */
static int pearl_depth_due(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s)
{
    return e->pearl.on || pearl_cand(v, e, r, s);
}

/* lane/pearlsee (2nd pass): how a pillar's climb (PP_UP) ended (vecenv.h neth_vec_pearl upx; counting only) */
enum { UX_TOP, UX_SHORT, UX_NOBLOCK, UX_CAP, UX_ROOF, UX_END, UX_DAWN, UX_OTHER, UX_CLIMB, UX_SKY, UX_N };
/* (UX_SKY: not a climb, a tower try refused by pearl_sky's look up) */

static void pp_upbook(struct pearl_env *p, int why)
{
    if (p->climb && why != UX_CLIMB) return;    /* the hide's climb out is not a pillar */
    p->book.upx[why] += 1;
    p->book.uph[p->placed < 0 ? 0 : p->placed > 11 ? 11 : p->placed] += 1;
}

static void pp_set(struct pearl_env *p, int ph)
{
    if (p->phase == PP_UP && ph != PP_UP && !p->upend) pp_upbook(p, p->upwhy ? p->upwhy - 1 : UX_OTHER);
    p->upwhy = p->upend = 0;
    if (ph == PP_UP && p->phase != PP_UP)   /* lane/pearlsee: pearl_sky's dug tower only from its look up */
        p->dig = p->dig_arm, p->dig_open = !p->dig_arm, p->open_placed = 0;
    p->dig_arm = 0;
    p->phase = ph;
    p->pstep = 0;
    p->pwait = 0;
    p->stuck = 0;
    p->flee_turn = 0.0f;
}

static void pearl_reset(struct vec_env *e)
{
    struct pearl_env *p = &e->pearl;
    struct neth_vec_pearl last = p->last;
    struct pearl_census cs_last = p->cs_last;
    memset(p, 0, sizeof *p);
    p->last = last;
    p->cs_last = cs_last;
    p->base = NAN;
    p->fresh = 1;
    p->kb_te = -100;
    p->kill_te = -100;
    p->fight_te = -1000000;     /* lane/pearlgate: no fight yet */
    for (int k = 0; k < NETH_VEC_PEARLS; ++k) p->book.step[k] = -1;
    for (int k = 0; k < PG_F_N; ++k) p->book.first[k] = -1;
    p->book.death_step = -1;
    p->book.home_land = p->book.home_d0 = p->book.home_d1 = -1;    /* lane/pearlhome */
}

/* macro_start's: 1 when it started */
static int pearl_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (!pearl_cand(v, e, r, s)) return 0;
    struct pearl_env *p = &e->pearl;
    /* lane/pearlgate: a fresh hunt gives way to a step mid-way (a pillar, the hide or its climb still come back) */
    if (!pearl_up(p, r) && !p->hs && !p->climb && !p->ndrop && pearl_busy(v, e, r, s)) return 0;
    e->kind = M_MINE;
    p->on = 1;
    int ph0 = p->hs ? PP_HIDE : p->climb ? PP_UP : pearl_up(p, r) ? (pearl_want(v, r, s) ? PP_SCAN : PP_DOWN)
              : p->ndrop ? PP_DROP : PP_WALK;
    /* lane/pearlhome: the travel leg (by day, or at night from water or under ground) */
    if (ph0 == PP_WALK && ph_want(v, e, r, s)) ph0 = PP_TRAVEL;
    pp_set(p, ph0);
    if (!p->heading_set)
    {
        p->heading = r->yaw;
        p->heading_set = 1;
        p->cdir = pc_dir_of(r->yaw);    /* lane/pearl3: the cave hunt's dig: the cardinal faced */
        p->ck = -1;
    }
    p->px = r->x, p->pz = r->z;
    p->hp_last = r->health;
    return 1;
}

static int pp_bad(const struct pearl_env *p, int id)
{
    for (int i = 0; i < p->nbad && i < PEARL_BAD; ++i)
        if (p->bad[i] == id) return 1;
    return 0;
}

/* the hotbar slot holding cobblestone, -1 none */
static int pp_cobble_slot(const struct neth_rl_state *s)
{
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count > 0) return i;
    return -1;
}

/* lane/pearlsee (3rd pass): pearl_restock, the pack's cobblestone into the hotbar (the inventory's own screen, a hotbar
 * swap click: what a player does). pp_restock_due: under PEARL_RESTOCK_LOW in the hotbar, a stack in the pack, on the
 * ground with no screen; pp_restock_src: the pack's largest stack (-1 none) */
static int pp_hotbar_cobble(const struct neth_rl_state *s)
{
    int k = 0;
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count > 0) k += s->inv[i].count;
    return k;
}

static int pp_restock_src(const struct neth_rl_state *s)
{
    int src = -1;
    for (int i = 9; i < 36; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count >= PEARL_COBBLE && (src < 0 || s->inv[i].count > s->inv[src].count))
            src = i;
    return src;
}

static int pp_restock_due(const struct neth_vec *v, const struct pearl_env *p, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    return MOPT(MO_PEARL_RESTOCK) && p->rs_at == 0 && p->clock >= p->rs_off && r->on_ground && r->screen == PS_NONE &&
           !s->eye_water && pp_hotbar_cobble(s) < PEARL_RESTOCK_LOW && pp_restock_src(s) >= 0;
}

static void pp_restock_begin(struct pearl_env *p, struct neth_rl_act *a)
{
    still(a);
    p->rs_at = p->clock;
    a->press[PK_INVENTORY] = 1;
}

/* the restock's step once begun: 1 this step taken (the click and the close, or waiting for the screen) */
static int pp_restock_step(const struct neth_vec *v, struct pearl_env *p, const struct vec_last *r,
                           const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (p->rs_at == 0) return 0;
    if (r->screen == PS_INVENTORY)
    {
        int src = pp_restock_src(s), to = pp_cobble_slot(s), best = 1 << 30;
        if (to < 0)
            for (int i = 0; i < 9; ++i)
            {
                int it = s->inv[i].item, c = s->inv[i].count, itc = it < 0 ? 0 : it > 4095 ? 4095 : it, score;
                if (it < 0 || c <= 0) score = 1;
                else if (v->T.pick[itc] || v->T.axe[itc] || v->T.shovel[itc] || it == 268 || it == 272 || it == 267 ||
                         it == 276 || it == 283 || it == BLAZE_ROD || it == PEARL || it == EYE || it == 326 || it == 259)
                    score = 1000;
                else if (it == 260 || it == 297 || it == 319 || it == 320 || it == 363 || it == 364 || it == 365 ||
                         it == 366 || it == 349 || it == 350 || it == 357 || it == 391 || it == 393 || it == 400 ||
                         it == 282)
                    score = 500;
                else score = 100;
                if (score < best) best = score, to = i;
            }
        still(a);
        a->nops = 0;
        if (src >= 0 && to >= 0) a->ops[a->nops++] = click(inv_slot(src, 0), to, 2), p->book.restocks += 1;
        a->ops[a->nops++] = CLOSE;
        p->rs_at = 0, p->rs_off = p->clock + PEARL_RESTOCK_OFF;
        return 1;
    }
    if (r->screen != PS_NONE || p->clock - p->rs_at > PEARL_RESTOCK_WAIT)
    {
        p->rs_at = 0, p->rs_off = p->clock + PEARL_RESTOCK_OFF;
        return 0;
    }
    still(a);
    return 1;
}

/* lane/pearlsee (3rd pass): pearl_oracle's target, PRIVILEGED (the census: the server's endermen, never a screen's):
 * the nearest under the sky (its feet at or over its column's height map) within NETH_RL_CENSUS_R; 1 found */
/* lane/pearlhome: pearl_target any (any 1), a third pass: the nearest of any of the census's endermen (in a cave, under
 * trees: the server's whole loaded area within NETH_RL_CENSUS_R), walked to on the pathfinder's way */
static int pp_oracle(const struct pearl_env *p, const struct vec_last *r, double *x, double *z, double *d, double *dy,
                     int *clear, int any)
{
    const struct neth_rl_census *c = &p->far.census;
    if (!c->on) return 0;
    /* the nearest near the eye's height first (one 9 to 13 over it on a hill held the walk against the slope), then
     * any */
    /* (any: one pass, the nearest of every one listed: a sky pass first held the walk on one 64+ off while nearer ones
     * stood in caves, out/logs/d1-h2a.log) */
    for (int pass = any ? 2 : 0; pass < (any ? 3 : 2); ++pass)
    for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
        if ((pass == 2 || c->e[i].sky || c->e[i].angry) && (pass || fabsf(c->e[i].dy) < PEARL_ORACLE_LEVEL))
        {
            /* the nearest it has a path to: one marked with no path (walk_to's -1 within range) skipped while marked */
            int bad = 0;
            for (int k = 0; k < 4 && k < p->o_badn; ++k)
                if (p->clock - p->o_bad_t[k] < PEARL_ORACLE_BAD_T &&
                    fabs(r->x + c->e[i].dx - p->o_bad[k][0]) < PEARL_ORACLE_BAD_R &&
                    fabs(r->y + c->e[i].dy - p->o_bad[k][1]) < PEARL_ORACLE_BAD_R &&
                    fabs(r->z + c->e[i].dz - p->o_bad[k][2]) < PEARL_ORACLE_BAD_R)
                    bad = 1;
            if (bad) continue;
            *x = r->x + c->e[i].dx, *z = r->z + c->e[i].dz;
            *d = sqrt((double)c->e[i].dx * c->e[i].dx + (double)c->e[i].dz * c->e[i].dz);
            *dy = c->e[i].dy, *clear = c->e[i].clear;
            return 1;
        }
    return 0;
}

static void pp_look(struct neth_rl_act *a, const struct vec_last *r, double ey, double x, double y, double z)
{
    double yaw, pitch;
    look_at(r->x, ey, r->z, x, y, z, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
}

static void pp_stuck(struct pearl_env *p, const struct vec_last *r, const struct neth_rl_state *s, struct neth_rl_act *a);

/* lane/pearlsee (3rd pass): pearl_oracle's pickup (PRIVILEGED): a pearl item in the census (the server's item entity,
 * within NETH_RL_CENSUS_PEARL_R) walked to on the pathfinder's way onto its own cell, then straight onto it, until it
 * leaves the census (picked up) or PEARL_ORACLE_ITEM_T steps. 1: it drove the step. (dusk-g9: 10 kills, 1 pearl; the
 * drop walk went to the enderman's last place, not the item's) */
static int pp_oracle_item(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, struct pearl_env *p, double ey)
{
    const struct neth_rl_census *c = &p->far.census;
    if (!MOPT(MO_PEARL_ORACLE) || !c->on || c->pearl_n <= 0)
    {
        p->o_item_t = 0;
        return 0;
    }
    if (p->o_item_t == 0) p->o_item_t = p->clock;
    if (p->clock - p->o_item_t > PEARL_ORACLE_ITEM_T)
    {
        p->o_wk = 2, p->o_wk_t = e->te;
        return 0;
    }
    p->book.item_walk += 1;
    int wk = walk_to(v, e, r, s, a, &p->wt, (int)floor(c->pearl_x), (int)floor(c->pearl_y + 1e-3),
                     (int)floor(c->pearl_z));
    p->o_wk = wk, p->o_wk_t = e->te;
    if (wk == 0) return 1;
    double hd = py_hypot2(c->pearl_x - r->x, c->pearl_z - r->z);
    pp_look(a, r, ey, c->pearl_x, c->pearl_y + 0.2, c->pearl_z);
    a->look[1] = a->look[1] > 40.0f ? 40.0f : a->look[1] < -40.0f ? -40.0f : a->look[1];
    a->hold = hd > 0.25 ? 1u << PK_FORWARD : 0;
    if (c->pearl_y > r->y + 0.6 || s->eye_water) a->hold |= 1u << PK_JUMP;
    pp_stuck(p, r, s, a);
    return 1;
}

/* the walk's stuck test: a jump when a step does not move, a quarter turn after PEARL_STUCK */
static void pp_stuck(struct pearl_env *p, const struct vec_last *r, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    double mv = py_hypot2(r->x - p->px, r->z - p->pz);
    p->stuck = mv < 0.05 ? p->stuck + 1 : 0;
    if (p->stuck > 2) a->hold |= 1u << PK_JUMP;
    /* in water: swum at the surface (the jump held; walking on, it sank: 8 of 15 first deaths drowned), turned back
     * once toward land */
    if (s->eye_water) a->hold |= 1u << PK_JUMP;
    if (p->stuck > PEARL_STUCK || (s->eye_water && p->clock >= p->turn_off))
    {
        p->heading += s->eye_water ? 180.0f : 90.0f;
        p->stuck = 0;
        if (s->eye_water) p->turn_off = p->clock + PEARL_SWIM_TURN;
    }
}

/* lane/pdig (P2, pearl_patrol): a block of the patrol's way dug: 1 it drove the step (looked at, the best tool, the
 * attack held once it is under the crosshair), 0 nothing there (open), -1 not dug (bedrock, obsidian, a liquid) */
static int pt_dig(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                  struct neth_rl_act *a, double ey, int x, int y, int z)
{
    int b = shoracle_block(v->p, (int)(e - v->e), x, y, z, NULL);
    if (b < 0 || shoracle_open(b)) return 0;
    if (b == 7 || b == OBSIDIAN || (b >= 8 && b <= 11)) return -1;
    pp_look(a, r, ey, x + 0.5, y + 0.5, z + 0.5);
    a->hold = 0;
    if (s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z)
    {
        a->hold = 1u << PK_ATTACK;
        int t = best_tool(&v->T, s, b);
        if (t >= 0 && t != s->current) a->hotbar = t;
    }
    return 1;
}

/* lane/pdig (P2, pearl_patrol): the straight walk at the census's enderman (ox, oz; od across, ody over the eye): the cell
 * ahead read each step (wk_front's classes) at 0, 30, 60 and 90 degrees off its heading, the first walkable taken:
 * open, an edge, shallow water or a drop of 1 to 3 walked on, a step up jumped (pt1-s2-eye's first form read the
 * heading alone and stood on 84 in 100 of its steps); none walkable: a wall, a low roof or a head-high block ahead dug
 * (the block at the head and the one over it when it stands over the eye: a staircase up; else the head's and the
 * feet's: level), else (a drop over 3, lava, water two deep) a turn of PT_TURN_BACK for PT_TURN_T steps, the side
 * changing each time; a walk that does not move jumps, then turns PT_TURN. Never the pathfinder (its range is 48: past it the straight walk met
 * a wall two high on 61 in 100 of its slow steps on s2-eye's nights) */
#define PT_TURN 70.0
#define PT_TURN_BACK 135.0
#define PT_TURN_T 40
static void pp_patrol(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, struct pearl_env *p, double ey, double ox, double oz, double od, double ody)
{
    int id = (int)(e - v->e);
    p->pt_book[0] += 1;
    double yaw0, pitch;
    look_at(r->x, ey, r->z, ox, ey + ody, oz, &yaw0, &pitch);
    if (p->clock < p->pt_until) yaw0 += p->pt_off;
    a->look_mode = 1;
    a->look[1] = 10.0f;     /* a little down: never at it (a stare provokes it) */
    a->hold = 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    /* the first walkable heading of 0, 30, 60 and 90 degrees off it, either side (the side last turned to first) */
    static const double PT_OFF[7] = {0.0, 30.0, -30.0, 60.0, -60.0, 90.0, -90.0};
    double sg = (p->pt_book[5] & 1) ? 1.0 : -1.0;
    int c0 = -1;
    for (int j = 0; j < 7; ++j)
    {
        double yaw = yaw0 + sg * PT_OFF[j];
        int c = wk_front(v, e, r, yaw);
        if (j == 0) c0 = c;
        int ok = c == 1 || c == 2 || c == 5 || c == 8 || c == 10, why = c == 2 ? 2 : 1;
        if (c == 6 || c == 7)
        {
            double rad = yaw * M_PI / 180.0;
            int ax = (int)floor(r->x - sin(rad) * 0.8), az = (int)floor(r->z + cos(rad) * 0.8);
            if (ax == fx && az == fz) ax = (int)floor(r->x - sin(rad) * 1.3), az = (int)floor(r->z + cos(rad) * 1.3);
            if (c == 6)
                /* the cell ahead open at the feet and the two under: a floor within 4 under the feet's level */
                for (int k = 3; k <= 4 && !ok; ++k)
                {
                    int b = shoracle_block(v->p, id, ax, fy - k, az, NULL);
                    if (b == 10 || b == 11) break;
                    if (shoracle_floor(b)) ok = 1, why = 4;
                    else if (!shoracle_open(b)) break;
                }
            else
            {
                int b0 = shoracle_block(v->p, id, ax, fy, az, NULL), b1 = shoracle_block(v->p, id, ax, fy + 1, az, NULL);
                int g1 = shoracle_block(v->p, id, ax, fy - 1, az, NULL), g2 = shoracle_block(v->p, id, ax, fy - 2, az, NULL);
                int hot = b0 == 10 || b0 == 11 || b1 == 10 || b1 == 11 || g1 == 10 || g1 == 11;
                int deep = (g1 == 8 || g1 == 9) && ((b0 == 8 || b0 == 9) || (g2 == 8 || g2 == 9));
                ok = !hot && (!deep || s->eye_water);
            }
        }
        if (!ok) continue;
        a->look[0] = (float)yaw;
        a->hold = 1u << PK_FORWARD;
        if (c == 2) a->hold |= 1u << PK_JUMP;
        else if (j == 0 && c == 1 && od > PEARL_ORACLE_SPRINT) a->hold |= 1u << PK_SPRINT;
        p->pt_book[why] += 1;
        WK_STAMP(e, WK_OTH + 4, od, ody, e->wk), e->wkf = c;    /* (counting only) */
        if (s->eye_water) a->hold |= 1u << PK_JUMP;
        /* a step that does not move: the jump, then a turn */
        double mv = py_hypot2(r->x - p->px, r->z - p->pz);
        p->stuck = mv < 0.05 ? p->stuck + 1 : 0;
        if (p->stuck > 2) a->hold |= 1u << PK_JUMP;
        if (p->stuck > PEARL_STUCK && p->clock >= p->pt_until)
        {
            p->stuck = 0;
            p->pt_book[5] += 1;
            p->pt_off = (float)((p->pt_book[5] & 1) ? PT_TURN : -PT_TURN);
            p->pt_until = p->clock + PT_TURN_T;
        }
        return;
    }
    /* nothing walkable within a quarter turn: the way ahead dug (a wall, a low roof, a block at the head), else a turn
     * back */
    double yaw = yaw0;
    a->look[0] = (float)yaw;
    int c = c0;
    WK_STAMP(e, WK_OTH + 4, od, ody, e->wk), e->wkf = c;    /* (counting only) */
    double rad = yaw * M_PI / 180.0;
    int ax = (int)floor(r->x - sin(rad) * 0.8), az = (int)floor(r->z + cos(rad) * 0.8);
    if (ax == fx && az == fz) ax = (int)floor(r->x - sin(rad) * 1.3), az = (int)floor(r->z + cos(rad) * 1.3);
    int turn = 0, dug = 0;
    switch (c)
    {
    case 3:
        dug = pt_dig(v, e, r, s, a, ey, fx, fy + 2, fz);
        if (dug == 0) dug = pt_dig(v, e, r, s, a, ey, ax, fy + 2, az);
        break;
    case 4:
        dug = pt_dig(v, e, r, s, a, ey, ax, fy + 1, az);
        if (dug == 0) dug = ody > 1.0 ? pt_dig(v, e, r, s, a, ey, ax, fy + 2, az) : pt_dig(v, e, r, s, a, ey, ax, fy, az);
        break;
    case 9:
        dug = pt_dig(v, e, r, s, a, ey, ax, fy + 1, az);
        break;
    default:    /* a drop over 3, lava, water two deep ahead and either side */
        turn = 1;
        break;
    }
    if (dug > 0)
    {
        p->pt_book[3] += 1;
        p->stuck = 0;
        return;
    }
    if (dug == 0 && !turn)
    {
        /* the class's blocks already open (dug this step): on */
        a->hold = 1u << PK_FORWARD | 1u << PK_JUMP;
        return;
    }
    /* a turn back, held PT_TURN_T steps (the side changing each time) */
    if (p->clock >= p->pt_until)
    {
        p->pt_book[5] += 1;
        p->pt_off = (float)((p->pt_book[5] & 1) ? PT_TURN_BACK : -PT_TURN_BACK);
        p->pt_until = p->clock + PT_TURN_T;
    }
}

/* lane/pground (pearl_march, orders 9:23 PM): the march step. The line: through the feet at the episode's first march
 * step, along the yaw faced then; its ends PEARL_MARCH_HALF either side; walked to the yaw's end first, then end to
 * end (out one way and back over the same ground, near where the hunt began). Under a roof first the sky dig (the
 * oracle walk's: the look up, a roof within PEARL_SKY_DIG dug through by the dug pillar), else pp_patrol's straight
 * walk at the end (the cell ahead read each step). PRIVILEGED as pp_patrol is (the server's blocks) */
static void pp_march(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, struct pearl_env *p, double ey)
{
    /* on a pillar of the hunt's own (its top over the ground it stood on): down first (every way off it is a drop the
     * walk refuses: the smoke's s2-eye nights stood 4,468 steps on one) */
    if (pearl_up(p, r))
    {
        pp_set(p, PP_DOWN);
        return;
    }
    if (!p->pm_set)
        p->pm_set = 1, p->pm_ax = r->x, p->pm_az = r->z, p->pm_h = r->yaw, p->pm_leg = 0, p->pm_d = -1.0f, p->pm_t = p->clock;
    p->pm_book[0] += 1;
    double rad = p->pm_h * M_PI / 180.0, sg = p->pm_leg ? -1.0 : 1.0;
    double tx = p->pm_ax - sin(rad) * PEARL_MARCH_HALF * sg, tz = p->pm_az + cos(rad) * PEARL_MARCH_HALF * sg;
    double d = py_hypot2(tx - r->x, tz - r->z);
    /* the progress check: every PEARL_MARCH_CHECK march steps the end at least PEARL_MARCH_GAIN nearer, else the
     * other end (a cliff, a sea or a wall the walk does not pass: the line swept the other way) */
    int flip = 0;
    if (p->pm_d < 0.0f || p->clock - p->pm_t >= PEARL_MARCH_CHECK)
    {
        if (p->pm_d >= 0.0f && p->pm_d - d < PEARL_MARCH_GAIN) flip = 1, p->pm_book[3] += 1;
        p->pm_d = (float)d, p->pm_t = p->clock;
    }
    if (d < PEARL_MARCH_END || flip)
    {
        if (!flip) p->pm_book[1] += 1;
        p->pm_leg ^= 1, sg = -sg;
        p->pm_d = -1.0f;
        tx = p->pm_ax - sin(rad) * PEARL_MARCH_HALF * sg, tz = p->pm_az + cos(rad) * PEARL_MARCH_HALF * sg;
        d = py_hypot2(tx - r->x, tz - r->z);
    }
    if (!p->far.census.pl_sky && r->on_ground && !s->eye_water && p->clock >= p->o_sky_off && pp_cobble_slot(s) >= 0)
    {
        if (r->pitch > -85.0f)
        {
            a->look_mode = 1;
            a->look[0] = r->yaw;
            a->look[1] = -90.0f;
            a->hold = 0;
            return;
        }
        int lb = p->far.look_block;
        if (p->far.look_depth < PEARL_SKY_DIG && lb != 12 && lb != 13 && !(lb >= 8 && lb <= 11) && lb != 7 &&
            lb != OBSIDIAN)
        {
            p->dig_arm = 1;
            p->o_goal = 1;
            p->pm_book[2] += 1;
            pp_set(p, PP_UP);
            return;
        }
        p->o_sky_off = p->clock + PEARL_ORACLE_SKY_OFF;     /* nothing to dig over it: walked on a while */
    }
    pp_patrol(v, e, r, s, a, p, ey, tx, tz, d, 0.0);
}

/* a hostile (not an enderman) in vis: its index nearest first, -1 none; *d its middle's distance from the eye */
static int pp_hostile(const struct neth_rl_state *s, const struct vec_last *r, double ey, double *d)
{
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (k == HK_ENDERMAN || !fight_kind(k)) continue;
        *d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
        return i;
    }
    return -1;
}

/* lane/pearl2: the view's memory (PEARL_SECTORS by yaw) */
#define PEARL_FRESH 200         /* steps a sector's reach is kept */
static int pp_sector(double yaw)
{
    double y = fmod(yaw, 360.0);
    if (y < 0.0) y += 360.0;
    return (int)(y / (360.0 / PEARL_SECTORS)) % PEARL_SECTORS;
}

static void pp_view(struct pearl_env *p, const struct vec_last *r)
{
    if (!p->far.depth_on) return;
    for (int i = 0; i < NETH_RL_DEPTH; ++i)
    {
        int k = pp_sector(r->yaw + 10.0 * (i - NETH_RL_DEPTH / 2));
        int b = p->far.depth_block[1][i];
        p->open[k] = p->far.depth[0][i];
        p->open_at[k] = p->clock;
        /* water, lava, and ice (a frozen river reads as a long view: walked onto, the player went under it and drowned,
         * 11 to 14 of 192 env-nights against lane/pearl's 5) */
        p->wet[k] = (b >= 8 && b <= 11) || b == 79 || b == 174;
    }
}

/* lane/pearlsee (2nd pass): pearl_high's memory, the hill row's faces (and the level row's: ground at the eye) by
 * sector, only with the row inside the frame (the look within 35 degrees of NETH_RL_HILL_PITCH up) */
static void pp_hill(struct pearl_env *p, const struct vec_last *r)
{
    if (!p->far.depth_on || r->pitch > 35.0f - (float)NETH_RL_HILL_PITCH || r->pitch < -35.0f) return;
    for (int i = 0; i < NETH_RL_DEPTH; ++i)
    {
        int k = pp_sector(r->yaw + 10.0 * (i - NETH_RL_DEPTH / 2));
        float h = p->far.hill_y[i];
        if (h < 0.0f && p->far.depth[0][i] < (float)NETH_RL_DEPTH_R - 0.5f) h = (float)(r->y + 1.62);
        if (h < 0.0f) h = (float)r->y;      /* the sky that way: nothing over the feet */
        p->hill[k] = h;
        p->hill_at[k] = p->clock;
    }
}

static int pp_fresh(const struct pearl_env *p, int k) { return p->open_at[k] > 0 && p->clock - p->open_at[k] <= PEARL_FRESH; }

/* the mean reach of the sectors seen lately (-1: none) */
static double pp_open(const struct pearl_env *p)
{
    double s = 0.0;
    int n = 0;
    for (int k = 0; k < PEARL_SECTORS; ++k)
        if (pp_fresh(p, k)) s += p->open[k], ++n;
    return n ? s / n : -1.0;
}

/* the yaw of the farthest-reaching dry sector seen lately within span degrees of yaw0 (yaw0 when none beats its own
 * sector by 8 blocks) */
static float pp_best(const struct pearl_env *p, float yaw0, double span)
{
    int k0 = pp_sector(yaw0), best = -1;
    double own = pp_fresh(p, k0) && !p->wet[k0] ? p->open[k0] : 0.0, bd = own + 8.0;
    for (int k = 0; k < PEARL_SECTORS; ++k)
    {
        double c = (k + 0.5) * (360.0 / PEARL_SECTORS), dy = fmod(c - yaw0, 360.0);
        if (dy < -180.0) dy += 360.0;
        if (dy > 180.0) dy -= 360.0;
        if (!pp_fresh(p, k) || p->wet[k] || fabs(dy) > span || p->open[k] < bd) continue;
        bd = p->open[k], best = k;
    }
    if (best < 0) return yaw0;
    double c = (best + 0.5) * (360.0 / PEARL_SECTORS), dy = fmod(c - yaw0, 360.0);
    if (dy < -180.0) dy += 360.0;
    if (dy > 180.0) dy -= 360.0;
    return yaw0 + (float)dy;
}

/* lane/pearl3: the cave hunt (NETH_VEC_PEARLCAVE). The cardinals as the strip mine's: 0 +z (yaw 0), 1 -x (90), 2 -z
 * (180), 3 +x (270) */
static const int PC_DX[4] = {0, -1, 0, 1}, PC_DZ[4] = {1, 0, -1, 0};

static int pc_dir_of(double yaw)
{
    double y = fmod(yaw + 45.0, 360.0);
    if (y < 0.0) y += 360.0;
    return (int)(y / 90.0) & 3;
}

void pearl_tune_default(struct pearl_tune *t)
{
    t->zombie_pillar = PEARL_ZOMBIE_PILLAR;
    t->drop_calm = PEARL_DROP_CALM;
    t->cave_y = PEARL_CAVE_Y;
    t->cave_back = PEARL_CAVE_BACK;
    t->cave_hold = PEARL_CAVE_HOLD;
}

int neth_vec_pearl_tune(struct neth_vec *v, const char *name, int value)
{
    struct pearl_tune *t = &v->pt;
    if (!strcmp(name, "zombie_pillar")) t->zombie_pillar = value;
    else if (!strcmp(name, "drop_calm")) t->drop_calm = value;
    else if (!strcmp(name, "cave_y")) t->cave_y = value;
    else if (!strcmp(name, "cave_back")) t->cave_back = value;
    else if (!strcmp(name, "cave_hold")) t->cave_hold = value;
    else return -1;
    return 0;
}

static int pc_floor(const struct neth_vec *v, const struct pearl_env *p)
{
    return p->cfloor_set ? p->cfloor : v->pt.cave_y;
}

/* where the cave hunt goes on from: the hold, the staircase above the tunnel's height, else the tunnel */
static int pc_home(const struct neth_vec *v, const struct pearl_env *p, const struct vec_last *r)
{
    return p->hold_set ? PP_HOLD : r->y > pc_floor(v, p) + 0.5 ? PP_STAIR : PP_TUNNEL;
}

/* the dig turned a quarter (a liquid ahead, a stall, a bad floor, bedrock): the next cell set picked again */
static void pc_turn(struct pearl_env *p)
{
    p->cdir = (p->cdir + 1) & 3;
    p->ck = -1;
    p->cgain = p->clock;
    p->tunnel_cells = 0;
}

/* the cell set ahead along cdir cleared (down: three, a stair step; else two, a 1x2 tunnel), each judged by the
 * crosshair (on it: dug with the best tool; through it: clear), the floor judged (the crosshair on it, no liquid in
 * the column the ray crosses), then the walk in. 0 working, 1 a cell walked into, 2 turned (a hazard), 3 a cell of
 * the tunnel found clear without digging (air: an opening) */
static int pc_dig(const struct neth_vec *v, struct pearl_env *p, const struct vec_last *r, const struct neth_rl_state *s,
                  struct neth_rl_act *a, double ey, int down)
{
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    int dx = PC_DX[p->cdir], dz = PC_DZ[p->cdir];
    double yaw, pitch;
    a->look_mode = 1;
    int liquid = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 &&
                 py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) < 3.0;
    if (liquid || p->clock - p->cgain > PEARL_CAVE_STALL)
    {
        pc_turn(p);
        return 2;
    }
    if (p->ck < 0 || ((p->ck < 3 || p->ck == 5) && (fx != p->ccx - dx || fz != p->ccz - dz || fy != p->ccy)))
    {
        p->ccx = fx + dx, p->ccz = fz + dz, p->ccy = fy;
        p->ck = 0;
        p->cdug = 0;
        p->clook = p->clock;
    }
    int ncell = down ? 3 : 2;
    if (p->ck < ncell)
    {
        int cy = p->ck == 0 ? p->ccy + 1 : p->ck == 1 ? p->ccy : p->ccy - 1;
        look_at(r->x, ey, r->z, p->ccx + 0.5, cy + 0.5, p->ccz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (p->clock - p->clook < 2) return 0;      /* the look settles before the crosshair is judged */
        if (s->mo_hit && s->mo_x == p->ccx && s->mo_y == cy && s->mo_z == p->ccz)
        {
            int b = s->mo_block;
            if (b == 7 || b == OBSIDIAN)
            {
                pc_turn(p);
                return 2;
            }
            a->hold = 1u << PK_ATTACK;
            int best = best_tool(&v->T, s, b);
            if (best >= 0 && best != s->current) a->hotbar = best;
            p->cdug = 1;
            return 0;
        }
        /* the crosshair passes the cell: clear; never dug, it was air (in the tunnel: a cave it broke into) */
        int air = !p->cdug;
        p->cdug = 0;
        p->ck += 1;
        p->clook = p->clock;
        p->cgain = p->clock;
        if (p->ck == ncell) p->ck = 5;
        return air && !down ? 3 : 0;
    }
    if (p->ck == 5)
    {
        int fl = down ? p->ccy - 2 : p->ccy - 1;
        look_at(r->x, ey, r->z, p->ccx + 0.5, fl + 0.5, p->ccz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (p->clock - p->clook < 2) return 0;
        int solid = s->mo_hit && s->mo_x == p->ccx && s->mo_y == fl && s->mo_z == p->ccz;
        int wet = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && s->lq_x == p->ccx && s->lq_z == p->ccz &&
                  s->lq_y >= fl && s->lq_y <= p->ccy + 1;
        if (solid && !wet)
        {
            p->ck = 3;
            p->clook = p->clock;
            return 0;
        }
        p->chaz += 1;
        pc_turn(p);
        return 2;
    }
    if (fx == p->ccx && fz == p->ccz)
    {
        /* in: the look level along the dig (the next step's frame shows the way on: the opening's test) */
        p->ck = -1;
        p->cgain = p->clock;
        p->chaz = 0;
        p->tunnel_cells += 1;
        p->cs.cells += 1;
        a->look[0] = (float)(90.0 * p->cdir), a->look[1] = 0.0f;
        return 1;
    }
    look_at(r->x, ey, r->z, p->ccx + 0.5, ey - 0.3, p->ccz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
    a->hold = 1u << PK_FORWARD;
    if (p->clock - p->clook > 40)
    {
        p->ck = 0;                                  /* something came back into the way: judged again */
        p->clook = p->clock;
    }
    return 0;
}

/* the frame's longest level ray (rlbind.h neth_rl_far depth) and its yaw, when the frame shows the level (the look within
 * 30 degrees of it: the 64x64 frame's view is 70 degrees) */
static double pc_open(const struct pearl_env *p, const struct vec_last *r, float *yaw)
{
    if (!p->far.depth_on || fabs(r->pitch) > 30.0f) return 0.0;
    double best = 0.0;
    int bi = NETH_RL_DEPTH / 2;
    for (int i = 0; i < NETH_RL_DEPTH; ++i)
        if (p->far.depth[0][i] > best) best = p->far.depth[0][i], bi = i;
    *yaw = r->yaw + 10.0f * (float)(bi - NETH_RL_DEPTH / 2);
    return best;
}

/* a hold begun: the cell cave_back (PEARL_CAVE_BACK) back along the dig from the cell the set was chosen from, looked
 * out of along the long view */
static void pc_hold_begin(const struct neth_vec *v, struct pearl_env *p, const struct vec_last *r, float yaw)
{
    int dx = PC_DX[p->cdir], dz = PC_DZ[p->cdir], back = v->pt.cave_back;
    int bx = p->ck >= 0 ? p->ccx - dx : (int)floor(r->x), bz = p->ck >= 0 ? p->ccz - dz : (int)floor(r->z);
    p->hold_set = 1;
    p->hx = bx - back * dx, p->hz = bz - back * dz, p->hy = (int)floor(r->y + 0.01);
    p->hyaw = yaw;
    p->hold_te = p->clock;
    p->cave_ender_te = -1;
    p->cs.holds += 1;
    pp_set(p, PP_HOLD);
}

/* the hold given up: a branch a quarter turn on from where the player stands, PEARL_CAVE_MIN cells before the next */
static void pc_hold_end(struct pearl_env *p)
{
    p->hold_set = 0;
    pc_turn(p);
    p->tunnel_cells = 2 - PEARL_CAVE_MIN;
    pp_set(p, PP_TUNNEL);
}

/* lane/pearlhome (pearl_home, PRIVILEGED): the travel leg's step. The goal: the stronghold's portal room (located once:
 * shoracle_locate), or with none found, or late in the day off land, or near the room off land, the census's high
 * ground (dry, treeless, within 48). By day before PEARL_DUSK_LEAD ticks to night and farther than PH_NEAR from the room
 * the leg goes on; else on land the player stands (the night's hunt starts there), off land the leg goes on to land.
 * The way: lane/shold's sho_follow over the server's cells (walk, swim and dug moves), a target PH_LEG ahead at the
 * feet's height (under ground: up to the land's height); no way, PH_STRAIGHT steps straight at it. 0 (it drove) */
static int ph_travel(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a)
{
    struct pearl_env *p = &e->pearl;
    int id = (int)(e - v->e);
    p->book.home_steps += 1;
    if (p->ph_ok == 0)
    {
        struct shoracle_room rm;
        if (shoracle_locate(v->p, id, r->x, r->z, &rm) == 0 && rm.ok)
        {
            p->ph_ok = 1;
            p->ph_x = 0.5 * (rm.box[0] + rm.box[3]) + 0.5, p->ph_z = 0.5 * (rm.box[2] + rm.box[5]) + 0.5;
        }
        else
            p->ph_ok = -1;
    }
    double d = p->ph_ok == 1 ? hypot(p->ph_x - r->x, p->ph_z - r->z) : -1.0;
    if (p->ph_ok == 1)
    {
        if (p->book.home_d0 < 0) p->book.home_d0 = (int32_t)d;
        p->book.home_d1 = (int32_t)d;
    }
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    int late = t >= PEARL_NIGHT0 - PEARL_DUSK_LEAD;
    int stop = late || p->ph_ok != 1 || d < PH_NEAR;
    still(a);
    if (stop && p->ph_land && !ph_under(e, r))
    {
        /* stood on land: the look level, round slowly (what a player waiting for the night does) */
        p->book.home_stand += 1;
        a->look_mode = 1;
        a->look[0] = r->yaw + 2.0f;
        a->look[1] = 0.0f;
        p->ph_leg = 0;
        return 0;
    }
    double gx, gz;
    const struct neth_rl_census *c = &p->far.census;
    if (stop && c->on && c->hi_d > 0) gx = c->hi_x, gz = c->hi_z;     /* land: the nearest high ground */
    else if (p->ph_ok == 1) gx = p->ph_x, gz = p->ph_z;
    else if (c->on && c->hi_d > 0) gx = c->hi_x, gz = c->hi_z;
    else
    {
        /* nothing to make for: up out of the water, the look level */
        a->hold = s->eye_water ? 1u << PK_JUMP : 0;
        return 0;
    }
    double dh = hypot(gx - r->x, gz - r->z);
    int fy = sho_feet_y(r);
    /* air low with the eye under water (sho_follow's swim cells allow the head under; the chain's macros have no
     * surface macro): up first, the jump held and the look up, until the air is back */
    if (s->eye_water && r->air < PH_AIR)
    {
        p->book.home_air += 1;
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = -60.0f;
        a->hold = (1u << PK_JUMP) | (1u << PK_FORWARD);
        return 0;
    }
    if (p->ph_straight > 0)
    {
        p->ph_straight -= 1;
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, gx, r->y + 1.62, gz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = 1u << PK_FORWARD;
        if (s->eye_water || ph_wet(v, e, r)) a->hold |= 1u << PK_JUMP;
        else if (r->food > 6) a->hold |= 1u << PK_SPRINT;
        pp_stuck(p, r, s, a);
        if (p->ph_straight == 0) e->sh.nop = 0, e->sh.onew = 1, e->sh.ofail = 0, p->ph_leg = 0;
        return 0;
    }
    double ux = dh > 0.01 ? (gx - r->x) / dh : 0.0, uz = dh > 0.01 ? (gz - r->z) / dh : 0.0;
    double leg = dh < PH_LEG ? dh : PH_LEG;
    int lx = (int)floor(r->x + ux * leg), lz = (int)floor(r->z + uz * leg);
    int under = ph_under(e, r), ly = under ? (fy + PH_LEG < (int)PEARL_SURF_Y + 2 ? fy + PH_LEG : (int)PEARL_SURF_Y + 2)
                                           : fy;
    /* under ground with the census's high ground within reach (dry, treeless, a standing cell): its own cell is the
     * leg's goal (a point at the land's height ahead held env 5 of g247 standing, no way to it) */
    if (under && c->on && c->hi_d > 0)
        lx = (int)floor(c->hi_x), ly = (int)floor(c->hi_y + 1e-3), lz = (int)floor(c->hi_z);
    if (!p->ph_leg || abs(lx - p->ph_lx) + abs(lz - p->ph_lz) > PH_LEG_MOVE)
        p->ph_leg = 1, p->ph_lx = lx, p->ph_ly = ly, p->ph_lz = lz;
    int rc = sho_follow(v, e, r, s, a, p->ph_lx, p->ph_ly, p->ph_lz, 3.0, under ? 0 : SHO_ANYY);
    if (rc != 0) p->ph_leg = 0;
    if (rc < 0)
    {
        p->book.home_noway += 1;
        p->ph_straight = PH_STRAIGHT;
    }
    return 0;
}

static int pearl_drive(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct pearl_env *p = &e->pearl;
    /* the policy's camera steers the walk (its yaw change, decode's deltas): the search's direction is the policy's */
    float steer = a->look_mode == 2 ? a->look[0] : 0.0f;
    still(a);
    p->clock += 1;
    p->pstep += 1;
    p->book.phase_steps[p->phase] += 1;
    pp_view(p, r);
    if (MOPT(MO_PEARL_HIGH)) pp_hill(p, r);
    int want = pearl_want(v, r, s), up = pearl_up(p, r);
    int cave = pearl_cave(v, s);
    /* lane/brutall: the pickaxe gone mid-hunt (broken, dropped): the night's surface hunt from here */
    if (!cave && (p->phase == PP_STAIR || p->phase == PP_TUNNEL || p->phase == PP_HOLD)) pp_set(p, PP_WALK);
    if (r->flags & PF_DEAD) return 1;
    if (pp_restock_step(v, p, r, s, a)) return 0;   /* lane/pearlsee (3rd pass): pearl_restock's screen */
    /* drowning is the surface macro's (macro_start's first) */
    if ((v->macros & NETH_VEC_SURFACE) && s->eye_water && r->air < 120) return 1;
    /* lane/pearlsee (3rd pass): pearl_oracle, a pearl item about: walked onto from any phase but a pillar being built
     * (dusk-g9: items lay 2,087 and 7,159 steps in 2 and 3 envs while the scan held the pillar, 136 steps walked) */
    if (MOPT(MO_PEARL_ORACLE) && p->phase != PP_UP && pp_oracle_item(v, e, r, s, a, p, ey))
    {
        if (p->phase != PP_WALK && p->phase != PP_DROP)
        {
            p->climb = 0, p->base = NAN, p->placed = 0;
            pp_set(p, PP_WALK);
        }
        return 0;
    }
    /* lane/chain2 (pearl_armor): on land while armor_kit still wants a piece, the leg or the walk gives the step back
     * (pearl_start's pearl_busy then holds a fresh hunt back, and armor_kit, last of all, begins) */
    if (MOPT(MO_PEARL_ARMOR) && MOPT(MO_PEARL_HOME) && MOPT(MO_ARMOR_KIT) && r->dim == 0 && e->pearl.ph_land && !up &&
        !p->climb && !p->hs && !p->ndrop && (p->phase == PP_TRAVEL || p->phase == PP_WALK) && arm_tier(v, s) >= 2 &&
        (arm_need(v, e, s) > 0 || arm_equip_want(e, s) >= 0))
        return 1;
    /* lane/pearlhome (pearl_home): the travel leg while wanted; the night's hunt on land from it */
    if (MOPT(MO_PEARL_HOME) && !up && !p->climb && !p->hs)
    {
        int pw = ph_want(v, e, r, s);
        if (p->phase == PP_TRAVEL && !pw)
        {
            if (!want) return 1;
            p->ph_leg = 0;
            pp_set(p, PP_WALK);
        }
        else if (p->phase == PP_WALK && pw && !p->ndrop)
            pp_set(p, PP_TRAVEL);
        if (p->phase == PP_TRAVEL)
        {
            if (eat_want(v, r, s)) return 1;
            return ph_travel(v, e, r, s, a);
        }
    }
    /* lane/pearlgo: pearl_night_first, the walk gone down under the land's height (a cave mouth, a ravine): the strip's
     * climb out (strip2.c st3_want) first */
    /* lane/pearlsee: pearl_dip turn, a dip under it no deeper than PEARL_DIP_Y (a river's bank, a hollow) is walked back
     * out of (the heading turned round): the climb it was handed latched and followed for 5,900 of the night's ~10,000
     * steps a run after the hunt began (out/ev/base: the strip's steps while the hunt was wanted, 61 runs) */
    if (r->y >= PEARL_SURF_Y && !up) p->surfaced = 1;
    int dip = MOPT(MO_PEARL_DIP) == PDP_TURN && r->y >= PEARL_DIP_Y;
    if (dip && p->phase == PP_WALK && !up && !p->climb && !s->eye_water && r->y < PEARL_SURF_Y && p->clock >= p->dip_off)
    {
        p->heading += 180.0f;
        p->dip_off = p->clock + PEARL_DIP_TURN;
        p->book.dips += 1;
    }
    if (MOPT(MO_PEARL_NIGHT_FIRST) && (v->macros & NETH_VEC_STRIP2) && !MOPT(MO_PEARL_HOME) && p->phase == PP_WALK &&
        !up && !p->climb && !s->eye_water && r->y < PEARL_SURF_Y && !p->ndrop && !dip)
        return 1;
    if (!want && !up && p->phase != PP_DROP && p->phase != PP_GUARD && p->phase != PP_HIDE && !p->climb) return 1;
    if (!want && up && p->phase != PP_DOWN)
    {
        p->upwhy = UX_DAWN + 1;
        pp_set(p, PP_DOWN);
    }
    /* eating is the food macro's (it stands still: on the pillar nothing reaches; in the hide's hole, held there) */
    if (eat_want(v, r, s) && p->phase != PP_UP && p->phase != PP_DOWN && p->phase != PP_GUARD &&
        !(p->phase == PP_HIDE && p->hs != HS_WAIT))
        return 1;
    /* the kill: the stats screen's count up with the target gone from near the eye */
    /* the count moves on the step the target still shows (dying a step later): a kill a few steps old counts */
    int killed = e->te - p->kill_te <= PEARL_KILL_STEPS;
    p->mo = s->mo_entity;
    int k = -1;
    for (int i = 0; i < p->far.n && i < NETH_RL_FAR; ++i)
        if (p->tid && p->far.e[i].id == p->tid) k = i;
    if (killed && p->tid && k < 0 && p->tdist < PEARL_KILL * 2.0)
    {
        p->kill_te = -100;
        /* (book.killed counts the stats screen's kills in pearl_books, lane/pnight: this test missed over half) */
        p->tkill += 1;
        /* its drop, where it died: picked up from the ground once no enderman has shown for PEARL_QUIET steps (an
         * angry one met the player off the pillar at the drop in 6 of 7 deaths in the arena) */
        if (p->ndrop == PEARL_DROPS)    /* full: the oldest let go */
        {
            memmove(&p->drop[0], &p->drop[1], (PEARL_DROPS - 1) * sizeof p->drop[0]);
            p->ndrop -= 1;
        }
        int j = p->ndrop++;
        p->cs.drops += 1;
        p->book.drops += 1;
        p->drop[j].x = p->tx, p->drop[j].y = p->ty, p->drop[j].z = p->tz, p->drop[j].te = e->te;
        p->tid = 0;
        p->tangry = 0;              /* lane/pearl2: the hurt response's (a provoked one not killed keeps the pillar) */
        if (!up && !cave) pp_set(p, PP_DROP);   /* lane/pearl3: the hold fetches it once quiet */
    }
    if (p->far.n > 0) p->ender_te = e->te;
    /* lane/pearl3: the last step a provoked one (screaming) was in view within PEARL_DOWN_R: the drops' quiet with
     * drop_calm on (a calm one does not come for the player; on open ground one is nearly always in view: seed 191's
     * sweep row booked 27 kills and picked up 5 pearls; measured after: 1 to 2 pearls more there, none on seed 42) */
    for (int i = 0; i < p->far.n && i < NETH_RL_FAR; ++i)
    {
        if (p->far.e[i].scream && p->far.d2[i] < (float)(PEARL_DOWN_R * PEARL_DOWN_R)) p->angry_te = e->te;
        if (p->far.e[i].scream) p->prov_te = e->te;     /* lane/pearlsee: any distance (pearl_lure's hold) */
    }
    int64_t drop_te = v->pt.drop_calm ? p->angry_te : p->ender_te;
    /* lane/pearl2: a hit from out of view (health down, no hostile in view within the guard's reach, no enderman
     * target): turned to where it came from (the knockback's reverse: the step's move against the step before's), then
     * around; on a pillar the pillar is left (walked off its side, away: 3 blocks fall without damage) */
    {
        int hurt = r->health < p->hp_last - 0.25f;
        p->hp_last = r->health;
        if (hurt) p->hit_clock = p->clock;
        /* hit early in the hide's dig (a knockback in the air digs at a fifth: the hole is never made): the hide given
         * up for the look back and the guard */
        if (hurt && p->phase == PP_HIDE && p->hs == HS_DIG && r->y > p->hy0 - 1.5)
        {
            p->hs = HS_NONE, p->hide_off = p->clock + PEARL_HIDE_QUIET;
            pp_set(p, PP_WALK);
        }
        double hd_;
        int hh = pp_hostile(s, r, ey, &hd_);
        if (hurt && !(hh >= 0 && hd_ < PEARL_GUARD) && p->tid == 0 && p->phase != PP_GUARD)
        {
            double mx = r->x - p->px, mz = r->z - p->pz;
            double yaw, pitch;
            if (py_hypot2(mx, mz) > 0.03) look_at(r->x, ey, r->z, r->x - mx, ey, r->z - mz, &yaw, &pitch);
            else yaw = r->yaw + 180.0;
            p->hurt_yaw = (float)yaw;
            p->hurt_clock = p->clock;
            p->look_back = PEARL_LOOK_BACK;
            /* (off a pillar taller than PEARL_PILLAR the fall costs a heart a block over 3: not at 4 HP or under) */
            /* (and never with an enderman about: the pillar is where one is fought; the arena's 3 envs of 16 killed
             * jumping off) */
            if (up && p->phase == PP_SCAN && e->te - p->ender_te > PEARL_ENDER_ABOUT && !p->tangry &&
                ++p->pillar_hurts >= PEARL_PILLAR_HURTS && (r->y < pp_land(p) + PEARL_PILLAR + 0.5 || r->health > 4.0f) &&
                (!PP_TOWER_ON || r->y < pp_land(p) + PEARL_TOWER_JUMP))
            {
                p->fx = (float)(r->x - sin(yaw * M_PI / 180.0) * 8.0);
                p->fz = (float)(r->z + cos(yaw * M_PI / 180.0) * 8.0);
                p->heading = p->hurt_yaw + 180.0f;
                p->base = NAN;
                p->placed = 0;
                p->look_back = 0;
                pp_set(p, PP_FLEE);
            }
        }
    }
    /* lane/pearl2: hurt to PEARL_HIDE_HP on the ground with nothing in view within PEARL_HIDE_NEAR: the hide */
    /* lane/pdig (F, pearl_hide_fight): with a provoked enderman about, the hide at PEARL_LOW_HP or under at once, its
     * quiet and its nothing-near tests skipped (fs1's eye legs: 17 of 56 provoked and unkilled ended with the player
     * dead in the fight, health from 15 at the scream to 1, 15 to 17 points taken, the enderman's own melee in 11) */
    int hide_fight = MOPT(MO_PEARL_HIDE_FIGHT) && p->tangry && r->health <= PEARL_LOW_HP;
    if ((r->health <= PEARL_HIDE_HP || hide_fight) && !up && !p->climb && p->phase != PP_HIDE && p->phase != PP_UP &&
        p->phase != PP_DOWN && r->on_ground && !s->eye_water && p->clock >= p->hide_off &&
        (hide_fight || p->clock - p->hit_clock > PEARL_HIDE_QUIET) && r->y >= PEARL_HIDE_MIN_Y &&
        pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_HIDE_DEEP + 1)
    {
        double d;
        int h = pp_hostile(s, r, ey, &d);
        int ender_near = 0;
        for (int i = 0; i < p->far.n && i < NETH_RL_FAR; ++i)
            ender_near |= p->far.d2[i] < (float)(PEARL_HIDE_NEAR * PEARL_HIDE_NEAR);
        if (hide_fight || (!(h >= 0 && d < PEARL_HIDE_NEAR) && !ender_near))
        {
            e->fid = 0;
            p->tid = 0;
            p->hs = HS_DIG, p->hy0 = r->y;
            p->cs.hides += 1;
            pp_set(p, PP_HIDE);
        }
    }
    /* lane/pearl3: the cave hunt builds no pillar: the surface's walk and pillar phases are its staircase, tunnel and
     * hold (the hide's climb out and a pillar still standing run as before) */
    if (cave && !up && !p->climb &&
        (p->phase == PP_WALK || p->phase == PP_UP || p->phase == PP_SCAN || p->phase == PP_DOWN))
        pp_set(p, pc_home(v, p, r));
    int ph = p->phase;
    if (ph == PP_WALK || ph == PP_SCAN || ph == PP_TUNNEL || ph == PP_HOLD)
    {
        /* a target: the one followed while in view, else the nearest enderman in view not given up */
        if (p->tid == 0)
            for (int i = 0; i < p->far.n && i < NETH_RL_FAR && k < 0; ++i)
                if (!pp_bad(p, p->far.e[i].id))
                {
                    k = i;
                    p->tid = p->far.e[i].id;
                    p->tlost = p->tstare = p->tangry = 0;
                    p->thit = -100;
                }
        if (k >= 0)
        {
            p->tx = p->far.e[k].x, p->ty = p->far.e[k].y, p->tz = p->far.e[k].z, p->th = p->far.e[k].h;
            p->tdist = py_dist3(r->x, ey, r->z, p->tx, p->ty + 0.5 * p->th, p->tz);
            p->tlost = 0;
        }
    }
    switch (ph)
    {
    case PP_WALK:
    {
        /* lane/pearl2: drops remembered are gone for from the walk too once it is quiet (lane/pearl went for them only
         * as a pillar came down: a pillar left any other way forgot them; 47 kills, 10 envs with a pearl at 96 envs) */
        if (k < 0 && p->ndrop && e->te - drop_te > 2 * PEARL_QUIET)
        {
            pp_set(p, PP_DROP);
            break;
        }
        if (k >= 0)
        {
            pp_set(p, pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE && r->on_ground && !s->eye_water &&
                          p->clock >= p->up_off ? PP_UP : PP_SCAN);
            break;
        }
        double d;
        int h = pp_hostile(s, r, ey, &d);
        if (h >= 0 && s->vis[h].kind == HK_CREEPER && d < PEARL_CREEPER)
        {
            p->fx = s->vis[h].x, p->fz = s->vis[h].z;
            pp_set(p, PP_FLEE);
            break;
        }
        int can_up = pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE && r->on_ground && !s->eye_water &&
                     p->clock >= p->up_off;
        (void)can_up;
        /* (a zombie or a spider is fought, not fled: a zombie kept pace with the flee and hit as it caught up, 16 of 70
         * first deaths at 96 envs; the sword kills it in 4 hits) */
        if (h >= 0 && d < PEARL_GUARD && r->health <= PEARL_LOW_HP && s->vis[h].kind == HK_SKELETON)
        {
            /* lane/pearl2: hurt with a hostile near: sprinted away (lane/pearl went up a pillar, which a hit's
             * knockback stalls: the hide comes once it is quiet) */
            p->fx = s->vis[h].x, p->fz = s->vis[h].z;
            pp_set(p, PP_FLEE);
            break;
        }
        /* a skeleton in view past the guard's reach: walked away from (it shot the walk: 14 of 28 first deaths) */
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].kind == HK_SKELETON && p->clock >= p->turn_off &&
                py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.9, s->vis[i].z) >= PEARL_GUARD)
            {
                double yaw, pitch;
                look_at(s->vis[i].x, ey, s->vis[i].z, r->x, ey, r->z, &yaw, &pitch);
                p->heading = (float)yaw;
                p->turn_off = p->clock + PEARL_SWIM_TURN;
            }
        /* lane/pearl3: a zombie (or a witch) still a few blocks off: the pillar, and it is hit from the top (the sweep's
         * deaths: 769 of 4,277 were zombies in the ground fight; its attack needs the boxes level in height, and the
         * player's reach meets its head at the pillar's foot) */
        if (h >= 0 && d < 16.0 && d > PEARL_ZOMBIE_UP && v->pt.zombie_pillar &&
            (s->vis[h].kind == HK_ZOMBIE || s->vis[h].kind == HK_WITCH) && can_up)
        {
            p->zombie_up = 1;
            pp_set(p, PP_UP);
            break;
        }
        if (h >= 0 && d < PEARL_GUARD)
        {
            e->fid = s->vis[h].id;
            e->flost = 0;
            e->flast = -100;
            e->age = 0;
            pp_set(p, PP_GUARD);
            break;
        }
        /* lane/pearlsee (3rd pass): pearl_oracle on (PRIVILEGED), toward the nearest enderman under the sky the census
         * lists (the server's entities: what no screen shows), no tower or pillar on the way; once it is in the macro's
         * own view the pillar's fight takes it (k >= 0 above: PEARL_PILLAR blocks with a target, pearl_lure's) */
        if (MOPT(MO_PEARL_ORACLE))
        {
            double ox, oz, od, ody;
            int oclear;
            if (pp_oracle_item(v, e, r, s, a, p, ey))
            {
                p->pstep = 0;
                break;
            }
            /* lane/pdig (P2): pearl_patrol at night in the overworld: the nearest under the sky (or provoked), walked to
             * straight (pp_patrol), out from under a roof by the sky dig first */
            int patrol = MOPT(MO_PEARL_PATROL) && r->dim == 0 && pearl_night(r);
            int found = pp_oracle(p, r, &ox, &oz, &od, &ody, &oclear,
                                  MOPT(MO_PEARL_TARGET) == PTG_ANY && (!patrol || MOPT(MO_PEARL_PATROL) == 2));
            /* lane/pground (pearl_march): no census target within PEARL_MARCH_NEAR (or none with a path): the march */
            if (MOPT(MO_PEARL_MARCH) && r->dim == 0 && pearl_night(r) && (!found || od > PEARL_MARCH_NEAR))
            {
                pp_march(v, e, r, s, a, p, ey);
                p->pstep = 0;
                break;
            }
            if (found)
            {
                double yaw, pitch;
                look_at(r->x, ey, r->z, ox, ey + ody, oz, &yaw, &pitch);   /* its middle: in the frame's middle */
                p->heading = (float)yaw;
                p->book.oracle_steps += 1;
                p->book.oracle_d[od < 16.0 ? 0 : od < 32.0 ? 1 : od < 64.0 ? 2 : 3] += 1;
                /* its kill (the stats screen's count up a few steps after a hit of ours): booked, its drop walked to */
                if (e->te - p->kill_te <= PEARL_KILL_STEPS && p->clock - p->thit <= PEARL_ORACLE_KILL_T)
                {
                    p->kill_te = -100;
                    p->tkill += 1;      /* (lane/pnight: prov_end's) */
                    if (p->ndrop == PEARL_DROPS)
                    {
                        memmove(&p->drop[0], &p->drop[1], (PEARL_DROPS - 1) * sizeof p->drop[0]);
                        p->ndrop -= 1;
                    }
                    int j = p->ndrop++;
                    p->book.drops += 1;
                    p->drop[j].x = p->tx, p->drop[j].y = p->ty, p->drop[j].z = p->tz, p->drop[j].te = e->te;
                    pp_set(p, PP_DROP);
                    break;
                }
                /* in reach: hit (a hit provokes it as a stare does; the trees' leaves and the hills block a stare's
                 * line), its middle looked at, the best weapon */
                int reach = od * od + ody * ody < PEARL_ORACLE_REACH * PEARL_ORACLE_REACH;
                /* lane/pnight (pearl_reach_aim): a swing only with an enderman under the crosshair; held off one for
                 * PEARL_REACH_BLIND steps (a floor or a roof between), the way to it below takes the step */
                if (MOPT(MO_PEARL_REACH_AIM))
                {
                    if (!reach || s->mo_ekind == HK_ENDERMAN) p->reach_blind = 0;
                    else if (++p->reach_blind > PEARL_REACH_BLIND)
                    {
                        reach = 0;
                        p->aim_book[0] += 1;
                    }
                }
                if (reach)
                {
                    look_at(r->x, ey, r->z, ox, ey + ody, oz, &yaw, &pitch);
                    a->look_mode = 1;
                    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                    a->hold = 0;
                    int wb = best_weapon(s);
                    if (wb >= 0 && wb != s->current) a->hotbar = wb;
                    if (p->clock - p->thit >= PEARL_HIT &&
                        (!MOPT(MO_PEARL_REACH_AIM) || s->mo_ekind == HK_ENDERMAN))
                    {
                        a->press[PK_ATTACK] = 1;
                        p->thit = p->clock;
                        p->book.hits += 1;
                        p->aim_book[1] += s->mo_ekind != HK_ENDERMAN;
                        p->aim_book[2] += 1;
                    }
                    p->tx = (float)ox, p->ty = (float)(ey + ody - 1.45), p->tz = (float)oz;
                    p->pstep = 0;
                    break;
                }
                (void)oclear;
                /* the engine's pathfinder's way to its cell (walkto.c walk_to, PRIVILEGED); the straight walk and the
                 * dig below only where it has no path within range */
                int wk = patrol ? -1 : walk_to(v, e, r, s, a, &p->wt, (int)floor(ox), (int)floor(ey + ody - 1.45 + 1e-3),
                                               (int)floor(oz));
                /* lane/pearlhome: pearl_target any, no walking path to one under the feet (a cave): a dug way to its
                 * cell (lane/shold's sho_follow: dug moves, nothing liquid or hot beside a dug cell; out/logs/d1-h3a
                 * env 3 stood 4,000 steps 20 over one) */
                if (wk < 0 && MOPT(MO_PEARL_TARGET) == PTG_ANY && ody < -PH_DIG_DY &&
                    (!patrol || MOPT(MO_PEARL_PATROL) == 2))
                {
                    /* the target cell kept until it moves PH_DIG_MOVE (a moved target is a new plan: replanned every
                     * step, the case ran at 750 env-steps/s against 5,000) */
                    int gx = (int)floor(ox), gy = (int)floor(ey + ody - 1.45 + 1e-3), gz = (int)floor(oz);
                    if (!p->ph_dg_set || abs(gx - p->ph_dg[0]) + abs(gz - p->ph_dg[2]) > PH_DIG_MOVE ||
                        abs(gy - p->ph_dg[1]) > PH_DIG_MOVE)
                        p->ph_dg_set = 1, p->ph_dg[0] = gx, p->ph_dg[1] = gy, p->ph_dg[2] = gz;
                    int rc = sho_follow(v, e, r, s, a, p->ph_dg[0], p->ph_dg[1], p->ph_dg[2], 2.0, 0);
                    if (rc >= 0)
                    {
                        p->book.home_digs += 1;
                        p->pstep = 0;
                        break;
                    }
                }
                /* no path, it over the eye and a roof over the player (the dusk starts: every env under the strip's
                 * stair roof at y 62, its target 9 over it on the surface, 2,457 steps against the wall): the look
                 * up, and a roof of rock, dirt or a tree within PEARL_SKY_DIG dug up through (pearl_sky's dug pillar,
                 * a block past the opening) */
                if (wk < 0 && (ody > 2.0 || (patrol && MOPT(MO_PEARL_PATROL) == 1)) && !p->far.census.pl_sky && r->on_ground && !s->eye_water &&
                    p->clock >= p->o_sky_off && pp_cobble_slot(s) >= 0)
                {
                    if (patrol && r->pitch > -85.0f) p->pt_book[6] += 1;
                    if (r->pitch > -85.0f)
                    {
                        a->look_mode = 1;
                        a->look[0] = r->yaw;
                        a->look[1] = -90.0f;
                        a->hold = 0;
                        break;
                    }
                    int lb = p->far.look_block;
                    if (p->far.look_depth < PEARL_SKY_DIG && lb != 12 && lb != 13 && !(lb >= 8 && lb <= 11) &&
                        lb != 7 && lb != OBSIDIAN)
                    {
                        p->dig_arm = 1;
                        p->o_goal = 1;
                        p->book.oracle_ups += 1;
                        p->book.sky_climbs += 1;
                        pp_set(p, PP_UP);
                        break;
                    }
                    p->o_sky_off = p->clock + PEARL_ORACLE_SKY_OFF;  /* nothing to dig over it: walked on a while */
                }
                /* near and on its level: the fight's pillar first (PEARL_PILLAR blocks), the stare from its top (the
                 * hill case: a stare while walking provoked it unbooked, and it hit the player off a half-built
                 * pillar, every env dead) */
                if (od < PEARL_ORACLE_PILLAR_D && fabs(ody) < PEARL_ORACLE_LEVEL && r->on_ground && !s->eye_water &&
                    p->clock >= p->up_off && pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE)
                {
                    p->o_goal = PEARL_PILLAR;
                    p->book.oracle_ups += 1;
                    pp_set(p, PP_UP);
                    break;
                }
                if (patrol)
                {
                    if (!p->far.census.pl_sky) p->pt_book[7] += 1;
                    pp_patrol(v, e, r, s, a, p, ey, ox, oz, od, ody);
                    p->pstep = 0;
                    break;
                }
                if (wk >= 0)
                {
                    /* the look along the way, a little down (walk_to's): never at it (a stare provokes it) */
                    p->book.oracle_path += 1;
                    p->pstep = 0;
                    break;
                }
                if (od < PEARL_ORACLE_BAD_D && MOPT(MO_PEARL_REACHABLE))
                {
                    /* no path to it within range: marked, the next nearest asked on the next step */
                    int k = p->o_badn++ % 4;
                    p->o_bad[k][0] = (float)ox, p->o_bad[k][1] = (float)(r->y + ody), p->o_bad[k][2] = (float)oz;
                    p->o_bad_t[k] = p->clock;
                    p->book.oracle_bad += 1;
                }
                a->look_mode = 1;
                a->look[0] = (float)yaw;
                a->look[1] = (float)(pitch < -35.0 ? -35.0 : pitch > 35.0 ? 35.0 : pitch);
                a->hold = 1u << PK_FORWARD;
                WK_STAMP(e, WK_OTH + 0, od, ody, e->wk), WK_FRONT(e, yaw);  /* (lane/pdig, counting only) */
                if (od > PEARL_ORACLE_SPRINT) a->hold |= 1u << PK_SPRINT;
                if (s->eye_water) a->hold |= 1u << PK_JUMP;
                /* stopped by a wall (traced: dusk-g9's env 5 held 1,000 steps against one within 16 of its enderman,
                 * the wall also its line of sight): the way ahead dug, the head's block then the feet's, with the best
                 * tool (never a liquid or bedrock) */
                int was = p->stuck;
                pp_stuck(p, r, s, a);
                if (was > 0 && p->stuck == 0 && !s->eye_water) p->o_dig = PEARL_ORACLE_DIG;
                if (p->o_dig > 0)
                {
                    p->o_dig -= 1;
                    /* a staircase: up toward one over the eye (the block ahead at the head, then the one over it,
                     * the jump held: the pathfinder has no way out of a pit or off a 2-high bank), else level */
                    int upw = ody > 1.0;
                    a->look[1] = (p->o_dig / 10) & 1 ? (upw ? -40.0f : 35.0f) : 0.0f;
                    a->hold = 1u << PK_FORWARD;
                    if (upw) a->hold |= 1u << PK_JUMP;
                    if (s->mo_hit && s->mo_block > 0 && !(s->mo_block >= 8 && s->mo_block <= 11) && s->mo_block != 7)
                    {
                        a->hold |= 1u << PK_ATTACK;
                        int best = best_tool(&v->T, s, s->mo_block);
                        if (best >= 0 && best != s->current) a->hotbar = best;
                    }
                }
                p->pstep = 0;           /* no pillar or tower on the way */
                break;
            }
        }
        /* lane/pearl2: toward the longest view seen lately (dry), within a quarter turn either side */
        /* lane/pearlgo (lane/pearlhunt's pearl_walk): straight, neither the long view's turn nor the policy's steer (the
         * turns from water, skeletons and a stuck walk stay), sprinting: the most new ground a step */
        /* lane/pearlsee: pearl_look turn, every PEARL_TURN_EVERY steps of the walk a level full turn, standing */
        if (MOPT(MO_PEARL_LOOK) == PLK_TURN)
        {
            if (p->turn_left == 0 && p->clock >= p->turn_next)
            {
                p->turn_left = PEARL_TURN_STEPS;
                p->book.turns += 1;
            }
            if (p->turn_left > 0)
            {
                if (--p->turn_left == 0) p->turn_next = p->clock + PEARL_TURN_EVERY;
                a->look_mode = 1;
                a->look[0] = r->yaw + (float)PEARL_TURN;
                a->look[1] = (float)PEARL_TURN_PITCH;
                if (s->eye_water) a->hold |= 1u << PK_JUMP;
                p->pstep -= 1;          /* the walk's own clock (the pillar's PEARL_WALK) waits */
                break;
            }
        }
        /* lane/pearlsee: pearl_tower on, the tower on the night's first dry cell, then after each PEARL_WALK (a try that
         * stopped short under a roof came down and went up again on the same cell: 10 pillars in 1,400 steps, the
         * trace of pearl_check's env 0, out/logs/trace1.txt) */
        if (PP_TOWER_ON && (p->tower_tries == 0 || p->pstep > PEARL_WALK) && p->clock >= p->up_off &&
            pp_restock_due(v, p, r, s))
        {
            pp_restock_begin(p, a);     /* lane/pearlsee (3rd pass): the pack's cobblestone first */
            p->pstep -= 1;
            break;
        }
        if (PP_TOWER_ON && (p->tower_tries == 0 || p->pstep > PEARL_WALK) && p->clock >= p->up_off &&
            !s->eye_water && r->on_ground && pp_cobble_slot(s) >= 0 &&
            inv_count(s, COBBLE) >= PEARL_TOWER + PEARL_HIDE_DEEP + 1)
        {
            /* lane/pearlsee (2nd pass): pearl_high on, the tower on the high ground seen. The dusk case's census over
             * the hunt (out/logs/c4-g9-off.log): of 4,402 endermen-steps within 64, the line clear from the eye 0,
             * from 16 over it 2,179, from the eye of a player on the highest dry ground within 48 blocks (13.7 over
             * the feet, 31.6 away) 3,231. First a level turn (the hill row in the frame), then the walk toward the
             * sector whose face was highest, sprinting, until none seen is PEARL_HIGH_MIN over the feet or
             * PEARL_HIGH_WALK steps */
            if (MOPT(MO_PEARL_HIGH))
            {
                if (p->high_look < PEARL_TURN_STEPS)
                {
                    p->high_look += 1;
                    a->look_mode = 1;
                    a->look[0] = r->yaw + (float)PEARL_TURN;
                    a->look[1] = (float)PEARL_HIGH_PITCH;
                    p->pstep -= 1;
                    break;
                }
                int bk = -1;
                for (int k = 0; k < PEARL_SECTORS; ++k)
                    if (p->hill_at[k] > 0 && p->clock - p->hill_at[k] <= PEARL_HIGH_FRESH &&
                        (bk < 0 || p->hill[k] > p->hill[bk]))
                        bk = k;
                if (bk >= 0 && p->hill[bk] > r->y + PEARL_HIGH_MIN && p->high_steps < PEARL_HIGH_WALK)
                {
                    if (p->high_steps == 0) p->high_y0 = (float)r->y;
                    p->high_steps += 1;
                    p->book.high_steps += 1;
                    p->heading = (float)((bk + 0.5) * (360.0 / PEARL_SECTORS));
                    a->look_mode = 1;
                    a->look[0] = p->heading;
                    a->look[1] = (float)PEARL_HIGH_PITCH;
                    a->hold = 1u << PK_FORWARD | 1u << PK_SPRINT;
                    pp_stuck(p, r, s, a);
                    p->pstep -= 1;
                    break;
                }
                if (p->high_steps > 0) p->book.high_gain += (int)lround(r->y - p->high_y0), p->book.high_walks += 1;
                p->high_steps = 0;      /* (high_look is cleared as the try ends: the tower begun or refused) */
            }
            /* lane/pearlsee (2nd pass): pearl_sky on, the look straight up first; a roof over the cell within the
             * centre ray's reach (the strip's stair at y 62, a tree): walked on from. The dusk case's climbs from
             * gold-g9's stair all ended at the two short jumps' roof (165 of 165, out/logs/c1-g9.log) */
            if (MOPT(MO_PEARL_SKY))
            {
                if (r->pitch > -85.0f)
                {
                    a->look_mode = 1;
                    a->look[0] = r->yaw;
                    a->look[1] = -90.0f;
                    p->pstep -= 1;
                    break;
                }
                if (p->far.look_depth < (float)NETH_RL_DEPTH_R - 0.5f)
                {
                    p->high_look = 0;
                    p->tower_tries += 1;
                    /* a roof of the ground's own (stone, dirt, snow: not a tree's) within PEARL_SKY_DIG: the tower dug
                     * up through it (PP_UP's dig: the block over the head mined before each jump) */
                    int lb = p->far.look_block;
                    if (lb != 17 && lb != 18 && lb != 12 && lb != 13 && !(lb >= 8 && lb <= 11) &&
                        p->far.look_depth < PEARL_SKY_DIG)
                    {
                        p->book.sky_climbs += 1;
                        p->dig_arm = 1;
                        pp_set(p, PP_UP);
                        break;
                    }
                    p->book.upx[UX_SKY] += 1;
                    p->pstep = PEARL_WALK - PEARL_SKY_WALK;
                    a->look_mode = 1;
                    a->look[0] = p->heading;
                    a->look[1] = (float)PEARL_WALK_PITCH;
                    a->hold = 1u << PK_FORWARD;
                    break;
                }
            }
            p->tower_tries += 1;
            p->high_look = 0;
            pp_set(p, PP_UP);
            break;
        }
        int straight = MOPT(MO_PEARL_WALK) == PWK_STRAIGHT;
        if (p->far.depth_on && p->clock >= p->rehead && !straight)
        {
            p->heading = pp_best(p, p->heading, 90.0);
            p->rehead = p->clock + PEARL_REHEAD;
        }
        if (!straight) p->heading += steer;
        a->look_mode = 1;
        a->look[0] = p->heading + (float)(PEARL_SWEEP * sin(2.0 * M_PI * p->pstep / PEARL_SWEEP_T));
        a->look[1] = (float)PEARL_WALK_PITCH;
        a->hold = 1u << PK_FORWARD;
        if (MOPT(MO_PEARL_WALK) != PWK_SWEEP) a->hold |= 1u << PK_SPRINT;
        pp_stuck(p, r, s, a);
        /* water or lava ahead (the bucket's ray, 5 blocks along the look, meets a source): turned away */
        if (s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && p->clock >= p->turn_off)
        {
            p->heading += 90.0f;
            p->turn_off = p->clock + 20;
        }
        /* lane/pearl2: the pillar where the view is long (or after PEARL_OPEN_WALK steps of looking) */
        if (p->pstep > PEARL_WALK && p->clock >= p->up_off && !s->eye_water && r->on_ground &&
            (!p->far.depth_on || pp_open(p) >= PEARL_OPEN || p->pstep > PEARL_OPEN_WALK))
            pp_set(p, pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE ? PP_UP : PP_SCAN);
        break;
    }
    case PP_UP:
    {
        int hb = pp_cobble_slot(s);
        if (hb < 0 && pp_restock_due(v, p, r, s))
        {
            pp_restock_begin(p, a);     /* lane/pearlsee (3rd pass): on the pillar's top, the pack's cobblestone */
            break;
        }
        /* the blocks to place (lane/pearlsee: a dug tower's PEARL_TOWER from where the sky showed over it) */
        int goal = MOPT(MO_PEARL_ORACLE) && p->dig ? (p->dig_open ? p->open_placed + 2 : 1000)
                   : !PP_TOWER_ON ? (MOPT(MO_PEARL_ORACLE) && p->o_goal > 0 ? p->o_goal : PEARL_TALL)
                   : !p->dig ? PEARL_TOWER : p->dig_open ? p->open_placed + PEARL_TOWER : 1000;
        if (hb < 0 || p->pstep > (p->dig ? PEARL_DIG_CAP : PP_TOWER_ON ? 400 : 160))
        {
            p->upwhy = (hb < 0 ? UX_NOBLOCK : UX_CAP) + 1;
            if (p->climb)
            {
                p->climb = 0, p->base = NAN, p->placed = 0;
                pp_set(p, PP_WALK);
            }
            else pp_set(p, PP_SCAN);
            break;
        }
        /* lane/pearlsee (2nd pass): pearl_sky's dug tower, before each jump: the look up; the block over the head (the
         * feet's cell + 2) mined with its tool; sand, gravel, bedrock or obsidian there ends it (a roof); the first look
         * up that meets nothing within NETH_RL_DEPTH_R: the sky, PEARL_TOWER blocks from here */
        if (p->dig && p->pwait == 0 && r->on_ground)
        {
            if (r->pitch > -85.0f)
            {
                a->look_mode = 1;
                a->look[0] = r->yaw;
                a->look[1] = -90.0f;
                break;
            }
            int fx = (int)floor(r->x), fy = (int)floor(r->y + 1e-3), fz = (int)floor(r->z);
            if (!p->dig_open && p->far.look_depth >= (float)NETH_RL_DEPTH_R - 0.5f)
                p->dig_open = 1, p->open_placed = p->placed, p->book.dig_open += 1;
            if (s->mo_hit && s->mo_x == fx && s->mo_z == fz && s->mo_y == fy + 2)
            {
                int b = s->mo_block;
                if (b == 12 || b == 13 || b == 7 || b == OBSIDIAN)
                {
                    p->upwhy = UX_ROOF + 1;
                    pp_set(p, p->placed ? PP_SCAN : PP_WALK);
                    break;
                }
                int tb = best_tool(&v->T, s, b);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->look_mode = 1;
                a->look[0] = r->yaw;
                a->look[1] = -90.0f;
                a->hold = 1u << PK_ATTACK;
                p->book.dig_steps += 1;
                break;
            }
        }
        if (hb != s->current) a->hotbar = hb;
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = 90.0f;
        if (p->pwait == 0)
        {
            if (!r->on_ground) break;
            if (p->placed == 0) p->base = r->y, p->pillar_hurts = 0;
            p->jbase = r->y;
            p->pbefore = (int)inv_count(s, COBBLE);
            p->pwait = 1;
        }
        if (p->pwait == 1)
        {
            a->hold |= 1u << PK_JUMP;
            if (r->y > p->jbase + 1.05 && s->current == hb)
            {
                a->press[PK_USE] = 1;
                p->pwait = 2;
                break;
            }
            /* a jump that came down short (a roof of leaves or rock over the head): twice, and the pillar is built
             * elsewhere (a short walk on), or scanned from as high as it got */
            if (r->y > p->jpeak) p->jpeak = r->y;
            if (r->on_ground && p->jpeak > p->jbase + 0.1 && r->y < p->jbase + 0.05)
            {
                p->jpeak = 0;
                if (++p->upfail >= 2)
                {
                    p->upfail = 0;
                    if (p->climb) p->climb = 0, p->base = NAN, p->placed = 0;
                    p->up_off = p->clock + PEARL_UP_OFF;
                    if (p->placed == 0) p->base = NAN;
                    p->upwhy = UX_ROOF + 1;
                    pp_set(p, p->placed ? PP_SCAN : PP_WALK);
                    if (p->phase == PP_WALK) p->pstep = PEARL_WALK - PEARL_UP_OFF;
                }
            }
            break;
        }
        /* (lane/pearlsee: pearl_tower on, a jump from a snow layer lands 0.875 up: the layer was replaced by the block;
         * the exact test held the tower's climb to its 400 steps, 4 of 81 towers topped out on the ice plains) */
        double rise = r->y - p->jbase;
        if (inv_count(s, COBBLE) < p->pbefore && r->on_ground &&
            (PP_TOWER_ON ? rise > 0.8 && rise < 1.05 : fabs(rise - 1.0) < 0.01))
        {
            p->placed += 1;
            p->pwait = 0;
            p->upfail = 0;
            p->jpeak = 0;
            if (p->placed == 1 && !p->climb) p->book.pillars += 1;
            if (p->placed >= PEARL_PILLAR && p->climb)
            {
                /* lane/pearl2: out of the hide's hole, on the ground again */
                p->climb = 0, p->base = NAN, p->placed = 0;
                p->upwhy = UX_CLIMB + 1;
                pp_set(p, PP_WALK);
            }
            else if (p->placed >= goal || (p->placed >= PEARL_PILLAR && (p->tid || p->far.n > 0 || p->zombie_up)))
            {
                if (PP_TOWER_ON && p->placed >= goal && goal >= PEARL_TOWER)
                    p->book.towers += 1, p->towers_n += 1;
                p->zombie_up = 0;
                p->upwhy = (p->placed >= goal ? UX_TOP : UX_SHORT) + 1;
                /* pearl_oracle's dig up through a roof: out at the ground's level, the walk on (its own target) */
                if (MOPT(MO_PEARL_ORACLE) && p->dig && p->placed >= goal)
                {
                    p->climb = 0, p->base = NAN, p->placed = 0;
                    pp_set(p, PP_WALK);
                }
                else
                    pp_set(p, PP_SCAN); /* lane/pearl2: tall to scan, PEARL_PILLAR with an enderman (lane/pearl3: or a
                                         * zombie) about */
            }
        }
        else if (r->on_ground && r->y < p->jbase + 0.5)
            p->pwait = 0;               /* landed where it jumped from: again */
        break;
    }
    case PP_SCAN:
    {
        int wb = best_weapon(s);
        /* At normal health below full, wait on the pillar rather than provoke a new enderman. An angry lock is still
         * fought; at dawn the food scout can restock before the next night. */
        if (MOPT(MO_FOOD_TRIPS) == FT_PEARL && pearl_night(r) && r->health < 20.0f && up && !p->tangry &&
            (k < 0 || !p->far.e[k].scream))
            break;
        if (k >= 0)
        {
            if (p->far.e[k].scream && !p->tangry)
            {
                p->tangry = 1;
                p->book.provoked += 1;
            }
            double hd = py_hypot2(p->tx - r->x, p->tz - r->z);
            /* on the ground with time before it is near: the pillar first */
            if (!up && hd > PEARL_UP_NEAR && pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE &&
                r->on_ground && !s->eye_water && p->clock >= p->up_off)
            {
                pp_set(p, PP_UP);
                break;
            }
            /* lane/pearl2: provoked and near, on the tall pillar: dug down to PEARL_PILLAR (a block a time, the pick) */
            if (up && p->tangry && hd < PEARL_LOWER_R && r->y > pp_floor(v, p) + 0.5)
            {
                int fy = (int)floor(r->y + 1e-3);
                a->look_mode = 1;
                a->look[0] = r->yaw;
                a->look[1] = 90.0f;
                if (s->mo_hit && s->mo_y == fy - 1 && s->mo_x == (int)floor(r->x) && s->mo_z == (int)floor(r->z))
                {
                    int tb = best_tool(&v->T, s, s->mo_block);
                    if (tb >= 0 && tb != s->current) a->hotbar = tb;
                    a->hold = 1u << PK_ATTACK;
                }
                break;
            }
            if (!p->tangry)
            {
                pp_look(a, r, ey, p->tx, p->ty + 0.5 * p->th, p->tz);     /* the stare: the box middle */
                if (++p->tstare > PEARL_STARE)
                {
                    p->bad[p->nbad++ % PEARL_BAD] = p->tid;
                    p->cs.stare_out += 1;
                    if (p->nbad > PEARL_BAD) p->nbad = PEARL_BAD;
                    p->tid = 0;
                }
            }
            else if (hd <= PEARL_NEAR)
                pp_look(a, r, ey, p->tx, p->ty + p->th - PEARL_TOP, p->tz);
            else
                pp_look(a, r, ey, p->tx, p->ty - PEARL_LOW, p->tz);
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            if (s->mo_entity == p->tid && p->clock - p->thit >= PEARL_HIT)
            {
                a->press[PK_ATTACK] = 1;
                p->thit = p->clock;
                p->book.hits += 1;      /* lane/pearlgo's census */
            }
            /* angry and far (no path past 16 blocks; across water its teleports toward the player fail): on the ground
             * gone to; on the pillar waited for PEARL_FAR_WAIT, then the pillar left for it */
            p->tfar = p->tangry && hd > 16.0 ? p->tfar + 1 : 0;
            if (!up && p->tangry && hd > 2.6)
            {
                a->hold = 1u << PK_FORWARD;
                pp_stuck(p, r, s, a);
            }
            /* lane/pearlsee (2nd pass): pearl_lure on, never down for a far one (it teleports to the player: the dusk
             * case's pixel arm, 7 of 8 first deaths an enderman's on the way down, out/logs/d3-g9slp-1.log): held up for
             * PEARL_LURE_FAR, then let go and the scan on */
            if (up && MOPT(MO_PEARL_LURE))
            {
                if (p->tfar > PEARL_LURE_FAR)
                {
                    p->bad[p->nbad++ % PEARL_BAD] = p->tid;
                    if (p->nbad > PEARL_BAD) p->nbad = PEARL_BAD;
                    p->tid = 0, p->tfar = 0;
                }
            }
            else if (up && p->tfar > PEARL_FAR_WAIT)
            {
                p->up_off = p->clock + 2 * PEARL_FAR_WAIT;
                p->tfar = 0;
                pp_set(p, PP_DOWN);
            }
            break;
        }
        if (p->tid)
        {
            /* lane/pearl2: a provoked one out of view while on the ground: the pillar first (it comes back to the
             * player: the arena's 3 envs of 16 killed watching for it on the ground) */
            if (!up && p->tangry && pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE && r->on_ground &&
                !s->eye_water && p->clock >= p->up_off)
            {
                pp_set(p, PP_UP);
                break;
            }
            /* out of view (a teleport, behind something): its last place watched by its top; lane/pearl2: after
             * PEARL_LOST_LOOK steps, a provoked one is looked for around (it teleports toward the player: 14 of 29
             * provoked were let go watching the last place) */
            /* lane/pearlsee (2nd pass): pearl_lure on, a provoked one lost from a tall tower: the tower dug down to
             * PEARL_PILLAR on (it comes to the foot, where only the pillar's height reaches its head; the kill case's 32
             * envs dug one block, lost it under the tower and never hit it: out/logs/tk1.log), and looked for round the
             * foot (PEARL_LURE_PITCH down) */
            int lure = MOPT(MO_PEARL_LURE) && up && p->tangry;
            if (lure && r->y > pp_floor(v, p) + 0.5 && p->tlost <= PEARL_LOST)
            {
                int fy = (int)floor(r->y + 1e-3);
                a->look_mode = 1;
                a->look[0] = r->yaw;
                a->look[1] = 90.0f;
                if (s->mo_hit && s->mo_y == fy - 1 && s->mo_x == (int)floor(r->x) && s->mo_z == (int)floor(r->z))
                {
                    int tb = best_tool(&v->T, s, s->mo_block);
                    if (tb >= 0 && tb != s->current) a->hotbar = tb;
                    a->hold = 1u << PK_ATTACK;
                }
                p->book.lure_digs += 1;
                break;
            }
            pp_look(a, r, ey, p->tx, p->ty + p->th - PEARL_TOP, p->tz);
            if (p->tangry && p->tlost >= PEARL_LOST_LOOK)
            {
                a->look[0] = r->yaw + (float)PEARL_LOST_TURN;
                a->look[1] = up ? (lure ? (float)PEARL_LURE_PITCH : 20.0f) : 5.0f;
            }
            if (++p->tlost > PEARL_LOST)
            {
                if (p->tangry) p->cs.lost_angry += 1;
                else p->cs.lost_calm += 1;
                p->tid = 0;
            }
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            break;
        }
        double d;
        int h = pp_hostile(s, r, ey, &d);
        /* lane/pearl3: from the pillar, a zombie (or witch) at its foot by its head (its middle is out of PEARL_REACH, its
         * box's top is not) */
        double dtop = h >= 0 ? py_dist3(r->x, ey, r->z, s->vis[h].x, s->vis[h].y + s->vis[h].h - PEARL_TOP, s->vis[h].z)
                             : 1e9;
        if (up && v->pt.zombie_pillar && h >= 0 && d >= PEARL_REACH && dtop < PEARL_HEAD_REACH &&
            (s->vis[h].kind == HK_ZOMBIE || s->vis[h].kind == HK_WITCH))
        {
            pp_look(a, r, ey, s->vis[h].x, s->vis[h].y + s->vis[h].h - PEARL_TOP, s->vis[h].z);
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            if (s->mo_entity == s->vis[h].id && p->clock - p->thit >= PEARL_HIT)
            {
                a->press[PK_ATTACK] = 1;
                p->thit = p->clock;
            }
            p->pstep -= 1;      /* the scan's clock waits while it fights */
            break;
        }
        if (h >= 0 && d < PEARL_REACH && s->vis[h].kind != HK_CREEPER)
        {
            pp_look(a, r, ey, s->vis[h].x, s->vis[h].y + 0.5 * s->vis[h].h, s->vis[h].z);
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            if (s->mo_entity == s->vis[h].id && p->clock - p->thit >= PEARL_HIT)
            {
                a->press[PK_ATTACK] = 1;
                p->thit = p->clock;
            }
            break;
        }
        if (!up && h >= 0 && d < PEARL_GUARD)
        {
            pp_set(p, PP_WALK);         /* on the ground: the walk's guard takes it */
            break;
        }
        a->look_mode = 1;
        a->look[0] = r->yaw + (float)PEARL_TURN;
        /* lane/pearlsee: pearl_tower on, from a tower's top: the far ground, the whole PEARL_TOWER_SCAN, no short cut */
        int tw = PP_TOWER_ON && up && r->y > pp_land(p) + PEARL_TALL + 0.5;
        a->look[1] = (float)(tw ? PEARL_TOWER_PITCH : PEARL_PITCH);
        /* the drops fetched when it is quiet (or before they despawn); else on after PEARL_SCAN, healed first */
        if (up && p->ndrop && (e->te - drop_te > PEARL_QUIET || e->te - p->drop[0].te > PEARL_DROP_AGE))
            pp_set(p, PP_DOWN);
        else if ((p->pstep > (tw ? PEARL_TOWER_SCAN : PEARL_SCAN) ||
                  (!tw && p->far.depth_on && p->pstep > PEARL_SCAN_SHORT && pp_open(p) < PEARL_OPEN &&
                   e->te - p->ender_te > PEARL_ENDER_ABOUT)) &&
                 !(up && r->health < PEARL_HEAL_HP))
        {
            /* lane/pearl2: a short view cuts the scan; the walk on goes toward the longest view */
            if (p->far.depth_on) p->heading = pp_best(p, p->heading, 180.0);
            pp_set(p, up ? PP_DOWN : PP_WALK);
        }
        break;
    }
    case PP_DOWN:
    {
        /* another enderman in view: kept up for it, the pillar built back to its height if dug (the drop waits; an
         * angry one met the arena's player half way down in 8 of 8 envs) */
        int near = 0;
        for (int i = 0; i < p->far.n && i < NETH_RL_FAR; ++i)
            near |= !pp_bad(p, p->far.e[i].id) && p->far.d2[i] < (float)(PEARL_DOWN_R * PEARL_DOWN_R) &&
                    p->far.e[i].scream;     /* lane/pearl2: a provoked one (a calm one does not come for the player:
                                             * kept up for any, the arena's player rebuilt and dug for 2,400 steps
                                             * and fetched 1 drop of 28) */
        /* lane/pearlsee (2nd pass): pearl_lure on, a provoked one still followed or screaming lately: kept up (the
         * pixel arm's dusk case: 7 of 8 first deaths an enderman's in PP_DOWN on the pillar) */
        if (MOPT(MO_PEARL_LURE) && want && pearl_up(p, r) &&
            ((p->tid && p->tangry) || (p->prov_te > 0 && e->te - p->prov_te < PEARL_LURE_HOLD)))
            near = 1;
        if (want && !isnan(p->base) && near)
        {
            /* lane/pearl2: floor, not round: mid-fall a block dug (y 2.77 over the base) is a 2-high pillar, and the
             * enderman's hit from beside a 2-high one knocked the arena's player off (3 envs of 16 killed) */
            int h = (int)floor(r->y - p->base + 0.05);
            p->placed = h < 0 ? 0 : h;
            pp_set(p, p->placed >= PEARL_PILLAR ? PP_SCAN : PP_UP);
            break;
        }
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = 90.0f;
        /* lane/pearl2: a creeper in view near the foot (looking down shows the ground around): held up until it goes,
         * at most 400 steps */
        int creeper = 0, zombie = 0;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        {
            creeper |= s->vis[i].kind == HK_CREEPER &&
                       py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.85, s->vis[i].z) < PEARL_DOWN_CREEPER;
            zombie |= (s->vis[i].kind == HK_ZOMBIE || s->vis[i].kind == HK_WITCH) &&
                      py_hypot2(s->vis[i].x - r->x, s->vis[i].z - r->z) < PEARL_DOWN_ZOMBIE;
        }
        /* lane/pearl3: nor with a zombie (or witch) near the foot at night: back to the scan, which hits it from the top */
        if (v->pt.zombie_pillar && zombie && want && pearl_up(p, r))
        {
            pp_set(p, PP_SCAN);
            break;
        }
        if (creeper && pearl_up(p, r) && p->pwait < 400)
        {
            p->pwait += 1;
            p->pstep -= 1;
            break;
        }
        int fy = (int)floor(r->y + 1e-3);
        int on_cobble = s->mo_hit && s->mo_block == COBBLE && s->mo_y == fy - 1 && s->mo_x == (int)floor(r->x) &&
                        s->mo_z == (int)floor(r->z);
        if (isnan(p->base) || (r->on_ground && (r->y < pp_land(p) + 0.05 || (!on_cobble && p->pstep > 5))) || p->pstep > 300)
        {
            p->base = NAN;
            p->placed = 0;
            if (!want && !p->ndrop) return 1;
            pp_set(p, p->ndrop ? PP_DROP : PP_WALK);
            break;
        }
        int tb = best_tool(&v->T, s, COBBLE);
        if (tb >= 0 && tb != s->current) a->hotbar = tb;
        if (on_cobble) a->hold = 1u << PK_ATTACK;
        break;
    }
    case PP_DROP:
    {
        /* an enderman in view within 12 blocks: up again first (the drop waits) */
        if (p->far.n > 0 && p->far.d2[0] < 144.0f && pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE &&
            r->on_ground && !s->eye_water)
        {
            pp_set(p, PP_UP);
            break;
        }
        /* pearl_oracle: the pearl item's own place (the census), else none dropped: the walk on */
        if (MOPT(MO_PEARL_ORACLE) && p->far.census.on)
        {
            if (pp_oracle_item(v, e, r, s, a, p, ey)) break;
            if (p->pstep > PEARL_ORACLE_NODROP)
            {
                p->ndrop = 0;
                p->pstep = p->pwait = 0;
                pp_set(p, PP_WALK);
                break;
            }
        }
        if (p->ndrop == 0)
        {
            if (!want) return 1;
            pp_set(p, pp_cobble_slot(s) >= 0 && inv_count(s, COBBLE) >= PEARL_COBBLE ? PP_UP : PP_WALK);
            break;
        }
        /* the nearest drop: walked to, stood on PEARL_DROP_WAIT steps (the item's pickup delay, its fall), let go */
        int j = 0;
        for (int i = 1; i < p->ndrop; ++i)
            if (py_hypot2(p->drop[i].x - r->x, p->drop[i].z - r->z) < py_hypot2(p->drop[j].x - r->x, p->drop[j].z - r->z))
                j = i;
        double hd = py_hypot2(p->drop[j].x - r->x, p->drop[j].z - r->z);
        pp_look(a, r, ey, p->drop[j].x, p->drop[j].y + 0.2, p->drop[j].z);
        a->look[1] = a->look[1] > 40.0f ? 40.0f : a->look[1];
        /* lane/pearlsee (2nd pass): pearl_fetch stay, the walk longer (a stuck walk strafes round what stops it) and
         * the drop stood on until a pearl comes into the inventory (the one kill of the first pass gave up at the cap,
         * 72 steps of PEARL_DROP's 70) */
        int stay = MOPT(MO_PEARL_FETCH) == PFE_STAY;
        /* lane/pearlsee (3rd pass): pearl_oracle on, the drop's cell walked to on the pathfinder's way (PRIVILEGED), and
         * reached only at its height (the hill case's 31 drops were all "walked onto" from the cliff's foot, 4 under
         * them: 1 pearl) */
        if (MOPT(MO_PEARL_ORACLE) && !(hd <= 0.4 && fabs(r->y - p->drop[j].y) < 1.5) && p->pstep <= PEARL_ORACLE_DROP)
        {
            int wk = walk_to(v, e, r, s, a, &p->wt, (int)floor(p->drop[j].x), (int)floor(p->drop[j].y + 1e-3),
                             (int)floor(p->drop[j].z));
            if (wk >= 0)
            {
                if (p->pstep == 1) p->fetch_held = (int)pearl_held(s);
                if ((int)pearl_held(s) > p->fetch_held) p->pstep = PEARL_ORACLE_DROP + 1;    /* picked up on the way */
                break;
            }
        }
        if (hd > 0.4) a->hold = 1u << PK_FORWARD;
        else
        {
            if (p->pwait == 0) p->book.fetched += 1;
            p->pwait += 1;
        }
        pp_stuck(p, r, s, a);
        if (stay && hd > 0.4 && p->stuck > PEARL_FETCH_STUCK)
            a->hold |= 1u << (((p->pstep / PEARL_FETCH_SIDE) & 1) ? PK_LEFT : PK_RIGHT);
        if (p->pstep == 1) p->fetch_held = (int)pearl_held(s);
        int got = (int)pearl_held(s) > p->fetch_held;
        if (stay ? got || p->pwait > PEARL_FETCH_WAIT || p->pstep > PEARL_FETCH_STEPS
                 : p->pwait > PEARL_DROP_WAIT || p->pstep > PEARL_DROP)
        {
            p->book.fetch_out += !got && p->pwait <= (stay ? PEARL_FETCH_WAIT : PEARL_DROP_WAIT);
            p->cs.drop_out += p->pwait <= PEARL_DROP_WAIT;
            memmove(&p->drop[j], &p->drop[j + 1], (size_t)(p->ndrop - j - 1) * sizeof p->drop[0]);
            p->ndrop -= 1;
            p->pstep = p->pwait = 0;
        }
        break;
    }
    case PP_FLEE:
    {
        /* lane/pearl2: a creeper in view within PEARL_CREEPER is fled from instead (5 of 39 first deaths at 96 envs
         * ran into one); the walk's look down (the bucket's ray meets water 3 ahead) turns the run from open water, as
         * the walk's does (14 of 80 drowned at 1 to 2 HP fleeing over a frozen river, under its ice) */
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].kind == HK_CREEPER &&
                py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.85, s->vis[i].z) < PEARL_CREEPER)
                p->fx = s->vis[i].x, p->fz = s->vis[i].z;
        pp_look(a, r, ey, 2.0 * r->x - p->fx, ey, 2.0 * r->z - p->fz);
        a->look[0] += p->flee_turn;
        a->look[1] = (float)PEARL_WALK_PITCH;
        if (s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && p->clock >= p->turn_off)
        {
            p->flee_turn += 90.0f;
            p->turn_off = p->clock + 20;
        }
        a->hold = (1u << PK_FORWARD) | (1u << PK_SPRINT);
        pp_stuck(p, r, s, a);
        if (p->pstep > PEARL_FLEE) pp_set(p, PP_WALK);
        break;
    }
    case PP_HIDE:
    {
        int fy = (int)floor(r->y + 1e-3), cx = (int)floor(r->x), cz = (int)floor(r->z);
        if (s->eye_water)
        {
            /* water came in: given up (the surface macro swims) */
            p->hs = HS_NONE, p->hide_off = p->clock + PEARL_HIDE_OFF;
            pp_set(p, PP_WALK);
            break;
        }
        if (p->hs == HS_DIG)
        {
            /* straight down, the block under the feet with its tool, until PEARL_HIDE_DEEP down */
            a->look_mode = 1;
            a->look[0] = r->yaw;
            a->look[1] = 90.0f;
            if (r->on_ground && r->y <= p->hy0 - PEARL_HIDE_DEEP + 0.01)
            {
                p->hs = HS_COVER, p->pstep = 0, p->hcover_try = 0;
                break;
            }
            /* the block under the feet, or what stands in the feet's cell over it (a snow layer: the ice plains) */
            int below = s->mo_hit && (s->mo_y == fy - 1 || s->mo_y == fy) && s->mo_x == cx && s->mo_z == cz;
            /* bedrock, obsidian, a liquid, ice (broken, it turns to water and floods the hole) */
            int bad = below && (s->mo_block == 7 || s->mo_block == 49 || (s->mo_block >= 8 && s->mo_block <= 11) ||
                                s->mo_block == 79);
            /* the block under gone, the player not dropped (off the middle): the centring below drops it */
            int hollow = s->mo_hit && s->mo_y == fy - 2 && s->mo_x == cx && s->mo_z == cz;
            if (bad || p->pstep > PEARL_HIDE_DIG || (r->on_ground && !below && !hollow && p->pstep > 10))
            {
                /* bedrock, obsidian, a liquid, a cave under (no block within reach), or too slow: given up, and
                 * climbed out where it got down */
                p->hs = HS_NONE, p->hide_off = p->clock + PEARL_HIDE_OFF;
                if (r->y < p->hy0 - 0.5 && pp_cobble_slot(s) >= 0)
                {
                    p->climb = 1, p->base = r->y, p->placed = 0;
                    pp_set(p, PP_UP);
                }
                else pp_set(p, PP_WALK);
                break;
            }
            if (below)
            {
                int tb = best_tool(&v->T, s, s->mo_block);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->hold = 1u << PK_ATTACK;
            }
            /* centred in the cell (the box, 0.6 wide, within 0.2 of the middle: off it, the box rests on a neighbour
             * when the block under goes, and the player does not drop): a step toward the middle every other step */
            double ox = cx + 0.5 - r->x, oz = cz + 0.5 - r->z;
            if ((fabs(ox) > 0.18 || fabs(oz) > 0.18) && p->pstep % 2 == 0)
            {
                double yaw, pitch;
                look_at(r->x, ey, r->z, cx + 0.5, ey, cz + 0.5, &yaw, &pitch);
                a->look[0] = (float)yaw;
                a->hold |= 1u << PK_FORWARD;
            }
            break;
        }
        if (p->hs == HS_COVER)
        {
            /* the block over the head: against the inner face of a rim block one under the ground, looked at 60
             * degrees up along a cardinal; sealed when the look straight up meets a block one under the ground */
            int hb = pp_cobble_slot(s);
            int top = (int)lround(p->hy0) - 1;
            if (hb >= 0 && hb != s->current) a->hotbar = hb;
            a->look_mode = 1;
            int sealed_look = p->pstep % 8 >= 6;
            if (sealed_look)
            {
                a->look[0] = r->yaw;
                a->look[1] = -90.0f;
                if (s->mo_hit && s->mo_y == top && s->mo_x == cx && s->mo_z == cz)
                    p->hs = HS_WAIT, p->pstep = 0, p->cs.sealed += 1;
                else if (p->pstep % 8 == 7 && ++p->hcover_try >= 8)
                    p->hs = HS_WAIT, p->pstep = 0;  /* no rim to put it on: held open (3 deep is cover from arrows) */
                break;
            }
            /* the face's middle along the cardinal (yaw 0 +z, 90 -x, 180 -z, 270 +x) from where the eye is */
            int q = p->hcover_try % 4;
            double fd = q == 0 ? cz + 1 - r->z : q == 1 ? r->x - cx : q == 2 ? r->z - cz : cx + 1 - r->x;
            a->look[0] = (float)(90.0 * q);
            a->look[1] = (float)(-atan2(top + 0.5 - ey, fd < 0.05 ? 0.05 : fd) * 180.0 / M_PI);
            if (hb >= 0 && s->current == hb && s->mo_hit && s->mo_y == top && abs(s->mo_x - cx) + abs(s->mo_z - cz) == 1 &&
                p->pstep % 8 == 3)
                a->press[PK_USE] = 1;
            break;
        }
        if (p->hs == HS_WAIT)
        {
            a->look_mode = 1;
            a->look[0] = r->yaw;
            a->look[1] = -90.0f;
            if ((r->health >= PEARL_HIDE_OUT || !want || p->pstep > PEARL_HIDE_WAIT) && !eat_want(v, r, s))
                p->hs = HS_OPEN, p->pstep = 0;
            break;
        }
        /* HS_OPEN: the cover mined (its tool), then up the hole as a pillar (PP_UP, climb: back to the walk) */
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = -90.0f;
        int top = (int)lround(p->hy0) - 1;
        if (s->mo_hit && s->mo_y <= top + 1 && s->mo_y > fy + 1 && s->mo_x == cx && s->mo_z == cz && p->pstep < 200)
        {
            int tb = best_tool(&v->T, s, s->mo_block);
            if (tb >= 0 && tb != s->current) a->hotbar = tb;
            a->hold = 1u << PK_ATTACK;
            break;
        }
        p->hs = HS_NONE;
        p->climb = 1, p->base = r->y, p->placed = 0;
        pp_set(p, PP_UP);
        break;
    }
    case PP_STAIR:
    case PP_TUNNEL:
    {
        /* lane/pearl3: down to the tunnel's height, then the tunnel; an enderman in view from the tunnel, or a long
         * view (a cave opened), begins a hold (the staircase's 3-high cells let one in: it holds nothing, and its look
         * is on the cells: no stare) */
        if (ph == PP_TUNNEL && k >= 0)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, p->tx, ey, p->tz, &yaw, &pitch);
            pc_hold_begin(v, p, r,(float)yaw);
            break;
        }
        double d;
        int h = pp_hostile(s, r, ey, &d);
        if (h >= 0 && s->vis[h].kind == HK_CREEPER && d < PEARL_CREEPER)
        {
            p->fx = s->vis[h].x, p->fz = s->vis[h].z;
            pp_set(p, PP_FLEE);
            break;
        }
        if (h >= 0 && d < PEARL_GUARD)
        {
            e->fid = s->vis[h].id;
            e->flost = 0;
            e->flast = -100;
            e->age = 0;
            pp_set(p, PP_GUARD);
            break;
        }
        if (ph == PP_STAIR && r->y <= pc_floor(v, p) + 0.5)
        {
            pp_set(p, PP_TUNNEL);
            break;
        }
        if (ph == PP_STAIR && p->cwalk > 0)
        {
            /* bad floors all round near the surface (water by the spawn): walked on along the next cardinal first */
            p->cwalk -= 1;
            a->look_mode = 1;
            a->look[0] = (float)(90.0 * p->cdir), a->look[1] = (float)PEARL_WALK_PITCH;
            a->hold = 1u << PK_FORWARD;
            pp_stuck(p, r, s, a);
            if (s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && p->clock >= p->turn_off)
                p->cdir = (p->cdir + 1) & 3, p->turn_off = p->clock + 20;
            p->ck = -1, p->cgain = p->clock;
            break;
        }
        int rc = pc_dig(v, p, r, s, a, ey, ph == PP_STAIR);
        if (ph == PP_STAIR && rc == 2 && p->chaz >= PEARL_CAVE_HAZ && r->y > PEARL_CAVE_SURFACE)
        {
            p->chaz = 0;
            p->cwalk = PEARL_CAVE_WALK;
            break;
        }
        if (ph == PP_STAIR && rc == 2 && p->chaz >= PEARL_CAVE_HAZ)
        {
            /* the staircase's floors bad in every direction (a cave or water under it): the tunnel from here */
            p->cfloor = (int)floor(r->y + 0.01), p->cfloor_set = 1, p->chaz = 0;
            pp_set(p, PP_TUNNEL);
            break;
        }
        float oy;
        if (ph == PP_TUNNEL && p->tunnel_cells >= 2 && pc_open(p, r, &oy) >= PEARL_CAVE_OPEN)
            pc_hold_begin(v, p, r,oy);
        else if (ph == PP_TUNNEL && p->tunnel_cells >= 2 && rc == 3)
            pc_hold_begin(v, p, r,(float)(90.0 * p->cdir));
        break;
    }
    case PP_HOLD:
    {
        /* lane/pearl3: back in the hold's cell (from the opening, a drop or a fight), then the watch out of it */
        if (!p->hold_set)
        {
            pp_set(p, pc_home(v, p, r));
            break;
        }
        int wb = best_weapon(s);
        double d;
        int h = pp_hostile(s, r, ey, &d);
        if (h >= 0 && s->vis[h].kind == HK_CREEPER && d < PEARL_CREEPER)
        {
            p->fx = s->vis[h].x, p->fz = s->vis[h].z;
            pp_set(p, PP_FLEE);
            break;
        }
        if (h >= 0 && (d < PEARL_HIDE_NEAR || (d < PEARL_GUARD && k < 0)))
        {
            e->fid = s->vis[h].id;
            e->flost = 0;
            e->flast = -100;
            e->age = 0;
            pp_set(p, PP_GUARD);
            break;
        }
        double ox = p->hx + 0.5 - r->x, oz = p->hz + 0.5 - r->z;
        int at = (int)floor(r->x) == p->hx && (int)floor(r->z) == p->hz && fabs(ox) < 0.3 && fabs(oz) < 0.3;
        if (!at)
        {
            if (p->pstep > 200)
            {
                pc_hold_end(p);     /* not got back to (a fall, a block in the way): a branch from here */
                break;
            }
            double yaw, pitch;
            look_at(r->x, ey, r->z, p->hx + 0.5, ey, p->hz + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
            if (py_hypot2(ox, oz) < 0.7) a->hold |= 1u << PK_SNEAK;
            pp_stuck(p, r, s, a);
            break;
        }
        if (wb >= 0 && wb != s->current) a->hotbar = wb;
        if (k >= 0)
        {
            if (p->far.e[k].scream && !p->tangry)
            {
                p->tangry = 1;
                p->book.provoked += 1;
            }
            if (p->cave_ender_te < p->hold_te) p->cs.hold_seen += 1;
            p->cave_ender_te = p->clock;
            double hd = py_hypot2(p->tx - r->x, p->tz - r->z);
            if (!p->tangry)
            {
                pp_look(a, r, ey, p->tx, p->ty + 0.5 * p->th, p->tz);     /* the stare: the box middle */
                if (++p->tstare > PEARL_STARE)
                {
                    p->bad[p->nbad++ % PEARL_BAD] = p->tid;
                    p->cs.stare_out += 1;
                    if (p->nbad > PEARL_BAD) p->nbad = PEARL_BAD;
                    p->tid = 0;
                }
            }
            /* provoked and near: its legs (the tunnel's ceiling hides its top, and the middle is the stare's) */
            else if (hd <= PEARL_NEAR) pp_look(a, r, ey, p->tx, p->ty + PEARL_CAVE_LEGS, p->tz);
            else pp_look(a, r, ey, p->tx, p->ty - PEARL_LOW, p->tz);
            if (s->mo_entity == p->tid && p->clock - p->thit >= PEARL_HIT)
            {
                a->press[PK_ATTACK] = 1;
                p->thit = p->clock;
                p->book.hits += 1;      /* lane/pearlgo's census */
            }
            /* provoked and staying far (no way to the mouth): let go after PEARL_FAR_WAIT */
            p->tfar = p->tangry && hd > 16.0 ? p->tfar + 1 : 0;
            if (p->tfar > PEARL_FAR_WAIT)
            {
                p->bad[p->nbad++ % PEARL_BAD] = p->tid;
                if (p->nbad > PEARL_BAD) p->nbad = PEARL_BAD;
                p->tid = 0, p->tfar = 0;
            }
            break;
        }
        if (p->tid)
        {
            /* out of view: its last place watched (by its legs), a provoked one looked for along the sweep */
            pp_look(a, r, ey, p->tx, p->ty + PEARL_CAVE_LEGS, p->tz);
            if (p->tangry && p->tlost >= PEARL_LOST_LOOK)
            {
                a->look[0] = p->hyaw + (float)(PEARL_CAVE_SWEEP * sin(2.0 * M_PI * p->pstep / PEARL_CAVE_SWEEP_T));
                a->look[1] = 0.0f;
            }
            if (++p->tlost > PEARL_LOST)
            {
                if (p->tangry) p->cs.lost_angry += 1;
                else p->cs.lost_calm += 1;
                p->tid = 0;
            }
            break;
        }
        if (p->ndrop && e->te - drop_te > PEARL_QUIET)
        {
            pp_set(p, PP_DROP);
            break;
        }
        if (p->clock - p->hold_te > v->pt.cave_hold && e->te - p->ender_te > PEARL_CAVE_QUIET && p->ndrop == 0)
        {
            pc_hold_end(p);
            break;
        }
        a->look_mode = 1;
        a->look[0] = p->hyaw + (float)(PEARL_CAVE_SWEEP * sin(2.0 * M_PI * p->pstep / PEARL_CAVE_SWEEP_T));
        a->look[1] = (float)(10.0 * sin(M_PI * p->pstep / PEARL_CAVE_SWEEP_T));
        break;
    }
    case PP_GUARD:
        double gd;
        int gh = pp_hostile(s, r, ey, &gd);
        if (r->health <= PEARL_LOW_HP && (gh < 0 || s->vis[gh].kind == HK_SKELETON))
        {
            /* lane/pearl2: hurt: sprinted away from a skeleton fought, or from a hit with none in view (the hide once it
             * is quiet); a zombie or a spider is fought on */
            if (gh >= 0) p->fx = s->vis[gh].x, p->fz = s->vis[gh].z;
            else p->fx = (float)(r->x - sin(r->yaw * M_PI / 180.0) * 4.0), p->fz = (float)(r->z + cos(r->yaw * M_PI / 180.0) * 4.0);
            e->fid = 0;
            pp_set(p, PP_FLEE);
            break;
        }
        if (fight(v, e, r, s, a, ey))
        {
            e->fid = 0;
            if (!want) return 1;
            pp_set(p, PP_WALK);
        }
        break;
    }
    float dbg_yaw;
    p->dbg[0] = cave ? (int)(10.0 * pc_open(p, r, &dbg_yaw)) : p->hs, p->dbg[1] = s->mo_hit, p->dbg[2] = s->mo_block, p->dbg[3] = s->mo_x - (int)floor(r->x);
    p->dbg[4] = s->mo_y - (int)floor(r->y + 1e-3), p->dbg[5] = s->mo_z - (int)floor(r->z), p->dbg[6] = (int)a->hold;
    p->dbg[7] = (int)lround(r->pitch);
    /* lane/pearl2: the look back (the hurt response above): the walk stands, the scan's sweep waits */
    if (p->look_back > 0)
    {
        p->look_back -= 1;
        if ((p->phase == PP_WALK || p->phase == PP_SCAN || p->phase == PP_TUNNEL || p->phase == PP_HOLD) &&
            p->tid == 0)
        {
            int i = PEARL_LOOK_BACK - p->look_back;
            a->look_mode = 1;
            a->look[0] = p->hurt_yaw + (i <= 4 ? 0.0f : (float)((i - 4) * PEARL_LOOK_TURN));
            a->look[1] = 5.0f;
            a->hold &= ~(1u << PK_FORWARD);
        }
    }
    p->px = r->x, p->pz = r->z;
    return 0;
}

/* lane/pearlsee: pearl_see frame, the endermen of neth_rl_far that the 64x64 frame shows: in front of the eye, within
 * PEARL_SEE_HALF degrees of the look across and up (the 70-degree square frame's own edges: the box middle's angle
 * along the frame's right and up axes) and within PEARL_SEE_R blocks; the list kept nearest first */
static void ps_frame(struct neth_rl_far *far, const struct vec_last *r, int pixel)
{
    double ex = r->x, ey = r->y + 1.62, ez = r->z;
    double yr = r->yaw * M_PI / 180.0, pr = r->pitch * M_PI / 180.0;
    double fx = -sin(yr) * cos(pr), fy = -sin(pr), fz = cos(yr) * cos(pr);    /* the look */
    double rx = -cos(yr), rz = -sin(yr);                                        /* the frame's right (level) */
    double ux = -sin(yr) * sin(pr), uy = cos(pr), uz = cos(yr) * sin(pr);       /* its up (the look's d/d(-pitch)) */
    double t = tan(PEARL_SEE_HALF * M_PI / 180.0);
    int n = 0;
    for (int i = 0; i < far->n && i < NETH_RL_FAR; ++i)
    {
        double dx = far->e[i].x - ex, dy = far->e[i].y + 0.5 * far->e[i].h - ey, dz = far->e[i].z - ez;
        double fw = dx * fx + dy * fy + dz * fz, ri = dx * rx + dz * rz, up = dx * ux + dy * uy + dz * uz;
        if (fw <= 0.0 || fabs(ri) > t * fw || fabs(up) > t * fw) continue;
        /* lane/pearlsee (2nd pass): pearl_see pixel, the box's height in the frame's rows (its depth along the look)
         * at least PEARL_SEE_PX; frame, within PEARL_SEE_R */
        if (pixel ? PEARL_SEE_ROWS * far->e[i].h / (2.0 * t * fw) < PEARL_SEE_PX
                  : far->d2[i] > (float)(PEARL_SEE_R * PEARL_SEE_R))
            continue;
        far->e[n] = far->e[i];
        far->d2[n] = far->d2[i];
        ++n;
    }
    far->n = n;
}

/* the step hook's books (with the bit off too: they change nothing the env does): the view's copy, the endermen seen,
 * each pearl's step, the deaths and what caused them; at the episode's end, the last episode's */
static void pearl_books(const struct neth_vec *v, struct vec_env *e, int id, const struct pool_result *r,
                        const struct neth_rl_state *s)
{
    struct pearl_env *p = &e->pearl;
    /* lane/pearlgate: the policy's fight (macro_start's NETH_VEC_FIGHT lock: the mine kind with a living, not a hunt,
     * not another macro's) */
    if (e->kind == M_MINE && e->fid && !e->hunt && !p->on && !e->nt.on && !e->dragon) p->fight_te = e->te;
    const struct neth_rl_far *f = neth_rl_far(v->h, id);
    if (f) p->far = *f;
    else p->far.n = 0;
    /* lane/pearlsee (3rd pass): pearl_oracle on, the privileged census on (every env's, from the next step) */
    if (MOPT(MO_PEARL_ORACLE) && !p->far.census.on) neth_rl_far_census(v->h, 1);
    /* lane/pearlhome (pearl_home): the census on too (the sky over the feet, the high ground), and land checked every
     * PH_LAND_EVERY steps while the pearl step is held; the first step on land booked */
    if (MOPT(MO_PEARL_HOME))
    {
        if (!p->far.census.on) neth_rl_far_census(v->h, 1);
        p->ph_wet_n = ph_wet(v, e, &e->last) ? p->ph_wet_n + 1 : 0;
        if (p->far.census.on && p->far.census.pl_sky && e->last.on_ground && e->last.y >= PEARL_SURF_Y)
            p->ph_sky_seen = 1;
        if (pearl_stage(v, &e->last, s) && (e->te % PH_LAND_EVERY == 0 || p->ph_land_t == 0))
        {
            p->ph_land = ph_land_now(v, e, &e->last);
            p->ph_land_t = e->te;
            if (p->ph_land && !ph_under(e, &e->last) && p->book.home_land < 0) p->book.home_land = (int32_t)e->te;
        }
        else if (!pearl_stage(v, &e->last, s))
            p->ph_land = 0;
    }
    int raw_n = p->far.n;
    if (MOPT(MO_PEARL_SEE) >= PSE_FRAME)                                /* lane/pearlsee: only what the frame shows */
        ps_frame(&p->far, &e->last, MOPT(MO_PEARL_SEE) == PSE_PIXEL);
    /* lane/pearlsee (3rd pass): pearl_oracle's trace (pearl_check --trace's dbg line, counting only): the census's
     * nearest under the sky (distance x10, tracked, line clear, in the cone, height over the eye x10), the path's points
     * and the point walked to, the view's endermen before the frame's rule */
    if (MOPT(MO_PEARL_ORACLE))
    {
        const struct neth_rl_census *c = &p->far.census;
        memset(p->dbg, 0, sizeof p->dbg);
        for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
            if (c->e[i].sky || MOPT(MO_PEARL_TARGET) == PTG_ANY)    /* (lane/pearlhome: any's, the nearest) */
            {
                p->dbg[0] = (int32_t)(10.0 * sqrt((double)c->e[i].dx * c->e[i].dx + (double)c->e[i].dy * c->e[i].dy +
                                                  (double)c->e[i].dz * c->e[i].dz));
                p->dbg[1] = c->e[i].tracked, p->dbg[2] = c->e[i].clear, p->dbg[3] = c->e[i].cone;
                p->dbg[4] = (int32_t)(10.0 * c->e[i].dy);
                break;
            }
        p->dbg[5] = p->wt.have ? p->wt.path.n : -9, p->dbg[6] = p->wt.k, p->dbg[7] = raw_n;
    }
    if (p->on && p->far.n > 0)      /* lane/pearlsee: the hunt's steps with one in view, by the nearest's distance */
    {
        int b = (int)(sqrtf(p->far.d2[0]) / 16.0f);
        p->book.vsee[b < 0 ? 0 : b > 3 ? 3 : b] += 1;
    }
    int held = (int)pearl_held(s);
    if (p->fresh)
    {
        memcpy(p->killed_by, p->far.killed_by, sizeof p->killed_by);
        p->held = held;
        p->kills = p->far.ender_kills;
        p->fresh = 0;
    }
    /* lane/pearl2: the stats screen's endermen killed (lane/pearl read stat.mobKills, every kind: a zombie killed
     * from the pillar as the target left view was booked an enderman's, its drop walked to) */
    p->kills_prev = p->kills;
    p->kills = p->far.ender_kills;
    if (p->kills > p->kills_prev) p->kill_te = e->te;
    /* lane/pnight (counting only): the book's kills are the stats screen's endermen killed (stat.killEntity.Enderman,
     * the episode's), whoever's target: the target tests above (gone from view within PEARL_KILL_STEPS, within
     * PEARL_ORACLE_KILL_T of our hit) booked 13 of seed 2's eye leg's 30; they still record the drops walked to */
    if (p->kills > p->kills_prev) p->book.killed += p->kills - p->kills_prev;
    for (int k = p->held; k < held && k < NETH_VEC_PEARLS; ++k)
        if (p->book.step[k] < 0) p->book.step[k] = (int32_t)e->te;
    int held_before = p->held;
    p->held = held;
    if (held > held_before && !(r->flags & PF_DIED)) p->cs.picks += held - held_before;
    for (int i = 0; i < p->far.n && i < NETH_RL_FAR; ++i)
    {
        int j = 0;
        while (j < p->nseen && p->seen_ids[j] != p->far.e[i].id) ++j;
        if (j == p->nseen && p->nseen < PEARL_SEEN)
        {
            p->seen_ids[p->nseen++] = p->far.e[i].id;
            p->book.seen += 1;
        }
    }
    /* the killer's stat moves a step before the step that reports the death */
    for (int k = 0; k < NETH_RL_EGGS; ++k)
        if (p->far.killed_by[k] > p->killed_by[k]) p->kb_egg = k, p->kb_te = e->te;
    const struct neth_rl_census *c = &p->far.census;
    if (c->on && !(r->flags & PF_DIED))
    {
        struct pearl_census *cs = &p->cs;
        if (cs->samples == 0) cs->biome0 = c->biome & 63;
        cs->samples += 1;
        cs->alive += c->alive;
        if (r->priv.dim == 0)   /* lane/pearlgo: the books' census sums (vecenv.h neth_vec_pearl cen) */
        {
            int64_t n64 = 0, sky64 = 0, view64 = 0;
            for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
                if (c->e[i].dx * c->e[i].dx + c->e[i].dz * c->e[i].dz < 64.0f * 64.0f)
                    n64 += 1, sky64 += c->e[i].sky, view64 += c->e[i].clear && c->e[i].cone;
            int h = p->on ? 4 : 0;
            if (p->on) p->book.hsky += c->pl_sky != 0, p->book.hbiome[c->biome & 63] += 1;
            if (p->on)      /* lane/pearlsee: what holds the line (counting only) */
                for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
                    if (c->e[i].dx * c->e[i].dx + c->e[i].dz * c->e[i].dz < 64.0f * 64.0f)
                        p->book.cen2[0] += c->e[i].tracked, p->book.cen2[1] += c->e[i].clear,
                        p->book.cen2[2] += c->e[i].clear4, p->book.cen2[3] += c->e[i].clear && c->e[i].sky,
                        p->book.cen3[0] += c->e[i].clear8, p->book.cen3[1] += c->e[i].clear12,
                        p->book.cen3[2] += c->e[i].clear16, p->book.cen3[3] += c->e[i].hi,
                        p->book.cen3[4] += c->e[i].hi10;
            if (p->on)      /* lane/pearlsee (3rd pass): where the clear lines point against the frame, by phase */
            {
                int g = p->phase == PP_WALK ? 0 : p->phase == PP_UP ? 1 : p->phase == PP_SCAN ? 2 : 3;
                double yr = e->last.yaw * M_PI / 180.0, pr = e->last.pitch * M_PI / 180.0;
                double fx = -sin(yr) * cos(pr), fy = -sin(pr), fz = cos(yr) * cos(pr);
                double rx = -cos(yr), rz = -sin(yr), ux = -sin(yr) * sin(pr), uy = cos(pr), uz = cos(yr) * sin(pr);
                double t = tan(PEARL_SEE_HALF * M_PI / 180.0);
                int any = 0, anyf = 0;
                for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
                {
                    double dx = c->e[i].dx, dy = c->e[i].dy, dz = c->e[i].dz;
                    if (!c->e[i].clear || dx * dx + dy * dy + dz * dz > 64.0 * 64.0) continue;
                    double fw = dx * fx + dy * fy + dz * fz, ri = dx * rx + dz * rz, up = dx * ux + dy * uy + dz * uz;
                    int inf = fw > 0.0 && fabs(ri) <= t * fw && fabs(up) <= t * fw;
                    any = 1, anyf |= inf;
                    p->book.cen4[g * 4 + 2] += 1, p->book.cen4[g * 4 + 3] += inf;
                    if (g == 2)
                    {
                        double off = fabs(remainder(atan2(-dx, dz) * 180.0 / M_PI - e->last.yaw, 360.0));
                        p->book.cyaw[off < 35.0 ? 0 : off < 70.0 ? 1 : off < 120.0 ? 2 : 3] += 1;
                        double ep = -atan2(dy, sqrt(dx * dx + dz * dz)) * 180.0 / M_PI - e->last.pitch;
                        p->book.cpitch[ep < -35.0 ? 0 : ep <= 35.0 ? 1 : 2] += 1;
                    }
                }
                p->book.cen4[g * 4 + 0] += 1, p->book.cen4[g * 4 + 1] += any;
                p->book.cany_f[g] += anyf;
            }
            if (c->pearl_n > 0) p->book.item_steps += 1;    /* lane/pearlsee (3rd pass): a pearl item about */
            if (p->on && c->hi_d >= 0) p->book.cen3[5] += 1, p->book.cen3[6] += c->hi_dy, p->book.cen3[7] += c->hi_d;
            for (int k = 0; k <= h; k += 4)
                p->book.cen[k] += 1, p->book.cen[k + 1] += n64, p->book.cen[k + 2] += sky64, p->book.cen[k + 3] += view64;
        }
        cs->pl_sky += c->pl_sky;
        for (int k = 0; k < 4; ++k) cs->hostile[k] += c->hostile[k];
        cs->skel_los += c->skel_los;
        int bi = c->biome & 63;
        cs->biome_steps[bi] += 1;
        double pey = r->priv.y + 1.62;
        int py8 = (int)floor(r->priv.y / 8.0);
        py8 = py8 < 0 ? 0 : py8 > 15 ? 15 : py8;
        cs->pl_y[py8] += 1;
        for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
        {
            /* lane/pearl3: where the endermen within 64 are in height, and the ones in view by the player's height */
            if (c->e[i].dx * c->e[i].dx + c->e[i].dz * c->e[i].dz >= 64.0f * 64.0f) continue;
            int y8 = (int)floor((pey + c->e[i].dy - 1.45) / 8.0);
            cs->ender_y[y8 < 0 ? 0 : y8 > 15 ? 15 : y8] += 1;
            cs->view_y[py8] += c->e[i].clear && c->e[i].cone;
        }
        for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
        {
            int b = (int)(sqrt((double)c->e[i].dx * c->e[i].dx + (double)c->e[i].dz * c->e[i].dz) / 16.0);
            if (b < 4) cs->biome_view[bi] += c->e[i].clear && c->e[i].cone, cs->biome_sky64[bi] += c->e[i].sky;
            if (b < 4)
                cs->clear0_64 += c->e[i].clear, cs->clear2_64 += c->e[i].clear2, cs->clear4_64 += c->e[i].clear4;
            if (b >= PEARL_CBANDS) b = PEARL_CBANDS - 1;
            cs->n[b] += 1;
            cs->sky[b] += c->e[i].sky;
            cs->tracked[b] += c->e[i].tracked;
            cs->clear[b] += c->e[i].clear;
            cs->view[b] += c->e[i].clear && c->e[i].cone;
            cs->skyclear[b] += c->e[i].clear && c->e[i].sky;
            cs->angry[b] += c->e[i].angry;
            cs->below[b] += c->e[i].dy < -6.0f;
            cs->level[b] += c->e[i].dy >= -6.0f && c->e[i].dy <= 6.0f;
            cs->above[b] += c->e[i].dy > 6.0f;
        }
    }
    /* lane/chain (counting only): a provoked target's lock, how it ended (the books' prov_end) */
    {
        int32_t now = p->tid && p->tangry ? p->tid : 0;
        if (p->pv_tid && now != p->pv_tid)
        {
            int64_t t = ((e->last.wtime % 24000) + 24000) % 24000;
            /* (lane/pnight: the drive's target kills, as the book's killed counted them before it took the stats') */
            int j = p->tkill != p->pv_tk ? 0
                  : (r->flags & (PF_DIED | PF_DEAD)) ? 1 : now ? 4
                  : (t >= 23000 || t < 12500) ? 2 : 3;
            p->book.prov_end[j] += 1;
        }
        p->pv_tid = now, p->pv_killed = p->book.killed, p->pv_tk = p->tkill;
    }
    if (r->flags & PF_DIED)
    {
        int cause = e->te - p->kb_te <= 5 ? p->kb_egg : 103;
        if (cause == 103)   /* the step before's too: the death step's own state may be the respawn's */
            cause = r->priv.fire > 0 || p->lfire > 0 ? 100 : r->priv.air <= 0 || p->lair <= 0 ? 101
                    : r->priv.food <= 0 || p->lfood <= 0 ? 102 : 103;
        if (c->on && p->cs.ndeath < PEARL_CDEATHS)
        {
            struct pearl_census *cs = &p->cs;
            int j = cs->ndeath++;
            cs->death[j].te = (int32_t)e->te;
            cs->death[j].cause = cause;
            cs->death[j].kind = e->kind;
            cs->death[j].on = p->on;
            cs->death[j].phase = p->phase;
            cs->death[j].up = pearl_up(p, &e->last);
            cs->death[j].sky = p->cprev.pl_sky;
            cs->death[j].hp10 = (int32_t)p->hp_ring[(e->te + 6) & 15];
            for (int k = 0; k < 4; ++k) cs->death[j].hostile[k] = p->cprev.hostile[k];
            cs->death[j].skel_los = p->cprev.skel_los;
            cs->death[j].y = (int32_t)floor(e->last.y);
        }
        if (p->book.deaths < 4) p->book.cause[p->book.deaths] = cause;
        if (p->book.deaths == 0)
        {
            /* lane/pearlhome (for lane/chain's frontier): the first death's place (the step before's feet), the
             * macro running and its phase, pearl_home's leg, the feet's wet steps in a row */
            p->book.death_x = p->prev_x, p->book.death_y = p->prev_y, p->book.death_z = p->prev_z;
            p->book.death_kind = e->kind, p->book.death_on = p->on, p->book.death_phase = p->phase;
            p->book.death_mbit = (int32_t)v->b.label[id].mbit;     /* (2nd pass: the macro by its switch bit) */
            p->book.death_leg = p->on && p->phase == PP_TRAVEL;
            p->book.death_wet = p->ph_wet_n;
            p->book.death_step = (int32_t)e->te;
            p->book.chain = held_before > held ? held_before : held;
        }
        p->book.deaths += 1;
        p->base = NAN;
        p->placed = 0;
        p->tid = 0;
        p->ndrop = 0;
    }
    memcpy(p->killed_by, p->far.killed_by, sizeof p->killed_by);
    p->lair = r->priv.air, p->lfire = r->priv.fire, p->lfood = r->priv.food;
    if (c->on && !(r->flags & PF_DIED)) p->cprev = *c;
    p->hp_ring[e->te & 15] = r->priv.health;
    if (!(r->flags & PF_DIED)) p->prev_x = (float)e->last.x, p->prev_y = (float)e->last.y, p->prev_z = (float)e->last.z;
    if (e->te >= e->eplen)
    {
        p->book.pearls = held;
        if (p->book.deaths == 0) p->book.chain = held;
        p->last = p->book;
        p->cs_last = p->cs;
    }
}

/* lane/pearlgo: the census at a result (counting only: nothing here is read by a drive). The step's driver is the
 * label of the act this result follows; the conditions are the result's state, tested in pearl_want's order */
static void pg_first(struct pearl_env *p, int k, int64_t te)
{
    if (p->book.first[k] < 0) p->book.first[k] = (int32_t)te;
}

static void pg_census(const struct neth_vec *v, struct vec_env *e, int id, const struct neth_rl_state *s)
{
    struct pearl_env *p = &e->pearl;
    const struct vec_last *r = &e->last;
    if (e->te <= 1) p->book.wtime0 = (int32_t)(((r->wtime % 24000) + 24000) % 24000);
    uint32_t mb = v->b.label[id].mbit;
    int m = mb ? 1 + __builtin_ctz(mb) : 0;
    int w;
    if (mb == NETH_VEC_PEARL || mb == NETH_VEC_PEARLCAVE) w = PG_WHY_ON;
    else if (r->flags & PF_DEAD) w = PG_WHY_DEAD;
    else if (r->dim == -1) w = PG_WHY_NETHER;
    else if (r->dim == 1) w = PG_WHY_END;
    else if (r->screen != PS_NONE) w = PG_WHY_SCREEN;
    else if (!pearl_gate(v, s)) w = PG_WHY_NOBLAZE;
    else if (pearl_held(s) >= PEARL_WANT) w = PG_WHY_FULL;
    else if (!(pearl_night(r) || pearl_cave(v, s))) w = PG_WHY_DAY;
    else if (mb) w = PG_WHY_OTHER;
    else if (pearl_busy(v, e, r, s)) w = PG_WHY_YIELD;
    else w = PG_WHY_POLICY;
    p->book.why[w] += 1;
    if (w == PG_WHY_OTHER && m < 33) p->book.whom[m] += 1;
    /* lane/pnight (counting only): a PG_WHY_POLICY step (no macro bit on the step) split: provision drove it (its
     * steps carry no bit), the hunt no candidate, or the hunt a candidate and still no macro */
    if (w == PG_WHY_POLICY)
        p->gate_book[provision_mode(v, e, r, s) ? 0 : !pearl_cand(v, e, r, s) ? 1 : 2] += 1;
    {   /* lane/pearlhome (2nd pass): the pearl items about (counting only) */
        const struct neth_rl_census *c = &p->far.census;
        int n = c->on && !(r->flags & PF_DEAD) ? c->pearl_n : 0;
        if (n > p->it_n) p->book.it_new += n - p->it_n;
        p->it_n = n;
        if (n > 0)
        {
            p->book.it_why[w] += 1;
            if (w == PG_WHY_ON) p->book.it_ph[p->phase < 12 ? p->phase : 11] += 1;
            double hd = hypot(c->pearl_x - r->x, c->pearl_z - r->z), dy = c->pearl_y - r->y;
            p->book.it_d[hd < 1.5 ? 0 : hd < 6.0 ? 1 : hd < 16.0 ? 2 : 3] += 1;
            p->book.it_dy[dy < -1.5 ? 0 : dy <= 1.5 ? 1 : 2] += 1;
            if (p->o_wk_t >= e->te - 1) p->book.it_wk[p->o_wk < 0 ? 0 : p->o_wk > 2 ? 3 : p->o_wk + 1] += 1;
            int room = 0;
            for (int k = 0; k < 36 && !room; ++k)
                room = s->inv[k].count <= 0 || (s->inv[k].item == PEARL && s->inv[k].count < 16);
            p->book.it_full += !room;
            p->book.it_wet += s->eye_water != 0;
        }
    }
    if (w == PG_WHY_ON)
    {
        static const double YB[7] = {40, 50, 56, 60, 64, 70, 80};
        int b = 0;
        while (b < 7 && r->y >= YB[b]) ++b;
        p->book.hunt_y[b] += 1;
    }
    int64_t te = e->te;
    int ow = r->dim == 0 && !(r->flags & PF_DEAD);
    if (ow && pearl_gate(v, s)) pg_first(p, PG_F_GATE, te);
    if (ow && pearl_gate(v, s) && pearl_night(r)) pg_first(p, PG_F_NIGHT, te);
    if (w == PG_WHY_ON) pg_first(p, PG_F_ON, te);
    if (w == PG_WHY_ON && r->y >= PG_SURFACE_Y) pg_first(p, PG_F_SURFACE, te);
    if (p->book.seen > 0) pg_first(p, PG_F_SEEN, te);
    if (p->book.provoked > 0) pg_first(p, PG_F_PROVOKED, te);
    if (p->book.hits > 0) pg_first(p, PG_F_HIT, te);
    if (p->book.killed > 0) pg_first(p, PG_F_KILLED, te);
    int64_t held = inv_count(s, PEARL) + inv_count(s, EYE);
    if (held > 0) pg_first(p, PG_F_PEARL, te);
    if (held >= PEARL_WANT) pg_first(p, PG_F_TWELVE, te);
}

int neth_vec_pearl_dbg(const struct neth_vec *v, int id, int32_t out[8])
{
    if (id < 0 || id >= v->n) return -1;
    memcpy(out, v->e[id].pearl.dbg, sizeof v->e[id].pearl.dbg);
    return 0;
}

int neth_vec_pearl_census(struct neth_vec *v, int on, int id, struct pearl_census *out)
{
    if (on >= 0) neth_rl_far_census(v->h, on);
    if (id < 0 || id >= v->n) return -1;
    if (out) *out = v->e[id].pearl.cs_last;
    return 0;
}

int neth_vec_pearl_gate(const struct neth_vec *v, int id, int32_t out[3])
{
    if (id < 0 || id >= v->n) return -1;
    memcpy(out, v->e[id].pearl.gate_book, 3 * sizeof *out);
    return 0;
}

int neth_vec_pearl_aim(const struct neth_vec *v, int id, int32_t out[3])
{
    if (id < 0 || id >= v->n) return -1;
    memcpy(out, v->e[id].pearl.aim_book, 3 * sizeof *out);
    return 0;
}

int neth_vec_pearl_patrol(const struct neth_vec *v, int id, int32_t out[8])
{
    if (id < 0 || id >= v->n) return -1;
    memcpy(out, v->e[id].pearl.pt_book, 8 * sizeof *out);
    return 0;
}

int neth_vec_pearl(const struct neth_vec *v, int id, int now, struct neth_vec_pearl *out)
{
    if (id < 0 || id >= v->n) return -1;
    const struct pearl_env *p = &v->e[id].pearl;
    *out = now ? p->book : p->last;
    if (now)
    {
        out->phase = p->phase;
        out->on = p->on;
        out->pearls = p->held;
        out->wtime = (int32_t)(v->e[id].last.wtime % 24000);
        out->tid = p->tid, out->angry = p->tangry, out->nfar = p->far.n, out->tdist = (float)p->tdist;
        /* now: cause[] holds the trace's stat.mobKills, the crosshair's entity at the last drive and the killed-by sum */
        out->cause[1] = p->kills;
        out->cause[2] = p->mo;
        out->cause[3] = 0;
        for (int k = 0; k < NETH_RL_EGGS; ++k) out->cause[3] += p->far.killed_by[k];
        /* lane/pground (counting only): the target's place, in view or not, the clock since the last swing */
        out->tx = (float)p->tx, out->ty = (float)p->ty, out->tz = (float)p->tz;
        out->tvis = 0;
        for (int i = 0; p->tid && i < p->far.n && i < NETH_RL_FAR; ++i)
            if (p->far.e[i].id == p->tid) out->tvis = 1;
        out->thit_age = (int32_t)(p->clock - p->thit);
    }
    return 0;
}

#ifdef PEARL_GATE_TRACE
/* A bounded diagnosis from the same saved shore node: selected policy rows in the yield arm and pearl rows in off. */
static void pearl_gate_trace(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s)
{
    if (e != v->e) return;
    static const int64_t A[10] = {109111, 109211, 109261, 109336, 109361, 109511, 110261, 110311, 111111, 111136};
    static const int64_t B[10] = {109811, 109836, 109861, 109886, 109911, 109936, 109961, 109986, 110011, 110036};
    const int64_t *times = MOPT(MO_PEARL_STRIP_YIELD) ? B : A;
    int selected = 0;
    for (int k = 0; k < 10; ++k)
        if (llabs(r->wtime + 1 - times[k]) <= 1) selected = 1;
    if (!selected) return;
    const struct neth_rl_far *f = neth_rl_far(v->h, 0);
    const struct pearl_env *p = &e->pearl;
    int64_t tod = ((r->wtime % 24000) + 24000) % 24000;
    int macro = !!(v->macros & NETH_VEC_PEARL), dim = r->dim == 0, alive = !(r->flags & PF_DEAD);
    int screen = r->screen == PS_NONE, night = pearl_night(r), cave = pearl_cave(v, s);
    int held = (int)pearl_held(s), blaze = pearl_gate(v, s);
    int want = pearl_want(v, r, s), home = ph_want(v, e, r, s);
    int back = (pearl_up(p, r) || p->hs || p->climb) && alive;
    int cand = pearl_cand(v, e, r, s), busy = pearl_busy(v, e, r, s);
    int need = arm_need(v, e, s), equip = arm_equip_want(e, s), tier = arm_tier(v, s);
    int plan = craft_plan(v, e, r, s), land = !MOPT(MO_PEARL_HOME) || p->ph_land;
    int armor = MOPT(MO_ARMOR_KIT) && dim && tier >= 2 && (need > 0 || equip >= 0) && land;
    int sword = MOPT(MO_SWORD_KIT) == 2 && dim && land && plan == C_ISWORD;
    int raw = raw_meat(s), cooked = food_cooked_meat(s), fuel = inv_count(s, COAL) > 0 || inv_count(s, PLANKS) >= 2;
    int finish_cook = cooked > 0 && raw >= FOOD_COOK_N && (inv_count(s, FURNACE) > 0 || near_furnace(e, r));
    int cook = MOPT(MO_FOOD_COOK) == FK_PEARL && food_pearl_stage(v, r, s) && land &&
               ((food_day(r) && raw >= FOOD_PEARL_COOK_START) || finish_cook) && fuel &&
               (plan == C_SMELT || plan == C_FURNACE);
    int scout = scout_want(v, e, r, s), hunt = hunt_want(v, e, s), cook_want_now = cook_want(v, s);
    int trips = MOPT(MO_FOOD_TRIPS) == FT_PEARL && food_pearl_stage(v, r, s) && food_day(r) && land &&
                (r->food < 18 || cooked < FOOD_PEARL_COOKED) &&
                (scout || hunt >= 0 || (cook_want_now && (plan == C_FURNACE || plan == C_SMELT)));
    int stronghold = sh_owns(v, e, r), strip = !!((v->macros & NETH_VEC_STRIP2) && e->st3_out &&
                                                  !shore_night_yield(v, e, r, s));
    int fight = e->te - p->fight_te < PEARL_YIELD_FIGHT;
    int craft = plan == C_POWDER || plan == C_EYE;
    double ox = 0, oz = 0, od = -1, ody = 0;
    int clear = 0, target = pp_oracle(p, r, &ox, &oz, &od, &ody, &clear, MOPT(MO_PEARL_TARGET) == PTG_ANY);
    double home_d = p->ph_ok == 1 ? hypot(p->ph_x - r->x, p->ph_z - r->z) : -1;
    const char *first = e->pearl.on ? "continuing" : !macro ? "macro" : !dim ? "dimension" : !alive ? "death" :
                        !screen ? "screen" : !night && !cave && !home && !back ? "time" : held >= 12 ? "pearls" :
                        !blaze ? "blaze" : !cand ? "candidate" : armor ? "armor" : sword ? "sword" : cook ? "cook" :
                        trips ? "trips" : e->bp.site ? "portal" : stronghold ? "stronghold" : strip ? "strip" :
                        fight ? "fight" : craft ? "craft" : busy ? "busy_other" : "ready";
    fprintf(stderr, "PGATE world=%lld pos=%.2f,%.2f,%.2f first=%s macro=%d dim=%d alive=%d screen=%d tod=%lld night=%d cave=%d held=%d blaze=%d want=%d home=%d back=%d cand=%d busy=%d armorneed=%d equip=%d armorbar=%d macropts=%d armorbusy=%d sword=%d cook=%d trips=%d scout=%d hunt=%d foodcook=%d portal=%d stronghold=%d strip=%d fight=%d craft=%d prov=%d fs_home=%d phland=%d phok=%d homed=%.1f target=%d target_d=%.1f target_dy=%.1f target_clear=%d census_n=%d oracle=%d reachable=%d pearlarmor=%d foodnight=%d meat=%d cooked=%d food=%d hp=%.1f phase=%d on=%d\n",
            (long long)r->wtime, r->x, r->y, r->z, first, macro, dim, alive, screen, (long long)tod, night, cave,
            held, blaze, want, home, back, cand, busy, need, equip, f ? f->armor_worn : -1, e->arm.points, armor,
            sword, cook, trips, scout, hunt, cook_want_now, e->bp.site, stronghold, strip, fight, craft,
            provision_mode(v, e, r, s), e->fs_home, p->ph_land, p->ph_ok, home_d, target, od, ody, clear,
            p->far.census.n, MOPT(MO_PEARL_ORACLE), MOPT(MO_PEARL_REACHABLE), MOPT(MO_PEARL_ARMOR),
            MOPT(MO_FOOD_NIGHT), (int)meat_count(s), cooked, r->food, r->health, p->phase, p->on);
}
#endif

#ifdef PEARL_CENSUS_TRACE
static void pearl_census_trace(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (e != v->e || r->dim != 0 || e->steps % 1000 != 0) return;
    int64_t tod = ((r->wtime % 24000) + 24000) % 24000;
    if (tod < 13000 || tod >= 23000) return;
    const struct neth_rl_far *f = neth_rl_far(v->h, 0);
    if (f == NULL) return;
    const struct neth_rl_census *c = &f->census;
    int n64 = 0, n128 = 0;
    for (int i = 0; i < c->n && i < NETH_RL_CENSUS; ++i)
    {
        double d2 = (double)c->e[i].dx * c->e[i].dx + (double)c->e[i].dz * c->e[i].dz;
        n64 += d2 <= 64.0 * 64.0;
        n128 += d2 <= 128.0 * 128.0;
    }
    int monsters = -1, cap = -1, chunks = -1;
    shoracle_monster_count_cap(v->p, 0, &monsters, &cap, &chunks);
    fprintf(stderr, "PCENSUS step=%lld world=%lld tod=%lld pos=%.2f,%.2f,%.2f food=%d hp=%.1f on=%d phase=%d census_on=%d endermen_alive=%d within64=%d within128=%d list_n=%d monsters=%d cap=%d eligible_chunks=%d nearby_skel=%d zombie=%d spider=%d creeper=%d biome=%d\n",
            (long long)e->steps, (long long)r->wtime, (long long)tod, r->x, r->y, r->z, r->food, r->health,
            e->pearl.on, e->pearl.phase, c->on, c->alive, n64, n128, c->n, monsters, cap, chunks,
            c->hostile[0], c->hostile[1], c->hostile[2], c->hostile[3], c->biome);
}
#endif

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static int pearl_m_cand(struct mac_step *c) { return pearl_cand(c->v, c->e, c->r, c->s); }

static int pearl_m_start(struct mac_step *c) { return pearl_start(c->v, c->e, c->r, c->s); }   /* at night, before a hunt */

static int pearl_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->pearl.on) return -1;
    /* lane/chain (pearl_trim): the pack's surplus out, by day or night, on the ground and off a pillar (strip2's
     * errand; netherrack thrown too): a full pack with every pearl made an eye takes no dropped pearl (lane/pearlhome:
     * 98% of the item-near steps at e43l node 412) */
    const struct neth_vec *v = c->v;
    if (MOPT(MO_PEARL_TRIM) && (v->macros & NETH_VEC_STRIP2))
    {
        if (c->e->stf) return st_errand(v, c->e, c->r, c->s, a);
        if (c->r->on_ground && !pearl_up(&c->e->pearl, c->r) && !c->e->pearl.hs &&
            st_drop_start(v, c->e, c->r, c->s, a))
            return 0;
    }
    return pearl_drive(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t pearl_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->pearl.on ? NETH_VEC_PEARL : 0; }

static void pearl_m_end(struct vec_env *e)
{
    struct pearl_env *p = &e->pearl;
    if (p->on && p->phase == PP_UP && !p->upend) pp_upbook(p, UX_END), p->upend = 1;    /* lane/pearlsee (books) */
    p->on = 0;
}

/* the make: the macro's settings (lane/pearl3, neth_vec_pearl_tune; lane/macroopts: its two either/ors are options) */
static void pearl_m_make(struct neth_vec *v)
{
    pearl_tune_default(&v->pt);
    v->pt.zombie_pillar = MOPT(MO_PEARL_ZOMBIE_PILLAR);
    v->pt.drop_calm = MOPT(MO_PEARL_DROP_CALM);
}

static void pearl_m_books(struct mac_books *b)
{
    if (b->at != MB_RESULT) return;
    const struct neth_vec *v = b->v;
    struct vec_env *e = b->e;
    int depth_due = (v->macros & NETH_VEC_PEARL) && pearl_depth_due(v, e, &e->last, b->s);   /* lane/macroperf */
    if (depth_due) neth_rl_far_depth_now(v->h, b->id);
    if (v->macros & NETH_VEC_PEARL) pg_census(v, e, b->id, b->s);    /* lane/pearlgo (counting only) */
    pearl_books(v, e, b->id, b->r, b->s);
#ifdef PEARL_DEPTH_POISON
    /* a VARIANT build's proof that no row is read stale: the rows not made this step are junk (the digest moves on a read) */
    if (!depth_due)
        for (int i = 0; i < 2 * NETH_RL_DEPTH; ++i) e->pearl.far.depth[i / NETH_RL_DEPTH][i % NETH_RL_DEPTH] = 1000.0f + (float)i;
#else
    (void)depth_due;
#endif
}

static const struct mac_switch PEARL_SW[] = {{"pearl", NETH_VEC_PEARL}, {"pearlcave", NETH_VEC_PEARLCAVE}, {NULL, 0}};

static const struct vec_macro MAC_PEARL = {
    .name = "pearl", .bit = NETH_VEC_PEARL, .sw = PEARL_SW, .cand = pearl_m_cand, .start = pearl_m_start, .drive = pearl_m_drive,
    .label = pearl_m_label, .end = pearl_m_end, .reset = pearl_reset, .books = pearl_m_books, .make = pearl_m_make,
};
