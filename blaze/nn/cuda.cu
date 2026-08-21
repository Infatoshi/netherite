/* FP32 CUDA Blaze policy backend.
 * cuDNN convs, cuBLAS dense, custom kernels for the rest.
 * Fixed arenas at create; no allocate/free in forward/sample/update. */
#include "cuda.h"
#include "fixture.h"
#include "model.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <cudnn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const float kAdamBeta1 = 0.9f;
static const float kAdamBeta2 = 0.999f;
static const float kAdamEps = 1e-8f;

static char g_err[512] = "";

static void set_err(const char *msg) {
  std::snprintf(g_err, sizeof(g_err), "%s", msg);
}

const char *nn_cuda_last_error(void) { return g_err; }

#define CU_CHECK(call)                                                         \
  do {                                                                         \
    cudaError_t _e = (call);                                                   \
    if (_e != cudaSuccess) {                                                   \
      std::snprintf(g_err, sizeof(g_err), "%s:%d %s", __FILE__, __LINE__,      \
                    cudaGetErrorString(_e));                                   \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define BLAS_CHECK(call)                                                       \
  do {                                                                         \
    cublasStatus_t _s = (call);                                                \
    if (_s != CUBLAS_STATUS_SUCCESS) {                                         \
      std::snprintf(g_err, sizeof(g_err), "%s:%d cublas %d", __FILE__,         \
                    __LINE__, (int)_s);                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define DNN_CHECK(call)                                                        \
  do {                                                                         \
    cudnnStatus_t _s = (call);                                                 \
    if (_s != CUDNN_STATUS_SUCCESS) {                                          \
      std::snprintf(g_err, sizeof(g_err), "%s:%d cudnn %s", __FILE__,          \
                    __LINE__, cudnnGetErrorString(_s));                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

/* ---- validation ---- */

static int validate_config(const NnConfig *cfg) {
  if (!cfg) {
    set_err("null config");
    return -1;
  }
  if (!std::isfinite(cfg->lr) || !std::isfinite(cfg->ppo_clip) ||
      !std::isfinite(cfg->value_coef) || !std::isfinite(cfg->entropy_coef) ||
      !std::isfinite(cfg->grad_limit)) {
    set_err("config field is non-finite");
    return -1;
  }
  if (cfg->lr < 0.f) {
    set_err("lr must be >= 0");
    return -1;
  }
  if (cfg->ppo_clip < 0.f || cfg->ppo_clip >= 1.f) {
    set_err("ppo_clip must be >= 0 and < 1");
    return -1;
  }
  if (cfg->value_coef < 0.f) {
    set_err("value_coef must be >= 0");
    return -1;
  }
  if (cfg->entropy_coef < 0.f) {
    set_err("entropy_coef must be >= 0");
    return -1;
  }
  if (!(cfg->grad_limit > 0.f)) {
    set_err("grad_limit must be > 0");
    return -1;
  }
  return 0;
}

static int validate_actions(const int32_t *acts, int n) {
  for (int ni = 0; ni < n; ++ni) {
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int32_t a = acts[(size_t)ni * NN_N_HEAD + h];
      if (a < 0 || a >= NN_HEAD_WIDTHS[h]) {
        std::snprintf(g_err, sizeof(g_err),
                      "action out of range: sample %d head %d value %d width %d",
                      ni, h, (int)a, NN_HEAD_WIDTHS[h]);
        return -1;
      }
    }
  }
  return 0;
}

static size_t tensor_offset(int tid) {
  size_t off = 0;
  for (int i = 0; i < tid; ++i)
    off += NN_TENSOR_FLOATS[i];
  return off;
}

/* ---- device constants ---- */
__constant__ int kHeadWDev[NN_N_HEAD];
__constant__ int kHeadOffDev[NN_N_HEAD];

__device__ __forceinline__ float d_hash_u01(uint64_t seed, uint32_t a,
                                           uint32_t b, uint32_t c) {
  uint64_t x = seed ^ (uint64_t)a * 0x9E3779B97F4A7C15ULL;
  x ^= (uint64_t)b * 0xBF58476D1CE4E5B9ULL;
  x ^= (uint64_t)c * 0x94D049BB133111EBULL;
  x ^= x >> 33;
  x *= 0xFF51AFD7ED558CCDULL;
  x ^= x >> 33;
  x *= 0xC4CEB9FE1A85EC53ULL;
  x ^= x >> 33;
  const float u = ((x >> 40) + 0.5f) * (1.0f / 16777216.0f);
  return fminf(fmaxf(u, 1e-7f), 1.f - 1e-7f);
}

__device__ __forceinline__ float d_gumbel0(float u) {
  float e = -logf(fmaxf(u, 1e-20f));
  e = fmaxf(e, 1e-20f);
  return -logf(e);
}

/* ---- kernels ---- */

__global__ void k_obs_to_float(const uint8_t *__restrict__ obs,
                               float *__restrict__ out, int n) {
  const int total = n * NN_N_CH * NN_CAM_H * NN_CAM_W;
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    int t = idx;
    t /= NN_CAM_W;
    t /= NN_CAM_H;
    const int c = t % NN_N_CH;
    float x = (float)obs[idx];
    if (c == NN_DEPTH_CH0 || c == NN_DEPTH_CH1)
      x *= (1.f / 255.f);
    out[idx] = x;
  }
}

__global__ void k_add_bias_nchw(float *__restrict__ y,
                                const float *__restrict__ b, int n, int c,
                                int h, int w) {
  const int total = n * c * h * w;
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    const int ch = (idx / (h * w)) % c;
    y[idx] += b[ch];
  }
}

__global__ void k_relu_store_pre(float *__restrict__ y,
                                 float *__restrict__ pre, int total) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    const float v = y[idx];
    pre[idx] = v;
    y[idx] = v > 0.f ? v : 0.f;
  }
}

__global__ void k_pack_fc_in(const float *__restrict__ conv,
                             const float *__restrict__ scal,
                             float *__restrict__ out, int n) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * NN_FC_IN;
       idx += blockDim.x * gridDim.x) {
    const int ni = idx / NN_FC_IN;
    const int j = idx % NN_FC_IN;
    if (j < NN_FLAT) {
      const int c = j / (NN_H2 * NN_W2);
      const int rem = j % (NN_H2 * NN_W2);
      const int h = rem / NN_W2;
      const int w = rem % NN_W2;
      out[idx] =
          conv[(((size_t)ni * NN_C_OUT2 + c) * NN_H2 + h) * NN_W2 + w];
    } else {
      out[idx] = scal[(size_t)ni * NN_N_SCAL + (j - NN_FLAT)];
    }
  }
}

__global__ void k_bias_relu_store_pre(float *__restrict__ y,
                                      const float *__restrict__ b,
                                      float *__restrict__ pre, int n,
                                      int width) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * width;
       idx += blockDim.x * gridDim.x) {
    const int j = idx % width;
    const float v = y[idx] + b[j];
    pre[idx] = v;
    y[idx] = v > 0.f ? v : 0.f;
  }
}

__global__ void k_bias_row(float *__restrict__ y, const float *__restrict__ b,
                           int n, int width) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < n * width;
       idx += blockDim.x * gridDim.x)
    y[idx] += b[idx % width];
}

