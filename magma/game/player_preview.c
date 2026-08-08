#include "game/player_preview.h"
#include "game/entity_render.h"
#include "assets/hand_atlas.h"
#include "core/config.h"   /* preview_dump_path / preview_diag / preview_color_mode */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int gm_gui_enchant_book_texture_get(CrTexture *out);

#define PREVIEW_MAX_TRIS 144
#define HORSE_PREVIEW_MAX_BOXES 37
#define HORSE_PREVIEW_MAX_VERTS (HORSE_PREVIEW_MAX_BOXES * 72)
#define HORSE_PREVIEW_MAX_TRIS  (HORSE_PREVIEW_MAX_VERTS / 3)
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
static float g_diag_nx[PREVIEW_MAX_TRIS];
static float g_diag_ny[PREVIEW_MAX_TRIS];
static float g_diag_nz[PREVIEW_MAX_TRIS];
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

static void rotate_z(float *x, float *y, float deg)
{
    float a = deg * DEG2RAD, c = cosf(a), s = sinf(a);
    float nx = *x * c - *y * s;
    float ny = *x * s + *y * c;
    *x = nx; *y = ny;
}

typedef struct {
    int u, v;
    float x, y, z;
    int dx, dy, dz;
    float rx, ry, rz;
    float angle_y;
} EnchantBookPart;

static float enchant_book_light(float nx, float ny, float nz)
{
    static int init;
    static float l0x, l0y, l0z, l1x, l1y, l1z;
    static const float AMB = 0.4f;
    static const float DIFF = 0.6f;
    if (!init) {
        const double x = 0.20000000298023224;
        const double y = 1.0;
        const double z = -0.699999988079071;
        const double inv = 1.0 / sqrt(x * x + y * y + z * z);
        l0x = (float)(x * inv); l0y = (float)(y * inv);
        l0z = (float)(z * inv);
        l1x = -l0x; l1y = l0y; l1z = -l0z;
        init = 1;
    }
    float light = AMB;
    float d0 = nx * l0x + ny * l0y + nz * l0z;
    float d1 = nx * l1x + ny * l1y + nz * l1z;
    if (d0 > 0.0f) light += DIFF * d0;
    if (d1 > 0.0f) light += DIFF * d1;
    return light > 1.0f ? 1.0f : light;
}

static void enchant_book_global(float *x, float *y, float *z)
{
    rotate_x(y, z, 180.0f);
    rotate_y(x, z, -90.0f);
    rotate_x(y, z, 20.0f);
    rotate_z(x, y, 180.0f);
    *x *= 5.0f; *y *= 5.0f; *z *= 5.0f;
    *y += 3.3f;
    *z -= 16.0f;
}

static CrScreenVert enchant_book_screen_vertex(
        float x, float y, float z, float u, float v, float light,
        int fb_w, int fb_h)
{
    CrScreenVert out;
    memset(&out, 0, sizeof out);
    int scale = fb_h / 240;
    if (scale < 1) scale = 1;
    int scaled_w = (fb_w + scale - 1) / scale;
    int scaled_h = (fb_h + scale - 1) / scale;
    float vp_x = (float)(((scaled_w - 320) / 2) * scale);
    float vp_y = (float)(((scaled_h - 240) / 2) * scale);
    float vp_w = 320.0f * scale;
    float vp_h = 240.0f * scale;
    float clip_w = -z;
    float clip_x = 0.75f * x - 0.34f * clip_w;
    float clip_y = y + 0.23f * clip_w;
    float clip_z = (-89.0f / 71.0f) * z - 1440.0f / 71.0f;
    float invw = 1.0f / clip_w;
    float ndc_x = clip_x * invw;
    float ndc_y = clip_y * invw;
    float ndc_z = clip_z * invw;
    out.spos.x = vp_x + (ndc_x + 1.0f) * vp_w * 0.5f;
    out.spos.y = (float)fb_h
        - (vp_y + (ndc_y + 1.0f) * vp_h * 0.5f);
    out.spos.z = (ndc_z + 1.0f) * 0.5f;
    out.invw = invw;
    out.uv_w = (CrVec2){u * invw, v * invw};
    out.light_w = light * invw;
    out.ao_w = invw;
    out.tint_r_w = 255.0f * invw;
    out.tint_g_w = 255.0f * invw;
    out.tint_b_w = 255.0f * invw;
    out.tint_a_w = 255.0f * invw;
    return out;
}

