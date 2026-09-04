/* Unit tests for chain_reward + chain_curr. No Python. */
#include "chain_curr.h"
#include "chain_reward.h"
#include "blaze_abi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
  float d = a > b ? a - b : b - a;
  if (!isfinite(a) || !isfinite(b) || d > tol) {
    fprintf(stderr, "FAIL: %s (got %g want %g)\n", msg, (double)a, (double)b);
    g_fails++;
  }
}

static void test_gae(void) {
  /* T=2 N=1; last step is a cut (timeout), not a true terminal. */
  const int T = 2, N = 1;
  float rew[2] = {1.f, 1.f};
  unsigned char term[2] = {0, 0};
  unsigned char cut[2] = {0, 1};
  float val[2] = {0.5f, 0.4f};
  float next_val[1] = {0.3f};
  float cut_val[2] = {0.f, 0.3f};
  float adv[2], ret[2];
  float gamma = 0.99f, lam = 0.95f;
  float delta1 = 1.f + gamma * 0.3f * 1.f - 0.4f;
  float gae1 = delta1;
  float delta0 = 1.f + gamma * 0.4f * 1.f - 0.5f;
  float gae0 = delta0 + gamma * lam * 1.f * gae1;

  cr_gae(rew, term, cut, val, next_val, cut_val, gamma, lam, T, N, adv, ret);
  expect_near(adv[1], gae1, 1e-6f, "gae t=1 cut");
  expect_near(adv[0], gae0, 1e-6f, "gae t=0");
  expect_near(ret[1], gae1 + 0.4f, 1e-6f, "ret t=1");
  expect_near(ret[0], gae0 + 0.5f, 1e-6f, "ret t=0");

  /* True terminal zeros the bootstrap value. */
  term[1] = 1;
  cut[1] = 1;
  cr_gae(rew, term, cut, val, next_val, cut_val, 1.f, 1.f, T, N, adv, ret);
  expect_near(adv[1], 1.f - 0.4f, 1e-6f, "gae terminal ignores next_val");
  expect_near(ret[1], 1.f, 1e-6f, "ret terminal = rew (val cancels)");

  /* Boundary in the middle of a rollout. The reset episode's value and
   * rewards are deliberately enormous: neither may leak across the cut. */
  term[0] = 0; cut[0] = 1; cut_val[0] = 4.f;
  val[1] = 999.f; rew[1] = 10000.f;
  cr_gae(rew, term, cut, val, next_val, cut_val, 0.5f, 1.f, T, N, adv, ret);
  expect_near(ret[0], 3.f, 1e-6f, "boundary bootstraps final value, not reset value");
  expect_near(adv[0], 2.5f, 1e-6f, "boundary cuts following episode advantage");
  term[0] = 1;
  cr_gae(rew, term, cut, val, next_val, cut_val, 0.5f, 1.f, T, N, adv, ret);
  expect_near(ret[0], 1.f, 1e-6f, "death does not bootstrap boundary value");
}

static void zero_status(int *st) { memset(st, 0, 17 * sizeof(int)); }

