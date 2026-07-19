/* app/game_main.c - craster_game: PLAY the verified mc-sim simulation inside the
 * craster software rasterizer. Keyboard/mouse -> the verified player_survival.h
 * physics/break/place/vitals kernels -> a live view-distance mc-sim world meshed by
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
#include "game/config.h"
#include "game/sky.h"
#include "game/underwater.h"
#include "game/caps.h"
#include "game/hand.h"
#include "game/timer.h"   /* Timer.java port: 20 TPS accumulator + renderPartialTicks */
#include "game/live_sim.h" /* minimal live entities + plant plot */
#include "game/player_ctl.h"
#include "game/runtime.h"
#include "game/screen.h"
#include "game/script.h"
#include "game/rl_mode.h"
#include "game/view.h"
#include "game/overlay.h"        /* selection outline + dig crack decal geometry */
#include "game/sel_box.h"        /* vanilla per-block selection bounding boxes */
#include "game/item_render.h"    /* dropped-item mini blocks + flat sprites */
#include "container_click.h"
#include "items_core.h"

/* mc-sim: PsvPlayer / Chunk / McSinTable + the verified init helpers. */
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

/* ALLOCATE-ONCE: the screen-tri scratch cap comes from caps (craster.conf), resolved
 * once at startup into this file-static so render_layer/render_world can bound
 * cr_transform without threading it through every call. */
static int g_max_tris = CR_DEF_MAX_TRIS;

/* CUDA raster wiring (only compiled into craster_game_cuda, built by `make
 * game-cuda` with -DCRASTER_CUDA and cuda/raster_cuda.o + -lcudart). The default
 * `make game` is pure gcc: CRASTER_CUDA is undefined, g_use_cuda is a const 0,
 * and none of the cr_raster_cuda_* symbols are referenced, so nothing changes. */
#ifdef CRASTER_CUDA
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
static const int g_use_cuda __attribute__((unused)) = 0;  /* CPU build: referenced only under CRASTER_CUDA */
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
#ifdef CRASTER_CUDA
        if (g_use_cuda) cr_raster_cuda_into(fb, tris, ntris, sh);
        else            cr_raster_cpu(fb, tris, ntris, sh);
#else
        cr_raster_cpu(fb, tris, ntris, sh);
#endif
    }
    return ntris;
}

/* Draw the 4 terrain layers in MC order with the state that produced the rung-3/4
 * match (see raster/verify/chunk_candidate.c): alpha test on cutouts, mips on
 * CUTOUT_MIPPED, blend on TRANSLUCENT. MC terrain fog (setupFog(0): linear 96->128,
 * updateFogColor noon color; see game/sky.h) DEFAULT ON; CRASTER_FOG=0 disables. */
