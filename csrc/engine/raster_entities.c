#define _POSIX_C_SOURCE 200809L
#include "raster_entities.h"
#include "raster_itemframe.h"
#include "texanim.h"
#include "raster_entity_quad.h"
#include "raster_prec.h"
#include "raster_obs.h"
#include "nbtjson.h"
#include "render_blocks.h"
#include "block_item_model.h"
#include "jrand.h"
#include "jmath.h"
#include "tape.h"
#include "raster.h"   /* raster_hud_item_entry */
#include "firework.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct painting_art {
    const char *title;
    int size_x, size_y, offset_x, offset_y;
};

static const struct painting_art ARTS[] = {
    {"Kebab", 16, 16, 0, 0},
    {"Aztec", 16, 16, 16, 0},
    {"Alban", 16, 16, 32, 0},
    {"Aztec2", 16, 16, 48, 0},
    {"Bomb", 16, 16, 64, 0},
    {"Plant", 16, 16, 80, 0},
    {"Wasteland", 16, 16, 96, 0},
    {"Pool", 32, 16, 0, 32},
    {"Courbet", 32, 16, 32, 32},
    {"Sea", 32, 16, 64, 32},
    {"Sunset", 32, 16, 96, 32},
    {"Creebet", 32, 16, 128, 32},
    {"Wanderer", 16, 32, 0, 64},
    {"Graham", 16, 32, 16, 64},
    {"Match", 32, 32, 0, 128},
    {"Bust", 32, 32, 32, 128},
    {"Stage", 32, 32, 64, 128},
    {"Void", 32, 32, 96, 128},
    {"SkullAndRoses", 32, 32, 128, 128},
    {"Wither", 32, 32, 160, 128},
    {"Fighters", 64, 32, 0, 96},
    {"Pointer", 64, 64, 0, 192},
    {"Pigscene", 64, 64, 64, 192},
    {"BurningSkull", 64, 64, 128, 192},
    {"Skeleton", 64, 48, 192, 64},
    {"DonkeyKong", 64, 48, 192, 112},
    {NULL, 0, 0, 0, 0}
};

static const struct painting_art *find_art(const char *title)
{
    if (!title) return &ARTS[0];
    for (int i = 0; ARTS[i].title; ++i) {
        if (!strcmp(ARTS[i].title, title)) return &ARTS[i];
    }
    return &ARTS[0];
}

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static int entity_overlay;
static float entity_overlay_brightness;
static int entity_overlay_lightmap;
static float entity_overlay_alpha = 0.4f;
static int entity_overlay_has_rgb;
static float entity_overlay_rgb[3];
static float entity_material[3] = {1, 1, 1};
static int entity_blend;
static int entity_unlit;
static float entity_alpha;
static int entity_clamp;
static int entity_linear_depth;
static int entity_cull_front;
static int entity_depth_equal;
static float entity_alpha_ref;
static int entity_vertex_color;
static int entity_two_sided;
static int entity_no_light;
static int entity_no_alpha;
static int entity_alpha_cut;
static int entity_vertex_alpha;
static int entity_depth_lequal;
/* raster_prec.h: the stages this process draws fast (0: exact) */
static int entity_prec;

void raster_entity_set_prec(int prec) { entity_prec = prec; }
int raster_entity_prec(void) { return entity_prec; }

static unsigned char byte(float x)
{
    x = clamp01(x);
    return (unsigned char)lrintf(x * 255.0f);
}

static float edge(const struct entity_clip_vertex *a, const struct entity_clip_vertex *b, float x, float y)
{
    return (x - a->x) * (b->y - a->y) - (y - a->y) * (b->x - a->x);
}

/* The coverage test on snapped coordinates (multiples of 1/256 plus 0.5): in
 * double the products are exact, so a shared edge evaluates to exact
 * negatives from its two triangles and the top-left rule leaves no crack. */
static double edge_exact(const struct entity_clip_vertex *a, const struct entity_clip_vertex *b,
                         double x, double y)
{
    return (x - a->x) * ((double)b->y - a->y) - (y - a->y) * ((double)b->x - a->x);
}

static int top_left(const struct entity_clip_vertex *a, const struct entity_clip_vertex *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    return dy < 0 || (dy == 0 && dx > 0);
}

static float compute_diffuse(float nx, float ny, float nz)
{
    /* RenderHelper.enableStandardItemLighting():
     * L0 = normalize(0.2, 1.0, -0.7)
     * L1 = normalize(-0.2, 1.0, 0.7)
     * ambient = 0.4, diffuse = 0.6 */
    static const float l0[3] = {0.20000000298023224f / 1.236931687687298f,
                                1.0f / 1.236931687687298f,
                                -0.699999988079071f / 1.236931687687298f};
    static const float l1[3] = {-0.20000000298023224f / 1.236931687687298f,
                                1.0f / 1.236931687687298f,
                                0.699999988079071f / 1.236931687687298f};
    float dot0 = nx * l0[0] + ny * l0[1] + nz * l0[2];
    float dot1 = nx * l1[0] + ny * l1[1] + nz * l1[2];
    float d0 = dot0 > 0.0f ? dot0 : 0.0f;
    float d1 = dot1 > 0.0f ? dot1 : 0.0f;
    float diff = 0.4f + 0.6f * d0 + 0.6f * d1;
    return diff < 0.0f ? 0.0f : diff > 1.0f ? 1.0f : diff;
}

float raster_entity_diffuse(float nx, float ny, float nz) { return compute_diffuse(nx, ny, nz); }

/* The lightmap texture: 16x16, GL_LINEAR, GL_CLAMP (not to the edge), under
 * the texture matrix scale(1/256) translate(8, 8, 8). Inside the texture the
 * coordinate (c + 8) / 256 lands on texel c / 16; past it, GL_CLAMP's linear
 * filter mixes the edge texel with the black border, so the eye passes'
 * 61680 (block coordinate 61680, sky 0) reads half the block-15 texel. */
static void light_sample(const uint32_t lm[256], float block, float sky, float out[3])
{
    float x = block / 16.0f, y = sky / 16.0f;
    if (x > 15.5f) x = 15.5f;
    if (y > 15.5f) y = 15.5f;
    /* the filter's weights are 8-bit: a coordinate within 1/256 of a texel
     * centre takes that texel alone */
    if (x > 15.0f && x < 15.0f + 1.0f / 256.0f) x = 15.0f;
    if (y > 15.0f && y < 15.0f + 1.0f / 256.0f) y = 15.0f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 + 1, y1 = y0 + 1;
    float fx = x - x0, fy = y - y0;
    uint32_t c[4] = {lm[y0 * 16 + (x0 > 15 ? 15 : x0)], x1 > 15 ? 0u : lm[y0 * 16 + x1],
                     y1 > 15 ? 0u : lm[y1 * 16 + (x0 > 15 ? 15 : x0)],
                     x1 > 15 || y1 > 15 ? 0u : lm[y1 * 16 + x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = ((c[0] >> shift) & 255) * (1 - fx) + ((c[1] >> shift) & 255) * fx;
        float b = ((c[2] >> shift) & 255) * (1 - fx) + ((c[3] >> shift) & 255) * fx;
        out[i] = (a * (1 - fy) + b * fy) / 255.0f;
    }
}

/* raster_prec.h RP_LIGHT: light_sample in float16 */
static void light_sample_h(const uint32_t lm[256], float block, float sky, float out[3])
{
    float x = rp_h(block * 0.0625f), y = rp_h(sky * 0.0625f);
    if (x > 15.5f) x = 15.5f;
    if (y > 15.5f) y = 15.5f;
    if (x > 15.0f && x < 15.0f + 1.0f / 256.0f) x = 15.0f;
    if (y > 15.0f && y < 15.0f + 1.0f / 256.0f) y = 15.0f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 + 1, y1 = y0 + 1;
    float fx = rp_h(x - x0), fy = rp_h(y - y0), gx = rp_h(1 - fx), gy = rp_h(1 - fy);
    uint32_t c[4] = {lm[y0 * 16 + (x0 > 15 ? 15 : x0)], x1 > 15 ? 0u : lm[y0 * 16 + x1],
                     y1 > 15 ? 0u : lm[y1 * 16 + (x0 > 15 ? 15 : x0)],
                     x1 > 15 || y1 > 15 ? 0u : lm[y1 * 16 + x1]};
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * (2 - i);
        float a = rp_h(rp_h((float)((c[0] >> shift) & 255) * gx) + rp_h((float)((c[1] >> shift) & 255) * fx));
        float b = rp_h(rp_h((float)((c[2] >> shift) & 255) * gx) + rp_h((float)((c[3] >> shift) & 255) * fx));
        out[i] = rp_h(rp_h(rp_h(a * gy) + rp_h(b * fy)) / 255.0f);
    }
}

/* raster_prec.h: an attribute's perspective interpolation in float16 from
 * the weights QH (b / w / iw, each rounded to half) */
static float interp_h(const float qh[3], float a0, float a1, float a2)
{
    float a = rp_h(qh[0] * rp_h(a0));
    a = rp_h(a + rp_h(qh[1] * rp_h(a1)));
    return rp_h(a + rp_h(qh[2] * rp_h(a2)));
}

