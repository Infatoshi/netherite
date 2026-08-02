/* Fused C/CUDA rollout policy for ChainPolicy.
 *
 * Shapes (STONE chain, IRON_CHAIN=0):
 *   obs uint8 [N,18,36,64]  depth planes 7 and 16 scaled /255
 *   conv1: 18->32, 5x5 s2  -> [N,32,16,30]
 *   conv2: 32->64, 3x3 s2  -> [N,64,7,14]  flat 6272
 *   fc: 6272+27=6299 -> 256 + ReLU
 *   heads: 9 categoricals widths {3,3,3,2,2,2,7,2,10}=34 + value
 *   sample: Gumbel-argmax or greedy; joint logp + entropy
 *
 * Accuracy target: match torch fp32 within ~1e-3 abs on logits (reassociation
 * noise from a different reduction order). Not bit-identical to cuDNN.
 */
#include "cpolicy_fwd.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

/* ---- constants matching ppo_chain_cu.ChainPolicy (IRON_CHAIN=0) ---- */
enum {
  kCamH = 36,
  kCamW = 64,
  kNPlanes = 9,
  kStack = 2,
  kNCh = 18,            /* NPlanes * Stack */
  kNScal = 27,
  kNHead = 9,
  kWMax = 10,
  kCOut1 = 32,
  kCOut2 = 64,
  kK1 = 5,
  kK2 = 3,
  kS1 = 2,
  kS2 = 2,
  kH1 = 16,             /* (36-5)/2+1 */
  kW1 = 30,             /* (64-5)/2+1 */
  kH2 = 7,              /* (16-3)/2+1 */
  kW2 = 14,             /* (30-3)/2+1 */
  kFlat = 6272,         /* 64*7*14 */
  kFcIn = 6299,         /* 6272+27 */
  kFcOut = 256,
  kLogits = 34,         /* sum of head widths */
  kHeadW0 = 3, kHeadW1 = 3, kHeadW2 = 3, kHeadW3 = 2, kHeadW4 = 2,
  kHeadW5 = 2, kHeadW6 = 7, kHeadW7 = 2, kHeadW8 = 10
};

__constant__ int kHeadWDev[kNHead] = {3, 3, 3, 2, 2, 2, 7, 2, 10};
/* prefix offsets into the packed-34 logit vector */
__constant__ int kHeadOffDev[kNHead] = {0, 3, 6, 9, 11, 13, 15, 22, 24};

static char g_err[512] = "";

static void set_err(const char *msg) {
  std::snprintf(g_err, sizeof(g_err), "%s", msg);
}

const char *cpolicy_last_error(void) { return g_err; }