__global__ void k_value_fwd(const float *__restrict__ h,
                            const float *__restrict__ w,
                            const float *__restrict__ b, float *__restrict__ out,
                            int n) {
  for (int ni = blockIdx.x * blockDim.x + threadIdx.x; ni < n;
       ni += blockDim.x * gridDim.x) {
    float acc = b[0];
    const float *hi = h + (size_t)ni * NN_FC_OUT;
    for (int j = 0; j < NN_FC_OUT; ++j)
      acc += hi[j] * w[j];
    out[ni] = acc;
  }
}

__global__ void k_sample(const float *__restrict__ logits, int n, int mode,
                         uint64_t seed, int32_t *__restrict__ acts,
                         float *__restrict__ logp, float *__restrict__ entropy) {
  for (int ni = blockIdx.x * blockDim.x + threadIdx.x; ni < n;
       ni += blockDim.x * gridDim.x) {
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int w = kHeadWDev[h];
      const int off = kHeadOffDev[h];
      const float *row = logits + (size_t)ni * NN_N_LOGITS + off;
      float m = row[0];
      for (int c = 1; c < w; ++c)
        m = fmaxf(m, row[c]);
      float ex[NN_W_MAX];
      float sum = 0.f;
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      const float lse = m + logf(sum);
      int a = 0;
      if (mode == NN_SAMPLE_GREEDY) {
        float best = row[0];
        for (int c = 1; c < w; ++c) {
          if (row[c] > best) {
            best = row[c];
            a = c;
          }
        }
      } else {
        float best =
            row[0] + d_gumbel0(d_hash_u01(seed, (uint32_t)ni, (uint32_t)h, 0u));
        for (int c = 1; c < w; ++c) {
          const float s =
              row[c] +
              d_gumbel0(d_hash_u01(seed, (uint32_t)ni, (uint32_t)h, (uint32_t)c));
          if (s > best) {
            best = s;
            a = c;
          }
        }
      }
      acts[(size_t)ni * NN_N_HEAD + h] = a;
      lp_sum += row[a] - lse;
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

__global__ void k_ppo_dlogits(const float *__restrict__ logits,
                              const int32_t *__restrict__ acts,
                              const float *__restrict__ old_logp,
                              const float *__restrict__ advantages,
                              const float *__restrict__ returns,
                              const float *__restrict__ values, int n,
                              float clip, float value_coef, float entropy_coef,
                              float *__restrict__ d_logits,
                              float *__restrict__ d_value,
                              float *__restrict__ stats_buf) {
  for (int ni = blockIdx.x * blockDim.x + threadIdx.x; ni < n;
       ni += blockDim.x * gridDim.x) {
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    float p_all[NN_N_HEAD][NN_W_MAX];
    float eh_all[NN_N_HEAD];
    int a_all[NN_N_HEAD];

    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int w = kHeadWDev[h];
      const int off = kHeadOffDev[h];
      const float *row = logits + (size_t)ni * NN_N_LOGITS + off;
      float m = row[0];
      for (int c = 1; c < w; ++c)
        m = fmaxf(m, row[c]);
      float ex[NN_W_MAX];
      float sum = 0.f;
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      float eh = 0.f;
      for (int c = 0; c < w; ++c) {
        p_all[h][c] = ex[c] * inv;
        if (p_all[h][c] > 0.f)
          eh -= p_all[h][c] * logf(p_all[h][c]);
      }
      eh_all[h] = eh;
      const int a = acts[(size_t)ni * NN_N_HEAD + h];
      a_all[h] = a;
      lp_sum += row[a] - (m + logf(sum));
      ent_sum += eh;
    }

    const float inv_n = 1.f / (float)n;
    const float ratio = expf(lp_sum - old_logp[ni]);
    const float adv = advantages[ni];
    const float surr1 = ratio * adv;
    float r_clip = ratio;
    if (r_clip < 1.f - clip)
      r_clip = 1.f - clip;
    if (r_clip > 1.f + clip)
      r_clip = 1.f + clip;
    const float surr2 = r_clip * adv;
    const float obj = surr1 < surr2 ? surr1 : surr2;

    float dobj_dratio;
    if (surr1 < surr2)
      dobj_dratio = adv;
    else if (surr2 < surr1) {
      if (ratio < 1.f - clip || ratio > 1.f + clip)
        dobj_dratio = 0.f;
      else
        dobj_dratio = adv;
    } else
      dobj_dratio = adv;

    const float d_logp = -dobj_dratio * ratio * inv_n;
    const float d_ent = -entropy_coef * inv_n;
    const float diff = values[ni] - returns[ni];
    d_value[ni] = value_coef * 2.f * diff * inv_n;

    atomicAdd(&stats_buf[0], -obj);
    atomicAdd(&stats_buf[1], diff * diff);
    atomicAdd(&stats_buf[2], ent_sum);

    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int w = kHeadWDev[h];
      const int off = kHeadOffDev[h];
      float *drow = d_logits + (size_t)ni * NN_N_LOGITS + off;
      const int a = a_all[h];
      const float eh = eh_all[h];
      for (int c = 0; c < w; ++c) {
        const float p = p_all[h][c];
        float g = d_logp * (((c == a) ? 1.f : 0.f) - p);
        if (p > 0.f)
          g += d_ent * (-p * (logf(p) + eh));
        drow[c] = g;
      }
    }

    /* Diagnostic only (stats_buf[3], [4]). After all d_* stores. */
    atomicAdd(&stats_buf[3], ratio - 1.f - logf(ratio));
    if (fabsf(ratio - 1.f) > clip)
      atomicAdd(&stats_buf[4], 1.f);
  }
}

__global__ void k_relu_bwd(float *__restrict__ dy, const float *__restrict__ pre,
                           int total) {
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    if (pre[idx] <= 0.f)
      dy[idx] = 0.f;
  }
}

__global__ void k_unpack_fc_in_bwd(const float *__restrict__ d_fc_in,
                                   float *__restrict__ d_conv2, int n) {
  const int total = n * NN_C_OUT2 * NN_H2 * NN_W2;
  for (int idx = blockIdx.x * blockDim.x + threadIdx.x; idx < total;
       idx += blockDim.x * gridDim.x) {
    int t = idx;
    const int w = t % NN_W2;
    t /= NN_W2;
    const int h = t % NN_H2;
    t /= NN_H2;
    const int c = t % NN_C_OUT2;
    const int ni = t / NN_C_OUT2;
    const int j = c * (NN_H2 * NN_W2) + h * NN_W2 + w;
    d_conv2[idx] = d_fc_in[(size_t)ni * NN_FC_IN + j];
  }
}

__global__ void k_zero(float *p, size_t n) {
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < n;
       i += (size_t)blockDim.x * gridDim.x)
    p[i] = 0.f;
}

__global__ void k_sumsq_partial(const float *__restrict__ g, size_t n,
                                float *__restrict__ partial) {
  extern __shared__ float sh[];
  float sum = 0.f;
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < n;
       i += (size_t)blockDim.x * gridDim.x) {
    const float v = g[i];
    sum += v * v;
  }
  sh[threadIdx.x] = sum;
  __syncthreads();
  for (int s = blockDim.x / 2; s > 0; s >>= 1) {
    if (threadIdx.x < s)
      sh[threadIdx.x] += sh[threadIdx.x + s];
    __syncthreads();
  }
  if (threadIdx.x == 0)
    partial[blockIdx.x] = sh[0];
}

