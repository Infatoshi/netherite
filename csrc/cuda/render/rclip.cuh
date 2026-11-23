/* The clipper by recorded operations (lane/cliprep): raster.c
 * clip_triangle's and raster_entities.c clip_and_draw's Sutherland-Hodgman
 * over a triangle's clip coordinates alone. The polygon is the ids of its
 * vertices (four bits a vertex, in a register: 0 to 2 the triangle's, 3 on
 * the vertices the planes make, in the order made), and each made vertex
 * is recorded as the operation that made it: the ids of its edge's start
 * and end and its t. Every other attribute of a vertex is then made by
 * replaying those operations in that order (rc_replay): the start's value
 * plus t times the end's less the start's, the interpolations' own
 * operands and operations, so (--fmad=false) the same bits as the
 * polygon arrays of whole vertices gave, which lived in local memory and
 * were the kernels' stalls (lane/kernclimb3: 52% long scoreboard). A
 * thread's values by id (each id's clip coordinates, then one attribute's
 * or two's) are its column of its block's table in shared memory (rc_tab:
 * word w at T[w * GBLOCK], so a warp's lanes meet 32 banks), read by id
 * with one load where a select among the ids took two instructions an id.
 * A polygon that makes more vertices than RC_K, or holds more than RC_N,
 * is left to the arrays' clipper, as before. */
#ifndef NETHERITE_RENDER_RCLIP_CUH
#define NETHERITE_RENDER_RCLIP_CUH

#include "dev.cuh"

enum { RC_K = 6, RC_S = 3 + RC_K, RC_N = 9, RC_W = 4 * RC_S };

/* a kernel's table (its blocks GBLOCK threads): the kernel declares one
 * (18 KB a block) and hands each thread's column (NAME + threadIdx.x) to
 * its clippers, which take turns in it */
#define RC_TAB(name) __shared__ float name[RC_W * GBLOCK]

struct rcpoly {
    float *T;                           /* the thread's column of its block's table */
    float t[RC_K];                      /* each made vertex's t */
    uint64_t poly;                      /* the polygon's ids, vertex i at bits 4 i */
    uint64_t src;                       /* made vertex k's start id at bits 8 k, its end's at 8 k + 4 */
    int n, nops;
};

static __device__ __forceinline__ int rc_id(const rcpoly &r, int i) { return (int)(r.poly >> (4 * i) & 15); }
static __device__ __forceinline__ float &rc_w(const rcpoly &r, int w) { return r.T[w * GBLOCK]; }

/* id ID's clip coordinates (words 4 ID on, until the fan's are made) */
static __device__ __forceinline__ void rc_pos(const rcpoly &r, int id, float c[4])
{
#pragma unroll
    for (int k = 0; k < 4; ++k) c[k] = rc_w(r, 4 * id + k);
}

/* the planes a vertex is outside (bit P: clip_triangle's distance to plane
 * P not >= 0), each the clipper's own operations */
static __device__ __forceinline__ uint32_t rc_out(const float c[4])
{
    uint32_t m = 0;
#pragma unroll
    for (int plane = 0; plane < 6; ++plane) {
        int axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        m |= (uint32_t)!(c[3] - sign * c[axis] >= 0) << plane;
    }
    return m;
}

/* Sutherland-Hodgman of the triangle IN's clip coordinates, plane by
 * plane, in the table at T: a plane every vertex is inside is skipped (it
 * would copy the polygon), and each edge's distances, t and new clip
 * coordinates are the arrays' clipper's operations. 1: R holds the
 * polygon; 0: it kept fewer than three vertices (nothing drawn); -1: it
 * does not fit R. */
