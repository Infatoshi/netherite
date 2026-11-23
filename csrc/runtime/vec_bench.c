/* vec_bench (lane/vecenv): the vector env (vecenv.h) under random actions,
 * no Python: env-steps/s and the vector env's own C work per env-step.
 *
 *   out/runtime/vec_bench [--config configs/train-fast.yaml] [--ns 256,512] [--groups 4] [--threads T]
 *                         [--warm 100] [--steps 300] [--ep-len L] [--seed K] [--policy 0|1]
 *                         [--macros BITS] [--picks 0|1|2] [--digest] [--obs-digest] [--starts DIR [--window W[:S]]]
 *
 * --starts (lane/seedwin): every subdirectory of DIR holding a manifest.json a start of the mix (in name order, s2
 * before s10: a seed set's out/java/try/seedset-KEY), each world seed allowed (NETH_RL_ANYSEED), weighted equally with
 * the config's own; --window W:S loads W of them at a time, moving S a round (neth_vec_opts.start_window,
 * start_stride). With --starts it prints a starts line: the window's loads and frees, the most loaded at once (the
 * resident set sampled at each receive), the episodes by start (how many starts were played, the fewest and most
 * episodes one had).
 *
 * --macros: neth_vec_opts.macros (default NETH_VEC_ALL). --picks (lane/picks): 0 off; 1 the policy's craft picks on
 * and never one sent (so nothing may be crafted: the craft macro never starts on its own); 2 on, and each step an
 * allowed pick with probability 1/8 (one of the mask's at random) or else a disallowed one with probability 1/16,
 * which must never start the craft macro. With picks on it prints a picks line: the picks sent, allowed and not,
 * the craft macros an allowed pick started, the ones a disallowed pick started (must be 0), and the episodes' crafted
 * items and wooden and stone pickaxes. --digest: an FNV-1a hash an env over every step's reward, driver, kind,
 * chosen and scalars, the envs' hashes summed (any group order), printed as "trajectory digest". --obs-digest: the
 * same over every observation recv hands back (the make's reset step's included), eight bytes at a time, printed as
 * "observation digest": the camera's frames byte for byte, which the trajectory digest does not see (a host
 * observation only; with obs on the device it says so and hashes nothing).
 *
 * The macros' digest (lane/macromod: a refactor of the macros held to the same trajectories): --mopts SPEC the run
 * file's actions options (neth_vec_opts.macro_opts, NAME=WORD,...), --late BITS neth_vec_opts.late_scalars (the digest
 * then hashes every scalar of the row), --level BITS[:LABELLER] neth_vec_level after the make (LABELLER 4096 the
 * seek, 1 the surface, 8 the craft; default 4096) with the odd envs playing at the level (neth_vec_env_level),
 * --level-walk the seek's walk labeller beside the surface labeller (neth_vec_level_walk, lane/ladder18), and
 * --label-digest: an FNV-1a hash an env over every step's label (bufs.label) and act (bufs.acts) and over each finished
 * episode's record (its milestones, items, extras, deaths, kills, steps by macro kind, dims, obsidian, cells), summed
 * over the envs as "label digest" and "episode digest", and the GUI, craft labeller and seek books hashed as "books
 * digest". --dev: a dev start allowed (neth_rl_make_ex's NETH_RL_DEV: a case's kit given at its first tick). The action is
 * neth_vec_action_width wide (NETH_VEC_HOTBAR's last element 0: keep the slot).
 *
 * --walk-ask (lane/divpearl): every env asks a path every step (neth_rl_walkto_ask_rel: a cell up to WALK_ASK_R across
 * from its feet, its answer unread), the privileged search walkto.h runs between the env's ticks. It must leave every
 * trajectory as it was: gates.mk's digest-pearl-walk holds the pearl night start's digests with it (a search over the
 * env's pathfinder arrays once left stale keys in the livings' table: pearlsee-oracle-t15960; that start's endermen
 * and zombies path near the player, where the default start's animals did not show it).
 *
 * For each N: the vector env made (its make timed), W group steps a group
 * unmeasured (the spawn's chunk loads are in the first hundred), then S
 * measured, each group sent again as soon as recv
 * returns it, its actions drawn as the trainer's would be (a xorshift per
 * env: the camera's bins near the centre, forward and attack often, use
 * sometimes, the rest rarely; --policy 0: the centre bins and no key, the
 * player idle). One line an N: env-steps/s (the policy's
 * steps over the measured window's wall time), the vector env's work in the
 * workers (its act and books) and the workers' whole step, both per
 * env-step, the trainer's thread waiting in recv, the resets and the
 * episodes that ended. */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "rlgui.h"
