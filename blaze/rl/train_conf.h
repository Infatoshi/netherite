/* train_conf.h - zero-dep flat key=value config for ppo_native / cgraph_train.
 *
 * Same discipline as magma/core/config: conf file + repeatable --set key=value,
 * unknown key is fatal, bools accept 0/1 only. Shared by both trainer binaries
 * so their knobs stay consistent. Header-only, no heap beyond FILE I/O stack.
 *
 * KEY NAMING: lowercase snake_case. Former env vars keep their name lowercased
 * (NATIVE_BF16 -> native_bf16, TRAIN_SEEDS -> train_seeds, BLAZE_DEV ->
 * blaze_dev). Empty string = "unset" for optional path knobs.
 */
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace netherite::train_conf {

constexpr int kStrMax = 1024;

struct Conf {
  /* rollout / optim (defaults = historical unset-env values; measure_chunks
   * default differs by binary and is set by the caller before parse). */
  int n_envs = 6144;
  int t_chunk = 32;
  int epochs = 2;
  int mb = 8192;
  int bench_warmup_chunks = 2;
  int bench_measure_chunks = 0; /* ppo_native overrides default to 1 */
  int blaze_dev = 0;
  int rng_seed = 0;
  int cap_refresh = 25;
  int success_item = 50;
  long long max_ticks = 3000000000LL;
  double max_wall = 6.5 * 3600.0;
  double lr = 3.0e-4;
  double lr_floor = 1.0e-4;
  double lr_decay_ticks = 1.5e9;
  double t0_share = 0.30;
  double ret_norm_momentum = 0.99;

  int iron_chain = 0; /* presence used to hard-error on unsupported recipes */
  int reward_json_set = 0; /* 1 if reward_json key was set (even empty) */

  /* native-only */
  int native_bf16 = 1;
  int native_bf16_update = 1;
  int native_channels_last = 0;
  int native_profile = 0;
  int native_telemetry = 0;
  int native_oracle_bf16 = 1;
  int fp32_value_head = 0;
  int value_clip = 0;
  int ret_norm = 0;
  char native_bf16_scope[kStrMax] = "full";
  char native_oracle_fixture[kStrMax] = "";
  char native_curve[kStrMax] = "";
  char native_telemetry_json[kStrMax] = "";
  char native_checkpoint[kStrMax] = "";

  /* cgraph-only */
  int cgraph_profile = 0;
  int smoke_telemetry = 0;
  char cgraph_equiv_fixture[kStrMax] = "";
  char cgraph_init[kStrMax] = "";
  char cgraph_curve[kStrMax] = "";
  char cgraph_checkpoint[kStrMax] = "";

  /* shared paths / lists */
  char train_seeds[kStrMax] = "2,3,10,14,16,20,27,29,32,44,46";
  char reward_json[kStrMax] = "";
  char coal_chew[kStrMax] = ""; /* empty = unset; else float string */
  char hunt_desc[kStrMax] = "";
};

inline void die(const char *where, const char *fmt, const char *a = "",
                const char *b = "") {
  std::fprintf(stderr, "config: %s: ", where);
  std::fprintf(stderr, fmt, a, b);
  std::fputc('\n', stderr);
  std::fprintf(stderr, "config: run with --dump-config for the full key list\n");
  std::exit(2);
}

inline void str_copy(char *dst, const char *src) {
  std::size_t n = std::strlen(src);
  if (n >= static_cast<std::size_t>(kStrMax))
    n = static_cast<std::size_t>(kStrMax) - 1;
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

inline int p_ll(const char *v, long long *out) {
  char *end = nullptr;
  errno = 0;
  long long x = std::strtoll(v, &end, 10);
  if (errno || !end || end == v || *end)
    return 0;
  *out = x;
  return 1;
}

inline int p_int(const char *v, int *out) {
  long long x;
  if (!p_ll(v, &x))
    return 0;
  if (x < -2147483647LL - 1 || x > 2147483647LL)
    return 0;
  *out = static_cast<int>(x);
  return 1;
}

inline int p_bool(const char *v, int *out) {
  if (!std::strcmp(v, "0")) {
    *out = 0;
    return 1;
  }
  if (!std::strcmp(v, "1")) {
    *out = 1;
    return 1;
  }
  return 0;
}

inline int p_f64(const char *v, double *out) {
  char *end = nullptr;
  errno = 0;
  double x = std::strtod(v, &end);
  if (errno || !end || end == v || *end)
    return 0;
  *out = x;
  return 1;
}

/* Returns 0 ok, -1 unknown key, -2 bad value. */
inline int conf_set(Conf *c, const char *key, const char *val) {
  if (!c || !key || !val)
    return -1;
#define TC_BOOL(name)                                                          \
  if (!std::strcmp(key, #name)) {                                              \
    int t;                                                                     \
    if (!p_bool(val, &t))                                                      \
      return -2;                                                               \
    c->name = t;                                                               \
    return 0;                                                                  \
  }
#define TC_INT(name)                                                           \
  if (!std::strcmp(key, #name)) {                                              \
    int t;                                                                     \
    if (!p_int(val, &t))                                                       \
      return -2;                                                               \
    c->name = t;                                                               \
    return 0;                                                                  \
  }
#define TC_I64(name)                                                           \
  if (!std::strcmp(key, #name)) {                                              \
    long long t;                                                               \
    if (!p_ll(val, &t))                                                        \
      return -2;                                                               \
    c->name = t;                                                               \
    return 0;                                                                  \
  }
#define TC_F64(name)                                                           \
  if (!std::strcmp(key, #name)) {                                              \
    double t;                                                                  \
    if (!p_f64(val, &t))                                                       \
      return -2;                                                               \
    c->name = t;                                                               \
    return 0;                                                                  \
  }
#define TC_STR(name)                                                           \
  if (!std::strcmp(key, #name)) {                                              \
    str_copy(c->name, val);                                                    \
    return 0;                                                                  \
  }

  TC_INT(n_envs)
  TC_INT(t_chunk)
  TC_INT(epochs)
  TC_INT(mb)
  TC_INT(bench_warmup_chunks)
  TC_INT(bench_measure_chunks)
  TC_INT(blaze_dev)
  TC_INT(rng_seed)
  TC_INT(cap_refresh)
  TC_INT(success_item)
  TC_I64(max_ticks)
  TC_F64(max_wall)
  TC_F64(lr)
  TC_F64(lr_floor)
  TC_F64(lr_decay_ticks)
  TC_F64(t0_share)
  TC_F64(ret_norm_momentum)
  TC_BOOL(iron_chain)
  TC_BOOL(native_bf16)
  TC_BOOL(native_bf16_update)
  TC_BOOL(native_channels_last)
  TC_BOOL(native_profile)
  TC_BOOL(native_telemetry)
  TC_BOOL(native_oracle_bf16)
  TC_BOOL(fp32_value_head)
  TC_BOOL(value_clip)
  TC_BOOL(ret_norm)
  TC_BOOL(cgraph_profile)
  TC_BOOL(smoke_telemetry)
  TC_STR(native_bf16_scope)
  TC_STR(native_oracle_fixture)
  TC_STR(native_curve)
  TC_STR(native_telemetry_json)
  TC_STR(native_checkpoint)
  TC_STR(cgraph_equiv_fixture)
  TC_STR(cgraph_init)
  TC_STR(cgraph_curve)
  TC_STR(cgraph_checkpoint)
  TC_STR(train_seeds)
  TC_STR(coal_chew)
  TC_STR(hunt_desc)
  if (!std::strcmp(key, "reward_json")) {
    str_copy(c->reward_json, val);
    c->reward_json_set = 1;
    return 0;
  }

#undef TC_BOOL
#undef TC_INT
#undef TC_I64
#undef TC_F64
#undef TC_STR
  return -1;
}

inline void conf_load_file(Conf *c, const char *path) {
  FILE *f = std::fopen(path, "r");
  if (!f)
    return; /* missing conf is normal: pure defaults */
  char line[64 + kStrMax + 32];
  int lineno = 0;
  while (std::fgets(line, sizeof line, f)) {
    lineno++;
    char *hash = std::strchr(line, '#');
    if (hash)
      *hash = '\0';
    for (char *q = line; *q; ++q)
      if (*q == '=')
        *q = ' ';
    char key[64], val[kStrMax];
    int got = std::sscanf(line, "%63s %1023s", key, val);
    if (got <= 0)
      continue;
    if (got == 1) {
      std::fclose(f);
      die(path, "key '%s' has no value", key);
    }
    int rc = conf_set(c, key, val);
    if (rc == -1) {
      std::fclose(f);
      die(path, "unknown key '%s'", key);
    }
    if (rc == -2) {
      std::fclose(f);
      die(path, "bad value for '%s': '%s'", key, val);
    }
  }
  std::fclose(f);
}

inline void conf_dump(const Conf *c, FILE *out) {
  std::fprintf(out, "# trainer config (blaze/rl/train_conf.h)\n");
  std::fprintf(out, "# Set with: --conf FILE  or  --set key=value\n");
#define ROW_I(name)                                                            \
  std::fprintf(out, "  %-24s = %d\n", #name, c->name)
#define ROW_LL(name)                                                           \
  std::fprintf(out, "  %-24s = %lld\n", #name,                  \
               static_cast<long long>(c->name))
#define ROW_F(name)                                                            \
  std::fprintf(out, "  %-24s = %g\n", #name, c->name)
#define ROW_S(name)                                                            \
  std::fprintf(out, "  %-24s = %s\n", #name, c->name[0] ? c->name : "\"\"")
  ROW_I(n_envs);
  ROW_I(t_chunk);
  ROW_I(epochs);
  ROW_I(mb);
  ROW_I(bench_warmup_chunks);
  ROW_I(bench_measure_chunks);
  ROW_I(blaze_dev);
  ROW_I(rng_seed);
  ROW_I(cap_refresh);
  ROW_I(success_item);
  ROW_LL(max_ticks);
  ROW_F(max_wall);
  ROW_F(lr);
  ROW_F(lr_floor);
  ROW_F(lr_decay_ticks);
  ROW_F(t0_share);
  ROW_F(ret_norm_momentum);
  ROW_I(iron_chain);
  ROW_I(native_bf16);
  ROW_I(native_bf16_update);
  ROW_I(native_channels_last);
  ROW_I(native_profile);
  ROW_I(native_telemetry);
  ROW_I(native_oracle_bf16);
  ROW_I(fp32_value_head);
  ROW_I(value_clip);
  ROW_I(ret_norm);
  ROW_I(cgraph_profile);
  ROW_I(smoke_telemetry);
  ROW_S(native_bf16_scope);
  ROW_S(native_oracle_fixture);
  ROW_S(native_curve);
  ROW_S(native_telemetry_json);
  ROW_S(native_checkpoint);
  ROW_S(cgraph_equiv_fixture);
  ROW_S(cgraph_init);
  ROW_S(cgraph_curve);
  ROW_S(cgraph_checkpoint);
  ROW_S(train_seeds);
  ROW_S(reward_json);
  ROW_S(coal_chew);
  ROW_S(hunt_desc);
#undef ROW_I
#undef ROW_LL
#undef ROW_F
#undef ROW_S
}

/* Parse argv. On --dump-config prints and exits 0. Unknown --set key dies.
 * Returns Conf with defaults + conf file + --set applied. measure_chunks
 * default is left at 0; caller may pre-seed before calling. */
inline Conf parse_argv(int argc, char **argv, Conf seed = Conf{}) {
  Conf c = seed;
  const char *conf_path = nullptr;
  /* first pass: find --conf only (load before --set so --set wins) */
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--conf") && i + 1 < argc) {
      conf_path = argv[++i];
    }
  }
  if (conf_path)
    conf_load_file(&c, conf_path);
  else
    conf_load_file(&c, "blaze_train.conf"); /* optional cwd default */

  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (!std::strcmp(a, "--conf")) {
      if (i + 1 >= argc)
        die("argv", "--conf needs a path");
      ++i;
      continue; /* already applied */
    }
    if (!std::strcmp(a, "--set")) {
      if (i + 1 >= argc)
        die("argv", "--set needs key=value");
      const char *kv = argv[++i];
      const char *eq = std::strchr(kv, '=');
      if (!eq || eq == kv)
        die("argv", "--set expects key=value, got '%s'", kv);
      char key[64];
      std::size_t kn = static_cast<std::size_t>(eq - kv);
      if (kn >= sizeof key)
        die("argv", "key too long in '%s'", kv);
      std::memcpy(key, kv, kn);
      key[kn] = '\0';
      int rc = conf_set(&c, key, eq + 1);
      if (rc == -1)
        die("argv", "unknown key '%s'", key);
      if (rc == -2)
        die("argv", "bad value for '%s': '%s'", key, eq + 1);
      continue;
    }
    if (!std::strcmp(a, "--dump-config")) {
      conf_dump(&c, stdout);
      std::exit(0);
    }
    if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
      std::fprintf(stdout,
                   "usage: %s [--conf FILE] [--set key=value]... "
                   "[--dump-config]\n",
                   argv[0]);
      std::exit(0);
    }
    die("argv", "unknown argument '%s' (want --conf/--set/--dump-config)", a);
  }
  return c;
}

inline std::vector<int> parse_seeds(const char *text) {
  std::vector<int> result;
  if (!text || !*text)
    return result;
  std::string s(text);
  std::size_t begin = 0;
  while (begin < s.size()) {
    std::size_t end = s.find(',', begin);
    result.push_back(std::stoi(s.substr(begin, end - begin)));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return result;
}

} // namespace netherite::train_conf
