/* world/mesh_mc.c - MC-faithful chunk mesher.
 *
 * Composes the wave-3 modules (world/light.h + assets/blockmodels.h) into
 * per-render-layer CrVertex triangle lists for one 16x256x16 chunk. World-space
 * float positions feed cr_transform + cr_raster_* directly.
 *
 * For each non-air block we emit the (up to) 6 cube faces whose neighbour does
 * not occlude, as two CCW-from-outside triangles (front-facing under the
 * rasterizer's CR_FRONT_SIGN convention). Per-vertex we bake: atlas-tile uv from
 * bm_sprite_uv, combined sky/block light * MC directional face shade, 3-neighbour
 * ambient occlusion, and the face's biome tint class (grass/foliage/water) or
 * white.
 */
#include "world/mesh_mc.h"
#include "world/light.h"
#include "assets/blockmodels.h"
#include "renderkernels/rk.h"   /* facebakery kernels 31-34 (bake non-cube quads) */
#include "core/config.h"        /* cr_cfg()->ao (MAGMA_SMOOTH stays env; deferred) */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Meshing mode for push_face: when g_allow_grow is 0 the per-layer buffer is a
 * caller-provided fixed slab (worldmc_mesh_chunk_into) and an overflow is a hard
 * error (loud cap-too-small beats silent corruption); when 1 push_face realloc-
 * grows a self-owned buffer (worldmc_mesh_chunk, used by the unit test). */
static int g_allow_grow = 1;
static int g_emit_fire_mesh = 1;

void worldmc_set_fire_mesh_enabled(int enabled) {
    g_emit_fire_mesh = enabled != 0;
}

struct CrWorldMC {
    CrLight *light;
};

/* magma-local dimension id from assets/blockmodels.c. Vanilla BlockMagma
 * overrides getPackedLightmapCoords with 15728880 (sky=15, block=15). */
#define CR_CB_MAGMA 220
#define CR_CB_IRON_BARS 221
#define CR_CB_GLASS_PANE 253
#define CR_CB_GLASS 200
#define CR_CB_TEST_FENCE 203
#define CR_CB_WEB 230
#define CR_CB_COBBLESTONE_WALL 233
#define CR_CB_GRASS 3
#define CR_CB_SLIME 229
/* weighted-variant cubes (ids from assets/blockmodels.c) */
#define CR_CB_STONE 1
#define CR_CB_DIRT 4
#define CR_CB_BEDROCK 5
#define CR_CB_SAND 7
#define CR_CB_NETHERRACK 210
#define CR_CB_TALLGRASS 39
#define CR_CB_FERN 40
#define CR_CB_YELLOW_FLOWER 50
#define CR_CB_RED_FLOWER_LAST 59
#define CR_CB_DPLANT_LOWER_BASE 60
#define CR_CB_DPLANT_UPPER 66
#define CR_CB_DPLANT_LAST 66
#define CR_CB_DPLANT_UPPER_CONTEXT_BASE 257
#define CR_CB_DPLANT_UPPER_CONTEXT_LAST 262

/* BlockDoublePlant.getActualState copies VARIANT from the lower half into an
 * upper half. Legacy upper metadata contains only HALF/FACING, so choose the
 * upper render model from the canonical state below at mesh time. */
static int contextual_model_key(const CrLight *L, int cb,
                                int wx, int wy, int wz) {
    if (cb != CR_CB_DPLANT_LAST || wy <= 0) return cb;
    uint16_t state = light_state(L, wx, wy, wz);
    if ((state >> 4) != 175 || !(state & 8)) return cb;
    uint16_t below = light_state(L, wx, wy - 1, wz);
    if ((below >> 4) != 175 || (below & 8)) return cb;
    int variant = below & 7;
    return variant >= 1 && variant <= 5
        ? CR_CB_DPLANT_UPPER_CONTEXT_BASE + variant : cb;
}

static int is_double_plant_model(int cb) {
    return (cb >= 60 && cb <= CR_CB_DPLANT_LAST) ||
           (cb > CR_CB_DPLANT_UPPER_CONTEXT_BASE &&
            cb <= CR_CB_DPLANT_UPPER_CONTEXT_LAST);
}

/* One cube face template, indexed by EnumFacing (0=DOWN 1=UP 2=NORTH 3=SOUTH
 * 4=WEST 5=EAST, matching BM_DOWN..BM_EAST). Holds the outward normal, the MC
 * directional face-shade multiplier, the 4 corner offsets (in {0,1}, CCW seen
 * from outside), and the two tangent axis indices (0=X 1=Y 2=Z) used for the
 * 3-neighbour ambient occlusion lookup. Corner order matches CORNER_UV below.
 * Adapted from world/mesh.c, reordered to EnumFacing. */
typedef struct {
    int   n[3];
    float shade;
    int   corner[4][3];
    int   tu, tv;
} Face;

static const Face FACES[6] = {
    /* 0 DOWN  -Y */ { { 0,-1, 0}, 0.5f, { {0,0,0},{1,0,0},{1,0,1},{0,0,1} }, 0, 2 },
    /* 1 UP    +Y */ { { 0, 1, 0}, 1.0f, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} }, 0, 2 },
    /* 2 NORTH -Z */ { { 0, 0,-1}, 0.8f, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} }, 0, 1 },
    /* 3 SOUTH +Z */ { { 0, 0, 1}, 0.8f, { {0,0,1},{1,0,1},{1,1,1},{0,1,1} }, 0, 1 },
    /* 4 WEST  -X */ { {-1, 0, 0}, 0.6f, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} }, 2, 1 },
    /* 5 EAST  +X */ { { 1, 0, 0}, 0.6f, { {1,0,1},{1,0,0},{1,1,0},{1,1,1} }, 2, 1 },
};

/* Full-cube faces are baked via FaceBakery (bake_face / rk_facebakery_make_quad)
 * so corner UVs match MC including the 0.999/0.001 edge inset. The old
 * CORNER_UV_FACE table was facebakery-derived and is now unused. */

/* two triangles per quad: (0,1,2) and (0,2,3), preserving the CCW winding */
static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };

/* Opt-in via MAGMA_SMOOTH=1. Maps to Java ao:2 smooth path. Default OFF
 * matches hard-scene / options ao:0 (BlockModelRenderer.renderModelFlat). */
static int smooth_light_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *s = getenv("MAGMA_SMOOTH");
        cached = (s && atoi(s) != 0) ? 1 : 0;
    }
    return cached;
}

/* Java ao:0 flat quads: no ambient-occlusion darkening (vertex ao == 1).
 * Legacy magma always applied 3-neighbour ao_level even with smooth off,
 * which systematically darkened canopy corners vs Fast goldens.
 * ao=1 re-enables vertex AO without smooth light averaging. */
static int vertex_ao_enabled(void) {
    if (smooth_light_enabled()) return 1;
    static int cached = -1;
    if (cached < 0) cached = cr_cfg()->ao ? 1 : 0;
    return cached;
}

/* standard voxel AO: {0.4,0.6,0.8,1.0} from the 3 blocks touching a corner. */
static float ao_level(int side1, int side2, int corner) {
    int occ = (side1 && side2) ? 3 : (side1 + side2 + corner);
    switch (occ) {
        case 0:  return 1.0f;
        case 1:  return 0.8f;
        case 2:  return 0.6f;
        default: return 0.4f;
    }
}

/* MC BlockModelRenderer AO: leaves (cutout-mipped cubes) count as occluders
 * even though is_full_cube==0. */
static int mc_ao_normal_cube_at(const CrLight *L, int wx, int wy, int wz) {
    const BmBlock *m = bm_block(light_block(L, wx, wy, wz));
    return m->is_full_cube ||
           (m->kind == BM_KIND_CUBE && m->layer == CR_LAYER_CUTOUT_MIPPED);
}

/* Occupancy used by corner AO (must match MC's "normal cube" for AO). */
static int ao_occludes_at(const CrLight *L, int wx, int wy, int wz) {
    return mc_ao_normal_cube_at(L, wx, wy, wz);
}

static float mc_ao_light_value_at(const CrLight *L, int wx, int wy, int wz) {
    return mc_ao_normal_cube_at(L, wx, wy, wz) ? 0.2f : 1.0f;
}

static int mc_translucent_at(const CrLight *L, int wx, int wy, int wz) {
    const BmBlock *m = bm_block(light_block(L, wx, wy, wz));
    return m->is_air || m->kind == BM_KIND_CROSS ||
           m->kind == BM_KIND_DPLANT_SUNFLOWER_TOP ||
           (m->kind == BM_KIND_CUBE && !m->is_full_cube && m->layer == CR_LAYER_SOLID);
}

/* 0xRRGGBB -> CrRgba (opaque). */
static CrRgba rgb_to_rgba(int c) {
    CrRgba t;
    t.r = (u8)((c >> 16) & 0xff);
    t.g = (u8)((c >> 8) & 0xff);
    t.b = (u8)(c & 0xff);
    t.a = 255;
    return t;
}

static CrRgba face_tint(const CrLight *L, int tint_class, int wx, int wy, int wz) {
    switch (tint_class) {
        case BM_TINT_GRASS:   return rgb_to_rgba(light_grass_color(L, wx, wy, wz));
        case BM_TINT_FOLIAGE: return rgb_to_rgba(light_foliage_color(L, wx, wy, wz));
        case BM_TINT_WATER:   return rgb_to_rgba(light_water_color(L, wx, wz));
        case BM_TINT_LILY: {
            /* BlockColors WATERLILY in-world: 2129968 = 0x208030 */
            CrRgba t = { 0x20, 0x80, 0x30, 255 };
            return t;
        }
        /* BlockColors leaves: spruce/birch use fixed ColorizerFoliage pine/birch
         * (6396257 / 8431445), not the biome foliage colormap. */
        case BM_TINT_FOLIAGE_PINE:  return rgb_to_rgba(6396257);
        case BM_TINT_FOLIAGE_BIRCH: return rgb_to_rgba(8431445);
        default: { CrRgba w = {255,255,255,255}; return w; }
    }
}

/* Append 6 verts to the layer's buffer, growing capacity geometrically. */
static void push_face(CrChunkMeshMC *out, int *cap, int layer, const CrVertex *quad) {
    if (layer < 0 || layer > 3) return;
    if (out->nverts[layer] + 6 > cap[layer]) {
        if (!g_allow_grow) {
            fprintf(stderr,
                "[mesh_mc] FATAL: layer %d exceeds fixed cap %d verts "
                "(CR_MAX_VERTS_PER_CHUNK too small for this chunk)\n", layer, cap[layer]);
            assert(0 && "worldmc_mesh_chunk_into: per-layer vertex cap exceeded");
            abort();
        }
        int ncap = cap[layer] ? cap[layer] * 2 : 4096;
        while (out->nverts[layer] + 6 > ncap) ncap *= 2;
        out->verts[layer] = (CrVertex *)realloc(out->verts[layer], (size_t)ncap * sizeof(CrVertex));
        cap[layer] = ncap;
    }
    CrVertex *dst = out->verts[layer] + out->nverts[layer];
    for (int k = 0; k < 6; ++k) dst[k] = quad[TRI[k]];
    out->nverts[layer] += 6;
}

/* =============== non-cube models (facebakery kernels 31-34) =============== */
/* Every non-cube quad below is baked by rk_facebakery_make_quad (FaceBakery.
 * makeBakedQuad), the render-opt kernel that is bit-verified against real
 * Minecraft. We feed it model-space from/to bounds (0..16), a facing, the sprite
 * atlas UV rect, and (for cross-plants) the 45deg Y BlockPartRotation; it returns
 * the baked int[28] whose per-vertex position (block-local, already /16) and
 * interpolated atlas UV we fold into world-space CrVertex quads. This keeps the
 * geometry faithful instead of hand-rolled. VINFO vertex order in the kernel
 * matches the cube path's CCW-from-outside winding (same CR_FRONT_SIGN). */

static float bits2f(int32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }

/* Fold the actual lightmap texture sample into the vertex representation. The
 * overworld stays on the historical scalar-red path so existing verified scenes
 * remain byte-stable. Nether and End require RGB: their blocks have no biome tint,
 * so multiplying the lightmap RGB into CrVertex.tint is algebraically the same
 * GL_MODULATE operation while CrVertex.light remains the directional face shade. */
/* ---- shade-time lightmap mode (game binaries opt in) ----
 * Legacy (default): the lightmap is folded into the vertex scalar at bake
 * time (noon-locked; what every golden harness verifies against). Lightmap
 * mode (overworld only): vertices carry the raw lightmap COORDS (sky/blk
 * levels, CrVertex.light/.blk) and the frame's 16x16 lightmap texture is
 * sampled per fragment (CrShadeCtx.lightmap) - GL semantics, so time of day
 * changes without remeshing. The directional face shade moves into .ao. */
static int   g_lm_mode = 0;
static int   g_lvl_active = 0;      /* last fold was a dim-0 lightmap-mode fold */
static float g_lvl_s = 15.0f, g_lvl_b = 0.0f;
void worldmc_set_lightmap_mode(int on) { g_lm_mode = on; }
int  worldmc_lightmap_mode(void) { return g_lm_mode; }

static float fold_lightmap(const CrLight *L, int sky, int blk, CrRgba *tint) {
    if (g_lm_mode && light_dimension(L) == 0) {
        g_lvl_active = 1;
        g_lvl_s = (float)sky;
        g_lvl_b = (float)blk;
        return 1.0f;   /* scalar channel now carries only shade multipliers */
    }
    g_lvl_active = 0;
    CrLightmapRgb lm = light_lightmap_rgb(L, sky, blk);
    if (light_dimension(L) == 0) return lm.r;
    CrRgba q = cr_lightmap_rgba8(lm);
    tint->r = (u8)(((int)tint->r * (int)q.r + 127) / 255);
    tint->g = (u8)(((int)tint->g * (int)q.g + 127) / 255);
    tint->b = (u8)(((int)tint->b * (int)q.b + 127) / 255);
    return 1.0f;
}

/* Combined sky/block light at a cell, with dimension RGB folded into *tint. */
static float cell_light01(const CrLight *L, int wx, int wy, int wz, CrRgba *tint) {
    int sy = wy < 0 ? 0 : (wy > 255 ? 255 : wy);
    int s = light_sky(L, wx, sy, wz);
    int b = light_blk(L, wx, sy, wz);
    return fold_lightmap(L, s, b, tint);
}

static int mc_light_level(const CrLight *L, int sky, int wx, int wy, int wz) {
    if (wy < 0 || wy >= 256) return sky ? 15 : 0;
    return sky ? light_sky(L, wx, wy, wz) : light_blk(L, wx, wy, wz);
}

static int mc_use_neighbor_brightness_at(const CrLight *L, int wx, int wy, int wz) {
    const BmBlock *m = bm_block(light_block(L, wx, wy, wz));
    if (m->is_air) return 0;
    if (m->kind == BM_KIND_SLAB_BOTTOM || m->kind == BM_KIND_SLAB_TOP ||
        m->kind == BM_KIND_STAIRS || m->kind == BM_KIND_IRON_BARS ||
        m->kind == BM_KIND_GLASS_PANE ||
        m->kind == BM_KIND_TORCH || m->kind == BM_KIND_RAIL ||
        m->kind == BM_KIND_TRAPDOOR || m->kind == BM_KIND_LADDER ||
        m->kind == BM_KIND_CACTUS || m->kind == BM_KIND_CHORUS_PLANT ||
        m->kind == BM_KIND_CHORUS_FLOWER)
        return 1;
    if (mc_translucent_at(L, wx, wy, wz)) return 1;
    return 0;
}

