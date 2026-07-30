/* Live path gate: real inventory armor -> GmPlayerView.armor_points, and
 * overlay_live 8-corner eye sample against a real GmWorld.
 *
 * Exercises view (gm_runtime_view) + composition (gm_hud_draw /
 * gm_overlay_block_in_hand_live) together. Does not claim Java pixel parity.
 *
 * Build/run: bash ../verify/ui_hud/run_ui_hud_gates.sh
 */
#include "game/runtime.h"
#include "game/hud.h"
#include "game/overlay.h"
#include "game/block_registry.h"
#include "assets/blockmodels.h"
#include "inventory_stack_rules.h"
#include "items_tools_armor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); \
                g_fail = 1; } \
} while (0)

#define W 128
#define H 72
#define GRAY 40

static int region_mean_r(const CrFramebuffer *fb, int x0, int y0, int x1, int y1) {
    long sum = 0; int n = 0;
    for (int y = y0; y < y1 && y < fb->h; ++y)
        for (int x = x0; x < x1 && x < fb->w; ++x) {
            sum += fb->color[y * fb->w + x].r;
            ++n;
        }
    return n ? (int)(sum / n) : 0;
}

static int region_non_gray(const CrFramebuffer *fb, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1 && y < fb->h; ++y)
        for (int x = x0; x < x1 && x < fb->w; ++x) {
            CrRgba c = fb->color[y * fb->w + x];
            if (c.r != GRAY || c.g != GRAY || c.b != GRAY) return 1;
        }
    return 0;
}

static int init_flat(GmRuntime *r) {
    GmConfig c; char err[256];
    gm_config_defaults(&c);
    c.world = GM_WORLD_SUPERFLAT;
    c.view_distance = 1;
    if (!gm_runtime_init(r, &c, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err);
        return 0;
    }
    /* Feet at y=4 so eye (y+1.62) sits inside a solid we place at y=5. */
    gm_runtime_set_pose(r, 8.5, 4.0, 8.5, 0.0f, 0.0f);
    return 1;
}

