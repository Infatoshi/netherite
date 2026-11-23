/* The macros as mods (lane/macromod): the registration table and the dispatcher, the only part of the folder the env
 * core (csrc/runtime/vecenv.c) calls (mac_*). macro.h has the API a macro fills and the step's phases. To add a macro:
 * its own file here defining a `static const struct vec_macro MAC_NAME` (its hooks, each testing its switch), an
 * #include line below and a line in MACROS with its rank in each phase it takes part in; its switch in vecenv.h, its
 * run-file name in its .sw (neth_vec_macro_switch: what train/loop.c's and brutetab's actions.macros read). The folder compiles into vecenv.c's translation unit (as the macros always did:
 * every function static, no call across a library boundary); a build without it is MACROS=0 (rlbind.mk: vecenv.c
 * against nomacros.c's empty dispatcher, raw controls only). */

#include "macro.h"
#include "common.c"
/* lane/pdig (counting only, neth_vec_walk): the step's last walk primitive and why it returned (WK_*: walk_to 16 +,
 * sho_follow 32 +, the food census walk's straight walk 48 +), the target's distance across and height over the feet,
 * the way's length (walk_to's path, sho_follow's cells) */
#define WK_STAMP(E, CODE, D, DY, N) ((E)->wk = (CODE), (E)->wkd = (int32_t)(D), (E)->wky = (int32_t)(DY), (E)->wkn = (N))
/* ... and on a walking step the cell the look's yaw goes into (the server's blocks, read only): 1 open and a floor, 2 a
 * block at the feet with the head's open and room over (a step up the jump takes), 3 the same with a block over the head
 * or over the step (no room to jump), 4 a wall two high, 5 an edge (a drop of one), 6 a drop of two or more, 7 a liquid
 * at the feet, the head or the floor, 8 not loaded, 9 a block at the head's height only */
static int wk_front(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double yaw)
{
    int id = (int)(e - v->e);
    double rad = yaw * M_PI / 180.0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    int ax = (int)floor(r->x - sin(rad) * 0.8), az = (int)floor(r->z + cos(rad) * 0.8);
    if (ax == fx && az == fz) ax = (int)floor(r->x - sin(rad) * 1.3), az = (int)floor(r->z + cos(rad) * 1.3);
    int b0 = shoracle_block(v->p, id, ax, fy, az, NULL), b1 = shoracle_block(v->p, id, ax, fy + 1, az, NULL);
    int g1 = shoracle_block(v->p, id, ax, fy - 1, az, NULL);
    if (b0 < 0 || b1 < 0 || g1 < 0) return 8;
    /* 10: the client world's (what the player's movement collides with) not the server's at the feet, head or floor */
    if (obsview_block(v->p, id, ax, fy, az, NULL) != b0 || obsview_block(v->p, id, ax, fy + 1, az, NULL) != b1 ||
        obsview_block(v->p, id, ax, fy - 1, az, NULL) != g1)
        return 10;
    if ((b0 >= 8 && b0 <= 11) || (b1 >= 8 && b1 <= 11) || (g1 >= 8 && g1 <= 11)) return 7;
    if (!shoracle_open(b0) && !shoracle_open(b1)) return 4;
    if (!shoracle_open(b0))
        return shoracle_open(shoracle_block(v->p, id, fx, fy + 2, fz, NULL)) &&
               shoracle_open(shoracle_block(v->p, id, ax, fy + 2, az, NULL)) ? 2 : 3;
    if (!shoracle_open(b1)) return 9;
    if (shoracle_floor(g1)) return 1;
    return shoracle_floor(shoracle_block(v->p, id, ax, fy - 2, az, NULL)) ? 5 : 6;
}
#define WK_FRONT(E, YAW) ((E)->wkf = wk_front(v, (E), r, (YAW)))
static int nt_chamber_yield(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r);
static int provision_mode(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s);
static int provision_under(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r);
static int provision_food_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                               const struct neth_rl_state *s);
static int nt_trim(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s, struct neth_rl_act *a, int preportal); /* nether.c, after craft.c */
static void mac_end(struct vec_env *e);     /* (below: fight.c's threat_step, lane/proom) */
static void mac_abort(struct vec_env *e);
#include "craft.c"
#include "x2label.c"
#include "portal.c"
#include "loot.c"
#include "fight.c"
#include "food.c"
#include "nether.c"
#include "dig.c"
#include "armor.c"
#include "stronghold.c"
static int pearl_want(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s);  /* (pearl.c) */
static int pearl_stage(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s); /* (pearl.c) */
static int pearl_dusk(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s);  /* (pearl.c) */
/* lane/pearlgo: pearl_night_first's surface wanted (the strip not, its climb out to the land's height): on, while the
 * night's hunt is wanted; day, from the pearl step on whatever the hour (nothing left to dig with blaze held) */
