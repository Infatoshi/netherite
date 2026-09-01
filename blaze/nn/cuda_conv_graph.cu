/* Shape-agnostic cuDNN-graph conv. NHWC / KRSC, fp16 store, fp32 accumulate.
 * Fable: heur A/B, check_support, time top K=8. See cuda_fable_contract.h. */
#include "cuda_conv_graph.h"
#include "cuda_fable_contract.h"

#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <cudnn.h>
#include <cudnn_backend.h>
#include <cudnn_graph.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kAlign = 16;
constexpr int kTopK = 8;
constexpr int kMaxHeur = 128;
constexpr int kUidX = 1;
constexpr int kUidW = 2;
constexpr int kUidY = 3;
constexpr int kUidB = 4;
constexpr int kUidDy = 5;
constexpr int kUidDw = 6;
constexpr int kUidDx = 7;
constexpr int kUidV0 = 100;
constexpr int kUidV1 = 101;


enum OpKind { OP_FWD = 0, OP_WGRAD = 1, OP_DGRAD = 2 };

struct CacheKey {
  int op, n, C, H, W, K, R, S, pad, stride, dil;
  int dtype, layout, fusion, cudnn_ver, device;
  bool operator<(const CacheKey &o) const {
    const int a[] = {op, n,     C,     H,     W,     K,     R,      S,
                     pad, stride, dil, dtype, layout, fusion, cudnn_ver, device};
    const int c[] = {o.op,     o.n,     o.C,     o.H,     o.W,     o.K,
                     o.R,      o.S,     o.pad,   o.stride, o.dil,  o.dtype,
                     o.layout, o.fusion, o.cudnn_ver, o.device};
    for (int i = 0; i < 16; ++i) {
      if (a[i] < c[i])
        return true;
      if (a[i] > c[i])
        return false;
    }
    return false;
  }
};

struct CachedPlan {
  cudnnBackendDescriptor_t graph = nullptr;
  cudnnBackendDescriptor_t plan = nullptr;
  int64_t ws = 0;
  int64_t engine_id = -1;
  double time_us = 0;
  uint32_t note_mask = 0;
  int n_uids = 0;
  int64_t uids[8] = {};
  char tag[64] = {};
  char notes[192] = {};
};

struct Layer {
  NnConvLayerSpec spec;
  int c_in_pad;
  int h_out;
  int w_out;
};

struct DnnDel {
  void operator()(void *p) const {
    if (p)
      cudnnBackendDestroyDescriptor(static_cast<cudnnBackendDescriptor_t>(p));
  }
};
using UniqueD = std::unique_ptr<void, DnnDel>;

char g_err[512] = "";

void set_err(const char *msg) {
  std::snprintf(g_err, sizeof(g_err), "%s", msg);
  std::fprintf(stderr, "nn_conv_graph: %s\n", msg);
}

void set_errf(const char *fmt, cudnnStatus_t st) {
  std::snprintf(g_err, sizeof(g_err), "%s: %s", fmt, cudnnGetErrorString(st));
  std::fprintf(stderr, "nn_conv_graph: %s\n", g_err);
}

UniqueD make_desc(cudnnBackendDescriptorType_t t) {
  cudnnBackendDescriptor_t d = nullptr;
  if (cudnnBackendCreateDescriptor(t, &d) != CUDNN_STATUS_SUCCESS)
    return UniqueD();
  return UniqueD(d);
}

int pad4(int c) { return (c + 3) & ~3; }

int out_dim(int in, int pad, int k, int stride, int dil) {
  const int win = (k - 1) * dil + 1;
  return (in + 2 * pad - win) / stride + 1;
}

int bucket_n(int n, int max_n) {
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

void nhwc_stride(int64_t n, int64_t c, int64_t h, int64_t w, int64_t *s) {
  (void)n;
  s[0] = h * w * c;
  s[1] = 1;
  s[2] = w * c;
  s[3] = c;
}

void krsc_stride(int64_t k, int64_t c, int64_t r, int64_t s, int64_t *st) {
  (void)k;
  st[0] = r * s * c;
  st[1] = 1;
  st[2] = s * c;
  st[3] = c;
}

int fin(cudnnBackendDescriptor_t d) {
  return cudnnBackendFinalize(d) == CUDNN_STATUS_SUCCESS ? 0 : -1;
}

UniqueD make_tensor(int64_t uid, const int64_t dim[4], const int64_t str[4],
                    bool virt) {
  UniqueD t = make_desc(CUDNN_BACKEND_TENSOR_DESCRIPTOR);
  if (!t)
    return UniqueD();
  cudnnBackendDescriptor_t p = t.get();
  cudnnDataType_t dt = CUDNN_DATA_HALF;
  int64_t align = kAlign;
  bool v = virt;
  if (cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_DATA_TYPE,
                               CUDNN_TYPE_DATA_TYPE, 1, &dt) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_DIMENSIONS, CUDNN_TYPE_INT64,
                               4, dim) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_STRIDES, CUDNN_TYPE_INT64, 4,
                               str) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_UNIQUE_ID, CUDNN_TYPE_INT64,
                               1, &uid) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_BYTE_ALIGNMENT,
                               CUDNN_TYPE_INT64, 1, &align) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_TENSOR_IS_VIRTUAL,
                               CUDNN_TYPE_BOOLEAN, 1, &v) !=
          CUDNN_STATUS_SUCCESS ||
      fin(p) != 0)
    return UniqueD();
  return t;
}

UniqueD make_conv(int pad, int stride, int dil) {
  UniqueD c = make_desc(CUDNN_BACKEND_CONVOLUTION_DESCRIPTOR);
  if (!c)
    return UniqueD();
  cudnnBackendDescriptor_t p = c.get();
  int64_t spatial = 2;
  cudnnDataType_t comp = CUDNN_DATA_FLOAT;
  cudnnConvolutionMode_t mode = CUDNN_CROSS_CORRELATION;
  int64_t pre[2] = {pad, pad};
  int64_t post[2] = {0, 0};
  int64_t strs[2] = {stride, stride};
  int64_t dils[2] = {dil, dil};
  if (cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_SPATIAL_DIMS,
                               CUDNN_TYPE_INT64, 1, &spatial) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_COMP_TYPE,
                               CUDNN_TYPE_DATA_TYPE, 1, &comp) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_CONV_MODE,
                               CUDNN_TYPE_CONVOLUTION_MODE, 1, &mode) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_PRE_PADDINGS,
                               CUDNN_TYPE_INT64, 2, pre) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_POST_PADDINGS,
                               CUDNN_TYPE_INT64, 2, post) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_DILATIONS,
                               CUDNN_TYPE_INT64, 2, dils) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(p, CUDNN_ATTR_CONVOLUTION_FILTER_STRIDES,
                               CUDNN_TYPE_INT64, 2, strs) !=
          CUDNN_STATUS_SUCCESS ||
      fin(p) != 0)
    return UniqueD();
  return c;
}

UniqueD make_pw(cudnnPointwiseMode_t mode) {
  UniqueD d = make_desc(CUDNN_BACKEND_POINTWISE_DESCRIPTOR);
  if (!d)
    return UniqueD();
  cudnnDataType_t prec = CUDNN_DATA_FLOAT;
  if (cudnnBackendSetAttribute(d.get(), CUDNN_ATTR_POINTWISE_MODE,
                               CUDNN_TYPE_POINTWISE_MODE, 1, &mode) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(d.get(), CUDNN_ATTR_POINTWISE_MATH_PREC,
                               CUDNN_TYPE_DATA_TYPE, 1, &prec) !=
          CUDNN_STATUS_SUCCESS)
    return UniqueD();
  if (mode == CUDNN_POINTWISE_RELU_FWD) {
    float lo = 0.f;
    cudnnBackendSetAttribute(d.get(), CUDNN_ATTR_POINTWISE_RELU_LOWER_CLIP,
                             CUDNN_TYPE_FLOAT, 1, &lo);
  }
  if (fin(d.get()) != 0)
    return UniqueD();
  return d;
}

