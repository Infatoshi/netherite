/* Lane A: fused cuDNN-graph conv. Naive CPU ref lives here only. */
#include "cuda_conv_graph.h"
#include "cuda_layout.h"
#include "cuda_ws.h"
#include "model.h"

#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <cudnn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

static int g_fails;

static void expect(int cond, const char *msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
  }
}

static uint32_t xorshift(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

static float urand(uint32_t *s) {
  return (xorshift(s) / 4294967296.f) * 2.f - 1.f;
}

/* Naive NHWC conv + bias + ReLU. w is KRSC. */
static void cpu_conv_bias_relu(const float *x, const float *w, const float *bias,
                               float *y, int n, int C, int H, int W, int K,
                               int R, int S, int pad, int stride, int Ho,
                               int Wo) {
  for (int ni = 0; ni < n; ++ni) {
    for (int oh = 0; oh < Ho; ++oh) {
      for (int ow = 0; ow < Wo; ++ow) {
        for (int oc = 0; oc < K; ++oc) {
          float acc = bias[oc];
          for (int r = 0; r < R; ++r) {
            const int ih = oh * stride + r - pad;
            if (ih < 0 || ih >= H)
              continue;
            for (int s = 0; s < S; ++s) {
              const int iw = ow * stride + s - pad;
              if (iw < 0 || iw >= W)
                continue;
              for (int ic = 0; ic < C; ++ic) {
                const float xv =
                    x[(((size_t)ni * H + ih) * W + iw) * C + ic];
                const float wv =
                    w[(((size_t)oc * R + r) * S + s) * C + ic];
                acc += xv * wv;
              }
            }
          }
          y[(((size_t)ni * Ho + oh) * Wo + ow) * K + oc] =
              acc > 0.f ? acc : 0.f;
        }
      }
    }
  }
}

static int all_finite(const float *p, size_t n, const char *tag) {
  for (size_t i = 0; i < n; ++i) {
    if (!std::isfinite(p[i])) {
      std::fprintf(stderr, "FAIL: %s[%zu] not finite (%g)\n", tag, i, p[i]);
      g_fails++;
      return 0;
    }
  }
  return 1;
}

static int any_nonzero(const float *p, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (p[i] != 0.f)
      return 1;
  return 0;
}

static float *halloc(size_t n) {
  float *p = (float *)std::calloc(n, sizeof(float));
  expect(p != nullptr, "calloc");
  return p;
}

static int dalloc_h(__half **p, size_t n) {
  if (cudaMalloc(p, n * sizeof(__half)) != cudaSuccess)
    return -1;
  cudaMemset(*p, 0, n * sizeof(__half));
  return 0;
}

static int h2d_f16(const float *h, __half *d, size_t n) {
  float *tmp = nullptr;
  if (cudaMalloc(&tmp, n * sizeof(float)) != cudaSuccess)
    return -1;
  if (cudaMemcpy(tmp, h, n * sizeof(float), cudaMemcpyHostToDevice) !=
      cudaSuccess) {
    cudaFree(tmp);
    return -1;
  }
  int rc = nn_layout_f32_to_f16(tmp, d, n);
  cudaFree(tmp);
  return rc;
}

static int d2h_f16(const __half *d, float *h, size_t n) {
  float *tmp = nullptr;
  if (cudaMalloc(&tmp, n * sizeof(float)) != cudaSuccess)
    return -1;
  if (nn_layout_f16_to_f32(d, tmp, n) != 0) {
    cudaFree(tmp);
    return -1;
  }
  int rc = cudaMemcpy(h, tmp, n * sizeof(float), cudaMemcpyDeviceToHost) ==
                   cudaSuccess
               ? 0
               : -1;
  cudaFree(tmp);
  return rc;
}

static int grep_banned(void) {
  const char *paths[] = {"cuda_conv_graph.cu",
                         "./cuda_conv_graph.cu",
                         "../nn/cuda_conv_graph.cu"};
  FILE *f = nullptr;
  for (const char *p : paths) {
    f = std::fopen(p, "r");
    if (f)
      break;
  }
  if (!f) {
    std::fprintf(stderr, "FAIL: cannot open cuda_conv_graph.cu to grep\n");
    g_fails++;
    return -1;
  }
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<char> buf((size_t)sz + 1);
  size_t rd = std::fread(buf.data(), 1, (size_t)sz, f);
  buf[rd] = 0;
  std::fclose(f);
  const char *ban[] = {"GetConvolution", "cudnnFind", "cudnnConvolutionForward",
                       "BackwardBias",   "k_add_bias", "k_relu_store"};
  int hits = 0;
  for (const char *b : ban) {
    if (std::strstr(buf.data(), b)) {
      std::fprintf(stderr, "FAIL: banned token %s in cuda_conv_graph.cu\n", b);
      g_fails++;
      hits++;
    }
  }
  if (!hits)
    std::printf("grep banned tokens: 0 matches\n");
  return hits;
}

int main(void) {
  if (cudaSetDevice(0) != cudaSuccess) {
    std::fprintf(stderr, "FAIL: cudaSetDevice\n");
    return 1;
  }
  cudnnHandle_t dnn = nullptr;
  if (cudnnCreate(&dnn) != CUDNN_STATUS_SUCCESS) {
    std::fprintf(stderr, "FAIL: cudnnCreate\n");
    return 1;
  }

  NnConvLayerSpec spec[2] = {
      {NN_N_CH, NN_C_OUT1, NN_K1, NN_S1, 0, NN_CAM_H, NN_CAM_W},
      {NN_C_OUT1, NN_C_OUT2, NN_K2, NN_S2, 0, NN_H1, NN_W1},
  };
  const int max_n = 8;
  NnWsArena ws{};
  NnConvNet *net = nn_conv_net_create(dnn, 0, max_n, spec, 2, &ws);
  expect(net != nullptr, "create");
  if (!net) {
    cudnnDestroy(dnn);
    return 1;
  }
  expect(nn_conv_net_n_layers(net) == 2, "n_layers");
  expect(nn_conv_net_c_in_pad(net, 0) == 20, "L0 c_in_pad");
  expect(nn_conv_net_c_out(net, 0) == NN_C_OUT1, "L0 c_out");
  expect(nn_conv_net_h_out(net, 0) == NN_H1, "L0 h_out");
  expect(nn_conv_net_w_out(net, 0) == NN_W1, "L0 w_out");
  expect(nn_conv_net_c_in_pad(net, 1) == NN_C_OUT1, "L1 c_in_pad");
  expect(nn_conv_net_c_out(net, 1) == NN_C_OUT2, "L1 c_out");
  expect(nn_conv_net_h_out(net, 1) == NN_H2, "L1 h_out");
  expect(nn_conv_net_w_out(net, 1) == NN_W2, "L1 w_out");

  expect(nn_conv_net_prepare(net, 1) == 0, "prepare(1)");
  clock_t t0 = std::clock();
  expect(nn_conv_net_prepare(net, 8) == 0, "prepare(8)");
  clock_t t1 = std::clock();
  expect(nn_conv_net_prepare(net, 8) == 0, "prepare(8) again");
  clock_t t2 = std::clock();
  const double ms_first8 =
      1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
  const double ms_hit = 1000.0 * (double)(t2 - t1) / (double)CLOCKS_PER_SEC;
  std::printf("prepare(8) %.3f ms, second prepare(8) %.3f ms (cache)\n",
              ms_first8, ms_hit);
  expect(ms_hit < 500.0 || ms_hit < ms_first8 + 1.0, "second prepare is cache hit");

  const int n = max_n;
  const int c0 = nn_conv_net_c_in_pad(net, 0);
  const int k0 = nn_conv_net_c_out(net, 0);
  const int h0o = nn_conv_net_h_out(net, 0);
  const int w0o = nn_conv_net_w_out(net, 0);
  const int c1 = nn_conv_net_c_in_pad(net, 1);
  const int k1 = nn_conv_net_c_out(net, 1);
  const int h1o = nn_conv_net_h_out(net, 1);
  const int w1o = nn_conv_net_w_out(net, 1);

  const size_t nx0 = (size_t)n * NN_CAM_H * NN_CAM_W * (size_t)c0;
  const size_t nw0 = (size_t)k0 * NN_K1 * NN_K1 * (size_t)c0;
  const size_t ny0 = (size_t)n * (size_t)h0o * (size_t)w0o * (size_t)k0;
  const size_t nb0 = (size_t)k0;
  const size_t nx1 = (size_t)n * (size_t)h0o * (size_t)w0o * (size_t)c1;
  const size_t nw1 = (size_t)k1 * NN_K2 * NN_K2 * (size_t)c1;
  const size_t ny1 = (size_t)n * (size_t)h1o * (size_t)w1o * (size_t)k1;
  const size_t nb1 = (size_t)k1;

  float *hx0 = halloc(nx0), *hw0 = halloc(nw0), *hb0 = halloc(nb0),
        *hy0 = halloc(ny0), *hy0c = halloc(ny0);
  float *hw1 = halloc(nw1), *hb1 = halloc(nb1), *hy1 = halloc(ny1),
        *hy1c = halloc(ny1);
  float *hdpre0 = halloc(ny0), *hdw0 = halloc(nw0);
  float *hdpre1 = halloc(ny1), *hdw1 = halloc(nw1), *hdx1 = halloc(nx1);

  uint32_t rng = 0xC0FFEEu;
  for (int ni = 0; ni < n; ++ni)
    for (int h = 0; h < NN_CAM_H; ++h)
      for (int w = 0; w < NN_CAM_W; ++w)
        for (int c = 0; c < c0; ++c) {
          const size_t i =
              (((size_t)ni * NN_CAM_H + h) * NN_CAM_W + w) * (size_t)c0 +
              (size_t)c;
          hx0[i] = (c < NN_N_CH) ? urand(&rng) * 0.5f : 0.f;
        }
  for (int oc = 0; oc < k0; ++oc)
    for (int r = 0; r < NN_K1; ++r)
      for (int s = 0; s < NN_K1; ++s)
        for (int ic = 0; ic < c0; ++ic) {
          const size_t i =
              (((size_t)oc * NN_K1 + r) * NN_K1 + s) * (size_t)c0 + (size_t)ic;
          hw0[i] = (ic < NN_N_CH) ? urand(&rng) * 0.2f : 0.f;
        }
  for (int i = 0; i < k0; ++i)
    hb0[i] = urand(&rng) * 0.1f;
  for (int oc = 0; oc < k1; ++oc)
    for (int r = 0; r < NN_K2; ++r)
      for (int s = 0; s < NN_K2; ++s)
        for (int ic = 0; ic < c1; ++ic) {
          const size_t i =
              (((size_t)oc * NN_K2 + r) * NN_K2 + s) * (size_t)c1 + (size_t)ic;
          hw1[i] = urand(&rng) * 0.2f;
        }
  for (int i = 0; i < k1; ++i)
    hb1[i] = urand(&rng) * 0.1f;
  for (size_t i = 0; i < ny0; ++i)
    hdpre0[i] = urand(&rng) * 0.1f;
  for (size_t i = 0; i < ny1; ++i)
    hdpre1[i] = urand(&rng) * 0.1f;

  __half *dx0, *dw0, *db0, *dy0, *dw1, *db1, *dy1, *ddpre0, *ddpre1, *ddw0,
      *ddw1, *ddx1;
  expect(dalloc_h(&dx0, nx0) == 0 && dalloc_h(&dw0, nw0) == 0 &&
             dalloc_h(&db0, nb0) == 0 && dalloc_h(&dy0, ny0) == 0 &&
             dalloc_h(&dw1, nw1) == 0 && dalloc_h(&db1, nb1) == 0 &&
             dalloc_h(&dy1, ny1) == 0 && dalloc_h(&ddpre0, ny0) == 0 &&
             dalloc_h(&ddpre1, ny1) == 0 && dalloc_h(&ddw0, nw0) == 0 &&
             dalloc_h(&ddw1, nw1) == 0 && dalloc_h(&ddx1, nx1) == 0,
         "device alloc");

  expect(h2d_f16(hx0, dx0, nx0) == 0, "x0 H2D");
  expect(h2d_f16(hw0, dw0, nw0) == 0, "w0 H2D");
  expect(h2d_f16(hb0, db0, nb0) == 0, "b0 H2D");
  expect(h2d_f16(hw1, dw1, nw1) == 0, "w1 H2D");
  expect(h2d_f16(hb1, db1, nb1) == 0, "b1 H2D");
  expect(h2d_f16(hdpre0, ddpre0, ny0) == 0, "dpre0 H2D");
  expect(h2d_f16(hdpre1, ddpre1, ny1) == 0, "dpre1 H2D");

  expect(nn_conv_net_fwd(net, 0, dx0, dw0, db0, dy0) == 0, "fwd L0");
  expect(d2h_f16(dy0, hy0, ny0) == 0, "y0 D2H");
  expect(all_finite(hy0, ny0, "y0"), "y0 finite");
  expect(any_nonzero(hy0, ny0), "y0 not all zero");

  expect(nn_conv_net_fwd(net, 1, dy0, dw1, db1, dy1) == 0, "fwd L1");
  expect(d2h_f16(dy1, hy1, ny1) == 0, "y1 D2H");
  expect(all_finite(hy1, ny1, "y1"), "y1 finite");
  expect(any_nonzero(hy1, ny1), "y1 not all zero");

  cpu_conv_bias_relu(hx0, hw0, hb0, hy0c, n, c0, NN_CAM_H, NN_CAM_W, k0, NN_K1,
                     NN_K1, 0, NN_S1, h0o, w0o);
  cpu_conv_bias_relu(hy0c, hw1, hb1, hy1c, n, c1, h0o, w0o, k1, NN_K2, NN_K2, 0,
                     NN_S2, h1o, w1o);

  float max0 = 0.f, max1 = 0.f;
  for (size_t i = 0; i < ny0; ++i)
    max0 = std::max(max0, std::fabs(hy0[i] - hy0c[i]));
  for (size_t i = 0; i < ny1; ++i)
    max1 = std::max(max1, std::fabs(hy1[i] - hy1c[i]));
  const float max_err = std::max(max0, max1);
  std::printf("max err L0=%.6g L1=%.6g atol=5e-2 (fp16 store)\n", max0, max1);
  expect(max0 <= 5e-2f, "L0 fp16 atol 5e-2");
  expect(max1 <= 5e-2f, "L1 fp16 atol 5e-2");

  expect(nn_conv_net_wgrad(net, 0, dx0, ddpre0, ddw0, 0.f) == 0, "wgrad L0");
  expect(nn_conv_net_wgrad(net, 1, dy0, ddpre1, ddw1, 0.f) == 0, "wgrad L1");
  expect(nn_conv_net_dgrad(net, 1, dw1, ddpre1, ddx1) == 0, "dgrad L1");
  expect(nn_conv_net_dgrad(net, 0, dw0, ddpre0, ddx1) == 0, "dgrad L0 skip");

  expect(d2h_f16(ddw0, hdw0, nw0) == 0, "dw0 D2H");
  expect(d2h_f16(ddw1, hdw1, nw1) == 0, "dw1 D2H");
  expect(d2h_f16(ddx1, hdx1, nx1) == 0, "dx1 D2H");
  expect(all_finite(hdw0, nw0, "dw0"), "dw0 finite");
  expect(all_finite(hdw1, nw1, "dw1"), "dw1 finite");
  expect(all_finite(hdx1, nx1, "dx1"), "dx1 finite");

  grep_banned();

  nn_conv_net_destroy(net);
  nn_ws_free(&ws);
  cudaFree(dx0);
  cudaFree(dw0);
  cudaFree(db0);
  cudaFree(dy0);
  cudaFree(dw1);
  cudaFree(db1);
  cudaFree(dy1);
  cudaFree(ddpre0);
  cudaFree(ddpre1);
  cudaFree(ddw0);
  cudaFree(ddw1);
  cudaFree(ddx1);
  std::free(hx0);
  std::free(hw0);
  std::free(hb0);
  std::free(hy0);
  std::free(hy0c);
  std::free(hw1);
  std::free(hb1);
  std::free(hy1);
  std::free(hy1c);
  std::free(hdpre0);
  std::free(hdw0);
  std::free(hdpre1);
  std::free(hdw1);
  std::free(hdx1);
  cudnnDestroy(dnn);

  if (g_fails) {
    std::fprintf(stderr, "test_cuda_conv_graph: %d FAIL\n", g_fails);
    return 1;
  }
  std::printf("PASS max_err=%.6g\n", max_err);
  return 0;
}
