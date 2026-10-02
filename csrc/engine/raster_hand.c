#define _POSIX_C_SOURCE 200809L
/* EntityRenderer.renderHand and ItemRenderer.renderItemInFirstPerson: the bare
 * arm (RenderPlayer.renderFirstPersonArm's ModelBiped right arm), flat items
 * (renderItemIn2D's extruded sprite), held blocks (renderBlockAsItem through
 * block_item_model), the swing, the equip animation, eating and drinking, the
 * bow pull and the sword block. The modelview is built with the GL11 calls in
 * vanilla order through Mesa's own matrix arithmetic; the lighting is
 * RenderHelper.enableStandardItemLighting's two lights in eye space, evaluated
 * the way Mesa's fixed-function vertex program does it; the fragments go
 * through the entity rasterizer with the lightmap and no fog, over a depth
 * buffer cleared for this pass. */
#include "raster_hand.h"
#include "itemtab.h"
#include "texanim.h"
#include "block_item_model.h"
#include "blocks.h"
#include "jmath.h"
#include "raster_entity_quad.h"
#include "raster_obs.h"
#include "render_blocks.h"
#include "renderstate.h"
#include "tape.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.1415927f /* (float)Math.PI */

struct hand_ctx
{
    const struct raster_hand_in *in;
    float mv[16];
    float light[2][3]; /* the two light directions in eye space, normalized */
    float color[3];    /* glColor, the lit material */
    struct entity_raster_target target;
    int tex_matrix;    /* a GL_TEXTURE matrix is set: tm, column-major */
    float tm[16];
    int unlit;         /* GL_LIGHTING off: the material alone */
};

/* ------------------------------------------------------------ lighting */

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

/* RenderHelper.enableStandardItemLighting under the current modelview: each
 * light's GL_POSITION (w = 0) goes to eye space, and Mesa normalizes the
 * direction of an infinite light. The vectors are Vec3.normalize of the two
 * constants, whose length is MathHelper.sqrt_double (a float). */
static void set_lights(struct hand_ctx *c)
{
    static const double src[2][3] = {{0.20000000298023224, 1.0, -0.699999988079071},
                                     {-0.20000000298023224, 1.0, 0.699999988079071}};
    for (int i = 0; i < 2; ++i)
    {
        double len = (double)(float)sqrt(src[i][0] * src[i][0] + src[i][1] * src[i][1] +
                                         src[i][2] * src[i][2]);
        float l[3] = {(float)(src[i][0] / len), (float)(src[i][1] / len), (float)(src[i][2] / len)};
        mat3_apply(c->mv, l, c->light[i]);
        normalize3(c->light[i]);
    }
}

/* The eye-space normal under GL_RESCALE_NORMAL: the inverse transpose of the
 * modelview's upper 3x3, scaled by the reciprocal length of the inverse's third
 * row (Mesa's _ModelViewInvScaleEyespace). */
static void eye_normal(const float *m, const float n[3], float out[3])
{
    double a = m[0], b = m[4], cc = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], k = m[10];
    double det = a * (e * k - f * h) - b * (d * k - f * g) + cc * (d * h - e * g);
    if (det == 0.0) { out[0] = n[0]; out[1] = n[1]; out[2] = n[2]; return; }
    /* inv (row-major r,c) */
    float inv[3][3] = {
        {(float)((e * k - f * h) / det), (float)((cc * h - b * k) / det), (float)((b * f - cc * e) / det)},
        {(float)((f * g - d * k) / det), (float)((a * k - cc * g) / det), (float)((cc * d - a * f) / det)},
        {(float)((d * h - e * g) / det), (float)((b * g - a * h) / det), (float)((a * e - b * d) / det)}};
    /* n_eye = n * inv (the transpose applied as a row vector) */
    for (int j = 0; j < 3; ++j) out[j] = n[0] * inv[0][j] + n[1] * inv[1][j] + n[2] * inv[2][j];
    float s = inv[2][0] * inv[2][0] + inv[2][1] * inv[2][1] + inv[2][2] * inv[2][2];
    if (s < 1e-12f) s = 1.0f;
    s = 1.0f / sqrtf(s);
    for (int j = 0; j < 3; ++j) out[j] *= s;
}

/* The lit colour's scalar factor for a white material: the scene ambient 0.4
 * plus each light's diffuse 0.6 times max(0, N.L). The material (glColor)
 * multiplies it in the rasterizer before the clamp. */
static float lit(const struct hand_ctx *c, const float n[3])
{
    /* Tessellator.setNormal packs each component as a signed byte (x * 127) */
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = (float)(signed char)(int)(n[i] * 127.0f) / 127.0f;
    float e[3];
    eye_normal(c->mv, q, e);
    float d = 0.4f;
    for (int i = 0; i < 2; ++i)
    {
        float dot = e[0] * c->light[i][0] + e[1] * c->light[i][1] + e[2] * c->light[i][2];
        if (dot > 0.0f) d += 0.6f * dot;
    }
    return d;
}

/* ------------------------------------------------------------ drawing */

