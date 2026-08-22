/* game/test_world_live.c - standalone verification for game/world_live.c.
 *
 * (A) REGRESSION LOCK at the FROZEN chunk_scene pose (seed 0, fixed cam):
 *     - Column frustum: gm_world_mesh_view n_kept/n_culled match chunkscene_init.
 *     - Full-column geometry: gm_world_mesh_chunks per-layer nverts match the
 *       frozen scene (same mesher, no W16/W19 submission filters).
 *     - Per kept chunk: live cached mesh is the mesher output section-packed
 *       exactly as wl_ensure_mesh does (opaque layers reordered by 16-high
 *       section; translucent preserves blend order byte-for-byte).
 *     - View submission is a filter, not a second mesher: mesh_view nverts[l]
 *       never exceeds the full-column total (section frustum + occlusion only
 *       REMOVE geometry).
 *     Pre-W16 mesh_view == full concat was the old contract; after section
 *     packing + occlusion that equality is intentionally false. Asserting it
 *     would reject correct production code.
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
#include "game/game.h"
#include "world/mesh_mc.h"
#include "verify/chunk_scene.h"   /* ChunkScene + chunkscene_init (frozen pose) */
#include "player_survival.h"             /* struct Chunk, mc_get, mc_state_id, PSV_* */
#include "verify/ui_entities/entity_oracle_scene.h"

/* test hooks exported (non-header) by world_live.c */
const CrChunkMeshMC *gm_world__cached_mesh(GmWorld *w, int cx, int cz, int *builds);
int gm_world__model_key(const GmWorld *w, int wx, int wy, int wz);

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

/* Section bucket identical to world_live.c wl_quad_section (min vertex y / 16). */
static int test_quad_section(const CrVertex *quad) {
    float miny = quad[0].pos.y;
    for (int i = 1; i < 6; ++i)
        if (quad[i].pos.y < miny) miny = quad[i].pos.y;
    int y = (int)floorf(miny);
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    return y >> 4;
}

/* Opaque-layer stable section pack matching wl_ensure_mesh (not translucent). */
static void test_pack_opaque(const CrVertex *src, int n, CrVertex *dst) {
    enum { SECS = 16 };
    int sec_n[SECS];
    memset(sec_n, 0, sizeof(sec_n));
    for (int i = 0; i < n; i += 6)
        sec_n[test_quad_section(src + i)] += 6;
    int cursor[SECS], next = 0;
    for (int sec = 0; sec < SECS; ++sec) {
        cursor[sec] = next;
        next += sec_n[sec];
    }
    for (int i = 0; i < n; i += 6) {
        int sec = test_quad_section(src + i);
        memcpy(dst + cursor[sec], src + i, 6 * sizeof(CrVertex));
        cursor[sec] += 6;
    }
}

