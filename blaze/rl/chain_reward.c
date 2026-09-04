/* Host scalar ChainReward + GAE. Port of blaze/env/reward_chain.py. */
#include "chain_reward.h"
#include "blaze_abi.h"

#include "blaze_snapshot.h"

#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(RlSnapHead) == 752, "RlSnapHead must match snapshot_logs");
_Static_assert(sizeof(RlSnapItem) == 76, "RlSnapItem must match snapshot_logs");
_Static_assert(offsetof(RlSnapHead, n_items) == 724, "n_items offset");
_Static_assert(offsetof(RlSnapHead, rx0) == 728, "region offset");

static float clampf(float x, float lo, float hi) {
  if (x < lo)
    return lo;
  if (x > hi)
    return hi;
  return x;
}

static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }

void cr_spec_default(CrSpec *s) {
  if (!s)
    return;
  memset(s, 0, sizeof(*s));
  s->shaping_scale = 1.f;
  s->time_cost = 0.01f;
  s->death_penalty = 5.0f;
  s->w_log_per = 1.0f;
  s->log_clamp = 5.0f;
  s->w_plank_first = 2.0f;
  s->w_stick_first = 2.0f;
  s->w_table_first = 3.0f;
  s->w_container_open = 4.0f;
  s->w_wpick_first = 6.0f;
  s->w_cobble_per = 1.0f;
  s->cobble_clamp = 4.0f;
  s->w_spick_first = 0.0f;
  s->w_coal_first = 6.0f;
  s->w_torch_first = 12.0f;
  s->chop_dist_coef = 0.5f;
  s->chop_dist_clamp = 1.0f;
  s->chop_crosshair = 0.03f;
  s->dig_descend_coef = 0.25f;
  s->dig_stone_atk = 0.02f;
  s->dig_hold_pick = 0.005f;
  s->digprog_coef = 0.0015f;
  s->coal_dist_coef = 0.5f;
  s->coal_dist_clamp = 1.0f;
  s->coal_crosshair = 0.03f;
  s->coal_crosshair_maxd = 3.5f;
  s->coal_hold_pick = 0.005f;
  s->coal_chew = 0.0f;
  s->hunt_desc = 0.0f;
  s->w_furnace_first = 0.0f;
  s->w_furnace_open = 0.0f;
  s->w_ironore_per = 0.0f;
  s->ironore_clamp = 3.0f;
  s->w_ingot_first = 0.0f;
  s->w_ipick_first = 0.0f;
}

