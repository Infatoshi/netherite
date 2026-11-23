/* lane/stronghold: NETH_VEC_STRONGHOLD's per-env state (stronghold.c, included by macros.c; struct vec_env
 * holds one) and its books (neth_vec_stronghold_book) */
#ifndef NETHERITE_RUNTIME_VECENV_STRONGHOLD_H
#define NETHERITE_RUNTIME_VECENV_STRONGHOLD_H

#include <stdint.h>
#include <string.h>

#include "../../runtime/shoracle.h"
#include "../../runtime/shview.h"
#include "../../runtime/vecenv.h"

#define SH_LINES 8              /* the throws' lines kept (the newest) */
#define SH_EYE_SAMPLES 8        /* the flight's samples kept (one a tick) */
#define SH_OPATH 256            /* lane/shold: the oracle way's cells kept */

struct sh_env {
    int on;                     /* running (the mine macro's kind, as the seek and the strip) */
    int go;                     /* latched this episode (12 eyes held in the overworld) */
    int64_t fw1;                /* lane/chain2 (stronghold_food wait): the food wait's first step + 1 (0 not begun) */
    int fwait;                  /* ... and waiting now (the latch held back for the food) */
    int phase;                  /* SH_THROW .. */
    int64_t pstart;             /* the step the phase began */
    /* the throw and the flight */
    int thrown;                 /* the use pressed */
    int64_t fetch, fpress;      /* lane/stronghold3: the last step fetching the eyes into the hotbar, its last E press */
    double ox, oy, oz;          /* the eye's origin (the player's eye at the throw) */
    int eid;                    /* the flying eye's entity id (0: not seen yet) */
    int ns;                     /* samples kept */
    double fx[SH_EYE_SAMPLES], fy[SH_EYE_SAMPLES], fz[SH_EYE_SAMPLES];
    int64_t ft[SH_EYE_SAMPLES];
    int est;                    /* 0 none, 1 far (a line), 2 near (the room's column) */
    double tx, tz;              /* the flight's target (far: 12 ahead; near: the room's column) */
    double ux, uz;              /* its heading */
    double hx, hy, hz;          /* the eye's last place seen */
    int64_t hseen, died;        /* the step it was last seen; the step it was gone (0 not yet) */
    int eyes_after;             /* eyes held after the throw */
    int64_t tstep;              /* the throw's step */
    int search;                 /* look turns while the eye is not in view */
    int dseen;                  /* this throw's drop seen lying */
    int64_t canopy, cdone, cstep;   /* lane/stronghold2: the canopy over a drop being cleared since (0 none), the step
                                     * its column was clear, the pillar block's step */
    int relocs, sitec;          /* the throw's site moved (away from leaves) so far; leaves counted round it */
    double syaw, sitex, sitez;  /* the look the site's quarters start from; the leaves' summed places */
    int cmode;                /* 0 breaking what is over the head, 1 a block placed under the feet */
    double ccy0, cpy;           /* the feet's height when it began, when the pillar block began */
    /* the lines and the goal */
    int nlines;
    struct { double sx, sz, ux, uz; } line[SH_LINES];
    int pair;                   /* lines in the pair being thrown (0, 1) */
    double gx, gz;              /* the travel's goal */
    int gk;                     /* what follows arriving there: SG_THROW, SG_DIG */
    double gnear;               /* the pair's throws' distance from their crossing (a near pair's crossing is the room) */
    /* the travel */
    double ax, az;              /* the stall anchor */
    int64_t astep;
    int tunnel, tk, tlook;      /* digging ahead: on, the cell (0 head, 1 feet), the step its look began */
    int tcx, tcy, tcz, tc2x, tc2z;   /* the first column ahead, the second the box sweeps */
    int tnh;                    /* the rows dug from tcy up */
    int climb;                  /* the step a pillar out of water began (0 none) */
    double ytop;                /* the staircase's top (the fall's top, or a cave's way out), the step it was set */
    int64_t ytop_t, asc_t;      /* ... and the step the staircase began */
    int asc, ak, alook, acx, acy, acz, ad;   /* the upward staircase: on, its cell (0 pick, 1..3 dig, 4 step), look */
    double cy0;                 /* the feet's height then */
    int64_t dend;               /* detouring until this step */
    double dyaw;
    int dside, stalls;
    int64_t dlast;              /* the step the last detour began (its side kept for the next) */
    double std;                 /* the goal's distance when the stall streak began */
    double lx, lz;              /* the position a step ago */
    double wbest, wgx, wgz;     /* lane/stronghold3 (stronghold_stuck): the walk's nearest to its goal, that goal */
    int64_t wbest_t, wlast;     /* ... the step it was reached (0: begun again), the walk's last step */
    double stx, stz;            /* ... where it was last stuck, how many times in a row there */
    int nstuck;
    int pdn;                    /* lane/stronghold3: the thrown eye's drop seen, where (remembered out of view) */
    double pdx, pdy, pdz;
    int64_t fl_t, fl_off;       /* lane/stronghold3 (stronghold_flesh): the eating's first step, no eating before */
    int fl_food;                /* ... the food level then */
    int64_t spend;              /* ... the tunnel spends the dig's pickaxe reserve until this step */
    /* the dig: the spiral round the room's column (cx, cz) */
    int cx, cz;
    int sidx;                   /* the spiral's cell index (0..7 round the column) */
    int sk, slook;              /* the cell being judged (0 head, 1 feet, 2 below, 3 floor, 4 the walk), its look's step */
    int scx, scy, scz;          /* the next cell, the feet's y it was picked at */
    int sfx, sfz;               /* the feet's cell it was picked from */
    int rfx, rfz;               /* the roof's steps: the feet's cell they began in */
    int sfail;
    int sdir;                   /* the spiral's way round: +1, -1 (0: +1); turned back at a refused cell */
    int slevel;                 /* level moves since the last step down */
    int snapped;                /* lane/stronghold2: the start's chunk centre read off one far line */
    double vx, vz;              /* the goal after a waypoint (SG_VIA) */
    double lsx, lsz;          /* the flight's line: a sample of the eye (on it), set with ux, uz by the estimate */
    int roof;                  /* the roof's y (0: not found) */
    int rk;                     /* the roof's steps: 0 into the column, 1 down it */
    /* the room */
    int nfr;                    /* frames remembered */
    struct { int32_t x, y, z, eye; } fr[12];
    int64_t used_at;            /* the step an eye was last put in */
    int ulx, uly, ulz;          /* the frame it went in */
    int rlook;                  /* the room's look-round index */
    int64_t fskip[12];          /* lane/stronghold2: a frame left until this step (out of reach from here; never taken
                                 * as filled) */
    int ptseen, ptx, pty, ptz;  /* the end portal block seen (lit) */
    int rpick;                  /* steps spent going to an eye lying in the room */
    int64_t reyes;              /* eyes held at the room's last step (an increase: one picked up there) */
    /* the books */
    struct neth_vec_sh_book book, last;
    float pair_, prev_hp;
    int prev_air, prev_fire, prev_food, prev_nvis;
    double ymax;                /* the highest feet since last on the ground (a fall's height) */
    double fall;                /* the last landing's fall */
    int64_t fall_step, lava_step;
    double x0, z0;
    int armed;
    double fstride;             /* lane/macroopts: stronghold_locate follow's walk along the newest line (0: not begun) */
    /* lane/stronghold3: the night's hide (a hole 3 deep, a block over the head, held until the morning) */
    int hs, hprev, hgk, htry;   /* its step (SHH_*), the phase it took over, that phase's goal kind, cover tries */
    double hy0, hgx, hgz;       /* the feet's height where it began, the goal it took over */
    int64_t hstep, hoff, hbeg;  /* the step its step began; no hide before this step (one given up); the hide's start */
    int sky;                    /* lane/stronghold3: the staircase up is a climb to the sky (stronghold_roof sky) */
    int crafts;                 /* lane/stronghold3: the craft macro's starts given the step for a stone pickaxe */
    int tdug;                   /* lane/stronghold3: the tunnel under way broke a block */
    int rgo;                    /* lane/stronghold3: steps walked on the room's floor toward a frame's outside */
    /* lane/shold (stronghold_oracle locate, PRIVILEGED): the room as the world's data has it, the way's cells */
    int osub;                   /* SO_* */
    struct shoracle_room oroom;
    int16_t op[SH_OPATH][3];    /* the way's cells */
    int nop, opk, ooff, onew, ofail, otgt[3], oflags;
    int64_t ot, odig_t, ouse, oread, ostart;
    int odx, ody, odz;          /* the block being dug */
    int ofr, ostand[3], ostand_ok;  /* the frame being filled, the cell it is filled from */
    int ofr_skip[12];           /* frames whose stand cells failed (tries) */
    int olx, oly, olz;          /* the travel's leg target */
    double obest;               /* the travel's nearest to the room so far, and the step */
    int64_t oturn0, oturn_last, oturn_off;  /* the turn to a hostile out of view: its start, last step, none until */
    int64_t obest_t, odrive;    /* ... the step it drove last (a hide, a fight, a yield between: no stall counted) */
    int64_t rstart;             /* lane/proom (stronghold_room_start on): the step + 1 it latched on a portal room's frame
                                 * in view (0 not); appended: the chain sidecar's controller grows at its end only */
    double owp[3];              /* lane/pdig (sneak_drop): sho_follow's last airborne feet, and the steps in a row there */
    int32_t owst, owpad;
};

/* env_reset's: the episode's books kept as the last one's, the rest cleared */
static inline void sh_reset(struct sh_env *h)
{
    struct neth_vec_sh_book keep = h->book;
    memset(h, 0, sizeof *h);
    h->last = keep;
    struct neth_vec_sh_book *b = &h->book;
    b->started = b->brick = b->frame = b->room = b->lit = b->end = b->death_step = b->gave_up = -1;
    h->sk = -1;
}

#endif
