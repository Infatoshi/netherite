/* magma - Metal rasterizer kernels (metal/raster_kernels.metal).
 *
 * Port of cuda/raster_cuda.cu's __global__ kernels to MSL, semantics-first.
 * The CPU reference is cpu/raster_cpu.c; the CUDA path is bit-exact vs CPU
 * under --fmad=false and this file mirrors the CUDA kernels expression-for-
 * expression (same expression trees, same int/float conversions, same
 * rounding) so the Metal path inherits the same parity target.
 *
 * BUILD CONTRACT (host loads the .metallib at runtime, see raster_metal_host.m):
 *   xcrun -sdk macosx metal -fno-fast-math [-ffp-contract=off] \
 *       raster_kernels.metal -o raster_kernels.metallib
 * -fno-fast-math makes '/', sqrt and friends IEEE-correct but does NOT by
 * itself forbid fma contraction; the file-scope `#pragma clang fp contract(off)`
 * below is the load-bearing guard (the CUDA build uses --fmad=false, the CPU
 * build -ffp-contract=off; contraction anywhere breaks the bit contract).
 * Passing -ffp-contract=off on the command line as well is belt-and-braces.
 * MEASURED-ON-MAC: confirm the produced metallib really contains no fused
 * multiply-adds on a parity gate frame (pragma/flag honored by the metal
 * front end must be measured, not assumed).
 *
 * Kernel map (CUDA -> MSL, same dispatch geometry):
 *   cr_raster_tri_kernel   -> cr_raster_tri_kernel    16x16 threadgroups over one
 *                                                     triangle's clamped bbox
 *   cr_raster_bbox_kernel  -> cr_raster_bbox_kernel   256-wide 1D, one thread/tri
 *   cr_raster_tiled_kernel -> cr_raster_tiled_kernel  16x16 threadgroup == one
 *                                                     screen tile, one thread/pixel
 *   k_sky                  -> k_sky                   16x16 over the framebuffer
 *   cr_transform_kernel    -> cr_transform_kernel     256-wide 1D, one thread/tri
 *   cr_gather_kernel       -> cr_gather_kernel        256-wide 1D, one thread/word
 *
 * Race/order model (identical to CUDA): the tiled kernel gives every pixel to
 * exactly one thread which walks triangles in ascending index order, so there
 * are NO depth/color atomics anywhere; the legacy per-triangle kernel relies
 * on serial dispatch order (one dispatch per triangle in a serial compute
 * encoder) exactly like CUDA's default-stream serialization.
 *
 * Struct mirrors: every *M struct below redeclares a core/types.h (or
 * game/sky.h) struct with byte-identical layout; offsets are documented at
 * each declaration and guarded with static_assert(sizeof) where MSL allows.
 * Scalar x/y/z fields are used instead of float3 (float3 is 16-byte aligned
 * in MSL and would break the C layout).
 *
 * Pointers inside structs (CrTextureM.texels/mip, CrShadeCtxM.atlas/lightmap)
 * are raw GPU virtual addresses written by the host via MTLBuffer.gpuAddress
 * (argument-buffer tier 2, Apple silicon). The host marks every such buffer
 * resident with useResource before dispatch.
 * MEASURED-ON-MAC: verify gpuAddress pointer-chasing + useResource residency
 * on the target macOS (needs macOS 13+); verify
 * maxTotalThreadsPerThreadgroup >= 256 for the tiled PSO (Apple silicon: 1024).
 */

#include <metal_stdlib>
using namespace metal;

/* No fma contraction anywhere in this translation unit (see BUILD CONTRACT). */
#pragma clang fp contract(off)

typedef uint  u32;
typedef uchar u8;

/* Must match cpu/raster_cpu.c exactly. */
#define CR_FRONT_SIGN -1.0f

/* ==================== struct mirrors (byte layout = core/types.h) ========= */

/* CrVec2: 8 bytes, align 4. offsets: x 0, y 4. */
typedef struct { float x, y; } CrVec2M;
/* CrVec3: 12 bytes, align 4. offsets: x 0, y 4, z 8. */
typedef struct { float x, y, z; } CrVec3M;
/* CrVec4: 16 bytes, align 4. */
typedef struct { float x, y, z, w; } CrVec4M;
/* CrMat4: 64 bytes, align 4; column-major m[col*4+row]. */
typedef struct { float m[16]; } CrMat4M;
/* CrRgba: 4 bytes, align 1. offsets: r 0, g 1, b 2, a 3. */
typedef struct { u8 r, g, b, a; } CrRgbaM;

/* CrVertex: 36 bytes, align 4.
 * offsets: pos 0, uv 12, light 20, tint 24, ao 28, blk 32. */
typedef struct {
    CrVec3M pos;
    CrVec2M uv;
    float   light;
    CrRgbaM tint;
    float   ao;
    float   blk;
} CrVertexM;

/* CrScreenVert: 56 bytes, align 4.
 * offsets: spos 0, invw 12, uv_w 16, light_w 24, ao_w 28, eye_dist_w 32,
 * tint_r_w 36, tint_g_w 40, tint_b_w 44, tint_a_w 48, blk_w 52. */
typedef struct {
    CrVec3M spos;
    float   invw;
    CrVec2M uv_w;
    float   light_w;
    float   ao_w;
    float   eye_dist_w;
    float   tint_r_w, tint_g_w, tint_b_w, tint_a_w;
    float   blk_w;
} CrScreenVertM;

/* CrScreenTri: 168 bytes. */
typedef struct { CrScreenVertM v[3]; } CrScreenTriM;

/* CrTexture: 264 bytes, align 8 (pointers are 8-byte GPU VAs).
 * offsets: w 0, h 4, texels 8, tile 16, mip_levels 20, mip 24 (15*8=120),
 * mipw 144 (15*4=60), miph 204 (60). size 264. */
typedef struct {
    int w, h;
    device const CrRgbaM *texels;
    int tile;
    int mip_levels;
    device const CrRgbaM *mip[15];
    int mipw[15], miph[15];
} CrTextureM;

/* CrRenderLayer values (core/types.h enum). */
#define CR_LAYER_SOLID         0
#define CR_LAYER_CUTOUT_MIPPED 1
#define CR_LAYER_CUTOUT        2
#define CR_LAYER_TRANSLUCENT   3

/* CrShadeCtx: 96 bytes, align 8.
 * offsets: atlas 0, fog_color 8, fog_start 12, fog_end 16, alpha_test 20,
 * alpha_ref 24, enable_fog 28, layer 32, blend 36, use_mips 40, mip_bias 44,
 * lightmap 48, depth_lequal 56, fog_exp_density 60, alpha_mask 64,
 * mask_u_off 68, mask_v_off 72, untextured 76, color_trunc 80, cover_eps 84,
 * sample_mode 88, entity_brightness 92. size 96. */
typedef struct {
    device const CrTextureM *atlas;
    CrRgbaM fog_color;
    float   fog_start;
    float   fog_end;
    int     alpha_test;
    float   alpha_ref;
    int     enable_fog;
    int     layer;
    int     blend;
    int     use_mips;
    float   mip_bias;
    device const CrRgbaM *lightmap;
    int     depth_lequal;
    float   fog_exp_density;
    int     alpha_mask;
    float   mask_u_off, mask_v_off;
    int     untextured;
    int     color_trunc;
    float   cover_eps;
    int     sample_mode;
    int     entity_brightness;
} CrShadeCtxM;

/* CrFragment: kernel-internal only (no host ABI). */
typedef struct {
    CrVec2M uv;
    float   light;
    float   ao;
    CrRgbaM tint;
    float   eye_dist;
    float   lod;
    float   blk;
} CrFragmentM;

/* GmSkyCtx (game/sky.h): 92 bytes, align 4.
 * offsets: sky_top 0, fog 12, sunset_active 24, sunset 28, sun_h 44,
 * starB 56, cA 60, sA 64, uw 68, uw_fog 72, uw_density 84, plane_y 88. */
typedef struct {
    CrVec3M sky_top, fog;
    int     sunset_active;
    float   sunset[4];
    CrVec3M sun_h;
    float   starB;
    float   cA, sA;
    int     uw;
    CrVec3M uw_fog;
    float   uw_density;
    float   plane_y;
} GmSkyCtxM;

