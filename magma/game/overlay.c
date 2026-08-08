/* game/overlay.c - selection outline + dig crack decal geometry (see overlay.h). */
#include "game/overlay.h"
#include "game/hud.h"
#include "game/runtime.h"
#include "assets/loading_bg.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "assets/blockmodels.h"  /* bm_sprite_uv */
#include "assets/atlas_gen.h"    /* CR_SPRITE_* indices */

static int outline_clamp(int value, int upper) {
    if (value < 0) return 0;
    if (value >= upper) return upper - 1;
    return value;
}

static unsigned char outline_avg5(int sum) {
    return (unsigned char)((sum + 2) / 5);
}

void gm_overlay_entity_outline_build(int w, int h, CrRgba *mask,
                                     CrRgba *scratch) {
    if (!mask || !scratch || w <= 0 || h <= 0)
        return;

    /* entity_sobel.fsh. Outline mode replaces model RGB with the unteamed
     * color white while retaining texture alpha, so only mask alpha is needed
     * here. Each shader pass writes an RGBA8 framebuffer and therefore
     * quantizes before the next pass. */
    for (int y = 0; y < h; ++y) {
        int ym = outline_clamp(y - 1, h), yp = outline_clamp(y + 1, h);
        for (int x = 0; x < w; ++x) {
            int xm = outline_clamp(x - 1, w), xp = outline_clamp(x + 1, w);
            int c = mask[y * w + x].a;
            int l = mask[y * w + xm].a;
            int r = mask[y * w + xp].a;
            int u = mask[ym * w + x].a;
            int d = mask[yp * w + x].a;
            int rgb = outline_avg5(c + l + r + u + d);
            int alpha = abs(c - l) + abs(c - r)
                + abs(c - u) + abs(c - d);
            if (alpha > 255) alpha = 255;
            scratch[y * w + x] = (CrRgba){
                (u8)rgb, (u8)rgb, (u8)rgb, (u8)alpha};
        }
    }

    /* blur.fsh, BlurDir=(1,0), Radius=2. RGB is the five-sample average;
     * alpha is the clamped sum (the source's totalStrength is unused). */
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int dx = -2; dx <= 2; ++dx) {
                CrRgba p = scratch[y * w + outline_clamp(x + dx, w)];
                sr += p.r; sg += p.g; sb += p.b; sa += p.a;
            }
            if (sa > 255) sa = 255;
            mask[y * w + x] = (CrRgba){
                outline_avg5(sr), outline_avg5(sg), outline_avg5(sb),
                (u8)sa};
        }

    /* blur.fsh, BlurDir=(0,1). */
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                CrRgba p = mask[outline_clamp(y + dy, h) * w + x];
                sr += p.r; sg += p.g; sb += p.b; sa += p.a;
            }
            if (sa > 255) sa = 255;
            scratch[y * w + x] = (CrRgba){
                outline_avg5(sr), outline_avg5(sg), outline_avg5(sb),
                (u8)sa};
        }

    /* Shader.loadShader clears every outtarget before drawing. blit.fsh
     * therefore writes swap onto transparent final with src-alpha blending,
     * multiplying RGB and alpha by the vertical result's alpha. */
    for (int i = 0; i < w * h; ++i) {
        CrRgba src = scratch[i];
        int a = src.a;
        scratch[i].r = (u8)((src.r * a + 127) / 255);
        scratch[i].g = (u8)((src.g * a + 127) / 255);
        scratch[i].b = (u8)((src.b * a + 127) / 255);
        scratch[i].a = (u8)((src.a * a + 127) / 255);
    }
}

void gm_overlay_entity_outline_composite(CrFramebuffer *fb,
                                         const CrRgba *outline) {
    if (!fb || !fb->color || !outline || fb->w <= 0 || fb->h <= 0)
        return;
    /* Framebuffer.framebufferRenderExt with separate blend factors preserves
     * destination alpha. Magma output is RGB-owned, so leave it untouched. */
    for (int i = 0; i < fb->w * fb->h; ++i) {
        CrRgba src = outline[i], dst = fb->color[i];
        int a = src.a, inv = 255 - a;
        fb->color[i].r = (u8)((src.r * a + dst.r * inv + 127) / 255);
        fb->color[i].g = (u8)((src.g * a + dst.g * inv + 127) / 255);
        fb->color[i].b = (u8)((src.b * a + dst.b * inv + 127) / 255);
    }
}

void gm_overlay_entity_outline(CrFramebuffer *fb, CrRgba *mask,
                               CrRgba *scratch) {
    if (!fb) return;
    gm_overlay_entity_outline_build(fb->w, fb->h, mask, scratch);
    gm_overlay_entity_outline_composite(fb, scratch);
}