static __device__ __forceinline__ int rc_clip(const float (&in)[3][4], float *T, rcpoly &r)
{
    uint64_t ocs = 0;                   /* each id's rc_out, 6 bits an id */
    uint32_t orm = 0;                   /* the polygon's, or'ed */
    r.T = T;
#pragma unroll
    for (int i = 0; i < 3; ++i) {
#pragma unroll
        for (int k = 0; k < 4; ++k) rc_w(r, 4 * i + k) = in[i][k];
        uint32_t m = rc_out(in[i]);
        ocs |= (uint64_t)m << (6 * i);
        orm |= m;
    }
#pragma unroll
    for (int k = 0; k < RC_K; ++k) r.t[k] = 0;
    r.poly = 0x210;
    r.src = 0;
    r.n = 3;
    r.nops = 0;
    for (int plane = 0; plane < 6; ++plane) {
        if (!(orm >> plane & 1)) continue;
        int axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        uint64_t np = 0;
        uint32_t nor = 0;
        int m = 0, p = rc_id(r, 0);
        float P[4];
        rc_pos(r, p, P);
        float dp = P[3] - sign * P[axis];
        for (int i = 0; i < r.n; ++i) {
            int q = rc_id(r, i + 1 < r.n ? i + 1 : 0);
            float Q[4];
            rc_pos(r, q, Q);
            float dq = Q[3] - sign * Q[axis];
            if (dp >= 0) {
                if (m == RC_N) return -1;
                np |= (uint64_t)p << (4 * m++);
                nor |= (uint32_t)(ocs >> (6 * p)) & 63;
            }
            if ((dp >= 0) != (dq >= 0)) {
                if (m == RC_N || r.nops == RC_K) return -1;
                float t = dp / (dp - dq), v[4];
#pragma unroll
                for (int k = 0; k < 4; ++k) v[k] = P[k] + t * (Q[k] - P[k]);
                int id = 3 + r.nops;
#pragma unroll
                for (int k = 0; k < 4; ++k) rc_w(r, 4 * id + k) = v[k];
#pragma unroll
                for (int s = 0; s < RC_K; ++s)
                    if (s == r.nops) r.t[s] = t;
                r.src |= (uint64_t)(p | q << 4) << (8 * r.nops++);
                uint32_t oc = rc_out(v);
                ocs |= (uint64_t)oc << (6 * id);
                nor |= oc;
                np |= (uint64_t)id << (4 * m++);
            }
            p = q;
#pragma unroll
            for (int k = 0; k < 4; ++k) P[k] = Q[k];
            dp = dq;
        }
        r.n = m;
        r.poly = np;
        orm = nor;
        if (m < 3) return 0;
    }
    return 1;
}

/* An attribute of every id, once the fan's screen fields are made (they
 * were the last to read the clip coordinates): field F's values at words F
 * on (F 0 or RC_S). rc_set puts the triangle's vertex I's, then rc_replay
 * makes each made vertex's in the order made, as the interpolation made it
 * (its start's value plus t times the end's less the start's), or
 * rc_replay_start as the start's (an attribute the interpolation copies:
 * raster_entities.c's colour without vertex_color); rc_val reads id ID's. */
static __device__ __forceinline__ void rc_set(const rcpoly &r, int F, int i, float x) { rc_w(r, F + i) = x; }
static __device__ __forceinline__ float rc_val(const rcpoly &r, int F, int id) { return rc_w(r, F + id); }

static __device__ __forceinline__ void rc_replay(const rcpoly &r, int F)
{
#pragma unroll
    for (int k = 0; k < RC_K; ++k)
        if (k < r.nops) {
            float fa = rc_val(r, F, (int)(r.src >> (8 * k) & 15)), fb = rc_val(r, F, (int)(r.src >> (8 * k + 4) & 15));
            rc_set(r, F, 3 + k, fa + r.t[k] * (fb - fa));
        }
}

static __device__ __forceinline__ void rc_replay_start(const rcpoly &r, int F)
{
#pragma unroll
    for (int k = 0; k < RC_K; ++k)
        if (k < r.nops) rc_set(r, F, 3 + k, rc_val(r, F, (int)(r.src >> (8 * k) & 15)));
}

#endif