static int pg_surface(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    int m = MOPT(MO_PEARL_NIGHT_FIRST);
    return m == 2 ? pearl_stage(v, r, s) : m == 1 && (pearl_want(v, r, s) || pearl_dusk(v, r, s));
}
/* lane/pearlgo (nether_done held, lane/pearlhunt's): a blaze rod, blaze powder or an eye of ender held, the portal the
 * obsidian search and the bucket portal serve is behind the chain (a library start past the Nether has no visit in its
 * episode's dimensions: from the blaze rod starts the obsidian search dug the day away, and the climb back took the
 * night) */
static int pg_nether_done(const struct neth_vec *v, const struct neth_rl_state *s)
{
    return MOPT(MO_NETHER_DONE) && inv_count(s, BLAZE_ROD) + inv_count(s, BLAZE_POWDER) + inv_count(s, EYE) > 0;
}
static void mac_end(struct vec_env *e);   /* strip2's opt-in full-pack interrupt uses the common end hooks */
static int shore_night_yield(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s);
#include "strip2.c"
#include "seek.c"
#include "obsidian.c"
#include "bportal.c"
#include "lavahit.c"        /* lane/lava: lava_hit (before strip.c: its lava hit) */
#include "strip.c"
#include "walkto.c"
#include "snow.c"           /* lane/rod (after walkto.c: its surface walk) */
#include "pearl.c"
#include "pfloor.c"         /* lane/pground: pearl_floor's rule (after fight.c and food.c) */
#include "surface.c"
#include "provision.c"
#include "shore.c"
#include "screen.c"
#include "mine.c"
#include "end.c"
#include "dragon.c"
#include "aids.c"
#include "shtrace.c"        /* lane/proom: the stronghold latch's trace (read only) */
#include "bptrace.c"        /* lane/bportal: the portal build's trace (read only) */
#include "wood.c"           /* lane/wood: wood_search (after seek.c, fight.c, obsidian.c: seek_begin, fight_kind, ob_solid) */
#include "lavalog.c"        /* lane/lava: chain --lava-log's read of the lava tests (read only, books) */

/* The registration table: a line a macro, its rank in each ordered phase (1 first, 0 not in it). The ranks are the
 * order the code before lane/macromod called them in (vecenv.c's macro_start, decode's candidate test and books,
 * macro_drive's kinds and mbit_of): the start a priority, the candidate test in order because the stronghold's latches
 * its start as it tests (what the later tests read), the observe because the strip's lava check reads the Nether's
 * state before the Nether's books move it, the drive and the label within the kind a macro runs as (the label names the
 * strip while the seek it handed off to drives). The interrupts run in the observe order; policy, aid, after, end,
 * abort and reset in the table's (each writes only its own) */
