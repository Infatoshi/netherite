#define _POSIX_C_SOURCE 200809L
/* The tile-entity renderers (raster_tileent.h). Each renderer builds its
 * modelview with the GL11 calls in vanilla order through Mesa's matrix
 * arithmetic (rs_gl_*), from the camera modelview the frame recorded; the
 * lighting is RenderHelper.enableStandardItemLighting's two lights, set in eye
 * space under that camera, with the normal each TexturedQuad packs into the
 * Tessellator, as raster_hand.c evaluates it; the fragments go through the
 * entity rasterizer with the lightmap at the tile's brightness and the fog.
 * RenderEndPortal's layers are projective (glTexGen and a texture matrix), so
 * they have a rasterizer of their own here. */
#include "raster_tileent.h"
#include "raster_obs.h"
#include "raster_prec.h"
#include "font.h"
#include "jmath.h"
#include "jrand.h"
#include "raster_mobs.h"
#include "renderstate.h"
#include "tape.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.1415927f /* (float)Math.PI */

/* ------------------------------------------------------------ textures */

struct te_tex
{
    unsigned char *px;
    int w, h;
};

enum
{
    TT_CHEST, TT_CHEST_DOUBLE, TT_TRAPPED, TT_TRAPPED_DOUBLE, TT_ENDER, TT_SIGN,
    TT_STEVE, TT_END_SKY, TT_END_PORTAL, TT_ASCII,
    TT_SKELETON, TT_WITHER, TT_ZOMBIE, TT_CREEPER, TT_BOOK, TT_COUNT
};

static const struct { const char *scene, *asset; int w, h; } tex_files[TT_COUNT] = {
    {"state/te_chest.rgba", "te/chest.rgba", 64, 64},
    {"state/te_chest_double.rgba", "te/chest_double.rgba", 128, 64},
    {"state/te_trapped.rgba", "te/trapped.rgba", 64, 64},
    {"state/te_trapped_double.rgba", "te/trapped_double.rgba", 128, 64},
    {"state/te_ender.rgba", "te/ender.rgba", 64, 64},
    {"state/te_sign.rgba", "te/sign.rgba", 64, 32},
    {"state/te_steve.rgba", "mobs/steve.rgba", 64, 32},
    {"state/te_end_sky.rgba", "te/end_sky.rgba", 128, 128},
    {"state/te_end_portal.rgba", "te/end_portal.rgba", 256, 256},
    {"state/te_ascii.rgba", "te/ascii.rgba", 128, 128},
    {"state/skeleton.rgba", "mobs/skeleton.rgba", 64, 32},
    {"state/wither_skeleton.rgba", "mobs/wither_skeleton.rgba", 64, 32},
    {"state/zombie.rgba", "mobs/zombie.rgba", 64, 64},
    {"state/creeper.rgba", "mobs/creeper.rgba", 64, 32},
    {"state/te_enchanting_table_book.rgba", "te/enchanting_table_book.rgba", 64, 32},
};

static struct te_tex textures[TT_COUNT];
static char textures_from[1024];
static struct font_metrics font;
static int font_loaded;

static int read_exact(const char *path, unsigned char *out, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(out, 1, n, f) == n;
    fclose(f);
    return ok;
}

/* The texture I: the scene's own dump first, then the committed assets. */
static const struct te_tex *tex(const char *assets, int i)
{
    if (strcmp(textures_from, assets ? assets : ""))
    {
        for (int k = 0; k < TT_COUNT; ++k)
        {
            free(textures[k].px);
            textures[k] = (struct te_tex){0};
        }
        font_loaded = 0;
        snprintf(textures_from, sizeof textures_from, "%s", assets ? assets : "");
    }
    struct te_tex *t = &textures[i];
    if (t->px) return t;
    size_t n = (size_t)tex_files[i].w * tex_files[i].h * 4;
    unsigned char *px = malloc(n);
    if (!px) abort();
    char path[1400];
    int ok = 0;
    if (assets && *assets)
    {
        snprintf(path, sizeof path, "%s/%s", assets, tex_files[i].scene);
        ok = read_exact(path, px, n);
    }
    const char *roots[] = {"out/assets", "../out/assets"};
    for (int r = 0; !ok && r < 2; ++r)
    {
        snprintf(path, sizeof path, "%s/%s", roots[r], tex_files[i].asset);
        ok = read_exact(path, px, n);
    }
    if (!ok) { free(px); return NULL; }
    *t = (struct te_tex){px, tex_files[i].w, tex_files[i].h};
    return t;
}

/* ------------------------------------------------------------ GL state */

struct te_ctx
{
    struct entity_raster_target target; /* the frame and this pass's switches */
    const char *assets;
    const float *cam;                   /* the camera modelview */
    float mv[16];
    float light[2][3];                  /* the item lights in eye space */
    int brf;                            /* the lightmap coordinates, packed */
    float color[4];                     /* glColor */
    /* state one renderer leaves for the next */
    int cull;                           /* GL_CULL_FACE (the skull turns it off) */
    int rescale;                        /* GL_RESCALE_NORMAL (the skull leaves it on) */
    int normalize;                      /* GL_NORMALIZE (the frame's: a slime left it on) */
    int depth_mask;
};

static void mat3_apply(const float *m, const float v[3], float out[3])
{
    for (int r = 0; r < 3; ++r) out[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2];
}

static void normalize3(float v[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f)
        for (int i = 0; i < 3; ++i) v[i] /= len;
}

/* RenderHelper.enableStandardItemLighting under the camera modelview (as
 * EntityRenderer.renderWorld calls it before renderEntities). */
static void set_lights(struct te_ctx *c)
{
    static const double src[2][3] = {{0.20000000298023224, 1.0, -0.699999988079071},
                                     {-0.20000000298023224, 1.0, 0.699999988079071}};
    for (int i = 0; i < 2; ++i)
    {
        double len = (double)(float)sqrt(src[i][0] * src[i][0] + src[i][1] * src[i][1] +
                                         src[i][2] * src[i][2]);
        float l[3] = {(float)(src[i][0] / len), (float)(src[i][1] / len), (float)(src[i][2] / len)};
        mat3_apply(c->cam, l, c->light[i]);
        normalize3(c->light[i]);
    }
}

/* The eye-space normal: the inverse transpose of the modelview's upper 3x3,
 * scaled by the reciprocal length of the inverse's third row under
 * GL_RESCALE_NORMAL (Mesa's _ModelViewInvScaleEyespace), unscaled without it
 * (a scaled modelview then lengthens or shortens the normal); GL_NORMALIZE
 * then makes it unit length. */
