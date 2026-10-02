/* The lakes lane's features (csrc/engine/features_lakes.c): WorldGenLakes,
 * WorldGenBigMushroom, WorldGenDesertWells, WorldGenIceSpike and
 * WorldGenIcePath, one table row per row this lane appended to the oracle's
 * FEATURES table, with the same config indices. */
#ifndef NETHERITE_FEATURES_LAKES_H
#define NETHERITE_FEATURES_LAKES_H

#include "features.h"

/* The rows for one of this lane's feature names, or NULL; *n gets their
 * number. */
const struct feature *features_lakes_for(const char *name, int *n);

/* World.getBiomeGenForCoords and BiomeGenBase.getFloatTemperature: the lakes
 * feature and the server tick's ice, snow and lightning branches both read
 * them. */
int biome_at(struct world *w, int x, int z);
float biome_temperature(int biome, int x, int y, int z);

#endif