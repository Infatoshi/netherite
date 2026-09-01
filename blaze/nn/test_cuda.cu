/* CPU vs CUDA parity tests for the Blaze policy backend. */
#include "fixture.h"
#include "model.h"
#include "nn.h"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static int g_fails = 0;
static float g_max_fwd_err = 0.f;
static float g_max_w_err = 0.f;

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

static void expect_near(float a, float b, float tol, const char *msg) {
  float d = std::fabs(a - b);
  if (d > tol || std::isnan(a) || std::isnan(b)) {
    std::fprintf(stderr, "FAIL: %s (got %.8g want %.8g tol %.3g diff %.3g)\n",
                 msg, a, b, tol, d);
    g_fails++;
  }
}

static void note_fwd_err(float a, float b) {
  float d = std::fabs(a - b);
  if (d > g_max_fwd_err)
    g_max_fwd_err = d;
}

static void note_w_err(float a, float b) {
  float d = std::fabs(a - b);
  if (d > g_max_w_err)
    g_max_w_err = d;
}

static void check_grad_norm_rel(const char *label, float cpu_gn, float gpu_gn,
                                float rel_lim) {
  const float diff = std::fabs(cpu_gn - gpu_gn);
  const float denom = std::fmax(1.f, std::fmax(std::fabs(cpu_gn),
                                               std::fabs(gpu_gn)));
  const float rel = diff / denom;
  std::printf("  %s: cpu=%.6g gpu=%.6g rel=%.6g limit=%.6g\n", label,
              cpu_gn, gpu_gn, rel, rel_lim);
  expect_true(cpu_gn > 0.f && gpu_gn > 0.f, "grad_norm must be nonzero");
  expect_true(rel <= rel_lim, "grad_norm relative error");
}

static void tmp_path(char *buf, size_t n, const char *tag) {
  std::snprintf(buf, n, "/tmp/blaze_nn_cuda_%s_%d.bin", tag, (int)getpid());
}

static void fill_inputs(uint8_t *planes, float *scalars, int n, uint64_t seed) {
  for (int ni = 0; ni < n; ++ni) {
    for (int c = 0; c < NN_N_CH; ++c) {
      for (int h = 0; h < NN_CAM_H; ++h) {
        for (int w = 0; w < NN_CAM_W; ++w) {
          float u = nn_hash_u01(seed, (uint32_t)ni, (uint32_t)c,
                                (uint32_t)(h * NN_CAM_W + w));
          planes[(((size_t)ni * NN_N_CH + c) * NN_CAM_H + h) * NN_CAM_W + w] =
              (uint8_t)(u * 255.f);
        }
      }
    }
    for (int s = 0; s < NN_N_SCAL; ++s) {
      float u = nn_hash_u01(seed + 1, (uint32_t)ni, (uint32_t)s, 0u);
      scalars[(size_t)ni * NN_N_SCAL + s] = (u - 0.5f) * 2.f;
    }
  }
}

static Nn *create_backend(NnBackend backend, int max_n, int device,
                          const NnConfig *cfg) {
  NnCreate desc;
  desc.backend = backend;
  desc.device = device;
  desc.max_n = max_n;
  desc.config = cfg ? *cfg : nn_config_default();
  return nn_create(&desc);
}

static Nn *create_cpu(int max_n, const NnConfig *cfg) {
  return create_backend(NN_BACKEND_CPU, max_n, 0, cfg);
}

static Nn *create_cuda(int max_n, int device, const NnConfig *cfg) {
  return create_backend(NN_BACKEND_CUDA, max_n, device, cfg);
}

static size_t gpu_free_bytes(int device) {
  cudaSetDevice(device);
  size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess)
    return 0;
  return free_b;
}

/* ---- tests ---- */

