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
#include "game/window_compose.h"
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

/* ParticleDigging multiplyColor: block color multiplier for live dig
 * particles (grass exception handled in bm_particle_tint). */
static void dig_particle_base_color(const GmWorld *world, int model_key,
                                    int wx, int wy, int wz,
                                    float *br, float *bg, float *bb)
{
    *br = *bg = *bb = 1.0f;
    int tint = bm_particle_tint(model_key);
    int rgb = -1; /* vanilla: no handler -> -1 -> white when multiplied */
    switch (tint) {
        case BM_TINT_GRASS:
            rgb = gm_world_grass_color(world, wx, wy, wz);
            break;
        case BM_TINT_FOLIAGE:
            rgb = gm_world_foliage_color(world, wx, wy, wz);
            break;
        case BM_TINT_LILY:
            rgb = 2129968; /* BlockColors WATERLILY in-world */
            break;
        case BM_TINT_FOLIAGE_PINE:
            rgb = 6396257; /* ColorizerFoliage pine */
            break;
        case BM_TINT_FOLIAGE_BIRCH:
            rgb = 8431445; /* ColorizerFoliage birch */
            break;
        default:
            return;
    }
    if (rgb < 0) return;
    *br = (float)((rgb >> 16) & 255) / 255.0f;
    *bg = (float)((rgb >> 8) & 255) / 255.0f;
    *bb = (float)(rgb & 255) / 255.0f;
}

/* Deterministic window-battery view of two sealed source-fluid basins. This
 * is a measurement fixture only; normal worlds and the capture compositor do
 * not enter it. */
static void init_anim_texture_demo(GmRuntime *r)
{
    for (int side = 0; side < 2; ++side) {
        int x0 = side ? 9 : 1;
        int x1 = side ? 15 : 7;
        int fluid = side ? 11 : 9;
        for (int x = x0; x <= x1; ++x) {
            for (int z = 4; z <= 10; ++z) {
                int rim = x == x0 || x == x1 || z == 4 || z == 10;
                gm_runtime_set_block(r, x, 3, z, 1, 0);
                gm_runtime_set_block(r, x, 4, z, rim ? 1 : fluid, 0);
                gm_runtime_set_block(r, x, 5, z, 0, 0);
            }
        }
    }
    for (int y = 4; y <= 7; ++y)
        gm_runtime_set_block(r, 8, y, 16, 1, 0);
    gm_runtime_set_pose(r, 8.5, 8.0, 16.5, 180.0f, 35.0f);
}

/* Deterministic window-battery view for translucent-terrain entity ordering.
 * Three zombies share one camera: left is behind a two-block-thick full-height
 * water column, centre is in front of an identical column, and right is behind
 * a two-block-thick one-block-high column (half-submerged in screen space).
 * MAGMA_ENTITY_WATER_DRY keeps the geometry/poses but omits only the water. */
