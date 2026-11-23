/* The vector env (vecenv.h). Each piece names the Python it replaces
 * (mcsr-speedrun mcsr/rl: semteach.py Loop and scalars, macros.py,
 * reward.py, actmap.py; mcsr/pairs.py) and computes what it computes, in
 * the same order and width. */
#define _GNU_SOURCE
#include "vecenv.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../cuda/raycast/rcdev.h"
#include "../engine/env.h"
#include "../engine/image.h"
#include "../engine/living.h"
#include "../engine/survival.h"
#include "pipeline.h"
#include "pool.h"
#include "moveact.h"
#include "rlgui.h"
#include "rlraw.h"
#include "rlscan.h"
#include "vecdragon.h"
#include "vecenv_task.h"
#include "train/stages.h"
/* lane/macromod: the macros (csrc/mods/macros, vecmods.h's dispatcher) unless the build leaves them out (rlbind.mk
 * MACROS=0: -DNETH_RL_MACROS=0, nomacros.c, raw controls only) */
#ifndef NETH_RL_MACROS
#define NETH_RL_MACROS 1
#endif
#if NETH_RL_MACROS
#include "../mods/macros/nether.h"
#include "../mods/macros/armor.h"
#include "../mods/macros/stronghold.h"
#include "../mods/macros/pearl.h"
#include "../mods/macros/obsidian.h"
#include "../mods/macros/bportal.h"
#endif

/* macros.py's kinds */
enum { M_NONE, M_SURFACE, M_SCREEN, M_LOOT, M_MINE, M_CRAFT };
enum { PV_OFF, PV_SURFACE, PV_STOCK }; /* food_provision's exclusive surface and stock stages */
/* macros._craft's plans (their names) and phases */
enum { C_PLANKS, C_TABLE, C_STICK, C_WPICK, C_SPICK, C_FURNACE, C_SMELT, C_IPICK, C_BUCKET, C_FLINT, C_DPICK, C_POWDER,
       C_EYE, C_FILL, C_CAST, C_PORTAL, C_FRAME, C_THROW, C_TAKE,
       C_ISWORD, C_SSWORD,      /* lane/nether: the sword plans (no pick names them: picks end at C_TAKE) */
       C_IBOOTS, C_ILEGS, C_ICHEST, C_IHELM,    /* lane/rod (armor_kit): the iron armor plans (the labeller's "other") */
       C_WSHOVEL };             /* lane/rod (snow_kit): the wooden shovel */
/* lane/rod (snow_kit): the snow's blocks and items */
enum { SNOW_LAYER = 78, SNOW_BLOCK = 80, SNOWBALL = 332, WOOD_SHOVEL = 269, STONE_SHOVEL = 273, IRON_SHOVEL = 256,
       DIAMOND_SHOVEL = 277 };
/* lane/rod (armor_kit): the iron armor's items */
enum { IRON_HELM = 306, IRON_CHEST = 307, IRON_LEGS = 308, IRON_BOOTS = 309 };
enum { CP_OPEN, CP_CLICKS, CP_CLOSE, CP_CLOSED, CP_WAIT };
/* the items and blocks macros.py names */
enum { LOG = 17, LOG2 = 162, PLANKS = 5, STICK = 280, TABLE = 58, COBBLE = 4, CHEST = 54 };
/* NETH_VEC_IRON's (lane/ironmacro) */
enum { FURNACE = 61, FURNACE_LIT = 62, IRON_ORE = 15, INGOT = 265, COAL = 263, FLINT = 318, BUCKET = 325,
       FLINT_STEEL = 259, GRAVEL = 13 };
/* NETH_VEC_LATE's (lane/latecraft) */
enum { DIAMOND = 264, BLAZE_ROD = 369, BLAZE_POWDER = 377, PEARL = 368, EYE = 381 };
/* NETH_VEC_LIQUID's (lane/liquidlook) */
enum { WATER_BUCKET = 326, LAVA_BUCKET = 327, OBSIDIAN = 49, IRON_PICK = 257, DIAMOND_PICK = 278 };
enum { END_FRAME = 120 };                   /* NETH_VEC_END's: metadata bit 4 the eye */
static const int16_t EXTRA_ITEMS[8] = {WATER_BUCKET, LAVA_BUCKET, OBSIDIAN, IRON_PICK, DIAMOND_PICK, BLAZE_ROD, PEARL, EYE};
/* lane/starts: neth_vec_opts.late_scalars' items, a row each (a row's ids summed): obsidian, the iron and diamond
 * pickaxes, the iron and diamond swords, blaze rods, blaze powder, pearls, eyes, the water and lava buckets, food (the
 * meats raw and cooked, bread); then the Nether and the End (NETH_VEC_LATE_N 14) */
#define LATE_NITEM 12
static const int16_t LATE_ITEMS[LATE_NITEM][7] = {
    {OBSIDIAN}, {IRON_PICK}, {DIAMOND_PICK}, {267}, {276}, {BLAZE_ROD}, {BLAZE_POWDER}, {PEARL}, {EYE}, {WATER_BUCKET},
    {LAVA_BUCKET}, {319, 363, 365, 320, 364, 366, 297}};
_Static_assert(LATE_NITEM + 2 == NETH_VEC_LATE_N, "the late scalars' row");

#define CRAFT_OPS 16            /* _recipe_ops' longest list (a pickaxe: 10) */
#define TRIED 64                /* the loot macro's chests tried (the scans keep NETH_RL_CHESTS) */

/* ---------------------------------------------------------------- tables */

/* semteach.ITEMS: the items whose counts the teacher sees */
static const int16_t TEACH_ITEMS[NETH_VEC_ITEMS] = {17, 162, 5, 280, 58, 270, 271, 269, 268, 4, 274, 275, 273, 272, 263, 15,
                                             265, 61, 3, 12, 13, 6, 260, 295, 318, 1, 14, 264, 56, 331, 325, 259};
/* reward.MILESTONES */
static const int16_t MS_ITEMS[NETH_VEC_MILESTONES] = {270, 274, 257, 278, 61, 265, 325, 259, 49, 369, 377, 368, 381};

/* reward.OBJECTIVES' weights in Reward.step's term order: items, looted,
 * logs, chest, progress, ach, death, milestone */
static const double WEIGHTS[4][8] = {
    {1.0, 0.0, 0.5, 2.0, 0.02, 1.0, -5.0, 2.0},     /* full */
    {0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0},       /* logs */
    {0.5, 0.0, 0.0, 2.0, 0.02, 0.0, -2.0, 1.0},     /* loot */
    {0.0, 1.0, 0.0, 5.0, 0.05, 0.0, -2.0, 0.0},     /* chests */
};

/* tables built once (vec_tabs_init) from macros.py's sets */
struct vec_tabs {
    uint8_t item_index[4096];   /* semteach.ITEM_INDEX */
    uint16_t msbit[4096];       /* the milestone bit an item sets */
    uint8_t hand[4096], lockable[4096], craft[4096];
    uint8_t craft_iron[4096];   /* NETH_VEC_IRON's and NETH_VEC_LATE's items that may start a craft plan */
    int8_t picky[4096];         /* PICKY: the pickaxe tier a block needs (0 none) */
    int8_t pick[4096], axe[4096], shovel[4096];   /* PICKS, AXES, SHOVELS: a tool's tier */
    int8_t sword[4096];                           /* SWORDS: a sword's tier (1 wood/gold, 2 stone, 3 iron, 4 diamond) */
    uint8_t build_block[4096], food[4096];        /* legal HUD hotbar classes (.hs): building blocks and edible food */
    int8_t tool_of[4096];       /* _best_tool's table for a block: 0 axes, 1 shovels, 2 pickaxes, -1 none */
};

static void vec_tabs_init(struct vec_tabs *T)
{
    memset(T, 0, sizeof *T);
    for (int i = 0; i < NETH_VEC_ITEMS; ++i) T->item_index[TEACH_ITEMS[i]] = (uint8_t)(i + 1);
    for (int i = 0; i < NETH_VEC_MILESTONES; ++i) T->msbit[MS_ITEMS[i]] |= (uint16_t)(1u << i);
    static const int16_t woody[] = {LOG, LOG2, PLANKS, TABLE, 47, 54, 85, 107, 96, 72};
    static const int16_t dirty[] = {2, 3, 12, 13, 82, 78, 80, 110, 88};
    static const int16_t hand[] = {18, 161, 31, 32, 37, 38, 39, 40, 59, 81, 83, 86, 103, 106, 111};
    static const int16_t picky[][2] = {{1, 1}, {4, 1}, {16, 1}, {24, 1}, {48, 1}, {67, 1}, {87, 1}, {89, 1}, {15, 2},
                                       {21, 2}, {14, 3}, {56, 3}, {73, 3}, {74, 3}, {129, 3}};
    static const int16_t picks[][2] = {{270, 1}, {285, 1}, {274, 2}, {257, 3}, {278, 4}};
    static const int16_t axes[][2] = {{271, 1}, {286, 1}, {275, 2}, {258, 3}, {279, 4}};
    static const int16_t shovels[][2] = {{269, 1}, {284, 1}, {273, 2}, {256, 3}, {277, 4}};
    for (int i = 0; i < 4096; ++i) T->tool_of[i] = -1;
    /* _best_tool: WOODY first, then DIRTY, then PICKY */
    for (size_t i = 0; i < sizeof picky / sizeof picky[0]; ++i)
    {
        T->picky[picky[i][0]] = (int8_t)picky[i][1];
        T->lockable[picky[i][0]] = 1;
        T->tool_of[picky[i][0]] = 2;
    }
    T->tool_of[FURNACE] = T->tool_of[FURNACE_LIT] = 2;  /* furnace_kit carry: a pickaxe can take either block */
    for (size_t i = 0; i < sizeof dirty / sizeof dirty[0]; ++i)
    {
        T->hand[dirty[i]] = T->lockable[dirty[i]] = 1;
        T->tool_of[dirty[i]] = 1;
    }
    for (size_t i = 0; i < sizeof woody / sizeof woody[0]; ++i)
    {
        T->hand[woody[i]] = T->lockable[woody[i]] = 1;
        T->tool_of[woody[i]] = 0;
    }
    for (size_t i = 0; i < sizeof hand / sizeof hand[0]; ++i) T->hand[hand[i]] = T->lockable[hand[i]] = 1;
    for (size_t i = 0; i < 5; ++i)
    {
        T->pick[picks[i][0]] = (int8_t)picks[i][1];
        T->axe[axes[i][0]] = (int8_t)axes[i][1];
        T->shovel[shovels[i][0]] = (int8_t)shovels[i][1];
    }
    static const int16_t swords[][2] = {{268, 1}, {283, 1}, {272, 2}, {267, 3}, {276, 4}};
    for (size_t i = 0; i < 5; ++i) T->sword[swords[i][0]] = (int8_t)swords[i][1];
    static const int16_t bblocks[] = {1, 2, 3, 4, 5, 12, 13, 24, 49, 80, 87, 112, 121};
    for (size_t i = 0; i < sizeof bblocks / sizeof bblocks[0]; ++i) T->build_block[bblocks[i]] = 1;
    static const int16_t foods[] = {260, 282, 297, 319, 320, 322, 349, 350, 357, 360,
                                    363, 364, 365, 366, 367, 391, 392, 393, 400};
    for (size_t i = 0; i < sizeof foods / sizeof foods[0]; ++i) T->food[foods[i]] = 1;
    static const int16_t craft[] = {LOG, LOG2, PLANKS, STICK, TABLE, COBBLE};
    for (size_t i = 0; i < sizeof craft / sizeof craft[0]; ++i) T->craft[craft[i]] = 1;
    static const int16_t craft_iron[] = {IRON_ORE, INGOT, FURNACE, FLINT, DIAMOND, BLAZE_ROD, BLAZE_POWDER, PEARL,
                                         BUCKET, WATER_BUCKET, OBSIDIAN, FLINT_STEEL, EYE};
    for (size_t i = 0; i < sizeof craft_iron / sizeof craft_iron[0]; ++i) T->craft_iron[craft_iron[i]] = 1;
}

/* numpy's float64 add.reduce of n contiguous values (pairwise_sum: a plain
 * loop under 8, the 8-way block up to 128; from the identity) */
static double npsum_d(const double *a, int n)
{
    double s;
    if (n < 8)
    {
        s = 0.;
        for (int i = 0; i < n; ++i) s += a[i];
    }
    else
    {
        double r[8];
        for (int j = 0; j < 8; ++j) r[j] = a[j];
        int i = 8;
        for (; i < n - n % 8; i += 8)
            for (int j = 0; j < 8; ++j) r[j] += a[i + j];
        s = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) s += a[i];
    }
    return 0.0 + s;
}

/* the same for one row of 36 float32 (inv_value's sum) */
static float npsum36(const float *a)
{
    float r[8];
    for (int j = 0; j < 8; ++j) r[j] = a[j];
    int i = 8;
    for (; i < 36 - 36 % 8; i += 8)
        for (int j = 0; j < 8; ++j) r[j] = r[j] + a[i + j];
    float s = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < 36; ++i) s = s + a[i];
    return 0.0f + s;
}

/* the tables neth_vec_tables gives, built once */
static float tab_values[4096];
static double tab_camdeg[NETH_VEC_CAM];
static pthread_once_t tab_once = PTHREAD_ONCE_INIT;

static void tables_build(void)
{
    /* reward.BASE (float32), then values(): every recipe's output lifted to
     * at least its inputs' value, until nothing moves */
    static const struct { double v; int16_t ids[8]; } base[] = {
        {1.0, {17, 162}}, {0.1, {4}}, {0.02, {3, 2, 12, 13}}, {0.02, {6}}, {0.5, {263}}, {2.0, {15}}, {1.5, {14}},
        {5.0, {56}}, {5.0, {264}}, {0.3, {331}}, {0.3, {73, 74}}, {0.3, {21}}, {0.1, {351}}, {1.0, {388}},
        {1.0, {129}}, {0.5, {49}}, {0.05, {318}}, {0.2, {287}}, {0.1, {288}}, {0.3, {289}}, {0.3, {334}},
        {0.2, {352}}, {0.2, {341}}, {0.05, {367}}, {2.0, {368}}, {3.0, {369}}, {1.0, {370}}, {0.3, {372}},
        {0.1, {375}}, {0.1, {296}}, {0.05, {338}}, {0.2, {344}}, {0.3, {260}}, {0.5, {297}},
        {0.2, {319, 363, 365, 349, 411, 423}}, {0.5, {329}}, {0.05, {81}}, {0.1, {82}}, {0.05, {337}}, {0.1, {88}},
        {0.02, {87}}, {0.2, {89}}, {0.05, {348}}, {0.2, {406}}, {0.1, {112}}, {0.1, {121}}, {0.01, {80, 78, 332}},
        {0.1, {35}}, {0.0, {18, 161}}, {0.02, {110}}, {0.1, {392, 391}}, {0.05, {361, 362}}, {0.1, {86, 103}},
    };
    static double v[4096];
    static struct neth_rl_recipe rec[1024];
    for (int i = 0; i < 4096; ++i) v[i] = 0.0;
    for (size_t i = 0; i < sizeof base / sizeof base[0]; ++i)
        for (int j = 0; j < 8 && base[i].ids[j]; ++j) v[base[i].ids[j]] = (double)(float)base[i].v;
    int nr = neth_rl_recipes(rec, 1024);
    if (nr > 1024) nr = 1024;
    for (int it = 0; it < 100; ++it)
    {
        int moved = 0;
        for (int q = 0; q < nr; ++q)
        {
            const struct neth_rl_recipe *r = &rec[q];
            int recolor = 0;
            double x[9];
            for (int j = 0; j < r->n; ++j)
            {
                recolor |= r->in_item[j] == r->out_item;
                x[j] = v[r->in_item[j] & 4095];
            }
            if (recolor) continue;
            double need = npsum_d(x, r->n) + (r->kind == 2 ? 0.5 / 8 : 0.0);
            double unit = need / (double)(r->out_count > 1 ? r->out_count : 1);
            int o = r->out_item & 4095;
            if (unit > v[o] + 1e-9)
            {
                v[o] = unit;
                moved = 1;
            }
        }
        if (!moved) break;
    }
    for (int i = 0; i < 4096; ++i) tab_values[i] = (float)v[i];
    /* pairs.mulaw_center: v = b / 40 * 2 - 1, sign(v) * ((1 + 20) ** |v| - 1) / 20 * 90 */
    for (int b = 0; b < NETH_VEC_CAM; ++b)
    {
        double x = (double)b / 40.0 * 2 - 1;
        double s = x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0;
        tab_camdeg[b] = s * (pow(21.0, fabs(x)) - 1) / 20.0 * 90.0;
    }
}

void neth_vec_tables(float values[4096], double camdeg[NETH_VEC_CAM])
{
    pthread_once(&tab_once, tables_build);
    memcpy(values, tab_values, sizeof tab_values);
    memcpy(camdeg, tab_camdeg, sizeof tab_camdeg);
}

/* ------------------------------------------------------------ CPython math */

/* Modules/mathmodule.c (3.12) vector_norm, for n finite values >= 0 whose
 * largest is max: math.dist and math.hypot */
static double vector_norm(int n, double *vec, double max)
{
    if (isinf(max)) return max;
    for (int i = 0; i < n; ++i)
        if (isnan(vec[i])) return NAN;
    if (max == 0.0 || n <= 1) return max;
    int max_e;
    frexp(max, &max_e);
    if (max_e < -1023)
    {
        for (int i = 0; i < n; ++i) vec[i] /= DBL_MIN;
        return DBL_MIN * vector_norm(n, vec, max / DBL_MIN);
    }
    double scale = ldexp(1.0, -max_e), csum = 1.0, frac1 = 0.0, frac2 = 0.0, x, h;
    for (int i = 0; i < n; ++i)
    {
        x = vec[i] * scale;
        double phi = x * x, plo = fma(x, x, -phi);
        double shi = csum + phi, slo = (csum - shi) + phi;
        csum = shi;
        frac1 += plo;
        frac2 += slo;
    }
    h = sqrt(csum - 1.0 + (frac1 + frac2));
    double phi = -h * h, plo = fma(-h, h, -phi);
    double shi = csum + phi, slo = (csum - shi) + phi;
    csum = shi;
    frac1 += plo;
    frac2 += slo;
    x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}


static double py_hypot2(double a, double b)
{
    double d[2] = {fabs(a), fabs(b)}, max = 0.0;
    for (int i = 0; i < 2; ++i)
        if (d[i] > max) max = d[i];
    return vector_norm(2, d, max);
}

/* math.degrees */
static double degrees(double x) { return x * (180.0 / 3.14159265358979323846); }

/* macros.look_at: yaw, pitch that put (x, y, z) under the crosshair from the eye */
static void look_at(double ex, double ey, double ez, double x, double y, double z, double *yaw, double *pitch)
{
    double dx = x - ex, dy = y - ey, dz = z - ez;
    double h = sqrt(dx * dx + dz * dz);
    *yaw = degrees(atan2(dz, dx)) - 90.0;
    double p = -degrees(atan2(dy, h));
    p = p < 90.0 ? p : 90.0;                /* min(90.0, p) */
    *pitch = p > -90.0 ? p : -90.0;         /* max(-90.0, ...) */
}

/* ------------------------------------------------------------- the state */

/* what the act phase reads of the last result (the pool's result is
 * cleared when the next step is queued) */
struct vec_last {
    double x, y, z;
    float yaw, pitch;
    int32_t flags, air, screen, nslots;
    int16_t slot_item[90], slot_count[90];
    int16_t cur_item, cur_count;
    int32_t dim, pad;
    int32_t food;               /* lane/food: the food level and health */
    int32_t fire, on_ground;    /* lane/lavaesc: the burning flag (the flames on the screen) and on the ground */
    float health;
    int64_t wtime;              /* lane/pearl: the day's time (the sky the screen shows) */
};

struct vec_tried { int32_t x, y, z; int64_t until; };     /* a cell (or chest) left alone until a step */

/* lane/ladder6 (neth_vec_level, mods/macros/shadow.c): the labeller's own copy of what a macro keeps between steps, run beside
 * an env playing at the student's level on that env's state and never stepped: the kind and age it would have, the
 * seek's fields (vec_env's seek .. gpz), and its own memo of cells given up */
/* lane/ladder11: the craft labeller's run (mods/macros/shadow.c shc_*): on, and the craft macro's fields it swaps in (vec_env's
 * own names; age is cage), its own queue of GUI ops, its memory of the table and furnace the crosshair has shown */
struct vec_shc {
    int con, cage, cplan, cphase, pressed, used, aimed, placed, has_ops, nops, op_at, ccap;
    int pdir, ptries, pdig, pdx, pdy, pdz, has_table, has_furnace;
    double tx, ty, tz, fx, fy, fz;
    struct neth_rl_op ops[CRAFT_OPS];
    struct neth_rl_op gq[NETH_RL_OPS];
    int gq_n, gq_at;
};
enum { SHC_RUN, SHC_TIDY, SHC_CLOSE, SHC_ROLLBACK, SHC_RESET, SHC_START, SHC_DONE, SHC_SAME, SHC_RELEASE, SHC_NB };
#define SHC_NPLAN 22            /* the craft plans (C_PLANKS .. C_SSWORD) and one more for any other */

struct vec_shd {
    int kind, age, seek, gx, gy, gz, ggain, gstuck, gbefore, gon, gdug, ntried;
    double gbest, gpx, gpz;
    struct vec_tried tried[TRIED];
    int surf, sage;     /* lane/ladder9: the surface labeller's run (on, its age) */
    struct vec_shc c;   /* lane/ladder11: the craft labeller's */
    int64_t cb[SHC_NB]; /* lane/ladder11: its books (neth_vec_shc_books) */
    int pk12, puse;     /* lane/ladder11: the student's key 12 and use held on its last step (a press is on the down
                         * step: held the step before, the label is the release) */
    int64_t cp[SHC_NPLAN][6];   /* lane/ladder11: its run's labels by plan and what they ask, and the equal acts */
    int sfirst, skrun, swhy, sph, sphl;     /* lane/ladder18: the seek's other fields seek_begin and seek write */
};

#ifdef FIRE_TRACE
#define FR_RING 40
struct fr_rec { int64_t step; double x, y, z; float pitch, hp; int gr, kind, mbit, mo_hit, mo[3], mob, under; uint32_t hold; };
#endif

/* lane/pground (pearl_floor watch, mods/macros/pfloor.c): the shelter's cell and the rule's memory, zeroed at a reset */
#define PFL_NBAD 8
struct pfl_env {
    int set, kills, has_last, drop, dsteps, home_n, tid, tst, nbad, bad[PFL_NBAD], wander_set, wander;
    int64_t hit, home_off, sweep, swings, stares, bad_until[PFL_NBAD];
    double hx, hz, lx, lz, dx, dz;
    float yaw0;
};

