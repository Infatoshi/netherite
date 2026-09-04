/* ppo.c - C11 chain trainer: Blaze env (dlopen) + blaze/nn policy.
 *
 * One host binary. Config selects backend + device.
 *   Linux:  cpu | cuda
 *   Darwin: cpu | metal  (CPU tick; Metal overwrites cam/depth/edge; Metal nn)
 * Env drivers export the same symbols; load via dlopen + function table.
 * CUDA env ABI is device pointers: env_cuda_stage copies host↔device.
 * No environment-variable behavior. No backend fallback.
 *
 * Allocation contract: every buffer is allocated once before run_rollout.
 * run_rollout has a compile-time gate against malloc/calloc/realloc/free.
 */
#define _DEFAULT_SOURCE
#include "train_config.h"
#include "world_recipe.h"

#include "blaze_abi.h"
#include "chain_curr.h"
#include "chain_reward.h"
#include "model.h"
#include "nn.h"
#include "obs_pack.h"
#include "rl_ckpt.h"

#if defined(BLAZE_RL_HAVE_CUDA) && BLAZE_RL_HAVE_CUDA
#include "env_cuda_stage.h"
#endif
#if defined(BLAZE_RL_HAVE_METAL) && BLAZE_RL_HAVE_METAL
#include "env_metal_obs.h"
#endif

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef BLAZE_RL_HAVE_CUDA
#define BLAZE_RL_HAVE_CUDA 0
#endif
#ifndef BLAZE_RL_HAVE_METAL
#define BLAZE_RL_HAVE_METAL 0
#endif

enum {
  MAX_SEEDS = 32,
  LOG_CAP = 8192,
  T0_TRAIL = 200,
  MAX_SNAPS = MAX_SEEDS * CR_N_STAGES
};

static const char kEnvCpuSo[] = "out/blaze/env/blaze_cpu.so";
#if BLAZE_RL_HAVE_CUDA
static const char kEnvCudaSo[] = "out/blaze/env/blaze_cuda.so";
#endif

#if !(defined(BLAZE_RL_HAVE_CUDA) && BLAZE_RL_HAVE_CUDA)
typedef int (*BlazeStepFullFn)(void *h, const double *actions, int repeat,
                               unsigned short *cam, unsigned char *depth,
                               unsigned char *edge, float *scal, float *rew,
                               unsigned char *done, float *pose, int *status);
#endif

#if !(defined(BLAZE_RL_HAVE_METAL) && BLAZE_RL_HAVE_METAL)
typedef int (*BlazeObsCamInputsFn)(void *vh, int env, double *ex, double *ey,
                                   double *ez, float *yaw, float *pitch,
                                   int *x0, int *y0, int *z0, int *nx, int *ny,
                                   int *nz, const unsigned short **cells);
#endif

typedef struct BlazeFns {
  void *lib;
  void *(*create)(int device, int n, const BlazeCreateOpts *opts);
  void (*destroy)(void *h);
  int (*load_snapshots)(void *h, const char *const *paths, int count,
                        char *err, int err_cap);
  int (*assign)(void *h, const int *snap_idx);
  int (*reset)(void *h, const unsigned char *mask);
  BlazeStepFullFn step_full;
  BlazeObsCamInputsFn obs_cam_inputs;
  int (*set_success_item)(void *h, int item);
  int (*capture)(void *h, int env, int slot);
} BlazeFns;

static void die(const char *msg) {
  fprintf(stderr, "ppo: %s\n", msg);
  exit(1);
}

static void dief(const char *fmt, const char *a) {
  fprintf(stderr, "ppo: ");
  fprintf(stderr, fmt, a);
  fputc('\n', stderr);
  exit(1);
}

static void blaze_fns_close(BlazeFns *f) {
  if (!f)
    return;
  if (f->lib)
    dlclose(f->lib);
  memset(f, 0, sizeof(*f));
}

static void *must_dlsym(void *lib, const char *name) {
  void *p;
  const char *err;
  dlerror();
  p = dlsym(lib, name);
  err = dlerror();
  if (err != NULL || !p) {
    fprintf(stderr, "ppo: dlsym %s: %s\n", name, err ? err : "null");
    return NULL;
  }
  return p;
}

static int blaze_fns_load(BlazeFns *f, const char *path, int want_cam_inputs) {
  void *lib;
  if (!f || !path)
    return -1;
  memset(f, 0, sizeof(*f));
  lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "ppo: dlopen '%s' failed: %s\n", path, dlerror());
    return -1;
  }
  f->lib = lib;
  f->create = (void *(*)(int, int, const BlazeCreateOpts *))must_dlsym(
      lib, "blaze_create");
  f->destroy = (void (*)(void *))must_dlsym(lib, "blaze_destroy");
  f->load_snapshots =
      (int (*)(void *, const char *const *, int, char *, int))must_dlsym(
          lib, "blaze_load_snapshots");
  f->assign = (int (*)(void *, const int *))must_dlsym(lib, "blaze_assign");
  f->reset =
      (int (*)(void *, const unsigned char *))must_dlsym(lib, "blaze_reset");
  f->step_full = (BlazeStepFullFn)must_dlsym(lib, "blaze_step_full");
  f->set_success_item =
      (int (*)(void *, int))must_dlsym(lib, "blaze_set_success_item");
  f->capture = (int (*)(void *, int, int))must_dlsym(lib, "blaze_capture");
  if (want_cam_inputs) {
    f->obs_cam_inputs =
        (BlazeObsCamInputsFn)must_dlsym(lib, "blaze_obs_cam_inputs");
  }
  if (!f->create || !f->destroy || !f->load_snapshots || !f->assign ||
      !f->reset || !f->step_full || !f->set_success_item || !f->capture ||
      (want_cam_inputs && !f->obs_cam_inputs)) {
    blaze_fns_close(f);
    return -1;
  }
  return 0;
}

static int path_is_reg(const char *path) {
  struct stat st;
  if (!path || !path[0])
    return 0;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISREG(st.st_mode) ? 1 : 0;
}

static int mkdir_parents_for_file(const char *path) {
  char buf[TR_CFG_STR_MAX + 16];
  size_t n;
  char *slash;
  char *p;

  n = strlen(path);
  if (n == 0 || n >= sizeof(buf))
    return -1;
  memcpy(buf, path, n + 1);
  slash = strrchr(buf, '/');
  if (!slash)
    return 0;
  *slash = '\0';
  if (!buf[0])
    return 0;
  for (p = buf + 1; *p; ++p) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }
  if (mkdir(buf, 0755) != 0 && errno != EEXIST)
    return -1;
  return 0;
}

/* checkpoint.bin -> checkpoint_best.bin / checkpoint_best.json
 * checkpoint     -> checkpoint_best.bin / checkpoint_best.json */
static int best_ckpt_paths(const char *ckpt, char *bin, size_t bin_cap,
                           char *side, size_t side_cap) {
  size_t n;
  size_t stem;
  if (!ckpt || !bin || !side || !ckpt[0])
    return -1;
  n = strlen(ckpt);
  stem = n;
  if (n >= 4 && memcmp(ckpt + n - 4, ".bin", 4) == 0)
    stem = n - 4;
  if (stem + 9 >= bin_cap || stem + 10 >= side_cap)
    return -1;
  memcpy(bin, ckpt, stem);
  memcpy(bin + stem, "_best.bin", 10);
  memcpy(side, ckpt, stem);
  memcpy(side + stem, "_best.json", 11);
  return 0;
}

static float t0_from_trail(const uint8_t *trail, int n) {
  int k;
  float s = 0.f;
  if (!trail || n <= 0)
    return 0.f;
  for (k = 0; k < n; ++k)
    s += (float)trail[k];
  return s / (float)n;
}

