#define _POSIX_C_SOURCE 200809L
/* The non-living entity renders and the shared held-item path (raster_things.h),
 * each a statement-for-statement port of its Render class drawn through the
 * fixed-function emulation in raster_glx.c. */
#include "raster_things.h"
#include "block_item_model.h"
#include "jmath.h"
#include "jrand.h"
#include "render_blocks.h"
#include "renderstate.h"
#include "tape.h"
#include "texanim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.1415927f

/* ------------------------------------------------------------ json */

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
    int64_t n = 0;
    return json_int(v, &n) ? (int)n : dflt;
}

/* ------------------------------------------------------------ textures */

struct tex_slot
{
    char scene[1024], name[64];
    unsigned char *px;
    int w, h;
};
static struct tex_slot tex_cache[48];

static unsigned char *read_rgba(const char *path, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *b = malloc(n);
    if (b && fread(b, 1, n, f) != n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

const unsigned char *thing_texture(const char *scene, const char *name, int w, int h)
{
    for (size_t i = 0; i < sizeof tex_cache / sizeof tex_cache[0]; ++i)
        if (tex_cache[i].px && !strcmp(tex_cache[i].scene, scene) && !strcmp(tex_cache[i].name, name))
            return tex_cache[i].px;
    char p[1200];
    snprintf(p, sizeof p, "%s/state/%s.rgba", scene, name);
    unsigned char *px = read_rgba(p, (size_t)w * h * 4);
    if (!px)
    {
        snprintf(p, sizeof p, "out/assets/%s.rgba", name);
        px = read_rgba(p, (size_t)w * h * 4);
    }
    if (!px && !strncmp(name, "gui_chest", 9))
    {
        /* TileEntityRendererChestHelper's textures, from the jar's own
         * entity/chest sheets (out/assets/te) when the scene has none */
        const char *te = !strcmp(name, "gui_chest_ender") ? "ender" : !strcmp(name, "gui_chest_trapped") ? "trapped" : "chest";
        snprintf(p, sizeof p, "out/assets/te/%s.rgba", te);
        px = read_rgba(p, (size_t)w * h * 4);
    }
    if (!px) return NULL;
    if (!strcmp(name, "gui_items")) texanim_patch_scene(scene, TEXANIM_ITEMS, px, w, h);
    /* a slot whose scene changed is reused; the oldest otherwise */
    static size_t next;
    size_t slot = next++ % (sizeof tex_cache / sizeof tex_cache[0]);
    for (size_t i = 0; i < sizeof tex_cache / sizeof tex_cache[0]; ++i)
        if (!tex_cache[i].px || strcmp(tex_cache[i].scene, scene)) { slot = i; break; }
    free(tex_cache[slot].px);
    tex_cache[slot].px = px;
    tex_cache[slot].w = w;
    tex_cache[slot].h = h;
    snprintf(tex_cache[slot].scene, sizeof tex_cache[slot].scene, "%s", scene);
    snprintf(tex_cache[slot].name, sizeof tex_cache[slot].name, "%s", name);
    return px;
}

/* The block atlas (atlas.rgba at atlas.json's size, the animated sprites
 * patched to the recorded frame) and its sprite table. */
static struct
{
    char scene[1024];
    unsigned char *px;
    int w, h;
    struct rb_atlas atlas;
    int has_atlas;
    struct texanim_memo memo;     /* the frames patched into px (only block_atlas writes it) */
} blocks;

static const unsigned char *block_atlas(const char *scene, int *w, int *h)
{
    if (blocks.px && !strcmp(blocks.scene, scene))
    {
        /* the animated sprites (the fire the burning overlay draws) at the
         * frame's state, not the state of the first frame that loaded it */
        texanim_patch_scene_memo(scene, TEXANIM_BLOCKS, blocks.px, blocks.w, blocks.h, &blocks.memo);
        *w = blocks.w; *h = blocks.h;
        return blocks.px;
    }
    free(blocks.px);
    blocks.px = NULL;
    if (blocks.has_atlas) rb_atlas_free(&blocks.atlas);
    blocks.has_atlas = 0;
    char p[1200];
    snprintf(p, sizeof p, "%s/atlas.json", scene);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    struct jval *j = json_parse(b);
    int aw = ji(json_get(j, "atlas_width"), 0), ah = ji(json_get(j, "atlas_height"), 0);
    json_free(j);
    if (aw <= 0 || ah <= 0) return NULL;
    snprintf(p, sizeof p, "%s/atlas.rgba", scene);
    blocks.px = read_rgba(p, (size_t)aw * ah * 4);
    if (!blocks.px) return NULL;
    blocks.memo.atlas = NULL;
    texanim_patch_scene_memo(scene, TEXANIM_BLOCKS, blocks.px, aw, ah, &blocks.memo);
    blocks.w = aw;
    blocks.h = ah;
    blocks.has_atlas = rb_atlas_load(&blocks.atlas, scene) == 0;
    snprintf(blocks.scene, sizeof blocks.scene, "%s", scene);
    *w = aw;
    *h = ah;
    return blocks.px;
}

static const struct rb_uv *block_sprite(const char *scene, const char *name)
{
    int w, h;
    if (!block_atlas(scene, &w, &h) || !blocks.has_atlas) return NULL;
    for (int i = 0; i < blocks.atlas.n; ++i)
        if (!strcmp(blocks.atlas.name[i], name)) return &blocks.atlas.uv[i];
    return NULL;
}

/* ------------------------------------------------------------ items */

static void read_uv(const struct jval *o, float *min_u, float *max_u, float *min_v, float *max_v)
{
    *min_u = jf(json_get(o, "minU"));
    *max_u = jf(json_get(o, "maxU"));
    *min_v = jf(json_get(o, "minV"));
    *max_v = jf(json_get(o, "maxV"));
}

int thing_item_parse(const struct jval *o, struct thing_item *it)
{
    memset(it, 0, sizeof *it);
    if (!o || o->kind != J_OBJ || !json_get(o, "id")) return 0;
    it->id = ji(json_get(o, "id"), 0);
    it->meta = ji(json_get(o, "dmg"), 0);
    it->sprite = ji(json_get(o, "sprite"), 1);
    it->b3d = ji(json_get(o, "b3d"), 0);
    it->ib3d = ji(json_get(o, "ib3d"), it->b3d);
    it->bpass = ji(json_get(o, "bpass"), 0);
    it->rc = ji(json_get(o, "rc"), 0xffffff);
    it->full3d = ji(json_get(o, "full3d"), 0);
    it->rot = ji(json_get(o, "rot"), 0);
    it->multi = ji(json_get(o, "multi"), 0);
    it->bow = ji(json_get(o, "bow"), 0);
    it->eff = ji(json_get(o, "eff"), 0);
    const struct jval *passes = json_get(o, "passes");
    it->npass = json_len(passes) > 2 ? 2 : json_len(passes);
    for (int i = 0; i < it->npass; ++i)
    {
        const struct jval *q = json_at(passes, i);
        it->pass[i].tint = ji(json_get(q, "tint"), 0xffffff);
        if (json_get(q, "minU"))
        {
            read_uv(q, &it->pass[i].min_u, &it->pass[i].max_u, &it->pass[i].min_v, &it->pass[i].max_v);
            it->pass[i].iw = ji(json_get(q, "iw"), 16);
            it->pass[i].ih = ji(json_get(q, "ih"), 16);
            it->pass[i].has = 1;
        }
    }
    if (json_get(o, "ari"))
    {
        it->armor = 1;
        it->armor_index = ji(json_get(o, "ari"), 0);
        it->cloth = ji(json_get(o, "cloth"), 0);
        it->color = ji(json_get(o, "color"), 0xffffff);
    }
    return 1;
}

void thing_block_as_item(struct glx *g, const char *scene, int id, int meta, int rc, float brightness)
{
    struct block_item_model model;
    if (block_item_model_build(scene, id, meta, &model)) return;
    int tw, th;
    const unsigned char *tex = block_atlas(scene, &tw, &th);
    if (model.chest_texture)
    {
        tex = thing_texture(scene, model.chest_texture == 2 ? "gui_chest_ender" :
                                   model.chest_texture == 3 ? "gui_chest_trapped" : "gui_chest", 64, 64);
        tw = th = 64;
    }
    if (!tex) return;
    float saved[16];
    memcpy(saved, g->mv, sizeof saved);
    int shifted = 0;
    if (model.inner_rotate_y90)
    {
        rs_gl_rotate(g->mv, 90.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_translate(g->mv, -0.5f, -0.5f, -0.5f);
        shifted = 1;
    }
    else if (!model.chest_texture)
    {
        rs_gl_translate(g->mv, -0.5f, -0.5f, -0.5f);
        shifted = 1;
    }
    /* useInventoryTint: glColor4f(getRenderColor(meta) * brightness, 1), white
     * for grass, whose top face alone takes the render colour */
    int grass = id == 2;
    for (int i = 0; i < model.n; ++i)
    {
        const struct block_item_quad *q = &model.quad[i];
        int c = grass && !q->tint_top ? 0xffffff : rc;
        g->color[0] = (float)(c >> 16 & 255) / 255.0f * brightness;
        g->color[1] = (float)(c >> 8 & 255) / 255.0f * brightness;
        g->color[2] = (float)(c & 255) / 255.0f * brightness;
        g->color[3] = 1.0f;
        float p[4][3];
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 3; ++j) p[k][j] = shifted ? q->p[k][j] + 0.5f : q->p[k][j];
        glx_quad(g, p, (const float(*)[2])q->uv, q->normal, tex, tw, th);
    }
    memcpy(g->mv, saved, sizeof saved);
}

/* The two glint passes of ItemRenderer.renderItem over the sprite it just
 * drew: GL_EQUAL, lighting off, GL_SRC_COLOR, GL_ONE, colour 0.76 * (0.5,
 * 0.25, 0.8), each a renderItemIn2D of the whole glint texture under its own
 * texture matrix. */
static void item_glint(struct glx *g, const char *scene, int64_t now)
{
    const unsigned char *glint = thing_texture(scene, "gui_glint", 64, 64);
    if (!glint) return;
    struct glx s = *g;
    s.lighting = 0;
    s.t.depth_equal = 1;
    s.t.blend = 5;
    float v16 = 0.76f;
    s.color[0] = 0.5f * v16;
    s.color[1] = 0.25f * v16;
    s.color[2] = 0.8f * v16;
    s.color[3] = 1.0f;
    for (int pass = 0; pass < 2; ++pass)
    {
        float tm[16];
        rs_gl_identity(tm);
        float v17 = 0.125f;
        rs_gl_scale(tm, v17, v17, v17);
        if (pass == 0)
        {
            float v18 = (float)(now % 3000LL) / 3000.0f * 8.0f;
            rs_gl_translate(tm, v18, 0.0f, 0.0f);
            rs_gl_rotate(tm, -50.0f, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            float v18 = (float)(now % 4873LL) / 4873.0f * 8.0f;
            rs_gl_translate(tm, -v18, 0.0f, 0.0f);
            rs_gl_rotate(tm, 10.0f, 0.0f, 0.0f, 1.0f);
        }
        s.tex_matrix = 1;
        memcpy(s.tm, tm, sizeof tm);
        glx_item_in_2d(&s, 0.0f, 0.0f, 1.0f, 1.0f, 256, 256, 0.0625f, glint, 64, 64);
    }
}

void thing_render_item(struct glx *g, const char *scene, const struct thing_item *it, int pass,
                       int64_t now)
{
    float saved[16];
    memcpy(saved, g->mv, sizeof saved);
    struct entity_raster_target saved_t = g->t;
    if (it->bpass != 0)
    {
        /* a translucent block item: GL_BLEND and GL_CULL_FACE on */
        g->t.blend = 3;
        g->t.alpha = 1.0f;
        g->t.two_sided = 0;
    }
    if (it->sprite == 0 && it->b3d)
    {
        thing_block_as_item(g, scene, it->id, it->meta, it->rc, 1.0f);
    }
    else if (pass < it->npass && it->pass[pass].has)
    {
        int tw = 256, th = 256;
        const unsigned char *tex;
        if (it->sprite == 1)
            tex = thing_texture(scene, "gui_items", 256, 256);
        else
            tex = block_atlas(scene, &tw, &th);
        if (tex)
        {
            rs_gl_translate(g->mv, -0.0f, -0.3f, 0.0f);
            rs_gl_scale(g->mv, 1.5f, 1.5f, 1.5f);
            rs_gl_rotate(g->mv, 50.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_rotate(g->mv, 335.0f, 0.0f, 0.0f, 1.0f);
            rs_gl_translate(g->mv, -0.9375f, -0.0625f, 0.0f);
            glx_item_in_2d(g, it->pass[pass].max_u, it->pass[pass].min_v, it->pass[pass].min_u,
                           it->pass[pass].max_v, it->pass[pass].iw, it->pass[pass].ih, 0.0625f, tex,
                           tw, th);
            if (it->eff && pass == 0)
            {
                item_glint(g, scene, now);
                /* the glint leaves its glColor set: a renderer that does not
                 * set the next pass's colour (RenderWitch) draws it tinted */
                g->color[0] = 0.5f * 0.76f;
                g->color[1] = 0.25f * 0.76f;
                g->color[2] = 0.8f * 0.76f;
                g->color[3] = 1.0f;
            }
        }
    }
    g->t = saved_t;
    memcpy(g->mv, saved, sizeof saved);
}

/* ------------------------------------------------------------ the renders */

struct view
{
    float yaw, pitch; /* RenderManager.playerViewY and playerViewX */
    float pt;
    int64_t now;
};

/* RenderArrow.doRender */
static void draw_arrow(struct glx *g, const char *scene, const struct jval *e, float x, float y,
                       float z, float pt)
{
    float yaw = jf(json_get(e, "pyaw")) + (jf(json_get(e, "yaw")) - jf(json_get(e, "pyaw"))) * pt;
    float pitch = jf(json_get(e, "ppitch")) + (jf(json_get(e, "pitch")) - jf(json_get(e, "ppitch"))) * pt;
    rs_gl_translate(g->mv, x, y, z);
    thing_arrow(g, scene, yaw, pitch, (float)ji(json_get(e, "shake"), 0) - pt);
}

void thing_arrow(struct glx *g, const char *scene, float yaw, float pitch, float shake)
{
    const unsigned char *tex = thing_texture(scene, "arrow", 32, 32);
    if (!tex) return;
    rs_gl_rotate(g->mv, yaw - 90.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(g->mv, pitch, 0.0f, 0.0f, 1.0f);
    float v12 = 0.0f, v13 = 0.5f, v14 = 0.0f / 32.0f, v15 = 5.0f / 32.0f;
    float v16 = 0.0f, v17 = 0.15625f, v18 = 5.0f / 32.0f, v19 = 10.0f / 32.0f;
    float v20 = 0.05625f;
    float v21 = shake;
    if (v21 > 0.0f)
    {
        float v22 = -mh_sin(v21 * 3.0f) * v21;
        rs_gl_rotate(g->mv, v22, 0.0f, 0.0f, 1.0f);
    }
    rs_gl_rotate(g->mv, 45.0f, 1.0f, 0.0f, 0.0f);
    rs_gl_scale(g->mv, v20, v20, v20);
    rs_gl_translate(g->mv, -4.0f, 0.0f, 0.0f);
    g->normal[0] = v20; g->normal[1] = 0.0f; g->normal[2] = 0.0f;
    {
        const float p[4][3] = {{-7, -2, -2}, {-7, -2, 2}, {-7, 2, 2}, {-7, 2, -2}};
        const float uv[4][2] = {{v16, v18}, {v17, v18}, {v17, v19}, {v16, v19}};
        glx_quad(g, p, uv, NULL, tex, 32, 32);
    }
    g->normal[0] = -v20;
    {
        const float p[4][3] = {{-7, 2, -2}, {-7, 2, 2}, {-7, -2, 2}, {-7, -2, -2}};
        const float uv[4][2] = {{v16, v18}, {v17, v18}, {v17, v19}, {v16, v19}};
        glx_quad(g, p, uv, NULL, tex, 32, 32);
    }
    for (int i = 0; i < 4; ++i)
    {
        rs_gl_rotate(g->mv, 90.0f, 1.0f, 0.0f, 0.0f);
        g->normal[0] = 0.0f; g->normal[1] = 0.0f; g->normal[2] = v20;
        const float p[4][3] = {{-8, -2, 0}, {8, -2, 0}, {8, 2, 0}, {-8, 2, 0}};
        const float uv[4][2] = {{v12, v14}, {v13, v14}, {v13, v15}, {v12, v15}};
        glx_quad(g, p, uv, NULL, tex, 32, 32);
    }
}

/* RenderSnowball.func_77026_a and RenderFireball's quad: the billboard
 * facing the view, normal (0, 1, 0) through the Tessellator. */
static void billboard(struct glx *g, const struct view *v, float min_u, float max_u, float min_v,
                      float max_v, const unsigned char *tex, int tw, int th)
{
    float v7 = 1.0f, v8 = 0.5f, v9 = 0.25f;
    rs_gl_rotate(g->mv, 180.0f - v->yaw, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(g->mv, -v->pitch, 1.0f, 0.0f, 0.0f);
    const float p[4][3] = {{0.0f - v8, 0.0f - v9, 0}, {v7 - v8, 0.0f - v9, 0},
                           {v7 - v8, v7 - v9, 0}, {0.0f - v8, v7 - v9, 0}};
    const float uv[4][2] = {{min_u, max_v}, {max_u, max_v}, {max_u, min_v}, {min_u, min_v}};
    const float n[3] = {0, 1, 0};
    glx_quad(g, p, uv, n, tex, tw, th);
}

static void draw_sprite(struct glx *g, const char *scene, const struct jval *e, float x, float y,
                        float z, const struct view *v, float scale)
{
    const unsigned char *items = thing_texture(scene, "gui_items", 256, 256);
    if (!items || !json_get(e, "minU")) return;
    float u0, u1, v0, v1;
    read_uv(e, &u0, &u1, &v0, &v1);
    rs_gl_translate(g->mv, x, y, z);
    rs_gl_scale(g->mv, scale, scale, scale);
    const struct jval *ov = json_get(e, "overlay");
    if (ov)
    {
        int c = ji(json_get(e, "color"), 0xffffff);
        g->color[0] = (float)(c >> 16 & 255) / 255.0f;
        g->color[1] = (float)(c >> 8 & 255) / 255.0f;
        g->color[2] = (float)(c & 255) / 255.0f;
        float saved[16];
        memcpy(saved, g->mv, sizeof saved);
        float o0, o1, o2, o3;
        read_uv(ov, &o0, &o1, &o2, &o3);
        billboard(g, v, o0, o1, o2, o3, items, 256, 256);
        memcpy(g->mv, saved, sizeof saved);
        g->color[0] = g->color[1] = g->color[2] = 1.0f;
    }
    billboard(g, v, u0, u1, v0, v1, items, 256, 256);
}

/* RenderTNTPrimed.doRender */
static void draw_tnt(struct glx *g, const char *scene, const struct jval *e, float x, float y,
                     float z, float pt)
{
    int fuse = ji(json_get(e, "fuse"), 0);
    rs_gl_translate(g->mv, x, y, z);
    float v10;
    if ((float)fuse - pt + 1.0f < 10.0f)
    {
        v10 = 1.0f - ((float)fuse - pt + 1.0f) / 10.0f;
        if (v10 < 0.0f) v10 = 0.0f;
        if (v10 > 1.0f) v10 = 1.0f;
        v10 *= v10;
        v10 *= v10;
        float v11 = 1.0f + v10 * 0.3f;
        rs_gl_scale(g->mv, v11, v11, v11);
    }
    thing_block_as_item(g, scene, 46, 0, 0xffffff, jf(json_get(e, "bright")));
    if (fuse / 5 % 2 == 0)
    {
        /* texture and lighting off, GL_SRC_ALPHA, GL_DST_ALPHA; renderBlockAsItem
         * sets glColor4f(1, 1, 1, 1) itself, and the pixels the first draw
         * wrote carry destination alpha 1: white added through the lightmap */
        struct glx f = *g;
        f.lighting = 0;
        f.t.blend = 2;
        f.t.no_alpha = 1;
        struct block_item_model model;
        if (!block_item_model_build(scene, 46, 0, &model))
        {
            float saved[16];
            memcpy(saved, f.mv, sizeof saved);
            rs_gl_rotate(f.mv, 90.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_translate(f.mv, -0.5f, -0.5f, -0.5f);
            f.color[0] = f.color[1] = f.color[2] = f.color[3] = 1.0f;
            for (int i = 0; i < model.n; ++i)
            {
                float p[4][3];
                for (int k = 0; k < 4; ++k)
                    for (int j = 0; j < 3; ++j) p[k][j] = model.quad[i].p[k][j] + 0.5f;
                glx_quad(&f, p, (const float(*)[2])model.quad[i].uv, model.quad[i].normal,
                         glx_white, 1, 1);
            }
            memcpy(f.mv, saved, sizeof saved);
        }
    }
}

/* RenderFallingBlock.doRender: renderBlockSandFalling with lighting off, the
 * face shades 0.5, 1.0, 0.8, 0.8, 0.6, 0.6 and the block's brightness at the
 * entity's block as every vertex's lightmap coordinate. */
static void draw_falling(struct glx *g, const char *scene, const struct jval *e, float x, float y,
                         float z)
{
    if (!ji(json_get(e, "draws"), 0)) return;
    int id = ji(json_get(e, "block"), 0), meta = ji(json_get(e, "meta"), 0);
    const struct jval *quads = json_get(e, "quads");
    if (quads)
    {
        /* the anvil and the dragon egg: renderBlockAnvilMetadata and
         * renderBlockDragonEgg's quads over the world, each face one colour
         * and one brightness (the non-AO path), lighting off */
        int tw, th;
        const unsigned char *tex = block_atlas(scene, &tw, &th);
        if (!tex) return;
        rs_gl_translate(g->mv, x, y, z);
        g->lighting = 0;
        int n = json_len(quads) / 7;
        for (int q = 0; q + 3 < n; q += 4)
        {
            float p[4][3], uv[4][2];
            for (int k = 0; k < 4; ++k)
            {
                int o = (q + k) * 7;
                p[k][0] = jf(json_at(quads, o));
                p[k][1] = jf(json_at(quads, o + 1));
                p[k][2] = jf(json_at(quads, o + 2));
                uv[k][0] = jf(json_at(quads, o + 3));
                uv[k][1] = jf(json_at(quads, o + 4));
            }
            int c = ji(json_at(quads, q * 7 + 5), -1), br = ji(json_at(quads, q * 7 + 6), 0);
            g->color[0] = (float)(c & 255) / 255.0f;
            g->color[1] = (float)(c >> 8 & 255) / 255.0f;
            g->color[2] = (float)(c >> 16 & 255) / 255.0f;
            g->lb = br % 65536;
            g->ls = br / 65536;
            glx_quad(g, p, (const float(*)[2])uv, NULL, tex, tw, th);
        }
        return;
    }
    struct block_item_model model;
    if (block_item_model_build(scene, id, meta, &model)) return;
    int tw, th;
    const unsigned char *tex = block_atlas(scene, &tw, &th);
    if (!tex) return;
    rs_gl_translate(g->mv, x, y, z);
    g->lighting = 0;
    int bbr = ji(json_get(e, "bbr"), 0);
    g->lb = bbr % 65536;
    g->ls = bbr / 65536;
    static const float shade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
    for (int i = 0; i < model.n && i < 6; ++i)
    {
        /* block_item_model builds the six faces in renderFace order */
        const struct block_item_quad *q = &model.quad[i];
        int side = q->normal[1] < 0 ? 0 : q->normal[1] > 0 ? 1 : q->normal[2] < 0 ? 2
                 : q->normal[2] > 0 ? 3 : q->normal[0] < 0 ? 4 : 5;
        g->color[0] = g->color[1] = g->color[2] = shade[side];
        glx_quad(g, q->p, (const float(*)[2])q->uv, NULL, tex, tw, th);
    }
}

/* RenderLightningBolt.doRender: texture and lighting off, GL_SRC_ALPHA,
 * GL_ONE, four nested triangle-strip shells per segment. */
static void draw_bolt(struct glx *g, const struct jval *e, double x, double y, double z)
{
    const char *s = json_str(json_get(e, "bolt"));
    if (!s) return;
    int64_t bolt = strtoll(s, NULL, 10);
    double v11[8], v12[8];
    double v13 = 0.0, v15 = 0.0;
    jrand r;
    jr_seed(&r, bolt);
    for (int i = 7; i >= 0; --i)
    {
        v11[i] = v13;
        v12[i] = v15;
        v13 += (double)(jr_int_n(&r, 11) - 5);
        v15 += (double)(jr_int_n(&r, 11) - 5);
    }
    g->lighting = 0;
    g->t.blend = 6;
    g->t.no_alpha = 1;
    for (int v45 = 0; v45 < 4; ++v45)
    {
        jrand r2;
        jr_seed(&r2, bolt);
        for (int v19 = 0; v19 < 3; ++v19)
        {
            int v20 = 7, v21 = 0;
            if (v19 > 0) v20 = 7 - v19;
            if (v19 > 0) v21 = v20 - 2;
            double v22 = v11[v20] - v13;
            double v24 = v12[v20] - v15;
            for (int v26 = v20; v26 >= v21; --v26)
            {
                double v27 = v22, v29 = v24;
                if (v19 == 0)
                {
                    v22 += (double)(jr_int_n(&r2, 11) - 5);
                    v24 += (double)(jr_int_n(&r2, 11) - 5);
                }
                else
                {
                    v22 += (double)(jr_int_n(&r2, 31) - 15);
                    v24 += (double)(jr_int_n(&r2, 31) - 15);
                }
                float v31 = 0.5f;
                g->color[0] = 0.9f * v31;
                g->color[1] = 0.9f * v31;
                g->color[2] = 1.0f * v31;
                g->color[3] = 0.3f;
                double v32 = 0.1 + (double)v45 * 0.2;
                if (v19 == 0) v32 *= (double)v26 * 0.1 + 1.0;
                double v34 = 0.1 + (double)v45 * 0.2;
                if (v19 == 0) v34 *= (double)(v26 - 1) * 0.1 + 1.0;
                float strip[10][3];
                for (int v36 = 0; v36 < 5; ++v36)
                {
                    double v37 = x + 0.5 - v32, v39 = z + 0.5 - v32;
                    if (v36 == 1 || v36 == 2) v37 += v32 * 2.0;
                    if (v36 == 2 || v36 == 3) v39 += v32 * 2.0;
                    double v41 = x + 0.5 - v34, v43 = z + 0.5 - v34;
                    if (v36 == 1 || v36 == 2) v41 += v34 * 2.0;
                    if (v36 == 2 || v36 == 3) v43 += v34 * 2.0;
                    strip[v36 * 2][0] = (float)(v41 + v22);
                    strip[v36 * 2][1] = (float)(y + (double)(v26 * 16));
                    strip[v36 * 2][2] = (float)(v43 + v24);
                    strip[v36 * 2 + 1][0] = (float)(v37 + v27);
                    strip[v36 * 2 + 1][1] = (float)(y + (double)((v26 + 1) * 16));
                    strip[v36 * 2 + 1][2] = (float)(v39 + v29);
                }
                /* GL_TRIANGLE_STRIP: triangle i is (i, i+1, i+2), the odd ones
                 * with their first two swapped to keep the winding */
                for (int i = 0; i + 2 < 10; ++i)
                {
                    int a = i, b = i + 1, c = i + 2;
                    if (i & 1) { a = i + 1; b = i; }
                    const float p[4][3] = {{strip[a][0], strip[a][1], strip[a][2]},
                                           {strip[b][0], strip[b][1], strip[b][2]},
                                           {strip[c][0], strip[c][1], strip[c][2]},
                                           {strip[c][0], strip[c][1], strip[c][2]}};
                    const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
                    glx_quad(g, p, uv, NULL, glx_white, 1, 1);
                }
            }
        }
    }
}

void thing_fire(const struct entity_raster_target *t, const char *scene, float x, float y, float z,
                float width, float height, float fdy, float view_yaw, int brightness)
{
    const struct rb_uv *l0 = block_sprite(scene, "fire_layer_0");
    const struct rb_uv *l1 = block_sprite(scene, "fire_layer_1");
    int tw, th;
    const unsigned char *tex = block_atlas(scene, &tw, &th);
    if (!l0 || !l1 || !tex) return;
    struct glx g;
    glx_init(&g, t, brightness);
    g.lighting = 0;
    rs_gl_translate(g.mv, x, y, z);
    float v11 = width * 1.4f;
    rs_gl_scale(g.mv, v11, v11, v11);
    float v13 = 0.5f, v14 = 0.0f;
    float v15 = height / v11;
    float v16 = fdy;
    rs_gl_rotate(g.mv, -view_yaw, 0.0f, 1.0f, 0.0f);
    rs_gl_translate(g.mv, 0.0f, 0.0f, -0.3f + (float)(int)v15 * 0.02f);
    float v17 = 0.0f;
    int v18 = 0;
    while (v15 > 0.0f)
    {
        const struct rb_uv *ic = v18 % 2 == 0 ? l0 : l1;
        float v20 = ic->min_u, v21 = ic->min_v, v22 = ic->max_u, v23 = ic->max_v;
        if (v18 / 2 % 2 == 0)
        {
            float v24 = v22;
            v22 = v20;
            v20 = v24;
        }
        const float p[4][3] = {{v13 - v14, 0.0f - v16, v17}, {-v13 - v14, 0.0f - v16, v17},
                               {-v13 - v14, 1.4f - v16, v17}, {v13 - v14, 1.4f - v16, v17}};
        const float uv[4][2] = {{v22, v23}, {v20, v23}, {v20, v21}, {v22, v21}};
        glx_quad(&g, p, uv, NULL, tex, tw, th);
        v15 -= 0.45f;
        v16 -= 0.45f;
        v13 *= 0.9f;
        v17 += 0.03f;
        ++v18;
    }
}

static char *last_line(const char *path)
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

/* One entry of an "ents" list, in the view V. */
static void thing_one(const char *scene, const struct entity_raster_target *t, const struct jval *e,
                      const struct view *v)
{
    if (!ji(json_get(e, "vis"), 1)) return;
    const char *k = json_str(json_get(e, "k"));
    if (!k) return;
    double ex = jd(json_get(e, "x")), ey = jd(json_get(e, "y")), ez = jd(json_get(e, "z"));
    double rx = ex - t->cam[0], ry = ey - t->cam[1], rz = ez - t->cam[2];
    float x = (float)rx, y = (float)ry, z = (float)rz;
    struct glx g;
    glx_init(&g, t, ji(json_get(e, "brf"), 0));
    if (!strcmp(k, "arrow")) draw_arrow(&g, scene, e, x, y, z, v->pt);
    else if (!strcmp(k, "fireball")) draw_sprite(&g, scene, e, x, y, z, v, 2.0f);
    else if (!strcmp(k, "smallfireball")) draw_sprite(&g, scene, e, x, y, z, v, 0.5f);
    else if (!strcmp(k, "sprite")) draw_sprite(&g, scene, e, x, y, z, v, 0.5f);
    else if (!strcmp(k, "tnt")) draw_tnt(&g, scene, e, x, y, z, v->pt);
    else if (!strcmp(k, "falling")) draw_falling(&g, scene, e, x, y, z);
    else if (!strcmp(k, "bolt")) draw_bolt(&g, e, rx, ry, rz);
    if (ji(json_get(e, "burning"), 0))
        thing_fire(t, scene, x, y, z, jf(json_get(e, "width")), jf(json_get(e, "height")),
                   jf(json_get(e, "fdy")), v->yaw, 15728880);
}

void raster_things_scene(const char *scene, const struct entity_raster_target *t)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/state/frames.jsonl", scene);
    char *line = last_line(p);
    if (!line) return;
    struct jval *root = json_parse(line);
    if (!root) return;
    const struct jval *ents = json_get(root, "ents");
    if (!ents || !json_len(ents)) { json_free(root); return; }
    const struct jval *pl = json_get(root, "pl");
    struct view v;
    v.pt = json_get(root, "pt") ? jf(json_get(root, "pt")) : 1.0f;
    v.yaw = jf(json_get(pl, "pyaw")) + (jf(json_get(pl, "yaw")) - jf(json_get(pl, "pyaw"))) * v.pt;
    v.pitch = jf(json_get(pl, "ppit")) + (jf(json_get(pl, "pit")) - jf(json_get(pl, "ppit"))) * v.pt;
    int64_t now = 0;
    json_int(json_get(json_get(json_get(root, "hud"), "toast"), "now"), &now);
    v.now = now;
    for (int i = 0; i < json_len(ents); ++i) thing_one(scene, t, json_at(ents, i), &v);
    json_free(root);
}

void raster_things_live(const char *scene, const struct entity_raster_target *t, const struct jval *ents,
                        float view_yaw, float view_pitch, float pt, int64_t now)
{
    struct view v = {view_yaw, view_pitch, pt, now};
    for (int i = 0; i < json_len(ents); ++i) thing_one(scene, t, json_at(ents, i), &v);
}