#define CP_CHECK(call)                                                         \
  do {                                                                         \
    cudaError_t _e = (call);                                                   \
    if (_e != cudaSuccess) {                                                   \
      std::snprintf(g_err, sizeof(g_err), "%s:%d %s", __FILE__, __LINE__,      \
                    cudaGetErrorString(_e));                                   \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CP_CHECK_BLAS(call)                                                    \
  do {                                                                         \
    cublasStatus_t _s = (call);                                                \
    if (_s != CUBLAS_STATUS_SUCCESS) {                                         \
      std::snprintf(g_err, sizeof(g_err), "%s:%d cublas %d", __FILE__,         \
                    __LINE__, (int)_s);                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

/* ---- device helpers ---- */
__device__ __forceinline__ float relu(float x) { return x > 0.f ? x : 0.f; }

/* Philox-ish mix for Gumbel uniforms (independent of torch Philox). */
__device__ __forceinline__ float u01(uint64_t seed, uint32_t a, uint32_t b,
                                    uint32_t c) {
  uint64_t x = seed ^ (uint64_t)a * 0x9E3779B97F4A7C15ULL;
  x ^= (uint64_t)b * 0xBF58476D1CE4E5B9ULL;
  x ^= (uint64_t)c * 0x94D049BB133111EBULL;
  x ^= x >> 33;
  x *= 0xFF51AFD7ED558CCDULL;
  x ^= x >> 33;
  x *= 0xC4CEB9FE1A85EC53ULL;
  x ^= x >> 33;
  /* (0,1) open interval */
  const float u = ((x >> 40) + 0.5f) * (1.0f / 16777216.0f);
  return fminf(fmaxf(u, 1e-7f), 1.f - 1e-7f);
}

__device__ __forceinline__ float gumbel0(float u) {
  /* Gumbel(0,1) = -log(-log U); clamps mirror fused_rollout. */
  float e = -logf(fmaxf(u, 1e-20f));
  e = fmaxf(e, 1e-20f);
  return -logf(e);
}

/* ---- conv1: uint8 NCHW -> float, depth /255, 5x5 s2, bias+ReLU ----
 * Each thread writes one (n, oc, oh, ow). */
__global__ void k_conv1_u8(const uint8_t *__restrict__ obs,
                           const float *__restrict__ w,
                           const float *__restrict__ b, float *__restrict__ out,
                           int n) {
  const int total = n * kCOut1 * kH1 * kW1;
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    int t = idx;
    const int ow = t % kW1;
    t /= kW1;
    const int oh = t % kH1;
    t /= kH1;
    const int oc = t % kCOut1;
    const int ni = t / kCOut1;
    const int ih0 = oh * kS1;
    const int iw0 = ow * kS1;
    float acc = b[oc];
    const float *wc = w + oc * (kNCh * kK1 * kK1);
    for (int ic = 0; ic < kNCh; ++ic) {
      const int base = ((ni * kNCh + ic) * kCamH) * kCamW;
      const bool depth = (ic == 7) || (ic == 16);
      for (int kh = 0; kh < kK1; ++kh) {
        const int ih = ih0 + kh;
        for (int kw = 0; kw < kK1; ++kw) {
          const int iw = iw0 + kw;
          float x = (float)obs[base + ih * kCamW + iw];
          if (depth)
            x *= (1.f / 255.f);
          acc += x * wc[(ic * kK1 + kh) * kK1 + kw];
        }
      }
    }
    out[((ni * kCOut1 + oc) * kH1 + oh) * kW1 + ow] = relu(acc);
  }
}

/* ---- conv2: float, 3x3 s2, bias+ReLU ---- */
__global__ void k_conv2(const float *__restrict__ in,
                        const float *__restrict__ w,
                        const float *__restrict__ b, float *__restrict__ out,
                        int n) {
  const int total = n * kCOut2 * kH2 * kW2;
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    int t = idx;
    const int ow = t % kW2;
    t /= kW2;
    const int oh = t % kH2;
    t /= kH2;
    const int oc = t % kCOut2;
    const int ni = t / kCOut2;
    const int ih0 = oh * kS2;
    const int iw0 = ow * kS2;
    float acc = b[oc];
    const float *wc = w + oc * (kCOut1 * kK2 * kK2);
    for (int ic = 0; ic < kCOut1; ++ic) {
      const int base = ((ni * kCOut1 + ic) * kH1) * kW1;
      for (int kh = 0; kh < kK2; ++kh) {
        const int ih = ih0 + kh;
        for (int kw = 0; kw < kK2; ++kw) {
          const int iw = iw0 + kw;
          acc += in[base + ih * kW1 + iw] *
                 wc[(ic * kK2 + kh) * kK2 + kw];
        }
      }
    }
    out[((ni * kCOut2 + oc) * kH2 + oh) * kW2 + ow] = relu(acc);
  }
}

/* Pack conv2 NCHW -> [N, flat] row-major and concat scal -> [N, 6299]. */
__global__ void k_pack_fc_in(const float *__restrict__ conv,
                             const float *__restrict__ scal,
                             float *__restrict__ out, int n) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * kFcIn;
       idx += blockDim.x * gridDim.x) {
    const int ni = idx / kFcIn;
    const int j = idx % kFcIn;
    if (j < kFlat) {
      /* j -> (c,h,w) with c-major as flatten after NCHW */
      const int c = j / (kH2 * kW2);
      const int rem = j % (kH2 * kW2);
      const int h = rem / kW2;
      const int w = rem % kW2;
      out[idx] = conv[((ni * kCOut2 + c) * kH2 + h) * kW2 + w];
    } else {
      out[idx] = scal[ni * kNScal + (j - kFlat)];
    }
  }
}

/* bias + ReLU on FC output [N, 256] */
__global__ void k_bias_relu(float *__restrict__ y, const float *__restrict__ b,
                            int n, int width) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * width;
       idx += blockDim.x * gridDim.x) {
    const int j = idx % width;
    y[idx] = relu(y[idx] + b[j]);
  }
}

