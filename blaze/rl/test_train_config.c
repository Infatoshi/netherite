/* C11 unit test for blaze/rl/train_config. No Python, no shell. */
#define _POSIX_C_SOURCE 200809L
#include "train_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void expect_eq_s(const char *a, const char *b, const char *msg) {
  if (strcmp(a, b) != 0) {
    fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, a, b);
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

static void test_defaults(void) {
  TrainConfig c;
  tr_cfg_defaults(&c);
  expect_eq_s(c.backend, "cpu", "default backend");
  expect_eq_i(c.device, 0, "default device");
  expect_eq_i(c.n_envs, 2, "default n_envs");
  expect_true(strstr(c.fixture, "s10_t0_r64_no_liquid.bsnp") != NULL,
              "default fixture");
  expect_eq_i(c.rollout_steps, 4, "default rollout_steps");
  expect_eq_i(c.action_repeat, 4, "default action_repeat");
  expect_near(c.lr, 3e-4f, 1e-6f, "default lr");
  expect_near(c.ppo_clip, 0.2f, 1e-9f, "default ppo_clip");
  expect_near(c.value_coef, 0.5f, 1e-9f, "default value_coef");
  expect_near(c.entropy_coef, 0.01f, 1e-9f, "default entropy_coef");
  expect_near(c.grad_limit, 0.5f, 1e-9f, "default grad_limit");
  expect_near(c.gamma, 0.995f, 1e-6f, "default gamma");
  expect_near(c.lam, 0.95f, 1e-6f, "default lam");
  expect_eq_i(c.epochs, 1, "default epochs");
  expect_eq_i(c.mb, 0, "default mb");
  expect_eq_i(c.max_chunks, 1, "default max_chunks");
  expect_eq_i((int)c.max_ticks, 0, "default max_ticks");
  expect_near(c.max_wall, 0.f, 1e-9f, "default max_wall");
  expect_eq_i(c.success_item, 0, "default success_item");
  expect_near(c.t0_share, 0.30f, 1e-6f, "default t0_share");
  expect_eq_i(c.cap_refresh, 25, "default cap_refresh");
  expect_eq_s(c.train_seeds, "fixture", "default train_seeds");
  expect_eq_s(c.snaps_dir, "blaze/rl/out/snaps", "default snaps_dir");
  expect_eq_i(c.stage_snaps, 0, "default stage_snaps");
  expect_near(c.lr_floor, 1e-4f, 1e-9f, "default lr_floor");
  expect_true(c.lr_decay_ticks == 1500000000LL, "default lr_decay_ticks");
  expect_eq_i(c.ep_dec, 1500, "default ep_dec");
  expect_true(c.ckpt_ticks == 2000000, "default ckpt_ticks");
  expect_eq_i((int)c.seed, 0, "default seed");
  expect_true(strstr(c.checkpoint, "ppo_ckpt.bin") != NULL,
              "default checkpoint");
  expect_eq_s(c.init_from, "", "default init_from");
  expect_eq_i(c.metal_max_cells, 2097152, "default metal_max_cells");
  expect_eq_s(c.metallib, "auto", "default metallib");
  expect_eq_i(c.ktime, 0, "default ktime");
  expect_eq_i(c.stage_time, 0, "default stage_time");
  expect_eq_i(c.legacy_recenter, 0, "default legacy_recenter");
  expect_eq_i(c.warp_tick, 1, "default warp_tick");
  expect_eq_i(c.op_trace, 0, "default op_trace");
  expect_eq_i(c.no_ore_xy, 0, "default no_ore_xy");
  expect_eq_i(c.stack_kib, 128, "default stack_kib");
  expect_eq_s(c.nn_prec, "fast", "default nn_prec");
  expect_eq_s(c.tail_mb, "overlap", "default tail_mb");
}