static struct entity_clip_vertex vertex(const struct hand_ctx *c, const float p[3], float u, float v,
                                        float diffuse)
{
    struct entity_clip_vertex out;
    memset(&out, 0, sizeof out);
    float eye[4], w[4] = {p[0], p[1], p[2], 1.0f};
    for (int r = 0; r < 4; ++r)
        eye[r] = c->mv[r] * w[0] + c->mv[4 + r] * w[1] + c->mv[8 + r] * w[2] + c->mv[12 + r] * w[3];
    for (int r = 0; r < 4; ++r)
        out.clip[r] = c->in->proj[r] * eye[0] + c->in->proj[4 + r] * eye[1] +
                      c->in->proj[8 + r] * eye[2] + c->in->proj[12 + r] * eye[3];
    if (c->tex_matrix)
    {
        float tu = c->tm[0] * u + c->tm[4] * v + c->tm[12];
        float tv = c->tm[1] * u + c->tm[5] * v + c->tm[13];
        u = tu;
        v = tv;
    }
    out.u = u;
    out.v = v;
    out.diffuse = diffuse;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1.0f;
    out.light[0] = (float)(c->in->light % 65536);
    out.light[1] = (float)(c->in->light / 65536);
    return out;
}

/* One Tessellator quad with a normal, in the current modelview and glColor. */
static void quad(struct hand_ctx *c, const float p[4][3], const float uv[4][2], const float n[3],
                 const unsigned char *tex, int tw, int th)
{
    if (!tex) return;
    float d = c->unlit ? 1.0f : lit(c, n);
    struct entity_clip_vertex q[4];
    for (int k = 0; k < 4; ++k) q[k] = vertex(c, p[k], uv[k][0], uv[k][1], d);
    struct entity_raster_target t = c->target;
    for (int i = 0; i < 3; ++i) t.material[i] = c->color[i];
    raster_entity_quad(&t, tex, tw, th, q);
}

static void set_color(struct hand_ctx *c, int rgb)
{
    c->color[0] = (float)(rgb >> 16 & 255) / 255.0f;
    c->color[1] = (float)(rgb >> 8 & 255) / 255.0f;
    c->color[2] = (float)(rgb & 255) / 255.0f;
}

/* ModelBox.render as a compiled display list: six TexturedQuads, the normal of
 * each from its first three vertices. */
static void model_box(struct hand_ctx *c, float x, float y, float z, int dx, int dy, int dz, int u,
                      int v, float scale, const unsigned char *tex, int tw, int th)
{
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
        quad(c, qp, quv, nf, tex, tw, th);
    }
}

/* RenderPlayer.renderFirstPersonArm: ModelBiped.setRotationAngles with every
 * argument 0 and onGround 0 leaves the right arm at its pivot (-5, 2, 0) with
 * only the idle sway's rotateAngleZ, cos(0) * 0.05 + 0.05. */
static void first_person_arm(struct hand_ctx *c)
{
    c->color[0] = c->color[1] = c->color[2] = 1.0f;
    float scale = 0.0625f;
    float body = mh_sin(sqrtf(0.0f) * PI_F * 2.0f) * 0.2f;
    float px = -mh_cos(body) * 5.0f, pz = mh_sin(body) * 5.0f, py = 2.0f;
    float az = 0.0f;
    float onground = 0.0f;
    az = mh_sin(onground * PI_F) * -0.4f;
    az += mh_cos(0.0f * 0.09f) * 0.05f + 0.05f;
    float ax = 0.0f;
    ax += mh_sin(0.0f * 0.067f) * 0.05f;
    rs_gl_translate(c->mv, 0.0f, 0.0f, 0.0f);
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    rs_gl_translate(c->mv, px * scale, py * scale, pz * scale);
    if (az != 0.0f) rs_gl_rotate(c->mv, az * (180.0f / PI_F), 0.0f, 0.0f, 1.0f);
    if (ax != 0.0f) rs_gl_rotate(c->mv, ax * (180.0f / PI_F), 1.0f, 0.0f, 0.0f);
    model_box(c, -3.0f, -2.0f, -2.0f, 4, 12, 4, 40, 16, scale, c->in->skin, 64, 32);
    memcpy(c->mv, saved, sizeof saved);
    rs_gl_translate(c->mv, -0.0f, -0.0f, -0.0f);
}

/* ItemRenderer.renderItemIn2D: the sprite's front and back faces and one
 * edge strip per texel column and row, THICK deep. */