__global__ void k_sum_partials(const float *__restrict__ partial, int n,
                               float *__restrict__ out) {
  float s = 0.f;
  for (int i = threadIdx.x; i < n; i += blockDim.x)
    s += partial[i];
  __shared__ float sh[256];
  sh[threadIdx.x] = s;
  __syncthreads();
  for (int k = blockDim.x / 2; k > 0; k >>= 1) {
    if (threadIdx.x < k)
      sh[threadIdx.x] += sh[threadIdx.x + k];
    __syncthreads();
  }
  if (threadIdx.x == 0)
    out[0] = sh[0];
}

__global__ void k_scale(float *__restrict__ g, size_t n, float scale) {
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < n;
       i += (size_t)blockDim.x * gridDim.x)
    g[i] *= scale;
}

__global__ void k_adam(float *__restrict__ w, float *__restrict__ m,
                       float *__restrict__ v, const float *__restrict__ g,
                       size_t n, float lr, float bc1, float bc2) {
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < n;
       i += (size_t)blockDim.x * gridDim.x) {
    const float gi = g[i];
    float mi = kAdamBeta1 * m[i] + (1.f - kAdamBeta1) * gi;
    float vi = kAdamBeta2 * v[i] + (1.f - kAdamBeta2) * gi * gi;
    m[i] = mi;
    v[i] = vi;
    const float mhat = mi / bc1;
    const float vhat = vi / bc2;
    w[i] -= lr * mhat / (sqrtf(vhat) + kAdamEps);
  }
}

__global__ void k_value_bwd(const float *__restrict__ h,
                            const float *__restrict__ w,
                            const float *__restrict__ d_value,
                            float *__restrict__ d_hidden,
                            float *__restrict__ gw, float *__restrict__ gb,
                            int n) {
  for (int ni = blockIdx.x; ni < n; ni += gridDim.x) {
    const float g = d_value[ni];
    if (threadIdx.x == 0)
      atomicAdd(&gb[0], g);
    const float *hi = h + (size_t)ni * NN_FC_OUT;
    float *dh = d_hidden + (size_t)ni * NN_FC_OUT;
    for (int i = threadIdx.x; i < NN_FC_OUT; i += blockDim.x) {
      atomicAdd(&gw[i], g * hi[i]);
      atomicAdd(&dh[i], g * w[i]);
    }
  }
}

__global__ void k_bias_grad_rows(const float *__restrict__ dy,
                                 float *__restrict__ gb, int n, int width) {
  for (int o = blockIdx.x * blockDim.x + threadIdx.x; o < width;
       o += blockDim.x * gridDim.x) {
    float s = 0.f;
    for (int ni = 0; ni < n; ++ni)
      s += dy[(size_t)ni * width + o];
    gb[o] += s;
  }
}

/* ---- handle ---- */
struct NnCuda {
  int max_n;
  int device;
  NnConfig cfg;
  uint64_t sample_step; /* Gumbel decision nonce; not reset on lr-only set_config */
  int64_t adam_t;
  size_t n_params;
  int desc_n;

  cublasHandle_t blas;
  cudnnHandle_t dnn;

  float *h_params;
  float *h_t[NN_T_COUNT];

  float *d_params;
  float *d_grads;
  float *d_adam_m;
  float *d_adam_v;
  float *d_t[NN_T_COUNT];
  float *d_g[NN_T_COUNT];

  /* host staging */
  uint8_t *h_planes;
  float *h_scalars;
  float *h_logits;
  float *h_values;
  int32_t *h_acts;
  float *h_logp;
  float *h_entropy;
  float *h_old_logp;
  float *h_adv;
  float *h_ret;
  float *h_scratch; /* stats etc. */

  /* device IO / acts */
  uint8_t *d_planes;
  float *d_scalars;
  float *d_obs;
  float *d_pre_c1;
  float *d_act_c1;
  float *d_pre_c2;
  float *d_act_c2;
  float *d_fc_in;
  float *d_pre_h;
  float *d_act_h;
  float *d_logits;
  float *d_value;
  int32_t *d_acts;
  float *d_logp;
  float *d_entropy;
  float *d_old_logp;
  float *d_adv;
  float *d_ret;
  float *d_dlogits;
  float *d_dvalue;
  float *d_dhidden;
  float *d_dfc_in;
  float *d_dc2;
  float *d_dc1;
  float *d_stats;
  float *d_reduce;
  float *d_norm;
  int reduce_blocks;

  void *d_ws_conv;
  size_t ws_conv_bytes;

  cudnnTensorDescriptor_t x1_desc, y1_desc, b1_desc;
  cudnnTensorDescriptor_t x2_desc, y2_desc, b2_desc;
  cudnnFilterDescriptor_t f1_desc, f2_desc;
  cudnnConvolutionDescriptor_t conv1_desc, conv2_desc;

  cudnnConvolutionFwdAlgo_t algo_f1, algo_f2;
  cudnnConvolutionBwdDataAlgo_t algo_bd2;
  cudnnConvolutionBwdFilterAlgo_t algo_bf1, algo_bf2;
};

static int grid_for(int work, int thr = 256) {
  if (work <= 0)
    return 1;
  return (work + thr - 1) / thr;
}

static int grid_for_sz(size_t work, int thr = 256) {
  if (work == 0)
    return 1;
  size_t g = (work + (size_t)thr - 1) / (size_t)thr;
  if (g > 65535)
    g = 65535;
  return (int)g;
}

/* Y[N,out] = X[N,in] @ W[out,in]^T */
static int gemm_fwd(cublasHandle_t blas, int n, int out, int in, const float *W,
                    const float *X, float *Y) {
  const float alpha = 1.f, beta = 0.f;
  BLAS_CHECK(cublasSgemm(blas, CUBLAS_OP_T, CUBLAS_OP_N, out, n, in, &alpha, W,
                         in, X, in, &beta, Y, out));
  return 0;
}

/* dW[out,in] += dY^T @ X */
static int gemm_dw(cublasHandle_t blas, int n, int out, int in, const float *dY,
                   const float *X, float *dW) {
  const float alpha = 1.f, beta = 1.f;
  BLAS_CHECK(cublasSgemm(blas, CUBLAS_OP_N, CUBLAS_OP_T, in, out, n, &alpha, X,
                         in, dY, out, &beta, dW, in));
  return 0;
}

/* dX[N,in] = dY @ W */
static int gemm_dx(cublasHandle_t blas, int n, int out, int in, const float *W,
                   const float *dY, float *dX) {
  const float alpha = 1.f, beta = 0.f;
  BLAS_CHECK(cublasSgemm(blas, CUBLAS_OP_N, CUBLAS_OP_N, in, n, out, &alpha, W,
                         in, dY, out, &beta, dX, in));
  return 0;
}

static int set_batch_descs(NnCuda *nn, int n) {
  if (n == nn->desc_n)
    return 0;
  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->x1_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, n, NN_N_CH, NN_CAM_H,
                                       NN_CAM_W));
  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->y1_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, n, NN_C_OUT1, NN_H1,
                                       NN_W1));
  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->x2_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, n, NN_C_OUT1, NN_H1,
                                       NN_W1));
  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->y2_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, n, NN_C_OUT2, NN_H2,
                                       NN_W2));
  nn->desc_n = n;
  return 0;
}

