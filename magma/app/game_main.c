/* app/game_main.c - magma_game: PLAY the verified blaze simulation inside the
 * magma software rasterizer. Keyboard/mouse -> the verified player_survival.h
 * physics/break/place/vitals kernels -> a live view-distance blaze world meshed by
 * the MC-faithful mesher (pixel-matched to real MC ~39/ch) -> our C rasterizer ->
 * window. HUD + mobs on top. NO OpenGL in the render path.
 *
 * Seam design (see game/game.h): the verified psv_* kernels address an ORIGIN-CENTERED
 * PSV_DIM x PSV_DIM chunk window, so the player is ticked in a FLOATING-ORIGIN local
 * frame. Each frame we keep the player inside the CENTER chunk of the window: offset
 * (ox,oz) = playerChunk*16, local pos = worldpos - offset, refill the window from the
 * live world, tick, apply block edits back to the world, derive the camera + HUD.
 *
 * The deliberately narrow launch surface is parsed by game/config.c and specified
 * in PRODUCT.md. `--help` and `--print-config` require no SDL/display initialization.
 */
#include "core/types.h"
#include "game/game.h"
#include "game/entity_render.h"
#include "game/config.h"
#include "game/sky.h"
#include "game/underwater.h"
#include "game/caps.h"
#include "game/hand.h"
#include "game/hud.h"
#include "game/timer.h"   /* Timer.java port: 20 TPS accumulator + renderPartialTicks */
#include "game/live_sim.h" /* minimal live entities + plant plot */
#include "game/player_ctl.h"
#include "game/particles_live.h"
#include "game/runtime.h"
#include "game/screen.h"
#include "game/script.h"
#include "game/rl_mode.h"
#include "game/frame_capture.h"  /* gm_frame_lightmap_fill: shared updateLightmap LUT */
#include "game/view.h"
#include "game/overlay.h"        /* selection outline + dig crack decal geometry */
#include "game/sel_box.h"        /* vanilla per-block selection bounding boxes */
#include "game/item_render.h"    /* dropped-item mini blocks + flat sprites */
#include "container_click.h"
#include "items_core.h"
#include "assets/blockmodels.h"
#include "game/block_registry.h"   /* vanilla state -> particle model key */

/* blaze: PsvPlayer / Chunk / McSinTable + the verified init helpers. */
#include "player_survival.h"
#include "player_vitals.h"   /* verified vanilla vitals (PvStats, pv_init) */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "world/mesh_mc.h"
#include "world/lightmap.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD   ((float)(M_PI / 180.0))
#define MAX_EDITS 8

/* ALLOCATE-ONCE: the screen-tri scratch cap comes from caps (magma.conf), resolved
 * once at startup into this file-static so render_layer/render_world can bound
 * cr_transform without threading it through every call. */
static int g_max_tris = CR_DEF_MAX_TRIS;

/* CUDA raster wiring (only compiled into magma_game_cuda, built by `make
 * game-cuda` with -DMAGMA_CUDA and cuda/raster_cuda.o + -lcudart). The default
 * `make game` is pure gcc: MAGMA_CUDA is undefined, g_use_cuda is a const 0,
 * and none of the cr_raster_cuda_* symbols are referenced, so nothing changes. */
#ifdef MAGMA_CUDA
extern void cr_raster_cuda_pre(int w, int h, int max_tris);
extern void cr_raster_cuda_into(CrFramebuffer *fb, const CrScreenTri *tris,
                                int ntris, const CrShadeCtx *sh);
extern void cr_raster_cuda_frame_begin(const CrFramebuffer *fb);
extern void cr_raster_cuda_frame_end(CrFramebuffer *fb);
extern void cr_raster_cuda_sky(const GmSkyCtx *sc, const float *basis,
                               int W, int H);
extern void cr_raster_cuda_post(void);
static int g_use_cuda = 0;   /* runtime switch: --backend cuda / legacy --cuda */
#else
static const int g_use_cuda __attribute__((unused)) = 0;  /* CPU build: referenced only under MAGMA_CUDA */
#endif

/* Metal raster wiring (only compiled into magma_game_metal, built by `make
 * game-metal` with -DMAGMA_METAL and metal/raster_metal_host.o; macOS only).
 * Mirrors the CUDA seam above 1:1: same signatures, s/cuda/metal/. The default
 * `make game` leaves MAGMA_METAL undefined and references no metal symbol. */
#ifdef MAGMA_METAL
extern void cr_raster_metal_pre(int w, int h, int max_tris);
extern void cr_raster_metal_into(CrFramebuffer *fb, const CrScreenTri *tris,
                                 int ntris, const CrShadeCtx *sh);
extern void cr_raster_metal_frame_begin(const CrFramebuffer *fb);
extern void cr_raster_metal_frame_end(CrFramebuffer *fb);
extern void cr_raster_metal_sky(const GmSkyCtx *sc, const float *basis,
                                int W, int H);
extern void cr_raster_metal_post(void);
static int g_use_metal = 0;  /* runtime switch: --backend metal */
#endif

/* Raster one concatenated per-layer vertex buffer once (normal backface cull;
 * unlike the GL-parity candidate we do NOT double windings). The CUDA path
 * (cr_raster_cuda_into, alloc-once) is bit-identical to cr_raster_cpu. */
static int render_layer(CrFramebuffer *fb, const CrCamera *cam,
                         const CrVertex *verts, int nv,
                         CrScreenTri *tris, const CrShadeCtx *sh) {
    if (nv < 3) return 0;
    int ntris = cr_transform(verts, nv, NULL, 0, cam, fb->w, fb->h, tris, g_max_tris);
    if (ntris > 0) {
#ifdef MAGMA_CUDA
        if (g_use_cuda) cr_raster_cuda_into(fb, tris, ntris, sh);
        else            cr_raster_cpu(fb, tris, ntris, sh);
#elif defined(MAGMA_METAL)
        if (g_use_metal) cr_raster_metal_into(fb, tris, ntris, sh);
        else             cr_raster_cpu(fb, tris, ntris, sh);
#else
        cr_raster_cpu(fb, tris, ntris, sh);
#endif
    }
    return ntris;
}

/* Draw the 4 terrain layers in MC order with the state that produced the rung-3/4
 * match (see verify/chunk_candidate.c): alpha test on cutouts, mips on
 * CUTOUT_MIPPED, blend on TRANSLUCENT. MC terrain fog (setupFog(0): linear 96->128,
 * updateFogColor noon color; see game/sky.h) DEFAULT ON; MAGMA_FOG=0 disables. */
static int render_world(CrFramebuffer *fb, const CrCamera *cam, const GmMeshView *mv,
                         const CrTexture *atlas, CrScreenTri *tris, float time_of_day,
                         const GmUnderwater *uw, const CrRgba *lm) {
    int    fon = gm_terrain_fog_enabled();
    CrRgba fog = gm_terrain_fog_color(time_of_day);
    const float fst = GM_TERRAIN_FOG_START, fen = GM_TERRAIN_FOG_END;
    /* Designated, NOT positional - see terrain_shades() in frame_capture.c:
     * the old positional forms predate CrShadeCtx.alpha_ref and shifted the
     * fog flag into alpha_ref, killing every CUTOUT texel. */
#define TSH(at, ly, bl) { .atlas = atlas, .fog_color = fog,                  \
                          .fog_start = fst, .fog_end = fen,                  \
                          .alpha_test = (at), .enable_fog = fon,             \
                          .layer = (ly), .blend = (bl) }
    CrShadeCtx sh_solid = TSH(0, CR_LAYER_SOLID,         0);
    /* mips off: oracle profiles pin mipmapLevels:0 */
    CrShadeCtx sh_cmip  = TSH(1, CR_LAYER_CUTOUT_MIPPED, 0);
    sh_cmip.depth_lequal = 1;  /* coplanar grass_side_overlay (GL_LEQUAL) */
    CrShadeCtx sh_cut   = TSH(1, CR_LAYER_CUTOUT,        0);
    CrShadeCtx sh_trans = TSH(0, CR_LAYER_TRANSLUCENT,   1);
