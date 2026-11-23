/* The carvers in two phases (MapGenBase.func_151539_a: MapGenCaves,
 * MapGenRavine, MapGenCavesHell), each byte for byte the one-thread-per-chunk
 * walk it replaced (csrc/engine/carve.c, nether.c).
 *
 * A carver's tunnels depend on their source chunk and the world seed alone:
 * the source chunk's Random places them, and each tunnel draws from a Random
 * of its own. What a target chunk adds is only which of a tunnel's steps it
 * uses: a step returns (the tunnel can no longer reach the chunk), skips (its
 * box misses the chunk), gives up on water (lava in the Nether) inside its
 * box, or carves; a room's first carve ends it. Water is never written or
 * cleared by a carver (it writes air, lava, flowing lava and the biome's top
 * block, never water), so whether a step's box holds water is a function of
 * the chunk before carving. So:
 *
 *  1. sources: every (seed, source chunk) the batch's targets reach (17 x 17
 *     around each) once, through a hash table (carve_insert);
 *  2. walk: each source's top tunnels listed from its Random (carve_sources),
 *     then each tunnel walked once, a thread a tunnel (carve_walk): every step
 *     that reaches the target tests becomes a sphere, in order, a branch's two
 *     children after their parent's steps (the branchy part, once per source
 *     instead of once per source and target);
 *  3. carve: a block per target chunk, a thread per column (carve_apply): the
 *     block walks the spheres of its 289 sources in the walk's order, all
 *     threads together (the return, skip and room tests are the chunk's, the
 *     same for every column), tests a sphere's box shell for water together
 *     (the block's OR), and each thread carves its own column, in order. A
 *     column's cells are written only by its own thread, and its grass flag
 *     and the dirt below read only that column.
 *
 * Every unit that includes this header instantiates its own kernels (the
 * anonymous namespace): carve.cu (caves, ravines), dim_caves.cu (the
 * Nether's caves). */
#ifndef NETHERITE_WORLDGEN_CARVE_CUH
#define NETHERITE_WORLDGEN_CARVE_CUH

#include "dev.cuh"

#include <string.h>

#define CV_PI_F ((float)3.141592653589793)

namespace {

__device__ inline struct cv_tun cv_tun_make(int64_t seed, double x, double y, double z, double yscale, float width,
                                            float yaw, float pitch, int step, int steps)
{
    struct cv_tun t;
    memset(&t, 0, sizeof t);
    t.seed = seed;
    t.x = x;
    t.y = y;
    t.z = z;
    t.yscale = yscale;
    t.width = width;
    t.yaw = yaw;
    t.pitch = pitch;
    t.step = step;
    t.steps = steps;
    return t;
}

/* a sphere into its tunnel's reach box */
__device__ inline void cv_reach(struct cv_box *b, double x, double z, double w)
{
    double lx = x - w * 2.0, hx = x + w * 2.0, lz = z - w * 2.0, hz = z + w * 2.0;
    if (lx < b->lo_x) b->lo_x = lx;
    if (hx > b->hi_x) b->hi_x = hx;
    if (lz < b->lo_z) b->lo_z = lz;
    if (hz > b->hi_z) b->hi_z = hz;
}

__device__ inline unsigned long long cv_key(int si, int sx, int sz)
{
    return ((unsigned long long)(si + 1) << 56) | ((unsigned long long)(uint32_t)(sx & 0xFFFFFFF) << 28) |
           (unsigned long long)(uint32_t)(sz & 0xFFFFFFF);
}

__device__ inline void cv_unkey(unsigned long long k, int *si, int *sx, int *sz)
{
    *si = (int)(k >> 56) - 1;
    *sx = (int)((uint32_t)(k >> 28 & 0xFFFFFFF) << 4) >> 4;
    *sz = (int)((uint32_t)(k & 0xFFFFFFF) << 4) >> 4;
}

/* phase 1: target t's source o into the table; valid(t) is the carver's
 * (the Nether's caves skip the End's requests) */
template <class P>
__global__ void carve_insert(const typename P::req_t *req, int n, struct cv_work w)
{
    int g = blockIdx.x * blockDim.x + threadIdx.x;
    if (g >= n * CV_SRC) return;
    int t = g / CV_SRC, o = g % CV_SRC;
    if (!P::valid(req[t]))
    {
        w.tgt_slot[g] = -1;
        return;
    }
    int sx = req[t].cx - CV_RANGE + o / CV_SIDE, sz = req[t].cz - CV_RANGE + o % CV_SIDE;
    unsigned long long key = cv_key(req[t].seed_index, sx, sz);
    unsigned mask = (1u << w.ht_bits) - 1;
    unsigned h = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> (64 - w.ht_bits));
    for (;;)
    {
        unsigned long long was = atomicCAS(&w.ht_key[h], 0ull, key);
        if (was == 0ull)
        {
            int uid = atomicAdd(&w.cnt->nuniq, 1);
            if (uid >= w.cap_uniq)
            {
                atomicOr(&w.cnt->err, WORLDGEN_ERR_CARVE_CAP);
                uid = -1;
            }
            else w.uniq_key[uid] = key;
            w.ht_uid[h] = uid;
            break;
        }
        if (was == key) break;
        h = (h + 1) & mask;
    }
    w.tgt_slot[g] = (int)h;
}

