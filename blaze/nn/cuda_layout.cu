/* Device NHWC acts, KRSC filters, HWC-flat FC. Host ABI stays NCHW / KCRS. */
#include "cuda_layout.h"

#include <cuda_runtime.h>

#include <cstdio>

/* Last relu_bwd_bias launch. Test reads these. */
int nn_layout_last_grid_x;
int nn_layout_last_grid_y;
int nn_layout_last_grid_z;

static const int kThr = 256;
static const int kSms = 188; /* one wave on the Fable GPU */

static int layout_grid(size_t nelt) {
  if (nelt == 0)
    return kSms;
  size_t g = (nelt + (size_t)kThr - 1) / (size_t)kThr;
  if (g < (size_t)kSms)
    g = (size_t)kSms;
  if (g > 65535)
    g = 65535;
  return (int)g;
}

__global__ void k_obs_to_nhwc(const uint8_t *__restrict__ planes,
                              float *__restrict__ out, int n, int n_ch, int h,
                              int w, int c_pad, int depth0, int depth1) {
  const size_t total =
      (size_t)n * (size_t)h * (size_t)w * (size_t)c_pad;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const int c = (int)(i % (size_t)c_pad);
    size_t t = i / (size_t)c_pad;
    const int ww = (int)(t % (size_t)w);
    t /= (size_t)w;
    const int hh = (int)(t % (size_t)h);
    const int ni = (int)(t / (size_t)h);
    if (c >= n_ch) {
      out[i] = 0.f;
      continue;
    }
    const size_t src =
        (((size_t)ni * (size_t)n_ch + (size_t)c) * (size_t)h + (size_t)hh) *
            (size_t)w +
        (size_t)ww;
    float x = (float)planes[src];
    if (c == depth0 || c == depth1)
      x *= (1.f / 255.f);
    out[i] = x;
  }
}

__global__ void k_kcrs_to_krsc(const float *__restrict__ kcrs,
                               float *__restrict__ krsc, int k, int c, int r,
                               int s, int c_pad) {
  const size_t total =
      (size_t)k * (size_t)r * (size_t)s * (size_t)c_pad;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const int cc = (int)(i % (size_t)c_pad);
    size_t t = i / (size_t)c_pad;
    const int ss = (int)(t % (size_t)s);
    t /= (size_t)s;
    const int rr = (int)(t % (size_t)r);
    const int kk = (int)(t / (size_t)r);
    if (cc >= c) {
      krsc[i] = 0.f;
      continue;
    }
    const size_t src =
        (((size_t)kk * (size_t)c + (size_t)cc) * (size_t)r + (size_t)rr) *
            (size_t)s +
        (size_t)ss;
    krsc[i] = kcrs[src];
  }
}

__global__ void k_krsc_to_kcrs(const float *__restrict__ krsc,
                               float *__restrict__ kcrs, int k, int c, int r,
                               int s, int c_pad) {
  const size_t total = (size_t)k * (size_t)c * (size_t)r * (size_t)s;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const int ss = (int)(i % (size_t)s);
    size_t t = i / (size_t)s;
    const int rr = (int)(t % (size_t)r);
    t /= (size_t)r;
    const int cc = (int)(t % (size_t)c);
    const int kk = (int)(t / (size_t)c);
    const size_t src =
        (((size_t)kk * (size_t)r + (size_t)rr) * (size_t)s + (size_t)ss) *
            (size_t)c_pad +
        (size_t)cc;
    kcrs[i] = krsc[src];
  }
}

__global__ void k_fc_chw_to_hwc(const float *__restrict__ w_chw,
                                float *__restrict__ w_hwc, int out, int in,
                                int c, int h, int w, int n_flat) {
  const size_t total = (size_t)out * (size_t)in;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  const int hw = h * w;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const int col = (int)(i % (size_t)in);
    const int row = (int)(i / (size_t)in);
    int src_col = col;
    if (col < n_flat) {
      const int cc = col % c;
      const int rem = col / c;
      const int ww = rem % w;
      const int hh = rem / w;
      src_col = cc * hw + hh * w + ww;
    }
    w_hwc[i] = w_chw[(size_t)row * (size_t)in + (size_t)src_col];
  }
}

__global__ void k_fc_hwc_to_chw(const float *__restrict__ w_hwc,
                                float *__restrict__ w_chw, int out, int in,
                                int c, int h, int w, int n_flat) {
  const size_t total = (size_t)out * (size_t)in;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  const int wc = w * c;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const int col = (int)(i % (size_t)in);
    const int row = (int)(i / (size_t)in);
    int src_col = col;
    if (col < n_flat) {
      const int ww = col % w;
      const int rem = col / w;
      const int hh = rem % h;
      const int cc = rem / h;
      src_col = hh * wc + ww * c + cc;
    }
    w_chw[i] = w_hwc[(size_t)row * (size_t)in + (size_t)src_col];
  }
}

