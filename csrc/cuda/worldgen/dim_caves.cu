/* nether.c's MapGenCavesHell in the two phases of carve.cuh: each source
 * chunk's tunnels walked once, then each Nether chunk carved a thread a
 * column (the End has no carver). */
#include "carve.cuh"

namespace {

struct hell_ctx {
    const struct worldgen_dim_req *req;
    uint8_t *raw_all;
    int cx, cz;
};

/* MapGenCavesHell.func_151541_a: as MapGenCaves's, a tunnel branches at most
 * once, into two of width below 1.0f that cannot branch */
template <bool TOP>
__device__ static __noinline__ void hell_walk(struct cv_work *w, struct cv_tun *t, struct cv_box *bx, int seg, int64_t seed, double x,
                                              double y, double z, float width, float yaw, float pitch, int step,
                                              int steps, double yscale)
{
    float dyaw = 0.0f, dpitch = 0.0f;
    float yaw_k = c_dneg == WORLDGEN_DIM_NEG_CAVES ? 4.01f : 4.0f;
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
    /* MapGenCavesHell has no steps > 0 test here, unlike MapGenCaves */
    int branches = !room && width > 1.0f && branch >= step && branch < steps;
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
            hell_walk<false>(w, t, bx, 1, s1, x, y, z, w1, yaw - CV_PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            int64_t s2 = jr_long(&r);
            float w2 = jr_float(&r) * 0.5f + 0.5f;
            hell_walk<false>(w, t, bx, 2, s2, x, y, z, w2, yaw + CV_PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            return;
        }
        if (!room && jr_int_n(&r, 4) == 0) continue;
        cv_reach(bx, x, z, wd);
        if (base >= 0)
        {
            struct cv_sph *s = &w->sph[base + n++];
            s->x = x;
            s->y = y;
            s->z = z;
            s->w = wd;
            s->h = h;
            s->left = steps - step;
        }
    }
    t->seg_n[seg] = base < 0 ? 0 : n;
}

struct hell_p {
    typedef struct worldgen_dim_req req_t;
    typedef struct cd_seed seed_t;
    typedef struct hell_ctx ctx_t;
    static const int H = 128, YTOP = 120;
    static const bool PROFILE = false;
    __device__ static bool valid(const struct worldgen_dim_req &r) { return r.dim == -1; }
    __device__ static uint8_t *column(hell_ctx *c, int t, int i, int k)
    {
        return c->raw_all + (size_t)t * WORLDGEN_DIM_RAW + (size_t)(i * 16 + k) * 128;
    }
    __device__ static void target(hell_ctx *c, int t)
    {
        c->cx = c->req[t].cx;
        c->cz = c->req[t].cz;
    }
    __device__ static int wet(uint8_t id) { return id == BLK_FLOWING_LAVA || id == BLK_LAVA; }
    /* hell_carve's second loop for column (i, k): the cell written is one
     * above the y tested (vanilla's off-by-one) */
    __device__ static void carve_col(const hell_ctx *c, uint8_t *col, int i, int k, double x, double y, double z,
                                     double w, double h, int x0, int y0, int y1, const float *profile)
    {
        double ex = ((double)(i + c->cx * 16) + 0.5 - x) / w;
        double ez = ((double)(k + c->cz * 16) + 0.5 - z) / w;
        for (int j = y1 - 1; j >= y0; --j)
        {
            double ey = ((double)j + 0.5 - y) / h;
            if (!(ey > -0.7 && ex * ex + ey * ey + ez * ez < 1.0)) continue;
            uint8_t b = col[j + 1];
            if (b == BLK_NETHERRACK || b == BLK_DIRT || b == BLK_GRASS) col[j + 1] = BLK_AIR;
        }
    }
    /* hell_source: the caves that start in chunk (sx, sz) */
    __device__ static int source(const struct cd_seed *s, int sx, int sz, struct cv_tun *out)
    {
        jrand rr, *r = &rr;
        jr_seed(r, (int64_t)(((uint64_t)(int64_t)sx * (uint64_t)s->carve_kx) ^
                             ((uint64_t)(int64_t)sz * (uint64_t)s->carve_kz) ^ (uint64_t)s->seed));
        int k = 0;
        int a = jr_int_n(r, 10);
        int b = jr_int_n(r, a + 1);
        int n = jr_int_n(r, b + 1);
        if (jr_int_n(r, 5) != 0) n = 0;
        for (int i = 0; i < n; ++i)
        {
            double x = (double)(sx * 16 + jr_int_n(r, 16));
            double y = (double)jr_int_n(r, 128);
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
                w *= 2.0f;
                int64_t ts = jr_long(r);
                if (out) out[k] = cv_tun_make(ts, x, y, z, 0.5, w, yaw, pitch, 0, 0);
                ++k;
            }
        }
        return k;
    }
    __device__ static void walk(struct cv_work *w, struct cv_tun *t, struct cv_box *bx)
    {
        hell_walk<true>(w, t, bx, 0, t->seed, t->x, t->y, t->z, t->width, t->yaw, t->pitch, t->step, t->steps, t->yscale);
    }
};

}  // namespace

int worldgen_dim_carve_run(struct cv_work *w, const struct cd_seed *seeds, const struct worldgen_dim_req *req, int n, uint8_t *raw_all,
                    cudaStream_t s)
{
    struct hell_ctx c = {req, raw_all, 0, 0};
    return cv_run<hell_p>(w, seeds, req, n, c, s);
}
