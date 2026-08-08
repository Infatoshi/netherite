/* game/world_live.c - LIVE streaming view-distance world over the MC-faithful mesher.
 *
 * Wraps a CrWorldMC (world/mesh_mc.c) + the CrLight block store and adds:
 *   - a per-loaded-chunk cache of the 4 per-CrRenderLayer CrVertex buffers
 *     (CrChunkMeshMC) with a `dirty` flag, so unchanged chunks are never re-meshed,
 *   - gm_world_mesh_view: build the 6 frustum planes from the camera EXACTLY like
 *     verify/chunk_scene.h (cr_perspective + cr_look_yaw_pitch + cr_frustum_
 *     extract), iterate the Chebyshev radius SCN_VIEW_RADIUS with the same cx-outer /
 *     cz-inner order, frustum-test each chunk's full [0,256] column AABB, mesh kept
 *     chunks (rebuild only if dirty or not yet meshed), frustum-test their non-empty
 *     16-block-high render sections, and concatenate the intersecting per-layer verts
 *     into world-owned buffers. Translucent quads are sorted far-to-near from the
 *     render camera without allocating during a frame.
 *   - gm_world_mesh_runs: the SAME walk (one shared implementation, wl_view_walk)
 *     recording the contiguous slab runs that concat would have copied instead of
 *     copying them, so a GPU backend can gather them from device-resident slabs.
 *   - W19 occlusion culling: a vanilla-mirror visibility graph. Each meshed
 *     chunk caches a 36-bit SetVisibility per 16-block section (which pairs of
 *     the six faces connect through non-opaque cells, VisGraph.computeVisibility),
 *     and each frame wl_view_walk floods RenderGlobal.setupTerrain's BFS out from
 *     the camera's section; sections the flood never reaches are not submitted.
 *     The filter sits INSIDE the shared walk, so the host-concat and the
 *     device-gather sink see the identical section set by construction.
 *     no_occlusion bypasses the BFS alone, no_cull bypasses everything.
 *     Culling only ever REMOVES sections that no path of non-opaque cells
 *     connects to the camera, so it is pixel-neutral: 480 still and 480 rotating
 *     frames over seeds 0/1000 are byte-identical with the BFS on and off, on
 *     both backends and through both sinks.
 *   - gm_world_block / gm_world_fill_window read canonical vanilla states through CrLight,
 *   - gm_world_set_block edits the store, re-lights, marks the touched chunk (and any
 *     border neighbour) dirty.
 *
 * Build STANDALONE via game/test_world_live.sh (no Makefile edits). Compile with
 * -ffp-contract=off -Wall -Wextra and -I. -Icore -I<blaze>/core.
 */
#include "game/game.h"
#include "game/caps.h"
#include "core/config.h"   /* cr_cfg()->debug_caps */
#include "core/types.h"
#include "core/frustum.h"
#include "world/mesh_mc.h"
#include "world/light.h"
#include "world/populate_mc.h"   /* popmc_window_builds (compute-once metric, debug) */
#include "assets/blockmodels.h"  /* bm_block: the mesher's own opaque-cube predicate */

/* raw blaze Chunk + mc_set/mc_state + PSV_DIM/PSV_R/PSV_NCHUNKS for fill_window. */
#include "player_survival.h"
#include "world_weather.h"   /* ww_init / ww_tick for live world-time composition */
#include "block_props_table.h"
#include "biome_props_full.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <float.h>
#include <limits.h>

/* Not in mesh_mc.h, but a real (non-static) symbol: hands us the internal CrLight so
 * we can read/edit blocks and relight through world/light.h. */
CrLight *worldmc_light(CrWorldMC *w);

/* View-distance radius: MUST match chunk_scene.h's SCN_VIEW_RADIUS for the regression
 * lock. chunk_scene.h #defines it; here we keep an independent copy of the same value. */
#ifndef SCN_VIEW_RADIUS
#define SCN_VIEW_RADIUS 12
#endif
#define WL_AABB_Y_MIN 0.0
#define WL_AABB_Y_MAX 256.0
#define WL_SECTIONS GM_MESH_SECTIONS
#define WL_SECTION_HEIGHT 16.0
#define WL_SECTION_Y_PAD 1.0

/* floor-division of a block coord to its chunk coord (identical to scn__floordiv16). */
static inline int wl_floordiv16(int a) { return a >> 4; }

/* ======================= vanilla visibility graph (W19) ======================
 * Mirror of net.minecraft.client.renderer.chunk.VisGraph + SetVisibility and of
 * RenderGlobal.setupTerrain's BFS flood over render sections. Both are pure CPU
 * bookkeeping over the sections world_live.c already partitions (W16); nothing
 * about the mesh, the vertex order, or either rasterizer changes.
 *
 * EnumFacing ordinals, verbatim (util/EnumFacing.java): DOWN 0, UP 1, NORTH 2,
 * SOUTH 3, WEST 4, EAST 5; getOpposite() == ordinal ^ 1 for all six.
 * Cell index inside a section is VisGraph.getIndex(x,y,z) = x<<0 | y<<8 | z<<4. */
enum { WL_F_DOWN = 0, WL_F_UP, WL_F_NORTH, WL_F_SOUTH, WL_F_WEST, WL_F_EAST };
#define WL_F_OPP(f)  ((f) ^ 1)
/* SetVisibility's BitSet(6*6): bit (a + b*6) means "face a connects to face b". */
#define WL_VIS_BIT(a, b) ((uint64_t)1 << ((a) + (b) * 6))
#define WL_VIS_ALL   (((uint64_t)1 << 36) - 1)   /* setAllVisible(true)  */
#define WL_VIS_NONE  ((uint64_t)0)               /* setAllVisible(false) */

/* VisGraph.INDEX_OF_EDGES: the 16^3 - 14^3 = 1352 boundary cells, in the static
 * initializer's exact (x, y, z) nesting order. */
#define WL_EDGE_N 1352
static int wl_edge_idx[WL_EDGE_N];
static int wl_edge_ready = 0;
static void wl_edge_init(void) {
    if (wl_edge_ready) return;
    int k = 0;
    for (int l = 0; l < 16; ++l)          /* x */
        for (int i1 = 0; i1 < 16; ++i1)   /* y */
            for (int j1 = 0; j1 < 16; ++j1) /* z */
                if (l == 0 || l == 15 || i1 == 0 || i1 == 15 || j1 == 0 || j1 == 15)
                    wl_edge_idx[k++] = l | (j1 << 4) | (i1 << 8);
    assert(k == WL_EDGE_N);
    wl_edge_ready = 1;
}

/* One reusable VisGraph scratch (VisGraph.bitSet + the flood queue). The flood
 * SETS bits as it visits, exactly like vanilla, so `opaque` is consumed by a
 * computeVisibility pass and must be refilled per section. */
typedef struct {
    unsigned char opaque[4096];
    int           queue[4096];
    int           empty;        /* VisGraph.empty */
} WlVisGraph;

/* Block.isOpaqueCube. Our mesher's own occluder predicate: a full cube drawn in
 * the SOLID layer. It is exactly the test mesh_mc.c uses to delete a neighbour
 * face (`nm->is_full_cube && nm->layer == CR_LAYER_SOLID`), so a cell that is
 * "opaque" here provably has six opaque faces and nothing renders through it.
 * Ice and slime are full cubes in the TRANSLUCENT layer -> not opaque, matching
 * vanilla; leaves are SOLID full cubes here (assets/blockmodels.c LEAF, the Fast
 * graphics BlockLeaves.isOpaqueCube==true branch) -> opaque, also matching. */
static inline int wl_cell_opaque(const CrLight *L, int wx, int wy, int wz) {
    int cb = light_block(L, wx, wy, wz);
    const BmBlock *m = bm_block(cb);
    if (cr_cfg()->fancy_leaves
            && (cb == 34 || cb == 35 || cb == 36 || cb == 78
                || cb == 83 || cb == 86))
        return 0;
    return m->is_full_cube && m->layer == CR_LAYER_SOLID;
}

/* VisGraph.setOpaqueCube over one 16^3 section (RenderChunk.rebuildChunk's loop). */
static void wl_vis_fill(WlVisGraph *g, const CrLight *L, int cx, int cz, int sec) {
    const int bx = cx * 16, bz = cz * 16, by = sec * 16;
    g->empty = 4096;
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int op = wl_cell_opaque(L, bx + x, by + y, bz + z);
                g->opaque[x | (z << 4) | (y << 8)] = (unsigned char)op;
                g->empty -= op;
            }
}

/* VisGraph.floodFill: flood over non-opaque cells from `start`, returning the
 * EnumFacing set VisGraph.addEdges accumulates (a bitmask over the six
 * ordinals). Marks every visited cell, start included.
 *
 * DELIBERATE WIDENING of vanilla: vanilla walks the 6 axis neighbours, we walk
 * all 26. A sight line is not restricted to axis steps - consecutive cells along
 * a ray need only share a corner - so the 6-connected component set is not a
 * superset of what a ray can pass through, and a face pair vanilla calls
 * disconnected can still be seen through. 26-connectivity only MERGES
 * components, so every pair vanilla marks connected stays connected and more are
 * added: strictly more conservative, never less. Measured cost at the seed-1000
 * jungle pose: 172 -> 171 sections occluded, +48 verts out of 1.53M. */
static int wl_vis_flood(WlVisGraph *g, int start) {
    int set = 0, head = 0, tail = 0;
    g->queue[tail++] = start;
    g->opaque[start] = 1;
    while (head < tail) {
        int i = g->queue[head++];
        int x = i & 15, z = (i >> 4) & 15, y = (i >> 8) & 15;
        /* addEdges */
        if (x == 0)       set |= 1 << WL_F_WEST;
        else if (x == 15) set |= 1 << WL_F_EAST;
        if (y == 0)       set |= 1 << WL_F_DOWN;
        else if (y == 15) set |= 1 << WL_F_UP;
        if (z == 0)       set |= 1 << WL_F_NORTH;
        else if (z == 15) set |= 1 << WL_F_SOUTH;
        /* getNeighborIndexAtFace over all six facings, WIDENED to all 26
         * neighbours (see wl_vis_flood's header comment). */
        for (int dy = -1; dy <= 1; ++dy) {
            if ((unsigned)(y + dy) > 15u) continue;
            for (int dz = -1; dz <= 1; ++dz) {
                if ((unsigned)(z + dz) > 15u) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((unsigned)(x + dx) > 15u) continue;
                    if (!dx && !dy && !dz) continue;
                    int j = i + dx + (dz << 4) + (dy << 8);
                    if (!g->opaque[j]) { g->opaque[j] = 1; g->queue[tail++] = j; }
                }
            }
        }
    }
    return set;
}

/* SetVisibility.setManyVisible: the full cross product of one flood component. */
static inline uint64_t wl_vis_many(int set) {
    uint64_t v = 0;
    for (int a = 0; a < 6; ++a) {
        if (!(set & (1 << a))) continue;
        for (int b = 0; b < 6; ++b)
            if (set & (1 << b)) v |= WL_VIS_BIT(a, b) | WL_VIS_BIT(b, a);
    }
    return v;
}

/* VisGraph.computeVisibility, including both shortcuts: fewer than 256 opaque
 * cells -> everything connects; zero non-opaque cells -> nothing connects. */
static uint64_t wl_vis_compute(WlVisGraph *g) {
    if (4096 - g->empty < 256) return WL_VIS_ALL;
    if (g->empty == 0)         return WL_VIS_NONE;
    uint64_t vis = 0;
    wl_edge_init();
    for (int k = 0; k < WL_EDGE_N; ++k) {
        int i = wl_edge_idx[k];
        if (!g->opaque[i]) vis |= wl_vis_many(wl_vis_flood(g, i));
    }
    return vis;
}

/* EnumFacing.getFacingFromVector: the axis facing with the largest dot product,
 * seeded with NORTH / Float.MIN_VALUE exactly as vanilla does. */
static int wl_facing_from_vector(float x, float y, float z) {
    static const float dv[6][3] = {
        {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}
    };
    int best = WL_F_NORTH;
    float f = FLT_MIN;
    for (int i = 0; i < 6; ++i) {
        float f1 = x * dv[i][0] + y * dv[i][1] + z * dv[i][2];
        if (f1 > f) { f = f1; best = i; }
    }
    return best;
}

