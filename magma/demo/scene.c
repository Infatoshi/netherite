/* magma demo scene: unit cube geometry + procedural debug atlas.
 * Implements demo/scene.h. Owned by the SCENE agent. */
#include "demo/scene.h"

/* One cube face = 4 corners (CCW when viewed from OUTSIDE) triangulated as
 * (0,1,2) and (0,2,3). Corner order maps to uv (0,0),(1,0),(1,1),(0,1) so each
 * face samples exactly one atlas tile spanning [0,1]. */
static const float FACE_POS[6][4][3] = {
    /* +Z front  (outward normal +z) */
    { {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f} },
    /* -Z back   (outward normal -z) */
    { { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f} },
    /* +X right  (outward normal +x) */
    { { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f} },
    /* -X left   (outward normal -x) */
    { {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f} },
    /* +Y top    (outward normal +y) */
    { {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f} },
    /* -Y bottom (outward normal -y) */
    { {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f} },
};

/* uv per corner, same order as FACE_POS corners; one full atlas tile in [0,1]. */
static const float CORNER_UV[4][2] = {
    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
};

/* corner index sequence for the two triangles of a quad */
static const int TRI_IDX[6] = { 0, 1, 2, 0, 2, 3 };

int scene_cube(CrVertex *verts, int max)
{
    if (max < 36)
        return 0;

    int n = 0;
    for (int f = 0; f < 6; f++) {
        for (int t = 0; t < 6; t++) {
            int c = TRI_IDX[t];
            CrVertex v;
            v.pos.x = FACE_POS[f][c][0];
            v.pos.y = FACE_POS[f][c][1];
            v.pos.z = FACE_POS[f][c][2];
            v.uv.x  = CORNER_UV[c][0];
            v.uv.y  = CORNER_UV[c][1];
            v.light = 1.0f;
            v.ao    = 1.0f;
            v.tint.r = 255; v.tint.g = 255; v.tint.b = 255; v.tint.a = 255;
            verts[n++] = v;
        }
    }
    return n; /* 36 */
}

/* Procedural debug atlas: 16x16, one tile. Each tile is a 4x4-texel checker of
 * two colors with a distinct 1px border, so a rendered face reveals orientation
 * and uv mapping at a glance. Byte order R,G,B,A. */
#define ATLAS_W    16
#define ATLAS_H    16
#define ATLAS_TILE 16

CrTexture scene_atlas(void)
{
    static CrRgba texels[ATLAS_W * ATLAS_H];
    static int built = 0;

    if (!built) {
        const CrRgba c0     = {  40,  40,  48, 255 }; /* dark checker  */
        const CrRgba c1     = { 210, 180,  60, 255 }; /* light checker */
        const CrRgba border = { 220,  40,  40, 255 }; /* red 1px frame */

        for (int y = 0; y < ATLAS_H; y++) {
            for (int x = 0; x < ATLAS_W; x++) {
                int tx = x % ATLAS_TILE;
                int ty = y % ATLAS_TILE;
                CrRgba col;
                if (tx == 0 || ty == 0 || tx == ATLAS_TILE - 1 || ty == ATLAS_TILE - 1) {
                    col = border;
                } else {
                    int checker = ((tx / 4) + (ty / 4)) & 1;
                    col = checker ? c1 : c0;
                }
                texels[y * ATLAS_W + x] = col;
            }
        }
        built = 1;
    }

    CrTexture tex;
    tex.w      = ATLAS_W;
    tex.h      = ATLAS_H;
    tex.texels = texels;
    tex.tile   = ATLAS_TILE;
    return tex;
}
