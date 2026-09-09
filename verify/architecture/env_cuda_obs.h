/* Measurement bridge: CPU-authoritative packed world -> CUDA semantic camera.
 * Full world upload each call; no claim of delta transfer or RGB rendering. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*HybridCamInputsFn)(void *,int,double*,double*,double*,float*,float*,int*,int*,int*,int*,int*,int*,const unsigned short**);
typedef int (*HybridCamFreshFn)(void *,int);
typedef struct EnvCudaObs EnvCudaObs;
typedef struct { double pack_ms, upload_ms, kernel_ms, download_ms; size_t h2d_bytes,d2h_bytes; } EnvCudaObsStats;
EnvCudaObs *env_cuda_obs_create(int device,int n,size_t max_cells,HybridCamInputsFn,HybridCamFreshFn);
/* NULL mask resets all lanes to CPU camera defaults, without rendering. */
int env_cuda_obs_reset(EnvCudaObs *,const unsigned char *mask);
void env_cuda_obs_destroy(EnvCudaObs *);
/* force=1 after reset, otherwise respects CPU decision camera freshness.
 * host outputs all NULL leaves observations device-resident. Synchronous. */
int env_cuda_obs_render(EnvCudaObs *,void *cpu_env,int force,unsigned short *,unsigned char *,unsigned char *,EnvCudaObsStats *);
const unsigned short *env_cuda_obs_cam(EnvCudaObs *);
const unsigned char *env_cuda_obs_depth(EnvCudaObs *);
const unsigned char *env_cuda_obs_edge(EnvCudaObs *);
#ifdef __cplusplus
}
#endif
