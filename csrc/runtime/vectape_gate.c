/* vectape_gate (lane/tape, vecenv.mk's rl-tape-gate): the vector env's own episode replayed by the Java oracle. A
 * seeded stand-in for the policy (a xorshift per env: the camera's bins near the centre, forward and attack often,
 * as vec_bench draws them) drives N envs of a config with the macros on; env 0's second episode (the first one's
 * start was queued by the make) is written as the oracle's script (neth_vec_tape) and every tick's row record of
 * env 0 from then on is kept (neth_rl_rows). The make target runs the script on the oracle from the config's world and
 * holds test_snapshots' row records of that recording to the episode's, row for row.
 *
 *   vectape_gate --config FILE --script OUT --rows OUT [--n 8] [--groups 2] [--threads 4] [--seconds 60]
 *                [--macros BITS] [--seed K] [--env E]
 *
 * --env E (lane/obsidian): env E's second episode instead of env 0's (a macro's gate picks an env the macro acts in).
 * --idle (lane/obsidian5): the stand-in holds no key and the camera's centre bins (obsidian_case --policy 0), so the
 * macros drive alone (the random keys cut the bucket portal's builds: seed 393's kit lit 1 of 32 under them, 2 of 8 idle).
 * --start DIR [--dev] (lane/obsidian3): that start instead of the config's (a dev one: its setup's Dev entries are
 * the oracle script's after its Snapshot line, written there at the end since lane/obsidian6: script_setup); with
 * the bucket portal's bit, a line of its books of the episode.
 * Prints one line: the episode's record (logs, items crafted, milestones, obsidian mined and held, water buckets
 * held), what the replay must reproduce. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rlgui.h"
#include "vecenv.h"

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static int coin(uint64_t *s, int per1024) { return (int)(xs(s) % 1024) < per1024; }

static int idle;   /* lane/obsidian5: --idle, the stand-in holds no key (the camera's centre bins): the macros alone */

static void draw(uint64_t *s, int32_t *a)
{
    if (idle)
    {
        memset(a, 0, NETH_VEC_ACTION * sizeof *a);
        a[0] = a[1] = NETH_VEC_CAM / 2;
        return;
    }
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

/* lane/obsidian6: a dev start's setup as the script's lines after its Snapshot line (the comment above said so, but
 * nothing wrote them: obsidian5 and obsidian6 inserted them by hand, and without them the oracle's first row differed).
 * The start's tape header (its first line) holds "setup":[{"class":"Dev","tick":2,"cmd":{...}}, ...]; each cmd object
 * becomes {"cmd":"dev",...} in its order. 0 done, -1 not read or not written */
static int script_setup(const char *start, const char *script)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/tape.jsonl", start);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t cap = 1 << 16, len = 0;
    char *hdr = malloc(cap);
    int ch;
    while (hdr && (ch = fgetc(f)) != EOF && ch != '\n')
    {
        if (len + 2 >= cap) hdr = realloc(hdr, cap *= 2);
        if (hdr) hdr[len++] = (char)ch;
    }
    fclose(f);
    if (!hdr) return -1;
    hdr[len] = 0;
    /* the script whole, then written again: its Snapshot line, the setup's lines, the rest */
    f = fopen(script, "r");
    if (!f) { free(hdr); return -1; }
    fseek(f, 0, SEEK_END);
    long sl = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *body = malloc((size_t)sl + 1);
    size_t got = body ? fread(body, 1, (size_t)sl, f) : 0;
    fclose(f);
    if (!body || got != (size_t)sl) { free(hdr); free(body); return -1; }
    body[sl] = 0;
    char *nl = strchr(body, '\n');
    f = fopen(script, "w");
    if (!f) { free(hdr); free(body); return -1; }
    fwrite(body, 1, nl ? (size_t)(nl - body + 1) : (size_t)sl, f);
    const char *p = strstr(hdr, "\"setup\":["), *send = NULL;
    if (p)
    {
        /* the array's end (strings skipped) */
        int depth = 0, instr = 0;
        for (send = p + 8; *send; ++send)
        {
            if (instr) { if (*send == '\\' && send[1]) ++send; else if (*send == '"') instr = 0; }
            else if (*send == '"') instr = 1;
            else if (*send == '[' || *send == '{') ++depth;
            else if ((*send == ']' || *send == '}') && --depth == 0) break;
        }
    }
    int nset = 0;
    while (p && (p = strstr(p, "\"cmd\":{")) != NULL && p < send)
    {
        const char *o = p + 7, *q = o;     /* just past the cmd object's brace */
        int depth = 1, instr = 0;
        for (; *q && depth > 0; ++q)
        {
            if (instr) { if (*q == '\\' && q[1]) ++q; else if (*q == '"') instr = 0; }
            else if (*q == '"') instr = 1;
            else if (*q == '{' || *q == '[') ++depth;
            else if (*q == '}' || *q == ']') --depth;
        }
        if (depth) break;
        fprintf(f, "{\"cmd\":\"dev\",%.*s}\n", (int)(q - 1 - o), o);
        nset += 1;
        p = q;
    }
    if (nl) fputs(nl + 1, f);
    fclose(f);
    free(hdr);
    free(body);
    return nset;
}

