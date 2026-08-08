/* game/test_world_live.c - standalone verification for game/world_live.c.
 *
 * (A) REGRESSION LOCK: gm_world_mesh_view over a fresh gm_world_create(0) at the
 *     FROZEN chunk_scene pose has the exact same triangles as chunkscene_init.
 *     The live cache packs opaque/cutout triangles by section, so those three
 *     layers are compared as exact triangle multisets. The live translucent
 *     layer is camera-sorted, so its source geometry is also compared as a
 *     multiset and its far-to-near order is checked independently.
 * (B) DIRTY CACHE: after gm_world_set_block, the touched chunk is re-meshed and its
 *     vertex buffer changes; a chunk two chunks away is byte-identical + not rebuilt.
 * (C) FILL WINDOW: gm_world_fill_window read back via mc_get equals gm_world_block at
 *     the matching world coords, for a region far from the origin (chunk 100,100).
 *
 * Build via game/test_world_live.sh (no Makefile edits).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "core/config.h"
#include "game/caps.h"
#include "game/game.h"
#include "world/mesh_mc.h"
#include "verify/chunk_scene.h"   /* ChunkScene + chunkscene_init (frozen pose) */
#include "player_survival.h"             /* struct Chunk, mc_get, mc_state_id, PSV_* */

/* test hooks exported (non-header) by world_live.c */
const CrChunkMeshMC *gm_world__cached_mesh(GmWorld *w, int cx, int cz, int *builds);
int gm_world__model_key(const GmWorld *w, int wx, int wy, int wz);
int gm_world__translucent_sorted(GmWorld *w, int cx, int cz,
                                 const CrCamera *cam);
int gm_world__section_nverts(GmWorld *w, int cx, int cz, int layer, int sec);

#define W 854
#define H 480

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; } } while (0)

/* deep-copy a CrChunkMeshMC's per-layer verts (so a later rebuild can't alias it). */
typedef struct { CrVertex *v[4]; int n[4]; } MeshSnap;
static void snap_take(MeshSnap *s, const CrChunkMeshMC *m) {
    for (int l = 0; l < 4; ++l) {
        s->n[l] = m->nverts[l];
        s->v[l] = NULL;
        if (s->n[l] > 0) {
            s->v[l] = (CrVertex *)malloc((size_t)s->n[l] * sizeof(CrVertex));
            memcpy(s->v[l], m->verts[l], (size_t)s->n[l] * sizeof(CrVertex));
        }
    }
}
static void snap_free(MeshSnap *s) { for (int l = 0; l < 4; ++l) free(s->v[l]); }
static int snap_equal(const MeshSnap *s, const CrChunkMeshMC *m) {
    for (int l = 0; l < 4; ++l) {
        if (s->n[l] != m->nverts[l]) return 0;
        if (s->n[l] && memcmp(s->v[l], m->verts[l],
                              (size_t)s->n[l] * sizeof(CrVertex)) != 0) return 0;
    }
    return 1;
}

typedef struct { CrVertex v[3]; } TestTri;

static int tri_cmp(const void *a, const void *b) {
    return memcmp(a, b, sizeof(TestTri));
}

static int tris_equal_unordered(const CrVertex *a, const CrVertex *b, int nverts) {
    if (nverts % 3) return 0;
    size_t ntri = (size_t)nverts / 3;
    TestTri *sa = (TestTri *)malloc(ntri * sizeof(TestTri));
    TestTri *sb = (TestTri *)malloc(ntri * sizeof(TestTri));
    if (!sa || !sb) { free(sa); free(sb); return 0; }
    memcpy(sa, a, ntri * sizeof(TestTri));
    memcpy(sb, b, ntri * sizeof(TestTri));
    qsort(sa, ntri, sizeof(TestTri), tri_cmp);
    qsort(sb, ntri, sizeof(TestTri), tri_cmp);
    int same = memcmp(sa, sb, ntri * sizeof(TestTri)) == 0;
    free(sa);
    free(sb);
    return same;
}