/* ---- toroidal mesh-slab pool (ALLOCATE-ONCE) ----
 * A fixed (2R+1)^2 pool of full-chunk mesh slabs indexed by (cx,cz) modulo mesh_D.
 * The needed region around the player is exactly mesh_D x mesh_D chunks, so each
 * in-region chunk maps to a UNIQUE slot; a chunk scrolling out frees its slot for
 * the incoming chunk at the same modulo, which RECYCLES the slot (re-mesh). Each
 * slot owns one pre-allocated slab of caps.max_verts_per_chunk verts, into which the
 * 4 layers are packed contiguously; `view` is a CrChunkMeshMC pointing into the slab
 * so callers see the classic per-layer interface. No malloc/realloc/free after init. */
typedef struct {
    int           cx, cz;
    int           valid;    /* slab holds a real chunk's packed mesh   */
    int           dirty;    /* a block edit touched this chunk -> rebuild */
    int           builds;   /* rebuild count (test hook)               */
    int           revision; /* slab upload generation: rebuild or resort */
    int           trans_sorted; /* translucent slab sorted since rebuild */
    CrVertex     *buf;      /* packed slab: layer0|layer1|layer2|layer3 */
    int           off[4], n[4];
    int           sec_off[4][WL_SECTIONS];
    int           sec_n[4][WL_SECTIONS];
    /* W19: per-section SetVisibility (36-bit face-pair set), recomputed with the
     * geometry in wl_ensure_mesh and never touched per frame. */
    uint64_t      sec_vis[WL_SECTIONS];
    CrChunkMeshMC view;     /* verts[l]=buf+off[l], nverts[l]=n[l]     */
} WlSlot;

typedef struct {
    int32_t cx, cz;
    uint64_t offset;
    uint64_t applied_epoch;
} WlColdChunk;

typedef struct {
    int32_t cx, cz;
    int mutation_head;
    uint64_t applied_epoch;
} WlOverlayChunk;

typedef struct {
    uint16_t cell;
    uint16_t state;
    int next;
} WlOverlayMutation;

typedef struct {
    int32_t cx, cz;
    WlColdChunk *base;
    WlOverlayChunk *overlay;
} WlSaveChunk;

#define WL_COLD_CELLS (16u * 16u * 256u)
#define WL_COLD_BLOCK_BYTES (WL_COLD_CELLS * 2u)
#define WL_COLD_LIGHT_BYTES WL_COLD_CELLS
#define WL_COLD_BIOME_BYTES 256u
#define WL_COLD_HEIGHT_BYTES (256u * 4u)
#define WL_COLD_PAYLOAD_BYTES \
    (WL_COLD_BLOCK_BYTES + 2u * WL_COLD_LIGHT_BYTES \
     + WL_COLD_BIOME_BYTES + WL_COLD_HEIGHT_BYTES)

struct GmWorld {
    CrWorldMC    *wmc;
    CrLight      *light;    /* == worldmc_light(wmc) */
    const CrCaps *caps;

    WlSlot       *slots;    /* caps.mesh_slots slabs, toroidal */
    int           mesh_D, mesh_slots;

    /* one reusable mesh-into scratch (per-layer buffers each max_verts_per_chunk). */
    CrChunkMeshMC scratch;
    int           scratch_cap[4];

    /* fixed concatenated per-layer draw buffers (returned by mesh_view). */
    CrVertex     *out_verts[4];
    int           out_nverts[4];

    /* VertexBuffer.sortVertexData scratch. One chunk is sorted at a time, so
     * these fixed buffers cover every slot without any per-frame allocation. */
    CrVertex     *trans_sort_verts;
    float        *trans_sort_dist;
    int          *trans_sort_idx;
    int          *trans_sort_tmp;
    float         trans_sort_x, trans_sort_y, trans_sort_z;

    /* W19 occlusion: ALLOCATE-ONCE state for the setupTerrain BFS over the
     * (2R+1)^2 x 16 section grid, sized for the max streaming radius. */
    int            bfs_D;        /* 2*caps.view_radius + 1 */
    int            bfs_nodes;    /* bfs_D * bfs_D * WL_SECTIONS */
    WlSlot       **bfs_col;      /* [bfs_D*bfs_D] kept column slot, NULL if culled */
    unsigned char *bfs_seen;     /* RenderChunk.setFrameIndex                      */
    unsigned char *bfs_reached;  /* node made it into renderInfos                  */
    unsigned char *bfs_setfac;   /* ContainerLocalRenderInformation.setFacing      */
    signed char   *bfs_facing;   /* entry travel direction, -1 == null             */
    int           *bfs_queue;    /* each node enqueued at most once                */
    WlVisGraph     vg;           /* reusable VisGraph scratch                      */

    /* Optional immutable Anvil cold store. Null in ordinary generated worlds,
     * so the hot path pays one predictable branch and no allocation. */
    FILE          *cold_stream;
    WlColdChunk   *cold_chunks;
    uint32_t       cold_count;
    int            cold_dimension;
    unsigned char *cold_blocks;
    unsigned char *cold_sky;
    unsigned char *cold_block_light;
    unsigned char *cold_biomes;
    unsigned char *cold_height;
    WlOverlayChunk *overlay_chunks;
    uint32_t         overlay_count;
    uint32_t         overlay_capacity;
    WlOverlayMutation *overlay_mutations;
    uint32_t           overlay_mutation_count;
    uint32_t           overlay_mutation_capacity;

    long long     block_gen; /* bumps on every set_block(_meta); window-refill memo */
#define WL_BLOCK_MUTATION_CAP 64
    long long     block_mutation_sequence;
    GmWorldBlockMutation block_mutations[WL_BLOCK_MUTATION_CAP];
};

/* v mod D, non-negative. */
static inline int wl_tor(int v, int D) { int m = v % D; if (m < 0) m += D; return m; }

static WlSlot *wl_slot(GmWorld *w, int cx, int cz) {
    int s = wl_tor(cx, w->mesh_D) * w->mesh_D + wl_tor(cz, w->mesh_D);
    return &w->slots[s];
}

/* Mark a chunk dirty ONLY if its slot currently holds it (a chunk not resident is
 * re-meshed fresh when it next becomes visible, so no flag is needed). */
static void wl_mark_dirty(GmWorld *w, int cx, int cz) {
    WlSlot *s = wl_slot(w, cx, cz);
    if (s->valid && s->cx == cx && s->cz == cz) s->dirty = 1;
}

/* debug_caps: worst-case single-chunk mesh maxima, for fixed-pool sizing. */
static int g_caps_on = -1;
static int g_cap_chunk_layer[4] = {0,0,0,0};
static int g_cap_chunk_total = 0;

/* Java Float.floatToIntBits/Float.compare, including signed zero and canonical
 * NaN handling. Mesh distances are finite, but retaining the exact comparison
 * makes this the same total ordering as VertexBuffer.sortVertexData. */
static int32_t wl_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)
            && (bits & UINT32_C(0x007fffff)) != 0)
        bits = UINT32_C(0x7fc00000);
    return (int32_t)bits;
}

static int wl_float_compare(float left, float right) {
    int32_t left_bits, right_bits;
    if (left < right) return -1;
    if (left > right) return 1;
    left_bits = wl_float_bits(left);
    right_bits = wl_float_bits(right);
    if (left_bits == right_bits) return 0;
    return left_bits < right_bits ? -1 : 1;
}

static int wl_sort_before(const float *dist, int left, int right) {
    /* Java compares right with left for descending distance. Arrays.sort on
     * Integer[] is stable, so an exact tie retains the current quad order. */
    return wl_float_compare(dist[right], dist[left]) <= 0;
}

static void wl_sort_translucent_section(GmWorld *w, WlSlot *slot,
                                        const CrCamera *cam, int sec) {
    CrVertex *verts = slot->buf
        + slot->sec_off[CR_LAYER_TRANSLUCENT][sec];
    int nverts = slot->sec_n[CR_LAYER_TRANSLUCENT][sec];
    int nquad = nverts / 6;
    assert(nverts % 6 == 0);
    if (nquad > w->caps->max_verts_per_chunk / 6) abort();

    for (int quad = 0; quad < nquad; ++quad) {
        const CrVertex *v = verts + quad * 6;
        /* push_face expands corners as 0,1,2,0,2,3. These are the same four
         * positions BufferBuilder.getDistanceSq averages. */
        float dx = (v[0].pos.x + v[1].pos.x + v[2].pos.x + v[5].pos.x)
            * 0.25F - cam->pos.x;
        float dy = (v[0].pos.y + v[1].pos.y + v[2].pos.y + v[5].pos.y)
            * 0.25F - cam->pos.y;
        float dz = (v[0].pos.z + v[1].pos.z + v[2].pos.z + v[5].pos.z)
            * 0.25F - cam->pos.z;
        w->trans_sort_dist[quad] = dx * dx + dy * dy + dz * dz;
        w->trans_sort_idx[quad] = quad;
    }

    /* Stable bottom-up merge sort: Java tie behavior, no qsort globals, and no
     * allocation or libc-dependent ordering in the render path. */
    for (int width = 1; width < nquad; width *= 2) {
        for (int begin = 0; begin < nquad; begin += width * 2) {
            int mid = begin + width;
            int end = begin + width * 2;
            int left = begin, right, out = begin;
            if (mid > nquad) mid = nquad;
            if (end > nquad) end = nquad;
            right = mid;
            while (left < mid && right < end) {
                int li = w->trans_sort_idx[left];
                int ri = w->trans_sort_idx[right];
                w->trans_sort_tmp[out++] =
                    wl_sort_before(w->trans_sort_dist, li, ri)
                        ? w->trans_sort_idx[left++]
                        : w->trans_sort_idx[right++];
            }
            while (left < mid)
                w->trans_sort_tmp[out++] = w->trans_sort_idx[left++];
            while (right < end)
                w->trans_sort_tmp[out++] = w->trans_sort_idx[right++];
        }
        memcpy(w->trans_sort_idx, w->trans_sort_tmp,
               (size_t)nquad * sizeof(int));
        if (width > nquad / 2) break;
    }
    for (int out = 0; out < nquad; ++out)
        memcpy(w->trans_sort_verts + out * 6,
               verts + w->trans_sort_idx[out] * 6,
               6 * sizeof(CrVertex));
    if (nverts)
        memcpy(verts, w->trans_sort_verts,
               (size_t)nverts * sizeof(CrVertex));
}

static void wl_sort_translucent(GmWorld *w, WlSlot *slot,
                                const CrCamera *cam) {
    if (!w || !slot || !cam || slot->trans_sorted) return;
    for (int sec = 0; sec < WL_SECTIONS; ++sec)
        wl_sort_translucent_section(w, slot, cam, sec);
    slot->trans_sorted = 1;
}

/* Ensure the toroidal slot for (cx,cz) holds a current packed mesh; (re)build if it
 * holds a different chunk, is empty, or is dirty. Alloc-free: meshes into the shared
 * scratch then packs into the slot's pre-allocated slab. */