static int emit_quad2(CrVertex *v, int n, int max,
                      CrVec3 a, CrVec3 b, CrVec3 c, CrVec3 d,
                      float u0, float v0, float u1, float v1, CrRgba tint)
{
    if (n + 12 > max) return n;
    const CrVec2 uva = {u0, v0}, uvb = {u1, v0}, uvc = {u1, v1}, uvd = {u0, v1};
    const CrVertex base = {
        .pos = {0, 0, 0}, .uv = {0, 0}, .light = 1.0f,
        .tint = tint, .ao = 1.0f, .blk = 0.0f,
    };
    CrVertex q[4] = { base, base, base, base };
    q[0].pos = a; q[0].uv = uva;  q[1].pos = b; q[1].uv = uvb;
    q[2].pos = c; q[2].uv = uvc;  q[3].pos = d; q[3].uv = uvd;
    /* two tris, both windings so one survives backface cull from any side */
    v[n++] = q[0]; v[n++] = q[1]; v[n++] = q[2];
    v[n++] = q[0]; v[n++] = q[2]; v[n++] = q[1];
    v[n++] = q[0]; v[n++] = q[2]; v[n++] = q[3];
    v[n++] = q[0]; v[n++] = q[3]; v[n++] = q[2];
    return n;
}

static int emit_quad2_gradient(
        CrVertex *v, int n, int max,
        CrVec3 a, CrVec3 b, CrVec3 c, CrVec3 d,
        float u, float uvv, CrRgba start, CrRgba end) {
    if (n + 6 > max) return n;
    CrVertex q[4] = {
        {.pos = a, .uv = {u, uvv}, .light = 1.0f,
         .tint = start, .ao = 1.0f, .blk = 0.0f},
        {.pos = b, .uv = {u, uvv}, .light = 1.0f,
         .tint = end, .ao = 1.0f, .blk = 0.0f},
        {.pos = c, .uv = {u, uvv}, .light = 1.0f,
         .tint = end, .ao = 1.0f, .blk = 0.0f},
        {.pos = d, .uv = {u, uvv}, .light = 1.0f,
         .tint = start, .ao = 1.0f, .blk = 0.0f},
    };
    v[n++] = q[0]; v[n++] = q[1]; v[n++] = q[2];
    v[n++] = q[0]; v[n++] = q[2]; v[n++] = q[3];
    return n;
}

/* Camera-facing ribbon for one AABB edge - approximates glLineWidth in world
 * space as a single translucent quad (no multi-face double-blend under
 * depthMask-false SRC_ALPHA compositing). */
static int emit_edge(CrVertex *v, int n, int max, CrVec3 p0, CrVec3 p1,
                     float half_w, float u, float uvv, CrRgba tint,
                     float ex, float ey, float ez)
{
    float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-8f) return n;
    dx /= len; dy /= len; dz /= len;
    float mx = 0.5f * (p0.x + p1.x);
    float my = 0.5f * (p0.y + p1.y);
    float mz = 0.5f * (p0.z + p1.z);
    float vx = ex - mx, vy = ey - my, vz = ez - mz;
    /* side = normalize(edge × to_eye); fallback if edge points at the eye */
    float sx = dy * vz - dz * vy;
    float sy = dz * vx - dx * vz;
    float sz = dx * vy - dy * vx;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl < 1e-8f) {
        /* pick a stable perpendicular */
        if (fabsf(dx) < 0.9f) { sx = 0.f; sy = dz; sz = -dy; }
        else                  { sx = -dz; sy = 0.f; sz = dx; }
        sl = sqrtf(sx * sx + sy * sy + sz * sz);
        if (sl < 1e-8f) return n;
    }
    sx = sx / sl * half_w;
    sy = sy / sl * half_w;
    sz = sz / sl * half_w;
    CrVec3 a = { p0.x + sx, p0.y + sy, p0.z + sz };
    CrVec3 b = { p1.x + sx, p1.y + sy, p1.z + sz };
    CrVec3 c = { p1.x - sx, p1.y - sy, p1.z - sz };
    CrVec3 d = { p0.x - sx, p0.y - sy, p0.z - sz };
    return emit_quad2(v, n, max, a, b, c, d, u, uvv, u, uvv, tint);
}

