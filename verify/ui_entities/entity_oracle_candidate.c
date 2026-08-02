/* entity_oracle_candidate.c - render one ui_entities state through the real
 * gm_frame_capture + CPU software-raster path (ghost views / dig_import).
 * Not a hand-painted stand-in. Reads goldens/meta/<id>.json for pose + entity.
 *
 * Usage:
 *   entity_oracle_candidate --state slime_size2 --meta goldens/meta/slime_size2.json \
 *       --ppm /tmp/c.ppm [--w 854 --h 480]
 */
#include "core/types.h"
#include "core/config.h"
#include "game/config.h"
#include "game/frame_capture.h"
#include "game/game.h"
#include "game/player_ctl.h"
#include "game/runtime.h"
#include "game/entity_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

static const char *find_key(const char *j, const char *key) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), ':');
    return p ? p + 1 : NULL;
}
static int j_int(const char *j, const char *key, int def) {
    const char *p = find_key(j, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return (int)strtol(p, NULL, 10);
}
static float j_float(const char *j, const char *key, float def) {
    const char *p = find_key(j, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return (float)strtod(p, NULL);
}
static int j_str_eq(const char *j, const char *key, const char *want) {
    const char *p = find_key(j, key);
    if (!p) return 0;
    while (*p && *p != '"') ++p;
    if (*p != '"') return 0;
    ++p;
    size_t n = strlen(want);
    return strncmp(p, want, n) == 0 && p[n] == '"';
}
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 1 << 20) { fclose(f); return NULL; }
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    b[n] = 0;
    fclose(f);
    return b;
}

static void place_pad(GmWorld *w, int y) {
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            gm_world_set_block(w, x, y, z, 1);
    gm_world_set_block(w, 10, y + 1, 11, 1); /* stone dig target */
    gm_world_set_block(w, 11, y + 1, 11, 2); /* grass dig target */
}

static void inject_from_meta(GmRuntime *rt, const char *state, const char *meta) {
    gm_runtime_ent_views_clear(rt);
    gm_player_dig_reset();
    int is_dig = (strstr(state, "dig_") != NULL) || (strstr(meta, "\"dig\"") != NULL);
    if (is_dig) {
        const char *ds = strstr(meta, "\"dig\"");
        if (!ds) ds = meta;
        GmPlayerCtlSnap snap;
        memset(&snap, 0, sizeof snap);
        int stage = j_int(ds, "stage", 4);
        snap.dig_progress = (float)stage / 10.0f;
        if (snap.dig_progress < 0.1f) snap.dig_progress = 0.1f;
        snap.dig_hx = j_int(ds, "bx", 10);
        snap.dig_hy = j_int(ds, "by", 5);
        snap.dig_hz = j_int(ds, "bz", 11);
        snap.dig_face = j_int(ds, "face", 1);
        snap.dig_hitting = 1;
        /* entity_pin dig_hit freezes N ParticleDigging billboards (not stage). */
        snap.dig_particle_count = j_int(ds, "count", 0);
        gm_player_ctl_dig_import(&snap);
        return;
    }
    GmEntityView ev;
    memset(&ev, 0, sizeof ev);
    const char *es = strstr(meta, "\"entity\"");
    if (!es) es = meta;
    const char *ss = strstr(es, "\"subject\"");
    const char *src = ss ? ss : es;
    ev.x = j_float(src, "x", 8.5f);
    ev.y = j_float(src, "y", 5.0f);
    ev.z = j_float(src, "z", 12.5f);
    ev.yaw = j_float(src, "yaw", 180.0f);
    ev.head_yaw = ev.yaw;
    ev.health = 20.0f;
    ev.ent_id = 42;
    ev.lm_lit = 2;
    ev.lm_mul_r = ev.lm_mul_g = ev.lm_mul_b = 1.0f;

    if (j_str_eq(es, "type", "slime") || (strstr(state, "slime") == state)) {
        ev.type = 35;
        ev.item_meta = j_int(es, "size", 2);
        if (ev.item_meta < 1) ev.item_meta = 1;
        ev.squish = j_float(es, "squish", 0.0f);
    } else if (j_str_eq(es, "type", "magma_cube") || (strstr(state, "magma") == state)) {
        ev.type = 27;
        ev.item_meta = j_int(es, "size", 2);
        if (ev.item_meta < 1) ev.item_meta = 1;
        ev.squish = j_float(es, "squish", 0.0f);
    } else if (j_str_eq(es, "type", "dragon") || strstr(state, "dragon_death")) {
        ev.type = 9;
        ev.death_ticks = j_int(es, "death_ticks", 50);
        /* Match qrl render pin: keep health full so onDeathUpdate/explosion
         * particles do not run; only deathTicks drives dissolve + rays. */
        ev.health = 200.0f;
    } else if (j_str_eq(es, "type", "small_fireball") || strstr(state, "fireball_small")) {
        ev.type = 30;
        ev.item_id = 385;
        ev.item_meta = 0;
    } else if (j_str_eq(es, "type", "dragon_fireball") || strstr(state, "fireball_dragon")) {
        /* RenderDragonFireball: scale 2.0 + entity/enderdragon/dragon_fireball.png
         * (item atlas id 9003). Not fire_charge (385) and not on-fire layers. */
        ev.type = 33;
        ev.item_id = 9003;
        ev.item_meta = 0;
    } else if (j_str_eq(es, "type", "xp_orb") || strstr(state, "xp_orb")) {
        ev.type = 21;
        ev.item_id = j_int(es, "value", 7);
        ev.item_meta = j_int(es, "color", 0);
        ev.age = j_int(es, "age", 0);
    } else {
        fprintf(stderr, "unknown entity for state %s\n", state);
        return;
    }
    gm_runtime_ent_view(rt, &ev);
}