struct vec_env {
    struct vec_last last;
#ifdef FIRE_TRACE
    struct fr_rec fr[FR_RING];          /* lane/flint (a trace build): the last decided acts, a ring */
    int fr_n, fr_was;                   /* acts written; burning on the step before */
#endif
    /* macros.EnvMacro */
    int kind, age;
    int mx, my, mz, mblock;                 /* the mine lock's block */
    int fid, flost, flast;                  /* NETH_VEC_FIGHT: the lock's living (0: a block), steps out of view, last hit */
    /* lane/deadhour: the policy's own stall anchor (a spot it has not left for istep steps: nothing mined, the
     * keys jammed): a new strip branch started from it (the recorded hour mined nothing for 50 of its 60 minutes) */
    double idx, idy, idz;
    int istep;
    int strip, sset, sdir, sk, slook, sgain, sturns;  /* NETH_VEC_STRIP: on; the direction set this episode; the
                                             * direction (0..3: yaw 0, 90, 180, 270); the cell being cleared (0 head,
                                             * 1 feet, 2 below, 3 the walk, -1 the next set to pick); the age its look
                                             * began; the age of the last progress; quarter turns this episode */
    int scx, scy, scz;                      /* the next cell along (its column) and the feet's y it was picked at */
    int shaz;                               /* NETH_VEC_STRIP: quarter turns at a bad floor since a cell was walked into */
    int64_t soff;                           /* NETH_VEC_STRIP: off until this step (a bad floor every way it turned) */
    int st_pack;                            /* lane/chain2: a late pack trim before the stronghold's food and sword kit */
    int stf, stf_age;                       /* lane/strip2 (NETH_VEC_STRIP2): the inventory screen's errand (0 none,
                                             * ST_FETCH a pickaxe to the hotbar, ST_DROP the surplus out), the age
                                             * its key was pressed */
    int64_t stf_off;                        /* lane/strip2: no errand begun before this step (the last one failed) */
    /* lane/strip2's books (mods/macros/strip2.c st_observe; kept with the switch off too, no act reads them): each good's
     * count held last step and the units it rose by since the episode began (coal, iron ore, gold ore, diamonds,
     * redstone, cobblestone), the cells the strip walked into, the step and place of the last, the strip's steps, the
     * diamond pickaxes held (the strip's last want met) */
    int32_t st_had[6], st_got[6], st_cells, st_dpick;
    int64_t st_last, st_steps;
    float st_lpos[3];
    int32_t st_held[7], st_table, st_err[4];    /* lane/strip3's books (struct neth_vec_strip's held, table, errand) */
    int64_t st_out_steps, st_dpick_step;        /* and its out_steps, the step the diamond pickaxe was first held */
    int64_t st_dia[NETH_VEC_DIA_N];                        /* lane/diamond's link books (strip2.c st_dia_books: struct neth_vec_strip dia) */
    int32_t st_dia_on, st_dia_pk, st_dia_cell[3];   /* and their state: counting, pickaxes held last step, the ore cell */
    /* lane/diamond (climb_way pillar, lane/pearlhunt's st3_pillar, strip2.c): the level's phase, its jumps that came
     * down short, the level's first step, the look's step, no pillar before cw_off, the pillar's last step (a gap begins
     * the level again), the jump's base and peak, levels done and given up, give-ups by why */
    int cw_ph, cw_fail;
    int64_t cw_t, cw_look, cw_off, cw_last;
    double cw_jbase, cw_jpeak;
    int32_t cw_levels, cw_quit, cw_qwhy[5];
    int32_t cw_cx, cw_cz;                   /* lane/bportal (climb_land centre): the column the level began on */
    int64_t st3_climb_at;                   /* lane/diamond (climb_swim): the strip's last climb step (m3 set) */
    int32_t st3_swims;                      /* and the swims climb_swim began (books) */
    /* lane/strip3 (NETH_VEC_STRIP2, mods/macros/strip2.c): the climb the strip drove last (0 none, ST3_WOOD the wood errand,
     * ST3_OUT out of the depth once the strip's want ended), the set's mode (ST3_UP, ST3_FLAT, ST3_DOWN), the climb
     * out begun and not yet at the surface; the step it began, the step the surface was reached (-1 not yet), the
     * step a falling block was last dug in the set */
    int st3, st3_m, st3_out;
    int64_t st3_at, st3_top, st3_fall;
    float st3_lhp;                          /* lane/strip3's guard: the health last step, the step of the last hurt */
    int64_t st3_hurt, st3_back;             /* with no fire or lava, the step the look last turned back for it */
    int hunt, hmeat, eat, efood;            /* lane/food: the fight lock a hunt (the meat held at its start); eating
                                             * (the food level at its start) */
    float hx, hy, hz;                       /* the hunted animal's last place in view (its drop is picked up there) */
    int64_t hunt_off;                       /* no hunt begun before this step (the last one came back empty) */
    int safe[8][3], nsafe, esc;             /* lane/lavaesc: the last cells stood on out of fire and water (newest
                                             * last); the escape from lava running (the surface macro's kind) */
    int64_t esc_off;                        /* no escape begun before this step (the last one found no way out) */
    int esl[3], esl_n, esl_done;            /* lane/flint (lava_escape seal): the cell being sealed, the steps spent on
                                             * it, the escape age the sealing gave up at (0: still sealing) */
    int64_t lava_t;                         /* the step of the last lava hit (burning, 3 or more lost: lava's 4) */
    float lhp;                              /* the health the step before */
    int seek, gx, gy, gz, ggain, gstuck, gbefore;   /* NETH_VEC_SEEK: 1 + the seen kind (0: none), the cell, the age of
                                                     * the last gain, steps without moving, the item held at the start */
    int gon, gdug;                          /* the crosshair on the cell's ore last step; the age it was dug (0: not) */
    double gbest, gpx, gpz;                 /* the nearest it has been, the position a step ago */
    int lx, ly, lz, lphase, lwait;          /* the loot macro's chest */
    int cplan, cphase, pressed, used, aimed, placed;
    int has_ops, nops, op_at;               /* "ops" in d: _recipe_ops' list, nops of it, from op_at */
    struct neth_rl_op ops[CRAFT_OPS];
    /* lane/ladder10 (actions.gui cursor): a macro's GUI ops past the step's first, one a step from gq_at; the kind and
     * label switch of the macro that made them */
    struct neth_rl_op gq[NETH_RL_OPS];
    int gq_n, gq_at, gq_kind, gq_mbit, gq_half;
    int chalf;                              /* lane/ladder12: the craft macro's half this call acted in (1 table, 2
                                             * recipe; 0 not a crafting plan): craft() sets it */
    int chand;                              /* lane/ladder12: NETH_VEC_NORECIPE's hand-off, the step the policy's */
    int rfirst;                             /* lane/ladder12: the run's recipe half has acted (its first step: half 3) */
    int rjump;                              /* lane/ladder12: the policy's jump key this step (NETH_VEC_RECIPEKEY) */
    int tkey;                               /* lane/ladder15: the policy's key 12 this step (NETH_VEC_TABLEKEY) */
    int tfirst;                             /* lane/ladder15: the run's table half has acted (its first step: half 4) */
    int skey;                               /* lane/ladder16: the policy's key 13 this step (NETH_VEC_SEEKKEY) */
    int sfirst;                             /* lane/ladder16: the seek begun and not yet acted (its first step: half 6) */
    int skrun;                              /* lane/ladder16: the seek running was started by the key (books) */
    int swhy;                               /* lane/ladder16: why the seek's last return 1 ended it (books: 1 got, 2
                                             * dug and not picked up, 3 given up; lane/ladder18: 4 its cell left the
                                             * macro's half, actions.seek_walk or seek_dig policy) */
    int sph, sphl;                          /* lane/ladder18: the seek's half this call acted in (1 the walk, 2 the
                                             * dig) and the call before's (0 the seek's first) */
    int64_t gb[NETH_VEC_GUI_BOOKS];        /* lane/ladder10: neth_vec_gui_books' counts, this env's */
    int gscr;                               /* lane/ladder13: the screen gui_books saw last step */
    int gk12;                               /* lane/ladder14: the action's key 12 gui_books saw last step */
    int lfits;                              /* lane/ladder14: this step's NETH_VEC_LBL_FITS (window_fits) */
    /* lane/ladder17 (actions.recipe_2x2 clicks): the 2x2 labeller's run under the policy's own inventory (x2_label):
     * its plan's ops (recipe_ops from the tidy window), the next one to label, the run's state (0 none, 1 clicking,
     * 2 the result taken: the close next, 3 off the run: the close) */
    struct neth_rl_op x2ops[CRAFT_OPS];
    int x2n, x2at, x2st, x2plan;
    /* Macros' per env */
    int64_t steps;
    int ntried;
    struct vec_tried tried[TRIED];
    int has_table;
    double tx, ty, tz;
    int has_furnace;                        /* NETH_VEC_IRON: the furnace the smelt plan placed or used */
    double fx, fy, fz;
    int ccap;                               /* the craft macro's age limit (80; a smelt's more) */
    int pdir, ptries, pdig, pdx, pdy, pdz;  /* NETH_VEC_IRON's placing: the side tried, sides tried, the dig's block */
    /* Reward */
    float prev_val, prev_dist;
    int64_t prev_logs, prev_opened, prev_near;
    uint64_t prev_ach, got;
    int16_t inv0_id[36];                    /* lane/curric2 (reward_rel): the item ids held at the episode's first */
    int64_t inv0_n[36];                     /* step and each one's count there, the items term's zero */
    int ninv0;
    int start;                              /* the start this episode began from */
    int scat;                               /* lane/seedwin: its record's start (0 the make's, 1 + k the mix's
                                             * starts[k], nstart + 1 a saved one) */
    FILE *tape;                             /* lane/tape: this episode's script, open (neth_vec_tape) */
    int tape_arm;                           /* lane/tape: open one at the next episode from the config's start */
    char tape_path[512];
    int want_save, saved;                   /* the save condition met this episode; a save made of it */
    uint32_t ms0, saved_ms, carry_ms;       /* lane/msstarts: the mask's milestones begun with, saved for, and
                                             * reached before the saved start this episode plays */
    int save_level, level;                  /* lane/msstarts: the save's level; the start's (0: not a saved one) */
    int dims;                               /* the episode record's dims */
    int ncells;                             /* lane/explore: the cells paid this episode, keys in cells (0 empty) */
    uint64_t cells[2 * NETH_VEC_EXPLORE_MAX];
    int32_t seen_steps[NETH_RL_SEEN];          /* the episode record's seen */
    /* NETH_VEC_PORTAL's build: the step, its wait, the frame's origin and axes, the count before a use */
    int pp_step, pp_wait, pp_tries, pp_x, pp_y, pp_z, pp_fx, pp_fz, pp_rx, pp_rz, pp_before, pp_dig;
    int pp_dim;                             /* the dimension the build began in (lane/chain: the Nether's way home) */
    double pp_base;                        /* a tower jump's feet height before it */
    int64_t pp_retry;                       /* no portal plan before this step (a build given up) */
    int64_t take_retry;                     /* NETH_VEC_CARRY: no take before this step (after a failed one) */
    int take_before;                        /* NETH_VEC_CARRY: tables held when the take began */
    int take_f;                             /* lane/chain2 (furnace_kit carry): the take is the furnace's, not the table's */
    uint64_t srng;                          /* the start draws' state (splitmix64) */
    int pin;                                /* lane/rawskill start_pin: the first draw's start + 1 (0: none yet) */
    int cyc;                                /* lane/eval1k start_cycle: the episodes drawn so far */
    int32_t prev_screen;
    int fresh;
    /* Loop */
    uint32_t prev_hold;
    int64_t prev_cam[2];
    float prev_keys[NETH_VEC_KEYS];
    int prev_macro;
    int64_t te;
    int64_t eplen;                          /* lane/stagelen: this episode's length in ticks (its start's) */
    double ret, x0, z0;
    int64_t deaths;
    int64_t macro_n[NETH_VEC_MACROS];
    int64_t ep_steps;
    /* the vector env's own */
    int reset_step;                         /* this step is a reset's: no act, no reward out */
    int ended;                              /* the clock ran out with the last step: recv resets */
    struct neth_vec_episode rec;
    struct pool_act cur;                    /* the step's act (its later ticks hold its holds) */
    uint64_t ns;                            /* the hooks' wall time */
    int64_t policy_steps, reset_steps;
    int lmbit;                              /* lane/ntbc: the macro that drove this step, by its switch (mbit_of) */
    struct neth_vec_db db;                  /* lane/dbench: the benchmark's books (neth_vec_opts.dbench) */
    struct neth_vec_food fb;                /* lane/food2: the food census (with the dbench books) */
    int fb_pmeat, fb_pother, fb_hunt;       /* ... the meat and other food held the step before, a hunt running */
    double fb_px, fb_pz;                    /* ... and the step before's place */
    int fs_on, fs_n, fs_sweep, fs_leg;      /* lane/food2 (food_scout): running, scouts begun, a turn on the spot, the
                                             * spiral's leg */
    int64_t fs_off, fs_legt, fs_mt;         /* ... no scout before; the leg's (or turn's) first step; the last step it
                                             * moved a block */
    double fs_yaw, fs_x, fs_z, fs_mx, fs_mz;    /* ... the leg's heading, its start, where it last moved a block */
    double fs_sx, fs_sz, fs_far;            /* ... the scout's start and its farthest from it (the census) */
    int fs_home, fs_hn, fs_det;             /* ... low bits: food-scout home; high bits: opt-in provision latch/roam */
    double fs_hx, fs_hz;                    /* ... home: where the scout began */
    int fs_trip, fs_tmeat, hk0;             /* ... the census: a trip running and its meat last step; the kill stat at
                                             * a hunt's start */
    int hpk;                                /* ... food_chase far: the drop search's step (0: not begun) */
    int fs_tripped;                         /* ... the census: a trip has ended this episode */
    int wd, wd_ph, wd_leg, wd_run;          /* lane/wood (wood_search): running, its phase (WD_*), the spiral's leg, the
                                             * run's steps */
    int64_t wd_t, wd_off, wd_mt;            /* ... the turn's first step, no run before, the last step it moved a block */
    double wd_yaw, wd_x, wd_z, wd_mx, wd_mz;    /* ... the leg's heading, its start, where it last moved a block */
    int64_t wd_seeks, wd_sweeps, wd_turns, wd_steps;    /* ... books: seeks begun, turns on the spot, heading turns,
                                                         * steps driven */
    struct walkto_st fo_wt;                 /* lane/chain: food_oracle's walk to the census's animal (PRIVILEGED) */
    double fo_base, fo_ty;                  /* ... its pillar out of a pit: the jump's floor; the pillar's first */
    int fo_tw;                              /* ... the pillar on */
    int do_on, do_x, do_y, do_z;            /* lane/chain: diamond_oracle's target ore (PRIVILEGED; strip2.c) */
    int64_t do_at, do_t0, do_hold;          /* ... its last scan, the target's first step, steering held until */
    int do_turns, do_last, do_nbad;         /* ... the strip's turns at the target's pick and at the last set; the
                                             * given-up count */
    int64_t do_picks, do_gaveup;            /* ... the books: targets picked, given up */
    int do_bad[4][3];                       /* ... the targets given up (a ring) */
    double db_ymax, db_py;                  /* ... the feet's highest y since last on the ground; the step before's y */
    int db_pg, db_pfire, db_pair, db_pfood; /* ... the step before's on_ground, fire, air and food */
    int db_pdim;                            /* ... and dimension */
    double st_x, st_y, st_z;                /* lane/speedrun-recipe: the stall's first step's feet (NAN: none yet) */
    int st_dim;                             /* ... its dimension */
    int64_t st_te0;                         /* ... its first step's te */
    uint32_t st_have;                       /* ... the milestones and dimensions held then */
    float st_val;                           /* ... the items' value then (the highest since) */
    int lhalf;                              /* lane/ladder12: and the craft macro's half (neth_vec_label.half) */
    int dragon;                             /* lane/dragon: the mine kind's End fight running (vecdragon.c) */
    struct vec_dragon dg;                   /* lane/dragon: its memory and books, zeroed at a reset */
    struct dragon_in dgin;                  /* lane/dragon: the player as this step's act reads it */
#if NETH_RL_MACROS
    struct nt_env nt;                       /* lane/nether: NETH_VEC_NETHER's */
    int chamber_on, chamber_x, chamber_y, chamber_z;  /* lane/chain2: a searched safe room target this episode */
    int exit_cover_done;                     /* lane/chain2: seven rods' covered descent completed */
    int64_t exit_cover_at;                   /* its first step, for the 100-step survival book */
    struct arm_env arm;                     /* lane/rod: armor_kit's (mods/macros/armor.c) */
    int nt_xportal[4];                      /* lane/rod: a case's portal cell (set, x, y, z; neth_vec_nether_portal) */
    struct sh_env sh;                       /* lane/stronghold: NETH_VEC_STRONGHOLD's */
    struct pearl_env pearl;                 /* lane/pearl: NETH_VEC_PEARL's (csrc/mods/macros/pearl.c) */
    struct pfl_env pfl;                     /* lane/pground: pearl_floor's (csrc/mods/macros/pfloor.c) */
    struct obs_env ob;                      /* lane/obsidian: NETH_VEC_OBSIDIAN's */
    struct bp_env bp;                       /* lane/obsidian3: NETH_VEC_BPORTAL's */
#endif
    int lvl;                                /* lane/ladder6 (neth_vec_env_level): 1, this env plays at the student's
                                             * level (neth_vec lv's macros) with the labeller beside it */
    struct vec_shd shd;                     /* lane/ladder6: the labeller's state (mods/macros/shadow.c) */
    struct task_env tk;                     /* lane/rawskill: the task's books (vecenv_task.c) */
    int hint_ok;                            /* lane/rawsearch (task_hint): the scan's nearest task block this step */
    int32_t hint[3];
    /* lane/chain (neth_vec_opts.chain): the last result, the next episode's start (-1 drawn) and length, the decisions
     * left decoded with no macro */
    struct neth_vec_chain_peek cpk;
    int cnext;
    int64_t cnext_len;
    int cfree;
    const struct neth_rl_act *cplay;           /* neth_vec_chain_play's acts (the caller's), the next one, how many */
    int64_t cplay_at, cplay_n;
    /* lane/pdig (counting only, neth_vec_lsrc): which branch of the step's dispatch made its act (VLS_*) */
    int32_t lsrc;
    int32_t lsub;                           /* ... and provision's wood step's own branch (VLW_*, + 8 the craft route) */
    int32_t wk, wkd, wky, wkn, wkf, wkc;    /* lane/pdig (counting only, neth_vec_walk): macros.c WK_STAMP, WK_FRONT */
#if NETH_RL_MACROS
    struct arm_choice ach;                  /* lane/choice (books only, never saved): armor.c arm_choose's */
#endif
    /* lane/hill-rnn-hotbar (never saved): server dead End crystal pillar (x,z) set for opt-in crystal_weight */
    int32_t cry_dead_n;
    int32_t cry_dead_xz[NETH_RL_END_CRYSTALS][2];
};

/* lane/dragon: the player as the End fight reads it, and the End in view (endview.h), for env e */
static const struct neth_rl_end *dragon_view(const struct neth_vec *v, const struct vec_env *e);

enum { G_FLIGHT, G_READY, G_RECEIVED };

/* lane/msstarts: what the macros knew when a start was saved from an env (the table and furnace it placed), given
 * back to the env that plays it: a run taken up again knows where its own table and furnace stand; and the save
 * mask's milestones it had reached (a placed furnace is held no more), so a level only grows along a chain of starts */
struct start_mem { int has_table, has_furnace; double tx, ty, tz, fx, fy, fz; uint32_t ms; };

/* lane/macroopts: the macros' options (neth_vec_opts.macro_opts; MOPTS below has each one's words and default) */
enum {
    MO_SH_LOCATE,           /* stronghold_locate: SHL_* */
    MO_LAVA_SENSE,          /* lava_sense: LS_* (obsidian, bportal and nether: lava_seen) */
    MO_NT_FAN, MO_NT_GOALS, MO_NT_ANY_BLAZE, MO_NT_NEAR_STOP, MO_NT_COVER, MO_NT_HOOD, MO_NT_GLANCE, MO_NT_FOOTING,
    MO_NT_BRICK_PICK, MO_NT_PICK_FETCH,
    MO_NT_FORT_STAIR, MO_NT_FORT_REJOIN, MO_NT_SKEL_FIGHT, MO_NT_FAR_SPAWNER, MO_NT_BLAZE_FAR, MO_NT_FRONTIER, MO_NT_HOOD_ANY, MO_NT_FORT_UP, MO_NT_SHY,  /* lane/nether5 */
    MO_NT_LAVA_FLOOR, MO_NT_COVER_SEARCH, MO_NT_SKEL_TOWER, MO_NT_CAMP_GIVEUP,   /* lane/nether5 */
    MO_OB_DEEP, MO_OB_EARLY_FILL, MO_OB_DESCEND, MO_OB_CAST_SOURCE, MO_OB_LOOK, MO_OB_SEARCH,
    MO_OB_FLINT,            /* lane/obsidian7: obsidian_flint: first, later */
    MO_OB_WATER,            /* lane/obsidian7: obsidian_water: search, sky */
    MO_SURF_CEIL,           /* lane/obsidian7: surface_ceiling: off, dig */
    MO_OB_DROP,             /* lane/obsidian7: obsidian_drop: off, on */
    MO_OB_CAST_WAY,         /* lane/obsidian7: obsidian_cast_way: walk, strip */
    MO_OB_FOOD,             /* lane/obsidian7: obsidian_food: off, hunt */
    MO_OB_CAST_RIM,         /* lane/obsidian7: obsidian_cast_rim: off, on */
    MO_OB_SEA,              /* lane/obsidian7: obsidian_sea: turn, stop */
    MO_DG_LOB, MO_DG_CRIT, MO_DG_BLOCK,
    MO_PEARL_ZOMBIE_PILLAR, MO_PEARL_DROP_CALM,
    MO_PEARL_WHEN, MO_PEARL_YIELD,  /* lane/pearlgate: mods/macros/pearl.c PW_* */
    MO_BP_ROAM,             /* lane/obsidian5: bportal_roam: BR_* */
    MO_BP_NIGHT,            /* lane/obsidian5: bportal_night: BN_* */
    MO_BP_BUCKETS,          /* lane/obsidian6: bportal_buckets: BB_* */
    MO_SH_NIGHT,            /* lane/stronghold3: stronghold_night: SHN_* */
    MO_SH_ROOF,             /* lane/stronghold3: stronghold_roof: SHR_* */
    MO_SH_SPRINT,           /* lane/stronghold3: stronghold_sprint: SHS_* */
    MO_SH_WALL,             /* lane/stronghold3: stronghold_wall: SHW_* */
    MO_SH_TUNNEL,           /* lane/stronghold3: stronghold_tunnel: SHT_* */
    MO_SH_THROW_KEY,        /* lane/stronghold3: stronghold_throw_key: SHK_* */
    MO_SH_ROOM_WALK,        /* lane/stronghold3: stronghold_room_walk: SHM_* */
    MO_SH_NIGHT_NEAR,       /* lane/stronghold3: stronghold_night_near: SHNN_* */
    MO_SH_STUCK,            /* lane/stronghold3: stronghold_stuck: SHU_* */
    MO_SH_HUNT,             /* lane/stronghold3: stronghold_hunt: SHH2_* */
    MO_SH_FLESH,            /* lane/stronghold3: stronghold_flesh: SHF_* */
    MO_BP_DPICK,            /* lane/obsidian7: bportal_dpick: BD_* */
    MO_POLICY_TOOL,         /* lane/ladder2: policy_tool: off, on */
    MO_MINE_TOOL,           /* lane/ladder2: mine_tool: off, on */
    MO_POLICY_HOLD,         /* lane/ladder4: policy_hold: off, on */
    MO_SEEK_REACH,          /* lane/ladder5: seek_reach: SKR_* */
    MO_GUI,                 /* lane/ladder10: gui: GUI_OFF, GUI_CURSOR */
    MO_POLICY_PLACE,        /* lane/ladder10: policy_place: off, on */
    MO_PRESS_LABEL,         /* lane/ladder11: press_label: PRL_* */
    MO_TABLE_CHANCE,        /* lane/ladder15: table_chance: off, on */
    MO_TABLE_KEY,           /* lane/ladder15: table_key: TK_* */
    MO_SEEK_CHANCE,         /* lane/ladder16: seek_chance: off, on */
    MO_SEEK_WALK,           /* lane/ladder18: seek_walk: macro, policy */
    MO_SEEK_DIG,            /* lane/ladder18: seek_dig: macro, policy */
    MO_RECIPE_2X2,          /* lane/ladder17: recipe_2x2: R2_KEY, R2_CLICKS */
    MO_X2_SOURCE,           /* lane/ladder17: x2_source: X2S_FIRST, X2S_ANY */
    MO_PLACE_DIG,           /* lane/obsid: place_dig: any, skip */
    MO_OB_TRIP,             /* lane/obsid: obsidian_water_trip: off, on */
    MO_OB_STRIP_CAST,       /* lane/obsid: obsidian_strip_cast: off, on */
    MO_OB_POOL_MIN,         /* lane/obsid: obsidian_pool_min: off, on */
    MO_OB_FACES,            /* lane/obsid2: obsidian_faces: off, on */
    MO_OB_GLANCE,           /* lane/obsid2: obsidian_glance: off, walk */
    MO_OB_WALK,             /* lane/obsid2: obsidian_walk: line, around */
    MO_OB_YIELD,            /* lane/obsid2: obsidian_yield: off, on */
    MO_OB_RESUME,           /* lane/obsid2: obsidian_resume: here, line */
    MO_OB_TRIP_REACH,       /* lane/obsid2: obsidian_trip_reach: near, far */
    MO_OB_TRIP_NIGHT,       /* lane/obsid2: obsidian_trip_night: off, on */
    MO_PT_BASE,             /* lane/obsid2: portal_base: any, ground */
    MO_PT_TOWER,            /* lane/obsid2: portal_tower: count, stand, centre, ground (lane/bportal) */
    MO_PT_ROOM,             /* lane/obsid2: portal_room: frame, wide */
    MO_PT_RETURN,           /* lane/chain2: portal_return: off, on (walk back to the build base after knockback) */
    MO_PT_BRICK_PICK,       /* lane/chain2: portal_brick_pick: off, on (pickaxe for Nether brick in the portal room) */
    MO_FOOD_EAT,            /* lane/food2: food_eat: FE_* */
    MO_FOOD_HUNT,           /* lane/food2: food_hunt: FH_* */
    MO_FOOD_BREAK,          /* lane/food2: food_break: off, on */
    MO_FOOD_SCOUT,          /* lane/food2: food_scout: FS_* */
    MO_FOOD_CHASE,          /* lane/food2: food_chase: FC_* */
    MO_FOOD_COOK,           /* lane/food2: food_cook: FK_* (mods/macros/craft.c's smelt) */
    MO_FOOD_FUEL,           /* lane/chain2: food_fuel: wood for the stronghold stock's cook */
    MO_FOOD_TRIPS,          /* lane/food2: food_trips: FT_* */
    MO_STRIP_WOOD,          /* lane/diamond: strip_wood: SW_DIGS, SW_CHAIN */
    MO_DIA_FIRST,           /* lane/diamond: dia_first: off, on */
    MO_CLIMB_WAY,           /* lane/diamond (lane/pearlhunt's): climb_way: CW_STAIR, CW_PILLAR */
    MO_CLIMB_SWIM,          /* lane/diamond: climb_swim: off, on */
    MO_STRIP_WOOD_NIGHT,    /* lane/diamond: strip_wood_night: SWN_GO, SWN_WAIT */
    MO_NT_SAFE_DIG,         /* lane/rod: nether_safe_dig: off, on, deep */
    MO_NT_SPREAD,           /* lane/rod: nether_spread: off, on */
    MO_NT_SEARCH_Y,         /* lane/rod: nether_search_y: 32, 40, 48 */
    MO_NT_SEAL_AIM,         /* lane/rod: nether_seal_aim: off, on */
    MO_NT_FEET_FILL,        /* lane/rod: nether_feet_fill: off, on */
    MO_NT_HOOD_FULL,        /* lane/rod: nether_hood_full: off, on */
    MO_NT_ARRIVE_FILL,      /* lane/rod: nether_arrive_fill: off, on */
    MO_NT_HOOD_HIT,         /* lane/rod: nether_hood_hit: off, on */
    MO_NT_ESC_FIRST,        /* lane/rod: nether_esc_first: off, on */
    MO_NT_OFF_HOLD,         /* lane/rod: nether_off_hold: off, on */
    MO_NT_LAVA_BACK,        /* lane/rod: nether_lava_back: off, on */
    MO_NT_PORTAL_EXIT,      /* lane/rod: nether_portal_exit: off, on */
    MO_NT_ENTRY_FILL,       /* lane/rod: nether_entry_fill: off, on */
    MO_NT_DROPS,            /* lane/rod: nether_drops: off, on */
    MO_NT_BLAZE_SEEK,       /* lane/rod: nether_blaze_seek: off, on */
    MO_NT_SEARCH,           /* lane/rod: nether_search: 0 tunnel, 1 surface */
    MO_NT_SWEEP,            /* lane/rod: nether_sweep: 0 one, 1 back, 2 draw, 3 minus */
    MO_NT_HOOD_HP,          /* lane/rod: nether_hood_hp: 0 8, 1 14 */
    MO_NT_ORACLE,           /* lane/rod: nether_oracle (PRIVILEGED): 0 off, 1 fortress, 2 spawner, 3 home */
    MO_NT_START,            /* lane/chain2: nether_start: off, locate (start), throne (generated blaze room) */
    MO_NT_ENTRY,            /* lane/chain2: nether_entry: off, sealed (enclosed piece at its standing height) */
    MO_NT_SP_TUN,           /* lane/chain2: nether_spawner_tunnel: off, on (sealed route to the blaze room) */
    MO_NT_SP_BREAK,         /* lane/chain2: nether_spawner_break: off, on (break the cage after seven rods) */
    MO_NT_CHAMBER,          /* lane/chain2: nether_chamber: off, on (build the return frame inside the home tunnel) */
    MO_NT_EXIT_COVER,       /* lane/chain2: nether_exit_cover: off, on (a hard seven-rod switch into a covered shaft) */
    MO_NT_TRIM,             /* lane/chain2: nether_trim: off, on (clear junk slots before the spawner fight) */
    MO_NT_ORACLE_LOCK,      /* lane/rod: nether_oracle_lock (PRIVILEGED): off, on */
    MO_CRAFT_RETURN,        /* lane/pearlgo (lane/pearlhunt's): craft_return: off, on */
    MO_NETHER_RODS,         /* lane/pearlgo: nether_rods: 0 seven, 1 six */
    MO_PEARL_NIGHT_FIRST,   /* lane/pearlgo (lane/pearlhunt's): pearl_night_first: off, on */
    MO_PEARL_WALK,          /* lane/pearlgo (lane/pearlhunt's): pearl_walk: PWK_* (mods/macros/pearl.c) */
    MO_NETHER_DONE,         /* lane/pearlgo (lane/pearlhunt's): nether_done: 0 dims, 1 held */
    MO_PEARL_LOOK,          /* lane/pearlsee: pearl_look: PLK_* (mods/macros/pearl.c) */
    MO_PEARL_TOWER,         /* lane/pearlsee: pearl_tower: off, on */
    MO_PEARL_DUSK,          /* lane/pearlsee: pearl_dusk: off, on */
    MO_PEARL_SEE,           /* lane/pearlsee: pearl_see: PSE_* (mods/macros/pearl.c) */
    MO_PEARL_DIP,           /* lane/pearlsee: pearl_dip: PDP_* (mods/macros/pearl.c) */
    MO_PEARL_SKY,           /* lane/pearlsee (2nd pass): pearl_sky: off, on */
    MO_PEARL_FETCH,         /* lane/pearlsee (2nd pass): pearl_fetch: PFE_* (mods/macros/pearl.c) */
    MO_PEARL_LURE,          /* lane/pearlsee (2nd pass): pearl_lure: off, on */
    MO_PEARL_HIGH,          /* lane/pearlsee (2nd pass): pearl_high: off, on */
    MO_PEARL_RESTOCK,       /* lane/pearlsee (3rd pass): pearl_restock: off, on */
    MO_PEARL_HEIGHT,        /* lane/pearlsee (3rd pass): pearl_height: 0 PEARL_TOWER, 1 PEARL_TOWER_HIGH */
    MO_PEARL_HOLD,          /* lane/pearlsee (3rd pass): pearl_hold: 0 3000 steps, 1 PEARL_TOWER_SCAN_SHORT */
    MO_PEARL_ORACLE,        /* lane/pearlsee (3rd pass): pearl_oracle: off, on (PRIVILEGED) */
    MO_OB_ORACLE,           /* lane/chain: obsidian_oracle: off, on (privileged) */
    MO_BP_ORACLE,           /* lane/chain: bportal_oracle: off, on (privileged) */
    MO_OB_DIG_SKIP,         /* lane/chain: obsidian_dig_skip: off, on, bad (lane/bportal) */
    MO_FOOD_ORACLE,         /* lane/chain: food_oracle: off, on (privileged) */
    MO_PEARL_REACHABLE,     /* lane/pearlsee (3rd pass): pearl_reachable: off, on (pearl_oracle's; PRIVILEGED) */
    MO_OB_CARRY,           /* lane/chain: obsidian_carry: off, on (20 obsidian mined, 10 carried to the Nether), frame
                            * (lane/bportal: 10, then 10 of the arrival frame mined in the Nether), fallback (on's 20
                            * until obsidian_fallback_gap steps pass with no gain, then frame's) */
    MO_NT_PORTAL_HOME,      /* lane/chain: nether_portal_home: off, on (the portal built in the Nether with the rods) */
    MO_DIA_ORACLE,          /* lane/chain: diamond_oracle: off, on (privileged) */
    MO_SH_ORACLE,           /* lane/shold: stronghold_oracle: off, locate (PRIVILEGED) */
    MO_DG_ORACLE,           /* lane/shold: dragon_oracle: off, crystals (PRIVILEGED) */
    MO_DG_CRYSTALS,         /* lane/shold: dragon_crystals: bow, snowball */
    MO_ARMOR_KIT,           /* lane/rod: armor_kit: off, iron (PRIVILEGED ore finding; mods/macros/armor.c) */
    MO_SNOW_KIT,            /* lane/rod: snow_kit: off, on, far (lane/snowleg) (PRIVILEGED snow finding; snow.c) */
    MO_PEARL_HOME,          /* lane/pearlhome: pearl_home: off, on (PRIVILEGED) */
    MO_PEARL_TARGET,        /* lane/pearlhome: pearl_target: sky, any (pearl_oracle's; PRIVILEGED) */
    MO_SNOW_FIRST,          /* lane/snowleg: snow_first: off, on (the stronghold waits for snow_kit's snowballs) */
    MO_FOOD_SWIM,           /* lane/pearlhome (2nd pass): food_swim: off, on (the food hunt swims at the top) */
    MO_PEARL_TRIM,          /* lane/chain: pearl_trim: off, on (the pack's surplus thrown out by day at the pearl stage) */
    MO_SNOW_HEAL,           /* lane/chain: snow_heal: off, on (the snowballs' trip waits for the health while it heals) */
    MO_SWORD_KIT,           /* lane/chain2: sword_kit: off, on, iron (upgrade a stone sword before the pearl hunt) */
    MO_SWORD_STOCK,         /* lane/chain2: sword_stock: off, two, one (fresh spare swords before the End) */
    MO_PEARL_ARMOR,         /* lane/chain2: pearl_armor: off, on (the pearl macro's day leg gives way to armor_kit on land) */
    MO_FOOD_NIGHT,          /* lane/chain2: food_night: off, pearl (no stock hunt begun on a pearl night with 6 meats held) */
    MO_DG_FOOD,             /* lane/chain2: dragon_food: off, fetch (the End's food swapped into the hotbar to eat) */
    MO_ARMOR_FUEL,          /* lane/chain2: armor_fuel: off, coal (armor_kit digs coal ore for the ore it cannot smelt) */
    MO_SH_FOOD,             /* lane/chain2: stronghold_food: off, wait, surface, wait80 */
    MO_SH_PACK,             /* lane/chain2: stronghold_pack: off, trim (free slots before the kit's pickups) */
    MO_SH_ARMOR,            /* lane/chain2: stronghold_armor: off, full (wait for a full iron set) */
    MO_FURNACE_KIT,         /* lane/chain2: furnace_kit: off, make, carry (a furnace made for any smelt; carried after) */
    MO_FOOD_PROVISION,      /* lane/chain3: food_provision: off, on, armor_first (kit before the exclusive stock stage) */
    MO_DG_BRIDGE,           /* lane/chain2: dragon_bridge: off, on (privileged floor check, bridge from End platform) */
    MO_ARMOR_TIMING,        /* lane/chain2: armor_timing: now, after_nether (defer the kit's ore work until home) */
    MO_FOOD_WOOD_ROUTE,     /* lane/chain3: food_wood_route: off, bridge, craft (route to wood before crafting) */
    MO_NT_DIG_FACES,        /* lane/chain3: nether_dig_faces: off, on (six-neighbour lava veto) */
    MO_NT_ARRIVAL_COVER,    /* lane/chain3: nether_arrival_cover: off, on (wall a seen ghast before leaving the portal) */
    MO_NT_FAR_DIG,          /* lane/chain3: nether_far_dig: off, on (sealed fallback toward a distant located start) */
    MO_FOOD_WOOD_RETRY,     /* lane/chain3: food_wood_retry: off, on (rearm the surface wood errand at a new place) */
    MO_FOOD_WOOD_RESERVE,   /* lane/chain3: food_wood_reserve: off, on (carry a table and wood for post-Nether armor) */
    MO_PEARL_SHORE_EXIT,    /* lane/chain3: pearl_shore_exit: off, on (own the dry exit after armor, before pearls) */
    MO_PEARL_STRIP_YIELD,   /* lane/chain3: pearl_strip_yield: off, on (night strip yields after full armor) */
    MO_WATER_DIA_STRIP,     /* lane/chain3: water_diamond_strip: off, on (mine diamonds before wood-only crafts) */
    MO_WATER_CRAFT_FIRST,   /* lane/chain3: water_craft_first: off, on (craft ready wood before mine lock) */
    MO_NT_EXIT_BACK,        /* lane/chain3: nether_exit_backtrack: off, on (leave blaze sight before portal craft) */
    MO_NT_CHAMBER_SOLID,    /* lane/chain3: nether_chamber_solid: off, on (full non-falling rock or brick) */
    MO_FOOD_PROVISION_EYES, /* lane/chain3: food_provision_eyes: off, on (briefly yield to craft the final eyes) */
    MO_AIR_RESCUE,          /* lane/chain3: air_rescue: off, on (surface and return to the dry approach); lane/flint:
                               pin (on, and released out of water with the head against a ceiling), out (on, and
                               released once out of water, on the ground or not), pocket (out, and pinned under
                               water the walk into the air beside, surface.c surface_pocket) */
    MO_SNOW_TARGET,         /* lane/chain3: snow_target: 64, 32 (held snowballs before stronghold) */
    MO_SNOW_HEIGHT,         /* lane/chain3: snow_height: off, on (nearby high hills before a distant cold biome) */
    MO_DG_ENDER_AVERT,      /* lane/chain3: dragon_ender_avert: off, on (the pit's look off endermen, no swing at one) */
    MO_DG_HUNT_CAP,         /* lane/legs: dragon_hunt_cap: off, on (the crystal hunt ends at DG_HUNTS on its crystal) */
    MO_DG_SNOW_TOWER,       /* lane/legs: dragon_snow_tower: 16, 24, 32 (the hunt stand's tower blocks at most) */
    MO_NT_FLOOR_KEEP,       /* lane/legs: nether_floor_keep: off, on (no dig under the feet over a drop) */
    MO_NT_DIG_REFILL,       /* lane/nleg: nether_dig_refill: off, on (a dug cell with lava's glow beside it filled again) */
    MO_NT_DIG_AIM,          /* lane/nleg: nether_dig_aim: off, on (a dig aimed at the cell's open face when the centre's
                             * ray lands on another block) */
    MO_NT_DROP_KEEP,        /* lane/nleg: nether_drop_keep: off, on (the tunnel's step down into an open cell with no
                             * floor within 4 under it planned again without the tunnel) */
    MO_NT_CAMP_SIDE,        /* lane/nleg: nether_camp_side: off, 1, 2 (the camp under the spawner this many cells to a
                             * side of its column, the hole over the head opening into its room) */
    MO_NT_FORT_PATH,        /* lane/nleg: nether_fort_path: off, on (the fortress phase's walk, stalled short of the kept
                             * brick, takes the spawner oracle's path) */
    MO_NT_HOOD_STARVE,      /* lane/nleg: nether_hood_starve: off, on (no hood, and none kept, with the food level under
                             * 18 and no food held: no heal comes); rest (lane/rods): and no rest, none kept, the same */
    MO_NT_BRINK_FLOOR,      /* lane/nleg: nether_brink_floor: off, on, lock (a bridge begun with the player's centre
                             * over air, held on a neighbour's edge: the floor under the centre placed first; lock: and
                             * any kind's fight lock held out of reach, unmoving, dropped as a blaze's is) */
    MO_SH_ROOM_START,       /* lane/proom: stronghold_room_start: off, on (a portal frame in view latches at once) */
    MO_FOOD_WOOD_STUCK,     /* lane/proom: food_wood_stuck: off, on (the wood route given up when the feet stand still) */
    MO_THREAT_INTERRUPT,    /* lane/proom: threat_interrupt: off, view, turn (a melee hostile close starts the fight) */
    MO_NT_PICK_UP,          /* lane/rods: nether_pick_up: off, on, dig (a kill's drop whose floor lies 2 or more over
                             * the feet walked toward by the walker; dig: and the block it rests on broken) */
    MO_NT_HOOD_ROOF,        /* lane/rods: nether_hood_roof: off, skip (no hood begun where its roof has no face to go
                             * on: the cell over the head's, the one over it and the four beside it all open) */
    MO_OB_YIELD_LIVE,      /* lane/bportal: obsidian_yield_live: off, on (the strip yields to the obsidian search's
                             * last target only while the obsidian macro is wanted) */
    MO_FOOD_SCOUT_SKY,      /* lane/bportal: food_scout_sky: off, on (food_oracle's scout begun only under the sky, the
                             * strip's climb on to it) */
    MO_MINE_YIELD,          /* lane/bportal (a driver what-if): mine_yield: off, on (no mine lock begun while the
                             * provisions, the scout, a hunt or the bucket portal want the step) */
    MO_OB_FB_GAP,           /* lane/bportal: obsidian_fallback_gap: 320, 640, 1280, 2560 (obsidian_carry fallback's) */
    MO_CLIMB_LAND,          /* lane/bportal: climb_land: exact, cell (the pillar's level judged by the feet's cell),
                             * centre (and the feet kept over the column before a jump) */
    MO_SNOW_GATE,           /* lane/bportal: snow_gate: off, start (the snow trip's start held to its cand's twelve
                             * pearls and snow_heal) */
    MO_FOOD_PROV_COOK,      /* lane/bportal: food_prov_cook: off, on (the smelt's meat before its iron ore while
                             * food_oracle's provisions are wanted) */
    MO_STRIP_SPIN,         /* lane/flint: strip_spin: off, water (a strip run that has walked no cell for
                             * STRIP_SPIN_AGE steps no longer turns at water the liquid ray sees ahead; lava still) */
    MO_INWALL,              /* lane/flint: inwall: off, dig (a full cube in the eye's cell dug before anything acts) */
    MO_LAVA_EDGE,           /* lane/flint: lava_edge: off, sneak (a decision's act sneaks beside lava with no floor
                             * over it; a multi-tick step's later ticks are not held to it), raw (only the act no
                             * macro drove, which also lets go of jump) */
    MO_WATER_EXIT,          /* lane/flint: water_exit: off, step (no macro in hand, the eye in water: the step out
                             * to a dry stand beside, surface.c surface_water_exit) */
    MO_THREAT_SKELETON,    /* lane/pnight: threat_skeleton: off, hit, view (with threat_interrupt: a skeleton in view
                             * after a hit taken, or close, is closed on and fought) */
    MO_PEARL_REACH_AIM,     /* lane/pnight: pearl_reach_aim: off, on (pearl_oracle's in-reach swing only with an
                             * enderman under the crosshair; held off it, the oracle's path and dig take the step) */
    MO_BP_WALK,             /* lane/flint: bportal_walk: off, dig (the bucket portal's water trip to remembered water
                             * out of reach: the obsidian search's strip tunnels to it first, obsidian_walk dig's walk) */
    MO_LAVA_ESCAPE,         /* lane/flint: lava_escape: off, seal (the overworld lava escape seals the lava against the
                             * body with cobblestone, then aims at a standable cell beside, else pillars; strip.c) */
    MO_PROV_UNDER_YIELD,   /* lane/pdig: prov_under_yield: off, on (provision's PVW_UNDER surface stage not entered
                             * while shore_night_yield gives the night to the hunt) */
    MO_PROV_UNDER_SURFACE,  /* lane/pdig: prov_under_surface: off, on (provision_under is the feet under y 62 only),
                             * hungry (the food macros allowed in the under stage by day under 14 food) */
    MO_PROV_WOOD_FOOD,      /* lane/pdig: prov_wood_food: off, on (by day under 14 food with none held, the food want
                             * before the craft's wood route) */
    MO_SCOUT_CENSUS,        /* lane/pdig: scout_census: off, on (with food_oracle: after the Nether, hungry with none
                             * held, the food scout walks to the census's animal, food_oracle_walk, not the spiral) */
    MO_SNEAK_DROP,          /* lane/pdig: sneak_drop: off, on (sho_follow's sneaking walk lets go of sneak where the
                             * way's next cell is one to three below with a safe landing) */
    MO_PEARL_PATROL,        /* lane/pdig (P2): pearl_patrol: off, on (with pearl_oracle, at night with no enderman in
                             * view: the nearest under the sky within NETH_RL_CENSUS_R walked to straight, not the
                             * pathfinder) */
    MO_PEARL_HIDE_FIGHT,    /* lane/pdig (F): pearl_hide_fight: off, on (with a provoked enderman about, the pearl hide at
                             * PEARL_LOW_HP or under at once) */
    MO_DG_CLUTCH,          /* lane/dleg: dragon_clutch: off, water (the dragon macro's fall caught in a water bucket's
                             * water placed on the ground under the feet, then picked up again) */
    MO_DG_HOLE_FLOOR,       /* lane/dleg: dragon_hole_floor: off, on (the pit's hole digs only with a block under the
                             * dug one; privileged) */
    MO_DG_VOID_EDGE,        /* lane/dleg: dragon_void_edge: off, sneak, raw (beside the End's void any act, or the act
                             * no macro drove, sneaks; privileged) */
    MO_DG_FOLLOW,           /* lane/dleg: dragon_follow: off, server (the follow toward the server's dragon; privileged) */
    MO_DG_WAIT,             /* lane/dleg: dragon_wait: ring, centre, band (where the box the charge is waited in is
                             * made; band privileged) */
    MO_DG_LATE,             /* lane/dleg: dragon_late: off, nohunt, nopit, walkin, all (the crystals gone: no hunt, no
                             * hole out past the centre, the wait out there a walk in) */
    MO_DG_PIT,              /* lane/dleg: dragon_pit: dig, once (no dig in its own hole), stand (no hole) */
    MO_THREAT_END,          /* lane/dleg: threat_end: off, ender (with threat_interrupt: the End's enderman that hit, fought) */
    MO_NT_EYE_STRIKE,       /* lane/rods2: nether_eye_strike: off, on, reach, margin (with no foe in the vis list, a blaze
                             * on the screen whose middle is hidden and whose eye is not: looked at, struck with the
                             * crosshair on it; reach within 3.3 of its eye; margin the aim at a clear grid point) */
    MO_NT_MOUTH_COVER,      /* lane/rods2: nether_mouth_cover: off, on (a blaze on the screen past reach whose sight of
                             * the player comes down the camp's hole: the cell over the head filled, its re-dig held) */
    MO_NT_DECK_WAIT,        /* lane/rods2: nether_deck_wait: off, on (the pickup's step not taken within 2 cells of a
                             * deck side over a drop while a blaze has a line to the player) */
    MO_PEARL_MARCH,       /* lane/pground: pearl_march: off, on (with pearl_oracle, at night in the overworld with no
                             * census target within PEARL_MARCH_NEAR: a straight walk along a line held all episode,
                             * out and back; provision and the yields held off the hunt at night once fed; privileged) */
    MO_THREAT_ENDER,        /* lane/pground: threat_ender: off, reach (a screaming enderman within THREAT_ENDER_R of the
                             * eye in the client's entity list starts the fight lock on it whatever runs; the lock keeps
                             * it by that reach when the view list drops it) */
    MO_PEARL_FLOOR,         /* lane/pground: pearl_floor: off, watch (a WHAT-IF's fixed rule: the first step's cell held,
                             * stare, hit in reach, a kill's drop fetched, eating; nothing else drives) */
    MO_ARMOR_GIVEUP,        /* lane/supply: armor_giveup: ring, all (armor_kit's ores given up: a ring of 8, or every
                             * one until the macro has dug an ore or the feet moved AR_BAD_MOVE blocks) */
    MO_DIG_WADE,            /* lane/supply: dig_wade: off, on (the dig walker with no way and its feet in water over a
                             * floor wades to the nearest dry cell first, dig.c dgw_wade) */
    MO_DIG_BRIDGE,          /* lane/supply: dig_bridge: off, on (the dig walker places the floor its plan bridges on) */
    MO_ARMOR_REACH,         /* lane/supply: armor_reach: off, on (armor_kit digs its ore from where it stands when the
                             * crosshair reaches it, no walk into its cell) */
    MO_CARRY_GATE,          /* lane/supply: carry_gate: hold, release (furnace_kit=carry's portal gate lets the portal
                             * go when no furnace is held and none can be had, craft.c carry_furnace_gate) */
    MO_ARMOR_TURN,          /* lane/supply: armor_turn: off, station (armor_kit ahead of the strip mine once an iron
                             * pickaxe is held beside a furnace and a table, armor.c arm_turn) */
    MO_ARMOR_PICK,          /* lane/supply: armor_pick: off, fetch (armor_kit's mine fetches a pickaxe that can dig its
                             * ore from the main inventory when the hotbar holds none, armor.c arm_pick_fetch) */
    MO_ARMOR_TABLE,         /* lane/supply: armor_table: off, take (the placed table taken before the armour mine
                             * leaves it, armor.c arm_table_take) */
    MO_THREAT_SCREEN,       /* lane/supply: threat_screen: off, close (threat_interrupt's threat under the furnace's
                             * screen closes it, the fight next, fight.c threat_step) */
    MO_ARMOR_CRAFT,         /* lane/supply: armor_craft: off, first (a piece craftable now starts the craft ahead of
                             * every start, and ends a mine's run, armor.c arm_craft_first) */
    MO_ARMOR_IPICK,         /* lane/supply: armor_ipick: off, first (no iron or diamond pickaxe held: the armour's want
                             * counts an iron pickaxe's 3 ingots and the pieces wait for iron_plan's pickaxe, armor.c) */
    MO_DIG_TOOL,            /* lane/armed: dig_tool: best, cheap (the dig walker and the armour reach dig a block a
                             * pickaxe mines with the cheapest hotbar pickaxe that mines it, common.c dig_tool) */
    MO_SPICK_KEEP,          /* lane/armed: spick_keep: off, first (st_spick_want's stone pickaxe ahead of the armour's,
                             * the iron's and the late plans), make (first, and with no table held or near a table made
                             * from 4 planks for it; craft.c craft_plan) */
    MO_WOOD_BARE,           /* lane/armed: wood_bare: off, climb (strip2's wood errand climbs with no pickaxe on 8
                             * cobblestone, the provision's bare exit: strip2.c st3_want, st3_pillar) */
    MO_NT_HOOD_KEEP,        /* lane/armed: nether_hood_keep: off, on (healing in the hood, a hood cell open again or the
                             * feet out of its cell: the hood built again, or a new one where the player stands;
                             * nether.c nt_hood) */
    MO_ARMOR_CHOICE,        /* lane/choice: armor_choice: ACS_* (the armour craft slot's selector, armor.c arm_choose) */
    MO_WOOD_SEARCH,         /* lane/wood: wood_search: off, look, walk (mods/macros/wood.c) */
    MO_SEEN_LOG2,           /* lane/wood: seen_log2: off, on (the cast's seen log is block 17 or 162, rlbind.c) */
    MO_LAVA_HIT,            /* lane/lava: lava_hit: off, seen (the lava escape's hit also burning, any health lost and
                             * lava in view within 3: mods/macros/lavahit.c) */
    MO_BP_FILL_GIVEUP,      /* lane/portal: bportal_fill_giveup: off, rest, sky (a streak of fill trips that filled
                             * nothing rests the bucket portal BP_FILL_LONG steps, or forgets the kind's sources and
                             * takes only the eye's for BP_FILL_BLIND steps; bportal.c) */
    MO_N
};
enum { ACS_ARMOR, ACS_IPICK, ACS_CHECK, ACS_INVALID };
enum { SHF_OFF, SHF_STARVING };
enum { SHNN_OFF, SHNN_ON };
enum { SHU_OFF, SHU_TURN };
enum { SHH2_OFF, SHH2_LOW };
enum { SHM_OFF, SHM_OUTSIDE };
enum { SHOR_OFF, SHOR_LOCATE };     /* lane/shold: stronghold_oracle */
enum { SHRS_OFF, SHRS_ON, SHRS_ROOM };  /* lane/proom: stronghold_room_start */
enum { SNK_OFF, SNK_ON, SNK_FAR };  /* lane/rod, lane/snowleg: snow_kit */
enum { SHK_POLICY, SHK_MACRO };
enum { SHW_DETOUR, SHW_PILLAR };
enum { SHT_TURN, SHT_STRAIGHT };
enum { SHN_WALK, SHN_HIDE, SHN_HURT };
enum { SHR_CAVE, SHR_SKY };
enum { SHS_ALWAYS, SHS_FED };
enum { SHL_FOLLOW, SHL_PAIR, SHL_SNAP };
enum { LS_VIEW, LS_LIGHT, LS_NEAR };
enum { BR_OFF, BR_SURFACE, BR_PILLAR };
enum { BN_WALK, BN_STRIP };
enum { BB_ONE, BB_TWO, BB_ANY };
enum { BFG_OFF, BFG_REST, BFG_SKY };   /* lane/portal: bportal_fill_giveup */
enum { BD_OBSIDIAN, BD_BPORTAL, BD_ALWAYS };   /* lane/obsid: BD_ALWAYS */
enum { GUI_OFF, GUI_CURSOR };
enum { PRL_FLAG, PRL_KEY, PRL_SLOT };
enum { TK_INVENTORY, TK_PAUSED };
enum { R2_KEY, R2_CLICKS };
enum { X2S_FIRST, X2S_ANY };
enum { FE_MEAT, FE_ANY };
enum { FH_VIEW, FH_STOCK, FH_STOCK32 };
enum { FS_OFF, FS_LOOK, FS_WALK, FS_EARLY };
enum { FC_OFF, FC_FAR };
enum { FK_OFF, FK_FURNACE, FK_MAKE, FK_PEARL };
enum { FF_OFF, FF_WOOD };
enum { SHW_OFF, SHW_WAIT, SHW_SURFACE, SHW_WAIT80 };
enum { FU_OFF, FU_MAKE, FU_CARRY };
enum { FT_MANY, FT_TWO, FT_ONE, FT_PEARL };
enum { SW_DIGS, SW_CHAIN, SW_EARLY };
enum { CW_STAIR, CW_PILLAR };
enum { SWN_GO, SWN_WAIT };
#define MOPT(k) (v->mo[k])

