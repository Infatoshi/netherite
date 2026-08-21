/* Fixed native tests for the Blaze policy FP32 CPU reference. */
#include "fixture.h"
#include "model.h"
#include "nn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Live PPO hyperparams for the FD loss mirror (set by FD test). */
static float g_test_clip = 0.2f;
static float g_test_vcoef = 0.5f;
static float g_test_ecoef = 0.01f;

static int g_fails = 0;

static void expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
  }
}

static void expect_eq_i(int a, int b, const char *msg) {
  if (a != b) {
    fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, a, b);
    g_fails++;
  }
}

static void expect_near(float a, float b, float tol, const char *msg) {
  float d = fabsf(a - b);
  if (d > tol || isnan(a) || isnan(b)) {
    fprintf(stderr, "FAIL: %s (got %.8g want %.8g tol %.3g diff %.3g)\n", msg,
            a, b, tol, d);
    g_fails++;
  }
}

static void expect_finite(float x, const char *msg) {
  if (!isfinite(x)) {
    fprintf(stderr, "FAIL: %s not finite (%.8g)\n", msg, x);
    g_fails++;
  }
}

/* Unique path under /tmp using process id. */
static void tmp_path(char *buf, size_t n, const char *tag) {
  snprintf(buf, n, "/tmp/blaze_nn_%s_%d.bin", tag, (int)getpid());
}
/* Build a CPU create descriptor and call the public nn_create. */
static Nn *create_cpu(int max_n, const NnConfig *cfg) {
  NnCreate d;
  d.backend = NN_BACKEND_CPU;
  d.device = 0;
  d.max_n = max_n;
  d.config = cfg ? *cfg : nn_config_default();
  return nn_create(&d);
}

/* ---- synthetic inputs ---- */

static void fill_inputs(uint8_t *planes, float *scalars, int n, uint64_t seed) {
  for (int ni = 0; ni < n; ++ni) {
    for (int c = 0; c < NN_N_CH; ++c) {
      for (int h = 0; h < NN_CAM_H; ++h) {
        for (int w = 0; w < NN_CAM_W; ++w) {
          float u = nn_hash_u01(seed, (uint32_t)ni, (uint32_t)c,
                                (uint32_t)(h * NN_CAM_W + w));
          uint8_t v = (uint8_t)(u * 255.f);
          planes[(((size_t)ni * NN_N_CH + c) * NN_CAM_H + h) * NN_CAM_W + w] =
              v;
        }
      }
    }
    for (int s = 0; s < NN_N_SCAL; ++s) {
      float u = nn_hash_u01(seed + 1, (uint32_t)ni, (uint32_t)s, 0u);
      scalars[(size_t)ni * NN_N_SCAL + s] = (u - 0.5f) * 2.f;
    }
  }
}

/* Special analytical weights: zero weights, constant biases. */
static void set_bias_only_weights(Nn *nn) {
  size_t np = nn_fixture_param_count();
  float *p = (float *)calloc(np, sizeof(float));
  expect_true(p != NULL, "alloc params");
  if (!p)
    return;
  size_t off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    size_t n = NN_TENSOR_FLOATS[t];
    if (t == NN_T_CONV1_B) {
      for (size_t i = 0; i < n; ++i)
        p[off + i] = 0.5f;
    } else if (t == NN_T_CONV2_B) {
      for (size_t i = 0; i < n; ++i)
        p[off + i] = 0.25f;
    } else if (t == NN_T_FC_B) {
      for (size_t i = 0; i < n; ++i)
        p[off + i] = 0.1f;
    } else if (t == NN_T_HEADS_B) {
      for (size_t i = 0; i < n; ++i)
        p[off + i] = 0.01f * (float)(i + 1);
    } else if (t == NN_T_VALUE_B) {
      p[off] = 1.5f;
    }
    off += n;
  }
  nn_fixture_set_params(nn, p, np);
  free(p);
}

static size_t param_index(int tensor_id, size_t local) {
  size_t off = 0;
  for (int t = 0; t < tensor_id; ++t)
    off += NN_TENSOR_FLOATS[t];
  return off + local;
}

/* ---- tests ---- */