static int mc_light_for_ext(const CrLight *L, int sky, int wx, int wy, int wz) {
    if (wy < 0 || wy >= 256) return sky ? 15 : 0;
    if (mc_use_neighbor_brightness_at(L, wx, wy, wz)) {
        int best = 0;
        for (int f = 0; f < 6; ++f) {
            const Face *fc = &FACES[f];
            int v = mc_light_level(L, sky, wx + fc->n[0], wy + fc->n[1], wz + fc->n[2]);
            if (v > best) best = v;
            if (best >= 15) return best;
        }
        return best;
    }
    return mc_light_level(L, sky, wx, wy, wz);
}

/* ChunkCache.getCombinedLight for opacity-0 non-cubes: sample the brightest
 * neighbour, then clamp block light to the block's own emission. */
static float neighbor_model_light01(const CrLight *L, int wx, int wy, int wz,
                                    int emission, CrRgba *tint) {
    int s = mc_light_for_ext(L, 1, wx, wy, wz);
    int b = mc_light_for_ext(L, 0, wx, wy, wz);
    if (b < emission) b = emission;
    return fold_lightmap(L, s, b, tint);
}

/* BlockFluidRenderer samples getPackedLightmapCoords per face: top face at
 * the fluid cell, bottom at pos.down(), sides at the face neighbour. Only
 * meaningful in lightmap mode (fluids skip fold_lightmap: their base01 is the
 * verified full-brightness 1.0 in the legacy path). */
static void lm_capture_ext(const CrLight *L, int wx, int wy, int wz, int emission) {
    if (!(g_lm_mode && light_dimension(L) == 0)) { g_lvl_active = 0; return; }
    /* BlockLiquid.getPackedLightmapCoords: per-axis MAX of getCombinedLight at
     * the queried cell and the cell above it (liquids read the sky through
     * their own surface). getCombinedLight == the neighbour-brightness walk
     * mc_light_for_ext implements. */
    int s0 = mc_light_for_ext(L, 1, wx, wy, wz);
    int b0 = mc_light_for_ext(L, 0, wx, wy, wz);
    int s1 = mc_light_for_ext(L, 1, wx, wy + 1, wz);
    int b1 = mc_light_for_ext(L, 0, wx, wy + 1, wz);
    int s = s0 > s1 ? s0 : s1;
    int b = b0 > b1 ? b0 : b1;
    if (b < emission) b = emission;
    g_lvl_active = 1;
    g_lvl_s = (float)s;
    g_lvl_b = (float)b;
}

static int mc_packed_light(const CrLight *L, int wx, int wy, int wz) {
    int s = mc_light_for_ext(L, 1, wx, wy, wz);
    int b = mc_light_for_ext(L, 0, wx, wy, wz);
    return (s << 20) | (b << 4);
}

static int mc_ao_brightness(int br1, int br2, int br3, int br4) {
    if (br1 == 0) br1 = br4;
    if (br2 == 0) br2 = br4;
    if (br3 == 0) br3 = br4;
    return (br1 + br2 + br3 + br4) >> 2 & 16711935;
}

static float mc_light_byte01(const CrLight *L, int byte) {
    int dimension = light_dimension(L);
    float lv = (float)byte * (1.0f / 16.0f);
    int lo = (int)lv;
    if (lo < 0) return cr_light_brightness(dimension, 0);
    if (lo >= 15) return cr_light_brightness(dimension, 15);
    float t = lv - (float)lo;
    float a = cr_light_brightness(dimension, lo);
    float b = cr_light_brightness(dimension, lo + 1);
    return a + (b - a) * t;
}

static float mc_packed_light01(const CrLight *L, int packed) {
    /* Smooth AO is only used by the overworld verifier. Recreate its interpolated
     * red lane, including the two conditioning passes at gamma 0. */
    float v = mc_light_byte01(L, (packed >> 16) & 255) +
              mc_light_byte01(L, packed & 255) * 1.5f;
    v = v * 0.96f + 0.03f;
    if (v > 1.0f) v = 1.0f;
    return cr_lm_gamma_finish(v, 0.0f);
}

static const int AO_NEIGHBOR_CORNERS[6][4][3] = {
    /* DOWN  */ { {-1, 0, 0}, { 1, 0, 0}, { 0, 0,-1}, { 0, 0, 1} },
    /* UP    */ { { 1, 0, 0}, {-1, 0, 0}, { 0, 0,-1}, { 0, 0, 1} },
    /* NORTH */ { { 0, 1, 0}, { 0,-1, 0}, { 1, 0, 0}, {-1, 0, 0} },
    /* SOUTH */ { {-1, 0, 0}, { 1, 0, 0}, { 0,-1, 0}, { 0, 1, 0} },
    /* WEST  */ { { 0, 1, 0}, { 0,-1, 0}, { 0, 0,-1}, { 0, 0, 1} },
    /* EAST  */ { { 0,-1, 0}, { 0, 1, 0}, { 0, 0,-1}, { 0, 0, 1} },
};

static const int MC_FACE_CORNER[6][4][3] = {
    /* DOWN  */ { {0,0,1}, {0,0,0}, {1,0,0}, {1,0,1} },
    /* UP    */ { {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} },
    /* NORTH */ { {1,1,0}, {1,0,0}, {0,0,0}, {0,1,0} },
    /* SOUTH */ { {0,1,1}, {0,0,1}, {1,0,1}, {1,1,1} },
    /* WEST  */ { {0,1,0}, {0,0,0}, {0,0,1}, {0,1,1} },
    /* EAST  */ { {1,1,1}, {1,0,1}, {1,0,0}, {1,1,0} },
};

static const int MC_VERTEX_TRANSLATION[6][4] = {
    /* logical vert0..3 -> FaceBakery vertex index */
    { 0, 1, 2, 3 }, { 2, 3, 0, 1 }, { 3, 0, 1, 2 },
    { 0, 1, 2, 3 }, { 3, 0, 1, 2 }, { 1, 2, 3, 0 },
};

static int same_corner3(const int a[3], const int b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static void apply_smooth_fullcube_face(const CrLight *L, int wx, int wy, int wz,
                                       int face, CrVertex quad[4]) {
    const Face *fc = &FACES[face];
    const int bx = wx + fc->n[0], by = wy + fc->n[1], bz = wz + fc->n[2];
    const int (*co)[3] = AO_NEIGHBOR_CORNERS[face];
    int px[4], py[4], pz[4], br[4];
    float ao[4];

    for (int i = 0; i < 4; ++i) {
        px[i] = bx + co[i][0];
        py[i] = by + co[i][1];
        pz[i] = bz + co[i][2];
        br[i] = mc_packed_light(L, px[i], py[i], pz[i]);
        ao[i] = mc_ao_light_value_at(L, px[i], py[i], pz[i]);
    }

    int flag0 = mc_translucent_at(L, px[0] + fc->n[0], py[0] + fc->n[1], pz[0] + fc->n[2]);
    int flag1 = mc_translucent_at(L, px[1] + fc->n[0], py[1] + fc->n[1], pz[1] + fc->n[2]);
    int flag2 = mc_translucent_at(L, px[2] + fc->n[0], py[2] + fc->n[1], pz[2] + fc->n[2]);
    int flag3 = mc_translucent_at(L, px[3] + fc->n[0], py[3] + fc->n[1], pz[3] + fc->n[2]);

    float ao4, ao5, ao6, ao7;
    int br4, br5, br6, br7;

    if (!flag2 && !flag0) { ao4 = ao[0]; br4 = br[0]; }
    else {
        ao4 = mc_ao_light_value_at(L, px[0] + co[2][0], py[0] + co[2][1], pz[0] + co[2][2]);
        br4 = mc_packed_light(L, px[0] + co[2][0], py[0] + co[2][1], pz[0] + co[2][2]);
    }
    if (!flag3 && !flag0) { ao5 = ao[0]; br5 = br[0]; }
    else {
        ao5 = mc_ao_light_value_at(L, px[0] + co[3][0], py[0] + co[3][1], pz[0] + co[3][2]);
        br5 = mc_packed_light(L, px[0] + co[3][0], py[0] + co[3][1], pz[0] + co[3][2]);
    }
    if (!flag2 && !flag1) { ao6 = ao[1]; br6 = br[1]; }
    else {
        ao6 = mc_ao_light_value_at(L, px[1] + co[2][0], py[1] + co[2][1], pz[1] + co[2][2]);
        br6 = mc_packed_light(L, px[1] + co[2][0], py[1] + co[2][1], pz[1] + co[2][2]);
    }
    if (!flag3 && !flag1) { ao7 = ao[1]; br7 = br[1]; }
    else {
        ao7 = mc_ao_light_value_at(L, px[1] + co[3][0], py[1] + co[3][1], pz[1] + co[3][2]);
        br7 = mc_packed_light(L, px[1] + co[3][0], py[1] + co[3][1], pz[1] + co[3][2]);
    }

    int center_br = mc_packed_light(L, bx, by, bz);
    float center_ao = mc_ao_light_value_at(L, bx, by, bz);
    float logical_ao[4];
    int logical_br[4];
    float mc_ao[4];
    int mc_br[4];

    logical_ao[0] = (ao[3] + ao[0] + ao5 + center_ao) * 0.25f;
    logical_ao[1] = (ao[2] + ao[0] + ao4 + center_ao) * 0.25f;
    logical_ao[2] = (ao[2] + ao[1] + ao6 + center_ao) * 0.25f;
    logical_ao[3] = (ao[3] + ao[1] + ao7 + center_ao) * 0.25f;
    logical_br[0] = mc_ao_brightness(br[3], br[0], br5, center_br);
    logical_br[1] = mc_ao_brightness(br[2], br[0], br4, center_br);
    logical_br[2] = mc_ao_brightness(br[2], br[1], br6, center_br);
    logical_br[3] = mc_ao_brightness(br[3], br[1], br7, center_br);

    for (int i = 0; i < 4; ++i) {
        int mv = MC_VERTEX_TRANSLATION[face][i];
        mc_ao[mv] = logical_ao[i];
        mc_br[mv] = logical_br[i];
    }

    for (int c = 0; c < 4; ++c) {
        int found = 0;
        for (int mv = 0; mv < 4; ++mv) {
            if (same_corner3(FACES[face].corner[c], MC_FACE_CORNER[face][mv])) {
                if (g_lm_mode && light_dimension(L) == 0) {
                    /* lightmap mode: interpolated packed coords -> levels */
                    quad[c].ao    = mc_ao[mv] * fc->shade;
                    quad[c].light = (float)((mc_br[mv] >> 16) & 255) * (1.0f / 16.0f);
                    quad[c].blk   = (float)(mc_br[mv] & 255) * (1.0f / 16.0f);
                } else {
                quad[c].ao = mc_ao[mv];
                quad[c].light = mc_packed_light01(L, mc_br[mv]) * fc->shade;
                }
                found = 1;
                break;
            }
        }
        assert(found && "smooth AO corner mapping failed");
    }
}

/* Default auto-UV: the face's UV rect in model space (0..16) is its two spanning
 * axes' from/to, exactly what MC derives when a model face omits explicit uv. */
static void face_uv_model(int facing, const float from[3], const float to[3],
                          float uvs[4]) {
    switch (facing) {
        case BM_DOWN: case BM_UP:   /* span X,Z */
            uvs[0] = from[0]; uvs[1] = from[2]; uvs[2] = to[0]; uvs[3] = to[2]; break;
        case BM_NORTH: case BM_SOUTH: /* span X,Y */
            uvs[0] = from[0]; uvs[1] = from[1]; uvs[2] = to[0]; uvs[3] = to[1]; break;
        default:                      /* WEST/EAST span Z,Y */
            uvs[0] = from[2]; uvs[1] = from[1]; uvs[2] = to[2]; uvs[3] = to[1]; break;
    }
}

/* Bake one (optionally part-rotated) box/plane face and translate to world.
 * custom_uv is the JSON face uv [u0,v0,u1,v1], or NULL for FaceBakery auto UV. */
static void bake_face_custom(int wx, int wy, int wz,
                             const float from[3], const float to[3], int facing,
                             const float custom_uv[4], int uv_quarter,
                             int partPresent, int axis, float angle,
                             const float origin[3], int rescale,
                             int sprite, float vlight, float ao, CrRgba tint,
                             CrVertex quad[4]) {
    float u0, v0, u1, v1;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    float uvs[4];
    if (custom_uv) memcpy(uvs, custom_uv, sizeof uvs);
    else face_uv_model(facing, from, to, uvs);
    int32_t d[28];
    rk_facebakery_make_quad(from[0], from[1], from[2], to[0], to[1], to[2],
                            facing, uv_quarter, uvs, u0, u1, v0, v1,
                            partPresent, axis, angle, origin, rescale, d);
    for (int c = 0; c < 4; ++c) {
        int o = c * 7;
        quad[c].pos.x = bits2f(d[o])     + (float)wx;
        quad[c].pos.y = bits2f(d[o + 1]) + (float)wy;
        quad[c].pos.z = bits2f(d[o + 2]) + (float)wz;
        quad[c].uv.x  = bits2f(d[o + 4]);
        quad[c].uv.y  = bits2f(d[o + 5]);
        quad[c].light = vlight;
        quad[c].ao    = ao;
        quad[c].blk   = 0.0f;
        quad[c].tint  = tint;
        if (g_lvl_active) {
            /* lightmap mode: fold_lightmap returned 1.0, so vlight is the pure
             * shade multiplier - move it (and ao) into the ao channel and put
             * the lightmap coords in light/blk. */
            quad[c].ao    = ao * vlight;
            quad[c].light = g_lvl_s;
            quad[c].blk   = g_lvl_b;
        }
    }
}

static void bake_face(int wx, int wy, int wz,
                      const float from[3], const float to[3], int facing,
                      int partPresent, int axis, float angle,
                      const float origin[3], int rescale,
                      int sprite, float vlight, float ao, CrRgba tint,
                      CrVertex quad[4]) {
    bake_face_custom(wx, wy, wz, from, to, facing, NULL, 0,
                     partPresent, axis, angle, origin, rescale,
                     sprite, vlight, ao, tint, quad);
}

/* defined below (fire section); shared by the weighted-variant cube path */
static uint64_t mc_position_random(int x, int y, int z);
static int weighted_variant(int wx, int wy, int wz, int selector_offset, int n);
static void rotate_quad_y(CrVertex q[4], int wx, int wz, int quarter_turns);

/* ModelRotation vertex transform: quarter turns about the cube centre with the
 * vanilla -angle convention (matrix = Y(-y) * X(-x), X applied first). One X
 * quarter maps centred (y,z) -> (z,-y); rotate_quad_y already implements the
 * matching Y quarter (x,z) -> (-z,x). */
static void rotate_quad_x(CrVertex q[4], int wy, int wz, int quarter_turns) {
    int qn = quarter_turns & 3;
    float cy = (float)wy + 0.5f, cz = (float)wz + 0.5f;
    for (int i = 0; i < 4; ++i) {
        float y = q[i].pos.y - cy, z = q[i].pos.z - cz;
        float ry = y, rz = z;
        if (qn == 1) { ry = z; rz = -y; }
        else if (qn == 2) { ry = -y; rz = -z; }
        else if (qn == 3) { ry = -z; rz = y; }
        q[i].pos.y = cy + ry;
        q[i].pos.z = cz + rz;
    }
}

/* EnumFacing.rotateX (N->D->S->U) / rotateY (N->E->S->W), one quarter each;
 * X/E-axis and Y-axis faces are fixed under their own-axis rotation. */
static const int ROTX_FACE[6] = { BM_SOUTH, BM_NORTH, BM_DOWN, BM_UP, BM_WEST, BM_EAST };
static const int ROTY_FACE[6] = { BM_DOWN, BM_UP, BM_EAST, BM_WEST, BM_NORTH, BM_SOUTH };

/* Model face that lands on world face f under rot = Y^qy o X^qx:
 * g = X^(-qx)( Y^(-qy)(f) ). */
static int variant_model_face(int f, int qx, int qy) {
    int g = f;
    for (int i = 0; i < ((4 - qy) & 3); ++i) g = ROTY_FACE[g];
    for (int i = 0; i < ((4 - qx) & 3); ++i) g = ROTX_FACE[g];
    return g;
}

/* 1.11.2 blockstates with position-random WeightedBakedModel variants (from the
 * jar; equal weights, JSON order):
 *   stone, bedrock:  [base, mirrored, base y180, mirrored y180]
 *   dirt, grass(snowy=false), sand, red_sand:  y 0/90/180/270
 *   netherrack:  16 = y(0/90/180/270-outer) x x(0/90/180/270-inner)
 * (waterlily y 0/90/180/270 is applied in emit_lily.) `mir` selects the
 * cube_mirrored model: explicit uv [16,0,0,16] (U-flip) on every face. */
static int variant_cube_params(int cb, int wx, int wy, int wz,
                               int *mir, int *qx, int *qy) {
    *mir = 0; *qx = 0; *qy = 0;
    switch (cb) {
        case CR_CB_STONE: case CR_CB_BEDROCK: {
            int v = weighted_variant(wx, wy, wz, 0, 4);
            *mir = v & 1;
            *qy = (v >= 2) ? 2 : 0;
            return 1;
        }
        case CR_CB_DIRT: case CR_CB_GRASS: case CR_CB_SAND: {
            *qy = weighted_variant(wx, wy, wz, 0, 4);
            return 1;
        }
        case CR_CB_NETHERRACK: {
            int v = weighted_variant(wx, wy, wz, 0, 16);
            *qy = v / 4;
            *qx = v & 3;
            return 1;
        }
        default:
            return 0;
    }
}

static int same_fluid_id(int a, int b) {
    int a_water = a == 2 || a == 13;
    int b_water = b == 2 || b == 13;
    int a_lava = a == 11 || a == 12;
    int b_lava = b == 11 || b == 12;
    return (a_water && b_water) || (a_lava && b_lava);
}

/* BlockFluidRenderer.getFluidHeight for one corner. This consumes the legacy
 * LEVEL metadata rather than merely carrying its nibble through the dump. */
static float fluid_corner_height(const CrLight *L, int wx, int wy, int wz,
                                 int fluid_id) {
    int count = 0;
    float sum = 0.0f;
    for (int j = 0; j < 4; ++j) {
        int x = wx - (j & 1);
        int z = wz - ((j >> 1) & 1);
        if (same_fluid_id(light_block(L, x, wy + 1, z), fluid_id))
            return 1.0f;
        int id = light_block(L, x, wy, z);
        if (!same_fluid_id(id, fluid_id)) {
            const BmBlock *other = bm_block(id);
            if (!(other->is_full_cube && other->layer == CR_LAYER_SOLID)) {
                sum += 1.0f;
                count++;
            }
        } else {
            int level = light_meta(L, x, wy, z);
            int normalized = level >= 8 ? 0 : level;
            float empty = (float)(normalized + 1) / 9.0f;
            if (level >= 8 || level == 0) {
                sum += empty * 10.0f;
                count += 10;
            }
            sum += empty;
            count++;
        }
    }
    return count ? 1.0f - sum / (float)count : 0.0f;
}

static int fluid_face_visible(const CrLight *L, int wx, int wy, int wz,
                              int fluid_id, int face) {
    const Face *fc = &FACES[face];
    int id = light_block(L, wx + fc->n[0], wy + fc->n[1], wz + fc->n[2]);
    if (same_fluid_id(id, fluid_id)) return 0;
    if (face == BM_UP) return 1;
    const BmBlock *m = bm_block(id);
    return !(m->is_full_cube && m->layer == CR_LAYER_SOLID);
}

/* BlockLiquid.shouldRenderSides, used for the reverse top quad. */
static int fluid_open_near(const CrLight *L, int wx, int wy, int wz,
                           int fluid_id) {
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            int id = light_block(L, wx + dx, wy, wz + dz);
            if (!same_fluid_id(id, fluid_id) && !bm_block(id)->is_full_cube)
                return 1;
        }
    return 0;
}