static WlSlot *wl_ensure_mesh(GmWorld *w, int cx, int cz) {
    WlSlot *s = wl_slot(w, cx, cz);
    if (s->valid && s->cx == cx && s->cz == cz && !s->dirty) return s;

    worldmc_mesh_chunk_into(w->wmc, cx, cz, &w->scratch, w->scratch_cap);

    int off = 0;
    for (int l = 0; l < 4; ++l) {
        int nl = w->scratch.nverts[l];
        if (off + nl > w->caps->max_verts_per_chunk) {
            fprintf(stderr,
                "[world_live] FATAL: chunk (%d,%d) needs %d verts > slab cap %d "
                "(raise max_verts_per_chunk in magma.conf / caps.h)\n",
                cx, cz, off + nl, w->caps->max_verts_per_chunk);
            assert(0 && "mesh slab cap exceeded");
            abort();
        }
        s->off[l] = off;
        s->n[l]   = nl;
        memset(s->sec_n[l], 0, sizeof(s->sec_n[l]));

        assert(nl % 6 == 0);
        for (int i = 0; i < nl; i += 6)
            s->sec_n[l][w->scratch.quad_sections[l][i / 6]] += 6;

        int cursor[WL_SECTIONS];
        int next = off;
        for (int sec = 0; sec < WL_SECTIONS; ++sec) {
            s->sec_off[l][sec] = next;
            cursor[sec] = next;
            next += s->sec_n[l][sec];
        }
        assert(next == off + nl);

        /* Stable partition: quad data and order within each section are
         * unchanged; only the sixteen section runs are placed together. */
        for (int i = 0; i < nl; i += 6) {
            const CrVertex *quad = w->scratch.verts[l] + i;
            int sec = w->scratch.quad_sections[l][i / 6];
            memcpy(s->buf + cursor[sec], quad, 6 * sizeof(CrVertex));
            cursor[sec] += 6;
        }
        off += nl;
        s->view.verts[l]  = s->buf + s->off[l];
        s->view.nverts[l] = nl;
    }
    s->cx = cx; s->cz = cz; s->valid = 1; s->dirty = 0; s->builds++;
    s->revision++;
    s->trans_sorted = 0;

    /* W19: RenderChunk.rebuildChunk builds the VisGraph in the same pass that
     * builds the geometry. Same here - the flood is paid on (re)mesh only. */
    for (int sec = 0; sec < WL_SECTIONS; ++sec) {
        wl_vis_fill(&w->vg, w->light, cx, cz, sec);
        s->sec_vis[sec] = wl_vis_compute(&w->vg);
    }

    if (g_caps_on < 0) g_caps_on = cr_cfg()->debug_caps;
    if (g_caps_on) {
        int tot = 0;
        for (int l = 0; l < 4; ++l) {
            if (s->n[l] > g_cap_chunk_layer[l]) g_cap_chunk_layer[l] = s->n[l];
            tot += s->n[l];
        }
        if (tot > g_cap_chunk_total) g_cap_chunk_total = tot;
    }
    return s;
}

/* Append `add` verts to a fixed per-layer draw buffer (bounded memcpy, no realloc). */
static void wl_append(GmWorld *w, int l, const CrVertex *src, int add) {
    if (add <= 0) return;
    if (w->out_nverts[l] + add > w->caps->draw_max[l]) {
        fprintf(stderr,
            "[world_live] FATAL: layer %d draw buffer overflow (%d > cap %d); raise "
            "draw_* in magma.conf / caps.h\n", l, w->out_nverts[l] + add,
            w->caps->draw_max[l]);
        assert(0 && "draw buffer cap exceeded");
        abort();
    }
    memcpy(w->out_verts[l] + w->out_nverts[l], src, (size_t)add * sizeof(CrVertex));
    w->out_nverts[l] += add;
}

/* =============================== public API ============================== */

GmWorld *gm_world_create(long long seed) {
    return gm_world_create_type(seed, 0);
}

GmWorld *gm_world_create_type(long long seed, int world_type) {
    const CrCaps *caps = cr_caps();
    GmWorld *w = (GmWorld *)calloc(1, sizeof(GmWorld));
    if (!w) return NULL;
    w->caps = caps;
    w->wmc = worldmc_create_type(seed, world_type);
    if (!w->wmc) { free(w); return NULL; }
    w->light = worldmc_light(w->wmc);
    w->cold_dimension = light_dimension(w->light);

    /* ALLOCATE-ONCE: every buffer sized from caps, all allocated here at init. */
    w->mesh_D     = caps->mesh_D;
    w->mesh_slots = caps->mesh_slots;
    w->slots = (WlSlot *)calloc((size_t)caps->mesh_slots, sizeof(WlSlot));
    if (!w->slots) { gm_world_destroy(w); return NULL; }
    for (int i = 0; i < caps->mesh_slots; ++i) {
        w->slots[i].buf = (CrVertex *)malloc(
            (size_t)caps->max_verts_per_chunk * sizeof(CrVertex));
        if (!w->slots[i].buf) { gm_world_destroy(w); return NULL; }
    }
    for (int l = 0; l < 4; ++l) {
        w->scratch_cap[l]   = caps->max_verts_per_chunk;
        w->scratch.verts[l] = (CrVertex *)malloc(
            (size_t)caps->max_verts_per_chunk * sizeof(CrVertex));
        w->scratch.quad_sections[l] = (unsigned char *)malloc(
            (size_t)caps->max_verts_per_chunk / 6u);
        w->scratch.nverts[l] = 0;
        w->out_verts[l] = (CrVertex *)malloc(
            (size_t)caps->draw_max[l] * sizeof(CrVertex));
        w->out_nverts[l] = 0;
        if (!w->scratch.verts[l] || !w->scratch.quad_sections[l] ||
            !w->out_verts[l]) { gm_world_destroy(w); return NULL; }
    }
    w->trans_sort_verts = (CrVertex *)malloc(
        (size_t)caps->max_verts_per_chunk * sizeof(CrVertex));
    w->trans_sort_dist = (float *)malloc(
        (size_t)(caps->max_verts_per_chunk / 6) * sizeof(float));
    w->trans_sort_idx = (int *)malloc(
        (size_t)(caps->max_verts_per_chunk / 6) * sizeof(int));
    w->trans_sort_tmp = (int *)malloc(
        (size_t)(caps->max_verts_per_chunk / 6) * sizeof(int));
    if (!w->trans_sort_verts || !w->trans_sort_dist
            || !w->trans_sort_idx || !w->trans_sort_tmp) {
        gm_world_destroy(w);
        return NULL;
    }

    /* W19 BFS grid, sized for the max streaming radius (never reallocated). */
    w->bfs_D     = 2 * caps->view_radius + 1;
    w->bfs_nodes = w->bfs_D * w->bfs_D * WL_SECTIONS;
    w->bfs_col     = (WlSlot **)calloc((size_t)(w->bfs_D * w->bfs_D), sizeof(WlSlot *));
    w->bfs_seen    = (unsigned char *)calloc((size_t)w->bfs_nodes, 1);
    w->bfs_reached = (unsigned char *)calloc((size_t)w->bfs_nodes, 1);
    w->bfs_setfac  = (unsigned char *)calloc((size_t)w->bfs_nodes, 1);
    w->bfs_facing  = (signed char *)calloc((size_t)w->bfs_nodes, 1);
    w->bfs_queue   = (int *)malloc((size_t)w->bfs_nodes * sizeof(int));
    if (!w->bfs_col || !w->bfs_seen || !w->bfs_reached || !w->bfs_setfac ||
        !w->bfs_facing || !w->bfs_queue) { gm_world_destroy(w); return NULL; }
    return w;
}

int gm_world_dimension(const GmWorld *w) {
    return w ? light_dimension(w->light) : 0;
}

void gm_world_destroy(GmWorld *w) {
    if (!w) return;
    if (w->slots) {
        for (int i = 0; i < w->mesh_slots; ++i) free(w->slots[i].buf);
        free(w->slots);
    }
    for (int l = 0; l < 4; ++l) {
        free(w->scratch.verts[l]);
        free(w->scratch.quad_sections[l]);
        free(w->out_verts[l]);
    }
    free(w->trans_sort_verts);
    free(w->trans_sort_dist);
    free(w->trans_sort_idx);
    free(w->trans_sort_tmp);
    free(w->bfs_col);
    free(w->bfs_seen);
    free(w->bfs_reached);
    free(w->bfs_setfac);
    free(w->bfs_facing);
    free(w->bfs_queue);
    if (w->cold_stream) fclose(w->cold_stream);
    free(w->cold_chunks);
    free(w->cold_blocks);
    free(w->cold_sky);
    free(w->cold_block_light);
    free(w->cold_biomes);
    free(w->cold_height);
    free(w->overlay_chunks);
    free(w->overlay_mutations);
    if (w->wmc) worldmc_destroy(w->wmc);
    free(w);
}

static int wl_read_u32le(FILE *stream, uint32_t *value) {
    unsigned char raw[4];
    if (fread(raw, 1, sizeof raw, stream) != sizeof raw) return 0;
    *value = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8)
        | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    return 1;
}

static int wl_read_i32le(FILE *stream, int32_t *value) {
    uint32_t raw;
    if (!wl_read_u32le(stream, &raw)) return 0;
    *value = (int32_t)raw;
    return 1;
}

static int wl_read_u64le(FILE *stream, uint64_t *value) {
    unsigned char raw[8];
    if (fread(raw, 1, sizeof raw, stream) != sizeof raw) return 0;
    *value = 0;
    for (int index = 7; index >= 0; --index)
        *value = (*value << 8) | raw[index];
    return 1;
}

static WlColdChunk *wl_cold_find(GmWorld *w, int cx, int cz) {
    uint32_t low = 0, high = w->cold_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        WlColdChunk *entry = &w->cold_chunks[middle];
        if (entry->cx < cx || (entry->cx == cx && entry->cz < cz))
            low = middle + 1;
        else
            high = middle;
    }
    if (low < w->cold_count && w->cold_chunks[low].cx == cx
            && w->cold_chunks[low].cz == cz)
        return &w->cold_chunks[low];
    return NULL;
}