template <typename PerfT, typename AlgoT, typename GetAlgoFn, typename GetWsFn>
static int pick_algo_generic(NnCuda *nn, GetAlgoFn get_algo, GetWsFn get_ws,
                             AlgoT *algo_out, size_t *ws_need,
                             bool sizing_pass) {
  PerfT perf[10];
  int returned = 0;
  cudnnStatus_t st = get_algo(perf, &returned);
  if (st != CUDNN_STATUS_SUCCESS || returned <= 0) {
    set_err("no cudnn algorithm returned");
    return -1;
  }
  int best = -1;
  size_t best_ws = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < returned; ++i) {
      if (perf[i].status != CUDNN_STATUS_SUCCESS)
        continue;
      if (pass == 0 && perf[i].determinism != CUDNN_DETERMINISTIC)
        continue;
      size_t cand = 0;
      if (get_ws(perf[i].algo, &cand) != CUDNN_STATUS_SUCCESS)
        continue;
      if (!sizing_pass && nn->d_ws_conv && cand > nn->ws_conv_bytes)
        continue;
      best = i;
      best_ws = cand;
      break;
    }
    if (best >= 0)
      break;
  }
  if (best < 0) {
    set_err("no cudnn algorithm within workspace");
    return -1;
  }
  *algo_out = perf[best].algo;
  *ws_need = best_ws;
  return 0;
}

static int pick_batch_algos(NnCuda *nn, bool sizing_pass) {
  size_t need = 0;
  size_t ws = 0;

  auto fwd1 = [&](cudnnConvolutionFwdAlgoPerf_t *perf, int *ret) {
    return cudnnGetConvolutionForwardAlgorithm_v7(
        nn->dnn, nn->x1_desc, nn->f1_desc, nn->conv1_desc, nn->y1_desc, 10, ret,
        perf);
  };
  auto fwd1ws = [&](cudnnConvolutionFwdAlgo_t a, size_t *o) {
    return cudnnGetConvolutionForwardWorkspaceSize(
        nn->dnn, nn->x1_desc, nn->f1_desc, nn->conv1_desc, nn->y1_desc, a, o);
  };
  if (pick_algo_generic<cudnnConvolutionFwdAlgoPerf_t,
                        cudnnConvolutionFwdAlgo_t>(nn, fwd1, fwd1ws,
                                                   &nn->algo_f1, &ws,
                                                   sizing_pass) != 0)
    return -1;
  if (ws > need)
    need = ws;

  auto fwd2 = [&](cudnnConvolutionFwdAlgoPerf_t *perf, int *ret) {
    return cudnnGetConvolutionForwardAlgorithm_v7(
        nn->dnn, nn->x2_desc, nn->f2_desc, nn->conv2_desc, nn->y2_desc, 10, ret,
        perf);
  };
  auto fwd2ws = [&](cudnnConvolutionFwdAlgo_t a, size_t *o) {
    return cudnnGetConvolutionForwardWorkspaceSize(
        nn->dnn, nn->x2_desc, nn->f2_desc, nn->conv2_desc, nn->y2_desc, a, o);
  };
  if (pick_algo_generic<cudnnConvolutionFwdAlgoPerf_t,
                        cudnnConvolutionFwdAlgo_t>(nn, fwd2, fwd2ws,
                                                   &nn->algo_f2, &ws,
                                                   sizing_pass) != 0)
    return -1;
  if (ws > need)
    need = ws;

  auto bf1 = [&](cudnnConvolutionBwdFilterAlgoPerf_t *perf, int *ret) {
    return cudnnGetConvolutionBackwardFilterAlgorithm_v7(
        nn->dnn, nn->x1_desc, nn->y1_desc, nn->conv1_desc, nn->f1_desc, 10, ret,
        perf);
  };
  auto bf1ws = [&](cudnnConvolutionBwdFilterAlgo_t a, size_t *o) {
    return cudnnGetConvolutionBackwardFilterWorkspaceSize(
        nn->dnn, nn->x1_desc, nn->y1_desc, nn->conv1_desc, nn->f1_desc, a, o);
  };
  if (pick_algo_generic<cudnnConvolutionBwdFilterAlgoPerf_t,
                        cudnnConvolutionBwdFilterAlgo_t>(
          nn, bf1, bf1ws, &nn->algo_bf1, &ws, sizing_pass) != 0)
    return -1;
  if (ws > need)
    need = ws;

  auto bf2 = [&](cudnnConvolutionBwdFilterAlgoPerf_t *perf, int *ret) {
    return cudnnGetConvolutionBackwardFilterAlgorithm_v7(
        nn->dnn, nn->x2_desc, nn->y2_desc, nn->conv2_desc, nn->f2_desc, 10, ret,
        perf);
  };
  auto bf2ws = [&](cudnnConvolutionBwdFilterAlgo_t a, size_t *o) {
    return cudnnGetConvolutionBackwardFilterWorkspaceSize(
        nn->dnn, nn->x2_desc, nn->y2_desc, nn->conv2_desc, nn->f2_desc, a, o);
  };
  if (pick_algo_generic<cudnnConvolutionBwdFilterAlgoPerf_t,
                        cudnnConvolutionBwdFilterAlgo_t>(
          nn, bf2, bf2ws, &nn->algo_bf2, &ws, sizing_pass) != 0)
    return -1;
  if (ws > need)
    need = ws;

  auto bd2 = [&](cudnnConvolutionBwdDataAlgoPerf_t *perf, int *ret) {
    return cudnnGetConvolutionBackwardDataAlgorithm_v7(
        nn->dnn, nn->f2_desc, nn->y2_desc, nn->conv2_desc, nn->x2_desc, 10, ret,
        perf);
  };
  auto bd2ws = [&](cudnnConvolutionBwdDataAlgo_t a, size_t *o) {
    return cudnnGetConvolutionBackwardDataWorkspaceSize(
        nn->dnn, nn->f2_desc, nn->y2_desc, nn->conv2_desc, nn->x2_desc, a, o);
  };
  if (pick_algo_generic<cudnnConvolutionBwdDataAlgoPerf_t,
                        cudnnConvolutionBwdDataAlgo_t>(
          nn, bd2, bd2ws, &nn->algo_bd2, &ws, sizing_pass) != 0)
    return -1;
  if (ws > need)
    need = ws;

  if (sizing_pass) {
    if (need < 1)
      need = 1;
    nn->ws_conv_bytes = need;
  } else if (need > nn->ws_conv_bytes) {
    std::snprintf(g_err, sizeof(g_err),
                  "cudnn workspace for batch %d exceeds create-time arena "
                  "(%zu > %zu)",
                  nn->desc_n, need, nn->ws_conv_bytes);
    return -1;
  }
  return 0;
}

static int ensure_batch(NnCuda *nn, int n) {
  if (set_batch_descs(nn, n) != 0)
    return -1;
  if (pick_batch_algos(nn, false) != 0)
    return -1;
  return 0;
}

static int bind_device(const NnCuda *nn) {
  if (!nn) {
    set_err("null handle");
    return -1;
  }
  if (cudaSetDevice(nn->device) != cudaSuccess) {
    set_err("cross-device or invalid device");
    return -1;
  }
  return 0;
}

