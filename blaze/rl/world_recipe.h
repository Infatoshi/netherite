/* Materialize the finite world selected by a training/evaluation recipe.
 * Prepared snapshots, their bank sidecars and a manifest live under out/.
 * The environment and reward target reader must use the same prepared paths.
 * This is startup work only; it does not stream or generate chunks. */
#ifndef BLAZE_WORLD_RECIPE_H
#define BLAZE_WORLD_RECIPE_H
#include <stddef.h>
#define WORLD_RECIPE_MAX 128
#define WORLD_RECIPE_PATH 1024
typedef struct {
    char directory[WORLD_RECIPE_PATH];
    char paths[WORLD_RECIPE_MAX][WORLD_RECIPE_PATH];
    int count;
    int max_cells; /* measured prepared region capacity; 0 when inheriting */
} WorldRecipe;
/* 0 succeeds; -1 fails with err. world_size=0 preserves source paths.
 * Positive sizes crop X/Z only; source snapshot files are never overwritten.
 * Do not reuse a successful output directory as an input cache. */
int world_recipe_prepare(WorldRecipe *r, const char *const *sources, int count,
                         int world_size, char *err, size_t err_cap);
#endif