/* bias only (heads / value) */
__global__ void k_bias(float *__restrict__ y, const float *__restrict__ b,
                       int n, int width) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * width;
       idx += blockDim.x * gridDim.x) {
    y[idx] += b[idx % width];
  }
}

/* value = hidden @ value_w^T + value_b  (small; do it in a kernel) */
__global__ void k_value(const float *__restrict__ h,
                        const float *__restrict__ w,
                        const float *__restrict__ b, float *__restrict__ out,
                        int n) {
  for (int ni = blockIdx.x * blockDim.x + threadIdx.x; ni < n;
       ni += blockDim.x * gridDim.x) {
    float acc = b[0];
    const float *hi = h + ni * kFcOut;
    for (int j = 0; j < kFcOut; ++j)
      acc += hi[j] * w[j];
    out[ni] = acc;
  }
}

/* Sample / greedy + logp + entropy over packed [N,34] logits.
 * acts written as [N,9] int64. */
__global__ void k_sample(const float *__restrict__ logits, const uint8_t *burnin,
                         const int64_t *noop, int n, int mode, uint64_t seed,
                         int64_t *__restrict__ acts, float *__restrict__ logp,
                         float *__restrict__ entropy) {
  for (int ni = blockIdx.x * blockDim.x + threadIdx.x; ni < n;
       ni += blockDim.x * gridDim.x) {
    const int bi = burnin ? (int)burnin[ni] : 0;
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    for (int h = 0; h < kNHead; ++h) {
      const int w = kHeadWDev[h];
      const int off = kHeadOffDev[h];
      const float *row = logits + ni * kLogits + off;
      /* log-sum-exp */
      float m = row[0];
      for (int c = 1; c < w; ++c)
        m = fmaxf(m, row[c]);
      float sum = 0.f;
      float ex[kWMax];
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      const float lse = m + logf(sum);
      int a;
      if (bi) {
        a = (int)noop[h];
      } else if (mode == 1) {
        /* greedy */
        a = 0;
        float best = row[0];
        for (int c = 1; c < w; ++c) {
          if (row[c] > best) {
            best = row[c];
            a = c;
          }
        }
      } else {
        /* Gumbel-argmax */
        a = 0;
        float best = row[0] + gumbel0(u01(seed, (uint32_t)ni, (uint32_t)h, 0u));
        for (int c = 1; c < w; ++c) {
          const float s =
              row[c] + gumbel0(u01(seed, (uint32_t)ni, (uint32_t)h, (uint32_t)c));
          if (s > best) {
            best = s;
            a = c;
          }
        }
      }
      acts[ni * kNHead + h] = (int64_t)a;
      lp_sum += row[a] - lse;
      /* entropy = -sum p log p */
      float eh = 0.f;
      for (int c = 0; c < w; ++c) {
        const float p = ex[c] * inv;
        if (p > 0.f)
          eh -= p * logf(p);
      }
      ent_sum += eh;
    }
    logp[ni] = lp_sum;
    if (entropy)
      entropy[ni] = ent_sum;
  }
}

/* ---- handle ---- */
struct CPolicy {
  int device = 0;
  int max_n = 0;
  cublasHandle_t blas = nullptr;

  /* weights (device) */
  float *conv1_w = nullptr, *conv1_b = nullptr;
  float *conv2_w = nullptr, *conv2_b = nullptr;
  float *fc_w = nullptr, *fc_b = nullptr;
  float *heads_w = nullptr, *heads_b = nullptr;
  float *value_w = nullptr, *value_b = nullptr;

  /* workspace (device), sized for max_n */
  float *ws_c1 = nullptr;   /* [max_n, 32, 16, 30] */
  float *ws_c2 = nullptr;   /* [max_n, 64, 7, 14] */
  float *ws_fc_in = nullptr;/* [max_n, 6299] */
  float *ws_h = nullptr;    /* [max_n, 256] */
  float *ws_logits = nullptr; /* [max_n, 34] */
  int ready = 0;
};

