#include "game/player_preview.h"
#include "assets/hand_atlas.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PREVIEW_MAX_TRIS 144
#define DEG2RAD 0.01745329251994329577f
/* GuiInventory.drawEntityOnScreen scale argument. */
#define PREVIEW_SCALE 30.0f
/* RenderPlayer.preRenderCallback: GlStateManager.scale(0.9375). */
#define PLAYER_SCALE 0.9375F
/* ModelRenderer scale factor returned by prepareScale (1/16). */
#define MODEL_SCALE 0.0625F
/* prepareScale translate after the Y flip (feet ~ origin). */
#define PREPARE_TY (-1.501f)

/* Side-channel for PREVIEW_DIAG attribution (part/face per emitted tri). */
static int g_diag_part[PREVIEW_MAX_TRIS];
static int g_diag_face[PREVIEW_MAX_TRIS];
static int g_diag_part_idx;
static const char *const PART_NAMES[] = {
    "head", "body", "rarm", "larm", "rleg", "lleg",
    "headwear", "bodywear", "rarmwear", "larmwear", "rlegwear", "llegwear",
};

typedef struct {
    int u, v;
    float x, y, z;
    int dx, dy, dz;
    float rx, ry, rz;
    float ax, ay, az;
    float inflate;
    int head; /* 1: applies netHeadYaw + rotationPitch */
} PreviewPart;

typedef struct { int idx[4]; int u1, v1, u2, v2; } PreviewFace;
typedef struct { float x, y, z, u, v; } PreviewVertex;

/* ModelPlayer (wide / Steve): ModelBiped base boxes + 64x64 wear layers.
 * Idle arm Z matches ModelBiped.setRotationAngles at ageInTicks=0 (the pin
 * used by qrl pin_preview_anim + drawEntityOnScreen partialTicks=1 with
 * ticksExisted=-1 so age = ticksExisted+partial = 0):
 *   right.rotateAngleZ += cos(age*0.09)*0.05 + 0.05  ->  +0.10
 *   left.rotateAngleZ  -= cos(age*0.09)*0.05 + 0.05  ->  -0.10
 *   arm X bob sin(age*0.067)*0.05 is 0 at age 0.
 * Legs use the non-sneak rotationPointZ=0.1. Wear layers copyModelAngles
 * from the base limbs (right armwear ctor z=10 is overwritten before render). */
static const PreviewPart PLAYER_PARTS[] = {
    { 0,  0, -4,-8,-4, 8, 8,8,  0, 0,0,     0,0,0,     0.0f,  1}, /* head */
    {16, 16, -4, 0,-2, 8,12,4,  0, 0,0,     0,0,0,     0.0f,  0}, /* body */
    {40, 16, -3,-2,-2, 4,12,4, -5, 2,0,     0,0,0.10f, 0.0f,  0}, /* right arm */
    {32, 48, -1,-2,-2, 4,12,4,  5, 2,0,     0,0,-0.10f,0.0f,  0}, /* left arm */
    { 0, 16, -2, 0,-2, 4,12,4, -1.9f,12,0.1f, 0,0,0,   0.0f,  0},
    {16, 48, -2, 0,-2, 4,12,4,  1.9f,12,0.1f, 0,0,0,   0.0f,  0},
    {32,  0, -4,-8,-4, 8, 8,8,  0, 0,0,     0,0,0,     0.5f,  1}, /* headwear */
    {16, 32, -4, 0,-2, 8,12,4,  0, 0,0,     0,0,0,     0.25f, 0}, /* body wear */
    {40, 32, -3,-2,-2, 4,12,4, -5, 2,0,     0,0,0.10f, 0.25f, 0},
    {48, 48, -1,-2,-2, 4,12,4,  5, 2,0,     0,0,-0.10f,0.25f, 0},
    { 0, 32, -2, 0,-2, 4,12,4, -1.9f,12,0.1f, 0,0,0,   0.25f, 0},
    { 0, 48, -2, 0,-2, 4,12,4,  1.9f,12,0.1f, 0,0,0,   0.25f, 0},
};