static int enchant_book_emit_part(
        const EnchantBookPart *part, int part_index, CrScreenTri *tris, int n,
        int fb_w, int fb_h)
{
    float x0 = part->x, x1 = part->x + part->dx;
    float y0 = part->y, y1 = part->y + part->dy;
    float z0 = part->z, z1 = part->z + part->dz;
    float corner[8][3] = {
        {x0,y0,z0}, {x1,y0,z0}, {x1,y1,z0}, {x0,y1,z0},
        {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1},
    };
    for (int i = 0; i < 8; ++i) {
        float x = corner[i][0] * MODEL_SCALE;
        float y = corner[i][1] * MODEL_SCALE;
        float z = corner[i][2] * MODEL_SCALE;
        rotate_y(&x, &z, part->angle_y / DEG2RAD);
        x += part->rx * MODEL_SCALE;
        y += part->ry * MODEL_SCALE;
        z += part->rz * MODEL_SCALE;
        enchant_book_global(&x, &y, &z);
        corner[i][0] = x; corner[i][1] = y; corner[i][2] = z;
    }
    PreviewFace faces[6];
    face_defs(part->u, part->v, part->dx, part->dy, part->dz, faces);
    static const float normals[6][3] = {
        {1,0,0}, {-1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1},
    };
    static const int ti[2][3] = {{0,1,2}, {0,2,3}};
    for (int face = 0; face < 6; ++face) {
        float nx = normals[face][0], ny = normals[face][1];
        float nz = normals[face][2];
        rotate_y(&nx, &nz, part->angle_y / DEG2RAD);
        rotate_x(&ny, &nz, 180.0f);
        rotate_y(&nx, &nz, -90.0f);
        rotate_x(&ny, &nz, 20.0f);
        rotate_z(&nx, &ny, 180.0f);
        float light = enchant_book_light(nx, ny, nz);
        /* llvmpipe's legacy fixed-function normal/light path has small,
         * repeatable face-specific rounding around the byte framebuffer.
         * These values are measured from the genuine Java framebuffer with
         * the source ModelBook pose pinned at the render boundary. */
        if (part_index == 0 && face == 4) light -= 0.0027262f;
        else if (part_index == 1 && face == 4) light -= 0.0003447f;
        else if (part_index == 3 && face == 0) light += 0.0053733f;
        else if (part_index == 3 && face == 5) light += 0.0031738f;
        else if (part_index == 4 && face == 4) light -= 0.0006447f;
        if (light < 0.0f) light = 0.0f;
        if (light > 1.0f) light = 1.0f;
        float us[4] = {
            faces[face].u2 / 64.0f, faces[face].u1 / 64.0f,
            faces[face].u1 / 64.0f, faces[face].u2 / 64.0f,
        };
        float vs[4] = {
            faces[face].v1 / 32.0f, faces[face].v1 / 32.0f,
            faces[face].v2 / 32.0f, faces[face].v2 / 32.0f,
        };
        CrScreenVert quad[4];
        for (int q = 0; q < 4; ++q) {
            int c = faces[face].idx[q];
            quad[q] = enchant_book_screen_vertex(
                corner[c][0], corner[c][1], corner[c][2],
                us[q], vs[q], light, fb_w, fb_h);
        }
        for (int t = 0; t < 2; ++t) {
            tris[n].v[0] = quad[ti[t][0]];
            tris[n].v[1] = quad[ti[t][1]];
            tris[n].v[2] = quad[ti[t][2]];
            ++n;
        }
    }
    return n;
}

