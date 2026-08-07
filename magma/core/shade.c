/* magma - shared per-fragment shader (core/shade.c).
 *
 * Owner: SHADE agent. Implements cr_atlas_sample() and cr_shade() from
 * core/types.h. Both are CR_HD (host + device) so the CPU rasterizer and the
 * CUDA rasterizer call byte-identical code. The .cu backend #includes this file
 * (see cuda/raster_cuda.cu) so device code is the very same source.
 *
 * Numerical contract: only fminf/fmaxf/floorf from libm, no fused-multiply-add
 * (build host with -ffp-contract=off, device with --fmad=false) and a single,
 * fixed order of float operations, so CPU and CUDA agree bit-for-bit.
 */
#include "core/types.h"
#include <math.h>
#if !defined(__CUDA_ARCH__)
#include "core/config.h"   /* texel_bias / sample_mode / flat_shade registry knobs */
#include <stdlib.h>
#endif

/* Per-call sample mode from CrShadeCtx.sample_mode (0 = registry/default path).
 * cr_atlas_sample is shared with CUDA and has no shade ctx arg, so cr_shade
 * stashes the mode here for the duration of one fragment. Host-only override
 * sample_mode still applies when this is 0. */
#if defined(__CUDA_ARCH__)
static __device__ int g_cr_sample_mode_override = 0;
#else
static int g_cr_sample_mode_override = 0;
#endif

/* Nearest-neighbour atlas fetch with clamp-to-edge. u,v in [0,1] over the whole
 * atlas (level 0 only). */
CR_HD CrRgba cr_atlas_sample(const CrTexture *tex, float u, float v) {
    CrRgba out;
    if (!tex || !tex->texels || tex->w <= 0 || tex->h <= 0) {
        out.r = 0; out.g = 0; out.b = 0; out.a = 255;
        return out;
    }
    /* Pull 1e-4 so exact high-edge UV (sprite x1/W) does not floor onto neighbor.
     * Optional texel_bias{,_u,_v} (host-only registry) shifts nearest phase in
     * texel units. Hard-scene leaf black-hole residual is sensitive to V phase. */
    float bu = 0.0f, bv = 0.0f;
#if !defined(__CUDA_ARCH__)
    {
        static int init = 0;
        static float sbu = 0.0f, sbv = 0.0f;
        if (!init) {
            const CrConfig *k = cr_cfg();
            if (k->texel_bias[0]) sbu = sbv = (float)atof(k->texel_bias);
            if (k->texel_bias_u[0]) sbu = (float)atof(k->texel_bias_u);
            if (k->texel_bias_v[0]) sbv = (float)atof(k->texel_bias_v);
            init = 1;
        }
        bu = sbu; bv = sbv;
    }
#endif
    float fu = u * (float)tex->w + bu;
    float fv = v * (float)tex->h + bv;
    int mode = g_cr_sample_mode_override;
#if !defined(__CUDA_ARCH__)
    if (mode == 0) {
        static int sm = -2;
        if (sm == -2) {
            const char *s = cr_cfg()->sample_mode;
            sm = s[0] ? atoi(s) : 0;
        }
        mode = sm;
    }
#endif
    int ix, iy;
    /* 0: floor(u*w - 1e-4) default; 1: floor; 2: round; 3: floor+1e-4 */
    if (mode == 1) { ix = (int)floorf(fu); iy = (int)floorf(fv); }
    else if (mode == 2) { ix = (int)floorf(fu + 0.5f); iy = (int)floorf(fv + 0.5f); }
    else if (mode == 3) { ix = (int)floorf(fu + 1e-4f); iy = (int)floorf(fv + 1e-4f); }
    else { ix = (int)floorf(fu - 1e-4f); iy = (int)floorf(fv - 1e-4f); }
    if (ix < 0) ix = 0; else if (ix >= tex->w) ix = tex->w - 1;
    if (iy < 0) iy = 0; else if (iy >= tex->h) iy = tex->h - 1;
    return tex->texels[iy * tex->w + ix];
}

