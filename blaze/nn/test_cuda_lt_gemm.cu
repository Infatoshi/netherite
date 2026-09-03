/* cublasLt dense path: CPU vs GPU, two shapes, prepare cache. */
#include "cuda_lt_gemm.h"
#include "model.h"

#include <cublasLt.h>
#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_fails;

static void expect_true(int cond, const char *msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
  }
}

static void expect_eq_i(int a, int b, const char *msg) {
  if (a != b) {
    std::fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, a, b);
    g_fails++;
  }
}

static float frand(uint32_t *s) {
  *s = *s * 1664525u + 1013904223u;
  return ((*s >> 8) * (1.f / 16777216.f) - 0.5f) * 0.04f;
}

static size_t aux_bytes(int out, int n) {
  const int64_t ld = ((int64_t)out + 127) & ~127LL;
  return (size_t)(ld / 8) * (size_t)n;
}

static int all_finite(const float *a, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!std::isfinite(a[i]))
      return 0;
  return 1;
}

static float max_abs(const float *a, const float *b, size_t n) {
  float m = 0.f;
  for (size_t i = 0; i < n; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
      return 1e30f;
    const float d = std::fabs(a[i] - b[i]);
    if (d > m)
      m = d;
  }
  return m;
}

static void cpu_fwd(const float *W, const float *x, const float *b, float *y,
                    float *pre, int n, int out, int k, int relu) {
  for (int ni = 0; ni < n; ++ni) {
    for (int o = 0; o < out; ++o) {
      float s = b[o];
      const float *wr = W + (size_t)o * (size_t)k;
      const float *xc = x + (size_t)ni * (size_t)k;
      for (int i = 0; i < k; ++i)
        s += wr[i] * xc[i];
      if (pre)
        pre[(size_t)ni * (size_t)out + (size_t)o] = s;
      y[(size_t)ni * (size_t)out + (size_t)o] = relu ? (s > 0.f ? s : 0.f) : s;
    }
  }
}

static void cpu_dpre_db_dx(const float *W, const float *dy, const float *pre,
                           float *dpre, float *db, float *dx, int n, int out,
                           int k) {
  std::memset(db, 0, (size_t)out * sizeof(float));
  std::memset(dx, 0, (size_t)k * (size_t)n * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    for (int o = 0; o < out; ++o) {
      const float p = pre[(size_t)ni * (size_t)out + (size_t)o];
      const float g = dy[(size_t)ni * (size_t)out + (size_t)o] * (p > 0.f ? 1.f : 0.f);
      dpre[(size_t)ni * (size_t)out + (size_t)o] = g;
      db[o] += g;
      const float *wr = W + (size_t)o * (size_t)k;
      float *dxc = dx + (size_t)ni * (size_t)k;
      for (int i = 0; i < k; ++i)
        dxc[i] += wr[i] * g;
    }
  }
}

static void cpu_dw(const float *dy, const float *x, float *dW, int n, int out,
                   int k) {
  std::memset(dW, 0, (size_t)out * (size_t)k * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    for (int o = 0; o < out; ++o) {
      const float g = dy[(size_t)ni * (size_t)out + (size_t)o];
      const float *xc = x + (size_t)ni * (size_t)k;
      float *wr = dW + (size_t)o * (size_t)k;
      for (int i = 0; i < k; ++i)
        wr[i] += g * xc[i];
    }
  }
}

struct DevBuf {
  float *W{}, *x{}, *b{}, *y{}, *dy{}, *dx{}, *dW{}, *db{};
  void *aux{};
  size_t aux_n{};
};

static void dev_free(DevBuf *d) {
  cudaFree(d->W);
  cudaFree(d->x);
  cudaFree(d->b);
  cudaFree(d->y);
  cudaFree(d->dy);
  cudaFree(d->dx);
  cudaFree(d->dW);
  cudaFree(d->db);
  cudaFree(d->aux);
  std::memset(d, 0, sizeof(*d));
}