static void test_invalid_config_and_device(void) {
  std::printf("test_invalid_config_and_device\n");
  NnConfig bad = nn_config_default();
  bad.lr = -1.f;
  expect_true(create_cuda(1, 0, &bad) == nullptr, "reject lr < 0");

  bad = nn_config_default();
  bad.ppo_clip = 1.f;
  expect_true(create_cuda(1, 0, &bad) == nullptr, "reject ppo_clip >= 1");

  bad = nn_config_default();
  bad.grad_limit = 0.f;
  expect_true(create_cuda(1, 0, &bad) == nullptr, "reject grad_limit");

  expect_true(create_cuda(1, 9999, nullptr) == nullptr, "reject bad device");
  expect_true(create_cuda(1, -1, nullptr) == nullptr, "reject device -1");
  expect_true(create_cuda(0, 0, nullptr) == nullptr, "reject max_n 0");

  NnConfig ok = nn_config_default();
  Nn *nn = create_cuda(1, 0, &ok);
  expect_true(nn != nullptr, "good create");
  if (!nn)
    return;
  bad = ok;
  bad.entropy_coef = -0.1f;
  expect_true(nn_set_config(nn, &bad) != 0, "set_config reject");
  expect_eq_i(nn_set_config(nn, &ok), 0, "set_config accept");
  nn_destroy(nn);
}

