/* lane/dragon (vecenv.h NETH_VEC_DRAGON): the End fight as a macro of the vector env, from the arrival on the
 * obsidian platform to the exit portal walked into. vecenv.c owns the env and calls these; this file owns what the
 * macro keeps and what it does. Everything it reads is the player's: its own position, look, health and inventory
 * (the F3 screen and the hotbar), the crosshair, and the End in view (endview.h: crystals, the dragon, endermen,
 * never through a wall); what it remembers is what it saw. */
#ifndef NETHERITE_RUNTIME_VECDRAGON_H
#define NETHERITE_RUNTIME_VECDRAGON_H

#include <stdint.h>

#include "endview.h"
#include "rlbind.h"

/* the player as the act phase reads it (vecenv.c's vec_last) */
struct dragon_in {
    double x, y, z;
    float yaw, pitch, health;
    int32_t dim, dead, screen, on_ground, food;
    int64_t steps;              /* the env's decision steps this episode */
    float nudge[2];             /* the policy's own look change this step (yaw, pitch degrees): the search's turn */
    int32_t lob, crit, block;   /* lane/macroopts: the run file's dragon_lob, dragon_crit, dragon_block (vecenv.c MOPTS) */
    /* lane/shold, PRIVILEGED (dragon_oracle crystals; dragon.c's dg_raid_pick fills it when the macro asks, from every
     * crystal copy the client holds and the server's End blocks): the crystal to take and the tower's cell beside its
     * pillar. oracle the option; rc_ok a pick made this step (0 none: no crystal left that a tower reaches) */
    int32_t oracle, rc_ok, rc_id;
    int32_t snow;               /* lane/shold: dragon_crystals snowball (the crystals' shots thrown snowballs) */
    int32_t food_fetch;         /* lane/chain2: dragon_food fetch (food in the main inventory swapped into the hotbar to eat) */
    int32_t sword_stock;        /* lane/chain2: fetch a spare sword when the hotbar's one breaks */
    int32_t hunt_opts;          /* lane/legs: bit 0 dragon_hunt_cap on (the hunt ends at DG_HUNTS on the crystal); bits
                                 * 8-15 dragon_snow_tower's cap on the hunt stand's tower blocks (0: DG_SNOW_TOWERS); lane/dleg: bit 1
                                 * dragon_clutch water, bit 2 dragon_hole_floor on, bit 3 no block two under the feet,
                                 * bit 4 the ground found (bits 16-23 its y), bit 5 dragon_follow server, bit 6
                                 * dragon_wait centre (bit 7 band, bits 29-31 its eighth), bits 24-26 dragon_late's nohunt, nopit, walkin, bits 27-28 dragon_pit once,
                                 * stand. In
                                 * the alignment gap before rc_x: the struct's size and every saved layout unchanged */
    double rc_x, rc_y, rc_z;    /* the crystal's position (the blast's point: the top of its pillar's bedrock cap) */
    int32_t rc_sx, rc_sz;       /* the tower's cell */
    int32_t rc_top;             /* the tower's top: the feet's cell at the swing (the cap's cell less 1) */
    int32_t rc_ground;          /* the feet's cell on the ground of the tower's column */
};

