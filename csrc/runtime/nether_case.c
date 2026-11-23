/* nether_case (lane/nether): NETH_VEC_NETHER's case, no Python. N envs from a Nether start (the config's
 * pipeline.start, or --start DIR: the seed-42 golden chain's nether checkpoint gold-g7-s42, the player just through
 * the portal), one episode of S steps each under an idle policy (the centre bins, no key) or the random one vec_bench
 * draws (--policy 1, its xorshift seeded by --seed: the sampler seed), the macros --macros. At the end, a line an env
 * and the totals: the fortress found, the spawner found, rods held (now and at most), home (the overworld again),
 * deaths and what hurt last before each, and the median step of each event over the envs that reached it.
 *
 *   out/runtime/nether_case [--config configs/train-fast.yaml] [--start DIR] [--ns 32] [--groups 2] [--threads T]
 *                           [--steps 24000] [--seed K] [--policy 0|1] [--macros BITS] [--quiet]
 *                           [--trace I [--every K]]    (lane/nether2: env I's books every K steps)
 *                           [--dump R [--dump-at T] [--dump-env E]]   (a developer's map of env E's client world at
 *                                                      step T, radius R: the fortress, the feet's level and the
 *                                                      level under it, S soul sand, C cobblestone; lane/nether3)
 *                           [--dump-levels LO HI]      (with --dump R: each level LO..HI's map, half side R/2, l lava
 *                                                      (block 10, moving), L lava (11, still), * the spawner)
 *
 * The last line is "nether_case: N envs, ... home H" with the counts, for a gate's grep. */
#define _GNU_SOURCE
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rlgui.h"
#include "vecenv.h"
#include "netherview.h"

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

/* vec_bench's draw: 0 idle, 1 its random policy */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

static int cmp64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

/* the median of the non-negative values (-1 none) and how many */
static int64_t median(const int64_t *v, int n, int *k)
{
    int64_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof *w);
    int m = 0;
    for (int i = 0; i < n; ++i)
        if (v[i] >= 0) w[m++] = v[i];
    qsort(w, (size_t)m, sizeof *w, cmp64);
    int64_t r = m ? (m % 2 ? w[m / 2] : (w[m / 2 - 1] + w[m / 2]) / 2) : -1;
    free(w);
    *k = m;
    return r;
}