/* ---------------- (A) regression lock ---------------- */
static void test_regression_lock(void) {
    printf("== (A) regression lock vs chunkscene_init ==\n");

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

    /* Column frustum: same kept/culled set as the frozen scene. */
    GmMeshView mv;
    gm_world_mesh_view(w, &cam, W, H, &mv);

    CHECK(mv.n_kept == s.n_kept, "n_kept matches chunkscene");
    CHECK(mv.n_culled == s.n_culled, "n_culled matches chunkscene");
    printf("   kept=%d culled=%d (chunkscene kept=%d culled=%d)\n",
           mv.n_kept, mv.n_culled, s.n_kept, s.n_culled);

    /* Full-column gather (no section frustum / occlusion): nverts must match
     * the frozen concat. This is the mesher-equivalence lock. */
    const int max_draws = 512;
    GmChunkDraw *draws = (GmChunkDraw *)calloc((size_t)max_draws, sizeof(GmChunkDraw));
    CHECK(draws != NULL, "mesh_chunks draw table alloc");
    int full_nverts[4] = {0, 0, 0, 0};
    int nch = 0;
    if (draws)
        nch = gm_world_mesh_chunks(w, &cam, W, H, draws, max_draws, full_nverts);
    CHECK(nch == s.n_kept, "mesh_chunks kept count matches chunkscene");
    printf("   mesh_chunks kept=%d\n", nch);

    for (int l = 0; l < 4; ++l) {
        int neq = (full_nverts[l] == s.nverts[l]);
        printf("   layer %d: fullcol nverts=%-7d scene nverts=%-7d  %s\n",
               l, full_nverts[l], s.nverts[l], neq ? "COUNT-MATCH" : "COUNT-DIFF");
        CHECK(neq, "full-column per-layer nverts match chunkscene");
        /* mesh_view only removes geometry (W16 section frustum + W19 occlusion). */
        CHECK(mv.nverts[l] <= full_nverts[l],
              "mesh_view nverts is a subset of full-column total");
        printf("            view nverts=%-7d (filter subset of fullcol)\n",
               mv.nverts[l]);
    }

    /* Per kept chunk: live slab == section-packed worldmc_mesh_chunk(scene).
     * Walk the frozen frustum order (identical to mesh_chunks / chunkscene). */
    int chunk_byte_ok = 0, chunk_byte_fail = 0;
    {
        const int R = SCN_VIEW_RADIUS;
        const int ccx = (int)floorf(cam.pos.x) >> 4;
        const int ccz = (int)floorf(cam.pos.z) >> 4;
        CrMat4 proj = cr_perspective(cam.fov_deg, (float)W / (float)H,
                                     cam.znear, cam.zfar);
        CrMat4 view = cr_look_yaw_pitch(cam.pos, cam.yaw, cam.pitch);
        float planes[6][4];
        cr_frustum_extract(proj.m, view.m, planes);

        for (int cx = ccx - R; cx <= ccx + R; ++cx) {
            for (int cz = ccz - R; cz <= ccz + R; ++cz) {
                double minx = (double)(cx * 16), maxx = (double)(cx * 16 + 16);
                double minz = (double)(cz * 16), maxz = (double)(cz * 16 + 16);
                if (!cr_aabb_in_frustum(planes, minx, SCN_AABB_Y_MIN, minz,
                                        maxx, SCN_AABB_Y_MAX, maxz))
                    continue;

                CrChunkMeshMC sm;
                worldmc_mesh_chunk(s.world, cx, cz, &sm);
                int builds = 0;
                const CrChunkMeshMC *lm = gm_world__cached_mesh(w, cx, cz, &builds);
                if (!lm) {
                    printf("   FAIL: live mesh missing for chunk (%d,%d)\n", cx, cz);
                    fails++;
                    chunk_byte_fail++;
                    worldmc_free_mesh(&sm);
                    continue;
                }
                for (int l = 0; l < 4; ++l) {
                    if (lm->nverts[l] != sm.nverts[l]) {
                        printf("   FAIL: chunk (%d,%d) L%d nverts live=%d scene=%d\n",
                               cx, cz, l, lm->nverts[l], sm.nverts[l]);
                        fails++;
                        chunk_byte_fail++;
                        continue;
                    }
                    if (sm.nverts[l] == 0) { chunk_byte_ok++; continue; }
                    int beq = 0;
                    if (l == CR_LAYER_TRANSLUCENT) {
                        beq = memcmp(lm->verts[l], sm.verts[l],
                                     (size_t)sm.nverts[l] * sizeof(CrVertex)) == 0;
                    } else {
                        CrVertex *packed = (CrVertex *)malloc(
                            (size_t)sm.nverts[l] * sizeof(CrVertex));
                        if (!packed) {
                            printf("   FAIL: pack alloc chunk (%d,%d) L%d\n", cx, cz, l);
                            fails++;
                            chunk_byte_fail++;
                            continue;
                        }
                        test_pack_opaque(sm.verts[l], sm.nverts[l], packed);
                        beq = memcmp(lm->verts[l], packed,
                                     (size_t)sm.nverts[l] * sizeof(CrVertex)) == 0;
                        free(packed);
                    }
                    if (beq) chunk_byte_ok++;
                    else {
                        printf("   FAIL: chunk (%d,%d) L%d bytes differ after "
                               "section pack\n", cx, cz, l);
                        fails++;
                        chunk_byte_fail++;
                    }
                }
                worldmc_free_mesh(&sm);
            }
        }
    }
    printf("   per-chunk mesh bytes: %d ok, %d fail\n", chunk_byte_ok, chunk_byte_fail);
    CHECK(chunk_byte_fail == 0, "every kept chunk live mesh == packed scene mesh");

    free(draws);
    gm_world_destroy(w);
    chunkscene_free(&s);
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

/* Capture pad must match capture_ui_entities_driver.py:87-111, not a 16x16
 * origin square. light.c:390-392 superflat biome=1 (plains). */
static void test_ui_entities_capture_pad(void) {
    printf("== ui_entities capture pad ==\n");
    GmWorld *w = gm_world_create_type(0, 1);
    CHECK(w != NULL, "superflat seed 0");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 2);
    ui_entities_place_pad(w);
    CHECK(gm_world_block(w, 2, 4, 6) == BLK_STONE, "pad (2,4,6) stone");
    CHECK(gm_world_block(w, 14, 4, 18) == BLK_STONE, "pad (14,4,18) stone");
    CHECK(gm_world_block(w, 8, 4, 8) == BLK_STONE, "pad under camera stone");
    CHECK(gm_world_block(w, 8, 5, 8) == BLK_AIR, "air above pad y=5");
    CHECK(gm_world_block(w, 8, 11, 8) == BLK_AIR, "air above pad y=11");
    CHECK(gm_world_block(w, 0, 4, 0) == BLK_AIR,
          "outside pad (0,4,0) is air, not the old 0..15 stone");
    CHECK(gm_world_block(w, 0, 3, 0) == BLK_GRASS, "superflat grass outside pad");
    CHECK(gm_world_block(w, 8, 4, 5) == BLK_AIR, "z=5 is before pad z=6");
    CHECK(gm_world_block(w, 8, 4, 19) == BLK_AIR, "z=19 is after pad z=18");
    CHECK(gm_world_block(w, 8, 3, 19) == BLK_GRASS, "horizon grass beyond pad");
    CHECK(gm_world_block(w, 10, 5, 11) == BLK_STONE, "stone dig target");
    CHECK(gm_world_block(w, 11, 5, 11) == BLK_GRASS, "grass dig target");
    CHECK(gm_world_biome(w, 8, 8) == 1, "plains biome id 1");
    gm_world_destroy(w);
}

