/* pose_scene.h - ARBITRARY-POSE view-distance scene for the game-verify harness.
 *
 * This is chunk_scene.h's view-distance mesher (worldmc_ensure + frustum-cull a
 * Chebyshev-radius neighbourhood, mesh the kept chunks into 4 per-layer CrVertex
 * arrays, expose the stitched atlas) GENERALIZED to a CALLER-SUPPLIED CrCamera
 * instead of chunk_scene.h's single FROZEN constant pose. Everything else (radius
 * 12, full-column AABB frustum test, no_cull escape hatch, znear/zfar) is
 * identical, so at the frozen pose posescene_init produces the exact same mesh and
 * camera as chunkscene_init -> the game-verify harness reproduces rung-4's numbers
 * for pose 0 (proving the arbitrary-pose path is wired correctly).
 *
 * Camera convention is magma's (core/math.c): yaw about +Y, pitch about +X,
 * forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)); yaw 0 looks
 * toward -Z, POSITIVE pitch looks UP. The caller converts an MC pose (yaw,pitch)
 * into this convention via magma_yaw = 180 - MC_yaw, magma_pitch = -MC_pitch
 * (see run_game_verify.sh). The scene is deliberately kept a header so it can be
 * dropped into a candidate exactly like chunk_scene.h.
 */
#ifndef MAGMA_VERIFY_POSE_SCENE_H
#define MAGMA_VERIFY_POSE_SCENE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "core/frustum.h"
#include "core/config.h"   /* cr_cfg()->no_cull */
#include "world/mesh_mc.h"
#include "world/populate_mc.h"
#include "game/caps.h"

/* VIEW-DISTANCE meshing radius in chunks (16 blocks). Pin to Java
 * renderDistance=8 (fast.yaml / options.txt) so hard-scene matches the oracle
 * frustum, not a longer C-only draw distance. Override: -DPSCN_VIEW_RADIUS=N. */
#ifndef PSCN_VIEW_RADIUS
#define PSCN_VIEW_RADIUS 8
#endif

/* Chunk AABB Y span for the frustum test. Full column [0,256] is conservative:
 * never culls a chunk whose terrain is inside the frustum (no holes). */
#define PSCN_AABB_Y_MIN 0.0
#define PSCN_AABB_Y_MAX 256.0

typedef struct {
    CrWorldMC *world;
    CrVertex  *verts[4];   /* per CrRenderLayer, concatenated over the kept chunks */
    int        nverts[4];
    CrTexture  atlas;      /* == worldmc_atlas() (level 0 + mip chain) */
    CrCamera   cam;        /* caller-supplied pose (aspect re-derived from fb dims) */
    int        center_cx, center_cz;
    int        view_radius;
    int        n_kept, n_culled;
} PoseScene;

/* floor-division of a block coord to its chunk coord (handles negatives). */
static inline int pscn__floordiv16(int a) { return a >> 4; }

/* Grow a per-layer vertex buffer and append n verts. */
static inline void pscn__append(CrVertex **dst, int *n, int *cap,
                                const CrVertex *src, int add) {
    if (add <= 0) return;
    if (*n + add > *cap) {
        int nc = *cap ? *cap : 4096;
        while (*n + add > nc) nc *= 2;
        *dst = (CrVertex *)realloc(*dst, (size_t)nc * sizeof(CrVertex));
        *cap = nc;
    }
    memcpy(*dst + *n, src, (size_t)add * sizeof(CrVertex));
    *n += add;
}

/* Append base (bx,bz) if not already in the flat list. */
static inline void pscn__bases_add(int **bases, int *n, int *cap, int bx, int bz) {
    for (int i = 0; i < *n; ++i)
        if ((*bases)[i * 2] == bx && (*bases)[i * 2 + 1] == bz) return;
    if (*n >= *cap) {
        *cap *= 2;
        *bases = (int *)realloc(*bases, (size_t)(*cap) * 2 * sizeof(int));
    }
    (*bases)[(*n) * 2] = bx;
    (*bases)[(*n) * 2 + 1] = bz;
    ++(*n);
}

/* CUMULATIVE populate prepare (the real lever): build owr windows in vanilla
 * populate order BEFORE light_ensure's nested gen_chunk order can create them
 * on demand with missing donors. Mirrors trace/world_dump.c --prep-list:
 *   1. spawn-square raster (MinecraftServer.initialWorldChunkLoad)
 *   2. remaining ensure-region bases by distance to spawn (PlayerChunkMap-ish)
 * Pool must hold every window at once (owr_d_min + owr_cells_max) or donors
 * get evicted and rebuild without cascade. Escape: MAGMA_NO_PREP=1.
 * Spawn: MAGMA_SPAWN_CX/CZ, else seed0 qrl_0 (2,11). Optional exact list:
 * MAGMA_PREP_LIST=path ("bcx bcz" per line, recorded genprobe order). */
