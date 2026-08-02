#include "game/player_preview.h"
#include "assets/hand_atlas.h"
#include "core/config.h"   /* preview_dump_path / preview_diag / preview_color_mode */

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
 * sandwich-adjusted directions. Y-flips from Rz(180)*S cancel in the dot
 * product (see comments in emit_part).
 *
 * LIGHT0/1 match RenderHelper.java double normalize of
 * ( +-0.20000000298023224, 1.0, -+0.699999988079071 ). shadeModel is GL_FLAT
 * (7424): one face normal, flat light per quad. colorMaterial FRONT_AND_BACK
 * AMBIENT_AND_DIFFUSE with glColor(1,1,1): material ambient=diffuse=1.
 * Light model ambient 0.4; each light diffuse 0.6, ambient/specular 0.
 *
 * Mesa/llvmpipe fixed-function path quantizes light*material to unorm8 before
 * the n·L scale (same (a*b+128)>>8 used elsewhere for unorm8 modulate):
 *   amb_u8  = round(0.4*255) = 102;  (102*255+128)>>8 = 102  → 102/255
 *   diff_u8 = round(0.6*255) = 153;  (153*255+128)>>8 = 152  → 152/255
 * Using raw 0.6 float for diffuse leaves primary L8 one high on the large
 * pose1 -Z bins (211 vs Java 210) while trunc packing helps pose2; the unorm8
 * light*material product closes both. Sum clamped at 1 (GL primary color). */