static void test_layer_outputs_and_logits(void) {
  printf("test_layer_outputs_and_logits\n");
  NnConfig cfg = nn_config_default();
  Nn *nn = create_cpu(2, &cfg);
  expect_true(nn != NULL, "create");
  set_bias_only_weights(nn);

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[1 * NN_N_SCAL];
  memset(planes, 0, sizeof(planes));
  memset(scalars, 0, sizeof(scalars));
  planes[7 * NN_CAM_H * NN_CAM_W] = 255;

  float logits[NN_N_LOGITS];
  float values[1];
  expect_eq_i(nn_forward(nn, planes, scalars, 1, logits, values), 0, "fwd");

  float c1[NN_C_OUT1 * NN_H1 * NN_W1];
  size_t n1 = nn_fixture_copy_layer(nn, 0, 0, c1, sizeof(c1) / sizeof(float));
  expect_eq_i((int)n1, NN_C_OUT1 * NN_H1 * NN_W1, "c1 count");
  expect_near(c1[0], 0.5f, 1e-6f, "c1[0]");
  expect_near(c1[n1 / 2], 0.5f, 1e-6f, "c1 mid");

  float c2[NN_C_OUT2 * NN_H2 * NN_W2];
  size_t n2 = nn_fixture_copy_layer(nn, 1, 0, c2, sizeof(c2) / sizeof(float));
  expect_eq_i((int)n2, NN_C_OUT2 * NN_H2 * NN_W2, "c2 count");
  expect_near(c2[0], 0.25f, 1e-6f, "c2[0]");

  float fcin[NN_FC_IN];
  nn_fixture_copy_layer(nn, 2, 0, fcin, NN_FC_IN);
  expect_near(fcin[0], 0.25f, 1e-6f, "fc_in flat");
  expect_near(fcin[NN_FLAT], 0.f, 1e-6f, "fc_in scal0");

  float hid[NN_FC_OUT];
  nn_fixture_copy_layer(nn, 3, 0, hid, NN_FC_OUT);
  expect_near(hid[0], 0.1f, 1e-6f, "hidden");

  for (int i = 0; i < NN_N_LOGITS; ++i)
    expect_near(logits[i], 0.01f * (float)(i + 1), 1e-6f, "logit");
  expect_near(values[0], 1.5f, 1e-6f, "value");

  nn_destroy(nn);
}

/*
 * Sparse nonzero path through conv1, conv2, FC, one head, value.
 *
 * Weights (all others zero, all biases zero):
 *   conv1_w[oc=0, ic=0, kh=0, kw=0] = 2
 *   conv2_w[oc=0, ic=0, kh=0, kw=0] = 3
 *   fc_w[o=0, i=0]                   = 4   (flat index 0 = c0,h0,w0)
 *   heads_w[logit0, hidden0]         = 5
 *   value_w[0]                       = 6
 *
 * Input: planes[c=0,h=0,w=0] = 1 (not a depth channel), rest 0; scalars 0.
 *
 * Hand computation:
 *   conv1(oc0,oh0,ow0) = 1 * 2 = 2; elsewhere 0  -> ReLU: 2
 *   conv2(oc0,oh0,ow0) = 2 * 3 = 6; elsewhere 0  -> ReLU: 6
 *   fc_in[0] = 6; hidden[0] = 4 * 6 = 24
 *   logit[0] = 5 * 24 = 120; other logits 0
 *   value    = 6 * 24 = 144
 */