static inline void pscn__prepare_populate(long long seed, int ccx, int ccz,
                                          int ensure_r) {
    if (getenv("MAGMA_NO_PREP")) {
        fprintf(stderr, "posescene: MAGMA_NO_PREP set, skipping cumulative prepare\n");
        return;
    }

    int scx = 0, scz = 0;
    const char *esx = getenv("MAGMA_SPAWN_CX");
    const char *esz = getenv("MAGMA_SPAWN_CZ");
    if (esx) scx = atoi(esx);
    if (esz) scz = atoi(esz);
    /* seed 0 hard-scene / qrl_0 known spawn (44,176) -> chunk (2,11) */
    if (!esx && !esz && seed == 0) { scx = 2; scz = 11; }

    int cap = 4096, nbases = 0;
    int *bases = (int *)malloc((size_t)cap * 2 * sizeof(int));
    if (!bases) return;

    const char *prepl = getenv("MAGMA_PREP_LIST");
    if (prepl) {
        FILE *pf = fopen(prepl, "r");
        if (pf) {
            int bx, bz;
            while (fscanf(pf, "%d %d", &bx, &bz) == 2)
                pscn__bases_add(&bases, &nbases, &cap, bx, bz);
            fclose(pf);
            fprintf(stderr, "posescene: prep list %s -> %d bases\n", prepl, nbases);
        } else {
            fprintf(stderr, "posescene: cannot open MAGMA_PREP_LIST=%s\n", prepl);
        }
    }

    if (!nbases) {
        /* Spawn square first (x-outer, z-inner), matching world_verify fallback. */
        for (int bx = scx - 12; bx < scx + 12; ++bx)
            for (int bz = scz - 12; bz < scz + 12; ++bz)
                pscn__bases_add(&bases, &nbases, &cap, bx, bz);

        /* Ensure-region bases (each chunk uses bases {cx-1,cx}x{cz-1,cz}). */
        int bx0 = ccx - ensure_r - 1, bx1 = ccx + ensure_r;
        int bz0 = ccz - ensure_r - 1, bz1 = ccz + ensure_r;
        int nextra = 0, extra_cap = 512;
        int *extra = (int *)malloc((size_t)extra_cap * 2 * sizeof(int));
        for (int bx = bx0; bx <= bx1; ++bx)
            for (int bz = bz0; bz <= bz1; ++bz) {
                int seen = 0;
                for (int i = 0; i < nbases; ++i)
                    if (bases[i * 2] == bx && bases[i * 2 + 1] == bz) { seen = 1; break; }
                if (seen) continue;
                if (nextra >= extra_cap) {
                    extra_cap *= 2;
                    extra = (int *)realloc(extra, (size_t)extra_cap * 2 * sizeof(int));
                }
                extra[nextra * 2] = bx;
                extra[nextra * 2 + 1] = bz;
                ++nextra;
            }
        /* Sort extras by dist^2 to spawn, then (bx,bz). Simple insertion. */
        for (int a = 1; a < nextra; ++a) {
            int kx = extra[a * 2], kz = extra[a * 2 + 1];
            long long kd = (long long)(kx - scx) * (kx - scx) +
                           (long long)(kz - scz) * (kz - scz);
            int b = a - 1;
            while (b >= 0) {
                int ox = extra[b * 2], oz = extra[b * 2 + 1];
                long long od = (long long)(ox - scx) * (ox - scx) +
                               (long long)(oz - scz) * (oz - scz);
                if (od < kd || (od == kd && (ox < kx || (ox == kx && oz < kz)))) break;
                extra[(b + 1) * 2] = ox;
                extra[(b + 1) * 2 + 1] = oz;
                --b;
            }
            extra[(b + 1) * 2] = kx;
            extra[(b + 1) * 2 + 1] = kz;
        }
        for (int i = 0; i < nextra; ++i)
            pscn__bases_add(&bases, &nbases, &cap, extra[i * 2], extra[i * 2 + 1]);
        free(extra);
        fprintf(stderr,
                "posescene: prepare order spawn(%d,%d)+ensure r=%d -> %d bases "
                "(%d outside spawn square)\n",
                scx, scz, ensure_r, nbases, nextra);
    }

    if (!nbases) { free(bases); return; }

    /* Span of prep bases + ensure window: no toroidal eviction mid-run. */
    int mnx = bases[0], mxx = bases[0], mnz = bases[1], mxz = bases[1];
    for (int i = 1; i < nbases; ++i) {
        int bx = bases[i * 2], bz = bases[i * 2 + 1];
        if (bx < mnx) mnx = bx; if (bx > mxx) mxx = bx;
        if (bz < mnz) mnz = bz; if (bz > mxz) mxz = bz;
    }
    int spanx = mxx - mnx + 1, spanz = mxz - mnz + 1;
    int span = spanx > spanz ? spanx : spanz;
    int owr_need = 2 * ensure_r + 4;
    if (owr_need < span + 3) owr_need = span + 3;
    /* Light pool must cover ensure; owr pool every prep window. Must run BEFORE
     * worldmc_create / first owr_pool_init. */
    if (ensure_r + 1 > PSCN_VIEW_RADIUS)
        cr_caps_override("view_radius", ensure_r + 1);
    cr_caps_override("owr_d_min", owr_need);
    cr_caps_override("owr_cells_max", 49152);

    popmc_prepare(seed, bases, nbases);
    fprintf(stderr, "posescene: prepared %d cumulative windows (%ld builds) owr_D_min=%d\n",
            nbases, popmc_window_builds(), owr_need);
    free(bases);
}