static int dev_alloc(DevBuf *d, int n, int out, int k) {
  auto mal = [](void **p, size_t bytes) -> int {
    return cudaMalloc(p, bytes) == cudaSuccess ? 0 : -1;
  };
  if (mal((void **)&d->W, (size_t)out * k * sizeof(float)) ||
      mal((void **)&d->x, (size_t)k * n * sizeof(float)) ||
      mal((void **)&d->b, (size_t)out * sizeof(float)) ||
      mal((void **)&d->y, (size_t)out * n * sizeof(float)) ||
      mal((void **)&d->dy, (size_t)out * n * sizeof(float)) ||
      mal((void **)&d->dx, (size_t)k * n * sizeof(float)) ||
      mal((void **)&d->dW, (size_t)out * k * sizeof(float)) ||
      mal((void **)&d->db, (size_t)out * sizeof(float)))
    return -1;
  d->aux_n = aux_bytes(out, n);
  if (d->aux_n < 16)
    d->aux_n = 16;
  if (cudaMalloc(&d->aux, d->aux_n) != cudaSuccess)
    return -1;
  return 0;
}

static void test_shape(NnLtGemm *g, int n, int out, int k, int do_relu,
                       const char *tag) {
  std::printf("shape %s n=%d out=%d k=%d relu=%d\n", tag, n, out, k, do_relu);
  uint32_t rng = 0xC0FFEEu ^ (uint32_t)(n * 17 + out * 13 + k);
  std::vector<float> hW((size_t)out * k), hx((size_t)k * n), hb(out);
  std::vector<float> hy((size_t)out * n), hpre((size_t)out * n);
  std::vector<float> hdy((size_t)out * n), hdpre((size_t)out * n);
  std::vector<float> hdx((size_t)k * n), hdb(out), hdW((size_t)out * k);
  std::vector<float> gy((size_t)out * n), gdx((size_t)k * n), gdb(out),
      gdW((size_t)out * k);
  for (size_t i = 0; i < hW.size(); ++i)
    hW[i] = frand(&rng);
  for (size_t i = 0; i < hx.size(); ++i)
    hx[i] = frand(&rng);
  for (int o = 0; o < out; ++o)
    hb[o] = frand(&rng) + 0.01f * (float)((o % 3) - 1);
  for (size_t i = 0; i < hdy.size(); ++i)
    hdy[i] = frand(&rng);

  cpu_fwd(hW.data(), hx.data(), hb.data(), hy.data(), hpre.data(), n, out, k,
          do_relu);
  cpu_dpre_db_dx(hW.data(), hdy.data(), hpre.data(), hdpre.data(), hdb.data(),
                 hdx.data(), n, out, k);
  cpu_dw(do_relu ? hdpre.data() : hdy.data(), hx.data(), hdW.data(), n, out, k);

  DevBuf d{};
  if (dev_alloc(&d, n, out, k) != 0) {
    expect_true(0, "cudaMalloc");
    return;
  }
  cudaMemcpy(d.W, hW.data(), hW.size() * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d.x, hx.data(), hx.size() * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d.b, hb.data(), hb.size() * sizeof(float), cudaMemcpyHostToDevice);
  cudaMemcpy(d.dy, hdy.data(), hdy.size() * sizeof(float),
             cudaMemcpyHostToDevice);
  cudaMemset(d.aux, 0xAB, d.aux_n);

  int rc;
  if (do_relu) {
    rc = nn_lt_fwd_relu_bias(g, n, out, k, d.W, d.x, d.b, d.y, d.aux);
    expect_eq_i(rc, 0, "fwd_relu_bias");
  } else {
    rc = nn_lt_fwd_bias(g, n, out, k, d.W, d.x, d.b, d.y);
    expect_eq_i(rc, 0, "fwd_bias");
  }
  cudaMemcpy(gy.data(), d.y, gy.size() * sizeof(float), cudaMemcpyDeviceToHost);
  expect_true(all_finite(gy.data(), gy.size()), "y finite");
  const float yerr = max_abs(gy.data(), hy.data(), gy.size());
  std::printf("  fwd maxabs=%.5g\n", yerr);
  expect_true(yerr < 2e-2f, "fwd vs CPU atol 2e-2");

  if (do_relu) {
    std::vector<uint8_t> haux(d.aux_n);
    cudaMemcpy(haux.data(), d.aux, d.aux_n, cudaMemcpyDeviceToHost);
    int poisoned = 1;
    for (size_t i = 0; i < haux.size(); ++i)
      if (haux[i] != 0xAB) {
        poisoned = 0;
        break;
      }
    expect_true(!poisoned, "aux written (finite bitmask)");
    rc = nn_lt_bwd_drelu_bgrad(g, n, out, k, d.W, d.dy, d.aux, d.dx, d.db, 0.f);
    expect_eq_i(rc, 0, "bwd_drelu_bgrad");
    cudaMemcpy(gdx.data(), d.dx, gdx.size() * sizeof(float),
               cudaMemcpyDeviceToHost);
    cudaMemcpy(gdb.data(), d.db, gdb.size() * sizeof(float),
               cudaMemcpyDeviceToHost);
    expect_true(all_finite(gdx.data(), gdx.size()), "dx finite");
    expect_true(all_finite(gdb.data(), gdb.size()), "db finite");
    const float dberr = max_abs(gdb.data(), hdb.data(), (size_t)out);
    const float dxerr = max_abs(gdx.data(), hdx.data(), gdx.size());
    std::printf("  drelu db maxabs=%.5g dx maxabs=%.5g\n", dberr, dxerr);
    expect_true(dberr < 2e-2f, "db vs ones-reduction of dpre atol 2e-2");

    cudaMemcpy(d.dy, hdpre.data(), hdpre.size() * sizeof(float),
               cudaMemcpyHostToDevice);
  }

  cudaMemset(d.dW, 0, (size_t)out * k * sizeof(float));
  rc = nn_lt_bwd_dw_bgrad(g, n, out, k, d.dy, d.x, d.dW, d.db, 0.f);
  expect_eq_i(rc, 0, "bwd_dw_bgrad");
  cudaMemcpy(gdW.data(), d.dW, gdW.size() * sizeof(float),
             cudaMemcpyDeviceToHost);
  cudaMemcpy(gdb.data(), d.db, gdb.size() * sizeof(float),
             cudaMemcpyDeviceToHost);
  expect_true(all_finite(gdW.data(), gdW.size()), "dW finite");
  expect_true(all_finite(gdb.data(), (size_t)out), "dw db finite");
  const float dwerr = max_abs(gdW.data(), hdW.data(), gdW.size());
  std::printf("  dw maxabs=%.5g\n", dwerr);

  rc = nn_lt_bwd_dx(g, n, out, k, d.W, d.dy, d.dx, 0.f);
  expect_eq_i(rc, 0, "bwd_dx");
  cudaMemcpy(gdx.data(), d.dx, gdx.size() * sizeof(float),
             cudaMemcpyDeviceToHost);
  expect_true(all_finite(gdx.data(), gdx.size()), "bwd_dx finite");

  dev_free(&d);
}

