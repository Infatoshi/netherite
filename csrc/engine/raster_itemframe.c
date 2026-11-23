/* RenderItemFrame over a recorded scene: the frame (renderFrameItemAsBlock's
 * five boxes of RenderBlocks.renderBlockAsItem with the bounds locked and the
 * itemframe_background and birch plank sprites forced), then func_82402_b's
 * item as an EntityItem through RenderItem.doRender with renderInFrame set
 * (fast graphics: one flat quad, no billboard turn). A compass draws the
 * frame's own needle (TextureCompass.updateCompass(..., true) at the frame's
 * position and wrapAngleTo180(180 + direction * 90)) and then moves the
 * compass animation on by one updateAnimation, which the hand and the HUD
 * drawn after it show. Every matrix is built the way GL11's calls build it
 * (renderstate.c's Mesa matrix ops) on the recorded modelview.
 *
 * RenderGlobal.renderEntities draws an entity only inside the frustum
 * (Frustrum/ClippingHelperImpl over the recorded matrices, ported below); the
 * compass step follows that decision. Not drawn: a filled map's contents
 * (the map renderer is not ported; its frame is), block items (the 3D block
 * path), the enchant glint and a named stack's label. */
#define _POSIX_C_SOURCE 200809L
#include "raster_itemframe.h"
#include "raster_things.h"
#include "block_item_model.h"
#include "renderstate.h"
#include "tape.h"
#include "texanim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direction.offsetX / offsetZ */
static const int OFF_X[4] = {0, -1, 0, 1};
static const int OFF_Z[4] = {1, 0, -1, 0};

void raster_itemframe_parse(const nbt *t, struct raster_itemframe *f)
{
    memset(f, 0, sizeof *f);
    f->tile[0] = (int)nbt_int_value(nbt_get(t, "TileX"));
    f->tile[1] = (int)nbt_int_value(nbt_get(t, "TileY"));
    f->tile[2] = (int)nbt_int_value(nbt_get(t, "TileZ"));
    const nbt *d = nbt_get(t, "Direction");
    f->dir = (int)(d ? nbt_int_value(d) : nbt_int_value(nbt_get(t, "Dir"))) & 3;
    const nbt *pos = nbt_get(t, "Pos");
    for (int i = 0; i < 3 && pos; ++i) {
        uint64_t b = nbt_double_bits(nbt_list_get(pos, i));
        memcpy(&f->pos[i], &b, 8);
    }
    const nbt *rot = nbt_get(t, "Rotation");
    if (rot) {
        uint32_t b = nbt_float_bits(nbt_list_get(rot, 0));
        memcpy(&f->yaw, &b, 4);
    }
    const nbt *item = nbt_get(t, "Item");
    if (item && nbt_get(item, "id")) {
        f->item_id = (int)nbt_int_value(nbt_get(item, "id"));
        f->item_damage = (int)nbt_int_value(nbt_get(item, "Damage"));
        f->item_rot = (int)nbt_int_value(nbt_get(t, "ItemRotation")) & 3;
    }
    /* EntityHanging.setDirection's bounding box, 9 pixels wide and high */
    float w = 9.0f, hgt = 9.0f, dep = 9.0f;
    if (f->dir != 2 && f->dir != 0) w = 0.5f; else dep = 0.5f;
    w /= 32.0f; hgt /= 32.0f; dep /= 32.0f;
    float x = (float)f->tile[0] + 0.5f, y = (float)f->tile[1] + 0.5f, z = (float)f->tile[2] + 0.5f;
    if (f->dir == 2) z -= 0.5625f;
    if (f->dir == 1) x -= 0.5625f;
    if (f->dir == 0) z += 0.5625f;
    if (f->dir == 3) x += 0.5625f;
    float e = -0.03125f;
    f->box[0] = (double)(x - w - e); f->box[1] = (double)(y - hgt - e); f->box[2] = (double)(z - dep - e);
    f->box[3] = (double)(x + w + e); f->box[4] = (double)(y + hgt + e); f->box[5] = (double)(z + dep + e);
}

/* ClippingHelperImpl.init over the recorded matrices, then
 * ClippingHelper.isBoxInFrustum relative to the Frustrum's position. */
