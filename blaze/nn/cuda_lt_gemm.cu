/* cublasLt dense path. TF32 compute, fp32 store/accumulate.
 *
 * Weight layout: W is row-major [out, k]. That is column-major k x out
 * with lda=k (W^T if W_math is out x k). Activations D, X, dY, dX are
 * features x n, column-major, so bias epilogues broadcast per feature.
 *
 *   fwd:  Y[out,n] = opT(W) @ X[k,n]     transA=T transB=N  lda=ldb=k ldc=out
 *   dX:   dX[k,n]  = W_cm @ dY[out,n]    transA=N transB=N  lda=ldc=k ldb=out
 *   dW:   dW_cm    = X @ dY^T            transA=N transB=T  lda=ldc=k ldb=out
 *
 * See cuda_fable_contract.h. */
#include "cuda_lt_gemm.h"
#include "cuda_fable_contract.h"

#include <cublasLt.h>
#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

char g_err[512] = "";

void set_err(const char *msg) {
  std::snprintf(g_err, sizeof(g_err), "%s", msg);
  std::fprintf(stderr, "cuda_lt_gemm: %s\n", msg);
}

enum {
  kHeurN = 32,
  kTimeN = 8,
  kRepeat = 3,
  kPlanCap = 64,
  kPrepCap = 8,
  kWsMin = 32 << 20
};

enum Kind {
  KIND_FWD_RELU = 0,
  KIND_FWD_BIAS,
  KIND_DRELU,
  KIND_DW,
  KIND_DX,
  KIND_GEMM,
  KIND_N
};

struct GemmSpec {
  cublasOperation_t ta, tb;
  int m, n, k;
  int lda, ldb, ldc;
  cublasLtEpilogue_t epi;
  int has_bias; /* 1 = BIAS_POINTER used (in or out) */
  int has_aux;
  float alpha, beta;
};

struct Plan {
  int kind, n, out, k, lda, ldc, device;
  cublasLtEpilogue_t epi;
  cublasLtMatmulAlgo_t algo;
  size_t ws_need;
  int fallback; /* 1: fused epi unavailable; DEFAULT GEMM + sgemv/kernel */
  int valid;
};

} // namespace

struct NnLtGemm {
  cublasLtHandle_t lt;
  cublasHandle_t blas;
  int device;
  int max_n;
  NnPrec prec;
  int prepared_n;
  NnWsArena *ws;
  float *d_ones;
  int ones_n;
  float *d_I;
  int I_n;
  float *d_pre;
  int pre_out, pre_n;
  float *d_time;
  size_t time_bytes;
  Plan plans[kPlanCap];
  int n_plans;
  int sealed; /* 1 = plan set frozen; a lazy pick is an error */
  int prep_ns[kPrepCap]; /* n values pinned by nn_lt_pin: never evicted */
  int n_prep;
  int evict_cur; /* ring cursor over the unpinned plans */
  long long n_evict;
  cudaEvent_t ev0, ev1;
};

#define LT_OK(call)                                                            \
  do {                                                                         \
    cublasStatus_t _s = (call);                                                \
    if (_s != CUBLAS_STATUS_SUCCESS) {                                         \
      std::fprintf(stderr, "cuda_lt_gemm:%d cublas %d\n", __LINE__, (int)_s);  \
      goto fail;                                                               \
    }                                                                          \
  } while (0)

#define CU_OK(call)                                                            \
  do {                                                                         \
    cudaError_t _e = (call);                                                   \
    if (_e != cudaSuccess) {                                                   \
      std::fprintf(stderr, "cuda_lt_gemm:%d %s\n", __LINE__,                   \
                   cudaGetErrorString(_e));                                    \
      goto fail;                                                               \
    }                                                                          \
  } while (0)

static int64_t aux_ld_bits(int out) {
  return ((int64_t)out + 127) & ~127LL;
}

static int grid_x(int n, int thr) {
  if (n <= 0)
    return 1;
  return (n + thr - 1) / thr;
}

__global__ void k_fill_ones(float *x, int n) {
  int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  if (i < n)
    x[i] = 1.f;
}

__global__ void k_identity(float *I, int n) {
  int r = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int c = (int)blockIdx.y;
  if (r < n && c < n)
    I[(size_t)c * (size_t)n + (size_t)r] = (r == c) ? 1.f : 0.f;
}

/* Fallback dReLU from packed aux bitmask. LSB of each byte is the lower row. */
__global__ void k_apply_relu_mask(const float *__restrict__ dy,
                                  const uint8_t *__restrict__ aux,
                                  float *__restrict__ dpre, int out, int n,
                                  int64_t ld_bits) {
  int o = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int ni = (int)blockIdx.y;
  if (o >= out || ni >= n)
    return;
  const size_t bit = (size_t)ni * (size_t)ld_bits + (size_t)o;
  const uint8_t keep = (uint8_t)((aux[bit >> 3] >> (bit & 7)) & 1u);
  dpre[(size_t)ni * (size_t)out + (size_t)o] =
      keep ? dy[(size_t)ni * (size_t)out + (size_t)o] : 0.f;
}

__global__ void k_add_bias(float *__restrict__ y, const float *__restrict__ b,
                           int out, int n) {
  int o = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int ni = (int)blockIdx.y;
  if (o >= out || ni >= n)
    return;
  y[(size_t)ni * (size_t)out + (size_t)o] += b[o];
}

/* Fallback fwd: y = relu(y + optional b) and pack aux. y holds GEMM result. */
__global__ void k_bias_relu_aux(float *__restrict__ y, const float *__restrict__ b,
                                uint8_t *__restrict__ aux, int out, int n,
                                int64_t ld_bits, int add_bias) {
  int o = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int ni = (int)blockIdx.y;
  if (o >= out || ni >= n)
    return;
  float v = y[(size_t)ni * (size_t)out + (size_t)o];
  if (add_bias && b)
    v += b[o];
  const int keep = v > 0.f;
  y[(size_t)ni * (size_t)out + (size_t)o] = keep ? v : 0.f;
  if (!aux)
    return;
  const size_t bit = (size_t)ni * (size_t)ld_bits + (size_t)o;
  if (keep)
    atomicOr((unsigned int *)(aux + ((bit >> 5) << 2)), 1u << (bit & 31));
}

