/* sps_benchmark: batched tick throughput + deterministic trajectory tail hash.
 *
 * stdout (oracle): total_steps, final_combined_hash (CPU==CUDA).
 * stderr (human):  SPS line with envs/ticks/rounds/elapsed. */
#ifndef MC_SPS_BENCHMARK_H
#define MC_SPS_BENCHMARK_H

#include <time.h>
#include "cuda_batch_tick.h"

#define SPS_NENVS   CBT_NENVS
#define SPS_NTICKS  CBT_NTICKS
#define SPS_ROUNDS  2

MC_HD static inline u64 sps_run_batch(int n_envs, u64 *final_hash_out) {
    McSinTable st;
    int env, round;
    u64 last_hash = 0;

    mc_sin_table_init(&st);
    for (round = 0; round < SPS_ROUNDS; ++round) {
        for (env = 0; env < n_envs; ++env) {
            Env e;
            TcfAux aux;
            TcfScratch scratch;
            ChunkPrimer primer;
            CpScratch sc;
            PfWork work;
            CbtEmitLine lines[SPS_NTICKS];

            cbt_run_one(&e, &aux, CBT_SEEDS[env], &primer, &sc, &st,
                        &scratch, &work, lines);
            last_hash = lines[SPS_NTICKS - 1].combined_hash;
        }
    }
    if (final_hash_out)
        *final_hash_out = last_hash;
    return (u64)n_envs * (u64)SPS_NTICKS * (u64)SPS_ROUNDS;
}

#endif