static int render_world(CrFramebuffer *fb, const CrCamera *cam, const GmMeshView *mv,
                         const CrTexture *atlas, CrScreenTri *tris, float time_of_day,
                         const GmUnderwater *uw) {
    int    fon = gm_terrain_fog_enabled();
    CrRgba fog = gm_terrain_fog_color(time_of_day);
    const float fst = GM_TERRAIN_FOG_START, fen = GM_TERRAIN_FOG_END;
    CrShadeCtx sh_solid = { atlas, fog, fst, fen, 0, fon, CR_LAYER_SOLID,         0, 0, 0.f };
    CrShadeCtx sh_cmip  = { atlas, fog, fst, fen, 1, fon, CR_LAYER_CUTOUT_MIPPED, 0, 0, 0.f }; /* mips off: oracle profiles pin mipmapLevels:0 */
    sh_cmip.depth_lequal = 1;  /* coplanar grass_side_overlay (GL_LEQUAL) */
    CrShadeCtx sh_cut   = { atlas, fog, fst, fen, 1, fon, CR_LAYER_CUTOUT,        0, 0, 0.f };
    CrShadeCtx sh_trans = { atlas, fog, fst, fen, 0, fon, CR_LAYER_TRANSLUCENT,   1, 0, 0.f };
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
 * the pixel-verified conversion (craster_yaw = 180 - mc_yaw, craster_pitch =
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
    c.zfar    = 600.0f;
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

/* ---- CRASTER_BENCH: per-frame wall-clock decomposition (MEASUREMENT ONLY) ----
 * Env-gated, exactly like the CRASTER_STILL / CRASTER_TP measurement gates:
 *   CRASTER_BENCH=1            enable (off => zero clock reads, unchanged run)
 *   CRASTER_BENCH_CSV=path     per-frame CSV rows (microseconds)
 *   CRASTER_BENCH_WARMUP=N     frames excluded from the summary stats (default 120)
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
    g_bench_on = getenv("CRASTER_BENCH") != NULL;
    if (!g_bench_on) return;
    const char *wp = getenv("CRASTER_BENCH_WARMUP");
    if (wp) { long long w = atoll(wp); if (w >= 0) g_bench_warm = w; }
    long long cap = want_frames > 0 ? want_frames : 65536;
    g_bench_totals = (long long *)malloc((size_t)cap * sizeof(long long));
    g_bench_totals_cap = g_bench_totals ? cap : 0;
    const char *cp = getenv("CRASTER_BENCH_CSV");
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

#ifdef CRASTER_CUDA
    const int cuda_compiled = 1;
#else
    const int cuda_compiled = 0;
#endif
    if (gm_config_validate_runtime(&cfg, cuda_compiled, cfg_err, sizeof cfg_err) != 0) {
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

#ifdef CRASTER_CUDA
    g_use_cuda = cfg.backend == GM_BACKEND_CUDA;
#endif

    /* Transitional bridge until view distance is carried through GmWorldConfig.
     * It is still sourced from the strict argv config, never a hidden user setting. */
    {
        char view_distance[16];
        snprintf(view_distance, sizeof view_distance, "%d", cfg.view_distance);
        setenv("CRASTER_VIEW_RADIUS", view_distance, 1);
    }
    fprintf(stderr, "[config] ");
    gm_config_print(stderr, &cfg);

    /* --- ALLOCATE-ONCE caps: load craster.conf (env override) BEFORE any pool alloc,
     * so the whole pre-allocation (here + gm_world_create's toroidal pools) is a pure
     * function of the effective caps computed before the window opens. --- */
    cr_caps_load(getenv("CRASTER_CONF"));
    /* Shade-time lightmap (time-of-day terrain lighting). Game binaries opt
     * in; CRASTER_LEGACY_LIGHTMAP=1 restores the noon-baked scalar path. */
    worldmc_set_lightmap_mode(!getenv("CRASTER_LEGACY_LIGHTMAP"));
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

#ifdef CRASTER_CUDA
    /* ALLOCATE-ONCE: cudaMalloc the device framebuffer + tri buffer + shade-ctx
     * ONCE here, sized from caps (fb w*h, max_tris). Every frame only memcpys
     * up/down and launches; no per-frame cudaMalloc. Freed at exit. */
    if (g_use_cuda) cr_raster_cuda_pre(fb_w, fb_h, caps->max_tris);
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
    if (getenv("CRASTER_FIXTURES")) gm_live_init(&live, seed, surface);

    /* Headless inventory demo: seed stone stack and exercise slotClick via the
     * SAME gm_player_inv_click path the live input loop uses (Q / shift+hotbar). */
    if (getenv("CRASTER_INV_DEMO")) {
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
    gm_input_reset();
    gm_hud_init();

    CrWindow *cwin = cr_window_open(fb_w, fb_h, "craster - game");
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
    const int timer_dbg = getenv("CRASTER_TIMER_DEBUG") != NULL;

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
    const int SWING_LEN = 6;         /* MC swings the arm over ~6 ticks */

    /* MEASUREMENT hooks (env-gated, no effect on a normal run):
     *  - ALLOCTRACK: per-frame allocation tripwire via trace/alloctrack.so
     *    (weak symbol; NULL unless the .so is LD_PRELOADed).
     *  - CRASTER_DEBUG_CAPS: per-frame draw-buffer maxima for the fixed-pool sizing. */
    const int at_on   = getenv("ALLOCTRACK") != NULL;
    const int caps_on = getenv("CRASTER_DEBUG_CAPS") != NULL;
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
            /* CRASTER_STILL: measurement gate - stand still (no walk) so a run can
             * hold steady-state without crossing chunk boundaries. Default walks. */
            act.forward = getenv("CRASTER_STILL") ? 0.0f : 1.0f;
            /* CRASTER_JUMP: hold jump every tick (demo runs hop over terrain instead
             * of wedging against a 1-block step). CRASTER_YAWRATE=<deg/frame>: slow
             * scripted turn so a demo pans across the world. */
            if (getenv("CRASTER_JUMP")) act.jump = 1;
            { const char *yr = getenv("CRASTER_YAWRATE"); if (yr) act.dyaw = (float)atof(yr); }
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

            if (screen_open) {
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

        /* MEASUREMENT harness (env-gated, no effect on a normal run): CRASTER_TP=<step>
         * teleports the player -Z by <step> blocks/frame, snapping Y to the surface, so a
         * scripted run traverses many chunks in a straight line without getting stuck on
         * terrain or dying. Used only to profile the streaming allocation path. */
        if (want_frames >= 0) {
            /* CRASTER_SPAWN_SURFACE: snap the frame-0 spawn onto the terrain surface so a
             * demo run does not open with a 47-block death drop from the fixed y=120 spawn. */
            if (frame == 0 && getenv("CRASTER_SPAWN_SURFACE")) {
                double twx = pl.ent.posX + ox, twz = pl.ent.posZ + oz;
                int sy = gm_world_surface_y(world, (int)floor(twx), (int)floor(twz));
                pl.ent.posY = (double)sy + 1.0;
                pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
                pl.ent.motionX = pl.ent.motionY = pl.ent.motionZ = 0.0;
                pl.fall_distance = 0.0f;
            }
            const char *tp = getenv("CRASTER_TP");
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
            }
            /* held-use repeat (Minecraft.rightClickDelayTimer semantics): the
             * click edge fires immediately and arms the 4-tick timer; while the
             * button stays held each expiry fires another place/use. */
            if (!tact.use) use_repeat_delay = 0;
            else if (tact.do_place) use_repeat_delay = 4;
            else if (--use_repeat_delay <= 0) { tact.do_place = 1; use_repeat_delay = 4; }
        }

        /* debug/test hook: force a lethal state at a chosen frame. */
        if (kill_frame >= 0 && frame == kill_frame) { vitals.health = 0.0f; pl.health = 0.0f; }
        gm_runtime_tick(&runtime, tact);
        if (g_dead || runtime.won) running = 0;

        }   /* end tick batch (for t < nticks) */
        bench_stamp(2);

        /* Using a crafting table / furnace opened a runtime container: show it. */
        if (want_frames < 0 && runtime.container != 0 && !screen_open) {
            screen_open = 1;
            cr_window_capture_enable(cwin, 0);
        }

        GmPlayerView pv; gm_runtime_view(&runtime, &pv);
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
         * differ by device sinf in hash21 <=0.012%/frame). CRASTER_CPU_SKY
         * forces the host loop for A/B. */
        int gpu_sky = 0;
#ifdef CRASTER_CUDA
        gpu_sky = g_use_cuda && runtime.dimension == 0 && !getenv("CRASTER_CPU_SKY");
#endif
        if (!gpu_sky) gm_sky_draw(&fb, &cam, day);
        bench_stamp(4);
        /* CUDA: upload the (sky- or clear-filled) fb ONCE, keep it device-resident
         * across the 4 terrain layers + entities, download ONCE after (frame_end,
         * before HUD). */
#ifdef CRASTER_CUDA
        if (g_use_cuda) cr_raster_cuda_frame_begin(&fb);
        if (gpu_sky) {
            GmSkyCtx sc; float bas[11];
            gm_sky_frame_args(&cam, day, &sc, bas);
            cr_raster_cuda_sky(&sc, bas, fb_w, fb_h);
        }
#endif
        bench_stamp(5);
        GmMeshView mv; gm_world_mesh_view(world, &cam, fb_w, fb_h, &mv);
        bench_stamp(6);
        int ntris = render_world(&fb, &cam, &mv, &atlas, tris, day, &uw);
        bench_stamp(7);

        /* ---- targeted-block selection outline + dig crack decal (windowed) ---- */
        if (want_frames < 0 && !screen_open && !g_dead) {
            static CrVertex ov[GM_OVERLAY_MAX_VERTS];
            int hx = 0, hy = 0, hz = 0, ax, ay, az;
            int have_sel = gm_raycast_sel((const struct Chunk *)win, (const struct McSinTable *)&st, (const struct PsvPlayer *)&pl, &hx, &hy, &hz, &ax, &ay, &az) >= 0;
            int dx = 0, dy2 = 0, dz = 0; float dmg = 0.0f;
            int have_dig = gm_player_dig_state(&dx, &dy2, &dz, &dmg);
            float selb[6];
            if (have_sel) gm_sel_box_at(win, hx, hy, hz, selb);
            int nov = gm_overlay_emit(ov, GM_OVERLAY_MAX_VERTS,
                                      have_sel, hx + ox, hy, hz + oz,
                                      have_sel ? selb : 0,
                                      have_dig, dx + ox, dy2, dz + oz, dmg,
                                      cam.pos.x, cam.pos.y, cam.pos.z);
            if (nov > 0) {
                /* vanilla drawSelectionBox: SRC_ALPHA blend, GL_LEQUAL, no
                 * depth write (blend path), no fog/alpha-test. */
                CrShadeCtx osh = {0};
                osh.atlas = &atlas;
                osh.fog_color = sky;
                osh.alpha_test = 0;
                osh.enable_fog = 0;
                osh.layer = CR_LAYER_TRANSLUCENT;
                osh.blend = 1;
                osh.depth_lequal = 1;
                render_layer(&fb, &cam, ov, nov, tris, &osh);
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
        nents += gm_runtime_projectile_views(&runtime, ents+nents, GM_LIVE_MAX-nents);
        nents += gm_live_fill_views(&live, ents + nents, GM_LIVE_MAX - nents);
        if (nents > 0) {
            int nv = gm_entities_emit(ents, nents, ent_verts, ent_max_verts);
            CrTexture eatlas = gm_entity_atlas();
            CrRgba fog = sky;
            CrShadeCtx esh = { &eatlas, fog, 0.f, 0.f, 1, 0, CR_LAYER_CUTOUT, 0, 0, 0.f };
            render_layer(&fb, &cam, ent_verts, nv, tris, &esh);
            /* dropped items, second pass: block cubes/plants on the TERRAIN
             * atlas, then non-block items on the item atlas. ent_verts is
             * reusable: render_layer consumed the mob verts above. */
            nv = gm_items_emit(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrShadeCtx ish = { &atlas, fog, 0.f, 0.f, 1, 0, CR_LAYER_CUTOUT, 0, 0, 0.f };
                render_layer(&fb, &cam, ent_verts, nv, tris, &ish);
            }
            nv = gm_items_emit_flat(ents, nents, ent_verts, ent_max_verts);
            if (nv > 0) {
                CrTexture iatlas = gm_item_atlas();
                CrShadeCtx fsh = { &iatlas, fog, 0.f, 0.f, 1, 0, CR_LAYER_CUTOUT, 0, 0, 0.f };
                render_layer(&fb, &cam, ent_verts, nv, tris, &fsh);
            }
        }

        bench_stamp(9);
#ifdef CRASTER_CUDA
        if (g_use_cuda) cr_raster_cuda_frame_end(&fb);   /* device fb -> host, once */
#endif
        bench_stamp(10);
        /* ---- first-person hand (over the world, before the 2D HUD) ---- */
        {
            float mv_mag = fabsf(act.forward) + fabsf(act.strafe);
            if (mv_mag > 0.01f) hand_bob += 0.30f;   /* advance walk bob */
            if (act.attack || act.do_break) {         /* (re)start a swing */
                if (swing_ticks <= 0) swing_ticks = SWING_LEN;
            }
            float swing = swing_ticks > 0
                ? (float)(SWING_LEN - swing_ticks) / (float)SWING_LEN : 0.0f;
            gm_hand_set_swing(swing);
            if (swing_ticks > 0) swing_ticks--;
            /* Viewmodel environment: eye-block combined light folded into the
             * tint (no frame lightmap texture on this path), the eye-in-water
             * 60/70 fov, and the item-light anchor rotation. */
            {
                int hx = (int)floorf(cpv.x);
                int hy = (int)floorf(cpv.y + cpv.eye_height);
                int hz = (int)floorf(cpv.z);
                int hsky = gm_world_sky_light(world, hx, hy, hz);
                int hblk = gm_world_block_light(world, hx, hy, hz);
                CrLightmapRgb hc3 = cr_lightmap_rgb(runtime.dimension, hsky, hblk,
                    cr_dimension_sun_brightness(runtime.dimension), 0.f, 0.f);
                gm_hand_set_env(0, 15.f, 0.f, hc3.r, hc3.g, hc3.b,
                                uw.fov_scale, cpv.yaw, cpv.pitch);
            }
            /* CRASTER_NO_HAND: measurement gate - skip the first-person arm (used
             * to pixel-diff hand vs no-hand against the MC golden). Default draws. */
            if (!pv.dead && !getenv("CRASTER_NO_HAND"))
                gm_hand_draw(&fb, &pv, hand_bob);
            /* ItemRenderer.renderOverlays: with the hand, before the HUD. */
            if (uw.overlay && !pv.dead)
                gm_uw_overlay_draw(&fb, &cpv, uw.brightness, cam.fov_deg);
        }
        gm_hud_draw(&fb, &pv);
        if (screen_open && !pv.dead)
            gm_screen_draw(&fb, &runtime, mouse_x, mouse_y);
        bench_stamp(11);
        cr_window_present(cwin, &fb);
        bench_stamp(12);
        bench_record(frame, nticks, ntris);

        /* CRASTER_DUMP_DIR: per-frame PPM dump for video encoding (headless demo). */
        if (want_frames >= 0) {
            const char *dd = getenv("CRASTER_DUMP_DIR");
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

#ifdef CRASTER_CUDA
    if (g_use_cuda) cr_raster_cuda_post();
#endif
    free(tris); free(ent_verts);
    gm_runtime_destroy(&runtime);
    cr_window_close(cwin);
    cr_fb_free(&fb);
    return 0;
}
