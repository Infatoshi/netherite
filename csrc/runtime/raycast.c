/* The textured ray cast (raycast.h). */
#include "raycast.h"
#include "rchead.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
/* (RC_NO_SIMD: a build as one without x86's lanes, the Mac's, on this host: its bytes held to the lanes', lane/hostf32) */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(RC_NO_SIMD)
#include <immintrin.h>          /* rays_air_body's and rays_row's lanes: AVX2 and AVX-512 by target, run where the CPU has them */
#define RC_AVX2 1
#endif

#include "../engine/blocks.h"
#include "../engine/clientworld.h"
#include "../engine/combat.h"
#include "../engine/item_entity.h"
#include "../engine/jmath.h"
#include "../engine/jrand.h"
#include "../engine/living.h"
#include "../engine/noise.h"
#include "../engine/dragon.h"
#include "../engine/pick.h"
#include "../engine/pickobj.h"
#include "../engine/player.h"
#include "../engine/rbshare.h"
#include "../engine/render_blocks.h"
#include "../engine/renderstate.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/smath.h"
#include "../engine/world.h"

/* The cast's scalar (raycast.h "The two instances"): this file is compiled twice, as itself (RC_FP 64, the reference,
 * every ray's arithmetic in double from the eye's absolute position) and through raycast_f32.c (RC_FP 32, the
 * production cast: float from the eye's offset in its cell on). The per-ray code is one source in rcr, the scalar,
 * with its literals through RC_K (1.0 a double's, 1.0f a float's) and its cells through RC_CI (a cell's index against
 * the frame's origin cell k->b*: 0 in double, so the expressions are today's; the eye's cell in float, so every
 * coordinate is a small whole number less the eye's offset). The FP32 instance's exported names end in _f32; the
 * FP64 instance's entry points hand a spec with fp 32 to them. */
#if !defined(RC_FP)
#define RC_FP 64
#endif
/* RC_FIX (lane/castexact, raycast_fx.c): the fixed instance, FP32's source with the eye's offset and the direction
 * rounded to RC_FIXF and RC_FIXG fractional bits and every decision of the walk's order exact (rc_fx_cmp) */
#if !defined(RC_FIX)
#define RC_FIX 0
#endif
/* RC_FX_ALWAYS 1 (a measurement build, lane/cxfix): the integers decide every finite comparison of the walk's order,
 * not only the pairs within 2^-20; the same bytes as the default is the floats' shortcut's proof */
#if !defined(RC_FX_ALWAYS)
#define RC_FX_ALWAYS 0
#endif
#if RC_FP == 32 && RC_FIX
typedef float rcr;
#define RC_K(x) x##f
#define RC_SQRT sqrtf
#define RC_FABS fabsf
#define RC_FLOOR floorf
#define RC_NEXTAFTER nextafterf
#define raycast_load(...) raycast_load_fx(__VA_ARGS__)
#define raycast(...) raycast_fx(__VA_ARGS__)
#define raycast_ref(...) raycast_ref_fx(__VA_ARGS__)
#define raycast_seen(...) raycast_seen_fx(__VA_ARGS__)
#define raycast_keep_new(...) raycast_keep_new_fx(__VA_ARGS__)
#define raycast_keep_free(...) raycast_keep_free_fx(__VA_ARGS__)
#define raycast_seen_keep(...) raycast_seen_keep_fx(__VA_ARGS__)
#define raycast_head(...) raycast_head_fx(__VA_ARGS__)
#define raycast_keep raycast_keepfx
#elif RC_FP == 32
typedef float rcr;
#define RC_K(x) x##f
#define RC_SQRT sqrtf
#define RC_FABS fabsf
#define RC_FLOOR floorf
#define RC_NEXTAFTER nextafterf
/* (function-like: struct raycast_seen keeps its name) */
#define raycast_load(...) raycast_load_f32(__VA_ARGS__)
#define raycast(...) raycast_f32(__VA_ARGS__)
#define raycast_ref(...) raycast_ref_f32(__VA_ARGS__)
#define raycast_seen(...) raycast_seen_f32(__VA_ARGS__)
#define raycast_keep_new(...) raycast_keep_new_f32(__VA_ARGS__)
#define raycast_keep_free(...) raycast_keep_free_f32(__VA_ARGS__)
#define raycast_seen_keep(...) raycast_seen_keep_f32(__VA_ARGS__)
#define raycast_head(...) raycast_head_f32(__VA_ARGS__)
#define raycast_keep raycast_keep32
#elif RC_FP == 16
/* the FP16 instance (raycast_f16.c, built with each _Float16 operation rounded to half: gcc -fexcess-precision=16):
 * every operation an IEEE half's, rounded to nearest; sqrt, floor and fabs through float (a float's correctly rounded
 * sqrt rounded to half is half's: 24 >= 2 x 11 + 2) */
typedef _Float16 rcr;
#define RC_K(x) ((_Float16)(x))          /* (a double literal rounded to half once, as the device's h16(double)) */
#define RC_SQRT(x) ((_Float16)sqrtf((float)(x)))
#define RC_FABS(x) ((_Float16)fabsf((float)(x)))
#define RC_FLOOR(x) ((_Float16)floorf((float)(x)))
#define RC_NEXTAFTER(x, y) rc_nextafter16(x, y)
#define raycast_load(...) raycast_load_f16(__VA_ARGS__)
#define raycast(...) raycast_f16(__VA_ARGS__)
#define raycast_ref(...) raycast_ref_f16(__VA_ARGS__)
#define raycast_seen(...) raycast_seen_f16(__VA_ARGS__)
#define raycast_keep_new(...) raycast_keep_new_f16(__VA_ARGS__)
#define raycast_keep_free(...) raycast_keep_free_f16(__VA_ARGS__)
#define raycast_seen_keep(...) raycast_seen_keep_f16(__VA_ARGS__)
#define raycast_head(...) raycast_head_f16(__VA_ARGS__)
#define raycast_keep raycast_keep16
/* the next half up from a finite x (the range's rnext) */
static inline _Float16 rc_nextafter16(_Float16 x, double toward)
{
    (void)toward;
    uint16_t b;
    memcpy(&b, &x, 2);
    b = (b & 0x8000u) ? (uint16_t)(b - 1u) : (uint16_t)(b + 1u);
    memcpy(&x, &b, 2);
    return x;
}
#else
typedef double rcr;
#define RC_K(x) x
#define RC_SQRT sqrt
#define RC_FABS fabs
#define RC_FLOOR floor
#define RC_NEXTAFTER nextafter
#endif
/* cell index i on axis a's coordinate in the frame (k->bx, k->by, k->bz: the origin cell) */
#if RC_FP == 16
/* (a cell's whole number through float, exact, then rounded once: gcc takes int to half through double, a libgcc call) */
#define RC_CX(k, i) ((rcr)(float)((i) - (k)->bx))
#define RC_CY(k, i) ((rcr)(float)((i) - (k)->by))
#define RC_CZ(k, i) ((rcr)(float)((i) - (k)->bz))
#else
#define RC_CX(k, i) ((rcr)((i) - (k)->bx))
#define RC_CY(k, i) ((rcr)((i) - (k)->by))
#define RC_CZ(k, i) ((rcr)((i) - (k)->bz))
#endif

/* The fixed instance's walk (RC_FIX, lane/castexact; raycast.h "The instances"): the eye's offset o and the ray's
 * direction d are rounded to RC_FIXF and RC_FIXG fractional bits (rc_fxq, rc_fxqf: whole numbers O = o 2^F and D =
 * d 2^G), and every choice of the walk's order (which axis' boundary comes first, whether a boundary comes before an
 * exit event) is the order of the exact t = (b - o) / d of the two boundaries (b a whole number, the boundary's
 * coordinate in the frame): t_a - t_b has the sign of (P_a D_b - P_b D_a) sign(D_a D_b), P = b 2^F - O, a product of
 * at most 9 + F + G bits (|b| < 256). The floats' own t (FP32's, from these o and d) decide wherever they are more than
 * 2^-20 apart relative (each is at most three float roundings off the exact t), the integers where closer. */
#if RC_FIX
/* (RC_FIXF, RC_FIXG: rchead.h's, the device's too) */
_Static_assert(RC_FIXF <= 24 && RC_FIXG <= 24 && 9 + RC_FIXF + RC_FIXG <= 62, "the products fit 64 bits");
static inline double rc_fxq(double x, int bits)
{
    return nearbyint(x * ldexp(1.0, bits)) * ldexp(1.0, -bits);
}
static inline float rc_fxqf(float x)
{
    return rintf(x * (float)(1 << RC_FIXG)) * (1.0f / (float)(1 << RC_FIXG));
}
/* the sign of t_a - t_b exactly (da, db not 0) */
static inline int rc_fx_exact(int ba, float oa, float da, int bb, float ob, float db)
{
    const int64_t pa = (int64_t)ba * ((int64_t)1 << RC_FIXF) - (int64_t)(oa * (float)(1 << RC_FIXF));
    const int64_t pb = (int64_t)bb * ((int64_t)1 << RC_FIXF) - (int64_t)(ob * (float)(1 << RC_FIXF));
    const int64_t qa = (int64_t)(da * (float)(1 << RC_FIXG)), qb = (int64_t)(db * (float)(1 << RC_FIXG));
    const int64_t x = pa * qb - pb * qa;
    const int s = (x > 0) - (x < 0);
    return (qa > 0) == (qb > 0) ? s : -s;
}
#if defined(RC_FX_COUNT)
static uint64_t rc_fx_n[2];     /* the measurement build: decisions the integers made, and of them the ones float's differed on */
static void rc_fx_print(void)
{
    fprintf(stderr, "rcfx F %d G %d: decisions by the integers %llu, of them the floats' order differed %llu\n", RC_FIXF, RC_FIXG,
            (unsigned long long)rc_fx_n[0], (unsigned long long)rc_fx_n[1]);
}
__attribute__((constructor)) static void rc_fx_atexit(void) { atexit(rc_fx_print); }
#endif
/* Which pairs go to the integers (lane/cxfix; cast.cu's knobs too): any superset of the floats' 2^-20 gives the same
 * bytes, the integers being exact. RC_FXNEAR 0 (the default) the relative test |ta - tb| <= (|ta| + |tb|) 2^-20; 1 the
 * two floats within RC_FXULP units in the last place, their bits ordered as integers (rc_ford: -0 next to +0), which
 * holds every pair the relative test does at 64 or more (2^-19 of the larger is at most 64 ulps of the smaller's
 * binade): an integer subtract and compare in place of two adds, a multiply and two compares. The same bytes; the
 * host cast's cycles -1.6 to +5.5% against 0's at 16 to 128 (within their noise), the device's 0.2 to 1.9% slower
 * (lane/cxfix), so 0 stays */
#if !defined(RC_FXNEAR)
#define RC_FXNEAR 0
#endif
#if !defined(RC_FXULP)
#define RC_FXULP 128
#endif
_Static_assert(RC_FXULP >= 64, "the ulp test holds the relative test's pairs");
static inline int32_t rc_ford(float x)
{
    int32_t b;
    memcpy(&b, &x, 4);
    return b ^ ((b >> 31) & 0x7fffffff);
}
/* the sign of t_a - t_b as the exact values order them */
static inline int rc_fx_cmp(float ta, int ba, float oa, float da, float tb, int bb, float ob, float db)
{
#if RC_FX_ALWAYS
    /* (the measurement build: every finite pair by the integers, the floats' shortcut's proof) */
    if (!(fabsf(ta) < INFINITY) || !(fabsf(tb) < INFINITY)) return (ta > tb) - (ta < tb);
#elif RC_FXNEAR == 1
    if ((uint32_t)(rc_ford(ta) - rc_ford(tb)) + (uint32_t)RC_FXULP > 2u * (uint32_t)RC_FXULP) return (ta > tb) - (ta < tb);
#else
    const float g = (fabsf(ta) + fabsf(tb)) * 0x1p-20f;
    if (__builtin_expect(!(fabsf(ta - tb) <= g) || !(g < INFINITY), 1)) return (ta > tb) - (ta < tb);
#endif
    const int c = rc_fx_exact(ba, oa, da, bb, ob, db);
#if defined(RC_FX_COUNT)
    __atomic_add_fetch(&rc_fx_n[0], 1, __ATOMIC_RELAXED);
    if (c != (ta > tb) - (ta < tb)) __atomic_add_fetch(&rc_fx_n[1], 1, __ATOMIC_RELAXED);
#endif
    return c;
}
/* t_a <= t_b; t_a < t_b */
#define RC_TLE(ta, ba, oa, da, tb, bb, ob, db) (rc_fx_cmp(ta, ba, oa, da, tb, bb, ob, db) <= 0)
#define RC_TLT(ta, ba, oa, da, tb, bb, ob, db) (rc_fx_cmp(ta, ba, oa, da, tb, bb, ob, db) < 0)
#else
#define RC_TLE(ta, ba, oa, da, tb, bb, ob, db) ((ta) <= (tb))
#define RC_TLT(ta, ba, oa, da, tb, bb, ob, db) ((ta) < (tb))
#endif

/* tan(35 degrees): half the renderer's vertical field of view of 70 (semcam.c); in float 0.700207531f */
#define RC_TAN_HALF RC_K(0.7002075382097097)
#define RC_PIF 3.1415927f
#define RC_PI 3.141592653589793
#define RC_DEG2RAD 0.017453292f

/* ------------------------------------------------------------ the tables */

/* what a cell's block is to a ray (RK_*) and the colour multiplier a block's faces take (TN_*): rchead.h */
enum { RC_KINDS = 32 };

struct rctab {
    int ok;
    char err[1600];
    int nspr;
    uint32_t (*tex)[256];       /* a sprite's 16x16 texels, r | g << 8 | b << 16 | a << 24, v then u */
    uint16_t *face[2];          /* [graphics fancy][(id * 16 + meta) * 6 + side]: a sprite, 0xffff none */
    const rcr *bounds;          /* the renderer's table (doubles; FP32: each rounded to float): [(id * 16 + meta) * 6] */
    const double *bounds64;     /* the renderer's own (raycast_tables: the device's tables are the FP64 instance's) */
    uint8_t kind[4096], tint[4096], emit[4096], cull_same[4096];
    float grass[256][3], foliage[256][3], water[256][3];   /* the biome's colours, 0..1 */
    float mob[RC_KINDS][3], unknown[3], item[3], orb[3], arrow[3], fireball[3];
    float end_sky[3];
    float underwater[3];        /* misc/underwater.png's mean, 0..255 */
    uint16_t water_spr, lava_spr;
    uint16_t dp_low[6], dp_high[6];     /* BlockDoublePlant's halves by variant (the table's special icons) */
    float biome_temp[256];
    int sky_by_temp[256];
    struct perlin perlin_a;     /* BiomeGenBase.field_150605_ac: the temperature's height term */
    int avx2, avx512;           /* rays_row's four- and eight-lane loops: the CPU has AVX2, AVX-512F */
};
static struct rctab T;
static pthread_once_t t_once = PTHREAD_ONCE_INIT;
static char t_root[1024];
/* renderstate_compute sets a static (the reference's): one lock for every instance, the FP64 unit's (each instance's own
 * let two instances' references race on it: lane/castexact's raycast-gate, fixed and FP16 casts on 16 threads) */
#if RC_FP == 64
__attribute__((visibility("hidden"))) pthread_mutex_t raycast_rs_lock = PTHREAD_MUTEX_INITIALIZER;
#else
extern __attribute__((visibility("hidden"))) pthread_mutex_t raycast_rs_lock;
#endif
#define t_rs_lock raycast_rs_lock
/* raycast_rays' record (raycast.h), the FP64 unit's, every instance's walk writing it */
#if RC_FP == 64
__attribute__((visibility("hidden"))) _Thread_local struct raycast_ray *raycast_rayrec;
void raycast_rays(struct raycast_ray *r) { raycast_rayrec = r; }
#else
extern __attribute__((visibility("hidden"))) _Thread_local struct raycast_ray *raycast_rayrec;
#endif

static void *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = len > 0 ? proc_malloc((size_t)len) : NULL;
    if (b == NULL || fread(b, 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        proc_free(b);
        return NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return b;
}

static int atlas_int(const char *json, const char *key)
{
    char k[64];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(json, k);
    return p ? atoi(p + strlen(k)) : 0;
}

/* the mean of a texture's opaque texels (an RGBA file of any size), 0..255;
 * 0, or -1 with the message when the file cannot be read */
static int mean_rgba(const char *root, const char *name, float *out)
{
    char p[1400];
    snprintf(p, sizeof p, "%s/out/assets/%s", root, name);
    size_t n = 0;
    unsigned char *b = read_file(p, &n);
    if (b == NULL)
    {
        snprintf(T.err, sizeof T.err, "raycast: cannot read %s (make -C csrc assets)", p);
        return -1;
    }
    double s[3] = {0, 0, 0};
    double c = 0;
    for (size_t i = 0; b && i + 3 < n; i += 4)
        if (b[i + 3] > 25)
        {
            s[0] += b[i], s[1] += b[i + 1], s[2] += b[i + 2];
            c += 1;
        }
    proc_free(b);
    for (int k = 0; k < 3; ++k) out[k] = c > 0 ? (float)(s[k] / c) : 128.0f;
    return 0;
}

/* ColorizerGrass / ColorizerFoliage at a temperature and rainfall */
static int32_t colormap(const uint32_t *map, double temperature, double humidity)
{
    double h = humidity * temperature;
    int a = (int)((1.0 - temperature) * 255.0);
    int b = (int)((1.0 - h) * 255.0);
    return (int32_t)map[(b << 8) | a];
}

static float clamp01(float f) { return f < 0.0F ? 0.0F : f > 1.0F ? 1.0F : f; }

/* render_blocks.c biome_color at the biome's base temperature (y <= 64) */
static int32_t biome_color(const struct rb_table *tab, int slot, int grass, int depth)
{
    const struct rb_biome *b = &tab->biomes[slot & 255];
    int kind = grass ? b->grass_kind : b->foliage_kind;
    if (kind == RB_KIND_MUTATED && depth < 4 && b->base >= 0) return biome_color(tab, b->base, grass, depth + 1);
    if (kind == RB_KIND_CONST) return grass ? b->grass_const : b->foliage_const;
    if (kind == RB_KIND_SWAMP) return 6975545;
    float t = clamp01(b->temperature), h = clamp01(b->rainfall);
    if (grass)
    {
        int32_t c = colormap(tab->grass_map, (double)t, (double)h);
        return kind == RB_KIND_ROOFED ? (((c & 16711422) + 2634760 + 2) >> 1) : c;
    }
    return colormap(tab->foliage_map, (double)t, (double)h);
}

static void rgb01(int32_t c, float *o)
{
    o[0] = (float)(c >> 16 & 255) / 255.0f;
    o[1] = (float)(c >> 8 & 255) / 255.0f;
    o[2] = (float)(c & 255) / 255.0f;
}

/* java.awt.Color.HSBtoRGB (play.c's) */
static int hsb_to_rgb(float hue, float sat, float bri)
{
    int r = 0, g = 0, b = 0;
    if (sat == 0.0F) r = g = b = (int)(bri * 255.0F + 0.5F);
    else
    {
        float h = (hue - (float)floor(hue)) * 6.0F;
        float f = h - (float)floor(h);
        float p = bri * (1.0F - sat);
        float q = bri * (1.0F - sat * f);
        float t = bri * (1.0F - (sat * (1.0F - f)));
        switch ((int)h)
        {
        case 0: r = (int)(bri * 255.0F + 0.5F); g = (int)(t * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
        case 1: r = (int)(q * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
        case 2: r = (int)(p * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(t * 255.0F + 0.5F); break;
        case 3: r = (int)(p * 255.0F + 0.5F); g = (int)(q * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
        case 4: r = (int)(t * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
        case 5: r = (int)(bri * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(q * 255.0F + 0.5F); break;
        }
    }
    return (int)(0xff000000u | (unsigned)(r << 16) | (unsigned)(g << 8) | (unsigned)b);
}

/* BiomeGenBase.getSkyColorByTemp */
static int sky_color_by_temp(float f)
{
    f /= 3.0F;
    if (f < -1.0F) f = -1.0F;
    if (f > 1.0F) f = 1.0F;
    return hsb_to_rgb(0.62222224F - f * 0.05F, 0.5F + f * 0.1F, 1.0F);
}

static int find_sprite(const struct rb_atlas *a, const char *name)
{
    for (int i = 0; i < a->n; ++i)
        if (!strcmp(a->name[i], name)) return i;
    return -1;
}

static void load(void)
{
    const char *root = t_root[0] ? t_root : ".";
    char scene[1200], p[1400];
    snprintf(scene, sizeof scene, "%s/out/java/render/forest-fast", root);
    const struct rb_table *tab = rb_table_shared(scene);
    const struct rb_atlas *atl = rb_atlas_shared(scene);
    if (tab == NULL || atl == NULL)
    {
        snprintf(T.err, sizeof T.err, "raycast: no block table or atlas in %s", scene);
        return;
    }
    snprintf(p, sizeof p, "%s/atlas.json", scene);
    size_t jn = 0;
    char *json = read_file(p, &jn);
    if (json == NULL) { snprintf(T.err, sizeof T.err, "raycast: cannot read %s", p); return; }
    char *js = proc_malloc(jn + 1);
    memcpy(js, json, jn);
    js[jn] = 0;
    proc_free(json);
    int aw = atlas_int(js, "atlas_width"), ah = atlas_int(js, "atlas_height");
    proc_free(js);
    snprintf(p, sizeof p, "%s/atlas.rgba", scene);
    size_t an = 0;
    unsigned char *atlas = read_file(p, &an);
    if (atlas == NULL || aw <= 0 || ah <= 0 || an != (size_t)aw * (size_t)ah * 4)
    {
        snprintf(T.err, sizeof T.err, "raycast: %s is not %dx%d RGBA", p, aw, ah);
        proc_free(atlas);
        return;
    }
    /* every atlas sprite as 16x16 texels, nearest */
    T.nspr = atl->n;
    T.tex = proc_calloc((size_t)atl->n, sizeof *T.tex);
    for (int s = 0; s < atl->n; ++s)
    {
        const struct rb_uv *uv = &atl->uv[s];
        double x0 = (double)uv->min_u * aw, x1 = (double)uv->max_u * aw;
        double y0 = (double)uv->min_v * ah, y1 = (double)uv->max_v * ah;
        for (int v = 0; v < 16; ++v)
            for (int u = 0; u < 16; ++u)
            {
                int x = (int)floor(x0 + (x1 - x0) * ((double)u + 0.5) / 16.0);
                int y = (int)floor(y0 + (y1 - y0) * ((double)v + 0.5) / 16.0);
                if (x < 0) x = 0;
                if (x >= aw) x = aw - 1;
                if (y < 0) y = 0;
                if (y >= ah) y = ah - 1;
                const unsigned char *t = atlas + ((size_t)y * (size_t)aw + (size_t)x) * 4;
                T.tex[s][v * 16 + u] = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
            }
    }
    proc_free(atlas);
    /* the table's icons as sprites; fancy graphics takes a leaf's cutout icon for its opaque one */
    int *spr = proc_calloc((size_t)tab->n_icons + 1, sizeof *spr), *fancy = proc_calloc((size_t)tab->n_icons + 1, sizeof *fancy);
    for (int i = 0; i < tab->n_icons; ++i)
    {
        spr[i] = fancy[i] = find_sprite(atl, tab->icon_name[i]);
        size_t l = strlen(tab->icon_name[i]);
        if (l > 7 && !strcmp(tab->icon_name[i] + l - 7, "_opaque"))
        {
            char nm[256];
            snprintf(nm, sizeof nm, "%.*s", (int)(l - 7), tab->icon_name[i]);
            int f = find_sprite(atl, nm);
            if (f >= 0) fancy[i] = f;
        }
    }
    size_t nf = (size_t)RB_IDS * RB_METAS * RB_SIDES;
    T.face[0] = proc_malloc(nf * sizeof *T.face[0]);
    T.face[1] = proc_malloc(nf * sizeof *T.face[1]);
    for (size_t i = 0; i < nf; ++i)
    {
        int ic = tab->icon_index[i];
        T.face[0][i] = ic != RB_NO_ICON && ic < tab->n_icons && spr[ic] >= 0 ? (uint16_t)spr[ic] : 0xffff;
        T.face[1][i] = ic != RB_NO_ICON && ic < tab->n_icons && fancy[ic] >= 0 ? (uint16_t)fancy[ic] : 0xffff;
    }
    proc_free(spr);
    proc_free(fancy);
    T.bounds64 = tab->bounds;
#if RC_FP < 64
    {
        rcr *bf = proc_malloc(nf * sizeof *bf);
        if (bf == NULL)
        {
            snprintf(T.err, sizeof T.err, "raycast: out of memory");
            return;
        }
        for (size_t i = 0; i < nf; ++i) bf[i] = (rcr)tab->bounds[i];
        T.bounds = bf;
    }
#else
    T.bounds = tab->bounds;
#endif
    for (int id = 0; id < 4096; ++id)
    {
        const struct block_def *bd = &BLOCKS[id];
        int any = 0;
        for (int m = 0; m < 16 && !any; ++m)
            for (int s = 0; s < 6 && !any; ++s) any = T.face[0][((size_t)id * 16 + (size_t)m) * 6 + (size_t)s] != 0xffff;
        int rt = bd->render_type;
        const double *b = tab->bounds + (size_t)id * 16 * 6;
        int full = b[0] == 0.0 && b[1] == 0.0 && b[2] == 0.0 && b[3] == 1.0 && b[4] == 1.0 && b[5] == 1.0;
        uint8_t k = RK_AIR;
        if (id == 0 || !bd->exists || !any) k = RK_AIR;
        else if (id == 8 || id == 9) k = RK_WATER;
        else if (id == 10 || id == 11) k = RK_LAVA;
        else if (rt == 1 || rt == 6 || rt == 19 || rt == 40) k = RK_CROSS;
        else if (full && rt != 2) k = RK_CUBE;
        else k = RK_BOX;
        T.kind[id] = k;
        T.emit[id] = bd->light;
        T.cull_same[id] = id == 20 || id == 95 || id == 79 || id == 102 || id == 160;
        T.tint[id] = id == 2 ? TN_GRASS_TOP : id == 18 ? TN_OLDLEAF : id == 161 || id == 106 ? TN_FOLIAGE
                   : id == 8 || id == 9 ? TN_WATER : id == 31 ? TN_TALLGRASS : id == 83 ? TN_GRASS
                   : id == 104 || id == 105 ? TN_STEM : id == 175 ? TN_DPLANT : TN_NONE;
    }
    for (int s = 0; s < 256; ++s)
    {
        rgb01(biome_color(tab, s, 1, 0), T.grass[s]);
        rgb01(biome_color(tab, s, 0, 0), T.foliage[s]);
        rgb01(tab->biomes[s].water, T.water[s]);
        T.biome_temp[s] = tab->biomes[s].temperature;
    }
    T.water_spr = T.face[0][((size_t)9 * 16) * 6 + 1];
    T.lava_spr = T.face[0][((size_t)11 * 16) * 6 + 1];
    for (int v = 0; v < 6; ++v)
    {
        int lo = RB_ICON_DP_LOW + v < tab->n_special ? find_sprite(atl, tab->special[RB_ICON_DP_LOW + v]) : -1;
        int hi = RB_ICON_DP_HIGH + v < tab->n_special ? find_sprite(atl, tab->special[RB_ICON_DP_HIGH + v]) : -1;
        T.dp_low[v] = lo >= 0 ? (uint16_t)lo : 0xffff;
        T.dp_high[v] = hi >= 0 ? (uint16_t)hi : 0xffff;
    }
    /* the entities' flat colours: their textures' means */
    static const char *const MOB[RC_KINDS] = {
        [AK_PIG] = "mobs/pig.rgba", [AK_COW] = "mobs/cow.rgba", [AK_MOOSHROOM] = "mobs/mooshroom.rgba",
        [AK_CHICKEN] = "mobs/chicken.rgba", [AK_SHEEP] = "mobs/sheep_fur.rgba", [VK_VILLAGER] = "mobs/villager.rgba",
        [VK_IRON_GOLEM] = "mobs/iron_golem.rgba", [SK_SLIME] = "mobs/slime.rgba", [SK_MAGMA_CUBE] = "mobs/magmacube.rgba",
        [SK_PLAYER] = "mobs/steve.rgba", [HK_ZOMBIE] = "mobs/zombie.rgba", [HK_SKELETON] = "mobs/skeleton.rgba",
        [HK_CREEPER] = "mobs/creeper.rgba", [HK_SPIDER] = "mobs/spider.rgba", [HK_PLAYER] = "mobs/steve.rgba",
        [GK_GHAST] = "mobs/ghast.rgba", [AK_SQUID] = "mobs/squid.rgba", [AK_BAT] = "mobs/bat.rgba",
        [HK_ENDERMAN] = "mobs/enderman.rgba", [HK_WITCH] = "mobs/witch.rgba", [HK_SILVERFISH] = "mobs/silverfish.rgba",
        [HK_PIGMAN] = "mobs/pigman.rgba", [HK_BLAZE] = "mobs/blaze.rgba", [HK_CAVE_SPIDER] = "mobs/cave_spider.rgba",
    };
    for (int k = 0; k < RC_KINDS; ++k)
        if (MOB[k]) { if (mean_rgba(root, MOB[k], T.mob[k])) return; }
        else T.mob[k][0] = T.mob[k][1] = T.mob[k][2] = 128.0f;
    T.unknown[0] = T.unknown[1] = T.unknown[2] = 128.0f;
    if (mean_rgba(root, "items.rgba", T.item) || mean_rgba(root, "orb.rgba", T.orb) || mean_rgba(root, "arrow.rgba", T.arrow) ||
        mean_rgba(root, "misc/underwater.rgba", T.underwater))
        return;
    T.fireball[0] = 230.0f, T.fireball[1] = 120.0f, T.fireball[2] = 30.0f;
    {
        /* RenderGlobal's End sky: the texture times 0x282828 */
        snprintf(p, sizeof p, "%s/out/assets/end_sky.rgb", root);
        size_t n = 0;
        unsigned char *b = read_file(p, &n);
        if (b == NULL)
        {
            snprintf(T.err, sizeof T.err, "raycast: cannot read %s (make -C csrc assets)", p);
            return;
        }
        double s[3] = {0, 0, 0};
        for (size_t i = 0; b && i + 2 < n; i += 3) s[0] += b[i], s[1] += b[i + 1], s[2] += b[i + 2];
        for (int k = 0; k < 3; ++k) T.end_sky[k] = n >= 3 ? (float)(s[k] / (double)(n / 3)) * (40.0f / 255.0f) : 10.0f;
        proc_free(b);
    }
    jrand r;
    jr_seed(&r, 1234);
    perlin_init(&T.perlin_a, &r, 1);
#if defined(RC_AVX2)
    T.avx2 = __builtin_cpu_supports("avx2") != 0;
    T.avx512 = __builtin_cpu_supports("avx512f") != 0;
#endif
    T.ok = 1;
}

int raycast_load(const char *root, char *err, size_t n)
{
    if (root && !t_root[0]) snprintf(t_root, sizeof t_root, "%s", root);
    pthread_once(&t_once, load);
    if (!T.ok && err) snprintf(err, n, "%s", T.err);
#if RC_FP == 64
    /* the other instances' own tables from the same root */
    if (T.ok && raycast_load_f32(root, err, n)) return -1;
    if (T.ok && raycast_load_fx(root, err, n)) return -1;
    if (T.ok && raycast_load_f16(root, err, n)) return -1;
#endif
    return T.ok ? 0 : -1;
}

/* ------------------------------------------- the frame's light, sky and fog */

/* what a cast reads of renderstate: the lightmap, the sky's colour, the fog */
struct rclight {
    int lm[256];
    double sky[3];
    float fcr, fcg, fcb, far;
    float gfogs, gfoge, gfogd;
    int gfogm;
};

static float rc_rain(const struct rs_in *in, float pt) { return in->prain + (in->rain - in->prain) * pt; }
static float rc_thunder(const struct rs_in *in, float pt) { return (in->pthu + (in->thu - in->pthu) * pt) * rc_rain(in, pt); }

/* renderstate.c's rs_celestial with the provider as an argument */
static float rc_celestial(int dim, int64_t wt, float pt)
{
    if (dim == -1) return 0.5f;
    if (dim == 1) return 0.0f;
    int32_t v4 = (int32_t)(wt % 24000);
    float v5 = ((float)v4 + pt) / 24000.0f - 0.25f;
    if (v5 < 0.0f) v5 = v5 + 1.0f;
    if (v5 > 1.0f) v5 = v5 - 1.0f;
    float v6 = v5;
    float v7 = (float)((fd_cos((double)v5 * RC_PI) + 1.0) / 2.0);
    v5 = 1.0f - v7;
    v5 = v6 + (v5 - v6) / 3.0f;
    return v5;
}

static float rc_sunbrightness(const struct rs_in *in, float pt)
{
    float a = rc_celestial(in->dim, in->wt, pt);
    float v3 = 1.0f - (mh_cos(a * RC_PIF * 2.0f) * 2.0f + 0.2f);
    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;
    v3 = 1.0f - v3;
    v3 = (float)((double)v3 * (1.0 - (double)(rc_rain(in, pt) * 5.0f) / 16.0));
    v3 = (float)((double)v3 * (1.0 - (double)(rc_thunder(in, pt) * 5.0f) / 16.0));
    return v3 * 0.8f + 0.2f;
}

static int rc_sunrise(int dim, int64_t wt, float pt, float *r)
{
    float a = rc_celestial(dim, wt, pt);
    float v4 = mh_cos(a * RC_PIF * 2.0f) - 0.0f;
    float v5 = -0.0f;
    if (v4 >= v5 - 0.4f && v4 <= v5 + 0.4f)
    {
        float v6 = (v4 - v5) / 0.4f * 0.5f + 0.5f;
        float v7 = 1.0f - (1.0f - mh_sin(v6 * RC_PIF)) * 0.99f;
        v7 = v7 * v7;
        r[0] = v6 * 0.3f + 0.7f;
        r[1] = v6 * v6 * 0.7f + 0.2f;
        r[2] = v6 * v6 * 0.0f + 0.2f;
        r[3] = v7;
        return 1;
    }
    return 0;
}

static void rc_providerfog(int dim, int64_t wt, float pt, double *v)
{
    if (dim == -1)
    {
        v[0] = 0.20000000298023224;
        v[1] = 0.029999999329447746;
        v[2] = 0.029999999329447746;
        return;
    }
    if (dim == 1)
    {
        v[0] = (float)(0xA080A0 >> 16 & 255) / 255.0f * 0.15f;
        v[1] = (float)(0xA080A0 >> 8 & 255) / 255.0f * 0.15f;
        v[2] = (float)(0xA080A0 & 255) / 255.0f * 0.15f;
        return;
    }
    float a = rc_celestial(dim, wt, pt);
    float v3 = mh_cos(a * RC_PIF * 2.0f) * 2.0f + 0.5f;
    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;
    float v4 = 0.7529412f;
    float v5 = 0.84705883f;
    float v6 = 1.0f;
    v4 = v4 * (v3 * 0.94f + 0.06f);
    v5 = v5 * (v3 * 0.94f + 0.06f);
    v6 = v6 * (v3 * 0.91f + 0.09f);
    v[0] = (double)v4;
    v[1] = (double)v5;
    v[2] = (double)v6;
}

static void rc_skycolor(const struct rs_in *in, double *v)
{
    float a = rc_celestial(in->dim, in->wt, in->pt);
    float v4 = mh_cos(a * RC_PIF * 2.0f) * 2.0f + 0.5f;
    if (v4 < 0.0f) v4 = 0.0f;
    if (v4 > 1.0f) v4 = 1.0f;
    int t = in->dim == 1 ? 0 : in->skytemp;
    float v11 = (float)(t >> 16 & 255) / 255.0f;
    float v12 = (float)(t >> 8 & 255) / 255.0f;
    float v13 = (float)(t & 255) / 255.0f;
    v11 = v11 * v4;
    v12 = v12 * v4;
    v13 = v13 * v4;
    float v14 = rc_rain(in, in->pt);
    float v15, v16;
    if (v14 > 0.0f)
    {
        v15 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.6f;
        v16 = 1.0f - v14 * 0.75f;
        v11 = v11 * v16 + v15 * (1.0f - v16);
        v12 = v12 * v16 + v15 * (1.0f - v16);
        v13 = v13 * v16 + v15 * (1.0f - v16);
    }
    v15 = rc_thunder(in, in->pt);
    if (v15 > 0.0f)
    {
        v16 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.2f;
        float v17 = 1.0f - v15 * 0.75f;
        v11 = v11 * v17 + v16 * (1.0f - v17);
        v12 = v12 * v17 + v16 * (1.0f - v17);
        v13 = v13 * v17 + v16 * (1.0f - v17);
    }
    if (in->lbolt > 0)
    {
        float v16b = (float)in->lbolt - in->pt;
        if (v16b > 1.0f) v16b = 1.0f;
        v16b = v16b * 0.45f;
        v11 = v11 * (1.0f - v16b) + 0.8f * v16b;
        v12 = v12 * (1.0f - v16b) + 0.8f * v16b;
        v13 = v13 * (1.0f - v16b) + 1.0f * v16b;
    }
    v[0] = (double)v11;
    v[1] = (double)v12;
    v[2] = (double)v13;
}

/* renderstate_compute's lightmap, sky colour, updateFogColor and
 * setupFog(0), the provider an argument instead of a static */
static void rc_light_port(const struct rs_in *in, struct rclight *o)
{
    float pt = in->pt;
    rc_skycolor(in, o->sky);
    float sun1 = rc_sunbrightness(in, 1.0f);
    /* the lightmap reads only these: the last one a thread made is kept (in
     * daylight the sun term is clamped, so most casts make the same) */
    struct lmkey { float sun1, tfx, gamma, pt, lbt[16]; int lbolt, dim, nv, nvd; } key;
    memset(&key, 0, sizeof key);
    key.sun1 = sun1, key.tfx = in->tfx, key.gamma = in->gamma, key.pt = in->pt;
    memcpy(key.lbt, in->lbt, sizeof key.lbt);
    key.lbolt = in->lbolt, key.dim = in->dim, key.nv = in->nv, key.nvd = in->nvd;
    /* on the process heap, one pointer a thread: libneth's thread-locals take glibc's small static TLS (rlbind.mk) */
    static _Thread_local struct lmmemo { struct lmkey key; int lm[256]; } *memo;
    static _Thread_local int lm_have;
    if (memo == NULL) memo = proc_malloc(sizeof *memo);
    if (lm_have && !memcmp(&key, &memo->key, sizeof key)) memcpy(o->lm, memo->lm, sizeof memo->lm);
    else
    {
        for (int i = 0; i < 256; ++i)
        {
            float v4 = sun1 * 0.95f + 0.05f;
            float v5 = in->lbt[i / 16] * v4;
            float v6 = in->lbt[i % 16] * (in->tfx * 0.1f + 1.5f);
            if (in->lbolt > 0) v5 = in->lbt[i / 16];
            float v7 = v5 * (sun1 * 0.65f + 0.35f);
            float v8 = v5 * (sun1 * 0.65f + 0.35f);
            float v11 = v6 * ((v6 * 0.6f + 0.4f) * 0.6f + 0.4f);
            float v12 = v6 * (v6 * v6 * 0.6f + 0.4f);
            float v13 = v7 + v6;
            float v14 = v8 + v11;
            float v15 = v5 + v12;
            v13 = v13 * 0.96f + 0.03f;
            v14 = v14 * 0.96f + 0.03f;
            v15 = v15 * 0.96f + 0.03f;
            if (in->dim == 1)
            {
                v13 = 0.22f + v6 * 0.75f;
                v14 = 0.28f + v11 * 0.75f;
                v15 = 0.25f + v12 * 0.75f;
            }
            if (in->nv)
            {
                float v16 = in->nvd > 200 ? 1.0f : 0.7f + mh_sin(((float)in->nvd - pt) * RC_PIF * 0.2f) * 0.3f;
                float v17 = 1.0f / v13;
                if (v17 > 1.0f / v14) v17 = 1.0f / v14;
                if (v17 > 1.0f / v15) v17 = 1.0f / v15;
                v13 = v13 * (1.0f - v16) + v13 * v17 * v16;
                v14 = v14 * (1.0f - v16) + v14 * v17 * v16;
                v15 = v15 * (1.0f - v16) + v15 * v17 * v16;
            }
            if (v13 > 1.0f) v13 = 1.0f;
            if (v14 > 1.0f) v14 = 1.0f;
            if (v15 > 1.0f) v15 = 1.0f;
            float v16 = in->gamma;
            float v17 = 1.0f - v13;
            float v18 = 1.0f - v14;
            float v19 = 1.0f - v15;
            v17 = 1.0f - v17 * v17 * v17 * v17;
            v18 = 1.0f - v18 * v18 * v18 * v18;
            v19 = 1.0f - v19 * v19 * v19 * v19;
            v13 = v13 * (1.0f - v16) + v17 * v16;
            v14 = v14 * (1.0f - v16) + v18 * v16;
            v15 = v15 * (1.0f - v16) + v19 * v16;
            v13 = v13 * 0.96f + 0.03f;
            v14 = v14 * 0.96f + 0.03f;
            v15 = v15 * 0.96f + 0.03f;
            if (v13 > 1.0f) v13 = 1.0f;
            if (v14 > 1.0f) v14 = 1.0f;
            if (v15 > 1.0f) v15 = 1.0f;
            if (v13 < 0.0f) v13 = 0.0f;
            if (v14 < 0.0f) v14 = 0.0f;
            if (v15 < 0.0f) v15 = 0.0f;
            int v21 = (int)(v13 * 255.0f);
            int v22 = (int)(v14 * 255.0f);
            int v23 = (int)(v15 * 255.0f);
            o->lm[i] = 255 << 24 | v21 << 16 | v22 << 8 | v23;
        }
        if (memo != NULL)
        {
            memo->key = key;
            memcpy(memo->lm, o->lm, sizeof memo->lm);
            lm_have = 1;
        }
    }
    /* EntityRenderer.updateFogColor */
    {
        float v4 = 0.25f + 0.75f * (float)in->rd / 16.0f;
        v4 = 1.0f - (float)fd_pow((double)v4, 0.25);
        double sky[3];
        rc_skycolor(in, sky);
        float v6 = (float)sky[0];
        float v7 = (float)sky[1];
        float v8 = (float)sky[2];
        double fog[3];
        rc_providerfog(in->dim, in->wt, pt, fog);
        o->fcr = (float)fog[0];
        o->fcg = (float)fog[1];
        o->fcb = (float)fog[2];
        float v11;
        if (in->rd >= 4)
        {
            /* getLook(pt) at pt 1: the rotation as it stands */
            double look0;
            if (in->pt == 1.0f)
            {
                float v3 = mh_sin(-in->yaw * RC_DEG2RAD - RC_PIF);
                float v4b = -mh_cos(-in->pit * RC_DEG2RAD);
                look0 = (double)(v3 * v4b);
            }
            else
            {
                float v2 = in->ppit + (in->pit - in->ppit) * in->pt;
                float v3 = in->pyaw + (in->yaw - in->pyaw) * in->pt;
                float v5 = mh_sin(-v3 * RC_DEG2RAD - RC_PIF);
                float v6b = -mh_cos(-v2 * RC_DEG2RAD);
                look0 = (double)(v5 * v6b);
            }
            float angr = rc_celestial(in->dim, in->wt, pt) * RC_PIF * 2.0f;
            float x = mh_sin(angr) > 0.0f ? -1.0f : 1.0f;
            v11 = (float)(look0 * (double)x);
            if (v11 < 0.0f) v11 = 0.0f;
            if (v11 > 0.0f)
            {
                float rise[4];
                if (rc_sunrise(in->dim, in->wt, pt, rise))
                {
                    v11 = v11 * rise[3];
                    o->fcr = o->fcr * (1.0f - v11) + rise[0] * v11;
                    o->fcg = o->fcg * (1.0f - v11) + rise[1] * v11;
                    o->fcb = o->fcb * (1.0f - v11) + rise[2] * v11;
                }
            }
        }
        o->fcr = o->fcr + (v6 - o->fcr) * v4;
        o->fcg = o->fcg + (v7 - o->fcg) * v4;
        o->fcb = o->fcb + (v8 - o->fcb) * v4;
        float v19 = rc_rain(in, pt);
        if (v19 > 0.0f)
        {
            v11 = 1.0f - v19 * 0.5f;
            float v20 = 1.0f - v19 * 0.4f;
            o->fcr = o->fcr * v11;
            o->fcg = o->fcg * v11;
            o->fcb = o->fcb * v20;
        }
        v11 = rc_thunder(in, pt);
        if (v11 > 0.0f)
        {
            float v20 = 1.0f - v11 * 0.5f;
            o->fcr = o->fcr * v20;
            o->fcg = o->fcg * v20;
            o->fcb = o->fcb * v20;
        }
        if (in->mat == 1)
        {
            float v22 = (float)in->resp * 0.2f;
            o->fcr = 0.02f + v22;
            o->fcg = 0.02f + v22;
            o->fcb = 0.2f + v22;
        }
        else if (in->mat == 2)
        {
            o->fcr = 0.6f;
            o->fcg = 0.1f;
            o->fcb = 0.0f;
        }
        float v22 = in->fc2 + (in->fc1 - in->fc2) * pt;
        o->fcr = o->fcr * v22;
        o->fcg = o->fcg * v22;
        o->fcb = o->fcb * v22;
        double v14 = (in->ppy + (in->py - in->ppy) * (double)pt) * in->voidf;
        if (in->blind)
        {
            if (in->bldur < 20) v14 = v14 * (double)(1.0f - (float)in->bldur / 20.0f);
            else v14 = 0.0;
        }
        if (v14 < 1.0)
        {
            if (v14 < 0.0) v14 = 0.0;
            v14 = v14 * v14;
            o->fcr = (float)((double)o->fcr * v14);
            o->fcg = (float)((double)o->fcg * v14);
            o->fcb = (float)((double)o->fcb * v14);
        }
        if (in->nv)
        {
            float v23 = in->nvd > 200 ? 1.0f : 0.7f + mh_sin(((float)in->nvd - pt) * RC_PIF * 0.2f) * 0.3f;
            float v17 = 1.0f / o->fcr;
            if (v17 > 1.0f / o->fcg) v17 = 1.0f / o->fcg;
            if (v17 > 1.0f / o->fcb) v17 = 1.0f / o->fcb;
            o->fcr = o->fcr * (1.0f - v23) + o->fcr * v17 * v23;
            o->fcg = o->fcg * (1.0f - v23) + o->fcg * v17 * v23;
            o->fcb = o->fcb * (1.0f - v23) + o->fcb * v17 * v23;
        }
    }
    o->far = (float)(in->rd * 16);
    /* EntityRenderer.setupFog(0, pt) */
    if (in->blind)
    {
        float v6 = 5.0f;
        if (in->bldur < 20) v6 = 5.0f + (o->far - 5.0f) * (1.0f - (float)in->bldur / 20.0f);
        o->gfogm = 0x2601;
        o->gfogs = 0.0f;
        o->gfoge = v6 * 0.8f;
    }
    else if (in->mat == 1)
    {
        o->gfogm = 0x0800;
        o->gfogd = in->wb ? 0.05f : 0.1f - (float)in->resp * 0.03f;
    }
    else if (in->mat == 2)
    {
        o->gfogm = 0x0800;
        o->gfogd = 2.0f;
    }
    else
    {
        float v6 = o->far;
        if (in->voidp && !in->crea)
        {
            double v10 = (double)((in->brf & 15728640) >> 20) / 16.0 + (in->ppy + (in->py - in->ppy) * (double)pt + 4.0) / 32.0;
            if (v10 < 1.0)
            {
                if (v10 < 0.0) v10 = 0.0;
                v10 = v10 * v10;
                float v9 = 100.0f * (float)v10;
                if (v9 < 5.0f) v9 = 5.0f;
                if (v6 > v9) v6 = v9;
            }
        }
        o->gfogm = 0x2601;
        o->gfogs = v6 * 0.75f;
        o->gfoge = v6;
        if (in->xzfog)
        {
            o->gfogs = v6 * 0.05f;
            float e = v6 < 192.0f ? v6 : 192.0f;
            o->gfoge = e * 0.5f;
        }
    }
}

/* the reference's: renderstate_compute itself (its provider is a static,
 * so one cast at a time) */
static void rc_light_ref(const struct rs_in *in, struct rclight *o)
{
    struct rs_out r;
    pthread_mutex_lock(&t_rs_lock);
    renderstate_compute(in, &r);
    pthread_mutex_unlock(&t_rs_lock);
    memcpy(o->lm, r.lm, sizeof o->lm);
    memcpy(o->sky, r.sky, sizeof o->sky);
    o->fcr = r.fcr, o->fcg = r.fcg, o->fcb = r.fcb, o->far = r.far;
    o->gfogs = r.gfogs, o->gfoge = r.gfoge, o->gfogd = r.gfogd;
    o->gfogm = r.gfogm;
}

/* ------------------------------------------------------------ the world */

/* one cast's state */
enum { RC_ENTS = 64 };
/* what of the light, sky and fog a cast's pixels read (the kept frame's key): the lightmap entries lit() indexes, and
 * whether a pixel took the fog's or the sky's colour; written through the state's pointer to its own use_at (walk
 * reads the state as const) */
struct rcuse {
    uint64_t lm[4];
    int fog, sky;
};
#define RC_USE_LM(k, i) ((k)->use->lm[(i) >> 6] |= 1ull << ((i) & 63))
#define RC_USE_FOG(k) ((k)->use->fog = 1)
#define RC_USE_SKY(k) ((k)->use->sky = 1)

/* a pixel's shading as its walk made it (the kept frame's, for its shading again under another light, fog or sky):
 * each surface lit() lit, in the ray's order (rt the texel times the tint, li the lightmap entry, the face's shade, f
 * the fog's factor; a the alpha of a surface gone through), then what the ray ended on: a lit hit, the fog's colour,
 * the End's sky, or the sky plane fogged at f */
enum { RL_THRU, RL_LIT, RL_FOG, RL_END, RL_SKY };
struct rclayer {
    float rt[3];
    float shade, f, a;
    uint8_t li, kind;
};
/* the frame's layers, pixel p's cnt[p] from off[p] (a pixel walked again appends its own past at); over: there was no
 * room (the frame cannot be shaded again); each pixel's reach: its ray's direction across x and z and the t of the
 * boundary its walk's last cell ends at (every cell the walk read lies on the ray before it, or above or below such a
 * cell); and its seen note (raycast_seen's: the slot sq[p], -1 none, its cell and distance) */
struct rcsv {
    int32_t x, y, z;
    float d;
};
struct rcrec {
    struct rclayer *lay;
    uint32_t *off, *cnt;
    uint32_t at, cap;
    int over;
    int pix;
    float *rix, *riz, *reach;   /* 1 / the direction across x and z (held to 1e30), the reach: keep_reaches' */
    int8_t *sq;
    struct rcsv *sv;
    uint8_t *mk;                /* keep_rewalk's pixels to walk again */
};

struct rcent {
    rcr box[6];                 /* in the frame's coordinates (FP32: less the eye's cell) */
    int i0, i1, j0, j1;         /* the pixels the box can cover */
    float rgb[3];
    int lmi;                    /* sky * 16 + block at 0.66 of its height */
};

struct rc {
    struct world *w;
    rcr ox, oy, oz;             /* the eye in the frame's coordinates (FP32: its offset in its cell, 0 <= o < 1) */
    double wx, wy, wz;          /* the eye's world position (the window's, the eye's cell's) */
    rcr fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect;
    int bx, by, bz;             /* the frame's origin cell: FP64 0, FP32 the eye's (RC_CX) */
    int eye_x, eye_y, eye_z;     /* the eye's cell */
    int liquid;                 /* the eye's liquid: 8 water, 10 lava, 0 none */
    int range, no_sky, dim, fancy, top, ch;   /* ch: the frame's bytes a pixel (client.channels) */
    rcr rnext;                  /* the least t past the range */
    /* the light: lm as 0..1 floats; the sky's and fog's colour 0..255 */
    float lm[256][3];
#if NETH_RC_HEAD_U8
    uint32_t lm_u32[256];
#endif
    float sky[3], fog[3];
    int exp_fog;
    int uw;                     /* the eye in water: ItemRenderer.renderWarpedTextureOverlay at alpha 0.5 */
    float uw_rgb[3];
    float fs, fe, fd, sky_end, fspan;   /* fspan: 1 / (fe - fs) */
    struct raycast_seen *seen;  /* raycast_seen's (NULL: raycast) and its ids */
    int16_t seen_ids[RC_SEEN];
    int pix_at;                 /* the pixel being walked (rays_runs) */
    int *spx, spx_at[RC_SEEN];  /* each seen slot's pixel (seen_add's tie): spx is &spx_at, set with seen */
    int susp;                   /* walk may hand rays to rays_on (rays_runs): 1 in water (a3), 2 up through air (a4) */
    struct rcuse *use, use_at;  /* what of the light, sky and fog the frame's pixels read: use is &use_at */
    struct rcrec *rec;          /* the frame's shading kept (NULL: not kept) */
    int nent;
    struct rcent ent[RC_ENTS];
    /* fast: the sections around the eye, [(cx - cx0) * ncz + cz - cz0] */
    int cx0, cz0, ncx, ncz;
    struct chunk **col;
    const struct chunk_sec **sec;   /* [col * 16 + sy], NULL: no blocks */
    const struct chunk_sec **lsec;  /* the lanes' (rays_air_body): sec's, NULL too for a section with ids past 255 */
    const uint64_t *bm;         /* lsec's sections' bricks (sec_bricks, RC_BRICK) */
#if defined(RC_RUN_VERIFY)
    int no_run;                 /* the verify build's second walk: the jumps a section, not empty_run */
#endif
    struct rcrow *row;        /* the screen x of the run of pixels being cast (rays_row) */
    struct rcrays *rays;        /* the batch of pixels being cast (rays_fill) */
    struct rcpix *pix;          /* the batch's pixels before their bytes (pixels_out) */
};

/* a run of pixels' screen x, a column's the same on every row (rays_sx) */
enum { RC_ROW = 64 };
struct rcrow {
    rcr sx[RC_ROW + 16];        /* made for run sx_i0 (to a multiple of 16 past it: the lanes' reads) */
    int sx_i0;                  /* -1 at a cast's start */
    int sx_n;                   /* the run length sx was made for: rays_fill's batches end runs short of a row */
};

/* a batch of pixels (row-major) as their walks start: rays_row's rays, a run at a time in one pass (it vectorizes;
 * each ray's operations are walk's own, in its order: the unit direction, the forward part of it, the reciprocal
 * directions, 0 on an axis the ray does not move along, and the t of the first boundary on each axis, INFINITY
 * there; its lanes' stores past a run's end land in the next run's rays or the arrays' last eight), the cell each
 * walk starts from and the t of its next boundary on each axis (the eye's, or where rays_air moved it), the nearest
 * entity box the pixel sees (te INFINITY, ent -1: none), the face the ray enters it by, and the fast path's t limit
 * (te or the range's). how: the walk starts there (RC_WALK); or the air prefix took the fast loop's step onto a block
 * across axis at t, the cell and boundaries after it (RC_HIT); or it ended the ray's run with no block (RC_END) */
enum { RC_BATCH = 256, RC_BPAD = RC_BATCH + 16 };
enum { RC_WALK = 0, RC_HIT, RC_END };
struct rcrays {
    rcr dx[RC_BPAD], dy[RC_BPAD], dz[RC_BPAD], fwd[RC_BPAD];
    rcr idx[RC_BPAD], idy[RC_BPAD], idz[RC_BPAD];
    rcr tx[RC_BPAD], ty[RC_BPAD], tz[RC_BPAD];
    rcr te[RC_BATCH], tlim[RC_BATCH], t[RC_BATCH];
    int32_t ix[RC_BATCH], iy[RC_BATCH], iz[RC_BATCH];
    int32_t ent[RC_BATCH], eface[RC_BATCH];
    int32_t how[RC_BATCH], axis[RC_BATCH];
    /* sfa 1: the air prefix took a ray going up from the eye's water out of it and on through air (walk starts with
     * eye_was_liquid 0); 2: by a step up into cell sfx, sfy, sfz at sft, whose surface walk draws first */
    int32_t sfa[RC_BATCH], sfx[RC_BATCH], sfy[RC_BATCH], sfz[RC_BATCH];
    rcr sft[RC_BATCH];
    /* sfa 3 (lane/castwet): walk handed the ray on in water, past a water surface it drew, to rays_on, which takes
     * it through the water and hands it back as the air prefix does (how); walk goes on from there with the colour
     * it had (wacc, wtr) and in_water 1. sfa 4: the same for a ray going up past a block it passed, on through air,
     * in_water 0. wq: those rays, nw of them, in pixel order */
    float wacc[3][RC_BATCH], wtr[RC_BATCH];
    int32_t wq[RC_BATCH + 8], nw;
};

/* a batch's pixels as pixel_out takes them (lane/castmove): the surfaces passed through (acc, trans) and what the
 * ray ended on (col), made bytes a batch at a time (pixels_out) */
struct rcpix {
    float acc[3][RC_BATCH], trans[RC_BATCH], col[3][RC_BATCH];
};

static int liquid_of(int id) { return id == 8 || id == 9 ? 8 : id == 10 || id == 11 ? 10 : 0; }

/* raycast_seen: a pixel drawn from block id's opaque cube face at (x, y, z), t from the eye; of two as near, the one
 * the frame's pixel order meets first: with bp (each slot's pixel), the lower pixel px, so a walk out of pixel order
 * (the runs handed to rays_on) keeps the same cell; without it the adds come in pixel order */
static inline void seen_add(struct raycast_seen *s, int q, int x, int y, int z, float d, int px, int *bp)
{
    s->n[q] += 1;
    if (s->d[q] < 0.0f || d < s->d[q] || (bp && d == s->d[q] && px < bp[q]))
    {
        s->d[q] = d;
        s->x[q] = x, s->y[q] = y, s->z[q] = z;
        if (bp) bp[q] = px;
    }
}

static inline void seen_note(const struct rc *k, int id, int x, int y, int z, rcr t)
{
    for (int q = 0; q < RC_SEEN; ++q)
        if (k->seen_ids[q] == id || (k->seen_ids[q] == RC_SEEN_LOGS && (id == 17 || id == 162)))
        {
            seen_add(k->seen, q, x, y, z, (float)t, k->pix_at, k->spx);
            /* the pixel's note, for the seen record made again when some pixels are walked again (keep_rewalk) */
            if (k->rec)
            {
                k->rec->sq[k->rec->pix] = (int8_t)q;
                k->rec->sv[k->rec->pix] = (struct rcsv){x, y, z, (float)t};
            }
            return;
        }
}

/* Chunk.getSavedLightValue (render_blocks.c saved_light over the engine's
 * chunk): a missing section is sky 15 at or above the height map; a world
 * without sky has none */
static inline void chunk_light(const struct rc *k, const struct chunk *c, const struct chunk_sec *s, int x, int y, int z,
                               int *sky, int *block)
{
    if (y < 0) { *sky = 0, *block = 0; return; }
    if (y > 255) y = 255;
    if (c == NULL) { *sky = k->no_sky ? 0 : 15, *block = 0; return; }
    if (!(c->mask >> (y >> 4) & 1) || s == NULL)
    {
        *block = 0;
        *sky = !k->no_sky && !(c->mask >> (y >> 4) & 1) && y >= c->height[(z & 15) << 4 | (x & 15)] ? 15 : 0;
        return;
    }
    int i = SEC_XYZ(x & 15, y, z & 15);
    *sky = k->no_sky ? 0 : nibble_get(chunk_sec_sky(s), i);
    *block = nibble_get(chunk_sec_blocklight(s), i);
}

/* the reference's reads: each through the world's chunk lookup */
static int cell_ref(const struct rc *k, int x, int y, int z, int *meta)
{
    if (y < 0 || y > 255) return *meta = 0, 0;
    struct chunk *c = world_chunk(k->w, x >> 4, z >> 4);
    if (c == NULL) return *meta = 0, 0;
    int id = chunk_get_block(c, x & 15, y, z & 15);
    *meta = id ? chunk_get_meta(c, x & 15, y, z & 15) : 0;
    return id;
}

static void light_ref(const struct rc *k, int x, int y, int z, int *sky, int *block)
{
    struct chunk *c = world_chunk(k->w, x >> 4, z >> 4);
    int yy = y < 0 ? 0 : y > 255 ? 255 : y;
    chunk_light(k, c, c ? chunk_sec_at(c, yy >> 4) : NULL, x, y, z, sky, block);
}

static int biome_ref(const struct rc *k, int x, int z)
{
    struct chunk *c = world_chunk(k->w, x >> 4, z >> 4);
    return c ? c->biome[(z & 15) << 4 | (x & 15)] : 0;
}

/* the fast path's: the cast's own table */
static inline int col_of(const struct rc *k, int x, int z)
{
    unsigned dx = (unsigned)((x >> 4) - k->cx0), dz = (unsigned)((z >> 4) - k->cz0);
    return dx < (unsigned)k->ncx && dz < (unsigned)k->ncz ? (int)(dx * (unsigned)k->ncz + dz) : -1;
}

static inline const struct chunk_sec *sec_fast(const struct rc *k, int x, int y, int z)
{
    int c = col_of(k, x, z);
    return c >= 0 ? k->sec[c * 16 + (y >> 4)] : NULL;
}

static inline int cell_fast(const struct rc *k, int x, int y, int z, int *meta)
{
    const struct chunk_sec *s = sec_fast(k, x, y, z);
    if (s == NULL) return *meta = 0, 0;
    int i = SEC_XYZ(x & 15, y, z & 15);
    int id = chunk_sec_id(s, i);
    *meta = id ? nibble_get(chunk_sec_metas(s), i) : 0;
    return id;
}

static inline void light_fast(const struct rc *k, int x, int y, int z, int *sky, int *block)
{
    int c = col_of(k, x, z);
    if (c < 0)
    {
        light_ref(k, x, y, z, sky, block);
        return;
    }
    if (y < 0)
    {
        *sky = 0, *block = 0;
        return;
    }
    /* the section from sections()' table (the mask's sections, NULL elsewhere): chunk_light's own reads when it has
     * one, its height-map rule when not */
    int yy = y > 255 ? 255 : y;
    const struct chunk_sec *s = k->sec[c * 16 + (yy >> 4)];
    if (s != NULL)
    {
        int i = SEC_XYZ(x & 15, yy, z & 15);
        *sky = k->no_sky ? 0 : nibble_get(chunk_sec_sky(s), i);
        *block = nibble_get(chunk_sec_blocklight(s), i);
        return;
    }
    chunk_light(k, k->col[c], NULL, x, y, z, sky, block);
}

static inline int biome_fast(const struct rc *k, int x, int z)
{
    int c = col_of(k, x, z);
    if (c < 0) return biome_ref(k, x, z);
    return k->col[c] ? k->col[c]->biome[(z & 15) << 4 | (x & 15)] : 0;
}

/* the block's colour multiplier on a face, 0..1 */
static inline const float *tint_of(const struct rc *k, int id, int meta, int side, int x, int y, int z, int fast,
                                   float tmp[3])
{
    static const float white[3] = {1.0f, 1.0f, 1.0f};
    int t = T.tint[id];
    if (t == TN_NONE) return white;
    int bio = fast ? biome_fast(k, x, z) : biome_ref(k, x, z);
    switch (t)
    {
    case TN_GRASS_TOP: return side == 1 ? T.grass[bio] : white;
    case TN_GRASS: return T.grass[bio];
    case TN_FOLIAGE: return T.foliage[bio];
    case TN_OLDLEAF:
        if ((meta & 3) == 1) { rgb01(6396257, tmp); return tmp; }
        if ((meta & 3) == 2) { rgb01(8431445, tmp); return tmp; }
        return T.foliage[bio];
    case TN_WATER: return T.water[bio];
    case TN_TALLGRASS: return meta == 0 ? white : T.grass[bio];
    case TN_STEM: rgb01(meta * 32 << 16 | (255 - meta * 8) << 8 | meta * 4, tmp); return tmp;
    case TN_DPLANT:
    {
        int v = meta & 7;
        if (meta & 8)
        {
            int m2 = 0;
            (void)(fast ? cell_fast(k, x, y - 1, z, &m2) : cell_ref(k, x, y - 1, z, &m2));
            v = m2 & 7;
        }
        return v == 2 || v == 3 ? T.grass[bio] : white;
    }
    }
    return white;
}

/* e to the X (X <= 0) as a float, in plain arithmetic, the same on every
 * machine (a libm's exp need not be): 2^n times a degree-8 series of the
 * rest, rounded to float. The definition is Horner's series in double (the
 * device's exp_neg, cuda/raycast/cast.cu); here the series goes by Estrin's
 * scheme (four multiply-adds deep, not nine: the underwater casts' fog waited
 * on it), and both forms are within 3e-15 of its exact value (p near 1, 2^n
 * exact, the float normal), so they round to the same float unless p is
 * within 4096 double ulps (9e-13) of the midpoint between two floats (its 29
 * dropped bits near 1 << 28); there, about 1 in 65,000, Horner's p decides.
 * 3.2 billion X in [-40, 0] gave the same floats, the widest gap 2 ulps. */
static inline float exp_neg_f(double x)
{
    if (!(x > -40.0)) return 0.0f;
    int n = (int)(x * 1.4426950408889634 - 0.5);
    double r = x - (double)n * 0.6931471805599453;
    double r2 = r * r, r4 = r2 * r2;
    double a = 1.0 + r, b = 0.5 + r * (1.0 / 6.0), c = 1.0 / 24.0 + r * (1.0 / 120.0), d = 1.0 / 720.0 + r * (1.0 / 5040.0);
    double p = (a + r2 * b) + r4 * ((c + r2 * d) + r4 * (1.0 / 40320.0));
    union { double d; uint64_t u; } pb = {p};
    if ((uint32_t)((pb.u & 0x1fffffffu) - ((1u << 28) - 4096u)) <= 8192u)
        p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6.0 + r * (1.0 / 24.0 + r * (1.0 / 120.0 + r * (1.0 / 720.0 + r * (1.0 / 5040.0 + r * (1.0 / 40320.0))))))));
    union { uint64_t u; double d; } s = {(uint64_t)(n + 1023) << 52};
    return (float)(p * s.d);
}

/* The FP32 instance's e to the X (X <= 0), every operation a float's (raycast.h's definition, the device's
 * exp_neg32): 0 at X <= -40; else n = (int)(X * 1.44269502f - 0.5f) (truncated, so n is X log2 e rounded up from
 * a half below), r = (X - n * 0.693145752f) - n * 1.42860677e-6f (ln 2 in two parts, the first's product exact),
 * p = Horner's degree-6 series 1 + r (1 + r (0.5 + r (0.166666672f + r (0.0416666679f + r (0.00833333377f
 * + r 0.00138888892f))))), and p times 2^n (exact: n >= -58). Within 3e-7 of e^X relative; the colours it
 * scales are bytes. (unused in the FP64 instance: clang's -Wunused-function) */
static inline __attribute__((unused)) float exp_neg32(float x)
{
    if (!(x > -40.0f)) return 0.0f;
    int n = (int)(x * 1.44269502f - 0.5f);
    float r = (x - (float)n * 0.693145752f) - (float)n * 1.42860677e-6f;
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.166666672f + r * (0.0416666679f + r * (0.00833333377f + r * 0.00138888892f)))));
    union { uint32_t u; float f; } s = {(uint32_t)(n + 127) << 23};
    return p * s.f;
}
#if RC_FP < 64
#define RC_EXPNEG(x) exp_neg32(x)
#else
#define RC_EXPNEG(x) exp_neg_f(x)
#endif

/* vanilla's face shading: DOWN, UP, NORTH, SOUTH, WEST, EAST */
static const float SHADE[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};

/* lit()'s colour from its parts: the texel times the tint (rt), the lightmap entry, the face's shade and the fog's
 * factor; the kept frame's shading again (reshade) takes the same function */
static inline void lit_apply(const struct rc *k, const float rt[3], int li, float shade, float f, float out[3])
{
    const float *l = k->lm[li];
    out[0] = rt[0] * l[0] * shade;
    out[1] = rt[1] * l[1] * shade;
    out[2] = rt[2] * l[2] * shade;
    out[0] = out[0] * f + k->fog[0] * (1.0f - f);
    out[1] = out[1] * f + k->fog[1] * (1.0f - f);
    out[2] = out[2] * f + k->fog[2] * (1.0f - f);
}

/* the frame's shading as the walk made it (the kept frame's, rcrec): a layer the walk lit, in the pixel's order */
static inline void rec_push(const struct rc *k, int kind, const float rt[3], int li, float shade, float f)
{
    struct rcrec *r = k->rec;
    if (r == NULL) return;
    if (r->at >= r->cap) { r->over = 1; return; }
    struct rclayer *L = &r->lay[r->at++];
    L->rt[0] = rt[0], L->rt[1] = rt[1], L->rt[2] = rt[2];
    L->shade = shade, L->f = f, L->a = 0.0f;
    L->li = (uint8_t)li, L->kind = (uint8_t)kind;
}

/* the last layer was a surface the ray goes on through at alpha a */
static inline void rec_thru(const struct rc *k, float a)
{
    struct rcrec *r = k->rec;
    if (r == NULL || r->over || r->at == 0) return;
    r->lay[r->at - 1].kind = RL_THRU;
    r->lay[r->at - 1].a = a;
}

/* a lit, fogged colour (0..255) from a texel or flat colour (inlined: out of line it took 1.8% of the bench's cycles
 * once its record grew it) */
static inline __attribute__((always_inline)) void lit(const struct rc *k, float r, float g, float b, const float *tint,
                                                      int sky, int block, int emit, float shade, rcr depth,
                                                      float out[3])
{
    if (block < emit) block = emit;
    const int li = sky * 16 + block;
    RC_USE_LM(k, li);
    const float rt[3] = {r * tint[0], g * tint[1], b * tint[2]};
    float f;
    if (k->exp_fog) f = RC_EXPNEG(-(rcr)k->fd * depth);
    else
    {
        f = (k->fe - (float)depth) * k->fspan;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    /* at f 1 the fog's term is 0 (its colour is finite) */
    if (!(f >= 1.0f)) RC_USE_FOG(k);
    lit_apply(k, rt, li, shade, f, out);
    rec_push(k, RL_LIT, rt, li, shade, f);
}

/* a surface the ray goes on through: its colour col at alpha a over what is behind */
static inline void thru_add(float acc[3], float *trans, float a, const float col[3])
{
    acc[0] += *trans * a * col[0];
    acc[1] += *trans * a * col[1];
    acc[2] += *trans * a * col[2];
    *trans *= 1.0f - a;
}

/* the miss's sky plane: the sky's colour fogged at f */
static inline void sky_mix(const struct rc *k, float f, float col[3])
{
    col[0] = k->sky[0] * f + k->fog[0] * (1.0f - f);
    col[1] = k->sky[1] * f + k->fog[1] * (1.0f - f);
    col[2] = k->sky[2] * f + k->fog[2] * (1.0f - f);
}

/* the pixel's bytes: the surfaces passed through, then what the ray ended on, the eye's overlay */
static inline void pixel_out(const struct rc *k, const float acc[3], float trans, const float col[3], uint8_t *out)
{
    uint8_t b[3];
    for (int c = 0; c < 3; ++c)
    {
        float v = acc[c] + trans * col[c];
        if (k->uw) v = v * 0.5f + k->uw_rgb[c] * 0.5f;
        b[c] = (uint8_t)(v >= 255.0f ? 255 : v <= 0.0f ? 0 : (int)(v + 0.5f));
    }
    /* client.channels 1: the luma of the same RGB bytes, BT.601 in 8 bits: (77R + 150G + 29B + 128) >> 8 */
    if (k->ch == 1) out[0] = (uint8_t)((77u * b[0] + 150u * b[1] + 29u * b[2] + 128u) >> 8);
    else out[0] = b[0], out[1] = b[1], out[2] = b[2];
}

/* a face's texel at local (lx, ly, lz) in the cell: u and v as the
 * renderer's faces map them (DOWN and UP: x, z; NORTH: -x, -y; SOUTH: x,
 * -y; WEST: z, -y; EAST: -z, -y) */
static inline uint32_t texel(uint16_t spr, int face, rcr lx, rcr ly, rcr lz)
{
    rcr u, v;
    switch (face)
    {
    case 0: case 1: u = lx, v = lz; break;
    case 2: u = RC_K(1.0) - lx, v = RC_K(1.0) - ly; break;
    case 3: u = lx, v = RC_K(1.0) - ly; break;
    case 4: u = lz, v = RC_K(1.0) - ly; break;
    default: u = RC_K(1.0) - lz, v = RC_K(1.0) - ly; break;
    }
    int iu = (int)(u * RC_K(16.0)), iv = (int)(v * RC_K(16.0));
    iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
    iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
    return T.tex[spr][iv * 16 + iu];
}

#define TEX_A(t) ((t) >> 24)
#define TEX_PASS(t) (TEX_A(t) > 25u)

/* the face a ray along +axis or -axis enters through */
static inline int entry_face(int axis, int s)
{
    return axis == 0 ? (s > 0 ? 4 : 5) : axis == 1 ? (s > 0 ? 0 : 1) : (s > 0 ? 2 : 3);
}

/* the DDA's state: the cell, each axis' step, its reciprocal direction and
 * its next boundary's t (from the cell's index alone, so a jump over cells
 * lands where cell-by-cell steps would) */
struct dda {
    int ix, iy, iz, sx, sy, sz;
    rcr idx, idy, idz, tx, ty, tz;
    rcr dx, dy, dz;             /* the direction: a jump's first guess at an axis' cell */
};

/* an axis' boundary t from its cell (every step's, jump's and run's): (c(i + (s > 0)) - o) * id, c the cell's
 * coordinate in the frame (RC_CX) */
#define DDA_TX(k, g) ((RC_CX(k, (g)->ix + ((g)->sx > 0)) - (k)->ox) * (g)->idx)
#define DDA_TY(k, g) ((RC_CY(k, (g)->iy + ((g)->sy > 0)) - (k)->oy) * (g)->idy)
#define DDA_TZ(k, g) ((RC_CZ(k, (g)->iz + ((g)->sz > 0)) - (k)->oz) * (g)->idz)

/* one step to the next cell: its entry t and the axis crossed (ties x, then y, then z) */
/* (each boundary's coordinate in the frame, for RC_TLE) */
#define DDA_BX(k, g) ((g)->ix + ((g)->sx > 0) - (k)->bx)
#define DDA_BY(k, g) ((g)->iy + ((g)->sy > 0) - (k)->by)
#define DDA_BZ(k, g) ((g)->iz + ((g)->sz > 0) - (k)->bz)
static inline __attribute__((always_inline)) rcr dda_step(const struct rc *k, struct dda *g, int *axis)
{
    rcr t;
    if (RC_TLE(g->tx, DDA_BX(k, g), k->ox, g->dx, g->ty, DDA_BY(k, g), k->oy, g->dy) &&
        RC_TLE(g->tx, DDA_BX(k, g), k->ox, g->dx, g->tz, DDA_BZ(k, g), k->oz, g->dz))
    {
        t = g->tx;
        g->ix += g->sx;
        g->tx = DDA_TX(k, g);
        *axis = 0;
    }
    else if (RC_TLE(g->ty, DDA_BY(k, g), k->oy, g->dy, g->tz, DDA_BZ(k, g), k->oz, g->dz))
    {
        t = g->ty;
        g->iy += g->sy;
        g->ty = DDA_TY(k, g);
        *axis = 1;
    }
    else
    {
        t = g->tz;
        g->iz += g->sz;
        g->tz = DDA_TZ(k, g);
        *axis = 2;
    }
    return t;
}

/* where cell-by-cell steps along one axis (origin O, 1 / direction ID,
 * direction D, step S) from cell I stop when the box's exit event at TE
 * comes: past every boundary crossed before it (at it too when WIN, the
 * axis winning the tie), never past LAST. The ray's coordinate at TE
 * guesses the cell; the boundaries' own t settle it, as the steps would.
 * B: the frame's origin cell on the axis (RC_CX's; 0 in FP64). */
#if RC_FP == 32
/* the guess is the float's p = o + tE d, off the steps' coordinate by a few float ulps of the coordinates (under
 * 1e-4 of a block out to 300 blocks from the eye; the window is at most 200): trusted 1/1024 clear of a boundary */
#define RC_ADV_EPS 0.0009765625f
#elif RC_FP == 16
/* (FP16: a half's ulp at 128 is 1/8 and its t's errors a few of them, so the guess is never trusted: 0.5 leaves no
 * fraction clear, and the boundaries' own t settle every advance, monotone in the cell as any rounding keeps them) */
#define RC_ADV_EPS RC_K(0.5)
#else
/* well inside a cell (the boundaries' t carry errors near 1e-14 of a block, the guess near 1e-8 at the world's
 * edge), the guess is the steps' cell; near a boundary the boundaries decide */
#define RC_ADV_EPS 1e-6
#endif
#if RC_FIX
/* a boundary at t (coordinate b) crossed before the exit event at tE (bE), or at it when win */
static inline int adv_crossed(rcr t, int b, rcr o, rcr d, rcr tE, int bE, rcr oE, rcr dE, int win)
{
    const int c = rc_fx_cmp(t, b, o, d, tE, bE, oE, dE);
    return c < 0 || (c == 0 && win);
}
#endif
/* (bE, oE, dE: the exit event's boundary coordinate, eye offset and direction, the fixed instance's exact order) */
static inline int dda_advance(rcr o, rcr id, rcr d, int i, int s, int last, rcr tE, int win, int B, int bE, rcr oE, rcr dE)
{
    rcr p = o + tE * d;
    int f = (int)p;
    if ((rcr)f > p) --f;
    rcr fr = p - (rcr)f;
    f += B;
    int in = s > 0 ? f >= i && f <= last : f <= i && f >= last;
    if (in && fr > RC_ADV_EPS && fr < RC_K(1.0) - RC_ADV_EPS) return f;
    if (s > 0) f = f < i ? i : f > last ? last : f;
    else f = f > i ? i : f < last ? last : f;
#if RC_FIX
#define ADV_CROSSED(j) adv_crossed(((rcr)((j) + (s > 0) - B) - o) * id, (j) + (s > 0) - B, o, d, tE, bE, oE, dE, win)
#else
    (void)bE, (void)oE, (void)dE;
#define ADV_CROSSED(j) (((rcr)((j) + (s > 0) - B) - o) * id < tE || ((((rcr)((j) + (s > 0) - B) - o) * id == tE) && win))
#endif
    while (f != last && ADV_CROSSED(f)) f += s;
    while (f != i && !ADV_CROSSED(f - s)) f -= s;
#undef ADV_CROSSED
    return f;
}

/* on to the last cell of the aligned box of side B (16 a section, a power of
 * two) holding the current cell before the ray leaves it, in one jump: the
 * box's exit is the least of the three boundary events, each other axis
 * advanced past every event the steps would have taken before it */
static inline __attribute__((always_inline)) void dda_jump(const struct rc *k, struct dda *g, int B)
{
    rcr tE = INFINITY, oE = RC_K(0.0), dE = RC_K(0.0);
    int aE = -1, bE = 0;
    int lx = g->sx > 0 ? g->ix | (B - 1) : g->ix & ~(B - 1);
    int ly = g->sy > 0 ? g->iy | (B - 1) : g->iy & ~(B - 1);
    int lz = g->sz > 0 ? g->iz | (B - 1) : g->iz & ~(B - 1);
    if (g->sx)
    {
        rcr e = (RC_CX(k, lx + (g->sx > 0)) - k->ox) * g->idx;
        tE = e, aE = 0, bE = lx + (g->sx > 0) - k->bx, oE = k->ox, dE = g->dx;
    }
    if (g->sy)
    {
        rcr e = (RC_CY(k, ly + (g->sy > 0)) - k->oy) * g->idy;
        const int b = ly + (g->sy > 0) - k->by;
        if (RC_TLT(e, b, k->oy, g->dy, tE, bE, oE, dE)) tE = e, aE = 1, bE = b, oE = k->oy, dE = g->dy;
    }
    if (g->sz)
    {
        rcr e = (RC_CZ(k, lz + (g->sz > 0)) - k->oz) * g->idz;
        const int b = lz + (g->sz > 0) - k->bz;
        if (RC_TLT(e, b, k->oz, g->dz, tE, bE, oE, dE)) tE = e, aE = 2, bE = b, oE = k->oz, dE = g->dz;
    }
    if (aE == 0) g->ix = lx, g->tx = tE;
    else if (g->sx) g->ix = dda_advance(k->ox, g->idx, g->dx, g->ix, g->sx, lx, tE, aE > 0, k->bx, bE, oE, dE), g->tx = DDA_TX(k, g);
    if (aE == 1) g->iy = ly, g->ty = tE;
    else if (g->sy) g->iy = dda_advance(k->oy, g->idy, g->dy, g->iy, g->sy, ly, tE, aE > 1, k->by, bE, oE, dE), g->ty = DDA_TY(k, g);
    if (aE == 2) g->iz = lz, g->tz = tE;
    else if (g->sz) g->iz = dda_advance(k->oz, g->idz, g->dz, g->iz, g->sz, lz, tE, 0, k->bz, bE, oE, dE), g->tz = DDA_TZ(k, g);
}

/* The fast loop's run through sections with no blocks (lane/castmove), from the cell (*ix, *iy, *iz) in one: the
 * ray steps section by section (a section's boundary is a cell's: its t is the cell walk's own expression, the
 * axis chosen by the same compares), so it enters the same sections in the same order as cell steps with a jump
 * a section would. 0: its run ends first (past tlim, or out of y 0 .. ytop), with *t the ending step's. 1: it
 * steps into a section with blocks across *axis at *t: the cell on that axis is the section's first, on the others
 * dda_advance's from the section the ray is in there (from the starting cell while that is the start's), each
 * boundary t from its cell, as the jumps and the step after the last would leave them. */
#if defined(RC_BRICK_COUNT)
/* the measurement build (lane/castexact): of the ids the air walks read (the lanes', the fast loop's), how many lie in
 * a 4x4x4 brick of air, counted in proc_malloc memory and printed at exit */
static uint64_t *rbc;
static pthread_once_t rbc_once = PTHREAD_ONCE_INIT;
static void rbc_print(void)
{
    if (rbc)
        fprintf(stderr, "rcbrick lanes reads %llu in air bricks %llu, lane steps %llu; scalar reads %llu in air bricks %llu; "
                "sections with blocks read %llu of them all air in 64 bricks %llu\n",
                (unsigned long long)rbc[0], (unsigned long long)rbc[1], (unsigned long long)rbc[2],
                (unsigned long long)rbc[3], (unsigned long long)rbc[4], (unsigned long long)rbc[5],
                (unsigned long long)rbc[6]);
}
static void rbc_init(void)
{
    if ((rbc = proc_calloc(8, sizeof *rbc)) != NULL) atexit(rbc_print);
}
static int brick_air(const uint8_t *ids, int ci)
{
    const int x0 = ci >> 8 & 12, z0 = ci >> 4 & 12, y0 = ci & 12;
    for (int x = x0; x < x0 + 4; ++x)
        for (int z = z0; z < z0 + 4; ++z)
            for (int y = y0; y < y0 + 4; ++y)
                if (ids[x << 8 | z << 4 | y]) return 0;
    return 1;
}
#define RBC_SCALAR(ids, ci) (rbc ? (__atomic_add_fetch(&rbc[3], 1, __ATOMIC_RELAXED), \
                                    brick_air(ids, ci) ? (void)__atomic_add_fetch(&rbc[4], 1, __ATOMIC_RELAXED) : (void)0) : (void)0)
#else
#define RBC_SCALAR(ids, ci) ((void)0)
#endif
#if defined(RC_RUN_VERIFY)
static uint64_t *rrv;           /* rays_verify_run's counts */
#define RRV_ADD(i) (rrv && !k->no_run ? (void)__atomic_add_fetch(&rrv[i], 1, __ATOMIC_RELAXED) : (void)0)
#else
#define RRV_ADD(i) ((void)0)
#endif
static inline __attribute__((always_inline)) int empty_run(const struct rc *k, int *ix, int *iy, int *iz, rcr *tx,
                                                          rcr *ty, rcr *tz, rcr *t, int *axis, int sx, int sy,
                                                          int sz, rcr idx, rcr idy, rcr idz, rcr dx,
                                                          rcr dy, rcr dz, unsigned ytop, rcr tlim)
{
    const rcr ox = k->ox, oy = k->oy, oz = k->oz;
    int cx = *ix >> 4, cy = *iy >> 4, cz = *iz >> 4;
    /* the cell each axis' advance starts from: the start's while the ray is in its section on that axis */
    int fx = *ix, fy = *iy, fz = *iz;
    rcr ex = sx ? (RC_CX(k, (cx + (sx > 0)) * 16) - ox) * idx : INFINITY;
    rcr ey = sy ? (RC_CY(k, (cy + (sy > 0)) * 16) - oy) * idy : INFINITY;
    rcr ez = sz ? (RC_CZ(k, (cz + (sz > 0)) * 16) - oz) * idz : INFINITY;
    RRV_ADD(2);
    for (;;)
    {
        int a, bE;
        rcr te, oE, dE;
        /* (each axis' next section boundary's coordinate in the frame, for RC_TLE) */
        const int bx = (cx + (sx > 0)) * 16 - k->bx, by = (cy + (sy > 0)) * 16 - k->by, bz = (cz + (sz > 0)) * 16 - k->bz;
        if (RC_TLE(ex, bx, ox, dx, ey, by, oy, dy) && RC_TLE(ex, bx, ox, dx, ez, bz, oz, dz))
        {
            te = ex, a = 0, cx += sx, bE = bx, oE = ox, dE = dx;
            fx = sx > 0 ? cx * 16 : cx * 16 + 15;
            ex = (RC_CX(k, (cx + (sx > 0)) * 16) - ox) * idx;
        }
        else if (RC_TLE(ey, by, oy, dy, ez, bz, oz, dz))
        {
            te = ey, a = 1, cy += sy, bE = by, oE = oy, dE = dy;
            fy = sy > 0 ? cy * 16 : cy * 16 + 15;
            ey = (RC_CY(k, (cy + (sy > 0)) * 16) - oy) * idy;
            if ((unsigned)fy >= ytop) return *t = te, 0;
        }
        else
        {
            te = ez, a = 2, cz += sz, bE = bz, oE = oz, dE = dz;
            fz = sz > 0 ? cz * 16 : cz * 16 + 15;
            ez = (RC_CZ(k, (cz + (sz > 0)) * 16) - oz) * idz;
        }
        if (te >= tlim) return *t = te, 0;
        unsigned dcx = (unsigned)(cx - k->cx0), dcz = (unsigned)(cz - k->cz0);
        if (dcx >= (unsigned)k->ncx || dcz >= (unsigned)k->ncz || k->sec[(dcx * (unsigned)k->ncz + dcz) * 16 + (unsigned)cy] == NULL)
            continue;
        /* into a section with blocks: the steps' cell on the other axes at te (dda_jump's advance) */
        if (a != 0 && sx) *ix = dda_advance(ox, idx, dx, fx, sx, sx > 0 ? fx | 15 : fx & ~15, te, a > 0, k->bx, bE, oE, dE);
        else *ix = fx;
        if (a != 1 && sy) *iy = dda_advance(oy, idy, dy, fy, sy, sy > 0 ? fy | 15 : fy & ~15, te, a > 1, k->by, bE, oE, dE);
        else *iy = fy;
        if (a != 2 && sz) *iz = dda_advance(oz, idz, dz, fz, sz, sz > 0 ? fz | 15 : fz & ~15, te, 0, k->bz, bE, oE, dE);
        else *iz = fz;
        if (sx) *tx = (RC_CX(k, *ix + (sx > 0)) - ox) * idx;
        if (sy) *ty = (RC_CY(k, *iy + (sy > 0)) - oy) * idy;
        if (sz) *tz = (RC_CZ(k, *iz + (sz > 0)) - oz) * idz;
        *t = te, *axis = a;
        RRV_ADD(3);
        return 1;
    }
}

/* the water's surface seen from below by a ray going up out of the water the eye is in, stepping up into cell
 * (ix, iy, iz) at t: lit by the cell it left, blended over what is behind */
static inline __attribute__((always_inline)) void surface_up(const struct rc *k, int ix, int iy, int iz, rcr t, rcr dx,
                                                            rcr dz, rcr fwd, int fast, float acc[3], float *trans)
{
    int sky, block;
    if (fast) light_fast(k, ix, iy - 1, iz, &sky, &block);
    else light_ref(k, ix, iy - 1, iz, &sky, &block);
    uint32_t tw = texel(T.water_spr, 1, k->ox + dx * t - RC_CX(k, ix), RC_K(0.0), k->oz + dz * t - RC_CZ(k, iz));
    float tmp[3], col[3];
    const float *tint = tint_of(k, 9, 0, 1, ix, iy - 1, iz, fast, tmp);
    lit(k, (float)(tw & 255), (float)(tw >> 8 & 255), (float)(tw >> 16 & 255), tint, sky, block, 0, SHADE[1], t * fwd, col);
    float a = (float)TEX_A(tw) / 255.0f;
    rec_thru(k, a);
    thru_add(acc, trans, a, col);
}

/* walk hands ray q on to rays_on (sfa 3 in water, 4 through air) from its cell and boundaries g, its colour so far */
static inline __attribute__((always_inline)) void hand_on(struct rcrays *R, int q, const struct dda *g, const float acc[3],
                                                          float trans, int sfa)
{
    R->ix[q] = g->ix, R->iy[q] = g->iy, R->iz[q] = g->iz, R->tx[q] = g->tx, R->ty[q] = g->ty, R->tz[q] = g->tz;
    R->wacc[0][q] = acc[0], R->wacc[1][q] = acc[1], R->wacc[2][q] = acc[2], R->wtr[q] = trans;
    R->sfa[q] = sfa, R->wq[R->nw++] = q;
}

/* one ray: the same arithmetic for both casts; fast only changes how a cell
 * is read and crosses air without the general per-cell work (an empty
 * section in one jump) */
static inline __attribute__((always_inline)) void walk(const struct rc *k, struct rcrays *R, int q, int fast,
                                                       uint8_t *out)
{
    rcr dx = R->dx[q], dy = R->dy[q], dz = R->dz[q];
    rcr fwd = R->fwd[q];
    struct dda g;
    g.ix = R->ix[q], g.iy = R->iy[q], g.iz = R->iz[q];
    g.sx = (dx > 0) - (dx < 0), g.sy = (dy > 0) - (dy < 0), g.sz = (dz > 0) - (dz < 0);
    g.idx = R->idx[q], g.idy = R->idy[q], g.idz = R->idz[q];
    g.dx = dx, g.dy = dy, g.dz = dz;
    g.tx = R->tx[q], g.ty = R->ty[q], g.tz = R->tz[q];
    const int sx = g.sx, sy = g.sy, sz = g.sz;
    const rcr idx = g.idx, idy = g.idy, idz = g.idz;
    rcr range = (rcr)k->range;
    /* the nearest entity box this pixel can see (rays_fill) */
    const rcr te = R->te[q];
    const int ent = R->ent[q], eface = R->eface[q];

    float acc[3] = {0, 0, 0}, trans = 1.0f, col[3];
    int in_water = 0, cull = -1;
    const int eye_liquid = k->liquid;
    int eye_was_liquid = eye_liquid != 0;
    /* the fast path's runs: y past ytop ends one; t past tlim (the entity's, or
     * the range's: t > range) too; the cell index a step on each axis
     * enters a section at (e*), and its change in a band */
    const unsigned ytop = sy >= 0 ? (unsigned)k->top : 256u;
    const rcr tlim = R->tlim[q];
    const int ex = sx > 0 ? 0 : 15, ey = sy > 0 ? 0 : 15, ez = sz > 0 ? 0 : 15;
    const int dcx = sx * 256, dcz = sz * 16;
    /* the eye in water: a ray going up out of it meets the surface (the general path) */
    const int up_out = eye_liquid == 8 && sy > 0;
    /* the fast path's section of the current cell */
    const struct chunk_sec *cs = NULL;
    int csx = INT32_MIN, csy = INT32_MIN, csz = INT32_MIN;
    int axis = 0, id, meta;
    rcr t = RC_K(0.0);
    int rr_kind = 0;            /* raycast_rays' kind: 0 a block, 1 the sky or fog, 2 an entity */
    if (R->sfa[q])
    {
        /* the air prefix took it up out of the eye's water, by a step up through the surface (drawn here first), and
         * on through air */
        eye_was_liquid = 0;
        if (R->sfa[q] == 2 && T.water_spr != 0xffff)
            surface_up(k, R->sfx[q], R->sfy[q], R->sfz[q], R->sft[q], dx, dz, fwd, fast, acc, &trans);
        else if (R->sfa[q] >= 3)
        {
            /* on from rays_on: the colour walk had when it handed the ray there, in the water still (3) or not */
            acc[0] = R->wacc[0][q], acc[1] = R->wacc[1][q], acc[2] = R->wacc[2][q], trans = R->wtr[q];
            in_water = R->sfa[q] == 3;
        }
    }
    if (R->how[q] == RC_HIT)
    {
        /* the air prefix took the step onto a block (the fast loop's take, the run's state as it leaves it) */
        axis = R->axis[q], t = R->t[q];
        cs = sec_fast(k, g.ix, g.iy, g.iz), csx = g.ix >> 4, csy = g.iy >> 4, csz = g.iz >> 4;
        id = cs->ids[SEC_XYZ(g.ix & 15, g.iy, g.iz & 15)];
        meta = nibble_get(chunk_sec_metas(cs), SEC_XYZ(g.ix & 15, g.iy, g.iz & 15));
        goto block;
    }
    if (R->how[q] == RC_END) goto miss;    /* the run ended with no block (the fast loop's run_end) */
    if (fast && g.iy >= k->top && sy >= 0) goto miss;
    for (;;)
    {
        meta = 0;
        if (fast && eye_liquid != 10 && (unsigned)g.iy <= 255u)
        {
            /* air (and water the ray sees through) cell by cell, an empty
             * section in one jump, until a block, the ray's end or something
             * the general path decides; the DDA in locals, the section and
             * cell index worked out again only as the ray enters a section
             * (a jump leaves the ray on the section's last cell, so the next
             * step enters one). inw, cl and ewl are in_water, cull and
             * eye_was_liquid as the general path would leave them. 4x4x4
             * bricks of air crossed in one jump cost more than they saved
             * (lane/raycast: 1.91 M instructions a 32x32 cast against 1.65). */
            int ix = g.ix, iy = g.iy, iz = g.iz;
            rcr tx = g.tx, ty = g.ty, tz = g.tz;
            const rcr ox = k->ox, oy = k->oy, oz = k->oz;
            int found = 0, inw = in_water, cl = cull, ewl = eye_was_liquid, si, ci;
            int c0 = col_of(k, ix, iz);
            si = c0 * 16 + (iy >> 4);
            const struct chunk_sec *cur = c0 >= 0 ? k->sec[si] : NULL;
            const uint8_t *hi = cur ? cur->ids_hi : NULL;   /* ids past 255 */
            ci = SEC_XYZ(ix & 15, iy, iz & 15);
            t = RC_K(0.0);
            if (cur == NULL)
            {
                /* the current cell's section is empty: the cells after it in it are air */
                if (ewl && up_out) goto general;
                goto jump_air;
            }
            for (;;)
            {
#if RC_FIX
                /* (the boundaries' coordinates in the frame, for the exact order) */
                const int fbx = ix + (sx > 0) - k->bx, fby = iy + (sy > 0) - k->by, fbz = iz + (sz > 0) - k->bz;
#endif
                if (RC_TLE(tx, fbx, ox, dx, ty, fby, oy, dy) && RC_TLE(tx, fbx, ox, dx, tz, fbz, oz, dz))
                {
                    t = tx;
                    ix += sx;
                    tx = (RC_CX(k, ix + (sx > 0)) - ox) * idx;
                    axis = 0;
                    ci += dcx;
                    if ((ix & 15) == ex) goto boundary;
                }
                else if (RC_TLE(ty, fby, oy, dy, tz, fbz, oz, dz))
                {
                    t = ty;
                    iy += sy;
                    ty = (RC_CY(k, iy + (sy > 0)) - oy) * idy;
                    axis = 1;
                    if ((unsigned)iy >= ytop) goto run_end;
                    ci += sy;
                    if ((iy & 15) == ey) goto boundary;
                }
                else
                {
                    t = tz;
                    iz += sz;
                    tz = (RC_CZ(k, iz + (sz > 0)) - oz) * idz;
                    axis = 2;
                    ci += dcz;
                    if ((iz & 15) == ez) goto boundary;
                }
                if (t >= tlim) goto run_end;
            read:
                RBC_SCALAR(cur->ids, ci);
                id = cur->ids[ci];
                if (hi) id |= nibble_get(hi, ci) << 8;
                if (id == 0)
                {
                    if (ewl && up_out && axis == 1) goto take;
                    inw = 0, cl = -1, ewl = 0;
                    continue;
                }
                if (eye_liquid && (id == 8 || id == 9))
                {
                    /* the eye's own water: seen through */
                    cl = -1, ewl = 1;
                    continue;
                }
                if (ewl && up_out && axis == 1) goto take;
                /* on through the water the ray is already in (its surface was blended) */
                if (inw && (id == 8 || id == 9))
                {
                    ewl = 0;
                    continue;
                }
            take:
                meta = id ? nibble_get(chunk_sec_metas(cur), ci) : 0;
                found = 1;
                goto run_end;
            boundary:
                if (t >= tlim) goto run_end;
                ci = SEC_XYZ(ix & 15, iy, iz & 15);
                {
                    /* into another section */
                    int c = col_of(k, ix, iz);
                    si = c * 16 + (iy >> 4);
                    cur = c >= 0 ? k->sec[si] : NULL;
                    hi = cur ? cur->ids_hi : NULL;
                    if (cur == NULL)
                    {
                        if (ewl && up_out && axis == 1) { id = 0, meta = 0, found = 1; goto run_end; }
                        goto jump_air;
                    }
                }
                goto read;
            jump_air:
                inw = 0, cl = -1, ewl = 0;
#if defined(RC_RUN_VERIFY)
                if (k->rec == NULL && !k->no_run)
#else
                if (k->rec == NULL)
#endif
                {
                    /* the sections with no blocks on from here at the section level (empty_run), to the step into
                     * one with blocks or the run's end; a recorded frame's walk keeps the jumps (its pixels' reach
                     * is the walk's last cell) */
                    if (empty_run(k, &ix, &iy, &iz, &tx, &ty, &tz, &t, &axis, sx, sy, sz, idx, idy, idz, dx, dy, dz, ytop, tlim))
                        goto boundary;
                    goto run_end;
                }
                g.ix = ix, g.iy = iy, g.iz = iz, g.tx = tx, g.ty = ty, g.tz = tz;
                dda_jump(k, &g, 16);
                ix = g.ix, iy = g.iy, iz = g.iz, tx = g.tx, ty = g.ty, tz = g.tz;
            }
        run_end:
            in_water = inw, cull = cl, eye_was_liquid = ewl;
            g.ix = ix, g.iy = iy, g.iz = iz, g.tx = tx, g.ty = ty, g.tz = tz;
            cs = cur, csx = ix >> 4, csy = iy >> 4, csz = iz >> 4;
            if (found) goto block;
            if (t >= te) goto entity;
            goto miss;
        }
    general:
        t = dda_step(k, &g, &axis);
        if (t >= te) goto entity;
        if (!(t <= range) || g.iy > 255 || g.iy < 0) goto miss;
        if (fast && g.iy >= k->top && sy >= 0) goto miss;
        if (fast)
        {
            if ((g.ix >> 4) != csx || (g.iy >> 4) != csy || (g.iz >> 4) != csz)
            {
                csx = g.ix >> 4, csy = g.iy >> 4, csz = g.iz >> 4;
                cs = sec_fast(k, g.ix, g.iy, g.iz);
            }
            id = 0;
            if (cs != NULL)
            {
                int ci = SEC_XYZ(g.ix & 15, g.iy, g.iz & 15);
                id = chunk_sec_id(cs, ci);
                if (id) meta = nibble_get(chunk_sec_metas(cs), ci);
            }
        }
        else id = cell_ref(k, g.ix, g.iy, g.iz, &meta);
    block:;
        const int ix = g.ix, iy = g.iy, iz = g.iz;
        /* the cell the ray left */
        const int px = ix - (axis == 0 ? sx : 0), py = iy - (axis == 1 ? sy : 0), pz = iz - (axis == 2 ? sz : 0);
        int face = entry_face(axis, axis == 0 ? sx : axis == 1 ? sy : sz);
        /* up out of the water the eye is in: the surface from below */
        if (eye_was_liquid && eye_liquid == 8 && liquid_of(id) != 8 && axis == 1 && sy > 0 && T.water_spr != 0xffff)
            surface_up(k, ix, iy, iz, t, dx, dz, fwd, fast, acc, &trans);
        eye_was_liquid = eye_liquid && liquid_of(id) == eye_liquid;
        int kind = T.kind[id];
        if (kind == RK_AIR || (eye_liquid && liquid_of(id) == eye_liquid))
        {
            if (kind == RK_AIR) in_water = 0;
            cull = -1;
            if (fast && cs == NULL) dda_jump(k, &g, 16);
            continue;
        }
        rcr texit = g.tx < g.ty ? (g.tx < g.tz ? g.tx : g.tz) : (g.ty < g.tz ? g.ty : g.tz);
        rcr hx = k->ox + dx * t - RC_CX(k, ix), hy = k->oy + dy * t - RC_CY(k, iy), hz = k->oz + dz * t - RC_CZ(k, iz);
        if (kind == RK_WATER || kind == RK_LAVA)
        {
            if (kind == RK_WATER && in_water) continue;
            /* the surface: full under the same liquid, else BlockLiquid's
             * height for the level */
            int m2 = 0;
            int above = iy < 255 ? (fast ? cell_fast(k, ix, iy + 1, iz, &m2) : cell_ref(k, ix, iy + 1, iz, &m2)) : 0;
            rcr h = liquid_of(above) == liquid_of(id) ? RC_K(1.0) : RC_K(1.0) - (rcr)((meta >= 8 ? 0 : meta) + 1) / RC_K(9.0);
            rcr th = t;
            int hf = face;
            if (hy > h)
            {
                if (sy >= 0) { cull = -1; continue; }
                th = (RC_CY(k, iy) + h - k->oy) * idy;
                if (th > texit) { cull = -1; continue; }
                hf = 1;
                hx = k->ox + dx * th - RC_CX(k, ix), hy = h, hz = k->oz + dz * th - RC_CZ(k, iz);
            }
            if (th >= te) goto entity;
            int sky, block;
            if (fast) light_fast(k, ix, iy, iz, &sky, &block);
            else light_ref(k, ix, iy, iz, &sky, &block);
            uint16_t spr = kind == RK_WATER ? T.water_spr : T.lava_spr;
            uint32_t tx_ = spr != 0xffff ? texel(spr, hf, hx, hy, hz) : 0xff808080u;
            float tmp[3];
            const float *tint = tint_of(k, id, meta, hf, ix, iy, iz, fast, tmp);
            lit(k, (float)(tx_ & 255), (float)(tx_ >> 8 & 255), (float)(tx_ >> 16 & 255), tint, sky, block, T.emit[id],
                SHADE[hf], th * fwd, col);
            if (kind == RK_LAVA) goto done;
            float a = (float)TEX_A(tx_) / 255.0f;
            rec_thru(k, a);
            thru_add(acc, &trans, a, col);
            in_water = 1, cull = -1;
            if (fast && (k->susp & 1) && R->sfa[q] < 3)
            {
                /* on through the water in rays_on's lanes (a3: the fast loop goes on from this cell with in_water 1,
                 * cull -1, eye_was_liquid 0 and the eye dry, and so do they), where they can start: the cell's
                 * section in the lanes' table; and where the run is likely long (only the speed hangs on it): going
                 * down into water at least RC_WDEEP deep below the cell (the brief's shape's runs averaged 3 steps,
                 * the seed starts' 41) */
#if !defined(RC_WDEEP)
#define RC_WDEEP 4
#endif
                const int c = col_of(k, ix, iz);
                int m2;
                if (c >= 0 && k->lsec[c * 16 + (iy >> 4)] != NULL && sy < 0 && iy >= RC_WDEEP &&
                    (cell_fast(k, ix, iy - RC_WDEEP, iz, &m2) | 1) == 9)
                {
                    hand_on(R, q, &g, acc, trans, 3);
                    return;
                }
            }
            continue;
        }
        in_water = 0;
        int key = id | meta << 12;
        if (T.cull_same[id] && key == cull) continue;
        cull = -1;
        const uint16_t *faces = T.face[k->fancy] + ((size_t)id * 16 + (size_t)meta) * 6;
        if (kind == RK_CUBE)
        {
            uint16_t spr = faces[face];
            uint32_t tt = spr != 0xffff ? texel(spr, face, hx, hy, hz) : 0;
            if (TEX_PASS(tt))
            {
                int sky, block;
                if (fast) light_fast(k, px, py, pz, &sky, &block);
                else light_ref(k, px, py, pz, &sky, &block);
                float tmp[3];
                const float *tint = tint_of(k, id, meta, face, ix, iy, iz, fast, tmp);
                lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block, T.emit[id],
                    SHADE[face], t * fwd, col);
                if (k->seen) seen_note(k, id, ix, iy, iz, t);
                goto done;
            }
            if (T.cull_same[id]) cull = key;
        }
        else if (kind == RK_BOX)
        {
            const rcr *b = T.bounds + ((size_t)id * 16 + (size_t)meta) * 6;
            const rcr o3[3] = {k->ox - RC_CX(k, ix), k->oy - RC_CY(k, iy), k->oz - RC_CZ(k, iz)}, i3[3] = {idx, idy, idz};
            const int s3[3] = {sx, sy, sz};
            rcr tn = -INFINITY, tf = INFINITY;
            int ax = -1, miss = 0;
            for (int a = 0; a < 3 && !miss; ++a)
            {
                if (!s3[a])
                {
                    if (o3[a] < b[a] || o3[a] > b[a + 3]) miss = 1;
                    continue;
                }
                rcr t0 = (b[a] - o3[a]) * i3[a], t1 = (b[a + 3] - o3[a]) * i3[a];
                if (t0 > t1) { rcr s = t0; t0 = t1; t1 = s; }
                if (t0 > tn) tn = t0, ax = a;
                if (t1 < tf) tf = t1;
            }
            if (!miss && ax >= 0 && tn <= tf && tn > RC_K(0.0) && tn <= texit)
            {
                if (tn >= te) goto entity;
                int bf = entry_face(ax, s3[ax]);
                uint16_t spr = faces[bf];
                rcr bx = k->ox + dx * tn - RC_CX(k, ix), by = k->oy + dy * tn - RC_CY(k, iy), bz = k->oz + dz * tn - RC_CZ(k, iz);
                uint32_t tt = spr != 0xffff ? texel(spr, bf, bx, by, bz) : 0;
                if (TEX_PASS(tt))
                {
                    /* a face on the cell's boundary takes the light of the
                     * cell the ray left; one inside, the cell's own */
                    static const int BI[6] = {1, 4, 2, 5, 0, 3};
                    rcr bd = b[BI[bf]];
                    int edge = (bf & 1) ? bd == RC_K(1.0) : bd == RC_K(0.0);
                    int sky, block;
                    int lx = edge ? px : ix, ly = edge ? py : iy, lz = edge ? pz : iz;
                    if (fast) light_fast(k, lx, ly, lz, &sky, &block);
                    else light_ref(k, lx, ly, lz, &sky, &block);
                    float tmp[3];
                    const float *tint = tint_of(k, id, meta, bf, ix, iy, iz, fast, tmp);
                    lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block,
                        T.emit[id], SHADE[bf], tn * fwd, col);
                    goto done;
                }
            }
        }
        else /* RK_CROSS: the planes x = z and x + z = 1, 0.05 to 0.95 across, the cell's height */
        {
            rcr lx0 = k->ox - RC_CX(k, ix), lz0 = k->oz - RC_CZ(k, iz), ly0 = k->oy - RC_CY(k, iy);
            rcr tp[2] = {INFINITY, INFINITY};
            rcr a1 = dx - dz, a2 = dx + dz;
            if (a1 != RC_K(0.0)) tp[0] = -(lx0 - lz0) / a1;
            if (a2 != RC_K(0.0)) tp[1] = (RC_K(1.0) - lx0 - lz0) / a2;
            if (tp[1] < tp[0]) { rcr s = tp[0]; tp[0] = tp[1]; tp[1] = s; }
            uint16_t spr = faces[0];
            if (id == 175)
            {
                /* renderBlockDoublePlant: the half's icon by the lower half's variant */
                int v = meta & 7, m2 = 0;
                if (meta & 8)
                {
                    int below = fast ? cell_fast(k, ix, iy - 1, iz, &m2) : cell_ref(k, ix, iy - 1, iz, &m2);
                    v = below == 175 ? m2 & 7 : 6;
                }
                spr = v < 6 ? ((meta & 8) ? T.dp_high[v] : T.dp_low[v]) : 0xffff;
            }
            for (int q = 0; q < 2 && spr != 0xffff; ++q)
            {
                rcr tq = tp[q];
                if (!(tq >= t && tq <= texit)) continue;
                rcr qx = lx0 + dx * tq, qy = ly0 + dy * tq;
                if (qx < RC_K(0.05) || qx > RC_K(0.95) || qy < RC_K(0.0) || qy > RC_K(1.0)) continue;
                if (tq >= te) goto entity;
                rcr u = (qx - RC_K(0.05)) / RC_K(0.9);
                int iu = (int)(u * RC_K(16.0)), iv = (int)((RC_K(1.0) - qy) * RC_K(16.0));
                iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
                iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
                uint32_t tt = T.tex[spr][iv * 16 + iu];
                if (!TEX_PASS(tt)) continue;
                int sky, block;
                if (fast) light_fast(k, ix, iy, iz, &sky, &block);
                else light_ref(k, ix, iy, iz, &sky, &block);
                float tmp[3];
                const float *tint = tint_of(k, id, meta, 2, ix, iy, iz, fast, tmp);
                lit(k, (float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, sky, block, T.emit[id],
                    1.0f, tq * fwd, col);
                goto done;
            }
        }
        /* a block the ray passed (in_water 0, eye_was_liquid 0): a ray going up goes on through air in rays_on's
         * lanes (a4: the fast loop goes on from this cell with cull -1 as they do, air passing and any other cell
         * taken; going up it is most often on to the sky, 11.6 air steps a run on the brief's shape's long runs, under
         * one going down), where they can start */
        if (fast && (k->susp & 2) && sy > 0 && cull == -1 && R->sfa[q] < 3)
        {
            const int c = col_of(k, ix, iz);
            if (c >= 0 && k->lsec[c * 16 + (iy >> 4)] != NULL)
            {
                hand_on(R, q, &g, acc, trans, 4);
                return;
            }
        }
    }
entity:
    rr_kind = 2;
    {
        const struct rcent *en = &k->ent[ent];
        static const float white[3] = {1.0f, 1.0f, 1.0f};
        lit(k, en->rgb[0], en->rgb[1], en->rgb[2], white, en->lmi >> 4, en->lmi & 15, 0, SHADE[eface], te * fwd, col);
        goto done;
    }
miss:
    if (ent >= 0) goto entity;
    rr_kind = 1;
    {
        static const float none[3] = {0.0f, 0.0f, 0.0f};
        if (k->exp_fog || k->dim == -1) memcpy(col, k->fog, sizeof col), RC_USE_FOG(k), rec_push(k, RL_FOG, none, 0, 0.0f, 0.0f);
        else if (k->dim == 1) memcpy(col, T.end_sky, sizeof col), rec_push(k, RL_END, none, 0, 0.0f, 0.0f);
        else if (dy <= RC_K(0.0)) memcpy(col, k->fog, sizeof col), RC_USE_FOG(k), rec_push(k, RL_FOG, none, 0, 0.0f, 0.0f);
        else
        {
            /* the sky plane 16 blocks up, renderSky's fog from 0 to 0.8 of the far plane */
            rcr z = RC_K(16.0) / dy * fwd;
            float f = (k->sky_end - (float)z) / k->sky_end;
            f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
            /* at f 0 the sky's term is 0, at 1 the fog's */
            if (!(f <= 0.0f)) RC_USE_SKY(k);
            if (!(f >= 1.0f)) RC_USE_FOG(k);
            sky_mix(k, f, col);
            rec_push(k, RL_SKY, none, 0, 0.0f, f);
        }
    }
done:
    if (__builtin_expect(raycast_rayrec != NULL, 0))
    {
        struct raycast_ray *r = &raycast_rayrec[k->pix_at];
        memset(r, 0, sizeof *r);
        r->kind = (uint8_t)rr_kind;
        if (rr_kind == 0)
            r->x = g.ix, r->y = g.iy, r->z = g.iz, r->axis = (uint8_t)axis, r->dist = (int32_t)floor((double)t * 256.0);
        else if (rr_kind == 2)
            r->x = ent, r->axis = (uint8_t)eface, r->dist = (int32_t)floor((double)te * 256.0);
    }
    if (k->rec)
    {
        rcr te_ = g.tx < g.ty ? (g.tx < g.tz ? g.tx : g.tz) : (g.ty < g.tz ? g.ty : g.tz);
        k->rec->reach[k->rec->pix] = (float)te_ + 2.0f;
    }
    if (out) pixel_out(k, acc, trans, col, out);
    else
    {
        /* the batch's bytes are made together (pixels_out) */
        struct rcpix *P = k->pix;
        P->acc[0][q] = acc[0], P->acc[1][q] = acc[1], P->acc[2][q] = acc[2], P->trans[q] = trans;
        P->col[0][q] = col[0], P->col[1][q] = col[1], P->col[2][q] = col[2];
    }
}

#if defined(RC_AVX2)
/* pixels_out sixteen pixels a step in AVX-512's lanes, each lane pixel_out's operations in its order (IEEE's: the
 * same bits); a lane's byte is the truncation's low byte, as the scalar cast's (uint8_t) of it, NaN's 0 too */
__attribute__((target("avx512f,avx512bw,avx512vl"))) static void pixels_lanes(const struct rc *k, int n, uint8_t *out)
{
    const struct rcpix *P = k->pix;
    const __m512 half = _mm512_set1_ps(0.5f), f255 = _mm512_set1_ps(255.0f), zero = _mm512_setzero_ps();
    const __m512i i255 = _mm512_set1_epi32(255), izero = _mm512_setzero_si512();
    const __m512 uwc[3] = {_mm512_set1_ps(k->uw_rgb[0] * 0.5f), _mm512_set1_ps(k->uw_rgb[1] * 0.5f),
                           _mm512_set1_ps(k->uw_rgb[2] * 0.5f)};
    for (int q = 0; q < n; q += 16)
    {
        const __mmask16 m = n - q >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << (n - q)) - 1u);
        const __m512 tr = _mm512_maskz_loadu_ps(m, P->trans + q);
        __m512i b[3];
        for (int c = 0; c < 3; ++c)
        {
            __m512 v = _mm512_add_ps(_mm512_maskz_loadu_ps(m, P->acc[c] + q), _mm512_mul_ps(tr, _mm512_maskz_loadu_ps(m, P->col[c] + q)));
            if (k->uw) v = _mm512_add_ps(_mm512_mul_ps(v, half), uwc[c]);
            __m512i bi = _mm512_cvttps_epi32(_mm512_add_ps(v, half));
            bi = _mm512_mask_mov_epi32(bi, _mm512_cmp_ps_mask(v, zero, _CMP_LE_OQ), izero);
            b[c] = _mm512_mask_mov_epi32(bi, _mm512_cmp_ps_mask(v, f255, _CMP_GE_OQ), i255);
            b[c] = _mm512_and_si512(b[c], i255);
        }
        if (k->ch == 1)
        {
            /* the luma of the bytes, BT.601 in 8 bits */
            __m512i l = _mm512_add_epi32(_mm512_add_epi32(_mm512_mullo_epi32(b[0], _mm512_set1_epi32(77)),
                                                          _mm512_mullo_epi32(b[1], _mm512_set1_epi32(150))),
                                         _mm512_add_epi32(_mm512_mullo_epi32(b[2], _mm512_set1_epi32(29)), _mm512_set1_epi32(128)));
            _mm512_mask_cvtepi32_storeu_epi8(out + q, m, _mm512_srli_epi32(l, 8));
            continue;
        }
        uint8_t t[3][16];
        for (int c = 0; c < 3; ++c) _mm_storeu_si128((__m128i *)t[c], _mm512_cvtepi32_epi8(b[c]));
        const int e = n - q < 16 ? n - q : 16;
        for (int i = 0; i < e; ++i)
        {
            uint8_t *o = out + (size_t)(q + i) * 3;
            o[0] = t[0][i], o[1] = t[1][i], o[2] = t[2][i];
        }
    }
}
#endif

static int rc_lanes(void);

#if defined(RC_PIX_VERIFY) && defined(RC_AVX2)
/* the verify build: every batch's bytes from the lanes against pixel_out's, in both channel modes, counted in
 * proc_malloc memory and printed every 2^20 pixels and at exit */
static uint64_t *rpv;
static pthread_once_t rpv_once = PTHREAD_ONCE_INIT;
static void rpv_print(void)
{
    if (rpv) fprintf(stderr, "rcpix pixels %llu differ %llu\n", (unsigned long long)rpv[0], (unsigned long long)rpv[1]);
}
static void rpv_init(void)
{
    if ((rpv = proc_calloc(2, sizeof *rpv)) != NULL) atexit(rpv_print);
}
static void pixels_check(const struct rc *k, int n)
{
    pthread_once(&rpv_once, rpv_init);
    if (rpv == NULL) return;
    struct rc c = *k;
    uint8_t a[RC_BATCH * 3], b[RC_BATCH * 3];
    for (int ch = 1; ch <= 3; ch += 2)
    {
        c.ch = ch;
        pixels_lanes(&c, n, a);
        const struct rcpix *P = k->pix;
        for (int q = 0; q < n; ++q)
        {
            const float acc[3] = {P->acc[0][q], P->acc[1][q], P->acc[2][q]}, col[3] = {P->col[0][q], P->col[1][q], P->col[2][q]};
            pixel_out(&c, acc, P->trans[q], col, b + (size_t)q * (size_t)ch);
        }
        __atomic_add_fetch(&rpv[0], (uint64_t)n, __ATOMIC_RELAXED);
        if (memcmp(a, b, (size_t)n * (size_t)ch))
        {
            __atomic_add_fetch(&rpv[1], 1, __ATOMIC_RELAXED);
            fprintf(stderr, "rcpix MISMATCH: a batch's %d-channel bytes differ\n", ch);
        }
        if (__atomic_load_n(&rpv[0], __ATOMIC_RELAXED) % (1u << 20) < (uint64_t)n) rpv_print();
    }
}
#endif

/* the bytes of the batch's n pixels (walk's sums in k->pix) into out */
static void pixels_out(const struct rc *k, int n, uint8_t *out)
{
#if defined(RC_AVX2)
    if (rc_lanes())
    {
#if defined(RC_PIX_VERIFY)
        pixels_check(k, n);
#endif
        pixels_lanes(k, n, out);
        return;
    }
#endif
    const struct rcpix *P = k->pix;
    for (int q = 0; q < n; ++q)
    {
        const float acc[3] = {P->acc[0][q], P->acc[1][q], P->acc[2][q]}, col[3] = {P->col[0][q], P->col[1][q], P->col[2][q]};
        pixel_out(k, acc, P->trans[q], col, out + (size_t)q * (size_t)k->ch);
    }
}

/* the session's light inputs (play.c build_rs, for the cast's state: no
 * frame clock, pt 1, the fog's light at the eye as it would have settled) */
static void build_rs(const struct raycast_spec *sp, struct session *ss, const struct rc *k, int rd, struct rs_in *in)
{
    const struct client_player *cp = &ss->cp;
    memset(in, 0, sizeof *in);
    in->rd = rd;
    in->fov = 70.0F;
    in->gamma = sp->gamma;
    in->dw = sp->w;
    in->dh = sp->h;
    in->zoom = 1.0;
    in->ppx = in->px = cp->e.pos_x;
    in->ppy = in->py = cp->e.pos_y;
    in->ppz = in->pz = cp->e.pos_z;
    in->yaw = in->pyaw = cp->rotation_yaw;
    in->pit = in->ppit = cp->rotation_pitch;
    in->yoff = cp->e.y_offset;
    in->hp = cp->sv.health;
    in->mhurt = 10;
    in->dim = k->dim;
    in->voidp = k->dim == 0;
    in->voidf = 0.03125;
    in->xzfog = k->dim == -1 || k->dim == 1;
    in->cloud = 16777215;
    in->wt = cp->cw_day;
    for (int i = 0; ss->sr && i < ss->sr->nworlds; ++i)
        if (ss->sr->w[i].dim == k->dim)
        {
            in->rain = in->prain = ss->sr->w[i].st.prev_raining_strength;
            in->thu = in->pthu = ss->sr->w[i].st.prev_thundering_strength;
        }
    float floor_ = k->dim == -1 ? 0.1F : 0.0F;
    for (int i = 0; i <= 15; ++i)
    {
        float v = 1.0F - (float)i / 15.0F;
        in->lbt[i] = (1.0F - v) / (v * 3.0F + 1.0F) * (1.0F - floor_) + floor_;
    }
    in->pt = 1.0F;
    int bx = (int)floor(cp->e.pos_x), bz = (int)floor(cp->e.pos_z);
    int by = (int)floor(cp->e.pos_y - (double)cp->e.y_offset + 1.8 * 0.66);
    int sky, block;
    light_ref(k, bx, by, bz, &sky, &block);
    in->brf = sky << 20 | block << 4;
    /* the biome's temperature at the player (getFloatTemperature) */
    int fy = (int)floor(cp->e.pos_y);
    float temp = T.biome_temp[biome_ref(k, bx, bz)];
    if (fy > 64)
    {
        float n = (float)perlin_point(&T.perlin_a, (double)bx * 1.0 / 8.0, (double)bz * 1.0 / 8.0) * 4.0F;
        temp = temp - (n + (float)fy - 64.0F) * 0.05F / 30.0F;
    }
    in->temp = temp;
    in->skytemp = sky_color_by_temp(temp);
    /* updateRenderer's fog brightness, settled: the light at the feet */
    int fsky, fblock;
    light_ref(k, bx, fy, bz, &fsky, &fblock);
    int level = fsky - (k->dim == -1 ? 11 : 0);
    if (fblock > level) level = fblock;
    float b = in->lbt[level < 0 ? 0 : level > 15 ? 15 : level];
    float f1 = (float)rd / 16.0F;
    in->fc1 = in->fc2 = b * (1.0F - f1) + f1;
    in->mat = k->liquid == 8 ? 1 : k->liquid == 10 ? 2 : 0;
}

/* the entities a cast can meet: the client world's livings and fireballs,
 * the dimension's items, orbs and arrows */
static void add_ent(struct rc *k, const struct aabb *bx, const float *rgb)
{
    if (k->nent >= RC_ENTS) return;
    double cx = (bx->min_x + bx->max_x) * 0.5, cy = (bx->min_y + bx->max_y) * 0.5, cz = (bx->min_z + bx->max_z) * 0.5;
    /* (the frame's coordinates: a world position less the origin cell, in double, then rounded to the scalar once) */
    const double b3[3] = {(double)k->bx, (double)k->by, (double)k->bz};
    rcr ddx = (rcr)(cx - b3[0]) - k->ox, ddy = (rcr)(cy - b3[1]) - k->oy, ddz = (rcr)(cz - b3[2]) - k->oz;
    rcr rr = (rcr)k->range + RC_K(2.0);
    if (ddx * ddx + ddy * ddy + ddz * ddz > rr * rr) return;
    struct rcent *e = &k->ent[k->nent];
    e->box[0] = (rcr)(bx->min_x - b3[0]), e->box[1] = (rcr)(bx->min_y - b3[1]), e->box[2] = (rcr)(bx->min_z - b3[2]);
    e->box[3] = (rcr)(bx->max_x - b3[0]), e->box[4] = (rcr)(bx->max_y - b3[1]), e->box[5] = (rcr)(bx->max_z - b3[2]);
    if (!(e->box[3] > e->box[0] && e->box[4] > e->box[1] && e->box[5] > e->box[2])) return;
    memcpy(e->rgb, rgb, sizeof e->rgb);
    /* the light at 0.66 of the box's height (getBrightnessForRender) */
    int sky, block;
    light_ref(k, (int)floor(cx), (int)floor(bx->min_y + (bx->max_y - bx->min_y) * 0.66), (int)floor(cz), &sky, &block);
    e->lmi = sky * 16 + block;
    /* its screen rectangle: the corners projected, a pixel more each way;
     * a corner behind the eye takes the whole frame */
    rcr i0 = INFINITY, i1 = -INFINITY, j0 = INFINITY, j1 = -INFINITY;
    int behind = 0, ahead = 0;
    for (int c = 0; c < 8; ++c)
    {
        rcr px = e->box[(c & 1) ? 3 : 0] - k->ox, py = e->box[(c & 2) ? 4 : 1] - k->oy, pz = e->box[(c & 4) ? 5 : 2] - k->oz;
        rcr z = px * k->fx + py * k->fy + pz * k->fz;
        if (z < RC_K(0.05)) { behind = 1; continue; }
        ++ahead;
        rcr x = (px * k->rx + py * k->ry + pz * k->rz) / z, y = (px * k->ux + py * k->uy + pz * k->uz) / z;
        rcr ii = (x / (RC_TAN_HALF * k->aspect) + RC_K(1.0)) * RC_K(0.5) * RC_K(4096.0),
            jj = (RC_K(1.0) - y / RC_TAN_HALF) * RC_K(0.5) * RC_K(4096.0);
        if (ii < i0) i0 = ii;
        if (ii > i1) i1 = ii;
        if (jj < j0) j0 = jj;
        if (jj > j1) j1 = jj;
    }
    /* wholly behind the eye: no ray meets it */
    if (!ahead) return;
    if (behind) e->i0 = e->j0 = INT32_MIN / 2, e->i1 = e->j1 = INT32_MAX / 2;
    else
    {
        /* in 1/4096 of the frame, scaled to pixels by the caller */
        e->i0 = (int)RC_FLOOR(i0) - 1, e->i1 = (int)-RC_FLOOR(-i1) + 1, e->j0 = (int)RC_FLOOR(j0) - 1, e->j1 = (int)-RC_FLOOR(-j1) + 1;
    }
    k->nent++;
}

static void entities(struct rc *k, struct session *ss, int w, int h)
{
    k->nent = 0;
    struct serverreplay *sr = ss->sr;
    if (sr == NULL) return;
    if (sr->combat)
    {
        const struct clientworld *cw = &sr->combat->client;
        for (int i = 0; i < cw->nents; ++i)
        {
            const struct client_entity *ce = &cw->ents[i];
            if (ce->is_dead) continue;
            if (!ce->is_living && !ce->is_fireball) continue;
            struct aabb b = client_entity_box(ce);
            const float *rgb = ce->is_fireball ? T.fireball : ce->kind >= 0 && ce->kind < RC_KINDS ? T.mob[ce->kind] : T.unknown;
            add_ent(k, &b, rgb);
        }
    }
    /* the client's EntityDragon, a living the client world holds as a pick object (pickobj.c's PK_DRAGON copy), after
     * the living copies: its box (setSize(16, 8)) in the unknown kind's grey (lane/parity: the oracle drew it, the cast
     * did not, every End recording's frames differed there) */
    {
        int n = 0;
        const struct pickobj *po = pickobj_client_objs(&ss->cp, &n);
        for (int i = 0; i < n; ++i)
        {
            const struct dragon_state *d = po[i].kind == PK_DRAGON && !po[i].dead ? pickobj_client_dragon(&po[i], NULL) : NULL;
            if (d == NULL) continue;
            struct aabb b = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
            add_ent(k, &b, T.unknown);
        }
    }
    if (sr->d && serverreplay_player_dim(sr) == sr->d->dim)
        for (int i = 0; i < sr->d->nents; ++i)
        {
            const struct sr_ent *rec = &sr->d->ents[i];
            const ie_ent *ie = NULL;
            if (rec->pool == 0) ie = sr_ent_p(sr, rec);
            else if (rec->pool == 2)
            {
                const struct an_ent *ae = sr_ent_p(sr, rec);
                if (ae && !ae->is_living) ie = ie_get(ae->ieh);
            }
            if (ie == NULL || ie->is_dead) continue;
            const float *rgb = ie->kind == IE_ITEM ? T.item : ie->kind == IE_ORB ? T.orb : ie->kind == IE_ARROW ? T.arrow : NULL;
            if (rgb == NULL) continue;
            add_ent(k, &ie->e.bounding_box, rgb);
        }
    /* the rectangles from 1/4096 of the frame to pixels; one off the frame goes */
    int kept = 0;
    for (int e = 0; e < k->nent; ++e)
    {
        struct rcent *en = &k->ent[e];
        if (en->i0 > INT32_MIN / 4)
        {
            en->i0 = (int)floor((double)en->i0 / 4096.0 * w - 0.5) - 1;
            en->i1 = (int)ceil((double)en->i1 / 4096.0 * w - 0.5) + 1;
            en->j0 = (int)floor((double)en->j0 / 4096.0 * h - 0.5) - 1;
            en->j1 = (int)ceil((double)en->j1 / 4096.0 * h - 0.5) + 1;
            if (en->i1 < 0 || en->i0 >= w || en->j1 < 0 || en->j0 >= h) continue;
        }
        k->ent[kept++] = *en;
    }
    k->nent = kept;
}

/* The window's tables an env keeps for its next cast (the keep's, lane/castmove). sections() read every window
 * column's chunk header each cast (its mask and bands: a line a chunk, 2% of the brief bench's cycles). They hold
 * while the client world is unwritten since they were made: the same world at the same write sequence (every block,
 * metadata and light write and every insertion moves a world's wseq: chunk_touch, world_note_write, world_put_chunk),
 * write epoch, map version (every insertion and removal) and chunk count, the same pool epoch (a reset puts the
 * image's counters back), dimension and window, and world_chunk giving every column's chunk again. Otherwise, at the
 * same world, pool epoch, dimension and window, a column whose chunk has the address and write sequence it had keeps
 * its entries (the keep's own key, castreuse's) and the others are made again. A band shared with the server's copy
 * is never written in place (chunk_sec_own), so a client chunk's sections change only by its own writes. */
/* RC_BRICK (lane/castexact): the dry air walk's lanes read a cell's id only in a brick with blocks (sec_bricks): 0
 * off (every id read, the default), 1 those ids by a load a lane, 2 by a masked gather. Measured off: at the 16x16
 * trainer's shape (512 envs in 4 groups, fixed work) the FP32 cast took 132.1k cycles an env-step without bricks,
 * 141.8k with 1, 139.4k with 2, 139.7k with 1 and RC_BRICKJ (FP64: 146.0k, 153.0k, -, 159.2k): the dry walk saved
 * 3.2k to 4.6k (5.6k on FP64), the masks cost 9.4k to make (sec_bricks_lanes, a window's sections at each cast that
 * moved) and the scalar walk after the prefix 2k more (DEVLOG lane/castexact) */
#if !defined(RC_BRICK)
#define RC_BRICK 0
#endif
struct rcwin {
    int ok, cap;
    const struct world *w;
    uint64_t wseq, wepoch;
    uint32_t map_ver, epoch;
    size_t used;
    int dim, cx0, cz0, ncx, ncz, top;
    struct chunk **col;
    const struct chunk_sec **sec, **lsec;
    uint64_t *cws;              /* a column's chunk's wseq when its entries were made */
    int *ctop;                  /* a column's top (16 past its highest section) */
    uint64_t *bm;               /* each lsec entry's bricks (sec_bricks; 0 where lsec is NULL) */
};

static int win_room(struct rcwin *v, int n)
{
    if (n <= v->cap) return 0;
    proc_free(v->col), proc_free(v->sec), proc_free(v->lsec), proc_free(v->cws), proc_free(v->ctop), proc_free(v->bm);
    v->col = proc_malloc((size_t)n * sizeof *v->col);
    v->sec = proc_malloc((size_t)n * 16 * sizeof *v->sec);
    v->lsec = proc_malloc((size_t)n * 16 * sizeof *v->lsec);
    v->cws = proc_malloc((size_t)n * sizeof *v->cws);
    v->ctop = proc_malloc((size_t)n * sizeof *v->ctop);
    v->bm = proc_malloc((size_t)n * 16 * sizeof *v->bm);
    v->ok = 0;
    if (v->col == NULL || v->sec == NULL || v->lsec == NULL || v->cws == NULL || v->ctop == NULL || v->bm == NULL)
    {
        proc_free(v->col), proc_free(v->sec), proc_free(v->lsec), proc_free(v->cws), proc_free(v->ctop), proc_free(v->bm);
        v->col = NULL, v->sec = v->lsec = NULL, v->cws = NULL, v->ctop = NULL, v->bm = NULL;
        return v->cap = 0, -1;
    }
    v->cap = n;
    return 0;
}

/* A section's bricks (lane/castexact): bit (x >> 2) * 16 + (z >> 2) * 4 + (y >> 2) set where the 4 x 4 x 4 brick of
 * cells holds an id other than 0 (the lanes' air: a ray of a dry cast passes every cell of a clear brick, so its id
 * need not be read). SEC_XYZ's order: 64 bytes at x << 8 | zq << 6 hold z 4 zq .. 4 zq + 3, y 0 .. 15. */
#if defined(RC_AVX2)
__attribute__((target("avx512f,avx512bw,bmi2"), unused)) static uint64_t sec_bricks_lanes(const uint8_t *ids)
{
    uint64_t m = 0;
    for (int xq = 0; xq < 4; ++xq)
        for (int zq = 0; zq < 4; ++zq)
        {
            const uint8_t *p = ids + (xq << 10) + (zq << 6);
            const __m512i v = _mm512_or_si512(_mm512_or_si512(_mm512_loadu_si512(p), _mm512_loadu_si512(p + 256)),
                                              _mm512_or_si512(_mm512_loadu_si512(p + 512), _mm512_loadu_si512(p + 768)));
            uint64_t b = (uint64_t)_mm512_test_epi8_mask(v, v);
            b |= b >> 32, b |= b >> 16;                  /* y's 16 bits, any z */
            b |= b >> 2, b |= b >> 1;                    /* bit 4 yq: any of y 4 yq .. 4 yq + 3 */
            m |= (uint64_t)_pext_u32((uint32_t)b, 0x1111u) << (xq * 16 + zq * 4);
        }
    return m;
}
#endif
static __attribute__((unused)) uint64_t sec_bricks(const uint8_t *ids)
{
#if defined(RC_AVX2)
    if (__builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("bmi2")) return sec_bricks_lanes(ids);
#endif
    uint64_t m = 0;
    for (int i = 0; i < 4096; ++i)
        if (ids[i]) m |= 1ull << ((i >> 10) * 16 + (i >> 6 & 3) * 4 + (i >> 2 & 3));
    return m;
}

/* (RC_BRICK) the bricks of a window the cast moved from (sections()): a column there whose chunk is the one now at its
 * place, at the write sequence it had, takes its sections' bricks from it (the same world and pool epoch) */
struct rccarry {
    int ok, cap, cx0, cz0, ncx, ncz;
    struct chunk **col;
    uint64_t *cws, *bm;
};

static __attribute__((unused)) void carry_save(struct rccarry *cy, const struct rcwin *v)
{
    cy->ok = 0;
    const int n = v->ncx * v->ncz;
    if (n > cy->cap)
    {
        proc_free(cy->col), proc_free(cy->cws), proc_free(cy->bm);
        cy->col = proc_malloc((size_t)n * sizeof *cy->col);
        cy->cws = proc_malloc((size_t)n * sizeof *cy->cws);
        cy->bm = proc_malloc((size_t)n * 16 * sizeof *cy->bm);
        if (cy->col == NULL || cy->cws == NULL || cy->bm == NULL)
        {
            proc_free(cy->col), proc_free(cy->cws), proc_free(cy->bm);
            cy->col = NULL, cy->cws = cy->bm = NULL;
            cy->cap = 0;
            return;
        }
        cy->cap = n;
    }
    memcpy(cy->col, v->col, (size_t)n * sizeof *cy->col);
    memcpy(cy->cws, v->cws, (size_t)n * sizeof *cy->cws);
    memcpy(cy->bm, v->bm, (size_t)n * 16 * sizeof *cy->bm);
    cy->cx0 = v->cx0, cy->cz0 = v->cz0, cy->ncx = v->ncx, cy->ncz = v->ncz;
    cy->ok = 1;
}

/* k's window's tables into o's arrays (room for them), its top into o; with reuse (o's tables are this window's
 * at this world and pool epoch), a column whose chunk is the one there at the write sequence it had is kept; with
 * cy (RC_BRICK), a column made again takes the bricks cy holds for its chunk */
static void win_fill(const struct rc *k, struct rcwin *o, int reuse, const struct rccarry *cy)
{
    struct chunk **col = o->col;
    const struct chunk_sec **sec = o->sec, **lsec = o->lsec;
    int top = 0;
    for (int a = 0; a < k->ncx; ++a)
        for (int b = 0; b < k->ncz; ++b)
        {
            int c = a * k->ncz + b;
            struct chunk *ch = world_chunk(k->w, k->cx0 + a, k->cz0 + b);
            if (reuse && ch == col[c] && (ch == NULL || o->cws[c] == ch->wseq))
            {
                if (o->ctop[c] > top) top = o->ctop[c];
                continue;
            }
            col[c] = ch;
            o->cws[c] = ch ? ch->wseq : 0;
            /* NULL, then the mask's sections (most of a column's 16 are above its top) */
            memset(&sec[c * 16], 0, 16 * sizeof *sec);
            memset(&lsec[c * 16], 0, 16 * sizeof *lsec);
            memset(&o->bm[c * 16], 0, 16 * sizeof *o->bm);
            unsigned m = ch ? ch->mask : 0;
#if RC_BRICK
            /* the bricks from the window moved from, where it holds this chunk as it is */
            const unsigned pa = (unsigned)(k->cx0 + a - (cy ? cy->cx0 : 0)), pb = (unsigned)(k->cz0 + b - (cy ? cy->cz0 : 0));
            const int pc = cy && cy->ok && ch && pa < (unsigned)cy->ncx && pb < (unsigned)cy->ncz ? (int)(pa * (unsigned)cy->ncz + pb) : -1;
            const uint64_t *cbm = pc >= 0 && cy->col[pc] == ch && cy->cws[pc] == ch->wseq ? &cy->bm[pc * 16] : NULL;
#else
            (void)cy;
#endif
            for (unsigned r = m; r; r &= r - 1)
            {
                const struct chunk_sec *s = chunk_sec_at(ch, __builtin_ctz(r));
                sec[c * 16 + __builtin_ctz(r)] = s;
                lsec[c * 16 + __builtin_ctz(r)] = s && s->ids_hi == NULL ? s : NULL;
#if RC_BRICK
                if (s && s->ids_hi == NULL) o->bm[c * 16 + __builtin_ctz(r)] = cbm ? cbm[__builtin_ctz(r)] : sec_bricks(s->ids);
#endif
            }
            o->ctop[c] = m != 0 ? 16 * (32 - __builtin_clz(m)) : 0;
            if (o->ctop[c] > top) top = o->ctop[c];
        }
    o->top = top;
}

#if defined(RC_WIN_VERIFY)
/* the verify build: every kept window's tables (whole or in part) made again and compared, counted in proc_malloc
 * memory (as rkv) and printed every 65,536 casts and at exit: casts with a keep, tables kept whole, kept in part,
 * kept tables that differ */
static uint64_t *rwv;
static pthread_once_t rwv_once = PTHREAD_ONCE_INIT;
static void rwv_print(void)
{
    if (rwv)
        fprintf(stderr, "rcwin casts %llu kept %llu in part %llu differ %llu\n", (unsigned long long)rwv[0],
                (unsigned long long)rwv[1], (unsigned long long)rwv[3], (unsigned long long)rwv[2]);
}
static void rwv_init(void)
{
    if ((rwv = proc_calloc(4, sizeof *rwv)) != NULL) atexit(rwv_print);
}
static void win_check(const struct rc *k, const struct rcwin *v, int n, int part)
{
    static _Thread_local struct rcwin *sv;
    if (sv == NULL) sv = proc_calloc(1, sizeof *sv);
    if (sv == NULL || rwv == NULL || win_room(sv, n)) return;
    win_fill(k, sv, 0, NULL);
    __atomic_add_fetch(&rwv[part ? 3 : 1], 1, __ATOMIC_RELAXED);
    if (sv->top != v->top || memcmp(sv->col, v->col, (size_t)n * sizeof *sv->col) ||
        memcmp(sv->sec, v->sec, (size_t)n * 16 * sizeof *sv->sec) || memcmp(sv->lsec, v->lsec, (size_t)n * 16 * sizeof *sv->lsec) ||
        memcmp(sv->bm, v->bm, (size_t)n * 16 * sizeof *sv->bm))
    {
        __atomic_add_fetch(&rwv[2], 1, __ATOMIC_RELAXED);
        fprintf(stderr, "rcwin MISMATCH: a kept window's tables differ from the chunks'\n");
    }
}
#endif

/* the fast path's section table: the chunks within reach of the eye; with v (an env's keep), the tables it kept when
 * they hold (in whole or in part), else made there and kept */
static int sections(struct rc *k, struct rcwin *v, uint32_t epoch)
{
    static _Thread_local struct rcwin *tv;
#if defined(RC_WIN_VERIFY)
    if (v) pthread_once(&rwv_once, rwv_init);
    if (v && rwv && __atomic_add_fetch(&rwv[0], 1, __ATOMIC_RELAXED) % 65536 == 0) rwv_print();
#endif
    int r = k->range + 1;
    k->cx0 = (int)floor(k->wx - r) >> 4;
    k->cz0 = (int)floor(k->wz - r) >> 4;
    k->ncx = ((int)floor(k->wx + r) >> 4) - k->cx0 + 1;
    k->ncz = ((int)floor(k->wz + r) >> 4) - k->cz0 + 1;
    int n = k->ncx * k->ncz;
    const struct world *w = k->w;
    int reuse = v && v->ok && v->w == w && v->epoch == epoch && v->dim == k->dim && v->cx0 == k->cx0 &&
                v->cz0 == k->cz0 && v->ncx == k->ncx && v->ncz == k->ncz;
    if (reuse && v->wseq == w->wseq && v->wepoch == w->epoch && v->map_ver == w->map_ver && v->used == w->used)
    {
        int same = 1;
        for (int a = 0; a < k->ncx && same; ++a)
            for (int b = 0; b < k->ncz; ++b)
                if (world_chunk(k->w, k->cx0 + a, k->cz0 + b) != v->col[a * k->ncz + b])
                {
                    same = 0;
                    break;
                }
        if (same)
        {
            k->col = v->col, k->sec = v->sec, k->lsec = v->lsec, k->bm = v->bm, k->top = v->top;
#if defined(RC_WIN_VERIFY)
            win_check(k, v, n, 0);
#endif
            return 0;
        }
    }
    /* (RC_BRICK) a kept window the eye moved from, at this world, pool epoch and dimension: its bricks carried */
    struct rccarry *cy = NULL;
#if RC_BRICK
    static _Thread_local struct rccarry *tcy;
    if (v && !reuse && v->ok && v->w == w && v->epoch == epoch && v->dim == k->dim &&
        (tcy != NULL || (tcy = proc_calloc(1, sizeof *tcy)) != NULL))
        carry_save(tcy, v), cy = tcy;
#endif
    if (v && !reuse && win_room(v, n)) v = NULL;
    if (v == NULL)
    {
        if (tv == NULL && (tv = proc_calloc(1, sizeof *tv)) == NULL) return -1;
        if (win_room(tv, n)) return -1;
        reuse = 0;
    }
    struct rcwin *o = v ? v : tv;
    win_fill(k, o, reuse, cy);
    k->col = o->col, k->sec = o->sec, k->lsec = o->lsec, k->bm = o->bm, k->top = o->top;
#if defined(RC_WIN_VERIFY)
    if (reuse) win_check(k, o, n, 1);
#endif
    o->ok = v != NULL;
    o->w = w, o->wseq = w->wseq, o->wepoch = w->epoch, o->map_ver = w->map_ver, o->used = w->used, o->epoch = epoch;
    o->dim = k->dim, o->cx0 = k->cx0, o->cz0 = k->cz0, o->ncx = k->ncx, o->ncz = k->ncz;
    return 0;
}

/* a cast's state zeroed up to its entities, but for the lightmap, which setup writes whole */
static void rc_clear(struct rc *k)
{
    memset(k, 0, offsetof(struct rc, lm));
    memset((uint8_t *)k + offsetof(struct rc, lm) + sizeof k->lm, 0, offsetof(struct rc, ent) - offsetof(struct rc, lm) - sizeof k->lm);
}

/* about 50 KB: on the process heap, one pointer a thread (libneth's thread-locals take glibc's small static TLS,
 * rlbind.mk; proc_malloc, as an env's image heap is restored under the worker); the state, its row, its batch and
 * the batch's pixels each from a 64-byte line (rays_row8's stores are a line each; off the line rays_row8 took twice
 * the cycles) */
static struct rc *rc_state(void)
{
    static _Thread_local struct rc *k;
    if (k == NULL)
    {
#define RC_LINE(n) (((n) + 63) & ~(size_t)63)
        uint8_t *p = proc_malloc(63 + RC_LINE(sizeof *k) + RC_LINE(sizeof *k->row) + RC_LINE(sizeof *k->rays) + sizeof *k->pix);
        if (p == NULL) return NULL;
        p += -(size_t)p & 63;
        k = (struct rc *)p;
        k->row = (struct rcrow *)(p + RC_LINE(sizeof *k));
        k->rays = (struct rcrays *)(p + RC_LINE(sizeof *k) + RC_LINE(sizeof *k->row));
        k->pix = (struct rcpix *)(p + RC_LINE(sizeof *k) + RC_LINE(sizeof *k->row) + RC_LINE(sizeof *k->rays));
#undef RC_LINE
    }
    return k;
}

/* a run's screen x from pixel i0 on (a column's is the same on every row): made once a run, so once a cast at widths
 * to RC_ROW; to a multiple of sixteen past n, the lanes' loops' reads */
static void rays_sx(struct rcrow *R, int i0, int n, int w, rcr sxs)
{
    int m = (n + 15) & ~15;
    for (int q = 0; q < m && q < RC_ROW + 16; ++q)
        R->sx[q] = (RC_K(2.0) * ((rcr)(i0 + q) + RC_K(0.5)) / (rcr)w - RC_K(1.0)) * sxs;
    R->sx_i0 = i0;
    R->sx_n = n;
}

/* a run's rays in the batch, from its first: rays_row's outputs, and its screen x */
struct rcrun {
    rcr *dx, *dy, *dz, *fwd, *idx, *idy, *idz, *tx, *ty, *tz;
    const rcr *sx;
};

#if defined(RC_AVX2) && RC_FP == 32
/* the FP32 instance's rays_row: eight rays a step in AVX2's lanes (sixteen in AVX-512's, rays_row16), each lane the
 * scalar loop's operations in its order. The first boundary on each axis: (c - o) * id, c the eye's cell's own
 * boundary in the frame's coordinates, 1 going + and 0 going - (the cell is the origin) */
static __attribute__((target("avx2"), noinline)) void rays_row8f(const struct rc *k, const struct rcrun *R, int n, float sy)
{
    const __m256 one = _mm256_set1_ps(1.0f), zero = _mm256_setzero_ps(), inf = _mm256_set1_ps(INFINITY);
    const __m256 vfx = _mm256_set1_ps(k->fx), vfy = _mm256_set1_ps(k->fy), vfz = _mm256_set1_ps(k->fz);
    const __m256 vrx = _mm256_set1_ps(k->rx), vry = _mm256_set1_ps(k->ry), vrz = _mm256_set1_ps(k->rz);
    const __m256 syux = _mm256_set1_ps(sy * k->ux), syuy = _mm256_set1_ps(sy * k->uy), syuz = _mm256_set1_ps(sy * k->uz);
    const __m256 vox = _mm256_set1_ps(k->ox), voy = _mm256_set1_ps(k->oy), voz = _mm256_set1_ps(k->oz);
    const __m256 vx1 = _mm256_set1_ps(RC_CX(k, k->eye_x + 1)), vy1 = _mm256_set1_ps(RC_CY(k, k->eye_y + 1)),
                 vz1 = _mm256_set1_ps(RC_CZ(k, k->eye_z + 1));
    const __m256 vx0 = _mm256_set1_ps(RC_CX(k, k->eye_x)), vy0 = _mm256_set1_ps(RC_CY(k, k->eye_y)), vz0 = _mm256_set1_ps(RC_CZ(k, k->eye_z));
    for (int q = 0; q < n; q += 8)
    {
        __m256 sx = _mm256_loadu_ps(&R->sx[q]);
        __m256 dx = _mm256_add_ps(_mm256_add_ps(vfx, _mm256_mul_ps(sx, vrx)), syux);
        __m256 dy = _mm256_add_ps(_mm256_add_ps(vfy, _mm256_mul_ps(sx, vry)), syuy);
        __m256 dz = _mm256_add_ps(_mm256_add_ps(vfz, _mm256_mul_ps(sx, vrz)), syuz);
        __m256 ss = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy)), _mm256_mul_ps(dz, dz));
        __m256 inv = _mm256_div_ps(one, _mm256_sqrt_ps(ss));
        __m256 d0 = _mm256_mul_ps(dx, inv), d1 = _mm256_mul_ps(dy, inv), d2 = _mm256_mul_ps(dz, inv);
#if RC_FIX
        {
            /* rc_fxqf in the lanes: rint(d 2^G) 2^-G, ties to even */
            const __m256 q = _mm256_set1_ps((float)(1 << RC_FIXG)), qi = _mm256_set1_ps(1.0f / (float)(1 << RC_FIXG));
            d0 = _mm256_mul_ps(_mm256_round_ps(_mm256_mul_ps(d0, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
            d1 = _mm256_mul_ps(_mm256_round_ps(_mm256_mul_ps(d1, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
            d2 = _mm256_mul_ps(_mm256_round_ps(_mm256_mul_ps(d2, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
        }
#endif
        _mm256_storeu_ps(&R->dx[q], d0), _mm256_storeu_ps(&R->dy[q], d1), _mm256_storeu_ps(&R->dz[q], d2);
        _mm256_storeu_ps(&R->fwd[q], _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(d0, vfx), _mm256_mul_ps(d1, vfy)), _mm256_mul_ps(d2, vfz)));
        __m256 n0 = _mm256_cmp_ps(d0, zero, _CMP_NEQ_UQ), n1 = _mm256_cmp_ps(d1, zero, _CMP_NEQ_UQ), n2 = _mm256_cmp_ps(d2, zero, _CMP_NEQ_UQ);
        __m256 r0 = _mm256_and_ps(n0, _mm256_div_ps(one, d0)), r1 = _mm256_and_ps(n1, _mm256_div_ps(one, d1)),
               r2 = _mm256_and_ps(n2, _mm256_div_ps(one, d2));
        _mm256_storeu_ps(&R->idx[q], r0), _mm256_storeu_ps(&R->idy[q], r1), _mm256_storeu_ps(&R->idz[q], r2);
        __m256 t0 = _mm256_mul_ps(_mm256_sub_ps(_mm256_blendv_ps(vx0, vx1, _mm256_cmp_ps(d0, zero, _CMP_GT_OQ)), vox), r0);
        __m256 t1 = _mm256_mul_ps(_mm256_sub_ps(_mm256_blendv_ps(vy0, vy1, _mm256_cmp_ps(d1, zero, _CMP_GT_OQ)), voy), r1);
        __m256 t2 = _mm256_mul_ps(_mm256_sub_ps(_mm256_blendv_ps(vz0, vz1, _mm256_cmp_ps(d2, zero, _CMP_GT_OQ)), voz), r2);
        _mm256_storeu_ps(&R->tx[q], _mm256_blendv_ps(inf, t0, n0)), _mm256_storeu_ps(&R->ty[q], _mm256_blendv_ps(inf, t1, n1));
        _mm256_storeu_ps(&R->tz[q], _mm256_blendv_ps(inf, t2, n2));
    }
}

static __attribute__((target("avx512f"), noinline)) void rays_row16(const struct rc *k, const struct rcrun *R, int n, float sy)
{
    const __m512 one = _mm512_set1_ps(1.0f), zero = _mm512_setzero_ps(), inf = _mm512_set1_ps(INFINITY);
    const __m512 vfx = _mm512_set1_ps(k->fx), vfy = _mm512_set1_ps(k->fy), vfz = _mm512_set1_ps(k->fz);
    const __m512 vrx = _mm512_set1_ps(k->rx), vry = _mm512_set1_ps(k->ry), vrz = _mm512_set1_ps(k->rz);
    const __m512 syux = _mm512_set1_ps(sy * k->ux), syuy = _mm512_set1_ps(sy * k->uy), syuz = _mm512_set1_ps(sy * k->uz);
    const __m512 vox = _mm512_set1_ps(k->ox), voy = _mm512_set1_ps(k->oy), voz = _mm512_set1_ps(k->oz);
    const __m512 vx1 = _mm512_set1_ps(RC_CX(k, k->eye_x + 1)), vy1 = _mm512_set1_ps(RC_CY(k, k->eye_y + 1)),
                 vz1 = _mm512_set1_ps(RC_CZ(k, k->eye_z + 1));
    const __m512 vx0 = _mm512_set1_ps(RC_CX(k, k->eye_x)), vy0 = _mm512_set1_ps(RC_CY(k, k->eye_y)), vz0 = _mm512_set1_ps(RC_CZ(k, k->eye_z));
    for (int q = 0; q < n; q += 16)
    {
        __m512 sx = _mm512_loadu_ps(&R->sx[q]);
        __m512 dx = _mm512_add_ps(_mm512_add_ps(vfx, _mm512_mul_ps(sx, vrx)), syux);
        __m512 dy = _mm512_add_ps(_mm512_add_ps(vfy, _mm512_mul_ps(sx, vry)), syuy);
        __m512 dz = _mm512_add_ps(_mm512_add_ps(vfz, _mm512_mul_ps(sx, vrz)), syuz);
        __m512 ss = _mm512_add_ps(_mm512_add_ps(_mm512_mul_ps(dx, dx), _mm512_mul_ps(dy, dy)), _mm512_mul_ps(dz, dz));
        __m512 inv = _mm512_div_ps(one, _mm512_sqrt_ps(ss));
        __m512 d0 = _mm512_mul_ps(dx, inv), d1 = _mm512_mul_ps(dy, inv), d2 = _mm512_mul_ps(dz, inv);
#if RC_FIX
        {
            const __m512 q = _mm512_set1_ps((float)(1 << RC_FIXG)), qi = _mm512_set1_ps(1.0f / (float)(1 << RC_FIXG));
            d0 = _mm512_mul_ps(_mm512_roundscale_ps(_mm512_mul_ps(d0, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
            d1 = _mm512_mul_ps(_mm512_roundscale_ps(_mm512_mul_ps(d1, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
            d2 = _mm512_mul_ps(_mm512_roundscale_ps(_mm512_mul_ps(d2, q), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), qi);
        }
#endif
        _mm512_storeu_ps(&R->dx[q], d0), _mm512_storeu_ps(&R->dy[q], d1), _mm512_storeu_ps(&R->dz[q], d2);
        _mm512_storeu_ps(&R->fwd[q], _mm512_add_ps(_mm512_add_ps(_mm512_mul_ps(d0, vfx), _mm512_mul_ps(d1, vfy)), _mm512_mul_ps(d2, vfz)));
        __mmask16 n0 = _mm512_cmp_ps_mask(d0, zero, _CMP_NEQ_UQ), n1 = _mm512_cmp_ps_mask(d1, zero, _CMP_NEQ_UQ),
                  n2 = _mm512_cmp_ps_mask(d2, zero, _CMP_NEQ_UQ);
        __m512 r0 = _mm512_maskz_mov_ps(n0, _mm512_div_ps(one, d0)), r1 = _mm512_maskz_mov_ps(n1, _mm512_div_ps(one, d1)),
               r2 = _mm512_maskz_mov_ps(n2, _mm512_div_ps(one, d2));
        _mm512_storeu_ps(&R->idx[q], r0), _mm512_storeu_ps(&R->idy[q], r1), _mm512_storeu_ps(&R->idz[q], r2);
        __m512 t0 = _mm512_mul_ps(_mm512_sub_ps(_mm512_mask_blend_ps(_mm512_cmp_ps_mask(d0, zero, _CMP_GT_OQ), vx0, vx1), vox), r0);
        __m512 t1 = _mm512_mul_ps(_mm512_sub_ps(_mm512_mask_blend_ps(_mm512_cmp_ps_mask(d1, zero, _CMP_GT_OQ), vy0, vy1), voy), r1);
        __m512 t2 = _mm512_mul_ps(_mm512_sub_ps(_mm512_mask_blend_ps(_mm512_cmp_ps_mask(d2, zero, _CMP_GT_OQ), vz0, vz1), voz), r2);
        _mm512_storeu_ps(&R->tx[q], _mm512_mask_blend_ps(n0, inf, t0)), _mm512_storeu_ps(&R->ty[q], _mm512_mask_blend_ps(n1, inf, t1));
        _mm512_storeu_ps(&R->tz[q], _mm512_mask_blend_ps(n2, inf, t2));
    }
}
#endif

#if defined(RC_AVX2) && RC_FP == 64
/* rays_row's loop four rays a step in AVX2's lanes: the SSE2 loop's operations, each lane in the scalar order */
static __attribute__((target("avx2"), noinline)) void rays_row4(const struct rc *k, const struct rcrun *R, int n, double sy)
{
    const __m256d one = _mm256_set1_pd(1.0), zero = _mm256_setzero_pd(), inf = _mm256_set1_pd(INFINITY);
    const __m256d vfx = _mm256_set1_pd(k->fx), vfy = _mm256_set1_pd(k->fy), vfz = _mm256_set1_pd(k->fz);
    const __m256d vrx = _mm256_set1_pd(k->rx), vry = _mm256_set1_pd(k->ry), vrz = _mm256_set1_pd(k->rz);
    const __m256d syux = _mm256_set1_pd(sy * k->ux), syuy = _mm256_set1_pd(sy * k->uy), syuz = _mm256_set1_pd(sy * k->uz);
    const __m256d vox = _mm256_set1_pd(k->ox), voy = _mm256_set1_pd(k->oy), voz = _mm256_set1_pd(k->oz);
    const __m256d vex0 = _mm256_set1_pd((double)k->eye_x), vey0 = _mm256_set1_pd((double)k->eye_y), vez0 = _mm256_set1_pd((double)k->eye_z);
    const __m256d vex1 = _mm256_set1_pd((double)(k->eye_x + 1)), vey1 = _mm256_set1_pd((double)(k->eye_y + 1)),
                  vez1 = _mm256_set1_pd((double)(k->eye_z + 1));
    for (int q = 0; q < n; q += 4)
    {
        __m256d sx = _mm256_loadu_pd(&R->sx[q]);
        __m256d dx = _mm256_add_pd(_mm256_add_pd(vfx, _mm256_mul_pd(sx, vrx)), syux);
        __m256d dy = _mm256_add_pd(_mm256_add_pd(vfy, _mm256_mul_pd(sx, vry)), syuy);
        __m256d dz = _mm256_add_pd(_mm256_add_pd(vfz, _mm256_mul_pd(sx, vrz)), syuz);
        __m256d ss = _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(dx, dx), _mm256_mul_pd(dy, dy)), _mm256_mul_pd(dz, dz));
        __m256d inv = _mm256_div_pd(one, _mm256_sqrt_pd(ss));
        __m256d d0 = _mm256_mul_pd(dx, inv), d1 = _mm256_mul_pd(dy, inv), d2 = _mm256_mul_pd(dz, inv);
        _mm256_storeu_pd(&R->dx[q], d0), _mm256_storeu_pd(&R->dy[q], d1), _mm256_storeu_pd(&R->dz[q], d2);
        _mm256_storeu_pd(&R->fwd[q], _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(d0, vfx), _mm256_mul_pd(d1, vfy)), _mm256_mul_pd(d2, vfz)));
        __m256d n0 = _mm256_cmp_pd(d0, zero, _CMP_NEQ_UQ), n1 = _mm256_cmp_pd(d1, zero, _CMP_NEQ_UQ), n2 = _mm256_cmp_pd(d2, zero, _CMP_NEQ_UQ);
        __m256d r0 = _mm256_and_pd(n0, _mm256_div_pd(one, d0)), r1 = _mm256_and_pd(n1, _mm256_div_pd(one, d1)),
                r2 = _mm256_and_pd(n2, _mm256_div_pd(one, d2));
        _mm256_storeu_pd(&R->idx[q], r0), _mm256_storeu_pd(&R->idy[q], r1), _mm256_storeu_pd(&R->idz[q], r2);
        __m256d t0 = _mm256_mul_pd(_mm256_sub_pd(_mm256_blendv_pd(vex0, vex1, _mm256_cmp_pd(d0, zero, _CMP_GT_OQ)), vox), r0);
        __m256d t1 = _mm256_mul_pd(_mm256_sub_pd(_mm256_blendv_pd(vey0, vey1, _mm256_cmp_pd(d1, zero, _CMP_GT_OQ)), voy), r1);
        __m256d t2 = _mm256_mul_pd(_mm256_sub_pd(_mm256_blendv_pd(vez0, vez1, _mm256_cmp_pd(d2, zero, _CMP_GT_OQ)), voz), r2);
        _mm256_storeu_pd(&R->tx[q], _mm256_blendv_pd(inf, t0, n0)), _mm256_storeu_pd(&R->ty[q], _mm256_blendv_pd(inf, t1, n1));
        _mm256_storeu_pd(&R->tz[q], _mm256_blendv_pd(inf, t2, n2));
    }
}

/* the same eight rays a step in AVX-512's lanes, where the CPU has it */
static __attribute__((target("avx512f"), noinline)) void rays_row8(const struct rc *k, const struct rcrun *R, int n, double sy)
{
    const __m512d one = _mm512_set1_pd(1.0), zero = _mm512_setzero_pd(), inf = _mm512_set1_pd(INFINITY);
    const __m512d vfx = _mm512_set1_pd(k->fx), vfy = _mm512_set1_pd(k->fy), vfz = _mm512_set1_pd(k->fz);
    const __m512d vrx = _mm512_set1_pd(k->rx), vry = _mm512_set1_pd(k->ry), vrz = _mm512_set1_pd(k->rz);
    const __m512d syux = _mm512_set1_pd(sy * k->ux), syuy = _mm512_set1_pd(sy * k->uy), syuz = _mm512_set1_pd(sy * k->uz);
    const __m512d vox = _mm512_set1_pd(k->ox), voy = _mm512_set1_pd(k->oy), voz = _mm512_set1_pd(k->oz);
    const __m512d vex0 = _mm512_set1_pd((double)k->eye_x), vey0 = _mm512_set1_pd((double)k->eye_y), vez0 = _mm512_set1_pd((double)k->eye_z);
    const __m512d vex1 = _mm512_set1_pd((double)(k->eye_x + 1)), vey1 = _mm512_set1_pd((double)(k->eye_y + 1)),
                  vez1 = _mm512_set1_pd((double)(k->eye_z + 1));
    for (int q = 0; q < n; q += 8)
    {
        __m512d sx = _mm512_loadu_pd(&R->sx[q]);
        __m512d dx = _mm512_add_pd(_mm512_add_pd(vfx, _mm512_mul_pd(sx, vrx)), syux);
        __m512d dy = _mm512_add_pd(_mm512_add_pd(vfy, _mm512_mul_pd(sx, vry)), syuy);
        __m512d dz = _mm512_add_pd(_mm512_add_pd(vfz, _mm512_mul_pd(sx, vrz)), syuz);
        __m512d ss = _mm512_add_pd(_mm512_add_pd(_mm512_mul_pd(dx, dx), _mm512_mul_pd(dy, dy)), _mm512_mul_pd(dz, dz));
        __m512d inv = _mm512_div_pd(one, _mm512_sqrt_pd(ss));
        __m512d d0 = _mm512_mul_pd(dx, inv), d1 = _mm512_mul_pd(dy, inv), d2 = _mm512_mul_pd(dz, inv);
        _mm512_storeu_pd(&R->dx[q], d0), _mm512_storeu_pd(&R->dy[q], d1), _mm512_storeu_pd(&R->dz[q], d2);
        _mm512_storeu_pd(&R->fwd[q], _mm512_add_pd(_mm512_add_pd(_mm512_mul_pd(d0, vfx), _mm512_mul_pd(d1, vfy)), _mm512_mul_pd(d2, vfz)));
        __mmask8 n0 = _mm512_cmp_pd_mask(d0, zero, _CMP_NEQ_UQ), n1 = _mm512_cmp_pd_mask(d1, zero, _CMP_NEQ_UQ),
                 n2 = _mm512_cmp_pd_mask(d2, zero, _CMP_NEQ_UQ);
        __m512d r0 = _mm512_maskz_mov_pd(n0, _mm512_div_pd(one, d0)), r1 = _mm512_maskz_mov_pd(n1, _mm512_div_pd(one, d1)),
                r2 = _mm512_maskz_mov_pd(n2, _mm512_div_pd(one, d2));
        _mm512_storeu_pd(&R->idx[q], r0), _mm512_storeu_pd(&R->idy[q], r1), _mm512_storeu_pd(&R->idz[q], r2);
        __m512d t0 = _mm512_mul_pd(_mm512_sub_pd(_mm512_mask_blend_pd(_mm512_cmp_pd_mask(d0, zero, _CMP_GT_OQ), vex0, vex1), vox), r0);
        __m512d t1 = _mm512_mul_pd(_mm512_sub_pd(_mm512_mask_blend_pd(_mm512_cmp_pd_mask(d1, zero, _CMP_GT_OQ), vey0, vey1), voy), r1);
        __m512d t2 = _mm512_mul_pd(_mm512_sub_pd(_mm512_mask_blend_pd(_mm512_cmp_pd_mask(d2, zero, _CMP_GT_OQ), vez0, vez1), voz), r2);
        _mm512_storeu_pd(&R->tx[q], _mm512_mask_blend_pd(n0, inf, t0)), _mm512_storeu_pd(&R->ty[q], _mm512_mask_blend_pd(n1, inf, t1));
        _mm512_storeu_pd(&R->tz[q], _mm512_mask_blend_pd(n2, inf, t2));
    }
}
#endif

/* the rays of pixels i0 .. i0 + n - 1 of a row whose screen y is sy (sxs: the screen's half width), into the batch
 * B from its ray q0 (row: the run's screen x); walk's own arithmetic before it moved here, a ray at a time in its
 * order. On x86 two rays a step in SSE2's lanes (four in AVX2's, eight in AVX-512's where the CPU has them), each
 * lane the scalar loop's operations in its order (IEEE's: the same bits), the lanes past n computed and stored past
 * the run, unused; the divides on an axis with no direction are made and thrown away, so no form branches. */
static __attribute__((noinline)) void rays_row(const struct rc *k, struct rcrow *row, struct rcrays *B, int q0, int i0, int n, int w,
                                               rcr sy, rcr sxs)
{
    if (row->sx_i0 != i0 || row->sx_n < n) rays_sx(row, i0, n, w, sxs);
    const struct rcrun run = {B->dx + q0, B->dy + q0, B->dz + q0, B->fwd + q0, B->idx + q0, B->idy + q0, B->idz + q0,
                              B->tx + q0, B->ty + q0, B->tz + q0, row->sx};
    const struct rcrun *R = &run;
#if defined(RC_AVX2) && RC_FP == 32
    if (T.avx512)
    {
        rays_row16(k, R, n, sy);
        return;
    }
    if (T.avx2)
    {
        rays_row8f(k, R, n, sy);
        return;
    }
#elif defined(RC_AVX2)
    if (T.avx512)
    {
        rays_row8(k, R, n, sy);
        return;
    }
    if (T.avx2)
    {
        rays_row4(k, R, n, sy);
        return;
    }
#endif
    const rcr fx = k->fx, fy = k->fy, fz = k->fz, rx = k->rx, ry = k->ry, rz = k->rz, ux = k->ux, uy = k->uy, uz = k->uz;
    const rcr ox = k->ox, oy = k->oy, oz = k->oz;
    const rcr ex0 = RC_CX(k, k->eye_x), ey0 = RC_CY(k, k->eye_y), ez0 = RC_CZ(k, k->eye_z);
    const rcr ex1 = RC_CX(k, k->eye_x + 1), ey1 = RC_CY(k, k->eye_y + 1), ez1 = RC_CZ(k, k->eye_z + 1);
#if defined(__SSE2__) && RC_FP == 64
    const __m128d one = _mm_set1_pd(1.0), zero = _mm_setzero_pd(), inf = _mm_set1_pd(INFINITY);
    const __m128d vfx = _mm_set1_pd(fx), vfy = _mm_set1_pd(fy), vfz = _mm_set1_pd(fz);
    const __m128d vrx = _mm_set1_pd(rx), vry = _mm_set1_pd(ry), vrz = _mm_set1_pd(rz);
    const __m128d syux = _mm_set1_pd(sy * ux), syuy = _mm_set1_pd(sy * uy), syuz = _mm_set1_pd(sy * uz);
    const __m128d vox = _mm_set1_pd(ox), voy = _mm_set1_pd(oy), voz = _mm_set1_pd(oz);
    const __m128d vex0 = _mm_set1_pd(ex0), vey0 = _mm_set1_pd(ey0), vez0 = _mm_set1_pd(ez0);
    const __m128d vex1 = _mm_set1_pd(ex1), vey1 = _mm_set1_pd(ey1), vez1 = _mm_set1_pd(ez1);
#define RC_SEL(m, a, b) _mm_or_pd(_mm_and_pd(m, a), _mm_andnot_pd(m, b))
    for (int q = 0; q < n; q += 2)
    {
        __m128d sx = _mm_loadu_pd(&R->sx[q]);
        __m128d dx = _mm_add_pd(_mm_add_pd(vfx, _mm_mul_pd(sx, vrx)), syux);
        __m128d dy = _mm_add_pd(_mm_add_pd(vfy, _mm_mul_pd(sx, vry)), syuy);
        __m128d dz = _mm_add_pd(_mm_add_pd(vfz, _mm_mul_pd(sx, vrz)), syuz);
        __m128d ss = _mm_add_pd(_mm_add_pd(_mm_mul_pd(dx, dx), _mm_mul_pd(dy, dy)), _mm_mul_pd(dz, dz));
        __m128d inv = _mm_div_pd(one, _mm_sqrt_pd(ss));
        __m128d d0 = _mm_mul_pd(dx, inv), d1 = _mm_mul_pd(dy, inv), d2 = _mm_mul_pd(dz, inv);
        _mm_storeu_pd(&R->dx[q], d0), _mm_storeu_pd(&R->dy[q], d1), _mm_storeu_pd(&R->dz[q], d2);
        _mm_storeu_pd(&R->fwd[q], _mm_add_pd(_mm_add_pd(_mm_mul_pd(d0, vfx), _mm_mul_pd(d1, vfy)), _mm_mul_pd(d2, vfz)));
        __m128d n0 = _mm_cmpneq_pd(d0, zero), n1 = _mm_cmpneq_pd(d1, zero), n2 = _mm_cmpneq_pd(d2, zero);
        __m128d r0 = _mm_and_pd(n0, _mm_div_pd(one, d0)), r1 = _mm_and_pd(n1, _mm_div_pd(one, d1)),
                r2 = _mm_and_pd(n2, _mm_div_pd(one, d2));
        _mm_storeu_pd(&R->idx[q], r0), _mm_storeu_pd(&R->idy[q], r1), _mm_storeu_pd(&R->idz[q], r2);
        __m128d t0 = _mm_mul_pd(_mm_sub_pd(RC_SEL(_mm_cmpgt_pd(d0, zero), vex1, vex0), vox), r0);
        __m128d t1 = _mm_mul_pd(_mm_sub_pd(RC_SEL(_mm_cmpgt_pd(d1, zero), vey1, vey0), voy), r1);
        __m128d t2 = _mm_mul_pd(_mm_sub_pd(RC_SEL(_mm_cmpgt_pd(d2, zero), vez1, vez0), voz), r2);
        _mm_storeu_pd(&R->tx[q], RC_SEL(n0, t0, inf)), _mm_storeu_pd(&R->ty[q], RC_SEL(n1, t1, inf));
        _mm_storeu_pd(&R->tz[q], RC_SEL(n2, t2, inf));
    }
#undef RC_SEL
#else
    for (int q = 0; q < n; ++q)
    {
        rcr sx = R->sx[q];
        rcr dx = fx + sx * rx + sy * ux, dy = fy + sx * ry + sy * uy, dz = fz + sx * rz + sy * uz;
        rcr inv = RC_K(1.0) / RC_SQRT(dx * dx + dy * dy + dz * dz);
        rcr d0 = dx * inv, d1 = dy * inv, d2 = dz * inv;
#if RC_FIX
        d0 = rc_fxqf(d0), d1 = rc_fxqf(d1), d2 = rc_fxqf(d2);
#endif
#if RC_FP == 16
        /* (FP16, lane/cxf16: a component whose reciprocal overflows a half, |d| under 1/65504, is 0, the ray not moving
         * on that axis, as d +-0: its 1 / d would be inf and a boundary at the eye's own offset 0 x inf = NaN, which the
         * reference's t <= range and the runs' t >= tlim read differently: nether-trip-s1 row 125 at 128x128) */
        d0 = RC_FABS(RC_K(1.0) / d0) <= RC_K(65504.0) ? d0 : RC_K(0.0);
        d1 = RC_FABS(RC_K(1.0) / d1) <= RC_K(65504.0) ? d1 : RC_K(0.0);
        d2 = RC_FABS(RC_K(1.0) / d2) <= RC_K(65504.0) ? d2 : RC_K(0.0);
#endif
        R->dx[q] = d0, R->dy[q] = d1, R->dz[q] = d2;
        R->fwd[q] = d0 * fx + d1 * fy + d2 * fz;
        rcr i0_ = RC_K(1.0) / d0, i1_ = RC_K(1.0) / d1, i2_ = RC_K(1.0) / d2;
        i0_ = d0 != RC_K(0.0) ? i0_ : RC_K(0.0), i1_ = d1 != RC_K(0.0) ? i1_ : RC_K(0.0), i2_ = d2 != RC_K(0.0) ? i2_ : RC_K(0.0);
        R->idx[q] = i0_, R->idy[q] = i1_, R->idz[q] = i2_;
        rcr t0 = ((d0 > RC_K(0.0) ? ex1 : ex0) - ox) * i0_, t1 = ((d1 > RC_K(0.0) ? ey1 : ey0) - oy) * i1_,
            t2 = ((d2 > RC_K(0.0) ? ez1 : ez0) - oz) * i2_;
        R->tx[q] = d0 != RC_K(0.0) ? t0 : INFINITY, R->ty[q] = d1 != RC_K(0.0) ? t1 : INFINITY, R->tz[q] = d2 != RC_K(0.0) ? t2 : INFINITY;
    }
#endif
}

/* the nearest entity box each of the batch's pixels (p0 .. p0 + n - 1 of a row-major frame w wide) sees (walk's own
 * test before it moved to rays_fill): each entity over the pixels of its screen rectangle alone, entities in their
 * order, a pixel taking one only nearer than the one it has, so each pixel meets the same boxes in the same order as
 * a test of every box at every pixel (lane/castwet: on the 8 seed starts, their squids in the water, that test was
 * 1.5% of the cycles); where the CPU has AVX-512, a row's pixels RC_W at a time (ents_lanes) */
#if defined(RC_AVX2)
/* The lanes (lane/hostf32): RC_W rays a step, each a lane of a zmm. FP64: eight doubles. FP32: RC_W32 floats, 16 (the
 * default: a zmm of floats) or 8 (a ymm, AVX-512VL's forms, the lanes before lane/hostf32); a build sets it with
 * -DRC_W32=8. Every operation the same IEEE operation in each lane as the scalar's, so the lane count changes no byte.
 * Beside the scalar vector RV: RI, as many int32 (a cell, a step, an index: a ymm for eight, a zmm for 16); RM, a
 * mask of RC_W lanes and its operations RK_*; RP, each lane's section address (rp_*: one zmm of eight pointers, two
 * for 16) */
#if !defined(RC_W32)
#define RC_W32 16
#endif
#if RC_FP == 32 && RC_W32 == 16
#define RC_W 16
#define RV __m512
#define RV_SET1(x) _mm512_set1_ps(x)
#define RV_ZERO() _mm512_setzero_ps()
#define RV_ADD(a, b) _mm512_add_ps(a, b)
#define RV_SUB(a, b) _mm512_sub_ps(a, b)
#define RV_MUL(a, b) _mm512_mul_ps(a, b)
#define RV_ABS(a) _mm512_abs_ps(a)
#define RV_MIN(a, b) _mm512_min_ps(a, b)
#define RV_CMP(a, b, c) _mm512_cmp_ps_mask(a, b, c)
#define RV_MCMP(k, a, b, c) _mm512_mask_cmp_ps_mask(k, a, b, c)
#define RV_MMOV(s, k, a) _mm512_mask_mov_ps(s, k, a)
#define RV_MBLEND(k, a, b) _mm512_mask_blend_ps(k, a, b)
#define RV_MMUL(s, k, a, b) _mm512_mask_mul_ps(s, k, a, b)
#define RV_MADD(s, k, a, b) _mm512_mask_add_ps(s, k, a, b)
#define RV_ZLOADU(k, p) _mm512_maskz_loadu_ps(k, p)
#define RV_MSTOREU(p, k, a) _mm512_mask_storeu_ps(p, k, a)
#define RV_MSTORE(p, k, a) _mm512_mask_store_ps(p, k, a)
#define RV_STORE(p, a) _mm512_store_ps(p, a)
#define RV_MCVTI(s, k, i) _mm512_mask_cvtepi32_ps(s, k, i)
#define RV_CVTI(i) _mm512_cvtepi32_ps(i)
#define RV_MEXPAND(s, k, p) _mm512_mask_expandloadu_ps(s, k, p)
#define RV_ZEXPAND(k, p) _mm512_maskz_expandloadu_ps(k, p)
#define RV_MGATHER(s, k, i, p) _mm512_mask_i32gather_ps(s, k, i, p, 4)
#define RI __m512i
#define RM __mmask16
#define RM_ALL ((__mmask16)0xffff)
#define RK_AND(a, b) _kand_mask16(a, b)
#define RK_ANDN(a, b) _kandn_mask16(a, b)
#define RK_OR(a, b) _kor_mask16(a, b)
#define RK_NOT(a) _knot_mask16(a)
#define RI_ZERO() _mm512_setzero_si512()
#define RI_SET1(x) _mm512_set1_epi32(x)
#define RI_IOTA() _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
#define RI_AND(a, b) _mm512_and_si512(a, b)
#define RI_OR(a, b) _mm512_or_si512(a, b)
#define RI_XOR(a, b) _mm512_xor_si512(a, b)
#define RI_ADD(a, b) _mm512_add_epi32(a, b)
#define RI_SUB(a, b) _mm512_sub_epi32(a, b)
#define RI_MULLO(a, b) _mm512_mullo_epi32(a, b)
#define RI_SLLI(a, n) _mm512_slli_epi32(a, n)
#define RI_SRLI(a, n) _mm512_srli_epi32(a, n)
#define RI_SRAI(a, n) _mm512_srai_epi32(a, n)
#define RI_MMOV(s, k, a) _mm512_mask_mov_epi32(s, k, a)
#define RI_ZMOV(k, a) _mm512_maskz_mov_epi32(k, a)
#define RI_MADD(s, k, a, b) _mm512_mask_add_epi32(s, k, a, b)
#define RI_MSUB(s, k, a, b) _mm512_mask_sub_epi32(s, k, a, b)
#define RI_CMPEQ(a, b) _mm512_cmpeq_epi32_mask(a, b)
#define RI_MCMPEQ(k, a, b) _mm512_mask_cmpeq_epi32_mask(k, a, b)
#define RI_CMPGT(a, b) _mm512_cmpgt_epi32_mask(a, b)
#define RI_CMPLT(a, b) _mm512_cmplt_epi32_mask(a, b)
#define RI_CMPLTU(a, b) _mm512_cmplt_epu32_mask(a, b)
#define RI_MCMPLTU(k, a, b) _mm512_mask_cmplt_epu32_mask(k, a, b)
#define RI_MTEST(k, a, b) _mm512_mask_test_epi32_mask(k, a, b)
#define RI_LOAD(p) _mm512_load_si512((const void *)(p))
#define RI_STORE(p, a) _mm512_store_si512((void *)(p), a)
#define RI_MSTORE(p, k, a) _mm512_mask_store_epi32(p, k, a)
#define RI_MSTOREU(p, k, a) _mm512_mask_storeu_epi32(p, k, a)
#define RI_MEXPAND(s, k, a) _mm512_mask_expand_epi32(s, k, a)
#define RI_MEXPANDLOAD(s, k, p) _mm512_mask_expandloadu_epi32(s, k, p)
#define RI_MGATHER(s, k, i, p) _mm512_mask_i32gather_epi32(s, k, i, p, 4)
/* the lanes' ids from their addresses (ad: RC_W of them) */
#define RI_BYTES(ad) _mm512_setr_epi32(RC_IDB(ad, 0), RC_IDB(ad, 1), RC_IDB(ad, 2), RC_IDB(ad, 3), RC_IDB(ad, 4), \
    RC_IDB(ad, 5), RC_IDB(ad, 6), RC_IDB(ad, 7), RC_IDB(ad, 8), RC_IDB(ad, 9), RC_IDB(ad, 10), RC_IDB(ad, 11), \
    RC_IDB(ad, 12), RC_IDB(ad, 13), RC_IDB(ad, 14), RC_IDB(ad, 15))
#else
#define RC_W 8
#define RI __m256i
#define RM __mmask8
#define RM_ALL ((__mmask8)0xff)
#define RK_AND(a, b) _kand_mask8(a, b)
#define RK_ANDN(a, b) _kandn_mask8(a, b)
#define RK_OR(a, b) _kor_mask8(a, b)
#define RK_NOT(a) _knot_mask8(a)
#define RI_ZERO() _mm256_setzero_si256()
#define RI_SET1(x) _mm256_set1_epi32(x)
#define RI_IOTA() _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7)
#define RI_AND(a, b) _mm256_and_si256(a, b)
#define RI_OR(a, b) _mm256_or_si256(a, b)
#define RI_XOR(a, b) _mm256_xor_si256(a, b)
#define RI_ADD(a, b) _mm256_add_epi32(a, b)
#define RI_SUB(a, b) _mm256_sub_epi32(a, b)
#define RI_MULLO(a, b) _mm256_mullo_epi32(a, b)
#define RI_SLLI(a, n) _mm256_slli_epi32(a, n)
#define RI_SRLI(a, n) _mm256_srli_epi32(a, n)
#define RI_SRAI(a, n) _mm256_srai_epi32(a, n)
#define RI_MMOV(s, k, a) _mm256_mask_mov_epi32(s, k, a)
#define RI_ZMOV(k, a) _mm256_maskz_mov_epi32(k, a)
#define RI_MADD(s, k, a, b) _mm256_mask_add_epi32(s, k, a, b)
#define RI_MSUB(s, k, a, b) _mm256_mask_sub_epi32(s, k, a, b)
#define RI_CMPEQ(a, b) _mm256_cmpeq_epi32_mask(a, b)
#define RI_MCMPEQ(k, a, b) _mm256_mask_cmpeq_epi32_mask(k, a, b)
#define RI_CMPGT(a, b) _mm256_cmpgt_epi32_mask(a, b)
#define RI_CMPLT(a, b) _mm256_cmplt_epi32_mask(a, b)
#define RI_CMPLTU(a, b) _mm256_cmplt_epu32_mask(a, b)
#define RI_MCMPLTU(k, a, b) _mm256_mask_cmplt_epu32_mask(k, a, b)
#define RI_MTEST(k, a, b) _mm256_mask_test_epi32_mask(k, a, b)
#define RI_LOAD(p) _mm256_load_si256((const __m256i *)(p))
#define RI_STORE(p, a) _mm256_store_si256((__m256i *)(p), a)
#define RI_MSTORE(p, k, a) _mm256_mask_store_epi32(p, k, a)
#define RI_MSTOREU(p, k, a) _mm256_mask_storeu_epi32(p, k, a)
#define RI_MEXPAND(s, k, a) _mm256_mask_expand_epi32(s, k, a)
#define RI_MEXPANDLOAD(s, k, p) _mm256_mask_expandloadu_epi32(s, k, p)
#define RI_MGATHER(s, k, i, p) _mm256_mmask_i32gather_epi32(s, k, i, p, 4)
#define RI_BYTES(ad) _mm256_setr_epi32(RC_IDB(ad, 0), RC_IDB(ad, 1), RC_IDB(ad, 2), RC_IDB(ad, 3), RC_IDB(ad, 4), \
    RC_IDB(ad, 5), RC_IDB(ad, 6), RC_IDB(ad, 7))
#endif
#define RC_IDB(ad, i) (*(const uint8_t *)(uintptr_t)(ad)[i])
#if RC_FP == 32 && RC_W32 == 16
#elif RC_FP == 32
#define RV __m256
#define RV_SET1(x) _mm256_set1_ps(x)
#define RV_ZERO() _mm256_setzero_ps()
#define RV_ADD(a, b) _mm256_add_ps(a, b)
#define RV_SUB(a, b) _mm256_sub_ps(a, b)
#define RV_MUL(a, b) _mm256_mul_ps(a, b)
#define RV_ABS(a) _mm256_andnot_ps(_mm256_set1_ps(-0.0f), a)
#define RV_MIN(a, b) _mm256_min_ps(a, b)
#define RV_CMP(a, b, c) _mm256_cmp_ps_mask(a, b, c)
#define RV_MCMP(k, a, b, c) _mm256_mask_cmp_ps_mask(k, a, b, c)
#define RV_MMOV(s, k, a) _mm256_mask_mov_ps(s, k, a)
#define RV_MBLEND(k, a, b) _mm256_mask_blend_ps(k, a, b)
#define RV_MMUL(s, k, a, b) _mm256_mask_mul_ps(s, k, a, b)
#define RV_MADD(s, k, a, b) _mm256_mask_add_ps(s, k, a, b)
#define RV_ZLOADU(k, p) _mm256_maskz_loadu_ps(k, p)
#define RV_MSTOREU(p, k, a) _mm256_mask_storeu_ps(p, k, a)
#define RV_MSTORE(p, k, a) _mm256_mask_store_ps(p, k, a)
#define RV_STORE(p, a) _mm256_store_ps(p, a)
#define RV_MCVTI(s, k, i) _mm256_mask_cvtepi32_ps(s, k, i)
#define RV_CVTI(i) _mm256_cvtepi32_ps(i)
#define RV_MEXPAND(s, k, p) _mm256_mask_expandloadu_ps(s, k, p)
#define RV_ZEXPAND(k, p) _mm256_maskz_expandloadu_ps(k, p)
#define RV_MGATHER(s, k, i, p) _mm256_mmask_i32gather_ps(s, k, i, p, 4)
#else
#define RV __m512d
#define RV_SET1(x) _mm512_set1_pd(x)
#define RV_ZERO() _mm512_setzero_pd()
#define RV_ADD(a, b) _mm512_add_pd(a, b)
#define RV_SUB(a, b) _mm512_sub_pd(a, b)
#define RV_MUL(a, b) _mm512_mul_pd(a, b)
#define RV_ABS(a) _mm512_abs_pd(a)
#define RV_MIN(a, b) _mm512_min_pd(a, b)
#define RV_CMP(a, b, c) _mm512_cmp_pd_mask(a, b, c)
#define RV_MCMP(k, a, b, c) _mm512_mask_cmp_pd_mask(k, a, b, c)
#define RV_MMOV(s, k, a) _mm512_mask_mov_pd(s, k, a)
#define RV_MBLEND(k, a, b) _mm512_mask_blend_pd(k, a, b)
#define RV_MMUL(s, k, a, b) _mm512_mask_mul_pd(s, k, a, b)
#define RV_MADD(s, k, a, b) _mm512_mask_add_pd(s, k, a, b)
#define RV_ZLOADU(k, p) _mm512_maskz_loadu_pd(k, p)
#define RV_MSTOREU(p, k, a) _mm512_mask_storeu_pd(p, k, a)
#define RV_MSTORE(p, k, a) _mm512_mask_store_pd(p, k, a)
#define RV_STORE(p, a) _mm512_store_pd(p, a)
#define RV_MCVTI(s, k, i) _mm512_mask_cvtepi32_pd(s, k, i)
#define RV_CVTI(i) _mm512_cvtepi32_pd(i)
#define RV_MEXPAND(s, k, p) _mm512_mask_expandloadu_pd(s, k, p)
#define RV_ZEXPAND(k, p) _mm512_maskz_expandloadu_pd(k, p)
#define RV_MGATHER(s, k, i, p) _mm512_mask_i32gather_pd(s, k, i, p, 8)
#endif

/* RP: each lane's section's ids as an address (a lane's cell ci at base + ci); zmm of eight 64-bit addresses, two
 * for 16 lanes (lo the lanes 0 to 7). rp_gather: each lane k's of table tab at index si (the others 0); rp_test: the
 * lanes of k whose address is not 0; rp_maddoff: the lanes of k p + off, the others s's; rp_addr: base + ci into ad */
#define RC_LT __attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), always_inline)) static inline
/* knobs (lane/hostf32's grid): RC_EGATHER 0 looks a section up for each lane crossing into it by a load of its own
 * (a step's few edge lanes) instead of the gather, 2 so when two lanes or fewer cross and by the gathers else;RC_IDGATHER 1 reads the lanes' ids by gathers of the dword each id
 * ends instead of a load a lane */
#if !defined(RC_EGATHER)
#define RC_EGATHER 1
#endif
#if !defined(RC_IDGATHER)
#define RC_IDGATHER 0
#endif
/* RC_IDPACK (the id read by loads, rp_ids): 0 the compiler's (the addresses out of the registers, the bytes into a
 * vector by inserts), 1 the addresses from memory and the bytes packed by shifts, 2 the addresses from memory and the
 * bytes stored and loaded as a vector, 3 the bytes packed by shifts, 4 (16 lanes) two eight-lane halves joined */
#if !defined(RC_IDPACK)
#define RC_IDPACK 0
#endif
/* RC_HBSCATTER 1 (FP32 at 16 lanes): hand_back's fields to the dead lanes' rays by scatters */
#if !defined(RC_HBSCATTER)
#define RC_HBSCATTER 0
#endif
#if RC_W == 16
typedef struct {
    __m512i lo, hi;
} RP;
RC_LT RP rp_set1(const void *p)
{
    const __m512i v = _mm512_set1_epi64((int64_t)(uintptr_t)p);
    return (RP){v, v};
}
RC_LT RP rp_mmov(RP s, RM k, RP a)
{
    return (RP){_mm512_mask_mov_epi64(s.lo, (__mmask8)k, a.lo), _mm512_mask_mov_epi64(s.hi, (__mmask8)(k >> 8), a.hi)};
}
RC_LT RP rp_gather(RM k, RI si, const void *tab)
{
    const __m512i z = _mm512_setzero_si512();
#if RC_EGATHER == 2
    if (__builtin_popcount(k) <= 2) goto loads;
#endif
#if RC_EGATHER
    return (RP){_mm512_mask_i32gather_epi64(z, (__mmask8)k, _mm512_castsi512_si256(si), tab, 8),
                _mm512_mask_i32gather_epi64(z, (__mmask8)(k >> 8), _mm512_extracti64x4_epi64(si, 1), tab, 8)};
#endif
#if RC_EGATHER != 1
#if RC_EGATHER == 2
loads:;
#endif
    int32_t ix[16] __attribute__((aligned(64)));
    RI_STORE(ix, si);
    RP r = {z, z};
    for (unsigned m = k; m; m &= m - 1)
    {
        const unsigned j = (unsigned)__builtin_ctz(m);
        const int64_t v = ((const int64_t *)tab)[ix[j]];
        if (j < 8) r.lo = _mm512_mask_set1_epi64(r.lo, (__mmask8)(1u << j), v);
        else r.hi = _mm512_mask_set1_epi64(r.hi, (__mmask8)(1u << (j - 8)), v);
    }
    return r;
#endif
}
RC_LT RM rp_test(RM k, RP p)
{
    return _mm512_kunpackb((__mmask16)_mm512_mask_test_epi64_mask((__mmask8)(k >> 8), p.hi, p.hi),
                           (__mmask16)_mm512_mask_test_epi64_mask((__mmask8)k, p.lo, p.lo));
}
RC_LT RP rp_maddoff(RP s, RM k, RP p, int64_t off)
{
    const __m512i o = _mm512_set1_epi64(off);
    return (RP){_mm512_mask_add_epi64(s.lo, (__mmask8)k, p.lo, o), _mm512_mask_add_epi64(s.hi, (__mmask8)(k >> 8), p.hi, o)};
}
RC_LT void rp_addr(uint64_t *ad, RP base, RI ci)
{
    _mm512_store_si512((void *)ad, _mm512_add_epi64(base.lo, _mm512_cvtepi32_epi64(_mm512_castsi512_si256(ci))));
    _mm512_store_si512((void *)(ad + 8), _mm512_add_epi64(base.hi, _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(ci, 1))));
}
RC_LT void rp_store(uint64_t *bs, RP base)
{
    _mm512_store_si512((void *)bs, base.lo), _mm512_store_si512((void *)(bs + 8), base.hi);
}
/* (RC_BRICK) a 64-bit value in every lane; the lanes of k whose bit bi of m is set */
RC_LT __attribute__((unused)) RP rp_set1q(uint64_t v)
{
    const __m512i x = _mm512_set1_epi64((int64_t)v);
    return (RP){x, x};
}
RC_LT __attribute__((unused)) RM rp_bocc(RM k, RP m, RI bi)
{
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i lo = _mm512_srlv_epi64(m.lo, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(bi)));
    const __m512i hi = _mm512_srlv_epi64(m.hi, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(bi, 1)));
    return _mm512_kunpackb((__mmask16)_mm512_mask_test_epi64_mask((__mmask8)(k >> 8), hi, one),
                           (__mmask16)_mm512_mask_test_epi64_mask((__mmask8)k, lo, one));
}
#else
typedef __m512i RP;
RC_LT RP rp_set1(const void *p)
{
    return _mm512_set1_epi64((int64_t)(uintptr_t)p);
}
RC_LT RP rp_mmov(RP s, RM k, RP a)
{
    return _mm512_mask_mov_epi64(s, k, a);
}
RC_LT RP rp_gather(RM k, RI si, const void *tab)
{
#if RC_EGATHER == 2
    if (__builtin_popcount(k) <= 2) goto loads;
#endif
#if RC_EGATHER
    return _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), k, si, tab, 8);
#endif
#if RC_EGATHER != 1
#if RC_EGATHER == 2
loads:;
#endif
    int32_t ix[8] __attribute__((aligned(32)));
    RI_STORE(ix, si);
    RP r = _mm512_setzero_si512();
    for (unsigned m = k; m; m &= m - 1)
    {
        const unsigned j = (unsigned)__builtin_ctz(m);
        r = _mm512_mask_set1_epi64(r, (__mmask8)(1u << j), ((const int64_t *)tab)[ix[j]]);
    }
    return r;
#endif
}
RC_LT RM rp_test(RM k, RP p)
{
    return _mm512_mask_test_epi64_mask(k, p, p);
}
RC_LT RP rp_maddoff(RP s, RM k, RP p, int64_t off)
{
    return _mm512_mask_add_epi64(s, k, p, _mm512_set1_epi64(off));
}
RC_LT void rp_addr(uint64_t *ad, RP base, RI ci)
{
    _mm512_store_si512((void *)ad, _mm512_add_epi64(base, _mm512_cvtepi32_epi64(ci)));
}
RC_LT void rp_store(uint64_t *bs, RP base)
{
    _mm512_store_si512((void *)bs, base);
}
RC_LT __attribute__((unused)) RP rp_set1q(uint64_t v)
{
    return _mm512_set1_epi64((int64_t)v);
}
RC_LT __attribute__((unused)) RM rp_bocc(RM k, RP m, RI bi)
{
    return _mm512_mask_test_epi64_mask(k, _mm512_srlv_epi64(m, _mm512_cvtepu32_epi64(bi)), _mm512_set1_epi64(1));
}
#endif
/* each lane's id at base + ci: a load a lane, or (RC_IDGATHER) the dword ending at it gathered, its top byte (the
 * three bytes before a section's ids are its own: ids_hi first; before rc_zero_ids, other statics) */
RC_LT RI rp_ids(RP base, RI ci)
{
#if RC_IDGATHER && RC_W == 16
    const __m512i m3 = _mm512_set1_epi64(-3);
    const __m256i lo = _mm512_i64gather_epi32(_mm512_add_epi64(_mm512_add_epi64(base.lo, _mm512_cvtepi32_epi64(_mm512_castsi512_si256(ci))), m3),
                                              (const void *)0, 1);
    const __m256i hi = _mm512_i64gather_epi32(_mm512_add_epi64(_mm512_add_epi64(base.hi, _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(ci, 1))), m3),
                                              (const void *)0, 1);
    return _mm512_srli_epi32(_mm512_inserti64x4(_mm512_castsi256_si512(lo), hi, 1), 24);
#elif RC_IDGATHER
    const __m256i d = _mm512_i64gather_epi32(_mm512_add_epi64(_mm512_add_epi64(base, _mm512_cvtepi32_epi64(ci)), _mm512_set1_epi64(-3)),
                                             (const void *)0, 1);
    return _mm256_srli_epi32(d, 24);
#else
    uint64_t ad[RC_W] __attribute__((aligned(64)));
    rp_addr(ad, base, ci);
#if RC_IDPACK == 1 || RC_IDPACK == 2
    /* the addresses read back from memory a lane at a time (the load ports'), not taken out of the registers (the
     * shuffles': valignq, vextracti64x2, vmovq) */
    __asm__("" : "+m"(*(uint64_t(*)[RC_W])ad));
#endif
#if RC_IDPACK == 4 && RC_W == 16
    /* two halves as the eight-lane build makes its one (the bytes packed by shifts), then joined */
    return _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_setr_epi32(RC_IDB(ad, 0), RC_IDB(ad, 1), RC_IDB(ad, 2),
        RC_IDB(ad, 3), RC_IDB(ad, 4), RC_IDB(ad, 5), RC_IDB(ad, 6), RC_IDB(ad, 7))), _mm256_setr_epi32(RC_IDB(ad, 8),
        RC_IDB(ad, 9), RC_IDB(ad, 10), RC_IDB(ad, 11), RC_IDB(ad, 12), RC_IDB(ad, 13), RC_IDB(ad, 14), RC_IDB(ad, 15)), 1);
#endif
#if RC_IDPACK == 1 || RC_IDPACK == 3
    /* the bytes packed eight to a 64-bit register by shifts, then widened */
    uint64_t lo = 0;
    for (int j = 7; j >= 0; --j) lo = lo << 8 | *(const uint8_t *)(uintptr_t)ad[j];
#if RC_W == 16
    uint64_t hi = 0;
    for (int j = 15; j >= 8; --j) hi = hi << 8 | *(const uint8_t *)(uintptr_t)ad[j];
    return _mm512_cvtepu8_epi32(_mm_set_epi64x((long long)hi, (long long)lo));
#else
    return _mm256_cvtepu8_epi32(_mm_cvtsi64_si128((long long)lo));
#endif
#elif RC_IDPACK == 2
    /* the bytes stored and loaded as one vector */
    uint8_t b[16] __attribute__((aligned(16)));
    for (int j = 0; j < RC_W; ++j) b[j] = *(const uint8_t *)(uintptr_t)ad[j];
    __asm__("" : "+m"(b));
#if RC_W == 16
    return _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i *)b));
#else
    return _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *)b));
#endif
#else
    return RI_BYTES(ad);
#endif
#endif
}

/* (RC_BRICK) the ids of the lanes of occ at base + ci, 0 in the others: RC_BRICK 1 a load each (the addresses from
 * memory), 2 the dword each id ends gathered under the mask (rp_ids' RC_IDGATHER) */
RC_LT __attribute__((unused)) RI rp_ids_m(RP base, RI ci, RM occ)
{
#if RC_BRICK == 2
#if RC_W == 16
    const __m512i m3 = _mm512_set1_epi64(-3);
    const __m256i z = _mm256_setzero_si256();
    const __m256i lo = _mm512_mask_i64gather_epi32(z, (__mmask8)occ,
        _mm512_add_epi64(_mm512_add_epi64(base.lo, _mm512_cvtepi32_epi64(_mm512_castsi512_si256(ci))), m3), (const void *)0, 1);
    const __m256i hi = _mm512_mask_i64gather_epi32(z, (__mmask8)(occ >> 8),
        _mm512_add_epi64(_mm512_add_epi64(base.hi, _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(ci, 1))), m3), (const void *)0, 1);
    return _mm512_srli_epi32(_mm512_inserti64x4(_mm512_castsi256_si512(lo), hi, 1), 24);
#else
    const __m256i d = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), occ,
        _mm512_add_epi64(_mm512_add_epi64(base, _mm512_cvtepi32_epi64(ci)), _mm512_set1_epi64(-3)), (const void *)0, 1);
    return _mm256_srli_epi32(d, 24);
#endif
#else
    uint64_t ad[RC_W] __attribute__((aligned(64)));
    rp_addr(ad, base, ci);
    RI w = RI_ZERO();
    for (unsigned m = occ; m; m &= m - 1)
    {
        const unsigned j = (unsigned)__builtin_ctz(m);
#if RC_W == 16
        w = _mm512_mask_set1_epi32(w, (__mmask16)(1u << j), *(const uint8_t *)(uintptr_t)ad[j]);
#else
        w = _mm256_mask_set1_epi32(w, (__mmask8)(1u << j), *(const uint8_t *)(uintptr_t)ad[j]);
#endif
    }
    return w;
#endif
}

/* rays_ents' test of box e over the batch's pixels qa .. qb (a row's, in order), RC_W a step in AVX-512's lanes: each
 * lane the scalar test's operations in its order (the slab's t by the same subtraction and product, its swap and the
 * nearest entry's axis by the same strict compares; t is never NaN past them: tn and tf change only on an ordered
 * compare), so each pixel takes the same box */
__attribute__((target("avx512f,avx512vl"))) static void ents_lanes(const struct rc *k, struct rcrays *R,
                                                                   const struct rcent *en, int e, int qa, int qb)
{
    const RV zero = RV_ZERO(), rnext = RV_SET1(k->rnext);
    const RV ninf = RV_SET1(-INFINITY), pinf = RV_SET1(INFINITY);
    const rcr o3[3] = {k->ox, k->oy, k->oz};
    const rcr *const D[3] = {R->dx, R->dy, R->dz}, *const I[3] = {R->idx, R->idy, R->idz};
    /* entry_face's: the face entered across axis a going + (and + 1 going -) */
    static const int F0[3] = {4, 0, 2};
    for (int q = qa; q <= qb; q += RC_W)
    {
        const RM m = qb - q >= RC_W - 1 ? RM_ALL : (RM)((1u << (qb - q + 1)) - 1u);
        RV tn = ninf, tf = pinf;
        RI face = RI_ZERO();
        RM miss = 0, any = 0;
        for (int a = 0; a < 3; ++a)
        {
            const RV d = RV_ZLOADU(m, D[a] + q), id = RV_ZLOADU(m, I[a] + q);
            const RM pos = RV_CMP(d, zero, _CMP_GT_OQ), mv = pos | RV_CMP(d, zero, _CMP_LT_OQ);
            /* not moving along a: inside the slab or no hit */
            if (o3[a] < en->box[a] || o3[a] > en->box[a + 3]) miss |= (RM)~mv;
            const RV t0 = RV_MUL(RV_SET1(en->box[a] - o3[a]), id);
            const RV t1 = RV_MUL(RV_SET1(en->box[a + 3] - o3[a]), id);
            const RM sw = RV_CMP(t0, t1, _CMP_GT_OQ);
            const RV lo = RV_MBLEND(sw, t0, t1), hi = RV_MBLEND(sw, t1, t0);
            const RM up = mv & RV_CMP(lo, tn, _CMP_GT_OQ);
            tn = RV_MMOV(tn, up, lo);
            face = RI_MMOV(face, up, RI_MMOV(RI_SET1(F0[a] + 1), pos, RI_SET1(F0[a])));
            any |= up;
            tf = RV_MMOV(tf, mv & RV_CMP(hi, tf, _CMP_LT_OQ), hi);
        }
        /* the scalar's take: no miss, an entry axis, tn <= tf, 0 < tn, nearer than the pixel's box */
        const RM take = m & ~miss & any & RV_CMP(tn, tf, _CMP_LE_OQ) & RV_CMP(tn, zero, _CMP_GT_OQ) &
                        RV_CMP(tn, RV_ZLOADU(m, R->te + q), _CMP_LT_OQ);
        if (take == 0) continue;
        RV_MSTOREU(R->te + q, take, tn);
        RV_MSTOREU(R->tlim + q, take, RV_MBLEND(RV_CMP(tn, rnext, _CMP_LT_OQ), rnext, tn));
        RI_MSTOREU(R->ent + q, take, RI_SET1(e));
        RI_MSTOREU(R->eface + q, take, face);
    }
}
#endif

static void rays_ents(const struct rc *k, struct rcrays *R, int p0, int n, int w)
{
    const int ja = p0 / w, jb = (p0 + n - 1) / w;
#if defined(RC_AVX2)
    const int lanes = rc_lanes();
#endif
    for (int e = 0; e < k->nent; ++e)
    {
        const struct rcent *en = &k->ent[e];
        const int j0 = en->j0 > ja ? en->j0 : ja, j1 = en->j1 < jb ? en->j1 : jb;
        for (int j = j0; j <= j1; ++j)
        {
            /* the row's pixels in the batch and the rectangle */
            int ia = j == ja ? p0 - ja * w : 0, ib = j == jb ? p0 + n - 1 - jb * w : w - 1;
            if (en->i0 > ia) ia = en->i0;
            if (en->i1 < ib) ib = en->i1;
#if defined(RC_AVX2)
            if (lanes)
            {
                if (ia <= ib) ents_lanes(k, R, en, e, j * w + ia - p0, j * w + ib - p0);
                continue;
            }
#endif
            for (int i = ia; i <= ib; ++i)
            {
                const int qq = j * w + i - p0;
                const rcr idx = R->idx[qq], idy = R->idy[qq], idz = R->idz[qq];
                const int sx = (R->dx[qq] > 0) - (R->dx[qq] < 0), sy_ = (R->dy[qq] > 0) - (R->dy[qq] < 0),
                          sz = (R->dz[qq] > 0) - (R->dz[qq] < 0);
                rcr tn = -INFINITY, tf = INFINITY;
                int ax = -1;
                const rcr o3[3] = {k->ox, k->oy, k->oz}, i3[3] = {idx, idy, idz};
                const int s3[3] = {sx, sy_, sz};
                int miss = 0;
                for (int a = 0; a < 3 && !miss; ++a)
                {
                    if (!s3[a])
                    {
                        if (o3[a] < en->box[a] || o3[a] > en->box[a + 3]) miss = 1;
                        continue;
                    }
                    rcr t0 = (en->box[a] - o3[a]) * i3[a], t1 = (en->box[a + 3] - o3[a]) * i3[a];
                    if (t0 > t1) { rcr s = t0; t0 = t1; t1 = s; }
                    if (t0 > tn) tn = t0, ax = a;
                    if (t1 < tf) tf = t1;
                }
                if (miss || ax < 0 || tn > tf || tn <= RC_K(0.0) || tn >= R->te[qq]) continue;
                R->te[qq] = tn, R->ent[qq] = e, R->eface[qq] = entry_face(ax, s3[ax]);
                R->tlim[qq] = tn < k->rnext ? tn : k->rnext;
            }
        }
    }
}

/* pixels p0 .. p0 + n - 1 of a w x h frame into the batch: each row's run of rays from rays_row (cast's sy for the
 * row), each walk starting at the eye, and the nearest entity box each pixel sees (rays_ents) */
static void rays_fill(const struct rc *k, struct rcrays *R, int p0, int n, int w, int h, rcr sxs)
{
    struct rcrow *row = k->row;
    for (int q = 0; q < n;)
    {
        const int j = (p0 + q) / w, i0 = (p0 + q) % w;
        int m = w - i0 < n - q ? w - i0 : n - q;
        if (m > RC_ROW) m = RC_ROW;
        rcr sy = (RC_K(1.0) - RC_K(2.0) * ((rcr)j + RC_K(0.5)) / (rcr)h) * RC_TAN_HALF;
        rays_row(k, row, R, q, i0, m, w, sy, sxs);
        for (int r = 0; r < m; ++r)
        {
            const int qq = q + r;
            R->ix[qq] = k->eye_x, R->iy[qq] = k->eye_y, R->iz[qq] = k->eye_z, R->how[qq] = RC_WALK, R->sfa[qq] = 0;
            R->te[qq] = INFINITY, R->ent[qq] = -1, R->eface[qq] = 1, R->tlim[qq] = k->rnext;
        }
        q += m;
    }
    if (k->nent > 0) rays_ents(k, R, p0, n, w);
}

/* The air prefix (rays_air): the steps walk's fast path takes through air before anything else happens to a ray,
 * taken for the whole batch first. With the eye in no liquid, those steps change none of walk's other state
 * (in_water, cull and eye_was_liquid stay 0, -1 and 0) and walk reads no cell before its first step, so walk from
 * where they leave a ray takes the same steps from there (lane/hostcast's air prefix). With the eye in water, a ray
 * not going up sees the water it crosses through as air (the fast loop's "the eye's own water: seen through", and
 * eye_was_liquid only matters to a ray going up). A ray going up from it sees that water through and stops at any
 * other cell, air too, onto which it is handed back as a block (RC_HIT): there walk's block: does what the fast loop
 * would with eye_was_liquid 1, which those steps keep (the water's surface from below on a step up, the air passed on
 * a step across; any other block taken as the loop takes it). The prefix
 * hands a ray back as the fast loop would go on from the step it stopped on: onto a block (RC_HIT: the cell and its
 * boundaries after the step, the axis and t, as the loop's take leaves them), past the ray's limit or out of y 0 ..
 * ytop (RC_END: walk's run_end with no block), or into a section with no blocks or ids past 255, or out of the
 * window (RC_WALK from the cell before). The caller holds what it needs: the fast path, the eye in no liquid or in
 * water, in a section with blocks and no ids past 255. A build with RC_AIR_VERIFY walks every ray again from the
 * eye with no prefix and counts the pixels that differ (cast). */
#if defined(RC_AVX2)
#define RC_LANES 1
_Static_assert(offsetof(struct chunk_sec, ids_hi) == 0, "a section's ids_hi is its first field");

/* RC_BRICKJ 1 (lane/castexact): the dry lanes that read air in a 4 x 4 x 4 brick of air (sec_bricks' bit clear) go to
 * the brick's last cell on their way at once: the state its steps would leave before the step out of it. Each axis'
 * steps to the brick's far side n (3 - (i & 3) going up, i & 3 down, none without a step); its boundaries' t after the
 * next, T_2 .. T_4, by the step's own expression ((u - o) id, u stepped by the same additions), so the same bits; the
 * step out is the least of the three far sides' t under the step's order (ties x, y, z), te; each other axis takes the
 * boundaries ordered before it (t <= te for an axis before the exit's, t < te after). Taken only when te is under the
 * ray's limit (no step inside could have ended the run); a lane not taken steps on. The fixed instance keeps its steps
 * (its order is the integers' near a tie, rc_fx_cmp, which these compares are not) */
#if !defined(RC_BRICKJ)
#define RC_BRICKJ 0
#endif
#if RC_BRICK && RC_BRICKJ && !RC_FIX
RC_LT void brick_axis(RI n, RV t, RV u, RV ds, RV o, RV id, RV *e, RV *t2, RV *t3, RV *t4, RV *u3, RV *u4, RV *u5)
{
    *u3 = RV_ADD(u, ds), *u4 = RV_ADD(*u3, ds), *u5 = RV_ADD(*u4, ds);
    *t2 = RV_MUL(RV_SUB(u, o), id), *t3 = RV_MUL(RV_SUB(*u3, o), id), *t4 = RV_MUL(RV_SUB(*u4, o), id);
    const RI ione = RI_SET1(1), itwo = RI_SET1(2), i3 = RI_SET1(3);
    *e = RV_MMOV(RV_MMOV(RV_MMOV(t, RI_CMPEQ(n, ione), *t2), RI_CMPEQ(n, itwo), *t3), RI_CMPEQ(n, i3), *t4);
}
/* the boundaries of an axis with n steps to go ordered before te: its own t, then T_2, T_3 (le: t <= te, else t < te) */
RC_LT RI brick_count(RM le, RI n, RV t, RV t2, RV t3, RV te)
{
    const RI izero = RI_ZERO(), ione = RI_SET1(1), i3 = RI_SET1(3);
    const RM c1 = RK_OR(RK_AND(le, RV_CMP(t, te, _CMP_LE_OQ)), RK_ANDN(le, RV_CMP(t, te, _CMP_LT_OQ)));
    const RM c2 = RK_OR(RK_AND(le, RV_CMP(t2, te, _CMP_LE_OQ)), RK_ANDN(le, RV_CMP(t2, te, _CMP_LT_OQ)));
    const RM c3 = RK_OR(RK_AND(le, RV_CMP(t3, te, _CMP_LE_OQ)), RK_ANDN(le, RV_CMP(t3, te, _CMP_LT_OQ)));
    RI m = izero;
    m = RI_MADD(m, RK_AND(c1, RI_CMPGT(n, izero)), m, ione);
    m = RI_MADD(m, RK_AND(c2, RI_CMPGT(n, ione)), m, ione);
    m = RI_MADD(m, RK_AND(c3, RI_CMPEQ(n, i3)), m, ione);
    return m;
}
/* an axis' state after m of its steps (lanes j): the cell, the next boundary's t, the boundary after it */
RC_LT void brick_take(RM j, RI m, RI s, RI *i, RV *t, RV *u, RV t2, RV t3, RV t4, RV u3, RV u4, RV u5)
{
    const RI ione = RI_SET1(1), itwo = RI_SET1(2), i3 = RI_SET1(3);
    const RM m1 = RK_AND(j, RI_CMPEQ(m, ione)), m2 = RK_AND(j, RI_CMPEQ(m, itwo)), m3 = RK_AND(j, RI_CMPEQ(m, i3));
    *i = RI_MADD(*i, j, *i, RI_MULLO(m, s));
    *t = RV_MMOV(RV_MMOV(RV_MMOV(*t, m1, t2), m2, t3), m3, t4);
    *u = RV_MMOV(RV_MMOV(RV_MMOV(*u, m1, u3), m2, u4), m3, u5);
}
RC_LT void brick_jump(RM jm, RI *ix, RI *iy, RI *iz, RV *tx, RV *ty, RV *tz, RV *ux, RV *uy, RV *uz, RI isx, RI isy,
                      RI isz, RV dsx, RV dsy, RV dsz, RV idx, RV idy, RV idz, RV vox, RV voy, RV voz, RV tlim)
{
    const RI izero = RI_ZERO(), i3 = RI_SET1(3), inot = RI_SET1(-1);
    /* steps to the far side: i & 3 going down, ~i & 3 going up, 0 for no step */
    const RI nx = RI_AND(RI_MMOV(RI_ZMOV(RI_CMPLT(isx, izero), *ix), RI_CMPGT(isx, izero), RI_XOR(*ix, inot)), i3);
    const RI ny = RI_AND(RI_MMOV(RI_ZMOV(RI_CMPLT(isy, izero), *iy), RI_CMPGT(isy, izero), RI_XOR(*iy, inot)), i3);
    const RI nz = RI_AND(RI_MMOV(RI_ZMOV(RI_CMPLT(isz, izero), *iz), RI_CMPGT(isz, izero), RI_XOR(*iz, inot)), i3);
    RV ex, x2, x3, x4, xu3, xu4, xu5, ey, y2, y3, y4, yu3, yu4, yu5, ez, z2, z3, z4, zu3, zu4, zu5;
    brick_axis(nx, *tx, *ux, dsx, vox, idx, &ex, &x2, &x3, &x4, &xu3, &xu4, &xu5);
    brick_axis(ny, *ty, *uy, dsy, voy, idy, &ey, &y2, &y3, &y4, &yu3, &yu4, &yu5);
    brick_axis(nz, *tz, *uz, dsz, voz, idz, &ez, &z2, &z3, &z4, &zu3, &zu4, &zu5);
    /* the step out: the step's order on the far sides' t */
    const RM ax = RK_AND(RV_CMP(ex, ey, _CMP_LE_OQ), RV_CMP(ex, ez, _CMP_LE_OQ));
    const RM ay = RK_ANDN(ax, RV_CMP(ey, ez, _CMP_LE_OQ)), az = RK_NOT(RK_OR(ax, ay));
    const RV te = RV_MIN(RV_MIN(ex, ey), ez);
    const RM j = RV_MCMP(jm, te, tlim, _CMP_LT_OQ);
    if (j == 0) return;
    /* x is before y and z: t <= te when the exit is y's or z's; y before z: <= when z's, < when x's; z after both */
    const RI mx = RI_MMOV(brick_count(RM_ALL, nx, *tx, x2, x3, te), ax, nx);
    const RI my = RI_MMOV(brick_count(az, ny, *ty, y2, y3, te), ay, ny);
    const RI mz = RI_MMOV(brick_count(0, nz, *tz, z2, z3, te), az, nz);
    brick_take(j, mx, isx, ix, tx, ux, x2, x3, x4, xu3, xu4, xu5);
    brick_take(j, my, isy, iy, ty, uy, y2, y3, y4, yu3, yu4, yu5);
    brick_take(j, mz, isz, iz, tz, uz, z2, z3, z4, zu3, zu4, zu5);
}
#endif

/* The air prefix RC_W rays at a time in AVX-512's lanes, where the CPU has it (FP64 eight; FP32 sixteen since
 * lane/hostf32: the air walk's cycles 18% fewer, 43.1k to 35.4k an env-step on the 16x16 trainer's shape, the id read
 * by a load a lane still 52% of them, the step's arithmetic 6.5%; at sixteen the dword gathers stay slower, +9%, and
 * the addresses through memory, +23%). A lane holds a ray's cell, its next
 * boundaries' t, the boundary after the next step on each axis (u = i + s + (s > 0), a whole number in a double:
 * u - o is the scalar walk's (double)(i + (s > 0)) - o after the step, the same bits) and its section's ids. A step
 * is the scalar one in every lane: the axis by the same compares (ties x, then y, then z), the stepped axis' next t
 * by the same subtraction and product, the cell's index in its section from the cell. A lane crossing a section's
 * edge finds the next section by the window's table and the section's ids_hi (gathers). Each lane reads its new
 * cell's id by a load of its own (eight loads: 6k more instructions an env-step than the gather of the dword the id
 * ends, but Zen 5's eight-wide gather held the lanes to half the walk's IPC: the cast's cycles 3.5% fewer, paired).
 * Every active lane takes four steps before any takes a new ray: a lane
 * that stops is dead to the block's end, its step kept (masked stores), so no step waits on a load to go on; then
 * the dead lanes' rays go back to the batch and the batch's next rays come in (expand loads). */
/* The dead lanes' rays back as the fast loop would go on from the step each stopped on (rays_air_body's and rays_on's):
 * the cell and boundaries after that step (six.., svx..; a run that ended keeps them too, as the fast loop's run_end
 * leaves them: the kept frame's record takes a pixel's reach from the walk's last cell, lane/castmove), and by how
 * (sco: how << 4 | the axis): RC_END; RC_HIT with the axis and the step's t, the boundary it crossed from the cell
 * before it (walk's own expression: ((double)(i - s + (s > 0)) - o) * id on the axis, s the axis' step); RC_WALK
 * from the cell before the step, its boundary on the axis that t. Made in the lanes first, every lane's t, cell and
 * boundaries by the scalar's operations (lane/castwet a6: the scalar's branches on how and the axis, a ray each,
 * mispredicted), then stored a lane at a time with no branch: axis and t go to a ray handed back as RC_END or RC_WALK
 * too, which nothing reads (walk takes them for RC_HIT alone). */
__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), always_inline)) static inline void
hand_back(const struct rc *k, struct rcrays *R, RM dead, const int32_t *ray, RI scov, RI cx, RI cy,
          RI cz, RV bx, RV by, RV bz, RI isx, RI isy, RI isz, RV idx, RV idy,
          RV idz, RP base)
{
    /* (the stopped lanes' state comes in registers, a9: from the lanes' masked stores, which a load cannot take from
     * the store queue, the first vector load here waited for them to retire, 0.8% of the brief's cycles) */
    const RI izero = RI_ZERO(), ione = RI_SET1(1), i15 = RI_SET1(15);
    const RI axv = RI_AND(scov, i15), howv = RI_SRLI(scov, 4);
    const RM a0 = RI_CMPEQ(axv, izero), a1 = RI_CMPEQ(axv, ione);
    const RM a2 = RK_NOT(RK_OR(a0, a1));
    int32_t six[RC_W] __attribute__((aligned(4 * RC_W))), siy[RC_W] __attribute__((aligned(4 * RC_W))),
            siz[RC_W] __attribute__((aligned(4 * RC_W)));
    rcr svx[RC_W] __attribute__((aligned(64))), svy[RC_W] __attribute__((aligned(64))), svz[RC_W] __attribute__((aligned(64)));
    /* the axis' step s, cell, origin and origin cell, and 1 / its direction */
    const RI s = RI_MMOV(RI_MMOV(isz, a0, isx), a1, isy);
    const RI c = RI_MMOV(RI_MMOV(cz, a0, cx), a1, cy);
    const RI ob = RI_MMOV(RI_MMOV(RI_SET1(k->bz), a0, RI_SET1(k->bx)), a1, RI_SET1(k->by));
    const RV o = RV_MMOV(RV_MMOV(RV_SET1(k->oz), a0, RV_SET1(k->ox)), a1, RV_SET1(k->oy));
    const RV id = RV_MMOV(RV_MMOV(idz, a0, idx), a1, idy);
    /* i - s + (s > 0) less the origin cell, as an rcr: the boundary's coordinate */
    const RI b = RI_MADD(RI_SUB(c, s), RI_CMPGT(s, izero), RI_SUB(c, s), ione);
    const RV t = RV_MUL(RV_SUB(RV_CVTI(RI_SUB(b, ob)), o), id);
    /* RC_WALK: the cell before the step on its axis, that boundary's t */
    const RM wk = RI_CMPEQ(howv, RI_SET1(RC_WALK));
#if RC_HBSCATTER && RC_FP == 32 && RC_W == 16
    /* (RC_HBSCATTER, lane/hostf32's grid: each field to the dead lanes' rays by a scatter, not a store a lane) */
    {
        const RI rv = RI_LOAD(ray);
        _mm512_mask_i32scatter_epi32(R->ix, dead, rv, RI_MSUB(cx, RK_AND(wk, a0), cx, s), 4);
        _mm512_mask_i32scatter_epi32(R->iy, dead, rv, RI_MSUB(cy, RK_AND(wk, a1), cy, s), 4);
        _mm512_mask_i32scatter_epi32(R->iz, dead, rv, RI_MSUB(cz, RK_AND(wk, a2), cz, s), 4);
        _mm512_mask_i32scatter_ps(R->tx, dead, rv, RV_MMOV(bx, RK_AND(wk, a0), t), 4);
        _mm512_mask_i32scatter_ps(R->ty, dead, rv, RV_MMOV(by, RK_AND(wk, a1), t), 4);
        _mm512_mask_i32scatter_ps(R->tz, dead, rv, RV_MMOV(bz, RK_AND(wk, a2), t), 4);
        _mm512_mask_i32scatter_epi32(R->how, dead, rv, howv, 4);
        _mm512_mask_i32scatter_epi32(R->axis, dead, rv, axv, 4);
        _mm512_mask_i32scatter_ps(R->t, dead, rv, t, 4);
        (void)six, (void)siy, (void)siz, (void)svx, (void)svy, (void)svz;
    }
#else
    RI_STORE(six, RI_MSUB(cx, RK_AND(wk, a0), cx, s));
    RI_STORE(siy, RI_MSUB(cy, RK_AND(wk, a1), cy, s));
    RI_STORE(siz, RI_MSUB(cz, RK_AND(wk, a2), cz, s));
    RV_STORE(svx, RV_MMOV(bx, RK_AND(wk, a0), t));
    RV_STORE(svy, RV_MMOV(by, RK_AND(wk, a1), t));
    RV_STORE(svz, RV_MMOV(bz, RK_AND(wk, a2), t));
    rcr tt[RC_W] __attribute__((aligned(64)));
    int32_t hw[RC_W] __attribute__((aligned(4 * RC_W))), ax[RC_W] __attribute__((aligned(4 * RC_W)));
    RV_STORE(tt, t), RI_STORE(hw, howv), RI_STORE(ax, axv);
    for (unsigned m = dead; m; m &= m - 1)
    {
        const int j = __builtin_ctz(m), q = ray[j];
        R->ix[q] = six[j], R->iy[q] = siy[j], R->iz[q] = siz[j], R->tx[q] = svx[j], R->ty[q] = svy[j], R->tz[q] = svz[j];
        R->how[q] = hw[j], R->axis[q] = ax[j], R->t[q] = tt[j];
    }
#endif
#if !defined(RC_NO_PREFETCH)
    /* a ray handed back onto a block: walk reads its metadata, then the light about it, first (their nibble arrays'
     * lines, which no lane read: 0.57% of the long runs' cycles waited there), the batch's other rays before it
     * (a7): their lines asked for now, the hit cell's metadata and sky light (the light of the cell before is most
     * often on the same line). base: each lane's section's ids */
    const RM hit = RK_AND(dead, RI_CMPEQ(howv, RI_SET1(RC_HIT)));
    if (hit)
    {
        uint64_t bs[RC_W] __attribute__((aligned(64)));
        int32_t ci[RC_W] __attribute__((aligned(4 * RC_W)));
        rp_store(bs, base);
        RI_STORE(ci, RI_OR(RI_OR(RI_SLLI(RI_AND(cx, i15), 8), RI_SLLI(RI_AND(cz, i15), 4)), RI_AND(cy, i15)));
        for (unsigned m = hit; m; m &= m - 1)
        {
            const int j = __builtin_ctz(m);
            const struct chunk_sec *s = (const struct chunk_sec *)(uintptr_t)(bs[j] - offsetof(struct chunk_sec, ids));
            __builtin_prefetch(chunk_sec_metas(s) + (ci[j] >> 1));
            __builtin_prefetch(chunk_sec_sky(s) + (ci[j] >> 1));
        }
    }
#endif
}

#if RC_FIX
/* The fixed instance's step order in the lanes: the lanes of act whose pair of t's is closer than 2^-20 relative take
 * that pair's order from rc_fx_exact (the scalar walk's rc_fx_cmp, lane by lane), the rest the floats' compares; the
 * close ones out of line (a few in a million steps) */
__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), noinline)) static void
fx_lanes_fix(const struct rc *k, const struct rcrays *R, RM nxy, RM nxz, RM nyz, const int32_t *ray, RI ix, RI iy, RI iz,
             RI isx, RI isy, RI isz, RM *mxy, RM *mxz, RM *myz)
{
    int32_t cx[RC_W] __attribute__((aligned(64))), cy[RC_W] __attribute__((aligned(64))), cz[RC_W] __attribute__((aligned(64)));
    int32_t sx[RC_W] __attribute__((aligned(64))), sy[RC_W] __attribute__((aligned(64))), sz[RC_W] __attribute__((aligned(64)));
    RI_STORE(cx, ix), RI_STORE(cy, iy), RI_STORE(cz, iz), RI_STORE(sx, isx), RI_STORE(sy, isy), RI_STORE(sz, isz);
    for (unsigned m = (unsigned)(nxy | nxz | nyz); m; m &= m - 1)
    {
        const int j = __builtin_ctz(m), q = ray[j];
        const int bx = cx[j] + (sx[j] > 0) - k->bx, by = cy[j] + (sy[j] > 0) - k->by, bz = cz[j] + (sz[j] > 0) - k->bz;
        const RM bit = (RM)(1u << j);
#if defined(RC_FX_COUNT)
#define FXL_SET(M, E) do { const RM b_ = (E) <= 0 ? bit : 0; __atomic_add_fetch(&rc_fx_n[0], 1, __ATOMIC_RELAXED); \
                           if ((*(M) & bit) != b_) __atomic_add_fetch(&rc_fx_n[1], 1, __ATOMIC_RELAXED); \
                           *(M) = (RM)((*(M) & ~bit) | b_); } while (0)
#else
#define FXL_SET(M, E) (*(M) = (RM)((*(M) & ~bit) | ((E) <= 0 ? bit : 0)))
#endif
        if (nxy & bit) FXL_SET(mxy, rc_fx_exact(bx, k->ox, R->dx[q], by, k->oy, R->dy[q]));
        if (nxz & bit) FXL_SET(mxz, rc_fx_exact(bx, k->ox, R->dx[q], bz, k->oz, R->dz[q]));
        if (nyz & bit) FXL_SET(myz, rc_fx_exact(by, k->oy, R->dy[q], bz, k->oz, R->dz[q]));
#undef FXL_SET
    }
}
/* the lanes of k whose a and b are within 2^-20 relative, both finite */
RC_LT RM fx_near(RM k, RV a, RV b)
{
#if RC_FX_ALWAYS
    /* (the measurement build: every lane with both finite) */
    return RK_AND(RK_AND(k, RV_CMP(RV_ABS(a), RV_SET1(INFINITY), _CMP_LT_OQ)), RV_CMP(RV_ABS(b), RV_SET1(INFINITY), _CMP_LT_OQ));
#elif RC_FXNEAR == 1
    /* rc_fx_cmp's ulp test lane by lane */
#if RC_W == 16
    const __m512i ia = _mm512_castps_si512(a), ib = _mm512_castps_si512(b);
#else
    const __m256i ia = _mm256_castps_si256(a), ib = _mm256_castps_si256(b);
#endif
    const RI m = RI_SET1(0x7fffffff);
    const RI oa = RI_XOR(ia, RI_AND(RI_SRAI(ia, 31), m)), ob = RI_XOR(ib, RI_AND(RI_SRAI(ib, 31), m));
    const RI dd = RI_ADD(RI_SUB(oa, ob), RI_SET1(RC_FXULP));
#if RC_W == 16
    return _mm512_mask_cmple_epu32_mask(k, dd, _mm512_set1_epi32(2 * RC_FXULP));
#else
    return _mm256_mask_cmple_epu32_mask(k, dd, _mm256_set1_epi32(2 * RC_FXULP));
#endif
#else
    const RV g = RV_MUL(RV_ADD(RV_ABS(a), RV_ABS(b)), RV_SET1(0x1p-20f));
    return RK_AND(RV_MCMP(k, RV_ABS(RV_SUB(a, b)), g, _CMP_LE_OQ), RV_CMP(g, RV_SET1(INFINITY), _CMP_LT_OQ));
#endif
}
#define RC_FX_LANES(R, act, ray, tx, ty, tz, ix, iy, iz, isx, isy, isz, mxy, mxz, myz)                                 \
    do {                                                                                                         \
        const RM nxy_ = fx_near(act, tx, ty), nxz_ = fx_near(act, tx, tz), nyz_ = fx_near(act, ty, tz);              \
        if (__builtin_expect((nxy_ | nxz_ | nyz_) != 0, 0))                                                       \
            fx_lanes_fix(k, R, nxy_, nxz_, nyz_, ray, ix, iy, iz, isx, isy, isz, &mxy, &mxz, &myz);                    \
    } while (0)
#else
#define RC_FX_LANES(R, act, ray, tx, ty, tz, ix, iy, iz, isx, isy, isz, mxy, mxz, myz) ((void)0)
#endif

/* The air loops' hard bound (the fault of 2026-11-12: a camera whose yaw is NaN has no forward, its rays' directions
 * are NaN, t never reaches the limit and a lane steps in place for ever). A ray crosses at most 3 cells for each unit
 * of t and t stops at the limit, which is under the range: at most 1.8 range + 6 steps from a unit direction. A lane
 * that takes more than 2 range + 64 (counted a block at a time) ends as a miss, RC_END at the cell it stands on, as a
 * ray past its limit does. No ray of a sane camera comes near it, so no result moves. */
static inline int rc_air_cap(const struct rc *k) { return 2 * k->range + 64; }
#define RC_AIR_BOUND()                                                                                                \
    do                                                                                                                \
    {                                                                                                                 \
        cnt = RI_MADD(cnt, act, cnt, RI_SET1(RC_BLOCK));                                                              \
        const RM ov_ = RK_ANDN(dead, RK_AND(act, RI_CMPGT(cnt, vcap)));                                               \
        if (ov_)                                                                                                      \
        {                                                                                                             \
            sco = RI_MMOV(sco, ov_, vend);                                                                            \
            six = RI_MMOV(six, ov_, ix), siy = RI_MMOV(siy, ov_, iy), siz = RI_MMOV(siz, ov_, iz);                    \
            svx = RV_MMOV(svx, ov_, tx), svy = RV_MMOV(svy, ov_, ty), svz = RV_MMOV(svz, ov_, tz);                    \
            dead = RK_OR(dead, ov_);                                                                                  \
        }                                                                                                             \
    } while (0)

/* (wet: the eye in water, k->liquid 8, a constant: two instances, so a cast with the eye out of water carries none of
 * the water's registers and compares; lane/castmove) */
__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), always_inline)) static inline void
rays_air_body(const struct rc *k, struct rcrays *R, int n, const struct chunk_sec *eye, const int wet)
{
    const RV vox = RV_SET1(k->ox), voy = RV_SET1(k->oy), voz = RV_SET1(k->oz);
    const RV zero = RV_ZERO();
    const RI izero = RI_ZERO(), ione = RI_SET1(1), imone = RI_SET1(-1);
    const RI i15 = RI_SET1(15), inot15 = RI_SET1(~15);
    const RI iota = RI_IOTA();
    const RI ecx = RI_SET1(k->eye_x), ecy = RI_SET1(k->eye_y), ecz = RI_SET1(k->eye_z);
    /* the eye's cell in the frame's coordinates (RC_CX's: 0 in FP32) */
    const RI fcx = RI_SET1(k->eye_x - k->bx), fcy = RI_SET1(k->eye_y - k->by), fcz = RI_SET1(k->eye_z - k->bz);
    const RI vcx0 = RI_SET1(k->cx0), vcz0 = RI_SET1(k->cz0);
    const RI vncx = RI_SET1(k->ncx), vncz = RI_SET1(k->ncz);
    const RI vtop = RI_SET1(k->top), v256 = RI_SET1(256);
    /* a lane's base: its section's ids, cell ci's at base + ci */
    const int64_t idsoff = (int64_t)offsetof(struct chunk_sec, ids);
    const RP eyebase = rp_set1(eye->ids);
#if RC_BRICK
    /* (dry) each lane's section's bricks (sec_bricks), the eye's to start */
    const RI i12 = RI_SET1(12);
    const RP eyebm = rp_set1q(wet ? 0 : k->bm[col_of(k, k->eye_x, k->eye_z) * 16 + (k->eye_y >> 4)]);
    RP bmk = eyebm;
#endif
    /* (id | 1) == seen9: water the eye is in (ids 8, 9); 256 matches no id */
    const RI seen9 = RI_SET1(wet ? 9 : 256);
    RV tx = zero, ty = zero, tz = zero, ux = zero, uy = zero, uz = zero, idx = zero, idy = zero, idz = zero;
    RV dsx = zero, dsy = zero, dsz = zero, tlim = zero;
    RP base = eyebase;
    RI ix = izero, iy = izero, iz = izero, isx = izero, isy = izero, isz = izero;
    /* id == air passes: 0, or -1 (none) for a ray going up from the eye's water; (id | 1) == w9 passes the eye's
     * water (seen9), 256 none: a lane's own */
    RI air = izero, w9 = seen9;
    /* the lanes going up from the eye's water (ph0), and those past it on through air alone (ph1, lane/castmove): of
     * those, the ones that left it by a step up (sfm), the surface walk draws from below: that step's t and cell */
    RM ph0 = 0, ph1 = 0, sfm = 0;
    rcr sft[RC_W] __attribute__((aligned(64)));
    int32_t sfx[RC_W] __attribute__((aligned(4 * RC_W))), sfy[RC_W] __attribute__((aligned(4 * RC_W))),
            sfz[RC_W] __attribute__((aligned(4 * RC_W)));
    int32_t ray[RC_W] __attribute__((aligned(4 * RC_W))) = {0};
    /* a stopped lane's cell and boundaries after its step, kept to the block's end; the step each lane stopped on,
     * its axis and why */
    RV svx = zero, svy = zero, svz = zero;
    RI six = izero, siy = izero, siz = izero;
    RI sco = izero;             /* how << 4 | the axis */
    const RI vhit = RI_SET1(RC_HIT << 4), vwalk = RI_SET1(RC_WALK << 4), vend = RI_SET1(RC_END << 4);
    const RI itwo = RI_SET1(2);
    RM act = 0, dead = RM_ALL;
    int next = 0;
    RI cnt = izero;             /* a lane's steps (rc_air_over) */
    const RI vcap = RI_SET1(rc_air_cap(k));
    goto refill;
    for (;;)
    {
#if !defined(RC_BLOCK)
#define RC_BLOCK 4
#endif
        for (int b = 0; b < RC_BLOCK; ++b)
        {
            /* the axis each lane steps on, the step's t (the least of the three) against the ray's limit */
            RM mxy = RV_CMP(tx, ty, _CMP_LE_OQ), mxz = RV_CMP(tx, tz, _CMP_LE_OQ);
            RM myz = RV_CMP(ty, tz, _CMP_LE_OQ);
            RC_FX_LANES(R, act, ray, tx, ty, tz, ix, iy, iz, isx, isy, isz, mxy, mxz, myz);
            const RM mx = RK_AND(mxy, mxz), my = RK_ANDN(mx, myz), mz = RK_NOT(RK_OR(mx, my));
            const RM st = RV_MCMP(act, RV_MIN(RV_MIN(tx, ty), tz), tlim, _CMP_GE_OQ);
            /* the step: the cell, the stepped axis' next t, the boundary after the next step there */
            const RI nix = RI_MADD(ix, mx, ix, isx), niy = RI_MADD(iy, my, iy, isy), niz = RI_MADD(iz, mz, iz, isz);
            const RV ntx = RV_MMUL(tx, mx, RV_SUB(ux, vox), idx);
            const RV nty = RV_MMUL(ty, my, RV_SUB(uy, voy), idy);
            const RV ntz = RV_MMUL(tz, mz, RV_SUB(uz, voz), idz);
            ux = RV_MADD(ux, mx, ux, dsx), uy = RV_MADD(uy, my, uy, dsy), uz = RV_MADD(uz, mz, uz, dsz);
            /* the cell's index in its section; a lane whose cell left its section (a bit above the low four changed) */
            const RI lx = RI_AND(nix, i15), ly = RI_AND(niy, i15), lz = RI_AND(niz, i15);
            const RI ci = RI_OR(RI_OR(RI_SLLI(lx, 8), RI_SLLI(lz, 4)), ly);
            const RI moved = RI_OR(RI_OR(RI_XOR(nix, ix), RI_XOR(niy, iy)), RI_XOR(niz, iz));
            const RM edge = RI_MTEST(RK_ANDN(st, act), moved, inot15);
            RM yout = 0, back = 0;
            if (edge)
            {
                /* out of y 0 .. the ray's ytop the run ends; else the window's section there: one with blocks and
                 * no ids past 255 is read on; one with none (walk jumps it) or ids past 255 (both NULL in the
                 * lanes' table), or out of the window, hands the ray back as it stood */
                const RI cxv = RI_SUB(RI_SRAI(nix, 4), vcx0);
                const RI czv = RI_SUB(RI_SRAI(niz, 4), vcz0);
                const RI ytop = RI_MMOV(vtop, RI_CMPLT(isy, izero), v256);
                const RM yin = RI_MCMPLTU(edge, niy, ytop);
                yout = RK_ANDN(yin, edge);
                const RM inw = RK_AND(RI_MCMPLTU(yin, cxv, vncx), RI_CMPLTU(czv, vncz));
                const RI si = RI_ADD(RI_SLLI(RI_ADD(RI_MULLO(cxv, vncz), czv), 4), RI_SRAI(niy, 4));
                const RP p = rp_gather(inw, si, (const void *)k->lsec);
                const RM has = rp_test(inw, p);
                back = RK_ANDN(has, yin);
                base = rp_maddoff(base, has, p, idsoff);
#if RC_BRICK
                if (!wet) bmk = rp_mmov(bmk, has, rp_gather(has, si, (const void *)k->bm));
#endif
            }
            /* the new cell's id (every lane reads one: a lane not reading reads a cell of its section, unused): air
             * passes, and with the eye in water water too; a ray going up from that water passes the water only */
            const RM end = RK_OR(st, yout), rd = RK_ANDN(RK_OR(end, back), act);
#if RC_BRICK
            /* (dry) a lane in a brick of air reads none: its id is 0 */
            const RM occ = wet ? rd : rp_bocc(rd, bmk, RI_OR(RI_OR(RI_SLLI(RI_AND(lx, i12), 2), RI_AND(lz, i12)), RI_SRLI(ly, 2)));
            const RI w = wet ? RI_ZMOV(rd, rp_ids(base, ci)) : rp_ids_m(base, ci, occ);
#else
            const RI w = RI_ZMOV(rd, rp_ids(base, ci));
#endif
#if defined(RC_BRICK_COUNT)
            if (rbc && !wet)
            {
                uint64_t bs[RC_W] __attribute__((aligned(64)));
                int32_t cc[RC_W] __attribute__((aligned(4 * RC_W)));
                rp_store(bs, base), RI_STORE(cc, ci);
                __atomic_add_fetch(&rbc[0], (uint64_t)__builtin_popcount(rd), __ATOMIC_RELAXED);
                __atomic_add_fetch(&rbc[2], (uint64_t)__builtin_popcount(act), __ATOMIC_RELAXED);
                uint64_t na = 0;
                for (unsigned m = rd; m; m &= m - 1)
                {
                    const int j = __builtin_ctz(m);
                    na += (uint64_t)brick_air((const uint8_t *)(uintptr_t)bs[j], cc[j]);
                }
                __atomic_add_fetch(&rbc[1], na, __ATOMIC_RELAXED);
            }
#endif
            /* (dry: air is 0 and seen9 matches no id) */
            RM pass = wet ? RK_OR(RI_CMPEQ(w, air), RI_CMPEQ(RI_OR(w, ione), w9)) : RI_CMPEQ(w, izero);
            if (wet && ph0)
            {
                /* a ray going up from the eye's water reading air is out of it: the fast loop's take on a step up,
                 * where walk's block: draws the surface from below (its t, this step's, and cell kept), then air
                 * on (eye_was_liquid 0); on a step across, air on. From there it passes air alone, as the fast loop
                 * does with eye_was_liquid 0 up to the next water, which it hands back */
                /* (a lane that stopped steps on to the block's end, dead: not it) */
                const RM tr = RK_ANDN(dead, RK_AND(RK_AND(ph0, rd), RI_CMPEQ(w, izero)));
                if (tr)
                {
                    const RM up = RK_AND(tr, my);
                    RV_MSTORE(sft, up, ty);
                    RI_MSTORE(sfx, up, nix), RI_MSTORE(sfy, up, niy), RI_MSTORE(sfz, up, niz);
                    sfm = RK_OR(sfm, up);
                    pass = RK_OR(pass, tr);
                    ph0 = RK_ANDN(tr, ph0), ph1 = RK_OR(ph1, tr);
                    air = RI_MMOV(air, tr, izero), w9 = RI_MMOV(w9, tr, v256);
                }
            }
            const RM stop = RK_OR(RK_OR(end, back), RK_ANDN(pass, rd));
            /* a lane stopping now keeps its step to the block's end */
            const RM now = RK_ANDN(dead, stop);
            svx = RV_MMOV(svx, now, ntx), svy = RV_MMOV(svy, now, nty), svz = RV_MMOV(svz, now, ntz);
            six = RI_MMOV(six, now, nix), siy = RI_MMOV(siy, now, niy), siz = RI_MMOV(siz, now, niz);
            const RI how = RI_MMOV(RI_MMOV(vhit, back, vwalk), end, vend);
            sco = RI_MMOV(sco, now, RI_OR(how, RI_MMOV(RI_ZMOV(my, ione), mz, itwo)));
            dead = RK_OR(dead, stop);
            tx = ntx, ty = nty, tz = ntz, ix = nix, iy = niy, iz = niz;
#if RC_BRICK && RC_BRICKJ && !RC_FIX
            /* (dry) the lanes that read air in a brick of air: to its last cell (brick_jump) */
            if (!wet)
            {
                const RM jm = RK_ANDN(RK_OR(occ, dead), rd);
                if (jm) brick_jump(jm, &ix, &iy, &iz, &tx, &ty, &tz, &ux, &uy, &uz, isx, isy, isz, dsx, dsy, dsz, idx, idy, idz,
                                   vox, voy, voz, tlim);
            }
#endif
        }
        RC_AIR_BOUND();
        if (dead == 0) continue;
        /* the dead lanes' rays back as the fast loop would go on from the step they stopped on (hand_back) */
        hand_back(k, R, dead, ray, sco, six, siy, siz, svx, svy, svz, isx, isy, isz, idx, idy, idz, base);
        if (wet && (dead & ph1))
            for (unsigned m = dead & ph1; m; m &= m - 1)
            {
                /* (sfa is rays_fill's 0 else) */
                const int j = __builtin_ctz(m), q = ray[j];
                R->sfa[q] = 1;
                if ((unsigned)sfm >> j & 1) R->sfa[q] = 2, R->sft[q] = sft[j], R->sfx[q] = sfx[j], R->sfy[q] = sfy[j], R->sfz[q] = sfz[j];
            }
    refill:;
        /* the next rays into the dead lanes, as many as are left */
        RM fill = dead;
        const int left = n - next;
        if (__builtin_popcount(fill) > left) fill = (RM)_pdep_u32((1u << left) - 1u, fill);
        act = (RM)((act & ~dead) | fill);
        dead = 0;
        if (fill)
        {
            tx = RV_MEXPAND(tx, fill, &R->tx[next]);
            ty = RV_MEXPAND(ty, fill, &R->ty[next]);
            tz = RV_MEXPAND(tz, fill, &R->tz[next]);
            idx = RV_MEXPAND(idx, fill, &R->idx[next]);
            idy = RV_MEXPAND(idy, fill, &R->idy[next]);
            idz = RV_MEXPAND(idz, fill, &R->idz[next]);
            tlim = RV_MEXPAND(tlim, fill, &R->tlim[next]);
            const RV dx = RV_ZEXPAND(fill, &R->dx[next]);
            const RV dy = RV_ZEXPAND(fill, &R->dy[next]);
            const RV dz = RV_ZEXPAND(fill, &R->dz[next]);
            RI_STORE(ray, RI_MEXPAND(RI_LOAD(ray), fill, RI_ADD(iota, RI_SET1(next))));
            next += __builtin_popcount(fill);
            cnt = RI_MMOV(cnt, fill, izero);
            /* s = (d > 0) - (d < 0) on each axis, the cell the eye's, the boundary after the first step */
            const RM px = RV_CMP(dx, zero, _CMP_GT_OQ), nx = RV_CMP(dx, zero, _CMP_LT_OQ);
            const RM py = RV_CMP(dy, zero, _CMP_GT_OQ), ny = RV_CMP(dy, zero, _CMP_LT_OQ);
            const RM pz = RV_CMP(dz, zero, _CMP_GT_OQ), nz = RV_CMP(dz, zero, _CMP_LT_OQ);
            isx = RI_MMOV(RI_MMOV(RI_MMOV(isx, fill, izero), px, ione), nx, imone);
            isy = RI_MMOV(RI_MMOV(RI_MMOV(isy, fill, izero), py, ione), ny, imone);
            isz = RI_MMOV(RI_MMOV(RI_MMOV(isz, fill, izero), pz, ione), nz, imone);
            dsx = RV_MCVTI(dsx, fill, isx);
            dsy = RV_MCVTI(dsy, fill, isy);
            dsz = RV_MCVTI(dsz, fill, isz);
            ix = RI_MMOV(ix, fill, ecx), iy = RI_MMOV(iy, fill, ecy);
            iz = RI_MMOV(iz, fill, ecz);
            ux = RV_MCVTI(ux, fill, RI_MADD(RI_ADD(fcx, isx), px, RI_ADD(fcx, isx), ione));
            uy = RV_MCVTI(uy, fill, RI_MADD(RI_ADD(fcy, isy), py, RI_ADD(fcy, isy), ione));
            uz = RV_MCVTI(uz, fill, RI_MADD(RI_ADD(fcz, isz), pz, RI_ADD(fcz, isz), ione));
            base = rp_mmov(base, fill, eyebase);
#if RC_BRICK
            if (!wet) bmk = rp_mmov(bmk, fill, eyebm);
#endif
            if (wet)
            {
                air = RI_MMOV(air, fill, RI_ZMOV(py, imone));
                w9 = RI_MMOV(w9, fill, seen9);
                ph0 = RK_OR(RK_ANDN(fill, ph0), RK_AND(fill, py));
                ph1 = RK_ANDN(fill, ph1), sfm = RK_ANDN(fill, sfm);
            }
        }
        if (act == 0) break;
    }
}

__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), noinline)) static void
rays_air_dry(const struct rc *k, struct rcrays *R, int n, const struct chunk_sec *eye)
{
    rays_air_body(k, R, n, eye, 0);
}

__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), noinline)) static void
rays_air_wet(const struct rc *k, struct rcrays *R, int n, const struct chunk_sec *eye)
{
    rays_air_body(k, R, n, eye, 1);
}

/* what a lane not yet given a ray reads (rays_on) */
static const uint8_t rc_zero_ids[4096];

/* The runs walk hands on (lane/castwet). With the eye in no liquid, a ray that walk took into water from air (the
 * surface drawn, in_water 1) goes on cell by cell through the water in the fast loop: 588 steps a moved cast on the 8
 * seed starts, in about 14 rays, which took 60% of the walk's cycles there (a3); and a ray going up past a block it
 * passed (a snow layer, grass) goes on through air, 11.6 steps a run on the brief's shape's long runs (a4). walk hands
 * those rays here (sfa 3 and 4, wq) and they go on in AVX-512's lanes as rays_air_body takes air: a lane's step is
 * the fast loop's (the same compares, t's, section edges and reads), each lane from its ray's own cell and boundaries
 * as walk left them and its section's ids from the lanes' table, passing water (sfa 3: ids 8 and 9, the loop's "on
 * through the water the ray is already in") or air (sfa 4: id 0, with cull -1 and eye_was_liquid 0 already), neither
 * of which changes the loop's state, and stopping at any other cell, onto which it hands the ray back as a block
 * (RC_HIT: walk's block: takes it as the loop would: air in_water 0 and on, the eye's water seen through), before a
 * section with no blocks, ids past 255 or out of the window (RC_WALK from the cell before: walk's loop goes on there)
 * or past the ray's limit or y range (RC_END), as rays_air_body hands back. Rays come to the lanes by gathers. */
__attribute__((target("avx512f,avx512vl,avx512dq,avx512bw,bmi2"), noinline)) static void
rays_on(const struct rc *k, struct rcrays *R)
{
    const int n = R->nw;
    const RV vox = RV_SET1(k->ox), voy = RV_SET1(k->oy), voz = RV_SET1(k->oz);
    const RV zero = RV_ZERO();
    const RI izero = RI_ZERO(), ione = RI_SET1(1), imone = RI_SET1(-1);
    const RI i15 = RI_SET1(15), inot15 = RI_SET1(~15), i3 = RI_SET1(3);
    const RI vcx0 = RI_SET1(k->cx0), vcz0 = RI_SET1(k->cz0);
    const RI vncx = RI_SET1(k->ncx), vncz = RI_SET1(k->ncz);
    const RI vtop = RI_SET1(k->top), v256 = RI_SET1(256);
    /* the frame's origin cell (RC_CX's) */
    const RI vbx = RI_SET1(k->bx), vby = RI_SET1(k->by), vbz = RI_SET1(k->bz);
    const int64_t idsoff = (int64_t)offsetof(struct chunk_sec, ids);
    RV tx = zero, ty = zero, tz = zero, ux = zero, uy = zero, uz = zero, idx = zero, idy = zero, idz = zero;
    RV dsx = zero, dsy = zero, dsz = zero, tlim = zero;
    RP base = rp_set1(rc_zero_ids);
    RI ix = izero, iy = izero, iz = izero, isx = izero, isy = izero, isz = izero;
    /* a lane's pass: id == pa (air: 0; -1 none) or (id | 1) == pw (water: 9; 256 none) */
    RI pa = imone, pw = RI_SET1(256);
    int32_t ray[RC_W] __attribute__((aligned(4 * RC_W))) = {0};
    RV svx = zero, svy = zero, svz = zero;
    RI six = izero, siy = izero, siz = izero;
    RI sco = izero;             /* how << 4 | the axis */
    const RI vhit = RI_SET1(RC_HIT << 4), vwalk = RI_SET1(RC_WALK << 4), vend = RI_SET1(RC_END << 4);
    const RI itwo = RI_SET1(2);
    RM act = 0, dead = RM_ALL;
    int next = 0;
    RI cnt = izero;             /* a lane's steps (rc_air_over) */
    const RI vcap = RI_SET1(rc_air_cap(k));
    goto refill;
    for (;;)
    {
        for (int b = 0; b < RC_BLOCK; ++b)
        {
            /* rays_air_body's step */
            RM mxy = RV_CMP(tx, ty, _CMP_LE_OQ), mxz = RV_CMP(tx, tz, _CMP_LE_OQ);
            RM myz = RV_CMP(ty, tz, _CMP_LE_OQ);
            RC_FX_LANES(R, act, ray, tx, ty, tz, ix, iy, iz, isx, isy, isz, mxy, mxz, myz);
            const RM mx = RK_AND(mxy, mxz), my = RK_ANDN(mx, myz), mz = RK_NOT(RK_OR(mx, my));
            const RM st = RV_MCMP(act, RV_MIN(RV_MIN(tx, ty), tz), tlim, _CMP_GE_OQ);
            const RI nix = RI_MADD(ix, mx, ix, isx), niy = RI_MADD(iy, my, iy, isy), niz = RI_MADD(iz, mz, iz, isz);
            const RV ntx = RV_MMUL(tx, mx, RV_SUB(ux, vox), idx);
            const RV nty = RV_MMUL(ty, my, RV_SUB(uy, voy), idy);
            const RV ntz = RV_MMUL(tz, mz, RV_SUB(uz, voz), idz);
            ux = RV_MADD(ux, mx, ux, dsx), uy = RV_MADD(uy, my, uy, dsy), uz = RV_MADD(uz, mz, uz, dsz);
            const RI lx = RI_AND(nix, i15), ly = RI_AND(niy, i15), lz = RI_AND(niz, i15);
            const RI ci = RI_OR(RI_OR(RI_SLLI(lx, 8), RI_SLLI(lz, 4)), ly);
            const RI moved = RI_OR(RI_OR(RI_XOR(nix, ix), RI_XOR(niy, iy)), RI_XOR(niz, iz));
            const RM edge = RI_MTEST(RK_ANDN(st, act), moved, inot15);
            RM yout = 0, back = 0;
            if (edge)
            {
                const RI cxv = RI_SUB(RI_SRAI(nix, 4), vcx0);
                const RI czv = RI_SUB(RI_SRAI(niz, 4), vcz0);
                const RI ytop = RI_MMOV(vtop, RI_CMPLT(isy, izero), v256);
                const RM yin = RI_MCMPLTU(edge, niy, ytop);
                yout = RK_ANDN(yin, edge);
                const RM inw = RK_AND(RI_MCMPLTU(yin, cxv, vncx), RI_CMPLTU(czv, vncz));
                const RI si = RI_ADD(RI_SLLI(RI_ADD(RI_MULLO(cxv, vncz), czv), 4), RI_SRAI(niy, 4));
                const RP p = rp_gather(inw, si, (const void *)k->lsec);
                const RM has = rp_test(inw, p);
                back = RK_ANDN(has, yin);
                base = rp_maddoff(base, has, p, idsoff);
            }
            const RM end = RK_OR(st, yout), rd = RK_ANDN(RK_OR(end, back), act);
            const RI w = RI_ZMOV(rd, rp_ids(base, ci));
            /* the lane's water or air passes */
            const RM pass = RK_OR(RI_CMPEQ(w, pa), RI_CMPEQ(RI_OR(w, ione), pw));
            const RM stop = RK_OR(RK_OR(end, back), RK_ANDN(pass, rd));
            const RM now = RK_ANDN(dead, stop);
            svx = RV_MMOV(svx, now, ntx), svy = RV_MMOV(svy, now, nty), svz = RV_MMOV(svz, now, ntz);
            six = RI_MMOV(six, now, nix), siy = RI_MMOV(siy, now, niy), siz = RI_MMOV(siz, now, niz);
            const RI how = RI_MMOV(RI_MMOV(vhit, back, vwalk), end, vend);
            sco = RI_MMOV(sco, now, RI_OR(how, RI_MMOV(RI_ZMOV(my, ione), mz, itwo)));
            dead = RK_OR(dead, stop);
            tx = ntx, ty = nty, tz = ntz, ix = nix, iy = niy, iz = niz;
        }
        RC_AIR_BOUND();
        if (dead == 0) continue;
        /* the dead lanes' rays back as the fast loop would go on from the step they stopped on */
        hand_back(k, R, dead, ray, sco, six, siy, siz, svx, svy, svz, isx, isy, isz, idx, idy, idz, base);
    refill:;
        /* the next rays of wq into the dead lanes, as many as are left, each from its own cell */
        RM fill = dead;
        const int left = n - next;
        if (__builtin_popcount(fill) > left) fill = (RM)_pdep_u32((1u << left) - 1u, fill);
        act = (RM)((act & ~dead) | fill);
        dead = 0;
        if (fill)
        {
            const RI qv = RI_MEXPANDLOAD(izero, fill, &R->wq[next]);
            RI_STORE(ray, RI_MMOV(RI_LOAD(ray), fill, qv));
            next += __builtin_popcount(fill);
            cnt = RI_MMOV(cnt, fill, izero);
            tx = RV_MGATHER(tx, fill, qv, R->tx);
            ty = RV_MGATHER(ty, fill, qv, R->ty);
            tz = RV_MGATHER(tz, fill, qv, R->tz);
            idx = RV_MGATHER(idx, fill, qv, R->idx);
            idy = RV_MGATHER(idy, fill, qv, R->idy);
            idz = RV_MGATHER(idz, fill, qv, R->idz);
            tlim = RV_MGATHER(tlim, fill, qv, R->tlim);
            const RV dx = RV_MGATHER(zero, fill, qv, R->dx);
            const RV dy = RV_MGATHER(zero, fill, qv, R->dy);
            const RV dz = RV_MGATHER(zero, fill, qv, R->dz);
            ix = RI_MGATHER(ix, fill, qv, R->ix);
            iy = RI_MGATHER(iy, fill, qv, R->iy);
            iz = RI_MGATHER(iz, fill, qv, R->iz);
            /* in water (sfa 3) or air (4) */
            const RM wat = RI_MCMPEQ(fill, RI_MGATHER(i3, fill, qv, R->sfa), i3);
            pa = RI_MMOV(RI_MMOV(pa, fill, izero), wat, imone);
            pw = RI_MMOV(RI_MMOV(pw, fill, RI_SET1(256)), wat, RI_SET1(9));
            /* s on each axis; the boundary after the next step there (u = i + s + (s > 0)); the cell's section */
            const RM px = RV_CMP(dx, zero, _CMP_GT_OQ), nx = RV_CMP(dx, zero, _CMP_LT_OQ);
            const RM py = RV_CMP(dy, zero, _CMP_GT_OQ), ny = RV_CMP(dy, zero, _CMP_LT_OQ);
            const RM pz = RV_CMP(dz, zero, _CMP_GT_OQ), nz = RV_CMP(dz, zero, _CMP_LT_OQ);
            isx = RI_MMOV(RI_MMOV(RI_MMOV(isx, fill, izero), px, ione), nx, imone);
            isy = RI_MMOV(RI_MMOV(RI_MMOV(isy, fill, izero), py, ione), ny, imone);
            isz = RI_MMOV(RI_MMOV(RI_MMOV(isz, fill, izero), pz, ione), nz, imone);
            dsx = RV_MCVTI(dsx, fill, isx);
            dsy = RV_MCVTI(dsy, fill, isy);
            dsz = RV_MCVTI(dsz, fill, isz);
            /* (in the frame's coordinates: the cell less the origin cell) */
            const RI fx = RI_SUB(ix, vbx), fy = RI_SUB(iy, vby), fz = RI_SUB(iz, vbz);
            ux = RV_MCVTI(ux, fill, RI_MADD(RI_ADD(fx, isx), px, RI_ADD(fx, isx), ione));
            uy = RV_MCVTI(uy, fill, RI_MADD(RI_ADD(fy, isy), py, RI_ADD(fy, isy), ione));
            uz = RV_MCVTI(uz, fill, RI_MADD(RI_ADD(fz, isz), pz, RI_ADD(fz, isz), ione));
            const RI si = RI_ADD(RI_SLLI(RI_ADD(RI_MULLO(RI_SUB(RI_SRAI(ix, 4), vcx0), vncz), RI_SUB(RI_SRAI(iz, 4), vcz0)), 4),
                                 RI_SRAI(iy, 4));
            base = rp_maddoff(base, fill, rp_gather(fill, si, (const void *)k->lsec), idsoff);
        }
        if (act == 0) break;
    }
}
#endif

/* walk's starts moved past the batch's air prefix (above), where the CPU has the lanes for it; else they stay */
static int rc_lanes(void)
{
#if defined(RC_LANES)
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("avx512dq") &&
           __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("bmi2");
#else
    return 0;
#endif
}

static void rays_air(const struct rc *k, struct rcrays *R, int n, const struct chunk_sec *eye)
{
#if defined(RC_LANES)
    if (k->liquid == 8) rays_air_wet(k, R, n, eye);
    else rays_air_dry(k, R, n, eye);
#else
    (void)k, (void)R, (void)n, (void)eye;
#endif
}

#if defined(RC_AIR_VERIFY)
/* the batch's pixels (out, ch bytes each) against walk from the eye with no air prefix, counted in proc_malloc
 * memory (lane/hostcast: libneth's statics read zero in some runs) and printed as they differ */
static void rays_verify(const struct rc *k, struct rcrays *R, int p0, int n, int w, int h, rcr sxs, const uint8_t *out,
                        int ch)
{
    static _Thread_local uint64_t *cnt;
    if (cnt == NULL) cnt = proc_malloc(4 * sizeof *cnt), cnt[0] = cnt[1] = cnt[2] = cnt[3] = 0;
    cnt[2] += 1;
    cnt[3] += (uint64_t)R->nw;  /* the batch's rays through rays_on */
    rays_fill(k, R, p0, n, w, h, sxs);
    for (int q = 0; q < n; ++q)
    {
        uint8_t o[3];
        walk(k, R, q, 1, o);
        cnt[0] += 1;
        if (memcmp(o, out + (size_t)q * (size_t)ch, (size_t)ch))
        {
            cnt[1] += 1;
            fprintf(stderr, "rcverify MISMATCH pixel %d: walk %u %u %u prefix %u %u %u\n", p0 + q, o[0], o[1], o[2],
                    out[(size_t)q * ch], ch > 1 ? out[(size_t)q * ch + 1] : 0, ch > 2 ? out[(size_t)q * ch + 2] : 0);
        }
        if ((cnt[0] & ((1u << 16) - 1)) == 0)
            fprintf(stderr, "rcverify pixels %llu batches %llu mismatched %llu on %llu\n", (unsigned long long)cnt[0],
                    (unsigned long long)cnt[2], (unsigned long long)cnt[1], (unsigned long long)cnt[3]);
    }
}
#endif

#if defined(RC_RUN_VERIFY)
/* the verify build: a batch walked without a record walked again with a jump a section (no empty_run), its air
 * prefix as before, and each pixel compared; counted in proc_malloc memory and printed every 2^20 rays and at exit:
 * rays, rays that differ, empty runs taken, of them into a section with blocks (rrv: above empty_run) */
static pthread_once_t rrv_once = PTHREAD_ONCE_INIT;
static void rrv_print(void)
{
    if (rrv)
        fprintf(stderr, "rcmove rays %llu differ %llu empty runs %llu into blocks %llu\n", (unsigned long long)rrv[0],
                (unsigned long long)rrv[1], (unsigned long long)rrv[2], (unsigned long long)rrv[3]);
}
static void rrv_init(void)
{
    if ((rrv = proc_calloc(4, sizeof *rrv)) != NULL) atexit(rrv_print);
}
static void rays_verify_run(struct rc *k, struct rcrays *R, int p0, int n, int w, int h, rcr sxs, const uint8_t *out,
                            int ch, int air, const struct chunk_sec *eye)
{
    if (rrv == NULL) return;
    struct raycast_seen *sv = k->seen;
    k->seen = NULL, k->no_run = 1;
    rays_fill(k, R, p0, n, w, h, sxs);
    if (air) rays_air(k, R, n, eye);
    for (int q = 0; q < n; ++q)
    {
        uint8_t o[3];
        walk(k, R, q, 1, o);
        if (__atomic_add_fetch(&rrv[0], 1, __ATOMIC_RELAXED) % (1u << 20) == 0) rrv_print();
        if (memcmp(o, out + (size_t)q * (size_t)ch, (size_t)ch))
        {
            __atomic_add_fetch(&rrv[1], 1, __ATOMIC_RELAXED);
            fprintf(stderr, "rcmove MISMATCH pixel %d: jumps %u %u %u runs %u %u %u\n", p0 + q, o[0], o[1], o[2],
                    out[(size_t)q * ch], ch > 1 ? out[(size_t)q * ch + 1] : 0, ch > 2 ? out[(size_t)q * ch + 2] : 0);
        }
    }
    k->seen = sv, k->no_run = 0;
}
#endif

/* cast()'s state before its rays (k zeroed up to ent): the camera, the light, sky and fog, the eye's liquid and
 * overlay, the entity boxes */
static void setup(const struct raycast_spec *sp, struct session *ss, struct world *w, struct rc *k, int fast)
{
    const struct client_player *cp = &ss->cp;
    k->w = w;
    k->use = &k->use_at;
    k->wx = cp->e.pos_x;
    /* EntityRenderer.orientCamera's yOffset - 1.62 (0 standing) */
    k->wy = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F);
    k->wz = cp->e.pos_z;
#if RC_FP < 64
    /* the eye as its cell and its offset in it: e = floor(w), o = (rcr)(w - e) (the difference exact in double,
     * rounded once; the fixed instance's rounded to RC_FIXF fractional bits, rc_fxq); an offset that rounds to 1 is
     * the next cell's 0 */
    {
        const double w3[3] = {k->wx, k->wy, k->wz};
        int e3[3];
        rcr o3[3];
        for (int a = 0; a < 3; ++a)
        {
            e3[a] = (int)floor(w3[a]);
#if RC_FIX
            o3[a] = (rcr)rc_fxq(w3[a] - (double)e3[a], RC_FIXF);
#else
            o3[a] = (rcr)(w3[a] - (double)e3[a]);
#endif
            if (o3[a] >= RC_K(1.0)) e3[a] += 1, o3[a] = RC_K(0.0);
        }
        k->eye_x = e3[0], k->eye_y = e3[1], k->eye_z = e3[2];
        k->ox = o3[0], k->oy = o3[1], k->oz = o3[2];
        k->bx = k->eye_x, k->by = k->eye_y, k->bz = k->eye_z;
    }
#else
    k->ox = k->wx, k->oy = k->wy, k->oz = k->wz;
    k->eye_x = (int)floor(k->ox), k->eye_y = (int)floor(k->oy), k->eye_z = (int)floor(k->oz);
    k->bx = k->by = k->bz = 0;
#endif
    int rd = sp->rd > 0 ? sp->rd : session_client_rd(ss);
    if (rd < 2) rd = 4;
    k->range = (sp->range > 0 && sp->range < rd * 16) ? sp->range : rd * 16;
    k->rnext = RC_NEXTAFTER((rcr)k->range, INFINITY);
    k->dim = cp->dimension;
    k->no_sky = k->dim != 0;
    k->fancy = sp->fancy != 0;
    int m0 = 0, ey = k->eye_y;
    k->liquid = ey >= 0 && ey < 256 ? liquid_of(cell_ref(k, k->eye_x, ey, k->eye_z, &m0)) : 0;
    /* the camera's axes (semcam.c): Entity.getLook, right horizontal, up = r x f (in the scalar) */
    float yaw = cp->rotation_yaw, pitch = cp->rotation_pitch;
    /* (a NaN angle indexes MathHelper's tables at 0: sin 0 and cos 0, so a NaN yaw has no forward at all and every
     * ray's direction is NaN, which no step ever leaves: look straight ahead instead) */
    if (yaw != yaw) yaw = 0.0F;
    if (pitch != pitch) pitch = 0.0F;
    float f1 = mh_cos(-yaw * 0.017453292F - 3.1415927F);
    float f2 = mh_sin(-yaw * 0.017453292F - 3.1415927F);
    float f3 = -mh_cos(-pitch * 0.017453292F);
    float f4 = mh_sin(-pitch * 0.017453292F);
    k->fx = (rcr)(f2 * f3), k->fy = (rcr)f4, k->fz = (rcr)(f1 * f3);
    k->rx = (rcr)f1, k->ry = RC_K(0.0), k->rz = (rcr)-f2;
    k->ux = k->ry * k->fz - k->rz * k->fy;
    k->uy = k->rz * k->fx - k->rx * k->fz;
    k->uz = k->rx * k->fy - k->ry * k->fx;
    k->aspect = (rcr)sp->w / (rcr)sp->h;
    /* the light, sky and fog */
    struct rs_in in;
    build_rs(sp, ss, k, rd, &in);
    struct rclight L;
    memset(&L, 0, sizeof L);
    if (fast) rc_light_port(&in, &L);
    else rc_light_ref(&in, &L);
#if NETH_RC_HEAD_U8
    memcpy(k->lm_u32, L.lm, sizeof k->lm_u32);
#endif
    /* the lightmap as floats: the last a thread made, when its bytes are the same (on the process heap, one pointer a
     * thread, as rc_state) */
    static _Thread_local struct lmf { int lm[256]; float f[256][3]; int have; } *lmf;
    if (lmf == NULL && (lmf = proc_malloc(sizeof *lmf)) != NULL) lmf->have = 0;
    if (lmf && lmf->have && !memcmp(lmf->lm, L.lm, sizeof lmf->lm)) memcpy(k->lm, lmf->f, sizeof k->lm);
    else
    {
        for (int i = 0; i < 256; ++i)
        {
            k->lm[i][0] = (float)(L.lm[i] >> 16 & 255) / 255.0f;
            k->lm[i][1] = (float)(L.lm[i] >> 8 & 255) / 255.0f;
            k->lm[i][2] = (float)(L.lm[i] & 255) / 255.0f;
        }
        if (lmf)
        {
            memcpy(lmf->lm, L.lm, sizeof lmf->lm);
            memcpy(lmf->f, k->lm, sizeof lmf->f);
            lmf->have = 1;
        }
    }
    k->sky[0] = (float)L.sky[0] * 255.0f, k->sky[1] = (float)L.sky[1] * 255.0f, k->sky[2] = (float)L.sky[2] * 255.0f;
    k->fog[0] = L.fcr * 255.0f, k->fog[1] = L.fcg * 255.0f, k->fog[2] = L.fcb * 255.0f;
    for (int c = 0; c < 3; ++c)
    {
        k->sky[c] = k->sky[c] < 0.0f ? 0.0f : k->sky[c] > 255.0f ? 255.0f : k->sky[c];
        k->fog[c] = k->fog[c] < 0.0f ? 0.0f : k->fog[c] > 255.0f ? 255.0f : k->fog[c];
    }
    k->exp_fog = L.gfogm == 0x0800;
    k->fs = L.gfogs, k->fe = L.gfoge, k->fd = L.gfogd;
    if (!(k->fe > k->fs)) k->fe = k->fs + 1.0f;
    k->fspan = 1.0f / (k->fe - k->fs);
    k->sky_end = L.far * 0.8f;
    if (k->liquid == 8)
    {
        /* the overlay's colour: the texture times Entity.getBrightness, the light at
         * 0.66 of the player's height less World.skylightSubtracted */
        float f1 = 1.0F - (mh_cos(rc_celestial(in.dim, in.wt, 1.0F) * RC_PIF * 2.0F) * 2.0F + 0.5F);
        f1 = f1 < 0.0F ? 0.0F : f1 > 1.0F ? 1.0F : f1;
        f1 = 1.0F - f1;
        f1 = (float)((double)f1 * (1.0 - (double)(rc_rain(&in, 1.0F) * 5.0F) / 16.0));
        f1 = (float)((double)f1 * (1.0 - (double)(rc_thunder(&in, 1.0F) * 5.0F) / 16.0));
        int sub = (int)((1.0F - f1) * 11.0F);
        int sky = in.brf >> 20 & 15, block = in.brf >> 4 & 15;
        int l = sky - sub > block ? sky - sub : block;
        float b = in.lbt[l < 0 ? 0 : l > 15 ? 15 : l];
        k->uw = 1;
        for (int c = 0; c < 3; ++c) k->uw_rgb[c] = T.underwater[c] * b;
    }
    entities(k, ss, sp->w, sp->h);
}

/* ------------------------------------------------------- the kept frame
 *
 * A frame kept for its env's next cast (lane/castreuse): the frame, its seen record and every input its pixels read.
 * A pixel's walk reads the camera (the eye, its axes, the aspect), the eye's cell, liquid and section, the range, top
 * and graphics, the entity boxes (each with its rectangle, colour and light), the cells of the window's chunks
 * (their ids, metadata, light, biomes and height maps), and of the light, sky and fog only what lit() and the miss
 * take: the lightmap entries its hits index, the fog's colour where a hit or the miss is fogged (factor under 1), the
 * sky's where the miss takes the sky plane (factor over 0), the fog's form and its distances, the overlay. The walk's
 * path does not depend on a colour, so a cast whose inputs equal the kept frame's on what the kept frame read
 * takes the same path and reads the same entries again: the same bytes. A chunk's cells are named by its address and
 * its write sequence (world.h wseq: any block, metadata or light write, a client copy's cells and biomes, the
 * insertion; rcmirror.c's device copy skips a column on the same two),
 * which only orders writes within one run of the env: a reset puts the start's sequence back, so the caller's epoch
 * (pool.h pool_epoch: a reset makes it new) is part of the key. */
struct rckcol {
    const struct chunk *c;
    uint64_t wseq;
};
struct rckeep {
    int have, seen_on;
    uint32_t epoch;
    const struct world *w;
    size_t bytes;
    uint8_t *frame;
    struct rckcol *col;
    int colcap;
    int cx0, cz0, ncx, ncz;
    struct rcuse use;
    struct raycast_seen seen;
    /* the frame's record (rcrec: its layers, lay[RC_LAYERS * pixels], each pixel's reach and seen note) for npix
     * pixels; rec_ok: the frame's cast made it, shade_ok: its layers all fit */
    struct rcrec rec;
    size_t npix;
    int rec_ok, shade_ok;
    struct rcwin win;           /* the window's tables kept (sections()) */
    struct rc k;                /* the cast's state up to its entities' end (the pointers past it unused) */
};
/* the window's columns whose chunk changed since the kept frame that keep_match tests against its rays, at most */
enum { RCK_MOVED = 8 };
/* the layers a frame's record has room for, on average a pixel (a recorded cast took 1.060 a pixel on the brief's
 * shape and 1.005 on the 8 seed starts, the verify build's count: a water surface's pixel takes two; a pixel walked
 * again appends its own, so the room is for re-walks: at 2, 0.6% of the brief's and 1.5% of the seed starts' taken
 * frames were lost to records out of room). 28 bytes a layer: 28 KB an env at 16x16, 1.8 MB at 128x128, made on the
 * env's first cast with the camera still */
enum { RC_LAYERS = 4 };
struct raycast_keep {
    int n;
    struct rckeep *e;
#if RC_FP == 64
    struct raycast_keep32 *k32; /* the FP32 instance's keep of the same envs */
    struct raycast_keepfx *kfx; /* the fixed instance's */
    struct raycast_keep16 *k16; /* the FP16 instance's */
#endif
};

struct raycast_keep *raycast_keep_new(int n)
{
    struct raycast_keep *kp = proc_calloc(1, sizeof *kp);
    if (kp == NULL) return NULL;
    kp->n = n;
    if (n > 0 && (kp->e = proc_calloc((size_t)n, sizeof *kp->e)) == NULL)
    {
        proc_free(kp);
        return NULL;
    }
#if RC_FP == 64
    if ((kp->k32 = raycast_keep_new_f32(n)) == NULL || (kp->kfx = raycast_keep_new_fx(n)) == NULL ||
        (kp->k16 = raycast_keep_new_f16(n)) == NULL)
    {
        raycast_keep_free_f32(kp->k32), raycast_keep_free_fx(kp->kfx);
        proc_free(kp->e);
        proc_free(kp);
        return NULL;
    }
#endif
    return kp;
}

void raycast_keep_free(struct raycast_keep *kp)
{
    if (kp == NULL) return;
#if RC_FP == 64
    raycast_keep_free_f32(kp->k32), raycast_keep_free_fx(kp->kfx), raycast_keep_free_f16(kp->k16);
#endif
    for (int i = 0; i < kp->n; ++i)
    {
        struct rcrec *r = &kp->e[i].rec;
        proc_free(kp->e[i].frame);
        proc_free(kp->e[i].col);
        proc_free(kp->e[i].win.col), proc_free(kp->e[i].win.sec), proc_free(kp->e[i].win.lsec);
        proc_free(kp->e[i].win.cws), proc_free(kp->e[i].win.ctop), proc_free(kp->e[i].win.bm);
        proc_free(r->lay), proc_free(r->off), proc_free(r->cnt), proc_free(r->rix), proc_free(r->riz), proc_free(r->reach);
        proc_free(r->sq), proc_free(r->sv), proc_free(r->mk);
    }
    proc_free(kp->e);
    proc_free(kp);
}

#define RCK_SPAN(a, b) (offsetof(struct rc, b) - offsetof(struct rc, a))
/* a kept frame's pixel reads window column c: its ray's run to its reach (its last cell's end, 2 blocks more), across
 * x and z, meets the column's square a block wider each way. In floats from the eye (the square's sides less the
 * eye's, under 100 blocks: their rounding and the reciprocals' are under 1e-5 of a block, inside the margins); a
 * reciprocal is held to 1e30 (the eye's own offset times it is 0 or past every reach, never a NaN); the lanes' pad
 * pixels reach -1, so they meet nothing */
#if defined(RC_LANES)
__attribute__((target("avx512f"))) static int reaches_lanes(const struct rcrec *r, size_t n, const float b[4], uint8_t *mk)
{
    const __m512 x0 = _mm512_set1_ps(b[0]), x1 = _mm512_set1_ps(b[1]), z0 = _mm512_set1_ps(b[2]), z1 = _mm512_set1_ps(b[3]);
    const __m512 zero = _mm512_setzero_ps();
    int hit = 0;
    for (size_t p = 0; p < n; p += 16)
    {
        const __m512 ix = _mm512_loadu_ps(r->rix + p), iz = _mm512_loadu_ps(r->riz + p);
        const __m512 ax = _mm512_mul_ps(x0, ix), bx = _mm512_mul_ps(x1, ix), az = _mm512_mul_ps(z0, iz), bz = _mm512_mul_ps(z1, iz);
        __m512 s0 = _mm512_max_ps(zero, _mm512_max_ps(_mm512_min_ps(ax, bx), _mm512_min_ps(az, bz)));
        __m512 s1 = _mm512_min_ps(_mm512_loadu_ps(r->reach + p), _mm512_min_ps(_mm512_max_ps(ax, bx), _mm512_max_ps(az, bz)));
        unsigned m = _mm512_cmp_ps_mask(s0, s1, _CMP_LE_OQ);
        if (m == 0) continue;
        if (mk == NULL) return 1;
        hit = 1;
        for (; m; m &= m - 1) mk[p + (size_t)__builtin_ctz(m)] = 1;
    }
    return hit;
}
#endif

/* with mk, every pixel whose ray reaches the column marked in it */
static int keep_reaches(const struct rckeep *e, const struct rc *k, int c, uint8_t *mk)
{
    const float b[4] = {(float)((double)(k->cx0 + c / k->ncz) * 16.0 - 1.0 - k->wx),
                        (float)((double)(k->cx0 + c / k->ncz) * 16.0 + 17.0 - k->wx),
                        (float)((double)(k->cz0 + c % k->ncz) * 16.0 - 1.0 - k->wz),
                        (float)((double)(k->cz0 + c % k->ncz) * 16.0 + 17.0 - k->wz)};
    const struct rcrec *r = &e->rec;
#if defined(RC_LANES)
    if (T.avx512) return reaches_lanes(r, e->npix, b, mk);
#endif
    int hit = 0;
    for (size_t p = 0; p < e->npix; ++p)
    {
        const float ax = b[0] * r->rix[p], bx = b[1] * r->rix[p], az = b[2] * r->riz[p], bz = b[3] * r->riz[p];
        float s0 = ax < bx ? ax : bx, s1 = ax < bx ? bx : ax;
        const float u0 = az < bz ? az : bz, u1 = az < bz ? bz : az;
        s0 = s0 > u0 ? s0 : u0, s1 = s1 < u1 ? s1 : u1;
        s0 = s0 > 0.0f ? s0 : 0.0f, s1 = s1 < r->reach[p] ? s1 : r->reach[p];
        if (!(s0 <= s1)) continue;
        if (mk == NULL) return 1;
        hit = 1, mk[p] = 1;
    }
    return hit;
}

/* the kept frame against this cast (k after setup and sections): 2 it is this cast's; 1 this cast's walks are the
 * kept frame's, only the colours its pixels took differ (the lightmap entries, the fog's, the sky's, the overlay's:
 * its shading again makes this frame); 3 the entity boxes differ too, or a window column whose chunk changed (its
 * address or write sequence) is one some pixels' rays reach (keep_reaches): those pixels and the ones a changed box's
 * rectangle holds walked again, the rest shaded again (keep_rewalk); 0 none. A changed column no pixel's ray reaches
 * is the same to the frame. m: the changed columns, whether the boxes differ */
struct rckm {
    int mv[RCK_MOVED], nmv;
    int ents, reached;
    int still;                  /* the camera and the eye's state are the kept frame's (a frame cast now is recorded) */
};
static int keep_match(const struct rckeep *e, const struct rc *k, uint32_t epoch, size_t bytes, int seen_on,
                      struct rckm *m)
{
    const struct rc *p = &e->k;
    m->nmv = m->ents = m->reached = m->still = 0;
    if (!e->have || e->epoch != epoch || e->w != k->w || e->bytes != bytes || e->seen_on != seen_on) return 0;
    /* the camera, the eye's cell and liquid, the range, no_sky, dim, graphics, top, channels and rnext */
    if (memcmp(&p->ox, &k->ox, RCK_SPAN(ox, lm))) return 0;
    m->still = 1;
    m->ents = p->nent != k->nent || memcmp(p->ent, k->ent, (size_t)k->nent * sizeof *k->ent);
    if (e->cx0 != k->cx0 || e->cz0 != k->cz0 || e->ncx != k->ncx || e->ncz != k->ncz) return 0;
    for (int c = 0; c < k->ncx * k->ncz; ++c)
        if (e->col[c].c != k->col[c] || (k->col[c] != NULL && e->col[c].wseq != k->col[c]->wseq))
        {
            if (m->nmv == RCK_MOVED || !e->rec_ok) return 0;
            m->mv[m->nmv++] = c;
        }
    if (seen_on && memcmp(p->seen_ids, k->seen_ids, sizeof k->seen_ids)) return 0;
    /* the fog's form and distances (fs to fspan), the overlay */
    if (p->exp_fog != k->exp_fog || p->uw != k->uw || memcmp(&p->fs, &k->fs, RCK_SPAN(fs, seen))) return 0;
    for (int i = 0; i < m->nmv && !m->reached; ++i) m->reached = keep_reaches(e, k, m->mv[i], NULL);
    if (m->ents || m->reached) return e->shade_ok ? 3 : 0;
    if (k->uw && memcmp(p->uw_rgb, k->uw_rgb, sizeof k->uw_rgb)) return 1;
    if (e->use.fog && memcmp(p->fog, k->fog, sizeof k->fog)) return 1;
    if (e->use.sky && memcmp(p->sky, k->sky, sizeof k->sky)) return 1;
    for (int q = 0; q < 4; ++q)
        for (uint64_t m = e->use.lm[q]; m; m &= m - 1)
        {
            int i = q * 64 + __builtin_ctzll(m);
            if (memcmp(p->lm[i], k->lm[i], sizeof k->lm[i])) return 1;
        }
    return 2;
}

/* the kept frame's shading again under this cast's colours (keep_match 1): each pixel's layers through lit_apply,
 * thru_add, sky_mix and pixel_out as its walk took them */
static inline void shade_pixel(const struct rc *k, const struct rclayer *L, uint32_t m, uint8_t *out)
{
    float acc[3] = {0.0f, 0.0f, 0.0f}, trans = 1.0f, col[3] = {0.0f, 0.0f, 0.0f};
    for (uint32_t j = 0; j < m; ++j, ++L)
        switch (L->kind)
        {
        case RL_THRU:
            lit_apply(k, L->rt, L->li, L->shade, L->f, col);
            thru_add(acc, &trans, L->a, col);
            break;
        case RL_LIT: lit_apply(k, L->rt, L->li, L->shade, L->f, col); break;
        case RL_FOG: memcpy(col, k->fog, sizeof col); break;
        case RL_END: memcpy(col, T.end_sky, sizeof col); break;
        default: sky_mix(k, L->f, col); break;
        }
    pixel_out(k, acc, trans, col, out);
}

/* shade_pixel's sums for the batch's pixel q (k->pix), its bytes made with the batch's (pixels_out) */
static inline void shade_sums(const struct rc *k, const struct rclayer *L, uint32_t m, int q)
{
    float acc[3] = {0.0f, 0.0f, 0.0f}, trans = 1.0f, col[3] = {0.0f, 0.0f, 0.0f};
    for (uint32_t j = 0; j < m; ++j, ++L)
        switch (L->kind)
        {
        case RL_THRU:
            lit_apply(k, L->rt, L->li, L->shade, L->f, col);
            thru_add(acc, &trans, L->a, col);
            break;
        case RL_LIT: lit_apply(k, L->rt, L->li, L->shade, L->f, col); break;
        case RL_FOG: memcpy(col, k->fog, sizeof col); break;
        case RL_END: memcpy(col, T.end_sky, sizeof col); break;
        default: sky_mix(k, L->f, col); break;
        }
    struct rcpix *P = k->pix;
    P->acc[0][q] = acc[0], P->acc[1][q] = acc[1], P->acc[2][q] = acc[2], P->trans[q] = trans;
    P->col[0][q] = col[0], P->col[1][q] = col[1], P->col[2][q] = col[2];
}

static void reshade(const struct rc *k, const struct rckeep *e, uint8_t *rgb, size_t n, int ch)
{
    for (size_t p0 = 0; p0 < n; p0 += RC_BATCH)
    {
        const int nb = n - p0 < RC_BATCH ? (int)(n - p0) : RC_BATCH;
        for (int q = 0; q < nb; ++q) shade_sums(k, e->rec.lay + e->rec.off[p0 + q], e->rec.cnt[p0 + q], q);
        pixels_out(k, nb, rgb + p0 * (size_t)ch);
    }
}

/* the frame reshade made is kept: this cast's colours */
static void keep_colours(struct rckeep *e, const struct rc *k, const uint8_t *rgb)
{
    memcpy(e->frame, rgb, e->bytes);
    memcpy(e->k.lm, k->lm, sizeof k->lm);
    memcpy(e->k.sky, k->sky, sizeof k->sky);
    memcpy(e->k.fog, k->fog, sizeof k->fog);
    memcpy(e->k.uw_rgb, k->uw_rgb, sizeof k->uw_rgb);
}

/* the keep's room for a frame of n pixels' layers; 0, or -1 (no room: the frame is cast unkept) */
static int keep_room(struct rckeep *e, size_t n)
{
    struct rcrec *r = &e->rec;
    if (e->npix == n) return 0;
    proc_free(r->lay), proc_free(r->off), proc_free(r->cnt), proc_free(r->rix), proc_free(r->riz), proc_free(r->reach);
    proc_free(r->sq), proc_free(r->sv), proc_free(r->mk);
    memset(r, 0, sizeof *r);
    e->have = 0;
    e->npix = 0;
    /* the reach arrays to a multiple of 16 pixels (reaches_lanes), the pad reaching nothing */
    const size_t n16 = (n + 15) & ~(size_t)15;
    r->lay = proc_malloc(n * RC_LAYERS * sizeof *r->lay);
    r->off = proc_malloc(n * sizeof *r->off);
    r->cnt = proc_malloc(n * sizeof *r->cnt);
    r->rix = proc_calloc(n16, sizeof *r->rix);
    r->riz = proc_calloc(n16, sizeof *r->riz);
    r->reach = proc_malloc(n16 * sizeof *r->reach);
    r->sq = proc_malloc(n * sizeof *r->sq);
    r->sv = proc_malloc(n * sizeof *r->sv);
    r->mk = proc_malloc(n);
    if (!r->lay || !r->off || !r->cnt || !r->rix || !r->riz || !r->reach || !r->sq || !r->sv || !r->mk) return -1;
    for (size_t p = n; p < n16; ++p) r->reach[p] = -1.0f;
    r->cap = (uint32_t)(n * RC_LAYERS);
    e->npix = n;
    return 0;
}

/* the window's stamps now (a frame taken with columns changed that none of its rays reaches) */
static void keep_window(struct rckeep *e, const struct rc *k)
{
    for (int c = 0; c < k->ncx * k->ncz; ++c) e->col[c].c = k->col[c], e->col[c].wseq = k->col[c] ? k->col[c]->wseq : 0;
}

static void rays_runs(struct rc *k, const struct raycast_spec *sp, uint8_t *rgb, int fast, const int *at,
                      const int *len, int nr, int ch);

/* the pixels entity box en's rectangle holds (rays_fill tests a box only there), marked */
static void rewalk_mark(uint8_t *mk, const struct rcent *en, int w, int h)
{
    int i0 = en->i0, i1 = en->i1, j0 = en->j0, j1 = en->j1;
    if (i0 <= INT32_MIN / 4) i0 = j0 = 0, i1 = w - 1, j1 = h - 1;
    i0 = i0 < 0 ? 0 : i0, j0 = j0 < 0 ? 0 : j0;
    i1 = i1 > w - 1 ? w - 1 : i1, j1 = j1 > h - 1 ? h - 1 : j1;
    for (int j = j0; j <= j1; ++j)
        if (i0 <= i1) memset(mk + (size_t)j * (size_t)w + i0, 1, (size_t)(i1 - i0 + 1));
}

/* keep_match 3: the kept frame's walks are this cast's but for the entity boxes and the changed columns some rays
 * reach. A pixel's walk meets a box only through rays_fill, which tests a box only at the pixels its rectangle holds
 * and takes the nearest, the first of equals; so a pixel no changed box's rectangle holds (a box at an index whose
 * box differs, before or now, or past the shorter list) and whose ray reaches no changed column tests the same boxes
 * in the same order and reads the same cells as the kept frame's walk did: it is shaded again from its layers. The
 * other pixels are walked again, their layers appended (the record's room ending makes the frame unshadeable from
 * then on, its bytes still the walk's). The seen record is made again from the pixels' notes in pixel order, as
 * seen_note made it. -1: more than half the frame (cast it whole). */
static int keep_rewalk(struct rckeep *e, struct rc *k, const struct raycast_spec *sp, uint8_t *rgb, size_t n, int ch,
                       struct raycast_seen *seen, const struct rckm *km, int *walked)
{
    struct rcrec *r = &e->rec;
    const struct rc *p = &e->k;
    const int w = sp->w, h = sp->h;
    memset(r->mk, 0, n);
    const int m = p->nent < k->nent ? p->nent : k->nent;
    for (int i = 0; i < m; ++i)
        if (memcmp(&p->ent[i], &k->ent[i], sizeof k->ent[i])) rewalk_mark(r->mk, &p->ent[i], w, h), rewalk_mark(r->mk, &k->ent[i], w, h);
    for (int i = m; i < p->nent; ++i) rewalk_mark(r->mk, &p->ent[i], w, h);
    for (int i = m; i < k->nent; ++i) rewalk_mark(r->mk, &k->ent[i], w, h);
    if (km->reached)
        for (int i = 0; i < km->nmv; ++i) keep_reaches(e, k, km->mv[i], r->mk);
    size_t nm = 0;
    for (size_t q = 0; q < n; ++q) nm += r->mk[q];
    if (nm * 2 > n) return -1;
    k->rec = r;
    for (size_t q = 0; q < n;)
    {
        size_t q1 = q;
        while (q1 < n && r->mk[q1] == r->mk[q]) ++q1;
        if (r->mk[q])
        {
            const int at = (int)q, len = (int)(q1 - q);
            rays_runs(k, sp, rgb, 1, &at, &len, 1, ch);
        }
        else
            for (size_t s = q; s < q1; ++s) shade_pixel(k, r->lay + r->off[s], r->cnt[s], rgb + s * (size_t)ch);
        q = q1;
    }
    k->rec = NULL;
    if (seen)
    {
        memset(seen, 0, sizeof *seen);
        for (int q = 0; q < RC_SEEN; ++q) seen->d[q] = -1.0f;
        for (size_t q = 0; q < n; ++q)
            if (r->sq[q] >= 0) seen_add(seen, r->sq[q], r->sv[q].x, r->sv[q].y, r->sv[q].z, r->sv[q].d, 0, NULL);
        e->seen = *seen;
    }
    keep_colours(e, k, rgb);
    memcpy(e->k.ent, k->ent, (size_t)k->nent * sizeof *k->ent);
    e->k.nent = k->nent;
    /* the walked pixels' lightmap entries, fog and sky with the rest's (a superset of what the frame read) */
    for (int q = 0; q < 4; ++q) e->use.lm[q] |= k->use_at.lm[q];
    e->use.fog |= k->use_at.fog, e->use.sky |= k->use_at.sky;
    if (r->over) e->shade_ok = 0;
    *walked = (int)nm;
    return 0;
}

static void keep_store(struct rckeep *e, const struct rc *k, uint32_t epoch, const uint8_t *rgb, size_t bytes,
                       const struct raycast_seen *seen)
{
    e->have = 0;
    if (e->bytes != bytes || e->frame == NULL)
    {
        proc_free(e->frame);
        e->bytes = bytes;
        if ((e->frame = proc_malloc(bytes)) == NULL) return;
    }
    const int nc = k->ncx * k->ncz;
    if (nc > e->colcap)
    {
        proc_free(e->col);
        e->colcap = 0;
        if ((e->col = proc_malloc((size_t)nc * sizeof *e->col)) == NULL) return;
        e->colcap = nc;
    }
    for (int c = 0; c < nc; ++c) e->col[c].c = k->col[c], e->col[c].wseq = k->col[c] ? k->col[c]->wseq : 0;
    e->cx0 = k->cx0, e->cz0 = k->cz0, e->ncx = k->ncx, e->ncz = k->ncz;
    memcpy(e->frame, rgb, bytes);
    memcpy(&e->k, k, offsetof(struct rc, ent) + (size_t)k->nent * sizeof *k->ent);
    e->use = k->use_at;
    e->seen_on = seen != NULL;
    if (seen) e->seen = *seen;
    e->epoch = epoch;
    e->w = k->w;
    e->have = 1;
}
#undef RCK_SPAN

#if defined(RC_KEEP_VERIFY)
/* the verify build: a kept or shaded frame cast again and compared, counted in proc_malloc memory (lane/hostcast:
 * libneth's statics read zero in some runs) and printed every 65,536 casts and at exit */
static uint64_t *rkv;           /* casts with a keep, frames taken (kept or shaded), frames that differ, seen records
                                 * that differ, frames shaded, shaded frames that differ, frames taken with window
                                 * columns changed (3: any reached), frames walked again in part and their pixels,
                                 * walked frames that differ; the recorded casts' layers and pixels, those over room */
static pthread_once_t rkv_once = PTHREAD_ONCE_INIT;
static void rkv_print(void)
{
    if (rkv)
        fprintf(stderr, "rckeep casts %llu taken %llu (shaded %llu, moved %llu, walked again %llu: %llu pixels) differ %llu "
                "(shaded %llu, walked %llu) seen-differ %llu; recorded %llu pixels, %.3f layers a pixel, %llu over room\n",
                (unsigned long long)rkv[0], (unsigned long long)rkv[1], (unsigned long long)rkv[4],
                (unsigned long long)rkv[6], (unsigned long long)rkv[7], (unsigned long long)rkv[8],
                (unsigned long long)rkv[2], (unsigned long long)rkv[5], (unsigned long long)rkv[9],
                (unsigned long long)rkv[3], (unsigned long long)rkv[11], rkv[11] ? (double)rkv[10] / (double)rkv[11] : 0.0,
                (unsigned long long)rkv[12]);
}
static void rkv_init(void)
{
    if ((rkv = proc_calloc(13, sizeof *rkv)) != NULL) atexit(rkv_print);
}
#endif

/* keep_reaches' reciprocal of a direction d (id: rays_row's, 0 where d is): held to 1e30 */
static inline float rc_inv(rcr d, rcr id)
{
    return d == RC_K(0.0) || id > RC_K(1e30) ? 1e30f : id < -RC_K(1e30) ? -1e30f : (float)id;
}

/* the rays of runs of pixels (run r: pixels at[r] to at[r] + len[r]), k set up; with k->rec each pixel's record */
static void rays_runs(struct rc *k, const struct raycast_spec *sp, uint8_t *rgb, int fast, const int *at,
                      const int *len, int nr, int ch)
{
    rcr sxs = RC_TAN_HALF * k->aspect;
    struct rcrays *R = k->rays;
    /* the air prefix where it holds (rays_air) */
    const struct chunk_sec *eye = fast && (unsigned)k->eye_y <= 255u ? sec_fast(k, k->eye_x, k->eye_y, k->eye_z) : NULL;
    const int air = eye != NULL && (k->liquid == 0 || k->liquid == 8) && eye->ids_hi == NULL && rc_lanes();
    k->row->sx_i0 = -1;
    for (int r = 0; r < nr; ++r)
        for (int p0 = at[r]; p0 < at[r] + len[r]; p0 += RC_BATCH)
        {
            const int nb = at[r] + len[r] - p0 < RC_BATCH ? at[r] + len[r] - p0 : RC_BATCH;
            rays_fill(k, R, p0, nb, sp->w, sp->h, sxs);
            if (air) rays_air(k, R, nb, eye);
            /* runs walk hands to rays_on: in a frame without a record (a pixel's layers are made in one walk) with the
             * fast path's lanes, through air (a4: the eye in no liquid or in water) and through water (a3: in none) */
#if !defined(RC_SUSP)
#define RC_SUSP 3               /* which runs (a measure's builds: 0 none, 1 water alone) */
#endif
            k->susp = fast && air && k->rec == NULL ? (k->liquid == 0 ? 3 : 2) & RC_SUSP : 0;
            R->nw = 0;
            for (int q = 0; q < nb; ++q)
            {
                uint8_t *o = rgb + (size_t)(p0 + q) * (size_t)ch;
                struct rcrec *rc_ = k->rec;
                if (rc_)
                {
                    rc_->off[p0 + q] = rc_->at, rc_->pix = p0 + q, rc_->sq[p0 + q] = -1;
                    rc_->rix[p0 + q] = rc_inv(R->dx[q], R->idx[q]), rc_->riz[p0 + q] = rc_inv(R->dz[q], R->idz[q]);
                }
                k->pix_at = p0 + q;
                /* two instances: the reference's reads and the fast path's */
                if (fast) walk(k, R, q, 1, NULL);
                else walk(k, R, q, 0, o);
                if (rc_) rc_->cnt[p0 + q] = rc_->at - rc_->off[p0 + q];
            }
#if defined(RC_LANES)
            if (R->nw)
            {
                /* through the water or air, then on from where the lanes left each ray, in pixel order */
                rays_on(k, R);
                for (int i = 0; i < R->nw; ++i)
                {
                    const int q = R->wq[i];
                    k->pix_at = p0 + q;
                    walk(k, R, q, 1, NULL);
                }
            }
#endif
            k->susp = 0;
            if (fast) pixels_out(k, nb, rgb + (size_t)p0 * (size_t)ch);
#if defined(RC_AIR_VERIFY)
            if (air) rays_verify(k, R, p0, nb, sp->w, sp->h, sxs, rgb + (size_t)p0 * (size_t)ch, ch);
#endif
#if defined(RC_RUN_VERIFY)
            if (fast && k->rec == NULL) rays_verify_run(k, R, p0, nb, sp->w, sp->h, sxs, rgb + (size_t)p0 * (size_t)ch, ch, air, eye);
#endif
        }
}

/* the frame's rays */
static void rays_all(struct rc *k, const struct raycast_spec *sp, uint8_t *rgb, int fast, size_t n, int ch)
{
    const int at = 0, len = (int)n;
    rays_runs(k, sp, rgb, fast, &at, &len, 1, ch);
}

/* the cast; with kp (the fast path's alone), env id's kept frame taken when this cast's inputs are the ones it read,
 * else the frame cast and kept */
static int cast(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, int fast, const int16_t *ids,
                struct raycast_seen *seen, struct raycast_keep *kp, int id, uint32_t epoch)
{
    if (seen)
    {
        memset(seen, 0, sizeof *seen);
        for (int q = 0; q < RC_SEEN; ++q) seen->d[q] = -1.0f;
    }
    const int ch = sp->ch == 1 ? 1 : 3;
    size_t n = (size_t)sp->w * (size_t)sp->h;
    raycast_load(NULL, NULL, 0);
    struct world *w = ss->client_world;
    struct rc *k = w != NULL && T.ok ? rc_state() : NULL;
    if (k == NULL)
    {
        memset(rgb, 0, n * (size_t)ch);
        return -1;
    }
    rc_clear(k);
#if defined(RC_RUN_VERIFY)
    pthread_once(&rrv_once, rrv_init);
    k->no_run = 0;
#endif
#if defined(RC_BRICK_COUNT)
    pthread_once(&rbc_once, rbc_init);
#endif
    if (seen)
    {
        k->seen = seen, k->spx = k->spx_at;
        memcpy(k->seen_ids, ids, sizeof k->seen_ids);
    }
    setup(sp, ss, w, k, fast);
    k->ch = ch;
    struct rckeep *e = fast && kp != NULL && id >= 0 && id < kp->n ? &kp->e[id] : NULL;
    if (fast && sections(k, e ? &e->win : NULL, epoch)) fast = 0, e = NULL;
    const size_t bytes = n * (size_t)ch;
#if defined(RC_KEEP_VERIFY)
    if (e) pthread_once(&rkv_once, rkv_init);
    if (e && rkv && __atomic_add_fetch(&rkv[0], 1, __ATOMIC_RELAXED) % 65536 == 0) rkv_print();
#endif
    struct rckm m;
    int walked = 0;
    const int km = e ? keep_match(e, k, epoch, bytes, seen != NULL, &m) : 0;
    if (km == 2 || (km == 1 && e->shade_ok) || (km == 3 && keep_rewalk(e, k, sp, rgb, n, ch, seen, &m, &walked) == 0))
    {
        if (km == 2) memcpy(rgb, e->frame, bytes);
        else if (km == 1)
        {
            reshade(k, e, rgb, n, ch);
            keep_colours(e, k, rgb);
        }
        if (m.nmv) keep_window(e, k);
        if (seen) *seen = e->seen;
#if defined(RC_KEEP_VERIFY)
        if (km == 1) __atomic_add_fetch(&rkv[4], 1, __ATOMIC_RELAXED);
        if (m.nmv) __atomic_add_fetch(&rkv[6], 1, __ATOMIC_RELAXED);
        if (km == 3) __atomic_add_fetch(&rkv[7], 1, __ATOMIC_RELAXED), __atomic_add_fetch(&rkv[8], (uint64_t)walked, __ATOMIC_RELAXED);
        /* cast it anyway into a frame of the thread's and compare */
        static _Thread_local uint8_t *vf;
        static _Thread_local size_t vn;
        if (vn < bytes) proc_free(vf), vf = proc_malloc(bytes), vn = vf ? bytes : 0;
        struct raycast_seen vs;
        if (seen)
        {
            memset(&vs, 0, sizeof vs);
            for (int q = 0; q < RC_SEEN; ++q) vs.d[q] = -1.0f;
            k->seen = &vs;
        }
        if (vf && rkv)
        {
            rays_all(k, sp, vf, fast, n, ch);
            __atomic_add_fetch(&rkv[1], 1, __ATOMIC_RELAXED);
            if (memcmp(vf, rgb, bytes))
            {
                __atomic_add_fetch(&rkv[2], 1, __ATOMIC_RELAXED);
                if (km == 1) __atomic_add_fetch(&rkv[5], 1, __ATOMIC_RELAXED);
                if (km == 3) __atomic_add_fetch(&rkv[9], 1, __ATOMIC_RELAXED);
                fprintf(stderr, "rckeep MISMATCH env %d: the %s frame differs from its cast\n", id,
                        km == 1 ? "shaded" : km == 3 ? "walked" : "kept");
            }
            if (seen && memcmp(&vs, seen, sizeof vs)) __atomic_add_fetch(&rkv[3], 1, __ATOMIC_RELAXED);
        }
#endif
        return 0;
    }
    /* cast, the frame's record in the keep's when the camera was still since the kept frame (a record serves only a
     * cast with the camera of the frame it records; making it costs 19k instructions a cast, 2.3% of an env-step
     * with the macros off; the frame after a camera's move is kept whole) */
#if defined(RC_KEEP_RECORD_ALL)
    m.still = 1;
#endif
    if (e && m.still && keep_room(e, n) == 0)
    {
        e->rec.at = 0, e->rec.over = 0;
        k->rec = &e->rec;
    }
    rays_all(k, sp, rgb, fast, n, ch);
#if defined(RC_KEEP_VERIFY)
    if (k->rec && rkv)
    {
        __atomic_add_fetch(&rkv[10], k->rec->at, __ATOMIC_RELAXED), __atomic_add_fetch(&rkv[11], n, __ATOMIC_RELAXED);
        if (k->rec->over) __atomic_add_fetch(&rkv[12], 1, __ATOMIC_RELAXED);
    }
#endif
    if (e)
    {
        keep_store(e, k, epoch, rgb, bytes, seen);
        e->rec_ok = k->rec != NULL;
        e->shade_ok = k->rec != NULL && !e->rec.over;
        k->rec = NULL;
    }
    return 0;
}

/* (the FP64 instance's entry points hand a spec with fp 32 to the FP32 instance's) */
int raycast(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb)
{
#if RC_FP == 64
    if (sp->fp == 32 || sp->fp == RAYCAST_FP_MIXED16 || sp->fp == RAYCAST_FP_FAST32) return raycast_f32(sp, ss, rgb);
    if (sp->fp == RAYCAST_FP_FIXED) return raycast_fx(sp, ss, rgb);
    if (sp->fp == 16 || sp->fp == RAYCAST_FP_FAST16) return raycast_f16(sp, ss, rgb);
#endif
    return cast(sp, ss, rgb, 1, NULL, NULL, NULL, -1, 0);
}

int raycast_ref(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb)
{
#if RC_FP == 64
    if (sp->fp == 32 || sp->fp == RAYCAST_FP_MIXED16 || sp->fp == RAYCAST_FP_FAST32) return raycast_ref_f32(sp, ss, rgb);
    if (sp->fp == RAYCAST_FP_FIXED) return raycast_ref_fx(sp, ss, rgb);
    if (sp->fp == 16 || sp->fp == RAYCAST_FP_FAST16) return raycast_ref_f16(sp, ss, rgb);
#endif
    return cast(sp, ss, rgb, 0, NULL, NULL, NULL, -1, 0);
}

int raycast_seen(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                 struct raycast_seen *seen)
{
#if RC_FP == 64
    if (sp->fp == 32 || sp->fp == RAYCAST_FP_MIXED16 || sp->fp == RAYCAST_FP_FAST32) return raycast_seen_f32(sp, ss, rgb, ids, seen);
    if (sp->fp == RAYCAST_FP_FIXED) return raycast_seen_fx(sp, ss, rgb, ids, seen);
    if (sp->fp == 16 || sp->fp == RAYCAST_FP_FAST16) return raycast_seen_f16(sp, ss, rgb, ids, seen);
#endif
    return cast(sp, ss, rgb, 1, ids, seen, NULL, -1, 0);
}

#if defined(RC_CAST_TIME) && defined(RC_AVX2)
/* the timing build (lane/castwet): the vector env's casts' own TSC cycles, summed in proc_malloc memory and printed at
 * exit (casts, cycles, cycles a cast), so a pair of builds compares the cast alone at the same work */
static uint64_t *rct;
static pthread_once_t rct_once = PTHREAD_ONCE_INIT;
static void rct_print(void)
{
    if (rct)
        fprintf(stderr, "rctime casts %llu cycles %llu a cast %.0f\n", (unsigned long long)rct[0], (unsigned long long)rct[1],
                rct[0] ? (double)rct[1] / (double)rct[0] : 0.0);
}
static void rct_init(void)
{
    if ((rct = proc_calloc(2, sizeof *rct)) != NULL) atexit(rct_print);
}
#endif

int raycast_seen_keep(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                      struct raycast_seen *seen, struct raycast_keep *kp, int id, uint32_t epoch)
{
#if RC_FP == 64
    if (sp->fp == 32 || sp->fp == RAYCAST_FP_MIXED16 || sp->fp == RAYCAST_FP_FAST32) return raycast_seen_keep_f32(sp, ss, rgb, ids, seen, kp ? kp->k32 : NULL, id, epoch);
    if (sp->fp == RAYCAST_FP_FIXED) return raycast_seen_keep_fx(sp, ss, rgb, ids, seen, kp ? kp->kfx : NULL, id, epoch);
    if (sp->fp == 16 || sp->fp == RAYCAST_FP_FAST16) return raycast_seen_keep_f16(sp, ss, rgb, ids, seen, kp ? kp->k16 : NULL, id, epoch);
#endif
#if defined(RC_CAST_TIME) && defined(RC_AVX2)
    pthread_once(&rct_once, rct_init);
    const uint64_t t0 = __rdtsc();
    const int r = cast(sp, ss, rgb, 1, ids, seen, kp, id, epoch);
    if (rct) __atomic_add_fetch(&rct[0], 1, __ATOMIC_RELAXED), __atomic_add_fetch(&rct[1], __rdtsc() - t0, __ATOMIC_RELAXED);
    return r;
#else
    return cast(sp, ss, rgb, 1, ids, seen, kp, id, epoch);
#endif
}

/* ------------------------------------------------- the device's cast's side */

int raycast_head(const struct raycast_spec *sp, struct session *ss, const int16_t *ids, struct rcd_head *h)
{
#if RC_FP == 64
    _Static_assert(sizeof(struct rcd_ent) == sizeof(struct rcent), "rchead.h's entity is raycast.c's");
    if (sp->fp == 32) return raycast_head_f32(sp, ss, ids, h);
    if (sp->fp == RAYCAST_FP_FAST16 || sp->fp == RAYCAST_FP_MIXED16 || sp->fp == RAYCAST_FP_FAST32)
    {
        int r = raycast_head_f32(sp, ss, ids, h);
        h->fp = sp->fp;
        return r;
    }
    if (sp->fp == RAYCAST_FP_FIXED) return raycast_head_fx(sp, ss, ids, h);
    if (sp->fp == 16) return raycast_head_f16(sp, ss, ids, h);
#endif
#if NETH_RC_HEAD_U8
    memset(h, 0, offsetof(struct rcd_head, ent));
#else
    memset(h, 0, sizeof *h);
#endif
    /* the instance the device casts: the head's reals are this instance's values (FP32's and FP16's widened, exactly) */
    h->fp = RC_FIX ? RAYCAST_FP_FIXED : RC_FP;
    if (ids)
    {
        h->seen_on = 1;
        for (int q = 0; q < RC_SEEN; ++q) h->seen_ids[q] = ids[q];
    }
    raycast_load(NULL, NULL, 0);
    struct world *w = ss->client_world;
    struct rc *k = w != NULL && T.ok ? rc_state() : NULL;
    if (k == NULL) return -1;
    rc_clear(k);
    setup(sp, ss, w, k, 1);
    h->ox = k->ox, h->oy = k->oy, h->oz = k->oz;
    h->fx = k->fx, h->fy = k->fy, h->fz = k->fz;
    h->rx = k->rx, h->ry = k->ry, h->rz = k->rz;
    h->ux = k->ux, h->uy = k->uy, h->uz = k->uz;
    h->aspect = k->aspect, h->rnext = k->rnext;
    h->eye_x = k->eye_x, h->eye_y = k->eye_y, h->eye_z = k->eye_z;
    h->liquid = k->liquid, h->range = k->range, h->no_sky = k->no_sky, h->dim = k->dim, h->fancy = k->fancy;
#if NETH_RC_HEAD_U8
    memcpy(h->lm, k->lm_u32, sizeof h->lm);
#else
    memcpy(h->lm, k->lm, sizeof h->lm);
#endif
    memcpy(h->sky, k->sky, sizeof h->sky);
    memcpy(h->fog, k->fog, sizeof h->fog);
    h->exp_fog = k->exp_fog, h->uw = k->uw;
    memcpy(h->uw_rgb, k->uw_rgb, sizeof h->uw_rgb);
    h->fs = k->fs, h->fe = k->fe, h->fd = k->fd, h->sky_end = k->sky_end, h->fspan = k->fspan;
    h->nent = k->nent;
#if RC_FP == 64
    memcpy(h->ent, k->ent, (size_t)k->nent * sizeof *h->ent);
#else
    for (int e = 0; e < k->nent; ++e)
    {
        struct rcd_ent *d = &h->ent[e];
        const struct rcent *s = &k->ent[e];
        for (int a = 0; a < 6; ++a) d->box[a] = (double)s->box[a];
        d->i0 = s->i0, d->i1 = s->i1, d->j0 = s->j0, d->j1 = s->j1;
        memcpy(d->rgb, s->rgb, sizeof d->rgb);
        d->lmi = s->lmi;
    }
#endif
    /* sections()'s window; its top is the mirror's (rcmirror.c), which reads the same chunks */
    int r = k->range + 1;
    h->cx0 = (int)floor(k->wx - r) >> 4;
    h->cz0 = (int)floor(k->wz - r) >> 4;
    h->ncx = ((int)floor(k->wx + r) >> 4) - h->cx0 + 1;
    h->ncz = ((int)floor(k->wz + r) >> 4) - h->cz0 + 1;
    h->ok = 1;
    return 0;
}

#if RC_FP == 64
int raycast_tables(struct rcd_tabsrc *o)
{
    raycast_load(NULL, NULL, 0);
    if (!T.ok) return -1;
    memset(o, 0, sizeof *o);
    o->nspr = T.nspr;
    o->tex = &T.tex[0][0];
    o->nface = (size_t)RB_IDS * RB_METAS * RB_SIDES;
    o->face[0] = T.face[0], o->face[1] = T.face[1];
    o->bounds = T.bounds64;
    o->kind = T.kind, o->tint = T.tint, o->emit = T.emit, o->cull_same = T.cull_same;
    o->grass = &T.grass[0][0], o->foliage = &T.foliage[0][0], o->water = &T.water[0][0];
    o->end_sky = T.end_sky;
    o->water_spr = T.water_spr, o->lava_spr = T.lava_spr;
    o->dp_low = T.dp_low, o->dp_high = T.dp_high;
    return 0;
}
#endif