/* NHWC dpre = dy * (y>0). Per-block db partials in smem, then atomicAdd. */
__global__ void k_relu_bwd_bias(const float *__restrict__ dy,
                                const float *__restrict__ y,
                                float *__restrict__ dpre, float *__restrict__ db,
                                int n, int c, int h, int w) {
  extern __shared__ float sdb[];
  for (int ch = threadIdx.x; ch < c; ch += blockDim.x)
    sdb[ch] = 0.f;
  __syncthreads();

  const size_t total =
      (size_t)n * (size_t)h * (size_t)w * (size_t)c;
  const size_t stride = (size_t)blockDim.x * (size_t)gridDim.x;
  for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < total;
       i += stride) {
    const float dp = dy[i] * (y[i] > 0.f ? 1.f : 0.f);
    dpre[i] = dp;
    const int ch = (int)(i % (size_t)c);
    atomicAdd(&sdb[ch], dp);
  }
  __syncthreads();
  for (int ch = threadIdx.x; ch < c; ch += blockDim.x) {
    const float v = sdb[ch];
    if (v != 0.f)
      atomicAdd(&db[ch], v);
  }
}

int nn_layout_obs_to_nhwc(const uint8_t *planes, float *out, int n, int n_ch,
                          int h, int w, int c_pad, int depth0, int depth1) {
  if (!planes || !out || n <= 0 || n_ch <= 0 || h <= 0 || w <= 0 ||
      c_pad < n_ch)
    return -1;
  const size_t nelt = (size_t)n * (size_t)h * (size_t)w * (size_t)c_pad;
  const int grid = layout_grid(nelt);
  k_obs_to_nhwc<<<grid, kThr>>>(planes, out, n, n_ch, h, w, c_pad, depth0,
                                depth1);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

int nn_layout_kcrs_to_krsc(const float *kcrs, float *krsc, int k, int c, int r,
                           int s, int c_pad) {
  if (!kcrs || !krsc || k <= 0 || c <= 0 || r <= 0 || s <= 0 || c_pad < c)
    return -1;
  const size_t nelt = (size_t)k * (size_t)r * (size_t)s * (size_t)c_pad;
  const int grid = layout_grid(nelt);
  k_kcrs_to_krsc<<<grid, kThr>>>(kcrs, krsc, k, c, r, s, c_pad);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

int nn_layout_krsc_to_kcrs(const float *krsc, float *kcrs, int k, int c, int r,
                           int s, int c_pad) {
  if (!krsc || !kcrs || k <= 0 || c <= 0 || r <= 0 || s <= 0 || c_pad < c)
    return -1;
  const size_t nelt = (size_t)k * (size_t)c * (size_t)r * (size_t)s;
  const int grid = layout_grid(nelt);
  k_krsc_to_kcrs<<<grid, kThr>>>(krsc, kcrs, k, c, r, s, c_pad);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

int nn_layout_fc_chw_to_hwc(const float *w_chw, float *w_hwc, int out, int in,
                            int c, int h, int w) {
  if (!w_chw || !w_hwc || out <= 0 || in <= 0 || c <= 0 || h <= 0 || w <= 0)
    return -1;
  const int n_flat = c * h * w;
  if (in < n_flat)
    return -1;
  const size_t nelt = (size_t)out * (size_t)in;
  const int grid = layout_grid(nelt);
  k_fc_chw_to_hwc<<<grid, kThr>>>(w_chw, w_hwc, out, in, c, h, w, n_flat);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

int nn_layout_fc_hwc_to_chw(const float *w_hwc, float *w_chw, int out, int in,
                            int c, int h, int w) {
  if (!w_hwc || !w_chw || out <= 0 || in <= 0 || c <= 0 || h <= 0 || w <= 0)
    return -1;
  const int n_flat = c * h * w;
  if (in < n_flat)
    return -1;
  const size_t nelt = (size_t)out * (size_t)in;
  const int grid = layout_grid(nelt);
  k_fc_hwc_to_chw<<<grid, kThr>>>(w_hwc, w_chw, out, in, c, h, w, n_flat);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

int nn_layout_relu_bwd_bias(const float *dy, const float *y, float *dpre,
                            float *db, int n, int c, int h, int w) {
  if (!dy || !y || !dpre || !db || n <= 0 || c <= 0 || h <= 0 || w <= 0)
    return -1;
  const size_t nelt = (size_t)n * (size_t)h * (size_t)w * (size_t)c;
  const int grid = layout_grid(nelt);
  const size_t smem = (size_t)c * sizeof(float);
  nn_layout_last_grid_x = grid;
  nn_layout_last_grid_y = 1;
  nn_layout_last_grid_z = 1;
  k_relu_bwd_bias<<<grid, kThr, smem>>>(dy, y, dpre, db, n, c, h, w);
  return cudaGetLastError() == cudaSuccess ? 0 : -1;
}