/* One registry governs parsing, validation and reproducible dumps. */
static const struct { const char *key; size_t offset; } spec_fields[] = {
  {"reward.shaping_scale", offsetof(CrSpec, shaping_scale)},
  {"reward.time_cost", offsetof(CrSpec, time_cost)},
  {"reward.death_penalty", offsetof(CrSpec, death_penalty)},
  {"reward.w_log_per", offsetof(CrSpec, w_log_per)},
  {"reward.log_clamp", offsetof(CrSpec, log_clamp)},
  {"reward.w_plank_first", offsetof(CrSpec, w_plank_first)},
  {"reward.w_stick_first", offsetof(CrSpec, w_stick_first)},
  {"reward.w_table_first", offsetof(CrSpec, w_table_first)},
  {"reward.w_container_open", offsetof(CrSpec, w_container_open)},
  {"reward.w_wpick_first", offsetof(CrSpec, w_wpick_first)},
  {"reward.w_cobble_per", offsetof(CrSpec, w_cobble_per)},
  {"reward.cobble_clamp", offsetof(CrSpec, cobble_clamp)},
  {"reward.w_spick_first", offsetof(CrSpec, w_spick_first)},
  {"reward.w_coal_first", offsetof(CrSpec, w_coal_first)},
  {"reward.w_torch_first", offsetof(CrSpec, w_torch_first)},
  {"reward.chop_dist_coef", offsetof(CrSpec, chop_dist_coef)},
  {"reward.chop_dist_clamp", offsetof(CrSpec, chop_dist_clamp)},
  {"reward.chop_crosshair", offsetof(CrSpec, chop_crosshair)},
  {"reward.dig_descend_coef", offsetof(CrSpec, dig_descend_coef)},
  {"reward.dig_stone_atk", offsetof(CrSpec, dig_stone_atk)},
  {"reward.dig_hold_pick", offsetof(CrSpec, dig_hold_pick)},
  {"reward.digprog_coef", offsetof(CrSpec, digprog_coef)},
  {"reward.coal_dist_coef", offsetof(CrSpec, coal_dist_coef)},
  {"reward.coal_dist_clamp", offsetof(CrSpec, coal_dist_clamp)},
  {"reward.coal_crosshair", offsetof(CrSpec, coal_crosshair)},
  {"reward.coal_crosshair_maxd", offsetof(CrSpec, coal_crosshair_maxd)},
  {"reward.coal_hold_pick", offsetof(CrSpec, coal_hold_pick)},
  {"reward.coal_chew", offsetof(CrSpec, coal_chew)},
  {"reward.hunt_desc", offsetof(CrSpec, hunt_desc)},
  {"reward.w_furnace_first", offsetof(CrSpec, w_furnace_first)},
  {"reward.w_furnace_open", offsetof(CrSpec, w_furnace_open)},
  {"reward.w_ironore_per", offsetof(CrSpec, w_ironore_per)},
  {"reward.ironore_clamp", offsetof(CrSpec, ironore_clamp)},
  {"reward.w_ingot_first", offsetof(CrSpec, w_ingot_first)},
  {"reward.w_ipick_first", offsetof(CrSpec, w_ipick_first)},
};

int cr_spec_set(CrSpec *s, const char *key, const char *value) {
  size_t i;
  if (!s || !key || !value) return -2;
  for (i = 0; i < sizeof(spec_fields) / sizeof(spec_fields[0]); ++i) {
    if (!strcmp(key, spec_fields[i].key)) {
      char *end;
      float v;
      errno = 0;
      v = strtof(value, &end);
      if (end == value) return -2;
      while (isspace((unsigned char)*end)) ++end;
      if (*end || errno || !isfinite(v) || v < 0.f || v > 1e6f) return -2;
      *(float *)((char *)s + spec_fields[i].offset) = v;
      return 0;
    }
  }
  return -1;
}

int cr_spec_validate(const CrSpec *s) {
  size_t i;
  if (!s) return -2;
  for (i = 0; i < sizeof(spec_fields) / sizeof(spec_fields[0]); ++i) {
    float v = *(const float *)((const char *)s + spec_fields[i].offset);
    if (!isfinite(v) || v < 0.f || v > 1e6f) return -2;
  }
  return 0;
}

void cr_spec_dump(FILE *f, const CrSpec *s) {
  size_t i;
  if (!f || !s) return;
  for (i = 0; i < sizeof(spec_fields) / sizeof(spec_fields[0]); ++i)
    fprintf(f, "%s = %.9g\n", spec_fields[i].key,
            (double)*(const float *)((const char *)s + spec_fields[i].offset));
}

static int iron_on(const CrSpec *s) {
  return s->w_furnace_first != 0.f || s->w_furnace_open != 0.f ||
         s->w_ironore_per != 0.f || s->w_ingot_first != 0.f ||
         s->w_ipick_first != 0.f;
}

int cr_state_set_spec(CrState *st, const CrSpec *spec) {
  if (!st || cr_spec_validate(spec)) return -2;
  st->spec = *spec;
  st->iron_on = iron_on(spec);
  return 0;
}

