/* Chunk's block-array constructor and generateSkylightMap over the raw block
 * array, the stage after ravines: height maps, section mask, sky light. */
#ifndef NETHERITE_LIGHT_H
#define NETHERITE_LIGHT_H

#include <stdint.h>
#include "terrain.h"

struct light_map {
    int32_t height[256];         /* heightMap, index z << 4 | x */
    int32_t height_min;          /* heightMapMinimum, INT32_MAX if no column */
    int32_t precipitation[256];  /* precipitationHeightMap, same index, -999 */
    uint16_t mask;               /* bit s set when section s holds a block */
    uint8_t sky[CHUNK_CELLS];    /* index x << 12 | z << 8 | y, 0 with no section */
};

/* ids are the block ids after ravines, in terrain's layout; metas are not read. */
void light_pass(const uint16_t *ids, struct light_map *out);

/* Chunk.generateSkylightMap: the height maps and the sky light. */
void skylight_map(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                  int32_t *precip, uint8_t *sky);

/* The same, in a world whose provider.hasNoSky (the Nether): the height maps
 * only, the sky array untouched. */
void skylight_map_nosky(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                        int32_t *precip);

/* Chunk.func_150808_b over the flat array: Block.getLightOpacity of the block at
 * (x, y, z). */
int block_opacity(const uint16_t *ids, int x, int y, int z);

/* The body of Chunk.generateSkylightMap over flat arrays whose sections already
 * exist, for a chunk that is already live: mask is the section mask (bit s set
 * when section s exists), height, height_min, precip and sky are written in
 * place. Sky light is only written, never cleared, exactly as Java leaves it:
 * the caller starts a fresh chunk from zero. */
void skylight_map(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                  int32_t *precip, uint8_t *sky);

#endif