/* ---------------- (A) regression lock ---------------- */
static void test_regression_lock(void) {
    printf("== (A) regression lock vs chunkscene_init ==\n");

    /* Compare the complete meshes, not the independently evolved visibility
     * filters. no_cull disables both chunkscene's column filter and the live
     * path's column/section/occlusion filters, leaving exactly the shared
     * radius-R mesh concatenation that this regression lock is meant to test. */
    CHECK(cr_cfg_set("no_cull", "1") == 0, "enable full-mesh comparison");

    ChunkScene s;
    chunkscene_init(&s, W, H);

    GmWorld *w = gm_world_create(0);

    const float pi = 3.14159265358979323846f;
    CrCamera cam = {0};
    memset(&cam, 0, sizeof(cam));
    cam.pos.x   = 8.2994f;
    cam.pos.y   = 95.0f;
    cam.pos.z   = 40.0f;
    cam.yaw     = 0.0f;
    cam.pitch   = -35.0f * (pi / 180.0f);
    cam.fov_deg = 70.0f;
    cam.aspect  = (float)W / (float)H;
    cam.znear   = 0.05f;
    cam.zfar    = 600.0f;

    GmMeshView mv;
    gm_world_mesh_view(w, &cam, W, H, &mv);

    CHECK(mv.n_kept == s.n_kept, "n_kept matches chunkscene");
    CHECK(mv.n_culled == s.n_culled, "n_culled matches chunkscene");
    printf("   kept=%d culled=%d (chunkscene kept=%d culled=%d)\n",
           mv.n_kept, mv.n_culled, s.n_kept, s.n_culled);

    int all_identical = 1;
    for (int l = 0; l < 4; ++l) {
        int neq = (mv.nverts[l] == s.nverts[l]);
        int beq = neq && (mv.nverts[l] == 0 ||
                          tris_equal_unordered(mv.verts[l], s.verts[l],
                                               mv.nverts[l]));
        printf("   layer %d: live nverts=%-7d scene nverts=%-7d  %s\n",
               l, mv.nverts[l], s.nverts[l],
               beq ? "TRIANGLE-IDENTICAL" : "DIFFERS");
        if (!beq) all_identical = 0;
        CHECK(neq, "per-layer nverts match");
        CHECK(beq, "per-layer geometry matches");
    }
    CHECK(all_identical, "all layers geometry-identical to chunkscene");
    CHECK(gm_world__translucent_sorted(w, 0, 2, &cam),
          "cached translucent quads are far-to-near per render section");

    /* The device-resident path records slab runs instead of copying. Re-read
     * every run on the host and prove it reproduces the exact concatenation,
     * including reverse RenderInfo order for TRANSLUCENT. */
    {
        const CrCaps *caps = cr_caps();
        int stride = caps->mesh_slots * GM_MESH_SECTIONS;
        GmChunkDraw *chunks = calloc(
            (size_t)caps->mesh_slots, sizeof(*chunks));
        GmMeshRun *runs = calloc((size_t)4 * stride, sizeof(*runs));
        int nruns[4], nverts[4], kept, culled;
        int nch = gm_world_mesh_runs(
            w, &cam, W, H, chunks, caps->mesh_slots,
            runs, stride, nruns, nverts, &kept, &culled);
        CHECK(chunks && runs && nch >= 0, "mesh-run buffers and walk succeed");
        CHECK(kept == mv.n_kept && culled == mv.n_culled,
              "mesh-run kept/culled counts match host concat");
        for (int layer = 0; layer < 4 && nch >= 0; ++layer) {
            int cursor = 0;
            CHECK(nverts[layer] == mv.nverts[layer],
                  "mesh-run vertex total matches host concat");
            for (int ri = 0; ri < nruns[layer]; ++ri) {
                const GmMeshRun *run = &runs[layer * stride + ri];
                const CrVertex *slab = NULL;
                for (int ci = 0; ci < nch; ++ci)
                    if (chunks[ci].slot == run->slot) {
                        slab = chunks[ci].slab;
                        break;
                    }
                CHECK(slab != NULL, "mesh run names a resident slab");
                if (slab)
                    CHECK(memcmp(mv.verts[layer] + cursor,
                                 slab + run->off,
                                 (size_t)run->n * sizeof(CrVertex)) == 0,
                          "mesh run bytes match host concat");
                cursor += run->n;
            }
            CHECK(cursor == mv.nverts[layer],
                  "mesh runs cover complete host layer");
        }
        free(runs);
        free(chunks);
    }

    gm_world_destroy(w);
    chunkscene_free(&s);
    CHECK(cr_cfg_set("no_cull", "0") == 0, "restore culling default");
}

