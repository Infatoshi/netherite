/* magma - CPU reference rasterizer (cpu/raster_cpu.c).
 *
 * Owner: RASTER agent. Implements cr_raster_cpu() plus the framebuffer helpers
 * from core/types.h. The CUDA backend (cuda/raster_cuda.cu) mirrors this inner
 * loop expression-for-expression so the two paths agree bit-for-bit.
 *
 * Pipeline per triangle:
 *   - backface cull by signed area (front winding = CR_FRONT_SIGN),
 *   - integer bounding box, pixel-center sampling,
 *   - top-left fill rule so shared edges are covered exactly once,
 *   - perspective-correct attribute recovery (interpolate invw and attr/w
 *     linearly, then attr = attr_w / invw),
 *   - z-test (keep if z < depth), then cr_shade; alpha 0 => discard (no write).
 */
#include "core/types.h"
#include <stdlib.h>
#include <math.h>

/* Front-facing winding. With this edge function and y-down pixel space, front
 * faces have signed area of this sign. Flip once during integration (to -1.0f)
 * if the mesher emits the opposite winding. */
static const float CR_FRONT_SIGN = -1.0f;

/* Signed area of triangle (a,b,p); >0 for the front winding. */
static inline float cr_edge(float ax, float ay, float bx, float by,
                            float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* Top-left classification of directed edge a->b. Exactly one of a->b and b->a
 * is top-left, so a shared edge is owned by a single triangle. */
static inline int cr_top_left(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

/* Deterministic log2f: exponent + a fixed-order minimax polynomial on the
 * mantissa in [1,2). No libm transcendental, only float mul/add + bit ops, so
 * the CPU and CUDA LOD are bit-identical (glibc/CUDA log2f would differ). */
static inline float cr_log2_det(float x) {
    union { float f; u32 i; } u;
    u.f = x;
    int e = (int)((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;   /* mantissa m in [1,2) */
    float m = u.f;
    float p = -1.7417939f + (2.8212026f + (-1.4699568f +
              (0.4479489f - 0.0563525f * m) * m) * m) * m;
    return (float)e + p;
}

/* Per-triangle constant LOD (log2 texels-per-pixel), from the affine texel-area
 * vs screen-area ratio: lod = 0.5*log2(texelArea/pixelArea). `area2` is the 2x
 * signed screen area (cr_edge of the 3 verts). Uniform minification (the mipped
 * test quad) makes this constant-per-triangle exact. Bit-identical on CPU/CUDA. */
static inline float cr_tri_lod(const CrScreenVert *v0, const CrScreenVert *v1,
                               const CrScreenVert *v2, const CrTexture *tex,
                               float area2) {
    if (!tex || tex->w <= 0 || tex->h <= 0) return 0.0f;
    float iw0 = 1.0f / v0->invw, iw1 = 1.0f / v1->invw, iw2 = 1.0f / v2->invw;
    float u0 = v0->uv_w.x * iw0, uv0 = v0->uv_w.y * iw0;
    float u1 = v1->uv_w.x * iw1, uv1 = v1->uv_w.y * iw1;
    float u2 = v2->uv_w.x * iw2, uv2 = v2->uv_w.y * iw2;
    float tw = (float)tex->w, th = (float)tex->h;
    float ex1 = (u1 - u0) * tw, ey1 = (uv1 - uv0) * th;
    float ex2 = (u2 - u0) * tw, ey2 = (uv2 - uv0) * th;
    float texArea2 = fabsf(ex1 * ey2 - ex2 * ey1);   /* 2x texel-space area */
    float pixArea2 = fabsf(area2);                    /* 2x screen area */
    if (texArea2 <= 0.0f || pixArea2 <= 0.0f) return 0.0f;
    return 0.5f * cr_log2_det(texArea2 / pixArea2);
}

void cr_fb_alloc(CrFramebuffer *fb, int w, int h) {
    fb->w = w;
    fb->h = h;
    fb->color = (CrRgba *)malloc((size_t)w * (size_t)h * sizeof(CrRgba));
    fb->depth = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
}

void cr_fb_free(CrFramebuffer *fb) {
    free(fb->color);
    free(fb->depth);
    fb->color = NULL;
    fb->depth = NULL;
    fb->w = 0;
    fb->h = 0;
}

void cr_fb_clear(CrFramebuffer *fb, CrRgba color) {
    int n = fb->w * fb->h;
    for (int i = 0; i < n; i++) {
        fb->color[i] = color;
        fb->depth[i] = 1.0f;
    }
}

void cr_raster_cpu(CrFramebuffer *fb, const CrScreenTri *tris, int ntris,
                   const CrShadeCtx *sh) {
    int W = fb->w, H = fb->h;

    for (int t = 0; t < ntris; t++) {
        const CrScreenVert *v0 = &tris[t].v[0];
        const CrScreenVert *v1 = &tris[t].v[1];
        const CrScreenVert *v2 = &tris[t].v[2];

        float x0 = v0->spos.x, y0 = v0->spos.y;
        float x1 = v1->spos.x, y1 = v1->spos.y;
        float x2 = v2->spos.x, y2 = v2->spos.y;

        float area = cr_edge(x0, y0, x1, y1, x2, y2);
        if (area * CR_FRONT_SIGN <= 0.0f) continue; /* backface / degenerate */

        /* per-triangle LOD (constant across the tri); 0 when mips are off. */
        float tri_lod = sh->use_mips ? cr_tri_lod(v0, v1, v2, sh->atlas, area) : 0.0f;

        float fminx = fminf(x0, fminf(x1, x2));
        float fmaxx = fmaxf(x0, fmaxf(x1, x2));
        float fminy = fminf(y0, fminf(y1, y2));
        float fmaxy = fmaxf(y0, fmaxf(y1, y2));
        int minx = (int)floorf(fminx); if (minx < 0) minx = 0;
        int maxx = (int)ceilf(fmaxx);  if (maxx > W) maxx = W;
        int miny = (int)floorf(fminy); if (miny < 0) miny = 0;
        int maxy = (int)ceilf(fmaxy);  if (maxy > H) maxy = H;

        int tl0 = cr_top_left(x1, y1, x2, y2);
        int tl1 = cr_top_left(x2, y2, x0, y0);
        int tl2 = cr_top_left(x0, y0, x1, y1);

        for (int py = miny; py < maxy; py++) {
            for (int px = minx; px < maxx; px++) {
                float fx = (float)px + 0.5f;
                float fy = (float)py + 0.5f;

                float w0 = cr_edge(x1, y1, x2, y2, fx, fy);
                float w1 = cr_edge(x2, y2, x0, y0, fx, fy);
                float w2 = cr_edge(x0, y0, x1, y1, fx, fy);

                float b0 = w0 / area;
                float b1 = w1 / area;
                float b2 = w2 / area;

                /* Coverage: interior (b>0) + top-left exact edge. Optional
                 * cover_eps is PIXEL-space outward slack: a sample < cover_eps
                 * px outside an edge still counts. Slack hits must be strictly
                 * closer in z (not LEQUAL) so coplanar sibling faces cannot
                 * overwrite a strict interior hit (pose2 arm top vs side). */
                float cov_eps = sh->cover_eps;
                int s0 = (b0 > 0.0f) || (b0 == 0.0f && tl0);
                int s1 = (b1 > 0.0f) || (b1 == 0.0f && tl1);
                int s2 = (b2 > 0.0f) || (b2 == 0.0f && tl2);
                int strict_in = s0 && s1 && s2;
                int in0 = s0, in1 = s1, in2 = s2;
                if (!strict_in && cov_eps > 0.0f) {
                    float el0 = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
                    float el1 = sqrtf((x0 - x2) * (x0 - x2) + (y0 - y2) * (y0 - y2));
                    float el2 = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
                    if (!in0 && el0 > 1e-12f && w0 * area < 0.0f
                        && fabsf(w0) / el0 <= cov_eps) in0 = 1;
                    if (!in1 && el1 > 1e-12f && w1 * area < 0.0f
                        && fabsf(w1) / el1 <= cov_eps) in1 = 1;
                    if (!in2 && el2 > 1e-12f && w2 * area < 0.0f
                        && fabsf(w2) / el2 <= cov_eps) in2 = 1;
                }
                if (!(in0 && in1 && in2)) continue;
                int slack_hit = !strict_in;

                float invw = b0 * v0->invw + b1 * v1->invw + b2 * v2->invw;
                float z = b0 * v0->spos.z + b1 * v1->spos.z + b2 * v2->spos.z;

                int idx = py * W + px;
                /* Depth: strict coverage uses LEQUAL when enabled. Slack hits
                 * require a material depth improvement (>1e-5) so a coplanar
                 * sibling face a few ULP closer cannot overwrite a strict
                 * interior sample (pose2 arm top vs side), while hat-over-head
                 * (~1e-3) and empty-hole fills still pass. */
                int depth_ok;
                if (slack_hit)
                    depth_ok = (z + 1.0e-5f < fb->depth[idx]);
                else
                    depth_ok = (z < fb->depth[idx]) ||
                        (sh->depth_lequal && z == fb->depth[idx]);
                if (!depth_ok) continue;

                float iw = 1.0f / invw;

                CrFragment frag;
                frag.uv.x = (b0 * v0->uv_w.x + b1 * v1->uv_w.x + b2 * v2->uv_w.x) * iw;
                frag.uv.y = (b0 * v0->uv_w.y + b1 * v1->uv_w.y + b2 * v2->uv_w.y) * iw;
                frag.light = (b0 * v0->light_w + b1 * v1->light_w + b2 * v2->light_w) * iw;
                frag.ao = (b0 * v0->ao_w + b1 * v1->ao_w + b2 * v2->ao_w) * iw;
                frag.blk = (b0 * v0->blk_w + b1 * v1->blk_w + b2 * v2->blk_w) * iw;
                /* Smooth tint (GL_SMOOTH / shadeModel 7425). */
                {
                    float tr = (b0 * v0->tint_r_w + b1 * v1->tint_r_w + b2 * v2->tint_r_w) * iw;
                    float tg = (b0 * v0->tint_g_w + b1 * v1->tint_g_w + b2 * v2->tint_g_w) * iw;
                    float tb = (b0 * v0->tint_b_w + b1 * v1->tint_b_w + b2 * v2->tint_b_w) * iw;
                    float ta = (b0 * v0->tint_a_w + b1 * v1->tint_a_w + b2 * v2->tint_a_w) * iw;
                    frag.tint.r = (u8)(fminf(255.0f, fmaxf(0.0f, tr)) + 0.5f);
                    frag.tint.g = (u8)(fminf(255.0f, fmaxf(0.0f, tg)) + 0.5f);
                    frag.tint.b = (u8)(fminf(255.0f, fmaxf(0.0f, tb)) + 0.5f);
                    frag.tint.a = (u8)(fminf(255.0f, fmaxf(0.0f, ta)) + 0.5f);
                }
                frag.eye_dist = (b0 * v0->eye_dist_w + b1 * v1->eye_dist_w
                               + b2 * v2->eye_dist_w) * iw;
                frag.lod = tri_lod;

                CrRgba c = cr_shade(sh, &frag);
                if (c.a == 0) continue; /* alpha_test discard: no color/depth write */

                if (sh->blend == 1 || sh->blend == 4) {
                    /* src-over into dst. blend=1: translucent water (no depth).
                     * blend=4: LayerSlimeGel (depth write; vanilla depthMask true). */
                    CrRgba d = fb->color[idx];
                    float a = c.a * (1.0f / 255.0f);
                    float ia = 1.0f - a;
                    fb->color[idx].r = (u8)(fminf(255.0f, fmaxf(0.0f, c.r * a + d.r * ia)) + 0.5f);
                    fb->color[idx].g = (u8)(fminf(255.0f, fmaxf(0.0f, c.g * a + d.g * ia)) + 0.5f);
                    fb->color[idx].b = (u8)(fminf(255.0f, fmaxf(0.0f, c.b * a + d.b * ia)) + 0.5f);
                    fb->color[idx].a = 255;
                    if (sh->blend == 4) fb->depth[idx] = z;
                } else if (sh->blend == 2) {
                    /* GL blendFunc(DST_COLOR, SRC_COLOR): out = 2*src*dst.
                     * Dig crack (RenderGlobal.preRenderDamagedBlocks). No depth. */
                    CrRgba d = fb->color[idx];
                    fb->color[idx].r = (u8)(fminf(255.0f, (2.0f * c.r * d.r) * (1.0f / 255.0f)) + 0.5f);
                    fb->color[idx].g = (u8)(fminf(255.0f, (2.0f * c.g * d.g) * (1.0f / 255.0f)) + 0.5f);
                    fb->color[idx].b = (u8)(fminf(255.0f, (2.0f * c.b * d.b) * (1.0f / 255.0f)) + 0.5f);
                    fb->color[idx].a = 255;
                } else if (sh->blend == 3) {
                    /* GL blendFunc(SRC_ALPHA, ONE): out = src*src.a + dst.
                     * LayerEnderDragonDeath. No depth write. */
                    CrRgba d = fb->color[idx];
                    float a = c.a * (1.0f / 255.0f);
                    fb->color[idx].r = (u8)(fminf(255.0f, c.r * a + (float)d.r) + 0.5f);
                    fb->color[idx].g = (u8)(fminf(255.0f, c.g * a + (float)d.g) + 0.5f);
                    fb->color[idx].b = (u8)(fminf(255.0f, c.b * a + (float)d.b) + 0.5f);
                    fb->color[idx].a = 255;
                } else {
                    fb->color[idx] = c;
                    fb->depth[idx] = z;
                }
            }
        }
    }
}
