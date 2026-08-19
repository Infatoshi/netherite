/* ppo.c - C11 training smoke: Blaze env (dlopen) + blaze/nn policy.
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

#include "blaze_abi.h"
#include "model.h"
#include "nn.h"

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

#ifndef BLAZE_RL_HAVE_CUDA
#define BLAZE_RL_HAVE_CUDA 0
#endif
#ifndef BLAZE_RL_HAVE_METAL
#define BLAZE_RL_HAVE_METAL 0
#endif

/* Env layout (matches blaze/env/blaze_cpu.c and ppo_chain heads). */
enum {
  ENV_ACT = 13,
  ENV_CAM_H = 36,
  ENV_CAM_W = 64,
  ENV_NPIX = ENV_CAM_H * ENV_CAM_W,
  ENV_SCAL = 6,
  ENV_POSE = 5,
  ENV_STATUS = 17,
  ENV_N_PLANES = 9,
  ENV_STACK = 2,
  ENV_N_CH = ENV_N_PLANES * ENV_STACK,
  POL_HEADS = 9,
  POL_SCAL = 27,
  /* Episode fraction denominator (chain trainer EP_DEC). */
  EP_DEC = 1500
};

/* Repo-root relative paths for env shared libraries (out/ is disposable). */
static const char kEnvCpuSo[] = "out/blaze/env/blaze_cpu.so";
#if BLAZE_RL_HAVE_CUDA
static const char kEnvCudaSo[] = "out/blaze/env/blaze_cuda.so";
#endif

static const double kYaws[3] = {-15.0, 0.0, 15.0};
static const double kPitches[3] = {-10.0, 0.0, 10.0};
static const double kFwd[3] = {-1.0, 0.0, 1.0};
static const int kSelItems[9] = {17, 5, 280, 4, 58, 270, 274, 263, 50};

/* Device or host step_full (same C signature; pointer space differs on CUDA). */
#if !(defined(BLAZE_RL_HAVE_CUDA) && BLAZE_RL_HAVE_CUDA)
typedef int (*BlazeStepFullFn)(void *h, const double *actions, int repeat,
                               unsigned short *cam, unsigned char *depth,
                               unsigned char *edge, float *scal, float *rew,
                               unsigned char *done, float *pose, int *status);
#endif

#if !(defined(BLAZE_RL_HAVE_METAL) && BLAZE_RL_HAVE_METAL)
/* blaze_obs_cam_inputs from the CPU env .so (Metal observation inputs). */
typedef int (*BlazeObsCamInputsFn)(void *vh, int env, double *ex, double *ey,
                                   double *ez, float *yaw, float *pitch,
                                   int *x0, int *y0, int *z0, int *nx, int *ny,
                                   int *nz, const unsigned short **cells);
#endif

/* Explicit function table for the Blaze env ABI (CPU and CUDA .so). */
typedef struct BlazeFns {
  void *lib;
  void *(*create)(int device, int n, const BlazeCreateOpts *opts);
  void (*destroy)(void *h);
  int (*load_snapshots)(void *h, const char *const *paths, int count,
                        char *err, int err_cap);
  int (*assign)(void *h, const int *snap_idx);
  int (*reset)(void *h, const unsigned char *mask);
  BlazeStepFullFn step_full;
  BlazeObsCamInputsFn obs_cam_inputs; /* required for metal; optional else */
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

/* Load env shared library at path. Missing library or symbol fails; no fallback.
 * want_cam_inputs: require blaze_obs_cam_inputs (Metal observation path). */
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
  if (want_cam_inputs) {
    f->obs_cam_inputs =
        (BlazeObsCamInputsFn)must_dlsym(lib, "blaze_obs_cam_inputs");
  }
  if (!f->create || !f->destroy || !f->load_snapshots || !f->assign ||
      !f->reset || !f->step_full || (want_cam_inputs && !f->obs_cam_inputs)) {
    blaze_fns_close(f);
    return -1;
  }
  return 0;
}

