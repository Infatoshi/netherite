#include "lang.h"
/* The 2D overlay pass, drawn over the finished world frame: GuiIngame's HUD
 * (the crosshair, the hotbar, the health/hunger/experience bars, the
 * held-item tooltip, the boss bar) and the achievement toast.
 *
 * Inputs are the render-state dump's "hud" object ($scene/state/frames.jsonl,
 * recorded by RenderStateProbe at frame time) and the GUI textures beside it
 * ($scene/state/gui.json and gui_*.rgba, read back from GL by the recorder, so
 * the renderer samples exactly what the client sampled). A scene recorded
 * before the HUD state existed has no "hud" member and draws nothing, which
 * keeps its frame byte-identical.
 *
 * Everything is in the overlay's own coordinates: setupOverlayRendering's
 * glOrtho over the scaled resolution, modelview translated to z = -2000, depth
 * buffer cleared first, so the pass is a painter's algorithm over the RGB frame
 * in vanilla's draw order. gui.json's per-texture GL filters decide the
 * sampling, as they do in GL.
 *
 * Not drawn, and why: the chat, player list and scoreboard screens, the debug
 * screen, the first-person hand, and the item paths that need the item model
 * (isometric block items, layered icons, the enchant glint, the durability
 * bar's HSB colour). No recorded frame has any of them yet.
 */
#define _POSIX_C_SOURCE 200809L
#include "raster.h"
#include "env.h"
#include "texanim.h"
#include "gui_screens.h"
#include "jrand.h"
#include "tape.h"
#include "block_item_model.h"
#include "font.h"
#include "items.h"
#include "chat.h"
#include "item_color.h"
#include "firework.h"
#include "raster_mobs.h"
#include "itemtag.h"
#include "potion.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601

/* The GUI sheets are all 256x256 and drawTexturedModalRect scales its texel
 * arguments by 1/256 whatever the sheet really is. */
#define MODAL_UV 0.00390625f

/* ------------------------------------------------------------------ inputs */

static void *alloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "raster_hud: allocation failed\n"); exit(1); }
    return p;
}

/* Whole file, NUL terminated, or NULL when it is not there. */
static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    rewind(f);
    char *b = alloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[n] = 0;
    if (size) *size = (size_t)n;
    return b;
}

/* The state file's last line: the frame this scene renders. */
static struct jval *read_state(const char *scene)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) { free(last); last = strdup(line); }
    free(line);
    fclose(f);
    if (!last) return NULL;
    struct jval *j = json_parse(last);
    if (!j) { fprintf(stderr, "raster_hud: invalid JSON in %s\n", p); exit(1); }
    return j;
}

static void die(const char *what, const struct jval *v)
{
    (void)v;
    fprintf(stderr, "raster_hud: %s\n", what);
    exit(1);
}

static float jf(const struct jval *v)
{
    uint32_t n = 0;
    if (!json_float(v, &n)) die("expected a float", v);
    float f;
    memcpy(&f, &n, sizeof f);
    return f;
}

static int ji(const struct jval *v)
{
    int64_t n = 0;
    if (!json_int(v, &n)) die("expected an integer", v);
    return (int)n;
}

/* A JSON null is not an absent member: it is an item slot with nothing in it. */
static int present(const struct jval *v)
{
    return v && v->kind != J_NULL;
}

static int opt_int(const struct jval *o, const char *k, int dflt)
{
    const struct jval *v = json_get(o, k);
    int64_t n = 0;
    if (!v || !json_int(v, &n)) return dflt;
    return (int)n;
}