static float standard_item_light(float nx, float ny, float nz)
{
    /* Exact Java double Vec3d.normalize results, kept in double through the
     * Ry(135) sandwich then stored as float (glLight float upload). */
    static int init = 0;
    static float e0x, e0y, e0z, e1x, e1y, e1z;
    /* unorm8 light*material (see comment above). */
    static const float AMB = 102.0f / 255.0f;
    static const float DIFF = 152.0f / 255.0f;
    if (!init) {
        const double lx = 0.20000000298023224, ly = 1.0, lz = -0.699999988079071;
        const double inv = 1.0 / sqrt(lx * lx + ly * ly + lz * lz);
        const double l0x = lx * inv, l0y = ly * inv, l0z = lz * inv;
        const double l1x = -l0x, l1y = l0y, l1z = -l0z;
        const double c = cos(135.0 * (double)DEG2RAD), s = sin(135.0 * (double)DEG2RAD);
        e0x = (float)(c * l0x + s * l0z);
        e0y = (float)l0y;
        e0z = (float)(-s * l0x + c * l0z);
        e1x = (float)(c * l1x + s * l1z);
        e1y = (float)l1y;
        e1z = (float)(-s * l1x + c * l1z);
        init = 1;
    }
    float sum = AMB;
    float d0 = nx * e0x + ny * e0y + nz * e0z;
    float d1 = nx * e1x + ny * e1y + nz * e1z;
    if (d0 > 0.0f) sum += DIFF * d0;
    if (d1 > 0.0f) sum += DIFF * d1;
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
    /* ModelBox outward normals in part-local space (TexturedQuad emits these;
     * GL then transforms via modelview + RESCALE_NORMAL). Transform the unit
     * axis normal through the same rotations as vertices (uniform scale drops
     * out after renormalize) so lighting matches fixed-function, not a
     * cross-product of float-transformed corners. */
    static const float FACE_N[6][3] = {
        { 1, 0, 0}, {-1, 0, 0}, { 0,-1, 0}, { 0, 1, 0}, { 0, 0,-1}, { 0, 0, 1},
    };
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
        /* Rotate part-local normal: ModelRenderer Rz*Ry*Rx on directions, then
         * entity_frame linear part S(-1,-1,1) * Ry(180-body) * Rx(pitch). */
        float nx = FACE_N[f][0], ny = FACE_N[f][1], nz = FACE_N[f][2];
        rotate_zyx_vertex(&nx, &ny, &nz, ax, ay, az);
        nx = -nx; ny = -ny; /* prepareScale S(-1,-1,1); z unchanged */
        {
            float tmpx = nx, tmpz = nz;
            rotate_y(&tmpx, &tmpz, 180.0f - body_yaw_deg);
            nx = tmpx; nz = tmpz;
        }
        {
            float tmpy = ny, tmpz = nz;
            rotate_x(&tmpy, &tmpz, matrix_pitch_deg);
            ny = tmpy; nz = tmpz;
        }
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

/* Preview-local color packing experiment (registry preview_color_mode). Not a
 * terrain default; only player_preview recolor uses it. */
static int g_preview_color_mode;

/* Integer / float color conversions for GL_MODULATE of a flat primary light L
 * with an 8-bit texel. Identified from >=20 interior pixel traces vs llvmpipe
 * goldens: primary is quantized to u8 (round), then
 *   out = (tex * L8 + 127) / 255
 * which is the classic fixed-function 8x8->8 modulate (Mesa/llvmpipe path
 * for RGBA8 * primary). Mode 0 keeps the historical float trunc for A/B. */
static void preview_modulate_u8(u8 tex, float light, int mode, u8 *out)
{
    if (mode == 0) {
        /* float trunc: (u8)(tex * L) via (tex/255)*L*255 */
        float c = (tex * (1.0f / 255.0f)) * light;
        if (c < 0.0f) c = 0.0f;
        if (c > 1.0f) c = 1.0f;
        *out = (u8)(c * 255.0f);
        return;
    }
    if (mode == 1) {
        float c = (tex * (1.0f / 255.0f)) * light;
        if (c < 0.0f) c = 0.0f;
        if (c > 1.0f) c = 1.0f;
        *out = (u8)(c * 255.0f + 0.5f);
        return;
    }
    if (mode == 10) {
        double c = (double)tex * (double)light;
        if (c < 0.0) c = 0.0;
        if (c > 255.0) c = 255.0;
        *out = (u8)c;
        return;
    }
    if (mode == 11) {
        double c = (double)tex * (double)light + 0.5;
        if (c < 0.0) c = 0.0;
        if (c > 255.0) c = 255.0;
        *out = (u8)c;
        return;
    }
    /* modes 2-9, 12: quantize light to u8 then integer modulate */
    int L8;
    if (mode == 2 || mode == 4 || mode == 6 || mode == 8)
        L8 = (int)(light * 255.0f);           /* trunc */
    else
        L8 = (int)(light * 255.0f + 0.5f);    /* round: FLOAT_TO_UBYTE */
    if (L8 < 0) L8 = 0;
    if (L8 > 255) L8 = 255;
    int p = (int)tex * L8;
    int v;
    switch (mode) {
    case 2: case 3: v = p / 255; break;
    case 4: case 5: v = (p + 127) / 255; break;
    case 6: case 7: v = p >> 8; break;
    case 8: case 9: v = (p + 255) >> 8; break;
    /* 12: Mesa/llvmpipe unorm8 modulate (a*b + 0x80) >> 8 with L8=round.
     * Measured mean residual ~0.008 on pose1 vs float-trunc ~0.081; exact
     * /255 form needs a slightly lower primary (lighting residual). */
    case 12: v = (p + 128) >> 8; break;
    default: v = (p + 128) >> 8; break;
    }
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    *out = (u8)v;
}

/* Same coverage / depth rule as the preview raster (cover_eps + LEQUAL / slack). */
static int preview_cover_pixel(const CrScreenTri *tris, int n, int px, int py,
                               float cover_eps, float *out_light, float *out_u, float *out_v,
                               float *out_z, int *out_tri)
{
    const float CR_FRONT_SIGN = -1.0f;
    float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
    float best_z = 2.0f;
    int best = -1;
    float best_b0 = 0, best_b1 = 0, best_b2 = 0;
    int best_slack = 0;
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
        int s0 = (b0 > 0.0f) || (b0 == 0.0f && tl0);
        int s1 = (b1 > 0.0f) || (b1 == 0.0f && tl1);
        int s2 = (b2 > 0.0f) || (b2 == 0.0f && tl2);
        int strict_in = s0 && s1 && s2;
        int in0 = s0, in1 = s1, in2 = s2;
        if (!strict_in && cover_eps > 0.0f) {
            float el0 = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
            float el1 = sqrtf((x0 - x2) * (x0 - x2) + (y0 - y2) * (y0 - y2));
            float el2 = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            if (!in0 && el0 > 1e-12f && w0 * area < 0.0f
                && fabsf(w0) / el0 <= cover_eps) in0 = 1;
            if (!in1 && el1 > 1e-12f && w1 * area < 0.0f
                && fabsf(w1) / el1 <= cover_eps) in1 = 1;
            if (!in2 && el2 > 1e-12f && w2 * area < 0.0f
                && fabsf(w2) / el2 <= cover_eps) in2 = 1;
        }
        if (!(in0 && in1 && in2)) continue;
        int slack_hit = !strict_in;
        float z = b0 * v0->spos.z + b1 * v1->spos.z + b2 * v2->spos.z;
        int better;
        if (best < 0) better = 1;
        else if (slack_hit)
            better = (z + 1.0e-5f < best_z);
        else if (best_slack)
            better = (z < best_z) || (z == best_z); /* strict can replace prior slack at equal */
        else
            better = (z < best_z) || (z == best_z); /* LEQUAL + later tri wins */
        if (!better) continue;
        /* Alpha cutout: sample texel before accepting. */
        float invw = b0 * v0->invw + b1 * v1->invw + b2 * v2->invw;
        float iw = 1.0f / invw;
        float u = (b0 * v0->uv_w.x + b1 * v1->uv_w.x + b2 * v2->uv_w.x) * iw;
        float v = (b0 * v0->uv_w.y + b1 * v1->uv_w.y + b2 * v2->uv_w.y) * iw;
        int tu = (int)floorf(u * 64.0f);
        int tv = (int)floorf(v * 64.0f);
        if (tu < 0) tu = 0;
        if (tu > 63) tu = 63;
        if (tv < 0) tv = 0;
        if (tv > 63) tv = 63;
        const unsigned char *tex = HAND_SKIN_RGBA_STEVE + ((tv * 64 + tu) * 4);
        if (tex[3] < 128) continue;
        best_z = z;
        best = t;
        best_b0 = b0; best_b1 = b1; best_b2 = b2;
        best_slack = slack_hit;
        (void)best_b0; (void)best_b1; (void)best_b2;
        if (out_u) *out_u = u;
        if (out_v) *out_v = v;
        if (out_light) {
            /* GL_FLAT: primary from provoking vertex 0 (same light on all verts). */
            *out_light = v0->light_w; /* invw=1 so light_w == light */
        }
        if (out_z) *out_z = z;
        if (out_tri) *out_tri = t;
    }
    return best >= 0;
}