static void eye_normal(const float *m, const float n[3], int rescale, int normalize, float out[3])
{
    double a = m[0], b = m[4], cc = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], k = m[10];
    double det = a * (e * k - f * h) - b * (d * k - f * g) + cc * (d * h - e * g);
    if (det == 0.0) { out[0] = n[0]; out[1] = n[1]; out[2] = n[2]; return; }
    float inv[3][3] = {
        {(float)((e * k - f * h) / det), (float)((cc * h - b * k) / det), (float)((b * f - cc * e) / det)},
        {(float)((f * g - d * k) / det), (float)((a * k - cc * g) / det), (float)((cc * d - a * f) / det)},
        {(float)((d * h - e * g) / det), (float)((b * g - a * h) / det), (float)((a * e - b * d) / det)}};
    for (int j = 0; j < 3; ++j) out[j] = n[0] * inv[0][j] + n[1] * inv[1][j] + n[2] * inv[2][j];
    if (normalize)
    {
        normalize3(out);
        return;
    }
    if (!rescale) return;
    float s = inv[2][0] * inv[2][0] + inv[2][1] * inv[2][1] + inv[2][2] * inv[2][2];
    if (s < 1e-12f) s = 1.0f;
    s = 1.0f / sqrtf(s);
    for (int j = 0; j < 3; ++j) out[j] *= s;
}

/* The lit factor for a white material: the scene ambient 0.4 plus each
 * light's diffuse 0.6 times max(0, N.L), N packed to bytes as
 * Tessellator.setNormal does. */
static float lit(const struct te_ctx *c, const float n[3])
{
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = (float)(signed char)(int)(n[i] * 127.0f) / 127.0f;
    float e[3];
    eye_normal(c->mv, q, c->rescale, c->normalize, e);
    float d = 0.4f;
    for (int i = 0; i < 2; ++i)
    {
        float dot = e[0] * c->light[i][0] + e[1] * c->light[i][1] + e[2] * c->light[i][2];
        if (dot > 0.0f) d += 0.6f * dot;
    }
    return d;
}

static struct entity_clip_vertex vertex(const struct te_ctx *c, const float p[3], float u, float v,
                                        float diffuse)
{
    struct entity_clip_vertex out;
    memset(&out, 0, sizeof out);
    float eye[4], w[4] = {p[0], p[1], p[2], 1.0f};
    for (int r = 0; r < 4; ++r)
        eye[r] = c->mv[r] * w[0] + c->mv[4 + r] * w[1] + c->mv[8 + r] * w[2] + c->mv[12 + r] * w[3];
    const float *proj = c->target.proj;
    for (int r = 0; r < 4; ++r)
        out.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
    out.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    out.u = u;
    out.v = v;
    out.diffuse = diffuse;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1.0f;
    out.light[0] = (float)(c->brf % 65536);
    out.light[1] = (float)(c->brf / 65536);
    return out;
}

/* One Tessellator quad with its normal, in the current modelview and glColor. */
static void quad(struct te_ctx *c, const float p[4][3], const float uv[4][2], const float n[3],
                 const struct te_tex *t)
{
    if (!t) return;
    float d = lit(c, n);
    struct entity_clip_vertex q[4];
    for (int k = 0; k < 4; ++k) q[k] = vertex(c, p[k], uv[k][0], uv[k][1], d);
    struct entity_raster_target rt = c->target;
    for (int i = 0; i < 3; ++i) rt.material[i] = c->color[i];
    rt.two_sided = !c->cull;
    if (!c->depth_mask)
    {
        /* depth writes off: the blend path with a whole alpha writes the
         * same colour and leaves the depth buffer alone */
        rt.blend = 1;
        rt.alpha = 1.0f;
    }
    raster_entity_quad(&rt, t->px, t->w, t->h, q);
}

/* ModelBox.render compiled at 0.0625: six TexturedQuads, each normal from its
 * own vertices (TexturedQuad.draw), the texture size the ModelRenderer's. */
static void model_box(struct te_ctx *c, float x, float y, float z, int dx, int dy, int dz, int u,
                      int v, int tw, int th, const struct te_tex *t)
{
    const float scale = 0.0625f;
    float x0 = x, x1 = x + (float)dx, y0 = y, y1 = y + (float)dy, z0 = z, z1 = z + (float)dz;
    float p[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                     {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    static const int face[6][4] = {{5, 1, 2, 6}, {0, 4, 7, 3}, {5, 4, 0, 1},
                                   {2, 3, 7, 6}, {1, 0, 3, 2}, {4, 5, 6, 7}};
    int uv[6][4] = {{u + dz + dx, v + dz, u + dz + dx + dz, v + dz + dy},
                    {u, v + dz, u + dz, v + dz + dy},
                    {u + dz, v, u + dz + dx, v + dz},
                    {u + dz + dx, v + dz, u + dz + dx + dx, v},
                    {u + dz, v + dz, u + dz + dx, v + dz + dy},
                    {u + dz + dx + dz, v + dz, u + dz + dx + dz + dx, v + dz + dy}};
    for (int f = 0; f < 6; ++f)
    {
        const float *a0 = p[face[f][0]], *a1 = p[face[f][1]], *a2 = p[face[f][2]];
        double va[3], vb[3];
        for (int i = 0; i < 3; ++i)
        {
            va[i] = (double)a0[i] - (double)a1[i];
            vb[i] = (double)a2[i] - (double)a1[i];
        }
        double n[3] = {vb[1] * va[2] - vb[2] * va[1], vb[2] * va[0] - vb[0] * va[2],
                       vb[0] * va[1] - vb[1] * va[0]};
        double len = (double)(float)sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); /* Vec3.normalize */
        float nf[3] = {0, 0, 0};
        if (len >= 1.0E-4)
            for (int i = 0; i < 3; ++i) nf[i] = (float)(n[i] / len);
        float us[4] = {(float)uv[f][2] / (float)tw, (float)uv[f][0] / (float)tw,
                       (float)uv[f][0] / (float)tw, (float)uv[f][2] / (float)tw};
        float vs[4] = {(float)uv[f][1] / (float)th, (float)uv[f][1] / (float)th,
                       (float)uv[f][3] / (float)th, (float)uv[f][3] / (float)th};
        float qp[4][3], quv[4][2];
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < 3; ++i) qp[k][i] = p[face[f][k]][i] * scale;
            quv[k][0] = us[k];
            quv[k][1] = vs[k];
        }
        quad(c, qp, quv, nf, t);
    }
}

/* One ModelRenderer with one box: ModelRenderer.render(0.0625). */
struct te_part
{
    float px, py, pz;       /* rotationPoint */
    float ax, ay, az;       /* rotateAngle */
    float bx, by, bz;       /* the box */
    int dx, dy, dz, u, v, tw, th;
};

