/* CUDA driver for spawner_activate - same core/spawner_activate.h as CPU. */
#include <cstdio>
#include <cstdlib>
#include "../core/spawner_activate.h"

__global__ void run_sa(TeSpawnerScene *scene, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    sa_run(scene, out);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    TeSpawnerScene *d_scene;
    u64 *d_out, h_out[SA_OUT];
    cudaMalloc(&d_scene, sizeof(TeSpawnerScene));
    cudaMalloc(&d_out, sizeof(h_out));
    run_sa<<<1, 1>>>(d_scene, d_out);
    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost);
    for (int i = 0; i < SA_OUT; ++i)
        printf("%016llx\n", (unsigned long long)h_out[i]);
    cudaFree(d_scene);
    cudaFree(d_out);
    return 0;
}