int main(int argc, char **argv)
{
    const char *config = "configs/train-fast.yaml", *start = NULL;
    int dev = 0;
    int xportal = 0, xp[3] = {0, 0, 0};     /* lane/rod: --portal X,Y,Z */
    int n = 32, groups = 2, threads = 0, steps = 24000, policy = 0, quiet = 0, trace = -1, every = 100, dump = 0,
        dump_at = 1, de = 0, dlo = 0, dhi = 0, items = -1, items_until = -1, last_bstat = 0;
    int macros = NETH_VEC_ALL | NETH_VEC_TABLE2 | NETH_VEC_IRON | NETH_VEC_LATE | NETH_VEC_LIQUID | NETH_VEC_PORTAL |
                 NETH_VEC_END | NETH_VEC_FIGHT | NETH_VEC_CARRY | NETH_VEC_SEEK | NETH_VEC_STRIP | NETH_VEC_FOOD |
                 NETH_VEC_NETHER;
    uint64_t seed = 1;
    char mopts[1024] = "";              /* lane/macroopts: --opt NAME=WORD, the macro options */
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
        else if (!strcmp(argv[i], "--dev")) dev = 1;    /* lane/rod: a dev start (rlgui.h NETH_RL_DEV) */
        else if (!strcmp(argv[i], "--portal") && i + 1 < argc)
            xportal = sscanf(argv[++i], "%d,%d,%d", &xp[0], &xp[1], &xp[2]) == 3;
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--items") && i + 1 < argc) items = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) dump_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-env") && i + 1 < argc) de = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-levels") && i + 2 < argc)
        {
            dlo = atoi(argv[++i]);
            dhi = atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: nether_case [--config F] [--start DIR] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--quiet] [--trace I] [--every K] "
                            "[--items I]\n");
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
    o.macro_opts = mopts;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? 1 : 0;              /* rlgui.h NETH_RL_DEV */
    char err[1024];
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v != NULL && xportal)
        for (int i = 0; i < n; ++i) neth_vec_nether_portal(v, i, xp[0], xp[1], xp[2]);
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
    struct neth_vec_nether *nb = calloc((size_t)n, sizeof *nb);
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i) neth_vec_nether(v, i, &nb[i]);
        if (items >= g * gs && items < (g + 1) * gs)
        {
            int tick = gcount[g];
            const struct neth_vec_nether *q = &nb[items];
            if (q->bstat > last_bstat)
            {
                printf("blaze-kill tick %d stats %d->%d player %.2f %.2f %.2f spawner %d %d %d\n", tick,
                       last_bstat, q->bstat, q->pos[0], q->pos[1], q->pos[2], q->sp[1], q->sp[2], q->sp[3]);
                items_until = tick + 200;
            }
            last_bstat = q->bstat;
            if (tick <= items_until)
            {
                struct netherview_item drops[16];
                int nd = neth_vec_nether_items(v, items, drops, 16);
                printf("blaze-rod-items tick %d player %.2f %.2f %.2f free %d n %d", tick, q->pos[0], q->pos[1],
                       q->pos[2], q->free_slots, nd);
                for (int k = 0; k < nd; ++k)
                {
                    int x = (int)floor(drops[k].x), y = (int)floor(drops[k].y), z = (int)floor(drops[k].z);
                    printf(" id %d age %d count %d at %.2f %.2f %.2f block %d below %d", drops[k].id,
                           drops[k].age, drops[k].count, drops[k].x, drops[k].y, drops[k].z,
                           neth_vec_nether_block(v, items, x, y, z), neth_vec_nether_block(v, items, x, y - 1, z));
                }
                printf("\n");
            }
        }
        if (dump > 0 && de / gs == g && gcount[g] == dump_at)
        {
            /* lane/nether2: a developer's map of env 0's client world round the player: the fortress's blocks by
             * height, a top-down map of each column's highest fortress block (its height less 40, base 36), and
             * of the feet's level (# solid, . air, L lava, F fire, P portal, B fortress, - not loaded) */
            int px = (int)nb[de].pos[0] - (nb[de].pos[0] < 0), py = (int)nb[de].pos[1], pz = (int)nb[de].pos[2] - (nb[de].pos[2] < 0);
            printf("dump at %d: player %d %d %d\n", dump_at, px, py, pz);
            for (int y = 0; y < 128; ++y)
            {
                int cnt = 0, x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
                for (int x = px - dump; x <= px + dump; ++x)
                    for (int z = pz - dump; z <= pz + dump; ++z)
                    {
                        int b = neth_vec_nether_block(v, de, x, y, z);
                        if (b == 112 || b == 113 || b == 114 || b == 52)
                        {
                            ++cnt;
                            if (x < x0) x0 = x;
                            if (x > x1) x1 = x;
                            if (z < z0) z0 = z;
                            if (z > z1) z1 = z;
                        }
                        if (b == 52) printf("spawner %d %d %d\n", x, y, z);
                    }
                if (cnt) printf("y %3d: %5d fortress blocks, x %d..%d z %d..%d\n", y, cnt, x0, x1, z0, z1);
            }
            static const char B36[] = "0123456789abcdefghijklmnopqrstuvwxyz";
            for (int pass = 0; pass < 3; ++pass)
            {
                printf(pass ? "the feet's level (y %d), north up, x from %d:\n" : "highest fortress block - 40, x from %d:\n",
                       pass ? py - (pass == 2) : px - dump, px - dump);
                if (pass) printf("x from %d\n", px - dump);
                for (int z = pz - dump; z <= pz + dump; ++z)
                {
                    char line[1024];
                    int k = 0;
                    for (int x = px - dump; x <= px + dump && k < 1000; ++x)
                    {
                        char ch = '.';
                        if (!pass)
                        {
                            for (int y = 127; y >= 0; --y)
                            {
                                int b = neth_vec_nether_block(v, de, x, y, z);
                                if (b < 0) { ch = '-'; break; }
                                if (b == 112 || b == 113 || b == 114 || b == 52)
                                {
                                    int h = y - 40;
                                    ch = h < 0 ? '_' : h > 35 ? '^' : B36[h];
                                    break;
                                }
                            }
                        }
                        else
                        {
                            int b = neth_vec_nether_block(v, de, x, py - (pass == 2), z);
                            ch = b < 0 ? '-' : b == 0 ? '.' : b == 88 ? 'S' : b == 4 ? 'C' : b == 10 || b == 11 ? 'L' : b == 51 ? 'F' : b == 90 ? 'P'
                                 : b == 112 || b == 113 || b == 114 ? 'B' : '#';
                        }
                        if (x == px && z == pz) ch = '@';
                        line[k++] = ch;
                    }
                    line[k] = 0;
                    printf("z %5d %s\n", z, line);
                }
            }
            /* lane/nether5: --dump-levels LO HI, each level's map in a box of half side dump/2 (the feet's
             * characters; the level's own height in the header) */
            for (int y = dlo; y <= dhi && dhi > 0; ++y)
            {
                int r = dump / 2;
                printf("level y %d, x from %d:\n", y, px - r);
                for (int z = pz - r; z <= pz + r; ++z)
                {
                    char line[512];
                    int k = 0;
                    for (int x = px - r; x <= px + r && k < 500; ++x)
                    {
                        int b = neth_vec_nether_block(v, de, x, y, z);
                        char ch = b < 0 ? '-' : b == 0 ? '.' : b == 88 ? 'S' : b == 4 ? 'C' : b == 10 ? 'l' : b == 11 ? 'L'
                                  : b == 51 ? 'F' : b == 90 ? 'P' : b == 52 ? '*' : b == 112 || b == 113 || b == 114 ? 'B' : '#';
                        if (x == px && z == pz) ch = '@';
                        line[k++] = ch;
                    }
                    line[k] = 0;
                    printf("z %5d %s\n", z, line);
                }
            }
        }
        if (trace >= g * gs && trace < (g + 1) * gs && gcount[g] % every == 0)
        {
            const struct neth_vec_nether *q = &nb[trace];
            printf("trace %d t %d: dim %d at %.1f %.1f %.1f hp %.1f phase %d mode %d cobble %d rods %d deaths %d "
                   "goal %d %d %d %d%s\n",
                   trace, gcount[g], q->dim, q->pos[0], q->pos[1], q->pos[2], q->hp, q->phase, q->mode, q->cobble,
                   q->rods, q->deaths, q->goal[0], q->goal[1], q->goal[2], q->goal[3], q->goal[4] ? " lava" : "");
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
    static const char *const CAUSE[6] = {"lava", "fire", "fall", "mob", "starve", "other"};
    int64_t *ev[5];
    for (int k = 0; k < 5; ++k) ev[k] = malloc((size_t)n * sizeof(int64_t));
    int found = 0, sp = 0, rod1 = 0, rods7 = 0, home = 0, home7 = 0, dead = 0, deaths = 0, cause[6] = {0}, portal = 0;
    int hist[8] = {0}, mhist[8] = {0};
    int64_t kills = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_nether *q = &nb[i];
        ev[0][i] = q->found, ev[1][i] = q->spawner, ev[2][i] = q->rod1, ev[3][i] = q->rods7, ev[4][i] = q->home;
        found += q->found >= 0, sp += q->spawner >= 0, rod1 += q->rod1 >= 0, rods7 += q->rods7 >= 0;
        home += q->home >= 0, home7 += q->home >= 0 && q->rods >= 7;
        dead += q->deaths > 0, deaths += q->deaths, portal += q->portal[0];
        hist[q->rods > 7 ? 7 : q->rods < 0 ? 0 : q->rods] += 1;
        mhist[q->rods_max > 7 ? 7 : q->rods_max < 0 ? 0 : q->rods_max] += 1;
        kills += q->kills;
        for (int k = 0; k < 6; ++k) cause[k] += q->cause[k];
        if (!quiet)
        {
            printf("env %2d: dim %2d at %.0f %.0f %.0f phase %d arrive %lld portal %s(%d %d %d) found %6lld spawner %6lld "
                   "rod1 %6lld rods7 %6lld home %6lld; rods %d (max %d), kills %d, deaths %d",
                   i, q->dim, q->pos[0], q->pos[1], q->pos[2], q->phase, (long long)q->arrive,
                   q->portal[0] ? "" : "none ", q->portal[1], q->portal[2], q->portal[3],
                   (long long)q->found, (long long)q->spawner, (long long)q->rod1, (long long)q->rods7,
                   (long long)q->home, q->rods, q->rods_max, q->kills, q->deaths);
            for (int k = 0; k < 6; ++k)
                if (q->cause[k]) printf(" %s %d", CAUSE[k], q->cause[k]);
            if (q->death_t >= 0)
                printf(" (at %lld, %.1f %.1f %.1f)", (long long)q->death_t, q->death_pos[0], q->death_pos[1],
                       q->death_pos[2]);
            if (q->fort[0]) printf("; fort %d %d %d", q->fort[1], q->fort[2], q->fort[3]);
            if (q->sp[0]) printf("; spawner %d %d %d", q->sp[1], q->sp[2], q->sp[3]);
            if (q->opaths || q->or_lock_steps)   /* lane/rod: nether_oracle's books (privileged options only) */
                printf("; oracle paths %d found %d lock %lld bstat %d", q->opaths, q->opaths_ok,
                       (long long)q->or_lock_steps, q->bstat);
            if (q->home >= 0) printf("; home with %d rods at health %.1f", q->home_rods, q->home_hp);
            if (q->portal_dmin >= 0) printf("; portal %d blocks off (least %d)", q->portal_d, q->portal_dmin);
            printf("\n");
        }
    }
    static const char *const EV[5] = {"fortress found", "spawner found", "first rod", "7 rods", "home"};
    printf("medians (envs reaching it):");
    for (int k = 0; k < 5; ++k)
    {
        int m;
        int64_t md = median(ev[k], n, &m);
        printf("%s %s %lld (%d)", k ? "," : "", EV[k], (long long)md, m);
    }
    printf("\nrods held at the end:");
    for (int k = 0; k <= 7; ++k) printf(" %d:%d", k, hist[k]);
    printf("; at most:");
    for (int k = 0; k <= 7; ++k) printf(" %d:%d", k, mhist[k]);
    printf("; kills %lld; deaths %d in %d envs:", (long long)kills, deaths, dead);
    for (int k = 0; k < 6; ++k) printf(" %s %d", CAUSE[k], cause[k]);
    int64_t scan_ns = 0, nsteps = 0;
    for (int i = 0; i < n; ++i) scan_ns += nb[i].scan_ns, nsteps += nb[i].nsteps;
    printf("\nscans: %.2f s over %lld Nether steps, %.2f us a step\n", 1e-9 * (double)scan_ns, (long long)nsteps,
           nsteps ? 1e-3 * (double)scan_ns / (double)nsteps : 0.0);
    {
        /* lane/rod: the link's funnel (envs reaching each) and the blaze kills two ways: the fights' locks lost on a
         * blaze (killed or gone) and the stats screen's (the player's credited kills, the only ones that drop a rod) */
        int fo = 0, bs = 0, bh = 0, bk = 0, bt = 0, r1 = 0;
        int64_t nbk = 0, nbt = 0, nbh = 0;
        for (int i = 0; i < n; ++i)
        {
            fo += nb[i].found >= 0, bs += nb[i].bseen >= 0, bh += nb[i].bhit >= 0, bk += nb[i].bkills > 0;
            bt += nb[i].bstat > 0, r1 += nb[i].rods_max > 0;
            nbk += nb[i].bkills, nbt += nb[i].bstat, nbh += nb[i].bhits;
        }
        printf("funnel: fortress seen %d, blaze seen %d, struck %d, lock lost on a blaze %d, a blaze killed (stats) %d, "
               "a rod %d; strikes %lld, locks lost %lld, blazes killed %lld\n", fo, bs, bh, bk, bt, r1,
               (long long)nbh, (long long)nbk, (long long)nbt);
        /* lane/rod: on the fortress (its brick underfoot, nt fort_at) before any death, and the median step of it */
        int fin = 0, fin_alive = 0, m;
        for (int i = 0; i < n; ++i)
        {
            ev[0][i] = nb[i].fortin;
            fin += nb[i].fortin >= 0;
            fin_alive += nb[i].fortin >= 0 && (nb[i].death_t < 0 || nb[i].death_t > nb[i].fortin);
        }
        int64_t md = median(ev[0], n, &m);
        printf("fortress: on it %d, on it before any death %d, median step %lld\n", fin, fin_alive, (long long)md);
    }
    {
        /* lane/nether4: ghasts and their fireballs, the cover at the spawner */
        int64_t df = 0, dh = 0, gs = 0, cv = 0, hd = 0;
        int gsee = 0;
        for (int i = 0; i < n; ++i)
            df += nb[i].deflects, dh += nb[i].deflect_hits, gs += nb[i].ghast_seen, cv += nb[i].covers,
                gsee += nb[i].ghast_seen > 0, hd += nb[i].hoods;
        printf("ghasts: in view %lld steps (%d envs), fireballs aimed at %lld, hits pressed %lld; cover blocks %lld, "
               "hoods %lld\n", (long long)gs, gsee, (long long)df, (long long)dh, (long long)cv, (long long)hd);
    }
    printf("nether_case: %d envs, %d steps, seed %llu, policy %d, macros %d, %.1f s: portal %d, fortress %d, "
           "spawner %d, a rod %d, 7 rods %d, home %d (with 7 rods %d), died %d; macro options %s\n",
           n, steps, (unsigned long long)seed, policy, macros, wall, portal, found, sp, rod1, rods7, home, home7, dead,
           mopts[0] ? mopts : "(defaults)");
    neth_vec_free(v);
    return 0;
}
