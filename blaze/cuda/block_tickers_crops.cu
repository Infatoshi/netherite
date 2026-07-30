/* CUDA driver for block_tickers_crops. */
#include <cstdio>
#include <cstdlib>
#include "../core/block_tickers_crops.h"

__global__ void run_btc(BtcWorld *w) {
    if (threadIdx.x || blockIdx.x) return;
    btc_run(w);
}

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    BtcWorld hw, *dw;
    hw.seed = seed;
    cudaMalloc(&dw, sizeof(BtcWorld));
    cudaMemcpy(dw, &hw, sizeof(BtcWorld), cudaMemcpyHostToDevice);
    run_btc<<<1, 1>>>(dw);
    cudaDeviceSynchronize();
    cudaMemcpy(&hw, dw, sizeof(BtcWorld), cudaMemcpyDeviceToHost);
    const u16 *b = btc_now(&hw);
    for (int y = 0; y < BTC_H; ++y)
        for (int z = 0; z < BTC_W; ++z)
            for (int x = 0; x < BTC_W; ++x)
                printf("%016llx\n", (unsigned long long)btc_get(b, x, y, z));
    cudaFree(dw);
    return 0;
}