int read_engine(cudnnBackendDescriptor_t cfg, int64_t *id, uint32_t *mask,
                char *notes, int notes_n) {
  UniqueD eng = make_desc(CUDNN_BACKEND_ENGINE_DESCRIPTOR);
  if (!eng)
    return -1;
  cudnnBackendDescriptor_t e = eng.get();
  int64_t nret = 0;
  cudnnStatus_t st = cudnnBackendGetAttribute(
      cfg, CUDNN_ATTR_ENGINECFG_ENGINE, CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &nret,
      &e);
  if (st != CUDNN_STATUS_SUCCESS)
    return -1;
  *id = -1;
  nret = 0;
  cudnnBackendGetAttribute(e, CUDNN_ATTR_ENGINE_GLOBAL_INDEX, CUDNN_TYPE_INT64,
                           1, &nret, id);
  *mask = 0;
  cudnnBackendNumericalNote_t present[CUDNN_NUMERICAL_NOTE_TYPE_COUNT];
  nret = 0;
  st = cudnnBackendGetAttribute(e, CUDNN_ATTR_ENGINE_NUMERICAL_NOTE,
                                CUDNN_TYPE_NUMERICAL_NOTE,
                                CUDNN_NUMERICAL_NOTE_TYPE_COUNT, &nret, present);
  if (st == CUDNN_STATUS_SUCCESS) {
    for (int64_t i = 0; i < nret; ++i) {
      int v = (int)present[i];
      if (v >= 0 && v < 32)
        *mask |= (1u << v);
    }
  } else {
    bool b[CUDNN_NUMERICAL_NOTE_TYPE_COUNT] = {};
    nret = 0;
    st = cudnnBackendGetAttribute(e, CUDNN_ATTR_ENGINE_NUMERICAL_NOTE,
                                  CUDNN_TYPE_BOOLEAN,
                                  CUDNN_NUMERICAL_NOTE_TYPE_COUNT, &nret, b);
    if (st == CUDNN_STATUS_SUCCESS) {
      for (int i = 0; i < CUDNN_NUMERICAL_NOTE_TYPE_COUNT; ++i)
        if (b[i])
          *mask |= (1u << i);
    }
  }
  static const char *kNames[] = {
      "TENSOR_CORE", "DOWN_CONVERT_INPUTS", "REDUCED_PRECISION_REDUCTION",
      "FFT",         "NONDETERMINISTIC",    "WINOGRAD",
      "WINOGRAD_4x4", "WINOGRAD_6x6",       "WINOGRAD_13x13",
      "STRICT_NAN"};
  notes[0] = 0;
  int off = 0;
  for (int i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); ++i) {
    if (!(*mask & (1u << i)))
      continue;
    off += std::snprintf(notes + off, (size_t)(notes_n - off), "%s%s",
                         off ? "," : "", kNames[i]);
    if (off >= notes_n - 1)
      break;
  }
  if (off == 0)
    std::snprintf(notes, (size_t)notes_n, "(none)");
  return 0;
}

int collect_heur(cudnnBackendDescriptor_t graph, cudnnBackendHeurMode_t mode,
                 std::vector<cudnnBackendDescriptor_t> *out) {
  UniqueD heur = make_desc(CUDNN_BACKEND_ENGINEHEUR_DESCRIPTOR);
  if (!heur)
    return -1;
  if (cudnnBackendSetAttribute(heur.get(), CUDNN_ATTR_ENGINEHEUR_OPERATION_GRAPH,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &graph) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(heur.get(), CUDNN_ATTR_ENGINEHEUR_MODE,
                               CUDNN_TYPE_HEUR_MODE, 1, &mode) !=
          CUDNN_STATUS_SUCCESS)
    return -1;
  cudnnStatus_t st = cudnnBackendFinalize(heur.get());
  if (st != CUDNN_STATUS_SUCCESS)
    return 0;
  int64_t count = 0;
  cudnnBackendGetAttribute(heur.get(), CUDNN_ATTR_ENGINEHEUR_RESULTS,
                           CUDNN_TYPE_BACKEND_DESCRIPTOR, 0, &count, nullptr);
  if (count <= 0)
    return 0;
  if (count > kMaxHeur)
    count = kMaxHeur;
  std::vector<cudnnBackendDescriptor_t> cfgs((size_t)count, nullptr);
  for (int64_t i = 0; i < count; ++i) {
    if (cudnnBackendCreateDescriptor(CUDNN_BACKEND_ENGINECFG_DESCRIPTOR,
                                     &cfgs[(size_t)i]) != CUDNN_STATUS_SUCCESS)
      return -1;
  }
  int64_t got = 0;
  st = cudnnBackendGetAttribute(heur.get(), CUDNN_ATTR_ENGINEHEUR_RESULTS,
                                CUDNN_TYPE_BACKEND_DESCRIPTOR, count, &got,
                                cfgs.data());
  if (st != CUDNN_STATUS_SUCCESS)
    got = 0;
  for (int64_t i = 0; i < got; ++i)
    out->push_back(cfgs[(size_t)i]);
  for (int64_t i = got; i < count; ++i)
    cudnnBackendDestroyDescriptor(cfgs[(size_t)i]);
  return 0;
}

int try_plan(cudnnHandle_t h, cudnnBackendDescriptor_t cfg,
             cudnnBackendDescriptor_t *plan_out, int64_t *ws_out) {
  UniqueD plan = make_desc(CUDNN_BACKEND_EXECUTION_PLAN_DESCRIPTOR);
  if (!plan)
    return -1;
  if (cudnnBackendSetAttribute(plan.get(), CUDNN_ATTR_EXECUTION_PLAN_HANDLE,
                               CUDNN_TYPE_HANDLE, 1, &h) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(plan.get(),
                               CUDNN_ATTR_EXECUTION_PLAN_ENGINE_CONFIG,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &cfg) !=
          CUDNN_STATUS_SUCCESS)
    return -1;
  cudnnStatus_t st = cudnnBackendFinalize(plan.get());
  if (st != CUDNN_STATUS_SUCCESS)
    return -1;
  int64_t ws = 0, nret = 0;
  st = cudnnBackendGetAttribute(plan.get(),
                                CUDNN_ATTR_EXECUTION_PLAN_WORKSPACE_SIZE,
                                CUDNN_TYPE_INT64, 1, &nret, &ws);
  if (st != CUDNN_STATUS_SUCCESS)
    return -1;
  *ws_out = ws;
  *plan_out = plan.release();
  return 0;
}

int run_plan(cudnnHandle_t h, cudnnBackendDescriptor_t plan, void *ws,
             const int64_t *uids, void **ptrs, int n_uids) {
  UniqueD vp = make_desc(CUDNN_BACKEND_VARIANT_PACK_DESCRIPTOR);
  if (!vp)
    return -1;
  if (cudnnBackendSetAttribute(vp.get(), CUDNN_ATTR_VARIANT_PACK_DATA_POINTERS,
                               CUDNN_TYPE_VOID_PTR, n_uids, ptrs) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(vp.get(), CUDNN_ATTR_VARIANT_PACK_UNIQUE_IDS,
                               CUDNN_TYPE_INT64, n_uids, uids) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(vp.get(), CUDNN_ATTR_VARIANT_PACK_WORKSPACE,
                               CUDNN_TYPE_VOID_PTR, 1, &ws) !=
          CUDNN_STATUS_SUCCESS ||
      fin(vp.get()) != 0)
    return -1;
  cudnnStatus_t st = cudnnBackendExecute(h, plan, vp.get());
  return st == CUDNN_STATUS_SUCCESS ? 0 : -1;
}

