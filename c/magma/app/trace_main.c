/* app/trace_main.c - TICK-TRACE DIVERGENCE ORACLE, C side.
 *
 * Headless trace runner: replay a FIXED action tape (the same qrl action space the
 * Java game consumes) through the VERIFIED mc-sim player tick inside the magma world,
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
 *   forward back left right jump sneak sprint attack use yaw pitch
 * forward/back/left/right/jump/sneak/sprint/attack/use in {0,1}; yaw/pitch in {-1,0,1}
 * (15-degree quantum steps, matching qrl_client.py / QuantizedRL.applyAction).
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
 * (mc-sim IC_* ids -- a DIFFERENT id namespace from Java's vanilla registry ids; see README),
 * sprint/sneak/jump INTENT (from the tape). UNSIMULATED -> null: air, fire, xp, potion
 * effects, attack cooldown, hurt/death timers, ENTITIES (magma game wires nents=0), and
 * TIME/WEATHER (no world-time progression, no weather).
 *
 * A spawn sidecar (--spawn-out PATH) writes the INITIAL (pre-tick) spawn pose as
 *   x y z yaw pitch
 * (world FEET, MC degrees) so the Java tracer can teleport its player to the SAME tick-0
 * pose before replaying the tape (fair per-tick physics comparison; see README).
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
 */
#include "core/types.h"
#include "game/game.h"
#include "game/view.h"

/* mc-sim: PsvPlayer / Chunk / McSinTable + the verified init helpers. */
#include "player_survival.h"
#include "player_vitals.h"   /* verified vanilla vitals (PvStats, pv_init) */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD   ((float)(M_PI / 180.0))
#define MAX_TRIS  (4 * 1024 * 1024)
#define MAX_EDITS 8
#define AIM_QUANTUM 15.0f     /* qrl 15-degree aim step (QuantizedRL.QUANTUM) */

/* floor-division block coord -> chunk coord (handles negatives). */
static int floordiv16(int a) { return (a >= 0) ? (a >> 4) : -(((-a) + 15) >> 4); }

/* ---- action tape ---- */
typedef struct {
    int forward, back, left, right, jump, sneak, sprint, attack, use, yaw, pitch;
} TapeTick;

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
        int got = sscanf(p, "%d %d %d %d %d %d %d %d %d %d %d",
                         &tt.forward, &tt.back, &tt.left, &tt.right, &tt.jump,
                         &tt.sneak, &tt.sprint, &tt.attack, &tt.use, &tt.yaw, &tt.pitch);
        if (got != 11) {
            fprintf(stderr, "trace: bad tape line (%d/11 fields): %s", got, line);
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
    CrShadeCtx sh_solid = { atlas, fog, 0.f, 0.f, 0, 0, CR_LAYER_SOLID,         0, 0, 0.f };
    CrShadeCtx sh_cmip  = { atlas, fog, 0.f, 0.f, 1, 0, CR_LAYER_CUTOUT_MIPPED, 0, 1, 0.f };
    sh_cmip.depth_lequal = 1;  /* coplanar grass_side_overlay (GL_LEQUAL) */
    CrShadeCtx sh_cut   = { atlas, fog, 0.f, 0.f, 1, 0, CR_LAYER_CUTOUT,        0, 0, 0.f };
    CrShadeCtx sh_trans = { atlas, fog, 0.f, 0.f, 0, 0, CR_LAYER_TRANSLUCENT,   1, 0, 0.f };
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
    c.zfar    = 600.0f;
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
    int         dump_lo = -1, dump_hi = -1;

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
        else { fprintf(stderr,
            "usage: %s --tape PATH [--seed N] [--out CSV] [--state JSONL] [--spawn-out F]\n"
            "          [--w W --h H] [--render 0|1] [--dump-dir DIR --dump-lo L --dump-hi H]\n",
            argv[0]); return 2; }
    }
    if (!tape_path) { fprintf(stderr, "trace: --tape is required\n"); return 2; }
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
    if (spawn_out) {
        FILE *sf = fopen(spawn_out, "w");
        if (sf) {
            fprintf(sf, "%.17g %.17g %.17g %.9g %.9g\n",
                    spawn_x, spawn_y, spawn_z, (double)pl.yaw, (double)pl.pitch);
            fclose(sf);
            fprintf(stderr, "trace: spawn %.3f %.3f %.3f yaw=%.1f pitch=%.1f -> %s\n",
                    spawn_x, spawn_y, spawn_z, (double)pl.yaw, (double)pl.pitch, spawn_out);
        }
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
        /* inventory: 36 main slots, non-empty only (mc-sim IC_* id namespace) */
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
        /* entities + time: UNSIMULATED on the magma game path (nents=0, no world clock) */
        fprintf(state, "\"entities\":null,\"time\":null}\n");
    }

    fclose(csv);
    fclose(state);
    fprintf(stderr, "trace: wrote %d rows to %s (+ state %s)\n", nticks, out_path, state_path);

    free(tape); free(tris); free(win);
    gm_world_destroy(world);
    cr_fb_free(&fb);
    return 0;
}