int raster_itemframe_in_frustum(const float proj[16], const float mv[16], const double cam[3],
                                const double box[6])
{
    float c[16], fr[6][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            c[i * 4 + j] = mv[i * 4 + 0] * proj[0 + j] + mv[i * 4 + 1] * proj[4 + j] +
                           mv[i * 4 + 2] * proj[8 + j] + mv[i * 4 + 3] * proj[12 + j];
    static const int col[6] = {0, 0, 1, 1, 2, 2};
    static const float sgn[6] = {-1, 1, 1, -1, -1, 1};
    for (int p = 0; p < 6; ++p) {
        for (int k = 0; k < 4; ++k) {
            float a = c[k * 4 + 3], b = c[k * 4 + col[p]];
            fr[p][k] = sgn[p] < 0 ? a - b : a + b;
        }
        float len = (float)sqrt((double)(fr[p][0] * fr[p][0] + fr[p][1] * fr[p][1] + fr[p][2] * fr[p][2]));
        for (int k = 0; k < 4; ++k) fr[p][k] /= len;
    }
    double x0 = box[0] - cam[0], y0 = box[1] - cam[1], z0 = box[2] - cam[2];
    double x1 = box[3] - cam[0], y1 = box[4] - cam[1], z1 = box[5] - cam[2];
    for (int p = 0; p < 6; ++p) {
        const double a = fr[p][0], b = fr[p][1], cc = fr[p][2], d = fr[p][3];
        if (a * x0 + b * y0 + cc * z0 + d <= 0.0 && a * x1 + b * y0 + cc * z0 + d <= 0.0 &&
            a * x0 + b * y1 + cc * z0 + d <= 0.0 && a * x1 + b * y1 + cc * z0 + d <= 0.0 &&
            a * x0 + b * y0 + cc * z1 + d <= 0.0 && a * x1 + b * y0 + cc * z1 + d <= 0.0 &&
            a * x0 + b * y1 + cc * z1 + d <= 0.0 && a * x1 + b * y1 + cc * z1 + d <= 0.0)
            return 0;
    }
    return 1;
}

/* RenderGlobal.renderEntities' gate: isInRangeToRender3d (an item frame's
 * range is 16 * 64 blocks) and the frustum. */
int raster_itemframe_rendered(const struct raster_itemframe *f, const float proj[16],
                              const float mv[16], const double cam[3])
{
    double dx = f->pos[0] - cam[0], dy = f->pos[1] - cam[1], dz = f->pos[2] - cam[2];
    double range = 16.0 * 64.0;
    if (!(dx * dx + dy * dy + dz * dz < range * range)) return 0;
    return raster_itemframe_in_frustum(proj, mv, cam, f->box);
}

/* The compass world RenderItemFrame hands updateCompass, and the one the
 * animation step after it reads (the player at frame time). */
static void dial_world(const char *scene, struct texanim_world *w, const struct raster_itemframe *f)
{
    memset(w, 0, sizeof *w);
    char p[1200];
    snprintf(p, sizeof p, "%s/state/frames.jsonl", scene);
    FILE *fp = fopen(p, "rb");
    if (!fp) return;
    struct lines in;
    lines_init(&in);
    lines_file(&in, fp);
    const char *line = lines_next(&in);
    struct jval *row = line ? json_parse(strdup(line)) : NULL;
    lines_free(&in);
    fclose(fp);
    if (!row) return;
    const struct jval *anim = json_get(row, "anim"), *pl = json_get(row, "pl");
    int64_t n = 0;
    w->world = 1;
    json_int(json_get(anim, "surface"), &n); w->surface = (int)n;
    json_int(json_at(json_get(anim, "spawn"), 0), &n); w->spawn_x = (int)n;
    json_int(json_at(json_get(anim, "spawn"), 2), &n); w->spawn_z = (int)n;
    uint64_t b = 0;
    uint32_t fb = 0;
    if (f) {
        w->px = f->pos[0];
        w->pz = f->pos[2];
        /* MathHelper.wrapAngleTo180_float((float)(180 + direction * 90)) */
        float a = fmodf((float)(180 + f->dir * 90), 360.0f);
        if (a >= 180.0f) a -= 360.0f;
        if (a < -180.0f) a += 360.0f;
        w->yaw = (double)a;
    } else {
        if (json_double(json_get(pl, "x"), &b)) memcpy(&w->px, &b, 8);
        if (json_double(json_get(pl, "z"), &b)) memcpy(&w->pz, &b, 8);
        float yaw = 0;
        if (json_float(json_get(pl, "yaw"), &fb)) memcpy(&yaw, &fb, 4);
        w->yaw = (double)yaw;
    }
    json_free(row);
}

int raster_itemframe_compass(const char *scene, const struct raster_itemframe *f)
{
    const struct texanim *a;
    struct texanim_state *st;
    if (!texanim_scene(scene, &a, &st)) return -1;
    for (int i = 0; i < a->n[TEXANIM_ITEMS]; ++i)
        if (a->s[TEXANIM_ITEMS][i].kind == TEXANIM_COMPASS) {
            struct texanim_world w;
            dial_world(scene, &w, f);
            return texanim_compass_frame(&w, a->s[TEXANIM_ITEMS][i].ndata);
        }
    return -1;
}

void raster_itemframe_compass_step(const char *scene)
{
    const struct texanim *a;
    struct texanim_state *st;
    if (!texanim_scene(scene, &a, &st)) return;
    struct texanim_world w;
    dial_world(scene, &w, NULL);
    texanim_compass_render_step(a, st, &w, NULL);
}

/* ------------------------------------------------------------ drawing */

static struct entity_clip_vertex vtx(const struct entity_raster_target *t, const float m[16],
                                     float x, float y, float z, float u, float v,
                                     float lb, float ls, float diffuse)
{
    struct entity_clip_vertex q = {0};
    float eye[4];
    for (int r = 0; r < 4; ++r) eye[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
    for (int r = 0; r < 4; ++r)
        q.clip[r] = t->proj[r] * eye[0] + t->proj[4 + r] * eye[1] + t->proj[8 + r] * eye[2] +
                    t->proj[12 + r] * eye[3];
    q.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    q.u = u; q.v = v;
    q.light[0] = lb; q.light[1] = ls;
    q.diffuse = diffuse;
    q.color[0] = q.color[1] = q.color[2] = q.color[3] = 1.0f;
    return q;
}

/* A normal through a rotation-only matrix's upper 3x3 (column major). */
static void turn(const float r[16], const float n[3], float out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = r[i] * n[0] + r[4 + i] * n[1] + r[8 + i] * n[2];
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 0) for (int i = 0; i < 3; ++i) out[i] /= len;
}

static unsigned char *read_rgba(const char *scene, const char *name, size_t n)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", scene, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    unsigned char *b = malloc(n);
    if (b && fread(b, 1, n, f) != n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static struct jval *read_json(const char *scene, const char *name)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", scene, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    return json_parse(b);
}

/* One renderBlockAsItem box: glRotatef(90, 0, 1, 0), glTranslatef(-0.5,
 * -0.5, -0.5) on top of M, the six faces each with its own normal. */
static void frame_box(const struct entity_raster_target *t, const char *scene, const float m0[16],
                      const float turn0[16], const char *icon, const double b[6],
                      const unsigned char *atlas, int aw, int ah, float lb, float ls)
{
    struct block_item_model model;
    memset(&model, 0, sizeof model);
    if (block_item_model_override_box(scene, icon, b[0], b[1], b[2], b[3], b[4], b[5], &model)) return;
    float m[16], r[16];
    memcpy(m, m0, sizeof m);
    memcpy(r, turn0, sizeof r);
    rs_gl_rotate(m, 90.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(r, 90.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_translate(m, -0.5f, -0.5f, -0.5f);
    for (int i = 0; i < model.n; ++i) {
        const struct block_item_quad *q = &model.quad[i];
        float n[3];
        turn(r, q->normal, n);
        float d = raster_entity_diffuse(n[0], n[1], n[2]);
        struct entity_clip_vertex v[4];
        for (int k = 0; k < 4; ++k)
            v[k] = vtx(t, m, q->p[k][0], q->p[k][1], q->p[k][2], q->uv[k][0], q->uv[k][1], lb, ls, d);
        raster_entity_quad(t, atlas, aw, ah, v);
    }
}

/* The item table's entry for (id, damage): sprite sheet, tint and uv. */
static int item_icon(const struct jval *table, int id, int damage, int *sprite, int *tint, int *mode,
                     float uv[4])
{
    const struct jval *list = json_at(table, id);
    if (!list || json_len(list) == 0) return 0;
    const struct jval *e = NULL;
    for (int i = 0; i < json_len(list); ++i) {
        int64_t d = 0;
        json_int(json_get(json_at(list, i), "dmg"), &d);
        if (d == damage) { e = json_at(list, i); break; }
    }
    if (!e) e = json_at(list, 0);
    int64_t n = 0;
    *sprite = json_int(json_get(e, "sprite"), &n) ? (int)n : 1;
    *tint = json_int(json_get(e, "tint"), &n) ? (int)n : 0xffffff;
    *mode = json_int(json_get(e, "mode"), &n) ? (int)n : 0;
    const char *k[4] = {"minU", "maxU", "minV", "maxV"};
    for (int i = 0; i < 4; ++i) {
        uint32_t b = 0;
        if (!json_float(json_get(e, k[i]), &b)) return 0;
        memcpy(&uv[i], &b, 4);
    }
    return 1;
}

/* An item table entry's mode (1: a block drawn as its 3D model) and tint,
 * without the icon's uv a block model entry lacks. */
static int item_mode(const struct jval *table, int id, int damage, int *tint)
{
    const struct jval *list = json_at(table, id);
    if (!list || json_len(list) == 0) return -1;
    const struct jval *e = NULL;
    for (int i = 0; i < json_len(list); ++i) {
        int64_t d = 0;
        json_int(json_get(json_at(list, i), "dmg"), &d);
        if (d == damage) { e = json_at(list, i); break; }
    }
    if (!e) e = json_at(list, 0);
    int64_t n = 0;
    *tint = json_int(json_get(e, "tint"), &n) ? (int)n : 0xffffff;
    return json_int(json_get(e, "mode"), &n) ? (int)n : 0;
}

/* The frame's five boxes and, with ITEMS (sprite sheet IW x IH, its uv and
 * tint), func_82402_b's item quad, lit at (LB, LS); with BLOCK_ICON_SCENE a
 * block item instead (RenderItem.doRender's 3D path under renderInFrame). */
static void frame_draw(const char *scene, const struct entity_raster_target *t0,
                       const struct raster_itemframe *f, float lb, float ls,
                       const unsigned char *blocks, int aw, int ah,
                       const unsigned char *tex, int tw, int th, int tint, const float uv[4],
                       const char *block_icon_scene)
{
    struct entity_raster_target t = *t0;
    t.material[0] = t.material[1] = t.material[2] = 1.0f;

    /* doRender: glTranslated(tile + offset - (pos - x - 0.5)) with x = pos - renderPos */
    double rx = f->pos[0] - t.cam[0], ry = f->pos[1] - t.cam[1], rz = f->pos[2] - t.cam[2];
    double v10 = f->pos[0] - rx - 0.5, v12 = f->pos[1] - ry - 0.5, v14 = f->pos[2] - rz - 0.5;
    int tx = f->tile[0] + OFF_X[f->dir], ty = f->tile[1], tz = f->tile[2] + OFF_Z[f->dir];
    float base[16], rot[16];
    memcpy(base, t.mv, sizeof base);
    rs_gl_translate(base, (float)((double)tx - v10), (float)((double)ty - v12), (float)((double)tz - v14));

    /* renderFrameItemAsBlock (or func_147915_b for a map: a full-size frame) */
    int map = f->item_id == 358;
    float m[16];
    memcpy(m, base, sizeof m);
    rs_gl_rotate(m, f->yaw, 0.0f, 1.0f, 0.0f);
    rs_gl_identity(rot);
    rs_gl_rotate(rot, f->yaw, 0.0f, 1.0f, 0.0f);
    float v3 = 0.0625f, v4 = map ? 1.0f : 0.75f, v5 = v4 / 2.0f;
    double bg[6] = {0.0, (double)(0.5f - v5 + 0.0625f), (double)(0.5f - v5 + 0.0625f),
                    (double)(map ? v3 : v3 * 0.5f), (double)(0.5f + v5 - 0.0625f), (double)(0.5f + v5 - 0.0625f)};
    frame_box(&t, scene, m, rot, "itemframe_background", bg, blocks, aw, ah, lb, ls);
    const double sides[4][6] = {
        {0.0, (double)(0.5f - v5), (double)(0.5f - v5), (double)(v3 + 1.0E-4f), (double)(v3 + 0.5f - v5), (double)(0.5f + v5)},
        {0.0, (double)(0.5f + v5 - v3), (double)(0.5f - v5), (double)(v3 + 1.0E-4f), (double)(0.5f + v5), (double)(0.5f + v5)},
        {0.0, (double)(0.5f - v5), (double)(0.5f - v5), (double)v3, (double)(0.5f + v5), (double)(v3 + 0.5f - v5)},
        {0.0, (double)(0.5f - v5), (double)(0.5f + v5 - v3), (double)v3, (double)(0.5f + v5), (double)(0.5f + v5)}};
    for (int i = 0; i < 4; ++i)
        frame_box(&t, scene, m, rot, "planks_birch", sides[i], blocks, aw, ah, lb, ls);

    if (!tex && !block_icon_scene) return;
    float mi[16], ri[16];
    memcpy(mi, base, sizeof mi);
    rs_gl_identity(ri);
    rs_gl_translate(mi, -0.453125f * (float)OFF_X[f->dir], -0.18f, -0.453125f * (float)OFF_Z[f->dir]);
    rs_gl_rotate(mi, 180.0f + f->yaw, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(ri, 180.0f + f->yaw, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(mi, (float)(-90 * f->item_rot), 0.0f, 0.0f, 1.0f);
    rs_gl_rotate(ri, (float)(-90 * f->item_rot), 0.0f, 0.0f, 1.0f);
    if (f->item_rot == 1) rs_gl_translate(mi, -0.16f, -0.16f, 0.0f);
    if (f->item_rot == 2) rs_gl_translate(mi, 0.0f, -0.32f, 0.0f);
    if (f->item_rot == 3) rs_gl_translate(mi, 0.16f, -0.16f, 0.0f);
    /* RenderItem.doRender at (0, 0, 0), age 0, hoverStart 0, pt 0:
     * the bob is sin(0) * 0.1F + 0.1F */
    rs_gl_translate(mi, 0.0f, 0.1f, 0.0f);
    if (block_icon_scene)
    {
        /* the 3D block: the spin (0 at age 0), renderInFrame's 1.25 scale,
         * 0.05 lift and -90 turn, the 0.25 scale, one renderBlockAsItem at
         * brightness 1 (the stack is set to 1) */
        struct glx g;
        glx_init(&g, &t, (int)ls << 16 | (int)lb);
        memcpy(g.mv, mi, sizeof g.mv);
        rs_gl_scale(g.mv, 1.25f, 1.25f, 1.25f);
        rs_gl_translate(g.mv, 0.0f, 0.05f, 0.0f);
        rs_gl_rotate(g.mv, -90.0f, 0.0f, 1.0f, 0.0f);
        rs_gl_scale(g.mv, 0.25f, 0.25f, 0.25f);
        thing_block_as_item(&g, block_icon_scene, f->item_id, f->item_damage, tint, 1.0f);
        return;
    }
    rs_gl_scale(mi, 0.5128205f, 0.5128205f, 0.5128205f);
    rs_gl_translate(mi, 0.0f, -0.05f, 0.0f);
    /* renderDroppedItem's fast path: setNormal(0, 1, 0) */
    float n[3];
    turn(ri, (const float[3]){0.0f, 1.0f, 0.0f}, n);
    float d = raster_entity_diffuse(n[0], n[1], n[2]);
    t.material[0] = (float)(tint >> 16 & 255) / 255.0f;
    t.material[1] = (float)(tint >> 8 & 255) / 255.0f;
    t.material[2] = (float)(tint & 255) / 255.0f;
    struct entity_clip_vertex q[4] = {
        vtx(&t, mi, -0.5f, -0.25f, 0.0f, uv[0], uv[3], lb, ls, d),
        vtx(&t, mi, 0.5f, -0.25f, 0.0f, uv[1], uv[3], lb, ls, d),
        vtx(&t, mi, 0.5f, 0.75f, 0.0f, uv[1], uv[2], lb, ls, d),
        vtx(&t, mi, -0.5f, 0.75f, 0.0f, uv[0], uv[2], lb, ls, d)};
    raster_entity_quad(&t, tex, tw, th, q);
}

void raster_itemframe_draw(const char *scene, const struct entity_raster_target *t0,
                           const struct raster_itemframe *f, const struct rb_world *world,
                           int has_world)
{
    if (!raster_itemframe_rendered(f, t0->proj, t0->mv, t0->cam)) return;

    /* RenderManager.renderEntityStatic: the frame's brightness for the lightmap */
    float lb = 0.0f, ls = 240.0f;
    if (has_world) {
        double h = (f->box[4] - f->box[1]) * 0.66;
        int bx = (int)floor(f->pos[0]), by = (int)floor(f->pos[1] + h), bz = (int)floor(f->pos[2]);
        lb = (float)(rb_world_blocklight(world, bx, by, bz) << 4);
        ls = (float)(rb_world_sky(world, bx, by, bz) << 4);
    }

    struct jval *aj = read_json(scene, "atlas.json");
    int64_t aw = 0, ah = 0;
    json_int(json_get(aj, "atlas_width"), &aw);
    json_int(json_get(aj, "atlas_height"), &ah);
    json_free(aj);
    unsigned char *blocks = aw > 0 && ah > 0 ? read_rgba(scene, "atlas.rgba", (size_t)(aw * ah * 4)) : NULL;
    if (!blocks) return;
    texanim_patch_scene(scene, TEXANIM_BLOCKS, blocks, (int)aw, (int)ah);

    /* func_82402_b: the item, unless it is a map (whose contents are not drawn) */
    int map = f->item_id == 358;
    unsigned char *items = NULL;
    const unsigned char *tex = NULL;
    int tw = 0, th = 0, tint = 0xffffff, compass = 0;
    float uv[4] = {0};
    struct jval *table = f->item_id > 0 && !map ? read_json(scene, "state/item_table.json") : NULL;
    int sprite = 1, mode = 0;
    if (table && item_icon(table, f->item_id, f->item_damage, &sprite, &tint, &mode, uv) && mode != 1) {
        int iw = 256, ih = 256;
        if (sprite == 1) {
            items = read_rgba(scene, "state/gui_items.rgba", (size_t)iw * ih * 4);
            if (items) texanim_patch_scene(scene, TEXANIM_ITEMS, items, iw, ih);
        }
        tex = sprite == 1 ? items : blocks;
        tw = sprite == 1 ? iw : (int)aw;
        th = sprite == 1 ? ih : (int)ah;
        compass = f->item_id == 345;
        if (compass && items) {
            /* updateCompass(..., true): the frame's own needle */
            const struct texanim *a;
            struct texanim_state *st;
            int k = raster_itemframe_compass(scene, f);
            if (k >= 0 && texanim_scene(scene, &a, &st)) {
                struct texanim_state own = *st;
                for (int i = 0; i < a->n[TEXANIM_ITEMS]; ++i)
                    if (a->s[TEXANIM_ITEMS][i].kind == TEXANIM_COMPASS) own.fc[TEXANIM_ITEMS][i] = k;
                texanim_apply(a, &own, TEXANIM_ITEMS, items, iw, ih);
            }
        }
    }
    frame_draw(scene, t0, f, lb, ls, blocks, (int)aw, (int)ah, tex, tw, th, tint, uv, NULL);
    /* the compass's updateAnimation after the draw */
    if (compass) raster_itemframe_compass_step(scene);
    free(items);
    json_free(table);
    free(blocks);
}

void raster_itemframe_draw_live(const char *scene, const char *icon_scene, const struct entity_raster_target *t0,
                                const struct raster_itemframe *f, const struct rb_world *world,
                                const unsigned char *blocks, int aw, int ah)
{
    if (!blocks || !raster_itemframe_rendered(f, t0->proj, t0->mv, t0->cam)) return;
    float lb = 0.0f, ls = 240.0f;
    if (world) {
        double h = (f->box[4] - f->box[1]) * 0.66;
        int bx = (int)floor(f->pos[0]), by = (int)floor(f->pos[1] + h), bz = (int)floor(f->pos[2]);
        lb = (float)(rb_world_blocklight(world, bx, by, bz) << 4);
        ls = (float)(rb_world_sky(world, bx, by, bz) << 4);
    }
    unsigned char *items = NULL;
    const unsigned char *tex = NULL;
    int tw = 0, th = 0, tint = 0xffffff, sprite = 1, mode = 0;
    float uv[4] = {0};
    struct jval *table = f->item_id > 0 && f->item_id != 358 ? read_json(icon_scene, "state/item_table.json") : NULL;
    const char *block_scene = NULL;
    if (table && item_mode(table, f->item_id, f->item_damage, &tint) == 1)
        block_scene = scene;
    else if (table && item_icon(table, f->item_id, f->item_damage, &sprite, &tint, &mode, uv) && mode != 1) {
        if (sprite == 1) items = read_rgba(icon_scene, "state/gui_items.rgba", (size_t)256 * 256 * 4);
        tex = sprite == 1 ? items : blocks;
        tw = sprite == 1 ? 256 : aw;
        th = sprite == 1 ? 256 : ah;
    }
    frame_draw(scene, t0, f, lb, ls, blocks, aw, ah, tex, tw, th, tint, uv, block_scene);
    free(items);
    json_free(table);
}
