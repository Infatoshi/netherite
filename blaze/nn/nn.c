/* Public Nn dispatch: backend-neutral C ABI over CPU / Metal / CUDA seams.
 * NN_HAVE_METAL and NN_HAVE_CUDA are build-time only (linked code present).
 * A selected unavailable backend fails with a clear error; never falls back. */
#include "cpu.h"
#include "fixture.h"
#include "nn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NN_HAVE_METAL
#define NN_HAVE_METAL 0
#endif
#ifndef NN_HAVE_CUDA
#define NN_HAVE_CUDA 0
#endif

#if NN_HAVE_METAL
#include "metal.h"
#endif

#if NN_HAVE_CUDA
#include "cuda.h"
#endif

struct Nn {
  NnBackend backend;
  int device;
  int max_n;
  void *impl; /* NnCpu* | NnCuda* | NnMetal* */
};

static char g_err[512] = "";

static void set_err(const char *msg) {
  snprintf(g_err, sizeof(g_err), "%s", msg);
}

static void set_errf(const char *fmt, const char *name) {
  snprintf(g_err, sizeof(g_err), fmt, name);
}

const char *nn_last_error(void) { return g_err; }

static void take_cpu_err(int rc) {
  if (rc != 0)
    set_err(nn_cpu_last_error());
}

#if NN_HAVE_METAL
static void take_metal_err(int rc) {
  if (rc != 0)
    set_err(nn_metal_last_error());
}
#endif

#if NN_HAVE_CUDA
static void take_cuda_err(int rc) {
  if (rc != 0)
    set_err(nn_cuda_last_error());
}
#endif

Nn *nn_create(const NnCreate *desc) {
  g_err[0] = 0;
  if (!desc) {
    set_err("null create descriptor");
    return NULL;
  }
  if (desc->max_n <= 0) {
    set_err("max_n must be > 0");
    return NULL;
  }

  Nn *nn = (Nn *)calloc(1, sizeof(Nn));
  if (!nn) {
    set_err("oom handle");
    return NULL;
  }
  nn->backend = desc->backend;
  nn->device = desc->device;
  nn->max_n = desc->max_n;

  switch (desc->backend) {
  case NN_BACKEND_CPU: {
    NnCpu *cpu = nn_cpu_create(desc->max_n, desc->device, &desc->config);
    if (!cpu) {
      set_err(nn_cpu_last_error());
      free(nn);
      return NULL;
    }
    nn->impl = cpu;
    return nn;
  }

  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    NnCuda *cuda =
        nn_cuda_create(desc->max_n, desc->device, &desc->config);
    if (!cuda) {
      set_err(nn_cuda_last_error());
      free(nn);
      return NULL;
    }
    nn->impl = cuda;
    return nn;
#else
    set_errf("%s backend not available", "CUDA");
    free(nn);
    return NULL;
#endif
  }

  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    NnMetal *mtl =
        nn_metal_create(desc->max_n, desc->device, &desc->config);
    if (!mtl) {
      set_err(nn_metal_last_error());
      free(nn);
      return NULL;
    }
    nn->impl = mtl;
    return nn;
#else
    set_errf("%s backend not available", "Metal");
    free(nn);
    return NULL;
#endif
  }

  default:
    snprintf(g_err, sizeof(g_err), "invalid backend %d", (int)desc->backend);
    free(nn);
    return NULL;
  }
}

void nn_destroy(Nn *nn) {
  if (!nn)
    return;
  switch (nn->backend) {
  case NN_BACKEND_CPU:
    nn_cpu_destroy((NnCpu *)nn->impl);
    break;
  case NN_BACKEND_METAL:
#if NN_HAVE_METAL
    nn_metal_destroy((NnMetal *)nn->impl);
#endif
    break;
  case NN_BACKEND_CUDA:
#if NN_HAVE_CUDA
    nn_cuda_destroy((NnCuda *)nn->impl);
#endif
    break;
  default:
    break;
  }
  free(nn);
}

