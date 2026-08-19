/* CUDA-owned host/device staging for the blaze_cuda device-pointer ABI.
 * Allocate once at create. step_full only copies and calls; no heap/device
 * alloc in a rollout step. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EnvCudaStage {
  int n;
  int device;
  double *d_act;
  unsigned short *d_cam;
  unsigned char *d_depth;
  unsigned char *d_edge;
  float *d_scal;
  float *d_rew;
  unsigned char *d_done;
  float *d_pose;
  int *d_status;
} EnvCudaStage;

/* Device ABI for blaze_step_full (actions + outputs are device pointers). */
typedef int (*BlazeStepFullFn)(void *h, const double *actions, int repeat,
                               unsigned short *cam, unsigned char *depth,
                               unsigned char *edge, float *scal, float *rew,
                               unsigned char *done, float *pose, int *status);

/* Create staging on `device` for `n` envs. Returns 0 or -1. */
int env_cuda_stage_create(EnvCudaStage *s, int n, int device);

void env_cuda_stage_destroy(EnvCudaStage *s);

/* H2D actions, device step_full, D2H outputs. No alloc. Returns step_full rc
 * or -1 on CUDA error. */
int env_cuda_stage_step_full(EnvCudaStage *s, BlazeStepFullFn step_full,
                             void *env, const double *h_act, int repeat,
                             unsigned short *h_cam, unsigned char *h_depth,
                             unsigned char *h_edge, float *h_scal, float *h_rew,
                             unsigned char *h_done, float *h_pose,
                             int *h_status);

#ifdef __cplusplus
}
#endif
