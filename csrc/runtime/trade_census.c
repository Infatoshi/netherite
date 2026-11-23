/* trade_census (lane/eyetrade): what the eye-of-ender trade route has to work with, measured on the C engine's own
 * code, no Python. Two modes.
 *
 *   trade_census --census SPAWNS [--radius R] [-v]
 *       SPAWNS: one "seed x z" line a seed (the player's spawn, the tape's first row). The village walk
 *       (structure_walk over structure_village, the world generator's own test: this is a measurement of the world,
 *       never something a macro reads) over the chunks within R blocks (default 512) of each spawn; a village counts
 *       when its start is valid (hasMoreThanTwoComponents). One CENSUS line a seed: villages in reach, the nearest's
 *       distance from spawn to its well, and its villagers by profession (each house's spawnVillagers: house4,
 *       woodhut and house3 farmers (house3 two), house1 a librarian, house2 a blacksmith, the church a priest, the
 *       hall a butcher and a farmer) and its wheat (14 crops a wheat strip of the fields). Then a summary over the
 *       seeds. -v lists every village.
 *
 *   trade_census --econ [--runs N] [--seed S] [--eyes E] [--profs P,P,...] [--tools 0|1]
 *       The trade ladder through csrc/engine/trades.c (trades_add_default, trades_unlock_boost, the 40-tick unlock
 *       rule of villagers.c's villager_player_trade, MerchantRecipe's 7 uses): N villages of the given professions
 *       (default one of each: farmer, librarian, priest, blacksmith, butcher), each villager's first offer drawn as
 *       getRecipes draws it, then a player who trades greedily: sells what the cheapest buy offers want (coal, wheat,
 *       paper, the raw meats, ...) for emeralds, buys eyes from any priest's eye offer, and to reach new offers buys a
 *       villager's last offer (the unlock) when it costs only emeralds (--tools 1 also pays the priest's and
 *       blacksmith's tool-for-enchanted-tool offers with the tool, counted). Counted per run: the goods sold by kind,
 *       emeralds earned and spent (on eyes, on unlocks), unlocks, and whether E eyes were reached; a run stalls when
 *       no offer is left that moves it on. The ECON line: the share of runs reaching E eyes, and the mean goods a run
 *       sold to get there (coal-equivalents: the input the strip mine makes). The villager RNG and the shuffle's are
 *       seeded per run from S; a villager's offers depend on its own Random's position at its first trade, which no
 *       world fixes, so this is a distribution, not a seed's answer. */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/jrand.h"
#include "../engine/seedworld.h"
#include "../engine/structure.h"
#include "../engine/trades.h"
#include "../engine/village.h"

/* ---- census ---- */

#define CENSUS_RADIUS 512 /* blocks around the spawn walked for village starts */
#define WHEAT_STRIP 14    /* crops in one strip of a field (2 columns, z 1..7) */
#define WHEAT_ID 59

struct vill {
    double dist;
    int wx, wz;
    int prof[5]; /* farmer, librarian, priest, blacksmith, butcher */
    int wheat, fields, pieces;
    int desert;
};

