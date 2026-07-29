/* CUDA driver for ender_dragon_death - same core/ender_dragon_death.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/ender_dragon_death.h"

__global__ void run_ede(int idx, int nticks, const McSinTable *st, EdeWorld *world, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    ede_run_scenario(idx, st, world, nticks, out);
}

static void run_one(int idx, int nticks) {
    McSinTable h_st;
    mc_sin_table_init(&h_st);
    McSinTable *d_st;
    cudaMalloc(&d_st, sizeof(McSinTable));
    cudaMemcpy(d_st, &h_st, sizeof(McSinTable), cudaMemcpyHostToDevice);

    EdeWorld *d_world;
    u64 *d_out;
    cudaMalloc(&d_world, sizeof(EdeWorld));
    cudaMalloc(&d_out, sizeof(u64) * (size_t)nticks * EDE_DUMP_FIELDS);

    run_ede<<<1, 1>>>(idx, nticks, d_st, d_world, d_out);
    cudaDeviceSynchronize();

    u64 *h_out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * EDE_DUMP_FIELDS);
    cudaMemcpy(h_out, d_out, sizeof(u64) * (size_t)nticks * EDE_DUMP_FIELDS, cudaMemcpyDeviceToHost);

    for (int i = 0; i < nticks * EDE_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);

    free(h_out);
    cudaFree(d_st);
    cudaFree(d_world);
    cudaFree(d_out);
}

int main(int argc, char **argv) {
    int nticks = (argc > 2) ? atoi(argv[2]) : EDE_NUM_TICKS;

    if (argc > 1) {
        run_one(atoi(argv[1]), nticks);
    } else {
        for (int i = 0; i < EDE_NUM_SCENARIOS; ++i)
            run_one(i, nticks);
    }
    return 0;
}
