#include "eval_config.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
static const int kCanonSeeds[] = {2,  3,  10, 11, 14, 16, 20,
                                  27, 29, 32, 33, 44, 46};
static const int kCanonNSeeds = (int)(sizeof(kCanonSeeds) / sizeof(kCanonSeeds[0]));
static int str_copy_fit(char *dst, size_t cap, const char *src) {
  size_t n = strlen(src);
  if (n + 1 > cap)
    return 0;
  memcpy(dst, src, n + 1);
  return 1;
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
  const char *p = s;
  int n = 0;
  if (!p) return -1;
  while (isspace((unsigned char)*p)) ++p;
  if (!*p) return -1;
  for (;;) {
    char *end;
    errno = 0;
    long v = strtol(p, &end, 10);
    if (errno || end == p || v < 0 || v > INT_MAX || n >= cap) return -1;
    out[n++] = (int)v;
    p = end;
    int space = isspace((unsigned char)*p);
    while (isspace((unsigned char)*p)) ++p;
    if (!*p) return n;
    if (*p == ',') {
      ++p;
      while (isspace((unsigned char)*p)) ++p;
      if (!*p || *p == ',') return -1;
    } else if (!space) return -1;
  }
}

void eval_cfg_defaults(EvalCfg *c) {
  int i;
  memset(c, 0, sizeof(*c));
  (void)str_copy_fit(c->backend, sizeof(c->backend), "cpu");
  c->device = 0;
  (void)str_copy_fit(c->snaps_dir, sizeof(c->snaps_dir), "blaze/rl/out/snaps");
  c->nseeds = kCanonNSeeds;
  for (i = 0; i < kCanonNSeeds; ++i)
    c->seeds[i] = kCanonSeeds[i];
  policy_io_default(&c->policy);
  strcpy(c->report, "out/blaze/rl/eval.json");
  c->world_size = 64;
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

int eval_cfg_set(EvalCfg *c, const char *key, const char *val) {
  if (!c || !key || !val)
    return -1;
  if (!strcmp(key, "heldout_seeds")) key = "seeds";
  if (!strcmp(key, "start_fixture")) key = "fixture";
  if (!strcmp(key, "report_path")) key = "report";
  if (!strcmp(key, "episodes_per_seed")) key = "tries";
  if (!strcmp(key, "ep_dec")) key = "episode_decisions";
  if (!strcmp(key, "fixture") || !strcmp(key, "report")) {
    char *dst = !strcmp(key, "fixture") ? c->fixture : c->report;
    return str_copy_fit(dst, EVAL_STR_MAX, val) ? 0 : -2;
  }
  if (!strcmp(key, "world_size")) {
    int n;
    if (!parse_int(val, &n, 0, 256) || (n && (n < 32 || n % 16))) return -2;
    c->world_size = n; return 0;
  }
  if (!strcmp(key, "episode_decisions")) {
    return parse_int(val, &c->episode_decisions, 1, 1000000) ? 0 : -2;
  }
  if (!strcmp(key, "deterministic") || !strcmp(key, "allow_missing")) {
    int *dst = !strcmp(key, "deterministic") ? &c->deterministic : &c->allow_missing;
    return parse_int(val, dst, 0, 1) ? 0 : -2;
  }
  const char *knobs[] = {"ktime", "stage_time", "legacy_recenter", "warp_tick", "op_trace", "no_ore_xy", "stack_kib"};
  int *fields[] = {&c->ktime, &c->stage_time, &c->legacy_recenter, &c->warp_tick, &c->op_trace, &c->no_ore_xy, &c->stack_kib};
  for (int i = 0; i < 7; ++i) if (!strcmp(key, knobs[i]))
    return parse_int(val, fields[i], i == 6 ? 1 : 0, i == 6 ? 4096 : 1) ? 0 : -2;
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
    if (!parse_int(val, &c->ep_ticks, 1, INT_MAX)) return -2;
    c->episode_decisions = 0;
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
  char err[256];
  int rc = policy_io_set(&c->policy, key, val, err, sizeof err);
  return rc == 0 ? 0 : rc == 1 ? -1 : -2;
}

void eval_cfg_dump(const EvalCfg *c, FILE *out) {
  int i;
  fprintf(out, "fixture = %s\nreport = %s\nworld_size = %d\nepisode_decisions = %d\ndeterministic = %d\nallow_missing = %d\n", c->fixture, c->report, c->world_size, c->episode_decisions ? c->episode_decisions : c->ep_ticks / c->action_repeat, c->deterministic, c->allow_missing);
  policy_io_dump(&c->policy, out);
  fprintf(out, "ktime = %d\nstage_time = %d\nlegacy_recenter = %d\nwarp_tick = %d\nop_trace = %d\nno_ore_xy = %d\nstack_kib = %d\n", c->ktime, c->stage_time, c->legacy_recenter, c->warp_tick, c->op_trace, c->no_ore_xy, c->stack_kib);
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

void eval_usage(void) {
  fprintf(stderr,
          "usage: eval --checkpoint PATH [options]\n"
          "  --conf PATH         strict key=value recipe\n"
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
          "missing snapshots fail unless allow_missing=1; zero episodes always fails.\n"
          "stage all runs 0..4 then a ladder table of new milestones.\n"
          "magma closed: net reads Magma --rl-bin BOLR (64x36 oc_pixel).\n"
          "magma replay: net reads blaze_cpu; actions replay on Magma.\n");
}

int eval_cfg_parse_argv(EvalCfg *c, int argc, char **argv, int *dump) {
  int i;
  eval_cfg_defaults(c);
  *dump = 0;
  for (i = 1; i < argc; ++i) if (!strcmp(argv[i], "--conf")) {
    char err[2048];
    if (++i >= argc || eval_cfg_load(c, argv[i], err, sizeof err)) {
      fprintf(stderr, "eval: config error: %s\n", i < argc ? err : "missing path");
      return -1;
    }
  }
  for (i = 1; i < argc; ++i) {
    const char *a = argv[i];
    const char *key = NULL;
    const char *val = NULL;
    char tmpkey[32];
    if (!strcmp(a, "--conf")) { ++i; continue; }
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      eval_usage();
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
      if (!eq || eq == argv[i])
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
      int rc = eval_cfg_set(c, key, val);
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

static char *trim(char *s) {
  while (isspace((unsigned char)*s)) ++s;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
  return s;
}

int eval_cfg_load(EvalCfg *c, const char *path, char *err, size_t cap) {
  FILE *f = fopen(path, "r");
  char line[4096];
  int number = 0, rc = 0;
  if (!f) { snprintf(err, cap, "cannot read %s", path); return -1; }
  while (fgets(line, sizeof line, f)) {
    ++number;
    if (!strchr(line, '\n') && !feof(f)) { rc = -1; break; }
    char *key = trim(line), *eq;
    if (!*key || *key == '#') continue;
    eq = strchr(key, '=');
    if (!eq || eq == key) { rc = -1; break; }
    *eq++ = 0;
    if (eval_cfg_set(c, trim(key), trim(eq))) { rc = -1; break; }
  }
  if (ferror(f)) rc = -1;
  if (fclose(f)) rc = -1;
  if (rc) snprintf(err, cap, "%s:%d: invalid or unknown configuration", path, number);
  return rc;
}

int eval_cfg_validate(EvalCfg *c, char *err, size_t cap) {
  if (!c || c->action_repeat < 1 || c->action_repeat > 64 ||
      c->episode_decisions < 0 || c->nseeds < 1 || c->nseeds > EVAL_MAX_SEEDS ||
      c->tries < 1 || c->tries > EVAL_MAX_TRIES) {
    snprintf(err, cap, "invalid evaluation limits"); return -1;
  }
  if (c->episode_decisions) {
    if (c->episode_decisions > INT_MAX / c->action_repeat) {
      snprintf(err, cap, "episode_decisions*action_repeat overflows"); return -1;
    }
    c->ep_ticks = c->episode_decisions * c->action_repeat;
  }
  if (c->ep_ticks < c->action_repeat || c->ep_ticks % c->action_repeat) {
    snprintf(err, cap, "ep_ticks must be divisible by action_repeat"); return -1;
  }
  if (!c->report[0] || c->nseeds <= 0 || c->tries <= 0) {
    snprintf(err, cap, "report and positive sample counts are required"); return -1;
  }
  for (int i = 0; i < c->nseeds; ++i)
    for (int j = 0; j < i; ++j) if (c->seeds[i] == c->seeds[j]) {
      snprintf(err, cap, "duplicate evaluation seed %d", c->seeds[i]); return -1;
    }
  if (c->fixture[0] && (c->nseeds != 1 || c->stage == EVAL_STAGE_ALL)) {
    snprintf(err, cap, "fixture requires exactly one seed and one stage"); return -1;
  }
  return policy_io_validate(&c->policy, err, cap);
}