/* ModelBox face order / UVs (textureWidth=64). */
static void face_defs(int u, int v, int w, int h, int d, PreviewFace q[6])
{
    q[0] = (PreviewFace){{5,1,2,6}, u+d+w,   v+d, u+d+w+d,   v+d+h}; /* +X */
    q[1] = (PreviewFace){{0,4,7,3}, u,       v+d, u+d,       v+d+h}; /* -X */
    q[2] = (PreviewFace){{5,4,0,1}, u+d,     v,   u+d+w,     v+d};   /* -Y top UV */
    q[3] = (PreviewFace){{2,3,7,6}, u+d+w,   v+d, u+d+w+w,   v};     /* +Y bot UV */
    q[4] = (PreviewFace){{1,0,3,2}, u+d,     v+d, u+d+w,     v+d+h}; /* -Z */
    q[5] = (PreviewFace){{4,5,6,7}, u+d+w+d, v+d, u+d+w+d+w, v+d+h}; /* +Z */
}

/* ModelRenderer: matrix Rz then Ry then Rx => vertex sees Rx, Ry, Rz. */
static void rotate_zyx_vertex(float *x, float *y, float *z, float ax, float ay, float az)
{
    float c, s, nx, ny, nz;
    c = cosf(ax); s = sinf(ax);
    ny = *y * c - *z * s; nz = *y * s + *z * c;
    *y = ny; *z = nz;
    c = cosf(ay); s = sinf(ay);
    nx = *x * c + *z * s; nz = -*x * s + *z * c;
    *x = nx; *z = nz;
    c = cosf(az); s = sinf(az);
    nx = *x * c - *y * s; ny = *x * s + *y * c;
    *x = nx; *y = ny;
}

static void rotate_y(float *x, float *z, float deg)
{
    float a = deg * DEG2RAD, c = cosf(a), s = sinf(a);
    float nx = *x * c + *z * s;
    float nz = -*x * s + *z * c;
    *x = nx; *z = nz;
}

static void rotate_x(float *y, float *z, float deg)
{
    float a = deg * DEG2RAD, c = cosf(a), s = sinf(a);
    float ny = *y * c - *z * s;
    float nz = *y * s + *z * c;
    *y = ny; *z = nz;
}

/* RenderHelper.enableStandardItemLighting after the drawEntityOnScreen
 * Ry(135)/Ry(-135) sandwich. Lights are fixed in eye space at specification
 * time (modelview had Ry(135) from the GUI frame); subsequent Ry(-135) on
 * geometry means lighting sees normals as if lights were Ry(135)*LIGHT.
 * With the GUI frame including Rz(180)*S(-s,s,s) absorbed into the screen
 * map, we light in the post-applyRotations / pre-GUI entity space using the
 * sandwich-adjusted directions.
 *
 * LIGHT0/1 match RenderHelper.java double normalize of
 * ( +-0.20000000298023224, 1.0, -+0.699999988079071 ). shadeModel is GL_FLAT
 * (7424): one face normal, flat light per quad. Ambient 0.4 + two 0.6
 * diffuse terms, clamped at 1. */
static float standard_item_light(float nx, float ny, float nz)
{
    /* Pre-normalized from Java double Vec3d.normalize (bit-stable as float). */
    const float l0x = 0.16169041669088864f, l0y = 0.8084520834544432f,
                l0z = -0.5659164584181101f;
    const float l1x = -0.16169041669088864f, l1y = 0.8084520834544432f,
                l1z = 0.5659164584181101f;
    const float c = cosf(135.0f * DEG2RAD), s = sinf(135.0f * DEG2RAD);
    float e0x = c * l0x + s * l0z, e0z = -s * l0x + c * l0z;
    float e1x = c * l1x + s * l1z, e1z = -s * l1x + c * l1z;
    float sum = 0.4f;
    float d0 = nx * e0x + ny * l0y + nz * e0z;
    float d1 = nx * e1x + ny * l1y + nz * e1z;
    if (d0 > 0.0f) sum += 0.6f * d0;
    if (d1 > 0.0f) sum += 0.6f * d1;
    return sum > 1.0f ? 1.0f : sum;
}