static void test_reward_milestones(void) {
  CrSpec spec;
  CrState st;
  int status[17];
  unsigned short cam[CR_NPIX];
  int32_t acts[9];
  float pose[5];
  float scal[6];
  unsigned char done;
  int lane_seed;
  float logs[3];
  float r;
  int i;

  cr_spec_default(&spec);
  expect_true(cr_state_init(&st, 1, &spec) == 0, "cr_state_init");
  memset(cam, 0, sizeof(cam));
  memset(acts, 0, sizeof(acts));
  memset(pose, 0, sizeof(pose));
  memset(scal, 0, sizeof(scal));
  done = 0;
  lane_seed = 0;
  logs[0] = 1e9f;
  logs[1] = 1e9f;
  logs[2] = 1e9f;
  zero_status(status);
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f, 1e-6f, "time cost only");

  zero_status(status);
  status[CR_IX_PLANK] = 1;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f + 2.0f, 1e-5f, "first plank");

  cr_reset_lane(&st, 0);
  zero_status(status);
  status[CR_IX_LOG] = 2;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f + 2.0f, 1e-5f, "two new logs");
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f, 1e-6f, "no new logs");

  cr_reset_lane(&st, 0);
  zero_status(status);
  done = 2;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f - 5.0f, 1e-5f, "death");

  cr_reset_lane(&st, 0);
  done = BLAZE_DONE_BOUNDARY;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1, &r);
  expect_near(r, -0.01f, 1e-6f, "boundary has neither death penalty nor success bonus");

  cr_reset_lane(&st, 0);
  zero_status(status);
  done = 0;
  status[CR_IX_LOG] = 5;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f + 5.0f, 1e-5f, "log clamp 5");

  /* Crosshair chop bonus: log under center, attacking, still chopping. */
  cr_reset_lane(&st, 0);
  zero_status(status);
  cam[(size_t)CR_CY * CR_CAM_W + CR_CX] = 17;
  acts[4] = 1;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f + 0.03f, 1e-5f, "chop crosshair");

  expect_eq_i(cr_stage_of_best(st.best), 0, "stage 0 after 0 logs");
  for (i = 0; i < 9; ++i)
    st.best[i] = 0;
  st.best[CR_IX_LOG] = 3;
  expect_eq_i(cr_stage_of_best(st.best), 1, "stage 1 at 3 logs");
  st.best[CR_IX_WPICK] = 1;
  expect_eq_i(cr_stage_of_best(st.best), 2, "stage 2 pick");
  st.best[CR_IX_COBBLE] = 3;
  expect_eq_i(cr_stage_of_best(st.best), 3, "stage 3 cobble");
  st.best[CR_IX_COAL] = 1;
  expect_eq_i(cr_stage_of_best(st.best), 4, "stage 4 coal");

  cr_state_free(&st);
}

static void test_reset_nonzero_inventory_no_first_bonus(void) {
  CrSpec spec;
  CrState st;
  int status[17];
  unsigned short cam[CR_NPIX];
  int32_t acts[9];
  float pose[5];
  float scal[6];
  unsigned char done;
  int lane_seed;
  float logs[3];
  float r;

  cr_spec_default(&spec);
  expect_true(cr_state_init(&st, 1, &spec) == 0, "cr_state_init seed");
  memset(cam, 0, sizeof(cam));
  memset(acts, 0, sizeof(acts));
  memset(pose, 0, sizeof(pose));
  memset(scal, 0, sizeof(scal));
  done = 0;
  lane_seed = 0;
  logs[0] = 1e9f;
  logs[1] = 1e9f;
  logs[2] = 1e9f;
  zero_status(status);
  status[CR_IX_LOG] = 3;
  status[CR_IX_PLANK] = 1;
  status[CR_IX_WPICK] = 1;
  cr_reset_lane(&st, 0);
  cr_seed_lane(&st, 0, status);
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f, 1e-6f, "reset into logs+plank+pick pays no first bonus");
  expect_eq_i(st.best[CR_IX_LOG], 3, "seeded log count");
  expect_eq_i(st.best[CR_IX_PLANK], 1, "seeded plank count");
  expect_eq_i(st.best[CR_IX_WPICK], 1, "seeded pick count");

  status[CR_IX_STICK] = 1;
  cr_step(&st, status, cam, acts, pose, scal, &done, &lane_seed, logs, 1, 1,
          &r);
  expect_near(r, -0.01f + 2.0f, 1e-5f, "new stick still pays first bonus");

  cr_state_free(&st);
}

static void test_logs_fixture(void) {
  float xyz[4096 * 3];
  int n = 0;
  int rc = cr_logs_from_bsnp(
      "../../verify/fixtures/port/s10_t0_r64_no_liquid.bsnp", xyz, 4096, &n);
  expect_eq_i(rc, 0, "logs from s10 fixture");
  expect_true(n > 0, "s10 has at least one log");
  if (n > 0) {
    expect_true(isfinite(xyz[0]) && isfinite(xyz[1]) && isfinite(xyz[2]),
                "log xyz finite");
  }
}