static void render_part(struct te_ctx *c, const struct te_part *p, const struct te_tex *t)
{
    const float s = 0.0625f;
    rs_gl_translate(c->mv, 0.0f, 0.0f, 0.0f); /* offsetX, offsetY, offsetZ */
    if (p->ax == 0.0f && p->ay == 0.0f && p->az == 0.0f)
    {
        if (p->px == 0.0f && p->py == 0.0f && p->pz == 0.0f)
            model_box(c, p->bx, p->by, p->bz, p->dx, p->dy, p->dz, p->u, p->v, p->tw, p->th, t);
        else
        {
            rs_gl_translate(c->mv, p->px * s, p->py * s, p->pz * s);
            model_box(c, p->bx, p->by, p->bz, p->dx, p->dy, p->dz, p->u, p->v, p->tw, p->th, t);
            rs_gl_translate(c->mv, -p->px * s, -p->py * s, -p->pz * s);
        }
        return;
    }
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    rs_gl_translate(c->mv, p->px * s, p->py * s, p->pz * s);
    if (p->az != 0.0f) rs_gl_rotate(c->mv, p->az * (180.0f / PI_F), 0.0f, 0.0f, 1.0f);
    if (p->ay != 0.0f) rs_gl_rotate(c->mv, p->ay * (180.0f / PI_F), 0.0f, 1.0f, 0.0f);
    if (p->ax != 0.0f) rs_gl_rotate(c->mv, p->ax * (180.0f / PI_F), 1.0f, 0.0f, 0.0f);
    model_box(c, p->bx, p->by, p->bz, p->dx, p->dy, p->dz, p->u, p->v, p->tw, p->th, t);
    memcpy(c->mv, saved, sizeof saved);
}

/* ------------------------------------------------------------ the book */

/* RenderEnchantmentTable.renderTileEntityAt: ModelBook over the table,
 * bobbing on the tick count, turned toward the reader, its covers opened by
 * the spread and its two loose pages at the flip's fractions. */
