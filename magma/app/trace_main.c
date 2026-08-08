/* app/trace_main.c - TICK-TRACE DIVERGENCE ORACLE, C side.
 *
 * Headless trace runner: replay a FIXED action tape (the same qrl action space the
 * Java game consumes) through the VERIFIED blaze player tick inside the magma world,
 * and emit a COMPACT per-tick physics CSV (`c_phys.csv`) with the SAME columns as the
 * Java tracer (trace/trace_java.py). No window is opened; nothing is displayed.
 *
 * This is a SIBLING of app/game_main.c (do NOT edit that file). It copies the seam loop
 * (floating-origin recenter -> fill window -> gm_player_tick -> apply edits -> view) but
 * replaces live keyboard input with tape lines, and replaces the window present with a
 * CSV row + an optional per-tick FNV-1a hash of the rendered RGBA framebuffer.
 *
 * DISK EFFICIENCY: a normal run writes ONLY the CSV (one small row per tick). Frames are
 * never stored per tick. When the diff tool finds a divergence at tick T it RE-RUNS this
 * binary with --dump-dir/--dump-lo/--dump-hi to MATERIALIZE just the frames in [T-K,T+K]
 * as PPMs -- so a clean run costs only the CSV.
 *
 * Action tape line (one per tick, whitespace-separated, '#' comment / blank ok):
 *   forward back left right jump sneak sprint attack use yaw pitch [close]
 * forward/back/left/right/jump/sneak/sprint/attack/use/close in {0,1};
 * yaw/pitch in {-1,0,1}. The optional close flag defaults to zero.
 * (15-degree quantum steps, matching qrl_client.py / Recorder.applyAction).
 *
 * CSV columns (match trace_java.py):
 *   tick,x,y,z,yaw,pitch,vx,vy,vz,on_ground,health,food,air,frame_hash
 * x/y/z world FEET coords (double), yaw/pitch MC-convention degrees, vx/vy/vz = motion,
 * on_ground/health/food ints-or-floats, air = -1 (C interim vitals do not model air),
 * frame_hash = 64-bit FNV-1a of the RGBA framebuffer (0 if --render 0).
 *
 * FULL STATE VECTOR (--state PATH, default trace/out/c_state.jsonl): one JSON object per
 * tick with the SAME schema the Java tracer emits (trace/trace_java.py). Categories the C
 * magma game does NOT simulate emit JSON `null` (a SENTINEL) so the diff tool can report
 * them as UNSIMULATED rather than "matching". Simulated on C: player physics + verified
 * vanilla vitals (health/food/saturation/fall_distance), held item + full 36-slot inventory
 * (blaze IC_* ids -- a DIFFERENT id namespace from Java's vanilla registry ids; see README),
 * sprint/sneak/jump INTENT (from the tape). UNSIMULATED -> null: air, fire, xp, potion
 * effects, attack cooldown, hurt/death timers, and ENTITIES (magma game wires nents=0).
 * The trace clock is explicit: --world-time plus --daylight; weather is pinned clear.
 *
 * A spawn sidecar (--spawn-out PATH) writes the INITIAL (pre-tick) spawn pose as
 *   x y z yaw pitch
 * (world FEET, MC degrees) so the Java tracer can teleport its player to the SAME tick-0
 * pose plus dynamics before replaying the tape (fair per-tick physics comparison;
 * see README):
 *   x y z yaw pitch vx vy vz on_ground fall_distance
 * Older readers deliberately consume only the first five fields.
 *
 * yaw/pitch conventions match Java: player yaw starts MC-180, each tick adds step*15deg to
 * the MC yaw/pitch (player_ctl integrates dyaw/dpitch); positions are converted local->world
 * exactly as game_main via gm_player_view. See app/game_main.c for the seam rationale.
 *
 * Args:
 *   --seed N          worldgen seed                              (default 0)
 *   --tape PATH       action tape file (required)
 *   --out PATH        output CSV                                 (default trace/out/c_phys.csv)
 *   --w W --h H       framebuffer size for the frame hash        (default 320x180, cheap)
 *   --render 0|1      render+hash each tick                      (default 1)
 *   --dump-dir DIR    materialize frames for [dump-lo,dump-hi]   (default none)
 *   --dump-lo L --dump-hi H   inclusive tick window to dump as DIR/c_<tick>.ppm
 *   --platform N      replace an odd NxN square below spawn with stone and
 *                     clear six blocks above it (the complete BlockFire
 *                     scheduled-update proof neighborhood)
 *   --blocks-before-out PATH --blocks-out PATH
 *   --blocks-box X0 Y0 Z0 X1 Y1 Z1
 *                     pre/post raw little-endian u16 (id<<4|meta), y/z/x order
 *   --blocks-y-relative
 *                     interpret Y0/Y1 as offsets from settled pre-tick feet
 *   --blocks-box-out PATH
 *                     write the resolved absolute six-integer box
 */