static int emit_edge_screen_gradient(
        CrVertex *v, int n, int max, CrVec3 p0, CrVec3 p1,
        float line_width, float u, float uvv, CrRgba start, CrRgba end,
        const CrCamera *camera, int fb_w, int fb_h) {
    CrMat4 view;
    CrVec4 q0, q1;
    float f, aspect, x0, y0, x1, y1, dx, dy, len, nx, ny;
    float ex0, ey0, ex1, ey1;
    CrVec3 a, b, c, d;
    if (!camera || fb_w < 1 || fb_h < 1 || line_width <= 0.0f)
        return n;
    view = cr_camera_view(camera);
    q0 = cr_mat4_mul_vec4(view, (CrVec4){p0.x, p0.y, p0.z, 1.0f});
    q1 = cr_mat4_mul_vec4(view, (CrVec4){p1.x, p1.y, p1.z, 1.0f});
    /* GL clips the line before rasterization. Structure boxes are normally
     * wholly in front of the eye; handle a near-plane crossing so an unusual
     * enclosing box cannot turn the screen-space expansion inside out. */
    {
        float near_z = -(camera->znear > 0.0f ? camera->znear : 0.05f);
        if (q0.z >= near_z && q1.z >= near_z) return n;
        if (q0.z >= near_z || q1.z >= near_z) {
            float t = (near_z - q0.z) / (q1.z - q0.z);
            CrVec3 pc = {
                p0.x + t * (p1.x - p0.x),
                p0.y + t * (p1.y - p0.y),
                p0.z + t * (p1.z - p0.z),
            };
            CrRgba cc = {
                (u8)((float)start.r + t * ((float)end.r - start.r)),
                (u8)((float)start.g + t * ((float)end.g - start.g)),
                (u8)((float)start.b + t * ((float)end.b - start.b)),
                (u8)((float)start.a + t * ((float)end.a - start.a)),
            };
            if (q0.z >= near_z) { p0 = pc; start = cc; }
            else { p1 = pc; end = cc; }
            q0 = cr_mat4_mul_vec4(
                view, (CrVec4){p0.x, p0.y, p0.z, 1.0f});
            q1 = cr_mat4_mul_vec4(
                view, (CrVec4){p1.x, p1.y, p1.z, 1.0f});
        }
    }
    f = 1.0f / tanf(camera->fov_deg * 0.00872664625997164788f);
    aspect = (float)fb_w / (float)fb_h;
    x0 = (q0.x * f / aspect / -q0.z * 0.5f + 0.5f) * fb_w;
    y0 = (0.5f - q0.y * f / -q0.z * 0.5f) * fb_h;
    x1 = (q1.x * f / aspect / -q1.z * 0.5f + 0.5f) * fb_w;
    y1 = (0.5f - q1.y * f / -q1.z * 0.5f) * fb_h;
    dx = x1 - x0; dy = y1 - y0;
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-8f) return n;
    nx = -dy / len * line_width * 0.5f;
    ny = dx / len * line_width * 0.5f;
    /* Convert the exact screen-perpendicular offset at each endpoint back
     * through the perspective scale and the rigid view rotation. The two
     * endpoints intentionally get different world offsets: that is what
     * keeps glLineWidth constant as depth changes along an edge. */
    ex0 = 2.0f * nx / fb_w * -q0.z * aspect / f;
    ey0 = -2.0f * ny / fb_h * -q0.z / f;
    ex1 = 2.0f * nx / fb_w * -q1.z * aspect / f;
    ey1 = -2.0f * ny / fb_h * -q1.z / f;
#define WORLD_OFFSET(ex, ey) (CrVec3){ \
    view.m[0] * (ex) + view.m[1] * (ey), \
    view.m[4] * (ex) + view.m[5] * (ey), \
    view.m[8] * (ex) + view.m[9] * (ey) }
    {
        CrVec3 s0 = WORLD_OFFSET(ex0, ey0);
        CrVec3 s1 = WORLD_OFFSET(ex1, ey1);
        a = (CrVec3){p0.x + s0.x, p0.y + s0.y, p0.z + s0.z};
        b = (CrVec3){p1.x + s1.x, p1.y + s1.y, p1.z + s1.z};
        c = (CrVec3){p1.x - s1.x, p1.y - s1.y, p1.z - s1.z};
        d = (CrVec3){p0.x - s0.x, p0.y - s0.y, p0.z - s0.z};
    }
#undef WORLD_OFFSET
    return emit_quad2_gradient(
        v, n, max, a, b, c, d, u, uvv, start, end);
}

static void overlay_opaque_uv(float *u, float *v) {
    float u0, v0, u1, v1;
    bm_sprite_uv(CR_SPRITE_STONE, &u0, &v0, &u1, &v1);
    *u = (u0 + u1) * 0.5f;
    *v = (v0 + v1) * 0.5f;
}