static CrScreenVert screen_vertex(float x, float y, float z, float u, float v,
                                  float light, int cx, int bottom, float unit,
                                  float depth_bias)
{
    CrScreenVert out;
    memset(&out, 0, sizeof out);
    /* Orthographic GUI map: entity +Y up -> screen -Y; unit is GUI px / entity.
     *
     * Java depth (EntityRenderer.setupOverlayRendering):
     *   ortho(near=1000, far=3000); modelview translate(0,0,-2000);
     *   drawEntityOnScreen translate(posX,posY,50) then scale(-s,s,s).
     * Eye-space z ≈ -1950 + s*ez_entity (plus body rotations). Relative
     * order is monotonic in entity-frame +z (toward viewer after the GUI
     * sandwich is absorbed). Pack into [0,1] depth for GL_LEQUAL as
     *   depth = 0.5 - z * k - depth_bias
     * with k=0.02 so a body-width of ~2 entity units stays ordered without
     * saturating. depth_bias is a tiny per-part term so later ModelBiped
     * parts win true coplanar ties the way GL_LEQUAL does after draw order. */
    float dep = 0.5f - z * 0.02f - depth_bias;
    if (dep < 0.0f) dep = 0.0f;
    if (dep > 1.0f) dep = 1.0f;
    {
        double sx = (double)cx + (double)x * (double)unit;
        double sy = (double)bottom - (double)y * (double)unit;
        out.spos = (CrVec3){(float)sx, (float)sy, dep};
    }
    out.invw = 1.0f;
    out.uv_w = (CrVec2){u / 64.0f, v / 64.0f};
    out.light_w = light;
    out.ao_w = 1.0f;
    out.tint_r_w = 255.0f;
    out.tint_g_w = 255.0f;
    out.tint_b_w = 255.0f;
    out.tint_a_w = 255.0f;
    return out;
}

/* Transform a model-space point (ModelPlayer units) through prepareScale +
 * applyRotations body yaw + drawEntityOnScreen matrix pitch into a
 * camera-facing entity frame where +Y is up and the character faces +Z
 * toward the viewer after the GUI Rz(180)*S(-s,s,s) sandwich is absorbed. */
static void entity_frame(float *x, float *y, float *z,
                         float body_yaw_deg, float matrix_pitch_deg)
{
    /* prepareScale on model units m (ModelRenderer multiplies verts by 0.0625):
     *   v = m * MODEL_SCALE
     *   T(0, -1.501, 0) then S(0.9375) then S(-1, -1, 1)
     * => ex = -PLAYER_SCALE * mx * MODEL_SCALE
     *    ey = -PLAYER_SCALE * (my * MODEL_SCALE - 1.501)
     *    ez =  PLAYER_SCALE * mz * MODEL_SCALE
     */
    float ex = *x * MODEL_SCALE;
    float ey = *y * MODEL_SCALE + PREPARE_TY; /* PREPARE_TY = -1.501 */
    float ez = *z * MODEL_SCALE;
    ex *= PLAYER_SCALE; ey *= PLAYER_SCALE; ez *= PLAYER_SCALE;
    ex = -ex; ey = -ey; /* S(-1,-1,1); z unchanged */

    /* applyRotations: rotate(180 - renderYawOffset) about Y. */
    rotate_y(&ex, &ez, 180.0f - body_yaw_deg);

    /* drawEntityOnScreen: rotate(-atan(my/40)*20, 1, 0, 0). matrix_pitch_deg
     * is already that angle in degrees (negative when mouseY > 0). */
    rotate_x(&ey, &ez, matrix_pitch_deg);

    /* GUI scale(-s,s,s) * rotate(180,Z) absorbed into the screen map:
     * after Rz(180): (x,y)->(-x,-y); then S(-s,s,s): x_gui = s*x_pre, y_gui = -s*y_pre
     * with y_gui growing down. Entity +Y (up) becomes screen -Y. We keep
     * (ex,ey,ez) as entity-frame offsets and let screen_vertex apply unit. */
    *x = ex;
    *y = ey;
    *z = ez;
}

