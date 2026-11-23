/* dim_surface: nether.c's nether_surface (func_147418_b) with the provider
 * rand provideChunk reseeds per chunk, and end.c's end_surface
 * (func_147421_b), a thread per chunk in C's column order. */
#include "dev.cuh"

/* nether.c's nether_surface for column (x, z) (blocks its 128 cells) from
 * the rand r, W false counting only (no writes: every cell is read before
 * its own write and no column reads another's). Returns the draws it made;
 * *rej whether a nextInt(5) was rejected (a redraw). */
template <bool W>
__device__ static int nether_column(jrand *r, const double *nz, int x, int z, uint8_t *blocks, int *rej)
{
    const double *slowsand = nz, *gravel = nz + 256, *exclusivity = nz + 512;
    double depth_add = c_dneg == WORLDGEN_DIM_NEG_SURFACE ? 3.1 : 3.0;
    int i = z + x * 16, calls = 6;
    int soul = slowsand[i] + jr_double(r) * 0.2 > 0.0;
    int deposit = gravel[i] + jr_double(r) * 0.2 > 0.0;
    int depth = (int)(exclusivity[i] / 3.0 + depth_add + jr_double(r) * 0.25);
    int top = -1;
    uint8_t surf = BLK_NETHERRACK, fill = BLK_NETHERRACK;
    for (int y = 127; y >= 0; --y)
    {
        int idx = y;
        /* the second draw only happens when the first comparison holds */
        int c0 = calls, a1 = jr_int_n(r, 5);
        ++calls;
        int cond = y < 127 - a1;
        if (cond)
        {
            int a2 = jr_int_n(r, 5);
            ++calls;
            cond = y > 0 + a2;
        }
        (void)c0;
        if (cond)
        {
            uint8_t b = blocks[idx];
            if (b != BLK_AIR)
            {
                if (b == BLK_NETHERRACK)
                {
                    if (top == -1)
                    {
                        if (depth <= 0)
                        {
                            surf = BLK_AIR;
                            fill = BLK_NETHERRACK;
                        }
                        else if (y >= 64 - 4 && y <= 64 + 1)
                        {
                            surf = BLK_NETHERRACK;
                            fill = BLK_NETHERRACK;
                            if (deposit)
                            {
                                surf = BLK_GRAVEL;
                                fill = BLK_NETHERRACK;
                            }
                            if (soul)
                            {
                                surf = BLK_SOUL_SAND;
                                fill = BLK_SOUL_SAND;
                            }
                        }
                        if (y < 64 && surf == BLK_AIR) surf = BLK_LAVA;
                        top = depth;
                        if (W) blocks[idx] = y >= 64 - 1 ? surf : fill;
                    }
                    else if (top > 0)
                    {
                        --top;
                        if (W) blocks[idx] = fill;
                    }
                }
            }
            else
                top = -1;
        }
        else if (W)
            blocks[idx] = BLK_BEDROCK;
    }
    /* a rejected nextInt(5) advanced the rand further than the calls counted */
    *rej = 0;
    return calls;
}

/* the LCG n steps on (java.util.Random's multiplier and addend, mod 2^48) */
__device__ static uint64_t lcg_jump(uint64_t s, uint64_t n)
{
    uint64_t a = 0x5DEECE66DULL, c = 0xBULL;
    while (n)
    {
        if (n & 1) s = (s * a + c) & JR_MASK;
        c = (c * (a + 1)) & JR_MASK;
        a = (a * a) & JR_MASK;
        n >>= 1;
    }
    return s;
}

/* one step of java.util.Random's next(31) */
__device__ static inline int32_t lcg_next31(uint64_t *s)
{
    *s = (*s * 0x5DEECE66DULL + 0xBULL) & JR_MASK;
    return (int32_t)(uint32_t)(*s >> 17);
}

/* the draws a column's top five cells add past its 257 (the second
 * nextInt(5) of y 126 to 123), from the rand at the column's y 127 draw;
 * -1 when a nextInt(5) there would reject */
__device__ static int nether_extra(uint64_t s)
{
    int e = 0;
    for (int y = 127; y >= 123; --y)
    {
        int32_t bits = lcg_next31(&s), val = bits % 5;
        if ((int32_t)((uint32_t)bits - (uint32_t)val + 4u) < 0) return -1;
        if (y < 127 - val)
        {
            bits = lcg_next31(&s);
            val = bits % 5;
            if ((int32_t)((uint32_t)bits - (uint32_t)val + 4u) < 0) return -1;
            ++e;
        }
    }
    return e;
}

/* nether_surface over the chunk, serially (a start outside the window, a
 * rejected draw) */
__device__ static uint64_t nether_serial(uint64_t s0, const double *nz, uint8_t *raw)
{
    jrand r = {s0};
    int rej;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) nether_column<true>(&r, nz, x, z, raw + (size_t)(x << 11 | z << 7), &rej);
    return r.seed;
}