static int emit_uniform_box(
        CrVertex *v, int max, const float box[6], float line_width,
        CrRgba color, const CrCamera *camera, int fb_w, int fb_h) {
    CrVec3 p[8];
    float u, uvv;
    int n = 0;
    overlay_opaque_uv(&u, &uvv);
    p[0] = (CrVec3){box[0], box[1], box[2]};
    p[1] = (CrVec3){box[3], box[1], box[2]};
    p[2] = (CrVec3){box[0], box[4], box[2]};
    p[3] = (CrVec3){box[3], box[4], box[2]};
    p[4] = (CrVec3){box[0], box[1], box[5]};
    p[5] = (CrVec3){box[3], box[1], box[5]};
    p[6] = (CrVec3){box[0], box[4], box[5]};
    p[7] = (CrVec3){box[3], box[4], box[5]};
#define EDGE(a, b) n = emit_edge_screen_gradient( \
    v, n, max, p[a], p[b], line_width, u, uvv, color, color, \
    camera, fb_w, fb_h)
    /* RenderGlobal.drawBoundingBox's exact GL_LINE_STRIP edge order. This
     * matters when projected edges overlap and the opaque lines write depth. */
    EDGE(0, 1); EDGE(1, 5); EDGE(5, 4); EDGE(4, 0);
    EDGE(0, 2);
    EDGE(2, 3); EDGE(3, 7); EDGE(7, 6); EDGE(6, 2);
    EDGE(4, 6); EDGE(7, 5); EDGE(3, 1);
#undef EDGE
    return n;
}

int gm_overlay_emit_structure_bounds(
        CrVertex *v, int max, const GmRuntimeStructureBlock *s,
        const CrCamera *camera, int fb_w, int fb_h) {
    CrVec3 p[16];
    CrRgba c[16];
    double d1, d2, d3, d4, d5, d6, d7, d8, d9, d10;
    float u, uvv;
    int n = 0;
    if (!v || !s || !camera || fb_w < 1 || fb_h < 1 || max < 1 || !s->active
            || s->size_x < 1 || s->size_y < 1 || s->size_z < 1
            || (s->mode != GM_STRUCTURE_MODE_SAVE
                && s->mode != GM_STRUCTURE_MODE_LOAD)
            || (s->mode == GM_STRUCTURE_MODE_LOAD
                && !s->show_bounding_box))
        return 0;
    d1 = s->pos_x; d2 = s->pos_z;
    d6 = s->wy + s->pos_y - 0.01;
    d9 = d6 + s->size_y + 0.02;
    if (s->mirror == GM_STRUCTURE_MIRROR_LEFT_RIGHT) {
        d3 = s->size_x + 0.02; d4 = -(s->size_z + 0.02);
    } else if (s->mirror == GM_STRUCTURE_MIRROR_FRONT_BACK) {
        d3 = -(s->size_x + 0.02); d4 = s->size_z + 0.02;
    } else {
        d3 = s->size_x + 0.02; d4 = s->size_z + 0.02;
    }
    if (s->rotation == GM_STRUCTURE_ROTATION_CW90) {
        d5 = s->wx + (d4 < 0.0 ? d1 - 0.01 : d1 + 1.01);
        d7 = s->wz + (d3 < 0.0 ? d2 + 1.01 : d2 - 0.01);
        d8 = d5 - d4; d10 = d7 + d3;
    } else if (s->rotation == GM_STRUCTURE_ROTATION_CW180) {
        d5 = s->wx + (d3 < 0.0 ? d1 - 0.01 : d1 + 1.01);
        d7 = s->wz + (d4 < 0.0 ? d2 - 0.01 : d2 + 1.01);
        d8 = d5 - d3; d10 = d7 - d4;
    } else if (s->rotation == GM_STRUCTURE_ROTATION_CCW90) {
        d5 = s->wx + (d4 < 0.0 ? d1 + 1.01 : d1 - 0.01);
        d7 = s->wz + (d3 < 0.0 ? d2 - 0.01 : d2 + 1.01);
        d8 = d5 + d4; d10 = d7 - d3;
    } else {
        d5 = s->wx + (d3 < 0.0 ? d1 + 1.01 : d1 - 0.01);
        d7 = s->wz + (d4 < 0.0 ? d2 + 1.01 : d2 - 0.01);
        d8 = d5 + d3; d10 = d7 + d4;
    }
    p[0] = (CrVec3){(float)d5, (float)d6, (float)d7};
    p[1] = (CrVec3){(float)d8, (float)d6, (float)d7};
    p[2] = (CrVec3){(float)d8, (float)d6, (float)d10};
    p[3] = (CrVec3){(float)d5, (float)d6, (float)d10};
    p[4] = p[0];
    p[5] = (CrVec3){(float)d5, (float)d9, (float)d7};
    p[6] = (CrVec3){(float)d8, (float)d9, (float)d7};
    p[7] = (CrVec3){(float)d8, (float)d9, (float)d10};
    p[8] = (CrVec3){(float)d5, (float)d9, (float)d10};
    p[9] = p[5];
    p[10] = p[8];
    p[11] = p[3];
    p[12] = p[2];
    p[13] = p[7];
    p[14] = p[6];
    p[15] = p[1];
    c[0] = (CrRgba){223, 223, 223, 255};
    c[1] = (CrRgba){223, 127, 127, 255};
    c[2] = (CrRgba){223, 223, 223, 255};
    c[3] = (CrRgba){223, 223, 223, 255};
    c[4] = (CrRgba){127, 127, 223, 255};
    c[5] = (CrRgba){127, 223, 127, 255};
    for (int i = 6; i < 16; ++i)
        c[i] = (CrRgba){223, 223, 223, 255};
    overlay_opaque_uv(&u, &uvv);
    /* EntityRenderer restores GL_FLAT before renderEntities/TESRs. GL line
     * strips therefore use the second (provoking) vertex color for the whole
     * segment; interpolating the endpoint colors produces gradients Java
     * never draws. */
    for (int i = 0; i < 15; ++i)
        n = emit_edge_screen_gradient(
            v, n, max, p[i], p[i + 1], 2.0f,
            u, uvv, c[i + 1], c[i + 1], camera, fb_w, fb_h);
    return n;
}

