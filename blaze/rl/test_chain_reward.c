/* Unit tests for chain_reward + chain_curr. No Python. */
#include "chain_curr.h"
#include "chain_reward.h"

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
  if (d > tol) {
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
  float adv[2], ret[2];
  float gamma = 0.99f, lam = 0.95f;
  float delta1 = 1.f + gamma * 0.3f * 1.f - 0.4f;
  float gae1 = delta1;
  float delta0 = 1.f + gamma * 0.4f * 1.f - 0.5f;
  float gae0 = delta0 + gamma * lam * 1.f * gae1;

  cr_gae(rew, term, cut, val, next_val, gamma, lam, T, N, adv, ret);
  expect_near(adv[1], gae1, 1e-6f, "gae t=1 cut");
  expect_near(adv[0], gae0, 1e-6f, "gae t=0");
  expect_near(ret[1], gae1 + 0.4f, 1e-6f, "ret t=1");
  expect_near(ret[0], gae0 + 0.5f, 1e-6f, "ret t=0");

  /* True terminal zeros the bootstrap value. */
  term[1] = 1;
  cut[1] = 1;
  cr_gae(rew, term, cut, val, next_val, 1.f, 1.f, T, N, adv, ret);
  expect_near(adv[1], 1.f - 0.4f, 1e-6f, "gae terminal ignores next_val");
  expect_near(ret[1], 1.f, 1e-6f, "ret terminal = rew (val cancels)");
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

int main(void) {
  test_gae();
  test_reward_milestones();
  test_logs_fixture();
  test_curr();
  if (g_fails) {
    fprintf(stderr, "test_chain_reward: %d failure(s)\n", g_fails);
    return 1;
  }
  printf("test_chain_reward: PASS\n");
  return 0;
}