static void test_invalid_batch_and_action(void) {
  std::printf("test_invalid_batch_and_action\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  Nn *nn = create_cuda(2, 0, &cfg);
  expect_true(nn != nullptr, "create");
  if (!nn)
    return;

  uint8_t planes[2 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[2 * NN_N_SCAL];
  float logits[2 * NN_N_LOGITS];
  float values[2];
  fill_inputs(planes, scalars, 2, 3);

  expect_true(nn_forward(nn, planes, scalars, 0, logits, values) != 0,
              "reject n=0");
  expect_true(nn_forward(nn, planes, scalars, 3, logits, values) != 0,
              "reject n>max");
  expect_eq_i(nn_forward(nn, planes, scalars, 1, logits, values), 0,
              "fwd n=1");

  int32_t acts[NN_N_HEAD];
  float logp[1];
  nn_sample(nn, logits, 1, NN_SAMPLE_GREEDY, acts, logp, nullptr);
  acts[0] = 3; /* head0 width 3 */
  float old_logp[1] = {logp[0]};
  float adv[1] = {0.5f};
  float ret[1] = {values[0]};
  int rc =
      nn_update(nn, planes, scalars, acts, old_logp, adv, ret, 1, nullptr);
  expect_true(rc != 0, "update rejects invalid action");
  expect_true(nn_last_error() != nullptr && nn_last_error()[0] != 0,
              "error set");
  expect_true(std::strstr(nn_last_error(), "action") != nullptr,
              "error mentions action");

  nn_destroy(nn);
}

static void test_forward_sample_update(void) {
  std::printf("test_forward_sample_update\n");
  const int max_n = 8;
  const int device = 0;
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  cfg.rng_seed = 42;

  Nn *cpu = create_cpu(max_n, &cfg);
  Nn *gpu = create_cuda(max_n, device, &cfg);
  expect_true(cpu && gpu, "create cpu+gpu");
  if (!cpu || !gpu)
    return;

  /* Shared deterministic checkpoint */
  char path[256];
  tmp_path(path, sizeof(path), "shared");
  expect_eq_i(nn_save(cpu, path), 0, "save shared");
  expect_eq_i(nn_load(cpu, path), 0, "reload cpu");
  expect_eq_i(nn_load(gpu, path), 0, "load gpu");

  const int batches[] = {1, 4, max_n};
  for (int bi = 0; bi < 3; ++bi) {
    const int n = batches[bi];
    std::printf("  forward n=%d\n", n);
    uint8_t *planes =
        (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
    float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
    float *lc = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
    float *lg = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
    float *vc = (float *)malloc((size_t)n * sizeof(float));
    float *vg = (float *)malloc((size_t)n * sizeof(float));
    fill_inputs(planes, scalars, n, 100u + (uint64_t)n);

    expect_eq_i(nn_forward(cpu, planes, scalars, n, lc, vc), 0, "cpu fwd");
    expect_eq_i(nn_forward(gpu, planes, scalars, n, lg, vg), 0, "gpu fwd");

    for (int i = 0; i < n * NN_N_LOGITS; ++i) {
      note_fwd_err(lc[i], lg[i]);
      expect_near(lg[i], lc[i], 5e-2f, "logit");
    }
    for (int i = 0; i < n; ++i) {
      note_fwd_err(vc[i], vg[i]);
      expect_near(vg[i], vc[i], 5e-2f, "value");
    }

    free(planes);
    free(scalars);
    free(lc);
    free(lg);
    free(vc);
    free(vg);
  }

  /* Sample compare on identical logits (CPU forward) */
  {
    const int n = 4;
    std::printf("  sample n=%d\n", n);
    uint8_t *planes =
        (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
    float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
    float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
    float *values = (float *)malloc((size_t)n * sizeof(float));
    fill_inputs(planes, scalars, n, 77);
    nn_forward(cpu, planes, scalars, n, logits, values);

    int32_t *ac = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
    int32_t *ag = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
    float *lpc = (float *)malloc((size_t)n * sizeof(float));
    float *lpg = (float *)malloc((size_t)n * sizeof(float));
    float *ec = (float *)malloc((size_t)n * sizeof(float));
    float *eg = (float *)malloc((size_t)n * sizeof(float));

    expect_eq_i(nn_sample(cpu, logits, n, NN_SAMPLE_GREEDY, ac, lpc, ec), 0,
                "cpu greedy");
    expect_eq_i(nn_sample(gpu, logits, n, NN_SAMPLE_GREEDY, ag, lpg, eg),
                0, "gpu greedy");
    expect_true(std::memcmp(ac, ag, (size_t)n * NN_N_HEAD * sizeof(int32_t)) ==
                    0,
                "greedy acts match");
    for (int i = 0; i < n; ++i) {
      expect_near(lpg[i], lpc[i], 1e-5f, "greedy logp");
      expect_near(eg[i], ec[i], 1e-5f, "greedy ent");
    }

    expect_eq_i(nn_sample(cpu, logits, n, NN_SAMPLE_GUMBEL, ac, lpc, ec), 0,
                "cpu gumbel");
    expect_eq_i(nn_sample(gpu, logits, n, NN_SAMPLE_GUMBEL, ag, lpg, eg),
                0, "gpu gumbel");
    expect_true(std::memcmp(ac, ag, (size_t)n * NN_N_HEAD * sizeof(int32_t)) ==
                    0,
                "gumbel acts match");
    for (int i = 0; i < n; ++i) {
      expect_near(lpg[i], lpc[i], 1e-5f, "gumbel logp");
      expect_near(eg[i], ec[i], 1e-5f, "gumbel ent");
    }

    free(planes);
    free(scalars);
    free(logits);
    free(values);
    free(ac);
    free(ag);
    free(lpc);
    free(lpg);
    free(ec);
    free(eg);
  }

  /* One PPO+Adam update from identical weights + zero Adam */
  {
    const int n = 4;
    std::printf("  update n=%d\n", n);
    /* reload shared weights and zero adam on both */
    expect_eq_i(nn_load(cpu, path), 0, "reload cpu pre-update");
    expect_eq_i(nn_load(gpu, path), 0, "reload gpu pre-update");

    uint8_t *planes =
        (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
    float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
    float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
    float *values = (float *)malloc((size_t)n * sizeof(float));
    fill_inputs(planes, scalars, n, 123);
    nn_forward(cpu, planes, scalars, n, logits, values);

    int32_t *acts = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
    float *logp = (float *)malloc((size_t)n * sizeof(float));
    nn_sample(cpu, logits, n, NN_SAMPLE_GREEDY, acts, logp, nullptr);

    float *old_logp = (float *)malloc((size_t)n * sizeof(float));
    float *adv = (float *)malloc((size_t)n * sizeof(float));
    float *ret = (float *)malloc((size_t)n * sizeof(float));
    for (int i = 0; i < n; ++i) {
      old_logp[i] = logp[i];
      adv[i] = (float)(i + 1) * 0.1f;
      ret[i] = values[i] + 0.5f;
    }

    NnUpdateStats sc, sg;
    expect_eq_i(
        nn_update(cpu, planes, scalars, acts, old_logp, adv, ret, n, &sc), 0,
        "cpu update");
    expect_eq_i(nn_update(gpu, planes, scalars, acts, old_logp, adv, ret, n,
                          &sg),
                0, "gpu update");
    std::printf("  cpu stats: policy=%.9g value=%.9g entropy=%.9g total=%.9g "
                "grad_norm=%.9g kl=%.9g clip=%.9g\n",
                sc.policy_loss, sc.value_loss, sc.entropy_mean, sc.total_loss,
                sc.grad_norm, sc.approx_kl, sc.clipfrac);
    std::printf("  gpu stats: policy=%.9g value=%.9g entropy=%.9g total=%.9g "
                "grad_norm=%.9g kl=%.9g clip=%.9g\n",
                sg.policy_loss, sg.value_loss, sg.entropy_mean, sg.total_loss,
                sg.grad_norm, sg.approx_kl, sg.clipfrac);
    expect_near(sg.policy_loss, sc.policy_loss, 2e-2f, "policy loss");
    expect_near(sg.value_loss, sc.value_loss, 2e-2f, "value loss");
    expect_near(sg.entropy_mean, sc.entropy_mean, 2e-2f, "entropy mean");
    expect_near(sg.total_loss, sc.total_loss, 2e-2f, "total loss");
    expect_near(sg.approx_kl, sc.approx_kl, 2e-2f, "approx_kl");
    expect_near(sg.clipfrac, sc.clipfrac, 2e-2f, "clipfrac");
    check_grad_norm_rel("full grad_norm", sc.grad_norm, sg.grad_norm, 0.05f);

    char path_c[256], path_g[256];
    tmp_path(path_c, sizeof(path_c), "cpu_upd");
    tmp_path(path_g, sizeof(path_g), "gpu_upd");
    expect_eq_i(nn_save(cpu, path_c), 0, "save cpu upd");
    expect_eq_i(nn_save(gpu, path_g), 0, "save gpu upd");

    /* Decode both and compare every param */
    size_t np = nn_fixture_param_count();
    float *pc = (float *)malloc(np * sizeof(float));
    float *pg = (float *)malloc(np * sizeof(float));
    float *tc[NN_T_COUNT];
    float *tg[NN_T_COUNT];
    size_t off = 0;
    for (int t = 0; t < NN_T_COUNT; ++t) {
      tc[t] = pc + off;
      tg[t] = pg + off;
      off += NN_TENSOR_FLOATS[t];
    }
    expect_eq_i(nn_fixture_load(path_c, tc), 0, "load cpu ckpt");
    expect_eq_i(nn_fixture_load(path_g, tg), 0, "load gpu ckpt");
    float *p0 = (float *)malloc(np * sizeof(float));
    float *t0[NN_T_COUNT];
    off = 0;
    for (int t = 0; t < NN_T_COUNT; ++t) {
      t0[t] = p0 + off;
      off += NN_TENSOR_FLOATS[t];
    }
    expect_eq_i(nn_fixture_load(path, t0), 0, "load initial ckpt");
    size_t max_i = 0;
    int sign_mismatch = 0;
    for (size_t i = 0; i < np; ++i) {
      note_w_err(pc[i], pg[i]);
      if (std::fabs(pc[i] - pg[i]) > std::fabs(pc[max_i] - pg[max_i]))
        max_i = i;
      const float dc = pc[i] - p0[i];
      const float dg = pg[i] - p0[i];
      if (dc * dg < 0.f)
        sign_mismatch++;
      expect_near(pg[i], pc[i], 5e-2f, "updated weight");
    }
    size_t base = 0;
    int max_t = 0;
    for (int t = 0; t < NN_T_COUNT; ++t) {
      if (max_i < base + NN_TENSOR_FLOATS[t]) {
        max_t = t;
        break;
      }
      base += NN_TENSOR_FLOATS[t];
    }
    std::printf("  max weight error: tensor=%s local=%zu global=%zu init=%.9g cpu=%.9g gpu=%.9g cpu_delta=%.9g gpu_delta=%.9g\n",
                NN_TENSOR_NAMES[max_t], max_i - base, max_i, p0[max_i], pc[max_i],
                pg[max_i], pc[max_i] - p0[max_i], pg[max_i] - p0[max_i]);
    std::printf("  opposite update signs: %d / %zu\n", sign_mismatch, np);

    free(planes);
    free(scalars);
    free(logits);
    free(values);
    free(acts);
    free(logp);
    free(old_logp);
    free(adv);
    free(ret);
    free(pc);
    free(pg);
    free(p0);
    unlink(path_c);
    unlink(path_g);
  }

  /* Policy-only PPO at ratio=1 proves the equality branch carries gradient. */
  {
    const int n = 4;
    std::printf("  policy-only update n=%d\n", n);
    NnConfig po = cfg;
    po.value_coef = 0.f;
    po.entropy_coef = 0.f;
    expect_eq_i(nn_set_config(cpu, &po), 0, "set cpu policy-only config");
    expect_eq_i(nn_set_config(gpu, &po), 0, "set gpu policy-only config");
    expect_eq_i(nn_load(cpu, path), 0, "reload cpu policy-only");
    expect_eq_i(nn_load(gpu, path), 0, "reload gpu policy-only");

    uint8_t *planes =
        (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
    float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
    float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
    float *values = (float *)malloc((size_t)n * sizeof(float));
    int32_t *acts = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
    float *old_logp = (float *)malloc((size_t)n * sizeof(float));
    float *adv = (float *)malloc((size_t)n * sizeof(float));
    float *ret = (float *)malloc((size_t)n * sizeof(float));
    fill_inputs(planes, scalars, n, 123);
    expect_eq_i(nn_forward(cpu, planes, scalars, n, logits, values), 0,
                "cpu policy-only fwd");
    expect_eq_i(nn_sample(cpu, logits, n, NN_SAMPLE_GUMBEL, acts, old_logp,
                          nullptr),
                0, "cpu policy-only sample");
    for (int i = 0; i < n; ++i) {
      adv[i] = (float)(i + 1) * 0.1f;
      ret[i] = values[i] + 0.5f;
    }

    NnUpdateStats sc, sg;
    expect_eq_i(nn_update(cpu, planes, scalars, acts, old_logp, adv, ret, n,
                          &sc),
                0, "cpu policy-only update");
    expect_eq_i(nn_update(gpu, planes, scalars, acts, old_logp, adv, ret, n,
                          &sg),
                0, "gpu policy-only update");
    std::printf("  cpu policy-only: policy=%.9g value=%.9g entropy=%.9g "
                "total=%.9g grad_norm=%.9g kl=%.9g clip=%.9g\n",
                sc.policy_loss, sc.value_loss, sc.entropy_mean, sc.total_loss,
                sc.grad_norm, sc.approx_kl, sc.clipfrac);
    std::printf("  gpu policy-only: policy=%.9g value=%.9g entropy=%.9g "
                "total=%.9g grad_norm=%.9g kl=%.9g clip=%.9g\n",
                sg.policy_loss, sg.value_loss, sg.entropy_mean, sg.total_loss,
                sg.grad_norm, sg.approx_kl, sg.clipfrac);
    expect_near(sg.policy_loss, sc.policy_loss, 2e-2f,
                "policy-only policy loss");
    expect_near(sg.value_loss, sc.value_loss, 2e-2f,
                "policy-only value loss");
    expect_near(sg.total_loss, sc.total_loss, 2e-2f,
                "policy-only total loss");
    expect_near(sg.approx_kl, sc.approx_kl, 2e-2f, "policy-only approx_kl");
    expect_near(sg.clipfrac, sc.clipfrac, 2e-2f, "policy-only clipfrac");
    check_grad_norm_rel("policy-only grad_norm", sc.grad_norm, sg.grad_norm,
                        0.05f);

    free(planes);
    free(scalars);
    free(logits);
    free(values);
    free(acts);
    free(old_logp);
    free(adv);
    free(ret);
    expect_eq_i(nn_set_config(cpu, &cfg), 0, "restore cpu config");
    expect_eq_i(nn_set_config(gpu, &cfg), 0, "restore gpu config");
  }

  /* save/load round trip on GPU */
  {
    std::printf("  save/load roundtrip\n");
    char p2[256];
    tmp_path(p2, sizeof(p2), "rt");
    expect_eq_i(nn_save(gpu, p2), 0, "save rt");
    Nn *g2 = create_cuda(max_n, device, &cfg);
    expect_true(g2 != nullptr, "create g2");
    if (g2) {
      expect_eq_i(nn_load(g2, p2), 0, "load rt");

      uint8_t planes[NN_N_CH * NN_CAM_H * NN_CAM_W];
      float scalars[NN_N_SCAL];
      float l1[NN_N_LOGITS], l2[NN_N_LOGITS], v1[1], v2[1];
      fill_inputs(planes, scalars, 1, 5);
      nn_forward(gpu, planes, scalars, 1, l1, v1);
      nn_forward(g2, planes, scalars, 1, l2, v2);
      for (int i = 0; i < NN_N_LOGITS; ++i)
        expect_near(l1[i], l2[i], 0.f, "rt logit");
      expect_near(v1[0], v2[0], 0.f, "rt value");
      nn_destroy(g2);
    }
    unlink(p2);
  }

  unlink(path);
  nn_destroy(cpu);
  nn_destroy(gpu);
}

static void test_no_gpu_mem_growth(void) {
  std::printf("test_no_gpu_mem_growth\n");
  const int device = 0;
  const int max_n = 4;
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-4f;
  Nn *nn = create_cuda(max_n, device, &cfg);
  expect_true(nn != nullptr, "create");
  if (!nn)
    return;

  const int n = 4;
  uint8_t *planes =
      (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
  float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
  float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *values = (float *)malloc((size_t)n * sizeof(float));
  int32_t *acts = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
  float *logp = (float *)malloc((size_t)n * sizeof(float));
  float *old_logp = (float *)malloc((size_t)n * sizeof(float));
  float *adv = (float *)malloc((size_t)n * sizeof(float));
  float *ret = (float *)malloc((size_t)n * sizeof(float));
  fill_inputs(planes, scalars, n, 9);

  /* Warmup */
  for (int i = 0; i < 2; ++i) {
    nn_forward(nn, planes, scalars, n, logits, values);
    nn_sample(nn, logits, n, NN_SAMPLE_GREEDY, acts, logp, nullptr);
    for (int j = 0; j < n; ++j) {
      old_logp[j] = logp[j];
      adv[j] = 0.1f;
      ret[j] = values[j];
    }
    nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, nullptr);
  }
  cudaDeviceSynchronize();
  size_t free0 = gpu_free_bytes(device);

  for (int i = 0; i < 8; ++i) {
    nn_forward(nn, planes, scalars, n, logits, values);
    nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts, logp, nullptr);
    for (int j = 0; j < n; ++j) {
      old_logp[j] = logp[j];
      adv[j] = 0.1f;
      ret[j] = values[j];
    }
    nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, nullptr);
  }
  cudaDeviceSynchronize();
  size_t free1 = gpu_free_bytes(device);

  std::printf("  free GPU bytes before=%zu after=%zu\n", free0, free1);
  expect_true(free1 >= free0, "zero free-memory growth (after >= before)");
  if (free1 < free0)
    std::fprintf(stderr, "  leaked %zu bytes\n", free0 - free1);

  free(planes);
  free(scalars);
  free(logits);
  free(values);
  free(acts);
  free(logp);
  free(old_logp);
  free(adv);
  free(ret);
  nn_destroy(nn);
}

int main(void) {
  test_invalid_config_and_device();
  test_invalid_batch_and_action();
  test_forward_sample_update();
  test_no_gpu_mem_growth();

  std::printf("\nmax forward abs error:   %.6g (limit 5e-2, fp16 store)\n",
              g_max_fwd_err);
  std::printf("max updated weight error: %.6g (limit 5e-2)\n", g_max_w_err);

  if (g_max_fwd_err > 5e-2f) {
    std::fprintf(stderr, "FAIL: forward max_abs exceeds 5e-2\n");
    g_fails++;
  }
  if (g_max_w_err > 5e-2f) {
    std::fprintf(stderr, "FAIL: weight max_abs exceeds 5e-2\n");
    g_fails++;
  }

  if (g_fails) {
    std::fprintf(stderr, "\n%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("\nALL TESTS PASSED\n");
  return 0;
}
