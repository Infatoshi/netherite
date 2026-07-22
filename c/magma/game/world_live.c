/* game/world_live.c - LIVE streaming view-distance world over the MC-faithful mesher.
 *
 * Wraps a CrWorldMC (world/mesh_mc.c) + the CrLight block store and adds:
 *   - a per-loaded-chunk cache of the 4 per-CrRenderLayer CrVertex buffers
 *     (CrChunkMeshMC) with a `dirty` flag, so unchanged chunks are never re-meshed,
 *   - gm_world_mesh_view: build the 6 frustum planes from the camera EXACTLY like
 *     raster/verify/chunk_scene.h (cr_perspective + cr_look_yaw_pitch + cr_frustum_
 *     extract), iterate the Chebyshev radius SCN_VIEW_RADIUS with the same cx-outer /
 *     cz-inner order, frustum-test each chunk's full [0,256] column AABB, mesh kept
 *     chunks (rebuild only if dirty or not yet meshed) and concatenate their per-layer
 *     verts into world-owned buffers (grown with realloc like scn__append). This
 *     reproduces chunkscene_init byte-for-byte at the frozen pose over a fresh world.
 *   - gm_world_block / gm_world_fill_window read canonical vanilla states through CrLight,
 *   - gm_world_set_block edits the store, re-lights, marks the touched chunk (and any
 *     border neighbour) dirty.
 *
 * Build STANDALONE via game/test_world_live.sh (no Makefile edits). Compile with
 * -ffp-contract=off -Wall -Wextra and -I. -Icore -I<mc-sim>/core.
 */
#include "game/game.h"
#include "game/caps.h"
#include "core/types.h"
#include "core/frustum.h"
#include "world/mesh_mc.h"
#include "world/light.h"
#include "world/populate_mc.h"   /* popmc_window_builds (compute-once metric, debug) */

/* raw mc-sim Chunk + mc_set/mc_state + PSV_DIM/PSV_R/PSV_NCHUNKS for fill_window. */
#include "player_survival.h"
#include "world_weather.h"   /* ww_init / ww_tick for live world-time composition */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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

/* floor-division of a block coord to its chunk coord (identical to scn__floordiv16). */
static inline int wl_floordiv16(int a) { return a >> 4; }

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

/* MAGMA_DEBUG_CAPS: worst-case single-chunk mesh maxima, for fixed-pool sizing. */
static int g_caps_on = -1;
static int g_cap_chunk_layer[4] = {0,0,0,0};
static int g_cap_chunk_total = 0;

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
        if (nl) memcpy(s->buf + off, w->scratch.verts[l], (size_t)nl * sizeof(CrVertex));
        off += nl;
        s->view.verts[l]  = s->buf + s->off[l];
        s->view.nverts[l] = nl;
    }
    s->cx = cx; s->cz = cz; s->valid = 1; s->dirty = 0; s->builds++;

    if (g_caps_on < 0) g_caps_on = getenv("MAGMA_DEBUG_CAPS") != NULL;
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
    light_set_state(w->light, wx, wy, wz, mc_state(id, meta));
    wl_mark_dirty(w, cx, cz);
    int lx = wx & 15, lz = wz & 15;
    if (lx == 0)  wl_mark_dirty(w, cx - 1, cz);
    if (lx == 15) wl_mark_dirty(w, cx + 1, cz);
    if (lz == 0)  wl_mark_dirty(w, cx, cz - 1);
    if (lz == 15) wl_mark_dirty(w, cx, cz + 1);
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

CrTexture gm_world_atlas(const GmWorld *w) {
    return worldmc_atlas(w->wmc);
}