/* Create parent directories for a file path (path itself is a file). */
static int mkdir_parents_for_file(const char *path) {
  char buf[TR_CFG_STR_MAX];
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

/* [N,9] head indices -> [N,13] env action rows (chain trainer decode). */
static void acts_to_rows(const int32_t *acts, int n, double *rows) {
  int i;
  memset(rows, 0, (size_t)n * ENV_ACT * sizeof(double));
  for (i = 0; i < n; ++i) {
    const int32_t *a = acts + (size_t)i * POL_HEADS;
    double *r = rows + (size_t)i * ENV_ACT;
    int y = a[0], p = a[1], f = a[2];
    if (y < 0 || y > 2)
      y = 1;
    if (p < 0 || p > 2)
      p = 1;
    if (f < 0 || f > 2)
      f = 1;
    r[0] = kFwd[f];
    r[2] = kYaws[y];
    r[3] = kPitches[p];
    r[4] = (double)a[3];            /* jump */
    r[7] = (double)a[4];            /* attack */
    r[8] = (double)a[5];            /* use */
    r[10] = (double)a[6] - 1.0;     /* craft: 0 -> -1 */
    r[11] = (double)a[7];           /* interact */
    r[9] = (double)a[8] - 1.0;      /* hotbar: 0 -> -1 */
  }
}

/* One frame of 9 planes from cam/depth/edge into planes[ch0..ch8] NCHW. */
static void pack_frame(const unsigned short *cam, const unsigned char *depth,
                       const unsigned char *edge, uint8_t *dst9) {
  int pix;
  for (pix = 0; pix < ENV_NPIX; ++pix) {
    unsigned short id = cam[pix];
    int y = pix / ENV_CAM_W;
    int x = pix % ENV_CAM_W;
    size_t base = (size_t)y * ENV_CAM_W + (size_t)x;
    dst9[0 * ENV_NPIX + base] = (uint8_t)(id == 17);
    dst9[1 * ENV_NPIX + base] = (uint8_t)(id == 18);
    dst9[2 * ENV_NPIX + base] = (uint8_t)(id == 16);
    dst9[3 * ENV_NPIX + base] = (uint8_t)(id == 1 || id == 4);
    dst9[4 * ENV_NPIX + base] = (uint8_t)(id == 2 || id == 3);
    dst9[5 * ENV_NPIX + base] = (uint8_t)(id == 58);
    dst9[6 * ENV_NPIX + base] = (uint8_t)(id != 0);
    dst9[7 * ENV_NPIX + base] = depth[pix];
    dst9[8 * ENV_NPIX + base] = edge[pix];
  }
}

/*
 * Build [n,18,H,W] stack and [n,27] scalars.
 * Stack contract (chain trainer): first decision after reset has the current
 * frame in both 9-plane slots; later decisions keep prior in slots 0..8 and
 * current in slots 9..17.
 * Scalar 26 = episode_decisions / 1500.0.
 */
static void pack_obs(const unsigned short *cam, const unsigned char *depth,
                     const unsigned char *edge, const float *scal6,
                     const float *pose, const int *status, const int *ep_dec,
                     const uint8_t *have_prior, const uint8_t *prior_frame,
                     int n, uint8_t *planes, float *scalars,
                     uint8_t *frame_scratch) {
  int e;
  for (e = 0; e < n; ++e) {
    uint8_t *frame = frame_scratch + (size_t)e * ENV_N_PLANES * ENV_NPIX;
    uint8_t *dst = planes + (size_t)e * ENV_N_CH * ENV_NPIX;
    float *s = scalars + (size_t)e * POL_SCAL;
    const uint8_t *prior =
        prior_frame + (size_t)e * ENV_N_PLANES * ENV_NPIX;
    int k;

    pack_frame(cam + (size_t)e * ENV_NPIX, depth + (size_t)e * ENV_NPIX,
               edge + (size_t)e * ENV_NPIX, frame);
    if (!have_prior[e]) {
      memcpy(dst, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
      memcpy(dst + ENV_N_PLANES * ENV_NPIX, frame,
             (size_t)ENV_N_PLANES * ENV_NPIX);
    } else {
      memcpy(dst, prior, (size_t)ENV_N_PLANES * ENV_NPIX);
      memcpy(dst + ENV_N_PLANES * ENV_NPIX, frame,
             (size_t)ENV_N_PLANES * ENV_NPIX);
    }

    memcpy(s, scal6 + (size_t)e * ENV_SCAL, ENV_SCAL * sizeof(float));
    for (k = 0; k < 9; ++k) {
      float v = (float)status[(size_t)e * ENV_STATUS + k];
      if (v > 10.f)
        v = 10.f;
      s[6 + k] = v / 10.f;
    }
    s[15] = status[(size_t)e * ENV_STATUS + 11] > 0 ? 1.f : 0.f;
    {
      int held = status[(size_t)e * ENV_STATUS + 10];
      for (k = 0; k < 9; ++k)
        s[16 + k] = (held == kSelItems[k]) ? 1.f : 0.f;
    }
    s[25] = pose[(size_t)e * ENV_POSE + 1] / 64.f;
    s[26] = (float)ep_dec[e] / (float)EP_DEC;
  }
}

/* Monte-Carlo returns and advantages over T x N (row-major t, then env). */
static void build_returns(const float *rew, const unsigned char *done,
                          const float *values, float gamma, int T, int N,
                          float *returns, float *advantages) {
  int e;
  for (e = 0; e < N; ++e) {
    float R = 0.f;
    int t;
    for (t = T - 1; t >= 0; --t) {
      size_t ix = (size_t)t * (size_t)N + (size_t)e;
      float keep = done[ix] ? 0.f : 1.f;
      R = rew[ix] + gamma * R * keep;
      returns[ix] = R;
      advantages[ix] = R - values[ix];
    }
  }
}

/* Rollout buffers owned by main; no heap ops inside. */
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
  float *ret_roll;
  float *adv_roll;
  uint8_t *prior_frame;
  uint8_t *have_prior;
  uint8_t *frame_scratch;
  int *ep_dec;
};

/* Env step context: CPU host ABI; CUDA uses staging; Metal overwrites obs. */
struct EnvStepCtx {
  BlazeFns *fns;
#if BLAZE_RL_HAVE_CUDA
  EnvCudaStage *stage; /* non-NULL only for backend=cuda */
#endif
#if BLAZE_RL_HAVE_METAL
  EnvMetalObs *metal; /* non-NULL only for backend=metal */
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
    /* CPU tick done; replace cam/depth/edge with Metal k_obs for every lane. */
    if (env_metal_obs_overwrite(ctx->metal, env, cam, depth, edge) != 0)
      return -1;
  }
#endif
  return 0;
}

