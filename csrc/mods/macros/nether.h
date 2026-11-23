/* lane/nether: NETH_VEC_NETHER's per-env state (nether.c, included by macros.c; struct vec_env holds one) */
#ifndef NETHERITE_RUNTIME_VECENV_NETHER_H
#define NETHERITE_RUNTIME_VECENV_NETHER_H

#include <stdint.h>

#include "../../runtime/netherview.h"

enum { NT_SEARCH, NT_FORT, NT_CAMP, NT_HOME };          /* the phases */
#define NT_FRONT 96             /* lane/nether5: the fortress floors remembered (nether.c nt_front_add) */
#define NT_CRUMBS 768          /* lane/nether2: the way's cells remembered (nether.c's crumbs) */
#define NT_OPATH 512           /* lane/rod (nether_oracle): a path's cells at most */
enum { NC_LAVA, NC_FIRE, NC_FALL, NC_MOB, NC_STARVE, NC_OTHER, NC_KINDS };   /* what hurt last (a death's cause) */

struct nt_env {
    int on;                     /* the macro running (the mine macro's kind, as the seek and the strip) */
    int arrived;                /* the episode's first Nether step seen */
    int phase;
    int has_portal, px, py, pz; /* the portal cell the player stood in on arrival (its block seen: the feet's) */
    int heading;                /* the search's leg's direction, 0..3 (yaw 0, 90, 180, 270) */
    int zdir, xdir, legk;       /* the search's z legs' direction, its x legs', the leg now (0 z, 1 x) */
    int wflip;                  /* the walker's side flipped from the sweep's (kept until the held way opens) */
    int pol_yaw;                /* the policy's camera yaw bin this step, off centre */
    int spin;                   /* a look round: steps left */
    float spin_yaw;
    int64_t leg_at;             /* the step the search's leg began */
    double leg_x, leg_z;        /* where it began */
    int has_fort, fx, fy, fz;   /* the nearest fortress block seen (nether brick or a fence) */
    int has_loc, loc_x, loc_y, loc_z;  /* privileged candidate start's centre and travel height, before a piece loads */
    double loc_d0;             /* arrival's horizontal distance to that start, for the search's progress */
    int loc_mode;              /* the sweep was aimed at the candidate; re-aim when an actual piece is generated */
    int has_entry, entry_x, entry_y, entry_z, entry_kind; /* opt-in enclosed standing cell from generated boxes */
    int entry_way, entry_wx, entry_wy, entry_wz, entry_fail, entry_done;
    int64_t entry_retry_at;
    int fort_at;                /* gone to it: the search goes on at its height */
    int rods, kills;            /* held now; stat.mobKills */
    int free_slots;             /* the inventory's empty main slots at the last Nether observation */
    int has_sp, sx, sy, sz;     /* the blaze spawner seen */
    int64_t sp_break_at;        /* lane/chain2: the first opt-in attempt to mine it after seven rods */
    int64_t scan_at;            /* the last view scan's step */
    /* the walker (the strip's cell rule toward a target): the direction, the cell being cleared (0.. the column's
     * cells, 5 the floor, 3 the walk, -1 the next column to pick), the column, its vertical mode (-1 down, 0 level,
     * 1 up), the age its look began, the age of the last progress, turns at this cell, the cell they began at */
    int wdir, wk, wcx, wcy, wcz, wmode, wlook, wgain, wturns, wbx, wby, wbz;
    int wtried;                 /* bit d: direction d judged bad from this cell */
    int wside;                  /* the side tried after the way ahead: +1 or -1 quarter (0 unset) */
    int wstill;                 /* walk steps without moving */
    int whead;                  /* the head room over both columns cleared too (a lip the walk did not climb) */
    double wpx, wpz;            /* the position a step ago */
    int wplace;                 /* tries to bridge the floor cell */
    int wseal;                  /* tries to seal lava come into the column */
    int rdug[4][3];             /* lane/nleg (nether_dig_refill): the walker's newest dug cells (a ring), each's dig step */
    int64_t rdug_t[4];
    int rdug_k[4];              /* the steps spent filling each */
    int nrdug;
    int faim, fax, fay, faz;    /* lane/nleg (nether_dig_aim): the cell whose open face the dig aims at (the crosshair
                                 * landed on another block aiming at its centre) */
    int aseal;                  /* tries to seal lava beside the player's own cells (this cell) */
    int ffill;                  /* lane/rod (nether_feet_fill): steps spent filling the cell under the feet */
    int pexit;                  /* lane/rod (nether_portal_exit): steps spent walking out of the portal */
    int bstat_set, bstat0, bstat;   /* lane/rod: the stats' blaze kills at the episode's first step, and since */
    /* lane/rod (nether_drops): the blazes struck (id, last place in view, the step of the last strike) and the drops
     * of the credited kills not yet walked to (a ring) */
    int sid[4];
    float spos[4][3];
    int64_t sat[4];
    int bstat_seen, ndrop, drops;
    int bseek, bseeks;          /* lane/rod (nether_blaze_seek): walking toward a blaze seen on the fortress; times */
    float bsx, bsy, bsz;        /* where it was last seen */
    int64_t bseek_at, bseek_t0; /* the step it was last seen, the seek's first step */
    float drop[8][3];
    int wlevel;                 /* only level moves from this cell (no stair was possible any way) */
    int back, backs;            /* steps left backing off from lava; back-offs this cell */
    float back_yaw;             /* the look held while backing off */
    int nsealed, sealed[16][3]; /* the cells sealed this episode (a ring of NT_SEALS): never dug */
    int64_t off;                /* the macro off until this step (the policy's turn: no way on found) */
    /* the fight's drop: walked to after a kill */
    int pick, pick_age;
    float rx, ry, rz;
    int fight_kind;             /* the locked living's kind (a blaze's drop is walked to) */
    int camp_age;               /* steps at the spawner since the last fight */
    /* the books (the episode's steps; -1 not yet) */
    int64_t t_arrive, t_found, t_spawner, t_rod1, t_rods7, t_home;
    int rods_max, deaths, was_dead, cause_last, last_dim;
    int64_t lava_at;            /* the last step the feet or head stood in lava (its burning is the lava's) */
    int cause[NC_KINDS];
    int64_t t_death;            /* lane/nether2: the first death's step (-1 none) and where */
    double death_pos[3];
    int cobble, mode;           /* cobblestone held; the walker's travel mode (NM_*) */
    int64_t scan_ns, nsteps;    /* the scans' time (CLOCK_MONOTONIC), the Nether steps observed */
    /* lane/nether2: the way walked (the feet's cells on the ground since the arrival, a loop cut where a cell is met
     * again; full, every other kept), walked back in reverse to the portal; home_k the crumb walked to (-1 not yet) */
    int ncrumb, home_k, crumb_skip;
    int warr;                   /* the walker's way into its cell (0..3, -1 none) */
    int fort_ty;                /* the feet's height where the fortress was reached (the search's there after) */
    double pil_base;            /* a pillar's jump: the feet's height it began at (0: none) */
    int pil_age;
    int64_t pil_off;            /* no pillar tried before this step (one given up) */
    int has_camp, cx, cy, cz;   /* where the first blaze was killed (the camp when no spawner is seen) */
    int heal_k;                 /* healing: the crumb backed off to (-1 not healing) */
    int resting;                /* hurt away from the blazes: still until healed */
    /* lane/nether3: the search by the far view: a goal (a floor the fan saw) walked to, chosen after each look round
     * from the floors the look round's fans saw (best kept as they come), along a bearing (degrees, the arrival's) */
    int has_goal, gx, gy, gz, glava;
    int64_t goal_at;
    float bearing, cand_score;
    int has_cand, cx2, cy2, cz2, clava;
    int goals, goal_fail;       /* goals chosen; goals given up in a row (the bearing turns) */
    int spin_none;              /* a look round for a goal running (none seen after it: a dig leg) */
    int64_t fort_direct_off;    /* the straight way to a far fortress given up until this step (goals toward it) */
    int64_t refill_at, refill_off;  /* the inventory opened for fill blocks at this step (0 none); no refill before */
    int64_t trim_at, trim_off;      /* opt-in: inventory opened to leave slots free for blaze-rod drops */
    int trim_n, trim_slots[3];
    float trim_yaw;
    int has_top, tx, ty, tz;    /* lane/nether3: the highest fortress block seen near (the climb's height) */
    int climbed;                /* a climb ran (its end: the fortress reached again at the new height) */
    int clook;                  /* a look round up from the pillar: steps left */
    int fort_looks;             /* look rounds at the fortress's foot (before its height is taken as the fortress's) */
    int climb_fails;            /* climbs tried again toward a deck well over the player */
    int nplaced;                /* the cells the player's fill went into (a ring of 64): never taken as the fortress */
    int16_t placed[64][3];
    int gather_age, gax, gaz;   /* netherrack gathered: steps on it, the cell it is dug from */
    int64_t gather_off;
    int64_t climb_off;          /* no climb before this step (one given up) */
    int ccx, ccz, has_cc;       /* the column the climb goes up (beside the player's when its own is covered) */
    int fstuck;                 /* steps a blaze lock stood out of reach with the player not moving */
    int64_t fight_off;          /* blazes fought only within reach until this step (a lock dropped) */
    double fpx, fpz;            /* the position a fight step ago */
    int64_t fan_at, fan_ns;     /* the last fan's step; the fans' time */
    int nfans;
    int deflect_id, deflects, deflect_hits, ghast_seen;   /* lane/nether4: fireballs hit back (nt_deflect) */
    int covers;                 /* lane/nether4: cover blocks placed against blazes out of reach */
    int cov_fx, cov_fy, cov_fz, cov_here, cov_age;   /* the feet cell the cover is built at, blocks there, a try's age */
    int64_t cov_off;
    int64_t eye_strikes;        /* lane/rods2 (nether_eye_strike): steps the eye strike drove */
    int64_t mouth_until, mouth_steps, mouths;   /* lane/rods2 (nether_mouth_cover): the re-dig held to, steps it ran,
                                                 * blocks it placed */
    int32_t deck_wait;          /* lane/rods2 (nether_deck_wait): steps the current drop's walk waited */
    int64_t deck_waits, deck_gaveup;   /* (books) steps waited, drops given up past NT_DECK_WAIT */
    int glance;                 /* lane/nether4: a glance round's steps left, from glance_yaw */
    float glance_yaw, glance_hp;
    int64_t glance_at, dig_at;
    int64_t dn_t;               /* lane/nleg (books): the last dig's step (0 none), cell, block, lava masks (neth_vec_nether) */
    int dn_x, dn_y, dn_z, dn_b, dn_lava, dn_seen;
    int refills;                /* lane/nleg (nether_dig_refill): dug cells filled again for the lava's glow beside them */
    double prog_x, prog_y, prog_z;  /* lane/nether4: the unstick's last place of progress, its step, the unsticks */
    int64_t prog_at;
    int unsticks;
    int hood, hx, hy, hz, hood_k, hood_age, hoods, hood_press, hood_mid;   /* lane/nether4: the hood (0 none, 1 building, 2 healing, 3 out) */
    int hood_keeps;         /* lane/armed (nether_hood_keep): the hood built again this many times */
    int shy, shy_k, shy_clear, shys;    /* lane/nether5: the shy from a far blaze (nt_drive1) */
    int64_t shy_at, shy_off;
    float shx, shy_y, shz;              /* where the blaze was last in view */
    float hood_yaw;            /* lane/nether5: the look as the hood began */
    int64_t far_at;            /* lane/nether5: the far spawner scan's last step */
    int tower, tower_y0, tower_top, tower_age, tower_clear, towers;   /* lane/nether5: the tower from wither skeletons (nt_tower) */
    int64_t tower_off, tower_hit;
    int tower_hits, tower_x, tower_z;
    int camps_given;           /* lane/nether5: camps with no spawner given up */
    int floor_untun;           /* lane/legs (nether_floor_keep): the tunnel's dig down vetoed once: planned without it */
    int csx, csy, csz, cs_ox, cs_oz;   /* lane/nleg (nether_camp_side): the spawner the side was chosen for, the offset */
    int fort_stall;            /* lane/nleg (nether_fort_path): steps in the fortress phase short of the kept brick */
    /* lane/nether5: the fortress's floors the far view saw (a ring of NT_FRONT), each walked near or given up (done) */
    int nfront, kfront, fronts;     /* remembered, the ring's next, goals taken from it */
    int16_t front[NT_FRONT][4];     /* x, y (the feet's), z, done */
    int16_t crumb[NT_CRUMBS][3];
    /* lane/rod: the link's funnel (the first step of each; -1 not yet): on the fortress (fort_at), a blaze in view, a
     * blaze under the crosshair as the attack is pressed, a blaze fight won (its lock lost on a blaze); hits and kills;
     * the Nether's alive steps the macro was off (its off step, the screen, the lava escape) and the steps it drove;
     * the feet's last cell on the ground (a fall's start) and the first death's */
    int64_t t_fortin, t_bseen, t_bhit, t_bkill;
    int bhits, bkills;
    int64_t st_off, st_screen, st_esc, st_drive;
    int gfx, gfy, gfz, dgx, dgy, dgz;
    float hood_hp;              /* lane/rod (nether_hood_hit): health in the hood a step ago; no hood before hood_off */
    int64_t hood_off;
    float lhp;
    int lground;
    double fall_top;
    /* lane/rod (nether_search surface): the next scan pillar's step, its blocks left, the cells bridged in a row on the
     * way, and the books' pillars and digs refused */
    int64_t surf_next;
    int spil, sbr, surf_pils, surf_refused;
    int zlegs;                  /* lane/rod (nether_sweep back): z legs begun; the leg now's length (0 the default) */
    double leg_len;
    double sbest;               /* lane/rod (surface): the leg's farthest along its heading, and the step of it */
    int64_t sbest_t;
    float tower_hp;             /* lane/rod (nether_skel_tower far): health a tower step ago; pillars given up hit */
    int tower_quits;
    int deck_only;            /* lane/rod (nether_blaze_seek deck): this walk only onto the fortress's floor */
    /* lane/rod (nether_oracle, privileged): the last read's step, the heading set from the fortress map, the reads'
     * books (the step the map first held a fortress piece, the spawner's cell first given) */
    int64_t or_at, t_or_piece, t_or_sp, or_hit_at, or_lock_steps;
    int or_aimed;
    /* lane/rod (nether_oracle spawner): the walk to the spawner (netherview_oracle_path): its cells, how many, the next
     * one's index, the step it was made, the steps off it, the paths made and found */
    int16_t opath[NT_OPATH][3];
    int nopath, opath_k, opath_off, opaths, opaths_ok, opaths_new, opath_tgt[3], opath_build;
    int64_t opath_t;
    int or_picks;               /* lane/rod (nether_oracle_lock): steps walking to a rod's drop */
    int or_build;               /* lane/rod (nether_oracle): the walker's step builds a cell of the oracle's path */
    int64_t or_drop_at;         /* lane/rod (nether_oracle): the step a path's drop was walked (no brink crouch) */
    int dep_done, dep_quiet, dep_age, dep_sink, dep_y0;
    int dg_side, dg_sdir, dg_flip, dg_age, dg_x, dg_z, dg_blocks, dg_sides, dg_skip, dg_still, dg_have, dg_ax, dg_az;
    double dg_wx, dg_wz;
    /* lane/rod: the digger's cell left, its best distance to the portal, bridges, the step walked (its height, steps
     * left), and the columns entered (x, z, times; a ring) */
    int dg_lx, dg_lz, dg_best, dg_bridges, dg_ty, dg_walk, dg_nvis, dg_replan, dg_planned;
    int16_t dg_vis[256][3];
    int64_t dg_best_t;/* lane/rod: the tunnel digger (nt_digger) */ /* lane/rod (nether_oracle tunnel): the closed departure done, its quiet steps */
    int pdmin;                 /* lane/rod: the least blocks from the portal cell this episode, + 1 (0 none) */
    int home_rods;            /* lane/rod: the rods held, and the health, the step the overworld was reached */
    float home_hp;
    int opath_tun;             /* lane/rod (nether_oracle home): the path a tunnel (NV_PATH_TUNNEL) */
    int pin, pout, pox, poy, poz, pouts;   /* lane/rod (nether_oracle home): steps in the portal, out, the cell out, outs */
    int64_t or_digs;           /* lane/rod (nether_oracle_lock): steps digging a block between the eye and a blaze */
    int64_t or_skel_steps;     /* lane/rod (nether_oracle_lock): steps striking a skeleton on the way */
    int64_t or_lock_t0, or_lock_last, or_lock_off; /* the lock's run began, its last step, no lock off the spawner before */
    struct netherview_hit hit[NV_KINDS];
    /* lane/rods (books only, for --nether-trace; never read by a macro): nt_drive's last step: its return, the act's
     * held keys and presses (bit k: PK_k), the blazes in the player's view and the nearest's distance from the eye */
    int bk_rc, bk_hold, bk_press, bk_bn, bk_doing;
    float bk_bd;
    int64_t bk_t;               /* the step of those (0 none) */
};

#endif