static float opt_float(const struct jval *o, const char *k, float dflt)
{
    const struct jval *v = json_get(o, k);
    uint32_t b = 0;
    if (!v || !json_float(v, &b)) return dflt;
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static double opt_double(const struct jval *o, const char *k, double dflt)
{
    const struct jval *v = json_get(o, k);
    uint64_t b = 0;
    if (!v || !json_double(v, &b)) return dflt;
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

/* ---------------------------------------------------------------- textures */

struct tex {
    unsigned char *px;
    int w, h;
    int min, mag;
    int repeat;                /* GL_REPEAT wrap (the default ones clamp to the edge) */
};

/* an icon's uv range in the block atlas */
struct hud_uv { float min_u, max_u, min_v, max_v; };

static float uv_bits(const struct jval *v)
{
    int64_t n = 0;
    uint32_t b;
    float f;
    if (!json_int(v, &n)) return 0.0f;
    b = (uint32_t)n;
    memcpy(&f, &b, 4);
    return f;
}

static void tex_load(struct tex *t, const struct jval *meta, const char *path)
{
    t->w = ji(json_get(meta, "w"));
    t->h = ji(json_get(meta, "h"));
    t->min = ji(json_get(meta, "min"));
    t->mag = ji(json_get(meta, "mag"));
    size_t n = (size_t)t->w * t->h * 4, got = 0;
    char *b = read_file(path, &got);
    if (!b || got != n)
    {
        fprintf(stderr, "raster_hud: cannot read %s\n", path);
        exit(1);
    }
    t->px = (unsigned char *)b;
}

static unsigned char quant(float x)
{
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    /* the driver's software rasterizer converts to eight bits with a half-up
     * round, not the round-to-even lrintf would give */
    return (unsigned char)floorf(x * 255.0f + 0.5f);
}

static const unsigned char *texel(const struct tex *t, int x, int y)
{
    if (t->repeat)
    {
        x = ((x % t->w) + t->w) % t->w;
        y = ((y % t->h) + t->h) % t->h;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= t->w) x = t->w - 1;
    if (y >= t->h) y = t->h - 1;
    return t->px + ((size_t)y * t->w + x) * 4;
}

/* One GL texture sample at texel coordinates (u,v), with the filter the
 * recorder read back: NEAREST picks the texel the coordinate falls in, LINEAR
 * blends the four around it (all of these textures clamp to the edge). */
static void tex_sample(const struct tex *t, float u, float v, float out[4])
{
    float tu = u * (float)t->w, tv = v * (float)t->h;
    if (t->mag == GL_LINEAR || t->min == GL_LINEAR)
    {
        float x = tu - 0.5f, y = tv - 0.5f;
        int x0 = (int)floorf(x), y0 = (int)floorf(y);
        float fx = x - x0, fy = y - y0;
        const unsigned char *c[4] = {texel(t, x0, y0), texel(t, x0 + 1, y0),
                                     texel(t, x0, y0 + 1), texel(t, x0 + 1, y0 + 1)};
        for (int i = 0; i < 4; ++i)
        {
            float a = c[0][i] * (1 - fx) + c[1][i] * fx;
            float b = c[2][i] * (1 - fx) + c[3][i] * fx;
            /* the filtered texel leaves the fixed-function texture stage as
             * eight bits, as it does in the driver's software rasterizer */
            out[i] = quant((a * (1 - fy) + b * fy) / 255.0f) / 255.0f;
        }
        return;
    }
    const unsigned char *c = texel(t, (int)floorf(tu), (int)floorf(tv));
    for (int i = 0; i < 4; ++i) out[i] = c[i] / 255.0f;
}

/* One of the recorder's GUI textures by its gui.json key, from SCENE's state;
 * 0 (and t zeroed) when the scene has none. */
static int chest_tex_fallback(const char *key, struct tex *t);

static int scene_tex(const char *scene, const char *key, struct tex *t)
{
    memset(t, 0, sizeof *t);
    char p[1024];
    snprintf(p, sizeof p, "%s/state/gui.json", scene);
    char *text = read_file(p, NULL);
    if (!text) return 0;
    struct jval *meta = json_parse(text);
    if (!meta) { fprintf(stderr, "raster_hud: invalid JSON in %s\n", p); exit(1); }
    const struct jval *m = json_get(meta, key);
    if (m)
    {
        snprintf(p, sizeof p, "%s/state/gui_%s.rgba", scene, key);
        tex_load(t, m, p);
    }
    json_free(meta);
    if (!t->px) chest_tex_fallback(key, t);
    return t->px != NULL;
}

/* TileEntityRendererChestHelper's sheets for a scene that recorded none:
 * the jar's entity/chest textures (out/assets/te). */
static int chest_tex_fallback(const char *key, struct tex *t)
{
    const char *file = !strcmp(key, "chest") ? "out/assets/te/chest.rgba" :
                       !strcmp(key, "chest_ender") ? "out/assets/te/ender.rgba" :
                       !strcmp(key, "chest_trapped") ? "out/assets/te/trapped.rgba" : NULL;
    if (!file) return 0;
    size_t sz = 0;
    t->px = (unsigned char *)read_file(file, &sz);
    if (t->px && sz == 64U * 64U * 4U) { t->w = t->h = 64; t->min = t->mag = GL_NEAREST; return 1; }
    free(t->px);
    t->px = NULL;
    return 0;
}

/* An item atlas sprite's uv range by icon name, from the recorder's item
 * atlas dump (gui.json "items".sprites); 0 when there is none. */
/* item_sprite over a parsed gui.json */
static int item_sprite_in(const struct jval *meta, const char *name, struct hud_uv *uv)
{
    if (!meta) return 0;
    const struct jval *sprites = json_get(json_get(meta, "items"), "sprites");
    for (int i = 0; i < json_len(sprites); ++i)
    {
        const struct jval *sp = json_at(sprites, i);
        const char *n = json_str(json_get(sp, "name"));
        if (!n || strcmp(n, name)) continue;
        uv->min_u = opt_float(sp, "minU", 0); uv->max_u = opt_float(sp, "maxU", 0);
        uv->min_v = opt_float(sp, "minV", 0); uv->max_v = opt_float(sp, "maxV", 0);
        return 1;
    }
    return 0;
}

static int item_sprite(const char *scene, const char *name, struct hud_uv *uv)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/state/gui.json", scene);
    char *text = read_file(p, NULL);
    if (!text) return 0;
    struct jval *meta = json_parse(text);
    if (!meta) return 0;
    const struct jval *sprites = json_get(json_get(meta, "items"), "sprites");
    int found = 0;
    for (int i = 0; i < json_len(sprites) && !found; ++i)
    {
        const struct jval *sp = json_at(sprites, i);
        const char *n = json_str(json_get(sp, "name"));
        if (!n || strcmp(n, name)) continue;
        uv->min_u = opt_float(sp, "minU", 0); uv->max_u = opt_float(sp, "maxU", 0);
        uv->min_v = opt_float(sp, "minV", 0); uv->max_v = opt_float(sp, "maxV", 0);
        found = 1;
    }
    json_free(meta);
    return found;
}

int raster_hud_item_sprite(const char *scene, const char *name, float uv[4])
{
    struct hud_uv u;
    if (!item_sprite(scene, name, &u)) return 0;
    uv[0] = u.min_u; uv[1] = u.max_u; uv[2] = u.min_v; uv[3] = u.max_v;
    return 1;
}

/* ----------------------------------------------------------------- drawing */

enum {
    BLEND_ALPHA,      /* GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA */
    BLEND_INVERT,     /* GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR: the crosshair */
    BLEND_MULTIPLY,   /* GL_ZERO, GL_ONE_MINUS_SRC_COLOR: the vignette */
    BLEND_NONE,       /* blending off: the source replaces the destination */
    BLEND_GLINT       /* GL_DST_ALPHA, GL_ONE over the item's depth mask */
};

struct hud {
    unsigned char *rgb;
    int w, h;
    double sx, sy;             /* overlay coordinates to device pixels */
    const struct tex *tex;
    float color[4];
    int text_blend;            /* the blend the font draws with */
    unsigned char *item_mask;
    int mask_x, mask_y, mask_w, mask_h, mask_record;
    int no_alpha_test;         /* GL_ALPHA_TEST off (it is on unless a draw turns it off) */
    float gx, gy, gs;          /* a glTranslatef(gx, gy) then glScalef(gs) under the
                                * overlay's own matrix; gs 0 is none (the chat's) */
    float gsy;                 /* the y scale when it differs from gs (0: gs) */
};

/* The overlay matrix: an overlay coordinate through the pushed transform. */
static float ov_x(const struct hud *h, float x) { return h->gs != 0 ? h->gx + h->gs * x : x; }
static float ov_y(const struct hud *h, float y) { return h->gs != 0 ? h->gy + (h->gsy != 0 ? h->gsy : h->gs) * y : y; }

/* GL_ALPHA_TEST: the overlay leaves it on with the 0.1 reference the world
 * render set, and the font, the item icons and the widget sheets are all alpha
 * 0 or 255, so a threshold anywhere inside is the same test. */
#define ALPHA_REF 0.1f

/* One fragment: the texture sample modulated by the current colour, alpha
 * tested, then blended. The sample and its modulation go to eight bits, the
 * blend itself stays in float and only its result is rounded, which is the
 * model that matches the goldens' software rasterizer best. */
static void shade(struct hud *h, int x, int y, float u, float v, int blend)
{
    int mi = (y - h->mask_y) * h->mask_w + (x - h->mask_x);
    if (blend == BLEND_GLINT && (!h->item_mask || x < h->mask_x || y < h->mask_y ||
        x >= h->mask_x + h->mask_w || y >= h->mask_y + h->mask_h || !h->item_mask[mi])) return;
    if (blend == BLEND_GLINT) { u -= floorf(u); v -= floorf(v); }
    float s[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    /* no texture: GL_TEXTURE_2D off, the fragment is the colour alone */
    if (h->tex) tex_sample(h->tex, u, v, s);
    for (int c = 0; c < 4; ++c) s[c] = quant(s[c] * h->color[c]) / 255.0f;
    if (!h->no_alpha_test && s[3] <= ALPHA_REF) return;
    if (h->mask_record && h->item_mask && x >= h->mask_x && y >= h->mask_y &&
        x < h->mask_x + h->mask_w && y < h->mask_y + h->mask_h) h->item_mask[mi] = 1;
    unsigned char *dst = h->rgb + ((size_t)y * h->w + x) * 3;
    for (int c = 0; c < 3; ++c)
    {
        float d = dst[c] / 255.0f, r;
        switch (blend)
        {
        case BLEND_NONE: r = s[c]; break;
        case BLEND_INVERT: r = s[c] * (1.0f - d) + d * (1.0f - s[c]); break;
        case BLEND_MULTIPLY: r = d * (1.0f - s[c]); break;
        case BLEND_GLINT: r = d + s[c]; break;
        default: r = s[c] * s[3] + d * (1.0f - s[3]); break;
        }
        dst[c] = quant(r);
    }
}

struct hvert { float x, y, u, v; };

static float hedge(const struct hvert *a, const struct hvert *b, float x, float y)
{
    return (x - a->x) * (b->y - a->y) - (y - a->y) * (b->x - a->x);
}

static int htop_left(const struct hvert *a, const struct hvert *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    return dy < 0 || (dy == 0 && dx > 0);
}

/* Screen y grows downward and the overlay is drawn with face culling off, so a
 * triangle's winding is fixed up rather than dropped. */
static void htriangle(struct hud *h, const struct hvert in[3], int blend)
{
    struct hvert v[3] = {in[0], in[1], in[2]};
    float area = hedge(&v[0], &v[1], v[2].x, v[2].y);
    if (area == 0) return;
    if (area < 0)
    {
        struct hvert t = v[1];
        v[1] = v[2];
        v[2] = t;
        area = -area;
    }
    float minx = fminf(v[0].x, fminf(v[1].x, v[2].x));
    float maxx = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    float miny = fminf(v[0].y, fminf(v[1].y, v[2].y));
    float maxy = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(h->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(h->h - 1, ceilf(maxy));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
    {
        float px = x + 0.5f, py = y + 0.5f;
        float e0 = hedge(&v[1], &v[2], px, py);
        float e1 = hedge(&v[2], &v[0], px, py);
        float e2 = hedge(&v[0], &v[1], px, py);
        if (e0 < 0 || e1 < 0 || e2 < 0) continue;
        if ((e0 == 0 && !htop_left(&v[1], &v[2])) ||
            (e1 == 0 && !htop_left(&v[2], &v[0])) ||
            (e2 == 0 && !htop_left(&v[0], &v[1]))) continue;
        float b0 = e0 / area, b1 = e1 / area, b2 = e2 / area;
        float u = b0 * v[0].u + b1 * v[1].u + b2 * v[2].u;
        float t = b0 * v[0].v + b1 * v[1].v + b2 * v[2].v;
        shade(h, x, y, u, t, blend);
    }
}

static void hquad(struct hud *h, const struct hvert q[4], int blend)
{
    struct hvert a[3] = {q[0], q[1], q[3]};
    struct hvert b[3] = {q[1], q[2], q[3]};
    htriangle(h, a, blend);
    htriangle(h, b, blend);
}

/* An axis-aligned rectangle in overlay coordinates, textured from (u0,v0) to
 * (u1,v1) in texel coordinates. */
static void rect_uv(struct hud *h, float x, float y, float w, float hh,
                    float u0, float v0, float u1, float v1, int blend)
{
    struct hvert q[4];
    float xs[4] = {x, x + w, x + w, x};
    float ys[4] = {y, y, y + hh, y + hh};
    float us[4] = {u0, u1, u1, u0};
    float vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; ++i)
    {
        q[i].x = (float)(ov_x(h, xs[i]) * h->sx);
        q[i].y = (float)(ov_y(h, ys[i]) * h->sy);
        q[i].u = us[i];
        q[i].v = vs[i];
    }
    hquad(h, q, blend);
}

static void tex_rect(struct hud *h, const struct tex *t, float x, float y,
                     float w, float hh, float u0, float v0, float u1, float v1, int blend)
{
    h->tex = t;
    rect_uv(h, x, y, w, hh, u0, v0, u1, v1, blend);
}

/* Gui.drawTexturedModalRect: x,y,w,h in overlay pixels, u,v in sheet texels. */
static void modal(struct hud *h, const struct tex *t, int x, int y, int u, int v, int w, int hh)
{
    tex_rect(h, t, (float)x, (float)y, (float)w, (float)hh, u * MODAL_UV, v * MODAL_UV,
             (u + w) * MODAL_UV, (v + hh) * MODAL_UV, BLEND_ALPHA);
}

/* --------------------------------------------------------------------- font */

struct font {
    const struct tex *tex;
    struct font_metrics m;
};

static int font_width(const struct font *f, const char *s)
{
    return font_string_width(&f->m, s);
}

/* FontRenderer.renderDefaultChar: the glyph's quad, sheared one pixel each
 * way when italic. */
static void font_glyph(struct hud *h, const struct font *f, int c, float x, float y, int italic)
{
    float u = (float)(c % 16 * 8), v = (float)(c / 16 * 8);
    float w = (float)f->m.cw[c] - 0.01f - 1.0f;
    float sk = italic ? 1.0f : 0.0f;
    struct hvert q[4];
    float xs[4] = {x + sk, x - sk, x + w - sk, x + w + sk};
    float ys[4] = {y, y + 7.99f, y + 7.99f, y};
    float us[4] = {u, u, u + w, u + w};
    float vs[4] = {v, v + 7.99f, v + 7.99f, v};
    for (int i = 0; i < 4; ++i)
    {
        q[i].x = (float)(ov_x(h, xs[i]) * h->sx);
        q[i].y = (float)(ov_y(h, ys[i]) * h->sy);
        q[i].u = us[i] / 128.0f;
        q[i].v = vs[i] / 128.0f;
    }
    h->tex = f->tex;
    hquad(h, q, h->text_blend);
}

/* A quad in the current colour with texturing off, as Tessellator draws the
 * underline and strikethrough (x0,y0)-(x1,y1) in overlay units. */
static void solid_quad(struct hud *h, float x0, float y0, float x1, float y1, int blend)
{
    const struct tex *keep = h->tex;
    h->tex = NULL;
    rect_uv(h, x0, y0, x1 - x0, y1 - y0, 0, 0, 0, 0, blend);
    h->tex = keep;
}

/* FontRenderer.colorCode: the 16 colours, then their shadows (a quarter, and
 * gold's extra red only on the bright one). */
static int font_code_color(int idx)
{
    int adjust = (idx >> 3 & 1) * 85;
    int r = (idx >> 2 & 1) * 170 + adjust;
    int g = (idx >> 1 & 1) * 170 + adjust;
    int b = (idx & 1) * 170 + adjust;
    if (idx == 6) r += 85;
    if (idx >= 16) { r /= 4; g /= 4; b /= 4; }
    return ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
}

/* FontRenderer.renderString and renderStringAtPos: the colour (dimmed for the
 * shadow pass), the formatting codes (a colour resets the styles; r returns
 * to the string's own colour), bold's second glyph one pixel over, italic's
 * shear, the strikethrough and underline quads, and the obfuscated style's
 * fontRandom draws (rnd; NULL leaves the glyphs as they are, for text that
 * never carries the code). Returns the pen's x. */
static float font_render(struct hud *h, const struct font *f, const char *s, float x, float y,
                         int color, int shadow, jrand *rnd)
{
    if (!s) return x;
    if ((color & 0xfc000000) == 0) color |= 0xff000000;
    if (shadow) color = ((color & 16579836) >> 2) | (color & (int)0xff000000);
    float base[4] = {(float)(color >> 16 & 255) / 255.0f, (float)(color >> 8 & 255) / 255.0f,
                     (float)(color & 255) / 255.0f, (float)(color >> 24 & 255) / 255.0f};
    memcpy(h->color, base, sizeof base);
    size_t n;
    uint32_t *cp = font_utf8_decode(s, &n);
    int random = 0, bold = 0, strike = 0, under = 0, italic = 0;
    float px = x, py = y;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t ch = cp[i];
        if (ch == FONT_SECTION && i + 1 < n)
        {
            uint32_t d = cp[i + 1];
            if (d >= 'A' && d <= 'Z') d += 32;
            const char *codes = "0123456789abcdefklmnor";
            const char *at = d < 128 && d ? strchr(codes, (int)d) : NULL;
            int idx = at ? (int)(at - codes) : -1;
            if (idx < 16)
            {
                random = bold = strike = under = italic = 0;
                if (idx < 0 || idx > 15) idx = 15;
                if (shadow) idx += 16;
                int c = font_code_color(idx);
                h->color[0] = (float)(c >> 16) / 255.0f;
                h->color[1] = (float)(c >> 8 & 255) / 255.0f;
                h->color[2] = (float)(c & 255) / 255.0f;
                h->color[3] = base[3];
            }
            else if (idx == 16) random = 1;
            else if (idx == 17) bold = 1;
            else if (idx == 18) strike = 1;
            else if (idx == 19) under = 1;
            else if (idx == 20) italic = 1;
            else
            {
                random = bold = strike = under = italic = 0;
                memcpy(h->color, base, sizeof base);
            }
            ++i;
            continue;
        }
        int gi = font_char_index(ch);
        if (random && gi != -1 && rnd)
        {
            int k;
            do k = jr_int_n(rnd, 256); while (f->m.cw[gi] != f->m.cw[k]);
            gi = k;
        }
        /* renderCharAtPos: a space advances four pixels undrawn, a character
         * in the table draws glyph gi, anything else is a unicode page
         * glyph, which no text drawn here has */
        float adv = 0.0f;
        int drawn = 0;
        if (ch == 32) adv = 4.0f;
        else if (font_char_index(ch) != -1) { font_glyph(h, f, gi, px, py, italic); adv = (float)f->m.cw[gi]; drawn = 1; }
        if (bold)
        {
            if (drawn) font_glyph(h, f, gi, px + 1.0f, py, italic);
            adv += 1.0f;
        }
        if (strike) solid_quad(h, px, py + 4.0f - 1.0f, px + adv, py + 4.0f, h->text_blend);
        if (under) solid_quad(h, px - 1.0f, py + 9.0f - 1.0f, px + adv, py + 9.0f, h->text_blend);
        px += (float)(int)adv;
    }
    free(cp);
    return px;
}

static void font_string(struct hud *h, const struct font *f, const char *s, int x, int y, int color)
{
    font_render(h, f, s, (float)x, (float)y, color, 0, NULL);
}

/* FontRenderer.drawStringWithShadow: the shadow one pixel down and right, then
 * the string; an obfuscated string draws both from the one fontRandom. */
static void font_shadow_rnd(struct hud *h, const struct font *f, const char *s, int x, int y,
                            int color, jrand *rnd)
{
    font_render(h, f, s, (float)(x + 1), (float)(y + 1), color, 1, rnd);
    font_render(h, f, s, (float)x, (float)y, color, 0, rnd);
}

static void font_shadow(struct hud *h, const struct font *f, const char *s, int x, int y, int color)
{
    font_shadow_rnd(h, f, s, x, y, color, NULL);
}

/* FontRenderer.drawSplitString. */
static void font_split(struct hud *h, const struct font *f, const char *s, int x, int y, int wrap)
{
    if (!s) return;
    int n = 0;
    char **lines = font_list_to_width(&f->m, s, wrap, &n);
    for (int i = 0; i < n; ++i, y += 9) font_string(h, f, lines[i], x, y, -1);
    font_list_free(lines, n);
}

/* ---------------------------------------------------------------- the item */

/* The icon's getColorFromItemStack: computed for the block-coloured items
 * (item_color.c), the recorded colour for the rest. */
static int item_tint(const char *scene, const struct jval *item)
{
    int c = item_block_color(opt_int(item, "id", 0), opt_int(item, "dmg", 0),
                             scene ? block_item_model_grass_color(scene) : -1);
    if (c >= 0) return c;
    return opt_int(item, "tint", 16777215);
}

/* RenderItem.renderItemIntoGUI's flat path: the sprite's own pixels from its
 * atlas, tinted, alpha blended, with lighting off (the flat branch disables
 * it), at the 16x16 the overlay asks for. */
static void item_icon_tint(struct hud *h, const struct tex *atlas, const struct jval *item, int x, int y,
                           int64_t tint)
{
    h->color[0] = (float)(tint >> 16 & 255) / 255.0f;
    h->color[1] = (float)(tint >> 8 & 255) / 255.0f;
    h->color[2] = (float)(tint & 255) / 255.0f;
    h->color[3] = 1.0f;
    tex_rect(h, atlas, (float)x, (float)y, 16, 16,
             jf(json_get(item, "minU")), jf(json_get(item, "minV")),
             jf(json_get(item, "maxU")), jf(json_get(item, "maxV")), BLEND_ALPHA);
}

static void item_icon(struct hud *h, const struct tex *atlas, const struct jval *item, int x, int y,
                      const char *scene)
{
    item_icon_tint(h, atlas, item, x, y, item_tint(scene, item));
}

struct gui {
    struct hud *h;
    const char *scene;
    struct font font;
    const struct tex *icons;
    const struct tex *widgets;
    const struct tex *achievement;
    const struct tex *vignette;
    const struct tex *glint;
    const struct tex *blocks;
    const struct tex *pumpkin;
    struct hud_uv portal_uv;
    int portal_ok;
    const struct tex *chest, *chest_ender, *chest_trapped;
    const struct tex *items;
    const struct jval *hud;
    const struct jval *bar;
    const struct jval *toast;
    int sw, sh;
    int64_t now;               /* Minecraft.getSystemTime, the glint's clock */
    /* renderItemOverlayIntoGUI's string: a yellow count (a drag's share at
     * its cap, the cursor's "0"); draw_stack draws the count yellow and
     * even at 1 or 0 while it is set */
    int count_yellow;
};
static void white(struct hud *h);

/* RenderItem's block branch. The model is shared with the world item path;
 * these transforms are the GUI's translate(1,.5,1), scale(10), rotations
 * 210 X and 45 Y, and RenderBlocks' own 90 Y for ordinary blocks. */
struct block_vertex { float x, y, z, u, v; };

static float block_edge(const struct block_vertex *a, const struct block_vertex *b,
                        float x, float y)
{
    return (x - a->x) * (b->y - a->y) - (y - a->y) * (b->x - a->x);
}

static void block_triangle(struct hud *h, const struct block_vertex in[3], float *depth,
                           int blend)
{
    struct block_vertex v[3] = {in[0], in[1], in[2]};
    float area = block_edge(&v[0], &v[1], v[2].x, v[2].y);
    if (area == 0) return;
    if (area < 0) { struct block_vertex t = v[1]; v[1] = v[2]; v[2] = t; area = -area; }
    int x0 = (int)fmaxf(0, floorf(fminf(v[0].x, fminf(v[1].x, v[2].x))));
    int x1 = (int)fminf(h->w - 1, ceilf(fmaxf(v[0].x, fmaxf(v[1].x, v[2].x))));
    int y0 = (int)fmaxf(0, floorf(fminf(v[0].y, fminf(v[1].y, v[2].y))));
    int y1 = (int)fminf(h->h - 1, ceilf(fmaxf(v[0].y, fmaxf(v[1].y, v[2].y))));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float sx = x + 0.5f, sy = y + 0.5f;
        float a = block_edge(&v[1], &v[2], sx, sy);
        float b = block_edge(&v[2], &v[0], sx, sy);
        float c = block_edge(&v[0], &v[1], sx, sy);
        if (a < 0 || b < 0 || c < 0) continue;
        a /= area; b /= area; c /= area;
        float z = a * v[0].z + b * v[1].z + c * v[2].z;
        int k = y * h->w + x;
        if (z > depth[k]) continue;
        float u = a * v[0].u + b * v[1].u + c * v[2].u;
        float tv = a * v[0].v + b * v[1].v + c * v[2].v;
        int tx = (int)floorf(u * h->tex->w);
        int ty = (int)floorf(tv * h->tex->h);
        if (texel(h->tex, tx, ty)[3] <= (blend == BLEND_NONE ? 127 : 25)) continue;
        shade(h, x, y, u, tv, blend);
        depth[k] = z;
    }
}

static void block_item_icon(struct gui *g, const char *scene, const struct jval *item,
                            int x, int y)
{
    struct block_item_model model;
    int id = opt_int(item, "id", 0), meta = opt_int(item, "dmg", 0);
    if (block_item_model_build(scene, id, meta, &model)) return;
    struct hud *h = g->h;
    /* TileEntityRendererChestHelper: the ender and trapped chests' own textures */
    const struct tex *chest = model.chest_texture == 2 ? g->chest_ender :
                              model.chest_texture == 3 ? g->chest_trapped : g->chest;
    h->tex = model.chest_texture && chest && chest->px ? chest : g->blocks;
    float *depth = malloc((size_t)h->w * h->h * sizeof *depth);
    if (!depth) { fprintf(stderr, "raster_hud: depth allocation failed\n"); exit(1); }
    const float sn = -0.5f, cs = -0.8660254037844386f; /* sin/cos 210 deg */
    float ys = model.inner_rotate_y90 ? 0.7071067811865476f : -0.7071067811865476f;
    float yc = 0.7071067811865476f;
    /* the depth test reads only the pixels of the item's triangles' boxes:
     * cleared there alone (lane/guiact: the whole window's buffer was
     * cleared for every block item) */
    {
        float lx = INFINITY, hx = -INFINITY, ly = INFINITY, hy = -INFINITY;
        for (int qi = 0; qi < model.n; ++qi)
            for (int i = 0; i < 4; ++i) {
                const struct block_item_quad *q = &model.quad[qi];
                float xx = q->p[i][0], yy = q->p[i][1], zz = q->p[i][2];
                float rx = yc * xx + ys * zz;
                float rz = -ys * xx + yc * zz;
                float ry = cs * yy - sn * rz;
                float vx = (float)(ov_x(h, x - 2 + 10 * (1 + rx)) * h->sx);
                float vy = (float)(ov_y(h, y + 3 + 10 * (.5f + ry)) * h->sy);
                lx = fminf(lx, vx); hx = fmaxf(hx, vx);
                ly = fminf(ly, vy); hy = fmaxf(hy, vy);
            }
        if (model.n > 0) {
            int x0 = (int)fmaxf(0, floorf(lx)), x1 = (int)fminf(h->w - 1, ceilf(hx));
            int y0 = (int)fmaxf(0, floorf(ly)), y1 = (int)fminf(h->h - 1, ceilf(hy));
            for (int yy = y0; yy <= y1; ++yy)
                for (int xx = x0; xx <= x1; ++xx) depth[yy * h->w + xx] = INFINITY;
        }
    }
    /* RenderHelper.enableGUIStandardItemLighting sets these positions after
     * -30 Y and 165 X in eye space. */
    const float light[2][3] = {{-0.23791005f,-0.63443479f,0.73545313f},
                                {0.028667255f,-0.92737470f,-0.37303398f}};
    int tint = item_tint(scene, item);
    /* RenderBlocks.renderBlockAsItem: the grass item's top alone takes its
     * render colour */
    int grass = block_item_model_grass_color(scene);
    for (int qi = 0; qi < model.n; ++qi) {
        const struct block_item_quad *q = &model.quad[qi];
        float nx = yc * q->normal[0] + ys * q->normal[2];
        float nz = -ys * q->normal[0] + yc * q->normal[2];
        float ny = cs * q->normal[1] - sn * nz;
        float ne_z = -(sn * q->normal[1] + cs * nz);
        float d = 0.4f;
        for (int i = 0; i < 2; ++i)
            d += 0.6f * fmaxf(0, nx * light[i][0] + ny * light[i][1] + ne_z * light[i][2]);
        /* GL lights the tint (the colour material) and clamps the lit colour,
         * not the light factor: a tint under one keeps the light over one */
        int qt = q->tint_top && grass >= 0 ? grass : tint;
        for (int c = 0; c < 3; ++c)
            h->color[c] = fminf(1.0f, d * ((qt >> (16 - 8 * c)) & 255) / 255.0f);
        h->color[3] = 1;
        struct block_vertex v[4];
        for (int i = 0; i < 4; ++i) {
            float xx = q->p[i][0], yy = q->p[i][1], zz = q->p[i][2];
            float rx = yc * xx + ys * zz;
            float rz = -ys * xx + yc * zz;
            float ry = cs * yy - sn * rz;
            /* glScalef(1,1,-1) turns the rotated z toward the viewer, so
             * the depth test keeps the smaller rotated z */
            float rz2 = sn * yy + cs * rz;
            v[i] = (struct block_vertex){
                (float)(ov_x(h, x - 2 + 10 * (1 + rx)) * h->sx),
                (float)(ov_y(h, y + 3 + 10 * (.5f + ry)) * h->sy),
                1 + rz2, q->uv[i][0], q->uv[i][1]
            };
        }
        const struct block_vertex a[3] = {v[0], v[1], v[3]};
        const struct block_vertex b[3] = {v[1], v[2], v[3]};
        block_triangle(h, a, depth, model.render_pass ? BLEND_ALPHA : BLEND_NONE);
        block_triangle(h, b, depth, model.render_pass ? BLEND_ALPHA : BLEND_NONE);
    }
    free(depth);
    white(h);
}

