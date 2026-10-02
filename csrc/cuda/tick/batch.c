/* S6 of several recordings in one launch (GPU plan L17): the replays of
 * test_snapshots run on one thread each, in one process, each in its own
 * image environment, with --phase-kernel S6 --phase-kernel-cuda and its own
 * C trace to compare each row's digests with. A baton lets one replay run at
 * a time (as test_interleave.c does): it passes at every row, and a replay
 * that submitted its S6 (host.c) passes it until its result is in. The
 * last live replay to submit launches them all, a thread each (--pack), so a
 * launch holds every replay that is still running.
 *
 *   tick_batch [--stack BYTES] [--rows N] [--phases LIST] [--pack W] [--device-root] [--fn-map FILE]
 *              DIR TRACE [DIR TRACE ...]
 *
 * DIR a snapshot recording, TRACE its --phase-trace from the C build (the
 * same directory may appear several times: each is its own environment).
 * Prints each replay's own lines, then one line: OK when every replay was
 * exact and every row's digests equal its trace's. --phases runs those
 * phases on the device (S6 by default; any of S1, S2, S1T, S4, S5, S6 and
 * SG, comma separated: servertick_phase). --pack W runs W environments a
 * thread block, one a thread (host.h tick_device_pack). --device-root keeps the
 * device in the process started (host.h tick_device_root: for ncu);
 * --fn-map FILE writes the device functions' addresses (tick_device_fnmap). */
#undef main
#define main snapshots_main
#include "../../tests/test_snapshots.c"
#undef main

#include <pthread.h>

#include "host.h"

#define S6B_MAX 128

struct runner {
    int index;
    const char *dir, *trace, *rows, *stack, *phases;
    int rc;
};

static pthread_mutex_t bmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bcv = PTHREAD_COND_INITIALIZER;
static int bturn, bdone[S6B_MAX], bn;
static _Thread_local int bme;

/* the next runner still going after k, or k itself */
static int next_live(int k)
{
    for (int i = 1; i <= bn; ++i)
    {
        int j = (k + i) % bn;
        if (!bdone[j]) return j;
    }
    return k;
}

static void baton_pass(void)
{
    pthread_mutex_lock(&bmu);
    bturn = next_live(bme);
    pthread_cond_broadcast(&bcv);
    while (bturn != bme) pthread_cond_wait(&bcv, &bmu);
    pthread_mutex_unlock(&bmu);
}

static void *runner_main(void *arg)
{
    struct runner *r = arg;
    bme = r->index;
    pthread_mutex_lock(&bmu);
    while (bturn != bme) pthread_cond_wait(&bcv, &bmu);
    pthread_mutex_unlock(&bmu);

    struct env *e = env_new();
    if (e == NULL) { r->rc = 2; return NULL; }
    nw_env = e;
    char *argv[16] = {"test_snapshots"};
    int argc = 1;
    if (r->rows != NULL) { argv[argc++] = "--rows"; argv[argc++] = (char *)r->rows; }
    if (r->stack != NULL) { argv[argc++] = "--phase-kernel-stack"; argv[argc++] = (char *)r->stack; }
    argv[argc++] = "--phase-kernel";
    argv[argc++] = (char *)r->phases;
    argv[argc++] = "--phase-kernel-cuda";
    argv[argc++] = "--phase-kernel-ref";
    argv[argc++] = (char *)r->trace;
    argv[argc++] = (char *)r->dir;
    argv[argc] = NULL;
    r->rc = snapshots_main(argc, argv);
    fflush(stdout);

    pthread_mutex_lock(&bmu);
    bdone[r->index] = 1;
    /* one fewer for the batch to wait for: the rest may launch now */
    tick_runner_done();
    bturn = next_live(r->index);
    pthread_cond_broadcast(&bcv);
    pthread_mutex_unlock(&bmu);
    return NULL;
}

int main(int argc, char **argv)
{
    static struct runner rs[S6B_MAX];
    const char *rows = NULL, *stack = NULL, *phases = "S6";
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; ++i)
    {
        if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows = argv[++i];
        else if (!strcmp(argv[i], "--stack") && i + 1 < argc) stack = argv[++i];
        else if (!strcmp(argv[i], "--phases") && i + 1 < argc) phases = argv[++i];
        else if (!strcmp(argv[i], "--device-root")) tick_device_root(1);
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) tick_device_pack(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--fn-map") && i + 1 < argc) tick_device_fnmap(argv[++i]);
        else { printf("FAIL tick_batch: unknown flag %s\n", argv[i]); return 2; }
    }
    if ((argc - i) % 2 != 0 || argc - i < 2 || (argc - i) / 2 > S6B_MAX)
    {
        fprintf(stderr, "usage: tick_batch [--stack BYTES] [--rows N] [--phases LIST] [--pack W] [--device-root] [--fn-map FILE] DIR TRACE [DIR TRACE ...] (at most %d)\n", S6B_MAX);
        return 2;
    }
    bn = (argc - i) / 2;
    for (int k = 0; k < bn; ++k)
    {
        rs[k].index = k;
        rs[k].dir = argv[i + 2 * k];
        rs[k].trace = argv[i + 2 * k + 1];
        rs[k].rows = rows;
        rs[k].stack = stack;
        rs[k].phases = phases;
    }
    /* the grave stays on (each replay enables it): with one runner at a
     * time on the baton a sweep frees the current environment's buried
     * livings only and scans every thread's stack (no guard pages, below),
     * as test_relocate's runners do; without it a long recording runs out
     * of living slots (65,536) */
    /* the device child before any thread: a fork keeps only the caller */
    if (tick_start(stack != NULL ? (size_t)atol(stack) : 0) != 0)
    {
        printf("FAIL tick_batch: no device child\n");
        return 1;
    }
    tick_live(bn);
    tick_set_yield(baton_pass);
    snapshots_row_hook = baton_pass;
    bturn = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)256 << 20);
    pthread_attr_setguardsize(&attr, 0);
    pthread_t th[S6B_MAX];
    for (int k = 0; k < bn; ++k)
        if (pthread_create(&th[k], &attr, runner_main, &rs[k]) != 0)
        {
            printf("FAIL tick_batch: cannot start runner %d\n", k);
            return 1;
        }
    for (int k = 0; k < bn; ++k) pthread_join(th[k], NULL);
    int bad = 0;
    for (int k = 0; k < bn; ++k) bad += rs[k].rc != 0;
    printf("tick_batch: %s: %d replays, S6 of all of them launched together, %d failed\n", bad ? "FAIL" : "OK", bn, bad);
    tick_stop();
    return bad ? 1 : 0;
}