/* Schema-1 weights plus a ticks/t0 sidecar. Last checkpoint is untouched. */
static int save_best_ckpt(Nn *nn, const char *ckpt, int64_t ticks, float t0,
                          char *bin_out, size_t bin_cap) {
  char bin[TR_CFG_STR_MAX + 16];
  char side[TR_CFG_STR_MAX + 16];
  FILE *f;
  size_t n;

  if (best_ckpt_paths(ckpt, bin, sizeof(bin), side, sizeof(side)) != 0)
    return -1;
  if (mkdir_parents_for_file(bin) != 0)
    return -1;
  if (nn_save(nn, bin) != 0)
    return -1;
  f = fopen(side, "w");
  if (!f)
    return -2;
  fprintf(f, "{\"ticks\": %lld, \"t0\": %.9g}\n", (long long)ticks, (double)t0);
  if (fclose(f) != 0)
    return -2;
  if (bin_out && bin_cap) {
    n = strlen(bin);
    if (n + 1 > bin_cap)
      return -1;
    memcpy(bin_out, bin, n + 1);
  }
  return 0;
}
struct RolloutBuf {
  unsigned short *cam;
  unsigned char *depth;
  unsigned char *edge;
  float *scal6;
  float *rew;
  unsigned char *done_buf;
  float *pose;
  int *status;
  double *act_rows;
  uint8_t *planes_cur;
  float *scal_cur;
  int32_t *acts_cur;
  float *logp_cur;
  float *val_cur;
  float *logits;
  uint8_t *planes_roll;
  float *scal_roll;
  int32_t *acts_roll;
  float *logp_roll;
  float *val_roll;
  float *rew_roll;
  unsigned char *done_roll;
  unsigned char *term_roll;
  unsigned char *cut_roll;
  unsigned char *valid_roll;
  float *ret_roll;
  float *adv_roll;
  float *next_val;
  float *cut_val_roll;
  uint8_t *prior_frame;
  uint8_t *have_prior;
  uint8_t *frame_scratch;
  int *ep_dec;
  uint8_t *burnin;
  uint8_t *reset_mask;
  int *lane_seed;
  int *lane_stage;
  int *lane_stage_start;
  int *lane_snap;
  int *assign;
  float *logs;
  int nseeds;
  int lmax;
  /* minibatch scratch */
  uint8_t *planes_mb;
  float *scal_mb;
  int32_t *acts_mb;
  float *logp_mb;
  float *adv_mb;
  float *ret_mb;
  int *perm;
};

struct EnvStepCtx {
  BlazeFns *fns;
#if BLAZE_RL_HAVE_CUDA
  EnvCudaStage *stage;
#endif
#if BLAZE_RL_HAVE_METAL
  EnvMetalObs *metal;
#endif
  int is_cuda;
  int is_metal;
};

static int env_step(struct EnvStepCtx *ctx, void *env, const double *act,
                    int repeat, unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
  int rc;
  if (!ctx || !ctx->fns || !ctx->fns->step_full)
    return -1;
#if BLAZE_RL_HAVE_CUDA
  if (ctx->is_cuda) {
    if (!ctx->stage)
      return -1;
    return env_cuda_stage_step_full(ctx->stage, ctx->fns->step_full, env, act,
                                    repeat, cam, depth, edge, scal, rew, done,
                                    pose, status);
  }
#endif
  rc = ctx->fns->step_full(env, act, repeat, cam, depth, edge, scal, rew, done,
                           pose, status);
  if (rc != 0)
    return rc;
#if BLAZE_RL_HAVE_METAL
  if (ctx->is_metal) {
    if (!ctx->metal)
      return -1;
    if (env_metal_obs_overwrite(ctx->metal, env, cam, depth, edge) != 0)
      return -1;
  }
#endif
  return 0;
}

static int cap_slot(int nseeds, int si, int stage) {
  if (stage <= 0)
    return si;
  return nseeds + si * (CR_N_STAGES - 1) + (stage - 1);
}

static uint64_t rng_next(uint64_t *s) {
  uint64_t x = *s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *s = x;
  return x * 0x2545F4914F6CDD1DULL;
}

static void shuffle_int(int *a, int n, uint64_t *rng) {
  int i;
  for (i = n - 1; i > 0; --i) {
    int j = (int)(rng_next(rng) % (uint64_t)(i + 1));
    int t = a[i];
    a[i] = a[j];
    a[j] = t;
  }
}

static float lr_at(const TrainConfig *c, int64_t ticks) {
  double p;
  double lr;
  if (!c || c->lr_decay_ticks <= 0)
    return c ? c->lr : 0.f;
  p = (double)ticks / (double)c->lr_decay_ticks;
  if (p > 1.0)
    p = 1.0;
  if (c->lr <= 0.f)
    return c->lr_floor;
  lr = (double)c->lr *
       (1.0 - p * (1.0 - (double)c->lr_floor / (double)c->lr));
  if (lr < (double)c->lr_floor)
    lr = (double)c->lr_floor;
  return (float)lr;
}

static double wall_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int parse_seed_list(const char *s, int *out, int cap) {
  const char *p;
  int n = 0;
  if (!s || !s[0] || !strcmp(s, "fixture"))
    return 0;
  p = s;
  while (*p) {
    char *end = NULL;
    long v;
    while (*p == ',' || *p == ' ' || *p == '\t')
      ++p;
    if (!*p)
      break;
    errno = 0;
    v = strtol(p, &end, 10);
    if (errno || end == p || v < 0 || v > 2147483647L)
      return -1;
    if (n >= cap)
      return -1;
    out[n++] = (int)v;
    p = end;
    if (*p && *p != ',' && *p != ' ' && *p != '\t')
      return -1;
  }
  return n;
}

static void normalize_adv(float *adv, const unsigned char *valid, int n,
                          int use_valid) {
  int i;
  int c = 0;
  double s = 0.0, s2 = 0.0;
  float mean, std;
  for (i = 0; i < n; ++i) {
    if (use_valid && !valid[i])
      continue;
    s += (double)adv[i];
    s2 += (double)adv[i] * (double)adv[i];
    c++;
  }
  if (c < 2)
    return;
  mean = (float)(s / (double)c);
  std = (float)sqrt(s2 / (double)c - (double)mean * (double)mean);
  std += 1e-8f;
  for (i = 0; i < n; ++i) {
    if (use_valid && !valid[i]) {
      adv[i] = 0.f;
      continue;
    }
    adv[i] = (adv[i] - mean) / std;
  }
}

static int nn_copy_weights(Nn *src, Nn *dst, const char *path) {
  if (!src || !dst || !path)
    return -1;
  if (src == dst)
    return 0;
  if (mkdir_parents_for_file(path) != 0)
    return -1;
  if (rl_ckpt_save(src, path) != 0)
    return -1;
  if (rl_ckpt_load(dst, path) != 0)
    return -1;
  return 0;
}

/*
 * Compile-time gate: any malloc/calloc/realloc/free in this function fails
 * the build (undeclared TR_NO_ALLOC_IN_ROLLOUT).
 *
 * First stored step after a reset is a noop burn-in (invalid for PPO).
 * Terminal / horizon end resets that lane in-place.
 */
