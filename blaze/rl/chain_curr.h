/* Per-seed frontier curriculum. Port of StageCurriculum in ppo_chain_cu.py. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CR_N_STAGES = 5, CR_CURR_WIN = 60, CR_CURR_MIN_EPS = 15 };

typedef struct CrCurr {
  int nseeds;
  float t0_share;
  float master; /* 0.6 */
  uint8_t *avail; /* [nseeds][CR_N_STAGES] */
  float *hist;    /* [nseeds][CR_N_STAGES][WIN] ring as packed counts */
  int *hist_n;
  int *hist_i;
  uint64_t rng;
} CrCurr;

int cr_curr_init(CrCurr *c, int nseeds, float t0_share, uint64_t seed);
void cr_curr_free(CrCurr *c);
void cr_curr_record(CrCurr *c, int seed_i, int stage, int ok);
float cr_curr_succ(const CrCurr *c, int seed_i, int stage);
void cr_curr_sample(CrCurr *c, int k, int *seeds, int *stages);
int cr_stage_of_best(const int *best9);

#ifdef __cplusplus
}
#endif
