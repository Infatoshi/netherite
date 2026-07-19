/* CUDA driver for block_tickers. */
#include <cstdio>
#include <cstdlib>
#include "../core/block_tickers.h"

__global__ void run_bt(BtWorld *w) {
    if (threadIdx.x || blockIdx.x) return;
    bt_run(w);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    BtWorld hw, *dw;
    hw.seed = seed;
    cudaMalloc(&dw, sizeof(BtWorld));
    cudaMemcpy(dw, &hw, sizeof(BtWorld), cudaMemcpyHostToDevice);
    run_bt<<<1, 1>>>(dw);
    cudaDeviceSynchronize();
    cudaMemcpy(&hw, dw, sizeof(BtWorld), cudaMemcpyDeviceToHost);
    const u16 *b = bt_now(&hw);
    for (int y = 0; y < BT_H; ++y)
        for (int z = 0; z < BT_W; ++z)
            for (int x = 0; x < BT_W; ++x)
                printf("%016llx\n", (unsigned long long)bt_get(b, x, y, z));
    cudaFree(dw);
    return 0;
}