static int ensure_ones(NnLtGemm *g, int n) {
  if (n <= 0)
    return -1;
  if (g->d_ones && g->ones_n >= n)
    return 0;
  float *p = nullptr;
  if (cudaMalloc(&p, (size_t)n * sizeof(float)) != cudaSuccess)
    return -1;
  k_fill_ones<<<grid_x(n, 256), 256>>>(p, n);
  if (cudaGetLastError() != cudaSuccess) {
    cudaFree(p);
    return -1;
  }
  if (g->d_ones)
    cudaFree(g->d_ones);
  g->d_ones = p;
  g->ones_n = n;
  return 0;
}

static int ensure_I(NnLtGemm *g, int n) {
  if (n <= 0)
    return -1;
  if (g->d_I && g->I_n == n)
    return 0;
  float *p = nullptr;
  if (cudaMalloc(&p, (size_t)n * (size_t)n * sizeof(float)) != cudaSuccess)
    return -1;
  dim3 grid((unsigned)grid_x(n, 256), (unsigned)n, 1);
  k_identity<<<grid, 256>>>(p, n);
  if (cudaGetLastError() != cudaSuccess) {
    cudaFree(p);
    return -1;
  }
  if (g->d_I)
    cudaFree(g->d_I);
  g->d_I = p;
  g->I_n = n;
  return 0;
}

static int ensure_pre(NnLtGemm *g, int out, int n) {
  if (out <= 0 || n <= 0)
    return -1;
  const size_t need = (size_t)out * (size_t)n;
  if (g->d_pre && (size_t)g->pre_out * (size_t)g->pre_n >= need) {
    g->pre_out = out;
    g->pre_n = n;
    return 0;
  }
  float *p = nullptr;
  if (cudaMalloc(&p, need * sizeof(float)) != cudaSuccess)
    return -1;
  if (g->d_pre)
    cudaFree(g->d_pre);
  g->d_pre = p;
  g->pre_out = out;
  g->pre_n = n;
  return 0;
}

static int ensure_ws(NnLtGemm *g, size_t need) {
  if (!g->ws)
    return -1;
  if (need < (size_t)kWsMin)
    need = (size_t)kWsMin;
  return nn_ws_ensure(g->ws, need);
}

/* Free device bytes minus a margin. Mirrors ws_budget() in cuda_conv_graph.cu
 * so a plan pick never eats the VRAM the rest of the step needs.
 * INT64_MAX when the query fails, so a failed query keeps the old behavior. */
static int64_t time_budget(void) {
  size_t freeb = 0, totalb = 0;
  const int64_t margin = (int64_t)1 << 30; /* 1 GiB, = kWsMargin */
  if (cudaMemGetInfo(&freeb, &totalb) != cudaSuccess)
    return INT64_MAX;
  if ((int64_t)freeb <= margin)
    return 0;
  return (int64_t)freeb - margin;
}

/* Scratch for the timed pick. Hands the old buffer back first, so the budget
 * sees those bytes as free. Fails (never OOMs) when the budget says no; the
 * caller then picks on the cuBLASLt heuristic order alone. */
static int ensure_time(NnLtGemm *g, size_t bytes) {
  if (bytes == 0)
    bytes = 1;
  if (g->d_time && g->time_bytes >= bytes)
    return 0;
  if (g->d_time) {
    cudaFree(g->d_time);
    g->d_time = nullptr;
    g->time_bytes = 0;
  }
  if ((int64_t)bytes > time_budget())
    return -1;
  float *p = nullptr;
  if (cudaMalloc(&p, bytes) != cudaSuccess)
    return -1;
  g->d_time = p;
  g->time_bytes = bytes;
  return 0;
}

/* Drop the timing scratch. Only safe once no further pick can happen. */
static void free_time(NnLtGemm *g) {
  if (!g->d_time)
    return;
  cudaFree(g->d_time);
  g->d_time = nullptr;
  g->time_bytes = 0;
}

static GemmSpec spec_fwd(int n, int out, int k, cublasLtEpilogue_t epi) {
  GemmSpec s{};
  s.ta = CUBLAS_OP_T;
  s.tb = CUBLAS_OP_N;
  s.m = out;
  s.n = n;
  s.k = k;
  s.lda = k;
  s.ldb = k;
  s.ldc = out;
  s.epi = epi;
  s.has_bias = 1;
  s.has_aux = (epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS ||
               epi == CUBLASLT_EPILOGUE_RELU_AUX);
  s.alpha = 1.f;
  s.beta = 0.f;
  return s;
}

static GemmSpec spec_dx(int n, int out, int k, float beta) {
  GemmSpec s{};
  s.ta = CUBLAS_OP_N;
  s.tb = CUBLAS_OP_N;
  s.m = k;
  s.n = n;
  s.k = out;
  s.lda = k;
  s.ldb = out;
  s.ldc = k;
  s.epi = CUBLASLT_EPILOGUE_DEFAULT;
  s.alpha = 1.f;
  s.beta = beta;
  return s;
}

static GemmSpec spec_dw(int n, int out, int k, cublasLtEpilogue_t epi,
                        float beta) {
  GemmSpec s{};
  s.ta = CUBLAS_OP_N;
  s.tb = CUBLAS_OP_T;
  s.m = k;
  s.n = out;
  s.k = n;
  s.lda = k;
  s.ldb = out;
  s.ldc = k;
  s.epi = epi;
  s.has_bias = (epi == CUBLASLT_EPILOGUE_BGRADB);
  s.alpha = 1.f;
  s.beta = beta;
  return s;
}