static void test_file_load(void) {
  char path[] = "/tmp/tr_cfg_XXXXXX";
  int fd;
  FILE *f;
  TrainConfig c;
  int rc;

  fd = mkstemp(path);
  expect_true(fd >= 0, "mkstemp");
  if (fd < 0)
    return;
  f = fdopen(fd, "w");
  expect_true(f != NULL, "fdopen");
  if (!f) {
    close(fd);
    unlink(path);
    return;
  }
  fprintf(f,
          "# test conf\n"
          "backend = cpu\n"
          "n_envs = 8\n"
          "lr = 1e-3\n"
          "gamma = 0.9\n"
          "seed = 42\n"
          "warp_tick = 0\n"
          "op_trace = 1\n"
          "stage_snaps = 1\n"
          "init_from =\n");
  fclose(f);

  tr_cfg_defaults(&c);
  rc = tr_cfg_load_file(&c, path);
  expect_eq_i(rc, 0, "load_file rc");
  expect_eq_i(c.n_envs, 8, "file n_envs");
  expect_near(c.lr, 1e-3f, 1e-9f, "file lr");
  expect_near(c.gamma, 0.9f, 1e-6f, "file gamma");
  expect_eq_i((int)c.seed, 42, "file seed");
  expect_eq_s(c.backend, "cpu", "file backend");
  expect_eq_i(c.warp_tick, 0, "file warp_tick");
  expect_eq_i(c.op_trace, 1, "file op_trace");
  expect_eq_i(c.stage_snaps, 1, "file stage_snaps");
  expect_eq_s(c.init_from, "", "file init_from empty");
  unlink(path);
}

static void test_set_override(void) {
  TrainConfig c;
  char *argv[] = {"test", "--set", "n_envs=16", "--set", "lr=0.001",
                  "--set", "seed=7", "--set", "checkpoint=out/x.bin",
                  "--set", "init_from=out/warm.bin", "--set", "stage_snaps=1"};
  int rc = tr_cfg_parse_argv(&c, 13, argv);
  expect_eq_i(rc, 0, "parse_argv override");
  expect_eq_i(c.n_envs, 16, "override n_envs");
  expect_near(c.lr, 0.001f, 1e-9f, "override lr");
  expect_eq_i((int)c.seed, 7, "override seed");
  expect_eq_s(c.checkpoint, "out/x.bin", "override checkpoint");
  expect_eq_s(c.init_from, "out/warm.bin", "override init_from");
  expect_eq_i(c.stage_snaps, 1, "override stage_snaps");
}

static void test_unknown_key(void) {
  TrainConfig c;
  int rc;
  tr_cfg_defaults(&c);
  rc = tr_cfg_set(&c, "not_a_key", "1");
  expect_eq_i(rc, -1, "unknown key -> -1");

  {
    char *argv[] = {"test", "--set", "bogus=1"};
    rc = tr_cfg_parse_argv(&c, 3, argv);
    expect_true(rc != 0, "parse_argv unknown key fails");
  }
}

