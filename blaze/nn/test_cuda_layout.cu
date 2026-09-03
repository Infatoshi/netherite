/* Layout kernels vs CPU reference. Device pointers; host copies in the test. */
#include "cuda_layout.h"
#include "model.h"

#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern int nn_layout_last_grid_x;

static int g_fails = 0;

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

static int cu_ok(cudaError_t e, const char *what) {
  if (e != cudaSuccess) {
    std::fprintf(stderr, "FAIL: %s: %s\n", what, cudaGetErrorString(e));
    g_fails++;
    return 0;
  }
  return 1;
}

template <typename T>
static T *dalloc(size_t n, const char *what) {
  T *p = nullptr;
  if (!cu_ok(cudaMalloc(&p, n * sizeof(T)), what))
    return nullptr;
  return p;
}

static float f16_round(float x) { return __half2float(__float2half(x)); }

static void cpu_obs_to_nhwc(const uint8_t *planes, float *out, int n, int n_ch,
                            int h, int w, int c_pad, int depth0, int depth1) {
  for (int ni = 0; ni < n; ++ni) {
    for (int hh = 0; hh < h; ++hh) {
      for (int ww = 0; ww < w; ++ww) {
        for (int c = 0; c < c_pad; ++c) {
          const size_t dst =
              ((((size_t)ni * (size_t)h + (size_t)hh) * (size_t)w +
                (size_t)ww) *
                   (size_t)c_pad +
               (size_t)c);
          if (c >= n_ch) {
            out[dst] = 0.f;
            continue;
          }
          const size_t src =
              (((size_t)ni * (size_t)n_ch + (size_t)c) * (size_t)h +
               (size_t)hh) *
                  (size_t)w +
              (size_t)ww;
          float x = (float)planes[src];
          if (c == depth0 || c == depth1)
            x *= (1.f / 255.f);
          out[dst] = x;
        }
      }
    }
  }
}

static void cpu_kcrs_to_krsc(const float *kcrs, float *krsc, int k, int c, int r,
                             int s, int c_pad) {
  for (int kk = 0; kk < k; ++kk) {
    for (int rr = 0; rr < r; ++rr) {
      for (int ss = 0; ss < s; ++ss) {
        for (int cc = 0; cc < c_pad; ++cc) {
          const size_t dst =
              ((((size_t)kk * (size_t)r + (size_t)rr) * (size_t)s +
                (size_t)ss) *
                   (size_t)c_pad +
               (size_t)cc);
          if (cc >= c) {
            krsc[dst] = 0.f;
            continue;
          }
          const size_t src =
              (((size_t)kk * (size_t)c + (size_t)cc) * (size_t)r +
               (size_t)rr) *
                  (size_t)s +
              (size_t)ss;
          krsc[dst] = kcrs[src];
        }
      }
    }
  }
}

static void cpu_krsc_to_kcrs(const float *krsc, float *kcrs, int k, int c, int r,
                             int s, int c_pad) {
  for (int kk = 0; kk < k; ++kk) {
    for (int cc = 0; cc < c; ++cc) {
      for (int rr = 0; rr < r; ++rr) {
        for (int ss = 0; ss < s; ++ss) {
          const size_t dst =
              (((size_t)kk * (size_t)c + (size_t)cc) * (size_t)r + (size_t)rr) *
                  (size_t)s +
              (size_t)ss;
          const size_t src =
              ((((size_t)kk * (size_t)r + (size_t)rr) * (size_t)s +
                (size_t)ss) *
                   (size_t)c_pad +
               (size_t)cc);
          kcrs[dst] = krsc[src];
        }
      }
    }
  }
}

static void cpu_fc_chw_to_hwc(const float *w_chw, float *w_hwc, int out, int in,
                              int c, int h, int w) {
  const int n_flat = c * h * w;
  const int hw = h * w;
  for (int o = 0; o < out; ++o) {
    for (int col = 0; col < in; ++col) {
      int src_col = col;
      if (col < n_flat) {
        const int cc = col % c;
        const int rem = col / c;
        const int ww = rem % w;
        const int hh = rem / w;
        src_col = cc * hw + hh * w + ww;
      }
      w_hwc[(size_t)o * (size_t)in + (size_t)col] =
          w_chw[(size_t)o * (size_t)in + (size_t)src_col];
    }
  }
}

