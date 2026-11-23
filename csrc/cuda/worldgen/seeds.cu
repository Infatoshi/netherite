/* gen_seeds: chunkgen_init's state for each world seed (terrain_init's noise
 * permutations, layers_init's world seeds, surface_init's Mesa bands and
 * noises, MapGenBase's two longs), a thread per seed. */
#include "dev.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

template <class G>
__device__ static __noinline__ void noise_init(G *n, jrand *r)
{
    n->xo = jr_double(r) * 256.0;
    n->yo = jr_double(r) * 256.0;
    n->zo = jr_double(r) * 256.0;
#pragma unroll 1
    for (int i = 0; i < 256; ++i) n->p[i] = (uint8_t)i;
#pragma unroll 1
    for (int i = 0; i < 256; ++i)
    {
        int j = jr_int_n(r, 256 - i) + i;
        uint8_t t = n->p[i];
        n->p[i] = n->p[j];
        n->p[j] = t;
        n->p[i + 256] = n->p[i];
    }
}

/* the dimension units build their generators with the same shuffle */
__device__ __noinline__ void improved_init(struct g_improved *n, jrand *r)
{
    noise_init(n, r);
}

__global__ void gen_seeds(struct cg_seed *seeds, const int64_t *values, int n)
{
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    struct cg_seed *s = &seeds[k];
    int64_t seed = values[k];
    s->seed = seed;

    /* terrain_init: the construction order consumes one rand */
    jrand r;
    jr_seed(&r, seed);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) noise_init(&s->min_limit[i], &r);
#pragma unroll 1
    for (int i = 0; i < 16; ++i) noise_init(&s->max_limit[i], &r);
    for (int i = 0; i < 8; ++i) noise_init(&s->main[i], &r);
    for (int i = 0; i < 4; ++i) noise_init(&s->stone[i], &r);
    {
        struct g_improved unused;   /* noiseGen5: built, never read */
        for (int i = 0; i < 10; ++i) noise_init(&unused, &r);
    }
#pragma unroll 1
    for (int i = 0; i < 16; ++i) noise_init(&s->depth[i], &r);

    /* layers_init: base seeds, then initWorldGenSeed's world seeds */
    for (int n2 = 0; n2 < NODES; ++n2)
    {
        int64_t arg = c_graph[n2].arg, base = arg;
        for (int i = 0; i < 3; ++i) base = lcg(base, arg);
        int64_t ws = 0;
        if (c_graph[n2].seeded)
        {
            ws = seed;
            for (int i = 0; i < 3; ++i) ws = lcg(ws, base);
        }
        s->ws[n2] = ws;
    }

    /* surface_init */
    for (int i = 0; i < 64; ++i) s->bands[i] = 16;
    jr_seed(&r, seed);
    noise_init(&s->band_shift, &r);
    for (int i = 0; i < 64; ++i)
    {
        i += jr_int_n(&r, 5) + 1;
        if (i < 64) s->bands[i] = 1;
    }
    const int bound[3] = {3, 3, 3}, add[3] = {1, 2, 1};
    const uint8_t color[3] = {4, 12, 14};
    for (int k2 = 0; k2 < 3; ++k2)
    {
        int m = jr_int_n(&r, 4) + 2;
        for (int i = 0; i < m; ++i)
        {
            int len = jr_int_n(&r, bound[k2]) + add[k2];
            int at0 = jr_int_n(&r, 64);
            for (int j = 0; at0 + j < 64 && j < len; ++j) s->bands[at0 + j] = color[k2];
        }
    }
    int m = jr_int_n(&r, 3) + 3, at0 = 0;
    for (int i = 0; i < m; ++i)
    {
        at0 += jr_int_n(&r, 16) + 4;
        if (at0 < 64)
        {
            s->bands[at0] = 0;
            if (at0 > 1 && jr_bool(&r)) s->bands[at0 - 1] = 8;
            if (at0 < 63 && jr_bool(&r)) s->bands[at0 + 1] = 8;
        }
    }
    jr_seed(&r, 0);
    for (int i = 0; i < 4; ++i) noise_init(&s->spire[i], &r);
    noise_init(&s->spire_cap, &r);

    /* MapGenBase.func_151539_a's two longs, the same for caves and ravines */
    jr_seed(&r, seed);
    s->carve_kx = jr_long(&r);
    s->carve_kz = jr_long(&r);
}
