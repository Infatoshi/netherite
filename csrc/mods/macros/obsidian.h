/* lane/obsidian: NETH_VEC_OBSIDIAN's per-env state (obsidian.c, included by macros.c; struct vec_env holds
 * one) */
#ifndef NETHERITE_RUNTIME_VECENV_OBSIDIAN_H
#define NETHERITE_RUNTIME_VECENV_OBSIDIAN_H

#include <stdint.h>

#include "../../runtime/obsview.h"

#define OB_BAD 16               /* the targets given up, kept for a while (a ring) */
#define OB_MEM 8                /* lane/obsidian2: the water and lava sources remembered a kind (a ring) */

/* lane/obsidian2: who drove the step a book is made of (vecenv.h neth_vec_obsidian's who) */
enum { OW_POLICY, OW_MINE, OW_FILL, OW_CAST, OW_DOUSE, OW_CLIMB, OW_GO, OW_LOOK, OW_SEARCH, OW_STRIP, OW_ESC, OW_SEEK,
       OW_SURF, OW_CRAFT, OW_OTHER, OW_BPORTAL, OW_KINDS };     /* OW_BPORTAL: lane/obsidian3's macro */
/* OW_MINE + a job (obsidian.c OJ_*) is the job's who */
/* what hurt last (the cause booked at a death) */
enum { OC_LAVA, OC_FIRE, OC_FALL, OC_MOB, OC_DROWN, OC_STARVE, OC_OTHER, OC_KINDS };

/* lane/obsid: the link's funnel after the kit: the head under the sky, a water source in view with an empty bucket, a
 * water bucket held, a lava source in view with a water bucket held, a cast begun, obsidian held, ten held, the Nether */
enum { OF_SKY, OF_WATER, OF_FILL, OF_LAVA, OF_CAST, OF_OBS1, OF_OBS10, OF_NETHER, OF_KINDS };

/* lane/obsidian2: the episode's books (read by neth_vec_obsidian), with the switch off too */
struct obs_book {
    int64_t nether;             /* the step the Nether was first entered (-1 not yet) */
    int64_t kit;                /* the step the kit was first held (-1) */
    int32_t obs_max, mined, obs_now;   /* obsidian held at most, stat obsidian mined, held now */
    int32_t jobs[OW_KINDS];     /* jobs started by kind (OW_MINE..OW_GO) */
    int32_t steps[OW_KINDS];    /* the steps each drove while the kit was held */
    int32_t deaths;
    int32_t cause[OC_KINDS];    /* deaths by what hurt last */
    int32_t d_cause, d_who;     /* the first death's cause and who drove the step before it */
    int64_t d_step;             /* its step (-1 none) */
    float d_pos[3];             /* where */
    int32_t d_kit;              /* the kit held then */
    int32_t cause_last, who_last, was_dead, lground;
    float lhp, fall_top;
    int64_t lava_at;            /* the step the feet or head were last in lava */
    int64_t burn_at;            /* the step the flames last began */
    int32_t lfire, who_hurt;    /* burning last step; who drove the step a lava incident began */
    /* lane/obsid: the water carried: water buckets filled (the count rose, any macro or the policy), the first step one
     * was held (-1 never), the steps one was held, the count last step */
    int32_t wb_fills, wb_last;
    int64_t wb_first, wb_steps;
    /* lane/obsid: the link's funnel, the first step after the kit was held of each (-1 not yet): OF_* */
    int64_t fn[OF_KINDS];
    /* lane/obsid2: the water census at the first sky after the kit (OSW_*, -1 not reached), the kept surface water's
     * distance across then (-1 none kept) */
    int32_t sky_why;
    float sky_d;
};
/* lane/obsid2: the water trip at the first sky after the kit: due, a water bucket already held, no empty bucket, no
 * surface water kept, kept but over 128 across, over 8 up or down, night */
enum { OSW_DUE, OSW_HELD, OSW_NOBUCKET, OSW_NONE, OSW_FAR, OSW_UPDOWN, OSW_NIGHT, OSW_KINDS };

