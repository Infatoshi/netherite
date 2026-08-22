/* Same-scene underlay for portal / underwater / fire overlay / first-person
 * hand goldens.
 *
 * Capture geometry (verify/ui_hud/capture_ui_hud_driver.py):
 *   superflat seed 0, CX=CZ=8, PLAT_Y=4
 *   stone pad [5..11]x4x[5..11], air above, stone wall x[6..10] y[5..8] z=11
 *   underwater: glass [6..10]x[5..8]x[6..10], still water [7..9]x[5..7]x[7..9]
 * Pose (8.5, 5.0, 8.5) yaw 0 pitch 0. World time 6000 (noon).
 * fogColor1 pinned from the oracle pair meta (EntityRenderer smoother).
 * hand_* draws world + selection box only (skip_hand/skip_hud); the candidate
 * then gm_hand_draw + gm_hud_draw in frame_capture order.
 */
#include "ui_hud_scene.h"

#include "core/config.h"
#include "game/config.h"
#include "game/game.h"
#include "game/particles_live.h"
#include "game/runtime.h"
#include "game/window_compose.h"
#include "mc_blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_W 854
#define SCENE_H 480
#define SCENE_CX 8
#define SCENE_CZ 8
#define SCENE_PLAT_Y 4

static GmRuntime g_rt;
static int g_rt_ok;

static void fill_box(GmRuntime *r, int x0, int y0, int z0,
                     int x1, int y1, int z1, int id, int meta) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                gm_runtime_set_block(r, x, y, z, id, meta);
}

/* Top-level json.dump indent=2 field. Nested objects use 4+ spaces. */
static int read_meta_float(const char *id, const char *key, float *out) {
    static const char *dirs[] = {
        "../verify/ui_hud/goldens/meta",
        "verify/ui_hud/goldens/meta",
        NULL
    };
    char path[512];
    char buf[65536];
    char needle[96];
    int i;
    if (!id || !key || !out) return 0;
    snprintf(needle, sizeof needle, "\n  \"%s\":", key);
    for (i = 0; dirs[i]; ++i) {
        FILE *f;
        size_t n;
        const char *p;
        char *end;
        float v;
        snprintf(path, sizeof path, "%s/%s.json", dirs[i], id);
        f = fopen(path, "r");
        if (!f) continue;
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        if (n == 0) continue;
        buf[n] = '\0';
        p = strstr(buf, needle);
        if (!p) continue;
        p = strchr(p, ':');
        if (!p) continue;
        p++;
        v = strtof(p, &end);
        if (end == p) continue;
        *out = v;
        return 1;
    }
    return 0;
}

static int ensure_runtime(void) {
    if (g_rt_ok) return 1;
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.seed = 0;
    cfg.mobs = 0;
    cfg.daylight = 0;
    cfg.weather = 0;
    cfg.backend = GM_BACKEND_CPU;
    cfg.view_distance = 8;
    cfg.width = SCENE_W;
    cfg.height = SCENE_H;
    if (!gm_runtime_init(&g_rt, &cfg, err, sizeof err)) {
        fprintf(stderr, "ui_hud_scene: runtime init failed: %s\n", err);
        return 0;
    }
    gm_runtime_set_time(&g_rt, 6000);
    gm_runtime_snapshot_region(&g_rt, 0, 0, 8);
    g_rt_ok = 1;
    return 1;
}

static void build_pad(GmRuntime *r, int want_pool) {
    int x0 = SCENE_CX - 3, x1 = SCENE_CX + 3;
    int z0 = SCENE_CZ - 3, z1 = SCENE_CZ + 3;
    /* Clear the pad volume so a prior underwater fill cannot leak. */
    fill_box(r, x0, SCENE_PLAT_Y + 1, z0, x1, SCENE_PLAT_Y + 4, z1,
             BLK_AIR, 0);
    fill_box(r, x0, SCENE_PLAT_Y, z0, x1, SCENE_PLAT_Y, z1, BLK_STONE, 0);
    fill_box(r, SCENE_CX - 2, SCENE_PLAT_Y + 1, SCENE_CZ + 3,
             SCENE_CX + 2, SCENE_PLAT_Y + 4, SCENE_CZ + 3, BLK_STONE, 0);
    if (!want_pool) return;
    /* capture_ui_hud_driver.build_water_pool */
    fill_box(r, SCENE_CX - 2, SCENE_PLAT_Y + 1, SCENE_CZ - 2,
             SCENE_CX + 2, SCENE_PLAT_Y + 4, SCENE_CZ + 2, BLK_GLASS, 0);
    fill_box(r, SCENE_CX - 1, SCENE_PLAT_Y + 1, SCENE_CZ - 1,
             SCENE_CX + 1, SCENE_PLAT_Y + 3, SCENE_CZ + 1, BLK_WATER, 0);
}

