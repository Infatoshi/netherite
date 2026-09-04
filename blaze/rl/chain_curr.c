/* Per-seed frontier curriculum. Port of StageCurriculum in ppo_chain_cu.py. */
#include "chain_curr.h"

#include "chain_reward.h"

#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

void cr_curr_config_defaults(CrCurrConfig *cfg) {
  int i;
  if (!cfg) return;
  memset(cfg, 0, sizeof(*cfg));
  cfg->mastery_threshold = 0.6f;
  cfg->min_episodes = CR_CURR_MIN_EPS;
  cfg->history_window = CR_CURR_WIN;
  for (i = 0; i < CR_N_STAGES; ++i) cfg->stage_weights[i] = 1.f;
  for (i = 0; i < CR_CURR_MAX_SEEDS; ++i) cfg->seed_weights[i] = 1.f;
}

static int parse_number(const char *value, double *v) {
  char *end;
  if (!value) return -2;
  errno = 0;
  *v = strtod(value, &end);
  if (end == value) return -2;
  while (isspace((unsigned char)*end)) ++end;
  return *end || errno || !isfinite(*v) ? -2 : 0;
}

int cr_curr_config_set(CrCurrConfig *cfg, const char *key, const char *value) {
  double v;
  float *weight = NULL;
  int kind = 0;
  if (!cfg || !key || !value) return -2;
  if (!strcmp(key, "curriculum.mastery_threshold")) kind = 1;
  else if (!strcmp(key, "curriculum.min_episodes")) kind = 2;
  else if (!strcmp(key, "curriculum.history_window")) kind = 3;
  else {
    const char *suffix;
    int cap;
    char *end;
    long index;
    if (!strncmp(key, "curriculum.stage_weight.", sizeof("curriculum.stage_weight.") - 1)) {
      suffix = key + sizeof("curriculum.stage_weight.") - 1; cap = CR_N_STAGES; weight = cfg->stage_weights;
    } else if (!strncmp(key, "curriculum.seed_weight.", sizeof("curriculum.seed_weight.") - 1)) {
      suffix = key + sizeof("curriculum.seed_weight.") - 1; cap = CR_CURR_MAX_SEEDS; weight = cfg->seed_weights;
    } else return -1;
    /* Index syntax is decimal digits only, without signs or whitespace. */
    if (!isdigit((unsigned char)*suffix)) return -2;
    errno = 0;
    index = strtol(suffix, &end, 10);
    if (*end || errno || index < 0 || index >= cap) return -2;
    weight += index;
  }
  if (parse_number(value, &v)) return -2;
  if (weight) {
    if (v < 0. || v > 1e6) return -2;
    *weight = (float)v;
  } else if (kind == 1) {
    if (v < 0. || v > 1.) return -2;
    cfg->mastery_threshold = (float)v;
  } else {
    if (v < 1. || v > CR_CURR_MAX_WIN || floor(v) != v) return -2;
    if (kind == 2) cfg->min_episodes = (int)v;
    else cfg->history_window = (int)v;
  }
  return 0;
}

int cr_curr_config_validate_seeds(const CrCurrConfig *cfg, int nseeds) {
  int i;
  double sum = 0.;
  if (!cfg || nseeds < 1 || nseeds > CR_CURR_MAX_SEEDS ||
      !isfinite(cfg->mastery_threshold) || cfg->mastery_threshold < 0.f ||
      cfg->mastery_threshold > 1.f || cfg->min_episodes < 1 ||
      cfg->min_episodes > cfg->history_window ||
      cfg->history_window > CR_CURR_MAX_WIN) return -2;
  for (i = 0; i < CR_N_STAGES; ++i)
    if (!isfinite(cfg->stage_weights[i]) || cfg->stage_weights[i] < 0.f ||
        cfg->stage_weights[i] > 1e6f) return -2;
  if (!(cfg->stage_weights[0] > 0.f)) return -2;
  for (i = 0; i < CR_CURR_MAX_SEEDS; ++i) {
    float w = cfg->seed_weights[i];
    if (!isfinite(w) || w < 0.f || w > 1e6f) return -2;
    if (i < nseeds) sum += w;
  }
  return sum > 0. ? 0 : -2;
}