int main(void) {
    g_fail = 0;
    CHECK(gm_hud_init() == 0, "gm_hud_init");

    GmRuntime r;
    if (!init_flat(&r)) return 1;

    /* ---- (1) Equip real iron armor in inventory; view must report 15 pts ---- */
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_HEAD, 306, 1, 0), "iron helm");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 307, 1, 0), "iron chest");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_LEGS, 308, 1, 0), "iron legs");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_FEET, 309, 1, 0), "iron boots");

    GmPlayerView pv;
    memset(&pv, 0, sizeof pv);
    gm_runtime_view(&r, &pv);
    CHECK(pv.armor_points == 15, "live view: full iron = 15 armor_points");

    /* Composition: HUD paints armor icons from the view field. */
    {
        CrFramebuffer fb;
        fb.w = 854; fb.h = 480;
        fb.color = calloc((size_t)fb.w * fb.h, sizeof(CrRgba));
        fb.depth = calloc((size_t)fb.w * fb.h, sizeof(float));
        for (int i = 0; i < fb.w * fb.h; ++i) {
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
            fb.depth[i] = 1.0f;
        }
        /* Survive vitals for a visible HUD. */
        pv.health = 20.0f; pv.max_health = 20.0f;
        pv.food = 20.0f; pv.max_food = 20.0f;
        pv.air = -1;
        gm_hud_draw(&fb, &pv);
        /* scale2: armor at y=(240-49)*2=382 */
        CHECK(region_non_gray(&fb, 244, 380, 244 + 80, 400),
              "live compose: armor row from inventory-backed view");
        free(fb.color); free(fb.depth);
    }

    /* ---- (1b) Right-click use_action: swords NONE, shield BLOCK; absorption 0 ---- */
    {
        CHECK(gm_runtime_set_inventory(&r, 0, 267, 1, 0), "give iron sword");
        r.player.inv.current_item = 0;
        GmAction use; memset(&use, 0, sizeof use);
        use.use = 1;
        use.hotbar_sel = -1;
        gm_runtime_tick(&r, use);
        gm_runtime_view(&r, &pv);
        CHECK(pv.use_action == 0,
              "live: right-click iron sword does not set use_action");
        CHECK(pv.absorption == 0.0f,
              "live: absorption is 0 without vitals absorption field");

        CHECK(gm_runtime_set_inventory(&r, 0, 442, 1, 0), "give shield");
        r.player.inv.current_item = 0;
        gm_player_dig_reset();
        gm_runtime_tick(&r, use);
        gm_runtime_view(&r, &pv);
        CHECK(pv.use_action == 2, "live: right-click shield sets use_action BLOCK");
        CHECK(pv.use_max == 72000, "live: shield use_max 72000");
    }

    /* ---- (2) overlay_live 8-sample: stone at eye height darkens; air does not ---- */
    {
        /* Clear a volume then fill a stone shell so every eye-corner sample hits. */
        for (int dy = 0; dy <= 2; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    gm_world_set_block(r.world, 8 + dx, 5 + dy, 8 + dz, 0);
        /* Place stone at the eye cell and neighbours the 8-corner walk can hit. */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block(r.world, 8 + dx, 5, 8 + dz, 1); /* stone */

        gm_runtime_view(&r, &pv);
        pv.dead = 0;
        /* Force eye-in-block pose: feet y such that floor(y+eye)=5. */
        pv.x = 8.5f; pv.y = 4.0f; pv.z = 8.5f;
        pv.eye_height = 1.62f;

        CrTexture atlas = bm_atlas();
        CrFramebuffer fb;
        fb.w = W; fb.h = H;
        fb.color = calloc((size_t)W * H, sizeof(CrRgba));
        fb.depth = NULL;
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ 200, 200, 200, 255 };

        int before = region_mean_r(&fb, 0, 0, W, H);
        gm_overlay_block_in_hand_live(&fb, &atlas, r.world, &pv);
        int after_stone = region_mean_r(&fb, 0, 0, W, H);
        CHECK(after_stone < before - 10,
              "live overlay: stone at eye 8-samples darkens frame");

        /* Clear stone -> no overlay. */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block(r.world, 8 + dx, 5, 8 + dz, 0);
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ 200, 200, 200, 255 };
        gm_overlay_block_in_hand_live(&fb, &atlas, r.world, &pv);
        int after_air = region_mean_r(&fb, 0, 0, W, H);
        CHECK(after_air == 200, "live overlay: air at eye is a no-op");

        /* Leaves do not cause suffocation (BlockLeaves override). */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block_meta(r.world, 8 + dx, 5, 8 + dz, 18, 0);
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ 200, 200, 200, 255 };
        gm_overlay_block_in_hand_live(&fb, &atlas, r.world, &pv);
        CHECK(region_mean_r(&fb, 0, 0, W, H) == 200,
              "live overlay: leaves do not suffocate");

        /* Barrier causesSuffocation but EnumBlockRenderType.INVISIBLE -> skip. */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block(r.world, 8 + dx, 5, 8 + dz, 166);
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ 200, 200, 200, 255 };
        gm_overlay_block_in_hand_live(&fb, &atlas, r.world, &pv);
        CHECK(region_mean_r(&fb, 0, 0, W, H) == 200,
              "live overlay: barrier INVISIBLE skips draw");

        /* Chest: missing-model particle is oak planks; !causesSuffocation so
         * no draw — confirm no false positive. */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block_meta(r.world, 8 + dx, 5, 8 + dz, 54, 2);
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ 200, 200, 200, 255 };
        gm_overlay_block_in_hand_live(&fb, &atlas, r.world, &pv);
        CHECK(region_mean_r(&fb, 0, 0, W, H) == 200,
              "live overlay: chest does not suffocate");

        free(fb.color);
    }

    /* ---- (3) GuiGameOver: timer lock, hit regions, respawn transition ---- */
    {
        r.vitals.health = 0.0f;
        r.player.health = 0.0f;
        GmAction idle; memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        gm_runtime_tick(&r, idle);
        CHECK(r.dead && r.deaths >= 1, "live death: dead after lethal tick");
        CHECK(r.death_screen_ticks == 0, "live death: timer starts at 0");

        /* Clicks while locked are ignored. */
        int bx, by0, bx1, by1, bw, bh;
        gm_hud_death_layout(854, 480, &bx, &by0, &bx1, &by1, &bw, &bh);
        (void)bx1; (void)by1; (void)bw; (void)bh;
        GmAction click; memset(&click, 0, sizeof click);
        click.hotbar_sel = -1;
        click.death_click = 1;
        click.death_button = 0;
        gm_runtime_tick(&r, click);
        CHECK(r.dead, "live death: respawn ignored while timer < 20");

        for (int t = 0; t < 19; ++t)
            gm_runtime_tick(&r, idle);
        CHECK(r.death_screen_ticks == 20, "live death: timer reaches 20");
        CHECK(gm_hud_death_buttons_enabled(r.death_screen_ticks),
              "live death: buttons enabled at 20");
        CHECK(gm_hud_death_button_at(854, 480, bx + 10, by0 + 10, 1) == 0,
              "live death: respawn hit region");

        gm_runtime_tick(&r, click);
        CHECK(!r.dead, "live death: respawn clears dead");
        CHECK(r.vitals.health == 20.0f && r.player.health == 20.0f,
              "live death: respawn restores health 20");
        CHECK(r.death_screen_ticks == 0, "live death: timer resets on respawn");
        gm_runtime_view(&r, &pv);
        CHECK(!pv.dead && pv.health == 20.0f, "live death: view is living post-respawn");
    }

    gm_runtime_destroy(&r);

    if (g_fail) {
        fprintf(stderr, "ui_hud live: FAIL\n");
        return 1;
    }
    printf("ui_hud live: PASS\n");
    printf("oracle pixel ROI: see goldens/ + compare_ui_hud_oracle.py\n");
    return 0;
}