static void screen_vertex(int w, int h, struct entity_clip_vertex *v)
{
    v->w = v->clip[3];
    v->x = (v->clip[0] / v->w * 0.5f + 0.5f) * w;
    v->y = (0.5f - v->clip[1] / v->w * 0.5f) * h;
    v->sx = rintf((v->x - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v->sy = rintf((v->y - 0.5f) * 256.0f) / 256.0f + 0.5f;
    v->depth = v->clip[2] / v->w * 0.5f + 0.5f;
}

static struct entity_clip_vertex interpolate(const struct entity_clip_vertex *a,
                                             const struct entity_clip_vertex *b, float t)
{
    struct entity_clip_vertex v = *a;
    for (int i = 0; i < 4; ++i)
        v.clip[i] = a->clip[i] + t * (b->clip[i] - a->clip[i]);
    for (int i = 0; i < 2; ++i)
        v.light[i] = a->light[i] + t * (b->light[i] - a->light[i]);
    v.diffuse = a->diffuse + t * (b->diffuse - a->diffuse);
    if (entity_vertex_color)
        for (int i = 0; i < 4; ++i)
            v.color[i] = a->color[i] + t * (b->color[i] - a->color[i]);
    v.fogcoord = a->fogcoord + t * (b->fogcoord - a->fogcoord);
    v.u = a->u + t * (b->u - a->u);
    v.v = a->v + t * (b->v - a->v);
    v.alpha = a->alpha + t * (b->alpha - a->alpha);
    return v;
}

static void raster_triangle(int w, int h, const float fog[3], float fogs, float foge,
                            float fogd, int fogm, const uint32_t lm[256],
                            const unsigned char *tex, int tex_w, int tex_h,
                            float *depth, unsigned char *rgb,
                            unsigned long *triangles, unsigned long *samples,
                            const struct entity_clip_vertex in[3])
{
    struct entity_clip_vertex v[3] = {in[0], in[1], in[2]};
    float area = edge(&v[0], &v[1], v[2].x, v[2].y);
    if (entity_cull_front)
    {
        /* glCullFace(GL_FRONT): the mirrored model keeps the other side. */
        if (!(area < 0.0f)) return;
        struct entity_clip_vertex t = v[0];
        v[0] = v[1];
        v[1] = t;
        area = -area;
    }
    else if (area == 0.0f) return;
    else if (area < 0.0f)
    {
        if (!entity_two_sided) return;
        /* GL_CULL_FACE is off (RendererLivingEntity turns it off around the
         * model), so a face's winding does not decide whether it is drawn. Flip
         * the triangle and keep the one interior test. */
        struct entity_clip_vertex flip = v[1];
        v[1] = v[2];
        v[2] = flip;
        area = -area;
    }
    struct entity_clip_vertex s[3] = {v[0], v[1], v[2]};
    for (int i = 0; i < 3; ++i) { s[i].x = v[i].sx; s[i].y = v[i].sy; }
    float minx = fminf(s[0].x, fminf(s[1].x, s[2].x));
    float maxx = fmaxf(s[0].x, fmaxf(s[1].x, s[2].x));
    float miny = fminf(s[0].y, fminf(s[1].y, s[2].y));
    float maxy = fmaxf(s[0].y, fmaxf(s[1].y, s[2].y));
    int x0 = (int)fmaxf(0, floorf(minx));
    int x1 = (int)fminf(w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny));
    int y1 = (int)fminf(h - 1, ceilf(maxy));
    if (triangles) ++(*triangles);
    /* raster_prec.h RP_RCP: the weights by reciprocals */
    const int rcp = (entity_prec & RP_RCP) != 0;
    float ra = 0, rw[3] = {0, 0, 0};
    if (rcp) {
        ra = 1.0f / area;
        for (int i = 0; i < 3; ++i) rw[i] = 1.0f / v[i].w;
    }
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float sx = x + 0.5f, sy = y + 0.5f;
        double e0, e1, e2;
        if (entity_prec & RP_EDGE) {
            /* raster_prec.h: the edge test in float */
            e0 = edge(&s[1], &s[2], sx, sy);
            e1 = edge(&s[2], &s[0], sx, sy);
            e2 = edge(&s[0], &s[1], sx, sy);
        } else {
            e0 = edge_exact(&s[1], &s[2], sx, sy);
            e1 = edge_exact(&s[2], &s[0], sx, sy);
            e2 = edge_exact(&s[0], &s[1], sx, sy);
        }
        if (e0 < 0 || (e0 == 0 && !top_left(&s[1], &s[2]))) continue;
        if (e1 < 0 || (e1 == 0 && !top_left(&s[2], &s[0]))) continue;
        if (e2 < 0 || (e2 == 0 && !top_left(&s[0], &s[1]))) continue;
        float b[3], bw[3] = {0, 0, 0}, iw, ri = 0;
        if (rcp) {
            b[0] = edge(&v[1], &v[2], sx, sy) * ra;
            b[1] = edge(&v[2], &v[0], sx, sy) * ra;
            b[2] = edge(&v[0], &v[1], sx, sy) * ra;
            for (int i = 0; i < 3; ++i) bw[i] = b[i] * rw[i];
            iw = bw[0] + bw[1] + bw[2];
            ri = 1.0f / iw;
        } else {
            b[0] = edge(&v[1], &v[2], sx, sy) / area;
            b[1] = edge(&v[2], &v[0], sx, sy) / area;
            b[2] = edge(&v[0], &v[1], sx, sy) / area;
            iw = b[0] / v[0].w + b[1] / v[1].w + b[2] / v[2].w;
        }
/* a vertex attribute's perspective interpolation: exact, or by the
 * reciprocals (RP_RCP) */
#define PI(a) (rcp ? (bw[0] * v[0].a + bw[1] * v[1].a + bw[2] * v[2].a) * ri \
                   : (b[0] / v[0].w * v[0].a + b[1] / v[1].w * v[1].a + b[2] / v[2].w * v[2].a) / iw)
        /* window z is affine in screen space: no perspective division */
        /* window z is affine in screen space; the older passes' budgets were
         * measured with it interpolated perspective-correct, the hand's with
         * the linear form (entity_linear_depth) */
        float z = entity_linear_depth
                      ? b[0] * v[0].depth + b[1] * v[1].depth + b[2] * v[2].depth
                      : PI(depth);
        int k = y * w + x;
        if (z < 0.0f || (entity_depth_equal ? fabsf(z - depth[k]) > 1e-7f :
                        entity_overlay ? fabsf(z - depth[k]) > 1e-7f :
                        entity_blend || entity_depth_lequal ? z > depth[k] : z >= depth[k])) continue;

        float u = PI(u);
        float v_coord = PI(v);

        int tx = (int)floorf(u * tex_w), ty = (int)floorf(v_coord * tex_h);
        if (entity_clamp) {
            if (tx < 0) tx = 0; else if (tx >= tex_w) tx = tex_w - 1;
            if (ty < 0) ty = 0; else if (ty >= tex_h) ty = tex_h - 1;
        } else {
            tx %= tex_w; ty %= tex_h;
            if (tx < 0) tx += tex_w;
            if (ty < 0) ty += tex_h;
        }
        const unsigned char *ptex = tex + (ty * tex_w + tx) * 4;
        if (!entity_overlay && !entity_no_alpha)
        {
            /* GL_ALPHA_TEST: the entity passes carry GL_GREATER 0.1 (or the
             * dragon death's deathTicks/200); the particle passes set
             * alpha_cut, in 1/255 texel alpha units, default 25 (0.098) */
            float ref = entity_alpha_cut ? entity_alpha_cut * (1.0f / 255.0f)
                                         : (entity_alpha_ref > 0.0f ? entity_alpha_ref : 0.1f);
            if (ptex[3] * (1.0f / 255.0f) <= ref) continue; /* GL_GREATER */
        }

        int pr = entity_prec;
        /* raster_prec.h's float16 stages interpolate by these weights */
        float qh[3] = {0, 0, 0};
        if (pr & RP_HALF)
            for (int i = 0; i < 3; ++i) qh[i] = rcp ? rp_h(bw[i] * ri) : rp_h(b[i] / v[i].w / iw);
        float light[3];
        if (entity_no_light) {
            light[0] = light[1] = light[2] = 1.0f;
        } else if (pr & RP_LIGHT) {
            light_sample_h(lm, interp_h(qh, v[0].light[0], v[1].light[0], v[2].light[0]),
                           interp_h(qh, v[0].light[1], v[1].light[1], v[2].light[1]), light);
        } else {
            float l0 = PI(light[0]);
            float l1 = PI(light[1]);
            light_sample(lm, l0, l1, light);
        }

        float diffuse = pr & RP_SHADE ? interp_h(qh, v[0].diffuse, v[1].diffuse, v[2].diffuse)
                                      : PI(diffuse);
        if (entity_unlit) diffuse = 1.0f;

        float distance = PI(fogcoord);
        float fog_factor = 1.0f;
        if (fogm == 9729) {
            fog_factor = clamp01((foge - distance) / (foge - fogs));
        } else if (fogm == 2048) {
            fog_factor = clamp01(expf(-fogd * distance));
        } else if (fogm == 2049) {
            float d = fogd * distance;
            fog_factor = clamp01(expf(-d * d));
        }
        float vcol[3] = {1.0f, 1.0f, 1.0f};
        float valpha = 1.0f;
        if (entity_vertex_color && (pr & RP_COLOR))
        {
            for (int c = 0; c < 3; ++c) vcol[c] = interp_h(qh, v[0].color[c], v[1].color[c], v[2].color[c]);
            valpha = interp_h(qh, v[0].color[3], v[1].color[3], v[2].color[3]);
        }
        else if (entity_vertex_color)
        {
            for (int c = 0; c < 3; ++c)
                vcol[c] = PI(color[c]);
            valpha = PI(color[3]);
        }
        for (int c = 0; c < 3; ++c) {
            float shade, val, src;
            if (pr & RP_SHADE) {
                /* raster_prec.h: the products in float16 */
                float dh = rp_h(diffuse);
                shade = entity_overlay ? (entity_overlay_has_rgb ? rp_h(rp_h(entity_overlay_rgb[c]) * dh) :
                                          c == 0 ? rp_h(rp_h(entity_overlay_brightness) * dh) : 0.0f)
                                       : rp_h(rp_h(entity_material[c]) * dh);
                if (entity_vertex_color) shade = rp_h(shade * rp_h(vcol[c]));
                shade = clamp01(shade);
                val = entity_overlay ? (entity_overlay_lightmap ? rp_h(shade * rp_h(light[c])) : shade)
                                     : rp_h(rp_h(rp_h(ptex[c] / 255.0f) * rp_h(light[c])) * shade);
            } else {
                shade = entity_overlay ? (entity_overlay_has_rgb ? entity_overlay_rgb[c] * diffuse :
                                          c == 0 ? entity_overlay_brightness * diffuse : 0.0f)
                                       : entity_material[c] * diffuse;
                if (entity_vertex_color) shade *= vcol[c];
                shade = clamp01(shade);
                val = entity_overlay ? (entity_overlay_lightmap ? shade * light[c] : shade)
                                     : (ptex[c] / 255.0f) * light[c] * shade;
            }
            if (pr & RP_FOG) {
                float fh = rp_h(fog_factor);
                src = rp_h(rp_h(rp_h(val) * fh) + rp_h(rp_h(fog[c]) * rp_h(1.0f - fh)));
            } else
                src = val * fog_factor + fog[c] * (1.0f - fog_factor);
            if (entity_overlay) src = src * entity_overlay_alpha + rgb[k * 3 + c] / 255.0f * (1.0f - entity_overlay_alpha);
            if (entity_blend == 5 || entity_blend == 6) {
                /* 8-bit blends with llvmpipe's rounded product: 5 is
                 * (GL_SRC_COLOR, GL_ONE), the item glint; 6 is (GL_SRC_ALPHA,
                 * GL_ONE) with the vertex alpha and depth writes, the
                 * lightning bolt */
                int s8 = byte(src);
                int f8 = s8;
                if (entity_blend == 6) {
                    float va = PI(alpha);
                    f8 = byte(va * ptex[3] / 255.0f);
                }
                int prod = s8 * f8;
                int add = (prod + 128 + ((prod + 128) >> 8)) >> 8;
                int d = rgb[k * 3 + c] + add;
                rgb[k * 3 + c] = (unsigned char)(d > 255 ? 255 : d);
                continue;
            }
            if (entity_blend == 4) {
                /* glBlendFunc(GL_SRC_ALPHA, GL_ONE) with the per-vertex glColor
                 * alpha (RenderDragon.renderEquippedItems' explosion fans). */
                src = fminf(1.0f, src * valpha + rgb[k * 3 + c] / 255.0f);
            } else if (entity_blend == 2 || entity_blend == 7) {
                src = fminf(1.0f, src + rgb[k * 3 + c] / 255.0f);
            } else if (entity_blend) {
                float va = entity_vertex_alpha
                    ? PI(alpha)
                    : entity_alpha;
                float a = va * ptex[3] / 255.0f;
                src = src * a + rgb[k * 3 + c] / 255.0f * (1.0f - a);
            }
            rgb[k * 3 + c] = byte(src);
        }
        if (!entity_overlay && (!entity_blend || entity_blend == 3 || entity_blend == 6 || entity_blend == 7) &&
            !entity_depth_equal) depth[k] = z;
        if (samples) ++(*samples);
    }
}
#undef PI