static void test_sparse_nonzero_layers(void) {
  printf("test_sparse_nonzero_layers\n");
  NnConfig cfg = nn_config_default();
  Nn *nn = create_cpu(1, &cfg);
  expect_true(nn != NULL, "create");

  size_t np = nn_fixture_param_count();
  float *p = (float *)calloc(np, sizeof(float));
  expect_true(p != NULL, "alloc");
  if (!p) {
    nn_destroy(nn);
    return;
  }

  /* conv1_w layout [oc, ic, kh, kw]: index = ((oc*C + ic)*K + kh)*K + kw */
  p[param_index(NN_T_CONV1_W, 0)] = 2.f;
  p[param_index(NN_T_CONV2_W, 0)] = 3.f;
  p[param_index(NN_T_FC_W, 0)] = 4.f;
  p[param_index(NN_T_HEADS_W, 0)] = 5.f; /* row 0, col 0 */
  p[param_index(NN_T_VALUE_W, 0)] = 6.f;
  nn_fixture_set_params(nn, p, np);
  free(p);

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  memset(planes, 0, sizeof(planes));
  memset(scalars, 0, sizeof(scalars));
  planes[0] = 1; /* c=0, h=0, w=0 */

  float logits[NN_N_LOGITS];
  float values[1];
  expect_eq_i(nn_forward(nn, planes, scalars, 1, logits, values), 0, "fwd");

  float c1[NN_C_OUT1 * NN_H1 * NN_W1];
  nn_fixture_copy_layer(nn, 0, 0, c1, sizeof(c1) / sizeof(float));
  expect_near(c1[0], 2.f, 1e-5f, "sparse c1[0,0,0]");
  expect_near(c1[1], 0.f, 1e-5f, "sparse c1 neighbor 0");

  float c2[NN_C_OUT2 * NN_H2 * NN_W2];
  nn_fixture_copy_layer(nn, 1, 0, c2, sizeof(c2) / sizeof(float));
  expect_near(c2[0], 6.f, 1e-5f, "sparse c2[0,0,0]");

  float fcin[NN_FC_IN];
  nn_fixture_copy_layer(nn, 2, 0, fcin, NN_FC_IN);
  expect_near(fcin[0], 6.f, 1e-5f, "sparse fc_in[0]");

  float hid[NN_FC_OUT];
  nn_fixture_copy_layer(nn, 3, 0, hid, NN_FC_OUT);
  expect_near(hid[0], 24.f, 1e-4f, "sparse hidden[0]");
  expect_near(hid[1], 0.f, 1e-5f, "sparse hidden[1]");

  expect_near(logits[0], 120.f, 1e-3f, "sparse logit0 (head0 cat0)");
  for (int i = 1; i < NN_N_LOGITS; ++i)
    expect_near(logits[i], 0.f, 1e-5f, "sparse other logit");
  expect_near(values[0], 144.f, 1e-3f, "sparse value");

  nn_destroy(nn);
}

static void check_acts_in_range(const int32_t *acts, int n, const char *msg) {
  for (int i = 0; i < n; ++i) {
    for (int h = 0; h < NN_N_HEAD; ++h) {
      int a = acts[i * NN_N_HEAD + h];
      expect_true(a >= 0 && a < NN_HEAD_WIDTHS[h], msg);
    }
  }
}