struct neth_vec {
    struct nwrl *h;
    int mo[MO_N];                           /* lane/macroopts: each option's word (its index in MOPTS[k].words) */
    struct pipe *p;
    int n, G, gs, ep_len, macros, objective;
    int picks;                              /* lane/picks: neth_vec_opts.picks */
    int pin;                                /* lane/rawskill: neth_vec_opts.start_pin */
    int dbench;                             /* lane/dbench: neth_vec_opts.dbench */
    int cycle;                              /* lane/eval1k: neth_vec_opts.start_cycle */
    int hotbar, aw;                         /* lane/nthotbar: NETH_VEC_HOTBAR in the opts' macros; bufs.actions' row */
    int late, ns;                           /* lane/starts: neth_vec_opts.late_scalars; bufs.scalars' row */
    int hsee;                               /* lane/ladder2: late_scalars' bit 1, the hotbar's tools */
    int dig;                                /* lane/ladder4: late_scalars' bit 2, the crack stage and the hit */
    int ore;                                /* lane/ladder5: late_scalars' bit 3, the seek's view of ore and logs */
    int gv;                                 /* lane/ladder10: late_scalars' bit 4, the open container's slots */
    int rel;                               /* lane/curric2: neth_vec_opts.reward_rel */
    int32_t *sep;                           /* lane/stagelen: [nstart + 1] each start's episode length (NULL: ep_len) */
    int clock;                              /* lane/stagelen: neth_vec_opts.clock */
#if NETH_RL_MACROS
    struct pearl_tune pt;                   /* lane/pearl3: the pearl macro's settings (neth_vec_pearl_tune) */
#endif
    struct task_cfg tk;                     /* lane/rawskill: neth_vec_opts.task_* (item 0: off) */
    int hint_noshape;                       /* lane/acrl: neth_vec_opts.task_hint_noshape */
    struct task_cfg tk2;                    /* lane/acrl: neth_vec_opts.task_item2's (item 0: none) */
    int from2, task;                        /* its first start; late_scalars bit 5, the task one-hot */
    const struct rlscan_out *scan;          /* lane/rawsearch (task_hint): the privileged scan's answers, [n] */
    int ticks;                             /* world ticks a decision (neth_rl_obs_spec: the pipeline's obs.ticks) */
    double w[8], msw[NETH_VEC_MILESTONES];  /* the reward's weights: the objective's row or neth_vec_opts.weights */
    int msw_on, log_cap;
    int32_t *caps;                          /* neth_vec_opts.item_caps' copy, NULL: none */
    int nstart;                             /* the mix's starts besides the make's (neth_vec_opts.starts) */
    double *scum;                           /* [nstart + 1]: the starts' cumulative shares, the last 1 */
    /* lane/seedwin: neth_vec_opts.start_window (win 0: every mix start loaded, starts[k] binding start k + 1): the
     * mix's directories, each one's binding start (-1 not loaded), each group's round (its resets since the make) */
    int win, stride;
    char **cat;
    int *cat_si;
    uint8_t *cat_want;
    int64_t *ground;
    double share0;                          /* the make's start's share of the draws */
    int64_t win_loads, win_frees;
    int *sids;                              /* [gs]: one start's envs of a group being reset */
    int save_item, save_count, save_pool;   /* neth_vec_opts' own starts */
    double nether_w;                        /* neth_vec_opts.nether_weight */
    double end_w, dragon_w, dragon_hp_w, crystal_w;    /* lane/dragon: neth_vec_opts.end_weight, dragon_weight, dragon_hp_weight, crystal_weight */
    double step_pen, stall_pen, stall_r2;   /* lane/speedrun-recipe: neth_vec_opts.step_pen, stall_pen, stall_radius^2 */
    int64_t stall_ticks;
    double explore_w;                       /* neth_vec_opts.explore_weight, explore_cap */
    int explore_cap;
    double save_share;
    int *pool, npool, pool_head;            /* [save_pool]: the saved starts made, oldest at pool_head */
    int *plevel;                            /* lane/msstarts: [save_pool] each saved start's level */
    struct start_mem *pmem;                 /* lane/msstarts: [save_pool] each one's placed table and furnace */
    struct start_mem pending_mem[64];
    int64_t *pseq, nseq;                    /* lane/msstarts: [save_pool] each one's order made */
    int pending[64], npending;              /* saved starts being made */
    int pending_level[64];
    int pending_src[64];                    /* lane/evict: the start each save replays its env's acts from */
    /* lane/evict: starts let go from a full pool while envs still play from them, freed when the last has reset;
     * retire_cap what the address window leaves past the pool (at most 64) */
    int retire[64], nretire, retire_cap;
    int64_t saves, saves_failed;
    float values[4096];
    double camdeg[NETH_VEC_CAM];
    struct vec_tabs T;
    struct vec_env *e;
    struct neth_vec_bufs b;
    int64_t sem_hw;
    struct pool_act *noop;                  /* the placeholders pipe_step copies */
    int *ids;
    const struct pipe_result **res;
    int *gstate, *gdone;
    int *ready, nready, ready_at;           /* groups done, in the order they finished */
    struct neth_vec_episode *ring;
    int ring_cap, ring_head, ring_n;
    int64_t dropped, episodes;
    uint64_t recv_ns, reset_ns, tick_ns;
    /* engine.raycast device (lane/rcdev): the frames are drawn by a launch a group (neth_vec_cast) and each env's
     * seen record reaches its state at send (the step's own seen_steps then too) */
    int rc_dev;
    int *rc_ticket;                         /* [G]: the group's launch since its recv, -1 none */
    uint8_t *rc_hostg;                      /* [G]: 1, the group casts on its workers and a put copies its frames
                                             * (engine.raycast_host_groups, lane/rcsplit) */
    int *cids;                              /* [n]: the envs of a launch's groups */
    /* lane/ladder6 (neth_vec_level): copies of this struct made when the level is set (NULL: off), sharing every
     * pointer, that differ only in macros: lv the student's level (an env with lvl set decodes and drives its macros
     * through it), lb the level and the labeller's macros (what the labeller reads, as a run at that level would) */
    struct neth_vec *lv, *lb;
    int lbits;                              /* the labeller's macros (NETH_VEC_SEEK) */
    double lalign;                          /* neth_vec_level_align: degrees (0 off), in lb */
    double lreach;                          /* neth_vec_level_reach: blocks (0 off), in lb */
    int lwalk;                              /* lane/ladder18: neth_vec_level_walk: the seek's walk labelled beside the
                                             * labeller (mods/macros/shadow.c shd_walk), in lb */
    /* lane/chain (neth_vec_opts.chain): on; each saved start's memory of its episode (by start index); the copy of
     * this struct with no macro that neth_vec_chain_free's decisions decode through (made at its first call) */
    int chain;
    struct chain_mem { struct start_mem m; int dims, valid; void *nt, *state; } *cmem;
    int ncmem;
    struct neth_vec *cfv;
};

/* lane/acrl: the env's task: task_item2's for a start at task_from2 or later (its record's start), else task_item's */
static const struct task_cfg *task_of(const struct neth_vec *v, const struct vec_env *e)
{
    return v->tk2.item && e->scat >= v->from2 ? &v->tk2 : &v->tk;
}

#include "vecmods.h"    /* lane/macromod: the core's calls into the macros */

static const struct neth_rl_end *dragon_view(const struct neth_vec *v, const struct vec_env *e)
{
    return neth_rl_end_view(v->h, (int)(e - v->e));
}

const struct neth_rl_end *neth_vec_end_view(const struct neth_vec *v, int id)
{
    return id < 0 || id >= v->n ? NULL : dragon_view(v, &v->e[id]);
}

