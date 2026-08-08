/* magma - CUDA rasterizer (cuda/raster_cuda.cu).
 *
 * Owner: RASTER agent. Implements cr_raster_cuda() with output identical to
 * cr_raster_cpu(). One thread per pixel over each triangle's bounding box; one
 * kernel launch per triangle on the default stream, which serializes triangles
 * in the same order as the CPU loop, so overlapping-triangle depth resolution
 * matches without any atomics.
 *
 * Linkage note: core/shade.c is #included here under private names
 * (cr_shade_dev / cr_atlas_sample_dev) so this translation unit does NOT export
 * a second host `cr_shade` / `cr_atlas_sample` that would collide with the
 * gcc-built core/shade.o at link time. The CPU path uses the shade.o symbols
 * (compiled with -ffp-contract=off); device code here uses the *_dev copies
 * (compiled with --fmad=false). Same source + matching flags => bit-identical.
 */
#include "core/types.h"
#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define cr_shade        cr_shade_dev
#define cr_atlas_sample cr_atlas_sample_dev
#include "core/shade.c"
#undef cr_shade
#undef cr_atlas_sample

/* Same private-name trick for the transform stage: core/math.c and
 * transform.c are compiled into this TU (device + host copies under _dev
 * names, no host-symbol collision with the gcc-built math.o/transform.o).
 * Same source + --fmad=false == the CPU's -ffp-contract=off => the GPU
 * transform kernel is bit-identical to the CPU cr_transform loop. */
#define cr_mat4_identity    cr_mat4_identity_dev
#define cr_mat4_mul         cr_mat4_mul_dev
#define cr_mat4_mul_vec4    cr_mat4_mul_vec4_dev
#define cr_perspective      cr_perspective_dev
#define cr_look_yaw_pitch   cr_look_yaw_pitch_dev
#define cr_camera_view      cr_camera_view_dev
#include "core/math.c"
#define cr_transform        cr_transform_dev
#define cr_transform_tri    cr_transform_tri_dev
#include "transform.c"
#undef cr_transform
#undef cr_transform_tri
#undef cr_mat4_identity
#undef cr_mat4_mul
#undef cr_mat4_mul_vec4
#undef cr_perspective
#undef cr_look_yaw_pitch
#undef cr_camera_view

/* Private copy of the sky shader (game/sky.c) for the GPU sky pass. The frame
 * ctx (all celestial trig) is built on the HOST by the cc-compiled sky.o
 * (gm_sky_frame_args: glibc libm, -ffp-contract=off) and passed in; the
 * per-ray shader below is IEEE-exact arithmetic on the device (--fmad=false)
 * EXCEPT hash21's sinf (night stars): measured <=0.012% of pixels on night
 * frames, isolated single-star dots, day frames bit-identical (sky_ab A/B).
 * The atlas arrays are host static const in sky_atlas.h; remap them to
 * __managed__ mirrors so the device shader can sample them too. */
#include <string.h>
#include "assets/sky_atlas.h"
static const unsigned char *cr_sky_sun_host  = CR_SUN_RGBA;
static const unsigned char *cr_sky_moon_host = CR_MOON_RGBA;
static const unsigned char *cr_sky_end_host  = CR_END_SKY_RGBA;
static __managed__ unsigned char cr_sky_sun_m[4096];
static __managed__ unsigned char cr_sky_moon_m[4096];
static __managed__ unsigned char cr_sky_end_m[65536];
#define CR_SUN_RGBA cr_sky_sun_m
#define CR_MOON_RGBA cr_sky_moon_m
#define CR_END_SKY_RGBA cr_sky_end_m
#define gm_sky_draw            cr_priv_gm_sky_draw
#define gm_sky_frame_args      cr_priv_gm_sky_frame_args
#define gm_end_sky_draw        cr_priv_gm_end_sky_draw
#define gm_end_sky_ray_color   cr_priv_gm_end_sky_ray_color
#define gm_sky_ray_color       cr_priv_gm_sky_ray_color
#define gm_terrain_fog_color   cr_priv_gm_terrain_fog_color
#define gm_terrain_fog_enabled cr_priv_gm_terrain_fog_enabled
#define gm_sky_set_fluid_fog   cr_priv_gm_sky_set_fluid_fog
#define gm_sky_set_fog_c1      cr_priv_gm_sky_set_fog_c1
#define gm_sky_set_night_vision cr_priv_gm_sky_set_night_vision
#define gm_sky_set_void_blindness cr_priv_gm_sky_set_void_blindness
#define gm_sky_set_eye_height  cr_priv_gm_sky_set_eye_height
#define gm_sky_set_weather     cr_priv_gm_sky_set_weather
#define gm_weather_rain_texture cr_priv_gm_weather_rain_texture
#define gm_weather_snow_texture cr_priv_gm_weather_snow_texture
#include "game/sky.c"
#undef gm_sky_draw
#undef gm_sky_frame_args
#undef gm_end_sky_draw
#undef gm_end_sky_ray_color
#undef gm_sky_ray_color
#undef gm_terrain_fog_color
#undef gm_terrain_fog_enabled
#undef gm_sky_set_fluid_fog
#undef gm_sky_set_fog_c1
#undef gm_sky_set_night_vision
#undef gm_sky_set_void_blindness
#undef gm_sky_set_eye_height
#undef gm_sky_set_weather
#undef gm_weather_rain_texture
#undef gm_weather_snow_texture
#undef CR_SUN_RGBA
#undef CR_MOON_RGBA
#undef CR_END_SKY_RGBA

/* Must match cpu/raster_cpu.c exactly. */
#define CR_FRONT_SIGN -1.0f