static void test_sample_repeatable(void) {
  printf("test_sample_repeatable\n");
  NnConfig cfg = nn_config_default();
  cfg.rng_seed = 42;
  Nn *nn = create_cpu(4, &cfg);
  expect_true(nn != NULL, "create");

  const int n = 3;
  uint8_t *planes =
      (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
  float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
  float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *values = (float *)malloc((size_t)n * sizeof(float));
  int32_t acts_a[3 * NN_N_HEAD];
  int32_t acts_b[3 * NN_N_HEAD];
  float logp_a[3], logp_b[3], ent[3];
  fill_inputs(planes, scalars, n, 99);
  expect_eq_i(nn_forward(nn, planes, scalars, n, logits, values), 0, "fwd");
  expect_eq_i(nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts_a, logp_a, ent),
              0, "sample a");
  expect_eq_i(nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts_b, logp_b, NULL),
              0, "sample b");
  expect_true(memcmp(acts_a, acts_b, sizeof(acts_a)) != 0,
              "consecutive gumbel acts differ");
  for (int i = 0; i < n; ++i) {
    expect_finite(logp_a[i], "logp finite");
    expect_finite(logp_b[i], "logp b finite");
    expect_finite(ent[i], "ent finite");
  }
  check_acts_in_range(acts_a, n, "act a in range");
  check_acts_in_range(acts_b, n, "act b in range");

  Nn *twin = create_cpu(4, &cfg);
  expect_true(twin != NULL, "twin create");
  int32_t acts_twin[3 * NN_N_HEAD];
  float logp_twin[3];
  expect_eq_i(
      nn_sample(twin, logits, n, NN_SAMPLE_GUMBEL, acts_twin, logp_twin, NULL),
      0, "twin first sample");
  expect_true(memcmp(acts_a, acts_twin, sizeof(acts_a)) == 0,
              "twin first acts match");
  for (int i = 0; i < n; ++i)
    expect_near(logp_a[i], logp_twin[i], 0.f, "twin logp match");

  NnConfig cfg_lr = cfg;
  cfg_lr.lr = 1e-3f;
  expect_eq_i(nn_set_config(nn, &cfg_lr), 0, "set_config same seed");
  int32_t acts_c[3 * NN_N_HEAD];
  float logp_c[3];
  expect_eq_i(nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts_c, logp_c, NULL),
              0, "sample after same-seed set_config");
  expect_true(memcmp(acts_a, acts_c, sizeof(acts_a)) != 0,
              "same-seed set_config does not replay");
  int32_t acts_t2[3 * NN_N_HEAD];
  int32_t acts_t3[3 * NN_N_HEAD];
  float logp_t2[3], logp_t3[3];
  expect_eq_i(
      nn_sample(twin, logits, n, NN_SAMPLE_GUMBEL, acts_t2, logp_t2, NULL), 0,
      "twin step 1");
  expect_eq_i(
      nn_sample(twin, logits, n, NN_SAMPLE_GUMBEL, acts_t3, logp_t3, NULL), 0,
      "twin step 2");
  expect_true(memcmp(acts_b, acts_t2, sizeof(acts_b)) == 0, "twin step 1 match");
  expect_true(memcmp(acts_c, acts_t3, sizeof(acts_c)) == 0,
              "continued step matches twin");

  NnConfig cfg_other = cfg;
  cfg_other.rng_seed = 99;
  expect_eq_i(nn_set_config(nn, &cfg_other), 0, "set other seed");
  int32_t acts_other[3 * NN_N_HEAD];
  float logp_other[3];
  expect_eq_i(
      nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts_other, logp_other, NULL),
      0, "sample other seed");
  expect_true(memcmp(acts_a, acts_other, sizeof(acts_a)) != 0,
              "other seed differs");
  expect_eq_i(nn_set_config(nn, &cfg), 0, "restore seed");
  int32_t acts_replay[3 * NN_N_HEAD];
  float logp_replay[3];
  expect_eq_i(
      nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts_replay, logp_replay, NULL),
      0, "replay sample");
  expect_true(memcmp(acts_a, acts_replay, sizeof(acts_a)) == 0,
              "seed restore replays first sample");
  for (int i = 0; i < n; ++i)
    expect_near(logp_a[i], logp_replay[i], 0.f, "replay logp");

  int32_t acts_g[3 * NN_N_HEAD];
  expect_eq_i(
      nn_sample(nn, logits, n, NN_SAMPLE_GREEDY, acts_g, logp_a, NULL), 0,
      "greedy");
  for (int i = 0; i < n; ++i) {
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const float *row = logits + (size_t)i * NN_N_LOGITS + NN_HEAD_OFF[h];
      int best = 0;
      for (int c = 1; c < NN_HEAD_WIDTHS[h]; ++c)
        if (row[c] > row[best])
          best = c;
      expect_eq_i(acts_g[i * NN_N_HEAD + h], best, "greedy argmax");
    }
  }

  free(planes);
  free(scalars);
  free(logits);
  free(values);
  nn_destroy(twin);
  nn_destroy(nn);
}

/* Scalar loss for FD: PPO total with fixed batch fields. */
static float ppo_loss_only(Nn *nn, const uint8_t *planes, const float *scalars,
                           const int32_t *acts, const float *old_logp,
                           const float *advantages, const float *returns,
                           int n) {
  float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *values = (float *)malloc((size_t)n * sizeof(float));
  float *logp = (float *)malloc((size_t)n * sizeof(float));
  float *ent = (float *)malloc((size_t)n * sizeof(float));
  nn_forward(nn, planes, scalars, n, logits, values);
  for (int ni = 0; ni < n; ++ni) {
    float lp = 0.f, eh = 0.f;
    for (int h = 0; h < NN_N_HEAD; ++h) {
      int w = NN_HEAD_WIDTHS[h];
      int off = NN_HEAD_OFF[h];
      const float *row = logits + (size_t)ni * NN_N_LOGITS + off;
      float m = row[0];
      for (int c = 1; c < w; ++c)
        if (row[c] > m)
          m = row[c];
      float sum = 0.f;
      float ex[NN_W_MAX];
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      float inv = 1.f / sum;
      int a = acts[(size_t)ni * NN_N_HEAD + h];
      lp += row[a] - (m + logf(sum));
      float e = 0.f;
      for (int c = 0; c < w; ++c) {
        float p = ex[c] * inv;
        if (p > 0.f)
          e -= p * logf(p);
      }
      eh += e;
    }
    logp[ni] = lp;
    ent[ni] = eh;
  }
  float clip = g_test_clip;
  float vcoef = g_test_vcoef;
  float ecoef = g_test_ecoef;

  float pl = 0.f, vl = 0.f, em = 0.f;
  for (int i = 0; i < n; ++i) {
    float ratio = expf(logp[i] - old_logp[i]);
    float adv = advantages[i];
    float s1 = ratio * adv;
    float rc = ratio;
    if (rc < 1.f - clip)
      rc = 1.f - clip;
    if (rc > 1.f + clip)
      rc = 1.f + clip;
    float s2 = rc * adv;
    float obj = s1 < s2 ? s1 : s2;
    pl += -obj;
    float d = values[i] - returns[i];
    vl += d * d;
    em += ent[i];
  }
  float inv = 1.f / (float)n;
  float total = pl * inv + vcoef * vl * inv - ecoef * em * inv;
  free(logits);
  free(values);
  free(logp);
  free(ent);
  return total;
}

