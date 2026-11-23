/* vecplay (lane/divlib, vecenv.mk's rl-play-gate): an oracle script's acts played by the vector env from a start, every
 * tick's row record written (rlgui.h neth_rl_rows: test_snapshots --row-dump's records), so the vector env's episode can
 * be held row for row to the pool's plain replay of the same script, or to the Java client's recording of it.
 *
 *   vecplay --config RUN (--script FILE | --random SEED --acts N) --rows OUT [--start DIR] [--pool] [--from K]
 *           [--acts N] [--threads T]
 *
 * --random SEED: N acts drawn from SEED (keys, small look turns, the hotbar; random_acts) in place of a script's.
 * --pool: the same acts through the pool alone (pool_make, pool_reset to --start, a no-act step for the vector env's
 * reset step, then the acts; the rows from the pool's tick hook), the vector env's reference: its rows must equal the
 * vector env's, row for row (rl-play-gate). d.cw is "-" on both sides (no tape to know the client world's Random by).
 *
 * One env, macros off, the chain's hooks (neth_vec_opts.chain): its first episode is ended at once, and its second
 * (from the config's start, or --start DIR) plays the script's acts from the K-th on (an act a world tick: the lines
 * after the Snapshot command, {"n":R,"act":{...}} repeated R times), N of them (default all), then ends. Rows: only the
 * second episode's, from its reset step (the tick with no act a Java replay of the script has as a leading {} act).
 * Prints one line: the acts played and the rows written. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool.h"
#include "rlgui.h"
#include "rlgui_int.h"   /* rlgui_row: the row record neth_rl_rows writes, the library's own */
#include "vecenv.h"

static const char *const kn[NETH_RL_KEYS] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "attack",
                                          "use", "drop", "inventory", "pick", "hotbar.1", "hotbar.2", "hotbar.3",
                                          "hotbar.4", "hotbar.5", "hotbar.6", "hotbar.7", "hotbar.8", "hotbar.9"};

/* the script's acts from the from-th on (lane/chain3's --replay-script parser): count, or -1 */
static int64_t read_acts(const char *path, int64_t from, int64_t max, struct neth_rl_act **out)
{
    FILE *f = fopen(path, "r");
    if (!f) return fprintf(stderr, "vecplay: %s: %s\n", path, strerror(errno)), -1;
    int64_t k = 0, n = 0, cap = 1 << 16;
    struct neth_rl_act *acts = calloc((size_t)cap, sizeof *acts);
    char *line = NULL;
    size_t lcap = 0;
    while (getline(&line, &lcap, f) > 0 && (max < 0 || n < max))
    {
        if (strncmp(line, "{\"n\":", 5)) continue;   /* (the Snapshot command) */
        int reps = atoi(line + 5);
        struct neth_rl_act a;
        memset(&a, 0, sizeof a);
        a.hotbar = -1;
        const char *p;
        if ((p = strstr(line, "\"look\":[")) || (p = strstr(line, "\"dlook\":[")))
        {
            a.look_mode = p[1] == 'l' ? 1 : 2;
            p = strchr(p, '[') + 1;
            char *q;
            a.look[0] = strtof(p, &q);
            a.look[1] = strtof(q + 1, NULL);
        }
        for (int w = 0; w < 2; ++w)
            if ((p = strstr(line, w ? "\"press\":[" : "\"hold\":[")))
            {
                const char *end = strchr(p, ']');
                for (p = strchr(p, '['); p && p < end && (p = strchr(p, '"')) && p < end;)
                {
                    const char *q = strchr(p + 1, '"');
                    int key = -1;
                    for (int j = 0; j < NETH_RL_KEYS; ++j)
                        if ((size_t)(q - p - 1) == strlen(kn[j]) && !strncmp(p + 1, kn[j], (size_t)(q - p - 1))) key = j;
                    if (key < 0) return fprintf(stderr, "vecplay: key %.*s\n", (int)(q - p - 1), p + 1), -1;
                    if (w) a.press[key]++;
                    else a.hold |= 1u << key;
                    p = q + 1;
                }
            }
        if ((p = strstr(line, "\"hotbar\":"))) a.hotbar = atoi(p + 9);
        if ((p = strstr(line, "\"gui\":[")))
        {
            p += 7;
            while (*p == '[' && a.nops < NETH_RL_OPS)
            {
                struct neth_rl_op *o = &a.ops[a.nops++];
                int x[4] = {0, 0, 0, 0};
                if (!strncmp(p, "[\"click\",", 9)) o->kind = PG_CLICK, sscanf(p + 9, "%d,%d,%d,%d", &x[0], &x[1], &x[2], &x[3]);
                else if (!strncmp(p, "[\"close\"", 8)) o->kind = PG_CLOSE;
                else if (!strncmp(p, "[\"respawn\"", 10)) o->kind = PG_RESPAWN;
                else if (!strncmp(p, "[\"wake\"", 7)) o->kind = PG_WAKE;
                else return fprintf(stderr, "vecplay: gui op %.20s\n", p), -1;
                o->window = x[0], o->slot = x[1], o->button = x[2], o->mode = x[3];
                p = strchr(p, ']') + 1;
                if (*p == ',') ++p;
            }
        }
        if (strstr(line, "\"ctrl\":") || strstr(line, "\"aim\""))
            return fprintf(stderr, "vecplay: an act field it does not take\n"), -1;
        for (int r = 0; r < (reps > 0 ? reps : 1) && (max < 0 || n < max); ++r, ++k)
        {
            if (k < from) continue;
            if (n == cap) acts = realloc(acts, (size_t)(cap *= 2) * sizeof *acts);
            acts[n++] = a;
        }
    }
    free(line);
    fclose(f);
    *out = acts;
    return n;
}

