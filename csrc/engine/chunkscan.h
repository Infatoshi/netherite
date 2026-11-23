/* A block scan's chunks, each looked up once: the 4 x 4 chunks from
 * (cx0, cz0). A read in a chunk that is loaded reads its cells straight
 * (world_get_block's answer: chunk_get_block); any other read, outside the
 * window or the world's bounds, or in a chunk not loaded, is
 * world_get_block's own, so a read that loads (World.getBlock's
 * provideChunk) still loads, in the scan's order, and the next read of that
 * chunk finds it. A loaded chunk stays where it is until it is unloaded,
 * which no block read does: a scan whose body unloads chunks cannot use it. */
#ifndef NETHERITE_CHUNKSCAN_H
#define NETHERITE_CHUNKSCAN_H

#include "world.h"

struct chunkscan {
    struct world *w;
    int cx0, cz0;
    struct chunk *c[4][4];
};

static inline void chunkscan_init(struct chunkscan *k, struct world *w, int cx0, int cz0)
{
    k->w = w;
    k->cx0 = cx0;
    k->cz0 = cz0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) k->c[i][j] = NULL;
}

static inline int chunkscan_block(struct chunkscan *k, int x, int y, int z)
{
    if ((unsigned)y < 256u && (unsigned)x + 30000000u < 60000000u && (unsigned)z + 30000000u < 60000000u)
    {
        unsigned i = (unsigned)((x >> 4) - k->cx0), j = (unsigned)((z >> 4) - k->cz0);

        if (i < 4u && j < 4u)
        {
            struct chunk *c = k->c[i][j];

            if (c == NULL) c = k->c[i][j] = world_chunk(k->w, x >> 4, z >> 4);
            if (c != NULL) return chunk_get_block(c, x & 15, y, z & 15);
        }
    }
    return world_get_block(k->w, x, y, z);
}

#endif