double time_plan(cudnnHandle_t h, cudnnBackendDescriptor_t plan, void *ws,
                 const int64_t *uids, void **ptrs, int n_uids, int warm,
                 int reps) {
  for (int i = 0; i < warm; ++i) {
    if (run_plan(h, plan, ws, uids, ptrs, n_uids) != 0)
      return -1;
  }
  cudaEvent_t a, b;
  if (cudaEventCreate(&a) != cudaSuccess || cudaEventCreate(&b) != cudaSuccess)
    return -1;
  double best = 1e100;
  for (int i = 0; i < reps; ++i) {
    cudaEventRecord(a);
    if (run_plan(h, plan, ws, uids, ptrs, n_uids) != 0) {
      cudaEventDestroy(a);
      cudaEventDestroy(b);
      return -1;
    }
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    float ms = 0;
    cudaEventElapsedTime(&ms, a, b);
    best = std::min(best, (double)ms * 1000.0);
  }
  cudaEventDestroy(a);
  cudaEventDestroy(b);
  return best;
}

bool note_fft(uint32_t m) { return (m & (1u << CUDNN_NUMERICAL_NOTE_FFT)) != 0; }

bool note_wino(uint32_t m) {
  return (m & ((1u << CUDNN_NUMERICAL_NOTE_WINOGRAD) |
               (1u << CUDNN_NUMERICAL_NOTE_WINOGRAD_TILE_4x4) |
               (1u << CUDNN_NUMERICAL_NOTE_WINOGRAD_TILE_6x6) |
               (1u << CUDNN_NUMERICAL_NOTE_WINOGRAD_TILE_13x13))) != 0;
}

bool note_tc_gemm(uint32_t m) {
  const bool tc = (m & (1u << CUDNN_NUMERICAL_NOTE_TENSOR_CORE)) != 0;
  const bool dn = (m & (1u << CUDNN_NUMERICAL_NOTE_DOWN_CONVERT_INPUTS)) != 0;
  return (tc || dn) && !note_fft(m) && !note_wino(m);
}

/* Workspace budget for the timed race. The arena grows to the MAX workspace
 * over the timed set, so one fat candidate can OOM a step that the winner
 * alone fits. Keep this many free bytes back for the rest of the step. */
constexpr int64_t kWsMargin = (int64_t)1 << 30; /* 1 GiB */

/* Never shrink under this. Matches kWsMin in cuda_lt_gemm.cu, which re-grows
 * the shared arena to it on every lt prepare. */
constexpr int64_t kWsFloor = 32 << 20; /* 32 MiB */

/* Free device bytes minus the margin. INT64_MAX when the query fails, so a
 * failed query keeps the old behavior instead of starving the race. */
int64_t ws_budget() {
  size_t freeb = 0, totalb = 0;
  if (cudaMemGetInfo(&freeb, &totalb) != cudaSuccess)
    return INT64_MAX;
  if ((int64_t)freeb <= kWsMargin)
    return 0;
  return (int64_t)freeb - kWsMargin;
}

struct Cand {
  cudnnBackendDescriptor_t cfg = nullptr;
  cudnnBackendDescriptor_t plan = nullptr;
  int64_t engine_id = -1;
  int64_t ws = 0;
  uint32_t mask = 0;
  double us = 1e100;
  char notes[192] = {};
};

/* Bytes the seven timing buffers need at n. */
size_t tbuf_bytes(int n, int C, int H, int W, int K, int R, int S, int Ho,
                  int Wo) {
  const size_t nx = (size_t)n * (size_t)H * (size_t)W * (size_t)C;
  const size_t nw = (size_t)K * (size_t)R * (size_t)S * (size_t)C;
  const size_t ny = (size_t)n * (size_t)Ho * (size_t)Wo * (size_t)K;
  const size_t nb = (size_t)K;
  return (2 * nx + 2 * ny + 2 * nw + nb) * sizeof(__half);
}

/* Next bucket below b on the ladder bucket_n produces. 0 when there is none. */
int prev_bucket(int b, int max_n) {
  const int m = (max_n < 256) ? 32 : 256;
  if (b <= 1)
    return 0;
  int p = ((b - 1) / m) * m;
  if (p < 1)
    p = 1;
  if (p >= b)
    p = b - 1;
  return p;
}

/* Heuristic configs that pass check_support, deduped by engine id. */
int collect_supported(cudnnHandle_t h, cudnnBackendDescriptor_t graph,
                      std::vector<Cand> *out, int *heur_n) {
  std::vector<cudnnBackendDescriptor_t> raw;
  if (collect_heur(graph, CUDNN_HEUR_MODE_A, &raw) != 0 ||
      collect_heur(graph, CUDNN_HEUR_MODE_B, &raw) != 0) {
    set_err("heur query failed");
    for (auto c : raw)
      cudnnBackendDestroyDescriptor(c);
    return -1;
  }
  *heur_n = (int)raw.size();
  if (raw.empty())
    collect_heur(graph, CUDNN_HEUR_MODE_FALLBACK, &raw);
  std::vector<int64_t> seen;
  for (auto cfg : raw) {
    Cand c;
    c.cfg = cfg;
    if (read_engine(cfg, &c.engine_id, &c.mask, c.notes, (int)sizeof(c.notes)) !=
        0) {
      cudnnBackendDestroyDescriptor(cfg);
      continue;
    }
    bool dup = false;
    for (int64_t id : seen)
      if (id == c.engine_id) {
        dup = true;
        break;
      }
    if (dup) {
      cudnnBackendDestroyDescriptor(cfg);
      continue;
    }
    seen.push_back(c.engine_id);
    if (try_plan(h, cfg, &c.plan, &c.ws) != 0) {
      cudnnBackendDestroyDescriptor(cfg);
      continue;
    }
    out->push_back(c);
  }
  if (out->empty()) {
    set_err("no engine passed check_support");
    return -1;
  }
  return 0;
}

/* Destroy every cfg, and every plan but keep_plan. -1 destroys all. */
void destroy_cands(std::vector<Cand> *v, int keep_plan) {
  for (size_t i = 0; i < v->size(); ++i) {
    Cand &c = (*v)[i];
    if (c.plan && (int)i != keep_plan)
      cudnnBackendDestroyDescriptor(c.plan);
    if (c.cfg)
      cudnnBackendDestroyDescriptor(c.cfg);
    c.cfg = nullptr;
    if ((int)i != keep_plan)
      c.plan = nullptr;
  }
}

/* Eligible head of the supported list: the first kTopK that need no grow, or
 * that fit the free-VRAM budget. */
void select_timed(const std::vector<Cand> &sup, int64_t have, int64_t budget,
                  std::vector<int> *tidx, int *capped) {
  const int nsup = (int)sup.size();
  *capped = 0;
  for (int i = 0; i < nsup && (int)tidx->size() < kTopK; ++i)
    if (sup[(size_t)i].ws <= have || sup[(size_t)i].ws <= budget)
      tidx->push_back(i);
  const int nhead = std::min(kTopK, nsup);
  for (int i = 0; i < nhead; ++i)
    if (sup[(size_t)i].ws > have && sup[(size_t)i].ws > budget)
      *capped = 1;
  if (tidx->empty()) {
    /* Nothing fits. Keep the smallest workspace and let the grow speak. */
    int small = 0;
    for (int i = 1; i < nsup; ++i)
      if (sup[(size_t)i].ws < sup[(size_t)small].ws)
        small = i;
    tidx->push_back(small);
    std::fprintf(stderr,
                 "nn_conv_graph ws budget %lld B fits no engine of %d; trying "
                 "smallest ws=%lld B\n",
                 (long long)budget, nsup, (long long)sup[(size_t)small].ws);
  }
}