static void clip_and_draw(int w, int h, const float fog[3], float fogs, float foge,
                          float fogd, int fogm, const uint32_t lm[256],
                          const unsigned char *tex, int tex_w, int tex_h,
                          float *depth, unsigned char *rgb,
                          unsigned long *triangles, unsigned long *samples,
                          const struct entity_clip_vertex in[3])
{
    struct entity_clip_vertex a[16], b[16];
    memcpy(a, in, 3 * sizeof(*a));
    int n = 3;
    for (int plane = 0; plane < 6; ++plane) {
        int m = 0, axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i) {
            const struct entity_clip_vertex *p = &a[i], *q = &a[(i + 1) % n];
            float dp = p->clip[3] - sign * p->clip[axis];
            float dq = q->clip[3] - sign * q->clip[axis];
            if (dp >= 0) b[m++] = *p;
            if ((dp >= 0) != (dq >= 0)) b[m++] = interpolate(p, q, dp / (dp - dq));
        }
        n = m;
        if (n < 3) return;
        memcpy(a, b, n * sizeof(*a));
    }
    for (int i = 0; i < n; ++i) screen_vertex(w, h, &a[i]);
    for (int i = 1; i + 1 < n; ++i) {
        struct entity_clip_vertex t[3] = {a[0], a[i], a[i + 1]};
        raster_triangle(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                        depth, rgb, triangles, samples, t);
    }
}

static struct entity_clip_vertex make_clip_vertex_off(float lx, float ly, float lz,
                                                      float u, float v,
                                                      float yaw_deg,
                                                      double pos_x, double pos_y, double pos_z,
                                                      const double cam[3],
                                                      const float mv[16], const float proj[16],
                                                      float light_block, float light_sky,
                                                      float diffuse,
                                                      float wox, float woy, float woz)
{
    struct entity_clip_vertex vert = {0};
    vert.u = u;
    vert.v = v;
    vert.light[0] = light_block;
    vert.light[1] = light_sky;
    vert.diffuse = diffuse;
    vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 1.0f;
    float rad = yaw_deg * (float)(3.14159265358979323846 / 180.0);
    float cos_a = cosf(rad), sin_a = sinf(rad);
    float rx = lx * cos_a + lz * sin_a;
    float ry = ly;
    float rz = -lx * sin_a + lz * cos_a;
    float wx = rx + (float)(pos_x - cam[0]) + wox;
    float wy = ry + (float)(pos_y - cam[1]) + woy;
    float wz = rz + (float)(pos_z - cam[2]) + woz;
    float eye[4];
    for (int r = 0; r < 4; ++r) {
        eye[r] = mv[r] * wx + mv[4 + r] * wy + mv[8 + r] * wz + mv[12 + r];
    }
    for (int r = 0; r < 4; ++r) {
        vert.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] +
                       proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
    }
    vert.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return vert;
}

static struct entity_clip_vertex make_clip_vertex(float lx, float ly, float lz,
                                                  float u, float v,
                                                  float yaw_deg,
                                                  double pos_x, double pos_y, double pos_z,
                                                  const double cam[3],
                                                  const float mv[16], const float proj[16],
                                                  float light_block, float light_sky,
                                                  float diffuse)
{
    return make_clip_vertex_off(lx, ly, lz, u, v, yaw_deg, pos_x, pos_y, pos_z,
                                cam, mv, proj, light_block, light_sky, diffuse, 0.0f, 0.0f, 0.0f);
}

/* raster_obs.h's recorder: the quad with the state raster_triangle will
 * read (every path here draws through draw_quad) */
static void record_quad(const float fog[3], float fogs, float foge, float fogd, int fogm,
                        const uint32_t lm[256], const unsigned char *tex, int tex_w, int tex_h,
                        const struct entity_clip_vertex q[4])
{
    struct raster_obs_estate st;
    memset(&st, 0, sizeof st);
    for (int c = 0; c < 3; ++c) {
        st.fog[c] = fog[c];
        st.overlay_rgb[c] = entity_overlay_rgb[c];
        st.material[c] = entity_material[c];
    }
    st.fogs = fogs; st.foge = foge; st.fogd = fogd; st.fogm = fogm;
    st.overlay = entity_overlay; st.overlay_lightmap = entity_overlay_lightmap;
    st.overlay_has_rgb = entity_overlay_has_rgb; st.blend = entity_blend; st.unlit = entity_unlit;
    st.clamp_texture = entity_clamp; st.linear_depth = entity_linear_depth;
    st.cull_front = entity_cull_front; st.depth_equal = entity_depth_equal;
    st.vertex_color = entity_vertex_color; st.two_sided = entity_two_sided; st.no_light = entity_no_light;
    st.no_alpha = entity_no_alpha; st.alpha_cut = entity_alpha_cut; st.vertex_alpha = entity_vertex_alpha;
    st.depth_lequal = entity_depth_lequal;
    st.overlay_brightness = entity_overlay_brightness; st.overlay_alpha = entity_overlay_alpha;
    st.alpha = entity_alpha; st.alpha_ref = entity_alpha_ref;
    raster_rec_equad(&st, entity_no_light ? NULL : lm, tex, tex_w, tex_h, q);
}

#ifdef NETHERITE_MOB_CULL_CHECK
static int cull_probe = -1;
static long cull_probe_models, cull_probe_quads;
static void cull_probe_report(void)
{
    fprintf(stderr, "mob cull check: %ld culled models drawn as a probe, %ld quads, each wholly outside a clip plane\n",
            cull_probe_models, cull_probe_quads);
}
void raster_entity_cull_probe(int on, int kind)
{
    static int registered;
    if (!registered) { registered = 1; atexit(cull_probe_report); }
    cull_probe = on ? kind : -1;
    if (on) ++cull_probe_models;
}
/* clip_and_draw keeps a vertex with w - sign * c >= 0: a quad all of whose
 * vertices fail one plane draws nothing */