static int run_rollout(Nn *nn, void *env, struct EnvStepCtx *estep,
                       const TrainConfig *cfg, BlazeFns *fns, CrState *cr,
                       CrCurr *curr, int use_curr, int n, int T, int chunk,
                       int64_t *cap_last, uint8_t *t0_trail, int *t0_n,
                       int *t0_i, int *n_eps, struct RolloutBuf *b,
                       double *ms_pack, double *ms_nn, double *ms_env,
                       double *ms_host) {
#define malloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define calloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define realloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define free(...) TR_NO_ALLOC_IN_ROLLOUT
  int t, i;
  int nseeds = b->nseeds;
  int any_reset, any_trunc;
  double t0, t1;
  double acc_pack = 0.0, acc_nn = 0.0, acc_env = 0.0, acc_host = 0.0;

  if (ms_pack)
    *ms_pack = 0.0;
  if (ms_nn)
    *ms_nn = 0.0;
  if (ms_env)
    *ms_env = 0.0;
  if (ms_host)
    *ms_host = 0.0;

  for (t = 0; t < T; ++t) {
    size_t off = (size_t)t * (size_t)n;

    t0 = wall_sec();
    pack_obs(b->cam, b->depth, b->edge, b->scal6, b->pose, b->status,
             b->ep_dec, cfg->ep_dec, b->have_prior, b->prior_frame, n,
             b->planes_cur, b->scal_cur, b->frame_scratch);
    t1 = wall_sec();
    acc_pack += t1 - t0;

    t0 = t1;
    if (nn_forward(nn, b->planes_cur, b->scal_cur, n, b->logits, b->val_cur) !=
        0)
      return -1;
    if (nn_sample(nn, b->logits, n, NN_SAMPLE_GUMBEL, b->acts_cur, b->logp_cur,
                  NULL) != 0)
      return -2;
    t1 = wall_sec();
    acc_nn += t1 - t0;

    t0 = t1;
    for (i = 0; i < n; ++i) {
      if (b->burnin[i]) {
        int32_t *a = b->acts_cur + (size_t)i * POL_HEADS;
        memset(a, 0, POL_HEADS * sizeof(int32_t));
        a[0] = 1;
        a[1] = 1;
        a[2] = 1;
      }
      b->valid_roll[off + (size_t)i] = (unsigned char)(b->burnin[i] ? 0 : 1);
    }

    memcpy(b->planes_roll + off * ENV_N_CH * ENV_NPIX, b->planes_cur,
           (size_t)n * ENV_N_CH * ENV_NPIX);
    memcpy(b->scal_roll + off * POL_SCAL, b->scal_cur,
           (size_t)n * POL_SCAL * sizeof(float));
    memcpy(b->acts_roll + off * POL_HEADS, b->acts_cur,
           (size_t)n * POL_HEADS * sizeof(int32_t));
    memcpy(b->logp_roll + off, b->logp_cur, (size_t)n * sizeof(float));
    memcpy(b->val_roll + off, b->val_cur, (size_t)n * sizeof(float));
    acc_host += wall_sec() - t0;

    t0 = wall_sec();
    acts_to_rows(b->acts_cur, n, b->act_rows);
    if (env_step(estep, env, b->act_rows, cfg->action_repeat, b->cam, b->depth,
                 b->edge, b->scal6, b->rew, b->done_buf, b->pose,
                 b->status) != 0)
      return -3;
    t1 = wall_sec();
    acc_env += t1 - t0;
    t0 = t1;

    /* After assign+reset, burn-in is the first step into the new snap.
     * Seed best[] from that inventory so first-time bonuses are not re-paid. */
    for (i = 0; i < n; ++i) {
      if (b->burnin[i])
        cr_seed_lane(cr, i, b->status + (size_t)i * ENV_STATUS);
    }

    cr_step(cr, b->status, b->cam, b->acts_cur, b->pose, b->scal6, b->done_buf,
            b->lane_seed, b->logs, nseeds, b->lmax, b->rew);

    memcpy(b->rew_roll + off, b->rew, (size_t)n * sizeof(float));
    memcpy(b->done_roll + off, b->done_buf, (size_t)n);

    any_reset = 0;
    any_trunc = 0;
    memset(b->reset_mask, 0, (size_t)n);
    for (i = 0; i < n; ++i) {
      unsigned char term = b->done_buf[i] == BLAZE_DONE_SUCCESS ||
                           b->done_buf[i] == BLAZE_DONE_DEATH;
      unsigned char success = b->done_buf[i] == BLAZE_DONE_SUCCESS;
      unsigned char trunc;
      unsigned char ended;
      if (!b->burnin[i])
        b->ep_dec[i] += 1;
      trunc = (unsigned char)(!term &&
          (b->done_buf[i] == BLAZE_DONE_BOUNDARY || b->ep_dec[i] >= cfg->ep_dec));
      ended = (unsigned char)(term || trunc);
      b->term_roll[off + (size_t)i] = term;
      b->cut_roll[off + (size_t)i] = ended;
      b->cut_val_roll[off + (size_t)i] = 0.f;
      if (trunc) any_trunc = 1;

      /* Torch stack_roll: a burn-in step writes [s',s'] (post-step
       * duplicated). Do not promote have_prior so the next pack copies
       * current cam into both slots. */
      if (!b->burnin[i]) {
        uint8_t *frame = b->frame_scratch + (size_t)i * ENV_N_PLANES * ENV_NPIX;
        uint8_t *prior = b->prior_frame + (size_t)i * ENV_N_PLANES * ENV_NPIX;
        memcpy(prior, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
        b->have_prior[i] = 1;
      }
      b->burnin[i] = 0;

      if (use_curr && curr) {
        int stg_now = cr_stage_of_best(cr->best + (size_t)i * 9);
        int si = b->lane_seed[i];
        int stg;
        if (!ended) {
          for (stg = 1; stg < CR_N_STAGES; ++stg) {
            if (stg_now != stg || b->lane_stage[i] >= stg)
              continue;
            if (stg == CR_N_STAGES - 1 &&
                b->status[(size_t)i * ENV_STATUS + CR_IX_STICK] < 1)
              continue;
            if (chunk - (int)cap_last[(size_t)si * CR_N_STAGES + stg] >=
                cfg->cap_refresh) {
              if (fns->capture(env, i, cap_slot(nseeds, si, stg)) != 0)
                return -5;
              printf("ppo: capture lane=%d si=%d stg=%d slot=%d chunk=%d\n",
                     i, si, stg, cap_slot(nseeds, si, stg), chunk);
              fflush(stdout);
              cap_last[(size_t)si * CR_N_STAGES + stg] = chunk;
              curr->avail[si * CR_N_STAGES + stg] = 1;
            }
            if (stg > b->lane_stage[i])
              b->lane_stage[i] = stg;
          }
        }
      }

      if (ended) {
        int si = b->lane_seed[i];
        int st0 = b->lane_stage_start[i];
        int stg_now = cr_stage_of_best(cr->best + (size_t)i * 9);
        int reached = success ? CR_N_STAGES : stg_now;
        int nxt = st0 + 1;
        int ok = (st0 >= CR_N_STAGES - 1) ? (int)success : (reached >= nxt);
        if (use_curr && curr)
          cr_curr_record(curr, si, st0, ok);
        if (st0 == 0 && t0_trail && t0_n && t0_i) {
          t0_trail[*t0_i] = success;
          *t0_i = (*t0_i + 1) % T0_TRAIL;
          if (*t0_n < T0_TRAIL)
            *t0_n += 1;
        }
        if (n_eps)
          *n_eps += 1;
        b->reset_mask[i] = 1;
        any_reset = 1;
      }
    }

    if (any_trunc) {
      /* Evaluate the final pre-reset observation with its actual frame
       * stack and episode clock. Do this for time limits and world bounds;
       * the next rollout row is a different episode after masked reset. */
      acc_host += wall_sec() - t0;
      t0 = wall_sec();
      pack_obs(b->cam, b->depth, b->edge, b->scal6, b->pose, b->status,
               b->ep_dec, cfg->ep_dec, b->have_prior, b->prior_frame, n,
               b->planes_cur, b->scal_cur, b->frame_scratch);
      acc_pack += wall_sec() - t0;
      t0 = wall_sec();
      if (nn_forward(nn, b->planes_cur, b->scal_cur, n, b->logits, b->val_cur) != 0)
        return -1;
      acc_nn += wall_sec() - t0;
      t0 = wall_sec();
      for (i = 0; i < n; ++i)
        if (b->cut_roll[off + (size_t)i] && !b->term_roll[off + (size_t)i])
          b->cut_val_roll[off + (size_t)i] = b->val_cur[i];
    }

    if (any_reset) {
      int nend = 0;
      int seeds_n[MAX_SEEDS * 8];
      int stages_n[MAX_SEEDS * 8];
      int idx[MAX_SEEDS * 8];
      int k;
      if (n > MAX_SEEDS * 8) {
        /* fallback: sequential sample one by one */
        for (i = 0; i < n; ++i) {
          if (!b->reset_mask[i])
            continue;
          if (use_curr && curr) {
            cr_curr_sample(curr, 1, &b->lane_seed[i], &b->lane_stage_start[i]);
            b->lane_stage[i] = b->lane_stage_start[i];
            b->lane_snap[i] =
                cap_slot(nseeds, b->lane_seed[i], b->lane_stage_start[i]);
            if (b->lane_stage_start[i] > 0)
              printf("ppo: sample lane=%d si=%d stage=%d\n", i, b->lane_seed[i],
                     b->lane_stage_start[i]);
          }
          cr_reset_lane(cr, i);
          b->burnin[i] = 1;
          b->have_prior[i] = 0;
          b->ep_dec[i] = 0;
        }
        if (fns->assign(env, b->lane_snap) != 0)
          return -6;
        if (fns->reset(env, b->reset_mask) != 0)
          return -6;
      } else {
        for (i = 0; i < n; ++i) {
          if (!b->reset_mask[i])
            continue;
          idx[nend++] = i;
        }
        if (use_curr && curr && nend > 0)
          cr_curr_sample(curr, nend, seeds_n, stages_n);
        for (k = 0; k < nend; ++k) {
          i = idx[k];
          if (use_curr && curr) {
            b->lane_seed[i] = seeds_n[k];
            b->lane_stage_start[i] = stages_n[k];
            b->lane_stage[i] = stages_n[k];
            b->lane_snap[i] = cap_slot(nseeds, seeds_n[k], stages_n[k]);
            if (stages_n[k] > 0)
              printf("ppo: sample lane=%d si=%d stage=%d\n", i, seeds_n[k],
                     stages_n[k]);
          }
          cr_reset_lane(cr, i);
          b->burnin[i] = 1;
          b->have_prior[i] = 0;
          b->ep_dec[i] = 0;
        }
        if (fns->assign(env, b->lane_snap) != 0)
          return -6;
        if (fns->reset(env, b->reset_mask) != 0)
          return -6;
      }
    }
    acc_host += wall_sec() - t0;
  }

  /* Bootstrap value at the post-chunk observation. */
  t0 = wall_sec();
  pack_obs(b->cam, b->depth, b->edge, b->scal6, b->pose, b->status, b->ep_dec,
           cfg->ep_dec, b->have_prior, b->prior_frame, n, b->planes_cur,
           b->scal_cur, b->frame_scratch);
  acc_pack += wall_sec() - t0;
  t0 = wall_sec();
  if (nn_forward(nn, b->planes_cur, b->scal_cur, n, b->logits, b->next_val) !=
      0)
    return -1;
  acc_nn += wall_sec() - t0;
  if (ms_pack)
    *ms_pack = acc_pack * 1000.0;
  if (ms_nn)
    *ms_nn = acc_nn * 1000.0;
  if (ms_env)
    *ms_env = acc_env * 1000.0;
  if (ms_host)
    *ms_host = acc_host * 1000.0;
  return 0;
#undef malloc
#undef calloc
#undef realloc
#undef free
}

#if BLAZE_RL_HAVE_METAL
static const char *resolve_metallib(const TrainConfig *cfg) {
  if (!cfg || !cfg->metallib[0])
    return NULL;
  if (strcmp(cfg->metallib, "auto") == 0)
    return ENV_METAL_OBS_DEFAULT_METALLIB;
  return cfg->metallib;
}
#endif

/* Compact valid PPO rows left in place. dst <= i; skip memcpy when dst == i. */
static int compact_valid_rows(struct RolloutBuf *b, int batch) {
  int i, dst = 0;
  for (i = 0; i < batch; ++i) {
    if (!b->valid_roll[i])
      continue;
    if (dst != i) {
      memcpy(b->planes_roll + (size_t)dst * ENV_N_CH * ENV_NPIX,
             b->planes_roll + (size_t)i * ENV_N_CH * ENV_NPIX,
             (size_t)ENV_N_CH * ENV_NPIX);
      memcpy(b->scal_roll + (size_t)dst * POL_SCAL,
             b->scal_roll + (size_t)i * POL_SCAL,
             (size_t)POL_SCAL * sizeof(float));
      memcpy(b->acts_roll + (size_t)dst * POL_HEADS,
             b->acts_roll + (size_t)i * POL_HEADS,
             (size_t)POL_HEADS * sizeof(int32_t));
      b->logp_roll[dst] = b->logp_roll[i];
      b->adv_roll[dst] = b->adv_roll[i];
      b->ret_roll[dst] = b->ret_roll[i];
    }
    dst++;
  }
  return dst;
}

static void gather_mb(struct RolloutBuf *b, const int *perm, int k, int nmb) {
  int j;
  for (j = 0; j < nmb; ++j) {
    int src = perm[k + j];
    memcpy(b->planes_mb + (size_t)j * ENV_N_CH * ENV_NPIX,
           b->planes_roll + (size_t)src * ENV_N_CH * ENV_NPIX,
           (size_t)ENV_N_CH * ENV_NPIX);
    memcpy(b->scal_mb + (size_t)j * POL_SCAL,
           b->scal_roll + (size_t)src * POL_SCAL,
           (size_t)POL_SCAL * sizeof(float));
    memcpy(b->acts_mb + (size_t)j * POL_HEADS,
           b->acts_roll + (size_t)src * POL_HEADS,
           (size_t)POL_HEADS * sizeof(int32_t));
    b->logp_mb[j] = b->logp_roll[src];
    b->adv_mb[j] = b->adv_roll[src];
    b->ret_mb[j] = b->ret_roll[src];
  }
}

static int ppo_updates(Nn *nn_upd, struct RolloutBuf *b, const TrainConfig *cfg,
                       int n, int T, int batch, int is_metal, int drop_tail,
                       int tail_overlap, uint64_t *rng, NnUpdateStats *stats,
                       int *n_skip) {
  int mb = cfg->mb;
  int ep, rc;
  int n_tr;
  int i;
  int n_upd = 0;
  double ent_sum = 0.0, kl_sum = 0.0, clip_sum = 0.0;
  NnUpdateStats st;

  (void)n;
  (void)T;
  normalize_adv(b->adv_roll, b->valid_roll, batch, 1);

#define TR_ACCUM_UPD_STATS()                                                   \
  do {                                                                         \
    ent_sum += (double)st.entropy_mean;                                        \
    kl_sum += (double)st.approx_kl;                                            \
    clip_sum += (double)st.clipfrac;                                           \
    n_upd++;                                                                   \
    *stats = st;                                                               \
  } while (0)

  /* Metal n must equal create max_n. mb==0 keeps the full [batch] plane. */
  if (is_metal && mb <= 0) {
    for (i = 0; i < batch; ++i) {
      if (!b->valid_roll[i]) {
        b->adv_roll[i] = 0.f;
        b->ret_roll[i] = b->val_roll[i];
      }
    }
    for (ep = 0; ep < cfg->epochs; ++ep) {
      memset(&st, 0, sizeof(st));
      rc = nn_update(nn_upd, b->planes_roll, b->scal_roll, b->acts_roll,
                     b->logp_roll, b->adv_roll, b->ret_roll, batch, &st);
      if (rc != 0)
        return rc;
      TR_ACCUM_UPD_STATS();
    }
    goto done;
  }

  n_tr = compact_valid_rows(b, batch);
  if (n_tr <= 0)
    return 0;

  if (mb <= 0) {
    for (ep = 0; ep < cfg->epochs; ++ep) {
      memset(&st, 0, sizeof(st));
      rc = nn_update(nn_upd, b->planes_roll, b->scal_roll, b->acts_roll,
                     b->logp_roll, b->adv_roll, b->ret_roll, n_tr, &st);
      if (rc != 0)
        return rc;
      TR_ACCUM_UPD_STATS();
    }
    goto done;
  }

  /* Metal and a sealed CUDA net run every update at n=mb: Metal fixes n at
   * create, CUDA has buckets only for n_envs and mb. The tail (n_tr mod mb)
   * is then either dropped (tail_mb=drop) or, by default, trained once more
   * as the last mb rows of the permutation (tail_mb=overlap: every row is
   * seen, mb minus tail rows twice). Dropping it cost the 2026-09-03 wood
   * probe half its t0: at mb=8192 and n_tr about 32.7k a quarter of each
   * chunk never reached the optimizer.
   * n_tr < mb has no full minibatch. An update at n_tr cannot run: Metal
   * fixes n at create, and a sealed CUDA net has no bucket for n_tr. So the
   * whole chunk skips its update. n_tr does not change over the epochs, so
   * this reports once per chunk. */
  if ((is_metal || drop_tail) && n_tr < mb) {
    (*n_skip)++;
    printf("ppo: update skipped n_tr=%d < mb=%d\n", n_tr, mb);
    fflush(stdout);
    goto done;
  }

  for (i = 0; i < n_tr; ++i)
    b->perm[i] = i;

  for (ep = 0; ep < cfg->epochs; ++ep) {
    int k;
    shuffle_int(b->perm, n_tr, rng);
    if (is_metal || drop_tail) {
      for (k = 0; k + mb <= n_tr; k += mb) {
        gather_mb(b, b->perm, k, mb);
        memset(&st, 0, sizeof(st));
        rc = nn_update(nn_upd, b->planes_mb, b->scal_mb, b->acts_mb, b->logp_mb,
                       b->adv_mb, b->ret_mb, mb, &st);
        if (rc != 0)
          return rc;
        TR_ACCUM_UPD_STATS();
      }
      if (tail_overlap && k < n_tr) {
        gather_mb(b, b->perm, n_tr - mb, mb);
        memset(&st, 0, sizeof(st));
        rc = nn_update(nn_upd, b->planes_mb, b->scal_mb, b->acts_mb, b->logp_mb,
                       b->adv_mb, b->ret_mb, mb, &st);
        if (rc != 0)
          return rc;
        TR_ACCUM_UPD_STATS();
      }
    } else {
      for (k = 0; k < n_tr; k += mb) {
        int nmb = mb;
        if (nmb > n_tr - k)
          nmb = n_tr - k;
        gather_mb(b, b->perm, k, nmb);
        memset(&st, 0, sizeof(st));
        rc = nn_update(nn_upd, b->planes_mb, b->scal_mb, b->acts_mb, b->logp_mb,
                       b->adv_mb, b->ret_mb, nmb, &st);
        if (rc != 0)
          return rc;
        TR_ACCUM_UPD_STATS();
      }
    }
  }

done:
  if (n_upd > 0) {
    stats->entropy_mean = (float)(ent_sum / (double)n_upd);
    stats->approx_kl = (float)(kl_sum / (double)n_upd);
    stats->clipfrac = (float)(clip_sum / (double)n_upd);
  }
  return 0;
#undef TR_ACCUM_UPD_STATS
}

int main(int argc, char **argv) {
  TrainConfig cfg;
  int prc = tr_cfg_parse_argv(&cfg, argc, argv);
  void *env = NULL;
  Nn *nn_roll = NULL;
  Nn *nn_upd = NULL;
  NnCreate nd;
  NnConfig nc;
  NnUpdateStats stats;
  BlazeCreateOpts opts;
  BlazeFns fns;
#if BLAZE_RL_HAVE_CUDA
  EnvCudaStage stage;
#endif
#if BLAZE_RL_HAVE_METAL
  EnvMetalObs metal_obs;
#endif
  struct EnvStepCtx estep;
  CrState cr;
  CrCurr curr;
  char err[512];
  const char *so_path = NULL;
  char path_store[MAX_SNAPS][TR_CFG_STR_MAX];
  const char *paths[MAX_SNAPS];
  WorldRecipe world_recipe;
  int seed_ids[MAX_SEEDS];
  float log_tmp[LOG_CAP * 3];
  struct RolloutBuf b;
  int n, T, batch, i, rc;
  int is_cuda = 0;
  int is_metal = 0;
  int want_cam = 0;
  int nseeds = 0;
  int use_curr = 0;
  int nsnaps = 0;
  int lmax = 1;
  int chunk;
  int64_t ticks = 0;
  int64_t next_ckpt;
  int n_eps = 0;
  uint8_t t0_trail[T0_TRAIL];
  int t0_n = 0, t0_i = 0;
  int64_t cap_last[MAX_SEEDS * CR_N_STAGES];
  uint64_t rng;
  double t0_wall;
  int split_nn = 0;
  int drop_tail = 0;
  int n_upd_skip = 0;
  int upd_n;
  float best_t0 = -1.f;
  int64_t best_ticks = 0;
  char best_bin[TR_CFG_STR_MAX + 16];

  memset(&b, 0, sizeof(b));
  memset(&fns, 0, sizeof(fns));
  memset(&estep, 0, sizeof(estep));
  memset(&cr, 0, sizeof(cr));
  memset(&curr, 0, sizeof(curr));
  memset(t0_trail, 0, sizeof(t0_trail));
  memset(cap_last, 0, sizeof(cap_last));
  best_bin[0] = '\0';
  for (i = 0; i < MAX_SEEDS * CR_N_STAGES; ++i)
    cap_last[i] = (int64_t)-1000000000;
#if BLAZE_RL_HAVE_CUDA
  memset(&stage, 0, sizeof(stage));
#endif
#if BLAZE_RL_HAVE_METAL
  memset(&metal_obs, 0, sizeof(metal_obs));
#endif

  if (prc == 1)
    return 0;
  if (prc != 0)
    return 2;

  if (strcmp(cfg.backend, "cpu") == 0) {
    is_cuda = 0;
    is_metal = 0;
    so_path = kEnvCpuSo;
  } else if (strcmp(cfg.backend, "cuda") == 0) {
#if BLAZE_RL_HAVE_CUDA
    is_cuda = 1;
    is_metal = 0;
    so_path = kEnvCudaSo;
#else
    fprintf(stderr,
            "ppo: backend 'cuda' is not available in this binary "
            "(no fallback)\n");
    return 1;
#endif
  } else if (strcmp(cfg.backend, "metal") == 0) {
#if BLAZE_RL_HAVE_METAL
    is_cuda = 0;
    is_metal = 1;
    so_path = kEnvCpuSo;
    want_cam = 1;
#else
    fprintf(stderr,
            "ppo: backend 'metal' is not available in this binary "
            "(no fallback)\n");
    return 1;
#endif
  } else {
    fprintf(stderr,
            "ppo: backend '%s' is not available in this binary "
            "(no fallback)\n",
            cfg.backend);
    return 1;
  }

  n = cfg.n_envs;
  T = cfg.rollout_steps;
  if (n <= 0 || T <= 0)
    die("n_envs and rollout_steps must be positive");
  if ((int64_t)n > (int64_t)INT_MAX / (int64_t)T) {
    fprintf(stderr, "ppo: n_envs (%d) * rollout_steps (%d) overflows int\n", n,
            T);
    return 1;
  }
  batch = n * T;
  rng = cfg.seed ? cfg.seed : 1;
  next_ckpt = cfg.ckpt_ticks;
  if (cfg.max_ticks > 0 &&
      (int64_t)cfg.max_chunks * (int64_t)n * (int64_t)T *
              (int64_t)cfg.action_repeat <
          cfg.max_ticks)
    fprintf(stderr,
            "ppo: warning: max_chunks=%d stops first (before max_ticks=%lld)\n",
            cfg.max_chunks, (long long)cfg.max_ticks);

  nseeds = parse_seed_list(cfg.train_seeds, seed_ids, MAX_SEEDS);
  if (nseeds < 0)
    die("train_seeds must be 'fixture' or a comma list of seed ids");
  use_curr = nseeds > 0;
  if (!use_curr) {
    nseeds = 1;
    seed_ids[0] = -1;
    paths[0] = cfg.fixture;
    nsnaps = 1;
  } else {
    if (nseeds * CR_N_STAGES > 128)
      die("too many train_seeds for snapshot slots");
    for (i = 0; i < nseeds; ++i) {
      int nw = snprintf(path_store[i], TR_CFG_STR_MAX, "%s/s%d_t0.bsnp",
                        cfg.snaps_dir, seed_ids[i]);
      if (nw < 0 || nw >= TR_CFG_STR_MAX)
        die("snaps path too long");
      paths[i] = path_store[i];
    }
    nsnaps = nseeds;
    if (cfg.stage_snaps) {
      int si, stg;
      nsnaps = nseeds + nseeds * (CR_N_STAGES - 1);
      for (si = 0; si < nseeds; ++si) {
        for (stg = 1; stg < CR_N_STAGES; ++stg) {
          char stg_path[TR_CFG_STR_MAX];
          int slot = cap_slot(nseeds, si, stg);
          int nw = snprintf(stg_path, sizeof(stg_path), "%s/s%d_stg%d.bsnp",
                            cfg.snaps_dir, seed_ids[si], stg);
          if (nw < 0 || nw >= (int)sizeof(stg_path))
            die("snaps path too long");
          if (path_is_reg(stg_path)) {
            memcpy(path_store[slot], stg_path, (size_t)nw + 1);
            paths[slot] = path_store[slot];
          } else {
            paths[slot] = paths[si];
          }
        }
      }
    }
  }

  /* Resolve the recipe before policy/GPU allocation. Both the environment
   * and the curriculum's log-target scan consume these exact same inputs. */
  if (world_recipe_prepare(&world_recipe, paths, nsnaps, cfg.world_size,
                           err, sizeof err) != 0) {
    fprintf(stderr, "ppo: world preparation failed: %s\n", err);
    return 1;
  }
  for (i = 0; i < nsnaps; ++i) paths[i] = world_recipe.paths[i];
  if (world_recipe.directory[0]) {
    char recipe_path[WORLD_RECIPE_PATH];
    int nw = snprintf(recipe_path, sizeof recipe_path, "%s/recipe.conf",
                      world_recipe.directory);
    if (nw < 0 || nw >= (int)sizeof recipe_path) die("world recipe path too long");
    FILE *recipe_file = fopen(recipe_path, "w");
    if (!recipe_file) dief("cannot write effective recipe: %s", recipe_path);
    tr_cfg_dump(&cfg, recipe_file);
    int bad = ferror(recipe_file);
    if (fclose(recipe_file) != 0) bad = 1;
    if (bad) dief("effective recipe write failed: %s", recipe_path);
  }

  /* Build the policy BEFORE the env. blaze_create raises the thread stack
   * limit and allocates the region pools; the cuDNN engine race at create
   * would otherwise run on a card that already holds all of that. nn_create
   * needs only the config and the batch sizes, so nothing here depends on the
   * env. It binds its own CUDA device. */
  nc = nn_config_default();
  nc.lr = cfg.lr;
  nc.ppo_clip = cfg.ppo_clip;
  nc.value_coef = cfg.value_coef;
  nc.entropy_coef = cfg.entropy_coef;
  nc.grad_limit = cfg.grad_limit;
  nc.rng_seed = cfg.seed;

  upd_n = (cfg.mb > 0) ? cfg.mb : batch;
  nd.backend = is_metal   ? NN_BACKEND_METAL
               : is_cuda  ? NN_BACKEND_CUDA
                          : NN_BACKEND_CPU;
  nd.device = cfg.device;
  nd.config = nc;
  nd.prec = (!strcmp(cfg.nn_prec, "f32")) ? NN_PREC_F32 : NN_PREC_FAST;
  if (is_metal) {
    nd.max_n = n;
    nn_roll = nn_create(&nd);
    if (!nn_roll)
      dief("nn_create: %s", nn_last_error());
    if (upd_n != n) {
      split_nn = 1;
      if (mkdir_parents_for_file(cfg.checkpoint) != 0)
        dief("mkdir parents for checkpoint failed: %s", cfg.checkpoint);
      if (rl_ckpt_save(nn_roll, cfg.checkpoint) != 0)
        dief("nn_save (init): %s", nn_last_error());
      nd.max_n = upd_n;
      nn_upd = nn_create(&nd);
      if (!nn_upd)
        dief("nn_create (upd): %s", nn_last_error());
      if (rl_ckpt_load(nn_upd, cfg.checkpoint) != 0)
        dief("nn_load (init upd): %s", nn_last_error());
    } else {
      nn_upd = nn_roll;
    }
  } else {
    /* Size for what the run passes, not for batch: forward always gets n
     * (n_envs) and update never gets more than upd_n. The batch rows live in
     * the host rollout store and reach the net one minibatch at a time. */
    nd.max_n = n > upd_n ? n : upd_n;
    nn_roll = nn_create(&nd);
    if (!nn_roll)
      dief("nn_create: %s", nn_last_error());
    nn_upd = nn_roll;
    /* create already prepared max_n. Prepare the only other batch size the
     * run uses, then seal, so no plan is built after this point. The last
     * prepare shrinks the workspace arena over both buckets. */
    if (n != nd.max_n && nn_prepare_n(nn_roll, n) != 0)
      dief("nn_prepare_n (rollout n): %s", nn_last_error());
    if (upd_n != nd.max_n && upd_n != n && nn_prepare_n(nn_roll, upd_n) != 0)
      dief("nn_prepare_n (update n): %s", nn_last_error());
    /* Drop the tail minibatch, and seal, only when mb <= batch - n_envs.
     * Every reset in the chunk sets burn-in and each burn-in invalidates one
     * more step per env, so n_tr can be batch - k*n_envs for k resets. The
     * gate only guarantees one full minibatch when each env resets at most
     * once in the chunk. With more resets n_tr can drop below mb; that chunk
     * then skips its update and ppo_updates logs the skip. The gate still
     * stops mb == batch (or close) from dropping every row every chunk. */
    drop_tail = is_cuda && cfg.mb > 0 && cfg.mb <= batch - n &&
                strcmp(cfg.tail_mb, "partial") != 0;
    if (drop_tail) {
      if (nn_seal(nn_roll) != 0)
        dief("nn_seal: %s", nn_last_error());
      printf("ppo: nn buckets fwd_n=%d upd_n=%d max_n=%d sealed=1\n", n, upd_n,
             nd.max_n);
    } else {
      /* mb==0 updates the compacted valid rows and mb>batch-n_envs may too,
       * so update n varies with burn-in. Cannot seal. */
      printf("ppo: nn buckets fwd_n=%d upd_n<=%d max_n=%d sealed=0 "
             "(mb=%d batch=%d)\n",
             n, upd_n, nd.max_n, cfg.mb, batch);
    }
    fflush(stdout);
  }

  if (cfg.init_from[0]) {
    if (rl_ckpt_load(nn_roll, cfg.init_from) != 0) {
      fprintf(stderr, "ppo: init_from load failed: %s (%s)\n", cfg.init_from,
              nn_last_error());
      exit(1);
    }
    if (split_nn && rl_ckpt_load(nn_upd, cfg.init_from) != 0) {
      fprintf(stderr, "ppo: init_from load failed: %s (%s)\n", cfg.init_from,
              nn_last_error());
      exit(1);
    }
    printf("ppo: init_from=%s\n", cfg.init_from);
    fflush(stdout);
  }

  if (blaze_fns_load(&fns, so_path, want_cam) != 0) {
    fprintf(stderr,
            "ppo: failed to load env library for backend=%s (path=%s); "
            "no fallback\n",
            cfg.backend, so_path);
    return 1;
  }

  blaze_create_opts_default(&opts);
  opts.ktime = cfg.ktime;
  opts.stage_time = cfg.stage_time;
  opts.legacy_recenter = cfg.legacy_recenter;
  opts.warp_tick = cfg.warp_tick;
  opts.op_trace = cfg.op_trace;
  opts.no_ore_xy = cfg.no_ore_xy;
  opts.stack_kib = cfg.stack_kib;

  env = fns.create(cfg.device, n, &opts);
  if (!env)
    die("blaze_create failed");
  if (fns.set_success_item(env, cfg.success_item) != 0)
    die("blaze_set_success_item failed");

#if BLAZE_RL_HAVE_CUDA
  if (is_cuda) {
    if (env_cuda_stage_create(&stage, n, cfg.device) != 0) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      die("env_cuda_stage_create failed");
    }
  }
#endif

#if BLAZE_RL_HAVE_METAL
  if (is_metal) {
    const char *mlib = resolve_metallib(&cfg);
    int observation_cells = cfg.metal_max_cells;
    if (world_recipe.max_cells > 0 && world_recipe.max_cells < observation_cells)
      observation_cells = world_recipe.max_cells;
    if (!mlib || !mlib[0]) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      die("metallib path empty");
    }
    if (env_metal_obs_create(&metal_obs, n, observation_cells, mlib,
                             fns.obs_cam_inputs) != 0) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      fprintf(stderr, "ppo: Metal observation create failed; no fallback\n");
      return 1;
    }
    printf("world: Metal observation capacity=%d cells per environment\n", observation_cells);
  }