static const struct mac_reg {
    const struct vec_macro *m;
    int8_t start, cand, observe, drive, label;
} MACROS[] = {
    /* the macro       start cand observe drive label */
    {&MAC_SURFACE,        1,   3,   0,     12,   16},
    {&MAC_SCREEN,         2,   1,   0,     13,    1},
    {&MAC_LOOT,           3,   4,   0,     14,    2},
    {&MAC_FIGHT,          4,   7,   0,      4,   14},
    {&MAC_EAT,            5,  15,   8,     15,    3},   /* (observe 8: food_break's interrupt, after the lava's) */
    {&MAC_NETHER,         6,   2,   5,      2,    6},
    {&MAC_DRAGON,         7,  17,   0,      1,    5},
    {&MAC_PEARL,          8,  18,   0,      3,    8},
    {&MAC_HUNT,           9,  16,   0,     11,   13},   /* (drive 11: food_scout's, before the swim's M_SURFACE) */
    {&MAC_SNOW_FIRST,    10,  12,   0,      0,    0},   /* lane/snowleg: snow_first (an option; before the
                                                          * stronghold's start, which it holds back) */
    {&MAC_STRONGHOLD,    10,  12,  11,      5,    7},   /* (observe 11: lane/proom's stronghold_room_start, last) */
    {&MAC_BPORTAL,       11,  14,   7,      6,   10},
    {&MAC_OBSIDIAN,      12,  13,   6,      7,    9},
    {&MAC_STRIP,         13,  11,   4,      9,   11},
    {&MAC_STRIP2,         6,  13,   3,     10,   13},   /* late pack trim before the food hunt and craft */
    {&MAC_MINE,          14,   5,   0,     11,   15},
    {&MAC_WOOD,          15,  10,   0,      8,   12},   /* lane/wood: wood_search (an option; before the seek in each
                                                          * of its ranks, after the mine lock's start) */
    {&MAC_SEEK,          15,  10,   2,      8,   12},
    {&MAC_END,           16,   6,   0,      0,    0},
    {&MAC_CRAFT,         17,   8,   1,     16,    4},
    {&MAC_AIDS,           0,   0,   0,      0,    0},
    {&MAC_ARMOR,         18,  19,   9,      1,    0},   /* lane/rod: armor_kit (the option its switch; drive 1: its
                                                          * M_MINE run claimed before the mine's and the seek's) */
    {&MAC_SNOW,          19,  20,  10,      1,    0},   /* lane/rod: snow_kit (the option its switch) */
};
#define MAC_N ((int)(sizeof MACROS / sizeof *MACROS))

enum { MP_START, MP_CAND, MP_OBSERVE, MP_DRIVE, MP_LABEL, MP_N };

/* each phase's macros in rank order, built once (mac_init, from neth_vec_make) and only read after */
static const struct vec_macro *mac_ord[MP_N][MAC_N];
static int mac_nord[MP_N];
static pthread_once_t mac_once = PTHREAD_ONCE_INIT;

static int mac_rank(const struct mac_reg *g, int p)
{
    return p == MP_START ? g->start : p == MP_CAND ? g->cand : p == MP_OBSERVE ? g->observe : p == MP_DRIVE ? g->drive
                                                                                                          : g->label;
}

static void mac_order(void)
{
    for (int p = 0; p < MP_N; ++p)
        for (int k = 1; k < 128; ++k)
            for (int i = 0; i < MAC_N; ++i)
                if (mac_rank(&MACROS[i], p) == k) mac_ord[p][mac_nord[p]++] = MACROS[i].m;
}

static void mac_init(void) { pthread_once(&mac_once, mac_order); }

/* vecenv.h: the run file's macro names, every registered macro's .sw in the table's order; the i-th, NULL past them */
const char *neth_vec_macro_switch(int i, int32_t *bit)
{
    for (int k = 0; k < MAC_N && i >= 0; ++k)
        for (const struct mac_switch *w = MACROS[k].m->sw; w && w->name; ++w)
            if (i-- == 0)
            {
                if (bit) *bit = w->bit;
                return w->name;
            }
    return NULL;
}

/* an episode's start: every macro's own state, the chests tried, and the labeller's (shadow.c) */
static void mac_reset(struct vec_env *e)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->reset) MACROS[i].m->reset(e);
    e->ntried = 0;
    e->shd.kind = e->shd.age = e->shd.seek = e->shd.ntried = 0;     /* lane/ladder6: the labeller's too */
    memset(&e->shd.c, 0, sizeof e->shd.c);  /* lane/ladder11 */
}