/* Time the eligible head and pick the winner. Returns the index into sup, or
 * -1. Fills out (not out->graph) and, when rank is given, the timed engine ids
 * best first. Destroys nothing; the caller owns sup. */
int time_and_pick(cudnnHandle_t h, NnWsArena *arena, std::vector<Cand> *sup,
                  const int64_t *uids, int n_uids, void **ptrs, int64_t volume,
                  int heur_n, CachedPlan *out, std::vector<int64_t> *rank) {
  const int nsup = (int)sup->size();
  const int64_t budget = ws_budget();
  const int64_t have = arena ? (int64_t)arena->bytes : 0;
  std::vector<int> tidx;
  int capped = 0;
  select_timed(*sup, have, budget, &tidx, &capped);
  const int ntime = (int)tidx.size();
  int64_t need = 0;
  for (int t = 0; t < ntime; ++t)
    if ((*sup)[(size_t)tidx[(size_t)t]].ws > need)
      need = (*sup)[(size_t)tidx[(size_t)t]].ws;
  if (need > 0) {
    if (!arena || nn_ws_ensure(arena, (size_t)need) != 0) {
      set_err("workspace grow failed");
      return -1;
    }
  }
  void *wsptr = (arena && arena->ptr) ? arena->ptr : nullptr;
  const int warm = (volume < 1000000) ? 1 : 2;
  const int reps = (volume < 1000000) ? 3 : 10;
  int best = -1;
  int gemm_i = -1;
  for (int t = 0; t < ntime; ++t) {
    const int i = tidx[(size_t)t];
    Cand &c = (*sup)[(size_t)i];
    if (c.ws > 0 && !wsptr)
      continue;
    double us = time_plan(h, c.plan, wsptr, uids, ptrs, n_uids, warm, reps);
    if (us < 0)
      continue;
    c.us = us;
    if (best < 0 || us < (*sup)[(size_t)best].us)
      best = i;
    if (note_tc_gemm(c.mask) &&
        (gemm_i < 0 || us < (*sup)[(size_t)gemm_i].us))
      gemm_i = i;
  }
  if (best < 0) {
    set_err("all timed engines failed execute");
    return -1;
  }
  const Cand &w = (*sup)[(size_t)best];
  if (note_fft(w.mask) && gemm_i >= 0 && gemm_i != best) {
    const Cand &g = (*sup)[(size_t)gemm_i];
    std::fprintf(stderr,
                 "nn_conv_graph FATAL FFT won (eng=%lld %.1fus notes=%s) over "
                 "TC GEMM (eng=%lld %.1fus notes=%s)\n",
                 (long long)w.engine_id, w.us, w.notes, (long long)g.engine_id,
                 g.us, g.notes);
    set_err("FFT won timed race over tensor-core implicit-GEMM");
    return -1;
  }
  if (rank) {
    std::vector<int> ok;
    for (int t = 0; t < ntime; ++t)
      if ((*sup)[(size_t)tidx[(size_t)t]].us < 1e99)
        ok.push_back(tidx[(size_t)t]);
    for (size_t a = 0; a < ok.size(); ++a)
      for (size_t b = a + 1; b < ok.size(); ++b)
        if ((*sup)[(size_t)ok[b]].us < (*sup)[(size_t)ok[a]].us)
          std::swap(ok[a], ok[b]);
    for (size_t a = 0; a < ok.size(); ++a)
      rank->push_back((*sup)[(size_t)ok[a]].engine_id);
  }
  out->plan = w.plan;
  out->ws = w.ws;
  out->engine_id = w.engine_id;
  out->time_us = w.us;
  out->note_mask = w.mask;
  std::snprintf(out->notes, sizeof(out->notes), "%s", w.notes);
  std::fprintf(stderr,
               "nn_conv_graph pick %s heur=%d supported=%d timed=%d budget=%lld"
               " capped=%d winner=eng=%lld %.1fus notes=%s ws=%lld\n",
               out->tag, heur_n, nsup, ntime, (long long)budget, capped,
               (long long)w.engine_id, w.us, w.notes, (long long)w.ws);
  return best;
}

int build_fwd_graph(cudnnHandle_t h, int n, int C, int H, int W, int K, int R,
                    int S, int pad, int stride, int dil, UniqueD *graph_out,
                    int64_t *uids, int *n_uids) {
  const int Ho = out_dim(H, pad, R, stride, dil);
  const int Wo = out_dim(W, pad, S, stride, dil);
  int64_t xdim[4] = {n, C, H, W};
  int64_t wdim[4] = {K, C, R, S};
  int64_t ydim[4] = {n, K, Ho, Wo};
  int64_t bdim[4] = {1, K, 1, 1};
  int64_t xst[4], wst[4], yst[4], bst[4];
  nhwc_stride(n, C, H, W, xst);
  krsc_stride(K, C, R, S, wst);
  nhwc_stride(n, K, Ho, Wo, yst);
  nhwc_stride(1, K, 1, 1, bst);
  UniqueD x = make_tensor(kUidX, xdim, xst, false);
  UniqueD w = make_tensor(kUidW, wdim, wst, false);
  UniqueD y = make_tensor(kUidY, ydim, yst, false);
  UniqueD b = make_tensor(kUidB, bdim, bst, false);
  UniqueD v0 = make_tensor(kUidV0, ydim, yst, true);
  UniqueD v1 = make_tensor(kUidV1, ydim, yst, true);
  UniqueD conv = make_conv(pad, stride, dil);
  UniqueD pw_add = make_pw(CUDNN_POINTWISE_ADD);
  UniqueD pw_relu = make_pw(CUDNN_POINTWISE_RELU_FWD);
  if (!x || !w || !y || !b || !v0 || !v1 || !conv || !pw_add || !pw_relu) {
    set_err("fwd tensor/desc alloc failed");
    return -1;
  }
  UniqueD conv_op =
      make_desc(CUDNN_BACKEND_OPERATION_CONVOLUTION_FORWARD_DESCRIPTOR);
  UniqueD add_op = make_desc(CUDNN_BACKEND_OPERATION_POINTWISE_DESCRIPTOR);
  UniqueD relu_op = make_desc(CUDNN_BACKEND_OPERATION_POINTWISE_DESCRIPTOR);
  if (!conv_op || !add_op || !relu_op) {
    set_err("fwd op alloc failed");
    return -1;
  }
  float alpha = 1.f, beta = 0.f, a1 = 1.f, a2 = 1.f;
  cudnnBackendDescriptor_t xd = x.get(), wd = w.get(), yd = y.get(), bd = b.get(),
                           v0d = v0.get(), v1d = v1.get(), cd = conv.get();
  cudnnBackendDescriptor_t cop = conv_op.get();
  if (cudnnBackendSetAttribute(cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_X,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &xd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_W,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &wd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_Y,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &v0d) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(
          cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_CONV_DESC,
          CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &cd) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_ALPHA,
                               CUDNN_TYPE_FLOAT, 1, &alpha) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(cop, CUDNN_ATTR_OPERATION_CONVOLUTION_FORWARD_BETA,
                               CUDNN_TYPE_FLOAT, 1, &beta) !=
          CUDNN_STATUS_SUCCESS ||
      fin(cop) != 0) {
    set_err("fwd conv op finalize failed");
    return -1;
  }
  cudnnBackendDescriptor_t padd = pw_add.get(), aop = add_op.get();
  if (cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_PW_DESCRIPTOR,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &padd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_XDESC,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &v0d) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_BDESC,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &bd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_YDESC,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &v1d) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_ALPHA1,
                               CUDNN_TYPE_FLOAT, 1, &a1) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(aop, CUDNN_ATTR_OPERATION_POINTWISE_ALPHA2,
                               CUDNN_TYPE_FLOAT, 1, &a2) !=
          CUDNN_STATUS_SUCCESS ||
      fin(aop) != 0) {
    set_err("fwd bias op finalize failed");
    return -1;
  }
  cudnnBackendDescriptor_t prelu = pw_relu.get(), rop = relu_op.get();
  if (cudnnBackendSetAttribute(rop, CUDNN_ATTR_OPERATION_POINTWISE_PW_DESCRIPTOR,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &prelu) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(rop, CUDNN_ATTR_OPERATION_POINTWISE_XDESC,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &v1d) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(rop, CUDNN_ATTR_OPERATION_POINTWISE_YDESC,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &yd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(rop, CUDNN_ATTR_OPERATION_POINTWISE_ALPHA1,
                               CUDNN_TYPE_FLOAT, 1, &a1) !=
          CUDNN_STATUS_SUCCESS ||
      fin(rop) != 0) {
    set_err("fwd relu op finalize failed");
    return -1;
  }
  UniqueD g = make_desc(CUDNN_BACKEND_OPERATIONGRAPH_DESCRIPTOR);
  if (!g) {
    set_err("fwd graph alloc failed");
    return -1;
  }
  cudnnBackendDescriptor_t ops[3] = {cop, aop, rop};
  cudnnBackendDescriptor_t gp = g.get();
  if (cudnnBackendSetAttribute(gp, CUDNN_ATTR_OPERATIONGRAPH_OPS,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 3, ops) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(gp, CUDNN_ATTR_OPERATIONGRAPH_HANDLE,
                               CUDNN_TYPE_HANDLE, 1, &h) !=
          CUDNN_STATUS_SUCCESS) {
    set_err("fwd graph set failed");
    return -1;
  }
  cudnnStatus_t st = cudnnBackendFinalize(gp);
  if (st != CUDNN_STATUS_SUCCESS) {
    set_errf("fwd graph finalize", st);
    return -1;
  }
  uids[0] = kUidX;
  uids[1] = kUidW;
  uids[2] = kUidB;
  uids[3] = kUidY;
  *n_uids = 4;
  *graph_out = std::move(g);
  return 0;
}