int neth_vec_dragon_hunt(const struct neth_vec *v, int id, double out[NETH_VEC_DRAGON_HUNT])
{
    if (id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    const struct neth_rl_end *en = dragon_view(v, e);
    const struct vec_dragon *d = &e->dg;
    out[0] = e->dragon ? d->phase : 0, out[1] = d->sub, out[2] = d->hunt;
    int j = 0;
    while (j < d->ncm && d->cm[j].id != d->hunt) ++j;
    int kept = d->hunt && j < d->ncm;
    out[3] = kept ? d->cm[j].x : -1, out[4] = kept ? d->cm[j].y : -1, out[5] = kept ? d->cm[j].z : -1;
    out[6] = kept ? d->cm[j].hunts : -1, out[7] = kept ? d->cm[j].shots : -1;
    out[8] = d->target, out[9] = d->tx, out[10] = d->ty, out[11] = d->tz, out[12] = d->tries;
    out[13] = d->lob, out[14] = d->htower, out[15] = d->ncm;
    int seen = 0;
    for (int k = 0; k < en->ncrystal; ++k) seen |= d->hunt && en->crystal[k].id == d->hunt;
    out[16] = seen, out[17] = en->ncrystal, out[18] = d->shots, out[19] = d->crystals_hit;
    return 0;
}

int neth_vec_dragon_info(const struct neth_vec *v, int id, double out[NETH_VEC_DRAGON_INFO])
{
    if (id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    const struct vec_last *r = &e->last;
    const struct neth_rl_end *en = dragon_view(v, e);
    const struct vec_dragon *d = &e->dg;
    out[0] = r->dim, out[1] = r->x, out[2] = r->y, out[3] = r->z, out[4] = r->health;
    out[5] = en->held, out[6] = en->ncrystal;
    out[7] = en->dragon_id ? en->dragon_health : -1.0;
    out[8] = d->saw_death;
    out[9] = e->dragon ? d->phase : 0;
    out[10] = d->shots, out[11] = d->crystals_hit;
    out[12] = (double)d->kill_step, out[13] = (double)d->exit_step;
    out[14] = (double)e->deaths, out[15] = en->nender;
    for (int k = 0; k < 4; ++k) out[16 + k] = d->died[k];
    out[20] = d->swings, out[21] = d->dragon_min;
    out[22] = d->dragon_shots, out[23] = d->dragon_hits;
    out[24] = (double)d->death_step;
    /* lane/dragon2: the death's attribution and the hits by cause */
    out[25] = d->d_cause - 1, out[26] = d->d_pushed, out[27] = d->d_phase;
    out[28] = d->d_x, out[29] = d->d_y, out[30] = d->d_z;
    out[31] = d->d_fall_len, out[32] = d->d_fall_dt, out[33] = d->d_fall_name, out[34] = d->d_head;
    out[35] = d->d_dt, out[36] = d->d_name, out[37] = d->pushes;
    for (int k = 0; k < DK_N; ++k) out[38 + k] = d->dmg[k];
    out[47] = d->d_food;
    out[48] = d->arrows, out[49] = d->hits[DK_DRAGON], out[50] = e->dgin.food;
    out[51] = d->arrow_dmg, out[52] = d->sword_dmg, out[53] = d->sword_hits;
    out[64] = d->dx, out[65] = d->dz, out[66] = d->sub, out[67] = d->blk, out[68] = d->blk_hot;
    out[69] = d->mo_block, out[70] = d->mo_dy, out[71] = d->rs;
    out[72] = d->lobs, out[73] = d->lob_hits, out[74] = d->picked;
    for (int k = 75; k < NETH_VEC_DRAGON_INFO; ++k) out[k] = 0;
    out[75] = d->exit_unstuck;
    out[76] = d->heal_sum;
    out[77] = d->raids, out[78] = d->raid_hits, out[79] = d->raid_gave;   /* lane/shold: dragon_oracle crystals */
    out[54] = d->ncm;
    for (int k = 0; k < 3; ++k)
        out[55 + 3 * k] = k < d->ncm ? d->cm[k].id : 0, out[56 + 3 * k] = k < d->ncm ? d->cm[k].hunts : 0,
                     out[57 + 3 * k] = k < d->ncm ? d->cm[k].shots : 0;
    return 0;
}

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void seterr(char *err, int n, const char *fmt, const char *a)
{
    if (err && n > 0) snprintf(err, (size_t)n, fmt, a);
}

#include "vecopts.c"     /* the run file's actions options (lane/macroopts' MOPTS) */

/* the word's index in a comma list, -1 none */
static int mopt_word(const char *words, const char *w)
{
    size_t lw = strlen(w);
    int i = 0;
    for (const char *p = words; *p; ++i)
    {
        const char *c = strchr(p, ',');
        size_t l = c ? (size_t)(c - p) : strlen(p);
        if (l == lw && !strncmp(p, w, l)) return i;
        if (!c) break;
        p = c + 1;
    }
    return -1;
}

int neth_vec_mopt_check(const char *name, const char *word, char *err, int en)
{
    for (int k = 0; k < MO_N; ++k)
    {
        if (strcmp(MOPTS[k].name, name)) continue;
        int i = mopt_word(MOPTS[k].words, word);
        if (i < 0 && err && en > 0)
            snprintf(err, (size_t)en, "macro option %s: %s is not one of %s", name, word, MOPTS[k].words);
        return i;
    }
    if (err && en > 0) snprintf(err, (size_t)en, "no macro option %s (neth_vec_mopt_list has them)", name);
    return -1;
}

void neth_vec_mopt_list(FILE *f)
{
    for (int k = 0; k < MO_N; ++k)
    {
        const char *d = MOPTS[k].words;
        for (int i = 0; i < MOPTS[k].def; ++i) d = strchr(d, ',') + 1;
        const char *c = strchr(d, ',');
        fprintf(f, "%-22s %-18s default %.*s: %s\n", MOPTS[k].name, MOPTS[k].words, c ? (int)(c - d) : (int)strlen(d),
                d, MOPTS[k].what);
    }
}

/* neth_vec_opts.macro_opts into mo[]: 0, or -1 with err */
static int mopt_parse(int *mo, const char *spec, char *err, int en)
{
    for (int k = 0; k < MO_N; ++k) mo[k] = MOPTS[k].def;
    if (!spec) return 0;
    char buf[2048];
    if (strlen(spec) >= sizeof buf)
    {
        seterr(err, en, "neth_vec_make: macro_opts is too long%s", "");
        return -1;
    }
    memcpy(buf, spec, strlen(spec) + 1);
    for (char *sv, *t = strtok_r(buf, ", ", &sv); t; t = strtok_r(NULL, ", ", &sv))
    {
        char *eq = strchr(t, '=');
        if (!eq)
        {
            seterr(err, en, "neth_vec_make: macro option %s is not NAME=WORD", t);
            return -1;
        }
        *eq = 0;
        int i = neth_vec_mopt_check(t, eq + 1, err, en);
        if (i < 0) return -1;
        for (int k = 0; k < MO_N; ++k)
            if (!strcmp(MOPTS[k].name, t)) mo[k] = i;
    }
    return 0;
}

/* Loop.reset_group, Macros.reset (mac_reset: each macro's own) and Reward.reset for one env */
static void env_reset(struct vec_env *e)
{
    mac_reset(e);
    e->kind = M_NONE;
    e->age = 0;
    e->gq_n = e->gq_at = 0;     /* lane/ladder10 */
    e->steps = 0;
#if NETH_RL_MACROS
    memset(&e->pfl, 0, sizeof e->pfl);      /* lane/pground (pearl_floor) */
#endif
    e->fresh = 1;
    e->got = 0;
    memset(&e->db, 0, sizeof e->db);        /* lane/dbench */
    memset(e->db.ms_first, 0xff, sizeof e->db.ms_first);
    e->db.food0 = -1;
    e->db_ymax = -1e9, e->db_py = 0.0, e->db_pg = 1, e->db_pfire = 0, e->db_pair = 300, e->db_pfood = 20, e->db_pdim = 0;
    e->st_x = NAN;                          /* lane/speedrun-recipe: no stall begun */
    e->prev_hold = 0;
    e->prev_cam[0] = e->prev_cam[1] = NETH_VEC_CAM / 2;
    memset(e->prev_keys, 0, sizeof e->prev_keys);
    e->prev_macro = 0;
    e->te = 0;
    e->ret = 0;
    e->x0 = e->z0 = NAN;
    e->deaths = 0;
    memset(e->macro_n, 0, sizeof e->macro_n);
    e->ep_steps = 0;
    e->reset_step = 1;
    e->ended = 0;
    e->want_save = e->saved = 0;
    e->ms0 = e->saved_ms = e->carry_ms = 0;
    e->save_level = 0;
    e->dims = 0;
    if (e->ncells)
        memset(e->cells, 0, sizeof e->cells);
    e->ncells = 0;
    memset(e->seen_steps, 0, sizeof e->seen_steps);
    /* lane/dragon: the End's books (vecdragon.c; the End and the dragon's death are milestones, the macro on or off) */
    memset(&e->dg, 0, sizeof e->dg);
    e->dg.kill_step = e->dg.exit_step = e->dg.first_shot = e->dg.death_step = -1;
    e->dg.dragon_min = -1.0f;
    e->cry_dead_n = 0;
    memset(&e->tk, 0, sizeof e->tk);        /* lane/rawskill (vecenv_task.c task_reset) */
}

/* ------------------------------------------------------------- the act */

static void act_noop(struct neth_rl_act *a)
{
    memset(a, 0, sizeof *a);
    a->hotbar = -1;
}

static struct neth_rl_op click(int slot, int button, int mode)
{
    return (struct neth_rl_op){PG_CLICK, -1, slot, button, mode, 0};
}

static const struct neth_rl_op CLOSE = {PG_CLOSE, 0, 0, 0, 0, 0};

/* lane/ladder10 (actions.gui cursor; vecenv.h NETH_VEC_GUI_*): the screens whose slots the cursor grid holds */
static int gui_screen(int sc)
{
    return sc == PS_INVENTORY || sc == PS_CRAFTING || sc == PS_FURNACE || sc == PS_CHEST;
}

#define GUI_OUT (7 * NETH_VEC_GUI_COLS)     /* the cell outside the window: slot -999 */
#define GUI_NONE (-1000)                    /* gui_slot: no slot at the cell */

/* lane/ladder10: slot k of a container of n slots under screen sc as a cell of the cursor grid (row * 9 + column),
 * -1 none. Every container's last 36 slots are the main inventory's 27 then the hotbar's 9 */
static int gui_cell(int sc, int n, int k)
{
    if (k == -999) return GUI_OUT;
    if (k < 0 || n < 36 || k >= n) return -1;
    int own = n - 36;
    if (k >= own)
    {
        int j = k - own;
        return j < 27 ? (3 + j / 9) * NETH_VEC_GUI_COLS + j % 9 : 6 * NETH_VEC_GUI_COLS + (j - 27);
    }
    switch (sc)
    {
    case PS_INVENTORY:      /* ContainerPlayer: 0 the result, 1 .. 4 the 2x2 grid, 5 .. 8 the armour */
        return k == 0 ? 13 : k <= 4 ? ((k - 1) / 2) * 9 + (k - 1) % 2 : k <= 8 ? 18 + 5 + (k - 5) : -1;
    case PS_CRAFTING:       /* ContainerWorkbench: 0 the result, 1 .. 9 the 3x3 grid */
        return k == 0 ? 13 : k <= 9 ? ((k - 1) / 3) * 9 + (k - 1) % 3 : -1;
    case PS_FURNACE:        /* ContainerFurnace: 0 the input, 1 the fuel, 2 the output */
        return k == 0 ? 0 : k == 1 ? 18 : k == 2 ? 13 : -1;
    case PS_CHEST:          /* a small chest's 27 (a large chest's second half has no cell) */
        return k < 27 ? k : -1;
    }
    return -1;
}

/* lane/ladder10: the slot at cell c (gui_cell's inverse): -999 outside, GUI_NONE none */
static int gui_slot(int sc, int n, int c)
{
    if (c == GUI_OUT) return -999;
    if (n > 90) n = 90;
    for (int k = 0; k < n; ++k)
        if (gui_cell(sc, n, k) == c) return k;
    return GUI_NONE;
}

/* lane/ladder10: an item's class in late_scalars' bit 4 (vecenv.h NETH_VEC_GV_K) */
static int gv_class(int it)
{
    switch (it)
    {
    case 17: case 162: return 0;    /* logs */
    case 5: return 1;               /* planks */
    case 280: return 2;             /* stick */
    case 4: return 3;               /* cobblestone */
    case 263: return 4;             /* coal, charcoal */
    case 15: return 5;              /* iron ore */
    case 265: return 6;             /* iron ingot */
    case 264: return 7;             /* diamond */
    case 58: return 8;              /* crafting table */
    case 318: return 9;             /* flint */
    }
    return 10;
}

/* macros._still */
static void still(struct neth_rl_act *a)
{
    a->hold = 0;
    memset(a->press, 0, sizeof a->press);
    a->look_mode = 0;
}

static int64_t inv_count(const struct neth_rl_state *s, int item)
{
    int64_t c = 0;
    for (int i = 0; i < 36; ++i)
        if (s->inv[i].item == item && s->inv[i].count > 0) c += s->inv[i].count;
    return c;
}

/* A reserve sword is new, rather than the worn stone sword the Nether and the pearls already used. */
#if NETH_RL_MACROS
static int sword_stock_fresh(const struct neth_rl_state *s)
{
    int n = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (s->inv[i].count > 0 && s->inv[i].damage == 0 &&
            (it == 276 || it == 267 || it == 272 || it == 268 || it == 283)) ++n;
    }
    return n;
}

/* off keeps the default; one makes a single fresh reserve while two retains the old End kit. */
static int sword_stock_need(const struct neth_vec *v)
{
    return MOPT(MO_SWORD_STOCK) == 2 ? 1 : MOPT(MO_SWORD_STOCK) == 1 ? 2 : 0;
}

#include "../mods/macros/macros.c"     /* lane/macromod: the macros (csrc/mods/macros) */
#else
#include "nomacros.c"                   /* lane/macromod: none (MACROS=0) */
#endif

/* lane/chain: the Nether macro's memory of the episode (the arrival's portal and the way back to it, the fortress,
 * the spawner, the camp, the cells sealed and placed) kept with a save made in the Nether and put back into an episode
 * played from it, so a state saved after the first rod still walks home; its phase and step clocks start over */
static void *chain_nt_keep(const struct vec_env *e)
{
#if NETH_RL_MACROS
    if (!e->nt.arrived) return NULL;
    struct nt_env *k = malloc(sizeof *k);
    if (k) *k = e->nt;
    return k;
#else
    (void)e;
    return NULL;
#endif
}

static void chain_nt_put(struct vec_env *e, const void *kept)
{
#if NETH_RL_MACROS
    const struct nt_env *k = kept;
    struct nt_env *n = &e->nt;
    n->arrived = k->arrived;
    n->has_portal = k->has_portal, n->px = k->px, n->py = k->py, n->pz = k->pz;
    n->heading = k->heading, n->zdir = k->zdir, n->xdir = k->xdir, n->legk = k->legk, n->bearing = k->bearing;
    n->has_fort = k->has_fort, n->fx = k->fx, n->fy = k->fy, n->fz = k->fz, n->fort_at = k->fort_at;
    n->fort_ty = k->fort_ty;
    n->has_loc = k->has_loc, n->loc_x = k->loc_x, n->loc_y = k->loc_y, n->loc_z = k->loc_z;
    n->loc_d0 = k->loc_d0, n->loc_mode = k->loc_mode;
    n->has_entry = k->has_entry, n->entry_x = k->entry_x, n->entry_y = k->entry_y, n->entry_z = k->entry_z;
    n->entry_kind = k->entry_kind, n->entry_way = k->entry_way;
    n->entry_wx = k->entry_wx, n->entry_wy = k->entry_wy, n->entry_wz = k->entry_wz;
    n->entry_fail = k->entry_fail, n->entry_done = k->entry_done;
    n->has_sp = k->has_sp, n->sx = k->sx, n->sy = k->sy, n->sz = k->sz;
    n->has_camp = k->has_camp, n->cx = k->cx, n->cy = k->cy, n->cz = k->cz;
    n->has_top = k->has_top, n->tx = k->tx, n->ty = k->ty, n->tz = k->tz;
    n->ncrumb = k->ncrumb, n->crumb_skip = k->crumb_skip;
    memcpy(n->crumb, k->crumb, sizeof n->crumb);
    n->nsealed = k->nsealed;
    memcpy(n->sealed, k->sealed, sizeof n->sealed);
    n->nplaced = k->nplaced;
    memcpy(n->placed, k->placed, sizeof n->placed);
    n->nfront = k->nfront, n->kfront = k->kfront, n->fronts = k->fronts;
    memcpy(n->front, k->front, sizeof n->front);
    n->rods_max = k->rods_max;
#else
    (void)e, (void)kept;
#endif
}

/* The chain's saved world image does not include the macro controller. Keep its plain-data ranges across a cut.
 * FILE*, playback pointers, queue handles and recorder storage are deliberately outside these ranges. */
enum {
    CHAIN_STATE_A = offsetof(struct vec_env, prev_val) - offsetof(struct vec_env, kind),
    CHAIN_STATE_REWARD = offsetof(struct vec_env, start) - offsetof(struct vec_env, prev_val),
    CHAIN_STATE_FLAGS = offsetof(struct vec_env, pp_step) - offsetof(struct vec_env, want_save),
    CHAIN_STATE_LOOP = offsetof(struct vec_env, reset_step) - offsetof(struct vec_env, pp_step),
    CHAIN_STATE_MACROS = offsetof(struct vec_env, lvl) - offsetof(struct vec_env, fb)
};
struct chain_macro_state {
    struct vec_last last;
    unsigned char a[CHAIN_STATE_A], reward[CHAIN_STATE_REWARD], flags[CHAIN_STATE_FLAGS];
    unsigned char loop[CHAIN_STATE_LOOP], macros[CHAIN_STATE_MACROS];
};

static void *chain_state_keep(const struct vec_env *e)
{
    struct chain_macro_state *k = calloc(1, sizeof *k);
    struct vec_env *copy = malloc(sizeof *copy);
    if (!k || !copy) { free(k); free(copy); return NULL; }
    *copy = *e;
    /* These are current entity references or cached views, not the macro's remembered observations. A restored
     * world must reacquire them from its own first view; the stable historical IDs and clocks remain saved. */
    copy->fid = 0;
    if (copy->dg.target > 0) copy->dg.target = 0; /* positive: crystal entity; negative: dragon sentinel */
#if NETH_RL_MACROS
    copy->pearl.tid = copy->pearl.pv_tid = 0;
#ifdef CHAIN3_THAW_COMPAT
    copy->pearl.thaw_prefix_pad = 0;
#endif
    copy->sh.eid = 0;
    memset(&copy->pearl.far, 0, sizeof copy->pearl.far);
    memset(copy->nt.hit, 0, sizeof copy->nt.hit);
#endif
    k->last = copy->last;
    memcpy(k->a, (const char *)copy + offsetof(struct vec_env, kind), sizeof k->a);
    memcpy(k->reward, (const char *)copy + offsetof(struct vec_env, prev_val), sizeof k->reward);
    memcpy(k->flags, (const char *)copy + offsetof(struct vec_env, want_save), sizeof k->flags);
    memcpy(k->loop, (const char *)copy + offsetof(struct vec_env, pp_step), sizeof k->loop);
    memcpy(k->macros, (const char *)copy + offsetof(struct vec_env, fb), sizeof k->macros);
    free(copy);
    return k;
}

static void chain_state_put(struct vec_env *e, const void *kept)
{
    const struct chain_macro_state *k = kept;
    e->last = k->last;
    memcpy((char *)e + offsetof(struct vec_env, kind), k->a, sizeof k->a);
    memcpy((char *)e + offsetof(struct vec_env, prev_val), k->reward, sizeof k->reward);
    memcpy((char *)e + offsetof(struct vec_env, want_save), k->flags, sizeof k->flags);
    memcpy((char *)e + offsetof(struct vec_env, pp_step), k->loop, sizeof k->loop);
    memcpy((char *)e + offsetof(struct vec_env, fb), k->macros, sizeof k->macros);
    /* Canonical bytes above already exclude active entity references. Keep the live env canonical too. */
    e->fid = 0;
    if (e->dg.target > 0) e->dg.target = 0;
#if NETH_RL_MACROS
    e->pearl.tid = e->pearl.pv_tid = 0;
#ifdef CHAIN3_THAW_COMPAT
    e->pearl.thaw_prefix_pad = 0;
#endif
    e->sh.eid = 0;
    memset(&e->pearl.far, 0, sizeof e->pearl.far);
    memset(e->nt.hit, 0, sizeof e->nt.hit);
#endif
}

/* lane/fwd (chain search --crossings, books only): the same ranges as chain_state_keep and chain_state_put, raw: no
 * entity reference or cached view cleared. For a restore in the same build and the same world rebuilt by its own acts
 * (the entity ids are the uninterrupted run's), so the controller continues exactly; never across a Java rejoin */
enum { FWD_MEM_MAGIC = 0x46574d31, FWD_MEM_VERSION = 1 };
int neth_vec_chain_memory_dump(const struct neth_vec *v, int id, const char *path, char *err, int en)
{
    if (!v->chain || id < 0 || id >= v->n)
    {
        seterr(err, en, "chain memory dump: chain off or a bad env%s", "");
        return -1;
    }
    const struct vec_env *e = &v->e[id];
    struct chain_macro_state *k = calloc(1, sizeof *k);
    if (!k)
    {
        seterr(err, en, "chain memory dump: cannot allocate%s", "");
        return -1;
    }
    k->last = e->last;
    memcpy(k->a, (const char *)e + offsetof(struct vec_env, kind), sizeof k->a);
    memcpy(k->reward, (const char *)e + offsetof(struct vec_env, prev_val), sizeof k->reward);
    memcpy(k->flags, (const char *)e + offsetof(struct vec_env, want_save), sizeof k->flags);
    memcpy(k->loop, (const char *)e + offsetof(struct vec_env, pp_step), sizeof k->loop);
    memcpy(k->macros, (const char *)e + offsetof(struct vec_env, fb), sizeof k->macros);
    uint32_t h[4] = {FWD_MEM_MAGIC, FWD_MEM_VERSION, (uint32_t)sizeof *k, 0};
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(h, sizeof h, 1, f) == 1 && fwrite(k, sizeof *k, 1, f) == 1;
    if (f) ok = fclose(f) == 0 && ok;
    free(k);
    if (!ok) seterr(err, en, "chain memory dump: cannot write %s", path);
    return ok ? 0 : -1;
}

int neth_vec_chain_memory_load(struct neth_vec *v, int id, const char *path, char *err, int en)
{
    if (!v->chain || id < 0 || id >= v->n)
    {
        seterr(err, en, "chain memory load: chain off or a bad env%s", "");
        return -1;
    }
    struct chain_macro_state *k = calloc(1, sizeof *k);
    uint32_t h[4];
    FILE *f = k ? fopen(path, "rb") : NULL;
    int ok = f && fread(h, sizeof h, 1, f) == 1 && h[0] == FWD_MEM_MAGIC && h[1] == FWD_MEM_VERSION &&
             h[2] == sizeof *k && fread(k, sizeof *k, 1, f) == 1 && fgetc(f) == EOF;
    if (f) fclose(f);
    if (ok)
    {
        struct vec_env *e = &v->e[id];
        e->last = k->last;
        memcpy((char *)e + offsetof(struct vec_env, kind), k->a, sizeof k->a);
        memcpy((char *)e + offsetof(struct vec_env, prev_val), k->reward, sizeof k->reward);
        memcpy((char *)e + offsetof(struct vec_env, want_save), k->flags, sizeof k->flags);
        memcpy((char *)e + offsetof(struct vec_env, pp_step), k->loop, sizeof k->loop);
        memcpy((char *)e + offsetof(struct vec_env, fb), k->macros, sizeof k->macros);
    }
    free(k);
    if (!ok) seterr(err, en, "chain memory load: missing or another build's %s", path);
    return ok ? 0 : -1;
}

/* lane/ladder10 (actions.gui cursor): a step the policy drives, its action's GUI part as a player's at the screen: with
 * no screen open key 12 presses the inventory key; under a container screen the keys and the look move nothing (the
 * mouse is on the window), key 12 closes it, else the camera's bins are the cursor's cell and the keys click there
 * (the hotbar element a number key's swap, sneak with mine a shift click, mine a left click, place a right one). Key 12
 * is a key: held over steps it presses once, on the step it goes down (held12: it was held the step before), as a held
 * E opens the inventory once (a policy that held it two steps opened the inventory and closed it again at once: the
 * L4b students at 34 to 104M, 165 to 262 opens and as many closes a run) */
static void gui_policy(const struct neth_vec *v, const struct vec_last *r, const int32_t *act, int held12,
                       struct neth_rl_act *a)
{
    const int32_t *kq = act + 2;
    int inv = kq[NETH_VEC_KEY_INV] > 0 && !held12;
    if (r->screen == PS_NONE)
    {
        if (inv) a->press[PK_INVENTORY] = 1;
        return;
    }
    if (!gui_screen(r->screen)) return;
    still(a);
    a->hotbar = -1;
    a->nops = 0;
    if (inv)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        return;
    }
    int row = act[0] - NETH_VEC_GUI_B0, col = act[1] - NETH_VEC_GUI_B0;
    if (row < 0 || row >= NETH_VEC_GUI_ROWS || col < 0 || col >= NETH_VEC_GUI_COLS) return;
    int slot = gui_slot(r->screen, r->nslots, row * NETH_VEC_GUI_COLS + col);
    if (slot == GUI_NONE) return;
    int hk = v->hotbar ? act[NETH_VEC_ACTION] : 0;
    int left = kq[7] > 0 || kq[11] > 0, right = kq[8] > 0 || kq[9] > 0;
    if (slot != -999 && hk >= 1 && hk <= 9)
        a->ops[0] = click(slot, hk - 1, 2);
    else if (left && kq[6] > 0 && slot != -999)
        a->ops[0] = click(slot, 0, 1);
    else if (left)
        a->ops[0] = click(slot, 0, 0);
    else if (right)
        a->ops[0] = click(slot, 1, 0);
    else
        return;
    a->nops = 1;
}

/* actmap.to_acts and Macros.apply for one env: its act, kind and chosen */
static void decode(struct neth_vec *v, struct vec_env *e, const int32_t *act, int pick, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int *kind, int *chosen)
{
    const struct vec_last *r = &e->last;
    const int32_t *kq = act + 2;
    /* to_acts: the keys (KEYS: forward back left right sprint jump sneak mine place throw drop hit swap_item paused) */
    act_noop(a);
    uint32_t hold = 0;
    static const int holdkey[7][2] = {{0, PK_FORWARD}, {1, PK_BACK}, {2, PK_LEFT}, {3, PK_RIGHT}, {5, PK_JUMP},
                                      {6, PK_SNEAK}, {4, PK_SPRINT}};
    for (int j = 0; j < 7; ++j) hold |= (uint32_t)(kq[holdkey[j][0]] > 0) << holdkey[j][1];
    hold |= (uint32_t)(kq[7] > 0 || kq[11] > 0) << PK_ATTACK;
    hold |= (uint32_t)(kq[8] > 0 || kq[9] > 0) << PK_USE;
    a->hold = hold;
    uint32_t nw = hold & ~e->prev_hold;
    a->press[PK_ATTACK] = (uint8_t)((nw >> PK_ATTACK) & 1);
    a->press[PK_USE] = (uint8_t)((nw >> PK_USE) & 1);
    a->press[PK_DROP] = kq[10] > 0;
    int pb = act[0] < 0 ? 0 : act[0] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[0];
    int yb = act[1] < 0 ? 0 : act[1] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[1];
    a->look_mode = 2;
    a->look[0] = (float)v->camdeg[yb];
    a->look[1] = (float)v->camdeg[pb];
    e->prev_hold = hold;
    int attack = (hold >> PK_ATTACK) & 1;
    /* Macros.apply (lane/macromod: the macros through their dispatcher, vecmods.h) */
    *kind = M_NONE;
    *chosen = 0;
    e->steps += 1;
    e->lsrc = VLS_MACRO, e->lsub = 0, e->wk = 0, e->wkf = 0, e->wkc = 0;
    struct mac_step c = {.v = v, .e = e, .r = r, .s = s, .act = act, .yb = yb, .attack = attack, .throw_key = kq[9] > 0,
                         .pick = pick, .picked = -1, .lava = 0, .ph = -1, .kind0 = M_NONE, .ey = 0.0};
    int busy = e->kind != M_NONE;
    int dead = (r->flags & PF_DEAD) != 0;
    mac_observe(&c, a);                     /* the books, the strip's lava hit, the act's vetoes */
#if defined(PEARL_GATE_TRACE) && NETH_RL_MACROS
    pearl_gate_trace(v, e, r, s);
#endif
#if defined(PEARL_CENSUS_TRACE) && NETH_RL_MACROS
    pearl_census_trace(v, e, r);
#endif
#if NETH_RL_MACROS
#if defined(AR_TRACE) && defined(AR_GRID_T0) && defined(AR_GRID_T1)
    if ((int)(e - v->e) == AR_TRACE && e->steps >= AR_GRID_T0 && e->steps <= AR_GRID_T1)
        surface_grid(v, e, r, s, "GRD", -4);   /* lane/flint: a trace build's window (surface.c) */
#endif
#ifdef FIRE_TRACE
    /* lane/flint (a VARIANT trace build): every burning step: the place, the macro, the held item, the water buckets, and
     * the lava cells (10, 11) within one column and two blocks up or down of the feet's cell, as dx,dy,dz:block:meta */
    if (r->fire && r->dim == 0)
    {
        int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        fprintf(stderr, "FIR %d %lld at %.2f %.2f %.2f gr %d kind %d held %d wb %d under %d lava", id, (long long)e->steps,
                r->x, r->y, r->z, r->on_ground, (int)e->kind, s->current >= 0 ? s->inv[s->current].item : -1,
                (int)inv_count(s, WATER_BUCKET), obsview_block(v->p, id, fx, fy - 1, fz, NULL));
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int m = 0, b = obsview_block(v->p, id, fx + dx, fy + dy, fz + dz, &m);
                    if (b == 10 || b == 11) fprintf(stderr, " %d,%d,%d:%d:%d", dx, dy, dz, b, m);
                }
        fprintf(stderr, "\n");
        if (!e->fr_was)
        {
            /* the burn's first step: the ring, oldest first (FAL id step-of-burn step x y z gr hp kind mbit hold pitch
             * mo_hit mx my mz mob under) */
            int n = e->fr_n < FR_RING ? e->fr_n : FR_RING;
            for (int i = e->fr_n - n; i < e->fr_n; ++i)
            {
                const struct fr_rec *q = &e->fr[i % FR_RING];
                fprintf(stderr, "FAL %d %lld %lld %.2f %.2f %.2f %d %.1f %d %d %x %.1f %d %d %d %d %d %d\n", id,
                        (long long)e->steps, (long long)q->step, q->x, q->y, q->z, q->gr, q->hp, q->kind, q->mbit,
                        q->hold, q->pitch, q->mo_hit, q->mo[0], q->mo[1], q->mo[2], q->mob, q->under);
            }
        }
    }
    e->fr_was = r->fire && r->dim == 0;
#endif
    if (!dead && MOPT(MO_AIR_RESCUE) && s->eye_water && r->air < 150 &&
        !(e->fs_home & PROVISION_AIR_RESCUE))
    {
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        e->fs_home |= PROVISION_AIR_RESCUE;
    }
    if (!dead && MOPT(MO_AIR_RESCUE) && (e->fs_home & PROVISION_AIR_RESCUE))
    {
        /* lane/flint (air_rescue pin): or with the head against a ceiling (surface.c surface_pinned); out: or at once */
        if (!s->eye_water && r->air >= 270 &&
            (r->on_ground || MOPT(MO_AIR_RESCUE) == 3 || MOPT(MO_AIR_RESCUE) == 4 ||
             (MOPT(MO_AIR_RESCUE) == 2 && surface_pinned(v, e, r))))
            e->fs_home &= ~PROVISION_AIR_RESCUE;
        else
        {
            still(a);
            if (r->screen != PS_NONE && r->screen != PS_GAMEOVER)
            {
                a->nops = 1;
                a->ops[0] = CLOSE;
            }
            else if (MOPT(MO_AIR_RESCUE) == 4 && surface_pocket(v, e, r, s, a))
                ; /* lane/flint (air_rescue pocket): pinned under water, the walk into the air beside (surface.c) */
            else
            {
                surface_swim(v, e, r, s, a);
                if (!(a->hold & (1u << PK_ATTACK)))
                {
                    a->hold &= ~(1u << PK_FORWARD);
                    a->hold |= (1u << PK_BACK) | (1u << PK_JUMP) | (1u << PK_SPRINT);
                }
            }
            *kind = M_SURFACE;
            e->lmbit = NETH_VEC_SURFACE;
            e->lhalf = 0;
            e->lsrc = VLS_AIR;
            return;
        }
    }
    /* lane/flint (inwall dig): a full cube in the eye's cell is dug before any macro or the policy acts (the macro in
     * hand is kept and drives again once the head is clear; surface.c surface_inwall) */
    if (!dead && MOPT(MO_INWALL) && r->screen == PS_NONE && surface_inwall(v, e, r, s, a))
    {
        *kind = M_SURFACE;
        e->lmbit = NETH_VEC_SURFACE;
        e->lhalf = 0;
        return;
    }
    /* lane/flint (water_exit step): no macro in hand and the eye in water: the step out to a dry stand beside */
    if (!dead && MOPT(MO_WATER_EXIT) && e->kind == M_NONE && r->screen == PS_NONE && surface_water_exit(v, e, r, s, a))
    {
        *kind = M_SURFACE;
        e->lmbit = NETH_VEC_SURFACE;
        e->lhalf = 0;
        return;
    }
    /* lane/pground (pearl_floor watch, a WHAT-IF's rule: mods/macros/pfloor.c): the step taken, or the eat macro run on
     * the macro path below with provision, the shore and the threats passed by */
    int pfm = !dead && MOPT(MO_PEARL_FLOOR) ? pfloor_step(&c, a, kind) : 0;
    if (pfm == 1)
    {
        e->lsrc = VLS_THREAT;
        return;
    }
    provision_retry_observe(v, e, r);
    if (!pfm && !dead && shore_mode(v, e, r, s))
    {
        shore_apply(&c, a, kind, chosen);
        e->lsrc = VLS_SHORE;
        return;
    }
    /* The stock stage otherwise returns before a ready inventory recipe can
     * start. Make the final eyes on this tick, then let stocking resume. */
    if (!dead && MOPT(MO_FOOD_PROVISION_EYES) && MOPT(MO_FOOD_PROVISION) &&
        (e->fs_home & PROVISION_LATCH) && !v->picks &&
        (v->macros & NETH_VEC_CRAFT) && (v->macros & NETH_VEC_LATE) &&
        e->kind == M_NONE && r->dim == 0 && r->screen == PS_NONE &&
        inv_count(s, EYE) < 12 && inv_count(s, PEARL) > 0 &&
        inv_count(s, EYE) + inv_count(s, PEARL) >= 12 &&
        (inv_count(s, BLAZE_ROD) > 0 || inv_count(s, BLAZE_POWDER) > 0))
    {
        int eye_plan = inv_count(s, BLAZE_ROD) > 0 ? C_POWDER : C_EYE;
        if (plan_ok(v, e, r, s, eye_plan)) craft_begin(v, e, s, eye_plan, CP_OPEN), busy = 1;
    }
    /* lane/proom (threat_interrupt, opt-in): a melee hostile close in view takes the step for the fight's lock before
     * provision and the running macro (fight.c threat_step); the lock it started runs through the macro path below */
    if (pfm == 2) busy = 1;
    if (!pfm && !dead && MOPT(MO_THREAT_INTERRUPT))
    {
        if (threat_step(&c, a, kind))
        {
            e->lsrc = VLS_THREAT;
            return;
        }
        if (threat_lock(e)) busy = 1;
    }
    int pvw = 0;                /* lane/pdig (counting only): the line that decided the stage (provision.c PVW_*) */
    int pm = dead || pfm || (MOPT(MO_THREAT_INTERRUPT) && threat_lock(e)) ? PV_OFF : provision_mode_w(v, e, r, s, &pvw);
    if (pm)
    {
        provision_apply(&c, a, kind, chosen, pm);
        e->lsub |= pvw << 4;
        return;
    }