static void cull_probe_quad(const struct entity_clip_vertex q[4])
{
    ++cull_probe_quads;
    for (int plane = 0; plane < 6; ++plane)
    {
        float sign = plane & 1 ? -1.0f : 1.0f;
        int out = 0;
        for (int i = 0; i < 4; ++i) out += q[i].clip[3] - sign * q[i].clip[plane / 2] < 0;
        if (out == 4) return;
    }
    fprintf(stderr, "mob cull check: a culled model of kind %d has a quad in view\n", cull_probe);
    abort();
}
#endif

static void draw_quad(int w, int h, const float fog[3], float fogs, float foge,
                      float fogd, int fogm, const uint32_t lm[256],
                      const unsigned char *tex, int tex_w, int tex_h,
                      float *depth, unsigned char *rgb,
                      unsigned long *triangles, unsigned long *samples,
                      const struct entity_clip_vertex q[4])
{
#ifdef NETHERITE_MOB_CULL_CHECK
    if (cull_probe >= 0) { cull_probe_quad(q); return; }
#endif
    if (raster_rec_on()) record_quad(fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h, q);
    if (raster_rec_only()) return;
    struct entity_clip_vertex a[3] = {q[0], q[1], q[3]};
    struct entity_clip_vertex b[3] = {q[1], q[2], q[3]};
    clip_and_draw(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                  depth, rgb, triangles, samples, a);
    clip_and_draw(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                  depth, rgb, triangles, samples, b);
}

void raster_entity_estate(const struct entity_raster_target *t, struct raster_obs_estate *st, const uint32_t **lm)
{
    /* raster_entity_quad's globals, then record_quad's copy of them */
    memset(st, 0, sizeof *st);
    for (int c = 0; c < 3; ++c) {
        st->fog[c] = t->fog[c];
        st->overlay_rgb[c] = t->overlay_rgb[c];
        st->material[c] = t->material[c];
    }
    st->fogs = t->fogs; st->foge = t->foge; st->fogd = t->fogd; st->fogm = t->fogm;
    st->overlay = t->overlay; st->overlay_lightmap = t->overlay_lightmap;
    st->overlay_has_rgb = t->overlay_has_rgb; st->blend = t->blend; st->unlit = t->unlit;
    st->clamp_texture = t->clamp_texture; st->linear_depth = t->linear_depth;
    st->cull_front = t->cull_front; st->depth_equal = t->depth_equal;
    st->vertex_color = t->vertex_color; st->two_sided = t->two_sided; st->no_light = t->no_light;
    st->no_alpha = t->no_alpha; st->alpha_cut = t->alpha_cut; st->vertex_alpha = t->vertex_alpha;
    st->depth_lequal = t->depth_lequal;
    st->overlay_brightness = t->overlay_brightness;
    st->overlay_alpha = t->overlay_alpha > 0.0f ? t->overlay_alpha : 0.4f;
    st->alpha = t->alpha; st->alpha_ref = t->alpha_ref;
    *lm = t->no_light ? NULL : t->lm;
}

void raster_entity_quad(const struct entity_raster_target *t,
                        const unsigned char *texture, int texture_w, int texture_h,
                        const struct entity_clip_vertex vertex[4])
{
    entity_overlay = t->overlay;
    entity_overlay_brightness = t->overlay_brightness;
    entity_overlay_lightmap = t->overlay_lightmap;
    entity_overlay_alpha = t->overlay_alpha > 0.0f ? t->overlay_alpha : 0.4f;
    entity_overlay_has_rgb = t->overlay_has_rgb;
    for (int c = 0; c < 3; ++c) entity_overlay_rgb[c] = t->overlay_rgb[c];
    for (int c = 0; c < 3; ++c) entity_material[c] = t->material[c];
    entity_blend = t->blend;
    entity_unlit = t->unlit;
    entity_alpha = t->alpha;
    entity_clamp = t->clamp_texture;
    entity_linear_depth = t->linear_depth;
entity_cull_front = t->cull_front;
    entity_depth_equal = t->depth_equal;
    entity_alpha_ref = t->alpha_ref;
    entity_vertex_color = t->vertex_color;
    entity_two_sided = t->two_sided;
    entity_no_light = t->no_light;
    entity_no_alpha = t->no_alpha;
    entity_alpha_cut = t->alpha_cut;
    entity_vertex_alpha = t->vertex_alpha;
    entity_depth_lequal = t->depth_lequal;
    draw_quad(t->w, t->h, t->fog, t->fogs, t->foge, t->fogd, t->fogm,
              t->lm, texture, texture_w, texture_h, t->depth, t->rgb,
              t->triangles, t->samples, vertex);
    entity_overlay = 0;
    entity_blend = 0;
    entity_unlit = 0;
    entity_clamp = 0;
    entity_linear_depth = 0;
entity_cull_front = 0;
    entity_depth_equal = 0;
    entity_alpha_ref = 0.1f;
    entity_vertex_color = 0;
    entity_two_sided = 0;
    entity_no_light = 0;
    entity_no_alpha = 0;
    entity_alpha_cut = 0;
    entity_vertex_alpha = 0;
    entity_depth_lequal = 0;
    for (int c = 0; c < 3; ++c) entity_material[c] = 1.0f;
}

static unsigned char *load_rgba(const char *scene, const char *name, size_t expected_size)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", scene, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        snprintf(p, sizeof p, "out/assets/%s", name);
        f = fopen(p, "rb");
    }
    if (!f) return NULL;
    unsigned char *buf = malloc(expected_size);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, expected_size, f) != expected_size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

static void render_painting(const char *scene, const struct painting_art *art,
                            float yaw_deg, int dir, double pos_x, double pos_y, double pos_z,
                            int w, int h, const float proj[16], const float mv[16],
                            const double cam[3], const float fog[3],
                            float fogs, float foge, float fogd, int fogm,
                            const uint32_t lm[256], float *depth, unsigned char *rgb,
                            unsigned long *triangles, unsigned long *samples,
                            unsigned char **tex_cache,
                            const struct rb_world *world, int has_world)
{
    int tex_w = 256, tex_h = 256;
    if (!*tex_cache) {
        *tex_cache = load_rgba(scene, "painting.rgba", (size_t)tex_w * tex_h * 4);
        /* the recordings since EntityRenderProbe dump it with the state */
        if (!*tex_cache)
            *tex_cache = load_rgba(scene, "state/painting.rgba", (size_t)tex_w * tex_h * 4);
    }
    const unsigned char *tex = *tex_cache;
    if (!tex) return;

    float scale = 0.0625f;
    float sx = (float)art->size_x, sy = (float)art->size_y;
    float ox = (float)art->offset_x, oy = (float)art->offset_y;
    float var6 = -sx / 2.0f;
    float var7 = -sy / 2.0f;
    float var8 = 0.5f;
    float var9 = 0.75f, var10 = 0.8125f, var11 = 0.0f, var12 = 0.0625f;
    float var13 = 0.75f, var14 = 0.8125f, var15 = 0.001953125f, var16 = 0.001953125f;
    float var17 = 0.7519531f, var18 = 0.7519531f, var19 = 0.0f, var20 = 0.0625f;

    /* Compute face diffuses based on normal rotated by yaw_deg */
    float rad = yaw_deg * (float)(3.14159265358979323846 / 180.0);
    float cos_a = cosf(rad), sin_a = sinf(rad);

    /* Front face: (0, 0, -1) -> (-sin_a, 0, -cos_a) */
    float diff_front = compute_diffuse(-sin_a, 0.0f, -cos_a);
    /* Back face: (0, 0, 1) -> (sin_a, 0, cos_a) */
    float diff_back = compute_diffuse(sin_a, 0.0f, cos_a);
    /* Top face: (0, 1, 0) */
    float diff_top = compute_diffuse(0.0f, 1.0f, 0.0f);
    /* Bottom face: (0, -1, 0) */
    float diff_bottom = compute_diffuse(0.0f, -1.0f, 0.0f);
    /* Left face: (-1, 0, 0) -> (-cos_a, 0, sin_a) */
    float diff_left = compute_diffuse(-cos_a, 0.0f, sin_a);
    /* Right face: (1, 0, 0) -> (cos_a, 0, -sin_a) */
    float diff_right = compute_diffuse(cos_a, 0.0f, -sin_a);

    for (int var21 = 0; var21 < art->size_x / 16; ++var21) {
        for (int var22 = 0; var22 < art->size_y / 16; ++var22) {
            float var23 = var6 + (float)((var21 + 1) * 16);
            float var24 = var6 + (float)(var21 * 16);
            float var25 = var7 + (float)((var22 + 1) * 16);
            float var26 = var7 + (float)(var22 * 16);

            float cx = (var23 + var24) / 2.0f;
            float cy = (var25 + var26) / 2.0f;
            float light_block = 0.0f, light_sky = 240.0f;
            if (has_world) {
                int bx = (int)floor(pos_x);
                int by = (int)floor(pos_y + (double)(cy / 16.0f));
                int bz = (int)floor(pos_z);
                if (dir == 2) bx = (int)floor(pos_x + (double)(cx / 16.0f));
                else if (dir == 1) bz = (int)floor(pos_z - (double)(cx / 16.0f));
                else if (dir == 0) bx = (int)floor(pos_x - (double)(cx / 16.0f));
                else if (dir == 3) bz = (int)floor(pos_z + (double)(cx / 16.0f));
                int sky = rb_world_sky(world, bx, by, bz);
                int blk = rb_world_blocklight(world, bx, by, bz);
                light_block = (float)(blk << 4);
                light_sky = (float)(sky << 4);
            }

            float var27 = (ox + sx - (float)(var21 * 16)) / 256.0f;
            float var28 = (ox + sx - (float)((var21 + 1) * 16)) / 256.0f;
            float var29 = (oy + sy - (float)(var22 * 16)) / 256.0f;
            float var30 = (oy + sy - (float)((var22 + 1) * 16)) / 256.0f;

            /* Face 1: Front face (z = -var8) */
            struct entity_clip_vertex q_front[4] = {
                make_clip_vertex(var23 * scale, var26 * scale, -var8 * scale, var28, var29,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_front),
                make_clip_vertex(var24 * scale, var26 * scale, -var8 * scale, var27, var29,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_front),
                make_clip_vertex(var24 * scale, var25 * scale, -var8 * scale, var27, var30,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_front),
                make_clip_vertex(var23 * scale, var25 * scale, -var8 * scale, var28, var30,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_front)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_front);

            /* Face 2: Back face (z = var8) */
            struct entity_clip_vertex q_back[4] = {
                make_clip_vertex(var23 * scale, var25 * scale, var8 * scale, var9, var11,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_back),
                make_clip_vertex(var24 * scale, var25 * scale, var8 * scale, var10, var11,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_back),
                make_clip_vertex(var24 * scale, var26 * scale, var8 * scale, var10, var12,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_back),
                make_clip_vertex(var23 * scale, var26 * scale, var8 * scale, var9, var12,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_back)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_back);

            /* Face 3: Top face (y = var25) */
            struct entity_clip_vertex q_top[4] = {
                make_clip_vertex(var23 * scale, var25 * scale, -var8 * scale, var13, var15,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_top),
                make_clip_vertex(var24 * scale, var25 * scale, -var8 * scale, var14, var15,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_top),
                make_clip_vertex(var24 * scale, var25 * scale, var8 * scale, var14, var16,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_top),
                make_clip_vertex(var23 * scale, var25 * scale, var8 * scale, var13, var16,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_top)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_top);

            /* Face 4: Bottom face (y = var26) */
            struct entity_clip_vertex q_bottom[4] = {
                make_clip_vertex(var23 * scale, var26 * scale, var8 * scale, var13, var15,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_bottom),
                make_clip_vertex(var24 * scale, var26 * scale, var8 * scale, var14, var15,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_bottom),
                make_clip_vertex(var24 * scale, var26 * scale, -var8 * scale, var14, var16,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_bottom),
                make_clip_vertex(var23 * scale, var26 * scale, -var8 * scale, var13, var16,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_bottom)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_bottom);

            /* Face 5: Left face (x = var23) */
            struct entity_clip_vertex q_left[4] = {
                make_clip_vertex(var23 * scale, var25 * scale, var8 * scale, var18, var19,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_left),
                make_clip_vertex(var23 * scale, var26 * scale, var8 * scale, var18, var20,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_left),
                make_clip_vertex(var23 * scale, var26 * scale, -var8 * scale, var17, var20,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_left),
                make_clip_vertex(var23 * scale, var25 * scale, -var8 * scale, var17, var19,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_left)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_left);

            /* Face 6: Right face (x = var24) */
            struct entity_clip_vertex q_right[4] = {
                make_clip_vertex(var24 * scale, var25 * scale, -var8 * scale, var18, var19,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_right),
                make_clip_vertex(var24 * scale, var26 * scale, -var8 * scale, var18, var20,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_right),
                make_clip_vertex(var24 * scale, var26 * scale, var8 * scale, var17, var20,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_right),
                make_clip_vertex(var24 * scale, var25 * scale, var8 * scale, var17, var19,
                                 yaw_deg, pos_x, pos_y, pos_z, cam, mv, proj, light_block, light_sky, diff_right)
            };
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
                      depth, rgb, triangles, samples, q_right);
        }
    }
}