int build_wgrad_graph(cudnnHandle_t h, int n, int C, int H, int W, int K, int R,
                      int S, int pad, int stride, int dil, float beta,
                      UniqueD *graph_out, int64_t *uids, int *n_uids) {
  const int Ho = out_dim(H, pad, R, stride, dil);
  const int Wo = out_dim(W, pad, S, stride, dil);
  int64_t xdim[4] = {n, C, H, W};
  int64_t wdim[4] = {K, C, R, S};
  int64_t ydim[4] = {n, K, Ho, Wo};
  int64_t xst[4], wst[4], yst[4];
  nhwc_stride(n, C, H, W, xst);
  krsc_stride(K, C, R, S, wst);
  nhwc_stride(n, K, Ho, Wo, yst);
  UniqueD x = make_tensor(kUidX, xdim, xst, false);
  UniqueD dw = make_tensor(kUidDw, wdim, wst, false);
  UniqueD dy = make_tensor(kUidDy, ydim, yst, false);
  UniqueD conv = make_conv(pad, stride, dil);
  UniqueD op =
      make_desc(CUDNN_BACKEND_OPERATION_CONVOLUTION_BACKWARD_FILTER_DESCRIPTOR);
  if (!x || !dw || !dy || !conv || !op) {
    set_err("wgrad desc alloc failed");
    return -1;
  }
  float alpha = 1.f;
  cudnnBackendDescriptor_t xd = x.get(), dwd = dw.get(), dyd = dy.get(),
                           cd = conv.get(), o = op.get();
  if (cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_X,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &xd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_DW,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &dwd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_DY,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &dyd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(
          o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_CONV_DESC,
          CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &cd) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(
          o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_ALPHA, CUDNN_TYPE_FLOAT,
          1, &alpha) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(
          o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_FILTER_BETA, CUDNN_TYPE_FLOAT,
          1, &beta) != CUDNN_STATUS_SUCCESS ||
      fin(o) != 0) {
    set_err("wgrad op finalize failed");
    return -1;
  }
  UniqueD g = make_desc(CUDNN_BACKEND_OPERATIONGRAPH_DESCRIPTOR);
  if (!g)
    return -1;
  if (cudnnBackendSetAttribute(g.get(), CUDNN_ATTR_OPERATIONGRAPH_OPS,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &o) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(g.get(), CUDNN_ATTR_OPERATIONGRAPH_HANDLE,
                               CUDNN_TYPE_HANDLE, 1, &h) !=
          CUDNN_STATUS_SUCCESS) {
    set_err("wgrad graph set failed");
    return -1;
  }
  cudnnStatus_t st = cudnnBackendFinalize(g.get());
  if (st != CUDNN_STATUS_SUCCESS) {
    set_errf("wgrad graph finalize", st);
    return -1;
  }
  uids[0] = kUidX;
  uids[1] = kUidDy;
  uids[2] = kUidDw;
  *n_uids = 3;
  *graph_out = std::move(g);
  return 0;
}

int build_dgrad_graph(cudnnHandle_t h, int n, int C, int H, int W, int K, int R,
                      int S, int pad, int stride, int dil, UniqueD *graph_out,
                      int64_t *uids, int *n_uids) {
  const int Ho = out_dim(H, pad, R, stride, dil);
  const int Wo = out_dim(W, pad, S, stride, dil);
  int64_t xdim[4] = {n, C, H, W};
  int64_t wdim[4] = {K, C, R, S};
  int64_t ydim[4] = {n, K, Ho, Wo};
  int64_t xst[4], wst[4], yst[4];
  nhwc_stride(n, C, H, W, xst);
  krsc_stride(K, C, R, S, wst);
  nhwc_stride(n, K, Ho, Wo, yst);
  UniqueD dx = make_tensor(kUidDx, xdim, xst, false);
  UniqueD w = make_tensor(kUidW, wdim, wst, false);
  UniqueD dy = make_tensor(kUidDy, ydim, yst, false);
  UniqueD conv = make_conv(pad, stride, dil);
  UniqueD op =
      make_desc(CUDNN_BACKEND_OPERATION_CONVOLUTION_BACKWARD_DATA_DESCRIPTOR);
  if (!dx || !w || !dy || !conv || !op) {
    set_err("dgrad desc alloc failed");
    return -1;
  }
  float alpha = 1.f, beta = 0.f;
  cudnnBackendDescriptor_t dxd = dx.get(), wd = w.get(), dyd = dy.get(),
                           cd = conv.get(), o = op.get();
  if (cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_DX,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &dxd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_W,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &wd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_DY,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &dyd) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(
          o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_CONV_DESC,
          CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &cd) != CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_ALPHA,
                               CUDNN_TYPE_FLOAT, 1, &alpha) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(o, CUDNN_ATTR_OPERATION_CONVOLUTION_BWD_DATA_BETA,
                               CUDNN_TYPE_FLOAT, 1, &beta) !=
          CUDNN_STATUS_SUCCESS ||
      fin(o) != 0) {
    set_err("dgrad op finalize failed");
    return -1;
  }
  UniqueD g = make_desc(CUDNN_BACKEND_OPERATIONGRAPH_DESCRIPTOR);
  if (!g)
    return -1;
  if (cudnnBackendSetAttribute(g.get(), CUDNN_ATTR_OPERATIONGRAPH_OPS,
                               CUDNN_TYPE_BACKEND_DESCRIPTOR, 1, &o) !=
          CUDNN_STATUS_SUCCESS ||
      cudnnBackendSetAttribute(g.get(), CUDNN_ATTR_OPERATIONGRAPH_HANDLE,
                               CUDNN_TYPE_HANDLE, 1, &h) !=
          CUDNN_STATUS_SUCCESS) {
    set_err("dgrad graph set failed");
    return -1;
  }
  cudnnStatus_t st = cudnnBackendFinalize(g.get());
  if (st != CUDNN_STATUS_SUCCESS) {
    set_errf("dgrad graph finalize", st);
    return -1;
  }
  uids[0] = kUidW;
  uids[1] = kUidDy;
  uids[2] = kUidDx;
  *n_uids = 3;
  *graph_out = std::move(g);
  return 0;
}

