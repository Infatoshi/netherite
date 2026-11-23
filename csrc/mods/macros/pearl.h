/* lane/pearl: NETH_VEC_PEARL's per-env state (pearl.c, included by macros.c; struct vec_env holds one) */
#ifndef NETHERITE_RUNTIME_VECENV_PEARL_H
#define NETHERITE_RUNTIME_VECENV_PEARL_H

#include <stdint.h>

#include "../../runtime/rlbind.h"
#include "../../runtime/vecenv.h"

#define PEARL_BAD 8             /* targets given up (a ring) */
#define PEARL_SEEN 64           /* the distinct endermen booked an episode */
#define PEARL_DROPS 8           /* the kills' drops remembered */
#define PEARL_SURF_Y 62         /* lane/pearlgo (pearl_night_first): the feet under this (F3's y; land stands over the
                                 * sea's 62) are not the hunt's ground: the strip's climb out first. At the strip's own
                                 * 56 the hunt walked its tunnels: 290k of 434k hunt steps at y 50 to 56 (out/ev/a3) */

/* lane/pearl2: the census's sums over an episode's steps (measurement only: pearl_check --census; rlbind.h
 * neth_rl_census), the endermen by horizontal distance band (16 blocks a band to 128), and the deaths' context */
#define PEARL_CBANDS 8
#define PEARL_CDEATHS 4
struct pearl_census {
    int64_t samples, alive, pl_sky, hostile[4], skel_los;
    int64_t hides, sealed;      /* the macro's (with the census on or off): hides begun, holes sealed */
    int64_t cells, holds, hold_seen;    /* lane/pearl3: the cave hunt's cells walked into, holds begun (a cave
                                         * opened), holds in which an enderman showed */
    int64_t clear0_64, clear2_64, clear4_64;    /* endermen within 64 with the line clear from the eye, 2 and 4 over it */
    int64_t lost_angry, lost_calm, stare_out, drops, drop_out, picks;   /* targets let go out of view (provoked or
                                                                          * not), stares given up, drops booked, drops
                                                                          * let go unpicked, pearls picked up */
    int64_t n[PEARL_CBANDS], sky[PEARL_CBANDS], tracked[PEARL_CBANDS], clear[PEARL_CBANDS], view[PEARL_CBANDS],
        skyclear[PEARL_CBANDS], angry[PEARL_CBANDS];
    int64_t below[PEARL_CBANDS], level[PEARL_CBANDS], above[PEARL_CBANDS];  /* the box middle's height against the
                                                                              * eye: under -6, within 6, over 6 */
    int64_t biome_steps[64], biome_view[64], biome_sky64[64];   /* steps by the player's biome (id & 63), endermen in
                                                                  * view and under the sky within 64 in those steps */
    int32_t ndeath;
    int32_t biome0;             /* lane/pearl3: the player's biome at the episode's first sample (the spawn's) */
    int64_t ender_y[16];        /* lane/pearl3: endermen within 64 (horizontally) by their feet's y, 8 blocks a bin */
    int64_t pl_y[16];           /* lane/pearl3: the player's feet y, 8 blocks a bin, a step */
    int64_t view_y[16];         /* lane/pearl3: endermen in view by the player's feet y bin */
    struct {
        int32_t te, cause, kind, on, phase, up, sky, hp10; /* hp10: health ten steps before */
        int32_t hostile[4], skel_los, y;
    } death[PEARL_CDEATHS];
};
int neth_vec_pearl_census(struct neth_vec *v, int on, int id, struct pearl_census *out);
/* lane/pearl3: the macro's settings, one build for every variant a case compares (pearl_check --tune NAME=VALUE):
 * zombie_pillar (1: a zombie is fought from a pillar; 0 lane/pearl2's ground fight), drop_calm (1: the drops are
 * fetched once no screaming enderman has been in view within 32 for the quiet; 0 lane/pearl2's: none in view at all),
 * cave_y (the cave hunt's tunnel height), cave_back (cells backed into the tunnel), cave_hold (steps a quiet hold is
 * kept). Defaults: pearl.c's PEARL_* constants. 0, or -1 on a name it does not know */