static void render_enchant_table(struct te_ctx *c, const struct te_render_item *e, const float d[3], float pt)
{
    const struct te_tex *t = tex(c->assets, TT_BOOK);
    rs_gl_translate(c->mv, d[0] + 0.5f, d[1] + 0.75f, d[2] + 0.5f);
    float ticks = (float)e->book_ticks + pt;
    rs_gl_translate(c->mv, 0.0f, 0.1f + mh_sin(ticks * 0.1f) * 0.01f, 0.0f);
    float turn;
    for (turn = e->book_rot - e->book_prev_rot; turn >= PI_F; turn -= PI_F * 2.0f) {}
    while (turn < -PI_F) turn += PI_F * 2.0f;
    float rot = e->book_prev_rot + turn * pt;
    rs_gl_rotate(c->mv, -rot * 180.0f / PI_F, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(c->mv, 80.0f, 0.0f, 0.0f, 1.0f);
    float f1 = e->book_prev_flip + (e->book_flip - e->book_prev_flip) * pt + 0.25f;
    float f2 = e->book_prev_flip + (e->book_flip - e->book_prev_flip) * pt + 0.75f;
    /* MathHelper.truncateDoubleToInt: (int)(d + 1024.0D) - 1024 */
    f1 = (f1 - (float)((int)((double)f1 + 1024.0) - 1024)) * 1.6f - 0.3f;
    f2 = (f2 - (float)((int)((double)f2 + 1024.0) - 1024)) * 1.6f - 0.3f;
    if (f1 < 0.0f) f1 = 0.0f;
    if (f2 < 0.0f) f2 = 0.0f;
    if (f1 > 1.0f) f1 = 1.0f;
    if (f2 > 1.0f) f2 = 1.0f;
    float spread = e->book_prev_spread + (e->book_spread - e->book_prev_spread) * pt;
    c->cull = 1;
    c->color[0] = c->color[1] = c->color[2] = c->color[3] = 1.0f;
    /* ModelBook.setRotationAngles */
    float open = (mh_sin(ticks * 0.02f) * 0.1f + 1.25f) * spread;
    float px = mh_sin(open);
    struct te_part parts[7] = {
        {0, 0, -1, 0, PI_F + open, 0, -6, -5, 0, 6, 10, 0, 0, 0, 64, 32},          /* coverRight */
        {0, 0, 1, 0, -open, 0, 0, -5, 0, 6, 10, 0, 16, 0, 64, 32},                 /* coverLeft */
        {0, 0, 0, 0, PI_F / 2.0f, 0, -1, -5, 0, 2, 10, 0, 12, 0, 64, 32},          /* bookSpine */
        {px, 0, 0, 0, open, 0, 0, -4, -0.99f, 5, 8, 1, 0, 10, 64, 32},             /* pagesRight */
        {px, 0, 0, 0, -open, 0, 0, -4, -0.01f, 5, 8, 1, 12, 10, 64, 32},           /* pagesLeft */
        {px, 0, 0, 0, open - open * 2.0f * f1, 0, 0, -4, 0, 5, 8, 0, 24, 10, 64, 32}, /* flippingPageRight */
        {px, 0, 0, 0, open - open * 2.0f * f2, 0, 0, -4, 0, 5, 8, 0, 24, 10, 64, 32}  /* flippingPageLeft */
    };
    for (int i = 0; i < 7; ++i) render_part(c, &parts[i], t);
}

/* ------------------------------------------------------------ chests */

/* ModelChest (or ModelLargeChest when LARGE) with the lid at ANGLE:
 * renderAll's lid, knob and bottom. */
static void model_chest(struct te_ctx *c, int large, float angle, const struct te_tex *t)
{
    int tw = large ? 128 : 64, w = large ? 30 : 14;
    struct te_part lid = {1, 7, 15, angle, 0, 0, 0, -5, -14, w, 5, 14, 0, 0, tw, 64};
    struct te_part knob = {large ? 16.0f : 8.0f, 7, 15, angle, 0, 0, -1, -2, -15, 2, 4, 1, 0, 0, tw, 64};
    struct te_part below = {1, 6, 1, 0, 0, 0, 0, 0, 0, w, 10, 14, 0, 19, tw, 64};
    render_part(c, &lid, t);
    render_part(c, &knob, t);
    render_part(c, &below, t);
}

/* The lid angle from lidAngle and prevLidAngle at the partial tick. */
static float lid_angle(float lid, float prev, float pt)
{
    float a = prev + (lid - prev) * pt;
    a = 1.0f - a;
    a = 1.0f - a * a * a;
    return -(a * PI_F / 2.0f);
}

/* TileEntityChestRenderer and TileEntityEnderChestRenderer. */
static void render_chest(struct te_ctx *c, const struct te_render_item *e, const float d[3], float pt)
{
    int large = 0;
    const struct te_tex *t;
    if (e->kind == TE_RENDER_ENDER_CHEST)
        t = tex(c->assets, TT_ENDER);
    else
    {
        /* a double chest draws once, from the half with no z- or x- neighbour */
        if (e->adj[0] || e->adj[2]) return;
        large = e->adj[1] || e->adj[3];
        int trapped = e->block == 146;
        t = tex(c->assets, large ? (trapped ? TT_TRAPPED_DOUBLE : TT_CHEST_DOUBLE)
                                 : (trapped ? TT_TRAPPED : TT_CHEST));
    }
    int meta = e->meta;
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    c->rescale = 1;
    c->color[0] = c->color[1] = c->color[2] = c->color[3] = 1.0f;
    rs_gl_translate(c->mv, d[0], d[1] + 1.0f, d[2] + 1.0f);
    rs_gl_scale(c->mv, 1.0f, -1.0f, -1.0f);
    rs_gl_translate(c->mv, 0.5f, 0.5f, 0.5f);
    int ang = meta == 2 ? 180 : meta == 3 ? 0 : meta == 4 ? 90 : meta == 5 ? -90 : 0;
    if (e->kind == TE_RENDER_CHEST)
    {
        if (meta == 2 && e->adj[1]) rs_gl_translate(c->mv, 1.0f, 0.0f, 0.0f);
        if (meta == 5 && e->adj[3]) rs_gl_translate(c->mv, 0.0f, 0.0f, -1.0f);
    }
    rs_gl_rotate(c->mv, (float)ang, 0.0f, 1.0f, 0.0f);
    rs_gl_translate(c->mv, -0.5f, -0.5f, -0.5f);
    model_chest(c, large, lid_angle(e->lid, e->prev_lid, pt), t);
    c->rescale = 0;
    memcpy(c->mv, saved, sizeof saved);
}

/* ------------------------------------------------------------ signs */

/* FontRenderer.renderDefaultChar: glyph C's triangle strip at (x, y). */
static void glyph(struct te_ctx *c, const struct te_tex *t, int ch, float x, float y, const float n[3])
{
    float u = (float)(ch % 16 * 8), v = (float)(ch / 16 * 8);
    float w = (float)font.cw[ch] - 0.01f;
    float p[4][3] = {{x, y, 0}, {x, y + 7.99f, 0}, {x + w - 1.0f, y + 7.99f, 0}, {x + w - 1.0f, y, 0}};
    float uv[4][2] = {{u / 128.0f, v / 128.0f}, {u / 128.0f, (v + 7.99f) / 128.0f},
                      {(u + w - 1.0f) / 128.0f, (v + 7.99f) / 128.0f}, {(u + w - 1.0f) / 128.0f, v / 128.0f}};
    quad(c, p, uv, n, t);
}

/* FontRenderer.drawString(s, x, y, 0) with the sign's glNormal3f. */
static void draw_string(struct te_ctx *c, const char *s, int x, int y, const float n[3])
{
    const struct te_tex *t = tex(c->assets, TT_ASCII);
    if (!t) return;
    /* renderString: colour 0 gets an opaque alpha */
    c->color[0] = c->color[1] = c->color[2] = 0.0f;
    c->color[3] = 1.0f;
    size_t len;
    uint32_t *cp = font_utf8_decode(s, &len);
    float px = (float)x, py = (float)y;
    for (size_t i = 0; cp && i < len; ++i)
    {
        uint32_t ch = cp[i];
        if (ch == FONT_SECTION && i + 1 < len) { ++i; continue; } /* colour codes: black stays black */
        float adv = 0.0f;
        int gi = font_char_index(ch);
        if (ch == 32) adv = 4.0f;
        else if (gi != -1) { glyph(c, t, gi, px, py, n); adv = (float)font.cw[gi]; }
        px += adv;
    }
    free(cp);
}

static void render_sign(struct te_ctx *c, const struct te_render_item *e, const float d[3])
{
    const struct te_tex *t = tex(c->assets, TT_SIGN);
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    float var10 = 0.6666667f;
    int stick;
    if (e->block == 63)
    {
        rs_gl_translate(c->mv, d[0] + 0.5f, d[1] + 0.75f * var10, d[2] + 0.5f);
        float var11 = (float)(e->meta * 360) / 16.0f;
        rs_gl_rotate(c->mv, -var11, 0.0f, 1.0f, 0.0f);
        stick = 1;
    }
    else
    {
        float var12 = e->meta == 2 ? 180.0f : e->meta == 4 ? 90.0f : e->meta == 5 ? -90.0f : 0.0f;
        rs_gl_translate(c->mv, d[0] + 0.5f, d[1] + 0.75f * var10, d[2] + 0.5f);
        rs_gl_rotate(c->mv, -var12, 0.0f, 1.0f, 0.0f);
        rs_gl_translate(c->mv, 0.0f, -0.3125f, -0.4375f);
        stick = 0;
    }
    float inner[16];
    memcpy(inner, c->mv, sizeof inner);
    rs_gl_scale(c->mv, var10, -var10, -var10);
    struct te_part board = {0, 0, 0, 0, 0, 0, -12, -14, -1, 24, 12, 2, 0, 0, 64, 32};
    struct te_part post = {0, 0, 0, 0, 0, 0, -1, -2, -1, 2, 14, 2, 0, 14, 64, 32};
    c->color[0] = c->color[1] = c->color[2] = c->color[3] = 1.0f;
    render_part(c, &board, t);
    if (stick) render_part(c, &post, t);
    memcpy(c->mv, inner, sizeof inner);
    float var12 = 0.016666668f * var10;
    rs_gl_translate(c->mv, 0.0f, 0.5f * var10, 0.07f * var10);
    rs_gl_scale(c->mv, var12, -var12, var12);
    float n[3] = {0.0f, 0.0f, -1.0f * var12};
    c->depth_mask = 0;
    if (!font_loaded)
    {
        const struct te_tex *a = tex(c->assets, TT_ASCII);
        if (a) { font_metrics_load(&font, a->px, a->w, a->h); font_loaded = 1; }
    }
    if (font_loaded)
        for (int i = 0; i < 4; ++i)
        {
            char line[TE_SIGN_LINE + 8];
            if (i == e->edit) snprintf(line, sizeof line, "> %s <", e->text[i]);
            else snprintf(line, sizeof line, "%s", e->text[i]);
            int w = font_string_width(&font, line);
            draw_string(c, line, -w / 2, i * 10 - 4 * 5, n);
        }
    c->depth_mask = 1;
    c->color[0] = c->color[1] = c->color[2] = c->color[3] = 1.0f;
    memcpy(c->mv, saved, sizeof saved);
}

/* ------------------------------------------------------------ skulls */

static void render_skull(struct te_ctx *c, const struct te_render_item *e, const float d[3])
{
    int type = e->skull_type;
    int ti = type == 1 ? TT_WITHER : type == 2 ? TT_ZOMBIE : type == 3 ? TT_STEVE :
             type == 4 ? TT_CREEPER : TT_SKELETON;
    const struct te_tex *t = tex(c->assets, ti);
    int facing = e->meta & 7;
    float rot = (float)(e->skull_rot * 360) / 16.0f;
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    c->cull = 0;
    if (facing != 1)
    {
        switch (facing)
        {
        case 2:
            rs_gl_translate(c->mv, d[0] + 0.5f, d[1] + 0.25f, d[2] + 0.74f);
            break;
        case 3:
            rs_gl_translate(c->mv, d[0] + 0.5f, d[1] + 0.25f, d[2] + 0.26f);
            rot = 180.0f;
            break;
        case 4:
            rs_gl_translate(c->mv, d[0] + 0.74f, d[1] + 0.25f, d[2] + 0.5f);
            rot = 270.0f;
            break;
        default:
            rs_gl_translate(c->mv, d[0] + 0.26f, d[1] + 0.25f, d[2] + 0.5f);
            rot = 90.0f;
        }
    }
    else
        rs_gl_translate(c->mv, d[0] + 0.5f, d[1], d[2] + 0.5f);
    c->rescale = 1;
    rs_gl_scale(c->mv, -1.0f, -1.0f, 1.0f);
    c->color[0] = c->color[1] = c->color[2] = c->color[3] = 1.0f;
    /* ModelSkeletonHead.setRotationAngles: the yaw through radians and back */
    float ay = rot / (180.0f / PI_F);
    float ax = 0.0f / (180.0f / PI_F);
    struct te_part head = {0, 0, 0, ax, ay, 0, -4, -8, -4, 8, 8, 8, 0, 0, 64, type == 2 ? 64 : 32};
    render_part(c, &head, t);
    memcpy(c->mv, saved, sizeof saved);
}

/* ------------------------------------------------------------ spawner */

static void render_spawner(struct te_ctx *c, const struct te_render_item *e, const float d[3],
                           const struct te_render_frame *f)
{
    if (e->mob < 0) return;
    /* TileEntityMobSpawnerRenderer's modelview, camera-relative, for the mob
     * renderer's own vertex path */
    float o[16];
    rs_gl_identity(o);
    rs_gl_translate(o, d[0] + 0.5f, d[1], d[2] + 0.5f);
    rs_gl_translate(o, 0.0f, 0.4f, 0.0f);
    rs_gl_rotate(o, (float)(e->prev_rot + (e->rot - e->prev_rot) * (double)f->pt) * 10.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(o, -30.0f, 1.0f, 0.0f, 0.0f);
    rs_gl_translate(o, 0.0f, -0.4f, 0.0f);
    rs_gl_scale(o, 0.4375f, 0.4375f, 0.4375f);
    struct mob_render_input m;
    memset(&m, 0, sizeof m);
    m.kind = (enum mob_model_kind)e->mob;
    m.partial_tick = f->pt;
    m.body_yaw = e->mob_body_yaw;
    m.head_yaw = e->mob_head_yaw;
    m.pitch = e->mob_pitch;
    m.age = e->mob_age;
    m.brightness = e->brf;
    m.height = 1.8f;
    m.width = 0.6f;
    m.outer_set = 1;
    memcpy(m.outer, o, sizeof o);
    raster_mobs_draw(c->assets, &c->target, &m, 1, NULL, NULL, 0, NULL);
    /* RendererLivingEntity.doRender leaves culling on and the rescale off */
    c->cull = 1;
    c->rescale = 0;
}

/* ------------------------------------------------------------ end portal */

struct pv
{
    float clip[4];
    float x, y, sx, sy, w, depth;
    float s, t, q;   /* the texture coordinates after the texture matrix */
    float fogcoord;
};

struct portal_pass
{
    const struct te_ctx *c;
    const struct te_tex *t;
    float color[4];
    float light[3];
    int additive;
};

static unsigned char byte01(float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return (unsigned char)lrintf(x * 255.0f);
}

static float pedge(const struct pv *a, const struct pv *b, float x, float y)
{
    return (x - a->x) * (b->y - a->y) - (y - a->y) * (b->x - a->x);
}

static double pedge_exact(const struct pv *a, const struct pv *b, double x, double y)
{
    return (x - a->x) * ((double)b->y - a->y) - (y - a->y) * ((double)b->x - a->x);
}

static int ptop_left(const struct pv *a, const struct pv *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    return dy < 0 || (dy == 0 && dx > 0);
}

static void portal_triangle(const struct portal_pass *pp, const struct pv in[3])
{
    if (raster_rec_on()) {
        const struct entity_raster_target *rt = &pp->c->target;
        struct raster_obs_portal rp;
        memset(&rp, 0, sizeof rp);
        for (int i = 0; i < 3; ++i) {
            const struct pv *p = &in[i];
            const float f[14] = {p->clip[0], p->clip[1], p->clip[2], p->clip[3], p->x, p->y, p->sx, p->sy,
                                 p->w, p->depth, p->s, p->t, p->q, p->fogcoord};
            memcpy(rp.v[i], f, sizeof f);
        }
        for (int c = 0; c < 4; ++c) rp.color[c] = pp->color[c];
        for (int c = 0; c < 3; ++c) { rp.light[c] = pp->light[c]; rp.fog[c] = rt->fog[c]; }
        rp.fogs = rt->fogs; rp.foge = rt->foge; rp.fogd = rt->fogd; rp.fogm = rt->fogm;
        rp.additive = pp->additive;
        rp.cull = pp->c->cull;
        raster_rec_portal(&rp, pp->t->px, pp->t->w, pp->t->h);
    }
    if (raster_rec_only()) return;
    const struct entity_raster_target *t = &pp->c->target;
    struct pv v[3] = {in[0], in[1], in[2]};
    float area = pedge(&v[0], &v[1], v[2].x, v[2].y);
    if (area == 0.0f) return;
    if (area < 0.0f)
    {
        if (pp->c->cull) return;
        struct pv tmp = v[1];
        v[1] = v[2];
        v[2] = tmp;
        area = -area;
    }
    struct pv s[3] = {v[0], v[1], v[2]};
    for (int i = 0; i < 3; ++i) { s[i].x = v[i].sx; s[i].y = v[i].sy; }
    float minx = fminf(s[0].x, fminf(s[1].x, s[2].x)), maxx = fmaxf(s[0].x, fmaxf(s[1].x, s[2].x));
    float miny = fminf(s[0].y, fminf(s[1].y, s[2].y)), maxy = fmaxf(s[0].y, fmaxf(s[1].y, s[2].y));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(t->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(t->h - 1, ceilf(maxy));
    if (t->triangles) ++*t->triangles;
    const struct te_tex *tx = pp->t;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            float px = x + 0.5f, py = y + 0.5f;
            double e0, e1, e2;
            if (raster_entity_prec() & RP_EDGE) {
                /* raster_prec.h: the edge test in float */
                e0 = pedge(&s[1], &s[2], px, py);
                e1 = pedge(&s[2], &s[0], px, py);
                e2 = pedge(&s[0], &s[1], px, py);
            } else {
                e0 = pedge_exact(&s[1], &s[2], px, py);
                e1 = pedge_exact(&s[2], &s[0], px, py);
                e2 = pedge_exact(&s[0], &s[1], px, py);
            }
            if (e0 < 0 || (e0 == 0 && !ptop_left(&s[1], &s[2]))) continue;
            if (e1 < 0 || (e1 == 0 && !ptop_left(&s[2], &s[0]))) continue;
            if (e2 < 0 || (e2 == 0 && !ptop_left(&s[0], &s[1]))) continue;
            float b[3] = {pedge(&v[1], &v[2], px, py) / area, pedge(&v[2], &v[0], px, py) / area,
                          pedge(&v[0], &v[1], px, py) / area};
            float iw = b[0] / v[0].w + b[1] / v[1].w + b[2] / v[2].w;
            float z = (b[0] / v[0].w * v[0].depth + b[1] / v[1].w * v[1].depth +
                       b[2] / v[2].w * v[2].depth) / iw;
            int k = y * t->w + x;
            if (z < 0.0f || z > t->depth[k]) continue; /* GL_LEQUAL */
            float ps = (b[0] / v[0].w * v[0].s + b[1] / v[1].w * v[1].s + b[2] / v[2].w * v[2].s) / iw;
            float pt = (b[0] / v[0].w * v[0].t + b[1] / v[1].w * v[1].t + b[2] / v[2].w * v[2].t) / iw;
            float pq = (b[0] / v[0].w * v[0].q + b[1] / v[1].w * v[1].q + b[2] / v[2].w * v[2].q) / iw;
            float u = ps / pq, vv = pt / pq;
            int tu = (int)floorf(u * tx->w), tv = (int)floorf(vv * tx->h);
            tu %= tx->w; tv %= tx->h;
            if (tu < 0) tu += tx->w;
            if (tv < 0) tv += tx->h;
            const unsigned char *texel = tx->px + ((size_t)tv * tx->w + tu) * 4;
            float alpha = pp->color[3] * (texel[3] / 255.0f);
            if (!(alpha > 0.1f)) continue; /* GL_ALPHA_TEST GREATER 0.1 */
            float dist = (b[0] / v[0].w * v[0].fogcoord + b[1] / v[1].w * v[1].fogcoord +
                          b[2] / v[2].w * v[2].fogcoord) / iw;
            float fog = 1.0f;
            if (t->fogm == 9729) fog = (t->foge - dist) / (t->foge - t->fogs);
            else if (t->fogm == 2048) fog = expf(-t->fogd * dist);
            else if (t->fogm == 2049) { float dd = t->fogd * dist; fog = expf(-dd * dd); }
            fog = fog < 0.0f ? 0.0f : fog > 1.0f ? 1.0f : fog;
            for (int ch = 0; ch < 3; ++ch)
            {
                float col = pp->color[ch] * (texel[ch] / 255.0f) * pp->light[ch];
                col = col * fog + t->fog[ch] * (1.0f - fog);
                int src = byte01(col);
                int dst = t->rgb[k * 3 + ch];
                int out;
                if (pp->additive)
                    out = src + dst > 255 ? 255 : src + dst;       /* GL_ONE, GL_ONE */
                else
                {
                    /* GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA in 8-bit products */
                    int a8 = byte01(alpha);
                    int p0 = src * a8, p1 = dst * (255 - a8);
                    out = ((p0 + 128 + ((p0 + 128) >> 8)) >> 8) + ((p1 + 128 + ((p1 + 128) >> 8)) >> 8);
                    if (out > 255) out = 255;
                }
                t->rgb[k * 3 + ch] = (unsigned char)out;
            }
            t->depth[k] = z;
            if (t->samples) ++*t->samples;
        }
}

static struct pv pv_lerp(const struct pv *a, const struct pv *b, float u)
{
    struct pv v = *a;
    for (int i = 0; i < 4; ++i) v.clip[i] = a->clip[i] + u * (b->clip[i] - a->clip[i]);
    v.s = a->s + u * (b->s - a->s);
    v.t = a->t + u * (b->t - a->t);
    v.q = a->q + u * (b->q - a->q);
    v.fogcoord = a->fogcoord + u * (b->fogcoord - a->fogcoord);
    return v;
}

static void portal_clip(const struct portal_pass *pp, const struct pv in[3])
{
    struct pv a[16], b[16];
    memcpy(a, in, 3 * sizeof *a);
    int n = 3;
    for (int plane = 0; plane < 6; ++plane)
    {
        int m = 0, axis = plane / 2;
        float sign = plane & 1 ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const struct pv *p = &a[i], *q = &a[(i + 1) % n];
            float dp = p->clip[3] - sign * p->clip[axis];
            float dq = q->clip[3] - sign * q->clip[axis];
            if (dp >= 0) b[m++] = *p;
            if ((dp >= 0) != (dq >= 0)) b[m++] = pv_lerp(p, q, dp / (dp - dq));
        }
        n = m;
        if (n < 3) return;
        memcpy(a, b, (size_t)n * sizeof *a);
    }
    int w = pp->c->target.w, h = pp->c->target.h;
    for (int i = 0; i < n; ++i)
    {
        struct pv *v = &a[i];
        v->w = v->clip[3];
        v->x = (v->clip[0] / v->w * 0.5f + 0.5f) * w;
        v->y = (0.5f - v->clip[1] / v->w * 0.5f) * h;
        v->sx = rintf((v->x - 0.5f) * 256.0f) / 256.0f + 0.5f;
        v->sy = rintf((v->y - 0.5f) * 256.0f) / 256.0f + 0.5f;
        v->depth = v->clip[2] / v->w * 0.5f + 0.5f;
    }
    for (int i = 1; i + 1 < n; ++i)
    {
        struct pv tri[3] = {a[0], a[i], a[i + 1]};
        portal_triangle(pp, tri);
    }
}

/* The inverse of an angle-preserving modelview's row 1 (Mesa's
 * invert_matrix_3d for a rotation and translation: the transposed 3x3 and the
 * translation through it): the eye plane glTexGen(GL_Q, GL_EYE_PLANE,
 * (0, 1, 0, 0)) stores. */
static void eye_plane_y(const float *m, float out[4])
{
    /* MAT(m, r, c) = m[c * 4 + r]; inv(1, c) = MAT(m, c, 1) = m[4 + c] for c < 3 */
    out[0] = m[4];
    out[1] = m[5];
    out[2] = m[6];
    out[3] = -(m[12] * out[0] + m[13] * out[1] + m[14] * out[2]);
}

static void render_end_portal(struct te_ctx *c, const struct te_render_item *e, const double rel[3],
                              const struct te_render_frame *f)
{
    const struct te_tex *sky = tex(c->assets, TT_END_SKY), *stars = tex(c->assets, TT_END_PORTAL);
    if (!sky || !stars) return;
    float var9 = (float)f->view[0], var10 = (float)f->view[1], var11 = (float)f->view[2];
    jrand rnd;
    jr_seed(&rnd, 31100LL);
    float var12 = 0.75f;
    /* the lightmap at the tile's brightness, sampled once: every vertex has it */
    float lb = (float)(e->brf % 65536), ls = (float)(e->brf / 65536);
    float light[3];
    {
        const uint32_t *lm = c->target.lm;
        float x = fmaxf(0, fminf(15, lb / 16.0f)), y = fmaxf(0, fminf(15, ls / 16.0f));
        int x0 = (int)x, y0 = (int)y;
        int x1 = x0 < 15 ? x0 + 1 : x0, y1 = y0 < 15 ? y0 + 1 : y0;
        float fx = x - x0, fy = y - y0;
        uint32_t cc[4] = {lm[y0 * 16 + x0], lm[y0 * 16 + x1], lm[y1 * 16 + x0], lm[y1 * 16 + x1]};
        for (int i = 0; i < 3; ++i)
        {
            int sh = 8 * (2 - i);
            float a = ((cc[0] >> sh) & 255) * (1 - fx) + ((cc[1] >> sh) & 255) * fx;
            float b = ((cc[2] >> sh) & 255) * (1 - fx) + ((cc[3] >> sh) & 255) * fx;
            light[i] = (a * (1 - fy) + b * fy) / 255.0f;
        }
    }
    /* the quad's object coordinates, as the Tessellator stores them */
    float qx0 = (float)rel[0], qx1 = (float)(rel[0] + 1.0), qy = (float)(rel[1] + (double)var12);
    float qz0 = (float)rel[2], qz1 = (float)(rel[2] + 1.0);
    float obj[4][3] = {{qx0, qy, qz0}, {qx0, qy, qz1}, {qx1, qy, qz1}, {qx1, qy, qz0}};
    for (int layer = 0; layer < 16; ++layer)
    {
        float var14 = (float)(16 - layer);
        float var15 = 0.0625f;
        float var16 = 1.0f / (var14 + 1.0f);
        const struct te_tex *t = stars;
        int additive = 1;
        if (layer == 0)
        {
            t = sky;
            var16 = 0.1f;
            var14 = 65.0f;
            var15 = 0.125f;
            additive = 0;
        }
        if (layer == 1) var15 = 0.5f;
        float var17 = (float)(-(rel[1] + (double)var12));
        float var18 = var17 + f->object[1];
        float var19 = var17 + var14 + f->object[1];
        float var20 = var18 / var19;
        var20 += (float)(rel[1] + (double)var12);
        /* the eye plane under the pushed translate */
        float spec[16];
        memcpy(spec, c->cam, sizeof spec);
        rs_gl_translate(spec, var9, var20, var11);
        float plane[4];
        eye_plane_y(spec, plane);
        /* the texture matrix */
        float tm[16];
        rs_gl_identity(tm);
        rs_gl_translate(tm, 0.0f, (float)(f->ms % 700000LL) / 700000.0f, 0.0f);
        rs_gl_scale(tm, var15, var15, var15);
        rs_gl_translate(tm, 0.5f, 0.5f, 0.0f);
        rs_gl_rotate(tm, (float)(layer * layer * 4321 + layer * 9) * 2.0f, 0.0f, 0.0f, 1.0f);
        rs_gl_translate(tm, -0.5f, -0.5f, 0.0f);
        rs_gl_translate(tm, -var9, -var11, -var10);
        var18 = var17 + f->object[1];
        rs_gl_translate(tm, f->object[0] * var14 / var18, f->object[2] * var14 / var18, -var10);
        float r = jr_float(&rnd) * 0.5f + 0.1f;
        float g = jr_float(&rnd) * 0.5f + 0.4f;
        float b = jr_float(&rnd) * 0.5f + 0.5f;
        if (layer == 0) r = g = b = 1.0f;
        /* Tessellator.setColorRGBA_F packs each channel to a byte */
        struct portal_pass pp = {c, t, {(float)(int)(r * var16 * 255.0f) / 255.0f,
                                        (float)(int)(g * var16 * 255.0f) / 255.0f,
                                        (float)(int)(b * var16 * 255.0f) / 255.0f, 1.0f},
                                 {light[0], light[1], light[2]}, additive};
        struct pv q[4];
        for (int k = 0; k < 4; ++k)
        {
            const float *p = obj[k];
            float eye[4];
            for (int rr = 0; rr < 4; ++rr)
                eye[rr] = c->cam[rr] * p[0] + c->cam[4 + rr] * p[1] + c->cam[8 + rr] * p[2] + c->cam[12 + rr];
            const float *proj = c->target.proj;
            for (int rr = 0; rr < 4; ++rr)
                q[k].clip[rr] = proj[rr] * eye[0] + proj[4 + rr] * eye[1] + proj[8 + rr] * eye[2] + proj[12 + rr] * eye[3];
            q[k].fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
            /* texgen: s = x, t = z, r = 1 in object space; q the eye plane */
            float s0 = p[0], t0 = p[2], r0 = 1.0f;
            float q0 = plane[0] * eye[0] + plane[1] * eye[1] + plane[2] * eye[2] + plane[3] * eye[3];
            float v4[4] = {s0, t0, r0, q0}, o4[4];
            for (int rr = 0; rr < 4; ++rr)
                o4[rr] = tm[rr] * v4[0] + tm[4 + rr] * v4[1] + tm[8 + rr] * v4[2] + tm[12 + rr] * v4[3];
            q[k].s = o4[0];
            q[k].t = o4[1];
            q[k].q = o4[3];
        }
        struct pv t0[3] = {q[0], q[1], q[3]}, t1[3] = {q[1], q[2], q[3]};
        portal_clip(&pp, t0);
        portal_clip(&pp, t1);
    }
}

/* ------------------------------------------------------------ dispatcher */

int raster_tileents_draw(const char *assets, const struct entity_raster_target *t,
                         const struct te_render_frame *f, const struct te_render_item *list, int n)
{
    struct te_ctx c;
    memset(&c, 0, sizeof c);
    c.target = *t;
    /* window z is affine in screen space, as GL interpolates it */
    c.target.linear_depth = 1;
    c.assets = assets;
    c.cam = t->mv;
    c.cull = 1;
    c.rescale = 0;
    c.normalize = f->normalize;
    c.depth_mask = 1;
    set_lights(&c);
    int drawn = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct te_render_item *e = &list[i];
        /* func_147544_a: TileEntity.getDistanceFrom under the max render
         * distance squared (64 blocks for every kind drawn here) */
        double dx = (double)e->x + 0.5 - f->view[0];
        double dy = (double)e->y + 0.5 - f->view[1];
        double dz = (double)e->z + 0.5 - f->view[2];
        if (!(dx * dx + dy * dy + dz * dz < 4096.0)) continue;
        if (e->kind == TE_RENDER_NONE) continue;
        c.brf = e->brf;
        c.color[0] = c.color[1] = c.color[2] = c.color[3] = 1.0f;
        memcpy(c.mv, c.cam, sizeof c.mv);
        double rel[3] = {(double)e->x - f->view[0], (double)e->y - f->view[1], (double)e->z - f->view[2]};
        float d[3] = {(float)rel[0], (float)rel[1], (float)rel[2]};
        switch (e->kind)
        {
        case TE_RENDER_CHEST:
        case TE_RENDER_ENDER_CHEST: render_chest(&c, e, d, f->pt); break;
        case TE_RENDER_SIGN: render_sign(&c, e, d); break;
        case TE_RENDER_SKULL: render_skull(&c, e, d); break;
        case TE_RENDER_SPAWNER:
        {
            /* the mob renderer reads the lightmap coordinates off the item */
            render_spawner(&c, e, d, f);
            break;
        }
        case TE_RENDER_END_PORTAL: render_end_portal(&c, e, rel, f); break;
        case TE_RENDER_ENCHANT_TABLE: render_enchant_table(&c, e, d, f->pt); break;
        default: break;
        }
        ++drawn;
    }
    return drawn;
}