/* phase 2a: each source's top tunnels, from its Random: counted, given a
 * contiguous run, then listed (the source Random draws the same either
 * time; the tunnels' own Randoms are not touched) */
template <class P>
__global__ void carve_sources(const typename P::seed_t *seeds, struct cv_work w)
{
    int nuniq = min(w.cnt->nuniq, w.cap_uniq);
    for (int u = blockIdx.x * blockDim.x + threadIdx.x; u < nuniq; u += gridDim.x * blockDim.x)
    {
        int si, sx, sz;
        cv_unkey(w.uniq_key[u], &si, &sx, &sz);
        const typename P::seed_t *s = &seeds[si];
        int n = P::source(s, sx, sz, NULL);
        int first = n ? atomicAdd(&w.cnt->ntun, n) : 0;
        if (first + n > w.cap_tun)
        {
            atomicOr(&w.cnt->err, WORLDGEN_ERR_CARVE_CAP);
            n = 0;
        }
        if (n) P::source(s, sx, sz, w.tun + first);
        w.src_first[u] = first;
        w.src_n[u] = n;
    }
}

/* the spheres a segment of cap steps may make: a run of the pool */
__device__ inline int cv_take(struct cv_work *w, int cap)
{
    int b = atomicAdd(&w->cnt->nsph, cap);
    if (b + cap > w->cap_sph)
    {
        atomicOr(&w->cnt->err, WORLDGEN_ERR_CARVE_CAP);
        return -1;
    }
    return b;
}

/* phase 2b: a thread a tunnel */
template <class P>
__global__ void carve_walk(struct cv_work w)
{
    int ntun = min(w.cnt->ntun, w.cap_tun);
    for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < ntun; i += gridDim.x * blockDim.x)
    {
        struct cv_box *b = &w.box[i];
        b->lo_x = b->lo_z = 1e300;
        b->hi_x = b->hi_z = -1e300;
        P::walk(&w, &w.tun[i], b);
    }
}

/* phase 3: a sphere's box in the target chunk (carve.c carve's clip) */
struct cv_cut { int x0, x1, y0, y1, z0, z1; };

/* one sphere against the block's chunk: 2 when the segment ends here (the
 * reach test), 0 when the box test fails, 1 when it passes (its box in *b) */