static void cpu_fc_hwc_to_chw(const float *w_hwc, float *w_chw, int out, int in,
                              int c, int h, int w) {
  const int n_flat = c * h * w;
  const int wc = w * c;
  for (int o = 0; o < out; ++o) {
    for (int col = 0; col < in; ++col) {
      int src_col = col;
      if (col < n_flat) {
        const int ww = col % w;
        int rem = col / w;
        const int hh = rem % h;
        const int cc = rem / h;
        src_col = hh * wc + ww * c + cc;
      }
      w_chw[(size_t)o * (size_t)in + (size_t)col] =
          w_hwc[(size_t)o * (size_t)in + (size_t)src_col];
    }
  }
}

static void cpu_relu_bwd_bias(const float *dy, const float *y, float *dpre,
                              float *db, int n, int c, int h, int w) {
  for (int ch = 0; ch < c; ++ch)
    db[ch] = 0.f;
  for (int ni = 0; ni < n; ++ni) {
    for (int hh = 0; hh < h; ++hh) {
      for (int ww = 0; ww < w; ++ww) {
        for (int ch = 0; ch < c; ++ch) {
          const size_t i =
              ((((size_t)ni * (size_t)h + (size_t)hh) * (size_t)w +
                (size_t)ww) *
                   (size_t)c +
               (size_t)ch);
          const float dp = dy[i] * (y[i] > 0.f ? 1.f : 0.f);
          dpre[i] = dp;
          db[ch] += dp;
        }
      }
    }
  }
}

static int floats_eq(const float *a, const float *b, size_t n, const char *tag) {
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i] || std::isnan(a[i]) || std::isnan(b[i])) {
      std::fprintf(stderr,
                   "FAIL: %s[%zu] got %.9g want %.9g\n", tag, i, a[i], b[i]);
      g_fails++;
      return 0;
    }
  }
  return 1;
}

static int floats_near(const float *a, const float *b, size_t n, float atol,
                       const char *tag) {
  int ok = 1;
  float worst = 0.f;
  size_t wi = 0;
  for (size_t i = 0; i < n; ++i) {
    const float d = std::fabs(a[i] - b[i]);
    if (d > worst) {
      worst = d;
      wi = i;
    }
    if (d > atol || std::isnan(a[i]) || std::isnan(b[i]))
      ok = 0;
  }
  if (!ok) {
    std::fprintf(stderr,
                 "FAIL: %s max|d|=%.6g at %zu got %.9g want %.9g atol=%.3g\n",
                 tag, worst, wi, a[wi], b[wi], atol);
    g_fails++;
  }
  return ok;
}

