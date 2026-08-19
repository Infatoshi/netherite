/* Metal observation adapter for the native PPO smoke.
 * CPU tick remains the simulation. After each CPU step, overwrite
 * cam/depth/edge via blaze_metal_obs_render for every lane.
 * Allocate the Metal handle once at create; the per-step path does not
 * malloc. Darwin only. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same signature as blaze_cpu blaze_obs_cam_inputs (loaded via dlsym). */
typedef int (*BlazeObsCamInputsFn)(void *vh, int env, double *ex, double *ey,
                                   double *ez, float *yaw, float *pitch,
                                   int *x0, int *y0, int *z0, int *nx, int *ny,
                                   int *nz, const unsigned short **cells);

typedef struct EnvMetalObs {
  int n;
  int max_cells;
  void *metal; /* BlazeMetalObs* */
  BlazeObsCamInputsFn cam_inputs;
} EnvMetalObs;

/* Owned default when config metallib is "auto". Path is repo-root relative. */
#define ENV_METAL_OBS_DEFAULT_METALLIB "out/blaze/env/blaze_metal_obs.metallib"

/* Create once. metallib_path must be non-NULL and readable; no fallback.
 * cam_inputs is required (from the CPU env .so). Returns 0 or -1. */
int env_metal_obs_create(EnvMetalObs *s, int n, int max_cells,
                         const char *metallib_path,
                         BlazeObsCamInputsFn cam_inputs);

void env_metal_obs_destroy(EnvMetalObs *s);

/* After a CPU blaze_step_full into host cam/depth/edge, re-render every lane
 * with Metal and overwrite those buffers. No heap ops. Returns 0 or -1. */
int env_metal_obs_overwrite(EnvMetalObs *s, void *cpu_env, unsigned short *cam,
                            unsigned char *depth, unsigned char *edge);

#ifdef __cplusplus
}
#endif
