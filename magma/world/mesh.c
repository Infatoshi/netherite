/* magma world meshing: chunk voxels -> visible-face CrVertex triangle list.
 * Implements world_mesh_chunk() from world/world.h. For every solid block, emit
 * the 6 cube faces whose neighbour is non-occluding, two CCW-from-outside tris
 * per face, with directional face shade (light), 3-neighbour ambient occlusion
 * (ao), atlas-tile uv, and a biome-ish tint. World-space float positions. */
#include "world/world.h"
#include "world/blocks.h"

#include "chunk_provider.h"   /* CB_* */

/* One cube face template: outward normal, directional shade, the 4 corner offsets
 * (CCW seen from outside), and the two tangent AXES (0=X,1=Y,2=Z) used for AO. */
typedef struct {
    int   n[3];        /* outward normal */
    float light;       /* classic MC directional face shade */
    int   corner[4][3];/* corner offsets in {0,1}, CCW from outside */
    int   tu, tv;      /* tangent axis indices for AO neighbour lookup */
} Face;

static const Face FACES[6] = {
    /* -X */ { {-1, 0, 0}, 0.6f, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} }, 2, 1 },
    /* +X */ { { 1, 0, 0}, 0.6f, { {1,0,1},{1,0,0},{1,1,0},{1,1,1} }, 2, 1 },
    /* -Y */ { { 0,-1, 0}, 0.5f, { {0,0,0},{1,0,0},{1,0,1},{0,0,1} }, 0, 2 },
    /* +Y */ { { 0, 1, 0}, 1.0f, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} }, 0, 2 },
    /* -Z */ { { 0, 0,-1}, 0.8f, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} }, 0, 1 },
    /* +Z */ { { 0, 0, 1}, 0.8f, { {0,0,1},{1,0,1},{1,1,1},{0,1,1} }, 0, 1 },
};

/* per-corner tile-space uv (fills the whole tile across the face) */
static const float CORNER_UV[4][2] = { {0.0f,1.0f}, {1.0f,1.0f}, {1.0f,0.0f}, {0.0f,0.0f} };

static float ao_level(int side1, int side2, int corner) {
    /* standard voxel AO: fully occluded if both sides solid */
    int occ = (side1 && side2) ? 3 : (side1 + side2 + corner);
    switch (occ) {
        case 0:  return 1.0f;
        case 1:  return 0.8f;
        case 2:  return 0.6f;
        default: return 0.4f;
    }
}

static int opaque_at(const CrWorld *w, int wx, int wy, int wz) {
    return block_is_opaque(world_block(w, wx, wy, wz));
}

/* biome-ish tint: grass tops green, mushroom tops purple-ish, water/ice bluish. */
static CrRgba face_tint(int cb, int face) {
    CrRgba t; t.a = 255;
    if (face == 3 /* +Y top */ && cb == CB_GRASS) { t.r = 120; t.g = 190; t.b = 90;  return t; }
    if (face == 3 && cb == CB_MYCELIUM)            { t.r = 150; t.g = 120; t.b = 160; return t; }
    if (cb == CB_WATER || cb == CB_FLOWING_WATER)  { t.r = 150; t.g = 180; t.b = 255; return t; }
    if (cb == CB_ICE)                              { t.r = 190; t.g = 215; t.b = 255; return t; }
    t.r = t.g = t.b = 255;
    return t;
}

int world_mesh_chunk(CrWorld *w, int ccx, int ccz, CrVertex *out, int max) {
    if (!w) return 0;
    const int baseX = ccx * 16, baseZ = ccz * 16;
    int count = 0;

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            const int wx = baseX + lx, wz = baseZ + lz;
            for (int y = 0; y < 256; ++y) {
                int self = world_block(w, wx, y, wz);
                if (!block_is_solid(self)) continue;
                int self_opaque = block_is_opaque(self);

                for (int f = 0; f < 6; ++f) {
                    const Face *fc = &FACES[f];
                    int nx = wx + fc->n[0], ny = y + fc->n[1], nz = wz + fc->n[2];
                    int neigh = world_block(w, nx, ny, nz);
                    /* cull if neighbour occludes; don't draw internal faces between
                     * two identical transparent blocks (e.g. water against water). */
                    if (block_is_opaque(neigh)) continue;
                    if (!self_opaque && neigh == self) continue;

                    /* uv tile rect for this block */
                    int tile = block_tile(self);
                    int tcol = tile % BLK_ATLAS_COLS, trow = tile / BLK_ATLAS_COLS;
                    float u0 = (float)(tcol * BLK_TILE) / (float)BLK_ATLAS_W;
                    float u1 = (float)((tcol + 1) * BLK_TILE) / (float)BLK_ATLAS_W;
                    float v0 = (float)(trow * BLK_TILE) / (float)BLK_ATLAS_H;
                    float v1 = (float)((trow + 1) * BLK_TILE) / (float)BLK_ATLAS_H;

                    CrRgba tint = face_tint(self, f);

                    /* build the 4 face vertices */
                    CrVertex quad[4];
                    for (int c = 0; c < 4; ++c) {
                        const int *off = fc->corner[c];
                        quad[c].pos.x = (float)(wx + off[0]);
                        quad[c].pos.y = (float)(y  + off[1]);
                        quad[c].pos.z = (float)(wz + off[2]);
                        quad[c].uv.x = u0 + CORNER_UV[c][0] * (u1 - u0);
                        quad[c].uv.y = v0 + CORNER_UV[c][1] * (v1 - v0);
                        quad[c].light = fc->light;
                        quad[c].blk = 0.0f;
                        quad[c].tint = tint;

                        /* AO from the 3 neighbours touching this corner, in the
                         * outward (P+normal) layer, along the two tangent axes. */
                        int su = off[fc->tu] * 2 - 1;   /* -1 or +1 */
                        int sv = off[fc->tv] * 2 - 1;
                        int du[3] = {0,0,0}, dv[3] = {0,0,0};
                        du[fc->tu] = su; dv[fc->tv] = sv;
                        int s1 = opaque_at(w, nx + du[0], ny + du[1], nz + du[2]);
                        int s2 = opaque_at(w, nx + dv[0], ny + dv[1], nz + dv[2]);
                        int cr = opaque_at(w, nx + du[0] + dv[0], ny + du[1] + dv[1],
                                              nz + du[2] + dv[2]);
                        quad[c].ao = ao_level(s1, s2, cr);
                    }

                    /* two triangles: (0,1,2) and (0,2,3), preserving CCW winding */
                    static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
                    if (out) {
                        if (count + 6 > max) return count;   /* buffer full */
                        for (int k = 0; k < 6; ++k) out[count + k] = quad[TRI[k]];
                    }
                    count += 6;
                }
            }
        }
    }
    return count;
}