static void test_bad_value(void) {
  TrainConfig c;
  int rc;
  tr_cfg_defaults(&c);
  rc = tr_cfg_set(&c, "n_envs", "0");
  expect_eq_i(rc, -2, "n_envs=0 bad");
  rc = tr_cfg_set(&c, "n_envs", "abc");
  expect_eq_i(rc, -2, "n_envs=abc bad");
  rc = tr_cfg_set(&c, "backend", "tpu");
  expect_eq_i(rc, -2, "backend=tpu bad");
  rc = tr_cfg_set(&c, "lr", "-1");
  expect_eq_i(rc, -2, "lr=-1 bad");
  rc = tr_cfg_set(&c, "ppo_clip", "1.5");
  expect_eq_i(rc, -2, "ppo_clip=1.5 bad");
  rc = tr_cfg_set(&c, "grad_limit", "0");
  expect_eq_i(rc, -2, "grad_limit=0 bad");
  rc = tr_cfg_set(&c, "gamma", "1.5");
  expect_eq_i(rc, -2, "gamma=1.5 bad");
  rc = tr_cfg_set(&c, "lam", "1.5");
  expect_eq_i(rc, -2, "lam=1.5 bad");
  rc = tr_cfg_set(&c, "epochs", "0");
  expect_eq_i(rc, -2, "epochs=0 bad");
  rc = tr_cfg_set(&c, "mb", "-1");
  expect_eq_i(rc, -2, "mb=-1 bad");
  rc = tr_cfg_set(&c, "max_chunks", "0");
  expect_eq_i(rc, -2, "max_chunks=0 bad");
  rc = tr_cfg_set(&c, "max_ticks", "-1");
  expect_eq_i(rc, -2, "max_ticks=-1 bad");
  rc = tr_cfg_set(&c, "success_item", "-1");
  expect_eq_i(rc, -2, "success_item=-1 bad");
  rc = tr_cfg_set(&c, "t0_share", "1.5");
  expect_eq_i(rc, -2, "t0_share=1.5 bad");
  rc = tr_cfg_set(&c, "train_seeds", "");
  expect_eq_i(rc, -2, "train_seeds empty bad");
  rc = tr_cfg_set(&c, "ep_dec", "0");
  expect_eq_i(rc, -2, "ep_dec=0 bad");
  rc = tr_cfg_set(&c, "ktime", "2");
  expect_eq_i(rc, -2, "ktime=2 bad");
  rc = tr_cfg_set(&c, "warp_tick", "true");
  expect_eq_i(rc, -2, "warp_tick=true bad");
  rc = tr_cfg_set(&c, "stack_kib", "0");
  expect_eq_i(rc, -2, "stack_kib=0 bad");
  rc = tr_cfg_set(&c, "stack_kib", "-8");
  expect_eq_i(rc, -2, "stack_kib=-8 bad");
  rc = tr_cfg_set(&c, "stack_kib", "2048");
  expect_eq_i(rc, -2, "stack_kib=2048 bad");
  rc = tr_cfg_set(&c, "stack_kib", "96");
  expect_eq_i(rc, 0, "stack_kib=96 ok");
  expect_eq_i(c.stack_kib, 96, "stack_kib=96 applied");
  rc = tr_cfg_set(&c, "metal_max_cells", "0");
  expect_eq_i(rc, -2, "metal_max_cells=0 bad");
  rc = tr_cfg_set(&c, "metal_max_cells", "-1");
  expect_eq_i(rc, -2, "metal_max_cells=-1 bad");
  rc = tr_cfg_set(&c, "metal_max_cells", "abc");
  expect_eq_i(rc, -2, "metal_max_cells=abc bad");
  rc = tr_cfg_set(&c, "metallib", "");
  expect_eq_i(rc, -2, "metallib empty bad");
  rc = tr_cfg_set(&c, "stage_snaps", "2");
  expect_eq_i(rc, -2, "stage_snaps=2 bad");
  rc = tr_cfg_set(&c, "stage_snaps", "true");
  expect_eq_i(rc, -2, "stage_snaps=true bad");
  rc = tr_cfg_set(&c, "nn_prec", "fp32");
  expect_eq_i(rc, -2, "nn_prec=fp32 bad");
  rc = tr_cfg_set(&c, "nn_prec", "f32");
  expect_eq_i(rc, 0, "nn_prec=f32 ok");
  expect_eq_s(c.nn_prec, "f32", "nn_prec=f32 applied");
  rc = tr_cfg_set(&c, "nn_prec", "fast");
  expect_eq_i(rc, 0, "nn_prec=fast ok");
  expect_eq_s(c.nn_prec, "fast", "nn_prec=fast applied");
  rc = tr_cfg_set(&c, "tail_mb", "keep");
  expect_eq_i(rc, -2, "tail_mb=keep bad");
  rc = tr_cfg_set(&c, "tail_mb", "partial");
  expect_eq_i(rc, 0, "tail_mb=partial ok");
  expect_eq_s(c.tail_mb, "partial", "tail_mb=partial applied");
  rc = tr_cfg_set(&c, "tail_mb", "drop");
  expect_eq_i(rc, 0, "tail_mb=drop ok");
}

