/* Per-seed frontier curriculum. Port of StageCurriculum in ppo_chain_cu.py. */
#include "chain_curr.h"

#include "chain_reward.h"

#include <stdlib.h>
#include <string.h>

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

static size_t hist_base(int seed_i, int stage) {
  return ((size_t)seed_i * (size_t)CR_N_STAGES + (size_t)stage) *
         (size_t)CR_CURR_WIN;
}

static int hist_idx(int seed_i, int stage) {
  return seed_i * CR_N_STAGES + stage;
}

int cr_curr_init(CrCurr *c, int nseeds, float t0_share, uint64_t seed) {
  int i;
  if (!c || nseeds <= 0)
    return -1;
  memset(c, 0, sizeof(*c));
  c->nseeds = nseeds;
  c->t0_share = t0_share;
  c->master = 0.6f;
  c->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;
  c->avail = (uint8_t *)calloc((size_t)nseeds * CR_N_STAGES, 1);
  c->hist = (float *)calloc((size_t)nseeds * CR_N_STAGES * CR_CURR_WIN,
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
  b = hist_base(seed_i, stage);
  i = c->hist_i[hi];
  c->hist[b + (size_t)i] = ok ? 1.f : 0.f;
  c->hist_i[hi] = (i + 1) % CR_CURR_WIN;
  if (c->hist_n[hi] < CR_CURR_WIN)
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
  b = hist_base(seed_i, stage);
  for (i = 0; i < n; ++i)
    s += c->hist[b + (size_t)i];
  return s / (float)n;
}

void cr_curr_sample(CrCurr *c, int k, int *seeds, int *stages) {
  int j;
  if (!c || k <= 0 || !seeds || !stages)
    return;
  for (j = 0; j < k; ++j) {
    int si = (int)(rng_next(&c->rng) % (uint64_t)c->nseeds);
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
      if (hn < CR_CURR_MIN_EPS || cr_curr_succ(c, si, stg) < c->master) {
        frontier = stg;
        break;
      }
    }
    wsum = 0.f;
    for (s = 0; s < nav; ++s) {
      w[s] = 0.15f;
      if (av[s] == frontier)
        w[s] += 1.0f - 0.15f * (float)nav;
      wsum += w[s];
    }
    if (!(wsum > 0.f)) {
      stages[j] = frontier;
      continue;
    }
    u = rng_u01(&c->rng) * wsum;
    pick = av[nav - 1];
    for (s = 0; s < nav; ++s) {
      u -= w[s];
      if (u <= 0.f) {
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