static void test_finite_diff_grads(void) {
  printf("test_finite_diff_grads\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 0.f; /* no weight change during analytic step */
  cfg.entropy_coef = 0.01f;
  cfg.value_coef = 0.5f;
  cfg.ppo_clip = 0.2f;
  cfg.grad_limit = 1e9f; /* no clip for FD agreement */
  g_test_clip = cfg.ppo_clip;
  g_test_vcoef = cfg.value_coef;
  g_test_ecoef = cfg.entropy_coef;

  Nn *nn = create_cpu(2, &cfg);
  expect_true(nn != NULL, "create");

  const int n = 2;
  uint8_t planes[2 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[2 * NN_N_SCAL];
  fill_inputs(planes, scalars, n, 7);

  float logits[2 * NN_N_LOGITS];
  float values[2];
  nn_forward(nn, planes, scalars, n, logits, values);

  int32_t acts[2 * NN_N_HEAD];
  float logp0[2], ent0[2];
  nn_sample(nn, logits, n, NN_SAMPLE_GREEDY, acts, logp0, ent0);

  float old_logp[2], adv[2], ret[2];
  for (int i = 0; i < n; ++i) {
    old_logp[i] = logp0[i] + 0.05f * (float)(i + 1);
    adv[i] = 0.3f - 0.1f * (float)i;
    ret[i] = values[i] + 0.2f * (float)(i + 1);
  }

  size_t np = nn_fixture_param_count();
  float *params = (float *)malloc(np * sizeof(float));
  float *grads = (float *)malloc(np * sizeof(float));
  nn_fixture_get_params(nn, params, np);

  NnUpdateStats st;
  expect_eq_i(nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, &st),
              0, "update for grads");
  expect_finite(st.total_loss, "loss finite");
  nn_fixture_get_grads(nn, grads, np);
  nn_fixture_set_params(nn, params, np);

  /* Selected indices through both convs and all head groups + value. */
  size_t checks[] = {
      param_index(NN_T_CONV1_W, 0),
      param_index(NN_T_CONV1_W, 17),
      param_index(NN_T_CONV2_W, 0),
      param_index(NN_T_CONV2_W, 100),
      param_index(NN_T_FC_W, 0),
      param_index(NN_T_HEADS_W, 0),
      param_index(NN_T_HEADS_W, 256 * 3),
      param_index(NN_T_HEADS_B, 0),
      param_index(NN_T_HEADS_B, 10),
      param_index(NN_T_HEADS_B, 24),
      param_index(NN_T_VALUE_W, 0),
      param_index(NN_T_VALUE_B, 0),
  };
  const int nchecks = (int)(sizeof(checks) / sizeof(checks[0]));
  const float eps = 1e-3f;

  for (int c = 0; c < nchecks; ++c) {
    size_t idx = checks[c];
    float p0 = params[idx];
    params[idx] = p0 + eps;
    nn_fixture_set_params(nn, params, np);
    float lp = ppo_loss_only(nn, planes, scalars, acts, old_logp, adv, ret, n);
    params[idx] = p0 - eps;
    nn_fixture_set_params(nn, params, np);
    float lm = ppo_loss_only(nn, planes, scalars, acts, old_logp, adv, ret, n);
    params[idx] = p0;
    nn_fixture_set_params(nn, params, np);

    float fd = (lp - lm) / (2.f * eps);
    float an = grads[idx];
    float abs_err = fabsf(fd - an);
    float denom = fmaxf(1e-3f, fmaxf(fabsf(fd), fabsf(an)));
    float rel_err = abs_err / denom;
    int pass = (abs_err <= 2e-3f) || (rel_err <= 2e-2f);
    printf("  FD[%d] idx=%zu fd=%.8g analytic=%.8g abs_err=%.8g rel_err=%.8g "
           "%s\n",
           c, idx, fd, an, abs_err, rel_err, pass ? "ok" : "FAIL");
    if (!pass) {
      fprintf(stderr,
              "FAIL: FD check %d must pass (abs<=2e-3 or rel<=2e-2)\n", c);
      g_fails++;
    }
  }

  free(params);
  free(grads);
  nn_destroy(nn);
}

static void test_ppo_and_adam(void) {
  printf("test_ppo_and_adam\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  cfg.rng_seed = 1;
  Nn *nn = create_cpu(4, &cfg);
  expect_true(nn != NULL, "create");

  const int n = 4;
  uint8_t *planes =
      (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
  float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
  fill_inputs(planes, scalars, n, 123);

  float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *values = (float *)malloc((size_t)n * sizeof(float));
  nn_forward(nn, planes, scalars, n, logits, values);

  int32_t *acts = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
  float *logp = (float *)malloc((size_t)n * sizeof(float));
  nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL);

  float *old_logp = (float *)malloc((size_t)n * sizeof(float));
  float *adv = (float *)malloc((size_t)n * sizeof(float));
  float *ret = (float *)malloc((size_t)n * sizeof(float));
  for (int i = 0; i < n; ++i) {
    old_logp[i] = logp[i];
    adv[i] = (float)(i + 1) * 0.1f;
    ret[i] = values[i] + 0.5f;
  }

  size_t np = nn_fixture_param_count();
  float *before = (float *)malloc(np * sizeof(float));
  float *after = (float *)malloc(np * sizeof(float));
  nn_fixture_get_params(nn, before, np);

  NnUpdateStats st;
  expect_eq_i(
      nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, &st), 0,
      "ppo update");
  expect_finite(st.policy_loss, "pl");
  expect_finite(st.value_loss, "vl");
  expect_finite(st.entropy_mean, "ent");
  expect_finite(st.total_loss, "tl");
  expect_finite(st.grad_norm, "gn");
  expect_finite(st.approx_kl, "kl");
  expect_finite(st.clipfrac, "clipfrac");
  expect_true(st.grad_norm > 0.f, "grad norm > 0");
  expect_true(st.approx_kl >= 0.f, "kl >= 0");
  expect_true(st.clipfrac >= 0.f && st.clipfrac <= 1.f, "clipfrac in [0,1]");
  /* old_logp == current logp => ratio 1, k1=0, nothing clipped. */
  expect_true(fabsf(st.approx_kl) < 1e-5f, "kl ~0 at ratio=1");
  expect_true(fabsf(st.clipfrac) < 1e-6f, "clipfrac 0 at ratio=1");

  nn_fixture_get_params(nn, after, np);
  double delta = 0.0;
  for (size_t i = 0; i < np; ++i) {
    double d = (double)after[i] - (double)before[i];
    delta += d * d;
  }
  expect_true(delta > 0.0, "Adam changed params");

  expect_eq_i(
      nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, &st), 0,
      "adam step 2");
  expect_finite(st.total_loss, "loss2");

  free(planes);
  free(scalars);
  free(logits);
  free(values);
  free(acts);
  free(logp);
  free(old_logp);
  free(adv);
  free(ret);
  free(before);
  free(after);
  nn_destroy(nn);
}