/* Mip-aware nearest fetch (GL_NEAREST_MIPMAP_NEAREST): pick one level from the
 * chain by rounding `lod` (already includes any mip_bias) and nearest-sample it.
 * Level 0 == tex->texels; level L (L>=1) == tex->mip[L-1] with mipw/miph dims.
 * Uses only floorf so CPU and CUDA agree bit-for-bit. */
CR_HD static CrRgba cr_atlas_sample_lod(const CrTexture *tex, float u, float v,
                                        float lod) {
    CrRgba out;
    if (!tex || !tex->texels || tex->w <= 0 || tex->h <= 0) {
        out.r = 0; out.g = 0; out.b = 0; out.a = 255;
        return out;
    }
    int L = (int)floorf(lod + 0.5f);          /* nearest level (round) */
    if (L < 0) L = 0;
    if (L > tex->mip_levels) L = tex->mip_levels;

    const CrRgba *tx; int tw, th;
    if (L == 0) { tx = tex->texels; tw = tex->w; th = tex->h; }
    else        { tx = tex->mip[L - 1]; tw = tex->mipw[L - 1]; th = tex->miph[L - 1]; }
    if (!tx || tw <= 0 || th <= 0) { tx = tex->texels; tw = tex->w; th = tex->h; }

    int ix = (int)floorf(u * (float)tw);
    int iy = (int)floorf(v * (float)th);
    if (ix < 0) ix = 0; else if (ix >= tw) ix = tw - 1;
    if (iy < 0) iy = 0; else if (iy >= th) iy = th - 1;
    return tx[iy * tw + ix];
}

/* Deterministic e^-x for x >= 0 (fog exponent). libm expf differs between
 * glibc and CUDA device code in the last ulps, which would break the CPU==CUDA
 * bit contract, so build it from mul/add/floorf only in one fixed order:
 * e^-x = 2^-t with t = x*log2(e); split t into integer i and fraction f in
 * [0,1), evaluate 2^-f by a degree-5 Horner polynomial (max rel err ~3e-7,
 * far below the 1/255 output quantum), and scale by 2^-i exactly. */
static CR_HD float cr_exp_neg(float x) {
    if (x <= 0.0f) return 1.0f;
    float t = x * 1.4426950408889634f;       /* log2(e) */
    if (t >= 127.0f) return 0.0f;            /* underflows the u8 quantum anyway */
    float i = floorf(t);
    float f = t - i;
    /* 2^-f, f in [0,1): minimax-ish poly in u = -f*ln2 via exp series (Horner,
     * no fma; coefficients are the Taylor series of e^u with u in (-ln2, 0],
     * degree 5: rel err < 4e-7 on the interval). */
    float u = f * -0.6931471805599453f;      /* -ln2 */
    float p = 1.0f + u * (1.0f + u * (0.5f + u * (0.16666666666666666f
                  + u * (0.041666666666666664f + u * 0.008333333333333333f))));
    /* scale by 2^-i: i is a small positive integer (< 127); build the power
     * exactly by repeated squaring on the float exponent via a bit-free loop.
     * i < 32 in practice (density*dist <= ~64); a simple loop is exact. */
    int n = (int)i;
    float s = 1.0f;
    while (n >= 8) { s = s * 0.00390625f; n -= 8; }  /* 2^-8 exact */
    while (n > 0)  { s = s * 0.5f; --n; }            /* 2^-1 exact */
    return p * s;
}

/* Per-fragment colour:
 *   texel = sample(uv)
 *   if alpha_test and texel.a < 128 -> return alpha 0 (raster must skip write)
 *   color = texel * tint * light * ao   (per channel, /255 float math)
 *   then, if enable_fog, lerp toward fog_color by
 *       clamp((eye_dist - fog_start) / (fog_end - fog_start), 0, 1)
 * Returned alpha carries texel*tint alpha (never fogged); a kept fragment never
 * returns alpha 0 (0 is reserved as the alpha_test discard signal). */