/* ----------------------------------------------------------------- the gui */

/* MathHelper.ceiling_float_int / ceiling_double_int. */
static int ceil_float_int(float f)
{
    int i = (int)f;
    return f > (float)i ? i + 1 : i;
}

static int ceil_double_int(double d)
{
    int i = (int)d;
    return d > (double)i ? i + 1 : i;
}

static void white(struct hud *h)
{
    h->color[0] = h->color[1] = h->color[2] = h->color[3] = 1.0f;
}

static const struct tex *item_atlas(const struct gui *g, const struct jval *item)
{
    return opt_int(item, "sprite", 1) == 1 ? g->items : g->blocks;
}

/* GuiIngame.renderVignette. In agent mode the oracle holds
 * prevVignetteBrightness still, so the recorded value is the colour the frame
 * draws with. */
static void draw_vignette(struct gui *g)
{
    struct hud *h = g->h;
    float v = opt_float(g->hud, "vign", 1.0f);
    h->color[0] = h->color[1] = h->color[2] = v;
    h->color[3] = 1.0f;
    tex_rect(h, g->vignette, 0, 0, (float)g->sw, (float)g->sh,
             0.0f, 0.0f, 1.0f, 1.0f, BLEND_MULTIPLY);
    white(h);
}

/* BossStatus's statics: set by the boss's renderer as it draws, counted down
 * by the HUD. A recorded frame carries them as the probe read them before the
 * frame drew; the frame's own world pass then sets them again when it draws
 * the dragon (drawn_this_frame), as RenderDragon.doRender does before
 * GuiIngame runs. */
static struct {
    float health_scale;
    int status_bar_time, drawn_this_frame;
    char name[96];
} boss_status;

void raster_hud_boss_set(float health_scale, const char *name)
{
    boss_status.health_scale = health_scale;
    boss_status.status_bar_time = 100;
    snprintf(boss_status.name, sizeof boss_status.name, "%s", name ? name : "");
    boss_status.drawn_this_frame = 1;
}

/* GuiIngame.renderBossHealth: the bar's frame and fill, then the name. */
static void draw_boss(struct gui *g)
{
    struct hud *h = g->h;
    if (json_get(g->hud, "bosst") && !boss_status.drawn_this_frame)
    {
        const char *name = json_str(json_get(g->hud, "boss"));
        boss_status.status_bar_time = opt_int(g->hud, "bosst", 0);
        boss_status.health_scale = opt_float(g->hud, "bossh", 0.0f);
        snprintf(boss_status.name, sizeof boss_status.name, "%s", name ? name : "");
        if (!name) boss_status.status_bar_time = 0;
    }
    boss_status.drawn_this_frame = 0;
    if (!boss_status.name[0] || boss_status.status_bar_time <= 0) return;
    --boss_status.status_bar_time;
    int sw = g->sw;
    int w = sw / 2 - 91;
    int filled = (int)(boss_status.health_scale * 183.0f);
    white(h);
    /* the frame is drawn twice in vanilla, to the same pixels */
    modal(h, g->icons, w, 12, 0, 74, 182, 5);
    modal(h, g->icons, w, 12, 0, 74, 182, 5);
    if (filled > 0) modal(h, g->icons, w, 12, 0, 79, filled, 5);
    const char *name = boss_status.name;
    font_shadow(h, &g->font, name, sw / 2 - font_width(&g->font, name) / 2, 12 - 10, 16777215);
}

/* GuiIngame.func_110327_a: armour, health, food, a mount's health and air. */
static void draw_bars(struct gui *g)
{
    struct hud *h = g->h;
    const struct jval *b = g->bar;
    int sw = g->sw, sh = g->sh;
    /* the health blinks while the player is hurt */
    int hurt = opt_int(b, "hurtres", 0);
    int blink = (hurt / 3 % 2 == 1) && hurt >= 10;
    int hp = ceil_float_int(opt_float(b, "hp", 0));
    int prevhp = ceil_float_int(opt_float(b, "prevhp", 0));
    jrand rand;
    jr_seed(&rand, (int64_t)opt_int(g->hud, "uc", 0) * 312871);
    int food = opt_int(b, "food", 0);
    float sat = opt_float(b, "sat", 0);
    int uc = opt_int(g->hud, "uc", 0);
    float maxhp = opt_float(b, "mhp", 20);
    float absorb = opt_float(b, "abs", 0);
    int armor = opt_int(b, "armorv", 0);
    int left = sw / 2 - 91, right = sw / 2 + 91;
    int y = sh - 39;
    int rows = ceil_float_int((maxhp + absorb) / 2.0f / 10.0f);
    int step = 10 - (rows - 2) > 3 ? 10 - (rows - 2) : 3;
    int abs_y = y - (rows - 1) * step - 10;
    float absleft = absorb;
    int regen = opt_int(b, "regen", 0) ? uc % ceil_float_int(maxhp + 5.0f) : -1;
    int hardcore = opt_int(b, "hardcore", 0) ? 5 : 0;

    white(h);
    /* armour */
    for (int i = 0; i < 10; ++i)
    {
        if (armor <= 0) break;
        int x = left + i * 8;
        if (i * 2 + 1 < armor) modal(h, g->icons, x, abs_y, 34, 9, 9, 9);
        if (i * 2 + 1 == armor) modal(h, g->icons, x, abs_y, 25, 9, 9, 9);
        if (i * 2 + 1 > armor) modal(h, g->icons, x, abs_y, 16, 9, 9, 9);
    }

    /* health */
    for (int i = ceil_float_int((maxhp + absorb) / 2.0f) - 1; i >= 0; --i)
    {
        int v = 16;
        if (opt_int(b, "poison", 0)) v += 36;
        else if (opt_int(b, "wither", 0)) v += 72;
        int fill = blink ? 1 : 0;
        int row = ceil_float_int((float)(i + 1) / 10.0f) - 1;
        int x = left + i % 10 * 8;
        int yy = y - row * step;
        if (hp <= 4) yy += jr_int_n(&rand, 2);
        if (i == regen) yy -= 2;
        modal(h, g->icons, x, yy, 16 + fill * 9, 9 * hardcore, 9, 9);
        if (blink)
        {
            if (i * 2 + 1 < prevhp) modal(h, g->icons, x, yy, v + 54, 9 * hardcore, 9, 9);
            if (i * 2 + 1 == prevhp) modal(h, g->icons, x, yy, v + 63, 9 * hardcore, 9, 9);
        }
        if (absleft > 0.0f)
        {
            if (absleft == absorb && absorb != 0.0f && (float)((int)absorb % 2) == 1.0f)
                modal(h, g->icons, x, yy, v + 153, 9 * hardcore, 9, 9);
            else
                modal(h, g->icons, x, yy, v + 144, 9 * hardcore, 9, 9);
            absleft -= 2.0f;
        }
        else
        {
            if (i * 2 + 1 < hp) modal(h, g->icons, x, yy, v + 36, 9 * hardcore, 9, 9);
            if (i * 2 + 1 == hp) modal(h, g->icons, x, yy, v + 45, 9 * hardcore, 9, 9);
        }
    }

    if (!opt_int(b, "mount", 0))
    {
        /* food */
        for (int i = 0; i < 10; ++i)
        {
            int yy = y;
            int v = 16, shake = 0;
            if (opt_int(b, "hunger", 0)) { v += 36; shake = 13; }
            if (sat <= 0.0f && food > 0 && uc % (food * 3 + 1) == 0)
                yy += jr_int_n(&rand, 3) - 1;
            int x = right - i * 8 - 9;
            modal(h, g->icons, x, yy, 16 + shake * 9, 27, 9, 9);
            if (i * 2 + 1 < food) modal(h, g->icons, x, yy, v + 36, 27, 9, 9);
            if (i * 2 + 1 == food) modal(h, g->icons, x, yy, v + 45, 27, 9, 9);
        }
    }
    else
    {
        /* a mount's health */
        int mount = ceil_float_int(opt_float(b, "mhp2", 0));
        int total = (int)(opt_float(b, "mmax", 0) + 0.5f) / 2;
        if (total > 30) total = 30;
        int yy = y;
        for (int row = 0; total > 0; row += 20)
        {
            int n = total < 10 ? total : 10;
            total -= n;
            for (int i = 0; i < n; ++i)
            {
                int x = right - i * 8 - 9;
                modal(h, g->icons, x, yy, 52 + blink * 9, 9, 9, 9);
                if (i * 2 + 1 + row < mount) modal(h, g->icons, x, yy, 88, 9, 9, 9);
                if (i * 2 + 1 + row == mount) modal(h, g->icons, x, yy, 97, 9, 9, 9);
            }
            yy -= 10;
        }
    }

    /* air */
    if (opt_int(b, "inwater", 0))
    {
        int air = opt_int(b, "air", 300);
        int full = ceil_double_int((double)(air - 2) * 10.0 / 300.0);
        int extra = ceil_double_int((double)air * 10.0 / 300.0) - full;
        for (int i = 0; i < full + extra; ++i)
            modal(h, g->icons, right - i * 8 - 9, abs_y, i < full ? 16 : 25, 18, 9, 9);
    }
}

/* RenderItem.renderGlint's two sheared, repeating texture passes. The equal
 * depth test keeps them on pixels the item's icon wrote. */
static void draw_glint(struct gui *g, int x, int y)
{
    struct hud *h = g->h;
    int64_t now = g->now;
    h->tex = g->glint;
    h->color[0] = 0.5f; h->color[1] = 0.25f; h->color[2] = 0.8f; h->color[3] = 1.0f;
    int x0 = (int)floor((x - 2) * h->sx), x1 = (int)ceil((x + 18) * h->sx);
    int y0 = (int)floor((y - 2) * h->sy), y1 = (int)ceil((y + 18) * h->sy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > h->w) x1 = h->w;
    if (y1 > h->h) y1 = h->h;
    /* glBlendFuncSeparate(772,1,0,0) zeros destination alpha in the first
     * pass, so the second pass contributes no RGB through GL_DST_ALPHA. */
    for (int pass = 0; pass < 1; ++pass)
    {
        float period = (float)(3000 + pass * 1873);
        float phase = (float)(now % (3000 + pass * 1873)) / period * 256.0f;
        float shear = pass == 0 ? 4.0f : -1.0f;
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px)
            {
                float lx = (float)((px + 0.5) / h->sx - (x - 2));
                float ly = (float)((py + 0.5) / h->sy - (y - 2));
                if (lx < 0 || lx >= 20 || ly < 0 || ly >= 20) continue;
                shade(h, px, py, (phase + lx + ly * shear) * MODAL_UV,
                      ly * MODAL_UV, BLEND_GLINT);
            }
    }
    white(h);
}

/* RenderItem.renderItemOverlayIntoGUI's durability bar: three untextured,
 * unblended quads, black under a dark track under the remaining length, in
 * the colour the damage fraction picks. */
static void draw_durability(struct hud *h, int id, int damage, int x, int y)
{
    int max = id > 0 && id < 4096 ? ITEMS[id].max_damage : 0;
    if (max <= 0 || damage <= 0) return; /* ItemStack.isItemDamaged */
    int len = (int)floor(13.0 - (double)damage * 13.0 / (double)max + 0.5);
    int green = (int)floor(255.0 - (double)damage * 255.0 / (double)max + 0.5);
    int fill = (255 - green) << 16 | green << 8;
    int track = (255 - green) / 4 << 16 | 16128;
    const struct { int w, hh, c; } q[3] = {{13, 2, 0}, {12, 1, track}, {len, 1, fill}};
    int keep = h->no_alpha_test;
    h->no_alpha_test = 1;
    for (int i = 0; i < 3; ++i)
    {
        h->color[0] = (float)(q[i].c >> 16 & 255) / 255.0f;
        h->color[1] = (float)(q[i].c >> 8 & 255) / 255.0f;
        h->color[2] = (float)(q[i].c & 255) / 255.0f;
        h->color[3] = 1.0f;
        solid_quad(h, (float)(x + 2), (float)(y + 13), (float)(x + 2 + q[i].w), (float)(y + 13 + q[i].hh), BLEND_NONE);
    }
    h->no_alpha_test = keep;
    white(h);
}

/* One stack as RenderItem.renderItemAndEffectIntoGUI then
 * renderItemOverlayIntoGUI draw it: the icon by its path (the item's icon, the
 * block model, the two-pass icon), the enchant glint over the icon's own
 * pixels, then the count (blending off) and the durability bar. ENTRY is the
 * item's icon record (a HUD slot's own, or the item table's for the stack's
 * id and damage). */
/* ItemStack.hasEffect's enchantment half: a stack whose tag lists one */
static int stack_enchanted(int tag)
{
    const struct itag *t = tag ? itag_get(tag) : NULL;
    return t != NULL && t->nench > 0;
}

