/* CUDA Blaze policy. Fable: NHWC fp16 acts, cublasLt dense, y>0 ReLU bwd.
 * Host ABI and checkpoints stay NCHW / KCRS fp32. See cuda_fable_contract.h. */
#include "cuda.h"
#include "cuda_conv_graph.h"
#include "cuda_fable_contract.h"
#include "cuda_layout.h"
#include "cuda_lt_gemm.h"
#include "cuda_ws.h"
#include "fixture.h"
#include "model.h"

#include <cublasLt.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <cudnn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nvtx3/nvToolsExt.h>

namespace {
struct NvtxSpan {
  bool on;
  explicit NvtxSpan(const char *name, bool enabled) : on(enabled) {
    if (on)
      nvtxRangePushA(name);
  }
  ~NvtxSpan() {
    if (on)
      nvtxRangePop();
  }
};
} // namespace

static const float kAdamBeta1 = 0.9f;
static const float kAdamBeta2 = 0.999f;
static const float kAdamEps = 1e-8f;
static const int kHvOut = NN_N_LOGITS + 1;

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

static int bucket_n(int n, int max_n) {
  if (n < 1)
    n = 1;
  if (n > max_n)
    n = max_n;
  if (n * 10 >= max_n * 9)
    return max_n;
  const int m = (max_n < 256) ? 32 : 256;
  int b = ((n + m - 1) / m) * m;
  if (b > max_n)
    b = max_n;
  if (b < 1)
    b = 1;
  return b;
}

static size_t aux_bytes(int out, int n) {
  const int64_t ld = ((int64_t)out + 127) & ~127LL;
  return (size_t)(ld / 8) * (size_t)n;
}

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

    atomicAdd(&stats_buf[3], ratio - 1.f - logf(ratio));
    if (fabsf(ratio - 1.f) > clip)
      atomicAdd(&stats_buf[4], 1.f);
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

/* hv is [35, n] col-major. logits [n,34], value [n]. */
__global__ void k_split_hv(const float *__restrict__ hv, float *__restrict__ logits,
                           float *__restrict__ value, int n) {
  for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < n * kHvOut;
       i += blockDim.x * gridDim.x) {
    const int ni = i / kHvOut;
    const int f = i % kHvOut;
    if (f < NN_N_LOGITS)
      logits[(size_t)ni * NN_N_LOGITS + f] = hv[i];
    else
      value[ni] = hv[i];
  }
}

__global__ void k_merge_dhv(const float *__restrict__ dlogits,
                            const float *__restrict__ dvalue,
                            float *__restrict__ dhv, int n) {
  for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < n * kHvOut;
       i += blockDim.x * gridDim.x) {
    const int ni = i / kHvOut;
    const int f = i % kHvOut;
    if (f < NN_N_LOGITS)
      dhv[i] = dlogits[(size_t)ni * NN_N_LOGITS + f];
    else
      dhv[i] = dvalue[ni];
  }
}

struct NnCuda {
  int max_n;
  int device;
  int sealed; /* 1 = prepared bucket set is frozen; a miss is an error */
  NnConfig cfg;
  uint64_t sample_step;
  int64_t adam_t;
  size_t n_params;
  int c0_pad;

  cublasLtHandle_t lt;
  cudnnHandle_t dnn;
  NnWsArena ws;
  NnConvNet *conv;
  NnLtGemm *ltg;

  float *h_params;
  float *h_t[NN_T_COUNT];

  float *d_params;
  float *d_grads;
  float *d_adam_m;
  float *d_adam_v;
  float *d_t[NN_T_COUNT];
  float *d_g[NN_T_COUNT];

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
  float *h_scratch;