/* Capture dragon shelf must match capture_ui_entities_driver.py:381-399.
 * Superflat layers under the shelf stay (reset type=flat seed=0). */
static void test_ui_entities_dragon_platform(void) {
    printf("== ui_entities dragon platform ==\n");
    GmWorld *w = gm_world_create_type(0, 1);
    CHECK(w != NULL, "superflat seed 0");
    if (!w) return;
    gm_world_ensure(w, 0, -3, 4);
    ui_entities_place_dragon_platform(w);
    CHECK(gm_world_block(w, -8, 60, -50) == BLK_END_STONE,
          "platform corner (-8,60,-50) end_stone");
    CHECK(gm_world_block(w, 8, 60, 20) == BLK_END_STONE,
          "platform corner (8,60,20) end_stone");
    CHECK(gm_world_block(w, 0, 60, 0) == BLK_END_STONE,
          "platform under dragon (0,60,0) end_stone");
    CHECK(gm_world_block(w, -9, 60, 0) == BLK_AIR,
          "x=-9 is outside platform x[-8,8]");
    CHECK(gm_world_block(w, 0, 60, -51) == BLK_AIR,
          "z=-51 is outside platform z[-50,20]");
    CHECK(gm_world_block(w, 0, 60, 21) == BLK_AIR,
          "z=21 is outside platform z[-50,20]");
    CHECK(gm_world_block(w, 0, 3, 0) == BLK_GRASS,
          "superflat grass under platform");
    CHECK(gm_world_block(w, 0, 1, 0) == BLK_DIRT &&
          gm_world_block(w, 0, 2, 0) == BLK_DIRT,
          "superflat dirt y1-2 under platform");
    CHECK(gm_world_block(w, 0, 0, 0) == BLK_BEDROCK,
          "superflat bedrock under platform");
    CHECK(gm_world_biome(w, 0, 0) == 1, "plains biome id 1");
    gm_world_destroy(w);
}

int main(void) {
    test_regression_lock();
    test_dirty_cache();
    test_fill_window();
    test_state_namespace();
    test_superflat();
    test_ui_entities_capture_pad();
    test_ui_entities_dragon_platform();

    if (fails == 0) { printf("\nALL PASS\n"); return 0; }
    printf("\n%d CHECK(S) FAILED\n", fails);
    return 1;
}