static void test_invalid_action_rejected(void) {
  printf("test_invalid_action_rejected\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  Nn *nn = create_cpu(2, &cfg);
  expect_true(nn != NULL, "create");

  const int n = 1;
  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  fill_inputs(planes, scalars, n, 11);

  float logits[NN_N_LOGITS];
  float values[1];
  nn_forward(nn, planes, scalars, n, logits, values);

  int32_t acts[NN_N_HEAD];
  float logp[1];
  nn_sample(nn, logits, n, NN_SAMPLE_GREEDY, acts, logp, NULL);

  /* Corrupt head 0: width is 3, so 3 is invalid. */
  acts[0] = 3;

  size_t np = nn_fixture_param_count();
  float *before = (float *)malloc(np * sizeof(float));
  float *after = (float *)malloc(np * sizeof(float));
  nn_fixture_get_params(nn, before, np);

  float old_logp[1] = {logp[0]};
  float adv[1] = {0.5f};
  float ret[1] = {values[0]};
  int rc = nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, NULL);
  expect_true(rc != 0, "update rejects invalid action");
  expect_true(nn_last_error() != NULL && nn_last_error()[0] != 0,
              "error message set");
  expect_true(strstr(nn_last_error(), "action") != NULL,
              "error mentions action");

  nn_fixture_get_params(nn, after, np);
  expect_true(memcmp(before, after, np * sizeof(float)) == 0,
              "params unchanged after reject");

  free(before);
  free(after);
  nn_destroy(nn);
}