int gm_overlay_emit_structure_marker(
        CrVertex *v, int max, const GmRuntimeStructureBlock *s,
        int wx, int wy, int wz, int block_id, int pass,
        const CrCamera *camera, int fb_w, int fb_h) {
    float box[6], f;
    CrRgba color;
    if (!v || !s || !camera || fb_w < 1 || fb_h < 1 || max < 1 || !s->active
            || s->mode != GM_STRUCTURE_MODE_SAVE || !s->show_air
            || (block_id != 0 && block_id != 217)
            || pass < 0 || pass > 1)
        return 0;
    f = block_id == 0 ? 0.05f : 0.0f;
    box[0] = (float)s->wx + (float)(wx - s->wx) + 0.45f - f;
    box[1] = (float)s->wy + (float)(wy - s->wy) + 0.45f - f;
    box[2] = (float)s->wz + (float)(wz - s->wz) + 0.45f - f;
    box[3] = (float)s->wx + (float)(wx - s->wx) + 0.55f + f;
    box[4] = (float)s->wy + (float)(wy - s->wy) + 0.55f + f;
    box[5] = (float)s->wz + (float)(wz - s->wz) + 0.55f + f;
    if (pass == 0) {
        color = (CrRgba){0, 0, 0, 255};
        return emit_uniform_box(
            v, max, box, 3.0f,
            color, camera, fb_w, fb_h);
    }
    color = block_id == 0
        ? (CrRgba){127, 127, 255, 255}
        : (CrRgba){255, 63, 63, 255};
    return emit_uniform_box(
        v, max, box, 1.0f,
        color, camera, fb_w, fb_h);
}

/* selection outline: vanilla RenderGlobal.drawSelectionBox /
 * drawSelectionBoundingBox / drawBoundingBox (1.11.2):
 *   - getSelectedBoundingBox.expandXyz(0.0020000000949949026)
 *   - colour black with alpha 0.4F (SRC_ALPHA / ONE_MINUS_SRC_ALPHA)
 *   - GL_LINES (mode 3 LINE_STRIP) at glLineWidth(2.0F)
 *   - texture off, depth test on, depthMask false
 * Software path: 12 camera-facing ribbons (~2 px half-width in world units at
 * typical look distance), translucent black. box = cell-space
 * {x0,y0,z0,x1,y1,z1}; NULL = full cube. */
