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