/* the window of starts a column's table covers, around its mean (259 a
 * column: 257 and the top cells' 2 on average, sd about 0.9 a column) */
#define NS_WIN 48

/* vanilla only rewrites stone and the End's density pass writes end stone,
 * so this never changes a block; it is ported literally so it stays that way
 * (a thread a column) */
__device__ static void end_column(uint8_t *blocks)
{
    int depth = 1;
    int top = -1;
    uint8_t surf = BLK_END_STONE, fill = BLK_END_STONE;
    for (int y = 127; y >= 0; --y)
    {
        uint8_t b = blocks[y];
        if (b != BLK_AIR)
        {
            if (b == BLK_STONE)
            {
                if (top == -1)
                {
                    if (depth <= 0)
                    {
                        surf = BLK_AIR;
                        fill = BLK_END_STONE;
                    }
                    top = depth;
                    if (y >= 0) blocks[y] = surf;
                    else blocks[y] = fill;
                }
                else if (top > 0)
                {
                    --top;
                    blocks[y] = fill;
                }
            }
        }
        else
            top = -1;
    }
}

/* A block per chunk, a thread per column (256), C's column order z outer, x
 * inner. A Nether column draws 257 times from the provider rand, and 0 to 4
 * more as its top five cells' first draws decide: each thread tabulates its
 * column's extras for every start within NS_WIN of its mean, thread 0 chains
 * the starts through the tables, every column counts its own draws from its
 * start (a rejected nextInt(5) or a start outside its window: the whole
 * chunk serially), then writes. */
__global__ void dim_surface(const struct worldgen_dim_req *req, int n, const double *noise, uint8_t *raw_all,
                              uint64_t *rand_all)
{
    __shared__ int8_t ext[256][2 * NS_WIN + 1];
    __shared__ int start[256];
    __shared__ int serial, total;
    int k = blockIdx.x, t = threadIdx.x;
    if (k >= n) return;
    uint8_t *raw = raw_all + (size_t)k * WORLDGEN_DIM_RAW;
    int z = t >> 4, x = t & 15;
    uint8_t *blocks = raw + (size_t)(x << 11 | z << 7);
    uint64_t s0;
    {
        jrand r;
        jr_seed(&r, (int64_t)((uint64_t)(int64_t)req[k].cx * 341873128712ULL +
                              (uint64_t)(int64_t)req[k].cz * 132897987541ULL));
        s0 = r.seed;
    }
    if (req[k].dim != -1)
    {
        end_column(blocks);
        /* the End's provider rand is reseeded per chunk and never drawn */
        if (t == 0) rand_all[k] = s0;
        return;
    }
    const double *nz = noise + (size_t)k * 768;
    /* the extras of this column for starts mean - NS_WIN .. mean + NS_WIN
     * (its y 127 draw comes 6 after the start: the three nextDoubles) */
    int lo = 259 * t - NS_WIN;
    uint64_t s = lcg_jump(s0, (uint64_t)(lo + 6 > 0 ? lo + 6 : 0));
    for (int i = 0; i <= 2 * NS_WIN; ++i)
    {
        int p = lo + i;
        ext[t][i] = p < 0 ? -1 : (int8_t)nether_extra(s);
        if (p >= 0 && lo + 6 + i >= 0) (void)0;
        if (p + 6 >= 0) lcg_next31(&s);
    }
    __syncthreads();
    if (t == 0)
    {
        int p = 0, bad = 0;
        for (int c = 0; c < 256 && !bad; ++c)
        {
            int i = p - (259 * c - NS_WIN);
            if (i < 0 || i > 2 * NS_WIN || ext[c][i] < 0)
            {
                bad = 1;
                break;
            }
            start[c] = p;
            p += 257 + ext[c][i];
        }
        serial = bad;
        total = p;
    }
    __syncthreads();
    if (!serial)
    {
        /* every column's own count from its start: a rejection anywhere in
         * it moves the rest */
        jrand r = {lcg_jump(s0, (uint64_t)start[t])};
        int rej, want = (t < 255 ? start[t + 1] : total) - start[t];
        int got = nether_column<false>(&r, nz, x, z, blocks, &rej);
        if (got != want || r.seed != lcg_jump(s0, (uint64_t)(start[t] + want))) atomicOr(&serial, 1);
    }
    __syncthreads();
    if (serial)
    {
        if (t == 0) rand_all[k] = nether_serial(s0, nz, raw);
        return;
    }
    jrand r = {lcg_jump(s0, (uint64_t)start[t])};
    int rej;
    nether_column<true>(&r, nz, x, z, blocks, &rej);
    if (t == 255) rand_all[k] = r.seed;
}