static void test_curr(void) {
  CrCurr c;
  int seeds[32];
  int stages[32];
  int i, n0;
  expect_true(cr_curr_init(&c, 2, 1.0f, 1) == 0, "curr init");
  cr_curr_sample(&c, 32, seeds, stages);
  for (i = 0; i < 32; ++i)
    expect_eq_i(stages[i], 0, "t0_share=1 always stage 0");
  cr_curr_free(&c);

  expect_true(cr_curr_init(&c, 1, 0.0f, 2) == 0, "curr init t0=0");
  cr_curr_record(&c, 0, 0, 1);
  expect_near(cr_curr_succ(&c, 0, 0), 1.f, 1e-6f, "succ 1/1");
  cr_curr_record(&c, 0, 0, 0);
  expect_near(cr_curr_succ(&c, 0, 0), 0.5f, 1e-6f, "succ 1/2");
  c.avail[1] = 1; /* seed 0 stage 1 now available */
  n0 = 0;
  cr_curr_sample(&c, 32, seeds, stages);
  for (i = 0; i < 32; ++i) {
    expect_true(stages[i] == 0 || stages[i] == 1, "stage in {0,1}");
    if (stages[i] == 0)
      n0++;
  }
  expect_true(n0 > 0 && n0 < 32, "frontier mix is not degenerate");
  cr_curr_free(&c);
}

static void test_recipe_parse(void) {
  CrSpec spec, copy, before;
  CrCurrConfig cfg, cc;
  FILE *f;
  char line[256], key[128], value[128];
  int count = 0;
  cr_spec_default(&spec);
  expect_eq_i(cr_spec_validate(&spec), 0, "default spec valid");
  before = spec;
  expect_eq_i(cr_spec_set(&spec, "reward.time_cost", "-1"), -2, "negative penalty invalid");
  expect_eq_i(cr_spec_set(&spec, "reward.w_log_per", "nan"), -2, "nan invalid");
  expect_eq_i(cr_spec_set(&spec, "reward.w_log_per", "1e999"), -2, "overflow invalid");
  expect_eq_i(cr_spec_set(&spec, "reward.w_log_per", "1x"), -2, "trailing garbage invalid");
  expect_eq_i(cr_spec_set(&spec, "reward.w_log_per", ""), -2, "empty invalid");
  expect_eq_i(cr_spec_set(&spec, "reward.w_log_per", "1000001"), -2, "bounded magnitude");
  expect_eq_i(cr_spec_set(&spec, "reward.unknown", "1"), -1, "unknown distinct");
  expect_true(!memcmp(&spec, &before, sizeof(spec)), "rejected spec changes atomic");
  f = tmpfile();
  expect_true(f != NULL, "reward dump file");
  if (f) {
    cr_spec_dump(f, &spec); rewind(f); memset(&copy, 0, sizeof(copy));
    while (fgets(line, sizeof(line), f)) {
      expect_eq_i(sscanf(line, "%127s = %127s", key, value), 2, "parse reward dump");
      expect_eq_i(cr_spec_set(&copy, key, value), 0, "every dumped reward field accepted");
      ++count;
    }
    expect_eq_i(count, (int)(sizeof(CrSpec) / sizeof(float)), "dump covers every coefficient");
    expect_true(!memcmp(&copy, &spec, sizeof(spec)), "reward dump exact roundtrip");
    fclose(f);
  }
  cr_curr_config_defaults(&cfg);
  expect_eq_i(cr_curr_config_validate(&cfg), 0, "default curriculum valid");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.min_episodes", "80"), 0, "cross fields deferred");
  expect_eq_i(cr_curr_config_validate(&cfg), -2, "episodes cannot exceed history");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.history_window", "100"), 0, "later history repairs config");
  expect_eq_i(cr_curr_config_validate(&cfg), 0, "cross fields valid after parse");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.history_window", "4097"), -2, "history bounded");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.min_episodes", "1.5"), -2, "integer episodes required");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.mastery_threshold", "1.01"), -2, "mastery range");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.stage_weight.5", "1"), -2, "stage index bounded");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.seed_weight.-1", "1"), -2, "seed index unsigned");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.seed_weight.256", "1"), -2, "seed index bounded");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.seed_weight.0", "inf"), -2, "seed weight finite");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.unknown", "1"), -1, "curriculum unknown distinct");
  expect_eq_i(cr_curr_config_set(&cfg, "curriculum.seed_weight.0", "0"), 0, "zero seed accepted by parser");
  expect_eq_i(cr_curr_config_validate_seeds(&cfg, 1), -2, "zero active seed sum rejected");
  expect_eq_i(cr_curr_config_validate_seeds(&cfg, 2), 0, "other active seed keeps sum positive");
  f = tmpfile(); expect_true(f != NULL, "curriculum dump file");
  if (f) {
    cr_curr_config_dump(f, &cfg); rewind(f); memset(&cc, 0, sizeof(cc));
    while (fgets(line, sizeof(line), f)) {
      expect_eq_i(sscanf(line, "%127s = %127s", key, value), 2, "parse curriculum dump");
      expect_eq_i(cr_curr_config_set(&cc, key, value), 0, "every dumped curriculum field accepted");
    }
    expect_true(!memcmp(&cc, &cfg, sizeof(cfg)), "curriculum dump exact roundtrip");
    fclose(f);
  }
}