static uint32_t wl_overlay_lower_bound(
        const GmWorld *w, int cx, int cz) {
    uint32_t low = 0, high = w->overlay_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        const WlOverlayChunk *entry = &w->overlay_chunks[middle];
        if (entry->cx < cx || (entry->cx == cx && entry->cz < cz))
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

static WlOverlayChunk *wl_overlay_find(GmWorld *w, int cx, int cz) {
    uint32_t index = wl_overlay_lower_bound(w, cx, cz);
    if (index < w->overlay_count && w->overlay_chunks[index].cx == cx
            && w->overlay_chunks[index].cz == cz)
        return &w->overlay_chunks[index];
    return NULL;
}

static WlOverlayChunk *wl_overlay_ensure(GmWorld *w, int cx, int cz) {
    uint32_t index = wl_overlay_lower_bound(w, cx, cz);
    if (index < w->overlay_count && w->overlay_chunks[index].cx == cx
            && w->overlay_chunks[index].cz == cz)
        return &w->overlay_chunks[index];
    if (w->overlay_count == w->overlay_capacity) {
        uint32_t next_capacity = w->overlay_capacity
            ? w->overlay_capacity * 2u : 16u;
        WlOverlayChunk *next = (WlOverlayChunk *)realloc(
            w->overlay_chunks,
            (size_t)next_capacity * sizeof(WlOverlayChunk));
        if (!next) return NULL;
        w->overlay_chunks = next;
        w->overlay_capacity = next_capacity;
    }
    memmove(&w->overlay_chunks[index + 1], &w->overlay_chunks[index],
            (size_t)(w->overlay_count - index) * sizeof(WlOverlayChunk));
    ++w->overlay_count;
    w->overlay_chunks[index].cx = cx;
    w->overlay_chunks[index].cz = cz;
    w->overlay_chunks[index].mutation_head = -1;
    w->overlay_chunks[index].applied_epoch = 0;
    return &w->overlay_chunks[index];
}

static void wl_overlay_record(
        GmWorld *w, int wx, int wy, int wz, uint16_t state) {
    int cx = wl_floordiv16(wx), cz = wl_floordiv16(wz);
    uint16_t cell = (uint16_t)((wy << 8) | ((wz & 15) << 4) | (wx & 15));
    WlOverlayChunk *chunk = wl_overlay_ensure(w, cx, cz);
    if (!chunk) {
        fprintf(stderr, "[world_live] FATAL: cold overlay allocation failed\n");
        abort();
    }
    for (int index = chunk->mutation_head; index >= 0;
            index = w->overlay_mutations[index].next) {
        if (w->overlay_mutations[index].cell == cell) {
            w->overlay_mutations[index].state = state;
            chunk->applied_epoch = light_chunk_epoch(w->light, cx, cz);
            return;
        }
    }
    if (w->overlay_mutation_count == w->overlay_mutation_capacity) {
        uint32_t next_capacity = w->overlay_mutation_capacity
            ? w->overlay_mutation_capacity * 2u : 64u;
        WlOverlayMutation *next = (WlOverlayMutation *)realloc(
            w->overlay_mutations,
            (size_t)next_capacity * sizeof(WlOverlayMutation));
        if (!next) {
            fprintf(stderr,
                    "[world_live] FATAL: cold mutation allocation failed\n");
            abort();
        }
        w->overlay_mutations = next;
        w->overlay_mutation_capacity = next_capacity;
    }
    WlOverlayMutation *mutation =
        &w->overlay_mutations[w->overlay_mutation_count];
    mutation->cell = cell;
    mutation->state = state;
    mutation->next = chunk->mutation_head;
    chunk->mutation_head = (int)w->overlay_mutation_count++;
    chunk->applied_epoch = light_chunk_epoch(w->light, cx, cz);
}

static int wl_cold_read(GmWorld *w, const WlColdChunk *entry) {
    if (fseek(w->cold_stream, (long)entry->offset, SEEK_SET) != 0
            || fread(w->cold_blocks, 1, WL_COLD_BLOCK_BYTES, w->cold_stream)
                != WL_COLD_BLOCK_BYTES
            || fread(w->cold_sky, 1, WL_COLD_LIGHT_BYTES, w->cold_stream)
                != WL_COLD_LIGHT_BYTES
            || fread(w->cold_block_light, 1, WL_COLD_LIGHT_BYTES,
                     w->cold_stream) != WL_COLD_LIGHT_BYTES
            || fread(w->cold_biomes, 1, WL_COLD_BIOME_BYTES, w->cold_stream)
                != WL_COLD_BIOME_BYTES
            || fread(w->cold_height, 1, WL_COLD_HEIGHT_BYTES, w->cold_stream)
                != WL_COLD_HEIGHT_BYTES)
        return 0;
    return 1;
}

int gm_world_attach_chunk_store(GmWorld *w, const char *path, int dimension) {
    static const unsigned char magic[8] = {
        'N', 'T', 'H', 'C', 'L', 'D', '0', '1'
    };
    unsigned char actual_magic[8];
    uint32_t version, count, payload_bytes;
    int32_t stored_dimension;
    long file_bytes;
    uint64_t expected_offset;
    if (!w || !path || !*path || dimension < -1 || dimension > 1
            || w->cold_stream || light_dimension(w->light) != dimension)
        return 0;
    w->cold_stream = fopen(path, "rb");
    if (!w->cold_stream
            || fread(actual_magic, 1, 8, w->cold_stream) != 8
            || memcmp(actual_magic, magic, 8) != 0
            || !wl_read_u32le(w->cold_stream, &version)
            || !wl_read_i32le(w->cold_stream, &stored_dimension)
            || !wl_read_u32le(w->cold_stream, &count)
            || !wl_read_u32le(w->cold_stream, &payload_bytes)
            || version != 1 || stored_dimension != dimension
            || count > 1000000u
            || payload_bytes != WL_COLD_PAYLOAD_BYTES)
        goto fail;
    w->cold_chunks = count
        ? (WlColdChunk *)calloc(count, sizeof(WlColdChunk)) : NULL;
    w->cold_blocks = (unsigned char *)malloc(WL_COLD_BLOCK_BYTES);
    w->cold_sky = (unsigned char *)malloc(WL_COLD_LIGHT_BYTES);
    w->cold_block_light = (unsigned char *)malloc(WL_COLD_LIGHT_BYTES);
    w->cold_biomes = (unsigned char *)malloc(WL_COLD_BIOME_BYTES);
    w->cold_height = (unsigned char *)malloc(WL_COLD_HEIGHT_BYTES);
    if ((count && !w->cold_chunks) || !w->cold_blocks || !w->cold_sky
            || !w->cold_block_light || !w->cold_biomes || !w->cold_height)
        goto fail;
    expected_offset = 24u + (uint64_t)count * 16u;
    for (uint32_t index = 0; index < count; ++index) {
        WlColdChunk *entry = &w->cold_chunks[index];
        if (!wl_read_i32le(w->cold_stream, &entry->cx)
                || !wl_read_i32le(w->cold_stream, &entry->cz)
                || !wl_read_u64le(w->cold_stream, &entry->offset)
                || entry->offset != expected_offset
                || (index > 0 && (w->cold_chunks[index - 1].cx > entry->cx
                    || (w->cold_chunks[index - 1].cx == entry->cx
                        && w->cold_chunks[index - 1].cz >= entry->cz))))
            goto fail;
        expected_offset += WL_COLD_PAYLOAD_BYTES;
    }
    if (fseek(w->cold_stream, 0, SEEK_END) != 0
            || (file_bytes = ftell(w->cold_stream)) < 0
            || (uint64_t)file_bytes != expected_offset)
        goto fail;
    w->cold_count = count;
    w->cold_dimension = dimension;
    return 1;
fail:
    if (w->cold_stream) fclose(w->cold_stream);
    w->cold_stream = NULL;
    free(w->cold_chunks); w->cold_chunks = NULL;
    free(w->cold_blocks); w->cold_blocks = NULL;
    free(w->cold_sky); w->cold_sky = NULL;
    free(w->cold_block_light); w->cold_block_light = NULL;
    free(w->cold_biomes); w->cold_biomes = NULL;
    free(w->cold_height); w->cold_height = NULL;
    return 0;
}

static int wl_save_chunk_compare(const void *left_raw, const void *right_raw) {
    const WlSaveChunk *left = (const WlSaveChunk *)left_raw;
    const WlSaveChunk *right = (const WlSaveChunk *)right_raw;
    if (left->cx != right->cx) return left->cx < right->cx ? -1 : 1;
    if (left->cz != right->cz) return left->cz < right->cz ? -1 : 1;
    return 0;
}

static int wl_write_u32le(FILE *stream, uint32_t value) {
    unsigned char raw[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24),
    };
    return fwrite(raw, 1, sizeof raw, stream) == sizeof raw;
}

static int wl_write_i32le(FILE *stream, int32_t value) {
    return wl_write_u32le(stream, (uint32_t)value);
}

static int wl_write_u64le(FILE *stream, uint64_t value) {
    unsigned char raw[8];
    for (int index = 0; index < 8; ++index) {
        raw[index] = (unsigned char)value;
        value >>= 8;
    }
    return fwrite(raw, 1, sizeof raw, stream) == sizeof raw;
}

static int wl_write_i32be(FILE *stream, int32_t value) {
    uint32_t raw_value = (uint32_t)value;
    unsigned char raw[4] = {
        (unsigned char)(raw_value >> 24),
        (unsigned char)(raw_value >> 16),
        (unsigned char)(raw_value >> 8),
        (unsigned char)raw_value,
    };
    return fwrite(raw, 1, sizeof raw, stream) == sizeof raw;
}

static int wl_write_base_payload(
        GmWorld *w, const WlColdChunk *entry, FILE *output) {
    return wl_cold_read(w, entry)
        && fwrite(w->cold_blocks, 1, WL_COLD_BLOCK_BYTES, output)
            == WL_COLD_BLOCK_BYTES
        && fwrite(w->cold_sky, 1, WL_COLD_LIGHT_BYTES, output)
            == WL_COLD_LIGHT_BYTES
        && fwrite(w->cold_block_light, 1, WL_COLD_LIGHT_BYTES, output)
            == WL_COLD_LIGHT_BYTES
        && fwrite(w->cold_biomes, 1, WL_COLD_BIOME_BYTES, output)
            == WL_COLD_BIOME_BYTES
        && fwrite(w->cold_height, 1, WL_COLD_HEIGHT_BYTES, output)
            == WL_COLD_HEIGHT_BYTES;
}

static int wl_write_live_payload(
        GmWorld *w, int cx, int cz, FILE *output) {
    gm_world_ensure(w, cx, cz, 1);
    for (int y = 0; y < 256; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                uint16_t state = light_state(
                    w->light, cx * 16 + x, y, cz * 16 + z);
                unsigned char raw[2] = {
                    (unsigned char)state, (unsigned char)(state >> 8),
                };
                if (fwrite(raw, 1, sizeof raw, output) != sizeof raw) return 0;
            }
    for (int y = 0; y < 256; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                unsigned char value = (unsigned char)light_sky(
                    w->light, cx * 16 + x, y, cz * 16 + z);
                if (fwrite(&value, 1, 1, output) != 1) return 0;
            }
    for (int y = 0; y < 256; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                unsigned char value = (unsigned char)light_blk(
                    w->light, cx * 16 + x, y, cz * 16 + z);
                if (fwrite(&value, 1, 1, output) != 1) return 0;
            }
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            int biome = light_biome(w->light, cx * 16 + x, cz * 16 + z);
            unsigned char value = (unsigned char)biome;
            if (biome < 0 || biome > 255
                    || fwrite(&value, 1, 1, output) != 1)
                return 0;
        }
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            int height = light_height(w->light, cx * 16 + x, cz * 16 + z);
            if (height < 0 || height > 256
                    || !wl_write_i32be(output, height))
                return 0;
        }
    return 1;
}

int gm_world_write_chunk_store(GmWorld *w, const char *path) {
    static const unsigned char magic[8] = {
        'N', 'T', 'H', 'C', 'L', 'D', '0', '1'
    };
    WlSaveChunk *chunks = NULL;
    uint32_t count = 0;
    FILE *output = NULL;
    char temporary[PATH_MAX];
    uint64_t offset;
    int ok = 0;
    if (!w || !path || !*path
            || snprintf(temporary, sizeof temporary, "%s.tmp", path)
                >= (int)sizeof temporary)
        return 0;
    chunks = (WlSaveChunk *)calloc(
        (size_t)w->cold_count + w->overlay_count, sizeof(WlSaveChunk));
    if (!chunks) return 0;
    for (uint32_t index = 0; index < w->cold_count; ++index) {
        chunks[count].cx = w->cold_chunks[index].cx;
        chunks[count].cz = w->cold_chunks[index].cz;
        chunks[count].base = &w->cold_chunks[index];
        chunks[count].overlay = wl_overlay_find(
            w, chunks[count].cx, chunks[count].cz);
        ++count;
    }
    for (uint32_t index = 0; index < w->overlay_count; ++index) {
        WlOverlayChunk *overlay = &w->overlay_chunks[index];
        if (wl_cold_find(w, overlay->cx, overlay->cz)) continue;
        chunks[count].cx = overlay->cx;
        chunks[count].cz = overlay->cz;
        chunks[count].overlay = overlay;
        ++count;
    }
    qsort(chunks, count, sizeof(WlSaveChunk), wl_save_chunk_compare);
    (void)remove(temporary);
    output = fopen(temporary, "wb");
    if (!output || fwrite(magic, 1, sizeof magic, output) != sizeof magic
            || !wl_write_u32le(output, 1)
            || !wl_write_i32le(output, w->cold_dimension)
            || !wl_write_u32le(output, count)
            || !wl_write_u32le(output, WL_COLD_PAYLOAD_BYTES))
        goto done;
    offset = 24u + (uint64_t)count * 16u;
    for (uint32_t index = 0; index < count; ++index) {
        if (!wl_write_i32le(output, chunks[index].cx)
                || !wl_write_i32le(output, chunks[index].cz)
                || !wl_write_u64le(output, offset))
            goto done;
        offset += WL_COLD_PAYLOAD_BYTES;
    }
    for (uint32_t index = 0; index < count; ++index) {
        int payload_ok = chunks[index].overlay
            ? wl_write_live_payload(
                w, chunks[index].cx, chunks[index].cz, output)
            : wl_write_base_payload(w, chunks[index].base, output);
        if (!payload_ok) goto done;
    }
    if (fflush(output) != 0 || fclose(output) != 0) {
        output = NULL;
        goto done;
    }
    output = NULL;
    if (rename(temporary, path) != 0) goto done;
    ok = 1;
done:
    if (output) fclose(output);
    if (!ok) (void)remove(temporary);
    free(chunks);
    return ok;
}