/* ---------------- (B) dirty cache ---------------- */
static void test_dirty_cache(void) {
    printf("== (B) dirty cache ==\n");

    GmWorld *w = gm_world_create(0);
    /* ensure a neighbourhood so every meshed chunk has its 8 neighbours. */
    gm_world_ensure(w, 0, 0, 4);

    const int NEAR_CX = 0,  NEAR_CZ = 0;   /* chunk we edit                 */
    const int FAR_CX  = 2,  FAR_CZ  = 0;   /* two chunks away in +X         */

    int b_near0 = 0, b_far0 = 0;
    const CrChunkMeshMC *m_near = gm_world__cached_mesh(w, NEAR_CX, NEAR_CZ, &b_near0);
    const CrChunkMeshMC *m_far  = gm_world__cached_mesh(w, FAR_CX,  FAR_CZ,  &b_far0);
    MeshSnap near0, far0;
    snap_take(&near0, m_near);
    snap_take(&far0,  m_far);

    /* place a stone cube in the middle of NEAR chunk, above the surface (non-border,
     * so no neighbour is marked dirty). This must change NEAR's mesh. */
    int wx = NEAR_CX * 16 + 4, wz = NEAR_CZ * 16 + 4;
    int wy = gm_world_surface_y(w, wx, wz);
    CHECK(gm_world_block(w, wx, wy, wz) == 0, "target cell is air before edit");
    gm_world_set_block(w, wx, wy, wz, CB_STONE);
    CHECK(gm_world_block(w, wx, wy, wz) == CB_STONE, "block store updated by set_block");

    int b_near1 = 0, b_far1 = 0;
    const CrChunkMeshMC *m_near2 = gm_world__cached_mesh(w, NEAR_CX, NEAR_CZ, &b_near1);
    const CrChunkMeshMC *m_far2  = gm_world__cached_mesh(w, FAR_CX,  FAR_CZ,  &b_far1);

    CHECK(b_near1 == b_near0 + 1, "NEAR chunk was re-meshed exactly once");
    CHECK(!snap_equal(&near0, m_near2), "NEAR chunk vertex buffer CHANGED after edit");
    CHECK(b_far1 == b_far0, "FAR chunk was NOT re-meshed");
    CHECK(snap_equal(&far0, m_far2), "FAR chunk vertex buffer BYTE-IDENTICAL after edit");
    printf("   NEAR builds %d->%d (changed), FAR builds %d->%d (identical)\n",
           b_near0, b_near1, b_far0, b_far1);

    snap_free(&near0);
    snap_free(&far0);
    gm_world_destroy(w);
}

