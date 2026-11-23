/* The biome registry for the CUDA generator, copied row for row from the
 * generated csrc/engine/biomes.h, and the block table's opacity and
 * random-tick columns for the built stage, so both engines read one table. */
#include "worldgen.h"
#include "../../engine/biomes.h"
#include "../../engine/blocks.h"

#include <string.h>

void worldgen_biome_rows(struct worldgen_biome rows[256])
{
    memset(rows, 0, sizeof(struct worldgen_biome) * 256);
    for (int i = 0; i < 256; ++i)
    {
        const struct biome_def *b = &BIOMES[i];
        struct worldgen_biome *r = &rows[i];
        r->root = b->root;
        r->variation = b->variation;
        r->temperature = b->temperature;
        r->top = b->top;
        r->top_meta = b->top_meta;
        r->filler = b->filler;
        r->base = b->base;
        r->object_id = b->object_id;
        r->exists = b->exists;
        r->cls = b->cls;
        r->temp_cat = b->temp_cat;
        r->snow = b->snow;
        r->mesa = b->mesa;
        r->surface = b->surface;
        r->p1 = b->p1;
        r->p2 = b->p2;
    }
}

void worldgen_block_rows(uint8_t opacity[256], uint8_t tick_randomly[256])
{
    for (int i = 0; i < 256; ++i)
    {
        opacity[i] = BLOCKS[i].opacity;
        tick_randomly[i] = BLOCKS[i].tick_randomly != 0;
    }
}
