/* Chunk's block-array constructor and Chunk.generateSkylightMap, ported from
 * oracle/src/world/chunk/Chunk.java with ExtendedBlockStorage for the sections.
 * The block array is x << 12 | z << 8 | y and the height maps z << 4 | x, as in
 * Java. Opacity comes from the generated BLOCKS table (Block.getLightOpacity),
 * so the port never touches a block object. */
#include "light.h"
#include "blocks.h"

#include <limits.h>
#include <string.h>

/* Chunk.func_150808_b: the opacity of the block at (x, y, z), 0 for air and for
 * a section the constructor never made. Ids are 12 bits wide, like the block
 * storage's block id. */
int block_opacity(const uint16_t *ids, int x, int y, int z)
{
    return BLOCKS[ids[CELL(x, y, z)] & 4095].opacity;
}

/* The height-map and precipitation half of generateSkylightMap, which runs in
 * every dimension; the sky fill below is what provider.hasNoSky skips. */
static void height_map_of(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                          int32_t *precip)
{
    /* getTopFilledSegment(): the yBase of the topmost section, or 0. */
    int top = 0;
    for (int s = 15; s >= 0; --s)
        if (mask & (1 << s))
        {
            top = s * 16;
            break;
        }

    *height_min = INT32_MAX;

    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            precip[x + (z << 4)] = -999;
            int v = top + 15;

            /* the first opaque block from the top; a column of nothing leaves
             * heightMap and heightMapMinimum where they were */
            while (v > 0)
            {
                if (block_opacity(ids, x, v - 1, z) == 0)
                {
                    --v;
                    continue;
                }

                height[z << 4 | x] = v;

                if (v < *height_min) *height_min = v;
                break;
            }
        }
    }
}

void skylight_map(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                  int32_t *precip, uint8_t *sky)
{
    height_map_of(ids, mask, height, height_min, precip);

    int top = 0;
    for (int s = 15; s >= 0; --s)
        if (mask & (1 << s))
        {
            top = s * 16;
            break;
        }

    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            int light = 15, y = top + 15;

            do
            {
                int o = block_opacity(ids, x, y, z);
                if (o == 0 && light != 15) o = 1;
                light -= o;

                if (light > 0 && (mask & (1 << (y >> 4)))) sky[CELL(x, y, z)] = (uint8_t)light;
                --y;
            }
            while (y > 0 && light > 0);
        }
    }
}

/* generateSkylightMap in a world whose provider.hasNoSky (the Nether): the
 * height maps only, the sky array the fill loop never touches. */
void skylight_map_nosky(const uint16_t *ids, uint16_t mask, int32_t *height, int32_t *height_min,
                        int32_t *precip)
{
    height_map_of(ids, mask, height, height_min, precip);
}

void light_pass(const uint16_t *ids, struct light_map *out)
{
    memset(out, 0, sizeof *out);

    /* The constructor makes one ExtendedBlockStorage per 16-block section that
     * holds a block that is neither null nor air. */
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 256; ++y)
                if (ids[CELL(x, y, z)] != BLK_AIR) out->mask |= (uint16_t)(1 << (y >> 4));

    skylight_map(ids, out->mask, out->height, &out->height_min, out->precipitation, out->sky);
}