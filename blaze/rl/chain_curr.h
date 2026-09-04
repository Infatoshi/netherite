/* Per-seed frontier curriculum. Port of StageCurriculum in ppo_chain_cu.py. */
#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CR_N_STAGES = 5, CR_CURR_WIN = 60, CR_CURR_MIN_EPS = 15,
       CR_CURR_MAX_WIN = 4096, CR_CURR_MAX_SEEDS = 256 };
typedef struct CrCurrConfig {
  float mastery_threshold;
  int min_episodes;
  int history_window;
  float stage_weights[CR_N_STAGES];
  float seed_weights[CR_CURR_MAX_SEEDS]; /* Indexed by configured seed order. */
} CrCurrConfig;
void cr_curr_config_defaults(CrCurrConfig *cfg);
/* curriculum.mastery_threshold/min_episodes/history_window,
 * curriculum.stage_weight.N/seed_weight.N. Weights are in [0,1e6].
 * 0 success, -1 unknown key, -2 invalid value. Cross-field validation is
 * deferred until full parsing: min_episodes <= history_window, active seed
 * sum > 0, stage 0 weight > 0 (the only guaranteed available stage).
 * t0_share remains an independent stage-0 rehearsal override. */
int cr_curr_config_set(CrCurrConfig *cfg, const char *key, const char *value);
int cr_curr_config_validate(const CrCurrConfig *cfg);
int cr_curr_config_validate_seeds(const CrCurrConfig *cfg, int nseeds);
void cr_curr_config_dump(FILE *f, const CrCurrConfig *cfg);


typedef struct CrCurr {
  int nseeds;
  float t0_share;
  float master; /* Legacy mastery threshold alias. */
  CrCurrConfig config;
  uint8_t *avail; /* [nseeds][CR_N_STAGES] */
  float *hist;    /* [nseeds][CR_N_STAGES][WIN] ring as packed counts */
  int *hist_n;
  int *hist_i;
  uint64_t rng;
} CrCurr;

int cr_curr_init(CrCurr *c, int nseeds, float t0_share, uint64_t seed);
int cr_curr_init_config(CrCurr *c, int nseeds, float t0_share, uint64_t seed,
                        const CrCurrConfig *cfg);
void cr_curr_free(CrCurr *c);
void cr_curr_record(CrCurr *c, int seed_i, int stage, int ok);
float cr_curr_succ(const CrCurr *c, int seed_i, int stage);
void cr_curr_sample(CrCurr *c, int k, int *seeds, int *stages);
int cr_stage_of_best(const int *best9);

#ifdef __cplusplus
}
#endif
