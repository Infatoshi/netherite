/* eval.c - native 13-seed chain eval. Blaze CPU env (CUDA/Metal slot later).
 *
 * Spec: deleted blaze/rl/eval_chain_rl.py (sampled best-of-5 x 6000 ticks,
 * canonical seeds including held-out 11 and 33). Port semantics, not code.
 * Policy obs/action packing is shared with ppo.c (obs_pack.h). Checkpoint
 * load is the same schema-1 reader (rl_ckpt.h -> nn_load).
 *
 * Run from repo root:
 *   ./out/blaze/rl/eval --checkpoint out/blaze/rl/overnight_gpu0_6m.bin
 *   ./out/blaze/rl/eval --checkpoint PATH --stage 0     # default; t0 snaps
 *   ./out/blaze/rl/eval --checkpoint PATH --stage all   # t0 + stg1..4 ladder
 *   ./out/blaze/rl/eval --checkpoint PATH --backend magma --transfer closed
 *   ./out/blaze/rl/eval --checkpoint PATH --backend magma --transfer replay
 */
#define _DEFAULT_SOURCE
#include "blaze_abi.h"
#include "chain_curr.h"
#include "chain_reward.h"
#include "eval_magma.h"
#include "model.h"
#include "nn.h"
#include "obs_pack.h"
#include "rl_ckpt.h"

_Static_assert(NN_CAM_W == OC_W && NN_CAM_H == OC_H, "nn camera != oc_pixel");
_Static_assert((int)ENV_CAM_W == (int)NN_CAM_W &&
                   (int)ENV_CAM_H == (int)NN_CAM_H,
               "pack camera != nn");
_Static_assert((int)ENV_ACT == (int)EM_ACT, "action width");
_Static_assert((int)ENV_NPIX == (int)EM_NPIX, "pix count");

#if defined(BLAZE_RL_HAVE_CUDA) && BLAZE_RL_HAVE_CUDA
#include "env_cuda_stage.h"
#endif
#if defined(BLAZE_RL_HAVE_METAL) && BLAZE_RL_HAVE_METAL
#include "env_metal_obs.h"
#endif

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef BLAZE_RL_HAVE_CUDA
#define BLAZE_RL_HAVE_CUDA 0
#endif
#ifndef BLAZE_RL_HAVE_METAL
#define BLAZE_RL_HAVE_METAL 0
#endif

enum {
  EVAL_STR_MAX = 1024,
  EVAL_BACKEND_MAX = 16,
  EVAL_MAX_SEEDS = 32,
  EVAL_MAX_TRIES = 16,
  EVAL_TORCH_ITEM = 50,
  EVAL_STAGE_ALL = -1,
  EVAL_STAGE_MAX = 4,
  EVAL_XFER_CLOSED = 0,
  EVAL_XFER_REPLAY = 1
};

static const int kCanonSeeds[] = {2,  3,  10, 11, 14, 16, 20,
                                  27, 29, 32, 33, 44, 46};
static const int kCanonNSeeds = (int)(sizeof(kCanonSeeds) / sizeof(kCanonSeeds[0]));
static const int kHeldOut[] = {11, 33};
static const char *const kMileNames[] = {"t0", "logs3", "pick", "cobble3",
                                         "coal"};

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
  int (*emit)(void *h, int env, int want_cam, void *out);
} BlazeFns;

typedef struct EvalCfg {
  char backend[EVAL_BACKEND_MAX];
  int device;
  char checkpoint[EVAL_STR_MAX];
  char snaps_dir[EVAL_STR_MAX];
  int seeds[EVAL_MAX_SEEDS];
  int nseeds;
  int tries;
  int ep_ticks;
  int action_repeat;
  int success_item;
  uint64_t seed;
  int metal_max_cells;
  char metallib[EVAL_STR_MAX];
  int ktime;
  int stage_time;
  int legacy_recenter;
  int warp_tick;
  int op_trace;
  int no_ore_xy;
  int stack_kib; /* CUDA per-thread stack limit, KiB (default 128) */
  int stage; /* 0..4, or EVAL_STAGE_ALL */
  int transfer; /* EVAL_XFER_CLOSED | EVAL_XFER_REPLAY */
  char magma_bin[EVAL_STR_MAX];
} EvalCfg;

typedef struct StageSeedResult {
  int skip;
  int base;
  int abs_end;
  int torch;
} StageSeedResult;

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

static void die(const char *msg) {
  fprintf(stderr, "eval: %s\n", msg);
  exit(1);
}

static void dief(const char *fmt, const char *a) {
  fprintf(stderr, "eval: ");
  fprintf(stderr, fmt, a);
  fputc('\n', stderr);
  exit(1);
}

static int str_copy_fit(char *dst, size_t cap, const char *src) {
  size_t n = strlen(src);
  if (n + 1 > cap)
    return 0;
  memcpy(dst, src, n + 1);
  return 1;
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
    fprintf(stderr, "eval: dlsym %s: %s\n", name, err ? err : "null");
    return NULL;
  }
  return p;
}

static int blaze_fns_load(BlazeFns *f, const char *path, int want_cam_inputs,
                          int want_emit) {
  void *lib;
  if (!f || !path)
    return -1;
  memset(f, 0, sizeof(*f));
  lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "eval: dlopen '%s' failed: %s\n", path, dlerror());
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
  if (want_cam_inputs) {
    f->obs_cam_inputs =
        (BlazeObsCamInputsFn)must_dlsym(lib, "blaze_obs_cam_inputs");
  }
  if (want_emit)
    f->emit = (int (*)(void *, int, int, void *))must_dlsym(lib, "blaze_emit");
  if (!f->create || !f->destroy || !f->load_snapshots || !f->assign ||
      !f->reset || !f->step_full || !f->set_success_item ||
      (want_cam_inputs && !f->obs_cam_inputs) || (want_emit && !f->emit)) {
    blaze_fns_close(f);
    return -1;
  }
  return 0;
}

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

static int parse_int(const char *v, int *out, int lo, int hi) {
  char *end = NULL;
  long x;
  errno = 0;
  x = strtol(v, &end, 10);
  if (errno || !end || end == v || *end)
    return 0;
  if (x < (long)lo || x > (long)hi)
    return 0;
  *out = (int)x;
  return 1;
}

static int parse_u64(const char *v, uint64_t *out) {
  char *end = NULL;
  unsigned long long x;
  if (!v || !v[0] || v[0] == '-' || v[0] == '+')
    return 0;
  errno = 0;
  x = strtoull(v, &end, 10);
  if (errno || !end || end == v || *end)
    return 0;
  *out = (uint64_t)x;
  return 1;
}