static_assert(sizeof(CrVec3M)       == 12,  "CrVec3 layout");
static_assert(sizeof(CrRgbaM)       == 4,   "CrRgba layout");
static_assert(sizeof(CrVertexM)     == 36,  "CrVertex layout");
static_assert(sizeof(CrScreenVertM) == 56,  "CrScreenVert layout");
static_assert(sizeof(CrScreenTriM)  == 168, "CrScreenTri layout");
static_assert(sizeof(CrTextureM)    == 264, "CrTexture layout");
static_assert(sizeof(CrShadeCtxM)   == 96,  "CrShadeCtx layout");
static_assert(sizeof(GmSkyCtxM)     == 92,  "GmSkyCtx layout");

/* ==================== kernel parameter blocks =============================
 * Each is mirrored byte-for-byte by a C struct in raster_metal_host.m and
 * bound with setBytes. All members are 4-byte-aligned scalars/aggregates, so
 * C (arm64) and MSL agree without padding surprises. */

typedef struct {                 /* cr_raster_tri_kernel, buffer(2) */
    int W, H, minx, miny, bw, bh;
    CrScreenTriM tri;            /* offset 24, by value like the CUDA param */
} TriParamsM;

typedef struct { int ntris, W, H, use_device_count; } BboxParamsM; /* buffer(1) */

typedef struct {                 /* cr_raster_tiled_kernel, buffer(2) */
    int W, H, ntris;
    int sb1, sb2, sb3;           /* merged-layer tri-slot boundaries */
    int shbase;                  /* shade-ctx ring base slot (CUDA passed
                                    &d_sh[si]; an offset avoids setBuffer
                                    offset-alignment constraints) */
    int use_device_count;        /* screen_tris[1] is current dense length */
} TileParamsM;

typedef struct {                 /* k_sky, buffer(1) */
    int W, H;
    GmSkyCtxM sc;                /* offset 8 */
    float b[11];                 /* offset 100: F(3) R(3) U(3) tanH aspect */
} SkyParamsM;

typedef struct {                 /* cr_transform_kernel, buffer(1) */
    int ntris_in, W, H, compact;
    CrMat4M mvp;                 /* offset 16 */
    CrVec3M campos;              /* offset 80 */
} XformParamsM;

typedef struct { int nents, total_words, base; } GatherParamsM; /* buffer(4) */

/* ==================== shared raster helpers (== raster_cuda.cu) =========== */

