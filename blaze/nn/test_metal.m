/* Native parity tests: Metal FP32 policy vs CPU reference. */
#import "fixture.h"
#import "model.h"
#import "nn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Limits from task. */
static const float kFwdTol = 2e-3f;
static const float kWgtTol = 5e-3f;

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

static void report_max_err(const char *label, float max_err, float limit) {
  printf("  %s: max_err=%.6g limit=%.6g %s\n", label, max_err, limit,
         max_err <= limit ? "ok" : "FAIL");
  if (max_err > limit) {
    fprintf(stderr, "FAIL: %s exceeds limit\n", label);
    g_fails++;
  }
}

static float max_abs_diff(const float *a, const float *b, size_t n) {
  float m = 0.f;
  for (size_t i = 0; i < n; ++i) {
    float d = fabsf(a[i] - b[i]);
    if (d > m)
      m = d;
    if (isnan(a[i]) || isnan(b[i]))
      return INFINITY;
  }
  return m;
}

static void tmp_path(char *buf, size_t n, const char *tag) {
  snprintf(buf, n, "/tmp/blaze_nn_metal_%s_%d.bin", tag, (int)getpid());
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
/* Public-API create helpers (dispatch gate: no backend structs). */
static Nn *create_backend(NnBackend backend, int max_n, int device,
                          const NnConfig *cfg) {
  NnCreate d;
  d.backend = backend;
  d.device = device;
  d.max_n = max_n;
  d.config = cfg ? *cfg : nn_config_default();
  return nn_create(&d);
}

static Nn *create_cpu(int max_n, const NnConfig *cfg) {
  return create_backend(NN_BACKEND_CPU, max_n, 0, cfg);
}

static Nn *create_metal(int max_n, const NnConfig *cfg) {
  return create_backend(NN_BACKEND_METAL, max_n, 0, cfg);
}

/* Shared checkpoint so CPU and Metal start from identical weights. */
static int write_shared_ckpt(const char *path) {
  NnConfig cfg = nn_config_default();
  Nn *cpu = create_cpu(1, &cfg);
  if (!cpu)
    return -1;
  int rc = nn_save(cpu, path);
  nn_destroy(cpu);
  return rc;
}

static void test_config_and_batch_reject(void) {
  printf("test_config_and_batch_reject\n");
  NnConfig bad = nn_config_default();
  bad.lr = -1.f;
  expect_true(create_metal(1, &bad) == NULL, "reject lr < 0");
  expect_true(nn_last_error()[0] != 0, "error set for bad lr");

  bad = nn_config_default();
  bad.ppo_clip = 1.f;
  expect_true(create_metal(1, &bad) == NULL, "reject ppo_clip >= 1");

  bad = nn_config_default();
  bad.grad_limit = 0.f;
  expect_true(create_metal(1, &bad) == NULL, "reject grad_limit == 0");

  NnConfig ok = nn_config_default();
  Nn *nn = create_metal(2, &ok);
  expect_true(nn != NULL, "good create batch 2");
  if (!nn)
    return;

  bad = ok;
  bad.entropy_coef = -0.1f;
  expect_true(nn_set_config(nn, &bad) != 0, "set_config reject");
  expect_eq_i(nn_set_config(nn, &ok), 0, "set_config accept");

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  float logits[NN_N_LOGITS];
  float values[1];
  memset(planes, 0, sizeof(planes));
  memset(scalars, 0, sizeof(scalars));
  expect_true(nn_forward(nn, planes, scalars, 1, logits, values) != 0,
              "reject mismatched batch");
  expect_true(strstr(nn_last_error(), "batch") != NULL,
              "error mentions batch");

  nn_destroy(nn);
}

static void test_forward_parity(int n, const char *ckpt) {
  printf("test_forward_parity n=%d\n", n);
  NnConfig cfg = nn_config_default();
  cfg.rng_seed = 7;
  Nn *cpu = create_cpu(n, &cfg);
  Nn *mtl = create_metal(n, &cfg);
  expect_true(cpu && mtl, "create cpu+metal");
  if (!cpu || !mtl) {
    nn_destroy(cpu);
    nn_destroy(mtl);
    return;
  }
  expect_eq_i(nn_load(cpu, ckpt), 0, "cpu load");
  expect_eq_i(nn_load(mtl, ckpt), 0, "metal load");

  uint8_t *planes =
      (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
  float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
  float *lc = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *lm = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *vc = (float *)malloc((size_t)n * sizeof(float));
  float *vm = (float *)malloc((size_t)n * sizeof(float));
  fill_inputs(planes, scalars, n, 99);

  expect_eq_i(nn_forward(cpu, planes, scalars, n, lc, vc), 0, "cpu fwd");
  expect_eq_i(nn_forward(mtl, planes, scalars, n, lm, vm), 0, "mtl fwd");

  float e_log = max_abs_diff(lc, lm, (size_t)n * NN_N_LOGITS);
  float e_val = max_abs_diff(vc, vm, (size_t)n);
  char lab[64];
  snprintf(lab, sizeof(lab), "logits n=%d", n);
  report_max_err(lab, e_log, kFwdTol);
  snprintf(lab, sizeof(lab), "values n=%d", n);
  report_max_err(lab, e_val, kFwdTol);

  free(planes);
  free(scalars);
  free(lc);
  free(lm);
  free(vc);
  free(vm);
  nn_destroy(cpu);
  nn_destroy(mtl);
}

static void test_sample_parity(const char *ckpt) {
  printf("test_sample_parity\n");
  const int n = 4;
  NnConfig cfg = nn_config_default();
  cfg.rng_seed = 42;
  Nn *cpu = create_cpu(n, &cfg);
  Nn *mtl = create_metal(n, &cfg);
  expect_true(cpu && mtl, "create");
  if (!cpu || !mtl) {
    nn_destroy(cpu);
    nn_destroy(mtl);
    return;
  }
  nn_load(cpu, ckpt);
  nn_load(mtl, ckpt);

  uint8_t *planes =
      (uint8_t *)malloc((size_t)n * NN_N_CH * NN_CAM_H * NN_CAM_W);
  float *scalars = (float *)malloc((size_t)n * NN_N_SCAL * sizeof(float));
  float *logits = (float *)malloc((size_t)n * NN_N_LOGITS * sizeof(float));
  float *values = (float *)malloc((size_t)n * sizeof(float));
  fill_inputs(planes, scalars, n, 11);
  nn_forward(mtl, planes, scalars, n, logits, values);

  int32_t *ac = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
  int32_t *am = (int32_t *)malloc((size_t)n * NN_N_HEAD * sizeof(int32_t));
  float *lpc = (float *)malloc((size_t)n * sizeof(float));
  float *lpm = (float *)malloc((size_t)n * sizeof(float));
  float *ec = (float *)malloc((size_t)n * sizeof(float));
  float *em = (float *)malloc((size_t)n * sizeof(float));

  expect_eq_i(nn_sample(cpu, logits, n, NN_SAMPLE_GREEDY, ac, lpc, ec), 0,
              "cpu greedy");
  expect_eq_i(nn_sample(mtl, logits, n, NN_SAMPLE_GREEDY, am, lpm, em), 0,
              "mtl greedy");
  expect_true(memcmp(ac, am, (size_t)n * NN_N_HEAD * sizeof(int32_t)) == 0,
              "greedy acts equal");
  report_max_err("greedy logp", max_abs_diff(lpc, lpm, (size_t)n), 0.f);
  report_max_err("greedy entropy", max_abs_diff(ec, em, (size_t)n), kFwdTol);

  expect_eq_i(nn_sample(cpu, logits, n, NN_SAMPLE_GUMBEL, ac, lpc, ec), 0,
              "cpu gumbel");
  expect_eq_i(nn_sample(mtl, logits, n, NN_SAMPLE_GUMBEL, am, lpm, em), 0,
              "mtl gumbel");
  expect_true(memcmp(ac, am, (size_t)n * NN_N_HEAD * sizeof(int32_t)) == 0,
              "gumbel acts equal");
  report_max_err("gumbel logp", max_abs_diff(lpc, lpm, (size_t)n), kFwdTol);
  report_max_err("gumbel entropy", max_abs_diff(ec, em, (size_t)n), kFwdTol);

  free(planes);
  free(scalars);
  free(logits);
  free(values);
  free(ac);
  free(am);
  free(lpc);
  free(lpm);
  free(ec);
  free(em);
  nn_destroy(cpu);
  nn_destroy(mtl);
}

static void check_grad_norm_rel(const char *label, float cpu_gn, float mtl_gn,
                                float rel_lim) {
  float e_gn = fabsf(cpu_gn - mtl_gn);
  float denom = fmaxf(1.f, fmaxf(fabsf(cpu_gn), fabsf(mtl_gn)));
  float rel = e_gn / denom;
  float abs_lim = rel_lim * denom;
  printf("  %s: cpu=%.6g mtl=%.6g max_err=%.6g limit=%.6g (rel=%.4g)\n", label,
         cpu_gn, mtl_gn, e_gn, abs_lim, rel);
  if (!(mtl_gn > 0.f) || !(cpu_gn > 0.f)) {
    fprintf(stderr, "FAIL: %s grad_norm must be nonzero\n", label);
    g_fails++;
  } else if (e_gn > abs_lim) {
    fprintf(stderr, "FAIL: %s grad_norm exceeds %.0f%% relative limit\n", label,
            rel_lim * 100.f);
    g_fails++;
  } else {
    printf("  %s: ok\n", label);
  }
}

static void test_update_and_weights(const char *ckpt) {
  printf("test_update_and_weights\n");
  const int n = 4;
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  cfg.rng_seed = 1;
  Nn *cpu = create_cpu(n, &cfg);
  Nn *mtl = create_metal(n, &cfg);
  expect_true(cpu && mtl, "create");
  if (!cpu || !mtl) {
    nn_destroy(cpu);
    nn_destroy(mtl);
    return;
  }
  nn_load(cpu, ckpt);
  nn_load(mtl, ckpt);

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
  fill_inputs(planes, scalars, n, 123);

  nn_forward(cpu, planes, scalars, n, logits, values);
  nn_sample(cpu, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL);
  for (int i = 0; i < n; ++i) {
    old_logp[i] = logp[i];
    adv[i] = (float)(i + 1) * 0.1f;
    ret[i] = values[i] + 0.5f;
  }

  NnUpdateStats sc, sm;
  expect_eq_i(
      nn_update(cpu, planes, scalars, acts, old_logp, adv, ret, n, &sc), 0,
      "cpu update");
  expect_eq_i(nn_update(mtl, planes, scalars, acts, old_logp, adv, ret, n,
                              &sm),
              0, "mtl update");

  printf("  cpu  pl=%.6g vl=%.6g ent=%.6g tot=%.6g gn=%.6g kl=%.6g clip=%.6g\n",
         sc.policy_loss, sc.value_loss, sc.entropy_mean, sc.total_loss,
         sc.grad_norm, sc.approx_kl, sc.clipfrac);
  printf("  mtl  pl=%.6g vl=%.6g ent=%.6g tot=%.6g gn=%.6g kl=%.6g clip=%.6g\n",
         sm.policy_loss, sm.value_loss, sm.entropy_mean, sm.total_loss,
         sm.grad_norm, sm.approx_kl, sm.clipfrac);

  report_max_err("policy_loss", fabsf(sc.policy_loss - sm.policy_loss), kFwdTol);
  report_max_err("value_loss", fabsf(sc.value_loss - sm.value_loss), kFwdTol);
  report_max_err("entropy_mean", fabsf(sc.entropy_mean - sm.entropy_mean),
                 kFwdTol);
  report_max_err("total_loss", fabsf(sc.total_loss - sm.total_loss), kFwdTol);
  report_max_err("approx_kl", fabsf(sc.approx_kl - sm.approx_kl), kFwdTol);
  report_max_err("clipfrac", fabsf(sc.clipfrac - sm.clipfrac), kFwdTol);
  check_grad_norm_rel("grad_norm", sc.grad_norm, sm.grad_norm, 0.02f);

  char path_c[256], path_m[256];
  tmp_path(path_c, sizeof(path_c), "cpu_upd");
  tmp_path(path_m, sizeof(path_m), "mtl_upd");
  expect_eq_i(nn_save(cpu, path_c), 0, "cpu save");
  expect_eq_i(nn_save(mtl, path_m), 0, "mtl save");

  size_t np = nn_fixture_param_count();
  float *pc = (float *)malloc(np * sizeof(float));
  float *pm = (float *)malloc(np * sizeof(float));
  /* Reload into fresh host tensors via fixture */
  {
    float **tc = (float **)calloc(NN_T_COUNT, sizeof(float *));
    float **tm = (float **)calloc(NN_T_COUNT, sizeof(float *));
    for (int t = 0; t < NN_T_COUNT; ++t) {
      tc[t] = (float *)malloc(NN_TENSOR_FLOATS[t] * sizeof(float));
      tm[t] = (float *)malloc(NN_TENSOR_FLOATS[t] * sizeof(float));
    }
    expect_eq_i(nn_fixture_load(path_c, tc), 0, "load cpu ckpt");
    expect_eq_i(nn_fixture_load(path_m, tm), 0, "load mtl ckpt");
    nn_fixture_pack_params((const float *const *)tc, pc);
    nn_fixture_pack_params((const float *const *)tm, pm);
    float e = max_abs_diff(pc, pm, np);
    report_max_err("weights after 1 Adam step", e, kWgtTol);
    for (int t = 0; t < NN_T_COUNT; ++t) {
      free(tc[t]);
      free(tm[t]);
    }
    free(tc);
    free(tm);
  }

  unlink(path_c);
  unlink(path_m);
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
  free(pm);
  nn_destroy(cpu);
  nn_destroy(mtl);
}

/* Policy-only PPO: value/entropy coefs off, old_logp == current logp so
 * ratio=1 and surr1==surr2. Proves the min branch carries a nonzero
 * policy gradient (equality must select surr1). */
static void test_policy_only_grad(const char *ckpt) {
  printf("test_policy_only_grad\n");
  const int n = 4;
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  cfg.value_coef = 0.f;
  cfg.entropy_coef = 0.f;
  cfg.rng_seed = 1;
  Nn *cpu = create_cpu(n, &cfg);
  Nn *mtl = create_metal(n, &cfg);
  expect_true(cpu && mtl, "create");
  if (!cpu || !mtl) {
    nn_destroy(cpu);
    nn_destroy(mtl);
    return;
  }
  nn_load(cpu, ckpt);
  nn_load(mtl, ckpt);

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
  fill_inputs(planes, scalars, n, 123);

  nn_forward(cpu, planes, scalars, n, logits, values);
  nn_sample(cpu, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL);
  for (int i = 0; i < n; ++i) {
    old_logp[i] = logp[i]; /* same policy: ratio=1, surr1==surr2 */
    adv[i] = (float)(i + 1) * 0.1f;
    ret[i] = values[i] + 0.5f;
  }

  NnUpdateStats sc, sm;
  expect_eq_i(
      nn_update(cpu, planes, scalars, acts, old_logp, adv, ret, n, &sc), 0,
      "cpu update");
  expect_eq_i(nn_update(mtl, planes, scalars, acts, old_logp, adv, ret, n,
                              &sm),
              0, "mtl update");

  printf("  cpu  pl=%.6g vl=%.6g ent=%.6g tot=%.6g gn=%.6g kl=%.6g clip=%.6g\n",
         sc.policy_loss, sc.value_loss, sc.entropy_mean, sc.total_loss,
         sc.grad_norm, sc.approx_kl, sc.clipfrac);
  printf("  mtl  pl=%.6g vl=%.6g ent=%.6g tot=%.6g gn=%.6g kl=%.6g clip=%.6g\n",
         sm.policy_loss, sm.value_loss, sm.entropy_mean, sm.total_loss,
         sm.grad_norm, sm.approx_kl, sm.clipfrac);

  report_max_err("policy_only policy_loss",
                 fabsf(sc.policy_loss - sm.policy_loss), kFwdTol);
  report_max_err("policy_only value_loss", fabsf(sc.value_loss - sm.value_loss),
                 kFwdTol);
  report_max_err("policy_only total_loss",
                 fabsf(sc.total_loss - sm.total_loss), kFwdTol);
  report_max_err("policy_only approx_kl", fabsf(sc.approx_kl - sm.approx_kl),
                 kFwdTol);
  report_max_err("policy_only clipfrac", fabsf(sc.clipfrac - sm.clipfrac),
                 kFwdTol);
  check_grad_norm_rel("policy_only grad_norm", sc.grad_norm, sm.grad_norm,
                      0.02f);

  free(planes);
  free(scalars);
  free(logits);
  free(values);
  free(acts);
  free(logp);
  free(old_logp);
  free(adv);
  free(ret);
  nn_destroy(cpu);
  nn_destroy(mtl);
}

static void test_invalid_action(const char *ckpt) {
  printf("test_invalid_action\n");
  NnConfig cfg = nn_config_default();
  cfg.lr = 1e-3f;
  Nn *nn = create_metal(1, &cfg);
  expect_true(nn != NULL, "create");
  if (!nn)
    return;
  nn_load(nn, ckpt);

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  float logits[NN_N_LOGITS];
  float values[1];
  fill_inputs(planes, scalars, 1, 11);
  nn_forward(nn, planes, scalars, 1, logits, values);

  int32_t acts[NN_N_HEAD];
  float logp[1];
  nn_sample(nn, logits, 1, NN_SAMPLE_GREEDY, acts, logp, NULL);
  acts[0] = 3; /* head 0 width is 3 */

  float old_logp[1] = {logp[0]};
  float adv[1] = {0.5f};
  float ret[1] = {values[0]};
  int rc =
      nn_update(nn, planes, scalars, acts, old_logp, adv, ret, 1, NULL);
  expect_true(rc != 0, "update rejects invalid action");
  expect_true(strstr(nn_last_error(), "action") != NULL,
              "error mentions action");
  nn_destroy(nn);
}

static void test_ckpt_roundtrip(const char *ckpt) {
  printf("test_ckpt_roundtrip\n");
  NnConfig cfg = nn_config_default();
  Nn *a = create_metal(2, &cfg);
  Nn *b = create_metal(2, &cfg);
  expect_true(a && b, "create");
  if (!a || !b) {
    nn_destroy(a);
    nn_destroy(b);
    return;
  }
  expect_eq_i(nn_load(a, ckpt), 0, "load a");

  char path[256];
  tmp_path(path, sizeof(path), "rt");
  expect_eq_i(nn_save(a, path), 0, "save a");
  expect_eq_i(nn_load(b, path), 0, "load b");

  uint8_t planes[1 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars[NN_N_SCAL];
  /* batch is 2: pad second sample zeros */
  uint8_t planes2[2 * NN_N_CH * NN_CAM_H * NN_CAM_W];
  float scalars2[2 * NN_N_SCAL];
  memset(planes2, 0, sizeof(planes2));
  memset(scalars2, 0, sizeof(scalars2));
  fill_inputs(planes, scalars, 1, 5);
  memcpy(planes2, planes, sizeof(planes));
  memcpy(scalars2, scalars, sizeof(scalars));

  float la[2 * NN_N_LOGITS], lb[2 * NN_N_LOGITS], va[2], vb[2];
  nn_forward(a, planes2, scalars2, 2, la, va);
  nn_forward(b, planes2, scalars2, 2, lb, vb);
  report_max_err("roundtrip logits",
                 max_abs_diff(la, lb, 2 * (size_t)NN_N_LOGITS), 0.f);
  report_max_err("roundtrip values", max_abs_diff(va, vb, 2), 0.f);

  unlink(path);
  nn_destroy(a);
  nn_destroy(b);
}

static void test_dispatch_gates(void) {
  printf("test_dispatch_gates\n");
  NnConfig cfg = nn_config_default();

  /* Metal device != 0. */
  {
    Nn *nn = create_backend(NN_BACKEND_METAL, 1, 1, &cfg);
    expect_true(nn == NULL, "Metal device != 0 rejected");
    expect_true(strstr(nn_last_error(), "device") != NULL ||
                    strstr(nn_last_error(), "Metal") != NULL,
                "error mentions device/Metal");
  }

  /* Unavailable CUDA never falls back. */
  {
    Nn *nn = create_backend(NN_BACKEND_CUDA, 1, 0, &cfg);
    expect_true(nn == NULL, "CUDA unavailable");
    expect_true(strstr(nn_last_error(), "CUDA") != NULL, "error names CUDA");
  }

  /* Invalid backend. */
  {
    Nn *nn = create_backend((NnBackend)42, 1, 0, &cfg);
    expect_true(nn == NULL, "invalid backend rejected");
    expect_true(strstr(nn_last_error(), "backend") != NULL,
                "error names backend");
  }

  /* NULL descriptor. */
  expect_true(nn_create(NULL) == NULL, "null descriptor");
  expect_true(nn_last_error()[0] != 0, "null desc error");
}

int main(void) {
  char ckpt[256];
  tmp_path(ckpt, sizeof(ckpt), "shared");
  if (write_shared_ckpt(ckpt) != 0) {
    fprintf(stderr, "FAIL: cannot write shared checkpoint\n");
    return 1;
  }

  test_config_and_batch_reject();
  test_dispatch_gates();
  test_forward_parity(1, ckpt);
  test_forward_parity(4, ckpt);
  test_sample_parity(ckpt);
  test_update_and_weights(ckpt);
  test_policy_only_grad(ckpt);
  test_invalid_action(ckpt);
  test_ckpt_roundtrip(ckpt);

  unlink(ckpt);

  if (g_fails) {
    fprintf(stderr, "\n%d failure(s)\n", g_fails);
    return 1;
  }
  printf("\nALL METAL TESTS PASSED\n");
  return 0;
}