void gm_world_ensure(GmWorld *w, int ccx, int ccz, int radius) {
    if (!w) return;
    worldmc_ensure(w->wmc, ccx, ccz, radius);
    if (!w->cold_stream && !w->overlay_count) return;
    int changed = 0;
    for (int cx = ccx - radius; cx <= ccx + radius; ++cx)
        for (int cz = ccz - radius; cz <= ccz + radius; ++cz) {
            WlColdChunk *entry = wl_cold_find(w, cx, cz);
            WlOverlayChunk *overlay = wl_overlay_find(w, cx, cz);
            uint64_t epoch = light_chunk_epoch(w->light, cx, cz);
            int load_base = entry && entry->applied_epoch != epoch;
            int load_overlay = overlay && overlay->applied_epoch != epoch;
            if (!epoch || (!load_base && !load_overlay)) continue;
            if (load_base && !wl_cold_read(w, entry)) {
                fprintf(stderr,
                        "[world_live] FATAL: short cold chunk (%d,%d)\n", cx, cz);
                abort();
            }
            if (load_base) {
                for (int y = 0; y < 256; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x) {
                            size_t index = (size_t)(y << 8 | z << 4 | x);
                            uint16_t state =
                                (uint16_t)w->cold_blocks[index * 2]
                                | (uint16_t)((uint16_t)
                                    w->cold_blocks[index * 2 + 1] << 8);
                            light_load_state(
                                w->light, cx * 16 + x, y, cz * 16 + z, state);
                        }
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        if (!light_debug_set_biome(
                                w->light, cx * 16 + x, cz * 16 + z,
                                w->cold_biomes[(z << 4) | x])) {
                            fprintf(stderr,
                                    "[world_live] FATAL: cold biome chunk vanished\n");
                            abort();
                        }
            }
            if (load_overlay) {
                for (int index = overlay->mutation_head; index >= 0;
                        index = w->overlay_mutations[index].next) {
                    const WlOverlayMutation *mutation =
                        &w->overlay_mutations[index];
                    int y = mutation->cell >> 8;
                    int z = (mutation->cell >> 4) & 15;
                    int x = mutation->cell & 15;
                    light_load_state(
                        w->light, cx * 16 + x, y, cz * 16 + z,
                        mutation->state);
                }
            }
            wl_mark_dirty(w, cx, cz);
            changed = 1;
        }
    if (!changed) return;
    worldmc_ensure(w->wmc, ccx, ccz, radius);
    for (int cx = ccx - radius; cx <= ccx + radius; ++cx)
        for (int cz = ccz - radius; cz <= ccz + radius; ++cz) {
            WlColdChunk *entry = wl_cold_find(w, cx, cz);
            WlOverlayChunk *overlay = wl_overlay_find(w, cx, cz);
            uint64_t epoch = light_chunk_epoch(w->light, cx, cz);
            if (!epoch) continue;
            if (entry && entry->applied_epoch != epoch) {
                if (!overlay) {
                    if (!wl_cold_read(w, entry)) abort();
                    for (int y = 0; y < 256; ++y)
                        for (int z = 0; z < 16; ++z)
                            for (int x = 0; x < 16; ++x) {
                                size_t index =
                                    (size_t)(y << 8 | z << 4 | x);
                                if (!light_load_sky_snapshot(
                                        w->light, cx * 16 + x, y,
                                        cz * 16 + z, w->cold_sky[index])
                                        || !light_load_block_snapshot(
                                            w->light, cx * 16 + x, y,
                                            cz * 16 + z,
                                            w->cold_block_light[index]))
                                    abort();
                            }
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x) {
                            size_t index = (size_t)((z << 4) | x) * 4u;
                            uint32_t raw_height =
                                ((uint32_t)w->cold_height[index] << 24)
                                | ((uint32_t)w->cold_height[index + 1] << 16)
                                | ((uint32_t)w->cold_height[index + 2] << 8)
                                | (uint32_t)w->cold_height[index + 3];
                            int height = raw_height <= 256u
                                ? (int)raw_height : -1;
                            if (!light_load_height_snapshot(
                                    w->light, cx * 16 + x, cz * 16 + z,
                                    height))
                                abort();
                        }
                }
                entry->applied_epoch = epoch;
            }
            if (overlay) overlay->applied_epoch = epoch;
        }
    light_finalize_sky_snapshot(w->light);
    light_finalize_block_snapshot(w->light);
}

int gm_world_block(const GmWorld *w, int wx, int wy, int wz) {
    if (!w) return 0;
    return mc_state_id(light_state(w->light, wx, wy, wz));
}

int gm_world_spawn_ground_id(const GmWorld *w, int wx, int wz) {
    return w ? light_spawn_ground_id(w->light, wx, wz) : -1;
}

int gm_world_meta(const GmWorld *w, int wx, int wy, int wz) {
    if (!w) return 0;
    return mc_state_meta(light_state(w->light, wx, wy, wz));
}

int gm_world_sky_light(const GmWorld *w, int wx, int wy, int wz) {
    return w ? light_sky(w->light, wx, wy, wz) : 0;
}

int gm_world_block_light(const GmWorld *w, int wx, int wy, int wz) {
    return w ? light_blk(w->light, wx, wy, wz) : 0;
}

int gm_world_biome(const GmWorld *w, int wx, int wz) {
    return w ? light_biome(w->light, wx, wz) : -1;
}

int gm_world_height(const GmWorld *w, int wx, int wz) {
    return w ? light_height(w->light, wx, wz) : -1;
}

int gm_world_load_height(GmWorld *w, int wx, int wz, int value) {
    return w ? light_load_height_snapshot(w->light, wx, wz, value) : 0;
}

int gm_world_debug_set_biome(GmWorld *w, int wx, int wz, int biome) {
    return w ? light_debug_set_biome(w->light, wx, wz, biome) : 0;
}

int gm_world_debug_set_block_light(
        GmWorld *w, int wx, int wy, int wz, int value) {
    return w
        ? light_debug_set_block_light(w->light, wx, wy, wz, value)
        : 0;
}

int gm_world_grass_color(const GmWorld *w, int wx, int wy, int wz) {
    return w ? light_grass_color(w->light, wx, wy, wz) : 0;
}

int gm_world_foliage_color(const GmWorld *w, int wx, int wy, int wz) {
    return w ? light_foliage_color(w->light, wx, wy, wz) : 0;
}

void gm_world_set_block(GmWorld *w, int wx, int wy, int wz, int id) {
    gm_world_set_block_meta(w, wx, wy, wz, id, 0);
}

void gm_world_set_block_meta(GmWorld *w, int wx, int wy, int wz, int id, int meta) {
    if (!w) return;
    int cx = wl_floordiv16(wx), cz = wl_floordiv16(wz);
    /* A cold saved chunk must be resident before observing or changing it.
     * Otherwise the post-edit ensure would page the immutable saved value over
     * a change made while the toroidal slot still contained another chunk. */
    gm_world_ensure(w, cx, cz, 0);
    int old_id = gm_world_block(w, wx, wy, wz);
    int old_meta = gm_world_meta(w, wx, wy, wz);

    /* edit the block store, then re-light: light_ensure re-runs sky light for the
     * (idempotent) chunk generation and the global block-light BFS over loaded
     * chunks, which is local in effect (block light radius <= 15 = one chunk). */
    light_set_state(w->light, wx, wy, wz, mc_state(id, meta));
    wl_overlay_record(w, wx, wy, wz, mc_state(id, meta));
    gm_world_ensure(w, cx, cz, 0);
    if ((old_id == 2 || old_id == 3) && id == 0)
        light_recheck_break_surfaces(w->light, wx, wy, wz);

    /* the touched chunk must re-mesh */
    wl_mark_dirty(w, cx, cz);

    /* a face on a chunk border changes the neighbour's culling/faces too */
    int lx = wx & 15, lz = wz & 15;
    if (lx == 0)  wl_mark_dirty(w, cx - 1, cz);
    if (lx == 15) wl_mark_dirty(w, cx + 1, cz);
    if (lz == 0)  wl_mark_dirty(w, cx, cz - 1);
    if (lz == 15) wl_mark_dirty(w, cx, cz + 1);

    if (old_id != id || old_meta != meta) {
        long long sequence = ++w->block_mutation_sequence;
        GmWorldBlockMutation *mutation =
            &w->block_mutations[(sequence - 1) % WL_BLOCK_MUTATION_CAP];
        *mutation = (GmWorldBlockMutation){
            .sequence = sequence,
            .x = wx, .y = wy, .z = wz,
            .old_id = old_id, .old_meta = old_meta,
            .new_id = id, .new_meta = meta,
        };
    }
    w->block_gen++;
}

void gm_world_load_block_meta(GmWorld *w, int wx, int wy, int wz, int id, int meta) {
    if (!w) return;
    int cx = wl_floordiv16(wx), cz = wl_floordiv16(wz);
    light_load_state(w->light, wx, wy, wz, mc_state(id, meta));
    wl_mark_dirty(w, cx, cz);
    int lx = wx & 15, lz = wz & 15;
    if (lx == 0)  wl_mark_dirty(w, cx - 1, cz);
    if (lx == 15) wl_mark_dirty(w, cx + 1, cz);
    if (lz == 0)  wl_mark_dirty(w, cx, cz - 1);
    if (lz == 15) wl_mark_dirty(w, cx, cz + 1);
}

void gm_world_finish_bulk_edit(GmWorld *w, int ccx, int ccz, int radius) {
    if (!w || radius < 0) return;
    gm_world_ensure(w, ccx, ccz, radius);
    ++w->block_gen;
}

int gm_world_load_sky_light(
    GmWorld *w, int wx, int wy, int wz, int value
) {
    return w
        ? light_load_sky_snapshot(w->light, wx, wy, wz, value)
        : 0;
}

void gm_world_finalize_sky_light_snapshot(GmWorld *w) {
    if (w) light_finalize_sky_snapshot(w->light);
}

int gm_world_load_block_light(
    GmWorld *w, int wx, int wy, int wz, int value
) {
    return w
        ? light_load_block_snapshot(w->light, wx, wy, wz, value)
        : 0;
}

void gm_world_finalize_block_light_snapshot(GmWorld *w) {
    if (w) light_finalize_block_snapshot(w->light);
}

long long gm_world_block_gen(const GmWorld *w) {
    /* fold in chunk generation: population (trees/structures) writes into
     * neighbour chunks directly, bypassing set_block_meta */
    return w ? w->block_gen + light_gen_events(w->light) : 0;
}

long long gm_world_block_mutation_sequence(const GmWorld *w) {
    return w ? w->block_mutation_sequence : 0;
}

int gm_world_block_mutation_get(
        const GmWorld *w, long long sequence, GmWorldBlockMutation *out) {
    const GmWorldBlockMutation *mutation;
    if (!w || !out || sequence <= 0
            || sequence > w->block_mutation_sequence
            || w->block_mutation_sequence - sequence
                >= WL_BLOCK_MUTATION_CAP)
        return 0;
    mutation = &w->block_mutations[
        (sequence - 1) % WL_BLOCK_MUTATION_CAP];
    if (mutation->sequence != sequence) return 0;
    *out = *mutation;
    return 1;
}

int gm_world_surface_y(const GmWorld *w, int wx, int wz) {
    if (!w) return 64;
    for (int y = 255; y >= 0; --y)
        if (mc_state_id(light_state(w->light, wx, y, wz)) != 0) return y + 1;
    return 64;   /* ungenerated / all-air column: sensible default */
}

int gm_world_precipitation_y(const GmWorld *w, int wx, int wz) {
    if (!w) return 0;
    for (int y = 255; y > 0; --y) {
        int id = mc_state_id(light_state(w->light, wx, y, wz));
        BptProps p = mc_bpt_props(id);
        if ((p.flags & BF_SOLID) || (p.flags & BF_LIQUID)) return y + 1;
    }
    return -1;
}

int gm_world_precipitation_kind(const GmWorld *w, int wx, int wy, int wz) {
    int biome = gm_world_biome(w, wx, wz);
    switch (biome) {
        case 2: case 8: case 9: case 17: case 35: case 36:
        case 37: case 38: case 39: case 127: case 130:
        case 163: case 164: case 165: case 166: case 167:
            return 0;
        default:
            return gm_world_temperature(w, wx, wy, wz) < 0.15f ? 2 : 1;
    }
}

float gm_world_temperature(
        const GmWorld *w, int wx, int wy, int wz) {
    return w ? light_biome_temperature(w->light, wx, wy, wz) : 0.5f;
}

static int gm_world_is_water(const GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    return id == 8 || id == 9;
}

int gm_world_can_freeze(
        const GmWorld *w, int wx, int wy, int wz,
        int no_water_adjacent) {
    int id;
    if (!w || wy < 0 || wy >= 256
            || gm_world_temperature(w, wx, wy, wz) >= 0.15f
            || gm_world_block_light(w, wx, wy, wz) >= 10)
        return 0;
    id = gm_world_block(w, wx, wy, wz);
    if ((id != 8 && id != 9)
            || gm_world_meta(w, wx, wy, wz) != 0)
        return 0;
    if (!no_water_adjacent)
        return 1;
    return !(gm_world_is_water(w, wx - 1, wy, wz)
        && gm_world_is_water(w, wx + 1, wy, wz)
        && gm_world_is_water(w, wx, wy, wz - 1)
        && gm_world_is_water(w, wx, wy, wz + 1));
}