int main(int argc, char **argv)
{
    const char *config = NULL, *script = NULL, *rows = NULL, *start = NULL;
    int n = 8, groups = 2, threads = 4, seconds = 60, macros = NETH_VEC_ALL, env = 0, dev = 0;
    static char mopts[1024];
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        int v = i + 1 < argc;
        if (!strcmp(a, "--config") && v) config = argv[++i];
        else if (!strcmp(a, "--script") && v) script = argv[++i];
        else if (!strcmp(a, "--rows") && v) rows = argv[++i];
        else if (!strcmp(a, "--n") && v) n = atoi(argv[++i]);
        else if (!strcmp(a, "--groups") && v) groups = atoi(argv[++i]);
        else if (!strcmp(a, "--threads") && v) threads = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && v) seconds = atoi(argv[++i]);
        else if (!strcmp(a, "--macros") && v) macros = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && v) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--env") && v) env = atoi(argv[++i]);
        else if (!strcmp(a, "--start") && v) start = argv[++i];     /* lane/obsidian3: another start than the config's */
        else if (!strcmp(a, "--dev")) dev = 1;                      /* and a dev one (rlgui.h NETH_RL_DEV) */
        else if (!strcmp(a, "--idle")) idle = 1;                    /* lane/obsidian5: no key from the stand-in */
        else if (!strcmp(a, "--opt") && v)                          /* lane/rod: a macro option NAME=WORD (again for more) */
        {
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else
        {
            fprintf(stderr, "vectape_gate: unknown argument %s\n", a);
            return 2;
        }
    }
    if (!config || !script || !rows || n < groups || n % groups || env < 0 || env >= n)
    {
        fprintf(stderr, "usage: vectape_gate --config FILE --script OUT --rows OUT [--n N] [--groups G] ...\n");
        return 2;
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.flags = dev ? NETH_RL_DEV : 0;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.env.region_spill = "off";
    o.groups = groups;
    o.ep_len = seconds * 20;
    o.macros = macros;
    o.macro_opts = mopts[0] ? mopts : NULL;
    o.objective = NETH_VEC_FULL;
    char err[1024];
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL)
    {
        printf("FAIL make: %s\n", err);
        return 1;
    }
    struct neth_vec_bufs b;
    neth_vec_buffers(v, &b);
    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    int gs = n / groups, armed = 0, done = 0, rc = 0;
    struct neth_vec_episode ep[64], mine;
    memset(&mine, 0, sizeof mine);
    struct neth_vec_bportal bp, bq;     /* lane/obsidian3: the bucket portal's books of the taped episode (its last
                                           step's: the books are zeroed at the reset the episode's record comes with) */
    memset(&bp, 0, sizeof bp);
    struct neth_vec_kit kb;             /* lane/rod: the kit macros' books of the taped episode (the latest step's) */
    memset(&kb, 0, sizeof kb);
    kb.t_ore1 = kb.t_worn3 = kb.t_shovel = kb.t_snow64 = -1;
    int epi[64] = {0};
    int64_t elit[64], enether[64];
    for (int i = 0; i < 64; ++i) elit[i] = enether[i] = -1;
    while (done < 2)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL recv: %s\n", err);
            rc = 1;
            break;
        }
        if (g == env / gs && !armed)
        {
            /* env E's first step ran (the make queued it): its next episode is the one written */
            if (neth_vec_tape(v, env, script) || neth_rl_rows(neth_vec_env(v), env, rows, 0))
            {
                printf("FAIL: the tape or the rows of env %d not opened\n", env);
                rc = 1;
                break;
            }
            armed = 1;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i) draw(&rng[i], b.actions + (size_t)i * NETH_VEC_ACTION);
        if (done == 1 && g == env / gs && neth_vec_bportal(v, env, &bq) == 0 && bq.steps_on >= bp.steps_on) bp = bq;
        if (done == 1 && g == env / gs && mopts[0])     /* lane/rod: the kit macros' books of the taped episode */
        {
            struct neth_vec_kit kq;
            if (neth_vec_kit(v, env, &kq) == 0 && kq.t_ore1 + kq.t_worn3 + kq.t_shovel + kq.t_snow64 >= kb.t_ore1 +
                                                     kb.t_worn3 + kb.t_shovel + kb.t_snow64)
                kb = kq;
        }
        /* lane/obsidian4: every env's second episode, the portal lit and the Nether entered (an env to tape) */
        for (int i = g * gs; i < (g + 1) * gs && i < 64; ++i)
            if (epi[i] == 1 && neth_vec_bportal(v, i, &bq) == 0)
            {
                struct neth_vec_obsidian oq;
                if (bq.lit > 0 && elit[i] < 0) elit[i] = bq.lit_step;
                if (neth_vec_obsidian(v, i, &oq) == 0 && oq.dim == -1 && enether[i] < 0) enether[i] = oq.nether;
            }
        int k = neth_vec_episodes(v, ep, 64);
        for (int j = 0; j < k; ++j)
        {
            if (ep[j].env >= 0 && ep[j].env < 64) epi[ep[j].env] += 1;
            if (ep[j].env == env && ++done == 2) mine = ep[j];
        }
        if (done < 2 && neth_vec_send(v, g))
        {
            printf("FAIL send %d refused\n", g);
            rc = 1;
            break;
        }
    }
    neth_rl_rows(neth_vec_env(v), env, NULL, 0);
    neth_vec_free(v);
    free(rng);
    if (rc) return rc;
    if (start && dev)
    {
        int ns = script_setup(start, script);
        if (ns < 0)
        {
            printf("FAIL: the start's setup not written into %s\n", script);
            return 1;
        }
        printf("setup: %d Dev lines after the script's Snapshot line\n", ns);
    }
    if (env == 0)
        printf("episode: start %d, %lld steps, logs %d, crafted %d, deaths %d, milestones 0x%x\n", mine.start,
               (long long)mine.steps, mine.logs, mine.crafted, mine.deaths, mine.ms);
    else
        printf("episode: env %d, start %d, %lld steps, logs %d, crafted %d, deaths %d, milestones 0x%x, obsidian mined "
               "%d, held %d, water buckets %d\n", env, mine.start, (long long)mine.steps, mine.logs, mine.crafted,
               mine.deaths, mine.ms, mine.obsidian, mine.extra[2], mine.extra[0]);
    if (mopts[0])
        printf("kit: worn %d (%d points), first ore %lld, ingots %lld, three worn %lld, four worn %lld; snowballs %d, "
               "shovel %lld, 64 snowballs %lld; options %s\n", kb.worn, kb.points, (long long)kb.t_ore1,
               (long long)kb.t_ingots, (long long)kb.t_worn3, (long long)kb.t_worn4, kb.n_snow, (long long)kb.t_shovel,
               (long long)kb.t_snow64, mopts);
    if (macros & NETH_VEC_BPORTAL)
        printf("bportal: starts %d, steps driven %lld, sites %d, cells cast %d, lit %d, trips lava %d water %d, uses %d\n",
               bp.starts, (long long)bp.steps_on, bp.sites, bp.casts, bp.lit, bp.trips_lava, bp.trips_water, bp.uses);
    if (macros & NETH_VEC_BPORTAL)
        for (int i = 0; i < n && i < 64; ++i)
            if (elit[i] >= 0 || enether[i] >= 0)
                printf("bportal env %d (second episode): lit at %lld, the Nether at %lld\n", i, (long long)elit[i],
                       (long long)enether[i]);
    return 0;
}