static void test_nonfinite_and_seed_and_overlong(void) {
  TrainConfig c;
  int rc;
  char long_path[TR_CFG_STR_MAX + 8];
  size_t i;

  tr_cfg_defaults(&c);
  rc = tr_cfg_set(&c, "lr", "nan");
  expect_eq_i(rc, -2, "lr=nan bad");
  rc = tr_cfg_set(&c, "lr", "inf");
  expect_eq_i(rc, -2, "lr=inf bad");
  rc = tr_cfg_set(&c, "gamma", "nan");
  expect_eq_i(rc, -2, "gamma=nan bad");
  rc = tr_cfg_set(&c, "seed", "-1");
  expect_eq_i(rc, -2, "seed=-1 bad");
  rc = tr_cfg_set(&c, "seed", "+3");
  expect_eq_i(rc, -2, "seed=+3 bad");

  for (i = 0; i < TR_CFG_STR_MAX + 4; ++i)
    long_path[i] = 'a';
  long_path[TR_CFG_STR_MAX + 4] = '\0';
  rc = tr_cfg_set(&c, "fixture", long_path);
  expect_eq_i(rc, -2, "overlong fixture rejected");
  rc = tr_cfg_set(&c, "checkpoint", long_path);
  expect_eq_i(rc, -2, "overlong checkpoint rejected");
  rc = tr_cfg_set(&c, "init_from", long_path);
  expect_eq_i(rc, -2, "overlong init_from rejected");
  rc = tr_cfg_set(&c, "metallib", long_path);
  expect_eq_i(rc, -2, "overlong metallib rejected");
  /* backend max is small; a long valid-looking name must fail */
  rc = tr_cfg_set(&c, "backend", "cpu_but_way_too_long_for_field");
  expect_eq_i(rc, -2, "overlong backend rejected");
}

static void test_conf_then_set(void) {
  char path[] = "/tmp/tr_cfg2_XXXXXX";
  int fd = mkstemp(path);
  FILE *f;
  TrainConfig c;
  char *argv[6];
  int rc;

  expect_true(fd >= 0, "mkstemp2");
  if (fd < 0)
    return;
  f = fdopen(fd, "w");
  if (!f) {
    close(fd);
    unlink(path);
    return;
  }
  fprintf(f, "n_envs = 4\nlr = 2e-4\n");
  fclose(f);

  argv[0] = "test";
  argv[1] = "--conf";
  argv[2] = path;
  argv[3] = "--set";
  argv[4] = "n_envs=32";
  argv[5] = NULL;
  rc = tr_cfg_parse_argv(&c, 5, argv);
  expect_eq_i(rc, 0, "conf+set rc");
  expect_eq_i(c.n_envs, 32, "--set wins over conf");
  expect_near(c.lr, 2e-4f, 1e-9f, "conf lr kept");
  unlink(path);
}