int alloc_h(__half **p, size_t n) {
  if (cudaMalloc(p, n * sizeof(__half)) != cudaSuccess)
    return -1;
  cudaMemset(*p, 0, n * sizeof(__half));
  return 0;
}

void free_h(__half *p) {
  if (p)
    cudaFree(p);
}

} // namespace

struct NnConvNet {
  cudnnHandle_t dnn = nullptr;
  int device = 0;
  int max_n = 0;
  int prepared_n = 0;
  int cudnn_ver = 0;
  NnWsArena *ws = nullptr;
  /* Workspace the arena's other user (cuda_lt_gemm) already committed to.
   * The shrink never goes below it. Published by the owner of both. */
  int64_t ws_floor = 0;
  std::vector<Layer> layers;
  std::map<CacheKey, CachedPlan> cache;
};

static void free_plan(CachedPlan *p) {
  if (p->plan)
    cudnnBackendDestroyDescriptor(p->plan);
  if (p->graph)
    cudnnBackendDestroyDescriptor(p->graph);
  p->plan = nullptr;
  p->graph = nullptr;
}

NnConvNet *nn_conv_net_create(cudnnHandle_t dnn, int device, int max_n,
                              const NnConvLayerSpec *layers, int n_layers,
                              NnWsArena *ws) {
  if (!dnn || !layers || n_layers < 1 || max_n < 1 || !ws) {
    set_err("create: bad args");
    return nullptr;
  }
  if (cudaSetDevice(device) != cudaSuccess) {
    set_err("create: cudaSetDevice failed");
    return nullptr;
  }
  NnConvNet *net = new NnConvNet();
  net->dnn = dnn;
  net->device = device;
  net->max_n = max_n;
  net->ws = ws;
  net->cudnn_ver = (int)cudnnGetVersion();
  net->layers.resize((size_t)n_layers);
  for (int i = 0; i < n_layers; ++i) {
    Layer &L = net->layers[(size_t)i];
    L.spec = layers[i];
    if (L.spec.c_in < 1 || L.spec.c_out < 1 || L.spec.k < 1 ||
        L.spec.stride < 1 || L.spec.h_in < 1 || L.spec.w_in < 1) {
      set_err("create: bad layer spec");
      delete net;
      return nullptr;
    }
    L.c_in_pad = pad4(L.spec.c_in);
    L.h_out = out_dim(L.spec.h_in, L.spec.pad, L.spec.k, L.spec.stride, 1);
    L.w_out = out_dim(L.spec.w_in, L.spec.pad, L.spec.k, L.spec.stride, 1);
    if (L.h_out < 1 || L.w_out < 1) {
      set_err("create: empty spatial out");
      delete net;
      return nullptr;
    }
  }
  std::fprintf(stderr, "nn_conv_graph create n_layers=%d max_n=%d cudnn=%d\n",
               n_layers, max_n, net->cudnn_ver);
  return net;
}

void nn_conv_net_destroy(NnConvNet *net) {
  if (!net)
    return;
  for (auto &kv : net->cache)
    free_plan(&kv.second);
  delete net;
}

static CacheKey make_key(const NnConvNet *net, int op, int n, const Layer &L,
                         int fusion) {
  CacheKey k{};
  k.op = op;
  k.n = n;
  k.C = L.c_in_pad;
  k.H = L.spec.h_in;
  k.W = L.spec.w_in;
  k.K = L.spec.c_out;
  k.R = L.spec.k;
  k.S = L.spec.k;
  k.pad = L.spec.pad;
  k.stride = L.spec.stride;
  k.dil = 1;
  k.dtype = (int)CUDNN_DATA_HALF;
  k.layout = 1; /* NHWC */
  k.fusion = fusion;
  k.cudnn_ver = net->cudnn_ver;
  k.device = net->device;
  return k;
}

/* Size the shared arena to what the cached plans need, plus `also` for a plan
 * about to be cached, never under the lt floor. Shrinks as well as grows, so
 * a race workspace is handed back before the next grow. Prepare time only. */
static int arena_settle(NnConvNet *net, int64_t also, const char *why) {
  if (!net->ws)
    return 0;
  int64_t need = net->ws_floor;
  for (const auto &kv : net->cache)
    if (kv.second.ws > need)
      need = kv.second.ws;
  if (also > need)
    need = also;
  if (need < kWsFloor)
    need = kWsFloor;
  const int64_t have = (int64_t)net->ws->bytes;
  if (have == need)
    return 0;
  if (have > need) {
    cudaDeviceSynchronize(); /* old workspace may still be live */
    if (nn_ws_shrink(net->ws, (size_t)need) != 0) {
      set_err("workspace shrink failed");
      return -1;
    }
    std::fprintf(stderr, "nn_conv_graph arena shrink %lld -> %lld B (%s)\n",
                 (long long)have, (long long)net->ws->bytes, why);
    return 0;
  }
  if (nn_ws_ensure(net->ws, (size_t)need) != 0) {
    set_err("workspace grow failed");
    return -1;
  }
  return 0;
}

/* Build the op's graph at n. Also writes the log tag. */
static int build_op_graph(NnConvNet *net, int layer, OpKind op, int n,
                          float beta, UniqueD *graph, int64_t *uids,
                          int *n_uids, char *tag, size_t tagsz) {
  const Layer &L = net->layers[(size_t)layer];
  const int C = L.c_in_pad, H = L.spec.h_in, W = L.spec.w_in, K = L.spec.c_out;
  const int R = L.spec.k, S = L.spec.k, pad = L.spec.pad, stride = L.spec.stride;
  if (op == OP_FWD) {
    std::snprintf(tag, tagsz, "L%d FWD", layer);
    return build_fwd_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1, graph,
                           uids, n_uids);
  }
  if (op == OP_WGRAD) {
    std::snprintf(tag, tagsz, "L%d WGRAD b=%.0f", layer, beta);
    return build_wgrad_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1,
                             beta, graph, uids, n_uids);
  }
  std::snprintf(tag, tagsz, "L%d DGRAD", layer);
  return build_dgrad_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1, graph,
                           uids, n_uids);
}