#endif
    attack = c.attack;
    int lava = c.lava;
    int cand = busy || dead;
    cand |= mac_cand(&c);
    /* lane/nthotbar: the policy's number key, on a step no macro drives */
    int ph = v->hotbar && act[NETH_VEC_ACTION] >= 1 && act[NETH_VEC_ACTION] <= 9 ? act[NETH_VEC_ACTION] - 1 : -1;
    if (ph == s->current) ph = -1;
    c.ph = ph;
    mac_aid(&c, a, cand);                   /* the run file's aids (policy_hold, policy_place, policy_tool) */
    ph = c.ph;
    attack = c.attack;
    if (e->gq_at < e->gq_n)
    {
        /* lane/ladder10 (actions.gui cursor): a macro's queued GUI op, one a step, the step its macro's (dropped on a
         * death, the lava escape or a closed screen) */
        if (dead || lava || r->screen == PS_NONE)
            e->gq_n = e->gq_at = 0;
        else
        {
            still(a);
            a->hotbar = -1;
            a->nops = 1;
            a->ops[0] = e->gq[e->gq_at++];
            *kind = e->gq_kind;
            e->lmbit = e->gq_mbit;
            e->lhalf = e->gq_half;
            return;
        }
    }
    if (!cand)
    {
        a->hotbar = ph;
        mac_policy(&c, a);
        if (MOPT(MO_GUI)) gui_policy(v, r, act, e->prev_keys[NETH_VEC_KEY_INV] > 0, a);     /* lane/ladder10 */
        return;
    }
    double ey = r->y + 1.62;
    c.ey = ey;
    c.attack = attack;
    if (dead)
    {
        a->nops = 1;
        a->ops[0] = (struct neth_rl_op){PG_RESPAWN, 0, 0, 0, 0, 0};
        mac_abort(e);
        mac_end(e);
        return;
    }
    mac_interrupt(&c);                      /* the lava escape, the obsidian's douse */
    if (e->kind == M_NONE)
    {
        mac_start(&c);
        *chosen = mac_chosen(&c);
    }
    int pol = 1;
    if (e->kind != M_NONE)
    {
        int handed;
        c.ph = ph;
        int done = mac_run(&c, a, kind, &handed);
        if (MOPT(MO_GUI) && a->nops > 1)
        {
            /* lane/ladder10 (actions.gui cursor): one GUI op a step, the rest queued in order */
            e->gq_n = a->nops - 1;
            e->gq_at = 0;
            for (int j = 1; j < a->nops; ++j) e->gq[j - 1] = a->ops[j];
            e->gq_kind = e->kind;
            e->gq_mbit = e->lmbit;
            e->gq_half = e->lhalf == 3 ? 2 : e->lhalf == 4 ? 1 : e->lhalf;    /* only the run's first op is its first
                                                                                * step */
            a->nops = 1;
        }
        if (done && !handed) mac_end(e);
        pol = done;
    }
    else
        a->hotbar = ph;
    if (pol) mac_policy(&c, a);
    if (pol && *kind == M_NONE && MOPT(MO_GUI))
        gui_policy(v, r, act, e->prev_keys[NETH_VEC_KEY_INV] > 0, a);     /* lane/ladder10: key 12 on its edge */
#if defined(ST_TRACE) && NETH_RL_MACROS
    /* lane/strip2: debugging only (a VARIANT build: make -C csrc/runtime rlbind VARIANT=stt XFLAGS=-DST_TRACE=ENV) */
#ifndef ST_TRACE_EVERY
#define ST_TRACE_EVERY 20
#define ST_TRACE_FROM 0
#define ST_TRACE_TO 1000000000
#endif
    if (e == v->e + ST_TRACE && e->steps % ST_TRACE_EVERY == 0 && e->steps >= ST_TRACE_FROM && e->steps <= ST_TRACE_TO)
    {
        int nfree = 0;
        for (int j = 0; j < 36; ++j) nfree += s->inv[j].item < 0 || s->inv[j].count <= 0;
        fprintf(stderr,
                "st %lld kind %d strip %d seek %d age %d pos %.2f %.2f %.2f yaw %.0f pitch %.0f sk %d sdir %d sc %d %d %d "
                "soff %lld shaz %d sturns %d want %d mo %d:%d %d %d %d cobble %lld free %d hold %x hp %.0f\n",
                (long long)e->steps, e->kind, e->strip, e->seek, e->age, r->x, r->y, r->z, (double)r->yaw,
                (double)r->pitch, e->sk, e->sdir, e->scx, e->scy, e->scz, (long long)e->soff, e->shaz, e->sturns,
                strip_want(v, e, r, s), s->mo_hit, s->mo_block, s->mo_x, s->mo_y, s->mo_z,
                (long long)inv_count(s, COBBLE), nfree, a->hold, (double)r->health);
        fprintf(stderr, "st %lld cur %d inv", (long long)e->steps, s->current);
        for (int j = 0; j < 36; ++j)
            if (s->inv[j].item >= 0 && s->inv[j].count > 0)
                fprintf(stderr, " %d:%dx%d/%d", j, s->inv[j].item, s->inv[j].count, s->inv[j].damage);
        fprintf(stderr, "\n");
    }
#endif
}

/* neth_rl_step_ticks' conversion */
static void to_pool(const struct neth_rl_act *s, struct pool_act *a)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hold = s->hold;
    memcpy(a->press, s->press, sizeof a->press);
    a->look_mode = s->look_mode == 1 ? PL_LOOK : s->look_mode == 2 ? PL_DLOOK : PL_NONE;
    a->look[0] = s->look[0];
    a->look[1] = s->look[1];
    a->hotbar = s->hotbar;
    a->nops = s->nops < 0 ? 0 : s->nops > POOL_GUI_OPS ? POOL_GUI_OPS : s->nops;
    for (int j = 0; j < a->nops; ++j)
    {
        a->ops[j].kind = s->ops[j].kind;
        a->ops[j].window = s->ops[j].window;
        a->ops[j].slot = s->ops[j].slot;
        a->ops[j].button = s->ops[j].button;
        a->ops[j].mode = s->ops[j].mode;
        a->ops[j].index = s->ops[j].index;
    }
}

static int vec_act(struct neth_vec *v, struct vec_env *e, int id, int k, const struct neth_rl_state *st,
                   struct pool_act *out);

/* lane/tape: the episode's script at its first tick: the last one's closed, and a new one opened when armed and the
 * episode plays from the config's start (a Java snapshot; a saved start is not one), its first line the Snapshot the
 * oracle's run records */
static void tape_turn(struct vec_env *e)
{
    if (e->tape)
    {
        fclose(e->tape);
        e->tape = NULL;
    }
    if (!e->tape_arm || e->start != 0) return;
    e->tape_arm = 0;
    if ((e->tape = fopen(e->tape_path, "w")) != NULL) fputs("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n", e->tape);
}

int neth_vec_tape(struct neth_vec *v, int id, const char *path)
{
    if (id < 0 || id >= v->n || path == NULL || strlen(path) >= sizeof v->e[id].tape_path) return -1;
    snprintf(v->e[id].tape_path, sizeof v->e[id].tape_path, "%s", path);
    v->e[id].tape_arm = 1;
    return 0;
}

int neth_vec_act_hook(struct neth_vec *v, int id, int k, const struct neth_rl_state *st, struct pool_act *out)
{
    struct vec_env *e = &v->e[id];
    if (k == 0 && e->reset_step && (e->tape || e->tape_arm)) tape_turn(e);
    int rc = vec_act(v, e, id, k, st, out);
    if (e->tape) moveact_script_line(e->tape, out);
    return rc;
}

/* lane/ntbc: the bin of the 41 nearest a look change of d degrees (ties to the centre's side) */
static int near_bin(const double *camdeg, double d)
{
    int best = NETH_VEC_CAM / 2;
    double bd = fabs(d);
    for (int b = 0; b < NETH_VEC_CAM; ++b)
        if (fabs(d - camdeg[b]) < bd) bd = fabs(d - camdeg[b]), best = b;
    return best;
}

/* lane/ladder10 (actions.gui cursor): the act's GUI part said in the action as gui_policy reads it: the inventory key's
 * press with no screen open as key 12; under a container screen one close as key 12 (the camera's centre bins), one
 * click on a cell as the cell's bins and the click's keys (a number key's swap the hotbar element, with the hotbar
 * head only); anything else (two ops, a respawn, a slot with no cell) stays unsayable */
static void gui_label(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_act *a,
                      struct neth_vec_label *l)
{
    const struct vec_last *r = &e->last;
    if (a->nops == 0 && a->press[PK_INVENTORY] && r->screen == PS_NONE)
    {
        l->keys |= 1u << NETH_VEC_KEY_INV;
        l->flags &= (uint8_t)~NETH_VEC_LBL_PRESS;
        for (int k = 0; k < NETH_RL_KEYS; ++k)
            if (a->press[k] && k != PK_DROP && k != PK_INVENTORY && !((a->hold >> k) & 1))
                l->flags |= NETH_VEC_LBL_PRESS;
        return;
    }
    if (a->nops != 1 || !gui_screen(r->screen)) return;
    const struct neth_rl_op *op = &a->ops[0];
    int cell = -1, hot = 0;
    uint16_t keys = 0;
    if (op->kind == PG_CLOSE)
        keys = 1u << NETH_VEC_KEY_INV;
    else if (op->kind == PG_CLICK)
    {
        cell = gui_cell(r->screen, r->nslots, op->slot);
        if (cell < 0) return;
        if (op->mode == 2 && op->slot != -999 && op->button >= 0 && op->button < 9 && v->hotbar)
            hot = op->button + 1;
        else if (op->mode == 1 && op->button == 0)
            keys = (1u << 7) | (1u << 6);
        else if (op->mode == 0 && op->button == 0)
            keys = 1u << 7;
        else if (op->mode == 0 && op->button == 1)
            keys = 1u << 8;
        else
            return;
    }
    else
        return;
    l->cam[0] = (int8_t)(cell < 0 ? NETH_VEC_CAM / 2 : NETH_VEC_GUI_B0 + cell / NETH_VEC_GUI_COLS);
    l->cam[1] = (int8_t)(cell < 0 ? NETH_VEC_CAM / 2 : NETH_VEC_GUI_B0 + cell % NETH_VEC_GUI_COLS);
    l->res[0] = l->res[1] = 0.0f;
    l->keys = keys;
    l->flags &= (uint8_t)~(NETH_VEC_LBL_GUI | NETH_VEC_LBL_FAR | NETH_VEC_LBL_PRESS | NETH_VEC_LBL_HOTBAR);
    l->hotbar = hot;
    if (hot) l->flags |= NETH_VEC_LBL_HOTBAR;
}

/* lane/ladder10: the GUI books (vecenv.h neth_vec_gui_books): a macro's GUI step whose label says it is decoded back
 * through gui_policy from the same state, and its op (or the inventory key's press) compared */
static void gui_books(const struct neth_vec *v, struct vec_env *e, const int32_t *act, const struct neth_rl_act *a,
                      int kind, const struct neth_vec_label *l, const struct neth_rl_state *s)
{
    const struct vec_last *r = &e->last;
    int inv = a->nops == 0 && a->press[PK_INVENTORY] && r->screen == PS_NONE;
    int was12 = e->gk12;    /* lane/ladder14: the action's key 12 the step before */
    e->gk12 = act[2 + NETH_VEC_KEY_INV] > 0;
    /* lane/ladder14, ladder15: the craft macro's books of the policy's windows and of its key's chance (craft.c) */
    struct mac_books mb = {.at = MB_GUI, .v = v, .e = e, .s = s, .act = act, .a = a, .kind = kind};
    mac_books(&mb);
    /* lane/ladder13: a crafting table's or a furnace's window seen open on a step after one without it */
    if (r->screen == PS_CRAFTING && e->gscr != PS_CRAFTING) e->gb[16] += 1;
    if (r->screen == PS_FURNACE && e->gscr != PS_FURNACE) e->gb[17] += 1;
    e->gscr = r->screen;
    if (kind == M_NONE)
    {
        if (gui_screen(r->screen))
        {
            /* lane/ladder14: the policy's inventory key under a container screen, and that key held from the step
             * before (no edge: gui_policy closes on the press alone), and its jump */
            int k12 = act[2 + NETH_VEC_KEY_INV] > 0;
            e->gb[31] += k12;
            e->gb[32] += k12 && was12;
            e->gb[33] += act[2 + 5] > 0;
        }
        if (gui_screen(r->screen)) e->gb[4] += 1;
        if (r->screen == PS_FURNACE) e->gb[15] += 1;    /* lane/ladder13 */
        if (a->nops == 1 && a->ops[0].kind == PG_CLICK) e->gb[5] += 1;
        if (a->nops == 1 && a->ops[0].kind == PG_CLOSE) e->gb[6] += 1;
        if (inv) e->gb[7] += 1;
        return;
    }
    if (a->nops == 0 && !inv) return;
    e->gb[0] += 1;
    if (l->flags & NETH_VEC_LBL_GUI) return;
    e->gb[1] += 1;
    int32_t q[NETH_VEC_ACTION + 1] = {0};
    q[0] = l->cam[0];
    q[1] = l->cam[1];
    for (int j = 0; j < NETH_VEC_KEYS; ++j) q[2 + j] = (l->keys >> j) & 1;
    q[NETH_VEC_ACTION] = l->hotbar;
    struct neth_rl_act b;
    act_noop(&b);
    gui_policy(v, r, q, 0, &b);    /* the label as a press after a release */
    int same = inv ? b.press[PK_INVENTORY] && b.nops == 0
                   : b.nops == 1 && b.ops[0].kind == a->ops[0].kind &&
                         (a->ops[0].kind == PG_CLOSE ||
                          (b.ops[0].slot == a->ops[0].slot && b.ops[0].button == a->ops[0].button &&
                           b.ops[0].mode == a->ops[0].mode));
    e->gb[same ? 2 : 3] += 1;
    (void)act;
}

int neth_vec_gui_books(const struct neth_vec *v, int64_t *out)
{
    if (!v || !out) return -1;
    memset(out, 0, NETH_VEC_GUI_BOOKS * sizeof *out);
    for (int i = 0; i < v->n; ++i)
        for (int k = 0; k < NETH_VEC_GUI_BOOKS; ++k) out[k] += v->e[i].gb[k];
    return MOPT(MO_GUI);
}

/* lane/ntbc: the act a (made from e->last's yaw and pitch) in the policy's 16-wide action (vecenv.h neth_vec_label) */
static void vec_label(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *st,
                      const struct neth_rl_act *a, int kind, int chosen, struct neth_vec_label *l)
{
    memset(l, 0, sizeof *l);
    l->flags = NETH_VEC_LBL_VALID | (chosen ? NETH_VEC_LBL_CHOSEN : 0) | (e->lfits ? NETH_VEC_LBL_FITS : 0);
    l->kind = (uint8_t)kind;
    double dy = 0, dp = 0;
    if (a->look_mode == 2)
        dy = a->look[0], dp = a->look[1];
    else if (a->look_mode == 1)
    {
        dy = fmod((double)a->look[0] - (double)e->last.yaw, 360.0);
        if (dy >= 180.0) dy -= 360.0;
        if (dy < -180.0) dy += 360.0;
        dp = (double)a->look[1] - (double)e->last.pitch;
    }
    int pb = near_bin(v->camdeg, dp), yb = near_bin(v->camdeg, dy);
    l->cam[0] = (int8_t)pb;
    l->cam[1] = (int8_t)yb;
    l->res[0] = (float)(dp - v->camdeg[pb]);
    l->res[1] = (float)(dy - v->camdeg[yb]);
    for (int c = 0; c < 2; ++c)
    {
        int b = c ? yb : pb;
        double r = c ? dy - v->camdeg[yb] : dp - v->camdeg[pb];
        int nb = r > 0 ? b + 1 : b - 1;
        double gap = nb >= 0 && nb < NETH_VEC_CAM ? fabs(v->camdeg[nb] - v->camdeg[b]) : 0.0;
        if (fabs(r) > 0.5 * gap + 1e-6) l->flags |= NETH_VEC_LBL_FAR;
    }
    static const int keyof[9] = {PK_FORWARD, PK_BACK, PK_LEFT, PK_RIGHT, PK_SPRINT, PK_JUMP, PK_SNEAK, PK_ATTACK, PK_USE};
    for (int j = 0; j < 9; ++j) l->keys |= (uint16_t)(((a->hold >> keyof[j]) & 1) << j);
    if (a->press[PK_DROP]) l->keys |= 1 << 10;
    int pkey = MOPT(MO_PRESS_LABEL) >= PRL_KEY;     /* lane/ladder11: press_label key (and slot, ladder13) */
    if (pkey && a->press[PK_ATTACK]) l->keys |= 1 << 7;
    if (pkey && a->press[PK_USE]) l->keys |= 1 << 8;
    for (int k = 0; k < NETH_RL_KEYS; ++k)
        if (a->press[k] && k != PK_DROP && !((a->hold >> k) & 1) && !(pkey && (k == PK_ATTACK || k == PK_USE)))
            l->flags |= NETH_VEC_LBL_PRESS;
    if (a->nops > 0) l->flags |= NETH_VEC_LBL_GUI;
    /* lane/ladder3: with policy_tool on, a macro's slot change to the tool the env's own pick would take (the attack
     * held on the crosshair's block, the hotbar's best tool for it) is not the student's to say: no label, so the
     * hotbar head learns only the switches the env does not make */
    int envpick = mac_envpick(v, st, a, kind);     /* (aids.c) */
    if (a->hotbar >= 0 && a->hotbar != st->current && !envpick)
    {
        l->flags |= NETH_VEC_LBL_HOTBAR;
        l->hotbar = a->hotbar < 9 ? a->hotbar + 1 : 0;
    }
    else if (MOPT(MO_PRESS_LABEL) == PRL_SLOT && kind != M_NONE && a->press[PK_USE] && st->current >= 0 &&
             st->current < 9 && (st->inv[st->current].item == TABLE || st->inv[st->current].item == FURNACE) &&
             st->inv[st->current].count > 0)
        l->hotbar = st->current + 1;    /* lane/ladder13: press_label slot (not a change: no NETH_VEC_LBL_HOTBAR) */
    l->mbit = kind == M_NONE ? 0 : (uint32_t)e->lmbit;
    /* lane/ladder12; lane/ladder16: the seek's first step too (half 6) */
    l->half = kind == M_NONE || (e->lmbit != NETH_VEC_CRAFT && e->lmbit != NETH_VEC_SEEK) ? 0 : (uint8_t)e->lhalf;
    /* lane/ladder15 (table_chance on, craft.c): 5, the policy's step with no screen and a crafting plan named;
     * lane/ladder16 (seek_chance on, seek.c): 7, the policy's step at a SEEKKEY level with the seek's start holding */
    l->half = (uint8_t)mac_chance(v, e, st, kind, l->half);
    if (MOPT(MO_GUI) == GUI_CURSOR) gui_label(v, e, a, l);     /* lane/ladder10 */
}

#if NETH_RL_MACROS
#include "../mods/macros/shadow.c"     /* lane/ladder6: the removed macro as the labeller */
#endif
#include "vecenv_task.c"        /* lane/rawskill: neth_vec_opts.task_item's task */


static int vec_act(struct neth_vec *v, struct vec_env *e, int id, int k, const struct neth_rl_state *st,
                   struct pool_act *out)
{
    /* lane/kmacro: one world tick of the episode, booked on every tick of a multi-tick step. te is the game
     * time: the observation's o[7] divides it by 6000 (five minutes at 20 ticks a second) and truncation
     * compares it to ep_len, so a step of n ticks must add n (it added one a decision, which ran a k=10
     * episode ten times long and o[7] ten times slow). */
    e->te += 1;
    if (k > 0)
    {
        /* Act.holdOnly: the holds stay, nothing else repeats */
        memset(out, 0, sizeof *out);
        out->kind = PA_AGENT;
        out->hold = e->cur.hold;
        out->look_mode = PL_NONE;
        out->hotbar = -1;
        if (e->kind != M_NONE)
        {
            /* the macro's state machine advances every tick of a multi-tick step (before, it ran once a
             * decision, so strip, seek and craft phases, the mine's look and the hold after a break froze
             * for k-1 ticks and every macro ran k times slow in game time). r is the step's start state;
             * the macros read it for the eye and the branch they already track. */
            struct neth_rl_act a;
            act_noop(&a);
            struct mac_step c = {.v = e->lvl ? v->lv : v, .e = e, .r = &e->last, .s = st, .act = NULL, .picked = -1,
                                 .ph = -1, .kind0 = e->kind, .ey = e->last.y + 1.62};
            int done = mac_drive(&c, &a);
            to_pool(&a, &e->cur);
            *out = e->cur;
            if (done) mac_end(e);
            if (v->b.tick_acts && k < v->ticks && !e->reset_step)
            {
                /* lane/ticks-per-step: the tick's act for the recorder (the hold-only ticks it makes again). Not a
                 * reset step's: recv runs it before the trainer reads the episode's last step from the buffers */
                size_t q = (size_t)id * (size_t)(v->ticks - 1) + (size_t)(k - 1);
                v->b.tick_acts[q] = a;
                v->b.tick_macro[q] = 1;
            }
        }
        return 1;
    }
    if (v->b.tick_macro && !e->reset_step) memset(v->b.tick_macro + (size_t)id * (size_t)(v->ticks - 1), 0, (size_t)(v->ticks - 1));
    uint64_t t0 = now_ns();
    struct neth_rl_act a;
    mac_decision(e);                        /* (the strip's stall anchor) */
    if (e->reset_step)
    {
        act_noop(&a);
        memset(&v->b.label[id], 0, sizeof v->b.label[id]);     /* lane/ntbc: no decision */
    }
    else
    {
        /* Loop.run after the reward: the act, the macros, the books */
        int kind, chosen;
        int32_t kbuf[NETH_VEC_ACTION + 1];
        const int32_t *act = task_keys(v, &e->tk, v->b.actions + (size_t)id * v->aw, kbuf);   /* lane/rawskill */
        /* lane/ladder14: the decision's state, before decode's macros move anything (only under a window) */
        e->lfits = e->last.screen != PS_NONE && mac_fits(e->lvl ? v->lv : v, e, st);
        struct neth_vec *dv = e->lvl ? v->lv : v;
        if (e->cfree > 0 && v->cfv && e->kind == M_NONE)
        {
            /* lane/chain (neth_vec_chain_free): the policy's action alone this decision */
            dv = v->cfv;
            --e->cfree;
        }
        if (e->cplay && e->cplay_at < e->cplay_n)
        {
            /* lane/chain (neth_vec_chain_play): the caller's act, no macro */
            a = e->cplay[e->cplay_at++];
            kind = M_NONE, chosen = 0;
            if (e->cplay_at == e->cplay_n) e->cplay = NULL;
        }
        else
        {
            decode(dv, e, act, v->picks ? v->b.picks[id] : 0, st, &a, &kind, &chosen);
#if NETH_RL_MACROS
            /* lane/flint (lava_edge sneak): any act beside lava with no floor over it sneaks (surface.c) */
            if (MOPT(MO_LAVA_EDGE) == 1 && surface_lava_edge(v, e, &e->last)) a.hold |= 1u << PK_SNEAK;
            /* lava_edge raw: only the act no macro drove; its jump dropped too (the base's fal- rows: 18 of 53
             * fall-first fire deaths were the policy's own keys off such an edge, 15 with jump held, where sneak
             * holds no edge in the air) */
            if (MOPT(MO_LAVA_EDGE) == 2 && kind == M_NONE && surface_lava_edge(v, e, &e->last))
                a.hold = (a.hold | 1u << PK_SNEAK) & ~(1u << PK_JUMP);
            /* lane/dleg (dragon_void_edge): in the End beside the void, a walk (an act holding a move key) sneaks
             * (sneak: any owner's; raw: only the act no macro drove, its jump dropped as lava_edge raw's); a tower's
             * jump or a still aim is left alone (a sneak lowers the eye 0.08, which the aims do not take) */
            if (MOPT(MO_DG_VOID_EDGE) && (MOPT(MO_DG_VOID_EDGE) == 1 || kind == M_NONE) &&
                (a.hold & (1u << PK_FORWARD | 1u << PK_BACK | 1u << PK_LEFT | 1u << PK_RIGHT)) &&
                dragon_void_edge(v, e, &e->last))
                a.hold = (a.hold | 1u << PK_SNEAK) & ~(MOPT(MO_DG_VOID_EDGE) == 2 ? 1u << PK_JUMP : 0u);
            /* lane/dleg (dragon_follow server): the follow toward the server's dragon sneaks at the void's edge the
             * same way, its jump dropped (a jump carries the body over the edge in the air, where sneak holds none) */
            else if (MOPT(MO_DG_FOLLOW) && e->dragon && e->dg.phase == DP_FOLLOW &&
                     (a.hold & (1u << PK_FORWARD | 1u << PK_BACK | 1u << PK_LEFT | 1u << PK_RIGHT)) &&
                     dragon_void_edge(v, e, &e->last))
                a.hold = (a.hold | 1u << PK_SNEAK) & ~(1u << PK_JUMP);
#endif
        }
        e->macro_n[kind] += 1;
        e->prev_cam[0] = act[0] < 0 ? 0 : act[0] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[0];
        e->prev_cam[1] = act[1] < 0 ? 0 : act[1] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[1];
        for (int j = 0; j < NETH_VEC_KEYS; ++j) e->prev_keys[j] = (float)act[2 + j];
        e->prev_macro = kind;
        v->b.kind[id] = (uint8_t)kind;
        v->b.chosen[id] = (uint8_t)chosen;
        v->b.driver[id] = (uint8_t)(kind == M_NONE || chosen ? 0 : kind);
        v->b.acts[id] = a;
#ifdef FIRE_TRACE
        {
            /* lane/flint (a VARIANT trace build): the last FR_RING decided acts, printed at a burn's first step */
            struct fr_rec *q = &e->fr[e->fr_n++ % FR_RING];
            const struct vec_last *rl = &e->last;
            q->step = e->steps, q->x = rl->x, q->y = rl->y, q->z = rl->z, q->gr = rl->on_ground, q->kind = kind;
            q->mbit = e->lmbit, q->hold = a.hold, q->pitch = rl->pitch, q->hp = rl->health, q->mo_hit = st->mo_hit;
            q->mo[0] = st->mo_x, q->mo[1] = st->mo_y, q->mo[2] = st->mo_z, q->mob = st->mo_block;
            q->under = obsview_block(v->p, id, (int)floor(rl->x), (int)floor(rl->y + 0.01) - 1, (int)floor(rl->z), NULL);
        }
#endif
        vec_label(v, e, st, &a, kind, chosen, &v->b.label[id]);
        /* lane/ladder17: the 2x2 labeller (craft.c's books: x2label.c), before gui_books */
        struct mac_books mb = {.at = MB_LABEL, .v = v, .e = e, .id = id, .s = st, .act = act, .a = &a, .kind = kind,
                               .l = &v->b.label[id]};
        mac_books(&mb);
        if (MOPT(MO_GUI) == GUI_CURSOR) gui_books(v, e, act, &a, kind, &v->b.label[id], st);   /* lane/ladder10 */
        /* lane/ladder16: the seek key's books; lane/ladder6: the labeller beside an env at a level (shadow.c) */
        mb.at = MB_DECISION;
        mac_books(&mb);
        ++e->policy_steps;
    }
    to_pool(&a, &e->cur);
    *out = e->cur;
    e->ns += now_ns() - t0;
    return 1;
}

/* --------------------------------------------------------- after the step */

/* Reward.step for one env: the total (and the terms' books) */
/* lane/explore: 1 when (x, y, z)'s 4x4x4 cell is new to the episode (kept, so it pays once), else 0; open addressing over
 * cells, at most half full (explore_cap <= NETH_VEC_EXPLORE_MAX) */
static int explore_new(struct vec_env *e, double x, double y, double z)
{
    uint64_t cx = (uint64_t)(int64_t)floor(x / 4.0) & 0x1fffff, cy = (uint64_t)(int64_t)floor(y / 4.0) & 0x1fffff,
             cz = (uint64_t)(int64_t)floor(z / 4.0) & 0x1fffff;
    uint64_t key = ((cx << 42) | (cy << 21) | cz) + 1, n = 2 * NETH_VEC_EXPLORE_MAX;
    uint64_t h = key * 0x9e3779b97f4a7c15ull;
    for (uint64_t i = (h >> 32) % n;; i = (i + 1) % n)
    {
        if (e->cells[i] == key)
            return 0;
        if (e->cells[i] == 0)
        {
            e->cells[i] = key;
            e->ncells += 1;
            return 1;
        }
    }
}

static float reward(const struct neth_vec *v, struct vec_env *e, const struct pool_result *r, const struct neth_rl_state *s)
{
    float p[36];
    uint64_t ms = 0;
    int64_t cap = v->log_cap, lrem = cap;      /* the held logs still paid for (log_cap) */
    int cid[36], cn = 0;                        /* item_caps: the ids met so far and what each has used */
    int64_t cused[36];
    int64_t zl[36];                             /* lane/curric2: what is left of each start item's zero */
    if (v->rel)
    {
        if (e->fresh)
        {
            /* the episode's first step: each item id's count held is its zero */
            e->ninv0 = 0;
            for (int j = 0; j < 36; ++j)
            {
                int it = s->inv[j].item;
                if (it < 0 || s->inv[j].count <= 0) continue;
                int k = 0;
                while (k < e->ninv0 && e->inv0_id[k] != it) ++k;
                if (k == e->ninv0) e->inv0_id[e->ninv0] = (int16_t)it, e->inv0_n[e->ninv0++] = 0;
                e->inv0_n[k] += s->inv[j].count;
            }
        }
        for (int k = 0; k < e->ninv0; ++k) zl[k] = e->inv0_n[k];
    }
    for (int j = 0; j < 36; ++j)
    {
        int it = s->inv[j].item, itc = it < 0 ? 0 : it > 4095 ? 4095 : it;
        int64_t c = s->inv[j].count;
        if (v->rel && it >= 0 && c > 0)
            for (int k = 0; k < e->ninv0; ++k)
                if (e->inv0_id[k] == it)
                {
                    int64_t z = c < zl[k] ? c : zl[k];
                    c -= z, zl[k] -= z;
                    break;
                }
        if (cap && (it == LOG || it == LOG2))
        {
            c = c < lrem ? c : lrem;
            c = c > 0 ? c : 0;
            lrem -= c;
        }
        if (v->caps && it >= 0 && v->caps[itc] > 0 && c > 0)
        {
            int k = 0;
            while (k < cn && cid[k] != itc) ++k;
            if (k == cn) cid[cn] = itc, cused[cn++] = 0;
            int64_t room = v->caps[itc] - cused[k];
            c = c < room ? c : room;
            c = c > 0 ? c : 0;
            cused[k] += c;
        }
        p[j] = (v->values[itc] * (float)c) * (float)(it >= 0);
        if (s->inv[j].count > 0) ms |= v->T.msbit[itc];
    }
    float val = npsum36(p);
    int64_t logs = s->mined_logs, opened = s->chests_opened, near = s->nearest;
    uint64_t ach = s->ach;
    float dist = s->nearest_dist;
    int f = e->fresh;
    float t[8];
    float dv = val - e->prev_val;
    t[0] = f ? 0.0f : dv;                                                           /* items */
    int chest_open = r->hud.screen == PS_CHEST || e->prev_screen == PS_CHEST;
    t[1] = chest_open && !f ? (dv >= 0.0f || isnan(dv) ? dv : 0.0f) : 0.0f;       /* looted: np.maximum(dv, 0) */
    e->prev_screen = r->hud.screen;
    int64_t pl = e->prev_logs, cl = logs;
    if (cap) pl = pl < cap ? pl : cap, cl = cl < cap ? cl : cap;
    t[2] = f ? 0.0f : (float)(cl - pl);                                             /* logs */
    t[3] = f ? 0.0f : (float)(opened - e->prev_opened);                             /* chest */
    int same = !f && near >= 0 && near == e->prev_near && dist < 64.0f && e->prev_dist < 64.0f;
    t[4] = same ? e->prev_dist - dist : 0.0f;                                       /* progress */
    t[5] = f ? 0.0f : (float)__builtin_popcountll(ach & ~e->prev_ach);              /* ach */
    t[6] = (r->flags & PF_DIED) != 0 ? 1.0f : 0.0f;                                 /* death */
    t[7] = f ? 0.0f : (float)__builtin_popcountll(ms & ~e->got);                    /* milestone */
    if (v->msw_on && !f)
    {
        double m = 0.0;
        for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
            if ((ms & ~e->got) >> i & 1) m += v->msw[i];
        t[7] = (float)m;
    }
    e->got = f ? ms : e->got | ms;
    /* sum(w[k] * t[k] for k in t): from 0, each product in float32 */
    float total = 0.0f;
    for (int j = 0; j < 8; ++j) total = total + (float)v->w[j] * t[j];
    e->prev_val = val;
    e->prev_logs = logs;
    e->prev_opened = opened;
    e->prev_ach = ach;
    e->prev_near = near;
    e->prev_dist = dist;
    e->fresh = 0;
    return total;
}