static void test_checkpoint_roundtrip(void) {
  printf("test_checkpoint_roundtrip\n");
  NnConfig cfg = nn_config_default();
  Nn *a = create_cpu(2, &cfg);
  Nn *b = create_cpu(2, &cfg);
  expect_true(a && b, "create");

  char path[256];
  tmp_path(path, sizeof(path), "roundtrip");
  expect_eq_i(nn_save(a, path), 0, "save");

  size_t np = nn_fixture_param_count();
  float *zeros = (float *)calloc(np, sizeof(float));
  nn_fixture_set_params(b, zeros, np);
  free(zeros);
  expect_eq_i(nn_load(b, path), 0, "load");

  float *pa = (float *)malloc(np * sizeof(float));
  float *pb = (float *)malloc(np * sizeof(float));
  nn_fixture_get_params(a, pa, np);
  nn_fixture_get_params(b, pb, np);
  expect_true(memcmp(pa, pb, np * sizeof(float)) == 0, "params equal");

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  fill_inputs(planes, scalars, 1, 5);
  float la[NN_N_LOGITS], lb[NN_N_LOGITS], va[1], vb[1];
  nn_forward(a, planes, scalars, 1, la, va);
  nn_forward(b, planes, scalars, 1, lb, vb);
  for (int i = 0; i < NN_N_LOGITS; ++i)
    expect_near(la[i], lb[i], 0.f, "logit equal after load");
  expect_near(va[0], vb[0], 0.f, "value equal after load");

  free(pa);
  free(pb);
  nn_destroy(a);
  nn_destroy(b);
  unlink(path);
}

static void test_truncated_checkpoint(void) {
  printf("test_truncated_checkpoint\n");
  NnConfig cfg = nn_config_default();
  Nn *nn = create_cpu(1, &cfg);
  char path[256];
  char trunc[256];
  tmp_path(path, sizeof(path), "full");
  tmp_path(trunc, sizeof(trunc), "trunc");
  expect_eq_i(nn_save(nn, path), 0, "save full");

  FILE *f = fopen(path, "rb");
  expect_true(f != NULL, "open full");
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  expect_true(sz > 64, "size");
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  expect_true(fread(buf, 1, (size_t)sz, f) == (size_t)sz, "read full ckpt");
  fclose(f);

  FILE *t = fopen(trunc, "wb");
  expect_true(t != NULL, "open trunc");
  expect_true(fwrite(buf, 1, (size_t)sz / 2, t) == (size_t)sz / 2, "write trunc");
  fclose(t);
  free(buf);

  expect_true(nn_load(nn, trunc) != 0, "reject truncated");
  expect_true(nn_last_error() != NULL && nn_last_error()[0] != 0, "error set");

  nn_destroy(nn);
  unlink(path);
  unlink(trunc);
}

static size_t heap_used(void) {
  /* Linux: /proc/self/statm resident pages. macOS: probe unavailable -> 0. */
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return 0;
  size_t size = 0, resident = 0;
  if (fscanf(f, "%zu %zu", &size, &resident) != 2) {
    fclose(f);
    return 0;
  }
  fclose(f);
  return resident;
}

static void test_no_heap_growth(void) {
  printf("test_no_heap_growth\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-4f;
  Nn *nn = create_cpu(4, &cfg);
  expect_true(nn != NULL, "create");

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
  fill_inputs(planes, scalars, n, 3);
  nn_forward(nn, planes, scalars, n, logits, values);
  nn_sample(nn, logits, n, NN_SAMPLE_GREEDY, acts, logp, NULL);
  for (int i = 0; i < n; ++i) {
    old_logp[i] = logp[i];
    adv[i] = 0.1f;
    ret[i] = values[i];
  }

  for (int i = 0; i < 3; ++i) {
    nn_forward(nn, planes, scalars, n, logits, values);
    nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, NULL);
  }
  size_t h0 = heap_used();
  for (int i = 0; i < 20; ++i) {
    nn_forward(nn, planes, scalars, n, logits, values);
    nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL);
    nn_update(nn, planes, scalars, acts, old_logp, adv, ret, n, NULL);
  }
  size_t h1 = heap_used();
  if (h0 > 0 && h1 > 0) {
    expect_true(h1 <= h0 + 64, "no heap growth on forward/update");
    if (h1 > h0 + 64)
      fprintf(stderr, "  heap %zu -> %zu\n", h0, h1);
  } else {
    expect_true(1, "heap probe skipped (non-Linux)");
  }

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