/* the macro's memory, per env (zeroed at an episode's reset) */
struct vec_dragon {
    int on;                     /* the macro running */
    int phase, age;             /* DP_*, the steps in it */
    int sub, sub_age;           /* a phase's own step (the climb's dig and tower; the shot's draw and wait) */
    int target, tries;          /* the crystal shot at (its id), shots at it from here */
    float tx, ty, tz;           /* where it was seen */
    int64_t shot_at;            /* the step the last arrow left */
    int crystals_before;        /* crystals in view at the release */
    int64_t lastswing;
    int sweep;                  /* the search's look: steps into a turn */
    double gx, gz;              /* the walk's goal */
    double px, pz, pmove;       /* the position a step ago; steps without moving */
    float dx, dz;               /* the dragon's last place in view (its death's: the exit portal under it) */
    int saw_death, death_gone;  /* its death seen; its copy gone since (the portal made) */
    int64_t dead_step;
    struct { int32_t id; int64_t until; } skip[8];   /* crystals given up on (out of reach from here) until */
    int nskip;
    int eating, efood;          /* the eat's steps, the food level at its start */
    int fetch_pressed, fetch_id, fetches;   /* a fetch to the hotbar: the inventory key pressed, the item, how many */
    int wp;                     /* the walk's waypoint (0 the centre, then a ring around it) */
    int tw, tw_age, towers;     /* a tower step (1 the jump, 2 placed), its age, the walk's towers to this waypoint */
    double tw_base;             /* the feet's height at the jump */
    double gbest;               /* the walk's nearest to its waypoint yet (0: none) */
    int pitted, pit_dug;        /* standing in the pit dug (the dragon's push cannot carry the player off); blocks dug */
    double ax, az;              /* where the macro began (the arrival) */
    int pit_y;                  /* the feet's cell when the pit's dig began */
    int64_t dragon_seen;        /* the step the dragon was last in view */
    int saw_c, vtowers;         /* a crystal in view in this look around; the towers made for the view */
    int snipe, stowers;         /* the snipe from a tower (1 on, -1 done), its tower's blocks */
    double sbase;               /* the feet's height where the snipe's tower began */
    int rs, rs_age;             /* the walk's rise (1: the look up, 2: digging over the head, 3: the tower step) */
    int ret_phase;              /* the phase an eat interrupted (taken up again after it) */
    float vx, vy, vz;           /* the dragon's head's motion a step, from two steps in view */
    int64_t pseen;              /* the step of the head's last place (pdx, pdy, pdz) */
    float hp_before;            /* the dragon's health at an arrow's release at it */
    int dragon_shots, dragon_hits;   /* arrows at the dragon, and the ones its health fell after */
    int64_t walk_until;         /* the walk's end (then a look around) */
    int swings;                 /* sword swings at the dragon */
    float hp_seen;              /* the dragon's health when last in view (the boss bar) */
    int healed, resnipes;       /* its health seen rising since (a crystal left somewhere); the snipes it started */
    int snipe_hits;             /* crystals_hit at a snipe's start */
    float pdx, pdy, pdz;        /* the dragon's head a step ago (its motion, for an arrow's lead) */
    /* the books (the case's and the milestones) */
    int64_t kill_step, exit_step, first_shot;
    int crystals_start, crystals_hit, shots;
    float dragon_min;
    int was_dead;
    int died[4];                /* deaths in the End by what was near, first that holds: the void (below y 0), the
                                 * dragon (its head within 10 blocks), an enderman (one within 4, seen or not: the
                                 * books' endview ender_near, never the macro's), anything else */
    int64_t death_step;         /* the episode's step of the first death in the End (-1 none) */
    float arrow_dmg, sword_dmg, swing_hp;   /* lane/dragon2's books: the dragon's health its arrows' and its swings' hits
                                             * took; its health at the last swing */
    int sword_hits;
    int mo_block, mo_dy;        /* lane/dragon2's books: the crosshair's block (-1 none) and its height over the feet */
    int blk, blk_hot;         /* lane/dragon2's books: tower blocks held, the hotbar's slot of one (-1) */
    int arrows;           /* lane/dragon2's books: arrows held at the last step the macro drove */
    int col_found, col_age;     /* lane/dragon2: the exit portal's column seen; the steps looking for it */
    int use_prev;              /* lane/dragon2: the last act held the use (a block or a draw) */
    int lost_age;             /* lane/dragon2: steps the drawn bow's dragon has been out of view */
    double py;               /* lane/dragon2: the feet's height a step ago (the crit's fall) */
    double rise_best;         /* lane/dragon2: the walk's distance to its waypoint at the first rise of a run */
    int64_t detour_until;       /* lane/dragon2: the walk turned aside (an obstacle past DG_WALK_RISE) until */
    int detour_side;
    struct { int32_t id; float x, y, z; int hunts, shots; } cm[NETH_RL_END_CRYSTALS];   /* lane/dragon2: the crystals
                                 * seen and not yet seen to blow up (where seen, the hunts made and arrows shot at them) */
    int ncm, hunt;              /* how many; the hunt's crystal (its id) */
    int lob, hunt_ret;          /* lane/dragon3: the shot is a lob (the high arc at the place seen); the shot came from
                                 * the hunt (taken up again after it) */
    int lobs, lob_hits;         /* lane/dragon3's books: lobs let go, and the ones whose crystal went */
    int picked, arrows_prev;    /* lane/dragon3's books: arrows picked up (the count rising), the count a step ago */
    float heal_sum;             /* lane/dragon3's books: the boss bar's rises between sightings, summed (its healing) */
    int exit_unstuck;           /* lane/dragon3: the exit's walks out from under the portal's floor, and its retries */
    int exit_try;               /* lane/dragon3: the exit's target cell round the column (of eight) */
    /* lane/dragon2's books: every hit and the death by what the server's CombatTracker names (DK_*), the wing push
     * seen as the feet's horizontal step past DG_PUSH_V */
    float prev_hp;              /* the server's health a step ago (0 not yet) */
    double bx, bz;              /* the feet a step ago (the books') */
    int64_t push_step;          /* the last step the push was seen (0 none) */
    int pushes;
    float dmg[9];               /* health lost in the End by DK_* */
    int hits[9];
    /* the End's death: 1 + its DK_* (0 none), pushed within DG_PUSH_WINDOW steps, the macro's phase (0 off), the
     * tracker's fields, the food level; where, the fall's length, the dragon's head's distance (-1 no copy) */
    int d_cause, d_pushed, d_phase, d_fall_dt, d_fall_name, d_dt, d_name, d_food;
    float d_x, d_y, d_z, d_fall_len, d_head;
    /* lane/shold (dragon_oracle crystals): the raid on the crystal rc (the pick dragon_in brought), its step (DR_*),
     * a pick asked for (the mod fills dragon_in's at the next step), the crystals given up on, the books */
    int raid_ask, raid_have;    /* a pick asked; one brought (rc_* below) and not yet begun */
    int64_t raid_none_at;       /* the step the last ask found none (asked again DG_RAID_REASK later) */
    int32_t rc_id, rc_sx, rc_sz, rc_top, rc_ground;
    double rc_x, rc_y, rc_z;
    int raid_skip[NETH_RL_END_CRYSTALS], nraid_skip;
    int raids, raid_hits, raid_gave;   /* raids begun, crystals gone during one, raids given up */
    int htower;                 /* lane/shold (dragon_crystals snowball): blocks the hunt's stand rose (0 none) */
    double hbase;               /* ... the feet's height where it began */
    /* lane/dleg (dragon_clutch water): the highest feet since the ground (fpk_ok 0: not read yet), the catch (1 the
     * fall, 2 the water to take up again), its steps, the use pressed in this fall; the books: catches pressed, water
     * taken up again */
    double fpeak;
    int fpk_ok, clutch, clutch_age, clutch_pressed;
    int cl_x, cl_y, cl_z;       /* the placed source's cell */
    int clutches, clutch_ups;
    int cliff_steps;            /* lane/dleg (dragon_bridge cliff): the steps the cliff's side blocks took */
    int cliff_x, cliff_z;       /* ... the bridge cell they go beside */
    int hole_ok, hole_x, hole_y, hole_z;   /* lane/dleg (dragon_pit once): the last hole's cell and floor (the feet's) */
    int band_ok, band_k;        /* lane/dleg (dragon_wait band): the island's widest eighth read (dragon.c), and which */
    float te_lhp;               /* lane/dleg (threat_end ender, fight.c): the health a step ago (0: not read yet) */
    int64_t te_hurt;            /* ... the step of the last hit (te_ok: one seen) */
    int te_ok, te_locks;        /* ...; the books: locks started */
    int32_t av_id[NETH_RL_END_ENDERS];   /* lane/dleg (dragon_ender_avert all, dragon.c): each enderman in view last step */
    int av_run[NETH_RL_END_ENDERS], av_n;  /* ... the steps running its stare's cone held the look; how many */
    int av_breaks;              /* ... the books: stares broken */
};

