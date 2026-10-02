/* Two environments in one process, interleaved tick by tick: the whole-server
 * replay of test_snapshots.c runs for two recordings on two threads, each with
 * its own struct env (env.h), and a baton lets exactly one of them run at a
 * time, handing over before every replayed row. Both must replay exact, as
 * each does alone: any state the tick keeps outside its environment (a
 * global, a function static, a table written after startup) is shared by the
 * two and diverges one of them.
 *
 * DIR holds manifest.json with the two recordings' names, "a" and "b", which
 * live in the snapshots area beside this one (DIR/../../snapshots/NAME). The
 * loads run one after the other (a holds the baton first); the rows alternate
 * a, b, a, b until one tape ends, and the other finishes alone.
 *
 * The negative check shared-env (test_interleave shared-env DIR) runs both on
 * the process's first environment: the replays must diverge.
 *
 * The phase tracer's flags pass through to both replays (phase.h):
 * --phase-digest and --phase-check as they are, --phase-trace PREFIX as
 * PREFIX.NAME for each recording, so each trace can be compared with the
 * recording's own run alone. */
#undef main
#define main snapshots_main
#include "test_snapshots.c"
#undef main
#define main gate_test_main

#include <pthread.h>

#include "../engine/env.h"

struct runner {
    int index;
    int shared;                 /* the negative check: both on the process's first env */
    char dir[4096];
    const char *phase_flag;     /* --phase-digest, --phase-check or --phase-trace, or NULL */
    const char *rows;           /* --rows N: each replay stops N rows past its snapshot */
    const char *region_cache;   /* --region-cache VALUE (regioncache.h) */
    char phase_path[4200];      /* --phase-trace's file */
    int rc;
};

static pthread_mutex_t baton_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t baton_cv = PTHREAD_COND_INITIALIZER;
static int baton_turn;          /* the runner that may run */
static int baton_done[2];
static _Thread_local int baton_me;

static void baton_wait_turn(void)
{
    while (baton_turn != baton_me) pthread_cond_wait(&baton_cv, &baton_mu);
}

/* The row hook: the other runner's turn, unless it has finished. */
static void baton_pass(void)
{
    pthread_mutex_lock(&baton_mu);
    if (!baton_done[1 - baton_me]) baton_turn = 1 - baton_me;
    pthread_cond_broadcast(&baton_cv);
    baton_wait_turn();
    pthread_mutex_unlock(&baton_mu);
}

static void *runner_main(void *arg)
{
    struct runner *r = arg;

    baton_me = r->index;
    pthread_mutex_lock(&baton_mu);
    baton_wait_turn();
    pthread_mutex_unlock(&baton_mu);

    if (!r->shared)
    {
        struct env *e = env_new();
        if (e == NULL) { r->rc = 2; return NULL; }
        nw_env = e;
    }

    char *argv[9] = {"test_snapshots"};
    int argc = 1;
    if (r->rows != NULL) { argv[argc++] = "--rows"; argv[argc++] = (char *)r->rows; }
    if (r->region_cache != NULL) { argv[argc++] = "--region-cache"; argv[argc++] = (char *)r->region_cache; }
    if (r->phase_flag != NULL) argv[argc++] = (char *)r->phase_flag;
    if (r->phase_path[0]) argv[argc++] = r->phase_path;
    argv[argc++] = r->dir;
    argv[argc] = NULL;
    r->rc = snapshots_main(argc, argv);

    pthread_mutex_lock(&baton_mu);
    baton_done[r->index] = 1;
    baton_turn = 1 - r->index;
    pthread_cond_broadcast(&baton_cv);
    pthread_mutex_unlock(&baton_mu);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_interleave [shared-env] [--rows N] [--region-cache VALUE] [--phase-digest | --phase-check | --phase-trace PREFIX] DIR\n");
        return 2;
    }

    /* two environments on two threads: no grave (grave.h grave_forbid) */
    grave_forbid();
    const char *dir = argv[argc - 1];
    int shared = argc > 2 && !strcmp(argv[1], "shared-env");
    const char *phase_flag = NULL, *phase_prefix = NULL, *rows = NULL, *region_cache = NULL;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (!strcmp(argv[i], "--phase-digest") || !strcmp(argv[i], "--phase-check")) phase_flag = argv[i];
        else if (!strcmp(argv[i], "--phase-trace") && i + 1 < argc - 1) { phase_flag = argv[i]; phase_prefix = argv[++i]; }
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc - 1) rows = argv[++i]; /* each replay's first N rows (make smoke) */
        else if (!strcmp(argv[i], "--region-cache") && i + 1 < argc - 1) region_cache = argv[++i];
        else if (strcmp(argv[i], "shared-env")) { printf("FAIL unknown flag %s\n", argv[i]); return 2; }
    }
    char path[4200];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL %s: no manifest.json\n", dir); return 1; }
    static char text[65536];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const struct jval *m = json_parse(text);
    const char *names[2] = {json_str(json_get(m, "a")), json_str(json_get(m, "b"))};
    if (names[0] == NULL || names[1] == NULL) { printf("FAIL %s: the manifest names no a and b\n", dir); return 1; }

    static struct runner runners[2];
    char real[4096];
    if (realpath(dir, real) == NULL) { printf("FAIL %s: does not resolve\n", dir); return 1; }
    for (int i = 0; i < 2; ++i)
    {
        runners[i].index = i;
        runners[i].shared = shared;
        runners[i].phase_flag = phase_flag;
        runners[i].rows = rows;
        runners[i].region_cache = region_cache;
        if (phase_prefix != NULL) snprintf(runners[i].phase_path, sizeof runners[i].phase_path, "%.3000s.%.900s", phase_prefix, names[i]);
        snprintf(runners[i].dir, sizeof runners[i].dir, "%.3000s/../../snapshots/%.900s", real, names[i]);
    }

    /* each replay keeps its snapshot and server in thread-local storage and
     * recurses deep in places: a large stack */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)256 << 20);
    /* no guard pages: glibc marks them inside the stack mapping (a guard
     * region /proc/self/maps does not show), and the grave's sweep reads
     * every writable anonymous mapping (grave.h) */
    pthread_attr_setguardsize(&attr, 0);
    snapshots_row_hook = baton_pass;
    baton_turn = 0;

    pthread_t th[2];
    for (int i = 0; i < 2; ++i)
        if (pthread_create(&th[i], &attr, runner_main, &runners[i]) != 0)
        {
            printf("FAIL %s: cannot start runner %d\n", dir, i);
            return 1;
        }
    for (int i = 0; i < 2; ++i) pthread_join(th[i], NULL);

    int ok = runners[0].rc == 0 && runners[1].rc == 0;
    printf("%s: %s (%s rc %d, %s rc %d, interleaved row by row in one process)\n", dir,
           ok ? "OK: both replays exact" : "FAIL", names[0], runners[0].rc, names[1], runners[1].rc);
    return ok ? 0 : 1;
}