static int emit_select_box(CrVertex *v, int max, int bx, int by, int bz,
                           const float *box,
                           float eye_x, float eye_y, float eye_z)
{
    /* Opaque atlas sample so shade is pure vertex colour (texture off). */
    float su0, sv0, su1, sv1;
    bm_sprite_uv(CR_SPRITE_STONE, &su0, &sv0, &su1, &sv1);
    float u   = (su0 + su1) * 0.5f;
    float uvv = (sv0 + sv1) * 0.5f;
    /* 0.0F, 0.0F, 0.0F, 0.4F -> a = round(0.4*255) = 102 */
    const CrRgba black = { 0, 0, 0, 102 };
    /* expandXyz(0.0020000000949949026D). half-width for glLineWidth(2.0F) at
     * ~1.6-block look distance, FOV 70, H=480: px≈0.0047 → half-w ≈ 0.0047. */
    const float e = 0.0020000000949949026f, t = 0.0045f;
    static const float FULL[6] = { 0.f, 0.f, 0.f, 1.f, 1.f, 1.f };
    if (!box) box = FULL;
    float x0 = bx + box[0] - e, y0 = by + box[1] - e, z0 = bz + box[2] - e;
    float x1 = bx + box[3] + e, y1 = by + box[4] + e, z1 = bz + box[5] + e;
    int n = 0;
    CrVec3 c000={x0,y0,z0}, c100={x1,y0,z0}, c010={x0,y1,z0}, c110={x1,y1,z0};
    CrVec3 c001={x0,y0,z1}, c101={x1,y0,z1}, c011={x0,y1,z1}, c111={x1,y1,z1};
    /* bottom ring */
    n = emit_edge(v,n,max,c000,c100,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c001,c101,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c000,c001,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c100,c101,t,u,uvv,black,eye_x,eye_y,eye_z);
    /* top ring */
    n = emit_edge(v,n,max,c010,c110,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c011,c111,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c010,c011,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c110,c111,t,u,uvv,black,eye_x,eye_y,eye_z);
    /* verticals */
    n = emit_edge(v,n,max,c000,c010,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c100,c110,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c001,c011,t,u,uvv,black,eye_x,eye_y,eye_z);
    n = emit_edge(v,n,max,c101,c111,t,u,uvv,black,eye_x,eye_y,eye_z);
    return n;
}

/* crack decal: destroy_stage_N. Vanilla re-renders the block model with the
 * destroy texture bound (all model faces); we approximate with the cube's
 * faces. Drawn with blend=2 (DST_COLOR/SRC_COLOR = 2*src*dst) and white
 * vertex color (preRenderDamagedBlocks: color(1,1,1,0.5) + alphaFunc 0.1).
 * face: 0=-z 1=+z 2=-x 3=+x 4=+y 5=-y, -1=all. */
static int emit_crack(CrVertex *v, int max, int bx, int by, int bz,
                      float damage, int face)
{
    /* PlayerControllerMP publishes (int)(curBlockDamageMP * 10) - 1. */
    int stage = (int)(damage * 10.0f) - 1;
    if (stage < 0) stage = 0;
    if (stage > 9) stage = 9;
    float u0, v0, u1, v1;
    bm_sprite_uv(CR_SPRITE_DESTROY_STAGE_0 + stage, &u0, &v0, &u1, &v1);
    const CrRgba white = { 255, 255, 255, 255 };
    /* polygonOffset(-3,-3) approx: sit just in front of the solid face. */
    const float e = 0.002f;
    float x0 = bx - e, y0 = by - e, z0 = bz - e;
    float x1 = bx + 1 + e, y1 = by + 1 + e, z1 = bz + 1 + e;
    CrVec3 c000={x0,y0,z0}, c100={x1,y0,z0}, c010={x0,y1,z0}, c110={x1,y1,z0};
    CrVec3 c001={x0,y0,z1}, c101={x1,y0,z1}, c011={x0,y1,z1}, c111={x1,y1,z1};
    int n = 0;
    int all = (face < 0 || face > 5);
    if (all || face == 0)
        n = emit_quad2(v,n,max, c000,c100,c110,c010, u1,v1,u0,v0, white); /* z- */
    if (all || face == 1)
        n = emit_quad2(v,n,max, c101,c001,c011,c111, u1,v1,u0,v0, white); /* z+ */
    if (all || face == 2)
        n = emit_quad2(v,n,max, c001,c000,c010,c011, u1,v1,u0,v0, white); /* x- */
    if (all || face == 3)
        n = emit_quad2(v,n,max, c100,c101,c111,c110, u1,v1,u0,v0, white); /* x+ */
    if (all || face == 4)
        n = emit_quad2(v,n,max, c010,c110,c111,c011, u0,v0,u1,v1, white); /* y+ */
    if (all || face == 5)
        n = emit_quad2(v,n,max, c001,c101,c100,c000, u0,v0,u1,v1, white); /* y- */
    return n;
}

int gm_overlay_emit_sel(CrVertex *v, int max,
                        int sx, int sy, int sz, const float *sel_box,
                        float eye_x, float eye_y, float eye_z)
{
    return emit_select_box(v, max, sx, sy, sz, sel_box, eye_x, eye_y, eye_z);
}

int gm_overlay_emit_crack(CrVertex *v, int max,
                          int dx, int dy, int dz, float damage, int face)
{
    if (damage <= 0.0f) return 0;
    return emit_crack(v, max, dx, dy, dz, damage, face);
}

