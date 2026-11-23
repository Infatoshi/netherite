/* dragon_case (lane/dragon): the End fight's case. N envs from an End start through the vector env, one episode
 * each of S steps, an idle (the centre bins, no key) or random policy (vec_bench's draw, seeded), the macros given;
 * every step each env's End books read (vecenv.h neth_vec_dragon_info). Prints a line a minute (the envs' mean
 * crystals left and dragon health, the kills, the exits, the deaths so far), then the summary: the kill and the exit
 * portal (how many envs, the median and fastest step), the deaths and what was near each, arrows shot and crystals
 * hit, and the episode records' milestones (vecenv.h NETH_VEC_MS_END, NETH_VEC_MS_DRAGON).
 *
 *   out/runtime/dragon_case --config configs/dragon.yaml [--start DIR] [--dev] [--ns 32] [--groups 2] [--threads T]
 *                           [--steps 24000] [--macros BITS] [--policy 0|1] [--seed K] [--trace ID] [--tape DIR]
 *
 * --deaths prints every End death's line (its cause by the server's CombatTracker, the push before it, the macro's
 * phase, where); the counts by cause and phase print always. --trace ID prints env ID's books every 100 steps (--every N: N). --dev takes a dev start (rlgui.h NETH_RL_DEV). --tape DIR runs each
 * env's episode twice, the sampler restarted for the second (the start and the draws the same: the same episode), and
 * writes the second as the oracle's script, DIR/env-I.jsonl (neth_vec_tape tapes an env's next episode); the summary
 * is the first's, and a line an env the second's end (`make -C oracle script SEED=S CONF=WORLD.yaml FROM=START
 * SCRIPT=DIR/env-I.jsonl`: CONF the config's world, or the oracle plays config.yaml's, difficulty normal; lane/kaudit). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "endview.h"
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

/* vec_bench's draw: policy 0 idle, 1 random */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

