#pragma once
#include "train_config.h"

enum { TR_RECIPE_MAX_PHASES = 16 };
typedef struct TrainRecipe {
  int count;
  TrainConfig phase[TR_RECIPE_MAX_PHASES];
  char source[TR_RECIPE_MAX_PHASES][TR_CFG_STR_MAX];
} TrainRecipe;

/* Every phase overlays the global recipe, never the previous phase. */
int tr_recipe_load(const TrainConfig *base, TrainRecipe *recipe,
                   char *err, size_t cap);
