#define _POSIX_C_SOURCE 200809L
#include <stdatomic.h>
#include "raster.h"
#include "raster_obs.h"
#include "texanim.h"
#include "raster_sky2.h"
#include "raster_texture.h"
#include "raster_entities.h"
#include "raster_mobs.h"
#include "raster_overlay.h"
#include "raster_worldfx.h"
#include "raster_particles.h"
#include "raster_hand.h"
#include "raster_things.h"
#include "raster_tileent.h"
#include "jmath.h"
#include "meshfeed.h"
#include "raster_meshkey.h"
#include "raster_prec.h"
#include "world.h"
#include "render_blocks.h"
#include "rbshare.h"
#include "renderstate.h"
#include "smath.h"
#include "tape.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct vertex {
    float x, y, sx, sy, z, w, u, v, color[4], light[2];
    float depth;
    float fogcoord;
    float clip[4];
};

struct live_list;

struct frame {
    int w, h;
    float proj[16], mv[16], fog[3], fogs, foge, fogd;
    int fogm, dimension, clouds_enabled;
    float sky[3], sky_bottom[3], sky_far, sky_floor, sunrise[4];
    float cloud_color[3], cloud_height;
    float celestial_angle;
    float star;                   /* getStarBrightness times 1 - getRainStrength */
    int cloud_tick, moon_phase;
    int sky_mode, cloud_prepass;
    int render_pass;
    int band, band_y0, band_y1;   /* live frames: the rows this thread owns */
    struct live_list *sink;       /* live frames: clip_triangle records instead of drawing */
    double cam[3];
    /* the blocks drawn a second time from inside (the row's "inside": the
     * render view entity's block when their section was last built); a row
     * without the list takes the mesh dump's own camera block */
    int has_inside, n_inside, inside[16][3];
    struct raster_texture texture;
    unsigned char *clouds, *sun, *moon, *end_sky, *rgb, *mask;
    float *depth;
    uint32_t lm[256];
    unsigned long triangles, samples;
    int prec;                     /* raster_prec.h: the stages drawn fast (0: exact) */
};

static unsigned char byte(float x);

static void *alloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "raster: allocation failed\n"); exit(1); }
    return p;
}

static float fbits(uint32_t n)
{
    float f;
    memcpy(&f, &n, sizeof f);
    return f;
}

static double dbits(uint64_t n)
{
    double d;
    memcpy(&d, &n, sizeof d);
    return d;
}

static float jf(const struct jval *v)
{
    uint32_t n = 0;
    if (!json_float(v, &n)) { fprintf(stderr, "raster: expected float\n"); exit(1); }
    return fbits(n);
}

static double jd(const struct jval *v)
{
    uint64_t n = 0;
    if (!json_double(v, &n)) { fprintf(stderr, "raster: expected double\n"); exit(1); }
    return dbits(n);
}

static int ji(const struct jval *v)
{
    int64_t n = 0;
    if (!json_int(v, &n)) { fprintf(stderr, "raster: expected integer\n"); exit(1); }
    return (int)n;
}

/* The same three readers with a default, for the state fields a recording may
 * predate (the weather and overlay inputs joined RenderStateProbe later). */
static float jf_opt(const struct jval *o, const char *k, float dflt)
{
    uint32_t n = 0;
    return json_float(json_get(o, k), &n) ? fbits(n) : dflt;
}

static double jd_opt(const struct jval *o, const char *k, double dflt)
{
    uint64_t n = 0;
    return json_double(json_get(o, k), &n) ? dbits(n) : dflt;
}

static int ji_opt(const struct jval *o, const char *k, int dflt)
{
    int64_t n = 0;
    return json_int(json_get(o, k), &n) ? (int)n : dflt;
}

static double jdouble_or(const struct jval *v, double dflt)
{
    uint64_t n = 0;
    return json_double(v, &n) ? dbits(n) : dflt;
}

static char *lastline(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) {
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    return last;
}

static struct jval *readjson(const char *path)
{
    char *line = lastline(path);
    if (!line) { fprintf(stderr, "raster: cannot read %s\n", path); exit(1); }
    struct jval *j = json_parse(line);
    if (!j) { fprintf(stderr, "raster: invalid JSON %s\n", path); exit(1); }
    return j;
}

/* readjson, but a missing or unreadable file is NULL instead of fatal: a scene
 * recorded before the HUD state dumps has no state/gui.json. */
static struct jval *readjson_opt(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fclose(f);
    return readjson(path);
}


static void path(char *out, size_t n, const char *dir, const char *name)
{
    if (snprintf(out, n, "%s/%s", dir, name) >= (int)n) exit(1);
}

static void load_state(struct frame *f, const char *dir)
{
    char p[1024];
    path(p, sizeof p, dir, "state/frames.jsonl");
    struct jval *root = readjson(p);
    const struct jval *o = json_get(root, "out"), *disp = json_get(root, "disp");
    const struct jval *a = json_get(o, "proj");
    for (int i = 0; i < 16; ++i) f->proj[i] = jf(json_at(a, i));
    a = json_get(o, "mv");
    for (int i = 0; i < 16; ++i) f->mv[i] = jf(json_at(a, i));
    a = json_get(o, "cam");
    f->cam[0] = jd(json_get(a, "x"));
    f->cam[1] = jd(json_get(a, "y"));
    f->cam[2] = jd(json_get(a, "z"));
    f->w = ji(json_at(disp, 0));
    f->h = ji(json_at(disp, 1));
    {
        const struct jval *in = json_get(root, "inside");
        if (in) {
            f->has_inside = 1;
            for (int i = 0; i < json_len(in) && f->n_inside < 16; ++i, ++f->n_inside)
                for (int k = 0; k < 3; ++k) f->inside[f->n_inside][k] = ji(json_at(json_at(in, i), k));
        }
    }
    a = json_get(o, "glfog");
    const struct jval *c = json_get(a, "c");
    for (int i = 0; i < 3; ++i) f->fog[i] = jf(json_at(c, i));
    f->fogs = jf(json_get(a, "s"));
    f->foge = jf(json_get(a, "e"));
    f->fogd = jf(json_get(a, "d"));
    f->fogm = ji(json_get(a, "m"));
    a = json_get(o, "lm");
    for (int i = 0; i < 256; ++i) f->lm[i] = (uint32_t)ji(json_at(a, i));
    const struct jval *wo = json_get(root, "wo");
    f->dimension = ji(json_get(wo, "dim"));
    int64_t wt = ji(json_get(wo, "wt"));
    float pt = jf(json_get(root, "pt"));
    /* World.getSkyColor's own result (the g block's "sky"), which already
     * carries the rain and thunder dimming; a recording that predates the g
     * block falls back to the biome sky colour times the celestial daylight,
     * which is that result with no rain. */
    const struct jval *gsky = json_get(json_get(root, "g"), "sky");
    if (gsky && json_len(gsky) == 3)
    {
        for (int i = 0; i < 3; ++i)
            f->sky[i] = (float)jdouble_or(json_at(gsky, i), 0.0);
    }
    else
    {
        int skytemp = ji(json_get(wo, "skytemp"));
        float a0 = ((float)(wt % 24000) + pt) / 24000.0f - 0.25f;
        if (a0 < 0.0f) a0 += 1.0f;
        if (a0 > 1.0f) a0 -= 1.0f;
        float a1 = a0;
        a0 = 1.0f - (float)((fd_cos((double)a0 * 3.141592653589793) + 1.0) / 2.0);
        a0 = a1 + (a0 - a1) / 3.0f;
        float daylight = mh_cos(a0 * 3.1415927f * 2.0f) * 2.0f + 0.5f;
        if (daylight < 0.0f) daylight = 0.0f;
        if (daylight > 1.0f) daylight = 1.0f;
        for (int i = 0; i < 3; ++i)
            f->sky[i] = ((skytemp >> (16 - 8*i)) & 255) / 255.0f * daylight;
    }
    f->sky_bottom[0] = f->sky[0] * 0.2f + 0.04f;
    f->sky_bottom[1] = f->sky[1] * 0.2f + 0.04f;
    f->sky_bottom[2] = f->sky[2] * 0.6f + 0.1f;
    f->sky_floor = -(float)(jd(json_get(json_get(root, "pl"), "y")) - 63.0);
    f->sky_far = ji(json_get(json_get(root, "opt"), "rd")) * 16.0f;
    const struct jval *cloudopt = json_get(json_get(root, "opt"), "clouds");
    f->clouds_enabled = cloudopt ? ji(cloudopt) : 1;
    f->cloud_tick = ji(json_get(wo, "cloudTick"));
    f->moon_phase = (int)((wt / 24000 % 8 + 8) % 8);
    f->celestial_angle = jf(json_get(json_get(root, "g"), "ang"));
    /* renderSky: getStarBrightness(pt) * (1 - getRainStrength(pt)) */
    {
        float rain = jf_opt(wo, "prain", 0.0f), rnow = jf_opt(wo, "rain", 0.0f);
        rain += (rnow - rain) * pt;
        f->star = jf_opt(json_get(root, "g"), "star", 0.0f) * (1.0f - rain);
    }
    a = json_get(json_get(root, "g"), "cloud");
    for (int i = 0; i < 3; ++i) f->cloud_color[i] = (float)jd(json_at(a, i));
    a = json_get(json_get(root, "g"), "rise");
    if (json_len(a) == 4)
        for (int i = 0; i < 4; ++i) f->sunrise[i] = jf(json_at(a, i));
    f->cloud_height = 128.0f - (float)jd(json_get(json_get(root, "pl"), "y")) + 0.33f;
    json_free(root);
    path(p, sizeof p, dir, "atlas.json");
    root = readjson(p);
    raster_texture_load(&f->texture, dir, ji(json_get(root, "atlas_width")),
                        ji(json_get(root, "atlas_height")),
                        ji(json_get(root, "mip_levels")),
                        ji(json_get(root, "min_filter")));
    json_free(root);
    FILE *file;
    if (f->dimension == 1) {
        f->end_sky = alloc(128 * 128 * 3);
        file = fopen("out/assets/end_sky.rgb", "rb");
        if (!file) file = fopen("../out/assets/end_sky.rgb", "rb");
        if (!file || fread(f->end_sky, 1, 128 * 128 * 3, file) != 128 * 128 * 3) {
            fprintf(stderr, "raster: end sky texture read failed\n"); exit(1);
        }
        fclose(file);
    }
    f->sun = alloc(32 * 32 * 4);
    path(p, sizeof p, dir, "sun.rgba");
    file = fopen(p, "rb");
    if (!file || fread(f->sun, 1, 32 * 32 * 4, file) != 32 * 32 * 4) {
        fprintf(stderr, "raster: sun texture read failed\n"); exit(1);
    }
    fclose(file);
    f->moon = alloc(128 * 64 * 4);
    path(p, sizeof p, dir, "moon_phases.rgba");
    file = fopen(p, "rb");
    if (!file || fread(f->moon, 1, 128 * 64 * 4, file) != 128 * 64 * 4) {
        fprintf(stderr, "raster: moon texture read failed\n"); exit(1);
    }
    fclose(file);
    f->clouds = alloc(256 * 256 * 4);
    path(p, sizeof p, dir, "clouds.rgba");
    file = fopen(p, "rb");
    if (!file || fread(f->clouds, 1, 256 * 256 * 4, file) != 256 * 256 * 4) {
        fprintf(stderr, "raster: cloud texture read failed\n"); exit(1);
    }
    fclose(file);
    size_t n = (size_t)f->w * f->h;
    f->rgb = calloc(n, 3);
    f->mask = calloc(n, 1);
    f->depth = alloc(n * sizeof(float));
    if (!f->rgb || !f->mask) exit(1);
    for (size_t i = 0; i < n; ++i) {
        f->depth[i] = 1.0f;
        for (int c = 0; c < 3; ++c) f->rgb[i * 3 + c] = byte(f->fog[c]);
    }
}

static float edge(const struct vertex *a, const struct vertex *b, float x, float y)
{
    return (x - a->x) * (b->y - a->y) - (y - a->y) * (b->x - a->x);
}

static int top_left(const struct vertex *a, const struct vertex *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    return dy < 0 || (dy == 0 && dx > 0);
}

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static unsigned char byte(float x)
{
    x = clamp01(x);
    return (unsigned char)lrintf(x * 255.0f);
}

static void light_sample(const struct frame *f, float block, float sky, float out[3])
{
    float x = fmaxf(0, fminf(15, block / 16.0f));
    float y = fmaxf(0, fminf(15, sky / 16.0f));
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    uint32_t c[4] = {f->lm[y0*16+x0], f->lm[y0*16+x1],
                     f->lm[y1*16+x0], f->lm[y1*16+x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2-i);
        float a = ((c[0] >> shift) & 255) * (1-fx) + ((c[1] >> shift) & 255) * fx;
        float b = ((c[2] >> shift) & 255) * (1-fx) + ((c[3] >> shift) & 255) * fx;
        out[i] = (a * (1-fy) + b * fy) / 255.0f;
    }
}

/* raster_prec.h RP_LIGHT: light_sample in float16 (the coordinates
 * already are) */
static void light_sample_h(const struct frame *f, float block, float sky, float out[3])
{
    float x = fmaxf(0, fminf(15, rp_h(block * 0.0625f)));
    float y = fmaxf(0, fminf(15, rp_h(sky * 0.0625f)));
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
    float fx = rp_h(x - x0), fy = rp_h(y - y0), gx = rp_h(1 - fx), gy = rp_h(1 - fy);
    uint32_t c[4] = {f->lm[y0*16+x0], f->lm[y0*16+x1],
                     f->lm[y1*16+x0], f->lm[y1*16+x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2-i);
        float a = rp_h(rp_h((float)((c[0] >> shift) & 255) * gx) + rp_h((float)((c[1] >> shift) & 255) * fx));
        float b = rp_h(rp_h((float)((c[2] >> shift) & 255) * gx) + rp_h((float)((c[3] >> shift) & 255) * fx));
        out[i] = rp_h(rp_h(rp_h(a * gy) + rp_h(b * fy)) / 255.0f);
    }
}

/* A terrain pixel's shading with some of raster_prec.h's float16 stages
 * (P: RP_LIGHT, RP_COLOR, RP_FOG, RP_SHADE): what triangle() does from the
 * perspective weights Q and the texel on, each stage's operations in
 * float16 when it is fast. 0: the alpha test drops the pixel. */
static int shade_h(const struct frame *f, int p, const struct vertex *v, const float q[3], float distance,
                   const float tex[4], float src[3], float *alpha_out)
{
    float col[4] = {0}, lightcoord[2] = {0}, light[3];
    if (p & RP_COLOR) {
        float qh[3] = {rp_h(q[0]), rp_h(q[1]), rp_h(q[2])};
        for (int c = 0; c < 4; ++c) {
            float a = rp_h(qh[0] * rp_h(v[0].color[c]));
            a = rp_h(a + rp_h(qh[1] * rp_h(v[1].color[c])));
            col[c] = rp_h(a + rp_h(qh[2] * rp_h(v[2].color[c])));
        }
    } else
        for (int i = 0; i < 3; ++i) for (int c = 0; c < 4; ++c) col[c] += q[i] * v[i].color[c];
    if (p & RP_LIGHT) {
        float qh[3] = {rp_h(q[0]), rp_h(q[1]), rp_h(q[2])};
        for (int c = 0; c < 2; ++c) {
            float a = rp_h(qh[0] * rp_h(v[0].light[c]));
            a = rp_h(a + rp_h(qh[1] * rp_h(v[1].light[c])));
            lightcoord[c] = rp_h(a + rp_h(qh[2] * rp_h(v[2].light[c])));
        }
        light_sample_h(f, lightcoord[0], lightcoord[1], light);
    } else {
        for (int i = 0; i < 3; ++i) for (int c = 0; c < 2; ++c) lightcoord[c] += q[i] * v[i].light[c];
        light_sample(f, lightcoord[0], lightcoord[1], light);
    }
    float alpha = p & RP_SHADE ? rp_h(rp_h(tex[3]) * rp_h(col[3])) : tex[3] * col[3];
    if (alpha <= (f->render_pass ? 0.1f : 0.5f)) return 0;
    *alpha_out = alpha;
    float fog;
    if (f->fogm == 9729)
        fog = clamp01((f->foge - distance) / (f->foge - f->fogs));
    else if (f->fogm == 2048)
        fog = clamp01(expf(-f->fogd * distance));
    else if (f->fogm == 2049) {
        float d = f->fogd * distance;
        fog = clamp01(expf(-d * d));
    } else {
        fprintf(stderr, "raster: unknown fog mode %d\n", f->fogm);
        exit(1);
    }
    for (int c = 0; c < 3; ++c) {
        float value = p & RP_SHADE ? rp_h(rp_h(rp_h(tex[c]) * rp_h(col[c])) * rp_h(light[c]))
                                   : tex[c] * col[c] * light[c];
        if (p & RP_FOG) {
            float fh = rp_h(fog);
            src[c] = rp_h(rp_h(rp_h(value) * fh) + rp_h(rp_h(f->fog[c]) * rp_h(1 - fh)));
        } else
            src[c] = value * fog + f->fog[c] * (1 - fog);
    }
    return 1;
}