static int gm_world_snow_can_place(
        const GmWorld *w, int wx, int wy, int wz) {
    int below, meta;
    BptProps props;
    if (!w || wy < 1)
        return 0;
    below = gm_world_block(w, wx, wy - 1, wz);
    meta = gm_world_meta(w, wx, wy - 1, wz);
    if (below == 79 || below == 174)
        return 0;
    if (below == 18 || below == 161)
        return 1;
    if (below == 78 && (meta & 7) == 7)
        return 1;
    props = mc_bpt_props(below);
    return (props.flags & BF_SOLID) && props.light_opacity >= 15;
}

int gm_world_can_snow(
        const GmWorld *w, int wx, int wy, int wz, int check_light) {
    if (!w || gm_world_temperature(w, wx, wy, wz) >= 0.15f)
        return 0;
    if (!check_light)
        return 1;
    return wy >= 0 && wy < 256
        && gm_world_block_light(w, wx, wy, wz) < 10
        && gm_world_block(w, wx, wy, wz) == 0
        && gm_world_snow_can_place(w, wx, wy, wz);
}

int gm_world_is_raining_at(
        const GmWorld *w, const GmWorldClock *clock,
        int wx, int wy, int wz) {
    if (!w || !clock || gm_world_rain_strength(clock, 1.0f) <= 0.2f
            || gm_world_precipitation_y(w, wx, wz) > wy)
        return 0;
    return gm_world_precipitation_kind(w, wx, wy, wz) == 1;
}

CrTexture gm_world_atlas(const GmWorld *w) {
    return worldmc_atlas(w->wmc);
}

void gm_world_fill_window(GmWorld *w, int ccx, int ccz, struct Chunk *win) {
    if (!w || !win) return;
    /* game.h's seam type `struct Chunk` is an opaque forward-decl; the caller hands
     * us a real region of blaze `Chunk` (an anonymous-struct typedef). Re-view it as
     * such so we can index + mc_set into it. */
    Chunk *cwin = (Chunk *)win;

    /* ensure the whole PSV_R-radius region is generated + lit first. */
    gm_world_ensure(w, ccx, ccz, PSV_R);

    for (int dz = -PSV_R; dz <= PSV_R; ++dz) {
        for (int dx = -PSV_R; dx <= PSV_R; ++dx) {
            int i = (dz + PSV_R) * PSV_DIM + (dx + PSV_R);
            int baseX = (ccx + dx) * 16, baseZ = (ccz + dz) * 16;
            Chunk *ch = &cwin[i];
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 256; ++y) {
                        u16 state = light_state(w->light, baseX + lx, y, baseZ + lz);
                        mc_set(ch, lx, y, lz, state);
                    }
        }
    }
}

/* Test-only provenance probe: the renderer key must never be mistaken for the
 * vanilla id returned by gm_world_block(). Not part of game/game.h. */
int gm_world__model_key(const GmWorld *w, int wx, int wy, int wz) {
    if (!w) return 0;
    return light_block(w->light, wx, wy, wz);
}

/* RenderChunk.boundingBox test. ONE definition for both the BFS and the
 * submission loop, so a section the submission pass would keep can never be
 * refused entry by the traversal. */
static inline int wl_section_in_frustum(const float planes[6][4],
                                        int cx, int cz, int sec) {
    double minx = (double)(cx * 16), maxx = (double)(cx * 16 + 16);
    double minz = (double)(cz * 16), maxz = (double)(cz * 16 + 16);
    double miny = (double)sec * WL_SECTION_HEIGHT - WL_SECTION_Y_PAD;
    double maxy = (double)(sec + 1) * WL_SECTION_HEIGHT + WL_SECTION_Y_PAD;
    return cr_aabb_in_frustum(planes, minx, miny, minz, maxx, maxy, maxz);
}

/* RenderGlobal.setupTerrain's "update" block: flood the section graph from the
 * camera's section and mark every node that reaches renderInfos.
 *
 * Grid node id = ((ix * D) + iz) * 16 + sy, ix/iz the 0..2R offsets of the
 * column from (ccx-R, ccz-R). Returns the number of reached nodes; the caller
 * reads w->bfs_reached. `planes` is the same frustum the submission pass uses.
 *
 * Vanilla rules mirrored one for one (RenderGlobal.java:877-957):
 *   - getVisibleFacings(eye) on the camera's own section; a one-facing result
 *     drops the facing opposite the view vector; an empty result renders ONLY
 *     the camera section (flag2, the sealed-pocket / inside-solid case),
 *   - camera section enters the queue with facing == null and setFacing == 0,
 *     so neither the main-direction nor the visibility test applies to it,
 *   - a null camera section (eye above/below the world) seeds every column's
 *     top (y>0) or bottom section that passes the frustum test,
 *   - per edge: skip if the path already travelled the opposite way
 *     (hasDirection), skip unless the current section's SetVisibility connects
 *     the entry face to the exit face, then setFrameIndex (visit-once, side
 *     effect BEFORE the frustum test, exactly as vanilla short-circuits) and
 *     finally the frustum test.
 * mc.renderChunksMany (flag1) is `true` in Minecraft.java:335, so both
 * flag1-guarded tests are always on. playerSpectator is always false here. */
static int wl_bfs_sections(GmWorld *w, const CrCamera *cam, const CrMat4 *view,
                           const float planes[6][4], int ccx, int ccz, int R) {
    const int D = 2 * R + 1;
    const int nodes = D * D * WL_SECTIONS;
    memset(w->bfs_seen,    0, (size_t)nodes);
    memset(w->bfs_reached, 0, (size_t)nodes);
    memset(w->bfs_setfac,  0, (size_t)nodes);
    memset(w->bfs_facing, -1, (size_t)nodes);

    int head = 0, tail = 0;
    const int ccy = (int)floorf(cam->pos.y) >> 4;
    const int in_world = (ccy >= 0 && ccy < WL_SECTIONS);
    const int cam_cell = R * D + R;      /* the camera's own column */

    if (in_world && w->bfs_col[cam_cell]) {
        /* getVisibleFacings(blockpos1): a FRESH VisGraph flood from the eye cell
         * (no computeVisibility shortcuts), inside the camera's own section. */
        wl_vis_fill(&w->vg, w->light, ccx, ccz, ccy);
        int ex = (int)floorf(cam->pos.x) & 15;
        int ey = (int)floorf(cam->pos.y) & 15;
        int ez = (int)floorf(cam->pos.z) & 15;
        int set1 = wl_vis_flood(&w->vg, ex | (ez << 4) | (ey << 8));

        int n1 = 0;
        for (int f = 0; f < 6; ++f) n1 += (set1 >> f) & 1;
        if (n1 == 1) {
            /* view vector == -(row 2 of the view matrix), i.e. the same forward
             * axis game/test_view.c reads out of cr_camera_view. */
            int vf = wl_facing_from_vector(-view->m[2], -view->m[6], -view->m[10]);
            set1 &= ~(1 << WL_F_OPP(vf));
        }
        int start = cam_cell * WL_SECTIONS + ccy;
        w->bfs_seen[start] = 1;
        if (set1 == 0) {
            /* flag2: sealed pocket / inside solid. renderInfos == the camera
             * section alone, with no frustum test and no traversal. */
            w->bfs_reached[start] = 1;
            w->bfs_queue[0] = start;
            return 1;
        }
        w->bfs_queue[tail++] = start;
    } else {
        /* renderchunk == null: seed the y=248 (or y=8) layer over the radius. */
        int sy = (cam->pos.y > 0.0f) ? 15 : 0;
        for (int ix = 0; ix < D; ++ix)
            for (int iz = 0; iz < D; ++iz) {
                int cell = ix * D + iz;
                if (!w->bfs_col[cell]) continue;
                int n = cell * WL_SECTIONS + sy;
                w->bfs_seen[n] = 1;
                if (!wl_section_in_frustum(planes, ccx - R + ix, ccz - R + iz, sy))
                    continue;
                w->bfs_queue[tail++] = n;
            }
    }

    int reached = 0;
    while (head < tail) {
        int ni = w->bfs_queue[head++];
        w->bfs_reached[ni] = 1;
        reached++;

        int sy   = ni & (WL_SECTIONS - 1);
        int cell = ni / WL_SECTIONS;
        int ix   = cell / D, iz = cell % D;
        const WlSlot *c = w->bfs_col[cell];
        /* A section whose column was frustum-culled is never enqueued, so `c`
         * is always resident; the null guard keeps the traversal permissive. */
        uint64_t vis = c ? c->sec_vis[sy] : WL_VIS_ALL;
        int f_in     = w->bfs_facing[ni];
        unsigned sf  = w->bfs_setfac[ni];

        for (int f = 0; f < 6; ++f) {
            if (sf & (1u << WL_F_OPP(f))) continue;            /* hasDirection  */
            if (f_in >= 0 &&
                !((vis >> (WL_F_OPP(f_in) + f * 6)) & 1)) continue; /* isVisible */

            int nix = ix, niz = iz, nsy = sy;
            switch (f) {
                case WL_F_DOWN:  nsy--; break;
                case WL_F_UP:    nsy++; break;
                case WL_F_NORTH: niz--; break;
                case WL_F_SOUTH: niz++; break;
                case WL_F_WEST:  nix--; break;
                default:         nix++; break;   /* WL_F_EAST */
            }
            /* getRenderChunkOffset: y in [0,256) and |dx|,|dz| <= R chunks. */
            if (nsy < 0 || nsy >= WL_SECTIONS) continue;
            if (nix < 0 || nix >= D || niz < 0 || niz >= D) continue;

            int nj = (nix * D + niz) * WL_SECTIONS + nsy;
            if (w->bfs_seen[nj]) continue;      /* setFrameIndex, side effect... */
            w->bfs_seen[nj] = 1;
            if (!wl_section_in_frustum(planes, ccx - R + nix, ccz - R + niz, nsy))
                continue;                       /* ...then isBoundingBoxInFrustum */

            w->bfs_facing[nj] = (signed char)f;
            w->bfs_setfac[nj] = (unsigned char)(sf | (1u << f));
            w->bfs_queue[tail++] = nj;
        }
    }
    return reached;
}

/* RenderGlobal.renderBlockLayer(TRANSLUCENT): once the eye moved more than one
 * block, enqueue transparency rebuilds for the first fifteen non-empty
 * renderInfos. The native renderer is synchronous, so apply those same sorts
 * now. Each section is one vanilla RenderChunk. */
static void wl_resort_translucent_visible(GmWorld *w, const CrCamera *cam,
                                          int D, int reached, int occl_on) {
    double dx = (double)cam->pos.x - (double)w->trans_sort_x;
    double dy = (double)cam->pos.y - (double)w->trans_sort_y;
    double dz = (double)cam->pos.z - (double)w->trans_sort_z;
    int moved = dx * dx + dy * dy + dz * dz > 1.0;
    if (!moved) return;
    w->trans_sort_x = cam->pos.x;
    w->trans_sort_y = cam->pos.y;
    w->trans_sort_z = cam->pos.z;

    int sorted = 0;
    if (occl_on) {
        for (int i = 0; i < reached && sorted < 15; ++i) {
            int node = w->bfs_queue[i];
            int sec = node & (WL_SECTIONS - 1);
            int cell = node / WL_SECTIONS;
            WlSlot *slot = w->bfs_col[cell];
            if (!slot || slot->sec_n[CR_LAYER_TRANSLUCENT][sec] == 0)
                continue;
            wl_sort_translucent_section(w, slot, cam, sec);
            slot->revision++;
            ++sorted;
        }
    } else {
        for (int cell = 0; cell < D * D && sorted < 15; ++cell) {
            WlSlot *slot = w->bfs_col[cell];
            if (!slot) continue;
            for (int sec = 0; sec < WL_SECTIONS && sorted < 15; ++sec) {
                if (slot->sec_n[CR_LAYER_TRANSLUCENT][sec] == 0) continue;
                wl_sort_translucent_section(w, slot, cam, sec);
                slot->revision++;
                ++sorted;
            }
        }
    }
}

/* Emit sink for the shared view walk. NULL `runs` selects the host concat into
 * w->out_verts; non-NULL records the equivalent slab runs instead. */