static void vill_count(const struct start *s, struct vill *v, int sx, int sz)
{
    memset(v, 0, sizeof *v);
    const struct piece *w = s->pieces[0];
    v->wx = (w->bb.minX + w->bb.maxX) / 2;
    v->wz = (w->bb.minZ + w->bb.maxZ) / 2;
    v->dist = sqrt((double)(v->wx - sx) * (v->wx - sx) + (double)(v->wz - sz) * (v->wz - sz));
    v->pieces = s->n;
    for (int i = 0; i < s->n; ++i)
    {
        const struct piece *p = s->pieces[i];
        if (p->kind != PIECE_VILLAGE) continue;
        if (p->u.village.desert) v->desert = 1;
        switch (p->u.village.kind)
        {
        case V_HOUSE4: case V_WOODHUT: v->prof[0] += 1; break;
        case V_HOUSE3: v->prof[0] += 2; break;
        case V_HOUSE1: v->prof[1] += 1; break;
        case V_CHURCH: v->prof[2] += 1; break;
        case V_HOUSE2: v->prof[3] += 1; break;
        case V_HALL: v->prof[4] += 1; v->prof[0] += 1; break;
        case V_FIELD1:
            ++v->fields;
            v->wheat += WHEAT_STRIP * ((p->u.village.ca == WHEAT_ID) + (p->u.village.cb == WHEAT_ID) +
                                       (p->u.village.cc == WHEAT_ID) + (p->u.village.cd == WHEAT_ID));
            break;
        case V_FIELD2:
            ++v->fields;
            v->wheat += WHEAT_STRIP * ((p->u.village.ca == WHEAT_ID) + (p->u.village.cb == WHEAT_ID));
            break;
        default: break;
        }
    }
}

static int cmp_dist(const void *a, const void *b)
{
    double x = ((const struct vill *)a)->dist, y = ((const struct vill *)b)->dist;
    return x < y ? -1 : x > y;
}

#define ECON_MAXV 64
static int econ(int runs, uint64_t seed, int eyes_want, const int *profs, int nprof, int tools, const char *allow);
static struct { double reach, eyes, em, eye_ge7; } econ_last; /* the last econ call's summary (the tool's state) */
static int econ_quiet;
static long econ_start_emeralds; /* --emeralds: what the player brings (emerald ore, a temple chest) */
/* --census with --econ: each seed's nearest priest village through the ladder (main sets these) */
static int census_econ_runs, census_eyes = 12, census_tools;
static const char *census_allow;