/* semteach.scalars for one env */
static void scalars(const struct neth_vec *v, const struct vec_env *e, const struct pool_result *r,
                    const struct neth_rl_state *s, float *o)
{
    float counts[NETH_VEC_ITEMS + 1] = {0};
    for (int j = 0; j < 36; ++j)
    {
        int it = s->inv[j].item;
        float c = it >= 0 ? (float)s->inv[j].count : 0.0f;
        int idx = it >= 0 ? v->T.item_index[it > 4095 ? 4095 : it] : 0;
        counts[idx] = counts[idx] + c;
    }
    for (int i = 0; i < NETH_VEC_ITEMS; ++i) o[i] = log1pf(counts[i + 1]);
    o += NETH_VEC_ITEMS;
    int sel = r->hud.selected < 0 ? 0 : r->hud.selected > 8 ? 8 : r->hud.selected;
    for (int i = 0; i < 9; ++i) o[i] = i == sel ? 1.0f : 0.0f;
    o += 9;
    int held = s->inv[sel].item;
    const struct pool_priv *pv = &r->priv;
    o[0] = pv->health / 20.0f;
    o[1] = (float)((double)pv->food / 20.0);
    o[2] = (float)((double)pv->air / 300.0);
    o[3] = (float)((pv->y - 64.0) / 32.0);
    o[4] = (float)pv->on_ground;
    o[5] = (float)s->eye_water;
    o[6] = (float)s->mo_hit;
    o[7] = (float)e->te / 6000.0f;          /* lane/abstime: the time played in 5-minute units (it was te / ep_len: the
                                             * same at 5-minute episodes; a minute now reads the same at any length) */
    if (v->clock)
    {
        /* lane/stagelen: the last five minutes' share gone (neth_vec_opts.clock) */
        int64_t gone = 6000 - (e->eplen - e->te);
        o[7] = (float)(gone > 0 ? gone : 0) / 6000.0f;
    }
    o[8] = r->hud.screen != 0 ? 1.0f : 0.0f;
    o[9] = (float)(r->flags & PF_DEAD);
    o[10] = (float)((double)(held >= 0 ? v->T.item_index[held > 4095 ? 4095 : held] : 0) / NETH_VEC_ITEMS);
    o[11] = sinf(pv->pitch * (3.14159265358979323846f / 180.0f));
    o += 12;
    for (int i = 0; i < NETH_VEC_MACROS; ++i) o[i] = i == e->prev_macro ? 1.0f : 0.0f;
    o += NETH_VEC_MACROS;
    for (int i = 0; i < 2 * NETH_VEC_CAM; ++i) o[i] = 0.0f;
    o[e->prev_cam[0]] = 1.0f;
    o[NETH_VEC_CAM + e->prev_cam[1]] = 1.0f;
    o += 2 * NETH_VEC_CAM;
    for (int i = 0; i < NETH_VEC_KEYS; ++i) o[i] = e->prev_keys[i];
    o += NETH_VEC_KEYS;
    if (v->hsee)
    {
        /* lane/ladder2 + lane/hill-rnn-hotbar: 100% legal 1.7.10 HUD hotbar features (9 slots x 12 = 108 floats):
         * per slot [0..3] pickaxe/axe/shovel/sword tier over 4, [4] bow, [5] snowball log-count (max 16),
         * [6] arrow log-count (max 64), [7] building block log-count (max 64), [8] edible food log-count (max 64),
         * [9] water bucket, [10] empty bucket, [11] flint & steel. Directly visible on the 9-slot HUD bar. */
        static const float INV_L16 = 0.35295612f;   /* 1 / log1p(16) */
        static const float INV_L64 = 0.23955613f;   /* 1 / log1p(64) */
        float *h = o + (v->late ? NETH_VEC_LATE_N : 0);
        for (int i = 0; i < 9; ++i)
        {
            int it = s->inv[i].item, cnt = s->inv[i].count;
            int ok = it >= 0 && it < 4096 && cnt > 0;
            float *p = h + 12 * i;
            if (!ok)
            {
                memset(p, 0, 12 * sizeof *p);
                continue;
            }
            int c16 = cnt > 16 ? 16 : cnt;
            int c64 = cnt > 64 ? 64 : cnt;
            p[0] = (float)v->T.pick[it] * 0.25f;
            p[1] = (float)v->T.axe[it] * 0.25f;
            p[2] = (float)v->T.shovel[it] * 0.25f;
            p[3] = (float)v->T.sword[it] * 0.25f;
            p[4] = it == 261 ? 1.0f : 0.0f;
            p[5] = it == SNOWBALL ? log1pf((float)c16) * INV_L16 : 0.0f;
            p[6] = it == 262 ? log1pf((float)c64) * INV_L64 : 0.0f;
            p[7] = v->T.build_block[it] ? log1pf((float)c64) * INV_L64 : 0.0f;
            p[8] = v->T.food[it] ? log1pf((float)c64) * INV_L64 : 0.0f;
            p[9] = it == WATER_BUCKET ? 1.0f : 0.0f;
            p[10] = it == BUCKET ? 1.0f : 0.0f;
            p[11] = it == FLINT_STEEL ? 1.0f : 0.0f;
        }
    }
    if (v->dig)
    {
        /* lane/ladder4: the dig the screen shows, after the hotbar's tools: the crack stage the client draws
         * (survival.c surv_client_dig_stage, play's overlay: -1 none) and the client's hit under way (the worker
         * runs this hook with the env's neth_env) */
        float *g = o + (v->late ? NETH_VEC_LATE_N : 0) + (v->hsee ? NETH_VEC_HSEE_N : 0);
        int dx, dy, dz, stage = surv_client_dig_stage(&dx, &dy, &dz);
        g[0] = (float)(stage + 1) * 0.1f;
        g[1] = neth_env->survival.dig.is_hitting ? 1.0f : 0.0f;
    }
    if (v->ore)
    {
        /* lane/ladder5: what NETH_VEC_SEEK reads of the view, after the dig's: for iron ore, diamond ore and logs
         * (neth_rl_state seen 0, 1 and 3: the observation's own ray cast), 1 when a face is drawn, the nearest cell's yaw
         * from the look over 180 (wrapped), its pitch from the look over 90 and its distance from the eye over 16 */
        static const int KINDS[3] = {0, 1, 3};
        float *g = o + (v->late ? NETH_VEC_LATE_N : 0) + (v->hsee ? NETH_VEC_HSEE_N : 0) +
                   (v->dig ? NETH_VEC_DIG_N : 0);
        double ey = pv->y + 1.62;
        for (int k = 0; k < 3; ++k)
        {
            int q = KINDS[k];
            float *u = g + 4 * k;
            u[0] = u[1] = u[2] = u[3] = 0.0f;
            double yaw, pitch, cx, cy, cz;
            float d;
            if (s->seen_n[q] > 0 && s->seen[q].d > 0.0f)
                cx = s->seen[q].x + 0.5, cy = s->seen[q].y + 0.5, cz = s->seen[q].z + 0.5, d = s->seen[q].d;
            else if (q == v->tk.seen && ((v->scan && e->hint_ok) || (v->tk.hint < 0 && e->tk.has_cell)))
            {
                /* lane/rawsearch (task_hint): none in view, the privileged scan's nearest of the task's block, or
                 * (task_hint -1) the cell of it last drawn this episode (the potential's) */
                if (v->tk.hint < 0) cx = e->tk.cx + 0.5, cy = e->tk.cy + 0.5, cz = e->tk.cz + 0.5;
                else cx = e->hint[0] + 0.5, cy = e->hint[1] + 0.5, cz = e->hint[2] + 0.5;
                d = (float)sqrt((cx - pv->x) * (cx - pv->x) + (cy - ey) * (cy - ey) + (cz - pv->z) * (cz - pv->z));
            }
            else
                continue;
            look_at(pv->x, ey, pv->z, cx, cy, cz, &yaw, &pitch);
            double dy = fmod(yaw - (double)pv->yaw, 360.0);
            dy = dy > 180.0 ? dy - 360.0 : dy < -180.0 ? dy + 360.0 : dy;
            u[0] = 1.0f;
            u[1] = (float)(dy / 180.0);
            u[2] = (float)((pitch - (double)pv->pitch) / 90.0);
            u[3] = d / 16.0f;
        }
    }
    if (v->gv)
    {
        /* lane/ladder10: what the open container screen shows, after the seek's view (vecenv.h NETH_VEC_GV_N): the
         * screen's kind, then each cursor cell's stack and the cursor's, count over 64 at the item's class */
        float *g = o + (v->late ? NETH_VEC_LATE_N : 0) + (v->hsee ? NETH_VEC_HSEE_N : 0) +
                   (v->dig ? NETH_VEC_DIG_N : 0) + (v->ore ? NETH_VEC_ORE_N : 0);
        memset(g, 0, NETH_VEC_GV_N * sizeof *g);
        int sc = r->hud.screen;
        if (gui_screen(sc))
        {
            g[sc == PS_INVENTORY ? 0 : sc == PS_CRAFTING ? 1 : sc == PS_FURNACE ? 2 : 3] = 1.0f;
            int n = r->hud.nslots < 0 ? 0 : r->hud.nslots > 90 ? 90 : r->hud.nslots;
            for (int k = 0; k < n; ++k)
            {
                int c = gui_cell(sc, n, k), it = r->hud.slots[k].item, cnt = r->hud.slots[k].count;
                if (c < 0 || c >= NETH_VEC_GUI_CELLS || it < 0 || cnt <= 0) continue;
                g[4 + c * NETH_VEC_GV_K + gv_class(it)] = (float)(cnt > 64 ? 64 : cnt) / 64.0f;
            }
            int it = r->hud.cursor.item, cnt = r->hud.cursor.count;
            if (it >= 0 && cnt > 0)
                g[4 + NETH_VEC_GUI_CELLS * NETH_VEC_GV_K + gv_class(it)] = (float)(cnt > 64 ? 64 : cnt) / 64.0f;
        }
    }
    if (v->task)
    {
        /* lane/acrl: the env's task as the policy is told it, after the container's (vecenv.h NETH_VEC_TASK_N): a
         * one-hot of task_item (or no second task) and task_item2 */
        float *g = o + (v->late ? NETH_VEC_LATE_N : 0) + (v->hsee ? NETH_VEC_HSEE_N : 0) +
                   (v->dig ? NETH_VEC_DIG_N : 0) + (v->ore ? NETH_VEC_ORE_N : 0) + (v->gv ? NETH_VEC_GV_N : 0);
        int two = task_of(v, e) == &v->tk2;
        g[0] = two ? 0.0f : 1.0f;
        g[1] = two ? 1.0f : 0.0f;
    }
    if (!v->late) return;
    /* lane/starts: the late game's counts (log1p of each, summed over the 36 slots), then the dimension */
    float late[NETH_VEC_LATE_N] = {0};
    for (int j = 0; j < 36; ++j)
    {
        int it = s->inv[j].item;
        for (int q = 0; it >= 0 && q < LATE_NITEM; ++q)
            for (int w = 0; w < 7 && LATE_ITEMS[q][w]; ++w)
                if (LATE_ITEMS[q][w] == it) late[q] = late[q] + (float)s->inv[j].count;
    }
    for (int q = 0; q < LATE_NITEM; ++q) o[q] = log1pf(late[q]);
    o[LATE_NITEM] = r->priv.dim == -1 ? 1.0f : 0.0f;
    o[LATE_NITEM + 1] = r->priv.dim == 1 ? 1.0f : 0.0f;
}

/* lane/dbench: the benchmark's books at a result (counting only; after the dims are updated, before the step is counted) */
static void db_step(const struct neth_vec *v, struct vec_env *e, int id, const struct pool_result *r,
                    const struct neth_rl_state *s)
{
    struct neth_vec_db *d = &e->db;
    uint32_t have = (uint32_t)e->got | ((e->dims & 1) ? 1u << NETH_VEC_MILESTONES : 0u) |
                    ((e->dims & 4) ? 1u << NETH_VEC_MS_END : 0u) | (e->dg.kill_step >= 0 ? 1u << NETH_VEC_MS_DRAGON : 0u);
    for (int b = 0; b < NETH_VEC_MS_BITS; ++b)
        if ((have >> b & 1) && d->ms_first[b] < 0) d->ms_first[b] = (int32_t)e->ep_steps;
    if (d->food0 < 0 && r->priv.food <= 0 && !e->reset_step) d->food0 = (int32_t)e->ep_steps;
    int st = 0;
    for (int j = 1; j < STAGE_N; ++j)
        if (stage_has(j, (uint32_t)e->got, r->priv.dim, e->dims)) st = j;
    int m = 0;
    if (!e->reset_step)
    {
        uint32_t mb = v->b.label[id].mbit;      /* who drove the action this result follows (0: the policy) */
        m = mb ? 1 + __builtin_ctz(mb) : 0;
        d->mb_stage[st][m] += 1;
    }
    if ((r->flags & PF_DIED) && d->ndeath < NETH_VEC_DB_DEATHS)
    {
        int k = d->ndeath++;
        d->d_step[k] = (int32_t)e->ep_steps;
        d->d_dim[k] = e->db_pdim;       /* the step before's: the death step's own may be the respawn's */
        d->d_mb[k] = m;
        d->d_stage[k] = st;
        /* a death step's own state may be the respawn's: the step before's, the fall from the highest y since the
         * ground (airborne, over 3.5 blocks above where it was last seen) and the fire */
        d->d_fall[k] = !e->db_pg && e->db_ymax - e->db_py > 3.5;
        d->d_fire[k] = e->db_pfire > 0;
        d->d_air[k] = e->db_pair <= 0;
        d->d_food[k] = e->db_pfood <= 0;
#if NETH_RL_MACROS
        d->d_egg[k] = e->te - e->pearl.kb_te <= 5 ? e->pearl.kb_egg : -1;   /* (pearl.c's rule: the stat moves a step before) */
#else
        d->d_egg[k] = -1;               /* lane/rod: no pearl macro (MACROS=0): no killer known */
#endif
    }
    if (r->priv.on_ground || s->eye_water)
        e->db_ymax = r->priv.y;
    else if (r->priv.y > e->db_ymax)
        e->db_ymax = r->priv.y;
    e->db_py = r->priv.y, e->db_pg = r->priv.on_ground || s->eye_water, e->db_pfire = r->priv.fire;
    e->db_pair = r->priv.air, e->db_pfood = r->priv.food, e->db_pdim = r->priv.dim;
    if (r->flags & PF_DIED) e->db_ymax = -1e9, e->db_pg = 1;
}

/* lane/speedrun-recipe: a stall ended at te (the step that left it or progressed); its books when stall_ticks long */
static void stall_close(const struct neth_vec *v, struct vec_env *e, int64_t te)
{
    int64_t len = te - e->st_te0;
    if (isnan(e->st_x) || len < v->stall_ticks) return;
    e->db.stall_steps += (int32_t)len;
    e->db.stall_runs += 1;
    if (len > e->db.stall_max) e->db.stall_max = (int32_t)len;
}

/* lane/speedrun-recipe: the stall tracker (neth_vec_opts.stall_*), always kept (the books); the step penalty and the
 * stall's taken from *rew only when their weights are not 0, so with both off the reward is the same float */
static void stall_step(const struct neth_vec *v, struct vec_env *e, const struct pool_result *r, float *rew)
{
    uint32_t have = (uint32_t)e->got | (uint32_t)e->dims << 24;
    if (r->priv.dim >= -1 && r->priv.dim <= 1) have |= 1u << (24 + r->priv.dim + 1);
    double dx = r->priv.x - e->st_x, dy = r->priv.y - e->st_y, dz = r->priv.z - e->st_z;
    int fresh = e->reset_step || isnan(e->st_x) || (r->flags & PF_DIED) || r->priv.dim != e->st_dim ||
                dx * dx + dy * dy + dz * dz > v->stall_r2 || (have & ~e->st_have) || e->prev_val > e->st_val;
    if (fresh)
    {
        if (!e->reset_step) stall_close(v, e, e->te);
        e->st_x = r->priv.x, e->st_y = r->priv.y, e->st_z = r->priv.z, e->st_dim = r->priv.dim;
        e->st_te0 = e->te, e->st_have = have, e->st_val = e->prev_val;
    }
    if (e->reset_step) return;
    if (v->step_pen != 0.0) *rew -= (float)v->step_pen;
    if (v->stall_pen != 0.0 && e->te - e->st_te0 >= v->stall_ticks) *rew -= (float)v->stall_pen;
}

int neth_vec_db(const struct neth_vec *v, int id, struct neth_vec_db *out)
{
    if (!v->dbench || id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    *out = e->db;
    out->steps = (int32_t)e->ep_steps;
    int64_t len = e->te - e->st_te0;     /* lane/speedrun-recipe: the stall open now, as it stands */
    if (!isnan(e->st_x) && len >= v->stall_ticks)
    {
        out->stall_steps += (int32_t)len, out->stall_runs += 1;
        if (len > out->stall_max) out->stall_max = (int32_t)len;
    }
    return 0;
}

int neth_vec_lsrc(const struct neth_vec *v, int id)
{
    return id < 0 || id >= v->n ? -1 : v->e[id].lsrc | v->e[id].lsub << 8;
}

int neth_vec_walk(const struct neth_vec *v, int id, int32_t out[6])
{
    if (id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    out[0] = e->wk, out[1] = e->wkd, out[2] = e->wky, out[3] = e->wkn, out[4] = e->wkf, out[5] = e->wkc;
    return 0;
}

/* lane/wood: the run file's seen_log2 (vecenv.h) */
int neth_vec_seen_log2(const struct neth_vec *v) { return v && MOPT(MO_SEEN_LOG2) != 0; }

/* lane/wood: env id's wood search books (vecenv.h) */
int neth_vec_wood(const struct neth_vec *v, int id, int64_t out[4])
{
    if (!v || id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    out[0] = e->wd_seeks, out[1] = e->wd_sweeps, out[2] = e->wd_turns, out[3] = e->wd_steps;
    return 0;
}

/* lane/chain: the checkpointed search's hooks (vecenv.h) */
int neth_vec_chain_peek(const struct neth_vec *v, int id, struct neth_vec_chain_peek *out)
{
    if (!v->chain || id < 0 || id >= v->n) return -1;
    *out = v->e[id].cpk;
    return 0;
}

int neth_vec_chain_room_target(const struct neth_vec *v, int id, double *x, double *z)
{
    if (!v || !v->chain || id < 0 || id >= v->n || !x || !z) return -1;
#if NETH_RL_MACROS
    const struct sh_env *h = &v->e[id].sh;
    if (!h->go || !h->oroom.ok) return 1;
    *x = (h->oroom.box[0] + h->oroom.box[3]) / 2.0 + 0.5;
    *z = (h->oroom.box[2] + h->oroom.box[5]) / 2.0 + 0.5;
    return 0;
#else
    return 1;
#endif
}

int neth_vec_chain_save(struct neth_vec *v, int id, char *err, int en)
{
    if (!v->chain || id < 0 || id >= v->n)
    {
        seterr(err, en, "neth_vec_chain_save: chain off or a bad env%s", "");
        return -1;
    }
    struct vec_env *e = &v->e[id];
    int st = neth_rl_start_save(v->h, id, err, en);
    if (st < 0) return -1;
    if (st < v->ncmem)
    {
        struct chain_mem *m = &v->cmem[st];
        void *state = chain_state_keep(e);
        if (!state)
        {
            neth_rl_start_free(v->h, st);
            seterr(err, en, "chain memory: cannot save macro state%s", "");
            return -1;
        }
        m->m = (struct start_mem){e->has_table, e->has_furnace, e->tx, e->ty, e->tz, e->fx, e->fy, e->fz,
                                  (uint32_t)e->got | e->carry_ms};
        m->dims = e->dims;
        m->valid = 1;
        free(m->nt);
        m->nt = chain_nt_keep(e);
        free(m->state);
        m->state = state;
    }
    return st;
}

/* Version 1 stored only Nether memory. Version 2 stores every controller range and keeps the old prefix for reads of
 * existing frontiers. The size fields reject an unknown layout rather than interpreting another build's bytes. */
struct chain_disk_memory {
    uint32_t magic, version, bytes, nt_bytes;
    struct start_mem m;
    int dims, has_nt;
#if NETH_RL_MACROS
    struct nt_env nt;
#endif
};
struct chain_disk_memory_v2 {
    struct chain_disk_memory base;
    uint32_t state_bytes;
    struct chain_macro_state state;
};
/* Version 2 kept every macro controller in one raw range. Adding a field to pearl moved the following obsidian
 * controllers, so the old range is not a prefix. Version 3 records each controller's byte count separately. A
 * controller may grow only by appending zero-default fields; the fixed gaps must remain byte-for-byte the same. */
enum { CHAIN_MEMORY_MAGIC = NETH_RL_CHAIN_MEM_MAGIC, CHAIN_MEMORY_V1 = 1, CHAIN_MEMORY_V2 = 2,
       CHAIN_MEMORY_VERSION = NETH_RL_CHAIN_MEM_VERSION, CHAIN_MEMORY_PIECES = NETH_RL_CHAIN_MEM_PIECES };
_Static_assert(sizeof(struct neth_rl_chain_mem_header) == 76, "chain sidecar header layout");

static void chain_memory_layout(size_t off[CHAIN_MEMORY_PIECES], uint32_t size[CHAIN_MEMORY_PIECES])
{
#if NETH_RL_MACROS
#define CHAIN_AT(field) (offsetof(struct chain_macro_state, macros) + \
                         offsetof(struct vec_env, field) - offsetof(struct vec_env, fb))
    const size_t edge[CHAIN_MEMORY_PIECES + 1] = {
        0, offsetof(struct chain_macro_state, macros), CHAIN_AT(nt), CHAIN_AT(nt) + sizeof(((struct vec_env *)0)->nt),
        CHAIN_AT(arm), CHAIN_AT(arm) + sizeof(((struct vec_env *)0)->arm),
        CHAIN_AT(sh), CHAIN_AT(sh) + sizeof(((struct vec_env *)0)->sh),
        CHAIN_AT(pearl), CHAIN_AT(pearl) + sizeof(((struct vec_env *)0)->pearl),
        CHAIN_AT(ob), CHAIN_AT(ob) + sizeof(((struct vec_env *)0)->ob),
        CHAIN_AT(bp), CHAIN_AT(bp) + sizeof(((struct vec_env *)0)->bp), sizeof(struct chain_macro_state)
    };
#undef CHAIN_AT
#else
    const size_t edge[CHAIN_MEMORY_PIECES + 1] = {
        0, offsetof(struct chain_macro_state, macros), sizeof(struct chain_macro_state),
        sizeof(struct chain_macro_state), sizeof(struct chain_macro_state), sizeof(struct chain_macro_state),
        sizeof(struct chain_macro_state), sizeof(struct chain_macro_state), sizeof(struct chain_macro_state),
        sizeof(struct chain_macro_state), sizeof(struct chain_macro_state), sizeof(struct chain_macro_state),
        sizeof(struct chain_macro_state), sizeof(struct chain_macro_state), sizeof(struct chain_macro_state)
    };
#endif
    for (int i = 0; i < CHAIN_MEMORY_PIECES; ++i)
        off[i] = edge[i], size[i] = (uint32_t)(edge[i + 1] - edge[i]);
}

static int chain_memory_controller_piece(int i)
{
#if NETH_RL_MACROS
    return i == 2 || i == 4 || i == 6 || i == 8 || i == 10 || i == 12;
#else
    (void)i;
    return 0;
#endif
}

int neth_vec_chain_memory_write(const struct neth_vec *v, int start, const char *path, char *err, int en)
{
    if (!v->chain || start < 0 || start >= v->ncmem || !v->cmem[start].valid)
    {
        seterr(err, en, "chain memory: unsaved start%s", "");
        return -1;
    }
    const struct chain_mem *m = &v->cmem[start];
    if (!m->state)
    {
        seterr(err, en, "chain memory: macro state missing%s", "");
        return -1;
    }
    struct neth_rl_chain_mem_header h = {0};
    size_t off[CHAIN_MEMORY_PIECES];
    chain_memory_layout(off, h.piece_bytes);
    h.magic = CHAIN_MEMORY_MAGIC, h.version = CHAIN_MEMORY_VERSION;
    h.state_bytes = sizeof(struct chain_macro_state);
    h.bytes = sizeof h + h.state_bytes;
    h.pieces = CHAIN_MEMORY_PIECES;
    char tmp[2200];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
    {
        seterr(err, en, "chain memory: path too long %s", path);
        return -1;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f)
    {
        seterr(err, en, "chain memory: cannot open %s", path);
        return -1;
    }
    int ok = fwrite(&h, sizeof h, 1, f) == 1 &&
             fwrite(m->state, h.state_bytes, 1, f) == 1;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) remove(tmp);
    if (!ok) seterr(err, en, "chain memory: cannot write %s", path);
    return ok ? 0 : -1;
}

int neth_vec_chain_memory_read(struct neth_vec *v, int id, const char *path, char *err, int en)
{
    if (!v->chain || id < 0 || id >= v->n) return -1;
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        if (errno == ENOENT) return 1;  /* nodes made before sidecars were introduced */
        seterr(err, en, "chain memory: cannot open %s", path);
        return -1;
    }
    uint32_t header[4];
    if (fread(header, sizeof header, 1, f) != 1 || header[0] != CHAIN_MEMORY_MAGIC)
    {
        fclose(f);
        seterr(err, en, "chain memory: incompatible %s", path);
        return -1;
    }
    rewind(f);
    if (header[1] == CHAIN_MEMORY_VERSION)
    {
        struct neth_rl_chain_mem_header h = {0};
        size_t off[CHAIN_MEMORY_PIECES], total = 0;
        uint32_t size[CHAIN_MEMORY_PIECES];
        chain_memory_layout(off, size);
        int ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == CHAIN_MEMORY_MAGIC &&
                 h.version == CHAIN_MEMORY_VERSION && h.pieces == CHAIN_MEMORY_PIECES &&
                 h.state_bytes <= sizeof(struct chain_macro_state) && h.bytes == sizeof h + h.state_bytes;
        for (int i = 0; ok && i < CHAIN_MEMORY_PIECES; ++i)
        {
            uint32_t n = h.piece_bytes[i];
            if (chain_memory_controller_piece(i) ? n > size[i] || n == 0 : n != size[i]) ok = 0;
            total += n;
        }
        if (total != h.state_bytes) ok = 0;
        struct chain_macro_state *state = ok ? calloc(1, sizeof *state) : NULL;
        if (!state) ok = 0;
        for (int i = 0; ok && i < CHAIN_MEMORY_PIECES; ++i)
            if (h.piece_bytes[i] &&
                fread((unsigned char *)state + off[i], 1, h.piece_bytes[i], f) != h.piece_bytes[i]) ok = 0;
        if (ok) ok = fgetc(f) == EOF;
        ok = fclose(f) == 0 && ok;
        if (!ok)
        {
            free(state);
            seterr(err, en, "chain memory: incompatible %s", path);
            return -1;
        }
        chain_state_put(&v->e[id], state);
        free(state);
        return 0;
    }
    if (header[1] == CHAIN_MEMORY_V2)
    {
        struct chain_disk_memory_v2 *d = calloc(1, sizeof *d);
        if (!d)
        {
            fclose(f);
            seterr(err, en, "chain memory: cannot allocate sidecar%s", "");
            return -1;
        }
        size_t got = fread(d, 1, sizeof *d, f);
        int ok = fgetc(f) == EOF;
        ok = fclose(f) == 0 && ok;
#if NETH_RL_MACROS && defined(CHAIN3_THAW_COMPAT)
        /* These are the two measured version-2 layouts: before the thaw fields (pearl 13360 bytes), and the
         * diagnostic layout that put them before prev_x (13368 bytes). The current pearl is 13376 bytes with its
         * new fields past the old 13360-byte end. Version 2 has no piece sizes, so no other shorter file is read. */
        if (ok && sizeof(struct pearl_env) == 13376 && sizeof d->state == 62592 &&
            offsetof(struct pearl_env, prev_x) == 13344 &&
            offsetof(struct pearl_env, thaw_side) == 13360 &&
            d->base.bytes == got && d->base.nt_bytes == sizeof d->base.nt)
        {
            size_t pearl_at = offsetof(struct chain_macro_state, macros) +
                              offsetof(struct vec_env, pearl) - offsetof(struct vec_env, fb);
            size_t ob_at = offsetof(struct chain_macro_state, macros) +
                           offsetof(struct vec_env, ob) - offsetof(struct vec_env, fb);
            unsigned char *state = (unsigned char *)&d->state;
            if (got + 16 == sizeof *d && d->state_bytes + 16 == sizeof d->state)
            {
                memmove(state + ob_at, state + ob_at - 16, d->state_bytes - (ob_at - 16));
                memset(state + pearl_at + 13356, 0, 20); /* old tail padding, then the new fields */
                d->state_bytes = sizeof d->state;
                d->base.bytes = got = sizeof *d;
            }
            else if (got + 8 == sizeof *d && d->state_bytes + 8 == sizeof d->state)
            {
                unsigned char old_thaw[12], old_prev[12];
                memcpy(old_thaw, state + pearl_at + 13344, sizeof old_thaw);
                memcpy(old_prev, state + pearl_at + 13356, sizeof old_prev);
                memmove(state + ob_at, state + ob_at - 8, d->state_bytes - (ob_at - 8));
                memset(state + pearl_at + 13344, 0, 32);
                memcpy(state + pearl_at + 13344, old_prev, sizeof old_prev);
                memcpy(state + pearl_at + 13360, old_thaw, sizeof old_thaw);
                d->state_bytes = sizeof d->state;
                d->base.bytes = got = sizeof *d;
            }
        }
#endif
        ok = ok && got == sizeof *d && d->base.bytes == got && d->state_bytes == sizeof d->state;
#if NETH_RL_MACROS
        ok = ok && d->base.nt_bytes == sizeof d->base.nt;
#else
        ok = ok && d->base.nt_bytes == 0;
#endif
        if (!ok)
        {
            free(d);
            seterr(err, en, "chain memory: incompatible %s", path);
            return -1;
        }
        struct vec_env *e = &v->e[id];
        chain_state_put(e, &d->state);
        free(d);
        return 0;
    }
    if (header[1] != CHAIN_MEMORY_V1)
    {
        fclose(f);
        seterr(err, en, "chain memory: incompatible %s", path);
        return -1;
    }
    struct chain_disk_memory d = {0};
    size_t got = fread(&d, 1, sizeof d, f);
    int ok = fgetc(f) == EOF;
    ok = fclose(f) == 0 && ok;
    if (!ok || d.magic != CHAIN_MEMORY_MAGIC || d.version != CHAIN_MEMORY_V1 || d.bytes != got)
    {
        seterr(err, en, "chain memory: incompatible %s", path);
        return -1;
    }
#if NETH_RL_MACROS
    /* The spawner-break clock added one int64_t to nt_env after the first saved search images. Preserve the earlier
     * sidecar's table, furnace and Nether cells rather than losing all of them on a library swap. Accept exactly that
     * known eight-byte layout change; any other layout remains an error. */
    if (got + sizeof d.nt.sp_break_at == sizeof d && d.nt_bytes + sizeof d.nt.sp_break_at == sizeof d.nt)
    {
        size_t at = offsetof(struct nt_env, sp_break_at);
        memmove((unsigned char *)&d.nt + at + sizeof d.nt.sp_break_at,
                (unsigned char *)&d.nt + at, d.nt_bytes - at);
        memset((unsigned char *)&d.nt + at, 0, sizeof d.nt.sp_break_at);
        d.nt_bytes = sizeof d.nt;
        got = sizeof d;
    }
    if (got != sizeof d || d.nt_bytes != sizeof d.nt)
#else
    if (got != sizeof d || d.nt_bytes != 0)
#endif
    {
        seterr(err, en, "chain memory: incompatible %s", path);
        return -1;
    }
    struct vec_env *e = &v->e[id];
    e->has_table = d.m.has_table, e->tx = d.m.tx, e->ty = d.m.ty, e->tz = d.m.tz;
    e->has_furnace = d.m.has_furnace, e->fx = d.m.fx, e->fy = d.m.fy, e->fz = d.m.fz;
    e->carry_ms = d.m.ms, e->dims = d.dims;
#if NETH_RL_MACROS
    if (d.has_nt) chain_nt_put(e, &d.nt);
#endif
    return 0;
}