template <class P>
__device__ inline int cv_test(const struct cv_sph *sp, double reach, int cx, int cz, struct cv_cut *b)
{
    double ox = (double)(cx * 16 + 8), oz = (double)(cz * 16 + 8);
    double x = sp->x, y = sp->y, z = sp->z, w = sp->w, h = sp->h;
    double dx = x - ox, dz = z - oz, left = (double)sp->left;
    if (dx * dx + dz * dz - left * left > reach * reach) return 2;
    if (!(x >= ox - 16.0 - w * 2.0 && z >= oz - 16.0 - w * 2.0 && x <= ox + 16.0 + w * 2.0 && z <= oz + 16.0 + w * 2.0))
        return 0;
    int x0 = mh_floor(x - w) - cx * 16 - 1, x1 = mh_floor(x + w) - cx * 16 + 1;
    int y0 = mh_floor(y - h) - 1, y1 = mh_floor(y + h) + 1;
    int z0 = mh_floor(z - w) - cz * 16 - 1, z1 = mh_floor(z + w) - cz * 16 + 1;
    b->x0 = x0 < 0 ? 0 : x0;
    b->x1 = x1 > 16 ? 16 : x1;
    b->y0 = y0 < 1 ? 1 : y0;
    b->y1 = y1 > P::YTOP ? P::YTOP : y1;
    b->z0 = z0 < 0 ? 0 : z0;
    b->z1 = z1 > 16 ? 16 : z1;
    return 1;
}

/* the shell test for column (ci, ck) of the box: every cell of an edge
 * column from y1 + 1 down to y0 - 1, only those two of the others
 * (vanilla's j = y0 jump); 1 on water (lava in the Nether) */
template <class P>
__device__ inline int cv_shell(const struct cv_cut *b, const uint32_t *wet, int ci, int ck)
{
    int x0 = b->x0, x1 = b->x1, y0 = b->y0, y1 = b->y1, z0 = b->z0, z1 = b->z1;
    if (!(ci >= x0 && ci < x1 && ck >= z0 && ck < z1)) return 0;
    int hit = 0;
    if (ci == x0 || ci == x1 - 1 || ck == z0 || ck == z1 - 1)
    {
        int lo = y0 - 1 < 0 ? 0 : y0 - 1, hi = y1 + 1 > P::H - 1 ? P::H - 1 : y1 + 1;
        for (int j = lo; j <= hi && !hit; ++j) hit = wet[j >> 5] >> (j & 31) & 1;
    }
    else if (y1 + 1 >= y0 - 1)
    {
        if (y1 + 1 >= 0 && y1 + 1 < P::H) hit = wet[(y1 + 1) >> 5] >> ((y1 + 1) & 31) & 1;
        if (!hit && y0 - 1 >= 0 && y0 - 1 < P::H) hit = wet[(y0 - 1) >> 5] >> ((y0 - 1) & 31) & 1;
    }
    return hit;
}

/* a block's spheres staged in shared memory, and the tunnels a target lists */
#define CV_STAGE 128
#define CV_LIST 1024
#define CV_APPLY_BLOCKS 2


/* segment s of tunnel t against the block's chunk, its spheres staged in
 * shared memory CV_STAGE at a time: first every staged sphere's water test
 * (each thread its column, one barrier for the stage: water never changes),
 * then the spheres in order, each thread carving its own column. 1 when the
 * segment ended (the reach test, or a room's carve). */