/* RenderPainting.doRender for the live client: one EntityPainting of art
 * ART (the EnumArt ordinal) facing DIR at (X, Y, Z), turned YAW, its
 * texture SCENE/painting.rgba, each tile lit from WORLD. */
void raster_painting_live(const char *scene, const struct entity_raster_target *t, int art, int dir, float yaw,
                          double x, double y, double z, const struct rb_world *world)
{
    static unsigned char *tex;
    for (int c = 0; c < 3; ++c) entity_material[c] = 1.0f;
    int n = 0;
    while (ARTS[n].title) ++n;
    if (art < 0 || art >= n) art = 0;
    render_painting(scene, &ARTS[art], yaw, dir, x, y, z, t->w, t->h, t->proj, t->mv, t->cam,
                    t->fog, t->fogs, t->foge, t->fogd, t->fogm, t->lm, t->depth, t->rgb, t->triangles,
                    t->samples, &tex, world, world != NULL);
}

static double item_number(const struct jval *v, const char *key)
{
    uint64_t bits = 0;
    json_double(json_get(v, key), &bits);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float item_float(const struct jval *v, const char *key)
{
    uint32_t bits = 0;
    json_float(json_get(v, key), &bits);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static float item_view_yaw(const char *scene, float pt)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct lines lines;
    lines_init(&lines);
    lines_file(&lines, f);
    char *last = NULL;
    for (const char *line = lines_next(&lines); line; line = lines_next(&lines)) {
        free(last);
        last = strdup(line);
    }
    lines_free(&lines);
    fclose(f);
    if (!last) return 0;
    struct jval *j = json_parse(last);
    const struct jval *pl = json_get(j, "pl");
    float yaw = item_float(pl, "pyaw") +
        (item_float(pl, "yaw") - item_float(pl, "pyaw")) * pt;
    json_free(j);
    return yaw;
}

/* RenderItem.renderDroppedItem's fast path for one sprite: the icon's uv range
 * and the stack tint come from the caller, so both the recorded rows (a JSON
 * icon entry) and the live client's item table draw through here. */
void raster_flat_item_quad(const struct entity_raster_target *t,
                           const unsigned char *tex, int tex_w, int tex_h,
                           const float uv4[4], int tint, float view_yaw,
                           float xoff, float yoff, float zoff,
                           double px, double py, double pz, int lb, int ls)
{
    const double *cam = t->cam;
    const float *mv = t->mv, *proj = t->proj;
    const int w = t->w, h = t->h;
    const float *fog = t->fog;
    float fogs = t->fogs, foge = t->foge, fogd = t->fogd;
    int fogm = t->fogm;
    const uint32_t *lm = t->lm;
    float *depth = t->depth;
    unsigned char *rgb = t->rgb;
    unsigned long *triangles = t->triangles, *samples = t->samples;
    float uv[4] = {uv4[0], uv4[1], uv4[2], uv4[3]};
    entity_material[0] = (float)((tint >> 16) & 255) / 255.0f;
    entity_material[1] = (float)((tint >> 8) & 255) / 255.0f;
    entity_material[2] = (float)(tint & 255) / 255.0f;
    float yaw = 180.0f - view_yaw;
    float xs[4] = {-0.5f, 0.5f, 0.5f, -0.5f};
    float ys[4] = {-0.25f, -0.25f, 0.75f, 0.75f};
    float us[4] = {uv[0], uv[1], uv[1], uv[0]};
    float vs[4] = {uv[3], uv[3], uv[2], uv[2]};
    /* doRender's chain is translate(pos) * scale(0.5) * translate(offset) *
     * rotate(180 - playerViewY): the per-copy offset is in world space, added
     * after the rotation, so it must not be rotated here. */
    float wox = 0.5f * xoff, woy = 0.5f * yoff, woz = 0.5f * zoff;
    /* setNormal(0, 1, 0) under the item lights: 0.4 + 0.6 * (L0.y + L1.y),
     * past 1. GL clamps the lit colour (the tint times this), not the light,
     * so a tint under white (a potion's or a firework star's second pass)
     * keeps the factor */
    const float up_y = 1.0f / 1.236931687687298f;
    const float up_diffuse = 0.4f + 0.6f * up_y + 0.6f * up_y;
    struct entity_clip_vertex q[4];
    for (int k = 0; k < 4; ++k)
        q[k] = make_clip_vertex_off(xs[k] * 0.5f, ys[k] * 0.5f, 0.0f, us[k], vs[k], yaw,
            px, py, pz, cam, mv, proj, (float)lb, (float)ls, up_diffuse, wox, woy, woz);
    draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, tex, tex_w, tex_h,
              depth, rgb, triangles, samples, q);
    entity_material[0] = entity_material[1] = entity_material[2] = 1.0f;
}

static void render_flat_item(const struct jval *item, const unsigned char *tex,
                             int tex_w, int tex_h, float view_yaw, float xoff,
                             float yoff, float zoff, double px, double py, double pz,
                             const double cam[3], const float mv[16], const float proj[16],
                             int lb, int ls, int w, int h, const float fog[3],
                             float fogs, float foge, float fogd, int fogm,
                             const uint32_t lm[256], float *depth, unsigned char *rgb,
                             unsigned long *triangles, unsigned long *samples)
{
    if (!item || !tex || !json_get(item, "minU")) return;
    struct entity_raster_target t = {0};
    float uv[4] = {item_float(item, "minU"), item_float(item, "maxU"),
                   item_float(item, "minV"), item_float(item, "maxV")};
    int64_t tint = 0xffffff;
    json_int(json_get(item, "tint"), &tint);
    t.w = w; t.h = h; t.proj = proj; t.mv = mv; t.cam = cam;
    t.fog = fog; t.fogs = fogs; t.foge = foge; t.fogd = fogd; t.fogm = fogm;
    t.lm = lm; t.depth = depth; t.rgb = rgb;
    t.triangles = triangles; t.samples = samples;
    t.material[0] = t.material[1] = t.material[2] = 1.0f;
    raster_flat_item_quad(&t, tex, tex_w, tex_h, uv, (int)tint, view_yaw,
                          xoff, yoff, zoff, px, py, pz, lb, ls);
}