static void reverse_quad(CrVertex dst[4], const CrVertex src[4]) {
    dst[0] = src[0]; dst[1] = src[3];
    dst[2] = src[2]; dst[3] = src[1];
}

static int fluid_rendered_depth(const CrLight *L, int wx, int wy, int wz,
                                int fluid_id) {
    int id = light_block(L, wx, wy, wz);
    int meta;
    if (!same_fluid_id(id, fluid_id)) return -1;
    meta = light_meta(L, wx, wy, wz);
    return meta >= 8 ? 0 : meta;
}

/* BlockLiquid.getFlow / getSlopeAngle, horizontal lanes only. */
static float fluid_slope_angle(const CrLight *L, int wx, int wy, int wz,
                               int fluid_id) {
    static const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    double dx = 0.0, dz = 0.0;
    int here = fluid_rendered_depth(L, wx, wy, wz, fluid_id);
    for (int d = 0; d < 4; ++d) {
        int nx = wx + dirs[d][0], nz = wz + dirs[d][1];
        int depth = fluid_rendered_depth(L, nx, wy, nz, fluid_id);
        if (depth < 0) {
            const BmBlock *neighbor = bm_block(light_block(L, nx, wy, nz));
            if (!(neighbor->is_full_cube && neighbor->layer == CR_LAYER_SOLID)) {
                depth = fluid_rendered_depth(L, nx, wy - 1, nz, fluid_id);
                if (depth >= 0) {
                    int k = depth - (here - 8);
                    dx += (double)(dirs[d][0] * k);
                    dz += (double)(dirs[d][1] * k);
                }
            }
        } else {
            int k = depth - here;
            dx += (double)(dirs[d][0] * k);
            dz += (double)(dirs[d][1] * k);
        }
    }
    if (dx == 0.0 && dz == 0.0) return -1000.0f;
    return (float)atan2(dz, dx) - 1.57079632679489661923f;
}

static float sprite_u(int sprite, float u) {
    float u0, u1;
    bm_sprite_uv(sprite, &u0, NULL, &u1, NULL);
    return u0 + (u1 - u0) * (u / 16.0f);
}

static float sprite_v(int sprite, float v) {
    float v0, v1;
    bm_sprite_uv(sprite, NULL, &v0, NULL, &v1);
    return v0 + (v1 - v0) * (v / 16.0f);
}

/* BlockFluidRenderer geometry. Stationary level-zero tops use the still sprite;
 * sloped/falling tops and every side use the flow sprite, matching the vanilla
 * still-vs-flow animation cadence. UV rotation for sloped tops remains a
 * separate geometric approximation. */
static void emit_fluid(CrChunkMeshMC *out, int *cap, int layer,
                       const CrLight *L, int wx, int wy, int wz, int fluid_id,
                       const BmBlock *model, CrRgba tint, float base01) {
    float h[4] = {
        fluid_corner_height(L, wx,     wy, wz,     fluid_id),
        fluid_corner_height(L, wx,     wy, wz + 1, fluid_id),
        fluid_corner_height(L, wx + 1, wy, wz + 1, fluid_id),
        fluid_corner_height(L, wx + 1, wy, wz,     fluid_id),
    };
    float avg = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
    const float from[3] = {0.0f, 0.0f, 0.0f};
    const float to[3] = {16.0f, avg * 16.0f, 16.0f};
    const float eps = 0.001f;
    float side_h[4] = {h[0], h[1], h[2], h[3]};
    int still_sprite = model->face[BM_UP].sprite;
    int flow_sprite = model->face[BM_NORTH].sprite;
    float slope = fluid_slope_angle(L, wx, wy, wz, fluid_id);
    int top_sprite = slope > -999.0f ? flow_sprite : still_sprite;

    int fl_em = (fluid_id == 11 || fluid_id == 12) ? 15 : 0;
    if (fluid_face_visible(L, wx, wy, wz, fluid_id, BM_UP)) {
        CrVertex q[4], back[4];
        lm_capture_ext(L, wx, wy, wz, fl_em);
        bake_face(wx, wy, wz, from, to, BM_UP, 0, 3, 0.0f, NULL, 0,
                  top_sprite, base01, 1.0f, tint, q);
        for (int i = 0; i < 4; ++i) {
            int east = q[i].pos.x > (float)wx + 0.5f;
            int south = q[i].pos.z > (float)wz + 0.5f;
            int corner = east ? (south ? 2 : 3) : (south ? 1 : 0);
            side_h[corner] -= eps;
            q[i].pos.y = (float)wy + side_h[corner];
            if (slope > -999.0f) {
                float sn = sinf(slope) * 0.25f;
                float cs = cosf(slope) * 0.25f;
                float uu, vv;
                if (!east && !south) {
                    uu = 8.0f + (-cs - sn) * 16.0f;
                    vv = 8.0f + (-cs + sn) * 16.0f;
                } else if (!east) {
                    uu = 8.0f + (-cs + sn) * 16.0f;
                    vv = 8.0f + ( cs + sn) * 16.0f;
                } else if (south) {
                    uu = 8.0f + ( cs + sn) * 16.0f;
                    vv = 8.0f + ( cs - sn) * 16.0f;
                } else {
                    uu = 8.0f + ( cs - sn) * 16.0f;
                    vv = 8.0f + (-cs - sn) * 16.0f;
                }
                q[i].uv.x = sprite_u(flow_sprite, uu);
                q[i].uv.y = sprite_v(flow_sprite, vv);
            }
        }
        push_face(out, cap, layer, q);
        if (fluid_open_near(L, wx, wy + 1, wz, fluid_id)) {
            reverse_quad(back, q);
            push_face(out, cap, layer, back);
        }
    }

    if (fluid_face_visible(L, wx, wy, wz, fluid_id, BM_DOWN)) {
        CrVertex q[4];
        lm_capture_ext(L, wx, wy - 1, wz, fl_em);
        bake_face(wx, wy, wz, from, to, BM_DOWN, 0, 3, 0.0f, NULL, 0,
                  still_sprite, base01 * FACES[BM_DOWN].shade, 1.0f, tint, q);
        push_face(out, cap, layer, q);
    }

    for (int face = BM_NORTH; face <= BM_EAST; ++face) {
        if (!fluid_face_visible(L, wx, wy, wz, fluid_id, face)) continue;
        CrVertex q[4], back[4];
        lm_capture_ext(L, wx + FACES[face].n[0], wy + FACES[face].n[1],
                       wz + FACES[face].n[2], fl_em);
        bake_face(wx, wy, wz, from, to, face, 0, 3, 0.0f, NULL, 0,
                  flow_sprite, base01 * FACES[face].shade, 1.0f, tint, q);
        for (int i = 0; i < 4; ++i) {
            int corner = 0;
            if (face == BM_NORTH) {
                q[i].pos.z = (float)wz + eps;
                corner = q[i].pos.x > (float)wx + 0.5f ? 3 : 0;
            } else if (face == BM_SOUTH) {
                q[i].pos.z = (float)wz + 1.0f - eps;
                corner = q[i].pos.x > (float)wx + 0.5f ? 2 : 1;
            } else if (face == BM_WEST) {
                q[i].pos.x = (float)wx + eps;
                corner = q[i].pos.z > (float)wz + 0.5f ? 1 : 0;
            } else {
                q[i].pos.x = (float)wx + 1.0f - eps;
                corner = q[i].pos.z > (float)wz + 0.5f ? 2 : 3;
            }
            int top = q[i].pos.y > (float)wy + 1e-5f;
            int east = q[i].pos.x > (float)wx + 0.5f;
            int south = q[i].pos.z > (float)wz + 0.5f;
            float uu = face == BM_NORTH ? (east ? 8.0f : 0.0f)
                     : face == BM_SOUTH ? (east ? 0.0f : 8.0f)
                     : face == BM_WEST  ? (south ? 0.0f : 8.0f)
                     :                    (south ? 8.0f : 0.0f);
            q[i].pos.y = top ? (float)wy + side_h[corner] : (float)wy;
            q[i].uv.x = sprite_u(flow_sprite, uu);
            q[i].uv.y = sprite_v(flow_sprite,
                                 top ? (1.0f - side_h[corner]) * 8.0f : 8.0f);
        }
        push_face(out, cap, layer, q);
        reverse_quad(back, q);
        push_face(out, cap, layer, back);
    }
}

/* Emit an axis-aligned box (model units 0..16). cull_self != -1 turns on
 * fluid-style face culling: a face flush against an opaque full cube, or against
 * the same fluid id, is skipped. */
static void emit_box(CrChunkMeshMC *out, int *cap, int layer,
                     const CrLight *L, int wx, int wy, int wz,
                     const float from[3], const float to[3],
                     int sprite, CrRgba tint, float base01, int cull_self) {
    for (int f = 0; f < 6; ++f) {
        if (cull_self >= 0) {
            const Face *fc = &FACES[f];
            int nx = wx + fc->n[0], ny = wy + fc->n[1], nz = wz + fc->n[2];
            int ncb = light_block(L, nx, ny, nz);
            const BmBlock *nm = bm_block(ncb);
            int neigh_opaque = nm->is_full_cube && nm->layer == CR_LAYER_SOLID;
            if (neigh_opaque || ncb == cull_self
                || (same_fluid_id(cull_self, ncb))) continue;
        }
        float vlight = base01 * FACES[f].shade;
        CrVertex quad[4];
        bake_face(wx, wy, wz, from, to, f, 0, 3, 0.0f, NULL, 0,
                  sprite, vlight, 1.0f, tint, quad);
        push_face(out, cap, layer, quad);
    }
}

/* Vanilla half_slab/upper_slab use per-face textures and explicit side UVs:
 * bottom halves sample V=8..16, top halves V=0..8. Auto-UV derives the
 * opposite halves from geometry, which makes a correctly placed slab select
 * the wrong vertical half of its side texture. */
static void emit_slab(CrChunkMeshMC *out, int *cap, const CrLight *L,
                      const BmBlock *m, int wx, int wy, int wz,
                      CrRgba tint, float base01) {
    int top = m->kind == BM_KIND_SLAB_TOP;
    const float from[3] = {0.0f, top ? 8.0f : 0.0f, 0.0f};
    const float to[3] = {16.0f, top ? 16.0f : 8.0f, 16.0f};
    const float full_uv[4] = {0.0f, 0.0f, 16.0f, 16.0f};
    const float bottom_side_uv[4] = {0.0f, 8.0f, 16.0f, 16.0f};
    const float top_side_uv[4] = {0.0f, 0.0f, 16.0f, 8.0f};
    (void)L;
    for (int f = 0; f < 6; ++f) {
        const float *uv = f >= BM_NORTH
            ? (top ? top_side_uv : bottom_side_uv)
            : full_uv;
        CrVertex quad[4];
        bake_face_custom(wx, wy, wz, from, to, f, uv, 0,
                         0, 3, 0.0f, NULL, 0,
                         m->face[f].sprite, base01 * FACES[f].shade,
                         1.0f, tint, quad);
        push_face(out, cap, m->layer, quad);
    }
}

static int rotate_face_y(int face, int quarter_turns);
static void emit_model_face(CrChunkMeshMC *out, int *cap, int layer,
                            int wx, int wy, int wz,
                            const float from[3], const float to[3], int face,
                            const float uv[4], int part_present, int axis,
                            float angle, const float origin[3], int rescale,
                            int sprite, CrRgba tint, float base01,
                            int shade, int quarter_turns);

/* models/block/trapdoor_{bottom,top,open}.json. The local open model occupies
 * z=13..16; metadata 0..3 maps NORTH, SOUTH, WEST, EAST and rotates that panel
 * by 0, 180, 270, 90 degrees. Explicit UVs preserve the texture's holes and
 * three-pixel edge strips. */