/* the running macro over (macro_end): each macro's run state cleared, no kind */
static void mac_end(struct vec_env *e)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->end) MACROS[i].m->end(e);
    e->kind = M_NONE;
    e->age = 0;
}

static void mac_abort(struct vec_env *e)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->abort) MACROS[i].m->abort(e);
}

static uint32_t mac_label(const struct vec_env *e, const struct vec_macro **who);

/* A pending wood recipe must finish before the water-to-diamond descent. A policy mine lock
 * otherwise keeps the turn while the table recipe sits ready in the inventory. */
static int water_craft_plan(const struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    if (v->picks || !MOPT(MO_WATER_CRAFT_FIRST) || !MOPT(MO_DIA_FIRST) ||
        !(v->macros & NETH_VEC_CRAFT) || (v->macros & NETH_VEC_NOTABLE) ||
        c->r->dim != 0 || c->r->screen != PS_NONE ||
        inv_count(c->s, IRON_PICK) == 0 || inv_count(c->s, WATER_BUCKET) == 0 ||
        inv_count(c->s, DIAMOND_PICK) > 0 || inv_count(c->s, DIAMOND) >= 3)
        return -1;
    int plan = craft_plan(v, c->e, c->r, c->s);
    return plan == C_PLANKS || plan == C_TABLE || plan == C_STICK ? plan : -1;
}

static void mac_observe(struct mac_step *c, struct neth_rl_act *a)
{
    for (int k = 0; k < mac_nord[MP_OBSERVE]; ++k)
        if (mac_ord[MP_OBSERVE][k]->observe) mac_ord[MP_OBSERVE][k]->observe(c, a);
}

/* 1 when any macro could take the step (every test runs: some latch as they test) */
static int mac_cand(struct mac_step *c)
{
    int cand = water_craft_plan(c) >= 0;
    for (int k = 0; k < mac_nord[MP_CAND]; ++k)
        if (mac_ord[MP_CAND][k]->cand) cand |= mac_ord[MP_CAND][k]->cand(c) != 0;
    return cand;
}

static void mac_aid(struct mac_step *c, struct neth_rl_act *a, int cand)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->aid) MACROS[i].m->aid(c, a, cand);
}

static void mac_policy(struct mac_step *c, struct neth_rl_act *a)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->policy) MACROS[i].m->policy(c, a);
}

static void mac_interrupt(struct mac_step *c)
{
    const struct vec_macro *who;
    if (c->e->kind == M_MINE && mac_label(c->e, &who) == NETH_VEC_MINE &&
        water_craft_plan(c) >= 0)
        mac_abort(c->e), mac_end(c->e);
    /* lane/supply (armor_turn=station): a strip mine's run ended on a scan step when the armour has its turn and an
     * ore to go to; the start below tries the armour ahead of the strip mines */
    const struct neth_vec *v = c->v;
    if (MOPT(MO_ARMOR_TURN) && c->e->kind == M_MINE && !c->e->arm.on && c->e->steps % AR_SCAN_EVERY == 0)
    {
        uint32_t lb = mac_label(c->e, &who);
        int tgt = (lb == NETH_VEC_STRIP || lb == NETH_VEC_STRIP2) && arm_turn(c->v, c->e, c->r, c->s)
                      ? arm_target(c->v, c->e, c->s) : 0;
        if (tgt && (c->e->arm.has_ore || arm_scan(c->v, c->e, c->r, tgt)))
            mac_abort(c->e), mac_end(c->e);
    }
    /* lane/supply (armor_craft=first): a strip mine's run ended on a scan step when an armour piece is craftable now */
    if (MOPT(MO_ARMOR_CRAFT) && c->e->kind == M_MINE && !c->e->arm.on && c->e->steps % AR_SCAN_EVERY == 0)
    {
        uint32_t lb = mac_label(c->e, &who);
        if ((lb == NETH_VEC_STRIP || lb == NETH_VEC_STRIP2) && arm_craft_first(c->v, c->e, c->r, c->s) >= 0)
            mac_abort(c->e), mac_end(c->e);
    }
    for (int k = 0; k < mac_nord[MP_OBSERVE]; ++k)
        if (mac_ord[MP_OBSERVE][k]->interrupt) mac_ord[MP_OBSERVE][k]->interrupt(c);
}