static void triangle(struct frame *f, const struct vertex in[3])
{
    struct vertex v[3] = {in[0], in[1], in[2]};
    float area = edge(&v[0], &v[1], v[2].x, v[2].y);
    if (area == 0) return;
    /* Screen y grows downward. GL front faces are CCW with y upward. */
    if (area < 0 && f->sky_mode != 3) return;
    if (area < 0) { struct vertex t = v[1]; v[1] = v[2]; v[2] = t; area = -area; }
    struct vertex s[3] = {v[0], v[1], v[2]};
    for (int i = 0; i < 3; ++i) { s[i].x = v[i].sx; s[i].y = v[i].sy; }
    float minx = fminf(s[0].x, fminf(s[1].x, s[2].x));
    float maxx = fmaxf(s[0].x, fmaxf(s[1].x, s[2].x));
    float miny = fminf(s[0].y, fminf(s[1].y, s[2].y));
    float maxy = fmaxf(s[0].y, fmaxf(s[1].y, s[2].y));
    int x0 = (int)fmaxf(0, floorf(minx));
    int x1 = (int)fminf(f->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny));
    int y1 = (int)fminf(f->h - 1, ceilf(maxy));
    if (f->band) {
        if (y0 < f->band_y0) y0 = f->band_y0;
        if (y1 > f->band_y1) y1 = f->band_y1;
    }
    ++f->triangles;
    /* raster_prec.h RP_RCP: the terrain's weights by reciprocals (the sky's
     * passes stay exact) */
    const int rcp = (f->prec & RP_RCP) && !f->sky_mode;
    float ra = 0, rw[3] = {0, 0, 0};
    if (rcp) {
        ra = 1.0f / area;
        for (int i = 0; i < 3; ++i) rw[i] = 1.0f / v[i].w;
    }
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float sx = x + 0.5f, sy = y + 0.5f;
        float e0 = edge(&s[1], &s[2], sx, sy);
        float e1 = edge(&s[2], &s[0], sx, sy);
        float e2 = edge(&s[0], &s[1], sx, sy);
        if (e0 < 0 || e1 < 0 || e2 < 0) continue;
        if ((e0 == 0 && !top_left(&s[1], &s[2])) ||
            (e1 == 0 && !top_left(&s[2], &s[0])) ||
            (e2 == 0 && !top_left(&s[0], &s[1]))) continue;
        float b[3], bw[3] = {0, 0, 0};
        if (rcp) {
            b[0] = edge(&v[1], &v[2], sx, sy) * ra;
            b[1] = edge(&v[2], &v[0], sx, sy) * ra;
            b[2] = edge(&v[0], &v[1], sx, sy) * ra;
        } else {
            b[0] = edge(&v[1], &v[2], sx, sy) / area;
            b[1] = edge(&v[2], &v[0], sx, sy) / area;
            b[2] = edge(&v[0], &v[1], sx, sy) / area;
        }
        float z = b[0] * v[0].depth + b[1] * v[1].depth + b[2] * v[2].depth;
        size_t k = (size_t)y * f->w + x;
        if (z > f->depth[k]) continue;
        float iw;
        if (rcp) {
            for (int i = 0; i < 3; ++i) bw[i] = b[i] * rw[i];
            iw = bw[0] + bw[1] + bw[2];
        } else
            iw = b[0] / v[0].w + b[1] / v[1].w + b[2] / v[2].w;
        if (iw <= 0) continue;
        if (f->sky_mode) {
            if (f->sky_mode == 6) {
                float q[3] = {b[0] / v[0].w / iw, b[1] / v[1].w / iw,
                              b[2] / v[2].w / iw};
                float alpha = q[0] * v[0].color[3] + q[1] * v[1].color[3] +
                              q[2] * v[2].color[3];
                alpha = byte(alpha) / 255.0f;
                for (int c = 0; c < 3; ++c) {
                    float src = q[0] * v[0].color[c] + q[1] * v[1].color[c] +
                                q[2] * v[2].color[c];
                    float src8 = byte(src) / 255.0f;
                    f->rgb[k * 3 + c] = byte(src8 * alpha +
                                              (f->rgb[k * 3 + c] / 255.0f) * (1.0f - alpha));
                }
                continue;
            }
            if (f->sky_mode == 8) {
                /* the stars: blend (SRC_ALPHA, ONE), no fog, no texture; the
                 * 8-bit product src * alpha / 255, rounded */
                int p = byte(v[0].color[0]) * byte(v[0].color[3]);
                int add = (p + 128 + ((p + 128) >> 8)) >> 8;
                for (int c = 0; c < 3; ++c) {
                    int d = f->rgb[k * 3 + c] + add;
                    f->rgb[k * 3 + c] = (unsigned char)(d > 255 ? 255 : d);
                }
                continue;
            }
            if (f->sky_mode == 4 || f->sky_mode == 5) {
                float u = (b[0] / v[0].w * v[0].u + b[1] / v[1].w * v[1].u +
                           b[2] / v[2].w * v[2].u) / iw;
                float t = (b[0] / v[0].w * v[0].v + b[1] / v[1].w * v[1].v +
                           b[2] / v[2].w * v[2].v) / iw;
                int w = f->sky_mode == 4 ? 32 : 128, h = f->sky_mode == 4 ? 32 : 64;
                const unsigned char *tex0 = f->sky_mode == 4 ? f->sun : f->moon;
                int tx = (int)floorf(u * w), ty = (int)floorf(t * h);
                tx = (tx % w + w) % w; ty = (ty % h + h) % h;
                const unsigned char *tex = tex0 + ((size_t)ty * w + tx) * 4;
                for (int c = 0; c < 3; ++c)
                    f->rgb[k * 3 + c] = byte(f->rgb[k * 3 + c] / 255.0f + tex[c] / 255.0f);
                continue;
            }
            if (f->sky_mode == 7) {
                float u = (b[0] / v[0].w * v[0].u + b[1] / v[1].w * v[1].u +
                           b[2] / v[2].w * v[2].u) / iw;
                float t = (b[0] / v[0].w * v[0].v + b[1] / v[1].w * v[1].v +
                           b[2] / v[2].w * v[2].v) / iw;
                int tx = (int)floorf(u * 128.0f), ty = (int)floorf(t * 128.0f);
                tx = (tx % 128 + 128) % 128;
                ty = (ty % 128 + 128) % 128;
                const unsigned char *tex = f->end_sky + ((size_t)ty * 128 + tx) * 3;
                for (int c = 0; c < 3; ++c)
                    f->rgb[k * 3 + c] = byte((tex[c] / 255.0f) * (40.0f / 255.0f));
                continue;
            }
            float distance = (b[0] / v[0].w * v[0].fogcoord +
                              b[1] / v[1].w * v[1].fogcoord +
                              b[2] / v[2].w * v[2].fogcoord) / iw;
            float fog = f->sky_mode == 1 || f->sky_mode == 2 ?
                clamp01(1.0f - distance / f->sky_far) : 1.0f;
            if (f->sky_mode == 3) {
                float u = (b[0] / v[0].w * v[0].u + b[1] / v[1].w * v[1].u +
                           b[2] / v[2].w * v[2].u) / iw;
                float t = (b[0] / v[0].w * v[0].v + b[1] / v[1].w * v[1].v +
                           b[2] / v[2].w * v[2].v) / iw;
                int tx = (int)floorf(u * 256.0f), ty = (int)floorf(t * 256.0f);
                tx = (tx % 256 + 256) % 256;
                ty = (ty % 256 + 256) % 256;
                const unsigned char *tex = f->clouds + ((size_t)ty * 256 + tx) * 4;
                if (tex[3] < 32) continue;
                if (f->cloud_prepass) {
                    f->depth[k] = z;
                    continue;
                }
                fog = clamp01((f->sky_far - distance) / (f->sky_far * 0.25f));
                for (int c = 0; c < 3; ++c) {
                    float col = (tex[c] / 255.0f) * f->cloud_color[c] * v[0].color[0];
                    float src = col * fog + f->fog[c] * (1.0f - fog);
                    float src8 = byte(src) / 255.0f;
                    f->rgb[k * 3 + c] = byte(src8 * 0.8f +
                                              (f->rgb[k * 3 + c] / 255.0f) * 0.2f);
                }
                continue;
            }
            const float *base = f->sky_mode == 1 ? f->sky : f->sky_bottom;
            for (int c = 0; c < 3; ++c)
                f->rgb[k * 3 + c] = byte(base[c] * fog + f->fog[c] * (1.0f - fog));
            continue;
        }
        float q[3], distance;
        if (rcp) {
            float ri = 1.0f / iw;
            for (int i = 0; i < 3; ++i) q[i] = bw[i] * ri;
            distance = (bw[0] * v[0].fogcoord + bw[1] * v[1].fogcoord + bw[2] * v[2].fogcoord) * ri;
        } else {
            q[0] = b[0] / v[0].w / iw; q[1] = b[1] / v[1].w / iw; q[2] = b[2] / v[2].w / iw;
            distance = (b[0] / v[0].w * v[0].fogcoord +
                        b[1] / v[1].w * v[1].fogcoord +
                        b[2] / v[2].w * v[2].fogcoord) / iw;
        }
        float u = q[0] * v[0].u + q[1] * v[1].u + q[2] * v[2].u;
        float t = q[0] * v[0].v + q[1] * v[1].v + q[2] * v[2].v;
        float ux = u, vx = t, uy = u, vy = t;
        if (f->texture.levels && f->texture.min_filter == 9986) {
            float bx[3] = {edge(&v[1], &v[2], sx + 1, sy) / area,
                           edge(&v[2], &v[0], sx + 1, sy) / area,
                           edge(&v[0], &v[1], sx + 1, sy) / area};
            float by[3] = {edge(&v[1], &v[2], sx, sy + 1) / area,
                           edge(&v[2], &v[0], sx, sy + 1) / area,
                           edge(&v[0], &v[1], sx, sy + 1) / area};
            float wx = bx[0] / v[0].w + bx[1] / v[1].w + bx[2] / v[2].w;
            float wy = by[0] / v[0].w + by[1] / v[1].w + by[2] / v[2].w;
            ux = (bx[0] / v[0].w * v[0].u + bx[1] / v[1].w * v[1].u +
                  bx[2] / v[2].w * v[2].u) / wx;
            vx = (bx[0] / v[0].w * v[0].v + bx[1] / v[1].w * v[1].v +
                  bx[2] / v[2].w * v[2].v) / wx;
            uy = (by[0] / v[0].w * v[0].u + by[1] / v[1].w * v[1].u +
                  by[2] / v[2].w * v[2].u) / wy;
            vy = (by[0] / v[0].w * v[0].v + by[1] / v[1].w * v[1].v +
                  by[2] / v[2].w * v[2].v) / wy;
        }
        float tex[4];
        raster_texture_sample(&f->texture, u, t, ux, vx, uy, vy, tex);
        if (f->prec & RP_HALF) {
            float src[3], alpha;
            if (!shade_h(f, f->prec, v, q, distance, tex, src, &alpha)) continue;
            for (int c = 0; c < 3; ++c) {
                if (f->render_pass) {
                    float src8 = byte(src[c]) / 255.0f;
                    float alpha8 = byte(alpha) / 255.0f;
                    f->rgb[k * 3 + c] = byte(src8 * alpha8 +
                                               (f->rgb[k * 3 + c] / 255.0f) * (1.0f - alpha8));
                } else {
                    f->rgb[k * 3 + c] = byte(src[c]);
                }
            }
            if (!f->render_pass) {
                f->depth[k] = z;
                f->mask[k] = 255;
            }
            ++f->samples;
            continue;
        }
        float col[4] = {0};
        float lightcoord[2] = {0}, light[3];
        for (int i = 0; i < 3; ++i) {
            for (int c = 0; c < 4; ++c) col[c] += q[i] * v[i].color[c];
            for (int c = 0; c < 2; ++c) lightcoord[c] += q[i] * v[i].light[c];
        }
        light_sample(f, lightcoord[0], lightcoord[1], light);
        /* EntityRenderer lowers the alpha test for the translucent pass. */
        float alpha = tex[3] * col[3];
        if (alpha <= (f->render_pass ? 0.1f : 0.5f)) continue;
        float fog;
        if (f->fogm == 9729)
            fog = clamp01((f->foge - distance) / (f->foge - f->fogs));
        else if (f->fogm == 2048)
            fog = clamp01(expf(-f->fogd * distance));
        else if (f->fogm == 2049) {
            float d = f->fogd * distance;
            fog = clamp01(expf(-d * d));
        } else {
            fprintf(stderr, "raster: unknown fog mode %d\n", f->fogm);
            exit(1);
        }
        for (int c = 0; c < 3; ++c) {
            float value = tex[c] * col[c] * light[c];
            float src = value * fog + f->fog[c] * (1 - fog);
            if (f->render_pass) {
                float src8 = byte(src) / 255.0f;
                float alpha8 = byte(alpha) / 255.0f;
                f->rgb[k * 3 + c] = byte(src8 * alpha8 +
                                           (f->rgb[k * 3 + c] / 255.0f) * (1.0f - alpha8));
            } else {
                f->rgb[k * 3 + c] = byte(src);
            }
        }
        if (!f->render_pass) {
            f->depth[k] = z;
            f->mask[k] = 255;
        }
        ++f->samples;
    }
}