#include "core/types.h"
#include "game/game.h"
#include "game/sky.h"
#include "game/view.h"

/* blaze: PsvPlayer / Chunk / McSinTable + the verified init helpers. */
#include "player_survival.h"
#include "player_vitals.h"   /* verified vanilla vitals (PvStats, pv_init) */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD   ((float)(M_PI / 180.0))
#define MAX_TRIS  (4 * 1024 * 1024)
#define MAX_EDITS 8
#define MAX_FIXTURE_BLOCKS 64
#define AIM_QUANTUM 15.0f     /* qrl 15-degree aim step (Recorder.QUANTUM) */

/* floor-division block coord -> chunk coord (handles negatives). */
static int floordiv16(int a) { return (a >= 0) ? (a >> 4) : -(((-a) + 15) >> 4); }

/* ---- action tape ---- */
typedef struct {
    int forward, back, left, right, jump, sneak, sprint, attack, use, yaw, pitch;
    int close_container;
} TapeTick;

typedef struct {
    int x, y, z, id, meta;
} FixtureBlock;

/* Parse the tape file into a heap array; returns count, or -1 on error. */
static int tape_load(const char *path, TapeTick **out) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "trace: cannot open tape %s\n", path); return -1; }
    int cap = 256, n = 0;
    TapeTick *t = (TapeTick *)malloc((size_t)cap * sizeof(TapeTick));
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* strip leading whitespace to detect comments/blanks */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        TapeTick tt;
        memset(&tt, 0, sizeof tt);
        int got = sscanf(p, "%d %d %d %d %d %d %d %d %d %d %d %d",
                         &tt.forward, &tt.back, &tt.left, &tt.right, &tt.jump,
                         &tt.sneak, &tt.sprint, &tt.attack, &tt.use, &tt.yaw,
                         &tt.pitch, &tt.close_container);
        if (got != 11 && got != 12) {
            fprintf(
                stderr, "trace: bad tape line (%d/11-or-12 fields): %s",
                got, line);
            free(t); fclose(f); return -1;
        }
        if (n == cap) { cap *= 2; t = (TapeTick *)realloc(t, (size_t)cap * sizeof(TapeTick)); }
        t[n++] = tt;
    }
    fclose(f);
    *out = t;
    return n;
}