/* macros.Macros._start: the first in start order that takes the step */
static void mac_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    int pick = c->pick;
    /* lane/picks: the plan the policy picked, if the macro could make it now; with picks on, the craft macro's only start */
    c->picked = v->picks && pick > 0 && pick < NETH_VEC_PICKS && plan_ok(v, c->e, c->r, c->s, pick - 1) ? pick - 1 : -1;
    /* Carry the placed furnace before a scout or mine starts a long walk away. The regular craft start is last and
     * missed this brief chance in s42n4: its 32 seven-rod nodes held ore and coal but no furnace or table. */
    int pack_first = MOPT(MO_SH_PACK) && c->e->sh.fwait && st_free(c->s) < ST_PACK_FREE_MIN;
    if (!v->picks && !pack_first && (v->macros & NETH_VEC_CRAFT) && MOPT(MO_FURNACE_KIT) == FU_CARRY &&
        furnace_take_want(v, c->e, c->r, c->s))
    {
        craft_begin(v, c->e, c->s, C_TAKE, CP_OPEN);
        return;
    }
    /* lane/supply (armor_craft=first): an armour piece craftable now, before anything walks away from the table */
    /* lane/choice: one decision there, the piece or (armor_choice) a replacement iron pickaxe (armor.c arm_choose) */
    if (!pack_first && arm_choose(v, c->e, c->r, c->s)) return;
    /* lane/supply (armor_table=take): and the placed table, before the armour mine leaves it (armor.c arm_table_take) */
    if (!v->picks && !pack_first && arm_table_take(v, c->e, c->r, c->s))
    {
        craft_begin(v, c->e, c->s, C_TAKE, CP_OPEN);
        return;
    }
    int plan = water_craft_plan(c);
    if (plan >= 0)
    {
        craft_begin(v, c->e, c->s, plan, CP_OPEN);
        return;
    }
    int turn = MOPT(MO_ARMOR_TURN) && arm_turn(v, c->e, c->r, c->s);   /* lane/supply (armor_turn) */
    for (int k = 0; k < mac_nord[MP_START]; ++k)
    {
        const struct vec_macro *m = mac_ord[MP_START][k];
        if (turn && (m == &MAC_STRIP || m == &MAC_STRIP2))
        {
            turn = 0;
            if (MAC_ARMOR.start(c)) return;
        }
        if (m->start && m->start(c)) return;
    }
}

/* the step was the policy's own too: the mine lock its attack started, or (lane/picks) the plan it picked */
static int mac_chosen(const struct mac_step *c)
{
    const struct vec_env *e = c->e;
    return e->kind == M_MINE || (c->v->picks && c->pick > 0 && e->kind == M_CRAFT && !e->eat && e->cplan == c->pick - 1);
}

/* macros.Macros._drive: the running macro's act, the first in drive order whose run it is; 1 when it is over */
static int mac_drive(struct mac_step *c, struct neth_rl_act *a)
{
    c->e->age += 1;
    for (int k = 0; k < mac_nord[MP_DRIVE]; ++k)
    {
        int d = mac_ord[MP_DRIVE][k]->drive ? mac_ord[MP_DRIVE][k]->drive(c, a) : -1;
#ifdef DGW_TRACE
        /* lane/supply (debugging only): the macro that drives env DGW_TRACE_ENV, every 100th step */
        if (d >= 0 && c->e == c->v->e + DGW_TRACE_ENV && c->e->steps % 100 == 0)
            fprintf(stderr, "DRV %lld %s kind %d\n", (long long)c->e->steps, mac_ord[MP_DRIVE][k]->name, c->e->kind);
#endif
        if (d >= 0) return d;
    }
    return 1;
}

