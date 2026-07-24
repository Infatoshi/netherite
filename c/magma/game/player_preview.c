#include "game/player_preview.h"
#include "assets/hand_atlas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_MAX_TRIS 144
#define DEG2RAD 0.01745329251994329577f

typedef struct {
    int u, v;
    float x, y, z;
    int dx, dy, dz;
    float rx, ry, rz;
    float ax, ay, az;
    float inflate;
} PreviewPart;

typedef struct { int idx[4]; int u1, v1, u2, v2; } PreviewFace;
typedef struct { float x, y, z, u, v; } PreviewVertex;

/* ModelPlayer: ModelBiped base boxes plus the 64x64 skin's six wear layers. */
static const PreviewPart PLAYER_PARTS[] = {
    { 0,  0, -4,-8,-4, 8, 8,8,  0, 0,0, 0,0,0, 0.0f}, /* head */
    {16, 16, -4, 0,-2, 8,12,4,  0, 0,0, 0,0,0, 0.0f}, /* body */
    {40, 16, -3,-2,-2, 4,12,4, -5, 2,0, 0,0,0, 0.0f}, /* right arm */
    {32, 48, -1,-2,-2, 4,12,4,  5, 2,0, 0,0,0, 0.0f}, /* left arm */
    { 0, 16, -2, 0,-2, 4,12,4, -1.9f,12,0, 0,0,0, 0.0f},
    {16, 48, -2, 0,-2, 4,12,4,  1.9f,12,0, 0,0,0, 0.0f},
    {32,  0, -4,-8,-4, 8, 8,8,  0, 0,0, 0,0,0, 0.5f}, /* headwear */
    {16, 32, -4, 0,-2, 8,12,4,  0, 0,0, 0,0,0, 0.25f},
    {40, 32, -3,-2,-2, 4,12,4, -5, 2,0, 0,0,0, 0.25f},
    {48, 48, -1,-2,-2, 4,12,4,  5, 2,0, 0,0,0, 0.25f},
    { 0, 32, -2, 0,-2, 4,12,4, -1.9f,12,0, 0,0,0, 0.25f},
    { 0, 48, -2, 0,-2, 4,12,4,  1.9f,12,0, 0,0,0, 0.25f},
};

static void face_defs(int u, int v, int w, int h, int d, PreviewFace q[6])
{
    q[0] = (PreviewFace){{5,1,2,6}, u+d+w,   v+d, u+d+w+d,   v+d+h};
    q[1] = (PreviewFace){{0,4,7,3}, u,       v+d, u+d,       v+d+h};
    q[2] = (PreviewFace){{5,4,0,1}, u+d,     v,   u+d+w,     v+d};
    q[3] = (PreviewFace){{2,3,7,6}, u+d+w,   v+d, u+d+w+w,   v};
    q[4] = (PreviewFace){{1,0,3,2}, u+d,     v+d, u+d+w,     v+d+h};
    q[5] = (PreviewFace){{4,5,6,7}, u+d+w+d, v+d, u+d+w+d+w, v+d+h};
}

static void rotate_xyz(float *x, float *y, float *z, float ax, float ay, float az)
{
    float c = cosf(ax), s = sinf(ax);
    float ny = *y*c - *z*s, nz = *y*s + *z*c;
    *y = ny; *z = nz;
    c = cosf(ay); s = sinf(ay);
    float nx = *x*c + *z*s; nz = -*x*s + *z*c;
    *x = nx; *z = nz;
    c = cosf(az); s = sinf(az);
    nx = *x*c - *y*s; ny = *x*s + *y*c;
    *x = nx; *y = ny;
}

static CrScreenVert screen_vertex(PreviewVertex v, int cx, int bottom,
                                  float unit, float face_light)
{
    CrScreenVert out;
    memset(&out, 0, sizeof out);
    out.spos = (CrVec3){cx + v.x * unit, bottom - v.y * unit,
                        0.5f + v.z * 0.01f};
    out.invw = 1.0f;
    out.uv_w = (CrVec2){v.u / 64.0f, v.v / 64.0f};
    out.light_w = face_light;
    out.ao_w = 1.0f;
    out.tint_r_w = 255.0f;
    out.tint_g_w = 255.0f;
    out.tint_b_w = 255.0f;
    out.tint_a_w = 255.0f;
    return out;
}

