/* lane/obsidian3: NETH_VEC_BPORTAL's per-env state (bportal.c, included by macros.c; struct vec_env holds one) */
#ifndef NETHERITE_RUNTIME_VECENV_BPORTAL_H
#define NETHERITE_RUNTIME_VECENV_BPORTAL_H

#include <stdint.h>

#define BP_MEM 8                /* the water and lava sources remembered a kind (a ring) */
#define BP_BAD 8                /* lava sources given up as a site's (a ring) */

/* the build's books (vecenv.h neth_vec_bportal) */
struct bp_book {
    int32_t starts, sites, aborts, casts, lit, trips_lava, trips_water, uses;
    int32_t fail_kind[8];       /* why a site was given up (bp_abort's reasons) */
    int64_t lit_step;           /* the step the portal first showed (-1 not yet) */
    int64_t steps_on;           /* steps the macro drove */
    int32_t best_casts;         /* the most frame cells cast at one site */
    /* lane/obsidian4 */
    int32_t pools;              /* the lava pools a bucket was filled at (one more than BP_POOL_APART from the others) */
    int32_t fills_lava;         /* lava buckets filled */
    int32_t search_found;       /* lava sources the search's strip brought into view */
    int64_t search_steps;       /* steps the search's strip drove for this macro */
    int32_t back_walks;         /* walks back to a held site from a fill */
    int32_t site_lost;          /* sites given up with no lava found for BP_SITE_KEEP steps */
    int32_t wet_who;            /* who drove the step the eyes last went under water (obsidian.h OW_*) */
    int32_t site_why[8];        /* the site tries, by why the first direction was refused (bportal.c BS_*) */
    int32_t climbs;             /* climbs begun (under the search's floor, lava wanted) */
    /* lane/obsidian5: the walk over the surface (BM_ROAM) and the climb to the sky (BM_UP) */
    int64_t roam_steps;         /* steps the walk and its far looks drove */
    int64_t up_steps;           /* steps the climb to the sky drove */
    int32_t roam_legs, roam_looks, ups, ups_done;   /* legs walked, far looks, climbs to the sky begun and ended there */
    int32_t roam_lava, roam_water;  /* lava and water sources the far look brought into view (remembered) */
    float roam_lava_d;          /* the first lava's distance from the eye (-1 none) */
    int64_t roam_lava_step;     /* the step the far look first found lava (-1 none) */
    float roam_far;             /* the farthest the walk got from where it began (blocks, horizontally) */
    int32_t pillars, pillar_tops, pillar_lava;  /* pillars begun, risen to the top, far looks from one that saw lava */
    /* lane/obsidian6: sites begun and cells cast by the one-bucket cast, lava buckets poured back into a pool (the
     * one bucket held lava when water was wanted), the water poured beside a cell before a lava trip */
    int32_t one_sites, one_casts, dumps, parks;
};

struct bp_env {
    int on;                     /* the macro running (the mine macro's kind) */
    int mode;                   /* BM_* */
    int tx, ty, tz;             /* a trip's target (a source cell) */
    int phase, at, uses, gain, stuck;
    double best, px, pz;
    int64_t off;                /* no start before this step */
    int64_t scan_at;            /* the last memory look */
    int64_t oracle_at;          /* lane/chain (bportal_oracle on): the step of the last privileged scan (0: none) */
    struct { int32_t x, y, z; } mem[2][BP_MEM];      /* the water [0] and lava [1] sources seen */
    int nmem[2], memn[2];
    struct { int32_t x, y, z; int64_t until; } bad[BP_BAD];
    int nbad;
    struct { int32_t x, y, z; int64_t until; } bs[BP_BAD];   /* sites given up (their anchors), a ring */
    int nbs;
    /* the build: the anchor (the feet's cell at the site), the frame's forward and right axes, the plan's step, its
     * sub-step and its age, the aim being tried and how long it has been held, the count a use watches */
    int site, ax, ay, az, fx, fz, rx, rz;
    int step, sub, resub, sat, aim, held, waited, before, wr, wh, wf, wset, tries;
    int here;                   /* lane/obsidian6: the step worked from where the player stands (the walk back gave up) */
    int wskip;                  /* lane/obsidian6: W candidates passed over at this step (their pour found no aim) */
    int lx, ly, lz;             /* the lava source the site was chosen beside */
    int site_tries, site_casts;
    int skx, sky, skz;          /* a fill's source that the ray would not land on, skipped until skuntil */
    int64_t skuntil;
    int lk_set;                 /* the look around: done this episode, where (lkx, lkz), when, its first yaw */
    double lkx, lkz;
    int64_t lk_step;
    float lk_yaw;
    /* lane/obsidian4: the walk's detour (a yaw offset until det_until, its side), the walk back to the site (its
     * nearest distance and the age it was reached; bwalk 1 while it runs), a fill's distance at its start, the pools
     * filled at, the step the site last had lava or a fill (its keep), the search's scan */
    int64_t det_until;
    int det_sign, ndet, bwalk, back_gain, fd0set;
    double back_best, fd0, det_off;
    struct { int32_t x, y, z; } used[BP_MEM];
    int nused;
    int64_t site_lava_at, srch_scan, srch_last;
    int lair;                   /* the air a step ago */
    int64_t climb_off;          /* no climb before this step (BP_CLIMB_OFF after one gave up) */
    /* lane/obsidian5: the walk over the surface: set once it began (its centre rm_cx, rm_cz and first yaw), the leg
     * (its number, start and age, the best distance to its end and the step it was gained), the far look (where the
     * last was, its phase: 0 none running, 1..BP_FAR_LOOKS), no walk before rm_off, the steps under the sky spent */
    int rm_set, rm_leg, rm_phase, rm_under, rm_fast;
    int rm_pil, rm_py, rm_stand;    /* the pillar (bportal_roam pillar): 0 none, 1 up, 2 its looks, 3 down; its base
                                       (the feet's cell), the feet's cell when last on the ground */
    int64_t rm_pat;
    double rm_cx, rm_cz, rm_lx, rm_lz, rm_kx, rm_kz, rm_best;
    float rm_yaw0, rm_kyaw;
    int64_t rm_leg_at, rm_gain_at, rm_off, rm_used;
    struct bp_book bk;
    /* lane/portal (bportal_fill_giveup): fill trips ended in a row with no bucket filled, the give-ups taken, and (sky)
     * the step until which a kind's remembered sources are the eye's alone (water [0], lava [1]) */
    int fill_fail, fill_rests;
    int64_t fill_blind[2];
};

static inline void bp_reset(struct bp_env *b)
{
    for (char *p = (char *)b; p < (char *)(b + 1); ++p) *p = 0;
    b->scan_at = -1000;
    b->lair = 300;
    b->bk.lit_step = -1;
    b->bk.roam_lava_d = -1.0f;
    b->bk.roam_lava_step = -1;
}

#endif