/* lane/ntbc: the switch of the macro running (vecenv.h neth_vec_label's mbit; 0 none), and the macro that named it */
static uint32_t mac_label(const struct vec_env *e, const struct vec_macro **who)
{
    *who = NULL;
    if (e->kind == M_NONE) return 0;
    for (int k = 0; k < mac_nord[MP_LABEL]; ++k)
    {
        uint32_t mb = mac_ord[MP_LABEL][k]->label ? mac_ord[MP_LABEL][k]->label(e) : 0;
        if (mb)
        {
            *who = mac_ord[MP_LABEL][k];
            return mb;
        }
    }
    return NETH_VEC_MINE;
}

/* the books at b->at (every macro's, in the table's order), and at MB_DECISION the labeller beside an env at a level
 * (lane/ladder6, shadow.c: its act may replace the step's label) */
static void mac_books(struct mac_books *b)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->books) MACROS[i].m->books(b);
    if (b->at == MB_DECISION && b->e->lvl) shd_step(b->v->lb, b->e, b->s, b->kind, b->a, b->l);
}

static void mac_make(struct neth_vec *v)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->make) MACROS[i].m->make(v);
}

static void mac_decision(struct vec_env *e)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->decision) MACROS[i].m->decision(e);
}

/* lane/ladder14 (NETH_VEC_LBL_FITS): the policy's own window where a macro's recipe would start (the craft macro's) */
static int mac_fits(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->fits && MACROS[i].m->fits(v, e, s)) return 1;
    return 0;
}

/* a policy step's label half where a macro's key would start it (the table key's chance 5, the seek key's 7) */
static int mac_chance(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int kind, int half)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->chance) half = MACROS[i].m->chance(v, e, s, kind, half);
    return half;
}

/* 1: the act's slot change is the env's own pick (policy_tool), not the student's to say */
static int mac_envpick(const struct neth_vec *v, const struct neth_rl_state *s, const struct neth_rl_act *a, int kind)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->envpick && MACROS[i].m->envpick(v, s, a, kind)) return 1;
    return 0;
}

static void mac_mask(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, uint8_t *m)
{
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->mask) MACROS[i].m->mask(v, e, s, m);
}

/* a decision step of the running macro: its drive, its label (lmbit, lhalf: before mac_end clears what names it), a
 * hand-back to the policy (*handed; its act a0 again); 1 when the run is over */
static int mac_run(struct mac_step *c, struct neth_rl_act *a, int *kind, int *handed)
{
    struct vec_env *e = c->e;
    struct neth_rl_act a0 = *a;
    c->kind0 = e->kind;
    e->chalf = e->chand = 0;    /* lane/ladder12: the craft macro's half and hand-off, set by its drive */
    int done = mac_drive(c, a);
    *kind = e->kind;
    const struct vec_macro *who;
    e->lmbit = (int)mac_label(e, &who);
    e->lhalf = who && who->half ? who->half(c, done) : 0;
    *handed = 0;
    for (int i = 0; i < MAC_N; ++i)
        if (MACROS[i].m->after && MACROS[i].m->after(c, a, &a0))
        {
            *handed = 1;
            *kind = M_NONE;
            e->lmbit = e->lhalf = 0;
            done = 1;
            break;
        }
    return done;
}

/* food_provision owns the decision while active. The existing submacros keep their own state and saved bytes; the
 * dispatcher admits only the climb, food and cook work and never falls through to the policy's raw act. */
/* lane/pdig (prov_under_surface=hungry): the surface stage under a roof, trees or ground, by day, the food level under
 * 14: the food macros are allowed and start before the strip */
static int provision_hungry(const struct mac_step *c, int pm)
{
    const struct neth_vec *v = c->v;
    return MOPT(MO_PROV_UNDER_SURFACE) == 2 && pm == PV_SURFACE && food_day(c->r) && c->r->food < 14 &&
           provision_under(v, c->e, c->r);
}