static void emit_trapdoor(CrChunkMeshMC *out, int *cap, const CrLight *L,
                          const BmBlock *m, int wx, int wy, int wz,
                          CrRgba tint, float base01) {
    int meta = light_meta(L, wx, wy, wz);
    int open = (meta & 4) != 0;
    int top = (meta & 8) != 0;
    static const int turns[4] = {0, 2, 3, 1};
    int qy = open ? turns[meta & 3] : 0;
    const float closed_bottom_from[3] = {0.0f, 0.0f, 0.0f};
    const float closed_bottom_to[3] = {16.0f, 3.0f, 16.0f};
    const float closed_top_from[3] = {0.0f, 13.0f, 0.0f};
    const float closed_top_to[3] = {16.0f, 16.0f, 16.0f};
    const float open_from[3] = {0.0f, 0.0f, 13.0f};
    const float open_to[3] = {16.0f, 16.0f, 16.0f};
    const float *from = open ? open_from
                      : top ? closed_top_from : closed_bottom_from;
    const float *to = open ? open_to
                    : top ? closed_top_to : closed_bottom_to;
    const float full_uv[4] = {0.0f, 0.0f, 16.0f, 16.0f};
    const float edge_uv[4] = {0.0f, 16.0f, 16.0f, 13.0f};
    static const float open_uv[6][4] = {
        {0.0f, 13.0f, 16.0f, 16.0f},
        {0.0f, 16.0f, 16.0f, 13.0f},
        {0.0f, 0.0f, 16.0f, 16.0f},
        {0.0f, 0.0f, 16.0f, 16.0f},
        {16.0f, 0.0f, 13.0f, 16.0f},
        {13.0f, 0.0f, 16.0f, 16.0f},
    };
    for (int face = 0; face < 6; ++face) {
        int has_cullface;
        if (open)
            has_cullface = face != BM_NORTH;
        else if (top)
            has_cullface = face != BM_DOWN;
        else
            has_cullface = face != BM_UP;
        if (has_cullface) {
            int cull_face = rotate_face_y(face, qy);
            const Face *fc = &FACES[cull_face];
            const BmBlock *neighbor = bm_block(light_block(
                L, wx + fc->n[0], wy + fc->n[1], wz + fc->n[2]));
            if (neighbor->is_full_cube &&
                neighbor->layer == CR_LAYER_SOLID)
                continue;
        }
        const float *uv = open ? open_uv[face]
                        : face >= BM_NORTH ? edge_uv : full_uv;
        emit_model_face(out, cap, m->layer, wx, wy, wz, from, to, face,
                        uv, 0, 3, 0.0f, NULL, 0,
                        m->face[face].sprite, tint, base01, 1, qy);
    }
}

/* Lily pad: models/block/waterlily.json - zero-thickness horizontal plane at
 * model y=0.25, UP+DOWN only, ambientocclusion false (ao=1). CUTOUT layer. */
static void emit_lily(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                      int wx, int wy, int wz, int sprite, CrRgba tint,
                      float base01) {
    const float from[3] = { 0.0f, 0.25f, 0.0f };
    const float to[3]   = { 16.0f, 0.25f, 16.0f };
    (void)L;
    const int faces[2] = { BM_DOWN, BM_UP };
    /* waterlily.json blockstate: position-random y 0/90/180/270 variants */
    int qy = weighted_variant(wx, wy, wz, 0, 4);
    for (int i = 0; i < 2; ++i) {
        float vlight = base01 * FACES[faces[i]].shade;
        CrVertex quad[4];
        bake_face(wx, wy, wz, from, to, faces[i], 0, 3, 0.0f, NULL, 0,
                  sprite, vlight, 1.0f, tint, quad);
        if (qy) rotate_quad_y(quad, wx, wz, qy);
        push_face(out, cap, layer, quad);
    }
}

/* rail_flat.json: zero-thickness horizontal plane at model y=1, UP+DOWN only,
 * ambient occlusion disabled. Legacy meta 0 is north/south and 1 east/west. */
static void emit_rail(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                      int wx, int wy, int wz, int sprite, CrRgba tint,
                      float base01) {
    const float from[3] = { 0.0f, 1.0f, 0.0f };
    const float to[3]   = { 16.0f, 1.0f, 16.0f };
    const int faces[2] = { BM_DOWN, BM_UP };
    int quarter_turns = (light_meta(L, wx, wy, wz) & 15) == 1;
    for (int i = 0; i < 2; ++i) {
        CrVertex quad[4];
        bake_face(wx, wy, wz, from, to, faces[i], 0, 3, 0.0f, NULL, 0,
                  sprite, base01, 1.0f, tint, quad);
        if (quarter_turns) rotate_quad_y(quad, wx, wz, quarter_turns);
        push_face(out, cap, layer, quad);
    }
}

/* ladder.json: default north-facing model is a two-sided plane at z=15.2.
 * Blockstate rotations map legacy EnumFacing metadata 2/5/3/4 to
 * north/east/south/west. Ambient occlusion and face shade are both disabled. */
static void emit_ladder(CrChunkMeshMC *out, int *cap, int layer,
                        const CrLight *L, int wx, int wy, int wz,
                        int sprite, CrRgba tint, float base01) {
    const float from[3] = { 0.0f, 0.0f, 15.2f };
    const float to[3]   = { 16.0f, 16.0f, 15.2f };
    const int faces[2] = { BM_NORTH, BM_SOUTH };
    int meta = light_meta(L, wx, wy, wz) & 7;
    int quarter_turns = meta == 5 ? 1 : meta == 3 ? 2 : meta == 4 ? 3 : 0;
    for (int i = 0; i < 2; ++i) {
        CrVertex quad[4];
        bake_face(wx, wy, wz, from, to, faces[i], 0, 3, 0.0f, NULL, 0,
                  sprite, base01, 1.0f, tint, quad);
        if (quarter_turns) rotate_quad_y(quad, wx, wz, quarter_turns);
        push_face(out, cap, layer, quad);
    }
}

/* snow_height2.json: box from y=0..2 (model units). SOLID, no cull. */
static void emit_snow_layer(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                            int wx, int wy, int wz, int sprite, CrRgba tint,
                            float base01) {
    const float from[3] = { 0.0f, 0.0f, 0.0f };
    const float to[3]   = { 16.0f, 2.0f, 16.0f };
    emit_box(out, cap, layer, L, wx, wy, wz, from, to, sprite, tint, base01, -1);
}

/* vine_1.json: thin plane at z=15.2, north+south faces, shade=false. */
static void emit_vine(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                      int wx, int wy, int wz, int sprite, CrRgba tint,
                      float base01) {
    const float from[3] = { 0.0f, 0.0f, 15.2f };
    const float to[3]   = { 16.0f, 16.0f, 15.2f };
    (void)L;
    const int faces[2] = { BM_NORTH, BM_SOUTH };
    for (int i = 0; i < 2; ++i) {
        CrVertex quad[4];
        bake_face(wx, wy, wz, from, to, faces[i], 0, 3, 0.0f, NULL, 0,
                  sprite, base01 /* shade=false */, 1.0f, tint, quad);
        push_face(out, cap, layer, quad);
    }
}