static int census(const char *path, int radius, int verbose)
{
    double sum_reach = 0, sum_eyes = 0;
    int n_econ = 0;
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    long long seed;
    double sx, sz;
    int seeds = 0, with = 0, with_priest = 0, with256 = 0, priest256 = 0;
    double sum_near = 0, sum_near_priest = 0;
    int nb[6] = {0}; /* nearest priest village: <128, <256, <384, <512, none */
    while (fscanf(f, "%lld %lf %lf", &seed, &sx, &sz) == 3)
    {
        int bx = (int)floor(sx), bz = (int)floor(sz);
        struct start_list l = {0};
        structure_walk(&structure_village, (int64_t)seed, (bx - radius) >> 4, (bz - radius) >> 4, (bx + radius) >> 4,
                       (bz + radius) >> 4, &l);
        struct vill v[256];
        int n = 0;
        for (int i = 0; i < l.n && n < 256; ++i)
        {
            const struct start *s = l.v[i];
            if (!s->n || !s->pieces[0]->u.village.valid) continue;
            vill_count(s, &v[n], bx, bz);
            if (v[n].dist <= radius) ++n;
        }
        start_list_free(&l);
        qsort(v, (size_t)n, sizeof *v, cmp_dist);
        int ip = -1;
        for (int i = 0; i < n; ++i)
            if (v[i].prof[2]) { ip = i; break; }
        ++seeds;
        if (n) { ++with; sum_near += v[0].dist; }
        if (n && v[0].dist <= 256) ++with256;
        if (ip >= 0)
        {
            ++with_priest;
            sum_near_priest += v[ip].dist;
            if (v[ip].dist <= 256) ++priest256;
            ++nb[v[ip].dist < 128 ? 0 : v[ip].dist < 256 ? 1 : v[ip].dist < 384 ? 2 : 3];
        }
        else ++nb[4];
        printf("CENSUS seed %lld spawn %d %d villages %d", seed, bx, bz, n);
        if (n)
            printf(" nearest %.0f at %d %d farmer %d librarian %d priest %d blacksmith %d butcher %d wheat %d%s", v[0].dist,
                   v[0].wx, v[0].wz, v[0].prof[0], v[0].prof[1], v[0].prof[2], v[0].prof[3], v[0].prof[4], v[0].wheat,
                   v[0].desert ? " desert" : "");
        if (ip >= 0) printf(" priest_nearest %.0f", v[ip].dist);
        if (ip >= 0 && census_econ_runs > 0)
        {
            /* the priest village's villagers through the trade ladder */
            int profs[ECON_MAXV], np = 0;
            for (int k = 0; k < 5; ++k)
                for (int j = 0; j < v[ip].prof[k] && np < ECON_MAXV; ++j) profs[np++] = k;
            econ_quiet = 1;
            econ(census_econ_runs, (uint64_t)seed, census_eyes, profs, np, census_tools, census_allow);
            printf(" econ_villagers %d reach12 %.4f eyes %.2f ge7 %.3f emeralds %.1f", np, econ_last.reach, econ_last.eyes,
                   econ_last.eye_ge7, econ_last.em);
            sum_reach += econ_last.reach;
            sum_eyes += econ_last.eyes;
            ++n_econ;
        }
        printf("\n");
        if (verbose)
            for (int i = 0; i < n; ++i)
                printf("  village %d at %d %d dist %.0f pieces %d farmer %d librarian %d priest %d blacksmith %d "
                       "butcher %d fields %d wheat %d%s\n",
                       i, v[i].wx, v[i].wz, v[i].dist, v[i].pieces, v[i].prof[0], v[i].prof[1], v[i].prof[2],
                       v[i].prof[3], v[i].prof[4], v[i].fields, v[i].wheat, v[i].desert ? " desert" : "");
    }
    fclose(f);
    printf("CENSUS-SUMMARY seeds %d radius %d with_village %d (within 256: %d) mean_nearest %.0f with_priest %d "
           "(within 256: %d) mean_nearest_priest %.0f nearest_priest <128 %d <256 %d <384 %d <%d %d none %d\n",
           seeds, radius, with, with256, with ? sum_near / with : 0.0, with_priest, priest256,
           with_priest ? sum_near_priest / with_priest : 0.0, nb[0], nb[1], nb[2], radius, nb[3], nb[4]);
    if (n_econ)
        printf("CENSUS-ECON priest_villages %d mean_reach12 %.4f mean_eyes %.2f (a priest village's ladder, %d draws "
               "each, tools %d)\n",
               n_econ, sum_reach / n_econ, sum_eyes / n_econ, census_econ_runs, census_tools);
    return 0;
}

/* ---- ore ---- */

#define ORE_TUNNEL_Y 12 /* NETH_VEC_STRIP_Y: the strip's feet */
#define ORE_COAL 16
#define ORE_EMERALD 129
#define ORE_IRON 15
#define ORE_DIAMOND 56

/* The seed world the server holds before its first tick (seedworld.c: 625 chunks around the spawn, the populated ones
 * fully decorated), its populated chunks' ore: in the strip's tunnel (feet and head at ORE_TUNNEL_Y, every x-row and
 * z-row: the cells a 1x2 tunnel digs, either direction), the tunnel's walls (the four side blocks, the floor and the
 * ceiling a cell: what its crosshair can see, for a seek that digs them), and the extreme hills' share of the columns
 * (the only biome with emerald ore). One ORE line a seed. */