static float item_partial_tick(const char *scene)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(path, "rb");
    if (!f) return 1.0f;
    struct lines lines;
    lines_init(&lines);
    lines_file(&lines, f);
    char *last = NULL;
    for (const char *line = lines_next(&lines); line; line = lines_next(&lines)) {
        free(last);
        last = strdup(line);
    }
    lines_free(&lines);
    fclose(f);
    if (!last) return 1.0f;
    struct jval *j = json_parse(last);
    uint32_t bits = 0x3f800000;
    if (j) json_float(json_get(j, "pt"), &bits);
    json_free(j);
    float pt;
    memcpy(&pt, &bits, 4);
    return pt;
}

/* The frame row's own entity list (EntityRenderProbe's "ents"), NULL for a
 * recording that predates it; the caller frees it with json_free. */
static struct jval *frame_ents_root(const char *scene)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct lines lines;
    lines_init(&lines);
    lines_file(&lines, f);
    char *last = NULL;
    for (const char *line = lines_next(&lines); line; line = lines_next(&lines)) {
        free(last);
        last = strdup(line);
    }
    lines_free(&lines);
    fclose(f);
    if (!last) return NULL;
    struct jval *j = json_parse(last);
    if (j && !json_get(j, "ents")) { json_free(j); return NULL; }
    return j;
}

/* The dropped items to draw: the frame row's "item" entries when the row has
 * the entity list (each frame its own), else MeshProbe's render_items.jsonl
 * written after the last frame. Each entry is a JSON text. */
static int item_entries(const char *scene, const char *kind, const char *file, char ***out)
{
    int n = 0, cap = 16;
    char **list = malloc((size_t)cap * sizeof *list);
    struct jval *root = frame_ents_root(scene);
    if (root) {
        const struct jval *ents = json_get(root, "ents");
        for (int i = 0; i < json_len(ents); ++i) {
            const struct jval *e = json_at(ents, i);
            const char *k = json_str(json_get(e, "k"));
            if (!k || strcmp(k, kind) || !json_get(e, "vis") || !((int)json_get(e, "vis")->dbl)) continue;
            if (n == cap) list = realloc(list, (size_t)(cap *= 2) * sizeof *list);
            list[n++] = json_raw(e);
        }
        json_free(root);
        *out = list;
        return n;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", scene, file);
    FILE *f = fopen(path, "rb");
    if (!f) { *out = list; return 0; }
    struct lines lines;
    lines_init(&lines);
    lines_file(&lines, f);
    for (const char *line = lines_next(&lines); line; line = lines_next(&lines)) {
        if (n == cap) list = realloc(list, (size_t)(cap *= 2) * sizeof *list);
        list[n++] = strdup(line);
    }
    lines_free(&lines);
    fclose(f);
    *out = list;
    return n;
}

struct world_items
{
    unsigned char *tex, *item_tex, *chest_tex;
    int64_t tw, th;
    float partial_tick, view_yaw;
    int unlit; /* GL_LIGHTING off: renderLitParticles draws with the item lighting disabled */
};

static int world_items_load(const char *scene, struct world_items *c)
{
    memset(c, 0, sizeof *c);
    char path[1024];
    snprintf(path, sizeof path, "%s/atlas.json", scene);
    FILE *af = fopen(path, "rb");
    if (!af) return -1;
    fseek(af, 0, SEEK_END);
    long alen = ftell(af);
    rewind(af);
    if (alen <= 0) { fclose(af); return -1; }
    char *ab = malloc((size_t)alen + 1);
    if (!ab) { fclose(af); return -1; }
    size_t an = fread(ab, 1, (size_t)alen, af);
    fclose(af);
    ab[an] = 0;
    struct jval *atlas = json_parse(ab);
    if (!atlas || !json_int(json_get(atlas, "atlas_width"), &c->tw) ||
        !json_int(json_get(atlas, "atlas_height"), &c->th)) {
        json_free(atlas);
        return -1;
    }
    json_free(atlas);
    c->tex = load_rgba(scene, "atlas.rgba", (size_t)c->tw * c->th * 4);
    if (!c->tex) return -1;
    c->item_tex = load_rgba(scene, "state/gui_items.rgba", 256u * 256u * 4u);
    texanim_patch_scene(scene, TEXANIM_BLOCKS, c->tex, (int)c->tw, (int)c->th);
    texanim_patch_scene(scene, TEXANIM_ITEMS, c->item_tex, 256, 256);
    c->chest_tex = load_rgba(scene, "state/gui_chest.rgba", 64u * 64u * 4u);
    c->partial_tick = item_partial_tick(scene);
    c->view_yaw = item_view_yaw(scene, c->partial_tick);
    return 0;
}

static void world_items_free(struct world_items *c)
{
    free(c->tex);
    free(c->item_tex);
    free(c->chest_tex);
}

/* RenderItem.doRender for one EntityItem entry at (px, py, pz) (the entry's
 * own position unless the pickup animation moves it). */
static void world_item_core(const char *scene, const struct world_items *c, int64_t id, int64_t meta,
                            int64_t count, int64_t age, float hover, int64_t packed,
                            const struct jval *sprite,
                            double px, double py, double pz, int w, int h,
                            const float proj[16], const float mv[16], const double cam[3],
                            const float fog[3], float fogs, float foge, float fogd, int fogm,
                            const uint32_t lm[256], float *depth, unsigned char *rgb,
                            unsigned long *triangles, unsigned long *samples)
{
    static struct block_item_model model;   /* 192 quads: off the stack */
    int block_model = !block_item_model_build(scene, (int)id, (int)meta, &model);
    if (!block_model && (!sprite || !c->item_tex)) return;
    float t = (float)age + c->partial_tick;
    float bob = mh_sin(t / 10.0f + hover) * .1f + .1f;
    float spin = (t / 20.0f + hover) * (180.0f / 3.14159265358979323846f);
    float scale = .25f;
    if (count > 40) count = 5;
    else if (count > 20) count = 4;
    else if (count > 5) count = 3;
    else if (count > 1) count = 2;
    else count = 1;
    if (model.inner_rotate_y90) spin += 90.0f;
    float sy = sinf(spin * (3.14159265358979323846f / 180.0f));
    float cy = cosf(spin * (3.14159265358979323846f / 180.0f));
    py += bob;
    int light_block = (int)packed & 65535;
    int light_sky = (int)((uint64_t)packed >> 16);
    jrand rand;
    jr_seed(&rand, 187);
    for (int copy = 0; copy < count; ++copy) {
        float ox = 0, oy = 0, oz = 0;
        if (copy) {
            if (!block_model) {
                ox = (jr_float(&rand) * 2 - 1) * .3f;
                oy = (jr_float(&rand) * 2 - 1) * .3f;
                oz = (jr_float(&rand) * 2 - 1) * .3f;
            } else {
                ox = (jr_float(&rand) * 2 - 1) * .2f / scale;
                oy = (jr_float(&rand) * 2 - 1) * .2f / scale;
                oz = (jr_float(&rand) * 2 - 1) * .2f / scale;
                /* the copy's glTranslatef comes before renderBlockAsItem's
                 * 90 degree turn, which the model's spin + 90 folds in: turn
                 * the offset back so only the faces take it */
                if (model.inner_rotate_y90) {
                    float t0 = ox;
                    ox = -oz;
                    oz = t0;
                }
            }
        }
        if (!block_model) {
            const unsigned char *surface = c->item_tex;
            int surface_w = 256, surface_h = 256;
            int64_t sprite_num = 1;
            json_int(json_get(sprite, "sprite"), &sprite_num);
            if (sprite_num == 0) { surface = c->tex; surface_w = (int)c->tw; surface_h = (int)c->th; }
            render_flat_item(sprite, surface, surface_w, surface_h,
                c->view_yaw, ox, oy, oz, px, py, pz, cam, mv, proj,
                light_block, light_sky, w, h, fog, fogs, foge, fogd, fogm,
                lm, depth, rgb, triangles, samples);
            const struct jval *pass1 = json_get(sprite, "pass1");
            if (pass1)
                render_flat_item(pass1, surface, surface_w, surface_h,
                    c->view_yaw, ox, oy, oz, px, py, pz, cam, mv, proj,
                    light_block, light_sky, w, h, fog, fogs, foge, fogd, fogm,
                    lm, depth, rgb, triangles, samples);
            continue;
        }
        for (int qi = 0; qi < model.n; ++qi) {
            const struct block_item_quad *q = &model.quad[qi];
            float nx = cy * q->normal[0] + sy * q->normal[2];
            float nz = -sy * q->normal[0] + cy * q->normal[2];
            float diffuse = c->unlit ? 1.0f : compute_diffuse(nx, q->normal[1], nz);
            struct entity_clip_vertex v[4];
            for (int k = 0; k < 4; ++k)
                v[k] = make_clip_vertex((q->p[k][0] + ox) * scale,
                    (q->p[k][1] + oy) * scale, (q->p[k][2] + oz) * scale,
                    q->uv[k][0], q->uv[k][1], spin, px, py, pz,
                    cam, mv, proj, (float)light_block, (float)light_sky, diffuse);
            const unsigned char *surface = model.chest_texture && c->chest_tex ? c->chest_tex : c->tex;
            int surface_w = model.chest_texture && c->chest_tex ? 64 : (int)c->tw;
            int surface_h = model.chest_texture && c->chest_tex ? 64 : (int)c->th;
            draw_quad(w, h, fog, fogs, foge, fogd, fogm, lm, surface, surface_w, surface_h,
                      depth, rgb, triangles, samples, v);
        }
    }
}

static void world_item_draw(const char *scene, const struct world_items *c, const struct jval *j,
                            double px, double py, double pz, int w, int h,
                            const float proj[16], const float mv[16], const double cam[3],
                            const float fog[3], float fogs, float foge, float fogd, int fogm,
                            const uint32_t lm[256], float *depth, unsigned char *rgb,
                            unsigned long *triangles, unsigned long *samples)
{
    int64_t id = 0, meta = 0, count = 1, age = 0, hover_bits = 0, packed = 0;
    json_int(json_get(j, "id"), &id);
    json_int(json_get(j, "meta"), &meta);
    json_int(json_get(j, "count"), &count);
    json_int(json_get(j, "age"), &age);
    json_int(json_get(j, "hover"), &hover_bits);
    json_int(json_get(j, "light"), &packed);
    float hover;
    uint32_t hb = (uint32_t)hover_bits;
    memcpy(&hover, &hb, 4);
    world_item_core(scene, c, id, meta, count, age, hover, packed, json_get(j, "sprite"), px, py, pz,
                    w, h, proj, mv, cam, fog, fogs, foge, fogd, fogm, lm, depth, rgb, triangles, samples);
}

/* The client items, with age and hoverStart, which EntityItem's NBT does not
 * save: the frame's own list, or MeshProbe's after the last frame. */
static void render_world_items(const char *scene, int w, int h,
                               const float proj[16], const float mv[16],
                               const double cam[3], const float fog[3],
                               float fogs, float foge, float fogd, int fogm,
                               const uint32_t lm[256], float *depth, unsigned char *rgb,
                               unsigned long *triangles, unsigned long *samples,
                               const struct rb_world *world, int has_world)
{
    char **list;
    int n = item_entries(scene, "item", "render_items.jsonl", &list);
    struct world_items c;
    if (n && !world_items_load(scene, &c)) {
        for (int i = 0; i < n; ++i) {
            struct jval *j = json_parse(strdup(list[i]));
            if (!j) continue;
            world_item_draw(scene, &c, j, item_number(j, "x"), item_number(j, "y"), item_number(j, "z"),
                            w, h, proj, mv, cam, fog, fogs, foge, fogd, fogm, lm, depth, rgb,
                            triangles, samples);
            json_free(j);
        }
        world_items_free(&c);
    }
    for (int i = 0; i < n; ++i) free(list[i]);
    free(list);
}

void raster_world_item_at(const char *scene, const struct entity_raster_target *t,
                          const struct jval *item, double x, double y, double z)
{
    struct world_items c;
    if (world_items_load(scene, &c)) return;
    c.unlit = 1;
    world_item_draw(scene, &c, item, x, y, z, t->w, t->h, t->proj, t->mv, t->cam, t->fog,
                    t->fogs, t->foge, t->fogd, t->fogm, t->lm, t->depth, t->rgb,
                    t->triangles, t->samples);
    world_items_free(&c);
}

/* RenderXPOrb.doRender's quad: the tile for the orb's XP value, the colour
 * pulse from xpColor and the partial tick, at 0.3 scale with the yaw and the
 * pitch rotation. The recorded rows and the live client both come through
 * here. RenderXPOrb sets the colour's alpha to 128, but the entity pass has
 * GL_BLEND disabled (the flat items, which match the goldens opaquely, prove
 * it), so the quad writes the tinted texel straight to the frame, depth and
 * all; the sprite's own alpha is cut by GL_ALPHA_TEST. */
void raster_orb_quad(const struct entity_raster_target *t,
                     const unsigned char *tex, int tex_w, int tex_h,
                     double x, double y, double z, int value, int color,
                     double pt, float pitch, float yaw, int light)
{
    const int xp_levels[] = {3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477};
    int tile = 0;
    for (int i = 0; i < 10; ++i) if (value >= xp_levels[i]) tile = i + 1;
    float pulse = ((float)color + (float)pt) / 2.0f;
    entity_material[0] = (int)((mh_sin(pulse) + 1.0f) * 0.5f * 255.0f) / 255.0f;
    entity_material[1] = 1.0f;
    entity_material[2] = (int)((mh_sin(pulse + 4.1887903f) + 1.0f) * 0.1f * 255.0f) / 255.0f;
    float u0 = (tile % 4) * .25f, u1 = u0 + .25f;
    float v0 = (tile / 4) * .25f, v1 = v0 + .25f;
    float cp = cosf(pitch * (float)(3.14159265358979323846 / 180.0));
    float sp = sinf(pitch * (float)(3.14159265358979323846 / 180.0));
    float xs[4] = {-0.5f, 0.5f, 0.5f, -0.5f};
    float ys[4] = {-0.25f, -0.25f, 0.75f, 0.75f};
    float us[4] = {u0, u1, u1, u0};
    float vs[4] = {v1, v1, v0, v0};
    struct entity_clip_vertex q[4];
    for (int k = 0; k < 4; ++k) {
        float lx = xs[k] * .3f, ly = ys[k] * .3f;
        q[k] = make_clip_vertex(lx, ly * cp, -ly * sp, us[k], vs[k], 180.0f - yaw,
            x, y, z, t->cam, t->mv, t->proj,
            (float)(light & 65535), (float)((uint64_t)light >> 16), 1.0f);
    }
    draw_quad(t->w, t->h, t->fog, t->fogs, t->foge, t->fogd, t->fogm,
              t->lm, tex, tex_w, tex_h, t->depth, t->rgb, t->triangles, t->samples, q);
    entity_material[0] = entity_material[1] = entity_material[2] = 1.0f;
}

static float frame_pitch(const char *scene, float pt)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    FILE *sf = fopen(path, "rb");
    float pitch = 0;
    if (sf) {
        struct lines sl;
        lines_init(&sl); lines_file(&sl, sf);
        char *last = NULL;
        for (const char *line = lines_next(&sl); line; line = lines_next(&sl)) {
            free(last); last = strdup(line);
        }
        lines_free(&sl); fclose(sf);
        if (last) {
            struct jval *s = json_parse(last);
            const struct jval *pl = json_get(s, "pl");
            pitch = item_float(pl, "ppit") +
                (item_float(pl, "pit") - item_float(pl, "ppit")) * pt;
            json_free(s);
        }
    }
    return pitch;
}