/* A seam check drops the in-process controller after writing its sidecar, so the next start can only get the
 * controller back through the disk read. The saved world image remains live. */
int neth_vec_chain_memory_forget(struct neth_vec *v, int start)
{
    if (!v->chain || start < 0 || start >= v->ncmem || !v->cmem[start].valid) return -1;
    free(v->cmem[start].nt);
    v->cmem[start].nt = NULL;
    free(v->cmem[start].state);
    v->cmem[start].state = NULL;
    return 0;
}

int neth_vec_chain_controller_probe(const struct neth_vec *v, int id,
                                    struct neth_vec_chain_controller_probe *out)
{
    if (!v->chain || id < 0 || id >= v->n || !out) return -1;
    const struct vec_env *e = &v->e[id];
    memset(out, 0, sizeof *out);
    out->steps = e->steps;
    out->portal_step = e->pp_step;
    out->portal_x = e->pp_x, out->portal_y = e->pp_y, out->portal_z = e->pp_z;
    out->food_trip = e->fs_trip;
    out->strip_phase = e->st3;
    out->dragon_phase = e->dg.phase;
#if NETH_RL_MACROS
    out->stronghold_wait_at = e->sh.fw1;
    out->pearl_phase = e->pearl.phase;
#endif
    return 0;
}

size_t neth_vec_chain_controller_bytes(const struct neth_vec *v, int id, void *out, size_t cap)
{
    if (!v->chain || id < 0 || id >= v->n) return 0;
    struct chain_macro_state *state = chain_state_keep(&v->e[id]);
    if (!state) return 0;
    size_t n = sizeof *state;
    if (out && cap >= n) memcpy(out, state, n);
    free(state);
    return n;
}

int neth_vec_chain_next(struct neth_vec *v, int id, int start, int64_t ep_len)
{
    if (!v->chain || id < 0 || id >= v->n) return -1;
    v->e[id].cnext = start;
    v->e[id].cnext_len = ep_len;
    return 0;
}

int neth_vec_chain_end(struct neth_vec *v, int id)
{
    if (!v->chain || id < 0 || id >= v->n) return -1;
    v->e[id].eplen = v->e[id].te + 1;
    return 0;
}

int neth_vec_chain_play(struct neth_vec *v, int id, const struct neth_rl_act *acts, int64_t n)
{
    if (!v->chain || id < 0 || id >= v->n || n < 0) return -1;
    v->e[id].cplay = n ? acts : NULL;
    v->e[id].cplay_at = 0;
    v->e[id].cplay_n = n;
    return 0;
}

int neth_vec_chain_free(struct neth_vec *v, int id, int n)
{
    if (!v->chain || id < 0 || id >= v->n) return -1;
    if (!v->cfv)
    {
        /* the make's struct with no macro (the screen's and the respawn are always on), sharing every pointer, as
         * neth_vec_level's copies are */
        if (!(v->cfv = malloc(sizeof *v))) return -1;
        *v->cfv = *v;
        v->cfv->lv = v->cfv->lb = v->cfv->cfv = NULL;
        v->cfv->macros = 0;
        v->cfv->picks = 0;
    }
    v->e[id].cfree = n;
    return 0;
}

void neth_vec_step_hook(struct neth_vec *v, int id, const struct pool_result *r, const struct neth_rl_state *s)
{
    uint64_t t0 = now_ns();
    struct vec_env *e = &v->e[id];
    /* the result the next act reads */
    struct vec_last *l = &e->last;
    l->dim = r->priv.dim;
    l->x = r->priv.x;
    l->y = r->priv.y;
    l->z = r->priv.z;
    l->yaw = r->priv.yaw;
    l->pitch = r->priv.pitch;
    l->flags = r->flags;
    l->air = r->priv.air;
    l->food = r->priv.food;
    l->fire = r->priv.fire;
    l->on_ground = r->priv.on_ground;
    l->health = r->priv.health;
    l->wtime = r->priv.world_time;
    l->screen = r->hud.screen;
    l->nslots = r->hud.nslots;
    int ns = r->hud.nslots < 0 ? 0 : r->hud.nslots > 90 ? 90 : r->hud.nslots;
    for (int i = 0; i < ns; ++i)
    {
        l->slot_item[i] = r->hud.slots[i].item;
        l->slot_count[i] = r->hud.slots[i].count;
    }
    l->cur_item = r->hud.cursor.item;
    l->cur_count = r->hud.cursor.count;
    struct mac_books mb = {.at = MB_RESULT0, .v = v, .e = e, .id = id, .r = r, .s = s};
    mac_books(&mb);                         /* (the stronghold's) */
    /* Loop.run at the result: the reward, the return, the first position, the deaths */
    float rew = reward(v, e, r, s);
    if (v->tk.item)
    {
        /* lane/rawskill: the task's reward is the whole reward; its pickup or a death ends the episode now */
        if (v->scan) task_hint_read(v, id, &e->hint_ok, e->hint);  /* lane/rawsearch */
        rew = task_step(v, task_of(v, e), &e->tk, r, s, e->reset_step,
                        e->hint_ok && !v->hint_noshape ? e->hint : NULL);
        if (e->tk.done && !e->reset_step && e->te < e->eplen) e->eplen = e->te;
    }
    if (v->nether_w != 0.0 && r->priv.dim == -1 && !(e->dims & 1) && !e->reset_step) rew += (float)v->nether_w;
    if (!e->reset_step && e->ncells < NETH_VEC_EXPLORE_MAX && r->priv.dim == 0 && r->priv.y < 56.0 && !s->eye_water)
    {
        int paid = e->ncells < v->explore_cap;
        if (explore_new(e, r->priv.x, r->priv.y, r->priv.z) && paid && v->explore_w != 0.0)
            rew += (float)v->explore_w;
    }
    if (r->priv.dim == 1 || e->dg.kill_step >= 0)
    {
        /* lane/dragon: the End's books (vecdragon.c), the macro on or off; the End and the dragon's death paid once
         * an episode at their weights (0 unless a run file names them) */
        int had = e->dg.kill_step >= 0;
        float min_before = e->dg.dragon_min;
        e->dgin.x = r->priv.x, e->dgin.y = r->priv.y, e->dgin.z = r->priv.z;
        e->dgin.dim = r->priv.dim;
        e->dgin.dead = (r->flags & PF_DEAD) != 0;
        const struct neth_rl_end *en = dragon_view(v, e);
        if (!e->reset_step) dragon_books(&e->dg, &e->dgin, en, e->te);
        if (v->crystal_w != 0.0 && en && en->on)
        {
            for (int k = 0; k < en->sv_dead_ncry; ++k)
            {
                int32_t cx = en->sv_dead_pos[k][0], cz = en->sv_dead_pos[k][1];
                int seen = 0;
                for (int q = 0; q < e->cry_dead_n; ++q)
                    if (e->cry_dead_xz[q][0] == cx && e->cry_dead_xz[q][1] == cz) { seen = 1; break; }
                if (!seen && e->cry_dead_n < NETH_RL_END_CRYSTALS)
                {
                    e->cry_dead_xz[e->cry_dead_n][0] = cx;
                    e->cry_dead_xz[e->cry_dead_n][1] = cz;
                    ++e->cry_dead_n;
                    if (!e->reset_step) rew += (float)v->crystal_w;
                }
            }
        }
        if (v->end_w != 0.0 && r->priv.dim == 1 && !(e->dims & 4) && !e->reset_step) rew += (float)v->end_w;
        if (v->dragon_hp_w != 0.0 && min_before > 0.0f && e->dg.dragon_min >= 0.0f && e->dg.dragon_min < min_before && !e->reset_step)
            rew += (float)(v->dragon_hp_w * (double)(min_before - e->dg.dragon_min));
        if (v->dragon_w != 0.0 && !had && e->dg.kill_step >= 0) rew += (float)v->dragon_w;
    }
    stall_step(v, e, r, &rew);              /* lane/speedrun-recipe: the step and stall penalties (off: rew as is) */
    e->ret += (double)rew;
    if (v->save_item > 0 && !e->saved && !e->reset_step && inv_count(s, v->save_item) >= v->save_count) e->want_save = 1;
    if (v->save_item < 0)
    {
        uint32_t mask = (uint32_t)v->save_count;
        uint32_t have = ((uint32_t)e->got | e->carry_ms | (r->priv.dim == -1 ? 1u << NETH_VEC_MILESTONES : 0u) |
                         (r->priv.dim == 1 ? 1u << NETH_VEC_MS_END : 0u) |
                         (e->dg.kill_step >= 0 ? 1u << NETH_VEC_MS_DRAGON : 0u)) & mask;
        if (e->reset_step)
            e->ms0 = have;
        else if (have & ~e->ms0 & ~e->saved_ms)
        {
            e->want_save = 1;
            e->saved_ms |= have & ~e->ms0;
            e->save_level = __builtin_popcount(have);
        }
    }
    if (isnan(e->x0))
    {
        e->x0 = r->priv.x;
        e->z0 = r->priv.z;
    }
    e->deaths += (r->flags & PF_DIED) != 0;
    mb.at = MB_RESULT;
    mac_books(&mb);                         /* (the pearl's, its far view's depth when due: lane/macroperf) */
    if (r->priv.dim >= -1 && r->priv.dim <= 1) e->dims |= 1 << (r->priv.dim + 1);
    if (v->dbench) db_step(v, e, id, r, s);
    if (v->chain)
    {
        /* lane/chain: the result the search reads (neth_vec_chain_peek) */
        struct neth_vec_chain_peek *c = &e->cpk;
        c->x = r->priv.x, c->y = r->priv.y, c->z = r->priv.z;
        c->yaw = r->priv.yaw, c->pitch = r->priv.pitch, c->health = r->priv.health, c->saturation = r->priv.saturation;
        c->dim = r->priv.dim, c->food = r->priv.food, c->air = r->priv.air, c->fire = r->priv.fire;
        c->flags = r->flags, c->kind = e->kind, c->dims = e->dims, c->reset_step = e->reset_step;
        c->ms = (uint32_t)e->got | e->carry_ms | ((e->dims & 4) ? 1u << NETH_VEC_MS_END : 0u) |
                (e->dg.kill_step >= 0 ? 1u << NETH_VEC_MS_DRAGON : 0u);
        c->world_time = r->priv.world_time, c->te = e->te;
        c->st = *s;
    }
    if (!v->rc_dev || v->rc_hostg[id / v->gs])
        for (int q = 0; q < NETH_RL_SEEN; ++q) e->seen_steps[q] += s->seen_n[q] > 0;
    e->ep_steps += 1;
    if (e->reset_step)
    {
        e->reset_step = 0;
        ++e->reset_steps;
    }
    else
    {
        v->b.rewards[id] = rew;
        v->b.truncations[id] = e->te >= e->eplen;
    }
    if (e->te >= e->eplen)
    {
        /* Loop.finish */
        struct neth_vec_episode *c = &e->rec;
        memset(c, 0, sizeof *c);
        c->env = id;
        c->logs = s->mined_logs;
        c->crafted = s->crafted;
        c->ret = e->ret;
        float p[36];
        for (int j = 0; j < 36; ++j)
            for (int k = 0; k < NETH_VEC_ITEMS; ++k)
                if (s->inv[j].item == TEACH_ITEMS[k] && s->inv[j].count > 0) c->items[k] += s->inv[j].count;
        for (int j = 0; j < 36; ++j)
            for (int k = 0; k < 8; ++k)
                if (s->inv[j].item == EXTRA_ITEMS[k] && s->inv[j].count > 0) c->extra[k] += s->inv[j].count;
        c->dims = e->dims;
        c->obsidian = s->mined_obsidian;
        c->kills = s->mob_kills;
        memcpy(c->seen, e->seen_steps, sizeof c->seen);
        for (int j = 0; j < 36; ++j)
        {
            int it = s->inv[j].item, itc = it < 0 ? 0 : it > 4095 ? 4095 : it;
            p[j] = (v->values[itc] * (float)s->inv[j].count) * (float)(it >= 0);
        }
        c->value = npsum36(p);
        c->ach = __builtin_popcountll(s->ach);
        c->deaths = (int32_t)e->deaths;
        c->net = py_hypot2(r->priv.x - e->x0, r->priv.z - e->z0);
        c->ms = (uint32_t)e->got | ((e->dims & 4) ? 1u << NETH_VEC_MS_END : 0u) |
                (e->dg.kill_step >= 0 ? 1u << NETH_VEC_MS_DRAGON : 0u);
        c->start = e->scat;
        c->level = e->level;
        c->cells = e->ncells;
        c->food = r->priv.food;
        memcpy(c->macro, e->macro_n, sizeof c->macro);
        c->steps = e->ep_steps;
        e->ended = 1;
    }
    else
    {
        scalars(v, e, r, s, v->b.scalars + (size_t)id * v->ns);
        mac_mask(v, e, s, v->b.mask + (size_t)id * NETH_VEC_PICKS);    /* lane/picks (craft.c) */
    }
    e->ns += now_ns() - t0;
}

/* ------------------------------------------------------------------ API */

static void *page_alloc(size_t bytes)
{
    size_t n = (bytes + 4095) & ~(size_t)4095;
    void *p = aligned_alloc(4096, n ? n : 4096);
    if (p) memset(p, 0, n ? n : 4096);
    return p;
}

static int queue_group(struct neth_vec *v, int g)
{
    for (int i = 0; i < v->gs; ++i) v->ids[i] = g * v->gs + i;
    /* a decision runs the config's ticks (client.ticks_per_step x client.action_repeat); the act hook holds it
     * over the later ticks. It stepped 1 whatever the config said, so action_repeat never reached a trainer
     * (the trainer's gamma ** ticks_a_step was the only effect: 2026-10-24's k=2 and k=10 arms ran k=1) */
    if (pipe_step(v->p, v->ids, v->gs, v->noop, v->ticks, 0)) return -1;
    v->gstate[g] = G_FLIGHT;
    v->gdone[g] = 0;
    return 0;
}

static int draw_start(const struct neth_vec *v, struct vec_env *e);
static int reset_mixed(struct neth_vec *v, const int *ids, int k, int skip0, char *err, int en);
static int win_keep(struct neth_vec *v, char *err, int en);
static int start_held(const struct neth_vec *v, int s);
static int start_dims(const struct neth_vec *v, int s);

struct neth_vec *neth_vec_make(const struct neth_vec_opts *o, char *err, int en)
{
    int G = o->groups > 0 ? o->groups : 2;
    if (o->env.n <= 0 || o->env.n % G)
    {
        seterr(err, en, "neth_vec_make: n must be a positive multiple of groups%s", "");
        return NULL;
    }
    if (o->objective < 0 || o->objective > NETH_VEC_CHESTS)
    {
        seterr(err, en, "neth_vec_make: no such objective%s", "");
        return NULL;
    }
    mac_init();                             /* lane/macromod: the macros' phase orders */
    struct neth_vec *v = calloc(1, sizeof *v);
    if (mopt_parse(v->mo, o->macro_opts, err, en))   /* lane/macroopts */
    {
        free(v);
        return NULL;
    }
    v->n = o->env.n;
    v->G = G;
    v->gs = v->n / G;
    v->ep_len = o->ep_len > 0 ? o->ep_len : 3600;
    v->macros = o->macros & (NETH_VEC_ALL | NETH_VEC_TABLE2 | NETH_VEC_IRON | NETH_VEC_LATE | NETH_VEC_LIQUID |
                             NETH_VEC_PORTAL | NETH_VEC_END | NETH_VEC_FIGHT | NETH_VEC_CARRY | NETH_VEC_SEEK | NETH_VEC_STRIP |
                             NETH_VEC_FOOD | NETH_VEC_PICK | NETH_VEC_OBSIDIAN | NETH_VEC_NETHER | NETH_VEC_PEARL |
                             NETH_VEC_STRONGHOLD | NETH_VEC_DRAGON | NETH_VEC_PEARLCAVE | NETH_VEC_BPORTAL |
                             NETH_VEC_STRIP2 | NETH_VEC_OFF_BITS);
    v->picks = o->picks == 1 && (v->macros & NETH_VEC_CRAFT);
    v->hotbar = (o->macros & NETH_VEC_HOTBAR) != 0;
    v->aw = NETH_VEC_ACTION + v->hotbar;
    v->late = (o->late_scalars & 1) != 0;
    v->hsee = (o->late_scalars & 2) != 0;  /* lane/ladder2 */
    v->dig = (o->late_scalars & 4) != 0;   /* lane/ladder4 */
    v->ore = (o->late_scalars & 8) != 0;   /* lane/ladder5 */
    v->gv = (o->late_scalars & 16) != 0;   /* lane/ladder10 */
    v->task = (o->late_scalars & 32) != 0; /* lane/acrl */
    mac_make(v);                            /* lane/macromod: the macros' own settings (the pearl's tune) */
    v->ns = NETH_VEC_NSCALAR + (v->late ? NETH_VEC_LATE_N : 0) + (v->hsee ? NETH_VEC_HSEE_N : 0) +
            (v->dig ? NETH_VEC_DIG_N : 0) + (v->ore ? NETH_VEC_ORE_N : 0) + (v->gv ? NETH_VEC_GV_N : 0) +
            (v->task ? NETH_VEC_TASK_N : 0);
    v->rel = o->reward_rel != 0;
    task_cfg_set(&v->tk, o);               /* lane/rawskill */
    v->hint_noshape = o->task_hint_noshape != 0;   /* lane/acrl */
    if (o->task_item > 0 && o->task_item2 > 0)
    {
        /* lane/acrl: the second task, task_item's settings with its own item, block and seen slot */
        struct neth_vec_opts o2 = *o;
        o2.task_item = o->task_item2;
        o2.task_block = 0;
        task_cfg_set(&v->tk2, &o2);
        v->from2 = o->task_from2;
        if (o->task_hint || o->task_count > 1)
        {
            seterr(err, en, "neth_vec_make: task_item2 with task_hint or task_count%s", "");
            neth_vec_free(v);
            return NULL;
        }
    }
    v->objective = o->objective;
    memcpy(v->w, o->weights ? o->weights : WEIGHTS[o->objective], sizeof v->w);
    v->msw_on = o->ms_weights != NULL;
    if (o->ms_weights) memcpy(v->msw, o->ms_weights, sizeof v->msw);
    v->log_cap = o->log_cap > 0 ? o->log_cap : 0;
    if (o->item_caps)
    {
        v->caps = malloc(4096 * sizeof *v->caps);
        memcpy(v->caps, o->item_caps, 4096 * sizeof *v->caps);
    }
    neth_vec_tables(v->values, v->camdeg);
    if (o->values) memcpy(v->values, o->values, sizeof v->values);
    if (o->camdeg) memcpy(v->camdeg, o->camdeg, sizeof v->camdeg);
    vec_tabs_init(&v->T);
    int n = v->n;
    v->e = calloc((size_t)n, sizeof *v->e);
    v->b.actions = page_alloc((size_t)n * v->aw * sizeof(int32_t));
    v->b.scalars = page_alloc((size_t)n * v->ns * sizeof(float));
    v->b.rewards = page_alloc((size_t)n * sizeof(float));
    v->b.terminals = page_alloc((size_t)n);
    v->b.truncations = page_alloc((size_t)n);
    v->b.driver = page_alloc((size_t)n);
    v->b.kind = page_alloc((size_t)n);
    v->b.chosen = page_alloc((size_t)n);
    v->b.acts = page_alloc((size_t)n * sizeof(struct neth_rl_act));
    v->b.label = page_alloc((size_t)n * sizeof(struct neth_vec_label));
    v->b.te = page_alloc((size_t)n * sizeof(int64_t));
    v->b.picks = page_alloc((size_t)n * sizeof(int32_t));
    v->b.mask = page_alloc((size_t)n * NETH_VEC_PICKS);
    for (int i = 0; i < n; ++i) v->b.mask[(size_t)i * NETH_VEC_PICKS] = 1;
    v->noop = calloc((size_t)v->gs, sizeof *v->noop);
    v->ids = calloc((size_t)n, sizeof *v->ids);
    v->res = calloc((size_t)n, sizeof *v->res);
    v->gstate = calloc((size_t)G, sizeof *v->gstate);
    v->gdone = calloc((size_t)G, sizeof *v->gdone);
    v->ready = calloc((size_t)G, sizeof *v->ready);
    v->ring_cap = o->ring > 0 ? o->ring : 65536;
    v->ring = calloc((size_t)v->ring_cap, sizeof *v->ring);
    for (int i = 0; i < v->gs; ++i)
    {
        v->noop[i].kind = PA_AGENT;
        v->noop[i].hotbar = -1;
    }
    for (int i = 0; i < n; ++i)
    {
        env_reset(&v->e[i]);
        v->e[i].eplen = v->ep_len;
        v->e[i].cnext = -1;                 /* lane/chain */
        v->b.actions[(size_t)i * v->aw] = v->b.actions[(size_t)i * v->aw + 1] = NETH_VEC_CAM / 2;
    }
    v->save_item = o->save_item > 0 ? o->save_item : o->save_item < 0 ? -1 : 0;   /* -1: lane/msstarts' milestones */
    v->save_count = o->save_count > 0 ? o->save_count : 1;
    v->save_pool = o->save_pool > 0 ? o->save_pool : 32;
    /* every env, start and save replay is an image in one address window (image.h IMG_SLOTS): the pool keeps what
     * the n envs, the mix's starts (two images each, a load's twin), the four save slots' two replays and a margin
     * leave (1024 envs and three starts: 40; past it each save failed "the address window is full") */
    v->pin = o->start_pin != 0;
    v->dbench = o->dbench != 0;
    v->cycle = o->start_cycle != 0 && o->nstarts > 0;
    /* lane/seedwin: a window of the mix's starts loaded at a time (none when the window holds the whole mix) */
    if (o->nstarts > 0 && o->start_window > 0 && o->start_window < o->nstarts)
    {
        if (v->pin)
        {
            seterr(err, en, "neth_vec_make: start_pin with a start window (its rounds move every env's start)%s", "");
            neth_vec_free(v);
            return NULL;
        }
        v->win = o->start_window;
        v->stride = o->start_stride > 0 ? o->start_stride : v->win / 2 > 0 ? v->win / 2 : 1;
        for (int k = 2; o->start_weights && k <= o->nstarts; ++k)
            if (o->start_weights[k] != o->start_weights[1])
            {
                seterr(err, en, "neth_vec_make: start_window takes the mix's starts weighted equally%s", "");
                neth_vec_free(v);
                return NULL;
            }
    }
    /* lane/stagelen: each start's episode length, the opts' clock */
    v->clock = o->clock != 0;
    if (o->start_ep_len)
    {
        if (v->win)
        {
            seterr(err, en, "neth_vec_make: start_ep_len with a start window (its rounds are the groups' resets)%s", "");
            neth_vec_free(v);
            return NULL;
        }
        int ns = o->nstarts > 0 ? o->nstarts : 0;
        v->sep = malloc((size_t)(ns + 1) * sizeof *v->sep);
        for (int k = 0; k <= ns; ++k) v->sep[k] = o->start_ep_len[k] > 0 ? o->start_ep_len[k] : v->ep_len;
        for (int i = 0; i < n; ++i) v->e[i].eplen = v->sep[0];
    }
    /* with a window: the rounds in flight's starts (a group a round ahead, the next round's loading) and a margin */
    int loaded = v->win ? v->win + 3 * v->stride : o->nstarts;
    int room = (int)IMG_SLOTS - v->n - 2 * (loaded + 1) - 8 - 4;
    room -= 4;                  /* the saves being made when the pool is full and one of them is let go */
    if (v->save_item && v->save_pool > room) v->save_pool = room > 1 ? room : 1;
    v->retire_cap = room - v->save_pool < 0 ? 0 : room - v->save_pool > 64 ? 64 : room - v->save_pool;
    if (v->save_item < 0) v->save_count = (int)((uint32_t)o->save_count & ((1u << NETH_VEC_MS_BITS) - 1));
    v->save_share = o->save_share;
    v->nether_w = o->nether_weight;
    v->end_w = o->end_weight;
    v->dragon_w = o->dragon_weight;
    v->dragon_hp_w = o->dragon_hp_weight;
    v->crystal_w = o->crystal_weight;
    v->step_pen = o->step_pen;             /* lane/speedrun-recipe */
    v->stall_pen = o->stall_pen;
    v->stall_ticks = o->stall_ticks > 0 ? o->stall_ticks : 400;
    v->stall_r2 = o->stall_radius > 0.0 ? o->stall_radius * o->stall_radius : 4.0;
    v->explore_w = o->explore_weight;
    v->explore_cap = o->explore_cap < 0 ? 0 : o->explore_cap > NETH_VEC_EXPLORE_MAX ? NETH_VEC_EXPLORE_MAX : o->explore_cap;
    if (v->save_item)
    {
        v->pool = calloc((size_t)v->save_pool, sizeof *v->pool);
        v->plevel = calloc((size_t)v->save_pool, sizeof *v->plevel);
        v->pseq = calloc((size_t)v->save_pool, sizeof *v->pseq);
        v->pmem = calloc((size_t)v->save_pool, sizeof *v->pmem);
    }
    v->chain = o->chain != 0;               /* lane/chain */
    if (v->chain)
    {
        v->ncmem = (int)IMG_SLOTS + 64;
        v->cmem = calloc((size_t)v->ncmem, sizeof *v->cmem);
    }
    v->h = neth_rl_make_ex(&o->env, o->flags | (v->save_item || v->chain ? NETH_RL_SAVE : 0), err, en);
    if (v->h == NULL)
    {
        neth_vec_free(v);
        return NULL;
    }
    /* lane/proom: the privileged census (neth_rl_far_census) is one switch for every env. Its callers turned it on lazily,
     * from whichever env's books first wanted it (pearl_books under pearl_oracle or pearl_home on every env's first
     * step, provision and food_oracle's scout on a state), so an env stepped before that call had none for its first
     * steps and one after had it: an attempt's trajectory moved with the batch it ran in (s42 blaze_powder attempt 3:
     * from step 1 at 1, 4 and 32 attempts against 8, 16 and 64). Every option that can turn it on turns it on here, for
     * every env before the first step; the lazy calls are then no-ops */
    if (MOPT(MO_PEARL_ORACLE) || MOPT(MO_PEARL_HOME) ||
        ((v->macros & NETH_VEC_FOOD) && (MOPT(MO_FOOD_ORACLE) || MOPT(MO_FOOD_PROVISION))) ||
        MOPT(MO_FOOD_WOOD_ROUTE) == 2)
        neth_rl_far_census(v->h, 1);
    v->p = neth_rl_pipe(v->h);
    if (v->tk.item && v->tk.hint > 0)
    {
        /* lane/rawsearch (task_hint): the privileged scan of the task's block, class 0 */
        uint8_t cls[4096] = {0};
        int64_t sz = 0;
        cls[v->tk.block & 4095] = 1;
        if (!v->ore || neth_rl_scan(v->h, v->tk.hint, TASK_HINT_BELOW, TASK_HINT_ABOVE, 1, cls) ||
            !(v->scan = neth_rl_scan_buf(v->h, &sz)) || sz != (int64_t)sizeof *v->scan)
        {
            seterr(err, en, "neth_vec_make: task_hint needs the seen-object scalars (late_scalars bit 3) and the scan%s",
                   "");
            neth_vec_free(v);
            return NULL;
        }
    }
    /* lane/pearl2: the frame's depth (rlbind.h); lane/macroperf: on demand, the step hook's pearl_depth_due */
    if (v->macros & NETH_VEC_PEARL) neth_rl_far_depth(v->h, 2);
    /* the groups' resets queued without waiting (pool.h pool_async_resets): nothing here reads an env between its
     * reset and its reset step's result, so recv goes on with the other groups while a group's resets run, every
     * start's at once (lane/balance) */
    pool_async_resets(pipe_pool(v->p), 1);
    {
        int ow, oh, dev;
        neth_rl_obs_spec(v->h, &ow, &oh, &v->ticks, &dev);
        if (v->ticks < 1) v->ticks = 1;
    }
    v->b.ticks = v->ticks;
    if (v->ticks > 1)
    {
        /* lane/ticks-per-step: each later tick's act a macro made (vecenv.h neth_vec_bufs.tick_acts) */
        v->b.tick_acts = page_alloc((size_t)n * (size_t)(v->ticks - 1) * sizeof(struct neth_rl_act));
        v->b.tick_macro = page_alloc((size_t)n * (size_t)(v->ticks - 1));
    }
    v->b.obs = neth_rl_semantic(v->h, v->b.obs_shape);
    v->b.obs_bytes = 2;
    if (v->b.obs_shape[1] == 0)
    {
        v->b.obs = neth_rl_raycast(v->h, v->b.obs_shape);
        v->b.obs_bytes = 1;
        void *dev = neth_rl_raycast_device(v->h, v->b.obs_shape);
        if (dev != NULL) v->b.obs = dev, v->b.obs_device = 1, v->rc_dev = 1;
        if (v->b.obs_shape[1] == 0) v->b.obs = NULL, v->b.obs_bytes = 0;
    }
    v->rc_ticket = malloc((size_t)v->G * sizeof *v->rc_ticket);
    v->rc_hostg = calloc((size_t)v->G, 1);
    v->cids = malloc((size_t)v->n * sizeof *v->cids);
    if (v->rc_ticket == NULL || v->rc_hostg == NULL || v->cids == NULL)
    {
        seterr(err, en, "neth_vec_make: out of memory%s", "");
        neth_vec_free(v);
        return NULL;
    }
    for (int g = 0; g < v->G; ++g) v->rc_ticket[g] = -1;
    if (v->rc_dev)
    {
        /* the split: K of the G groups cast on their workers, spread (group g when floor((g + 1) K / G) passes
         * floor(g K / G)) */
        int K = neth_rl_raycast_host_groups(v->h);
        K = K > v->G ? v->G : K;
        for (int g = 0; g < v->G; ++g)
        {
            v->rc_hostg[g] = (g + 1) * K / v->G > g * K / v->G;
            for (int i = g * v->gs; v->rc_hostg[g] && i < (g + 1) * v->gs; ++i)
                if (neth_rl_raycast_split(v->h, i, 1))
                {
                    seterr(err, en, "neth_vec_make: engine.raycast_host_groups: the host frames%s", "");
                    neth_vec_free(v);
                    return NULL;
                }
        }
    }
    neth_vec_attach(v->h, v);
    v->sids = calloc((size_t)v->gs, sizeof *v->sids);
    /* every env its own start draws, mix or not (lane/srng: seeded only under a mix, every env's state was 0, so a
     * group's envs drew in lockstep and a saved start went to the whole group at once) */
    for (int i = 0; i < n; ++i) v->e[i].srng = 0x5DEECE66Dull * (uint64_t)(i + 1);
    for (int i = 0; i < n; ++i) v->e[i].dims = start_dims(v, 0);    /* lane/proom: start 0's carried dimensions */
    if (o->nstarts > 0)
    {
        v->nstart = o->nstarts;
        v->scum = calloc((size_t)v->nstart + 1, sizeof *v->scum);
        double tot = 0.0;
        for (int k = 0; k <= v->nstart; ++k) tot += o->start_weights ? o->start_weights[k] : 1.0;
        double c = 0.0;
        for (int k = 0; k <= v->nstart; ++k)
            v->scum[k] = (c += (o->start_weights ? o->start_weights[k] : 1.0) / tot);
        v->scum[v->nstart] = 1.0;
        v->share0 = v->scum[0];
        if (v->win)
        {
            /* lane/seedwin: round 0's window and round 1's, loading in the background */
            v->cat = calloc((size_t)v->nstart, sizeof *v->cat);
            v->cat_si = malloc((size_t)v->nstart * sizeof *v->cat_si);
            v->cat_want = calloc((size_t)v->nstart, 1);
            v->ground = calloc((size_t)v->G, sizeof *v->ground);
            for (int k = 0; k < v->nstart; ++k) v->cat[k] = strdup(o->starts[k]), v->cat_si[k] = -1;
            if (win_keep(v, err, en))
            {
                neth_vec_free(v);
                return NULL;
            }
        }
        else
        {
            /* every start at once on the workers (one at a time took 65 s for 99 seeds' starts, lane/seedwin) */
            for (int k = 0; k < v->nstart; ++k)
                if (neth_rl_start_load_bg(v->h, o->starts[k], err, en) != k + 1)
                {
                    neth_vec_free(v);
                    return NULL;
                }
            for (int k = 0; k < v->nstart; ++k)
                if (neth_rl_start_wait(v->h, k + 1, err, en))
                {
                    neth_vec_free(v);
                    return NULL;
                }
        }
        for (int i = 0; i < n; ++i)
        {
            v->e[i].start = draw_start(v, &v->e[i]);
            v->ids[i % v->gs] = i;
            if (i % v->gs == v->gs - 1 && reset_mixed(v, v->ids, v->gs, 1, err, en))
            {
                neth_vec_free(v);
                return NULL;
            }
        }
    }
    /* Loop.run: every group reset (neth_rl_make did) and stepped once with no act */
    for (int g = 0; g < G; ++g)
        if (queue_group(v, g))
        {
            seterr(err, en, "neth_vec_make: the first step was refused%s", "");
            neth_vec_free(v);
            return NULL;
        }
    return v;
}

