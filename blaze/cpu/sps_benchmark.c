#include <stdio.h>
#include <time.h>
#include "../core/sps_benchmark.h"

int main(int argc, char **argv) {
    struct timespec t0, t1;
    u64 final_hash = 0;
    u64 total_steps;
    double elapsed;
    double sps;
    int n_envs = SPS_NENVS;

    if (argc > 1)
        n_envs = (int)strtol(argv[1], 0, 10);
    if (n_envs < 1 || n_envs > SPS_NENVS)
        n_envs = SPS_NENVS;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    total_steps = sps_run_batch(n_envs, &final_hash);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    sps = elapsed > 0.0 ? (double)total_steps / elapsed : 0.0;

    printf("%016llx\n", (unsigned long long)total_steps);
    printf("%016llx\n", (unsigned long long)final_hash);
    fprintf(stderr,
            "sps_benchmark cpu: envs=%d ticks=%d rounds=%d steps=%llu "
            "elapsed=%.3fs sps=%.0f\n",
            n_envs, SPS_NTICKS, SPS_ROUNDS,
            (unsigned long long)total_steps, elapsed, sps);
    return 0;
}