static void draw_stack(struct gui *g, const struct jval *entry, int id, int damage, int count,
                       int tag, int x, int y)
{
    if (!present(entry)) return;
    struct hud *h = g->h;
    int mx = (int)floor((x - 2) * h->sx), my = (int)floor((y - 2) * h->sy);
    int mw = (int)ceil((x + 18) * h->sx) - mx;
    int mh = (int)ceil((y + 18) * h->sy) - my;
    h->item_mask = calloc((size_t)mw * mh, 1);
    if (!h->item_mask) { fprintf(stderr, "raster_hud: allocation failed\n"); exit(1); }
    h->mask_x = mx; h->mask_y = my; h->mask_w = mw; h->mask_h = mh;
    h->mask_record = 1;
    /* GuiIngame.renderInventorySlot's pickup pop: the icon (not the count)
     * under translate(x + 8, y + 12), scale(1 / f, (f + 1) / 2) with
     * f = 1 + (animationsToGo - partialTicks) / 5 */
    float pop = opt_float(entry, "pop", 0.0f);
    float keep_gx = h->gx, keep_gy = h->gy, keep_gs = h->gs, keep_gsy = h->gsy;
    if (pop > 0.0f && h->gs == 0)
    {
        float f = 1.0F + pop / 5.0F;
        h->gs = 1.0F / f;
        h->gsy = (f + 1.0F) / 2.0F;
        h->gx = (float)(x + 8) * (1.0F - h->gs);
        h->gy = (float)(y + 12) * (1.0F - h->gsy);
    }
    int mode = opt_int(entry, "mode", 0);
    if (mode == 0 || mode == 2)
        item_icon(g->h, item_atlas(g, entry), entry, x, y, g->scene);
    if (mode == 1) block_item_icon(g, g->scene, entry, x, y);
    if (mode == 2)
    {
        const struct jval *pass1 = json_get(entry, "pass1");
        /* ItemFireworkCharge.getColorFromItemStack(stack, 1) reads the
         * stack's own Explosion colours */
        if (present(pass1) && id == 402 && tag)
            item_icon_tint(g->h, item_atlas(g, entry), pass1, x, y, firework_charge_color(tag));
        else if (present(pass1)) item_icon(g->h, item_atlas(g, entry), pass1, x, y, NULL);
    }
    h->mask_record = 0;
    /* hasEffect: the recorded entry's, else the stack's own enchantments */
    if (opt_int(entry, "effect", 0) || stack_enchanted(tag)) draw_glint(g, x, y);
    h->gx = keep_gx; h->gy = keep_gy; h->gs = keep_gs; h->gsy = keep_gsy;
    free(h->item_mask);
    h->item_mask = NULL;
    white(h);
    if (count > 1 || g->count_yellow)
    {
        char text[16];
        snprintf(text, sizeof text, "%d", count);
        int tx = x + 19 - 2 - font_width(&g->font, text);
        h->text_blend = BLEND_NONE;
        font_shadow(h, &g->font, text, tx, y + 6 + 3, g->count_yellow ? 16777045 : 16777215);
        h->text_blend = BLEND_ALPHA;
    }
    draw_durability(h, id, damage, x, y);
}

/* GuiIngame.renderInventorySlot: a recorded HUD slot. */
static void draw_slot(struct gui *g, const struct jval *item, int x, int y)
{
    if (!present(item)) return;
    draw_stack(g, item, opt_int(item, "id", 0), opt_int(item, "dmg", 0), opt_int(item, "n", 1),
               opt_int(item, "tag", 0), x, y);
}

/* GuiIngame.renderPumpkinBlur: the pumpkin sheet over the whole screen, alpha
 * blended with GL_ALPHA_TEST off (a texel of alpha 0 leaves the pixel alone, so
 * the sheet's own alpha test is the same test). */
static void draw_pumpkin(struct gui *g)
{
    struct hud *h = g->h;
    if (!opt_int(g->hud, "pumpkin", 0) || !g->pumpkin->px) return;
    white(h);
    tex_rect(h, g->pumpkin, 0.0f, 0.0f, (float)g->sw, (float)g->sh,
             0.0f, 0.0f, 1.0f, 1.0f, BLEND_ALPHA);
}

/* GuiIngame.func_130015_b: the portal swirl, the block atlas' portal sprite
 * tinted white with the timer's alpha. */
static void draw_portal(struct gui *g)
{
    struct hud *h = g->h;
    if (opt_int(g->hud, "tportal", 0)) return;
    float a = opt_float(g->hud, "portal", 0.0f);
    if (!(a > 0.0f) || !g->portal_ok || !g->blocks->px) return;
    if (a < 1.0f)
    {
        a *= a;
        a *= a;
        a = a * 0.8f + 0.2f;
    }
    white(h);
    h->color[3] = a;
    tex_rect(h, g->blocks, 0.0f, 0.0f, (float)g->sw, (float)g->sh,
             g->portal_uv.min_u, g->portal_uv.min_v,
             g->portal_uv.max_u, g->portal_uv.max_v, BLEND_ALPHA);
}

static void draw_chat(struct gui *g);

/* GuiIngame.renderGameOverlay, in its own order. */
static void draw_hud(struct gui *g)

{
    struct hud *h = g->h;
    int sw = g->sw, sh = g->sh;
    const struct jval *bar = g->bar;

    white(h);
    modal(h, g->widgets, sw / 2 - 91, sh - 22, 0, 0, 182, 22);
    int cur = opt_int(g->hud, "cur", 0);
    modal(h, g->widgets, sw / 2 - 91 - 1 + cur * 20, sh - 22 - 1, 0, 22, 24, 22);
    /* the crosshair inverts what is under it */
    tex_rect(h, g->icons, (float)(sw / 2 - 7), (float)(sh / 2 - 7), 16, 16,
             0.0f, 0.0f, 16 * MODAL_UV, 16 * MODAL_UV, BLEND_INVERT);
    draw_boss(g);
    if (opt_int(bar, "hud", 1)) draw_bars(g);

    /* the hotbar's own slots */
    const struct jval *inv = json_get(g->hud, "inv");
    for (int i = 0; i < 9; ++i)
        draw_slot(g, inv ? json_at(inv, i) : NULL, sw / 2 - 90 + i * 20 + 2, sh - 16 - 3);

    /* the sleep fade: drawRect over the whole overlay in 0x101020 at
     * 220 x the timer's fraction (falling over 100 to 110 after a wake),
     * after the hotbar and before the bars that follow; alpha test off */
    int slp = opt_int(g->hud, "slp", 0);
    if (slp > 0)
    {
        float f = (float)slp / 100.0F;
        if (f > 1.0F) f = 1.0F - (float)(slp - 100) / 10.0F;
        int argb = (int)(220.0F * f) << 24 | 1052704;
        h->color[0] = (float)(argb >> 16 & 255) / 255.0F;
        h->color[1] = (float)(argb >> 8 & 255) / 255.0F;
        h->color[2] = (float)(argb & 255) / 255.0F;
        h->color[3] = (float)(argb >> 24 & 255) / 255.0F;
        int keep = h->no_alpha_test;
        h->no_alpha_test = 1;
        solid_quad(h, 0.0F, 0.0F, (float)sw, (float)sh, BLEND_ALPHA);
        h->no_alpha_test = keep;
        white(h);
    }

    if (opt_int(bar, "horse", 0))
    {
        int w = (int)(opt_float(bar, "jump", 0.0f) * 183.0f);
        modal(h, g->icons, sw / 2 - 91, sh - 32 + 3, 0, 84, 182, 5);
        if (w > 0) modal(h, g->icons, sw / 2 - 91, sh - 32 + 3, 0, 89, w, 5);
    }
    else if (opt_int(bar, "surv", 0))
    {
        int cap = opt_int(bar, "xpc", 0);
        if (cap > 0)
        {
            int w = (int)(opt_float(bar, "xp", 0.0f) * 183.0f);
            modal(h, g->icons, sw / 2 - 91, sh - 32 + 3, 0, 64, 182, 5);
            if (w > 0) modal(h, g->icons, sw / 2 - 91, sh - 32 + 3, 0, 69, w, 5);
        }
        int level = opt_int(bar, "lvl", 0);
        if (level > 0)
        {
            char text[16];
            snprintf(text, sizeof text, "%d", level);
            int x = (sw - font_width(&g->font, text)) / 2;
            int y = sh - 31 - 4;
            font_string(h, &g->font, text, x + 1, y, 0);
            font_string(h, &g->font, text, x - 1, y, 0);
            font_string(h, &g->font, text, x, y + 1, 0);
            font_string(h, &g->font, text, x, y - 1, 0);
            font_string(h, &g->font, text, x, y, 8453920);
        }
    }

    /* the held item's name, fading out over its ten ticks */
    if (opt_int(g->hud, "tooltips", 0))
    {
        int ticks = opt_int(g->hud, "hlt", 0);
        const char *name = json_str(json_get(g->hud, "hlname"));
        const struct jval *item = json_get(g->hud, "hl");
        if (ticks > 0 && name && present(item))
        {
            int x = (sw - font_width(&g->font, name)) / 2;
            int y = sh - 59 + (opt_int(bar, "hud", 1) ? 0 : 14);
            int alpha = ticks * 256 / 10;
            if (alpha > 255) alpha = 255;
            if (alpha > 0) font_shadow(h, &g->font, name, x, y, 16777215 | alpha << 24);
        }
    }
    draw_chat(g);
}

/* Gui.drawRect: the corners in either order, the ARGB colour blended */
static void rect_argb(struct hud *h, int x1, int y1, int x2, int y2, unsigned argb)
{
    if (x1 < x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 < y2) { int t = y1; y1 = y2; y2 = t; }
    h->color[0] = (float)(argb >> 16 & 255) / 255.0f;
    h->color[1] = (float)(argb >> 8 & 255) / 255.0f;
    h->color[2] = (float)(argb & 255) / 255.0f;
    h->color[3] = (float)(argb >> 24 & 255) / 255.0f;
    solid_quad(h, (float)x2, (float)y2, (float)x1, (float)y1, BLEND_ALPHA);
}

/* GuiNewChat.func_146230_a under renderGameOverlay's translate(0, sh - 48):
 * the lines newest at the bottom, each a background bar in its fade's half
 * alpha (blended, alpha test off) and its text in its fade's alpha (drawRect
 * leaves blending off, so the text replaces what is under it where the font
 * passes the alpha test). The lines are split here from the messages
 * (func_146237_a), the way GuiNewChat keeps them. */
static void draw_chat(struct gui *g)
{
    const struct jval *cj = json_get(g->hud, "chat");
    if (!cj || !opt_int(cj, "on", 0) || opt_int(cj, "vis", 0) == 2) return;
    struct hud *h = g->h;
    float scale = opt_float(cj, "sc", 1.0f), opacity = opt_float(cj, "op", 1.0f);
    int open = opt_int(cj, "open", 0), scroll = opt_int(cj, "scroll", 0);
    int colours = opt_int(cj, "col", 1);
    int width = chat_width(opt_float(cj, "wd", 1.0f));
    int max_lines = chat_height(open ? opt_float(cj, "hf", 1.0f) : opt_float(cj, "hu", 0.44366196f)) / 9;
    static struct chat chat;
    chat_free(&chat);
    const struct jval *msgs = json_get(cj, "msgs");
    int split_width = (int)floorf((float)width / scale);
    for (int i = json_len(msgs) - 1; i >= 0; --i)
    {
        const struct jval *mj = json_at(msgs, i);
        const struct jval *parts = json_get(mj, "parts");
        const char *fmt[CHAT_PARTS], *text[CHAT_PARTS];
        int n = json_len(parts) < CHAT_PARTS ? json_len(parts) : CHAT_PARTS;
        for (int k = 0; k < n; ++k)
        {
            fmt[k] = json_str(json_at(json_at(parts, k), 0));
            text[k] = json_str(json_at(json_at(parts, k), 1));
            if (!fmt[k]) fmt[k] = "";
            if (!text[k]) text[k] = "";
        }
        struct chat_message m;
        chat_message_set(&m, opt_int(mj, "t", 0), opt_int(mj, "id", 0), n, fmt, text);
        chat_add(&chat, &g->font.m, &m, split_width, colours);
    }
    if (chat.nline == 0) return;
    int uc = opt_int(g->hud, "uc", 0);
    float alpha_scale = opacity * 0.9f + 0.1f;
    int bar = ceil_float_int((float)width / scale);
    float keep_gx = h->gx, keep_gy = h->gy, keep_gs = h->gs;
    h->gx = 2.0f;
    h->gy = (float)(g->sh - 48) + 20.0f;
    h->gs = scale;
    int drawn = 0;   /* var4: the lines the loop counted */
    for (int i = 0; i + scroll < chat.nline && i < max_lines; ++i)
    {
        const struct chat_line *l = &chat.line[i + scroll];
        int age = uc - l->counter;
        if (age >= 200 && !open) continue;
        ++drawn;
        double fade = 1.0 - (double)age / 200.0;
        fade *= 10.0;
        if (fade < 0.0) fade = 0.0;
        if (fade > 1.0) fade = 1.0;
        fade *= fade;
        int a = (int)(255.0 * fade);
        if (open) a = 255;
        a = (int)((float)a * alpha_scale);
        if (a <= 3) continue;
        int y = -i * 9;
        /* Gui.drawRect: blended, untextured; the chat turned the alpha test off */
        h->no_alpha_test = 1;
        h->color[0] = h->color[1] = h->color[2] = 0.0f;
        h->color[3] = (float)(a / 2) / 255.0f;
        solid_quad(h, 0.0f, (float)(y - 9), (float)(bar + 4), (float)y, BLEND_ALPHA);
        /* drawStringWithShadow turns the alpha test back on */
        h->no_alpha_test = 0;
        h->text_blend = BLEND_NONE;
        font_shadow(h, &g->font, l->text, 0, y - 8, (int)(16777215u + ((unsigned)a << 24)));
        h->text_blend = BLEND_ALPHA;
    }
    if (open)
    {
        /* the open chat's scroll bar, translated 3 left: its thumb over the
         * lines drawn against every line, brighter when scrolled, red-tinted
         * after a line arrived while scrolled (field_146251_k) */
        int total = chat.nline * 9 + chat.nline, shown = drawn * 9 + drawn;
        /* var19 over the line count, var13 over the pixel total */
        int top = chat.nline ? scroll * shown / chat.nline : 0, len = total ? shown * shown / total : 0;
        if (total != shown)
        {
            unsigned a = top > 0 ? 170u : 96u;
            unsigned base = opt_int(cj, "sflag", 0) ? 13382451u : 3355562u;
            h->no_alpha_test = 1;
            h->gx -= 3.0f;
            rect_argb(h, 0, -top, 2, -top - len, base + (a << 24));
            rect_argb(h, 2, -top, 1, -top - len, 13421772u + (a << 24));
            h->gx += 3.0f;
            h->no_alpha_test = 0;
        }
    }
    h->gx = keep_gx; h->gy = keep_gy; h->gs = keep_gs;
    white(h);
}

/* --------------------------------------------------------------- the toast */

static void draw_toast(struct gui *g)
{
    struct hud *h = g->h;
    const struct jval *toast = g->toast;
    if (!toast || opt_int(toast, "on", 0) != 1) return;
    int desc = opt_int(toast, "desc", 0);
    double var1 = (double)((int64_t)ji(json_get(toast, "now")) - (int64_t)ji(json_get(toast, "l"))) / 3000.0;
    if (!desc)
    {
        if (var1 < 0.0 || var1 > 1.0) return;
    }
    else if (var1 > 0.5) var1 = 0.5;
    double var3 = var1 * 2.0;
    if (var3 > 1.0) var3 = 2.0 - var3;
    var3 *= 4.0;
    var3 = 1.0 - var3;
    if (var3 < 0.0) var3 = 0.0;
    var3 *= var3;
    var3 *= var3;
    int var5 = g->sw - 160;
    int var6 = 0 - (int)(var3 * 36.0);
    white(h);
    modal(h, g->achievement, var5, var6, 96, 202, 160, 32);
    if (desc) font_split(h, &g->font, json_str(json_get(toast, "sub")), var5 + 30, var6 + 7, 120);
    else
    {
        font_string(h, &g->font, json_str(json_get(toast, "title")), var5 + 30, var6 + 7, -256);
        font_string(h, &g->font, json_str(json_get(toast, "sub")), var5 + 30, var6 + 18, -1);
    }
    const struct jval *item = json_get(toast, "item");
    if (present(item) && opt_int(item, "mode", 0) == 0)
        item_icon(h, item_atlas(g, item), item, var5 + 8, var6 + 8, g->scene);
}

/* -------------------------------------------------------------- entry point */

/* 1 when any item this frame draws comes from the block atlas. */
static int needs_block_atlas(const struct jval *hud, const struct jval *toast, const struct jval *inv)
{
    const struct jval *item = json_get(toast, "item");
    if (opt_float(hud, "portal", 0.0f) > 0.0f && !opt_int(hud, "tportal", 0)) return 1;
    if (present(item) && opt_int(item, "sprite", 1) == 0) return 1;
    for (int i = 0; inv && i < json_len(inv); ++i)
    {
        const struct jval *it = json_at(inv, i);
        if (present(it) && opt_int(it, "sprite", 1) == 0) return 1;
    }
    (void)hud;
    return 0;
}

/* The scene's block atlas, for the flat icons of block items and for the
 * portal overlay's sprite, resolved by name. */