struct pearl_tune { int zombie_pillar, drop_calm, cave_y, cave_back, cave_hold; };
void pearl_tune_default(struct pearl_tune *t);
int neth_vec_pearl_tune(struct neth_vec *v, const char *name, int value);
/* pearl_check --trace's: the last drive's hide stage, crosshair (hit, block, x, y, z), the feet cell's y, the hold */
int neth_vec_pearl_dbg(const struct neth_vec *v, int id, int32_t out[8]);

struct pearl_env {
    int on;                     /* the macro running (the mine macro's kind, as the seek and the strip) */
    int phase, pstep;           /* PP_*, the steps in it */
    int64_t clock;              /* the macro's own steps (the hits' spacing) */
    int tid;                    /* the target enderman's client id, 0 none */
    float tx, ty, tz, th;       /* its last place in view, its height */
    double tdist;               /* the eye's distance to its middle when last in view */
    int tlost, tstare, tangry;  /* steps out of view, steps stared at, it screamed */
    int64_t thit;               /* the clock of the last hit */
    double base;                /* the ground's height under the pillar (NAN: none) */
    int placed, pwait, pbefore; /* blocks placed; a tower jump's stage; cobblestone held before it */
    double jbase, jpeak;        /* the jump's feet height, the highest it got */
    int upfail;                 /* jumps that came down short at this pillar */
    int64_t up_off, turn_off;   /* no pillar before this clock (one failed here); no turn at water before this */
    int tfar;                   /* steps an angry target has stayed farther than 16 blocks */
    float heading;              /* the walk's yaw */
    int heading_set;
    double px, pz;              /* the position a step ago */
    int stuck;
    int kills, kills_prev;      /* stat.mobKills after the last step and the one before (the books') */
    int64_t kill_te;            /* the episode step it last moved */
    struct { float x, y, z; int64_t te; } drop[PEARL_DROPS];    /* the kills' drops to pick up, oldest first */
    int ndrop;
    int64_t ender_te;           /* the episode step an enderman was last in view */
    float fx, fz;               /* the creeper fled */
    int bad[PEARL_BAD], nbad;
    int seen_ids[PEARL_SEEN], nseen;
    int fresh, held;            /* the books' first step; pearls and eyes held last step */
    int32_t killed_by[NETH_RL_EGGS];
    int lair, lfire, lfood;     /* the last step's air, burning and food (a death's cause) */
    int mo;                     /* the crosshair's entity at the last drive (the trace's) */
    int kb_egg;                 /* the last killed-by stat moved (an egg) and when */
    int64_t kb_te;
    struct neth_rl_far far;        /* this step's view (the step hook's copy of neth_rl_far) */
    int turn_left;              /* lane/pearlsee: pearl_look turn's steps left of the turn, and the clock of the next */
    int64_t turn_next;
    int towers_n;               /* lane/pearlsee: pearl_tower's towers topped out this episode */
    int tower_tries;            /* lane/pearlsee: its tries begun from the walk this episode */
    int surfaced;               /* lane/pearlsee: the hunt has stood on the land (y PEARL_SURF_Y) this episode */
    int64_t dip_off;            /* lane/pearlsee: pearl_dip's next turn (the clock) */
    int64_t rs_at, rs_off;      /* lane/pearlsee (3rd pass): pearl_restock's E press (the clock, 0 none); none before */
    int o_dig;                  /* lane/pearlsee (3rd pass): pearl_oracle's dig through what stops the walk: steps left */
    int o_goal;                 /* pearl_oracle's pillar's blocks (0: PEARL_TALL) */
    struct walkto_st wt;        /* pearl_oracle's walk (mods/macros/walkto.c walk_to, PRIVILEGED) */
    float o_bad[4][3];          /* pearl_oracle: where targets with no path stood (skipped while marked) */
    int64_t o_bad_t[4];         /* each mark's clock (it expires after PEARL_ORACLE_BAD_T) */
    int o_badn;                 /* marks made (the next one is o_badn % 4) */
    int64_t o_sky_off;          /* pearl_oracle: the clock before which no look up for a roof (nothing to dig last) */
    int64_t o_item_t;           /* pearl_oracle: the clock its walk to a pearl item began (0 none in the census) */
    int o_wk;                   /* lane/pearlhome (books): the item walk's last answer (walk_to's -1, 0, 1; 2 its time
                                 * over), as of the step */
    int64_t o_wk_t;             /* the episode step (e->te) it was set */
    int it_n;                   /* the census's pearl items at the last step (it_new's rise) */    int upwhy, upend;           /* lane/pearlsee: the UP phase's end being booked (pearl.c UX_*), it was booked */
    float hill[16];             /* lane/pearlsee: pearl_high's memory, by yaw sector: the highest ground face seen (y) */
    int64_t hill_at[16];        /* the clock it was seen */
    int high_steps;             /* pearl_high's walk steps toward the high ground this try */
    int high_look;              /* its level turn's steps this try */
    float high_y0;              /* the feet's y as its walk began */
    int fetch_held;            /* lane/pearlsee: pearls and eyes held as the drop's walk began */
    int64_t prov_te;            /* lane/pearlsee: the episode step a screaming one was last in view (any distance) */
    int dig_arm;              /* lane/pearlsee: the next PP_UP is a dug tower */
    int dig, dig_open, open_placed; /* lane/pearlsee: pearl_sky's tower dug up through a roof; the sky seen open over
                                     * it; the blocks placed when it was (the tower's PEARL_TOWER counts from there) */
    struct neth_vec_pearl book, last;   /* this episode's books, the last finished episode's */
    int32_t pv_tid, pv_killed;  /* lane/chain: the provoked target held at the step before (0 none), the kills then */
    /* lane/pearl2: the view's reach by yaw sector (rlbind.h neth_rl_far depth: what the frame showed), -1 unseen */
    float open[16];
    int64_t open_at[16];        /* the clock it was seen */
    int8_t wet[16];             /* the sector's ground ahead last seen water or lava */
    int64_t rehead;             /* the walk's next heading choice (the clock) */
    int walk_open;              /* the walk's steps since the view was last long */
    /* lane/pearl2: the hide (PP_HIDE): a 3-deep hole sealed with one block over the head, held to heal */
    int hs;                     /* its stage (pearl.c HS_*), 0 none */
    double hy0;                 /* the feet's height where it started (the ground) */
    int hcover_try, climb;      /* the cover's cardinal tried; the climb out (PP_UP back to the ground) */
    int64_t hide_off;           /* no hide before this clock (one failed) */
    int32_t dbg[8];             /* neth_vec_pearl_dbg's */
    /* lane/pearl2: the hurt response (a hit from out of view: turned to, the pillar left) */
    float hp_last;              /* health at the last drive */
    int look_back;              /* steps left of the look back */
    float hurt_yaw;             /* the yaw the hit came from (the knockback's reverse) */
    int64_t hurt_clock;         /* the clock of the last hit from out of view */
    int64_t hit_clock;          /* the clock of the last hit of any kind */
    int pillar_hurts;           /* hits from out of view on this pillar */
    float flee_turn;            /* the flee's turn from water (degrees, reset with the phase) */
    struct pearl_census cs, cs_last;    /* lane/pearl2: the census's (this episode's, the last finished one's) */
    struct neth_rl_census cprev;           /* the step before's census (a death step's may be the respawn's) */
    float hp_ring[16];                  /* health, the last 16 steps */
    /* lane/pearl3: the cave hunt (NETH_VEC_PEARLCAVE): the dig's cardinal (0 +z, 1 -x, 2 -z, 3 +x), the cell set being
     * cleared (its feet cell, the set's stage, the clock its look was set, the clock of the last progress), the
     * floor's hazards in a row; the hold's cell (its feet cell), whether one is set, the yaw it looks out along, its
     * clock, cells to back up into the tunnel, the clock a hold may start again */
    int cdir, ccx, ccy, ccz, ck, chaz, cdug;   /* cdug: the cell being judged was dug (not found clear) */
    int64_t clook, cgain;
    int hold_set, hx, hy, hz, hback;
    float hyaw;
    int64_t hold_te, hold_off, cave_ender_te;
    int tunnel_cells;                   /* cells walked into since the last hold (the branch's length) */
    int cfloor, cfloor_set;             /* the tunnel's feet height (PEARL_CAVE_Y, or where the staircase stopped) */
    int cwalk;                          /* lane/pearl3: the staircase's walk on (steps left) */
    int zombie_up;                     /* lane/pearl3: the pillar going up for a zombie (PEARL_PILLAR high is enough) */
    int64_t angry_te;                   /* lane/pearl3: the episode step a screaming one was last in view within 32 */
    int64_t fight_te;                   /* lane/pearlgate: the episode step NETH_VEC_FIGHT's lock last drove (the
                                         * policy's fight; pearl_yield) */
    /* lane/pearlhome (pearl_home, PRIVILEGED): the travel leg's goal (the stronghold's room middle; ph_ok 0 not asked,
     * 1 found, -1 none), the last land check (its step and answer), the leg's own way-finding failures */
    int ph_ok;
    double ph_x, ph_z;
    int64_t ph_land_t;
    int ph_land;
    int64_t ph_straight;                /* straight-walk steps left (no way found: swum or walked straight at it) */
    int ph_leg, ph_lx, ph_ly, ph_lz;    /* the leg's target cell (sho_follow's), set */
    int ph_wet_n;                       /* steps in a row with the feet in water (the books') */
    int ph_dg_set, ph_dg[3];            /* pearl_target any: the dig's goal cell */
    int ph_sky_seen;                    /* stood on the ground under the sky at the land's height this episode */
    float prev_x, prev_y, prev_z;       /* the feet at the step before (a death's place) */
#ifdef CHAIN3_THAW_COMPAT
    int thaw_prefix_pad;                /* old pearl_env ended with four padding bytes: keep new fields past it */
    int thaw_side, thaw_sx, thaw_sz;    /* appended controller fields; zero with the option off */
#endif
    /* lane/pnight (past the old end: saved layouts read as an appended suffix; counting only, neth_vec_pearl_aim): the
     * episode's books: [0] in-reach steps given to the path (pearl_reach_aim), [1] pearl swings pressed with no
     * enderman under the crosshair, [2] all pearl swings pressed in reach */
    int32_t aim_book[3];
    /* lane/pnight (counting only, neth_vec_pearl_gate): the hunt's PG_WHY_POLICY steps by what took them: [0] provision
     * (provision_mode on), [1] the hunt not a candidate, [2] a candidate with no macro on the step */
    int32_t gate_book[3];
    /* lane/pnight (pearl_reach_aim): in-reach steps since an enderman was last under the crosshair */
    int reach_blind;
    /* lane/pnight (counting only): pearl_drive's target kills (its two tests: what the book's killed counted before it
     * took the stats') and their count at the step before (prov_end's kill) */
    int32_t tkill, pv_tk;
    /* lane/pdig (P2, pearl_patrol): the turn from a refused cell ahead (its yaw offset and the clock it ends), and the
     * books: [0] patrol steps, [1] open, [2] step up, [3] dug, [4] drop walked, [5] turned (drop, lava, deep water),
     * [6] the sky dig begun, [7] under a roof with no way up */
    float pt_off;
    int32_t pt_pad;
    int64_t pt_until;
    int32_t pt_book[8];
    /* lane/pground (pearl_march): the line (its middle and yaw, set at the episode's first march step), the end walked
     * to (0 the yaw's, 1 the other), the progress check (the distance to the end and the clock at the last one), and the
     * books: [0] march steps, [1] ends reached, [2] sky digs begun, [3] ends turned from for no progress */
    int32_t pm_set, pm_leg;
    float pm_h;
    float pm_d;
    double pm_ax, pm_az;
    int64_t pm_t;
    int32_t pm_book[4];
};

#endif
