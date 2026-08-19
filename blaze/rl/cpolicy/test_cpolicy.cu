/* Native acceptance test for cpolicy_fwd (cuDNN conv + cuBLAS FC/heads).
 *
 * Fixed deterministic FP32 weights and inputs, independent CPU reference for
 * logits / value / greedy acts / joint logp / entropy, and fixed-RNG
 * repeatability. Fails on any numeric limit breach. Selects CUDA device 0
 * by default (override with --device N).
 */
#include "cpolicy_fwd.h"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

enum {
  kCamH = 36,
  kCamW = 64,
  kNCh = 18,
  kNScal = 27,
  kNHead = 9,
  kCOut1 = 32,
  kCOut2 = 64,
  kK1 = 5,
  kK2 = 3,
  kS1 = 2,
  kS2 = 2,
  kH1 = 16,
  kW1 = 30,
  kH2 = 7,
  kW2 = 14,
  kFlat = 6272,
  kFcIn = 6299,
  kFcOut = 256,
  kLogits = 34,
  kN = 4,
  kMaxN = 8
};

static const int kHeadW[kNHead] = {3, 3, 3, 2, 2, 2, 7, 2, 10};
static const int kHeadOff[kNHead] = {0, 3, 6, 9, 11, 13, 15, 22, 24};

/* Numeric limits (FP32 reassociation CPU vs cuDNN/cuBLAS). */
static const float kLogitTol = 2e-3f;
static const float kValueTol = 2e-3f;
static const float kLogpTol = 2e-2f;  /* sum of 9 head terms */
static const float kEntTol = 2e-2f;

static float relu(float x) { return x > 0.f ? x : 0.f; }

/* Same mix as cpolicy_fwd.cu device u01 / gumbel0. */
static float u01_host(uint64_t seed, uint32_t a, uint32_t b, uint32_t c) {
  uint64_t x = seed ^ (uint64_t)a * 0x9E3779B97F4A7C15ULL;
  x ^= (uint64_t)b * 0xBF58476D1CE4E5B9ULL;
  x ^= (uint64_t)c * 0x94D049BB133111EBULL;
  x ^= x >> 33;
  x *= 0xFF51AFD7ED558CCDULL;
  x ^= x >> 33;
  x *= 0xC4CEB9FE1A85EC53ULL;
  x ^= x >> 33;
  const float u = ((x >> 40) + 0.5f) * (1.0f / 16777216.0f);
  if (u < 1e-7f)
    return 1e-7f;
  if (u > 1.f - 1e-7f)
    return 1.f - 1e-7f;
  return u;
}

static float gumbel0_host(float u) {
  float e = -std::log(u > 1e-20f ? u : 1e-20f);
  if (e < 1e-20f)
    e = 1e-20f;
  return -std::log(e);
}

/* Deterministic fill: small-magnitude FP32 from a fixed LCG. */
static void fill_det(std::vector<float> &v, uint32_t seed, float scale) {
  uint32_t s = seed;
  for (size_t i = 0; i < v.size(); ++i) {
    s = s * 1664525u + 1013904223u;
    /* map high bits to roughly [-scale, scale] */
    const int32_t centered = (int32_t)(s >> 8) - (1 << 23);
    v[i] = scale * ((float)centered * (1.f / (float)(1 << 23)));
  }
}

static void fill_obs(std::vector<uint8_t> &obs, uint32_t seed) {
  uint32_t s = seed;
  for (size_t i = 0; i < obs.size(); ++i) {
    s = s * 1664525u + 1013904223u;
    obs[i] = (uint8_t)(s >> 24);
  }
  /* depth planes: keep full 0..255; others already uint8 */
}

static float obs_f(const uint8_t *obs, int n, int c, int h, int w) {
  float x = (float)obs[((n * kNCh + c) * kCamH + h) * kCamW + w];
  if (c == 7 || c == 16)
    x *= (1.f / 255.f);
  return x;
}