static int load_block_atlas(const char *scene, struct tex *t, struct hud_uv *portal)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/atlas.json", scene);
    size_t n = 0;
    char *text = read_file(p, &n);
    if (!text) return 0;
    struct jval *j = json_parse(text);
    if (!j) { fprintf(stderr, "raster_hud: invalid JSON in %s\n", p); exit(1); }
    t->w = ji(json_get(j, "atlas_width"));
    t->h = ji(json_get(j, "atlas_height"));
    t->min = opt_int(j, "min_filter", GL_NEAREST);
    t->mag = opt_int(j, "mag_filter", GL_NEAREST);
    if (portal)
    {
        const struct jval *sprites = json_get(j, "sprites");
        portal->min_u = portal->min_v = portal->max_u = portal->max_v = -1.0f;
        for (int i = 0; i < json_len(sprites); ++i)
        {
            const struct jval *sp = json_at(sprites, i);
            const char *name = json_str(json_get(sp, "name"));
            if (!name || strcmp(name, "portal")) continue;
            /* each uv is the raw float bits in a JSON integer */
            portal->min_u = uv_bits(json_get(sp, "minU"));
            portal->max_u = uv_bits(json_get(sp, "maxU"));
            portal->min_v = uv_bits(json_get(sp, "minV"));
            portal->max_v = uv_bits(json_get(sp, "maxV"));
            break;
        }
    }
    json_free(j);
    snprintf(p, sizeof p, "%s/atlas.rgba", scene);
    size_t want = (size_t)t->w * t->h * 4, got = 0;
    char *b = read_file(p, &got);
    if (!b || got != want) { fprintf(stderr, "raster_hud: cannot read %s\n", p); exit(1); }
    t->px = (unsigned char *)b;
    texanim_patch_scene(scene, TEXANIM_BLOCKS, t->px, t->w, t->h);
    return 1;
}

static struct gui toast_gui;
static struct hud toast_hh;
static int toast_pending;

int raster_hud_draw(unsigned char *rgb, int w, int h, const char *scene, const struct jval *hud)
{
    static char loaded_scene[1024];
    static struct tex ascii, icons, widgets, achievement, vignette, glint, items, blocks, chest, pumpkin;
    static struct tex chest_ender, chest_trapped;
    static struct hud_uv portal_uv;
    static int portal_ok;
    char p[1024];
    if (strcmp(loaded_scene, scene))
    {
        free(ascii.px); free(icons.px); free(widgets.px);
        free(achievement.px); free(vignette.px); free(glint.px); free(items.px); free(blocks.px);
        free(chest.px); free(pumpkin.px); free(chest_ender.px); free(chest_trapped.px);
        memset(&blocks, 0, sizeof blocks);
        memset(&chest, 0, sizeof chest);
        memset(&chest_ender, 0, sizeof chest_ender);
        memset(&chest_trapped, 0, sizeof chest_trapped);
        memset(&pumpkin, 0, sizeof pumpkin);
        snprintf(p, sizeof p, "%s/state/gui.json", scene);
        size_t n = 0;
        char *text = read_file(p, &n);
        if (!text) { fprintf(stderr, "raster_hud: no %s\n", p); exit(1); }
        struct jval *meta = json_parse(text);
        if (!meta) { fprintf(stderr, "raster_hud: invalid JSON in %s\n", p); exit(1); }
        snprintf(p, sizeof p, "%s/state/gui_ascii.rgba", scene);
        tex_load(&ascii, json_get(meta, "ascii"), p);
        snprintf(p, sizeof p, "%s/state/gui_icons.rgba", scene);
        tex_load(&icons, json_get(meta, "icons"), p);
        snprintf(p, sizeof p, "%s/state/gui_widgets.rgba", scene);
        tex_load(&widgets, json_get(meta, "widgets"), p);
        snprintf(p, sizeof p, "%s/state/gui_achievement.rgba", scene);
        tex_load(&achievement, json_get(meta, "achievement"), p);
        snprintf(p, sizeof p, "%s/state/gui_vignette.rgba", scene);
        tex_load(&vignette, json_get(meta, "vignette"), p);
        if (json_get(meta, "glint"))
        {
            snprintf(p, sizeof p, "%s/state/gui_glint.rgba", scene);
            tex_load(&glint, json_get(meta, "glint"), p);
        }
        else memset(&glint, 0, sizeof glint);
        snprintf(p, sizeof p, "%s/state/gui_items.rgba", scene);
        tex_load(&items, json_get(meta, "items"), p);
        texanim_patch_scene(scene, TEXANIM_ITEMS, items.px, items.w, items.h);
        if (json_get(meta, "chest")) {
            snprintf(p, sizeof p, "%s/state/gui_chest.rgba", scene);
            tex_load(&chest, json_get(meta, "chest"), p);
        }
        if (json_get(meta, "chest_ender")) {
            snprintf(p, sizeof p, "%s/state/gui_chest_ender.rgba", scene);
            tex_load(&chest_ender, json_get(meta, "chest_ender"), p);
        }
        if (json_get(meta, "chest_trapped")) {
            snprintf(p, sizeof p, "%s/state/gui_chest_trapped.rgba", scene);
            tex_load(&chest_trapped, json_get(meta, "chest_trapped"), p);
        }
        if (json_get(meta, "pumpkin")) {
            snprintf(p, sizeof p, "%s/state/gui_pumpkin.rgba", scene);
            tex_load(&pumpkin, json_get(meta, "pumpkin"), p);
        }
        /* a scene without them: the jar's entity/chest sheets */
        if (!chest.px) chest_tex_fallback("chest", &chest);
        if (!chest_ender.px) chest_tex_fallback("chest_ender", &chest_ender);
        if (!chest_trapped.px) chest_tex_fallback("chest_trapped", &chest_trapped);
        json_free(meta);
        portal_ok = 0;
        snprintf(loaded_scene, sizeof loaded_scene, "%s", scene);
    }
    if (!blocks.px && needs_block_atlas(hud, json_get(hud, "toast"), json_get(hud, "inv")))
        portal_ok = load_block_atlas(scene, &blocks, &portal_uv);
    /* the animated sprites as they stand at this draw (the live client's
     * animation moves every tick; a recorded scene's state is fixed): the
     * portal overlay, the hotbar's clock and compass */
    else if (blocks.px) texanim_patch_scene(scene, TEXANIM_BLOCKS, blocks.px, blocks.w, blocks.h);
    if (items.px) texanim_patch_scene(scene, TEXANIM_ITEMS, items.px, items.w, items.h);

    struct hud hh = {0};
    hh.rgb = rgb;
    hh.w = w;
    hh.h = h;
    double swd = opt_double(hud, "swd", 0), shd = opt_double(hud, "shd", 0);
    hh.sx = swd > 0 ? (double)w / swd : 1.0;
    hh.sy = shd > 0 ? (double)h / shd : 1.0;
    hh.text_blend = BLEND_ALPHA;
    white(&hh);

    struct gui g = {0};
    g.h = &hh;
    g.scene = scene;
    g.icons = &icons;
    g.widgets = &widgets;
    g.achievement = &achievement;
    g.vignette = &vignette;
    g.glint = &glint;
    g.items = &items;
    g.blocks = &blocks;
    g.pumpkin = &pumpkin;
    g.portal_uv = portal_uv;
    g.portal_ok = portal_ok;
    g.chest = &chest;
    g.chest_ender = &chest_ender;
    g.chest_trapped = &chest_trapped;
    g.hud = hud;
    g.bar = json_get(hud, "bar");
    g.toast = json_get(hud, "toast");
    json_int(json_get(g.toast, "now"), &g.now);
    g.sw = opt_int(hud, "sw", 0);
    g.sh = opt_int(hud, "sh", 0);
    g.font.tex = &ascii;
    font_metrics_load(&g.font.m, ascii.px, ascii.w, ascii.h);

    int hide = opt_int(hud, "hide", 0);
    if (!hide || opt_int(hud, "screen", 0))
    {
        if (opt_int(hud, "fancy", 0)) draw_vignette(&g);
        draw_pumpkin(&g);
        draw_portal(&g);
        draw_hud(&g);
    }
    if (toast_pending)
    {
        toast_hh = hh;
        toast_gui = g;
        toast_gui.h = &toast_hh;
    }
    else
    {
        draw_toast(&g);
    }
    return 1;
}

static int raster_gui_scene(unsigned char *rgb, int w, int h, const char *scene,
                            const struct jval *hud);

/* The toast draws after the open GuiScreen (vanilla's runGameLoop renders it last,
 * Minecraft.java:1056), so raster_hud() defers it: raster_hud_draw stashes the gui
 * here, raster_gui_scene runs, then raster_hud_toast draws it. Live frames without a
 * scene pass draw inside raster_hud_draw (deferred stays 0). */
int raster_hud_toast(void)
{
    if (toast_pending)
    {
        toast_pending = 0;
        draw_toast(&toast_gui);
    }
    return 1;
}

int raster_hud(unsigned char *rgb, int w, int h, const char *scene)
{
    struct jval *root = read_state(scene);
    if (!root) return 0;
    const struct jval *hud = json_get(root, "hud");
    if (!hud)
    {
        fprintf(stderr, "raster_hud: %s/state/frames.jsonl has no hud member, overlay not drawn\n", scene);
        json_free(root);
        return 0;
    }
    toast_pending = 1;
    int result = raster_hud_draw(rgb, w, h, scene, hud);
    result &= raster_gui_scene(rgb, w, h, scene, hud);
    result &= raster_hud_toast();
    json_free(root);
    return result;
}

/* The following is the live GuiScreen pass. The four 256x256 container sheets
 * are decoded from the owned client jar by make play into out/native/gui.
 * The existing HUD font, atlas and quad rasterizer are reused here so the
 * modal sheet and item glyphs sample at the same pixel centers. */
static struct tex screen_sheet[7];
static const char *screen_name[7] = {"inventory", "crafting_table", "furnace", "generic_54", "villager", "dispenser",
                                     "hopper"};

static pthread_mutex_t screen_sheet_lock = PTHREAD_MUTEX_INITIALIZER;
static const struct tex *screen_texture_load(int kind);

/* the sheet of a container kind, read once a process (draws on several
 * threads share it: lane/guiact) */
static const struct tex *screen_texture(int kind)
{
    if (kind < 0 || kind > 6) return NULL;
    pthread_mutex_lock(&screen_sheet_lock);
    const struct tex *t = screen_texture_load(kind);
    pthread_mutex_unlock(&screen_sheet_lock);
    return t;
}

static const struct tex *screen_texture_load(int kind)
{
    struct tex *t = &screen_sheet[kind];
    if (t->px) return t;
    char path[256];
    snprintf(path, sizeof path, "out/native/gui/%s.rgba", screen_name[kind]);
    size_t size = 0;
    t->px = (unsigned char *)read_file(path, &size);
    if (!t->px)
    {
        snprintf(path, sizeof path, "../out/native/gui/%s.rgba", screen_name[kind]);
        t->px = (unsigned char *)read_file(path, &size);
    }
    if (!t->px || size != 256U * 256U * 4U)
    {
        fprintf(stderr, "raster_hud: missing GUI sheet %s (run make -C csrc play)\n", path);
        free(t->px); t->px = NULL;
        return NULL;
    }
    t->w = t->h = 256;
    t->min = t->mag = GL_NEAREST;
    return t;
}

static void screen_gradient(unsigned char *rgb, int w, int h,
                            int y0, int y1, unsigned top, unsigned bottom)
{
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; ++y)
    {
        float f = (float)(y - y0 + 0.5f) / (float)(y1 - y0);
        float alpha = ((top >> 24 & 255) * (1.0f - f) + (bottom >> 24 & 255) * f) / 255.0f;
        float color[3];
        for (int c = 0; c < 3; ++c)
        {
            int shift = 16 - 8 * c;
            color[c] = (float)((top >> shift & 255) * (1.0f - f) + (bottom >> shift & 255) * f);
        }
        for (int x = 0; x < w; ++x)
        {
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            for (int c = 0; c < 3; ++c)
                p[c] = quant((color[c] * alpha + p[c] * (1.0f - alpha)) / 255.0f);
        }
    }
}

static void screen_rect_gradient(unsigned char *rgb, int w, int h, int scale,
                                 int x0, int y0, int x1, int y1,
                                 unsigned top, unsigned bottom)
{
    int px0 = x0 * scale, px1 = x1 * scale, py0 = y0 * scale, py1 = y1 * scale;
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > w) px1 = w;
    if (py1 > h) py1 = h;
    if (px0 >= px1 || py0 >= py1) return;
    for (int y = py0; y < py1; ++y)
    {
        float f = (float)(y - y0 * scale + 0.5f) / (float)((y1 - y0) * scale);
        float alpha = ((top >> 24 & 255) * (1.0f - f) + (bottom >> 24 & 255) * f) / 255.0f;
        float color[3];
        for (int c = 0; c < 3; ++c)
        {
            int shift = 16 - 8 * c;
            color[c] = (float)((top >> shift & 255) * (1.0f - f) + (bottom >> shift & 255) * f);
        }
        for (int x = px0; x < px1; ++x)
        {
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            for (int c = 0; c < 3; ++c)
                p[c] = quant((color[c] * alpha + p[c] * (1.0f - alpha)) / 255.0f);
        }
    }
}

/* Potion.potionTypes: each effect's en_US name (potion.*) and status icon
 * (setIconIndex x + 8 y; -1 for none). */
static const struct { const char *name; int icon; } POTIONS[24] = {
    {NULL, -1}, {"potion.moveSpeed", 0}, {"potion.moveSlowdown", 1}, {"potion.digSpeed", 2}, {"potion.digSlowDown", 3},
    {"potion.damageBoost", 4}, {"potion.heal", -1}, {"potion.harm", -1}, {"potion.jump", 10},
    {"potion.confusion", 11}, {"potion.regeneration", 7}, {"potion.resistance", 14}, {"potion.fireResistance", 15},
    {"potion.waterBreathing", 16}, {"potion.invisibility", 8}, {"potion.blindness", 13}, {"potion.nightVision", 12},
    {"potion.hunger", 9}, {"potion.weakness", 5}, {"potion.poison", 6}, {"potion.wither", 17}, {"potion.healthBoost", 18},
    {"potion.absorption", 18}, {"potion.saturation", -1}};

/* Enchantment.getTranslatedName: the en_US name, then enchantment.level.N
 * (I to X, the bare key past ten). */
static const char *ench_name(int id)
{
    switch (id)
    {
    case 0: return lang_text("enchantment.protect.all"); case 1: return lang_text("enchantment.protect.fire"); case 2: return lang_text("enchantment.protect.fall");
    case 3: return lang_text("enchantment.protect.explosion"); case 4: return lang_text("enchantment.protect.projectile"); case 5: return lang_text("enchantment.oxygen");
    case 6: return lang_text("enchantment.waterWorker"); case 7: return lang_text("enchantment.thorns"); case 16: return lang_text("enchantment.damage.all");
    case 17: return lang_text("enchantment.damage.undead"); case 18: return lang_text("enchantment.damage.arthropods"); case 19: return lang_text("enchantment.knockback");
    case 20: return lang_text("enchantment.fire"); case 21: return lang_text("enchantment.lootBonus"); case 32: return lang_text("enchantment.digging");
    case 33: return lang_text("enchantment.untouching"); case 34: return lang_text("enchantment.durability"); case 35: return lang_text("enchantment.lootBonusDigger");
    case 48: return lang_text("enchantment.arrowDamage"); case 49: return lang_text("enchantment.arrowKnockback"); case 50: return lang_text("enchantment.arrowFire"); case 51: return lang_text("enchantment.arrowInfinite");
    case 61: return lang_text("enchantment.lootBonusFishing"); case 62: return lang_text("enchantment.fishingSpeed");
    default: return NULL;
    }
}

static void ench_line(char *out, size_t n, int id, int lvl)
{
    static const char *roman_keys[11] = {NULL, "enchantment.level.1", "enchantment.level.2", "enchantment.level.3", "enchantment.level.4", "enchantment.level.5", "enchantment.level.6", "enchantment.level.7", "enchantment.level.8", "enchantment.level.9", "enchantment.level.10"};
    const char *name = ench_name(id);
    if (!name) { out[0] = 0; return; }
    if (lvl >= 1 && lvl <= 10) snprintf(out, n, "%s %s", name, lang_text(roman_keys[lvl]));
    else snprintf(out, n, "%s enchantment.level.%d", name, lvl);
}

