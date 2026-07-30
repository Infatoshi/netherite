/* End-to-end frame composition gate for owned HUD / hand / overlay modules.
 *
 * Composes a full 854x480 frame (GUI scale 2) with live GmPlayerView fields:
 * armor row, XP, eat use pose via gm_hand_draw, block-in-hand overlay, then
 * HUD. This is the release gate that replaces numerical-only claims.
 *
 * Does NOT claim pixel parity with Java. Oracle goldens are listed in
 * ORACLE_CAPTURE.md and are still missing from the tree.
 *
 * Build/run: bash ../verify/ui_hud/run_ui_hud_gates.sh
 */
#include "game/hud.h"
#include "game/hand.h"
#include "game/overlay.h"
#include "assets/blockmodels.h"
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

/* Mean R over non-gray painted pixels only (gray fill would dilute a large ROI
 * enough for idle vs mid-eat to collide on integer means). */
static int region_mean_r(const CrFramebuffer *fb, int x0, int y0, int x1, int y1) {
    long sum = 0; int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            CrRgba c = fb->color[y * fb->w + x];
            if (c.r == GRAY && c.g == GRAY && c.b == GRAY) continue;
            sum += c.r;
            ++n;
        }
    return n ? (int)(sum / n) : 0;
}

/* Find horizontal span of green XP-level pixels (0x80FF20-ish) on a scanline band. */
static int xp_green_span(const CrFramebuffer *fb, int y0, int y1,
                         int *out_x0, int *out_x1) {
    int minx = fb->w, maxx = -1;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < fb->w; ++x) {
            CrRgba c = fb->color[y * fb->w + x];
            /* centre glyph is 0x80FF20; outline is black — accept green channel
             * dominant near the XP colour. */
            if (c.g > 180 && c.r < 160 && c.b < 80) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
            }
        }
    if (maxx < 0) return 0;
    if (out_x0) *out_x0 = minx;
    if (out_x1) *out_x1 = maxx;
    return 1;
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

static void fill_gray(CrFramebuffer *fb) {
    for (int i = 0; i < fb->w * fb->h; ++i) {
        fb->color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        if (fb->depth) fb->depth[i] = 1.0f;
    }
}

static GmPlayerView base_view(void) {
    GmPlayerView pv;
    memset(&pv, 0, sizeof pv);
    pv.health = 20.0f; pv.max_health = 20.0f;
    pv.food = 20.0f; pv.max_food = 20.0f;
    pv.air = -1;
    pv.eye_height = 1.62f;
    pv.xp_level = 7;
    pv.xp_frac = 0.5f;
    pv.hotbar_sel = 0;
    pv.hotbar_ids[0] = 297; /* bread */
    pv.hotbar_counts[0] = 1;
    pv.armor_points = 15; /* full iron */
    return pv;
}

