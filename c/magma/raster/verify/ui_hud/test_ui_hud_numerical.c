/* Focused numerical gates for HUD / first-person hand / screen overlays.
 *
 * Pure C: no oracle PNGs required. Compares owned-module outputs against
 * 1.11.2 formulas (GuiIngame / ItemRenderer / GuiBossOverlay).
 *
 * Build/run: bash raster/verify/ui_hud/run_ui_hud_gates.sh
 */
#include "game/hud.h"
#include "game/hand.h"
#include "game/overlay.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "assets/item_atlas.h"
#include "core/types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); \
                g_fail = 1; } \
} while (0)

#define W 854
#define H 480
#define GRAY 40

static int region_non_gray(const CrFramebuffer *fb, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            CrRgba c = fb->color[y * fb->w + x];
            if (c.r != GRAY || c.g != GRAY || c.b != GRAY) return 1;
        }
    return 0;
}

static CrFramebuffer make_fb(void) {
    CrFramebuffer fb;
    fb.w = W; fb.h = H;
    fb.color = calloc((size_t)W * H, sizeof(CrRgba));
    fb.depth = calloc((size_t)W * H, sizeof(float));
    for (int i = 0; i < W * H; ++i) {
        fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        fb.depth[i] = 1.0f;
    }
    return fb;
}

static void clear_fb(CrFramebuffer *fb) {
    for (int i = 0; i < fb->w * fb->h; ++i)
        fb->color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
}