  uint8_t *d_planes;
  float *d_scalars;
  __half *d_obs;
  __half *d_act_c1;
  __half *d_act_c2;
  __half *d_dpre_c1;
  __half *d_dpre_c2;
  __half *d_dc1;
  float *d_c2_f;
  __half *d_conv1_b;
  __half *d_conv2_b;
  float *d_act_h;
  float *d_dpre_h;
  float *d_dhidden;
  void *d_aux_h;
  float *d_hv;
  float *d_dhv;
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
  float *d_stats;
  float *d_reduce;
  float *d_norm;
  int reduce_blocks;

  __half *d_conv1_w;
  __half *d_conv2_w;
  float *d_fc_w;
  float *d_hv_w;
  float *d_hv_b;
  __half *d_conv1_gw;
  __half *d_conv2_gw;
  float *d_fc_gw;
  float *d_hv_gw;
  float *d_hv_gb;
};

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

static int sync_work_from_params(NnCuda *nn) {
  if (nn_layout_kcrs_to_krsc(nn->d_t[NN_T_CONV1_W], nn->d_conv1_w, NN_C_OUT1,
                             NN_N_CH, NN_K1, NN_K1, nn->c0_pad) != 0) {
    set_err("conv1 kcrs->krsc failed");
    return -1;
  }
  if (nn_layout_kcrs_to_krsc(nn->d_t[NN_T_CONV2_W], nn->d_conv2_w, NN_C_OUT2,
                             NN_C_OUT1, NN_K2, NN_K2, NN_C_OUT1) != 0) {
    set_err("conv2 kcrs->krsc failed");
    return -1;
  }
  if (nn_layout_f32_to_f16(nn->d_t[NN_T_CONV1_B], nn->d_conv1_b, NN_C_OUT1) !=
      0) {
    set_err("conv1 bias f16 failed");
    return -1;
  }
  if (nn_layout_f32_to_f16(nn->d_t[NN_T_CONV2_B], nn->d_conv2_b, NN_C_OUT2) !=
      0) {
    set_err("conv2 bias f16 failed");
    return -1;
  }
  if (nn_layout_fc_chw_to_hwc(nn->d_t[NN_T_FC_W], nn->d_fc_w, NN_FC_OUT,
                              NN_FC_IN, NN_C_OUT2, NN_H2, NN_W2) != 0) {
    set_err("fc chw->hwc failed");
    return -1;
  }
  CU_CHECK(cudaMemcpy(nn->d_hv_w, nn->d_t[NN_T_HEADS_W],
                      (size_t)NN_N_LOGITS * NN_FC_OUT * sizeof(float),
                      cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_hv_w + (size_t)NN_N_LOGITS * NN_FC_OUT,
                      nn->d_t[NN_T_VALUE_W], NN_FC_OUT * sizeof(float),
                      cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_hv_b, nn->d_t[NN_T_HEADS_B],
                      (size_t)NN_N_LOGITS * sizeof(float),
                      cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_hv_b + NN_N_LOGITS, nn->d_t[NN_T_VALUE_B],
                      sizeof(float), cudaMemcpyDeviceToDevice));
  return 0;
}

static int sync_grads_to_params(NnCuda *nn) {
  if (nn_layout_krsc_to_kcrs(nn->d_conv1_gw, nn->d_g[NN_T_CONV1_W], NN_C_OUT1,
                             NN_N_CH, NN_K1, NN_K1, nn->c0_pad) != 0) {
    set_err("conv1 krsc->kcrs grad failed");
    return -1;
  }
  if (nn_layout_krsc_to_kcrs(nn->d_conv2_gw, nn->d_g[NN_T_CONV2_W], NN_C_OUT2,
                             NN_C_OUT1, NN_K2, NN_K2, NN_C_OUT1) != 0) {
    set_err("conv2 krsc->kcrs grad failed");
    return -1;
  }
  if (nn_layout_fc_hwc_to_chw(nn->d_fc_gw, nn->d_g[NN_T_FC_W], NN_FC_OUT,
                              NN_FC_IN, NN_C_OUT2, NN_H2, NN_W2) != 0) {
    set_err("fc hwc->chw grad failed");
    return -1;
  }
  CU_CHECK(cudaMemcpy(nn->d_g[NN_T_HEADS_W], nn->d_hv_gw,
                      (size_t)NN_N_LOGITS * NN_FC_OUT * sizeof(float),
                      cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_g[NN_T_VALUE_W],
                      nn->d_hv_gw + (size_t)NN_N_LOGITS * NN_FC_OUT,
                      NN_FC_OUT * sizeof(float), cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_g[NN_T_HEADS_B], nn->d_hv_gb,
                      (size_t)NN_N_LOGITS * sizeof(float),
                      cudaMemcpyDeviceToDevice));
  CU_CHECK(cudaMemcpy(nn->d_g[NN_T_VALUE_B], nn->d_hv_gb + NN_N_LOGITS,
                      sizeof(float), cudaMemcpyDeviceToDevice));
  return 0;
}

