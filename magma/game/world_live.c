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
 *     into world-owned buffers. Translucent verts keep their original full-column
 *     order because blending is order-sensitive.
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
    const BmBlock *m = bm_block(light_block(L, wx, wy, wz));
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
    CrVertex     *buf;      /* packed slab: layer0|layer1|layer2|layer3 */
    int           off[4], n[4];
    int           sec_off[4][WL_SECTIONS];
    int           sec_n[4][WL_SECTIONS];
    /* W19: per-section SetVisibility (36-bit face-pair set), recomputed with the
     * geometry in wl_ensure_mesh and never touched per frame. */
    uint64_t      sec_vis[WL_SECTIONS];
    CrChunkMeshMC view;     /* verts[l]=buf+off[l], nverts[l]=n[l]     */
} WlSlot;

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

    long long     block_gen; /* bumps on every set_block(_meta); window-refill memo */
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

/* Mesher output is a six-vertex triangle list per source quad. Stable-bucket a
 * quad by its minimum vertex y so boundary-crossing geometry stays with the
 * lower source section. The clamp covers the world's bottom/top boundary. */
static int wl_quad_section(const CrVertex *quad) {
    float miny = quad[0].pos.y;
    for (int i = 1; i < 6; ++i)
        if (quad[i].pos.y < miny) miny = quad[i].pos.y;
    int y = (int)floorf(miny);
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    return y >> 4;
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

        if (l == CR_LAYER_TRANSLUCENT) {
            /* Preserve the mesher's exact blend order. The window path submits
             * this full layer whenever its column passes the coarse cull. */
            if (nl)
                memcpy(s->buf + off, w->scratch.verts[l],
                       (size_t)nl * sizeof(CrVertex));
            for (int sec = 0; sec < WL_SECTIONS; ++sec)
                s->sec_off[l][sec] = off;
            s->sec_n[l][0] = nl;
        } else {
            assert(nl % 6 == 0);
            for (int i = 0; i < nl; i += 6)
                s->sec_n[l][wl_quad_section(w->scratch.verts[l] + i)] += 6;

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
                int sec = wl_quad_section(quad);
                memcpy(s->buf + cursor[sec], quad, 6 * sizeof(CrVertex));
                cursor[sec] += 6;
            }
        }
        off += nl;
        s->view.verts[l]  = s->buf + s->off[l];
        s->view.nverts[l] = nl;
    }
    s->cx = cx; s->cz = cz; s->valid = 1; s->dirty = 0; s->builds++;

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
        w->scratch.nverts[l] = 0;
        w->out_verts[l] = (CrVertex *)malloc(
            (size_t)caps->draw_max[l] * sizeof(CrVertex));
        w->out_nverts[l] = 0;
        if (!w->scratch.verts[l] || !w->out_verts[l]) { gm_world_destroy(w); return NULL; }
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

void gm_world_destroy(GmWorld *w) {
    if (!w) return;
    if (w->slots) {
        for (int i = 0; i < w->mesh_slots; ++i) free(w->slots[i].buf);
        free(w->slots);
    }
    for (int l = 0; l < 4; ++l) {
        free(w->scratch.verts[l]);
        free(w->out_verts[l]);
    }
    free(w->bfs_col);
    free(w->bfs_seen);
    free(w->bfs_reached);
    free(w->bfs_setfac);
    free(w->bfs_facing);
    free(w->bfs_queue);
    if (w->wmc) worldmc_destroy(w->wmc);
    free(w);
}

void gm_world_ensure(GmWorld *w, int ccx, int ccz, int radius) {
    if (!w) return;
    worldmc_ensure(w->wmc, ccx, ccz, radius);
}

