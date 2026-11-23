/* kit_case (lane/rod): the End kit macros' case (armor_kit, mods/macros/armor.c), no Python. N envs from a start (the
 * config's pipeline.start, or --start DIR), one episode of S steps each under vec_bench's random policy (--policy 1,
 * its xorshift seeded by --seed) or the idle one (0), the macros --macros and the options --opt NAME=WORD. At the end, a
 * line an env (the pieces worn and points, ores dug and given up, the steps of the first ore, the ingots, three and four
 * pieces worn, deaths) and the totals with each event's median step.
 *
 *   out/runtime/kit_case [--config F] [--start DIR] [--dev] [--ns 32] [--groups 2] [--threads T] [--steps 12000]
 *                        [--seed K] [--policy 0|1] [--macros BITS] [--opt NAME=WORD]... [--every K] [--quiet]
 *                        [--trace-env E] [--tape SCRIPT --rows ROWS]
 *
 * --trace-env E: the --every trace and the tape are env E's. --tape: env E's second episode (the run two episodes long)
 * written as the oracle's script (neth_vec_tape) and its rows (neth_rl_rows) for a Java replay row for row: `make -C oracle
 * script FROM=<the start's checkpoint> SCRIPT=SCRIPT` and test_snapshots --row-dump, as rl-tape-gate does; the start is
 * then the config's pipeline.start (the second episode's) and must be the oracle's Snapshot of the checkpoint's join.
 *
 * The last line is "kit_case: N envs, ... three worn W3, four worn W4" for a gate's grep. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rlgui.h"
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

static int cmp64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

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
    const char *config = "configs/train-fast.yaml", *start = NULL, *tape = NULL, *rowsp = NULL;
    int dev = 0, n = 32, groups = 2, threads = 0, steps = 12000, policy = 1, quiet = 0, every = 0, te = 0;
    int macros = NETH_VEC_ALL | NETH_VEC_TABLE2 | NETH_VEC_IRON | NETH_VEC_LATE | NETH_VEC_LIQUID | NETH_VEC_FIGHT |
                 NETH_VEC_CARRY | NETH_VEC_FOOD;
    uint64_t seed = 1;
    char mopts[1024] = "";
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace-env") && i + 1 < argc) te = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tape") && i + 1 < argc) tape = argv[++i];
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc) rowsp = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: kit_case [--config F] [--start DIR] [--dev] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--opt NAME=WORD]... [--every K] "
                            "[--quiet]\n");
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
    o.ep_len = steps + 10;
    /* --tape PATH --rows PATH (with --trace-env E): env E's second episode written as the oracle's script
     * (neth_vec_tape: the first one's start was queued by the make) and its rows (neth_rl_rows), the run two episodes long;
     * the books printed are the second episode's last step's */
    int total = tape ? 2 * (steps + 10) : steps;
    o.macros = macros;
    o.macro_opts = mopts;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? 1 : 0;              /* rlgui.h NETH_RL_DEV */
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
    struct neth_vec_kit *kb = calloc((size_t)n, sizeof *kb);
    int *deaths = calloc((size_t)n, sizeof *deaths), *wasdead = calloc((size_t)n, sizeof *wasdead);
    int armed = 0;
    struct neth_vec_kit tk;
    memset(&tk, 0, sizeof tk);
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        if (tape && !armed && g == te / gs)
        {
            if (neth_vec_tape(v, te, tape) || (rowsp && neth_rl_rows(neth_vec_env(v), te, rowsp, 0)))
            {
                printf("FAIL: the tape or the rows of env %d not opened\n", te);
                return 1;
            }
            armed = 1;
        }
        if (tape && g == te / gs && gcount[g] == total - 50) neth_vec_kit(v, te, &tk);   /* its books 40 steps before its end (the run's
                                                                         * last steps are the next episode's) */
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            neth_vec_kit(v, i, &kb[i]);
            if (kb[i].dead && !wasdead[i]) deaths[i] += 1;
            wasdead[i] = kb[i].dead;
        }
        if (every > 0 && g == te / gs && gcount[g] % every == 0)
        {
            const struct neth_vec_kit *q = &kb[te];
            printf("trace %d t %d:", te, gcount[g]);
            printf(" t %d: at %.1f %.1f %.1f worn %d ores %d gave up %d plans %d cells %d; held ore %d ingots %d "
                   "pieces %d snowballs %d; kind %d plan %d armor %d\n", gcount[g], q->pos[0], q->pos[1], q->pos[2],
                   q->worn, q->ores_dug, q->gave_up, q->plans, q->cells, q->n_ore, q->n_ingot, q->n_pieces, q->n_snow,
                   q->kind, q->cplan, q->on);
        }
        if (++gcount[g] > total)
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
    int64_t *ev[7];
    for (int k = 0; k < 7; ++k) ev[k] = malloc((size_t)n * sizeof(int64_t));
    int w3 = 0, w4 = 0, died = 0, ore1 = 0, ing = 0, pts = 0, dug = 0, sh = 0, s16 = 0, s64 = 0, snow = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_kit *q = &kb[i];
        ev[0][i] = q->t_ore1, ev[1][i] = q->t_ingots, ev[2][i] = q->t_worn3, ev[3][i] = q->t_worn4;
        ev[4][i] = q->t_shovel, ev[5][i] = q->t_snow16, ev[6][i] = q->t_snow64;
        w3 += q->t_worn3 >= 0, w4 += q->t_worn4 >= 0, died += deaths[i] > 0, ore1 += q->t_ore1 >= 0;
        ing += q->t_ingots >= 0, pts += q->points, dug += q->ores_dug;
        sh += q->t_shovel >= 0, s16 += q->t_snow16 >= 0, s64 += q->t_snow64 >= 0, snow += q->n_snow;
        if (!quiet)
            printf("env %2d: at %.0f %.0f %.0f worn %d (%d points) ores dug %d gave up %d plans %d cells %d; ore1 %lld "
                   "ingots %lld worn3 %lld worn4 %lld; snowballs %d (cells dug %d, given up %d) shovel %lld snow64 "
                   "%lld; deaths %d\n",
                   i, q->pos[0], q->pos[1], q->pos[2], q->worn, q->points, q->ores_dug, q->gave_up, q->plans, q->cells,
                   (long long)q->t_ore1, (long long)q->t_ingots, (long long)q->t_worn3, (long long)q->t_worn4,
                   q->n_snow, q->sn_dug, q->sn_gave_up, (long long)q->t_shovel, (long long)q->t_snow64, deaths[i]);
    }
    static const char *const EV[7] = {"first ore", "ingots", "three worn", "four worn", "shovel", "16 snowballs",
                                      "64 snowballs"};
    printf("medians (envs reaching it):");
    for (int k = 0; k < 7; ++k)
    {
        int m;
        int64_t md = median(ev[k], n, &m);
        printf("%s %s %lld (%d)", k ? "," : "", EV[k], (long long)md, m);
    }
    printf("\nkit_case: %d envs, %d steps, seed %llu, policy %d, macros %d, %.1f s: an ore %d, the ingots %d, ores dug "
           "%d, armor points %d, died %d, three worn %d, four worn %d; a shovel %d, snowballs %d, 16 %d, 64 %d; macro "
           "options %s\n",
           n, steps, (unsigned long long)seed, policy, macros, wall, ore1, ing, dug, pts, died, w3, w4, sh, snow, s16,
           s64, mopts[0] ? mopts : "(defaults)");
    if (tape)
    {
        if (rowsp) neth_rl_rows(neth_vec_env(v), te, NULL, 0);
        printf("tape: env %d's second episode in %s: worn %d (%d points), first ore %lld, three worn %lld, four worn "
               "%lld; snowballs %d, shovel %lld, 64 snowballs %lld\n", te, tape, tk.worn, tk.points,
               (long long)tk.t_ore1, (long long)tk.t_worn3, (long long)tk.t_worn4, tk.n_snow, (long long)tk.t_shovel,
               (long long)tk.t_snow64);
    }
    neth_vec_free(v);
    return 0;
}