static GemmSpec spec_drelu_I(int n, int out, cublasLtEpilogue_t epi) {
  GemmSpec s{};
  s.ta = CUBLAS_OP_N;
  s.tb = CUBLAS_OP_N;
  s.m = out;
  s.n = n;
  s.k = out;
  s.lda = out;
  s.ldb = out;
  s.ldc = out;
  s.epi = epi;
  s.has_bias = (epi == CUBLASLT_EPILOGUE_DRELU_BGRAD);
  s.has_aux = 1;
  s.alpha = 1.f;
  s.beta = 0.f;
  return s;
}

static int make_layouts(const GemmSpec *s, cublasLtMatrixLayout_t *Ad,
                        cublasLtMatrixLayout_t *Bd, cublasLtMatrixLayout_t *Cd,
                        cublasLtMatrixLayout_t *Dd) {
  const int64_t ar = s->ta == CUBLAS_OP_N ? s->m : s->k;
  const int64_t ac = s->ta == CUBLAS_OP_N ? s->k : s->m;
  const int64_t br = s->tb == CUBLAS_OP_N ? s->k : s->n;
  const int64_t bc = s->tb == CUBLAS_OP_N ? s->n : s->k;
  if (cublasLtMatrixLayoutCreate(Ad, CUDA_R_32F, ar, ac, s->lda) !=
      CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatrixLayoutCreate(Bd, CUDA_R_32F, br, bc, s->ldb) !=
      CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatrixLayoutCreate(Cd, CUDA_R_32F, s->m, s->n, s->ldc) !=
      CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatrixLayoutCreate(Dd, CUDA_R_32F, s->m, s->n, s->ldc) !=
      CUBLAS_STATUS_SUCCESS)
    return -1;
  return 0;
}

static int make_desc(const NnLtGemm *g, const GemmSpec *s, void *bias, void *aux,
                     int64_t aux_ld, cublasLtMatmulDesc_t *desc) {
  cublasComputeType_t comp =
      (g && g->prec == NN_PREC_F32) ? CUBLAS_COMPUTE_32F : CUBLAS_COMPUTE_32F_FAST_TF32;
  if (cublasLtMatmulDescCreate(desc, comp, CUDA_R_32F) != CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatmulDescSetAttribute(*desc, CUBLASLT_MATMUL_DESC_TRANSA, &s->ta,
                                     sizeof(s->ta)) != CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatmulDescSetAttribute(*desc, CUBLASLT_MATMUL_DESC_TRANSB, &s->tb,
                                     sizeof(s->tb)) != CUBLAS_STATUS_SUCCESS)
    return -1;
  if (cublasLtMatmulDescSetAttribute(*desc, CUBLASLT_MATMUL_DESC_EPILOGUE,
                                     &s->epi, sizeof(s->epi)) !=
      CUBLAS_STATUS_SUCCESS)
    return -1;
  if (s->has_bias && bias) {
    cudaDataType_t bt = CUDA_R_32F;
    cublasLtMatmulDescSetAttribute(*desc, CUBLASLT_MATMUL_DESC_BIAS_DATA_TYPE,
                                    &bt, sizeof(bt));
    if (cublasLtMatmulDescSetAttribute(*desc, CUBLASLT_MATMUL_DESC_BIAS_POINTER,
                                        &bias, sizeof(bias)) !=
        CUBLAS_STATUS_SUCCESS)
      return -1;
  }
  if (s->has_aux && aux) {
    if (cublasLtMatmulDescSetAttribute(
            *desc, CUBLASLT_MATMUL_DESC_EPILOGUE_AUX_POINTER, &aux,
            sizeof(aux)) != CUBLAS_STATUS_SUCCESS)
      return -1;
    if (cublasLtMatmulDescSetAttribute(*desc,
                                        CUBLASLT_MATMUL_DESC_EPILOGUE_AUX_LD,
                                        &aux_ld, sizeof(aux_ld)) !=
        CUBLAS_STATUS_SUCCESS)
      return -1;
  }
  return 0;
}

static void destroy_layouts(cublasLtMatrixLayout_t Ad, cublasLtMatrixLayout_t Bd,
                            cublasLtMatrixLayout_t Cd,
                            cublasLtMatrixLayout_t Dd,
                            cublasLtMatmulDesc_t desc) {
  if (Ad)
    cublasLtMatrixLayoutDestroy(Ad);
  if (Bd)
    cublasLtMatrixLayoutDestroy(Bd);
  if (Cd)
    cublasLtMatrixLayoutDestroy(Cd);
  if (Dd)
    cublasLtMatrixLayoutDestroy(Dd);
  if (desc)
    cublasLtMatmulDescDestroy(desc);
}

static int launch_lt(NnLtGemm *g, const GemmSpec *s,
                     const cublasLtMatmulAlgo_t *algo, const void *A,
                     const void *B, const void *C, void *D, void *bias,
                     void *aux, int64_t aux_ld, size_t ws_cap) {
  cublasLtMatmulDesc_t desc = nullptr;
  cublasLtMatrixLayout_t Ad = nullptr, Bd = nullptr, Cd = nullptr, Dd = nullptr;
  if (make_desc(g, s, bias, aux, aux_ld, &desc) != 0)
    goto fail;
  if (make_layouts(s, &Ad, &Bd, &Cd, &Dd) != 0)
    goto fail;
  LT_OK(cublasLtMatmul(g->lt, desc, &s->alpha, A, Ad, B, Bd, &s->beta, C, Cd, D,
                       Dd, algo, g->ws->ptr, ws_cap, 0));
  destroy_layouts(Ad, Bd, Cd, Dd, desc);
  return 0;
fail:
  destroy_layouts(Ad, Bd, Cd, Dd, desc);
  return -1;
}