namespace {
struct TBufs {
  __half *x = nullptr, *w = nullptr, *y = nullptr, *b = nullptr;
  __half *dy = nullptr, *dw = nullptr, *dx = nullptr;
};

void free_tbufs(TBufs *t) {
  free_h(t->x);
  free_h(t->w);
  free_h(t->y);
  free_h(t->b);
  free_h(t->dy);
  free_h(t->dw);
  free_h(t->dx);
  *t = TBufs();
}

int alloc_tbufs(TBufs *t, int n, int C, int H, int W, int K, int R, int S,
                int Ho, int Wo) {
  const size_t nx = (size_t)n * (size_t)H * (size_t)W * (size_t)C;
  const size_t nw = (size_t)K * (size_t)R * (size_t)S * (size_t)C;
  const size_t ny = (size_t)n * (size_t)Ho * (size_t)Wo * (size_t)K;
  const size_t nb = (size_t)K;
  if (alloc_h(&t->x, nx) || alloc_h(&t->w, nw) || alloc_h(&t->y, ny) ||
      alloc_h(&t->b, nb) || alloc_h(&t->dy, ny) || alloc_h(&t->dw, nw) ||
      alloc_h(&t->dx, nx)) {
    set_err("timing buffer alloc failed");
    free_tbufs(t);
    return -1;
  }
  return 0;
}

void tbuf_ptrs(const TBufs &t, OpKind op, void **ptrs) {
  for (int i = 0; i < 8; ++i)
    ptrs[i] = nullptr;
  if (op == OP_FWD) {
    ptrs[0] = t.x;
    ptrs[1] = t.w;
    ptrs[2] = t.b;
    ptrs[3] = t.y;
  } else if (op == OP_WGRAD) {
    ptrs[0] = t.x;
    ptrs[1] = t.dy;
    ptrs[2] = t.dw;
  } else {
    ptrs[0] = t.w;
    ptrs[1] = t.dy;
    ptrs[2] = t.dx;
  }
}
} // namespace

static int ensure_plan(NnConvNet *net, int layer, OpKind op, int n, float beta) {
  const Layer &L = net->layers[(size_t)layer];
  int fusion = 0;
  if (op == OP_FWD)
    fusion = 1;
  else if (op == OP_WGRAD)
    fusion = (beta != 0.f) ? 3 : 2;
  else
    fusion = 4;
  CacheKey key = make_key(net, (int)op, n, L, fusion);
  if (net->cache.find(key) != net->cache.end())
    return 1; /* hit */
  const int C = L.c_in_pad, H = L.spec.h_in, W = L.spec.w_in, K = L.spec.c_out;
  const int R = L.spec.k, S = L.spec.k;
  const int Ho = L.h_out, Wo = L.w_out;

  CachedPlan rec;
  rec.n_uids = 0;
  UniqueD graph;
  if (build_op_graph(net, layer, op, n, beta, &graph, rec.uids, &rec.n_uids,
                     rec.tag, sizeof(rec.tag)) != 0)
    return -1;
  rec.graph = graph.release();

  /* Candidates at the real n. The winner's plan must come from here whatever
   * n the race runs at. */
  std::vector<Cand> cb;
  int heur_b = 0;
  if (collect_supported(net->dnn, rec.graph, &cb, &heur_b) != 0) {
    free_plan(&rec);
    return -1;
  }

  /* Cost of racing at n: the seven timing buffers plus the arena grow that the
   * would-be timed set forces. */
  const int64_t budget = ws_budget();
  const int64_t have = net->ws ? (int64_t)net->ws->bytes : 0;
  int64_t wsmax_b = 0;
  {
    int taken = 0;
    for (size_t i = 0; i < cb.size() && taken < kTopK; ++i)
      if (cb[i].ws <= have || cb[i].ws <= budget) {
        if (cb[i].ws > wsmax_b)
          wsmax_b = cb[i].ws;
        ++taken;
      }
  }
  const int64_t tb_full = (int64_t)tbuf_bytes(n, C, H, W, K, R, S, Ho, Wo);
  int race_n = n;
  if (tb_full + wsmax_b > budget) {
    /* Walk down the bucket ladder. Workspace scales about linearly with n, so
     * scale wsmax to choose; the race at race_n re-queries the heuristics and
     * applies its own budget, so an optimistic estimate stays safe. */
    int rn = prev_bucket(n, net->max_n);
    while (rn > 0) {
      const int64_t cost =
          (int64_t)tbuf_bytes(rn, C, H, W, K, R, S, Ho, Wo) +
          (int64_t)((double)wsmax_b * (double)rn / (double)n);
      if (cost <= budget)
        break;
      rn = prev_bucket(rn, net->max_n);
    }
    if (rn > 0)
      race_n = rn;
  }

  int chosen = -1;
  if (race_n != n) {
    /* Race small, then finalize only the winner at n. */
    CachedPlan sc;
    sc.n_uids = 0;
    UniqueD rg;
    if (build_op_graph(net, layer, op, race_n, beta, &rg, sc.uids, &sc.n_uids,
                       sc.tag, sizeof(sc.tag)) == 0) {
      sc.graph = rg.release();
      std::vector<Cand> cr;
      int heur_r = 0;
      if (collect_supported(net->dnn, sc.graph, &cr, &heur_r) == 0) {
        TBufs t;
        if (alloc_tbufs(&t, race_n, C, H, W, K, R, S, Ho, Wo) == 0) {
          void *ptrs[8];
          tbuf_ptrs(t, op, ptrs);
          const int64_t vol = (int64_t)race_n * H * W * C;
          std::vector<int64_t> rank;
          const int rb =
              time_and_pick(net->dnn, net->ws, &cr, sc.uids, sc.n_uids, ptrs,
                            vol, heur_r, &sc, &rank);
          free_tbufs(&t);
          if (rb >= 0) {
            /* Rank comes from race_n, but the workspace that matters is the
             * one at n. Take the best ranked engine that also supports n AND
             * whose workspace at n we can actually hold. */
            const int64_t bnow = ws_budget();
            const int64_t hnow = net->ws ? (int64_t)net->ws->bytes : 0;
            for (size_t a = 0; a < rank.size() && chosen < 0; ++a)
              for (size_t i = 0; i < cb.size(); ++i)
                if (cb[i].engine_id == rank[a] &&
                    (cb[i].ws <= hnow || cb[i].ws <= bnow)) {
                  chosen = (int)i;
                  break;
                }
            if (chosen < 0) {
              /* No ranked engine fits at n. Fall back to cuDNN's own order and
               * take the first candidate at n that fits. */
              for (size_t i = 0; i < cb.size(); ++i)
                if (cb[i].ws <= hnow || cb[i].ws <= bnow) {
                  chosen = (int)i;
                  std::fprintf(stderr,
                               "nn_conv_graph race n=%d for bucket=%d %s no "
                               "ranked engine fits at n; heur order eng=%lld\n",
                               race_n, n, rec.tag,
                               (long long)cb[i].engine_id);
                  break;
                }
            }
          }
        }
        destroy_cands(&cr, -1);
      }
      sc.plan = nullptr; /* owned by cr, already destroyed */
      free_plan(&sc);
    }
    if (chosen >= 0) {
      const Cand &w = cb[(size_t)chosen];
      rec.plan = w.plan;
      rec.ws = w.ws;
      rec.engine_id = w.engine_id;
      rec.time_us = 0.0;
      rec.note_mask = w.mask;
      std::snprintf(rec.notes, sizeof(rec.notes), "%s", w.notes);
      std::fprintf(stderr,
                   "nn_conv_graph race n=%d for bucket=%d %s winner=eng=%lld "
                   "ws=%lld\n",
                   race_n, n, rec.tag, (long long)w.engine_id,
                   (long long)w.ws);
      /* Hand the race workspace back, then cover the plan we keep. */
      if (arena_settle(net, rec.ws, "after race") != 0) {
        destroy_cands(&cb, chosen);
        free_plan(&rec);
        return -1;
      }
    } else {
      std::fprintf(stderr,
                   "nn_conv_graph race n=%d for bucket=%d %s no engine carried "
                   "over; full race\n",
                   race_n, n, rec.tag);
    }
  }

  if (chosen < 0) {
    /* Full race at n. Identical to the old path. */
    TBufs t;
    if (alloc_tbufs(&t, n, C, H, W, K, R, S, Ho, Wo) != 0) {
      destroy_cands(&cb, -1);
      free_plan(&rec);
      return -1;
    }
    void *ptrs[8];
    tbuf_ptrs(t, op, ptrs);
    const int64_t volume = (int64_t)n * H * W * C;
    chosen = time_and_pick(net->dnn, net->ws, &cb, rec.uids, rec.n_uids, ptrs,
                           volume, heur_b, &rec, nullptr);
    free_tbufs(&t);
    if (chosen < 0) {
      destroy_cands(&cb, -1);
      free_plan(&rec);
      return -1;
    }
  }
  destroy_cands(&cb, chosen);
  net->cache.emplace(key, rec);
  return 0;
}

