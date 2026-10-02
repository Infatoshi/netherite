/* The recorded entity quads' lean kernel (render.h, lane/renderbuild's
 * split of the geometry kernels): raster_entities.c draw_quad's triangles
 * that clip_and_draw keeps whole or drops whole, the rest handed to the
 * generic kernel (geom_gen.cu).
 * Every function below that computes a float is a port of the raster.c,
 * raster_sky2.c, raster_entities.c, raster_overlay.c or raster_tileent.c
 * function it names, operation for operation, so the build's --fmad=false,
 * -prec-div=true and -prec-sqrt=true give the C engine's bits
 * (csrc/cuda/render/mirrors.txt lists them for the mirror rule: a change
 * to one of those C functions needs its port here). */
#include "geom.cuh"

/* The recorded entity quads, lean: clip_and_draw keeps a triangle as it is
 * when all three vertices are inside every plane, and keeps nothing when all
 * are outside the first plane not all are inside (classify), so those take
 * the screen transform and the setup straight away; a quad with a triangle
 * that straddles a plane goes to the generic kernel's clipper. */
template <bool WRITE>
__global__ void __launch_bounds__(128)
equads(const envp *P, int n, const xunit *list, uint32_t nlist, uint32_t u0, uint32_t u1, recs R, int W, int H,
         uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base,
         tri *out, uint32_t *tiles)
{
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nlist) return;
    xunit x = list[i];
    if (x.g < u0 || x.g >= u1) return;
    /* the write: a quad the count found no triangle in writes none */
    if (WRITE && offsets[x.g + 1] == offsets[x.g]) return;
    const cquad &q = R.equad[x.a];
    int ka = classify(q.v[0].clip, q.v[1].clip, q.v[3].clip), kb = classify(q.v[1].clip, q.v[2].clip, q.v[3].clip);
    if (ka == 2 || kb == 2) {
        if (!WRITE) clip_list[atomicAdd(nclip, 1u)] = x;
        return;
    }
    const envp &E = P[env_of(P, n, x.g)];
    uint32_t sg = E.estate_base + (uint32_t)q.state;
    const raster_obs_estate &st = R.estate[sg];
    const int tv[2][3] = {{0, 1, 3}, {1, 2, 3}};
    if (WRITE) {
        twrite_vec s{{0, out, tiles, offsets[x.g] - tri_base, (E.prec & RP_RCP) != 0, E.prec == RP_FAST}};
        for (int t = 0; t < 2; ++t) {
            if ((t == 0 ? ka : kb) != 0) continue;
            evtx v[3];
            for (int k = 0; k < 3; ++k) {
                cq_vertex(q, R.eextra, tv[t][k], v[k]);
                e_screen(W, H, v[k]);
            }
            e_tri(v, st, sg, E.tex_base + (uint32_t)q.tex, W, H, s);
        }
    } else {
        /* the count: the clip coordinates and the screen position alone */
        tcount s{0};
        for (int t = 0; t < 2; ++t) {
            if ((t == 0 ? ka : kb) != 0) continue;
            cvx v[3];
            for (int k = 0; k < 3; ++k) {
                cq_vertex(q, tv[t][k], v[k]);
                screen_vertex(W, H, v[k]);
            }
            e_tri(v, st, sg, 0, W, H, s);
        }
        counts[x.g] = s.n;
    }
}

template __global__ void equads<true>(const envp *P, int n, const xunit *list, uint32_t nlist, uint32_t u0, uint32_t u1, recs R, int W, int H, uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
template __global__ void equads<false>(const envp *P, int n, const xunit *list, uint32_t nlist, uint32_t u0, uint32_t u1, recs R, int W, int H, uint32_t *counts, xunit *clip_list, uint32_t *nclip, const uint32_t *offsets, uint32_t tri_base, tri *out, uint32_t *tiles);