static bool algo_disallows_tf32(const cublasLtMatmulAlgo_t *algo) {
  uint64_t flags = 0;
  if (cublasLtMatmulAlgoCapGetAttribute(
          algo, CUBLASLT_ALGO_CAP_NUMERICAL_IMPL_FLAGS, &flags, sizeof(flags),
          nullptr) == CUBLAS_STATUS_SUCCESS) {
    if ((flags & CUBLASLT_NUMERICAL_IMPL_FLAGS_INPUT_TF32) != 0)
      return false;
  }
  return true;
}

static int heur_and_time(NnLtGemm *g, GemmSpec s, const void *A, const void *B,
                         const void *C, void *D, void *bias, void *aux,
                         int64_t aux_ld, cublasLtMatmulAlgo_t *algo_out,
                         size_t *ws_out) {
  cublasLtMatmulDesc_t desc = nullptr;
  cublasLtMatrixLayout_t Ad = nullptr, Bd = nullptr, Cd = nullptr, Dd = nullptr;
  cublasLtMatmulPreference_t pref = nullptr;
  cublasLtMatmulHeuristicResult_t heur[kHeurN];
  int nret = 0;
  int rc = -1;
  const size_t ws_cap = g->ws && g->ws->bytes ? g->ws->bytes : 0;
  /* Time into scratch, beta=0. Never write the caller's D. */
  s.beta = 0.f;
  const size_t out_bytes = (size_t)s.ldc * (size_t)s.n * sizeof(float);
  const int can_time = (ensure_time(g, out_bytes) == 0);
  if (can_time) {
    D = g->d_time;
    C = g->d_time;
  }

  std::memset(heur, 0, sizeof(heur));
  if (make_desc(g, &s, bias, aux, aux_ld, &desc) != 0)
    goto done;
  if (make_layouts(&s, &Ad, &Bd, &Cd, &Dd) != 0)
    goto done;
  if (cublasLtMatmulPreferenceCreate(&pref) != CUBLAS_STATUS_SUCCESS)
    goto done;
  if (cublasLtMatmulPreferenceSetAttribute(
          pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &ws_cap,
          sizeof(ws_cap)) != CUBLAS_STATUS_SUCCESS)
    goto done;

  if (cublasLtMatmulAlgoGetHeuristic(g->lt, desc, Ad, Bd, Cd, Dd, pref, kHeurN,
                                     heur, &nret) != CUBLAS_STATUS_SUCCESS)
    nret = 0;

  if (!can_time) {
    /* The timing scratch did not fit the free-VRAM budget. Take cuBLASLt's own
     * first choice that fits the workspace instead of failing the pick. */
    for (int i = 0; i < nret; ++i) {
      if (heur[i].state != CUBLAS_STATUS_SUCCESS)
        continue;
      if (heur[i].workspaceSize > ws_cap)
        continue;
      if (g && g->prec == NN_PREC_F32 && !algo_disallows_tf32(&heur[i].algo))
        continue;
      *algo_out = heur[i].algo;
      *ws_out = heur[i].workspaceSize;
      rc = 0;
      std::fprintf(stderr,
                   "nn_lt heur-only pick m=%d n=%d k=%d epi=%d ws=%lld "
                   "(timing scratch %lld B over budget)\n",
                   s.m, s.n, s.k, (int)s.epi, (long long)heur[i].workspaceSize,
                   (long long)out_bytes);
      break;
    }
    goto done;
  }

  {
    int ntime = 0;
    int best_i = -1;
    float best_ms = 1e30f;
    for (int i = 0; i < nret && ntime < kTimeN; ++i) {
      if (heur[i].state != CUBLAS_STATUS_SUCCESS)
        continue;
      if (heur[i].workspaceSize > ws_cap)
        continue;
      if (g && g->prec == NN_PREC_F32 && !algo_disallows_tf32(&heur[i].algo))
        continue;
      float times[kRepeat];
      int ok = 1;
      for (int r = 0; r < kRepeat; ++r) {
        if (cudaEventRecord(g->ev0, 0) != cudaSuccess) {
          ok = 0;
          break;
        }
        if (cublasLtMatmul(g->lt, desc, &s.alpha, A, Ad, B, Bd, &s.beta, C, Cd,
                           D, Dd, &heur[i].algo, g->ws->ptr, ws_cap, 0) !=
            CUBLAS_STATUS_SUCCESS) {
          ok = 0;
          break;
        }
        if (cudaEventRecord(g->ev1, 0) != cudaSuccess) {
          ok = 0;
          break;
        }
        if (cudaEventSynchronize(g->ev1) != cudaSuccess) {
          ok = 0;
          break;
        }
        if (cudaEventElapsedTime(&times[r], g->ev0, g->ev1) != cudaSuccess) {
          ok = 0;
          break;
        }
      }
      if (!ok)
        continue;
      float med = times[0];
      for (int a = 0; a < kRepeat; ++a)
        for (int b = a + 1; b < kRepeat; ++b)
          if (times[b] < times[a]) {
            float t = times[a];
            times[a] = times[b];
            times[b] = t;
          }
      med = times[kRepeat / 2];
      if (best_i < 0 || med < best_ms) {
        best_ms = med;
        best_i = i;
      }
      ++ntime;
    }
    if (best_i < 0)
      goto done;
    *algo_out = heur[best_i].algo;
    *ws_out = heur[best_i].workspaceSize;
    rc = 0;
  }

done:
  if (pref)
    cublasLtMatmulPreferenceDestroy(pref);
  destroy_layouts(Ad, Bd, Cd, Dd, desc);
  return rc;
}

static Plan *lookup(NnLtGemm *g, int kind, int n, int out, int k, int lda,
                    int ldc) {
  for (int i = 0; i < g->n_plans; ++i) {
    Plan *p = &g->plans[i];
    if (p->valid && p->kind == kind && p->n == n && p->out == out && p->k == k &&
        p->lda == lda && p->ldc == ldc && p->device == g->device)
      return p;
  }
  return nullptr;
}