static int parse_seed_list(const char *s, int *out, int cap) {
  const char *p;
  int n = 0;
  if (!s || !s[0])
    return -1;
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

static int seed_is_held_out(int seed) {
  int i;
  for (i = 0; i < (int)(sizeof(kHeldOut) / sizeof(kHeldOut[0])); ++i) {
    if (kHeldOut[i] == seed)
      return 1;
  }
  return 0;
}

static const char *mile_name(int reached) {
  if (reached >= CR_N_STAGES)
    return "TORCHES";
  if (reached < 0)
    reached = 0;
  if (reached > 4)
    reached = 4;
  return kMileNames[reached];
}

static const char *hist_name(int reached) {
  if (reached >= CR_N_STAGES)
    return "TORCH";
  return mile_name(reached);
}

static int snap_path(char *dst, size_t cap, const char *dir, int seed,
                     int stage) {
  int nw;
  if (stage <= 0)
    nw = snprintf(dst, cap, "%s/s%d_t0.bsnp", dir, seed);
  else
    nw = snprintf(dst, cap, "%s/s%d_stg%d.bsnp", dir, seed, stage);
  if (nw < 0 || (size_t)nw >= cap)
    return -1;
  return 0;
}

static int inv_stage(const int *best9) {
  if (!best9)
    return 0;
  if (best9[CR_IX_TORCH] >= 1)
    return CR_N_STAGES;
  return cr_stage_of_best(best9);
}

/* Ladder cell: L=logs3 P=pick C=cobble3 O=coal T=torch; .=none; SKIP. */
static void format_new_codes(char *buf, size_t cap, int base, int reached) {
  static const char kCode[] = "LPCOT";
  int s, nout = 0;
  int torch = reached >= CR_N_STAGES;
  int top = reached;
  if (!buf || cap == 0)
    return;
  buf[0] = 0;
  if (base < 0)
    base = 0;
  if (top > 4)
    top = 4;
  for (s = base + 1; s <= top; ++s) {
    if ((size_t)nout + 2 > cap)
      break;
    buf[nout++] = kCode[s - 1];
    buf[nout] = 0;
  }
  if (torch && base < CR_N_STAGES && (size_t)nout + 2 <= cap) {
    buf[nout++] = 'T';
    buf[nout] = 0;
  }
  if (nout == 0 && cap >= 2) {
    buf[0] = '.';
    buf[1] = 0;
  }
}

static void format_new_names(char *buf, size_t cap, int base, int reached) {
  int s;
  int torch = reached >= CR_N_STAGES;
  int top = reached;
  if (!buf || cap == 0)
    return;
  buf[0] = 0;
  if (base < 0)
    base = 0;
  if (top > 4)
    top = 4;
  for (s = base + 1; s <= top; ++s) {
    size_t n = strlen(buf);
    if (n + 1 >= cap)
      break;
    snprintf(buf + n, cap - n, "+%s", mile_name(s));
  }
  if (torch && base < CR_N_STAGES) {
    size_t n = strlen(buf);
    if (n + 2 < cap)
      snprintf(buf + n, cap - n, "+T");
  }
  if (!buf[0] && cap >= 2) {
    buf[0] = '-';
    buf[1] = 0;
  }
}

static void print_ladder(const EvalCfg *cfg, StageSeedResult rows[5][EVAL_MAX_SEEDS]) {
  int i, st;
  printf("\nlegend: L=logs3 P=pick C=cobble3 O=coal T=torch (full chain).\n");
  printf("        Concatenate milestones newly reached this episode "
         "(start inventory baselined out).\n");
  printf("        .=no new milestone. SKIP=missing snap "
         "(stg0 uses s{seed}_t0.bsnp; stgK uses s{seed}_stg{K}.bsnp).\n");
  printf("        Cell is best-of-%d tries. Absolute end is not shown "
         "here; see per-stage rows.\n",
         cfg->tries);
  printf("%5s", "seed");
  for (st = 0; st <= EVAL_STAGE_MAX; ++st)
    printf(" %6s%d", "stg", st);
  fputc('\n', stdout);
  for (i = 0; i < cfg->nseeds; ++i) {
    printf("%5d", cfg->seeds[i]);
    for (st = 0; st <= EVAL_STAGE_MAX; ++st) {
      char cell[24];
      if (rows[st][i].skip)
        snprintf(cell, sizeof(cell), "SKIP");
      else
        format_new_codes(cell, sizeof(cell), rows[st][i].base,
                         rows[st][i].abs_end);
      printf(" %7s", cell);
    }
    fputc('\n', stdout);
  }
  printf("%5s", "nT/n");
  for (st = 0; st <= EVAL_STAGE_MAX; ++st) {
    int n_ran = 0, n_t = 0;
    char cell[24];
    for (i = 0; i < cfg->nseeds; ++i) {
      if (rows[st][i].skip)
        continue;
      n_ran += 1;
      if (rows[st][i].torch)
        n_t += 1;
    }
    snprintf(cell, sizeof(cell), "%d/%d", n_t, n_ran);
    printf(" %7s", cell);
  }
  fputc('\n', stdout);
  fflush(stdout);
}

static void cfg_defaults(EvalCfg *c) {
  int i;
  memset(c, 0, sizeof(*c));
  (void)str_copy_fit(c->backend, sizeof(c->backend), "cpu");
  c->device = 0;
  (void)str_copy_fit(c->snaps_dir, sizeof(c->snaps_dir), "blaze/rl/out/snaps");
  c->nseeds = kCanonNSeeds;
  for (i = 0; i < kCanonNSeeds; ++i)
    c->seeds[i] = kCanonSeeds[i];
  c->tries = 5;
  c->ep_ticks = 6000;
  c->action_repeat = 4;
  c->success_item = EVAL_TORCH_ITEM;
  c->seed = 0;
  c->metal_max_cells = 2097152;
  (void)str_copy_fit(c->metallib, sizeof(c->metallib), "auto");
  c->warp_tick = 1;
  c->stack_kib = 128;
  c->stage = 0;
  c->transfer = EVAL_XFER_CLOSED;
  (void)str_copy_fit(c->magma_bin, sizeof(c->magma_bin), "magma/magma_game");
}

static int cfg_set(EvalCfg *c, const char *key, const char *val) {
  if (!c || !key || !val)
    return -1;
  if (!strcmp(key, "backend")) {
    if (strcmp(val, "cpu") && strcmp(val, "cuda") && strcmp(val, "metal") &&
        strcmp(val, "magma"))
      return -2;
    if (!str_copy_fit(c->backend, sizeof(c->backend), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "device")) {
    if (!parse_int(val, &c->device, 0, 64))
      return -2;
    return 0;
  }
  if (!strcmp(key, "checkpoint")) {
    if (!str_copy_fit(c->checkpoint, sizeof(c->checkpoint), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "snaps_dir")) {
    if (!str_copy_fit(c->snaps_dir, sizeof(c->snaps_dir), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "seeds")) {
    int n = parse_seed_list(val, c->seeds, EVAL_MAX_SEEDS);
    if (n <= 0)
      return -2;
    c->nseeds = n;
    return 0;
  }
  if (!strcmp(key, "tries")) {
    if (!parse_int(val, &c->tries, 1, EVAL_MAX_TRIES))
      return -2;
    return 0;
  }
  if (!strcmp(key, "ep_ticks")) {
    if (!parse_int(val, &c->ep_ticks, 1, 1000000))
      return -2;
    return 0;
  }
  if (!strcmp(key, "action_repeat") || !strcmp(key, "repeat")) {
    if (!parse_int(val, &c->action_repeat, 1, 64))
      return -2;
    return 0;
  }
  if (!strcmp(key, "success_item")) {
    if (!parse_int(val, &c->success_item, 0, 512))
      return -2;
    return 0;
  }
  if (!strcmp(key, "seed")) {
    if (!parse_u64(val, &c->seed))
      return -2;
    return 0;
  }
  if (!strcmp(key, "metal_max_cells")) {
    if (!parse_int(val, &c->metal_max_cells, 1, 1 << 30))
      return -2;
    return 0;
  }
  if (!strcmp(key, "metallib")) {
    if (!str_copy_fit(c->metallib, sizeof(c->metallib), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "stage")) {
    if (!strcmp(val, "all")) {
      c->stage = EVAL_STAGE_ALL;
      return 0;
    }
    if (!parse_int(val, &c->stage, 0, EVAL_STAGE_MAX))
      return -2;
    return 0;
  }
  if (!strcmp(key, "transfer")) {
    if (!strcmp(val, "closed")) {
      c->transfer = EVAL_XFER_CLOSED;
      return 0;
    }
    if (!strcmp(val, "replay")) {
      c->transfer = EVAL_XFER_REPLAY;
      return 0;
    }
    return -2;
  }
  if (!strcmp(key, "magma_bin")) {
    if (!str_copy_fit(c->magma_bin, sizeof(c->magma_bin), val))
      return -2;
    return 0;
  }
  return -1;
}

static void cfg_dump(const EvalCfg *c, FILE *out) {
  int i;
  fprintf(out, "  %-16s = %s\n", "backend", c->backend);
  fprintf(out, "  %-16s = %d\n", "device", c->device);
  fprintf(out, "  %-16s = %s\n", "checkpoint", c->checkpoint);
  fprintf(out, "  %-16s = %s\n", "snaps_dir", c->snaps_dir);
  fprintf(out, "  %-16s = ", "seeds");
  for (i = 0; i < c->nseeds; ++i) {
    if (i)
      fputc(',', out);
    fprintf(out, "%d", c->seeds[i]);
  }
  fputc('\n', out);
  fprintf(out, "  %-16s = %d\n", "tries", c->tries);
  fprintf(out, "  %-16s = %d\n", "ep_ticks", c->ep_ticks);
  fprintf(out, "  %-16s = %d\n", "action_repeat", c->action_repeat);
  fprintf(out, "  %-16s = %d\n", "success_item", c->success_item);
  fprintf(out, "  %-16s = %llu\n", "seed", (unsigned long long)c->seed);
  fprintf(out, "  %-16s = %d\n", "metal_max_cells", c->metal_max_cells);
  fprintf(out, "  %-16s = %s\n", "metallib", c->metallib);
  if (c->stage == EVAL_STAGE_ALL)
    fprintf(out, "  %-16s = all\n", "stage");
  else
    fprintf(out, "  %-16s = %d\n", "stage", c->stage);
  fprintf(out, "  %-16s = %s\n", "transfer",
          c->transfer == EVAL_XFER_REPLAY ? "replay" : "closed");
  fprintf(out, "  %-16s = %s\n", "magma_bin", c->magma_bin);
}

static void usage(void) {
  fprintf(stderr,
          "usage: eval --checkpoint PATH [options]\n"
          "  --snaps-dir DIR     t0 snapshots (default blaze/rl/out/snaps)\n"
          "  --seeds LIST        comma ids (default 13-seed canonical set)\n"
          "  --tries N           best-of-N (default 5)\n"
          "  --ep-ticks N        ticks per episode (default 6000)\n"
          "  --repeat N          action repeat (default 4)\n"
          "  --backend cpu|cuda|metal|magma\n"
          "  --transfer closed|replay  magma only (default closed)\n"
          "  --magma-bin PATH    magma_game (default magma/magma_game)\n"
          "  --device N\n"
          "  --seed N            Gumbel base seed (default 0)\n"
          "  --stage 0|1|2|3|4|all  snap ladder (default 0 = t0)\n"
          "  --set key=value     same keys as dump-config\n"
          "  --dump-config\n"
          "Run from repo root. Loads schema-1 weights via nn_load.\n"
          "stage 0 loads s{seed}_t0.bsnp (missing file is fatal).\n"
          "stage K loads s{seed}_stg{K}.bsnp; missing prints SKIP, exit 0.\n"
          "stage all runs 0..4 then a ladder table of new milestones.\n"
          "magma closed: net reads Magma --rl-bin BOLR (64x36 oc_pixel).\n"
          "magma replay: net reads blaze_cpu; actions replay on Magma.\n");
}

static int parse_argv(EvalCfg *c, int argc, char **argv, int *dump) {
  int i;
  cfg_defaults(c);
  *dump = 0;
  for (i = 1; i < argc; ++i) {
    const char *a = argv[i];
    const char *key = NULL;
    const char *val = NULL;
    char tmpkey[32];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      usage();
      return 1;
    }
    if (!strcmp(a, "--dump-config")) {
      *dump = 1;
      continue;
    }
    if (!strcmp(a, "--set")) {
      char *eq;
      if (i + 1 >= argc)
        return -1;
      eq = strchr(argv[++i], '=');
      if (!eq || eq == argv[i] || !eq[1])
        return -1;
      {
        size_t kn = (size_t)(eq - argv[i]);
        if (kn >= sizeof(tmpkey))
          return -1;
        memcpy(tmpkey, argv[i], kn);
        tmpkey[kn] = 0;
        key = tmpkey;
        val = eq + 1;
      }
    } else if (!strncmp(a, "--", 2)) {
      key = a + 2;
      if (i + 1 >= argc)
        return -1;
      val = argv[++i];
      if (!strcmp(key, "snaps-dir"))
        key = "snaps_dir";
      if (!strcmp(key, "ep-ticks"))
        key = "ep_ticks";
      if (!strcmp(key, "success-item"))
        key = "success_item";
      if (!strcmp(key, "metal-max-cells"))
        key = "metal_max_cells";
      if (!strcmp(key, "magma-bin"))
        key = "magma_bin";
    } else {
      fprintf(stderr, "eval: unexpected argument '%s'\n", a);
      return -1;
    }
    {
      int rc = cfg_set(c, key, val);
      if (rc == -1) {
        fprintf(stderr, "eval: unknown key '%s'\n", key);
        return -1;
      }
      if (rc == -2) {
        fprintf(stderr, "eval: bad value for '%s'\n", key);
        return -2;
      }
    }
  }
  return 0;
}

#if BLAZE_RL_HAVE_METAL
static const char *resolve_metallib(const EvalCfg *cfg) {
  if (!cfg || !cfg->metallib[0])
    return NULL;
  if (strcmp(cfg->metallib, "auto") == 0)
    return ENV_METAL_OBS_DEFAULT_METALLIB;
  return cfg->metallib;
}
#endif

static const char *ckpt_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static void print_stage_report(const EvalCfg *cfg, int stage_k, uint64_t rng_seed,
                               const uint8_t *skip, const int *seed_best,
                               const int *seed_base) {
  int i, h, n_ok, n_hist, n_ran, n_skip;
  int hist_vals[EVAL_MAX_SEEDS];
  int hist_count[EVAL_MAX_SEEDS];

  printf("net %s, sampled, %d tries x %d ticks\n", ckpt_basename(cfg->checkpoint),
         cfg->tries, cfg->ep_ticks);
  printf("backend %s  rng_protocol nn_sample Gumbel rng_seed=%llu "
         "sample_step=decision ni=seed_index*%d+attempt\n",
         cfg->backend, (unsigned long long)rng_seed, cfg->tries);
  if (strcmp(cfg->backend, "magma") == 0)
    printf("transfer %s magma_bin %s camera %dx%d (oc_pixel, not window)\n",
           cfg->transfer == EVAL_XFER_REPLAY ? "replay" : "closed",
           cfg->magma_bin, NN_CAM_W, NN_CAM_H);
  n_ok = 0;
  n_ran = 0;
  n_skip = 0;
  for (i = 0; i < cfg->nseeds; ++i) {
    const char *tag = seed_is_held_out(cfg->seeds[i]) ? " HELD-OUT" : "";
    if (skip[i]) {
      char path[EVAL_STR_MAX];
      if (snap_path(path, sizeof(path), cfg->snaps_dir, cfg->seeds[i],
                    stage_k) != 0)
        path[0] = 0;
      printf("seed %3d%s: SKIP missing %s\n", cfg->seeds[i], tag, path);
      n_skip += 1;
      continue;
    }
    n_ran += 1;
    if (seed_best[i] >= CR_N_STAGES)
      n_ok += 1;
    if (stage_k == 0) {
      printf("seed %3d%s: best milestone = %s (%d/%d)\n", cfg->seeds[i], tag,
             mile_name(seed_best[i]), seed_best[i], CR_N_STAGES);
    } else {
      char newly[80];
      format_new_names(newly, sizeof(newly), seed_base[i], seed_best[i]);
      printf("seed %3d%s: new=%s abs=%s (%d/%d) start=%s\n", cfg->seeds[i], tag,
             newly, mile_name(seed_best[i]), seed_best[i], CR_N_STAGES,
             mile_name(seed_base[i]));
    }
  }
  n_hist = 0;
  for (i = 0; i < cfg->nseeds; ++i) {
    int v;
    int found = 0;
    if (skip[i])
      continue;
    v = seed_best[i];
    for (h = 0; h < n_hist; ++h) {
      if (hist_vals[h] == v) {
        hist_count[h] += 1;
        found = 1;
        break;
      }
    }
    if (!found) {
      hist_vals[n_hist] = v;
      hist_count[n_hist] = 1;
      n_hist += 1;
    }
  }
  for (h = 0; h < n_hist; ++h) {
    int j;
    for (j = h + 1; j < n_hist; ++j) {
      if (hist_vals[j] < hist_vals[h]) {
        int tv = hist_vals[h], tc = hist_count[h];
        hist_vals[h] = hist_vals[j];
        hist_count[h] = hist_count[j];
        hist_vals[j] = tv;
        hist_count[j] = tc;
      }
    }
  }
  if (stage_k == 0) {
    printf("\nfull chain (torches): %d/%d seeds; milestone histogram: ", n_ok,
           cfg->nseeds);
  } else {
    printf("\nfull chain (torches): %d/%d seeds (%d SKIP); milestone histogram: ",
           n_ok, n_ran, n_skip);
  }
  for (h = 0; h < n_hist; ++h) {
    if (h)
      fputs(", ", stdout);
    printf("%s:%d", hist_name(hist_vals[h]), hist_count[h]);
  }
  fputc('\n', stdout);
  fflush(stdout);
}

static int eval_run_stage(const EvalCfg *cfg, int stage_k,
                          StageSeedResult *results);
static int eval_run_stage_magma(const EvalCfg *cfg, int stage_k,
                                StageSeedResult *results);

int main(int argc, char **argv) {
  EvalCfg cfg;
  int dump = 0;
  int prc;
  int ep_lim;

  prc = parse_argv(&cfg, argc, argv, &dump);
  if (prc == 1)
    return 0;
  if (prc != 0) {
    usage();
    return 2;
  }
  if (dump) {
    cfg_dump(&cfg, stdout);
    return 0;
  }
  if (!cfg.checkpoint[0]) {
    fprintf(stderr, "eval: --checkpoint PATH is required\n");
    usage();
    return 2;
  }
  if (cfg.ep_ticks % cfg.action_repeat != 0)
    die("ep_ticks must be divisible by action_repeat");
  ep_lim = cfg.ep_ticks / cfg.action_repeat;
  if (ep_lim <= 0)
    die("ep_lim must be positive");
  if (cfg.nseeds <= 0 || cfg.tries <= 0)
    die("nseeds and tries must be positive");
  if ((int64_t)cfg.nseeds * (int64_t)cfg.tries > (int64_t)INT_MAX / 4)
    die("nseeds*tries too large");
  if (access(cfg.checkpoint, R_OK) != 0)
    dief("checkpoint not readable: %s", cfg.checkpoint);
  if (strcmp(cfg.backend, "magma") == 0 && access(cfg.magma_bin, X_OK) != 0)
    dief("magma_game not executable: %s", cfg.magma_bin);

  if (cfg.stage == EVAL_STAGE_ALL) {
    StageSeedResult ladder[5][EVAL_MAX_SEEDS];
    int stage_k;
    int rc;
    memset(ladder, 0, sizeof(ladder));
    for (stage_k = 0; stage_k <= EVAL_STAGE_MAX; ++stage_k) {
      printf("--- stage %d ---\n", stage_k);
      fflush(stdout);
      rc = eval_run_stage(&cfg, stage_k, ladder[stage_k]);
      if (rc != 0)
        return rc;
    }
    print_ladder(&cfg, ladder);
    return 0;
  }
  return eval_run_stage(&cfg, cfg.stage, NULL);
}

static void magma_close_n(EvalMagma **mag, int n) {
  int i;
  if (!mag)
    return;
  for (i = 0; i < n; ++i) {
    eval_magma_close(mag[i]);
    mag[i] = NULL;
  }
}

static void magma_lane_fill(const EvalMagmaObs *o, int i, unsigned short *cam,
                            unsigned char *depth, unsigned char *edge,
                            float *scal6, float *pose, int *status,
                            unsigned char *done_buf) {
  eval_magma_fill_policy(o, cam + (size_t)i * ENV_NPIX,
                         depth + (size_t)i * ENV_NPIX,
                         edge + (size_t)i * ENV_NPIX,
                         pose + (size_t)i * ENV_POSE,
                         status + (size_t)i * ENV_STATUS,
                         scal6 + (size_t)i * ENV_SCAL);
  if (!done_buf)
    return;
  if (o->dead)
    done_buf[i] = 2;
  else if (o->inv_counts[CR_IX_TORCH] >= 1)
    done_buf[i] = 1;
  else
    done_buf[i] = 0;
}

static int eval_run_stage_magma(const EvalCfg *cfg, int stage_k,
                                StageSeedResult *results) {
  int n, nsnaps, ep_lim, i, dec, all_done, replay;
  void *env = NULL;
  Nn *nn = NULL;
  NnCreate nd;
  NnConfig nc;
  BlazeCreateOpts opts;
  BlazeFns fns;
  char err[512];
  char path_store[EVAL_MAX_SEEDS][EVAL_STR_MAX];
  const char *paths[EVAL_MAX_SEEDS];
  int loaded_si[EVAL_MAX_SEEDS];
  uint8_t skip[EVAL_MAX_SEEDS];
  int seed_best[EVAL_MAX_SEEDS];
  int seed_base[EVAL_MAX_SEEDS];
  int *assign = NULL;
  unsigned short *cam = NULL;
  unsigned char *depth = NULL;
  unsigned char *edge = NULL;
  float *scal6 = NULL;
  float *rew = NULL;
  unsigned char *done_buf = NULL;
  float *pose = NULL;
  int *status = NULL;
  double *act_rows = NULL;
  uint8_t *planes = NULL;
  float *scalars = NULL;
  int32_t *acts = NULL;
  float *logp = NULL;
  float *values = NULL;
  float *logits = NULL;
  uint8_t *prior_frame = NULL;
  uint8_t *have_prior = NULL;
  uint8_t *frame_scratch = NULL;
  int *ep_dec = NULL;
  uint8_t *finished = NULL;
  int *best9 = NULL;
  int *reached = NULL;
  int *base_stg = NULL;
  EvalMagma **mag = NULL;
  EvalMagmaObs blaze_obs;
  int *div_step = NULL;
  char (*div_why)[128] = NULL;
  int rc_out = 1;
  uint64_t rng_seed;

  memset(&fns, 0, sizeof(fns));
  memset(skip, 0, sizeof(skip));
  memset(seed_best, 0, sizeof(seed_best));
  memset(seed_base, 0, sizeof(seed_base));
  memset(loaded_si, 0, sizeof(loaded_si));
  blaze_create_opts_default(&opts);
  replay = cfg->transfer == EVAL_XFER_REPLAY;
  ep_lim = cfg->ep_ticks / cfg->action_repeat;
  rng_seed = cfg->seed + (uint64_t)stage_k;

  nsnaps = 0;
  for (i = 0; i < cfg->nseeds; ++i) {
    if (snap_path(path_store[i], EVAL_STR_MAX, cfg->snaps_dir, cfg->seeds[i],
                  stage_k) != 0)
      die("snaps path too long");
    if (access(path_store[i], R_OK) != 0) {
      skip[i] = 1;
      if (stage_k == 0)
        dief("missing t0 snapshot: %s", path_store[i]);
      continue;
    }
    paths[nsnaps] = path_store[i];
    loaded_si[nsnaps] = i;
    nsnaps += 1;
  }

  if (nsnaps == 0) {
    if (results) {
      for (i = 0; i < cfg->nseeds; ++i) {
        results[i].skip = 1;
        results[i].base = 0;
        results[i].abs_end = 0;
        results[i].torch = 0;
      }
    }
    print_stage_report(cfg, stage_k, rng_seed, skip, seed_best, seed_base);
    return 0;
  }

  n = nsnaps * cfg->tries;
  if (n <= 0)
    die("nseeds and tries must be positive");
  if (replay && access(kEnvCpuSo, R_OK) != 0)
    dief("missing blaze_cpu.so: %s", kEnvCpuSo);

  mag = (EvalMagma **)calloc((size_t)n, sizeof(*mag));
  div_step = (int *)calloc((size_t)n, sizeof(int));
  div_why = (char (*)[128])calloc((size_t)n, 128);
  if (!mag || !div_step || !div_why)
    die("alloc failed");
  for (i = 0; i < n; ++i)
    div_step[i] = -1;

  for (i = 0; i < n; ++i) {
    int loaded = i / cfg->tries;
    int si = loaded_si[loaded];
    mag[i] = eval_magma_open(cfg->magma_bin, paths[loaded], cfg->seeds[si], err,
                             (int)sizeof(err));
    if (!mag[i]) {
      fprintf(stderr, "eval: magma open seed %d: %s\n", cfg->seeds[si], err);
      goto fail;
    }
  }

  if (replay) {
    opts.ktime = cfg->ktime;
    opts.stage_time = cfg->stage_time;
    opts.legacy_recenter = cfg->legacy_recenter;
    opts.warp_tick = cfg->warp_tick;
    opts.op_trace = cfg->op_trace;
    opts.no_ore_xy = cfg->no_ore_xy;
    opts.stack_kib = cfg->stack_kib;
    if (blaze_fns_load(&fns, kEnvCpuSo, 0, 1) != 0) {
      fprintf(stderr, "eval: failed to load %s for magma replay\n", kEnvCpuSo);
      goto fail;
    }
    env = fns.create(0, n, &opts);
    if (!env)
      die("blaze_create failed");
    if (fns.set_success_item(env, cfg->success_item) != 0)
      die("blaze_set_success_item failed");
    if (fns.load_snapshots(env, paths, nsnaps, err, (int)sizeof(err)) < 0) {
      fprintf(stderr, "eval: blaze_load_snapshots: %s\n", err);
      goto fail;
    }
  }

  assign = (int *)calloc((size_t)n, sizeof(int));
  cam = (unsigned short *)calloc((size_t)n * ENV_NPIX, sizeof(*cam));
  depth = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  edge = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  scal6 = (float *)calloc((size_t)n * ENV_SCAL, sizeof(float));
  rew = (float *)calloc((size_t)n, sizeof(float));
  done_buf = (unsigned char *)calloc((size_t)n, 1);
  pose = (float *)calloc((size_t)n * ENV_POSE, sizeof(float));
  status = (int *)calloc((size_t)n * ENV_STATUS, sizeof(int));
  act_rows = (double *)calloc((size_t)n * ENV_ACT, sizeof(double));
  planes = (uint8_t *)calloc((size_t)n * ENV_N_CH * ENV_NPIX, 1);
  scalars = (float *)calloc((size_t)n * POL_SCAL, sizeof(float));
  acts = (int32_t *)calloc((size_t)n * POL_HEADS, sizeof(int32_t));
  logp = (float *)calloc((size_t)n, sizeof(float));
  values = (float *)calloc((size_t)n, sizeof(float));
  logits = (float *)calloc((size_t)n * NN_N_LOGITS, sizeof(float));
  prior_frame = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  have_prior = (uint8_t *)calloc((size_t)n, 1);
  frame_scratch = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  ep_dec = (int *)calloc((size_t)n, sizeof(int));
  finished = (uint8_t *)calloc((size_t)n, 1);
  best9 = (int *)calloc((size_t)n * 9, sizeof(int));
  reached = (int *)calloc((size_t)n, sizeof(int));
  base_stg = (int *)calloc((size_t)n, sizeof(int));
  if (!assign || !cam || !depth || !edge || !scal6 || !rew || !done_buf ||
      !pose || !status || !act_rows || !planes || !scalars || !acts || !logp ||
      !values || !logits || !prior_frame || !have_prior || !frame_scratch ||
      !ep_dec || !finished || !best9 || !reached || !base_stg)
    die("alloc failed");

  for (i = 0; i < n; ++i)
    assign[i] = i / cfg->tries;
  if (replay) {
    if (fns.assign(env, assign) != 0 || fns.reset(env, NULL) != 0)
      die("assign/reset failed");
    for (i = 0; i < n; ++i) {
      char why[80];
      if (!mag[i] || div_step[i] >= 0)
        continue;
      if (fns.emit(env, i, 1, &blaze_obs) != 0) {
        div_step[i] = 0;
        snprintf(div_why[i], sizeof div_why[i], "blaze_emit t=0");
        continue;
      }
      if (eval_magma_cmp_gated(eval_magma_obs(mag[i]), &blaze_obs, why,
                               (int)sizeof(why)) != 0) {
        div_step[i] = 0;
        snprintf(div_why[i], sizeof div_why[i], "t=0 %s", why);
      }
    }
  }

  nc = nn_config_default();
  nc.rng_seed = rng_seed;
  nd.backend = NN_BACKEND_CPU;
  nd.device = 0;
  nd.max_n = n;
  nd.config = nc;
  nn = nn_create(&nd);
  if (!nn)
    dief("nn_create: %s", nn_last_error());
  if (rl_ckpt_load(nn, cfg->checkpoint) != 0)
    dief("nn_load: %s", nn_last_error());

  for (i = 0; i < n; ++i) {
    int32_t *a = acts + (size_t)i * POL_HEADS;
    memset(a, 0, POL_HEADS * sizeof(int32_t));
    a[0] = 1;
    a[1] = 1;
    a[2] = 1;
  }
  acts_to_rows(acts, n, act_rows);
  if (replay) {
    if (fns.step_full(env, act_rows, cfg->action_repeat, cam, depth, edge,
                      scal6, rew, done_buf, pose, status) != 0)
      die("burn-in blaze step failed");
  }
  for (i = 0; i < n; ++i) {
    char why[80];
    if (!mag[i])
      continue;
    if (eval_magma_step(mag[i], act_rows + (size_t)i * ENV_ACT,
                        cfg->action_repeat) != 0) {
      if (div_step[i] < 0) {
        div_step[i] = 1;
        snprintf(div_why[i], sizeof div_why[i], "magma burn-in step");
      }
      eval_magma_close(mag[i]);
      mag[i] = NULL;
      continue;
    }
    if (replay && div_step[i] < 0) {
      if (fns.emit(env, i, 1, &blaze_obs) != 0) {
        div_step[i] = 1;
        snprintf(div_why[i], sizeof div_why[i], "blaze_emit burn-in");
      } else if (eval_magma_cmp_gated(eval_magma_obs(mag[i]), &blaze_obs, why,
                                      (int)sizeof(why)) != 0) {
        div_step[i] = 1;
        snprintf(div_why[i], sizeof div_why[i], "burn-in %s", why);
      }
    }
    if (!replay)
      magma_lane_fill(eval_magma_obs(mag[i]), i, cam, depth, edge, scal6, pose,
                      status, done_buf);
  }

  for (i = 0; i < n; ++i) {
    int k;
    for (k = 0; k < 9; ++k)
      best9[(size_t)i * 9 + (size_t)k] = status[(size_t)i * ENV_STATUS + k];
    base_stg[i] = inv_stage(best9 + (size_t)i * 9);
    have_prior[i] = 0;
    ep_dec[i] = 0;
    finished[i] = 0;
    reached[i] = 0;
  }

  for (dec = 0; dec < ep_lim; ++dec) {
    pack_obs(cam, depth, edge, scal6, pose, status, ep_dec, ep_lim, have_prior,
             prior_frame, n, planes, scalars, frame_scratch);
    if (nn_forward(nn, planes, scalars, n, logits, values) != 0)
      dief("nn_forward: %s", nn_last_error());
    if (nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL) != 0)
      dief("nn_sample: %s", nn_last_error());
    acts_to_rows(acts, n, act_rows);
    if (replay) {
      if (fns.step_full(env, act_rows, cfg->action_repeat, cam, depth, edge,
                        scal6, rew, done_buf, pose, status) != 0)
        die("blaze_step_full failed");
    }
    for (i = 0; i < n; ++i) {
      char why[80];
      if (!mag[i])
        continue;
      if (eval_magma_step(mag[i], act_rows + (size_t)i * ENV_ACT,
                          cfg->action_repeat) != 0) {
        if (div_step[i] < 0) {
          div_step[i] = dec + 2;
          snprintf(div_why[i], sizeof div_why[i], "magma step");
        }
        eval_magma_close(mag[i]);
        mag[i] = NULL;
        done_buf[i] = 2;
        continue;
      }
      if (replay && div_step[i] < 0) {
        if (fns.emit(env, i, 1, &blaze_obs) != 0) {
          div_step[i] = dec + 2;
          snprintf(div_why[i], sizeof div_why[i], "blaze_emit");
        } else if (eval_magma_cmp_gated(eval_magma_obs(mag[i]), &blaze_obs, why,
                                        (int)sizeof(why)) != 0) {
          div_step[i] = dec + 2;
          snprintf(div_why[i], sizeof div_why[i], "%s", why);
        }
      }
      if (!replay)
        magma_lane_fill(eval_magma_obs(mag[i]), i, cam, depth, edge, scal6,
                        pose, status, done_buf);
    }

    all_done = 1;
    for (i = 0; i < n; ++i) {
      int k;
      uint8_t *frame;
      uint8_t *prior;
      int stg;
      if (finished[i])
        continue;
      for (k = 0; k < 9; ++k) {
        int v = status[(size_t)i * ENV_STATUS + k];
        if (v > best9[(size_t)i * 9 + (size_t)k])
          best9[(size_t)i * 9 + (size_t)k] = v;
      }
      frame = frame_scratch + (size_t)i * ENV_N_PLANES * ENV_NPIX;
      prior = prior_frame + (size_t)i * ENV_N_PLANES * ENV_NPIX;
      memcpy(prior, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
      have_prior[i] = 1;
      ep_dec[i] += 1;
      if (best9[(size_t)i * 9 + CR_IX_TORCH] >= 1 || done_buf[i] == 1) {
        reached[i] = CR_N_STAGES;
        finished[i] = 1;
        continue;
      }
      stg = cr_stage_of_best(best9 + (size_t)i * 9);
      if (done_buf[i] > 0 || ep_dec[i] >= ep_lim) {
        reached[i] = stg;
        finished[i] = 1;
        continue;
      }
      reached[i] = stg;
      all_done = 0;
    }
    if (all_done)
      break;
    if ((dec + 1) % 50 == 0 || dec + 1 == ep_lim) {
      int nfin = 0;
      for (i = 0; i < n; ++i)
        nfin += (int)finished[i];
      fprintf(stderr, "eval: decision %d/%d finished=%d/%d\n", dec + 1, ep_lim,
              nfin, n);
    }
  }
  for (i = 0; i < n; ++i) {
    if (!finished[i])
      reached[i] = cr_stage_of_best(best9 + (size_t)i * 9);
  }

  for (i = 0; i < cfg->nseeds; ++i) {
    seed_best[i] = 0;
    seed_base[i] = 0;
  }
  for (i = 0; i < n; ++i) {
    int loaded = i / cfg->tries;
    int si = loaded_si[loaded];
    if (reached[i] > seed_best[si])
      seed_best[si] = reached[i];
    if ((i % cfg->tries) == 0)
      seed_base[si] = base_stg[i];
  }

  if (replay) {
    int n_match = 0, n_ran = 0;
    printf("replay magma --rl-bin vs blaze_cpu gated BOLR (cam/inv/pose/"
           "hotbar/coal/dead)\n");
    for (i = 0; i < n; ++i) {
      int loaded = i / cfg->tries;
      int si = loaded_si[loaded];
      int attempt = i % cfg->tries;
      n_ran += 1;
      if (div_step[i] < 0) {
        n_match += 1;
        printf("seed %3d try %d: MATCH\n", cfg->seeds[si], attempt);
      } else {
        printf("seed %3d try %d: DIVERGE step=%d %s\n", cfg->seeds[si],
               attempt, div_step[i], div_why[i]);
      }
    }
    printf("MATCH %d/%d (step 0=t0, 1=burn-in, 2+=policy decision)\n", n_match,
           n_ran);
    fflush(stdout);
  }

  if (results) {
    for (i = 0; i < cfg->nseeds; ++i) {
      results[i].skip = skip[i] ? 1 : 0;
      results[i].base = seed_base[i];
      results[i].abs_end = seed_best[i];
      results[i].torch = seed_best[i] >= CR_N_STAGES;
    }
  }
  print_stage_report(cfg, stage_k, rng_seed, skip, seed_best, seed_base);
  rc_out = 0;

fail:
  magma_close_n(mag, n);
  free(mag);
  free(div_step);
  free(div_why);
  free(assign);
  free(cam);
  free(depth);
  free(edge);
  free(scal6);
  free(rew);
  free(done_buf);
  free(pose);
  free(status);
  free(act_rows);
  free(planes);
  free(scalars);
  free(acts);
  free(logp);
  free(values);
  free(logits);
  free(prior_frame);
  free(have_prior);
  free(frame_scratch);
  free(ep_dec);
  free(finished);
  free(best9);
  free(reached);
  free(base_stg);
  if (nn)
    nn_destroy(nn);
  if (env && fns.destroy)
    fns.destroy(env);
  blaze_fns_close(&fns);
  return rc_out;
}

static int eval_run_stage(const EvalCfg *cfg, int stage_k,
                          StageSeedResult *results) {
  int n, nsnaps, ep_lim, i, dec, all_done;
  int is_cuda = 0;
  int is_metal = 0;
  int want_cam = 0;
  const char *so_path = NULL;
  void *env = NULL;
  Nn *nn = NULL;
  NnCreate nd;
  NnConfig nc;
  BlazeCreateOpts opts;
  BlazeFns fns;
  struct EnvStepCtx estep;
  char err[512];
  char path_store[EVAL_MAX_SEEDS][EVAL_STR_MAX];
  const char *paths[EVAL_MAX_SEEDS];
  int loaded_si[EVAL_MAX_SEEDS];
  uint8_t skip[EVAL_MAX_SEEDS];
  int seed_best[EVAL_MAX_SEEDS];
  int seed_base[EVAL_MAX_SEEDS];
  int *assign = NULL;
  unsigned short *cam = NULL;
  unsigned char *depth = NULL;
  unsigned char *edge = NULL;
  float *scal6 = NULL;
  float *rew = NULL;
  unsigned char *done_buf = NULL;
  float *pose = NULL;
  int *status = NULL;
  double *act_rows = NULL;
  uint8_t *planes = NULL;
  float *scalars = NULL;
  int32_t *acts = NULL;
  float *logp = NULL;
  float *values = NULL;
  float *logits = NULL;
  uint8_t *prior_frame = NULL;
  uint8_t *have_prior = NULL;
  uint8_t *frame_scratch = NULL;
  int *ep_dec = NULL;
  uint8_t *finished = NULL;
  int *best9 = NULL;
  int *reached = NULL;
  int *base_stg = NULL;
  int rc_out = 1;
  uint64_t rng_seed;

#if BLAZE_RL_HAVE_CUDA
  EnvCudaStage stage;
  memset(&stage, 0, sizeof(stage));
#endif
#if BLAZE_RL_HAVE_METAL
  EnvMetalObs metal_obs;
  memset(&metal_obs, 0, sizeof(metal_obs));
#endif
  memset(&fns, 0, sizeof(fns));
  memset(&estep, 0, sizeof(estep));
  memset(skip, 0, sizeof(skip));
  memset(seed_best, 0, sizeof(seed_best));
  memset(seed_base, 0, sizeof(seed_base));
  memset(loaded_si, 0, sizeof(loaded_si));
  blaze_create_opts_default(&opts);

  ep_lim = cfg->ep_ticks / cfg->action_repeat;
  /* Gumbel stream: rng_seed = cfg.seed + (uint64_t)stage_k.
   * Stage 0 equals the historical seed so --stage 0 is bit-identical to
   * the pre-flag binary. Later stages draw a different deterministic
   * stream. SKIP seeds occupy no lane; ni = loaded_index * tries + attempt
   * among snapshots that exist. */
  rng_seed = cfg->seed + (uint64_t)stage_k;

  if (strcmp(cfg->backend, "magma") == 0)
    return eval_run_stage_magma(cfg, stage_k, results);

  if (strcmp(cfg->backend, "cpu") == 0) {
    is_cuda = 0;
    is_metal = 0;
    so_path = kEnvCpuSo;
  } else if (strcmp(cfg->backend, "cuda") == 0) {
#if BLAZE_RL_HAVE_CUDA
    is_cuda = 1;
    is_metal = 0;
    so_path = kEnvCudaSo;
#else
    fprintf(stderr,
            "eval: backend 'cuda' is not available in this binary "
            "(no fallback)\n");
    return 1;
#endif
  } else if (strcmp(cfg->backend, "metal") == 0) {
#if BLAZE_RL_HAVE_METAL
    is_cuda = 0;
    is_metal = 1;
    so_path = kEnvCpuSo;
    want_cam = 1;
#else
    fprintf(stderr,
            "eval: backend 'metal' is not available in this binary "
            "(no fallback)\n");
    return 1;
#endif
  } else {
    fprintf(stderr, "eval: backend '%s' is not available (no fallback)\n",
            cfg->backend);
    return 1;
  }

  nsnaps = 0;
  for (i = 0; i < cfg->nseeds; ++i) {
    if (snap_path(path_store[i], EVAL_STR_MAX, cfg->snaps_dir, cfg->seeds[i],
                  stage_k) != 0)
      die("snaps path too long");
    if (access(path_store[i], R_OK) != 0) {
      skip[i] = 1;
      if (stage_k == 0)
        dief("missing t0 snapshot: %s", path_store[i]);
      continue;
    }
    paths[nsnaps] = path_store[i];
    loaded_si[nsnaps] = i;
    nsnaps += 1;
  }

  if (nsnaps == 0) {
    if (results) {
      for (i = 0; i < cfg->nseeds; ++i) {
        results[i].skip = 1;
        results[i].base = 0;
        results[i].abs_end = 0;
        results[i].torch = 0;
      }
    }
    print_stage_report(cfg, stage_k, rng_seed, skip, seed_best, seed_base);
    return 0;
  }

  n = nsnaps * cfg->tries;
  if (n <= 0)
    die("nseeds and tries must be positive");

  if (blaze_fns_load(&fns, so_path, want_cam, 0) != 0) {
    fprintf(stderr,
            "eval: failed to load env library for backend=%s (path=%s); "
            "no fallback\n",
            cfg->backend, so_path);
    return 1;
  }

  opts.ktime = cfg->ktime;
  opts.stage_time = cfg->stage_time;
  opts.legacy_recenter = cfg->legacy_recenter;
  opts.warp_tick = cfg->warp_tick;
  opts.op_trace = cfg->op_trace;
  opts.no_ore_xy = cfg->no_ore_xy;
  opts.stack_kib = cfg->stack_kib;

  env = fns.create(cfg->device, n, &opts);
  if (!env)
    die("blaze_create failed");
  if (fns.set_success_item(env, cfg->success_item) != 0)
    die("blaze_set_success_item failed");

#if BLAZE_RL_HAVE_CUDA
  if (is_cuda) {
    if (env_cuda_stage_create(&stage, n, cfg->device) != 0) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      die("env_cuda_stage_create failed");
    }
  }
#endif
#if BLAZE_RL_HAVE_METAL
  if (is_metal) {
    const char *mlib = resolve_metallib(cfg);
    if (!mlib || !mlib[0]) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      die("metallib path empty");
    }
    if (env_metal_obs_create(&metal_obs, n, cfg->metal_max_cells, mlib,
                             fns.obs_cam_inputs) != 0) {
      fns.destroy(env);
      blaze_fns_close(&fns);
      fprintf(stderr, "eval: Metal observation create failed; no fallback\n");
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

  if (fns.load_snapshots(env, paths, nsnaps, err, (int)sizeof(err)) < 0) {
    fprintf(stderr, "eval: blaze_load_snapshots: %s\n", err);
    goto fail;
  }

  assign = (int *)calloc((size_t)n, sizeof(int));
  cam = (unsigned short *)calloc((size_t)n * ENV_NPIX, sizeof(*cam));
  depth = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  edge = (unsigned char *)calloc((size_t)n * ENV_NPIX, 1);
  scal6 = (float *)calloc((size_t)n * ENV_SCAL, sizeof(float));
  rew = (float *)calloc((size_t)n, sizeof(float));
  done_buf = (unsigned char *)calloc((size_t)n, 1);
  pose = (float *)calloc((size_t)n * ENV_POSE, sizeof(float));
  status = (int *)calloc((size_t)n * ENV_STATUS, sizeof(int));
  act_rows = (double *)calloc((size_t)n * ENV_ACT, sizeof(double));
  planes = (uint8_t *)calloc((size_t)n * ENV_N_CH * ENV_NPIX, 1);
  scalars = (float *)calloc((size_t)n * POL_SCAL, sizeof(float));
  acts = (int32_t *)calloc((size_t)n * POL_HEADS, sizeof(int32_t));
  logp = (float *)calloc((size_t)n, sizeof(float));
  values = (float *)calloc((size_t)n, sizeof(float));
  logits = (float *)calloc((size_t)n * NN_N_LOGITS, sizeof(float));
  prior_frame = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  have_prior = (uint8_t *)calloc((size_t)n, 1);
  frame_scratch = (uint8_t *)calloc((size_t)n * ENV_N_PLANES * ENV_NPIX, 1);
  ep_dec = (int *)calloc((size_t)n, sizeof(int));
  finished = (uint8_t *)calloc((size_t)n, 1);
  best9 = (int *)calloc((size_t)n * 9, sizeof(int));
  reached = (int *)calloc((size_t)n, sizeof(int));
  base_stg = (int *)calloc((size_t)n, sizeof(int));
  if (!assign || !cam || !depth || !edge || !scal6 || !rew || !done_buf ||
      !pose || !status || !act_rows || !planes || !scalars || !acts || !logp ||
      !values || !logits || !prior_frame || !have_prior || !frame_scratch ||
      !ep_dec || !finished || !best9 || !reached || !base_stg)
    die("alloc failed");

  for (i = 0; i < n; ++i)
    assign[i] = i / cfg->tries;
  if (fns.assign(env, assign) != 0 || fns.reset(env, NULL) != 0)
    die("assign/reset failed");

  nc = nn_config_default();
  nc.rng_seed = rng_seed;
  nd.backend = is_metal  ? NN_BACKEND_METAL
               : is_cuda ? NN_BACKEND_CUDA
                         : NN_BACKEND_CPU;
  nd.device = cfg->device;
  nd.max_n = n;
  nd.config = nc;
  nn = nn_create(&nd);
  if (!nn)
    dief("nn_create: %s", nn_last_error());
  if (rl_ckpt_load(nn, cfg->checkpoint) != 0)
    dief("nn_load: %s", nn_last_error());

  /* Burn-in noop (trainer first stored step): populate cam/scal/status.
   * First policy obs is the post-noop frame duplicated (have_prior=0). */
  for (i = 0; i < n; ++i) {
    int32_t *a = acts + (size_t)i * POL_HEADS;
    memset(a, 0, POL_HEADS * sizeof(int32_t));
    a[0] = 1;
    a[1] = 1;
    a[2] = 1;
  }
  acts_to_rows(acts, n, act_rows);
  if (env_step(&estep, env, act_rows, cfg->action_repeat, cam, depth, edge,
               scal6, rew, done_buf, pose, status) != 0)
    die("burn-in step failed");
  for (i = 0; i < n; ++i) {
    int k;
    for (k = 0; k < 9; ++k)
      best9[(size_t)i * 9 + (size_t)k] = status[(size_t)i * ENV_STATUS + k];
    /* First populated best[] after assign+reset+burn-in. Stage-0
     * reporting still uses absolute reached (print unchanged). */
    base_stg[i] = inv_stage(best9 + (size_t)i * 9);
    have_prior[i] = 0;
    ep_dec[i] = 0;
    finished[i] = 0;
    reached[i] = 0;
  }

  for (dec = 0; dec < ep_lim; ++dec) {
    pack_obs(cam, depth, edge, scal6, pose, status, ep_dec, ep_lim, have_prior,
             prior_frame, n, planes, scalars, frame_scratch);
    if (nn_forward(nn, planes, scalars, n, logits, values) != 0)
      dief("nn_forward: %s", nn_last_error());
    if (nn_sample(nn, logits, n, NN_SAMPLE_GUMBEL, acts, logp, NULL) != 0)
      dief("nn_sample: %s", nn_last_error());
    acts_to_rows(acts, n, act_rows);
    if (env_step(&estep, env, act_rows, cfg->action_repeat, cam, depth, edge,
                 scal6, rew, done_buf, pose, status) != 0)
      die("blaze_step_full failed");

    all_done = 1;
    for (i = 0; i < n; ++i) {
      int k;
      uint8_t *frame;
      uint8_t *prior;
      int stg;
      if (finished[i])
        continue;
      for (k = 0; k < 9; ++k) {
        int v = status[(size_t)i * ENV_STATUS + k];
        if (v > best9[(size_t)i * 9 + (size_t)k])
          best9[(size_t)i * 9 + (size_t)k] = v;
      }
      frame = frame_scratch + (size_t)i * ENV_N_PLANES * ENV_NPIX;
      prior = prior_frame + (size_t)i * ENV_N_PLANES * ENV_NPIX;
      memcpy(prior, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
      have_prior[i] = 1;
      ep_dec[i] += 1;
      if (best9[(size_t)i * 9 + CR_IX_TORCH] >= 1 || done_buf[i] == 1) {
        reached[i] = CR_N_STAGES;
        finished[i] = 1;
        continue;
      }
      stg = cr_stage_of_best(best9 + (size_t)i * 9);
      if (done_buf[i] > 0 || ep_dec[i] >= ep_lim) {
        reached[i] = stg;
        finished[i] = 1;
        continue;
      }
      reached[i] = stg;
      all_done = 0;
    }
    if (all_done)
      break;
    if ((dec + 1) % 50 == 0 || dec + 1 == ep_lim) {
      int nfin = 0;
      for (i = 0; i < n; ++i)
        nfin += (int)finished[i];
      fprintf(stderr, "eval: decision %d/%d finished=%d/%d\n", dec + 1, ep_lim,
              nfin, n);
    }
  }
  for (i = 0; i < n; ++i) {
    if (!finished[i])
      reached[i] = cr_stage_of_best(best9 + (size_t)i * 9);
  }

  for (i = 0; i < cfg->nseeds; ++i) {
    seed_best[i] = 0;
    seed_base[i] = 0;
  }
  for (i = 0; i < n; ++i) {
    int loaded = i / cfg->tries;
    int si = loaded_si[loaded];
    if (reached[i] > seed_best[si])
      seed_best[si] = reached[i];
    if ((i % cfg->tries) == 0)
      seed_base[si] = base_stg[i];
  }

  if (results) {
    for (i = 0; i < cfg->nseeds; ++i) {
      results[i].skip = skip[i] ? 1 : 0;
      results[i].base = seed_base[i];
      results[i].abs_end = seed_best[i];
      results[i].torch = seed_best[i] >= CR_N_STAGES;
    }
  }
  print_stage_report(cfg, stage_k, rng_seed, skip, seed_best, seed_base);
  rc_out = 0;

fail:
  free(assign);
  free(cam);
  free(depth);
  free(edge);
  free(scal6);
  free(rew);
  free(done_buf);
  free(pose);
  free(status);
  free(act_rows);
  free(planes);
  free(scalars);
  free(acts);
  free(logp);
  free(values);
  free(logits);
  free(prior_frame);
  free(have_prior);
  free(frame_scratch);
  free(ep_dec);
  free(finished);
  free(best9);
  free(reached);
  free(base_stg);
  if (nn)
    nn_destroy(nn);
#if BLAZE_RL_HAVE_CUDA
  if (is_cuda)
    env_cuda_stage_destroy(&stage);
#endif
#if BLAZE_RL_HAVE_METAL
  if (is_metal)
    env_metal_obs_destroy(&metal_obs);
#endif
  if (env && fns.destroy)
    fns.destroy(env);
  blaze_fns_close(&fns);
  return rc_out;
}
