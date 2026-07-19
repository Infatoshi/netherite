/* cuenv_verify - CPU cuenv smoke + throughput tool.
 *
 *   ./cuenv_verify <snapshot.bsnp> [n_envs] [decisions] [repeat]
 *
 * Loads one snapshot into n envs, steps `decisions` trainer decisions with a
 * seeded pseudo-random policy (heavy attack, forward bias - the same
 * generator verify_cpu.py uses), and reports env-ticks/s and decisions/s.
 * The real-env lockstep fidelity gate lives in verify_cpu.py (this binary is
 * the perf harness + a standalone sanity check that the core runs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cuenv_core.h"

void *cuenv_create(int device, int n);
void  cuenv_destroy(void *vh);
int   cuenv_load_snapshots(void *vh, const char *const *paths, int count,
                           char *err, int err_cap);
int   cuenv_snapshot_has_liquid(void *vh, int snap);
int   cuenv_assign(void *vh, const int *snap_idx);
int   cuenv_reset(void *vh, const unsigned char *mask);
int   cuenv_step(void *vh, const double *actions, int repeat,
                 unsigned short *cam, unsigned char *depth,
                 unsigned char *edge, float *scal, float *rew,
                 unsigned char *done, float *pose);

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* xorshift32 - keep in sync with verify_cpu.py rand_actions() */
static unsigned rng_state;
static unsigned rng_next(void) {
    unsigned x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(int argc, char **argv) {
    const char *path;
    int n = 64, decisions = 250, repeat = 4;
    double *actions;
    int *assign;
    unsigned short *cam;
    unsigned char *dep, *edg, *done;
    float *scal, *rew, *pose;
    void *h;
    char err[256];
    double t0, t1;
    int d, i, rc = 0;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <snapshot.bsnp> [n_envs] [decisions] [repeat]\n",
                argv[0]);
        return 2;
    }
    path = argv[1];
    if (argc > 2) n = atoi(argv[2]);
    if (argc > 3) decisions = atoi(argv[3]);
    if (argc > 4) repeat = atoi(argv[4]);
    if (n <= 0 || decisions <= 0 || repeat <= 0) return 2;

    h = cuenv_create(0, n);
    if (!h) { fprintf(stderr, "cuenv_create failed\n"); return 1; }
    if (cuenv_load_snapshots(h, &path, 1, err, sizeof err) < 0) {
        fprintf(stderr, "load_snapshots: %s\n", err);
        cuenv_destroy(h);
        return 1;
    }
    if (cuenv_snapshot_has_liquid(h, 0))
        fprintf(stderr, "WARNING: region contains liquid (ids 8-11); "
                        "fluids are not simulated\n");
    assign = (int *)calloc((size_t)n, sizeof *assign);
    actions = (double *)calloc((size_t)n * 13, sizeof *actions);
    cam = (unsigned short *)malloc((size_t)n * CU_NPIX * sizeof *cam);
    dep = (unsigned char *)malloc((size_t)n * CU_NPIX);
    edg = (unsigned char *)malloc((size_t)n * CU_NPIX);
    scal = (float *)malloc((size_t)n * 6 * sizeof *scal);
    rew = (float *)malloc((size_t)n * sizeof *rew);
    done = (unsigned char *)malloc((size_t)n);
    pose = (float *)malloc((size_t)n * 5 * sizeof *pose);
    if (!assign || !actions || !cam || !dep || !edg || !scal || !rew ||
        !done || !pose) { fprintf(stderr, "alloc failed\n"); return 1; }
    if (cuenv_assign(h, assign) != 0 || cuenv_reset(h, NULL) != 0) {
        fprintf(stderr, "assign/reset failed\n");
        return 1;
    }

    rng_state = 0xC0A1u;
    t0 = now_s();
    for (d = 0; d < decisions; ++d) {
        for (i = 0; i < n; ++i) {
            /* the historical 5-head draws, expanded to the 13-double raw
             * action layout exactly like cuenv.py's legacy expansion */
            double *a = &actions[i * 13];
            static const double yaws[3] = {-15.0, 0.0, 15.0};
            static const double pitches[3] = {-10.0, 0.0, 10.0};
            a[2] = yaws[rng_next() % 3u];                   /* dyaw    */
            a[3] = pitches[rng_next() % 3u];                /* dpitch  */
            a[0] = (double)(rng_next() % 4u != 0u);         /* forward 75% */
            a[4] = (double)(rng_next() % 8u == 0u);         /* jump 12.5% */
            a[7] = (double)(rng_next() % 4u != 3u);         /* attack 75% */
            a[1] = a[5] = a[6] = a[8] = a[11] = a[12] = 0.0;
            a[9] = -1.0;                                    /* hotbar  */
            a[10] = -1.0;                                   /* craft   */
        }
        if (cuenv_step(h, actions, repeat, cam, dep, edg, scal, rew, done,
                       pose) != 0) {
            fprintf(stderr, "cuenv_step failed at decision %d\n", d);
            rc = 1;
            break;
        }
    }
    t1 = now_s();

    if (!rc) {
        long long ticks = (long long)decisions * repeat * n;
        int ndone = 0;
        for (i = 0; i < n; ++i) ndone += done[i] != 0;
        printf("cuenv_cpu: n=%d decisions=%d repeat=%d  "
               "%.0f env-ticks/s  %.0f decisions/s  (%.3fs, %d/%d done)\n",
               n, decisions, repeat, (double)ticks / (t1 - t0),
               (double)decisions * n / (t1 - t0), t1 - t0, ndone, n);
        printf("env0 pose: x=%.3f y=%.3f z=%.3f yaw=%.1f pitch=%.1f rew=%.4f\n",
               pose[0], pose[1], pose[2], pose[3], pose[4], rew[0]);
    }
    free(assign); free(actions); free(cam); free(dep); free(edg);
    free(scal); free(rew); free(done); free(pose);
    cuenv_destroy(h);
    return rc;
}