static int copy_file(const char *src, const char *dst) {
    FILE *sf = fopen(src, "rb");
    if (!sf) return 0;
    FILE *df = fopen(dst, "wb");
    if (!df) { fclose(sf); return 0; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, sf)) > 0) {
        if (fwrite(buf, 1, n, df) != n) { fclose(sf); fclose(df); return 0; }
    }
    fclose(sf); fclose(df);
    return 1;
}

int main(int argc, char **argv) {
    const char *state = "unknown", *meta_path = NULL, *ppm = "/tmp/entity_oracle_c.ppm";
    int W = 854, H = 480;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--state") && i + 1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "--meta") && i + 1 < argc) meta_path = argv[++i];
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) H = atoi(argv[++i]);
    }
    if (!meta_path) {
        fprintf(stderr, "usage: %s --meta PATH [--state ID] [--ppm PATH]\n", argv[0]);
        return 2;
    }
    char *meta = read_file(meta_path);
    if (!meta) { fprintf(stderr, "cannot read meta %s\n", meta_path); return 1; }

    float px = 8.5f, py = 5.0f, pz = 8.5f, yaw = 0.0f, pitch = 10.0f;
    const char *pose_sec = strstr(meta, "\"pose\"");
    if (pose_sec) {
        px = j_float(pose_sec, "x", px);
        py = j_float(pose_sec, "y", py);
        pz = j_float(pose_sec, "z", pz);
        yaw = j_float(pose_sec, "yaw", yaw);
        pitch = j_float(pose_sec, "pitch", pitch);
    }

    char outdir[] = "/tmp/magma_entity_oracle_frames";
    if (mkdir(outdir, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s failed\n", outdir);
        free(meta);
        return 1;
    }

    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.backend = GM_BACKEND_CPU;
    cfg.render = GM_RENDER_OFF;
    cfg.width = W;
    cfg.height = H;
    cfg.view_distance = 8;
    cfg.daylight = 0;
    cfg.weather = 0;
    cfg.mobs = 0;
    cfg.headless = 1;
    cfg.frames_out_dir = outdir;
    cfg.frame_every = 1;
    cfg.frame_offset = 0;

    char err[256];
    GmRuntime rt;
    if (!gm_runtime_init(&rt, &cfg, err, (int)sizeof err)) {
        fprintf(stderr, "runtime: %s\n", err); free(meta); return 1;
    }
    place_pad(rt.world, 4);
    gm_world_ensure(rt.world, 0, 0, 4);
    /* Dragon states need a wider mesh window around z=-40 / y=70. */
    if (strstr(state, "dragon")) {
        for (int cx = -4; cx <= 2; ++cx)
            for (int cz = -4; cz <= 2; ++cz)
                gm_world_ensure(rt.world, cx, cz, 2);
        for (int x = -8; x <= 8; ++x)
            for (int z = -50; z <= 20; ++z)
                gm_world_set_block(rt.world, x, 60, z, 121); /* end_stone */
    }
    gm_runtime_set_time(&rt, 6000);
    rt.clock.freeze_daylight = 1;

    rt.player.ent.posX = (double)px;
    rt.player.ent.posY = (double)py;
    rt.player.ent.posZ = (double)pz;
    rt.player.yaw = yaw;
    rt.player.pitch = pitch;
    rt.player.ent.onGround = 1;
    rt.ox = rt.oz = 0;
    /* Pin texture animations + creative (no hunger flash). */
    gm_runtime_tape_player_view(&rt, 0, 0.0f, 300, 0.0f, 0, 0, 0,
                                1 /* texture_animations_pinned */,
                                0, 1 /* creative */, 0, 0, 0.0f, 1.0f);

    /* Shared frame_capture reads strip_overlays / no_hand from the registry
     * (no longer getenv). Arm them here before open/write. */
    cr_cfg_set("strip_overlays", "1");
    cr_cfg_set("no_hand", "1");

    inject_from_meta(&rt, state, meta);

    GmFrameCapture *fc = gm_frame_capture_open(&cfg, err, (int)sizeof err);
    if (!fc) {
        fprintf(stderr, "frame_capture_open: %s\n", err);
        free(meta); gm_runtime_destroy(&rt); return 1;
    }
    GmAction act;
    memset(&act, 0, sizeof act);
    /* Re-inject ghosts immediately before write (no tick clears them). */
    inject_from_meta(&rt, state, meta);
    if (!gm_frame_capture_write(fc, &rt, &act, 1, err, (int)sizeof err)) {
        fprintf(stderr, "frame_capture_write: %s\n", err);
        gm_frame_capture_close(fc); free(meta); gm_runtime_destroy(&rt); return 1;
    }
    gm_frame_capture_close(fc);

    char src[512];
    snprintf(src, sizeof src, "%s/frame_000000.ppm", outdir);
    if (!copy_file(src, ppm)) {
        /* try any frame_*.ppm */
        char cmd[768];
        snprintf(cmd, sizeof cmd,
                 "f=$(ls %s/frame_*.ppm 2>/dev/null | head -1); "
                 "[ -n \"$f\" ] && cp -f \"$f\" '%s'", outdir, ppm);
        if (system(cmd) != 0) {
            fprintf(stderr, "no frame ppm in %s\n", outdir);
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }
    printf("wrote %s (state=%s pose=%.2f,%.2f,%.2f yaw=%.1f pitch=%.1f)\n",
           ppm, state, px, py, pz, yaw, pitch);
    free(meta);
    gm_runtime_destroy(&rt);
    return 0;
}
