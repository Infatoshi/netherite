/* env_cuda_stage.c - one-shot device staging for blaze_cuda step_full. */
#include "env_cuda_stage.h"

#include <cuda_runtime.h>
#include <stdio.h>
#include <string.h>

enum {
  STAGE_ACT = 13,
  STAGE_CAM_H = 36,
  STAGE_CAM_W = 64,
  STAGE_NPIX = STAGE_CAM_H * STAGE_CAM_W,
  STAGE_SCAL = 6,
  STAGE_POSE = 5,
  STAGE_STATUS = 17
};

static void stage_zero(EnvCudaStage *s) {
  if (!s)
    return;
  memset(s, 0, sizeof(*s));
  s->n = 0;
  s->device = -1;
}

static int ck(cudaError_t e, const char *what) {
  if (e == cudaSuccess)
    return 0;
  fprintf(stderr, "env_cuda_stage: %s: %s\n", what, cudaGetErrorString(e));
  return -1;
}

/* Free a typed device pointer without void** aliasing, then clear it. */
#define FREE_DEV(p)                                                            \
  do {                                                                         \
    if (p) {                                                                   \
      cudaFree((void *)(p));                                                   \
      (p) = NULL;                                                              \
    }                                                                          \
  } while (0)

void env_cuda_stage_destroy(EnvCudaStage *s) {
  if (!s)
    return;
  if (s->device >= 0)
    (void)cudaSetDevice(s->device);
  FREE_DEV(s->d_act);
  FREE_DEV(s->d_cam);
  FREE_DEV(s->d_depth);
  FREE_DEV(s->d_edge);
  FREE_DEV(s->d_scal);
  FREE_DEV(s->d_rew);
  FREE_DEV(s->d_done);
  FREE_DEV(s->d_pose);
  FREE_DEV(s->d_status);
  stage_zero(s);
}

int env_cuda_stage_create(EnvCudaStage *s, int n, int device) {
  size_t n_act, n_cam, n_pix, n_scal, n_pose, n_status;

  if (!s || n <= 0 || device < 0)
    return -1;
  stage_zero(s);
  s->n = n;
  s->device = device;
  if (ck(cudaSetDevice(device), "cudaSetDevice")) {
    stage_zero(s);
    return -1;
  }

  n_act = (size_t)n * STAGE_ACT * sizeof(double);
  n_cam = (size_t)n * STAGE_NPIX * sizeof(unsigned short);
  n_pix = (size_t)n * STAGE_NPIX;
  n_scal = (size_t)n * STAGE_SCAL * sizeof(float);
  n_pose = (size_t)n * STAGE_POSE * sizeof(float);
  n_status = (size_t)n * STAGE_STATUS * sizeof(int);

  if (ck(cudaMalloc((void **)&s->d_act, n_act), "d_act") ||
      ck(cudaMalloc((void **)&s->d_cam, n_cam), "d_cam") ||
      ck(cudaMalloc((void **)&s->d_depth, n_pix), "d_depth") ||
      ck(cudaMalloc((void **)&s->d_edge, n_pix), "d_edge") ||
      ck(cudaMalloc((void **)&s->d_scal, n_scal), "d_scal") ||
      ck(cudaMalloc((void **)&s->d_rew, (size_t)n * sizeof(float)), "d_rew") ||
      ck(cudaMalloc((void **)&s->d_done, (size_t)n), "d_done") ||
      ck(cudaMalloc((void **)&s->d_pose, n_pose), "d_pose") ||
      ck(cudaMalloc((void **)&s->d_status, n_status), "d_status")) {
    env_cuda_stage_destroy(s);
    return -1;
  }
  return 0;
}

int env_cuda_stage_step_full(EnvCudaStage *s, BlazeStepFullFn step_full,
                             void *env, const double *h_act, int repeat,
                             unsigned short *h_cam, unsigned char *h_depth,
                             unsigned char *h_edge, float *h_scal, float *h_rew,
                             unsigned char *h_done, float *h_pose,
                             int *h_status) {
  int rc;
  size_t n_act, n_cam, n_pix, n_scal, n_pose, n_status;

  if (!s || !step_full || !env || !h_act || repeat < 1 || s->n <= 0)
    return -1;
  if (ck(cudaSetDevice(s->device), "cudaSetDevice"))
    return -1;

  n_act = (size_t)s->n * STAGE_ACT * sizeof(double);
  n_cam = (size_t)s->n * STAGE_NPIX * sizeof(unsigned short);
  n_pix = (size_t)s->n * STAGE_NPIX;
  n_scal = (size_t)s->n * STAGE_SCAL * sizeof(float);
  n_pose = (size_t)s->n * STAGE_POSE * sizeof(float);
  n_status = (size_t)s->n * STAGE_STATUS * sizeof(int);

  if (ck(cudaMemcpy(s->d_act, h_act, n_act, cudaMemcpyHostToDevice),
         "H2D act"))
    return -1;

  rc = step_full(env, s->d_act, repeat, s->d_cam, s->d_depth, s->d_edge,
                 s->d_scal, s->d_rew, s->d_done, s->d_pose, s->d_status);
  if (rc != 0)
    return rc;

  if (h_cam &&
      ck(cudaMemcpy(h_cam, s->d_cam, n_cam, cudaMemcpyDeviceToHost), "D2H cam"))
    return -1;
  if (h_depth &&
      ck(cudaMemcpy(h_depth, s->d_depth, n_pix, cudaMemcpyDeviceToHost),
         "D2H depth"))
    return -1;
  if (h_edge &&
      ck(cudaMemcpy(h_edge, s->d_edge, n_pix, cudaMemcpyDeviceToHost),
         "D2H edge"))
    return -1;
  if (h_scal &&
      ck(cudaMemcpy(h_scal, s->d_scal, n_scal, cudaMemcpyDeviceToHost),
         "D2H scal"))
    return -1;
  if (h_rew &&
      ck(cudaMemcpy(h_rew, s->d_rew, (size_t)s->n * sizeof(float),
                    cudaMemcpyDeviceToHost),
         "D2H rew"))
    return -1;
  if (h_done &&
      ck(cudaMemcpy(h_done, s->d_done, (size_t)s->n, cudaMemcpyDeviceToHost),
         "D2H done"))
    return -1;
  if (h_pose &&
      ck(cudaMemcpy(h_pose, s->d_pose, n_pose, cudaMemcpyDeviceToHost),
         "D2H pose"))
    return -1;
  if (h_status &&
      ck(cudaMemcpy(h_status, s->d_status, n_status, cudaMemcpyDeviceToHost),
         "D2H status"))
    return -1;
  return 0;
}