static inline float cr_edge(float ax, float ay, float bx, float by,
                            float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static inline int cr_top_left(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

/* Deterministic log2f: exponent + fixed-order polynomial, no transcendental.
 * Bit-identical to the CPU/CUDA cr_log2_det (pure mul/add + bit ops). */
static inline float cr_log2_det(float x) {
    u32 i = as_type<u32>(x);
    int e = (int)((i >> 23) & 0xFF) - 127;
    i = (i & 0x007FFFFFu) | 0x3F800000u;
    float m = as_type<float>(i);
    float p = -1.7417939f + (2.8212026f + (-1.4699568f +
              (0.4479489f - 0.0563525f * m) * m) * m) * m;
    return (float)e + p;
}

/* Per-triangle constant LOD; matches cpu/raster_cpu.c expression-for-expression. */
static inline float cr_tri_lod(thread const CrScreenVertM *v0,
                               thread const CrScreenVertM *v1,
                               thread const CrScreenVertM *v2,
                               device const CrTextureM *tex,
                               float area2) {
    if (!tex || tex->w <= 0 || tex->h <= 0) return 0.0f;
    float iw0 = 1.0f / v0->invw, iw1 = 1.0f / v1->invw, iw2 = 1.0f / v2->invw;
    float u0 = v0->uv_w.x * iw0, uv0 = v0->uv_w.y * iw0;
    float u1 = v1->uv_w.x * iw1, uv1 = v1->uv_w.y * iw1;
    float u2 = v2->uv_w.x * iw2, uv2 = v2->uv_w.y * iw2;
    float tw = (float)tex->w, th = (float)tex->h;
    float ex1 = (u1 - u0) * tw, ey1 = (uv1 - uv0) * th;
    float ex2 = (u2 - u0) * tw, ey2 = (uv2 - uv0) * th;
    float texArea2 = fabs(ex1 * ey2 - ex2 * ey1);
    float pixArea2 = fabs(area2);
    if (texArea2 <= 0.0f || pixArea2 <= 0.0f) return 0.0f;
    return 0.5f * cr_log2_det(texArea2 / pixArea2);
}

/* ==================== core/shade.c port ===================================
 * The CUDA build #includes core/shade.c; this is the same code with two
 * mechanical (value-preserving) changes:
 *   - the g_cr_sample_mode_override device global becomes an explicit `mode`
 *     parameter (MSL has no writable program-scope globals). The CUDA global
 *     is 0 except between "override = sh->sample_mode" and the sample, so
 *     the value flow is identical: mask samples get 0, the main texel sample
 *     gets sh->sample_mode. (This also removes the CUDA global's cross-thread
 *     write race without changing any produced value.)
 *   - host-only getenv branches (#if !defined(__CUDA_ARCH__)) are absent,
 *     exactly as they are absent from CUDA device code (bu = bv = 0). */

static CrRgbaM cr_atlas_sample(device const CrTextureM *tex, float u, float v,
                               int mode) {
    CrRgbaM out;
    if (!tex || !tex->texels || tex->w <= 0 || tex->h <= 0) {
        out.r = 0; out.g = 0; out.b = 0; out.a = 255;
        return out;
    }
    float bu = 0.0f, bv = 0.0f;          /* host-only MAGMA_TEXEL_BIAS: 0 on device */
    float fu = u * (float)tex->w + bu;
    float fv = v * (float)tex->h + bv;
    int ix, iy;
    /* 0: floor(u*w - 1e-4) default; 1: floor; 2: round; 3: floor+1e-4 */
    if (mode == 1) { ix = (int)floor(fu); iy = (int)floor(fv); }
    else if (mode == 2) { ix = (int)floor(fu + 0.5f); iy = (int)floor(fv + 0.5f); }
    else if (mode == 3) { ix = (int)floor(fu + 1e-4f); iy = (int)floor(fv + 1e-4f); }
    else { ix = (int)floor(fu - 1e-4f); iy = (int)floor(fv - 1e-4f); }
    if (ix < 0) ix = 0; else if (ix >= tex->w) ix = tex->w - 1;
    if (iy < 0) iy = 0; else if (iy >= tex->h) iy = tex->h - 1;
    return tex->texels[iy * tex->w + ix];
}

/* Mip-aware nearest fetch (GL_NEAREST_MIPMAP_NEAREST); floorf only. */
static CrRgbaM cr_atlas_sample_lod(device const CrTextureM *tex, float u,
                                   float v, float lod) {
    CrRgbaM out;
    if (!tex || !tex->texels || tex->w <= 0 || tex->h <= 0) {
        out.r = 0; out.g = 0; out.b = 0; out.a = 255;
        return out;
    }
    int L = (int)floor(lod + 0.5f);
    if (L < 0) L = 0;
    if (L > tex->mip_levels) L = tex->mip_levels;

    device const CrRgbaM *tx; int tw, th;
    if (L == 0) { tx = tex->texels; tw = tex->w; th = tex->h; }
    else        { tx = tex->mip[L - 1]; tw = tex->mipw[L - 1]; th = tex->miph[L - 1]; }
    if (!tx || tw <= 0 || th <= 0) { tx = tex->texels; tw = tex->w; th = tex->h; }

    int ix = (int)floor(u * (float)tw);
    int iy = (int)floor(v * (float)th);
    if (ix < 0) ix = 0; else if (ix >= tw) ix = tw - 1;
    if (iy < 0) iy = 0; else if (iy >= th) iy = th - 1;
    return tx[iy * tw + ix];
}

/* Deterministic e^-x built from mul/add/floor only, one fixed order
 * (core/shade.c cr_exp_neg). Bit-identical across CPU/CUDA/Metal. */
static float cr_exp_neg(float x) {
    if (x <= 0.0f) return 1.0f;
    float t = x * 1.4426950408889634f;       /* log2(e) */
    if (t >= 127.0f) return 0.0f;
    float i = floor(t);
    float f = t - i;
    float u = f * -0.6931471805599453f;      /* -ln2 */
    float p = 1.0f + u * (1.0f + u * (0.5f + u * (0.16666666666666666f
                  + u * (0.041666666666666664f + u * 0.008333333333333333f))));
    int n = (int)i;
    float s = 1.0f;
    while (n >= 8) { s = s * 0.00390625f; n -= 8; }  /* 2^-8 exact */
    while (n > 0)  { s = s * 0.5f; --n; }            /* 2^-1 exact */
    return p * s;
}

static CrRgbaM cr_shade(device const CrShadeCtxM *sh,
                        thread const CrFragmentM *frag) {
    CrRgbaM out;
    const float inv255 = 1.0f / 255.0f;

    /* RenderDragon death dissolve. Mask sample uses mode 0 (the CUDA global
     * override is still 0 at this point). */
    if (sh->alpha_mask && frag->light < 0.0f && sh->atlas) {
        float mu = frag->uv.x + sh->mask_u_off;
        float mv = frag->uv.y + sh->mask_v_off;
        CrRgbaM mask = cr_atlas_sample(sh->atlas, mu, mv, 0);
        if ((float)mask.a * inv255 <= frag->ao) {
            out.r = 0; out.g = 0; out.b = 0; out.a = 0;
            return out;
        }
    }

    CrRgbaM texel;
    if (sh->untextured) {
        texel.r = 255; texel.g = 255; texel.b = 255; texel.a = 255;
    } else {
        texel = sh->use_mips
            ? cr_atlas_sample_lod(sh->atlas, frag->uv.x, frag->uv.y,
                                  frag->lod + sh->mip_bias)
            : cr_atlas_sample(sh->atlas, frag->uv.x, frag->uv.y,
                              sh->sample_mode);
    }

    int alpha_test = sh->alpha_test
        || sh->layer == CR_LAYER_CUTOUT
        || sh->layer == CR_LAYER_CUTOUT_MIPPED;
    if (sh->alpha_mask && frag->light < 0.0f) alpha_test = 0;
    if (alpha_test) {
        float ref = sh->alpha_ref > 0.0f ? sh->alpha_ref : 0.5f;
        int thr = (int)(ref * 255.0f + 1e-5f); /* floor(ref*255); 0.5 -> 127 */
        if ((int)texel.a <= thr) {
            out.r = 0; out.g = 0; out.b = 0; out.a = 0; /* discard */
            return out;
        }
    }

    float lmr = 1.0f, lmg = 1.0f, lmb = 1.0f;
    float brightness_mix = 0.0f;
    float frag_blk = frag->blk;
    if (sh->entity_brightness && frag_blk < 0.0f) {
        float packed = -frag_blk;
        float whole = floor(packed);
        frag_blk = whole - 1.0f;
        brightness_mix = (packed - whole) * 32.0f;
        brightness_mix = fmax(0.0f, fmin(1.0f, brightness_mix));
    }
    float lscalar = (frag->light < 0.0f) ? 1.0f : frag->light;
    float ao_mul = (sh->alpha_mask && frag->light < 0.0f) ? 1.0f : frag->ao;
    if (sh->lightmap && frag->light >= 0.0f) {
        float s = fmax(0.0f, fmin(15.0f, frag->light));
        float b = fmax(0.0f, fmin(15.0f, frag_blk));
        int s0 = (int)floor(s), b0 = (int)floor(b);
        int s1 = s0 < 15 ? s0 + 1 : 15, b1 = b0 < 15 ? b0 + 1 : 15;
        float fs = s - (float)s0, fb = b - (float)b0;
        device const CrRgbaM *T = sh->lightmap;
        CrRgbaM t00 = T[s0 * 16 + b0], t01 = T[s0 * 16 + b1];
        CrRgbaM t10 = T[s1 * 16 + b0], t11 = T[s1 * 16 + b1];
        float w00 = (1.0f - fs) * (1.0f - fb), w01 = (1.0f - fs) * fb;
        float w10 = fs * (1.0f - fb), w11 = fs * fb;
        lmr = ((float)t00.r * w00 + (float)t01.r * w01
             + (float)t10.r * w10 + (float)t11.r * w11) * inv255;
        lmg = ((float)t00.g * w00 + (float)t01.g * w01
             + (float)t10.g * w10 + (float)t11.g * w11) * inv255;
        lmb = ((float)t00.b * w00 + (float)t01.b * w01
             + (float)t10.b * w10 + (float)t11.b * w11) * inv255;
        lscalar = 1.0f;
    }
    float la = lscalar * ao_mul;
    float tr = frag->tint.r * inv255, tg = frag->tint.g * inv255,
          tb = frag->tint.b * inv255;
    float cr = (texel.r * inv255) * tr * la * lmr;
    float cg = (texel.g * inv255) * tg * la * lmg;
    float cb = (texel.b * inv255) * tb * la * lmb;
    if (brightness_mix > 0.0f) {
        cr += (1.0f - cr) * brightness_mix;
        cg += (1.0f - cg) * brightness_mix;
        cb += (1.0f - cb) * brightness_mix;
    }

    if (sh->enable_fog) {
        float t;
        if (sh->fog_exp_density > 0.0f) {
            t = 1.0f - cr_exp_neg(sh->fog_exp_density * frag->eye_dist);
        } else {
            float denom = sh->fog_end - sh->fog_start;
            t = (frag->eye_dist - sh->fog_start) / denom;
        }
        t = fmax(0.0f, fmin(1.0f, t));
        float fr = sh->fog_color.r * inv255;
        float fg = sh->fog_color.g * inv255;
        float fb2 = sh->fog_color.b * inv255;
        cr = cr + (fr - cr) * t;
        cg = cg + (fg - cg) * t;
        cb = cb + (fb2 - cb) * t;
    }

    if (sh->color_trunc) {
        out.r = (u8)(fmax(0.0f, fmin(1.0f, cr)) * 255.0f);
        out.g = (u8)(fmax(0.0f, fmin(1.0f, cg)) * 255.0f);
        out.b = (u8)(fmax(0.0f, fmin(1.0f, cb)) * 255.0f);
    } else {
        out.r = (u8)(fmax(0.0f, fmin(1.0f, cr)) * 255.0f + 0.5f);
        out.g = (u8)(fmax(0.0f, fmin(1.0f, cg)) * 255.0f + 0.5f);
        out.b = (u8)(fmax(0.0f, fmin(1.0f, cb)) * 255.0f + 0.5f);
    }

    if (sh->layer == CR_LAYER_SOLID) {
        out.a = 255;
    } else {
        float ca = (texel.a * inv255) * (frag->tint.a * inv255);
        u8 av;
        if (sh->color_trunc)
            av = (u8)(fmax(0.0f, fmin(1.0f, ca)) * 255.0f);
        else
            av = (u8)(fmax(0.0f, fmin(1.0f, ca)) * 255.0f + 0.5f);
        if (av == 0) av = 1;
        out.a = av;
    }
    return out;
}

/* ==================== legacy per-triangle kernel ==========================
 * One thread per pixel of one triangle's clamped bbox; the host dispatches one
 * serial-encoder dispatch per triangle in CPU order (== CUDA default-stream
 * launches), so overlapping-triangle depth resolution matches without atomics.
 * MEASURED-ON-MAC: the host uses a plain computeCommandEncoder (serial
 * dispatch type); verify dispatch-order serialization on overlapping tris. */
kernel void cr_raster_tri_kernel(device CrRgbaM *color        [[buffer(0)]],
                                 device float   *depth        [[buffer(1)]],
                                 constant TriParamsM &p       [[buffer(2)]],
                                 device const CrShadeCtxM *sh [[buffer(3)]],
                                 uint2 tpig [[thread_position_in_grid]]) {
    int lx = (int)tpig.x;
    int ly = (int)tpig.y;
    if (lx >= p.bw || ly >= p.bh) return;
    int px = p.minx + lx;
    int py = p.miny + ly;
    int W = p.W;

    CrScreenTriM tri = p.tri;    /* thread copy; values identical to CUDA's
                                    by-value kernel parameter */
    thread const CrScreenVertM *v0 = &tri.v[0];
    thread const CrScreenVertM *v1 = &tri.v[1];
    thread const CrScreenVertM *v2 = &tri.v[2];

    float x0 = v0->spos.x, y0 = v0->spos.y;
    float x1 = v1->spos.x, y1 = v1->spos.y;
    float x2 = v2->spos.x, y2 = v2->spos.y;

    float area = cr_edge(x0, y0, x1, y1, x2, y2);
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
    if (!(z < depth[idx] || (sh->depth_lequal && z == depth[idx]))) return;

    float iw = 1.0f / invw;

    CrFragmentM frag;
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
        frag.tint.r = (u8)(fmin(255.0f, fmax(0.0f, tr)) + 0.5f);
        frag.tint.g = (u8)(fmin(255.0f, fmax(0.0f, tg)) + 0.5f);
        frag.tint.b = (u8)(fmin(255.0f, fmax(0.0f, tb)) + 0.5f);
        frag.tint.a = (u8)(fmin(255.0f, fmax(0.0f, ta)) + 0.5f);
    }
    frag.eye_dist = (b0 * v0->eye_dist_w + b1 * v1->eye_dist_w
                   + b2 * v2->eye_dist_w) * iw;
    frag.lod = sh->use_mips ? cr_tri_lod(v0, v1, v2, sh->atlas, area) : 0.0f;

    CrRgbaM c = cr_shade(sh, &frag);
    if (c.a == 0) return;   /* alpha_test discard: no color/depth write */

    if (sh->blend == 1 || sh->blend == 4) {
        CrRgbaM d = color[idx];
        float a = c.a * (1.0f / 255.0f);
        float ia = 1.0f - a;
        color[idx].r = (u8)(fmin(255.0f, fmax(0.0f, c.r * a + d.r * ia)) + 0.5f);
        color[idx].g = (u8)(fmin(255.0f, fmax(0.0f, c.g * a + d.g * ia)) + 0.5f);
        color[idx].b = (u8)(fmin(255.0f, fmax(0.0f, c.b * a + d.b * ia)) + 0.5f);
        color[idx].a = 255;
        if (sh->blend == 4) depth[idx] = z;
    } else if (sh->blend == 2) {
        CrRgbaM d = color[idx];
        color[idx].r = (u8)(fmin(255.0f, (2.0f * c.r * d.r) * (1.0f / 255.0f)) + 0.5f);
        color[idx].g = (u8)(fmin(255.0f, (2.0f * c.g * d.g) * (1.0f / 255.0f)) + 0.5f);
        color[idx].b = (u8)(fmin(255.0f, (2.0f * c.b * d.b) * (1.0f / 255.0f)) + 0.5f);
        color[idx].a = 255;
    } else if (sh->blend == 3) {
        CrRgbaM d = color[idx];
        float a = c.a * (1.0f / 255.0f);
        color[idx].r = (u8)(fmin(255.0f, c.r * a + (float)d.r) + 0.5f);
        color[idx].g = (u8)(fmin(255.0f, c.g * a + (float)d.g) + 0.5f);
        color[idx].b = (u8)(fmin(255.0f, c.b * a + (float)d.b) + 0.5f);
        color[idx].a = 255;
    } else {
        color[idx] = c;
        depth[idx] = z;
    }
}

/* ==================== bbox / hi-z prepass ================================= */

typedef u32 CrTriBox;
#define CR_TBOX_SKIP 0xFF000000u   /* tminx=255, never matches a real tile */

/* Tile geometry, needed by BOTH raster kernels: the tiled kernel streams
 * triangles in CR_TILE_N-sized batches, and the bbox kernel is dispatched with
 * exactly CR_TILE_N threads per threadgroup, so bbox threadgroup b covers
 * tiled batch b. */
#define CR_TILE 16
#define CR_TILE_N (CR_TILE * CR_TILE)   /* 256 threads / triangles per batch */
#define CR_BBOX_THREADS CR_TILE_N       /* dispatch config: must equal CR_TILE_N */

static inline CrTriBox cr_tbox_pack(int tminx, int tminy, int tmaxx, int tmaxy) {
    return ((u32)(tminx & 0xFF) << 24) | ((u32)(tminy & 0xFF) << 16) |
           ((u32)(tmaxx & 0xFF) << 8)  |  (u32)(tmaxy & 0xFF);
}

kernel void cr_raster_bbox_kernel(device const CrScreenTriM *tris [[buffer(0)]],
                                  constant BboxParamsM &p         [[buffer(1)]],
                                  device CrTriBox *box            [[buffer(2)]],
                                  device float *tminz             [[buffer(3)]],
                                  device const int *screen_tris   [[buffer(4)]],
                                  device float *batchz            [[buffer(5)]],
                                  uint tpig  [[thread_position_in_grid]],
                                  uint tpitg [[thread_position_in_threadgroup]],
                                  uint tgpig [[threadgroup_position_in_grid]]) {
    int t = (int)tpig;
    int raster_ntris = p.use_device_count ? screen_tris[1] : p.ntris;
    int live = (t < raster_ntris);
    int W = p.W, H = p.H;

    /* hi-z reject input: interpolated z is barycentric over the vert z's, so
     * min vert z lower-bounds the tri's depth anywhere on screen. Written for
     * every live slot, including ones this kernel marks SKIP (a skipped tri's
     * z only pulls the batch bound DOWN, i.e. stays conservative). */
    float mz = 3.402823466e38f;
    if (live) {
        mz = fmin(tris[t].v[0].spos.z,
                  fmin(tris[t].v[1].spos.z, tris[t].v[2].spos.z));
        tminz[t] = mz;
    }
    /* BATCH HI-Z: min tminz over this threadgroup's CR_TILE_N triangles. Dead
     * lanes (t >= raster_ntris) feed +inf so a partial tail batch bounds only
     * its live triangles. Tree reduction over threadgroup memory: no atomics,
     * so the value is bit-deterministic run to run. Every thread must reach
     * these barriers, hence no early return above. */
    threadgroup float sbz[CR_BBOX_THREADS];
    int blane = (int)tpitg;
    sbz[blane] = mz;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int off = CR_BBOX_THREADS / 2; off > 0; off >>= 1) {
        if (blane < off) sbz[blane] = fmin(sbz[blane], sbz[blane + off]);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (blane == 0) batchz[tgpig] = sbz[0];

    if (!live) return;
    CrScreenTriM tri = tris[t];
    thread const CrScreenVertM *v0 = &tri.v[0];
    thread const CrScreenVertM *v1 = &tri.v[1];
    thread const CrScreenVertM *v2 = &tri.v[2];
    float x0 = v0->spos.x, y0 = v0->spos.y;
    float x1 = v1->spos.x, y1 = v1->spos.y;
    float x2 = v2->spos.x, y2 = v2->spos.y;

    float area = cr_edge(x0, y0, x1, y1, x2, y2);
    if (area * CR_FRONT_SIGN <= 0.0f) { box[t] = CR_TBOX_SKIP; return; }

    float fminx = fmin(x0, fmin(x1, x2));
    float fmaxx = fmax(x0, fmax(x1, x2));
    float fminy = fmin(y0, fmin(y1, y2));
    float fmaxy = fmax(y0, fmax(y1, y2));
    int minx = (int)floor(fminx); if (minx < 0) minx = 0;
    int maxx = (int)ceil(fmaxx);  if (maxx > W) maxx = W;
    int miny = (int)floor(fminy); if (miny < 0) miny = 0;
    int maxy = (int)ceil(fmaxy);  if (maxy > H) maxy = H;
    if (minx >= maxx || miny >= maxy) { box[t] = CR_TBOX_SKIP; return; }
    box[t] = cr_tbox_pack(minx >> 4, miny >> 4, (maxx - 1) >> 4, (maxy - 1) >> 4);
}

/* ==================== tiled rasterizer ====================================
 * 16x16 threadgroup == one screen tile; one thread per pixel; triangles walked
 * in strict ascending index order per pixel -> bit-identical to cr_raster_cpu.
 * CUDA's single __syncthreads_or is replaced by a threadgroup atomic OR plus
 * barriers (MSL has no fused barrier-reduce); this adds one barrier but
 * changes no ordering or values.
 * MEASURED-ON-MAC: verify the barrier/atomic pattern on-device (no hang, all
 * 256 threads participate; the any-flag branch is threadgroup-uniform). */
kernel void cr_raster_tiled_kernel(device CrRgbaM *color           [[buffer(0)]],
                                   device float   *depth           [[buffer(1)]],
                                   constant TileParamsM &p         [[buffer(2)]],
                                   device const CrScreenTriM *tris [[buffer(3)]],
                                   device const CrTriBox *box      [[buffer(4)]],
                                   device const CrShadeCtxM *sh_ring [[buffer(5)]],
                                   device const float *tminz       [[buffer(6)]],
                                   device const int *screen_tris   [[buffer(7)]],
                                   device const float *batchz      [[buffer(8)]],
                                   uint2 tgpig [[threadgroup_position_in_grid]],
                                   uint2 tpitg [[thread_position_in_threadgroup]]) {
    int W = p.W, H = p.H, ntris = p.ntris;
    int sb1 = p.sb1, sb2 = p.sb2, sb3 = p.sb3;
    device const CrShadeCtxM *sh = sh_ring + p.shbase;

    int px = (int)(tgpig.x * CR_TILE + tpitg.x);
    int py = (int)(tgpig.y * CR_TILE + tpitg.y);

    int bx = (int)tgpig.x, by = (int)tgpig.y;   /* this block's tile index */

    int valid = (px < W) && (py < H);
    int idx = valid ? (py * W + px) : 0;

    float fx = (float)px + 0.5f;
    float fy = (float)py + 0.5f;

    /* running color/depth for this pixel (exclusive to this thread). */
    CrRgbaM cur; float curz;
    if (valid) { cur = color[idx]; curz = depth[idx]; }
    else       { cur.r = cur.g = cur.b = cur.a = 0; curz = 0.0f; }

    threadgroup int sflag[CR_TILE_N];   /* pass flag, then inclusive-scan buffer */
    threadgroup int slist[CR_TILE_N];   /* compacted, ascending triangle indices */
    threadgroup float sred[CR_TILE_N];  /* tile max-depth reduction scratch */
    threadgroup atomic_int s_any;       /* __syncthreads_or replacement */
    int lane = (int)(tpitg.y * CR_TILE + tpitg.x);   /* 0..255 */

    int raster_ntris = p.use_device_count ? screen_tris[1] : ntris;
    int nbatches = (raster_ntris + CR_TILE_N - 1) / CR_TILE_N;

    /* HI-Z bound: the tile's worst (largest) pixel depth (see raster_cuda.cu).
     * curz only ever shrinks, so a bound carried forward from an earlier point
     * in the stream is conservative; it is refreshed only after a batch that
     * actually ran the walk, which are the only ones that can change curz. */
    sred[lane] = valid ? curz : -3.402823466e38f;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int off = CR_TILE_N / 2; off > 0; off >>= 1) {
        if (lane < off) sred[lane] = fmax(sred[lane], sred[lane + off]);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float tile_maxz = sred[0];

    for (int batch = 0; batch < nbatches; ++batch) {
        /* BATCH HI-Z: batchz[batch] is the min tminz over the batch's 256
         * triangles (bbox kernel). If even the nearest of them is behind the
         * tile's worst depth, every one would be rejected by the per-triangle
         * test below, so the whole batch is skipped - no box loads, no scan,
         * no barrier. The branch is threadgroup-uniform (batchz[batch] and
         * tile_maxz are both uniform), so no thread is left behind at a
         * barrier. Strict > keeps it valid for LEQUAL contexts too, whose
         * per-tri test is the strict one. */
        if (batchz[batch] > tile_maxz) continue;
        int base = batch * CR_TILE_N;
        int batch_n = raster_ntris - base;
        if (batch_n > CR_TILE_N) batch_n = CR_TILE_N;
        int t0 = base + lane;
        int pass = 0;
        if (lane < batch_n) {
            CrTriBox b = box[t0];         /* 4-byte packed inclusive tile bounds */
            int tminx = (int)((b >> 24) & 0xFF), tminy = (int)((b >> 16) & 0xFF);
            int tmaxx = (int)((b >> 8)  & 0xFF), tmaxy = (int)( b        & 0xFF);
            pass = (tminx <= bx && bx <= tmaxx && tminy <= by && by <= tmaxy);
        }
        sflag[lane] = pass;
        /* __syncthreads_or(pass): OR-reduce with barriers. All 256 threads take
         * the same branch (s_any is uniform after the barrier). */
        if (lane == 0) atomic_store_explicit(&s_any, 0, memory_order_relaxed);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (pass) atomic_fetch_or_explicit(&s_any, 1, memory_order_relaxed);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (!atomic_load_explicit(&s_any, memory_order_relaxed)) continue;

        /* Hillis-Steele inclusive scan over the 256 pass flags. */
        for (int off = 1; off < CR_TILE_N; off <<= 1) {
            int v = (lane >= off) ? sflag[lane - off] : 0;
            threadgroup_barrier(mem_flags::mem_threadgroup);
            sflag[lane] += v;
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        int total = sflag[CR_TILE_N - 1];
        int excl  = sflag[lane] - pass;      /* exclusive prefix = my slot */
        if (pass) slist[excl] = t0;
        /* publish slist before the coverage walk reads it */
        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (int k = 0; k < total; k++) {
            if (!valid) break;               /* whole tile off-screen: nothing to do */
            int t = slist[k];
            device const CrShadeCtxM *shl =
                sh + ((t >= sb1) + (t >= sb2) + (t >= sb3));
            if (shl->depth_lequal ? (tminz[t] > tile_maxz)
                                  : (tminz[t] >= tile_maxz)) continue;
            CrScreenTriM tri = tris[t];      /* thread copy; identical values */
            thread const CrScreenVertM *v0 = &tri.v[0];
            thread const CrScreenVertM *v1 = &tri.v[1];
            thread const CrScreenVertM *v2 = &tri.v[2];

            float x0 = v0->spos.x, y0 = v0->spos.y;
            float x1 = v1->spos.x, y1 = v1->spos.y;
            float x2 = v2->spos.x, y2 = v2->spos.y;

            float area = cr_edge(x0, y0, x1, y1, x2, y2);
            if (area * CR_FRONT_SIGN <= 0.0f) continue;

            float tri_lod = shl->use_mips
                ? cr_tri_lod(v0, v1, v2, shl->atlas, area) : 0.0f;

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

            CrFragmentM frag;
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
                frag.tint.r = (u8)(fmin(255.0f, fmax(0.0f, tr)) + 0.5f);
                frag.tint.g = (u8)(fmin(255.0f, fmax(0.0f, tg)) + 0.5f);
                frag.tint.b = (u8)(fmin(255.0f, fmax(0.0f, tb)) + 0.5f);
                frag.tint.a = (u8)(fmin(255.0f, fmax(0.0f, ta)) + 0.5f);
            }
            frag.eye_dist = (b0 * v0->eye_dist_w + b1 * v1->eye_dist_w
                           + b2 * v2->eye_dist_w) * iw;
            frag.lod = tri_lod;

            CrRgbaM c = cr_shade(shl, &frag);
            if (c.a == 0) continue; /* alpha_test discard */

            if (shl->blend == 1 || shl->blend == 4) {
                CrRgbaM d = cur;
                float a = c.a * (1.0f / 255.0f);
                float ia = 1.0f - a;
                cur.r = (u8)(fmin(255.0f, fmax(0.0f, c.r * a + d.r * ia)) + 0.5f);
                cur.g = (u8)(fmin(255.0f, fmax(0.0f, c.g * a + d.g * ia)) + 0.5f);
                cur.b = (u8)(fmin(255.0f, fmax(0.0f, c.b * a + d.b * ia)) + 0.5f);
                cur.a = 255;
                if (shl->blend == 4) curz = z;
            } else if (shl->blend == 2) {
                CrRgbaM d = cur;
                cur.r = (u8)(fmin(255.0f, (2.0f * c.r * d.r) * (1.0f / 255.0f)) + 0.5f);
                cur.g = (u8)(fmin(255.0f, (2.0f * c.g * d.g) * (1.0f / 255.0f)) + 0.5f);
                cur.b = (u8)(fmin(255.0f, (2.0f * c.b * d.b) * (1.0f / 255.0f)) + 0.5f);
                cur.a = 255;
            } else if (shl->blend == 3) {
                CrRgbaM d = cur;
                float a = c.a * (1.0f / 255.0f);
                cur.r = (u8)(fmin(255.0f, c.r * a + (float)d.r) + 0.5f);
                cur.g = (u8)(fmin(255.0f, c.g * a + (float)d.g) + 0.5f);
                cur.b = (u8)(fmin(255.0f, c.b * a + (float)d.b) + 0.5f);
                cur.a = 255;
            } else {
                cur = c;
                curz = z;
            }
        }  /* end for k (compacted triangles overlapping this tile) */

        /* Refresh the hi-z bound with the depths this batch just wrote. The
         * barrier below is also the "slist reads done" fence for the next
         * batch (sred and slist are distinct arrays). */
        sred[lane] = valid ? curz : -3.402823466e38f;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int off = CR_TILE_N / 2; off > 0; off >>= 1) {
            if (lane < off) sred[lane] = fmax(sred[lane], sred[lane + off]);
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        tile_maxz = sred[0];
    }  /* end for base (batch) */

    if (valid) { color[idx] = cur; depth[idx] = curz; }
}

/* ==================== sky pass (game/sky.c port) ==========================
 * The frame ctx (all celestial trig) is built on the HOST by the cc-compiled
 * sky.o and passed in; the per-ray shader below is IEEE-exact arithmetic
 * EXCEPT hash21's sin (night stars) and the eye-in-fluid exp - both use
 * metal::precise variants, the same divergence class CUDA has (measured
 * <= 0.012% of pixels on night frames there; day frames bit-identical).
 * MEASURED-ON-MAC: re-measure the night-star and underwater divergence rate
 * vs the Mac CPU build; day frames must be bit-identical. */

typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, w; } V4;