int cr_state_init(CrState *st, int n, const CrSpec *spec) {
  CrSpec def;
  if (!st || n <= 0)
    return -1;
  memset(st, 0, sizeof(*st));
  st->n = n;
  if (spec)
    st->spec = *spec;
  else {
    cr_spec_default(&def);
    st->spec = def;
  }
  if (cr_spec_validate(&st->spec)) {
    memset(st, 0, sizeof(*st));
    return -1;
  }
  st->iron_on = iron_on(&st->spec);
  st->best = (int *)calloc((size_t)n * 9, sizeof(int));
  st->flag_cont = (uint8_t *)calloc((size_t)n, 1);
  st->prev_logd = (float *)malloc((size_t)n * sizeof(float));
  st->prev_coald = (float *)malloc((size_t)n * sizeof(float));
  st->prev_y = (float *)calloc((size_t)n, sizeof(float));
  st->prev_digp = (float *)calloc((size_t)n, sizeof(float));
  st->min_y = (float *)malloc((size_t)n * sizeof(float));
  st->best_iron = (int *)calloc((size_t)n * 4, sizeof(int));
  st->flag_furn = (uint8_t *)calloc((size_t)n, 1);
  if (!st->best || !st->flag_cont || !st->prev_logd || !st->prev_coald ||
      !st->prev_y || !st->prev_digp || !st->min_y || !st->best_iron ||
      !st->flag_furn) {
    cr_state_free(st);
    return -1;
  }
  {
    int i;
    for (i = 0; i < n; ++i) {
      st->prev_logd[i] = -1.f;
      st->prev_coald[i] = -1.f;
      st->min_y[i] = 1e9f;
    }
  }
  return 0;
}

void cr_state_free(CrState *st) {
  if (!st)
    return;
  free(st->best);
  free(st->flag_cont);
  free(st->prev_logd);
  free(st->prev_coald);
  free(st->prev_y);
  free(st->prev_digp);
  free(st->min_y);
  free(st->best_iron);
  free(st->flag_furn);
  memset(st, 0, sizeof(*st));
}

void cr_reset_lane(CrState *st, int i) {
  int k;
  if (!st || i < 0 || i >= st->n)
    return;
  for (k = 0; k < 9; ++k)
    st->best[(size_t)i * 9 + (size_t)k] = 0;
  st->flag_cont[i] = 0;
  st->prev_logd[i] = -1.f;
  st->prev_coald[i] = -1.f;
  st->prev_y[i] = -1e9f;
  st->prev_digp[i] = 1e9f;
  st->min_y[i] = 1e9f;
  for (k = 0; k < 4; ++k)
    st->best_iron[(size_t)i * 4 + (size_t)k] = 0;
  st->flag_furn[i] = 0;
}

void cr_reset_mask(CrState *st, const uint8_t *mask, int n) {
  int i;
  if (!st)
    return;
  if (n > st->n)
    n = st->n;
  for (i = 0; i < n; ++i) {
    if (!mask || mask[i])
      cr_reset_lane(st, i);
  }
}

void cr_seed_lane(CrState *st, int i, const int *status) {
  int k;
  if (!st || !status || i < 0 || i >= st->n)
    return;
  for (k = 0; k < 9; ++k)
    st->best[(size_t)i * 9 + (size_t)k] = status[k];
  if (status[CR_ST_CONT] == 1)
    st->flag_cont[i] = 1;
  {
    /* Seed iron history even when its reward weights are disabled. */
    for (k = 0; k < 4; ++k)
      st->best_iron[(size_t)i * 4 + (size_t)k] = status[13 + k];
    if (status[CR_ST_CONT] == 2)
      st->flag_furn[i] = 1;
  }
}

static float nearest_log(const float *logs, int nseeds, int lmax, int si,
                         float px, float py, float pz) {
  int k;
  float best = 1e9f;
  const float *row;
  if (!logs || nseeds <= 0 || lmax <= 0 || si < 0 || si >= nseeds)
    return best;
  row = logs + (size_t)si * (size_t)lmax * 3u;
  for (k = 0; k < lmax; ++k) {
    float dx = row[k * 3] - px;
    float dy = row[k * 3 + 1] - py;
    float dz = row[k * 3 + 2] - pz;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < best)
      best = d;
  }
  return best;
}