static int forward_device(NnCuda *nn, int n) {
  const int thr = 256;
  const int bn = bucket_n(n, nn->max_n);
  const size_t n_c2 = (size_t)n * NN_C_OUT2 * NN_H2 * NN_W2;
  /* Sealed: every bucket was prepared before the run. A miss here would race
   * cuDNN engines and grow the workspace arena mid-step. Fail instead.
   * conv plans are bucketed, lt plans are keyed by exact n. Ask both: an n
   * that shares a conv bucket with a prepared n still has no lt plan, and
   * would otherwise fail deep inside a gemm. */
  if (nn->sealed) {
    const int has_conv = nn_conv_net_has_bucket(nn->conv, n);
    const int has_lt = nn_lt_has_n(nn->ltg, n);
    if (!has_conv || !has_lt) {
      char msg[160];
      std::snprintf(msg, sizeof(msg),
                    "nn: unprepared bucket n=%d bucket=%d max_n=%d conv=%d "
                    "lt=%d",
                    n, bn, nn->max_n, has_conv, has_lt);
      set_err(msg);
      return -1;
    }
  }
  /* lt picks its plans lazily during execute, so publish what it holds now.
   * conv prepare shrinks the shared arena and must not undercut them. */
  nn_conv_net_set_ws_floor(nn->conv, nn_lt_max_ws(nn->ltg));
  if (nn_conv_net_prepare(nn->conv, n) != 0) {
    set_err("conv prepare failed");
    return -1;
  }
  if (nn_lt_prepare(nn->ltg, n) != 0) {
    set_err("lt prepare failed");
    return -1;
  }
  if (bn > n) {
    const size_t elt = (size_t)NN_CAM_H * NN_CAM_W * (size_t)nn->c0_pad;
    CU_CHECK(cudaMemset(nn->d_obs + (size_t)n * elt, 0,
                        (size_t)(bn - n) * elt * sizeof(__half)));
  }
  if (nn_layout_obs_to_nhwc(nn->d_planes, nn->d_obs, n, NN_N_CH, NN_CAM_H,
                            NN_CAM_W, nn->c0_pad, NN_DEPTH_CH0,
                            NN_DEPTH_CH1) != 0) {
    set_err("obs to nhwc failed");
    return -1;
  }
  if (nn_conv_net_fwd(nn->conv, 0, nn->d_obs, nn->d_conv1_w, nn->d_conv1_b,
                      nn->d_act_c1) != 0) {
    set_err("conv0 fwd failed");
    return -1;
  }
  if (nn_conv_net_fwd(nn->conv, 1, nn->d_act_c1, nn->d_conv2_w, nn->d_conv2_b,
                      nn->d_act_c2) != 0) {
    set_err("conv1 fwd failed");
    return -1;
  }
  if (nn_layout_f16_to_f32(nn->d_act_c2, nn->d_c2_f, n_c2) != 0) {
    set_err("conv2 y upcast failed");
    return -1;
  }
  if (nn_lt_gemm(nn->ltg, n, NN_FC_OUT, NN_FLAT, NN_FC_IN, nn->d_fc_w,
                 nn->d_c2_f, nn->d_act_h, 0.f) != 0) {
    set_err("fc cam gemm failed");
    return -1;
  }
  if (nn_lt_fwd_relu_bias_ex(nn->ltg, n, NN_FC_OUT, NN_N_SCAL, NN_FC_IN,
                             nn->d_fc_w + NN_FLAT, nn->d_scalars,
                             nn->d_t[NN_T_FC_B], nn->d_act_h, nn->d_aux_h,
                             1.f) != 0) {
    set_err("fc scal relu-bias failed");
    return -1;
  }
  if (nn_lt_fwd_bias(nn->ltg, n, kHvOut, NN_FC_OUT, nn->d_hv_w, nn->d_act_h,
                     nn->d_hv_b, nn->d_hv) != 0) {
    set_err("heads+value gemm failed");
    return -1;
  }
  k_split_hv<<<grid_for(n * kHvOut), thr>>>(nn->d_hv, nn->d_logits, nn->d_value,
                                            n);
  CU_CHECK(cudaGetLastError());
  return 0;
}

