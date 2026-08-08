/* CUDA reference: simple_dungeon.json generation plus fillInventory. */
#include <cstdio>
#include <cuda_runtime.h>
#include "../core/stronghold_loot.h"

__global__ void run_simple_dungeon_loot(u32 *out) {
    if (threadIdx.x || blockIdx.x) return;
    shl_run_dungeon_battery(out);
}

int main(void) {
    u32 *device;
    u32 host[SHL_DUNGEON_BAT_OUT];
    cudaMalloc(&device, sizeof host);
    run_simple_dungeon_loot<<<1, 1>>>(device);
    cudaDeviceSynchronize();
    cudaMemcpy(host, device, sizeof host, cudaMemcpyDeviceToHost);
    for (int i = 0; i < SHL_DUNGEON_BAT_OUT; ++i)
        printf("%08x\n", (unsigned)host[i]);
    cudaFree(device);
    return 0;
}