static void test_obs_to_nhwc(void) {
  std::printf("test_obs_to_nhwc\n");
  const int n = 2;
  const int n_ch = NN_N_CH;
  const int h = NN_CAM_H;
  const int w = NN_CAM_W;
  const int c_pad = 20;
  const int depth0 = 7;
  const int depth1 = 16;
  const size_t n_in = (size_t)n * n_ch * h * w;
  const size_t n_out = (size_t)n * h * w * c_pad;

  std::vector<uint8_t> planes(n_in);
  std::vector<float> want(n_out), got(n_out);
  std::vector<__half> got_h(n_out);
  for (size_t i = 0; i < n_in; ++i)
    planes[i] = (uint8_t)((i * 17u + 11u) & 255u);

  cpu_obs_to_nhwc(planes.data(), want.data(), n, n_ch, h, w, c_pad, depth0,
                  depth1);
  for (size_t i = 0; i < n_out; ++i)
    want[i] = f16_round(want[i]);

  uint8_t *d_planes = dalloc<uint8_t>(n_in, "obs planes");
  __half *d_out = dalloc<__half>(n_out, "obs out");
  if (!d_planes || !d_out)
    return;
  if (!cu_ok(cudaMemcpy(d_planes, planes.data(), n_in, cudaMemcpyHostToDevice),
             "obs H2D"))
    return;
  expect_eq_i(nn_layout_obs_to_nhwc(d_planes, d_out, n, n_ch, h, w, c_pad,
                                    depth0, depth1),
              0, "obs kernel rc");
  if (!cu_ok(cudaMemcpy(got_h.data(), d_out, n_out * sizeof(__half),
                        cudaMemcpyDeviceToHost),
             "obs D2H"))
    return;
  for (size_t i = 0; i < n_out; ++i)
    got[i] = __half2float(got_h[i]);

  expect_true(floats_eq(got.data(), want.data(), n_out, "obs"), "obs f16 exact");

  int extra_ok = 1, depth_ok = 1, other_ok = 1;
  for (int ni = 0; ni < n; ++ni) {
    for (int hh = 0; hh < h; ++hh) {
      for (int ww = 0; ww < w; ++ww) {
        for (int c = n_ch; c < c_pad; ++c) {
          const size_t dst =
              ((((size_t)ni * h + hh) * w + ww) * c_pad + c);
          if (got[dst] != 0.f)
            extra_ok = 0;
        }
        for (int c = 0; c < n_ch; ++c) {
          const size_t dst =
              ((((size_t)ni * h + hh) * w + ww) * c_pad + c);
          const size_t src =
              (((size_t)ni * n_ch + c) * h + hh) * w + ww;
          const float raw = (float)planes[src];
          if (c == depth0 || c == depth1) {
            if (got[dst] != f16_round(raw * (1.f / 255.f)))
              depth_ok = 0;
          } else if (got[dst] != f16_round(raw)) {
            other_ok = 0;
          }
        }
      }
    }
  }
  expect_true(extra_ok, "obs extra channels 0");
  expect_true(depth_ok, "obs depth ch 7,16 scaled 1/255");
  expect_true(other_ok, "obs other channels unscaled");

  cudaFree(d_planes);
  cudaFree(d_out);
}