static void test_config_validation(void) {
  printf("test_config_validation\n");
  NnConfig bad = nn_config_default();
  bad.lr = -1.f;
  expect_true(create_cpu(1, &bad) == NULL, "reject lr < 0");

  bad = nn_config_default();
  bad.ppo_clip = 1.f;
  expect_true(create_cpu(1, &bad) == NULL, "reject ppo_clip >= 1");

  bad = nn_config_default();
  bad.grad_limit = 0.f;
  expect_true(create_cpu(1, &bad) == NULL, "reject grad_limit == 0");

  bad = nn_config_default();
  bad.value_coef = NAN;
  expect_true(create_cpu(1, &bad) == NULL, "reject non-finite");

  NnConfig ok = nn_config_default();
  Nn *nn = create_cpu(1, &ok);
  expect_true(nn != NULL, "good create");
  bad = ok;
  bad.entropy_coef = -0.1f;
  expect_true(nn_set_config(nn, &bad) != 0, "set_config reject");
  expect_eq_i(nn_set_config(nn, &ok), 0, "set_config accept");
  nn_destroy(nn);
}

static void test_dispatch_backends(void) {
  printf("test_dispatch_backends\n");
  NnConfig cfg = nn_config_default();

  /* NULL descriptor is invalid. */
  expect_true(nn_create(NULL) == NULL, "null descriptor rejected");
  expect_true(nn_last_error()[0] != 0, "null desc sets error");

  /* Invalid backend enum. */
  {
    NnCreate d;
    d.backend = (NnBackend)99;
    d.device = 0;
    d.max_n = 1;
    d.config = cfg;
    expect_true(nn_create(&d) == NULL, "invalid backend rejected");
    expect_true(strstr(nn_last_error(), "backend") != NULL,
                "error names backend");
  }

  /* CPU device != 0. */
  {
    NnCreate d;
    d.backend = NN_BACKEND_CPU;
    d.device = 1;
    d.max_n = 1;
    d.config = cfg;
    expect_true(nn_create(&d) == NULL, "CPU device != 0 rejected");
    expect_true(strstr(nn_last_error(), "device") != NULL ||
                    strstr(nn_last_error(), "CPU") != NULL,
                "error mentions device/CPU");
  }

  /* Unavailable CUDA (this build has no CUDA). */
  {
    NnCreate d;
    d.backend = NN_BACKEND_CUDA;
    d.device = 0;
    d.max_n = 1;
    d.config = cfg;
    expect_true(nn_create(&d) == NULL, "CUDA unavailable");
    expect_true(strstr(nn_last_error(), "CUDA") != NULL,
                "error names CUDA");
  }

  /* Unavailable Metal in CPU-only build. */
  {
    NnCreate d;
    d.backend = NN_BACKEND_METAL;
    d.device = 0;
    d.max_n = 1;
    d.config = cfg;
    expect_true(nn_create(&d) == NULL, "Metal unavailable in CPU build");
    expect_true(strstr(nn_last_error(), "Metal") != NULL,
                "error names Metal");
  }

  /* Happy path still works. */
  {
    Nn *nn = create_cpu(1, &cfg);
    expect_true(nn != NULL, "CPU create ok");
    nn_destroy(nn);
  }
}

int main(void) {
  test_config_validation();
  test_dispatch_backends();
  test_layer_outputs_and_logits();
  test_sparse_nonzero_layers();
  test_sample_repeatable();
  test_finite_diff_grads();
  test_ppo_and_adam();
  test_invalid_action_rejected();
  test_checkpoint_roundtrip();
  test_truncated_checkpoint();
  test_no_heap_growth();

  if (g_fails) {
    fprintf(stderr, "\n%d failure(s)\n", g_fails);
    return 1;
  }
  printf("\nALL TESTS PASSED\n");
  return 0;
}