static void render_world_orbs(const char *scene, const struct entity_raster_target *t)
{
    char **list;
    int n = item_entries(scene, "orb", "render_orbs.jsonl", &list);
    unsigned char *tex = n ? load_rgba(scene, "state/gui_orb.rgba", 64u * 64u * 4u) : NULL;
    if (tex) {
        float pt = item_partial_tick(scene);
        float yaw = item_view_yaw(scene, pt);
        float pitch = frame_pitch(scene, pt);
        for (int i = 0; i < n; ++i) {
            struct jval *j = json_parse(strdup(list[i]));
            if (!j) continue;
            int64_t value = 0, color = 0, light = 0;
            json_int(json_get(j, "value"), &value);
            json_int(json_get(j, "color"), &color);
            json_int(json_get(j, "light"), &light);
            raster_orb_quad(t, tex, 64, 64, item_number(j, "x"), item_number(j, "y"),
                            item_number(j, "z"), (int)value, (int)color, pt, pitch, yaw,
                            (int)light);
            json_free(j);
        }
        free(tex);
    }
    for (int i = 0; i < n; ++i) free(list[i]);
    free(list);
}

/* ------------------------------------------------------------------- live */

/* The live item and orb textures, loaded once: the asset scene's own dumps when
 * it has them, else the copies under out/assets (the same bytes in every
 * recording). */
static unsigned char *live_entity_tex(const char *assets, const char *name,
                                      const char *file, size_t n)
{
    static char cached[2][1024];
    static unsigned char *px[2];
    char p[1024];
    snprintf(p, sizeof p, "%s/state/%s", assets, name);
    for (int i = 0; i < 2; ++i)
        if (px[i] && !strcmp(cached[i], p)) return px[i];
    unsigned char *b = load_rgba(assets, name, n);
    if (!b) {
        char q[1024];
        snprintf(q, sizeof q, "out/assets/%s", file);
        b = load_rgba("out/assets", file, n);
    }
    if (!b) return NULL;
    int slot = px[0] ? 1 : 0;
    free(px[slot]);
    px[slot] = b;
    snprintf(cached[slot], sizeof cached[slot], "%s", p);
    return b;
}