/* cactus.json: top/bottom full footprint + N/S sides on z∈[1,15] + E/W on x∈[1,15]. */
static void emit_cactus(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                        int wx, int wy, int wz, const BmBlock *m, CrRgba t,
                        float base01) {
    int top_spr = m->face[BM_UP].sprite;
    int bot_spr = m->face[BM_DOWN].sprite;
    int side_spr = m->face[BM_NORTH].sprite;
    (void)L;
    CrVertex q[4];
    {
        const float from[3] = { 0.0f, 16.0f, 0.0f }, to[3] = { 16.0f, 16.0f, 16.0f };
        bake_face(wx, wy, wz, from, to, BM_UP, 0, 3, 0.0f, NULL, 0,
                  top_spr, base01 * FACES[BM_UP].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
    }
    {
        const float from[3] = { 0.0f, 0.0f, 0.0f }, to[3] = { 16.0f, 0.0f, 16.0f };
        bake_face(wx, wy, wz, from, to, BM_DOWN, 0, 3, 0.0f, NULL, 0,
                  bot_spr, base01 * FACES[BM_DOWN].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
    }
    {
        const float from[3] = { 0.0f, 0.0f, 1.0f }, to[3] = { 16.0f, 16.0f, 15.0f };
        bake_face(wx, wy, wz, from, to, BM_NORTH, 0, 3, 0.0f, NULL, 0,
                  side_spr, base01 * FACES[BM_NORTH].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
        bake_face(wx, wy, wz, from, to, BM_SOUTH, 0, 3, 0.0f, NULL, 0,
                  side_spr, base01 * FACES[BM_SOUTH].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
    }
    {
        const float from[3] = { 1.0f, 0.0f, 0.0f }, to[3] = { 15.0f, 16.0f, 16.0f };
        bake_face(wx, wy, wz, from, to, BM_WEST, 0, 3, 0.0f, NULL, 0,
                  side_spr, base01 * FACES[BM_WEST].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
        bake_face(wx, wy, wz, from, to, BM_EAST, 0, 3, 0.0f, NULL, 0,
                  side_spr, base01 * FACES[BM_EAST].shade, 1.0f, t, q);
        push_face(out, cap, layer, q);
    }
}

static int iron_bars_connects(const CrLight *L, int wx, int wy, int wz) {
    int cb = light_block(L, wx, wy, wz);
    const BmBlock *m = bm_block(cb);
    return cb == CR_CB_IRON_BARS || (m->is_full_cube && m->layer == CR_LAYER_SOLID);
}

static int iron_bars_neighbor_opaque(const CrLight *L, int wx, int wy, int wz) {
    int cb = light_block(L, wx, wy, wz);
    return cb == CR_CB_IRON_BARS || bm_block(cb)->is_full_cube;
}

static void rotate_quad_y(CrVertex q[4], int wx, int wz, int quarter_turns);
static int rotate_face_y(int face, int quarter_turns);

static void emit_model_face(CrChunkMeshMC *out, int *cap, int layer,
                            int wx, int wy, int wz,
                            const float from[3], const float to[3], int face,
                            const float uv[4], int part_present, int axis,
                            float angle, const float origin[3], int rescale,
                            int sprite, CrRgba tint, float base01,
                            int shade, int quarter_turns) {
    CrVertex q[4];
    int shaded_face = rotate_face_y(face, quarter_turns);
    bake_face_custom(wx, wy, wz, from, to, face, uv, 0,
                     part_present, axis, angle, origin, rescale,
                     sprite, base01 * (shade ? FACES[shaded_face].shade : 1.0f),
                     1.0f, tint, q);
    if (quarter_turns) rotate_quad_y(q, wx, wz, quarter_turns);
    push_face(out, cap, layer, q);
}

static void emit_iron_bars_post_ends(CrChunkMeshMC *out, int *cap,
                                     int wx, int wy, int wz, int layer,
                                     int sprite, CrRgba tint, float base01) {
    const float uv[4] = {7, 7, 9, 9};
    const int faces[2] = {BM_DOWN, BM_UP};
    const float ys[2] = {0.001f, 15.999f};
    for (int y = 0; y < 2; ++y) {
        const float from[3] = {7, ys[y], 7}, to[3] = {9, ys[y], 9};
        for (int f = 0; f < 2; ++f)
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, faces[f],
                            uv, 0, 3, 0.0f, NULL, 0, sprite, tint, base01, 1, 0);
    }
}

static void emit_iron_bars_post(CrChunkMeshMC *out, int *cap,
                                int wx, int wy, int wz, int layer,
                                int sprite, CrRgba tint, float base01) {
    const float a0[3] = {8, 0, 7}, a1[3] = {8, 16, 9};
    const float b0[3] = {7, 0, 8}, b1[3] = {9, 16, 8};
    const float uv0[4] = {7, 0, 9, 16}, uv1[4] = {9, 0, 7, 16};
    emit_model_face(out, cap, layer, wx, wy, wz, a0, a1, BM_WEST, uv0,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, 0);
    emit_model_face(out, cap, layer, wx, wy, wz, a0, a1, BM_EAST, uv1,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, 0);
    emit_model_face(out, cap, layer, wx, wy, wz, b0, b1, BM_NORTH, uv0,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, 0);
    emit_model_face(out, cap, layer, wx, wy, wz, b0, b1, BM_SOUTH, uv1,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, 0);
}

static void emit_iron_bars_cap(CrChunkMeshMC *out, int *cap,
                               int wx, int wy, int wz, int layer, int sprite,
                               CrRgba tint, float base01, int alt, int turns) {
    const float a0[3] = {8, 0, alt ? 7 : 8};
    const float a1[3] = {8, 16, alt ? 8 : 9};
    const float b0[3] = {7, 0, alt ? 7 : 9};
    const float b1[3] = {9, 16, alt ? 7 : 9};
    const float west[4] = {8, 0, alt ? 9 : 7, 16};
    const float east[4] = {alt ? 9 : 7, 0, 8, 16};
    const float north[4] = {alt ? 7 : 9, 0, alt ? 9 : 7, 16};
    const float south[4] = {alt ? 9 : 7, 0, alt ? 7 : 9, 16};
    emit_model_face(out, cap, layer, wx, wy, wz, a0, a1, BM_WEST, west,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
    emit_model_face(out, cap, layer, wx, wy, wz, a0, a1, BM_EAST, east,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
    emit_model_face(out, cap, layer, wx, wy, wz, b0, b1, BM_NORTH, north,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
    emit_model_face(out, cap, layer, wx, wy, wz, b0, b1, BM_SOUTH, south,
                    0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
}

static void emit_iron_bars_side(CrChunkMeshMC *out, int *cap,
                                int wx, int wy, int wz, int layer, int sprite,
                                CrRgba tint, float base01, int alt, int turns,
                                int neighbor_opaque) {
    if (!alt) {
        const float plane0[3] = {8, 0, 0}, plane1[3] = {8, 16, 8};
        const float west[4] = {16, 0, 8, 16}, east[4] = {8, 0, 16, 16};
        emit_model_face(out, cap, layer, wx, wy, wz, plane0, plane1, BM_WEST, west,
                        0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, plane0, plane1, BM_EAST, east,
                        0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        if (!neighbor_opaque) {
            const float edge0[3] = {7, 0, 0}, edge1[3] = {9, 16, 7};
            const float edge_uv[4] = {7, 0, 9, 16};
            emit_model_face(out, cap, layer, wx, wy, wz, edge0, edge1,
                            BM_NORTH, edge_uv, 0, 3, 0, NULL, 0, sprite, tint,
                            base01, 1, turns);
        }
        const float down_uv[4] = {9, 0, 7, 7}, up_uv[4] = {7, 0, 9, 7};
        const float ys[2] = {0.001f, 15.999f};
        for (int y = 0; y < 2; ++y) {
            const float from[3] = {7, ys[y], 0}, to[3] = {9, ys[y], 7};
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_DOWN,
                            down_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_UP,
                            up_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        }
    } else {
        const float plane0[3] = {8, 0, 8}, plane1[3] = {8, 16, 16};
        const float west[4] = {8, 0, 0, 16}, east[4] = {0, 0, 8, 16};
        emit_model_face(out, cap, layer, wx, wy, wz, plane0, plane1, BM_WEST, west,
                        0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, plane0, plane1, BM_EAST, east,
                        0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        const float edge0[3] = {7, 0, 9}, edge1[3] = {9, 16, 16};
        const float south_uv[4] = {7, 0, 9, 16};
        const float down_uv[4] = {9, 9, 7, 16}, up_uv[4] = {7, 9, 9, 16};
        if (!neighbor_opaque)
            emit_model_face(out, cap, layer, wx, wy, wz, edge0, edge1, BM_SOUTH,
                            south_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, edge0, edge1, BM_DOWN,
                        down_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, edge0, edge1, BM_UP,
                        up_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        const float ys[2] = {0.001f, 15.999f};
        for (int y = 0; y < 2; ++y) {
            const float from[3] = {7, ys[y], 9}, to[3] = {9, ys[y], 16};
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_DOWN,
                            down_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_UP,
                            up_uv, 0, 3, 0, NULL, 0, sprite, tint, base01, 1, turns);
        }
    }
}

/* Exact 1.11.2 iron_bars multipart: post ends always, isolated post or a
 * one-direction cap, then each connected directional side model. */
static void emit_iron_bars(CrChunkMeshMC *out, int *cap, const CrLight *L,
                           int wx, int wy, int wz, const BmBlock *m,
                           CrRgba tint, float base01) {
    int sprite = m->face[BM_NORTH].sprite;
    const int dx[4] = {0, 1, 0, -1};
    const int dz[4] = {-1, 0, 1, 0};
    int connected[4], n = 0;
    emit_iron_bars_post_ends(out, cap, wx, wy, wz, m->layer, sprite, tint, base01);
    for (int i = 0; i < 4; ++i) {
        connected[i] = iron_bars_connects(L, wx + dx[i], wy, wz + dz[i]);
        n += connected[i];
    }
    if (n == 0)
        emit_iron_bars_post(out, cap, wx, wy, wz, m->layer, sprite, tint, base01);
    else if (n == 1) {
        if (connected[0]) emit_iron_bars_cap(out, cap, wx, wy, wz, m->layer,
                                             sprite, tint, base01, 0, 0);
        if (connected[1]) emit_iron_bars_cap(out, cap, wx, wy, wz, m->layer,
                                             sprite, tint, base01, 0, 1);
        if (connected[2]) emit_iron_bars_cap(out, cap, wx, wy, wz, m->layer,
                                             sprite, tint, base01, 1, 0);
        if (connected[3]) emit_iron_bars_cap(out, cap, wx, wy, wz, m->layer,
                                             sprite, tint, base01, 1, 1);
    }
    for (int i = 0; i < 4; ++i) if (connected[i]) {
        int alt = i >= 2;
        int turns = i & 1;
        int opaque = iron_bars_neighbor_opaque(L, wx + dx[i], wy, wz + dz[i]);
        emit_iron_bars_side(out, cap, wx, wy, wz, m->layer, sprite, tint,
                            base01, alt, turns, opaque);
    }
}

static int glass_pane_connects(const CrLight *L, int wx, int wy, int wz) {
    int cb = light_block(L, wx, wy, wz);
    const BmBlock *m = bm_block(cb);
    return cb == CR_CB_GLASS_PANE || cb == CR_CB_IRON_BARS ||
           cb == CR_CB_GLASS || m->is_full_cube;
}

static int glass_pane_culls_edge(const CrLight *L, int wx, int wy, int wz) {
    int cb = light_block(L, wx, wy, wz);
    const BmBlock *m = bm_block(cb);
    return cb == CR_CB_GLASS_PANE ||
           (m->is_full_cube && m->layer == CR_LAYER_SOLID);
}

static void emit_glass_pane_post_ends(CrChunkMeshMC *out, int *cap,
                                      int wx, int wy, int wz, int layer,
                                      int edge_sprite, CrRgba tint,
                                      float base01) {
    const float from[3] = {7, 0, 7}, to[3] = {9, 16, 9};
    const float uv[4] = {7, 7, 9, 9};
    emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_DOWN, uv,
                    0, 3, 0, NULL, 0, edge_sprite, tint, base01, 1, 0);
    emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_UP, uv,
                    0, 3, 0, NULL, 0, edge_sprite, tint, base01, 1, 0);
}

static void emit_glass_pane_side(CrChunkMeshMC *out, int *cap,
                                 int wx, int wy, int wz, int layer,
                                 int pane_sprite, int edge_sprite,
                                 CrRgba tint, float base01, int alt,
                                 int turns, int cull_edge) {
    const float edge_uv[4] = {7, 0, 9, 16};
    const float cap_uv[4] = {7, 0, 9, 7};
    if (!alt) {
        const float from[3] = {7, 0, 0}, to[3] = {9, 16, 7};
        const float west_uv[4] = {16, 0, 9, 16};
        const float east_uv[4] = {9, 0, 16, 16};
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_DOWN,
                        cap_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                        base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_UP,
                        cap_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                        base01, 1, turns);
        if (!cull_edge)
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_NORTH,
                            edge_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                            base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_WEST,
                        west_uv, 0, 3, 0, NULL, 0, pane_sprite, tint,
                        base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_EAST,
                        east_uv, 0, 3, 0, NULL, 0, pane_sprite, tint,
                        base01, 1, turns);
    } else {
        const float from[3] = {7, 0, 9}, to[3] = {9, 16, 16};
        const float west_uv[4] = {7, 0, 0, 16};
        const float east_uv[4] = {0, 0, 7, 16};
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_DOWN,
                        cap_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                        base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_UP,
                        cap_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                        base01, 1, turns);
        if (!cull_edge)
            emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_SOUTH,
                            edge_uv, 0, 3, 0, NULL, 0, edge_sprite, tint,
                            base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_WEST,
                        west_uv, 0, 3, 0, NULL, 0, pane_sprite, tint,
                        base01, 1, turns);
        emit_model_face(out, cap, layer, wx, wy, wz, from, to, BM_EAST,
                        east_uv, 0, 3, 0, NULL, 0, pane_sprite, tint,
                        base01, 1, turns);
    }
}

static void emit_glass_pane_noside(CrChunkMeshMC *out, int *cap,
                                   int wx, int wy, int wz, int layer,
                                   int pane_sprite, CrRgba tint, float base01,
                                   int alt, int turns) {
    const float from[3] = {7, 0, 7}, to[3] = {9, 16, 9};
    const float normal_uv[4] = {9, 0, 7, 16};
    const float alt_uv[4] = {7, 0, 9, 16};
    emit_model_face(out, cap, layer, wx, wy, wz, from, to,
                    alt ? BM_EAST : BM_NORTH, alt ? alt_uv : normal_uv,
                    0, 3, 0, NULL, 0, pane_sprite, tint, base01, 1, turns);
}

/* Exact 1.11.2 glass_pane multipart. Unlike iron bars, panes always emit one
 * noside face for each absent connection and use a distinct edge texture. */
static void emit_glass_pane(CrChunkMeshMC *out, int *cap, const CrLight *L,
                            int wx, int wy, int wz, const BmBlock *m,
                            CrRgba tint, float base01) {
    int pane_sprite = m->face[BM_NORTH].sprite;
    int edge_sprite = m->face[BM_UP].sprite;
    const int dx[4] = {0, 1, 0, -1};
    const int dz[4] = {-1, 0, 1, 0};
    int connected[4];
    emit_glass_pane_post_ends(out, cap, wx, wy, wz, m->layer, edge_sprite,
                              tint, base01);
    for (int i = 0; i < 4; ++i)
        connected[i] = glass_pane_connects(L, wx + dx[i], wy, wz + dz[i]);
    for (int i = 0; i < 4; ++i) {
        if (connected[i]) {
            int alt = i >= 2;
            int turns = i & 1;
            int cull_edge =
                glass_pane_culls_edge(L, wx + dx[i], wy, wz + dz[i]);
            emit_glass_pane_side(out, cap, wx, wy, wz, m->layer, pane_sprite,
                                 edge_sprite, tint, base01, alt, turns,
                                 cull_edge);
        } else {
            int alt = i == 1 || i == 2;
            int turns = i == 2 ? 1 : i == 3 ? 3 : 0;
            emit_glass_pane_noside(out, cap, wx, wy, wz, m->layer,
                                   pane_sprite, tint, base01, alt, turns);
        }
    }
}

/* Exact torch.json / torch_wall.json planes, UVs, shade=false, -22.5 degree
 * wall tilt, and blockstate Y rotations. Legacy meta 1/2/3/4 = E/W/S/N. */
static void emit_torch(CrChunkMeshMC *out, int *cap, const CrLight *L,
                       int wx, int wy, int wz, const BmBlock *m,
                       CrRgba tint, float base01) {
    int meta = light_meta(L, wx, wy, wz) & 7;
    int sprite = m->face[BM_NORTH].sprite;
    const float cap_down[4] = {7, 13, 9, 15}, cap_up[4] = {7, 6, 9, 8};
    const float full[4] = {0, 0, 16, 16};
    if (meta == 0 || meta >= 5) {
        const float stem0[3] = {7, 0, 7}, stem1[3] = {9, 10, 9};
        const float x0[3] = {7, 0, 0}, x1[3] = {9, 16, 16};
        const float z0[3] = {0, 0, 7}, z1[3] = {16, 16, 9};
        emit_model_face(out, cap, m->layer, wx, wy, wz, stem0, stem1, BM_DOWN,
                        cap_down, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        emit_model_face(out, cap, m->layer, wx, wy, wz, stem0, stem1, BM_UP,
                        cap_up, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        emit_model_face(out, cap, m->layer, wx, wy, wz, x0, x1, BM_WEST,
                        full, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        emit_model_face(out, cap, m->layer, wx, wy, wz, x0, x1, BM_EAST,
                        full, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        emit_model_face(out, cap, m->layer, wx, wy, wz, z0, z1, BM_NORTH,
                        full, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        emit_model_face(out, cap, m->layer, wx, wy, wz, z0, z1, BM_SOUTH,
                        full, 0, 3, 0, NULL, 0, sprite, tint, base01, 0, 0);
        return;
    }
    int turns = meta == 1 ? 0 : meta == 3 ? 1 : meta == 2 ? 2 : 3;
    const float origin[3] = {0.0f, 3.5f / 16.0f, 0.5f};
    const float stem0[3] = {-1, 3.5f, 7}, stem1[3] = {1, 13.5f, 9};
    const float x0[3] = {-1, 3.5f, 0}, x1[3] = {1, 19.5f, 16};
    const float z0[3] = {-8, 3.5f, 7}, z1[3] = {8, 19.5f, 9};
    emit_model_face(out, cap, m->layer, wx, wy, wz, stem0, stem1, BM_DOWN,
                    cap_down, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
    emit_model_face(out, cap, m->layer, wx, wy, wz, stem0, stem1, BM_UP,
                    cap_up, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
    emit_model_face(out, cap, m->layer, wx, wy, wz, x0, x1, BM_WEST,
                    full, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
    emit_model_face(out, cap, m->layer, wx, wy, wz, x0, x1, BM_EAST,
                    full, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
    emit_model_face(out, cap, m->layer, wx, wy, wz, z0, z1, BM_NORTH,
                    full, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
    emit_model_face(out, cap, m->layer, wx, wy, wz, z0, z1, BM_SOUTH,
                    full, 1, 2, -22.5f, origin, 0,
                    sprite, tint, base01, 0, turns);
}

/* Cross-plant: two 45deg-rotated planes (block/cross), 4 quads, no directional
 * shade (MC shade=false), on the CUTOUT layer. */
static void emit_cross(CrChunkMeshMC *out, int *cap, int layer, const CrLight *L,
                       int cb, int wx, int wy, int wz, int sprite, CrRgba tint,
                       float base01) {
    /* BlockPartRotation origin is model-space/16 (JSON [8,8,8] -> 0.5), matching
     * the kernel's position space (bounds already divided by 16). */
    const float origin[3] = { 0.5f, 0.5f, 0.5f };
    (void)L;
    /* plane A spans X at z=8 (north+south faces); plane B spans Z at x=8. */
    const float aFrom[3] = { 0.8f, 0.0f, 8.0f }, aTo[3] = { 15.2f, 16.0f, 8.0f };
    const float bFrom[3] = { 8.0f, 0.0f, 0.8f }, bTo[3] = { 8.0f, 16.0f, 15.2f };
    const int   planeFace[4] = { BM_NORTH, BM_SOUTH, BM_WEST, BM_EAST };
    const float full_uv[4] = { 0.0f, 0.0f, 16.0f, 16.0f };
    /* Actual 1.11.2 getOffsetType bytecode: BlockTallGrass is XYZ;
     * BlockFlower and BlockDoublePlant are XZ; dead bush, mushrooms, reeds,
     * and cocoa inherit NONE. Block.getOffset hashes y=0, so double-plant
     * halves share one translation. */
    int offset_type = (cb >= CR_CB_TALLGRASS && cb <= CR_CB_FERN) ? 2
        : ((cb >= CR_CB_YELLOW_FLOWER && cb <= CR_CB_DPLANT_LAST) ||
           is_double_plant_model(cb)) ? 1 : 0;
    float offx = 0.0f, offy = 0.0f, offz = 0.0f;
    if (offset_type) {
        uint64_t r = mc_position_random(wx, 0, wz);
        offx = (((float)((r >> 16) & 15u) / 15.0f) - 0.5f) * 0.5f;
        offz = (((float)((r >> 24) & 15u) / 15.0f) - 0.5f) * 0.5f;
        if (offset_type == 2)
            offy = (((float)((r >> 20) & 15u) / 15.0f) - 1.0f) * 0.2f;
    }
    for (int q = 0; q < 4; ++q) {
        const float *from = (q < 2) ? aFrom : bFrom;
        const float *to   = (q < 2) ? aTo   : bTo;
        CrVertex quad[4];
        /* cross.json gives every plant/web face the full [0,0,16,16] UV.
         * FaceBakery's automatic UV would instead inherit the inset model
         * bounds (0.8..15.2), selecting the wrong texels across the plane. */
        bake_face_custom(wx, wy, wz, from, to, planeFace[q], full_uv, 0,
                         1, 1, 45.0f, origin, 1,
                         sprite, base01, 1.0f, tint, quad);
        for (int i = 0; i < 4; ++i) {
            quad[i].pos.x += offx;
            quad[i].pos.y += offy;
            quad[i].pos.z += offz;
        }
        push_face(out, cap, layer, quad);
    }
}

/* Exact double_sunflower_top.json: four half-height stem quads using the top
 * texture, then a tilted two-sided head with distinct back/front textures. */
static void emit_double_sunflower_top(CrChunkMeshMC *out, int *cap,
                                      const CrLight *L, int wx, int wy, int wz,
                                      const BmBlock *m, CrRgba tint,
                                      float base01) {
    const float stem_a0[3] = {0.8f, 0.0f, 8.0f};
    const float stem_a1[3] = {15.2f, 8.0f, 8.0f};
    const float stem_b0[3] = {8.0f, 0.0f, 0.8f};
    const float stem_b1[3] = {8.0f, 8.0f, 15.2f};
    const float head0[3] = {9.6f, -1.0f, 1.0f};
    const float head1[3] = {9.6f, 15.0f, 15.0f};
    const float origin[3] = {0.5f, 0.5f, 0.5f};
    const float stem_uv[4] = {0.0f, 8.0f, 16.0f, 16.0f};
    const float head_uv[4] = {0.0f, 0.0f, 16.0f, 16.0f};
    const float *from[6] = {stem_a0, stem_a0, stem_b0, stem_b0, head0, head0};
    const float *to[6] = {stem_a1, stem_a1, stem_b1, stem_b1, head1, head1};
    const int face[6] = {BM_NORTH, BM_SOUTH, BM_WEST, BM_EAST, BM_WEST, BM_EAST};
    const int axis[6] = {1, 1, 1, 1, 2, 2};
    const float angle[6] = {45.0f, 45.0f, 45.0f, 45.0f, 22.5f, 22.5f};
    const int sprite[6] = {
        m->face[BM_NORTH].sprite, m->face[BM_NORTH].sprite,
        m->face[BM_NORTH].sprite, m->face[BM_NORTH].sprite,
        m->face[BM_WEST].sprite, m->face[BM_EAST].sprite,
    };
    uint64_t r = mc_position_random(wx, 0, wz);
    float offx = (((float)((r >> 16) & 15u) / 15.0f) - 0.5f) * 0.5f;
    float offz = (((float)((r >> 24) & 15u) / 15.0f) - 0.5f) * 0.5f;
    (void)L;
    for (int q = 0; q < 6; ++q) {
        CrVertex quad[4];
        bake_face_custom(wx, wy, wz, from[q], to[q], face[q],
                         q < 4 ? stem_uv : head_uv, 0,
                         1, axis[q], angle[q], origin, 1,
                         sprite[q], base01, 1.0f, tint, quad);
        for (int i = 0; i < 4; ++i) {
            quad[i].pos.x += offx;
            quad[i].pos.z += offz;
        }
        push_face(out, cap, m->layer, quad);
    }
}

/* Java MathHelper.getCoordinateRandom with int/long wrap made explicit. */
static uint64_t mc_position_random(int x, int y, int z) {
    int32_t xi = (int32_t)((uint32_t)x * 3129871u);
    uint64_t i = (uint64_t)(int64_t)xi
               ^ (uint64_t)((int64_t)z * 116129781LL)
               ^ (uint64_t)(int64_t)y;
    return i * i * UINT64_C(42317861) + i * UINT64_C(11);
}

/* WeightedBakedModel.getRandomModel: equal-weight variants in JSON order. Each
 * matching MultipartBakedModel selector receives rand++, hence selector_offset. */
static int weighted_variant(int wx, int wy, int wz, int selector_offset, int n) {
    uint64_t r = mc_position_random(wx, wy, wz) + (uint64_t)selector_offset;
    int32_t hi = (int32_t)(uint32_t)r >> 16;
    int mag = hi < 0 ? -hi : hi;
    return mag % n;
}

/* Test hooks for Java-confirmed BlockModelShapes/WeightedBakedModel vectors. */
uint64_t worldmc_test_position_random(int x, int y, int z) {
    return mc_position_random(x, y, z);
}
int worldmc_test_fire_variant(int x, int y, int z, int selector_offset, int n) {
    return weighted_variant(x, y, z, selector_offset, n);
}

static void rotate_quad_y(CrVertex q[4], int wx, int wz, int quarter_turns) {
    int qn = quarter_turns & 3;
    float cx = (float)wx + 0.5f, cz = (float)wz + 0.5f;
    for (int i = 0; i < 4; ++i) {
        float x = q[i].pos.x - cx, z = q[i].pos.z - cz;
        float rx = x, rz = z;
        if (qn == 1) { rx = -z; rz = x; }
        else if (qn == 2) { rx = -x; rz = -z; }
        else if (qn == 3) { rx = z; rz = -x; }
        q[i].pos.x = cx + rx;
        q[i].pos.z = cz + rz;
    }
}

static int rotate_face_y(int face, int quarter_turns) {
    int qn = quarter_turns & 3;
    if (face == BM_UP || face == BM_DOWN || qn == 0) return face;
    static const int ring[4] = { BM_NORTH, BM_EAST, BM_SOUTH, BM_WEST };
    int at = 0;
    while (at < 4 && ring[at] != face) at++;
    return at < 4 ? ring[(at + qn) & 3] : face;
}

static void fire_flip_u(CrVertex q[4], int sprite) {
    float u0, v0, u1, v1;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    (void)v0; (void)v1;
    for (int i = 0; i < 4; ++i) q[i].uv.x = u0 + u1 - q[i].uv.x;
}

/* Supported-from-below BlockFire actual state: all direction flags false.
 * fire.json then matches the floor selector plus the N/E/S/W fallback selectors.
 * Each selector chooses ONE equal-weight variant from the position hash:
 *   floor: fire_floor0/1 (4 tilted one-sided quads)
 *   side:  side0/1/alt0/alt1 (one two-sided plane), rotated per cardinal
 * Total = 12 baked quads = 72 triangle vertices, AO/shade both disabled. */
static void emit_fire(CrChunkMeshMC *out, int *cap, const CrLight *L,
                      int wx, int wy, int wz, const BmBlock *m,
                      CrRgba tint, float base01) {
    (void)L;
    int spr0 = m->face[BM_NORTH].sprite;
    int spr1 = m->face[BM_SOUTH].sprite;
    const float uv[4] = { 0.0f, 0.0f, 16.0f, 16.0f };
    const float origin[3] = { 0.5f, 0.5f, 0.5f };
    const float floor_from[4][3] = {
        {0.0f,0.0f,8.8f}, {0.0f,0.0f,7.2f},
        {8.8f,0.0f,0.0f}, {7.2f,0.0f,0.0f},
    };
    const float floor_to[4][3] = {
        {16.0f,22.4f,8.8f}, {16.0f,22.4f,7.2f},
        {8.8f,22.4f,16.0f}, {7.2f,22.4f,16.0f},
    };
    const int floor_face[4] = { BM_SOUTH, BM_NORTH, BM_WEST, BM_EAST };
    const int floor_axis[4] = { 0, 0, 2, 2 };
    const float floor_angle[4] = { -22.5f, 22.5f, -22.5f, 22.5f };
    int floor_sprite = weighted_variant(wx, wy, wz, 0, 2) ? spr1 : spr0;
    for (int i = 0; i < 4; ++i) {
        CrVertex q[4];
        bake_face_custom(wx, wy, wz, floor_from[i], floor_to[i], floor_face[i],
                         uv, 0, 1, floor_axis[i], floor_angle[i], origin, 1,
                         floor_sprite, base01, 1.0f, tint, q);
        push_face(out, cap, m->layer, q);
    }

    const float sf[3] = { 0.0f, 0.0f, 0.01f };
    const float st[3] = { 16.0f, 22.4f, 0.01f };
    for (int dir = 0; dir < 4; ++dir) {
        int variant = weighted_variant(wx, wy, wz, dir + 1, 4);
        int sprite = (variant == 1 || variant == 3) ? spr1 : spr0;
        int alt = variant >= 2;
        const int faces[2] = { BM_SOUTH, BM_NORTH };
        for (int f = 0; f < 2; ++f) {
            CrVertex q[4];
            bake_face_custom(wx, wy, wz, sf, st, faces[f], uv, 0,
                             0, 3, 0.0f, NULL, 0,
                             sprite, base01, 1.0f, tint, q);
            if (alt) fire_flip_u(q, sprite);
            rotate_quad_y(q, wx, wz, dir);
            push_face(out, cap, m->layer, q);
        }
    }
}

/* portal_{ns,ew}.json. Legacy meta 1 is axis X (z-thin, N/S faces), meta 2 is
 * axis Z (x-thin, E/W faces). The portal texture is translucent but the baked
 * model itself has only two shaded faces, never a six-face full cube. */
static void emit_portal(CrChunkMeshMC *out, int *cap, const CrLight *L,
                        int wx, int wy, int wz, const BmBlock *m,
                        CrRgba tint, float base01) {
    int meta = light_meta(L, wx, wy, wz);
    int axis_z = (meta & 3) == 2;
    const float z_from[3] = { 6.0f, 0.0f, 0.0f };
    const float z_to[3]   = { 10.0f, 16.0f, 16.0f };
    const float x_from[3] = { 0.0f, 0.0f, 6.0f };
    const float x_to[3]   = { 16.0f, 16.0f, 10.0f };
    const float *from = axis_z ? z_from : x_from;
    const float *to = axis_z ? z_to : x_to;
    const int faces_z[2] = { BM_EAST, BM_WEST };
    const int faces_x[2] = { BM_NORTH, BM_SOUTH };
    const int *faces = axis_z ? faces_z : faces_x;
    for (int i = 0; i < 2; ++i) {
        int face = faces[i];
        CrVertex q[4];
        bake_face(wx, wy, wz, from, to, face, 0, 3, 0.0f, NULL, 0,
                  m->face[face].sprite, base01 * FACES[face].shade,
                  1.0f, tint, q);
        push_face(out, cap, m->layer, q);
    }
}

/* TileEntityEndPortalRenderer (1.11.2): shouldRenderFace is UP only; getOffset
 * places the plane at y=0.75. Full GL texgen + multi-pass projected end_sky /
 * end_portal is not available in the chunk mesher, so we bake a deterministic
 * multi-layer UP-face stack tinted with the Java Random(31100L) colour sequence
 * (first 8 of the 15 close-range passes). Phase is fixed (seed replay-stable);
 * live wall-clock UV scroll is a residual fidelity gap. Block light level is
 * fullbright (BlockEndPortal.setLightLevel(1.0F)). */
static void emit_end_portal(CrChunkMeshMC *out, int *cap, const CrLight *L,
                            int wx, int wy, int wz, const BmBlock *m,
                            CrRgba base_tint, float base01) {
    (void)L; (void)base01;
    /* Precomputed (RANDOM.nextFloat()*0.5+bias)*f1 for j=0..7 with seed 31100L
     * and f1 = j==0 ? 0.15 : 2/(18-j). RGB as 0..255. */
    static const unsigned char LAYER_RGB[8][3] = {
        {  5, 25, 28 }, {  3, 24, 22 }, {  7, 25, 25 }, { 11, 28, 29 },
        { 16, 30, 24 }, { 16, 22, 31 }, { 21, 28, 42 }, { 24, 39, 23 },
    };
    int sprite = m->face[BM_UP].sprite;
    for (int j = 0; j < 8; ++j) {
        /* y = 0.75 block = 12 model units (getOffset). Micro offsets avoid
         * z-fighting when stacking translucent layers. */
        float y = 12.0f + (float)j * 0.02f;
        const float from_j[3] = { 0.0f, y, 0.0f };
        const float to_j[3]   = { 16.0f, y, 16.0f };
        CrRgba tint = {
            (u8)((LAYER_RGB[j][0] * (int)base_tint.r + 127) / 255),
            (u8)((LAYER_RGB[j][1] * (int)base_tint.g + 127) / 255),
            (u8)((LAYER_RGB[j][2] * (int)base_tint.b + 127) / 255),
            /* first pass SRC_ALPHA (sky-ish), rest additive-ish via translucent */
            (u8)(j == 0 ? 200 : 180)
        };
        /* fullbright: shade=false equivalent, light=1 */
        CrVertex quad[4];
        bake_face(wx, wy, wz, from_j, to_j, BM_UP, 0, 3, 0.0f, NULL, 0,
                  sprite, 1.0f, 1.0f, tint, quad);
        push_face(out, cap, m->layer, quad);
    }
}

/* end_portal_frame_{empty,filled}.json. Meta bits 0..1 rotate SOUTH/WEST/
 * NORTH/EAST; bit 2 selects the eye box. Geometry and face UV rectangles are
 * copied from the 1.11.2 JAR models. */
static void emit_end_frame(CrChunkMeshMC *out, int *cap, const CrLight *L,
                           int wx, int wy, int wz, const BmBlock *m,
                           CrRgba tint, float base01) {
    int meta = light_meta(L, wx, wy, wz);
    int quarter_turns = meta & 3;
    const float base_from[3] = { 0.0f, 0.0f, 0.0f };
    const float base_to[3]   = { 16.0f, 13.0f, 16.0f };
    const float full_uv[4]   = { 0.0f, 0.0f, 16.0f, 16.0f };
    const float side_uv[4]   = { 0.0f, 3.0f, 16.0f, 16.0f };
    for (int face = 0; face < 6; ++face) {
        const float *uv = (face >= BM_NORTH) ? side_uv : full_uv;
        int sprite = face == BM_DOWN ? m->face[BM_DOWN].sprite
                   : face == BM_UP ? m->face[BM_UP].sprite
                   : m->face[BM_NORTH].sprite;
        int shaded_face = rotate_face_y(face, quarter_turns);
        CrVertex q[4];
        bake_face_custom(wx, wy, wz, base_from, base_to, face, uv, 0,
                         0, 3, 0.0f, NULL, 0, sprite,
                         base01 * FACES[shaded_face].shade, 1.0f, tint, q);
        rotate_quad_y(q, wx, wz, quarter_turns);
        push_face(out, cap, m->layer, q);
    }

    if ((meta & 4) != 0) {
        const float eye_from[3] = { 4.0f, 13.0f, 4.0f };
        const float eye_to[3]   = { 12.0f, 16.0f, 12.0f };
        const float eye_tb_uv[4] = { 4.0f, 4.0f, 12.0f, 12.0f };
        const float eye_side_uv[4] = { 4.0f, 0.0f, 12.0f, 3.0f };
        for (int face = 0; face < 6; ++face) {
            const float *uv = (face >= BM_NORTH) ? eye_side_uv : eye_tb_uv;
            int shaded_face = rotate_face_y(face, quarter_turns);
            CrVertex q[4];
            bake_face_custom(wx, wy, wz, eye_from, eye_to, face, uv, 0,
                             0, 3, 0.0f, NULL, 0,
                             m->face[BM_EAST].sprite,
                             base01 * FACES[shaded_face].shade, 1.0f, tint, q);
            rotate_quad_y(q, wx, wz, quarter_turns);
            push_face(out, cap, m->layer, q);
        }
    }
}

static int rotate_model_face(int face, int qx, int qy) {
    for (int i = 0; i < (qx & 3); ++i) face = ROTX_FACE[face];
    for (int i = 0; i < (qy & 3); ++i) face = ROTY_FACE[face];
    return face;
}

/* ForgeHooksClient.applyUVLock for the X0/X180 and quarter-Y rotations used by
 * stairs. The transform applies to the JSON UV rectangle before FaceBakery. */
static int stair_uv_lock(const float src[4], int face, int qx, int qy,
                         float dst[4]) {
    int turns = 0;
    if (face == BM_DOWN || face == BM_UP) {
        int sign = face == BM_DOWN ? 1 : -1;
        if (qx == 2) sign = -sign;
        turns = (sign * qy) & 3;
    } else if (qx == 2) {
        turns = 2;
    }
    switch (turns) {
        case 1:
            dst[0] = src[1];         dst[1] = 16.0f - src[2];
            dst[2] = src[3];         dst[3] = 16.0f - src[0];
            break;
        case 2:
            dst[0] = 16.0f - src[2]; dst[1] = 16.0f - src[3];
            dst[2] = 16.0f - src[0]; dst[3] = 16.0f - src[1];
            break;
        case 3:
            dst[0] = 16.0f - src[3]; dst[1] = src[0];
            dst[2] = 16.0f - src[1]; dst[3] = src[2];
            break;
        default:
            memcpy(dst, src, 4 * sizeof(float));
            break;
    }
    return turns;
}

/* models/block/stairs.json plus its uvlock blockstate rotations. Keeping the
 * two source elements intact matters: their explicit half-texture UVs are not
 * the auto-UVs of the rotated axis-aligned boxes. */
static void emit_stairs(CrChunkMeshMC *out, int *cap, const CrLight *L,
                        int wx, int wy, int wz, const BmBlock *m,
                        int sprite, CrRgba tint, float base01) {
    static const float from[2][3] = {
        {0, 0, 0}, {8, 8, 0},
    };
    static const float to[2][3] = {
        {16, 8, 16}, {16, 16, 16},
    };
    static const float uv[2][6][4] = {
        {
            {0,0,16,16}, {0,0,16,16},
            {0,8,16,16}, {0,8,16,16},
            {0,8,16,16}, {0,8,16,16},
        },
        {
            {8,0,16,16}, {8,0,16,16},
            {0,0,8,8}, {8,0,16,8},
            {0,0,16,8}, {0,0,16,8},
        },
    };
    static const unsigned cull_mask[2] = {
        0x3fu & ~(1u << BM_UP),
        0x3fu & ~(1u << BM_WEST),
    };
    int meta = light_meta(L, wx, wy, wz) & 7;
    int qx = (meta & 4) ? 2 : 0;
    static const int qy_for_facing[4] = {0, 2, 1, 3};
    int qy = qy_for_facing[meta & 3];

    for (int elem = 0; elem < 2; ++elem) {
        for (int face = 0; face < 6; ++face) {
            int world_face = rotate_model_face(face, qx, qy);
            if (cull_mask[elem] & (1u << face)) {
                const Face *fc = &FACES[world_face];
                int ncb = light_block(L, wx + fc->n[0], wy + fc->n[1],
                                      wz + fc->n[2]);
                const BmBlock *nm = bm_block(ncb);
                if (nm->is_full_cube && nm->layer == CR_LAYER_SOLID) continue;
            }
            float locked_uv[4];
            int uv_quarter = stair_uv_lock(uv[elem][face], face, qx, qy,
                                           locked_uv);
            CrVertex quad[4];
            bake_face_custom(wx, wy, wz, from[elem], to[elem], face,
                             locked_uv, uv_quarter, 0, 3, 0.0f, NULL, 0,
                             sprite, base01 * FACES[world_face].shade,
                             1.0f, tint, quad);
            if (qx) rotate_quad_x(quad, wy, wz, qx);
            if (qy) rotate_quad_y(quad, wx, wz, qy);
            push_face(out, cap, m->layer, quad);
        }
    }
}

/* BlockDoublePlant.getActualState: upper half copies VARIANT from the block
 * below. getType falls back to FERN when the below cell is not this block. */
static int dplant_type_from_below(const CrLight *L, int wx, int wy, int wz) {
    if (wy <= 0) return 3;
    int below = light_block(L, wx, wy - 1, wz);
    if (below >= CR_CB_DPLANT_LOWER_BASE && below < CR_CB_DPLANT_UPPER)
        return below - CR_CB_DPLANT_LOWER_BASE;
    /* Snapshot / live edits store canonical state; lower meta is type 0..5. */
    uint16_t st = light_state(L, wx, wy - 1, wz);
    int id = (int)(st >> 4), meta = (int)(st & 15);
    if (id == 175 && (meta & 8) == 0) return meta & 7;
    return 3; /* EnumPlantType.FERN */
}

/* Dispatch a non-cube block model. */
static void emit_noncube(CrChunkMeshMC *out, int *cap, const CrLight *L,
                         int cb, const BmBlock *m, int wx, int wy, int wz) {
    int dplant_type = -1;
    /* Upper double-plant: resolve model from lower half (BlockDoublePlant
     * getActualState). Without this, every upper is grass-top + grass tint and
     * forest lilac/rose/peony render as opaque biome-green slabs. */
    if (cb == CR_CB_DPLANT_UPPER) {
        dplant_type = dplant_type_from_below(L, wx, wy, wz);
        m = bm_dplant_upper(dplant_type);
    } else if (cb >= CR_CB_DPLANT_LOWER_BASE && cb < CR_CB_DPLANT_UPPER) {
        dplant_type = cb - CR_CB_DPLANT_LOWER_BASE;
    }
    int side_spr = m->face[BM_NORTH].sprite;
    int side_tint = m->face[BM_NORTH].tint;
    /* BlockColors DOUBLE_PLANT: grass/fern upper samples biome at pos.down(). */
    int tint_wy = (cb == CR_CB_DPLANT_UPPER && side_tint == BM_TINT_GRASS && wy > 0)
                      ? wy - 1 : wy;
    CrRgba tint = face_tint(L, side_tint, wx, tint_wy, wz);
    /* BlockFluidRenderer's live Java output uses the texture/tint at full
     * brightness, then applies the per-face 1/.8/.6/.5 multiplier below. Feeding
     * the scalar lightmap again here darkened surface water by almost exactly 2x. */
    float base01 = m->kind == BM_KIND_FLUID ? 1.0f :
        (((m->kind == BM_KIND_CROSS && cb != CR_CB_WEB) ||
          m->kind == BM_KIND_DPLANT_SUNFLOWER_TOP) ||
         m->kind == BM_KIND_SLAB_BOTTOM || m->kind == BM_KIND_SLAB_TOP ||
         m->kind == BM_KIND_STAIRS ||
         m->kind == BM_KIND_IRON_BARS ||
         m->kind == BM_KIND_GLASS_PANE ||
         m->kind == BM_KIND_TORCH ||
         m->kind == BM_KIND_TRAPDOOR || m->kind == BM_KIND_LADDER ||
         m->kind == BM_KIND_CACTUS)
        ? neighbor_model_light01(L, wx, wy, wz,
                                 m->kind == BM_KIND_TORCH ? 14 : 0, &tint)
        : cell_light01(L, wx, wy, wz, &tint);
    switch (m->kind) {
        case BM_KIND_CROSS:
            /* Sunflower upper: double_sunflower_top.json - half-height cross
             * (y 0..8, UV v 8..16) plus a 22.5deg face plane. Other types are
             * plain cross / tinted_cross full height. */
            if (cb == CR_CB_DPLANT_UPPER && dplant_type == 0) {
                const float origin[3] = { 0.5f, 0.5f, 0.5f };
                const float aFrom[3] = { 0.8f, 0.0f, 8.0f }, aTo[3] = { 15.2f, 8.0f, 8.0f };
                const float bFrom[3] = { 8.0f, 0.0f, 0.8f }, bTo[3] = { 8.0f, 8.0f, 15.2f };
                const float half_uv[4] = { 0.0f, 8.0f, 16.0f, 16.0f };
                const int planeFace[4] = { BM_NORTH, BM_SOUTH, BM_WEST, BM_EAST };
                uint64_t r = mc_position_random(wx, 0, wz);
                float offx = (((float)((r >> 16) & 15u) / 15.0f) - 0.5f) * 0.5f;
                float offz = (((float)((r >> 24) & 15u) / 15.0f) - 0.5f) * 0.5f;
                for (int q = 0; q < 4; ++q) {
                    const float *from = (q < 2) ? aFrom : bFrom;
                    const float *to   = (q < 2) ? aTo   : bTo;
                    CrVertex quad[4];
                    bake_face_custom(wx, wy, wz, from, to, planeFace[q], half_uv, 0,
                                     1, 1, 45.0f, origin, 1,
                                     side_spr, base01, 1.0f, tint, quad);
                    for (int i = 0; i < 4; ++i) {
                        quad[i].pos.x += offx;
                        quad[i].pos.z += offz;
                    }
                    push_face(out, cap, m->layer, quad);
                }
                /* Face plane: from [9.6,-1,1]..[9.6,15,15], rot z 22.5, front/back. */
                {
                    const float fFrom[3] = { 9.6f, -1.0f, 1.0f };
                    const float fTo[3]   = { 9.6f, 15.0f, 15.0f };
                    const float full_uv[4] = { 0.0f, 0.0f, 16.0f, 16.0f };
                    CrRgba white = { 255, 255, 255, 255 };
                    int front = bm_dplant_sunflower_front_sprite();
                    int back  = bm_dplant_sunflower_back_sprite();
                    for (int fi = 0; fi < 2; ++fi) {
                        int face = fi ? BM_EAST : BM_WEST;
                        int spr = fi ? front : back;
                        CrVertex quad[4];
                        bake_face_custom(wx, wy, wz, fFrom, fTo, face, full_uv, 0,
                                         1, 2, 22.5f, origin, 1,
                                         spr, base01, 1.0f, white, quad);
                        for (int i = 0; i < 4; ++i) {
                            quad[i].pos.x += offx;
                            quad[i].pos.z += offz;
                        }
                        push_face(out, cap, m->layer, quad);
                    }
                }
            } else {
                emit_cross(out, cap, m->layer, L, cb, wx, wy, wz, side_spr, tint,
                           base01);
            }
            break;
        case BM_KIND_DPLANT_SUNFLOWER_TOP:
            emit_double_sunflower_top(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_SLAB_BOTTOM: {
            emit_slab(out, cap, L, m, wx, wy, wz, tint, base01);
            break;
        }
        case BM_KIND_SLAB_TOP: {
            emit_slab(out, cap, L, m, wx, wy, wz, tint, base01);
            break;
        }
        case BM_KIND_STAIRS: {
            emit_stairs(out, cap, L, wx, wy, wz, m, side_spr, tint, base01);
            break;
        }
        case BM_KIND_FENCE: {
            const float pf[3] = {6,0,6}, pt[3] = {10,16,10};  /* centre post */
            emit_box(out, cap, m->layer, L, wx, wy, wz, pf, pt, side_spr, tint, base01, -1);
            int connected[4] = {
                cb == CR_CB_TEST_FENCE ||
                    light_block(L, wx, wy, wz - 1) == cb,
                cb == CR_CB_TEST_FENCE ||
                    light_block(L, wx, wy, wz + 1) == cb,
                cb == CR_CB_TEST_FENCE ||
                    light_block(L, wx - 1, wy, wz) == cb,
                cb == CR_CB_TEST_FENCE ||
                    light_block(L, wx + 1, wy, wz) == cb,
            };
            /* fence_{north,south,east,west}.json: two separate rails per
             * connection, preserving the 3-pixel vertical gap. */
            const float rails[4][2][3] = {
                { {7,0,0},  {9,0,9}  },   /* north */
                { {7,0,7},  {9,0,16} },   /* south */
                { {0,0,7},  {9,0,9}  },   /* west  */
                { {7,0,7},  {16,0,9} },   /* east  */
            };
            for (int i = 0; i < 4; ++i) if (connected[i]) {
                float lo_from[3] = {rails[i][0][0], 6, rails[i][0][2]};
                float lo_to[3] = {rails[i][1][0], 9, rails[i][1][2]};
                float hi_from[3] = {rails[i][0][0], 12, rails[i][0][2]};
                float hi_to[3] = {rails[i][1][0], 15, rails[i][1][2]};
                emit_box(out, cap, m->layer, L, wx, wy, wz, lo_from, lo_to,
                         side_spr, tint, base01, -1);
                emit_box(out, cap, m->layer, L, wx, wy, wz, hi_from, hi_to,
                         side_spr, tint, base01, -1);
            }
            break;
        }
        case BM_KIND_WALL: {
            int north = light_block(L, wx, wy, wz - 1) == CR_CB_COBBLESTONE_WALL;
            int south = light_block(L, wx, wy, wz + 1) == CR_CB_COBBLESTONE_WALL;
            int west = light_block(L, wx - 1, wy, wz) == CR_CB_COBBLESTONE_WALL;
            int east = light_block(L, wx + 1, wy, wz) == CR_CB_COBBLESTONE_WALL;
            int straight_ns = north && south && !west && !east;
            int straight_ew = east && west && !north && !south;
            int up = !(straight_ns || straight_ew) || light_block(L, wx, wy + 1, wz) != 0;
            if (up) {
                const float from[3] = {4,0,4}, to[3] = {12,16,12};
                emit_box(out, cap, m->layer, L, wx, wy, wz, from, to,
                         side_spr, tint, base01, -1);
            }
            const float arms[4][2][3] = {
                { {5,0,0}, {11,14,8} },
                { {5,0,8}, {11,14,16} },
                { {0,0,5}, {8,14,11} },
                { {8,0,5}, {16,14,11} },
            };
            int connected[4] = {north, south, west, east};
            for (int i = 0; i < 4; ++i) if (connected[i])
                emit_box(out, cap, m->layer, L, wx, wy, wz,
                         arms[i][0], arms[i][1], side_spr, tint, base01, -1);
            break;
        }
        case BM_KIND_FLUID: {
            emit_fluid(out, cap, m->layer, L, wx, wy, wz, cb, m,
                       tint, base01);
            break;
        }
        case BM_KIND_LILY:
            emit_lily(out, cap, m->layer, L, wx, wy, wz, side_spr, tint, base01);
            break;
        case BM_KIND_RAIL:
            emit_rail(out, cap, m->layer, L, wx, wy, wz, side_spr, tint, base01);
            break;
        case BM_KIND_TRAPDOOR:
            emit_trapdoor(out, cap, L, m, wx, wy, wz, tint, base01);
            break;
        case BM_KIND_LADDER:
            emit_ladder(out, cap, m->layer, L, wx, wy, wz,
                        side_spr, tint, base01);
            break;
        case BM_KIND_SNOW_LAYER:
            emit_snow_layer(out, cap, m->layer, L, wx, wy, wz, side_spr, tint, base01);
            break;
        case BM_KIND_PRESSURE_PLATE: {
            const float from[3] = {1,0,1}, to[3] = {15,1,15};
            emit_box(out, cap, m->layer, L, wx, wy, wz, from, to,
                     side_spr, tint, base01, -1);
            break;
        }
        case BM_KIND_VINE:
            emit_vine(out, cap, m->layer, L, wx, wy, wz, side_spr, tint, base01);
            break;
        case BM_KIND_CACTUS:
            emit_cactus(out, cap, m->layer, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_FIRE:
            if (g_emit_fire_mesh)
                emit_fire(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_PORTAL:
            emit_portal(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_END_FRAME:
            emit_end_frame(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_END_PORTAL:
            emit_end_portal(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_IRON_BARS:
            emit_iron_bars(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_GLASS_PANE:
            emit_glass_pane(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_TORCH:
            emit_torch(out, cap, L, wx, wy, wz, m, tint, base01);
            break;
        case BM_KIND_END_ROD: {
            int facing = light_meta(L, wx, wy, wz) & 7;
            float bf[3] = {6,0,6}, bt[3] = {10,1,10};
            float sf[3] = {7,1,7}, st[3] = {9,16,9};
            if (facing == 0) { /* down */
                bf[1] = 15; bt[1] = 16; sf[1] = 0; st[1] = 15;
            } else if (facing == 2 || facing == 3) { /* north/south */
                bf[0] = 6; bt[0] = 10; bf[1] = 6; bt[1] = 10;
                sf[0] = 7; st[0] = 9; sf[1] = 7; st[1] = 9;
                if (facing == 2) {
                    bf[2] = 15; bt[2] = 16; sf[2] = 0; st[2] = 15;
                } else {
                    bf[2] = 0; bt[2] = 1; sf[2] = 1; st[2] = 16;
                }
            } else if (facing == 4 || facing == 5) { /* west/east */
                bf[1] = 6; bt[1] = 10; bf[2] = 6; bt[2] = 10;
                sf[1] = 7; st[1] = 9; sf[2] = 7; st[2] = 9;
                if (facing == 4) {
                    bf[0] = 15; bt[0] = 16; sf[0] = 0; st[0] = 15;
                } else {
                    bf[0] = 0; bt[0] = 1; sf[0] = 1; st[0] = 16;
                }
            }
            emit_box(out, cap, m->layer, L, wx, wy, wz, bf, bt,
                     side_spr, tint, base01, -1);
            emit_box(out, cap, m->layer, L, wx, wy, wz, sf, st,
                     side_spr, tint, base01, -1);
            break;
        }
        case BM_KIND_CHORUS_PLANT: {
            const float center0[3] = {4,4,4}, center1[3] = {12,12,12};
            emit_box(out, cap, m->layer, L, wx, wy, wz,
                     center0, center1, side_spr, tint, base01, -1);
            /* BlockChorusPlant.getActualState: horizontal/up connect to plant
             * or flower; down additionally connects to End stone. */
            int connected[6] = {
                light_block(L, wx, wy - 1, wz) == 250
                    || light_block(L, wx, wy - 1, wz) == 251
                    || light_block(L, wx, wy - 1, wz) == 252
                    || light_block(L, wx, wy - 1, wz) == 212,
                light_block(L, wx, wy + 1, wz) == 250
                    || light_block(L, wx, wy + 1, wz) == 251
                    || light_block(L, wx, wy + 1, wz) == 252,
                light_block(L, wx, wy, wz - 1) == 250
                    || light_block(L, wx, wy, wz - 1) == 251
                    || light_block(L, wx, wy, wz - 1) == 252,
                light_block(L, wx, wy, wz + 1) == 250
                    || light_block(L, wx, wy, wz + 1) == 251
                    || light_block(L, wx, wy, wz + 1) == 252,
                light_block(L, wx - 1, wy, wz) == 250
                    || light_block(L, wx - 1, wy, wz) == 251
                    || light_block(L, wx - 1, wy, wz) == 252,
                light_block(L, wx + 1, wy, wz) == 250
                    || light_block(L, wx + 1, wy, wz) == 251
                    || light_block(L, wx + 1, wy, wz) == 252
            };
            static const float arm0[6][3] = {
                {4,0,4},{4,12,4},{4,4,0},{4,4,12},{0,4,4},{12,4,4}
            };
            static const float arm1[6][3] = {
                {12,4,12},{12,16,12},{12,12,4},{12,12,16},{4,12,12},{16,12,12}
            };
            for (int i = 0; i < 6; ++i)
                if (connected[i])
                    emit_box(out, cap, m->layer, L, wx, wy, wz,
                             arm0[i], arm1[i], side_spr, tint, base01, -1);
            break;
        }
        case BM_KIND_CHORUS_FLOWER: {
            static const float part0[6][3] = {
                {2,14,2},{0,2,2},{2,2,0},{2,2,14},{14,2,2},{2,0,2}
            };
            static const float part1[6][3] = {
                {14,16,14},{2,14,14},{14,14,2},{14,14,16},
                {16,14,14},{14,14,14}
            };
            for (int i = 0; i < 6; ++i) {
                int sprite = i == 5 ? m->face[BM_DOWN].sprite
                                    : m->face[BM_UP].sprite;
                emit_box(out, cap, m->layer, L, wx, wy, wz,
                         part0[i], part1[i], sprite, tint, base01, -1);
            }
            break;
        }
        case BM_KIND_CHEST: {
            /* ModelChest (TileEntityChestRenderer), closed lid pose:
             *   chestBelow: box(0,0,0, 14,10,14) at rp (1,6,1)  -> y 6..16
             *   chestLid:   box(0,-5,-14, 14,5,14) at rp (1,7,15) closed ax=0
             *               -> covers y 2..7 in model, mapped to block y 9..14
             *               with the hinge on +Z; closed lid sits on the body.
             *   chestKnob:  box(-1,-2,-15, 2,4,1) at rp (8,7,15)
             * World mesh has no per-frame lid_angle (TESR would remesh each
             * tick); closed pose matches vanilla when num_players_using==0.
             * Texture: oak-plank stand-in — entity/chest/normal.png is not in
             * the terrain atlas (atlas work is out of this scope). */
            const float body0[3] = {1.0f, 0.0f, 1.0f};
            const float body1[3] = {15.0f, 10.0f, 15.0f};
            emit_box(out, cap, m->layer, L, wx, wy, wz, body0, body1,
                     side_spr, tint, base01, -1);
            /* closed lid: 14x5x14 resting on body top (y 10..15) */
            const float lid0[3] = {1.0f, 10.0f, 1.0f};
            const float lid1[3] = {15.0f, 14.0f, 15.0f};
            emit_box(out, cap, m->layer, L, wx, wy, wz, lid0, lid1,
                     side_spr, tint, base01, -1);
            int meta = light_meta(L, wx, wy, wz) & 7;
            float k0[3], k1[3];
            /* knob on the facing side, mid-lid height (y 11..13) */
            if (meta == 3) { /* south */
                k0[0]=7; k0[1]=11; k0[2]=15; k1[0]=9; k1[1]=13; k1[2]=16;
            } else if (meta == 4) { /* west */
                k0[0]=0; k0[1]=11; k0[2]=7; k1[0]=1; k1[1]=13; k1[2]=9;
            } else if (meta == 5) { /* east */
                k0[0]=15; k0[1]=11; k0[2]=7; k1[0]=16; k1[1]=13; k1[2]=9;
            } else { /* north / 0 / 2 */
                k0[0]=7; k0[1]=11; k0[2]=0; k1[0]=9; k1[1]=13; k1[2]=1;
            }
            emit_box(out, cap, m->layer, L, wx, wy, wz, k0, k1,
                     side_spr, tint, base01, -1);
            break;
        }
        default: break;
    }
}

CrWorldMC *worldmc_create(long long seed) {
    return worldmc_create_type(seed, 0);
}

CrWorldMC *worldmc_create_type(long long seed, int world_type) {
    CrWorldMC *w = (CrWorldMC *)calloc(1, sizeof(CrWorldMC));
    if (!w) return NULL;
    w->light = light_create_type(seed, world_type);
    if (!w->light) { free(w); return NULL; }
    return w;
}

/* TEST HOOK: expose the internal light state (tests/test_mesh_models.c uses it
 * with light_debug_set_block to build a synthetic chunk). Not in the header. */
CrLight *worldmc_light(CrWorldMC *w) { return w ? w->light : NULL; }

void worldmc_destroy(CrWorldMC *w) {
    if (!w) return;
    light_destroy(w->light);
    free(w);
}

void worldmc_ensure(CrWorldMC *w, int ccx, int ccz, int radius) {
    if (!w) return;
    light_ensure(w->light, ccx, ccz, radius);
}

CrTexture worldmc_atlas(const CrWorldMC *w) {
    (void)w;
    return bm_atlas();
}

/* Shared meshing body. out->nverts[l] must be pre-zeroed; cap[l] is each layer's
 * capacity (updated when g_allow_grow). Returns total vertex count. */
static int mesh_body(CrWorldMC *w, int ccx, int ccz, CrChunkMeshMC *out, int *cap) {
    const CrLight *L = w->light;
    const int baseX = ccx * 16, baseZ = ccz * 16;

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            const int wx = baseX + lx, wz = baseZ + lz;
            for (int wy = 0; wy < 256; ++wy) {
                int cb = contextual_model_key(L, light_block(L, wx, wy, wz),
                                              wx, wy, wz);
                const BmBlock *m = bm_block(cb);
                if (m->is_air) continue;

                /* Non-cube models (stairs/slab/fence/cross-plant/fluid) are baked
                 * by the facebakery kernels; full cubes take the classic path. */
                if (m->kind != BM_KIND_CUBE) {
                    emit_noncube(out, cap, L, cb, m, wx, wy, wz);
                    continue;
                }

                int self_translucent = (m->layer == CR_LAYER_TRANSLUCENT);

                /* position-random model variant (stone/dirt/grass/sand/bedrock/
                 * netherrack): world face f shows model face g rotated onto it
                 * with UVs glued (ModelRotation, uvlock false); mirrored
                 * variants use cube_mirrored's explicit uv (U-flip). Cull,
                 * light, AO and shade all key on the FINAL face f. */
                int v_mir = 0, v_qx = 0, v_qy = 0;
                int v_on = variant_cube_params(cb, wx, wy, wz,
                                               &v_mir, &v_qx, &v_qy);
                static const float MIR_UV[4] = { 16.0f, 0.0f, 0.0f, 16.0f };

                for (int f = 0; f < 6; ++f) {
                    const Face *fc = &FACES[f];
                    int nx = wx + fc->n[0], ny = wy + fc->n[1], nz = wz + fc->n[2];
                    int ncb = light_block(L, nx, ny, nz);
                    const BmBlock *nm = bm_block(ncb);

                    /* Cull when the neighbour is an opaque (SOLID) full cube. For a
                     * translucent block, also skip the interface between two of the
                     * SAME translucent block (e.g. water-water). Water still draws
                     * against air / non-full cubes / different blocks. */
                    int neigh_opaque = nm->is_full_cube && nm->layer == CR_LAYER_SOLID;
                    if (neigh_opaque) continue;
                    if (self_translucent && ncb == cb) continue;

                    int g = v_on ? variant_model_face(f, v_qx, v_qy) : f;

                    CrRgba tint = face_tint(L, m->face[g].tint, wx, wy, wz);

                    /* combined sky/block light from the air cell the face looks
                     * into (fall back to the block cell if that is out of the
                     * vertical world range), folded with the directional shade. */
                    int sx = nx, sy = ny, sz = nz;
                    if (ny < 0 || ny > 255) { sx = wx; sy = wy; sz = wz; }
                    int s = mc_light_for_ext(L, 1, sx, sy, sz);
                    int b = mc_light_for_ext(L, 0, sx, sy, sz);
                    if (cb == CR_CB_MAGMA) { s = 15; b = 15; }
                    float combined01 = fold_lightmap(L, s, b, &tint);
                    float vlight = combined01 * fc->shade;

                    /* Full-cube face via FaceBakery (0.999 UV inset + MC corner
                     * order). Then re-bind light/ao/tint like the classic path. */
                    const float from16[3] = { 0.0f, 0.0f, 0.0f };
                    const float to16[3]   = { 16.0f, 16.0f, 16.0f };
                    CrVertex quad[4];
                    bake_face_custom(wx, wy, wz, from16, to16, g,
                                     v_mir ? MIR_UV : NULL, 0, 0, 3, 0.0f,
                                     NULL, 0, m->face[g].sprite, vlight, 1.0f,
                                     tint, quad);
                    if (v_on) {
                        if (v_qx) rotate_quad_x(quad, wy, wz, v_qx);
                        if (v_qy) rotate_quad_y(quad, wx, wz, v_qy);
                    }
                    for (int c = 0; c < 4; ++c) {
                        quad[c].light = vlight;
                        quad[c].tint = tint;
                        /* 3-neighbour AO (Java ao!=0 only). ao:0 flat path: ao=1.
                         * Use bakery position (block-local + world) to recover corner. */
                        if (vertex_ao_enabled()) {
                            int ox = (quad[c].pos.x > (float)wx + 0.5f) ? 1 : 0;
                            int oy = (quad[c].pos.y > (float)wy + 0.5f) ? 1 : 0;
                            int oz = (quad[c].pos.z > (float)wz + 0.5f) ? 1 : 0;
                            int off[3] = { ox, oy, oz };
                            int su = off[fc->tu] * 2 - 1;
                            int sv = off[fc->tv] * 2 - 1;
                            int du[3] = {0,0,0}, dv[3] = {0,0,0};
                            du[fc->tu] = su; dv[fc->tv] = sv;
                            int o1 = ao_occludes_at(L, nx + du[0], ny + du[1], nz + du[2]);
                            int o2 = ao_occludes_at(L, nx + dv[0], ny + dv[1], nz + dv[2]);
                            int oc = ao_occludes_at(L, nx + du[0] + dv[0],
                                                       ny + du[1] + dv[1],
                                                       nz + du[2] + dv[2]);
                            quad[c].ao = ao_level(o1, o2, oc);
                        } else {
                            quad[c].ao = 1.0f;
                        }
                        if (g_lvl_active) {
                            quad[c].light = g_lvl_s;
                            quad[c].blk   = g_lvl_b;
                            quad[c].ao    = quad[c].ao * fc->shade;
                        }
                    }

                    /* The current portal goldens are Java ao:0. Smooth AO needs
                     * a second interpolated lightmap attribute for exact dim RGB;
                     * keep the already-verified smooth path overworld-only. */
                    if (smooth_light_enabled() && light_dimension(L) == 0)
                        apply_smooth_fullcube_face(L, wx, wy, wz, f, quad);

                    /* models/block/slime.json element 0 before element 1:
                     * from [3,3,3] to [13,13,13], uv [3,3,13,13] every face,
                     * no cullface (generalQuads). Core first so SRC_ALPHA stacks
                     * under the outer shell (opacity 1-(1-a)^2, a~0.74 from
                     * slime.png). Real 3/16 inset, not a coplanar re-emit.
                     * BmBlock is single-box; dual-element geometry lives here
                     * like grass_side_overlay. Outer still uses BlockBreakable
                     * same-slime cull (ignoreSimilarity=false). */
                    if (cb == CR_CB_SLIME) {
                        static const float SLIME_CORE_FROM[3] = { 3.0f, 3.0f, 3.0f };
                        static const float SLIME_CORE_TO[3]   = { 13.0f, 13.0f, 13.0f };
                        static const float SLIME_CORE_UV[4]   = { 3.0f, 3.0f, 13.0f, 13.0f };
                        CrVertex iq[4];
                        bake_face_custom(wx, wy, wz, SLIME_CORE_FROM,
                                         SLIME_CORE_TO, g, SLIME_CORE_UV, 0,
                                         0, 3, 0.0f, NULL, 0,
                                         m->face[g].sprite, vlight, 1.0f,
                                         tint, iq);
                        if (v_on) {
                            if (v_qx) rotate_quad_x(iq, wy, wz, v_qx);
                            if (v_qy) rotate_quad_y(iq, wx, wz, v_qy);
                        }
                        for (int c = 0; c < 4; ++c) {
                            iq[c].light = quad[c].light;
                            iq[c].blk   = quad[c].blk;
                            iq[c].ao    = quad[c].ao;
                            iq[c].tint  = tint;
                        }
                        push_face(out, cap, m->layer, iq);
                    }

                    push_face(out, cap, m->layer, quad);

                    /* Vanilla block/grass.json has a SECOND element on the four
                     * side faces: grass_side_overlay with tintindex 0 (biome
                     * grass color), identical geometry and lighting, drawn over
                     * the untinted grass_side base. Emit it into CUTOUT_MIPPED
                     * (alpha-holes discard; drawn after SOLID); the coplanar
                     * quad passes the z-test via CrShadeCtx.depth_lequal
                     * (GL_LEQUAL, MC's depth func). Light/AO/blk are copied
                     * from the finished base quad so smooth lighting and the
                     * lightmap-coord mode stay bit-identical across the pair. */
                    if (cb == CR_CB_GRASS && f >= BM_NORTH) {
                        CrRgba otint = face_tint(L, BM_TINT_GRASS, wx, wy, wz);
                        (void)fold_lightmap(L, s, b, &otint);
                        CrVertex oq[4];
                        bake_face(wx, wy, wz, from16, to16, g, 0, 3, 0.0f, NULL, 0,
                                  bm_grass_side_overlay_sprite(), vlight, 1.0f,
                                  otint, oq);
                        if (v_on && v_qy) rotate_quad_y(oq, wx, wz, v_qy);
                        for (int c = 0; c < 4; ++c) {
                            oq[c].light = quad[c].light;
                            oq[c].blk   = quad[c].blk;
                            oq[c].ao    = quad[c].ao;
                            oq[c].tint  = otint;
                        }
                        push_face(out, cap, CR_LAYER_CUTOUT_MIPPED, oq);
                    }
                }

            }
        }
    }

    return out->nverts[0] + out->nverts[1] + out->nverts[2] + out->nverts[3];
}

/* Self-allocating path (unit test tests/test_mesh_models.c). Grows per-layer
 * buffers with realloc; caller releases with worldmc_free_mesh. */
int worldmc_mesh_chunk(CrWorldMC *w, int ccx, int ccz, CrChunkMeshMC *out) {
    if (!w || !out) return 0;
    memset(out, 0, sizeof(*out));
    int cap[4] = {0,0,0,0};
    g_allow_grow = 1;
    return mesh_body(w, ccx, ccz, out, cap);
}

/* Allocate-once path (game/world_live.c). out->verts[l] are caller-provided fixed
 * slabs of cap[l] verts each; out->nverts[l] is reset here. NO realloc: an overflow
 * asserts (see push_face). Returns total vertex count. */
int worldmc_mesh_chunk_into(CrWorldMC *w, int ccx, int ccz, CrChunkMeshMC *out,
                            const int cap[4]) {
    if (!w || !out) return 0;
    int capm[4] = { cap[0], cap[1], cap[2], cap[3] };
    for (int l = 0; l < 4; ++l) out->nverts[l] = 0;
    g_allow_grow = 0;
    int n = mesh_body(w, ccx, ccz, out, capm);
    g_allow_grow = 1;
    return n;
}

void worldmc_free_mesh(CrChunkMeshMC *m) {
    if (!m) return;
    for (int l = 0; l < 4; ++l) {
        free(m->verts[l]);
        m->verts[l] = NULL;
        m->nverts[l] = 0;
    }
}