static int alloc_weights(CPolicy *h) {
  CP_CHECK(cudaMalloc(&h->conv1_w, sizeof(float) * kCOut1 * kNCh * kK1 * kK1));
  CP_CHECK(cudaMalloc(&h->conv1_b, sizeof(float) * kCOut1));
  CP_CHECK(cudaMalloc(&h->conv2_w, sizeof(float) * kCOut2 * kCOut1 * kK2 * kK2));
  CP_CHECK(cudaMalloc(&h->conv2_b, sizeof(float) * kCOut2));
  CP_CHECK(cudaMalloc(&h->fc_w, sizeof(float) * kFcOut * kFcIn));
  CP_CHECK(cudaMalloc(&h->fc_b, sizeof(float) * kFcOut));
  CP_CHECK(cudaMalloc(&h->heads_w, sizeof(float) * kLogits * kFcOut));
  CP_CHECK(cudaMalloc(&h->heads_b, sizeof(float) * kLogits));
  CP_CHECK(cudaMalloc(&h->value_w, sizeof(float) * kFcOut));
  CP_CHECK(cudaMalloc(&h->value_b, sizeof(float) * 1));
  return 0;
}

static int alloc_ws(CPolicy *h) {
  const size_t n = (size_t)h->max_n;
  CP_CHECK(cudaMalloc(&h->ws_c1, sizeof(float) * n * kCOut1 * kH1 * kW1));
  CP_CHECK(cudaMalloc(&h->ws_c2, sizeof(float) * n * kCOut2 * kH2 * kW2));
  CP_CHECK(cudaMalloc(&h->ws_fc_in, sizeof(float) * n * kFcIn));
  CP_CHECK(cudaMalloc(&h->ws_h, sizeof(float) * n * kFcOut));
  CP_CHECK(cudaMalloc(&h->ws_logits, sizeof(float) * n * kLogits));
  return 0;
}

static void free_all(CPolicy *h) {
  auto fre = [](void *p) {
    if (p)
      cudaFree(p);
  };
  fre(h->conv1_w);
  fre(h->conv1_b);
  fre(h->conv2_w);
  fre(h->conv2_b);
  fre(h->fc_w);
  fre(h->fc_b);
  fre(h->heads_w);
  fre(h->heads_b);
  fre(h->value_w);
  fre(h->value_b);
  fre(h->ws_c1);
  fre(h->ws_c2);
  fre(h->ws_fc_in);
  fre(h->ws_h);
  fre(h->ws_logits);
  if (h->blas)
    cublasDestroy(h->blas);
}

CPolicy *cpolicy_create(int device, int max_n) {
  g_err[0] = 0;
  if (max_n <= 0) {
    set_err("max_n must be > 0");
    return nullptr;
  }
  auto *h = new CPolicy();
  h->device = device;
  h->max_n = max_n;
  if (cudaSetDevice(device) != cudaSuccess) {
    set_err("cudaSetDevice failed");
    delete h;
    return nullptr;
  }
  if (cublasCreate(&h->blas) != CUBLAS_STATUS_SUCCESS) {
    set_err("cublasCreate failed");
    delete h;
    return nullptr;
  }
  cublasSetMathMode(h->blas, CUBLAS_DEFAULT_MATH);
  if (alloc_weights(h) != 0 || alloc_ws(h) != 0) {
    free_all(h);
    delete h;
    return nullptr;
  }
  return h;
}

void cpolicy_destroy(CPolicy *h) {
  if (!h)
    return;
  cudaSetDevice(h->device);
  free_all(h);
  delete h;
}

static int copy_blob(float *dst, const float *src, size_t n, int on_device) {
  if (on_device)
    return cudaMemcpy(dst, src, n * sizeof(float), cudaMemcpyDeviceToDevice) ==
                   cudaSuccess
               ? 0
               : -1;
  return cudaMemcpy(dst, src, n * sizeof(float), cudaMemcpyHostToDevice) ==
                 cudaSuccess
             ? 0
             : -1;
}

int cpolicy_upload_weights(CPolicy *h, const float *conv1_w,
                           const float *conv1_b, const float *conv2_w,
                           const float *conv2_b, const float *fc_w,
                           const float *fc_b, const float *heads_w,
                           const float *heads_b, const float *value_w,
                           const float *value_b, int on_device) {
  if (!h) {
    set_err("null handle");
    return -1;
  }
  CP_CHECK(cudaSetDevice(h->device));
  if (copy_blob(h->conv1_w, conv1_w, kCOut1 * kNCh * kK1 * kK1, on_device) ||
      copy_blob(h->conv1_b, conv1_b, kCOut1, on_device) ||
      copy_blob(h->conv2_w, conv2_w, kCOut2 * kCOut1 * kK2 * kK2, on_device) ||
      copy_blob(h->conv2_b, conv2_b, kCOut2, on_device) ||
      copy_blob(h->fc_w, fc_w, kFcOut * kFcIn, on_device) ||
      copy_blob(h->fc_b, fc_b, kFcOut, on_device) ||
      copy_blob(h->heads_w, heads_w, kLogits * kFcOut, on_device) ||
      copy_blob(h->heads_b, heads_b, kLogits, on_device) ||
      copy_blob(h->value_w, value_w, kFcOut, on_device) ||
      copy_blob(h->value_b, value_b, 1, on_device)) {
    set_err("weight copy failed");
    return -1;
  }
  h->ready = 1;
  return 0;
}