static int backward_device(NnCuda *nn, int n) {
  const int thr = 256;
  const size_t n_c1 = (size_t)n * NN_C_OUT1 * NN_H1 * NN_W1;
  const size_t n_c2 = (size_t)n * NN_C_OUT2 * NN_H2 * NN_W2;
  const size_t n_c1b =
      (size_t)bucket_n(n, nn->max_n) * NN_C_OUT1 * NN_H1 * NN_W1;
  const size_t n_c2b =
      (size_t)bucket_n(n, nn->max_n) * NN_C_OUT2 * NN_H2 * NN_W2;

  k_merge_dhv<<<grid_for(n * kHvOut), thr>>>(nn->d_dlogits, nn->d_dvalue,
                                             nn->d_dhv, n);
  CU_CHECK(cudaGetLastError());

  CU_CHECK(cudaMemset(nn->d_hv_gw, 0,
                      (size_t)kHvOut * NN_FC_OUT * sizeof(float)));
  CU_CHECK(cudaMemset(nn->d_hv_gb, 0, (size_t)kHvOut * sizeof(float)));
  if (nn_lt_bwd_dw_bgrad(nn->ltg, n, kHvOut, NN_FC_OUT, nn->d_dhv, nn->d_act_h,
                         nn->d_hv_gw, nn->d_hv_gb, 0.f) != 0) {
    set_err("hv dw failed");
    return -1;
  }
  if (nn_lt_bwd_dx(nn->ltg, n, kHvOut, NN_FC_OUT, nn->d_hv_w, nn->d_dhv,
                   nn->d_dhidden, 0.f) != 0) {
    set_err("hv dx failed");
    return -1;
  }

  if (nn_lt_drelu_bgrad(nn->ltg, n, NN_FC_OUT, nn->d_dhidden, nn->d_aux_h,
                        nn->d_dpre_h, nn->d_g[NN_T_FC_B]) != 0) {
    set_err("fc drelu failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_fc_gw, 0,
                      (size_t)NN_FC_OUT * NN_FC_IN * sizeof(float)));
  if (nn_layout_f16_to_f32(nn->d_act_c2, nn->d_c2_f, n_c2) != 0) {
    set_err("conv2 y upcast (dw) failed");
    return -1;
  }
  if (nn_lt_bwd_dw_ex(nn->ltg, n, NN_FC_OUT, NN_FLAT, NN_FC_IN, nn->d_dpre_h,
                      nn->d_c2_f, nn->d_fc_gw, nullptr, 0.f) != 0) {
    set_err("fc dw cam failed");
    return -1;
  }
  if (nn_lt_bwd_dw_ex(nn->ltg, n, NN_FC_OUT, NN_N_SCAL, NN_FC_IN, nn->d_dpre_h,
                      nn->d_scalars, nn->d_fc_gw + NN_FLAT, nullptr, 1.f) != 0) {
    set_err("fc dw scal failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_c2_f, 0, n_c2b * sizeof(float)));
  if (nn_lt_bwd_dx_ex(nn->ltg, n, NN_FC_OUT, NN_FLAT, NN_FC_IN, nn->d_fc_w,
                      nn->d_dpre_h, nn->d_c2_f, 0.f) != 0) {
    set_err("fc dx cam failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_dpre_c2, 0, n_c2b * sizeof(__half)));
  if (nn_layout_f32_to_f16(nn->d_c2_f, nn->d_dpre_c2, n_c2) != 0) {
    set_err("conv2 dy downcast failed");
    return -1;
  }

  CU_CHECK(cudaMemset(nn->d_g[NN_T_CONV2_B], 0, NN_C_OUT2 * sizeof(float)));
  if (nn_layout_relu_bwd_bias(nn->d_dpre_c2, nn->d_act_c2, nn->d_dpre_c2,
                              nn->d_g[NN_T_CONV2_B], n, NN_C_OUT2, NN_H2,
                              NN_W2) != 0) {
    set_err("conv2 relu-bwd-bias failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_conv2_gw, 0,
                      (size_t)NN_C_OUT2 * NN_K2 * NN_K2 * NN_C_OUT1 *
                          sizeof(__half)));
  if (nn_conv_net_wgrad(nn->conv, 1, nn->d_act_c1, nn->d_dpre_c2, nn->d_conv2_gw,
                        0.f) != 0) {
    set_err("conv2 wgrad failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_dc1, 0, n_c1b * sizeof(__half)));
  if (nn_conv_net_dgrad(nn->conv, 1, nn->d_conv2_w, nn->d_dpre_c2, nn->d_dc1) !=
      0) {
    set_err("conv2 dgrad failed");
    return -1;
  }

  CU_CHECK(cudaMemset(nn->d_g[NN_T_CONV1_B], 0, NN_C_OUT1 * sizeof(float)));
  CU_CHECK(cudaMemset(nn->d_dpre_c1, 0, n_c1b * sizeof(__half)));
  if (nn_layout_relu_bwd_bias(nn->d_dc1, nn->d_act_c1, nn->d_dpre_c1,
                              nn->d_g[NN_T_CONV1_B], n, NN_C_OUT1, NN_H1,
                              NN_W1) != 0) {
    set_err("conv1 relu-bwd-bias failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_conv1_gw, 0,
                      (size_t)NN_C_OUT1 * NN_K1 * NN_K1 * (size_t)nn->c0_pad *
                          sizeof(__half)));
  if (nn_conv_net_wgrad(nn->conv, 0, nn->d_obs, nn->d_dpre_c1, nn->d_conv1_gw,
                        0.f) != 0) {
    set_err("conv1 wgrad failed");
    return -1;
  }
  if (nn_conv_net_dgrad(nn->conv, 0, nn->d_conv1_w, nn->d_dpre_c1, nn->d_dc1) !=
      0) {
    set_err("conv1 dgrad skip failed");
    return -1;
  }
  (void)n_c1;
  (void)n_c2;
  CU_CHECK(cudaGetLastError());
  return 0;
}

/* Build every plan for batch size n: the cuDNN conv graphs AND the cuBLASLt
 * algos, which are otherwise picked lazily inside the first update. One dry
 * forward+backward on the device buffers touches exactly the ops the run will
 * touch, so no shape list can drift out of sync. The values are meaningless;
 * only the shapes matter. Grads are zeroed after, and every real update
 * overwrites them anyway. */
static int prepare_bucket(NnCuda *nn, int n) {
  CU_CHECK(cudaMemset(nn->d_planes, 0,
                      (size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W));
  CU_CHECK(cudaMemset(nn->d_scalars, 0,
                      (size_t)n * NN_N_SCAL * sizeof(float)));
  CU_CHECK(cudaMemset(nn->d_dlogits, 0,
                      (size_t)n * NN_N_LOGITS * sizeof(float)));
  CU_CHECK(cudaMemset(nn->d_dvalue, 0, (size_t)n * sizeof(float)));
  if (forward_device(nn, n) != 0)
    return -1;
  if (backward_device(nn, n) != 0)
    return -1;
  /* The lt picks above may have grown the shared arena. Settle it over every
   * conv plan of every prepared bucket plus the new lt floor. */
  nn_conv_net_set_ws_floor(nn->conv, nn_lt_max_ws(nn->ltg));
  if (nn_conv_net_prepare(nn->conv, n) != 0) {
    set_err("conv arena settle failed");
    return -1;
  }
  CU_CHECK(cudaMemset(nn->d_grads, 0, nn->n_params * sizeof(float)));
  if (cudaDeviceSynchronize() != cudaSuccess) {
    set_err("prepare sync failed");
    return -1;
  }
  std::fprintf(stderr, "nn_prepare bucket n=%d done\n", n);
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

  if (nn->conv) {
    nn_conv_net_destroy(nn->conv);
    nn->conv = nullptr;
  }
  if (nn->ltg) {
    nn_lt_destroy(nn->ltg);
    nn->ltg = nullptr;
  }
  nn_ws_free(&nn->ws);

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
  free_ptr(nn->d_act_c1);
  free_ptr(nn->d_act_c2);
  free_ptr(nn->d_dpre_c1);
  free_ptr(nn->d_dpre_c2);
  free_ptr(nn->d_dc1);
  free_ptr(nn->d_c2_f);
  free_ptr(nn->d_conv1_b);
  free_ptr(nn->d_conv2_b);
  free_ptr(nn->d_act_h);
  free_ptr(nn->d_dpre_h);
  free_ptr(nn->d_dhidden);
  free_ptr(nn->d_aux_h);
  free_ptr(nn->d_hv);
  free_ptr(nn->d_dhv);
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
  free_ptr(nn->d_stats);
  free_ptr(nn->d_reduce);
  free_ptr(nn->d_norm);
  free_ptr(nn->d_conv1_w);
  free_ptr(nn->d_conv2_w);
  free_ptr(nn->d_fc_w);
  free_ptr(nn->d_hv_w);
  free_ptr(nn->d_hv_b);
  free_ptr(nn->d_conv1_gw);
  free_ptr(nn->d_conv2_gw);
  free_ptr(nn->d_fc_gw);
  free_ptr(nn->d_hv_gw);
  free_ptr(nn->d_hv_gb);

  if (nn->dnn) {
    cudnnDestroy(nn->dnn);
    nn->dnn = nullptr;
  }
  if (nn->lt) {
    cublasLtDestroy(nn->lt);
    nn->lt = nullptr;
  }
  free(nn);
}

static int cuda_malloc_f(float **p, size_t n) {
  *p = nullptr;
  size_t b = n * sizeof(float);
  if (b == 0)
    b = 1;
  return cudaMalloc(p, b) == cudaSuccess ? 0 : -1;
}

static int cuda_malloc_h(__half **p, size_t n) {
  *p = nullptr;
  size_t b = n * sizeof(__half);
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
  nn->ws.device = device;

  if (cublasLtCreate(&nn->lt) != CUBLAS_STATUS_SUCCESS) {
    set_err("cublasLtCreate failed");
    free_nn(nn);
    return nullptr;
  }
  if (cudnnCreate(&nn->dnn) != CUDNN_STATUS_SUCCESS) {
    set_err("cudnnCreate failed");
    free_nn(nn);
    return nullptr;
  }

  NnConvLayerSpec spec[2] = {
      {NN_N_CH, NN_C_OUT1, NN_K1, NN_S1, 0, NN_CAM_H, NN_CAM_W},
      {NN_C_OUT1, NN_C_OUT2, NN_K2, NN_S2, 0, NN_H1, NN_W1},
  };
  nn->conv = nn_conv_net_create(nn->dnn, device, max_n, spec, 2, &nn->ws);
  if (!nn->conv) {
    set_err("conv net create failed");
    free_nn(nn);
    return nullptr;
  }
  nn->c0_pad = nn_conv_net_c_in_pad(nn->conv, 0);
  if (nn->c0_pad < NN_N_CH) {
    set_err("c0_pad");
    free_nn(nn);
    return nullptr;
  }
  nn->ltg = nn_lt_create(nn->lt, device, max_n, &nn->ws);
  if (!nn->ltg) {
    set_err("lt create failed");
    free_nn(nn);
    return nullptr;
  }

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

  const size_t n_obs = N * NN_CAM_H * NN_CAM_W * (size_t)nn->c0_pad;
  const size_t n_c1 = N * NN_C_OUT1 * NN_H1 * NN_W1;
  const size_t n_c2 = N * NN_C_OUT2 * NN_H2 * NN_W2;
  const size_t n_c1w =
      (size_t)NN_C_OUT1 * NN_K1 * NN_K1 * (size_t)nn->c0_pad;
  const size_t n_c2w = (size_t)NN_C_OUT2 * NN_K2 * NN_K2 * NN_C_OUT1;
  const size_t auxn = aux_bytes(NN_FC_OUT, max_n);

  if (cuda_malloc_f(&nn->d_params, nn->n_params) ||
      cuda_malloc_f(&nn->d_grads, nn->n_params) ||
      cuda_malloc_f(&nn->d_adam_m, nn->n_params) ||
      cuda_malloc_f(&nn->d_adam_v, nn->n_params) ||
      cudaMalloc(&nn->d_planes, plane_bytes) != cudaSuccess ||
      cuda_malloc_f(&nn->d_scalars, N * NN_N_SCAL) ||
      cuda_malloc_h(&nn->d_obs, n_obs) || cuda_malloc_h(&nn->d_act_c1, n_c1) ||
      cuda_malloc_h(&nn->d_act_c2, n_c2) ||
      cuda_malloc_h(&nn->d_dpre_c1, n_c1) ||
      cuda_malloc_h(&nn->d_dpre_c2, n_c2) || cuda_malloc_h(&nn->d_dc1, n_c1) ||
      cuda_malloc_f(&nn->d_c2_f, n_c2) ||
      cuda_malloc_h(&nn->d_conv1_b, NN_C_OUT1) ||
      cuda_malloc_h(&nn->d_conv2_b, NN_C_OUT2) ||
      cuda_malloc_f(&nn->d_act_h, N * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_dpre_h, N * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_dhidden, N * NN_FC_OUT) ||
      cudaMalloc(&nn->d_aux_h, auxn < 16 ? 16 : auxn) != cudaSuccess ||
      cuda_malloc_f(&nn->d_hv, N * kHvOut) ||
      cuda_malloc_f(&nn->d_dhv, N * kHvOut) ||
      cuda_malloc_f(&nn->d_logits, N * NN_N_LOGITS) ||
      cuda_malloc_f(&nn->d_value, N) ||
      cudaMalloc(&nn->d_acts, N * NN_N_HEAD * sizeof(int32_t)) != cudaSuccess ||
      cuda_malloc_f(&nn->d_logp, N) || cuda_malloc_f(&nn->d_entropy, N) ||
      cuda_malloc_f(&nn->d_old_logp, N) || cuda_malloc_f(&nn->d_adv, N) ||
      cuda_malloc_f(&nn->d_ret, N) ||
      cuda_malloc_f(&nn->d_dlogits, N * NN_N_LOGITS) ||
      cuda_malloc_f(&nn->d_dvalue, N) || cuda_malloc_f(&nn->d_stats, 8) ||
      cuda_malloc_f(&nn->d_norm, 1) || cuda_malloc_h(&nn->d_conv1_w, n_c1w) ||
      cuda_malloc_h(&nn->d_conv2_w, n_c2w) ||
      cuda_malloc_f(&nn->d_fc_w, (size_t)NN_FC_OUT * NN_FC_IN) ||
      cuda_malloc_f(&nn->d_hv_w, (size_t)kHvOut * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_hv_b, kHvOut) ||
      cuda_malloc_h(&nn->d_conv1_gw, n_c1w) ||
      cuda_malloc_h(&nn->d_conv2_gw, n_c2w) ||
      cuda_malloc_f(&nn->d_fc_gw, (size_t)NN_FC_OUT * NN_FC_IN) ||
      cuda_malloc_f(&nn->d_hv_gw, (size_t)kHvOut * NN_FC_OUT) ||
      cuda_malloc_f(&nn->d_hv_gb, kHvOut)) {
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
  if (sync_work_from_params(nn) != 0) {
    free_nn(nn);
    return nullptr;
  }

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

  if (prepare_bucket(nn, max_n) != 0) {
    free_nn(nn);
    return nullptr;
  }

  if (cudaDeviceSynchronize() != cudaSuccess) {
    set_err("create sync failed");
    free_nn(nn);
    return nullptr;
  }
  nvtxMarkA("nn_cuda_ready");
  return nn;
}

void nn_cuda_destroy(NnCuda *nn) { free_nn(nn); }

int nn_cuda_prepare_n(NnCuda *nn, int n) {
  if (!nn) {
    set_err("null handle");
    return -1;
  }
  if (n < 1 || n > nn->max_n) {
    set_err("prepare n out of range");
    return -1;
  }
  if (nn->sealed) {
    set_err("prepare after seal");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;
  return prepare_bucket(nn, n);
}

int nn_cuda_seal(NnCuda *nn) {
  if (!nn) {
    set_err("null handle");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;
  if (nn_lt_seal(nn->ltg) != 0) {
    set_err("lt seal failed");
    return -1;
  }
  nn->sealed = 1;
  return 0;
}

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

  NvtxSpan span_upd("ppo_update", true);

  const size_t plane_n = (size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W;
  {
    NvtxSpan span("h2d", true);
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
  }

  {
    NvtxSpan span("zero_grads", true);
    const int thr = 256;
    k_zero<<<grid_for_sz(nn->n_params), thr>>>(nn->d_grads, nn->n_params);
    k_zero<<<1, 32>>>(nn->d_stats, 8);
  }

  {
    NvtxSpan span("forward", true);
    if (forward_device(nn, n) != 0)
      return -1;
  }

  {
    NvtxSpan span("ppo_loss", true);
    const int thr = 256;
    k_ppo_dlogits<<<grid_for(n), thr>>>(
        nn->d_logits, nn->d_acts, nn->d_old_logp, nn->d_adv, nn->d_ret,
        nn->d_value, n, nn->cfg.ppo_clip, nn->cfg.value_coef,
        nn->cfg.entropy_coef, nn->d_dlogits, nn->d_dvalue, nn->d_stats);
  }

  {
    NvtxSpan span("backward", true);
    if (backward_device(nn, n) != 0)
      return -1;
    if (sync_grads_to_params(nn) != 0)
      return -1;
  }

  float grad_norm = 0.f;
  {
    NvtxSpan span("clip_adam", true);
    if (clip_and_adam(nn, &grad_norm) != 0)
      return -1;
    if (sync_work_from_params(nn) != 0)
      return -1;
  }

  {
    NvtxSpan span("stats_d2h", true);
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
  }
  return 0;
}

int nn_cuda_save(const NnCuda *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  if (bind_device(nn) != 0)
    return -1;
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
  if (sync_work_from_params(nn) != 0)
    return -1;
  CU_CHECK(cudaDeviceSynchronize());
  return 0;
}