int raster_tileents_mob_kind(const char *name)
{
    static const struct { const char *name; int kind; } names[] = {
        {"Pig", MOB_PIG}, {"Cow", MOB_COW}, {"Sheep", MOB_SHEEP}, {"Chicken", MOB_CHICKEN},
        {"Zombie", MOB_ZOMBIE}, {"Skeleton", MOB_SKELETON}, {"Creeper", MOB_CREEPER},
        {"Spider", MOB_SPIDER}, {"CaveSpider", MOB_CAVE_SPIDER}, {"Enderman", MOB_ENDERMAN},
        {"Witch", MOB_WITCH}, {"Slime", MOB_SLIME}, {"Silverfish", MOB_SILVERFISH},
        {"PigZombie", MOB_PIGMAN}, {"Ghast", MOB_GHAST}, {"Blaze", MOB_BLAZE},
        {"LavaSlime", MOB_MAGMA_CUBE}, {"Villager", MOB_VILLAGER}, {"VillagerGolem", MOB_IRON_GOLEM},
        {"Squid", MOB_SQUID}, {"Bat", MOB_BAT}, {"MushroomCow", MOB_MOOSHROOM},
    };
    for (size_t i = 0; name && i < sizeof names / sizeof names[0]; ++i)
        if (!strcmp(name, names[i].name)) return names[i].kind;
    return -1;
}