static void test_reward_recipe_behavior(void) {
  CrSpec spec;
  CrState st;
  int status[17] = {0};
  unsigned short cam[CR_NPIX] = {0};
  int32_t acts[9] = {0};
  float pose[5] = {0}, scal[6] = {0}, r;
  unsigned char done = 0;
  cr_spec_default(&spec);
  cr_spec_set(&spec, "reward.shaping_scale", "0");
  cr_spec_set(&spec, "reward.w_log_per", "7");
  expect_eq_i(cr_state_init(&st, 1, &spec), 0, "custom reward init");
  cam[CR_CY * CR_CAM_W + CR_CX] = 17; acts[4] = 1;
  status[CR_IX_LOG] = 1;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, 6.99f, 1e-6f, "shaping zero preserves changed milestone");
  status[CR_IX_LOG] = 0;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  status[CR_IX_LOG] = 1;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, -.01f, 1e-6f, "drop and reacquire cannot repay milestone");
  spec.shaping_scale = 2.f;
  cr_state_set_spec(&st, &spec);
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, .05f, 1e-6f, "scale two doubles dense crosshair reward");
  /* Track iron progress while its weights are disabled, then enable it. */
  status[13] = 1; status[14] = 3; status[15] = 1; status[16] = 1;
  status[CR_ST_CONT] = 2;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  spec.w_furnace_first = 3.f; spec.w_furnace_open = 4.f;
  spec.w_ironore_per = 5.f; spec.w_ingot_first = 6.f; spec.w_ipick_first = 7.f;
  spec.shaping_scale = 0.f;
  expect_eq_i(cr_state_set_spec(&st, &spec), 0, "live spec change valid");
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, -.01f, 1e-6f, "enabling iron does not repay past milestones");
  cr_reset_lane(&st, 0); cr_seed_lane(&st, 0, status);
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, -.01f, 1e-6f, "seeded custom iron inventory and furnace cannot repay");
  status[14] = 4;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, 4.99f, 1e-6f, "new ore pays custom coefficient");
  done = BLAZE_DONE_DEATH;
  cr_step(&st, status, cam, acts, pose, scal, &done, NULL, NULL, 0, 0, &r);
  expect_near(r, -5.01f, 1e-6f, "shaping zero preserves death penalty");
  spec.time_cost = NAN;
  expect_eq_i(cr_state_set_spec(&st, &spec), -2, "live invalid spec rejected");
  expect_true(isfinite(st.spec.time_cost), "live rejected spec preserves state");
  cr_state_free(&st);
}

static int sample_stage_one(CrCurr *c) {
  int seeds[4096], stages[4096], i, count = 0;
  cr_curr_sample(c, 4096, seeds, stages);
  for (i = 0; i < 4096; ++i) count += stages[i] == 1;
  return count;
}