static int pinned_n(const NnLtGemm *g, int n) {
  for (int i = 0; i < g->n_prep; ++i)
    if (g->prep_ns[i] == n)
      return 1;
  return 0;
}

/* Plans are keyed by exact n. An unsealed run (ppo mb=0) updates on the
 * compacted valid rows, so n_tr changes every chunk and each new n costs one
 * plan per kind. The table is a ring over those plans: when it is full the
 * oldest plan whose n is not pinned is reused. Pinned n (the rollout and
 * update buckets, via nn_lt_pin) stay. A sealed run never gets here. */
static Plan *alloc_plan(NnLtGemm *g) {
  if (g->n_plans < kPlanCap) {
    Plan *p = &g->plans[g->n_plans++];
    std::memset(p, 0, sizeof(*p));
    return p;
  }
  for (int t = 0; t < kPlanCap; ++t) {
    int i = (g->evict_cur + t) % kPlanCap;
    Plan *p = &g->plans[i];
    if (pinned_n(g, p->n))
      continue;
    g->evict_cur = (i + 1) % kPlanCap;
    if (g->n_evict == 0)
      std::fprintf(stderr,
                   "nn_lt evict: plan table full (%d), reusing kind=%d n=%d "
                   "(later evictions are silent)\n",
                   kPlanCap, p->kind, p->n);
    g->n_evict++;
    std::memset(p, 0, sizeof(*p));
    return p;
  }
  set_err("nn_lt: plan table full and every plan is pinned");
  return nullptr;
}

static int pick_plan(NnLtGemm *g, int kind, int n, int out, int k, GemmSpec s,
                     const void *A, const void *B, const void *C, void *D,
                     void *bias, void *aux, int64_t aux_ld,
                     const cublasLtEpilogue_t *fallback_epi, int n_fallback,
                     Plan **outp) {
  Plan *p = lookup(g, kind, n, out, k, s.lda, s.ldc);
  if (p) {
    *outp = p;
    return 0;
  }
  if (g->sealed) {
    /* Every plan was picked before the run. A pick here would query and time
     * cuBLASLt algos mid-step and allocate GiB-scale timing scratch. */
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "nn: unprepared lt plan kind=%d n=%d out=%d k=%d lda=%d "
                  "ldc=%d max_n=%d plans=%d",
                  kind, n, out, k, s.lda, s.ldc, g->max_n, g->n_plans);
    set_err(msg);
    return -1;
  }
  p = alloc_plan(g);
  if (!p)
    return -1;
  p->kind = kind;
  p->n = n;
  p->out = out;
  p->k = k;
  p->lda = s.lda;
  p->ldc = s.ldc;
  p->device = g->device;
  if (ensure_ws(g, kWsMin) != 0)
    return -1;

  cublasLtEpilogue_t try_epi[6];
  int nt = 0;
  try_epi[nt++] = s.epi;
  for (int i = 0; i < n_fallback; ++i)
    try_epi[nt++] = fallback_epi[i];

  for (int t = 0; t < nt; ++t) {
    GemmSpec st = s;
    st.epi = try_epi[t];
    st.has_bias =
        (st.epi == CUBLASLT_EPILOGUE_BIAS ||
         st.epi == CUBLASLT_EPILOGUE_RELU_BIAS ||
         st.epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS ||
         st.epi == CUBLASLT_EPILOGUE_DRELU_BGRAD ||
         st.epi == CUBLASLT_EPILOGUE_BGRADB);
    st.has_aux =
        (st.epi == CUBLASLT_EPILOGUE_RELU_AUX ||
         st.epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS ||
         st.epi == CUBLASLT_EPILOGUE_DRELU ||
         st.epi == CUBLASLT_EPILOGUE_DRELU_BGRAD);
    if (heur_and_time(g, st, A, B, C, D, bias, aux, aux_ld, &p->algo,
                      &p->ws_need) == 0) {
      p->epi = st.epi;
      p->fallback = (st.epi != s.epi) ? 1 : 0;
      p->valid = 1;
      std::fprintf(stderr,
                   "nn_lt pick kind=%d n=%d out=%d k=%d epi=%d ws=%lld "
                   "fallback=%d\n",
                   kind, n, out, k, (int)p->epi, (long long)p->ws_need,
                   p->fallback);
      *outp = p;
      return 0;
    }
  }
  return -1;
}

static int sgemv_db(NnLtGemm *g, int out, int n, const float *dpre, float *db) {
  if (!db)
    return 0;
  if (ensure_ones(g, n) != 0)
    return -1;
  const float alpha = 1.f, beta = 0.f;
  if (cublasSgemv(g->blas, CUBLAS_OP_N, out, n, &alpha, dpre, out, g->d_ones, 1,
                  &beta, db, 1) != CUBLAS_STATUS_SUCCESS)
    return -1;
  return 0;
}

NnLtGemm *nn_lt_create(cublasLtHandle_t lt, int device, int max_n,
                       NnWsArena *ws, NnPrec prec) {
  if (!lt || !ws || max_n < 1 || device < 0)
    return nullptr;
  if (cudaSetDevice(device) != cudaSuccess)
    return nullptr;
  NnLtGemm *g = new NnLtGemm();
  std::memset(g, 0, sizeof(*g));
  g->lt = lt;
  g->device = device;
  g->max_n = max_n;
  g->prec = prec;
  g->prepared_n = -1;
  g->ws = ws;
  if (cublasCreate(&g->blas) != CUBLAS_STATUS_SUCCESS) {
    delete g;
    return nullptr;
  }
  if (cudaEventCreate(&g->ev0) != cudaSuccess ||
      cudaEventCreate(&g->ev1) != cudaSuccess) {
    cublasDestroy(g->blas);
    delete g;
    return nullptr;
  }
  if (ensure_ws(g, kWsMin) != 0 || ensure_ones(g, max_n) != 0) {
    nn_lt_destroy(g);
    return nullptr;
  }
  return g;
}

