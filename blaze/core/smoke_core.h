/* smoke_core.h - trivial shared kernel to prove CPU==CUDA from one source.
 * Exercises both RNGs. Not gameplay; just toolchain + determinism plumbing. */
#ifndef SMOKE_CORE_H
#define SMOKE_CORE_H

#include "mc_rng.h"

MC_HD static inline void smoke_kernel(u64 world_seed, u64 *out, int n) {
    JavaRandom jr; jrand_set(&jr, (i64)world_seed);
    for (int i = 0; i < n; i++) {
        /* worldgen-style stateful draw */
        u64 a = (u64)(u32)jrand_int_bound(&jr, 1000000);
        u64 b = (u64)jrand_float(&jr) * 1000000ULL;
        /* runtime-style stateless draw (order-independent) */
        u64 h = mc_hash_seed(world_seed, /*tick*/i, /*x*/i * 7 - 3, /*y*/64, /*z*/i * 13 + 5, /*purpose*/i & 7);
        u64 c = (u64)mc_hash_bound(h, 4096);
        out[i] = mc_hash64(a * 0x100000001ULL ^ (b << 20) ^ (c << 40));
    }
}

#endif
