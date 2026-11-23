/* The overworld's carvers, carve.c's MapGenCaves and MapGenRavine, in the two
 * phases of carve.cuh (the sources' tunnels walked once, then each
 * target chunk carved a thread a column), and the carvers' work area. */
#include "carve.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the work area */

int cv_work_alloc(struct cv_work *w, int max_batch)
{
    memset(w, 0, sizeof *w);
    w->max_batch = max_batch;
    long long cells = (long long)max_batch * CV_SRC;
    int bits = 10;
    while ((1ll << bits) < 2 * cells) ++bits;
    w->ht_bits = bits;
    w->cap_uniq = (int)cells;
    /* a source lists 0 tunnels six times in seven; a busy one 70 at most,
     * and a tunnel's steps (at most 112, a branch's children 2 x 84 more)
     * make spheres three steps in four */
    w->cap_tun = (int)(cells < 65536 ? 65536 : cells);
    w->cap_sph = 256 * w->cap_tun > (1 << 24) ? 1 << 24 : 256 * w->cap_tun;
    w->cap_prof = w->cap_uniq / 8 < 4096 ? 4096 : w->cap_uniq / 8;
    if (cudaMalloc(&w->ht_key, sizeof(unsigned long long) << bits) != cudaSuccess ||
        cudaMalloc(&w->ht_uid, sizeof(int) << bits) != cudaSuccess ||
        cudaMalloc(&w->tgt_slot, sizeof(int) * (size_t)cells) != cudaSuccess ||
        cudaMalloc(&w->uniq_key, sizeof(unsigned long long) * (size_t)w->cap_uniq) != cudaSuccess ||
        cudaMalloc(&w->src_first, sizeof(int) * (size_t)w->cap_uniq) != cudaSuccess ||
        cudaMalloc(&w->src_n, sizeof(int) * (size_t)w->cap_uniq) != cudaSuccess ||
        cudaMalloc(&w->tun, sizeof(struct cv_tun) * (size_t)w->cap_tun) != cudaSuccess ||
        cudaMalloc(&w->box, sizeof(struct cv_box) * (size_t)w->cap_tun) != cudaSuccess ||
        cudaMalloc(&w->sph, sizeof(struct cv_sph) * (size_t)w->cap_sph) != cudaSuccess ||
        cudaMalloc(&w->prof, sizeof(float) * 256 * (size_t)w->cap_prof) != cudaSuccess ||
        cudaMalloc(&w->cnt, sizeof(struct cv_counts)) != cudaSuccess)
    {
        fprintf(stderr, "worldgen: the carvers' work area for %d chunks does not fit\n", max_batch);
        cv_work_free(w);
        return -1;
    }
    return 0;
}

void cv_work_free(struct cv_work *w)
{
    cudaFree(w->ht_key);
    cudaFree(w->ht_uid);
    cudaFree(w->tgt_slot);
    cudaFree(w->uniq_key);
    cudaFree(w->src_first);
    cudaFree(w->src_n);
    cudaFree(w->tun);
    cudaFree(w->box);
    cudaFree(w->sph);
    cudaFree(w->prof);
    cudaFree(w->cnt);
    memset(w, 0, sizeof *w);
}

int cv_reset(struct cv_work *w, cudaStream_t s)
{
    if (cudaMemsetAsync(w->ht_key, 0, sizeof(unsigned long long) << w->ht_bits, s) != cudaSuccess ||
        cudaMemsetAsync(w->cnt, 0, sizeof(struct cv_counts), s) != cudaSuccess)
        return -1;
    return 0;
}

/* ---------------------------------------------------------------- the two carvers */