/* 64-bit FNV-1a over the framebuffer RGBA bytes. */
static unsigned long long fb_hash(const CrFramebuffer *fb) {
    unsigned long long h = 1469598103934665603ULL;
    const unsigned char *b = (const unsigned char *)fb->color;
    size_t nbytes = (size_t)fb->w * fb->h * 4;
    for (size_t i = 0; i < nbytes; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
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

/* Match QuantizedRL getblocks byte-for-byte: inclusive cuboid, y-major then z then x,
 * each state encoded as little-endian u16 (vanilla numeric block id << 4 | meta). */
static int write_blocks(const char *path, GmWorld *world, const int box[6]) {
    const int x0 = box[0], y0 = box[1], z0 = box[2];
    const int x1 = box[3], y1 = box[4], z1 = box[5];
    if (x1 < x0 || y1 < y0 || z1 < z0 || y0 < 0 || y1 > 255) return -1;

    /* Observation may cross the live view edge. Generate the complete sampled chunk
     * rectangle before reading it, just as Java WorldServer.getBlockState loads chunks. */
    const int cx0 = floordiv16(x0), cx1 = floordiv16(x1);
    const int cz0 = floordiv16(z0), cz1 = floordiv16(z1);
    const int ccx = (cx0 + cx1) / 2, ccz = (cz0 + cz1) / 2;
    int radius = abs(cx0 - ccx);
    if (abs(cx1 - ccx) > radius) radius = abs(cx1 - ccx);
    if (abs(cz0 - ccz) > radius) radius = abs(cz0 - ccz);
    if (abs(cz1 - ccz) > radius) radius = abs(cz1 - ccz);
    gm_world_ensure(world, ccx, ccz, radius);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                uint16_t v = (uint16_t)((gm_world_block(world, x, y, z) << 4) |
                                        (gm_world_meta(world, x, y, z) & 15));
                unsigned char b[2] = {(unsigned char)(v & 255),
                                      (unsigned char)((v >> 8) & 255)};
                if (fwrite(b, 1, 2, f) != 2) { fclose(f); return -1; }
            }
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* --- render helpers (copied from app/game_main.c; do NOT edit that file) --- */
static void render_layer(CrFramebuffer *fb, const CrCamera *cam,
                         const CrVertex *verts, int nv,
                         CrScreenTri *tris, const CrShadeCtx *sh) {
    if (nv < 3) return;
    int ntris = cr_transform(verts, nv, NULL, 0, cam, fb->w, fb->h, tris, MAX_TRIS);
    if (ntris > 0) cr_raster_cpu(fb, tris, ntris, sh);
}

static void render_world(CrFramebuffer *fb, const CrCamera *cam, const GmMeshView *mv,
                         const CrTexture *atlas, CrScreenTri *tris) {
    CrRgba fog = {135, 206, 235, 255};
    CrShadeCtx sh_solid = {
            .atlas = atlas, .fog_color = fog, .fog_start = 0.f, .fog_end = 0.f,
            .alpha_test = 0, .enable_fog = 0, .layer = CR_LAYER_SOLID,
            .blend = 0, .use_mips = 0, .mip_bias = 0.f };
    CrShadeCtx sh_cmip  = {
            .atlas = atlas, .fog_color = fog, .fog_start = 0.f, .fog_end = 0.f,
            .alpha_test = 1, .enable_fog = 0, .layer = CR_LAYER_CUTOUT_MIPPED,
            .blend = 0, .use_mips = 1, .mip_bias = 0.f };
    sh_cmip.depth_lequal = 1;  /* coplanar grass_side_overlay (GL_LEQUAL) */
    CrShadeCtx sh_cut   = {
            .atlas = atlas, .fog_color = fog, .fog_start = 0.f, .fog_end = 0.f,
            .alpha_test = 1, .enable_fog = 0, .layer = CR_LAYER_CUTOUT,
            .blend = 0, .use_mips = 0, .mip_bias = 0.f };
    CrShadeCtx sh_trans = {
            .atlas = atlas, .fog_color = fog, .fog_start = 0.f, .fog_end = 0.f,
            .alpha_test = 0, .enable_fog = 0, .layer = CR_LAYER_TRANSLUCENT,
            .blend = 1, .use_mips = 0, .mip_bias = 0.f };
    render_layer(fb, cam, mv->verts[0], mv->nverts[0], tris, &sh_solid);
    render_layer(fb, cam, mv->verts[1], mv->nverts[1], tris, &sh_cmip);
    render_layer(fb, cam, mv->verts[2], mv->nverts[2], tris, &sh_cut);
    render_layer(fb, cam, mv->verts[3], mv->nverts[3], tris, &sh_trans);
}

static CrCamera cam_from_view(const GmPlayerView *pv, int fb_w, int fb_h) {
    CrCamera c;
    c.pos.x = pv->x;
    c.pos.y = pv->y + pv->eye_height;
    c.pos.z = pv->z;
    c.yaw   = (pv->yaw - 180.0f) * DEG2RAD;
    c.pitch = -pv->pitch * DEG2RAD;
    c.fov_deg = 70.0f;
    c.aspect  = (float)fb_w / (float)fb_h;
    c.znear   = 0.05f;
    c.zfar    = GM_TERRAIN_ZFAR;
    c.hurt_yaw_deg = pv->hurt_yaw;
    c.hurt_roll_deg = gm_view_hurt_roll_deg(pv->hurt_time, pv->max_hurt_time);
    return c;
}

int main(int argc, char **argv) {
    long long   seed = 0;
    int         fb_w = 320, fb_h = 180;
    int         do_render = 1;
    const char *tape_path = NULL;
    const char *out_path  = "trace/out/c_phys.csv";
    const char *state_path = "trace/out/c_state.jsonl";
    const char *spawn_out = NULL;
    const char *dump_dir  = NULL;
    const char *blocks_before_out = NULL;
    const char *blocks_out = NULL;
    const char *blocks_box_out = NULL;
    int         dump_lo = -1, dump_hi = -1;
    int         platform = 0;
    int         empty_inventory = 0;
    long long   world_time = 6000;
    int         daylight = 0;
    FixtureBlock fixtures[MAX_FIXTURE_BLOCKS];
    int         nfixtures = 0;
    int         blocks_box[6] = {0, 0, 0, 0, 0, 0};
    int         have_blocks_box = 0;
    int         blocks_y_relative = 0;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--seed")     && i+1 < argc) seed = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--tape")     && i+1 < argc) tape_path = argv[++i];
        else if (!strcmp(argv[i], "--out")      && i+1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "--state")    && i+1 < argc) state_path = argv[++i];
        else if (!strcmp(argv[i], "--spawn-out")&& i+1 < argc) spawn_out = argv[++i];
        else if (!strcmp(argv[i], "--w")        && i+1 < argc) fb_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h")        && i+1 < argc) fb_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--render")   && i+1 < argc) do_render = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-dir") && i+1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--dump-lo")  && i+1 < argc) dump_lo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-hi")  && i+1 < argc) dump_hi = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--platform") && i+1 < argc) platform = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--empty-inventory")) empty_inventory = 1;
        else if (!strcmp(argv[i], "--world-time") && i+1 < argc) world_time = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--daylight") && i+1 < argc) daylight = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--set-block") && i+5 < argc) {
            if (nfixtures >= MAX_FIXTURE_BLOCKS) {
                fprintf(stderr, "trace: too many --set-block fixtures (max %d)\n",
                        MAX_FIXTURE_BLOCKS);
                return 2;
            }
            FixtureBlock *b = &fixtures[nfixtures++];
            b->x = atoi(argv[++i]); b->y = atoi(argv[++i]); b->z = atoi(argv[++i]);
            b->id = atoi(argv[++i]); b->meta = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--blocks-before-out") && i+1 < argc) blocks_before_out = argv[++i];
        else if (!strcmp(argv[i], "--blocks-out") && i+1 < argc) blocks_out = argv[++i];
        else if (!strcmp(argv[i], "--blocks-box-out") && i+1 < argc) blocks_box_out = argv[++i];
        else if (!strcmp(argv[i], "--blocks-y-relative")) blocks_y_relative = 1;
        else if (!strcmp(argv[i], "--blocks-box") && i+6 < argc) {
            for (int j = 0; j < 6; ++j) blocks_box[j] = atoi(argv[++i]);
            have_blocks_box = 1;
        }
        else { fprintf(stderr,
            "usage: %s --tape PATH [--seed N] [--out CSV] [--state JSONL] [--spawn-out F]\n"
            "          [--w W --h H] [--render 0|1] [--dump-dir DIR --dump-lo L --dump-hi H]\n"
            "          [--platform N] [--blocks-before-out F --blocks-out F]\n"
            "          [--blocks-box X0 Y0 Z0 X1 Y1 Z1]\n"
            "          [--blocks-y-relative --blocks-box-out F]\n"
            "          [--empty-inventory]\n"
            "          [--world-time N --daylight 0|1]\n"
            "          [--set-block X Y Z ID META]... (identical staged fixtures)\n",
            argv[0]); return 2; }
    }
    if (!tape_path) { fprintf(stderr, "trace: --tape is required\n"); return 2; }
    if ((blocks_out != NULL) != have_blocks_box) {
        fprintf(stderr, "trace: --blocks-out and --blocks-box must be supplied together\n");
        return 2;
    }
    if (blocks_before_out && !have_blocks_box) {
        fprintf(stderr, "trace: --blocks-before-out requires --blocks-box\n");
        return 2;
    }
    if (platform < 0 || (platform > 0 && platform % 2 == 0)) {
        fprintf(stderr, "trace: --platform must be 0 or a positive odd integer\n");
        return 2;
    }
    if (daylight != 0 && daylight != 1) {
        fprintf(stderr, "trace: --daylight must be 0 or 1\n");
        return 2;
    }
    if (dump_dir) do_render = 1;   /* need to render to dump frames */

    TapeTick *tape = NULL;
    int nticks = tape_load(tape_path, &tape);
    if (nticks < 0) return 1;
    fprintf(stderr, "trace: loaded %d ticks from %s\n", nticks, tape_path);

    /* --- framebuffer + scratch --- */
    CrFramebuffer fb; cr_fb_alloc(&fb, fb_w, fb_h);
    CrScreenTri *tris = (CrScreenTri *)malloc((size_t)MAX_TRIS * sizeof(CrScreenTri));
    Chunk       *win  = (Chunk *)malloc((size_t)PSV_NCHUNKS * sizeof(Chunk));
    if (!fb.color || !tris || !win) { fprintf(stderr, "alloc failed\n"); return 1; }

    McSinTable st; mc_sin_table_init(&st);

    /* --- world --- */
    GmWorld *world = gm_world_create(seed);
    if (!world) { fprintf(stderr, "gm_world_create failed\n"); return 1; }

    /* --- spawn the player on the real surface at the origin column (same as game_main) --- */
    int spawn_wx = 8, spawn_wz = 8;
    gm_world_ensure(world, 0, 0, 2);
    int surface = gm_world_surface_y(world, spawn_wx, spawn_wz);
    PsvPlayer pl; psv_player_init(&pl);
    if (empty_inventory) isr_init(&pl.inv);
    PvStats vitals; pv_init(&vitals);   /* verified vanilla vitals (mirrored into pl each tick) */
    int ccx = floordiv16(spawn_wx), ccz = floordiv16(spawn_wz);
    int ox  = ccx * 16, oz = ccz * 16;
    pl.ent.posX = (double)spawn_wx + 0.5 - ox;
    pl.ent.posZ = (double)spawn_wz + 0.5 - oz;
    pl.ent.posY = (double)surface + 1.0;
    /* Rebuild the collision AABB around the repositioned feet (game_main.c does this via
     * mc_pcm_player_box after every reposition). WITHOUT it the box stays at psv_player_init's
     * PSV_SPAWN_Y=120 and the first physics tick snaps posY back to ~120, silently ignoring the
     * surface spawn -- which also poisoned the spawn sidecar. */
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    /* MC yaw 180 -> magma looks -Z. Use the WRAPPED form -180 (identical direction):
     * the Java side reaches this pose via a `tp`, whose wrapDegrees clamps yaw to
     * [-180,180). rotationYaw then integrates UNWRAPPED on both sides, and the MathHelper
     * sin/cos LUT index truncates toward zero -- so +195 vs -165 (same angle!) land on
     * ADJACENT table entries and walk direction diverges by ~0.005 deg. Starting from the
     * same float keeps every subsequent yaw bit-identical to the live game. */
    pl.yaw = -180.0f; pl.pitch = 0.0f;

    /* Apply the same physics-isolation fixture as trace_java.py. Python round(8.5)
     * selects 8 (ties-to-even), which is also floor(spawn_wx + 0.5) for integer
     * spawn_wx. The platform lies immediately below the eventual grounded feet. */
    if (platform > 0) {
        int h = platform / 2;
        int pcx = spawn_wx, pcz = spawn_wz, py = surface - 1;
        gm_world_ensure(world, floordiv16(pcx), floordiv16(pcz), h / 16 + 1);
        for (int z = pcz - h; z <= pcz + h; ++z) {
            for (int x = pcx - h; x <= pcx + h; ++x) {
                gm_world_set_block(world, x, py, z, 1);
                for (int dy = 1; dy <= 6; ++dy)
                    gm_world_set_block(world, x, py + dy, z, 0);
            }
        }
        fprintf(stderr, "trace: platform %dx%d stone at y=%d with 6 air layers centered (%d,%d)\n",
                2 * h + 1, 2 * h + 1, py, pcx, pcz);
    }
    for (int i = 0; i < nfixtures; ++i) {
        FixtureBlock *b = &fixtures[i];
        if (b->y < 0 || b->y > 255 || b->id < 0 || b->meta < 0 || b->meta > 15) {
            fprintf(stderr, "trace: invalid --set-block fixture %d\n", i);
            return 2;
        }
        gm_world_ensure(world, floordiv16(b->x), floordiv16(b->z), 0);
        gm_world_set_block_meta(world, b->x, b->y, b->z, b->id, b->meta);
    }
    if (nfixtures)
        fprintf(stderr, "trace: staged %d identical fixture block(s)\n", nfixtures);

    /* SETTLE to the grounded rest BEFORE tick 0. gm_world_surface_y returns first-air y, so
     * posY=surface+1 spawns the player one block high (onGround=0, airborne). Java's tracer
     * teleports+settles to a GROUNDED pose (onGround=1) before replaying the tape, so an airborne
     * C tick-0 vs a grounded Java tick-0 is an apples-to-oranges start: different accel (0.02 air
     * vs ~0.1 ground) and friction (0.91 air vs 0.546 ground) that compounds over the run. Run
     * zero-input physics ticks here until the player rests on the ground (feet == surface,
     * onGround=1), so BOTH sides begin tick 0 in the identical grounded state. */
    {
        GmAction idle; memset(&idle, 0, sizeof(idle)); idle.hotbar_sel = -1;
        GmBlockEdit se[MAX_EDITS]; int sne = 0;
        for (int s = 0; s < 40; ++s) {
            gm_world_fill_window(world, ccx, ccz, (struct Chunk *)win);
            gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                           (struct PsvPlayer *)&pl, (struct PvStats *)&vitals, idle,
                           ox, 0, oz, se, &sne, MAX_EDITS);
            /* onGround=1 means the move already clamped feet to the surface (rest); psv
             * re-applies gravity post-move so motionY never hits exactly 0 at rest. */
            if (pl.ent.onGround) break;
        }
        fprintf(stderr, "trace: settled to feet=%.4f onGround=%d before tick 0\n",
                pl.ent.posY, pl.ent.onGround);
    }

    /* Initial (pre-tick) spawn pose in WORLD feet coords / MC degrees. Written to the spawn
     * sidecar so the Java tracer can teleport to the SAME tick-0 pose (fair physics diff). */
    double spawn_x = pl.ent.posX + (double)ox;
    double spawn_y = pl.ent.posY;
    double spawn_z = pl.ent.posZ + (double)oz;
    if (have_blocks_box && blocks_y_relative) {
        int feet_y = (int)floor(spawn_y);
        blocks_box[1] += feet_y;
        blocks_box[4] += feet_y;
        fprintf(stderr, "trace: resolved relative block Y box at feet=%d -> %d..%d\n",
                feet_y, blocks_box[1], blocks_box[4]);
    }
    if (have_blocks_box &&
        (blocks_box[3] < blocks_box[0] || blocks_box[4] < blocks_box[1] ||
         blocks_box[5] < blocks_box[2] ||
         blocks_box[1] < 0 || blocks_box[4] > 255)) {
        fprintf(stderr, "trace: invalid resolved block box %d %d %d %d %d %d\n",
                blocks_box[0], blocks_box[1], blocks_box[2],
                blocks_box[3], blocks_box[4], blocks_box[5]);
        return 2;
    }
    if (blocks_box_out && have_blocks_box) {
        FILE *bf = fopen(blocks_box_out, "w");
        if (!bf) {
            fprintf(stderr, "trace: cannot write resolved block box -> %s\n",
                    blocks_box_out);
            return 1;
        }
        fprintf(bf, "%d %d %d %d %d %d\n",
                blocks_box[0], blocks_box[1], blocks_box[2],
                blocks_box[3], blocks_box[4], blocks_box[5]);
        fclose(bf);
    }
    if (spawn_out) {
        FILE *sf = fopen(spawn_out, "w");
        if (sf) {
            fprintf(sf, "%.17g %.17g %.17g %.9g %.9g "
                        "%.17g %.17g %.17g %d %.9g\n",
                    spawn_x, spawn_y, spawn_z, (double)pl.yaw, (double)pl.pitch,
                    pl.ent.motionX, pl.ent.motionY, pl.ent.motionZ,
                    pl.ent.onGround, (double)pl.fall_distance);
            fclose(sf);
            fprintf(stderr, "trace: spawn %.3f %.3f %.3f yaw=%.1f pitch=%.1f -> %s\n",
                    spawn_x, spawn_y, spawn_z, (double)pl.yaw, (double)pl.pitch, spawn_out);
        }
    }

    if (blocks_before_out) {
        if (write_blocks(blocks_before_out, world, blocks_box) != 0) {
            fprintf(stderr, "trace: failed to write pre-tick blocks -> %s\n",
                    blocks_before_out);
            return 1;
        }
        fprintf(stderr, "trace: wrote pre-tick block states -> %s\n", blocks_before_out);
    }

    CrTexture atlas = gm_world_atlas(world);
    gm_input_reset();

    FILE *csv = fopen(out_path, "w");
    if (!csv) { fprintf(stderr, "trace: cannot open out %s\n", out_path); return 1; }
    fprintf(csv, "tick,x,y,z,yaw,pitch,vx,vy,vz,on_ground,health,food,air,frame_hash\n");

    FILE *state = fopen(state_path, "w");
    if (!state) { fprintf(stderr, "trace: cannot open state %s\n", state_path); return 1; }

    const CrRgba sky = {135, 206, 235, 255};
    int prev_attack = 0, prev_use = 0;

    for (int t = 0; t < nticks; ++t) {
        TapeTick tt = tape[t];

        /* ---- tape line -> GmAction (qrl action space) ---- */
        GmAction act;
        memset(&act, 0, sizeof(act));
        act.forward = (float)(tt.forward - tt.back);
        act.strafe  = (float)(tt.right   - tt.left);   /* D=+1 right, A=-1 left */
        act.jump    = tt.jump;
        act.sneak   = tt.sneak;
        act.sprint  = tt.sprint;
        act.attack  = tt.attack;
        act.use     = tt.use;
        act.do_break = (tt.attack && !prev_attack) ? 1 : 0;
        act.do_place = (tt.use    && !prev_use)    ? 1 : 0;
        prev_attack = tt.attack;
        prev_use    = tt.use;
        act.dyaw   = (float)tt.yaw   * AIM_QUANTUM;   /* +/-15 deg per step, MC yaw */
        act.dpitch = (float)tt.pitch * AIM_QUANTUM;
        act.hotbar_sel = -1;
        act.close_container = tt.close_container;

        /* ---- recenter floating origin (verbatim seam from game_main.c) ---- */
        double wx = pl.ent.posX + ox, wz = pl.ent.posZ + oz;
        int nccx = floordiv16((int)floor(wx)), nccz = floordiv16((int)floor(wz));
        if (nccx != ccx || nccz != ccz) {
            double dx = (double)((nccx - ccx) * 16), dz = (double)((nccz - ccz) * 16);
            ccx = nccx; ccz = nccz; ox = ccx * 16; oz = ccz * 16;
            pl.ent.posX -= dx;      pl.ent.posZ -= dz;
            pl.ent.box.minX -= dx;  pl.ent.box.maxX -= dx;
            pl.ent.box.minZ -= dz;  pl.ent.box.maxZ -= dz;
        }

        /* ---- fill the physics window, tick the player, apply edits ---- */
        gm_world_fill_window(world, ccx, ccz, (struct Chunk *)win);
        GmBlockEdit edits[MAX_EDITS]; int nedits = 0;
        gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&vitals, act,
                       ox, 0, oz, edits, &nedits, MAX_EDITS);
        for (int e = 0; e < nedits; ++e)
            gm_world_set_block(world, edits[e].wx, edits[e].wy, edits[e].wz, edits[e].id);

        GmPlayerView pv; gm_player_view((const struct PsvPlayer *)&pl, ox, oz, &pv);

        /* ---- optional render + frame hash ---- */
        unsigned long long h = 0ULL;
        if (do_render) {
            CrCamera cam = cam_from_view(&pv, fb_w, fb_h);
            cr_fb_clear(&fb, sky);
            GmMeshView mv; gm_world_mesh_view(world, &cam, fb_w, fb_h, &mv);
            render_world(&fb, &cam, &mv, &atlas, tris);
            h = fb_hash(&fb);
            if (dump_dir && t >= dump_lo && t <= dump_hi) {
                char pth[1024];
                snprintf(pth, sizeof(pth), "%s/c_%06d.ppm", dump_dir, t);
                write_ppm(pth, &fb);
            }
        }

        /* ---- CSV row: world FEET coords (double), motions (double), MC yaw/pitch ---- */
        double x = pl.ent.posX + (double)ox;
        double y = pl.ent.posY;
        double z = pl.ent.posZ + (double)oz;
        fprintf(csv, "%d,%.17g,%.17g,%.17g,%.9g,%.9g,%.17g,%.17g,%.17g,%d,%.9g,%.9g,%d,%llu\n",
                t, x, y, z, (double)pl.yaw, (double)pl.pitch,
                pl.ent.motionX, pl.ent.motionY, pl.ent.motionZ,
                pl.ent.onGround, (double)pl.health, (double)pl.food, -1, h);

        /* ---- FULL STATE VECTOR (JSONL); null == UNSIMULATED on the C side ---- */
        int sel = pl.inv.current_item; if (sel < 0) sel = 0; if (sel > 8) sel = 8;
        ICStack held = isr_get_stack(&pl.inv, sel);
        fprintf(state, "{\"tick\":%d,", t);
        /* player */
        fprintf(state,
            "\"player\":{"
            "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"yaw\":%.9g,\"pitch\":%.9g,"
            "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,\"on_ground\":%d,"
            "\"health\":%.9g,\"food\":%.9g,\"saturation\":%.9g,"
            "\"air\":null,\"fire\":null,\"xp_level\":null,\"xp_frac\":null,"
            "\"fall_distance\":%.9g,"
            "\"sprinting\":%d,\"sneaking\":%d,\"jumping\":%d,"
            "\"held_slot\":%d,\"held_id\":%d,\"held_count\":%d,\"held_meta\":%d,"
            "\"attack_cooldown\":null,\"hurt_time\":null,\"death_time\":null,"
            "\"dead\":%d,\"deaths\":0,\"dim\":0,\"potions\":null},",
            x, y, z, (double)pl.yaw, (double)pl.pitch,
            pl.ent.motionX, pl.ent.motionY, pl.ent.motionZ, pl.ent.onGround,
            (double)pl.health, (double)pl.food, (double)vitals.saturation,
            (double)pl.fall_distance,
            pl.sprinting, tt.sneak, tt.jump,
            sel, held.item, held.count, held.meta,
            (pl.health <= 0.0f) ? 1 : 0);
        /* inventory: 36 main slots, non-empty only (blaze IC_* id namespace) */
        fprintf(state, "\"inventory\":[");
        {
            int first = 1;
            for (int s = 0; s < ISR_MAIN_SLOTS; ++s) {
                ICStack st_i = isr_get_stack(&pl.inv, s);
                if (st_i.item == IC_AIR || st_i.count <= 0) continue;
                fprintf(state, "%s{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d}",
                        first ? "" : ",", s, st_i.item, st_i.count, st_i.meta);
                first = 0;
            }
        }
        fprintf(state, "],");
        /* Entity AI remains outside this small tracer. The explicit clock is enough
         * to verify frozen and advancing clear-weather scenarios without a null sentinel. */
        long long tick_world_time = world_time + (daylight ? t + 1 : 0);
        long long day = tick_world_time / 24000;
        int moon = (int)(day % 8); if (moon < 0) moon += 8;
        fprintf(state,
                "\"entities\":null,"
                "\"time\":{\"world_time\":%lld,\"total_time\":%d,"
                "\"moon_phase\":%d,\"raining\":false,\"thundering\":false}}\n",
                tick_world_time, t + 1, moon);
    }

    fclose(csv);
    fclose(state);
    fprintf(stderr, "trace: wrote %d rows to %s (+ state %s)\n", nticks, out_path, state_path);

    int blocks_rc = 0;
    if (blocks_out) {
        blocks_rc = write_blocks(blocks_out, world, blocks_box);
        if (blocks_rc == 0) {
            long long cells = (long long)(blocks_box[3] - blocks_box[0] + 1) *
                              (blocks_box[4] - blocks_box[1] + 1) *
                              (blocks_box[5] - blocks_box[2] + 1);
            fprintf(stderr, "trace: wrote %lld post-tick block states -> %s\n",
                    cells, blocks_out);
        } else {
            fprintf(stderr, "trace: failed to write post-tick blocks -> %s\n", blocks_out);
        }
    }

    free(tape); free(tris); free(win);
    gm_world_destroy(world);
    cr_fb_free(&fb);
    return blocks_rc == 0 ? 0 : 1;
}
