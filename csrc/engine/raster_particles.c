/* EffectRenderer.renderLitParticles and EffectRenderer.renderParticles over a
 * recorded scene or the live client's list, ported statement by statement.
 * See raster_particles.h for the contract. The recorded row's "fx" block
 * (RenderStateProbe.particles) is the exact particle list the frame draws; it
 * is read into the live client's struct (particles_live_from_json) so both
 * paths go through one draw: each subclass's renderParticle, the scale it
 * animates, the texture corners it picks and the colour it writes. Nothing
 * here re-rolls RNG or reads vanilla code at run time. */
#define _POSIX_C_SOURCE 200809L

#include "raster_particles.h"
#include "raster_entities.h"
#include "particles_live.h"

#include "raster_entity_quad.h"
#include "render_blocks.h"
#include "render_blocks_int.h"
#include "tape.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MathHelper.sin and cos: the SIN_TABLE jmath_init builds */
extern float MH_SIN[65536];
static float tsin(float f) { float v = f * 10430.378F; return MH_SIN[(v != v ? 0 : (int)v) & 65535]; }
static float tcos(float f) { float v = f * 10430.378F + 16384.0F; return MH_SIN[(v != v ? 0 : (int)v) & 65535]; }

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float fx_f(const struct jval *v, const char *key)
{
    uint32_t bits = 0;
    if (!json_float(json_get(v, key), &bits)) return 0.0f;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static double fx_d(const struct jval *v, const char *key)
{
    uint64_t bits = 0;
    if (!json_double(json_get(v, key), &bits)) return 0.0;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static double fx_da(const struct jval *a, int i)
{
    uint64_t bits = 0;
    if (!json_double(json_at(a, i), &bits)) return 0.0;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static int fx_i(const struct jval *v, const char *key)
{
    int64_t n = 0;
    if (!json_int(json_get(v, key), &n)) return 0;
    return (int)n;
}

static const char *fx_s(const struct jval *v, const char *key)
{
    return json_str(json_get(v, key));
}

int particles_live_from_json(const struct jval *m, struct live_fx *f, struct world *w)
{
    memset(f, 0, sizeof *f);
    entity_init(&f->e, w);
    f->e.can_trigger_walking = 0;
    const char *cls = fx_s(m, "cls");
    f->kind = particles_live_kind(cls);
    f->layer = fx_i(m, "layer");
    f->e.prev_pos_x = fx_d(m, "px");
    f->e.prev_pos_y = fx_d(m, "py");
    f->e.prev_pos_z = fx_d(m, "pz");
    f->e.pos_x = fx_d(m, "x");
    f->e.pos_y = fx_d(m, "y");
    f->e.pos_z = fx_d(m, "z");
    f->e.motion_x = fx_d(m, "mx");
    f->e.motion_y = fx_d(m, "my");
    f->e.motion_z = fx_d(m, "mz");
    f->age = fx_i(m, "age");
    f->maxage = fx_i(m, "maxage");
    f->scale = fx_f(m, "scale");
    f->red = fx_f(m, "red");
    f->green = fx_f(m, "green");
    f->blue = fx_f(m, "blue");
    f->alpha = fx_f(m, "alpha");
    f->jitx = fx_f(m, "jitx");
    f->jity = fx_f(m, "jity");
    f->tix = fx_i(m, "tix");
    f->tiy = fx_i(m, "tiy");
    f->gravity = fx_f(m, "gravity");
    f->brf = fx_i(m, "brf");
    if (fx_s(m, "icon"))
    {
        f->has_icon = 1;
        f->min_u = fx_f(m, "minU");
        f->max_u = fx_f(m, "maxU");
        f->min_v = fx_f(m, "minV");
        f->max_v = fx_f(m, "maxV");
    }
    f->tstart = fx_i(m, "tstart");
    f->tmax = fx_i(m, "tmax");
    f->vx = fx_i(m, "vx");
    f->vq = fx_i(m, "vq");
    f->vs = fx_f(m, "vs");
    switch (f->kind)
    {
    case PLIVE_SMOKE: f->base_scale = fx_f(m, "smscale"); break;
    case PLIVE_FLAME: f->base_scale = fx_f(m, "fscale"); break;
    case PLIVE_REDDUST: f->base_scale = fx_f(m, "rdscale"); break;
    case PLIVE_PORTAL: f->base_scale = fx_f(m, "pscale"); break;
    case PLIVE_ENCHANT: f->base_scale = fx_f(m, "escale"); break;
    case PLIVE_CRIT: f->base_scale = fx_f(m, "cscale"); break;
    case PLIVE_LAVA: f->base_scale = fx_f(m, "lscale"); break;
    /* noteParticleScale and particleScaleOverTime are not dumped: the
     * dump's scale is what renderParticle left, the whole of it once the
     * age and partial tick reach 1/32 of the life */
    case PLIVE_NOTE: case PLIVE_HEART: f->base_scale = f->scale; break;
    default: break;
    }
    f->home_x = fx_d(m, "ppx");
    f->home_y = fx_d(m, "ppy");
    f->home_z = fx_d(m, "ppz");
    f->spell_base = fx_i(m, "sbase");
    f->bob = fx_i(m, "bob");
    f->lava = fx_i(m, "lava");
    f->ent_id = fx_i(m, "ent");
    f->life = fx_i(m, "life");
    f->mlife = fx_i(m, "mlife");
    {
        const char *pn = fx_s(m, "pname");
        f->magic = pn && !strcmp(pn, "magicCrit");
    }
    const struct jval *bb = json_get(m, "bb");
    if (bb && json_len(bb) == 6)
    {
        f->e.bounding_box.min_x = fx_da(bb, 0);
        f->e.bounding_box.min_y = fx_da(bb, 1);
        f->e.bounding_box.min_z = fx_da(bb, 2);
        f->e.bounding_box.max_x = fx_da(bb, 3);
        f->e.bounding_box.max_y = fx_da(bb, 4);
        f->e.bounding_box.max_z = fx_da(bb, 5);
        f->e.width = fx_f(m, "w");
        f->e.height = fx_f(m, "h");
        f->e.y_offset = fx_f(m, "yo");
        f->e.y_size = fx_f(m, "ys");
        f->e.on_ground = (uint8_t)fx_i(m, "og");
        f->e.no_clip = (uint8_t)fx_i(m, "nc");
        f->dead = fx_i(m, "dead");
    }
    const char *rs = fx_s(m, "rs");
    if (rs)
    {
        uint64_t st = strtoull(rs, NULL, 16);
        f->rand.r.seed = st;
        f->rand.have_next_next_gaussian = 0;
    }
    return f->kind >= 0;
}

/* One particle vertex: the camera-relative position (the render view entity
 * is already subtracted) pushed through the frame's modelview and projection,
 * the way the Tessellator's quad vertices reach the rasterizer. light is the
 * packed setBrightness pair split the way the Tessellator stores it. */
static struct entity_clip_vertex fx_clip(const float proj[16], const float mv[16],
                                         float ex, float ey, float ez,
                                         float u, float v, float lb, float ls)
{
    struct entity_clip_vertex t = {0};

    t.u = u;
    t.v = v;
    t.light[0] = lb;
    t.light[1] = ls;
    t.diffuse = 1.0f;

    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = mv[r] * ex + mv[4 + r] * ey + mv[8 + r] * ez + mv[12 + r];

    for (int r = 0; r < 4; ++r)
        t.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] +
                    proj[8 + r] * eye[2] + proj[12 + r] * eye[3];

    t.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return t;
}

/* The particleScale each subclass's renderParticle sets before the base
 * draw, from the age and partial tick. */
static float render_scale(const struct live_fx *m, float pt)
{
    float var8;
    switch (m->kind)
    {
    case PLIVE_SMOKE: case PLIVE_REDDUST: case PLIVE_CRIT: case PLIVE_NOTE: case PLIVE_HEART:
        var8 = ((float)m->age + pt) / (float)m->maxage * 32.0F;
        if (var8 < 0.0F) var8 = 0.0F;
        if (var8 > 1.0F) var8 = 1.0F;
        return m->base_scale * var8;
    case PLIVE_FLAME:
        var8 = ((float)m->age + pt) / (float)m->maxage;
        return m->base_scale * (1.0F - var8 * var8 * 0.5F);
    case PLIVE_PORTAL:
        var8 = ((float)m->age + pt) / (float)m->maxage;
        var8 = 1.0F - var8;
        var8 *= var8;
        var8 = 1.0F - var8;
        return m->base_scale * var8;
    case PLIVE_LAVA:
        var8 = ((float)m->age + pt) / (float)m->maxage;
        return m->base_scale * (1.0F - var8 * var8);
    default:
        /* EntitySpellParticleFX computes its fade and never applies it */
        return m->scale;
    }
}

/* One renderParticle quad of a layers 0-2 particle, in the Tessellator's
 * vertex order. The caller has set the layer's texture. */
static void particle_quad(const struct live_fx *m, struct entity_raster_target *t,
                          const unsigned char *tex, int tex_w, int tex_h,
                          float interp_x, float interp_y, float interp_z,
                          float p3, float p4, float p5, float p6, float p7,
                          float pt)
{
    float var8, var9, var10, var11;
    /* EntityDiggingFX and EntityBreakingFX: a quarter of the icon at the
     * jitter, getInterpolatedU/V in float, and the corners start at u0 */
    int jitter = m->kind == PLIVE_DIGGING || m->kind == PLIVE_BLOCKDUST || m->kind == PLIVE_BREAKING;

    if (jitter)
    {
        var8 = ((float)m->tix + m->jitx / 4.0F) / 16.0F;
        var9 = var8 + 0.015609375F;
        var10 = ((float)m->tiy + m->jity / 4.0F) / 16.0F;
        var11 = var10 + 0.015609375F;
        if (m->has_icon)
        {
            float du = m->max_u - m->min_u, dv = m->max_v - m->min_v;
            var8 = m->min_u + du * (float)(double)(m->jitx / 4.0F * 16.0F) / 16.0F;
            var9 = m->min_u + du * (float)(double)((m->jitx + 1.0F) / 4.0F * 16.0F) / 16.0F;
            var10 = m->min_v + dv * ((float)(double)(m->jity / 4.0F * 16.0F) / 16.0F);
            var11 = m->min_v + dv * ((float)(double)((m->jity + 1.0F) / 4.0F * 16.0F) / 16.0F);
        }
    }
    else
    {
        var8 = (float)m->tix / 16.0F;
        var9 = var8 + 0.0624375F;
        var10 = (float)m->tiy / 16.0F;
        var11 = var10 + 0.0624375F;
        if (m->has_icon)
        {
            var8 = m->min_u; var9 = m->max_u;
            var10 = m->min_v; var11 = m->max_v;
        }
    }

    float var12 = 0.1F * render_scale(m, pt);
    float var13 = (float)(m->e.prev_pos_x + (m->e.pos_x - m->e.prev_pos_x) * (double)pt - (double)interp_x);
    float var14 = (float)(m->e.prev_pos_y + (m->e.pos_y - m->e.prev_pos_y) * (double)pt - (double)interp_y);
    float var15 = (float)(m->e.prev_pos_z + (m->e.pos_z - m->e.prev_pos_z) * (double)pt - (double)interp_z);

    float lb = (float)(m->brf & 65535), ls = (float)(m->brf >> 16);

    float hx = p3 * var12, hy = p4 * var12, hz = p5 * var12;
    float vy = p6 * var12, vz2 = p7 * var12;
    float ua = jitter ? var8 : var9, ub = jitter ? var9 : var8;

    struct entity_clip_vertex q[4];
    q[0] = fx_clip(t->proj, t->mv, var13 - hx - vy, var14 - hy, var15 - hz - vz2, ua, var11, lb, ls);
    q[1] = fx_clip(t->proj, t->mv, var13 - hx + vy, var14 + hy, var15 - hz + vz2, ua, var10, lb, ls);
    q[2] = fx_clip(t->proj, t->mv, var13 + hx + vy, var14 + hy, var15 + hz + vz2, ub, var10, lb, ls);
    q[3] = fx_clip(t->proj, t->mv, var13 + hx - vy, var14 - hy, var15 + hz - vz2, ub, var11, lb, ls);

    /* EntityFX writes setColorRGBA_F with particleAlpha; the digging and
     * breaking particles setColorOpaque_F (alpha 1) */
    t->alpha = jitter ? 1.0F : m->alpha;
    t->material[0] = m->red;
    t->material[1] = m->green;
    t->material[2] = m->blue;
    raster_entity_quad(t, tex, tex_w, tex_h, q);
}

/* The two passes over a list: renderLitParticles (layer 3) then
 * renderParticles (layers 0, 1, 2). tex[] is the four layer textures
 * (particles, block atlas, item atlas, the explosion sheet); icon_uv, when
 * set, fills a digging particle's icon from the live mesher. yaw and pitch
 * are the render view entity's raw rotation pair. */
static void draw_list(const struct live_fx *fx, int n, struct entity_raster_target *t,
                      const unsigned char *const tex[4], const int tex_wh[8],
                      const struct rb_mesher *mesher,
                      float interp_x, float interp_y, float interp_z,
                      float yaw, float pitch, float pt)
{
    /* ActiveRenderInfo.updateRenderInfo, first person: MathHelper's table
     * sin and cos of angle * (float)Math.PI / 180.0F */
    float rotationX = tcos(yaw * (float)M_PI / 180.0F);
    float rotationZ = tsin(yaw * (float)M_PI / 180.0F);
    float rotationYZ = -rotationZ * tsin(pitch * (float)M_PI / 180.0F);
    float rotationXY = rotationX * tsin(pitch * (float)M_PI / 180.0F);
    float rotationXZ = tcos(pitch * (float)M_PI / 180.0F);

    /* renderLitParticles' own basis, angle * 0.017453292F */
    float lit4 = tcos(yaw * 0.017453292F);
    float lit5 = tsin(yaw * 0.017453292F);
    float lit6 = -lit5 * tsin(pitch * 0.017453292F);
    float lit7 = lit4 * tsin(pitch * 0.017453292F);
    float lit8 = tcos(pitch * 0.017453292F);

    t->material[0] = t->material[1] = t->material[2] = 1.0f;
    t->blend = 0;
    /* the particle passes' alpha test: drawBlockDamageTexture re-enabled
     * GL_GREATER 0.1, so the lit pass's ref is 25 (0.098); renderParticles
     * sets its own 1/255 below. The lightmap stays on for layer 3: each
     * quad's brightness comes through its light[] pair, like the entity
     * pass. */
    t->alpha_cut = 25;

    /* renderLitParticles (layer 3): EntityLargeExplodeFX renders itself with
     * its own sheet, opaque with depth writes, per-quad brightness fixed at
     * 240 by setBrightness, colour (red, green, blue, 1). EntityCrit2FX's
     * renderParticle is empty. */
    for (int i = 0; i < n && tex[3]; ++i)
    {
        const struct live_fx *m = &fx[i];
        if (m->layer != 3 || m->kind != PLIVE_LARGE_EXPLODE) continue;
        int frame = (int)(((float)m->vx + pt) * 15.0F / (float)m->vq);
        if (frame > 15) continue;
        float u0 = (float)(frame % 4) / 4.0F;
        float u1 = u0 + 0.24975F;
        float v0 = (float)(frame / 4) / 4.0F;
        float v1 = v0 + 0.24975F;
        float size = 2.0F * m->vs;
        float var14 = (float)(m->e.prev_pos_x + (m->e.pos_x - m->e.prev_pos_x) * (double)pt - (double)interp_x);
        float var15 = (float)(m->e.prev_pos_y + (m->e.pos_y - m->e.prev_pos_y) * (double)pt - (double)interp_y);
        float var16 = (float)(m->e.prev_pos_z + (m->e.pos_z - m->e.prev_pos_z) * (double)pt - (double)interp_z);

        float hx = lit4 * size, hy = lit8 * size, hz = lit5 * size;
        float vy = lit6 * size, vz2 = lit7 * size;
        struct entity_clip_vertex q[4];
        q[0] = fx_clip(t->proj, t->mv, var14 - hx - vy, var15 - hy, var16 - hz - vz2, u1, v1, 240.0f, 0.0f);
        q[1] = fx_clip(t->proj, t->mv, var14 - hx + vy, var15 + hy, var16 - hz + vz2, u1, v0, 240.0f, 0.0f);
        q[2] = fx_clip(t->proj, t->mv, var14 + hx + vy, var15 + hy, var16 + hz + vz2, u0, v0, 240.0f, 0.0f);
        q[3] = fx_clip(t->proj, t->mv, var14 + hx - vy, var15 - hy, var16 + hz - vz2, u0, v1, 240.0f, 0.0f);

        t->material[0] = m->red;
        t->material[1] = m->green;
        t->material[2] = m->blue;
        t->alpha = 1.0f;
        raster_entity_quad(t, tex[3], tex_wh[6], tex_wh[7], q);
        t->material[0] = t->material[1] = t->material[2] = 1.0f;
    }

    /* renderParticles: layers 0, 1, 2 in order, each with its own texture,
     * blend on, depthMask false, GREATER 0.003921569 */
    for (int layer = 0; layer < 3; ++layer)
    {
        int count = 0;
        for (int i = 0; i < n; ++i)
            if (fx[i].layer == layer) ++count;
        if (!count || !tex[layer]) continue;

        t->blend = 1;
        t->alpha_cut = 1;
        for (int i = 0; i < n; ++i)
        {
            const struct live_fx *m = &fx[i];
            if (m->layer != layer) continue;
            /* EntityHugeExplodeFX.renderParticle is empty */
            if (m->kind == PLIVE_HUGE_EXPLODE || m->kind == PLIVE_CRIT2) continue;

            struct live_fx m2 = *m;
            if (mesher && (m2.kind == PLIVE_DIGGING || m2.kind == PLIVE_BLOCKDUST) && !m2.min_u && !m2.max_u)
            {
                /* the live client: the block atlas sprite particleIcon
                 * carries, table_icon's (id, meta, side 0) uv */
                const struct rb_uv *uv = table_icon(mesher, m2.block_id, m2.block_meta, 0);
                if (uv)
                {
                    m2.min_u = uv->min_u; m2.max_u = uv->max_u;
                    m2.min_v = uv->min_v; m2.max_v = uv->max_v;
                }
                else
                {
                    m2.has_icon = 0;
                }
            }
            particle_quad(&m2, t, tex[layer], tex_wh[layer * 2], tex_wh[layer * 2 + 1],
                          interp_x, interp_y, interp_z,
                          rotationX, rotationXZ, rotationZ, rotationYZ, rotationXY, pt);
        }
        t->blend = 0;
    }
}

/* The texture one layer draws with, from the scene's dumps: layer 0 the
 * particle atlas (gui_particles), layer 1 the block atlas, layer 2 the item
 * atlas, layer 3 the explosion sheet. */
static unsigned char *layer_texture(const char *dir, int layer, int *tw, int *th)
{
    char p[1024];
    if (layer == 0)
    {
        *tw = 128; *th = 128;
        snprintf(p, sizeof p, "%s/state/gui_particles.rgba", dir);
    }
    else if (layer == 1)
    {
        snprintf(p, sizeof p, "%s/atlas.json", dir);
        FILE *jf = fopen(p, "rb");
        if (!jf) return NULL;
        fseek(jf, 0, SEEK_END);
        long n = ftell(jf);
        fseek(jf, 0, SEEK_SET);
        char *buf = malloc((size_t)n + 1);
        if (!buf) { fclose(jf); return NULL; }
        if (fread(buf, 1, (size_t)n, jf) != (size_t)n) { free(buf); fclose(jf); return NULL; }
        buf[n] = 0;
        fclose(jf);
        struct jval *root = json_parse(buf);
        if (!root) return NULL;   /* json_parse owns buf and frees it on error */
        int64_t aw = 0, ah = 0;
        json_int(json_get(root, "atlas_width"), &aw);
        json_int(json_get(root, "atlas_height"), &ah);
        json_free(root);          /* frees buf too */
        if (aw <= 0 || ah <= 0) return NULL;
        *tw = (int)aw; *th = (int)ah;
        snprintf(p, sizeof p, "%s/atlas.rgba", dir);
    }
    else if (layer == 2)
    {
        *tw = 256; *th = 256;
        snprintf(p, sizeof p, "%s/state/gui_items.rgba", dir);
    }
    else
    {
        *tw = 128; *th = 128;
        snprintf(p, sizeof p, "%s/state/gui_explosion.rgba", dir);
    }
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    unsigned char *b = malloc((size_t)*tw * *th * 4);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)*tw * *th * 4, f) != (size_t)*tw * *th * 4)
    {
        free(b); fclose(f); return NULL;
    }
    fclose(f);
    return b;
}