static void preview_recolor_modulate(CrFramebuffer *local, const CrScreenTri *tris, int n,
                                     const CrTexture *skin, int mode)
{
    (void)skin;
    for (int py = 0; py < local->h; ++py) {
        for (int px = 0; px < local->w; ++px) {
            int idx = py * local->w + px;
            if (!local->color[idx].a) continue;
            float light = 0, u = 0, v = 0, z = 0;
            if (!preview_cover_pixel(tris, n, px, py, 0.001f, &light, &u, &v, &z, NULL))
                continue;
            /* Pure floor nearest (matches shade.sample_mode=1 / GL_NEAREST). */
            int tu = (int)floorf(u * 64.0f);
            int tv = (int)floorf(v * 64.0f);
            if (tu < 0) tu = 0;
            if (tu > 63) tu = 63;
            if (tv < 0) tv = 0;
            if (tv > 63) tv = 63;
            const unsigned char *tex = HAND_SKIN_RGBA_STEVE + ((tv * 64 + tu) * 4);
            u8 r, g, b;
            preview_modulate_u8(tex[0], light, mode, &r);
            preview_modulate_u8(tex[1], light, mode, &g);
            preview_modulate_u8(tex[2], light, mode, &b);
            local->color[idx].r = r;
            local->color[idx].g = g;
            local->color[idx].b = b;
            /* keep alpha */
        }
    }
}