static int emit_part(const PreviewPart *part, float body_yaw, float body_pitch,
                     int head, int cx, int bottom, float unit,
                     CrScreenTri *tris, int n)
{
    float x0 = part->x - part->inflate, x1 = part->x + part->dx + part->inflate;
    float y0 = part->y - part->inflate, y1 = part->y + part->dy + part->inflate;
    float z0 = part->z - part->inflate, z1 = part->z + part->dz + part->inflate;
    float corner[8][3] = {
        {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
        {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},
    };
    for (int i = 0; i < 8; ++i) {
        float x = corner[i][0], y = corner[i][1], z = corner[i][2];
        rotate_xyz(&x, &y, &z,
                   part->ax + (head ? body_pitch : 0.0f),
                   part->ay + (head ? body_yaw : 0.0f), part->az);
        x += part->rx; y += part->ry; z += part->rz;
        /* Model units -> feet-relative, Y up; the GUI matrix pitches the
         * entire entity after scaling, then RenderLivingBase applies body yaw. */
        y = 24.0f - y;
        rotate_xyz(&x, &y, &z, -body_pitch, body_yaw, 0.0f);
        corner[i][0] = x; corner[i][1] = y; corner[i][2] = z;
    }

    PreviewFace faces[6];
    face_defs(part->u, part->v, part->dx, part->dy, part->dz, faces);
    static const int tri_idx[2][3] = {{0,1,2},{0,2,3}};
    /* RenderHelper.enableStandardItemLighting: upward faces remain fullbright;
     * the vertical/bottom faces receive the two fixed GUI lights. */
    static const float face_light[6] = {0.38f,0.31f,1.0f,0.27f,0.46f,0.35f};
    for (int f = 0; f < 6 && n + 2 <= PREVIEW_MAX_TRIS; ++f) {
        float u[4] = {(float)faces[f].u2,(float)faces[f].u1,
                      (float)faces[f].u1,(float)faces[f].u2};
        float v[4] = {(float)faces[f].v1,(float)faces[f].v1,
                      (float)faces[f].v2,(float)faces[f].v2};
        PreviewVertex q[4];
        for (int k = 0; k < 4; ++k) {
            int c = faces[f].idx[k];
            q[k] = (PreviewVertex){corner[c][0],corner[c][1],corner[c][2],u[k],v[k]};
        }
        for (int t = 0; t < 2; ++t) {
            CrScreenVert a = screen_vertex(q[tri_idx[t][0]],cx,bottom,unit,face_light[f]);
            CrScreenVert b = screen_vertex(q[tri_idx[t][1]],cx,bottom,unit,face_light[f]);
            CrScreenVert c = screen_vertex(q[tri_idx[t][2]],cx,bottom,unit,face_light[f]);
            /* The rasterizer culls one winding. Pick the winding with positive
             * framebuffer signed area; depth still removes hidden faces. */
            float area = (b.spos.x-a.spos.x)*(c.spos.y-a.spos.y)
                       - (b.spos.y-a.spos.y)*(c.spos.x-a.spos.x);
            tris[n].v[0] = a;
            tris[n].v[1] = area > 0.0f ? c : b;
            tris[n].v[2] = area > 0.0f ? b : c;
            ++n;
        }
    }
    return n;
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
    CrFramebuffer local = {w,h,color,depth};

    float body_yaw = atanf(mouse_x / 40.0f) * 20.0f * DEG2RAD;
    float pitch = -atanf(mouse_y / 40.0f) * 20.0f * DEG2RAD;
    CrScreenTri tris[PREVIEW_MAX_TRIS];
    int n = 0;
    float unit = (28.5f / 16.0f) * ((float)h / 72.0f);
    for (int i = 0; i < (int)(sizeof PLAYER_PARTS / sizeof PLAYER_PARTS[0]); ++i)
        n = emit_part(&PLAYER_PARTS[i], body_yaw, pitch, i == 0 || i == 6,
                      w / 2 + h / 18, h * 68 / 72, unit, tris, n);

    CrTexture skin = {0};
    skin.w = HAND_SKIN_W; skin.h = HAND_SKIN_H;
    skin.texels = (const CrRgba *)HAND_SKIN_RGBA_STEVE;
    CrShadeCtx shade = {0};
    shade.atlas = &skin;
    shade.alpha_test = 1;
    shade.layer = CR_LAYER_CUTOUT;
    cr_raster_cpu(&local, tris, n, &shade);

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