namespace {

struct cg_ctx {
    const struct worldgen_req *req;
    const uint8_t *full256, *tops_all;
    uint8_t *ids_all;
    int cx, cz;
    const uint8_t *biomes, *tops;
};

/* what the caves and the ravines share: the overworld chunk, water */
struct cg_base {
    typedef struct worldgen_req req_t;
    typedef struct cg_seed seed_t;
    typedef struct cg_ctx ctx_t;
    static const int H = 256, YTOP = 248;
    __device__ static bool valid(const struct worldgen_req &) { return true; }
    __device__ static uint8_t *column(cg_ctx *c, int t, int i, int k)
    {
        return c->ids_all + (size_t)t * CELLS + (size_t)(i * 16 + k) * 256;
    }
    __device__ static void target(cg_ctx *c, int t)
    {
        c->cx = c->req[t].cx;
        c->cz = c->req[t].cz;
        c->biomes = c->full256 + (size_t)t * 256;
        c->tops = c->tops_all + (size_t)t * 256;
    }
    __device__ static int wet(uint8_t id) { return id == BLK_FLOWING_WATER || id == BLK_WATER; }
    /* carve.c carve's second loop for column (i, k): the cell written is one
     * above the y tested (vanilla's off-by-one) */
    __device__ static void carve_col(const cg_ctx *c, uint8_t *col, int i, int k, double x, double y, double z,
                                     double w, double h, int x0, int y0, int y1, const float *profile)
    {
        double ex = ((double)(i + c->cx * 16) + 0.5 - x) / w;
        double ez = ((double)(k + c->cz * 16) + 0.5 - z) / w;
        if (!(ex * ex + ez * ez < 1.0)) return;
        int grass = 0;
        for (int j = y1 - 1; j >= y0; --j)
        {
            double ey = ((double)j + 0.5 - y) / h;
            int in = profile ? (ex * ex + ez * ez) * (double)profile[j] + ey * ey / 6.0 < 1.0
                             : ey > -0.7 && ex * ex + ey * ey + ez * ez < 1.0;
            if (!in) continue;
            int id = col[j + 1];
            if (id == BLK_GRASS) grass = 1;
            if (id != BLK_STONE && id != BLK_DIRT && id != BLK_GRASS) continue;
            if (j < 10)
                col[j + 1] = profile ? BLK_FLOWING_LAVA : BLK_LAVA;
            else
            {
                col[j + 1] = BLK_AIR;
                if (grass && col[j] == BLK_DIRT) col[j] = c->tops[c->biomes[i + k * 16]];
            }
        }
    }
};

__device__ inline void cv_put(struct cv_work *w, struct cv_box *bx, int base, int *n, double x, double y, double z,
                              double wd, double h, int left)
{
    cv_reach(bx, x, z, wd);
    if (base < 0) return;
    struct cv_sph *s = &w->sph[base + (*n)++];
    s->x = x;
    s->y = y;
    s->z = z;
    s->w = wd;
    s->h = h;
    s->left = left;
}

/* MapGenCaves.func_151541_a's steps from step on, as spheres; a top tunnel's
 * branch walks its two children after it (segments 1 and 2) */
template <bool TOP>
__device__ static __noinline__ void cave_walk(struct cv_work *w, struct cv_tun *t, struct cv_box *bx, int seg, int64_t seed, double x,
                                              double y, double z, float width, float yaw, float pitch, int step,
                                              int steps, double yscale)
{
    float dyaw = 0.0f, dpitch = 0.0f;
    float yaw_k = c_negative == WORLDGEN_NEG_CAVES ? 4.01f : 4.0f;
    jrand r;
    jr_seed(&r, seed);
    if (steps <= 0)
    {
        int m = CV_RANGE * 16 - 16;
        steps = m - jr_int_n(&r, m / 4);
    }
    int room = 0;
    if (step == -1)
    {
        step = steps / 2;
        room = 1;
    }
    int branch = jr_int_n(&r, steps / 2) + steps / 4;
    int branches = !room && width > 1.0f && steps > 0 && branch >= step && branch < steps;
    if (!TOP && branches)
    {
        atomicOr(&d_err, WORLDGEN_ERR_BRANCH);
        branches = 0;
    }
    int base = cv_take(w, (branches ? branch : steps) - step), n = 0;
    t->seg_base[seg] = base < 0 ? 0 : base;
    t->seg_reach[seg] = (double)(width + 2.0f + 16.0f);
    if (seg == 0)
    {
        t->room = room;
        t->nseg = 1;
    }
    for (int steep = jr_int_n(&r, 6) == 0; step < steps; ++step)
    {
        double wd = 1.5 + (double)(mh_sin((float)step * CV_PI_F / (float)steps) * width * 1.0f);
        double h = wd * yscale;
        float cp = mh_cos(pitch), sp = mh_sin(pitch);
        x += (double)(mh_cos(yaw) * cp);
        y += (double)sp;
        z += (double)(mh_sin(yaw) * cp);
        pitch *= steep ? 0.92f : 0.7f;
        pitch += dpitch * 0.1f;
        yaw += dyaw * 0.1f;
        dpitch *= 0.9f;
        dyaw *= 0.75f;
        float a = jr_float(&r), b = jr_float(&r), d = jr_float(&r);
        dpitch += (a - b) * d * 2.0f;
        a = jr_float(&r);
        b = jr_float(&r);
        d = jr_float(&r);
        dyaw += (a - b) * d * yaw_k;

        if (TOP && branches && step == branch)
        {
            t->seg_n[0] = base < 0 ? 0 : n;
            t->nseg = 3;
            int64_t s1 = jr_long(&r);
            float w1 = jr_float(&r) * 0.5f + 0.5f;
            cave_walk<false>(w, t, bx, 1, s1, x, y, z, w1, yaw - CV_PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            int64_t s2 = jr_long(&r);
            float w2 = jr_float(&r) * 0.5f + 0.5f;
            cave_walk<false>(w, t, bx, 2, s2, x, y, z, w2, yaw + CV_PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            return;
        }
        if (!room && jr_int_n(&r, 4) == 0) continue;
        cv_put(w, bx, base, &n, x, y, z, wd, h, steps - step);
    }
    t->seg_n[seg] = base < 0 ? 0 : n;
}

struct cave_p : cg_base {
    static const bool PROFILE = false;
    /* MapGenCaves.func_151538_a: the tunnels source (sx, sz) starts */
    __device__ static int source(const struct cg_seed *s, int sx, int sz, struct cv_tun *out)
    {
        jrand rr, *r = &rr;
        jr_seed(r, (int64_t)(((uint64_t)(int64_t)sx * (uint64_t)s->carve_kx) ^
                             ((uint64_t)(int64_t)sz * (uint64_t)s->carve_kz) ^ (uint64_t)s->seed));
        int k = 0;
        int a = jr_int_n(r, 15) + 1;
        int b = jr_int_n(r, a) + 1;
        int n = jr_int_n(r, b);
        if (jr_int_n(r, 7) != 0) n = 0;
        for (int i = 0; i < n; ++i)
        {
            double x = (double)(sx * 16 + jr_int_n(r, 16));
            double y = (double)jr_int_n(r, jr_int_n(r, 120) + 8);
            double z = (double)(sz * 16 + jr_int_n(r, 16));
            int tunnels = 1;
            if (jr_int_n(r, 4) == 0)
            {
                int64_t ts = jr_long(r);
                float w = 1.0f + jr_float(r) * 6.0f;
                if (out) out[k] = cv_tun_make(ts, x, y, z, 0.5, w, 0.0f, 0.0f, -1, -1);
                ++k;
                tunnels += jr_int_n(r, 4);
            }
            for (int j = 0; j < tunnels; ++j)
            {
                float yaw = jr_float(r) * CV_PI_F * 2.0f;
                float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
                float w = jr_float(r) * 2.0f;
                w += jr_float(r);
                if (jr_int_n(r, 10) == 0)
                {
                    float f = jr_float(r);
                    f *= jr_float(r);
                    w *= f * 3.0f + 1.0f;
                }
                int64_t ts = jr_long(r);
                if (out) out[k] = cv_tun_make(ts, x, y, z, 1.0, w, yaw, pitch, 0, 0);
                ++k;
            }
        }
        return k;
    }
    __device__ static void walk(struct cv_work *w, struct cv_tun *t, struct cv_box *bx)
    {
        cave_walk<true>(w, t, bx, 0, t->seed, t->x, t->y, t->z, t->width, t->yaw, t->pitch, t->step, t->steps, t->yscale);
    }
};

struct ravine_p : cg_base {
    static const bool PROFILE = true;
    /* MapGenRavine.func_151538_a */
    __device__ static int source(const struct cg_seed *s, int sx, int sz, struct cv_tun *out)
    {
        jrand rr, *r = &rr;
        jr_seed(r, (int64_t)(((uint64_t)(int64_t)sx * (uint64_t)s->carve_kx) ^
                             ((uint64_t)(int64_t)sz * (uint64_t)s->carve_kz) ^ (uint64_t)s->seed));
        if (jr_int_n(r, 50) != 0) return 0;
        double x = (double)(sx * 16 + jr_int_n(r, 16));
        double y = (double)(jr_int_n(r, jr_int_n(r, 40) + 8) + 20);
        double z = (double)(sz * 16 + jr_int_n(r, 16));
        float yaw = jr_float(r) * CV_PI_F * 2.0f;
        float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
        float w = jr_float(r) * 2.0f;
        w = (w + jr_float(r)) * 2.0f;
        int64_t ts = jr_long(r);
        if (out) out[0] = cv_tun_make(ts, x, y, z, 3.0, w, yaw, pitch, 0, 0);
        return 1;
    }
    /* MapGenRavine.func_151540_a: no branches, no rooms */
    __device__ static void walk(struct cv_work *w, struct cv_tun *t, struct cv_box *bx)
    {
        jrand r;
        jr_seed(&r, t->seed);
        double x = t->x, y = t->y, z = t->z, yscale = t->yscale;
        float width = t->width, yaw = t->yaw, pitch = t->pitch;
        float dyaw = 0.0f, dpitch = 0.0f;
        float yaw_k = c_negative == WORLDGEN_NEG_RAVINES ? 0.06f : 0.05f;
        int step = t->step, steps = t->steps;
        if (steps <= 0)
        {
            int m = CV_RANGE * 16 - 16;
            steps = m - jr_int_n(&r, m / 4);
        }
        int room = 0;
        if (step == -1)
        {
            step = steps / 2;
            room = 1;
        }
        t->room = room;
        t->nseg = 1;
        t->seg_reach[0] = (double)(width + 2.0f + 16.0f);
        int p = atomicAdd(&w->cnt->nprof, 1);
        if (p >= w->cap_prof)
        {
            atomicOr(&w->cnt->err, WORLDGEN_ERR_CARVE_CAP);
            p = 0;
        }
        t->prof = p;
        float *profile = w->prof + (size_t)p * 256, f = 1.0f;
        for (int i = 0; i < 256; ++i)
        {
            if (i == 0 || jr_int_n(&r, 3) == 0)
            {
                float a = jr_float(&r), b = jr_float(&r);
                f = 1.0f + a * b * 1.0f;
            }
            profile[i] = f * f;
        }
        int base = cv_take(w, steps - step), n = 0;
        t->seg_base[0] = base < 0 ? 0 : base;
        for (; step < steps; ++step)
        {
            double wd = 1.5 + (double)(mh_sin((float)step * CV_PI_F / (float)steps) * width * 1.0f);
            double h = wd * yscale;
            wd *= (double)jr_float(&r) * 0.25 + 0.75;
            h *= (double)jr_float(&r) * 0.25 + 0.75;
            float cp = mh_cos(pitch), sp = mh_sin(pitch);
            x += (double)(mh_cos(yaw) * cp);
            y += (double)sp;
            z += (double)(mh_sin(yaw) * cp);
            pitch *= 0.7f;
            pitch += dpitch * 0.05f;
            yaw += dyaw * yaw_k;
            dpitch *= 0.8f;
            dyaw *= 0.5f;
            float a = jr_float(&r), b = jr_float(&r), d = jr_float(&r);
            dpitch += (a - b) * d * 2.0f;
            a = jr_float(&r);
            b = jr_float(&r);
            d = jr_float(&r);
            dyaw += (a - b) * d * 4.0f;

            if (!room && jr_int_n(&r, 4) == 0) continue;
            cv_put(w, bx, base, &n, x, y, z, wd, h, steps - step);
        }
        t->seg_n[0] = base < 0 ? 0 : n;
    }
};

}  // namespace

int worldgen_carve_run(struct cv_work *w, int ravines, const struct cg_seed *seeds, const struct worldgen_req *req, int n,
                    const uint8_t *full256, const uint8_t *tops_all, uint8_t *ids_all, cudaStream_t s)
{
    struct cg_ctx c = {req, full256, tops_all, ids_all, 0, 0, NULL, NULL};
    return ravines ? cv_run<ravine_p>(w, seeds, req, n, c, s) : cv_run<cave_p>(w, seeds, req, n, c, s);
}