static int forward_device(NnCuda *nn, int n) {
  const int thr = 256;
  if (ensure_batch(nn, n) != 0)
    return -1;

  {
    const int work = n * NN_N_CH * NN_CAM_H * NN_CAM_W;
    k_obs_to_float<<<grid_for(work), thr>>>(nn->d_planes, nn->d_obs, n);
  }

  const float alpha = 1.f, beta0 = 0.f;
  DNN_CHECK(cudnnConvolutionForward(
      nn->dnn, &alpha, nn->x1_desc, nn->d_obs, nn->f1_desc,
      nn->d_t[NN_T_CONV1_W], nn->conv1_desc, nn->algo_f1, nn->d_ws_conv,
      nn->ws_conv_bytes, &beta0, nn->y1_desc, nn->d_act_c1));
  {
    const int work = n * NN_C_OUT1 * NN_H1 * NN_W1;
    k_add_bias_nchw<<<grid_for(work), thr>>>(nn->d_act_c1,
                                             nn->d_t[NN_T_CONV1_B], n, NN_C_OUT1,
                                             NN_H1, NN_W1);
    k_relu_store_pre<<<grid_for(work), thr>>>(nn->d_act_c1, nn->d_pre_c1, work);
  }

  DNN_CHECK(cudnnConvolutionForward(
      nn->dnn, &alpha, nn->x2_desc, nn->d_act_c1, nn->f2_desc,
      nn->d_t[NN_T_CONV2_W], nn->conv2_desc, nn->algo_f2, nn->d_ws_conv,
      nn->ws_conv_bytes, &beta0, nn->y2_desc, nn->d_act_c2));
  {
    const int work = n * NN_C_OUT2 * NN_H2 * NN_W2;
    k_add_bias_nchw<<<grid_for(work), thr>>>(nn->d_act_c2,
                                             nn->d_t[NN_T_CONV2_B], n, NN_C_OUT2,
                                             NN_H2, NN_W2);
    k_relu_store_pre<<<grid_for(work), thr>>>(nn->d_act_c2, nn->d_pre_c2, work);
  }

  {
    const int work = n * NN_FC_IN;
    k_pack_fc_in<<<grid_for(work), thr>>>(nn->d_act_c2, nn->d_scalars,
                                          nn->d_fc_in, n);
  }

  if (gemm_fwd(nn->blas, n, NN_FC_OUT, NN_FC_IN, nn->d_t[NN_T_FC_W], nn->d_fc_in,
               nn->d_act_h) != 0)
    return -1;
  {
    const int work = n * NN_FC_OUT;
    k_bias_relu_store_pre<<<grid_for(work), thr>>>(
        nn->d_act_h, nn->d_t[NN_T_FC_B], nn->d_pre_h, n, NN_FC_OUT);
  }

  if (gemm_fwd(nn->blas, n, NN_N_LOGITS, NN_FC_OUT, nn->d_t[NN_T_HEADS_W],
               nn->d_act_h, nn->d_logits) != 0)
    return -1;
  {
    const int work = n * NN_N_LOGITS;
    k_bias_row<<<grid_for(work), thr>>>(nn->d_logits, nn->d_t[NN_T_HEADS_B], n,
                                        NN_N_LOGITS);
  }

  k_value_fwd<<<grid_for(n), thr>>>(nn->d_act_h, nn->d_t[NN_T_VALUE_W],
                                    nn->d_t[NN_T_VALUE_B], nn->d_value, n);

  CU_CHECK(cudaGetLastError());
  return 0;
}

static int backward_device(NnCuda *nn, int n) {
  const int thr = 256;
  const float alpha = 1.f, beta0 = 0.f, beta1 = 1.f;

  /* heads: dW, db, d_hidden = d_logits @ W */
  {
    const int work = n * NN_FC_OUT;
    k_zero<<<grid_for(work), thr>>>(nn->d_dhidden, (size_t)work);
  }
  if (gemm_dw(nn->blas, n, NN_N_LOGITS, NN_FC_OUT, nn->d_dlogits, nn->d_act_h,
              nn->d_g[NN_T_HEADS_W]) != 0)
    return -1;
  k_bias_grad_rows<<<grid_for(NN_N_LOGITS), thr>>>(
      nn->d_dlogits, nn->d_g[NN_T_HEADS_B], n, NN_N_LOGITS);
  if (gemm_dx(nn->blas, n, NN_N_LOGITS, NN_FC_OUT, nn->d_t[NN_T_HEADS_W],
              nn->d_dlogits, nn->d_dhidden) != 0)
    return -1;

  /* value bwd accumulates into d_hidden */
  k_value_bwd<<<n, thr>>>(nn->d_act_h, nn->d_t[NN_T_VALUE_W], nn->d_dvalue,
                          nn->d_dhidden, nn->d_g[NN_T_VALUE_W],
                          nn->d_g[NN_T_VALUE_B], n);

  /* FC ReLU bwd + gemm */
  {
    const int work = n * NN_FC_OUT;
    k_relu_bwd<<<grid_for(work), thr>>>(nn->d_dhidden, nn->d_pre_h, work);
  }
  if (gemm_dw(nn->blas, n, NN_FC_OUT, NN_FC_IN, nn->d_dhidden, nn->d_fc_in,
              nn->d_g[NN_T_FC_W]) != 0)
    return -1;
  k_bias_grad_rows<<<grid_for(NN_FC_OUT), thr>>>(nn->d_dhidden,
                                                 nn->d_g[NN_T_FC_B], n,
                                                 NN_FC_OUT);
  if (gemm_dx(nn->blas, n, NN_FC_OUT, NN_FC_IN, nn->d_t[NN_T_FC_W],
              nn->d_dhidden, nn->d_dfc_in) != 0)
    return -1;

  {
    const int work = n * NN_C_OUT2 * NN_H2 * NN_W2;
    k_unpack_fc_in_bwd<<<grid_for(work), thr>>>(nn->d_dfc_in, nn->d_dc2, n);
    k_relu_bwd<<<grid_for(work), thr>>>(nn->d_dc2, nn->d_pre_c2, work);
  }

  /* conv2 bias / filter / data */
  DNN_CHECK(cudnnConvolutionBackwardBias(nn->dnn, &alpha, nn->y2_desc,
                                          nn->d_dc2, &beta1, nn->b2_desc,
                                          nn->d_g[NN_T_CONV2_B]));
  DNN_CHECK(cudnnConvolutionBackwardFilter(
      nn->dnn, &alpha, nn->x2_desc, nn->d_act_c1, nn->y2_desc, nn->d_dc2,
      nn->conv2_desc, nn->algo_bf2, nn->d_ws_conv, nn->ws_conv_bytes, &beta1,
      nn->f2_desc, nn->d_g[NN_T_CONV2_W]));
  DNN_CHECK(cudnnConvolutionBackwardData(
      nn->dnn, &alpha, nn->f2_desc, nn->d_t[NN_T_CONV2_W], nn->y2_desc,
      nn->d_dc2, nn->conv2_desc, nn->algo_bd2, nn->d_ws_conv, nn->ws_conv_bytes,
      &beta0, nn->x2_desc, nn->d_dc1));

  {
    const int work = n * NN_C_OUT1 * NN_H1 * NN_W1;
    k_relu_bwd<<<grid_for(work), thr>>>(nn->d_dc1, nn->d_pre_c1, work);
  }

  /* conv1 bias / filter (no data grad needed) */
  DNN_CHECK(cudnnConvolutionBackwardBias(nn->dnn, &alpha, nn->y1_desc,
                                          nn->d_dc1, &beta1, nn->b1_desc,
                                          nn->d_g[NN_T_CONV1_B]));
  DNN_CHECK(cudnnConvolutionBackwardFilter(
      nn->dnn, &alpha, nn->x1_desc, nn->d_obs, nn->y1_desc, nn->d_dc1,
      nn->conv1_desc, nn->algo_bf1, nn->d_ws_conv, nn->ws_conv_bytes, &beta1,
      nn->f1_desc, nn->d_g[NN_T_CONV1_W]));

  CU_CHECK(cudaGetLastError());
  return 0;
}