static void item_in_2d(struct hand_ctx *c, float p1, float p2, float p3, float p4, int w, int h,
                       float thick, const unsigned char *tex, int tw, int th)
{
    float t = 0.0f - thick;
    {
        const float p[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const float uv[4][2] = {{p1, p4}, {p3, p4}, {p3, p2}, {p1, p2}};
        const float n[3] = {0, 0, 1};
        quad(c, p, uv, n, tex, tw, th);
    }
    {
        const float p[4][3] = {{0, 1, t}, {1, 1, t}, {1, 0, t}, {0, 0, t}};
        const float uv[4][2] = {{p1, p2}, {p3, p2}, {p3, p4}, {p1, p4}};
        const float n[3] = {0, 0, -1};
        quad(c, p, uv, n, tex, tw, th);
    }
    float v8 = 0.5f * (p1 - p3) / (float)w;
    float v9 = 0.5f * (p4 - p2) / (float)h;
    for (int i = 0; i < w; ++i)
    {
        float x = (float)i / (float)w;
        float u = p1 + (p3 - p1) * x - v8;
        const float p[4][3] = {{x, 0, t}, {x, 0, 0}, {x, 1, 0}, {x, 1, t}};
        const float uv[4][2] = {{u, p4}, {u, p4}, {u, p2}, {u, p2}};
        const float n[3] = {-1, 0, 0};
        quad(c, p, uv, n, tex, tw, th);
    }
    for (int i = 0; i < w; ++i)
    {
        float x = (float)i / (float)w;
        float u = p1 + (p3 - p1) * x - v8;
        float x1 = x + 1.0f / (float)w;
        const float p[4][3] = {{x1, 1, t}, {x1, 1, 0}, {x1, 0, 0}, {x1, 0, t}};
        const float uv[4][2] = {{u, p2}, {u, p2}, {u, p4}, {u, p4}};
        const float n[3] = {1, 0, 0};
        quad(c, p, uv, n, tex, tw, th);
    }
    for (int i = 0; i < h; ++i)
    {
        float y = (float)i / (float)h;
        float v = p4 + (p2 - p4) * y - v9;
        float y1 = y + 1.0f / (float)h;
        const float p[4][3] = {{0, y1, 0}, {1, y1, 0}, {1, y1, t}, {0, y1, t}};
        const float uv[4][2] = {{p1, v}, {p3, v}, {p3, v}, {p1, v}};
        const float n[3] = {0, 1, 0};
        quad(c, p, uv, n, tex, tw, th);
    }
    for (int i = 0; i < h; ++i)
    {
        float y = (float)i / (float)h;
        float v = p4 + (p2 - p4) * y - v9;
        const float p[4][3] = {{1, y, 0}, {0, y, 0}, {0, y, t}, {1, y, t}};
        const float uv[4][2] = {{p3, v}, {p1, v}, {p1, v}, {p3, v}};
        const float n[3] = {0, -1, 0};
        quad(c, p, uv, n, tex, tw, th);
    }
}

/* RenderBlocks.renderBlockAsItem over block_item_model's faces. The model's
 * faces sit at -0.5; vanilla's render types 0, 31, 39, 16 and 26 turn 90
 * degrees about Y and translate by -0.5 in GL, with the faces at 0..1. */
static void block_as_item(struct hand_ctx *c, const struct raster_hand_item *it)
{
    const struct raster_hand_in *in = c->in;
    struct block_item_model model;
    if (!in->scene || block_item_model_build(in->scene, it->id, it->meta, &model)) return;
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    int shifted = 0;
    if (model.inner_rotate_y90)
    {
        rs_gl_rotate(c->mv, 90.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_translate(c->mv, -0.5f, -0.5f, -0.5f);
        shifted = 1;
    }
    else if (!model.chest_texture)
    {
        rs_gl_translate(c->mv, -0.5f, -0.5f, -0.5f);
        shifted = 1;
    }
    const unsigned char *tex = model.chest_texture ? in->chest : in->blocks;
    int tw = model.chest_texture ? 64 : in->blocks_w, th = model.chest_texture ? 64 : in->blocks_h;
    int color = it->grass ? 0xffffff : it->render_color;
    for (int i = 0; i < model.n; ++i)
    {
        const struct block_item_quad *q = &model.quad[i];
        set_color(c, q->tint_top && it->grass ? it->render_color : color);
        float p[4][3];
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 3; ++j) p[k][j] = shifted ? q->p[k][j] + 0.5f : q->p[k][j];
        quad(c, p, (const float(*)[2])q->uv, q->normal, tex, tw, th);
    }
    memcpy(c->mv, saved, sizeof saved);
}

/* ItemRenderer.renderItem for the first-person path, one render pass. */
static void render_item(struct hand_ctx *c, const struct raster_hand_item *it, int pass)
{
    const struct raster_hand_in *in = c->in;
    if (it->block3d && pass == 0)
    {
        block_as_item(c, it);
        return;
    }
    if (!it->pass[pass].has_icon) return;
    float saved[16];
    memcpy(saved, c->mv, sizeof saved);
    const unsigned char *tex = it->sprite == 1 ? in->items : in->blocks;
    int tw = it->sprite == 1 ? in->items_w : in->blocks_w;
    int th = it->sprite == 1 ? in->items_h : in->blocks_h;
    rs_gl_translate(c->mv, -0.0f, -0.3f, 0.0f);
    rs_gl_scale(c->mv, 1.5f, 1.5f, 1.5f);
    rs_gl_rotate(c->mv, 50.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(c->mv, 335.0f, 0.0f, 0.0f, 1.0f);
    rs_gl_translate(c->mv, -0.9375f, -0.0625f, 0.0f);
    item_in_2d(c, it->pass[pass].max_u, it->pass[pass].min_v, it->pass[pass].min_u,
               it->pass[pass].max_v, it->pass[pass].icon_w, it->pass[pass].icon_h, 0.0625f, tex, tw,
               th);
    if (it->eff && pass == 0 && in->glint)
    {
        /* ItemRenderer.renderItem's glint: GL_EQUAL, lighting off, GL_SRC_COLOR,
         * GL_ONE, colour 0.76 * (0.5, 0.25, 0.8), two renderItemIn2D of the
         * whole texture under the scrolling texture matrices */
        struct hand_ctx g = *c;
        g.unlit = 1;
        g.target.depth_equal = 1;
        g.target.blend = 5;
        float v16 = 0.76f;
        g.color[0] = 0.5f * v16;
        g.color[1] = 0.25f * v16;
        g.color[2] = 0.8f * v16;
        for (int k = 0; k < 2; ++k)
        {
            rs_gl_identity(g.tm);
            rs_gl_scale(g.tm, 0.125f, 0.125f, 0.125f);
            float v18 = k == 0 ? (float)(in->now % 3000LL) / 3000.0f * 8.0f
                               : (float)(in->now % 4873LL) / 4873.0f * 8.0f;
            rs_gl_translate(g.tm, k == 0 ? v18 : -v18, 0.0f, 0.0f);
            rs_gl_rotate(g.tm, k == 0 ? -50.0f : 10.0f, 0.0f, 0.0f, 1.0f);
            g.tex_matrix = 1;
            item_in_2d(&g, 0.0f, 0.0f, 1.0f, 1.0f, 256, 256, 0.0625f, in->glint, 64, 64);
        }
        /* the glint leaves its glColor set */
        memcpy(c->color, g.color, sizeof c->color);
    }
    memcpy(c->mv, saved, sizeof saved);
}

static float swing_progress(const struct raster_hand_in *in)
{
    float d = in->swing - in->prev_swing;
    if (d < 0.0f) ++d;
    return in->prev_swing + d * in->pt;
}

void raster_hand_draw(const struct raster_hand_in *in, unsigned char *rgb, int w, int h)
{
    if (in->no_hand) return;
    /* its own depth buffer: a recording's clear (raster_obs.h) */
    raster_rec_marker(RASTER_CMD_CLEAR_DEPTH);
    /* recording only, nothing is drawn here and the buffer is not read */
    float *depth = NULL;
    if (!raster_rec_only()) {
        depth = malloc((size_t)w * h * sizeof *depth);
        if (!depth) return;
        for (size_t i = 0; i < (size_t)w * h; ++i) depth[i] = 1.0f;
    }
    static const double cam0[3] = {0, 0, 0};
    static const float fog0[3] = {0, 0, 0};
    struct hand_ctx c;
    memset(&c, 0, sizeof c);
    c.in = in;
    c.target = (struct entity_raster_target){.w = w, .h = h, .proj = in->proj, .mv = in->mv,
                                             .cam = cam0, .fog = fog0, .lm = in->lm,
                                             .depth = depth, .rgb = rgb, .material = {1, 1, 1},
                                             .linear_depth = 1}; /* the hand pass only */
    memcpy(c.mv, in->mv, sizeof c.mv);
    float pt = in->pt;
    float eq = in->prev_equipped + (in->equipped - in->prev_equipped) * pt;
    float pitch = in->prev_pitch + (in->pitch - in->prev_pitch) * pt;

    float saved[16];
    memcpy(saved, c.mv, sizeof saved);
    rs_gl_rotate(c.mv, pitch, 1.0f, 0.0f, 0.0f);
    rs_gl_rotate(c.mv, in->prev_yaw + (in->yaw - in->prev_yaw) * pt, 0.0f, 1.0f, 0.0f);
    set_lights(&c);
    memcpy(c.mv, saved, sizeof saved);

    float arm_pitch = in->prev_arm_pitch + (in->arm_pitch - in->prev_arm_pitch) * pt;
    float arm_yaw = in->prev_arm_yaw + (in->arm_yaw - in->prev_arm_yaw) * pt;
    rs_gl_rotate(c.mv, (in->pitch - arm_pitch) * 0.1f, 1.0f, 0.0f, 0.0f);
    rs_gl_rotate(c.mv, (in->yaw - arm_yaw) * 0.1f, 0.0f, 1.0f, 0.0f);

    const struct raster_hand_item *it = in->has_item && !in->no_hand ? &in->item : NULL;
    set_color(&c, it ? it->pass[0].tint : 0xffffff);
    float s = 0.8f;

    if (it)
    {
        memcpy(saved, c.mv, sizeof saved);
        if (in->use_count > 0)
        {
            if (it->action == HAND_USE_EAT || it->action == HAND_USE_DRINK)
            {
                float v14 = (float)in->use_count - pt + 1.0f;
                float v15 = 1.0f - v14 / (float)it->max_use;
                float v16 = 1.0f - v15;
                v16 = v16 * v16 * v16;
                v16 = v16 * v16 * v16;
                v16 = v16 * v16 * v16;
                float v17 = 1.0f - v16;
                rs_gl_translate(c.mv, 0.0f,
                                fabsf(mh_cos(v14 / 4.0f * PI_F) * 0.1f) *
                                    (float)((double)v15 > 0.2 ? 1 : 0),
                                0.0f);
                rs_gl_translate(c.mv, v17 * 0.6f, -v17 * 0.5f, 0.0f);
                rs_gl_rotate(c.mv, v17 * 90.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_rotate(c.mv, v17 * 10.0f, 1.0f, 0.0f, 0.0f);
                rs_gl_rotate(c.mv, v17 * 30.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        else
        {
            float sp = swing_progress(in);
            float v14 = mh_sin(sp * PI_F);
            float v15 = mh_sin(sqrtf(sp) * PI_F);
            rs_gl_translate(c.mv, -v15 * 0.4f, mh_sin(sqrtf(sp) * PI_F * 2.0f) * 0.2f, -v14 * 0.2f);
        }
        rs_gl_translate(c.mv, 0.7f * s, -0.65f * s - (1.0f - eq) * 0.6f, -0.9f * s);
        rs_gl_rotate(c.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        float sp = swing_progress(in);
        float v14 = mh_sin(sp * sp * PI_F);
        float v15 = mh_sin(sqrtf(sp) * PI_F);
        rs_gl_rotate(c.mv, -v14 * 20.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_rotate(c.mv, -v15 * 20.0f, 0.0f, 0.0f, 1.0f);
        rs_gl_rotate(c.mv, -v15 * 80.0f, 1.0f, 0.0f, 0.0f);
        rs_gl_scale(c.mv, 0.4f, 0.4f, 0.4f);
        if (in->use_count > 0)
        {
            if (it->action == HAND_USE_BLOCK)
            {
                rs_gl_translate(c.mv, -0.5f, 0.2f, 0.0f);
                rs_gl_rotate(c.mv, 30.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_rotate(c.mv, -80.0f, 1.0f, 0.0f, 0.0f);
                rs_gl_rotate(c.mv, 60.0f, 0.0f, 1.0f, 0.0f);
            }
            else if (it->action == HAND_USE_BOW)
            {
                rs_gl_rotate(c.mv, -18.0f, 0.0f, 0.0f, 1.0f);
                rs_gl_rotate(c.mv, -12.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_rotate(c.mv, -8.0f, 1.0f, 0.0f, 0.0f);
                rs_gl_translate(c.mv, -0.9f, 0.2f, 0.0f);
                float v18 = (float)it->max_use - ((float)in->use_count - pt + 1.0f);
                float v19 = v18 / 20.0f;
                v19 = (v19 * v19 + v19 * 2.0f) / 3.0f;
                if (v19 > 1.0f) v19 = 1.0f;
                if (v19 > 0.1f)
                    rs_gl_translate(c.mv, 0.0f, mh_sin((v18 - 0.1f) * 1.3f) * 0.01f * (v19 - 0.1f), 0.0f);
                rs_gl_translate(c.mv, 0.0f, 0.0f, v19 * 0.1f);
                rs_gl_rotate(c.mv, -335.0f, 0.0f, 0.0f, 1.0f);
                rs_gl_rotate(c.mv, -50.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_translate(c.mv, 0.0f, 0.5f, 0.0f);
                float v20 = 1.0f + v19 * 0.2f;
                rs_gl_scale(c.mv, 1.0f, 1.0f, v20);
                rs_gl_translate(c.mv, 0.0f, -0.5f, 0.0f);
                rs_gl_rotate(c.mv, 50.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_rotate(c.mv, 335.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        if (it->rotate) rs_gl_rotate(c.mv, 180.0f, 0.0f, 1.0f, 0.0f);
        render_item(&c, it, 0);
        if (it->npass > 1)
        {
            /* the second pass lies on the first: GL_LEQUAL (Minecraft's
             * depth function) lets it through */
            struct hand_ctx c1 = c;
            c1.target.depth_lequal = 1;
            set_color(&c1, it->pass[1].tint);
            render_item(&c1, it, 1);
            memcpy(c.color, c1.color, sizeof c.color);
        }
        memcpy(c.mv, saved, sizeof saved);
    }
    else if (!in->no_hand && !in->invisible && in->skin)
    {
        memcpy(saved, c.mv, sizeof saved);
        float sp = swing_progress(in);
        float v14 = mh_sin(sp * PI_F);
        float v15 = mh_sin(sqrtf(sp) * PI_F);
        rs_gl_translate(c.mv, -v15 * 0.3f, mh_sin(sqrtf(sp) * PI_F * 2.0f) * 0.4f, -v14 * 0.4f);
        rs_gl_translate(c.mv, 0.8f * s, -0.75f * s - (1.0f - eq) * 0.6f, -0.9f * s);
        rs_gl_rotate(c.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        sp = swing_progress(in);
        v14 = mh_sin(sp * sp * PI_F);
        v15 = mh_sin(sqrtf(sp) * PI_F);
        rs_gl_rotate(c.mv, v15 * 70.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_rotate(c.mv, -v14 * 20.0f, 0.0f, 0.0f, 1.0f);
        rs_gl_translate(c.mv, -1.0f, 3.6f, 3.5f);
        rs_gl_rotate(c.mv, 120.0f, 0.0f, 0.0f, 1.0f);
        rs_gl_rotate(c.mv, 200.0f, 1.0f, 0.0f, 0.0f);
        rs_gl_rotate(c.mv, -135.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_scale(c.mv, 1.0f, 1.0f, 1.0f);
        rs_gl_translate(c.mv, 5.6f, 0.0f, 0.0f);
        rs_gl_scale(c.mv, 1.0f, 1.0f, 1.0f);
        first_person_arm(&c);
        memcpy(c.mv, saved, sizeof saved);
    }
    free(depth);
}

/* ------------------------------------------------------------ recordings */

static float jf(const struct jval *v)
{
    uint32_t b = 0;
    json_float(v, &b);
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static double jd(const struct jval *v)
{
    uint64_t b = 0;
    json_double(v, &b);
    double d;
    memcpy(&d, &b, 8);
    return d;
}

static int ji(const struct jval *v, int dflt)
{
    int64_t n;
    return json_int(v, &n) ? (int)n : dflt;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!s || fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = 0;
    fclose(f);
    return s;
}

static char *last_line(const char *path)
{
    char *all = read_all(path);
    if (!all) return NULL;
    size_t n = strlen(all);
    while (n && (all[n - 1] == '\n' || all[n - 1] == '\r')) all[--n] = 0;
    char *nl = strrchr(all, '\n');
    char *out = strdup(nl ? nl + 1 : all);
    free(all);
    return out;
}

unsigned char *raster_hand_read_rgba(const char *scene, const char *name, const char *fallback,
                                     size_t n)
{
    char path[1200];
    const char *tries[3] = {NULL, NULL, NULL};
    char a[1200], b[1200];
    if (scene && name) { snprintf(path, sizeof path, "%s/%s", scene, name); tries[0] = path; }
    if (fallback)
    {
        snprintf(a, sizeof a, "out/assets/%s", fallback);
        snprintf(b, sizeof b, "../out/assets/%s", fallback);
        tries[1] = a;
        tries[2] = b;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (!tries[i]) continue;
        FILE *f = fopen(tries[i], "rb");
        if (!f) continue;
        unsigned char *px = malloc(n);
        int ok = px && fread(px, 1, n, f) == n;
        fclose(f);
        if (ok) return px;
        free(px);
    }
    return NULL;
}

/* An icon's uv range from a RenderStateProbe item record. */
static void read_pass(const struct jval *o, struct raster_hand_item *it, int pass)
{
    it->pass[pass].tint = ji(json_get(o, "tint"), 0xffffff);
    it->pass[pass].has_icon = json_get(o, "minU") != NULL;
    it->pass[pass].min_u = jf(json_get(o, "minU"));
    it->pass[pass].max_u = jf(json_get(o, "maxU"));
    it->pass[pass].min_v = jf(json_get(o, "minV"));
    it->pass[pass].max_v = jf(json_get(o, "maxV"));
    it->pass[pass].icon_w = ji(json_get(o, "iw"), 16);
    it->pass[pass].icon_h = ji(json_get(o, "ih"), 16);
}

/* The use action, duration and turn of the vanilla items the recordings and the
 * client hold, for records that do not carry them. */
static void item_defaults(struct raster_hand_item *it)
{
    static const int food[] = {260, 282, 297, 319, 320, 322, 349, 350, 357, 360, 363, 364, 365,
                               366, 367, 375, 391, 392, 393, 394, 396, 400};
    static const int swords[] = {267, 268, 272, 276, 283};
    it->action = HAND_USE_NONE;
    it->max_use = 0;
    for (size_t i = 0; i < sizeof food / sizeof food[0]; ++i)
        if (it->id == food[i]) { it->action = HAND_USE_EAT; it->max_use = 32; }
    for (size_t i = 0; i < sizeof swords / sizeof swords[0]; ++i)
        if (it->id == swords[i]) { it->action = HAND_USE_BLOCK; it->max_use = 72000; }
    if (it->id == 335 || it->id == 373) { it->action = HAND_USE_DRINK; it->max_use = 32; }
    if (it->id == 261) { it->action = HAND_USE_BOW; it->max_use = 72000; }
    it->rotate = it->id == 346 || it->id == 398;
    it->grass = it->id == 2;
}

/* A HUD item record (RenderStateProbe.item: top-level pass 0, "pass1"). */
static int hud_item(const struct jval *o, struct raster_hand_item *it)
{
    if (!o || o->kind != J_OBJ) return 0;
    memset(it, 0, sizeof *it);
    it->id = ji(json_get(o, "id"), 0);
    it->meta = ji(json_get(o, "dmg"), 0);
    it->sprite = ji(json_get(o, "sprite"), 1);
    int mode = ji(json_get(o, "mode"), 0);
    it->block3d = mode == 1;
    it->npass = mode == 2 ? 2 : 1;
    read_pass(o, it, 0);
    if (mode == 2) read_pass(json_get(o, "pass1"), it, 1);
    item_defaults(it);
    it->render_color = it->pass[0].tint;
    it->eff = ji(json_get(o, "effect"), 0);
    return 1;
}

/* The hand block's item (RenderStateProbe.handItem). */
static int hand_item(const struct jval *o, struct raster_hand_item *it)
{
    if (!hud_item(o, it)) return 0;
    it->action = ji(json_get(o, "action"), it->action);
    it->max_use = ji(json_get(o, "maxuse"), it->max_use);
    it->rotate = ji(json_get(o, "rot"), it->rotate);
    it->render_color = ji(json_get(o, "rc"), it->render_color);
    const struct jval *passes = json_get(o, "passes");
    int n = json_len(passes);
    if (n > 0)
    {
        it->npass = n > 2 ? 2 : n;
        for (int i = 0; i < it->npass; ++i) read_pass(json_at(passes, i), it, i);
    }
    return 1;
}

static int item_rec(const struct jval *r, void *out, int *dmg)
{
    *dmg = ji(json_get(r, "dmg"), -1);
    return hud_item(r, out);
}

/* the scene's item table (itemtab.h: one converted copy a process, shared
 * by every env's view): item id's record whose dmg is meta, else its first */
int raster_hand_item_lookup(const char *scene, int id, int meta, struct raster_hand_item *out)
{
    if (!scene) return 0;
    return itemtab_lookup(itemtab_get(scene, sizeof *out, item_rec), id, meta, out);
}

/* World.getSkyBlockTypeBrightness for a saved light value. */
static int saved_light(const struct rb_world *w, const struct rb_table *t, int sky, int x, int y,
                       int z)
{
    if (y < 0) y = 0;
    if (y >= 256) return sky ? 15 : 0;
    int id = rb_world_block(w, x, y, z);
    if (t && id > 0 && id < RB_IDS && t->props[id].neighbor)
    {
        static const int d[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        int best = 0;
        for (int i = 0; i < 5; ++i)
        {
            int v = sky ? rb_world_sky(w, x + d[i][0], y + d[i][1], z + d[i][2])
                        : rb_world_blocklight(w, x + d[i][0], y + d[i][1], z + d[i][2]);
            if (v > best) best = v;
        }
        return best;
    }
    return sky ? rb_world_sky(w, x, y, z) : rb_world_blocklight(w, x, y, z);
}

void raster_hand_scene(const char *scene, unsigned char *rgb, int w, int h,
                       const uint32_t lm[256], const struct rb_world *world,
                       const struct rb_table *table)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    char *line = last_line(path);
    if (!line) return;
    struct jval *root = json_parse(line); /* owns line */
    if (!root) return;
    const struct jval *opt = json_get(root, "opt"), *er = json_get(root, "er");
    const struct jval *pl = json_get(root, "pl"), *hud = json_get(root, "hud");
    const struct jval *hand = json_get(root, "hand");
    /* renderWorld's gates: the camera zoom and the debug view; renderHand's:
     * first person, awake, the GUI shown (a recording without the HUD block
     * predates the GUI option and is left as it was drawn). */
    if (!hud || ji(json_get(opt, "tpv"), 0) || ji(json_get(pl, "sleep"), 0) ||
        ji(json_get(er, "dvd"), 0) > 0 || (json_get(er, "zoom") && jd(json_get(er, "zoom")) != 1.0))
    {
        json_free(root);
        return;
    }
    struct rs_in rs;
    memset(&rs, 0, sizeof rs);
    const struct jval *disp = json_get(root, "disp");
    rs.dw = ji(json_at(disp, 0), w);
    rs.dh = ji(json_at(disp, 1), h);
    rs.rd = ji(json_get(opt, "rd"), 4);
    rs.bob = ji(json_get(opt, "bob"), 0);
    rs.pt = jf(json_get(root, "pt"));
    rs.hurt = ji(json_get(pl, "hurt"), 0);
    rs.mhurt = ji(json_get(pl, "mhurt"), 0);
    rs.death = ji(json_get(pl, "death"), 0);
    rs.aaty = jf(json_get(pl, "aaty"));
    rs.hp = jf(json_get(pl, "hp"));
    rs.dwm = jf(json_get(pl, "dwm"));
    rs.pdwm = jf(json_get(pl, "pdwm"));
    rs.ecyaw = jf(json_get(pl, "cyaw"));
    rs.pecyaw = jf(json_get(pl, "pcyaw"));
    rs.ecpit = jf(json_get(pl, "cpit"));
    rs.pepit = jf(json_get(pl, "pcpit"));
    rs.mat = ji(json_get(json_get(root, "vp"), "mat"), 0);
    float proj[16], mv[16];
    renderstate_hand_camera(&rs, proj, mv);

    struct raster_hand_in in;
    memset(&in, 0, sizeof in);
    in.pt = rs.pt;
    in.pitch = jf(json_get(pl, "pit"));
    in.prev_pitch = jf(json_get(pl, "ppit"));
    in.yaw = jf(json_get(pl, "yaw"));
    in.prev_yaw = jf(json_get(pl, "pyaw"));
    in.proj = proj;
    in.mv = mv;
    in.lm = lm;
    in.scene = scene;
    /* renderItemInFirstPerson's own gates (renderOverlays, drawn after the hand by
     * raster_worldfx, runs without them).
     * A recording older than the hand block that is mid-dig swings every
     * tick at a phase it did not record: its hand is left undrawn. */
    in.no_hand = ji(json_get(hud, "hide"), 1) || (hand && ji(json_get(hand, "esm"), 0)) ||
                 (!hand && json_get(json_get(root, "overlay"), "damage"));
    if (hand)
    {
        in.equipped = jf(json_get(hand, "eq"));
        in.prev_equipped = jf(json_get(hand, "peq"));
        in.swing = jf(json_get(hand, "sw"));
        in.prev_swing = jf(json_get(hand, "psw"));
        in.arm_pitch = jf(json_get(hand, "ap"));
        in.prev_arm_pitch = jf(json_get(hand, "pap"));
        in.arm_yaw = jf(json_get(hand, "ay"));
        in.prev_arm_yaw = jf(json_get(hand, "pay"));
        in.use_count = ji(json_get(hand, "use"), 0);
        in.light = ji(json_get(hand, "light"), 15728880);
        in.invisible = ji(json_get(hand, "inv"), 0);
        in.has_item = hand_item(json_get(hand, "item"), &in.item);
    }
    else
    {
        /* settled: fully equipped, no swing, the arm caught up with the look */
        in.equipped = in.prev_equipped = 1.0f;
        in.arm_pitch = in.prev_arm_pitch = in.pitch;
        in.arm_yaw = in.prev_arm_yaw = in.yaw;
        const struct jval *inv = json_get(hud, "inv");
        in.has_item = hud_item(json_at(inv, ji(json_get(hud, "cur"), 0)), &in.item);
        if (in.has_item && in.item.grass && table && table->grass_map)
            in.item.render_color = (int)(table->grass_map[127 << 8 | 127] & 0xffffff);
        int bx = mh_floor(jd(json_get(pl, "x"))), by = mh_floor(jd(json_get(pl, "y")));
        int bz = mh_floor(jd(json_get(pl, "z")));
        int nosky = ji(json_get(json_get(root, "wo"), "nosky"), 0);
        in.light = world ? (nosky ? 0 : saved_light(world, table, 1, bx, by, bz)) << 20 |
                               saved_light(world, table, 0, bx, by, bz) << 4
                         : 15728880;
    }

    in.skin = raster_hand_read_rgba(scene, "state/skin.rgba", "mobs/steve.rgba", 64 * 32 * 4);
    unsigned char *items = NULL, *blocks = NULL, *chest = NULL;
    snprintf(path, sizeof path, "%s/state/gui.json", scene);
    char *gui = read_all(path);
    struct jval *gj = gui ? json_parse(gui) : NULL;
    in.items_w = ji(json_get(json_get(gj, "items"), "w"), 256);
    in.items_h = ji(json_get(json_get(gj, "items"), "h"), 256);
    json_free(gj);
    snprintf(path, sizeof path, "%s/atlas.json", scene);
    char *atlas = read_all(path);
    struct jval *aj = atlas ? json_parse(atlas) : NULL;
    in.blocks_w = ji(json_get(aj, "atlas_width"), 0);
    in.blocks_h = ji(json_get(aj, "atlas_height"), 0);
    json_free(aj);
    if (in.has_item)
    {
        items = raster_hand_read_rgba(scene, "state/gui_items.rgba", NULL,
                                      (size_t)in.items_w * in.items_h * 4);
        texanim_patch_scene(scene, TEXANIM_ITEMS, items, in.items_w, in.items_h);
        if (in.blocks_w > 0 && in.blocks_h > 0)
            blocks = raster_hand_read_rgba(scene, "atlas.rgba", NULL,
                                           (size_t)in.blocks_w * in.blocks_h * 4);
        texanim_patch_scene(scene, TEXANIM_BLOCKS, blocks, in.blocks_w, in.blocks_h);
        chest = raster_hand_read_rgba(scene, "state/gui_chest.rgba", NULL, 64 * 64 * 4);
    }
    in.items = items;
    in.blocks = blocks;
    in.chest = chest;
    unsigned char *glint = NULL;
    if (in.has_item && in.item.eff)
    {
        glint = raster_hand_read_rgba(scene, "state/gui_glint.rgba", NULL, 64 * 64 * 4);
        int64_t now = 0;
        json_int(json_get(json_get(hud, "toast"), "now"), &now);
        in.now = now;
    }
    in.glint = glint;
    raster_hand_draw(&in, rgb, w, h);
    free(glint);
    free((void *)in.skin);
    free(items);
    free(blocks);
    free(chest);
    json_free(root);
}