template <class P>
__device__ inline int cv_segment(const struct cv_work *w, const struct cv_tun *t, int s, int cx, int cz,
                                 const uint32_t *wet, uint8_t *col, int ci, int ck, const typename P::ctx_t *ctx,
                                 struct cv_sph *stage, uint32_t *wetf, struct cv_cut *cuts, uint8_t *rc)
{
    const float *profile = P::PROFILE ? w->prof + (size_t)t->prof * 256 : NULL;
    const struct cv_sph *sp = w->sph + t->seg_base[s];
    int n = t->seg_n[s];
    double reach = t->seg_reach[s];
    int room = t->room;
    for (int k0 = 0; k0 < n; k0 += CV_STAGE)
    {
        int m = n - k0 < CV_STAGE ? n - k0 : CV_STAGE;
        __syncthreads();
        /* thread k: staged sphere k's reach and box tests, once (they are
         * the target's, the same for every column, and in double) */
        if ((int)threadIdx.x < m)
        {
            stage[threadIdx.x] = sp[k0 + threadIdx.x];
            rc[threadIdx.x] = (uint8_t)cv_test<P>(&stage[threadIdx.x], reach, cx, cz, &cuts[threadIdx.x]);
        }
        if (threadIdx.x < CV_STAGE / 32) wetf[threadIdx.x] = 0;
        __syncthreads();
        for (int k = 0; k < m; ++k)
        {
            if (rc[k] == 2) break;
            if (rc[k] == 1 && cv_shell<P>(&cuts[k], wet, ci, ck)) atomicOr(&wetf[k >> 5], 1u << (k & 31));
        }
        __syncthreads();
        for (int k = 0; k < m; ++k)
        {
            if (rc[k] == 2) return 1;
            if (rc[k] == 0 || (wetf[k >> 5] >> (k & 31) & 1)) continue;
            const struct cv_cut *b = &cuts[k];
            if (ci >= b->x0 && ci < b->x1 && ck >= b->z0 && ck < b->z1)
                P::carve_col(ctx, col, ci, ck, stage[k].x, stage[k].y, stage[k].z, stage[k].w, stage[k].h, b->x0, b->y0,
                             b->y1, profile);
            if (room) return 1;
        }
    }
    return 0;
}

/* the block's (256 threads) exclusive scan of v; *total the sum */
__device__ inline int cv_scan(int v, int *wsum, int *total)
{
    int t = threadIdx.x, x = v;
    for (int o = 1; o < 32; o <<= 1)
    {
        int u = __shfl_up_sync(0xffffffffu, x, o);
        if ((t & 31) >= o) x += u;
    }
    __syncthreads();
    if ((t & 31) == 31) wsum[t >> 5] = x;
    __syncthreads();
    int before = 0, all = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (i < (t >> 5)) before += wsum[i];
        all += wsum[i];
    }
    *total = all;
    return before + x - v;
}

/* a tunnel no sphere of which passes the target's box test (a block of
 * slack for the test's own rounding) leaves the chunk alone */
__device__ inline int cv_reaches(const struct cv_box *bx, double ox, double oz)
{
    return !(bx->hi_x < ox - 17.0 || bx->lo_x > ox + 17.0 || bx->hi_z < oz - 17.0 || bx->lo_z > oz + 17.0);
}

template <class P>
__device__ inline void cv_tunnel(const struct cv_work *w, int q, int cx, int cz, const uint32_t *wet, uint8_t *col,
                                 int ci, int ck, const typename P::ctx_t *ctx, struct cv_sph *stage, uint32_t *wetf,
                                 struct cv_cut *cuts, uint8_t *rc)
{
    const struct cv_tun *tn = &w->tun[q];
    if (cv_segment<P>(w, tn, 0, cx, cz, wet, col, ci, ck, ctx, stage, wetf, cuts, rc)) return;
    if (tn->nseg == 3)
    {
        cv_segment<P>(w, tn, 1, cx, cz, wet, col, ci, ck, ctx, stage, wetf, cuts, rc);
        cv_segment<P>(w, tn, 2, cx, cz, wet, col, ci, ck, ctx, stage, wetf, cuts, rc);
    }
}

/* a block per target, a thread per column (blockDim 256). The block first
 * lists the tunnels that reach it, in the walk's order (the 289 sources a
 * thread each, a scan), then walks their spheres together; a target whose
 * list overflows walks its sources one by one. */
