/* CPU reference: N envs x T ticks; three hex lines per tick per env (env-major order). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/cuda_batch_tick.h"

static void emit_line(const CbtEmitLine *line) {
    printf("%016llx\n", (unsigned long long)line->tick_bits);
    printf("%016llx\n", (unsigned long long)line->combined_hash);
    printf("%016llx\n", (unsigned long long)line->cur_bits);
}

int main(int argc, char **argv) {
    int n_envs = CBT_NENVS;
    int env;

    if (argc > 1)
        n_envs = (int)strtol(argv[1], 0, 10);
    if (n_envs < 1 || n_envs > CBT_NENVS)
        n_envs = CBT_NENVS;

    for (env = 0; env < n_envs; ++env) {
        Env e;
        TcfAux aux;
        TcfScratch scratch;
        ChunkPrimer primer;
        CpScratch sc;
        PfWork work;
        McSinTable st;
        CbtEmitLine lines[CBT_NTICKS];
        int t;

        mc_sin_table_init(&st);
        cbt_run_one(&e, &aux, CBT_SEEDS[env], &primer, &sc, &st, &scratch, &work, lines);
        for (t = 0; t < CBT_NTICKS; ++t)
            emit_line(&lines[t]);
    }
    return 0;
}
