/* train_config.c - flat key=value parser for the native trainer. */
#include "train_config.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copy only when the full source fits (including NUL). Rejects overlong. */
static int str_copy_fit(char *dst, size_t cap, const char *src) {
  size_t n = strlen(src);
  if (n + 1 > cap)
    return 0;
  memcpy(dst, src, n + 1);
  return 1;
}

static int p_ll(const char *v, long long *out) {
  char *end = NULL;
  long long x;
  errno = 0;
  x = strtoll(v, &end, 10);
  if (errno || !end || end == v || *end)
    return 0;
  *out = x;
  return 1;
}

static int p_int(const char *v, int *out) {
  long long x;
  if (!p_ll(v, &x))
    return 0;
  if (x < -2147483647LL - 1 || x > 2147483647LL)
    return 0;
  *out = (int)x;
  return 1;
}

/* Unsigned 64: reject leading sign, whitespace-only, and non-digits. */
static int p_u64(const char *v, uint64_t *out) {
  char *end = NULL;
  unsigned long long x;
  if (!v || !v[0])
    return 0;
  if (v[0] == '-' || v[0] == '+')
    return 0;
  errno = 0;
  x = strtoull(v, &end, 10);
  if (errno || !end || end == v || *end)
    return 0;
  *out = (uint64_t)x;
  return 1;
}

static int p_f32(const char *v, float *out) {
  char *end = NULL;
  float x;
  errno = 0;
  x = strtof(v, &end);
  if (errno || !end || end == v || *end)
    return 0;
  if (!isfinite(x))
    return 0;
  *out = x;
  return 1;
}

/* Bools accept 0/1 only. */
static int p_bool01(const char *v, int *out) {
  if (!strcmp(v, "0")) {
    *out = 0;
    return 1;
  }
  if (!strcmp(v, "1")) {
    *out = 1;
    return 1;
  }
  return 0;
}

void tr_cfg_defaults(TrainConfig *c) {
  if (!c)
    return;
  memset(c, 0, sizeof(*c));
  (void)str_copy_fit(c->backend, sizeof(c->backend), "cpu");
  c->device = 0;
  c->n_envs = 2;
  (void)str_copy_fit(c->fixture, sizeof(c->fixture),
                     "verify/fixtures/port/s10_t0_r64_no_liquid.bsnp");
  c->rollout_steps = 4;
  c->action_repeat = 4;
  c->lr = 3e-4f;
  c->ppo_clip = 0.2f;
  c->value_coef = 0.5f;
  c->entropy_coef = 0.01f;
  c->grad_limit = 0.5f;
  c->gamma = 0.995f;
  c->lam = 0.95f;
  c->epochs = 1;
  c->mb = 0;
  c->max_chunks = 1;
  c->max_ticks = 0;
  c->max_wall = 0.f;
  c->success_item = 0;
  c->t0_share = 0.30f;
  c->cap_refresh = 25;
  (void)str_copy_fit(c->train_seeds, sizeof(c->train_seeds), "fixture");
  (void)str_copy_fit(c->snaps_dir, sizeof(c->snaps_dir), "blaze/rl/out/snaps");
  c->stage_snaps = 0;
  c->lr_floor = 1e-4f;
  c->lr_decay_ticks = 1500000000LL;
  c->ep_dec = 1500;
  c->ckpt_ticks = 2000000;
  c->seed = 0;
  (void)str_copy_fit(c->checkpoint, sizeof(c->checkpoint),
                     "out/blaze/rl/ppo_ckpt.bin");
  c->init_from[0] = '\0';
  c->metal_max_cells = 2097152;
  (void)str_copy_fit(c->metallib, sizeof(c->metallib), "auto");
  c->ktime = 0;
  c->stage_time = 0;
  c->legacy_recenter = 0;
  c->warp_tick = 1;
  c->op_trace = 0;
  c->no_ore_xy = 0;
  c->stack_kib = 128;
  (void)str_copy_fit(c->nn_prec, sizeof(c->nn_prec), "fast");
  (void)str_copy_fit(c->tail_mb, sizeof(c->tail_mb), "overlap");
}