int raster_entities_live(const char *assets, const char *icon_scene,
                         const struct entity_raster_target *t,
                         const struct worldfx_entity *e, int n,
                         float pt, float view_yaw, float view_pitch,
                         const unsigned char *atlas, int atlas_w, int atlas_h)
{
    unsigned char *items = live_entity_tex(assets, "gui_items.rgba", "items.rgba", 256u * 256u * 4u);
    unsigned char *orb = live_entity_tex(assets, "gui_orb.rgba", "orb.rgba", 64u * 64u * 4u);
    /* the chest item's own 64x64 texture, from the scene or the icon scene */
    static unsigned char *chest;
    static int chest_tried;
    if (!chest_tried) {
        chest_tried = 1;
        chest = load_rgba(assets, "state/gui_chest.rgba", 64u * 64u * 4u);
        if (!chest) chest = load_rgba(icon_scene, "state/gui_chest.rgba", 64u * 64u * 4u);
    }
    int drawn = 0;

    for (int i = 0; i < n; ++i) {
        const struct worldfx_entity *en = &e[i];
        if (en->orb) {
            if (!orb) continue;
            raster_orb_quad(t, orb, 64, 64, en->x, en->y, en->z, en->count, en->color,
                            pt, view_pitch, view_yaw, en->light);
            ++drawn;
            continue;
        }
        const struct jval *icon = raster_hud_item_entry(icon_scene, en->id, en->meta);
        if (!icon || json_get(icon, "mode") == NULL) continue;
        int64_t mode = 1;
        json_int(json_get(icon, "mode"), &mode);
        if (mode == 1) {
            /* a block item: RenderItem.doRender's renderBlockAsItem path,
             * the recorded rows' own arithmetic over the live atlas */
            if (!atlas || ((en->id == 54 || en->id == 130 || en->id == 146) && !chest)) continue;
            struct world_items c = {0};
            c.tex = (unsigned char *)atlas;
            c.tw = atlas_w;
            c.th = atlas_h;
            c.item_tex = items;
            c.chest_tex = chest;
            c.partial_tick = pt;
            c.view_yaw = view_yaw;
            c.unlit = en->unlit;
            world_item_core(assets, &c, en->id, en->meta, en->count, en->age, en->hover,
                            (int64_t)en->light, NULL, en->x, en->y, en->z, t->w, t->h, t->proj, t->mv,
                            t->cam, t->fog, t->fogs, t->foge, t->fogd, t->fogm, t->lm, t->depth,
                            t->rgb, t->triangles, t->samples);
            ++drawn;
            continue;
        }
        int64_t sprite = 1, tint64 = 0xffffff;
        json_int(json_get(icon, "sprite"), &sprite);
        json_int(json_get(icon, "tint"), &tint64);
        /* sprite 0: an item drawn from the block atlas (a sapling, a flower) */
        const unsigned char *tex = sprite == 1 ? items : atlas;
        int tex_w = sprite == 1 ? 256 : atlas_w, tex_h = sprite == 1 ? 256 : atlas_h;
        if (!tex) continue;

        float count = en->count > 40 ? 5 : en->count > 20 ? 4 : en->count > 5 ? 3
                    : en->count > 1 ? 2 : 1;
        float tt = (float)en->age + pt;
        float bob = mh_sin(tt / 10.0f + en->hover) * .1f + .1f;
        jrand rand;
        jr_seed(&rand, 187);
        for (int copy = 0; copy < (int)count; ++copy) {
            float ox = 0, oy = 0, oz = 0;
            if (copy) {
                ox = (jr_float(&rand) * 2 - 1) * .3f;
                oy = (jr_float(&rand) * 2 - 1) * .3f;
                oz = (jr_float(&rand) * 2 - 1) * .3f;
            }
            float uv[4];
            uv[0] = item_float(icon, "minU"); uv[1] = item_float(icon, "maxU");
            uv[2] = item_float(icon, "minV"); uv[3] = item_float(icon, "maxV");
            raster_flat_item_quad(t, tex, tex_w, tex_h, uv, (int)tint64, view_yaw,
                                  ox, oy, oz, en->x, en->y + bob, en->z,
                                  en->light & 65535, en->light >> 16);
            const struct jval *p1 = json_get(icon, "pass1");
            if (p1) {
                float uv1[4];
                int64_t tint1 = 0xffffff;
                uv1[0] = item_float(p1, "minU"); uv1[1] = item_float(p1, "maxU");
                uv1[2] = item_float(p1, "minV"); uv1[3] = item_float(p1, "maxV");
                json_int(json_get(p1, "tint"), &tint1);
                /* ItemFireworkCharge.getColorFromItemStack(stack, 1) */
                if (en->id == 402 && en->tag) tint1 = firework_charge_color(en->tag);
                /* the second pass lies on the first: RenderItem draws it
                 * under the game's GL_LEQUAL */
                entity_depth_lequal = 1;
                raster_flat_item_quad(t, tex, tex_w, tex_h, uv1, (int)tint1, view_yaw,
                                      ox, oy, oz, en->x, en->y + bob, en->z,
                                      en->light & 65535, en->light >> 16);
                entity_depth_lequal = 0;
            }
        }
        ++drawn;
    }
    return drawn;
}

void raster_entities_draw(const char *scene, int w, int h,
                          const float proj[16], const float mv[16],
                          const double cam[3], const float fog[3],
                          float fogs, float foge, float fogd, int fogm,
                          const uint32_t lm[256], float *depth, unsigned char *rgb,
                          unsigned long *triangles, unsigned long *samples)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/entities.jsonl", scene);
    FILE *f = fopen(p, "rb");
    if (!f) {
        snprintf(p, sizeof p, "%s/state/entities.jsonl", scene);
        f = fopen(p, "rb");
    }
    if (!f) return;

    /* Load world lighting if chunks.bin is present */
    struct rb_world world = {0};
    int has_world = 0;
    char cp[1024];
    snprintf(cp, sizeof cp, "%s/chunks.bin", scene);
    if (access(cp, R_OK) == 0 && rb_world_load(&world, scene) == 0) {
        int radius, pcx, pcz, px, py, pz;
        if (rb_manifest_load(scene, &radius, &pcx, &pcz, &px, &py, &pz) == 0) {
            world.origin_cx = pcx;
            world.origin_cz = pcz;
            has_world = 1;
        } else {
            rb_world_free(&world);
        }
    }

    struct lines in;
    lines_init(&in);
    lines_file(&in, f);

    unsigned char *painting_tex = NULL;

    for (const char *line = lines_next(&in); line; line = lines_next(&in)) {
        if (!line[0]) continue;
        struct jval *v = json_parse(strdup(line));
        if (!v || v->kind != J_OBJ) {
            json_free(v);
            continue;
        }
        int player = 0;
        int64_t iv = 0;
        if (json_int(json_get(v, "player"), &iv)) player = (int)iv;
        if (player) {
            json_free(v);
            continue;
        }

        const char *cls = json_str(json_get(v, "class"));
        const struct jval *nbt_val = json_get(v, "nbt");
        char *raw_nbt = json_raw(nbt_val);
        nbt *t = raw_nbt ? nbt_parse(raw_nbt) : NULL;
        free(raw_nbt);

        if (!cls || !t) {
            nbt_free(t);
            json_free(v);
            continue;
        }

        double pos[3] = {0};
        const nbt *pos_node = nbt_get(t, "Pos");
        if (pos_node && nbt_list_size(pos_node) >= 3) {
            for (int i = 0; i < 3; ++i) {
                uint64_t bits = nbt_double_bits(nbt_list_get(pos_node, i));
                memcpy(&pos[i], &bits, sizeof(double));
            }
        }

        float rot[2] = {0};
        const nbt *rot_node = nbt_get(t, "Rotation");
        if (rot_node && nbt_list_size(rot_node) >= 2) {
            for (int i = 0; i < 2; ++i) {
                uint32_t bits = nbt_float_bits(nbt_list_get(rot_node, i));
                memcpy(&rot[i], &bits, sizeof(float));
            }
        }

        if (!strcmp(cls, "EntityPainting")) {
            const nbt *mot_node = nbt_get(t, "Motive");
            const char *motive = mot_node ? nbt_string_value(mot_node) : "Kebab";
            int dir = 0;
            const nbt *dir_node = nbt_get(t, "Direction");
            if (dir_node) dir = (int)nbt_int_value(dir_node);
            const struct painting_art *art = find_art(motive);
            render_painting(scene, art, rot[0], dir, pos[0], pos[1], pos[2],
                            w, h, proj, mv, cam, fog, fogs, foge, fogd, fogm,
                            lm, depth, rgb, triangles, samples, &painting_tex,
                            &world, has_world);

        } else if (!strcmp(cls, "EntityItemFrame")) {
            struct raster_itemframe fr;
            raster_itemframe_parse(t, &fr);
            struct entity_raster_target ft = {.w = w, .h = h, .proj = proj, .mv = mv, .cam = cam,
                .fog = fog, .fogs = fogs, .foge = foge, .fogd = fogd, .fogm = fogm, .lm = lm,
                .depth = depth, .rgb = rgb, .triangles = triangles, .samples = samples,
                .material = {1, 1, 1}};
            raster_itemframe_draw(scene, &ft, &fr, &world, has_world);
        }

        nbt_free(t);
        json_free(v);
    }

    lines_free(&in);
    fclose(f);
    if (has_world) rb_world_free(&world);
    free(painting_tex);
    render_world_items(scene, w, h, proj, mv, cam, fog, fogs, foge, fogd, fogm,
                       lm, depth, rgb, triangles, samples, &world, has_world);
    struct entity_raster_target orbs = {.w = w, .h = h, .proj = proj, .mv = mv, .cam = cam,
        .fog = fog, .fogs = fogs, .foge = foge, .fogd = fogd, .fogm = fogm, .lm = lm,
        .depth = depth, .rgb = rgb, .triangles = triangles, .samples = samples,
        .material = {1, 1, 1}};
    render_world_orbs(scene, &orbs);
}