int nn_conv_net_prepare(NnConvNet *net, int n) {
  if (!net || n < 1 || n > net->max_n) {
    set_err("prepare: bad n");
    return -1;
  }
  if (cudaSetDevice(net->device) != cudaSuccess) {
    set_err("prepare: cudaSetDevice failed");
    return -1;
  }
  const int b = bucket_n(n, net->max_n);
  int rebuilt = 0, hits = 0;
  for (int li = 0; li < (int)net->layers.size(); ++li) {
    int r = ensure_plan(net, li, OP_FWD, b, 0.f);
    if (r < 0)
      return -1;
    rebuilt += (r == 0);
    hits += (r == 1);
    r = ensure_plan(net, li, OP_WGRAD, b, 0.f);
    if (r < 0)
      return -1;
    rebuilt += (r == 0);
    hits += (r == 1);
    r = ensure_plan(net, li, OP_WGRAD, b, 1.f);
    if (r < 0)
      return -1;
    rebuilt += (r == 0);
    hits += (r == 1);
    if (li == 0)
      continue;
    r = ensure_plan(net, li, OP_DGRAD, b, 0.f);
    if (r < 0)
      return -1;
    rebuilt += (r == 0);
    hits += (r == 1);
  }
  net->prepared_n = b;
  /* Drop the arena to what the CHOSEN plans need. The timed race grows it to
   * the max over the timed set, and a winner with ws=0 can leave GiBs parked
   * for the life of the run. Every cached plan of every bucket counts, so
   * "workspace too small in execute" stays impossible. Prepare time only, so
   * "grow only outside a PPO step" (cuda_ws.h) still holds. */
  if (arena_settle(net, 0, "after prepare") != 0)
    return -1;
  if (rebuilt > 0) {
    std::fprintf(stderr,
                 "nn_conv_graph prepare n=%d bucket=%d rebuilt=%d cache_hit=%d\n",
                 n, b, rebuilt, hits);
    for (const auto &kv : net->cache) {
      if (kv.first.n != b)
        continue;
      std::fprintf(stderr, "  %s eng=%lld %.1fus notes=%s ws=%lld\n",
                   kv.second.tag, (long long)kv.second.engine_id,
                   kv.second.time_us, kv.second.notes, (long long)kv.second.ws);
    }
  }
  return 0;
}

int nn_conv_net_has_bucket(const NnConvNet *net, int n) {
  if (!net || n < 1 || n > net->max_n)
    return 0;
  const int b = bucket_n(n, net->max_n);
  for (int li = 0; li < (int)net->layers.size(); ++li) {
    const Layer &L = net->layers[(size_t)li];
    /* Same (op, fusion) set nn_conv_net_prepare builds. */
    const OpKind ops[4] = {OP_FWD, OP_WGRAD, OP_WGRAD, OP_DGRAD};
    const int fus[4] = {1, 2, 3, 4};
    for (int j = 0; j < 4; ++j) {
      if (j == 3 && li == 0)
        continue; /* layer 0 dgrad is a no-op */
      CacheKey key = make_key(net, (int)ops[j], b, L, fus[j]);
      if (net->cache.find(key) == net->cache.end())
        return 0;
    }
  }
  return 1;
}

void nn_conv_net_set_ws_floor(NnConvNet *net, long long bytes) {
  if (!net)
    return;
  net->ws_floor = bytes > 0 ? (int64_t)bytes : 0;
}

int nn_conv_net_n_layers(const NnConvNet *net) {
  return net ? (int)net->layers.size() : 0;
}

int nn_conv_net_c_in_pad(const NnConvNet *net, int layer) {
  if (!net || layer < 0 || layer >= (int)net->layers.size())
    return 0;
  return net->layers[(size_t)layer].c_in_pad;
}

int nn_conv_net_c_out(const NnConvNet *net, int layer) {
  if (!net || layer < 0 || layer >= (int)net->layers.size())
    return 0;
  return net->layers[(size_t)layer].spec.c_out;
}

int nn_conv_net_h_out(const NnConvNet *net, int layer) {
  if (!net || layer < 0 || layer >= (int)net->layers.size())
    return 0;
  return net->layers[(size_t)layer].h_out;
}

int nn_conv_net_w_out(const NnConvNet *net, int layer) {
  if (!net || layer < 0 || layer >= (int)net->layers.size())
    return 0;
  return net->layers[(size_t)layer].w_out;
}

static const CachedPlan *find_plan(const NnConvNet *net, int layer, OpKind op,
                                   float beta) {
  const Layer &L = net->layers[(size_t)layer];
  int fusion = (op == OP_FWD) ? 1 : (op == OP_WGRAD) ? ((beta != 0.f) ? 3 : 2) : 4;
  CacheKey key = make_key(net, (int)op, net->prepared_n, L, fusion);
  auto it = net->cache.find(key);
  if (it == net->cache.end())
    return nullptr;
  return &it->second;
}

static int exec_cached(NnConvNet *net, const CachedPlan *p, void **ptrs) {
  if (!p || !p->plan) {
    set_err("missing plan; call prepare");
    return -1;
  }
  if (p->ws > 0 && (!net->ws->ptr || net->ws->bytes < (size_t)p->ws)) {
    set_err("workspace too small in execute");
    return -1;
  }
  void *wsptr = (p->ws > 0) ? net->ws->ptr : nullptr;
  if (run_plan(net->dnn, p->plan, wsptr, p->uids, ptrs, p->n_uids) != 0) {
    set_err("execute failed");
    return -1;
  }
  return 0;
}

int nn_conv_net_fwd(NnConvNet *net, int layer, const __half *x, const __half *w,
                    const __half *bias, __half *y) {
  if (!net || layer < 0 || layer >= (int)net->layers.size() || !x || !w ||
      !bias || !y) {
    set_err("fwd: bad args");
    return -1;
  }
  const CachedPlan *p = find_plan(net, layer, OP_FWD, 0.f);
  void *ptrs[4] = {(void *)x, (void *)w, (void *)bias, (void *)y};
  return exec_cached(net, p, ptrs);
}

int nn_conv_net_wgrad(NnConvNet *net, int layer, const __half *x,
                      const __half *dpre, __half *dw, float beta) {
  if (!net || layer < 0 || layer >= (int)net->layers.size() || !x || !dpre ||
      !dw) {
    set_err("wgrad: bad args");
    return -1;
  }
  const CachedPlan *p = find_plan(net, layer, OP_WGRAD, beta);
  void *ptrs[3] = {(void *)x, (void *)dpre, (void *)dw};
  return exec_cached(net, p, ptrs);
}

int nn_conv_net_dgrad(NnConvNet *net, int layer, const __half *w,
                      const __half *dpre, __half *dx) {
  if (!net || layer < 0 || layer >= (int)net->layers.size())
    return -1;
  if (layer == 0)
    return 0;
  if (!w || !dpre || !dx) {
    set_err("dgrad: bad args");
    return -1;
  }
  const CachedPlan *p = find_plan(net, layer, OP_DGRAD, 0.f);
  void *ptrs[3] = {(void *)w, (void *)dpre, (void *)dx};
  return exec_cached(net, p, ptrs);
}