/*
 * Compile-time gate: any malloc/calloc/realloc/free in this function fails
 * the build (undeclared TR_NO_ALLOC_IN_ROLLOUT).
 *
 * Fixed-size smoke: no terminal reset / burn-in. If any lane ends mid-rollout,
 * return -4; the fixture must not finish in rollout_steps decisions.
 *
 * CPU and CUDA allow max_n to exceed the forward batch. Metal fixes n to
 * max_n, so the caller creates one policy for rollout and one for update.
 */
static int run_rollout(Nn *nn, void *env, struct EnvStepCtx *estep,
                       const TrainConfig *cfg, int n, int T,
                       struct RolloutBuf *b) {
#define malloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define calloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define realloc(...) TR_NO_ALLOC_IN_ROLLOUT
#define free(...) TR_NO_ALLOC_IN_ROLLOUT
  int t, i;

  for (t = 0; t < T; ++t) {
    size_t off = (size_t)t * (size_t)n;

    pack_obs(b->cam, b->depth, b->edge, b->scal6, b->pose, b->status,
             b->ep_dec, b->have_prior, b->prior_frame, n, b->planes_cur,
             b->scal_cur, b->frame_scratch);

    if (nn_forward(nn, b->planes_cur, b->scal_cur, n, b->logits, b->val_cur) !=
        0)
      return -1;
    if (nn_sample(nn, b->logits, n, NN_SAMPLE_GUMBEL, b->acts_cur, b->logp_cur,
                  NULL) != 0)
      return -2;

    memcpy(b->planes_roll + off * ENV_N_CH * ENV_NPIX, b->planes_cur,
           (size_t)n * ENV_N_CH * ENV_NPIX);
    memcpy(b->scal_roll + off * POL_SCAL, b->scal_cur,
           (size_t)n * POL_SCAL * sizeof(float));
    memcpy(b->acts_roll + off * POL_HEADS, b->acts_cur,
           (size_t)n * POL_HEADS * sizeof(int32_t));
    memcpy(b->logp_roll + off, b->logp_cur, (size_t)n * sizeof(float));
    memcpy(b->val_roll + off, b->val_cur, (size_t)n * sizeof(float));

    acts_to_rows(b->acts_cur, n, b->act_rows);
    if (env_step(estep, env, b->act_rows, cfg->action_repeat, b->cam, b->depth,
                 b->edge, b->scal6, b->rew, b->done_buf, b->pose,
                 b->status) != 0)
      return -3;

    memcpy(b->rew_roll + off, b->rew, (size_t)n * sizeof(float));
    memcpy(b->done_roll + off, b->done_buf, (size_t)n);

    for (i = 0; i < n; ++i) {
      if (b->done_buf[i])
        return -4; /* terminal mid-rollout: no reset support in this smoke */
      {
        uint8_t *frame =
            b->frame_scratch + (size_t)i * ENV_N_PLANES * ENV_NPIX;
        uint8_t *prior =
            b->prior_frame + (size_t)i * ENV_N_PLANES * ENV_NPIX;
        memcpy(prior, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
        b->have_prior[i] = 1;
        b->ep_dec[i] += 1;
      }
    }
  }
  return 0;
#undef malloc
#undef calloc
#undef realloc
#undef free
}

#if BLAZE_RL_HAVE_METAL
/* Resolve metallib config: "auto" -> owned default path. */
static const char *resolve_metallib(const TrainConfig *cfg) {
  if (!cfg || !cfg->metallib[0])
    return NULL;
  if (strcmp(cfg->metallib, "auto") == 0)
    return ENV_METAL_OBS_DEFAULT_METALLIB;
  return cfg->metallib;
}
#endif

int main(int argc, char **argv) {
  TrainConfig cfg;
  int prc = tr_cfg_parse_argv(&cfg, argc, argv);
  void *env = NULL;
  Nn *nn = NULL;
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
  char err[512];
  const char *paths[1];
  const char *so_path = NULL;
  int *assign = NULL;
  struct RolloutBuf b;
  int n, T, batch, i, rc;
  int is_cuda = 0;
  int is_metal = 0;
  int want_cam = 0;

  memset(&b, 0, sizeof(b));
  memset(&fns, 0, sizeof(fns));
  memset(&estep, 0, sizeof(estep));
#if BLAZE_RL_HAVE_CUDA
  memset(&stage, 0, sizeof(stage));
#endif
#if BLAZE_RL_HAVE_METAL
  memset(&metal_obs, 0, sizeof(metal_obs));
#endif

  if (prc == 1)
    return 0; /* --dump-config or --help */
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
    so_path = kEnvCpuSo; /* exact CPU tick; Metal observation overwrite */
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
  /* Reject overflow before the signed int product. */
  if ((int64_t)n > (int64_t)INT_MAX / (int64_t)T) {
    fprintf(stderr,
            "ppo: n_envs (%d) * rollout_steps (%d) overflows int\n", n, T);
    return 1;
  }
  batch = n * T;

  if (blaze_fns_load(&fns, so_path, want_cam) != 0) {
    fprintf(stderr,
            "ppo: failed to load env library for backend=%s (path=%s); "
            "no fallback\n",
            cfg.backend, so_path);
    return 1;
  }

  opts.ktime = cfg.ktime;
  opts.stage_time = cfg.stage_time;
  opts.legacy_recenter = cfg.legacy_recenter;
  opts.warp_tick = cfg.warp_tick;
  opts.op_trace = cfg.op_trace;
  opts.no_ore_xy = cfg.no_ore_xy;

  env = fns.create(cfg.device, n, &opts);
  if (!env)
    die("blaze_create failed");

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
    if (!mlib || !mlib[0]) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      die("metallib path empty");
    }
    if (env_metal_obs_create(&metal_obs, n, cfg.metal_max_cells, mlib,
                             fns.obs_cam_inputs) != 0) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      fprintf(stderr, "ppo: Metal observation create failed; no fallback\n");
      return 1;
    }
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

  paths[0] = cfg.fixture;
  if (fns.load_snapshots(env, paths, 1, err, (int)sizeof(err)) < 0) {
    fprintf(stderr, "ppo: blaze_load_snapshots: %s\n", err);
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

  /* Allocate every buffer once before the rollout loop. */
  assign = (int *)calloc((size_t)n, sizeof(int));
  b.cam = (unsigned short *)malloc((size_t)n * ENV_NPIX * sizeof(*b.cam));
  b.depth = (unsigned char *)malloc((size_t)n * ENV_NPIX);
  b.edge = (unsigned char *)malloc((size_t)n * ENV_NPIX);
  b.scal6 = (float *)malloc((size_t)n * ENV_SCAL * sizeof(float));
  b.rew = (float *)malloc((size_t)n * sizeof(float));
  b.done_buf = (unsigned char *)malloc((size_t)n);
  b.pose = (float *)malloc((size_t)n * ENV_POSE * sizeof(float));
  b.status = (int *)malloc((size_t)n * ENV_STATUS * sizeof(int));
  b.act_rows = (double *)calloc((size_t)n * ENV_ACT, sizeof(double));
  b.planes_cur = (uint8_t *)calloc((size_t)n * ENV_N_CH * ENV_NPIX, 1);
  b.scal_cur = (float *)calloc((size_t)n * POL_SCAL, sizeof(float));
  b.acts_cur = (int32_t *)calloc((size_t)n * POL_HEADS, sizeof(int32_t));
  b.logp_cur = (float *)calloc((size_t)n, sizeof(float));
  b.val_cur = (float *)calloc((size_t)n, sizeof(float));
  b.logits = (float *)calloc((size_t)n * NN_N_LOGITS, sizeof(float));
  b.planes_roll =
      (uint8_t *)malloc((size_t)batch * ENV_N_CH * ENV_NPIX * sizeof(uint8_t));
  b.scal_roll = (float *)malloc((size_t)batch * POL_SCAL * sizeof(float));
  b.acts_roll = (int32_t *)malloc((size_t)batch * POL_HEADS * sizeof(int32_t));
  b.logp_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.val_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.rew_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.done_roll = (unsigned char *)malloc((size_t)batch);
  b.ret_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.adv_roll = (float *)malloc((size_t)batch * sizeof(float));
  b.prior_frame =
      (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  b.have_prior = (uint8_t *)calloc((size_t)n, 1);
  b.frame_scratch =
      (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  b.ep_dec = (int *)calloc((size_t)n, sizeof(int));

  if (!assign || !b.cam || !b.depth || !b.edge || !b.scal6 || !b.rew ||
      !b.done_buf || !b.pose || !b.status || !b.act_rows || !b.planes_cur ||
      !b.scal_cur || !b.acts_cur || !b.logp_cur || !b.val_cur || !b.logits ||
      !b.planes_roll || !b.scal_roll || !b.acts_roll || !b.logp_roll ||
      !b.val_roll || !b.rew_roll || !b.done_roll || !b.ret_roll || !b.adv_roll ||
      !b.prior_frame || !b.have_prior || !b.frame_scratch || !b.ep_dec)
    die("alloc failed");

  if (fns.assign(env, assign) != 0 || fns.reset(env, NULL) != 0)
    die("assign/reset failed");

  /* Noop step to render the first camera frame after reset. */
  for (i = 0; i < n; ++i) {
    double *r = b.act_rows + (size_t)i * ENV_ACT;
    memset(r, 0, ENV_ACT * sizeof(double));
    r[9] = -1.0;
    r[10] = -1.0;
  }
  if (env_step(&estep, env, b.act_rows, cfg.action_repeat, b.cam, b.depth,
               b.edge, b.scal6, b.rew, b.done_buf, b.pose, b.status) != 0)
    die("warmup step failed");
  for (i = 0; i < n; ++i) {
    if (b.done_buf[i])
      die("lane ended during warmup (fixture must stay live)");
  }

  nc = nn_config_default();
  nc.lr = cfg.lr;
  nc.ppo_clip = cfg.ppo_clip;
  nc.value_coef = cfg.value_coef;
  nc.entropy_coef = cfg.entropy_coef;
  nc.grad_limit = cfg.grad_limit;
  nc.rng_seed = cfg.seed;

  /* Metal fixes batch at create (n must equal max_n). Rollout uses n_envs;
   * the PPO update uses n_envs*T. Create for rollout first; after rollout,
   * recreate at batch size with the same weights for the one update. */
  nd.backend = is_metal   ? NN_BACKEND_METAL
               : is_cuda  ? NN_BACKEND_CUDA
                          : NN_BACKEND_CPU;
  nd.device = cfg.device;
  nd.max_n = is_metal ? n : batch;
  nd.config = nc;
  nn = nn_create(&nd);
  if (!nn)
    dief("nn_create: %s", nn_last_error());

  rc = run_rollout(nn, env, &estep, &cfg, n, T, &b);
  if (rc == -1)
    dief("nn_forward: %s", nn_last_error());
  if (rc == -2)
    dief("nn_sample: %s", nn_last_error());
  if (rc == -3)
    die("blaze_step_full failed");
  if (rc == -4)
    die("lane ended mid-rollout (this smoke has no terminal reset; "
        "fixture must not finish within rollout_steps)");
  if (rc != 0)
    die("rollout failed");

  if (is_metal && n != batch) {
    /* Weights-only handoff to a batch-sized Metal policy for the update. */
    if (mkdir_parents_for_file(cfg.checkpoint) != 0)
      dief("mkdir parents for checkpoint failed: %s", cfg.checkpoint);
    if (nn_save(nn, cfg.checkpoint) != 0)
      dief("nn_save (pre-update): %s", nn_last_error());
    nn_destroy(nn);
    nd.max_n = batch;
    nn = nn_create(&nd);
    if (!nn)
      dief("nn_create (batch): %s", nn_last_error());
    if (nn_load(nn, cfg.checkpoint) != 0)
      dief("nn_load (pre-update): %s", nn_last_error());
  }

  build_returns(b.rew_roll, b.done_roll, b.val_roll, cfg.gamma, T, n,
                b.ret_roll, b.adv_roll);

  memset(&stats, 0, sizeof(stats));
  rc = nn_update(nn, b.planes_roll, b.scal_roll, b.acts_roll, b.logp_roll,
                 b.adv_roll, b.ret_roll, batch, &stats);
  if (rc != 0)
    dief("nn_update: %s", nn_last_error());

  if (!isfinite(stats.grad_norm) || !(stats.grad_norm > 0.f)) {
    fprintf(stderr, "ppo: grad_norm not finite/nonzero (got %g)\n",
            (double)stats.grad_norm);
    return 1;
  }

  if (mkdir_parents_for_file(cfg.checkpoint) != 0)
    dief("mkdir parents for checkpoint failed: %s", cfg.checkpoint);
  if (nn_save(nn, cfg.checkpoint) != 0)
    dief("nn_save: %s", nn_last_error());
  if (nn_load(nn, cfg.checkpoint) != 0)
    dief("nn_load: %s", nn_last_error());

  printf("ppo: PASS backend=%s device=%d n_envs=%d rollout_steps=%d batch=%d "
         "grad_norm=%.6g policy_loss=%.6g value_loss=%.6g total_loss=%.6g "
         "ckpt=%s\n",
         cfg.backend, cfg.device, n, T, batch, (double)stats.grad_norm,
         (double)stats.policy_loss, (double)stats.value_loss,
         (double)stats.total_loss, cfg.checkpoint);

  nn_destroy(nn);
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
  free(assign);
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
  free(b.logits);
  free(b.planes_roll);
  free(b.scal_roll);
  free(b.acts_roll);
  free(b.logp_roll);
  free(b.val_roll);
  free(b.rew_roll);
  free(b.done_roll);
  free(b.ret_roll);
  free(b.adv_roll);
  free(b.prior_frame);
  free(b.have_prior);
  free(b.frame_scratch);
  free(b.ep_dec);
  return 0;
}
