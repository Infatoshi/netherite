/* lane/rod: the dig walker's (dig.c) and the armor macro's (armor.c, armor_kit) per-env state; struct vec_env holds one
 * of each (e->arm) */
#ifndef NETHERITE_MODS_ARMOR_H
#define NETHERITE_MODS_ARMOR_H

#include <stdint.h>

#include "../../runtime/walkto.h"

#define DGW_PATH 256            /* a plan's cells at most */

/* the dig walker: a tunnel planned over the server's blocks (netherview_oracle_path_dim, NV_PATH_TUNNEL), each next cell
 * dug and walked */
struct dig_walk {
    int16_t path[DGW_PATH][3];
    int n, k;                   /* the plan's cells, the next one's index */
    int64_t t;                  /* the step the plan was made (-1 never) */
    int replan, fails;          /* a new plan wanted; plans in a row with no step taken */
    int have, ax, ay, az, walk; /* the cell being walked into and the steps left to reach its middle */
    int tx, ty, tz;             /* the target the plan is for */
    double wx, wz;              /* the position a step ago (the unstick) */
    int still;
    int plans, cells;           /* books: plans made, cells walked into */
};

/* the armor macro: the run (on), its phase, the ore cell it digs to, the ores given up (a ring), the pieces worn (bit
 * 0 boots, 1 leggings, 2 chestplate, 3 helmet, read off the inventory screen's armor slots), and the books */
enum { AR_MINE = 1, AR_EQUIP };
struct arm_env {
    int on, phase, age;
    int ox, oy, oz, has_ore;
    int tgt;                    /* lane/chain2: the block dug for (iron ore, or coal ore under armor_fuel) */
    int nbad, bad[8][3];
    int64_t ore_t0, scan_at;
    int worn;                   /* bits of the pieces worn */
    int read;                   /* lane/chain2: the inventory screen's armor slots read once; until then the worn bits
                                 * are the client's armor slots (a chain --resume replays with no macro: books 0) */
    int points;                 /* their armor points (boots 2, leggings 5, chestplate 6, helmet 2: iron's) */
    int eq_k, eq_age;           /* the equip's next piece and its age */
    int64_t t_ore1, t_ingots, t_worn3, t_worn4;    /* books: the first ore held, the ingots wanted held, three pieces
                                                    * worn, four (-1 not yet) */
    int ores_dug, gave_up;
    int n_ore, n_ingot, n_pieces, kind, cplan;  /* books: held now, the run's kind and craft plan now */
    struct dig_walk dw;
    /* lane/rod (snow_kit, snow.c): the run, the snow cell it goes to, given up (a ring), the books (layers and blocks
     * dug, snowballs held now, the steps of a shovel held and of 16 and 64 snowballs held) */
    int sn_on, sn_age, sn_press, sx, sy, sz, sn_has, sn_nbad, sn_bad[8][3], sn_dug, sn_gave_up, n_snow;
    int64_t sn_t0, t_shovel, t_snow16, t_snow64;
    struct dig_walk sdw;
    struct walkto_st swt;       /* the surface walk to the snow (walk_to); sn_tun: none found, the tunnel instead */
    int sn_tun;
    /* lane/snowleg (snow_first, snow_kit far): the wait's first step (-1 not begun), given up (1), the trip's snowy
     * column (sn_col: 0 not looked for, 1 found, -1 none), its leg, steps straight, the log dug for the shovel's wood,
     * and the books: the trip's steps, legs with no way, logs got, the steps the trip began and ended (snow in the
     * scan), the snowballs held on reaching the End (-1 not yet), the start's distance to the column */
    int64_t sn_w0;
    int sn_quit, sn_col, sn_cx, sn_cz, sn_leg, sn_lx, sn_ly, sn_lz, sn_straight;
    /* sn_col: 0 unchosen, 1 cold biome, 2 cold after the high scan, >=256 high target y = sn_col - 256. */
    int sn_log, sn_lgx, sn_lgy, sn_lgz;
    int64_t sn_lg_t0;
    int sn_trip_steps, sn_noway, sn_logs, sn_end_snow, sn_col_d;
    int sn_hops, sn_ux, sn_uz;  /* the column moved on with no snow found there, and the trip's heading (x1000) */
    /* the first death: its step, the snow run's part that drove the step before (0 none: another macro or the policy,
     * 1 the wood, 2 the trip, 3 the dig), steps since the snow run last drove, its cell, the air then */
    int64_t sn_last, sn_dstep;
    int sn_part, sn_dpart, sn_dago, sn_dpos[3], sn_dair, sn_dead;
    int64_t sn_room_off;        /* the surplus thrown (snow.c sn_room): not again before this step; stacks thrown */
    int sn_thrown;
    int64_t t_trip0, t_trip1, t_end;
    /* lane/supply (armor_giveup all): every ore given up since the list began (at bx0, bz0, with ores_dug at bdug0) */
    int nbad_all, bdug0, bad_all[64][3];
    double bx0, bz0;
    /* lane/supply (books only): the ores picked as targets, given up with no cell walked since the pick (no way), given
     * up after walking, the walker's cells at the pick */
    int picks, gu_noway, gu_walked, pick_cells0;
    int reach_miss, reach_aim, reached;     /* armor_reach: looks off the ore, the last step aimed, ores dug so */
    int pk_press, pk_fetched;               /* armor_pick: the inventory presses, the pickaxes fetched (books) */
    /* armor_craft=first: the pieces had at the last craft start, the step before which no start with that many again,
     * the starts made (books) */
    int cf_had, cf_starts;
    int64_t cf_retry;
};

/* lane/choice (books only, never saved: struct vec_env holds it past lvl, outside every saved range and the RNG): the
 * armour craft slot's decisions (armor.c arm_choose). An offer is an option id: 1 the next armour piece, 2 a
 * replacement iron pickaxe; the offers themselves are made fresh each call and never kept */
enum { ACH_NONE, ACH_ARMOR, ACH_IPICK, ACH_N };
enum { ACHR_DEFAULT, ACHR_SELECTED, ACHR_INVALID };     /* why the committed option won */
struct arm_choice {
    int64_t queries;            /* calls of the slot with an armour piece offered */
    int64_t conflicts;          /* ... with the replacement pickaxe offered too */
    int64_t chose[ACH_N];       /* commits by option id */
    int64_t invalid;            /* a selection not offered: the default committed */
    int64_t check_n, check_diff;    /* armor_choice=check: queries run twice, and those whose bytes or offers moved */
    /* the last conflict: its env step, the armour plan offered, the ingots and sticks held, the iron pickaxe's damage,
     * the option committed and why */
    int64_t last_step;
    int last_plan, last_ingots, last_sticks, last_dmg, last_chose, last_why;
};

#endif