static int emit_part(const PreviewPart *part,
                     float body_yaw_deg, float net_head_yaw_deg, float head_pitch_deg,
                     float matrix_pitch_deg,
                     int cx, int bottom, float unit,
                     float depth_bias,
                     CrScreenTri *tris, int n)
{
    float x0 = part->x - part->inflate, x1 = part->x + part->dx + part->inflate;
    float y0 = part->y - part->inflate, y1 = part->y + part->dy + part->inflate;
    float z0 = part->z - part->inflate, z1 = part->z + part->dz + part->inflate;
    float corner[8][3] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
    };
    float ax = part->ax, ay = part->ay, az = part->az;
    if (part->head) {
        /* ModelBiped: rotateAngleY = netHeadYaw * deg2rad; rotateAngleX = headPitch * deg2rad. */
        ay += net_head_yaw_deg * DEG2RAD;
        ax += head_pitch_deg * DEG2RAD;
    }
    for (int i = 0; i < 8; ++i) {
        float x = corner[i][0], y = corner[i][1], z = corner[i][2];
        rotate_zyx_vertex(&x, &y, &z, ax, ay, az);
        x += part->rx; y += part->ry; z += part->rz;
        entity_frame(&x, &y, &z, body_yaw_deg, matrix_pitch_deg);
        corner[i][0] = x; corner[i][1] = y; corner[i][2] = z;
    }

    PreviewFace faces[6];
    face_defs(part->u, part->v, part->dx, part->dy, part->dz, faces);
    static const int tri_idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int f = 0; f < 6 && n + 2 <= PREVIEW_MAX_TRIS; ++f) {
        /* TexturedQuad UV assignment order. */
        float u[4] = {(float)faces[f].u2, (float)faces[f].u1,
                      (float)faces[f].u1, (float)faces[f].u2};
        float v[4] = {(float)faces[f].v1, (float)faces[f].v1,
                      (float)faces[f].v2, (float)faces[f].v2};
        PreviewVertex q[4];
        for (int k = 0; k < 4; ++k) {
            int c = faces[f].idx[k];
            q[k] = (PreviewVertex){corner[c][0], corner[c][1], corner[c][2], u[k], v[k]};
        }
        /* Face normal from transformed quad (enableRescaleNormal renormalize). */
        float e1x = q[1].x - q[0].x, e1y = q[1].y - q[0].y, e1z = q[1].z - q[0].z;
        float e2x = q[2].x - q[0].x, e2y = q[2].y - q[0].y, e2z = q[2].z - q[0].z;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float nl = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-12f) { nx /= nl; ny /= nl; nz /= nl; }
        float light = standard_item_light(nx, ny, nz);

        for (int t = 0; t < 2; ++t) {
            CrScreenVert a = screen_vertex(q[tri_idx[t][0]].x, q[tri_idx[t][0]].y,
                                           q[tri_idx[t][0]].z, q[tri_idx[t][0]].u,
                                           q[tri_idx[t][0]].v, light, cx, bottom, unit,
                                           depth_bias);
            CrScreenVert b = screen_vertex(q[tri_idx[t][1]].x, q[tri_idx[t][1]].y,
                                           q[tri_idx[t][1]].z, q[tri_idx[t][1]].u,
                                           q[tri_idx[t][1]].v, light, cx, bottom, unit,
                                           depth_bias);
            CrScreenVert c = screen_vertex(q[tri_idx[t][2]].x, q[tri_idx[t][2]].y,
                                           q[tri_idx[t][2]].z, q[tri_idx[t][2]].u,
                                           q[tri_idx[t][2]].v, light, cx, bottom, unit,
                                           depth_bias);
            /* Rasterizer keeps one winding; pick positive framebuffer area. */
            float area = (b.spos.x - a.spos.x) * (c.spos.y - a.spos.y)
                       - (b.spos.y - a.spos.y) * (c.spos.x - a.spos.x);
            tris[n].v[0] = a;
            tris[n].v[1] = area > 0.0f ? c : b;
            tris[n].v[2] = area > 0.0f ? b : c;
            if (n < PREVIEW_MAX_TRIS) {
                g_diag_part[n] = g_diag_part_idx;
                g_diag_face[n] = f;
            }
            ++n;
        }
    }
    return n;
}