typedef struct {
    GmChunkDraw *chunks;
    int          max_chunks;
    int          nch;
    GmMeshRun   *runs;        /* four layer-major banks of runs_stride entries */
    int          runs_stride;
    int          nruns[4];
    int          nverts[4];
    int          overflow;
} WlEmit;

/* Record one contiguous slab range for layer `l`. Sections placed back-to-back
 * in the slab (kept neighbours, and any zero-vert section between them) fold
 * into the run already open, so a layer costs at most one entry per maximal
 * kept run - never more than WL_SECTIONS per chunk. */
static void wl_emit_run(WlEmit *em, int l, int slot, int off, int n) {
    if (n <= 0) return;
    GmMeshRun *bank = em->runs + (size_t)l * (size_t)em->runs_stride;
    int i = em->nruns[l];
    if (i > 0 && bank[i - 1].slot == slot &&
        bank[i - 1].off + bank[i - 1].n == off) {
        bank[i - 1].n += n;
    } else if (i < em->runs_stride) {
        bank[i].slot = slot;
        bank[i].off  = off;
        bank[i].n    = n;
        em->nruns[l] = i + 1;
    } else {
        em->overflow = 1;
        return;
    }
    em->nverts[l] += n;
}

/* THE view walk, shared by gm_world_mesh_view (host concat) and
 * gm_world_mesh_runs (device gather) so the two can never drift in chunk
 * order, section order, or in-section order. */
static void wl_view_walk(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                         WlEmit *em, int nverts_out[4],
                         int *out_kept, int *out_culled) {
    /* View radius: default SCN_VIEW_RADIUS (=12, keeps the byte-identical regression
     * lock when view_radius_active is 0/unset). view_radius_active lowers it at
     * runtime for smooth interactive FPS (fewer chunks meshed/rasterized); clamped
     * to [1, SCN_VIEW_RADIUS]. Distinct from the pool-cap key view_radius. */
    int R = SCN_VIEW_RADIUS;
    { int r = cr_cfg()->view_radius_active;
      if (r >= 1 && r <= SCN_VIEW_RADIUS) R = r; }
    /* ALLOCATE-ONCE clamp: pools are sized for caps.view_radius (the DECISION max=8),
     * so the streaming radius can never exceed it. Keeps every toroidal region within
     * its pool span (no in-pass slot collisions) and every draw buffer within cap. */
    if (R > w->caps->view_radius) R = w->caps->view_radius;
    const int ccx = wl_floordiv16((int)floorf(cam->pos.x));
    const int ccz = wl_floordiv16((int)floorf(cam->pos.z));

    /* generate + light radius R plus a 1-chunk apron for correct edge meshing. */
    gm_world_ensure(w, ccx, ccz, R + 1);

    /* 6 frustum planes from the SAME matrices cr_transform uses (proj aspect from the
     * framebuffer dims, view from the camera pose): bit-faithful to chunk_scene.h. */
    CrMat4 proj = cr_perspective(cam->fov_deg, (float)fb_w / (float)fb_h,
                                 cam->znear, cam->zfar);
    CrMat4 view = cr_camera_view(cam);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);

    const int cull_off = cr_cfg()->no_cull;
    /* no_occlusion bypasses ONLY the W19 visibility-graph BFS; frustum
     * column + section culling stay on. no_cull remains the full bypass. */
    const int occl_off = cull_off || cr_cfg()->no_occlusion;
    /* debug_verts: prove far-chunk decoration reaches the mesh - tally the
     * leaf (CUTOUT_MIPPED=1) + solid (0, includes logs) verts contributed by chunks
     * OUTSIDE the origin 2x2 vs inside it. Before view-distance populate the far
     * tally was 0 (only chunks 0..1 were decorated). */
    const int dbg_verts = cr_cfg()->debug_verts;
    long dbg_far_leaf = 0, dbg_near_leaf = 0, dbg_far_solid = 0;
    int  dbg_far_chunks_with_leaf = 0;

    /* reset the world-owned output buffers (reuse allocations). */
    for (int l = 0; l < 4; ++l) w->out_nverts[l] = 0;
    int n_kept = 0, n_culled = 0;
    int sections_kept = 0, sections_culled = 0;
    int sections_reached = 0, sections_occluded = 0;

    /* ---- pass 1: coarse column cull + mesh + chunk-draw record, in the frozen
     * cx-outer/cz-inner order. Only kept columns are meshed, so only kept
     * columns need a VisGraph - and the BFS only ever enters a column that
     * passed here. The em->chunks list is recorded EXACTLY as before, occluded
     * columns included: it is the device-residency list, and a run may only
     * name a slot the sink was told to upload. ---- */
    const int D = 2 * R + 1;
    memset(w->bfs_col, 0, (size_t)(D * D) * sizeof(WlSlot *));
    for (int cx = ccx - R; cx <= ccx + R; ++cx) {
        for (int cz = ccz - R; cz <= ccz + R; ++cz) {
            double minx = (double)(cx * 16), maxx = (double)(cx * 16 + 16);
            double minz = (double)(cz * 16), maxz = (double)(cz * 16 + 16);
            int inside = cull_off ||
                cr_aabb_in_frustum(planes, minx, WL_AABB_Y_MIN, minz,
                                   maxx, WL_AABB_Y_MAX, maxz);
            if (!inside) { n_culled++; continue; }
            n_kept++;

            WlSlot *c = wl_ensure_mesh(w, cx, cz);
            if (!c) continue;
            wl_sort_translucent(w, c, cam);
            w->bfs_col[(cx - ccx + R) * D + (cz - ccz + R)] = c;
            if (em) {
                if (em->nch >= em->max_chunks) { em->overflow = 1; }
                else {
                    GmChunkDraw *d = &em->chunks[em->nch++];
                    d->slot   = (int)(c - w->slots);
                    d->builds = c->revision;
                    d->slab   = c->buf;
                    for (int l = 0; l < 4; ++l) {
                        d->off[l] = c->off[l];
                        d->n[l]   = c->n[l];
                    }
                }
            }
        }
    }

    /* ---- pass 2: the setupTerrain flood. Falls back to "submit everything the
     * frustum kept" whenever the camera's own column is not resident. ---- */
    int occl_on = !occl_off && w->bfs_col[R * D + R] != NULL;
    if (occl_on)
        sections_reached = wl_bfs_sections(w, cam, &view, planes, ccx, ccz, R);
    wl_resort_translucent_visible(
        w, cam, D, sections_reached, occl_on);
    if (em)
        for (int i = 0; i < em->nch; ++i)
            em->chunks[i].builds = w->slots[em->chunks[i].slot].revision;

    /* ---- pass 3: submission. Identical iteration order and identical per-layer
     * run concatenation as before; occlusion only REMOVES sections. Both emit
     * sinks read the SAME reached set from the SAME loop, so the host concat and
     * the device gather cannot disagree about which sections went out. ---- */
    for (int cx = ccx - R; cx <= ccx + R; ++cx) {
        for (int cz = ccz - R; cz <= ccz + R; ++cz) {
            int cell = (cx - ccx + R) * D + (cz - ccz + R);
            WlSlot *c = w->bfs_col[cell];
            if (!c) continue;
            int slot = (int)(c - w->slots);

            int added[4] = {0, 0, 0, 0};
            for (int sec = 0; sec < WL_SECTIONS; ++sec) {
                int nonempty = 0;
                for (int l = 0; l < CR_LAYER_TRANSLUCENT; ++l)
                    nonempty |= c->sec_n[l][sec] != 0;
                if (!nonempty) continue;

                int section_inside =
                    cull_off || wl_section_in_frustum(planes, cx, cz, sec);
                if (!section_inside) {
                    sections_culled++;
                    continue;
                }
                /* Every quad is tagged by the source BlockPos while meshing,
                 * exactly matching Java's one RenderChunk per 16-high section.
                 * In particular, the top face of y=15 remains owned by section
                 * zero even though every one of its vertices lies at y=16. */
                int sec_reached = w->bfs_reached[cell * WL_SECTIONS + sec];
                if (occl_on && !sec_reached) {
                    sections_occluded++;
                    continue;
                }
                sections_kept++;
                for (int l = 0; l < CR_LAYER_TRANSLUCENT; ++l) {
                    int ns = c->sec_n[l][sec];
                    if (em) wl_emit_run(em, l, slot, c->sec_off[l][sec], ns);
                    else    wl_append(w, l, c->buf + c->sec_off[l][sec], ns);
                    added[l] += ns;
                }
            }
            if (dbg_verts) {
                int in2x2 = (cx >= 0 && cx < 2 && cz >= 0 && cz < 2);
                if (in2x2) dbg_near_leaf += added[1];
                else {
                    dbg_far_leaf  += added[1];
                    dbg_far_solid += added[0];
                    if (added[1] > 0) dbg_far_chunks_with_leaf++;
                }
            }
        }
    }

    /* Java submits TRANSLUCENT RenderChunks in reverse renderInfos order. A
     * RenderChunk is one 16-high section, whose quads were independently
     * sorted above. This preserves painter order across both sections and
     * columns while using the same slab runs for host and device paths. */
    if (occl_on) {
        for (int i = sections_reached - 1; i >= 0; --i) {
            int node = w->bfs_queue[i];
            int sec = node & (WL_SECTIONS - 1);
            int cell = node / WL_SECTIONS;
            WlSlot *c = w->bfs_col[cell];
            int n, slot;
            if (!c) continue;
            n = c->sec_n[CR_LAYER_TRANSLUCENT][sec];
            slot = (int)(c - w->slots);
            if (em)
                wl_emit_run(em, CR_LAYER_TRANSLUCENT, slot,
                            c->sec_off[CR_LAYER_TRANSLUCENT][sec], n);
            else
                wl_append(w, CR_LAYER_TRANSLUCENT,
                          c->buf + c->sec_off[CR_LAYER_TRANSLUCENT][sec], n);
        }
    } else {
        /* Diagnostic no-occlusion mode has no renderInfos BFS. Its natural
         * forward list is cell-major/section-major, so reverse that list. */
        for (int cell = D * D - 1; cell >= 0; --cell) {
            WlSlot *c = w->bfs_col[cell];
            int ix = cell / D, iz = cell % D;
            int cx = ccx - R + ix, cz = ccz - R + iz;
            if (!c) continue;
            for (int sec = WL_SECTIONS - 1; sec >= 0; --sec) {
                int n = c->sec_n[CR_LAYER_TRANSLUCENT][sec];
                int slot = (int)(c - w->slots);
                if (!n || (!cull_off
                        && !wl_section_in_frustum(planes, cx, cz, sec)))
                    continue;
                if (em)
                    wl_emit_run(em, CR_LAYER_TRANSLUCENT, slot,
                                c->sec_off[CR_LAYER_TRANSLUCENT][sec], n);
                else
                    wl_append(w, CR_LAYER_TRANSLUCENT,
                              c->buf + c->sec_off[CR_LAYER_TRANSLUCENT][sec], n);
            }
        }
    }
    if (dbg_verts)
        fprintf(stderr,
            "[verts] kept=%d  leaf(CUTOUT_MIPPED) verts: near-2x2=%ld far=%ld  "
            "far solid verts=%ld  far chunks with leaves=%d  owr_run builds=%ld\n",
            n_kept, dbg_near_leaf, dbg_far_leaf, dbg_far_solid, dbg_far_chunks_with_leaf,
            popmc_window_builds());

    for (int l = 0; l < 4; ++l)
        nverts_out[l] = em ? em->nverts[l] : w->out_nverts[l];

    if (g_caps_on < 0) g_caps_on = cr_cfg()->debug_caps;
    if (g_caps_on) {
        int valid = 0;
        for (int i = 0; i < w->mesh_slots; ++i) if (w->slots[i].valid) valid++;
        fprintf(stderr,
            "[caps] R=%d kept=%d culled=%d mesh_slots=%d/%d light_chunks=%d owr_windows=%ld "
            "sections_kept=%d sections_culled=%d "
            "sections_reached=%d sections_occluded=%d "
            "drawverts[S/CM/C/T]=%d/%d/%d/%d "
            "chunk_max[S/CM/C/T]=%d/%d/%d/%d chunk_max_total=%d\n",
            R, n_kept, n_culled, valid, w->mesh_slots, light_loaded_chunks(w->light),
            popmc_window_builds(), sections_kept, sections_culled,
            sections_reached, sections_occluded,
            nverts_out[0], nverts_out[1], nverts_out[2], nverts_out[3],
            g_cap_chunk_layer[0], g_cap_chunk_layer[1], g_cap_chunk_layer[2],
            g_cap_chunk_layer[3], g_cap_chunk_total);
        if (em)
            fprintf(stderr, "[caps] gather runs[S/CM/C/T]=%d/%d/%d/%d cap=%d\n",
                    em->nruns[0], em->nruns[1], em->nruns[2], em->nruns[3],
                    em->runs_stride);
    }

    if (out_kept)   *out_kept   = n_kept;
    if (out_culled) *out_culled = n_culled;
}