/* Every key accepted by tr_cfg_set must appear in the committed conf. */
static const char *const k_accepted_keys[] = {
    "backend",
    "device",
    "n_envs",
    "fixture",
    "world_size",
    "rollout_steps",
    "action_repeat",
    "lr",
    "ppo_clip",
    "value_coef",
    "entropy_coef",
    "grad_limit",
    "gamma",
    "lam",
    "epochs",
    "mb",
    "max_chunks",
    "max_ticks",
    "max_wall",
    "success_item",
    "t0_share",
    "cap_refresh",
    "train_seeds",
    "snaps_dir",
    "stage_snaps",
    "lr_floor",
    "lr_decay_ticks",
    "ep_dec",
    "ckpt_ticks",
    "seed",
    "checkpoint",
    "init_from",
    "metal_max_cells",
    "metallib",
    "ktime",
    "stage_time",
    "legacy_recenter",
    "warp_tick",
    "op_trace",
    "no_ore_xy",
    "stack_kib",
    "nn_prec",
    "tail_mb",
    "phase_files",
    "run_dir",
    "eval_conf",
    "eval_executable",
    "eval_every_chunks",
    "checkpoint_metric",
    "world_min_logs",
    "world_min_coal",
    "spawn_yaw_jitter",
    "spawn_pitch_jitter",
    "obs_history",
    "obs_semantic_mask",
    "obs_depth",
    "obs_edges",
    "obs_base_scalars",
    "obs_inventory",
    "obs_pose",
    "obs_clock",
    "obs_pixel_stride",
    "action_yaw_degrees",
    "action_pitch_degrees",
    "action_heads",
    "curriculum.mastery_threshold",
    "curriculum.min_episodes",
    "curriculum.history_window",
    "curriculum.stage_weight.0",
    "curriculum.stage_weight.1",
    "curriculum.stage_weight.2",
    "curriculum.stage_weight.3",
    "curriculum.stage_weight.4",
    "reward.shaping_scale",
    "reward.time_cost",
    "reward.death_penalty",
    "reward.w_log_per",
    "reward.log_clamp",
    "reward.w_plank_first",
    "reward.w_stick_first",
    "reward.w_table_first",
    "reward.w_container_open",
    "reward.w_wpick_first",
    "reward.w_cobble_per",
    "reward.cobble_clamp",
    "reward.w_spick_first",
    "reward.w_coal_first",
    "reward.w_torch_first",
    "reward.chop_dist_coef",
    "reward.chop_dist_clamp",
    "reward.chop_crosshair",
    "reward.dig_descend_coef",
    "reward.dig_stone_atk",
    "reward.dig_hold_pick",
    "reward.digprog_coef",
    "reward.coal_dist_coef",
    "reward.coal_dist_clamp",
    "reward.coal_crosshair",
    "reward.coal_crosshair_maxd",
    "reward.coal_hold_pick",
    "reward.coal_chew",
    "reward.hunt_desc",
    "reward.w_furnace_first",
    "reward.w_furnace_open",
    "reward.w_ironore_per",
    "reward.ironore_clamp",
    "reward.w_ingot_first",
    "reward.w_ipick_first",
};