/* ItemStack.field_111284_a, DecimalFormat("#.###"). */
static void dec3(char *out, size_t n, double v)
{
    snprintf(out, n, "%.3f", v);
    char *e = out + strlen(out) - 1;
    while (*e == '0') *e-- = 0;
    if (*e == '.') *e = 0;
}

/* attribute.modifier.plus/take.OP of an amount on attribute.name.KEY, in
 * blue or red (the op 1 and 2 amounts as percentages). */
static void modifier_line(char *out, size_t n, const char *attr, int op, double amount)
{
    char num[48];
    double shown = op == 1 || op == 2 ? amount * 100.0 : amount;
    if (amount > 0.0)
    {
        dec3(num, sizeof num, shown);
        snprintf(out, n, "\302\2479+%s%s %s", num, op ? "%" : "", attr);
    }
    else if (amount < 0.0)
    {
        dec3(num, sizeof num, -shown);
        snprintf(out, n, "\302\247c-%s%s %s", num, op ? "%" : "", attr);
    }
    else out[0] = 0;
}

#define TIP_LINES 24

/* ItemStack.getTooltip as GuiContainer.func_146285_a colours it: the name in
 * the stack's rarity colour (Item.getRarity: an enchanted stack is rare; the
 * golden apple, the records and a stored-enchantment book have their own),
 * then, each later line grey: Item.addInformation (a record's author and
 * title, a potion's effects and its When Drank block, a book's stored
 * enchantments), the tag's enchantments, a dyed piece's Dyed, and the
 * attribute modifiers (a sword's or tool's attack damage with Sharpness'
 * 1.25 a level). ItemFireworkCharge's and ItemFirework's lines too.
 * Custom names and lore need the anvil or commands. */
static int tooltip_lines(int id, int damage, int tag, const char *name, char lines[TIP_LINES][192])
{
    if (!name || !*name) return 0;
    const struct itag *t = tag ? itag_get(tag) : NULL;
    const char *rarity = "\302\247f";
    if (t && t->has_ench) rarity = "\302\247b";
    if (id == 322) rarity = damage == 0 ? "\302\247b" : "\302\247d"; /* ItemAppleGold */
    else if (id >= 2256 && id <= 2267) rarity = "\302\247b";         /* ItemRecord */
    else if (id == 403 && t && t->nstored > 0) rarity = "\302\247e";  /* ItemEnchantedBook */
    snprintf(lines[0], sizeof lines[0], "%s%s", rarity, name);
    int n = 1;
    char buf[128];
#define TIP_ADD(str) do { if (n < TIP_LINES) snprintf(lines[n++], sizeof lines[0], "\302\2477%.184s", (str)); } while (0)
    if (id >= 2256 && id <= 2267)
    {
        static const char *rec[12] = {"13", "cat", "blocks", "chirp", "far", "mall", "mellohi", "stal",
                                      "strad", "ward", "11", "wait"};
        snprintf(buf, sizeof buf, "C418 - %s", rec[id - 2256]);
        TIP_ADD(buf);
    }
    if (id == 373 && damage != 0)
    {
        /* ItemPotion.addInformation */
        struct potion_effect eff[8];
        int ne = potion_get_effects_for_damage(damage, eff, 8);
        static const char *potency[4] = {"", "II", "III", "IV"};
        char attrs[8][128];
        int na = 0;
        if (ne == 0) { snprintf(buf, sizeof buf, "\302\2477%s", lang_text("potion.empty")); TIP_ADD(buf); }
        for (int k = 0; k < ne; ++k)
        {
            const char *pn = eff[k].id < 24 && POTIONS[eff[k].id].name ? lang_text(POTIONS[eff[k].id].name) : "";
            char line[128];
            snprintf(line, sizeof line, "%s", pn);
            if (eff[k].amplifier > 0)
            {
                size_t l = strlen(line);
                snprintf(line + l, sizeof line - l, " %s",
                         eff[k].amplifier < 4 ? potency[eff[k].amplifier] : "");
            }
            if (eff[k].duration > 20)
            {
                int sec = eff[k].duration / 20, min = sec / 60;
                sec %= 60;
                size_t l = strlen(line);
                snprintf(line + l, sizeof line - l, " (%d:%s%d)", min, sec < 10 ? "0" : "", sec);
            }
            snprintf(buf, sizeof buf, "%s%s", potion_is_bad(eff[k].id) ? "\302\247c" : "\302\2477", line);
            TIP_ADD(buf);
            /* Potion.func_111186_k's modifiers through func_111183_a */
            int a1 = eff[k].amplifier + 1;
            if (na < 8)
            {
                if (eff[k].id == 1) modifier_line(attrs[na++], 128, lang_text("attribute.name.generic.movementSpeed"), 2, 0.20000000298023224 * a1);
                else if (eff[k].id == 2) modifier_line(attrs[na++], 128, lang_text("attribute.name.generic.movementSpeed"), 2, -0.15000000596046448 * a1);
                else if (eff[k].id == 5) modifier_line(attrs[na++], 128, lang_text("attribute.name.generic.attackDamage"), 2, 1.3 * a1);
                else if (eff[k].id == 18) modifier_line(attrs[na++], 128, lang_text("attribute.name.generic.attackDamage"), 0, (double)(-0.5F * (float)a1));
                else if (eff[k].id == 21) modifier_line(attrs[na++], 128, lang_text("attribute.name.generic.maxHealth"), 0, 4.0 * a1);
            }
        }
        if (na > 0)
        {
            TIP_ADD("");
            snprintf(buf, sizeof buf, "\302\2475%s", lang_text("potion.effects.whenDrank")); TIP_ADD(buf);
            for (int k = 0; k < na; ++k) TIP_ADD(attrs[k]);
        }
    }
    /* addInformation: ItemFireworkCharge's and ItemFirework's lines, grey */
    {
        char info[TIP_LINES][184];
        int ni = firework_info_lines(id, tag, info[0], sizeof info[0], TIP_LINES - 3);
        for (int k = 0; k < ni && n < TIP_LINES; ++k) snprintf(lines[n++], sizeof lines[0], "\302\2477%.184s", info[k]);
    }
    if (id == 403 && t)
        for (int k = 0; k < t->nstored; ++k)
        {
            ench_line(buf, sizeof buf, t->stored[k].id, t->stored[k].lvl);
            if (buf[0]) TIP_ADD(buf);
        }
    if (t && t->has_ench)
        for (int k = 0; k < t->nench; ++k)
        {
            ench_line(buf, sizeof buf, t->ench[k].id, t->ench[k].lvl);
            if (buf[0]) TIP_ADD(buf);
        }
    if (t && t->has_display && t->has_color) { snprintf(buf, sizeof buf, "\302\247o%s", lang_text("item.dyed")); TIP_ADD(buf); }
    if (id > 0 && id < 4096 && (ITEMS[id].kind == ITEM_TOOL || ITEMS[id].kind == ITEM_SWORD))
    {
        /* attribute.modifier.plus.0, "+%s %s"; the Item.field_111210_e
         * modifier adds EnchantmentHelper.func_152377_a at UNDEFINED
         * (Sharpness' 1.25 a level) */
        double dmg = (double)ITEMS[id].damage;
        int sharp = t ? itag_ench_level(tag, 16) : 0;
        if (sharp > 0) dmg += (double)((float)sharp * 1.25F);
        TIP_ADD("");
        modifier_line(buf, sizeof buf, lang_text("attribute.name.generic.attackDamage"), 0, dmg);
        TIP_ADD(buf);
    }
#undef TIP_ADD
    return n;
}

/* GuiScreen.func_146283_a: the frame and gradients around the widest line,
 * then the lines, the first two pixels clear of the rest. */
static void tooltip_draw(unsigned char *rgb, int w, int h, int scale, struct hud *hh, const struct font *font,
                         const char (*lines)[192], int n, int mx, int my, int sw, int sh);

static void screen_tooltip(unsigned char *rgb, int w, int h, int scale,
                           struct hud *hh, const struct font *font, int id, int damage, int tag,
                           const char *name, int mx, int my, int sw, int sh)
{
    char lines[TIP_LINES][192];
    int n = tooltip_lines(id, damage, tag, name, lines);
    if (n == 0) return;
    tooltip_draw(rgb, w, h, scale, hh, font, (const char (*)[192])lines, n, mx, my, sw, sh);
}

/* GuiScreen.drawHoveringText over n lines at the mouse */
static void tooltip_draw(unsigned char *rgb, int w, int h, int scale, struct hud *hh, const struct font *font,
                         const char (*lines)[192], int n, int mx, int my, int sw, int sh)
{
    if (n == 0) return;
    int width = 0;
    for (int i = 0; i < n; ++i)
        if (font_width(font, lines[i]) > width) width = font_width(font, lines[i]);
    int x = mx + 12, y = my - 12, height = 8;
    if (n > 1) height += 2 + (n - 1) * 10;
    if (x + width > sw) x -= 28 + width;
    if (y + height + 6 > sh) y = sh - height - 6;
    unsigned edge = (unsigned)-267386864;
    unsigned top = 1347420415U;
    unsigned bottom = ((top & 16711422U) >> 1) | (top & 0xff000000U);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y - 4, x + width + 3, y - 3, edge, edge);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y + height + 3, x + width + 3, y + height + 4, edge, edge);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y - 3, x + width + 3, y + height + 3, edge, edge);
    screen_rect_gradient(rgb, w, h, scale, x - 4, y - 3, x - 3, y + height + 3, edge, edge);
    screen_rect_gradient(rgb, w, h, scale, x + width + 3, y - 3, x + width + 4, y + height + 3, edge, edge);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y - 2, x - 2, y + height + 2, top, bottom);
    screen_rect_gradient(rgb, w, h, scale, x + width + 2, y - 2, x + width + 3, y + height + 2, top, bottom);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y - 3, x + width + 3, y - 2, top, top);
    screen_rect_gradient(rgb, w, h, scale, x - 3, y + height + 2, x + width + 3, y + height + 3, bottom, bottom);
    for (int i = 0; i < n; ++i)
    {
        font_shadow(hh, font, lines[i], x, y, -1);
        if (i == 0) y += 2;
        y += 10;
    }
}

static void screen_highlight(unsigned char *rgb, int w, int h, int x0, int y0, int scale)
{
    for (int y = y0 * scale; y < (y0 + 16) * scale && y < h; ++y)
        for (int x = x0 * scale; x < (x0 + 16) * scale && x < w; ++x)
        {
            if (x < 0 || y < 0) continue;
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            for (int c = 0; c < 3; ++c) p[c] = (unsigned char)((p[c] + 255) / 2);
        }
}

static void screen_button(struct hud *h, const struct tex *widgets,
                          const struct font *font, int x, int y, int width,
                          const char *label, int hover, int enabled)
{
    int state = enabled ? hover ? 2 : 1 : 0;
    white(h);
    modal(h, widgets, x, y, 0, 46 + state * 20, width / 2, 20);
    modal(h, widgets, x + width / 2, y, 200 - width / 2, 46 + state * 20, width / 2, 20);
    int color = !enabled ? 10526880 : hover ? 16777120 : 14737632;
    font_shadow(h, font, label, x + width / 2 - font_width(font, label) / 2, y + 6, color);
}

/* GuiContainer.func_146977_a's item: the stack's icon record from the item
 * table, then draw_stack. */
static void raster_hud_draw_item(struct gui *g, struct hud *hh, const char *hud_scene,
                                 const struct craft_stack *stack, int sx, int sy)
{
    (void)hh;
    const struct jval *entry = raster_hud_item_entry(hud_scene, stack->item, stack->damage);
    if (!entry) return;
    draw_stack(g, entry, stack->item, stack->damage, stack->count, stack->tag, sx, sy);
}

/* ----------------------------------------------------------- the credits */

/* One of GuiWinGame's texts beside the GUI sheets (out/native/gui, from the
 * client jar), split the way BufferedReader.readLine splits. */
static char **read_lines(const char *name, int *n)
{
    char path[256];
    snprintf(path, sizeof path, "out/native/gui/%s", name);
    char *text = read_file(path, NULL);
    if (!text)
    {
        snprintf(path, sizeof path, "../out/native/gui/%s", name);
        text = read_file(path, NULL);
    }
    *n = 0;
    if (!text) { fprintf(stderr, "raster_hud: missing %s (make -C csrc play)\n", name); return NULL; }
    int cap = 64;
    char **lines = alloc((size_t)cap * sizeof *lines);
    char *p = text;
    while (*p)
    {
        char *e = p;
        while (*e && *e != '\n' && *e != '\r') ++e;
        if (*n + 1 > cap) { cap *= 2; lines = realloc(lines, (size_t)cap * sizeof *lines); if (!lines) exit(1); }
        lines[(*n)++] = strndup(p, (size_t)(e - p));
        if (*e == '\r' && e[1] == '\n') ++e;
        p = *e ? e + 1 : e;
    }
    free(text);
    return lines;
}

/* s with every "from" replaced by "to", malloc'd (String.replaceAll with a
 * plain pattern). */
static char *replace_all(const char *s, const char *from, const char *to)
{
    size_t lf = strlen(from), lt = strlen(to), n = 0;
    for (const char *p = strstr(s, from); p; p = strstr(p + lf, from)) ++n;
    char *out = alloc(strlen(s) + n * (lt > lf ? lt - lf : 0) + 1), *o = out;
    for (;;)
    {
        const char *p = strstr(s, from);
        if (!p) { strcpy(o, s); break; }
        memcpy(o, s, (size_t)(p - s)); o += p - s;
        memcpy(o, to, lt); o += lt;
        s = p + lf;
    }
    return out;
}

/* GuiWinGame.initGui's list: end.txt with PLAYERNAME and the obfuscated
 * names (Random(8124371L) picks each's length), then eight blanks, then
 * credits.txt with its tabs as four spaces, every line wrapped to 274 and
 * followed by a blank. */
static char **credits_lines(const struct font_metrics *f, const char *user, int *count)
{
    int cap = 256, n = 0;
    char **out = alloc((size_t)cap * sizeof *out);
#define PUSH(str) do { if (n + 1 > cap) { cap *= 2; out = realloc(out, (size_t)cap * sizeof *out); if (!out) exit(1); } out[n++] = (str); } while (0)
    jrand rnd;
    jr_seed(&rnd, 8124371LL);
    const char *mark = "\302\247f\302\247k\302\247a\302\247b";
    for (int file = 0; file < 2; ++file)
    {
        int nl = 0;
        char **lines = read_lines(file == 0 ? "end.txt" : "credits.txt", &nl);
        for (int i = 0; i < nl; ++i)
        {
            char *line = replace_all(lines[i], "PLAYERNAME", user ? user : "Player");
            if (file == 1)
            {
                char *t = replace_all(line, "\t", "    ");
                free(line);
                line = t;
            }
            else
                for (char *at = strstr(line, mark); at; at = strstr(line, mark))
                {
                    char x[9];
                    int k = jr_int_n(&rnd, 4) + 3;
                    memcpy(x, "XXXXXXXX", (size_t)k);
                    x[k] = 0;
                    size_t head = (size_t)(at - line);
                    char *t = alloc(strlen(line) + 16);
                    memcpy(t, line, head);
                    snprintf(t + head, strlen(line) + 16 - head, "\302\247f\302\247k%s%s", x, at + strlen(mark));
                    free(line);
                    line = t;
                }
            int w = 0;
            char **wrapped = font_list_to_width(f, line, 274, &w);
            for (int k = 0; k < w; ++k) PUSH(wrapped[k]);
            free(wrapped);
            PUSH(strdup(""));
            free(line);
            free(lines[i]);
        }
        free(lines);
        if (file == 0)
            for (int k = 0; k < 8; ++k) PUSH(strdup(""));
    }
#undef PUSH
    *count = n;
    return out;
}

char **raster_hud_credits_lines(const char *scene, const char *user, int *n)
{
    struct tex ascii;
    *n = 0;
    if (!scene_tex(scene, "ascii", &ascii)) return NULL;
    struct font_metrics m;
    font_metrics_load(&m, ascii.px, ascii.w, ascii.h);
    free(ascii.px);
    return credits_lines(&m, user, n);
}

/* GuiWinGame.drawScreen: func_146575_b's scrolling dirt at its fade-in
 * brightness, the logo and the text scrolled up by the clock (the lines'
 * obfuscation drawn from fontRandom seeded per line), then the vignette
 * multiplied over the screen. */
