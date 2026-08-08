/* trunk_core.h - shared smoke kernel exercising the trunk headers (world/blocks/tick/rng) so we
 * prove they are __host__ __device__-clean and deterministic CPU==CUDA. Not real worldgen. */
#ifndef TRUNK_CORE_H
#define TRUNK_CORE_H

#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_rng.h"

/* Fill one chunk with a toy hash-noise terrain using the real block ids + props, then return a
 * rolling hash of (state, packed light, props) over the whole chunk. Deterministic from seed. */
MC_HD static inline u64 mc_trunk_fill(Chunk *c, u64 seed) {
    c->cx = 0; c->cz = 0;
    for (int z = 0; z < MC_CZ; z++) {
        for (int x = 0; x < MC_CX; x++) {
            u64 hh = mc_hash_seed(seed, 0, x, 0, z, 1);
            int h = 60 + (int)mc_hash_bound(hh, 12);     /* surface height 60..71 */
            c->biome[z * MC_CX + x] = (u8)mc_hash_bound(hh ^ 0x55, 24);
            for (int y = 0; y < MC_CY; y++) {
                int id;
                if (y == 0) id = BLK_BEDROCK;
                else if (y < h - 4) id = BLK_STONE;
                else if (y < h) id = BLK_DIRT;
                else if (y == h) id = BLK_GRASS;
                else if (y <= 62) id = BLK_WATER;        /* sea level fill */
                else id = BLK_AIR;
                u16 st = mc_state(id, 0);
                mc_set(c, x, y, z, st);
                BlockProps p = mc_block_props(id);
                int sky = (id == BLK_AIR) ? 15 : 0;
                c->light[mc_idx(x, y, z)] = mc_light(sky, p.light_emit);
            }
        }
    }
    u64 acc = 1469598103934665603ULL; /* FNV-ish over the chunk */
    for (int i = 0; i < MC_CHUNK_VOL; i++) {
        acc = mc_hash64(acc ^ (u64)c->blocks[i]);
        acc = mc_hash64(acc ^ (u64)c->light[i]);
    }
    for (int i = 0; i < MC_COL_AREA; i++) acc = mc_hash64(acc ^ (u64)c->biome[i]);
    return acc;
}

#endif
