/* game/test_hud.c - standalone self-consistency test for the survival HUD.
 * Build+run via game/test_hud.sh (no Makefile). Dumps game/hud_preview.ppm. */
#include "game/hud.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 854
#define H 480
#define GRAY 128

static int is_gray(CrRgba c) {
    return c.r == GRAY && c.g == GRAY && c.b == GRAY;
}

/* true if ANY pixel in the rect differs from the gray background */
static int region_changed(const CrFramebuffer *fb, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->w) x1 = fb->w;
    if (y1 > fb->h) y1 = fb->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (!is_gray(fb->color[y * fb->w + x])) return 1;
    return 0;
}

int main(void) {
    CrFramebuffer fb;
    fb.w = W; fb.h = H;
    fb.color = malloc((size_t)W * H * sizeof(CrRgba));
    fb.depth = malloc((size_t)W * H * sizeof(float));
    if (!fb.color || !fb.depth) { fprintf(stderr, "alloc fail\n"); return 1; }
    for (int i = 0; i < W * H; i++) {
        fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        fb.depth[i] = 1.0f;
    }
    /* snapshot depth to prove gm_hud_draw never touches it */
    float *depth_before = malloc((size_t)W * H * sizeof(float));
    memcpy(depth_before, fb.depth, (size_t)W * H * sizeof(float));

    GmPlayerView pv;
    memset(&pv, 0, sizeof pv);
    pv.health = 15; pv.max_health = 20;   /* 7 full + 1 half heart */
    pv.food = 8;    pv.max_food = 20;      /* 4 full haunches       */
    pv.xp_frac = 0.6f; pv.xp_level = 7;
    pv.air = 123;
    pv.hotbar_sel = 3;
    /* dirt / cobble / crafting-table exercise isometric block icons;
     * stick (280) exercises the flat 2D item path. */
    pv.hotbar_ids[0] = 3;  pv.hotbar_counts[0] = 64;  /* dirt */
    pv.hotbar_ids[1] = 4;  pv.hotbar_counts[1] = 32;  /* cobble */
    pv.hotbar_ids[2] = 58; pv.hotbar_counts[2] = 1;   /* crafting table */
    pv.hotbar_ids[3] = 280; pv.hotbar_counts[3] = 12; /* stick (flat) */
    pv.hotbar_ids[8] = 9;  pv.hotbar_counts[8] = 1;

    int init_rc = gm_hud_init();
    {
        GmHudState hs = {0};
        GmPlayerView hv = {0};
        hv.health = 15.0f;
        gm_hud_state_step(&hs, &hv, 0);
        hv.health = 11.333333f; hv.hurt_time = 9;
        gm_hud_state_step(&hs, &hv, 1);
        if (hv.hud_health != 12 || hv.hud_last_health != 15 || hv.hud_flash) {
            fprintf(stderr, "FAIL: vanilla ceil/damage heart state\n");
            return 1;
        }
        gm_hud_state_step(&hs, &hv, 4);
        if (!hv.hud_flash) {
            fprintf(stderr, "FAIL: healthUpdateCounter blink phase\n");
            return 1;
        }
        memset(&hs, 0, sizeof hs);
        memset(&hv, 0, sizeof hv);
        hv.health = 20.0f; hv.hud_transition_lead = 1;
        gm_hud_state_step(&hs, &hv, 84);
        hv.health = 15.0f; hv.hurt_time = 9;
        gm_hud_state_step(&hs, &hv, 86);
        hv.hurt_time = 7;
        gm_hud_state_step(&hs, &hv, 88);
        if (!hv.hud_flash) {
            fprintf(stderr, "FAIL: post-tick tape heart-flash lead\n");
            return 1;
        }
    }
    gm_hud_draw(&fb, &pv);

    /* Potion-driven heart rows and renderPotionEffects use different atlas
     * pixels from normal hearts and draw the harmful icon at top-right. */
    CrRgba normal_hearts[162 * 18];
    for (int y = 0; y < 18; ++y)
        memcpy(&normal_hearts[y * 162], &fb.color[(402 + y) * W + 244],
               162 * sizeof(CrRgba));
    for (int i = 0; i < W * H; ++i)
        fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
    pv.potion_count = 1;
    pv.potions[0] = (GmPotionEffectView){20, 0, 157};
    gm_hud_draw(&fb, &pv);
    int wither_changed = 0;
    for (int y = 0; y < 18; ++y)
        if (memcmp(&normal_hearts[y * 162], &fb.color[(402 + y) * W + 244],
                   162 * sizeof(CrRgba)) != 0) wither_changed = 1;
    if (!wither_changed) {
        fprintf(stderr, "FAIL: wither hearts equal normal hearts\n"); return 1;
    }
    if (!region_changed(&fb, 804, 54, 852, 102)) {
        fprintf(stderr, "FAIL: harmful potion HUD icon missing\n"); return 1;
    }
    CrRgba wither_hearts[162 * 18];
    for (int y = 0; y < 18; ++y)
        memcpy(&wither_hearts[y * 162], &fb.color[(402 + y) * W + 244],
               162 * sizeof(CrRgba));
    for (int i = 0; i < W * H; ++i)
        fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
    pv.potions[0] = (GmPotionEffectView){19, 0, 157};
    gm_hud_draw(&fb, &pv);
    int poison_changed = 0;
    for (int y = 0; y < 18; ++y)
        if (memcmp(&wither_hearts[y * 162], &fb.color[(402 + y) * W + 244],
                   162 * sizeof(CrRgba)) != 0) poison_changed = 1;
    if (!poison_changed) {
        fprintf(stderr, "FAIL: poison hearts equal wither hearts\n"); return 1;
    }

    /* --- asserts --- */
    int fail = 0;

    /* (1) init ok */
    if (init_rc != 0) { fprintf(stderr, "FAIL: gm_hud_init returned %d\n", init_rc); fail = 1; }
    if (!gm_gui_item_icon(NULL, 276, 0, 0, 0, 1)) {
        fprintf(stderr, "FAIL: diamond sword has no GUI atlas icon\n"); fail = 1;
    }

    /* (2) crosshair center differs from gray */
    if (!region_changed(&fb, W/2 - 8, H/2 - 8, W/2 + 8, H/2 + 8)) {
        fprintf(stderr, "FAIL: crosshair region unchanged\n"); fail = 1;
    }

    /* (3) hotbar row near bottom-center differs from gray */
    if (!region_changed(&fb, W/2 - 60, H - 30, W/2 + 60, H - 2)) {
        fprintf(stderr, "FAIL: hotbar region unchanged\n"); fail = 1;
    }

    /* (3b) isometric block icons: dirt slot (first) must have non-gray pixels
     * inside the icon cell, and more structure than a flat monochrome pip. */
    {
        const int scale = (H / 240) > 1 ? (H / 240) : 1;
        const int hb_w = 182 * scale; /* widgets hotbar width */
        const int hb_x = (W - hb_w) / 2;
        const int hb_y = H - 22 * scale;
        const int ix = hb_x + 3 * scale, iy = hb_y + 3 * scale;
        int icon_px = 0, distinct = 0;
        unsigned seen[8] = {0}; int nseen = 0;
        for (int y = iy; y < iy + 16 * scale && y < H; y++)
            for (int x = ix; x < ix + 16 * scale && x < W; x++) {
                CrRgba c = fb.color[y * W + x];
                if (is_gray(c)) continue;
                icon_px++;
                unsigned rgb = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
                int hit = 0;
                for (int k = 0; k < nseen; k++) if (seen[k] == rgb) { hit = 1; break; }
                if (!hit && nseen < 8) seen[nseen++] = rgb;
            }
        distinct = nseen;
        if (icon_px < 10) {
            fprintf(stderr, "FAIL: dirt hotbar icon empty (px=%d)\n", icon_px); fail = 1;
        }
        if (distinct < 2) {
            fprintf(stderr, "FAIL: dirt icon not multi-shade iso (distinct=%d)\n", distinct); fail = 1;
        }
    }

    /* (4a) heart region on the left differs from gray */
    if (!region_changed(&fb, 0, H - 120, W/2, H - 40)) {
        fprintf(stderr, "FAIL: heart region unchanged\n"); fail = 1;
    }

    /* (4b) recorded partial air draws the vanilla bubble row above hunger. */
    if (!region_changed(&fb, W/2, H - 105, W/2 + 190, H - 90)) {
        fprintf(stderr, "FAIL: air bubble region unchanged\n"); fail = 1;
    }

    /* (4c) far-left-above area (no HUD) stays gray */
    if (region_changed(&fb, 0, 0, 200, 120)) {
        fprintf(stderr, "FAIL: top-left area was drawn on (should be clean)\n"); fail = 1;
    }

    /* (5) depth untouched */
    if (memcmp(depth_before, fb.depth, (size_t)W * H * sizeof(float)) != 0) {
        fprintf(stderr, "FAIL: gm_hud_draw modified fb->depth\n"); fail = 1;
    }

    /* (6) XP fill columns: vanilla (int)(frac * 183), not round(182*frac). */
    if (gm_hud_xp_fill_cols(0.0f) != 0 || gm_hud_xp_fill_cols(1.0f) != 182 ||
        gm_hud_xp_fill_cols(0.5f) != 91) {
        fprintf(stderr, "FAIL: xp fill cols %d/%d/%d\n",
                gm_hud_xp_fill_cols(0.0f), gm_hud_xp_fill_cols(0.5f),
                gm_hud_xp_fill_cols(1.0f));
        fail = 1;
    }

    /* (6b) Creative retains hotbar/crosshair but suppresses survival stats. */
    {
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        pv.creative = 1;
        pv.armor_points = 15;
        gm_hud_draw(&fb, &pv);
        if (!region_changed(&fb, W/2 - 60, H - 30, W/2 + 60, H - 2) ||
            !region_changed(&fb, W/2 - 8, H/2 - 8, W/2 + 8, H/2 + 8)) {
            fprintf(stderr, "FAIL: creative hotbar or crosshair missing\n");
            fail = 1;
        }
        if (region_changed(&fb, 244, 380, 610, 432)) {
            fprintf(stderr, "FAIL: creative HUD drew survival stats\n");
            fail = 1;
        }
        pv.creative = 0;
        pv.armor_points = 0;
    }

    /* (7) Durability strip width + green->red hue (wood pick max 59). */
    {
        int w_half = gm_hud_durability_width(270, 30); /* ~half worn */
        int w_full = gm_hud_durability_width(270, 1);
        int w_gone = gm_hud_durability_width(270, 59);
        unsigned char r0, g0, b0, r1, g1, b1;
        gm_hud_durability_rgb(270, 1, &r0, &g0, &b0);
        gm_hud_durability_rgb(270, 50, &r1, &g1, &b1);
        if (w_half < 5 || w_half > 8 || w_full != 13 || w_gone != 0) {
            fprintf(stderr, "FAIL: durability width half=%d full=%d gone=%d\n",
                    w_half, w_full, w_gone); fail = 1;
        }
        if (!(g0 > r0 && r1 > g1)) {
            fprintf(stderr, "FAIL: durability hue fresh=%d,%d,%d worn=%d,%d,%d\n",
                    r0, g0, b0, r1, g1, b1); fail = 1;
        }
    }

    /* (8) Armor row above hearts (sh-49) when points > 0. */
    {
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        pv.potion_count = 0;
        pv.armor_points = 15; /* full iron set */
        gm_hud_draw(&fb, &pv);
        /* armor icons: y = (480/2 wait) sh=240 scale=2 -> armor_y = (240-49)*2=382 */
        if (!region_changed(&fb, 244, 380, 244 + 80, 400)) {
            fprintf(stderr, "FAIL: armor row missing at sh-49\n"); fail = 1;
        }
        pv.armor_points = 0;
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        gm_hud_draw(&fb, &pv);
        if (region_changed(&fb, 244, 380, 244 + 80, 400)) {
            fprintf(stderr, "FAIL: armor row drawn with 0 points\n"); fail = 1;
        }
    }

    /* (9) Boss bar progress columns and name region. */
    {
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        gm_hud_set_boss(1, 0.5f);
        gm_hud_draw(&fb, &pv);
        /* bar at y=12*2=24, x=(213-91)*2=244 */
        if (!region_changed(&fb, 244, 22, 244 + 200, 40)) {
            fprintf(stderr, "FAIL: boss bar missing\n"); fail = 1;
        }
        gm_hud_set_boss(0, 1.0f);
    }

    /* (10) GuiGameOver replaces normal HUD (gradient + title + buttons). */
    {
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        GmPlayerView dead = pv;
        dead.dead = 1; dead.deaths = 3; dead.score = 0; dead.death_ticks = 0;
        gm_hud_draw(&fb, &dead);
        CrRgba c = fb.color[(H / 2) * W + (W / 2)];
        if (c.r <= c.g || c.r <= c.b) {
            fprintf(stderr, "FAIL: death gradient not red-tinted (%d,%d,%d)\n",
                    c.r, c.g, c.b); fail = 1;
        }
        /* Respawn button origin at scale 2: (226, 264). */
        CrRgba edge = fb.color[264 * W + 427];
        if (edge.r > 30 || edge.g > 30 || edge.b > 30) {
            fprintf(stderr, "FAIL: death button border not dark (%d,%d,%d)\n",
                    edge.r, edge.g, edge.b); fail = 1;
        }
        if (gm_hud_death_button_at(W, H, 300, 280, 1) != 0) {
            fprintf(stderr, "FAIL: respawn hit region\n"); fail = 1;
        }
    }

    /* (11) Hunger-poison sprites differ from normal haunches. */
    {
        CrRgba normal_food[162 * 18];
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        pv.dead = 0; pv.potion_count = 0; pv.food = 8;
        gm_hud_draw(&fb, &pv);
        /* hunger right side near bottom: roughly x 450-610, y 400-420 */
        for (int y = 0; y < 18; ++y)
            memcpy(&normal_food[y * 162], &fb.color[(402 + y) * W + 450],
                   162 * sizeof(CrRgba));
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        pv.potion_count = 1;
        pv.potions[0] = (GmPotionEffectView){17, 0, 200};
        gm_hud_draw(&fb, &pv);
        int hunger_changed = 0;
        for (int y = 0; y < 18; ++y)
            if (memcmp(&normal_food[y * 162], &fb.color[(402 + y) * W + 450],
                       162 * sizeof(CrRgba)) != 0) hunger_changed = 1;
        if (!hunger_changed) {
            fprintf(stderr, "FAIL: hunger-poison sprites equal normal\n"); fail = 1;
        }
        pv.potion_count = 0;
    }

    /* --- dump PPM (P6) --- */
    FILE *f = fopen("game/hud_preview.ppm", "wb");
    if (!f) { fprintf(stderr, "cannot open ppm\n"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = { fb.color[i].r, fb.color[i].g, fb.color[i].b };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);

    free(fb.color); free(fb.depth); free(depth_before);

    if (fail) { fprintf(stderr, "HUD TEST: FAIL\n"); return 1; }
    printf("HUD TEST: PASS (init=0, crosshair+hotbar+hearts drew, top-left clean, depth intact)\n");
    printf("wrote game/hud_preview.ppm\n");
    return 0;
}