/* Independent CPU reference forward (direct conv + GEMM, greedy/Gumbel). */
struct CpuOut {
  std::vector<float> logits; /* [N,34] */
  std::vector<float> value;  /* [N] */
  std::vector<float> logp;   /* [N] */
  std::vector<float> entropy;
  std::vector<int64_t> acts; /* [N,9] */
};

static void cpu_forward(const uint8_t *obs, const float *scal,
                        const uint8_t *burnin, const int64_t *noop, int n,
                        int mode, uint64_t rng_seed, const float *conv1_w,
                        const float *conv1_b, const float *conv2_w,
                        const float *conv2_b, const float *fc_w,
                        const float *fc_b, const float *heads_w,
                        const float *heads_b, const float *value_w,
                        const float *value_b, CpuOut *out) {
  out->logits.assign((size_t)n * kLogits, 0.f);
  out->value.assign((size_t)n, 0.f);
  out->logp.assign((size_t)n, 0.f);
  out->entropy.assign((size_t)n, 0.f);
  out->acts.assign((size_t)n * kNHead, 0);

  std::vector<float> c1((size_t)n * kCOut1 * kH1 * kW1);
  std::vector<float> c2((size_t)n * kCOut2 * kH2 * kW2);
  std::vector<float> h((size_t)n * kFcOut);

  /* conv1 */
  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < kCOut1; ++oc) {
      for (int oh = 0; oh < kH1; ++oh) {
        for (int ow = 0; ow < kW1; ++ow) {
          const int ih0 = oh * kS1;
          const int iw0 = ow * kS1;
          float acc = conv1_b[oc];
          const float *wc = conv1_w + oc * (kNCh * kK1 * kK1);
          for (int ic = 0; ic < kNCh; ++ic) {
            for (int kh = 0; kh < kK1; ++kh) {
              for (int kw = 0; kw < kK1; ++kw) {
                acc += obs_f(obs, ni, ic, ih0 + kh, iw0 + kw) *
                       wc[(ic * kK1 + kh) * kK1 + kw];
              }
            }
          }
          c1[((ni * kCOut1 + oc) * kH1 + oh) * kW1 + ow] = relu(acc);
        }
      }
    }
  }

  /* conv2 */
  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < kCOut2; ++oc) {
      for (int oh = 0; oh < kH2; ++oh) {
        for (int ow = 0; ow < kW2; ++ow) {
          const int ih0 = oh * kS2;
          const int iw0 = ow * kS2;
          float acc = conv2_b[oc];
          const float *wc = conv2_w + oc * (kCOut1 * kK2 * kK2);
          for (int ic = 0; ic < kCOut1; ++ic) {
            const float *in =
                &c1[((ni * kCOut1 + ic) * kH1) * kW1];
            for (int kh = 0; kh < kK2; ++kh) {
              for (int kw = 0; kw < kK2; ++kw) {
                acc += in[(ih0 + kh) * kW1 + (iw0 + kw)] *
                       wc[(ic * kK2 + kh) * kK2 + kw];
              }
            }
          }
          c2[((ni * kCOut2 + oc) * kH2 + oh) * kW2 + ow] = relu(acc);
        }
      }
    }
  }

  /* fc + heads + value */
  for (int ni = 0; ni < n; ++ni) {
    float xin[kFcIn];
    for (int j = 0; j < kFlat; ++j) {
      const int c = j / (kH2 * kW2);
      const int rem = j % (kH2 * kW2);
      const int hh = rem / kW2;
      const int ww = rem % kW2;
      xin[j] = c2[((ni * kCOut2 + c) * kH2 + hh) * kW2 + ww];
    }
    for (int j = 0; j < kNScal; ++j)
      xin[kFlat + j] = scal[ni * kNScal + j];

    for (int o = 0; o < kFcOut; ++o) {
      float acc = fc_b[o];
      const float *wr = fc_w + o * kFcIn;
      for (int j = 0; j < kFcIn; ++j)
        acc += wr[j] * xin[j];
      h[ni * kFcOut + o] = relu(acc);
    }

    for (int o = 0; o < kLogits; ++o) {
      float acc = heads_b[o];
      const float *wr = heads_w + o * kFcOut;
      for (int j = 0; j < kFcOut; ++j)
        acc += wr[j] * h[ni * kFcOut + j];
      out->logits[ni * kLogits + o] = acc;
    }

    float vacc = value_b[0];
    for (int j = 0; j < kFcOut; ++j)
      vacc += value_w[j] * h[ni * kFcOut + j];
    out->value[ni] = vacc;

    /* sample / greedy + logp + entropy */
    const int bi = burnin ? (int)burnin[ni] : 0;
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    for (int hd = 0; hd < kNHead; ++hd) {
      const int w = kHeadW[hd];
      const int off = kHeadOff[hd];
      const float *row = &out->logits[ni * kLogits + off];
      float m = row[0];
      for (int c = 1; c < w; ++c)
        if (row[c] > m)
          m = row[c];
      float sum = 0.f;
      float ex[10];
      for (int c = 0; c < w; ++c) {
        ex[c] = std::exp(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      const float lse = m + std::log(sum);
      int a;
      if (bi) {
        a = (int)noop[hd];
      } else if (mode == 1) {
        a = 0;
        float best = row[0];
        for (int c = 1; c < w; ++c) {
          if (row[c] > best) {
            best = row[c];
            a = c;
          }
        }
      } else {
        a = 0;
        float best =
            row[0] + gumbel0_host(u01_host(rng_seed, (uint32_t)ni, (uint32_t)hd,
                                           0u));
        for (int c = 1; c < w; ++c) {
          const float s =
              row[c] + gumbel0_host(u01_host(rng_seed, (uint32_t)ni,
                                             (uint32_t)hd, (uint32_t)c));
          if (s > best) {
            best = s;
            a = c;
          }
        }
      }
      out->acts[ni * kNHead + hd] = (int64_t)a;
      lp_sum += row[a] - lse;
      float eh = 0.f;
      for (int c = 0; c < w; ++c) {
        const float p = ex[c] * inv;
        if (p > 0.f)
          eh -= p * std::log(p);
      }
      ent_sum += eh;
    }
    out->logp[ni] = lp_sum;
    out->entropy[ni] = ent_sum;
  }
}

static float max_abs_err(const float *a, const float *b, int n) {
  float m = 0.f;
  for (int i = 0; i < n; ++i) {
    const float d = std::fabs(a[i] - b[i]);
    if (d > m)
      m = d;
  }
  return m;
}

static int report(const char *name, float err, float lim, int *fails) {
  const int ok = err <= lim;
  std::printf("%s  %s  max_err=%.6e  limit=%.6e\n", ok ? "PASS" : "FAIL", name,
              (double)err, (double)lim);
  if (!ok)
    (*fails)++;
  return ok;
}

static void usage(const char *argv0) {
  std::fprintf(stderr, "usage: %s [--device N]\n", argv0);
}

int main(int argc, char **argv) {
  int device = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      device = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-h") == 0 ||
               std::strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  int ndev = 0;
  if (cudaGetDeviceCount(&ndev) != cudaSuccess || ndev <= 0) {
    std::fprintf(stderr, "FAIL  no CUDA devices\n");
    return 1;
  }
  if (device < 0 || device >= ndev) {
    std::fprintf(stderr, "FAIL  device %d out of range [0,%d)\n", device, ndev);
    return 1;
  }
  if (cudaSetDevice(device) != cudaSuccess) {
    std::fprintf(stderr, "FAIL  cudaSetDevice(%d)\n", device);
    return 1;
  }
  std::printf("device=%d max_n=%d batch=%d\n", device, kMaxN, kN);

  /* Fixed deterministic weights and inputs. */
  std::vector<float> conv1_w((size_t)kCOut1 * kNCh * kK1 * kK1);
  std::vector<float> conv1_b(kCOut1);
  std::vector<float> conv2_w((size_t)kCOut2 * kCOut1 * kK2 * kK2);
  std::vector<float> conv2_b(kCOut2);
  std::vector<float> fc_w((size_t)kFcOut * kFcIn);
  std::vector<float> fc_b(kFcOut);
  std::vector<float> heads_w((size_t)kLogits * kFcOut);
  std::vector<float> heads_b(kLogits);
  std::vector<float> value_w(kFcOut);
  std::vector<float> value_b(1);
  fill_det(conv1_w, 1u, 0.02f);
  fill_det(conv1_b, 2u, 0.01f);
  fill_det(conv2_w, 3u, 0.02f);
  fill_det(conv2_b, 4u, 0.01f);
  fill_det(fc_w, 5u, 0.01f);
  fill_det(fc_b, 6u, 0.01f);
  fill_det(heads_w, 7u, 0.02f);
  fill_det(heads_b, 8u, 0.01f);
  fill_det(value_w, 9u, 0.02f);
  fill_det(value_b, 10u, 0.01f);

  std::vector<uint8_t> obs((size_t)kN * kNCh * kCamH * kCamW);
  std::vector<float> scal((size_t)kN * kNScal);
  std::vector<uint8_t> burnin(kN, 0);
  std::vector<int64_t> noop(kNHead, 0);
  fill_obs(obs, 11u);
  fill_det(scal, 12u, 0.5f);
  burnin[1] = 1;
  noop[0] = 1;
  noop[1] = 1;
  noop[2] = 0;

  const uint64_t rng_seed = 0xC0FFEEULL;

  CpuOut ref_greedy, ref_gumbel;
  cpu_forward(obs.data(), scal.data(), burnin.data(), noop.data(), kN,
              /*mode=*/1, rng_seed, conv1_w.data(), conv1_b.data(),
              conv2_w.data(), conv2_b.data(), fc_w.data(), fc_b.data(),
              heads_w.data(), heads_b.data(), value_w.data(), value_b.data(),
              &ref_greedy);
  cpu_forward(obs.data(), scal.data(), burnin.data(), noop.data(), kN,
              /*mode=*/0, rng_seed, conv1_w.data(), conv1_b.data(),
              conv2_w.data(), conv2_b.data(), fc_w.data(), fc_b.data(),
              heads_w.data(), heads_b.data(), value_w.data(), value_b.data(),
              &ref_gumbel);

  CPolicy *h = cpolicy_create(device, kMaxN);
  if (!h) {
    std::fprintf(stderr, "FAIL  cpolicy_create: %s\n", cpolicy_last_error());
    return 1;
  }
  if (cpolicy_upload_weights(h, conv1_w.data(), conv1_b.data(), conv2_w.data(),
                             conv2_b.data(), fc_w.data(), fc_b.data(),
                             heads_w.data(), heads_b.data(), value_w.data(),
                             value_b.data(), /*on_device=*/0) != 0) {
    std::fprintf(stderr, "FAIL  upload: %s\n", cpolicy_last_error());
    cpolicy_destroy(h);
    return 1;
  }

  uint8_t *d_obs = nullptr;
  float *d_scal = nullptr;
  uint8_t *d_burnin = nullptr;
  int64_t *d_noop = nullptr;
  int64_t *d_acts = nullptr;
  float *d_logp = nullptr;
  float *d_value = nullptr;
  float *d_entropy = nullptr;
  float *d_logits = nullptr;
  cudaStream_t stream;
  if (cudaStreamCreate(&stream) != cudaSuccess) {
    std::fprintf(stderr, "FAIL  cudaStreamCreate\n");
    cpolicy_destroy(h);
    return 1;
  }

#define CHECK_CUDA(call)                                                       \
  do {                                                                         \
    if ((call) != cudaSuccess) {                                               \
      std::fprintf(stderr, "FAIL  cuda %s\n", #call);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)

  CHECK_CUDA(cudaMalloc(&d_obs, obs.size()));
  CHECK_CUDA(cudaMalloc(&d_scal, scal.size() * sizeof(float)));
  CHECK_CUDA(cudaMalloc(&d_burnin, burnin.size()));
  CHECK_CUDA(cudaMalloc(&d_noop, noop.size() * sizeof(int64_t)));
  CHECK_CUDA(cudaMalloc(&d_acts, (size_t)kN * kNHead * sizeof(int64_t)));
  CHECK_CUDA(cudaMalloc(&d_logp, (size_t)kN * sizeof(float)));
  CHECK_CUDA(cudaMalloc(&d_value, (size_t)kN * sizeof(float)));
  CHECK_CUDA(cudaMalloc(&d_entropy, (size_t)kN * sizeof(float)));
  CHECK_CUDA(cudaMalloc(&d_logits, (size_t)kN * kLogits * sizeof(float)));
  CHECK_CUDA(cudaMemcpy(d_obs, obs.data(), obs.size(), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(d_scal, scal.data(), scal.size() * sizeof(float),
                        cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(d_burnin, burnin.data(), burnin.size(),
                        cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(d_noop, noop.data(), noop.size() * sizeof(int64_t),
                        cudaMemcpyHostToDevice));

  /* Warmup call (allowed to touch cuDNN plan caches). */
  if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 1,
                             rng_seed, stream, d_acts, d_logp, d_value,
                             d_entropy, d_logits) != 0) {
    std::fprintf(stderr, "FAIL  warmup forward: %s\n", cpolicy_last_error());
    return 1;
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));

  size_t free_before = 0;
  size_t total_before = 0;
  CHECK_CUDA(cudaMemGetInfo(&free_before, &total_before));
  for (int repeat = 0; repeat < 8; ++repeat) {
    if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 1,
                               rng_seed, stream, d_acts, d_logp, d_value,
                               d_entropy, d_logits) != 0) {
      std::fprintf(stderr, "FAIL  steady forward: %s\n",
                   cpolicy_last_error());
      return 1;
    }
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));
  size_t free_after = 0;
  size_t total_after = 0;
  CHECK_CUDA(cudaMemGetInfo(&free_after, &total_after));
  if (total_before != total_after || free_after < free_before) {
    std::fprintf(stderr,
                 "FAIL  steady forward allocated device memory: "
                 "free_before=%zu free_after=%zu\n",
                 free_before, free_after);
    return 1;
  }
  std::printf("PASS  steady_forward_device_alloc_bytes=0\n");

  /* Greedy pass after warmup. */
  if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 1,
                             rng_seed, stream, d_acts, d_logp, d_value,
                             d_entropy, d_logits) != 0) {
    std::fprintf(stderr, "FAIL  greedy forward: %s\n", cpolicy_last_error());
    return 1;
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));

  std::vector<float> g_logits((size_t)kN * kLogits);
  std::vector<float> g_value(kN), g_logp(kN), g_ent(kN);
  std::vector<int64_t> g_acts((size_t)kN * kNHead);
  CHECK_CUDA(cudaMemcpy(g_logits.data(), d_logits, g_logits.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(g_value.data(), d_value, g_value.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(g_logp.data(), d_logp, g_logp.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(g_ent.data(), d_entropy, g_ent.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(g_acts.data(), d_acts, g_acts.size() * sizeof(int64_t),
                        cudaMemcpyDeviceToHost));

  int fails = 0;
  report("logits",
         max_abs_err(g_logits.data(), ref_greedy.logits.data(), kN * kLogits),
         kLogitTol, &fails);
  report("value", max_abs_err(g_value.data(), ref_greedy.value.data(), kN),
         kValueTol, &fails);
  report("logp_greedy",
         max_abs_err(g_logp.data(), ref_greedy.logp.data(), kN), kLogpTol,
         &fails);
  report("entropy_greedy",
         max_abs_err(g_ent.data(), ref_greedy.entropy.data(), kN), kEntTol,
         &fails);

  int act_mismatch = 0;
  for (int i = 0; i < kN * kNHead; ++i) {
    if (g_acts[i] != ref_greedy.acts[i])
      act_mismatch++;
  }
  {
    const float err = (float)act_mismatch;
    const float lim = 0.f;
    report("greedy_acts", err, lim, &fails);
  }

  /* Gumbel mode: match CPU reference actions/logp/entropy for fixed seed. */
  if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 0,
                             rng_seed, stream, d_acts, d_logp, d_value,
                             d_entropy, d_logits) != 0) {
    std::fprintf(stderr, "FAIL  gumbel forward: %s\n", cpolicy_last_error());
    return 1;
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));
  std::vector<int64_t> s_acts((size_t)kN * kNHead);
  std::vector<float> s_logp(kN), s_ent(kN);
  CHECK_CUDA(cudaMemcpy(s_acts.data(), d_acts, s_acts.size() * sizeof(int64_t),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(s_logp.data(), d_logp, s_logp.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(s_ent.data(), d_entropy, s_ent.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));

  int gumbel_mismatch = 0;
  for (int i = 0; i < kN * kNHead; ++i) {
    if (s_acts[i] != ref_gumbel.acts[i])
      gumbel_mismatch++;
  }
  report("gumbel_acts_vs_ref", (float)gumbel_mismatch, 0.f, &fails);
  report("logp_gumbel",
         max_abs_err(s_logp.data(), ref_gumbel.logp.data(), kN), kLogpTol,
         &fails);
  report("entropy_gumbel",
         max_abs_err(s_ent.data(), ref_gumbel.entropy.data(), kN), kEntTol,
         &fails);

  /* Fixed RNG path repeatability: same seed -> identical acts/logp. */
  if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 0,
                             rng_seed, stream, d_acts, d_logp, d_value,
                             d_entropy, nullptr) != 0) {
    std::fprintf(stderr, "FAIL  gumbel repeat: %s\n", cpolicy_last_error());
    return 1;
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));
  std::vector<int64_t> s2_acts((size_t)kN * kNHead);
  std::vector<float> s2_logp(kN);
  CHECK_CUDA(cudaMemcpy(s2_acts.data(), d_acts, s2_acts.size() * sizeof(int64_t),
                        cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy(s2_logp.data(), d_logp, s2_logp.size() * sizeof(float),
                        cudaMemcpyDeviceToHost));
  int rep_act = 0;
  for (int i = 0; i < kN * kNHead; ++i) {
    if (s2_acts[i] != s_acts[i])
      rep_act++;
  }
  report("rng_repeat_acts", (float)rep_act, 0.f, &fails);
  report("rng_repeat_logp", max_abs_err(s2_logp.data(), s_logp.data(), kN), 0.f,
         &fails);

  /* Different seed must be allowed to differ (sanity, not a hard fail if equal). */
  if (cpolicy_forward_sample(h, d_obs, d_scal, d_burnin, d_noop, kN, 0,
                             rng_seed ^ 0xA5A5A5A5ULL, stream, d_acts, d_logp,
                             d_value, d_entropy, nullptr) != 0) {
    std::fprintf(stderr, "FAIL  alt seed: %s\n", cpolicy_last_error());
    return 1;
  }
  CHECK_CUDA(cudaStreamSynchronize(stream));
  std::vector<int64_t> s3_acts((size_t)kN * kNHead);
  CHECK_CUDA(cudaMemcpy(s3_acts.data(), d_acts, s3_acts.size() * sizeof(int64_t),
                        cudaMemcpyDeviceToHost));
  int seed_diff = 0;
  for (int i = 0; i < kN * kNHead; ++i) {
    if (s3_acts[i] != s_acts[i])
      seed_diff++;
  }
  std::printf("INFO  alt_seed_act_diffs=%d (expected >0)\n", seed_diff);

  cudaFree(d_obs);
  cudaFree(d_scal);
  cudaFree(d_burnin);
  cudaFree(d_noop);
  cudaFree(d_acts);
  cudaFree(d_logp);
  cudaFree(d_value);
  cudaFree(d_entropy);
  cudaFree(d_logits);
  cudaStreamDestroy(stream);
  cpolicy_destroy(h);

  if (fails) {
    std::printf("RESULT  FAIL  %d check(s) breached\n", fails);
    return 1;
  }
  std::printf("RESULT  PASS\n");
  return 0;
}