void cr_step(CrState *st, const int *status, const unsigned short *cam,
             const int32_t *acts, const float *pose, const float *scal,
             const unsigned char *done, const int *lane_seed,
             const float *logs, int nseeds, int lmax, float *r) {
  const CrSpec *s;
  int n;
  int e;
  if (!st || !status || !cam || !acts || !pose || !scal || !done || !r)
    return;
  s = &st->spec;
  n = st->n;
  for (e = 0; e < n; ++e) {
    const int *stt = status + (size_t)e * 17;
    const int *best = st->best + (size_t)e * 9;
    int newmax[9];
    float d[9];
    float re = -s->time_cost;
    int k;
    unsigned short center;
    int atk;
    int chopping;
    int digging;
    int hunting;
    int haspick;
    int held_pick;
    int held_any_pick;
    int on_stone;
    int stone_px;
    float digp;
    float dprog;
    float py;
    int no_coal_scan;
    float cd;
    int si;

    for (k = 0; k < 9; ++k) {
      int v = stt[k];
      newmax[k] = v > best[k] ? v : best[k];
      d[k] = (float)(newmax[k] - best[k]);
    }
    re += minf(d[CR_IX_LOG], s->log_clamp) * s->w_log_per;
    if (best[CR_IX_PLANK] == 0 && newmax[CR_IX_PLANK] > 0)
      re += s->w_plank_first;
    if (best[CR_IX_STICK] == 0 && newmax[CR_IX_STICK] > 0)
      re += s->w_stick_first;
    if (best[CR_IX_TABLE] == 0 && newmax[CR_IX_TABLE] > 0)
      re += s->w_table_first;
    {
      int cont_now = stt[CR_ST_CONT] == 1;
      if (cont_now && !st->flag_cont[e])
        re += s->w_container_open;
      st->flag_cont[e] = (uint8_t)(st->flag_cont[e] | (cont_now ? 1 : 0));
    }
    if (best[CR_IX_WPICK] == 0 && newmax[CR_IX_WPICK] > 0)
      re += s->w_wpick_first;
    re += minf(d[CR_IX_COBBLE], s->cobble_clamp) * s->w_cobble_per;
    if (s->w_spick_first != 0.f && best[CR_IX_SPICK] == 0 &&
        newmax[CR_IX_SPICK] > 0)
      re += s->w_spick_first;
    if (best[CR_IX_COAL] == 0 && newmax[CR_IX_COAL] > 0)
      re += s->w_coal_first;
    if (best[CR_IX_TORCH] == 0 && newmax[CR_IX_TORCH] > 0)
      re += s->w_torch_first;
    for (k = 0; k < 9; ++k)
      st->best[(size_t)e * 9 + (size_t)k] = newmax[k];

    {
      /* Always track milestones so later weight changes cannot repay them. */
      int *bi = st->best_iron + (size_t)e * 4;
      int newi[4];
      float di[4];
      /* status 13..16 = furnace, ironore, ingot, ipick */
      for (k = 0; k < 4; ++k) {
        int v = stt[13 + k];
        newi[k] = v > bi[k] ? v : bi[k];
        di[k] = (float)(newi[k] - bi[k]);
      }
      if (bi[0] == 0 && newi[0] > 0)
        re += s->w_furnace_first;
      {
        int furn_now = stt[CR_ST_CONT] == 2;
        if (furn_now && !st->flag_furn[e])
          re += s->w_furnace_open;
        st->flag_furn[e] = (uint8_t)(st->flag_furn[e] | (furn_now ? 1 : 0));
      }
      re += minf(di[1], s->ironore_clamp) * s->w_ironore_per;
      if (bi[2] == 0 && newi[2] > 0)
        re += s->w_ingot_first;
      if (bi[3] == 0 && newi[3] > 0)
        re += s->w_ipick_first;
      for (k = 0; k < 4; ++k)
        bi[k] = newi[k];
    }

    center = cam[(size_t)e * CR_NPIX + (size_t)CR_CY * CR_CAM_W + CR_CX];
    atk = acts[(size_t)e * 9 + 4] == 1;
    py = pose[(size_t)e * 5 + 1];
    si = lane_seed ? lane_seed[e] : 0;

    chopping = (newmax[CR_IX_LOG] < 3) && (newmax[CR_IX_PLANK] == 0) &&
               (newmax[CR_IX_WPICK] == 0);
    if (chopping) {
      float px = pose[(size_t)e * 5 + 0];
      float pz = pose[(size_t)e * 5 + 2];
      float ld = nearest_log(logs, nseeds, lmax, si, px, py + 1.62f, pz);
      if (st->prev_logd[e] >= 0.f) {
        float shp = s->chop_dist_coef * (st->prev_logd[e] - ld);
        re += s->shaping_scale * (clampf(shp, -s->chop_dist_clamp, s->chop_dist_clamp));
      }
      st->prev_logd[e] = ld;
      if (atk && center == 17)
        re += s->shaping_scale * s->chop_crosshair;
    } else {
      st->prev_logd[e] = -1.f;
    }

    digging = (newmax[CR_IX_WPICK] > 0) && (newmax[CR_IX_COBBLE] < 3);
    held_pick = stt[CR_ST_HELD] == 270;
    on_stone = (center == 1) || (center == 4) || (center == 3) || (center == 2);
    if (digging) {
      float dy = clampf(st->prev_y[e] - py, 0.f, 2.f);
      re += s->shaping_scale * (s->dig_descend_coef * dy);
      if (atk && held_pick && on_stone)
        re += s->shaping_scale * s->dig_stone_atk;
      if (held_pick)
        re += s->shaping_scale * s->dig_hold_pick;
    }
    st->prev_y[e] = py;

    haspick = newmax[CR_IX_WPICK] > 0;
    digp = (float)stt[CR_ST_DIGP];
    dprog = maxf(digp - st->prev_digp[e], 0.f);
    stone_px = (center == 1) || (center == 16);
    if (haspick && held_pick && stone_px)
      re += s->shaping_scale * (s->digprog_coef * dprog);
    st->prev_digp[e] = digp;

    hunting = (newmax[CR_IX_WPICK] > 0) && (newmax[CR_IX_COBBLE] >= 3) &&
              (newmax[CR_IX_COAL] == 0);
    {
      const float *sc = scal + (size_t)e * 6;
      no_coal_scan = (sc[0] == 0.f) && (sc[1] == 0.f) && (sc[2] == 0.f) &&
                     (sc[3] == 1.f);
      cd = sc[3] * 24.0f;
    }
    held_any_pick = (stt[CR_ST_HELD] == 270) || (stt[CR_ST_HELD] == 274);
    if (hunting) {
      int have_nc = !no_coal_scan;
      if (have_nc) {
        if (st->prev_coald[e] >= 0.f) {
          float shp = s->coal_dist_coef * (st->prev_coald[e] - cd);
          re += s->shaping_scale * (clampf(shp, -s->coal_dist_clamp, s->coal_dist_clamp));
        }
        st->prev_coald[e] = cd;
      } else {
        st->prev_coald[e] = -1.f;
      }
      if (atk && center == 16 && cd <= s->coal_crosshair_maxd)
        re += s->shaping_scale * s->coal_crosshair;
      if (held_any_pick)
        re += s->shaping_scale * s->coal_hold_pick;
      if (s->coal_chew > 0.f && center == 16 && held_any_pick)
        re += s->shaping_scale * (s->coal_chew * dprog);
    } else {
      st->prev_coald[e] = -1.f;
    }

    if (s->hunt_desc > 0.f) {
      float rec_gain = 0.f;
      if (st->min_y[e] <= 1e8f)
        rec_gain = clampf(st->min_y[e] - py, 0.f, 2.f);
      if (hunting && no_coal_scan)
        re += s->shaping_scale * (s->hunt_desc * rec_gain);
    }
    st->min_y[e] = minf(st->min_y[e], py);

    if (done[e] == BLAZE_DONE_DEATH)
      re += -s->death_penalty;
    r[e] = re;
  }
}