/* ------------------------------------------------------------ recorded frames */

static float getf(const struct jval *v)
{
    uint32_t bits = 0;
    json_float(v, &bits);
    float out;
    memcpy(&out, &bits, 4);
    return out;
}

static double getd(const struct jval *v)
{
    uint64_t bits = 0;
    json_double(v, &bits);
    double out;
    memcpy(&out, &bits, 8);
    return out;
}

static long long getl(const struct jval *v)
{
    int64_t n = 0;
    json_int(v, &n);
    return (long long)n;
}

static char *lastline(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0)
    {
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    return last;
}

void raster_tileents_scene(const char *scene, const struct entity_raster_target *t)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    char *line = lastline(path);
    if (!line) return;
    struct jval *root = json_parse(line);
    if (!root) { free(line); return; }
    const struct jval *tes = json_get(root, "tes");
    const struct jval *list = json_get(tes, "list");
    int n = json_len(list);
    if (!tes || n <= 0) { json_free(root); return; }
    struct te_render_frame f;
    memset(&f, 0, sizeof f);
    const struct jval *p = json_get(tes, "p"), *ari = json_get(tes, "ari");
    for (int i = 0; i < 3; ++i)
    {
        f.view[i] = getd(json_at(p, i));
        f.object[i] = getf(json_at(ari, i));
    }
    f.pt = getf(json_get(root, "pt"));
    f.ms = getl(json_get(tes, "ms"));
    struct te_render_item *items = calloc((size_t)n, sizeof *items);
    if (!items) abort();
    for (int i = 0; i < n; ++i)
    {
        const struct jval *v = json_at(list, i);
        struct te_render_item *e = &items[i];
        const char *k = json_str(json_get(v, "k"));
        e->kind = !k ? TE_RENDER_NONE :
                  !strcmp(k, "TileEntityChest") ? TE_RENDER_CHEST :
                  !strcmp(k, "TileEntityEnderChest") ? TE_RENDER_ENDER_CHEST :
                  !strcmp(k, "TileEntitySign") ? TE_RENDER_SIGN :
                  !strcmp(k, "TileEntitySkull") ? TE_RENDER_SKULL :
                  !strcmp(k, "TileEntityMobSpawner") ? TE_RENDER_SPAWNER :
                  !strcmp(k, "TileEntityEndPortal") ? TE_RENDER_END_PORTAL : TE_RENDER_NONE;
        e->x = (int)getl(json_get(v, "x"));
        e->y = (int)getl(json_get(v, "y"));
        e->z = (int)getl(json_get(v, "z"));
        e->block = (int)getl(json_get(v, "id"));
        e->meta = (int)getl(json_get(v, "meta"));
        e->brf = (int)getl(json_get(v, "brf"));
        e->lid = getf(json_get(v, "lid"));
        e->prev_lid = getf(json_get(v, "plid"));
        const struct jval *adj = json_get(v, "adj");
        for (int j = 0; j < 4 && j < json_len(adj); ++j) e->adj[j] = (unsigned char)getl(json_at(adj, j));
        const struct jval *text = json_get(v, "text");
        for (int j = 0; j < 4 && j < json_len(text); ++j)
        {
            const char *s = json_str(json_at(text, j));
            snprintf(e->text[j], sizeof e->text[j], "%s", s ? s : "");
        }
        e->edit = json_get(v, "edit") ? (int)getl(json_get(v, "edit")) : -1;
        e->skull_type = (int)getl(json_get(v, "type"));
        e->skull_rot = (int)getl(json_get(v, "rot"));
        if (e->kind == TE_RENDER_SPAWNER)
        {
            e->rot = getd(json_get(v, "rot"));
            e->prev_rot = getd(json_get(v, "prot"));
            e->mob = raster_tileents_mob_kind(json_str(json_get(v, "mob")));
            e->mob_body_yaw = getf(json_get(v, "byaw"));
            e->mob_head_yaw = getf(json_get(v, "hyaw"));
            e->mob_pitch = getf(json_get(v, "pitch"));
            e->mob_age = (int)getl(json_get(v, "age"));
        }
    }
    raster_tileents_draw(scene, t, &f, items, n);
    free(items);
    json_free(root);
}