int gm_world_block(const GmWorld *w, int wx, int wy, int wz) {
    if (!w) return 0;
    return mc_state_id(light_state(w->light, wx, wy, wz));
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
    int old_id = gm_world_block(w, wx, wy, wz);

    /* edit the block store, then re-light: light_ensure re-runs sky light for the
     * (idempotent) chunk generation and the global block-light BFS over loaded
     * chunks, which is local in effect (block light radius <= 15 = one chunk). */
    light_set_state(w->light, wx, wy, wz, mc_state(id, meta));
    worldmc_ensure(w->wmc, cx, cz, 0);
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

long long gm_world_block_gen(const GmWorld *w) {
    /* fold in chunk generation: population (trees/structures) writes into
     * neighbour chunks directly, bypassing set_block_meta */
    return w ? w->block_gen + light_gen_events(w->light) : 0;
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
    worldmc_ensure(w->wmc, ccx, ccz, PSV_R);

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
    worldmc_ensure(w->wmc, ccx, ccz, R + 1);

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
            w->bfs_col[(cx - ccx + R) * D + (cz - ccz + R)] = c;
            if (em) {
                if (em->nch >= em->max_chunks) { em->overflow = 1; }
                else {
                    GmChunkDraw *d = &em->chunks[em->nch++];
                    d->slot   = (int)(c - w->slots);
                    d->builds = c->builds;
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

            int col_reached = !occl_on;
            if (occl_on)
                for (int sec = 0; sec < WL_SECTIONS; ++sec)
                    col_reached |= w->bfs_reached[cell * WL_SECTIONS + sec];

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
                /* wl_quad_section buckets a quad by its MINIMUM vertex y, so the
                 * flat top faces of the cells at y = 16*sec+15 land in bucket
                 * sec+1 (that is also why the frustum AABB above carries a
                 * 1-block pad). Bucket `sec` therefore holds geometry owned by
                 * the CELLS of sections sec and sec-1, and may only be dropped
                 * when the flood reached NEITHER. Without this, a reached floor
                 * section loses its top surface whenever the section above it is
                 * occluded - a cave ceiling/floor pixel bug. */
                int sec_reached = w->bfs_reached[cell * WL_SECTIONS + sec] ||
                    (sec > 0 && w->bfs_reached[cell * WL_SECTIONS + sec - 1]);
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
            /* W16 exempted TRANSLUCENT from vertical section culling to keep the
             * mesher's blend order. W19 keeps that: the column is dropped only
             * when the flood reached NO section of it (nothing in the column is
             * visible at all); otherwise it goes out whole, in the same order. */
            if (col_reached) {
                if (em)
                    wl_emit_run(em, CR_LAYER_TRANSLUCENT, slot,
                                c->off[CR_LAYER_TRANSLUCENT],
                                c->n[CR_LAYER_TRANSLUCENT]);
                else
                    wl_append(w, CR_LAYER_TRANSLUCENT,
                              c->view.verts[CR_LAYER_TRANSLUCENT],
                              c->view.nverts[CR_LAYER_TRANSLUCENT]);
                added[CR_LAYER_TRANSLUCENT] = c->view.nverts[CR_LAYER_TRANSLUCENT];
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

int gm_world_mesh_chunks(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                         GmChunkDraw *out, int max_out, int nverts[4]) {
    if (!w || !cam || !out) return 0;
    /* Device-resident replay remains a full-column gather. W16 is deliberately
     * window-path CPU submission only; it does not change the CUDA gather API or
     * its fixed one-entry-per-mesh-slot storage. */
    int R = SCN_VIEW_RADIUS;
    { int r = cr_cfg()->view_radius_active;
      if (r >= 1 && r <= SCN_VIEW_RADIUS) R = r; }
    if (R > w->caps->view_radius) R = w->caps->view_radius;
    const int ccx = wl_floordiv16((int)floorf(cam->pos.x));
    const int ccz = wl_floordiv16((int)floorf(cam->pos.z));
    worldmc_ensure(w->wmc, ccx, ccz, R + 1);

    CrMat4 proj = cr_perspective(cam->fov_deg, (float)fb_w / (float)fb_h,
                                 cam->znear, cam->zfar);
    CrMat4 view = cr_camera_view(cam);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);
    const int cull_off = cr_cfg()->no_cull;

    int nout = 0;
    for (int l = 0; l < 4; ++l) nverts[l] = 0;
    for (int cx = ccx - R; cx <= ccx + R; ++cx) {
        for (int cz = ccz - R; cz <= ccz + R; ++cz) {
            double minx = (double)(cx * 16), maxx = (double)(cx * 16 + 16);
            double minz = (double)(cz * 16), maxz = (double)(cz * 16 + 16);
            if (!(cull_off ||
                  cr_aabb_in_frustum(planes, minx, WL_AABB_Y_MIN, minz,
                                     maxx, WL_AABB_Y_MAX, maxz)))
                continue;
            WlSlot *c = wl_ensure_mesh(w, cx, cz);
            if (!c || nout >= max_out) continue;
            GmChunkDraw *d = &out[nout++];
            d->slot   = (int)(c - w->slots);
            d->builds = c->builds;
            d->slab   = c->buf;
            for (int l = 0; l < 4; ++l) {
                d->off[l] = c->off[l];
                d->n[l]   = c->n[l];
                nverts[l] += c->n[l];
            }
        }
    }
    return nout;
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
    /* doWeatherCycle=false freezes the server weather state while the
     * independent daylight rule still controls world-time advancement. */
    if (c->freeze_weather) {
        ++c->total_time;
        if (!c->freeze_daylight) ++c->world_time;
        g_clock.ww.totalTime = c->total_time;
        g_clock.ww.worldTime = c->world_time;
        return;
    }
    i64 wt_prev = g_clock.ww.worldTime;
    if (c->weather_cycle) {
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