#include "vecenv.h"

#define WALK_ASK_R 12           /* --walk-ask: the asked cell's reach across from the feet (blocks, x and z) */

static int by_version(const void *a, const void *b) { return strverscmp(*(char *const *)a, *(char *const *)b); }

/* --starts: DIR's subdirectories holding a manifest.json, in version order; their count, -1 when DIR is not read */
static int list_starts(const char *dir, char ***out)
{
    DIR *d = opendir(dir);
    if (d == NULL) return -1;
    int k = 0, cap = 0;
    char **v = NULL;
    for (struct dirent *de; (de = readdir(d)) != NULL;)
    {
        char p[4096];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s/manifest.json", dir, de->d_name);
        if (de->d_name[0] == '.' || stat(p, &st) != 0) continue;
        if (k == cap) v = realloc(v, (size_t)(cap = cap ? 2 * cap : 64) * sizeof *v);
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        v[k++] = strdup(p);
    }
    closedir(d);
    qsort(v, (size_t)k, sizeof *v, by_version);
    *out = v;
    return k;
}

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

/* p in 1/1024 */
static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* --digest's chains: fnv over n bytes of each of m envs, s[e] env e's bytes, the envs' chains in one pass (DCH at
 * once: the multiply's latency hidden behind the other chains; each env's value fnv's) */
#define DCH 8
static void fnv_chains(uint64_t *h, const uint8_t *const *s, int m, size_t n)
{
    if (m == DCH)
    {
        /* the chains in registers (an array was kept in memory: a store and a load a byte) */
        const uint64_t P = 0x100000001b3ull;
        uint64_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4], h5 = h[5], h6 = h[6], h7 = h[7];
        const uint8_t *s0 = s[0], *s1 = s[1], *s2 = s[2], *s3 = s[3], *s4 = s[4], *s5 = s[5], *s6 = s[6], *s7 = s[7];
        for (size_t j = 0; j < n; ++j)
        {
            h0 = (h0 ^ s0[j]) * P;
            h1 = (h1 ^ s1[j]) * P;
            h2 = (h2 ^ s2[j]) * P;
            h3 = (h3 ^ s3[j]) * P;
            h4 = (h4 ^ s4[j]) * P;
            h5 = (h5 ^ s5[j]) * P;
            h6 = (h6 ^ s6[j]) * P;
            h7 = (h7 ^ s7[j]) * P;
        }
        h[0] = h0, h[1] = h1, h[2] = h2, h[3] = h3, h[4] = h4, h[5] = h5, h[6] = h6, h[7] = h7;
    }
    else
        for (size_t j = 0; j < n; ++j)
            for (int e = 0; e < m; ++e) h[e] = (h[e] ^ s[e][j]) * 0x100000001b3ull;
}

/* --digest: the last action's outcome hashed for the envs ids[0..k), each env's hash the one-env loop's (reward,
 * driver, kind, chosen, scalars): at 64 workers the main thread's byte-at-a-time chain was 72.5% of its cycles */