static int provision_allowed(const struct mac_step *c, int pm)
{
    const struct vec_env *e = c->e;
    const struct vec_macro *who;
    uint32_t bit = mac_label(e, &who);
    if (bit == NETH_VEC_SURFACE) return 1;
    if (bit == NETH_VEC_FOOD && provision_hungry(c, pm)) return 1;
    if (bit == NETH_VEC_STRIP && (e->st3 == ST3_OUT || e->st3 == ST3_WOOD)) return 1;
    if (bit == NETH_VEC_SEEK && pm == PV_STOCK && e->seek == 4) return 1; /* the wood-fuel log only */
    if (pm != PV_STOCK) return 0;
    if (bit == NETH_VEC_FOOD) return e->eat || food_day(c->r);
    if (bit == NETH_VEC_CRAFT)
        return e->cplan == C_PLANKS || e->cplan == C_TABLE || e->cplan == C_FURNACE ||
               e->cplan == C_SMELT || e->cplan == C_TAKE;
    return 0;
}

/* lane/pdig (counting only, nothing reads it but neth_vec_lsrc): provision standing idle, why its strip_m_start did not
 * take the step: strip_want0's gates in its order, none that writes (its ob_search is not called): 1
 * shore_night_yield, 2 no strip switch, 3 the strip's stall start would take it (its 400 steps), 6 water, a screen,
 * the strip's off time, 9 the stronghold owns the step, 4 pg_surface (the hunt first), 5 prov_want, 7 past those
 * gates (the obsidian search, the pick, the table, the ore need, the climb's st3_want); + 16 the feet at FOOD_SCOUT_Y
 * or over (under a roof or trees) */
static int provision_idle_why(const struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    const struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    int hi = r->y >= FOOD_SCOUT_Y ? 16 : 0;
    if (shore_night_yield(v, e, r, s)) return hi + 1;
    if (!(v->macros & NETH_VEC_STRIP)) return hi + 2;
    if (e->steps - e->istep > 400 && !sh_owns(v, e, r)) return hi + 3;
    if (r->dim != 0 || r->screen != PS_NONE || s->eye_water || r->y < 4.0 || e->steps < e->soff) return hi + 6;
    if (sh_owns(v, e, r)) return hi + 9;
    if (pg_surface(v, r, s)) return hi + 4;
    if (prov_want(v, e, r, s)) return hi + 5;
    return hi + 7;
}