/* ---------------- (C) fill window ---------------- */
static void test_fill_window(void) {
    printf("== (C) fill window (chunk 100,100) ==\n");

    GmWorld *w = gm_world_create(0);
    const int CCX = 100, CCZ = 100;

    /* blaze `Chunk` is a typedef of an anonymous struct; game.h's seam type is the
     * opaque `struct Chunk`. Allocate the real thing and pass it through the seam. */
    Chunk *win = (Chunk *)malloc((size_t)PSV_NCHUNKS * sizeof(Chunk));
    CHECK(win != NULL, "window allocation");
    if (!win) { gm_world_destroy(w); return; }

    gm_world_fill_window(w, CCX, CCZ, (struct Chunk *)win);

    /* check center + a couple of neighbour chunks at several local coords. */
    int checks = 0, ok = 0;
    for (int dz = -PSV_R; dz <= PSV_R; ++dz)
        for (int dx = -PSV_R; dx <= PSV_R; ++dx) {
            int i = (dz + PSV_R) * PSV_DIM + (dx + PSV_R);
            int baseX = (CCX + dx) * 16, baseZ = (CCZ + dz) * 16;
            int coords[5][3] = { {0,0,0}, {15,64,15}, {8,120,8}, {3,200,11}, {7,40,2} };
            for (int c = 0; c < 5; ++c) {
                int lx = coords[c][0], y = coords[c][1], lz = coords[c][2];
                int fromwin = mc_state_id(mc_get(&win[i], lx, y, lz));
                int fromworld = gm_world_block(w, baseX + lx, y, baseZ + lz);
                checks++;
                if (fromwin == fromworld) ok++;
                else printf("   MISMATCH chunk(%d,%d) l(%d,%d,%d): win=%d world=%d\n",
                            CCX + dx, CCZ + dz, lx, y, lz, fromwin, fromworld);
            }
        }
    printf("   %d/%d cells match between window and world\n", ok, checks);
    CHECK(ok == checks, "all sampled window cells equal gm_world_block");

    /* the far region must actually be non-empty terrain (proves it was generated). */
    int surf = gm_world_surface_y(w, CCX * 16 + 8, CCZ * 16 + 8);
    CHECK(surf > 0 && surf < 256, "far region generated (plausible surface)");
    printf("   surface_y at (%d,%d) = %d\n", CCX * 16 + 8, CCZ * 16 + 8, surf);

    free(win);
    gm_world_destroy(w);
}

/* ---------------- (D) canonical gameplay state vs renderer model key ------ */
static void test_state_namespace(void) {
    printf("== (D) canonical block-state namespace ==\n");
    GmWorld *w = gm_world_create(0);
    CHECK(w != NULL, "namespace world allocation");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 4);

    int water[3] = {0,0,0}, logp[3] = {0,0,0};
    int have_water = 0, have_log = 0;
    for (int wx = -64; wx < 80 && (!have_water || !have_log); ++wx)
        for (int wz = -64; wz < 80 && (!have_water || !have_log); ++wz)
            for (int y = 0; y < 160 && (!have_water || !have_log); ++y) {
                int key = gm_world__model_key(w, wx, y, wz);
                if (!have_water && key == 2) {
                    water[0] = wx; water[1] = y; water[2] = wz; have_water = 1;
                }
                if (!have_log && key == 31) {
                    logp[0] = wx; logp[1] = y; logp[2] = wz; have_log = 1;
                }
            }
    CHECK(have_water, "seed-0 generated PB water fixture found");
    CHECK(have_log, "seed-0 generated PB oak-log fixture found");

    Chunk *win = (Chunk *)malloc((size_t)PSV_NCHUNKS * sizeof(Chunk));
    CHECK(win != NULL, "namespace physics window allocation");
    if (win && have_water) {
        int ccx = water[0] >> 4, ccz = water[2] >> 4;
        gm_world_fill_window(w, ccx, ccz, (struct Chunk *)win);
        int ci = PSV_R * PSV_DIM + PSV_R;
        u16 state = mc_get(&win[ci], water[0] & 15, water[1], water[2] & 15);
        BptProps p = mc_bpt_props(mc_state_id(state));
        CHECK(mc_state_id(state) == BLK_WATER, "PB water enters physics as vanilla water");
        CHECK((p.flags & BF_LIQUID) != 0 && (p.flags & BF_SOLID) == 0,
              "generated water is liquid and non-solid");
    }
    if (win && have_log) {
        int ccx = logp[0] >> 4, ccz = logp[2] >> 4;
        gm_world_fill_window(w, ccx, ccz, (struct Chunk *)win);
        int ci = PSV_R * PSV_DIM + PSV_R;
        u16 state = mc_get(&win[ci], logp[0] & 15, logp[1], logp[2] & 15);
        BptProps p = mc_bpt_props(mc_state_id(state));
        CHECK(mc_state_id(state) == BLK_LOG, "PB oak log enters physics as vanilla log");
        CHECK((p.flags & BF_SOLID) != 0 && p.hardness == 2.0f,
              "generated log is solid with vanilla hardness");
    }

    /* These vanilla ids collide numerically with PB flowers/double plants. The
     * semantic state must survive while the renderer receives a distinct fallback. */
    {
        static const int ids[4] = {59, 60, 61, 64};
        static const int metas[4] = {4, 7, 3, 4};
        int sy = gm_world_surface_y(w, 8, 8) + 2;
        for (int i = 0; i < 4; ++i) {
            int wx = 8 + i;
            gm_world_set_block_meta(w, wx, sy, 8, ids[i], metas[i]);
            CHECK(gm_world_block(w, wx, sy, 8) == ids[i],
                  "vanilla edit id survives round trip");
            CHECK(gm_world_meta(w, wx, sy, 8) == metas[i],
                  "vanilla edit metadata survives round trip");
            CHECK(gm_world__model_key(w, wx, sy, 8) != ids[i],
                  "vanilla edit never aliases same-numbered PB key");
        }
        if (win) {
            gm_world_fill_window(w, 0, 0, (struct Chunk *)win);
            int ci = PSV_R * PSV_DIM + PSV_R;
            for (int i = 0; i < 4; ++i) {
                u16 state = mc_get(&win[ci], 8 + i, sy, 8);
                CHECK(mc_state_id(state) == ids[i] && mc_state_meta(state) == metas[i],
                      "physics window preserves vanilla edit state");
            }
        }
    }

    free(win);
    gm_world_destroy(w);
}

