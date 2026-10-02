/* The built stage: each generated chunk as the Chunk constructor and
 * Chunk.generateSkylightMap leave it, ported from the C engine that is its
 * oracle (csrc/engine/world.c chunk_construct, chunk_height_map and
 * generate_skylight_map; chunkgen.c chunkgen_construct runs them in that
 * order). The C port's fast paths (the air run above the height map, the
 * constant nibble arrays) are its own; this follows the Java loops they
 * shortcut, so the bytes are the same: worldgen_built_check (built_check.c)
 * compares the two chunk by chunk.
 *
 * A block per chunk, a thread per column (x << 4 | z). The column's cells are
 * its 256 consecutive bytes of the raw arrays (index x << 12 | z << 8 | y);
 * in a band they are 16 consecutive ids at x << 8 | z << 4 and 8 bytes of
 * each nibble array at x << 7 | z << 3, so every thread writes only its own
 * bytes of the bands, and the block reduces the band mask, the metadata
 * flags, the random-tick counts and heightMapMinimum. */
#include "dev.cuh"

__constant__ uint8_t c_opacity[256], c_tick[256];

__global__ void built(const uint8_t *ids_all, const uint8_t *metas_all, int n, struct worldgen_built *hdr_all,
                      uint8_t *bands_all)
{
    int i = blockIdx.x, col = threadIdx.x;
    if (i >= n) return;

    const uint8_t *ids = ids_all + (size_t)i * CELLS + ((size_t)col << 8);
    const uint8_t *metas = metas_all + (size_t)i * CELLS + ((size_t)col << 8);
    uint8_t *bands = bands_all + (size_t)i * 16 * WORLDGEN_BAND_BYTES;
    __shared__ unsigned s_tick[16];
    __shared__ int s_hmin;

    if (col < 16) s_tick[col] = 0;
    if (col == 0) s_hmin = INT_MAX;
    __syncthreads();

    /* the constructor: a band holding a block that is not air has storage,
     * its ids, and its metadata only under blocks; every band's
     * random-tick count covers its 4096 cells */
    unsigned mask = 0, metas_any = 0;
    for (int s = 0; s < 16; ++s)
    {
        uint4 v = *(const uint4 *)(ids + (s << 4));
        uint4 m = *(const uint4 *)(metas + (s << 4));
        const uint8_t *b = (const uint8_t *)&v, *mb = (const uint8_t *)&m;
        unsigned tick = 0, bad = 0;
        uint64_t mw = 0;

        for (int y = 0; y < 16; ++y)
        {
            unsigned mv = b[y] != BLK_AIR ? mb[y] : 0;
            tick += c_tick[b[y]];
            bad |= mv > 15;
            mw |= (uint64_t)(mv & 15) << (4 * y);
        }
        if (bad) atomicOr(&d_err, WORLDGEN_ERR_BUILT_META);

        uint8_t *band = bands + (size_t)s * WORLDGEN_BAND_BYTES;
        *(uint4 *)(band + (col << 4)) = v;
        *(uint64_t *)(band + 4096 + (col << 3)) = mw;
        if (__syncthreads_or((v.x | v.y | v.z | v.w) != 0)) mask |= 1u << s;
        if (__syncthreads_or(mw != 0)) metas_any |= 1u << s;
        for (int o = 16; o > 0; o >>= 1) tick += __shfl_down_sync(0xffffffffu, tick, o);
        if ((col & 31) == 0) atomicAdd(&s_tick[s], tick);
    }

    /* generateHeightMap: from the top band with storage, the first cell
     * down with an opacity (a band without storage reads air) */
    int top = mask != 0 ? (31 - __clz((int)mask)) << 4 : 0, height = 0;
    for (int y = top + 14; y >= 0; --y)
        if ((mask >> (y >> 4) & 1) && c_opacity[ids[y]] != 0)
        {
            height = y + 1;
            break;
        }
    if (height > 0) atomicMin(&s_hmin, height);

    /* the sky fill: 15 from the top band's top down, less each cell's
     * opacity (at least 1 once below 15), stored in bands with storage
     * while above 0, down to y 1 */
    int light = c_negative == WORLDGEN_NEG_BUILT ? 14 : 15, y = top + 15, run = 1;
    for (int s = 15; s >= 0; --s)
    {
        uint64_t w = 0;

        if (run && (s << 4) <= y)
            for (int yy = y - (s << 4); yy >= 0 && run; --yy)
            {
                int o = c_opacity[ids[(s << 4) | yy]];

                if (o == 0 && light != 15) o = 1;
                light -= o;
                if (light > 0 && (mask >> s & 1)) w |= (uint64_t)light << (4 * yy);
                --y;
                run = y > 0 && light > 0;
            }
        *(uint64_t *)(bands + (size_t)s * WORLDGEN_BAND_BYTES + 6144 + (col << 3)) = w;
    }

    struct worldgen_built *h = hdr_all + i;
    h->height[(col & 15) << 4 | col >> 4] = height;
    __syncthreads();
    if (col < 16) h->ticking[col] = (uint16_t)s_tick[col];
    if (col == 0)
    {
        h->mask = (uint16_t)mask;
        h->metas_any = (uint16_t)metas_any;
        h->height_min = s_hmin;
    }
}