/* --random SEED: n acts a player could make (keys held and pressed, the look turned a little, the hotbar now and then;
 * no screen key, no drop, no GUI op), from a xorshift seeded by SEED: the same acts for any build */
static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static struct neth_rl_act *random_acts(uint64_t seed, int64_t n)
{
    struct neth_rl_act *acts = calloc((size_t)n, sizeof *acts);
    uint64_t s = seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull;
    if (s == 0) s = 1;
    /* per mille a tick: forward back left right jump sneak sprint attack use (held); attack, use (pressed) */
    static const int hold_p[9] = {600, 60, 120, 120, 150, 40, 300, 350, 60};
    for (int64_t i = 0; i < n; ++i)
    {
        struct neth_rl_act *a = &acts[i];
        a->hotbar = -1;
        for (int k = 0; k < 9; ++k)
            if ((int)(xs(&s) % 1000) < hold_p[k]) a->hold |= 1u << k;
        if ((int)(xs(&s) % 1000) < 80) a->press[7] = 1;
        if ((int)(xs(&s) % 1000) < 20) a->press[8] = 1;
        a->look_mode = 2;
        a->look[0] = (float)((int)(xs(&s) % 2001) - 1000) / 200.0f;
        a->look[1] = (float)((int)(xs(&s) % 2001) - 1000) / 400.0f;
        if ((int)(xs(&s) % 1000) < 5) a->hotbar = (int)(xs(&s) % 9);
    }
    return acts;
}

/* --pool: the pool's plain replay of the same acts (no vector env): a reset to the start, the reset step's no act, then
 * the acts, each tick's row record from the tick hook (the worker's) */
static FILE *pool_rows;
static void pool_row_hook(void *ctx, int id, struct session *ss, int64_t t, int last)
{
    (void)ctx, (void)id, (void)last;
    if (pool_rows != NULL) rlgui_row(pool_rows, ss, t, 0);
}