int cr_curr_config_validate(const CrCurrConfig *cfg) {
  return cr_curr_config_validate_seeds(cfg, CR_CURR_MAX_SEEDS);
}

void cr_curr_config_dump(FILE *f, const CrCurrConfig *cfg) {
  int i;
  if (!f || !cfg) return;
  fprintf(f, "curriculum.mastery_threshold = %.9g\n"
          "curriculum.min_episodes = %d\ncurriculum.history_window = %d\n",
          (double)cfg->mastery_threshold, cfg->min_episodes, cfg->history_window);
  for (i = 0; i < CR_N_STAGES; ++i)
    fprintf(f, "curriculum.stage_weight.%d = %.9g\n", i, (double)cfg->stage_weights[i]);
  for (i = 0; i < CR_CURR_MAX_SEEDS; ++i)
    fprintf(f, "curriculum.seed_weight.%d = %.9g\n", i, (double)cfg->seed_weights[i]);
}

static uint64_t rng_next(uint64_t *s) {
  uint64_t x = *s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *s = x;
  return x * 0x2545F4914F6CDD1DULL;
}

static float rng_u01(uint64_t *s) {
  return (float)(rng_next(s) >> 11) * (1.0f / 9007199254740992.0f);
}

static size_t hist_base(const CrCurr *c, int seed_i, int stage) {
  return ((size_t)seed_i * (size_t)CR_N_STAGES + (size_t)stage) *
         (size_t)c->config.history_window;
}

static int hist_idx(int seed_i, int stage) {
  return seed_i * CR_N_STAGES + stage;
}

int cr_curr_init(CrCurr *c, int nseeds, float t0_share, uint64_t seed) {
  CrCurrConfig cfg;
  cr_curr_config_defaults(&cfg);
  return cr_curr_init_config(c, nseeds, t0_share, seed, &cfg);
}

int cr_curr_init_config(CrCurr *c, int nseeds, float t0_share, uint64_t seed,
                        const CrCurrConfig *cfg) {

  int i;
  if (!c || cr_curr_config_validate_seeds(cfg, nseeds) || !isfinite(t0_share) || t0_share < 0.f || t0_share > 1.f)
    return -1;
  memset(c, 0, sizeof(*c));
  c->nseeds = nseeds;
  c->t0_share = t0_share;
  c->config = *cfg;
  c->master = cfg->mastery_threshold;
  c->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;
  c->avail = (uint8_t *)calloc((size_t)nseeds * CR_N_STAGES, 1);
  c->hist = (float *)calloc((size_t)nseeds * CR_N_STAGES * (size_t)cfg->history_window,
                            sizeof(float));
  c->hist_n = (int *)calloc((size_t)nseeds * CR_N_STAGES, sizeof(int));
  c->hist_i = (int *)calloc((size_t)nseeds * CR_N_STAGES, sizeof(int));
  if (!c->avail || !c->hist || !c->hist_n || !c->hist_i) {
    cr_curr_free(c);
    return -1;
  }
  for (i = 0; i < nseeds; ++i)
    c->avail[i * CR_N_STAGES + 0] = 1;
  return 0;
}

void cr_curr_free(CrCurr *c) {
  if (!c)
    return;
  free(c->avail);
  free(c->hist);
  free(c->hist_n);
  free(c->hist_i);
  memset(c, 0, sizeof(*c));
}

void cr_curr_record(CrCurr *c, int seed_i, int stage, int ok) {
  int hi;
  size_t b;
  int i;
  if (!c || seed_i < 0 || seed_i >= c->nseeds || stage < 0 ||
      stage >= CR_N_STAGES)
    return;
  hi = hist_idx(seed_i, stage);
  b = hist_base(c, seed_i, stage);
  i = c->hist_i[hi];
  c->hist[b + (size_t)i] = ok ? 1.f : 0.f;
  c->hist_i[hi] = (i + 1) % c->config.history_window;
  if (c->hist_n[hi] < c->config.history_window)
    c->hist_n[hi] += 1;
}