CR_HD CrRgba cr_shade(const CrShadeCtx *sh, const CrFragment *frag) {
    CrRgba out;
    const float inv255 = 1.0f / 255.0f;

    /* RenderDragon death dissolve: light < 0 marks dissolve fragments; ao holds
     * deathTicks/200. Sample dragon_exploding at uv+mask_off (GL_GREATER thr). */
    if (sh->alpha_mask && frag->light < 0.0f && sh->atlas) {
        float mu = frag->uv.x + sh->mask_u_off;
        float mv = frag->uv.y + sh->mask_v_off;
        CrRgba mask = cr_atlas_sample(sh->atlas, mu, mv);
        if ((float)mask.a * inv255 <= frag->ao) {
            out.r = 0; out.g = 0; out.b = 0; out.a = 0;
            return out;
        }
    }

    /* SOLID keeps level-0 nearest (byte-identical to the pre-layer path); mipped
     * layers select a level from the chain via the fragment LOD + bias.
     * untextured: opaque white (POSITION_COLOR / death rays). */
    CrRgba texel;
    if (sh->untextured) {
        texel.r = 255; texel.g = 255; texel.b = 255; texel.a = 255;
    } else {
        float su = frag->uv.x, sv = frag->uv.y;
        if (sh->repeat_uv) {
            su -= floorf(su);
            sv -= floorf(sv);
        }
        g_cr_sample_mode_override = sh->sample_mode;
        texel = sh->use_mips
            ? cr_atlas_sample_lod(sh->atlas, su, sv,
                                  frag->lod + sh->mip_bias)
            : cr_atlas_sample(sh->atlas, su, sv);
        g_cr_sample_mode_override = 0;
    }

    /* Alpha test: explicit flag, or the CUTOUT layers. Default ref 0.5
     * (texel.a < 128, GL_GREATER 0.5). Living entities use alpha_ref=0.1
     * (discard a/255 <= 0.1 i.e. a <= 25). Skip when dissolve already
     * applied the exploding-mask gate for this fragment. */
    int alpha_test = sh->alpha_test
        || sh->layer == CR_LAYER_CUTOUT
        || sh->layer == CR_LAYER_CUTOUT_MIPPED;
    if (sh->alpha_mask && frag->light < 0.0f) alpha_test = 0;
#if !defined(__CUDA_ARCH__)
    /* Opt-in: alpha-test SOLID too (experiment; Java Fast SOLID has alpha DISABLED). */
    if (!alpha_test && sh->layer == CR_LAYER_SOLID) {
        static int sa = -1;
        if (sa < 0) {
            const char *s = getenv("MAGMA_SOLID_ALPHA");
            sa = (s && atoi(s) != 0) ? 1 : 0;
        }
        if (sa) alpha_test = 1;
    }
#endif
    if (alpha_test) {
        float ref = sh->alpha_ref > 0.0f ? sh->alpha_ref : 0.5f;
        /* GL_GREATER: discard when a/255 <= ref. Byte form for 0.5 stays a<128. */
        int thr = (int)(ref * 255.0f + 1e-5f); /* floor(ref*255); 0.5 -> 127 */
        if ((int)texel.a <= thr) {
            out.r = 0; out.g = 0; out.b = 0; out.a = 0; /* discard */
            return out;
        }
    }

    /* Lightmap-coord mode: fragment light/blk are 0..15 lightmap levels; sample
     * the frame's 16x16 lightmap texture with GL_LINEAR semantics (bilerp of
     * the 8-bit texels; the GL coord (level*16+8)/256 puts the sample point at
     * texel-space == level exactly). Legacy mode (lightmap NULL): lm stays 1
     * and frag->light is the prefolded 0..1 scalar (multiplying by 1.0f is an
     * exact IEEE identity, so the legacy path is bit-unchanged). */
    float lmr = 1.0f, lmg = 1.0f, lmb = 1.0f;
    float brightness_mix = 0.0f;
    float frag_blk = frag->blk;
    if (sh->entity_brightness && frag_blk < 0.0f) {
        /* gm_entities_emit packs an integer block-light level and a [0,1]
         * white-combiner mix into one otherwise non-negative attribute. */
        float packed = -frag_blk;
        float whole = floorf(packed);
        frag_blk = whole - 1.0f;
        brightness_mix = (packed - whole) * 32.0f;
        brightness_mix = fmaxf(0.0f, fminf(1.0f, brightness_mix));
    }
    /* Dissolve fragments encode the death threshold in ao and mark light < 0;
     * shade as fullbright (End sky) so the mask gate is the only visibility. */
    float lscalar = (frag->light < 0.0f) ? 1.0f : frag->light;
    float ao_mul = (sh->alpha_mask && frag->light < 0.0f) ? 1.0f : frag->ao;
    if (sh->lightmap && frag->light >= 0.0f) {
        float s = fmaxf(0.0f, fminf(15.0f, frag->light));
        float b = fmaxf(0.0f, fminf(15.0f, frag_blk));
        int s0 = (int)floorf(s), b0 = (int)floorf(b);
        int s1 = s0 < 15 ? s0 + 1 : 15, b1 = b0 < 15 ? b0 + 1 : 15;
        float fs = s - (float)s0, fb = b - (float)b0;
        const CrRgba *T = sh->lightmap;
        CrRgba t00 = T[s0 * 16 + b0], t01 = T[s0 * 16 + b1];
        CrRgba t10 = T[s1 * 16 + b0], t11 = T[s1 * 16 + b1];
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
    float tr = frag->tint.r * inv255, tg = frag->tint.g * inv255, tb = frag->tint.b * inv255;
#if !defined(__CUDA_ARCH__)
    {
        static int flat = -1;
        if (flat < 0) flat = cr_cfg()->flat_shade ? 1 : 0;
        if (flat) { la = 1.f; tr = tg = tb = 1.f; lmr = lmg = lmb = 1.f; }
    }
#endif
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
            /* GL_EXP (setupFog fluid branch): factor = exp(-density * c). */
            t = 1.0f - cr_exp_neg(sh->fog_exp_density * frag->eye_dist);
        } else {
            float denom = sh->fog_end - sh->fog_start;
            t = (frag->eye_dist - sh->fog_start) / denom;
        }
        t = fmaxf(0.0f, fminf(1.0f, t));
        float fr = sh->fog_color.r * inv255;
        float fg = sh->fog_color.g * inv255;
        float fb = sh->fog_color.b * inv255;
        cr = cr + (fr - cr) * t;
        cg = cg + (fg - cg) * t;
        cb = cb + (fb - cb) * t;
    }

    /* color_trunc: match fixed-function GL / DynamicTexture (int)(c*255).
     * Default remains round-half-up for terrain/oracle goldens. */
    if (sh->color_trunc) {
        out.r = (u8)(fmaxf(0.0f, fminf(1.0f, cr)) * 255.0f);
        out.g = (u8)(fmaxf(0.0f, fminf(1.0f, cg)) * 255.0f);
        out.b = (u8)(fmaxf(0.0f, fminf(1.0f, cb)) * 255.0f);
    } else {
        out.r = (u8)(fmaxf(0.0f, fminf(1.0f, cr)) * 255.0f + 0.5f);
        out.g = (u8)(fmaxf(0.0f, fminf(1.0f, cg)) * 255.0f + 0.5f);
        out.b = (u8)(fmaxf(0.0f, fminf(1.0f, cb)) * 255.0f + 0.5f);
    }

    if (sh->layer == CR_LAYER_SOLID) {
        /* opaque: force full alpha (never blended, never discarded). */
        out.a = 255;
    } else {
        /* CUTOUT/TRANSLUCENT: carry texel*tint alpha un-premultiplied; the raster
         * uses it for src-over when ctx->blend. Reserve 0 for the discard path. */
        float ca = (texel.a * inv255) * (frag->tint.a * inv255);
        u8 av;
        if (sh->color_trunc)
            av = (u8)(fmaxf(0.0f, fminf(1.0f, ca)) * 255.0f);
        else
            av = (u8)(fmaxf(0.0f, fminf(1.0f, ca)) * 255.0f + 0.5f);
        if (av == 0) av = 1;
        out.a = av;
    }
    return out;
}