static int ore(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    long long seed;
    double sx, sz;
    while (fscanf(f, "%lld %lf %lf", &seed, &sx, &sz) == 3)
    {
        static struct seedworld sw;
        seedworld_init(&sw, (int64_t)seed);
        seedworld_build(&sw);
        struct world *w = &sw.p.world;
        long cells = 0, t_coal = 0, t_em = 0, t_iron = 0, t_dia = 0, w_coal = 0, w_em = 0, w_iron = 0, w_dia = 0;
        long cols = 0, hills = 0, stone_cells = 0;
        int cx0 = (int)(sw.spawn_x >> 4), cz0 = (int)(sw.spawn_z >> 4);
        for (int cx = cx0 - 12; cx <= cx0 + 12; ++cx)
            for (int cz = cz0 - 12; cz <= cz0 + 12; ++cz)
            {
                struct chunk *c = world_chunk_near(w, cx, cz);
                if (!c) c = world_chunk(w, cx, cz);
                if (!c || !c->terrain_populated) continue;
                for (int i = 0; i < 256; ++i)
                {
                    ++cols;
                    int b = c->biome[i];
                    if (b == 3 || b == 20 || b == 34 || b == 131 || b == 162) ++hills; /* extreme hills, edge, plus, M */
                }
                for (int lx = 0; lx < 16; ++lx)
                    for (int lz = 0; lz < 16; ++lz)
                    {
                        int x = cx * 16 + lx, z = cz * 16 + lz, y = ORE_TUNNEL_Y;
                        ++cells;
                        int a = world_get_block(w, x, y, z), h = world_get_block(w, x, y + 1, z);
                        if (a == 1 || h == 1) ++stone_cells;
                        t_coal += (a == ORE_COAL) + (h == ORE_COAL);
                        t_em += (a == ORE_EMERALD) + (h == ORE_EMERALD);
                        t_iron += (a == ORE_IRON) + (h == ORE_IRON);
                        t_dia += (a == ORE_DIAMOND) + (h == ORE_DIAMOND);
                        /* a tunnel along x sees z +-1 at both heights, the floor and the ceiling (a z tunnel the x
                         * ones: the same counts in expectation; the x one is counted) */
                        int wb[6] = {world_get_block(w, x, y, z - 1), world_get_block(w, x, y + 1, z - 1),
                                     world_get_block(w, x, y, z + 1), world_get_block(w, x, y + 1, z + 1),
                                     world_get_block(w, x, y - 1, z), world_get_block(w, x, y + 2, z)};
                        for (int k = 0; k < 6; ++k)
                        {
                            w_coal += wb[k] == ORE_COAL;
                            w_em += wb[k] == ORE_EMERALD;
                            w_iron += wb[k] == ORE_IRON;
                            w_dia += wb[k] == ORE_DIAMOND;
                        }
                    }
            }
        printf("ORE seed %lld populated_cells %ld stone_cells %ld extreme_hills %.3f per_1000_cells tunnel coal %.2f "
               "emerald %.3f iron %.2f diamond %.3f walls coal %.2f emerald %.3f iron %.2f diamond %.3f\n",
               seed, cells, stone_cells, cols ? (double)hills / cols : 0.0, 1000.0 * t_coal / cells,
               1000.0 * t_em / cells, 1000.0 * t_iron / cells, 1000.0 * t_dia / cells, 1000.0 * w_coal / cells,
               1000.0 * w_em / cells, 1000.0 * w_iron / cells, 1000.0 * w_dia / cells);
        fflush(stdout);
        seedworld_free(&sw);
    }
    fclose(f);
    return 0;
}

/* ---- econ ---- */

#define ECON_EYE 381
#define ECON_EMERALD 388
#define ECON_UNLOCK_MAX_PRICE 64 /* emeralds the player will pay for an unlock offer */
#define ECON_MAX_TRADES 20000    /* a run's trade cap (a stall stops it sooner) */

/* What a player can bring for each bought good, in units of the input it is made from: coal and the raw goods are
 * themselves; paper is sugar cane (3 make 3); a book is 3 paper and a leather (counted as 3 cane + 1 leather). The
 * route column names which input pays; 'x' is a good this route does not make (diamonds, gold, a written book,
 * fish, wool, the raw meats are kills, counted apart). */
struct good { short item; const char *name; };
static const struct good GOODS[] = {
    {263, "coal"}, {296, "wheat"}, {339, "paper"}, {340, "book"}, {265, "iron"}, {266, "gold"}, {264, "diamond"},
    {35, "wool"}, {365, "chicken"}, {350, "fish"}, {319, "porkchop"}, {363, "beef"}, {387, "written_book"},
};
#define NGOODS (int)(sizeof GOODS / sizeof GOODS[0])