static void provision_apply(struct mac_step *c, struct neth_rl_act *a, int *kind, int *chosen, int pm)
{
    struct vec_env *e = c->e;
    const struct neth_vec *v = c->v;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    act_noop(a);
    *kind = M_NONE, *chosen = 0;
    c->attack = c->pick = 0, c->picked = -1, c->ph = -1;
    c->ey = r->y + 1.62;
    const int ls = 8 * pm;      /* lane/pdig (counting only): neth_vec_lsrc's stage */
    e->lsrc = ls + VLP_RUN;
    if (pm == PV_SURFACE && provision_exit_prime(v, e, r, s))
    {
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        provision_exit_step(v, e, r, s, a);
        e->lmbit = NETH_VEC_SURFACE, e->lhalf = 0;
        e->lsrc = ls + VLP_EXIT;
        return;
    }
    if (pm == PV_SURFACE && provision_wood_step(v, e, r, s, a))
    {
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        e->lmbit = NETH_VEC_SURFACE, e->lhalf = 0;
        e->lsrc = ls + VLP_WOOD;
        return;
    }
    if (pm == PV_STOCK && provision_complete(v, e, r, s))
    {
        e->fs_home |= PROVISION_DONE;
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        e->lmbit = NETH_VEC_FOOD, e->lhalf = 0;
        e->lsrc = ls + VLP_DONE;
        return;
    }
    if (pm == PV_STOCK) e->fs_home |= PROVISION_LATCH;
    if (e->kind != M_NONE && !provision_allowed(c, pm)) mac_abort(e), mac_end(e);
    if (pm == PV_STOCK && e->kind == M_MINE && e->strip && e->st3 == ST3_WOOD && seek_want(v, e, s) == 3)
    {
        mac_end(e);
        seek_begin(e, r, s, 3);
    }
    if (e->kind == M_NONE && r->screen != PS_NONE)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        e->lmbit = NETH_VEC_FOOD, e->lhalf = 0;
        e->lsrc = ls + VLP_SCREEN;
        return;
    }
    if (e->kind == M_NONE && s->eye_water)
    {
        a->look_mode = 1;
        a->look[0] = r->yaw, a->look[1] = -60.0f;
        a->hold = (1u << PK_JUMP) | (1u << PK_FORWARD);
        e->lmbit = NETH_VEC_FOOD, e->lhalf = 0;
        e->lsrc = ls + VLP_SWIM;
        {   /* lane/pdig (counting only): 10 + the feet under y 56, 56 to 61, 61 and over, + 3 under the open sky (the
             * census's), + 16 at FOOD_SCOUT_Y or over */
            const struct neth_rl_far *sf = neth_rl_far(v->h, (int)(e - v->e));
            int sky = sf && sf->census.on && sf->census.pl_sky;
            e->lsub = (10 + (r->y < 56.0 ? 0 : r->y < 61.0 ? 1 : 2) + 3 * sky + (r->y >= FOOD_SCOUT_Y ? 16 : 0)) << 8;
        }
        return;
    }
    if (e->kind == M_NONE)
    {
        if (pm == PV_STOCK) eat_m_start(c);
        if (e->kind == M_NONE && provision_hungry(c, pm))
        {
            if (food_points(s) > 0) eat_m_start(c);
            if (e->kind == M_NONE && food_points(s) == 0) hunt_m_start(c);
        }
        if (e->kind == M_NONE && provision_under(v, e, r)) strip_m_start(c);
        if (e->kind == M_NONE && pm == PV_STOCK)
        {
            /* A loaded furnace can finish a meat batch now; starting another scout first kept carrying raw meat
             * farther from the cook on the seed-1 stock case. */
            int cook = provision_food_plan(v, e, r, s);
            if (cook >= 0)
            {
                craft_begin(v, e, s, cook, CP_OPEN);
                if (cook == C_TAKE) e->take_f = 1; /* carry the nearby furnace before the next scout */
            }
            if (e->kind == M_NONE && food_day(r) && e->steps % 5 == 0 && hunt_want(v, e, s) >= 0)
                hunt_m_start(c);
            /* Gather the cook's wood while still near the first food ground, before another scout carries the pack
             * into treeless land. Keep eating and the surface climb ahead of it. */
            if (e->kind == M_NONE && (raw_meat(s) >= 8 || food_points(s) >= PROVISION_FOOD) &&
                st3_provision_wood_want(v, e, s))
            {
                if (seek_want(v, e, s) == 3) seek_begin(e, r, s, 3);
                else strip_m_start(c);
            }
            if (e->kind == M_NONE && provision_roam(v, e, r, s, a))
            {
                a->hotbar = -1, a->nops = 0;
                e->lmbit = NETH_VEC_FOOD, e->lhalf = 0;
                e->lsrc = ls + VLP_ROAM;
                return;
            }
            if (e->kind == M_NONE && food_day(r)) hunt_m_start(c);
            int plan = e->kind == M_NONE ? provision_food_plan(v, e, r, s) : -1;
            if (plan >= 0) craft_begin(v, e, s, plan, CP_OPEN);
            if (e->kind == M_NONE && st3_provision_wood_want(v, e, s))
            {
                if (seek_want(v, e, s) == 3) seek_begin(e, r, s, 3);
                else strip_m_start(c);
            }
        }
    }
    if (e->kind != M_NONE)
    {
        int handed;
        int done = mac_run(c, a, kind, &handed);
        if (done) mac_end(e);
        return;
    }
    act_noop(a);
    if (pm == PV_STOCK && !food_day(r)) a->hold = 1u << PK_SNEAK;
    e->lmbit = NETH_VEC_FOOD, e->lhalf = 0;
    e->lsrc = ls + VLP_IDLE;
    e->lsub = provision_idle_why(c) << 8;
}