static int clip_and_adam(NnCuda *nn, float *grad_norm_out) {
  const int thr = 256;
  const size_t n = nn->n_params;
  const int blocks = nn->reduce_blocks;
  k_sumsq_partial<<<blocks, thr, thr * sizeof(float)>>>(nn->d_grads, n,
                                                        nn->d_reduce);
  k_sum_partials<<<1, thr>>>(nn->d_reduce, blocks, nn->d_norm);
  float sumsq = 0.f;
  CU_CHECK(cudaMemcpy(&sumsq, nn->d_norm, sizeof(float), cudaMemcpyDeviceToHost));
  const float norm = std::sqrt(sumsq);
  if (grad_norm_out)
    *grad_norm_out = norm;
  if (norm > nn->cfg.grad_limit && norm > 0.f) {
    const float scale = nn->cfg.grad_limit / (norm + 1e-6f);
    k_scale<<<grid_for_sz(n), thr>>>(nn->d_grads, n, scale);
  }

  nn->adam_t += 1;
  const float t = (float)nn->adam_t;
  const float bc1 = 1.f - std::pow(kAdamBeta1, t);
  const float bc2 = 1.f - std::pow(kAdamBeta2, t);
  k_adam<<<grid_for_sz(n), thr>>>(nn->d_params, nn->d_adam_m, nn->d_adam_v,
                                  nn->d_grads, n, nn->cfg.lr, bc1, bc2);
  CU_CHECK(cudaGetLastError());
  return 0;
}

static void free_ptr(void *p) {
  if (p)
    cudaFree(p);
}

static void free_host(void *p) {
  if (p)
    free(p);
}

static void free_nn(NnCuda *nn) {
  if (!nn)
    return;
  if (nn->device >= 0)
    cudaSetDevice(nn->device);

  free_host(nn->h_params);
  free_host(nn->h_planes);
  free_host(nn->h_scalars);
  free_host(nn->h_logits);
  free_host(nn->h_values);
  free_host(nn->h_acts);
  free_host(nn->h_logp);
  free_host(nn->h_entropy);
  free_host(nn->h_old_logp);
  free_host(nn->h_adv);
  free_host(nn->h_ret);
  free_host(nn->h_scratch);

  free_ptr(nn->d_params);
  free_ptr(nn->d_grads);
  free_ptr(nn->d_adam_m);
  free_ptr(nn->d_adam_v);
  free_ptr(nn->d_planes);
  free_ptr(nn->d_scalars);
  free_ptr(nn->d_obs);
  free_ptr(nn->d_pre_c1);
  free_ptr(nn->d_act_c1);
  free_ptr(nn->d_pre_c2);
  free_ptr(nn->d_act_c2);
  free_ptr(nn->d_fc_in);
  free_ptr(nn->d_pre_h);
  free_ptr(nn->d_act_h);
  free_ptr(nn->d_logits);
  free_ptr(nn->d_value);
  free_ptr(nn->d_acts);
  free_ptr(nn->d_logp);
  free_ptr(nn->d_entropy);
  free_ptr(nn->d_old_logp);
  free_ptr(nn->d_adv);
  free_ptr(nn->d_ret);
  free_ptr(nn->d_dlogits);
  free_ptr(nn->d_dvalue);
  free_ptr(nn->d_dhidden);
  free_ptr(nn->d_dfc_in);
  free_ptr(nn->d_dc2);
  free_ptr(nn->d_dc1);
  free_ptr(nn->d_stats);
  free_ptr(nn->d_reduce);
  free_ptr(nn->d_norm);
  free_ptr(nn->d_ws_conv);

  auto dt = [](cudnnTensorDescriptor_t &d) {
    if (d) {
      cudnnDestroyTensorDescriptor(d);
      d = nullptr;
    }
  };
  auto df = [](cudnnFilterDescriptor_t &d) {
    if (d) {
      cudnnDestroyFilterDescriptor(d);
      d = nullptr;
    }
  };
  auto dc = [](cudnnConvolutionDescriptor_t &d) {
    if (d) {
      cudnnDestroyConvolutionDescriptor(d);
      d = nullptr;
    }
  };
  dt(nn->x1_desc);
  dt(nn->y1_desc);
  dt(nn->b1_desc);
  dt(nn->x2_desc);
  dt(nn->y2_desc);
  dt(nn->b2_desc);
  df(nn->f1_desc);
  df(nn->f2_desc);
  dc(nn->conv1_desc);
  dc(nn->conv2_desc);
  if (nn->dnn) {
    cudnnDestroy(nn->dnn);
    nn->dnn = nullptr;
  }
  if (nn->blas) {
    cublasDestroy(nn->blas);
    nn->blas = nullptr;
  }
  free(nn);
}

static int setup_cudnn(NnCuda *nn) {
  DNN_CHECK(cudnnCreate(&nn->dnn));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->x1_desc));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->y1_desc));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->b1_desc));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->x2_desc));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->y2_desc));
  DNN_CHECK(cudnnCreateTensorDescriptor(&nn->b2_desc));
  DNN_CHECK(cudnnCreateFilterDescriptor(&nn->f1_desc));
  DNN_CHECK(cudnnCreateFilterDescriptor(&nn->f2_desc));
  DNN_CHECK(cudnnCreateConvolutionDescriptor(&nn->conv1_desc));
  DNN_CHECK(cudnnCreateConvolutionDescriptor(&nn->conv2_desc));

  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->b1_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, 1, NN_C_OUT1, 1, 1));
  DNN_CHECK(cudnnSetTensor4dDescriptor(nn->b2_desc, CUDNN_TENSOR_NCHW,
                                       CUDNN_DATA_FLOAT, 1, NN_C_OUT2, 1, 1));
  DNN_CHECK(cudnnSetFilter4dDescriptor(nn->f1_desc, CUDNN_DATA_FLOAT,
                                       CUDNN_TENSOR_NCHW, NN_C_OUT1, NN_N_CH,
                                       NN_K1, NN_K1));
  DNN_CHECK(cudnnSetFilter4dDescriptor(nn->f2_desc, CUDNN_DATA_FLOAT,
                                       CUDNN_TENSOR_NCHW, NN_C_OUT2, NN_C_OUT1,
                                       NN_K2, NN_K2));
  DNN_CHECK(cudnnSetConvolution2dDescriptor(
      nn->conv1_desc, 0, 0, NN_S1, NN_S1, 1, 1, CUDNN_CROSS_CORRELATION,
      CUDNN_DATA_FLOAT));
  DNN_CHECK(cudnnSetConvolution2dDescriptor(
      nn->conv2_desc, 0, 0, NN_S2, NN_S2, 1, 1, CUDNN_CROSS_CORRELATION,
      CUDNN_DATA_FLOAT));
  DNN_CHECK(cudnnSetConvolutionMathType(nn->conv1_desc, CUDNN_DEFAULT_MATH));
  DNN_CHECK(cudnnSetConvolutionMathType(nn->conv2_desc, CUDNN_DEFAULT_MATH));

  if (set_batch_descs(nn, nn->max_n) != 0)
    return -1;
  if (pick_batch_algos(nn, true) != 0)
    return -1;
  CU_CHECK(cudaMalloc(&nn->d_ws_conv, nn->ws_conv_bytes));
  return 0;
}