static void run_lt_suite(cublasLtHandle_t lt, NnPrec prec, const char *name) {
  std::printf("--- lt suite prec=%s ---\n", name);
  NnWsArena ws{};
  ws.device = 0;
  NnLtGemm *g = nn_lt_create(lt, 0, 8, &ws, prec);
  expect_true(g != nullptr, "nn_lt_create");
  if (!g)
    return;

  expect_eq_i(nn_lt_prepare(g, 8), 0, "prepare(8) first");
  expect_eq_i(nn_lt_prepare(g, 8), 0, "prepare(8) cache hit");

  test_shape(g, 8, NN_FC_OUT, NN_FLAT, 1, "hidden");
  test_shape(g, 8, NN_N_LOGITS + 1, NN_FC_OUT, 0, "heads+value");
  test_shape(g, 4, 64, 128, 1, "generic");

  nn_lt_destroy(g);
  nn_ws_free(&ws);
}

int main(void) {
  int ndev = 0;
  if (cudaGetDeviceCount(&ndev) != cudaSuccess || ndev < 1) {
    std::fprintf(stderr, "no CUDA device\n");
    return 1;
  }
  cudaSetDevice(0);

  cublasLtHandle_t lt = nullptr;
  if (cublasLtCreate(&lt) != CUBLAS_STATUS_SUCCESS) {
    std::fprintf(stderr, "cublasLtCreate failed\n");
    return 1;
  }

  run_lt_suite(lt, NN_PREC_FAST, "fast");
  run_lt_suite(lt, NN_PREC_F32, "f32");

  cublasLtDestroy(lt);

  if (g_fails) {
    std::fprintf(stderr, "test_cuda_lt_gemm: %d FAIL\n", g_fails);
    return 1;
  }
  std::printf("test_cuda_lt_gemm: PASS\n");
  return 0;
}