#endif

  estep.fns = &fns;
  estep.is_cuda = is_cuda;
  estep.is_metal = is_metal;
#if BLAZE_RL_HAVE_CUDA
  estep.stage = is_cuda ? &stage : NULL;
#endif
#if BLAZE_RL_HAVE_METAL
  estep.metal = is_metal ? &metal_obs : NULL;
#endif

  if (fns.load_snapshots(env, paths, nsnaps, err, (int)sizeof(err)) < 0) {
    fprintf(stderr, "ppo: blaze_load_snapshots: %s\n", err);
    goto fail_env;
  }

  /* Log oracle per loaded t0 snap. */
  for (i = 0; i < nseeds; ++i) {
    int got = 0;
    int lrc = cr_logs_from_bsnp(paths[i], log_tmp, LOG_CAP, &got);
    if (lrc != 0)
      dief("cr_logs_from_bsnp failed: %s", paths[i]);
    if (got > lmax)
      lmax = got;
  }
  if (lmax < 1)
    lmax = 1;
  b.logs = (float *)malloc((size_t)nseeds * (size_t)lmax * 3u * sizeof(float));
  if (!b.logs)
    die("alloc logs");
  {
    int s;
    for (s = 0; s < nseeds * lmax * 3; ++s)
      b.logs[s] = 1e9f;
  }
  for (i = 0; i < nseeds; ++i) {
    int got = 0;
    int lrc = cr_logs_from_bsnp(paths[i], log_tmp, LOG_CAP, &got);
    if (lrc != 0)
      dief("cr_logs_from_bsnp failed: %s", paths[i]);
    if (got > 0)
      memcpy(b.logs + (size_t)i * (size_t)lmax * 3u, log_tmp,
             (size_t)got * 3u * sizeof(float));
  }
  b.nseeds = nseeds;
  b.lmax = lmax;

  b.assign = (int *)calloc((size_t)n, sizeof(int));
  b.cam = (unsigned short *)calloc((size_t)n * ENV_NPIX, sizeof(*b.cam));
  b.depth = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  b.edge = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  b.scal6 = (float *)calloc((size_t)n * ENV_SCAL, sizeof(float));
  b.rew = (float *)malloc((size_t)n * sizeof(float));
  b.done_buf = (unsigned char *)malloc((size_t)n);
  b.pose = (float *)calloc((size_t)n * ENV_POSE, sizeof(float));
  b.status = (int *)calloc((size_t)n * ENV_STATUS, sizeof(int));
  b.act_rows = (double *)calloc((size_t)n * ENV_ACT, sizeof(double));
  b.planes_cur = (uint8_t *)calloc((size_t)n * ENV_N_CH * ENV_NPIX, 1);
  b.scal_cur = (float *)calloc((size_t)n * POL_SCAL, sizeof(float));
  b.acts_cur = (int32_t *)calloc((size_t)n * POL_HEADS, sizeof(int32_t));
  b.logp_cur = (float *)calloc((size_t)n, sizeof(float));
  b.val_cur = (float *)calloc((size_t)n, sizeof(float));
  b.next_val = (float *)calloc((size_t)n, sizeof(float));
  b.cut_val_roll = (float *)calloc((size_t)batch, sizeof(float));
  b.logits = (float *)calloc((size_t)n * NN_N_LOGITS, sizeof(float));
  b.planes_roll =
      (uint8_t *)malloc((size_t)batch * ENV_N_CH * ENV_NPIX * sizeof(uint8_t));
  b.scal_roll = (float *)malloc((size_t)batch * POL_SCAL * sizeof(float));
  b.acts_roll = (int32_t *)malloc((size_t)batch * POL_HEADS * sizeof(int32_t));
  b.logp_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.val_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.rew_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.done_roll = (unsigned char *)malloc((size_t)batch);
  b.term_roll = (unsigned char *)malloc((size_t)batch);
  b.cut_roll = (unsigned char *)malloc((size_t)batch);
  b.valid_roll = (unsigned char *)malloc((size_t)batch);
  b.ret_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.adv_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.prior_frame = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  b.have_prior = (uint8_t *)calloc((size_t)n, 1);
  b.frame_scratch = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  b.ep_dec = (int *)calloc((size_t)n, sizeof(int));
  b.burnin = (uint8_t *)calloc((size_t)n, 1);
  b.reset_mask = (uint8_t *)calloc((size_t)n, 1);
  b.lane_seed = (int *)calloc((size_t)n, sizeof(int));
  b.lane_stage = (int *)calloc((size_t)n, sizeof(int));
  b.lane_stage_start = (int *)calloc((size_t)n, sizeof(int));
  b.lane_snap = (int *)calloc((size_t)n, sizeof(int));
  b.perm = (int *)malloc((size_t)batch * sizeof(int));
  if (cfg.mb > 0) {
    b.planes_mb =
        (uint8_t *)malloc((size_t)cfg.mb * ENV_N_CH * ENV_NPIX);
    b.scal_mb = (float *)malloc((size_t)cfg.mb * POL_SCAL * sizeof(float));
    b.acts_mb = (int32_t *)malloc((size_t)cfg.mb * POL_HEADS * sizeof(int32_t));
    b.logp_mb = (float *)malloc((size_t)cfg.mb * sizeof(float));
    b.adv_mb = (float *)malloc((size_t)cfg.mb * sizeof(float));
    b.ret_mb = (float *)malloc((size_t)cfg.mb * sizeof(float));
  }

  if (!b.assign || !b.cam || !b.depth || !b.edge || !b.scal6 || !b.rew ||
      !b.done_buf || !b.pose || !b.status || !b.act_rows || !b.planes_cur ||
      !b.scal_cur || !b.acts_cur || !b.logp_cur || !b.val_cur || !b.logits ||
      !b.planes_roll || !b.scal_roll || !b.acts_roll || !b.logp_roll ||
      !b.val_roll || !b.rew_roll || !b.done_roll || !b.term_roll ||
      !b.cut_roll || !b.valid_roll || !b.ret_roll || !b.adv_roll ||
      !b.next_val || !b.cut_val_roll || !b.prior_frame || !b.have_prior || !b.frame_scratch ||
      !b.ep_dec || !b.burnin || !b.reset_mask || !b.lane_seed ||
      !b.lane_stage || !b.lane_stage_start || !b.lane_snap || !b.perm ||
      (cfg.mb > 0 &&
       (!b.planes_mb || !b.scal_mb || !b.acts_mb || !b.logp_mb || !b.adv_mb ||
        !b.ret_mb)))
    die("alloc failed");

  if (cr_state_init(&cr, n, NULL) != 0)
    die("cr_state_init failed");
  if (use_curr) {
    if (cr_curr_init(&curr, nseeds, cfg.t0_share, cfg.seed) != 0)
      die("cr_curr_init failed");
    if (cfg.stage_snaps) {
      int si, stg;
      for (si = 0; si < nseeds; ++si) {
        char pbuf[16];
        char abuf[16];
        int poff = 0, aoff = 0;
        int npre = 0;
        pbuf[0] = '\0';
        abuf[0] = '\0';
        for (stg = 1; stg < CR_N_STAGES; ++stg) {
          if (paths[cap_slot(nseeds, si, stg)] == paths[si])
            continue;
          curr.avail[si * CR_N_STAGES + stg] = 1;
          poff += snprintf(pbuf + poff, sizeof(pbuf) - (size_t)poff, "%s%d",
                           npre ? "," : "", stg);
          npre++;
        }
        for (stg = 0; stg < CR_N_STAGES; ++stg) {
          if (!curr.avail[si * CR_N_STAGES + stg])
            continue;
          aoff += snprintf(abuf + aoff, sizeof(abuf) - (size_t)aoff, "%s%d",
                           aoff ? "," : "", stg);
        }
        printf("ppo: stage_snaps seed=%d preload stg=%s avail=[%s]\n",
               seed_ids[si], npre ? pbuf : "none", abuf);
      }
      fflush(stdout);
    }
  }

  /* Assign t0 snaps and, for curriculum, pre-seed capture slots. */
  for (i = 0; i < n; ++i) {
    b.lane_seed[i] = i % nseeds;
    b.lane_snap[i] = b.lane_seed[i];
    b.lane_stage[i] = 0;
    b.lane_stage_start[i] = 0;
    b.assign[i] = b.lane_snap[i];
  }
  if (fns.assign(env, b.assign) != 0 || fns.reset(env, NULL) != 0)
    die("assign/reset failed");
  if (use_curr && !cfg.stage_snaps) {
    int si, stg;
    for (si = 0; si < nseeds; ++si) {
      /* Capture from lane 0 after placing seed si's t0 there. */
      for (i = 0; i < n; ++i)
        b.assign[i] = si;
      if (fns.assign(env, b.assign) != 0)
        die("capture assign failed");
      memset(b.reset_mask, 0, (size_t)n);
      b.reset_mask[0] = 1;
      if (fns.reset(env, b.reset_mask) != 0)
        die("capture reset failed");
      for (stg = 1; stg < CR_N_STAGES; ++stg) {
        if (fns.capture(env, 0, cap_slot(nseeds, si, stg)) != 0)
          die("blaze_capture pre-seed failed");
      }
    }
    for (i = 0; i < n; ++i)
      b.assign[i] = b.lane_snap[i];
    if (fns.assign(env, b.assign) != 0 || fns.reset(env, NULL) != 0)
      die("post-capture assign/reset failed");
  }

  /* Torch: burnin=1, have_prior=0, ep_dec ~ U[0, EP_DEC) from seed+1.
   * First stored step is the noop burn-in (valid=0); cr_step still runs. */
  for (i = 0; i < n; ++i)
    b.burnin[i] = 1;
  if (cfg.ep_dec > 0) {
    uint64_t ep_rng = cfg.seed + 1ULL;
    if (ep_rng == 0)
      ep_rng = 1;
    for (i = 0; i < n; ++i)
      b.ep_dec[i] = (int)(rng_next(&ep_rng) % (uint64_t)cfg.ep_dec);
  }

  t0_wall = wall_sec();
  memset(&stats, 0, sizeof(stats));

  for (chunk = 0; chunk < cfg.max_chunks; ++chunk) {
    float lr_now;
    double elapsed;

    lr_now = lr_at(&cfg, ticks);
    nc.lr = lr_now;
    if (nn_set_config(nn_roll, &nc) != 0)
      dief("nn_set_config: %s", nn_last_error());
    if (split_nn && nn_set_config(nn_upd, &nc) != 0)
      dief("nn_set_config upd: %s", nn_last_error());

    {
      double ms_pack = 0.0, ms_nn = 0.0, ms_env = 0.0, ms_host = 0.0;
      double t_upd0, ms_upd;
      rc = run_rollout(nn_roll, env, &estep, &cfg, &fns, &cr,
                       use_curr ? &curr : NULL, use_curr, n, T, chunk, cap_last,
                       t0_trail, &t0_n, &t0_i, &n_eps, &b, &ms_pack, &ms_nn,
                       &ms_env, &ms_host);
      if (rc == -1)
        dief("nn_forward: %s", nn_last_error());
      if (rc == -2)
        dief("nn_sample: %s", nn_last_error());
      if (rc == -3)
        die("blaze_step_full failed");
      if (rc == -5)
        die("blaze_capture failed");
      if (rc == -6)
        die("reset/assign failed");
      if (rc != 0)
        die("rollout failed");

      cr_gae(b.rew_roll, b.term_roll, b.cut_roll, b.val_roll, b.next_val,
             b.cut_val_roll,
             cfg.gamma, cfg.lam, T, n, b.adv_roll, b.ret_roll);

      if (split_nn) {
        if (nn_copy_weights(nn_roll, nn_upd, cfg.checkpoint) != 0)
          dief("weight sync roll->upd: %s", nn_last_error());
      }

      t_upd0 = wall_sec();
      rc = ppo_updates(nn_upd, &b, &cfg, n, T, batch, is_metal, drop_tail,
                       !strcmp(cfg.tail_mb, "overlap"), &rng, &stats,
                       &n_upd_skip);
      if (rc != 0)
        dief("nn_update: %s", nn_last_error());
      ms_upd = (wall_sec() - t_upd0) * 1000.0;

      if (split_nn) {
        if (nn_copy_weights(nn_upd, nn_roll, cfg.checkpoint) != 0)
          dief("weight sync upd->roll: %s", nn_last_error());
      }

      printf("ppo: phases pack=%.1fms nn=%.1fms env=%.1fms host=%.1fms "
             "upd=%.1fms\n",
             ms_pack, ms_nn, ms_env, ms_host, ms_upd);
      fflush(stdout);
    }

    ticks += (int64_t)n * (int64_t)T * (int64_t)cfg.action_repeat;
    elapsed = wall_sec() - t0_wall;

    {
      int k;
      int brc;
      float t0s = t0_from_trail(t0_trail, t0_n);
      float rsum = 0.f;
      Nn *nn_w = split_nn ? nn_upd : nn_roll;
      for (k = 0; k < batch; ++k)
        rsum += b.rew_roll[k];
      printf("ppo: chunk=%d ticks=%lld t0=%.3f rew_mean=%.4f grad=%.4g "
             "ent=%.4g kl=%.4g clipfrac=%.4g ploss=%.4g vloss=%.4g lr=%.3g "
             "wall=%.1fs eps=%d\n",
             chunk, (long long)ticks, (double)t0s,
             (double)(rsum / (float)batch), (double)stats.grad_norm,
             (double)stats.entropy_mean, (double)stats.approx_kl,
             (double)stats.clipfrac, (double)stats.policy_loss,
             (double)stats.value_loss, (double)lr_now, elapsed, n_eps);
      fflush(stdout);

      /* Probe t0 is the trailing full-chain rate from t0 starts (same
       * quantity as chain_probe.py's chain success). _best never regresses. */
      if (t0s > best_t0) {
        brc = save_best_ckpt(nn_w, cfg.checkpoint, ticks, t0s, best_bin,
                             sizeof(best_bin));
        if (brc == -2)
          dief("best sidecar write failed: %s", cfg.checkpoint);
        if (brc != 0)
          dief("nn_save (best): %s", nn_last_error());
        best_t0 = t0s;
        best_ticks = ticks;
        printf("ppo: best t0=%.3f ticks=%lld ckpt=%s\n", (double)best_t0,
               (long long)best_ticks, best_bin);
        fflush(stdout);
      }
    }

    if (cfg.ckpt_ticks > 0 && ticks >= next_ckpt) {
      if (mkdir_parents_for_file(cfg.checkpoint) != 0)
        dief("mkdir parents for checkpoint failed: %s", cfg.checkpoint);
      if (rl_ckpt_save(split_nn ? nn_upd : nn_roll, cfg.checkpoint) != 0)
        dief("nn_save: %s", nn_last_error());
      next_ckpt += cfg.ckpt_ticks;
    }
    if (cfg.max_ticks > 0 && ticks >= cfg.max_ticks)
      break;
    if (cfg.max_wall > 0.f && elapsed >= (double)cfg.max_wall)
      break;
  }

  if (!isfinite(stats.grad_norm) || !(stats.grad_norm > 0.f)) {
    fprintf(stderr, "ppo: grad_norm not finite/nonzero (got %g)\n",
            (double)stats.grad_norm);
    return 1;
  }

  if (mkdir_parents_for_file(cfg.checkpoint) != 0)
    dief("mkdir parents for checkpoint failed: %s", cfg.checkpoint);
  if (rl_ckpt_save(split_nn ? nn_upd : nn_roll, cfg.checkpoint) != 0)
    dief("nn_save: %s", nn_last_error());
  if (rl_ckpt_load(split_nn ? nn_upd : nn_roll, cfg.checkpoint) != 0)
    dief("nn_load: %s", nn_last_error());

  if (n_upd_skip > 0)
    printf("ppo: update skips=%d chunks=%d\n", n_upd_skip, chunk);

  printf("ppo: PASS backend=%s device=%d n_envs=%d rollout_steps=%d batch=%d "
         "chunks=%d ticks=%lld grad_norm=%.6g policy_loss=%.6g "
         "value_loss=%.6g total_loss=%.6g ckpt=%s best=%s best_t0=%.3f "
         "best_ticks=%lld\n",
         cfg.backend, cfg.device, n, T, batch, chunk, (long long)ticks,
         (double)stats.grad_norm, (double)stats.policy_loss,
         (double)stats.value_loss, (double)stats.total_loss, cfg.checkpoint,
         best_bin[0] ? best_bin : "-", (double)best_t0, (long long)best_ticks);

  if (split_nn)
    nn_destroy(nn_upd);
  nn_destroy(nn_roll);