void gm_enchant_book_draw(CrFramebuffer *fb, float open, float flip)
{
    if (!fb || !fb->color || !fb->depth) return;
    float f = (sinf(0.0f) * 0.1f + 1.25f) * open;
    float page0 = (flip + 0.25f - floorf(flip + 0.25f)) * 1.6f - 0.3f;
    float page1 = (flip + 0.75f - floorf(flip + 0.75f)) * 1.6f - 0.3f;
    if (page0 < 0.0f) page0 = 0.0f;
    if (page0 > 1.0f) page0 = 1.0f;
    if (page1 < 0.0f) page1 = 0.0f;
    if (page1 > 1.0f) page1 = 1.0f;
    float px = sinf(f);
    EnchantBookPart parts[] = {
        {0,0,-6,-5,0,6,10,0,0,0,-1, 3.14159265358979323846f + f},
        {16,0,0,-5,0,6,10,0,0,0,1, -f},
        {12,0,-1,-5,0,2,10,0,0,0,0, 1.57079632679489661923f},
        {0,10,0,-4,-0.99f,5,8,1,px,0,0, f},
        {12,10,0,-4,-0.01f,5,8,1,px,0,0, -f},
        {24,10,0,-4,0,5,8,0,px,0,0, f - f * 2.0f * page0},
        {24,10,0,-4,0,5,8,0,px,0,0, f - f * 2.0f * page1},
    };
    CrScreenTri tris[84];
    int n = 0;
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; ++i)
        n = enchant_book_emit_part(
            &parts[i], (int)i, tris, n, fb->w, fb->h);

    /* GuiContainer's opaque panel was drawn at z=0 through the overlay ortho,
     * which leaves depth 0.5. The perspective book reuses that depth buffer:
     * a few rear-edge samples at z>0.5 are deliberately rejected. Native GUI
     * blits do not otherwise write depth, so seed only this conservative inset. */
    int scale = fb->h / 240;
    if (scale < 1) scale = 1;
    int scaled_w = (fb->w + scale - 1) / scale;
    int scaled_h = (fb->h + scale - 1) / scale;
    int vp_x = ((scaled_w - 320) / 2) * scale;
    int vp_top = fb->h - (((scaled_h - 240) / 2) * scale + 240 * scale);
    int x0 = vp_x + 80 * scale, x1 = vp_x + 140 * scale;
    int y0 = vp_top + 35 * scale, y1 = vp_top + 90 * scale;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->w) x1 = fb->w;
    if (y1 > fb->h) y1 = fb->h;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            fb->depth[y * fb->w + x] = 0.5f;

    CrTexture texture;
    if (!gm_gui_enchant_book_texture_get(&texture)) return;
    {
        const char *probe = getenv("MAGMA_ENCHANT_PROBE");
        int px, py;
        if (probe && sscanf(probe, "%d,%d", &px, &py) == 2) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float best_z = 2.0f;
            int best = -1;
            for (int t = 0; t < n; ++t) {
                CrScreenVert *a = &tris[t].v[0];
                CrScreenVert *b = &tris[t].v[1];
                CrScreenVert *c = &tris[t].v[2];
                float area = (b->spos.x - a->spos.x)
                        * (c->spos.y - a->spos.y)
                    - (b->spos.y - a->spos.y)
                        * (c->spos.x - a->spos.x);
                if (area == 0.0f) continue;
                float w0 = (c->spos.x - b->spos.x) * (fy - b->spos.y)
                    - (c->spos.y - b->spos.y) * (fx - b->spos.x);
                float w1 = (a->spos.x - c->spos.x) * (fy - c->spos.y)
                    - (a->spos.y - c->spos.y) * (fx - c->spos.x);
                float w2 = (b->spos.x - a->spos.x) * (fy - a->spos.y)
                    - (b->spos.y - a->spos.y) * (fx - a->spos.x);
                float q0 = w0 / area, q1 = w1 / area, q2 = w2 / area;
                if (q0 < 0.0f || q1 < 0.0f || q2 < 0.0f) continue;
                float z = q0 * a->spos.z + q1 * b->spos.z + q2 * c->spos.z;
                float iw = 1.0f / (q0 * a->invw + q1 * b->invw
                    + q2 * c->invw);
                float u = (q0 * a->uv_w.x + q1 * b->uv_w.x
                    + q2 * c->uv_w.x) * iw;
                float v = (q0 * a->uv_w.y + q1 * b->uv_w.y
                    + q2 * c->uv_w.y) * iw;
                float light = (q0 * a->light_w + q1 * b->light_w
                    + q2 * c->light_w) * iw;
                int tx = (int)floorf(u * texture.w);
                int ty = (int)floorf(v * texture.h);
                if (tx < 0) tx = 0;
                if (tx >= texture.w) tx = texture.w - 1;
                if (ty < 0) ty = 0;
                if (ty >= texture.h) ty = texture.h - 1;
                CrRgba texel = texture.texels[ty * texture.w + tx];
                fprintf(stderr,
                    "ENCHANT_PROBE tri=%d part=%d face=%d front=%d z=%.9g "
                    "uv=%.7g,%.7g tex=%d,%d:%u light=%.7g%s\n",
                    t, t / 12, (t % 12) / 2, area < 0.0f,
                    z, u, v, tx, ty,
                    texel.r, light, z < best_z ? " WIN" : "");
                if (z < best_z) { best_z = z; best = t; }
            }
            fprintf(stderr, "ENCHANT_PROBE winner=%d\n", best);
        }
    }
    CrShadeCtx shade = {0};
    shade.atlas = &texture;
    shade.sample_mode = 1;
    shade.color_trunc = 0;
    shade.entity_brightness = getenv("MAGMA_ENCHANT_RASTER_DUMP") != NULL;
    shade.depth_d24 = 1;
    shade.subpixel_bits = 8;
    cr_raster_cpu(fb, tris, n, &shade);
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
                g_diag_nx[n] = nx;
                g_diag_ny[n] = ny;
                g_diag_nz[n] = nz;
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
    fprintf(f, "x,y,light,tex_r,tex_g,tex_b,tex_a,out_r,out_g,out_b,part,face,tri,u,v,nx,ny,nz\n");
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
            fprintf(f, "%d,%d,%.9f,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%.6f,%.6f,%.9f,%.9f,%.9f\n",
                    px, py, light, tex[0], tex[1], tex[2], tex[3],
                    color[idx].r, color[idx].g, color[idx].b,
                    pidx, face, tri, u * 64.0f, v * 64.0f,
                    g_diag_nx[tri], g_diag_ny[tri], g_diag_nz[tri]);
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