__host__ __device__ static inline float cr_edge(float ax, float ay,
                                                float bx, float by,
                                                float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

__host__ __device__ static inline int cr_top_left(float ax, float ay,
                                                  float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

/* Deterministic log2f (see cpu/raster_cpu.c): exponent + fixed-order polynomial,
 * no transcendental, so CPU and CUDA LOD are bit-identical. */
__host__ __device__ static inline float cr_log2_det(float x) {
    union { float f; u32 i; } u;
    u.f = x;
    int e = (int)((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f;
    float p = -1.7417939f + (2.8212026f + (-1.4699568f +
              (0.4479489f - 0.0563525f * m) * m) * m) * m;
    return (float)e + p;
}

/* Per-triangle constant LOD; must match cpu/raster_cpu.c expression-for-expression. */
__host__ __device__ static inline float cr_tri_lod(const CrScreenVert *v0,
                                                   const CrScreenVert *v1,
                                                   const CrScreenVert *v2,
                                                   const CrTexture *tex,
                                                   float area2) {
    if (!tex || tex->w <= 0 || tex->h <= 0) return 0.0f;
    float iw0 = 1.0f / v0->invw, iw1 = 1.0f / v1->invw, iw2 = 1.0f / v2->invw;
    float u0 = v0->uv_w.x * iw0, uv0 = v0->uv_w.y * iw0;
    float u1 = v1->uv_w.x * iw1, uv1 = v1->uv_w.y * iw1;
    float u2 = v2->uv_w.x * iw2, uv2 = v2->uv_w.y * iw2;
    float tw = (float)tex->w, th = (float)tex->h;
    float ex1 = (u1 - u0) * tw, ey1 = (uv1 - uv0) * th;
    float ex2 = (u2 - u0) * tw, ey2 = (uv2 - uv0) * th;
    float texArea2 = fabsf(ex1 * ey2 - ex2 * ey1);
    float pixArea2 = fabsf(area2);
    if (texArea2 <= 0.0f || pixArea2 <= 0.0f) return 0.0f;
    return 0.5f * cr_log2_det(texArea2 / pixArea2);
}

/* One thread per pixel of a triangle's clamped bounding box [minx,minx+bw) x
 * [miny,miny+bh). Body is expression-identical to cr_raster_cpu's inner loop. */
__global__ void cr_raster_tri_kernel(CrRgba *color, float *depth, int W, int H,
                                     CrScreenTri tri, const CrShadeCtx *sh,
                                     int minx, int miny, int bw, int bh) {
    int lx = blockIdx.x * blockDim.x + threadIdx.x;
    int ly = blockIdx.y * blockDim.y + threadIdx.y;
    if (lx >= bw || ly >= bh) return;
    int px = minx + lx;
    int py = miny + ly;

    const CrScreenVert *v0 = &tri.v[0];
    const CrScreenVert *v1 = &tri.v[1];
    const CrScreenVert *v2 = &tri.v[2];

    float x0 = v0->spos.x, y0 = v0->spos.y;
    float x1 = v1->spos.x, y1 = v1->spos.y;
    float x2 = v2->spos.x, y2 = v2->spos.y;

    float area = cr_edge(x0, y0, x1, y1, x2, y2);
    /* culled on host; guard anyway for exact parity of the sign test */
    if (area * CR_FRONT_SIGN <= 0.0f) return;

    int tl0 = cr_top_left(x1, y1, x2, y2);
    int tl1 = cr_top_left(x2, y2, x0, y0);
    int tl2 = cr_top_left(x0, y0, x1, y1);

    float fx = (float)px + 0.5f;
    float fy = (float)py + 0.5f;

    float w0 = cr_edge(x1, y1, x2, y2, fx, fy);
    float w1 = cr_edge(x2, y2, x0, y0, fx, fy);
    float w2 = cr_edge(x0, y0, x1, y1, fx, fy);

    float b0 = w0 / area;
    float b1 = w1 / area;
    float b2 = w2 / area;

    int in0 = (b0 > 0.0f) || (b0 == 0.0f && tl0);
    int in1 = (b1 > 0.0f) || (b1 == 0.0f && tl1);
    int in2 = (b2 > 0.0f) || (b2 == 0.0f && tl2);
    if (!(in0 && in1 && in2)) return;

    float invw = b0 * v0->invw + b1 * v1->invw + b2 * v2->invw;
    float z = b0 * v0->spos.z + b1 * v1->spos.z + b2 * v2->spos.z;

    int idx = py * W + px;
    /* GL_LEQUAL opt-in (see cpu/raster_cpu.c): coplanar overlay quads. */
    if (!(z < depth[idx] || (sh->depth_lequal && z == depth[idx]))) return;

    float iw = 1.0f / invw;

    CrFragment frag;
    frag.uv.x = (b0 * v0->uv_w.x + b1 * v1->uv_w.x + b2 * v2->uv_w.x) * iw;
    frag.uv.y = (b0 * v0->uv_w.y + b1 * v1->uv_w.y + b2 * v2->uv_w.y) * iw;
    frag.light = (b0 * v0->light_w + b1 * v1->light_w + b2 * v2->light_w) * iw;
    frag.ao = (b0 * v0->ao_w + b1 * v1->ao_w + b2 * v2->ao_w) * iw;
    frag.blk = (b0 * v0->blk_w + b1 * v1->blk_w + b2 * v2->blk_w) * iw;
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
    /* per-triangle constant LOD (pure fn of tri+atlas => same value every pixel). */
    frag.lod = sh->use_mips ? cr_tri_lod(v0, v1, v2, sh->atlas, area) : 0.0f;

    CrRgba c = cr_shade_dev(sh, &frag);
    if (c.a == 0) return;   /* alpha_test discard: no color/depth write */

    if (sh->blend == 1 || sh->blend == 4) {
        /* src-over. blend=1 no depth; blend=4 LayerSlimeGel depth write. */
        CrRgba d = color[idx];
        float a = c.a * (1.0f / 255.0f);
        float ia = 1.0f - a;
        color[idx].r = (u8)(fminf(255.0f, fmaxf(0.0f, c.r * a + d.r * ia)) + 0.5f);
        color[idx].g = (u8)(fminf(255.0f, fmaxf(0.0f, c.g * a + d.g * ia)) + 0.5f);
        color[idx].b = (u8)(fminf(255.0f, fmaxf(0.0f, c.b * a + d.b * ia)) + 0.5f);
        color[idx].a = 255;
        if (sh->blend == 4) depth[idx] = z;
    } else if (sh->blend == 2) {
        /* GL blendFunc(DST_COLOR, SRC_COLOR): out = 2*src*dst. Dig crack. */
        CrRgba d = color[idx];
        color[idx].r = (u8)(fminf(255.0f, (2.0f * c.r * d.r) * (1.0f / 255.0f)) + 0.5f);
        color[idx].g = (u8)(fminf(255.0f, (2.0f * c.g * d.g) * (1.0f / 255.0f)) + 0.5f);
        color[idx].b = (u8)(fminf(255.0f, (2.0f * c.b * d.b) * (1.0f / 255.0f)) + 0.5f);
        color[idx].a = 255;
    } else if (sh->blend == 3) {
        CrRgba d = color[idx];
        float a = c.a * (1.0f / 255.0f);
        color[idx].r = (u8)(fminf(255.0f, c.r * a + (float)d.r) + 0.5f);
        color[idx].g = (u8)(fminf(255.0f, c.g * a + (float)d.g) + 0.5f);
        color[idx].b = (u8)(fminf(255.0f, c.b * a + (float)d.b) + 0.5f);
        color[idx].a = 255;
    } else {
        color[idx] = c;
        depth[idx] = z;
    }
}

static void cr_cuda_check(const char *what) {
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) {
        fprintf(stderr, "magma CUDA error (%s): %s\n", what,
                cudaGetErrorString(e));
    }
}

/* ============================================================================
 * ALLOCATE-ONCE game entrypoints (raster_cuda.cu, game wiring).
 *
 * cr_raster_cuda() above is the VERIFIED per-call parity entry (rung-1): it
 * allocates + frees every device buffer per call and launches one kernel PER
 * TRIANGLE (serialized on the default stream so overlapping-tri depth resolves
 * in CPU order). That is correct but (a) allocs every call and (b) does ~1
 * launch per triangle -> hundreds of thousands of launches per game frame. It
 * MUST NOT change (the parity test depends on it).
 *
 * For the game loop we add an alloc-once, single-launch-per-layer variant:
 *   cr_raster_cuda_pre(w,h,max_tris)  - cudaMalloc the device framebuffer, the
 *                                       tri buffer, and the shade-ctx ONCE.
 *   cr_raster_cuda_into(fb,tris,n,sh) - reuse them every frame: memcpy fb+tris
 *                                       up, launch ONE kernel over the frame,
 *                                       copy color/depth back. No malloc.
 *   cr_raster_cuda_post()             - free.
 *
 * The frame kernel puts ONE THREAD PER PIXEL and loops over all triangles IN
 * ORDER inside the thread, executing the exact same coverage/z-test/shade/blend
 * body as cpu/raster_cpu.c's inner loop, in the same triangle order, with the
 * same shade.c (compiled --fmad=false). Each pixel is owned by a single thread,
 * so there are no races and no atomics; the per-pixel sequence of triangle
 * updates is identical to the CPU, hence BIT-IDENTICAL output. This trades the
 * per-triangle launches for one launch per layer, which is what makes it fast.
 * ==========================================================================*/

/* Per-triangle *tile* bounding box, packed into 4 bytes so a whole layer's box
 * array stays L2-resident (the tiled kernel re-reads it once per tile per
 * triangle -> O(tiles*tris); at 16 bytes/tri that thrashes DRAM). Packed as
 * (tminx<<24)|(tminy<<16)|(tmaxx<<8)|tmaxy, inclusive tile indices. The SKIP
 * sentinel 0xFF00xxxx (tminx=255 > any real tile) never overlaps -> identical
 * to the CPU's `continue` for backface / degenerate / off-screen triangles.
 * Tile indices are pixel_bbox>>4; at <=4096px wide (256 tiles) they fit in a
 * byte with margin for our 854/1920 targets. */
typedef u32 CrTriBox;
#define CR_TBOX_SKIP 0xFF000000u   /* tminx=255, never matches a real tile */

__device__ __forceinline__ CrTriBox cr_tbox_pack(int tminx, int tminy,
                                                 int tmaxx, int tmaxy) {
    return ((u32)(tminx & 0xFF) << 24) | ((u32)(tminy & 0xFF) << 16) |
           ((u32)(tmaxx & 0xFF) << 8)  |  (u32)(tmaxy & 0xFF);
}

/* Tile geometry, needed by BOTH raster kernels: the tiled kernel streams
 * triangles in CR_TILE_N-sized batches, and the bbox kernel is launched with
 * exactly CR_TILE_N threads per block, so bbox block b covers tiled batch b. */
#define CR_TILE 16
#define CR_TILE_N (CR_TILE * CR_TILE)   /* 256 threads / triangles per batch */
#define CR_BBOX_THREADS CR_TILE_N       /* launch config: must equal CR_TILE_N */

/* One thread per triangle: compute the same clamped pixel bbox the CPU inner
 * loop uses, reduce it to inclusive tile bounds, and pre-mark backface/
 * degenerate/off-screen as skip. Also reduces the block's min tminz into
 * batchz[blockIdx.x] for the tiled kernel's batch-level hi-z reject. */
__global__ void cr_raster_bbox_kernel(const CrScreenTri *tris, int ntris,
                                      int W, int H, CrTriBox *box,
                                      float *tminz, float *batchz,
                                      const int *screen_tris,
                                      int use_device_count) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    int raster_ntris = use_device_count ? screen_tris[1] : ntris;
    int live = (t < raster_ntris);

    /* hi-z reject input: interpolated z is barycentric over the vert z's, so
     * min vert z lower-bounds the tri's depth anywhere on screen. Written for
     * every live slot, including ones this kernel marks SKIP (a skipped tri's
     * z only pulls the batch bound DOWN, i.e. stays conservative). */
    float mz = 3.402823466e38f;
    if (live) {
        mz = fminf(tris[t].v[0].spos.z,
                   fminf(tris[t].v[1].spos.z, tris[t].v[2].spos.z));
        tminz[t] = mz;
    }
    /* BATCH HI-Z: min tminz over this block's CR_TILE_N triangles. Dead lanes
     * (t >= raster_ntris) feed +inf so a partial tail batch bounds only its
     * live triangles. Tree reduction over shared memory: no atomics, so the
     * value is bit-deterministic run to run. Every lane must reach these
     * barriers, hence no early return above. */
    __shared__ float sbz[CR_BBOX_THREADS];
    int blane = threadIdx.x;
    sbz[blane] = mz;
    __syncthreads();
    for (int off = CR_BBOX_THREADS / 2; off > 0; off >>= 1) {
        if (blane < off) sbz[blane] = fminf(sbz[blane], sbz[blane + off]);
        __syncthreads();
    }
    if (blane == 0) batchz[blockIdx.x] = sbz[0];

    if (!live) return;
    const CrScreenVert *v0 = &tris[t].v[0];
    const CrScreenVert *v1 = &tris[t].v[1];
    const CrScreenVert *v2 = &tris[t].v[2];
    float x0 = v0->spos.x, y0 = v0->spos.y;
    float x1 = v1->spos.x, y1 = v1->spos.y;
    float x2 = v2->spos.x, y2 = v2->spos.y;

    float area = cr_edge(x0, y0, x1, y1, x2, y2);
    if (area * CR_FRONT_SIGN <= 0.0f) { box[t] = CR_TBOX_SKIP; return; }

    float fminx = fminf(x0, fminf(x1, x2));
    float fmaxx = fmaxf(x0, fmaxf(x1, x2));
    float fminy = fminf(y0, fminf(y1, y2));
    float fmaxy = fmaxf(y0, fmaxf(y1, y2));
    int minx = (int)floorf(fminx); if (minx < 0) minx = 0;
    int maxx = (int)ceilf(fmaxx);  if (maxx > W) maxx = W;
    int miny = (int)floorf(fminy); if (miny < 0) miny = 0;
    int maxy = (int)ceilf(fmaxy);  if (maxy > H) maxy = H;
    if (minx >= maxx || miny >= maxy) { box[t] = CR_TBOX_SKIP; return; }
    /* inclusive tile range covering pixels [minx,maxx) x [miny,maxy). */
    box[t] = cr_tbox_pack(minx >> 4, miny >> 4, (maxx - 1) >> 4, (maxy - 1) >> 4);
}

/* Tiled rasterizer: a 16x16 block owns one screen tile; each thread owns one
 * pixel. Triangles are processed IN ASCENDING INDEX ORDER; per 256-batch the 256
 * threads test one triangle's packed tile-box each, an early-out skips batches
 * with no overlap, and a block prefix scan compacts the overlapping triangles
 * (order-preserving) so the per-pixel coverage/shade loop only touches triangles
 * that actually reach this tile. The per-pixel sequence of triangle updates is
 * thus the exact CPU order, so output is bit-identical to cr_raster_cpu; the
 * coverage/shade/blend body mirrors it expression-for-expression. Work is
 * proportional to real per-tile coverage (like the CPU), not W*H*N. */

/* sb1..sb3: tri-slot boundaries for merged multi-layer draws - tri slot t
 * shades with sh[(t>=sb1)+(t>=sb2)+(t>=sb3)]. Single-layer callers pass
 * 0x7fffffff for all three (select is always sh[0], same arithmetic). */
__global__ void cr_raster_tiled_kernel(CrRgba *color, float *depth, int W, int H,
                                       const CrScreenTri *tris, int ntris,
                                       const CrTriBox *box, const CrShadeCtx *sh,
                                       const float *tminz, const float *batchz,
                                       const int *screen_tris,
                                       int use_device_count,
                                       int sb1, int sb2, int sb3) {
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;

    int bx = blockIdx.x, by = blockIdx.y;   /* this block's tile index */

    int valid = (px < W) && (py < H);
    int idx = valid ? (py * W + px) : 0;

    float fx = (float)px + 0.5f;
    float fy = (float)py + 0.5f;

    /* running color/depth for this pixel (exclusive to this thread). */
    CrRgba cur; float curz;
    if (valid) { cur = color[idx]; curz = depth[idx]; }
    else       { cur.r = cur.g = cur.b = cur.a = 0; curz = 0.0f; }

    /* Per batch of 256 triangles: each of the 256 threads tile-tests exactly ONE
     * triangle (so the reject is done once per triangle per block, not 256x), then
     * a block-level exclusive prefix scan compacts the passing triangle indices
     * into `slist` IN ASCENDING ORDER. All threads then run coverage/shade only
     * over that compacted list -> work proportional to real per-tile coverage.
     * Ascending order is preserved (lane order -> scan position), so each pixel
     * still visits triangles in strict index order -> bit-identical to the CPU. */
    __shared__ int sflag[CR_TILE_N];   /* pass flag, then inclusive-scan buffer */
    __shared__ int slist[CR_TILE_N];   /* compacted, ascending triangle indices */
    __shared__ float sred[CR_TILE_N];  /* tile max-depth reduction scratch */
    int lane = threadIdx.y * CR_TILE + threadIdx.x;   /* 0..255 */

    int raster_ntris = use_device_count ? screen_tris[1] : ntris;
    int nbatches = (raster_ntris + CR_TILE_N - 1) / CR_TILE_N;

    /* HI-Z: the tile's worst (largest) pixel depth. A triangle whose min vert
     * z is >= this bound fails z<curz at EVERY pixel in the tile (opaque and
     * blend draws alike), so skipping it changes no output - the walk stays
     * exact, it just touches 4 bytes (tminz) instead of the 120-byte tri.
     * curz only ever shrinks, so a bound carried forward from an earlier point
     * in the stream is conservative (too large -> rejects less). It is
     * therefore refreshed only after a batch that actually ran the shade loop:
     * batches skipped by the batch/overlap tests cannot touch curz, so the
     * carried value is exactly what a per-batch recompute would produce. */
    sred[lane] = valid ? curz : -3.402823466e38f;
    __syncthreads();
    for (int off = CR_TILE_N / 2; off > 0; off >>= 1) {
        if (lane < off) sred[lane] = fmaxf(sred[lane], sred[lane + off]);
        __syncthreads();
    }
    float tile_maxz = sred[0];

    for (int batch = 0; batch < nbatches; ++batch) {
        /* BATCH HI-Z: batchz[batch] is the min tminz over the batch's 256
         * triangles (bbox kernel). If even the nearest of them is behind the
         * tile's worst depth, every one of them would be rejected by the
         * per-triangle test below, so the whole batch is skipped - no box
         * loads, no scan, no barrier. The branch is threadgroup-uniform
         * (batchz[batch] and tile_maxz are both uniform), so no lane is left
         * behind at a barrier. Strict > keeps it valid for LEQUAL contexts
         * too, whose per-tri test is the strict one. */
        if (batchz[batch] > tile_maxz) continue;
        int base = batch * CR_TILE_N;
        int batch_n = raster_ntris - base;
        if (batch_n > CR_TILE_N) batch_n = CR_TILE_N;
        int t0 = base + lane;
        int pass = 0;
        if (lane < batch_n) {
            CrTriBox b = box[t0];         /* 4-byte packed inclusive tile bounds */
            int tminx = (b >> 24) & 0xFF, tminy = (b >> 16) & 0xFF;
            int tmaxx = (b >> 8)  & 0xFF, tmaxy =  b        & 0xFF;
            pass = (tminx <= bx && bx <= tmaxx && tminy <= by && by <= tmaxy);
        }
        sflag[lane] = pass;
        /* Barrier + early-out: most 256-batches have NO triangle overlapping this
         * tile, so skip the sync-heavy scan entirely for them. __syncthreads_or
         * is one barrier (also publishes the sflag writes above). */
        if (!__syncthreads_or(pass)) continue;

        /* Hillis-Steele inclusive scan over the 256 pass flags. */
        for (int off = 1; off < CR_TILE_N; off <<= 1) {
            int v = (lane >= off) ? sflag[lane - off] : 0;
            __syncthreads();
            sflag[lane] += v;
            __syncthreads();
        }
        int total = sflag[CR_TILE_N - 1];
        int excl  = sflag[lane] - pass;      /* exclusive prefix = my slot */
        if (pass) slist[excl] = t0;
        __syncthreads();   /* publish slist before the coverage walk reads it */

        for (int k = 0; k < total; k++) {
            if (!valid) break;               /* whole tile off-screen: nothing to do */
            int t = slist[k];
            const CrShadeCtx *shl = sh + ((t >= sb1) + (t >= sb2) + (t >= sb3));
            /* hi-z reject: with LEQUAL an equal-depth tri still draws, so the
             * bound must be strict-greater for those ctxs. */
            if (shl->depth_lequal ? (tminz[t] > tile_maxz)
                                  : (tminz[t] >= tile_maxz)) continue;
            const CrScreenVert *v0 = &tris[t].v[0];
            const CrScreenVert *v1 = &tris[t].v[1];
            const CrScreenVert *v2 = &tris[t].v[2];

        float x0 = v0->spos.x, y0 = v0->spos.y;
        float x1 = v1->spos.x, y1 = v1->spos.y;
        float x2 = v2->spos.x, y2 = v2->spos.y;

        float area = cr_edge(x0, y0, x1, y1, x2, y2);
        /* precomputed skip means this is always front-facing; keep for parity. */
        if (area * CR_FRONT_SIGN <= 0.0f) continue;

        float tri_lod = shl->use_mips ? cr_tri_lod(v0, v1, v2, shl->atlas, area) : 0.0f;

        int tl0 = cr_top_left(x1, y1, x2, y2);
        int tl1 = cr_top_left(x2, y2, x0, y0);
        int tl2 = cr_top_left(x0, y0, x1, y1);

        float w0 = cr_edge(x1, y1, x2, y2, fx, fy);
        float w1 = cr_edge(x2, y2, x0, y0, fx, fy);
        float w2 = cr_edge(x0, y0, x1, y1, fx, fy);

        float b0 = w0 / area;
        float b1 = w1 / area;
        float b2 = w2 / area;

        int in0 = (b0 > 0.0f) || (b0 == 0.0f && tl0);
        int in1 = (b1 > 0.0f) || (b1 == 0.0f && tl1);
        int in2 = (b2 > 0.0f) || (b2 == 0.0f && tl2);
        if (!(in0 && in1 && in2)) continue;

        float invw = b0 * v0->invw + b1 * v1->invw + b2 * v2->invw;
        float z = b0 * v0->spos.z + b1 * v1->spos.z + b2 * v2->spos.z;

        if (!(z < curz || (shl->depth_lequal && z == curz))) continue;

        float iw = 1.0f / invw;

        CrFragment frag;
        frag.uv.x = (b0 * v0->uv_w.x + b1 * v1->uv_w.x + b2 * v2->uv_w.x) * iw;
        frag.uv.y = (b0 * v0->uv_w.y + b1 * v1->uv_w.y + b2 * v2->uv_w.y) * iw;
        frag.light = (b0 * v0->light_w + b1 * v1->light_w + b2 * v2->light_w) * iw;
        frag.ao = (b0 * v0->ao_w + b1 * v1->ao_w + b2 * v2->ao_w) * iw;
        frag.blk = (b0 * v0->blk_w + b1 * v1->blk_w + b2 * v2->blk_w) * iw;
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

        CrRgba c = cr_shade_dev(shl, &frag);
        if (c.a == 0) continue; /* alpha_test discard */

        if (shl->blend == 1 || shl->blend == 4) {
            CrRgba d = cur;
            float a = c.a * (1.0f / 255.0f);
            float ia = 1.0f - a;
            cur.r = (u8)(fminf(255.0f, fmaxf(0.0f, c.r * a + d.r * ia)) + 0.5f);
            cur.g = (u8)(fminf(255.0f, fmaxf(0.0f, c.g * a + d.g * ia)) + 0.5f);
            cur.b = (u8)(fminf(255.0f, fmaxf(0.0f, c.b * a + d.b * ia)) + 0.5f);
            cur.a = 255;
            if (shl->blend == 4) curz = z;
        } else if (shl->blend == 2) {
            /* GL blendFunc(DST_COLOR, SRC_COLOR): out = 2*src*dst. Dig crack. */
            CrRgba d = cur;
            cur.r = (u8)(fminf(255.0f, (2.0f * c.r * d.r) * (1.0f / 255.0f)) + 0.5f);
            cur.g = (u8)(fminf(255.0f, (2.0f * c.g * d.g) * (1.0f / 255.0f)) + 0.5f);
            cur.b = (u8)(fminf(255.0f, (2.0f * c.b * d.b) * (1.0f / 255.0f)) + 0.5f);
            cur.a = 255;
        } else if (shl->blend == 3) {
            CrRgba d = cur;
            float a = c.a * (1.0f / 255.0f);
            cur.r = (u8)(fminf(255.0f, c.r * a + (float)d.r) + 0.5f);
            cur.g = (u8)(fminf(255.0f, c.g * a + (float)d.g) + 0.5f);
            cur.b = (u8)(fminf(255.0f, c.b * a + (float)d.b) + 0.5f);
            cur.a = 255;
        } else {
            cur = c;
            curz = z;
        }
        }  /* end for k (compacted triangles overlapping this tile) */

        /* Refresh the hi-z bound with the depths this batch just wrote. The
         * store also acts as the "done reading slist" fence for the next
         * batch (sred and slist are distinct, and the barrier below orders
         * both). Only reached by batches that ran the walk, which are the only
         * ones that can have changed curz. */
        sred[lane] = valid ? curz : -3.402823466e38f;
        __syncthreads();
        for (int off = CR_TILE_N / 2; off > 0; off >>= 1) {
            if (lane < off) sred[lane] = fmaxf(sred[lane], sred[lane + off]);
            __syncthreads();
        }
        tile_maxz = sred[0];
    }  /* end for batch */

    if (valid) { color[idx] = cur; depth[idx] = curz; }
}

/* Alloc-once device state. cudaMalloc'd once in cr_raster_cuda_pre(), reused
 * every frame, freed in cr_raster_cuda_post(). The atlas mirror is cached by
 * the host texels pointer (the world atlas is static), so it uploads once. */
/* Device-side atlas mirror. Frames switch atlases mid-frame (terrain ->
 * entity -> terrain -> item), so a single-slot cache would cudaFree + realloc
 * + re-upload per switch, every frame. Cache each distinct atlas (keyed by
 * its host texels pointer) once for the process lifetime. */
#define CR_ATLAS_CACHE 6
typedef struct {
    const void *key;          /* host atlas->texels */
    CrTexture  *d_tex;        /* device CrTexture pointing at device copies */
    CrRgba     *d_texels;
    CrRgba     *d_mip[15];
    int         n_mip;
} CrAtlasSlot;

/* Ring of shade contexts: layer launches are asynchronous, so each layer
 * needs its own live device CrShadeCtx (and pinned host mirror for the async
 * copy). 16 slots >> the max ~8 layer draws per frame; frame_end's stream
 * sync retires a whole frame's slots at once. */
#define CR_SH_RING 16

/* Lazily page-locked host vert buffers (world layers, overlay, entities).
 * They are allocate-once on the game side, so the table saturates at a
 * handful of entries. */
#define CR_PIN_TAB 16
typedef struct { const void *p; size_t n; } CrPinEnt;

static struct {
    int inited;
    int W, H, max_tris;
    cudaStream_t stream;      /* all per-frame copies + kernels, in order */
    CrRgba *d_color;
    float  *d_depth;
    CrScreenTri *d_tris;   /* 2*max_tris slots: transform emits 0..2 per input tri */
    CrScreenTri *d_dense;  /* order-preserving global transform compaction */
    CrTriBox    *d_box;
    float       *d_tminz;  /* per-tri min vert z (hi-z reject), bbox-written */
    float       *d_batchz; /* per-256-tri-batch min tminz (batch hi-z reject) */
    int         *d_xcounts;/* live output count per 256-input transform block */
    int         *d_xoffsets;/* exclusive prefix of d_xcounts */
    int         *d_screen_tris; /* [0]=frame total; [1]=current dense layer */
    int          screen_tris;   /* host copy after frame_end */
    CrVertex    *d_verts;  /* input verts for the GPU transform path */
    CrShadeCtx  *d_sh;     /* CR_SH_RING slots */
    CrShadeCtx  *h_sh;     /* pinned host mirrors of the ring */
    CrRgba      *d_lm;     /* CR_SH_RING * 256 lightmap texels (per-slot LUT) */
    CrRgba      *h_lm;     /* pinned host staging, same layout */
    int          sh_idx;   /* next ring slot; reset by frame_end's sync */
    CrAtlasSlot  atlas[CR_ATLAS_CACHE];
    int          n_atlas;
    CrPinEnt     pins[CR_PIN_TAB];
    int          n_pins;
    int          frame_open;  /* 1 between frame_begin/frame_end: fb is resident */
    /* device-resident chunk-mesh slab pool (mirror of world_live's toroidal
     * mesh pool; uploaded per slot on rebuild, gathered on-GPU per frame). */
    CrVertex    *d_slabs;       /* slab_nslots * slab_cap contiguous verts */
    int         *slab_builds;   /* host: last-uploaded builds per slot (-1 = never) */
    int          slab_nslots, slab_cap;
    int         *d_gsrc, *d_gpfx;   /* gather tables (device) */
    int         *h_gsrc, *h_gpfx;   /* pinned ring: CR_GR_RING * (CR_GR_MAX+1) */
    int          gr_idx;            /* ring slot; reset by frame_end's sync */
    cudaEvent_t  end_ev;            /* deferred frame end (frame_end_async) */
    int          end_pending;
    cudaEvent_t  up_ev;             /* frame's H2D uploads (fb + slabs) done:
                                       the host may mutate those buffers again
                                       once this fires - long before end_ev */
} g_gpu = {0};

#define CR_GR_RING 8      /* gather calls per frame (terrain merges into 1) */
/* Entries per call. Replay needs 4 layers * mesh_slots (289). The window path
 * submits per-16-block-SECTION runs to keep W16's vertical cull, so its worst
 * case is mesh_slots * GM_MESH_SECTIONS = 4624; 8192 covers it with headroom.
 * Must stay >= game/game.h GM_GATHER_MAX_ENTRIES (and its Metal twin). */
#define CR_GR_MAX  8192

extern "C" void cr_raster_cuda_pre(int w, int h, int max_tris) {
    if (g_gpu.inited) return;
    g_gpu.W = w; g_gpu.H = h; g_gpu.max_tris = max_tris;
    size_t npix = (size_t)w * (size_t)h;
    cudaStreamCreate(&g_gpu.stream);
    cudaMalloc(&g_gpu.d_color, npix * sizeof(CrRgba));
    cudaMalloc(&g_gpu.d_depth, npix * sizeof(float));
    cudaMalloc(&g_gpu.d_tris,  2 * (size_t)max_tris * sizeof(CrScreenTri));
    cudaMalloc(&g_gpu.d_dense, 2 * (size_t)max_tris * sizeof(CrScreenTri));
    cudaMalloc(&g_gpu.d_box,   2 * (size_t)max_tris * sizeof(CrTriBox));
    cudaMalloc(&g_gpu.d_tminz, 2 * (size_t)max_tris * sizeof(float));
    /* one slot per bbox block; bbox is launched CR_BBOX_THREADS wide over the
     * same 2*max_tris slot space the tiled kernel batches over. */
    cudaMalloc(&g_gpu.d_batchz,
               ((2 * (size_t)max_tris + CR_BBOX_THREADS - 1) / CR_BBOX_THREADS + 1)
               * sizeof(float));
    cudaMalloc(&g_gpu.d_xcounts,
               (size_t)((max_tris + 255) / 256) * sizeof(int));
    cudaMalloc(&g_gpu.d_xoffsets,
               (size_t)((max_tris + 255) / 256) * sizeof(int));
    cudaMalloc(&g_gpu.d_screen_tris, 2 * sizeof(int));
    cudaMemset(g_gpu.d_screen_tris, 0, 2 * sizeof(int));
    cudaMalloc(&g_gpu.d_verts, 3 * (size_t)max_tris * sizeof(CrVertex));
    cudaMalloc(&g_gpu.d_sh,    CR_SH_RING * sizeof(CrShadeCtx));
    cudaMallocHost(&g_gpu.h_sh, CR_SH_RING * sizeof(CrShadeCtx));
    cudaMalloc(&g_gpu.d_lm,    CR_SH_RING * 256 * sizeof(CrRgba));
    cudaMallocHost(&g_gpu.h_lm, CR_SH_RING * 256 * sizeof(CrRgba));
    cudaEventCreateWithFlags(&g_gpu.end_ev, cudaEventDisableTiming);
    cudaEventCreateWithFlags(&g_gpu.up_ev,  cudaEventDisableTiming);
    g_gpu.sh_idx = 0;
    g_gpu.n_atlas = 0;
    g_gpu.n_pins = 0;
    g_gpu.inited = 1;
    cr_cuda_check("cuda_pre");
}

/* Upload the atlas (texels + mip chain) to a persistent device mirror on
 * first sight; later switches back to it are pointer-compare hits. Uploads
 * are synchronous cudaMemcpy (once per atlas per process, off the hot path).
 * Returns the device CrTexture to point the layer's shade ctx at. */
/* Host mutates atlas texels in place (water_still animation). Mark dirty so the
 * next cr_cuda_sync_atlas re-uploads the matching cache entry. */
static int g_atlas_host_dirty;
extern "C" void cr_raster_cuda_atlas_dirty(void) { g_atlas_host_dirty = 1; }

static CrTexture *cr_cuda_sync_atlas(const CrTexture *atlas) {
    for (int i = 0; i < g_gpu.n_atlas; ++i)
        if (g_gpu.atlas[i].key == (const void *)atlas->texels) {
            if (g_atlas_host_dirty) {
                size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
                cudaMemcpy(g_gpu.atlas[i].d_texels, atlas->texels,
                           ntex * sizeof(CrRgba), cudaMemcpyHostToDevice);
                for (int l = 0; l < g_gpu.atlas[i].n_mip; ++l) {
                    size_t nl = (size_t)atlas->mipw[l] *
                                (size_t)atlas->miph[l];
                    if (nl > 0 && atlas->mip[l])
                        cudaMemcpy(g_gpu.atlas[i].d_mip[l], atlas->mip[l],
                                   nl * sizeof(CrRgba),
                                   cudaMemcpyHostToDevice);
                }
                g_atlas_host_dirty = 0;
            }
            return g_gpu.atlas[i].d_tex;
        }

    if (g_gpu.n_atlas == CR_ATLAS_CACHE) {   /* never in practice: ~4 atlases */
        cudaDeviceSynchronize();
        CrAtlasSlot *s = &g_gpu.atlas[0];
        cudaFree(s->d_texels);
        for (int l = 0; l < s->n_mip; l++) if (s->d_mip[l]) cudaFree(s->d_mip[l]);
        cudaFree(s->d_tex);
        memmove(&g_gpu.atlas[0], &g_gpu.atlas[1],
                (CR_ATLAS_CACHE - 1) * sizeof(CrAtlasSlot));
        g_gpu.n_atlas--;
    }

    CrAtlasSlot *s = &g_gpu.atlas[g_gpu.n_atlas];
    memset(s, 0, sizeof *s);
    size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
    cudaMalloc(&s->d_texels, ntex * sizeof(CrRgba));
    cudaMemcpy(s->d_texels, atlas->texels, ntex * sizeof(CrRgba),
               cudaMemcpyHostToDevice);

    CrTexture h_tex = *atlas;
    h_tex.texels = s->d_texels;
    int n_mip = atlas->mip_levels;
    if (n_mip < 0) n_mip = 0;
    if (n_mip > 15) n_mip = 15;
    for (int l = 0; l < n_mip; l++) {
        size_t nl = (size_t)atlas->mipw[l] * (size_t)atlas->miph[l];
        s->d_mip[l] = NULL;
        if (nl > 0 && atlas->mip[l]) {
            cudaMalloc(&s->d_mip[l], nl * sizeof(CrRgba));
            cudaMemcpy(s->d_mip[l], atlas->mip[l], nl * sizeof(CrRgba),
                       cudaMemcpyHostToDevice);
        }
        h_tex.mip[l] = s->d_mip[l];
    }
    s->n_mip = n_mip;
    cudaMalloc(&s->d_tex, sizeof(CrTexture));
    cudaMemcpy(s->d_tex, &h_tex, sizeof(CrTexture), cudaMemcpyHostToDevice);
    s->key = atlas->texels;
    g_gpu.n_atlas++;
    cr_cuda_check("cuda_sync_atlas");
    return s->d_tex;
}

/* Grab the next shade-ctx ring slot. Pure modulo, no sync: a frame uses at
 * most ~7 slots and the pipeline is depth-1 (frame N's end_ev is waited
 * before frame N+1's end is armed), so a slot 16 allocations back belongs to
 * a frame at least two behind - already retired. Legacy one-shot callers
 * (cr_raster_cuda_into) synchronize within the call anyway. */
static CrShadeCtx *cr_cuda_sh_slot(CrShadeCtx **h_out) {
    if (g_gpu.sh_idx == CR_SH_RING) g_gpu.sh_idx = 0;
    int i = g_gpu.sh_idx++;
    *h_out = &g_gpu.h_sh[i];
    return &g_gpu.d_sh[i];
}

/* Stage the host lightmap LUT (256 texels) into the slot's pinned mirror,
 * enqueue its H2D, and repoint the ctx at the device copy. Rides the sh ring:
 * the same depth-1 reuse bound that protects the ctx protects its LUT. */
static void cr_cuda_patch_lightmap(CrShadeCtx *h_sh, int slot) {
    if (!h_sh->lightmap) return;
    CrRgba *hst = g_gpu.h_lm + (size_t)slot * 256;
    memcpy(hst, h_sh->lightmap, 256 * sizeof(CrRgba));
    cudaMemcpyAsync(g_gpu.d_lm + (size_t)slot * 256, hst,
                    256 * sizeof(CrRgba), cudaMemcpyHostToDevice, g_gpu.stream);
    h_sh->lightmap = g_gpu.d_lm + (size_t)slot * 256;
}

/* Uploads-done marker: recorded after a frame's last host-buffer H2D (fb +
 * chunk slabs), waited by the game BEFORE it mutates those buffers for the
 * next frame. Never-recorded events synchronize immediately (CUDA semantics),
 * so the first frame needs no special case. */
extern "C" void cr_raster_cuda_uploads_mark(void) {
    if (g_gpu.inited) cudaEventRecord(g_gpu.up_ev, g_gpu.stream);
}

extern "C" void cr_raster_cuda_uploads_wait(void) {
    if (g_gpu.inited) cudaEventSynchronize(g_gpu.up_ev);
}

/* Page-lock a host vert buffer on first sight so its per-layer H2D copy is
 * truly asynchronous (pageable "async" copies stage and stall the host).
 * Buffers are allocate-once on the game side; a same-pointer, larger-range
 * sighting re-registers after draining in-flight copies. */
static void cr_cuda_ensure_pinned(const void *p, size_t n) {
    for (int i = 0; i < g_gpu.n_pins; ++i) {
        if (g_gpu.pins[i].p != p) continue;
        if (g_gpu.pins[i].n >= n) return;
        cudaStreamSynchronize(g_gpu.stream);
        cudaHostUnregister((void *)p);
        cudaHostRegister((void *)p, n, cudaHostRegisterDefault);
        g_gpu.pins[i].n = n;
        return;
    }
    if (g_gpu.n_pins == CR_PIN_TAB) return; /* copy stays pageable, still correct */
    if (cudaHostRegister((void *)p, n, cudaHostRegisterDefault) == cudaSuccess) {
        g_gpu.pins[g_gpu.n_pins].p = p;
        g_gpu.pins[g_gpu.n_pins].n = n;
        g_gpu.n_pins++;
    } else {
        (void)cudaGetLastError();  /* already registered elsewhere (fb/tris) */
    }
}

/* Keep the framebuffer resident on the device across a frame's multiple layer
 * rasters. Without this, each of the 4 render_layer calls memcpys the whole fb
 * up and back (8 fb copies/frame); with it, the fb goes up once (after the host
 * sky pass) and comes back once (before the host HUD pass). Between begin/end,
 * cr_raster_cuda_into skips the per-call fb copies and accumulates depth/color
 * directly on the resident d_color/d_depth - identical result, fewer transfers. */
/* Pin host buffers that are memcpy'd every frame (fb color/depth, tris).
 * Pageable copies staged at ~1GB/s and were 2.5x the kernel time on the
 * nsys timeline; pinned goes direct over PCIe. Host side stays malloc'd -
 * these only page-lock existing allocations (clean CPU/GPU ownership). */
extern "C" void cr_raster_cuda_pin(void *p, size_t bytes) {
    if (p && bytes) cudaHostRegister(p, bytes, cudaHostRegisterDefault);
}

extern "C" void cr_raster_cuda_unpin(void *p) {
    if (p) cudaHostUnregister(p);
}

extern "C" void cr_raster_cuda_frame_begin(const CrFramebuffer *fb) {
    if (!g_gpu.inited) return;
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    /* Async on the frame stream (fb is pinned at open). The host does not
     * touch fb again until frame_end, which syncs the stream. */
    cudaMemcpyAsync(g_gpu.d_color, fb->color, npix * sizeof(CrRgba),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    cudaMemcpyAsync(g_gpu.d_depth, fb->depth, npix * sizeof(float),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    cudaMemsetAsync(g_gpu.d_screen_tris, 0, 2 * sizeof(int), g_gpu.stream);
    g_gpu.frame_open = 1;
}

/* GPU sky: fill every pixel of the resident d_color with the sky shader.
 * Runs right after frame_begin (depth is far everywhere - sky is the first
 * draw of the frame), on the frame stream, so layers queue behind it. */
static __global__ void k_sky(CrRgba *color, int W, int H, GmSkyCtx sc,
                             float Fx, float Fy, float Fz,
                             float Rx, float Ry, float Rz,
                             float Ux, float Uy, float Uz,
                             float tanH, float aspect) {
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= W || py >= H) return;
    float ndc_y = 1.0f - 2.0f * ((float)py + 0.5f) / (float)H;
    float sv = ndc_y * tanH;
    float ndc_x = 2.0f * ((float)px + 0.5f) / (float)W - 1.0f;
    float su = ndc_x * tanH * aspect;
    CrVec3 dir = v3(su * Rx + sv * Ux + Fx,
                    su * Ry + sv * Uy + Fy,
                    su * Rz + sv * Uz + Fz);
    color[py * W + px] = gm_sky_ray_color_ctx(&sc, dir);
}

extern "C" void cr_raster_cuda_sky(const GmSkyCtx *sc, const float *b,
                                   int W, int H) {
    if (!g_gpu.inited || !g_gpu.frame_open) return;
    static int tex_up = 0;
    if (!tex_up) {
        memcpy(cr_sky_sun_m, cr_sky_sun_host, sizeof cr_sky_sun_m);
        memcpy(cr_sky_moon_m, cr_sky_moon_host, sizeof cr_sky_moon_m);
        memcpy(cr_sky_end_m, cr_sky_end_host, sizeof cr_sky_end_m);
        tex_up = 1;
    }
    dim3 blk(16, 16), grd((W + 15) / 16, (H + 15) / 16);
    k_sky<<<grd, blk, 0, g_gpu.stream>>>(g_gpu.d_color, W, H, *sc,
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
    cr_cuda_check("cuda_sky");
}

extern "C" void cr_raster_cuda_frame_end(CrFramebuffer *fb) {
    if (!g_gpu.inited || !g_gpu.frame_open) return;
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    /* THE frame barrier: waits for every enqueued layer (copies + kernels),
     * then brings the fb home. Also retires the shade-ctx ring and makes all
     * host vert buffers safe to refill for the next frame. */
    cudaMemcpyAsync(fb->color, g_gpu.d_color, npix * sizeof(CrRgba),
                    cudaMemcpyDeviceToHost, g_gpu.stream);
    cudaMemcpyAsync(fb->depth, g_gpu.d_depth, npix * sizeof(float),
                    cudaMemcpyDeviceToHost, g_gpu.stream);
    cudaStreamSynchronize(g_gpu.stream);
    cudaMemcpy(&g_gpu.screen_tris, g_gpu.d_screen_tris, sizeof(int),
               cudaMemcpyDeviceToHost);
    cr_cuda_check("cuda_frame_end");
    g_gpu.sh_idx = 0;
    g_gpu.gr_idx = 0;
    g_gpu.frame_open = 0;
}

extern "C" int cr_raster_cuda_screen_tris(void) {
    return g_gpu.screen_tris;
}

/* Deferred frame end: enqueue the color readback into `dst_color` (pinned by
 * the caller) and record an event - NO sync, the GPU tail and D2H overlap
 * the sim ticks until the next rendered frame. Depth is NOT read back: the
 * frames-path consumers never read it (hand clears it, hud ignores it, the
 * next frame_begin re-uploads a cleared one). Rings retire in frame_wait,
 * which the caller MUST invoke before the next frame_begin. */
extern "C" int cr_raster_cuda_frame_end_async(CrFramebuffer *fb,
                                              CrRgba *dst_color) {
    if (!g_gpu.inited || !g_gpu.frame_open || g_gpu.end_pending) return 0;
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    cudaMemcpyAsync(dst_color, g_gpu.d_color, npix * sizeof(CrRgba),
                    cudaMemcpyDeviceToHost, g_gpu.stream);
    cudaEventRecord(g_gpu.end_ev, g_gpu.stream);
    cr_cuda_check("cuda_frame_end_async");
    g_gpu.end_pending = 1;
    g_gpu.frame_open = 0;
    return 1;
}

extern "C" void cr_raster_cuda_frame_wait(void) {
    if (!g_gpu.inited || !g_gpu.end_pending) return;
    cudaEventSynchronize(g_gpu.end_ev);
    cr_cuda_check("cuda_frame_wait");
    /* rings are NOT reset here: with the depth-1 pipeline the next frame's
     * slots are already enqueued behind this event; the rings advance modulo
     * and reuse is bounded by the pipeline depth (see cr_cuda_sh_slot). */
    g_gpu.end_pending = 0;
}

/* Matches cr_raster_cpu / cr_raster_cuda signature. Alloc-once fast path. */
extern "C" void cr_raster_cuda_into(CrFramebuffer *fb, const CrScreenTri *tris,
                                    int ntris, const CrShadeCtx *sh) {
    if (!g_gpu.inited) { cr_raster_cuda(fb, tris, ntris, sh); return; }
    if (ntris <= 0) return;
    if (ntris > g_gpu.max_tris) ntris = g_gpu.max_tris; /* bounded; caps guarantee fit */

    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    CrTexture *d_tex = cr_cuda_sync_atlas(sh->atlas);

    CrShadeCtx *h_sh;
    CrShadeCtx *d_sh = cr_cuda_sh_slot(&h_sh);
    *h_sh = *sh;
    h_sh->atlas = d_tex;
    cr_cuda_patch_lightmap(h_sh, (int)(h_sh - g_gpu.h_sh));
    cudaMemcpyAsync(d_sh, h_sh, sizeof(CrShadeCtx), cudaMemcpyHostToDevice,
                    g_gpu.stream);

    /* framebuffer up (skipped when it is already resident via frame_begin). */
    if (!g_gpu.frame_open) {
        cudaMemcpyAsync(g_gpu.d_color, fb->color, npix * sizeof(CrRgba),
                        cudaMemcpyHostToDevice, g_gpu.stream);
        cudaMemcpyAsync(g_gpu.d_depth, fb->depth, npix * sizeof(float),
                        cudaMemcpyHostToDevice, g_gpu.stream);
    }
    cudaMemcpyAsync(g_gpu.d_tris, tris, (size_t)ntris * sizeof(CrScreenTri),
                    cudaMemcpyHostToDevice, g_gpu.stream);

    /* pass 1: per-triangle clamped bbox + backface skip mark. */
    int bthr = CR_BBOX_THREADS, bblk = (ntris + bthr - 1) / bthr;
    cr_raster_bbox_kernel<<<bblk, bthr, 0, g_gpu.stream>>>(g_gpu.d_tris, ntris,
                                                           W, H, g_gpu.d_box,
                                                           g_gpu.d_tminz,
                                                           g_gpu.d_batchz,
                                                           g_gpu.d_screen_tris, 0);

    /* pass 2: one 16x16 block per screen tile, streaming tris in order. */
    dim3 block(16, 16);
    dim3 grid((W + block.x - 1) / block.x, (H + block.y - 1) / block.y);
    cr_raster_tiled_kernel<<<grid, block, 0, g_gpu.stream>>>(
        g_gpu.d_color, g_gpu.d_depth, W, H, g_gpu.d_tris, ntris, g_gpu.d_box,
        d_sh, g_gpu.d_tminz, g_gpu.d_batchz, g_gpu.d_screen_tris, 0,
        0x7fffffff, 0x7fffffff, 0x7fffffff);

    /* Caller reuses its tris scratch per call, so this legacy path stays
     * synchronous (it is off the hot path; render_layer is the async one). */
    cudaStreamSynchronize(g_gpu.stream);
    cr_cuda_check("cuda_into");

    /* framebuffer back (skipped while resident; frame_end copies it once). */
    if (!g_gpu.frame_open) {
        cudaMemcpy(fb->color, g_gpu.d_color, npix * sizeof(CrRgba), cudaMemcpyDeviceToHost);
        cudaMemcpy(fb->depth, g_gpu.d_depth, npix * sizeof(float),  cudaMemcpyDeviceToHost);
    }
}

/* ---- GPU transform path: verts -> screen tris on-device ------------------
 * One thread per input triangle emits 0..2 clipped front faces. Single-layer
 * draws compact them stably within each 256-input block; merged terrain keeps
 * fixed 2t/2t+1 slots so its existing shade boundaries remain valid. */
__global__ void cr_transform_kernel(const CrVertex *verts, int ntris_in,
                                    CrMat4 mvp, CrVec3 campos, int W, int H,
                                    CrScreenTri *out, int *block_counts,
                                    int *frame_count, int compact) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    CrScreenTri pair[2];
    int n = 0;
    if (t < ntris_in)
        n = cr_transform_tri_dev(mvp, campos, &verts[t * 3], W, H, pair);
    int kept = 0;
    for (int i = 0; i < n; ++i) {
        if (!cr_screen_backface(&pair[i]))
            pair[kept++] = pair[i];
    }
    n = kept;
    CrScreenTri zero;
    memset(&zero, 0, sizeof zero);
    if (!compact) {
        if (t < ntris_in) {
            out[2 * t]     = (n > 0) ? pair[0] : zero;
            out[2 * t + 1] = (n > 1) ? pair[1] : zero;
        }
        return;
    }

    /* Stable block-local compaction. Each 256-input block owns the same 512
     * output slots as before, but its live front faces occupy one prefix.
     * The tiled raster reads block_counts and skips the unused suffix, while
     * block order and lane order preserve the CPU triangle sequence. */
    __shared__ int scan[256];
    int lane = threadIdx.x;
    if (t < ntris_in) {
        out[2 * t] = zero;
        out[2 * t + 1] = zero;
    }
    scan[lane] = n;
    __syncthreads();
    for (int off = 1; off < 256; off <<= 1) {
        int v = lane >= off ? scan[lane - off] : 0;
        __syncthreads();
        scan[lane] += v;
        __syncthreads();
    }
    int excl = scan[lane] - n;
    int total = scan[255];
    if (lane == 255) {
        block_counts[blockIdx.x] = total;
        atomicAdd(frame_count, total);
    }
    int base = blockIdx.x * 512 + excl;
    if (n > 0) out[base] = pair[0];
    if (n > 1) out[base + 1] = pair[1];
}

/* Serial exclusive scan over transform-block live counts. nxblocks is at most
 * ~7820 at the configured cap, so one device thread is the simplest identical
 * CUDA/MSL implementation. screen_tris[0] remains the frame accumulator from
 * cr_transform_kernel; [1] is overwritten with this layer's dense count. */
__global__ void cr_compact_scan_kernel(const int *block_counts, int nxblocks,
                                       int *block_offsets, int *screen_tris) {
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    int total = 0;
    for (int xb = 0; xb < nxblocks; ++xb) {
        block_offsets[xb] = total;
        total += block_counts[xb];
    }
    screen_tris[1] = total;
}

/* Stable global scatter: block-major order plus each block's stable live
 * prefix is exactly the CPU transform output order. */
__global__ void cr_compact_scatter_kernel(const CrScreenTri *slotted,
                                          const int *block_counts,
                                          const int *block_offsets,
                                          int nxblocks, CrScreenTri *dense) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    int xb = t / 512;
    int i = t % 512;
    if (xb >= nxblocks || i >= block_counts[xb]) return;
    dense[block_offsets[xb] + i] = slotted[xb * 512 + i];
}

/* Full GPU layer render: upload verts once, transform + bbox + raster on
 * device. Replaces the host cr_transform + tri upload in cr_raster_cuda_into
 * (~29% of the frames-run CPU profile lived in cr_transform/to_screen).
 * MVP is built HERE with the same _dev source the kernel uses. */
/* Common tail for the layer paths: verts are already in d_verts (uploaded or
 * gathered on-device); run transform + bbox + raster on the frame stream. */
static void cr_cuda_run_layer(CrFramebuffer *fb, int nverts,
                              const CrCamera *cam, const CrShadeCtx *sh) {
    int ntris_in = nverts / 3;
    if (ntris_in > g_gpu.max_tris) ntris_in = g_gpu.max_tris; /* caps guarantee fit */

    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    float aspect = (float)W / (float)H; /* fb dims authoritative (transform.c) */
    CrMat4 proj = cr_perspective_dev(cam->fov_deg, aspect, cam->znear, cam->zfar);
    /* Must match host cr_transform / transform.c: cr_camera_view applies
     * EntityRenderer.hurtCameraEffect (hurt_roll_deg / hurt_yaw_deg). Using
     * look-only here made CPU vs CUDA frames diverge on every hurt tick
     * (scenario combat tapes: rolled horizon on CPU, flat on CUDA). */
    CrMat4 view = cr_camera_view_dev(cam);
    CrMat4 mvp  = cr_mat4_mul_dev(proj, view);

    CrTexture *d_tex = cr_cuda_sync_atlas(sh->atlas);
    CrShadeCtx *h_sh;
    CrShadeCtx *d_sh = cr_cuda_sh_slot(&h_sh);
    *h_sh = *sh;
    h_sh->atlas = d_tex;
    cr_cuda_patch_lightmap(h_sh, (int)(h_sh - g_gpu.h_sh));
    cudaMemcpyAsync(d_sh, h_sh, sizeof(CrShadeCtx), cudaMemcpyHostToDevice,
                    g_gpu.stream);

    int ntris = 2 * ntris_in; /* worst-case dense count; launch sizing only */
    int tthr = 256, tblk = (ntris_in + tthr - 1) / tthr;
    cr_transform_kernel<<<tblk, tthr, 0, g_gpu.stream>>>(
        g_gpu.d_verts, ntris_in, mvp, cam->pos, W, H, g_gpu.d_tris,
        g_gpu.d_xcounts, g_gpu.d_screen_tris, 1);

    cr_compact_scan_kernel<<<1, 1, 0, g_gpu.stream>>>(
        g_gpu.d_xcounts, tblk, g_gpu.d_xoffsets, g_gpu.d_screen_tris);
    int sthr = 256, nslots = tblk * 512;
    int sblk = (nslots + sthr - 1) / sthr;
    cr_compact_scatter_kernel<<<sblk, sthr, 0, g_gpu.stream>>>(
        g_gpu.d_tris, g_gpu.d_xcounts, g_gpu.d_xoffsets, tblk, g_gpu.d_dense);

    int bthr = CR_BBOX_THREADS, bblk = (ntris + bthr - 1) / bthr;
    cr_raster_bbox_kernel<<<bblk, bthr, 0, g_gpu.stream>>>(g_gpu.d_dense, ntris,
                                                           W, H, g_gpu.d_box,
                                                           g_gpu.d_tminz,
                                                           g_gpu.d_batchz,
                                                           g_gpu.d_screen_tris, 1);

    dim3 block(16, 16);
    dim3 grid((W + block.x - 1) / block.x, (H + block.y - 1) / block.y);
    cr_raster_tiled_kernel<<<grid, block, 0, g_gpu.stream>>>(
        g_gpu.d_color, g_gpu.d_depth, W, H, g_gpu.d_dense, ntris, g_gpu.d_box,
        d_sh, g_gpu.d_tminz, g_gpu.d_batchz, g_gpu.d_screen_tris, 1,
        0x7fffffff, 0x7fffffff, 0x7fffffff);
    /* No sync: layers queue back-to-back on the stream; frame_end (resident
     * fb) or the D2H below (standalone call) is the barrier. */
    cr_cuda_check("cuda_render_layer");

    if (!g_gpu.frame_open) {
        cudaStreamSynchronize(g_gpu.stream);
        cudaMemcpy(fb->color, g_gpu.d_color, npix * sizeof(CrRgba), cudaMemcpyDeviceToHost);
        cudaMemcpy(fb->depth, g_gpu.d_depth, npix * sizeof(float),  cudaMemcpyDeviceToHost);
    }
}

extern "C" void cr_raster_cuda_render_layer(CrFramebuffer *fb,
                                            const CrVertex *verts, int nverts,
                                            const CrCamera *cam,
                                            const CrShadeCtx *sh) {
    if (!g_gpu.inited || !verts || nverts < 3) return;
    int ntris_in = nverts / 3;
    if (ntris_in > g_gpu.max_tris) ntris_in = g_gpu.max_tris;

    if (!g_gpu.frame_open) {
        size_t npix = (size_t)fb->w * (size_t)fb->h;
        cudaMemcpyAsync(g_gpu.d_color, fb->color, npix * sizeof(CrRgba),
                        cudaMemcpyHostToDevice, g_gpu.stream);
        cudaMemcpyAsync(g_gpu.d_depth, fb->depth, npix * sizeof(float),
                        cudaMemcpyHostToDevice, g_gpu.stream);
    }
    /* HOST-BUFFER CONTRACT: `verts` must stay untouched until frame_end's
     * stream sync (world layers are stable per-frame slabs; frame_capture
     * rotates its entity buffers so same-frame emits never alias). */
    size_t vbytes = (size_t)ntris_in * 3 * sizeof(CrVertex);
    cr_cuda_ensure_pinned(verts, vbytes);
    cudaMemcpyAsync(g_gpu.d_verts, verts, vbytes, cudaMemcpyHostToDevice,
                    g_gpu.stream);
    cr_cuda_run_layer(fb, ntris_in * 3, cam, sh);
}

/* ---- device-resident chunk meshes ------------------------------------- */

/* One-time pool alloc mirroring world_live's toroidal mesh-slab pool.
 * Returns 1 on success; 0 leaves the host-concat path in charge. */
extern "C" int cr_raster_cuda_slab_pool(int nslots, int slab_verts) {
    if (!g_gpu.inited) return 0;
    if (g_gpu.d_slabs) return g_gpu.slab_nslots == nslots &&
                              g_gpu.slab_cap == slab_verts;
    if (nslots <= 0 || nslots > CR_GR_MAX || slab_verts <= 0) return 0;
    size_t vbytes = (size_t)nslots * (size_t)slab_verts * sizeof(CrVertex);
    if (cudaMalloc(&g_gpu.d_slabs, vbytes) != cudaSuccess) {
        g_gpu.d_slabs = NULL;
        return 0;
    }
    int tab = CR_GR_RING * (CR_GR_MAX + 1);
    if (cudaMalloc(&g_gpu.d_gsrc, tab * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&g_gpu.d_gpfx, tab * sizeof(int)) != cudaSuccess ||
        cudaMallocHost(&g_gpu.h_gsrc, tab * sizeof(int)) != cudaSuccess ||
        cudaMallocHost(&g_gpu.h_gpfx, tab * sizeof(int)) != cudaSuccess ||
        !(g_gpu.slab_builds = (int *)malloc((size_t)nslots * sizeof(int)))) {
        if (g_gpu.d_slabs) cudaFree(g_gpu.d_slabs);
        if (g_gpu.d_gsrc)  cudaFree(g_gpu.d_gsrc);
        if (g_gpu.d_gpfx)  cudaFree(g_gpu.d_gpfx);
        if (g_gpu.h_gsrc)  cudaFreeHost(g_gpu.h_gsrc);
        if (g_gpu.h_gpfx)  cudaFreeHost(g_gpu.h_gpfx);
        g_gpu.d_slabs = NULL; g_gpu.d_gsrc = g_gpu.d_gpfx = NULL;
        g_gpu.h_gsrc = g_gpu.h_gpfx = NULL;
        return 0;
    }
    for (int i = 0; i < nslots; ++i) g_gpu.slab_builds[i] = -1;
    g_gpu.slab_nslots = nslots;
    g_gpu.slab_cap = slab_verts;
    g_gpu.gr_idx = 0;
    cr_cuda_check("cuda_slab_pool");
    return 1;
}

/* Upload slot's packed slab if its rebuild counter moved. Pageable async copy
 * (rebuilds are rare after warmup; the slab host buffer is stable until the
 * slot's NEXT rebuild, which can only happen after frame_end's sync). */
extern "C" void cr_raster_cuda_slab_sync(int slot, int builds,
                                         const void *host, int used_verts) {
    if (!g_gpu.d_slabs || slot < 0 || slot >= g_gpu.slab_nslots) return;
    if (g_gpu.slab_builds[slot] == builds) return;
    if (used_verts > g_gpu.slab_cap) used_verts = g_gpu.slab_cap;
    if (used_verts > 0)
        cudaMemcpyAsync(g_gpu.d_slabs + (size_t)slot * g_gpu.slab_cap, host,
                        (size_t)used_verts * sizeof(CrVertex),
                        cudaMemcpyHostToDevice, g_gpu.stream);
    g_gpu.slab_builds[slot] = builds;
}

/* Forget every uploaded slab. Needed when the renderer switches to a different
 * GmWorld (dimension change): each world counts slot rebuilds from zero, so a
 * new world's counters can equal the cached ones and silently skip uploads,
 * leaving the old world's geometry on the GPU. */
extern "C" void cr_raster_cuda_slabs_reset(void) {
    if (!g_gpu.slab_builds) return;
    for (int i = 0; i < g_gpu.slab_nslots; ++i) g_gpu.slab_builds[i] = -1;
}

/* Concatenate gather entries (u32 words) from the slab pool into d_verts:
 * thread w binary-searches the word-prefix table for its entry. */
static __global__ void cr_gather_kernel(unsigned int *dst,
                                        const unsigned int *slabs,
                                        const int *src_word, const int *pfx,
                                        int nents, int total_words) {
    int w = blockIdx.x * blockDim.x + threadIdx.x;
    if (w >= total_words) return;
    int lo = 0, hi = nents - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (pfx[mid] <= w) lo = mid; else hi = mid - 1;
    }
    dst[w] = slabs[src_word[lo] + (w - pfx[lo])];
}

#define CR_VERT_WORDS ((int)(sizeof(CrVertex) / sizeof(unsigned int)))

/* Render one layer whose verts live in the device slab pool: src_vert[i] is
 * a vert offset in POOL coords (slot * slab_cap + layer off), nvert[i] its
 * count. Entries are concatenated in order - byte-identical to the host
 * wl_append concat of the same chunks. */
extern "C" void cr_raster_cuda_render_gather(CrFramebuffer *fb,
                                             const int *src_vert,
                                             const int *nvert, int nents,
                                             int total_verts,
                                             const CrCamera *cam,
                                             const CrShadeCtx *sh) {
    if (!g_gpu.inited || !g_gpu.d_slabs || nents <= 0 || total_verts < 3)
        return;
    if (nents > CR_GR_MAX) return;   /* caps guarantee <= mesh_slots < CR_GR_MAX */
    if (total_verts > 3 * g_gpu.max_tris) total_verts = 3 * g_gpu.max_tris;

    int base = g_gpu.gr_idx * (CR_GR_MAX + 1);
    g_gpu.gr_idx = (g_gpu.gr_idx + 1) % CR_GR_RING; /* frame_end sync retires */
    int *hs = g_gpu.h_gsrc + base, *hp = g_gpu.h_gpfx + base;
    int words = 0;
    for (int i = 0; i < nents; ++i) {
        hs[i] = src_vert[i] * CR_VERT_WORDS;
        hp[i] = words;
        words += nvert[i] * CR_VERT_WORDS;
    }
    int cap_words = total_verts * CR_VERT_WORDS;
    if (words > cap_words) words = cap_words;
    cudaMemcpyAsync(g_gpu.d_gsrc + base, hs, (size_t)nents * sizeof(int),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    cudaMemcpyAsync(g_gpu.d_gpfx + base, hp, (size_t)nents * sizeof(int),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    int thr = 256, blk = (words + thr - 1) / thr;
    cr_gather_kernel<<<blk, thr, 0, g_gpu.stream>>>(
        (unsigned int *)g_gpu.d_verts, (const unsigned int *)g_gpu.d_slabs,
        g_gpu.d_gsrc + base, g_gpu.d_gpfx + base, nents, words);
    cr_cuda_check("cuda_render_gather");
    cr_cuda_run_layer(fb, total_verts, cam, sh);
}

/* All 4 terrain layers as ONE gather + transform + bbox + raster chain.
 * Entries are the per-chunk layer runs concatenated in layer-major order
 * (all of layer 0's chunks, then layer 1, ...); lay_verts[l] is layer l's
 * vert total. The tiled raster visits tri slots in strict ascending order,
 * so shading slot t with the shade ctx of the layer that owns t is
 * pixel-identical to the 4 sequential launches - only 3 kernel launches and
 * one ring slot burn instead of 12+4. */
extern "C" void cr_raster_cuda_render_terrain(CrFramebuffer *fb,
                                              const int *src_vert,
                                              const int *nvert, int nents,
                                              const int lay_verts[4],
                                              const CrCamera *cam,
                                              const CrShadeCtx sh[4]) {
    if (!g_gpu.inited || !g_gpu.d_slabs || nents <= 0 || nents > CR_GR_MAX)
        return;
    int total_verts = lay_verts[0] + lay_verts[1] + lay_verts[2] + lay_verts[3];
    if (total_verts < 3) return;
    if (total_verts > 3 * g_gpu.max_tris) total_verts = 3 * g_gpu.max_tris;

    /* gather: identical to render_gather but over the combined entry list. */
    int base = g_gpu.gr_idx * (CR_GR_MAX + 1);
    g_gpu.gr_idx = (g_gpu.gr_idx + 1) % CR_GR_RING;
    int *hs = g_gpu.h_gsrc + base, *hp = g_gpu.h_gpfx + base;
    int words = 0;
    for (int i = 0; i < nents; ++i) {
        hs[i] = src_vert[i] * CR_VERT_WORDS;
        hp[i] = words;
        words += nvert[i] * CR_VERT_WORDS;
    }
    int cap_words = total_verts * CR_VERT_WORDS;
    if (words > cap_words) words = cap_words;
    cudaMemcpyAsync(g_gpu.d_gsrc + base, hs, (size_t)nents * sizeof(int),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    cudaMemcpyAsync(g_gpu.d_gpfx + base, hp, (size_t)nents * sizeof(int),
                    cudaMemcpyHostToDevice, g_gpu.stream);
    int gthr = 256, gblk = (words + gthr - 1) / gthr;
    cr_gather_kernel<<<gblk, gthr, 0, g_gpu.stream>>>(
        (unsigned int *)g_gpu.d_verts, (const unsigned int *)g_gpu.d_slabs,
        g_gpu.d_gsrc + base, g_gpu.d_gpfx + base, nents, words);

    int ntris_in = total_verts / 3;
    if (ntris_in > g_gpu.max_tris) ntris_in = g_gpu.max_tris;
    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    float aspect = (float)W / (float)H;
    CrMat4 proj = cr_perspective_dev(cam->fov_deg, aspect, cam->znear, cam->zfar);
    /* Same as cr_cuda_run_layer: full camera view including hurt roll. */
    CrMat4 view = cr_camera_view_dev(cam);
    CrMat4 mvp  = cr_mat4_mul_dev(proj, view);

    /* 4 CONTIGUOUS shade-ctx ring slots (kernel indexes d_sh[0..3]); modulo
     * wrap without sync - same depth-1 reuse bound as cr_cuda_sh_slot. */
    if (g_gpu.sh_idx + 4 > CR_SH_RING) g_gpu.sh_idx = 0;
    int si = g_gpu.sh_idx;
    g_gpu.sh_idx += 4;
    for (int l = 0; l < 4; ++l) {
        g_gpu.h_sh[si + l] = sh[l];
        g_gpu.h_sh[si + l].atlas = cr_cuda_sync_atlas(sh[l].atlas);
        cr_cuda_patch_lightmap(&g_gpu.h_sh[si + l], si + l);
    }
    cudaMemcpyAsync(&g_gpu.d_sh[si], &g_gpu.h_sh[si], 4 * sizeof(CrShadeCtx),
                    cudaMemcpyHostToDevice, g_gpu.stream);

    /* layer boundaries in OUTPUT tri-slot space (input tri i -> slots 2i,2i+1) */
    int sb1 = 2 * (lay_verts[0] / 3);
    int sb2 = sb1 + 2 * (lay_verts[1] / 3);
    int sb3 = sb2 + 2 * (lay_verts[2] / 3);

    int ntris = 2 * ntris_in;
    int tthr = 256, tblk = (ntris_in + tthr - 1) / tthr;
    cr_transform_kernel<<<tblk, tthr, 0, g_gpu.stream>>>(
        g_gpu.d_verts, ntris_in, mvp, cam->pos, W, H, g_gpu.d_tris,
        g_gpu.d_xcounts, g_gpu.d_screen_tris, 0);
    int bthr = CR_BBOX_THREADS, bblk = (ntris + bthr - 1) / bthr;
    cr_raster_bbox_kernel<<<bblk, bthr, 0, g_gpu.stream>>>(g_gpu.d_tris, ntris,
                                                           W, H, g_gpu.d_box,
                                                           g_gpu.d_tminz,
                                                           g_gpu.d_batchz,
                                                           g_gpu.d_screen_tris, 0);
    dim3 block(16, 16);
    dim3 grid((W + block.x - 1) / block.x, (H + block.y - 1) / block.y);
    cr_raster_tiled_kernel<<<grid, block, 0, g_gpu.stream>>>(
        g_gpu.d_color, g_gpu.d_depth, W, H, g_gpu.d_tris, ntris, g_gpu.d_box,
        &g_gpu.d_sh[si], g_gpu.d_tminz, g_gpu.d_batchz, g_gpu.d_screen_tris, 0,
        sb1, sb2, sb3);
    cr_cuda_check("cuda_render_terrain");

    if (!g_gpu.frame_open) {
        cudaStreamSynchronize(g_gpu.stream);
        cudaMemcpy(fb->color, g_gpu.d_color, npix * sizeof(CrRgba), cudaMemcpyDeviceToHost);
        cudaMemcpy(fb->depth, g_gpu.d_depth, npix * sizeof(float),  cudaMemcpyDeviceToHost);
    }
}

extern "C" void cr_raster_cuda_post(void) {
    if (!g_gpu.inited) return;
    cudaStreamSynchronize(g_gpu.stream);
    for (int i = 0; i < g_gpu.n_pins; ++i)
        cudaHostUnregister((void *)g_gpu.pins[i].p);
    if (g_gpu.d_color)  cudaFree(g_gpu.d_color);
    if (g_gpu.d_depth)  cudaFree(g_gpu.d_depth);
    if (g_gpu.d_tris)   cudaFree(g_gpu.d_tris);
    if (g_gpu.d_dense)  cudaFree(g_gpu.d_dense);
    if (g_gpu.d_box)    cudaFree(g_gpu.d_box);
    if (g_gpu.d_tminz)  cudaFree(g_gpu.d_tminz);
    if (g_gpu.d_batchz) cudaFree(g_gpu.d_batchz);
    if (g_gpu.d_xcounts) cudaFree(g_gpu.d_xcounts);
    if (g_gpu.d_xoffsets) cudaFree(g_gpu.d_xoffsets);
    if (g_gpu.d_screen_tris) cudaFree(g_gpu.d_screen_tris);
    if (g_gpu.d_verts)  cudaFree(g_gpu.d_verts);
    if (g_gpu.d_sh)     cudaFree(g_gpu.d_sh);
    if (g_gpu.d_lm)     cudaFree(g_gpu.d_lm);
    if (g_gpu.h_lm)     cudaFreeHost(g_gpu.h_lm);
    if (g_gpu.h_sh)     cudaFreeHost(g_gpu.h_sh);
    if (g_gpu.d_slabs)  cudaFree(g_gpu.d_slabs);
    if (g_gpu.d_gsrc)   cudaFree(g_gpu.d_gsrc);
    if (g_gpu.d_gpfx)   cudaFree(g_gpu.d_gpfx);
    if (g_gpu.h_gsrc)   cudaFreeHost(g_gpu.h_gsrc);
    if (g_gpu.h_gpfx)   cudaFreeHost(g_gpu.h_gpfx);
    free(g_gpu.slab_builds);
    cudaEventDestroy(g_gpu.end_ev);
    cudaEventDestroy(g_gpu.up_ev);
    for (int i = 0; i < g_gpu.n_atlas; ++i) {
        CrAtlasSlot *s = &g_gpu.atlas[i];
        if (s->d_texels) cudaFree(s->d_texels);
        for (int l = 0; l < s->n_mip; l++)
            if (s->d_mip[l]) cudaFree(s->d_mip[l]);
        if (s->d_tex) cudaFree(s->d_tex);
    }
    cudaStreamDestroy(g_gpu.stream);
    memset(&g_gpu, 0, sizeof(g_gpu));
}

/* Matches the extern "C" prototype in core/types.h. */
void cr_raster_cuda(CrFramebuffer *fb, const CrScreenTri *tris, int ntris,
                    const CrShadeCtx *sh) {
    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    CrRgba *d_color = NULL;
    float *d_depth = NULL;
    cudaMalloc(&d_color, npix * sizeof(CrRgba));
    cudaMalloc(&d_depth, npix * sizeof(float));
    cudaMemcpy(d_color, fb->color, npix * sizeof(CrRgba), cudaMemcpyHostToDevice);
    cudaMemcpy(d_depth, fb->depth, npix * sizeof(float), cudaMemcpyHostToDevice);

    /* Deep-copy the shading context: atlas texels -> device, a device CrTexture
     * pointing at them, a device CrShadeCtx pointing at that. */
    const CrTexture *atlas = sh->atlas;
    size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
    CrRgba *d_texels = NULL;
    cudaMalloc(&d_texels, ntex * sizeof(CrRgba));
    cudaMemcpy(d_texels, atlas->texels, ntex * sizeof(CrRgba),
               cudaMemcpyHostToDevice);

    CrTexture h_tex = *atlas;
    h_tex.texels = d_texels;
    /* Copy the mip chain too, else device code would deref host pointers. */
    CrRgba *d_mip[15] = {0};
    int n_mip = atlas->mip_levels;
    if (n_mip < 0) n_mip = 0;
    if (n_mip > 15) n_mip = 15;
    for (int l = 0; l < n_mip; l++) {
        size_t nl = (size_t)atlas->mipw[l] * (size_t)atlas->miph[l];
        d_mip[l] = NULL;
        if (nl > 0 && atlas->mip[l]) {
            cudaMalloc(&d_mip[l], nl * sizeof(CrRgba));
            cudaMemcpy(d_mip[l], atlas->mip[l], nl * sizeof(CrRgba),
                       cudaMemcpyHostToDevice);
        }
        h_tex.mip[l] = d_mip[l];
    }
    CrTexture *d_tex = NULL;
    cudaMalloc(&d_tex, sizeof(CrTexture));
    cudaMemcpy(d_tex, &h_tex, sizeof(CrTexture), cudaMemcpyHostToDevice);

    CrShadeCtx h_sh = *sh;
    h_sh.atlas = d_tex;
    CrShadeCtx *d_sh = NULL;
    CrRgba *d_lm1 = NULL;
    if (h_sh.lightmap) {
        cudaMalloc(&d_lm1, 256 * sizeof(CrRgba));
        cudaMemcpy(d_lm1, h_sh.lightmap, 256 * sizeof(CrRgba),
                   cudaMemcpyHostToDevice);
        h_sh.lightmap = d_lm1;
    }
    cudaMalloc(&d_sh, sizeof(CrShadeCtx));
    cudaMemcpy(d_sh, &h_sh, sizeof(CrShadeCtx), cudaMemcpyHostToDevice);

    dim3 block(16, 16);
    for (int t = 0; t < ntris; t++) {
        const CrScreenVert *v0 = &tris[t].v[0];
        const CrScreenVert *v1 = &tris[t].v[1];
        const CrScreenVert *v2 = &tris[t].v[2];
        float x0 = v0->spos.x, y0 = v0->spos.y;
        float x1 = v1->spos.x, y1 = v1->spos.y;
        float x2 = v2->spos.x, y2 = v2->spos.y;

        float area = cr_edge(x0, y0, x1, y1, x2, y2);
        if (area * CR_FRONT_SIGN <= 0.0f) continue;

        float fminx = fminf(x0, fminf(x1, x2));
        float fmaxx = fmaxf(x0, fmaxf(x1, x2));
        float fminy = fminf(y0, fminf(y1, y2));
        float fmaxy = fmaxf(y0, fmaxf(y1, y2));
        int minx = (int)floorf(fminx); if (minx < 0) minx = 0;
        int maxx = (int)ceilf(fmaxx);  if (maxx > W) maxx = W;
        int miny = (int)floorf(fminy); if (miny < 0) miny = 0;
        int maxy = (int)ceilf(fmaxy);  if (maxy > H) maxy = H;
        int bw = maxx - minx;
        int bh = maxy - miny;
        if (bw <= 0 || bh <= 0) continue;

        dim3 grid((bw + block.x - 1) / block.x, (bh + block.y - 1) / block.y);
        cr_raster_tri_kernel<<<grid, block>>>(d_color, d_depth, W, H, tris[t],
                                              d_sh, minx, miny, bw, bh);
    }
    cudaDeviceSynchronize();
    cr_cuda_check("raster");

    cudaMemcpy(fb->color, d_color, npix * sizeof(CrRgba), cudaMemcpyDeviceToHost);
    cudaMemcpy(fb->depth, d_depth, npix * sizeof(float), cudaMemcpyDeviceToHost);

    cudaFree(d_color);
    cudaFree(d_depth);
    cudaFree(d_texels);
    for (int l = 0; l < n_mip; l++) if (d_mip[l]) cudaFree(d_mip[l]);
    cudaFree(d_tex);
    cudaFree(d_sh);
    if (d_lm1) cudaFree(d_lm1);
}
