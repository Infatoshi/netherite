#ifndef MAGMA_END_POPULATION_LIVE_H
#define MAGMA_END_POPULATION_LIVE_H

#include "mc_rng.h"

/* Minimal block-access seam shared by the oracle fixture and the live world.
 * IDs and metadata are the canonical 1.11.2 values. */
typedef struct {
    void *ctx;
    int (*get)(void *ctx, int x, int y, int z);
    void (*set)(void *ctx, int x, int y, int z, int id, int meta);
} GmEndBlockAccess;

void gm_end_generate_chorus(
    GmEndBlockAccess *world, int x, int y, int z,
    JavaRandom *random, int radius);
void gm_end_generate_island(
    GmEndBlockAccess *world, int x, int y, int z, JavaRandom *random);
void gm_end_generate_gateway(
    GmEndBlockAccess *world, int x, int y, int z);

#endif