void nn_lt_destroy(NnLtGemm *g) {
  if (!g)
    return;
  if (g->d_ones)
    cudaFree(g->d_ones);
  if (g->d_I)
    cudaFree(g->d_I);
  if (g->d_pre)
    cudaFree(g->d_pre);
  if (g->d_time)
    cudaFree(g->d_time);
  if (g->ev0)
    cudaEventDestroy(g->ev0);
  if (g->ev1)
    cudaEventDestroy(g->ev1);
  if (g->blas)
    cublasDestroy(g->blas);
  delete g;
}

long long nn_lt_max_ws(const NnLtGemm *g) {
  if (!g)
    return 0;
  size_t m = 0;
  for (int i = 0; i < g->n_plans; ++i)
    if (g->plans[i].valid && g->plans[i].ws_need > m)
      m = g->plans[i].ws_need;
  return (long long)m;
}

int nn_lt_seal(NnLtGemm *g) {
  if (!g)
    return -1;
  if (cudaSetDevice(g->device) != cudaSuccess)
    return -1;
  cudaDeviceSynchronize();
  /* No pick can follow, so the timing scratch is dead weight. Freeing it
   * returns the largest single transient of the whole create path. */
  free_time(g);
  g->sealed = 1;
  std::fprintf(stderr, "nn_lt seal: %d plans, max ws=%lld B\n", g->n_plans,
               nn_lt_max_ws(g));
  return 0;
}

const char *nn_lt_last_error(void) { return g_err; }

int nn_lt_has_n(const NnLtGemm *g, int n) {
  if (!g || n < 1 || n > g->max_n)
    return 0;
  /* Plans are keyed by exact n. The conv side buckets n, so a bucket hit says
   * nothing about this side; the seal guard asks both. */
  for (int i = 0; i < g->n_plans; ++i)
    if (g->plans[i].valid && g->plans[i].n == n)
      return 1;
  return 0;
}

int nn_lt_prepare(NnLtGemm *g, int n) {
  if (!g || n < 1 || n > g->max_n)
    return -1;
  if (g->prepared_n == n)
    return 0;
  if (cudaSetDevice(g->device) != cudaSuccess)
    return -1;
  if (ensure_ws(g, kWsMin) != 0)
    return -1;
  if (ensure_ones(g, n > g->max_n ? n : g->max_n) != 0)
    return -1;
  g->prepared_n = n;
  return 0;
}

/* nn_lt_prepare runs on every forward, so it cannot mark buckets. The conv
 * side calls this once per prepared bucket (rollout n, update n, max_n). */
int nn_lt_pin(NnLtGemm *g, int n) {
  if (!g || n < 1 || n > g->max_n)
    return -1;
  if (pinned_n(g, n))
    return 0;
  if (g->n_prep >= kPrepCap)
    return -1;
  g->prep_ns[g->n_prep++] = n;
  return 0;
}

int nn_lt_fwd_relu_bias(NnLtGemm *g, int n, int out, int k, const float *W,
                        const float *x, const float *b, float *y, void *aux) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || !W || !x || !b || !y ||
      !aux)
    return -1;
  const int64_t ald = aux_ld_bits(out);
  GemmSpec s = spec_fwd(n, out, k, CUBLASLT_EPILOGUE_RELU_AUX_BIAS);
  Plan *p = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_RELU_BIAS,
                                   CUBLASLT_EPILOGUE_BIAS,
                                   CUBLASLT_EPILOGUE_DEFAULT};
  if (pick_plan(g, KIND_FWD_RELU, n, out, k, s, W, x, y, y, (void *)b, aux, ald,
                fb, 3, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.has_bias = (p->epi != CUBLASLT_EPILOGUE_DEFAULT);
  s.has_aux = (p->epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS ||
               p->epi == CUBLASLT_EPILOGUE_RELU_AUX);
  if (launch_lt(g, &s, &p->algo, W, x, y, y, (void *)b, aux, ald,
                g->ws->bytes) != 0)
    return -1;
  if (p->epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS)
    return 0;
  /* Fallback: finish ReLU + aux (and bias if the GEMM had none). */
  const int add_bias = (p->epi == CUBLASLT_EPILOGUE_DEFAULT);
  if (p->epi == CUBLASLT_EPILOGUE_DEFAULT || p->epi == CUBLASLT_EPILOGUE_BIAS) {
    cudaMemset(aux, 0, (size_t)(ald / 8) * (size_t)n);
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_bias_relu_aux<<<grid, 256>>>(y, b, (uint8_t *)aux, out, n, ald, add_bias);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  } else if (p->epi == CUBLASLT_EPILOGUE_RELU_BIAS) {
    cudaMemset(aux, 0, (size_t)(ald / 8) * (size_t)n);
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_bias_relu_aux<<<grid, 256>>>(y, nullptr, (uint8_t *)aux, out, n, ald, 0);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  }
  return 0;
}

int nn_lt_fwd_bias(NnLtGemm *g, int n, int out, int k, const float *W,
                   const float *x, const float *b, float *y) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || !W || !x || !b || !y)
    return -1;
  GemmSpec s = spec_fwd(n, out, k, CUBLASLT_EPILOGUE_BIAS);
  s.has_aux = 0;
  Plan *p = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_DEFAULT};
  if (pick_plan(g, KIND_FWD_BIAS, n, out, k, s, W, x, y, y, (void *)b, nullptr,
                0, fb, 1, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.has_bias = (p->epi == CUBLASLT_EPILOGUE_BIAS);
  if (launch_lt(g, &s, &p->algo, W, x, y, y, (void *)b, nullptr, 0,
                g->ws->bytes) != 0)
    return -1;
  if (p->epi == CUBLASLT_EPILOGUE_BIAS)
    return 0;
  dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
  k_add_bias<<<grid, 256>>>(y, b, out, n);
  if (cudaGetLastError() != cudaSuccess)
    return -1;
  return 0;
}

int nn_lt_bwd_drelu_bgrad(NnLtGemm *g, int n, int out, int k, const float *W,
                          const float *dy, const void *aux, float *dx,
                          float *db, float beta_dx) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || !W || !dy || !aux ||
      !dx || !db)
    return -1;
  if (beta_dx != 0.f && beta_dx != 1.f)
    return -1;
  if (ensure_I(g, out) != 0 || ensure_pre(g, out, n) != 0)
    return -1;
  const int64_t ald = aux_ld_bits(out);
  GemmSpec sd = spec_drelu_I(n, out, CUBLASLT_EPILOGUE_DRELU_BGRAD);
  Plan *pd = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_DRELU};
  int have_drelu = 0;
  if (pick_plan(g, KIND_DRELU, n, out, out, sd, g->d_I, dy, g->d_pre, g->d_pre,
                db, (void *)aux, ald, fb, 1, &pd) == 0) {
    sd.epi = pd->epi;
    sd.has_bias = (pd->epi == CUBLASLT_EPILOGUE_DRELU_BGRAD);
    sd.has_aux = (pd->epi == CUBLASLT_EPILOGUE_DRELU_BGRAD ||
                  pd->epi == CUBLASLT_EPILOGUE_DRELU);
    if (launch_lt(g, &sd, &pd->algo, g->d_I, dy, g->d_pre, g->d_pre, db,
                  (void *)aux, ald, g->ws->bytes) == 0)
      have_drelu = 1;
  }
  if (!have_drelu) {
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_apply_relu_mask<<<grid, 256>>>(dy, (const uint8_t *)aux, g->d_pre, out, n,
                                     ald);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  }
  if (!have_drelu || (pd && pd->epi != CUBLASLT_EPILOGUE_DRELU_BGRAD)) {
    if (sgemv_db(g, out, n, g->d_pre, db) != 0)
      return -1;
  }
  GemmSpec sx = spec_dx(n, out, k, beta_dx);
  Plan *px = nullptr;
  if (pick_plan(g, KIND_DX, n, out, k, sx, W, g->d_pre, dx, dx, nullptr, nullptr,
                0, nullptr, 0, &px) != 0)
    return -1;
  sx.epi = px->epi;
  return launch_lt(g, &sx, &px->algo, W, g->d_pre, dx, dx, nullptr, nullptr, 0,
                   g->ws->bytes);
}