static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline float v3dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline V3 v3norm(V3 a) {
    float l = precise::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
    if (l <= 1e-12f) return v3(0.0f, 1.0f, 0.0f);
    float inv = 1.0f / l; return v3(a.x*inv, a.y*inv, a.z*inv);
}
static inline V3 v3mix(V3 a, V3 b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
static inline float sky_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float sky_clamp01(float x) { return sky_clampf(x, 0.0f, 1.0f); }
static inline float smoothstepf(float a, float b, float x) {
    if (a == b) return x < a ? 0.0f : 1.0f;
    float t = sky_clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
/* 2D hash -> [0,1). precise::sin is NOT bit-identical to glibc sinf (same
 * caveat as CUDA device sinf; isolated night-star dots only). */
static inline float hash21(float x, float y) {
    float s = precise::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - floor(s);
}

/* Nearest-neighbour sample of a host-uploaded RGBA byte array (sun/moon). */
static inline V4 tex_sample(device const u8 *rgba, int w, int h, float u, float v) {
    int ix = (int)(u * (float)w); if (ix < 0) ix = 0; if (ix >= w) ix = w - 1;
    int iy = (int)(v * (float)h); if (iy < 0) iy = 0; if (iy >= h) iy = h - 1;
    device const u8 *p = rgba + ((ulong)iy * (ulong)w + (ulong)ix) * 4;
    V4 c; c.x = p[0] / 255.0f; c.y = p[1] / 255.0f; c.z = p[2] / 255.0f;
    c.w = p[3] / 255.0f;
    return c;
}

#define SKY_TILE 64.0f
#define SKY_RENDER_DISTANCE_CHUNKS 8.0f
#define SKY_FOG_END (SKY_RENDER_DISTANCE_CHUNKS * 16.0f)
#define CR_SUN_W  32
#define CR_SUN_H  32
#define CR_MOON_W 32
#define CR_MOON_H 32

static inline V3 mc_sky_corner_fog(V3 vertex_color, V3 fog_color,
                                   float cx, float plane_y, float cz) {
    float dist = precise::sqrt(cx * cx + plane_y * plane_y + cz * cz);
    float fog_factor = sky_clamp01((SKY_FOG_END - dist) / SKY_FOG_END);
    return v3mix(fog_color, vertex_color, fog_factor);
}
static inline V3 mc_sky_plane_fog(V3 vertex_color, V3 fog_color,
                                  V3 dir, float plane_y) {
    float dir_y = dir.y;
    if ((plane_y > 0.0f && dir_y <= 0.0f) || (plane_y < 0.0f && dir_y >= 0.0f)) {
        return fog_color;
    }
    float t = plane_y / dir_y;
    if (t <= 0.0f) return fog_color;
    float px = dir.x * t;
    float pz = dir.z * t;
    float tx0 = floor(px / SKY_TILE) * SKY_TILE;
    float tz0 = floor(pz / SKY_TILE) * SKY_TILE;
    float fx = (px - tx0) / SKY_TILE;
    float fz = (pz - tz0) / SKY_TILE;
    V3 c00 = mc_sky_corner_fog(vertex_color, fog_color, tx0, plane_y, tz0);
    V3 c10 = mc_sky_corner_fog(vertex_color, fog_color, tx0 + SKY_TILE, plane_y, tz0);
    V3 c01 = mc_sky_corner_fog(vertex_color, fog_color, tx0, plane_y, tz0 + SKY_TILE);
    V3 c11 = mc_sky_corner_fog(vertex_color, fog_color, tx0 + SKY_TILE, plane_y,
                               tz0 + SKY_TILE);
    V3 c0 = v3mix(c00, c10, fx);
    V3 c1 = v3mix(c01, c11, fx);
    return v3mix(c0, c1, fz);
}

/* gm_sky_ray_color_ctx (game/sky.c), with the __managed__ sun/moon arrays
 * threaded through as buffer pointers (same texels, host-uploaded once). */
static CrRgbaM gm_sky_ray_color_ctx(thread const GmSkyCtxM *sc, V3 dir_in,
                                    device const u8 *sun,
                                    device const u8 *moon) {
    V3 dir = v3norm(dir_in);
    float ey = dir.y;                          /* sin(elevation) */

    if (sc->uw) {
        V3 col = v3(sc->uw_fog.x, sc->uw_fog.y, sc->uw_fog.z);
        if (ey > 1e-4f) {
            float t = sc->plane_y / ey;
            /* CUDA compiles game/sky.c's expf to device expf here; precise::exp
             * is the same divergence class (not bit-glibc). MEASURED-ON-MAC. */
            float f = precise::exp(-sc->uw_density * t);
            col = v3mix(v3(sc->uw_fog.x, sc->uw_fog.y, sc->uw_fog.z),
                        v3(sc->sky_top.x, sc->sky_top.y, sc->sky_top.z), f);
        }
        CrRgbaM out;
        out.r = (u8)(sky_clamp01(col.x) * 255.0f + 0.5f);
        out.g = (u8)(sky_clamp01(col.y) * 255.0f + 0.5f);
        out.b = (u8)(sky_clamp01(col.z) * 255.0f + 0.5f);
        out.a = 255;
        return out;
    }

    V3 sky_top = v3(sc->sky_top.x, sc->sky_top.y, sc->sky_top.z);
    V3 fog = v3(sc->fog.x, sc->fog.y, sc->fog.z);

    /* vertical gradient: GL linear fog on MC's 64-tile sky plane (Gouraud). */
    V3 col = (ey >= 0.0f) ? mc_sky_plane_fog(sky_top, fog, dir, sc->plane_y)
                          : fog;

    /* sunrise / sunset horizon glow, centered on the sun's azimuth. */
    if (sc->sunset_active) {
        float az = sky_clamp01(v3dot(v3(dir.x, 0.0f, dir.z),
                                     v3(sc->sun_h.x, sc->sun_h.y, sc->sun_h.z)));
        float aey = ey < 0.0f ? -ey : ey;
        float low = 1.0f - smoothstepf(0.0f, 0.35f, aey);
        float w = sc->sunset[3] * low * az * az * (ey > -0.15f ? 1.0f : 0.0f);
        V3 glow = v3(sc->sunset[0], sc->sunset[1], sc->sunset[2]);
        col = v3mix(col, glow, sky_clamp01(w));
    }

    /* night stars (before the sun/moon so discs sit on top). */
    float starB = sc->starB;
    if (starB > 0.001f && ey > 0.02f) {
        float u = (dir.x / (ey + 0.25f)) * 26.0f, v = (dir.z / (ey + 0.25f)) * 26.0f;
        float gx = floor(u), gy = floor(v);
        float h = hash21(gx, gy);
        if (h > 0.985f) {
            float px = hash21(gx + 1.3f, gy), py = hash21(gx, gy + 2.7f);
            float dx = (u - gx) - px, dy = (v - gy) - py;
            float d2 = dx * dx + dy * dy;
            float pt = 1.0f - smoothstepf(0.0f, 0.020f, d2);
            float tw = 0.5f + 0.5f * hash21(gy, gx);
            float s = starB * tw * pt * smoothstepf(0.02f, 0.15f, ey);
            col = v3mix(col, v3(1.0f, 1.0f, 1.0f), sky_clamp01(s));
        }
    }

    /* sun: the REAL sun.png on MC's celestial quad, ADDITIVE. */
    {
        float cA = sc->cA, sA = sc->sA;
        V3 csun = v3(-sA, cA, 0.0f);
        float denom = v3dot(dir, csun);
        if (denom > 1e-4f) {
            float t = 100.0f / denom;
            V3 P = v3(dir.x * t, dir.y * t, dir.z * t);
            float lx = P.z;
            float lz = -cA * P.x - sA * P.y;
            if (lx >= -30.0f && lx <= 30.0f && lz >= -30.0f && lz <= 30.0f) {
                float tu = (lx + 30.0f) / 60.0f, tv = (lz + 30.0f) / 60.0f;
                V4 s = tex_sample(sun, CR_SUN_W, CR_SUN_H, tu, tv);
                col = v3(col.x + s.x, col.y + s.y, col.z + s.z);
            }
        }
    }

    /* moon: the full-moon cell on the opposite quad. Additive, same basis. */
    {
        float cA = sc->cA, sA = sc->sA;
        V3 cmoon = v3(sA, -cA, 0.0f);
        float denom = v3dot(dir, cmoon);
        if (denom > 1e-4f) {
            float t = 100.0f / denom;
            V3 P = v3(dir.x * t, dir.y * t, dir.z * t);
            float lx = P.z;
            float lz = -cA * P.x - sA * P.y;
            if (lx >= -20.0f && lx <= 20.0f && lz >= -20.0f && lz <= 20.0f) {
                float tu = (20.0f - lx) / 40.0f;
                float tv = (lz + 20.0f) / 40.0f;
                V4 m = tex_sample(moon, CR_MOON_W, CR_MOON_H, tu, tv);
                col = v3(col.x + m.x, col.y + m.y, col.z + m.z);
            }
        }
    }

    CrRgbaM out;
    out.r = (u8)(sky_clamp01(col.x) * 255.0f + 0.5f);
    out.g = (u8)(sky_clamp01(col.y) * 255.0f + 0.5f);
    out.b = (u8)(sky_clamp01(col.z) * 255.0f + 0.5f);
    out.a = 255;
    return out;
}

kernel void k_sky(device CrRgbaM *color   [[buffer(0)]],
                  constant SkyParamsM &p  [[buffer(1)]],
                  device const u8 *sun    [[buffer(2)]],
                  device const u8 *moon   [[buffer(3)]],
                  uint2 tpig [[thread_position_in_grid]]) {
    int px = (int)tpig.x;
    int py = (int)tpig.y;
    int W = p.W, H = p.H;
    if (px >= W || py >= H) return;
    GmSkyCtxM sc = p.sc;
    float Fx = p.b[0], Fy = p.b[1], Fz = p.b[2];
    float Rx = p.b[3], Ry = p.b[4], Rz = p.b[5];
    float Ux = p.b[6], Uy = p.b[7], Uz = p.b[8];
    float tanH = p.b[9], aspect = p.b[10];
    float ndc_y = 1.0f - 2.0f * ((float)py + 0.5f) / (float)H;
    float sv = ndc_y * tanH;
    float ndc_x = 2.0f * ((float)px + 0.5f) / (float)W - 1.0f;
    float su = ndc_x * tanH * aspect;
    V3 dir = v3(su * Rx + sv * Ux + Fx,
                su * Ry + sv * Uy + Fy,
                su * Rz + sv * Uz + Fz);
    color[py * W + px] = gm_sky_ray_color_ctx(&sc, dir, sun, moon);
}

/* ==================== transform (core/math.c + transform.c port) ==========
 * The MVP is built on the HOST (public cr_perspective/cr_camera_view/
 * cr_mat4_mul from core/math.o, compiled -ffp-contract=off) and passed in,
 * exactly like the CUDA path builds it with its host _dev copies. Only
 * cr_mat4_mul_vec4 and the transform.c chain run on-device. */

static inline V4 cr_mat4_mul_vec4(thread const CrMat4M &mm, V4 v) {
    V4 r;
    r.x = mm.m[0] * v.x + mm.m[4] * v.y + mm.m[8]  * v.z + mm.m[12] * v.w;
    r.y = mm.m[1] * v.x + mm.m[5] * v.y + mm.m[9]  * v.z + mm.m[13] * v.w;
    r.z = mm.m[2] * v.x + mm.m[6] * v.y + mm.m[10] * v.z + mm.m[14] * v.w;
    r.w = mm.m[3] * v.x + mm.m[7] * v.y + mm.m[11] * v.z + mm.m[15] * v.w;
    return r;
}

/* transform.c ClipVert (kernel-internal working type, no host ABI). */
typedef struct {
    V4    clip;
    CrVec2M uv;
    float light;
    float ao;
    float eye_dist;
    float tint[4];
    float blk;
} ClipVertM;

static ClipVertM make_clipvert(thread const CrMat4M &mvp,
                               thread const CrVertexM *v, V3 campos) {
    ClipVertM cv;
    V4 p; p.x = v->pos.x; p.y = v->pos.y; p.z = v->pos.z; p.w = 1.0f;
    cv.clip = cr_mat4_mul_vec4(mvp, p);
    cv.uv = v->uv;
    cv.light = v->light;
    cv.ao = v->ao;
    cv.blk = v->blk;
    float dx = v->pos.x - campos.x;
    float dy = v->pos.y - campos.y;
    float dz = v->pos.z - campos.z;
    cv.eye_dist = precise::sqrt(dx * dx + dy * dy + dz * dz);
    cv.tint[0] = (float)v->tint.r;
    cv.tint[1] = (float)v->tint.g;
    cv.tint[2] = (float)v->tint.b;
    cv.tint[3] = (float)v->tint.a;
    return cv;
}

static inline float near_dist(thread const ClipVertM *v) {
    return v->clip.z + v->clip.w;
}

static ClipVertM lerp_clipvert(thread const ClipVertM *a,
                               thread const ClipVertM *b, float t) {
    ClipVertM r;
    r.clip.x = a->clip.x + t * (b->clip.x - a->clip.x);
    r.clip.y = a->clip.y + t * (b->clip.y - a->clip.y);
    r.clip.z = a->clip.z + t * (b->clip.z - a->clip.z);
    r.clip.w = a->clip.w + t * (b->clip.w - a->clip.w);
    r.uv.x = a->uv.x + t * (b->uv.x - a->uv.x);
    r.uv.y = a->uv.y + t * (b->uv.y - a->uv.y);
    r.light = a->light + t * (b->light - a->light);
    r.ao = a->ao + t * (b->ao - a->ao);
    r.blk = a->blk + t * (b->blk - a->blk);
    r.eye_dist = a->eye_dist + t * (b->eye_dist - a->eye_dist);
    for (int i = 0; i < 4; ++i)
        r.tint[i] = a->tint[i] + t * (b->tint[i] - a->tint[i]);
    return r;
}

static int clip_near(thread const ClipVertM in3[3], thread ClipVertM out4[4]) {
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        thread const ClipVertM *cur = &in3[i];
        thread const ClipVertM *nxt = &in3[(i + 1) % 3];
        float dc = near_dist(cur);
        float dn = near_dist(nxt);
        int cur_in = dc >= 0.0f;
        int nxt_in = dn >= 0.0f;
        if (cur_in)
            out4[n++] = *cur;
        if (cur_in != nxt_in) {
            float t = dc / (dc - dn);
            out4[n++] = lerp_clipvert(cur, nxt, t);
        }
    }
    return n;
}

static CrScreenVertM to_screen(thread const ClipVertM *cv, int fb_w, int fb_h) {
    CrScreenVertM sv;
    float invw = 1.0f / cv->clip.w;
    float ndc_x = cv->clip.x * invw;
    float ndc_y = cv->clip.y * invw;
    float ndc_z = cv->clip.z * invw;

    sv.spos.x = (ndc_x * 0.5f + 0.5f) * (float)fb_w;
    sv.spos.y = (0.5f - ndc_y * 0.5f) * (float)fb_h; /* y flipped: y=0 at top */
    sv.spos.z = ndc_z * 0.5f + 0.5f;                 /* [-1,1] -> [0,1] */

    sv.invw = invw;
    sv.uv_w.x = cv->uv.x * invw;
    sv.uv_w.y = cv->uv.y * invw;
    sv.light_w = cv->light * invw;
    sv.ao_w = cv->ao * invw;
    sv.blk_w = cv->blk * invw;
    sv.eye_dist_w = cv->eye_dist * invw;
    sv.tint_r_w = cv->tint[0] * invw;
    sv.tint_g_w = cv->tint[1] * invw;
    sv.tint_b_w = cv->tint[2] * invw;
    sv.tint_a_w = cv->tint[3] * invw;
    return sv;
}

/* With the y-down viewport map above, front faces have negative signed area.
 * Keep exact degenerates in the transform output so the rasterizer retains
 * its existing zero-area handling. */
#define CR_BACKFACE_EPSILON 0.0f
static int cr_screen_backface(thread const CrScreenTriM *tri) {
    float x0 = tri->v[0].spos.x, y0 = tri->v[0].spos.y;
    float x1 = tri->v[1].spos.x, y1 = tri->v[1].spos.y;
    float x2 = tri->v[2].spos.x, y2 = tri->v[2].spos.y;
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    return area > CR_BACKFACE_EPSILON;
}

static int cr_transform_tri(thread const CrMat4M &mvp, V3 campos,
                            thread const CrVertexM v3in[3],
                            int fb_w, int fb_h, thread CrScreenTriM out2[2]) {
    ClipVertM tri[3];
    for (int k = 0; k < 3; ++k)
        tri[k] = make_clipvert(mvp, &v3in[k], campos);

    ClipVertM poly[4];
    int np = clip_near(tri, poly);
    if (np < 3)
        return 0; /* fully behind near plane */

    int n = 0;
    for (int i = 1; i + 1 < np && n < 2; ++i) {
        CrScreenTriM st;
        st.v[0] = to_screen(&poly[0], fb_w, fb_h);
        st.v[1] = to_screen(&poly[i], fb_w, fb_h);
        st.v[2] = to_screen(&poly[i + 1], fb_w, fb_h);
        out2[n++] = st;
    }
    return n;
}

/* One thread per input triangle emits 0..2 clipped front faces. Single-layer
 * draws compact stably within each 256-input threadgroup; merged terrain keeps
 * fixed 2t/2t+1 slots so its existing shade boundaries remain valid. */
kernel void cr_transform_kernel(device const CrVertexM *verts [[buffer(0)]],
                                constant XformParamsM &p      [[buffer(1)]],
                                device CrScreenTriM *outb     [[buffer(2)]],
                                device int *block_counts       [[buffer(3)]],
                                device atomic_int *frame_count [[buffer(4)]],
                                uint tpig [[thread_position_in_grid]],
                                uint tpitg [[thread_position_in_threadgroup]],
                                uint tgpos [[threadgroup_position_in_grid]]) {
    int t = (int)tpig;
    CrVertexM vin[3];
    CrScreenTriM pair[2];
    int n = 0;
    if (t < p.ntris_in) {
        vin[0] = verts[t * 3];
        vin[1] = verts[t * 3 + 1];
        vin[2] = verts[t * 3 + 2];
        CrMat4M mvp = p.mvp;
        V3 campos = v3(p.campos.x, p.campos.y, p.campos.z);
        n = cr_transform_tri(mvp, campos, vin, p.W, p.H, pair);
    }
    int kept = 0;
    for (int i = 0; i < n; ++i) {
        if (!cr_screen_backface(&pair[i]))
            pair[kept++] = pair[i];
    }
    n = kept;
    CrScreenTriM zero = {};          /* all-zero bytes, like CUDA's memset */
    if (!p.compact) {
        if (t < p.ntris_in) {
            outb[2 * t]     = (n > 0) ? pair[0] : zero;
            outb[2 * t + 1] = (n > 1) ? pair[1] : zero;
        }
        return;
    }

    threadgroup int scan[256];
    int lane = (int)tpitg;
    if (t < p.ntris_in) {
        outb[2 * t] = zero;
        outb[2 * t + 1] = zero;
    }
    scan[lane] = n;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int off = 1; off < 256; off <<= 1) {
        int v = lane >= off ? scan[lane - off] : 0;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        scan[lane] += v;
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    int excl = scan[lane] - n;
    int total = scan[255];
    if (lane == 255) {
        block_counts[tgpos] = total;
        atomic_fetch_add_explicit(frame_count, total, memory_order_relaxed);
    }
    int base = (int)tgpos * 512 + excl;
    if (n > 0) outb[base] = pair[0];
    if (n > 1) outb[base + 1] = pair[1];
}

/* Serial exclusive scan over transform-block live counts. nxblocks is at most
 * ~7820 at the configured cap, so one device thread is the simplest identical
 * CUDA/MSL implementation. screen_tris[0] remains the frame accumulator from
 * cr_transform_kernel; [1] is overwritten with this layer's dense count. */
kernel void cr_compact_scan_kernel(device const int *block_counts [[buffer(0)]],
                                   constant int &nxblocks          [[buffer(1)]],
                                   device int *block_offsets       [[buffer(2)]],
                                   device int *screen_tris         [[buffer(3)]],
                                   uint tpig [[thread_position_in_grid]]) {
    if (tpig != 0) return;
    int total = 0;
    for (int xb = 0; xb < nxblocks; ++xb) {
        block_offsets[xb] = total;
        total += block_counts[xb];
    }
    screen_tris[1] = total;
}

/* Stable global scatter: block-major order plus each block's stable live
 * prefix is exactly the CPU transform output order. */
kernel void cr_compact_scatter_kernel(
        device const CrScreenTriM *slotted [[buffer(0)]],
        device const int *block_counts     [[buffer(1)]],
        device const int *block_offsets    [[buffer(2)]],
        constant int &nxblocks             [[buffer(3)]],
        device CrScreenTriM *dense         [[buffer(4)]],
        uint tpig [[thread_position_in_grid]]) {
    int t = (int)tpig;
    int xb = t / 512;
    int i = t % 512;
    if (xb >= nxblocks || i >= block_counts[xb]) return;
    CrScreenTriM tri = slotted[xb * 512 + i];
    dense[block_offsets[xb] + i] = tri;
}

/* ==================== slab-pool gather ====================================
 * Concatenate gather entries (u32 words) from the slab pool into d_verts:
 * thread w binary-searches the word-prefix table for its entry. `base` offsets
 * into the gsrc/gpfx rings (CUDA passed pre-offset pointers). */
kernel void cr_gather_kernel(device u32 *dst            [[buffer(0)]],
                             device const u32 *slabs    [[buffer(1)]],
                             device const int *src_word_ring [[buffer(2)]],
                             device const int *pfx_ring [[buffer(3)]],
                             constant GatherParamsM &p  [[buffer(4)]],
                             uint tpig [[thread_position_in_grid]]) {
    int w = (int)tpig;
    if (w >= p.total_words) return;
    device const int *src_word = src_word_ring + p.base;
    device const int *pfx      = pfx_ring + p.base;
    int lo = 0, hi = p.nents - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (pfx[mid] <= w) lo = mid; else hi = mid - 1;
    }
    dst[w] = slabs[src_word[lo] + (w - pfx[lo])];
}