static int g_ns = NETH_VEC_NSCALAR;     /* a scalar row's floats (--late) */
static void digest_envs(uint64_t *hash, const struct neth_vec_bufs *b, const int *ids, int k)
{
    for (int c = 0; c < k; c += DCH)
    {
        int m = k - c < DCH ? k - c : DCH;
        uint64_t h[DCH];
        const uint8_t *s[DCH];
        for (int e = 0; e < m; ++e) h[e] = hash[ids[c + e]];
        for (int e = 0; e < m; ++e) s[e] = (const uint8_t *)&b->rewards[ids[c + e]];
        fnv_chains(h, s, m, sizeof b->rewards[0]);
        for (int e = 0; e < m; ++e) s[e] = &b->driver[ids[c + e]];
        fnv_chains(h, s, m, 1);
        for (int e = 0; e < m; ++e) s[e] = &b->kind[ids[c + e]];
        fnv_chains(h, s, m, 1);
        for (int e = 0; e < m; ++e) s[e] = &b->chosen[ids[c + e]];
        fnv_chains(h, s, m, 1);
        for (int e = 0; e < m; ++e) s[e] = (const uint8_t *)(b->scalars + (size_t)ids[c + e] * (size_t)g_ns);
        fnv_chains(h, s, m, (size_t)g_ns * sizeof(float));
        for (int e = 0; e < m; ++e) hash[ids[c + e]] = h[e];
    }
}

/* --obs-digest: FNV-1a's step over eight-byte words, then the tail's bytes */
static uint64_t fnv8(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        uint64_t w;
        memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    return fnv(h, b + i, n - i);
}

/* --picks 2: an allowed pick (1/8), else a disallowed one (1/16), else none; *bad 1 for a disallowed one */
static int draw_pick(uint64_t *s, const uint8_t *m, int *bad)
{
    int ok[NETH_VEC_PICKS], no[NETH_VEC_PICKS], nok = 0, nno = 0;
    for (int k = 1; k < NETH_VEC_PICKS; ++k)
        if (m[k]) ok[nok++] = k;
        else no[nno++] = k;
    *bad = 0;
    if (nok && coin(s, 128)) return ok[xs(s) % (uint64_t)nok];
    if (nno && coin(s, 64))
    {
        *bad = 1;
        return no[xs(s) % (uint64_t)nno];
    }
    return 0;
}