/* Same edge / top-left tests as cpu/raster_cpu.c (y-down, pixel-center). */
static float diag_edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
static int diag_top_left(float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

/* PREVIEW_DIAG=1: attribute pose1 hard pixels to part/face/tri/UV/depth/light. */
static void preview_diag_attribute(const CrScreenTri *tris, int n, int w, int h)
{
    static const int probes[][2] = {{42, 49}, {49, 139}};
    const float CR_FRONT_SIGN = -1.0f;
    fprintf(stderr, "PREVIEW_DIAG ntris=%d viewport=%dx%d\n", n, w, h);
    for (int pi = 0; pi < 2; ++pi) {
        int px = probes[pi][0], py = probes[pi][1];
        float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        float best_z = 2.0f, best_inc_z = 2.0f;
        int best = -1, best_inc = -1, hits = 0;
        fprintf(stderr, "PREVIEW_DIAG pixel (%d,%d) center=(%.1f,%.1f)\n", px, py, fx, fy);
        for (int t = 0; t < n; ++t) {
            const CrScreenVert *v0 = &tris[t].v[0];
            const CrScreenVert *v1 = &tris[t].v[1];
            const CrScreenVert *v2 = &tris[t].v[2];
            float x0 = v0->spos.x, y0 = v0->spos.y;
            float x1 = v1->spos.x, y1 = v1->spos.y;
            float x2 = v2->spos.x, y2 = v2->spos.y;
            float area = diag_edge(x0, y0, x1, y1, x2, y2);
            if (area * CR_FRONT_SIGN <= 0.0f) continue;
            float w0 = diag_edge(x1, y1, x2, y2, fx, fy);
            float w1 = diag_edge(x2, y2, x0, y0, fx, fy);
            float w2 = diag_edge(x0, y0, x1, y1, fx, fy);
            float b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
            int tl0 = diag_top_left(x1, y1, x2, y2);
            int tl1 = diag_top_left(x2, y2, x0, y0);
            int tl2 = diag_top_left(x0, y0, x1, y1);
            int in0 = (b0 > 0.0f) || (b0 == 0.0f && tl0);
            int in1 = (b1 > 0.0f) || (b1 == 0.0f && tl1);
            int in2 = (b2 > 0.0f) || (b2 == 0.0f && tl2);
            int strict = in0 && in1 && in2;
            int inclusive = (b0 >= 0.0f && b1 >= 0.0f && b2 >= 0.0f);
            int near = (b0 > -2e-3f && b1 > -2e-3f && b2 > -2e-3f);
            if (!strict && !near && !inclusive) continue;
            float z = b0 * v0->spos.z + b1 * v1->spos.z + b2 * v2->spos.z;
            float u = (b0 * v0->uv_w.x + b1 * v1->uv_w.x + b2 * v2->uv_w.x);
            float v = (b0 * v0->uv_w.y + b1 * v1->uv_w.y + b2 * v2->uv_w.y);
            float light = b0 * v0->light_w + b1 * v1->light_w + b2 * v2->light_w;
            int pidx = g_diag_part[t];
            const char *pname = (pidx >= 0 && pidx < 12) ? PART_NAMES[pidx] : "?";
            /* Sample atlas at UV for alpha (cutout). */
            int tu = (int)floorf(u * 64.0f), tv = (int)floorf(v * 64.0f);
            if (tu < 0) tu = 0;
            if (tu > 63) tu = 63;
            if (tv < 0) tv = 0;
            if (tv > 63) tv = 63;
            const unsigned char *tex = HAND_SKIN_RGBA_STEVE + ((tv * 64 + tu) * 4);
            fprintf(stderr,
                    "  %s%s tri=%d part=%s face=%d z=%.6f light=%.6f uv=(%.4f,%.4f) "
                    "bary=(%.5f,%.5f,%.5f) texel=(%u,%u,%u,%u)\n",
                    strict ? "HIT " : "NEAR", inclusive && !strict ? "+INC" : "    ",
                    t, pname, g_diag_face[t],
                    z, light, u * 64.0f, v * 64.0f, b0, b1, b2,
                    tex[0], tex[1], tex[2], tex[3]);
            if (strict && tex[3] >= 128) {
                ++hits;
                if (z < best_z || (z == best_z && t > best)) {
                    best_z = z;
                    best = t;
                }
            }
            if (inclusive && tex[3] >= 128) {
                if (z < best_inc_z || (z == best_inc_z && t > best_inc)) {
                    best_inc_z = z;
                    best_inc = t;
                }
            }
        }
        if (best >= 0) {
            int pidx = g_diag_part[best];
            fprintf(stderr, "  WINNER(tl) tri=%d part=%s face=%d z=%.6f opaque_hits=%d\n",
                    best, (pidx >= 0 && pidx < 12) ? PART_NAMES[pidx] : "?",
                    g_diag_face[best], best_z, hits);
        } else {
            fprintf(stderr, "  WINNER(tl) none opaque (hits=%d)\n", hits);
        }
        if (best_inc >= 0) {
            int pidx = g_diag_part[best_inc];
            fprintf(stderr, "  WINNER(inc b>=0) tri=%d part=%s face=%d z=%.6f\n",
                    best_inc, (pidx >= 0 && pidx < 12) ? PART_NAMES[pidx] : "?",
                    g_diag_face[best_inc], best_inc_z);
        }
    }
}

void gm_player_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                            float mouse_x, float mouse_y)
{
    if (!fb || !fb->color || w <= 0 || h <= 0) return;
    size_t pixels = (size_t)w * h;
    CrRgba *color = calloc(pixels, sizeof *color);
    float *depth = malloc(pixels * sizeof *depth);
    if (!color || !depth) { free(color); free(depth); return; }
    for (size_t i = 0; i < pixels; ++i) depth[i] = 1.0f;
    CrFramebuffer local = {w, h, color, depth};

    /* Exact GuiInventory.drawEntityOnScreen entity field assignments (degrees).
     * No calibration gains. */
    float body_yaw_deg = atanf(mouse_x / 40.0f) * 20.0f;       /* renderYawOffset */
    float head_yaw_deg = atanf(mouse_x / 40.0f) * 40.0f;       /* rotationYaw / yawHead */
    float head_pitch_deg = -atanf(mouse_y / 40.0f) * 20.0f;    /* rotationPitch */
    float matrix_pitch_deg = -atanf(mouse_y / 40.0f) * 20.0f;  /* GlStateManager.rotate */
    float net_head_yaw_deg = head_yaw_deg - body_yaw_deg;

    /* unit: entity-space (after prepareScale, before GUI scale) -> local GUI px.
     * GUI GlStateManager.scale(PREVIEW_SCALE) maps 1 entity unit -> PREVIEW_SCALE
     * GUI px; local viewport is in framebuffer px of size (w,h) for a 52x72 GUI
     * rect, so multiply by (h/72). */
    float unit = PREVIEW_SCALE * ((float)h / 72.0f);
    /* Feet anchor: drawEntityOnScreen(guiLeft+51, guiTop+75). Viewport is the
     * 52x72 rect at panel (24,7), so local feet = (51-24, 75-7) = (27, 68). */
    int cx = (int)lroundf(27.0f * ((float)w / 52.0f));
    int bottom = (int)lroundf(68.0f * ((float)h / 72.0f));

    CrScreenTri tris[PREVIEW_MAX_TRIS];
    int n = 0;
    int nparts = (int)(sizeof PLAYER_PARTS / sizeof PLAYER_PARTS[0]);
    for (int i = 0; i < nparts; ++i) {
        /* Later ModelBiped parts win exact coplanar float ties (GL_LEQUAL).
         * Keep this tiny vs real front/back separation (~0.01 depth units). */
        float depth_bias = (float)(i + 1) * 1.0e-5f;
        g_diag_part_idx = i;
        n = emit_part(&PLAYER_PARTS[i], body_yaw_deg, net_head_yaw_deg, head_pitch_deg,
                      matrix_pitch_deg, cx, bottom, unit, depth_bias, tris, n);
    }

    {
        const char *pd = getenv("PREVIEW_DIAG");
        int mode = pd ? atoi(pd) : 0;
        if (mode >= 1)
            preview_diag_attribute(tris, n, w, h);
        if (mode >= 2) {
            /* Per-part screen AABB + 2d distance of probes to nearest front tri. */
            static const int probes[][2] = {{42, 49}, {49, 139}};
            for (int p = 0; p < 12; ++p) {
                float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
                int any = 0;
                for (int t = 0; t < n; ++t) {
                    if (g_diag_part[t] != p) continue;
                    for (int k = 0; k < 3; ++k) {
                        float x = tris[t].v[k].spos.x, y = tris[t].v[k].spos.y;
                        if (x < minx) minx = x;
                        if (x > maxx) maxx = x;
                        if (y < miny) miny = y;
                        if (y > maxy) maxy = y;
                        any = 1;
                    }
                }
                if (!any) continue;
                fprintf(stderr, "PREVIEW_DIAG part=%s aabb=(%.2f,%.2f)-(%.2f,%.2f)\n",
                        PART_NAMES[p], minx, miny, maxx, maxy);
            }
            for (int pi = 0; pi < 2; ++pi) {
                float fx = probes[pi][0] + 0.5f, fy = probes[pi][1] + 0.5f;
                for (int p = 0; p < 12; ++p) {
                    float best_out = 1e9f; /* how far outside (sum of negative bary * |area|) */
                    int best_t = -1;
                    for (int t = 0; t < n; ++t) {
                        if (g_diag_part[t] != p) continue;
                        const CrScreenVert *v0 = &tris[t].v[0];
                        const CrScreenVert *v1 = &tris[t].v[1];
                        const CrScreenVert *v2 = &tris[t].v[2];
                        float area = diag_edge(v0->spos.x, v0->spos.y, v1->spos.x, v1->spos.y,
                                               v2->spos.x, v2->spos.y);
                        if (area * -1.0f <= 0.0f) continue;
                        float w0 = diag_edge(v1->spos.x, v1->spos.y, v2->spos.x, v2->spos.y, fx, fy);
                        float w1 = diag_edge(v2->spos.x, v2->spos.y, v0->spos.x, v0->spos.y, fx, fy);
                        float w2 = diag_edge(v0->spos.x, v0->spos.y, v1->spos.x, v1->spos.y, fx, fy);
                        float b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
                        float outside = 0.0f;
                        if (b0 < 0) outside += -b0;
                        if (b1 < 0) outside += -b1;
                        if (b2 < 0) outside += -b2;
                        if (outside < best_out) { best_out = outside; best_t = t; }
                    }
                    if (best_t >= 0)
                        fprintf(stderr, "  probe(%d,%d) part=%s outside_bary=%.5f tri=%d face=%d\n",
                                probes[pi][0], probes[pi][1], PART_NAMES[p], best_out,
                                best_t, g_diag_face[best_t]);
                }
            }
        }
    }

    CrTexture skin = {0};
    skin.w = HAND_SKIN_W; skin.h = HAND_SKIN_H;
    skin.texels = (const CrRgba *)HAND_SKIN_RGBA_STEVE;
    CrShadeCtx shade = {0};
    shade.atlas = &skin;
    shade.alpha_test = 1;          /* cutout like entity skin */
    shade.layer = CR_LAYER_CUTOUT;
    /* GL depth func is LEQUAL; coplanar head/body neck edges need it so the
     * later ModelBiped part wins the way fixed-function does. */
    shade.depth_lequal = 1;
    /* Fixed-function entity color path truncates to 8-bit (not +0.5 round). */
    shade.color_trunc = 1;
    /* Mesa/Java covers pixel centers ~1e-3 px outside our mathematical edges
     * (pose1 hard pixels measured at ~0.0008 px). Pixel-space slack, not bary. */
    shade.cover_eps = 0.001f;
    cr_raster_cpu(&local, tris, n, &shade);

    /* Depth-tested local buffer -> parent: replace when alpha survives cutout
     * (entity pass is opaque + alpha test, not translucent blend). */
    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            CrRgba c = color[sy * w + sx];
            if (!c.a) continue;
            int dx = x + sx, dy = y + sy;
            if (dx >= 0 && dy >= 0 && dx < fb->w && dy < fb->h)
                fb->color[dy * fb->w + dx] = c;
        }
    free(depth);
    free(color);
}