float cr_curr_succ(const CrCurr *c, int seed_i, int stage) {
  int hi;
  int n;
  size_t b;
  int i;
  float s = 0.f;
  if (!c || seed_i < 0 || seed_i >= c->nseeds || stage < 0 ||
      stage >= CR_N_STAGES)
    return 0.f;
  hi = hist_idx(seed_i, stage);
  n = c->hist_n[hi];
  if (n <= 0)
    return 0.f;
  b = hist_base(c, seed_i, stage);
  for (i = 0; i < n; ++i)
    s += c->hist[b + (size_t)i];
  return s / (float)n;
}

/* Equal weights retain the historical modulo sampler and RNG consumption. */
static int sample_seed(CrCurr *c) {
  int i, equal = 1, last = 0;
  double sum = 0., u;
  uint64_t bits = rng_next(&c->rng);
  for (i = 0; i < c->nseeds; ++i) {
    float w = c->config.seed_weights[i];
    if (w != c->config.seed_weights[0]) equal = 0;
    if (w > 0.f) last = i;
    sum += w;
  }
  if (equal) return (int)(bits % (uint64_t)c->nseeds);
  u = (double)(bits >> 11) * (1. / 9007199254740992.) * sum;
  for (i = 0; i < c->nseeds; ++i) {
    u -= c->config.seed_weights[i];
    if (c->config.seed_weights[i] > 0.f && u < 0.) return i;
  }
  return last;
}

void cr_curr_sample(CrCurr *c, int k, int *seeds, int *stages) {
  int j;
  if (!c || k <= 0 || !seeds || !stages)
    return;
  for (j = 0; j < k; ++j) {
    int si = sample_seed(c);
    int av[CR_N_STAGES];
    int nav = 0;
    int s;
    int frontier;
    float w[CR_N_STAGES];
    float wsum;
    float u;
    int pick;
    seeds[j] = si;
    stages[j] = 0;
    if (rng_u01(&c->rng) < c->t0_share)
      continue;
    for (s = 0; s < CR_N_STAGES; ++s) {
      if (c->avail[si * CR_N_STAGES + s])
        av[nav++] = s;
    }
    if (nav <= 0)
      continue;
    frontier = av[nav - 1];
    for (s = 0; s < nav; ++s) {
      int stg = av[s];
      int hn = c->hist_n[hist_idx(si, stg)];
      if (hn < c->config.min_episodes || cr_curr_succ(c, si, stg) < c->master) {
        frontier = stg;
        break;
      }
    }
    wsum = 0.f;
    for (s = 0; s < nav; ++s) {
      w[s] = 0.15f;
      if (av[s] == frontier)
        w[s] += 1.0f - 0.15f * (float)nav;
      w[s] *= c->config.stage_weights[av[s]];
      wsum += w[s];
    }
    if (!(wsum > 0.f)) {
      stages[j] = frontier;
      continue;
    }
    u = rng_u01(&c->rng) * wsum;
    pick = frontier;
    for (s = 0; s < nav; ++s)
      if (w[s] > 0.f) pick = av[s];
    for (s = 0; s < nav; ++s) {
      u -= w[s];
      if (w[s] > 0.f && u <= 0.f) {
        pick = av[s];
        break;
      }
    }
    stages[j] = pick;
  }
}

int cr_stage_of_best(const int *best9) {
  int st = 0;
  if (!best9)
    return 0;
  if (best9[CR_IX_LOG] >= 3 || best9[CR_IX_PLANK] >= 1)
    st = 1;
  if (best9[CR_IX_WPICK] >= 1)
    st = 2;
  if (best9[CR_IX_WPICK] >= 1 && best9[CR_IX_COBBLE] >= 3)
    st = 3;
  if (best9[CR_IX_COAL] >= 1)
    st = 4;
  return st;
}
