/* obsidian_case (lane/obsidian2): NETH_VEC_OBSIDIAN's case, no Python. N envs from a start (the config's
 * pipeline.start: configs/s64portal-kit.yaml's obs-g5-3600, the kit held; or --start DIR), one episode of S steps each
 * under an idle policy (the centre bins, no key) or vec_bench's random one (--policy 1, its xorshift seeded by --seed:
 * the sampler seed), the macros --macros (default recipe,strip,food,obsidian: 163839; 32767 the same without the bit).
 * At the end a line an env (the books of vecenv.h neth_vec_obsidian: the kit, obsidian held and mined, the Nether's
 * step, the jobs, the deaths with the first one's step, place, cause and who drove the step before it) and the totals.
 *
 *   out/runtime/obsidian_case [--config configs/s64portal-kit.yaml] [--start DIR] [--ns 32] [--groups 2] [--threads T]
 *                             [--steps 24000] [--seed K] [--policy 0|1] [--macros BITS] [--quiet]
 *
 * The last line is "obsidian_case: N envs, ... nether K, ten T, died D (kit deaths KD)" for a gate's grep. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vecenv.h"

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

/* vec_bench's draw (nether_case's): 0 idle, 1 its random policy */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

int main(int argc, char **argv)
{
    const char *config = "configs/s64portal-kit.yaml", *start = NULL;
    int n = 32, groups = 2, threads = 0, steps = 24000, policy = 0, quiet = 0, macros = 163839, dev = 0;
    int trace = -1, trace_every = 100;
    uint64_t seed = 1;
    char mopts[1024] = "";
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--dev")) dev = 1;    /* lane/obsidian3: a dev start (rlgui.h NETH_RL_DEV) */
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = atoi(argv[++i]);   /* lane/obsidian4: env E's */
        else if (!strcmp(argv[i], "--trace-every") && i + 1 < argc) trace_every = atoi(argv[++i]);   /* line every N */
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            /* lane/macroopts: a macro option, NAME=WORD (neth_vec_opts.macro_opts; sh_check --opts lists them) */
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: obsidian_case [--config F] [--start DIR] [--ns N] [--groups G] [--threads T] "
                            "[--opt NAME=WORD]... "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--quiet]\n");
            return 2;
        }
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = steps + 10;              /* one episode: no reset inside the window */
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? 1 : 0;              /* rlgui.h NETH_RL_DEV */
    o.macro_opts = mopts;               /* lane/macroopts */
    char err[1024];
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL)
    {
        printf("FAIL: %s\n", err);
        return 1;
    }
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(v, &b);
    int aw = neth_vec_action_width(v), gs = n / groups;
    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    int *gcount = calloc((size_t)groups, sizeof *gcount), live = groups;
    struct neth_vec_obsidian *ob = calloc((size_t)n, sizeof *ob);
    struct neth_vec_bportal *bp = calloc((size_t)n, sizeof *bp);   /* lane/obsidian3 */
    /* lane/obsidian4: deaths by cause and who, and a line each */
    int dtab[7][16] = {{0}}, nlog = 0;
    struct { int env, step, cause, who; float x, y, z; } dlog[256];
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            int before = ob[i].deaths, bc[7];
            memcpy(bc, ob[i].cause, sizeof bc);
            neth_vec_obsidian(v, i, &ob[i]), neth_vec_bportal(v, i, &bp[i]);
            if (i == trace && gcount[g] % trace_every == 0)
                printf("trace %d step %d at %.1f %.1f %.1f who %d mode %d step %d search %lld mem %d/%d\n", i, gcount[g],
                       ob[i].pos[0], ob[i].pos[1], ob[i].pos[2], ob[i].who_last, bp[i].mode, bp[i].step,
                       (long long)bp[i].search_steps, bp[i].nmem_water, bp[i].nmem_lava);
            /* lane/obsidian4: every death by cause and by who drove (a burn: who began the lava incident) */
            for (int k = 0; k < 7 && ob[i].deaths > before; ++k)
                if (ob[i].cause[k] > bc[k])
                {
                    /* a drowning (the bit on): who walked into the water, not the surface macro swimming at the end */
                    int w = k <= 1 ? ob[i].who_hurt
                            : k == 4 && (macros & NETH_VEC_BPORTAL) ? bp[i].wet_who
                                                                     : ob[i].who_last;
                    w = w < 0 || w >= 16 ? 14 : w;
                    dtab[k][w] += 1;
                    if (nlog < 256)
                        dlog[nlog].env = i, dlog[nlog].step = gcount[g], dlog[nlog].cause = k, dlog[nlog].who = w,
                        dlog[nlog].x = (float)ob[i].pos[0], dlog[nlog].y = (float)ob[i].pos[1],
                        dlog[nlog].z = (float)ob[i].pos[2], ++nlog;
                }
        }
        if (++gcount[g] > steps)
        {
            --live;
            continue;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            draw(&rng[i], b.actions + (size_t)i * aw, policy);
            if (aw > NETH_VEC_ACTION) b.actions[(size_t)i * aw + NETH_VEC_ACTION] = 0;
        }
        if (neth_vec_send(v, g))
        {
            printf("FAIL: send %d refused\n", g);
            return 1;
        }
    }
    double wall = now_s() - t0;
    static const char *const CAUSE[7] = {"lava", "fire", "fall", "mob", "drown", "starve", "other"};
    enum { NW = 16, NJ = 7 };           /* mods/macros/obsidian.h OW_KINDS; the jobs are 1..NJ */
    static const char *const WHO[NW] = {"policy", "mine", "fill", "cast", "douse", "climb", "go", "look",
                                        "search", "strip", "escape", "seek", "surface", "craft", "other", "bportal"};
    /* lane/obsidian3: the bucket portal's books, a line an env that started it, and the totals */
    static const char *const BF[8] = {"site", "dig", "check", "aim", "pour", "trip", "pos", "light"};
    int bstart = 0, bsite = 0, bcast = 0, blit = 0, bl = 0, bw = 0, bfail[8] = {0}, bbest[11] = {0};
    int bpools = 0, bfills = 0, bfound = 0, bfound_env = 0, bpool2 = 0, bback = 0, lava_seen = 0;
    int64_t bsearch = 0, bwhy[7] = {0};
    int bclimb = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_bportal *q = &bp[i];
        bstart += q->starts > 0, bsite += q->sites, bcast += q->casts, blit += q->lit > 0;
        bl += q->trips_lava, bw += q->trips_water;
        bbest[q->best_casts > 10 ? 10 : q->best_casts < 0 ? 0 : q->best_casts] += 1;
        for (int k = 0; k < 8; ++k) bfail[k] += q->fail_kind[k];
        if (!quiet && (q->starts > 0 || (macros & NETH_VEC_BPORTAL)))
        {
            printf("bp %2d: starts %d sites %d aborts %d casts %d (best %d) lit %d at %lld trips lava %d water %d uses %d "
                   "steps %lld now step %d mode %d mem %d/%d fails",
                   i, q->starts, q->sites, q->aborts, q->casts, q->best_casts, q->lit, (long long)q->lit_step,
                   q->trips_lava, q->trips_water, q->uses, (long long)q->steps_on, q->step, q->mode, q->nmem_water,
                   q->nmem_lava);
            for (int k = 0; k < 8; ++k)
                if (q->fail_kind[k]) printf(" %s %d", BF[k], q->fail_kind[k]);
            printf("; pools %d fills %d search %lld found %d back %d lost %d\n", q->pools, q->fills_lava,
                   (long long)q->search_steps, q->search_found, q->back_walks, q->site_lost);
        }
        bpools += q->pools, bfills += q->fills_lava, bfound += q->search_found, bsearch += q->search_steps;
        bfound_env += q->search_found > 0, bpool2 += q->pools >= 2, bback += q->back_walks;
        for (int k = 0; k < 7; ++k) bwhy[k] += q->site_why[k];
        bclimb += q->climbs;
    }
    printf("bportal: envs started %d, sites %d, cells cast %d, lit %d, trips lava %d water %d; best cells a site:", bstart,
           bsite, bcast, blit, bl, bw);
    for (int k = 0; k <= 10; ++k) printf(" %d", bbest[k]);
    printf("; sites given up:");
    for (int k = 0; k < 8; ++k) printf(" %s %d", BF[k], bfail[k]);
    printf("\n");
    for (int i = 0; i < n; ++i) lava_seen += bp[i].nmem_lava > 0;
    printf("bportal lava: envs that saw lava %d, lava fills %d, pools filled at %d (envs with 2 or more %d), walks back "
           "%d, search steps %lld, lava the search found %d (in %d envs)\n",
           lava_seen, bfills, bpools, bpool2, bback, (long long)bsearch, bfound, bfound_env);
    static const char *const BS[7] = {"ok", "given-up", "ground", "feet", "plane", "lava", "water"};
    printf("bportal site tries by why refused:");
    for (int k = 0; k < 7; ++k) printf(" %s %lld", BS[k], (long long)bwhy[k]);
    printf("; climbs %d\n", bclimb);
    {
        /* lane/obsidian6: the one-bucket cast (bportal_buckets 1 or any) */
        int os = 0, oc = 0, od = 0, op = 0, oenv = 0;
        for (int i = 0; i < n; ++i)
            os += bp[i].one_sites, oc += bp[i].one_casts, od += bp[i].dumps, op += bp[i].parks,
                oenv += bp[i].one_casts > 0;
        printf("bportal one bucket: sites %d, cells cast %d (in %d envs), waters poured before a lava trip %d, lava "
               "buckets poured back %d\n", os, oc, oenv, op, od);
    }
    {
        /* lane/obsidian5: the walk over the surface and the climb to the sky */
        int64_t rs = 0, us = 0;
        int legs = 0, looks = 0, ups = 0, upd = 0, rl = 0, rl_env = 0, rw = 0, roamed = 0, pil = 0, pilt = 0, pill = 0;
        double fsum = 0, dsum = 0, ssum = 0, fmax = 0;
        for (int i = 0; i < n; ++i)
        {
            const struct neth_vec_bportal *q = &bp[i];
            rs += q->roam_steps, us += q->up_steps, legs += q->roam_legs, looks += q->roam_looks, ups += q->ups;
            upd += q->ups_done, rl += q->roam_lava, rw += q->roam_water;
            pil += q->pillars, pilt += q->pillar_tops, pill += q->pillar_lava;
            if (q->roam_steps > 0) roamed += 1, fsum += q->roam_far, fmax = q->roam_far > fmax ? q->roam_far : fmax;
            if (q->roam_lava_step >= 0) rl_env += 1, dsum += q->roam_lava_d, ssum += (double)q->roam_lava_step;
            if (!quiet && (q->roam_steps > 0 || q->ups > 0))
                printf("roam %2d: steps %lld up %lld legs %d looks %d ups %d/%d lava %d (first d %.0f step %lld) water "
                       "%d far %.0f\n", i, (long long)q->roam_steps, (long long)q->up_steps, q->roam_legs,
                       q->roam_looks, q->ups_done, q->ups, q->roam_lava, q->roam_lava_d, (long long)q->roam_lava_step,
                       q->roam_water, q->roam_far);
        }
        printf("bportal roam: envs that walked %d, walk steps %lld, climb steps %lld, legs %d, far looks %d, climbs to the "
               "sky %d of %d, lava found %d (in %d envs, first at %.0f blocks and step %.0f on average), water %d, "
               "farthest %.0f (mean %.0f); pillars %d, at the top %d, lava seen from one %d\n",
               roamed, (long long)rs, (long long)us, legs, looks, upd, ups, rl, rl_env, rl_env ? dsum / rl_env : -1.0,
               rl_env ? ssum / rl_env : -1.0, rw, fmax, roamed ? fsum / roamed : 0.0, pil, pilt, pill);
    }
    int nether = 0, ten = 0, dead = 0, deaths = 0, kitd = 0, cause[7] = {0}, who[NW] = {0}, kit = 0, mined = 0, anyob = 0;
    int64_t jobs[NW] = {0}, st[NW] = {0};
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_obsidian *q = &ob[i];
        nether += q->nether >= 0, ten += q->obs_max >= 10, kit += q->kit >= 0, mined += q->mined;
        anyob += q->mined > 0;
        dead += q->deaths > 0, deaths += q->deaths;
        if (q->deaths > 0) who[q->d_who] += 1, kitd += q->d_kit;
        for (int k = 0; k < 7; ++k) cause[k] += q->cause[k];
        for (int k = 0; k < NW; ++k) jobs[k] += q->jobs[k], st[k] += q->steps[k];
        if (!quiet)
        {
            printf("env %2d: dim %2d at %.0f %.0f %.0f kit %6lld nether %6lld obsidian %d (max %d, mined %d) jobs", i,
                   q->dim, q->pos[0], q->pos[1], q->pos[2], (long long)q->kit, (long long)q->nether, q->obsidian,
                   q->obs_max, q->mined);
            for (int k = 1; k <= NJ; ++k) printf(" %s %d", WHO[k], q->jobs[k]);
            printf("; deaths %d", q->deaths);
            if (q->deaths > 0)
                printf(" (first at %lld, %.1f %.1f %.1f, %s, %s, kit %d)", (long long)q->d_step, q->d_pos[0],
                       q->d_pos[1], q->d_pos[2], CAUSE[q->d_cause], WHO[q->d_who], q->d_kit);
            printf("\n");
        }
    }
    {
        /* lane/obsidian7: the water trip (obsidian_water sky) */
        int st = 0, sw = 0, se = 0, dr = 0, de = 0, sr = 0, rm = 0;
        for (int i = 0; i < n; ++i)
            st += ob[i].sky_trips, sw += ob[i].sky_water, se += ob[i].sky_trips > 0, dr += ob[i].drops, de += ob[i].drops > 0,
            sr += ob[i].steers, rm += ob[i].rims;
        printf("water trips %d (in %d envs), ended with water in view %d; drops %d (in %d envs); steers %d; rim casts %d\n",
               st, se, sw, dr, de, sr, rm);
    }
    {
        /* lane/obsid: the water carried: envs that ever held a water bucket, fills, the median first step, steps held */
        int ev = 0, fills = 0, sc = 0, scf = 0, sce = 0;
        int64_t held = 0, firsts[1024];
        for (int i = 0; i < n; ++i)
        {
            fills += ob[i].wb_fills, held += ob[i].wb_steps;
            sc += ob[i].scouts, scf += ob[i].scout_found, sce += ob[i].scout_found > 0;
            if (ob[i].wb_first >= 0 && ev < 1024) firsts[ev++] = ob[i].wb_first;
        }
        for (int i = 1; i < ev; ++i)
            for (int j = i; j > 0 && firsts[j - 1] > firsts[j]; --j)
            {
                int64_t t = firsts[j];
                firsts[j] = firsts[j - 1], firsts[j - 1] = t;
            }
        printf("water carried: envs that held a water bucket %d, fills %d, first held at median step %lld, steps held %lld\n",
               ev, fills, ev ? (long long)firsts[ev / 2] : -1LL, (long long)held);
        printf("scouts (obsidian_water scout): %d begun, water seen %d (in %d envs)\n", sc, scf, sce);
    /* The same start can stall before the obsidian kit: three diamonds without wood for the pickaxe. */
    for (int i = 0; i < n; ++i)
    {
        struct neth_vec_strip d;
        if (neth_vec_strip(v, i, &d) != 0) continue;
        printf("strip env %d: diamond %d planks %d sticks %d logs %d table %d tier %d, wood errand %d surface %d "
               "logs_got %d pick_made %d, diamond cells %lld no_pick_steps %lld, at %.1f %.1f %.1f\n",
               i, d.held[0], d.held[1], d.held[2], d.held[3], d.table, d.held[6], d.errand[0],
               d.errand[1], d.errand[2], d.errand[3], (long long)d.dia[8], (long long)d.dia[3],
               d.pos[0], d.pos[1], d.pos[2]);
    }
        /* lane/obsid: the link's funnel: envs with the kit, then each step reached after it */
        static const char *const FN[8] = {"sky", "water", "fill", "lava", "cast", "obs1", "obs10", "nether"};
        int kit = 0, f[8] = {0};
        for (int i = 0; i < n; ++i)
        {
            kit += ob[i].kit >= 0;
            for (int k = 0; k < 8; ++k) f[k] += ob[i].fn[k] >= 0;
        }
        printf("funnel: kit %d", kit);
        for (int k = 0; k < 8; ++k) printf(", %s %d", FN[k], f[k]);
        printf("\n");
    }
    printf("jobs:");
    for (int k = 1; k <= NJ; ++k) printf(" %s %lld", WHO[k], (long long)jobs[k]);
    printf("\nsteps with the kit by who:");
    for (int k = 0; k < NW; ++k) printf(" %s %lld", WHO[k], (long long)st[k]);
    printf("\ndeaths %d in %d envs, by cause:", deaths, dead);
    for (int k = 0; k < 7; ++k) printf(" %s %d", CAUSE[k], cause[k]);
    printf("; first deaths by who drove:");
    for (int k = 0; k < NW; ++k)
        if (who[k]) printf(" %s %d", WHO[k], who[k]);
    /* lane/obsidian4: every death, cause by who drove the step (a burn: who began the incident) */
    for (int c = 0; c < 7; ++c)
    {
        int t = 0;
        for (int w = 0; w < NW; ++w) t += dtab[c][w];
        if (!t) continue;
        printf("\ndeaths %s %d by who:", CAUSE[c], t);
        for (int w = 0; w < NW; ++w)
            if (dtab[c][w]) printf(" %s %d", WHO[w], dtab[c][w]);
    }
    for (int j = 0; j < nlog && !quiet; ++j)
        printf("\ndeath env %d step %d %s %s at %.1f %.1f %.1f", dlog[j].env, dlog[j].step, CAUSE[dlog[j].cause],
               WHO[dlog[j].who], dlog[j].x, dlog[j].y, dlog[j].z);
    printf("\nobsidian_case: %d envs, %d steps, seed %llu, policy %d, macros %d, %.1f s: kit %d, mined %d (in %d envs), "
           "ten %d, lit %d, nether %d, died %d (kit deaths %d); macro options %s\n",
           n, steps, (unsigned long long)seed, policy, macros, wall, kit, mined, anyob, ten, blit, nether, dead, kitd,
           mopts[0] ? mopts : "(defaults)");
    neth_vec_free(v);
    return 0;
}