static int pool_play(const char *config, const char *start, const struct neth_rl_act *acts, int64_t n, const char *rows,
                     int threads)
{
    struct pool_config cfg = {0};
    cfg.n = 1;
    cfg.threads = threads;
    cfg.obs.ticks = 1;
    cfg.config = config;
    cfg.tick_hook = pool_row_hook;
    cfg.vanilla_only = 1;
    char err[512];
    struct pool *p = pool_make(&cfg, err, sizeof err);
    if (p == NULL) return printf("FAIL pool_make: %s\n", err), 1;
    struct pool_start *s = pool_start_load(p, start, err, sizeof err);
    if (s == NULL) return printf("FAIL start: %s\n", err), 1;
    pool_rows = fopen(rows, "w");
    if (!pool_rows) return printf("FAIL: rows %s not opened\n", rows), 1;
    int one = 0;
    if (pool_reset(p, &one, 1, s, err, sizeof err)) return printf("FAIL reset: %s\n", err), 1;
    const struct pool_result *res[1];
    int64_t steps = 0;
    for (int64_t i = -1; i < n; ++i)
    {
        struct neth_rl_act noop;
        memset(&noop, 0, sizeof noop);
        noop.hotbar = -1;
        const struct neth_rl_act *src = i < 0 ? &noop : &acts[i];
        struct pool_act a;
        memset(&a, 0, sizeof a);
        a.kind = PA_AGENT;
        a.hold = src->hold;
        memcpy(a.press, src->press, sizeof a.press);
        a.look_mode = src->look_mode == 1 ? PL_LOOK : src->look_mode == 2 ? PL_DLOOK : PL_NONE;
        a.look[0] = src->look[0];
        a.look[1] = src->look[1];
        a.hotbar = src->hotbar;
        a.nops = src->nops < 0 ? 0 : src->nops > POOL_GUI_OPS ? POOL_GUI_OPS : src->nops;
        for (int j = 0; j < a.nops; ++j)
        {
            a.ops[j].kind = src->ops[j].kind;
            a.ops[j].window = src->ops[j].window;
            a.ops[j].slot = src->ops[j].slot;
            a.ops[j].button = src->ops[j].button;
            a.ops[j].mode = src->ops[j].mode;
            a.ops[j].index = src->ops[j].index;
        }
        if (pool_step(p, &one, 1, &a, 1, 0)) return printf("FAIL step %lld\n", (long long)i), 1;
        if (pool_poll(p, 1, res) != 1) return printf("FAIL poll %lld\n", (long long)i), 1;
        if (i >= 0) ++steps;
    }
    fclose(pool_rows);
    pool_start_free(s);
    pool_free(p);
    printf("vecplay --pool: %lld of %lld acts played, %lld rows\n", (long long)steps, (long long)n, (long long)n + 1);
    return steps == n ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *config = NULL, *script = NULL, *rows = NULL, *start = NULL;
    int64_t from = 0, max = -1;
    int threads = 1, pool = 0, random = 0;
    uint64_t rseed = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        int v = i + 1 < argc;
        if (!strcmp(a, "--config") && v) config = argv[++i];
        else if (!strcmp(a, "--script") && v) script = argv[++i];
        else if (!strcmp(a, "--rows") && v) rows = argv[++i];
        else if (!strcmp(a, "--start") && v) start = argv[++i];
        else if (!strcmp(a, "--from") && v) from = atoll(argv[++i]);
        else if (!strcmp(a, "--acts") && v) max = atoll(argv[++i]);
        else if (!strcmp(a, "--threads") && v) threads = atoi(argv[++i]);
        else if (!strcmp(a, "--random") && v) random = 1, rseed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--pool")) pool = 1;
        else return fprintf(stderr, "vecplay: unknown argument %s\n", a), 2;
    }
    if (!config || (!script && !random) || (script && random) || !rows || (pool && !start) || (random && max <= 0))
        return fprintf(stderr, "usage: vecplay --config RUN (--script FILE | --random SEED --acts N) --rows OUT [--start DIR]"
                               " [--pool] [--from K] [--acts N]\n"), 2;
    struct neth_rl_act *acts = NULL;
    int64_t n = random ? max : read_acts(script, from, max, &acts);
    if (n < 0) return 1;
    if (random) acts = random_acts(rseed, n);
    if (pool)
    {
        int rc = pool_play(config, start, acts, n, rows, threads);
        free(acts);
        return rc;
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.env.n = 1;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.env.region_spill = "off";
    o.groups = 1;
    o.ep_len = n + 2;
    o.macros = 0;
    o.objective = NETH_VEC_FULL;
    o.chain = 1;
    char err[1024];
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL) return printf("FAIL make: %s\n", err), 1;
    struct neth_vec_bufs b;
    neth_vec_buffers(v, &b);
    memset(b.actions, 0, (size_t)neth_vec_action_width(v) * sizeof *b.actions);
    /* recv 1: the make's episode's reset step ran. Its next step ends it; the recv after has the second one's reset */
    int g = neth_vec_recv(v, err, sizeof err);
    if (g < 0) return printf("FAIL recv: %s\n", err), 1;
    neth_vec_chain_next(v, 0, -1, n + 2);
    neth_vec_chain_end(v, 0);
    char tmp[4200];
    snprintf(tmp, sizeof tmp, "%s.all", rows);
    if (neth_rl_rows(neth_vec_env(v), 0, tmp, 0)) return printf("FAIL: rows %s not opened\n", tmp), 1;
    if (neth_vec_send(v, 0)) return printf("FAIL send\n"), 1;
    g = neth_vec_recv(v, err, sizeof err);
    if (g < 0) return printf("FAIL recv: %s\n", err), 1;
    struct neth_vec_episode ep[4];
    int ended = neth_vec_episodes(v, ep, 4);
    if (ended != 1) return printf("FAIL: the first episode did not end (%d)\n", ended), 1;
    neth_vec_chain_play(v, 0, acts, n);
    int64_t steps = 0;
    for (; steps < n; ++steps)
    {
        if (neth_vec_send(v, 0)) return printf("FAIL send at %lld\n", (long long)steps), 1;
        if (neth_vec_recv(v, err, sizeof err) < 0) return printf("FAIL recv: %s\n", err), 1;
        if (neth_vec_episodes(v, ep, 4) > 0 && steps + 1 < n) break;
    }
    neth_rl_rows(neth_vec_env(v), 0, NULL, 0);
    neth_vec_free(v);
    free(acts);
    /* the second episode's rows: from the second line whose t restarts (the first episode's last tick aside) */
    FILE *in = fopen(tmp, "r"), *outf = fopen(rows, "w");
    if (!in || !outf) return printf("FAIL: rows not rewritten\n"), 1;
    char *line = NULL;
    size_t lcap = 0;
    long long prev = -1, kept = 0;
    int on = 0;
    while (getline(&line, &lcap, in) > 0)
    {
        long long t = -1;
        if (!strncmp(line, "t=", 2)) t = atoll(line + 2);
        if (!on && prev >= 0 && t >= 0 && t <= prev) on = 1;
        if (t >= 0) prev = t;
        if (on) fputs(line, outf), ++kept;
    }
    free(line);
    fclose(in);
    fclose(outf);
    remove(tmp);
    printf("vecplay: %lld of %lld acts played, %lld rows\n", (long long)steps, (long long)n, kept);
    return steps == n ? 0 : 1;
}