/* Row-major Y[N, out] = X[N, in] @ W[out, in]^T
 * via col-major cublas: Y^T = W @ X^T  (with W stored row-major = col-major
 * transposed). */
static int gemm_linear(cublasHandle_t blas, int n, int out, int in,
                       const float *W, const float *X, float *Y) {
  const float alpha = 1.f, beta = 0.f;
  /* m=out, n=N, k=in
   * A = W interpreted col-major as (in x out), OP_T -> (out x in)
   * B = X interpreted col-major as (in x N), OP_N
   * C = Y interpreted col-major as (out x N) */
  CP_CHECK_BLAS(cublasSgemm(blas, CUBLAS_OP_T, CUBLAS_OP_N, out, n, in, &alpha,
                            W, in, X, in, &beta, Y, out));
  return 0;
}

int cpolicy_forward_sample(CPolicy *h, const uint8_t *obs_u8, const float *scal,
                           const uint8_t *burnin, const int64_t *noop, int n,
                           int mode, uint64_t rng_seed, void *stream,
                           int64_t *acts, float *logp, float *value,
                           float *entropy, float *logits) {
  if (!h || !h->ready) {
    set_err("handle not ready (upload weights first)");
    return -1;
  }
  if (n <= 0 || n > h->max_n) {
    set_err("n out of range");
    return -1;
  }
  if (!obs_u8 || !scal || !noop || !acts || !logp || !value) {
    set_err("null required pointer");
    return -1;
  }
  CP_CHECK(cudaSetDevice(h->device));
  cudaStream_t st = (cudaStream_t)stream;
  CP_CHECK_BLAS(cublasSetStream(h->blas, st));

  const int thr = 256;
  auto grid = [&](int work) {
    return (work + thr - 1) / thr;
  };

  /* conv1 */
  {
    const int work = n * kCOut1 * kH1 * kW1;
    k_conv1_u8<<<grid(work), thr, 0, st>>>(obs_u8, h->conv1_w, h->conv1_b,
                                           h->ws_c1, n);
  }
  /* conv2 */
  {
    const int work = n * kCOut2 * kH2 * kW2;
    k_conv2<<<grid(work), thr, 0, st>>>(h->ws_c1, h->conv2_w, h->conv2_b,
                                        h->ws_c2, n);
  }
  /* pack flat + scal */
  {
    const int work = n * kFcIn;
    k_pack_fc_in<<<grid(work), thr, 0, st>>>(h->ws_c2, scal, h->ws_fc_in, n);
  }
  /* FC GEMM + bias + ReLU */
  if (gemm_linear(h->blas, n, kFcOut, kFcIn, h->fc_w, h->ws_fc_in, h->ws_h))
    return -1;
  {
    const int work = n * kFcOut;
    k_bias_relu<<<grid(work), thr, 0, st>>>(h->ws_h, h->fc_b, n, kFcOut);
  }
  /* heads GEMM + bias */
  if (gemm_linear(h->blas, n, kLogits, kFcOut, h->heads_w, h->ws_h,
                  h->ws_logits))
    return -1;
  {
    const int work = n * kLogits;
    k_bias<<<grid(work), thr, 0, st>>>(h->ws_logits, h->heads_b, n, kLogits);
  }
  /* value */
  k_value<<<grid(n), thr, 0, st>>>(h->ws_h, h->value_w, h->value_b, value, n);

  /* optional logits copy for equiv gate */
  if (logits) {
    CP_CHECK(cudaMemcpyAsync(logits, h->ws_logits,
                             sizeof(float) * (size_t)n * kLogits,
                             cudaMemcpyDeviceToDevice, st));
  }

  /* sample */
  k_sample<<<grid(n), thr, 0, st>>>(h->ws_logits, burnin, noop, n, mode,
                                    rng_seed, acts, logp, entropy);

  CP_CHECK(cudaGetLastError());
  return 0;
}