static void test_curr_recipe_behavior(void) {
  CrCurrConfig cfg;
  CrCurr a, b;
  int sa[4096], sb[4096], ta[4096], tb[4096], i, count;
  cr_curr_config_defaults(&cfg);
  expect_eq_i(cr_curr_init(&a, 2, .1f, 123), 0, "legacy defaults init");
  expect_eq_i(cr_curr_init_config(&b, 2, .1f, 123, &cfg), 0, "explicit defaults init");
  memset(a.avail, 1, 2 * CR_N_STAGES); memset(b.avail, 1, 2 * CR_N_STAGES);
  cr_curr_sample(&a, 4096, sa, ta); cr_curr_sample(&b, 4096, sb, tb);
  expect_true(!memcmp(sa, sb, sizeof(sa)) && !memcmp(ta, tb, sizeof(ta)), "default wrapper exact deterministic sequence");
  cr_curr_free(&a); cr_curr_free(&b);

  cfg.history_window = 4; cfg.min_episodes = 4; cfg.mastery_threshold = .75f;
  expect_eq_i(cr_curr_init_config(&a, 1, 0.f, 3, &cfg), 0, "short history init");
  a.avail[1] = 1;
  for (i = 0; i < 3; ++i) cr_curr_record(&a, 0, 0, 1);
  count = sample_stage_one(&a);
  expect_true(count > 400 && count < 850, "minimum episodes holds frontier on stage zero with rehearsal");
  cr_curr_record(&a, 0, 0, 0);
  count = sample_stage_one(&a);
  expect_true(count > 3200 && count < 3750, "configured mastery threshold advances frontier at equality");
  cfg.mastery_threshold = .9f;
  cr_curr_init_config(&b, 1, 0.f, 3, &cfg); b.avail[1] = 1;
  for (i = 0; i < 4; ++i) cr_curr_record(&b, 0, 0, i < 3);
  count = sample_stage_one(&b);
  expect_true(count > 400 && count < 850, "higher mastery threshold holds identical history back");
  cr_curr_free(&b); cfg.mastery_threshold = .75f;

  cr_curr_record(&a, 0, 0, 0);
  expect_near(cr_curr_succ(&a, 0, 0), .5f, 1e-6f, "history evicts oldest success");
  count = sample_stage_one(&a);
  expect_true(count > 400 && count < 850, "history window actually returns frontier after regression");
  cr_curr_free(&a);

  cfg.stage_weights[1] = 100.f; cfg.seed_weights[0] = 0.f;
  expect_eq_i(cr_curr_init_config(&a, 2, 0.f, 91, &cfg), 0, "weighted sampler init");
  expect_eq_i(cr_curr_init_config(&b, 2, 0.f, 91, &cfg), 0, "weighted deterministic twin");
  a.avail[CR_N_STAGES + 1] = b.avail[CR_N_STAGES + 1] = 1;
  cr_curr_sample(&a, 4096, sa, ta); cr_curr_sample(&b, 4096, sb, tb);
  count = 0;
  for (i = 0; i < 4096; ++i) {
    expect_eq_i(sa[i], 1, "zero-weight seed never sampled"); count += ta[i] == 1;
  }
  expect_true(count > 3700 && count < 4000, "stage weights change actual frontier distribution and retain rehearsal");
  expect_true(!memcmp(sa, sb, sizeof(sa)) && !memcmp(ta, tb, sizeof(ta)), "weighted draws deterministic");
  cr_curr_free(&a); cr_curr_free(&b);
  cfg.stage_weights[1] = 0.f; cfg.seed_weights[0] = 1.f; cfg.seed_weights[1] = 3.f;
  cr_curr_init_config(&a, 2, 0.f, 10, &cfg);
  memset(a.avail, 1, 2 * CR_N_STAGES);
  cr_curr_sample(&a, 4096, sa, ta); count = 0;
  for (i = 0; i < 4096; ++i) {
    expect_true(ta[i] != 1, "zero-weight available stage never sampled"); count += sa[i] == 1;
  }
  expect_true(count > 2900 && count < 3250, "seed weights change actual sample frequency");
  cr_curr_free(&a);
  cr_curr_init_config(&a, 2, 1.f, 10, &cfg);
  cr_curr_sample(&a, 4096, sa, ta);
  for (i = 0; i < 4096; ++i) expect_eq_i(ta[i], 0, "t0 share overrides stage mixture");
  cr_curr_free(&a);
}

int main(void) {
  test_recipe_parse();
  test_reward_recipe_behavior();
  test_curr_recipe_behavior();
  test_gae();
  test_reward_milestones();
  test_reset_nonzero_inventory_no_first_bonus();
  test_logs_fixture();
  test_curr();
  if (g_fails) {
    fprintf(stderr, "test_chain_reward: %d failure(s)\n", g_fails);
    return 1;
  }
  printf("test_chain_reward: PASS\n");
  return 0;
}