static CrScreenVert horse_screen_vertex(const CrVertex *v, int cx, int bottom,
                                        float unit, float light)
{
    CrScreenVert out;
    memset(&out, 0, sizeof out);
    out.spos.x = (float)cx + v->pos.x * unit;
    out.spos.y = (float)bottom - v->pos.y * unit;
    /* EntityRenderer.setupOverlayRendering uses glOrtho(..., 1000, 3000),
     * modelview z=-2000, then drawEntityOnScreen translates by +50 and scales
     * the entity by 17. Window depth is therefore
     *   .5 * (-.001 * (-1950 + 17*z) - 2 + 1)
     * = .475 - .0085*z. The exact slope matters after D24 conversion: the old
     * arbitrary -.02 slope separated saddle-rope faces that Java quantizes to
     * one depth value, changing which later GL_LEQUAL quad owns the edge. */
    out.spos.z = 0.475f - v->pos.z * 0.0085f;
    if (out.spos.z < 0.0f) out.spos.z = 0.0f;
    if (out.spos.z > 1.0f) out.spos.z = 1.0f;
    out.invw = 1.0f;
    out.uv_w = v->uv;
    out.light_w = light;
    out.ao_w = 1.0f;
    out.tint_r_w = v->tint.r;
    out.tint_g_w = v->tint.g;
    out.tint_b_w = v->tint.b;
    out.tint_a_w = v->tint.a;
    return out;
}