/* Build the scene around cam for the given world seed. W/H drive aspect + frustum. */
static inline void posescene_init_seed(PoseScene *s, const CrCamera *cam, int W, int H,
                                       long long seed) {
    memset(s, 0, sizeof(*s));

    s->cam = *cam;
    s->cam.aspect = (float)W / (float)H;   /* transform re-derives from fb dims */

    const int R = PSCN_VIEW_RADIUS;
    const int ccx = pscn__floordiv16((int)floorf(s->cam.pos.x));
    const int ccz = pscn__floordiv16((int)floorf(s->cam.pos.z));
    s->center_cx = ccx;
    s->center_cz = ccz;
    s->view_radius = R;

    /* REAL LEVER: cumulative prepare in spawn/load order BEFORE ensure's nested
     * gen_chunk order builds windows with empty donor sets. Caps override first. */
    pscn__prepare_populate(seed, ccx, ccz, R + 1);

    s->world = worldmc_create(seed);

    /* Generate + light every chunk in radius R plus a 1-chunk apron. */
    worldmc_ensure(s->world, ccx, ccz, R + 1);
    s->atlas = worldmc_atlas(s->world);

    /* Frustum planes from the SAME matrices cr_transform uses (proj from fb
     * aspect, view from the supplied pose). */
    CrMat4 proj = cr_perspective(s->cam.fov_deg, (float)W / (float)H,
                                 s->cam.znear, s->cam.zfar);
    CrMat4 view = cr_look_yaw_pitch(s->cam.pos, s->cam.yaw, s->cam.pitch);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);

    const int cull_off = cr_cfg()->no_cull;

    int cap[4] = {0, 0, 0, 0};
    for (int cx = ccx - R; cx <= ccx + R; ++cx) {
        for (int cz = ccz - R; cz <= ccz + R; ++cz) {
            double minx = (double)(cx * 16), maxx = (double)(cx * 16 + 16);
            double minz = (double)(cz * 16), maxz = (double)(cz * 16 + 16);
            int inside = cull_off ||
                cr_aabb_in_frustum(planes, minx, PSCN_AABB_Y_MIN, minz,
                                   maxx, PSCN_AABB_Y_MAX, maxz);
            if (!inside) { s->n_culled++; continue; }
            s->n_kept++;

            CrChunkMeshMC m;
            worldmc_mesh_chunk(s->world, cx, cz, &m);
            for (int l = 0; l < 4; ++l)
                pscn__append(&s->verts[l], &s->nverts[l], &cap[l],
                             m.verts[l], m.nverts[l]);
            worldmc_free_mesh(&m);
        }
    }
}

/* Seed-0 convenience (pose 0 / rung-4 parity). */
static inline void posescene_init(PoseScene *s, const CrCamera *cam, int W, int H) {
    posescene_init_seed(s, cam, W, H, 0);
}

static inline void posescene_free(PoseScene *s) {
    for (int l = 0; l < 4; ++l) free(s->verts[l]);
    worldmc_destroy(s->world);
    memset(s, 0, sizeof(*s));
}

/* Write an RGB PPM (P6), top-down (row 0 = top). */
static inline int pscn_write_ppm(const char *path, const unsigned char *rgb,
                                 int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * h * 3, f);
    fclose(f);
    return 0;
}

#endif /* MAGMA_VERIFY_POSE_SCENE_H */