int nn_lt_bwd_dw_bgrad(NnLtGemm *g, int n, int out, int k, const float *dy,
                       const float *x, float *dW, float *db, float beta_dw) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || !dy || !x || !dW)
    return -1;
  if (beta_dw != 0.f && beta_dw != 1.f)
    return -1;
  const cublasLtEpilogue_t want =
      db ? CUBLASLT_EPILOGUE_BGRADB : CUBLASLT_EPILOGUE_DEFAULT;
  GemmSpec s = spec_dw(n, out, k, want, beta_dw);
  Plan *p = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_DEFAULT};
  if (pick_plan(g, KIND_DW, n, out, k, s, x, dy, dW, dW, db, nullptr, 0, fb,
                db ? 1 : 0, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.has_bias = (p->epi == CUBLASLT_EPILOGUE_BGRADB && db);
  if (launch_lt(g, &s, &p->algo, x, dy, dW, dW, db, nullptr, 0, g->ws->bytes) !=
      0)
    return -1;
  if (db && p->epi != CUBLASLT_EPILOGUE_BGRADB) {
    if (sgemv_db(g, out, n, dy, db) != 0)
      return -1;
  }
  return 0;
}

int nn_lt_bwd_dx(NnLtGemm *g, int n, int out, int k, const float *W,
                 const float *dy, float *dx, float beta_dx) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || !W || !dy || !dx)
    return -1;
  if (beta_dx != 0.f && beta_dx != 1.f)
    return -1;
  GemmSpec s = spec_dx(n, out, k, beta_dx);
  Plan *p = nullptr;
  if (pick_plan(g, KIND_DX, n, out, k, s, W, dy, dx, dx, nullptr, nullptr, 0,
                nullptr, 0, &p) != 0)
    return -1;
  s.epi = p->epi;
  return launch_lt(g, &s, &p->algo, W, dy, dx, dx, nullptr, nullptr, 0,
                   g->ws->bytes);
}