void gm_world_mesh_view(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                        GmMeshView *out) {
    if (!w || !cam || !out) return;
    wl_view_walk(w, cam, fb_w, fb_h, NULL, out->nverts,
                 &out->n_kept, &out->n_culled);
    for (int l = 0; l < 4; ++l) out->verts[l] = w->out_verts[l];
}

int gm_world_mesh_runs(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                       GmChunkDraw *chunks, int max_chunks,
                       GmMeshRun *runs, int runs_stride,
                       int nruns[4], int nverts[4],
                       int *n_kept, int *n_culled) {
    if (!w || !cam || !chunks || !runs || runs_stride <= 0) return -1;
    WlEmit em;
    memset(&em, 0, sizeof em);
    em.chunks = chunks;
    em.max_chunks = max_chunks;
    em.runs = runs;
    em.runs_stride = runs_stride;
    wl_view_walk(w, cam, fb_w, fb_h, &em, nverts, n_kept, n_culled);
    for (int l = 0; l < 4; ++l) nruns[l] = em.nruns[l];
    return em.overflow ? -1 : em.nch;
}

/* ---- TEST HOOK (not in the header): force-mesh (build if dirty/new) chunk (cx,cz)
 * and return its cached per-layer mesh + the cumulative build count for that slot.
 * Used by game/test_world_live.c to assert the dirty-cache behaviour per chunk. */
const CrChunkMeshMC *gm_world__cached_mesh(GmWorld *w, int cx, int cz, int *builds) {
    if (!w) return NULL;
    WlSlot *c = wl_ensure_mesh(w, cx, cz);
    if (!c) return NULL;
    if (builds) *builds = c->builds;
    return &c->view;
}

/* Test hook: each vanilla RenderChunk is one 16-high section and must be
 * independently far-to-near after a camera sort. */
int gm_world__translucent_sorted(GmWorld *w, int cx, int cz,
                                 const CrCamera *cam) {
    WlSlot *slot;
    if (!w || !cam) return 0;
    slot = wl_ensure_mesh(w, cx, cz);
    if (!slot) return 0;
    wl_sort_translucent(w, slot, cam);
    for (int sec = 0; sec < WL_SECTIONS; ++sec) {
        CrVertex *v = slot->buf + slot->sec_off[CR_LAYER_TRANSLUCENT][sec];
        int n = slot->sec_n[CR_LAYER_TRANSLUCENT][sec];
        float previous = INFINITY;
        for (int i = 0; i < n; i += 6) {
            float dx = (v[i].pos.x + v[i + 1].pos.x + v[i + 2].pos.x
                        + v[i + 5].pos.x) * 0.25F - cam->pos.x;
            float dy = (v[i].pos.y + v[i + 1].pos.y + v[i + 2].pos.y
                        + v[i + 5].pos.y) * 0.25F - cam->pos.y;
            float dz = (v[i].pos.z + v[i + 1].pos.z + v[i + 2].pos.z
                        + v[i + 5].pos.z) * 0.25F - cam->pos.z;
            float distance = dx * dx + dy * dy + dz * dz;
            if (distance > previous) return 0;
            previous = distance;
        }
    }
    return 1;
}

/* Test hook: source-RenderChunk ownership after the stable section partition. */
int gm_world__section_nverts(GmWorld *w, int cx, int cz, int layer, int sec) {
    if (!w || layer < 0 || layer >= 4 || sec < 0 || sec >= WL_SECTIONS)
        return -1;
    WlSlot *slot = wl_ensure_mesh(w, cx, cz);
    return slot ? slot->sec_n[layer][sec] : -1;
}

/* ---- world clock (weather + worldTime) ------------------------------------
 * Thin wrapper around the verified world_weather.h kernel so the live game
 * advances sim time every tick. WwState is larger (includes JavaRandom); we
 * keep it as a static per-clock blob keyed by seed on init. */
typedef struct {
    int     inited;
    WwState ww;
} GmClockPriv;

static GmClockPriv g_clock;

void gm_world_clock_init(GmWorldClock *c, i64 seed) {
    if (!c) return;
    ww_init(&g_clock.ww, seed);
    /* A fresh WorldInfo leaves both weather timers and flags at Java default
     * zero. The fixed raining=1/timer=50 state belongs only to the standalone
     * world_weather battery, where it forces every transition in 256 ticks. */
    g_clock.ww.rainTime = 0;
    g_clock.ww.thunderTime = 0;
    g_clock.ww.raining = 0;
    g_clock.ww.thundering = 0;
    g_clock.ww.prevRainingStrength = 0.0f;
    g_clock.ww.rainingStrength = 0.0f;
    g_clock.ww.prevThunderingStrength = 0.0f;
    g_clock.ww.thunderingStrength = 0.0f;
    g_clock.inited = 1;
    c->total_time   = g_clock.ww.totalTime;
    c->world_time   = g_clock.ww.worldTime;
    c->rain_time    = g_clock.ww.rainTime;
    c->thunder_time = g_clock.ww.thunderTime;
    c->raining      = g_clock.ww.raining;
    c->thundering   = g_clock.ww.thundering;
    c->prev_rain_strength = g_clock.ww.prevRainingStrength;
    c->rain_strength = g_clock.ww.rainingStrength;
    c->prev_thunder_strength = g_clock.ww.prevThunderingStrength;
    c->thunder_strength = g_clock.ww.thunderingStrength;
    c->clean_weather_time = 0;
    c->weather_cycle = 1;
    c->freeze_daylight = 0;
    c->freeze_weather = 0;
}

void gm_world_tick(GmWorldClock *c) {
    if (!c) return;
    if (!g_clock.inited) gm_world_clock_init(c, 0);
    i64 wt_prev = g_clock.ww.worldTime;
    /* doWeatherCycle=false freezes timers and flags, but World.updateWeather
     * still eases the independent client-facing rain/thunder strengths. */
    if (c->weather_cycle && !c->freeze_weather) {
        if (c->clean_weather_time > 0) {
            --c->clean_weather_time;
            g_clock.ww.thunderTime = g_clock.ww.thundering ? 1 : 2;
            g_clock.ww.rainTime = g_clock.ww.raining ? 1 : 2;
        }
        ww_update_weather(&g_clock.ww);
    } else {
        g_clock.ww.prevThunderingStrength =
            g_clock.ww.thunderingStrength;
        g_clock.ww.thunderingStrength = (float)(
            (double)g_clock.ww.thunderingStrength
            + (g_clock.ww.thundering ? 0.01 : -0.01));
        if (g_clock.ww.thunderingStrength < 0.0f)
            g_clock.ww.thunderingStrength = 0.0f;
        if (g_clock.ww.thunderingStrength > 1.0f)
            g_clock.ww.thunderingStrength = 1.0f;
        g_clock.ww.prevRainingStrength = g_clock.ww.rainingStrength;
        g_clock.ww.rainingStrength = (float)(
            (double)g_clock.ww.rainingStrength
            + (g_clock.ww.raining ? 0.01 : -0.01));
        if (g_clock.ww.rainingStrength < 0.0f)
            g_clock.ww.rainingStrength = 0.0f;
        if (g_clock.ww.rainingStrength > 1.0f)
            g_clock.ww.rainingStrength = 1.0f;
    }
    ++g_clock.ww.totalTime;
    ++g_clock.ww.worldTime;
    if (c->freeze_daylight) g_clock.ww.worldTime = wt_prev;
    c->total_time   = g_clock.ww.totalTime;
    c->world_time   = g_clock.ww.worldTime;
    c->rain_time    = g_clock.ww.rainTime;
    c->thunder_time = g_clock.ww.thunderTime;
    c->raining      = g_clock.ww.raining;
    c->thundering   = g_clock.ww.thundering;
    c->prev_rain_strength = g_clock.ww.prevRainingStrength;
    c->rain_strength = g_clock.ww.rainingStrength;
    c->prev_thunder_strength = g_clock.ww.prevThunderingStrength;
    c->thunder_strength = g_clock.ww.thunderingStrength;
}

void gm_world_tick_clear(GmWorldClock *c) {
    if (!c) return;
    ++c->total_time;
    if (!c->freeze_daylight) ++c->world_time;
    if (!c->freeze_weather) {
        c->rain_time = 0;
        c->thunder_time = 0;
        c->raining = 0;
        c->thundering = 0;
        c->prev_rain_strength = c->rain_strength = 0.0f;
        c->prev_thunder_strength = c->thunder_strength = 0.0f;
    }
}

void gm_world_clock_set_time(GmWorldClock *c, long long world_time) {
    if (!c) return;
    if (!g_clock.inited) gm_world_clock_init(c, 0);
    c->world_time = world_time;
    g_clock.ww.worldTime = world_time;
}

void gm_world_clock_set_total_time(GmWorldClock *c, long long total_time) {
    if (!c || total_time < 0) return;
    if (!g_clock.inited) gm_world_clock_init(c, 0);
    c->total_time = total_time;
    g_clock.ww.totalTime = total_time;
}

void gm_world_clock_set_weather(GmWorldClock *c, int raining, int thundering,
                                int rain_time, int thunder_time) {
    float rain_strength = raining ? 1.0f : 0.0f;
    float thunder_strength = raining && thundering ? 1.0f : 0.0f;
    gm_world_clock_set_weather_full(
        c, raining, thundering, rain_time, thunder_time, 0, 1,
        rain_strength, rain_strength, thunder_strength, thunder_strength);
}

void gm_world_clock_set_weather_full(
        GmWorldClock *c, int raining, int thundering,
        int rain_time, int thunder_time, int clean_weather_time,
        int weather_cycle, float prev_rain_strength, float rain_strength,
        float prev_thunder_strength, float thunder_strength) {
    if (!c) return;
    if (!g_clock.inited) gm_world_clock_init(c, 0);
    c->raining = raining ? 1 : 0;
    c->thundering = thundering ? 1 : 0;
    c->rain_time = rain_time;
    c->thunder_time = thunder_time;
    g_clock.ww.raining = c->raining;
    g_clock.ww.thundering = c->thundering;
    g_clock.ww.rainTime = c->rain_time;
    g_clock.ww.thunderTime = c->thunder_time;
    c->clean_weather_time = clean_weather_time;
    c->weather_cycle = weather_cycle ? 1 : 0;
    c->prev_rain_strength = prev_rain_strength;
    c->rain_strength = rain_strength;
    c->prev_thunder_strength = prev_thunder_strength;
    c->thunder_strength = thunder_strength;
    g_clock.ww.prevRainingStrength = c->prev_rain_strength;
    g_clock.ww.rainingStrength = c->rain_strength;
    g_clock.ww.prevThunderingStrength = c->prev_thunder_strength;
    g_clock.ww.thunderingStrength = c->thunder_strength;
    g_clock.ww.worldTime = c->world_time;
    g_clock.ww.totalTime = c->total_time;
}

void gm_world_clock_set_random_seed48(
        GmWorldClock *c, unsigned long long seed48) {
    (void)c;
    if (!g_clock.inited) return;
    jrand_set_seed48(&g_clock.ww.rand, (u64)seed48);
}

unsigned long long gm_world_clock_random_seed48(
        const GmWorldClock *c) {
    (void)c;
    return g_clock.inited
        ? (unsigned long long)g_clock.ww.rand.seed : 0ULL;
}

float gm_world_rain_strength(const GmWorldClock *c, float partial_ticks) {
    if (!c) return 0.0f;
    return c->prev_rain_strength
        + (c->rain_strength - c->prev_rain_strength) * partial_ticks;
}

float gm_world_thunder_strength(const GmWorldClock *c, float partial_ticks) {
    if (!c) return 0.0f;
    return (c->prev_thunder_strength
        + (c->thunder_strength - c->prev_thunder_strength) * partial_ticks)
        * gm_world_rain_strength(c, partial_ticks);
}
