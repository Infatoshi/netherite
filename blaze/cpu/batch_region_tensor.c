/* CPU reference: fill BRT_N region tensors sequentially (own primer/scratch/McSinTable),
 * then dump ALL envs element-wise as %04x lines, preceded by a per-env header line. The
 * dump is byte-identical to cuda/batch_region_tensor.cu so runner.py diffs it literally. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/batch_region_tensor.h"

int main(void) {
    McSinTable st;
    u16 *out_all;
    ChunkPrimer *primer;
    CpScratch *sc;
    int e;
    long i;

    mc_sin_table_init(&st);

    out_all = (u16 *)malloc((size_t)BRT_N * BRT_VOL * sizeof(u16));
    primer  = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    sc      = (CpScratch *)malloc(sizeof(CpScratch));
    if (!out_all || !primer || !sc) { fprintf(stderr, "malloc failed\n"); return 1; }

    for (e = 0; e < BRT_N; ++e)
        brt_fill_one(out_all, &BRT_ENVS[e], e, primer, sc, &st);

    for (e = 0; e < BRT_N; ++e) {
        const BrtEnv *env = &BRT_ENVS[e];
        const u16 *slice = out_all + (long)e * BRT_VOL;
        printf("env %d seed=%lld x0=%d y0=%d z0=%d\n",
               e, (long long)env->seed, env->x0, env->y0, env->z0);
        for (i = 0; i < BRT_VOL; ++i)
            printf("%04x\n", (unsigned)slice[i]);
    }

    free(sc);
    free(primer);
    free(out_all);
    return 0;
}