static void screen_vertex(struct frame *f, struct vertex *v)
{
    v->w = v->clip[3];
    v->x = (v->clip[0] / v->w * 0.5f + 0.5f) * f->w;
    v->y = (0.5f - v->clip[1] / v->w * 0.5f) * f->h;
    v->sx = rintf((v->x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v->sy = rintf((v->y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v->depth = v->clip[2] / v->w * 0.5f + 0.5f;
}

static struct vertex interpolate(const struct vertex *a, const struct vertex *b, float t)
{
    struct vertex v = *a;
    for (int i = 0; i < 4; ++i) {
        v.clip[i] = a->clip[i] + t * (b->clip[i] - a->clip[i]);
        v.color[i] = a->color[i] + t * (b->color[i] - a->color[i]);
    }
    for (int i = 0; i < 2; ++i)
        v.light[i] = a->light[i] + t * (b->light[i] - a->light[i]);
    v.fogcoord = a->fogcoord + t * (b->fogcoord - a->fogcoord);
    v.u = a->u + t * (b->u - a->u);
    v.v = a->v + t * (b->v - a->v);
    return v;
}

static void live_emit(struct frame *f, const struct vertex v[3]);

static void clip_triangle(struct frame *f, const struct vertex in[3])
{
    /* Fast path: all three vertices inside every plane, straight to the
     * screen. The clip loop with n==3 copies vertices but interpolates
     * nothing, so the output is the same three vertices (b[] holds the same
     * floats a[] held, in the same order). */
    {
        const struct vertex *p = in;
        int inside = 1;
        for (int plane = 0; plane < 6 && inside; ++plane) {
            int axis = plane / 2;
            float sign = plane & 1 ? -1.0f : 1.0f;
            for (int i = 0; i < 3; ++i)
                if (!(p[i].clip[3] - sign * p[i].clip[axis] >= 0)) { inside = 0; break; }
        }
        if (inside) {
            struct vertex a[3] = {in[0], in[1], in[2]};
            for (int i = 0; i < 3; ++i) screen_vertex(f, &a[i]);
            if (f->sink) live_emit(f, a);
            else triangle(f, a);
            return;
        }
    }
    struct vertex a[16], b[16];
    memcpy(a, in, 3 * sizeof(*a));
    int n = 3;
    /* OpenGL clip planes: -w <= x,y,z <= w. */
    for (int plane = 0; plane < 6; ++plane) {
        int m = 0, axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i) {
            const struct vertex *p = &a[i], *q = &a[(i+1)%n];
            float dp = p->clip[3] - sign * p->clip[axis];
            float dq = q->clip[3] - sign * q->clip[axis];
            if (dp >= 0) b[m++] = *p;
            if ((dp >= 0) != (dq >= 0)) b[m++] = interpolate(p, q, dp / (dp - dq));
        }
        n = m;
        if (n < 3) return;
        memcpy(a, b, n * sizeof(*a));
    }
    for (int i = 0; i < n; ++i) screen_vertex(f, &a[i]);
    for (int i = 1; i + 1 < n; ++i) {
        struct vertex t[3] = {a[0], a[i], a[i+1]};
        if (f->sink) live_emit(f, t);
        else triangle(f, t);
    }
}

static struct vertex sky_vertex(const struct frame *f, float x, float y, float z)
{
    struct vertex v = {0};
    float pos[4] = {x, y, z, 1.0f};
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = f->mv[r] * pos[0] + f->mv[4+r] * pos[1] +
                 f->mv[8+r] * pos[2] + f->mv[12+r];
    for (int r = 0; r < 4; ++r)
        v.clip[r] = f->proj[r] * eye[0] + f->proj[4+r] * eye[1] +
                    f->proj[8+r] * eye[2] + f->proj[12+r] * eye[3];
    v.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return v;
}

static void draw_sky(struct frame *f, int pass)
{
    f->sky_mode = pass;
    float y = pass == 1 ? 16.0f : f->sky_floor;
    for (int x = -384; x <= 384; x += 64) {
        for (int z = -384; z <= 384; z += 64) {
            struct vertex q[4];
            if (pass == 1) {
                q[0] = sky_vertex(f, (float)x, y, (float)z);
                q[1] = sky_vertex(f, (float)(x+64), y, (float)z);
                q[2] = sky_vertex(f, (float)(x+64), y, (float)(z+64));
                q[3] = sky_vertex(f, (float)x, y, (float)(z+64));
            } else {
                q[0] = sky_vertex(f, (float)(x+64), y, (float)z);
                q[1] = sky_vertex(f, (float)x, y, (float)z);
                q[2] = sky_vertex(f, (float)x, y, (float)(z+64));
                q[3] = sky_vertex(f, (float)(x+64), y, (float)(z+64));
            }
            struct vertex a[3] = {q[0], q[1], q[3]};
            struct vertex b[3] = {q[1], q[2], q[3]};
            clip_triangle(f, a);
            clip_triangle(f, b);
        }
    }
    f->sky_mode = 0;
}

static void draw_end_sky(struct frame *f)
{
    f->sky_mode = 7;
    for (int side = 0; side < 6; ++side) {
        const float p[4][3] = {{-100,-100,-100}, {-100,-100,100},
                               {100,-100,100}, {100,-100,-100}};
        const float uv[4][2] = {{0,0}, {0,16}, {16,16}, {16,0}};
        struct vertex q[4];
        for (int i = 0; i < 4; ++i) {
            float x = p[i][0], y = p[i][1], z = p[i][2], a;
            if (side == 1) { a = y; y = -z; z = a; }
            if (side == 2) { a = y; y = z; z = -a; }
            if (side == 3) { y = -y; z = -z; }
            if (side == 4) { a = x; x = -y; y = a; }
            if (side == 5) { a = x; x = y; y = -a; }
            q[i] = sky_vertex(f, x, y, z);
            q[i].u = uv[i][0];
            q[i].v = uv[i][1];
        }
        struct vertex a[3] = {q[0], q[1], q[3]};
        struct vertex b[3] = {q[1], q[2], q[3]};
        clip_triangle(f, a);
        clip_triangle(f, b);
    }
    f->sky_mode = 0;
}

/* the sunrise fan's side, its centre colour and its rim's sine and cosine */
static float sunrise_dir(const struct frame *f)
{
    return mh_sin(f->celestial_angle * 6.2831855f) < 0.0f ? 1.0f : -1.0f;
}

static float sunrise_center(const struct frame *f, int c)
{
    return (float)(int)(f->sunrise[c] * 255.0f) / 255.0f;
}

static void sunrise_rim(int i, float *s, float *c)
{
    float a = (float)i * 6.2831855f / 16.0f;
    *s = mh_sin(a);
    *c = mh_cos(a);
}

static void draw_sunrise(struct frame *f)
{
    if (f->sunrise[3] <= 0.0f) return;
    float dir = sunrise_dir(f);
    struct vertex center = sky_vertex(f, dir * 100.0f, 0.0f, 0.0f), prev = {0};
    for (int c = 0; c < 4; ++c) center.color[c] = sunrise_center(f, c);
    f->sky_mode = 6;
    for (int i = 0; i <= 16; ++i) {
        float s, c;
        sunrise_rim(i, &s, &c);
        struct vertex v = sky_vertex(f, dir * c * 120.0f,
                                     c * 40.0f * f->sunrise[3], -dir * s * 120.0f);
        for (int j = 0; j < 3; ++j) v.color[j] = center.color[j];
        if (i > 0) {
            struct vertex t[3] = {center, prev, v};
            clip_triangle(f, t);
        }
        prev = v;
    }
    f->sky_mode = 0;
}

static void celestial_quad(void *ctx, int mode, const struct raster_sky2_vertex vertices[4])
{
    struct frame *f = ctx;
    struct vertex q[4];
    for (int i = 0; i < 4; ++i) {
        q[i] = sky_vertex(f, vertices[i].x, vertices[i].y, vertices[i].z);
        q[i].u = vertices[i].u;
        q[i].v = vertices[i].v;
        /* a star: glColor4f(b, b, b, b), untextured */
        if (mode == 8) q[i].color[0] = q[i].color[1] = q[i].color[2] = q[i].color[3] = f->star;
    }
    struct vertex tri0[3] = {q[0],q[1],q[3]};
    struct vertex tri1[3] = {q[1],q[2],q[3]};
    f->sky_mode = mode;
    clip_triangle(f, tri0);
    clip_triangle(f, tri1);
    f->sky_mode = 0;
}

struct cloud_point { float x, y, z, u, v; };

static void cloud_quad(struct frame *f, const struct cloud_point p[4], float shade)
{
    struct vertex q[4];
    for (int i = 0; i < 4; ++i) {
        q[i] = sky_vertex(f, p[i].x * 12.0f, p[i].y, p[i].z * 12.0f);
        q[i].u = p[i].u;
        q[i].v = p[i].v;
        q[i].color[0] = shade;
    }
    struct vertex a[3] = {q[0], q[1], q[3]};
    struct vertex b[3] = {q[1], q[2], q[3]};
    clip_triangle(f, a);
    clip_triangle(f, b);
}

/* renderCloudsFancy's texture origin and the camera's place in its cell */
static void cloud_origin(const struct frame *f, float *ix, float *iz, float *fx, float *fz)
{
    double x = (f->cam[0] + ((float)f->cloud_tick + 1.0f) * 0.029999999329447746) / 12.0;
    double z = f->cam[2] / 12.0 + 0.33000001311302185;
    x -= floor(x / 2048.0) * 2048.0;
    z -= floor(z / 2048.0) * 2048.0;
    *ix = (float)floor(x) * 0.00390625f;
    *iz = (float)floor(z) * 0.00390625f;
    *fx = (float)(x - floor(x));
    *fz = (float)(z - floor(z));
}

static void draw_clouds(struct frame *f)
{
    float ix, iz, fx, fz;
    cloud_origin(f, &ix, &iz, &fx, &fz);
    f->sky_mode = 3;
    for (int pass = 0; pass < 2; ++pass) {
        f->cloud_prepass = pass == 0;
        for (int xi = -3; xi <= 4; ++xi) for (int zi = -3; zi <= 4; ++zi) {
        float xx = (float)(xi * 8), zz = (float)(zi * 8);
        float x0 = xx - fx, x1 = xx + 8.0f - fx;
        float z0 = zz - fz, z1 = zz + 8.0f - fz;
        float u0 = xx * 0.00390625f + ix, u1 = (xx + 8.0f) * 0.00390625f + ix;
        float v0 = zz * 0.00390625f + iz, v1 = (zz + 8.0f) * 0.00390625f + iz;
        float y0 = f->cloud_height, y1 = y0 + 4.0f;
        if (y0 > -5.0f) {
            const struct cloud_point q[4] = {
                {x0,y0,z1,u0,v1}, {x1,y0,z1,u1,v1},
                {x1,y0,z0,u1,v0}, {x0,y0,z0,u0,v0}
            };
            cloud_quad(f, q, 0.7f);
        }
        if (y0 <= 5.0f) {
            const struct cloud_point q[4] = {
                {x0,y1-0.0009765625f,z1,u0,v1}, {x1,y1-0.0009765625f,z1,u1,v1},
                {x1,y1-0.0009765625f,z0,u1,v0}, {x0,y1-0.0009765625f,z0,u0,v0}
            };
            cloud_quad(f, q, 1.0f);
        }
        for (int unit = 0; unit < 8; ++unit) {
            float u = (xx + (float)unit + 0.5f) * 0.00390625f + ix;
            float v = (zz + (float)unit + 0.5f) * 0.00390625f + iz;
            if (xi > -1) {
                float side = x0 + (float)unit;
                const struct cloud_point q[4] = {
                    {side,y0,z1,u,v1}, {side,y1,z1,u,v1},
                    {side,y1,z0,u,v0}, {side,y0,z0,u,v0}
                };
                cloud_quad(f, q, 0.9f);
            }
            if (xi <= 1) {
                float side = x0 + (float)unit + 1.0f - 0.0009765625f;
                const struct cloud_point q[4] = {
                    {side,y0,z1,u,v1}, {side,y1,z1,u,v1},
                    {side,y1,z0,u,v0}, {side,y0,z0,u,v0}
                };
                cloud_quad(f, q, 0.9f);
            }
            if (zi > -1) {
                float side = z0 + (float)unit;
                const struct cloud_point q[4] = {
                    {x0,y1,side,u0,v}, {x1,y1,side,u1,v},
                    {x1,y0,side,u1,v}, {x0,y0,side,u0,v}
                };
                cloud_quad(f, q, 0.8f);
            }
            if (zi <= 1) {
                float side = z0 + (float)unit + 1.0f - 0.0009765625f;
                const struct cloud_point q[4] = {
                    {x0,y1,side,u0,v}, {x1,y1,side,u1,v},
                    {x1,y0,side,u1,v}, {x0,y0,side,u0,v}
                };
                cloud_quad(f, q, 0.8f);
            }
        }
    }
    }
    f->sky_mode = 0;
}

static struct vertex make_vertex(struct frame *f, const uint32_t raw[8], int cx, int s, int cz, int flags)
{
    struct vertex v = {0};
    int bx = cx * 16, by = s * 16, bz = cz * 16;
    float pos[4] = {
        (float)((double)(bx - (bx & 1023)) - f->cam[0]) + (float)(bx & 1023) +
            (fbits(raw[0]) - 8.0f) * 1.000001f + 8.0f,
        (float)((double)by - f->cam[1]) + (fbits(raw[1]) - 8.0f) * 1.000001f + 8.0f,
        (float)((double)(bz - (bz & 1023)) - f->cam[2]) + (float)(bz & 1023) +
            (fbits(raw[2]) - 8.0f) * 1.000001f + 8.0f, 1};
    if (f->prec & RP_XFORM) {
        /* raster_prec.h: the camera offset in float */
        pos[0] = ((float)(bx - (bx & 1023)) - (float)f->cam[0]) + (float)(bx & 1023) +
                 (fbits(raw[0]) - 8.0f) * 1.000001f + 8.0f;
        pos[1] = ((float)by - (float)f->cam[1]) + (fbits(raw[1]) - 8.0f) * 1.000001f + 8.0f;
        pos[2] = ((float)(bz - (bz & 1023)) - (float)f->cam[2]) + (float)(bz & 1023) +
                 (fbits(raw[2]) - 8.0f) * 1.000001f + 8.0f;
    }
    float eye[4], clip[4];
    for (int r = 0; r < 4; ++r) {
        eye[r] = f->mv[r] * pos[0] + f->mv[4+r] * pos[1] +
                 f->mv[8+r] * pos[2] + f->mv[12+r] * pos[3];
    }
    for (int r = 0; r < 4; ++r) {
        clip[r] = f->proj[r] * eye[0] + f->proj[4+r] * eye[1] +
                  f->proj[8+r] * eye[2] + f->proj[12+r] * eye[3];
    }
    memcpy(v.clip, clip, sizeof clip);
    /* llvmpipe exposes GL_NV_fog_distance; setupFog selects eye radial distance. */
    v.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    v.u = fbits(raw[3]); v.v = fbits(raw[4]);
    uint32_t c = flags & 2 ? raw[5] : 0xffffffffU;
    for (int i = 0; i < 4; ++i) v.color[i] = ((c >> (8 * i)) & 255) / 255.0f;
    v.light[0] = flags & 4 ? (float)(raw[7] & 65535) : 240;
    v.light[1] = flags & 4 ? (float)(raw[7] >> 16) : 240;
    return v;
}

struct water_section { int cx, cz, s; double distance; };
struct water_quad { int offset; float distance; };

static int water_section_compare(const void *a0, const void *b0)
{
    const struct water_section *a = a0, *b = b0;
    if (a->distance != b->distance) return a->distance < b->distance ? 1 : -1;
    return 0;
}

static int water_quad_compare(const void *a0, const void *b0)
{
    const struct water_quad *a = a0, *b = b0;
    if (a->distance != b->distance) return a->distance < b->distance ? 1 : -1;
    return a->offset - b->offset;
}

static float water_quad_distance(const struct rb_tess *t, int offset,
                                 float x, float y, float z)
{
    float sum[3] = {0};
    for (int i = 0; i < 4; ++i) {
        const int32_t *raw = t->raw + (size_t)(offset + i) * 8;
        sum[0] += fbits((uint32_t)raw[0]) - x;
        sum[1] += fbits((uint32_t)raw[1]) - y;
        sum[2] += fbits((uint32_t)raw[2]) - z;
    }
    float dx = sum[0] * 0.25f, dy = sum[1] * 0.25f, dz = sum[2] * 0.25f;
    return dx * dx + dy * dy + dz * dz;
}

/* The inside draw of the section about to be meshed: its entry in the row's
 * list, or none (a row without the list keeps the mesh dump's block). */
static void inside_for(const struct frame *f, struct rb_mesher *m, int cx, int s, int cz)
{
    if (!f->has_inside) return;
    m->px = m->py = m->pz = -1000000;
    for (int i = 0; i < f->n_inside; ++i)
        if (f->inside[i][0] >> 4 == cx && f->inside[i][1] >> 4 == s && f->inside[i][2] >> 4 == cz) {
            m->px = f->inside[i][0]; m->py = f->inside[i][1]; m->pz = f->inside[i][2];
        }
}

static void draw_section(struct frame *f, struct rb_mesher *m,
                         int cx, int s, int cz, int pass)
{
    inside_for(f, m, cx, s, cz);
    rb_mesh_section(m, cx, cz, s, pass);
    struct rb_tess *t = m->t;
    if (t->dropped) { fprintf(stderr, "raster: mesher vertex buffer full\n"); exit(1); }
    int flags = (t->has_texture ? 1 : 0) | (t->has_color ? 2 : 0) |
                (t->has_brightness ? 4 : 0) | (t->has_normals ? 8 : 0);
    int count = t->vertex_count / 4;
    struct water_quad *order = NULL;
    if (pass && count) {
        order = alloc((size_t)count * sizeof(*order));
        float x = (float)f->cam[0] - cx * 16;
        float y = (float)f->cam[1] - s * 16;
        float z = (float)f->cam[2] - cz * 16;
        for (int i = 0; i < count; ++i) {
            order[i].offset = i * 4;
            order[i].distance = water_quad_distance(t, i * 4, x, y, z);
        }
        qsort(order, (size_t)count, sizeof(*order), water_quad_compare);
    }
    for (int i = 0; i < count; ++i) {
        int offset = order ? order[i].offset : i * 4;
        struct vertex quad[4];
        for (int j = 0; j < 4; ++j) {
            uint32_t raw[8];
            for (int k = 0; k < 8; ++k)
                raw[k] = (uint32_t)t->raw[(size_t)(offset + j) * 8 + k];
            quad[j] = make_vertex(f, raw, cx, s, cz, flags);
        }
        struct vertex a[3] = {quad[0], quad[1], quad[3]};
        struct vertex b[3] = {quad[1], quad[2], quad[3]};
        clip_triangle(f, a);
        clip_triangle(f, b);
    }
    free(order);
}

/* The textures the weather and the overlays draw with, from the scene's own
 * state directory (the block atlas is the full dump, so the fire sprite and the
 * in-block icon come out of it by the recorded uv). */
struct worldfx_tex
{
    unsigned char *rain, *snow, *underwater, *atlas;
    int atlas_w, atlas_h, underwater_w, underwater_h;
    int rain_w, rain_h, snow_w, snow_h;
    float fire[4];
};

/* One dumped texture's size, out of the scene's state/gui.json; 0 when the
 * recording predates the entry. */
static int gui_texture_size(const struct jval *meta, const char *name, int *w, int *h)
{
    const struct jval *t = json_get(meta, name);
    int64_t v;
    if (!t || !json_int(json_get(t, "w"), &v)) return 0;
    *w = (int)v;
    if (!json_int(json_get(t, "h"), &v)) return 0;
    *h = (int)v;
    return 1;
}

static unsigned char *scene_read(const char *dir, const char *name, size_t n)
{
    char p[1024];
    path(p, sizeof p, dir, name);
    FILE *file = fopen(p, "rb");
    unsigned char *b;
    if (!file) return NULL;
    b = alloc(n);
    if (fread(b, 1, n, file) != n) { free(b); b = NULL; }
    fclose(file);
    return b;
}

/* One sprite's uv range by name, out of the scene's atlas.json. */
static int atlas_sprite_uv(const char *dir, const char *name, float uv[4])
{
    char p[1024];
    path(p, sizeof p, dir, "atlas.json");
    struct jval *root = readjson_opt(p);
    if (!root) return 0;
    const struct jval *sprites = json_get(root, "sprites");
    int found = 0;
    for (int i = 0; i < json_len(sprites); ++i) {
        const struct jval *s = json_at(sprites, i);
        const char *n = json_str(json_get(s, "name"));
        if (!n || strcmp(n, name)) continue;
        /* each uv is the raw float bits in a JSON integer */
        uv[0] = fbits((uint32_t)ji(json_get(s, "minU")));
        uv[1] = fbits((uint32_t)ji(json_get(s, "maxU")));
        uv[2] = fbits((uint32_t)ji(json_get(s, "minV")));
        uv[3] = fbits((uint32_t)ji(json_get(s, "maxV")));
        found = 1;
        break;
    }
    json_free(root);
    return found;
}

static void worldfx_tex_load(struct worldfx_tex *t, const char *dir)
{
    char p[1024];
    memset(t, 0, sizeof *t);
    path(p, sizeof p, dir, "atlas.json");
    struct jval *root = readjson_opt(p);
    if (root) {
        t->atlas_w = ji(json_get(root, "atlas_width"));
        t->atlas_h = ji(json_get(root, "atlas_height"));
        json_free(root);
    }
    if (t->atlas_w > 0 && t->atlas_h > 0)
        t->atlas = scene_read(dir, "atlas.rgba", (size_t)t->atlas_w * t->atlas_h * 4);
    if (t->atlas) texanim_patch_scene(dir, TEXANIM_BLOCKS, t->atlas, t->atlas_w, t->atlas_h);
    /* The environment sheets are 64x256 in 1.7.10 (four 64x64 frames stacked,
     * the v coordinate scrolling through them), so the size comes from the
     * recording, never a guess. */
    path(p, sizeof p, dir, "state/gui.json");
    root = readjson_opt(p);
    t->rain_w = t->rain_h = t->snow_w = t->snow_h = 0;
    t->underwater_w = t->underwater_h = 0;
    if (root) {
        gui_texture_size(root, "rain", &t->rain_w, &t->rain_h);
        gui_texture_size(root, "snow", &t->snow_w, &t->snow_h);
        gui_texture_size(root, "underwater", &t->underwater_w, &t->underwater_h);
        json_free(root);
    }
    if (t->rain_w > 0) t->rain = scene_read(dir, "state/gui_rain.rgba", (size_t)t->rain_w * t->rain_h * 4);
    if (t->snow_w > 0) t->snow = scene_read(dir, "state/gui_snow.rgba", (size_t)t->snow_w * t->snow_h * 4);
    if (t->underwater_w > 0)
        t->underwater = scene_read(dir, "state/gui_underwater.rgba",
                                   (size_t)t->underwater_w * t->underwater_h * 4);
    if (!t->underwater) {
        /* a recording older than the GUI sheet: the hand probe's copy, or the
         * vanilla bytes every recording shares */
        t->underwater_w = t->underwater_h = 16;
        t->underwater = raster_hand_read_rgba(dir, "state/underwater.rgba", "misc/underwater.rgba",
                                              16 * 16 * 4);
    }
    if (!atlas_sprite_uv(dir, "fire_layer_1", t->fire))
        t->fire[0] = t->fire[1] = t->fire[2] = t->fire[3] = 0.0f;
}

static void worldfx_tex_free(struct worldfx_tex *t)
{
    free(t->rain); free(t->snow); free(t->underwater); free(t->atlas);
    memset(t, 0, sizeof *t);
}

/* EntityRenderer.renderRainSnow, renderHand's hand and ItemRenderer.renderOverlays
 * over a recorded scene, in that order: the state row the rest of the frame
 * came from carries every input. */
static void draw_worldfx(struct frame *f, const char *dir, struct rb_mesher *mesher,
                         const struct rb_world *world, const struct rb_table *tab)
{
    char p[1024];
    path(p, sizeof p, dir, "state/frames.jsonl");
    struct jval *root = readjson(p);
    if (!root) { raster_hand_scene(dir, f->rgb, f->w, f->h, f->lm, world, tab); return; }
    const struct jval *pl = json_get(root, "pl");
    const struct jval *wo = json_get(root, "wo");
    const struct jval *vp = json_get(root, "vp");
    const struct jval *opt = json_get(root, "opt");
    struct worldfx_tex tex;
    struct worldfx_in v;

    if (!pl || !wo || !vp || !opt) {
        json_free(root);
        raster_hand_scene(dir, f->rgb, f->w, f->h, f->lm, world, tab);
        return;
    }
    worldfx_tex_load(&tex, dir);

    memset(&v, 0, sizeof v);
    v.w = f->w; v.h = f->h;
    v.proj = f->proj; v.mv = f->mv; v.cam = f->cam;
    v.fog = f->fog; v.fogs = f->fogs; v.foge = f->foge; v.fogd = f->fogd; v.fogm = f->fogm;
    v.lm = f->lm; v.depth = f->depth; v.rgb = f->rgb;
    v.triangles = &f->triangles; v.samples = &f->samples;
    v.world = world; v.mesher = mesher;
    v.rain_tex = tex.rain; v.snow_tex = tex.snow;
    v.atlas = tex.atlas; v.atlas_w = tex.atlas_w; v.atlas_h = tex.atlas_h;
    v.underwater_tex = tex.underwater;
    v.underwater_w = tex.underwater_w; v.underwater_h = tex.underwater_h;
    v.rain_w = tex.rain_w; v.rain_h = tex.rain_h;
    v.snow_w = tex.snow_w; v.snow_h = tex.snow_h;
    v.fire_u0 = tex.fire[0]; v.fire_u1 = tex.fire[1];
    v.fire_v0 = tex.fire[2]; v.fire_v1 = tex.fire[3];

    v.pt = jf_opt(root, "pt", 1.0f);
    v.ruc = ji_opt(json_get(root, "er"), "ruc", 0);
    v.fancy = tab->fancy;
    double px = jd_opt(pl, "px", 0.0), py = jd_opt(pl, "py", 0.0), pz = jd_opt(pl, "pz", 0.0);
    double x = jd_opt(pl, "x", 0.0), y = jd_opt(pl, "y", 0.0), z = jd_opt(pl, "z", 0.0);
    v.eye_x = px + (x - px) * (double)v.pt;
    v.eye_y = py + (y - py) * (double)v.pt;
    v.eye_z = pz + (z - pz) * (double)v.pt;
    v.eye_now_x = x; v.eye_now_y = y; v.eye_now_z = z;
    v.rain = jf_opt(wo, "rain", 0.0f); v.prain = jf_opt(wo, "prain", 0.0f);
    v.mat = ji_opt(vp, "mat", 0);
    v.burning = ji_opt(pl, "burning", 0);
    /* a recording older than the pl fields has the same two numbers in the HUD block */
    const struct jval *hud = json_get(root, "hud");
    v.underwater = json_get(pl, "underwater") ? ji_opt(pl, "underwater", 0)
                                              : ji_opt(json_get(hud, "bar"), "inwater", 0);
    v.brightness = json_get(pl, "brightness") ? jf_opt(pl, "brightness", 0.0f)
                                              : jf_opt(hud, "bright", 0.0f);
    v.yaw = jf_opt(pl, "yaw", 0.0f); v.pitch = jf_opt(pl, "pit", 0.0f);
    v.far = (float)(ji_opt(opt, "rd", 4) * 16);

    struct rs_in rs;
    float hand_mv[16];
    memset(&rs, 0, sizeof rs);
    rs.rd = ji_opt(opt, "rd", 4);
    rs.dw = f->w; rs.dh = f->h;
    rs.pt = v.pt;
    rs.hp = jf_opt(pl, "hp", 20.0f);
    rs.death = ji_opt(pl, "death", 0);
    rs.mat = v.mat;
    renderstate_hand_camera(&rs, v.hand_proj, hand_mv);   /* the overlays take its projection only */

    if (ji_opt(pl, "opaque", 0))
        v.inside_count = worldfx_inside_block(world, mesher, x, y, z, 0.6, 1.8, v.inside_uv);

    worldfx_weather(&v);
    raster_hand_scene(dir, f->rgb, f->w, f->h, f->lm, world, tab);

    /* renderWorld clears the depth buffer before renderHand, so the first-person
     * overlays draw over the whole frame whatever the terrain left behind. */
    for (size_t i = 0; i < (size_t)f->w * (size_t)f->h; ++i) f->depth[i] = 1.0f;
    worldfx_screen(&v);
    worldfx_tex_free(&tex);
    json_free(root);
}

static void draw_mesh(struct frame *f, const char *dir)
{
    int radius, pcx, pcz, px, py, pz;
    struct rb_table tab;
    struct rb_atlas atlas;
    struct rb_world world;
    struct rb_tess tess;
    struct rb_mesher mesher;
    if (rb_manifest_load(dir, &radius, &pcx, &pcz, &px, &py, &pz) != 0 ||
        rb_table_load(&tab, dir) != 0 || rb_atlas_load(&atlas, dir) != 0 ||
        rb_world_load(&world, dir) != 0 || rb_tess_init(&tess, RB_TESS_CAP) != 0)
        exit(1);
    world.origin_cx = pcx;
    world.origin_cz = pcz;
    if (world.margin != radius + 1 ||
        rb_mesher_init(&mesher, &tab, &atlas, &world, &tess, px, py, pz) != 0)
        exit(1);
    for (int cx = pcx - radius; cx <= pcx + radius; ++cx) {
        for (int cz = pcz - radius; cz <= pcz + radius; ++cz) {
            for (int s = 0; s < 16; ++s) {
                draw_section(f, &mesher, cx, s, cz, 0);
            }
        }
    }
    raster_entities_draw(dir, f->w, f->h, f->proj, f->mv, f->cam,
                         f->fog, f->fogs, f->foge, f->fogd, f->fogm,
                         f->lm, f->depth, f->rgb, &f->triangles, &f->samples);
    struct entity_raster_target mobs = {.w = f->w, .h = f->h, .proj = f->proj, .mv = f->mv,
        .cam = f->cam, .fog = f->fog, .fogs = f->fogs, .foge = f->foge, .fogd = f->fogd,
        .fogm = f->fogm, .lm = f->lm, .depth = f->depth, .rgb = f->rgb,
        .triangles = &f->triangles, .samples = &f->samples, .material = {1, 1, 1}};
    raster_mobs_scene(dir, &mobs);
    raster_things_scene(dir, &mobs);
    /* RenderGlobal.renderEntities' block-entity loop, after the entities */
    raster_tileents_scene(dir, &mobs);
    char state_path[1024];
    path(state_path, sizeof state_path, dir, "state/frames.jsonl");
    struct jval *state = readjson(state_path);
    const struct jval *overlay = json_get(state, "overlay");
    if (overlay) {
        struct raster_overlay_in oi = {0};
        oi.w = f->w; oi.h = f->h;
        oi.proj = f->proj; oi.mv = f->mv; oi.depth = f->depth;
        oi.cam = f->cam; oi.rgb = f->rgb; oi.texture = &f->texture;
        oi.mesher = &mesher; oi.atlas = &atlas; oi.stage = -1;
        oi.fog = f->fog; oi.fogs = f->fogs; oi.foge = f->foge; oi.fogd = f->fogd; oi.fogm = f->fogm;
        const struct jval *a = json_get(overlay, "selection");
        if (json_len(a) == 9) {
            oi.selected = 1;
            oi.sx = ji(json_at(a, 0)); oi.sy = ji(json_at(a, 1)); oi.sz = ji(json_at(a, 2));
            for (int i = 0; i < 6; ++i) oi.box[i] = jd(json_at(a, i + 3));
        }
        a = json_get(overlay, "damage");
        if (json_len(a) == 4) {
            oi.dx = ji(json_at(a, 0)); oi.dy = ji(json_at(a, 1));
            oi.dz = ji(json_at(a, 2)); oi.stage = ji(json_at(a, 3));
        }
        raster_overlay_draw(&oi);
    }
    json_free(state);
    /* EffectRenderer.renderLitParticles (layer 3) + renderParticles (layers
     * 0-2): EntityRenderer.renderWorld draws them after the destroy-progress
     * overlay and before the weather, so the pass sits in the same slot. The
     * fx block's presence is the only gate: scenes without it stay
     * byte-identical. */
    raster_particles_scene(dir, f->w, f->h, f->proj, f->mv, f->cam,
                           f->fog, f->fogs, f->foge, f->fogd, f->fogm,
                           f->lm, f->depth, f->rgb, &f->triangles, &f->samples);
    /* sortAndRender(pass 1): EntityRenderer.renderWorld draws the translucent
     * terrain after the entities, the destroy overlay and the particles, with
     * depth writes off, so what lies behind water or a portal is blended over */
    int capacity = (radius * 2 + 1) * (radius * 2 + 1) * 16;
    struct water_section *sections = alloc((size_t)capacity * sizeof(*sections));
    int nsections = 0;
    for (int cx = pcx - radius; cx <= pcx + radius; ++cx)
        for (int cz = pcz - radius; cz <= pcz + radius; ++cz)
            for (int s = 0; s < 16; ++s) {
                inside_for(f, &mesher, cx, s, cz);
                rb_mesh_section(&mesher, cx, cz, s, 1);
                if (!tess.vertex_count) continue;
                double dx = cx * 16 + 8 - f->cam[0];
                double dy = s * 16 + 8 - f->cam[1];
                double dz = cz * 16 + 8 - f->cam[2];
                sections[nsections++] = (struct water_section){cx, cz, s,
                    dx * dx + dy * dy + dz * dz};
            }
    qsort(sections, (size_t)nsections, sizeof(*sections), water_section_compare);
    f->render_pass = 1;
    for (int i = 0; i < nsections; ++i)
        draw_section(f, &mesher, sections[i].cx, sections[i].s, sections[i].cz, 1);
    f->render_pass = 0;
    free(sections);
    draw_worldfx(f, dir, &mesher, &world, &tab);
    rb_mesher_free(&mesher);
    rb_tess_free(&tess);
    rb_world_free(&world);
    rb_atlas_free(&atlas);
    rb_table_free(&tab);
}

static void be32(unsigned char *p, uint32_t n)
{
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}

static void chunk(FILE *f, const char type[4], const unsigned char *data, uint32_t n)
{
    unsigned char b[4];
    be32(b, n); fwrite(b, 1, 4, f); fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t crc = crc32(0, (const Bytef *)type, 4);
    crc = crc32(crc, data, n);
    be32(b, crc); fwrite(b, 1, 4, f);
}

static int png_write(const char *path0, int w, int h, const unsigned char *rgb)
{
    size_t stride = (size_t)w * 3, n = (stride + 1) * h;
    unsigned char *scan = alloc(n);
    for (int y = 0; y < h; ++y) {
        scan[(stride+1)*y] = 0;
        memcpy(scan + (stride+1)*y + 1, rgb + stride*y, stride);
    }
    uLongf bound = compressBound(n);
    unsigned char *z = alloc(bound);
    int ok = compress2(z, &bound, scan, n, Z_BEST_SPEED) == Z_OK;
    FILE *file = fopen(path0, "wb");
    if (!file) ok = 0;
    if (file) {
        static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
        fwrite(sig, 1, 8, file);
        unsigned char ihdr[13] = {0};
        be32(ihdr, w); be32(ihdr+4, h); ihdr[8] = 8; ihdr[9] = 2;
        chunk(file, "IHDR", ihdr, 13);
        chunk(file, "IDAT", z, bound);
        chunk(file, "IEND", NULL, 0);
        ok &= fclose(file) == 0;
    }
    free(scan); free(z);
    return ok;
}

int raster_png(const char *path0, int w, int h, const unsigned char *rgb)
{
    return png_write(path0, w, h, rgb) ? 0 : 1;
}

int raster_render(const char *scene, const char *png, const char *coverage,
                  const char *sky_png)
{
    struct frame f = {0};
    jmath_init();
    load_state(&f, scene);
    if (f.dimension == 1) draw_end_sky(&f);
    else if (f.dimension == 0) {
        draw_sky(&f, 1);
        draw_sunrise(&f);
        raster_sky2_draw(&f, f.celestial_angle, f.moon_phase, celestial_quad);
        if (f.star > 0.0f) raster_sky2_stars(&f, f.celestial_angle, celestial_quad);
        draw_sky(&f, 2);
        if (f.clouds_enabled) draw_clouds(&f);
    }
    int ok = !sky_png || png_write(sky_png, f.w, f.h, f.rgb);
    draw_mesh(&f, scene);
    raster_hud(f.rgb, f.w, f.h, scene);
    ok &= png_write(png, f.w, f.h, f.rgb);
    if (coverage) {
        size_t n = (size_t)f.w * f.h;
        unsigned char *c = alloc(n*3);
        for (size_t i = 0; i < n; ++i) c[i*3] = c[i*3+1] = c[i*3+2] = f.mask[i];
        ok &= png_write(coverage, f.w, f.h, c);
        free(c);
    }
    fprintf(stderr, "raster: %lu triangles, %lu depth-passing samples, %dx%d\n",
            f.triangles, f.samples, f.w, f.h);
    raster_texture_free(&f.texture);
    free(f.clouds); free(f.sun); free(f.moon); free(f.end_sky);
    free(f.rgb); free(f.mask); free(f.depth);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------ live frames
 *
 * The passes above over a live world, for the playable client: the camera, fog
 * and lightmap come from renderstate_compute instead of a RenderStateProbe
 * dump, the chunks from an rb_world the caller keeps current, and each
 * section's quads stay cached until the caller marks the section stale.
 *
 * A frame runs in two parallel stages. First every pass's triangles are
 * transformed and clipped once, in draw order, into lists (clip_triangle's
 * sink), sections spread over the threads. Then the screen splits into row
 * bands the threads take in turn, and each band rasterizes every recorded
 * triangle that reaches its rows, in the recorded order. triangle() evaluates
 * each pixel from absolute edge functions, so a band writes exactly the bytes
 * the whole frame would. A section entirely outside one clip plane is skipped:
 * every triangle of it would clip away. */

/* keyed and key: what the mesh was made from (raster_meshkey.h: MESHKEY_OK
 * and its key, MESHKEY_EMPTY the empty mesh of a pass no cell of the
 * section is drawn in, or MESHKEY_NONE); a stale mark with the same key
 * keeps the mesh */
struct live_section { int meshed, flags, count; uint32_t version; int32_t *raw; int keyed; struct meshkey key; };
struct live_draw { int cx, s, cz; const struct live_section *sec; struct water_quad *order; };

/* One stage-one list: its triangles, the rows each reaches, and the pass state
 * triangle() reads while drawing it. */
struct live_list {
    struct vertex *v;             /* 3 per triangle */
    int16_t *rows;                /* first and last row per triangle */
    uint8_t *mode;                /* sky_mode | cloud_prepass << 4 | render_pass << 5 */
    int n, cap;
    int render_pass;              /* the state the recorder is in */
};

struct live_worker { struct raster_live *L; int id; };
static void *pool_thread(void *arg);

struct raster_live {
    int w, h, threads;
    int prec;                 /* raster_live_set_prec */
    struct raster_texture texture;
    unsigned char *clouds, *sun, *moon, *end_sky;
    /* the world's weather sheets and the block atlas sprite the first-person
     * fire overlay draws (out/assets, or the asset scene's own dumps) */
    unsigned char *rain_tex, *snow_tex, *underwater_tex;
    int rain_w, rain_h, snow_w, snow_h, underwater_w, underwater_h;
    float fire_uv[4];
    unsigned char *block_atlas;
    int block_atlas_w, block_atlas_h;
    /* the particle pass's two sheets: layer 0's particle atlas and layer 3's
     * explosion sheet (the asset scene's state dumps, or out/assets) */
    unsigned char *particle_tex, *explosion_tex;
    const struct rb_table *tab;   /* the process's (rbshare.h) */
    const struct rb_atlas *atlas;
    struct rb_tess *tess;         /* one per thread: the mesh stage runs them all */
    struct rb_mesher *mesher;
    const struct rb_world *world;
    struct live_section *sec;
    /* per window slot, bit s set when section s of it may have a pass not
     * meshed (every write of meshed = 0 sets it; the frame clears a drawn
     * slot's bits once it has seen to its sections), so the frame reads
     * the sections of those alone */
    uint16_t *pend;
    /* the square the last frame dropped the meshes outside of (drop_r -1:
     * none yet, or the window moved since) */
    int drop_cx, drop_cz, drop_r;
    /* the device mesher (raster_live_device_mesh): the mode, the feed of
     * requests and world bytes, its export and the host's meshes of it,
     * last frame's requests (the device's counts come back for them) and
     * the device's resync flag */
    int dmesh;
    struct meshfeed *feed;
    struct meshfeed_out feed_out;
    struct raster_obs_hostmesh *feed_host;
    struct live_feed_prev { int32_t cx, s, cz, pass; uint32_t version; } *feed_prev;
    int32_t *feed_counts;
    unsigned char *feed_keep;
    int nfeed_prev, capfeed;
    int feed_resync, feed_frame;
    char *assets;
    float *depth;
    unsigned char *mask;
    struct live_draw *opaque, *water;
    int nopaque, nwater, capdraw;
    struct live_list sky;         /* the sky passes, recorded on one thread */
    /* check: the kept mesh (1) or the empty one (2) the key gave, meshed
     * again to be compared (raster_live_mesh_check) */
    struct live_stale { int cx, s, cz, pass, check; struct live_section *e; } *stale;
    int nstale, capstale;
    struct live_list *lists;      /* one per draw: opaque then water */
    int caplists;
    struct frame base;
    /* raster_live_obs's view of the last frame */
    struct raster_obs_draw *obs_draws;
    int32_t *obs_order;
    size_t obs_cap, obs_order_cap;
    float *obs_stars;
    int nstars_obs;
    int nanim, anim[RASTER_OBS_ANIM][4];   /* the sprites raster_live_animate patches */
    struct texanim_memo anim_memo[2];      /* the frames it wrote into texture's level 0 and block_atlas (nothing else writes them) */
    int keep;                     /* raster_live_keep: no mesh is dropped */
    int keycheck;                 /* raster_live_mesh_check */
    struct meshkey_cache *mkc;    /* the bands' part of the keys (raster_meshkey.h) */
    uint64_t mkc_bad;             /* its bad count, as far as check_bad holds it */
    uint8_t pass[4096];           /* per block id, the passes the mesher draws it in (rb_block_pass) */
    _Atomic int checked, check_bad;
    int next;                     /* the shared work counter of a stage */
    struct raster_live_stats stats;
    pthread_mutex_t lock;
    /* the persistent pool: threads wait for a stage instead of being created
     * and joined three times a frame */
    struct live_worker *workers;  /* workers[0] is the calling thread */
    pthread_t th[63];
    int npool;                    /* spawned pool threads */
    _Atomic unsigned stage_id;    /* bumped before every stage */
    _Atomic int done;             /* pool threads finished with the stage */
    void *(*run)(void *);         /* the stage body, read by the pool */
};

/* four floats a lane each: live_corner_out's row loops and a depth fill done as lanes, each lane the
 * same multiplies and adds in the same order (no contraction), so the same
 * bits */
typedef float live_v4 __attribute__((vector_size(16)));

static live_v4 live_v4_at(const float *p)
{
    live_v4 v;
    memcpy(&v, p, sizeof v);
    return v;
}

static live_v4 live_v4_all(float x) { return (live_v4){x, x, x, x}; }

static struct live_section *live_sec(struct raster_live *L, int cx, int s, int cz, int pass)
{
    const struct rb_world *w = L->world;
    int dx = cx - (w->origin_cx - w->margin), dz = cz - (w->origin_cz - w->margin);
    if (dx < 0 || dx >= w->rows || dz < 0 || dz >= w->rows || s < 0 || s > 15) return NULL;
    return &L->sec[(((size_t)dx * w->rows + dz) * 16 + s) * 2 + pass];
}

static unsigned char *live_read(const char *dir, const char *name, size_t n, int required)
{
    char p[1024];
    path(p, sizeof p, dir, name);
    FILE *file = fopen(p, "rb");
    unsigned char *b = NULL;
    if (file) {
        b = alloc(n);
        if (fread(b, 1, n, file) != n) { free(b); b = NULL; }
        fclose(file);
    }
    if (!b && required) { fprintf(stderr, "raster: cannot read %s\n", p); exit(1); }
    return b;
}

/* The world's environment sheets and the fire sprite: from the asset scene's
 * own dumps when it has them, else the vanilla constants and out/assets. A
 * sheet the asset scene does not carry is the same bytes in every recording, so
 * one copy under out/assets serves them all (as end_sky.rgb already does). */
static void live_worldfx_assets(struct raster_live *L, const char *assets)
{
    char p[1024];
    struct jval *meta = NULL;
    path(p, sizeof p, assets, "state/gui.json");
    meta = readjson_opt(p);

    static const struct { const char *key, *file; int dw, dh; } sheets[] = {
        {"rain", "rain.rgba", 64, 256},
        {"snow", "snow.rgba", 64, 256},
        {"underwater", "misc/underwater.rgba", 16, 16},
    };
    for (int i = 0; i < 3; ++i)
    {
        int sw = sheets[i].dw, sh = sheets[i].dh;
        if (meta) gui_texture_size(meta, sheets[i].key, &sw, &sh);
        unsigned char *b = live_read(assets, sheets[i].key[0] == 'r' ? "state/gui_rain.rgba"
                                             : sheets[i].key[0] == 's' ? "state/gui_snow.rgba"
                                                                       : "state/gui_underwater.rgba",
                                     (size_t)sw * sh * 4, 0);
        if (!b) {
            char q[1024];
            snprintf(q, sizeof q, "out/assets/%s", sheets[i].file);
            b = live_read("out/assets", sheets[i].file, (size_t)sw * sh * 4, 0);
        }
        if (i == 0) { L->rain_tex = b; L->rain_w = sw; L->rain_h = sh; }
        else if (i == 1) { L->snow_tex = b; L->snow_w = sw; L->snow_h = sh; }
        else { L->underwater_tex = b; L->underwater_w = sw; L->underwater_h = sh; }
    }
    if (meta) json_free(meta);

    /* the particle pass's two sheets, 128x128 each (gui.json's sizes) */
    L->particle_tex = live_read(assets, "state/gui_particles.rgba", 128 * 128 * 4, 0);
    if (!L->particle_tex) L->particle_tex = live_read("out/assets", "particles.rgba", 128 * 128 * 4, 1);
    L->explosion_tex = live_read(assets, "state/gui_explosion.rgba", 128 * 128 * 4, 0);
    if (!L->explosion_tex) L->explosion_tex = live_read("out/assets", "explosion.rgba", 128 * 128 * 4, 1);

    /* the block atlas and the fire sprite, for the first-person fire overlay */
    path(p, sizeof p, assets, "atlas.json");
    struct jval *atlas = readjson_opt(p);
    if (atlas) {
        int64_t aw = 0, ah = 0;
        json_int(json_get(atlas, "atlas_width"), &aw);
        json_int(json_get(atlas, "atlas_height"), &ah);
        L->block_atlas_w = (int)aw;
        L->block_atlas_h = (int)ah;
        if (aw > 0 && ah > 0)
            L->block_atlas = live_read(assets, "atlas.rgba", (size_t)aw * (size_t)ah * 4, 0);
        const struct jval *sprites = json_get(atlas, "sprites");
        L->fire_uv[0] = L->fire_uv[1] = L->fire_uv[2] = L->fire_uv[3] = -1.0f;
        for (int i = 0; i < json_len(sprites); ++i) {
            const struct jval *sp = json_at(sprites, i);
            const char *name = json_str(json_get(sp, "name"));
            if (!name || strcmp(name, "fire_layer_1")) continue;
            L->fire_uv[0] = fbits((uint32_t)ji(json_get(sp, "minU")));
            L->fire_uv[1] = fbits((uint32_t)ji(json_get(sp, "maxU")));
            L->fire_uv[2] = fbits((uint32_t)ji(json_get(sp, "minV")));
            L->fire_uv[3] = fbits((uint32_t)ji(json_get(sp, "maxV")));
            break;
        }
        json_free(atlas);
    }
}

static struct worldfx_in live_worldfx_in(struct raster_live *L, const struct worldfx_in *in)
{
    struct worldfx_in v = *in;
    v.world = L->world;
    v.mesher = &L->mesher[0];
    v.rain_tex = L->rain_tex; v.snow_tex = L->snow_tex;
    v.rain_w = L->rain_w; v.rain_h = L->rain_h;
    v.snow_w = L->snow_w; v.snow_h = L->snow_h;
    v.underwater_tex = L->underwater_tex;
    v.underwater_w = L->underwater_w; v.underwater_h = L->underwater_h;
    v.atlas = L->block_atlas; v.atlas_w = L->block_atlas_w; v.atlas_h = L->block_atlas_h;
    v.fire_u0 = L->fire_uv[0]; v.fire_u1 = L->fire_uv[1];
    v.fire_v0 = L->fire_uv[2]; v.fire_v1 = L->fire_uv[3];
    return v;
}

void raster_live_weather(struct raster_live *L, const struct worldfx_in *in)
{
    struct worldfx_in v = live_worldfx_in(L, in);
    worldfx_weather(&v);
}

void raster_live_overlays(struct raster_live *L, const struct worldfx_in *in)
{
    raster_rec_marker(RASTER_CMD_CLEAR_DEPTH);
    struct worldfx_in v = live_worldfx_in(L, in);
    /* the far depth, four lanes a store (recording only, nothing draws
     * into it or reads it) */
    size_t n = raster_rec_only() ? 0 : (size_t)L->w * (size_t)L->h, i = 0;
    for (live_v4 one = live_v4_all(1.0f); i + 4 <= n; i += 4) memcpy(L->depth + i, &one, sizeof one);
    for (; i < n; ++i) L->depth[i] = 1.0f;
    worldfx_screen(&v);
}

struct raster_live *raster_live_new(const char *assets, const struct rb_world *world,
                                    int w, int h, int threads)
{
    struct raster_live *L = calloc(1, sizeof *L);
    if (!L) exit(1);
    jmath_init();
    L->w = w; L->h = h; L->threads = threads < 1 ? 1 : threads;
    L->world = world;
    L->assets = strdup(assets);
    char p[1024];
    path(p, sizeof p, assets, "atlas.json");
    struct jval *root = readjson(p);
    raster_texture_load(&L->texture, assets, ji(json_get(root, "atlas_width")),
                        ji(json_get(root, "atlas_height")),
                        ji(json_get(root, "mip_levels")),
                        ji(json_get(root, "min_filter")));
    json_free(root);
    L->sun = live_read(assets, "sun.rgba", 32 * 32 * 4, 1);
    L->moon = live_read(assets, "moon_phases.rgba", 128 * 64 * 4, 1);
    L->clouds = live_read(assets, "clouds.rgba", 256 * 256 * 4, 1);
    L->end_sky = live_read("out/assets", "end_sky.rgb", 128 * 128 * 3, 0);
    if (L->threads > 64) L->threads = 64;
    L->tess = calloc((size_t)L->threads, sizeof *L->tess);
    L->mesher = calloc((size_t)L->threads, sizeof *L->mesher);
    L->mkc = meshkey_cache_new();
    L->tab = rb_table_shared(assets);
    L->atlas = rb_atlas_shared(assets);
    if (!L->tess || !L->mesher || L->tab == NULL || L->atlas == NULL)
        exit(1);
    for (int i = 0; i < L->threads; ++i)
        if (rb_tess_init(&L->tess[i], RB_TESS_CAP) != 0 ||
            rb_mesher_init(&L->mesher[i], L->tab, L->atlas, world, &L->tess[i], 0, 0, 0) != 0)
            exit(1);
    for (int id = 0; id < 4096; ++id) {
        int p = rb_block_pass(L->tab, id);
        L->pass[id] = p == 0 || p == 1 ? (uint8_t)(1 << p) : 0;
    }
    L->sec = calloc((size_t)world->rows * world->rows * 32, sizeof *L->sec);
    L->pend = malloc((size_t)world->rows * world->rows * sizeof *L->pend);
    if (!L->pend) exit(1);
    memset(L->pend, 0xff, (size_t)world->rows * world->rows * sizeof *L->pend);
    L->drop_r = -1;
    L->depth = alloc((size_t)w * h * sizeof(float));
    L->mask = alloc((size_t)w * h);
    if (!L->sec) exit(1);
    pthread_mutex_init(&L->lock, NULL);
    L->workers = calloc((size_t)L->threads, sizeof *L->workers);
    if (!L->workers) exit(1);
    for (int i = 0; i < L->threads; ++i) L->workers[i] = (struct live_worker){L, i};
    for (int i = 1; i < L->threads; ++i)
        if (pthread_create(&L->th[L->npool], NULL, pool_thread, &L->workers[i]) == 0) ++L->npool;
    live_worldfx_assets(L, assets);
    return L;
}

struct rb_mesher *raster_live_mesher(struct raster_live *L) { return &L->mesher[0]; }
const unsigned char *raster_live_particle_tex(struct raster_live *L) { return L->particle_tex; }
const unsigned char *raster_live_explosion_tex(struct raster_live *L) { return L->explosion_tex; }
/* the layer 1 sheet: the terrain atlas and its dims */
const unsigned char *raster_live_block_atlas(struct raster_live *L, int *w, int *h)
{
    *w = L->block_atlas_w;
    *h = L->block_atlas_h;
    return L->block_atlas;
}
const float *raster_live_depth(const struct raster_live *L) { return L->depth; }
float *raster_live_depth_mut(struct raster_live *L) { return L->depth; }
const struct raster_live_stats *raster_live_stats(const struct raster_live *L) { return &L->stats; }

size_t raster_live_mesh_bytes(const struct raster_live *L)
{
    size_t n = (size_t)L->world->rows * L->world->rows * 32;
    size_t b = n * sizeof *L->sec;
    for (size_t i = 0; i < n; ++i)
        if (L->sec[i].raw) b += (size_t)L->sec[i].count * 8 * sizeof(int32_t);
    return b;
}

void raster_live_overlay(struct raster_live *L, const struct rs_out *rs,
                         unsigned char *rgb, int selected, int sx, int sy, int sz,
                         const double *box, int stage, int dx, int dy, int dz)
{
    double cam[3] = {rs->camx, rs->camy, rs->camz};
    struct raster_overlay_in oi = {0};
    oi.w = L->w; oi.h = L->h;
    oi.proj = rs->proj; oi.mv = rs->mv; oi.depth = L->depth;
    oi.cam = cam; oi.rgb = rgb; oi.texture = &L->texture;
    oi.mesher = &L->mesher[0]; oi.atlas = L->atlas;
    oi.stage = stage; oi.dx = dx; oi.dy = dy; oi.dz = dz;
    oi.fog = rs->gfogc; oi.fogs = rs->gfogs; oi.foge = rs->gfoge; oi.fogd = rs->gfogd; oi.fogm = rs->gfogm;
    if (selected) {
        int id = rb_world_block(L->world, sx, sy, sz);
        if (id > 0 && id < RB_IDS) {
            int meta = rb_world_meta(L->world, sx, sy, sz) & 15;
            const double *b = box ? box : L->tab->bounds + ((size_t)id * 16 + meta) * 6;
            oi.selected = 1;
            oi.sx = sx; oi.sy = sy; oi.sz = sz;
            oi.box[0] = sx + b[0]; oi.box[1] = sy + b[1]; oi.box[2] = sz + b[2];
            oi.box[3] = sx + b[3]; oi.box[4] = sy + b[4]; oi.box[5] = sz + b[5];
        }
    }
    raster_overlay_draw(&oi);
}

static double live_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

void raster_live_stale(struct raster_live *L, int cx, int s, int cz)
{
    if (L->feed) meshfeed_dirty(L->feed, cx, s, cz);
    for (int pass = 0; pass < 2; ++pass) {
        struct live_section *e = live_sec(L, cx, s, cz, pass);
        if (e) e->meshed = 0;
    }
    struct live_section *e = live_sec(L, cx, s, cz, 0);
    if (e) L->pend[(e - L->sec) / 32] |= (uint16_t)(1u << s);
}

void raster_live_rebase(struct raster_live *L, int old_origin_cx, int old_origin_cz, int old_margin, int old_rows)
{
    const struct rb_world *w = L->world;
    struct live_section *sec = calloc((size_t)w->rows * w->rows * 32, sizeof *sec);
    uint16_t *pend = malloc((size_t)w->rows * w->rows * sizeof *pend);
    if (!sec || !pend) exit(1);
    memset(pend, 0xff, (size_t)w->rows * w->rows * sizeof *pend);
    for (int dx = 0; dx < old_rows; ++dx)
        for (int dz = 0; dz < old_rows; ++dz) {
            struct live_section *from = &L->sec[((size_t)dx * old_rows + dz) * 32];
            int nx = old_origin_cx - old_margin + dx - (w->origin_cx - w->margin);
            int nz = old_origin_cz - old_margin + dz - (w->origin_cz - w->margin);
            if (nx >= 0 && nx < w->rows && nz >= 0 && nz < w->rows) {
                memcpy(&sec[((size_t)nx * w->rows + nz) * 32], from, 32 * sizeof *sec);
                pend[(size_t)nx * w->rows + nz] = L->pend[(size_t)dx * old_rows + dz];
            }
            else
                for (int k = 0; k < 32; ++k) free(from[k].raw);
        }
    free(L->pend);
    L->pend = pend;
    L->drop_r = -1;
    free(L->sec);
    L->sec = sec;
}

void raster_live_device_mesh(struct raster_live *L, int mode)
{
    L->dmesh = mode;
    if (mode && !L->feed) {
        L->feed = meshfeed_new();
        meshfeed_band_ids(L->feed, L->mkc);
    }
    if (L->feed) meshfeed_reset(L->feed);
    raster_live_stale_all(L);
}

void raster_live_mesh_resync(struct raster_live *L)
{
    L->feed_resync = 1;
}

void raster_live_band_taken(struct raster_live *L, const struct chunk_sec *b)
{
    if (L) meshkey_cache_taken(L->mkc, b);
}

void raster_live_band_same(struct raster_live *L, const struct chunk_sec *from, const struct chunk_sec *b)
{
    if (L) meshkey_cache_same(L->mkc, from, b);
}

void raster_live_key_stats(const struct raster_live *L, uint64_t *checked, uint64_t *bad)
{
    meshkey_cache_stats(L->mkc, checked, bad);
}

void raster_live_stale_all(struct raster_live *L)
{
    if (L->feed) meshfeed_dirty_all(L->feed);
    size_t n = (size_t)L->world->rows * L->world->rows * 32;
    for (size_t i = 0; i < n; ++i) L->sec[i].meshed = L->sec[i].keyed = 0;
    memset(L->pend, 0xff, (size_t)L->world->rows * L->world->rows * sizeof *L->pend);
}

/* A mesh's version, unique in the process: a renderer that keeps meshes by
 * (section slot, version) (raster_obs.h, csrc/cuda/render) never takes a
 * mesh of another section or another renderer for this one (a window's
 * rebase moves sections between slots, and a new renderer after a
 * dimension change would count from 1 again) */
static _Atomic uint32_t live_mesh_serial;

/* the empty mesh of a pass none of the section's cells is drawn in
 * (MESHKEY_EMPTY): what rb_mesh_section leaves, no vertices and no
 * Tessellator state */
static void live_empty(struct live_section *e)
{
    free(e->raw);
    e->raw = NULL;
    e->count = 0;
    e->flags = 0;
    e->meshed = 1;
    e->keyed = MESHKEY_EMPTY;
    e->version = atomic_fetch_add_explicit(&live_mesh_serial, 1, memory_order_relaxed) + 1;
}

/* rb_mesh_section over the chunk-window world, the cells no pass-PASS
 * block holds passed over by their band's ids (rb_mesh_cell_kind answers 0
 * for them: air, or drawn in the other pass) */
static void live_mesh_cells(const struct raster_live *L, struct rb_mesher *m, int cx, int s, int cz, int pass)
{
    const struct chunk_sec *b = meshkey_band(L->world, cx, s, cz);
    if (!L->world->chunks) { rb_mesh_section(m, cx, cz, s, pass); return; }
    rb_mesh_section_begin(m, cx, cz, s);
    if (!b) return;
    int bit = 1 << pass, x0 = cx << 4, y0 = s << 4, z0 = cz << 4;
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int c = x << 8 | z << 4 | y;
                if (!(L->pass[b->ids_hi ? chunk_sec_id(b, c) : b->ids[c]] & bit)) continue;
                int id = 0, meta = 0;
                int kind = rb_mesh_cell_kind(m, x0 + x, y0 + y, z0 + z, pass, &id, &meta);
                if (kind) rb_mesh_cell(m, kind, id, meta, x0 + x, y0 + y, z0 + z);
            }
}

static void live_mesh(struct raster_live *L, struct rb_mesher *m, const struct live_stale *st)
{
    struct live_section *e = st->e;
    int cx = st->cx, s = st->s, cz = st->cz, pass = st->pass;
    /* a check makes the mesh again the reference way */
    if (st->check) rb_mesh_section(m, cx, cz, s, pass);
    else live_mesh_cells(L, m, cx, s, cz, pass);
    struct rb_tess *t = m->t;
    if (t->dropped) { fprintf(stderr, "raster: mesher vertex buffer full\n"); exit(1); }
    int flags = (t->has_texture ? 1 : 0) | (t->has_color ? 2 : 0) |
                (t->has_brightness ? 4 : 0) | (t->has_normals ? 8 : 0);
    if (st->check) {
        /* the mesh the key kept (or the empty one), against this one: the
         * same keeps it as the unchecked renderer would */
        int same = st->check == 2 ? !t->vertex_count && !flags
                 : e->count == t->vertex_count && e->flags == flags &&
                   (!e->count || !memcmp(e->raw, t->raw, (size_t)e->count * 8 * sizeof(int32_t)));
        atomic_fetch_add_explicit(&L->checked, 1, memory_order_relaxed);
        if (same) {
            if (st->check == 2) live_empty(e);
            else e->meshed = 1;
            return;
        }
        atomic_fetch_add_explicit(&L->check_bad, 1, memory_order_relaxed);
        fprintf(stderr, "raster: the %s mesh of section %d %d %d pass %d differs from its mesh made again (%d vertices, %d)\n",
                st->check == 2 ? "empty" : "kept", cx, s, cz, pass, st->check == 2 ? 0 : e->count, t->vertex_count);
    }
    e->flags = flags;
    e->count = t->vertex_count;
    free(e->raw);
    e->raw = NULL;
    if (e->count) {
        e->raw = alloc((size_t)e->count * 8 * sizeof(int32_t));
        memcpy(e->raw, t->raw, (size_t)e->count * 8 * sizeof(int32_t));
    }
    e->meshed = 1;
    e->version = atomic_fetch_add_explicit(&live_mesh_serial, 1, memory_order_relaxed) + 1;
}

/* The frame's feed for the device mesher (raster_live_device_mesh): the
 * stale sections, in the order the host meshes them, as requests (mode 1:
 * each gets its version here and no mesh), the world bytes they need that
 * the device lacks, the host's meshes beside them (mode 2), and where the
 * device's answers come back. */
static void live_feed(struct raster_live *L, int pcx, int pcz, int r)
{
    for (int i = 0; L->dmesh == 1 && i < L->nstale; ++i) {
        struct live_section *e = L->stale[i].e;
        if (L->stale[i].check) continue;
        free(e->raw);
        e->raw = NULL;
        e->count = -1;
        e->flags = 0;
        e->meshed = 1;
        e->version = atomic_fetch_add_explicit(&live_mesh_serial, 1, memory_order_relaxed) + 1;
    }
    meshfeed_begin(L->feed);
    const struct rb_mesher *m = &L->mesher[0];
    for (int i = 0; i < L->nstale; ++i) {
        const struct live_stale *st = &L->stale[i];
        if (st->check) continue;
        meshfeed_request(L->feed, L->world, st->cx, st->s, st->cz, st->pass, m->px, m->py, m->pz,
                         (uint32_t)(st->e - L->sec), st->e->version);
    }
    /* the requests of frames whose feed did not go out: a section meshed
     * again since, or dropped, goes */
    int nr = 0;
    const struct meshfeed_req *rq = meshfeed_requests(L->feed, &nr);
    if (nr > L->capfeed) {
        L->capfeed = 2 * nr;
        free(L->feed_host); free(L->feed_prev); free(L->feed_counts); free(L->feed_keep);
        L->feed_host = alloc((size_t)L->capfeed * sizeof *L->feed_host);
        L->feed_prev = alloc((size_t)L->capfeed * sizeof *L->feed_prev);
        L->feed_counts = alloc((size_t)L->capfeed * sizeof *L->feed_counts);
        L->feed_keep = alloc((size_t)L->capfeed);
    }
    for (int i = 0; i < nr; ++i) {
        const struct live_section *e = live_sec(L, rq[i].cx, rq[i].s, rq[i].cz, rq[i].pass);
        L->feed_keep[i] = e && e->meshed && e->version == rq[i].version;
    }
    meshfeed_keep(L->feed, L->feed_keep);
    L->feed_out = *meshfeed_end(L->feed);
    for (int i = 0; i < L->feed_out.nreq; ++i) {
        const struct meshfeed_req *q = &L->feed_out.req[i];
        const struct live_section *e = live_sec(L, q->cx, q->s, q->cz, q->pass);
        L->feed_host[i] = (struct raster_obs_hostmesh){e->count, e->flags, e->raw};
        L->feed_prev[i] = (struct live_feed_prev){q->cx, q->s, q->cz, q->pass, q->version};
        L->feed_counts[i] = -1;
    }
    L->nfeed_prev = L->feed_out.nreq;
    L->feed_out.cx0 = pcx - r; L->feed_out.cx1 = pcx + r;
    L->feed_out.cz0 = pcz - r; L->feed_out.cz1 = pcz + r;
    L->feed_out.count_back = L->feed_counts;
    L->feed_out.resync = &L->feed_resync;
    ++L->feed_frame;
}

/* Conservative: 1 when all eight corners of the section lie outside one clip
 * plane, in the camera-relative frame make_vertex builds. */
/* The planes a section corner (the grid point (gx, gy, gz) * 16) lies
 * outside of, a bit each */
static int live_corner_out(const struct frame *f, int gx, int gy, int gz)
{
    float pos[4] = {(float)((double)(gx * 16) - f->cam[0]), (float)((double)(gy * 16) - f->cam[1]),
                    (float)((double)(gz * 16) - f->cam[2]), 1};
    /* eye[r] = mv[r] p0 + mv[4+r] p1 + mv[8+r] p2 + mv[12+r], then clip[r]
     * = proj[r] eye0 + proj[4+r] eye1 + proj[8+r] eye2 + proj[12+r] eye3 */
    live_v4 ev = live_v4_at(f->mv) * live_v4_all(pos[0]) + live_v4_at(f->mv + 4) * live_v4_all(pos[1]);
    ev = ev + live_v4_at(f->mv + 8) * live_v4_all(pos[2]);
    ev = ev + live_v4_at(f->mv + 12);
    live_v4 cv = live_v4_at(f->proj) * live_v4_all(ev[0]) + live_v4_at(f->proj + 4) * live_v4_all(ev[1]);
    cv = cv + live_v4_at(f->proj + 8) * live_v4_all(ev[2]);
    cv = cv + live_v4_at(f->proj + 12) * live_v4_all(ev[3]);
    float clip[4] = {cv[0], cv[1], cv[2], cv[3]};
    float m = fabsf(clip[3]) * 1e-3f + 1e-3f;   /* margin against rounding */
    int out = 0;
    for (int plane = 0; plane < 6; ++plane) {
        float sign = plane & 1 ? -1.0f : 1.0f;
        if (clip[3] - sign * clip[plane / 2] < -m) out |= 1 << plane;
    }
    return out;
}

/* A section is culled when all eight corners lie outside one plane. The
 * corners are grid points its neighbours share: GRID caches each one's
 * planes (0xff: not yet), the grid from (gx0, gz0), side columns wide;
 * LEVEL caches, for each column and height, the planes its four corners
 * there all lie outside (the AND of theirs), so a section's eight are two
 * lookups: the AND of its bottom level's and its top's (lane/simclimb: every
 * pending section is tested each frame). */
struct live_cull { const struct frame *f; unsigned char *grid, *level; int gx0, gz0, side; };

static inline int live_level(const struct live_cull *c, int cx, int y, int cz)
{
    size_t col = (size_t)(cx - c->gx0) * (size_t)c->side + (size_t)(cz - c->gz0);
    unsigned char *l = &c->level[col * 17 + (size_t)y];
    if (*l == 0xff) {
        int all = 63;
        for (int i = 0; i < 4; ++i) {
            int gx = cx + (i & 1), gz = cz + (i >> 1 & 1);
            unsigned char *g = &c->grid[((size_t)(gx - c->gx0) * (size_t)c->side + (size_t)(gz - c->gz0)) * 17 + (size_t)y];
            if (*g == 0xff) *g = (unsigned char)live_corner_out(c->f, gx, y, gz);
            all &= *g;
        }
        *l = (unsigned char)all;
    }
    return *l;
}

static inline int live_culled(const struct live_cull *c, int cx, int s, int cz)
{
    return (live_level(c, cx, s, cz) & live_level(c, cx, s + 1, cz)) != 0;
}

static float live_quad_distance(const int32_t *raw, int offset, float x, float y, float z)
{
    float sum[3] = {0};
    for (int i = 0; i < 4; ++i) {
        const int32_t *r = raw + (size_t)(offset + i) * 8;
        sum[0] += fbits((uint32_t)r[0]) - x;
        sum[1] += fbits((uint32_t)r[1]) - y;
        sum[2] += fbits((uint32_t)r[2]) - z;
    }
    float dx = sum[0] * 0.25f, dy = sum[1] * 0.25f, dz = sum[2] * 0.25f;
    return dx * dx + dy * dy + dz * dz;
}

/* Java sorts the translucent renderers with a stable sort: equal distances keep
 * the square's own order. */
struct live_water_key { double distance; int index; };
static int live_water_compare(const void *a0, const void *b0)
{
    const struct live_water_key *a = a0, *b = b0;
    if (a->distance != b->distance) return a->distance < b->distance ? 1 : -1;
    return a->index - b->index;
}

static void live_draw_section(struct frame *f, const struct live_draw *d)
{
    const struct live_section *e = d->sec;
    int count = e->count / 4;
    for (int i = 0; i < count; ++i) {
        int offset = d->order ? d->order[i].offset : i * 4;
        struct vertex quad[4];
        for (int j = 0; j < 4; ++j) {
            uint32_t raw[8];
            for (int k = 0; k < 8; ++k) raw[k] = (uint32_t)e->raw[(size_t)(offset + j) * 8 + k];
            quad[j] = make_vertex(f, raw, d->cx, d->s, d->cz, e->flags);
        }
        struct vertex a[3] = {quad[0], quad[1], quad[3]};
        struct vertex b[3] = {quad[1], quad[2], quad[3]};
        clip_triangle(f, a);
        clip_triangle(f, b);
    }
}

static void live_list_push(struct live_list *l, const struct vertex v[3], int y0, int y1, int mode)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 1024;
        l->v = realloc(l->v, (size_t)l->cap * 3 * sizeof *l->v);
        l->rows = realloc(l->rows, (size_t)l->cap * 2 * sizeof *l->rows);
        l->mode = realloc(l->mode, (size_t)l->cap);
        if (!l->v || !l->rows || !l->mode) exit(1);
    }
    memcpy(l->v + (size_t)l->n * 3, v, 3 * sizeof *v);
    l->rows[l->n * 2] = (int16_t)y0;
    l->rows[l->n * 2 + 1] = (int16_t)y1;
    l->mode[l->n] = (uint8_t)mode;
    ++l->n;
}

/* clip_triangle's sink: keep what triangle() would draw, with the rows it
 * would visit. A zero area, or a back face outside the two-sided clouds, draws
 * nothing; so does a triangle whose rows or columns miss the screen. */
static void live_emit(struct frame *f, const struct vertex v[3])
{
    float area = edge(&v[0], &v[1], v[2].x, v[2].y);
    if (area == 0 || (area < 0 && f->sky_mode != 3)) return;
    float minx = fminf(v[0].sx, fminf(v[1].sx, v[2].sx));
    float maxx = fmaxf(v[0].sx, fmaxf(v[1].sx, v[2].sx));
    float miny = fminf(v[0].sy, fminf(v[1].sy, v[2].sy));
    float maxy = fmaxf(v[0].sy, fmaxf(v[1].sy, v[2].sy));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(f->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(f->h - 1, ceilf(maxy));
    if (x0 > x1 || y0 > y1) return;
    live_list_push(f->sink, v, y0, y1,
                   f->sky_mode | (f->cloud_prepass ? 16 : 0) | (f->render_pass ? 32 : 0));
}

static int live_take(struct raster_live *L)
{
    pthread_mutex_lock(&L->lock);
    int i = L->next++;
    pthread_mutex_unlock(&L->lock);
    return i;
}

/* Stage zero: the stale sections of the square, each thread with its mesher. */
static void *live_mesh_run(void *arg)
{
    struct live_worker *w = arg;
    struct raster_live *L = w->L;
    for (int i; (i = live_take(L)) < L->nstale;) {
        live_mesh(L, &L->mesher[w->id], &L->stale[i]);
    }
    return NULL;
}

/* Stage one: every draw's quads through make_vertex and the clipper. */
static void *live_record_run(void *arg)
{
    struct raster_live *L = ((struct live_worker *)arg)->L;
    struct frame f = L->base;
    int n = L->nopaque + L->nwater;
    for (int i; (i = live_take(L)) < n;) {
        const struct live_draw *d = i < L->nopaque ? &L->opaque[i] : &L->water[i - L->nopaque];
        struct live_list *l = &L->lists[i];
        l->n = 0;
        f.render_pass = i >= L->nopaque;
        f.sink = l;
        live_draw_section(&f, d);
    }
    return NULL;
}

static void live_rasterize(struct frame *f, const struct live_list *l)
{
    for (int i = 0; i < l->n; ++i) {
        if (l->rows[i * 2 + 1] < f->band_y0 || l->rows[i * 2] > f->band_y1) continue;
        int m = l->mode[i];
        f->sky_mode = m & 15;
        f->cloud_prepass = (m >> 4) & 1;
        f->render_pass = (m >> 5) & 1;
        triangle(f, l->v + (size_t)i * 3);
    }
    f->sky_mode = f->cloud_prepass = f->render_pass = 0;
}

/* Stage two: row bands, taken in turn; each draws every recorded list in order. */
enum { LIVE_BAND_ROWS = 4 };
static void *live_band_run(void *arg)
{
    struct raster_live *L = ((struct live_worker *)arg)->L;
    struct frame f = L->base;
    int nbands = (L->h + LIVE_BAND_ROWS - 1) / LIVE_BAND_ROWS;
    int nlists = L->nopaque;   /* the translucent lists wait for raster_live_translucent */
    for (int b; (b = live_take(L)) < nbands;) {
        f.band = 1;
        f.band_y0 = b * LIVE_BAND_ROWS;
        f.band_y1 = f.band_y0 + LIVE_BAND_ROWS - 1;
        if (f.band_y1 > f.h - 1) f.band_y1 = f.h - 1;
        for (int y = f.band_y0; y <= f.band_y1; ++y) for (int x = 0; x < f.w; ++x) {
            size_t k = (size_t)y * f.w + x;
            f.depth[k] = 1.0f;
            f.mask[k] = 0;
            for (int c = 0; c < 3; ++c) f.rgb[k * 3 + c] = byte(f.fog[c]);
        }
        live_rasterize(&f, &L->sky);
        for (int i = 0; i < nlists; ++i) live_rasterize(&f, &L->lists[i]);
    }
    return NULL;
}

/* The translucent pass's bands over what the frame and the caller's passes
 * drew (raster_live_translucent). */
static void *live_water_band_run(void *arg)
{
    struct raster_live *L = ((struct live_worker *)arg)->L;
    struct frame f = L->base;
    int nbands = (L->h + LIVE_BAND_ROWS - 1) / LIVE_BAND_ROWS;
    for (int b; (b = live_take(L)) < nbands;) {
        f.band = 1;
        f.band_y0 = b * LIVE_BAND_ROWS;
        f.band_y1 = f.band_y0 + LIVE_BAND_ROWS - 1;
        if (f.band_y1 > f.h - 1) f.band_y1 = f.h - 1;
        for (int i = 0; i < L->nwater; ++i) live_rasterize(&f, &L->lists[L->nopaque + i]);
    }
    return NULL;
}

/* The pool: threads spin on the stage id between stages (stages are so short
 * that a condvar round trip costs more than the work), and sleep in a
 * short nanosleep loop when idle between frames. stage_id is read and written
 * with atomics; the counters it guards are only touched while it is stable. */
#include <time.h>

static void *pool_thread(void *arg)
{
    struct live_worker *w = arg;
    struct raster_live *L = w->L;
    unsigned seen = 0;
    for (;;) {
        unsigned s;
        long budget = 20000;   /* spin ~0.3 ms after finishing work, so the
                                * next stage needs no wakeup, then poll */
        while ((s = atomic_load_explicit(&L->stage_id, memory_order_acquire)) == seen) {
            if (--budget <= 0) {
                struct timespec ts = {0, 100000};
                nanosleep(&ts, NULL);
            }
#if defined(__x86_64__)
            else __builtin_ia32_pause();
#endif
        }
        if (s == UINT_MAX) return NULL;
        seen = s;
        L->run(w);
        atomic_fetch_add_explicit(&L->done, 1, memory_order_release);
    }
}

static void live_stage(struct raster_live *L, void *(*run)(void *))
{
    L->next = 0;
    if (L->npool) {
        L->run = run;
        atomic_store_explicit(&L->done, 0, memory_order_relaxed);
        atomic_store_explicit(&L->stage_id, L->stage_id + 1, memory_order_release);
        run(&L->workers[0]);   /* the caller's thread takes a share too */
        while (atomic_load_explicit(&L->done, memory_order_acquire) < L->npool)
            ;   /* a stage is under a millisecond: spin, do not sleep */
        return;
    }
    run(&L->workers[0]);
}

void raster_live_translucent(struct raster_live *L, unsigned char *rgb)
{
    raster_rec_marker(RASTER_CMD_WATER);
    if (raster_rec_only()) return;
    L->base.rgb = rgb;
    live_stage(L, live_water_band_run);
}

int raster_live_frame(struct raster_live *L, const struct raster_live_in *in, unsigned char *rgb)
{
    const struct rs_out *o = in->rs;
    struct frame *f = &L->base;
    memset(f, 0, sizeof *f);
    f->w = L->w; f->h = L->h;
    f->prec = L->prec;
    memcpy(f->proj, o->proj, sizeof f->proj);
    memcpy(f->mv, o->mv, sizeof f->mv);
    f->cam[0] = o->camx; f->cam[1] = o->camy; f->cam[2] = o->camz;
    for (int i = 0; i < 3; ++i) f->fog[i] = o->gfogc[i];
    f->fogs = o->gfogs; f->foge = o->gfoge; f->fogd = o->gfogd; f->fogm = o->gfogm;
    for (int i = 0; i < 256; ++i) f->lm[i] = (uint32_t)o->lm[i];
    f->dimension = in->dimension;
    f->clouds_enabled = in->clouds;
    /* RenderGlobal.renderSky reads getSkyColor's float triple back out of its Vec3 */
    for (int i = 0; i < 3; ++i) f->sky[i] = (float)o->sky[i];
    f->sky_bottom[0] = f->sky[0] * 0.2f + 0.04f;
    f->sky_bottom[1] = f->sky[1] * 0.2f + 0.04f;
    f->sky_bottom[2] = f->sky[2] * 0.6f + 0.1f;
    f->sky_floor = -(float)(in->view_y - 63.0);
    f->sky_far = in->rd * 16.0f;
    f->cloud_tick = in->cloud_tick;
    f->moon_phase = (int)((in->wt / 24000 % 8 + 8) % 8);
    f->celestial_angle = o->ang;
    f->star = o->star * (1.0f - o->rain);
    for (int i = 0; i < 3; ++i) f->cloud_color[i] = (float)o->cloud[i];
    if (o->rise_ok) for (int i = 0; i < 4; ++i) f->sunrise[i] = o->rise[i];
    f->cloud_height = 128.0f - (float)in->view_y + 0.33f;
    f->texture = L->texture;
    f->sun = L->sun; f->moon = L->moon; f->clouds = L->clouds; f->end_sky = L->end_sky;
    f->rgb = rgb; f->depth = L->depth; f->mask = L->mask;

    /* the square of sections around the camera chunk, meshed on demand */
    int pcx = (int)floor(f->cam[0] / 16.0), pcz = (int)floor(f->cam[2] / 16.0);
    int r = in->rd;
    for (int i = 0; i < L->threads; ++i) {
        L->mesher[i].px = (int)floor(f->cam[0]);
        L->mesher[i].py = (int)floor(f->cam[1]);
        L->mesher[i].pz = (int)floor(f->cam[2]);
    }
    int need = (2 * r + 1) * (2 * r + 1) * 16;
    if (2 * need > L->capstale) {
        free(L->stale);
        L->stale = alloc((size_t)2 * need * sizeof *L->stale);
        L->capstale = 2 * need;
    }
    /* a section outside the square drops its mesh: it is meshed again from
     * its record if it comes back, the same bytes the cache would hold (the
     * records it reads mark it stale whenever they change) */
    const struct rb_world *w = L->world;
    /* nothing outside a square that has not moved has a mesh again (a
     * section is meshed only inside it) */
    int drop = !L->keep && (L->drop_r != r || L->drop_cx != pcx || L->drop_cz != pcz);
    for (int dx = 0; dx < w->rows && drop; ++dx)
        for (int dz = 0; dz < w->rows; ++dz)
        {
            int cx = w->origin_cx - w->margin + dx, cz = w->origin_cz - w->margin + dz;
            if (abs(cx - pcx) <= r && abs(cz - pcz) <= r) continue;
            struct live_section *e = &L->sec[((size_t)dx * w->rows + dz) * 32];
            for (int k = 0; k < 32; ++k)
            {
                free(e[k].raw);
                e[k].raw = NULL;
                e[k].count = 0;
                e[k].meshed = 0;
                e[k].keyed = MESHKEY_NONE;
            }
            L->pend[(size_t)dx * w->rows + dz] = 0xffff;
        }
    if (drop) { L->drop_cx = pcx; L->drop_cz = pcz; L->drop_r = r; }
    if (L->keep) L->drop_r = -1;
    /* the device's answers to the last frame's feed: its counts, or a
     * resync (it could not follow the feed: every section again, from a
     * new epoch) */
    if (L->dmesh && L->feed_resync) {
        L->feed_resync = 0;
        raster_live_stale_all(L);
        meshfeed_reset(L->feed);
        for (int i = 0; i < L->nfeed_prev; ++i) L->feed_counts[i] = -1;
    }
    for (int i = 0; L->dmesh && i < L->nfeed_prev; ++i) {
        const struct live_feed_prev *q = &L->feed_prev[i];
        struct live_section *e = live_sec(L, q->cx, q->s, q->cz, q->pass);
        if (e && e->meshed && e->version == q->version && e->count < 0 && L->feed_counts[i] >= 0)
            e->count = L->feed_counts[i];
    }
    double t0 = live_now_ms();
    L->nstale = 0;
    /* a stale section pass whose key (raster_meshkey.h) is the one its mesh
     * was made from keeps the mesh; a pass none of its cells is drawn in
     * takes the empty mesh; the rest are meshed (with raster_live_mesh_check,
     * every one: the kept and empty meshes are compared with the new ones) */
    int reused = 0, empty = 0, check = L->keycheck && L->dmesh != 1;
    atomic_store(&L->checked, 0);
    atomic_store(&L->check_bad, 0);
    int kx = L->mesher[0].px, ky = L->mesher[0].py, kz = L->mesher[0].pz;
    /* the frame's corner planes (live_culled), for the stale sections here
     * and the draw list below */
    int gside = 2 * r + 2;
    unsigned char *grid = alloc((size_t)gside * (size_t)gside * 17 * 2);
    memset(grid, 0xff, (size_t)gside * (size_t)gside * 17 * 2);
    const struct live_cull cull = {f, grid, grid + (size_t)gside * (size_t)gside * 17, pcx - r, pcz - r, gside};
    for (int cx = pcx - r; cx <= pcx + r; ++cx)
        for (int cz = pcz - r; cz <= pcz + r; ++cz) {
            struct live_section *c0 = live_sec(L, cx, 0, cz, 0);
            if (!c0) continue;
            uint16_t *pend = &L->pend[(c0 - L->sec) / 32];
            unsigned bits = *pend;
            /* every section of the slot is meshed, kept or listed below,
             * or stays pending */
            *pend = 0;
            for (int s = 0; s < 16; ++s) {
                if (!(bits >> s & 1)) continue;
                struct live_section *e0 = c0 + s * 2;
                if (e0->meshed && e0[1].meshed) continue;
                /* a section with a cell to draw that the frame does not
                 * draw (outside the view, the test the draw list makes)
                 * stays stale until a frame that draws it meshes it, from
                 * the world as it is then: what a mesh made now and kept or
                 * made again would hold (lane/simclimb: two thirds of the
                 * speedrun walk's meshes were out of view); an empty one is
                 * settled now, as it costs nothing to */
                if (meshkey_drawn(L->world, L->pass, L->mkc, cx, s, cz) && live_culled(&cull, cx, s, cz)) {
                    *pend |= (uint16_t)(1u << s);
                    continue;
                }
                struct meshkey k;
                int drawn;
                int kind0 = meshkey_section(L->world, L->pass, L->mkc, cx, s, cz, kx, ky, kz, &k, &drawn);
                for (int pass = 0; pass < 2; ++pass) {
                    struct live_section *e = e0 + pass;
                    if (e->meshed) continue;
                    int kind = drawn >> pass & 1 ? kind0 : MESHKEY_EMPTY;
                    int same = kind == MESHKEY_EMPTY ? e->keyed == MESHKEY_EMPTY
                             : kind == MESHKEY_OK && e->keyed == MESHKEY_OK && !memcmp(&e->key, &k, sizeof k);
                    if (same) ++reused;
                    else if (kind == MESHKEY_EMPTY) ++empty;
                    if ((same || kind == MESHKEY_EMPTY) && check) {
                        L->stale[L->nstale++] = (struct live_stale){cx, s, cz, pass, same ? 1 : 2, e};
                        continue;
                    }
                    if (same) { e->meshed = 1; continue; }
                    if (kind == MESHKEY_EMPTY) { live_empty(e); continue; }
                    e->keyed = kind;
                    e->key = k;
                    L->stale[L->nstale++] = (struct live_stale){cx, s, cz, pass, 0, e};
                }
            }
        }
    int meshed = L->nstale - (check ? reused + empty : 0);
    /* a kept band hash that no longer matched its bytes (raster_live_mesh_check) */
    uint64_t kchecked, kbad;
    meshkey_cache_stats(L->mkc, &kchecked, &kbad);
    if (kbad > L->mkc_bad) atomic_fetch_add_explicit(&L->check_bad, (int)(kbad - L->mkc_bad), memory_order_relaxed);
    L->mkc_bad = kbad;
    if (L->nstale && L->dmesh != 1) live_stage(L, live_mesh_run);
    double feed_ms = 0;
    if (L->dmesh) {
        double tf = live_now_ms();
        live_feed(L, pcx, pcz, r);
        feed_ms = live_now_ms() - tf;
    }
    double tmesh = live_now_ms() - t0;
    if (need > L->capdraw) {
        free(L->opaque); free(L->water);
        L->opaque = alloc((size_t)need * sizeof *L->opaque);
        L->water = alloc((size_t)need * sizeof *L->water);
        L->capdraw = need;
    }
    if (2 * need > L->caplists) {
        L->lists = realloc(L->lists, (size_t)2 * need * sizeof *L->lists);
        if (!L->lists) exit(1);
        memset(L->lists + L->caplists, 0, (size_t)(2 * need - L->caplists) * sizeof *L->lists);
        L->caplists = 2 * need;
    }
    for (int i = 0; i < L->nwater; ++i) free(L->water[i].order);
    L->nopaque = L->nwater = 0;
    struct live_water_key *keys = alloc((size_t)need * sizeof *keys);
    struct live_draw *wlist = alloc((size_t)need * sizeof *wlist);
    int nkeys = 0;
    for (int cx = pcx - r; cx <= pcx + r; ++cx)
        for (int cz = pcz - r; cz <= pcz + r; ++cz) {
            /* the column's 16 sections by 2 passes follow its first */
            struct live_section *col = live_sec(L, cx, 0, cz, 0);
            if (!col) continue;
            for (int s = 0; s < 16; ++s) {
                struct live_section *e0 = col + 2 * s, *e1 = e0 + 1;
                if (!e0->count && !e1->count) continue;
                if (live_culled(&cull, cx, s, cz)) continue;
                if (e0->count) L->opaque[L->nopaque++] = (struct live_draw){cx, s, cz, e0, NULL};
                if (e1->count) {
                    double dx = cx * 16 + 8 - f->cam[0];
                    double dy = s * 16 + 8 - f->cam[1];
                    double dz = cz * 16 + 8 - f->cam[2];
                    keys[nkeys] = (struct live_water_key){dx * dx + dy * dy + dz * dz, nkeys};
                    wlist[nkeys++] = (struct live_draw){cx, s, cz, e1, NULL};
                }
            }
        }
    qsort(keys, (size_t)nkeys, sizeof *keys, live_water_compare);
    for (int i = 0; i < nkeys; ++i) {
        struct live_draw d = wlist[keys[i].index];
        int count = d.sec->count / 4;
        if (!d.sec->raw) {
            /* the device's mesh: the device orders its quads */
            L->water[L->nwater++] = d;
            continue;
        }
        d.order = alloc((size_t)(count ? count : 1) * sizeof *d.order);
        float x = (float)f->cam[0] - d.cx * 16, y = (float)f->cam[1] - d.s * 16,
              z = (float)f->cam[2] - d.cz * 16;
        for (int q = 0; q < count; ++q) {
            d.order[q].offset = q * 4;
            d.order[q].distance = live_quad_distance(d.sec->raw, q * 4, x, y, z);
        }
        qsort(d.order, (size_t)count, sizeof *d.order, water_quad_compare);
        L->water[L->nwater++] = d;
    }
    free(keys); free(wlist); free(grid);

    double t1 = live_now_ms();
    if (raster_rec_only()) {
        /* the lists and the sky are what raster_live_obs exports as data:
         * nothing to record or draw here */
        struct raster_live_stats *st = &L->stats;
        memset(st, 0, sizeof *st);
        st->mesh_ms = tmesh;
        st->feed_ms = feed_ms;
        st->lists_ms = t1 - t0 - tmesh;
        st->meshed = meshed;
        st->reused = reused;
        st->empty = empty;
        st->checked = atomic_load(&L->checked);
        st->check_bad = atomic_load(&L->check_bad);
        st->draws = L->nopaque + L->nwater;
        return meshed;
    }
    /* the sky passes, recorded here: a few thousand triangles */
    L->sky.n = 0;
    f->sink = &L->sky;
    if (f->dimension == 1) { if (f->end_sky) draw_end_sky(f); }
    else if (f->dimension == 0) {
        draw_sky(f, 1);
        draw_sunrise(f);
        raster_sky2_draw(f, f->celestial_angle, f->moon_phase, celestial_quad);
        if (f->star > 0.0f) raster_sky2_stars(f, f->celestial_angle, celestial_quad);
        draw_sky(f, 2);
        if (f->clouds_enabled) draw_clouds(f);
    }
    f->sink = NULL;
    f->sky_mode = f->cloud_prepass = f->render_pass = 0;

    double t2 = live_now_ms();
    live_stage(L, live_record_run);
    double t3 = live_now_ms();
    live_stage(L, live_band_run);
    double t4 = live_now_ms();
    struct raster_live_stats *st = &L->stats;
    st->mesh_ms = tmesh;
    st->feed_ms = feed_ms;
    st->lists_ms = t1 - t0 - tmesh;   /* t0 is before the mesh stage */
    st->sky_ms = t2 - t1;
    st->record_ms = t3 - t2;
    st->raster_ms = t4 - t3;
    st->meshed = meshed;
    st->reused = reused;
    st->empty = empty;
    st->checked = atomic_load(&L->checked);
    st->check_bad = atomic_load(&L->check_bad);
    st->draws = L->nopaque + L->nwater;
    st->triangles = L->sky.n;
    for (int i = 0; i < L->nopaque + L->nwater; ++i) st->triangles += L->lists[i].n;
    return meshed;
}

void raster_live_keep(struct raster_live *L, int on) { L->keep = on; }

void raster_live_set_prec(struct raster_live *L, int prec) { L->prec = prec; }

void raster_live_mesh_check(struct raster_live *L, int on)
{
    L->keycheck = on;
    meshkey_cache_verify(L->mkc, on);
}

void raster_live_free(struct raster_live *L)
{
    if (!L) return;
    size_t n = (size_t)L->world->rows * L->world->rows * 32;
    for (size_t i = 0; i < n; ++i) free(L->sec[i].raw);
    for (int i = 0; i < L->nwater; ++i) free(L->water[i].order);
    for (int i = 0; i < L->caplists; ++i) { free(L->lists[i].v); free(L->lists[i].rows); free(L->lists[i].mode); }
    free(L->lists); free(L->sky.v); free(L->sky.rows); free(L->sky.mode);
    if (L->npool) {
        atomic_store_explicit(&L->stage_id, UINT_MAX, memory_order_release);
        for (int i = 0; i < L->npool; ++i) pthread_join(L->th[i], NULL);
    }
    free(L->workers);
    free(L->obs_draws); free(L->obs_order); free(L->obs_stars);
    pthread_mutex_destroy(&L->lock);
    free(L->sec); free(L->pend); free(L->opaque); free(L->water); free(L->depth); free(L->mask);
    free(L->sun); free(L->moon); free(L->clouds); free(L->end_sky);
    free(L->rain_tex); free(L->snow_tex); free(L->underwater_tex);
    free(L->particle_tex); free(L->explosion_tex);
    free(L->block_atlas);
    raster_texture_free(&L->texture);
    for (int i = 0; i < L->threads; ++i) { rb_mesher_free(&L->mesher[i]); rb_tess_free(&L->tess[i]); }
    free(L->mesher); free(L->tess); free(L->stale);
    meshfeed_free(L->feed);
    meshkey_cache_free(L->mkc);
    free(L->feed_host); free(L->feed_prev); free(L->feed_counts); free(L->feed_keep); free(L->assets);
    free(L);
}

void raster_live_animate(struct raster_live *L, const struct texanim *a, const struct texanim_state *st)
{
    /* the mip levels are built once from level 0; with none the patch is the whole upload */
    if (!L->texture.levels)
        texanim_apply_memo(a, st, TEXANIM_BLOCKS, L->texture.rgba[0], L->texture.width, L->texture.height, &L->anim_memo[0]);
    /* what the patch can touch, for a renderer that keeps a copy (raster_obs) */
    L->nanim = 0;
    for (int i = 0; i < a->n[TEXANIM_BLOCKS] && L->nanim < RASTER_OBS_ANIM; ++i, ++L->nanim) {
        const struct texanim_sprite *sp = &a->s[TEXANIM_BLOCKS][i];
        L->anim[L->nanim][0] = sp->x; L->anim[L->nanim][1] = sp->y;
        L->anim[L->nanim][2] = sp->w; L->anim[L->nanim][3] = sp->h;
    }
    if (L->block_atlas)
        texanim_apply_memo(a, st, TEXANIM_BLOCKS, L->block_atlas, L->block_atlas_w, L->block_atlas_h, &L->anim_memo[1]);
}

/* ------------------------------------------------------------ the frame as data
 *
 * raster_obs.h: the last raster_live_frame's inputs and lists, for a
 * renderer that draws them itself. Every derived number comes from the
 * helpers the passes above call. */
int raster_live_obs(struct raster_live *L, struct raster_obs *o)
{
    const struct frame *f = &L->base;
    if (L->texture.levels) return -1;
    memset(o, 0, sizeof *o);
    o->w = f->w; o->h = f->h;
    o->prec = f->prec;
    memcpy(o->proj, f->proj, sizeof o->proj);
    memcpy(o->mv, f->mv, sizeof o->mv);
    for (int i = 0; i < 3; ++i) { o->cam[i] = f->cam[i]; o->fog[i] = f->fog[i]; }
    o->fogs = f->fogs; o->foge = f->foge; o->fogd = f->fogd; o->fogm = f->fogm;
    memcpy(o->lm, f->lm, sizeof o->lm);
    struct raster_obs_sky *k = &o->sky;
    for (int i = 0; i < 3; ++i) {
        k->sky[i] = f->sky[i]; k->sky_bottom[i] = f->sky_bottom[i];
        k->fog[i] = f->fog[i]; k->cloud_color[i] = f->cloud_color[i];
    }
    k->sky_far = f->sky_far; k->sky_floor = f->sky_floor;
    k->cloud_height = f->cloud_height; k->star = f->star;
    for (int i = 0; i < 4; ++i) k->sunrise[i] = f->sunrise[i];
    k->rise_dir = sunrise_dir(f);
    for (int i = 0; i < 4; ++i) k->rise_center[i] = sunrise_center(f, i);
    for (int i = 0; i <= 16; ++i) sunrise_rim(i, &k->rise_sc[i][0], &k->rise_sc[i][1]);
    raster_sky2_rotation(f->celestial_angle, &k->cel_cs, &k->cel_sn);
    int col = f->moon_phase % 4, row = f->moon_phase / 4;
    k->moon_uv[0] = (float)(col + 1) / 4.0f; k->moon_uv[1] = (float)(row + 1) / 2.0f;
    k->moon_uv[2] = (float)col / 4.0f; k->moon_uv[3] = (float)row / 2.0f;
    cloud_origin(f, &k->cloud_ix, &k->cloud_iz, &k->cloud_fx, &k->cloud_fz);
    k->celestial_angle = f->celestial_angle;
    k->moon_phase = f->moon_phase;
    k->cloud_tick = f->cloud_tick;
    k->dimension = f->dimension == 1 && !L->end_sky ? 2 : f->dimension;
    k->clouds = f->clouds_enabled;
    k->stars = f->star > 0.0f;
    size_t n = (size_t)L->nopaque + L->nwater, norder = 0;
    for (int i = 0; i < L->nwater; ++i)
        if (L->water[i].order) norder += (size_t)L->water[i].sec->count / 4;
    if (n > L->obs_cap) {
        free(L->obs_draws);
        L->obs_draws = alloc(n * sizeof *L->obs_draws);
        L->obs_cap = n;
    }
    if (norder > L->obs_order_cap) {
        free(L->obs_order);
        L->obs_order = alloc(norder * sizeof *L->obs_order);
        L->obs_order_cap = norder;
    }
    size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        int water = i >= (size_t)L->nopaque;
        const struct live_draw *d = water ? &L->water[i - L->nopaque] : &L->opaque[i];
        struct raster_obs_draw *e = &L->obs_draws[i];
        e->cx = d->cx; e->s = d->s; e->cz = d->cz;
        e->flags = d->sec->flags; e->count = d->sec->count;
        e->key = (uint32_t)(d->sec - L->sec);
        e->version = d->sec->version;
        e->raw = d->sec->raw;
        e->order = NULL;
        e->device = L->dmesh != 0;
        if (water && d->order) {
            int q = d->sec->count / 4;
            for (int j = 0; j < q; ++j) L->obs_order[at + j] = d->order[j].offset;
            e->order = L->obs_order + at;
            at += (size_t)q;
        }
    }
    o->nopaque = L->nopaque; o->nwater = L->nwater;
    o->opaque = L->obs_draws; o->water = L->obs_draws + L->nopaque;
    o->atlas = L->texture.rgba[0];
    o->atlas_w = L->texture.width; o->atlas_h = L->texture.height;
    o->sun = L->sun; o->moon = L->moon; o->clouds = L->clouds; o->end_sky = L->end_sky;
    if (!L->obs_stars) {
        int ns = 0;
        const struct raster_sky2_vertex *st = raster_sky2_star_table(&ns);
        L->obs_stars = alloc((size_t)(ns ? ns : 1) * 12 * sizeof(float));
        for (int i = 0; i < ns * 4; ++i) {
            L->obs_stars[i * 3] = st[i].x;
            L->obs_stars[i * 3 + 1] = st[i].y;
            L->obs_stars[i * 3 + 2] = st[i].z;
        }
        L->nstars_obs = ns;
    }
    o->stars = L->obs_stars;
    o->nstars = L->nstars_obs;
    o->nanim = L->nanim;
    memcpy(o->anim, L->anim, sizeof o->anim);
    if (L->dmesh) {
        o->mesh = &L->feed_out;
        o->mesh_host = L->dmesh == 2 ? L->feed_host : NULL;
        meshfeed_take(L->feed);
    }
    o->mesher = &L->mesher[0];
    o->assets = L->assets;
    return 0;
}

/* raster_obs_render's sections [a, b) (the opaque ones, then the translucent) */
static void obs_sections(struct frame *f, const struct raster_obs *o, int a, int b)
{
    for (int i = a; i < b; ++i) {
        const struct raster_obs_draw *d = i < o->nopaque ? &o->opaque[i] : &o->water[i - o->nopaque];
        struct live_section sec = {1, d->flags, d->count, d->version, (int32_t *)d->raw, MESHKEY_NONE, {{0, 0}}};
        struct water_quad *order = NULL;
        if (d->order) {
            order = alloc((size_t)(d->count / 4 ? d->count / 4 : 1) * sizeof *order);
            for (int q = 0; q < d->count / 4; ++q) order[q] = (struct water_quad){d->order[q], 0.0f};
        }
        struct live_draw ld = {d->cx, d->s, d->cz, &sec, order};
        f->render_pass = i >= o->nopaque;
        live_draw_section(f, &ld);
        free(order);
    }
    f->render_pass = 0;
}

void raster_obs_render(const struct raster_obs *o, unsigned char *rgb, float *depth)
{
    struct frame f;
    memset(&f, 0, sizeof f);
    f.w = o->w; f.h = o->h;
    f.prec = o->prec;
    int eprec = raster_entity_prec();
    raster_entity_set_prec(o->prec);
    memcpy(f.proj, o->proj, sizeof f.proj);
    memcpy(f.mv, o->mv, sizeof f.mv);
    for (int i = 0; i < 3; ++i) { f.cam[i] = o->cam[i]; f.fog[i] = o->fog[i]; }
    f.fogs = o->fogs; f.foge = o->foge; f.fogd = o->fogd; f.fogm = o->fogm;
    memcpy(f.lm, o->lm, sizeof f.lm);
    const struct raster_obs_sky *k = &o->sky;
    for (int i = 0; i < 3; ++i) {
        f.sky[i] = k->sky[i]; f.sky_bottom[i] = k->sky_bottom[i]; f.cloud_color[i] = k->cloud_color[i];
    }
    f.sky_far = k->sky_far; f.sky_floor = k->sky_floor; f.cloud_height = k->cloud_height;
    f.star = k->star;
    for (int i = 0; i < 4; ++i) f.sunrise[i] = k->sunrise[i];
    f.celestial_angle = k->celestial_angle; f.moon_phase = k->moon_phase; f.cloud_tick = k->cloud_tick;
    f.dimension = k->dimension; f.clouds_enabled = k->clouds;
    f.texture.width = o->atlas_w; f.texture.height = o->atlas_h;
    f.texture.levels = 0; f.texture.min_filter = 9728;
    f.texture.rgba[0] = (unsigned char *)o->atlas;
    f.sun = (unsigned char *)o->sun; f.moon = (unsigned char *)o->moon;
    f.clouds = (unsigned char *)o->clouds; f.end_sky = (unsigned char *)o->end_sky;
    f.rgb = rgb; f.depth = depth;
    /* the band stage's clear; triangle() writes the mask only for coverage */
    unsigned char *mask = malloc((size_t)o->w * o->h);
    if (!mask) exit(1);
    f.mask = mask;
    for (size_t i = 0; i < (size_t)o->w * o->h; ++i) {
        depth[i] = 1.0f;
        for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = byte(f.fog[c]);
    }
    if (f.dimension == 1) draw_end_sky(&f);
    else if (f.dimension == 0) {
        draw_sky(&f, 1);
        draw_sunrise(&f);
        raster_sky2_draw(&f, f.celestial_angle, f.moon_phase, celestial_quad);
        if (f.star > 0.0f) raster_sky2_stars(&f, f.celestial_angle, celestial_quad);
        draw_sky(&f, 2);
        if (f.clouds_enabled) draw_clouds(&f);
    }
    f.sky_mode = f.cloud_prepass = f.render_pass = 0;
    obs_sections(&f, o, 0, o->nopaque);
    /* the recorded passes; without them the translucent sections follow */
    int water = 0;
    /* the model box and the flat item whose quads come next
     * (raster_ebox_quads and raster_eitem_quad make them) */
    int bi = 0, bq_first = -1, ii = 0;
    struct entity_clip_vertex bq[6][4], iq[4];
    for (int c = 0; c < o->ncmd; ++c) {
        const struct raster_obs_cmd *cmd = &o->cmd[c];
        if (cmd->kind == RASTER_CMD_WATER) { obs_sections(&f, o, o->nopaque, o->nopaque + o->nwater); water = 1; }
        else if (cmd->kind == RASTER_CMD_CLEAR_DEPTH)
            for (size_t i = 0; i < (size_t)o->w * o->h; ++i) depth[i] = 1.0f;
        else if (cmd->kind == RASTER_CMD_QUAD)
            for (int q = cmd->first; q < cmd->first + cmd->count; ++q) {
                const struct raster_obs_equad *e = &o->equad[q];
                const struct raster_obs_estate *st = &o->estate[e->state];
                struct entity_raster_target t = {.w = o->w, .h = o->h, .proj = o->proj, .mv = o->mv, .cam = o->cam,
                    .fog = st->fog, .fogs = st->fogs, .foge = st->foge, .fogd = st->fogd, .fogm = st->fogm,
                    .lm = st->lm >= 0 ? o->lm_table[st->lm] : NULL, .depth = depth, .rgb = rgb,
                    .overlay = st->overlay, .overlay_brightness = st->overlay_brightness,
                    .overlay_lightmap = st->overlay_lightmap, .blend = st->blend, .alpha = st->alpha,
                    .clamp_texture = st->clamp_texture, .unlit = st->unlit, .linear_depth = st->linear_depth,
                    .cull_front = st->cull_front, .depth_equal = st->depth_equal, .alpha_ref = st->alpha_ref,
                    .vertex_color = st->vertex_color, .overlay_alpha = st->overlay_alpha,
                    .overlay_has_rgb = st->overlay_has_rgb, .two_sided = st->two_sided,
                    .no_light = st->no_light, .no_alpha = st->no_alpha, .vertex_alpha = st->vertex_alpha,
                    .alpha_cut = st->alpha_cut, .depth_lequal = st->depth_lequal};
                for (int k = 0; k < 3; ++k) { t.overlay_rgb[k] = st->overlay_rgb[k]; t.material[k] = st->material[k]; }
                const struct raster_obs_tex *tx = &o->tex[e->tex];
                const struct entity_clip_vertex *v = e->v;
                while (bi < o->nebox && o->ebox[bi].first + 6 <= q) ++bi;
                if (bi < o->nebox && o->ebox[bi].first <= q) {
                    const struct raster_obs_ebox *b = &o->ebox[bi];
                    if (bq_first != b->first) {
                        raster_ebox_quads(b, o->ebox_xf[b->xf], o->ebox_xf[b->xf] + 16, bq);
                        bq_first = b->first;
                    }
                    v = bq[q - b->first];
                }
                while (ii < o->neitem && o->eitem[ii].first + raster_eitem_quads(&o->eitem[ii]) <= q) ++ii;
                if (ii < o->neitem && o->eitem[ii].first <= q) {
                    const struct raster_obs_eitem *it = &o->eitem[ii];
                    raster_eitem_quad(it, o->ebox_xf[it->xf], o->ebox_xf[it->xf] + 16, q - it->first, iq);
                    v = iq;
                }
                raster_entity_quad(&t, tx->rgba, tx->w, tx->h, v);
            }
        else if (cmd->kind == RASTER_CMD_PORTAL)
            for (int q = cmd->first; q < cmd->first + cmd->count; ++q) {
                const struct raster_obs_portal *pp = &o->portal[q];
                struct entity_raster_target t = {.w = o->w, .h = o->h, .depth = depth, .rgb = rgb};
                const struct raster_obs_tex *tx = &o->tex[pp->tex];
                raster_tileent_portal_triangle(&t, pp, tx->rgba, tx->w, tx->h);
            }
        else if (cmd->kind == RASTER_CMD_CRACK)
            for (int q = cmd->first; q < cmd->first + cmd->count; ++q) {
                const struct raster_obs_crack *k = &o->crack[q];
                struct raster_overlay_in in = {0};
                in.w = o->w; in.h = o->h; in.depth = depth; in.rgb = rgb; in.texture = &f.texture;
                in.fog = k->fog_on ? k->fog : NULL;
                in.fogs = k->fogs; in.foge = k->foge; in.fogd = k->fogd; in.fogm = k->fogm;
                raster_overlay_crack_triangle(&in, k->p);
            }
        else if (cmd->kind == RASTER_CMD_LINE)
            for (int q = cmd->first; q < cmd->first + cmd->count; ++q) {
                const struct raster_obs_line *l = &o->line[q];
                struct raster_overlay_in in = {0};
                in.w = o->w; in.h = o->h; in.depth = depth; in.rgb = rgb;
                in.fog = l->fog_on ? l->fog : NULL;
                in.fogs = l->fogs; in.foge = l->foge; in.fogd = l->fogd; in.fogm = l->fogm;
                raster_overlay_line(&in, l);
            }
    }
    if (!water) obs_sections(&f, o, o->nopaque, o->nopaque + o->nwater);
    f.render_pass = 0;
    raster_entity_set_prec(eprec);
    free(mask);
}