static void draw_credits(struct gui *g, const struct gui_screen_extra *x, int sw, int sh)
{
    struct hud *h = g->h;
    /* zeroed: a scene without one of them stops the || chain before the
     * later ones are read, and all three are freed */
    struct tex bg = {0}, title = {0}, vignette = {0};
    if (!scene_tex(g->scene, "options_bg", &bg) || !scene_tex(g->scene, "title", &title) ||
        !scene_tex(g->scene, "vignette", &vignette))
    { free(bg.px); free(title.px); free(vignette.px); return; }
    bg.repeat = 1;
    int n = 0;
    char **lines = credits_lines(&g->font.m, x->user, &n);
    float t = (float)x->credits_time + x->partial_tick, speed = x->credits_speed;
    int total = n * 12;
    /* the background */
    float v6 = 0.0f - t * 0.5f * speed, v7 = (float)sh - t * 0.5f * speed, k = 0.015625f;
    float fade = (t - 0.0f) * 0.02f;
    float end = ((float)(total + sh + sh + 24) / speed - 20.0f - t) * 0.005f;
    if (end < fade) fade = end;
    if (fade > 1.0f) fade = 1.0f;
    fade *= fade;
    fade = fade * 96.0f / 255.0f;
    int c = (int)(fade * 255.0f);
    h->color[0] = h->color[1] = h->color[2] = (float)c / 255.0f;
    h->color[3] = 1.0f;
    h->tex = &bg;
    /* one plane per coordinate, as the rasterizer sets up a screen-aligned
     * quad: the value at the origin plus its step per device pixel */
    {
        float dudx = (float)sw * k / (float)h->w, dvdy = (v6 * k - v7 * k) / (float)h->h;
        for (int py = 0; py < h->h; ++py)
            for (int px = 0; px < h->w; ++px)
                shade(h, px, py, dudx * ((float)px + 0.5f), v7 * k + dvdy * ((float)py + 0.5f), BLEND_NONE);
    }
    /* the logo and the lines, under glTranslatef(0, scroll) */
    int left = sw / 2 - 274 / 2, top = sh + 50;
    float scroll = -t * speed;
    h->gx = 0.0f; h->gy = scroll; h->gs = 1.0f;
    white(h);
    modal(h, &title, left, top, 0, 0, 155, 44);
    modal(h, &title, left + 155, top, 0, 45, 155, 44);
    int y = top + 200;
    for (int i = 0; i < n; ++i, y += 12)
    {
        if (i == n - 1)
        {
            float over = (float)y + scroll - (float)(sh / 2 - 6);
            if (over < 0.0f) h->gy -= over;
        }
        if (!((float)y + scroll + 12.0f + 8.0f > 0.0f && (float)y + scroll < (float)sh)) continue;
        if (!strncmp(lines[i], "[C]", 3))
            font_shadow(h, &g->font, lines[i] + 3, left + (274 - font_width(&g->font, lines[i] + 3)) / 2, y, 16777215);
        else
        {
            jrand rnd;
            jr_seed(&rnd, (int64_t)i * 4238972211LL + (int64_t)(x->credits_time / 4));
            font_shadow_rnd(h, &g->font, lines[i], left, y, 16777215, &rnd);
        }
    }
    h->gx = h->gy = h->gs = 0.0f;
    white(h);
    tex_rect(h, &vignette, 0, 0, (float)sw, (float)sh, 0.0f, 0.0f, 1.0f, 1.0f, BLEND_MULTIPLY);
    for (int i = 0; i < n; ++i) free(lines[i]);
    free(lines);
    free(bg.px); free(title.px); free(vignette.px);
}

/* InventoryEffectRenderer.func_147044_g: a panel per effect left of the
 * inventory, 33 pixels apart (132 over the gaps past five), the status icon,
 * the name with its level, and Potion.getDurationString. */
static void draw_effects(struct gui *g, const struct tex *sheet, const struct gui_screen_extra *x,
                         int left, int top)
{
    struct hud *h = g->h;
    int px = left - 124, py = top;
    int step = x->neffects > 5 ? 132 / (x->neffects - 1) : 33;
    for (int i = 0; i < x->neffects; ++i, py += step)
    {
        const struct gui_effect *e = &x->effects[i];
        int id = e->id >= 0 && e->id < 24 ? e->id : 0;
        white(h);
        modal(h, sheet, px, py, 0, 166, 140, 32);
        if (POTIONS[id].icon >= 0)
            modal(h, sheet, px + 6, py + 7, POTIONS[id].icon % 8 * 18, 198 + POTIONS[id].icon / 8 * 18, 18, 18);
        char name[96];
        const char *level = e->amplifier == 1 ? " II" : e->amplifier == 2 ? " III" : e->amplifier == 3 ? " IV" : "";
        snprintf(name, sizeof name, "%s%s", POTIONS[id].name ? lang_text(POTIONS[id].name) : "", level);
        font_shadow(h, &g->font, name, px + 10 + 18, py + 6, 16777215);
        char dur[32];
        if (e->max) snprintf(dur, sizeof dur, "**:**");
        else
        {
            /* StringUtils.ticksToElapsedTime */
            int secs = e->duration / 20, mins = secs / 60;
            secs %= 60;
            snprintf(dur, sizeof dur, secs < 10 ? "%d:0%d" : "%d:%d", mins, secs);
        }
        font_shadow(h, &g->font, dur, px + 10 + 18, py + 6 + 10, 8355711);
    }
    white(h);
}

/* GuiTextField.drawTextBox, the chat's field (no background, enabled): the
 * text before the cursor, the rest after it (a bar cursor takes one pixel
 * back), the cursor while it blinks on ("_", or a bar when the cursor is
 * inside the text or the text is full), then the selection (func_146188_c:
 * glLogicOp OR_REVERSE with blue, each pixel's red and green inverted and
 * its blue full). */
static void draw_text_box(struct hud *hh, const struct font *font, const struct gui_chat_draw *d,
                          unsigned char *rgb, int w, int h, int scale)
{
    const int color = 14737632;
    int x0 = d->x, y = d->y;
    int xe = x0;
    if (d->vis_len > 0)
    {
        const char *first = d->on ? d->pre : d->vis;
        if (first[0]) font_shadow(hh, font, first, x0, y, color);
        xe = x0 + font_width(font, first) + 1;
    }
    int xc = xe;
    if (!d->on) xc = d->cursor > 0 ? x0 + d->w : x0;
    else if (d->bar)
    {
        xc = xe - 1;
        --xe;
    }
    if (d->vis_len > 0 && d->on && d->cursor < d->vis_len) font_shadow(hh, font, d->post, xe, y, color);
    if (d->blink_on)
    {
        if (d->bar) screen_rect_gradient(rgb, w, h, scale, xc, y - 1, xc + 1, y + 1 + 9, 0xFFD0D0D0U, 0xFFD0D0D0U);
        else font_shadow(hh, font, "_", xc, y, color);
    }
    int sel = d->sel > d->vis_len ? d->vis_len : d->sel;
    if (sel != d->cursor)
    {
        int x1 = xc, y1 = y - 1, x2 = x0 + font_width(font, d->sel_pre) - 1, y2 = y + 1 + 9;
        if (x1 < x2) { int t = x1; x1 = x2; x2 = t; }
        if (y1 < y2) { int t = y1; y1 = y2; y2 = t; }
        if (x2 > x0 + d->w) x2 = x0 + d->w;
        if (x1 > x0 + d->w) x1 = x0 + d->w;
        int px0 = x2 * scale, px1 = x1 * scale, py0 = y2 * scale, py1 = y1 * scale;
        if (px0 < 0) px0 = 0;
        if (py0 < 0) py0 = 0;
        if (px1 > w) px1 = w;
        if (py1 > h) py1 = h;
        for (int py = py0; py < py1; ++py)
            for (int px = px0; px < px1; ++px)
            {
                unsigned char *p = rgb + ((size_t)py * w + px) * 3;
                p[0] = (unsigned char)(255 - p[0]);
                p[1] = (unsigned char)(255 - p[1]);
                p[2] = 255;
            }
    }
}

/* raster_gui_live's textures, font and gui.json, read once a scene and
 * process and only read after (lane/guiact: they were read from disk at
 * every call, and draws on several threads share them) */
struct gui_assets {
    char scene[1024];
    int ok;
    struct tex ascii, widgets, items, glint, blocks, chest, chest_ender, chest_trapped;
    struct font_metrics font;
    struct jval *gui_json;
};
static pthread_mutex_t gui_assets_lock = PTHREAD_MUTEX_INITIALIZER;
static struct gui_assets *gui_assets_list[8];
static int gui_assets_n;

static int sheet_read(const char *scene, const char *name, int side, struct tex *t)
{
    char path[1024];
    size_t size = 0;
    snprintf(path, sizeof path, "%s/state/gui_%s.rgba", scene, name);
    t->px = (unsigned char *)read_file(path, &size);
    if (!t->px || size != (size_t)side * side * 4U) { free(t->px); t->px = NULL; return 0; }
    t->w = t->h = side;
    t->min = t->mag = GL_NEAREST;
    return 1;
}

static const struct gui_assets *gui_assets_get(const char *scene)
{
    pthread_mutex_lock(&gui_assets_lock);
    for (int i = 0; i < gui_assets_n; ++i)
        if (!strcmp(gui_assets_list[i]->scene, scene))
        {
            pthread_mutex_unlock(&gui_assets_lock);
            return gui_assets_list[i];
        }
    struct gui_assets *a = calloc(1, sizeof *a);
    if (!a) { fprintf(stderr, "raster_hud: allocation failed\n"); exit(1); }
    snprintf(a->scene, sizeof a->scene, "%s", scene);
    scene_tex(scene, "chest", &a->chest);
    scene_tex(scene, "chest_ender", &a->chest_ender);
    scene_tex(scene, "chest_trapped", &a->chest_trapped);
    a->ok = sheet_read(scene, "ascii", 128, &a->ascii) && sheet_read(scene, "widgets", 256, &a->widgets) &&
            sheet_read(scene, "items", 256, &a->items);
    sheet_read(scene, "glint", 64, &a->glint);
    load_block_atlas(scene, &a->blocks, NULL);
    if (a->ok) font_metrics_load(&a->font, a->ascii.px, a->ascii.w, a->ascii.h);
    char p[1024];
    snprintf(p, sizeof p, "%s/state/gui.json", scene);
    char *text = read_file(p, NULL);
    a->gui_json = text ? json_parse(text) : NULL;
    free(text);
    /* a full list keeps its scenes: one more is read at every call */
    if (gui_assets_n < (int)(sizeof gui_assets_list / sizeof gui_assets_list[0])) gui_assets_list[gui_assets_n++] = a;
    pthread_mutex_unlock(&gui_assets_lock);
    return a;
}

