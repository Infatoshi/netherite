/* Shape-agnostic cuDNN-graph conv. NHWC / KRSC, fp32 store, TF32 compute.
 * Fable: heur A/B, check_support, time top K=8. See cuda_fable_contract.h. */
#include "cuda_conv_graph.h"
#include "cuda_fable_contract.h"

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
  cudnnDataType_t dt = CUDNN_DATA_FLOAT;
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

bool note_tf32_gemm(uint32_t m) {
  const bool tc = (m & (1u << CUDNN_NUMERICAL_NOTE_TENSOR_CORE)) != 0;
  const bool dn = (m & (1u << CUDNN_NUMERICAL_NOTE_DOWN_CONVERT_INPUTS)) != 0;
  return (tc || dn) && !note_fft(m) && !note_wino(m);
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

int pick_timed(cudnnHandle_t h, NnWsArena *arena, cudnnBackendDescriptor_t graph,
               const int64_t *uids, int n_uids, void **ptrs, int64_t volume,
               CachedPlan *out) {
  std::vector<cudnnBackendDescriptor_t> raw;
  if (collect_heur(graph, CUDNN_HEUR_MODE_A, &raw) != 0 ||
      collect_heur(graph, CUDNN_HEUR_MODE_B, &raw) != 0) {
    set_err("heur query failed");
    for (auto c : raw)
      cudnnBackendDestroyDescriptor(c);
    return -1;
  }
  const int nAplusB = (int)raw.size();
  if (raw.empty())
    collect_heur(graph, CUDNN_HEUR_MODE_FALLBACK, &raw);
  std::vector<int64_t> seen;
  std::vector<Cand> supported;
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
    supported.push_back(c);
  }
  if (supported.empty()) {
    set_err("no engine passed check_support");
    return -1;
  }
  const int nsup = (int)supported.size();
  int ntime = std::min(kTopK, nsup);
  int64_t need = 0;
  for (int i = 0; i < ntime; ++i)
    if (supported[(size_t)i].ws > need)
      need = supported[(size_t)i].ws;
  if (need > 0) {
    if (!arena || nn_ws_ensure(arena, (size_t)need) != 0) {
      set_err("workspace grow failed");
      for (auto &c : supported) {
        if (c.plan)
          cudnnBackendDestroyDescriptor(c.plan);
        if (c.cfg)
          cudnnBackendDestroyDescriptor(c.cfg);
      }
      return -1;
    }
  }
  void *wsptr = (arena && arena->ptr) ? arena->ptr : nullptr;
  const int warm = (volume < 1000000) ? 1 : 2;
  const int reps = (volume < 1000000) ? 3 : 10;
  int best = -1;
  int gemm_i = -1;
  for (int i = 0; i < ntime; ++i) {
    Cand &c = supported[(size_t)i];
    if (c.ws > 0 && !wsptr)
      continue;
    double us = time_plan(h, c.plan, wsptr, uids, ptrs, n_uids, warm, reps);
    if (us < 0)
      continue;
    c.us = us;
    if (best < 0 || us < supported[(size_t)best].us)
      best = i;
    if (note_tf32_gemm(c.mask) &&
        (gemm_i < 0 || us < supported[(size_t)gemm_i].us))
      gemm_i = i;
  }
  if (best < 0) {
    set_err("all timed engines failed execute");
    for (auto &c : supported) {
      if (c.plan)
        cudnnBackendDestroyDescriptor(c.plan);
      if (c.cfg)
        cudnnBackendDestroyDescriptor(c.cfg);
    }
    return -1;
  }
  const Cand &w = supported[(size_t)best];
  if (note_fft(w.mask) && gemm_i >= 0 && gemm_i != best) {
    const Cand &g = supported[(size_t)gemm_i];
    std::fprintf(stderr,
                 "nn_conv_graph FATAL FFT won (eng=%lld %.1fus notes=%s) over "
                 "TF32 GEMM (eng=%lld %.1fus notes=%s)\n",
                 (long long)w.engine_id, w.us, w.notes, (long long)g.engine_id,
                 g.us, g.notes);
    set_err("FFT won timed race over TF32 implicit-GEMM");
    for (auto &c : supported) {
      if (c.plan)
        cudnnBackendDestroyDescriptor(c.plan);
      if (c.cfg)
        cudnnBackendDestroyDescriptor(c.cfg);
    }
    return -1;
  }
  out->plan = w.plan;
  out->ws = w.ws;
  out->engine_id = w.engine_id;
  out->time_us = w.us;
  out->note_mask = w.mask;
  std::snprintf(out->notes, sizeof(out->notes), "%s", w.notes);
  std::fprintf(stderr,
               "nn_conv_graph pick %s heur=%d supported=%d timed=%d winner="
               "eng=%lld %.1fus notes=%s ws=%lld\n",
               out->tag, nAplusB, nsup, ntime, (long long)w.engine_id, w.us,
               w.notes, (long long)w.ws);
  for (int i = 0; i < nsup; ++i) {
    if (i == best)
      continue;
    if (supported[(size_t)i].plan)
      cudnnBackendDestroyDescriptor(supported[(size_t)i].plan);
    if (supported[(size_t)i].cfg)
      cudnnBackendDestroyDescriptor(supported[(size_t)i].cfg);
  }
  if (w.cfg)
    cudnnBackendDestroyDescriptor(w.cfg);
  return 0;
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

int alloc_f(float **p, size_t n) {
  if (cudaMalloc(p, n * sizeof(float)) != cudaSuccess)
    return -1;
  cudaMemset(*p, 0, n * sizeof(float));
  return 0;
}

void free_f(float *p) {
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
  k.dtype = (int)CUDNN_DATA_FLOAT;
  k.layout = 1; /* NHWC */
  k.fusion = fusion;
  k.cudnn_ver = net->cudnn_ver;
  k.device = net->device;
  return k;
}

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
  UniqueD graph;
  CachedPlan rec;
  rec.n_uids = 0;
  const int C = L.c_in_pad, H = L.spec.h_in, W = L.spec.w_in, K = L.spec.c_out;
  const int R = L.spec.k, S = L.spec.k, pad = L.spec.pad, stride = L.spec.stride;
  const int Ho = L.h_out, Wo = L.w_out;
  if (op == OP_FWD) {
    std::snprintf(rec.tag, sizeof(rec.tag), "L%d FWD", layer);
    if (build_fwd_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1, &graph,
                        rec.uids, &rec.n_uids) != 0)
      return -1;
  } else if (op == OP_WGRAD) {
    std::snprintf(rec.tag, sizeof(rec.tag), "L%d WGRAD b=%.0f", layer, beta);
    if (build_wgrad_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1, beta,
                          &graph, rec.uids, &rec.n_uids) != 0)
      return -1;
  } else {
    std::snprintf(rec.tag, sizeof(rec.tag), "L%d DGRAD", layer);
    if (build_dgrad_graph(net->dnn, n, C, H, W, K, R, S, pad, stride, 1, &graph,
                          rec.uids, &rec.n_uids) != 0)
      return -1;
  }
  rec.graph = graph.release();
  float *x = nullptr, *w = nullptr, *y = nullptr, *b = nullptr, *dy = nullptr,
        *dw = nullptr, *dx = nullptr;
  const size_t nx = (size_t)n * (size_t)H * (size_t)W * (size_t)C;
  const size_t nw = (size_t)K * (size_t)R * (size_t)S * (size_t)C;
  const size_t ny = (size_t)n * (size_t)Ho * (size_t)Wo * (size_t)K;
  const size_t nb = (size_t)K;
  if (alloc_f(&x, nx) || alloc_f(&w, nw) || alloc_f(&y, ny) || alloc_f(&b, nb) ||
      alloc_f(&dy, ny) || alloc_f(&dw, nw) || alloc_f(&dx, nx)) {
    set_err("timing buffer alloc failed");
    free_f(x);
    free_f(w);
    free_f(y);
    free_f(b);
    free_f(dy);
    free_f(dw);
    free_f(dx);
    return -1;
  }
  void *ptrs[8] = {};
  if (op == OP_FWD) {
    ptrs[0] = x;
    ptrs[1] = w;
    ptrs[2] = b;
    ptrs[3] = y;
  } else if (op == OP_WGRAD) {
    ptrs[0] = x;
    ptrs[1] = dy;
    ptrs[2] = dw;
  } else {
    ptrs[0] = w;
    ptrs[1] = dy;
    ptrs[2] = dx;
  }
  const int64_t volume = (int64_t)n * H * W * C;
  const int rc =
      pick_timed(net->dnn, net->ws, rec.graph, rec.uids, rec.n_uids, ptrs,
                 volume, &rec);
  free_f(x);
  free_f(w);
  free_f(y);
  free_f(b);
  free_f(dy);
  free_f(dw);
  free_f(dx);
  if (rc != 0) {
    free_plan(&rec);
    return -1;
  }
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

int nn_conv_net_fwd(NnConvNet *net, int layer, const float *x, const float *w,
                    const float *bias, float *y) {
  if (!net || layer < 0 || layer >= (int)net->layers.size() || !x || !w ||
      !bias || !y) {
    set_err("fwd: bad args");
    return -1;
  }
  const CachedPlan *p = find_plan(net, layer, OP_FWD, 0.f);
  void *ptrs[4] = {(void *)x, (void *)w, (void *)bias, (void *)y};
  return exec_cached(net, p, ptrs);
}

int nn_conv_net_wgrad(NnConvNet *net, int layer, const float *x,
                      const float *dpre, float *dw, float beta) {
  if (!net || layer < 0 || layer >= (int)net->layers.size() || !x || !dpre ||
      !dw) {
    set_err("wgrad: bad args");
    return -1;
  }
  const CachedPlan *p = find_plan(net, layer, OP_WGRAD, beta);
  void *ptrs[3] = {(void *)x, (void *)dpre, (void *)dw};
  return exec_cached(net, p, ptrs);
}

int nn_conv_net_dgrad(NnConvNet *net, int layer, const float *w,
                      const float *dpre, float *dx) {
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