static void test_committed_conf(void) {
  TrainConfig c;
  int rc;
  FILE *f;
  char line[64 + TR_CFG_STR_MAX + 32];
  size_t i, nkeys;
  int seen[sizeof(k_accepted_keys) / sizeof(k_accepted_keys[0])];
  int lineno = 0;

  nkeys = sizeof(k_accepted_keys) / sizeof(k_accepted_keys[0]);
  memset(seen, 0, sizeof(seen));

  /* Make test-config runs from blaze/rl only. No path fallback. */
  tr_cfg_defaults(&c);
  rc = tr_cfg_load_file(&c, "ppo.conf");
  expect_eq_i(rc, 0, "committed conf exists and loads");
  expect_eq_s(c.backend, "cpu", "committed backend");
  expect_eq_i(c.n_envs, 2, "committed n_envs");
  expect_eq_i(c.warp_tick, 1, "committed warp_tick");
  expect_eq_i(c.stack_kib, 128, "committed stack_kib");
  expect_near(c.gamma, 0.995f, 1e-6f, "committed gamma");
  expect_true(c.checkpoint[0] != '\0', "committed checkpoint set");
  expect_eq_s(c.init_from, "", "committed init_from off");
  expect_eq_i(c.stage_snaps, 0, "committed stage_snaps");
  expect_eq_i(c.world_size, 64, "committed world_size");
  expect_eq_i(tr_cfg_set(&c, "world_size", "0"), 0, "original world extent");
  expect_eq_i(tr_cfg_set(&c, "world_size", "32"), 0, "small world accepted");
  expect_eq_i(tr_cfg_set(&c, "world_size", "128"), 0, "large world accepted");
  expect_eq_i(tr_cfg_set(&c, "world_size", "65"), -2, "unaligned world rejected");
  expect_eq_i(tr_cfg_set(&c, "world_size", "-1"), -2, "negative world rejected");
  expect_eq_i(tr_cfg_set(&c, "world_size", "512"), -2, "oversize world rejected");
  expect_eq_i(c.metal_max_cells, 2097152, "committed metal_max_cells");
  expect_eq_s(c.metallib, "auto", "committed metallib");
  expect_eq_s(c.nn_prec, "fast", "committed nn_prec");

  /* Exact key presence: parse non-comment lines up to '=', reject duplicates. */
  f = fopen("ppo.conf", "r");
  expect_true(f != NULL, "open ppo.conf for key scan");
  if (!f)
    return;
  while (fgets(line, (int)sizeof(line), f)) {
    char *hash;
    char *eq;
    char *p;
    char *end;
    char key[64];
    size_t kn;
    int matched;

    lineno++;
    hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    p = line;
    while (*p == ' ' || *p == '\t')
      ++p;
    if (*p == '\0' || *p == '\n' || *p == '\r')
      continue;
    eq = strchr(p, '=');
    if (!eq) {
      expect_true(0, "committed conf line missing '='");
      continue;
    }
    end = eq;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
      --end;
    kn = (size_t)(end - p);
    if (kn == 0 || kn >= sizeof(key)) {
      expect_true(0, "committed conf key empty or too long");
      continue;
    }
    memcpy(key, p, kn);
    key[kn] = '\0';

    matched = 0;
    for (i = 0; i < nkeys; ++i) {
      if (strcmp(key, k_accepted_keys[i]) != 0)
        continue;
      matched = 1;
      if (seen[i]) {
        fprintf(stderr, "FAIL: duplicate key '%s' in ppo.conf line %d\n", key,
                lineno);
        g_fails++;
      } else {
        seen[i] = 1;
      }
      break;
    }
    if (!matched) {
      fprintf(stderr, "FAIL: unknown key '%s' in ppo.conf line %d\n", key,
              lineno);
      g_fails++;
    }
  }
  fclose(f);

  for (i = 0; i < nkeys; ++i) {
    if (!seen[i]) {
      fprintf(stderr, "FAIL: accepted key '%s' missing from ppo.conf\n",
              k_accepted_keys[i]);
      g_fails++;
    }
  }
}

static void test_missing_conf(void) {
  TrainConfig c;
  int rc;
  char *argv_missing[] = {"test", "--conf",
                          "/tmp/tr_cfg_does_not_exist_XXXX_nope.conf"};
  char *argv_opt[] = {"test", "--conf", "--dump-config"};

  tr_cfg_defaults(&c);
  rc = tr_cfg_load_file(&c, "/tmp/tr_cfg_does_not_exist_XXXX_nope.conf");
  expect_eq_i(rc, -3, "load_file missing -> -3");

  rc = tr_cfg_parse_argv(&c, 3, argv_missing);
  expect_true(rc != 0, "parse_argv missing conf fails");

  rc = tr_cfg_parse_argv(&c, 3, argv_opt);
  expect_true(rc != 0, "--conf --dump-config fails (option is not a path)");
}

int main(void) {
  test_defaults();
  test_file_load();
  test_set_override();
  test_unknown_key();
  test_bad_value();
  test_nonfinite_and_seed_and_overlong();
  test_conf_then_set();
  test_committed_conf();
  test_missing_conf();
  if (g_fails) {
    fprintf(stderr, "test_train_config: %d failure(s)\n", g_fails);
    return 1;
  }
  printf("test_train_config: PASS\n");
  return 0;
}