/* preview_diag=3: CSV of interior samples for formula identification. */
static void preview_dump_fragments(const CrScreenTri *tris, int n, const CrTexture *skin,
                                   const CrRgba *color, int w, int h)
{
    (void)skin;
    const char *path = cr_cfg()->preview_dump_path;
    if (!path[0]) path = "/tmp/preview_frags.csv";
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "PREVIEW_DIAG dump open failed: %s\n", path);
        return;
    }
    fprintf(f, "x,y,light,tex_r,tex_g,tex_b,tex_a,out_r,out_g,out_b,part,face,tri,u,v\n");
    int written = 0;
    /* Interior: shrink 3px; require opaque output. */
    for (int py = 3; py < h - 3; ++py) {
        for (int px = 3; px < w - 3; ++px) {
            int idx = py * w + px;
            if (!color[idx].a) continue;
            float light = 0, u = 0, v = 0, z = 0;
            int tri = -1;
            if (!preview_cover_pixel(tris, n, px, py, 0.001f, &light, &u, &v, &z, &tri))
                continue;
            int tu = (int)floorf(u * 64.0f);
            int tv = (int)floorf(v * 64.0f);
            if (tu < 0) tu = 0;
            if (tu > 63) tu = 63;
            if (tv < 0) tv = 0;
            if (tv > 63) tv = 63;
            const unsigned char *tex = HAND_SKIN_RGBA_STEVE + ((tv * 64 + tu) * 4);
            int pidx = (tri >= 0 && tri < PREVIEW_MAX_TRIS) ? g_diag_part[tri] : -1;
            int face = (tri >= 0 && tri < PREVIEW_MAX_TRIS) ? g_diag_face[tri] : -1;
            fprintf(f, "%d,%d,%.9f,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%.6f,%.6f\n",
                    px, py, light, tex[0], tex[1], tex[2], tex[3],
                    color[idx].r, color[idx].g, color[idx].b,
                    pidx, face, tri, u * 64.0f, v * 64.0f);
            ++written;
        }
    }
    fclose(f);
    fprintf(stderr, "PREVIEW_DIAG dump %d frags -> %s\n", written, path);
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
        int mode = cr_cfg()->preview_diag;
        /* mode 1: hard-pixel attribute; 2: +AABB; 3: fragment dump only. */
        if (mode == 1 || mode == 2)
            preview_diag_attribute(tris, n, w, h);
        if (mode == 2) {
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
    /* Terrain goldens keep shade.color_trunc default (round). Preview uses a
     * separate Mesa fixed-function modulate (see recolor below). */
    shade.color_trunc = 1;
    /* Mesa/Java covers pixel centers ~1e-3 px outside our mathematical edges
     * (pose1 hard pixels measured at ~0.0008 px). Pixel-space slack, not bary. */
    shade.cover_eps = 0.001f;
    /* Preview-local packing (does not touch terrain defaults).
     * sample_mode=1: pure floor nearest (GL_NEAREST). Terrain keeps the
     * high-edge -1e-4 bias via sample_mode=0.
     *
     * Color path (preview_color_mode):
     * Default 4 = Mesa unorm8 modulate after ubyte primary:
     *   L8 = trunc(primary*255);  out = (tex*L8 + 127) / 255
     * Interior traces (>=20 faces, both poses) identify this packing once the
     * unorm8 light*material primary is correct (see standard_item_light).
     * Mode 0 keeps historical float trunc via shade for A/B; modes 2-12 are
     * experiment recolors (preview_diag=3). */
    shade.sample_mode = 1;
    {
        const char *pcm = cr_cfg()->preview_color_mode;
        /* Default 4: trunc L8 + (tex*L8+127)/255 — identified packing. */
        g_preview_color_mode = pcm[0] ? atoi(pcm) : 4;
        if (g_preview_color_mode == 1) shade.color_trunc = 0;
    }
    cr_raster_cpu(&local, tris, n, &shade);

    /* Integer unorm8 modulate recolor (default and experiment modes). */
    if (g_preview_color_mode != 0 && g_preview_color_mode != 1) {
        preview_recolor_modulate(&local, tris, n, &skin, g_preview_color_mode);
    }

    /* preview_diag=3: dump interior pixel (x,y,light,tex,out) for formula fit. */
    {
        int mode = cr_cfg()->preview_diag;
        if (mode >= 3)
            preview_dump_fragments(tris, n, &skin, color, w, h);
    }

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
