/* Exercise the actual rollout with a deterministic environment and value
 * function. Link with function sections/dead stripping so unused trainer
 * entry points do not require policy backend libraries. */
#define main unused_trainer_main
#include "ppo.c"
#undef main

typedef struct {
  int code, steps, resets, captures, final_evaluations;
  float *pose;
} Mock;
static Mock mock;
static void *owned[64];
static int nowned;
static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static void *alloc_test(size_t n, size_t size) {
  void *p = calloc(n, size);
  check(p && nowned < 64, "test allocation");
  owned[nowned++] = p;
  return p;
}
int nn_forward(Nn *nn, const uint8_t *planes, const float *scalars, int n,
               float *logits, float *values) {
  (void)nn; (void)planes;
  memset(logits, 0, (size_t)n * NN_N_LOGITS * sizeof *logits);
  for (int i = 0; i < n; i++) {
    const float *s = scalars + i * POL_SCAL;
    values[i] = s[25] * 64.f + 10.f * s[26];
    if (s[25] * 64.f == 40.f) {
      check(mock.resets == 0, "final observation evaluated before reset");
      mock.final_evaluations++;
    }
  }
  return 0;
}
int nn_sample(Nn *nn, const float *logits, int n, int mode, int32_t *acts,
              float *logp, float *entropy) {
  (void)nn; (void)logits; (void)mode; (void)entropy;
  memset(acts, 0, (size_t)n * POL_HEADS * sizeof *acts);
  memset(logp, 0, (size_t)n * sizeof *logp);
  return 0;
}
static int step(void *env, const double *act, int repeat, unsigned short *cam,
                unsigned char *depth, unsigned char *edge, float *scal,
                float *rew, unsigned char *done, float *pose, int *status) {
  (void)env; (void)act; (void)repeat; (void)cam; (void)depth;
  (void)edge; (void)scal;
  memset(status, 0, ENV_STATUS * sizeof *status);
  status[CR_IX_PLANK] = mock.steps == 0 ? 1 : 0; /* could advance a frontier */
  pose[1] = mock.steps == 0 ? 40.f : 50.f;
  done[0] = mock.steps == 0 ? (unsigned char)mock.code : 0;
  rew[0] = 0;
  mock.pose = pose;
  mock.steps++;
  return 0;
}
static int reset(void *env, const unsigned char *mask) {
  (void)env;
  check(mask && mask[0], "ended lane reset");
  mock.resets++;
  mock.pose[1] = 999.f; /* reset observation must never bootstrap prior episode */
  return 0;
}
static int assign(void *env, const int *idx) { (void)env; (void)idx; return 0; }
static int capture(void *env, int lane, int slot) {
  (void)env; (void)lane; (void)slot; mock.captures++; return 0;
}
static void run_case(int code) {
  struct RolloutBuf b;
  TrainConfig cfg;
  BlazeFns fns;
  struct EnvStepCtx ctx;
  CrState cr;
  CrSpec spec;
  CrCurr curr;
  int64_t cap_last[CR_N_STAGES];
  int eps = 0;
  memset(&b, 0, sizeof b); memset(&cfg, 0, sizeof cfg);
  memset(&fns, 0, sizeof fns); memset(&ctx, 0, sizeof ctx);
  memset(&mock, 0, sizeof mock); mock.code = code;
  cfg.ep_dec = code == 0 ? 1 : 10;
  cfg.action_repeat = 4; cfg.cap_refresh = 1;
  cfg.gamma = 0.5f; cfg.lam = 1.f;
  fns.step_full = step; fns.reset = reset;
  fns.assign = assign; fns.capture = capture; ctx.fns = &fns;
  cr_spec_default(&spec);
  check(cr_state_init(&cr, 1, &spec) == 0, "reward state");
  check(cr_curr_init(&curr, 1, 1.f, 1) == 0, "curriculum state");
  for (int i = 0; i < CR_N_STAGES; i++) cap_last[i] = -100;
  b.nseeds = b.lmax = 1;
#define BUF(field, count) b.field = alloc_test((count), sizeof *b.field)
  BUF(cam, ENV_NPIX); BUF(depth, ENV_NPIX); BUF(edge, ENV_NPIX);
  BUF(scal6, ENV_SCAL); BUF(rew, 1); BUF(done_buf, 1); BUF(pose, ENV_POSE);
  BUF(status, ENV_STATUS); BUF(act_rows, ENV_ACT);
  BUF(planes_cur, ENV_N_CH * ENV_NPIX); BUF(scal_cur, POL_SCAL);
  BUF(acts_cur, POL_HEADS); BUF(logp_cur, 1); BUF(val_cur, 1);
  BUF(logits, NN_N_LOGITS); BUF(planes_roll, 2 * ENV_N_CH * ENV_NPIX);
  BUF(scal_roll, 2 * POL_SCAL); BUF(acts_roll, 2 * POL_HEADS);
  BUF(logp_roll, 2); BUF(val_roll, 2); BUF(rew_roll, 2); BUF(done_roll, 2);
  BUF(term_roll, 2); BUF(cut_roll, 2); BUF(cut_val_roll, 2); BUF(valid_roll, 2);
  BUF(ret_roll, 2); BUF(adv_roll, 2); BUF(next_val, 1);
  BUF(prior_frame, ENV_N_PLANES * ENV_NPIX); BUF(have_prior, 1);
  BUF(frame_scratch, ENV_N_PLANES * ENV_NPIX); BUF(ep_dec, 1); BUF(burnin, 1);
  BUF(reset_mask, 1); BUF(lane_seed, 1); BUF(lane_stage, 1);
  BUF(lane_stage_start, 1); BUF(lane_snap, 1); BUF(logs, 3);
#undef BUF
  b.pose[1] = 10.f;
  for (int i = 0; i < 3; i++) b.logs[i] = 1e9f;
  check(run_rollout(NULL, &mock, &ctx, &cfg, &fns, &cr, &curr, 1,
                    1, 2, 1, cap_last, NULL, NULL, NULL, &eps, &b,
                    NULL, NULL, NULL, NULL) == 0, "actual rollout succeeds");
  int terminal = code == BLAZE_DONE_DEATH || code == BLAZE_DONE_SUCCESS;
  check(b.term_roll[0] == terminal && b.cut_roll[0] == 1, "terminal and cut masks distinguish truncation");
  check(mock.resets == 1 && eps == 1, "ended episode resets once");
  check(mock.captures == 0, "ended world never becomes a frontier capture");
  check(mock.final_evaluations == !terminal, "only truncations evaluate final observation");
  float final_value = 40.f + 10.f / cfg.ep_dec;
  if (!terminal) check(b.cut_val_roll[0] == final_value, "final value includes pre-reset episode clock");
  cr_gae(b.rew_roll, b.term_roll, b.cut_roll, b.val_roll, b.next_val,
         b.cut_val_roll, cfg.gamma, cfg.lam, 2, 1, b.adv_roll, b.ret_roll);
  float expected = b.rew_roll[0] + (terminal ? 0.f : cfg.gamma * final_value);
  check(fabsf(b.ret_roll[0] - expected) < 1e-5f, "return excludes reset value and subsequent episode advantage");
  printf("rollout boundary: done=%d bootstrap=%g masks=%u/%u PASS\n",
         code, b.cut_val_roll[0], b.term_roll[0], b.cut_roll[0]);
  cr_curr_free(&curr); cr_state_free(&cr);
  while (nowned) free(owned[--nowned]);
}
int main(void) {
  run_case(BLAZE_DONE_BOUNDARY);
  run_case(BLAZE_DONE_RUNNING); /* decision horizon, same truncation path */
  run_case(BLAZE_DONE_DEATH);
  run_case(BLAZE_DONE_SUCCESS);
  return 0;
}