int gm_overlay_emit(CrVertex *v, int max,
                    int have_sel, int sx, int sy, int sz, const float *sel_box,
                    int have_dig, int dx, int dy, int dz, float damage,
                    float eye_x, float eye_y, float eye_z)
{
    int n = 0;
    if (have_sel)
        n = emit_select_box(v, max, sx, sy, sz, sel_box, eye_x, eye_y, eye_z);
    if (have_dig && damage > 0.0f)
        n += emit_crack(v + n, max - n, dx, dy, dz, damage, -1);
    return n;
}

void gm_overlay_portal_screen(CrFramebuffer *fb, const CrTexture *atlas,
                              float time_in_portal) {
    if (!fb || !atlas || !atlas->texels || time_in_portal <= 0.0f) return;
    float alpha = time_in_portal;
    if (alpha < 1.0f) {
        alpha *= alpha;
        alpha *= alpha;
        alpha = alpha * 0.8f + 0.2f;
    }
    if (alpha > 1.0f) alpha = 1.0f;
    float u0,v0,u1,v1;
    bm_sprite_uv(CR_SPRITE_PORTAL,&u0,&v0,&u1,&v1);
    int sx0=(int)(u0*atlas->w),sy0=(int)(v0*atlas->h);
    int sw=(int)((u1-u0)*atlas->w+0.5f),sh=(int)((v1-v0)*atlas->h+0.5f);
    for(int y=0;y<fb->h;++y){
        int ty=sy0+(int)(((long long)(2*y+1)*sh)/(2*fb->h));
        for(int x=0;x<fb->w;++x){
            int tx=sx0+(int)(((long long)(2*x+1)*sw)/(2*fb->w));
            CrRgba src=atlas->texels[ty*atlas->w+tx];
            float a=((float)src.a/255.0f)*alpha,ia=1.0f-a;
            CrRgba *dst=&fb->color[y*fb->w+x];
            dst->r=(u8)((float)src.r*a+(float)dst->r*ia+0.5f);
            dst->g=(u8)((float)src.g*a+(float)dst->g*ia+0.5f);
            dst->b=(u8)((float)src.b*a+(float)dst->b*ia+0.5f);
        }
    }
}

static void rotate_portal_axis(float angle, float *x, float *y, float *z) {
    const float k = 0.7071067811865475244f;
    float c = cosf(angle), s = sinf(angle);
    float dot = k * (*y + *z);
    float rx = *x * c + k * (*z - *y) * s;
    float ry = *y * c + k * (*x) * s + k * dot * (1.0f - c);
    float rz = *z * c - k * (*x) * s + k * dot * (1.0f - c);
    *x = rx; *y = ry; *z = rz;
}

void gm_overlay_portal_warp(CrFramebuffer *fb, CrRgba *scratch,
                            float time_in_portal, int renderer_phase,
                            float partial_ticks, int nausea, float fov_deg) {
    if (!fb || !fb->color || !scratch || time_in_portal <= 0.0f) return;
    float f = time_in_portal;
    float scale = 5.0f / (f * f + 5.0f) - f * 0.04f;
    scale *= scale;
    float angle = ((float)renderer_phase + partial_ticks)
                * (nausea ? 7.0f : 20.0f)
                * 0.01745329251994329577f;
    float tan_half = tanf(fov_deg * 0.5f * 0.01745329251994329577f);
    float aspect = (float)fb->w / (float)fb->h;
    memcpy(scratch, fb->color, (size_t)fb->w * (size_t)fb->h * sizeof *scratch);
    for (int y = 0; y < fb->h; ++y) {
        float ny = 1.0f - (2.0f * ((float)y + 0.5f) / (float)fb->h);
        for (int x = 0; x < fb->w; ++x) {
            float nx = 2.0f * ((float)x + 0.5f) / (float)fb->w - 1.0f;
            float dx = nx * tan_half * aspect;
            float dy = ny * tan_half;
            float dz = -1.0f;
            rotate_portal_axis(-angle, &dx, &dy, &dz);
            dx *= scale;
            rotate_portal_axis(angle, &dx, &dy, &dz);
            float sx = (dx / -dz) / (tan_half * aspect);
            float sy = (dy / -dz) / tan_half;
            int ix = (int)(((sx + 1.0f) * 0.5f) * (float)fb->w);
            int iy = (int)(((1.0f - sy) * 0.5f) * (float)fb->h);
            if (ix < 0) ix = 0; else if (ix >= fb->w) ix = fb->w - 1;
            if (iy < 0) iy = 0; else if (iy >= fb->h) iy = fb->h - 1;
            fb->color[y * fb->w + x] = scratch[iy * fb->w + ix];
        }
    }
}