int raster_particles_scene(const char *scene, int w, int h,
                           const float proj[16], const float mv[16],
                           const double cam[3], const float fog[3],
                           float fogs, float foge, float fogd, int fogm,
                           const uint32_t lm[256], float *depth, unsigned char *rgb,
                           unsigned long *triangles, unsigned long *samples)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char *last = NULL;
    {
        struct lines lines;
        lines_init(&lines);
        lines_file(&lines, f);
        for (const char *line = lines_next(&lines); line; line = lines_next(&lines))
        {
            free(last);
            last = strdup(line);
        }
        lines_free(&lines);
    }
    fclose(f);
    if (!last) return 0;
    struct jval *root = json_parse(last);
    if (!root) return 0;   /* json_parse owns last and frees it on error */

    const struct jval *fx = json_get(root, "fx");
    if (!fx || !json_len(fx))
    {
        json_free(root);   /* owns and frees last */
        return 0;
    }

    const struct jval *pl = json_get(root, "pl");
    float pt = fx_f(root, "pt");
    if (pt == 0.0f && !json_get(root, "pt")) pt = 1.0f;

    /* EntityFX.interpPosX/Y/Z: the render view entity interpolated by pt,
     * recomputed in renderParticles (layers 0-2 use this frame's). */
    float interp_x = (float)(fx_d(pl, "px") + (fx_d(pl, "x") - fx_d(pl, "px")) * (double)pt);
    float interp_y = (float)(fx_d(pl, "py") + (fx_d(pl, "y") - fx_d(pl, "py")) * (double)pt);
    float interp_z = (float)(fx_d(pl, "pz") + (fx_d(pl, "z") - fx_d(pl, "pz")) * (double)pt);

    int n = json_len(fx);
    struct live_fx *list = calloc((size_t)n, sizeof *list);
    int k = 0;
    for (int i = 0; list && i < n; ++i)
        if (particles_live_from_json(json_at(fx, i), &list[k], NULL) || 1) ++k;

    int wh[8] = {0};
    unsigned char *tex[4];
    for (int layer = 0; layer < 4; ++layer)
    {
        int used = 0;
        for (int i = 0; i < k; ++i) used |= list[i].layer == layer;
        tex[layer] = used ? layer_texture(scene, layer, &wh[layer * 2], &wh[layer * 2 + 1]) : NULL;
    }

    struct entity_raster_target t = {0};
    t.w = w; t.h = h;
    t.proj = proj; t.mv = mv; t.cam = cam;
    t.fog = fog; t.fogs = fogs; t.foge = foge; t.fogd = fogd; t.fogm = fogm;
    t.lm = lm; t.depth = depth; t.rgb = rgb;
    t.triangles = triangles; t.samples = samples;

    /* EntityPickupFX (layer 3): the collected item or orb drawn by its own
     * renderer, moved from where it was toward the collector's position plus
     * yOffs by ((age + pt) / maxAge)^2. */
    {
        double ipx = fx_d(pl, "px") + (fx_d(pl, "x") - fx_d(pl, "px")) * (double)pt;
        double ipy = fx_d(pl, "py") + (fx_d(pl, "y") - fx_d(pl, "py")) * (double)pt;
        double ipz = fx_d(pl, "pz") + (fx_d(pl, "z") - fx_d(pl, "pz")) * (double)pt;
        for (int i = 0; i < n; ++i)
        {
            const struct jval *m = json_at(fx, i);
            const char *cls = fx_s(m, "cls");
            const struct jval *ent = json_get(m, "ent");
            if (!cls || strcmp(cls, "EntityPickupFX") || !ent) continue;
            float v8 = ((float)fx_i(m, "page") + pt) / (float)fx_i(m, "pmax");
            v8 *= v8;
            double v9 = fx_d(m, "ix"), v11 = fx_d(m, "iy"), v13 = fx_d(m, "iz");
            double v15 = fx_d(m, "bx"), v17 = fx_d(m, "by") + (double)fx_f(m, "yoffs"), v19 = fx_d(m, "bz");
            double v21 = v9 + (v15 - v9) * (double)v8;
            double v23 = v11 + (v17 - v11) * (double)v8;
            double v25 = v13 + (v19 - v13) * (double)v8;
            /* func_147940_a gets (double)(float)(position - interpPos); the
             * renderers subtract the camera again */
            double x = cam[0] + (double)(float)(v21 - ipx);
            double y = cam[1] + (double)(float)(v23 - ipy);
            double z = cam[2] + (double)(float)(v25 - ipz);
            const char *k = fx_s(ent, "k");
            if (k && !strcmp(k, "item"))
                raster_world_item_at(scene, &t, ent, x, y, z);
            else if (k && !strcmp(k, "orb"))
            {
                unsigned char *orb = NULL;
                char op[1100];
                snprintf(op, sizeof op, "%s/state/gui_orb.rgba", scene);
                FILE *of = fopen(op, "rb");
                if (of)
                {
                    orb = malloc(64 * 64 * 4);
                    if (orb && fread(orb, 1, 64 * 64 * 4, of) != 64 * 64 * 4) { free(orb); orb = NULL; }
                    fclose(of);
                }
                if (orb)
                {
                    float yaw = fx_f(pl, "pyaw") + (fx_f(pl, "yaw") - fx_f(pl, "pyaw")) * pt;
                    float pitch = fx_f(pl, "ppit") + (fx_f(pl, "pit") - fx_f(pl, "ppit")) * pt;
                    raster_orb_quad(&t, orb, 64, 64, x, y, z, fx_i(ent, "value"), fx_i(ent, "color"),
                                    pt, pitch, yaw, fx_i(ent, "light"));
                    free(orb);
                }
            }
        }
    }

    if (list)
        draw_list(list, k, &t, (const unsigned char *const *)tex, wh, NULL,
                  interp_x, interp_y, interp_z, fx_f(pl, "yaw"), fx_f(pl, "pit"), pt);

    for (int layer = 0; layer < 4; ++layer) free(tex[layer]);
    free(list);
    json_free(root);   /* owns and frees last */
    return 1;
}

int raster_particles_draw_live(const struct live_fx *fx, int n,
                               int w, int h, const float proj[16], const float mv[16],
                               const double cam[3], const float fog[3],
                               float fogs, float foge, float fogd, int fogm,
                               const uint32_t lm[256], float *depth, unsigned char *rgb,
                               unsigned long *triangles, unsigned long *samples,
                               const unsigned char *const tex[4], const int tex_wh[8],
                               const struct rb_mesher *mesher,
                               float pt, float yaw, float pitch)
{
    if (!n) return 0;

    struct entity_raster_target t = {0};
    t.w = w; t.h = h;
    t.proj = proj; t.mv = mv; t.cam = cam;
    t.fog = fog; t.fogs = fogs; t.foge = foge; t.fogd = fogd; t.fogm = fogm;
    t.lm = lm; t.depth = depth; t.rgb = rgb;
    t.triangles = triangles; t.samples = samples;

    /* EntityFX.interpPosX/Y/Z at this frame's partial tick */
    draw_list(fx, n, &t, tex, tex_wh, mesher, (float)cam[0], (float)cam[1], (float)cam[2], yaw, pitch, pt);
    return 1;
}