void neth_vec_free(struct neth_vec *v)
{
    if (v == NULL) return;
    if (v->h)
    {
        /* the steps in flight end before the envs go */
        int any = 0;
        for (int g = 0; g < v->G; ++g) any |= v->gstate[g] == G_FLIGHT;
        while (any && pipe_poll(v->p, v->n, v->res) > 0) {}
        neth_rl_free(v->h);
    }
    for (int i = 0; v->e && i < v->n; ++i)
        if (v->e[i].tape) fclose(v->e[i].tape);
    for (int k = 0; v->cmem && k < v->ncmem; ++k)
    {
        free(v->cmem[k].nt);
        free(v->cmem[k].state);
    }
    free(v->cmem);                          /* lane/chain */
    free(v->cfv);
    free(v->e);
    free(v->b.actions);
    free(v->b.scalars);
    free(v->b.rewards);
    free(v->b.terminals);
    free(v->b.truncations);
    free(v->b.driver);
    free(v->b.kind);
    free(v->b.chosen);
    free(v->b.acts);
    free(v->b.tick_acts);
    free(v->b.tick_macro);
    free(v->b.label);
    free(v->b.te);
    free(v->b.picks);
    free(v->b.mask);
    free(v->sep);
    free(v->noop);
    free(v->ids);
    free(v->rc_ticket);
    free(v->rc_hostg);
    free(v->cids);
    free(v->res);
    free(v->gstate);
    free(v->gdone);
    free(v->ready);
    free(v->ring);
    free(v->caps);
    free(v->scum);
    for (int k = 0; v->cat && k < v->nstart; ++k) free(v->cat[k]);
    free(v->cat);
    free(v->cat_si);
    free(v->cat_want);
    free(v->ground);
    free(v->sids);
    free(v->pool);
    free(v->plevel);
    free(v->pmem);
    free(v->pseq);
    free(v->lv);                            /* lane/ladder6: the shallow copies (their pointers are v's) */
    free(v->lb);
    free(v);
}

struct nwrl *neth_vec_env(struct neth_vec *v) { return v->h; }

int neth_vec_buffers(struct neth_vec *v, struct neth_vec_bufs *b)
{
    *b = v->b;
    return 0;
}

int neth_vec_action_width(const struct neth_vec *v) { return v->aw; }

static void ring_put(struct neth_vec *v, const struct neth_vec_episode *c)
{
    if (v->ring_n == v->ring_cap)
    {
        v->ring_head = (v->ring_head + 1) % v->ring_cap;
        --v->ring_n;
        ++v->dropped;
    }
    v->ring[(v->ring_head + v->ring_n) % v->ring_cap] = *c;
    ++v->ring_n;
    ++v->episodes;
}

static double draw_u(struct vec_env *e)
{
    uint64_t z = (e->srng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * 0x1.0p-53;
}

/* the next start of an env: a saved one with share save_share once there is
 * one, else the mix's (0 without a mix) */
static int draw_start_mix(const struct neth_vec *v, struct vec_env *e);

/* lane/proom: the dimensions a library start's episode has been in before its first step (the dims bits: 1 the
 * Nether, 4 the End): its stage (neth_rl_start_stage) past stages.h's nether entry has visited the Nether, past its end
 * entry the End. A start outside a library, a saved start (its chain memory carries its own) and spawn's carry none,
 * so every reader of e->dims (the macros' "the Nether visited" tests, the chain's rungs) reads a library start as the
 * kill that made it would have */
static int start_dims(const struct neth_vec *v, int s)
{
    const char *st = neth_rl_start_stage(v->h, s);
    if (st == NULL) return 0;
    int j = -1, nether = -1, end = -1;
    for (int k = 0; k < STAGE_N; ++k)
    {
        if (!strcmp(STAGE_CHAIN[k].name, st)) j = k;
        if (STAGE_CHAIN[k].kind == STAGE_DIM && STAGE_CHAIN[k].v == -1) nether = k;
        if (STAGE_CHAIN[k].kind == STAGE_DIM && STAGE_CHAIN[k].v == 1) end = k;
    }
    return (j > nether ? 1 : 0) | (j > end ? 4 : 0);
}

/* lane/stagelen: and the episode's length, its start's (a saved start's ep_len) */
static int draw_start(const struct neth_vec *v, struct vec_env *e)
{
    int s = draw_start_mix(v, e);
    e->dims = start_dims(v, s);     /* lane/proom (after env_reset's 0) */
    e->eplen = v->sep && e->scat >= 0 && e->scat <= v->nstart ? v->sep[e->scat] : v->ep_len;
    return s;
}

static int draw_start_mix(const struct neth_vec *v, struct vec_env *e)
{
    e->level = 0;
    e->scat = 0;
    if (v->npool && draw_u(e) < v->save_share)
    {
        int k = (int)(draw_u(e) * v->npool);
        k = k < v->npool ? k : v->npool - 1;
        if (v->save_item < 0)
        {
            /* lane/msstarts: a level present at random (the deep ones as often as the shallow), then one of its */
            int lv[NETH_VEC_MS_BITS + 1], nlv = 0;
            for (int j = 0; j < v->npool; ++j)
            {
                int seen = 0;
                for (int q = 0; q < nlv && !seen; ++q) seen = lv[q] == v->plevel[j];
                if (!seen) lv[nlv++] = v->plevel[j];
            }
            int li = (int)(draw_u(e) * nlv);
            int want = lv[li < nlv ? li : nlv - 1], m = 0;
            for (int j = 0; j < v->npool; ++j) m += v->plevel[j] == want;
            int pick = (int)(draw_u(e) * m);
            pick = pick < m ? pick : m - 1;
            for (int j = 0; j < v->npool; ++j)
                if (v->plevel[j] == want && pick-- == 0) k = j;
            e->level = v->plevel[k];
        }
        const struct start_mem *m = &v->pmem[k];
        e->has_table = m->has_table, e->tx = m->tx, e->ty = m->ty, e->tz = m->tz;
        e->has_furnace = m->has_furnace, e->fx = m->fx, e->fy = m->fy, e->fz = m->fz;
        e->carry_ms = m->ms;
        e->scat = v->nstart + 1;
        return v->pool[k];
    }
    if (!v->nstart) return 0;
    if (v->cycle && !v->win)
    {
        /* lane/eval1k start_cycle: the env's j-th episode plays start (index + j * n) mod nstart, no draw */
        int k = 1 + (int)(((int64_t)(e - v->e) + (int64_t)e->cyc++ * v->n) % v->nstart);
        e->scat = k;
        return k;
    }
    if (v->pin && e->pin)
    {
        /* lane/rawskill start_pin: the env's first start again, no draw */
        e->scat = e->pin - 1;
        return e->pin - 1;
    }
    uint64_t z = (e->srng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    double u = (double)(z >> 11) * 0x1.0p-53;
    if (v->win)
    {
        /* lane/seedwin: the make's start at its share, else one of the group's round's window */
        if (u < v->share0) return 0;
        int j = (int)((u - v->share0) / (1.0 - v->share0) * v->win);
        j = j < v->win ? j : v->win - 1;
        int64_t r = v->ground[(e - v->e) / v->gs];
        int c = (int)((r * v->stride + j) % v->nstart);
        e->scat = 1 + c;
        return v->cat_si[c];
    }
    int k = 0;
    while (k < v->nstart && u >= v->scum[k]) ++k;
    e->scat = k;
    if (v->pin) e->pin = k + 1;
    return k;
}

int neth_vec_env_start(const struct neth_vec *v, int id)
{
    return id >= 0 && id < v->n ? v->e[id].scat : -1;
}

int neth_vec_start_weights(struct neth_vec *v, const double *w)
{
    if (!v->nstart || v->win || v->pin || !w) return -1;   /* lane/rawskill: a pinned env draws no more */
    double tot = 0.0;
    for (int k = 0; k <= v->nstart; ++k)
    {
        if (!(w[k] >= 0.0)) return -1;
        tot += w[k];
    }
    if (!(tot > 0.0)) return -1;
    double c = 0.0;
    for (int k = 0; k <= v->nstart; ++k) v->scum[k] = (c += w[k] / tot);
    v->scum[v->nstart] = 1.0;
    v->share0 = v->scum[0];
    return 0;
}

/* lane/seedwin: the starts of the rounds in flight loaded (each group's round's window, and the next round's
 * loading in the background), every other one freed once no env plays from it (start_held) */
static int win_keep(struct neth_vec *v, char *err, int en)
{
    int64_t lo = v->ground[0], hi = v->ground[0];
    for (int g = 1; g < v->G; ++g)
    {
        lo = v->ground[g] < lo ? v->ground[g] : lo;
        hi = v->ground[g] > hi ? v->ground[g] : hi;
    }
    memset(v->cat_want, 0, (size_t)v->nstart);
    for (int64_t r = lo; r <= hi + 1; ++r)
        for (int j = 0; j < v->win; ++j) v->cat_want[(r * v->stride + j) % v->nstart] = 1;
    for (int c = 0; c < v->nstart; ++c)
        if (v->cat_want[c] && v->cat_si[c] < 0)
        {
            if ((v->cat_si[c] = neth_rl_start_load_bg(v->h, v->cat[c], err, en)) < 0) return -1;
            ++v->win_loads;
        }
        else if (!v->cat_want[c] && v->cat_si[c] >= 0 && !start_held(v, v->cat_si[c]))
        {
            neth_rl_start_free(v->h, v->cat_si[c]);
            v->cat_si[c] = -1;
            ++v->win_frees;
        }
    return 0;
}

/* envs ids[0..k) to their drawn starts, one neth_rl_reset a start; skip0: the
 * envs drawn start 0 are there already (neth_rl_make's reset) */
static int reset_mixed(struct neth_vec *v, const int *ids, int k, int skip0, char *err, int en)
{
    for (int i = 0; i < k; ++i)
    {
        int s = v->e[ids[i]].start, seen = 0;
        for (int j = 0; j < i && !seen; ++j) seen = v->e[ids[j]].start == s;
        if (seen || (skip0 && s == 0)) continue;
        int m = 0;
        for (int j = i; j < k; ++j)
            if (v->e[ids[j]].start == s) v->sids[m++] = ids[j];
        if (neth_rl_reset(v->h, v->sids, m, s, err, en)) return -1;
    }
    return 0;
}

/* lane/msstarts: saved start st of level lv into the pool; a full pool lets
 * go the oldest start no env plays from of its most kept level (the next
 * most kept if all of that one's are played), else the new one */
/* lane/evict: pool slot j's start let go: its image freed now when no env plays from it, else retired (out of the
 * draws at once, freed by retire_sweep once the envs that play from it have reset: a start an env holds keeps its
 * image until the env's reset); 0 when the retired list is full (nothing let go) */
/* lane/evict: start s still held: an env plays from it, or a save being made replays its env's acts from it (a
 * start let go while its save replayed crashed the replay's tick, ticks.c heap_push) */
static int start_held(const struct neth_vec *v, int s)
{
    for (int i = 0; i < v->n; ++i)
        if (v->e[i].start == s) return 1;
    for (int q = 0; q < v->npending; ++q)
        if (v->pending_src[q] == s) return 1;
    return 0;
}

static int pool_let_go(struct neth_vec *v, int j)
{
    int used = start_held(v, v->pool[j]);
    if (!used)
        neth_rl_start_free(v->h, v->pool[j]);
    else if (v->nretire < v->retire_cap)
        v->retire[v->nretire++] = v->pool[j];
    else
        return 0;
    return 1;
}

/* lane/evict: the retired starts no env plays from any more, freed */
static void retire_sweep(struct neth_vec *v)
{
    for (int q = 0; q < v->nretire;)
    {
        if (start_held(v, v->retire[q]))
        {
            ++q;
            continue;
        }
        neth_rl_start_free(v->h, v->retire[q]);
        v->retire[q] = v->retire[--v->nretire];
    }
}

static void pool_add_level(struct neth_vec *v, int st, int lv, const struct start_mem *mem)
{
    int k = -1;
    if (v->npool < v->save_pool)
        k = v->npool++;
    else
    {
        int cnt[NETH_VEC_MS_BITS + 1] = {0};
        for (int j = 0; j < v->npool; ++j) ++cnt[v->plevel[j]];
        ++cnt[lv];      /* the new one counts toward its level */
        /* the oldest start of the most kept level, never a level's last; an unplayed one first, then (lane/evict)
         * a played one, retired: with every env drawing its own start (lane/srng) every start of a pool far smaller
         * than the envs is always played, and the first start of each new level was let go instead (a random-play
         * archive of 16 stopped at levels 1 and 2 for 20 minutes) */
        for (int round = 0; round < 2 && k < 0; ++round)
        {
            int tried[NETH_VEC_MS_BITS + 1] = {0};
            for (int pass = 0; pass < NETH_VEC_MS_BITS + 1 && k < 0; ++pass)
            {
                int top = -1;
                for (int l = 0; l < NETH_VEC_MS_BITS + 1; ++l)
                    if (!tried[l] && cnt[l] > 1 && (top < 0 || cnt[l] > cnt[top])) top = l;
                if (top < 0) break;
                tried[top] = 1;
                for (int j = 0; j < v->npool; ++j)
                {
                    if (v->plevel[j] != top || (k >= 0 && v->pseq[j] >= v->pseq[k])) continue;
                    if (round == 1 || !start_held(v, v->pool[j])) k = j;
                }
            }
        }
        if (k < 0 || !pool_let_go(v, k))
        {
            neth_rl_start_free(v->h, st);
            return;
        }
    }
    v->pool[k] = st;
    v->pmem[k] = *mem;
    v->plevel[k] = lv;
    v->pseq[k] = ++v->nseq;
}

/* lane/ownstarts: the group's envs that met the save condition saved (as
 * many as the library's save slots take; the rest try again at their next
 * receive), the saves made since moved into the pool */
static void own_saves(struct neth_vec *v, int g)
{
    if (v->nretire) retire_sweep(v);
    for (int q = 0; q < v->npending;)
    {
        int st = neth_rl_start_ready(v->h, v->pending[q]);
        if (st == 0)
        {
            ++q;
            continue;
        }
        if (st == 1 && v->save_item < 0)
            pool_add_level(v, v->pending[q], v->pending_level[q], &v->pending_mem[q]);
        else if (st == 1)
        {
            if (v->npool == v->save_pool)
            {
                /* the oldest start no env plays from let go (a start an env holds keeps its image until the env's
                 * reset, and the address window counts it); none: the oldest retired (lane/evict), else the new
                 * one let go instead */
                int k = -1;
                for (int j = 0; j < v->save_pool && k < 0; ++j)
                {
                    int c = (v->pool_head + j) % v->save_pool;
                    if (!start_held(v, v->pool[c])) k = c;
                }
                if (k < 0) k = v->pool_head;
                if (!pool_let_go(v, k))
                    neth_rl_start_free(v->h, v->pending[q]);
                else
                {
                    v->pool[k] = v->pending[q];
                    v->pmem[k] = v->pending_mem[q];
                    v->pool_head = (k + 1) % v->save_pool;
                }
            }
            else
            {
                v->pmem[v->npool] = v->pending_mem[q];
                v->pool[v->npool++] = v->pending[q];
            }
        }
        else
            ++v->saves_failed;
        --v->npending;
        v->pending[q] = v->pending[v->npending];
        v->pending_level[q] = v->pending_level[v->npending];
        v->pending_src[q] = v->pending_src[v->npending];
        v->pending_mem[q] = v->pending_mem[v->npending];
    }
    char err[256];
    for (int i = g * v->gs; i < (g + 1) * v->gs && v->npending < 64; ++i)
    {
        struct vec_env *e = &v->e[i];
        /* between macros only (lane/msstarts): a macro's own state is not in the start, so one saved mid-smelt
         * left the ore in a furnace nothing came back for */
        if (!e->want_save || e->saved || e->kind != M_NONE) continue;
        int st = neth_rl_start_save(v->h, i, err, sizeof err);
        if (st < 0) break;
        e->saved = v->save_item > 0;
        e->want_save = 0;
        v->pending_level[v->npending] = e->save_level;
        v->pending_src[v->npending] = e->start;
        v->pending_mem[v->npending] = (struct start_mem){e->has_table, e->has_furnace, e->tx, e->ty, e->tz, e->fx, e->fy, e->fz,
                                                         e->ms0 | e->saved_ms};
        v->pending[v->npending++] = st;
        ++v->saves;
    }
}

/* group g's steps are all done: its episodes' end (records, the reset and
 * its step: 1, the group in flight again), else 0. lane/stagelen: the envs
 * whose episode ended, the group's others held (without start_ep_len a
 * group's envs end together, the whole group as before) */
static int group_done(struct neth_vec *v, int g, char *err, int en)
{
    int lo = g * v->gs, k = 0;
    for (int i = lo; i < lo + v->gs; ++i)
        if (v->e[i].ended) v->ids[k++] = i;
    if (k == 0) return 0;
    uint64_t t0 = now_ns();
    if (v->rc_dev && !v->rc_hostg[g])
    {
        /* the episodes' last step: its cast (no policy reads its frame) for the records' seen steps */
        memcpy(v->cids, v->ids, (size_t)k * sizeof *v->cids);
        int tk = neth_rl_raycast_launch(v->h, v->cids, k, RCDEV_THREAD_STREAM);
        if (tk < 0 || neth_rl_raycast_wait(v->h, tk))
        {
            seterr(err, en, "the episodes' last cast failed%s", "");
            return -1;
        }
        for (int q = 0; q < k; ++q)
        {
            int32_t sn[NETH_RL_SEEN];
            neth_rl_raycast_fold(v->h, v->ids[q], sn);
            for (int j = 0; j < NETH_RL_SEEN; ++j) v->e[v->ids[q]].rec.seen[j] += sn[j] > 0;
        }
    }
    struct timespec wall;
    clock_gettime(CLOCK_REALTIME, &wall);
    if (v->win)
    {
        /* lane/seedwin: the group's next round, its window loaded (the next round's loading) */
        ++v->ground[g];
        if (win_keep(v, err, en)) return -1;
    }
    for (int q = 0; q < k; ++q)
    {
        struct vec_env *e = &v->e[v->ids[q]];
        e->rec.t = (double)wall.tv_sec + 1e-9 * (double)wall.tv_nsec;
        ring_put(v, &e->rec);
        env_reset(e);
        e->start = draw_start(v, e);
        if (v->chain && e->cnext >= 0)
        {
            /* lane/chain: the caller's start, and the episode's memory as the save had it */
            e->start = e->cnext;
            e->scat = v->nstart + 1;
            e->eplen = e->cnext_len > 0 ? e->cnext_len : v->ep_len;
            if (e->cnext < v->ncmem && v->cmem[e->cnext].valid)
            {
                const struct chain_mem *m = &v->cmem[e->cnext];
                if (m->state) chain_state_put(e, m->state);
                else
                {
                    e->has_table = m->m.has_table, e->tx = m->m.tx, e->ty = m->m.ty, e->tz = m->m.tz;
                    e->has_furnace = m->m.has_furnace, e->fx = m->m.fx, e->fy = m->m.fy, e->fz = m->m.fz;
                    e->carry_ms = m->m.ms;
                    e->dims = m->dims;
                    if (m->nt) chain_nt_put(e, m->nt);  /* older start without the full controller */
                }
                e->eplen = e->cnext_len > 0 ? e->cnext_len : v->ep_len;
            }
            e->cnext = -1;
        }
    }
    if (reset_mixed(v, v->ids, k, 0, err, en)) return -1;
    if (k == v->gs ? queue_group(v, g) : pipe_step(v->p, v->ids, k, v->noop, v->ticks, 0))
    {
        seterr(err, en, "the reset's step was refused%s", "");
        return -1;
    }
    if (k < v->gs)
    {
        /* the reset envs' step alone: the group is done when they are */
        v->gstate[g] = G_FLIGHT;
        v->gdone[g] = v->gs - k;
    }
    v->reset_ns += now_ns() - t0;
    return 1;
}

/* recv and try_recv: wait 0, -2 when no group is done now */
static int vec_recv(struct neth_vec *v, int wait, char *err, int en)
{
    uint64_t t0 = now_ns();
    for (;;)
    {
        if (v->ready_at < v->nready)
        {
            int g = v->ready[v->ready_at++];
            if (v->ready_at == v->nready) v->ready_at = v->nready = 0;
            v->gstate[g] = G_RECEIVED;
            if (v->save_item) own_saves(v, g);
            for (int i = g * v->gs; i < (g + 1) * v->gs; ++i) v->b.te[i] = v->e[i].te;
            v->recv_ns += now_ns() - t0;
            return g;
        }
        /* wake when the group nearest its end can be done */
        int need = v->n + 1;
        for (int g = 0; g < v->G; ++g)
            if (v->gstate[g] == G_FLIGHT && v->gs - v->gdone[g] < need) need = v->gs - v->gdone[g];
        if (need == v->n + 1)
        {
            seterr(err, en, "neth_vec_recv: no group in flight%s", "");
            return -1;
        }
        int got = pipe_poll_some(v->p, wait ? need : 0, v->n, v->res);
        if (got == 0 && !wait)
        {
            v->recv_ns += now_ns() - t0;
            return -2;
        }
        if (got <= 0)
        {
            seterr(err, en, "neth_vec_recv: nothing in flight%s", "");
            return -1;
        }
        int bad = 0;
        for (int q = 0; q < got; ++q)
        {
            const struct pipe_result *pr = v->res[q];
            if ((pr->r->flags & PF_ERROR) && !bad++)
            {
                /* the engine stopped in this env: the first one's message (the rest still counted) */
                char m[700];
                snprintf(m, sizeof m, "env %d: %.600s", pr->id, pr->r->err);
                seterr(err, en, "%s", m);
            }
            int g = pr->id / v->gs;
            if (++v->gdone[g] < v->gs) continue;
            int rc = group_done(v, g, err, en);
            if (rc < 0) return -1;
            if (rc == 0)
            {
                v->gstate[g] = G_READY;
                v->ready[v->nready++] = g;
            }
        }
        if (bad) return -1;
    }
}

int neth_vec_recv(struct neth_vec *v, char *err, int en) { return vec_recv(v, 1, err, en); }

int neth_vec_try_recv(struct neth_vec *v, char *err, int en) { return vec_recv(v, 0, err, en); }

int neth_vec_cast_groups(struct neth_vec *v, const int *gl, int k, void *stream)
{
    if (!v->rc_dev) return 0;
    int m = 0;
    for (int q = 0; q < k; ++q)
    {
        int g = gl[q];
        if (g < 0 || g >= v->G || v->gstate[g] != G_RECEIVED) return -1;
        if (v->rc_hostg[g])
        {
            /* drawn by its workers: the frames' copy */
            v->rc_ticket[g] = neth_rl_raycast_put(v->h, g * v->gs, v->gs, stream);
            if (v->rc_ticket[g] < 0) return -1;
            continue;
        }
        for (int i = 0; i < v->gs; ++i) v->cids[m++] = g * v->gs + i;
    }
    if (m == 0) return 0;
    /* the groups' envs in one launch (lane/ntcast: a 16x16 launch of 32 envs is under a wave, its time the longest
     * ray's), each group's ticket the launch's */
    int tk = neth_rl_raycast_launch(v->h, v->cids, m, stream);
    for (int q = 0; q < k; ++q)
        if (!v->rc_hostg[gl[q]]) v->rc_ticket[gl[q]] = tk;
    return tk < 0 ? -1 : 0;
}

int neth_vec_cast(struct neth_vec *v, int g, void *stream) { return neth_vec_cast_groups(v, &g, 1, stream); }

int neth_vec_raycast_dump(struct neth_vec *v, int g, const char *path)
{
    if (!v->rc_dev || v->rc_hostg[g] || g < 0 || g >= v->G || v->gstate[g] != G_RECEIVED || v->rc_ticket[g] < 0) return -1;
    if (neth_rl_raycast_wait(v->h, v->rc_ticket[g])) return -1;
    for (int i = 0; i < v->gs; ++i) v->cids[i] = g * v->gs + i;
    return neth_rl_raycast_dump(v->h, v->cids, v->gs, path);
}

int neth_vec_send(struct neth_vec *v, int g)
{
    if (g < 0 || g >= v->G || v->gstate[g] != G_RECEIVED) return -1;
    if (v->rc_dev)
    {
        /* the group's cast done (its worker's next step rewrites what it read) and its seen records in the envs'
         * states before their macros run */
        if (v->rc_ticket[g] < 0 && neth_vec_cast(v, g, RCDEV_THREAD_STREAM)) return -1;
        if (neth_rl_raycast_wait(v->h, v->rc_ticket[g])) return -1;
        v->rc_ticket[g] = -1;
        for (int i = g * v->gs; !v->rc_hostg[g] && i < (g + 1) * v->gs; ++i)
        {
            int32_t sn[NETH_RL_SEEN];
            neth_rl_raycast_fold(v->h, i, sn);
            for (int q = 0; q < NETH_RL_SEEN; ++q) v->e[i].seen_steps[q] += sn[q] > 0;
        }
    }
    return queue_group(v, g);
}

void neth_vec_lat(struct neth_vec *v, double out[6])
{
    struct pipe_stats s;
    pipe_stats(v->p, &s);
    out[0] = (double)s.lat_step_ns;
    out[1] = (double)s.lat_queue_ns;
    out[2] = (double)s.lat_render_ns;
    out[3] = (double)s.lat_poll_ns;
    out[4] = (double)s.lat_trainer_ns;
    out[5] = (double)s.poll_wait_ns;
}

int neth_vec_episodes(struct neth_vec *v, struct neth_vec_episode *out, int max)
{
    int k = 0;
    while (k < max && v->ring_n > 0)
    {
        out[k++] = v->ring[v->ring_head];
        v->ring_head = (v->ring_head + 1) % v->ring_cap;
        --v->ring_n;
    }
    return k;
}

void neth_vec_saves(const struct neth_vec *v, double out[4])
{
    out[0] = (double)v->saves;
    out[1] = (double)v->saves_failed;
    out[2] = (double)v->npool;
    out[3] = (double)v->npending;
}

void neth_vec_window(const struct neth_vec *v, double out[4])
{
    int loaded = 0;
    int64_t lo = v->win ? v->ground[0] : 0;
    for (int c = 0; v->win && c < v->nstart; ++c) loaded += v->cat_si[c] >= 0;
    for (int g = 1; v->win && g < v->G; ++g) lo = v->ground[g] < lo ? v->ground[g] : lo;
    out[0] = (double)v->win_loads;
    out[1] = (double)v->win_frees;
    out[2] = v->win ? (double)loaded : (double)v->nstart;
    out[3] = (double)lo;
}

void neth_vec_stats(struct neth_vec *v, double out[8])
{
    double ps = 0, rs = 0, ns = 0;
    for (int i = 0; i < v->n; ++i)
    {
        ps += (double)v->e[i].policy_steps;
        rs += (double)v->e[i].reset_steps;
        ns += (double)v->e[i].ns;
    }
    uint64_t st[8];
    neth_rl_stats(v->h, st);
    v->tick_ns += st[3];
    out[0] = ps;
    out[1] = rs;
    out[2] = ns;
    out[3] = (double)v->recv_ns;
    out[4] = (double)v->reset_ns;
    out[5] = (double)v->episodes;
    out[6] = (double)v->dropped;
    out[7] = (double)v->tick_ns;
}