/* ------------------------------------------------------------ client ticks */

int raster_tileents_lid_tick(float *lid, float *prev_lid, int players_using, int lead)
{
    int draws = 0;
    *prev_lid = *lid;
    if (players_using > 0 && *lid == 0.0f && lead) ++draws; /* random.chestopen */
    if ((players_using == 0 && *lid > 0.0f) || (players_using > 0 && *lid < 1.0f))
    {
        float before = *lid;
        if (players_using > 0) *lid += 0.1f;
        else *lid -= 0.1f;
        if (*lid > 1.0f) *lid = 1.0f;
        if (*lid < 0.5f && before >= 0.5f && lead) ++draws; /* random.chestclosed */
        if (*lid < 0.0f) *lid = 0.0f;
    }
    return draws;
}

void raster_tileents_spawner_tick(double *rot, double *prev_rot, int *delay)
{
    if (*delay > 0) --*delay;
    *prev_rot = *rot;
    *rot = fmod(*rot + (double)(1000.0f / ((float)*delay + 200.0f)), 360.0);
}

void raster_tileent_portal_triangle(const struct entity_raster_target *t, const struct raster_obs_portal *p,
                                    const unsigned char *tex, int tex_w, int tex_h)
{
    struct te_ctx c;
    memset(&c, 0, sizeof c);
    c.target = *t;
    c.target.fog = p->fog;
    c.target.fogs = p->fogs; c.target.foge = p->foge; c.target.fogd = p->fogd; c.target.fogm = p->fogm;
    c.cull = p->cull;
    struct te_tex tx = {(unsigned char *)tex, tex_w, tex_h};
    struct portal_pass pp = {&c, &tx, {p->color[0], p->color[1], p->color[2], p->color[3]},
                             {p->light[0], p->light[1], p->light[2]}, p->additive};
    struct pv v[3];
    for (int i = 0; i < 3; ++i) {
        const float *f = p->v[i];
        v[i] = (struct pv){{f[0], f[1], f[2], f[3]}, f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13]};
    }
    portal_triangle(&pp, v);
}