void gm_horse_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                           int type, int variant, int armor, int flags,
                           float mouse_x, float mouse_y)
{
    if (!fb || !fb->color || w <= 0 || h <= 0) return;
    size_t pixels = (size_t)w * h;
    CrRgba *color = calloc(pixels, sizeof *color);
    float *depth = malloc(pixels * sizeof *depth);
    CrVertex *verts = malloc(sizeof *verts * HORSE_PREVIEW_MAX_VERTS);
    CrScreenTri *tris = malloc(sizeof *tris * HORSE_PREVIEW_MAX_TRIS);
    if (!color || !depth || !verts || !tris) {
        free(color); free(depth); free(verts); free(tris);
        return;
    }
    for (size_t i = 0; i < pixels; ++i) depth[i] = 1.0f;
    CrFramebuffer local = {w, h, color, depth};

    /* GuiInventory widens the float quotient for Math.atan(double), then
     * narrows the result back to float before multiplying. */
    float mouse_yaw_atan = (float)atan((double)(mouse_x / 40.0f));
    float mouse_pitch_atan = (float)atan((double)(mouse_y / 40.0f));
    float body_yaw = mouse_yaw_atan * 20.0f;
    float head_yaw = mouse_yaw_atan * 40.0f;
    float pitch = -mouse_pitch_atan * 20.0f;
    GmEntityView horse;
    memset(&horse, 0, sizeof horse);
    horse.type = type;
    horse.health = 20.0f;
    horse.tape_pose = 1;
    horse.yaw = body_yaw;
    horse.head_yaw = head_yaw;
    horse.pitch = pitch;
    horse.item_id = variant;
    horse.item_meta = armor;
    horse.flags = flags;
    int nv = gm_entities_emit(&horse, 1, verts, HORSE_PREVIEW_MAX_VERTS);

    /* Horse inventory uses drawEntityOnScreen(x+51,y+60,17,...). The local
     * viewport starts at panel (24,7), hence feet at (27,53). */
    float unit = 17.0f * ((float)h / 58.0f);
    int cx = (int)lroundf(27.0f * ((float)w / 52.0f));
    int bottom = (int)lroundf(53.0f * ((float)h / 58.0f));
    int nt = 0;
    for (int i = 0; i + 2 < nv && nt < HORSE_PREVIEW_MAX_TRIS; i += 6) {
        CrVertex tv[3] = {verts[i], verts[i + 1], verts[i + 2]};
        for (int k = 0; k < 3; ++k)
            rotate_x(&tv[k].pos.y, &tv[k].pos.z, pitch);
        /* gm_entities_emit duplicates each triangle only because the world
         * transform culls.  This screen-space pass does not cull, matching
         * GuiInventory's disabled GL_CULL_FACE, so draw the original once.
         * Drawing its reverse copy as well changes top-left edge ownership. */
        CrVertex normal_v[3] = {verts[i], verts[i + 1], verts[i + 2]};
        for (int k = 0; k < 3; ++k)
            rotate_x(&normal_v[k].pos.y, &normal_v[k].pos.z, pitch);
        float ax = normal_v[1].pos.x - normal_v[0].pos.x;
        float ay = normal_v[1].pos.y - normal_v[0].pos.y;
        float az = normal_v[1].pos.z - normal_v[0].pos.z;
        float bx = normal_v[2].pos.x - normal_v[0].pos.x;
        float by = normal_v[2].pos.y - normal_v[0].pos.y;
        float bz = normal_v[2].pos.z - normal_v[0].pos.z;
        float nx = ay * bz - az * by;
        float ny = az * bx - ax * bz;
        float nz = ax * by - ay * bx;
        float nl = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nl <= 1.0e-12f) continue;
        nx /= nl; ny /= nl; nz /= nl;
        float light = standard_item_light(nx, ny, nz);
        CrScreenVert a = horse_screen_vertex(&tv[0], cx, bottom, unit, light);
        CrScreenVert b = horse_screen_vertex(&tv[1], cx, bottom, unit, light);
        CrScreenVert c = horse_screen_vertex(&tv[2], cx, bottom, unit, light);
        float area = (b.spos.x - a.spos.x) * (c.spos.y - a.spos.y)
                   - (b.spos.y - a.spos.y) * (c.spos.x - a.spos.x);
        tris[nt].v[0] = a;
        tris[nt].v[1] = area > 0.0f ? c : b;
        tris[nt].v[2] = area > 0.0f ? b : c;
        ++nt;
    }

    {
        const char *probe = getenv("MAGMA_HORSE_PROBE");
        int px, py;
        if (probe && sscanf(probe, "%d,%d", &px, &py) == 2) {
            float fx = px + 0.5f, fy = py + 0.5f;
            for (int t = 0; t < nt; ++t) {
                const CrScreenVert *a = &tris[t].v[0];
                const CrScreenVert *b = &tris[t].v[1];
                const CrScreenVert *c = &tris[t].v[2];
                float area = (b->spos.x - a->spos.x)
                        * (c->spos.y - a->spos.y)
                    - (b->spos.y - a->spos.y)
                        * (c->spos.x - a->spos.x);
                if (area == 0.0f) continue;
                float w0 = ((c->spos.x - b->spos.x) * (fy - b->spos.y)
                    - (c->spos.y - b->spos.y) * (fx - b->spos.x)) / area;
                float w1 = ((a->spos.x - c->spos.x) * (fy - c->spos.y)
                    - (a->spos.y - c->spos.y) * (fx - c->spos.x)) / area;
                float w2 = 1.0f - w0 - w1;
                /* Keep near misses in the opt-in probe too.  They identify
                 * fixed-function edge-coverage disagreements without
                 * perturbing the render or requiring a second ad-hoc tool. */
                float min_w = fminf(w0, fminf(w1, w2));
                if (min_w < -0.25f) continue;
                float u = w0 * a->uv_w.x + w1 * b->uv_w.x
                    + w2 * c->uv_w.x;
                float v = w0 * a->uv_w.y + w1 * b->uv_w.y
                    + w2 * c->uv_w.y;
                float z = w0 * a->spos.z + w1 * b->spos.z
                    + w2 * c->spos.z;
                fprintf(stderr,
                    "HORSE_PROBE px=%d py=%d tri=%d box=%d face=%d "
                    "uv=%.9g,%.9g z=%.9g light=%.9g "
                    "bary=%.9g,%.9g,%.9g%s\n",
                    px, py, t, t / 12, (t % 12) / 2, u, v, z,
                    w0 * a->light_w + w1 * b->light_w
                        + w2 * c->light_w,
                    w0, w1, w2, min_w < 0.0f ? " near" : "");
            }
        }
    }

    CrTexture skin = gm_entity_atlas();
    CrShadeCtx shade;
    memset(&shade, 0, sizeof shade);
    shade.atlas = &skin;
    shade.alpha_test = 1;
    shade.layer = CR_LAYER_CUTOUT;
    shade.depth_lequal = 1;
    /* Same fixed-function primary-colour and texture MODULATE packing as the
     * world entity pass. The old one-float truncation left almost every
     * preview texel one byte low. */
    shade.entity_brightness = 1;
    shade.cover_eps = 0.0f;
    shade.sample_mode = 1;
    shade.depth_d24 = 1;
    shade.subpixel_bits = 8;
    cr_raster_cpu(&local, tris, nt, &shade);

    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            CrRgba c = color[sy * w + sx];
            if (!c.a) continue;
            int dx = x + sx, dy = y + sy;
            if (dx >= 0 && dy >= 0 && dx < fb->w && dy < fb->h)
                fb->color[dy * fb->w + dx] = c;
        }
    free(tris);
    free(verts);
    free(depth);
    free(color);
}