#if BLAZE_RL_HAVE_CUDA
  if (is_cuda)
    env_cuda_stage_destroy(&stage);
#endif
#if BLAZE_RL_HAVE_METAL
  if (is_metal)
    env_metal_obs_destroy(&metal_obs);
#endif
  fns.destroy(env);
  blaze_fns_close(&fns);
  cr_state_free(&cr);
  if (use_curr)
    cr_curr_free(&curr);
  free(b.logs);
  free(b.assign);
  free(b.cam);
  free(b.depth);
  free(b.edge);
  free(b.scal6);
  free(b.rew);
  free(b.done_buf);
  free(b.pose);
  free(b.status);
  free(b.act_rows);
  free(b.planes_cur);
  free(b.scal_cur);
  free(b.acts_cur);
  free(b.logp_cur);
  free(b.val_cur);
  free(b.next_val);
  free(b.cut_val_roll);
  free(b.logits);
  free(b.planes_roll);
  free(b.scal_roll);
  free(b.acts_roll);
  free(b.logp_roll);
  free(b.val_roll);
  free(b.rew_roll);
  free(b.done_roll);
  free(b.term_roll);
  free(b.cut_roll);
  free(b.valid_roll);
  free(b.ret_roll);
  free(b.adv_roll);
  free(b.prior_frame);
  free(b.have_prior);
  free(b.frame_scratch);
  free(b.ep_dec);
  free(b.burnin);
  free(b.reset_mask);
  free(b.lane_seed);
  free(b.lane_stage);
  free(b.lane_stage_start);
  free(b.lane_snap);
  free(b.perm);
  free(b.planes_mb);
  free(b.scal_mb);
  free(b.acts_mb);
  free(b.logp_mb);
  free(b.adv_mb);
  free(b.ret_mb);
  return 0;

fail_env:
#if BLAZE_RL_HAVE_CUDA
  if (is_cuda)
    env_cuda_stage_destroy(&stage);
#endif
#if BLAZE_RL_HAVE_METAL
  if (is_metal)
    env_metal_obs_destroy(&metal_obs);
#endif
  fns.destroy(env);
  blaze_fns_close(&fns);
  return 1;
}