/* the books' causes of a hit or a death (dragon_books, from endview's CombatTracker fields): the void, the dragon's
 * head, an enderman, a fall after a hit by the dragon (death.fell.assist/finish), any other fall, a blast (the crystals),
 * an arrow, hunger, anything else */
enum { DK_VOID, DK_DRAGON, DK_ENDER, DK_FALL_HIT, DK_FALL, DK_BLAST, DK_ARROW, DK_STARVE, DK_OTHER, DK_N };

/* the macro's phases */
enum { DP_CLIMB = 1, DP_SEEK, DP_SHOOT, DP_FIGHT, DP_EXIT, DP_EAT, DP_FETCH, DP_PIT, DP_SNIPE, DP_DESCEND, DP_HUNT,
       DP_FOLLOW, DP_RAID };

/* 1 when the End's step is the one the run needs: in the End, alive, no screen, a sword or a bow with arrows held */
int dragon_want(const struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                const struct neth_rl_end *en);
void dragon_begin(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                  const struct neth_rl_end *en);
/* one step's act into a (the vector env's act_noop'd act): 1 when over (out of the End, dead, a screen) */
int dragon_drive(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                 const struct neth_rl_end *en, struct neth_rl_act *a);
/* after every step (the macro on or off): the books */
void dragon_books(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_end *en, int64_t te);

#endif
