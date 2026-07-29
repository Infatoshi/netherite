/* cuda_batch_worldgen: Wave 14 batch layer - CBW_N cp_provide_chunk envs.
 *
 * INTERNAL verify (CPU==CUDA). Scalar CPU loop vs serial cp_provide_kernel launches.
 * Output: CBW_N x 4 hex lines (seed, cx, cz, primer FNV-1a) — not full 65536 dumps.
 * Device kernel: cuda/cp_provide_kernel.cu (cached .o via oracle/runner.py). */
#ifndef MC_CUDA_BATCH_WORLDGEN_H
#define MC_CUDA_BATCH_WORLDGEN_H

#include "chunk_provider.h"

#define CBW_N 4
#define CBW_CHUNK_VOL 65536
#define CBW_FIELDS 4

typedef struct {
    i64 seed;
    int cx;
    int cz;
} CbwEnv;

static const CbwEnv CBW_ENVS[CBW_N] = {
    {12345LL,  0,  0},
    {0LL,      1,  0},
    {7LL,      0,  1},
    {42LL,    -1,  2},
};

MC_HD static inline u64 cbw_primer_hash(const ChunkPrimer *p) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < CBW_CHUNK_VOL; ++i) {
        h ^= (u64)p->data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

MC_HD static inline void cbw_provide_one(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
        const CbwEnv *env) {
    cp_provide_chunk(primer, sc, st, env->seed, env->cx, env->cz);
}

#endif
