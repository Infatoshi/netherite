/* The section quads' lean kernel (render.h, lane/renderbuild's split of the
 * geometry kernels): raster.c live_draw_section's quads whose triangles
 * clip_triangle keeps whole or drops whole, the rest handed to the generic
 * kernel (geom_gen.cu).
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#include "geom.cuh"

/* make_vertex's position half: clip coordinates and the fog distance */
static __device__ __forceinline__ void quad_pos(const envp &P, const int32_t *raw, int cx, int s, int cz, float clip[4], float *fog)
{
    int bx = cx * 16, by = s * 16, bz = cz * 16;
    float pos[4];
    cam_pos(P, raw, bx, by, bz, pos);
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = P.mv[r] * pos[0] + P.mv[4 + r] * pos[1] + P.mv[8 + r] * pos[2] + P.mv[12 + r] * pos[3];
    for (int r = 0; r < 4; ++r)
        clip[r] = P.proj[r] * eye[0] + P.proj[4 + r] * eye[1] + P.proj[8 + r] * eye[2] + P.proj[12 + r] * eye[3];
    *fog = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
}

struct sv { float x, y, sx, sy, w, d; };

static __device__ __forceinline__ sv screen_of(int W, int H, const float *c)
{
    sv v;
    v.w = c[3];
    v.x = (c[0] / v.w * 0.5f + 0.5f) * W;
    v.y = (0.5f - c[1] / v.w * 0.5f) * H;
    v.sx = rintf((v.x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.sy = rintf((v.y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v.d = c[2] / v.w * 0.5f + 0.5f;
    return v;
}

/* live_emit's tests for one fast triangle; its box, or 0 */
static __device__ __forceinline__ int fast_box(int W, int H, const sv &a, const sv &b, const sv &c, float *area, short box[4])
{
    *area = edge_xy(a.x, a.y, b.x, b.y, c.x, c.y);
    if (*area == 0 || *area < 0) return 0;   /* live_emit's test (sections are one-sided; NaN draws) */
    float minx = fminf(a.sx, fminf(b.sx, c.sx)), maxx = fmaxf(a.sx, fmaxf(b.sx, c.sx));
    float miny = fminf(a.sy, fminf(b.sy, c.sy)), maxy = fmaxf(a.sy, fmaxf(b.sy, c.sy));
    int x0 = f2i(fmaxf(0, floorf(minx))), x1 = f2i(fminf((float)(W - 1), ceilf(maxx)));
    int y0 = f2i(fmaxf(0, floorf(miny))), y1 = f2i(fminf((float)(H - 1), ceilf(maxy)));
    if (x0 > x1 || y0 > y1) return 0;
    box[0] = (short)x0; box[1] = (short)x1; box[2] = (short)y0; box[3] = (short)y1;
    return 1;
}

template <bool WRITE>
__global__ void __launch_bounds__(256)
quads(const envp *P, const ddraw *draws, uint32_t draw0, const int32_t *mesh, const int32_t *orders, int W, int H,
        uint32_t *counts, xunit *clip_list, uint32_t *nclip,
        const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles)
{
    /* one block per draw: its environment's camera in shared memory */
    __shared__ ddraw d;
    __shared__ envp E;
    if (threadIdx.x == 0) d = draws[draw0 + blockIdx.x];
    __syncthreads();
    {
        const uint32_t *src = (const uint32_t *)&P[d.env];
        uint32_t *dst = (uint32_t *)&E;
        /* the fields the quads read: the matrices, the camera and the precision lead envp */
        for (uint32_t w = threadIdx.x; w < offsetof(envp, fog) / 4; w += blockDim.x) dst[w] = src[w];
    }
    __syncthreads();
    for (uint32_t i = threadIdx.x; i < d.nquads; i += blockDim.x) {
    uint32_t g = d.unit0 + i;
    /* the write: a quad the count found no triangle in writes none (a back
     * face, off the screen, or the clipper's quad that clips away) */
    if (WRITE && offsets[g + 1] == offsets[g]) continue;
    int offset = d.order >= 0 ? orders[d.order + i] : (int)i * 4;
    /* the quad's 128 bytes as eight 16-byte loads (lane/coalesce; its words
     * were a load each), aligned: mesh_alloc hands out whole quads; the
     * count reads the positions' half */
    union { uint4 q[8]; int32_t w[32]; } rq;
    const uint4 *M = (const uint4 *)(mesh + d.mesh) + (int64_t)offset * 2;
#pragma unroll
    for (int p = 0; p < 8; ++p)
        if (WRITE || !(p & 1)) rq.q[p] = M[p];
    const int32_t *raw = rq.w;
    float c[4][4], fog[4];
    for (int j = 0; j < 4; ++j) quad_pos(E, raw + j * 8, d.cx, d.s, d.cz, c[j], &fog[j]);
    int ka = classify(c[0], c[1], c[3]), kb = classify(c[1], c[2], c[3]);
    if (ka == 2 || kb == 2) {
        if (!WRITE) clip_list[atomicAdd(nclip, 1u)] = xunit{g, XU_QUAD, blockIdx.x + draw0, i};
        continue;
    }
    sv v[4];
    for (int j = 0; j < 4; ++j) v[j] = screen_of(W, H, c[j]);
    const int tv[2][3] = {{0, 1, 3}, {1, 2, 3}};
    uint32_t cnt = 0, at = WRITE ? offsets[g] - tri_base : 0;
    for (int t = 0; t < 2; ++t) {
        if ((t == 0 ? ka : kb) != 0) continue;
        float area;
        short box[4];
        const int *k = tv[t];
        if (!fast_box(W, H, v[k[0]], v[k[1]], v[k[2]], &area, box)) continue;
        if (WRITE) {
            /* built here, then stored in uint4s (a thread's triangle is
             * 240 bytes of its own: word stores used a sector each) */
            union { tri t; uint4 q[TRI_QUADS]; } ob;
            tri &o = ob.t;
            o.aux = o.tex = o.pad[0] = o.pad[1] = o.pad[2] = 0;
            for (int m = 0; m < 3; ++m) {
                const sv &q = v[k[m]];
                const int32_t *rw = raw + k[m] * 8;
                o.x[m] = q.x; o.y[m] = q.y; o.sx[m] = q.sx; o.sy[m] = q.sy; o.d[m] = q.d;
                o.w[m] = E.prec & RP_RCP ? 1.0f / q.w : q.w;     /* raster_prec.h RP_RCP */
                o.u[m] = __int_as_float(rw[3]);
                o.v[m] = __int_as_float(rw[4]);
                uint32_t col = d.flags & 2 ? (uint32_t)rw[5] : 0xffffffffU;
                float cf[4];
                for (int ch = 0; ch < 4; ++ch) cf[ch] = ((col >> (8 * ch)) & 255) / 255.0f;
                float l0 = d.flags & 4 ? (float)((uint32_t)rw[7] & 65535) : 240;
                float l1 = d.flags & 4 ? (float)((uint32_t)rw[7] >> 16) : 240;
                if (E.prec == RP_FAST) {
                    /* raster_prec.h's fast mode: as halves (tri_ch, tri_lh) */
                    tri_ch(o)[m * 2] = __floats2half2_rn(cf[0], cf[1]);
                    tri_ch(o)[m * 2 + 1] = __floats2half2_rn(cf[2], cf[3]);
                    tri_lh(o)[m] = __floats2half2_rn(l0, l1);
                } else {
                    for (int ch = 0; ch < 4; ++ch) o.c[m][ch] = cf[ch];
                    o.l[m][0] = l0;
                    o.l[m][1] = l1;
                }
                o.fog[m] = fog[k[m]];
            }
            o.area = E.prec & RP_RCP ? 1.0f / area : area;
            o.x0 = box[0]; o.x1 = box[1]; o.y0 = box[2]; o.y1 = box[3];
            o.mode = d.water ? 32u : 0u;
            uint4 *dst = (uint4 *)&out[at];
            for (int j = 0; j < TRI_QUADS; ++j) dst[j] = ob.q[j];
            tiles[at] = (uint32_t)((box[1] / TILE - box[0] / TILE + 1) * (box[3] / TILE - box[2] / TILE + 1));
            ++at;
        }
        ++cnt;
    }
    if (!WRITE) counts[g] = cnt;
    }
}

template __global__ void quads<true>(const envp *P, const ddraw *draws, uint32_t draw0, const int32_t *mesh, const int32_t *orders, int W, int H, uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
template __global__ void quads<false>(const envp *P, const ddraw *draws, uint32_t draw0, const int32_t *mesh, const int32_t *orders, int W, int H, uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