static int good_index(int item)
{
    for (int i = 0; i < NGOODS; ++i)
        if (GOODS[i].item == item) return i;
    return -1;
}

struct vstate {
    int prof;
    jrand rand;
    struct trade_list list;
    int pending; /* the unlock's 40 ticks: applied before the next trade with this villager */
};

/* the unlock as updateAITick does it once its 40 ticks pass with nobody trading */
static void vs_settle(struct vstate *v, jrand *shuf)
{
    if (!v->pending) return;
    trades_unlock_boost(&v->rand, &v->list);
    trades_add_default(&v->rand, shuf, v->prof, 1, &v->list);
    v->pending = 0;
}

/* useRecipe's unlock test: the recipe has the last recipe's ids */
static void vs_use(struct vstate *v, int i)
{
    struct trade_recipe *r = &v->list.r[i];
    ++r->uses;
    if (trades_same_ids(r, &v->list.r[v->list.n - 1])) v->pending = 1;
}

static int is_buy(const struct trade_recipe *r) /* the villager buys a good for one emerald */
{
    return !r->has_buy_b && r->sell.item == ECON_EMERALD && good_index(r->buy.item) >= 0;
}

static int is_emerald_only(const struct trade_recipe *r)
{
    return r->buy.item == ECON_EMERALD && !r->has_buy_b;
}

/* the priest's enchanted tools (its OP_TOOLS rows): the tool the offer takes, and what crafting it costs in iron
 * ingots (diamond 0) or diamonds (diamond 1) */
static const short TOOL_ITEM[8] = {267, 276, 307, 311, 258, 279, 257, 278};
static const char TOOL_MAT[8] = {2, 2, 8, 8, 3, 3, 3, 3};
static const char TOOL_DIA[8] = {0, 1, 0, 1, 0, 1, 0, 1};

static int tool_cost(int item, int diamond)
{
    for (int i = 0; i < 8; ++i)
        if (TOOL_ITEM[i] == item) return TOOL_DIA[i] == diamond ? TOOL_MAT[i] : 0;
    return 0;
}

/* a profession whose table (trades.c PROF_OPS) buys a good the route brings: the only villagers worth an unlock */
static int prof_useful(int prof, const char *allow)
{
    static const short BUYS[5][4] = {{296, 35, 365, 350}, {339, 340, 387, 0}, {0}, {263, 265, 266, 264},
                                     {263, 319, 363, 0}};
    for (int k = 0; k < 4; ++k)
    {
        int g = BUYS[prof][k] ? good_index(BUYS[prof][k]) : -1;
        if (g >= 0 && allow[g]) return 1;
    }
    return 0;
}

/* --tools 0: no tool given; 1: the iron ones; 2: iron and diamond */
static int tool_ok(int item, int tools)
{
    for (int i = 0; i < 8; ++i)
        if (TOOL_ITEM[i] == item) return tools >= 1 + TOOL_DIA[i];
    return 0;
}