#undef TSH
    /* lightmap mode (worldmc): meshes carry raw sky/blk lightmap coords, so
     * every terrain ctx must bind the frame's updateLightmap texels exactly
     * like frame_capture.c terrain_shades(); a NULL binding shades garbage. */
    sh_solid.lightmap = lm;
    sh_cmip.lightmap  = lm;
    sh_cut.lightmap   = lm;
    sh_trans.lightmap = lm;
    if (uw && uw->fluid) {
        /* setupFog fluid branch: GL_EXP over every terrain layer. */
        CrShadeCtx *all[4] = { &sh_solid, &sh_cmip, &sh_cut, &sh_trans };
        for (int i = 0; i < 4; ++i) {
            all[i]->fog_color = uw->fog_rgba;
            all[i]->fog_exp_density = uw->density;
        }
    }
    int t = 0;
    t += render_layer(fb, cam, mv->verts[0], mv->nverts[0], tris, &sh_solid);
    t += render_layer(fb, cam, mv->verts[1], mv->nverts[1], tris, &sh_cmip);
    t += render_layer(fb, cam, mv->verts[2], mv->nverts[2], tris, &sh_cut);
    t += render_layer(fb, cam, mv->verts[3], mv->nverts[3], tris, &sh_trans);
    return t;
}

/* Camera from the player view. player yaw/pitch are MC degrees; game/view.h owns
 * the pixel-verified conversion (magma_yaw = 180 - mc_yaw, magma_pitch =
 * -mc_pitch). The old (mc_yaw - 180) form agreed at spawn yaw 180 but X-mirrored
 * the view at every other yaw, so walking stopped tracking the look direction
 * once the player turned. */
static CrCamera cam_from_view(const GmPlayerView *pv, int fb_w, int fb_h) {
    CrCamera c;
    c.pos.x = pv->x;
    c.pos.y = pv->y + pv->eye_height;
    c.pos.z = pv->z;
    c.yaw   = gm_view_cam_yaw_rad(pv->yaw);
    c.pitch = gm_view_cam_pitch_rad(pv->pitch);
    c.fov_deg = 70.0f;
    c.aspect  = (float)fb_w / (float)fb_h;
    c.znear   = 0.05f;
    /* EntityRenderer.setupCameraTransform: far = RD*16 * sqrt(2). */
    c.zfar    = GM_TERRAIN_ZFAR;
    c.hurt_yaw_deg = pv->hurt_yaw;
    c.hurt_roll_deg = gm_view_hurt_roll_deg(pv->hurt_time, pv->max_hurt_time);
    return c;
}

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    size_t n = (size_t)fb->w * fb->h;
    unsigned char *rgb = (unsigned char *)malloc(n * 3);
    if (!rgb) { fclose(f); return -1; }
    for (size_t i = 0; i < n; ++i) {
        rgb[i*3+0] = fb->color[i].r; rgb[i*3+1] = fb->color[i].g; rgb[i*3+2] = fb->color[i].b;
    }
    size_t wrote = fwrite(rgb, 3, n, f);
    free(rgb); fclose(f);
    return wrote == n ? 0 : -1;
}

/* ---- MAGMA_BENCH: per-frame wall-clock decomposition (MEASUREMENT ONLY) ----
 * Env-gated, exactly like the MAGMA_STILL / MAGMA_TP measurement gates:
 *   MAGMA_BENCH=1            enable (off => zero clock reads, unchanged run)
 *   MAGMA_BENCH_CSV=path     per-frame CSV rows (microseconds)
 *   MAGMA_BENCH_WARMUP=N     frames excluded from the summary stats (default 120)
 * Every timestamp is guarded by bench_on; with the env unset the loop is
 * byte-for-byte the uninstrumented one (one extra predictable branch per stage
 * boundary). No simulation or rendering state is touched.
 *
 * Timestamp slots (stage = difference to the previous slot):
 *   0 frame start | 1 input done | 2 sim ticks done | 3 camera/uw done |
 *   4 sky done (clear+sky draw) | 5 cuda frame_begin done | 6 mesh_view done |
 *   7 terrain raster done | 8 overlay done | 9 entities done |
 *   10 cuda frame_end done | 11 hand+hud done | 12 present done
 * cuda_in/cuda_out are ~0 on the pure-CPU build. */
#define BM_TS 13
#define BM_STAGES 12
static int       g_bench_on = -1;
static long long g_bench_warm = 120;
static long long g_bench_ts[BM_TS];
static long long g_bench_sum[BM_STAGES];   /* post-warmup frames only */
static long long g_bench_max[BM_STAGES];   /* post-warmup frames only */
static long long g_bench_frames_rec = 0;
static long long g_bench_meas = 0;         /* recorded frames past warmup */
static long long *g_bench_totals = NULL;   /* allocate-once at startup */
static long long  g_bench_totals_cap = 0;
static FILE      *g_bench_csv = NULL;
static const char *const g_bench_names[BM_STAGES] = {
    "input", "sim", "view", "sky", "cuda_in", "mesh", "raster", "overlay",
    "ents", "cuda_out", "hud", "present"
};

static long long bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static void bench_init(int want_frames) {
    if (g_bench_on >= 0) return;
    g_bench_on = getenv("MAGMA_BENCH") != NULL;
    if (!g_bench_on) return;
    const char *wp = getenv("MAGMA_BENCH_WARMUP");
    if (wp) { long long w = atoll(wp); if (w >= 0) g_bench_warm = w; }
    long long cap = want_frames > 0 ? want_frames : 65536;
    g_bench_totals = (long long *)malloc((size_t)cap * sizeof(long long));
    g_bench_totals_cap = g_bench_totals ? cap : 0;
    const char *cp = getenv("MAGMA_BENCH_CSV");
    if (cp) {
        g_bench_csv = fopen(cp, "w");
        if (g_bench_csv)
            fprintf(g_bench_csv,
                "frame,nticks,ntris,total_us,input_us,sim_us,view_us,sky_us,"
                "cuda_in_us,mesh_us,raster_us,overlay_us,ents_us,cuda_out_us,"
                "hud_us,present_us\n");
    }
}

static void bench_stamp(int slot) {
    if (g_bench_on > 0) g_bench_ts[slot] = bench_now_ns();
}

static void bench_record(int frame, int nticks, int ntris) {
    if (g_bench_on <= 0) return;
    long long total = g_bench_ts[BM_TS - 1] - g_bench_ts[0];
    int meas = g_bench_frames_rec >= g_bench_warm;
    for (int s = 0; s < BM_STAGES; ++s) {
        long long d = g_bench_ts[s + 1] - g_bench_ts[s];
        if (meas) {
            g_bench_sum[s] += d;
            if (d > g_bench_max[s]) g_bench_max[s] = d;
        }
    }
    if (meas) g_bench_meas++;
    if (g_bench_totals && g_bench_frames_rec < g_bench_totals_cap)
        g_bench_totals[g_bench_frames_rec] = total;
    g_bench_frames_rec++;
    if (g_bench_csv) {
        fprintf(g_bench_csv, "%d,%d,%d,%.3f", frame, nticks, ntris,
                (double)total / 1000.0);
        for (int s = 0; s < BM_STAGES; ++s)
            fprintf(g_bench_csv, ",%.3f",
                    (double)(g_bench_ts[s + 1] - g_bench_ts[s]) / 1000.0);
        fputc('\n', g_bench_csv);
    }
}

static int bench_cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return (x > y) - (x < y);
}