template <class P>
__global__ void __launch_bounds__(256, CV_APPLY_BLOCKS) carve_apply(const typename P::req_t *req, int n, struct cv_work w, typename P::ctx_t ctx)
{
    __shared__ int list[CV_LIST], wsum[8];
    __shared__ struct cv_sph stage[CV_STAGE];
    __shared__ uint32_t wetf[CV_STAGE / 32];
    __shared__ struct cv_cut cuts[CV_STAGE];
    __shared__ uint8_t rc[CV_STAGE];
    int t = blockIdx.x;
    if (!P::valid(req[t])) return;
    if (w.cnt->err)
    {
        if (threadIdx.x == 0) atomicOr(&d_err, w.cnt->err);
        return;
    }
    int c = threadIdx.x, ci = c >> 4, ck = c & 15;
    int cx = req[t].cx, cz = req[t].cz;
    double ox = (double)(cx * 16 + 8), oz = (double)(cz * 16 + 8);
    uint8_t *col = P::column(&ctx, t, ci, ck);
    /* the column's water (lava in the Nether) cells, a bit each: they never
     * change while the carvers run */
    uint32_t wet[P::H / 32];
    for (int q = 0; q < P::H / 32; ++q)
    {
        uint32_t b = 0;
        for (int j = 0; j < 32; ++j) b |= (uint32_t)P::wet(col[q * 32 + j]) << j;
        wet[q] = b;
    }
    typename P::ctx_t *cp = &ctx;
    P::target(cp, t);
    int nlist = 0, over = 0;
    for (int o0 = 0; o0 < CV_SRC; o0 += 256)
    {
        int o = o0 + c, first = 0, nt = 0, cnt = 0, total;
        if (o < CV_SRC)
        {
            int uid = w.ht_uid[w.tgt_slot[(size_t)t * CV_SRC + o]];
            if (uid >= 0)
            {
                first = w.src_first[uid];
                nt = w.src_n[uid];
                for (int q = first; q < first + nt; ++q) cnt += cv_reaches(&w.box[q], ox, oz);
            }
        }
        int at = cv_scan(cnt, wsum, &total);
        if (nlist + total > CV_LIST) over = 1;
        else if (cnt)
            for (int q = first, i = nlist + at; q < first + nt; ++q)
                if (cv_reaches(&w.box[q], ox, oz)) list[i++] = q;
        nlist += total;
    }
    __syncthreads();
    if (!over)
    {
        for (int i = 0; i < nlist; ++i) cv_tunnel<P>(&w, list[i], cx, cz, wet, col, ci, ck, cp, stage, wetf, cuts, rc);
        return;
    }
    for (int o = 0; o < CV_SRC; ++o)
    {
        int uid = w.ht_uid[w.tgt_slot[(size_t)t * CV_SRC + o]];
        if (uid < 0) continue;
        int first = w.src_first[uid], nt = w.src_n[uid];
        for (int q = first; q < first + nt; ++q)
            if (cv_reaches(&w.box[q], ox, oz)) cv_tunnel<P>(&w, q, cx, cz, wet, col, ci, ck, cp, stage, wetf, cuts, rc);
    }
}

}  // namespace

/* the three phases over a batch (the table and counters reset first) */
template <class P>
static int cv_run(struct cv_work *w, const typename P::seed_t *seeds, const typename P::req_t *req, int n,
                  typename P::ctx_t ctx, cudaStream_t s)
{
    if (n == 0) return 0;
    if (cv_reset(w, s)) return -1;
    int cells = n * CV_SRC;
    carve_insert<P><<<(cells + 255) / 256, 256, 0, s>>>(req, n, *w);
    carve_sources<P><<<(cells + 127) / 128 < 1024 ? (cells + 127) / 128 : 1024, 128, 0, s>>>(seeds, *w);
    carve_walk<P><<<256, 64, 0, s>>>(*w);
    carve_apply<P><<<n, 256, 0, s>>>(req, n, *w, ctx);
    return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

#endif