static int cmp_ll(const void *a, const void *b)
{
    long long x = *(const long long *)a, y = *(const long long *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    const char *config = "configs/dragon.yaml", *start = NULL, *tape = NULL;
    int n = 32, groups = 2, threads = 0, steps = 24000, policy = 0, dev = 0, trace = -1, map_r = 0, map_s = 1, map_t = 1, every = 100;
    int rows_id = -1, verbose_deaths = 0;
    char mopts[512] = "";
    int macros = NETH_VEC_ALL | NETH_VEC_FOOD | NETH_VEC_DRAGON;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--tape") && i + 1 < argc) tape = argv[++i];
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows_id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deaths")) verbose_deaths = 1;
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            /* lane/shold: a macro option, NAME=WORD (neth_vec_opts.macro_opts; sh_check --opts lists them) */
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else if (!strcmp(argv[i], "--map") && i + 3 < argc)
            map_r = atoi(argv[++i]), map_s = atoi(argv[++i]), map_t = atoi(argv[++i]);
        else
        {
            fprintf(stderr, "usage: dragon_case [--config F] [--start DIR] [--dev] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--macros BITS] [--policy 0|1] [--seed K] [--trace ID] [--tape DIR] "
                            "[--opt NAME=WORD]...\n");
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
    o.ep_len = steps;
    o.macros = macros;
    o.macro_opts = mopts;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? NETH_RL_DEV : 0;
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
    uint64_t *rng0 = malloc((size_t)n * sizeof *rng0);
    memcpy(rng0, rng, (size_t)n * sizeof *rng);
    int total = tape ? 2 * steps : steps;   /* --tape: the tape is each env's second episode (vecenv.h neth_vec_tape) */
    int *ntr = calloc((size_t)n, sizeof *ntr);
    double (*last2)[NETH_VEC_DRAGON_INFO] = calloc((size_t)n, sizeof *last2);
    int *got = calloc((size_t)groups, sizeof *got), live = groups;
    int minutes = steps / 1200 + 1;
    /* per minute: the sums over envs of crystals left, dragon health (envs with a copy), and the counts so far */
    double *cr = calloc((size_t)minutes, sizeof *cr), *dh = calloc((size_t)minutes, sizeof *dh);
    int *dn = calloc((size_t)minutes, sizeof *dn), *inend = calloc((size_t)minutes, sizeof *inend);
    double *mar = calloc((size_t)minutes, sizeof *mar), *mbt = calloc((size_t)minutes, sizeof *mbt);
    double *mhp = calloc((size_t)minutes, sizeof *mhp), *mfd = calloc((size_t)minutes, sizeof *mfd);
    double *cnear = calloc((size_t)minutes, sizeof *cnear);   /* lane/dragon3: copies within 80 of the centre */
    long phst[2][14] = {{0}};   /* lane/dragon3: steps in the End by the macro's phase, each half of the episode */
    float beam_at[64][3];
    long beam_n[64];
    int nbeam = 0;
    long *tk0 = calloc((size_t)n, sizeof *tk0), *tk1 = calloc((size_t)n, sizeof *tk1);
    long *st0 = calloc((size_t)n, sizeof *st0);
    double (*last)[NETH_VEC_DRAGON_INFO] = calloc((size_t)n, sizeof *last);
    int *c0 = calloc((size_t)n, sizeof *c0), *cmin = calloc((size_t)n, sizeof *cmin);
    for (int i = 0; i < n; ++i) c0[i] = cmin[i] = -1;
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        int t = got[g]++;
        if (tape && t == 0)
            for (int i = g * gs; i < (g + 1) * gs; ++i)
            {
                /* --tape DIR: every env's first episode as the oracle's script (neth_vec_tape), DIR/env-I.jsonl */
                char p[1024];
                snprintf(p, sizeof p, "%s/env-%d.jsonl", tape, i);
                if (neth_vec_tape(v, i, p))
                {
                    printf("FAIL: --tape %s\n", p);
                    return 1;
                }
                /* --rows ID: env ID's rows besides (test_snapshots --row-dump's, both episodes: the taped one's start
                 * at its second t=2 row), to hold against the oracle's rows of the tape (rl-tape-gate's check) */
                snprintf(p, sizeof p, "%s/env-%d.rows", tape, i);
                if (i == rows_id && neth_rl_rows(neth_vec_env(v), i, p, 0))
                {
                    printf("FAIL: --rows %s\n", p);
                    return 1;
                }
            }
        if (map_r > 0 && g == 0 && t == map_t)
        {
            /* --map R: env 0's client world around it, the top block's height a cell (debugging the macro only: the
             * macro never reads this). '.' nothing loaded or the void, '0'..'9' the top's height in 4s past y 40,
             * 'o' obsidian on top, '@' the player */
            double q[NETH_VEC_DRAGON_INFO];
            neth_vec_dragon_info(v, 0, q);
            int px = (int)q[1], pz = (int)q[3], w = 2 * map_r + 1;
            int16_t *bl = malloc((size_t)w * w * 128 * sizeof *bl);
            if (neth_rl_blocks(neth_vec_env(v), 0, px - map_r, 0, pz - map_r, px + map_r, 127, pz + map_r, bl) == 0)
                for (int z = 0; z < w; z += map_s)
                {
                    for (int x = 0; x < w; x += map_s)
                    {
                        int top = -1, id = 0;
                        for (int y = 127; y >= 0 && top < 0; --y)
                        {
                            int b = bl[((size_t)y * w + z) * w + x];
                            if (b > 0) top = y, id = b;
                        }
                        char c = top < 0 ? '.' : id == 49 ? 'o' : id == 7 ? 'B' : top < 40 ? '_' : (char)('0' + (top - 40) / 4 > 9 ? 9 : (top - 40) / 4);
                        if (top >= 0 && id != 49 && id != 7) c = (char)('0' + ((top - 40) / 4 > 9 ? 9 : (top - 40) / 4 < 0 ? 0 : (top - 40) / 4));
                        if (px - map_r + x == px && pz - map_r + z == pz) c = '@';
                        putchar(c);
                    }
                    putchar('\n');
                }
            free(bl);
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            double q[NETH_VEC_DRAGON_INFO];
            /* the receive whose truncation is set holds the next episode's reset already */
            if (neth_vec_dragon_info(v, i, q) || t >= steps || b.truncations[i]) continue;
            memcpy(last[i], q, sizeof q);
            if (q[0] == 1 && q[9] >= 0 && q[9] < 14) ++phst[t < steps / 2 ? 0 : 1][(int)q[9]];
            {
                /* lane/dragon3 (the books' only): the healing beam's crystal, by place, steps it was seen */
                const struct neth_rl_end *en = q[0] == 1 ? neth_vec_end_view(v, i) : NULL;
                /* the dragon ran this step (one tick a step: a step's tick count past one is a stretch without a copy) */
                long run = en && tk1[i] > 0 && en->sv_ticks - tk1[i] == 1;
                if (en && en->sv_heal && en->sv_dragon_hp > 0.0f && en->sv_dragon_hp < 200.0f && run > 0)
                {
                    /* the server's healing crystal on the ticks the dragon ran hurt and alive (1 every 10 ticks) */
                    int k = 0;
                    while (k < nbeam && (beam_at[k][0] != en->sv_heal_at[0] || beam_at[k][1] != en->sv_heal_at[2])) ++k;
                    if (k == nbeam && nbeam < 64) beam_at[nbeam][0] = en->sv_heal_at[0],
                                                  beam_at[nbeam][1] = en->sv_heal_at[2],
                                                  beam_at[nbeam][2] = en->sv_heal_at[1], beam_n[nbeam++] = 0;
                    if (k < 64) beam_n[k] += run;
                }
                if (en && en->dragon_id && en->sv_dragon_hp > 0.0f)
                {
                    /* the server dragon's ticks against the steps it was alive (its updates stop while frozen) */
                    /* its tick count restarts when its chunk reloads (ticksExisted is not saved): count the steps it
                     * ran once, against the steps with a copy */
                    tk0[i] += tk1[i] > 0 && en->sv_ticks - tk1[i] == 1;
                    ++st0[i];
                    tk1[i] = en->sv_ticks;
                }
            }
            if (q[0] == 1)
            {
                /* the most copies the client held (they arrive over the first ticks), the fewest after it */
                if (q[5] > c0[i]) c0[i] = (int)q[5], cmin[i] = (int)q[5];
                if (cmin[i] < 0 || q[5] < cmin[i]) cmin[i] = (int)q[5];
            }
            if (t % 1200 == 0)
            {
                int m = t / 1200;
                cr[m] += q[0] == 1 ? q[5] : cmin[i] > 0 ? cmin[i] : 0;
                if (q[7] >= 0) dh[m] += q[7], ++dn[m];
                inend[m] += q[0] == 1;
                if (q[0] == 1) mar[m] += q[48], mbt[m] += q[49], mhp[m] += q[4], mfd[m] += q[50];
                /* lane/dragon3 (the books' only): the crystal copies the client holds within 80 blocks of the island's
                 * centre (the dragon's reach from the fight; copies are held only in the loaded chunks) */
                const struct neth_rl_end *en = q[0] == 1 ? neth_vec_end_view(v, i) : NULL;
                for (int k = 0; en && k < en->held && k < NETH_RL_END_CRYSTALS; ++k)
                    cnear[m] += en->held_at[k][0] * en->held_at[k][0] + en->held_at[k][2] * en->held_at[k][2] <= 6400.0f;
            }
            if (i == trace && t % every == 0)
                printf("trace %d t %d: dim %.0f at %.1f %.1f %.1f health %.1f; crystals %.0f (in view %.0f), dragon %.1f%s; "
                       "phase %.0f (sub %.0f), shots %.0f, hits %.0f, swings %.0f, deaths %.0f, endermen in view %.0f; "
                       "arrows %.0f, blocks %.0f (hotbar %.0f), food %.0f, the dragon last at %.1f %.1f; crosshair %.0f at %+.0f, rise %.0f\n",
                       i, t, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8] ? " (dead)" : "", q[9], q[66], q[10],
                       q[11], q[20], q[14], q[15], q[48], q[67], q[68], q[50], q[64], q[65], q[69], q[70], q[71]);
            if ((i == trace || trace == -2) && t % 1200 == 0)
            {
                /* the crystal copies the client holds (the books' only), the macro's kept ones (id hunts/arrows) */
                const struct neth_rl_end *en = neth_vec_end_view(v, i);
                printf("trace %d t %d at %.0f %.0f %.0f: crystals held:", i, t, q[1], q[2], q[3]);
                for (int k = 0; en && k < en->held && k < NETH_RL_END_CRYSTALS; ++k)
                    printf(" %d (%.1f %.1f %.1f)", en->held_id[k], en->held_at[k][0], en->held_at[k][1],
                           en->held_at[k][2]);
                printf("; kept %.0f:", q[54]);
                for (int k = 0; k < 3 && k < q[54]; ++k)
                    printf(" %.0f %.0f/%.0f", q[55 + 3 * k], q[56 + 3 * k], q[57 + 3 * k]);
                if (en && en->dragon_id)
                    printf("; the dragon's body at %.1f %.1f %.1f%s", en->body[0], en->body[1], en->body[2],
                           en->dragon_view ? " (in view)" : "");
                printf("\n");
            }
        }
        for (int i = g * gs; tape && i < (g + 1) * gs; ++i)
        {
            /* --tape: at an env's first truncation (its receive holds the second episode's reset) the sampler from the
             * first's state, so the episode taped is the first one again; at its second, the taped episode's end */
            double q[NETH_VEC_DRAGON_INFO];
            if (b.truncations[i] && ++ntr[i] == 1) rng[i] = rng0[i];
            else if (b.truncations[i] && ntr[i] == 2)
                printf("taped episode, env %d (%s/env-%d.jsonl): the dragon killed at step %.0f, the exit portal at %.0f;"
                       " at its last step dim %.0f at %.3f %.3f %.3f health %.1f\n", i, tape, i, last2[i][12],
                       last2[i][13], last2[i][0], last2[i][1], last2[i][2], last2[i][3], last2[i][4]);
            else if (ntr[i] == 1 && !neth_vec_dragon_info(v, i, q))
                memcpy(last2[i], q, sizeof q);
        }
        if (t >= total)
        {
            --live;
            continue;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i) draw(&rng[i], b.actions + (size_t)i * aw, policy);
        if (neth_vec_send(v, g))
        {
            printf("FAIL: send %d\n", g);
            return 1;
        }
    }
    double wall = now_s() - t0;
    if (tape && rows_id >= 0 && rows_id < n) neth_rl_rows(neth_vec_env(v), rows_id, NULL, 0);
    static struct neth_vec_episode eps[4096];
    int ne = neth_vec_episodes(v, eps, 4096), ms_end = 0, ms_dragon = 0;
    for (int k = 0; k < ne; ++k)
    {
        ms_end += (eps[k].ms >> NETH_VEC_MS_END) & 1;
        ms_dragon += (eps[k].ms >> NETH_VEC_MS_DRAGON) & 1;
    }
    printf("dragon_case: %d envs, %d steps each, policy %s seed %llu, macros %d, %s; %.0f s\n", n, steps,
           policy ? "random" : "idle", (unsigned long long)seed, macros, start ? start : config, wall);
    /* lane/dragon3: the rate (out/native/abrun's default metric); the make and the episodes' end included */
    printf("dragon_case: %.0f env-steps/s\n", (double)n * total / wall);
    printf("minute  in-End  crystals-left  near  dragon-health  kills  exits  arrows  bites  health  food (near: copies "
           "within 80 of the centre a env; the last four: means over the envs in the End)\n");
    for (int m = 0; m < minutes && m * 1200 <= steps; ++m)
    {
        int kills = 0, exits = 0, deaths = 0;
        for (int i = 0; i < n; ++i)
        {
            kills += last[i][12] >= 0 && last[i][12] <= m * 1200;
            exits += last[i][13] >= 0 && last[i][13] <= m * 1200;
        }
        (void)deaths;
        double ie = inend[m] ? inend[m] : 1;
        printf("%6d  %6d  %13.2f  %4.2f  %13.1f  %5d  %5d  %6.1f  %5.1f  %6.1f  %4.1f\n", m, inend[m], cr[m] / n,
               cnear[m] / n, dn[m] ? dh[m] / dn[m] : -1.0, kills, exits, mar[m] / ie, mbt[m] / ie, mhp[m] / ie, mfd[m] / ie);
    }
    long long ks[4096], xs_[4096];
    int nk = 0, nx = 0;
    long long ds[4096];
    int nd = 0;
    double admg = 0, sdmg = 0, shit = 0;
    double died[4] = {0}, deaths = 0, shots = 0, hits = 0, swings = 0, lost = 0, dshots = 0, dhits = 0, dmin = 0;
    int ndmin = 0;
    for (int i = 0; i < n; ++i)
    {
        if (last[i][12] >= 0) ks[nk++] = (long long)last[i][12];
        if (last[i][13] >= 0) xs_[nx++] = (long long)last[i][13];
        for (int k = 0; k < 4; ++k) died[k] += last[i][16 + k];
        if (last[i][24] >= 0) ds[nd++] = (long long)last[i][24];
        deaths += last[i][14];
        shots += last[i][10];
        hits += last[i][11];
        swings += last[i][20];
        dshots += last[i][22];
        dhits += last[i][23];
        admg += last[i][51], sdmg += last[i][52], shit += last[i][53];
        if (last[i][21] >= 0) dmin += last[i][21], ++ndmin;
        lost += c0[i] >= 0 && cmin[i] >= 0 ? c0[i] - cmin[i] : 0;
    }
    for (int i = 0; i < n; ++i)
        if (last[i][12] >= 0)
            printf("env %d: the dragon killed at step %.0f, the exit portal at %.0f; first death in the End at %.0f; at "
                   "the end dim %.0f at %.1f %.1f %.1f health %.1f\n", i, last[i][12], last[i][13], last[i][24],
                   last[i][0], last[i][1], last[i][2], last[i][3], last[i][4]);
    qsort(ks, (size_t)nk, sizeof *ks, cmp_ll);
    qsort(xs_, (size_t)nx, sizeof *xs_, cmp_ll);
    printf("killed %d of %d (median step %lld, fastest %lld); exit portal %d of %d (median step %lld, fastest %lld)\n", nk,
           n, nk ? ks[nk / 2] : -1, nk ? ks[0] : -1, nx, n, nx ? xs_[nx / 2] : -1, nx ? xs_[0] : -1);
    qsort(ds, (size_t)nd, sizeof *ds, cmp_ll);
    printf("deaths %.0f (in the End, by what was near: void %.0f, dragon %.0f, enderman %.0f, other %.0f; the first in "
           "the End: %d envs, median step %lld, earliest %lld); arrows %.0f, crystals hit %.0f (crystals gone %.0f), "
           "sword swings %.0f\n", deaths, died[0], died[1], died[2], died[3], nd,
           nd ? ds[nd / 2] : -1, nd ? ds[0] : -1, shots, hits, lost, swings);
    printf("the dragon: arrows at it %.0f, %.0f of them hit; its lowest health seen, mean over %d envs that saw it, %.1f\n",
           dshots, dhits, ndmin, ndmin ? dmin / ndmin : -1.0);
    {
        /* lane/dragon3: the envs by the dragon's lowest health seen, in 25s */
        int hist[9] = {0};
        for (int i = 0; i < n; ++i)
            if (last[i][21] >= 0) ++hist[last[i][21] >= 200 ? 8 : (int)(last[i][21] / 25.0)];
        printf("envs by the dragon's lowest health:");
        for (int k = 0; k < 9; ++k) printf(" %s%d: %d", k == 8 ? "" : "<", k == 8 ? 200 : 25 * (k + 1), hist[k]);
        printf("\n");
    }
    printf("the dragon's health taken: by arrows %.0f (%.2f a hit), by the sword %.0f (%.0f swings landed, %.2f each)\n",
           admg, dhits ? admg / dhits : 0.0, sdmg, shit, shit ? sdmg / shit : 0.0);
    {
        /* lane/dragon3: the lobs, the arrows picked back up, the boss bar's rises, the exit's walks out */
        double lobs = 0, lobh = 0, pk = 0, heal = 0, unst = 0;
        for (int i = 0; i < n; ++i)
            lobs += last[i][72], lobh += last[i][73], pk += last[i][74], heal += last[i][76], unst += last[i][75];
        printf("lobs %.0f (%.0f took their crystal); arrows picked up %.0f; the boss bar's rises seen %.0f; the exit's "
               "walks out from under the portal %.0f\n", lobs, lobh, pk, heal, unst);
        double rr = 0, rh = 0, rg = 0;
        for (int i = 0; i < n; ++i) rr += last[i][77], rh += last[i][78], rg += last[i][79];
        printf("lane/shold's raids (dragon_oracle crystals): %.0f begun, %.0f crystals taken at the swing, %.0f given up\n",
               rr, rh, rg);
        printf("the server's healing crystal on the dragon's ticks hurt (all envs; the books' only):");
        long hsum = 0;
        for (int k = 0; k < nbeam; ++k) hsum += beam_n[k];
        for (int k = 0; k < nbeam; ++k)
            if (beam_n[k] >= 100) printf(" (%.1f %.1f %.1f) %ld", beam_at[k][0], beam_at[k][2], beam_at[k][1], beam_n[k]);
        printf("; total %ld\n", hsum);
        long dt = 0, ds_ = 0;
        for (int i = 0; i < n; ++i) dt += tk0[i], ds_ += st0[i];
        printf("the server dragon ran on %ld of the %ld steps it was alive with a copy (%.0f%%)\n", dt, ds_,
               ds_ ? 100.0 * dt / ds_ : 0.0);
    }
    {
        /* lane/dragon2: every End death by what the server's CombatTracker names (vecdragon.h DK_*), the push before it,
         * the macro's phase and where; then the counts, and the health every cause took */
        static const char *const DKN[9] = {"void", "dragon", "enderman", "fall-after-dragon-hit", "fall", "blast",
                                           "arrow", "hunger", "other"};
        enum { NPH = 14 };
        static const char *const DPN[NPH] = {"off", "climb", "seek", "shoot", "fight", "exit", "eat", "fetch", "pit",
                                             "snipe", "descend", "hunt", "follow", "raid"};
        int cn[9] = {0}, cp[9] = {0}, pn[NPH] = {0}, pushes = 0;
        double dmg[9] = {0};
        if (verbose_deaths) printf("death  env   step  cause                  pushed  phase    x      y      z      r     fall  head  food  dt/name\n");
        for (int i = 0; i < n; ++i)
        {
            const double *q = last[i];
            pushes += (int)q[37];
            for (int k = 0; k < 9; ++k) dmg[k] += q[38 + k];
            int c = (int)q[25];
            if (c < 0 || c > 8) continue;
            ++cn[c];
            cp[c] += q[26] != 0;
            int ph = (int)q[27];
            if (ph < 0 || ph >= NPH) ph = 0;
            ++pn[ph];
            if (verbose_deaths)
                printf("death %4d %6.0f  %-22s %6s  %-7s %6.1f %6.1f %6.1f %5.1f %6.1f %5.1f %5.0f  %.0f/%.0f\n", i, q[24],
                       DKN[c], q[26] ? "yes" : "", DPN[ph], q[28], q[29], q[30],
                       sqrt(q[28] * q[28] + q[30] * q[30]), q[31], q[34], q[47], q[35], q[36]);
        }
        printf("End deaths by cause (pushed in the %d steps before):", 100);
        for (int k = 0; k < 9; ++k)
            if (cn[k]) printf(" %s %d (%d)", DKN[k], cn[k], cp[k]);
        printf("\nEnd deaths by the macro's phase:");
        for (int k = 0; k < NPH; ++k)
            if (pn[k]) printf(" %s %d", DPN[k], pn[k]);
        printf("\nhealth lost in the End by cause, all envs:");
        for (int k = 0; k < 9; ++k)
            if (dmg[k] > 0) printf(" %s %.0f", DKN[k], dmg[k]);
        printf("; wing pushes seen %d\n", pushes);
        for (int h = 0; h < 2; ++h)
        {
            /* lane/dragon3: the steps in the End by phase (all envs), each half */
            long tot = 0;
            for (int k = 0; k < NPH; ++k) tot += phst[h][k];
            printf("steps in the End by phase, %s half (%ld):", h ? "second" : "first", tot);
            for (int k = 0; k < NPH; ++k)
                if (phst[h][k]) printf(" %s %.1f%%", DPN[k], 100.0 * phst[h][k] / (tot ? tot : 1));
            printf("\n");
        }
    }
    printf("records: %d episodes, the End milestone %d, the dragon milestone %d\n", ne, ms_end, ms_dragon);
    if (ne > 0)
    {
        /* the first record's items at its end (loop.c's EP_ITEMS and EXTRA_ITEMS: semteach.ITEMS' ids) */
        static const int EP[NETH_VEC_ITEMS] = {17, 162, 5, 280, 58, 270, 271, 269, 268, 4, 274, 275, 273, 272, 263, 15,
                                               265, 61, 3, 12, 13, 6, 260, 295, 318, 1, 14, 264, 56, 331, 325, 259};
        static const int EX[8] = {326, 327, 49, 257, 278, 369, 368, 381};
        printf("record 0 held at its end:");
        for (int k = 0; k < NETH_VEC_ITEMS; ++k)
            if (eps[0].items[k]) printf(" %d x%d", EP[k], eps[0].items[k]);
        for (int k = 0; k < 8; ++k)
            if (eps[0].extra[k]) printf(" %d x%d", EX[k], eps[0].extra[k]);
        printf(" (semteach's items only); food %d\n", eps[0].food);
    }
    neth_vec_free(v);
    return 0;
}