static void bench_report(void) {
    if (g_bench_on <= 0) return;
    long long n = g_bench_frames_rec;
    long long m = g_bench_meas;   /* frames past warmup with full stats */
    if (n <= 0) return;
    long long warm = g_bench_warm;
    if (warm >= n) warm = 0;
    if (m <= 0) { m = n; warm = 0; }

    long long sum_all = 0;
    for (long long i = warm; i < n; ++i) sum_all += g_bench_totals[i];
    double mean_ms = (double)sum_all / (double)m / 1e6;

    /* percentiles over a sorted COPY (CSV order stays chronological). */
    long long *sorted = (long long *)malloc((size_t)m * sizeof(long long));
    double p50 = 0, p95 = 0, p99 = 0;
    if (sorted) {
        memcpy(sorted, g_bench_totals + warm, (size_t)m * sizeof(long long));
        qsort(sorted, (size_t)m, sizeof(long long), bench_cmp_ll);
        p50 = (double)sorted[m / 2] / 1e6;
        p95 = (double)sorted[(m * 95) / 100] / 1e6;
        p99 = (double)sorted[(m * 99) / 100] / 1e6;
    }
    fprintf(stderr,
        "[bench] frames=%lld warmup=%lld measured=%lld\n"
        "[bench] frame ms: mean %.3f p50 %.3f p95 %.3f p99 %.3f\n"
        "[bench] fps: mean %.2f p50 %.2f p95 %.2f p99 %.2f\n",
        n, warm, m,
        mean_ms, p50, p95, p99,
        1000.0 / mean_ms, 1000.0 / p50, 1000.0 / p95, 1000.0 / p99);
    for (int s = 0; s < BM_STAGES; ++s)
        fprintf(stderr, "[bench] stage %-8s mean %.3f ms  max %.3f ms (measured frames)\n",
                g_bench_names[s],
                (double)g_bench_sum[s] / (double)m / 1e6,
                (double)g_bench_max[s] / 1e6);
    free(sorted);
    if (g_bench_csv) fclose(g_bench_csv);
    free(g_bench_totals);
    g_bench_totals = NULL;
    g_bench_on = 0;
}