int cr_logs_from_bsnp(const char *path, float *xyz, int cap, int *n_out) {
  FILE *f;
  RlSnapHead head;
  unsigned short *cells = NULL;
  size_t vol;
  int n = 0;
  int ix, iy, iz;

  if (n_out)
    *n_out = 0;
  if (!path || !xyz || cap < 0)
    return -1;
  f = fopen(path, "rb");
  if (!f)
    return -1;
  if (fread(&head, sizeof(head), 1, f) != 1) {
    fclose(f);
    return -1;
  }
  if (memcmp(head.magic, "BSNP", 4) != 0) {
    fclose(f);
    return -1;
  }
  if (head.rnx <= 0 || head.rny <= 0 || head.rnz <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, (long)head.n_items * (long)sizeof(RlSnapItem), SEEK_CUR) != 0) {
    fclose(f);
    return -1;
  }
  vol = (size_t)head.rnx * (size_t)head.rny * (size_t)head.rnz;
  cells = (unsigned short *)malloc(vol * sizeof(*cells));
  if (!cells) {
    fclose(f);
    return -1;
  }
  if (fread(cells, sizeof(*cells), vol, f) != vol) {
    free(cells);
    fclose(f);
    return -1;
  }
  fclose(f);

  for (ix = 0; ix < head.rnx; ++ix) {
    for (iy = 0; iy < head.rny; ++iy) {
      for (iz = 0; iz < head.rnz; ++iz) {
        size_t idx =
            ((size_t)ix * (size_t)head.rny + (size_t)iy) * (size_t)head.rnz +
            (size_t)iz;
        int id = (int)(cells[idx] >> 4);
        if (id != 17)
          continue;
        if (n < cap) {
          xyz[(size_t)n * 3 + 0] = (float)(ix + head.rx0) + 0.5f;
          xyz[(size_t)n * 3 + 1] = (float)(iy + head.ry0) + 0.5f;
          xyz[(size_t)n * 3 + 2] = (float)(iz + head.rz0) + 0.5f;
        }
        n++;
      }
    }
  }
  free(cells);
  if (n_out)
    *n_out = n > cap ? cap : n;
  return n > cap ? -2 : 0;
}

void cr_gae(const float *rew, const unsigned char *term,
            const unsigned char *cut, const float *val, const float *next_val,
            const float *cut_val,
            float gamma, float lam, int T, int N, float *adv, float *ret) {
  int e;
  if (!rew || !term || !cut || !val || !next_val || !cut_val || !adv || !ret || T <= 0 ||
      N <= 0)
    return;
  for (e = 0; e < N; ++e) {
    float gae = 0.f;
    float nextv = next_val[e];
    int t;
    for (t = T - 1; t >= 0; --t) {
      size_t ix = (size_t)t * (size_t)N + (size_t)e;
      float nonterm = term[ix] ? 0.f : 1.f;
      float keep = cut[ix] ? 0.f : 1.f;
      /* A cut ends the episode but a truncation still bootstraps from its
       * final observation. val[t+1] may already belong to the reset episode. */
      float bootstrap = cut[ix] && !term[ix] ? cut_val[ix] : nextv;
      float delta = rew[ix] + gamma * bootstrap * nonterm - val[ix];
      gae = delta + gamma * lam * keep * gae;
      adv[ix] = gae;
      ret[ix] = gae + val[ix];
      nextv = val[ix];
    }
  }
}