int raster_gui_live(unsigned char *rgb, int w, int h, const char *hud_scene,
                    const struct container *container, int inventory_open,
                    int death, int pause, int gui_scale, int mouse_x, int mouse_y,
                    int score, int death_ticks, int burn, int burn_total, int cook,
                    const struct gui_screen_extra *extra)
{
    int credits = extra && extra->credits;
    int sleep = extra && extra->sleep;
    int chat = extra && (extra->chat || extra->sleep);
    if (!inventory_open && !death && !pause && !credits && !chat) return 1;
    struct gui_screen_layout layout = gui_screen_layout(container, w, h, gui_scale);
    gui_screen_effects_shift(&layout, container, extra && extra->neffects > 0);
    const struct gui_assets *ga = gui_assets_get(hud_scene);
    if (!ga->ok) return 0;
    struct tex ascii = ga->ascii, items = ga->items, widgets = ga->widgets, glint = ga->glint, blocks = ga->blocks;
    struct tex chest = ga->chest, chest_ender = ga->chest_ender, chest_trapped = ga->chest_trapped;
    char path[1024];

    struct hud hh = {0};
    hh.rgb = rgb; hh.w = w; hh.h = h;
    hh.sx = hh.sy = layout.scale;
    hh.text_blend = BLEND_ALPHA;
    white(&hh);
    struct gui g = {0};
    g.h = &hh; g.font.tex = &ascii; g.items = &items; g.glint = &glint; g.blocks = &blocks;
    g.scene = hud_scene;
    g.now = extra ? extra->clock_ms : 0;
    g.chest = &chest; g.chest_ender = &chest_ender; g.chest_trapped = &chest_trapped;
    g.font.m = ga->font;

    if (credits)
        draw_credits(&g, extra, layout.sw, layout.sh);
    else if (death)
    {
        screen_gradient(rgb, w, h, 0, h, 1615855616U, (unsigned)-1602211792);
        hh.sx = hh.sy = layout.scale * 2;
        font_shadow(&hh, &g.font, lang_text("deathScreen.title"), layout.sw / 4 - font_width(&g.font, lang_text("deathScreen.title")) / 2, 30, 16777215);
        hh.sx = hh.sy = layout.scale;
        char label[80];
        snprintf(label, sizeof label, "%s: \247e%d", lang_text("deathScreen.score"), score);
        font_shadow(&hh, &g.font, label, layout.sw / 2 - font_width(&g.font, label) / 2, 100, 16777215);
        int x = layout.sw / 2 - 100, y = layout.sh / 4 + 72;
        screen_button(&hh, &widgets, &g.font, x, y, 200, lang_text("deathScreen.respawn"), mouse_x >= x && mouse_x < x + 200 && mouse_y >= y && mouse_y < y + 20, death_ticks >= 20);
        screen_button(&hh, &widgets, &g.font, x, y + 24, 200, lang_text("deathScreen.titleScreen"), mouse_x >= x && mouse_x < x + 200 && mouse_y >= y + 24 && mouse_y < y + 44, death_ticks >= 20);
    }
    else if (chat)
    {
        /* GuiChat.drawScreen: the strip, then GuiTextField.drawTextBox
         * without its background; GuiSleepMP's Leave Bed button after */
        screen_rect_gradient(rgb, w, h, layout.scale, 2, layout.sh - 14, layout.sw - 2, layout.sh - 2,
                             0x80000000U, 0x80000000U);
        draw_text_box(&hh, &g.font, &extra->chat_draw, rgb, w, h, layout.scale);
        tooltip_draw(rgb, w, h, layout.scale, &hh, &g.font, (const char (*)[192])extra->tip, extra->ntip,
                     extra->tip_x, extra->tip_y, layout.sw, layout.sh);
        if (sleep)
        {
            int x = layout.sw / 2 - 100, y = layout.sh - 40;
            screen_button(&hh, &widgets, &g.font, x, y, 200, lang_text("multiplayer.stopSleeping"),
                          mouse_x >= x && mouse_x < x + 200 && mouse_y >= y && mouse_y < y + 20, 1);
        }
    }
    else if (pause)
    {
        screen_gradient(rgb, w, h, 0, h, (unsigned)-1072689136, (unsigned)-804253680);
        font_shadow(&hh, &g.font, lang_text("menu.game"), layout.sw / 2 - font_width(&g.font, lang_text("menu.game")) / 2, 40, 16777215);
        screen_button(&hh, &widgets, &g.font, layout.sw / 2 - 100, layout.sh / 4 + 24,
                      200, lang_text("menu.returnToGame"), 0, 1);
    }
    else if (container)
    {
        screen_gradient(rgb, w, h, 0, h, (unsigned)-1072689136, (unsigned)-804253680);
        const struct tex *sheet = screen_texture(container->kind);
        if (!sheet) return 0;
        white(&hh);
        if (container->kind == CONTAINER_CHEST)
        {
            int rows = container->chest_size / 9;
            modal(&hh, sheet, layout.left, layout.top, 0, 0, 176, rows * 18 + 17);
            modal(&hh, sheet, layout.left, layout.top + rows * 18 + 17, 0, 126, 176, 96);
        }
        /* GuiHopper.func_146976_a: xSize by its ySize of 133 */
        else if (container->kind == CONTAINER_HOPPER) modal(&hh, sheet, layout.left, layout.top, 0, 0, 176, 133);
        else modal(&hh, sheet, layout.left, layout.top, 0, 0, 176, 166);
        if (container->kind == CONTAINER_PLAYER && extra && extra->has_preview && !extra->preview.invisible)
        {
            /* GuiInventory.func_146976_a: the player at (left + 51, top + 75),
             * scale 30, turned toward the stored mouse */
            snprintf(path, sizeof path, "%s/state/skin.rgba", hud_scene);
            size_t sn = 0;
            unsigned char *skin = (unsigned char *)read_file(path, &sn);
            if (!skin)
            {
                /* the live client's icon scene may have no skin dump: the
                 * same char.png bytes the mob pass reads */
                skin = (unsigned char *)read_file("out/assets/mobs/steve.rgba", &sn);
            }
            if (extra->preview_mob != NULL)
            {
                const struct gui_preview *gp = &extra->preview;
                struct player_preview pp = {
                    .dx = (float)(layout.left + 51) - gp->mouse_x,
                    .dy = (float)(layout.top + 75 - 50) - gp->mouse_y,
                    .prev_body_yaw = gp->prev_body_yaw, .prev_pitch = gp->prev_pitch,
                    .y_offset = gp->y_offset, .prev_body_own = gp->prev_body_own};
                raster_mobs_player_preview_full(extra->preview_assets, rgb, w, h, (double)w / layout.scale,
                                                (double)h / layout.scale, layout.left + 51, layout.top + 75, 30,
                                                &pp, extra->preview_mob, extra->preview_lm);
            }
            else if (skin && sn == 64U * 32U * 4U)
            {
                const struct gui_preview *gp = &extra->preview;
                struct player_preview pp = {
                    .dx = (float)(layout.left + 51) - gp->mouse_x,
                    .dy = (float)(layout.top + 75 - 50) - gp->mouse_y,
                    .prev_body_yaw = gp->prev_body_yaw, .prev_pitch = gp->prev_pitch,
                    .limb = gp->limb, .limb_amount = gp->limb_amount,
                    .prev_limb_amount = gp->prev_limb_amount, .swing = gp->swing,
                    .y_offset = gp->y_offset, .age = gp->age, .sneak = gp->sneak,
                    .riding = gp->riding, .held = gp->held, .prev_body_own = gp->prev_body_own};
                raster_mobs_player_preview(rgb, w, h, (double)w / layout.scale, (double)h / layout.scale,
                                           layout.left + 51, layout.top + 75, 30, skin, &pp);
            }
            free(skin);
        }

        if (container->kind == CONTAINER_PLAYER)
            font_string(&hh, &g.font, lang_text("container.crafting"), layout.left + 86, layout.top + 16, 4210752);
        else
        {
            const char *title = container->kind == CONTAINER_WORKBENCH ? lang_text("container.crafting") :
                                container->kind == CONTAINER_FURNACE ? lang_text("container.furnace") :
                                container->kind == CONTAINER_MERCHANT ?
                                    (extra && extra->merchant_name && extra->merchant_name[0] ?
                                     extra->merchant_name : lang_text("entity.Villager.name")) :
                                container->kind == CONTAINER_DISPENSER ?
                                    (!strcmp(container->title, "container.dropper") ? lang_text("container.dropper") : lang_text("container.dispenser")) :
                                container->kind == CONTAINER_HOPPER ? lang_text("container.hopper") :
                                container->chest_size == 54 ? lang_text("container.chestDouble") : lang_text("container.chest");
            /* GuiDispenser.func_146979_b centres its name like GuiFurnace */
            int tx = container->kind == CONTAINER_WORKBENCH ? 28 :
                     container->kind == CONTAINER_FURNACE || container->kind == CONTAINER_DISPENSER ?
                         88 - font_width(&g.font, title) / 2 :
                     container->kind == CONTAINER_MERCHANT ?
                         88 - font_width(&g.font, title) / 2 : 8;
            font_string(&hh, &g.font, title, layout.left + tx, layout.top + 6, 4210752);
            font_string(&hh, &g.font, lang_text("container.inventory"), layout.left + 8, layout.top + layout.height - 94, 4210752);
        }
        if (container->kind == CONTAINER_MERCHANT && container->recipes != NULL &&
            container->recipes->n > 0)
        {
            /* GuiMerchant's background layer: the disabled recipe's two grey
             * quads, then the arrows (the buttons draw before the slots'
             * neighbours do not overlap them). The arrow rows on the sheet:
             * u 176, disabled +24, hover +12, and the previous button's row
             * 19 pixels down. */
            int idx = container->current_recipe_index;
            if (idx >= container->recipes->n) idx = 0;
            const struct trade_recipe *r = &container->recipes->r[idx];
            white(&hh);

            if (trades_is_disabled(r))
            {
                modal(&hh, sheet, layout.left + 83, layout.top + 21, 212, 0, 28, 21);
                modal(&hh, sheet, layout.left + 83, layout.top + 51, 212, 0, 28, 21);
            }

            int next_hover = mouse_x >= layout.left + 147 && mouse_x < layout.left + 159 &&
                             mouse_y >= layout.top + 23 && mouse_y < layout.top + 42;
            int prev_hover = mouse_x >= layout.left + 17 && mouse_x < layout.left + 29 &&
                             mouse_y >= layout.top + 23 && mouse_y < layout.top + 42;
            int next_on = container->current_recipe_index < container->recipes->n - 1;
            int prev_on = container->current_recipe_index > 0;
            modal(&hh, sheet, layout.left + 147, layout.top + 23,
                  176 + (next_on ? (next_hover ? 12 : 0) : 24), 0, 12, 19);
            modal(&hh, sheet, layout.left + 17, layout.top + 23,
                  176 + (prev_on ? (prev_hover ? 12 : 0) : 24), 19, 12, 19);
        }
        if (container->kind == CONTAINER_FURNACE && burn > 0)
        {
            /* TileEntityFurnace.func_145955_e(13), a zero total read as 200 */
            white(&hh);
            int flame = burn * 13 / (burn_total == 0 ? 200 : burn_total);
            modal(&hh, sheet, layout.left + 56, layout.top + 48 - flame, 176, 12 - flame, 14, flame + 1);
            int progress = cook * 24 / 200;
            modal(&hh, sheet, layout.left + 79, layout.top + 34, 176, 14, progress + 1, 16);
        }
        for (int i = 0; i < container->nslots; ++i)
        {
            int sx, sy;
            if (!gui_screen_slot_xy(container, i, &sx, &sy)) continue;
            sx += layout.left; sy += layout.top;
            const struct craft_stack *stack = container_slot((struct container *)container, i);
            int mark = extra && i < GUI_DRAG_SLOTS ? extra->slot_mark[i] : 0;
            if (mark & GUI_MARK_HIDE) { /* func_146977_a returns before drawing */ }
            else if (stack && stack->count > 0)
            {
                /* a drag's share: the white square under the stack */
                if (mark & GUI_MARK_SHARE) screen_highlight(rgb, w, h, sx, sy, layout.scale);
                g.count_yellow = (mark & GUI_MARK_YELLOW) != 0;
                raster_hud_draw_item(&g, &hh, hud_scene, stack, sx, sy);
                g.count_yellow = 0;
            }
            else if (container->kind == CONTAINER_PLAYER && i >= 5 && i < 9)
            {
                /* Slot.getBackgroundIconIndex: ItemArmor.func_94602_b's empty
                 * slot sprite, drawTexturedModelRectFromIcon at 16x16 */
                static const char *const empty[4] = {"empty_armor_slot_helmet",
                    "empty_armor_slot_chestplate", "empty_armor_slot_leggings", "empty_armor_slot_boots"};
                struct hud_uv uv;
                if (item_sprite_in(ga->gui_json, empty[i - 5], &uv))
                {
                    white(&hh);
                    tex_rect(&hh, &items, (float)sx, (float)sy, 16, 16, uv.min_u, uv.min_v, uv.max_u, uv.max_v, BLEND_ALPHA);
                }
            }
            if (gui_screen_hit(container, layout, mouse_x, mouse_y) == i)
                screen_highlight(rgb, w, h, sx, sy, layout.scale);
        }
        if (container->kind == CONTAINER_MERCHANT && container->recipes != NULL &&
            container->recipes->n > 0)
        {
            /* GuiMerchant.drawScreen: the recipe items over slots 0/1/2
             * (zLevel 100, drawn after the slots and the buttons). */
            int idx = container->current_recipe_index;
            if (idx >= container->recipes->n) idx = 0;
            const struct trade_recipe *r = &container->recipes->r[idx];
            white(&hh);
            struct craft_stack buy = {r->buy.item, r->buy.count, r->buy.damage, r->buy.tag};
            struct craft_stack sell = {r->sell.item, r->sell.count, r->sell.damage, r->sell.tag};
            raster_hud_draw_item(&g, &hh, hud_scene, &buy, layout.left + 36, layout.top + 24);
            if (r->has_buy_b)
            {
                struct craft_stack buy_b = {r->buy_b.item, r->buy_b.count, r->buy_b.damage, r->buy_b.tag};
                raster_hud_draw_item(&g, &hh, hud_scene, &buy_b, layout.left + 62, layout.top + 24);
            }
            raster_hud_draw_item(&g, &hh, hud_scene, &sell, layout.left + 120, layout.top + 24);
        }
        if (container->cursor.count > 0)
        {
            /* a drag over more than one slot shows what the cursor keeps
             * (field_146996_I; a yellow "0" when nothing) */
            const struct craft_stack *stack = &container->cursor;
            const struct jval *entry = raster_hud_item_entry(hud_scene, stack->item, stack->damage);
            int n = extra && extra->drag_on ? extra->drag_remainder : stack->count;
            g.count_yellow = n == 0;
            if (entry) draw_stack(&g, entry, stack->item, stack->damage, n, stack->tag, mouse_x - 8, mouse_y - 8);
            g.count_yellow = 0;
        }
        else
        {
            int hovered = gui_screen_hit(container, layout, mouse_x, mouse_y);
            struct craft_stack *stack = container_slot((struct container *)container, hovered);
            if (stack && stack->count > 0)
                screen_tooltip(rgb, w, h, layout.scale, &hh, &g.font, stack->item, stack->damage, stack->tag,
                               raster_hud_item_name(hud_scene, stack->item, stack->damage),
                               mouse_x, mouse_y, layout.sw, layout.sh);
        }
        if (container->kind == CONTAINER_PLAYER && extra && extra->neffects > 0)
            draw_effects(&g, sheet, extra, layout.left, layout.top);
    }
    return 1;
}

static void screen_stack(struct craft_stack *s, const struct jval *j)
{
    craft_stack_free(s);
    s->item = -1; s->count = s->damage = 0; s->tag = 0;
    if (!present(j) || json_len(j) < 3) return;
    s->item = ji(json_at(j, 0));
    s->count = ji(json_at(j, 1));
    s->damage = ji(json_at(j, 2));
}

/* A trade stack out of a recipe's JSON object {item, count, damage}. */
static void screen_stack_trade(struct trade_stack *s, const struct jval *j)
{
    memset(s, 0, sizeof *s);
    if (!present(j) || json_len(j) < 3) { s->item = -1; return; }
    s->item = (short)ji(json_at(j, 0));
    s->count = (unsigned char)ji(json_at(j, 1));
    s->damage = (short)ji(json_at(j, 2));
}

static int raster_gui_scene(unsigned char *rgb, int w, int h, const char *scene,
                            const struct jval *hud)
{
    const struct jval *screen = json_get(hud, "guiScreen");
    if (!screen) return 1;
    const char *kind = json_str(json_get(screen, "kind"));
    if (!kind) return 1;
    int death = !strcmp(kind, "death");
    int credits = !strcmp(kind, "credits");
    int pause = !strcmp(kind, "pause");
    int container_kind = !strcmp(kind, "inventory") ? CONTAINER_PLAYER :
                         !strcmp(kind, "crafting") ? CONTAINER_WORKBENCH :
                         !strcmp(kind, "furnace") ? CONTAINER_FURNACE :
                         !strcmp(kind, "merchant") ? CONTAINER_MERCHANT : CONTAINER_CHEST;
    struct container c;
    struct container *cp = death || pause || credits ? NULL : &c;
    if (cp)
    {
        container_init(cp, container_kind, !strcmp(kind, "double_chest") ? 54 :
                       container_kind == CONTAINER_CHEST ? 27 : 0, 0);
        const struct jval *slots = json_get(screen, "slots");
        for (int i = 0; i < cp->nslots && i < json_len(slots); ++i)
        {
            struct slot *slot = &cp->slots[i];
            screen_stack(&slot->inv->slot[slot->index], json_at(slots, i));
        }
        screen_stack(&cp->cursor, json_get(screen, "cursor"));
        if (container_kind == CONTAINER_MERCHANT)
        {
            /* the recipe list the screen draws: ["recipes":[{"buy":..,
             * "sell":..,"buyB"?..,"uses"?,"maxUses"?..}], "trsel": n] */
            const struct jval *recipes = json_get(screen, "recipes");
            if (recipes)
            {
                int n = json_len(recipes) < TRADES_MAX ? json_len(recipes) : TRADES_MAX;
                cp->client_recipes.n = n;
                for (int i = 0; i < n; ++i)
                {
                    const struct jval *rj = json_at(recipes, i);
                    struct trade_recipe *r = &cp->client_recipes.r[i];
                    memset(r, 0, sizeof *r);
                    screen_stack_trade(&r->buy, json_get(rj, "buy"));
                    screen_stack_trade(&r->sell, json_get(rj, "sell"));
                    const struct jval *bb = json_get(rj, "buyB");
                    if (bb) { r->has_buy_b = 1; screen_stack_trade(&r->buy_b, bb); }
                    r->uses = opt_int(rj, "uses", 0);
                    r->max_uses = opt_int(rj, "maxUses", 7);
                }
                cp->recipes = &cp->client_recipes;
            }
            cp->current_recipe_index = opt_int(screen, "trsel", 0);
        }
    }
    /* the screen's extras: the merchant's name, the effects, the preview */
    static struct gui_screen_extra x;
    memset(&x, 0, sizeof x);
    x.merchant_name = json_str(json_get(screen, "name"));
    {
        int64_t now = 0;
        json_int(json_get(json_get(hud, "toast"), "now"), &now);
        x.clock_ms = now;
    }
    const struct jval *effects = json_get(screen, "effects");
    for (int i = 0; i < json_len(effects) && i < 32; ++i)
    {
        const struct jval *e = json_at(effects, i);
        x.effects[i] = (struct gui_effect){ji(json_at(e, 0)), ji(json_at(e, 1)), ji(json_at(e, 2)), ji(json_at(e, 3))};
        x.neffects = i + 1;
    }
    if (credits)
    {
        x.credits = 1;
        x.credits_time = opt_int(screen, "t", 0);
        x.credits_speed = opt_float(screen, "speed", 0.5f);
        x.partial_tick = 1.0f;
        x.user = json_str(json_get(screen, "user"));
    }
    const struct jval *pv = json_get(screen, "preview");
    if (pv)
    {
        x.has_preview = 1;
        x.preview.limb = opt_float(pv, "limb", 0);
        x.preview.limb_amount = opt_float(pv, "limba", 0);
        x.preview.prev_limb_amount = opt_float(pv, "plimba", 0);
        x.preview.prev_body_yaw = opt_float(pv, "pbyaw", 0);
        x.preview.prev_pitch = opt_float(pv, "ppitch", 0);
        x.preview.mouse_x = opt_float(screen, "pmx", 0);
        x.preview.mouse_y = opt_float(screen, "pmy", 0);
        x.preview.riding = opt_int(pv, "ride", 0);
        x.preview.held = present(json_get(pv, "held")) ? 1 : 0;
        x.preview.swing = opt_float(pv, "swing", 0);
        x.preview.y_offset = opt_float(pv, "yoff", 1.62f);
        x.preview.brightness = opt_float(pv, "bright", 1);
        x.preview.age = opt_int(pv, "age", 0);
        x.preview.hurt = opt_int(pv, "hurt", 0);
        x.preview.death = opt_int(pv, "death", 0);
        x.preview.sneak = opt_int(pv, "sneak", 0);
        x.preview.invisible = opt_int(pv, "invis", 0);
    }
    int result = raster_gui_live(rgb, w, h, scene, cp, cp != NULL, death, pause,
                                 opt_int(hud, "sf", 2), opt_int(screen, "mouseX", 0),
                                 opt_int(screen, "mouseY", 0), opt_int(screen, "score", 0),
                                 opt_int(screen, "deathTicks", 20), opt_int(screen, "burn", 0),
                                 opt_int(screen, "fuel", 200), opt_int(screen, "cook", 0), &x);
    if (cp) container_free(cp);
    return result;
}