int main(int argc, char **argv) {
    GmConfig cfg;
    char cfg_err[256];
    if (gm_config_parse(&cfg, argc, argv, cfg_err, sizeof cfg_err) != 0) {
        fprintf(stderr, "error: %s\n", cfg_err);
        gm_config_print_usage(stderr, argv[0]);
        return 2;
    }
    if (cfg.show_help) {
        gm_config_print_usage(stdout, argv[0]);
        return 0;
    }
    if (cfg.print_config) {
        gm_config_print(stdout, &cfg);
        return 0;
    }

#ifdef MAGMA_CUDA
    const int cuda_compiled = 1;
#else
    const int cuda_compiled = 0;
#endif
#ifdef MAGMA_METAL
    const int metal_compiled = 1;
#else
    const int metal_compiled = 0;
#endif
    if (gm_config_validate_runtime(&cfg, cuda_compiled, metal_compiled, cfg_err, sizeof cfg_err) != 0) {
        fprintf(stderr, "error: %s\n", cfg_err);
        return 2;
    }

    long long   seed = cfg.seed;
    int         fb_w = cfg.width, fb_h = cfg.height;
    float       sens = cfg.sensitivity;
    int         want_frames = cfg.frames;
    int         kill_frame  = cfg.kill_frame;
    const char *ppm_path = cfg.ppm_path;
    bench_init(want_frames);

#ifdef MAGMA_CUDA
    g_use_cuda = cfg.backend == GM_BACKEND_CUDA;
#endif
#ifdef MAGMA_METAL
    g_use_metal = cfg.backend == GM_BACKEND_METAL;
#endif

    /* Transitional bridge until view distance is carried through GmWorldConfig.
     * It is still sourced from the strict argv config, never a hidden user setting. */
    {
        char view_distance[16];
        snprintf(view_distance, sizeof view_distance, "%d", cfg.view_distance);
        setenv("MAGMA_VIEW_RADIUS", view_distance, 1);
    }
    fprintf(stderr, "[config] ");
    gm_config_print(stderr, &cfg);

    /* --- ALLOCATE-ONCE caps: load magma.conf (env override) BEFORE any pool alloc,
     * so the whole pre-allocation (here + gm_world_create's toroidal pools) is a pure
     * function of the effective caps computed before the window opens. --- */
    cr_caps_load(getenv("MAGMA_CONF"));
    /* Shade-time lightmap (time-of-day terrain lighting). Game binaries opt
     * in; MAGMA_LEGACY_LIGHTMAP=1 restores the noon-baked scalar path. */
    worldmc_set_lightmap_mode(!getenv("MAGMA_LEGACY_LIGHTMAP"));
    const CrCaps *caps = cr_caps();
    if (cfg.view_distance > caps->view_radius) {
        fprintf(stderr, "error: requested view distance %d exceeds configured pool cap %d\n",
                cfg.view_distance, caps->view_radius);
        return 2;
    }
    g_max_tris = caps->max_tris;
    const int ent_max_verts = caps->ent_max_verts;

    if (cfg.rl) return gm_rl_run(&cfg);
    if (cfg.headless) return gm_script_run(&cfg);

    /* --- framebuffer + scratch (all sized from caps, allocated once here) --- */
    CrFramebuffer fb; cr_fb_alloc(&fb, fb_w, fb_h);
    CrScreenTri *tris = (CrScreenTri *)malloc((size_t)caps->max_tris * sizeof(CrScreenTri));
    CrVertex    *ent_verts = (CrVertex *)malloc((size_t)ent_max_verts * sizeof(CrVertex));
    if (!fb.color || !tris || !ent_verts) { fprintf(stderr, "alloc failed\n"); return 1; }

#ifdef MAGMA_CUDA
    /* ALLOCATE-ONCE: cudaMalloc the device framebuffer + tri buffer + shade-ctx
     * ONCE here, sized from caps (fb w*h, max_tris). Every frame only memcpys
     * up/down and launches; no per-frame cudaMalloc. Freed at exit. */
    if (g_use_cuda) cr_raster_cuda_pre(fb_w, fb_h, caps->max_tris);
#elif defined(MAGMA_METAL)
    /* ALLOCATE-ONCE: same contract as the CUDA pre, on the Metal device. */
    if (g_use_metal) cr_raster_metal_pre(fb_w, fb_h, caps->max_tris);
#endif

    /* One owned simulation state and one authoritative transition. The macros keep
     * the rendering code readable while making every gameplay field part of runtime. */
    GmRuntime runtime;
    char runtime_err[256];
    if (!gm_runtime_init(&runtime, &cfg, runtime_err, sizeof runtime_err)) {
        fprintf(stderr, "error: %s\n", runtime_err);
        return 1;
    }
#define world   (runtime.world)
#define win     (runtime.window)
#define st      (runtime.sin_table)
#define pl      (runtime.player)
#define vitals  (runtime.vitals)
#define g_clock (runtime.clock)
#define live    (runtime.entities)
#define ccx     (runtime.ccx)
#define ccz     (runtime.ccz)
#define ox      (runtime.ox)
#define oz      (runtime.oz)
#define g_dead  (runtime.dead)
#define g_deaths (runtime.deaths)
    int surface = gm_world_surface_y(world, 8, 8);
    if (getenv("MAGMA_FIXTURES")) gm_live_init(&live, seed, surface);

    /* Deterministic mobs ringing spawn for windowed-path render checks (the
     * gates never draw this loop; MAGMA_FIXTURES-style measurement hook). */
    if (getenv("MAGMA_MOB_DEMO")) {
        static const int demo_types[4] = { GM_MOB_COW, EW_TYPE_ZOMBIE,
                                           GM_MOB_SHEEP, GM_MOB_PIG };
        static const int demo_off[4][2] = { {6,0}, {0,6}, {-6,0}, {0,-6} };
        for (int i = 0; i < 4; ++i) {
            int mx = 8 + demo_off[i][0], mz = 8 + demo_off[i][1];
            int my = gm_world_surface_y(world, mx, mz) + 1;
            gm_mobs_spawn(&runtime.mobs, demo_types[i],
                          (double)mx + 0.5, (double)my, (double)mz + 0.5);
        }
    }

    /* Headless inventory demo: seed stone stack and exercise slotClick via the
     * SAME gm_player_inv_click path the live input loop uses (Q / shift+hotbar). */
    if (getenv("MAGMA_INV_DEMO")) {
        isr_set_stack(&pl.inv, 0, ic_mk(1, 10, 0));
        pl.inv.current_item = 0;
        gm_player_cursor_set(ic_empty());
        /* PICKUP slot 0 (left) -> cursor 10 stone, slot empty */
        gm_player_inv_click((struct PsvPlayer *)&pl, 0, 0, CC_CLICK_PICKUP);
        /* PICKUP slot 1 (left) -> place into slot 1 */
        gm_player_inv_click((struct PsvPlayer *)&pl, 1, 0, CC_CLICK_PICKUP);
        ICStack s1 = isr_get_stack(&pl.inv, 1);
        ICStack cur = gm_player_cursor();
        fprintf(stderr, "[inv_demo] slot1 item=%d count=%d cursor_empty=%d\n",
                s1.item, s1.count, (cur.item == 0 || cur.count <= 0));
    }

    CrTexture atlas = gm_world_atlas(world);
    GmParticlesLive live_particles;
    gm_particles_live_init(&live_particles,
        (uint64_t)seed ^ UINT64_C(0x7061727469636c65));
    const int particle_demo = getenv("MAGMA_PARTICLE_DEMO") != NULL;
    gm_input_reset();
    gm_hud_init();

    CrWindow *cwin = cr_window_open(fb_w, fb_h, "magma - game");
    if (!cwin) { fprintf(stderr, "cr_window_open failed\n"); return 1; }

    const CrRgba sky = {135, 206, 235, 255};
    int frame = 0, running = 1;

    /* ---- 20 TPS tick accumulator + render interpolation (Timer.java port) ----
     * INTERACTIVE path only: each frame gm_timer_update turns wall time into
     * elapsed_ticks whole sim ticks (cap 10) + render_partial_ticks, so game time
     * runs at real speed regardless of frame rate. The HEADLESS --frames path keeps
     * the exact original semantics: 1 loop iteration == 1 tick, no wall clock,
     * partial_ticks pinned to 1.0 (render the CURRENT state, byte-identical to HEAD).
     *
     * prev_* is the Entity.prevPosX/prevRotationYaw analogue: the player state
     * BEFORE the last executed tick, snapshotted in WORLD coords (immune to the
     * floating-origin recenter) right before each gm_player_tick. The camera lerps
     * prev->cur by partial_ticks. */
    GmTimer timer; gm_timer_init(&timer, 20.0f);
    double prev_x = pl.ent.posX + ox, prev_y = pl.ent.posY, prev_z = pl.ent.posZ + oz;
    float  prev_yaw = pl.yaw, prev_pitch = pl.pitch;
    /* Delta/edge inputs (mouse look, click edges, hotbar) must apply EXACTLY once
     * per poll, not once per tick of a catch-up batch, and must not be lost on a
     * 0-tick frame: accumulate them here, consume on the first tick executed.
     * (Matches MC: mouse turn + click handling are per-frame/per-event, while held
     * movement keys feed movementInput every tick.) */
    float pend_dyaw = 0.0f, pend_dpitch = 0.0f;
    int   pend_break = 0, pend_place = 0, pend_hotbar = -1;
    /* vanilla Minecraft.rightClickDelayTimer: a held right button re-fires the
     * place/use action every 4 ticks after the initial click edge. */
    int   use_repeat_delay = 0;
    int   pend_inv_click = 0, pend_inv_slot = 0, pend_inv_button = 0, pend_inv_type = 0;
    const int timer_dbg = getenv("MAGMA_TIMER_DEBUG") != NULL;

    /* ---- interactive container screen (game/screen.c) ----
     * E (or a table/furnace use) opens it; E/ESC closes it. While open the mouse
     * owns a cursor, movement/look/attack are suppressed, and every click becomes
     * a GmAction.inv_click through the SAME authoritative tick as headless play. */
    int screen_open = 0, prev_e = 0, prev_q_screen = 0;
    int mouse_x = fb_w / 2, mouse_y = fb_h / 2;

    /* first-person hand animation state (game/hand.c). bob_phase advances while
     * the player moves (subtle viewmodel bob); swing_prog runs 0->1 over a short
     * window on a left-click attack, mirroring MC's swingProgress. */
    float hand_bob = 0.0f;
    int   swing_ticks = 0;            /* frames remaining in the current swing */
    int   prev_atk = 0;               /* attack held last tick (clickMouse edge) */
    GmHudState hud_state = {0};
    const int SWING_LEN = 6;         /* MC swings the arm over ~6 ticks */

    /* MEASUREMENT hooks (env-gated, no effect on a normal run):
     *  - ALLOCTRACK: per-frame allocation tripwire via trace/alloctrack.so
     *    (weak symbol; NULL unless the .so is LD_PRELOADed).
     *  - MAGMA_DEBUG_CAPS: per-frame draw-buffer maxima for the fixed-pool sizing. */
    const int at_on   = getenv("ALLOCTRACK") != NULL;
    const int caps_on = getenv("MAGMA_DEBUG_CAPS") != NULL;
    extern void alloctrack_frame(int) __attribute__((weak));
    int cap_max_kept = 0, cap_max_tris = 0;
    int cap_max_layer[4] = {0,0,0,0}, cap_max_total = 0;

    while (running) {
        bench_stamp(0);
        if (at_on && alloctrack_frame) alloctrack_frame(frame);
        /* ---- input -> action ---- */
        GmAction act;
        if (want_frames >= 0) {
            memset(&act, 0, sizeof(act));
            /* MAGMA_STILL: measurement gate - stand still (no walk) so a run can
             * hold steady-state without crossing chunk boundaries. Default walks. */
            act.forward = getenv("MAGMA_STILL") ? 0.0f : 1.0f;
            /* MAGMA_JUMP: hold jump every tick (demo runs hop over terrain instead
             * of wedging against a 1-block step). MAGMA_YAWRATE=<deg/frame>: slow
             * scripted turn so a demo pans across the world. */
            if (getenv("MAGMA_JUMP")) act.jump = 1;
            { const char *yr = getenv("MAGMA_YAWRATE"); if (yr) act.dyaw = (float)atof(yr); }
            act.hotbar_sel = -1;
        } else {
            CrInput in; cr_window_poll(cwin, &in);
            if (in.quit) { running = 0; break; }
            act = gm_input_map(&in, sens);
            mouse_x = in.mouse_x; mouse_y = in.mouse_y;

            /* E edge toggles the container screen; ESC closes it. Closing is a
             * real container close: grid/cursor return to the inventory. */
            int e_edge = in.key_e && !prev_e;
            prev_e = in.key_e;
            if (screen_open && (e_edge || in.key_esc)) {
                screen_open = 0;
                gm_container_close(&runtime);
                runtime.container = 0; runtime.active_furnace = -1;
                cr_window_capture_enable(cwin, 1);
            } else if (!screen_open && e_edge) {
                screen_open = 1;
                cr_window_capture_enable(cwin, 0);
            }

            if (runtime.dead) {
                /* GuiGameOver: freeze survival input; left-click hits Respawn /
                 * Title Screen after enableButtonsTimer (20 ticks). */
                GmAction dact; memset(&dact, 0, sizeof dact);
                dact.hotbar_sel = -1;
                if (in.click_left) {
                    int btn = gm_hud_death_button_at(
                        fb_w, fb_h, mouse_x, mouse_y,
                        gm_hud_death_buttons_enabled(runtime.death_screen_ticks));
                    if (btn >= 0) {
                        dact.death_click = 1;
                        dact.death_button = btn;
                    }
                }
                act = dact;
            } else if (screen_open) {
                /* GUI mode: suppress movement/look/attack/use; clicks map to
                 * Container.slotClick ids via the vanilla-layout hit test. */
                GmAction gui; memset(&gui, 0, sizeof gui);
                gui.hotbar_sel = -1;
                int slot = gm_screen_slot_at(runtime.container, fb_w, fb_h,
                                             mouse_x, mouse_y);
                int q_edge = in.key_q && !prev_q_screen;
                if (in.click_left || in.click_right || q_edge) {
                    int click_ok = 1, button = 0, type = CC_CLICK_PICKUP;
                    if (q_edge) {
                        if (slot >= 0) { type = CC_CLICK_THROW; button = in.key_ctrl ? 1 : 0; }
                        else click_ok = 0;
                    } else if (slot == GMC_OUTSIDE) {
                        button = in.click_right ? 1 : 0;
                    } else if (slot >= 0) {
                        if (in.key_shift) type = CC_CLICK_QUICK_MOVE;
                        button = in.click_right ? 1 : 0;
                    } else {
                        click_ok = 0; /* panel background */
                    }
                    if (click_ok) {
                        gui.inv_click = 1; gui.inv_slot = slot;
                        gui.inv_button = button; gui.inv_type = type;
                    }
                }
                act = gui;
            }
            prev_q_screen = in.key_q;
        }

        /* MEASUREMENT harness (env-gated, no effect on a normal run): MAGMA_TP=<step>
         * teleports the player -Z by <step> blocks/frame, snapping Y to the surface, so a
         * scripted run traverses many chunks in a straight line without getting stuck on
         * terrain or dying. Used only to profile the streaming allocation path. */
        if (want_frames >= 0) {
            /* MAGMA_SPAWN_SURFACE: snap the frame-0 spawn onto the terrain surface so a
             * demo run does not open with a 47-block death drop from the fixed y=120 spawn. */
            if (frame == 0 && getenv("MAGMA_SPAWN_SURFACE")) {
                double twx = pl.ent.posX + ox, twz = pl.ent.posZ + oz;
                int sy = gm_world_surface_y(world, (int)floor(twx), (int)floor(twz));
                pl.ent.posY = (double)sy + 1.0;
                pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
                pl.ent.motionX = pl.ent.motionY = pl.ent.motionZ = 0.0;
                pl.fall_distance = 0.0f;
            }
            const char *tp = getenv("MAGMA_TP");
            if (tp) {
                double step = atof(tp);
                pl.ent.posZ -= step;
                double twx = pl.ent.posX + ox, twz = pl.ent.posZ + oz;
                int sy = gm_world_surface_y(world, (int)floor(twx), (int)floor(twz));
                pl.ent.posY = (double)sy + 1.0;
                pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
                pl.ent.motionX = pl.ent.motionY = pl.ent.motionZ = 0.0;
                pl.fall_distance = 0.0f;
            }
        }

        /* ---- how many sim ticks this frame: wall-clock accumulator (interactive)
         * or exactly 1 (headless --frames, no wall clock, deterministic) ---- */
        bench_stamp(1);
        int   nticks = 1;
        float partial_ticks = 1.0f;
        if (want_frames < 0) {
            long long dbg_prev_ms = timer.last_sync_sys_clock;
            gm_timer_update(&timer);
            nticks        = timer.elapsed_ticks;
            partial_ticks = timer.render_partial_ticks;
            /* bank this frame's delta/edge inputs; the first tick below consumes
             * them (and a 0-tick frame carries them to the next frame's ticks). */
            pend_dyaw   += act.dyaw;
            pend_dpitch += act.dpitch;
            pend_break  |= act.do_break;
            pend_place  |= act.do_place;
            if (act.hotbar_sel >= 0) pend_hotbar = act.hotbar_sel;
            if (act.inv_click) {
                pend_inv_click  = 1;
                pend_inv_slot   = act.inv_slot;
                pend_inv_button = act.inv_button;
                pend_inv_type   = act.inv_type;
            }
            if (timer_dbg)
                fprintf(stderr, "[timer] frame %d elapsed_ticks %d partial %.3f dt_ms %lld\n",
                        frame, nticks, partial_ticks,
                        timer.last_sync_sys_clock - dbg_prev_ms);
        }

        for (int t = 0; t < nticks; ++t) {

        /* Entity prev* snapshot: player state BEFORE this tick, in WORLD coords so
         * the floating-origin recenter below cannot skew it. After the batch this
         * holds the state before the LAST tick (Entity.prevPosX semantics). */
        prev_x = pl.ent.posX + ox; prev_y = pl.ent.posY; prev_z = pl.ent.posZ + oz;
        prev_yaw = pl.yaw; prev_pitch = pl.pitch;

        /* per-tick action: held movement repeats every tick of a catch-up batch;
         * banked deltas/edges fire exactly once, on the first tick. Headless uses
         * the frame action verbatim (1 frame == 1 tick, unchanged semantics). */
        GmAction tact = act;
        if (want_frames < 0) {
            if (t == 0) {
                tact.dyaw = pend_dyaw;      tact.dpitch = pend_dpitch;
                tact.do_break = pend_break; tact.do_place = pend_place;
                tact.hotbar_sel = pend_hotbar;
                tact.inv_click = pend_inv_click; tact.inv_slot = pend_inv_slot;
                tact.inv_button = pend_inv_button; tact.inv_type = pend_inv_type;
                pend_dyaw = pend_dpitch = 0.0f;
                pend_break = pend_place = 0; pend_hotbar = -1;
                pend_inv_click = 0;
            } else {
                tact.dyaw = tact.dpitch = 0.0f;
                tact.do_break = tact.do_place = 0;
                tact.hotbar_sel = -1;
                tact.inv_click = 0;
                tact.death_click = 0;
            }
            /* held-use repeat (Minecraft.rightClickDelayTimer semantics): the
             * click edge fires immediately and arms the 4-tick timer; while the
             * button stays held each expiry fires another place/use. */
            if (!tact.use) use_repeat_delay = 0;
            else if (tact.do_place) use_repeat_delay = 4;
            else if (--use_repeat_delay <= 0) { tact.do_place = 1; use_repeat_delay = 4; }
        }

        /* Retain the aimed block before gm_runtime_tick may replace it with
         * air. Hit particles use the block's selected AABB, which is its
         * ordinary state bounding box for the vanilla blocks in this port. */
        int phit = 0, phx = 0, phy = 0, phz = 0, pface = -1;
        int pworldx = 0, pworldz = 0;
        int pblock = 0, pmeta = 0;
        float pbounds[6] = {0};
        if (tact.attack) {
            PsvPlayer aim_pl = pl;
            aim_pl.yaw += tact.dyaw;
            aim_pl.pitch += tact.dpitch;
            if (aim_pl.pitch > 89.0f) aim_pl.pitch = 89.0f;
            if (aim_pl.pitch < -89.0f) aim_pl.pitch = -89.0f;
            int pax, pay, paz;
            if (gm_raycast_sel_reach(win, &st, &aim_pl, PSV_REACH,
                                     &phx, &phy, &phz,
                                     &pax, &pay, &paz) >= 0) {
                phit = 1;
                pworldx = phx + ox;
                pworldz = phz + oz;
                pblock = gm_world_block(world, pworldx, phy, pworldz);
                pmeta = gm_world_meta(world, pworldx, phy, pworldz) & 15;
                gm_sel_box_at(win, phx, phy, phz, pbounds);
                if (pax < phx) pface = 4;
                else if (pax > phx) pface = 5;
                else if (pay < phy) pface = 0;
                else if (pay > phy) pface = 1;
                else if (paz < phz) pface = 2;
                else pface = 3;
            }
        }

        if (particle_demo && runtime.tick == 20) {
            PsvPlayer demo_pl = pl;
            demo_pl.pitch = 10.0f;
            int hx, hy, hz, ax, ay, az;
            int have_surface = gm_raycast_sel_reach(
                win, &st, &demo_pl, 24.0,
                &hx, &hy, &hz, &ax, &ay, &az) >= 0;
            int pwx = have_surface ? hx + ox : (int)floor(pl.ent.posX + (double)ox);
            int pwz = have_surface ? hz + oz : (int)floor(pl.ent.posZ + (double)oz) - 3;
            int pwy = have_surface ? hy : gm_world_surface_y(world, pwx, pwz) - 1;
            int id = gm_world_block(world, pwx, pwy, pwz);
            int meta = gm_world_meta(world, pwx, pwy, pwz) & 15;
            int model = gm_state_to_model_key(gm_pack_state(id, meta));
            CrLightmapRgb plm = cr_lightmap_rgb(
                runtime.dimension,
                gm_world_sky_light(world, pwx, pwy + 1, pwz),
                gm_world_block_light(world, pwx, pwy + 1, pwz),
                cr_dimension_sun_brightness(runtime.dimension), 0.f, 0.f);
            gm_particles_live_seed(&live_particles,
                UINT64_C(0x5041525449434c45));
            int spawned = gm_particles_live_spawn_destroy(&live_particles,
                pwx, pwy, pwz, model, plm.r, plm.g, plm.b);
            fprintf(stderr, "[particle_demo] tick=%lld block=%d,%d,%d spawned=%d\n",
                    runtime.tick, pwx, pwy, pwz, spawned);
        }

        /* debug/test hook: force a lethal state at a chosen frame. */
        if (kill_frame >= 0 && frame == kill_frame) { vitals.health = 0.0f; pl.health = 0.0f; }
        gm_runtime_tick(&runtime, tact);
        if (phit && pblock != 0) {
            int pwx = pworldx, pwz = pworldz;
            int model = gm_state_to_model_key(gm_pack_state(pblock, pmeta));
            int block_now = gm_world_block(world, pwx, phy, pwz);
            int meta_now = gm_world_meta(world, pwx, phy, pwz) & 15;
            if (block_now != pblock || meta_now != pmeta) {
                CrLightmapRgb plm = cr_lightmap_rgb(
                    runtime.dimension,
                    gm_world_sky_light(world, pwx, phy, pwz),
                    gm_world_block_light(world, pwx, phy, pwz),
                    cr_dimension_sun_brightness(runtime.dimension), 0.f, 0.f);
                gm_particles_live_spawn_destroy(&live_particles,
                    pwx, phy, pwz, model, plm.r, plm.g, plm.b);
            }
            if (gm_player_dig_swing()) {
                int lx = pwx, ly = phy, lz = pwz;
                if (pface == 0) ly--; else if (pface == 1) ly++;
                else if (pface == 2) lz--; else if (pface == 3) lz++;
                else if (pface == 4) lx--; else if (pface == 5) lx++;
                CrLightmapRgb plm = cr_lightmap_rgb(
                    runtime.dimension,
                    gm_world_sky_light(world, lx, ly, lz),
                    gm_world_block_light(world, lx, ly, lz),
                    cr_dimension_sun_brightness(runtime.dimension), 0.f, 0.f);
                gm_particles_live_spawn_hit(&live_particles,
                    pwx, phy, pwz, model, pface, pbounds,
                    plm.r, plm.g, plm.b);
            }
        }
        gm_particles_live_tick(&live_particles, win, ox, oz);
        /* Episode ends on victory or Title Screen from GuiGameOver. Death
         * itself holds on the death screen (Respawn continues the run). */
        if (runtime.won || runtime.quit_to_title) running = 0;

        }   /* end tick batch (for t < nticks) */
        bench_stamp(2);

        /* Using a crafting table / furnace opened a runtime container: show it. */
        if (want_frames < 0 && runtime.container != 0 && !screen_open) {
            screen_open = 1;
            cr_window_capture_enable(cwin, 0);
        }

        GmPlayerView pv; gm_runtime_view(&runtime, &pv);
        gm_hud_state_step(&hud_state, &pv, runtime.tick);
        /* camera view: headless keeps partial_ticks pinned at 1.0 and renders the
         * CURRENT state (byte-identical to the pre-timer loop); interactive lerps
         * prev->cur by renderPartialTicks, exactly Entity prevPos + (pos-prev)*pt
         * as the EntityRenderer camera setup does. HUD still reads the live pv. */
        GmPlayerView cpv = pv;
        if (want_frames < 0) {
            cpv.x     = (float)(prev_x + ((pl.ent.posX + (double)ox) - prev_x) * (double)partial_ticks);
            cpv.y     = (float)(prev_y + ( pl.ent.posY               - prev_y) * (double)partial_ticks);
            cpv.z     = (float)(prev_z + ((pl.ent.posZ + (double)oz) - prev_z) * (double)partial_ticks);
            cpv.yaw   = prev_yaw   + (pl.yaw   - prev_yaw)   * partial_ticks;
            cpv.pitch = prev_pitch + (pl.pitch - prev_pitch) * partial_ticks;
        }
        CrCamera cam = cam_from_view(&cpv, fb_w, fb_h);
        /* eye-in-fluid state (fog / FOV / overlay - game/underwater.h). The
         * interactive path uses the steady-state fogColor1 (no per-tick
         * smoother history; visual-only). */
        GmUnderwater uw;
        {
            float c1 = gm_uw_fog_c1_seed(world, runtime.dimension,
                                         cpv.x, cpv.y, cpv.z);
            gm_uw_eval(world, runtime.dimension, &cpv, c1, &uw);
        }
        cam.fov_deg *= uw.fov_scale;   /* getFOVModifier: 60/70 in water */
        bm_atlas_set_animation_tick(g_clock.total_time);
        gm_sky_set_eye_height(cpv.eye_height > 0.01f ? cpv.eye_height : 1.62f);
        gm_sky_set_fluid_fog(uw.fluid ? 1 : 0, uw.fog01, uw.density);
        bench_stamp(3);

        /* ---- render: real MC sky (gradient + sun/moon + clouds), terrain, mobs, HUD ----
         * gm_sky_draw fills every far-depth pixel, replacing the flat cr_fb_clear; it also
         * clears depth to far so the terrain z-tests correctly on top. time_of_day 0.25 = noon. */
        cr_fb_clear(&fb, uw.fluid ? uw.fog_rgba : sky);  /* clears depth=far; color is overwritten by the sky */
        long long day_tick=g_clock.world_time%24000LL;
        if(day_tick<0)day_tick+=24000LL;
        float day=(float)day_tick/24000.0f;
        /* GPU sky (windowed CUDA, overworld): the host per-pixel gm_sky_draw is
         * ~35 ms at 1080p - co-dominant with raster. Skip it and run k_sky on
         * the uploaded cleared fb instead (same kernel+ctx the frame_capture
         * path already uses; day frames bit-identical, night star pixels can
         * differ by device sinf in hash21 <=0.012%/frame). MAGMA_CPU_SKY
         * forces the host loop for A/B. */
        int gpu_sky = 0;
#ifdef MAGMA_CUDA
        gpu_sky = g_use_cuda && runtime.dimension == 0 && !getenv("MAGMA_CPU_SKY");
#elif defined(MAGMA_METAL)
        gpu_sky = g_use_metal && runtime.dimension == 0 && !getenv("MAGMA_CPU_SKY");
#endif
        if (!gpu_sky) gm_sky_draw(&fb, &cam, day);
        bench_stamp(4);
        /* CUDA: upload the (sky- or clear-filled) fb ONCE, keep it device-resident
         * across the 4 terrain layers + entities, download ONCE after (frame_end,
         * before HUD). */
#ifdef MAGMA_CUDA
        if (g_use_cuda) cr_raster_cuda_frame_begin(&fb);
        if (gpu_sky) {
            GmSkyCtx sc; float bas[11];
            gm_sky_frame_args(&cam, day, &sc, bas);
            cr_raster_cuda_sky(&sc, bas, fb_w, fb_h);
        }
#elif defined(MAGMA_METAL)
        if (g_use_metal) cr_raster_metal_frame_begin(&fb);
        if (gpu_sky) {
            GmSkyCtx sc; float bas[11];
            gm_sky_frame_args(&cam, day, &sc, bas);
            cr_raster_metal_sky(&sc, bas, fb_w, fb_h);
        }
#endif
        bench_stamp(5);
        GmMeshView mv; gm_world_mesh_view(world, &cam, fb_w, fb_h, &mv);
        bench_stamp(6);
        /* frame lightmap texels (overworld lightmap mode only, like
         * build_lightmap_lut in frame_capture.c) */
        static CrRgba lm_lut[256];
        const CrRgba *lm = NULL;
        if (worldmc_lightmap_mode() && runtime.dimension == 0) {
            gm_frame_lightmap_fill(&st, g_clock.world_time, lm_lut);
            lm = lm_lut;
        }
        int ntris = render_world(&fb, &cam, &mv, &atlas, tris, day, &uw, lm);
        bench_stamp(7);

        /* ---- targeted-block selection outline + dig crack decal (windowed) ----
         * Selection: SRC_ALPHA/ONE_MINUS_SRC_ALPHA (blend=1). Crack:
         * DST_COLOR/SRC_COLOR multiply-2x (blend=2). Separate passes -
         * vanilla RenderGlobal draws them with different blend state
         * (drawSelectionBox vs preRenderDamagedBlocks). Combined
         * gm_overlay_emit is legacy-tests-only. */
        if (want_frames < 0 && !screen_open && !g_dead
            && !getenv("MAGMA_NO_OVERLAY")) {
            static CrVertex sel_ov[GM_OVERLAY_MAX_VERTS];
            static CrVertex crack_ov[GM_OVERLAY_MAX_VERTS];
            int hx = 0, hy = 0, hz = 0, ax, ay, az;
            int have_sel = gm_raycast_sel(win, &st, &pl,
                                          &hx, &hy, &hz, &ax, &ay, &az) >= 0;
            float selb[6];
            if (have_sel) gm_sel_box_at(win, hx, hy, hz, selb);
            if (have_sel) {
                int ns = gm_overlay_emit_sel(sel_ov, GM_OVERLAY_MAX_VERTS,
                                             hx + ox, hy, hz + oz, selb,
                                             cam.pos.x, cam.pos.y, cam.pos.z);
                if (ns > 0) {
                    CrShadeCtx osh = {0};
                    osh.atlas = &atlas;
                    osh.fog_color = sky;
                    osh.alpha_test = 0;
                    osh.enable_fog = 0;
                    osh.layer = CR_LAYER_TRANSLUCENT;
                    osh.blend = 1;
                    osh.depth_lequal = 1;
                    render_layer(&fb, &cam, sel_ov, ns, tris, &osh);
                }
            }
            int dx = 0, dy2 = 0, dz = 0; float dmg = 0.0f;
            int have_dig = gm_player_dig_state(&dx, &dy2, &dz, &dmg);
            if (have_dig && dmg > 0.0f && !getenv("MAGMA_NO_CRACK")) {
                /* BlockRendererDispatcher.renderBlockDamage re-renders the full
                 * block model with the destroy sprite, not only the raycast face. */
                int nc = gm_overlay_emit_crack(crack_ov, GM_OVERLAY_MAX_VERTS,
                                               dx + ox, dy2, dz + oz, dmg, -1);
                if (nc > 0) {
                    CrShadeCtx csh = {0};
                    csh.atlas = &atlas;
                    csh.fog_color = sky;
                    /* alphaFunc(GL_GREATER, 0.1F): discard a <= ~26. Our cutout
                     * threshold is 128 - tighter than vanilla, keeps only solid
                     * crack strokes (destroy_stage bg is a≈1 white). */
                    csh.alpha_test = 1;
                    csh.enable_fog = 0;
                    csh.layer = CR_LAYER_CUTOUT;
                    csh.blend = 2;           /* DST_COLOR, SRC_COLOR → 2*src*dst */
                    csh.depth_lequal = 1;
                    render_layer(&fb, &cam, crack_ov, nc, tris, &csh);
                }
            }
        }

        if (caps_on) {
            int total = 0;
            for (int l = 0; l < 4; ++l) {
                if (mv.nverts[l] > cap_max_layer[l]) cap_max_layer[l] = mv.nverts[l];
                total += mv.nverts[l];
            }
            if (total       > cap_max_total) cap_max_total = total;
            if (mv.n_kept   > cap_max_kept)  cap_max_kept  = mv.n_kept;
            if (ntris       > cap_max_tris)  cap_max_tris  = ntris;
            fprintf(stderr,
                "[caps] frame %d kept=%d culled=%d drawverts[S/CM/C/T]=%d/%d/%d/%d total=%d screen_tris=%d\n",
                frame, mv.n_kept, mv.n_culled, mv.nverts[0], mv.nverts[1],
                mv.nverts[2], mv.nverts[3], total, ntris);
        }
        bench_stamp(8);

        /* live entity store (items/hostiles markers) -> mesh pass */
        GmEntityView ents[GM_LIVE_MAX];
        int nents = gm_dragon_fill_views(&runtime.dragon, ents, GM_LIVE_MAX);
        nents += gm_mobs_fill_views(&runtime.mobs, ents+nents, GM_LIVE_MAX-nents);
        int n_proj0 = nents;
        nents += gm_runtime_projectile_views(&runtime, ents+nents, GM_LIVE_MAX-nents);
        {
            int ptypes[GM_RUNTIME_PROJECTILES], npt = 0;
            for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
                if (runtime.projectiles[i].active)
                    ptypes[npt++] = runtime.projectiles[i].type;
            gm_entity_patch_large_fireballs(ptypes, npt, ents + n_proj0,
                                            nents - n_proj0);
        }
        nents += gm_live_fill_views(&live, ents + nents, GM_LIVE_MAX - nents);
        if (runtime.dragon.initialized) {
            int dt = runtime.dragon.state.arena.dragon.death_ticks;
            for (int i = 0; i < nents; ++i)
                if (ents[i].type == GM_ENTITY_DRAGON && ents[i].death_ticks <= 0 && dt > 0)
                    ents[i].death_ticks = dt;
        }
        /* per-entity light fields: without this every entity renders
         * unlit-white (lm_lit==0), exactly like the capture path pre-fill. */
        gm_frame_entities_light(ents, nents, world, runtime.dimension, lm);
        if (nents > 0) {
            int nv = gm_entities_emit(ents, nents, ent_verts, ent_max_verts);
            gm_particles_dragon_latch(runtime.tick, ents, nents);
            nv += gm_particles_emit(ents, nents, pv.yaw, pv.pitch,
                                    ent_verts + nv, ent_max_verts - nv);
            CrTexture eatlas = gm_entity_atlas();
            CrRgba fog = sky;
            CrShadeCtx esh = {0};
            esh.atlas = &eatlas; esh.fog_color = fog;
            esh.alpha_test = 1; esh.layer = CR_LAYER_CUTOUT;
            esh.alpha_mask = 1;
            esh.lightmap = lm;
            gm_entity_dissolve_mask(&esh.mask_u_off, &esh.mask_v_off);
            render_layer(&fb, &cam, ent_verts, nv, tris, &esh);
            /* RenderXPOrb: SRC_ALPHA + alpha 128 (not cutout thr 0.5). */
            {
                int nx = gm_xp_orbs_emit(ents, nents, pv.yaw, pv.pitch,
                                         ent_verts, ent_max_verts);
                if (nx > 0) {
                    CrShadeCtx xp = {0};
                    xp.atlas = &eatlas; xp.fog_color = fog;
                    xp.alpha_test = 1; xp.alpha_ref = 0.1f;
                    xp.layer = CR_LAYER_TRANSLUCENT; xp.blend = 1;
                    xp.lightmap = lm;
                    render_layer(&fb, &cam, ent_verts, nx, tris, &xp);
                }
            }
            /* LayerSlimeGel: living alphaFunc(GL_GREATER, 0.1) + blend depth write. */
            nv = gm_slime_gel_emit(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx gel = {0};
                gel.atlas = &eatlas; gel.fog_color = fog;
                gel.alpha_test = 1; gel.alpha_ref = 0.1f;
                gel.layer = CR_LAYER_TRANSLUCENT;
                gel.blend = 4;
                render_layer(&fb, &cam, ent_verts, nv, tris, &gel);
            }
            /* LayerEnderDragonDeath: untextured additive smooth rays. */
            nv = gm_dragon_death_rays_emit(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx rays = {0};
                rays.atlas = &eatlas; rays.fog_color = fog;
                rays.untextured = 1; rays.blend = 3;
                rays.layer = CR_LAYER_TRANSLUCENT;
                rays.lightmap = lm;
                render_layer(&fb, &cam, ent_verts, nv, tris, &rays);
            }
            /* RenderDragon.renderCrystalBeams: end-crystal healing beam. */
            nv = gm_crystal_beams_emit(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx bm = {0};
                bm.atlas = &eatlas; bm.fog_color = fog;
                bm.alpha_test = 1; bm.alpha_ref = 0.1f;
                bm.layer = CR_LAYER_CUTOUT;
                bm.lightmap = lm;
                render_layer(&fb, &cam, ent_verts, nv, tris, &bm);
            }
            /* dropped items, second pass: block cubes/plants on the TERRAIN
             * atlas, then non-block items on the item atlas. ent_verts is
             * reusable: render_layer consumed the mob verts above. */
            nv = gm_items_emit(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx ish = {0};
                ish.atlas = &atlas; ish.fog_color = fog;
                ish.alpha_test = 1; ish.layer = CR_LAYER_CUTOUT;
                render_layer(&fb, &cam, ent_verts, nv, tris, &ish);
            }
            nv = gm_items_emit_flat(ents, nents, ent_verts, ent_max_verts);
            nv += gm_items_emit_billboard(ents, nents, pv.yaw, pv.pitch,
                                          ent_verts + nv,
                                          ent_max_verts - nv);
            if (nv > 0) {
                CrTexture iatlas = gm_item_atlas();
                CrShadeCtx fsh = {0};
                fsh.atlas = &iatlas; fsh.fog_color = fog;
                fsh.alpha_test = 1; fsh.layer = CR_LAYER_CUTOUT;
                render_layer(&fb, &cam, ent_verts, nv, tris, &fsh);
            }
            gm_entity_prep_large_fireball_fire(ents, nents);
            nv = gm_small_fireball_fire_emit(ents, nents, pv.yaw,
                                             ent_verts, ent_max_verts);
            gm_entity_restore_large_fireball_types(ents, nents);
            nv += gm_entity_fire_emit(ents, nents, pv.yaw,
                                      ent_verts + nv, ent_max_verts - nv);
            if (nv > 0) {
                CrShadeCtx fire_sh = {0};
                fire_sh.atlas = &atlas; fire_sh.fog_color = fog;
                fire_sh.alpha_test = 1; fire_sh.layer = CR_LAYER_CUTOUT;
                render_layer(&fb, &cam, ent_verts, nv, tris, &fire_sh);
            }
        }
        {
            int nv = gm_particles_live_emit(&live_particles, partial_ticks,
                                             cpv.yaw, cpv.pitch,
                                             ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx dig = {0};
                dig.atlas = &atlas;
                dig.fog_color = sky;
                dig.alpha_test = 1;
                dig.layer = CR_LAYER_CUTOUT;
                render_layer(&fb, &cam, ent_verts, nv, tris, &dig);
            }
        }

        bench_stamp(9);
#ifdef MAGMA_CUDA
        if (g_use_cuda) cr_raster_cuda_frame_end(&fb);   /* device fb -> host, once */
#elif defined(MAGMA_METAL)
        if (g_use_metal) cr_raster_metal_frame_end(&fb); /* device fb -> host, once */
#endif
        bench_stamp(10);
        /* ---- first-person hand (over the world, before the 2D HUD) ---- */
        {
            float mv_mag = fabsf(act.forward) + fabsf(act.strafe);
            /* advance walk bob per SIM TICK (nticks==1 on the headless path,
             * byte-identical): per-frame advance raced at window frame rate */
            if (mv_mag > 0.01f) hand_bob += 0.30f * (float)nticks;
            /* EntityLivingBase.swingArm restart rule: the clickMouse press edge
             * and every tick sendClickBlockToController damages a block. Vanilla
             * restarts once swingProgressInt >= end/2, so a held dig loops over
             * the first half rather than replaying all six ticks or freezing. */
            {
                int atk = act.attack || act.do_break;
                int swing_arm = (atk && !prev_atk) || gm_player_dig_swing();
                prev_atk = atk;
                if (swing_arm && swing_ticks <= SWING_LEN / 2)
                    swing_ticks = SWING_LEN;
            }
            float swing = swing_ticks > 0
                ? (float)(SWING_LEN - swing_ticks) / (float)SWING_LEN : 0.0f;
            gm_hand_set_swing(swing);
            /* swingProgressInt advances per SIM TICK (20 Hz), not per rendered
             * frame: at uncapped window fps the old per-frame decrement ran
             * the 6-tick swing in a few dozen ms. nticks==1 headless. */
            swing_ticks -= nticks;
            if (swing_ticks < 0) swing_ticks = 0;
            /* Viewmodel environment: eye-block combined light folded into the
             * tint (no frame lightmap texture on this path), the eye-in-water
             * 60/70 fov, and the item-light anchor rotation. */
            {
                int hx = (int)floorf(cpv.x);
                int hy = (int)floorf(cpv.y + cpv.eye_height);
                int hz = (int)floorf(cpv.z);
                int hsky = gm_world_sky_light(world, hx, hy, hz);
                int hblk = gm_world_block_light(world, hx, hy, hz);
                if (lm) {
                    /* lightmap mode: raw coords through the frame LUT, exactly
                     * like the capture path (frame_capture.c hand env). */
                    gm_hand_set_env(lm, (float)hsky, (float)hblk, 1.f, 1.f, 1.f,
                                    uw.fov_scale, cpv.yaw, cpv.pitch);
                } else {
                    CrLightmapRgb hc3 = cr_lightmap_rgb(runtime.dimension, hsky, hblk,
                        cr_dimension_sun_brightness(runtime.dimension), 0.f, 0.f);
                    gm_hand_set_env(0, 15.f, 0.f, hc3.r, hc3.g, hc3.b,
                                    uw.fov_scale, cpv.yaw, cpv.pitch);
                }
            }
            /* MAGMA_NO_HAND: measurement gate - skip the first-person arm (used
             * to pixel-diff hand vs no-hand against the MC golden). Default draws. */
            if (!pv.dead && !getenv("MAGMA_NO_HAND"))
                gm_hand_draw(&fb, &pv, hand_bob);
            /* ItemRenderer.renderOverlays + GuiIngame portal: block, water,
             * fire, then portal, then HUD. Use is filled on GmPlayerView. */
            if (!pv.dead)
                gm_overlay_block_in_hand_live(&fb, &atlas, world, &cpv);
            if (uw.overlay && !pv.dead)
                gm_uw_overlay_draw(&fb, &cpv, uw.brightness, cam.fov_deg);
            if (pv.fire && !pv.creative && !pv.dead)
                gm_hand_fire_overlay_draw(&fb, &atlas, uw.fov_scale);
        }
        if (pv.portal > 0.0f)
            gm_overlay_portal_screen(&fb, &atlas, pv.portal);
        if (pv.dead)
            gm_hud_set_pointer(mouse_x, mouse_y);
        gm_hud_draw(&fb, &pv);
        if (screen_open && !pv.dead)
            gm_screen_draw(&fb, &runtime, mouse_x, mouse_y);
        bench_stamp(11);
        cr_window_present(cwin, &fb);
        bench_stamp(12);
        bench_record(frame, nticks, ntris);

        /* MAGMA_DUMP_DIR: per-frame PPM dump for video encoding (headless demo). */
        if (want_frames >= 0) {
            const char *dd = getenv("MAGMA_DUMP_DIR");
            if (dd) {
                char fp[512];
                snprintf(fp, sizeof fp, "%s/frame_%05d.ppm", dd, frame);
                write_ppm(fp, &fb);
            }
        }
        /* headless: emit the health/food + death arc so a scripted run can be verified. */
        if (want_frames >= 0)
            fprintf(stderr,
                    "frame %d health %.2f food %.0f dead %d deaths %d slot0=%d pos %.1f,%.1f,%.1f\n",
                    frame, pv.health, pv.food, pv.dead, pv.deaths, pv.hotbar_counts[0],
                    pv.x, pv.y, pv.z);

        if (want_frames >= 0 && ++frame >= want_frames) running = 0;
    }

    bench_report();

    if (caps_on)
        fprintf(stderr,
            "[caps] SUMMARY sizeof(CrVertex)=%zu sizeof(CrScreenTri)=%zu MAX_TRIS=%d\n"
            "[caps] SUMMARY max_kept=%d max_screen_tris=%d\n"
            "[caps] SUMMARY max_drawverts[S/CM/C/T]=%d/%d/%d/%d max_total=%d\n",
            sizeof(CrVertex), sizeof(CrScreenTri), g_max_tris,
            cap_max_kept, cap_max_tris,
            cap_max_layer[0], cap_max_layer[1], cap_max_layer[2], cap_max_layer[3],
            cap_max_total);

    if (ppm_path) write_ppm(ppm_path, &fb);

    /* Live composition side-effect summary (entity motion + plant age + worldTime). */
    fprintf(stderr,
            "[live] ticks=%d worldTime=%lld plant_age=%d ent_moved=%d ent0_age=%d ent0_y=%.3f\n",
            live.ticks, (long long)g_clock.world_time, gm_live_plant_age(&live),
            gm_live_entity_moved(&live),
            live.ents[0].age, live.ents[0].y);

#ifdef MAGMA_CUDA
    if (g_use_cuda) cr_raster_cuda_post();
#elif defined(MAGMA_METAL)
    if (g_use_metal) cr_raster_metal_post();
#endif
    free(tris); free(ent_verts);
    gm_runtime_destroy(&runtime);
    cr_window_close(cwin);
    cr_fb_free(&fb);
    return 0;
}