static void test_kcrs_krsc(void) {
  std::printf("test_kcrs_krsc\n");
  const int k = 32, c = 18, r = 5, s = 5, c_pad = 20;
  const size_t n_kcrs = (size_t)k * c * r * s;
  const size_t n_krsc = (size_t)k * r * s * c_pad;

  std::vector<float> kcrs(n_kcrs), krsc_want(n_krsc), krsc_got(n_krsc);
  std::vector<float> kcrs_rt(n_kcrs), kcrs_rt_got(n_kcrs);
  std::vector<__half> krsc_h(n_krsc);
  for (size_t i = 0; i < n_kcrs; ++i)
    kcrs[i] = (float)((int)(i % 251) - 80) * 0.125f;

  cpu_kcrs_to_krsc(kcrs.data(), krsc_want.data(), k, c, r, s, c_pad);
  for (size_t i = 0; i < n_krsc; ++i)
    krsc_want[i] = f16_round(krsc_want[i]);
  cpu_krsc_to_kcrs(krsc_want.data(), kcrs_rt.data(), k, c, r, s, c_pad);

  float *d_kcrs = dalloc<float>(n_kcrs, "kcrs");
  __half *d_krsc = dalloc<__half>(n_krsc, "krsc");
  float *d_kcrs2 = dalloc<float>(n_kcrs, "kcrs2");
  if (!d_kcrs || !d_krsc || !d_kcrs2)
    return;
  cu_ok(cudaMemcpy(d_kcrs, kcrs.data(), n_kcrs * sizeof(float),
                   cudaMemcpyHostToDevice),
        "kcrs H2D");
  expect_eq_i(nn_layout_kcrs_to_krsc(d_kcrs, d_krsc, k, c, r, s, c_pad), 0,
              "kcrs_to_krsc rc");
  expect_eq_i(nn_layout_krsc_to_kcrs(d_krsc, d_kcrs2, k, c, r, s, c_pad), 0,
              "krsc_to_kcrs rc");
  cu_ok(cudaMemcpy(krsc_h.data(), d_krsc, n_krsc * sizeof(__half),
                   cudaMemcpyDeviceToHost),
        "krsc D2H");
  for (size_t i = 0; i < n_krsc; ++i)
    krsc_got[i] = __half2float(krsc_h[i]);
  cu_ok(cudaMemcpy(kcrs_rt_got.data(), d_kcrs2, n_kcrs * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "kcrs2 D2H");

  expect_true(floats_eq(krsc_got.data(), krsc_want.data(), n_krsc, "krsc"),
              "kcrs->krsc f16 exact");
  expect_true(floats_eq(kcrs_rt_got.data(), kcrs_rt.data(), n_kcrs, "kcrs_rt"),
              "krsc->kcrs roundtrip f16");

  int pad_ok = 1;
  for (int kk = 0; kk < k; ++kk)
    for (int rr = 0; rr < r; ++rr)
      for (int ss = 0; ss < s; ++ss)
        for (int cc = c; cc < c_pad; ++cc) {
          const size_t dst =
              ((((size_t)kk * r + rr) * s + ss) * c_pad + cc);
          if (krsc_got[dst] != 0.f)
            pad_ok = 0;
        }
  expect_true(pad_ok, "krsc extra C_pad channels 0");

  cudaFree(d_kcrs);
  cudaFree(d_krsc);
  cudaFree(d_kcrs2);
}

static void test_fc_chw_hwc(void) {
  std::printf("test_fc_chw_hwc\n");
  const int out = NN_FC_OUT;
  const int in = NN_FC_IN;
  const int c = NN_C_OUT2;
  const int h = NN_H2;
  const int w = NN_W2;
  const int n_flat = c * h * w;
  expect_eq_i(n_flat, 6272, "n_flat 64*7*14");
  expect_eq_i(in, n_flat + 27, "in = 6272+27");

  const size_t n_w = (size_t)out * (size_t)in;
  std::vector<float> w_chw(n_w), w_hwc_want(n_w), w_hwc_got(n_w);
  std::vector<float> w_rt(n_w), w_rt_got(n_w);
  for (size_t i = 0; i < n_w; ++i)
    w_chw[i] = (float)((int)(i % 997) - 400);

  cpu_fc_chw_to_hwc(w_chw.data(), w_hwc_want.data(), out, in, c, h, w);
  cpu_fc_hwc_to_chw(w_hwc_want.data(), w_rt.data(), out, in, c, h, w);

  float *d_chw = dalloc<float>(n_w, "fc chw");
  float *d_hwc = dalloc<float>(n_w, "fc hwc");
  float *d_chw2 = dalloc<float>(n_w, "fc chw2");
  if (!d_chw || !d_hwc || !d_chw2)
    return;
  cu_ok(cudaMemcpy(d_chw, w_chw.data(), n_w * sizeof(float),
                   cudaMemcpyHostToDevice),
        "fc H2D");
  expect_eq_i(nn_layout_fc_chw_to_hwc(d_chw, d_hwc, out, in, c, h, w), 0,
              "fc_chw_to_hwc rc");
  expect_eq_i(nn_layout_fc_hwc_to_chw(d_hwc, d_chw2, out, in, c, h, w), 0,
              "fc_hwc_to_chw rc");
  cu_ok(cudaMemcpy(w_hwc_got.data(), d_hwc, n_w * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "fc hwc D2H");
  cu_ok(cudaMemcpy(w_rt_got.data(), d_chw2, n_w * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "fc rt D2H");

  expect_true(floats_eq(w_hwc_got.data(), w_hwc_want.data(), n_w, "fc_hwc"),
              "chw->hwc exact");
  expect_true(floats_eq(w_rt_got.data(), w_chw.data(), n_w, "fc_rt"),
              "hwc->chw roundtrip exact");
  expect_true(floats_eq(w_rt.data(), w_chw.data(), n_w, "fc_cpu_rt"),
              "cpu fc roundtrip identity");

  /* One known (c,h,w) pair and a scalar column. */
  const int ch = 2, row = 3, col = 5;
  const int chw_i = ch * (h * w) + row * w + col;
  const int hwc_i = row * (w * c) + col * c + ch;
  expect_true(w_hwc_got[(size_t)7 * in + hwc_i] ==
                  w_chw[(size_t)7 * in + chw_i],
              "fc sample CHW<->HWC");
  expect_true(w_hwc_got[(size_t)3 * in + n_flat + 4] ==
                  w_chw[(size_t)3 * in + n_flat + 4],
              "fc scalar columns copied");

  cudaFree(d_chw);
  cudaFree(d_hwc);
  cudaFree(d_chw2);
}

static void run_relu_bwd_bias(int n, int c, int h, int w, int require_grid64) {
  const size_t nelt = (size_t)n * h * w * c;
  std::vector<float> y(nelt), dy(nelt), dpre_want(nelt), dpre_got(nelt);
  std::vector<float> db_want(c), db_got(c);
  for (size_t i = 0; i < nelt; ++i) {
    const int k = (int)(i % 11);
    if (k == 0)
      y[i] = 0.f;
    else if (k == 1 || k == 2)
      y[i] = -((float)k);
    else
      y[i] = (float)k * 0.5f;
    dy[i] = (float)((int)(i % 13) - 6) * 0.25f;
  }
  cpu_relu_bwd_bias(dy.data(), y.data(), dpre_want.data(), db_want.data(), n, c,
                    h, w);

  std::vector<__half> y_h(nelt), dy_h(nelt), dpre_h(nelt);
  for (size_t i = 0; i < nelt; ++i) {
    y_h[i] = __float2half(y[i]);
    dy_h[i] = __float2half(dy[i]);
    dpre_want[i] = f16_round(dpre_want[i]);
  }

  __half *d_y = dalloc<__half>(nelt, "relu y");
  __half *d_dy = dalloc<__half>(nelt, "relu dy");
  __half *d_dpre = dalloc<__half>(nelt, "relu dpre");
  float *d_db = dalloc<float>((size_t)c, "relu db");
  if (!d_y || !d_dy || !d_dpre || !d_db)
    return;
  cu_ok(cudaMemcpy(d_y, y_h.data(), nelt * sizeof(__half), cudaMemcpyHostToDevice),
        "relu y H2D");
  cu_ok(cudaMemcpy(d_dy, dy_h.data(), nelt * sizeof(__half),
                   cudaMemcpyHostToDevice),
        "relu dy H2D");
  cu_ok(cudaMemset(d_db, 0, (size_t)c * sizeof(float)), "relu db zero");

  expect_eq_i(nn_layout_relu_bwd_bias(d_dy, d_y, d_dpre, d_db, n, c, h, w), 0,
              "relu_bwd_bias rc");
  cu_ok(cudaDeviceSynchronize(), "relu sync");

  const int gx = nn_layout_last_grid_x;
  std::printf("relu_bwd_bias gridDim.x=%d (n=%d c=%d h=%d w=%d)\n", gx, n, c, h,
              w);
  if (require_grid64)
    expect_true(gx >= 64, "relu_bwd_bias gridDim.x >= 64");

  cu_ok(cudaMemcpy(dpre_h.data(), d_dpre, nelt * sizeof(__half),
                   cudaMemcpyDeviceToHost),
        "relu dpre D2H");
  for (size_t i = 0; i < nelt; ++i)
    dpre_got[i] = __half2float(dpre_h[i]);
  cu_ok(cudaMemcpy(db_got.data(), d_db, (size_t)c * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "relu db D2H");

  expect_true(floats_eq(dpre_got.data(), dpre_want.data(), nelt, "dpre"),
              "dpre exact vs CPU");
  expect_true(floats_near(db_got.data(), db_want.data(), (size_t)c, 1e-4f, "db"),
              "db atol 1e-4 vs CPU");

  cudaFree(d_y);
  cudaFree(d_dy);
  cudaFree(d_dpre);
  cudaFree(d_db);
}

static void test_relu_bwd_bias(void) {
  std::printf("test_relu_bwd_bias\n");
  run_relu_bwd_bias(4, 32, 16, 30, 1);
  run_relu_bwd_bias(8, NN_C_OUT2, NN_H2, NN_W2, 0);
}

static void run_obs_to_nhwc_f32(int n) {
  const int n_ch = NN_N_CH;
  const int h = NN_CAM_H;
  const int w = NN_CAM_W;
  const int c_pad = 20;
  const int depth0 = 7;
  const int depth1 = 16;
  const size_t n_in = (size_t)n * n_ch * h * w;
  const size_t n_out = (size_t)n * h * w * c_pad;

  std::vector<uint8_t> planes(n_in);
  std::vector<float> want(n_out), got(n_out);
  for (size_t i = 0; i < n_in; ++i)
    planes[i] = (uint8_t)((i * 17u + 11u) & 255u);

  cpu_obs_to_nhwc(planes.data(), want.data(), n, n_ch, h, w, c_pad, depth0,
                  depth1);

  uint8_t *d_planes = dalloc<uint8_t>(n_in, "obs planes");
  float *d_out = dalloc<float>(n_out, "obs out f32");
  if (!d_planes || !d_out)
    return;
  if (!cu_ok(cudaMemcpy(d_planes, planes.data(), n_in, cudaMemcpyHostToDevice),
             "obs H2D"))
    return;
  expect_eq_i(nn_layout_obs_to_nhwc_f32(d_planes, d_out, n, n_ch, h, w, c_pad,
                                        depth0, depth1),
              0, "obs f32 kernel rc");
  if (!cu_ok(cudaMemcpy(got.data(), d_out, n_out * sizeof(float),
                        cudaMemcpyDeviceToHost),
             "obs D2H"))
    return;

  expect_true(floats_eq(got.data(), want.data(), n_out, "obs f32"),
              "obs f32 exact");
  cudaFree(d_planes);
  cudaFree(d_out);
}

static void test_obs_to_nhwc_f32(void) {
  std::printf("test_obs_to_nhwc_f32\n");
  run_obs_to_nhwc_f32(1);
  run_obs_to_nhwc_f32(4);
}

static void test_kcrs_krsc_f32(void) {
  std::printf("test_kcrs_krsc_f32\n");
  const int k = 32, c = 18, r = 5, s = 5, c_pad = 20;
  const size_t n_kcrs = (size_t)k * c * r * s;
  const size_t n_krsc = (size_t)k * r * s * c_pad;

  std::vector<float> kcrs(n_kcrs), krsc_want(n_krsc), krsc_got(n_krsc);
  std::vector<float> kcrs_rt(n_kcrs), kcrs_rt_got(n_kcrs);
  for (size_t i = 0; i < n_kcrs; ++i)
    kcrs[i] = (float)((int)(i % 251) - 80) * 0.125f;

  cpu_kcrs_to_krsc(kcrs.data(), krsc_want.data(), k, c, r, s, c_pad);
  cpu_krsc_to_kcrs(krsc_want.data(), kcrs_rt.data(), k, c, r, s, c_pad);

  float *d_kcrs = dalloc<float>(n_kcrs, "kcrs");
  float *d_krsc = dalloc<float>(n_krsc, "krsc f32");
  float *d_kcrs2 = dalloc<float>(n_kcrs, "kcrs2");
  if (!d_kcrs || !d_krsc || !d_kcrs2)
    return;
  cu_ok(cudaMemcpy(d_kcrs, kcrs.data(), n_kcrs * sizeof(float),
                   cudaMemcpyHostToDevice),
        "kcrs H2D");
  expect_eq_i(nn_layout_kcrs_to_krsc_f32(d_kcrs, d_krsc, k, c, r, s, c_pad), 0,
              "kcrs_to_krsc_f32 rc");
  expect_eq_i(nn_layout_krsc_to_kcrs_f32(d_krsc, d_kcrs2, k, c, r, s, c_pad), 0,
              "krsc_to_kcrs_f32 rc");
  cu_ok(cudaMemcpy(krsc_got.data(), d_krsc, n_krsc * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "krsc D2H");
  cu_ok(cudaMemcpy(kcrs_rt_got.data(), d_kcrs2, n_kcrs * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "kcrs2 D2H");

  expect_true(floats_eq(krsc_got.data(), krsc_want.data(), n_krsc, "krsc f32"),
              "kcrs->krsc f32 exact");
  expect_true(floats_eq(kcrs_rt_got.data(), kcrs_rt.data(), n_kcrs, "kcrs_rt f32"),
              "krsc->kcrs roundtrip f32");

  cudaFree(d_kcrs);
  cudaFree(d_krsc);
  cudaFree(d_kcrs2);
}

static void run_relu_bwd_bias_f32(int n, int c, int h, int w, int require_grid64) {
  const size_t nelt = (size_t)n * c * h * w;
  std::vector<float> y(nelt), dy(nelt), dpre_want(nelt), dpre_got(nelt);
  std::vector<float> db_want(c), db_got(c);

  for (size_t i = 0; i < nelt; ++i) {
    const float v = (float)((int)(i % 101) - 30) * 0.1f;
    y[i] = v > 0.f ? v : 0.f;
    dy[i] = (float)((int)((i * 7u) % 83) - 41) * 0.05f;
  }

  cpu_relu_bwd_bias(dy.data(), y.data(), dpre_want.data(), db_want.data(), n, c,
                    h, w);

  float *d_y = dalloc<float>(nelt, "relu y f32");
  float *d_dy = dalloc<float>(nelt, "relu dy f32");
  float *d_dpre = dalloc<float>(nelt, "relu dpre f32");
  float *d_db = dalloc<float>((size_t)c, "relu db f32");
  if (!d_y || !d_dy || !d_dpre || !d_db)
    return;
  cu_ok(cudaMemcpy(d_y, y.data(), nelt * sizeof(float), cudaMemcpyHostToDevice),
        "relu y H2D");
  cu_ok(cudaMemcpy(d_dy, dy.data(), nelt * sizeof(float),
                   cudaMemcpyHostToDevice),
        "relu dy H2D");
  cu_ok(cudaMemset(d_db, 0, (size_t)c * sizeof(float)), "relu db zero");

  expect_eq_i(nn_layout_relu_bwd_bias_f32(d_dy, d_y, d_dpre, d_db, n, c, h, w),
              0, "relu_bwd_bias_f32 rc");
  cu_ok(cudaDeviceSynchronize(), "relu sync");

  cu_ok(cudaMemcpy(dpre_got.data(), d_dpre, nelt * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "relu dpre D2H");
  cu_ok(cudaMemcpy(db_got.data(), d_db, (size_t)c * sizeof(float),
                   cudaMemcpyDeviceToHost),
        "relu db D2H");

  expect_true(floats_eq(dpre_got.data(), dpre_want.data(), nelt, "dpre f32"),
              "dpre exact vs CPU f32");
  expect_true(floats_near(db_got.data(), db_want.data(), (size_t)c, 1e-4f, "db f32"),
              "db atol 1e-4 vs CPU f32");

  cudaFree(d_y);
  cudaFree(d_dy);
  cudaFree(d_dpre);
  cudaFree(d_db);
}

static void test_relu_bwd_bias_f32(void) {
  std::printf("test_relu_bwd_bias_f32\n");
  run_relu_bwd_bias_f32(4, 32, 16, 30, 1);
  run_relu_bwd_bias_f32(8, NN_C_OUT2, NN_H2, NN_W2, 0);
}

int main(void) {
  test_obs_to_nhwc();
  test_obs_to_nhwc_f32();
  test_kcrs_krsc();
  test_kcrs_krsc_f32();
  test_fc_chw_hwc();
  test_relu_bwd_bias();
  test_relu_bwd_bias_f32();
  if (g_fails) {
    std::fprintf(stderr, "test_cuda_layout: %d FAIL\n", g_fails);
    return 1;
  }
  std::printf("test_cuda_layout: PASS\n");
  return 0;
}