static void draw(uint64_t *s, int32_t *a, int policy)
{
    if (policy == 0)
    {
        /* idle: the centre bins, no key */
        memset(a, 0, NETH_VEC_ACTION * sizeof *a);
        a[0] = a[1] = NETH_VEC_CAM / 2;
        return;
    }
    /* the camera: the centre bin half the time, else within 4 of it */
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

int main(int argc, char **argv)
{
    const char *config = "configs/train-fast.yaml";
    char ns_arg[256] = "256,512";
    int groups = 4, threads = 0, warm = 100, steps = 300, ep_len = 3600, policy = 1;
    int macros = NETH_VEC_ALL, picks = 0, digest = 0, odigest = 0, window = 0, stride = 0;
    const char *starts_dir = NULL;
    /* --dump-cast N:PATH (lane/castexact): group 0's N-th cast as ntloop's (rcdev_dump, rcbench's input), under
     * engine.raycast device, at any observation size (ntloop's nets have two) */
    int dump_at = -1;
    const char *dump_path = NULL;
    uint64_t seed = 1;
    const char *mopts = NULL;
    int late = 0, level = 0, labeller = 4096, ldigest = 0, dev = 0, lwalk = 0, walk_ask = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--walk-ask"))
        {
            walk_ask = 1;
            continue;
        }
        if (!strcmp(argv[i], "--level-walk"))
        {
            lwalk = 1;
            continue;
        }
        if (!strcmp(argv[i], "--mopts") && i + 1 < argc)
        {
            mopts = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--late") && i + 1 < argc)
        {
            late = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--level") && i + 1 < argc)
        {
            level = atoi(argv[++i]);
            const char *colon = strchr(argv[i], ':');
            if (colon) labeller = atoi(colon + 1);
            continue;
        }
        if (!strcmp(argv[i], "--label-digest"))
        {
            ldigest = 1;
            continue;
        }
        if (!strcmp(argv[i], "--dev"))
        {
            dev = 1;
            continue;
        }
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) snprintf(ns_arg, sizeof ns_arg, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warm") && i + 1 < argc) warm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ep-len") && i + 1 < argc) ep_len = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--picks") && i + 1 < argc) picks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--digest")) digest = 1;
        else if (!strcmp(argv[i], "--obs-digest")) odigest = 1;
        else if (!strcmp(argv[i], "--starts") && i + 1 < argc) starts_dir = argv[++i];
        else if (!strcmp(argv[i], "--dump-cast") && i + 1 < argc && strchr(argv[i + 1], ':'))
            dump_at = atoi(argv[++i]), dump_path = strchr(argv[i], ':') + 1;
        else if (!strcmp(argv[i], "--window") && i + 1 < argc)
        {
            window = atoi(argv[++i]);
            const char *colon = strchr(argv[i], ':');
            stride = colon ? atoi(colon + 1) : 0;
        }
        else
        {
            fprintf(stderr, "usage: vec_bench [--config F] [--ns 256,512] [--groups G] [--threads T] [--warm W] "
                            "[--steps S] [--ep-len L] [--seed K] [--policy 0|1] [--macros BITS] [--picks 0|1|2] "
                            "[--digest] [--obs-digest] [--starts DIR [--window W[:S]]] [--mopts SPEC] [--late BITS] "
                            "[--level BITS[:LABELLER] [--level-walk]] [--label-digest]\n");
            return 2;
        }
    }
    char **sdirs = NULL;
    int nsd = 0;
    if (starts_dir && (nsd = list_starts(starts_dir, &sdirs)) <= 0)
    {
        printf("FAIL --starts %s: no start directories\n", starts_dir);
        return 1;
    }
    int fail = 0;
    for (char *tok = strtok(ns_arg, ","); tok; tok = strtok(NULL, ","))
    {
        int n = atoi(tok);
        struct neth_vec_opts o;
        memset(&o, 0, sizeof o);
        o.env.config = config;
        o.env.n = n;
        o.env.threads = threads;
        o.env.device_mesh = -1;
        o.groups = groups;
        o.ep_len = ep_len;
        o.macros = macros;
        o.picks = picks > 0;
        o.objective = NETH_VEC_FULL;
        o.macro_opts = mopts;
        if (dev) o.flags |= NETH_RL_DEV;
        o.late_scalars = late;
        g_ns = NETH_VEC_NSCALAR + ((late & 1) ? NETH_VEC_LATE_N : 0) + ((late & 2) ? NETH_VEC_HSEE_N : 0) +
               ((late & 4) ? NETH_VEC_DIG_N : 0) + ((late & 8) ? NETH_VEC_ORE_N : 0) + ((late & 16) ? NETH_VEC_GV_N : 0);
        if (starts_dir)
        {
            o.starts = (const char *const *)sdirs;
            o.nstarts = nsd;
            o.start_window = window;
            o.start_stride = stride;
            o.flags |= NETH_RL_ANYSEED;
        }
        char err[1024];
        double t0 = now_s();
        struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
        if (v == NULL)
        {
            printf("FAIL N %d: %s\n", n, err);
            fail = 1;
            continue;
        }
        double t_make = now_s() - t0;
        if (level)
        {
            if (neth_vec_level(v, level, labeller, err, sizeof err))
            {
                printf("FAIL --level %d:%d: %s\n", level, labeller, err);
                return 1;
            }
            for (int i = 1; i < n; i += 2) neth_vec_env_level(v, i, 1);
            if (lwalk && neth_vec_level_walk(v, 1))
            {
                printf("FAIL --level-walk: neth_vec_level_walk refused (the labeller must be the surface's: :1)\n");
                return 1;
            }
        }
        int aw = neth_vec_action_width(v);
        struct neth_vec_bufs b;
        memset(&b, 0, sizeof b);
        neth_vec_buffers(v, &b);
        /* --label-digest: each env's hash over its labels and acts, and over its finished episodes */
        uint64_t *lhash = malloc((size_t)n * sizeof *lhash), *ehash = malloc((size_t)n * sizeof *ehash);
        for (int i = 0; i < n; ++i) lhash[i] = ehash[i] = 0xcbf29ce484222325ull;
        static struct neth_vec_episode lep[65536];
        uint64_t *rng = malloc((size_t)n * sizeof *rng);
        for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
        int gs = n / groups;
        double s0[8], s1[8], tw = 0;
        int64_t sent = 0;
        /* --digest's hashes and --picks' books: each env's last pick and whether it was disallowed */
        uint64_t *hash = malloc((size_t)n * sizeof *hash);
        int *dids = malloc((size_t)n * sizeof *dids);
        int32_t *lpick = calloc((size_t)n, sizeof *lpick), *wcount = calloc((size_t)n, sizeof *wcount);
        uint8_t *lbad = calloc((size_t)n, 1), *seen = calloc((size_t)n, 1);
        for (int i = 0; i < n; ++i) hash[i] = 0xcbf29ce484222325ull;
        /* --obs-digest: each env's hash over its observations, and an env's observation bytes */
        uint64_t *ohash = malloc((size_t)n * sizeof *ohash);
        for (int i = 0; i < n; ++i) ohash[i] = 0xcbf29ce484222325ull;
        size_t obs_env = b.obs ? (size_t)b.obs_shape[1] * (size_t)b.obs_shape[2] * (size_t)b.obs_shape[3] *
                                     (size_t)b.obs_bytes : 0;
        int ohost = odigest && b.obs && !b.obs_device;
        int64_t p_ok = 0, p_bad = 0, start_ok = 0, start_bad = 0;
        /* --digest and --picks: every group exactly warm + steps receives, a group done not sent again (which group
         * recv returns depends on the workers' timing, so a total count alone gives each env its own number of steps
         * and the digest of a different trajectory prefix every run) */
        int quota = digest || odigest || picks, live = groups;
        int *gcount = calloc((size_t)groups, sizeof *gcount);
        /* --starts: the episodes by start (drained at each receive, the ring holds a few rounds), the most loaded */
        int64_t *by_start = calloc((size_t)nsd + 2, sizeof *by_start);
        static struct neth_vec_episode sep[65536];
        double most_loaded = 0;
        for (int it = 0; quota ? live > 0 : it < (warm + steps) * groups; ++it)
        {
            if (it == warm * groups)
            {
                neth_vec_stats(v, s0);
                tw = now_s();
            }
            int g = neth_vec_recv(v, err, sizeof err);
            if (g < 0)
            {
                printf("FAIL N %d: %s\n", n, err);
                fail = 1;
                break;
            }
            if (starts_dir)
            {
                double w[4];
                neth_vec_window(v, w);
                most_loaded = w[2] > most_loaded ? w[2] : most_loaded;
                int ne = neth_vec_episodes(v, sep, 65536);
                for (int k = 0; k < ne; ++k)
                    if (sep[k].start >= 0 && sep[k].start <= nsd + 1) ++by_start[sep[k].start];
            }
            if (ldigest)
            {
                for (int i = g * gs; i < (g + 1) * gs; ++i)
                    if (seen[i])
                    {
                        if (b.label) lhash[i] = fnv(lhash[i], &b.label[i], sizeof b.label[i]);
                        const struct neth_rl_act *ac = &b.acts[i];
                        lhash[i] = fnv(lhash[i], &ac->hold, sizeof ac->hold);
                        lhash[i] = fnv(lhash[i], ac->press, sizeof ac->press);
                        lhash[i] = fnv(lhash[i], &ac->hotbar, sizeof ac->hotbar);
                        lhash[i] = fnv(lhash[i], &ac->look_mode, sizeof ac->look_mode);
                        lhash[i] = fnv(lhash[i], ac->look, sizeof ac->look);
                        lhash[i] = fnv(lhash[i], &ac->nops, sizeof ac->nops);
                        for (int k = 0; k < ac->nops && k < NETH_RL_OPS; ++k)
                            lhash[i] = fnv(lhash[i], &ac->ops[k], sizeof ac->ops[k]);
                    }
                if (!starts_dir)
                {
                    int ne = neth_vec_episodes(v, lep, 65536);
                    for (int k = 0; k < ne; ++k)
                    {
                        const struct neth_vec_episode *q = &lep[k];
                        if (q->env < 0 || q->env >= n) continue;
                        uint64_t *h = &ehash[q->env];
                        *h = fnv(*h, &q->ms, sizeof q->ms);
                        *h = fnv(*h, &q->deaths, sizeof q->deaths);
                        *h = fnv(*h, &q->crafted, sizeof q->crafted);
                        *h = fnv(*h, q->macro, sizeof q->macro);
                        *h = fnv(*h, &q->steps, sizeof q->steps);
                        *h = fnv(*h, q->items, sizeof q->items);
                        *h = fnv(*h, q->extra, sizeof q->extra);
                        *h = fnv(*h, &q->dims, sizeof q->dims);
                        *h = fnv(*h, &q->obsidian, sizeof q->obsidian);
                        *h = fnv(*h, &q->kills, sizeof q->kills);
                        *h = fnv(*h, &q->cells, sizeof q->cells);
                        *h = fnv(*h, &q->food, sizeof q->food);
                        *h = fnv(*h, &q->ret, sizeof q->ret);
                    }
                }
            }
            /* the last action's outcome (from the second recv on: the first follows the make's reset step) */
            int nd = 0;
            for (int i = g * gs; digest && i < (g + 1) * gs; ++i)
                if (seen[i]) dids[nd++] = i;
            digest_envs(hash, &b, dids, nd);
            for (int i = g * gs; i < (g + 1) * gs; ++i)
            {
                if (ohost) ohash[i] = fnv8(ohash[i], (const uint8_t *)b.obs + (size_t)i * obs_env, obs_env);
                if (seen[i] && picks && lpick[i] > 0 && b.kind[i] == 5 && b.chosen[i])
                    lbad[i] ? ++start_bad : ++start_ok;     /* 5: vecenv.c's M_CRAFT */
                seen[i] = 1;
            }
            if (dump_path && g == 0 && dump_at-- == 0)
            {
                /* the group's cast launched on the per-thread stream (rcdev.h RCDEV_THREAD_STREAM), then dumped */
                if (neth_vec_cast(v, 0, (void *)0x2) || neth_vec_raycast_dump(v, 0, dump_path)) printf("FAIL --dump-cast %s\n", dump_path);
                else printf("--dump-cast: group 0's cast to %s\n", dump_path);
            }
            if (quota && ++gcount[g] >= warm + steps)
            {
                --live;
                continue;
            }
            for (int i = g * gs; i < (g + 1) * gs; ++i)
            {
                draw(&rng[i], b.actions + (size_t)i * (size_t)aw, policy);
                if (aw > NETH_VEC_ACTION) b.actions[(size_t)i * (size_t)aw + NETH_VEC_ACTION] = 0;
                if (picks)
                {
                    int bad = 0;
                    b.picks[i] = picks == 2 ? draw_pick(&rng[i], b.mask + (size_t)i * NETH_VEC_PICKS, &bad) : 0;
                    lpick[i] = b.picks[i];
                    lbad[i] = (uint8_t)bad;
                    p_ok += b.picks[i] > 0 && !bad;
                    p_bad += bad;
                }
                /* --walk-ask: a path asked every step, to a cell up to WALK_ASK_R across from the feet (the answer
                 * unread): the search runs between the env's ticks, which it must leave as they were */
                if (walk_ask)
                {
                    uint32_t c = (uint32_t)wcount[i]++;
                    int k = (int)((c + (uint32_t)i * 7u) % (2u * WALK_ASK_R + 1u));
                    int k2 = (int)((c * 3u + (uint32_t)i) % (2u * WALK_ASK_R + 1u));
                    neth_rl_walkto_ask_rel(neth_vec_env(v), i, k - WALK_ASK_R, 0, k2 - WALK_ASK_R, 2.0f * WALK_ASK_R);
                }
            }
            if (neth_vec_send(v, g))
            {
                printf("FAIL N %d: send %d refused\n", n, g);
                fail = 1;
                break;
            }
            sent += gs;
        }
        double wall = now_s() - tw;
        neth_vec_stats(v, s1);
        double es = s1[0] - s0[0];
        printf("N %d, %d groups: %.0f env-steps/s (%.0f env-steps in %.2f s); the vector env's C %.4f ms an env-step, "
               "the workers' step %.4f ms; recv waited %.0f%% of the window; %.0f resets, %.0f episodes; made in %.2f s\n",
               n, groups, es / wall, es, wall, (s1[2] - s0[2]) * 1e-6 / (es > 0 ? es : 1),
               (s1[7] - s0[7]) * 1e-6 / (es + s1[1] - s0[1] > 0 ? es + s1[1] - s0[1] : 1),
               100.0 * (s1[3] - s0[3]) * 1e-9 / wall, s1[1] - s0[1], s1[5] - s0[5], t_make);
        if (digest)
        {
            uint64_t d = 0;
            for (int i = 0; i < n; ++i) d += hash[i];
            printf("trajectory digest %016llx\n", (unsigned long long)d);
        }
        if (ldigest)
        {
            uint64_t dl = 0, de = 0, db = 0xcbf29ce484222325ull;
            for (int i = 0; i < n; ++i) dl += lhash[i], de += ehash[i];
            static int64_t bk[NETH_VEC_GUI_BOOKS + NETH_VEC_SHC_BOOKS + NETH_VEC_SHC_PLANS];
            memset(bk, 0, sizeof bk);
            neth_vec_gui_books(v, bk);
            neth_vec_shc_books(v, bk + NETH_VEC_GUI_BOOKS);
            neth_vec_shc_plans(v, bk + NETH_VEC_GUI_BOOKS + NETH_VEC_SHC_BOOKS);
            db = fnv(db, bk, sizeof bk);
            printf("label digest %016llx\nepisode digest %016llx%s\nbooks digest %016llx\n", (unsigned long long)dl,
                   (unsigned long long)de, starts_dir ? " (not with --starts)" : "", (unsigned long long)db);
        }
        free(lhash);
        free(ehash);
        if (odigest && !ohost)
            printf("observation digest: none (%s)\n", b.obs ? "the observation is on the device" : "no observation");
        else if (odigest)
        {
            uint64_t d = 0;
            for (int i = 0; i < n; ++i) d += ohash[i];
            printf("observation digest %016llx (%zu bytes an env a step)\n", (unsigned long long)d, obs_env);
        }
        if (starts_dir)
        {
            double w[4];
            neth_vec_window(v, w);
            int played = 0;
            int64_t lo = -1, hi = 0, tot = 0;
            for (int k = 1; k <= nsd; ++k)
            {
                played += by_start[k] > 0;
                lo = lo < 0 || by_start[k] < lo ? by_start[k] : lo;
                hi = by_start[k] > hi ? by_start[k] : hi;
                tot += by_start[k];
            }
            printf("starts: %d of the mix (window %d, stride %d): %.0f loads, %.0f frees, at most %.0f loaded, round "
                   "%.0f; episodes: %lld from the config's start, %lld from the mix's (%d starts played, %lld to %lld "
                   "each), %lld from saved ones\n",
                   nsd, window, stride, w[0], w[1], most_loaded, w[3], (long long)by_start[0], (long long)tot, played,
                   (long long)lo, (long long)hi, (long long)by_start[nsd + 1]);
        }
        free(by_start);
        if (picks)
        {
            static struct neth_vec_episode ep[65536];
            int ne = neth_vec_episodes(v, ep, 65536);
            int64_t crafted = 0, wpick = 0, spick = 0;
            for (int k = 0; k < ne; ++k)
            {
                crafted += ep[k].crafted;
                wpick += (ep[k].ms >> 0) & 1;    /* reward.MILESTONES: 270 the wooden pickaxe, 274 the stone */
                spick += (ep[k].ms >> 1) & 1;
            }
            printf("picks %d: %lld allowed and %lld disallowed sent; the craft macro started by %lld allowed and %lld "
                   "disallowed; %d episodes, %lld items crafted, %lld wooden and %lld stone pickaxes\n",
                   picks, (long long)p_ok, (long long)p_bad, (long long)start_ok, (long long)start_bad, ne,
                   (long long)crafted, (long long)wpick, (long long)spick);
        }
        fflush(stdout);
        free(rng);
        free(hash);
        free(dids);
        free(ohash);
        free(lpick);
        free(wcount);
        free(lbad);
        free(seen);
        free(gcount);
        neth_vec_free(v);
    }
    return fail;
}