static void init_entity_water_demo(GmRuntime *r)
{
    int dry = getenv("MAGMA_ENTITY_WATER_DRY") != NULL;
    static const int x0[3] = {2, 7, 11};
    static const int x1[3] = {5, 9, 14};
    static const int ymax[3] = {6, 6, 4};
    for (int group = 0; group < 3; ++group) {
        for (int x = x0[group]; x <= x1[group]; ++x) {
            for (int z = 10; z <= 11; ++z) {
                for (int y = 4; y <= ymax[group]; ++y)
                    gm_runtime_set_block(r, x, y, z, dry ? 0 : 9, 0);
            }
        }
    }
    gm_runtime_set_pose(r, 8.5, 4.2, 18.5, 180.0f, 0.0f);
    gm_mobs_spawn(&r->mobs, EW_TYPE_ZOMBIE, 4.0, 4.0, 7.5);
    gm_mobs_spawn(&r->mobs, EW_TYPE_ZOMBIE, 8.5, 4.0, 13.5);
    gm_mobs_spawn(&r->mobs, EW_TYPE_ZOMBIE, 12.5, 4.0, 7.5);
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
    if (cfg.rl) return gm_rl_run(&cfg);
    if (cfg.headless) return gm_script_run(&cfg);

    /* --- persistent window compositor + framebuffer/scratch --- */
    GmWindowCompose *compose = gm_window_compose_open(
        &cfg, cfg_err, sizeof cfg_err);
    if (!compose) {
        fprintf(stderr, "window compose: %s\n", cfg_err);
        return 1;
    }
    CrFramebuffer *window_fb = gm_window_compose_framebuffer(compose);
#define fb (*window_fb)

    /* One owned simulation state and one authoritative transition. The macros keep
     * the rendering code readable while making every gameplay field part of runtime. */
    GmRuntime runtime;
    char runtime_err[256];
    if (!gm_runtime_init(&runtime, &cfg, runtime_err, sizeof runtime_err)) {
        fprintf(stderr, "error: %s\n", runtime_err);
        gm_window_compose_close(compose);
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
    if (getenv("MAGMA_ANIM_TEXTURE_DEMO"))
        init_anim_texture_demo(&runtime);
    if (getenv("MAGMA_ENTITY_WATER_DEMO"))
        init_entity_water_demo(&runtime);
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

    GmParticlesLive live_particles;
    gm_particles_live_init(&live_particles,
        (uint64_t)seed ^ UINT64_C(0x7061727469636c65));
    const int particle_demo = getenv("MAGMA_PARTICLE_DEMO") != NULL;
    gm_input_reset();
    gm_hud_init();
    gm_window_compose_bind(compose, &runtime, &live_particles);

    CrWindow *cwin = cr_window_open(fb_w, fb_h, "magma - game");
    if (!cwin) { fprintf(stderr, "cr_window_open failed\n"); return 1; }

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
            /* MAGMA_ATTACK: hold left-click every tick (drives dig SM + live particles). */
            if (getenv("MAGMA_ATTACK")) act.attack = 1;
            /* MAGMA_HOTBAR=<0..8>: select that hotbar slot once at frame 0. */
            act.hotbar_sel = -1;
            if (frame == 0) {
                const char *hb = getenv("MAGMA_HOTBAR");
                if (hb) {
                    int slot = atoi(hb);
                    if (slot >= 0 && slot <= 8) act.hotbar_sel = slot;
                }
            }
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
            /* MAGMA_PITCH=<deg>: set player pitch at frame 0 (clamped to look range). */
            if (frame == 0) {
                const char *pit = getenv("MAGMA_PITCH");
                if (pit) {
                    float p = (float)atof(pit);
                    if (p > 89.0f) p = 89.0f;
                    if (p < -89.0f) p = -89.0f;
                    pl.pitch = p;
                }
            }
            /* MAGMA_SET_TIME=<ticks>: pin world clock before the first tick so night
             * lighting is reachable in a short dump (same as script set_time). */
            if (frame == 0) {
                const char *stime = getenv("MAGMA_SET_TIME");
                if (stime) gm_runtime_set_time(&runtime, atoll(stime));
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
            float pbr, pbg, pbb;
            dig_particle_base_color(world, model, pwx, pwy, pwz, &pbr, &pbg, &pbb);
            gm_particles_live_seed(&live_particles,
                UINT64_C(0x5041525449434c45));
            int spawned = gm_particles_live_spawn_destroy(&live_particles,
                pwx, pwy, pwz, model, plm.r, plm.g, plm.b, pbr, pbg, pbb);
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
                float pbr, pbg, pbb;
                dig_particle_base_color(world, model, pwx, phy, pwz,
                                        &pbr, &pbg, &pbb);
                gm_particles_live_spawn_destroy(&live_particles,
                    pwx, phy, pwz, model, plm.r, plm.g, plm.b, pbr, pbg, pbb);
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
                float pbr, pbg, pbb;
                dig_particle_base_color(world, model, pwx, phy, pwz,
                                        &pbr, &pbg, &pbb);
                gm_particles_live_spawn_hit(&live_particles,
                    pwx, phy, pwz, model, pface, pbounds,
                    plm.r, plm.g, plm.b, pbr, pbg, pbb);
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
        gm_window_compose_advance(compose, &pv, &act, nticks);
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
        GmWindowComposeFrame compose_frame = {
            .view = &pv,
            .camera_view = &cpv,
            .partial_ticks = partial_ticks,
            .interactive = want_frames < 0,
            .screen_open = screen_open,
            .mouse_x = mouse_x,
            .mouse_y = mouse_y,
            .stamp = bench_stamp,
        };
        GmWindowComposeStats compose_stats;
        if (!gm_window_compose_draw(compose, &compose_frame, &compose_stats,
                                    cfg_err, sizeof cfg_err)) {
            fprintf(stderr, "window compose: %s\n", cfg_err);
            running = 0;
            break;
        }
        int ntris = compose_stats.ntris;
        if (caps_on) {
            int total = 0;
            for (int l = 0; l < 4; ++l) {
                if (compose_stats.mesh_nverts[l] > cap_max_layer[l])
                    cap_max_layer[l] = compose_stats.mesh_nverts[l];
                total += compose_stats.mesh_nverts[l];
            }
            if (total > cap_max_total) cap_max_total = total;
            if (compose_stats.mesh_kept > cap_max_kept)
                cap_max_kept = compose_stats.mesh_kept;
            if (ntris > cap_max_tris) cap_max_tris = ntris;
            fprintf(stderr,
                "[caps] frame %d kept=%d culled=%d drawverts[S/CM/C/T]=%d/%d/%d/%d total=%d screen_tris=%d\n",
                frame, compose_stats.mesh_kept, compose_stats.mesh_culled,
                compose_stats.mesh_nverts[0], compose_stats.mesh_nverts[1],
                compose_stats.mesh_nverts[2], compose_stats.mesh_nverts[3],
                total, ntris);
        }
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
            sizeof(CrVertex), sizeof(CrScreenTri), caps->max_tris,
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

    gm_window_compose_close(compose);
    gm_runtime_destroy(&runtime);
    cr_window_close(cwin);
#undef fb
    return 0;
}