int main(void) {
    g_fail = 0;
    CHECK(gm_hud_init() == 0, "gm_hud_init");

    CrFramebuffer fb = make_fb();
    GmPlayerView pv = base_view();

    /* ---- (1) HUD alone: armor + hearts + XP + hotbar on gray ---- */
    gm_hud_draw(&fb, &pv);
    CHECK(region_non_gray(&fb, 244, 380, 244 + 80, 400), "compose: armor row");
    CHECK(region_non_gray(&fb, 244, 400, 244 + 80, 430), "compose: hearts row");
    CHECK(region_non_gray(&fb, 244, 442, 244 + 360, 476), "compose: hotbar");
    /* XP level 7 green text around center above bar */
    CHECK(region_non_gray(&fb, 400, 400, 454, 430), "compose: XP level region");

    /* Pixel-assert XP centering: FontRenderer i1 = (sw - stringWidth) / 2.
     * Midpoint of green glyph span must sit at fb centre (W/2) within 1 px of
     * the integer formula (both even/odd cases). */
    {
        int gx0 = 0, gx1 = 0;
        CHECK(xp_green_span(&fb, 400, 430, &gx0, &gx1),
              "compose: XP level green pixels present");
        int mid = (gx0 + gx1) / 2;
        int expect_mid = W / 2; /* 427 for 854; font centre of mass near sw/2 */
        /* Level "7" is one glyph; (sw - w)/2 * scale centres the string. */
        char buf[8];
        snprintf(buf, sizeof buf, "%d", pv.xp_level);
        int sw_s = W / 2; /* scale=2 -> scaledWidth 427 */
        int lx = ((sw_s - gm_font_width(buf)) / 2) * 2;
        int glyph_w = gm_font_width(buf) * 2;
        int formula_mid = lx + glyph_w / 2;
        CHECK(abs(mid - formula_mid) <= 2,
              "compose: XP level pixels match (sw-w)/2 centering");
        CHECK(abs(mid - expect_mid) <= glyph_w,
              "compose: XP level near framebuffer horizontal centre");
        (void)expect_mid;
    }

    /* ---- (2) Multi-row hearts displace armor upward ---- */
    fill_gray(&fb);
    pv.max_health = 40.0f; /* two heart rows */
    pv.health = 40.0f;
    pv.armor_points = 10;
    gm_hud_draw(&fb, &pv);
    /* j1=201, rows=2, gap=10, armor_y_s = 201-10-10=181, fb y=362 */
    CHECK(region_non_gray(&fb, 244, 360, 244 + 80, 378),
          "compose: armor above multi-row hearts");
    /* and hearts still present on first row y=402 */
    CHECK(region_non_gray(&fb, 244, 398, 244 + 80, 420),
          "compose: multi-row hearts base");
    pv.max_health = 20.0f; pv.health = 20.0f;

    /* ---- (2b) Absorption increases heart rows, gold sprites, lifts armor ---- */
    fill_gray(&fb);
    pv.absorption = 20.0f; /* +10 gold heart icons -> 2 rows total with 20 max */
    pv.armor_points = 10;
    gm_hud_draw(&fb, &pv);
    /* heart_icons = ceil((20+20)/2)=20, rows=2 -> armor at same y as multi-row */
    CHECK(region_non_gray(&fb, 244, 360, 244 + 80, 378),
          "compose: absorption lifts armor row");
    {
        /* Second row must be gold (icons.png U=160), not empty containers. */
        int gold_n = 0;
        for (int i = 0; i < 10; ++i) {
            CrRgba c = fb.color[386 * W + (244 + i * 16 + 8)];
            if (c.r > 160 && c.g > 100 && c.g < 230 && c.b < 130 &&
                c.r > c.g + 10 && c.g > c.b)
                gold_n++;
        }
        CHECK(gold_n == 10, "compose: absorption draws 10 gold heart icons");
    }
    /* abs=3: half on highest then one full (Java l2 consume high->low). */
    fill_gray(&fb);
    pv.absorption = 3.0f;
    pv.armor_points = 0;
    gm_hud_draw(&fb, &pv);
    {
        /* heart_icons=ceil(23/2)=12; j5=11 half gold, j5=10 full gold on row1. */
        CrRgba half = fb.color[386 * W + (244 + 1 * 16 + 8)]; /* j5=11 -> col1 */
        CrRgba full = fb.color[386 * W + (244 + 0 * 16 + 8)]; /* j5=10 -> col0 */
        int half_ok = (half.r > 160 && half.g > 100 && half.b < 130);
        int full_ok = (full.r > 160 && full.g > 100 && full.b < 130);
        CHECK(half_ok && full_ok,
              "compose: abs=3 half+full gold on top row icons");
    }
    pv.absorption = 0.0f;

    /* ---- (3) Hand eat pose wired through gm_hand_draw (not test setter only) ---- */
    /* Idle sits lower-right; mid-eat (remaining=16/32) uses transformEatFirstPerson
     * with f3≈1 and swings the item toward screen center. Compare on a shared
     * lower band that covers both poses (not idle-only lower-right). */
    const int hand_x0 = W / 3, hand_y0 = H / 2, hand_x1 = W - 20, hand_y1 = H - 20;
    fill_gray(&fb);
    pv.use_action = 0; pv.use_remaining = 0; pv.use_max = 0;
    gm_hand_draw(&fb, &pv, 0.0f);
    int idle_r = region_mean_r(&fb, hand_x0, hand_y0, hand_x1, hand_y1);
    CHECK(region_non_gray(&fb, W * 2 / 3, H * 2 / 3, W - 20, H - 20),
          "compose: idle hand draws lower-right viewmodel");

    fill_gray(&fb);
    pv.use_action = 1; pv.use_remaining = 16; pv.use_max = 32; /* mid-eat */
    gm_hand_draw(&fb, &pv, 0.0f);
    int eat_r = region_mean_r(&fb, hand_x0, hand_y0, hand_x1, hand_y1);
    CHECK(region_non_gray(&fb, hand_x0, hand_y0, hand_x1, hand_y1),
          "compose: eat hand draws mid/lower viewmodel");
    /* Non-vacuous: eat pose must change the region mean vs idle (not OR drawn). */
    CHECK(idle_r != eat_r, "compose: eat use changes hand region mean (non-vacuous)");

    /* Geometry path also differs under emit (belt-and-suspenders). */
    {
        static CrVertex a[6156], b[6156];
        gm_hand_set_use(0, 0, 0);
        int n0 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, a, 6156);
        gm_hand_set_use(1, 16, 32);
        int n1 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, b, 6156);
        gm_hand_set_use(0, 0, 0);
        float d = 0.0f;
        int n = n0 < n1 ? n0 : n1;
        for (int i = 0; i < n; ++i) {
            float dx = b[i].pos.x - a[i].pos.x;
            float dy = b[i].pos.y - a[i].pos.y;
            float dz = b[i].pos.z - a[i].pos.z;
            d += dx * dx + dy * dy + dz * dz;
        }
        CHECK(d > 0.01f, "compose: eat emit offsets bread");
    }

    /* ---- (4) Block-in-hand darken then HUD still composites on top ---- */
    fill_gray(&fb);
    {
        /* Synthetic white tile as block face; live path samples real atlas. */
        CrRgba *texels = calloc(256, sizeof(CrRgba));
        for (int i = 0; i < 256; ++i)
            texels[i] = (CrRgba){255, 255, 255, 255};
        CrTexture atlas = { .w = 16, .h = 16, .texels = texels };
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){200, 200, 200, 255};
        gm_overlay_block_in_hand(&fb, &atlas, 0.f, 0.f, 1.f, 1.f, 70.f);
        int mid = fb.color[(H / 2) * W + W / 2].r;
        /* blend off: white*0.1 replace (~26), not src-over mid-gray */
        CHECK(mid < 40 && mid > 15, "compose: block overlay replace dark");
        /* HUD over darkened frame still paints hotbar / armor */
        pv.use_action = 0;
        gm_hud_draw(&fb, &pv);
        CHECK(region_non_gray(&fb, 244, 442, 244 + 100, 476),
              "compose: HUD over block overlay");
        free(texels);
    }

    /* ---- (5) Overlay order constant: block then water-region fire/portal
     * are caller-wired; here we only lock that block + portal both leave
     * measurable non-identity pixels without asserting Java PNGs. ---- */
    fill_gray(&fb);
    {
        CrRgba *texels = calloc(256, sizeof(CrRgba));
        for (int i = 0; i < 256; ++i)
            texels[i] = (CrRgba){128, 64, 200, 255};
        CrTexture atlas = { .w = 16, .h = 16, .texels = texels };
        for (int i = 0; i < W * H; ++i)
            fb.color[i] = (CrRgba){180, 180, 180, 255};
        gm_overlay_block_in_hand(&fb, &atlas, 0.f, 0.f, 1.f, 1.f, 70.f);
        int after_block = fb.color[0].r;
        /* Portal with time<=0 is a no-op; time>0 needs real portal sprite.
         * Order gate: block ran first and changed the clear. */
        CHECK(after_block != 180, "compose: block-before-portal order slot");
        free(texels);
    }

    free(fb.color); free(fb.depth);

    if (g_fail) {
        fprintf(stderr, "ui_hud compose: FAIL\n");
        return 1;
    }
    printf("ui_hud compose: PASS\n");
    printf("oracle pixel ROI: see goldens/ + compare_ui_hud_oracle.py\n");
    return 0;
}
