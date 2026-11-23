/* lane/nether: NETH_VEC_NETHER, the Nether from just through the portal to back in the overworld with 7 blaze rods
 * (vecenv.h). Included by macros.c (vecenv.c's statics: the env, the look helpers, the inventory counts, best_tool,
 * best_weapon, fight), not compiled alone.
 *
 * What a speedrunner does in the Nether, from what the player sees (the cast's seen record for nether brick, the
 * livings in view, the crosshair, and netherview.h's look at the client world for the fortress's fences, its spawner,
 * the portal and lava: never a block behind a wall) and their own state as F3 shows it:
 *
 *   arrival   a look round (NT_SPIN steps of 30 degrees, a ring 10 up and one 35 down); the portal block seen in it
 *             within NT_PORTAL_D (face on, or edge on behind its obsidian frame), the lowest of its column, remembered
 *   search    a heading held along the z axis (the arrival's facing's sign: fortresses lie along z) for NT_LEG steps,
 *             then a side step of NT_XLEG along x, then z again (a sweep); walked cell by cell toward a point 64
 *             ahead, stairs toward feet at NT_Y: the next column's head and feet cleared (dug with the best tool), its
 *             floor judged first, a gap bridged with cobblestone where the crosshair's face puts the block, a drop of
 *             at most NT_DROP walked off; a direction judged bad (lava in or beside the column, fire, the portal,
 *             bedrock, obsidian, an unloaded cell, NT_CELL_AGE steps without progress) turns a quarter to the
 *             sweep's side; lava come into the player's own cells sealed with cobblestone, or backed off from when
 *             it is at head height; a sealed cell never dug again; a look round each leg
 *   fortress  nether brick or a fence in view: gone to; then the search goes on at the fortress's height
 *   camp      the spawner in view: gone to within NT_CAMP_D; there the look turns round until a blaze is in view
 *   fights    a blaze in view within NT_FIGHT_D, or any other hostile but a pigman (never hit) and a ghast within
 *             NT_MOB_D: NETH_VEC_FIGHT's lock (fight()), the walk crouched (no edge walked off); a blaze killed, its
 *             drop walked to (crouched, NT_PICK_AGE steps)
 *   home      NT_RODS rods held: the walker to the remembered portal cell, then still in it until the overworld; no
 *             portal remembered (a start that did not see it): the macro gives the step back (NT_OFF at a time)
 *
 * It starts in the Nether whenever the player is alive with no screen open, after the screen, the surface, the lava
 * escape and eating (which end it for their step), and gives the step back in the overworld (the next lane's), or for
 * NT_OFF steps when the walker finds no way on (the policy's turn; the search turns a quarter). Every decision reads
 * the player's state, the crosshair, the liquid look, the livings in view and the scans' seen cells. */

#include "../../engine/blocks.h"

/* lane/macroopts: the either/ors that were -D switches are run-file options (vecenv.c MOPTS), read where v is */
#define NT_FOOTING MOPT(MO_NT_FOOTING)       /* lane/nether4: the inventory opened only with footing under the whole box */
#define NT_BRICK_PICK MOPT(MO_NT_BRICK_PICK) /* lane/nether4: nether brick dug with the best pickaxe (nt_tool), 0 by hand */
#if defined(NT_TRACE) && !defined(NT_TRACE_FROM)
#define NT_TRACE_FROM 0         /* lane/rod: -DNT_TRACE=ENV traces every step unless these bound it */
#endif
#if defined(NT_TRACE) && !defined(NT_TRACE_TO)
#define NT_TRACE_TO 1000000000
#endif
#ifndef NT_RODS
#define NT_RODS (7 - MOPT(MO_NETHER_RODS))  /* the rods wanted (12 eyes' powder; a test build of the way home sets 0;
                                             * lane/pearlgo's nether_rods: 7 or 6) */
#endif
#ifndef NT_SCAN_EVERY
#define NT_SCAN_EVERY 10        /* steps between view scans */
#endif
#define NT_SCAN_SPIN 3          /* in a look round (lane/nether2: 6 found the g8 spawner in 2 of 32 envs against 27) */
#ifndef NT_SCAN_R
#define NT_SCAN_R 20            /* the scan's radius (blocks; lane/nether2: 32 every 30 steps found no more fortress from
                                 * g7 and cost the same) */
#endif
#define NT_SPIN 36              /* a look round: steps of 30 degrees, a ring 10 up, one 35 down, one 50 up (lane/nether2) */
#ifndef NT_LEG_D
#define NT_LEG_D 112.0          /* lane/nether2: a z leg's length (blocks along its heading; 600 steps before) */
#endif
#ifndef NT_XLEG_D
#define NT_XLEG_D 80.0          /* lane/nether2: an x leg's (the sweep's side step; 300 steps before) */
#endif
#define NT_LEG_MAX 3000         /* lane/nether2: a leg's steps at most */
#ifndef NT_Y
#define NT_Y (32 + 8 * MOPT(MO_NT_SEARCH_Y))   /* the search's feet height: the lava sea's top is 31, the open caverns
                                 * over it (lane/nether2: 64 before, the stair up to it dug into the lava over the head; a
                                 * search at 56 and 70 by pillars found seed 42's fortresses in 0 to 8 of 32 envs and lost
                                 * 13 to 21 in lava). lane/rod's nether_search_y: 32, 40 or 48 (46 of 94 deaths from the
                                 * harvested starts were in one tunnel at 32 that lava flooded) */
#endif
#define NT_PILLAR_OFF 40        /* lane/nether2: steps before another pillar after one given up */
#define NT_PILLAR_NEAR 4.0      /* lane/nether2: fortress brick over the player within this (horizontally): pillared to */
#define NT_DROP 3               /* a missing floor stepped down to a floor at most this far below (no fall damage) */
#ifndef NT_CAMP_D
#define NT_CAMP_D 5.0           /* at the spawner: within this of its cell (horizontally) */
#endif
#ifndef NT_FIGHT_D
#define NT_FIGHT_D 6.0          /* a blaze in view nearer than this fought (lane/nether2: 14 before, chased into the open:
                                 * from g8 7 rods in 9 and 10 of 32 envs, 26 and 25 killed by blazes; at 6 the blazes come
                                 * to the player: 32 and 32, 3 and 2) */
#endif
#define NT_FAR_SP MOPT(MO_NT_FAR_SPAWNER)  /* lane/nether5: nt_observe's far spawner scan */
#ifndef NT_FAR_SP_R
#define NT_FAR_SP_R 40          /* lane/nether5: its radius (blocks; the client world's render distance 4 is 64) */
#endif
#define NT_FAR_SP_EVERY 30      /* lane/nether5: steps between far scans */
#define NT_FAR_SP_SPIN 4        /* lane/nether5: in a look round (30 degrees a step: every 120 degrees, the cone 150) */
#define NT_HOOD_ANY MOPT(MO_NT_HOOD_ANY)    /* lane/nether5: the hood anywhere on the fortress */
#define NT_FRONTIER MOPT(MO_NT_FRONTIER) /* lane/nether5: the fortress search's remembered floors */
#define NT_FRONT_SEP 3          /* lane/nether5: remembered floors at least this far apart (x plus z) */
#define NT_BLAZE_FAR MOPT(MO_NT_BLAZE_FAR) /* lane/nether5: blazes fought from NT_FIGHT_FAR_D while searching */
#define NT_FIGHT_FAR_D 12.0     /* lane/nether5: the search's blaze fight distance (the camp's stays NT_FIGHT_D) */
#define NT_SHY MOPT(MO_NT_SHY)  /* lane/nether5: the shy from a far blaze while searching */
#define NT_SHY_BACK 6           /* lane/nether5: cells of the way walked backed a time */
#define NT_SHY_CLEAR 40         /* lane/nether5: steps out of the blaze's view (looking at where it was) to go on */
#define NT_SHY_MAX 600          /* lane/nether5: steps of one shy at most */
#define NT_SHY_OFF 200          /* lane/nether5: steps before another */
#define NT_FORT_UP MOPT(MO_NT_FORT_UP)   /* lane/nether5: the fortress search's floors over the feet first */
#define NT_UP_GAIN 3.0          /* lane/nether5: a floor's score a block of rise (nether_fort_up) */
#define NT_LAVA_FLOOR MOPT(MO_NT_LAVA_FLOOR)   /* lane/nether5: lava on the fortress's bridge a goal */
#define NT_COVER_SEARCH MOPT(MO_NT_COVER_SEARCH)   /* lane/nether5: the blaze cover while searching the fortress */
#define NT_CAMP_GIVEUP_ON MOPT(MO_NT_CAMP_GIVEUP)  /* lane/nether5: a camp with no spawner given up */
#define NT_CAMP_GIVEUP 2400     /* lane/nether5: steps at a camp with no spawner and no kill before it is given up */
#define NT_LAVA_FLOOR_DY 1      /* lane/nether5: the lava's top (where the player stands on the fill) this near the
                                 * fortress's height (fort_ty): a bridge's, not the lava sea's (31) */
#define NT_SKEL_D 16.0       /* lane/nether5: a skeleton in view nearer than this fought (walked to, crouched) */
#define NT_MOB_D 4.0           /* another hostile (not a pigman, not a ghast) nearer than this fought */
#ifndef NT_FIGHT_STUCK
#define NT_FIGHT_STUCK 40       /* lane/nether3: a blaze locked out of reach (over NT_REACH) with the player not moving
                                 * this many steps: the lock dropped (gold-g7's spawner: 400 steps held at 4.9 blocks,
                                 * no hit, then three blazes burned the player) */
#endif
#ifndef NT_EYE_REACH
#define NT_EYE_REACH 3.3        /* lane/rods2 (nether_eye_strike reach): a blaze's eye this near, its box in NT_REACH */
#endif
#define NT_MOUTH_D 4.5          /* lane/rods2 (nether_mouth_cover): a blaze's eye within this, past NT_EYE_REACH */
#define NT_MOUTH_HOLD 100       /* ... the steps the camp's re-dig of the shut mouth is held */
#ifndef NT_REACH
#define NT_REACH 3.0            /* lane/nether3: the hit's reach (the client's entity mouseover) */
#endif
#define NT_FIGHT_OFF 300        /* lane/nether3: after a dropped lock, blazes fought only within NT_REACH this long */
#ifndef NT_LOW_HP
#define NT_LOW_HP 8.0f          /* lane/nether2: at the blazes, health at or under this: back along the way to heal */
#endif
#define NT_HEAL_HP 18.0f        /* lane/nether2: healed to this: back to the camp, or on the way */
#ifndef NT_REST_HP
#define NT_REST_HP 6.0f         /* lane/nether2: away from the blazes, at or under this: still until healed (lane/nether3:
                                 * -1 in a test build: never, for lane/obsidian's arrival at 1 HP and no food) */
#endif
#ifndef NT_RETREAT
#define NT_RETREAT 30           /* lane/nether2: the cells backed off along the way (lane/nether3: 10 before; at gold-g7's
                                 * spawner 10 cells healed in the blazes' fire, 13 blocks off: died there, no rod; 30 with
                                 * the dropped lock: a rod in every env reaching it, 60: none) */
#endif
#define NT_PICK_AGE 60          /* steps walking to a kill's drop */
#define NT_SEEK_NEAR 4.0        /* lane/rod (nether_blaze_seek): blocks across from the blaze's place: there */
#define NT_SEEK_LOST 200        /* steps out of view before the seek is given up */
#define NT_SEEK_MAX 1500        /* steps a seek at most */
#define NT_OFF 40               /* steps the macro is off after the walker found no way on */
#define NT_PORTAL_D 10.0        /* the arrival's portal: seen within this in the first look round */
#define NT_CELL_AGE 80         /* steps on one cell without progress: that direction judged bad */
#define NT_PLACE_TRIES 8        /* steps trying to bridge one floor cell */
#define NT_BACK 8               /* steps backing off from lava at head height (about two blocks) */
#define NT_BACKS 2              /* back-offs from one cell before sealing instead */
#define NT_SEALS 16            /* sealed cells remembered (nether.h's ring) */
#define NT_STILL 8             /* steps of a walk without moving before a jump */
#define NT_LAVA_BURN 300       /* a lava touch sets the player burning 15 s (the books: those flames are the lava's) */
#define NT_PILLAR_AGE 60        /* lane/nether2: steps for one block of pillar before it is given up */
#define NT_PORTAL_MID 0.15      /* lane/nether2: home, within this of the portal cell's middle (horizontally): still */
#define NT_BRIDGE_TRIES 40      /* lane/nether2: steps to bridge one floor cell (the brink, then the look and the place) */
#define NT_BRINK 0.12           /* lane/nether2: the middle this far past the block's edge before the look back down */

/* lane/nether3: the far view and the search by it */
#ifndef NT_FAN_R
#define NT_FAN_R 64             /* the fan's range (blocks): the camera's far view */
#endif
#define NT_FAN_YAW 7            /* rays across, over NT_FAN_HFOV degrees */
#define NT_FAN_PITCH 4          /* rays up and down, over NT_FAN_VFOV */
#define NT_FAN_HFOV 90.0f
#define NT_FAN_VFOV 50.0f
#define NT_FAN_EVERY 15         /* steps between fans while walking (every NT_FAN_SPIN in a look round) */
#define NT_FAN_SPIN 2
#define NT_GOAL_MIN 8.0         /* a goal at least this far (horizontally) */
#define NT_GOAL_MAX 600         /* steps walking to one goal at most */
#define NT_GOAL_NEAR 1.5        /* a goal within this: reached */
#define NT_VISITED 5            /* a floor within this of a cell walked (a crumb) scores NT_VISITED_PEN less */
#define NT_VISITED_PEN 30.0
#define NT_LAVA_PEN 10.0        /* a goal on lava's surface (a bridge all the way) scores this less, and its distance more */
#define NT_GOAL_FAILS 2         /* goals given up in a row: the bearing turns NT_BEAR_TURN */
#define NT_BEAR_TURN 90.0f
#define NT_FORT_NEAR 16.0       /* a fortress block seen farther than this (horizontally): travelled to as the search */
#define NT_SEA_TOP 31           /* the lava sea's top: a fortress block under it is seen only through lava (not kept) */
#define NT_CLIMB_NEAR 10.0      /* the highest fortress block seen within this (horizontally): climbed to */
#define NT_CLIMB_MAX 56         /* blocks of climb at most */
#define NT_CLIMB_OFF 300        /* steps without a climb after one given up */
#define NT_CLIMB_LOOK 8         /* a look round up every this many blocks of pillar, and at the top seen */
#define NT_CLIMB_LOOKS 12       /* its steps (30 degrees each, 60 and 25 up in turn) */
#define NT_FORT_LOOKS 2         /* look rounds at the fortress's foot before its height is taken as the fortress's */
#define NT_CLIMB_FAILS 5        /* climbs tried again toward a deck seen more than 3 over the player */
#define NT_FORT_GOALS 600       /* steps of the far search's goals toward a fortress whose straight way was given up */
#define NT_SAFE_DIG MOPT(MO_NT_SAFE_DIG)   /* lane/rod: no block dug that the player's box stands on (nt_walk) */
#define NT_DEEP_D 4             /* lane/rod (nether_safe_dig deep): the cells under a dug floor looked at for a landing */
#define NT_FLOOR_KEEP MOPT(MO_NT_FLOOR_KEEP)  /* lane/legs: nt_deep's rule on every dig under the feet, not the walker's alone */
#define NT_DIG_REFILL MOPT(MO_NT_DIG_REFILL)  /* lane/nleg: a dug cell lit by lava beside it filled again (nt_walk) */
#define NT_DROP_KEEP MOPT(MO_NT_DROP_KEEP)  /* lane/nleg: the tunnel's step down into an open cell over a drop refused */
#define NT_DIG_AIM MOPT(MO_NT_DIG_AIM) /* lane/nleg: a dig whose crosshair lands on another block aims at an open face */
#define NT_FACE_IN 0.02         /* lane/nleg (nether_dig_aim): the aim point this far inside the face */
#define NT_CAMP_SIDE MOPT(MO_NT_CAMP_SIDE)  /* lane/nleg: the camp's cell this many cells beside the spawner's column */
#define NT_CAMP_SIDE_NEAR 0.45  /* lane/nleg (nether_camp_side): the camp's walk ends this near the side cell's centre */
#define NT_FORT_PATH MOPT(MO_NT_FORT_PATH)  /* lane/nleg: a stalled fortress walk takes the oracle's tunnel path */
#define NT_FORT_STALL 400       /* lane/nleg (nether_fort_path): fortress-phase steps short of the kept brick first */
#define NT_BRINK_FLOOR MOPT(MO_NT_BRINK_FLOOR)  /* lane/nleg: the bridge's own centre's floor placed first (nt_walk) */
#define NT_BRINK_IN 0.01        /* lane/nleg (nether_brink_floor): the aim point this far inside the neighbour's face */
#define NT_BRINK_Y 0.25         /* lane/nleg (nether_brink_floor): and this high in its cell */
#define NT_BRINK_D 0.06         /* lane/nleg (nether_brink_floor): the eye at least this far off the face, else back */
static int nt_lava_glow(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z);
static int nt_deep(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z);
#define NT_SEAL_AIM MOPT(MO_NT_SEAL_AIM)  /* lane/rod: the seal aimed at a solid face bordering the lava cell */
#define NT_SPREAD MOPT(MO_NT_SPREAD)  /* lane/rod: the search's side and bearing from the env's own draw */
#define NT_HOOD_HIT_OFF 400    /* lane/rod (nether_hood_hit): steps without a hood after one the player was hurt in */
#define NT_SURF MOPT(MO_NT_SEARCH)  /* lane/rod: nether_search surface (1): the search walks the netherrack as it lies */
#define NT_SURF_GAP 6          /* lane/rod (surface): cells bridged in a row at most on the search's way */
#define NT_SURF_SCAN 600       /* lane/rod (surface): steps between scan pillars */
#define NT_SWEEP_BACK (MOPT(MO_NT_SWEEP) == 1) /* lane/rod: nether_sweep back: each z leg the other way, longer */
#define NT_BACK_D 48.0         /* lane/rod (nether_sweep back): the first z leg; leg k is k + 1 times it */
#define NT_BACK_XD 32.0        /* lane/rod (nether_sweep back): the side step between z legs */
#define NT_SURF_STUCK 300     /* lane/rod (surface): steps without a block gained along the leg: the next leg */
#define NT_SURF_PIL 3        /* lane/rod (surface): a scan pillar's blocks (a drop of 3 walks off it unhurt) */
#define NT_FAN MOPT(MO_NT_FAN)     /* 0: no far view, the lava veto as lane/nether2's */
#define NT_GOALS MOPT(MO_NT_GOALS) /* 1: the search by goals the far view saw (lane/nether3's first try; 0 the legs) */
#define NT_ORACLE MOPT(MO_NT_ORACLE)  /* lane/rod: nether_oracle, PRIVILEGED (netherview_oracle): 1 fortress, 2 + spawner,
                                       * 3 + home; off by default, never in the benchmark's numbers */
#define NT_OR_EVERY 20          /* lane/rod (nether_oracle): steps between reads of the fortress map */
#define NT_OR_BRICK_D 40.0      /* lane/rod (nether_oracle): the throne's brick taken as the fortress seen within this */
#define NT_OR_BRICK_DY 6.0      /* ... and within this of the feet's height */
#define NT_OR_LOCK_R 16.0       /* lane/rod (nether_oracle_lock): blazes this near the eye locked on */
#define NT_OR_LOCK_CLOSE 5.0    /* ... closed on (crouched) within this */
#define NT_OR_LOCK_SP 14.0      /* ... while within this of the spawner (horizontally; or3i: 8 never met, 10 off) */
#define NT_DEP_QUIET 40         /* lane/rod (nether_oracle tunnel): steps with no fire, no blaze in view, before leaving */
#define NT_DIG_SIDE 2           /* lane/rod (nether_oracle tunnel): a sidestep's cells (lava, a drop, bedrock ahead) */
#define NT_DIG_GAIN 400         /* ... steps with no gain toward the portal before the first move is dropped */
#define NT_DIG_Y 45            /* ... the tunnel's height at least, away from the lava sea (y 31) */
#define NT_FAR_LOW_Y 55        /* an enclosed level near seed-4's nearest fortress piece (measured at y 55) */
#define NT_DIG_NEAR 16          /* ... until this near the portal (x and z steps), then down to it */
#define NT_DIG_STILL 20        /* ... walk steps without moving before a jump, uncrouched */
#define NT_DIG_STUCK 200       /* ... steps at one cell before a sidestep */
#define NT_DEP_SINK 4          /* ... then this far down under the feet before the tunnel */
#define NT_DEP_SINK_MAX 800     /* ... in this many steps at most */
#define NT_DEP_MAX 1200        /* ... the wait's steps at most */
#define NT_HOME_PIN 120        /* lane/rod (nether_oracle home): steps in the portal's cell before a step out */
#define NT_HOME_OUT_R 3         /* ... to the nearest cell within this that is no portal's */
#define NT_HOME_POUT 60        /* ... and the step out's steps */
#define NT_OR_HOME_NEAR 1.2    /* lane/rod (nether_oracle home): the planner's walk ends this near the portal's cell */
#define NT_OR_UNDER 3          /* lane/rod (nether_oracle spawner): the walk's end this far under the spawner's cell */
#define NT_OR_UNDER_PITCH -60.0f    /* ... the look's pitch there, turning (up through the hole) */
#define NT_OR_UNDER_NEAR 1.0   /* ... within this (horizontally) */
#define NT_OR_SKEL_R 4.0       /* ... a skeleton this near the eye struck on the oracle's way */
#define NT_OR_LOCK_DRY 100     /* ... off the spawner, steps locked with no strike before the walk takes it */
#define NT_OR_LOCK_REST 0       /* ... and steps the walk has then (0: never; or3n's 300 walked in and burnt) */
#define NT_OR_ROD_R 10.0       /* ... a rod's drop this near walked to, no blaze within reach */
#define NT_OPATH_OFF 60        /* lane/rod (nether_oracle spawner): steps off the path before it is made again */
#define NT_OPATH_AGE 600        /* ... and steps a path is followed at most */
#define NT_CHAMBER_OPATH_AGE 20000 /* the optional long return tunnel keeps a valid path while cells are dug */
#define NT_OPATH_BUILD_MAX 200  /* ... steps building one cell before a new path */
#define NT_OPATH_RETRY 100     /* ... and steps after none was found before the next try (the walker meanwhile) */
#define NT_OR_SP_NEAR 3.0       /* ... the walk's end within this of the spawner (horizontally) */
#define NT_OR_PORTAL_R 6     /* lane/rod (nether_oracle home): the arrival's portal block looked for this far */
struct vec_env;
static int nt_open(int b);
static int nt_floor_ok(int b);
static int nt_cover_place(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int tx, int ty, int tz, int depth,
                          int material);
static int nt_far_enclose(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey,
                          int x, int y, int z, int dx, int dz);
static int nt_far_supply(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, int material);
static int nt_block(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z);
static int nt_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey, int tx, int ty, int tz, double near, int prefer);
static int nt_search_fan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, double ey);

static const int NDX[4] = {0, -1, 0, 1}, NDZ[4] = {1, 0, -1, 0};   /* yaw 0, 90, 180, 270 */

static void nt_reset(struct vec_env *e)
{
    memset(&e->nt, 0, sizeof e->nt);
    e->chamber_on = e->chamber_x = e->chamber_y = e->chamber_z = 0;
    e->exit_cover_done = 0, e->exit_cover_at = 0;
    e->nt.t_arrive = e->nt.t_found = e->nt.t_spawner = e->nt.t_rod1 = e->nt.t_rods7 = e->nt.t_home = -1;
    e->nt.t_death = -1;
    e->nt.t_fortin = e->nt.t_bseen = e->nt.t_bhit = e->nt.t_bkill = -1;   /* lane/rod */
    e->nt.home_k = -1;
    e->nt.t_or_piece = e->nt.t_or_sp = e->nt.or_drop_at = -1;     /* lane/rod (nether_oracle) */
    e->nt.warr = -1;
    e->nt.heal_k = -1;
    e->nt.scan_at = -1000;
    e->nt.wk = -1;
    e->nt.lhp = 20.0f;
    e->nt.cause_last = NC_OTHER;
    for (int k = 0; k < NV_KINDS; ++k) e->nt.hit[k].d = -1.0f;
}

static int nt_id(const struct neth_vec *v, const struct vec_env *e) { return (int)(e - v->e); }

/* lane/nether4: the hotbar's tool for a block: the macros' best_tool, and for the fortress's nether brick, fence and
 * stairs (112, 113, 114: not in its PICKY table) the best pickaxe. By hand nether brick is 200 ticks and drops nothing,
 * and any look away or release resets it: 14 of 32 gold-g7 envs held one brick dig from step 7,377 to the end */
static int nt_tool(const struct neth_vec *v, const struct neth_rl_state *s, int block)
{
    if (!NT_BRICK_PICK || (block != 112 && block != 113 && block != 114)) return best_tool(&v->T, s, block);
    int best = -1, bt = 0;
    for (int i = 0; i < 9; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
        if (t > bt) best = i, bt = t;
    }
    return best;
}

static int nt_lava(int b) { return b == 10 || b == 11 || b == 51; }   /* lava, or fire */

/* lane/nether2: the way's next cell (the feet's, on the ground): a cell met again cuts the loop back to it; full, every
 * other crumb kept (the first, the arrival's, always) */
static void nt_crumb(struct nt_env *n, int x, int y, int z)
{
    int k = n->ncrumb;
    if (k > 0 && n->crumb[k - 1][0] == x && n->crumb[k - 1][1] == y && n->crumb[k - 1][2] == z) return;
    for (int i = k - 1; i >= 0; --i)
        if (n->crumb[i][0] == x && n->crumb[i][2] == z && abs(n->crumb[i][1] - y) <= 1)
        {
            n->ncrumb = i + 1;
            return;
        }
    if (k == NT_CRUMBS)
    {
        int m = 1;
        for (int i = 2; i < k; i += 2) memcpy(n->crumb[m++], n->crumb[i], sizeof n->crumb[0]);
        k = m;
    }
    n->crumb[k][0] = (int16_t)x, n->crumb[k][1] = (int16_t)y, n->crumb[k][2] = (int16_t)z;
    n->ncrumb = k + 1;
}

/* lane/nether3: a floor walked near (a crumb within NT_VISITED horizontally and 3 vertically) */
static int nt_visited(const struct nt_env *n, int x, int y, int z)
{
    for (int i = 0; i < n->ncrumb; ++i)
        if (abs(n->crumb[i][0] - x) + abs(n->crumb[i][2] - z) <= NT_VISITED && abs(n->crumb[i][1] - y) <= 3) return 1;
    return 0;
}

/* lane/nether3: a seen floor's worth as the search's next goal (higher better; -1e9 not one): along the bearing, out
 * from the arrival, level with the feet, not walked near, not lava */
static double nt_goal_score(const struct neth_vec *v, const struct nt_env *n, const struct vec_last *r,
                            const struct netherview_floor *f)
{
    double dx = f->x + 0.5 - r->x, dz = f->z + 0.5 - r->z, hd = sqrt(dx * dx + dz * dz);
    int fy = (int)floor(r->y + 0.13);
    if (hd < NT_GOAL_MIN || f->y < 30) return -1e9;
    if (n->fort_at)
    {
        /* lane/nether3: in the fortress, its own floors (brick) only, the farthest first, level with the feet */
        if (NT_LAVA_FLOOR && f->lava && abs(f->y + 1 - n->fort_ty) <= NT_LAVA_FLOOR_DY)
        {
            /* lane/nether5: lava lying at the deck's height (gold-g7: a lavafall spread 15 blocks along the bridge
             * that leads to the spawner's stair; the walker bridges a lava floor with cobblestone), after the dry
             * floors: the farthest first, NT_LAVA_PEN under them */
            return 0.5 * hd - 3.0 * abs(f->y + 1 - fy) - NT_LAVA_PEN;
        }
        if (f->block != 112 || f->lava) return -1e9;
        /* lane/nether5: with nether_fort_up a floor over the feet scores its rise (a blaze spawner stands on the
         * fortress's raised platform: gold-g7's two at 77 and 83 over decks at 74) */
        int dy = f->y + 1 - fy;
        if (NT_FORT_UP && dy > 0) return 0.5 * hd + NT_UP_GAIN * dy;
        return 0.5 * hd - 3.0 * abs(dy);
    }
    double br = (double)n->bearing * M_PI / 180.0, bx = -sin(br), bz = cos(br);
    double along = dx * bx + dz * bz;
    double ox = f->x + 0.5 - n->leg_x, oz = f->z + 0.5 - n->leg_z;   /* out from where the search began */
    double out = sqrt(ox * ox + oz * oz) - sqrt((r->x - n->leg_x) * (r->x - n->leg_x) + (r->z - n->leg_z) * (r->z - n->leg_z));
    double sc = along + 0.5 * out - 1.5 * abs(f->y + 1 - fy);
    if (f->lava) sc -= NT_LAVA_PEN + 0.5 * hd;
    return sc;
}

/* lane/nether3: the far view (netherview_fan) at the look: fortress blocks met (the nearest kept), a spawner, and in a
 * look round or with no goal the best floor seen as the next goal's candidate */
/* lane/nether5: the frontier, the fortress's floors the far view saw: one remembered a cell NT_FRONT_SEP apart (the
 * feet's height within 1), the oldest given way to when full */
static void nt_front_add(struct nt_env *n, int x, int y, int z)
{
    for (int i = 0; i < n->nfront; ++i)
        if (abs(n->front[i][0] - x) + abs(n->front[i][2] - z) <= NT_FRONT_SEP && abs(n->front[i][1] - y) <= 1) return;
    int k = n->nfront < NT_FRONT ? n->nfront++ : (n->kfront++ % NT_FRONT);
    n->front[k][0] = (int16_t)x, n->front[k][1] = (int16_t)y, n->front[k][2] = (int16_t)z, n->front[k][3] = 0;
}

/* lane/nether5: the floors within NT_VISITED of a cell (walked near it, or a goal given up there) done */
static void nt_front_done(struct nt_env *n, int x, int y, int z)
{
    for (int i = 0; i < n->nfront; ++i)
        if (abs(n->front[i][0] - x) + abs(n->front[i][2] - z) <= NT_VISITED && abs(n->front[i][1] - y) <= 3)
            n->front[i][3] = 1;
}

/* lane/nether5: the nearest floor not done and at least NT_GOAL_MIN off, -1 none */
static int nt_front_pick(const struct nt_env *n, const struct vec_last *r)
{
    int best = -1;
    double bd = 1e18;
    for (int i = 0; i < n->nfront; ++i)
    {
        if (n->front[i][3]) continue;
        double dx = n->front[i][0] + 0.5 - r->x, dz = n->front[i][2] + 0.5 - r->z, dy = n->front[i][1] - r->y;
        double d = dx * dx + dz * dz + 4.0 * dy * dy;
        if (dx * dx + dz * dz < NT_GOAL_MIN * NT_GOAL_MIN || d >= bd) continue;
        best = i, bd = d;
    }
    return best;
}

static void nt_fan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct nt_env *n = &e->nt;
    n->fan_at = e->steps;
    struct netherview_fan fan;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    double ey = r->y + 1.62;
    netherview_fan(v->p, nt_id(v, e), r->x, ey, r->z, r->yaw, r->pitch, NT_FAN_R, NT_FAN_YAW, NT_FAN_PITCH,
                   NT_FAN_HFOV, NT_FAN_VFOV, &fan);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    n->fan_ns += (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec);
    n->nfans += 1;
    for (int k = NV_FENCE; k <= NV_BRICK; ++k)
        if (fan.fort[k].d >= 0 && (!n->has_fort || fan.fort[k].d < py_dist3(r->x, ey, r->z, n->fx + 0.5, n->fy + 0.5,
                                                                           n->fz + 0.5)))
        {
            n->has_fort = 1;
            n->fx = fan.fort[k].x, n->fy = fan.fort[k].y, n->fz = fan.fort[k].z;
        }
    if (fan.top.d >= 0 && py_hypot2(fan.top.x + 0.5 - r->x, fan.top.z + 0.5 - r->z) <= NT_CLIMB_NEAR &&
        (!n->has_top || fan.top.y > n->ty || py_hypot2(n->tx + 0.5 - r->x, n->tz + 0.5 - r->z) > NT_CLIMB_NEAR))
    {
        n->has_top = 1;     /* lane/nether3: the highest fortress block near (a deck over the player) */
        n->tx = fan.top.x, n->ty = fan.top.y, n->tz = fan.top.z;
    }
    if (fan.fort[NV_SPAWNER].d >= 0 && !n->has_sp)
    {
        n->has_sp = 1;
        n->sx = fan.fort[NV_SPAWNER].x, n->sy = fan.fort[NV_SPAWNER].y, n->sz = fan.fort[NV_SPAWNER].z;
    }
    if (NT_FRONTIER && n->fort_at)
        for (int i = 0; i < fan.nfloor; ++i)
            if (fan.floor[i].block == 112 && !fan.floor[i].lava && fan.floor[i].y >= NT_SEA_TOP)
                nt_front_add(n, fan.floor[i].x, fan.floor[i].y + 1, fan.floor[i].z);
    if ((!NT_GOALS && !n->fort_at) || (n->spin <= 0 && n->has_goal)) return;
    for (int i = 0; i < fan.nfloor; ++i)
    {
        double sc = nt_goal_score(v, n, r, &fan.floor[i]);
        if (sc <= -1e8 || (n->has_cand && sc <= n->cand_score)) continue;
        if (nt_visited(n, fan.floor[i].x, fan.floor[i].y + 1, fan.floor[i].z)) sc -= NT_VISITED_PEN;
        if (n->has_cand && sc <= n->cand_score) continue;
        n->has_cand = 1, n->cand_score = (float)sc;
        n->cx2 = fan.floor[i].x, n->cy2 = fan.floor[i].y + 1, n->cz2 = fan.floor[i].z, n->clava = fan.floor[i].lava;
    }
}

/* the books, every step with the switch on (before the macros): the arrival and its portal, the scans, the fortress
 * and the spawner first seen, the rods, the home, what hurt last and the deaths */
/* lane/rod (nether_oracle, PRIVILEGED): the search aimed from the fortress map, not from what the player has seen. The
 * z and x legs' signs toward the nearest fortress piece (re-read every NT_OR_EVERY steps until the player is on the
 * fortress), the heading turned once when the map first holds one; its top nether brick nearest the player taken as the
 * fortress seen (the walker's goal); with 2, the throne's spawner cell as the spawner seen once on the fortress (or the
 * throne within 24); with 3, the arrival's portal cell from the server's blocks when the arrival did not stand in it */
static void nt_oracle(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct nt_env *n = &e->nt;
    n->or_at = e->steps;
    if (MOPT(MO_NT_START) && !n->has_loc)
    {
        int32_t chunk[2];
        int found = MOPT(MO_NT_START) == 2 ? netherview_oracle_throne(v->p, nt_id(v, e), r->x, r->z, chunk)
                                            : netherview_oracle_start(v->p, nt_id(v, e), r->x, r->z, chunk);
        if (found > 0)
        {
            n->has_loc = 1;
            n->loc_x = MOPT(MO_NT_START) == 2 ? chunk[0] : chunk[0] * 16 + 8;
            n->loc_y = (int)floor(r->y + 0.13);
            n->loc_z = MOPT(MO_NT_START) == 2 ? chunk[1] : chunk[1] * 16 + 8;
            n->loc_d0 = hypot(n->loc_x - r->x, n->loc_z - r->z);
        }
    }
    if (MOPT(MO_NT_ENTRY) && !n->has_entry)
    {
        int32_t goal[4];
        if (netherview_oracle_entry(v->p, nt_id(v, e), r->x, r->y, r->z, goal) > 0)
        {
            n->has_entry = 1;
            n->entry_x = goal[0], n->entry_y = goal[1], n->entry_z = goal[2], n->entry_kind = goal[3];
            fprintf(stderr, "NTENTRY env %d at %.1f %.1f %.1f target %d %d %d kind %d\n", nt_id(v, e), r->x,
                    r->y, r->z, n->entry_x, n->entry_y, n->entry_z, n->entry_kind);
        }
    }
    struct netherview_oracle o;
    if (netherview_oracle(v->p, nt_id(v, e), r->x, r->y, r->z, &o) < 0) return;
    if (!n->has_portal && n->arrived && e->nt_xportal[0])
        n->has_portal = 1, n->px = e->nt_xportal[1], n->py = e->nt_xportal[2], n->pz = e->nt_xportal[3];
    if (NT_ORACLE >= 3 && !n->has_portal && n->arrived)
    {
        int32_t c[3];
        if (netherview_oracle_portal(v->p, nt_id(v, e), (int)floor(r->x), (int)floor(r->y), (int)floor(r->z),
                                     NT_OR_PORTAL_R, c))
            n->has_portal = 1, n->px = c[0], n->py = c[1], n->pz = c[2];
    }
    if (!n->fort_at)
    {
        /* toward the throne (the spawner's room) where the map holds one: h9's nearest piece was a bridge 25 blocks
         * over the portal that the walker stood under for 24,000 steps; the throne lay 190 blocks off at -z */
        double tx, tz;
        if (o.has_piece)
        {
            const int32_t *bx = o.has_throne ? o.tbox : o.box;
            tx = 0.5 * (bx[0] + bx[3] + 1), tz = 0.5 * (bx[2] + bx[5] + 1);
            if (n->loc_mode) n->or_aimed = 0, n->loc_mode = 0;   /* the real piece replaces the seed's candidate */
        }
        else if (n->has_loc)
            tx = n->loc_x, tz = n->loc_z, n->loc_mode = 1;
        else
            return;
        double cx = tx - r->x, cz = tz - r->z;
        n->zdir = cz >= 0 ? 0 : 2;
        n->xdir = cx >= 0 ? 3 : 1;
        n->bearing = (float)(atan2(-cx, cz) * 180.0 / M_PI);
        if (!n->or_aimed)
        {
            n->or_aimed = 1;
            /* a z leg first, as the search's sweep runs (fortresses lie along z), the signs the map's */
            n->heading = n->zdir;
            n->legk = 0;
            n->leg_at = e->steps, n->leg_x = r->x, n->leg_z = r->z;
        }
        /* the throne's brick as the fortress seen only near the feet's height (or2-h9: given 51 blocks over the
         * player, all 32 envs stood under it at y 32 to the end; the fortress the walk meets is seen honestly) */
        if (o.has_piece && o.has_brick && py_hypot2(o.brick[0] + 0.5 - r->x, o.brick[2] + 0.5 - r->z) < NT_OR_BRICK_D &&
            fabs(o.brick[1] - r->y) <= NT_OR_BRICK_DY)
        {
            n->has_fort = 1;
            n->fx = o.brick[0], n->fy = o.brick[1], n->fz = o.brick[2];
        }
    }
    if (!o.has_piece) return;
    if (n->t_or_piece < 0) n->t_or_piece = e->steps;
    if (NT_ORACLE >= 2 && o.has_throne && n->fort_at && o.has_sp)
    {
        if (!n->has_sp || n->sx != o.sp[0] || n->sy != o.sp[1] || n->sz != o.sp[2])
            n->has_sp = 1, n->sx = o.sp[0], n->sy = o.sp[1], n->sz = o.sp[2];
        if (n->t_or_sp < 0) n->t_or_sp = e->steps;
    }
}

/* lane/rod (nether_oracle spawner, PRIVILEGED): the walk to (tx, ty, tz) along netherview_oracle_path's cells (the
 * server's blocks: a floor, no lava beside), each next cell walked to by nt_walk; the path made again after NT_OPATH_OFF
 * steps off it or NT_OPATH_AGE steps. 1 within near of the target, 0 walking, -1 no path */
static int nt_opath_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int tx, int ty, int tz, double near,
                         int mode)
{
    /* mode: 1 the target's height exact (NV_PATH_EXACT), 2 the tunnel (NV_PATH_TUNNEL) */
    struct nt_env *n = &e->nt;
    int exact = mode & 1;
    n->opath_tun = (mode & 2) != 0 && !n->floor_untun;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (py_hypot2(tx + 0.5 - r->x, tz + 0.5 - r->z) <= near && abs(fy - ty) <= (exact ? 0 : NV_PATH_GOAL_DY)) return 1;
    if (tx != n->opath_tgt[0] || ty != n->opath_tgt[1] || tz != n->opath_tgt[2])
    {
        /* a new target: a new path (no wait for the retry) */
        n->opath_tgt[0] = tx, n->opath_tgt[1] = ty, n->opath_tgt[2] = tz;
        n->nopath = 0, n->opaths_new = 1;
    }
    if (!r->on_ground && n->nopath > 0) a->hold = 1u << PK_FORWARD;    /* (mid-jump or mid-drop: on) */
    int k = -1;
    /* The long return route can descend below a path cell; admit one level only when it cannot skip a two-high stair. */
    int match_dy = n->opath_tun && !(MOPT(MO_NT_CHAMBER) == 2 && e->chamber_on == 3) ? 0 : 1;
    for (int j = n->opath_k - 1 < 0 ? 0 : n->opath_k - 1; j < n->nopath && j <= n->opath_k + 3; ++j)
        if (n->opath[j][0] == fx && n->opath[j][2] == fz && abs(n->opath[j][1] - fy) <= match_dy &&
            (n->opath[j][1] == fy || j + 1 >= n->nopath || abs(n->opath[j + 1][1] - fy) <= 1))
            k = j;      /* the tunnel: at its height exactly (tunp-9ct: a dug step down taken as walked, the next
                         * cell 2 under, 600 steps a block) */
    if (k >= 0) n->opath_k = k + 1, n->opath_off = 0;
    else if (n->nopath > 0 && r->on_ground) n->opath_off += 1;
    if (n->nopath > 0 && n->opath_k >= n->nopath && r->on_ground)
        n->nopath = 0, n->opaths_new = 1;       /* a partial path's end (the target past the loaded chunks): on */
    if (n->nopath == 0 && !n->opaths_new && n->opaths > 0 && e->steps - n->opath_t < NT_OPATH_RETRY)
        return -1;      /* none found lately */
    n->opaths_new = 0;
    int path_age = MOPT(MO_NT_CHAMBER) == 2 && e->chamber_on == 3 ? NT_CHAMBER_OPATH_AGE : NT_OPATH_AGE;
    if (n->nopath == 0 || n->opath_off > NT_OPATH_OFF || e->steps - n->opath_t > path_age)
    {
        if (!r->on_ground) return 0;
        n->opaths += 1, n->opath_t = e->steps, n->opath_off = 0, n->opath_k = 0;
        n->nopath = netherview_oracle_path(v->p, nt_id(v, e), fx, fy, fz, tx, ty, tz, near,
                                           NV_PATH_BUILD | (exact ? NV_PATH_EXACT : 0) |
                                               (n->opath_tun ? NV_PATH_TUNNEL : 0), n->opath,
                                           NT_OPATH);
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
        {
            fprintf(stderr, "op %lld from %d %d %d to %d %d %d len %d:", (long long)e->steps, fx, fy, fz, tx, ty, tz,
                    n->nopath);
            for (int j = 0; j < n->nopath && j < 12; ++j)
                fprintf(stderr, " %d,%d,%d", n->opath[j][0], n->opath[j][1], n->opath[j][2]);
            if (n->nopath > 12)
                fprintf(stderr, " .. %d,%d,%d", n->opath[n->nopath - 1][0], n->opath[n->nopath - 1][1],
                        n->opath[n->nopath - 1][2]);
            fprintf(stderr, "\n");
        }
#endif
        if (n->nopath <= 0) return n->nopath = 0, -1;
        n->opaths_ok += 1;
    }
    if (n->opath_k >= n->nopath) n->opath_k = n->nopath - 1;
    const int16_t *c = n->opath[n->opath_k];
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
        fprintf(stderr, "ow %lld at %d %d %d (%.2f %.2f %.2f g %d) k %d of %d -> %d %d %d off %d build %d\n",
                (long long)e->steps, fx, fy, fz, r->x, r->y, r->z, r->on_ground, n->opath_k, n->nopath, c[0], c[1],
                c[2], n->opath_off, n->opath_build);
#endif
    if (n->opath_tun && c[0] == fx && c[2] == fz && c[1] == fy - 1)
    {
        /* the tunnel's next cell straight under the feet (tunp-9dt: a dug step down's head cell walked into, the
         * landing under it, a new path every 65 steps): dug, else dropped into */
        int b = nt_block(v, e, fx, fy - 1, fz);
        /* lane/legs (nether_floor_keep): not the floor under the feet over a drop (seed 42's library Nether start: the
         * tunnel's way to the spawner went down through the fortress deck at y 74, 61 of 64 fell 50 blocks into the
         * lava sea): planned again without the tunnel for the rest of the stay (with the guard alone 41 of 64 stood on
         * the deck's cell until the bound, the same tunnel cell planned each time) */
        if (NT_FLOOR_KEEP && !nt_open(b) && nt_deep(v, e, fx, fy - 1, fz))
        {
            n->floor_untun = 1, n->opath_tun = 0, n->nopath = 0;
            return 0;
        }
        /* lane/nleg (nether_drop_keep): nor an open cell with no floor under it (e44b and k20's seed-42 Nether starts:
         * the tunnel's way to the spawner stepped down off the player's own bridge at y 75, the walk to the column's
         * middle walked off the brink uncrouched and fell 50 blocks into the lava sea): the same answer */
        if (NT_DROP_KEEP && nt_open(b) && nt_deep(v, e, fx, fy - 1, fz))
        {
            n->floor_untun = 1, n->opath_tun = 0, n->nopath = 0;
            return 0;
        }
        n->or_drop_at = e->steps; /* the wrapper's brink sneak must not stop the planner's safe centred drop */
        still(a);
        /* A player can stand on the neighboring block's collision edge while the centre is already over air. Looking
         * down and standing still then never drops into the path's next cell (seed 1: 34.26,85,-47.50, cell below air,
         * the tunnel's path waited until NT_CELL_AGE and replanned). Walk to this column's middle first. */
        if (nt_open(b) && r->on_ground && py_hypot2(fx + 0.5 - r->x, fz + 0.5 - r->z) > 0.12)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, fx + 0.5, ey, fz + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 0.0f;
            a->hold = 1u << PK_FORWARD;
            return 0;
        }
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = 90.0f;
        if (!nt_open(b) && b != 10 && b != 11)
        {
            int tb = nt_tool(v, s, b);
            if (tb >= 0 && tb != s->current) a->hotbar = tb;
            a->hold = 1u << PK_ATTACK;
        }
        n->opath_off = 0;
        return 0;
    }
    int vm = c[1] - fy;     /* lane/rod (tunnel): a cell one up or one down built too (the walker's stair modes) */
    if (abs(vm) <= (n->opath_tun ? 1 : 0) && abs(c[0] - fx) + abs(c[2] - fz) == 1 &&
        (!nt_open(nt_block(v, e, c[0], c[1], c[2])) || !nt_open(nt_block(v, e, c[0], c[1] + 1, c[2])) ||
         nt_open(nt_block(v, e, c[0], c[1] - 1, c[2])) ||
         (vm > 0 && !nt_open(nt_block(v, e, fx, fy + 2, fz))) ||
         (vm < 0 && !nt_open(nt_block(v, e, c[0], c[1] + 2, c[2])))))
    {
        /* a cell the path builds (a block dug, a floor placed): the walker's column forced to it (its own choice takes
         * an open way before a dug one), then its dig and bridge and its lava rules */
        int d = c[2] - fz == 1 ? 0 : c[0] - fx == -1 ? 1 : c[2] - fz == -1 ? 2 : 3;
        if (n->wk < 0 || n->wcx != c[0] || n->wcz != c[2] || n->wbx != fx || n->wby != fy || n->wbz != fz)
        {
            n->wbx = fx, n->wby = fy, n->wbz = fz, n->wtried = 0, n->warr = -1;
            n->wdir = d, n->wcx = c[0], n->wcy = fy, n->wcz = c[2], n->wmode = vm;
            n->wk = 0, n->wlook = (int)e->steps, n->wgain = (int)e->steps, n->wplace = 0, n->whead = 0, n->wstill = 0;
            n->wseal = 0;
        }
        n->or_build = 1;
        int w = nt_walk(v, e, r, s, a, ey, c[0], c[1], c[2], 0.4, d);
        n->or_build = 0;
        if (w < 0 || ++n->opath_build > NT_OPATH_BUILD_MAX)
            n->nopath = 0, n->opaths_new = 1, n->opath_build = 0;   /* refused or stuck: a new path next step */
        return 0;
    }
    n->opath_build = 0;
    double yaw, pitch;
    look_at(r->x, ey, r->z, c[0] + 0.5, ey, c[2] + 0.5, &yaw, &pitch);
    still(a);
    a->look_mode = 1;
    a->look[0] = (float)yaw, a->look[1] = 10.0f;
    a->hold = 1u << PK_FORWARD;
    if (c[1] > fy) a->hold |= 1u << PK_JUMP;                    /* a step up */
    else if (c[1] == fy) a->hold |= 1u << PK_SNEAK;              /* level: no edge walked off */
    else n->or_drop_at = e->steps;                               /* a drop: nt_drive's brink crouch not held */
    (void)s;
    return 0;
}

/* Rise or descend in sealed stages while the corridor is still away. The staged target bounds the tunnel planner's
 * grid and puts the stair in rock early, so the last leg enters through a wall at the corridor's standing height. */
#define NT_ENTRY_WAY 32.0
#define NT_ENTRY_NEAR 0.8
#define NT_ENTRY_RETRY 200
#define NT_ENTRY_FAILS 3
static int nt_entry_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!n->has_entry || n->entry_done || e->steps < n->entry_retry_at) return -1;
    if (!n->entry_way)
    {
        double dx = n->entry_x + 0.5 - r->x, dz = n->entry_z + 0.5 - r->z, d = hypot(dx, dz);
        int fy = (int)floor(r->y + 0.13), dy = n->entry_y - fy;
        double step = d > NT_ENTRY_WAY ? NT_ENTRY_WAY : d;
        if (abs(dy) > 8 && d > 8 && step > d * 0.5) step = d * 0.5;
        if (d <= NT_ENTRY_WAY && abs(dy) <= 8)
            n->entry_wx = n->entry_x, n->entry_wy = n->entry_y, n->entry_wz = n->entry_z;
        else
        {
            n->entry_wx = (int)floor(r->x + dx * step / (d > 0 ? d : 1));
            n->entry_wz = (int)floor(r->z + dz * step / (d > 0 ? d : 1));
            int climb = (int)floor(step);
            n->entry_wy = fy + (dy > climb ? climb : dy < -climb ? -climb : dy);
        }
        n->entry_way = 1;
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE)
            fprintf(stderr, "NTENTRYWAY %lld from %.1f %.1f %.1f to %d %d %d final %d %d %d\n",
                    (long long)e->steps, r->x, r->y, r->z, n->entry_wx, n->entry_wy, n->entry_wz,
                    n->entry_x, n->entry_y, n->entry_z);
#endif
    }
    n->phase = NT_SEARCH;
    int w = nt_opath_walk(v, e, r, s, a, ey, n->entry_wx, n->entry_wy, n->entry_wz, NT_ENTRY_NEAR, 3);
    if (w == 0) return 0;
    if (w > 0)
    {
        if (n->entry_wx == n->entry_x && n->entry_wy == n->entry_y && n->entry_wz == n->entry_z)
            n->entry_done = 1;
        n->entry_way = 0, n->entry_fail = 0;
        return 0;
    }
    n->entry_way = 0, n->entry_retry_at = e->steps + NT_ENTRY_RETRY;
    if (++n->entry_fail >= NT_ENTRY_FAILS) n->entry_done = 1;
    return -1;
}

static void nt_observe(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                       int yaw_bin)
{
    struct nt_env *n = &e->nt;
    n->free_slots = 0;
    for (int i = 0; i < 36; ++i) n->free_slots += s->inv[i].item < 0 || s->inv[i].count <= 0;
    n->pol_yaw = yaw_bin - NETH_VEC_CAM / 2;      /* the policy's camera this step (the search's side) */
    if (NT_SPREAD)
    {
        /* lane/rod (nether_spread): the side and the bearing a player picks, drawn from the env, the episode and the
         * step (-4 .. 4 bins, 30 degrees each), not the policy's camera: the frozen s64-it42 turns the same bin from
         * one start in every run, so its 128 runs were one search (5 distinct trajectories from gold-g7) */
        uint64_t h = (uint64_t)nt_id(v, e) * 0x9E3779B97F4A7C15ull ^ (uint64_t)e->cyc * 0xBF58476D1CE4E5B9ull ^
                     (uint64_t)e->steps * 0x94D049BB133111EBull;
        h ^= h >> 31, h *= 0xD6E8FEB86659FD93ull, h ^= h >> 32;
        n->pol_yaw = (int)(h % 9) - 4;
    }
    int dead = (r->flags & PF_DEAD) != 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
    {
        fprintf(stderr, "ob %lld kind %d on %d fid %d pos %.2f %.2f %.2f hp %.1f fire %d mv %.3f %.3f %.3f vis", (long long)e->steps,
                e->kind, n->on, e->fid, r->x, r->y, r->z, r->health, r->fire, r->x - n->fpx, 0.0, r->z - n->fpz);
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS && i < 4; ++i)
            fprintf(stderr, " %d:%.1f:h%.2f:dy%.2f", s->vis[i].kind,
                    py_dist3(r->x, r->y + 1.62, r->z, s->vis[i].x, s->vis[i].y, s->vis[i].z), s->vis[i].h,
                    s->vis[i].y - r->y);
        fprintf(stderr, "\n");
        if (e->steps % 4 == 0 || r->health < n->lhp) netherview_debug_ents(v->p, nt_id(v, e), r->x, r->y, r->z, 64.0);
    }
#endif
    if (!dead && r->dim == -1)
    {
        /* what hurt (the screen shows it): lava the feet or head stand in, a hostile in view (a blaze's fireball burns
         * too), the flames alone, a landing from over 3.5 blocks, hunger at 0 */
        float drop = n->lhp - r->health;
        int id = nt_id(v, e), fb = netherview_block(v->p, id, fx, fy, fz, NULL);
        int hb = netherview_block(v->p, id, fx, fy + 1, fz, NULL), in_lava = fb == 10 || fb == 11 || hb == 10 || hb == 11;
        /* lane/nether3: the player's box (0.6 wide) over a neighbour's lava at the feet's height (soul sand's top is
         * under the lava sea's surface) */
        for (int k = 0; k < 4 && !in_lava; ++k)
        {
            int b = netherview_block(v->p, id, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), (int)floor(r->y),
                                     (int)floor(r->z + (k & 2 ? 0.3 : -0.3)), NULL);
            in_lava = b == 10 || b == 11;
        }
        if (in_lava) n->lava_at = e->steps;
        if (r->on_ground && !n->lground && n->fall_top - r->y > 3.5 && drop > 0) n->cause_last = NC_FALL;
        else if (drop > 0)
        {
            int near = 0;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
                if (s->vis[i].kind != HK_PIGMAN && py_dist3(r->x, r->y, r->z, s->vis[i].x, s->vis[i].y, s->vis[i].z) < 16.0)
                    near = 1;
            /* lava's burning outlasts it (15 s): the flames within NT_LAVA_BURN steps of a lava touch are the lava's */
            int burn_lava = r->fire && n->lava_at > 0 && e->steps - n->lava_at <= NT_LAVA_BURN;
            n->cause_last = in_lava || burn_lava ? NC_LAVA : near ? NC_MOB : r->fire ? NC_FIRE : r->food == 0 ? NC_STARVE
                                                                                                    : NC_OTHER;
        }
        if (r->on_ground) n->fall_top = r->y;
        else if (r->y > n->fall_top) n->fall_top = r->y;
        n->lground = r->on_ground;
        n->lhp = r->health;
        /* lane/rod: the funnel's books (counting only) */
        if (r->on_ground) n->gfx = fx, n->gfy = fy, n->gfz = fz;
        if (n->fort_at && n->t_fortin < 0) n->t_fortin = e->steps;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS && n->t_bseen < 0; ++i)
            if (s->vis[i].kind == HK_BLAZE) n->t_bseen = e->steps;
        if (e->kind == M_MINE && n->on) n->st_drive += 1;
        else if (e->kind == M_SURFACE && e->esc) n->st_esc += 1;
        else if (r->screen != PS_NONE) n->st_screen += 1;
        else if (e->steps < n->off) n->st_off += 1;
    }
    if (dead && !n->was_dead && n->last_dim == -1)
    {
        /* a death in the Nether (the chain's end: keep_inventory off) */
        n->deaths += 1;
        n->cause[n->cause_last] += 1;
        if (n->t_death < 0)
        {
            n->t_death = e->steps, n->death_pos[0] = r->x, n->death_pos[1] = r->y, n->death_pos[2] = r->z;
            n->dgx = n->gfx, n->dgy = n->gfy, n->dgz = n->gfz;     /* lane/rod: where the feet last stood */
        }
    }
    n->cobble = (int)inv_count(s, COBBLE);
    if (dead) n->last_dim = 99;             /* a respawn is in no dimension the walk came from */
    n->was_dead = dead;
    if (dead) n->lhp = 20.0f, n->lground = 1;
    int rods = (int)inv_count(s, BLAZE_ROD);
    n->rods = rods, n->kills = s->mob_kills;
    {
        /* lane/rod: the stats screen's blaze kills since the episode's first step (the books' bstat) */
        const struct neth_rl_far *f = neth_rl_far(v->h, nt_id(v, e));
        if (f && !n->bstat_set) n->bstat0 = f->blaze_kills, n->bstat_set = 1;
        if (f) n->bstat = f->blaze_kills - n->bstat0;
    }
    if (rods > n->rods_max) n->rods_max = rods;
    if (rods >= 1 && n->t_rod1 < 0) n->t_rod1 = e->steps;
    if (rods >= NT_RODS && n->t_rods7 < 0) n->t_rods7 = e->steps;
    /* home: the overworld straight from the Nether alive (a respawn is not a way home) */
    if (r->dim == 0 && n->last_dim == -1 && !dead && !n->was_dead && n->t_home < 0)
        n->t_home = e->steps, n->home_rods = (int)inv_count(s, BLAZE_ROD), n->home_hp = r->health;
    if (!dead) n->last_dim = r->dim;
    if (r->dim != -1 || dead) return;
    n->nsteps += 1;
    if (n->has_portal)
    {
        /* lane/rod: the least distance to the portal cell this episode (books portal_dmin, stored + 1) */
        int d = abs((int)floor(r->x) - n->px) + abs((int)floor(r->y + 0.13) - n->py) + abs((int)floor(r->z) - n->pz);
        if (n->pdmin == 0 || d + 1 < n->pdmin) n->pdmin = d + 1;
    }
    if (!n->arrived)
    {
        /* the arrival: the portal block the feet (or the head) stand in, else one beside them */
        n->arrived = 1;
        n->t_arrive = e->steps;
        /* the search's direction: along z the arrival's facing's sign (fortresses lie along z), NT_SIDE degrees off it
         * toward the side the policy's camera turns to this step, else the side the facing leans to */
        double base = cos((double)r->yaw * M_PI / 180.0) >= 0 ? 0.0 : 180.0;
        double lean = remainder((double)r->yaw - base, 360.0);
        int side = n->pol_yaw > 0 ? 1 : n->pol_yaw < 0 ? -1 : lean >= 0 ? 1 : -1;
        n->zdir = (int)lround(base / 90.0) & 3;
        if (MOPT(MO_NT_SWEEP) == 2)
        {
            /* lane/rod (nether_sweep draw): a player's coin for the way along z (the env, the episode): from one
             * portal the -z way met the fortress in 4,650 steps and the +z way never */
            uint64_t h = (uint64_t)nt_id(v, e) * 0x9E3779B97F4A7C15ull ^ (uint64_t)e->cyc * 0xBF58476D1CE4E5B9ull;
            h ^= h >> 31, h *= 0xD6E8FEB86659FD93ull, h ^= h >> 32;
            if (h & 1) n->zdir = (n->zdir + 2) & 3;
        }
        if (MOPT(MO_NT_SWEEP) == 3) n->zdir = 2;   /* lane/rod (nether_sweep minus): -z, an eval-only upper bound */
        n->xdir = (int)lround((base + 90.0 * side) / 90.0) & 3;
        n->heading = n->zdir, n->legk = 0;
        n->spin = NT_SPIN;
        n->spin_yaw = r->yaw;
        n->leg_at = e->steps, n->sbest_t = e->steps;
        if (NT_SWEEP_BACK) n->leg_len = NT_BACK_D;     /* lane/rod (nether_sweep back): the first leg short */
        n->leg_x = r->x, n->leg_z = r->z;
        /* lane/nether3: the far search's bearing: the arrival's facing, turned by the policy's camera this step (30
         * degrees a bin off centre: the envs fan out) */
        n->bearing = (float)remainder((double)r->yaw + 30.0 * n->pol_yaw, 360.0);
        int id = nt_id(v, e);
        for (int k = 0; k < 27 * 2 && !n->has_portal; ++k)
        {
            int j = k % 27, dy = k < 27 ? (j % 3) - 1 : (j % 3) - 1 + 1;
            int dx = j / 9 - 1, dz = (j / 3) % 3 - 1;
            if (k < 27 && (dx || dz)) continue;              /* the feet's column first */
            if (netherview_block(v->p, id, fx + dx, fy + dy, fz + dz, NULL) == 90)
            {
                n->has_portal = 1;
                n->px = fx + dx, n->py = fy + dy, n->pz = fz + dz;
            }
        }
    }
    if (NT_ORACLE && (v->macros & NETH_VEC_NETHER) && e->steps - n->or_at >= NT_OR_EVERY) nt_oracle(v, e, r);
    if (r->on_ground && n->phase != NT_HOME && (v->macros & NETH_VEC_NETHER))
    {
        nt_crumb(n, fx, (int)floor(r->y + 0.13), fz);
        if (n->nfront) nt_front_done(n, fx, (int)floor(r->y + 0.13), fz);   /* lane/nether5: walked near */
    }
    /* the fortress first seen: the cast's nether brick (its seen slot 2 in the Nether) or the scans' */
    if (s->seen_n[2] > 0 && s->seen[2].d >= 0 && (!NT_FAN || s->seen[2].y >= NT_SEA_TOP))
    {
        double d = py_dist3(r->x, r->y + 1.62, r->z, s->seen[2].x + 0.5, s->seen[2].y + 0.5, s->seen[2].z + 0.5);
        if (!n->has_fort || d < py_dist3(r->x, r->y + 1.62, r->z, n->fx + 0.5, n->fy + 0.5, n->fz + 0.5))
            n->fx = s->seen[2].x, n->fy = s->seen[2].y, n->fz = s->seen[2].z;
        n->has_fort = 1;
    }
    int every = n->spin > 0 ? NT_SCAN_SPIN : NT_SCAN_EVERY;
    if (e->steps - n->scan_at >= every)
    {
        n->scan_at = e->steps;
        int arriving = !n->has_portal && e->steps - n->t_arrive <= NT_SPIN * 2;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        /* lane/nether2: no lava kind (nothing read it; the lava sea's thousands of cells were most of the scan) */
        netherview_scan(v->p, nt_id(v, e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, NT_SCAN_R,
                        (1 << NV_SPAWNER) | (1 << NV_FENCE) | (1 << NV_BRICK) | (arriving ? 1 << NV_PORTAL : 0),
                        n->hit);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        n->scan_ns += (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec);
        if (arriving && n->hit[NV_PORTAL].d >= 0 && n->hit[NV_PORTAL].d < NT_PORTAL_D)
        {
            /* the portal the player came through, seen in the arrival's look round: its column's lowest cell (the
             * portal's face is in view down to its frame) */
            int id = nt_id(v, e), x = n->hit[NV_PORTAL].x, y = n->hit[NV_PORTAL].y, z = n->hit[NV_PORTAL].z;
            while (y > 1 && netherview_block(v->p, id, x, y - 1, z, NULL) == 90) --y;
            n->has_portal = 1;
            n->px = x, n->py = y, n->pz = z;
        }
        for (int k = NV_FENCE; k <= NV_BRICK; ++k)
            if (n->hit[k].d >= 0 && (!NT_FAN || n->hit[k].y >= NT_SEA_TOP) &&
                (!n->has_fort || n->hit[k].d < py_dist3(r->x, r->y + 1.62, r->z, n->fx + 0.5,
                                                                            n->fy + 0.5, n->fz + 0.5)))
            {
                n->has_fort = 1;
                n->fx = n->hit[k].x, n->fy = n->hit[k].y, n->fz = n->hit[k].z;
            }
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
            fprintf(stderr, "ns %lld spawner n %d d %.1f at %d %d %d; fence n %d brick n %d\n", (long long)e->steps,
                    n->hit[NV_SPAWNER].n, n->hit[NV_SPAWNER].d, n->hit[NV_SPAWNER].x, n->hit[NV_SPAWNER].y,
                    n->hit[NV_SPAWNER].z, n->hit[NV_FENCE].n, n->hit[NV_BRICK].n);
#ifdef NT_TRACE_SP
        if (nt_id(v, e) == NT_TRACE && e->steps <= 3)
        {
            /* lane/rod: the cells on the line from the eye to a known spawner cell (a developer's check) */
            static const int SP[3] = {NT_TRACE_SP};
            double ex = r->x, ey2 = r->y + 1.62, ez = r->z, tx = SP[0] + 0.5, ty = SP[1] + 0.5, tz = SP[2] + 0.5;
            int lx = 1 << 30, ly = 0, lz = 0;
            fprintf(stderr, "line:");
            for (int k = 0; k <= 200; ++k)
            {
                double f = k / 200.0;
                int cx = (int)floor(ex + (tx - ex) * f), cy = (int)floor(ey2 + (ty - ey2) * f), cz = (int)floor(ez + (tz - ez) * f);
                if (cx == lx && cy == ly && cz == lz) continue;
                lx = cx, ly = cy, lz = cz;
                fprintf(stderr, " (%d %d %d)=%d", cx, cy, cz, netherview_block(v->p, nt_id(v, e), cx, cy, cz, NULL));
            }
            fprintf(stderr, " seen %d\n", netherview_seen(v->p, nt_id(v, e), ex, ey2, ez, SP[0], SP[1], SP[2]));
        }
#endif
#endif
        if (n->hit[NV_SPAWNER].d >= 0)
        {
            n->has_sp = 1;
            n->sx = n->hit[NV_SPAWNER].x, n->sy = n->hit[NV_SPAWNER].y, n->sz = n->hit[NV_SPAWNER].z;
        }
    }
    if (NT_FAR_SP && (v->macros & NETH_VEC_NETHER) && !n->has_sp && n->has_fort &&
        e->steps - n->far_at >= (n->spin > 0 ? NT_FAR_SP_SPIN : NT_FAR_SP_EVERY))
    {
        /* lane/nether5: the spawner farther off (gold-g7's hall: 32 blocks down its length, past the scan's 20 and
         * between the fan's rays): the same in-view, clear-line scan of the spawner alone, NT_FAR_SP_R out */
        n->far_at = e->steps;
        struct netherview_hit fh[NV_KINDS];
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        netherview_scan(v->p, nt_id(v, e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, NT_FAR_SP_R, 1 << NV_SPAWNER, fh);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        n->scan_ns += (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec);
        if (fh[NV_SPAWNER].d >= 0)
        {
            n->has_sp = 1;
            n->sx = fh[NV_SPAWNER].x, n->sy = fh[NV_SPAWNER].y, n->sz = fh[NV_SPAWNER].z;
        }
    }
    if (NT_FAN && (v->macros & NETH_VEC_NETHER) &&
        e->steps - n->fan_at >= (n->spin > 0 || n->clook > 0 ? NT_FAN_SPIN : NT_FAN_EVERY))
        nt_fan(v, e, r);
    if (n->has_fort && n->t_found < 0) n->t_found = e->steps;
    if (s->mo_hit && s->mo_block == 52 && !n->has_sp)
    {
        n->has_sp = 1;                      /* the crosshair on the spawner */
        n->sx = s->mo_x, n->sy = s->mo_y, n->sz = s->mo_z;
    }
    if (n->has_sp && n->t_spawner < 0) n->t_spawner = e->steps;
}

/* the search's next leg (a look round first): after a z leg an x leg, its side the one the policy's camera turns to at
 * that step (its yaw bin right of centre: the z heading's right; left: its left; centred: the side kept); after an x
 * leg the z leg again (the sweep: along z, a side step, along z) */
static void nt_leg_next(struct nt_env *n, const struct vec_last *r, int64_t t, int back)
{
    n->leg_len = 0;
    n->sbest = 0, n->sbest_t = t;
    if (n->legk == 0)
    {
        if (n->pol_yaw != 0) n->xdir = (n->zdir + (n->pol_yaw > 0 ? 3 : 1)) & 3;   /* yaw + 90: the right hand */
        n->legk = 1, n->heading = n->xdir;
    }
    else
    {
        n->legk = 0;
        if (back)
        {
            /* lane/rod (nether_sweep back): each z leg the other way and longer, back past the arrival's z and as far
             * the other side (out NT_LEG_D, back 2 NT_LEG_D, out 3 ...): a player who found nothing one way turns
             * round */
            n->zlegs += 1;
            n->zdir = (n->zdir + 2) & 3;
            n->leg_len = NT_BACK_D * (double)(n->zlegs + 1);
        }
        n->heading = n->zdir;
    }
    n->wflip = 0;
    n->leg_at = t;
    n->leg_x = r->x, n->leg_z = r->z;
    n->spin = NT_SPIN, n->spin_yaw = r->yaw;
}

/* lava (flowing or a source) beside the player's own feet or head, or over the head: its index k (the cell fx + AX[k],
 * fy + AY[k], fz + AZ[k]), -1 none. The cells next to the player's own are in view (right at their face) */
static const int AX[9] = {0, -1, 0, 1, 0, -1, 0, 1, 0}, AY[9] = {0, 0, 0, 0, 1, 1, 1, 1, 2}, AZ[9] = {1, 0, -1, 0, 1, 0, -1, 0, 0};
static int nt_block(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z);
static int nt_lava_near(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    for (int k = 0; k < 9; ++k)
    {
        int b = nt_block(v, e, fx + AX[k], fy + AY[k], fz + AZ[k]);
        if (b == 10 || b == 11) return k;
    }
    return -1;
}

/* the macro wanted: the switch on, in the Nether, alive, no screen, not off (lava beside the player overrides off) */
/* lane/bportal (obsidian_carry frame): the arrival frame's mining would start (obsidian.c, after this file); the macro
 * yields to it, at its start and at its drive's end test */
static int ob_frame_ready(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s);
static int nt_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    return (v->macros & NETH_VEC_NETHER) && r->dim == -1 && !(r->flags & PF_DEAD) &&
           (r->screen == PS_NONE || (r->screen == PS_INVENTORY && (e->nt.refill_at > 0 || e->nt.trim_at > 0))) &&
           e->nt.arrived && (e->steps >= e->nt.off || MOPT(MO_NT_OFF_HOLD) ||
                             (e->nt.aseal < 2 * NT_PLACE_TRIES && nt_lava_near(v, e, r) >= 0));
}

/* the hostile to fight (its vis index), -1 none: a blaze within NT_FIGHT_D, another (never a pigman or a ghast)
 * within NT_MOB_D (lane/nether5: with skel, a skeleton within NT_SKEL_D) */
static int nt_foe(const struct vec_last *r, const struct neth_rl_state *s, double blaze_d, int skel)
{
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (k == HK_PIGMAN || k == GK_GHAST || !fight_kind(k)) continue;
        double d = py_dist3(r->x, r->y + 1.62, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
        /* lane/nether5: a skeleton (an archer: its arrows knocked gold-g7's player off the hall's edge into the lava
         * from 15 blocks behind) within NT_SKEL_D, unless the caller wants only what is in reach */
        double kd = k == HK_BLAZE ? blaze_d : k == HK_SKELETON && skel && blaze_d > NT_REACH ? NT_SKEL_D : NT_MOB_D;
        if (d < kd && d < bd) best = i, bd = d;
    }
    return best;
}

/* the block in a cell next to the player's own two (what is right in front of their face, or under the next cell once
 * it is clear: the walker reads no other) */
static int nt_block(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    /* lane/macroopts: lava only as lava_sense allows (vecenv.c lava_seen) */
    return lava_seen(v, e, x, y, z, netherview_block(v->p, nt_id(v, e), x, y, z, NULL));
}

/* a cell the player walks through: air, a mushroom, tall grass, snow (no box) */
static int nt_open(int b) { return b == 0 || b == 39 || b == 40 || b == 31 || b == 78; }

/* lane/rod (nether_safe_dig deep): the cells under (x, y, z) to NT_DEEP_D below it all open or lava (or not loaded): no
 * floor to land on within a fall that does not hurt, so that block is never dug from under the player */
static int nt_deep(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    for (int d = 1; d <= NT_DEEP_D; ++d)
    {
        int b = nt_block(v, e, x, y - d, z);
        if (b > 0 && !nt_open(b) && !nt_lava(b)) return 0;
    }
    return 1;
}

/* lane/nleg (nether_camp_side): the camp's column under the kept spawner, NT_CAMP_SIDE cells to the side (sx + ox,
 * sz + oz) whose room cell beside the spawner (its height) and the cell over that are open, the sides tried nearest
 * the player first; chosen once a spawner and kept, once its cells are loaded (until then, and with none open, the
 * spawner's own column) */
static void nt_camp_cell(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int *tx, int *tz)
{
    struct nt_env *n = &e->nt;
    if (n->csx != n->sx || n->csy != n->sy || n->csz != n->sz)
    {
        n->csx = n->sx, n->csy = n->sy, n->csz = n->sz, n->cs_ox = 0, n->cs_oz = 0;
        int k = NT_CAMP_SIDE, best = -1, unloaded = 0;
        double bd = 0.0;
        for (int d = 0; d < 4; ++d)
        {
            int cx = n->sx + k * NDX[d], cz = n->sz + k * NDZ[d];
            int b0 = nt_block(v, e, cx, n->sy, cz), b1 = nt_block(v, e, cx, n->sy + 1, cz);
            unloaded |= b0 < 0 || b1 < 0;
            if (!nt_open(b0) || !nt_open(b1)) continue;
            double dd = (cx + 0.5 - r->x) * (cx + 0.5 - r->x) + (cz + 0.5 - r->z) * (cz + 0.5 - r->z);
            if (best < 0 || dd < bd) best = d, bd = dd;
        }
        if (unloaded) n->csx = n->csy = n->csz = 0;   /* chosen again next step (the spawner is kept from far off) */
        else if (best >= 0) n->cs_ox = k * NDX[best], n->cs_oz = k * NDZ[best];
    }
    *tx = n->sx + n->cs_ox, *tz = n->sz + n->cs_oz;
}

/* lane/nleg (nether_dig_refill): lava beside the open cell (x, y, z) on a side or over it (lava under a cell does not
 * flow up into it), the cell in the eye's view: the lava's glow lights the cell a player looks into, as lava_sense
 * light's rule has it for that lava (the lava itself may be out of the eye's line, round the cell's edge) */
static int nt_lava_glow(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int D[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}};
    int lava = 0;
    for (int k = 0; k < 5 && !lava; ++k)
    {
        int b = netherview_block(v->p, nt_id(v, e), x + D[k][0], y + D[k][1], z + D[k][2], NULL);
        lava = b == 10 || b == 11;
    }
    return lava && lava_seen_1(v, e, x, y, z);
}

/* the cells under a missing floor (fl open, the column above it clear: in view down it): the drop to a solid,
 * lava-free landing, 1 .. NT_DROP, else 0 */
static int nt_drop(const struct neth_vec *v, const struct vec_env *e, int x, int fl, int z)
{
    for (int k = 1; k <= NT_DROP; ++k)
    {
        int b = nt_block(v, e, x, fl - k, z);
        if (b < 0 || nt_lava(b) || b == 90) return 0;
        if (!nt_open(b)) return nt_lava(nt_block(v, e, x, fl - k + 1, z)) ? 0 : k;
    }
    return 0;
}

/* a cell this episode sealed (lava behind it) */
static int nt_sealed(const struct nt_env *n, int x, int y, int z)
{
    for (int i = 0; i < NT_SEALS && i < n->nsealed; ++i)
        if (n->sealed[i][0] == x && n->sealed[i][1] == y && n->sealed[i][2] == z) return 1;
    return 0;
}

/* lava the player sees beside a cell about to be dug: a lava neighbour of (x, y, z) whose face borders an open cell
 * within a block of the player's own two (seen through the tunnel's open space); dug, the cell lets it in */
static int nt_lava_by(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z, int fx, int fy, int fz)
{
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 6; ++i)
    {
        if (i == 3) continue;                   /* lava under the cell does not flow up into it */
        int lx = x + D[i][0], ly = y + D[i][1], lz = z + D[i][2], b = nt_block(v, e, lx, ly, lz);
        if (b != 10 && b != 11) continue;
        for (int j = 0; j < 6; ++j)
        {
            int ox = lx + D[j][0], oy = ly + D[j][1], oz = lz + D[j][2];
            if (abs(ox - fx) <= 1 && oy >= fy - 1 && oy <= fy + 2 && abs(oz - fz) <= 1 &&
                nt_open(nt_block(v, e, ox, oy, oz)))
                return 1;
        }
    }
    return 0;
}

static int nt_dig_face_unsafe(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int k = 0; k < 6; ++k)
    {
        int b = netherview_oracle_block(v->p, nt_id(v, e), x + d[k][0], y + d[k][1], z + d[k][2]);
        if (b < 0 || b == 10 || b == 11 || b == 51) return 1;
    }
    return 0;
}

static int nt_far_hot_by(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    static const int d[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int dy = 0; dy <= 1; ++dy)
        for (int k = 0; k < 5; ++k)
        {
            int b = netherview_oracle_block(v->p, nt_id(v, e), x + d[k][0], y + dy, z + d[k][1]);
            if (b < 0 || b == 10 || b == 11 || b == 51) return 1;
        }
    return 0;
}

static int nt_far_solid(int b) { return b > 0 && !nt_open(b) && b != 8 && b != 9 && b != 10 && b != 11 && b != 51 && b != 90; }

static int nt_far_open_faces(const struct neth_vec *v, const struct vec_env *e,
                             int x, int y, int z, int dx, int dz)
{
    int sx = dz != 0, sz = dx != 0, open = 0;
    int roof = netherview_oracle_block(v->p, nt_id(v, e), x, y + 2, z);
    open += !nt_far_solid(roof);
    for (int side = -1; side <= 1; side += 2)
        for (int dy = 0; dy <= 1; ++dy)
        {
            int b = netherview_oracle_block(v->p, nt_id(v, e), x + side * sx, y + dy, z + side * sz);
            open += !nt_far_solid(b);
        }
    return open;
}

enum { NM_WALK, NM_BRIDGE, NM_PILLAR, NM_DIG };      /* lane/nether2: the walker's travel mode (the books') */

/* lane/nether3: a cell the player's fill went into (remembered: a pillar of nether brick is not the fortress) */
static void nt_placed(struct nt_env *n, int x, int y, int z)
{
    int i = n->nplaced++ & 63;
    n->placed[i][0] = (int16_t)x, n->placed[i][1] = (int16_t)y, n->placed[i][2] = (int16_t)z;
}
static int nt_own(const struct nt_env *n, int x, int y, int z)
{
    for (int i = 0; i < 64 && i < n->nplaced; ++i)
        if (n->placed[i][0] == x && n->placed[i][1] == y && n->placed[i][2] == z) return 1;
    return 0;
}

/* lane/nether2: the hotbar slot of a block to fill with: cobblestone, else netherrack (dug on the way), -1 none */
static int nt_fill_slot(const struct neth_vec *v, const struct neth_rl_state *s)
{
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count > 0) return i;
    for (int i = 0; i < 9; ++i)
        if ((s->inv[i].item == 87 || (NT_FAN && s->inv[i].item == 112)) && s->inv[i].count > 0)
            return i;   /* netherrack (lane/nether3: or nether brick, dug from a fortress) */
    return -1;
}

/* lane/nether3: the fill blocks (cobblestone, netherrack) kept in the hotbar: fewer than NT_FILL_LOW there and a stack
 * of either in the main inventory, the inventory opened (its key) and the stack swapped into a hotbar slot with a
 * number key's click (mode 2), then closed: the player's own way. The slot: the fill's own smallest stack, else an
 * empty one, else the least wanted (no tool, weapon, food, rod). 1 the step taken */
#define NT_FILL_LOW 16
#define NT_FORT_STAIR MOPT(MO_NT_FORT_STAIR)    /* lane/nether5: nt_walk */
#define NT_FORT_REJOIN MOPT(MO_NT_FORT_REJOIN)  /* lane/nether5: nt_drive1's fortress search */
#define NT_SKEL_FIGHT MOPT(MO_NT_SKEL_FIGHT)    /* lane/nether5: nt_foe's skeletons, the hurt glance */
#define NT_REJOIN_DEEP 4       /* lane/nether5: blocks under fort_ty on the fortress's own blocks: pillared up too */
#define NT_REJOIN_DROP 2      /* lane/nether5: blocks under the fortress's height (fort_ty) off its blocks: pillared up */
#define NT_PICK_FETCH MOPT(MO_NT_PICK_FETCH)  /* lane/nether4: the best pickaxe swapped into the hotbar when it holds none */
#define NT_REFILL_WAIT 10       /* steps for the inventory screen to open before giving up a while */
#define NT_REFILL_OFF 200
static int nt_hotbar_fill(const struct neth_rl_state *s)
{
    int k = 0;
    for (int i = 0; i < 9; ++i)
        if ((s->inv[i].item == COBBLE || s->inv[i].item == 87 || s->inv[i].item == 112) && s->inv[i].count > 0)
            k += s->inv[i].count;
    return k;
}
static int nt_refill(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a)
{
    struct nt_env *n = &e->nt;
    int64_t t = e->steps;
    /* lane/nether4: no pickaxe in the hotbar and one in the main inventory: the best one swapped in first (gold-g7's
     * pickaxes sit in the main inventory; nether brick by hand is 200 ticks, and a glance or a look reset it: 14 of 32
     * envs stood on one deck 16,000 steps) */
    int hpick = 0, mpick = -1, hrock = 0, mrock = -1, hcobble = 0, mcobble = -1;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, tier = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
        if (i < 9 && tier > hpick) hpick = tier;
        if (i >= 9 && tier > 0 && (mpick < 0 || tier > v->T.pick[s->inv[mpick].item])) mpick = i;
        if (it == 87 && s->inv[i].count > 0)
        {
            if (i < 9) hrock = 1;
            else if (mrock < 0 || s->inv[i].count > s->inv[mrock].count) mrock = i;
        }
        if (it == COBBLE && s->inv[i].count > 0)
        {
            if (i < 9) hcobble = 1;
            else if (mcobble < 0 || s->inv[i].count > s->inv[mcobble].count) mcobble = i;
        }
    }
    int want_pick = NT_PICK_FETCH && hpick == 0 && mpick >= 0;
    int far_low = MOPT(MO_NT_FAR_DIG) == 2 && n->has_loc && !n->has_fort;
    int want_cobble = far_low && !hcobble && mcobble >= 0;
    int want_rock = far_low && !hrock && mrock >= 0 && !want_cobble;
    if (n->refill_at == 0 && ((t < n->refill_off && !want_rock && !want_cobble) ||
                              (nt_hotbar_fill(s) >= NT_FILL_LOW && !want_pick && !want_rock && !want_cobble))) return 0;
    int src = want_pick ? mpick : want_cobble ? mcobble : want_rock ? mrock : -1;
    for (int i = 9; i < 36 && !want_pick && !want_rock && !want_cobble; ++i) /* largest ordinary fill stack */
        if ((s->inv[i].item == COBBLE || s->inv[i].item == 87 || s->inv[i].item == 112) && s->inv[i].count > 0 &&
            (src < 0 || s->inv[i].count > s->inv[src].count))
            src = i;
    if (src < 0 || (n->refill_at > 0 && t - n->refill_at > NT_REFILL_WAIT))
    {
        n->refill_at = 0, n->refill_off = t + NT_REFILL_OFF;
        if (r->screen == PS_INVENTORY) a->nops = 1, a->ops[0] = CLOSE;
        return r->screen == PS_INVENTORY;
    }
    still(a);
    if (r->screen == PS_INVENTORY)
    {
        int to = -1, best = 1 << 30;
        for (int i = 0; i < 9; ++i)
        {
            int it = s->inv[i].item, c = s->inv[i].count, itc = it < 0 ? 0 : it > 4095 ? 4095 : it, score;
            if (it < 0 || c <= 0) score = 1;
            else if (it == COBBLE || it == 87 || it == 112) score = c / 8;
            else if (v->T.pick[itc] || v->T.axe[itc] || v->T.shovel[itc] || it == 268 || it == 272 || it == 267 ||
                     it == 276 || it == 283 || it == BLAZE_ROD)
                score = 1000;
            else if (it == 260 || it == 297 || it == 319 || it == 320 || it == 363 || it == 364 || it == 365 ||
                     it == 366 || it == 349 || it == 350 || it == 357 || it == 391 || it == 393 || it == 400 || it == 282)
                score = 500;
            else score = 100;
            if (score < best) best = score, to = i;
        }
        a->nops = 2;
        a->ops[0] = click(inv_slot(src, 0), to, 2);
        a->ops[1] = CLOSE;
        n->refill_at = 0, n->refill_off = t + 20;
        return 1;
    }
    if (r->screen == PS_NONE && n->refill_at == 0)
    {
        n->refill_at = t;
        a->press[PK_INVENTORY] = 1;
    }
    return 1;
}

/* lane/rod (nether_entry_fill): the portal builders' walk into a lit portal (portal.c PT_ENTER, bportal.c BO_ENTER)
 * first moves fill blocks into the hotbar when it holds under NT_FILL_LOW of them and the main inventory has a stack
 * (nt_refill: the inventory's own screen): the arrival's bridges need them, and in a portal no screen stays open (the
 * harvested starts came through with 124 to 191 cobblestone, none in the hotbar). 1 the step taken */
static int nt_entry_fill(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (!MOPT(MO_NT_ENTRY_FILL) || !(v->macros & NETH_VEC_NETHER) || r->dim != 0) return 0;
    if (!r->on_ground && e->nt.refill_at == 0 && r->screen == PS_NONE) return 0;
    return nt_refill(v, e, r, s, a);
}

/* lane/nether3: netherrack gathered for the bridges and pillars ahead: below NT_FILL_WANT fill blocks held, on solid
 * ground (not the player's own fill, not over a drop), a netherrack block beside the feet or the head (in front of the
 * face) dug with the best tool, never one with lava in view beside it or over it. 1 the step taken */
#define NT_FILL_WANT 256        /* a bridge of 75 over the lava sea and a pillar of 45 to a deck spent 127 */
#define NT_GATHER_AGE 30        /* steps on one block at most */
static int nt_gather(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!r->on_ground || n->pil_base != 0 || inv_count(s, COBBLE) + inv_count(s, 87) + inv_count(s, 112) >= NT_FILL_WANT)
        return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (n->gather_age > 0 && (fx != n->gax || fz != n->gaz || ++n->gather_age > NT_GATHER_AGE))
        n->gather_age = 0, n->gather_off = e->steps + 40;
    if (e->steps < n->gather_off) return 0;
    for (int k = 0; k < 8; ++k)
    {
        int d = k & 3, y = fy + (k >> 2), cx = fx + NDX[d], cz = fz + NDZ[d];
        int gb = nt_block(v, e, cx, y, cz);
        if ((gb != 87 && (gb != 112 || NT_BRICK_PICK)) || nt_lava_by(v, e, cx, y, cz, fx, fy, fz))
            continue;      /* lane/nether4: netherrack only (the fortress's brick hides its lava) */
        int up = nt_block(v, e, cx, y + 1, cz);
        if (up == 10 || up == 11 || up < 0) continue;
        if (n->gather_age == 0) n->gather_age = 1, n->gax = fx, n->gaz = fz;
        double yaw, pitch;
        look_at(r->x, ey, r->z, cx + 0.5, y + 0.5, cz + 0.5, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (s->mo_hit && s->mo_x == cx && s->mo_y == y && s->mo_z == cz)
        {
            a->hold = 1u << PK_ATTACK;
            int best = nt_tool(v, s, gb);
            if (best >= 0 && best != s->current) a->hotbar = best;
        }
        return 1;
    }
    n->gather_age = 0;
    return 0;
}

/* lava in a cell next to the player's (cx, cy, cz): cobblestone placed into it, the crosshair through the lava on the
 * block behind it with the face toward the eye (a placed block replaces the liquid). 1 while it tries (wseal counts
 * the tries), 0 no cobblestone in the hotbar */
static int nt_seal(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey, int cx, int cy, int cz)
{
    static const int OX[6] = {0, 0, 0, 0, -1, 1}, OY[6] = {-1, 1, 0, 0, 0, 0}, OZ[6] = {0, 0, -1, 1, 0, 0};
    int hb = nt_fill_slot(v, s);
    if (hb < 0) return 0;
    e->nt.wseal += 1;
    /* the cell remembered: never dug again this episode (behind it is the pocket) */
    int known = 0;
    for (int i = 0; i < NT_SEALS && i < e->nt.nsealed; ++i)
        known |= e->nt.sealed[i][0] == cx && e->nt.sealed[i][1] == cy && e->nt.sealed[i][2] == cz;
    if (!known)
    {
        int i = e->nt.nsealed++ % NT_SEALS;
        e->nt.sealed[i][0] = cx, e->nt.sealed[i][1] = cy, e->nt.sealed[i][2] = cz;
    }
    a->hotbar = hb;
    double yaw, pitch, tx = cx + 0.5, ty = cy + 0.5, tz = cz + 0.5;
    if (NT_SEAL_AIM)
    {
        /* lane/rod (nether_seal_aim): the crosshair passes through lava, so the cell's centre hits the block behind it,
         * whose face does not border the cell, and no seal ever placed (a harvested start's tunnel at y 32: 16 steps
         * looking at the lava beside the feet, then it flowed in). Aimed instead at a face of a solid block that borders
         * the cell, in view: the top of the block under it, else the face of the one beyond it (away from the eye),
         * else a side one's */
        int fx = (int)floor(r->x), fz = (int)floor(r->z);
        int bx = cx - fx > 0 ? 1 : cx - fx < 0 ? -1 : 0, bz = cz - fz > 0 ? 1 : cz - fz < 0 ? -1 : 0;
        int cand[5][3] = {{0, -1, 0}, {bx, 0, bz}, {bz, 0, bx}, {-bz, 0, -bx}, {0, 1, 0}};
        for (int k = 0; k < 5; ++k)
        {
            int dx = cand[k][0], dy = cand[k][1], dz = cand[k][2];
            if (!dx && !dy && !dz) continue;
            if (dy < 0 && ey <= cy) continue;                         /* a floor's top seen only from over it */
            if (dy > 0 && ey >= cy + 1) continue;                     /* an underside only from under it */
            int b = nt_block(v, e, cx + dx, cy + dy, cz + dz);
            if (b <= 0 || nt_open(b) || nt_lava(b)) continue;
            tx = cx + 0.5 + 0.5 * dx, ty = cy + 0.5 + 0.5 * dy, tz = cz + 0.5 + 0.5 * dz;
            break;
        }
    }
    look_at(r->x, ey, r->z, tx, ty, tz, &yaw, &pitch);
    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
    int side = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
    if (s->mo_hit && s->mo_x + OX[side] == cx && s->mo_y + OY[side] == cy && s->mo_z + OZ[side] == cz &&
        s->current == hb)
    {
        a->look[0] = r->yaw, a->look[1] = r->pitch;
        a->press[PK_USE] = 1;
    }
    return 1;
}

/* the direction the walker tries next from this cell: d0 (the heading held, else the larger axis toward the target),
 * the other axis toward the target, then the side kept (wside: a shore followed one way), the other side, the way
 * back last; -1 every one judged bad */
static int nt_dir(struct nt_env *n, double dx, double dz, int prefer, int open)
{
    int ax = fabs(dx) >= fabs(dz), sx = dx >= 0 ? 3 : 1, sz = dz >= 0 ? 0 : 2;
    int d0 = prefer >= 0 ? prefer : ax ? sx : sz;
    int d1 = prefer >= 0 ? -1 : ax ? (fabs(dz) > 0.5 ? sz : -1) : (fabs(dx) > 0.5 ? sx : -1);
    if (n->wside == 0) n->wside = 1;
    int order[5] = {d0, d1, (d0 + n->wside + 4) & 3, (d0 - n->wside + 4) & 3, (d0 + 2) & 3};
    /* lane/nether: a way open now (nothing to dig: no lava pocket opened) before one that is dug, never back */
    /* lane/nether2: never the open way back into the cell just left (two cells each other's open side: a walker
     * stepped between them for thousands of steps) */
    int back = n->warr >= 0 ? (n->warr + 2) & 3 : -1;
    if (!((open >> d0) & 1))
        for (int i = 1; i < 4; ++i)
            if (order[i] >= 0 && order[i] != back && !((n->wtried >> order[i]) & 1) && ((open >> order[i]) & 1))
                return order[i];
    for (int i = 0; i < 5; ++i)
    {
        int d = order[i];
        if (d < 0 || ((n->wtried >> d) & 1)) continue;
        if (i == 3) n->wside = -n->wside;   /* the kept side was bad here: the other kept from now on */
        return d;
    }
    return -1;
}

/* lane/nleg (nether_dig_refill): the walker's newest dug cells, a ring of NT_RDUG; a cell leaves it NT_RDUG_AGE steps
 * after its dig, when it is no longer open (filled, or lava come in: the walker's own seal then), or past NT_RDUG_D
 * blocks from the feet */
#define NT_RDUG 4
#define NT_RDUG_AGE 100         /* steps a dug cell is watched (lava flows a cell each 10 ticks in the Nether) */
#define NT_RDUG_D 4.0           /* blocks from the eye a watched cell may be (a seal's reach) */
static void nt_rdug_add(struct nt_env *n, int x, int y, int z, int64_t t)
{
    for (int i = 0; i < n->nrdug; ++i)
        if (n->rdug[i][0] == x && n->rdug[i][1] == y && n->rdug[i][2] == z)
        {
            n->rdug_t[i] = t;
            return;
        }
    if (n->nrdug == NT_RDUG)
    {
        memmove(n->rdug[0], n->rdug[1], (NT_RDUG - 1) * sizeof n->rdug[0]);
        memmove(&n->rdug_t[0], &n->rdug_t[1], (NT_RDUG - 1) * sizeof n->rdug_t[0]);
        memmove(&n->rdug_k[0], &n->rdug_k[1], (NT_RDUG - 1) * sizeof n->rdug_k[0]);
        n->nrdug -= 1;
    }
    n->rdug[n->nrdug][0] = x, n->rdug[n->nrdug][1] = y, n->rdug[n->nrdug][2] = z;
    n->rdug_k[n->nrdug] = 0;
    n->rdug_t[n->nrdug++] = t;
}

/* a watched dug cell, open, in view, with lava's glow from a side or over it (nt_lava_glow): filled again with the fill
 * block (nt_seal, which also keeps it from being dug again) before anything else the walker does; the lava would flow
 * in at its next tick. A cell the player's box is in is left (the escape's). 1 the step taken */
static int nt_refill_ring(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    for (int i = 0; i < n->nrdug;)
    {
        int *c = n->rdug[i];
        int open = nt_open(nt_block(v, e, c[0], c[1], c[2]));
        if (e->steps - n->rdug_t[i] > NT_RDUG_AGE || (!open && e->steps - n->rdug_t[i] > 2) ||
            n->rdug_k[i] > NT_PLACE_TRIES || py_dist3(r->x, ey, r->z, c[0] + 0.5, c[1] + 0.5, c[2] + 0.5) > NT_RDUG_D)
        {
            memmove(n->rdug[i], n->rdug[i + 1], (n->nrdug - i - 1) * sizeof n->rdug[0]);
            memmove(&n->rdug_t[i], &n->rdug_t[i + 1], (n->nrdug - i - 1) * sizeof n->rdug_t[0]);
            memmove(&n->rdug_k[i], &n->rdug_k[i + 1], (n->nrdug - i - 1) * sizeof n->rdug_k[0]);
            n->nrdug -= 1;
            continue;
        }
        int inbox = c[1] >= (int)floor(r->y) && c[1] <= (int)floor(r->y + 1.8) &&
                    c[0] >= (int)floor(r->x - 0.3) && c[0] <= (int)floor(r->x + 0.3) &&
                    c[2] >= (int)floor(r->z - 0.3) && c[2] <= (int)floor(r->z + 0.3);
        if (open && !inbox && nt_lava_glow(v, e, c[0], c[1], c[2]) && nt_seal(v, e, r, s, a, ey, c[0], c[1], c[2]))
        {
            n->rdug_k[i] += 1;
            n->refills += n->rdug_k[i] == 1;    /* a cell counted once */
            n->wk = -1;                         /* the column judged again once it is filled (sealed: turned from) */
            return 1;
        }
        ++i;
    }
    return 0;
}

/* the walker toward cell (tx, ty, tz): 1 within `near` blocks of it horizontally (and 2 vertically), 0 going on,
 * -1 no way on from here (every direction judged bad). prefer: a direction to hold (the search's heading), -1 none.
 * A column at a time, as the strip walks: its cells (level: head and feet; up: over the head, then the column's two
 * over the floor; down: the column's head, feet and below) cleared, dug when solid (the crosshair on it, the best
 * tool), the floor judged, then walked into */
static int nt_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey, int tx, int ty, int tz, double near, int prefer)
{
    struct nt_env *n = &e->nt;
    /* the feet's cell: soul sand's top is 7/8 up its cell (+0.13 puts the feet over it); in the air the last one's */
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (!r->on_ground && n->wk >= 0) fy = n->wby;
    /* lane/nether2: bridging, the middle past the brink over the gap is still the cell bridged from */
    if (n->wk == 5 && n->wplace > 0 && fx == n->wcx && fz == n->wcz) fx = n->wbx, fz = n->wbz;
    int64_t t = e->steps;
    a->look_mode = 1;
    if (NT_DIG_REFILL && nt_refill_ring(v, e, r, s, a, ey)) return 0;   /* lane/nleg */
    double hx = tx + 0.5 - r->x, hz = tz + 0.5 - r->z;
    if (py_hypot2(hx, hz) <= near && abs(ty - fy) <= 2) return 1;
    if (fx != n->wbx || fy != n->wby || fz != n->wbz)
    {
        n->wtried = 0;                      /* a new cell: every direction worth a try again */
        /* lane/nether2: the way it came in (a step to a side cell: its direction; else none) */
        int ddx = fx - n->wbx, ddz = fz - n->wbz;
        n->warr = abs(ddx) + abs(ddz) == 1 ? (ddz == 1 ? 0 : ddx == -1 ? 1 : ddz == -1 ? 2 : 3) : -1;
        n->wbx = fx, n->wby = fy, n->wbz = fz;
        n->aseal = 0, n->wlevel = 0, n->backs = 0;
        if (r->on_ground && !nt_own(n, fx, fy - 1, fz)) n->sbr = 0;   /* lane/rod (surface): off the bridge */
        if (n->wk >= 0 && n->wk != 3) n->wk = -1;
    }
    if (n->wk < 0)
    {
        int open = 0;
        for (int d = 0; d < 4; ++d)
        {
            int cx = fx + NDX[d], cz = fz + NDZ[d], fb = nt_block(v, e, cx, fy - 1, cz);
            open |= (nt_open(nt_block(v, e, cx, fy, cz)) && nt_open(nt_block(v, e, cx, fy + 1, cz)) && !nt_open(fb) &&
                     !nt_lava(fb) && fb >= 0 && fb != 90) << d;
        }
        int d = nt_dir(n, hx, hz, prefer, open);
        if (d < 0 && !n->wlevel)
        {
            /* no stair from here every way: the level moves tried before giving up */
            n->wlevel = 1, n->wtried = 0;
            d = nt_dir(n, hx, hz, prefer, open);
        }
        if (d < 0) return -1;
        n->wdir = d;
        n->wcx = fx + NDX[d], n->wcz = fz + NDZ[d], n->wcy = fy;
        n->wmode = n->wlevel ? 0 : ty > fy ? 1 : ty < fy ? -1 : 0;
        n->wk = 0, n->wlook = (int)t, n->wgain = (int)t, n->wplace = 0, n->whead = 0, n->wstill = 0, n->wseal = 0;
    }
#ifdef NT_TRACE
#define NT_BAD_WHY()                                                                                                   \
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)                               \
        fprintf(stderr, "nt %lld bad at line %d\n", (long long)e->steps, __LINE__)
#else
#define NT_BAD_WHY()
#endif
#define NT_BAD()                                                                                                       \
    do {                                                                                                               \
        NT_BAD_WHY();                                                                                                  \
        n->wtried |= 1 << n->wdir;                                                                                     \
        n->wk = -1;                                                                                                    \
        return 0;                                                                                                      \
    } while (0)
    if (t - n->wgain > NT_CELL_AGE) NT_BAD();
    /* a lava source on the liquid look within 3 blocks, at the next column or beside it: not that way */
    /* lane/nether3: only lava at the feet's height or over it: lava under the way (the lava sea's top under a bridge,
     * seen from its brink) never reaches a player standing over it */
    if (s->lq_hit && (s->lq_block == 10 || s->lq_block == 11) &&
        py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) < 3.0 &&
        abs(s->lq_x - n->wcx) + abs(s->lq_z - n->wcz) <= 1 && (!NT_FAN || s->lq_y >= n->wcy))
        NT_BAD();
    int cy = n->wcy, m = n->wmode, ncell = m == 0 ? 2 : 3;
    int cells[4][3];
    if (m == 0 && n->whead)
    {
        /* a level walk that did not move: the head room over both columns cleared too (a jump over a lip) */
        cells[0][0] = fx, cells[0][1] = cy + 2, cells[0][2] = fz;
        cells[1][0] = n->wcx, cells[1][1] = cy + 2, cells[1][2] = n->wcz;
        cells[2][0] = n->wcx, cells[2][1] = cy + 1, cells[2][2] = n->wcz;
        cells[3][0] = n->wcx, cells[3][1] = cy, cells[3][2] = n->wcz;
        ncell = 4;
    }
    else if (m > 0)
    {
        cells[0][0] = fx, cells[0][1] = cy + 2, cells[0][2] = fz;
        cells[1][0] = n->wcx, cells[1][1] = cy + 2, cells[1][2] = n->wcz;
        cells[2][0] = n->wcx, cells[2][1] = cy + 1, cells[2][2] = n->wcz;
    }
    else
        for (int k = 0; k < 3; ++k) cells[k][0] = n->wcx, cells[k][1] = cy + 1 - k, cells[k][2] = n->wcz;
    int fl = m > 0 ? cy : m < 0 ? cy - 2 : cy - 1;
    double yaw, pitch;
    while (n->wk < ncell)
    {
        int *c = cells[n->wk], b = nt_block(v, e, c[0], c[1], c[2]);
        if ((b == 10 || b == 11) && n->wseal < NT_PLACE_TRIES && nt_seal(v, e, r, s, a, ey, c[0], c[1], c[2]))
            return 0;                           /* lava come into the cell (a pocket dug open): sealed with cobblestone */
        if (nt_lava(b) || b == 90 || b < 0) NT_BAD();           /* lava, fire, a portal, not loaded */
        if (nt_sealed(n, c[0], c[1], c[2])) NT_BAD();           /* sealed: digging it would open the pocket again */
        if (nt_lava_by(v, e, c[0], c[1], c[2], fx, fy, fz)) NT_BAD();   /* lava in view beside it */
        if (nt_open(b))
        {
            n->wk += 1, n->wlook = (int)t;      /* clear: the next cell at once */
            continue;
        }
        if (b == 7 || b == OBSIDIAN || b == 52) NT_BAD();        /* bedrock, obsidian, the spawner */
        if (MOPT(MO_NT_DIG_FACES) && nt_dig_face_unsafe(v, e, c[0], c[1], c[2])) NT_BAD();
        /* lane/rod (nether_safe_dig): nor a block under the player's own box (its corners 0.3 out: a stair down from a
         * cell's edge digs the next column's top, which the box may stand on; at gold-g7's deck at y 74 that was the
         * last block under it and 118 of 128 frozen runs fell 50 blocks into the lava sea at one step) */
        if (NT_SAFE_DIG && c[1] < (int)floor(r->y + 0.13) && r->on_ground)
            for (int k = 0; k < 4; ++k)
                if ((int)floor(r->x + (k & 1 ? 0.3 : -0.3)) == c[0] && (int)floor(r->z + (k & 2 ? 0.3 : -0.3)) == c[2] &&
                    (int)floor(r->y + 0.13) - 1 == c[1] && (NT_SAFE_DIG == 1 || nt_deep(v, e, c[0], c[1], c[2])))
                    NT_BAD();
        if (m != 0 && (n->phase == NT_SEARCH || n->phase == NT_HOME))
        {
            /* lane/nether2: travelling, a stair is never dug (the cells over the head hide lava that falls in, the
             * cells under the feet lava to fall into): a natural step only, else the level walk this column. In the
             * fortress (its brick, toward the spawner) stairs are dug as before */
            n->wmode = 0, n->wk = 0, n->wlook = (int)t;
            return 0;
        }
        if (NT_SURF && n->phase == NT_SEARCH)
        {
            /* lane/rod (nether_search surface): nothing dug on the search's way. A block at the next column's feet
             * with the two cells over it and the head room over the player open is a natural step up (the stair's
             * cells, all open); anything else in the way is turned from */
            if (m == 0 && !n->whead && c[0] == n->wcx && c[1] == cy && c[2] == n->wcz &&
                nt_open(nt_block(v, e, n->wcx, cy + 1, n->wcz)) && nt_open(nt_block(v, e, n->wcx, cy + 2, n->wcz)) &&
                nt_open(nt_block(v, e, fx, cy + 2, fz)))
            {
                n->wmode = 1, n->wk = 0, n->wlook = (int)t;
                return 0;
            }
            n->surf_refused += 1;
            NT_BAD();
        }
        if (c[0] == fx && c[2] == fz && (n->phase == NT_SEARCH || n->phase == NT_HOME))
            NT_BAD();                              /* lane/nether2: nor the cell over the player's own head */
        /* lane/rod (nether_oracle): a cell the oracle's path builds was checked for lava on the server's own blocks */
        if (!n->or_build && (n->phase == NT_FORT || n->phase == NT_CAMP) && b != 112 && b != 113 && b != 114 &&
            !nt_own(n, c[0], c[1], c[2]))   /* lane/nether4: or the player's own cover */
            NT_BAD();
        /* lane/nether4: nor, in the fortress, any of its own blocks (its open ways walked, a wall turned from): by hand
         * no brick dig ever finished (200 ticks, reset by any look away: 14 of 32 envs held one from step 7,377 to the
         * end), and dug with a pickaxe the walls and floors let in the lava behind them (24 of 32 envs at one step) */
        if (NT_BRICK_PICK && !n->or_build && (n->fort_at || n->phase == NT_CAMP) && !nt_own(n, c[0], c[1], c[2]))
        {
            if (NT_FORT_STAIR && m != 0)
            {
                /* lane/nether5: a stair at the fortress whose cell is its brick: the level walk this column (gold-g7's
                 * bridge: the walk along its wall on the player's own fill, every stair down onto the wall's top a
                 * brick to dig, the way judged bad until the player fell off into the netherrack under the deck) */
                n->wmode = 0, n->wk = 0, n->wlook = (int)t;
                return 0;
            }
            NT_BAD();      /* lane/nether2: at the fortress only its own blocks are dug (the netherrack round it hid lava) */
        }
        {
            double px = c[0] + 0.5, py = c[1] + 0.5, pz = c[2] + 0.5;
            if (NT_DIG_AIM)
            {
                /* lane/nleg (nether_dig_aim): the centre's ray landed on another block (s1endrank10: a stair up's head
                 * cell behind the edge of its feet cell, the attack never pressed for 26,000 steps): the centre of the
                 * nearest face the eye is outside of whose neighbour is open, held until the cell changes */
                int same = n->faim && n->fax == c[0] && n->fay == c[1] && n->faz == c[2];
                if (!same && s->mo_hit && !(s->mo_x == c[0] && s->mo_y == c[1] && s->mo_z == c[2]))
                    n->faim = 1, n->fax = c[0], n->fay = c[1], n->faz = c[2], same = 1;
                if (same)
                {
                    double eye[3] = {r->x, ey, r->z}, best = -1;
                    for (int f = 0; f < 6; ++f)
                    {
                        int ax = f >> 1, sg = f & 1 ? -1 : 1, nb[3] = {c[0], c[1], c[2]};
                        double face = c[ax] + (sg > 0 ? 1.0 : 0.0);
                        if (sg > 0 ? eye[ax] <= face : eye[ax] >= face) continue;   /* the eye inside that side */
                        nb[ax] += sg;
                        if (!nt_open(nt_block(v, e, nb[0], nb[1], nb[2]))) continue;
                        double q[3] = {c[0] + 0.5, c[1] + 0.5, c[2] + 0.5};
                        q[ax] = face - sg * NT_FACE_IN;
                        double d = py_dist3(eye[0], eye[1], eye[2], q[0], q[1], q[2]);
                        if (best < 0 || d < best) best = d, px = q[0], py = q[1], pz = q[2];
                    }
                }
            }
            look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
        }
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (s->mo_hit && s->mo_x == c[0] && s->mo_y == c[1] && s->mo_z == c[2])
        {
            n->wgain = (int)t;                  /* digging: progress */
            if (NT_DIG_REFILL) nt_rdug_add(n, c[0], c[1], c[2], t);   /* lane/nleg */
            n->mode = NM_DIG;
            n->dig_at = t;                      /* lane/nether4: no glance mid-dig (it resets the block's damage) */
            a->hold = 1u << PK_ATTACK;
            int best = nt_tool(v, s, b);
            if (best >= 0 && best != s->current) a->hotbar = best;
        }
        return 0;
    }
    if (n->wk == ncell) n->wk = 5;
    if (n->wk == 5)
    {
        int b = nt_block(v, e, n->wcx, fl, n->wcz);
        int wet = s->lq_hit && (s->lq_block == 10 || s->lq_block == 11) && s->lq_x == n->wcx && s->lq_z == n->wcz &&
                  s->lq_y >= fl && s->lq_y <= cy + 2;
        /* lane/nether2: lava as the floor (the lava sea's top, the floor cell in view) is bridged over as a gap is */
        int lfloor = b == 10 || b == 11;
        if (b == 51 || (wet && !(lfloor && s->lq_y == fl))) NT_BAD();
        if (n->deck_only && b != 112 && b != 114) NT_BAD();   /* lane/rod (nether_blaze_seek deck): brick only */
        if (NT_SURF && n->phase == NT_SEARCH && (lfloor || fl < NT_SEA_TOP))
        {
            n->surf_refused += 1;   /* lane/rod (surface): never down to the lava sea's shore, nor over the lava */
            NT_BAD();
        }
        if (NT_FAN && b == 88)
            for (int d = 0; d < 4; ++d)
            {
                /* lane/nether3: soul sand beside lava at its own height: a player's box over its edge is in the lava */
                int lb = nt_block(v, e, n->wcx + NDX[d], fl, n->wcz + NDZ[d]);
                if (lb == 10 || lb == 11) NT_BAD();
            }
        if (!nt_open(b) && !lfloor && b != 90 && b >= 0)
        {
            n->wk = 3, n->wlook = (int)t;
            /* the walk begins this step */
        }
        else if (m != 0)
        {
            /* no floor for a stair: the level walk instead */
            n->wmode = 0, n->wk = 0, n->wlook = (int)t;
            return 0;
        }
        else if (!lfloor && n->wplace == 0 && nt_drop(v, e, n->wcx, fl, n->wcz) > 0)
        {
            /* the floor missing over a short drop (its landing in view down the clear column): walked off */
            n->wk = 3, n->wlook = (int)t;
        }
        else
        {
            /* the floor missing: cobblestone placed in it where the crosshair's face puts it there */
            static const int OX[6] = {0, 0, 0, 0, -1, 1}, OY[6] = {-1, 1, 0, 0, 0, 0}, OZ[6] = {0, 0, -1, 1, 0, 0};
            int hb = nt_fill_slot(v, s);
            if (hb < 0 || ++n->wplace > NT_BRIDGE_TRIES) NT_BAD();
            if (NT_SURF && n->phase == NT_SEARCH && n->sbr >= NT_SURF_GAP)
            {
                n->surf_refused += 1;          /* lane/rod (surface): a gap wider than NT_SURF_GAP turned from */
                NT_BAD();
            }
            /* lane/nether3: never to the brink of lava from soul sand: its top (7/8) is under the lava's surface, and
             * the overhang stands in the lava beside it (24 of 32 burned to death at one shore) */
            if (NT_FAN && lfloor && nt_block(v, e, fx, n->wby - 1, fz) == 88) NT_BAD();
            n->mode = NM_BRIDGE;
            int side = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
            int puts = s->mo_hit && s->mo_x + OX[side] == n->wcx && s->mo_y + OY[side] == fl &&
                       s->mo_z + OZ[side] == n->wcz;
            if (hb != s->current) a->hotbar = hb;
            a->hold |= 1u << PK_SNEAK;
            if (NT_BRINK_FLOOR && r->on_ground)
            {
                /* lane/nleg (nether_brink_floor): the player's centre over an open cell, the box held crouched on a
                 * neighbour's edge (e44b, k20: (-35.3, 75, -161.2) at the brink of the player's own bridge, the cell
                 * under the centre air): the look below meets the side of that air cell, never a face, and every
                 * direction was tried and turned from to the bound. The cell under the centre filled first, on the
                 * face of a solid neighbour that borders it; with none (k20: the box's corner on a diagonal block,
                 * (-35.3, 75, -161.3)), first a cell beside it that borders a solid block */
                int ux = (int)floor(r->x), uy = (int)floor(r->y + 0.13) - 1, uz = (int)floor(r->z);
                int ub = nt_block(v, e, ux, uy, uz);
                if (!(ux == n->wcx && uz == n->wcz) && nt_open(ub) && !nt_lava(ub))
                {
                    /* the farthest such face from the eye, the eye on its front side (the eye straight over a face's
                     * edge sees the neighbour's top first): the ray to a point NT_BRINK_IN inside it, low in the cell,
                     * crosses the face's plane inside the cell's height while the eye is NT_BRINK_D or more off it;
                     * nearer, a step back */
                    int sd = -1, tx = ux, tz = uz;
                    double best = 0;
                    for (int pass = 0; pass < 2 && sd < 0; ++pass)
                        for (int t = pass ? 0 : 3; t < 4; ++t)
                        {
                            int cx = pass ? ux + NDX[t] : ux, cz = pass ? uz + NDZ[t] : uz;
                            int cb = nt_block(v, e, cx, uy, cz);
                            if (pass && (!nt_open(cb) || nt_lava(cb))) continue;
                            for (int d = 0; d < 4; ++d)
                            {
                                if (pass && d == ((t + 2) & 3)) continue;   /* the centre's own cell */
                                int nb = nt_block(v, e, cx + NDX[d], uy, cz + NDZ[d]);
                                if (nt_open(nb) || nb < 0 || nt_lava(nb) || nb == 90) continue;
                                double dd = NDX[d] ? (cx + 0.5 + 0.5 * NDX[d] - r->x) * NDX[d]
                                                   : (cz + 0.5 + 0.5 * NDZ[d] - r->z) * NDZ[d];
                                if (dd <= 0) continue;
                                if (sd < 0 || dd > best) sd = d, best = dd, tx = cx, tz = cz;
                            }
                        }
                    if (sd >= 0)
                    {
                        int fills = s->mo_hit && s->mo_x + OX[side] == tx && s->mo_y + OY[side] == uy &&
                                    s->mo_z + OZ[side] == tz;
                        if (fills && s->current == hb)
                        {
                            a->look[0] = r->yaw, a->look[1] = r->pitch;
                            a->press[PK_USE] = 1;
                            nt_placed(n, tx, uy, tz);
                            return 0;
                        }
                        look_at(r->x, ey, r->z, tx + 0.5 + (0.5 + NT_BRINK_IN) * NDX[sd], uy + NT_BRINK_Y,
                                tz + 0.5 + (0.5 + NT_BRINK_IN) * NDZ[sd], &yaw, &pitch);
                        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                        if (best < NT_BRINK_D) a->hold |= 1u << PK_BACK;   /* crouched: the box keeps its edge */
                        return 0;
                    }
                }
            }
            if (puts && s->current == hb)
            {
                a->look[0] = r->yaw, a->look[1] = r->pitch;      /* the look that found the face kept */
                a->press[PK_USE] = 1;
                nt_placed(n, n->wcx, fl, n->wcz);
                n->sbr += 1;                   /* lane/rod (surface): the gap's cells bridged in a row */
                return 0;
            }
            /* lane/nether2: a player's bridge: crouched to the brink (a crouched player never walks off), the
             * middle over the gap by NT_BRINK, then the look back down at the side of the block underfoot that faces
             * the gap (from the middle of a cell the ray met that block's top face: no bridge was ever placed) */
            int d = n->wdir;
            double over = NDX[d] ? (r->x - (fx + 0.5 + 0.5 * NDX[d])) * NDX[d] : (r->z - (fz + 0.5 + 0.5 * NDZ[d])) * NDZ[d];
            if (!r->on_ground) return 0;       /* in the air (a jump's end): crouched, still (crouching holds no edge) */
            if (over < NT_BRINK && n->wplace < NT_BRIDGE_TRIES / 2)
            {
                look_at(r->x, ey, r->z, n->wcx + 0.5, ey - 1.0, n->wcz + 0.5, &yaw, &pitch);
                a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                a->hold |= 1u << PK_FORWARD;
                return 0;
            }
            look_at(r->x, ey, r->z, fx + 0.5 + 0.5 * NDX[d], fl + 0.5, fz + 0.5 + 0.5 * NDZ[d], &yaw, &pitch);
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            return 0;
        }
    }
    /* every cell clear: the walk into the column (a jump up a stair), then the next column; lava come into one of
     * its cells meanwhile judged again (sealed) before a step more */
    for (int k = 0; k < ncell; ++k)
        if (nt_lava(nt_block(v, e, cells[k][0], cells[k][1], cells[k][2])))
        {
            n->wk = 0, n->wlook = (int)t;
            return 0;
        }
    if (fx == n->wcx && fz == n->wcz)
    {
        n->wk = -1, n->wgain = (int)t;
        return 0;
    }
    look_at(r->x, ey, r->z, n->wcx + 0.5, ey - 0.3 + (m > 0), n->wcz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
    a->hold = 1u << PK_FORWARD;
    n->mode = NM_WALK;
    if (m == 0 && !nt_open(nt_block(v, e, n->wcx, fl, n->wcz)))
    {
        /* lane/nether2: a level walk onto a floor with an edge (open or lava beside it, in view from over it) is
         * crouched: a crouched player never walks off a block (falls off bridges and pillars into the lava sea) */
        for (int d = 0; d < 4; ++d)
        {
            int b = nt_block(v, e, n->wcx + NDX[d], fl, n->wcz + NDZ[d]);
            if (b == 0 || b == 10 || b == 11 || b == 51) a->hold |= 1u << PK_SNEAK;
        }
        if ((a->hold & (1u << PK_SNEAK)) && !r->on_ground) a->hold &= ~(1u << PK_FORWARD);   /* no edge held aloft */
    }
    /* a jump up a stair, or when the walk has not moved for NT_STILL steps (a lip the step height misses) */
    n->wstill = py_hypot2(r->x - n->wpx, r->z - n->wpz) < 0.02 ? n->wstill + 1 : 0;
    n->wpx = r->x, n->wpz = r->z;
    /* The planner's two-high tunnel is walkable at feet and head height. Jumping under its solid third block kept
     * seed 1 at (33.30,86,-47.70) from step 2,500 through 5,000 while the next level cell was open. */
    int tunnel_ceiling = n->opath_tun && m == 0 && nt_open(nt_block(v, e, fx, fy + 1, fz)) &&
                         !nt_open(nt_block(v, e, fx, fy + 2, fz));
    if ((m > 0 || n->wstill >= NT_STILL) && r->on_ground &&
        (!NT_FAN || m > 0 || !(a->hold & (1u << PK_SNEAK))) && !tunnel_ceiling)  /* never a jump at an edge or ceiling */
        a->hold |= 1u << PK_JUMP;
    if (n->wstill >= 2 * NT_STILL && m == 0 && !n->whead && !tunnel_ceiling)
    {
        n->whead = 1, n->wk = 0, n->wlook = (int)t, n->wstill = 0;     /* the head room cleared, then again */
        return 0;
    }
    if (t - n->wlook > 40) n->wk = 0, n->wlook = (int)t;   /* something came back into the way: judged again */
    return 0;
#undef NT_BAD
}

/* lane/nether2: one block up where the player stands (a drop on the way out climbed back): the look straight down, a
 * jump, the fill block placed under the feet at the jump's top (pearl's pillar). 1 a block up, 0 going on, -1 no
 * block to fill with, the cell over the head not open, or NT_PILLAR_AGE steps without it */
static int nt_pillar(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a)
{
    struct nt_env *n = &e->nt;
    int hb = nt_fill_slot(v, s), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (hb < 0 || ++n->pil_age > NT_PILLAR_AGE) return n->pil_base = 0, n->pil_age = 0, -1;
    if (n->pil_base == 0 && !nt_open(nt_block(v, e, fx, fy + 2, fz))) return n->pil_age = 0, -1;
    n->mode = NM_PILLAR;
    a->look_mode = 1;
    a->look[0] = r->yaw, a->look[1] = 90.0f;
    if (hb != s->current) a->hotbar = hb;
    if (n->pil_base == 0)
    {
        if (!r->on_ground) return 0;
        n->pil_base = r->y;
    }
    if (r->on_ground && r->y > n->pil_base + 0.9)
    {
        n->pil_base = 0, n->pil_age = 0;
        return 1;
    }
    a->hold |= 1u << PK_JUMP;
    if (r->y > n->pil_base + 1.05 && s->current == hb && s->mo_hit && s->mo_side == 1)
    {
        a->press[PK_USE] = 1;
        nt_placed(n, s->mo_x, s->mo_y + 1, s->mo_z);
    }
    if (r->on_ground && r->y < n->pil_base + 0.05 && n->pil_age > 4) n->pil_base = 0;    /* came down short: again */
    return 0;
}

/* lane/nether3: a column clear from the feet's cell to `top` (every cell open: in view looking up it) */
static int nt_col_open(const struct neth_vec *v, const struct vec_env *e, int x, int y0, int y1, int z)
{
    for (int y = y0; y <= y1; ++y)
        if (!nt_open(nt_block(v, e, x, y, z))) return 0;
    return 1;
}

/* lane/nether5: the tower from wither skeletons (nether_skel_tower). A wither skeleton (EntitySkeleton type 1, 0.72
 * by 2.34: vis height over NT_WITHER_H) strikes 9 at hard when the player's feet are within its reach (EntityAIAttack-
 * OnCollide: squared distance under (2 * 0.72)^2 + 0.6, about 1.6 blocks), and two at once killed gold-g7's player in
 * the bridge's corridor from 20 health in 34 ticks. One in view within NT_TOWER_D, the column over the head open and
 * fill blocks held: a pillar NT_TOWER_H blocks up (out of its reach, its head within the player's 3), then the fight from
 * the top, crouched, never a step; none in view within NT_TOWER_END_D for NT_TOWER_CLEAR steps (or NT_TOWER_MAX steps
 * in all): down again, the pillar's own blocks dug from the top. 1 the step driven */
#define NT_SKEL_TOWER MOPT(MO_NT_SKEL_TOWER)
#define NT_WITHER_H 2.1         /* a skeleton's height over this: a wither skeleton (2.34; the archer's 1.99) */
#define NT_TOWER_D 10.0         /* a wither skeleton this near (eye to its middle): the tower (a block up is about 10
                                 * steps; begun at 6 the tower was a block short when the first one arrived) */
#define NT_TOWER_H 3            /* blocks over the highest one's feet (and over the player's own at least): its reach
                                 * is 1.6 over its feet and it jumps 1.25 as it paths (2 left gold-g7's player struck) */
#define NT_TOWER_MAXH 4         /* blocks up at most */
#define NT_TOWER_DOWN 200       /* steps to dig the tower back down at most */
#define NT_TOWER_PITCH 65.0f    /* the look round from the top: this far down */
#define NT_TOWER_TURN 12.0f     /* degrees a step */
#define NT_TOWER_HIT 10         /* steps between strikes from the top */
#define NT_TOWER_END_D 12.0     /* none this near for NT_TOWER_CLEAR steps: down */
#define NT_TOWER_CLEAR 60
#define NT_TOWER_MAX 1200       /* steps on one tower at most */
#define NT_TOWER_OFF 200        /* steps before another after one given up */
#define NT_HOOD_HP_HI 14.0f     /* lane/rod (nether_hood_hp 14): the hood at or under this (a blaze's melee is 6) */
#define NT_TOWER_NEAR 4.5      /* lane/rod (nether_skel_tower far): no tower begun with one nearer than this */
static int nt_tower(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!NT_SKEL_TOWER) return 0;
    int64_t t = e->steps;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z), wk = -1, wy = -1000;
    double wd = 1e9;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        if (s->vis[i].kind == HK_SKELETON && s->vis[i].h > NT_WITHER_H)
        {
            double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
            if (d < wd) wd = d, wk = i;
            /* the highest one near: the tower's top NT_TOWER_H over its feet (gold-g7's walked the corridor's wall
             * tops, a block over its floor: a tower of 2 from the floor left the player in reach, struck off it) */
            /* the block it stands on, not its jump (NT_TOWER_H covers that): a jump's ceil put the top a block over
             * its head's reach, the box's top 3.3 under the eye, never struck */
            int sy = (int)floor(s->vis[i].y + 0.01);
            if (d < NT_TOWER_END_D && sy > wy) wy = sy;
        }
    if (n->tower == 2)
    {
        /* down: the pillar's own block under the feet dug (straight down, the best tool), a block at a time, to the
         * floor it stood on; one near again: up again */
        int ub = nt_block(v, e, fx, fy - 1, fz);
        if (wd <= NT_TOWER_D && fy > n->tower_y0) n->tower = 1, n->tower_clear = 0;
        else if (fy <= n->tower_y0 || ++n->tower_age > NT_TOWER_MAX + NT_TOWER_DOWN || ub == 112 || ub == 113 ||
                 ub == 114 || ub < 0 || nt_open(ub) || nt_lava(ub) ||
                 (NT_FLOOR_KEEP && nt_deep(v, e, fx, fy - 1, fz)))
        {
            n->tower = 0, n->pil_base = 0, n->pil_age = 0;
            n->wtried = 0, n->wk = -1;
            return 0;
        }
        else
        {
            still(a);
            a->look_mode = 1;
            a->look[0] = r->yaw, a->look[1] = 90.0f;
            a->hold = 1u << PK_SNEAK;
            if (r->on_ground && s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz)
            {
                a->hold |= 1u << PK_ATTACK;
                int best = nt_tool(v, s, ub);
                if (best >= 0 && best != s->current) a->hotbar = best;
            }
            return 1;
        }
    }
    if (n->tower == 1)
    {
        n->tower_clear = wd > NT_TOWER_END_D ? n->tower_clear + 1 : 0;
        /* (the age limit only with none within NT_TOWER_D: dug down among them at the limit, gold-g7's env 0 died) */
        if (n->tower_clear > NT_TOWER_CLEAR || (++n->tower_age > NT_TOWER_MAX && wd > NT_TOWER_D))
        {
            if (n->tower_age > NT_TOWER_MAX) n->tower_off = t + NT_TOWER_OFF;
            n->tower = 2, n->pil_base = 0, n->pil_age = 0;
            e->fid = 0;
            still(a);
            return 1;
        }
    }
    else
    {
        if (wd > NT_TOWER_D || t < n->tower_off || !r->on_ground) return 0;
        if (NT_SKEL_TOWER == 2 && wd < NT_TOWER_NEAR) return 0;   /* lane/rod (far): too near to build: the fight */
        /* never a tower on a tower (gold-g7: a descent cut short, then the next from its top, to 82) */
        if (n->towers && fy > n->tower_y0 && fy <= n->tower_y0 + NT_TOWER_MAXH && fx == n->tower_x && fz == n->tower_z)
        {
            n->tower = 1, n->tower_age = 0, n->tower_clear = 0;     /* back on the same one */
            if (n->tower_top < fy) n->tower_top = fy;
        }
        else
        {
        int top = wy + NT_TOWER_H > fy + NT_TOWER_H ? wy + NT_TOWER_H : fy + NT_TOWER_H;
        if (top > fy + NT_TOWER_MAXH) top = fy + NT_TOWER_MAXH;
        int ub = nt_block(v, e, fx, fy - 1, fz);
        if (ub < 0 || nt_open(ub) || nt_lava(ub) || !nt_col_open(v, e, fx, fy + 2, top + 1, fz)) return 0;
        int hb = nt_fill_slot(v, s);
        if (hb < 0 || s->inv[hb].count < top - fy) return 0;
        n->tower = 1, n->tower_y0 = fy, n->tower_top = top, n->tower_age = 0, n->tower_clear = 0, n->towers += 1;
        n->tower_hp = r->health;            /* lane/rod (far) */
        n->tower_x = fx, n->tower_z = fz;
        n->pil_base = 0, n->pil_age = 0;
        e->fid = 0;
        }
    }
    if (wy + NT_TOWER_H > n->tower_top && wy + NT_TOWER_H <= n->tower_y0 + NT_TOWER_MAXH &&
        nt_col_open(v, e, fx, fy + 2, wy + NT_TOWER_H + 1, fz))
        n->tower_top = wy + NT_TOWER_H;     /* one came higher: the tower too */
    if (n->pil_base != 0 || (fy < n->tower_top && r->on_ground))
    {
        /* lane/rod (far): hit while building, the pillar given up (the knockback moves the box off its column) */
        int hit = NT_SKEL_TOWER == 2 && n->tower_hp > 0 && r->health < n->tower_hp;
        n->tower_hp = r->health;
        int p = hit ? -1 : nt_pillar(v, e, r, s, a);
        if (hit) n->pil_base = 0, n->pil_age = 0, n->tower_quits += 1;
        if (p < 0)
        {
            n->tower = 0, n->tower_off = t + NT_TOWER_OFF;
            n->wtried = 0, n->wk = -1;
            return 0;
        }
        if (p > 0) still(a), a->look_mode = 1, a->look[0] = r->yaw, a->look[1] = 90.0f, a->hold = 1u << PK_SNEAK;
        return 1;
    }
    if (!r->on_ground)
    {
        still(a), a->look_mode = 1, a->look[0] = r->yaw, a->look[1] = r->pitch, a->hold = 1u << PK_SNEAK;
        return 1;
    }
    /* on the top: no lock (fight()'s, lost 10 steps out of view, struck twice in 160 steps): each step the look at the
     * nearest wither skeleton in view (else the nearest foe the fight would take), a strike whenever the crosshair
     * is on a skeleton or a blaze, NT_TOWER_HIT steps apart (the hurt time: a strike sooner does nothing) */
    e->fid = 0;
    still(a);
    a->look_mode = 1;
    int f = wk >= 0 ? wk : nt_foe(r, s, NT_FIGHT_D, 1);
    if (f >= 0)
    {
        double yaw, pitch;
        look_at(r->x, ey, r->z, s->vis[f].x, s->vis[f].y + 0.5 * s->vis[f].h, s->vis[f].z, &yaw, &pitch);
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        int wb = best_weapon(s);
        if (wb >= 0 && wb != s->current) a->hotbar = wb;
    }
    else
        a->look[0] = r->yaw + NT_TOWER_TURN, a->look[1] = NT_TOWER_PITCH;   /* a look round steeply down */
    if ((s->mo_ekind == HK_SKELETON || s->mo_ekind == HK_BLAZE) && t - n->tower_hit >= NT_TOWER_HIT)
    {
        a->press[PK_ATTACK] = 1;
        n->tower_hit = t, n->tower_hits += 1;
    }
    a->hold &= ~((1u << PK_FORWARD) | (1u << PK_JUMP));
    a->hold |= 1u << PK_SNEAK;
    return 1;
}

/* lane/nether3: up to the fortress seen over the player (its highest block near, ty): a pillar where the column over
 * the head is open to ty + 2, else first to a cell beside whose column is (bridged to over lava: the walker's), then
 * the pillar there. 0 the step driven, -1 no way up from here */
static int nt_climb(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    n->climbed = 1;
    if (!r->on_ground && n->pil_base == 0)
    {
        /* a block up, still coming down onto it: still, crouched */
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = 90.0f;
        a->hold = 1u << PK_SNEAK;
        return 0;
    }
    if (n->pil_base != 0 || (r->on_ground && nt_col_open(v, e, fx, fy + 2, n->ty + 2, fz) &&
                             (!n->has_cc || (n->ccx == fx && n->ccz == fz))))
    {
        if (n->pil_base == 0)
        {
            /* a pillar only from the middle of a solid floor (from a bridge's brink the jump fell off it) */
            int ub = nt_block(v, e, fx, fy - 1, fz);
            if (ub < 0 || nt_open(ub) || nt_lava(ub)) return -1;
            double cx = fx + 0.5 - r->x, cz = fz + 0.5 - r->z, yaw, pitch;
            if (py_hypot2(cx, cz) > 0.2)
            {
                look_at(r->x, ey, r->z, fx + 0.5, ey - 1.0, fz + 0.5, &yaw, &pitch);
                a->look_mode = 1;
                a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
                return 0;
            }
        }
        int p = nt_pillar(v, e, r, s, a);
        if (p < 0) return -1;
        n->has_cc = 1, n->ccx = fx, n->ccz = fz;
        if (p > 0)
        {
            still(a), a->look_mode = 1, a->look[0] = r->yaw, a->look[1] = 90.0f, a->hold = 1u << PK_SNEAK;
            if (fy + 1 >= n->ty - 1 || (fy + 1) % NT_CLIMB_LOOK == 0) n->clook = NT_CLIMB_LOOKS;
        }
        return 0;
    }
    if (!n->has_cc || (n->ccx == fx && n->ccz == fz))
    {
        /* the column over the head covered (the deck): a cell beside, away from the highest block first */
        int best = -1;
        double bd = -1.0;
        for (int d = 0; d < 4; ++d)
        {
            int cx = fx + NDX[d], cz = fz + NDZ[d], fb = nt_block(v, e, cx, fy - 1, cz);
            /* its floor solid, or air or lava bridged by the walker (from a pillar's top every side is air) */
            if (fb < 0 || fb == 51 || fb == 90 || (nt_open(fb) && nt_fill_slot(v, s) < 0) ||
                !nt_col_open(v, e, cx, fy, n->ty + 2, cz))
                continue;
            double dd = py_hypot2(cx + 0.5 - (n->tx + 0.5), cz + 0.5 - (n->tz + 0.5));
            if (dd > bd) bd = dd, best = d;
        }
        if (best < 0) return -1;
        n->has_cc = 1, n->ccx = fx + NDX[best], n->ccz = fz + NDZ[best];
        n->wtried = 0, n->wk = -1;
    }
    int w = nt_walk(v, e, r, s, a, ey, n->ccx, fy, n->ccz, 0.25, -1);
    if (w < 0) return -1;
    if (w > 0) still(a), a->look_mode = 1, a->look[0] = r->yaw, a->look[1] = r->pitch, a->hold = 1u << PK_SNEAK;
    return 0;
}

/* lane/nether2: home along the way walked: the crumbs in reverse (the earliest one beside the player first: a short
 * cut), a crumb two or more over the feet within a block climbed by a pillar, then the remembered portal cell. 1 in
 * the portal's cell, 0 going on, -1 no way on */
/* lane/rod (nether_oracle tunnel, PRIVILEGED): the way home as a fixed-rule 1x2 tunnel, no per-cell path (the
 * planner's follower stalled on every dug step: 27 blocks in 8,000 steps, 169 paths). The heading along the axis with
 * the farther way to the portal; the cell ahead one down while the feet are over the portal's height (one up while
 * under), level else; its cells dug top first, the right tool held, then the step into it. Lava, fire, water, bedrock
 * or obsidian in the cells, an open floor under them, or lava beside them: NT_DIG_SIDE cells along the other axis
 * first (the side away from the last one tried). 1 beside the portal (the step after goes in), 0 acting */
/* the digger's visits to column (x, z) (add: one more); its slot's count */
static int nt_dg_visits(struct nt_env *n, int x, int z, int add)
{
    int m = n->dg_nvis < 256 ? n->dg_nvis : 256;
    for (int i = 0; i < m; ++i)
        if (n->dg_vis[i][0] == x && n->dg_vis[i][1] == z) return n->dg_vis[i][2] += add;
    if (!add) return 0;
    int k = n->dg_nvis++ % 256;
    n->dg_vis[k][0] = (int16_t)x, n->dg_vis[k][1] = (int16_t)z, n->dg_vis[k][2] = 1;
    return 1;
}

/* the tunnel planner's next cell for the digger (netherview_oracle_path, NV_PATH_TUNNEL over the server's blocks): the
 * path advanced past the feet's cell, made again when it ends, ages NT_DIG_PLAN_AGE steps or a step of it was refused
 * (at most every NT_DIG_PLAN_GAP steps); 1 and the cell when it is one step from the feet (a cell beside, up or down
 * at most one), else 0 (the digger's own rules) */
#define NT_DIG_PLAN_AGE 1500
#define NT_DIG_PLAN_GAP 40
static int nt_dg_next(const struct neth_vec *v, struct vec_env *e, int fx, int fy, int fz, int *ax, int *ty, int *az)
{
    struct nt_env *n = &e->nt;
    for (int j = n->opath_k - 1 < 0 ? 0 : n->opath_k - 1; j < n->nopath && j <= n->opath_k + 3; ++j)
        if (n->opath[j][0] == fx && n->opath[j][1] == fy && n->opath[j][2] == fz) n->opath_k = j + 1;
    int stale = n->nopath == 0 || n->opath_k >= n->nopath || e->steps - n->opath_t > NT_DIG_PLAN_AGE || n->dg_replan;
    if (stale && e->steps - n->opath_t >= NT_DIG_PLAN_GAP)
    {
        n->opaths += 1, n->opath_t = e->steps, n->opath_k = 0, n->dg_replan = 0;
        n->nopath = netherview_oracle_path(v->p, nt_id(v, e), fx, fy, fz, n->px, n->py, n->pz, 1.0,
                                           NV_PATH_BUILD | NV_PATH_EXACT | NV_PATH_TUNNEL, n->opath, NT_OPATH);
        if (n->nopath < 0) n->nopath = 0;
        n->opaths_ok += n->nopath > 0;
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
            fprintf(stderr, "dgp %lld from %d %d %d len %d to %d %d %d\n", (long long)e->steps, fx, fy, fz, n->nopath,
                    n->nopath ? n->opath[n->nopath - 1][0] : 0, n->nopath ? n->opath[n->nopath - 1][1] : 0,
                    n->nopath ? n->opath[n->nopath - 1][2] : 0);
#endif
    }
    if (n->opath_k >= n->nopath) return 0;
    const int16_t *c = n->opath[n->opath_k];
    if (abs(c[0] - fx) + abs(c[2] - fz) != 1 || abs(c[1] - fy) > 1)
    {
        n->dg_replan = 1;
        return 0;
    }
    *ax = c[0], *ty = c[1], *az = c[2];
    return 1;
}

static int nt_digger(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey, int sealed)
{
    struct nt_env *n = &e->nt;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (sealed && r->fire)
    {
        for (int dy = 0; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (netherview_oracle_block(v->p, nt_id(v, e), fx + dx, fy + dy, fz + dz) == 51 &&
                        nt_cover_place(v, e, r, s, a, ey, fx + dx, fy + dy, fz + dz, 0, COBBLE)) return 0;
        if (nt_refill(v, e, r, s, a)) return 0;
        still(a), a->hold = 1u << PK_SNEAK;
        return 0;
    }
    int dx = n->px - fx, dz = n->pz - fz;
    if (abs(dx) + abs(dz) <= 1 && abs(fy - n->py) <= 1) return 1;
    /* The exit retreat follows remembered turns. Enclosing the current cell
     * before selecting its next step can fill that next step's head space. */
    if (sealed == 2 && r->on_ground && !MOPT(MO_NT_EXIT_BACK))
    {
        int hx = abs(dx) >= abs(dz) ? (dx > 0 ? 1 : -1) : 0;
        int hz = hx == 0 ? (dz > 0 ? 1 : -1) : 0;
        if (nt_far_enclose(v, e, r, s, a, ey, fx, fy, fz, hx, hz)) return 0;
    }
    if (!r->on_ground)
    {
        if (sealed == 2) { still(a); a->hold = 1u << PK_SNEAK; return 0; }
        /* mid-jump or mid-drop: on toward the cell the step aimed at (dig-9dt: no key held in the air, every jump
         * straight up, the player at x -104.30 from step 2,750 to the end) */
        if (n->dg_have)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, n->dg_ax + 0.5, ey, n->dg_az + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
        }
        return 0;
    }
    if (n->dg_walk > 0)
    {
    if (sealed == 2 && !MOPT(MO_NT_EXIT_BACK) && abs(n->dg_ax - fx) + abs(n->dg_az - fz) == 1 &&
            nt_far_enclose(v, e, r, s, a, ey, n->dg_ax, n->dg_ty, n->dg_az,
                           n->dg_ax - fx, n->dg_az - fz)) return 0;
        /* a step begun is walked to the cell's middle (dig-9lt: the next move chosen as the feet crossed into the cell
         * below a step down, the player still on the old floor's edge: four cells round for 17,000 steps) */
        double cx = n->dg_ax + 0.5, cz = n->dg_az + 0.5;
        if (fabs(r->x - cx) > 0.3 || fabs(r->z - cz) > 0.3)
        {
            n->dg_walk -= 1;
            double yaw, pitch;
            look_at(r->x, ey, r->z, cx, ey, cz, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
            if (n->dg_ty > fy) a->hold |= 1u << PK_JUMP;
            else if (n->dg_ty < fy) n->or_drop_at = e->steps;
            else if (n->dg_walk > NT_DIG_STILL) a->hold |= 1u << PK_SNEAK;
            return 0;
        }
        n->dg_walk = 0;
    }
    /* the moves in order (dig-9at: two sidesteps both refused held the player still from step 1,400): the farther
     * axis toward the portal, the nearer, the two sides of the farther, back; each at the height wanted (down over
     * the portal's, up under it), level, then the other; a move off the first axis kept NT_DIG_SIDE blocks */
    int dirs[5], nd = 0;
    int pa = abs(dx) >= abs(dz) && dx != 0 ? (dx > 0 ? 3 : 1) : (dz > 0 ? 0 : 2);
    int pb = pa == 3 || pa == 1 ? (dz > 0 ? 0 : dz < 0 ? 2 : -1) : (dx > 0 ? 3 : dx < 0 ? 1 : -1);
    if (n->dg_side > 0) dirs[nd++] = n->dg_sdir;
    dirs[nd++] = pa;
    if (pb >= 0) dirs[nd++] = pb;
    dirs[nd++] = (pa + 1) & 3, dirs[nd++] = (pa + 3) & 3;
    if (nd < 5) dirs[nd++] = (pa + 2) & 3;
    /* no gain toward the portal in NT_DIG_GAIN steps (dig-9gt: two cells across x -103 traded for 11,000 steps, each
     * the other's best move): the first move dropped too */
    int dist = abs(dx) + abs(dz);
    if (n->dg_best == 0 || dist < n->dg_best) n->dg_best = dist, n->dg_best_t = e->steps;
    else if (e->steps - n->dg_best_t > NT_DIG_GAIN) n->dg_age = NT_DIG_STUCK + 1, n->dg_best_t = e->steps;
    if (n->dg_age > NT_DIG_STUCK)
    {
        /* stuck at one cell: the first move dropped for a while */
        n->dg_age = 0, n->dg_skip = (n->dg_skip + 1) % nd, n->dg_sides += 1;
    }
    /* the height: NT_DIG_Y or over (away from the lava sea, y 31) until NT_DIG_NEAR of the portal, then the portal's
     * (dig-9ft: going down to the portal's y 32 from the start, every move refused at y 41 by the lava's level) */
    int gy = abs(dx) + abs(dz) > NT_DIG_NEAR ? (n->py > NT_DIG_Y ? n->py : NT_DIG_Y) : n->py;
    int want = fy > gy ? -1 : fy < gy ? 1 : 0;
    /* at the height wanted, a step up or down too (dig-9it: level only at y 45, every move toward the portal refused at
     * z -164, 15,000 steps penned in its own tunnels) */
    int dys[3] = {want, 0, -want}, ndy = 3;
    if (!want) dys[1] = 1, dys[2] = -1;
    int cells[4][3], nc = 0, hx = 0, hz = 0, ty = fy, ax = fx, az = fz, found = 0, pick = -1;
    int half = r->y - floor(r->y) > 0.2;     /* on a slab or a stair: the head reaches the cell 2 over the feet's */
    /* two passes: the moves on a floor there, then (dig-9ht: 11,000 steps penned in a 20 x 20 patch at y 45 by an
     * open cavern) a level move over an open floor, bridged: a block placed there first (no liquid under it) */
    int bridge = 0;
    /* pass 0 only a column entered under twice (dig-9mt: 20,000 steps round x -84..-81 z -131..-120 on the soul sand
     * at y 65), pass 1 any, pass 2 a bridge too */
    /* pass -1: the tunnel planner's next cell (dig-9nt: the rules alone wandered into the fortress, 7,491 steps on its
     * brick, and ended 190 blocks from the portal), the lava box 1 round it (the planner's own rule) */
    int pdi = -1, pdy = 0, found_pass = 9;
    {
        int px_, py_, pz_;
        if (nt_dg_next(v, e, fx, fy, fz, &px_, &py_, &pz_))
        {
            for (int k = 0; k < 4; ++k)
                if (fx + NDX[k] == px_ && fz + NDZ[k] == pz_) pdi = k;
            pdy = py_ - fy;
        }
    }
    for (int shelter = sealed == 2 ? 0 : 1; shelter <= 1 && !found; ++shelter)
    for (int pass = pdi >= 0 ? -1 : 0; pass < (sealed ? 2 : 3) && !found; ++pass)
    for (int o = 0; o < (pass < 0 ? 1 : nd) && !found; ++o)
    {
        int di = pass < 0 ? pdi : dirs[(o + n->dg_skip) % nd];
        for (int q = 0; q < (pass < 0 ? 1 : ndy) && !found; ++q)
        {
            hx = NDX[di], hz = NDZ[di], ty = fy + (pass < 0 ? pdy : dys[q]), ax = fx + hx, az = fz + hz;
            if (sealed == 2 && MOPT(MO_NT_EXIT_BACK) && n->t_rods7 >= 0 && ty != n->py) continue;
            if (sealed == 2 && ty < 40) continue;
            /* the cells to open: the feet's and head's at the cell ahead, and the third a stair needs (over the
             * landing going down, over the head going up) */
            cells[0][0] = ax, cells[0][1] = ty + 1, cells[0][2] = az, cells[1][0] = ax, cells[1][1] = ty, cells[1][2] = az;
            nc = 2;
            if (ty < fy) cells[0][1] = ty + 2, cells[1][1] = ty + 1, cells[2][0] = ax, cells[2][1] = ty, cells[2][2] = az,
                         nc = 3;
            else if (ty > fy) cells[0][0] = fx, cells[0][1] = fy + 2, cells[0][2] = fz, cells[1][0] = ax,
                              cells[1][1] = ty + 1, cells[1][2] = az, cells[2][0] = ax, cells[2][1] = ty, cells[2][2] = az,
                              nc = 3;
            if (half && ty <= fy)
            {
                /* dig-9et: on a stair at y 69.5 the head (to 71.3) met the cell 2 over the feet ahead, never dug:
                 * that cell first (top first) */
                for (int i = nc; i > 0; --i) cells[i][0] = cells[i - 1][0], cells[i][1] = cells[i - 1][1],
                                             cells[i][2] = cells[i - 1][2];
                cells[0][0] = ax, cells[0][1] = fy + 2, cells[0][2] = az, nc += 1;
            }
            int bad = 0;
            for (int i = 0; i < nc && !bad; ++i)
            {
                int b = nt_block(v, e, cells[i][0], cells[i][1], cells[i][2]);
                if (b < 0 || b == 7 || b == 49 || b == 10 || b == 11 || b == 8 || b == 9 || b == 51 || b == 90) bad = 2;
                if (!bad && sealed == 2 && ty < fy && !nt_far_solid(netherview_oracle_block(v->p, nt_id(v, e),
                                                                                             cells[i][0], cells[i][1] - 1,
                                                                                             cells[i][2]))) bad = 11;
                if (!bad && sealed && !nt_open(b) && nt_dig_face_unsafe(v, e, cells[i][0], cells[i][1], cells[i][2]))
                {
#ifdef NT_TRACE_FACE
                    if (nt_id(v, e) == NT_TRACE_FACE)
                        fprintf(stderr, "face-veto step=%lld player=%d,%d,%d target=%d,%d,%d\n",
                                (long long)e->steps, fx, fy, fz, cells[i][0], cells[i][1], cells[i][2]);
#endif
                    bad = 8;
                }
                for (int k = 0; k < 4 && !bad; ++k)
                {
                    int w = netherview_block(v->p, nt_id(v, e), cells[i][0] + NDX[k], cells[i][1], cells[i][2] + NDZ[k],
                                             NULL);
                    if (w == 10 || w == 11) bad = 3;
                }
                int u = netherview_block(v->p, nt_id(v, e), cells[i][0], cells[i][1] + 1, cells[i][2], NULL);
                /* sand or gravel over: it falls into the cell and is dug again; on the planner's step refused only over
                 * the player's own column (dig-9pt: gravel over a step down, 17,000 steps held at -38 51 -50) */
                if (!bad && (u == 10 || u == 11 ||
                             ((u == 12 || u == 13) && (pass >= 0 || (cells[i][0] == fx && cells[i][2] == fz)))))
                    bad = 4;
            }
            /* no lava within 2 of the cell ahead, from under its floor to over a step's head (dig-9jt: lava ran in
             * from 2 off as a step up's cell over the head was dug, the player dead in it at step 9,347) */
            int lr = pass < 0 ? -1 : 2;     /* the planner's step: its own rule (lava beside or over the cells) */
            for (int lx = ax - lr; lx <= ax + lr && !bad; ++lx)
                for (int lz = az - lr; lz <= az + lr && !bad; ++lz)
                    for (int ly = (ty < fy ? ty : fy) - 1; ly <= (ty > fy ? ty : fy) + 3 && !bad; ++ly)
                    {
                        /* the raw block, past lava_sense (dig-9kt: the lava 2 off not yet seen, dug into at 9,250);
                         * the tunnel is privileged already */
                        int w = netherview_block(v->p, nt_id(v, e), lx, ly, lz, NULL);
                        if (w == 10 || w == 11) bad = 5;
                    }
            int fl = nt_block(v, e, ax, ty - 1, az), fb = 0;
            if (!bad && sealed && nt_far_hot_by(v, e, ax, ty, az)) bad = 9;
            if (bad) {}
            else if (fl < 0 || fl == 10 || fl == 11) bad = 6;
            else if (nt_open(fl))
            {
                int under = nt_block(v, e, ax, ty - 2, az);
                if (sealed || (pass >= 0 && pass < 2) || ty != fy || under == 10 || under == 11 || under == 8 || under == 9)
                    bad = 7;
                else fb = 1;
            }
#ifdef NT_TRACE
            if (pass < 0 && nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
                fprintf(stderr, "dgx %lld at %d %d %d path %d %d %d refused %d floor %d\n", (long long)e->steps, fx, fy,
                        fz, ax, ty, az, bad, fl);
#endif
            if (!bad && pass == 0 && nt_dg_visits(n, ax, az, 0) >= 2) bad = 1;
            /* never straight back into the cell just left while any other move is open */
            if (!bad && pass >= 0 && ax == n->dg_lx && az == n->dg_lz && o < nd - 1) bad = 1;
            if (!bad && sealed == 2 && shelter == 0 && nt_far_open_faces(v, e, ax, ty, az, hx, hz)) bad = 12;
            if (!bad) found = 1, pick = di, bridge = fb, found_pass = pass;
        }
    }
    if (pdi >= 0 && found_pass >= 0) n->dg_replan = 1;     /* the path's step refused: a new path */
    n->dg_planned += found_pass < 0;
    if (!found)
    {
#ifdef NT_FAR_DEBUG
        if (sealed == 2 && e->steps % 500 == 0)
            fprintf(stderr, "far-no-candidate step=%lld player=%.2f,%.2f,%.2f target=%d,%d,%d age=%d\n",
                    (long long)e->steps, r->x, r->y, r->z, n->px, n->py, n->pz, n->dg_age);
#endif
        still(a);
        n->dg_age += 1;
        return 0;
    }
    (void)pick;     /* no sidestep kept (dig-9ft: a kept sidestep back and the way on alternated, 9,000 steps) */
    n->dg_age += 1;
    if (bridge && nt_cover_place(v, e, r, s, a, ey, ax, ty - 1, az, 0, 0))
    {
        n->dg_bridges += 1;
        return 0;
    }
    /* dig the first cell not open, top first */
    for (int i = 0; i < nc; ++i)
    {
        int b = nt_block(v, e, cells[i][0], cells[i][1], cells[i][2]);
        if (nt_open(b)) continue;
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
            fprintf(stderr, "dgd %lld at %d %d %d dig %d %d %d (block %d) move to %d %d %d bridge %d mo %d %d %d %d\n",
                    (long long)e->steps, fx, fy, fz, cells[i][0], cells[i][1], cells[i][2], b, ax, ty, az, bridge,
                    s->mo_hit, s->mo_x, s->mo_y, s->mo_z);
#endif
        double yaw, pitch;
        look_at(r->x, ey, r->z, cells[i][0] + 0.5, cells[i][1] + 0.5, cells[i][2] + 0.5, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        int tb = nt_tool(v, s, b);
        if (tb >= 0 && tb != s->current) a->hotbar = tb;
        a->hold = (1u << PK_ATTACK) | (1u << PK_SNEAK);
        return 0;
    }
    if (sealed == 2 && !MOPT(MO_NT_EXIT_BACK) && nt_far_enclose(v, e, r, s, a, ey, ax, ty, az, hx, hz)) return 0;
    /* open: the step in (to the cell's middle), the floor's brink not crouched over for a stair down */
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
        fprintf(stderr, "dg %lld at %d %d %d -> %d %d %d: cells %d %d %d floor %d head-over %d server %d %d age %d\n",
                (long long)e->steps, fx, fy, fz, ax, ty, az, nt_block(v, e, ax, ty, az), nt_block(v, e, ax, ty + 1, az),
                nt_block(v, e, ax, ty + 2, az), nt_block(v, e, ax, ty - 1, az), nt_block(v, e, fx, fy + 2, fz),
                netherview_oracle_block(v->p, nt_id(v, e), ax, ty, az),
                netherview_oracle_block(v->p, nt_id(v, e), ax, ty + 1, az), n->dg_age);
#endif
    double yaw, pitch;
    n->dg_have = 1, n->dg_ax = ax, n->dg_az = az, n->dg_ty = ty, n->dg_walk = 40;
    look_at(r->x, ey, r->z, ax + 0.5, ey, az + 0.5, &yaw, &pitch);
    still(a);
    a->look_mode = 1;
    a->look[0] = (float)yaw, a->look[1] = 10.0f;
    a->hold = 1u << PK_FORWARD;
    if (ty > fy) a->hold |= 1u << PK_JUMP;
    else if (ty == fy) a->hold |= 1u << PK_SNEAK;
    else n->or_drop_at = e->steps;
    /* dig-9ct: the walk into an open cell held by sneak's brink rule (the feet's own floor dug by a stair, the
     * player on the next block's edge): still NT_DIG_STILL steps, no crouch and a jump */
    if (fabs(r->x - n->dg_wx) < 0.01 && fabs(r->z - n->dg_wz) < 0.01) n->dg_still += 1;
    else n->dg_still = 0, n->dg_wx = r->x, n->dg_wz = r->z;
    if (n->dg_still > NT_DIG_STILL)
    {
        a->hold &= ~(1u << PK_SNEAK);
        a->hold |= 1u << PK_JUMP;
        n->or_drop_at = e->steps;
    }
    if (fx != n->dg_x || fz != n->dg_z)
    {
        /* a block on: the sidestep counted down, the stuck count begun again */
        n->dg_lx = n->dg_x, n->dg_lz = n->dg_z;     /* the cell left */
        n->dg_x = fx, n->dg_z = fz, n->dg_age = 0, n->dg_blocks += 1;
        nt_dg_visits(n, fx, fz, 1);
        if (n->dg_side > 0) n->dg_side -= 1;
    }
    return 0;
}

#define NT_EXIT_SP_CLEAR 8.0    /* horizontal blocks: dropping below the spawner does not break its sight line */
#define NT_EXIT_CRUMB_LEG 8.0
#define NT_EXIT_COVER_DROP 5          /* the ordinary cover's feet below the spawner */
#define NT_STRONGHOLD_COVER_DROP 12  /* the long chamber's cover below the spawner */
static int nt_home(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (MOPT(MO_NT_FAR_DIG) && n->t_rods7 >= 0 && n->dg_best_t > 0 && n->dg_best_t < n->t_rods7)
    {
        /* The departure's digger was aimed at the fortress; the return starts a new path to the real portal. */
        n->dg_walk = n->dg_have = n->dg_best = n->dg_age = n->dg_side = n->dg_skip = n->dg_still = n->dg_nvis = 0;
        n->dg_replan = 1, n->nopath = n->opath_k = 0;
        n->dg_best_t = e->steps;
    }
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (NT_ORACLE >= 3)
    {
        /* lane/rod (nether_oracle home): vanilla's portal cooldown (Entity.setInPortal: timeUntilPortal set again each
         * tick in a portal while it is above 0) holds a player who never left the portal: NT_HOME_PIN steps in its cell
         * with no travel, then NT_HOME_POUT steps out beside it, across the portal's plane, and in again */
        if (n->pout > 0)
        {
            n->pout -= 1;
            int w = nt_opath_walk(v, e, r, s, a, ey, n->pox, n->poy, n->poz, 0.15, 1);
            if (w != 0) still(a);
            return 0;
        }
        int ib = nt_block(v, e, fx, fy, fz), ih = nt_block(v, e, fx, fy + 1, fz);
        if ((ib == 90 || ih == 90) && ++n->pin > NT_HOME_PIN)
        {
            /* the nearest cell to stand in that is no portal's (feet and head open, a floor), within NT_HOME_OUT_R */
            double bd = 1e9;
            int found = 0;
            for (int dx = -NT_HOME_OUT_R; dx <= NT_HOME_OUT_R; ++dx)
                for (int dz = -NT_HOME_OUT_R; dz <= NT_HOME_OUT_R; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        int cx = fx + dx, cy = fy + dy, cz = fz + dz;
                        int fb = nt_block(v, e, cx, cy, cz), hb = nt_block(v, e, cx, cy + 1, cz);
                        if (fb < 0 || hb < 0 || fb == 90 || hb == 90 || !nt_open(fb) || !nt_open(hb) ||
                            !nt_floor_ok(nt_block(v, e, cx, cy - 1, cz)))
                            continue;
                        /* home-r7c: a neighbour's cell stopped 0.37 off its middle left the box (0.6 wide) over
                         * the portal's cell, the cooldown set again: 2 cells off at least */
                        if (abs(dx) + abs(dz) < 2) continue;
                        double d = py_hypot2(cx + 0.5 - r->x, cz + 0.5 - r->z) + abs(dy);
                        if (d < bd) bd = d, n->pox = cx, n->poy = cy, n->poz = cz, found = 1;
                    }
            n->pin = 0;
            if (found) n->pout = NT_HOME_POUT, n->pouts += 1;
            return 0;
        }
    }
    if (fx == n->px && fz == n->pz && abs(fy - n->py) <= 1)
    {
        /* in the portal's cell: to its middle (the portal's box is a slab through the cell's middle), crouched */
        double cx = n->px + 0.5 - r->x, cz = n->pz + 0.5 - r->z, yaw, pitch;
        if (py_hypot2(cx, cz) < NT_PORTAL_MID) return 1;
        look_at(r->x, ey, r->z, n->px + 0.5, ey - 1.0, n->pz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
        return 0;
    }
    if (abs(fx - n->px) + abs(fz - n->pz) == 1 && abs(fy - n->py) <= 1 &&
        nt_open(nt_block(v, e, n->px, fy + 1, n->pz)) + (nt_block(v, e, n->px, fy + 1, n->pz) == 90) > 0)
    {
        /* beside the portal's cell (the walker never steps into a portal block): straight in */
        double yaw, pitch;
        look_at(r->x, ey, r->z, n->px + 0.5, ey - 1.0, n->pz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = 1u << PK_FORWARD;
        return 0;
    }
    if (NT_ORACLE >= 4)
    {
        /* lane/rod (nether_oracle tunnel): the fixed-rule digger (nt_digger) to beside the portal */
        if (nt_digger(v, e, r, s, a, ey, 0) == 0) return 0;
    }
    if (NT_ORACLE >= 3)
    {
        /* lane/rod (nether_oracle home, PRIVILEGED): the planner to the portal's cell (dig and bridge, the server's
         * floor); beside it the step above goes in; the crumbs where no path is found */
        int w = nt_opath_walk(v, e, r, s, a, ey, n->px, n->py, n->pz, NT_OR_HOME_NEAR, 0);
        if (w == 0) return 0;
        if (w > 0)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, n->px + 0.5, ey - 1.0, n->pz + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
            return 0;
        }
    }
    if (n->home_k < 0 && n->ncrumb > 0 && n->home_k != -2) n->home_k = n->ncrumb - 1;
    for (int j = 0; j < n->home_k; ++j)
        if (abs(n->crumb[j][0] - fx) + abs(n->crumb[j][2] - fz) <= 1 && abs(n->crumb[j][1] - fy) <= 1)
        {
            n->home_k = j;
            break;
        }
    while (n->home_k >= 0)
    {
        int16_t *c = n->crumb[n->home_k];
        double hx = c[0] + 0.5 - r->x, hz = c[2] + 0.5 - r->z;
        if (py_hypot2(hx, hz) < 0.45 && abs(c[1] - fy) <= 1)
        {
            n->home_k -= 1;
            continue;
        }
        if (NT_FAN && c[0] == fx && c[2] == fz && c[1] <= fy - 2)
        {
            /* lane/nether3: down a pillar the way went up: the block under the feet (the player's own fill) dug,
             * looking straight down, crouched */
            int ub = nt_block(v, e, fx, fy - 1, fz);
            if ((ub == COBBLE || ub == 87 || ub == 112) && !(NT_FLOOR_KEEP && nt_deep(v, e, fx, fy - 1, fz)))
            {
                a->look_mode = 1;
                a->look[0] = r->yaw, a->look[1] = 90.0f;
                a->hold = 1u << PK_SNEAK;
                if (r->on_ground) a->hold |= 1u << PK_ATTACK;
                int best = nt_tool(v, s, ub);
                if (best >= 0 && best != s->current) a->hotbar = best;
                return 0;
            }
        }
        if (c[1] >= fy + 2 && py_hypot2(hx, hz) < 1.6 && r->on_ground + n->pil_base > 0)
        {
            int p = nt_pillar(v, e, r, s, a);
            if (p >= 0) return 0;
        }
        int w = nt_walk(v, e, r, s, a, ey, c[0], c[1], c[2], 0.4, -1);
        if (w < 0)
        {
            n->home_k -= 1;     /* no way to it: the one before */
            n->wtried = 0, n->wk = -1;
            return 0;
        }
        return 0;
    }
    if (n->home_k == -1) n->home_k = -2;    /* the crumbs walked: the portal's cell */
    int w = nt_walk(v, e, r, s, a, ey, n->px, n->py, n->pz, 0.0, -1);
    if (w > 0)
    {
        double yaw, pitch;
        look_at(r->x, ey, r->z, n->px + 0.5, ey, n->pz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = 1u << PK_FORWARD;
    }
    return w < 0 ? -1 : 0;
}

/* A distance under the spawner is not safety: the portal room may still be in a blaze's
 * clear ray. The player sees the spawner cell and the client-known blazes through its world. */
static int nt_exit_hidden(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct nt_env *n = &e->nt;
    double horizontal = py_hypot2(r->x - (n->sx + 0.5), r->z - (n->sz + 0.5));
    if (horizontal < NT_EXIT_SP_CLEAR) return 0;
    int id = nt_id(v, e);
    int block = netherview_block(v->p, id, n->sx, n->sy, n->sz, NULL);
    int spawner_seen = block == 52 && netherview_seen(v->p, id, r->x, r->y + 1.62, r->z, n->sx, n->sy, n->sz);
    int blazes_seen = netherview_exit_blaze_los(v->p, id, r->x, r->y + 1.62, r->z);
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO && e->steps % 100 == 0)
        fprintf(stderr, "EXITSIGHT step=%lld pos=%.2f,%.2f,%.2f horizontal=%.1f spawner=%d seen=%d blazes=%d\n",
                (long long)e->steps, r->x, r->y, r->z, horizontal, block, spawner_seen, blazes_seen);
#endif
    return !spawner_seen && blazes_seen == 0;
}

/* The cell behind is the passage toward the spawner; the one ahead is the way the
 * tunnel continues. Both are adjacent to the player's current standing cell. */
static void nt_exit_cells(const struct nt_env *n, const struct vec_last *r,
                          int *bx, int *bz, int *ax, int *az)
{
    int fx = (int)floor(r->x), fz = (int)floor(r->z);
    double dx = n->sx + 0.5 - r->x, dz = n->sz + 0.5 - r->z;
    *bx = *ax = fx, *bz = *az = fz;
    if (fabs(dx) >= fabs(dz))
    {
        int step = dx >= 0 ? 1 : -1;
        *bx += step, *ax -= step;
    }
    else
    {
        int step = dz >= 0 ? 1 : -1;
        *bz += step, *az -= step;
    }
}

static int nt_exit_closed(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    return nt_far_solid(nt_block(v, e, x, y, z));
}

static int nt_exit_plug_ready(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct nt_env *n = &e->nt;
    int bx, bz, ax, az, fy = (int)floor(r->y + 0.13);
    nt_exit_cells(n, r, &bx, &bz, &ax, &az);
    (void)ax, (void)az;
    return nt_exit_closed(v, e, bx, fy, bz) && nt_exit_closed(v, e, bx, fy + 1, bz) &&
           nt_exit_hidden(v, e, r);
}

static int nt_exit_plug_cell(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int x, int y, int z)
{
    if (nt_exit_closed(v, e, x, y, z)) return 0;
    if (nt_far_supply(v, e, r, s, a, COBBLE)) return 1;
    int placed = nt_cover_place(v, e, r, s, a, ey, x, y, z, 0, COBBLE);
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO && e->steps % 50 == 0)
        fprintf(stderr, "EXITPLUG step=%lld target=%d,%d,%d block=%d placed=%d use=%d pos=%.2f,%.2f,%.2f\n",
                (long long)e->steps, x, y, z, nt_block(v, e, x, y, z), placed,
                a->press[PK_USE], r->x, r->y, r->z);
#endif
    if (!placed) still(a), a->hold = 1u << PK_SNEAK;
    return 1;
}

static int nt_exit_plug(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    int bx, bz, ax, az, fy = (int)floor(r->y + 0.13);
    nt_exit_cells(&e->nt, r, &bx, &bz, &ax, &az);
    if (nt_exit_plug_cell(v, e, r, s, a, ey, bx, fy, bz)) return 1;
    if (nt_exit_plug_cell(v, e, r, s, a, ey, bx, fy + 1, bz)) return 1;
    if (nt_exit_hidden(v, e, r)) return 0;
    if (nt_exit_plug_cell(v, e, r, s, a, ey, ax, fy, az)) return 1;
    if (nt_exit_plug_cell(v, e, r, s, a, ey, ax, fy + 1, az)) return 1;
    return 0;
}

/* A bucket-cast arrival has no original portal to return to. The last walked tunnel cells
 * give the retreat's horizontal bearing, while the safe digger cuts at the covered player's
 * current height: exit cover may have sealed the old standing cells above it. */
static int nt_exit_crumb_dig(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!MOPT(MO_NT_EXIT_BACK) || !n->has_sp || n->ncrumb < 1) return 0;
#ifdef NT_TRACE_CRUMBS
    if (nt_id(v, e) == 0 && e->steps == 11246)
        for (int q = n->ncrumb - 1; q >= 0 && q >= n->ncrumb - 18; --q)
        {
            int x = n->crumb[q][0], y = n->crumb[q][1], z = n->crumb[q][2];
            fprintf(stderr, "CRUMB %d pos=%d,%d,%d cell=%d head=%d roof=%d floor=%d sides=%d,%d,%d,%d\n",
                    q, x, y, z, nt_block(v, e, x, y, z), nt_block(v, e, x, y + 1, z),
                    nt_block(v, e, x, y + 2, z), nt_block(v, e, x, y - 1, z),
                    nt_block(v, e, x + 1, y + 1, z), nt_block(v, e, x - 1, y + 1, z),
                    nt_block(v, e, x, y + 1, z + 1), nt_block(v, e, x, y + 1, z - 1));
        }
#endif
    if (py_hypot2(r->x - (n->sx + 0.5), r->z - (n->sz + 0.5)) >= NT_EXIT_SP_CLEAR)
    {
        if (nt_exit_plug(v, e, r, s, a, ey)) return 1;
        if (nt_exit_plug_ready(v, e, r)) return 0;
    }
    if (n->home_k < 0 || n->home_k >= n->ncrumb)
    {
        int fy = (int)floor(r->y + 0.13);
        n->home_k = n->ncrumb - 1;
        /* Exit cover can fill the last few cells above the player. Join the
         * first open, floored crumb on this level, then follow each cell. */
        for (int k = n->ncrumb - 1; k >= 0; --k)
        {
            int x = n->crumb[k][0], y = n->crumb[k][1], z = n->crumb[k][2];
            if (y == fy && nt_open(nt_block(v, e, x, y, z)) &&
                nt_open(nt_block(v, e, x, y + 1, z)) &&
                nt_floor_ok(nt_block(v, e, x, y - 1, z)))
            {
                n->home_k = k;
                break;
            }
        }
        n->wk = -1;
    }
    int k = n->home_k;
    int16_t *c = n->crumb[k];
    n->mode = NM_DIG;
    int rc = nt_walk(v, e, r, s, a, ey, c[0], c[1], c[2], 0.35, -1);
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO && e->steps % 20 == 0)
        fprintf(stderr, "EXITCRUMB step=%lld hk=%d/%d goal=%d,%d,%d rc=%d pos=%.2f,%.2f,%.2f hold=%u\n",
                (long long)e->steps, k, n->ncrumb, c[0], c[1], c[2], rc,
                r->x, r->y, r->z, a->hold);
#endif
    if (rc > 0 && n->home_k > 0) n->home_k -= 1;
    if (rc < 0) still(a), a->hold = 1u << PK_SNEAK;
    return 1;
}

/* lane/lavaesc's escape in the Nether: lava beside the player sealed first (1: this step sealed), then the escape */
static int nt_esc_seal(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    if (!(v->macros & NETH_VEC_NETHER) || r->dim != -1 || r->screen != PS_NONE || e->nt.aseal >= 2 * NT_PLACE_TRIES)
        return 0;
    int lk = nt_lava_near(v, e, r);
    if (lk < 0) return 0;
    /* lane/rod (nether_esc_first): the player's own box in lava: out first (the escape's walk), no seal (on the
     * harvested starts 110 of 119 Nether deaths were in the lava sea with the escape running, its steps spent looking
     * down to seal the lava round a player standing in it) */
    if (MOPT(MO_NT_ESC_FIRST))
        for (int k = 0; k < 8; ++k)
        {
            int b = nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), (int)floor(r->y + (k & 4 ? 1.5 : 0.0)),
                             (int)floor(r->z + (k & 2 ? 0.3 : -0.3)));
            if (b == 10 || b == 11) return 0;
        }
    e->nt.aseal += 1;
    still(a);
    a->look_mode = 1;
    return nt_seal(v, e, r, s, a, ey, (int)floor(r->x) + AX[lk], (int)floor(r->y + 0.13) + AY[lk],
                   (int)floor(r->z) + AZ[lk]);
}

/* lane/rod (nether_feet_fill): the player held up by a block under one edge of its box (a harvested start's portal frame
 * over the lava sea: x -28.00 on the obsidian at -29), the cell under its own middle open or lava. Every bridge aims at
 * that cell's side and none ever placed (32 of 32 idle envs from s42-o22 stood there 24,000 steps). The cell is filled
 * first, against the supporting block's face that borders it: crouched (a crouched player never leaves its support), the
 * eye moved NT_FFILL_OFF off that face's plane toward the cell, then the crosshair on the face. 1 the step taken */
#define NT_FEET_FILL MOPT(MO_NT_FEET_FILL)
#define NT_FFILL_TRIES 60       /* steps at most an episode */
#define NT_FFILL_OFF 0.15       /* the eye's distance off the face's plane (on it the ray runs along the face) */
static int nt_feet_fill(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                        struct neth_rl_act *a, double ey)
{
    static const int OX[6] = {0, 0, 0, 0, -1, 1}, OY[6] = {-1, 1, 0, 0, 0, 0}, OZ[6] = {0, 0, -1, 1, 0, 0};
    struct nt_env *n = &e->nt;
    /* the arrival only (no cell of the way walked yet): a bridge's own step to the brink puts the middle over the gap */
    if (!NT_FEET_FILL || !r->on_ground || n->ffill >= NT_FFILL_TRIES || n->ncrumb > 1) return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z), cy = fy - 1;
    int b = nt_block(v, e, fx, cy, fz);
    if (b < 0 || !(nt_open(b) || b == 10 || b == 11)) return 0;
    int hb = nt_fill_slot(v, s);
    /* no cobblestone or netherrack in the hotbar (s42-o22's: pickaxes, obsidian, a bucket): any solid block there */
    for (int i = 0; i < 9 && hb < 0; ++i)
        if (s->inv[i].count > 0 && (s->inv[i].item == OBSIDIAN || s->inv[i].item == 3 || s->inv[i].item == 5))
            hb = i;
    if (hb < 0) return 0;
    for (int d = 0; d < 4; ++d)
    {
        int sb = nt_block(v, e, fx + NDX[d], cy, fz + NDZ[d]);
        if (sb <= 0 || nt_open(sb) || nt_lava(sb)) continue;
        int dd = NDX[d] ? NDX[d] : NDZ[d];
        double eye = NDX[d] ? r->x : r->z;
        double plane = (NDX[d] ? fx : fz) + (dd > 0 ? 1.0 : 0.0);
        if (!(dd > 0 ? eye + 0.3 > plane : eye - 0.3 < plane)) continue;     /* the box not over it: not the support */
        n->ffill += 1;
        still(a);
        a->look_mode = 1;
        a->hold = 1u << PK_SNEAK;
        if (hb != s->current) a->hotbar = hb;
        if ((plane - eye) * dd < NT_FFILL_OFF)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, r->x - NDX[d], ey, r->z - NDZ[d], &yaw, &pitch);
            a->look[0] = (float)yaw, a->look[1] = 60.0f;
            a->hold |= 1u << PK_FORWARD;
            return 1;
        }
        double yaw, pitch, tx = NDX[d] ? plane : fx + 0.5, tz = NDX[d] ? fz + 0.5 : plane;
        look_at(r->x, ey, r->z, tx, cy + 0.5, tz, &yaw, &pitch);
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        int side = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
        if (s->mo_hit && s->mo_x + OX[side] == fx && s->mo_y + OY[side] == cy && s->mo_z + OZ[side] == fz &&
            s->current == hb)
        {
            a->look[0] = r->yaw, a->look[1] = r->pitch;
            a->press[PK_USE] = 1;
            nt_placed(n, fx, cy, fz);
        }
        return 1;
    }
    return 0;
}

/* lane/nether4: a solid block under each corner of the player's box (0.3 out: in view, the cells under the feet). The
 * inventory screen releases the held keys, the crouch too: opened at a bridge's brink (the refill, its cobblestone run
 * out), the player slid off into the lava sea (14 and 12 of 32 gold-g7 envs at one step) */
static int nt_footing(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!NT_FOOTING) return 1;
    int fy = (int)floor(r->y + 0.13);
    for (int k = 0; k < 4; ++k)
    {
        int b = nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), fy - 1, (int)floor(r->z + (k & 2 ? 0.3 : -0.3)));
        if (b < 0 || nt_open(b) || nt_lava(b)) return 0;
    }
    return 1;
}

/* lane/rod (nether_portal_exit): in the portal (a corner of the box in a portal block) without a floor under each corner
 * (the frame's edge over the lava sea): the player walks through the portal, crouched, to the nearest cell within
 * NT_PEXIT_R whose feet and head are open (not the portal) over a solid floor, the cell between (a cell two off) with a
 * floor under it too. The client closes every screen while the player is in a portal (EntityPlayerSP's inPortal), so the
 * refill could never open the inventory there, and the walker judges the portal's cells bad: s42-o22's 32 envs stood at
 * the portal all episode. Standing in it keeps the server's portal cooldown, so the walk through sends nobody back. */
#define NT_PEXIT_R 2
#define NT_PEXIT_TRIES 200      /* steps at most an episode */
static int nt_floor_ok(int b) { return b > 0 && !nt_open(b) && !nt_lava(b) && b != 90; }
static int nt_portal_exit(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, struct neth_rl_act *a,
                          double ey)
{
    struct nt_env *n = &e->nt;
    if (!MOPT(MO_NT_PORTAL_EXIT) || n->pexit >= NT_PEXIT_TRIES || r->screen != PS_NONE) return 0;
    int fy = (int)floor(r->y + 0.13), in = 0, floor_all = 1;
    for (int k = 0; k < 8 && !in; ++k)
        in = nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), fy + (k >> 2),
                      (int)floor(r->z + (k & 2 ? 0.3 : -0.3))) == 90;
    if (!in) return 0;
    for (int k = 0; k < 4; ++k)
        floor_all &= nt_floor_ok(nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), fy - 1,
                                          (int)floor(r->z + (k & 2 ? 0.3 : -0.3))));
    if (floor_all) return 0;
    int fx = (int)floor(r->x), fz = (int)floor(r->z), bx = 0, bz = 0;
    double bd = 1e9;
    for (int dx = -NT_PEXIT_R; dx <= NT_PEXIT_R; ++dx)
        for (int dz = -NT_PEXIT_R; dz <= NT_PEXIT_R; ++dz)
        {
            int cx = fx + dx, cz = fz + dz;
            int fb = nt_block(v, e, cx, fy, cz), hb = nt_block(v, e, cx, fy + 1, cz);
            if (fb < 0 || hb < 0 || !nt_open(fb) || !nt_open(hb) || !nt_floor_ok(nt_block(v, e, cx, fy - 1, cz)))
                continue;
            int mx = fx + dx / 2, mz = fz + dz / 2;     /* the cell between (itself for a neighbour) */
            int mb = nt_block(v, e, mx, fy, mz), mh = nt_block(v, e, mx, fy + 1, mz);
            if ((mx != fx || mz != fz) && (!(nt_open(mb) || mb == 90) || !(nt_open(mh) || mh == 90) ||
                                           !nt_floor_ok(nt_block(v, e, mx, fy - 1, mz))))
                continue;
            double d = py_hypot2(cx + 0.5 - r->x, cz + 0.5 - r->z);
            if (d < bd) bd = d, bx = cx, bz = cz;
        }
    if (bd > 1e8) return 0;
    n->pexit += 1;
    double yaw, pitch;
    look_at(r->x, ey, r->z, bx + 0.5, ey, bz + 0.5, &yaw, &pitch);
    still(a);
    a->look_mode = 1;
    a->look[0] = (float)yaw, a->look[1] = 30.0f;
    a->hold = (1u << PK_SNEAK) | (1u << PK_FORWARD);
    n->wk = -1;
    return 1;
}

/* lane/nether4: the lava escape (a burn of 3 or more in a step) vetoed in the Nether with the switch on unless the
 * player's box touched lava within NT_ESC_LAVA steps (the books' lava_at, or lava at the box's cells now): a ghast's or a
 * blaze's fireball burns too, and the escape's forward and jump toward the last safe cell walked the player off the
 * fortress's deck into the lava sea (gold-g7 with natural spawning on: 24 of 32 envs at one step, 5069). 1 vetoed */
#define NT_ESC_LAVA 20
static int nt_esc_veto(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!(v->macros & NETH_VEC_NETHER) || r->dim != -1) return 0;
    if (e->nt.lava_at > 0 && e->steps - e->nt.lava_at <= NT_ESC_LAVA) return 0;
    for (int k = 0; k < 8; ++k)
    {
        int b = nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), (int)floor(r->y + (k & 4 ? 1.5 : 0.0)),
                         (int)floor(r->z + (k & 2 ? 0.3 : -0.3)));
        if (b == 10 || b == 11) return 0;
    }
    return 1;
}

/* lane/nether4: a ghast's fireball on the screen coming at the player (netherview_threats): the look on it and the
 * attack key once the crosshair is on it (EntityFireball.attackEntityFrom sends it back along the look), crouched and
 * still. A fireball NT_DEFLECT_D or nearer whose line passes within NT_DEFLECT_MISS of the eye. 1 the step taken */
#define NT_DEFLECT_D 7.0
#define NT_DEFLECT_MISS 2.5
#define NT_ARRIVAL_COVER_R 8.0
#define NT_ARRIVAL_GHAST_AGE 600
static int nt_deflect(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey, const struct netherview_threats *tp)
{
    struct nt_env *n = &e->nt;
    const struct netherview_threats th = *tp;
    (void)v;
    if (th.nghast > 0)
    {
        n->ghast_seen += 1;
        if (MOPT(MO_NT_ARRIVAL_COVER) && !n->bseek && n->t_arrive >= 0 &&
            e->steps - n->t_arrive <= NT_ARRIVAL_GHAST_AGE)
            n->bsx = th.gx, n->bsy = th.gy, n->bsz = th.gz, n->bseek_at = e->steps;
    }
    for (int i = 0; i < th.nball; ++i)
    {
        if (!th.ball[i].large || th.ball[i].d > NT_DEFLECT_D) continue;
        double bx = th.ball[i].x, by = th.ball[i].y + 0.5, bz = th.ball[i].z;
        double mx = th.ball[i].mx, my = th.ball[i].my, mz = th.ball[i].mz, m2 = mx * mx + my * my + mz * mz;
        double wx = r->x - bx, wy = ey - by, wz = r->z - bz, along = wx * mx + wy * my + wz * mz;
        if (m2 < 1e-6 || along <= 0) continue;               /* still, or going away */
        double t = along / m2, cx = bx + mx * t - r->x, cy = by + my * t - ey, cz = bz + mz * t - r->z;
        if (cx * cx + cy * cy + cz * cz > NT_DEFLECT_MISS * NT_DEFLECT_MISS) continue;
        if (n->deflect_id != th.ball[i].id) n->deflect_id = th.ball[i].id, n->deflects += 1;
        double yaw, pitch;
        look_at(r->x, ey, r->z, bx, by, bz, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = 1u << PK_SNEAK;
        if (s->mo_entity == th.ball[i].id) a->press[PK_ATTACK] = 1, n->deflect_hits += 1;
        return 1;
    }
    return 0;
}

/* lane/nether4: cover from a blaze out of reach. A blaze shoots only while it sees the player's eye (EntityBlaze's
 * canEntityBeSeen, eye to eye) and paths to its target while it cannot: a fill block in the first cell the line from
 * the eye to the blaze crosses outside the player's own two cells (beside the head, or over it) breaks the sight, and the
 * blaze comes round to within reach, where the lock fights it. The block goes in by a face of a solid neighbour of the
 * cell (the one under it, beyond it, over it or beside it, in that order), the crosshair on that face; with none, the
 * cell under it first (a column of two). NT_COVER_MAX blocks at one feet cell, NT_COVER_TRIES steps a block. 1 the step
 * taken */
#ifndef NT_COVER_D
#define NT_COVER_D 3.6          /* a blaze in view farther than this (beyond the hit's reach) is walled off */
#endif
#define NT_COVER_MAX 10
#define NT_COVER_TRIES 12
#define NT_COVER_OFF 60         /* steps without cover after a block could not be placed */
#define NT_COVER MOPT(MO_NT_COVER)
static int nt_cover_place(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int tx, int ty, int tz, int depth,
                          int material)
{
    static const int OX[6] = {0, 0, 0, 0, -1, 1}, OY[6] = {-1, 1, 0, 0, 0, 0}, OZ[6] = {0, 0, -1, 1, 0, 0};
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    /* A side block touching the player's box cannot be placed even with the correct face under the crosshair.
     * Centre on the current solid floor first; the side cell then clears the box by 0.2 block. */
    if (MOPT(MO_NT_FAR_DIG) == 2 && material && ty == fy && abs(tx - fx) + abs(tz - fz) == 1 &&
        r->x + 0.3 > tx && r->x - 0.3 < tx + 1 && r->z + 0.3 > tz && r->z - 0.3 < tz + 1 &&
        py_hypot2(fx + 0.5 - r->x, fz + 0.5 - r->z) > 0.08)
    {
        double yaw, pitch;
        look_at(r->x, ey, r->z, fx + 0.5, ey, fz + 0.5, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
        return 1;
    }
    int ddx = tx - fx, ddz = tz - fz;
    /* the neighbours of the cell, the order: under, beyond (away from the player), over, the two beside */
    int nb[5][3] = {{tx, ty - 1, tz}, {tx + ddx, ty, tz + ddz}, {tx, ty + 1, tz}, {tx + ddz, ty, tz + ddx},
                    {tx - ddz, ty, tz - ddx}};
    if (ddx == 0 && ddz == 0)
    {
        /* over the head: the cell over it, then the four beside it */
        int roof[5][3] = {{tx, ty + 1, tz}, {tx + 1, ty, tz}, {tx - 1, ty, tz}, {tx, ty, tz + 1}, {tx, ty, tz - 1}};
        memcpy(nb, roof, sizeof nb);
    }
    int hb = material ? -1 : nt_fill_slot(v, s);
    if (material)
        for (int i = 0; i < 9; ++i)
            if (s->inv[i].item == material && s->inv[i].count > 0) { hb = i; break; }
    if (hb < 0) return 0;
#ifdef NT_FAR_DEBUG
    if (material && ty >= fy + 2 && e->steps % 1000 == 0)
    {
        fprintf(stderr, "far-support step=%lld target=%d,%d,%d player=%.2f,%.2f,%.2f blocks=",
                (long long)e->steps, tx, ty, tz, r->x, r->y, r->z);
        for (int j = 0; j < 5; ++j)
            fprintf(stderr, "%s%d:%d,%d,%d", j ? " " : "", nt_block(v, e, nb[j][0], nb[j][1], nb[j][2]),
                    nb[j][0], nb[j][1], nb[j][2]);
        fprintf(stderr, "\n");
    }
#endif
    for (int k = 0; k < 5; ++k)
    {
        int b = nt_block(v, e, nb[k][0], nb[k][1], nb[k][2]);
        if (b < 0 || nt_open(b) || nt_lava(b) || b == 8 || b == 9 || b == 90 || b == 52) continue;
        if ((nb[k][0] == fx && nb[k][2] == fz) || (k == 1 && ddx == 0 && ddz == 0)) continue;
        double px = (tx + nb[k][0]) * 0.5 + 0.5, py = (ty + nb[k][1]) * 0.5 + 0.5, pz = (tz + nb[k][2]) * 0.5 + 0.5;
        double yaw, pitch;
        look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = 1u << PK_SNEAK;
        if (hb != s->current) a->hotbar = hb;
        int side = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
#ifdef NT_FAR_DEBUG
        if (e->steps % 500 == 0 && tx < -90 && tx > -125 && tz > 100 && tz < 135)
            fprintf(stderr, "far-place step=%lld target=%d,%d,%d support=%d,%d,%d b=%d side=%d hit=%d:%d,%d,%d hb=%d cur=%d press=%d\n",
                    (long long)e->steps, tx, ty, tz, nb[k][0], nb[k][1], nb[k][2], b, side,
                    s->mo_block, s->mo_x, s->mo_y, s->mo_z, hb, s->current, a->press[PK_USE]);
#endif
        if (s->mo_hit && s->current == hb && s->mo_x + OX[side] == tx && s->mo_y + OY[side] == ty &&
            s->mo_z + OZ[side] == tz)
        {
            a->look[0] = r->yaw, a->look[1] = r->pitch;
            a->press[PK_USE] = 1;
            nt_placed(&e->nt, tx, ty, tz);
            e->nt.covers += 1;
        }
        return 1;
    }
    /* no solid neighbour: the cell under it first (a beside-the-head cell over an open feet-level one) */
    if (depth == 0 && ty > fy - 1 && nt_open(nt_block(v, e, tx, ty - 1, tz)) && !(tx == fx && tz == fz))
        return nt_cover_place(v, e, r, s, a, ey, tx, ty - 1, tz, 1, material);
    return 0;
}

/* A block backed by rock can be netherrack; one facing open cavern must withstand a ghast blast. */
static int nt_far_wall_material(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s,
                                int x, int y, int z, int bx, int by, int bz)
{
    int dx = (x > bx) - (x < bx), dy = (y > by) - (y < by), dz = (z > bz) - (z < bz);
    int behind = netherview_oracle_block(v->p, nt_id(v, e), x + dx, y + dy, z + dz);
    return nt_far_solid(behind) && inv_count(s, 87) > 0 ? 87 : COBBLE;
}

static int nt_far_supply(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, int material)
{
    for (int k = 0; k < 9; ++k)
        if (s->inv[k].item == material && s->inv[k].count > 0) return 0;
    if (nt_refill(v, e, r, s, a)) return 1;
    still(a), a->hold = 1u << PK_SNEAK;
    return 1;
}

/* The far corridor's next standing cell gets a floor, side walls and a roof before the player enters it. */
static int nt_far_enclose(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey,
                          int x, int y, int z, int dx, int dz)
{
    int sx = dz != 0, sz = dx != 0;
    int faces[7][3] = {{x + sx, y, z + sz}, {x - sx, y, z - sz},
                       {x + sx, y + 1, z + sz}, {x - sx, y + 1, z - sz},
                       {x + sx, y + 2, z + sz}, {x - sx, y + 2, z - sz}, {x, y + 2, z}};
    int floor_b = netherview_oracle_block(v->p, nt_id(v, e), x, y - 1, z);
    if (y < 40 || !nt_far_solid(floor_b))
    {
#ifdef NT_FAR_DEBUG
        if (e->steps % 500 == 0)
            fprintf(stderr, "far-floor step=%lld cell=%d,%d,%d floor=%d\n", (long long)e->steps, x, y, z, floor_b);
#endif
        if (nt_open(floor_b) || floor_b == 51)
        {
            int material = nt_far_wall_material(v, e, s, x, y - 1, z, x, y, z);
            if (nt_far_supply(v, e, r, s, a, material)) return 1;
            if (nt_cover_place(v, e, r, s, a, ey, x, y - 1, z, 0, material)) return 1;
        }
        still(a), a->hold = 1u << PK_SNEAK;
        return 1;
    }
    for (int i = 0; i < 7; ++i)
    {
        int x1 = faces[i][0], y1 = faces[i][1], z1 = faces[i][2];
        int b = netherview_oracle_block(v->p, nt_id(v, e), x1, y1, z1);
        if (nt_far_solid(b)) continue;
        if (b == 51)
        {
            if (nt_far_supply(v, e, r, s, a, COBBLE)) return 1;
            if (nt_cover_place(v, e, r, s, a, ey, x1, y1, z1, 0, COBBLE)) return 1;
            still(a), a->hold = 1u << PK_SNEAK;
            return 1;
        }
        if (b < 0 || b == 10 || b == 11 || b == 8 || b == 9)
        {
#ifdef NT_FAR_DEBUG
            if (e->steps % 500 == 0)
                fprintf(stderr, "far-face-veto step=%lld cell=%d,%d,%d block=%d\n",
                        (long long)e->steps, x1, y1, z1, b);
#endif
            still(a), a->hold = 1u << PK_SNEAK;
            return 1;
        }
        int material = nt_far_wall_material(v, e, s, x1, y1, z1, x, y, z);
#ifdef NT_FAR_DEBUG
        if (e->steps % 1000 == 0)
            fprintf(stderr, "far-face step=%lld player=%.2f,%.2f,%.2f face=%d,%d,%d block=%d mat=%d held=%d cobble=%lld rack=%lld mo=%d:%d,%d,%d\n",
                    (long long)e->steps, r->x, r->y, r->z, x1, y1, z1, b, material,
                    s->current, (long long)inv_count(s, COBBLE), (long long)inv_count(s, 87),
                    s->mo_block, s->mo_x, s->mo_y, s->mo_z);
#endif
        if (nt_far_supply(v, e, r, s, a, material)) return 1;
        if (nt_cover_place(v, e, r, s, a, ey, x1, y1, z1, 0, material)) return 1;
        still(a), a->hold = 1u << PK_SNEAK;
        return 1;
    }
    return 0;
}

static int nt_arrival_cover(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                            const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!MOPT(MO_NT_ARRIVAL_COVER) || n->t_arrive < 0 || n->bseek_at < n->t_arrive || n->has_fort ||
        e->steps - n->bseek_at > NT_ARRIVAL_GHAST_AGE || !n->has_portal ||
        py_hypot2(r->x - n->px, r->z - n->pz) > NT_ARRIVAL_COVER_R)
        return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    double dx = n->bsx - r->x, dy = n->bsy - ey, dz = n->bsz - r->z;
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d < 1.0) return 0;
    for (double t = 0.05; t < d && t < 3.5; t += 0.05)
    {
        int tx = (int)floor(r->x + dx * t / d), ty = (int)floor(ey + dy * t / d),
            tz = (int)floor(r->z + dz * t / d);
        if (tx == fx && tz == fz && (ty == fy || ty == fy + 1)) continue;
        int b = nt_block(v, e, tx, ty, tz);
        if (b == 90) continue; /* the arrival portal stays intact */
        if (!nt_open(b)) return 0; /* solid rock already blocks this line */
        if (nt_cover_place(v, e, r, s, a, ey, tx, ty, tz, 0, 0)) return 1;
        still(a);
        a->hold = 1u << PK_SNEAK;
        return 1;
    }
    return 0;
}

static int nt_cover(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey, const struct netherview_threats *th)
{
    struct nt_env *n = &e->nt;
    if (!NT_COVER || th->nblaze == 0 || th->blaze_d <= NT_COVER_D || !r->on_ground || e->steps < n->cov_off)
        return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (fx != n->cov_fx || fy != n->cov_fy || fz != n->cov_fz)
        n->cov_fx = fx, n->cov_fy = fy, n->cov_fz = fz, n->cov_here = 0, n->cov_age = 0;
    if (n->cov_here >= NT_COVER_MAX) return 0;
    /* the first cell the line of sight crosses outside the player's feet and head cells */
    double dx = th->bx - r->x, dy = th->by - ey, dz = th->bz - r->z, d = sqrt(dx * dx + dy * dy + dz * dz);
    int tx = fx, ty = fy + 1, tz = fz, found = 0;
    for (double t = 0.05; t < d && t < 3.0; t += 0.05)
    {
        int cx = (int)floor(r->x + dx * t / d), cy = (int)floor(ey + dy * t / d), cz = (int)floor(r->z + dz * t / d);
        if (cx == fx && cz == fz && (cy == fy || cy == fy + 1)) continue;
        tx = cx, ty = cy, tz = cz, found = 1;
        break;
    }
    if (!found || ty < fy || !nt_open(nt_block(v, e, tx, ty, tz))) return 0;
    int before = n->covers;
    if (++n->cov_age > NT_COVER_TRIES || !nt_cover_place(v, e, r, s, a, ey, tx, ty, tz, 0, 0))
    {
        n->cov_age = 0, n->cov_off = e->steps + NT_COVER_OFF;
        return 0;
    }
    /* a press counts against the cell's NT_COVER_MAX whether the block took or not (a blaze in the cell refuses it) */
    if (n->covers > before) n->cov_here += 1, n->cov_age = 0;
    return 1;
}

/* lane/nether4: the hood, a heal the blazes cannot reach. Hurt at the blazes (NT_LOW_HP), the player walls in where it
 * stands: the cell over the head and the four beside it, each filled where open (nt_cover_place: a column of two where
 * the feet-level cell beside is open too). A blaze sees the eye only, and the eye is in the head's cell, so no blaze
 * sees in but one below it at a steep angle. Inside, crouched and still until NT_HEAL_HP (the food macro eats meanwhile:
 * regeneration wants food 18); then the player's own blocks toward the spawner (head and feet) dug out, and the camp
 * again. 1 the step taken */
#define NT_GLANCE MOPT(MO_NT_GLANCE)  /* lane/nether4: the glance round (nt_drive1) */
#define NT_GLANCE_EVERY 60
#ifndef NT_STUCK_STEPS
#define NT_STUCK_STEPS 800      /* lane/nether4: the unstick (nt_drive1): steps without NT_STUCK_D of progress */
#endif
#define NT_STUCK_D 3.0
#define NT_HOOD_TRIES 40        /* steps for one cell before it is given up (the hood is kept without it) */
#define NT_HOOD_PRESS 4         /* presses of the use key for one cell (two for a column of two, and retries) */
#define NT_HOOD_MID 0.12        /* the hood built from within this of the cell's middle */
#define NT_HOOD_KEEP 3          /* lane/armed (nether_hood_keep): a hood built again at most this many times */
#define NT_FIRE_ID 51           /* fire */
#define NT_HOOD MOPT(MO_NT_HOOD)
#define NT_REGEN_FOOD 18        /* lane/nleg: 1.7.10's natural regeneration needs the food level at this or over */
/* lane/nleg (nether_hood_starve): no heal is coming: the food level under NT_REGEN_FOOD and nothing the food macro would
 * eat held (seed 1's s1endrank10 with nether_camp_side 1: 19 of 64 sat in the hood at 11 health and food 17, no food
 * held, waiting for NT_HEAL_HP to the bound) */
static int nt_noregen(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return MOPT(MO_NT_HOOD_STARVE) && r->food < NT_REGEN_FOOD && !food_held(v, s, r->food);
}
#define NT_ANY_BLAZE MOPT(MO_NT_ANY_BLAZE)  /* a blaze fight strikes any blaze under the crosshair, not only its lock's; 0 off (1
                                 * measured worse: gold-g7 spawning off seed 1, the rod 24 to 0 of 32, 24 dead by fire) */
#define NT_NEAR_STOP_D 4.0      /* nether_near_stop on (lane/macroopts: the -DNT_NEAR_D=4.0 variant as an option) */
#define NT_NEAR_D (MOPT(MO_NT_NEAR_STOP) ? NT_NEAR_STOP_D : 0.0)
                                /* a blaze in view nearer than this (eye to its middle): no hood begun, no glance; 0 off
                                 * (4.0 measured worse: gold-g7 spawning off, the rod 24 to 0 of 32, deaths at 15,871
                                 * against 18,067: the fight's lock at 1 to 2 blocks lost to the melee) */
static int nt_hood(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (n->hood == 0)
    {
        if (!r->on_ground) return 0;
        /* lane/rods (nether_hood_roof skip): no hood where its roof cannot go in: the cell over the head's open with no
         * solid neighbour to place it against (the one over it and the four beside it open: a room's high ceiling). The
         * rod hunt's starts under near_stop and hood_starve rest: 34 of 83 hood deaths took most hits with the roof
         * open, the cell over it air on about 96% of those steps */
        if (MOPT(MO_NT_HOOD_ROOF) && nt_open(nt_block(v, e, fx, fy + 2, fz)))
        {
            int face = !nt_open(nt_block(v, e, fx, fy + 3, fz));
            for (int d = 0; d < 4 && !face; ++d) face = !nt_open(nt_block(v, e, fx + NDX[d], fy + 2, fz + NDZ[d]));
            if (!face) return 0;
        }
        n->hood = 1, n->hx = fx, n->hy = fy, n->hz = fz, n->hood_k = 0, n->hood_age = 0, n->hoods += 1;
        n->hood_yaw = r->yaw;               /* lane/nether5: the way out of a hood with no spawner or goal */
        n->hood_press = 0, n->hood_mid = 0, n->hood_keeps = 0;
    }
    /* lane/armed (nether_hood_keep): healing, the feet out of the hood's cell or a cell it covers open again (air or
     * fire: a fireball's fire in a side cell reads as shut to the build): built again (only the open cells are
     * placed), at most NT_HOOD_KEEP times a hood; the feet more than a block off or at another height: a new hood here
     * (k50 a52's entry start: 28 of 55 deaths in a hood with a cell open or the feet knocked out of its cell) */
    if (n->hood == 2 && MOPT(MO_NT_HOOD_KEEP) && n->hood_keeps < NT_HOOD_KEEP)
    {
        int in = fx == n->hx && fy == n->hy && fz == n->hz, open = !in;
        for (int k = 0; k < (MOPT(MO_NT_HOOD_FULL) ? 9 : 5) && !open; ++k)
        {
            int q = k > 4 ? k - 4 : k;
            int b = nt_block(v, e, n->hx + (k ? NDX[q - 1] : 0), n->hy + (k > 4 ? 0 : k ? 1 : 2),
                             n->hz + (k ? NDZ[q - 1] : 0));
            open = nt_open(b) || b == NT_FIRE_ID;
        }
        if (open)
        {
            if (abs(fx - n->hx) > 1 || abs(fz - n->hz) > 1 || fy != n->hy)
            {
                if (!r->on_ground) return 0;
                n->hx = fx, n->hy = fy, n->hz = fz, n->hood_yaw = r->yaw;
            }
            n->hood = 1, n->hood_k = 0, n->hood_age = 0, n->hood_press = 0, n->hood_mid = 0;
            n->hood_keeps += 1;
        }
    }
    if (n->hood == 1)
    {
        /* first to the cell's middle (a player's box over a side cell refuses a block there), crouched */
        double cx = n->hx + 0.5 - r->x, cz = n->hz + 0.5 - r->z;
        if (n->hood_k == 0 && n->hood_press == 0 && py_hypot2(cx, cz) > NT_HOOD_MID && ++n->hood_mid <= NT_HOOD_TRIES)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, n->hx + 0.5, ey - 1.0, n->hz + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
            return 1;
        }
        /* building: the roof, then the four sides; NT_HOOD_PRESS presses a cell at most */
        /* lane/rod (nether_hood_full): then the four sides at the feet too (gold-g7: a skeleton 13 to 15 blocks off shot
         * the healing player through the open feet level, each hit knocked it out of the hood: 110 of 128 frozen runs
         * died so at one step once nether_safe_dig was on) */
        while (n->hood_k < (MOPT(MO_NT_HOOD_FULL) ? 9 : 5))
        {
            int k = n->hood_k, q = k > 4 ? k - 4 : k;
            int tx = n->hx + (k ? NDX[q - 1] : 0), ty = n->hy + (k > 4 ? 0 : k ? 1 : 2), tz = n->hz + (k ? NDZ[q - 1] : 0);
            int tb = nt_block(v, e, tx, ty, tz);
            int topen = nt_open(tb) || (MOPT(MO_NT_HOOD_KEEP) && tb == NT_FIRE_ID);   /* lane/armed: fire replaced */
            if (!topen || ++n->hood_age > NT_HOOD_TRIES || n->hood_press >= NT_HOOD_PRESS)
            {
                n->hood_k += 1, n->hood_age = 0, n->hood_press = 0;
                continue;
            }
            int before = n->covers;
            if (nt_cover_place(v, e, r, s, a, ey, tx, ty, tz, 0, 0))
            {
                if (n->covers > before) n->hood_press += 1;
                return 1;
            }
            n->hood_k += 1, n->hood_age = 0, n->hood_press = 0;
        }
        n->hood = 2;
        n->hood_hp = r->health;     /* lane/rod */
    }
    if (n->hood == 2)
    {
        /* lane/rod (nether_hood_hit): hurt inside the hood, it does not cover the player (gold-g7: a skeleton 13 to 15
         * blocks off in view shot it through, 110 of 128 frozen runs died healing there): given up NT_HOOD_HIT_OFF
         * steps, the fight's rules next (a skeleton within 16 fought) */
        if (MOPT(MO_NT_HOOD_HIT) && r->health < n->hood_hp)
        {
            n->hood = 0, n->hood_off = e->steps + NT_HOOD_HIT_OFF;
            return 0;
        }
        n->hood_hp = r->health;
        if (r->health < NT_HEAL_HP && !nt_noregen(v, r, s))
        {
            still(a);
            a->look_mode = 1;
            a->look[0] = r->yaw, a->look[1] = 0.0f;
            a->hold = 1u << PK_SNEAK;
            return 1;
        }
        n->hood = 3, n->hood_k = 0, n->hood_age = 0;
    }
    /* healed: out toward the spawner (its cardinal), the player's own blocks dug, head then feet */
    /* lane/nether5: a hood before any spawner or camp: out toward the search's goal, else the way the look faces */
    double hyr = (double)n->hood_yaw * M_PI / 180.0;
    int ox = n->has_sp ? n->sx : n->has_camp ? n->cx : n->has_goal ? n->gx : n->hx - (int)lround(4.0 * sin(hyr));
    int oz = n->has_sp ? n->sz : n->has_camp ? n->cz : n->has_goal ? n->gz : n->hz + (int)lround(4.0 * cos(hyr));
    double dx = ox + 0.5 - r->x, dz = oz + 0.5 - r->z;
    int d = fabs(dx) >= fabs(dz) ? (dx >= 0 ? 3 : 1) : (dz >= 0 ? 0 : 2);
    while (n->hood_k < 2)
    {
        int tx = n->hx + NDX[d], ty = n->hy + 1 - n->hood_k, tz = n->hz + NDZ[d], b = nt_block(v, e, tx, ty, tz);
        if (nt_open(b) || !nt_own(n, tx, ty, tz) || ++n->hood_age > NT_HOOD_TRIES)
        {
            n->hood_k += 1, n->hood_age = 0;
            continue;
        }
        double yaw, pitch;
        look_at(r->x, ey, r->z, tx + 0.5, ty + 0.5, tz + 0.5, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = 1u << PK_SNEAK;
        if (s->mo_hit && s->mo_x == tx && s->mo_y == ty && s->mo_z == tz)
        {
            a->hold |= 1u << PK_ATTACK;
            int best = nt_tool(v, s, b);
            if (best >= 0 && best != s->current) a->hotbar = best;
        }
        return 1;
    }
    n->hood = 0;
    return 0;
}

/* the macro's start (macro_start, after eating): 1 when it took the step */
#define NT_SP_BREAK_MAX 3000
#ifdef SP_BREAK_TRACE
#define NT_SP_BREAK_MARK (1LL << 40)
static int nt_sp_break_blazes(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    struct netherview_blaze bz[64];
    return netherview_oracle_blazes(v->p, nt_id(v, e), r->x, r->y + 1.62, r->z, 16.0, bz, 64);
}
#endif
static int nt_sp_break_pending(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    const struct nt_env *n = &e->nt;
    return MOPT(MO_NT_SP_BREAK) && n->has_sp && inv_count(s, BLAZE_ROD) >= 7 &&
           (!n->sp_break_at || e->steps - n->sp_break_at < NT_SP_BREAK_MAX) &&
           nt_block(v, e, n->sx, n->sy, n->sz) == 52;
}

/* An enclosed standing cell near the seen spawner, reached by a tunnel rather than by the arrival portal's place.
 * A checkpoint made at the spawner has no portal in its macro memory (seed 1 node 19); this route does not need one. */
#define NT_CHAMBER_CELLS 5
#define NT_CHAMBER_SP_DIST 8.0
#define NT_CHAMBER_R 8
static int nt_chamber_sturdy(int block)
{
    if (block <= 0 || block >= 4096 || block == 12 || block == 13 || block == 122 || block == 145)
        return 0;
    return BLOCKS[block].opaque_cube && BLOCKS[block].normal_cube && BLOCKS[block].collidable;
}
/* Sight from a future standing cell: its feet and head are about to be dug.
 * Ignore only those two cells, then sample the client-known full blocks up to the spawner. */
static int nt_chamber_sp_hidden_after_dig(const struct neth_vec *v, const struct vec_env *e,
                                           int x, int y, int z)
{
    const struct nt_env *n = &e->nt;
    double ex = x + 0.5, ey = y + 1.62, ez = z + 0.5;
    double dx = n->sx + 0.5 - ex, dy = n->sy + 0.5 - ey, dz = n->sz + 0.5 - ez;
    int samples = (int)ceil(20.0 * sqrt(dx * dx + dy * dy + dz * dz));
    for (int i = 1; i < samples; ++i)
    {
        double t = (double)i / samples;
        int bx = (int)floor(ex + t * dx), by = (int)floor(ey + t * dy), bz = (int)floor(ez + t * dz);
        if (bx == x && bz == z && (by == y || by == y + 1)) continue;
        if (bx == n->sx && by == n->sy && bz == n->sz) return 0;
        int b = nt_block(v, e, bx, by, bz);
        if (b < 0) return -1;
        if (b > 0 && b < 4096 && BLOCKS[b].opaque_cube && BLOCKS[b].collidable) return 1;
    }
    return 0;
}
static int nt_chamber_pick(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                           int cx, int cy, int cz, int radius)
{
    const struct nt_env *n = &e->nt;
    if (!n->has_sp) return 0;
    int solid = MOPT(MO_NT_CHAMBER_SOLID);
    double best = -1e9;
    int bx = 0, by = 0, bz = 0;
    for (int dy = -12; dy <= -4; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                int x = cx + dx, y = cy + dy, z = cz + dz;
                double dist = py_dist3(r->x, r->y, r->z, x + 0.5, y, z + 0.5);
                if (y < 10 || dist < 3.0 || dist > 18.0 ||
                    py_dist3(x + 0.5, y, z + 0.5, n->sx + 0.5, n->sy + 0.5, n->sz + 0.5) < NT_CHAMBER_SP_DIST)
                    continue;
                int floor_b = nt_block(v, e, x, y - 1, z);
                int roof2 = nt_block(v, e, x, y + 2, z), roof3 = nt_block(v, e, x, y + 3, z);
                if (solid ? (!nt_chamber_sturdy(floor_b) || !nt_chamber_sturdy(roof2) ||
                             !nt_chamber_sturdy(roof3))
                          : (floor_b != 87 || roof2 != 87 || roof3 != 87)) continue;
                int feet = nt_block(v, e, x, y, z), head = nt_block(v, e, x, y + 1, z), walls = 0, lava = 0;
                if (feet < 0 || head < 0 || feet == 10 || feet == 11 || head == 10 || head == 11) continue;
                for (int k = 0; k < 4; ++k)
                {
                    int side = nt_block(v, e, x + NDX[k], y + 1, z + NDZ[k]);
                    walls += solid ? nt_chamber_sturdy(side) : side == 87;
                    for (int h = -1; h <= 3; ++h)
                    {
                        int b = nt_block(v, e, x + NDX[k], y + h, z + NDZ[k]);
                        lava |= b == 10 || b == 11;
                    }
                }
                if (walls < 2 || lava) continue;
                if (solid && nt_chamber_sp_hidden_after_dig(v, e, x, y, z) != 1) continue;
                double score = 100.0 * (nt_open(feet) && nt_open(head)) + 10.0 * walls - dist -
                               2.0 * abs(dy + 8);
                if (score > best) best = score, bx = x, by = y, bz = z;
            }
    if (best < -1e8)
    {
#ifdef CHAMBER_DIAG
        int counts[10] = {0}, nearest_fail = 99, nearest_x = 0, nearest_y = 0, nearest_z = 0;
        unsigned nearest_mask = 0;
        double nearest_dist = 1e9;
        for (int dy = -12; dy <= -4; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
                for (int dz = -radius; dz <= radius; ++dz)
                {
                    int x = cx + dx, y = cy + dy, z = cz + dz;
                    double dist = py_dist3(r->x, r->y, r->z, x + 0.5, y, z + 0.5);
                    double spdist = py_dist3(x + 0.5, y, z + 0.5, n->sx + 0.5, n->sy + 0.5, n->sz + 0.5);
                    int feet = nt_block(v, e, x, y, z), head = nt_block(v, e, x, y + 1, z);
                    int walls = 0, lava = 0;
                    unsigned mask = 0;
                    if (y < 10) mask |= 1u << 0;
                    if (dist < 3.0 || dist > 18.0) mask |= 1u << 1;
                    if (spdist < NT_CHAMBER_SP_DIST) mask |= 1u << 2;
                    if (nt_block(v, e, x, y - 1, z) != 87) mask |= 1u << 3;
                    if (nt_block(v, e, x, y + 2, z) != 87) mask |= 1u << 4;
                    if (nt_block(v, e, x, y + 3, z) != 87) mask |= 1u << 5;
                    if (feet < 0 || head < 0 || feet == 10 || feet == 11 || head == 10 || head == 11)
                        mask |= 1u << 6;
                    for (int k = 0; k < 4; ++k)
                    {
                        walls += nt_block(v, e, x + NDX[k], y + 1, z + NDZ[k]) == 87;
                        for (int h = -1; h <= 3; ++h)
                        {
                            int b = nt_block(v, e, x + NDX[k], y + h, z + NDZ[k]);
                            lava |= b == 10 || b == 11;
                        }
                    }
                    if (walls < 2) mask |= 1u << 7;
                    if (lava) mask |= 1u << 8;
                    for (int j = 0; j < 9; ++j) counts[j] += (mask >> j) & 1u;
                    int failed = __builtin_popcount(mask);
                    if (failed < nearest_fail || (failed == nearest_fail && dist < nearest_dist))
                        nearest_fail = failed, nearest_dist = dist, nearest_mask = mask,
                        nearest_x = x, nearest_y = y, nearest_z = z;
                }
        fprintf(stderr, "CHAMBER_PICK_FAIL env=%d step=%lld player=%.2f,%.2f,%.2f center=%d,%d,%d radius=%d "
                "near=%d,%d,%d fail=%d mask=%x dist=%.2f reasons=y,range,spdist,floor,roof2,roof3,cell,walls,lava "
                "counts=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", nt_id(v, e), (long long)e->steps,
                r->x, r->y, r->z, cx, cy, cz, radius, nearest_x, nearest_y, nearest_z,
                nearest_fail, nearest_mask, nearest_dist, counts[0], counts[1], counts[2], counts[3],
                counts[4], counts[5], counts[6], counts[7], counts[8]);
        if (radius == NT_CHAMBER_R)
        {
            static int extra_done;
            if (!extra_done)
            {
                extra_done = 1;
                int generic = 0, generic_open = 0, sp_hidden = 0, open_both_hidden = 0, unknown = 0;
                int near_x = 0, near_y = 0, near_z = 0, safe_x = 0, safe_y = 0, safe_z = 0;
                int near_sight = -2;
                double near_d = 1e9, safe_d = 1e9;
                for (int dy = -12; dy <= -4; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx)
                        for (int dz = -radius; dz <= radius; ++dz)
                        {
                            int x = cx + dx, y = cy + dy, z = cz + dz;
                            double dist = py_dist3(r->x, r->y, r->z, x + 0.5, y, z + 0.5);
                            double spdist = py_dist3(x + 0.5, y, z + 0.5,
                                                     n->sx + 0.5, n->sy + 0.5, n->sz + 0.5);
                            if (y < 10 || dist < 3.0 || dist > 18.0 || spdist < NT_CHAMBER_SP_DIST ||
                                !nt_chamber_sturdy(nt_block(v, e, x, y - 1, z)) ||
                                !nt_chamber_sturdy(nt_block(v, e, x, y + 2, z)) ||
                                !nt_chamber_sturdy(nt_block(v, e, x, y + 3, z))) continue;
                            int feet = nt_block(v, e, x, y, z), head = nt_block(v, e, x, y + 1, z);
                            if (feet < 0 || head < 0 || feet == 10 || feet == 11 ||
                                head == 10 || head == 11) continue;
                            int walls = 0, lava = 0;
                            for (int k = 0; k < 4; ++k)
                            {
                                walls += nt_chamber_sturdy(nt_block(v, e, x + NDX[k], y + 1, z + NDZ[k]));
                                for (int h = -1; h <= 3; ++h)
                                {
                                    int b = nt_block(v, e, x + NDX[k], y + h, z + NDZ[k]);
                                    lava |= b == 10 || b == 11;
                                }
                            }
                            if (walls < 2 || lava) continue;
                            ++generic;
                            int sight = nt_chamber_sp_hidden_after_dig(v, e, x, y, z);
                            if (dist < near_d)
                                near_d = dist, near_x = x, near_y = y, near_z = z, near_sight = sight;
                            if (sight < 0) ++unknown;
                            if (sight > 0)
                            {
                                ++sp_hidden;
                                if (dist < safe_d) safe_d = dist, safe_x = x, safe_y = y, safe_z = z;
                            }
                            if (!nt_open(feet) || !nt_open(head)) continue;
                            ++generic_open;
                            double ex = x + 0.5, ey = y + 1.62, ez = z + 0.5;
                            if (sight > 0 && netherview_exit_blaze_los(v->p, nt_id(v, e), ex, ey, ez) == 0)
                                ++open_both_hidden;
                        }
                fprintf(stderr, "CHAMBER_GENERIC env=%d step=%lld candidates=%d open=%d sp_hidden=%d "
                        "unknown=%d open_sp_and_blaze_hidden=%d nearest=%d,%d,%d dist=%.2f nearest_sight=%d "
                        "safe=%d,%d,%d safe_dist=%.2f\n", nt_id(v, e), (long long)e->steps,
                        generic, generic_open, sp_hidden, unknown, open_both_hidden,
                        near_x, near_y, near_z, near_d, near_sight, safe_x, safe_y, safe_z, safe_d);
                const int radii[3] = {16, 24, 32};
                for (int ri = 0; ri < 3; ++ri)
                {
                    int rr = radii[ri], count = 0, nx = 0, ny = 0, nz = 0;
                    double nd = 1e9;
                    for (int dy = -12; dy <= -4; ++dy)
                        for (int dx = -rr; dx <= rr; ++dx)
                            for (int dz = -rr; dz <= rr; ++dz)
                            {
                                int x = cx + dx, y = cy + dy, z = cz + dz;
                                double dist = py_dist3(r->x, r->y, r->z, x + 0.5, y, z + 0.5);
                                double spdist = py_dist3(x + 0.5, y, z + 0.5,
                                                         n->sx + 0.5, n->sy + 0.5, n->sz + 0.5);
                                if (y < 10 || dist < 3.0 || dist > 18.0 || spdist < NT_CHAMBER_SP_DIST ||
                                    nt_block(v, e, x, y - 1, z) != 87 ||
                                    nt_block(v, e, x, y + 2, z) != 87 ||
                                    nt_block(v, e, x, y + 3, z) != 87) continue;
                                int feet = nt_block(v, e, x, y, z), head = nt_block(v, e, x, y + 1, z);
                                if (feet < 0 || head < 0 || feet == 10 || feet == 11 ||
                                    head == 10 || head == 11) continue;
                                int walls = 0, lava = 0;
                                for (int k = 0; k < 4; ++k)
                                {
                                    walls += nt_block(v, e, x + NDX[k], y + 1, z + NDZ[k]) == 87;
                                    for (int h = -1; h <= 3; ++h)
                                    {
                                        int b = nt_block(v, e, x + NDX[k], y + h, z + NDZ[k]);
                                        lava |= b == 10 || b == 11;
                                    }
                                }
                                if (walls < 2 || lava) continue;
                                ++count;
                                if (dist < nd) nd = dist, nx = x, ny = y, nz = z;
                            }
                    fprintf(stderr, "CHAMBER_NETHERRACK env=%d step=%lld radius=%d candidates=%d "
                            "nearest=%d,%d,%d dist=%.2f\n", nt_id(v, e), (long long)e->steps,
                            rr, count, nx, ny, nz, nd);
                }
            }
        }
#endif
        return 0;
    }
    e->chamber_x = bx, e->chamber_y = by, e->chamber_z = bz, e->chamber_on = 1;
#ifdef CHAMBER_TRACE
    fprintf(stderr, "CHAMBER_TARGET env=%d step=%lld cell=%d,%d,%d score=%.1f\n", nt_id(v, e),
            (long long)e->steps, bx, by, bz, best);
#endif
    return 1;
}

/* A safe rock cell on the path counts before the planner's selected target: seed 1's path descended to y58 and then
 * circled below a y62 target, but had already gone through covered netherrack. */
static int nt_chamber_cell_safe(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct nt_env *n = &e->nt;
    int x = (int)floor(r->x), y = (int)floor(r->y + 0.13), z = (int)floor(r->z), walls = 0;
    if (!n->has_sp || !r->on_ground || r->fire ||
        py_dist3(x + 0.5, y, z + 0.5, n->sx + 0.5, n->sy + 0.5, n->sz + 0.5) < NT_CHAMBER_SP_DIST)
        return 0;
    int solid = MOPT(MO_NT_CHAMBER_SOLID);
    int roof2 = nt_block(v, e, x, y + 2, z), roof3 = nt_block(v, e, x, y + 3, z);
    int floor_b = nt_block(v, e, x, y - 1, z);
    /* The three-high tunnel clears y+2; a full roof at y+3 still seals it. */
    if (solid ? ((!nt_chamber_sturdy(roof2) && !nt_chamber_sturdy(roof3)) ||
                 !nt_chamber_sturdy(floor_b))
              : ((roof2 != 87 && roof3 != 87) || floor_b <= 0)) return 0;
    for (int k = 0; k < 4; ++k)
    {
        int side = nt_block(v, e, x + NDX[k], y + 1, z + NDZ[k]);
        walls += solid ? nt_chamber_sturdy(side) : side == 87;
    }
    if (solid && nt_chamber_sp_hidden_after_dig(v, e, x, y, z) != 1) return 0;
    return walls >= 2 && pt_site(v, e, r) >= 0;
}

/* The older home digger's visited columns also qualify once it reaches a safe rock cell. */
static int nt_chamber_ready(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct nt_env *n = &e->nt;
    /* The selected rock site stays ready while portal_room=wide digs its roof and side walls. Requiring those
     * original blocks again after portal step 17 restarts the Nether macro before craft can finish the frame. */
    if (e->chamber_on == 2) return 1;
    return n->dep_done && n->dg_nvis >= NT_CHAMBER_CELLS && nt_chamber_cell_safe(v, e, r);
}

/* The cover can be complete while a chamber's local search failed. A valid portal site at the player's present
 * feet is still a safe way out; the site, materials and health are checked by nt_portal_home before handing off. */
static int nt_chamber_yield(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!MOPT(MO_NT_EXIT_BACK))
        return !MOPT(MO_NT_CHAMBER) || e->chamber_on < 0 || nt_chamber_ready(v, e, r);
    if (!MOPT(MO_NT_CHAMBER) || nt_chamber_ready(v, e, r)) return 1;
    if (e->chamber_on >= 0) return 0;
    const struct nt_env *n = &e->nt;
    return !n->has_sp || nt_exit_plug_ready(v, e, r);
}

#define NT_EXIT_HEAL_HP 16.0f        /* the portal's own health floor */
static int nt_exit_cover(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    if (!n->has_sp) return e->exit_cover_done = 1;
    if (!e->exit_cover_at) e->exit_cover_at = e->steps;
    int x = (int)floor(r->x), y = (int)floor(r->y + 0.13), z = (int)floor(r->z);
    int drop = MOPT(MO_NT_CHAMBER) == 2 ? NT_STRONGHOLD_COVER_DROP : NT_EXIT_COVER_DROP;
    int floor_b = nt_block(v, e, x, y - 1, z), under = nt_block(v, e, x, y - 2, z);
    /* Keep the last solid floor above a cavity or lava; the deeper route can leave from that safe height. */
    int safe_floor = MOPT(MO_NT_CHAMBER) == 2 && y <= n->sy - NT_EXIT_COVER_DROP &&
                     r->on_ground && floor_b > 0 && floor_b != 7 && floor_b != 10 && floor_b != 11 &&
                     (under <= 0 || under == 10 || under == 11);
    if (y > n->sy - drop && !safe_floor)
    {
        still(a);
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = 90.0f;
        a->hold = 1u << PK_SNEAK;
        if (floor_b > 0 && floor_b != 7 && floor_b != 10 && floor_b != 11 &&
            under > 0 && under != 7 && under != 10 && under != 11)
        {
            int pick = best_tool(&v->T, s, 1);
            if (pick >= 0 && pick != s->current) a->hotbar = pick;
            if (pick >= 0 && s->mo_hit && s->mo_x == x && s->mo_y == y - 1 && s->mo_z == z)
                a->hold |= 1u << PK_ATTACK;
        }
        return 0;
    }
    int roof = nt_block(v, e, x, y + 2, z);
    if (nt_open(roof))
    {
        still(a);
        if (!nt_cover_place(v, e, r, s, a, ey, x, y + 2, z, 0, 0)) a->hold = 1u << PK_SNEAK;
        return 0;
    }
    e->exit_cover_done = 1;
#ifdef EXIT_COVER_TRACE
    struct netherview_blaze bz[64];
    int nb = netherview_oracle_blazes(v->p, nt_id(v, e), r->x, ey, r->z, 16.0, bz, 64);
    fprintf(stderr, "EXIT_COVER_DONE env=%d step=%lld elapsed=%lld pos=%.1f,%.1f,%.1f blazes16=%d\n",
            nt_id(v, e), (long long)e->steps, (long long)(e->steps - e->exit_cover_at + 1), r->x, r->y, r->z, nb);
#endif
    return 1;
}

/* 0 while moving, 1 in the selected chamber, -1 when the old home route must take over. */
#define NT_CHAMBER_LEG 72.0
#define NT_CHAMBER_WAY_NEAR 8.0
static int nt_chamber_next_leg(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct shoracle_room rm;
    if (shoracle_locate(v->p, nt_id(v, e), r->x * 8.0, r->z * 8.0, &rm) < 0 || !rm.ok) return -1;
    double gx = (0.5 * (rm.box[0] + rm.box[3]) + 0.5) / 8.0;
    double gz = (0.5 * (rm.box[2] + rm.box[5]) + 0.5) / 8.0;
    double dx = gx - r->x, dz = gz - r->z, d = hypot(dx, dz);
    if (d <= NT_CHAMBER_WAY_NEAR) return 1;
    double f = d < NT_CHAMBER_LEG ? 1.0 : NT_CHAMBER_LEG / d;
    e->chamber_x = (int)floor(r->x + f * dx);
    e->chamber_y = (int)floor(r->y + 0.13);
    e->chamber_z = (int)floor(r->z + f * dz);
    e->chamber_on = 3;
    e->nt.nopath = 0;
    e->nt.opaths_new = 1;
#ifdef CHAMBER_TRACE
    fprintf(stderr, "CHAMBER_LEG env=%d step=%lld from=%.1f,%.1f,%.1f to=%d,%d,%d goal=%.1f,%.1f distance=%.1f\n",
            nt_id(v, e), (long long)e->steps, r->x, r->y, r->z, e->chamber_x, e->chamber_y,
            e->chamber_z, gx, gz, d);
#endif
    return 0;
}

static int nt_chamber_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
#ifdef CHAMBER_PROBE
    if (nt_id(v, e) == 0 && e->steps == 11246)
    {
        int old_on = e->chamber_on, old_x = e->chamber_x, old_y = e->chamber_y, old_z = e->chamber_z;
        fprintf(stderr, "CHAMBER_STATE step=%lld world=%lld rods7_at=%lld cover_at=%lld cover_done=%d "
                "state=%d player=%.2f,%.2f,%.2f sp=%d,%d,%d\n", (long long)e->steps,
                (long long)r->wtime, (long long)e->nt.t_rods7, (long long)e->exit_cover_at,
                e->exit_cover_done, old_on, r->x, r->y, r->z, e->nt.sx, e->nt.sy, e->nt.sz);
        e->chamber_on = 0;
        int ok = nt_chamber_pick(v, e, r, e->nt.sx, e->nt.sy, e->nt.sz, NT_CHAMBER_R);
        fprintf(stderr, "CHAMBER_PROBE step=%lld ok=%d target=%d,%d,%d\n", (long long)e->steps,
                ok, e->chamber_x, e->chamber_y, e->chamber_z);
        e->chamber_on = 0;
        ok = nt_chamber_pick(v, e, r, e->nt.sx, e->nt.sy, e->nt.sz, 16);
        fprintf(stderr, "CHAMBER_PROBE_WIDE step=%lld ok=%d target=%d,%d,%d\n", (long long)e->steps,
                ok, e->chamber_x, e->chamber_y, e->chamber_z);
        e->chamber_on = old_on, e->chamber_x = old_x, e->chamber_y = old_y, e->chamber_z = old_z;
    }
#endif
    if (e->chamber_on == 2) return 1;
    if (MOPT(MO_NT_CHAMBER) == 2)
    {
        if (e->chamber_on == 0)
        {
            int rc = nt_chamber_next_leg(v, e, r);
            if (rc < 0) e->chamber_on = -1;
            else if (rc > 0) e->chamber_on = 4;
        }
        if (e->chamber_on == 3)
        {
            if (r->on_ground && !r->fire &&
                py_hypot2(e->chamber_x + 0.5 - r->x, e->chamber_z + 0.5 - r->z) <= NT_CHAMBER_WAY_NEAR)
            {
                int rc = nt_chamber_next_leg(v, e, r);
                if (rc < 0) e->chamber_on = -1;
                else if (rc > 0) e->chamber_on = 4;
                return 0;
            }
            int w = nt_opath_walk(v, e, r, s, a, ey, e->chamber_x, e->chamber_y, e->chamber_z, 0.6, 2);
            if (w == 0) return 0;
            if (w < 0) { e->chamber_on = -1; return -1; }
            if (!r->on_ground) { still(a); return 0; }
            int rc = nt_chamber_next_leg(v, e, r);
            if (rc < 0) e->chamber_on = -1;
            else if (rc > 0) e->chamber_on = 4;
            return 0;
        }
        if (e->chamber_on == 4)
        {
            if (nt_chamber_cell_safe(v, e, r)) e->chamber_on = 2;
            else if (!nt_chamber_pick(v, e, r, (int)floor(r->x), (int)floor(r->y + 0.13) + 8,
                                      (int)floor(r->z), 4)) e->chamber_on = -1;
        }
    }
    if (e->chamber_on == 0 && !nt_chamber_pick(v, e, r, e->nt.sx, e->nt.sy, e->nt.sz, NT_CHAMBER_R))
        e->chamber_on = -1;
    if (e->chamber_on == 2) return 1;
    if (e->chamber_on != 1) return -1;
    if (nt_chamber_cell_safe(v, e, r))
    {
        e->chamber_on = 2;
#ifdef CHAMBER_TRACE
        fprintf(stderr, "CHAMBER_AT env=%d step=%lld pos=%.1f,%.1f,%.1f ready=1 before_target=1\n",
                nt_id(v, e), (long long)e->steps, r->x, r->y, r->z);
#endif
        still(a);
        return 1;
    }
    int w = nt_opath_walk(v, e, r, s, a, ey, e->chamber_x, e->chamber_y, e->chamber_z, 0.6, 3);
#ifdef CHAMBER_TRACE
    if (e->steps % 100 == 0 || w < 0)
        fprintf(stderr, "CHAMBER_WALK env=%d step=%lld w=%d pos=%.2f,%.2f,%.2f path=%d next=%d off=%d\n",
                nt_id(v, e), (long long)e->steps, w, r->x, r->y, r->z, e->nt.nopath, e->nt.opath_k,
                e->nt.opath_off);
#endif
    if (w == 0) return 0;
    if (w < 0) { e->chamber_on = -1; return -1; }
    /* The path walker can report arrival on the first falling tick into the chamber.
     * Wait for the floor before checking the build site; the player falls without input. */
    if (!r->on_ground)
    {
        still(a);
        return 0;
    }
    e->chamber_on = 2;
    if (!nt_chamber_ready(v, e, r)) e->chamber_on = -1;
#ifdef CHAMBER_TRACE
    fprintf(stderr, "CHAMBER_AT env=%d step=%lld pos=%.1f,%.1f,%.1f ready=%d\n", nt_id(v, e),
            (long long)e->steps, r->x, r->y, r->z, e->chamber_on == 2);
#endif
    still(a);
    return e->chamber_on == 2 ? 1 : -1;
}

static int nt_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    /* lane/chain (nether_portal_home): the way home carried, the portal macro's to build (common.c) */
    if (!nt_want(v, e, r) || (!nt_sp_break_pending(v, e, s) &&
                             (!MOPT(MO_NT_EXIT_COVER) || e->exit_cover_done) &&
                             nt_chamber_yield(v, e, r) && nt_portal_home(v, e, r, s)))
        return 0;
    if (MOPT(MO_OB_CARRY) >= 2 && ob_frame_ready(v, e, r, s)) return 0;
    e->kind = M_MINE;
    e->nt.on = 1;
    e->nt.wk = -1;
    return 1;
}

static int nt_drive1(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey);

/* lane/rod (nether_drops): a blaze's rod lies where it died, and only a kill the player is credited with drops one (the
 * stats screen's count rises). Each blaze struck (the attack pressed with it under the crosshair) is kept with its last
 * place in view; when the count rises, the struck one last hit that is gone from view died: its place joins the drops
 * to walk to (nt_drive1's pick, before any new fight within NT_DROP_NEAR). Before, the walk went to wherever the lock
 * was lost (killed or flown off), once, and the next fight dropped it: gold-g8 with no rods picked up 62 rods of 157
 * credited kills (0.39; vanilla drops 0.5 a kill) */
#define NT_DROP_NEAR 6.0        /* blocks: a drop this near is walked to before a new fight */
static void nt_drops_track(struct vec_env *e, const struct neth_rl_state *s, const struct neth_rl_act *a)
{
    struct nt_env *n = &e->nt;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; n->sid[k] && i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].id == n->sid[k])
                n->spos[k][0] = s->vis[i].x, n->spos[k][1] = s->vis[i].y, n->spos[k][2] = s->vis[i].z;
    if (a->press[PK_ATTACK] && s->mo_entity && s->mo_ekind == HK_BLAZE)
    {
        int k = -1, old = 0;
        for (int j = 0; j < 4 && k < 0; ++j)
            if (n->sid[j] == s->mo_entity) k = j;
        for (int j = 1; j < 4 && k < 0; ++j)
            if (n->sat[j] < n->sat[old]) old = j;
        if (k < 0)
        {
            k = old, n->sid[k] = s->mo_entity;
            n->spos[k][0] = (float)e->last.x, n->spos[k][1] = (float)e->last.y, n->spos[k][2] = (float)e->last.z;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
                if (s->vis[i].id == s->mo_entity)
                    n->spos[k][0] = s->vis[i].x, n->spos[k][1] = s->vis[i].y, n->spos[k][2] = s->vis[i].z;
        }
        n->sat[k] = e->steps;
    }
    for (; n->bstat_seen < n->bstat; ++n->bstat_seen)
    {
        int k = -1;
        for (int j = 0; j < 4; ++j)
        {
            int in = 0;
            for (int i = 0; n->sid[j] && i < s->nvis && i < NETH_RL_VIS; ++i) in |= s->vis[i].id == n->sid[j];
            if (n->sid[j] && !in && (k < 0 || n->sat[j] > n->sat[k])) k = j;
        }
        if (k < 0) continue;
        int d = n->ndrop++ & 7;
        for (int c = 0; c < 3; ++c) n->drop[d][c] = n->spos[k][c];
        n->sid[k] = 0, n->sat[k] = 0;
    }
}
/* the nearest drop not walked to (its ring index), -1 none */
static int nt_drop_near(const struct nt_env *n, const struct vec_last *r, double *dist)
{
    int best = -1;
    double bd = 1e9;
    for (int i = n->drops; i < n->ndrop; ++i)
    {
        const float *p = n->drop[i & 7];
        double d = py_hypot2(p[0] - r->x, p[2] - r->z);
        if (d < bd) bd = d, best = i;
    }
    *dist = bd;
    return best;
}

/* lane/rods (nether_pick_up): a drop lying out of the crouched walk's reach. A blaze dies in the air and its rod falls
 * to the first floor under it: the rod hunt's eight library starts camped on the room's floor beside the spawner's
 * platform, and 791 of 1,074 rods lay 2 to 4 blocks over the feet, where the forward walk pressed into the platform's
 * wall for its 60 steps. The floor under the drop's place (the client world's blocks, from the place down at most
 * NT_PICK_UP_DOWN): its cell 2 or more over the feet, the walker toward it (it digs no fortress brick at the camp: the
 * walks rose 1.5 blocks in 2 of 530, and ended beside the platform with a rod in 126), at most NT_PICK_UP_AGE steps.
 * 1 the step taken, 0 not a drop above (the forward walk), -1 the walk over */
/* lane/rods2 (nether_deck_wait): the walk to a drop's next step (the feet 0.6 toward the drop) within NT_DECK_NEAR
 * cells of a deck side, a cell at the feet's level whose NT_DECK_DROP cells under it are open (the client world's
 * blocks), while a client-known blaze has a clear ray to the eye (netherview_exit_blaze_los, the exit cover's test):
 * 1 the step waits. s1c's deck stands about 30 over the lava sea: 26 of wafmc's 64 deaths there were a fireball's push
 * over its side, 20 in the pickup. A rod lies 6000 ticks: the wait is held to NT_DECK_WAIT steps a drop */
#define NT_DECK_NEAR 2
#define NT_DECK_DROP 4
#define NT_DECK_WAIT 1200
#define NT_DECK_FAR 3
static int nt_deck_edge(const struct neth_vec *v, const struct vec_env *e, double x, double y, double z)
{
    int fx = (int)floor(x), fy = (int)floor(y + 0.13), fz = (int)floor(z);
    for (int dx = -NT_DECK_NEAR; dx <= NT_DECK_NEAR; ++dx)
        for (int dz = -NT_DECK_NEAR; dz <= NT_DECK_NEAR; ++dz)
        {
            int open = 1;
            for (int k = 1; k <= NT_DECK_DROP && open; ++k) open = nt_open(nt_block(v, e, fx + dx, fy - k, fz + dz));
            if (open) return 1;
        }
    return 0;
}
/* the Chebyshev distance from cell (x, z) at the feet's level fy to the nearest deck side within 3 (4 when none) */
static int nt_deck_far(const struct neth_vec *v, const struct vec_env *e, int x, int fy, int z)
{
    int best = NT_DECK_FAR + 1;
    for (int dx = -NT_DECK_FAR; dx <= NT_DECK_FAR; ++dx)
        for (int dz = -NT_DECK_FAR; dz <= NT_DECK_FAR; ++dz)
        {
            int d = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            if (d >= best) continue;
            int open = 1;
            for (int k = 1; k <= NT_DECK_DROP && open; ++k) open = nt_open(nt_block(v, e, x + dx, fy - k, z + dz));
            if (open) best = d;
        }
    return best;
}
static int nt_deck_wait(const struct neth_vec *v, struct vec_env *e, struct nt_env *n, const struct vec_last *r,
                        struct neth_rl_act *a, double ey)
{
    if (!MOPT(MO_NT_DECK_WAIT) || !r->on_ground) return 0;
    double dx = n->rx - r->x, dz = n->rz - r->z, d = sqrt(dx * dx + dz * dz);
    if (d < 0.3) return 0;
    if (!nt_deck_edge(v, e, r->x + 0.6 * dx / d, r->y, r->z + 0.6 * dz / d)) return 0;
    if (netherview_exit_blaze_los(v->p, nt_id(v, e), r->x, ey, r->z) <= 0) return 0;
    if (++n->deck_wait > NT_DECK_WAIT)
    {
        if (n->deck_wait == NT_DECK_WAIT + 1) n->deck_gaveup += 1;
        return 0;
    }
    n->deck_waits += 1;
    still(a);
    a->hold = 1u << PK_SNEAK;
    if (MOPT(MO_NT_DECK_WAIT) == 2)
    {
        /* middle: the cell of the nine round the feet (standable: solid under, the feet's two cells open) farthest
         * from a deck side (nt_deck_far), walked to crouched when it is farther than the feet's own */
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
        int best = nt_deck_far(v, e, fx, fy, fz), bx = fx, bz = fz;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
            {
                if (!dx && !dz) continue;
                int x = fx + dx, z = fz + dz;
                if (nt_open(nt_block(v, e, x, fy - 1, z)) || !nt_open(nt_block(v, e, x, fy, z)) ||
                    !nt_open(nt_block(v, e, x, fy + 1, z)))
                    continue;
                int f = nt_deck_far(v, e, x, fy, z);
                if (f > best) best = f, bx = x, bz = z;
            }
        double tx = bx + 0.5 - r->x, tz = bz + 0.5 - r->z;
        if (tx * tx + tz * tz > 0.04)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, bx + 0.5, r->y, bz + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 30.0f;
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
        }
    }
    return 1;
}
#define NT_PICK_UP MOPT(MO_NT_PICK_UP)
#define NT_PICK_UP_AGE 300
#define NT_PICK_UP_DOWN 12
static int nt_pick_up(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    int px = (int)floor(n->rx), py = (int)floor(n->ry), pz = (int)floor(n->rz);
    for (int k = 0; k < NT_PICK_UP_DOWN && nt_open(nt_block(v, e, px, py - 1, pz)); ++k) --py;
    if (py < fy + 2) return 0;
    if (++n->pick_age > NT_PICK_UP_AGE) return -1;
    /* nether_pick_up=dig (lane/rods, the yield: half the drops lie on the spawner room's floor 3 over a camp under it):
     * a drop resting 2 to 4 over the feet on a block whose column down to the feet's level is open, with a floor under
     * that level: walk under it on the feet's level and break the block it rests on, so it falls to the feet */
    if (NT_PICK_UP == 2 && py - fy <= 4)
    {
        int col = nt_floor_ok(nt_block(v, e, px, fy - 1, pz)), sb = nt_block(v, e, px, py - 1, pz);
        for (int y = fy; y <= py - 2 && col; ++y) col = nt_open(nt_block(v, e, px, y, pz));
        /* or the drop 3 over the feet in a cell beside the feet's column (the camp's walls under it): broken from where
         * the feet stand, it falls one block, to 2 over the feet, in the pickup's reach */
        int beside = !col && py - fy == 3 && abs(px - fx) <= 1 && abs(pz - fz) <= 1 &&
                     !nt_open(nt_block(v, e, px, py - 2, pz)) && !(n->has_sp && px == n->sx && py - 1 == n->sy - 1 && pz == n->sz);
        if ((col || beside) && sb > 0 && !nt_open(sb) && !nt_lava(sb) && sb != 7 && sb != 52 && sb != 90 &&
            !nt_lava_by(v, e, px, py - 1, pz, fx, fy, fz))
        {
            if (!beside && (fx != px || fz != pz))
            {
                int w = nt_walk(v, e, r, s, a, ey, px, fy, pz, 0.3, -1);
                return w < 0 ? -1 : 1;
            }
            double yaw, pitch;
            look_at(r->x, ey, r->z, px + 0.5, py - 0.5, pz + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            if (s->mo_hit && s->mo_x == px && s->mo_y == py - 1 && s->mo_z == pz)
            {
                a->hold = 1u << PK_ATTACK;
                int best = nt_tool(v, s, sb);
                if (best >= 0 && best != s->current) a->hotbar = best;
            }
            return 1;
        }
    }
    if (fx == px && fz == pz && fy >= py) return -1;
    int w = nt_walk(v, e, r, s, a, ey, px, py, pz, 0.4, -1);
    if (w < 0 || (w > 0 && fy >= py)) return -1;
    if (w > 0)
    {
        /* beside it and under it: the last step up (forward, a jump) */
        double yaw, pitch;
        look_at(r->x, ey, r->z, px + 0.5, py + 0.5, pz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = (1u << PK_FORWARD) | (1u << PK_JUMP);
    }
    return 1;
}

/* A blaze rod cannot enter a full pack. Drop junk during the sheltered tunnel toward a fortress, before the spawner
 * fight; three free slots keep mined netherrack from immediately blocking the next rod. Off by default. */
#define NT_TRIM_FREE 3
#define NT_TRIM_MAX 3
#define NT_TRIM_WAIT 40
#define NT_TRIM_RETRY 80
#define NT_TRIM_KEEP_RACK 64
#define NT_TRIM_KEEP_COBBLE 128
static int nt_trim(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int preportal)
{
    if (!MOPT(MO_NT_TRIM)) return 0;
    struct nt_env *n = &e->nt;
    int64_t t = e->steps;
    if (n->trim_at > 0)
    {
        if (r->screen == PS_INVENTORY)
        {
            still(a);
            for (int k = 0; k < n->trim_n; ++k)
            {
                a->ops[2 * k] = click(inv_slot(n->trim_slots[k], 0), 0, 0);
                a->ops[2 * k + 1] = click(-999, 0, 0);
            }
            a->ops[2 * n->trim_n] = CLOSE;
            a->nops = 2 * n->trim_n + 1;
            n->trim_at = 0, n->trim_off = t + NT_TRIM_RETRY;
            return 1;
        }
        if (r->screen != PS_NONE || t - n->trim_at > NT_TRIM_WAIT)
        {
            n->trim_at = 0, n->trim_off = t + NT_TRIM_RETRY;
            return 0;
        }
        still(a);
        a->look_mode = 1;
        a->look[0] = n->trim_yaw, a->look[1] = 0.0f;
        if (fabs(remainder((double)r->yaw - n->trim_yaw, 360.0)) < 5.0 && fabs(r->pitch) < 5.0)
            a->press[PK_INVENTORY] = 1;
        return 1;
    }
    int sealed_sp = MOPT(MO_NT_SP_TUN) && n->has_sp && n->opath_tun && r->y <= n->sy - NT_OR_UNDER - 2;
    if (r->screen != PS_NONE || !r->on_ground || r->health < 16.0f || r->fire || t < n->trim_off ||
        (preportal ? r->dim != 0 : r->dim != -1 || ((!n->has_loc && !n->has_fort) ||
                                                    ((n->fort_at || n->has_sp) && !sealed_sp) || n->hood != 0 ||
                                                    n->heal_k >= 0 || n->refill_at > 0 ||
                                                    inv_count(s, BLAZE_ROD) >= NT_RODS || nt_lava_near(v, e, r) >= 0)))
        return 0;
    int free = 0;
    for (int i = 0; i < 36; ++i) free += s->inv[i].item < 0 || s->inv[i].count <= 0;
    int64_t rack = inv_count(s, 87) - NT_TRIM_KEEP_RACK;
    if (free >= NT_TRIM_FREE && (preportal || rack <= 0)) return 0;
    int want = NT_TRIM_FREE - free;
    if (!preportal && rack > 0 && want < 1) want = 1;
    if (want > NT_TRIM_MAX) want = NT_TRIM_MAX;
    n->trim_n = 0;
    int64_t cobble = inv_count(s, COBBLE) - NT_TRIM_KEEP_COBBLE;
    for (int pass = 0; pass < 3 && n->trim_n < want; ++pass)
        for (int j = 0; j < 36 && n->trim_n < want; ++j)
        {
            int i = (j + 9) % 36, it = s->inv[i].item;
            if (s->inv[i].count <= 0) continue;
            if (pass == 0 && (it != 87 || s->inv[i].count > rack)) continue;
            if (pass == 1 && it != 3 && it != 13 && it != 6) continue;
            if (pass == 2 && (it != COBBLE || s->inv[i].count > cobble)) continue;
            n->trim_slots[n->trim_n++] = i;
            if (pass == 0) rack -= s->inv[i].count;
            if (pass == 2) cobble -= s->inv[i].count;
        }
    if (n->trim_n == 0) return 0;
    n->trim_at = t, n->trim_yaw = (float)remainder((double)r->yaw + 180.0, 360.0);
    still(a);
    a->look_mode = 1;
    a->look[0] = n->trim_yaw, a->look[1] = 0.0f;
    return 1;
}

/* the macro's drive: 1 when over */
static int nt_drive(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey)
{
    int rc = nt_drive1(v, e, r, s, a, ey);
    if (MOPT(MO_NT_DROPS)) nt_drops_track(e, s, a);  /* lane/rod (nether_drops) */
    if (rc == 0 && a->press[PK_ATTACK] && s->mo_entity && s->mo_ekind == HK_BLAZE)
    {
        e->nt.bhits += 1;                           /* lane/rod: the books */
        if (e->nt.t_bhit < 0) e->nt.t_bhit = e->steps;
    }
    if (NT_FAN && rc == 0 && r->on_ground && !(a->hold & (1u << PK_JUMP)))
    {
        /* lane/nether3: on the ground with air or lava under the middle (a brink, a bridge's end): crouched whatever
         * the step asks (a look round's still released the crouch at a brink and the player slid off) */
        int ub = nt_block(v, e, (int)floor(r->x), (int)floor(r->y + 0.13) - 1, (int)floor(r->z));
        /* lane/rod (nether_oracle): not on a drop the oracle's path takes (its landing checked on the server) */
        if (ub >= 0 && (nt_open(ub) || nt_lava(ub)) && e->nt.or_drop_at != e->steps) a->hold |= 1u << PK_SNEAK;
    }
    if (a->look_mode == 1 && !e->fid)
    {
        /* lane/nether2: the yaw asked within (-180, 180] of the yaw now: look_at's answer for a point straight behind
         * flips between d and d - 360 from step to step, and the action compiler's turn toward each (its raw
         * difference) undid the last, the look never turning (a walker held 8000 steps at one cell) */
        double dy = remainder((double)a->look[0] - (double)r->yaw, 360.0);
        if (dy <= -179.5) dy = 180.0;
        a->look[0] = (float)((double)r->yaw + dy);
    }
    {
        /* lane/rods (books only): the step as neth_vec_nether reports it to --nether-trace */
        struct nt_env *n = &e->nt;
        n->bk_rc = rc, n->bk_hold = (int)a->hold, n->bk_press = 0, n->bk_bn = 0, n->bk_bd = -1.0f, n->bk_t = e->steps;
        n->bk_doing = (n->pick != 0) | (n->resting != 0) << 1 | (n->heal_k >= 0) << 2 | (n->hood > 0) << 3 |
                      (n->shy != 0) << 4 | (n->back > 0) << 5 | (n->spin > 0 || n->clook > 0) << 6 |
                      (n->or_lock_last == e->steps) << 7 | (n->tower != 0) << 8;
        for (int k = 0; k < NETH_RL_KEYS && k < 31; ++k) n->bk_press |= (a->press[k] != 0) << k;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].kind == HK_BLAZE)
            {
                double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
                n->bk_bn += 1;
                if (n->bk_bd < 0 || d < n->bk_bd) n->bk_bd = (float)d;
            }
    }
#ifdef NT_TRACE
    if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
    {
        struct nt_env *n = &e->nt;
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
        fprintf(stderr, "nt %lld rc %d pos %.2f %.2f %.2f yaw %.0f pit %.0f hp %.1f fire %d ph %d wk %d m %d d %d c %d %d %d "
                "tried %x hold %x look %.0f %.0f use %d atk %d | col", (long long)e->steps, rc, r->x, r->y, r->z, r->yaw,
                r->pitch, r->health, r->fire, n->phase, n->wk, n->wmode, n->wdir, n->wcx, n->wcy, n->wcz, n->wtried,
                a->hold, a->look[0], a->look[1], a->press[PK_USE], a->press[PK_ATTACK]);
        for (int dy = -1; dy <= 2; ++dy) fprintf(stderr, " %d", nt_block(v, e, fx, fy + dy, fz));
        fprintf(stderr, " | next");
        for (int dy = -1; dy <= 3; ++dy) fprintf(stderr, " %d", nt_block(v, e, n->wcx, fy + dy, n->wcz));
        double bd = -1;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].kind == HK_BLAZE)
            {
                double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
                if (bd < 0 || d < bd) bd = d;
            }
        fprintf(stderr, " | blaze %.1f nvis %d fid %d sp %d %d %d %d", bd, s->nvis, e->fid, n->has_sp, n->sx, n->sy, n->sz);
        fprintf(stderr, " | ask %d cur %d hotbar", a->hotbar, s->current);  /* lane/nether4: the hotbar's items */
        for (int i = 0; i < 9; ++i) fprintf(stderr, " %d", s->inv[i].item);
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS && i < 4; ++i)       /* lane/nether4: the livings in view */
            fprintf(stderr, " v%d:%.1f", s->vis[i].kind,
                    py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z));
        fprintf(stderr, " | goal %d %d %d %d at %lld spin %d none %d bear %.0f fort %d off %lld | at %d top %d %d %d %d "
                "clook %d looks %d climbed %d hb %d pil %.2f %d cf %d coff %lld fill %lld\n", n->has_goal, n->gx,
                n->gy, n->gz, (long long)n->goal_at, n->spin, n->spin_none, n->bearing, n->has_fort, (long long)n->off,
                n->fort_at, n->has_top, n->tx, n->ty, n->tz, n->clook, n->fort_looks, n->climbed, nt_hotbar_fill(s),
                n->pil_base, n->pil_age, n->climb_fails, (long long)n->climb_off, (long long)(inv_count(s, COBBLE) + inv_count(s, 87) + inv_count(s, 112)));
        /* lane/rod: the fight's and the heal's state */
        fprintf(stderr, "nf %lld fid %d foff %lld shy %d shyoff %lld hood %d heal %d rest %d sp %d camp %d crumbs %d "
                "pick %d kind %d\n", (long long)e->steps, e->fid, (long long)n->fight_off, n->shy,
                (long long)n->shy_off, n->hood, n->heal_k, n->resting, n->has_sp, n->has_camp, n->ncrumb, n->pick,
                e->kind);
    }
#endif
    return rc;
}

static int nt_drive1(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
#ifdef NT_TRACE_FIRE
    if (nt_id(v, e) == NT_TRACE_FIRE && r->dim == -1 && r->x > -125 && r->x < -95 && r->z > 100 && r->z < 135 &&
        r->y > 20 && r->y < 85)
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
        fprintf(stderr, "firectx step=%lld time=%lld pos=%.2f,%.2f,%.2f fire=%d grid=%d,%d,%d:",
                (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z, r->fire, fx, fy, fz);
        for (int dy = -1; dy <= 2; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    fprintf(stderr, "%s%d", dy == -1 && dz == -1 && dx == -1 ? "" : ",",
                            netherview_oracle_block(v->p, nt_id(v, e), fx + dx, fy + dy, fz + dz));
        struct netherview_fire_ent near[32];
        int nn = netherview_fire_near(v->p, nt_id(v, e), r->x, r->y, r->z, near, 32);
        fprintf(stderr, " near=%d", nn);
        for (int j = 0; j < nn; ++j)
            fprintf(stderr, " %d:%.1f,%.1f,%.1f", near[j].kind, near[j].x, near[j].y, near[j].z);
        fprintf(stderr, " mo=%d,%d,%d:%d\n", s->mo_x, s->mo_y, s->mo_z, s->mo_block);
    }
#endif
    still(a);
    if (MOPT(MO_NT_EXIT_COVER) && r->dim == -1 && inv_count(s, BLAZE_ROD) >= 7)
    {
        n->phase = NT_HOME;
        if (!e->exit_cover_done && !nt_exit_cover(v, e, r, s, a, ey)) return 0;
        if (eat_want(v, r, s)) return 1;
        if (r->health < NT_EXIT_HEAL_HP)
        {
            a->hold = 1u << PK_SNEAK;
            return 0;
        }
        if (MOPT(MO_NT_CHAMBER))
        {
            int w = nt_chamber_walk(v, e, r, s, a, ey);
            if (w == 0) return 0;
        }
        if (nt_exit_crumb_dig(v, e, r, s, a, ey)) return 0;
        if (nt_chamber_yield(v, e, r) && nt_portal_home(v, e, r, s)) return 1;
        if (n->has_portal)
        {
            nt_home(v, e, r, s, a, ey);
            return 0;
        }
        a->hold = 1u << PK_SNEAK;
        return 0;
    }
    if (n->trim_at > 0 && nt_trim(v, e, r, s, a, 0)) return 0;
    if (!nt_want(v, e, r) || eat_want(v, r, s) ||
        (!nt_sp_break_pending(v, e, s) && (!MOPT(MO_NT_CHAMBER) || nt_chamber_ready(v, e, r)) &&
         (!MOPT(MO_NT_EXIT_COVER) || e->exit_cover_done) && nt_portal_home(v, e, r, s)) ||
        (MOPT(MO_OB_CARRY) >= 2 && ob_frame_ready(v, e, r, s)))
        return 1;
    int64_t t = e->steps;
    /* lane/rod (nether_off_hold): the off steps (no way on found) held by the macro, still, crouched, a slow look round
     * (the fans and scans see new floors), never the policy's: from the harvested starts' portal over the lava sea the
     * policy's turns and the mine lock its attack began drove 761 of env 103's first 1,200 steps there, a hit took 6
     * and it slipped off into the lava; lava beside the player is still sealed first */
    if (r->screen == PS_NONE && nt_feet_fill(v, e, r, s, a, ey)) return 0;   /* lane/rod (nether_feet_fill) */
    if (r->on_ground && nt_portal_exit(v, e, r, a, ey)) return 0;           /* lane/rod (nether_portal_exit) */
    if (MOPT(MO_NT_OFF_HOLD) && t < n->off && !(n->aseal < 2 * NT_PLACE_TRIES && nt_lava_near(v, e, r) >= 0))
    {
        a->look_mode = 1;
        a->look[0] = r->yaw + 9.0f, a->look[1] = 20.0f;
        a->hold = 1u << PK_SNEAK;
        n->wk = -1;
        return 0;
    }
    if (NT_FAN && (n->refill_at > 0 || r->screen == PS_INVENTORY))
        return nt_refill(v, e, r, s, a) ? 0 : 1;     /* lane/nether3: the fill's swap under the inventory screen */
    if (nt_trim(v, e, r, s, a, 0)) return 0;
    /* lava come in beside the player (a pocket dug open, a flow): sealed before anything else (twice the tries a
     * cell; the lava escape takes over once it burns) */
    if (n->back > 0)
    {
        /* backing off from lava at head height or over the head (it falls into the player's own cells next) */
        n->back -= 1;
        a->look_mode = 1;
        a->look[0] = n->back_yaw, a->look[1] = 0.0f;
        a->hold = 1u << PK_BACK;
        n->wk = -1;
        return 0;
    }
    int lk = n->aseal < 2 * NT_PLACE_TRIES ? nt_lava_near(v, e, r) : -1;
    if (lk >= 0 && AY[lk] >= 1 && n->backs < NT_BACKS)
    {
        /* the way it came is behind the look: the back key, the look held toward the lava */
        double yaw, pitch;
        look_at(r->x, ey, r->z, (int)floor(r->x) + AX[lk] + 0.5, ey, (int)floor(r->z) + AZ[lk] + 0.5, &yaw, &pitch);
        n->back_yaw = AX[lk] == 0 && AZ[lk] == 0 ? r->yaw : (float)yaw;
        n->back = NT_BACK, n->backs += 1;
        n->wtried |= 1 << n->wdir;
        a->look_mode = 1;
        a->look[0] = n->back_yaw, a->look[1] = 0.0f;
        a->hold = 1u << PK_BACK;
        return 0;
    }
    if (lk >= 0)
    {
        int lx = (int)floor(r->x) + AX[lk], ly = (int)floor(r->y + 0.13) + AY[lk], lz = (int)floor(r->z) + AZ[lk];
        n->aseal += 1;
        n->wk = -1;
        a->look_mode = 1;
        if (nt_seal(v, e, r, s, a, ey, lx, ly, lz)) return 0;
    }
    /* lane/nether4: the unstick: searching (not at a spawner or camp, not healing or going home) with no NT_STUCK_D of
     * progress in NT_STUCK_STEPS, the goal and the walker's cell dropped and the policy's turn for NT_OFF steps (14 of 32
     * envs stood on one deck from step 7,377 to the end) */
    if (NT_STUCK_STEPS > 0 && r->on_ground)
    {
        if (py_hypot2(r->x - n->prog_x, r->z - n->prog_z) > NT_STUCK_D || fabs(r->y - n->prog_y) > NT_STUCK_D)
            n->prog_x = r->x, n->prog_y = r->y, n->prog_z = r->z, n->prog_at = t;
        else if (t - n->prog_at > NT_STUCK_STEPS && n->fort_at && !n->has_sp && !n->has_camp && n->hood == 0 &&
                 inv_count(s, BLAZE_ROD) < NT_RODS)
        {
            /* on the fortress only, and a look round (handed to the random policy for NT_OFF steps, and anywhere, it
             * walked into lava: fortress 32 to 12 of 32, deaths 14 to 28) */
            n->prog_at = t, n->unsticks += 1;
            n->has_goal = 0, n->has_cand = 0, n->wtried = 0, n->wk = -1;
            nt_leg_next(n, r, t, NT_SWEEP_BACK && !n->fort_at);
            still(a);
            a->look_mode = 1;
            a->look[0] = r->yaw, a->look[1] = r->pitch;
            return 0;
        }
    }
    /* lane/nether4: a ghast's fireball coming: hit back first (a fight's lock waits a step) */
    struct netherview_threats th;
    if (netherview_threats(v->p, nt_id(v, e), r->x, ey, r->z, r->yaw, r->pitch, &th) != 0) memset(&th, 0, sizeof th);
    if (nt_deflect(v, e, r, s, a, ey, &th)) return 0;
    if (nt_arrival_cover(v, e, r, s, a, ey)) return 0;
    /* lane/rod (nether_oracle_lock): a skeleton within NT_OR_SKEL_R of the eye on the oracle's way struck, looked at,
     * crouched, in place of the tower (or3k/or3l h22: the tower's pillar beside a wither skeleton, knocked off it, 20
     * to 7 in two hits, dead at 6,001) */
    if (MOPT(MO_NT_ORACLE_LOCK) && NT_ORACLE >= 2 && n->has_sp && n->hood == 0)
    {
        int ki = -1;
        double kd = NT_OR_SKEL_R;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        {
            if (s->vis[i].kind != HK_SKELETON) continue;
            double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
            if (d < kd) kd = d, ki = i;
        }
        if (ki >= 0)
        {
            double yaw, pitch;
            look_at(r->x, ey, r->z, s->vis[ki].x, s->vis[ki].y + 0.5 * s->vis[ki].h, s->vis[ki].z, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            a->hold = 1u << PK_SNEAK;
            int wb = best_weapon(s);
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            if (s->mo_entity && s->mo_ekind == HK_SKELETON && e->steps - n->or_hit_at >= 10)
                a->press[PK_ATTACK] = 1, n->or_hit_at = e->steps;
            n->or_skel_steps += 1;
            return 0;
        }
    }
    /* lane/nether5: a wither skeleton near: the tower (before the hood, the cover and the shy: backing off along the
     * corridor it followed and struck) */
    if (!(MOPT(MO_NT_ORACLE_LOCK) && NT_ORACLE >= 2 && n->has_sp) && nt_tower(v, e, r, s, a, ey)) return 0;
    /* lane/nether4: a blaze in view within NT_NEAR_D: fought, never turned from (the hood's centring and the glance
     * stood crouched and still with blazes 2 blocks off, their melee 6 a hit: 24 of 32 gold-g7 envs at the spawner) */
    double nearb = 1e9;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        if (s->vis[i].kind == HK_BLAZE)
        {
            double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
            if (d < nearb) nearb = d;
        }
    int close = NT_NEAR_D > 0 && nearb < NT_NEAR_D;
    if (close && n->hood == 1) n->hood = 0;
    /* lane/nether4: hurt at the blazes: the hood, healed inside it (the retreat along the way burned in their sight) */
    /* lane/nether5: with nether_hood_any, on the fortress before any spawner too (gold-g7: hit to 5 on the bridge, the
     * rest crouched in the open 300 steps, then a fireball) */
    if (NT_HOOD && !close && e->steps >= n->hood_off && (n->has_sp || n->has_camp || (NT_HOOD_ANY && n->fort_at)) &&
        (n->hood != 0 || !nt_noregen(v, r, s)) &&   /* lane/nleg (nether_hood_starve): no hood with no heal */
        (n->hood != 0 ||
         r->health <= (MOPT(MO_NT_HOOD_HP) && (n->has_sp || n->has_camp) ? NT_HOOD_HP_HI : NT_LOW_HP) ||
         (MOPT(MO_NT_PORTAL_HOME) && r->health < NT_PH_HP)) &&
        /* lane/chain (nether_portal_home): with the rods too, healed before the way home is built (common.c) */
        (inv_count(s, BLAZE_ROD) < NT_RODS || MOPT(MO_NT_PORTAL_HOME)) && nt_hood(v, e, r, s, a, ey))
        return 0;
    /* lane/nether4: a blaze in view out of reach: walled off (it comes round to within reach) */
    /* lane/nether5: without nether_cover_search, only once a spawner or camp is known (gold-g7: searching, the cover
     * filled the bridge's one-wide walk at head height against blazes across the fortress, 73 blocks an env, and the
     * walker dug them out again; the shy below backs off instead) */
    /* lane/rod (nether_oracle spawner): no cover on the oracle's way to the spawner (tr6: a blaze 15 off in view, the
     * cover filled the cell the path dug, 60 steps a cell, back and forth) */
    int or_way = NT_ORACLE >= 2 && n->has_sp && n->nopath > 0 &&
                 py_hypot2(n->sx + 0.5 - r->x, n->sz + 0.5 - r->z) > NT_CAMP_D;
    if (!or_way && (NT_COVER_SEARCH || !NT_SHY || n->has_sp || n->has_camp || !n->fort_at) &&
        nt_cover(v, e, r, s, a, ey, &th))
        return 0;
    /* lane/nether5: the shy: searching the fortress (no spawner, no camp), a blaze in view with a clear line farther
     * than the fight's reach (a natural spawn that has fired keeps its distance and shoots: no sword reaches it): back
     * NT_SHY_BACK cells along the way walked, there the look at where it was; still in view, NT_SHY_BACK more; out of
     * view NT_SHY_CLEAR steps, the search again. NT_SHY_MAX steps at most, NT_SHY_OFF before the next */
    /* lane/rod (nether_blaze_seek): searching the fortress (no spawner, no camp), a blaze in view past the fight's reach
     * is walked toward (its place kept while it is in view; blazes keep near their spawner, a player follows them to
     * it) instead of the shy's back-off: gold-g7's 128 frozen runs searched the fortress 20 minutes, the spawner never
     * in view, the blazes in view backed off from */
    if (MOPT(MO_NT_BLAZE_SEEK) && n->fort_at && !n->has_sp && !n->has_camp && n->hood == 0 &&
        inv_count(s, BLAZE_ROD) < NT_RODS && th.nblaze > 0 && th.blaze_d > NT_FIGHT_D)
    {
        if (!n->bseek) n->bseek = 1, n->bseeks += 1, n->bseek_t0 = t;
        n->bsx = th.bx, n->bsy = th.by, n->bsz = th.bz, n->bseek_at = t;
    }
    if (NT_SHY && !MOPT(MO_NT_BLAZE_SEEK) && n->fort_at && !n->has_sp && !n->has_camp && n->hood == 0 &&
        n->ncrumb > 2 && inv_count(s, BLAZE_ROD) < NT_RODS)
    {
        int seen = th.nblaze > 0 && th.blaze_d > NT_FIGHT_D;
        if (!n->shy && seen && t >= n->shy_off)
        {
            n->shy = 1, n->shy_at = t, n->shy_clear = 0, n->shys += 1;
            n->shy_k = n->ncrumb - 1 - NT_SHY_BACK > 0 ? n->ncrumb - 1 - NT_SHY_BACK : 0;
            n->shx = th.bx, n->shy_y = th.by, n->shz = th.bz;
        }
        if (n->shy)
        {
            if (seen) n->shx = th.bx, n->shy_y = th.by, n->shz = th.bz;
            if (t - n->shy_at > NT_SHY_MAX || n->shy_clear >= NT_SHY_CLEAR)
            {
                n->shy = 0, n->shy_off = t + NT_SHY_OFF;
                n->wtried = 0, n->wk = -1, n->has_goal = 0, n->has_cand = 0;
            }
            else
            {
                if (n->shy_k >= n->ncrumb) n->shy_k = n->ncrumb - 1;
                int16_t *c = n->crumb[n->shy_k];
                int ph = n->phase;
                n->phase = NT_HOME;             /* the walker's travel rules (no stair dug) */
                int w = nt_walk(v, e, r, s, a, ey, c[0], c[1], c[2], 0.5, -1);
                n->phase = ph;
                if (w != 0)
                {
                    /* there (or no way on): still, crouched, the look where the blaze was */
                    double yaw, pitch;
                    look_at(r->x, ey, r->z, n->shx, n->shy_y, n->shz, &yaw, &pitch);
                    still(a);
                    a->look_mode = 1;
                    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                    a->hold = 1u << PK_SNEAK;
                    if (seen && n->shy_k > 0 && w > 0)
                        n->shy_k = n->shy_k > NT_SHY_BACK ? n->shy_k - NT_SHY_BACK : 0, n->shy_clear = 0;
                    else if (!seen)
                        n->shy_clear += 1;
                }
                return 0;
            }
        }
    }
    /* lane/nether4: the glance: at the fortress every NT_GLANCE_EVERY steps, and at once when a burn comes with no blaze
     * in view, four quarter turns (the threats' look sees round the player: a blaze out of view knocked 24 of 32 envs
     * off gold-g7's deck into the lava sea) */
    if (NT_GLANCE && !e->fid && !close && r->on_ground && n->pil_base == 0 && n->hood == 0 && e->steps - n->dig_at > 2)
    {
        int burned = r->fire && r->health < n->glance_hp && th.nblaze == 0;
        if (NT_SKEL_FIGHT && !burned && r->health < n->glance_hp && th.nblaze == 0)
        {
            /* lane/nether5: a hit with no hostile in view (an arrow from behind): the glance too */
            int seen = 0;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
                seen |= s->vis[i].kind != HK_PIGMAN && fight_kind(s->vis[i].kind) &&
                        py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y, s->vis[i].z) < NT_SKEL_D;
            burned = !seen;
        }
        n->glance_hp = r->health;
        if (n->glance == 0 && (burned || ((n->fort_at || n->has_sp || n->has_camp) &&
                                          e->steps - n->glance_at >= NT_GLANCE_EVERY)))
            n->glance = 4, n->glance_yaw = r->yaw;
        if (n->glance > 0)
        {
            n->glance -= 1, n->glance_at = e->steps;
            n->fan_at = e->steps;       /* no far view in a glance (its floors and decks are the search's, not this) */
            still(a);
            a->look_mode = 1;
            a->look[0] = n->glance_yaw + 90.0f * (float)(4 - n->glance), a->look[1] = -10.0f;
            a->hold = 1u << PK_SNEAK;
            return 0;
        }
    }
    /* lane/rod (nether_oracle_lock, PRIVILEGED): at the spawner, the nearest blaze from the client's entity list
     * (through walls, out of view: never lost) looked at, closed on within NT_OR_LOCK_CLOSE, struck whenever the
     * crosshair is on a blaze (every 10 steps: the hurt time); in place of fight()'s lock */
    if (MOPT(MO_NT_ORACLE_LOCK) && n->has_sp && inv_count(s, BLAZE_ROD) < NT_RODS && n->hood == 0 && n->heal_k < 0 &&
        py_hypot2(n->sx + 0.5 - r->x, n->sz + 0.5 - r->z) < NT_OR_LOCK_SP)
    {
        struct netherview_blaze bz[4];
        int nb = netherview_oracle_blazes(v->p, nt_id(v, e), r->x, ey, r->z, NT_OR_LOCK_R, bz, 4);
        float rod[3];
        if ((nb == 0 || bz[0].d > NT_REACH) &&
            netherview_oracle_item(v->p, nt_id(v, e), r->x, r->y, r->z, NT_OR_ROD_R, BLAZE_ROD, rod))
        {
            /* a rod's drop (the server's items) with no blaze within reach: walked to over the floor */
            n->or_picks += 1;
            int w = nt_opath_walk(v, e, r, s, a, ey, (int)floor(rod[0]), (int)floor(rod[1] + 0.2), (int)floor(rod[2]),
                                  0.6, 0);
            if (w == 0) return 0;
            if (w > 0)
            {
                double yaw, pitch;
                look_at(r->x, ey, r->z, rod[0], rod[1], rod[2], &yaw, &pitch);
                still(a);
                a->look_mode = 1;
                a->look[0] = (float)yaw, a->look[1] = (float)pitch;
                a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
                return 0;
            }
        }
        /* or3j: the lock held 14 off with every blaze out of reach (96 strikes in 4,565 steps, burnt at 12,486): away
         * from the spawner only a blaze within reach is locked, the walk to the spawner goes on otherwise */
        int at_sp = py_hypot2(n->sx + 0.5 - r->x, n->sz + 0.5 - r->z) < NT_OR_SP_NEAR + 1.0;
        /* or3k: a blaze within reach behind the room's wall held the lock 5,000 steps for 10 strikes: off the spawner a
         * lock with no strike for NT_OR_LOCK_DRY steps gives way to the walk for NT_OR_LOCK_REST */
        /* or3m: a step a while (the glance, the reads) leaves the lock: a run is broken only by a gap over 50 steps */
        if (e->steps - n->or_lock_last > 50) n->or_lock_t0 = e->steps;
        int64_t dry = e->steps - (n->or_hit_at > n->or_lock_t0 ? n->or_hit_at : n->or_lock_t0);
        if (NT_OR_LOCK_REST > 0 && !at_sp && dry > NT_OR_LOCK_DRY) n->or_lock_off = e->steps + NT_OR_LOCK_REST;
#ifdef NT_TRACE
        if (nt_id(v, e) == NT_TRACE && e->steps >= NT_TRACE_FROM && e->steps <= NT_TRACE_TO)
            fprintf(stderr, "ol %lld nb %d d %.2f at_sp %d dry %lld off %lld mo %d k %d\n", (long long)e->steps, nb,
                    nb ? bz[0].d : -1.0f, at_sp, (long long)dry, (long long)n->or_lock_off, s->mo_entity, s->mo_ekind);
#endif
        if (nb > 0 && (at_sp || (bz[0].d < NT_REACH + 1.5 && e->steps >= n->or_lock_off)))
        {
            double yaw, pitch;
            n->or_lock_last = e->steps;
            e->fid = 0;
            n->fight_kind = HK_BLAZE;
            look_at(r->x, ey, r->z, bz[0].x, bz[0].y, bz[0].z, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            a->hold = 1u << PK_SNEAK;
            if (bz[0].d > 2.6 && bz[0].d < NT_OR_LOCK_CLOSE) a->hold |= 1u << PK_FORWARD;
            int wb = best_weapon(s);
            /* or3n: a blaze within reach behind a block (the crosshair on it, not on the blaze: 10 strikes in 5,000
             * steps) has the block dug, the tool's (the walk to the room instead burnt at 8,751) */
            int wall = !s->mo_entity && s->mo_hit && bz[0].d < NT_REACH + 1.5 && s->mo_block != 52 &&
                       s->mo_block != 10 && s->mo_block != 11 && s->mo_block != 7 &&
                       py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) < bz[0].d + 0.9;
            if (wall)
            {
                wb = nt_tool(v, s, s->mo_block);
                a->hold |= 1u << PK_ATTACK;
                n->or_digs += 1;
            }
            if (wb >= 0 && wb != s->current) a->hotbar = wb;
            if (!wall && s->mo_entity && s->mo_ekind == HK_BLAZE && e->steps - n->or_hit_at >= 10)
                a->press[PK_ATTACK] = 1, n->or_hit_at = e->steps;
            n->or_lock_steps += 1;
            return 0;
        }
    }
    /* a fight running: fight()'s lock, crouched; a blaze's death leaves its drop to walk to */
    if (e->fid)
    {
        int done = fight(v, e, r, s, a, ey);
        if (!done && ((NT_SKEL_FIGHT && n->fight_kind == HK_SKELETON) ||
                      (NT_BLAZE_FAR && n->fight_kind == HK_BLAZE && !n->has_sp && !n->has_camp)) &&
            py_dist3(r->x, ey, r->z, e->hx, e->hy + 0.9, e->hz) > 2.6)
            a->hold |= 1u << PK_FORWARD;    /* lane/nether5: an archer closed on past fight()'s 10 blocks */
        if (a->hold & (1u << PK_FORWARD)) a->hold |= 1u << PK_SNEAK;
        /* lane/nether4: any blaze under the crosshair is struck (the lock's only its own: with four at the spawner the
         * crosshair stood on another one, the attack pressed once in 12 steps while they hit) */
        if (NT_ANY_BLAZE && n->fight_kind == HK_BLAZE && s->mo_ekind == HK_BLAZE && s->mo_entity != e->fid &&
            e->age - e->flast >= 10)
            a->press[PK_ATTACK] = 1, e->flast = e->age;
        /* lane/nether3: a blaze held out of reach while the player cannot close in: the lock dropped */
        /* lane/nleg (nether_brink_floor lock): any kind's lock (k20: a wither skeleton 5 to 9 blocks off across the gap,
         * the player crouched at its own bridge's corner, pressing forward, for 14,000 steps) */
        if (!done && (n->fight_kind == HK_BLAZE || MOPT(MO_NT_BRINK_FLOOR) == 2))
        {
            double mv = py_hypot2(r->x - n->fpx, r->z - n->fpz);
            double fd = py_dist3(r->x, ey, r->z, e->hx, e->hy + 0.9, e->hz);
            n->fstuck = fd > NT_REACH && mv < 0.05 ? n->fstuck + 1 : 0;
            if (n->fstuck > NT_FIGHT_STUCK)
            {
                e->fid = 0;
                n->fstuck = 0;
                n->fight_off = e->steps + NT_FIGHT_OFF;
            }
        }
        n->fpx = r->x, n->fpz = r->z;
        if (done)
        {
            int lost = 1;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
                if (s->vis[i].id == e->fid) lost = 0;
            if (lost && n->fight_kind == HK_BLAZE)
            {
                n->bkills += 1;                     /* lane/rod: the books (a lock lost on a blaze: killed or gone) */
                if (n->t_bkill < 0) n->t_bkill = e->steps;
                if (!MOPT(MO_NT_DROPS)) n->pick = 1, n->pick_age = 0, n->deck_wait = 0;   /* (nether_drops: the credited kills' drops) */
                n->rx = e->hx, n->ry = e->hy, n->rz = e->hz;
                if (!n->has_camp)
                {
                    /* lane/nether2: a blaze killed: blazes come from near here (the camp when no spawner is seen) */
                    n->has_camp = 1;
                    n->cx = (int)floor(r->x), n->cy = (int)floor(r->y + 0.13), n->cz = (int)floor(r->z);
                }
            }
            e->fid = 0;
            n->camp_age = 0;
        }
        return 0;
    }
    /* lane/nether3: fill blocks fetched into the hotbar from the main inventory when it runs low (standing on the
     * ground, still) */
    /* lane/rod (nether_arrive_fill): or in the portal itself, its frame under the box (the harvested starts' portal
     * stands over the lava sea, every corner's footing failed, the cobblestone stayed in the main inventory, no bridge
     * was ever placed and the walker judged every way bad: the policy's turns walked into the lava) */
    int in_portal = 0;
    for (int k = 0; k < 8 && MOPT(MO_NT_ARRIVE_FILL) && !in_portal; ++k)   /* the box's corners at the feet and head */
        in_portal = nt_block(v, e, (int)floor(r->x + (k & 1 ? 0.3 : -0.3)), (int)floor(r->y + 0.13) + (k >> 2),
                             (int)floor(r->z + (k & 2 ? 0.3 : -0.3))) == 90;
    if (NT_FAN && r->on_ground && n->pil_base == 0 && (in_portal || nt_footing(v, e, r)) && nt_refill(v, e, r, s, a))
        return 0;
    /* lane/nether2: hurt at the blazes: back NT_RETREAT cells along the way (out of their sight round the way's
     * turns), there until healed to NT_HEAL_HP, a blaze fought only within NT_MOB_D meanwhile */
    if (!NT_HOOD && n->heal_k < 0 && (n->has_sp || n->has_camp) && r->health <= NT_LOW_HP && n->ncrumb > 1)
        n->heal_k = n->ncrumb > NT_RETREAT + 1 ? n->ncrumb - 1 - NT_RETREAT : 0;
    if (n->heal_k >= 0 && r->health >= NT_HEAL_HP) n->heal_k = -1;
    /* lane/rod (nether_drops): a credited kill's drop within NT_DROP_NEAR walked to before a new fight (nt_drops_track) */
    if (MOPT(MO_NT_DROPS) && !n->pick && n->ndrop > n->drops)
    {
        if (n->ndrop - n->drops > 8) n->drops = n->ndrop - 8;
        double dd;
        int i = nt_drop_near(n, r, &dd);
        if (i >= 0 && dd < NT_DROP_NEAR)
        {
            int j = n->drops & 7;
            float t3[3];
            for (int c = 0; c < 3; ++c)
                t3[c] = n->drop[i & 7][c], n->drop[i & 7][c] = n->drop[j][c], n->drop[j][c] = t3[c];
            n->drops += 1;
            n->pick = 1, n->pick_age = 0, n->deck_wait = 0;
            n->rx = t3[0], n->ry = t3[1], n->rz = t3[2];
        }
    }
    int pu = MOPT(MO_NT_DROPS) && n->pick && NT_PICK_UP ? nt_pick_up(v, e, r, s, a, ey) : 0;   /* lane/rods */
    if (pu > 0) return 0;
    if (pu < 0) n->pick = 0;
    if (MOPT(MO_NT_DROPS) && n->pick && nt_deck_wait(v, e, n, r, a, ey)) return 0;   /* lane/rods2 (nether_deck_wait) */
    if (MOPT(MO_NT_DROPS) && n->pick)
    {
        double dx = n->rx - r->x, dz = n->rz - r->z, yaw, pitch;
        if (++n->pick_age > NT_PICK_AGE || py_hypot2(dx, dz) < 0.3)
            n->pick = 0;
        else
        {
            look_at(r->x, ey, r->z, n->rx, r->y, n->rz, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 30.0f;
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
            return 0;
        }
    }
    /* lane/nether5: searching (no spawner, no camp), a blaze fought from NT_FIGHT_FAR_D (with natural spawning blazes
     * hover over the fortress's decks away from any spawner: gold-g7's env 0 had one in view at 6 to 16 blocks for 290
     * steps, never fought) */
    double bfd = NT_BLAZE_FAR && !n->has_sp && !n->has_camp ? NT_FIGHT_FAR_D : NT_FIGHT_D;
    int f = nt_foe(r, s, n->heal_k >= 0 ? NT_MOB_D : e->steps < n->fight_off ? NT_REACH : bfd,
                   NT_SKEL_FIGHT && n->heal_k < 0);
    if (f >= 0)
    {
        e->fid = s->vis[f].id;
        e->flost = 0, e->flast = -100;
        e->age = 0;
        n->fight_kind = s->vis[f].kind;
        n->fstuck = 0, n->fpx = r->x, n->fpz = r->z;
        return fight(v, e, r, s, a, ey);
    }
    /* lane/rods2 (nether_eye_strike): a blaze whose middle is hidden (the vis list's ray, so no foe above) and whose eye
     * is on the screen: it sees the player (its sight starts at its eye) and shoots; looked at at its eye, still, struck
     * with the crosshair on it (the rod hunt's camp: a blaze at the mouth of the hole over the head, 2.5 to 3 over the
     * eye, 190,844 steps seen by none of the macro's tests) */
    /* (reach: only a blaze whose eye is within NT_EYE_REACH, its box then inside the hit's NT_REACH; on struck at any
     * within NT_FIGHT_D, and the eight starts' s42k50 stood still 36,000 of its 43,635 strike steps with the crosshair
     * off the blaze, most 3 to 6 off) */
    struct netherview_eye eb;
    if (MOPT(MO_NT_EYE_STRIKE) && n->heal_k < 0 &&
        netherview_eye_blaze(v->p, nt_id(v, e), r->x, ey, r->z, r->yaw, r->pitch, NT_FIGHT_D,
                             MOPT(MO_NT_EYE_STRIKE) == 3, &eb) == 1 &&
        (MOPT(MO_NT_EYE_STRIKE) == 1 || eb.d <= NT_EYE_REACH))
    {
        /* (margin: the aim at the grid point of its box with the most clear neighbours, netherview_eye_blaze) */
        double yaw, pitch;
        look_at(r->x, ey, r->z, eb.mx, eb.my, eb.mz, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = 1u << PK_SNEAK;
        int wb = best_weapon(s);
        if (wb >= 0 && wb != s->current) a->hotbar = wb;
        if (s->mo_entity == eb.id) a->press[PK_ATTACK] = 1;
        n->eye_strikes += 1;
        return 0;
    }
    /* lane/rods2 (nether_mouth_cover): a blaze on the screen past the hit's reach (its eye over NT_EYE_REACH, within
     * NT_MOUTH_D) whose sight of the player comes down through the open cell over the head (the camp's hole): that
     * cell filled, and the camp's re-dig of it held NT_MOUTH_HOLD steps; never with a rod lying over the head (the
     * pick's drop 1.5 or more over the feet). The rod hunt's eight starts: one block there shut every line open on
     * 170,733 of the 190,844 camp steps a blaze at the hole's mouth shot unseen (3,541 health of 3,977) */
    if (MOPT(MO_NT_MOUTH_COVER) && n->heal_k < 0 && n->has_sp && r->on_ground && !(n->pick && n->ry > r->y + 1.5))
    {
        int mfx = (int)floor(r->x), mfy = (int)floor(r->y + 0.13), mfz = (int)floor(r->z);
        struct netherview_eye mb;
        if (nt_open(nt_block(v, e, mfx, mfy + 2, mfz)) &&
            netherview_mouth_blaze(v->p, nt_id(v, e), r->x, ey, r->z, r->yaw, r->pitch, NT_EYE_REACH, NT_MOUTH_D,
                                   &mb) == 1)
        {
            n->mouth_until = e->steps + NT_MOUTH_HOLD;
            n->mouth_steps += 1;
            int before = n->covers;
            if (nt_cover_place(v, e, r, s, a, ey, mfx, mfy + 2, mfz, 0, 0))
            {
                if (n->covers > before) n->mouths += 1;
                return 0;
            }
        }
    }
    a->look_mode = 1;
    a->look[0] = r->yaw, a->look[1] = r->pitch;
    /* lane/rods (nether_hood_starve rest): no rest begun and none kept with no heal coming (nt_noregen: the food level
     * under 18, nothing to eat held): 21 of the rod hunt's 29 limits rested at 1 to 6 health, food 16 or 17, to the
     * bound; the camp goes on */
    if (MOPT(MO_NT_HOOD_STARVE) == 2 && nt_noregen(v, r, s)) n->resting = 0;
    else if (r->health <= NT_REST_HP && n->heal_k < 0 && !n->pick)
    {
        /* lane/nether2: hurt away from the blazes: no travel (a touch of fire or lava kills), crouched and still, a slow
         * look round, until healed (food 18 or more heals; the food macro eats under 18) */
        n->resting = 1;
        a->look[0] = r->yaw + 3.0f, a->look[1] = 10.0f;
        a->hold = 1u << PK_SNEAK;
        return 0;
    }
    if (n->resting && r->health < NT_HEAL_HP)
    {
        a->look[0] = r->yaw + 3.0f, a->look[1] = 10.0f;
        a->hold = 1u << PK_SNEAK;
        return 0;
    }
    n->resting = 0;
    if (n->heal_k >= 0 && n->ncrumb > 0)
    {
        if (n->heal_k >= n->ncrumb) n->heal_k = n->ncrumb - 1;
        int16_t *c = n->crumb[n->heal_k];
        n->phase = NT_HOME;                 /* the walker's travel rules (no stair dug) */
        int w = nt_walk(v, e, r, s, a, ey, c[0], c[1], c[2], 0.5, -1);
        n->phase = NT_CAMP;
        if (w != 0)
        {
            still(a);
            a->look_mode = 1;
            a->look[0] = r->yaw + 6.0f, a->look[1] = 0.0f;
        }
        return 0;
    }
    /* a kill's drop: walked to, crouched */
    pu = n->pick && NT_PICK_UP ? nt_pick_up(v, e, r, s, a, ey) : 0;   /* lane/rods (nether_pick_up) */
    if (pu > 0) return 0;
    if (pu < 0) n->pick = 0;
    if (n->pick && nt_deck_wait(v, e, n, r, a, ey)) return 0;   /* lane/rods2 (nether_deck_wait) */
    if (n->pick)
    {
        double dx = n->rx - r->x, dz = n->rz - r->z, yaw, pitch;
        if (++n->pick_age > NT_PICK_AGE || py_hypot2(dx, dz) < 0.3)
            n->pick = 0;
        else
        {
            look_at(r->x, ey, r->z, n->rx, r->y, n->rz, &yaw, &pitch);
            a->look[0] = (float)yaw, a->look[1] = 30.0f;
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
            return 0;
        }
    }
    /* a look round: the scans (every 3 steps) and the cast see every side */
    if (n->spin > 0)
    {
        n->spin -= 1;
        a->look[0] = n->spin_yaw + 30.0f * (float)(NT_SPIN - n->spin);
        /* a ring a little up, one down, then (lane/nether2) one well up: a spawner over a corridor's ceiling's edge */
        a->look[1] = n->spin >= 2 * NT_SPIN / 3 ? -10.0f : n->spin >= NT_SPIN / 3 ? 35.0f : -50.0f;
        return 0;
    }
    if (NT_FAN && n->clook > 0 && n->pil_base == 0)
    {
        /* lane/nether3: a look round up (still, crouched; 75 and 35 degrees up in turn, 30 degrees a step): the fans
         * see a deck over the player */
        n->clook -= 1;
        a->look[0] = r->yaw + 30.0f, a->look[1] = n->clook & 1 ? -75.0f : -35.0f;
        a->hold = 1u << PK_SNEAK;
        return 0;
    }
    int fx = (int)floor(r->x), fz = (int)floor(r->z), w;
    int homing = inv_count(s, BLAZE_ROD) >= NT_RODS;
    if (NT_FAN && !homing && !n->has_sp && !n->has_camp && n->clook == 0 && nt_gather(v, e, r, s, a, ey)) return 0;
#ifdef NT_HOME_AT
    homing |= n->t_arrive >= 0 && e->steps - n->t_arrive >= NT_HOME_AT;   /* a test build of the way home */
#endif
    if (NT_FAN)
    {
        /* lane/nether3: on the fortress: standing on its own blocks */
        int fy = (int)floor(r->y + 0.13), ub = nt_block(v, e, fx, fy - 1, fz), deck = 0;
        for (int d = 0; d < 4 && (ub == 112 || ub == 114) && !nt_own(n, fx, fy - 1, fz); ++d)
        {
            /* a deck or a hall's floor: fortress blocks beside the floor block (a pillar of the player's own fill,
             * nether brick among it, has none but the fortress's own support beside it) */
            int b = nt_block(v, e, fx + NDX[d], fy - 1, fz + NDZ[d]);
            deck += b == 112 || b == 113 || b == 114;
        }
        if (!homing && !n->fort_at && r->on_ground && deck >= 2 && n->pil_base == 0)
        {
#ifdef NT_TRACE
            if (nt_id(v, e) == NT_TRACE) fprintf(stderr, "nt %lld fort_at by deck %d ub %d\n", (long long)t, deck, ub);
#endif
            n->fort_at = 1, n->fort_ty = fy, n->climbed = 0, n->has_cc = 0, n->fort_stall = 0;
            n->spin = NT_SPIN, n->spin_yaw = r->yaw;
            n->leg_at = t, n->leg_x = r->x, n->leg_z = r->z;
            n->has_goal = 0, n->has_cand = 0;
            n->wtried = 0, n->wk = -1;
            return 0;
        }
    }
    if (MOPT(MO_NT_ENTRY) && !homing && !n->fort_at && nt_entry_walk(v, e, r, s, a, ey) == 0) return 0;
    if (NT_FAN && !homing && !n->has_sp && !n->has_camp && !n->fort_at && n->has_top && t >= n->climb_off)
    {
        int fy = (int)floor(r->y + 0.13);
        if (n->ty >= fy && n->ty - fy <= NT_CLIMB_MAX &&
            py_hypot2(n->tx + 0.5 - r->x, n->tz + 0.5 - r->z) <= NT_CLIMB_NEAR)
        {
            /* lane/nether3: the fortress over the player: climbed to */
            n->phase = NT_FORT;
            if (nt_climb(v, e, r, s, a, ey) == 0) return 0;
            n->climb_off = t + NT_CLIMB_OFF, n->has_cc = 0, n->has_top = 0, n->pil_base = 0;
            n->wtried = 0, n->wk = -1;
        }
        else if (n->climbed && fy > n->ty)
        {
            /* up: the fortress gone to again from here (its height the search's) */
            n->climbed = 0, n->has_cc = 0, n->fort_at = 0;
            n->fx = n->tx, n->fy = n->ty, n->fz = n->tz;      /* the top climbed to: walked onto */
            n->wtried = 0, n->wk = -1;
        }
    }
    if (homing)
    {
        n->phase = NT_HOME;
        if (MOPT(MO_NT_CHAMBER) && !nt_sp_break_pending(v, e, s) && nt_chamber_walk(v, e, r, s, a, ey) == 0)
            return 0;
        if (nt_sp_break_pending(v, e, s))
        {
            if (!n->sp_break_at)
            {
                n->sp_break_at = e->steps;
#ifdef SP_BREAK_TRACE
                fprintf(stderr, "SPBREAK_START env=%d step=%lld sp=%d,%d,%d blazes16=%d\n", nt_id(v, e),
                        (long long)e->steps, n->sx, n->sy, n->sz, nt_sp_break_blazes(v, e, r));
#endif
            }
            int w = nt_opath_walk(v, e, r, s, a, ey, n->sx, n->sy - NT_OR_UNDER, n->sz, NT_OR_UNDER_NEAR, 3);
            if (w == 0) return 0;
            if (w < 0)
            {
                n->sp_break_at = e->steps - NT_SP_BREAK_MAX;  /* no sheltered route: use the old portal plan */
                return 0;
            }
            double yaw, pitch;
            look_at(r->x, ey, r->z, n->sx + 0.5, n->sy + 0.5, n->sz + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            a->hold = 1u << PK_SNEAK;
            if (s->mo_hit && s->mo_block > 0 && s->mo_block != 7 && s->mo_block != 10 && s->mo_block != 11 &&
                py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) < NT_REACH)
            {
                int tb = nt_tool(v, s, s->mo_block);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->hold |= 1u << PK_ATTACK;
            }
            return 0;
        }
        if (!n->has_portal)
        {
            /* no portal seen at the arrival: nowhere to go (report.md's scope: a portal built here). lane/nether2: held
             * where it stands, crouched, a look round (a blaze within NT_FIGHT_D still fought): handed to the policy
             * it walked into the lava in 23 of 32 envs after the 7th rod */
            a->look_mode = 1;
            a->look[0] = r->yaw + 6.0f, a->look[1] = 0.0f;
            a->hold = 1u << PK_SNEAK;
            return 0;
        }
        /* lane/rod (nether_oracle tunnel): a closed departure (lane/chain, 6abf7435: 341 of 347 deaths burning within 8 of
         * the spawner as the walk home began): the hood's roof and sides first (the hole over the head shut), then
         * still, crouched, until NT_DEP_QUIET steps with no fire and no blaze in view; the tunnel after */
        if (NT_ORACLE >= 4 && !n->dep_done)
        {
            if (n->hood <= 1)
            {
                if (nt_hood(v, e, r, s, a, ey) && n->hood == 1) return 0;
            }
            int blaze = 0;
            for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i) blaze |= s->vis[i].kind == HK_BLAZE;
            n->dep_quiet = r->fire || blaze ? 0 : n->dep_quiet + 1;
            if (n->dep_quiet < NT_DEP_QUIET && ++n->dep_age < NT_DEP_MAX)
            {
                still(a);
                a->look_mode = 1;
                a->look[0] = r->yaw, a->look[1] = 0.0f;
                a->hold = 1u << PK_SNEAK;
                return 0;
            }
            /* then down NT_DEP_SINK blocks under the feet, in its own shaft (tunp-9bt: the planner's first cells east of
             * the pocket were open to the room, the player stood there and burnt), while what is two under is no lava
             * and no air (no drop into an open space) */
            int sx = (int)floor(r->x), sy = (int)floor(r->y + 0.13), sz = (int)floor(r->z);
            if (n->dep_sink == 0) n->dep_y0 = sy;
            int b1 = nt_block(v, e, sx, sy - 1, sz), b2 = nt_block(v, e, sx, sy - 2, sz);
            if (n->dep_y0 - sy < NT_DEP_SINK && b1 > 0 && b1 != 7 && b1 != 10 && b1 != 11 && b2 > 0 && b2 != 10 &&
                b2 != 11 && !nt_open(b2) && ++n->dep_sink < NT_DEP_SINK_MAX)
            {
                still(a);
                a->look_mode = 1;
                a->look[0] = r->yaw, a->look[1] = 90.0f;
                int tb = nt_tool(v, s, b1);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->hold = (1u << PK_ATTACK) | (1u << PK_SNEAK);
                return 0;
            }
            n->dep_done = 1, n->hood = 0;
        }
        /* lane/nether2: the way walked, in reverse (nt_home); in the portal's cell: still until the overworld */
        w = nt_home(v, e, r, s, a, ey);
        if (w > 0) return 0;
    }
    else if (n->has_sp)
    {
        n->phase = NT_CAMP;
        /* lane/rod (nether_oracle spawner): the path over the fortress's floor; the walker where none is found */
        /* the cell NT_OR_UNDER under the spawner (gold-g8's: -129.5 80 -100.6 under -130 83 -99, below the room's
         * floor, 7 rods in 29 and 31 of 32 there with the default fight) */
        int ctx = n->sx, ctz = n->sz;
        if (NT_CAMP_SIDE && NT_ORACLE >= 2) nt_camp_cell(v, e, r, &ctx, &ctz);   /* lane/nleg */
        w = NT_ORACLE >= 2 ? nt_opath_walk(v, e, r, s, a, ey, ctx, n->sy - NT_OR_UNDER, ctz,
                                           ctx != n->sx || ctz != n->sz ? NT_CAMP_SIDE_NEAR : NT_OR_UNDER_NEAR,
                                           MOPT(MO_NT_SP_TUN) ? 3 : 1)
                             : -1;
        if (w == 0) return 0;
        if (w < 0) w = nt_walk(v, e, r, s, a, ey, n->sx, n->sy, n->sz, NT_CAMP_D, -1);
        if (w > 0 && NT_ORACLE >= 2)
        {
            /* lane/rod (nether_oracle spawner): under the spawner, the block over the head dug (or4b: sealed in the
             * room's foundation, no blaze in view from 7,000 to 40,000): a hole into the room beside the spawner */
            int hx = (int)floor(r->x), hy = (int)floor(r->y + 0.13) + 2, hz = (int)floor(r->z);
            int hb = nt_block(v, e, hx, hy, hz), ub = nt_block(v, e, hx, hy + 1, hz);
            /* lane/rods2 (nether_mouth_cover): not while the mouth is held shut */
            int mouth_held = MOPT(MO_NT_MOUTH_COVER) && e->steps < n->mouth_until;
            if (hb > 0 && hb != 52 && hb != 7 && hb != 10 && hb != 11 && ub != 10 && ub != 11 && !mouth_held)
            {
                still(a);
                a->look_mode = 1;
                a->look[0] = r->yaw, a->look[1] = -90.0f;
                int tb = nt_tool(v, s, hb);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->hold = 1u << PK_ATTACK;
                return 0;
            }
        }
        if (w > 0)
        {
            /* at the spawner: the look turns round it, a little up (blazes fly), until one is in view; under it
             * (nether_oracle) steeply up through the hole (or4c: a blaze in view 66 steps of 2,001 at -15) */
            n->camp_age += 1;
            a->look[0] = r->yaw + 12.0f;
            a->look[1] = NT_ORACLE >= 2 ? NT_OR_UNDER_PITCH : -15.0f;
            return 0;
        }
    }
    else if (n->has_camp && !homing && NT_CAMP_GIVEUP_ON && n->camp_age > NT_CAMP_GIVEUP)
    {
        /* lane/nether5: a camp with no spawner (a natural blaze killed there) and no blaze killed since in
         * NT_CAMP_GIVEUP steps: given up, the fortress searched again (gold-g7's env 0 camped from step 15,000 to the
         * end over one natural blaze's kill) */
        n->has_camp = 0, n->camp_age = 0, n->camps_given += 1;
        n->wtried = 0, n->wk = -1;
        still(a);
        return 0;
    }
    else if (n->has_camp && !homing)
    {
        /* lane/nether2: no spawner seen, a blaze killed here: the camp is here (the look round as at the spawner) */
        n->phase = NT_CAMP;
        w = nt_walk(v, e, r, s, a, ey, n->cx, n->cy, n->cz, NT_CAMP_D * 0.5, -1);
        if (w > 0)
        {
            n->camp_age += 1;
            a->look[0] = r->yaw + 12.0f;
            a->look[1] = n->camp_age % 120 < 60 ? -15.0f : -45.0f;
            return 0;
        }
    }
    else if (NT_GOALS && n->has_fort && !n->fort_at && py_hypot2(n->fx + 0.5 - r->x, n->fz + 0.5 - r->z) > NT_FORT_NEAR &&
             n->fort_direct_off > t)
    {
        /* lane/nether3: a fortress seen far off and the straight way to it given up: the far search's goals along
         * the bearing to it */
        n->phase = NT_SEARCH;
        double yaw, pitch;
        look_at(r->x, ey, r->z, n->fx + 0.5, n->fy + 0.5, n->fz + 0.5, &yaw, &pitch);
        n->bearing = (float)yaw;
        if (nt_search_fan(v, e, r, s, a, ey) == 0) return 0;
        n->goal_at = 0;
        w = nt_walk(v, e, r, s, a, ey, n->fx, n->fy, n->fz, 3.0, -1);
    }
    else if (n->has_fort && !n->fort_at)
    {
        int fy = (int)floor(r->y + 0.13);
        n->phase = py_hypot2(n->fx + 0.5 - r->x, n->fz + 0.5 - r->z) > NT_FORT_NEAR ? NT_SEARCH : NT_FORT;
        if (n->pil_base != 0 ||
            (n->fy > fy + 1 && py_hypot2(n->fx + 0.5 - r->x, n->fz + 0.5 - r->z) < NT_PILLAR_NEAR && r->on_ground &&
             t >= n->pil_off && nt_open(nt_block(v, e, fx, fy + 2, fz)) && nt_open(nt_block(v, e, fx, fy + 3, fz))))
        {
            /* lane/nether2: fortress brick seen over the player within NT_PILLAR_NEAR: a pillar up beside it */
            int p = nt_pillar(v, e, r, s, a);
            if (p >= 0)
            {
                if (p > 0) still(a), a->hold = 1u << PK_SNEAK;
                return 0;
            }
            n->pil_off = t + NT_PILLAR_OFF;
        }
        /* The mapped top brick can lie below a netherrack ledge. At seed 1 the ordinary NT_FORT walker refused the
         * netherrack at (17,89,-47) for thousands of steps while the brick was at (22,69,-53). The privileged tunnel
         * option digs a path to the standing cell above that brick before falling back to the ordinary walker. */
        if (NT_ORACLE >= 4)
        {
            int ow = nt_opath_walk(v, e, r, s, a, ey, n->fx, n->fy + 1, n->fz, 1.0, 3);
            if (ow == 0) return 0;
        }
        else if (NT_FORT_PATH && NT_ORACLE >= 2)
        {
            /* lane/nleg (nether_fort_path): the walker digs nothing but the fortress's own blocks here, so a kept brick
             * behind netherrack is never reached (seed 2: 14 of 64 stepped between two cells 6 to 14 blocks from it
             * to the bound); stalled NT_FORT_STALL steps, the same tunnel path as nether_oracle tunnel's */
            n->fort_stall = n->phase == NT_FORT ? n->fort_stall + 1 : 0;
            if (n->fort_stall > NT_FORT_STALL)
            {
                int ow = nt_opath_walk(v, e, r, s, a, ey, n->fx, n->fy + 1, n->fz, 1.0, 3);
                if (ow == 0) return 0;
            }
        }
        w = nt_walk(v, e, r, s, a, ey, n->fx, n->fy, n->fz, 3.0, -1);
        if (NT_GOALS && w < 0 && n->phase == NT_SEARCH)
        {
            n->fort_direct_off = t + NT_FORT_GOALS;     /* lane/nether3: the far search's goals toward it a while */
            n->wtried = 0, n->wk = -1;
            return 0;
        }
        if (NT_FAN && w > 0 && n->fort_looks >= NT_FORT_LOOKS && n->has_top && n->ty > (int)floor(r->y + 0.13) + 3 &&
            n->climb_fails < NT_CLIMB_FAILS)
        {
            /* lane/nether3: a deck seen well over the player: not this height, the climb again */
            n->climb_fails += 1;
            n->climb_off = 0, n->has_cc = 0;
            n->clook = NT_CLIMB_LOOKS;
            return 0;
        }
        if (NT_FAN && w > 0 && n->fort_looks < NT_FORT_LOOKS)
        {
            /* lane/nether3: at the fortress's foot, not on it: a look round (its ring up sees a deck over the player:
             * the climb next) before taking this height as the fortress's */
            n->fort_looks += 1;
            n->clook = 2 * NT_CLIMB_LOOKS;
            n->climb_off = 0;
            return 0;
        }
        if (w > 0)
        {
            /* at the fortress: a look round for the spawner, then the search at its height */
#ifdef NT_TRACE
            if (nt_id(v, e) == NT_TRACE) fprintf(stderr, "nt %lld fort_at by reach\n", (long long)t);
#endif
            n->fort_at = 1, n->fort_stall = 0;
            n->fort_ty = (int)floor(r->y + 0.13);  /* lane/nether2: the height reached, kept (the nearest brick moves) */
            n->spin = NT_SPIN, n->spin_yaw = r->yaw;
            n->leg_at = t, n->leg_x = r->x, n->leg_z = r->z;
            return 0;
        }
    }
    else
    {
        if (n->has_loc && !n->has_fort && (n->loc_mode || MOPT(MO_NT_FAR_DIG)))
        {
            /* A distant start can be beyond loaded chunks. Keep the sealed path until brick is actually seen: a
             * generated piece can clear loc_mode while still too far away for the player to see any of it. */
            n->phase = NT_SEARCH;
            int dest_y = MOPT(MO_NT_FAR_DIG) == 2 ? NT_FAR_LOW_Y : n->loc_y;
            int ow = nt_opath_walk(v, e, r, s, a, ey, n->loc_x, dest_y, n->loc_z, 8.0, 2);
#ifdef NT_FAR_DEBUG
            if (e->steps % 500 == 0)
                fprintf(stderr, "far-path step=%lld player=%.2f,%.2f,%.2f ow=%d path=%d/%d off=%d hold=%u\n",
                        (long long)e->steps, r->x, r->y, r->z, ow, n->opath_k, n->nopath,
                        n->opath_off, a->hold);
#endif
            if (ow == 0) return 0;
            if (ow < 0 && MOPT(MO_NT_FAR_DIG))
            {
                /* The partial path ended at loaded chunks. Dig toward its distant target without losing the portal. */
                int px = n->px, py = n->py, pz = n->pz;
                n->px = n->loc_x, n->py = dest_y, n->pz = n->loc_z;
                n->mode = NM_DIG;
                int dg = nt_digger(v, e, r, s, a, ey, MOPT(MO_NT_FAR_DIG) == 2 ? 2 : 1);
                n->px = px, n->py = py, n->pz = pz;
                if (dg == 0) return 0;
            }
        }
        n->phase = n->fort_at ? NT_FORT : NT_SEARCH;
        if (n->bseek)
        {
            /* lane/rod (nether_blaze_seek): toward the blaze last seen, at the fortress's height, until within
             * NT_SEEK_NEAR across, out of view NT_SEEK_LOST steps, NT_SEEK_MAX steps in all, or no way on */
            if (n->has_sp || n->has_camp || t - n->bseek_at > NT_SEEK_LOST || t - n->bseek_t0 > NT_SEEK_MAX ||
                py_hypot2(n->bsx - r->x, n->bsz - r->z) < NT_SEEK_NEAR)
                n->bseek = 0;
            else
            {
                /* deck (2): only onto the fortress's own floor (brick under every step: no bridge, no drop, no
                 * lava floor; on: 32 of 32 dead walking at blazes over the lava sea) */
                n->deck_only = MOPT(MO_NT_BLAZE_SEEK) == 2;
                int ws = nt_walk(v, e, r, s, a, ey, (int)floor(n->bsx), n->fort_ty, (int)floor(n->bsz), 1.0, -1);
                n->deck_only = 0;
                if (ws == 0) return 0;
                n->bseek = 0;
                n->wtried = 0, n->wk = -1;
            }
        }
        int digleg = 0;
        if (NT_GOALS)
        {
            if (!n->fort_at && nt_search_fan(v, e, r, s, a, ey) == 0) return 0;
            digleg = !n->fort_at;      /* lane/nether3: no goal seen: the heading (the bearing's) walked, dug */
        }
        if (NT_FAN)
        {
            /* lane/nether3: in the fortress, its floors the far view sees walked to (the farthest not walked near), a
             * look round at each: the decks and halls explored for the spawner */
            if (n->fort_at && nt_search_fan(v, e, r, s, a, ey) == 0) return 0;
            if (n->fort_at) digleg = 1;
        }
        /* lane/nether2: a leg ends at its length in blocks along its heading (a dug leg covers about a block in 12
         * steps, a walked one in 5), or at NT_LEG_MAX steps */
        double along = (r->x - n->leg_x) * NDX[n->heading] + (r->z - n->leg_z) * NDZ[n->heading];
        double leg_d = n->legk ? (NT_SWEEP_BACK ? NT_BACK_XD : NT_XLEG_D)
                               : n->leg_len > 0 ? n->leg_len : NT_LEG_D;   /* lane/rod: a back sweep's */
        if (along > n->sbest + 1.0) n->sbest = along, n->sbest_t = t;
        if (!digleg && (along >= leg_d || t - n->leg_at > (int64_t)(NT_LEG_MAX * (leg_d / NT_LEG_D)) ||
                        (NT_SURF && !n->fort_at && t - n->sbest_t > NT_SURF_STUCK)))
        {
            /* lane/rod (surface): or no block gained along the heading in NT_SURF_STUCK steps (a pocket walked round:
             * each new cell's directions are worth a try again, so the walker never gives up there) */
            nt_leg_next(n, r, t, NT_SWEEP_BACK && !n->fort_at);
            return 0;
        }
        /* the leg's cardinal held; blocked, the side toward the sweep's other direction first (flipped while that
         * side is bad, kept flipped until the held way opens: a shore followed one way) */
        int ty = n->fort_at ? n->fort_ty : NT_Y, h = n->heading, other = n->legk ? n->zdir : n->xdir;
        int fy = (int)floor(r->y + 0.13);
        int surf = NT_SURF && !n->fort_at;
        if (surf)
        {
            /* lane/rod (nether_search surface): the netherrack walked as it lies (the target at the feet's height: the
             * walker's natural steps and drops follow the ground), never a pillar to a search height; every
             * NT_SURF_SCAN steps a pillar of NT_SURF_PIL for a look round over the ground's lips */
            ty = fy;
            if (n->surf_next == 0) n->surf_next = t + NT_SURF_SCAN;
            if (n->spil == 0 && t >= n->surf_next && r->on_ground && n->pil_base == 0 &&
                nt_col_open(v, e, fx, fy, fy + 1 + NT_SURF_PIL, fz))
                n->spil = NT_SURF_PIL, n->surf_pils += 1;
            if (n->spil > 0)
            {
                int p = nt_pillar(v, e, r, s, a);
                if (p >= 0)
                {
                    if (p > 0)
                    {
                        still(a), a->hold = 1u << PK_SNEAK;    /* a block up: this step crouched, still */
                        if (--n->spil == 0)
                        {
                            n->spin = NT_SPIN, n->spin_yaw = r->yaw;     /* at the top: the look round */
                            n->surf_next = t + NT_SURF_SCAN;
                        }
                    }
                    return 0;
                }
                n->spil = 0;
                n->surf_next = t + NT_SURF_SCAN;
            }
            else if (t >= n->surf_next)
                n->surf_next = t + NT_SURF_SCAN / 4;    /* no room over the head here: soon again */
        }
        /* lane/nether5: on the fortress, fallen NT_REJOIN_DROP or more under its height off its blocks (onto the
         * netherrack under a deck, where the far view sees no floor of it): the pillar back up too */
        int ub = nt_block(v, e, fx, fy - 1, fz);
        int off_fort = (ub != 112 && ub != 113 && ub != 114) || nt_own(n, fx, fy - 1, fz);
        /* (or NT_REJOIN_DEEP under it on its own lower blocks: gold-g7's walk down from the deck at 74 to 65, 17,500
         * steps paced there) */
        int rejoin = NT_FORT_REJOIN && n->fort_at &&
                     ((off_fort && fy <= ty - NT_REJOIN_DROP) || fy <= ty - NT_REJOIN_DEEP);
        if ((!n->fort_at || NT_FORT_REJOIN) &&
            (n->pil_base != 0 ||
             ((n->fort_at ? rejoin : fy < ty) && r->on_ground && t >= n->pil_off &&
              nt_open(nt_block(v, e, fx, fy + 2, fz)) && nt_open(nt_block(v, e, fx, fy + 3, fz)))))
        {
            /* lane/nether2: below the search's height with open air over the head (in view, looking up): a pillar
             * up, never a dig */
            int p = nt_pillar(v, e, r, s, a);
            if (p >= 0)
            {
                if (p > 0) still(a), a->hold = 1u << PK_SNEAK;    /* a block up: this step crouched, still */
                return 0;
            }
            n->pil_off = t + NT_PILLAR_OFF;
        }
        if (n->wk < 0 && !n->wflip) n->wside = ((h + 1) & 3) == other ? 1 : -1;
        int side0 = n->wside;
        w = nt_walk(v, e, r, s, a, ey, fx + 64 * NDX[h], ty, fz + 64 * NDZ[h], 1.0, h);
        if (n->wside != side0) n->wflip = 1;
        if (n->wk == 3 && n->wdir == h) n->wflip = 0;
    }
    if (w < 0)
    {
        /* no way on: the policy's turn, the search's next leg after it */
        if (NT_GOALS && n->phase == NT_SEARCH && !n->fort_at)
        {
            /* lane/nether3: the dig leg ends, the far search's look round next, the bearing turned */
            n->goal_at = 0, n->spin_none = 0;
            n->bearing = (float)remainder((double)n->bearing + NT_BEAR_TURN, 360.0);
        }
        n->off = t + NT_OFF;
        if (n->phase == NT_SEARCH || n->phase == NT_FORT) nt_leg_next(n, r, t, NT_SWEEP_BACK && !n->fort_at);
        n->spin = 0;
        n->wtried = 0, n->wk = -1;
        return 1;
    }
    return 0;
}

/* lane/nether3: the search by the far view: the goal (the best floor the look round's fans saw) walked to; reached,
 * given up (no way on) or NT_GOAL_MAX steps old, a look round and the next. 0 the step driven, 1 no floor seen worth
 * a goal after a look round (the caller walks its heading instead, NT_DIG_LEG steps) */
#define NT_DIG_LEG 200
__attribute__((unused)) static int nt_search_fan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    struct nt_env *n = &e->nt;
    int64_t t = e->steps;
    if (t < n->goal_at && !n->has_goal) return 1;          /* a dig leg running (goal_at: its end) */
    if (!n->has_goal)
    {
        if (!n->has_cand)
        {
            int fk = NT_FRONTIER && n->fort_at && n->spin_none ? nt_front_pick(n, r) : -1;
            if (fk >= 0)
            {
                /* lane/nether5: in the fortress, no new floor in view: the nearest remembered one not yet walked near
                 * (a dig leg there dug netherrack beside the decks for the rest of the episode) */
                n->cx2 = n->front[fk][0], n->cy2 = n->front[fk][1], n->cz2 = n->front[fk][2], n->clava = 0;
                n->fronts += 1;
                goto take;
            }
            if (n->spin_none)
            {
                /* a look round saw no floor worth a goal: the heading walked (dug) a while, the bearing turned */
                n->spin_none = 0;
                n->goal_at = t + NT_DIG_LEG;
                n->bearing = (float)remainder((double)n->bearing + NT_BEAR_TURN, 360.0);
                n->heading = (int)lround(remainder((double)n->bearing, 360.0) / 90.0 + 4.0) & 3;
                return 1;
            }
            n->spin_none = 1;
            n->spin = NT_SPIN, n->spin_yaw = r->yaw;
            a->look_mode = 1;
            a->look[0] = r->yaw, a->look[1] = r->pitch;
            return 0;
        }
    take:
        n->spin_none = 0;
        n->has_goal = 1, n->has_cand = 0;
        n->gx = n->cx2, n->gy = n->cy2, n->gz = n->cz2, n->glava = n->clava;
        n->goal_at = t, n->goals += 1;
        n->wtried = 0, n->wk = -1;
    }
    int w = nt_walk(v, e, r, s, a, ey, n->gx, n->gy, n->gz, NT_GOAL_NEAR, -1);
    if (w != 0 || t - n->goal_at > NT_GOAL_MAX)
    {
        if (n->nfront) nt_front_done(n, n->gx, n->gy, n->gz);   /* lane/nether5: reached or given up: done */
        n->goal_fail = w > 0 ? 0 : n->goal_fail + 1;
        if (n->goal_fail >= NT_GOAL_FAILS)
            n->bearing = (float)remainder((double)n->bearing + NT_BEAR_TURN, 360.0), n->goal_fail = 0;
        n->has_goal = 0, n->has_cand = 0;
        n->spin = NT_SPIN, n->spin_yaw = r->yaw;
        n->wtried = 0, n->wk = -1;
        still(a);
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = r->pitch;
    }
    return 0;
}

/* lane/nether: NETH_VEC_NETHER's sword plan for the craft macro, -1 none: no sword held as good, a stone pickaxe made,
 * a stick and a table at hand: an iron sword from 2 ingots beyond what the iron plans still want (the iron pickaxe 3,
 * the bucket 3, flint and steel 1), else a stone sword from 2 cobblestone */
static int nt_sword_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, int64_t sticks, int64_t tables, int64_t cobble, int64_t tier)
{
    if (!(v->macros & NETH_VEC_NETHER) || r->dim != 0 || tier < 2 || sticks < 1 || !(tables > 0 || near_table(e, r)))
        return -1;
    int64_t ingots = inv_count(s, INGOT);
    int64_t keep = (tier < 3 ? 3 : 0) +
                   (inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0 ? 3 : 0) +
                   (inv_count(s, FLINT_STEEL) == 0 ? 1 : 0);
    int iron = inv_count(s, 267) + inv_count(s, 276) > 0, stone = iron || inv_count(s, 272) > 0;
    if (!iron && ingots - keep >= 2) return C_ISWORD;
    if (!stone && cobble >= 2) return C_SSWORD;
    return -1;
}

/* lane/nether2: env id's client world block at a cell (-1 not loaded), for nether_case's --dump (a developer's map of a
 * start); the macro never calls it */
int neth_vec_nether_block(const struct neth_vec *v, int id, int x, int y, int z)
{
    if (v == NULL || id < 0 || id >= v->n) return -1;
    return netherview_block(v->p, id, x, y, z, NULL);
}

int neth_vec_nether_portal(struct neth_vec *v, int id, int x, int y, int z)
{
    if (v == NULL || id < 0 || id >= v->n) return -1;
    int *p = v->e[id].nt_xportal;
    p[0] = 1, p[1] = x, p[2] = y, p[3] = z;
    return 0;
}

int neth_vec_nether(const struct neth_vec *v, int id, struct neth_vec_nether *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL) return -1;
    const struct nt_env *n = &v->e[id].nt;
    memset(out, 0, sizeof *out);
    out->arrive = n->t_arrive, out->found = n->t_found, out->spawner = n->t_spawner;
    out->rod1 = n->t_rod1, out->rods7 = n->t_rods7, out->home = n->t_home;
    out->rods_max = n->rods_max, out->deaths = n->deaths, out->phase = n->phase;
    out->rods = n->rods;
    for (int k = 0; k < NC_KINDS; ++k) out->cause[k] = n->cause[k];
    out->portal[0] = n->has_portal, out->portal[1] = n->px, out->portal[2] = n->py, out->portal[3] = n->pz;
    out->kills = n->kills;
    out->dim = v->e[id].last.dim;
    out->pos[0] = v->e[id].last.x, out->pos[1] = v->e[id].last.y, out->pos[2] = v->e[id].last.z;
    out->death_t = n->t_death;
    for (int k = 0; k < 3; ++k) out->death_pos[k] = n->death_pos[k];
    out->fort[0] = n->has_fort, out->fort[1] = n->fx, out->fort[2] = n->fy, out->fort[3] = n->fz;
    out->sp[0] = n->has_sp, out->sp[1] = n->sx, out->sp[2] = n->sy, out->sp[3] = n->sz;
    out->loc[0] = n->has_loc, out->loc[1] = n->loc_x, out->loc[2] = n->loc_z;
    out->loc_d0 = (float)n->loc_d0;
    out->entry[0] = n->has_entry, out->entry[1] = n->entry_x, out->entry[2] = n->entry_y;
    out->entry[3] = n->entry_z, out->entry[4] = n->entry_kind;
    out->free_slots = n->free_slots;
    out->hp = v->e[id].last.health;
    out->cobble = n->cobble;
    out->mode = n->mode;
    out->scan_ns = n->scan_ns, out->nsteps = n->nsteps;
    out->deflects = n->deflects, out->deflect_hits = n->deflect_hits, out->ghast_seen = n->ghast_seen;
    out->covers = n->covers, out->hoods = n->hoods;
    out->goal[0] = n->has_goal, out->goal[1] = n->gx, out->goal[2] = n->gy, out->goal[3] = n->gz;
    out->goal[4] = n->glava;
    out->fortin = n->t_fortin, out->bseen = n->t_bseen, out->bhit = n->t_bhit, out->bkill = n->t_bkill;   /* lane/rod */
    out->bhits = n->bhits, out->bkills = n->bkills, out->bstat = n->bstat;
    out->st_drive = n->st_drive, out->st_off = n->st_off, out->st_esc = n->st_esc, out->st_screen = n->st_screen;
    out->death_ground[0] = n->dgx, out->death_ground[1] = n->dgy, out->death_ground[2] = n->dgz;
    out->opaths = n->opaths, out->opaths_ok = n->opaths_ok, out->or_lock_steps = n->or_lock_steps;   /* lane/rod */
    out->home_rods = n->home_rods, out->home_hp = n->home_hp;
    {
        const struct vec_last *l = &v->e[id].last;
        int d = -1;
        if (n->has_portal && l->dim == -1)
            d = abs((int)floor(l->x) - n->px) + abs((int)floor(l->y + 0.13) - n->py) + abs((int)floor(l->z) - n->pz);
        out->portal_d = d, out->portal_dmin = n->pdmin > 0 ? n->pdmin - 1 : -1;
    }
    out->dig_t = n->dn_t > 0 ? n->dn_t : -1;   /* lane/nleg */
    out->dig[0] = n->dn_x, out->dig[1] = n->dn_y, out->dig[2] = n->dn_z;
    out->dig_b = n->dn_b, out->dig_lava = n->dn_lava, out->dig_seen = n->dn_seen;
    out->refills = n->refills;
    out->camp_age = n->camp_age;
    {
        /* lane/rods (books only) */
        const struct vec_env *ve = &v->e[id];
        out->fid = ve->fid, out->fkind = ve->fid ? n->fight_kind : -1, out->hood = n->hood, out->fstuck = n->fstuck;
        out->doing = n->bk_doing | (ve->esc != 0) << 9;
        out->rc = n->bk_rc, out->hold = n->bk_hold, out->press = n->bk_press;
        out->bseen_n = n->bk_bn, out->bseen_d = n->bk_bd, out->drive_t = n->bk_t;
        out->pick_age = n->pick ? n->pick_age : -1;
        out->hood_at[0] = n->hx, out->hood_at[1] = n->hy, out->hood_at[2] = n->hz, out->hood_k = n->hood_k;
        if (n->pick) out->pick_at[0] = n->rx, out->pick_at[1] = n->ry, out->pick_at[2] = n->rz;
    }
    if (v->e[id].last.dim == -1)
    {
        /* lane/nleg: PRIVILEGED, books only (the trace's question: is the spawner capped by blazes the camp cannot see) */
        struct netherview_blaze bz[64];
        const struct vec_last *l = &v->e[id].last;
        int nb = netherview_oracle_blazes(v->p, id, l->x, l->y + 1.62, l->z, 16.0, bz, 64);
        out->blz16 = nb < 0 ? -1 : nb;
        if (n->has_sp)
        {
            int nb2 = netherview_oracle_blazes(v->p, id, n->sx + 0.5, n->sy + 0.5, n->sz + 0.5, 16.0, bz, 64), c = 0;
            int up = 0;
            double sdy = 0.0, shd = 0.0;
            for (int k = 0; k < nb2 && k < 64; ++k)
            {
                int in = bz[k].x >= n->sx - 8 && bz[k].x <= n->sx + 9 && bz[k].y >= n->sy - 4 && bz[k].y <= n->sy + 5 &&
                         bz[k].z >= n->sz - 8 && bz[k].z <= n->sz + 9;
                if (!in) continue;
                c += 1;
                up += bz[k].y > n->sy + 1;
                sdy += bz[k].y - n->sy;
                shd += py_hypot2(bz[k].x - (n->sx + 0.5), bz[k].z - (n->sz + 0.5));
            }
            out->blzsp = c;
            out->blz_up = up;
            out->blz_dy10 = c ? (int)lround(10.0 * sdy / c) : 0;
            out->blz_hd10 = c ? (int)lround(10.0 * shd / c) : 0;
        }
        out->blz_los = netherview_exit_blaze_los(v->p, id, l->x, l->y + 1.62, l->z);
        out->lk_d = -1.0f, out->lk_dy = 0.0f, out->lk_los = -1;
        if (v->e[id].fid && n->fight_kind == HK_BLAZE &&
            !netherview_blaze_lock(v->p, id, v->e[id].fid, l->x, l->y + 1.62, l->z, &out->lk_d, &out->lk_dy, &out->lk_los))
            out->lk_d = -1.0f, out->lk_dy = 0.0f, out->lk_los = -1;
    }
    out->eye_strikes = n->eye_strikes;
    out->mouth_steps = n->mouth_steps, out->mouths = n->mouths;
    out->cov_here = n->cov_here;
    out->cov_off = v->e[id].steps < n->cov_off ? (int32_t)(n->cov_off - v->e[id].steps) : 0;
    return 0;
}

int neth_vec_nether_items(const struct neth_vec *v, int id, struct netherview_item *out, int max)
{
    if (v == NULL || id < 0 || id >= v->n) return -1;
    return netherview_oracle_items(v->p, id, BLAZE_ROD, out, max);
}

int neth_vec_nether_fire(const struct neth_vec *v, int id, double ex, double ey, double ez, float yaw, float pitch,
                         int *blz, int *blz_los, float *los_d, float *los_dy, int *los_cone, int *los_mouth,
                         int los_cell[3], struct netherview_fireball *out)
{
    *blz = *blz_los = *los_cone = *los_mouth = 0;
    los_cell[0] = los_cell[1] = los_cell[2] = -99;
    *los_d = -1.0f, *los_dy = 0.0f;
    if (v == NULL || id < 0 || id >= v->n) return -1;
    netherview_blaze_lines(v->p, id, ex, ey, ez, NETH_RL_FB_R, yaw, pitch, blz, blz_los, los_d, los_dy, los_cone,
                           los_mouth, los_cell);
    return netherview_fireballs(v->p, id, ex, ey, ez, NETH_RL_FB_R, out, NETH_RL_FB_MAX);
}

int neth_vec_nether_clear(const struct neth_vec *v, int id, double x0, double y0, double z0, double x1, double y1,
                          double z1)
{
    if (v == NULL || id < 0 || id >= v->n) return -1;
    return netherview_clear(v->p, id, x0, y0, z0, x1, y1, z1);
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

/* the Nether's books (neth_vec_nether) with the switch off too, so a case compares like with like; they change no act,
 * reward or scalar. Then (lane/nether) the policy's own attack on a pigman dropped (never hit one) */
static void nether_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if (c->r->dim == -1 || e->nt.arrived) nt_observe(v, e, c->r, s, c->yb);
#ifdef EXIT_TRACE
#ifndef EXIT_TRACE_TO
#define EXIT_TRACE_TO 220
#endif
    if (nt_id(v, e) == 0 && c->r->dim == -1 && inv_count(s, BLAZE_ROD) >= 7 && e->steps <= EXIT_TRACE_TO)
    {
        struct netherview_blaze bz[64];
        int nb = netherview_oracle_blazes(v->p, nt_id(v, e), c->r->x, c->r->y + 1.62, c->r->z, 16.0, bz, 64);
        fprintf(stderr, "EXIT t=%lld kind=%d phase=%d flags=%d hp=%.1f food=%d fire=%d blazes16=%d "
                        "pos=%.2f,%.2f,%.2f hood=%d heal=%d pick=%d portal=%d dep=%d ch=%d\n",
                (long long)e->steps, e->kind, e->nt.phase, c->r->flags, c->r->health, c->r->food, c->r->fire, nb,
                c->r->x, c->r->y, c->r->z, e->nt.hood, e->nt.heal_k, e->nt.pick, e->nt.has_portal,
                e->nt.dep_done, e->chamber_on);
    }
#endif
#ifdef SP_BREAK_TRACE
    struct nt_env *n = &e->nt;
    int spb = -1;
    if (MOPT(MO_NT_SP_BREAK) && c->r->dim == -1 && n->has_sp && n->sp_break_at > 0 &&
        (spb = nt_block(v, e, n->sx, n->sy, n->sz)) >= 0 && spb != 52)
    {
        n->sp_break_at = -(NT_SP_BREAK_MARK + e->steps);
        fprintf(stderr, "SPBREAK_GONE env=%d step=%lld sp=%d,%d,%d blazes16=%d\n", nt_id(v, e),
                (long long)e->steps, n->sx, n->sy, n->sz, nt_sp_break_blazes(v, e, c->r));
    }
    if (n->sp_break_at < -NT_SP_BREAK_MARK && e->steps == -n->sp_break_at - NT_SP_BREAK_MARK + 200)
        fprintf(stderr, "SPBREAK_200 env=%d step=%lld blazes16=%d\n", nt_id(v, e),
                (long long)e->steps, nt_sp_break_blazes(v, e, c->r));
#endif
    if (v->macros & NETH_VEC_NETHER)
    {
        if (c->attack && s->mo_entity && s->mo_ekind == HK_PIGMAN)
        {
            a->hold &= ~(1u << PK_ATTACK);
            a->press[PK_ATTACK] = 0;
            c->attack = 0;
        }
    }
}

static int nether_m_cand(struct mac_step *c) { return (c->v->macros & NETH_VEC_NETHER) && nt_want(c->v, c->e, c->r); }

static int nether_m_start(struct mac_step *c) { return nt_start(c->v, c->e, c->r, c->s); }   /* after eating */

/* lane/rod (nether_lava_back): the lava escape in the Nether walked back along the way's crumbs (the cells the feet stood
 * on, so a known floor): the newest crumb within NT_BACK_K that is not the feet's own cell and has no lava at its feet
 * level, looked at and walked to, a jump only when it is higher; over (1) on the ground 12 steps after the last lava
 * hit (the escape's rule) or after NT_BACK_AGE steps; -1 no crumb to go to (the strip's escape then). From the
 * harvested starts 46 of 94 Nether deaths were at one tunnel cell where lava flowed into the feet: the strip's escape
 * held forward and jump in a 2-high tunnel and moved 0.6 blocks in 30 steps */
#define NT_BACK_K 6
#define NT_BACK_AGE 80
static int nt_lava_back(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, struct neth_rl_act *a,
                        double ey)
{
    struct nt_env *n = &e->nt;
    if (!r->on_ground && e->steps - e->lava_t > 11) return -1;
    if ((e->steps - e->lava_t > 11 && r->on_ground) || e->age > NT_BACK_AGE) return 1;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    for (int k = n->ncrumb - 1; k >= 0 && k >= n->ncrumb - NT_BACK_K; --k)
    {
        int16_t *q = n->crumb[k];
        if (q[0] == fx && q[2] == fz) continue;
        int b0 = nt_block(v, e, q[0], q[1], q[2]), b1 = nt_block(v, e, q[0], q[1] + 1, q[2]);
        if (nt_lava(b0) || nt_lava(b1)) continue;
        double yaw, pitch;
        look_at(r->x, ey, r->z, q[0] + 0.5, ey, q[2] + 0.5, &yaw, &pitch);
        still(a);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = (1u << PK_FORWARD) | (q[1] > fy ? 1u << PK_JUMP : 0u);
        return 0;
    }
    return -1;
}

/* lane/nleg (books only): the cell the crosshair is on while the drive holds attack, its block, and its faces' lava in
 * the client world against the lava nt_block shows (neth_vec_nether's dig; --nether-trace). Reads, never acts */
static void nt_dig_note(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    struct nt_env *n = &e->nt;
    n->dn_t = e->steps, n->dn_x = s->mo_x, n->dn_y = s->mo_y, n->dn_z = s->mo_z;
    n->dn_b = netherview_block(v->p, nt_id(v, e), s->mo_x, s->mo_y, s->mo_z, NULL);
    n->dn_lava = n->dn_seen = 0;
    for (int k = 0; k < 6; ++k)
    {
        int x = s->mo_x + D[k][0], y = s->mo_y + D[k][1], z = s->mo_z + D[k][2];
        int raw = netherview_block(v->p, nt_id(v, e), x, y, z, NULL), seen = nt_block(v, e, x, y, z);
        n->dn_lava |= (raw == 10 || raw == 11) << k;
        n->dn_seen |= (seen == 10 || seen == 11) << k;
    }
}

/* the lava escape's seal (lava beside sealed first), and the Nether's run (its fights inside it) */
static int nether_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    struct vec_env *e = c->e;
    const struct neth_vec *v = c->v;
    if (e->kind == M_SURFACE && e->esc && MOPT(MO_NT_LAVA_BACK) && (v->macros & NETH_VEC_NETHER) && c->r->dim == -1)
    {
        int b = nt_lava_back(c->v, e, c->r, a, c->ey);
        if (b >= 0) return b;
    }
    if (e->kind == M_SURFACE && e->esc && nt_esc_seal(c->v, e, c->r, c->s, a, c->ey)) return 0;
    if (e->kind == M_MINE && e->nt.on)
    {
        int rc = nt_drive(c->v, e, c->r, c->s, a, c->ey);
        if ((a->hold & (1u << PK_ATTACK)) && c->s->mo_hit && c->r->dim == -1) nt_dig_note(c->v, e, c->s);
        return rc;
    }
    return -1;
}

static uint32_t nether_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->nt.on ? NETH_VEC_NETHER : 0; }

static void nether_m_end(struct vec_env *e) { e->nt.on = 0; }

static const struct mac_switch NETHER_SW[] = {{"nether", NETH_VEC_NETHER}, {NULL, 0}};

static const struct vec_macro MAC_NETHER = {
    .name = "nether", .bit = NETH_VEC_NETHER, .sw = NETHER_SW, .observe = nether_m_observe, .cand = nether_m_cand,
    .start = nether_m_start, .drive = nether_m_drive, .label = nether_m_label, .end = nether_m_end, .reset = nt_reset,
};