void gm_world_fill_window(GmWorld *w, int ccx, int ccz, struct Chunk *win) {
    if (!w || !win) return;
    /* game.h's seam type `struct Chunk` is an opaque forward-decl; the caller hands
     * us a real region of mc-sim `Chunk` (an anonymous-struct typedef). Re-view it as
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

void gm_world_mesh_view(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                        GmMeshView *out) {
    if (!w || !cam || !out) return;

    /* View radius: default SCN_VIEW_RADIUS (=12, keeps the byte-identical regression
     * lock when the env is unset). MAGMA_VIEW_RADIUS lowers it at runtime for smooth
     * interactive FPS (fewer chunks meshed/rasterized); clamped to [1, SCN_VIEW_RADIUS]. */
    int R = SCN_VIEW_RADIUS;
    { const char *e = getenv("MAGMA_VIEW_RADIUS");
      if (e) { int r = atoi(e); if (r >= 1 && r <= SCN_VIEW_RADIUS) R = r; } }
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
    CrMat4 view = cr_look_yaw_pitch(cam->pos, cam->yaw, cam->pitch);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);

    const int cull_off = getenv("MAGMA_NO_CULL") != NULL;
    /* MAGMA_DEBUG_VERTS: prove far-chunk decoration reaches the mesh - tally the
     * leaf (CUTOUT_MIPPED=1) + solid (0, includes logs) verts contributed by chunks
     * OUTSIDE the origin 2x2 vs inside it. Before view-distance populate the far
     * tally was 0 (only chunks 0..1 were decorated). */
    const int dbg_verts = getenv("MAGMA_DEBUG_VERTS") != NULL;
    long dbg_far_leaf = 0, dbg_near_leaf = 0, dbg_far_solid = 0;
    int  dbg_far_chunks_with_leaf = 0;

    /* reset the world-owned output buffers (reuse allocations). */
    for (int l = 0; l < 4; ++l) w->out_nverts[l] = 0;
    int n_kept = 0, n_culled = 0;

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
            for (int l = 0; l < 4; ++l)
                wl_append(w, l, c->view.verts[l], c->view.nverts[l]);
            if (dbg_verts) {
                int in2x2 = (cx >= 0 && cx < 2 && cz >= 0 && cz < 2);
                if (in2x2) dbg_near_leaf += c->view.nverts[1];
                else {
                    dbg_far_leaf  += c->view.nverts[1];
                    dbg_far_solid += c->view.nverts[0];
                    if (c->view.nverts[1] > 0) dbg_far_chunks_with_leaf++;
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

    if (g_caps_on < 0) g_caps_on = getenv("MAGMA_DEBUG_CAPS") != NULL;
    if (g_caps_on) {
        int valid = 0;
        for (int i = 0; i < w->mesh_slots; ++i) if (w->slots[i].valid) valid++;
        fprintf(stderr,
            "[caps] R=%d kept=%d culled=%d mesh_slots=%d/%d light_chunks=%d owr_windows=%ld "
            "drawverts[S/CM/C/T]=%d/%d/%d/%d "
            "chunk_max[S/CM/C/T]=%d/%d/%d/%d chunk_max_total=%d\n",
            R, n_kept, n_culled, valid, w->mesh_slots, light_loaded_chunks(w->light),
            popmc_window_builds(),
            w->out_nverts[0], w->out_nverts[1], w->out_nverts[2], w->out_nverts[3],
            g_cap_chunk_layer[0], g_cap_chunk_layer[1], g_cap_chunk_layer[2],
            g_cap_chunk_layer[3], g_cap_chunk_total);
    }

    for (int l = 0; l < 4; ++l) {
        out->verts[l]  = w->out_verts[l];
        out->nverts[l] = w->out_nverts[l];
    }
    out->n_kept   = n_kept;
    out->n_culled = n_culled;
}

int gm_world_mesh_chunks(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                         GmChunkDraw *out, int max_out, int nverts[4]) {
    if (!w || !cam || !out) return 0;
    /* SAME radius / ensure / frustum / iteration order as gm_world_mesh_view:
     * the device-side concat of these entries must be byte-identical to the
     * wl_append concat, so any logic drift here is a pixel bug. */
    int R = SCN_VIEW_RADIUS;
    { const char *e = getenv("MAGMA_VIEW_RADIUS");
      if (e) { int r = atoi(e); if (r >= 1 && r <= SCN_VIEW_RADIUS) R = r; } }
    if (R > w->caps->view_radius) R = w->caps->view_radius;
    const int ccx = wl_floordiv16((int)floorf(cam->pos.x));
    const int ccz = wl_floordiv16((int)floorf(cam->pos.z));
    worldmc_ensure(w->wmc, ccx, ccz, R + 1);

    CrMat4 proj = cr_perspective(cam->fov_deg, (float)fb_w / (float)fb_h,
                                 cam->znear, cam->zfar);
    CrMat4 view = cr_look_yaw_pitch(cam->pos, cam->yaw, cam->pitch);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);
    const int cull_off = getenv("MAGMA_NO_CULL") != NULL;

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
    g_clock.inited = 1;
    c->total_time   = g_clock.ww.totalTime;
    c->world_time   = g_clock.ww.worldTime;
    c->rain_time    = g_clock.ww.rainTime;
    c->thunder_time = g_clock.ww.thunderTime;
    c->raining      = g_clock.ww.raining;
    c->thundering   = g_clock.ww.thundering;
}

void gm_world_tick(GmWorldClock *c) {
    if (!c) return;
    if (!g_clock.inited) gm_world_clock_init(c, 0);
    i64 wt_prev = g_clock.ww.worldTime;
    ww_tick(&g_clock.ww);
    if (c->freeze_daylight) g_clock.ww.worldTime = wt_prev;
    c->total_time   = g_clock.ww.totalTime;
    c->world_time   = g_clock.ww.worldTime;
    c->rain_time    = g_clock.ww.rainTime;
    c->thunder_time = g_clock.ww.thunderTime;
    c->raining      = g_clock.ww.raining;
    c->thundering   = g_clock.ww.thundering;
}

void gm_world_tick_clear(GmWorldClock *c) {
    if (!c) return;
    ++c->total_time;
    if (!c->freeze_daylight) ++c->world_time;
    c->rain_time = 0;
    c->thunder_time = 0;
    c->raining = 0;
    c->thundering = 0;
}

void gm_world_clock_set_weather(GmWorldClock *c, int raining, int thundering,
                                int rain_time, int thunder_time) {
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
    g_clock.ww.worldTime = c->world_time;
    g_clock.ww.totalTime = c->total_time;
}