int nn_lt_gemm(NnLtGemm *g, int n, int out, int k, int lda, const float *W,
               const float *x, float *y, float beta) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || lda < k || !W || !x ||
      !y)
    return -1;
  if (beta != 0.f && beta != 1.f)
    return -1;
  GemmSpec s = spec_fwd(n, out, k, CUBLASLT_EPILOGUE_DEFAULT);
  s.lda = lda;
  s.has_bias = 0;
  s.has_aux = 0;
  s.beta = beta;
  Plan *p = nullptr;
  if (pick_plan(g, KIND_GEMM, n, out, k, s, W, x, y, y, nullptr, nullptr, 0,
                nullptr, 0, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.beta = beta;
  return launch_lt(g, &s, &p->algo, W, x, y, y, nullptr, nullptr, 0,
                   g->ws->bytes);
}

int nn_lt_fwd_relu_bias_ex(NnLtGemm *g, int n, int out, int k, int lda,
                           const float *W, const float *x, const float *b,
                           float *y, void *aux, float beta) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || lda < k || !W || !x ||
      !b || !y || !aux)
    return -1;
  if (beta != 0.f && beta != 1.f)
    return -1;
  const int64_t ald = aux_ld_bits(out);
  GemmSpec s = spec_fwd(n, out, k, CUBLASLT_EPILOGUE_RELU_AUX_BIAS);
  s.lda = lda;
  s.beta = beta;
  Plan *p = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_RELU_BIAS,
                                   CUBLASLT_EPILOGUE_BIAS,
                                   CUBLASLT_EPILOGUE_DEFAULT};
  if (pick_plan(g, KIND_FWD_RELU, n, out, k, s, W, x, y, y, (void *)b, aux, ald,
                fb, 3, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.has_bias = (p->epi != CUBLASLT_EPILOGUE_DEFAULT);
  s.has_aux = (p->epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS ||
               p->epi == CUBLASLT_EPILOGUE_RELU_AUX);
  s.beta = beta;
  if (launch_lt(g, &s, &p->algo, W, x, y, y, (void *)b, aux, ald,
                g->ws->bytes) != 0)
    return -1;
  if (p->epi == CUBLASLT_EPILOGUE_RELU_AUX_BIAS)
    return 0;
  const int add_bias = (p->epi == CUBLASLT_EPILOGUE_DEFAULT);
  if (p->epi == CUBLASLT_EPILOGUE_DEFAULT || p->epi == CUBLASLT_EPILOGUE_BIAS) {
    cudaMemset(aux, 0, (size_t)(ald / 8) * (size_t)n);
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_bias_relu_aux<<<grid, 256>>>(y, b, (uint8_t *)aux, out, n, ald, add_bias);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  } else if (p->epi == CUBLASLT_EPILOGUE_RELU_BIAS) {
    cudaMemset(aux, 0, (size_t)(ald / 8) * (size_t)n);
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_bias_relu_aux<<<grid, 256>>>(y, nullptr, (uint8_t *)aux, out, n, ald, 0);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  }
  return 0;
}

int nn_lt_drelu_bgrad(NnLtGemm *g, int n, int out, const float *dy,
                      const void *aux, float *dpre, float *db) {
  if (!g || n < 1 || n > g->max_n || out < 1 || !dy || !aux || !dpre || !db)
    return -1;
  if (ensure_I(g, out) != 0)
    return -1;
  const int64_t ald = aux_ld_bits(out);
  GemmSpec sd = spec_drelu_I(n, out, CUBLASLT_EPILOGUE_DRELU_BGRAD);
  Plan *pd = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_DRELU};
  int have_drelu = 0;
  if (pick_plan(g, KIND_DRELU, n, out, out, sd, g->d_I, dy, dpre, dpre, db,
                (void *)aux, ald, fb, 1, &pd) == 0) {
    sd.epi = pd->epi;
    sd.has_bias = (pd->epi == CUBLASLT_EPILOGUE_DRELU_BGRAD);
    sd.has_aux = (pd->epi == CUBLASLT_EPILOGUE_DRELU_BGRAD ||
                  pd->epi == CUBLASLT_EPILOGUE_DRELU);
    if (launch_lt(g, &sd, &pd->algo, g->d_I, dy, dpre, dpre, db, (void *)aux,
                  ald, g->ws->bytes) == 0)
      have_drelu = 1;
  }
  if (!have_drelu) {
    dim3 grid((unsigned)grid_x(out, 256), (unsigned)n, 1);
    k_apply_relu_mask<<<grid, 256>>>(dy, (const uint8_t *)aux, dpre, out, n,
                                     ald);
    if (cudaGetLastError() != cudaSuccess)
      return -1;
  }
  if (!have_drelu || (pd && pd->epi != CUBLASLT_EPILOGUE_DRELU_BGRAD)) {
    if (sgemv_db(g, out, n, dpre, db) != 0)
      return -1;
  }
  return 0;
}

int nn_lt_bwd_dw_ex(NnLtGemm *g, int n, int out, int k, int lda,
                    const float *dy, const float *x, float *dW, float *db,
                    float beta_dw) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || lda < k || !dy || !x ||
      !dW)
    return -1;
  if (beta_dw != 0.f && beta_dw != 1.f)
    return -1;
  const cublasLtEpilogue_t want =
      db ? CUBLASLT_EPILOGUE_BGRADB : CUBLASLT_EPILOGUE_DEFAULT;
  GemmSpec s = spec_dw(n, out, k, want, beta_dw);
  s.ldc = lda;
  Plan *p = nullptr;
  const cublasLtEpilogue_t fb[] = {CUBLASLT_EPILOGUE_DEFAULT};
  if (pick_plan(g, KIND_DW, n, out, k, s, x, dy, dW, dW, db, nullptr, 0, fb,
                db ? 1 : 0, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.has_bias = (p->epi == CUBLASLT_EPILOGUE_BGRADB && db);
  s.beta = beta_dw;
  if (launch_lt(g, &s, &p->algo, x, dy, dW, dW, db, nullptr, 0, g->ws->bytes) !=
      0)
    return -1;
  if (db && p->epi != CUBLASLT_EPILOGUE_BGRADB) {
    if (sgemv_db(g, out, n, dy, db) != 0)
      return -1;
  }
  return 0;
}

int nn_lt_bwd_dx_ex(NnLtGemm *g, int n, int out, int k, int lda,
                    const float *W, const float *dy, float *dx, float beta_dx) {
  if (!g || n < 1 || n > g->max_n || out < 1 || k < 1 || lda < k || !W || !dy ||
      !dx)
    return -1;
  if (beta_dx != 0.f && beta_dx != 1.f)
    return -1;
  GemmSpec s = spec_dx(n, out, k, beta_dx);
  s.lda = lda;
  Plan *p = nullptr;
  if (pick_plan(g, KIND_DX, n, out, k, s, W, dy, dx, dx, nullptr, nullptr, 0,
                nullptr, 0, &p) != 0)
    return -1;
  s.epi = p->epi;
  s.beta = beta_dx;
  return launch_lt(g, &s, &p->algo, W, dy, dx, dx, nullptr, nullptr, 0,
                   g->ws->bytes);
}