int ui_hud_scene_draw(CrFramebuffer *dst, const char *id) {
    if (!dst || !dst->color || !id) return 0;
    if (dst->w != SCENE_W || dst->h != SCENE_H) return 0;
    int want_portal = !strcmp(id, "overlay_portal_050");
    int want_uw = !strcmp(id, "overlay_underwater");
    int want_fire = !strcmp(id, "overlay_fire");
    int want_hand = !strncmp(id, "hand_", 5);
    if (!want_portal && !want_uw && !want_fire && !want_hand) return 0;
    if (!ensure_runtime()) return 0;

    build_pad(&g_rt, want_uw);
    gm_runtime_set_pose(&g_rt, SCENE_CX + 0.5, (double)(SCENE_PLAT_Y + 1),
                        SCENE_CZ + 0.5, 0.0f, 0.0f);
    gm_world_fill_window(g_rt.world, g_rt.ccx, g_rt.ccz,
                         (struct Chunk *)g_rt.window);
    gm_world_ensure(g_rt.world, g_rt.ccx, g_rt.ccz, 2);

    /* Oracle pair meta: pass_a.fogColor1 (EntityRenderer smoother). */
    if (want_portal)
        cr_cfg_set("fog_c1_init", "0.9986948");
    else if (want_uw)
        cr_cfg_set("fog_c1_init", "0.6447164");
    else if (want_fire)
        /* overlay_fire.json pass_a.fogColor1 (EntityRenderer fogColor1). */
        cr_cfg_set("fog_c1_init", "0.99830884");
    else if (!strcmp(id, "hand_eat_mid"))
        cr_cfg_set("fog_c1_init", "0.999935");
    else
        cr_cfg_set("fog_c1_init", "0.9999515");

    int air = want_uw ? 200 : 300;
    float portal = want_portal ? 0.5f : 0.0f;
    int phase = want_portal ? 0 : 0;
    /* fire=1: Entity.isBurning (Entity.java:2477-2481). texture_animations_pinned
     * 1: MixinPinTextureAnimations + hud_pin fire_frame=0 physical strip row
     * (TextureMap.java:205, TextureAtlasSprite.java:35-36,177-196). */
    gm_runtime_tape_player_view(&g_rt, 0, 0.0f, air, portal,
                                0, phase, 0, 1, want_fire ? 1 : 0, 0, 0, 0, 0.0f, 1.0f);

    GmConfig wcfg;
    gm_config_defaults(&wcfg);
    wcfg.backend = GM_BACKEND_CPU;
    wcfg.width = SCENE_W;
    wcfg.height = SCENE_H;
    wcfg.view_distance = 8;
    char err[256];
    GmWindowCompose *wc = gm_window_compose_open(&wcfg, err, sizeof err);
    if (!wc) {
        fprintf(stderr, "ui_hud_scene: compose open failed: %s\n", err);
        return 0;
    }
    GmParticlesLive particles;
    gm_particles_live_init(&particles, 0);
    gm_window_compose_bind(wc, &g_rt, &particles);

    GmPlayerView pv;
    memset(&pv, 0, sizeof pv);
    gm_runtime_view(&g_rt, &pv);
    gm_runtime_apply_tape_view(&g_rt, &pv);
    /* World projection uses getFOVModifier(pt, true) = fovSetting *
     * fovModifierHand (EntityRenderer.java:529-532). Hand projection stays
     * 70: getFOVModifier(pt, false) skips this term (EntityRenderer.java:804).
     * Read the capture meta's recorded fovModifierHand. Do not hardcode 0.85
     * or the old mid-ease 0.887. */
    if (!strcmp(id, "hand_bow_pull20")) {
        float fm = 0.0f;
        if (read_meta_float(id, "fov_mult", &fm) && fm >= 0.1f && fm <= 1.5f)
            pv.fov_mult = fm;
        else
            fprintf(stderr,
                    "ui_hud_scene: hand_bow_pull20 meta fov_mult missing; "
                    "world FOV stays unzoomed\n");
    }

    GmWindowComposeFrame frame;
    memset(&frame, 0, sizeof frame);
    frame.view = &pv;
    frame.camera_view = &pv;
    frame.partial_ticks = 1.0f;
    frame.interactive = 1; /* selection box: RenderGlobal.drawSelectionBox */
    frame.skip_hand = want_hand;
    frame.skip_hud = want_hand;
    if (!gm_window_compose_draw(wc, &frame, NULL, err, sizeof err)) {
        fprintf(stderr, "ui_hud_scene: compose draw failed: %s\n", err);
        gm_window_compose_close(wc);
        return 0;
    }
    CrFramebuffer *src = gm_window_compose_framebuffer(wc);
    if (!src || !src->color) {
        gm_window_compose_close(wc);
        return 0;
    }
    memcpy(dst->color, src->color,
           (size_t)SCENE_W * (size_t)SCENE_H * sizeof(CrRgba));
    gm_window_compose_close(wc);
    return 1;
}

void ui_hud_scene_shutdown(void) {
    if (!g_rt_ok) return;
    gm_runtime_destroy(&g_rt);
    g_rt_ok = 0;
}