static int cuda_malloc_f(float **p, size_t n) {
  *p = nullptr;
  size_t b = n * sizeof(float);
  if (b == 0)
    b = 1;
  return cudaMalloc(p, b) == cudaSuccess ? 0 : -1;
}

NnCuda *nn_cuda_create(int max_n, int device, const NnConfig *cfg) {
  g_err[0] = 0;
  if (max_n <= 0) {
    set_err("max_n must be > 0");
    return nullptr;
  }
  int ndev = 0;
  if (cudaGetDeviceCount(&ndev) != cudaSuccess || device < 0 ||
      device >= ndev) {
    set_err("invalid CUDA device");
    return nullptr;
  }
  NnConfig resolved = cfg ? *cfg : nn_config_default();
  if (validate_config(&resolved) != 0)
    return nullptr;

  if (cudaSetDevice(device) != cudaSuccess) {
    set_err("cudaSetDevice failed");
    return nullptr;
  }

  NnCuda *nn = (NnCuda *)calloc(1, sizeof(NnCuda));
  if (!nn) {
    set_err("oom handle");
    return nullptr;
  }
  nn->max_n = max_n;
  nn->device = device;
  nn->cfg = resolved;
  nn->sample_step = 0;
  nn->adam_t = 0;
  nn->n_params = nn_model_param_floats();
  nn->desc_n = 0;
  nn->blas = nullptr;
  nn->dnn = nullptr;

  if (cublasCreate(&nn->blas) != CUBLAS_STATUS_SUCCESS) {
    set_err("cublasCreate failed");
    free_nn(nn);
    return nullptr;
  }
  cublasSetMathMode(nn->blas, CUBLAS_DEFAULT_MATH);

  /* host params + init */
  nn->h_params = (float *)calloc(nn->n_params, sizeof(float));
  if (!nn->h_params) {
    set_err("oom host params");
    free_nn(nn);
    return nullptr;
  }
  for (int t = 0; t < NN_T_COUNT; ++t)
    nn->h_t[t] = nn->h_params + tensor_offset(t);
  nn_fixture_init_weights(nn->h_t, 0xC0FFEEu);

  const size_t N = (size_t)max_n;
  const size_t plane_bytes = N * NN_N_CH * NN_CAM_H * NN_CAM_W;

  nn->h_planes = (uint8_t *)malloc(plane_bytes);
  nn->h_scalars = (float *)malloc(N * NN_N_SCAL * sizeof(float));
  nn->h_logits = (float *)malloc(N * NN_N_LOGITS * sizeof(float));
  nn->h_values = (float *)malloc(N * sizeof(float));
  nn->h_acts = (int32_t *)malloc(N * NN_N_HEAD * sizeof(int32_t));
  nn->h_logp = (float *)malloc(N * sizeof(float));
  nn->h_entropy = (float *)malloc(N * sizeof(float));
  nn->h_old_logp = (float *)malloc(N * sizeof(float));
  nn->h_adv = (float *)malloc(N * sizeof(float));
  nn->h_ret = (float *)malloc(N * sizeof(float));
  nn->h_scratch = (float *)malloc(16 * sizeof(float));
  if (!nn->h_planes || !nn->h_scalars || !nn->h_logits || !nn->h_values ||
      !nn->h_acts || !nn->h_logp || !nn->h_entropy || !nn->h_old_logp ||
      !nn->h_adv || !nn->h_ret || !nn->h_scratch) {
    set_err("oom host staging");
    free_nn(nn);
    return nullptr;
  }

  if (cuda_malloc_f(&nn->d_params, nn->n_params) ||
      cuda_malloc_f(&nn->d_grads, nn->n_params) ||
      cuda_malloc_f(&nn->d_adam_m, nn->n_params) ||
      cuda_malloc_f(&nn->d_adam_v, nn->n_params) ||
      cudaMalloc(&nn->d_planes, plane_bytes) != cudaSuccess ||
      cuda_malloc_f(&nn->d_scalars, N * NN_N_SCAL) ||
      cuda_malloc_f(&nn->d_obs, N * NN_N_CH * NN_CAM_H * NN_CAM_W) ||
      cuda_malloc_f(&nn->d_pre_c1, N * NN_C_OUT1 * NN_H1 * NN_W1) ||
      cuda_malloc_f(&nn->d_act_c1, N * NN_C_OUT1 * NN_H1 * NN_W1) ||
      cuda_malloc_f(&nn->d_pre_c2, N * NN_C_OUT2 * NN_H2 * NN_W2) ||
      cuda_malloc_f(&nn->d_act_c2, N * NN_C_OUT2 * NN_H2 * NN_W2) ||
      cuda_malloc_f(&nn->d_fc_in, N * NN_FC_IN) ||
      cuda_malloc_f(&nn->d_pre_h, N * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_act_h, N * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_logits, N * NN_N_LOGITS) ||
      cuda_malloc_f(&nn->d_value, N) ||
      cudaMalloc(&nn->d_acts, N * NN_N_HEAD * sizeof(int32_t)) != cudaSuccess ||
      cuda_malloc_f(&nn->d_logp, N) || cuda_malloc_f(&nn->d_entropy, N) ||
      cuda_malloc_f(&nn->d_old_logp, N) || cuda_malloc_f(&nn->d_adv, N) ||
      cuda_malloc_f(&nn->d_ret, N) ||
      cuda_malloc_f(&nn->d_dlogits, N * NN_N_LOGITS) ||
      cuda_malloc_f(&nn->d_dvalue, N) ||
      cuda_malloc_f(&nn->d_dhidden, N * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_dfc_in, N * NN_FC_IN) ||
      cuda_malloc_f(&nn->d_dc2, N * NN_C_OUT2 * NN_H2 * NN_W2) ||
      cuda_malloc_f(&nn->d_dc1, N * NN_C_OUT1 * NN_H1 * NN_W1) ||
      cuda_malloc_f(&nn->d_stats, 8) || cuda_malloc_f(&nn->d_norm, 1)) {
    set_err("cudaMalloc arenas failed");
    free_nn(nn);
    return nullptr;
  }

  nn->reduce_blocks = 256;
  if (cuda_malloc_f(&nn->d_reduce, (size_t)nn->reduce_blocks) != 0) {
    set_err("cudaMalloc reduce failed");
    free_nn(nn);
    return nullptr;
  }

  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->d_t[t] = nn->d_params + tensor_offset(t);
    nn->d_g[t] = nn->d_grads + tensor_offset(t);
  }

  if (cudaMemcpy(nn->d_params, nn->h_params, nn->n_params * sizeof(float),
                 cudaMemcpyHostToDevice) != cudaSuccess) {
    set_err("weight H2D failed");
    free_nn(nn);
    return nullptr;
  }
  cudaMemset(nn->d_grads, 0, nn->n_params * sizeof(float));
  cudaMemset(nn->d_adam_m, 0, nn->n_params * sizeof(float));
  cudaMemset(nn->d_adam_v, 0, nn->n_params * sizeof(float));

  if (setup_cudnn(nn) != 0) {
    free_nn(nn);
    return nullptr;
  }

  /* Upload head width tables to constant memory */
  int hw[NN_N_HEAD], ho[NN_N_HEAD];
  for (int i = 0; i < NN_N_HEAD; ++i) {
    hw[i] = NN_HEAD_WIDTHS[i];
    ho[i] = NN_HEAD_OFF[i];
  }
  if (cudaMemcpyToSymbol(kHeadWDev, hw, sizeof(hw)) != cudaSuccess ||
      cudaMemcpyToSymbol(kHeadOffDev, ho, sizeof(ho)) != cudaSuccess) {
    set_err("constant memory upload failed");
    free_nn(nn);
    return nullptr;
  }

  if (cudaDeviceSynchronize() != cudaSuccess) {
    set_err("create sync failed");
    free_nn(nn);
    return nullptr;
  }
  return nn;
}