void gm_overlay_loading_screen(CrFramebuffer *fb) {
    if (!fb || !fb->color) return;
    int scale = fb->h / 240;
    if (scale < 1) scale = 1;
    int period = 32 * scale;
    for (int y = 0; y < fb->h; ++y) {
        int ty = ((y % period) * 16) / period;
        for (int x = 0; x < fb->w; ++x) {
            int tx = ((x % period) * 16) / period;
            const unsigned char *src = &CR_LOADING_BG_RGBA[(ty * 16 + tx) * 4];
            CrRgba *dst = &fb->color[y * fb->w + x];
            dst->r = (u8)((src[0] * 64 + 127) / 255);
            dst->g = (u8)((src[1] * 64 + 127) / 255);
            dst->b = (u8)((src[2] * 64 + 127) / 255);
            dst->a = 255;
        }
    }
    const char *label = "Loading terrain";
    int gui_w = fb->w / scale;
    int gui_h = fb->h / scale;
    int x = (gui_w / 2 - gm_font_width(label) / 2) * scale;
    int y = (gui_h / 2 - 50) * scale;
    gm_font_draw(fb, label, x, y, scale, 0xFFFFFFu, 1);
}

void gm_overlay_block_in_hand(CrFramebuffer *fb, const CrTexture *atlas,
                              float u0, float v0, float u1, float v1,
                              float fov_deg) {
    /* ItemRenderer.renderBlockInHand (1.11.2):
     *   bind blocks atlas; color(0.1,0.1,0.1,0.5); blend not enabled
     *   (renderOverlays disables alpha test only — water/fire enable blend)
     *   view-space quad at z=-0.5, x,y in [-1,1], drawn under the hand
     *   perspective (EntityRenderer.renderHand gluPerspective FOV).
     *   tex: pos(-1,-1)->(maxU,maxV), pos(1,-1)->(minU,maxV),
     *        pos(1,1)->(minU,minV), pos(-1,1)->(maxU,minV).
     * Replace RGB with tex*0.1 (GL_MODULATE, blend off). Screen only sees
     * the centre crop of the quad: eye x at |ndc|=1 is
     *   0.5 * aspect * tan(fov/2)  (same inverse as gm_uw_overlay_draw). */
    if (!fb || !fb->color || !atlas || !atlas->texels) return;
    if (fov_deg <= 0.0f) fov_deg = 70.0f;
    const float d2r = 3.14159265358979323846f / 180.0f;
    float tanH = tanf(0.5f * fov_deg * d2r);
    float aspect = (float)fb->w / (float)fb->h;
    /* Sprite bounds in atlas texels (NEAREST sample, GL unblurred blocks). */
    float min_u = u0, max_u = u1, min_v = v0, max_v = v1;
    if (max_u < min_u) { float t = min_u; min_u = max_u; max_u = t; }
    if (max_v < min_v) { float t = min_v; min_v = max_v; max_v = t; }
    const float mul = 0.1f;
    for (int py = 0; py < fb->h; ++py) {
        float ndcy = 1.0f - 2.0f * ((float)py + 0.5f) / (float)fb->h;
        float yq = ndcy * tanH * 0.5f; /* eye y at |z|=0.5 */
        /* y=-1 -> maxV, y=+1 -> minV */
        float fv = (yq + 1.0f) * 0.5f;
        float v = max_v + (min_v - max_v) * fv;
        int ty = (int)(v * (float)atlas->h);
        if (ty < 0) ty = 0;
        if (ty >= atlas->h) ty = atlas->h - 1;
        for (int px = 0; px < fb->w; ++px) {
            float ndcx = 2.0f * ((float)px + 0.5f) / (float)fb->w - 1.0f;
            float xq = ndcx * tanH * aspect * 0.5f;
            /* x=-1 -> maxU, x=+1 -> minU (U mirrored) */
            float fu = (xq + 1.0f) * 0.5f;
            float u = max_u + (min_u - max_u) * fu;
            int tx = (int)(u * (float)atlas->w);
            if (tx < 0) tx = 0;
            if (tx >= atlas->w) tx = atlas->w - 1;
            CrRgba src = atlas->texels[ty * atlas->w + tx];
            CrRgba *dst = &fb->color[py * fb->w + px];
            /* Blend off: replace with tex * color.rgb (alpha unused on RGB).
             * GL_MODULATE then UNORM8 pack on llvmpipe/Java uses round-half-to-
             * even (IEEE default / rintf), not half-up. Dirt particle hits .5
             * boundaries (85->8.5, 185->18.5): half-up produced (12,9,6)/
             * (19,13,9) vs Java (12,8,6)/(18,13,9). Stone has no .5 cases so
             * half-up and rintf agree (body already bit-exact). */
            dst->r = (u8)rintf((float)src.r * mul);
            dst->g = (u8)rintf((float)src.g * mul);
            dst->b = (u8)rintf((float)src.b * mul);
            dst->a = 255;
        }
    }
}