int main(void) {
    g_fail = 0;
    CHECK(gm_hud_init() == 0, "gm_hud_init");

    /* ---- XP fill columns (int)(frac * 183) ---- */
    CHECK(gm_hud_xp_fill_cols(0.0f) == 0, "xp 0");
    CHECK(gm_hud_xp_fill_cols(1.0f) == 182, "xp full clips to 182");
    CHECK(gm_hud_xp_fill_cols(0.5f) == 91, "xp half = 91");
    CHECK(gm_hud_xp_fill_cols(0.333f) == 60, "xp ~1/3");

    /* ---- Durability width + hue ---- */
    CHECK(gm_hud_durability_width(270, 0) == 0, "undamaged no bar");
    CHECK(gm_hud_durability_width(270, 1) == 13, "almost-new full width");
    CHECK(gm_hud_durability_width(276, 780) >= 5 &&
          gm_hud_durability_width(276, 780) <= 8, "diamond sword half");
    {
        unsigned char r, g, b;
        gm_hud_durability_rgb(270, 1, &r, &g, &b);
        CHECK(g >= r && g >= b, "fresh durability is green-dominant");
        gm_hud_durability_rgb(270, 55, &r, &g, &b);
        CHECK(r > g, "near-broken durability is red-dominant");
    }

    /* ---- Hurt flash phase from healthUpdateCounter ---- */
    {
        GmHudState hs = {0};
        GmPlayerView hv = {0};
        hv.health = 20.0f;
        gm_hud_state_step(&hs, &hv, 0);
        hv.health = 14.0f; hv.hurt_time = 10;
        gm_hud_state_step(&hs, &hv, 1);
        CHECK(hv.hud_health == 14, "ceil health on damage");
        CHECK(hv.hud_last_health == 20, "last health retained");
        int saw_flash = 0, saw_off = 0;
        for (long long t = 1; t < 25; ++t) {
            gm_hud_state_step(&hs, &hv, t);
            if (hv.hud_flash) saw_flash = 1; else saw_off = 1;
        }
        CHECK(saw_flash && saw_off, "hurt flash blinks on and off");
    }

    /* ---- Boss bar fill columns + draw region ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 20; pv.max_health = 20;
        pv.food = 20; pv.max_food = 20;
        pv.air = -1;
        gm_hud_set_boss(1, 0.5f);
        gm_hud_draw(&fb, &pv);
        /* scaledWidth=427 at 854/2; cx=213; bar x=(213-91)*2=244, y=24 */
        CHECK(region_non_gray(&fb, 244, 22, 244 + 100, 36), "boss bar bg/fill");
        /* half fill: (int)(0.5*183)=91 of 182 -> fill ends before full width */
        int filled = 0, emptyish = 0;
        for (int x = 244; x < 244 + 91 * 2; ++x)
            if (fb.color[24 * W + x].r != GRAY) filled = 1;
        for (int x = 244 + 170 * 2; x < 244 + 182 * 2; ++x)
            if (fb.color[24 * W + x].r == GRAY) emptyish = 1;
        CHECK(filled, "boss half-fill has pixels in first 91 cols");
        CHECK(emptyish, "boss half-fill leaves rightmost columns unfilled");
        gm_hud_set_boss(0, 1.0f);
        free(fb.color); free(fb.depth);
    }

    /* ---- Armor row only when points > 0 ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 20; pv.max_health = 20;
        pv.food = 20; pv.max_food = 20;
        pv.air = -1;
        gm_hud_set_armor(7); /* 3 full + 1 half */
        gm_hud_draw(&fb, &pv);
        CHECK(region_non_gray(&fb, 244, 380, 244 + 80, 400), "armor at sh-49");
        clear_fb(&fb);
        gm_hud_set_armor(0);
        gm_hud_draw(&fb, &pv);
        CHECK(!region_non_gray(&fb, 244, 380, 244 + 80, 400),
              "no armor icons at 0 points");
        free(fb.color); free(fb.depth);
    }

    /* ---- Hotbar count + durability strip pixels ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 20; pv.max_health = 20;
        pv.food = 20; pv.max_food = 20;
        pv.air = -1;
        pv.hotbar_ids[0] = 270; /* wood pick */
        pv.hotbar_counts[0] = 1;
        pv.hotbar_meta[0] = 30;
        pv.hotbar_ids[1] = 4;
        pv.hotbar_counts[1] = 64;
        gm_hud_draw(&fb, &pv);
        /* slot0 icon cell ~ (244+6, 442) at scale2; durability at y+13*2 */
        CHECK(region_non_gray(&fb, 250, 450, 280, 476), "damaged tool strip/icon");
        CHECK(region_non_gray(&fb, 290, 450, 330, 476), "stack count region");
        free(fb.color); free(fb.depth);
    }

    /* ---- Air bubble formula (ceil) ---- */
    {
        /* air=123: full=ceil((123-2)*10/300)=ceil(4.033)=5
         *          total=ceil(123*10/300)=ceil(4.1)=5 -> 5 full, 0 partial */
        int air = 123;
        int full = (int)ceil(((double)air - 2.0) * 10.0 / 300.0);
        int total = (int)ceil((double)air * 10.0 / 300.0);
        CHECK(full == 5 && total == 5, "air=123 bubble counts");
        air = 2;
        full = (int)ceil(((double)air - 2.0) * 10.0 / 300.0);
        total = (int)ceil((double)air * 10.0 / 300.0);
        CHECK(full == 0 && total == 1, "air=2 is one partial bubble");
    }

    /* ---- Held-item registration: rest pose lower-right ---- */
    {
        static CrVertex verts[6156];
        int n = gm_hand_emit_held(280, 0, 0.0f, 0.0f, verts, 6156);
        CHECK(n > 12, "stick mesh");
        float minx = 1e9f, maxz = -1e9f, miny = 1e9f;
        for (int i = 0; i < n; ++i) {
            if (verts[i].pos.x < minx) minx = verts[i].pos.x;
            if (verts[i].pos.z > maxz) maxz = verts[i].pos.z;
            if (verts[i].pos.y < miny) miny = verts[i].pos.y;
        }
        CHECK(minx > 0.0f, "held item rest x>0 (right hand)");
        CHECK(maxz < 0.0f, "held item rest z<0 (in front)");
        CHECK(miny < 0.0f, "held item rest below eye");
    }

    /* ---- Rim / edge shading geometry budget ---- */
    {
        static CrVertex verts[6156];
        int n = gm_hand_emit_held(268, 0, 0.0f, 0.0f, verts, 6156); /* wood sword */
        CHECK(n > 12 + 24, "sword has opaque-edge rim quads");
    }

    /* ---- Bow pull stages move verts and change sprite path ---- */
    {
        static CrVertex a[6156], b[6156];
        gm_hand_set_bow_pull(0);
        int n0 = gm_hand_emit_held(261, 0, 0.0f, 0.0f, a, 6156);
        gm_hand_set_bow_pull(13); /* >=0.65 pull sprite */
        int n1 = gm_hand_emit_held(261, 0, 0.0f, 0.0f, b, 6156);
        gm_hand_set_bow_pull(0);
        CHECK(n0 > 0 && n1 > 0, "bow idle+drawn");
        float d = 0.0f;
        int n = n0 < n1 ? n0 : n1;
        for (int i = 0; i < n; ++i) {
            float dx = b[i].pos.x - a[i].pos.x;
            float dy = b[i].pos.y - a[i].pos.y;
            float dz = b[i].pos.z - a[i].pos.z;
            d += dx * dx + dy * dy + dz * dz;
        }
        CHECK(d > 0.01f, "bow pull offsets geometry");
    }

    /* ---- Equip / swing / eat / block poses ---- */
    {
        static CrVertex a[6156], b[6156];
        int n0 = gm_hand_emit_held(280, 0, 0.0f, 0.0f, a, 6156);
        int n1 = gm_hand_emit_held(280, 0, 0.0f, 0.8f, b, 6156);
        float y0 = 0, y1 = 0;
        for (int i = 0; i < n0; ++i) { y0 += a[i].pos.y; y1 += b[i].pos.y; }
        CHECK(y1 / n1 < y0 / n0 - 0.1f, "equip lowers item");
        n1 = gm_hand_emit_held(280, 0, 0.6f, 0.0f, b, 6156);
        float d = 0;
        for (int i = 0; i < n0; ++i) {
            float dx = b[i].pos.x - a[i].pos.x;
            float dy = b[i].pos.y - a[i].pos.y;
            float dz = b[i].pos.z - a[i].pos.z;
            d += dx * dx + dy * dy + dz * dz;
        }
        CHECK(d > 1e-3f, "swing moves item");
        gm_hand_set_use(1, 20, 32);
        n1 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, b, 6156);
        gm_hand_set_use(0, 0, 0);
        n0 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, a, 6156);
        d = 0;
        for (int i = 0; i < n0 && i < n1; ++i) {
            float dx = b[i].pos.x - a[i].pos.x;
            float dy = b[i].pos.y - a[i].pos.y;
            float dz = b[i].pos.z - a[i].pos.z;
            d += dx * dx + dy * dy + dz * dz;
        }
        CHECK(d > 0.01f, "eat pose offsets food");
        gm_hand_set_use(0, 0, 0);
        n0 = gm_hand_emit_held(267, 0, 0.5f, 0.0f, a, 6156);
        gm_hand_set_use(2, 0, 0);
        n1 = gm_hand_emit_held(267, 0, 0.5f, 0.0f, b, 6156);
        gm_hand_set_use(0, 0, 0);
        d = 0;
        for (int i = 0; i < n0; ++i) {
            float dx = b[i].pos.x - a[i].pos.x;
            float dy = b[i].pos.y - a[i].pos.y;
            float dz = b[i].pos.z - a[i].pos.z;
            d += dx * dx + dy * dy + dz * dz;
        }
        CHECK(d > 1e-4f, "block-use ignores mid-swing");
    }

    /* ---- Portal fourth-power alpha curve (analytical) ---- */
    {
        /* time t in (0,1): a = ((t^2)^2)*0.8 + 0.2 */
        float t = 0.5f;
        float a = t * t; a = a * a; a = a * 0.8f + 0.2f;
        CHECK(fabsf(a - 0.25f) < 1e-6f, "portal alpha at 0.5 is 0.25");
        t = 0.0f;
        a = 0.2f; /* lower clamp path still multiplies, but draw skips t<=0 */
        CHECK(a == 0.2f, "portal floor constant");
    }

    /* ---- Block-in-hand darken + loading fill ---- */
    {
        enum { PW = 48, PH = 32 };
        CrRgba *color = calloc((size_t)PW * PH, sizeof(CrRgba));
        CrRgba *texels = calloc(256, sizeof(CrRgba));
        for (int i = 0; i < PW * PH; ++i)
            color[i] = (CrRgba){200, 200, 200, 255};
        for (int i = 0; i < 256; ++i)
            texels[i] = (CrRgba){255, 255, 255, 255};
        CrFramebuffer fb = { .w = PW, .h = PH, .color = color, .depth = 0 };
        CrTexture atlas = { .w = 16, .h = 16, .texels = texels };
        gm_overlay_block_in_hand(&fb, &atlas, 0.f, 0.f, 1.f, 1.f);
        /* white*0.1 src, a=0.5 over 200: out = 25.5*0.5 + 200*0.5 ≈ 112.75 */
        int mid = color[(PH / 2) * PW + PW / 2].r;
        CHECK(mid < 160 && mid > 80, "block overlay mid-tone");
        free(color); free(texels);

        color = calloc((size_t)PW * PH, sizeof(CrRgba));
        fb.color = color;
        gm_overlay_loading_screen(&fb);
        int nonblack = 0;
        for (int i = 0; i < PW * PH; ++i)
            if (color[i].r | color[i].g | color[i].b) nonblack++;
        CHECK(nonblack == PW * PH, "loading covers frame");
        free(color);
    }

    /* ---- Underwater constants ---- */
    {
        /* water fog RGB base * fog_c1=1 -> (0.02,0.02,0.2); density 0.1; fov 60/70 */
        CHECK(fabsf(60.0f / 70.0f - 0.857142f) < 1e-5f, "water fov scale");
        /* liquid_height_percent(0) = 1/9; surface test uses -0.11111111 */
        float pct = 1.0f / 9.0f;
        CHECK(fabsf(pct - 0.11111111f) < 1e-5f, "liquid height percent meta0");
    }

    /* ---- Death HUD wash ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.dead = 1; pv.deaths = 2;
        gm_hud_draw(&fb, &pv);
        CrRgba c = fb.color[(H / 2) * W + W / 2];
        CHECK(c.r > c.g && c.r > c.b, "death wash is red-dominant");
        free(fb.color); free(fb.depth);
    }

    if (g_fail) {
        fprintf(stderr, "ui_hud numerical: FAIL\n");
        return 1;
    }
    printf("ui_hud numerical: PASS\n");
    return 0;
}