/* ---------------- (E) vanilla-default superflat provider ----------------- */
static void test_superflat(void) {
    printf("== (E) superflat RL arena ==\n");
    GmWorld *w = gm_world_create_type(12345, 1);
    CHECK(w != NULL, "superflat world allocation");
    if (!w) return;
    gm_world_ensure(w, -2, 3, 1);
    static const int coords[4][2] = {{-48,32},{-33,47},{-17,63},{-1,79}};
    for (int i = 0; i < 4; ++i) {
        int x = coords[i][0], z = coords[i][1];
        CHECK(gm_world_block(w, x, 0, z) == BLK_BEDROCK,
              "superflat y0 is bedrock");
        CHECK(gm_world_block(w, x, 1, z) == BLK_DIRT &&
              gm_world_block(w, x, 2, z) == BLK_DIRT,
              "superflat y1-2 are dirt");
        CHECK(gm_world_block(w, x, 3, z) == BLK_GRASS,
              "superflat y3 is grass");
        CHECK(gm_world_block(w, x, 4, z) == BLK_AIR,
              "superflat y4 is air");
        CHECK(gm_world_surface_y(w, x, z) == 4,
              "superflat surface is deterministic");
    }
    gm_world_destroy(w);
}

/* ---------------- (F) exact source RenderChunk ownership ------------------ */
static void test_section_ownership(void) {
    printf("== (F) source RenderChunk ownership ==\n");
    GmWorld *w = gm_world_create(0);
    CHECK(w != NULL, "section-ownership world allocation");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 1);

    int before14 = gm_world__section_nverts(
        w, 0, 0, CR_LAYER_TRANSLUCENT, 14);
    int before15 = gm_world__section_nverts(
        w, 0, 0, CR_LAYER_TRANSLUCENT, 15);
    gm_world_set_block_meta(w, 4, 239, 4, 95, 0);
    gm_world_set_block_meta(w, 8, 240, 8, 95, 0);
    int after14 = gm_world__section_nverts(
        w, 0, 0, CR_LAYER_TRANSLUCENT, 14);
    int after15 = gm_world__section_nverts(
        w, 0, 0, CR_LAYER_TRANSLUCENT, 15);

    CHECK(after14 - before14 == 36,
          "all six y=239 glass faces stay in source section 14");
    CHECK(after15 - before15 == 36,
          "all six y=240 glass faces stay in source section 15");
    gm_world_destroy(w);
}

int main(void) {
    test_regression_lock();
    test_dirty_cache();
    test_fill_window();
    test_state_namespace();
    test_superflat();
    test_section_ownership();

    if (fails == 0) { printf("\nALL PASS\n"); return 0; }
    printf("\n%d CHECK(S) FAILED\n", fails);
    return 1;
}