void nn_cuda_destroy(NnCuda *nn) { free_nn(nn); }

int nn_cuda_set_config(NnCuda *nn, const NnConfig *cfg) {
  if (!nn || !cfg) {
    set_err("null");
    return -1;
  }
  if (validate_config(cfg) != 0)
    return -1;
  if (cfg->rng_seed != nn->cfg.rng_seed)
    nn->sample_step = 0;
  nn->cfg = *cfg;
  return 0;
}

int nn_cuda_forward(NnCuda *nn, const uint8_t *planes, const float *scalars,
                    int n, float *logits, float *values) {
  if (!nn || !planes || !scalars || !logits || !values) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;

  const size_t plane_n = (size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W;
  CU_CHECK(cudaMemcpy(nn->d_planes, planes, plane_n, cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_scalars, scalars,
                      (size_t)n * NN_N_SCAL * sizeof(float),
                      cudaMemcpyHostToDevice));

  if (forward_device(nn, n) != 0)
    return -1;

  CU_CHECK(cudaMemcpy(logits, nn->d_logits,
                      (size_t)n * NN_N_LOGITS * sizeof(float),
                      cudaMemcpyDeviceToHost));
  CU_CHECK(cudaMemcpy(values, nn->d_value, (size_t)n * sizeof(float),
                      cudaMemcpyDeviceToHost));
  CU_CHECK(cudaDeviceSynchronize());
  return 0;
}

int nn_cuda_sample(NnCuda *nn, const float *logits, int n, int mode,
                   int32_t *acts, float *logp, float *entropy) {
  if (!nn || !logits || !acts || !logp) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  if (mode != NN_SAMPLE_GUMBEL && mode != NN_SAMPLE_GREEDY) {
    set_err("bad sample mode");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;

  CU_CHECK(cudaMemcpy(nn->d_logits, logits,
                      (size_t)n * NN_N_LOGITS * sizeof(float),
                      cudaMemcpyHostToDevice));

  const int thr = 256;
  float *ent_ptr = entropy ? nn->d_entropy : nullptr;
  /* Mix handle sample_step into rng_seed. d_hash_u01 stays 4-arg. */
  const uint64_t seed =
      nn->cfg.rng_seed ^ (nn->sample_step * 0xD1B54A32D192ED03ULL);
  k_sample<<<grid_for(n), thr>>>(nn->d_logits, n, mode, seed, nn->d_acts,
                                 nn->d_logp, ent_ptr);
  CU_CHECK(cudaGetLastError());
  CU_CHECK(cudaMemcpy(acts, nn->d_acts,
                      (size_t)n * NN_N_HEAD * sizeof(int32_t),
                      cudaMemcpyDeviceToHost));
  CU_CHECK(cudaMemcpy(logp, nn->d_logp, (size_t)n * sizeof(float),
                      cudaMemcpyDeviceToHost));
  if (entropy)
    CU_CHECK(cudaMemcpy(entropy, nn->d_entropy, (size_t)n * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CU_CHECK(cudaDeviceSynchronize());
  nn->sample_step++;
  return 0;
}

int nn_cuda_update(NnCuda *nn, const uint8_t *planes, const float *scalars,
                   const int32_t *acts, const float *old_logp,
                   const float *advantages, const float *returns, int n,
                   NnUpdateStats *stats) {
  if (!nn || !planes || !scalars || !acts || !old_logp || !advantages ||
      !returns) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  if (validate_actions(acts, n) != 0)
    return -1;
  if (bind_device(nn) != 0)
    return -1;

  const size_t plane_n = (size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W;
  CU_CHECK(cudaMemcpy(nn->d_planes, planes, plane_n, cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_scalars, scalars,
                      (size_t)n * NN_N_SCAL * sizeof(float),
                      cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_acts, acts, (size_t)n * NN_N_HEAD * sizeof(int32_t),
                      cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_old_logp, old_logp, (size_t)n * sizeof(float),
                      cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_adv, advantages, (size_t)n * sizeof(float),
                      cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemcpy(nn->d_ret, returns, (size_t)n * sizeof(float),
                      cudaMemcpyHostToDevice));

  {
    const int thr = 256;
    k_zero<<<grid_for_sz(nn->n_params), thr>>>(nn->d_grads, nn->n_params);
    k_zero<<<1, 32>>>(nn->d_stats, 8);
  }

  if (forward_device(nn, n) != 0)
    return -1;

  {
    const int thr = 256;
    k_ppo_dlogits<<<grid_for(n), thr>>>(
        nn->d_logits, nn->d_acts, nn->d_old_logp, nn->d_adv, nn->d_ret,
        nn->d_value, n, nn->cfg.ppo_clip, nn->cfg.value_coef,
        nn->cfg.entropy_coef, nn->d_dlogits, nn->d_dvalue, nn->d_stats);
  }

  if (backward_device(nn, n) != 0)
    return -1;

  float grad_norm = 0.f;
  if (clip_and_adam(nn, &grad_norm) != 0)
    return -1;

  if (stats) {
    float s[5] = {0, 0, 0, 0, 0};
    CU_CHECK(cudaMemcpy(s, nn->d_stats, 5 * sizeof(float),
                        cudaMemcpyDeviceToHost));
    const float inv_n = 1.f / (float)n;
    stats->policy_loss = s[0] * inv_n;
    stats->value_loss = nn->cfg.value_coef * s[1] * inv_n;
    stats->entropy_mean = s[2] * inv_n;
    stats->total_loss = stats->policy_loss + stats->value_loss -
                        nn->cfg.entropy_coef * stats->entropy_mean;
    stats->grad_norm = grad_norm;
    stats->approx_kl = s[3] * inv_n;
    stats->clipfrac = s[4] * inv_n;
  }

  CU_CHECK(cudaDeviceSynchronize());
  return 0;
}

int nn_cuda_save(const NnCuda *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;
  /* Staging buffer is handle-owned scratch; const only guards public state. */
  NnCuda *mut = const_cast<NnCuda *>(nn);
  if (cudaMemcpy(mut->h_params, mut->d_params, mut->n_params * sizeof(float),
                 cudaMemcpyDeviceToHost) != cudaSuccess) {
    set_err("D2H params failed");
    return -1;
  }
  const float *tensors[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    tensors[t] = mut->h_t[t];
  if (nn_fixture_save(path, tensors) != 0) {
    set_err("save failed");
    return -1;
  }
  return 0;
}

int nn_cuda_load(NnCuda *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;
  float *tensors[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    tensors[t] = nn->h_t[t];
  if (nn_fixture_load(path, tensors) != 0) {
    set_err("load failed");
    return -1;
  }
  CU_CHECK(cudaMemcpy(nn->d_params, nn->h_params, nn->n_params * sizeof(float),
                      cudaMemcpyHostToDevice));
  CU_CHECK(cudaMemset(nn->d_adam_m, 0, nn->n_params * sizeof(float)));
  CU_CHECK(cudaMemset(nn->d_adam_v, 0, nn->n_params * sizeof(float)));
  nn->adam_t = 0;
  CU_CHECK(cudaDeviceSynchronize());
  return 0;
}