int tr_cfg_set(TrainConfig *c, const char *key, const char *val) {
  if (!c || !key || !val)
    return -1;

  if (!strcmp(key, "backend")) {
    if (strcmp(val, "cpu") && strcmp(val, "cuda") && strcmp(val, "metal"))
      return -2;
    if (!str_copy_fit(c->backend, sizeof(c->backend), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "device")) {
    int t;
    if (!p_int(val, &t) || t < 0)
      return -2;
    c->device = t;
    return 0;
  }
  if (!strcmp(key, "n_envs")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->n_envs = t;
    return 0;
  }
  if (!strcmp(key, "fixture")) {
    if (!val[0])
      return -2;
    if (!str_copy_fit(c->fixture, sizeof(c->fixture), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "rollout_steps")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->rollout_steps = t;
    return 0;
  }
  if (!strcmp(key, "action_repeat")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->action_repeat = t;
    return 0;
  }
  if (!strcmp(key, "lr")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f)
      return -2;
    c->lr = t;
    return 0;
  }
  if (!strcmp(key, "ppo_clip")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f || t >= 1.f)
      return -2;
    c->ppo_clip = t;
    return 0;
  }
  if (!strcmp(key, "value_coef")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f)
      return -2;
    c->value_coef = t;
    return 0;
  }
  if (!strcmp(key, "entropy_coef")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f)
      return -2;
    c->entropy_coef = t;
    return 0;
  }
  if (!strcmp(key, "grad_limit")) {
    float t;
    if (!p_f32(val, &t) || !(t > 0.f))
      return -2;
    c->grad_limit = t;
    return 0;
  }
  if (!strcmp(key, "gamma")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f || t > 1.f)
      return -2;
    c->gamma = t;
    return 0;
  }
  if (!strcmp(key, "lam")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f || t > 1.f)
      return -2;
    c->lam = t;
    return 0;
  }
  if (!strcmp(key, "epochs")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->epochs = t;
    return 0;
  }
  if (!strcmp(key, "mb")) {
    int t;
    if (!p_int(val, &t) || t < 0)
      return -2;
    c->mb = t;
    return 0;
  }
  if (!strcmp(key, "max_chunks")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->max_chunks = t;
    return 0;
  }
  if (!strcmp(key, "max_ticks")) {
    long long t;
    if (!p_ll(val, &t) || t < 0)
      return -2;
    c->max_ticks = t;
    return 0;
  }
  if (!strcmp(key, "max_wall")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f)
      return -2;
    c->max_wall = t;
    return 0;
  }
  if (!strcmp(key, "success_item")) {
    int t;
    if (!p_int(val, &t) || t < 0)
      return -2;
    c->success_item = t;
    return 0;
  }
  if (!strcmp(key, "t0_share")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f || t > 1.f)
      return -2;
    c->t0_share = t;
    return 0;
  }
  if (!strcmp(key, "cap_refresh")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->cap_refresh = t;
    return 0;
  }
  if (!strcmp(key, "train_seeds")) {
    if (!val[0])
      return -2;
    if (!str_copy_fit(c->train_seeds, sizeof(c->train_seeds), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "snaps_dir")) {
    if (!val[0])
      return -2;
    if (!str_copy_fit(c->snaps_dir, sizeof(c->snaps_dir), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "stage_snaps")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->stage_snaps = t;
    return 0;
  }
  if (!strcmp(key, "lr_floor")) {
    float t;
    if (!p_f32(val, &t) || t < 0.f)
      return -2;
    c->lr_floor = t;
    return 0;
  }
  if (!strcmp(key, "lr_decay_ticks")) {
    long long t;
    if (!p_ll(val, &t) || t <= 0)
      return -2;
    c->lr_decay_ticks = t;
    return 0;
  }
  if (!strcmp(key, "ep_dec")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->ep_dec = t;
    return 0;
  }
  if (!strcmp(key, "ckpt_ticks")) {
    long long t;
    if (!p_ll(val, &t) || t <= 0)
      return -2;
    c->ckpt_ticks = t;
    return 0;
  }
  if (!strcmp(key, "seed")) {
    uint64_t t;
    if (!p_u64(val, &t))
      return -2;
    c->seed = t;
    return 0;
  }
  if (!strcmp(key, "checkpoint")) {
    if (!val[0])
      return -2;
    if (!str_copy_fit(c->checkpoint, sizeof(c->checkpoint), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "init_from")) {
    /* Empty is off. */
    if (!str_copy_fit(c->init_from, sizeof(c->init_from), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "metal_max_cells")) {
    int t;
    if (!p_int(val, &t) || t <= 0)
      return -2;
    c->metal_max_cells = t;
    return 0;
  }
  if (!strcmp(key, "metallib")) {
    /* Non-empty only. "auto" selects the owned default search path at run. */
    if (!val[0])
      return -2;
    if (!str_copy_fit(c->metallib, sizeof(c->metallib), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "ktime")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->ktime = t;
    return 0;
  }
  if (!strcmp(key, "stage_time")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->stage_time = t;
    return 0;
  }
  if (!strcmp(key, "legacy_recenter")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->legacy_recenter = t;
    return 0;
  }
  if (!strcmp(key, "warp_tick")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->warp_tick = t;
    return 0;
  }
  if (!strcmp(key, "op_trace")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->op_trace = t;
    return 0;
  }
  if (!strcmp(key, "no_ore_xy")) {
    int t;
    if (!p_bool01(val, &t))
      return -2;
    c->no_ore_xy = t;
    return 0;
  }
  if (!strcmp(key, "stack_kib")) {
    int t;
    if (!p_int(val, &t) || t <= 0 || t > 1024)
      return -2;
    c->stack_kib = t;
    return 0;
  }
  if (!strcmp(key, "nn_prec")) {
    if (strcmp(val, "fast") && strcmp(val, "f32"))
      return -2;
    if (!str_copy_fit(c->nn_prec, sizeof(c->nn_prec), val))
      return -2;
    return 0;
  }
  if (!strcmp(key, "tail_mb")) {
    if (strcmp(val, "overlap") && strcmp(val, "drop") && strcmp(val, "partial"))
      return -2;
    if (!str_copy_fit(c->tail_mb, sizeof(c->tail_mb), val))
      return -2;
    return 0;
  }
  return -1;
}

int tr_cfg_load_file(TrainConfig *c, const char *path) {
  char line[64 + TR_CFG_STR_MAX + 32];
  int lineno = 0;
  FILE *f;

  if (!c) {
    fprintf(stderr, "config: null TrainConfig\n");
    return -3;
  }
  if (!path || !path[0]) {
    fprintf(stderr, "config: conf path is empty\n");
    return -3;
  }
  f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "config: cannot open conf file '%s'\n", path);
    return -3;
  }

  while (fgets(line, (int)sizeof(line), f)) {
    char *hash;
    char key[64], val[TR_CFG_STR_MAX];
    int got;
    int rc;

    lineno++;
    hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    {
      int has_eq = strchr(line, '=') != NULL;
      for (char *q = line; *q; ++q) {
        if (*q == '=')
          *q = ' ';
      }
      got = sscanf(line, "%63s %1023s", key, val);
      if (got <= 0)
        continue;
      if (got == 1) {
        if (!has_eq) {
          fprintf(stderr, "config: %s:%d: key '%s' has no value\n", path, lineno,
                  key);
          fclose(f);
          return -2;
        }
        val[0] = '\0';
      }
    }
    rc = tr_cfg_set(c, key, val);
    if (rc == -1) {
      fprintf(stderr, "config: %s:%d: unknown key '%s'\n", path, lineno, key);
      fclose(f);
      return -1;
    }
    if (rc == -2) {
      fprintf(stderr, "config: %s:%d: bad value for '%s': '%s'\n", path, lineno,
              key, val);
      fclose(f);
      return -2;
    }
  }
  fclose(f);
  return 0;
}

void tr_cfg_dump(const TrainConfig *c, FILE *out) {
  if (!c || !out)
    return;
  fprintf(out, "# blaze trainer config (blaze/rl/train_config.h)\n");
  fprintf(out, "# Set with: --conf FILE  or  --set key=value\n");
  fprintf(out, "  %-16s = %s\n", "backend", c->backend);
  fprintf(out, "  %-16s = %d\n", "device", c->device);
  fprintf(out, "  %-16s = %d\n", "n_envs", c->n_envs);
  fprintf(out, "  %-16s = %s\n", "fixture", c->fixture);
  fprintf(out, "  %-16s = %d\n", "rollout_steps", c->rollout_steps);
  fprintf(out, "  %-16s = %d\n", "action_repeat", c->action_repeat);
  fprintf(out, "  %-16s = %.9g\n", "lr", (double)c->lr);
  fprintf(out, "  %-16s = %.9g\n", "ppo_clip", (double)c->ppo_clip);
  fprintf(out, "  %-16s = %.9g\n", "value_coef", (double)c->value_coef);
  fprintf(out, "  %-16s = %.9g\n", "entropy_coef", (double)c->entropy_coef);
  fprintf(out, "  %-16s = %.9g\n", "grad_limit", (double)c->grad_limit);
  fprintf(out, "  %-16s = %.9g\n", "gamma", (double)c->gamma);
  fprintf(out, "  %-16s = %.9g\n", "lam", (double)c->lam);
  fprintf(out, "  %-16s = %d\n", "epochs", c->epochs);
  fprintf(out, "  %-16s = %d\n", "mb", c->mb);
  fprintf(out, "  %-16s = %d\n", "max_chunks", c->max_chunks);
  fprintf(out, "  %-16s = %lld\n", "max_ticks", (long long)c->max_ticks);
  fprintf(out, "  %-16s = %.9g\n", "max_wall", (double)c->max_wall);
  fprintf(out, "  %-16s = %d\n", "success_item", c->success_item);
  fprintf(out, "  %-16s = %.9g\n", "t0_share", (double)c->t0_share);
  fprintf(out, "  %-16s = %d\n", "cap_refresh", c->cap_refresh);
  fprintf(out, "  %-16s = %s\n", "train_seeds", c->train_seeds);
  fprintf(out, "  %-16s = %s\n", "snaps_dir", c->snaps_dir);
  fprintf(out, "  %-16s = %d\n", "stage_snaps", c->stage_snaps);
  fprintf(out, "  %-16s = %.9g\n", "lr_floor", (double)c->lr_floor);
  fprintf(out, "  %-16s = %lld\n", "lr_decay_ticks",
          (long long)c->lr_decay_ticks);
  fprintf(out, "  %-16s = %d\n", "ep_dec", c->ep_dec);
  fprintf(out, "  %-16s = %lld\n", "ckpt_ticks", (long long)c->ckpt_ticks);
  fprintf(out, "  %-16s = %llu\n", "seed", (unsigned long long)c->seed);
  fprintf(out, "  %-16s = %s\n", "checkpoint", c->checkpoint);
  fprintf(out, "  %-16s = %s\n", "init_from", c->init_from);
  fprintf(out, "  %-16s = %d\n", "metal_max_cells", c->metal_max_cells);
  fprintf(out, "  %-16s = %s\n", "metallib", c->metallib);
  fprintf(out, "  %-16s = %d\n", "ktime", c->ktime);
  fprintf(out, "  %-16s = %d\n", "stage_time", c->stage_time);
  fprintf(out, "  %-16s = %d\n", "legacy_recenter", c->legacy_recenter);
  fprintf(out, "  %-16s = %d\n", "warp_tick", c->warp_tick);
  fprintf(out, "  %-16s = %d\n", "op_trace", c->op_trace);
  fprintf(out, "  %-16s = %d\n", "no_ore_xy", c->no_ore_xy);
  fprintf(out, "  %-16s = %d\n", "stack_kib", c->stack_kib);
  fprintf(out, "  %-16s = %s\n", "nn_prec", c->nn_prec);
  fprintf(out, "  %-16s = %s\n", "tail_mb", c->tail_mb);
}

/* True if token looks like a CLI option, not a conf path. */
static int is_option_token(const char *s) {
  return s && s[0] == '-' && s[1] != '\0';
}

int tr_cfg_parse_argv(TrainConfig *c, int argc, char **argv) {
  const char *conf_path = NULL;
  int dump = 0;
  int i;

  if (!c)
    return -1;
  tr_cfg_defaults(c);

  /* First pass: resolve --conf (must not treat later options as the path). */
  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--conf")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "config: --conf needs a path\n");
        return -1;
      }
      conf_path = argv[++i];
      if (is_option_token(conf_path)) {
        fprintf(stderr,
                "config: --conf needs a path, got option '%s'\n", conf_path);
        return -1;
      }
      continue;
    }
    if (!strcmp(argv[i], "--dump-config"))
      dump = 1;
  }
  /* Explicit --conf PATH must exist; defaults alone when --conf is absent. */
  if (conf_path) {
    int rc = tr_cfg_load_file(c, conf_path);
    if (rc != 0)
      return rc;
  }

  for (i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (!strcmp(a, "--conf")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "config: --conf needs a path\n");
        return -1;
      }
      ++i;
      if (is_option_token(argv[i])) {
        fprintf(stderr,
                "config: --conf needs a path, got option '%s'\n", argv[i]);
        return -1;
      }
      continue;
    }
    if (!strcmp(a, "--set")) {
      const char *kv;
      const char *eq;
      char key[64];
      size_t kn;
      int rc;

      if (i + 1 >= argc) {
        fprintf(stderr, "config: --set needs key=value\n");
        return -1;
      }
      kv = argv[++i];
      eq = strchr(kv, '=');
      if (!eq || eq == kv) {
        fprintf(stderr, "config: --set expects key=value, got '%s'\n", kv);
        return -1;
      }
      kn = (size_t)(eq - kv);
      if (kn >= sizeof(key)) {
        fprintf(stderr, "config: key too long in '%s'\n", kv);
        return -1;
      }
      memcpy(key, kv, kn);
      key[kn] = '\0';
      rc = tr_cfg_set(c, key, eq + 1);
      if (rc == -1) {
        fprintf(stderr, "config: unknown key '%s'\n", key);
        return -1;
      }
      if (rc == -2) {
        fprintf(stderr, "config: bad value for '%s': '%s'\n", key, eq + 1);
        return -2;
      }
      continue;
    }
    if (!strcmp(a, "--dump-config"))
      continue;
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      fprintf(stdout,
              "usage: %s [--conf FILE] [--set key=value]... "
              "[--dump-config]\n",
              argv[0] ? argv[0] : "ppo");
      return 1;
    }
    fprintf(stderr,
            "config: unknown argument '%s' (want --conf/--set/--dump-config)\n",
            a);
    return -1;
  }

  if (dump) {
    tr_cfg_dump(c, stdout);
    return 1;
  }
  return 0;
}