struct obs_env {
    int on;                     /* the macro running (the mine macro's kind, as the seek and the strip) */
    int job;                    /* OJ_MINE, OJ_FILL, OJ_CAST */
    int tx, ty, tz;             /* the target cell */
    int phase;                  /* 0 going to it, 1 the use sent (or the dig done), 2 the cast settling */
    int at;                     /* the age the phase began */
    int before;                 /* the count the job watches (obsidian, water buckets) at its start */
    int uses;                   /* uses sent at this target */
    int gain, stuck;            /* the age of the last half-block gain, steps without moving */
    double best, px, pz;        /* the nearest it has been, the position a step ago */
    int64_t scan_at;            /* the step of the last look (obsview_scan), -1 none */
    int64_t calm;               /* no job starts before this step: a douse's water stays poured (the fill had taken it
                                   back beside the lava, and the player burned again: a run's 16 hearts in five rounds) */
    int64_t off;                /* no start before this step (100 after a target given up) */
    struct obsview_hit hit[OV_KINDS];
    struct { int32_t x, y, z; int64_t until; } bad[OB_BAD];
    int nbad;
    int search;                 /* lane/obsidian2: the strip driven by the search this step (ob_search said 1) */
    int64_t hot_at;             /* the step of the last hot look (lava in view at the feet's level or below) */
    int hot;                    /* that look found one ahead: the strip turns */
    int cy;                     /* OJ_CLIMB / OJ_GO: the cell being cleared (0..2), 3 the step */
    int cdir, turns;            /* the climb's direction (the strip's 0..3) and its quarter turns */
    int climb_to;               /* lane/obsidian5: the climb's feet height to stop at (0: OB_SEARCH_Y; the bucket
                                   portal's climb to the sky sets 256 and stops it itself where the sky shows) */
    int bx, by, bz;             /* the stair's base: the feet's cell when it began */
    int lk_set;                 /* OJ_LOOK: a look around done this episode, where (lkx, lkz), its first yaw */
    double lkx, lkz;
    float lk_yaw;
    int remem;                  /* the job's target is a remembered source (the walk digs toward it) */
    int64_t burn_seen;        /* the last lava touch or burn the search's strip turned back from */
    struct { int32_t x, y, z, n; } mem[2][OB_MEM];   /* the water [0] and lava [1] sources seen (n: the ring's next) */
    int nmem[2];
    int64_t mem_at;             /* the step of the last memory look */
    int64_t oracle_at;          /* lane/chain (obsidian_oracle on): the step of the last privileged scan (0: none) */
    int sky;                    /* lane/obsidian7 (obsidian_water sky): the climb is the water trip to the sky */
    int64_t sky_off;            /* no water trip begins before this step */
    int sky_trips, sky_water;   /* the trips begun, and those that ended with water in view */
    int32_t stx, sty, stz;      /* the search's stall rule: the feet's cell, and the step it was first stood in */
    int64_t st_at;
    int stalls;                 /* the search's quarter turns for a stall */
    int drops;                  /* lane/obsidian7 (obsidian_drop on): the search's walks off onto a deeper floor */
    int drop_ph;                /* the deeper look's phase (strip's sk 6: creep, look, drop, back) */
    int steer_turns, steers;    /* obsidian_cast_way strip: the strip's turns at the last look, the steers made */
    int rims;                   /* obsidian_cast_rim on: the pours on a rim */
    int64_t steer_off;          /* no steer before this step (a hazard's turn) */
    int64_t drop_look;          /* the age the phase began */
    /* lane/obsid (obsidian_water scout): the walk over the surface for water (OJ_SCOUT): the spiral's leg, its heading,
     * where it began and the step, the far look's next yaw (-1 walking), the last place moved from and when, the water
     * the far look saw and is walked to (have), the rest after a scout, scouts begun and those that saw water */
    int sc_leg, sc_k, sc_have, sc_n, sc_found;
    double sc_yaw, sc_x, sc_z, sc_mx, sc_mz, sc_lx, sc_lz, sc_gx, sc_gz, sc_y0;
    int64_t sc_mt, sc_at, sc_off;
    int32_t sc_wx, sc_wy, sc_wz, sc_det, sc_stuck;
    int32_t sc_pil, sc_py, sc_stand;    /* the far look's pillar: 0 none, 1 rising, 2 at the top, 3 dug down; its base */
    int64_t sc_pat;                     /* the step the rise or the way down began */
    /* lane/obsid (obsidian_water_trip on): the surface water kept, the trips begun and those that ended at water */
    int32_t sw_ok, sw_x, sw_y, sw_z, sw_trips, sw_fill, sc_trip;   /* sc_trip: this scout is a trip to sw */
    int32_t sc_strip;           /* lane/obsid (obsidian_strip_cast on): the pickaxe and the water held, as last seen */
    int32_t pc_look;            /* and the looks around still due after a cast */
    int32_t pool_skip;          /* lane/obsid (obsidian_pool_min on): the casts not begun at a pool too small */
    int32_t gl_n, gl_done;      /* lane/obsid2 (obsidian_glance walk): the search's cells walked, and whether this
                                   cell's glance was looked from */
    int32_t ar_dir, ar_cx, ar_cz, ar_n;   /* lane/obsid2 (obsidian_walk around): the side step's direction (-1 none),
                                             the cell it began in, the side steps taken */
    int64_t ar_until;           /* the age the side step ends at the latest */
    int32_t yield;              /* lane/obsid2 (obsidian_yield on): the search saw a target this step: the strip yields */
    int32_t ds_at;              /* lane/obsid2: the cast's stair down (ob_descend): the age its look began to settle */
    int32_t rl_on, rl_x, rl_z, rl_dir, rl_n;   /* lane/obsid2 (obsidian_resume line): the strip's line kept at a cast
                                                  (its feet's cell and direction), and the returns made to it */
    int64_t rl_at;              /* the step it was kept */
    int32_t te[5];              /* lane/obsid2: the water trips that ended short: under y 50, 10 under the start, night,
                                   out of air, too long (obsidian.c ob_scout) */
#ifdef OB_CENSUS
    int cz_bk, cz_dp, cz_l[2];          /* lane/obsid's census (a VARIANT build): bucket, diamond pickaxe, lava printed */
#endif
    struct obs_book bk;
    /* lane/bportal (books only, nothing reads them): ob_start's last answer (OBS_*, vecenv.h) and the step it gave it */
    int32_t why;
    int64_t why_step;
    /* lane/bportal (obsidian_carry frame): the arrival frame's mining over (10 held, none left to dig, its window
     * past), and the frame cells it began (books) */
    int fr_done, fr_jobs;
    /* lane/bportal (obsidian_carry fallback): the count last seen, the step it last rose, and the latch: the first frame
     * at 10 (ob_observe) */
    int fb_n, fb_on;
    int64_t fb_at;
    /* lane/flint (obsidian_walk dig): the strip's walk to a remembered source (its cell, the job it is for, the step and
     * the strip's turn count it began at, the strip's last turn seen and the steering's hold after it); books: walks
     * begun, arrived, given up */
    int wk_on, wk_x, wk_y, wk_z, wk_job, wk_turns, wk_last;
    int64_t wk_t0, wk_hold;
    int wk_n, wk_arr, wk_gave;
    int mpx, mpy, mpz;          /* ob_mem_pick's last cell (its answer's, set whenever it answers a job) */
    int wk_bp;                  /* lane/flint (bportal_walk dig): the walk is the bucket portal's water trip's */
};

static inline void ob_reset(struct obs_env *o)
{
    for (char *p = (char *)o; p < (char *)(o + 1); ++p) *p = 0;
    o->scan_at = -1;
    o->why = -1;
    o->hot_at = -1000;
    o->mem_at = -1000;
    o->bk.nether = o->bk.kit = o->bk.d_step = o->bk.wb_first = -1;
    for (int k = 0; k < OF_KINDS; ++k) o->bk.fn[k] = -1;
    o->bk.sky_why = -1;
    o->bk.sky_d = -1.0f;
    o->bk.lhp = 20.0f;
    o->bk.lground = 1;
    o->bk.cause_last = OC_OTHER;
}

#endif
