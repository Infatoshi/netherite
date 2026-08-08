/* CUDA reference: abandoned_mineshaft.json generation plus fillInventory. */
#include <stdio.h>
#include <cuda_runtime.h>
#include "../core/stronghold_loot.h"

__global__ void run_abandoned_mineshaft_loot(u32 *out) {
    if (blockIdx.x == 0 && threadIdx.x == 0)
        shl_run_mineshaft_battery(out);
}

int main(void) {
    u32 *device = NULL;
    u32 host[SHL_MINESHAFT_BAT_OUT];
    cudaMalloc(&device, sizeof(host));
    run_abandoned_mineshaft_loot<<<1, 1>>>(device);
    cudaMemcpy(host, device, sizeof(host), cudaMemcpyDeviceToHost);
    for (int i = 0; i < SHL_MINESHAFT_BAT_OUT; ++i)
        printf("%u\n", (unsigned)host[i]);
    cudaFree(device);
    return 0;
}