int nn_set_config(Nn *nn, const NnConfig *cfg) {
  if (!nn || !cfg) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc = nn_cpu_set_config((NnCpu *)nn->impl, cfg);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc = nn_metal_set_config((NnMetal *)nn->impl, cfg);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_set_config((NnCuda *)nn->impl, cfg);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_prepare_n(Nn *nn, int n) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU:
  case NN_BACKEND_METAL:
    return 0; /* no plan cache to prepare */
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_prepare_n((NnCuda *)nn->impl, n);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_seal(Nn *nn) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU:
  case NN_BACKEND_METAL:
    return 0; /* nothing to seal */
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_seal((NnCuda *)nn->impl);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_forward(Nn *nn, const uint8_t *planes, const float *scalars, int n,
               float *logits, float *values) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc = nn_cpu_forward((NnCpu *)nn->impl, planes, scalars, n, logits,
                            values);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc = nn_metal_forward((NnMetal *)nn->impl, planes, scalars, n, logits,
                              values);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_forward((NnCuda *)nn->impl, planes, scalars, n, logits,
                             values);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_sample(Nn *nn, const float *logits, int n, int mode, int32_t *acts,
              float *logp, float *entropy) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc =
        nn_cpu_sample((NnCpu *)nn->impl, logits, n, mode, acts, logp, entropy);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc = nn_metal_sample((NnMetal *)nn->impl, logits, n, mode, acts, logp,
                             entropy);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_sample((NnCuda *)nn->impl, logits, n, mode, acts, logp,
                            entropy);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_update(Nn *nn, const uint8_t *planes, const float *scalars,
              const int32_t *acts, const float *old_logp,
              const float *advantages, const float *returns, int n,
              NnUpdateStats *stats) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc = nn_cpu_update((NnCpu *)nn->impl, planes, scalars, acts, old_logp,
                           advantages, returns, n, stats);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc =
        nn_metal_update((NnMetal *)nn->impl, planes, scalars, acts, old_logp,
                        advantages, returns, n, stats);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc =
        nn_cuda_update((NnCuda *)nn->impl, planes, scalars, acts, old_logp,
                       advantages, returns, n, stats);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_save(const Nn *nn, const char *path) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc = nn_cpu_save((const NnCpu *)nn->impl, path);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc = nn_metal_save((const NnMetal *)nn->impl, path);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_save((const NnCuda *)nn->impl, path);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

int nn_load(Nn *nn, const char *path) {
  if (!nn) {
    set_err("null");
    return -1;
  }
  switch (nn->backend) {
  case NN_BACKEND_CPU: {
    int rc = nn_cpu_load((NnCpu *)nn->impl, path);
    take_cpu_err(rc);
    return rc;
  }
  case NN_BACKEND_METAL: {
#if NN_HAVE_METAL
    int rc = nn_metal_load((NnMetal *)nn->impl, path);
    take_metal_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "Metal");
    return -1;
#endif
  }
  case NN_BACKEND_CUDA: {
#if NN_HAVE_CUDA
    int rc = nn_cuda_load((NnCuda *)nn->impl, path);
    take_cuda_err(rc);
    return rc;
#else
    set_errf("%s backend not available", "CUDA");
    return -1;
#endif
  }
  default:
    set_err("invalid backend");
    return -1;
  }
}

/* ---- fixture helpers: CPU internals only ---- */

int nn_fixture_get_params(const Nn *nn, float *dst, size_t count) {
  if (!nn || !dst)
    return -1;
  if (nn->backend != NN_BACKEND_CPU)
    return -1;
  return nn_cpu_fixture_get_params((const NnCpu *)nn->impl, dst, count);
}

int nn_fixture_set_params(Nn *nn, const float *src, size_t count) {
  if (!nn || !src)
    return -1;
  if (nn->backend != NN_BACKEND_CPU)
    return -1;
  return nn_cpu_fixture_set_params((NnCpu *)nn->impl, src, count);
}

int nn_fixture_get_grads(const Nn *nn, float *dst, size_t count) {
  if (!nn || !dst)
    return -1;
  if (nn->backend != NN_BACKEND_CPU)
    return -1;
  return nn_cpu_fixture_get_grads((const NnCpu *)nn->impl, dst, count);
}

size_t nn_fixture_copy_layer(const Nn *nn, int layer, int sample, float *dst,
                             size_t max_n) {
  if (!nn || !dst)
    return 0;
  if (nn->backend != NN_BACKEND_CPU)
    return 0;
  return nn_cpu_fixture_copy_layer((const NnCpu *)nn->impl, layer, sample, dst,
                                   max_n);
}