static int econ(int runs, uint64_t seed, int eyes_want, const int *profs, int nprof, int tools, const char *allow)
{
    int reached = 0;
    double sum_goods[NGOODS] = {0}, sum_goods_ok[NGOODS] = {0}, sum_em = 0, sum_em_eyes = 0, sum_em_unlock = 0,
           sum_unlocks = 0, sum_unlocks_ok = 0, sum_tools = 0, sum_eyes = 0, sum_input_ok = 0;
    double sum_priest_unlocks = 0, sum_iron = 0, sum_dia = 0, sum_spent_ok = 0, sum_iron_ok = 0;
    int first_has_eye = 0, priests = 0, stall_money = 0, stall_priest = 0;
    int stall_hist[13] = {0};
    for (int run = 0; run < runs; ++run)
    {
        jrand shuf;
        jr_seed(&shuf, (int64_t)(seed * 1000003ULL + (uint64_t)run * 7919ULL));
        struct vstate v[ECON_MAXV];
        for (int i = 0; i < nprof; ++i)
        {
            memset(&v[i], 0, sizeof v[i]);
            v[i].prof = profs[i];
            jr_seed(&v[i].rand, (int64_t)(seed * 0x9E3779B97F4A7C15ULL + (uint64_t)run * 131ULL + (uint64_t)i));
            /* the villager has lived some ticks before the player comes: its Random is somewhere; draw a few */
            int skip = jr_int_n(&shuf, 64);
            for (int k = 0; k < skip; ++k) (void)jr_int(&v[i].rand);
            trades_add_default(&v[i].rand, &shuf, v[i].prof, 1, &v[i].list); /* getRecipes on the first window */
            if (v[i].prof == 2)
            {
                ++priests;
                if (v[i].list.r[0].sell.item == ECON_EYE) ++first_has_eye;
            }
        }
        long em = econ_start_emeralds, em_earned = 0, em_eyes = 0, em_unlock = 0, unlocks = 0, tools_paid = 0,
             priest_unlocks = 0;
        long tool_iron = 0, tool_dia = 0;
        long goods[NGOODS] = {0};
        int eyes = 0;
        for (int step = 0; step < ECON_MAX_TRADES && eyes < eyes_want; ++step)
        {
            for (int i = 0; i < nprof; ++i) vs_settle(&v[i], &shuf);
            /* the want: an eye offer's price, else the cheapest priest unlock's (its last recipe) */
            int ei = -1, ek = -1, ep = 1 << 30;
            for (int i = 0; i < nprof; ++i)
                for (int k = 0; k < v[i].list.n; ++k)
                {
                    struct trade_recipe *r = &v[i].list.r[k];
                    if (r->sell.item == ECON_EYE && is_emerald_only(r) && !trades_is_disabled(r) && r->buy.count < ep)
                    { ep = r->buy.count; ei = i; ek = k; }
                }
            int ui = -1, up = 1 << 30, utool = 0;
            if (ei < 0)
                for (int i = 0; i < nprof; ++i)
                {
                    if (v[i].prof != 2) continue;
                    struct trade_recipe *r = &v[i].list.r[v[i].list.n - 1];
                    if (trades_is_disabled(r)) continue;
                    int cost, tool = 0;
                    if (is_emerald_only(r)) cost = r->buy.count;
                    else if (r->has_buy_b && r->buy_b.item == ECON_EMERALD && tool_ok(r->buy.item, tools))
                    { cost = r->buy_b.count; tool = r->buy.item; }
                    else continue;
                    if (cost < up && cost <= ECON_UNLOCK_MAX_PRICE) { up = cost; ui = i; utool = tool; }
                }
            int need = ei >= 0 ? ep : ui >= 0 ? up : -1;
            if (need < 0)
            {
                /* a priest whose first draw found nothing buys gold (addDefaultEquipmentAndRecipies' fallback): its
                 * unlock is a sale */
                int si = -1;
                for (int i = 0; i < nprof && si < 0; ++i)
                {
                    struct trade_recipe *r = &v[i].list.r[v[i].list.n - 1];
                    if (v[i].prof == 2 && is_buy(r) && !trades_is_disabled(r) && allow[good_index(r->buy.item)]) si = i;
                }
                if (si >= 0)
                {
                    struct trade_recipe *r = &v[si].list.r[v[si].list.n - 1];
                    goods[good_index(r->buy.item)] += r->buy.count;
                    ++em;
                    ++em_earned;
                    ++unlocks;
                    ++priest_unlocks;
                    vs_use(&v[si], v[si].list.n - 1);
                    continue;
                }
                ++stall_priest; /* no eye offer and every priest's last offer out of reach: the ladder ends */
                break;
            }
            if (em >= need)
            {
                if (ei >= 0)
                {
                    em -= ep;
                    em_eyes += ep;
                    ++eyes;
                    vs_use(&v[ei], ek);
                }
                else
                {
                    em -= up;
                    em_unlock += up;
                    ++unlocks;
                    ++priest_unlocks;
                    if (utool) { ++tools_paid; tool_iron += tool_cost(utool, 0); tool_dia += tool_cost(utool, 1); }
                    vs_use(&v[ui], v[ui].list.n - 1);
                }
                continue;
            }
            /* short of emeralds (or no priest offer moves on): sell a good the route makes */
            int done = 0;
            for (int i = 0; i < nprof && !done; ++i)
                for (int k = 0; k < v[i].list.n && !done; ++k)
                {
                    struct trade_recipe *r = &v[i].list.r[k];
                    if (!is_buy(r) || trades_is_disabled(r)) continue;
                    int g = good_index(r->buy.item);
                    if (!allow[g]) continue;
                    goods[g] += r->buy.count;
                    ++em;
                    ++em_earned;
                    vs_use(&v[i], k);
                    done = 1;
                }
            if (done) continue;
            /* nothing to sell: unlock another villager's last offer for more buy offers (the cheapest payable) */
            int bi = -1, bc = 1 << 30;
            for (int i = 0; i < nprof; ++i)
            {
                if (v[i].prof == 2 || !prof_useful(v[i].prof, allow)) continue;
                struct trade_recipe *r = &v[i].list.r[v[i].list.n - 1];
                if (trades_is_disabled(r)) continue;
                int cost;
                if (is_emerald_only(r)) cost = r->buy.count;
                else if (r->has_buy_b && r->buy_b.item == ECON_EMERALD && r->buy.item == 13) cost = r->buy_b.count;
                else if (r->has_buy_b && r->buy_b.item == ECON_EMERALD && r->buy.item == 340 && allow[3])
                    cost = r->buy_b.count; /* the librarian's book and emeralds for an enchanted book */
                else continue;
                if (cost < bc && cost <= ECON_UNLOCK_MAX_PRICE) { bc = cost; bi = i; }
            }
            if (bi < 0 || em < bc)
            {
                ++stall_money;
                break; /* stalled: short of emeralds, no sale and no unlock within reach */
            }
            struct trade_recipe *r = &v[bi].list.r[v[bi].list.n - 1];
            em -= bc;
            em_unlock += bc;
            if (r->buy.item == 340) ++goods[3];
            ++unlocks;
            vs_use(&v[bi], v[bi].list.n - 1);
        }
        sum_em += em_earned;
        sum_priest_unlocks += priest_unlocks;
        sum_iron += tool_iron;
        sum_dia += tool_dia;
        sum_em_eyes += em_eyes;
        sum_em_unlock += em_unlock;
        sum_unlocks += unlocks;
        sum_tools += tools_paid;
        sum_eyes += eyes;
        for (int g = 0; g < NGOODS; ++g) sum_goods[g] += goods[g];
        stall_hist[eyes > 12 ? 12 : eyes]++;
        if (eyes >= eyes_want)
        {
            ++reached;
            sum_unlocks_ok += unlocks;
            sum_spent_ok += em_eyes + em_unlock;
            sum_iron_ok += tool_iron;
            long in = 0;
            for (int g = 0; g < NGOODS; ++g) { sum_goods_ok[g] += goods[g]; in += goods[g]; }
            sum_input_ok += in;
        }
    }
    econ_last.reach = (double)reached / runs;
    econ_last.eyes = sum_eyes / runs;
    econ_last.em = sum_em / runs;
    econ_last.eye_ge7 = 0;
    for (int k = 7; k <= 12; ++k) econ_last.eye_ge7 += stall_hist[k];
    econ_last.eye_ge7 /= runs;
    if (econ_quiet) return 0;
    printf("ECON runs %d villagers %d priests_first_offer_eye %d/%d reached %d/%d eyes_mean %.2f emeralds_earned %.1f "
           "on_eyes %.1f on_unlocks %.1f unlocks %.1f tools_paid %.2f\n",
           runs, nprof, first_has_eye, priests, reached, runs, sum_eyes / runs, sum_em / runs, sum_em_eyes / runs,
           sum_em_unlock / runs, sum_unlocks / runs, sum_tools / runs);
    printf("ECON-MORE priest_unlocks %.2f tool_iron %.2f tool_diamonds %.2f stalled: priest_ladder_ended %d "
           "short_of_emeralds %d\n",
           sum_priest_unlocks / runs, sum_iron / runs, sum_dia / runs, stall_priest, stall_money);
    printf("ECON-GOODS (mean a run that reached %d eyes)", eyes_want);
    for (int g = 0; g < NGOODS; ++g)
        if (sum_goods_ok[g] > 0) printf(" %s %.1f", GOODS[g].name, reached ? sum_goods_ok[g] / reached : 0.0);
    printf(" total %.1f unlocks %.1f emeralds_spent %.1f tool_iron %.1f\n", reached ? sum_input_ok / reached : 0.0,
           reached ? sum_unlocks_ok / reached : 0.0, reached ? sum_spent_ok / reached : 0.0,
           reached ? sum_iron_ok / reached : 0.0);
    printf("ECON-EYES (runs ending with k eyes, k = 0..12+):");
    for (int k = 0; k <= 12; ++k) printf(" %d", stall_hist[k]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *spawns = NULL;
    int radius = CENSUS_RADIUS, verbose = 0, mode = 0, econ_mode = 0, runs = 1000, eyes = 12, tools = 0;
    uint64_t seed = 1;
    int profs[ECON_MAXV] = {0, 1, 2, 3, 4}, nprof = 5;
    char allow[NGOODS];
    memset(allow, 0, sizeof allow);
    allow[0] = 1; /* coal */
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--census") && i + 1 < argc) { mode = 1; spawns = argv[++i]; }
        else if (!strcmp(argv[i], "--ore") && i + 1 < argc) { mode = 3; spawns = argv[++i]; }
        else if (!strcmp(argv[i], "--econ")) econ_mode = 1;
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--eyes") && i + 1 < argc) eyes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tools") && i + 1 < argc) tools = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--emeralds") && i + 1 < argc) econ_start_emeralds = atol(argv[++i]);
        else if (!strcmp(argv[i], "--goods") && i + 1 < argc)
        {
            memset(allow, 0, sizeof allow);
            char *s = strdup(argv[++i]);
            for (char *t = strtok(s, ","); t; t = strtok(NULL, ","))
            {
                int found = 0;
                for (int g = 0; g < NGOODS; ++g)
                    if (!strcmp(GOODS[g].name, t)) { allow[g] = 1; found = 1; }
                if (!found) { fprintf(stderr, "unknown good %s\n", t); return 2; }
            }
            free(s);
        }
        else if (!strcmp(argv[i], "--profs") && i + 1 < argc)
        {
            nprof = 0;
            char *s = strdup(argv[++i]);
            for (char *t = strtok(s, ","); t && nprof < ECON_MAXV; t = strtok(NULL, ",")) profs[nprof++] = atoi(t);
            free(s);
        }
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else
        {
            fprintf(stderr, "usage: trade_census --census SPAWNS [--radius R] [-v] [--econ ...] | --ore SPAWNS | --econ "
                            "[--runs N] [--seed S] [--eyes E] [--profs P,...] [--goods coal,wheat,...] [--tools 0|1|2] "
                            "[--emeralds N]\n");
            return 2;
        }
    }
    if (mode == 1)
    {
        census_econ_runs = econ_mode ? runs : 0;
        census_eyes = eyes;
        census_tools = tools;
        census_allow = allow;
        return census(spawns, radius, verbose);
    }
    if (mode == 3) return ore(spawns);
    if (econ_mode) return econ(runs, seed, eyes, profs, nprof, tools, allow);
    fprintf(stderr, "trade_census: --census or --econ\n");
    return 2;
}
