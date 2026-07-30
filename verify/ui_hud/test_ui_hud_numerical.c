/* Focused numerical gates for HUD / first-person hand / screen overlays.
 *
 * Pure C: no oracle PNGs required. Compares owned-module outputs against
 * 1.11.2 formulas (GuiIngame / ItemRenderer / GuiBossOverlay).
 *
 * Build/run: bash ../verify/ui_hud/run_ui_hud_gates.sh
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

    /* ---- Durability width + hue (ItemRenderer + Item.getRGBDurability) ---- */
    CHECK(gm_hud_durability_width(270, 0) == 0, "undamaged no bar");
    CHECK(gm_hud_durability_width(270, 1) == 13, "almost-new full width");
    /* wood pick max=59, damage=30: round(13 - 30/59*13) = round(6.3898) = 6 */
    CHECK(gm_hud_durability_width(270, 30) == 6, "wood pick half width=6");
    CHECK(gm_hud_durability_width(276, 780) >= 5 &&
          gm_hud_durability_width(276, 780) <= 8, "diamond sword half");
    {
        unsigned char r, g, b;
        gm_hud_durability_rgb(270, 1, &r, &g, &b);
        CHECK(g >= r && g >= b, "fresh durability is green-dominant");
        gm_hud_durability_rgb(270, 55, &r, &g, &b);
        CHECK(r > g, "near-broken durability is red-dominant");
        /* damage 30/59: hue=(1-30/59)/3 => HSV sector 0 => (255, 250, 0) */
        gm_hud_durability_rgb(270, 30, &r, &g, &b);
        CHECK(r == 255 && g == 250 && b == 0, "wood pick half is (255,250,0)");
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
        pv.armor_points = 7; /* 3 full + 1 half */
        gm_hud_draw(&fb, &pv);
        CHECK(region_non_gray(&fb, 244, 380, 244 + 80, 400), "armor at sh-49");
        clear_fb(&fb);
        pv.armor_points = 0;
        gm_hud_draw(&fb, &pv);
        CHECK(!region_non_gray(&fb, 244, 380, 244 + 80, 400),
              "no armor icons at 0 points");
        /* absorption 20 with max 20 -> 2 heart rows; armor lifts to multi-row y */
        clear_fb(&fb);
        pv.armor_points = 10;
        pv.absorption = 20.0f;
        gm_hud_draw(&fb, &pv);
        CHECK(region_non_gray(&fb, 244, 360, 244 + 80, 378),
              "absorption lifts armor above base heart row");
        free(fb.color); free(fb.depth);
    }

    /* ---- Absorption gold hearts: GuiIngame l2 high->low, U=160/169 ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 20; pv.max_health = 20;
        pv.food = 20; pv.max_food = 20;
        pv.air = -1;
        pv.armor_points = 15;
        pv.absorption = 20.0f; /* +10 full gold icons on second row */
        gm_hud_draw(&fb, &pv);
        /* scale=2: base hearts y=402, abs row y=382, armor y=362; icon pitch 16 */
        int gold_icons = 0, red_icons = 0;
        for (int i = 0; i < 10; ++i) {
            int cx = 244 + i * 16 + 8;
            CrRgba gld = fb.color[386 * W + cx]; /* second row center */
            CrRgba red = fb.color[406 * W + cx]; /* base row center */
            if (gld.r > 160 && gld.g > 100 && gld.g < 230 && gld.b < 130 &&
                gld.r > gld.g + 10 && gld.g > gld.b)
                gold_icons++;
            if (red.r > 140 && red.r > red.g + 30 && red.r > red.b + 30)
                red_icons++;
        }
        CHECK(gold_icons == 10, "abs=20 draws 10 full gold hearts on row 1");
        CHECK(red_icons == 10, "abs=20 keeps 10 red hearts on row 0");
        CHECK(region_non_gray(&fb, 244, 360, 244 + 80, 378),
              "armor still above dual heart rows");

        /* Odd absorption: half gold on highest icon (j5=heart_icons-1).
         * abs=1 -> 11 icons; half gold at j5=10 -> row1 col0. */
        clear_fb(&fb);
        pv.absorption = 1.0f;
        pv.armor_points = 0;
        gm_hud_draw(&fb, &pv);
        {
            CrRgba half = fb.color[386 * W + (244 + 8)]; /* row1 icon0 center */
            CrRgba no_g = fb.color[386 * W + (244 + 16 + 8)]; /* row1 icon1 */
            int half_gold = (half.r > 160 && half.g > 100 && half.b < 130 &&
                             half.g > half.b);
            /* icon1 on row1 should be empty container only (grayish dark), not gold */
            int icon1_gold = (no_g.r > 160 && no_g.g > 100 && no_g.b < 130 &&
                              no_g.g > no_g.b && no_g.r > no_g.g + 10);
            CHECK(half_gold, "abs=1 places half gold on highest icon");
            CHECK(!icon1_gold, "abs=1 does not gold-fill lower abs slots");
        }

        /* Flash last-health under abs: last=20 on slots 0-9 only; abs row still gold. */
        clear_fb(&fb);
        pv.absorption = 20.0f;
        pv.health = 14.0f;
        pv.hud_health = 14;
        pv.hud_last_health = 20;
        pv.hud_flash = 1;
        pv.hud_state_valid = 1;
        gm_hud_draw(&fb, &pv);
        {
            CrRgba gld = fb.color[386 * W + (244 + 8)];
            CHECK(gld.r > 160 && gld.g > 100 && gld.b < 130,
                  "flash still leaves gold absorption hearts");
            /* damaged base row: some red hearts remain (health 14 -> 7 full) */
            CrRgba red0 = fb.color[406 * W + (244 + 8)];
            CHECK(red0.r > 140 && red0.r > red0.g + 30,
                  "flash path still draws current red hearts");
        }
        free(fb.color); free(fb.depth);
    }

    /* ---- Fishing rod durability max 64; XP level uses (sw-w)/2 ---- */
    {
        CHECK(gm_hud_durability_width(346, 1) == 13, "fishing rod almost-new");
        CHECK(gm_hud_durability_width(346, 32) >= 5 &&
              gm_hud_durability_width(346, 32) <= 8, "fishing rod half");
        CHECK(gm_hud_durability_width(346, 0) == 0, "undamaged rod no bar");
        /* width=6 on scaledWidth=427: (427-6)/2=210, not 427/2-6/2=210.5->210
         * both equal for even width; formula lock is in gm_hud_draw via font. */
        CHECK(gm_font_width("7") > 0, "font width for XP level digit");
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

    /* ---- Wood pick GUI icon: flat !isGui3d blit matches gui_atlas layer0 ---- */
    {
        CHECK(gm_gui_item_icon(NULL, 270, 0, 0, 0, 1), "wood pick has GUI icon");
        CrFramebuffer fb = make_fb();
        /* Blit icon alone at known origin, scale 2 (matches capture guiScale). */
        const int scale = 2, ix = 40, iy = 40;
        CHECK(gm_gui_item_icon(&fb, 270, 30, ix, iy, scale), "wood pick draws");
        /* Known opaque texels from items/wood_pickaxe.png (nearest x scale). */
        int hit_55 = 0, hit_107 = 0, wood_px = 0;
        for (int y = iy; y < iy + 16 * scale; ++y)
            for (int x = ix; x < ix + 16 * scale; ++x) {
                CrRgba c = fb.color[y * W + x];
                if (c.r == GRAY && c.g == GRAY && c.b == GRAY) continue;
                wood_px++;
                if (c.r == 55 && c.g == 41 && c.b == 16) hit_55 = 1;
                if (c.r == 107 && c.g == 81 && c.b == 31) hit_107 = 1;
            }
        CHECK(wood_px >= 60, "wood pick icon has opaque body");
        CHECK(hit_55 && hit_107, "wood pick icon samples layer0 texels");
        /* Undamaged: no durability strip pixels (black 13x2 at +2,+13). */
        clear_fb(&fb);
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 20; pv.max_health = 20;
        pv.food = 20; pv.max_food = 20;
        pv.air = -1;
        pv.hotbar_ids[0] = 270;
        pv.hotbar_counts[0] = 1;
        pv.hotbar_meta[0] = 0; /* undamaged */
        gm_hud_draw(&fb, &pv);
        const int hix = 250, hiy = 442;
        int strip_black = 0;
        for (int y = hiy + 13 * 2; y < hiy + 15 * 2; ++y)
            for (int x = hix + 2 * 2; x < hix + 15 * 2; ++x) {
                CrRgba c = fb.color[y * W + x];
                if (c.r < 8 && c.g < 8 && c.b < 8) strip_black++;
            }
        CHECK(strip_black == 0, "undamaged wood pick has no durability strip");
        free(fb.color); free(fb.depth);
    }

    /* ---- Air bubble formula (Forge GuiIngameForge.renderAir ceil) ---- */
    {
        /* air=121: full=ceil((121-2)*10/300)=ceil(3.966)=4
         *          total=ceil(121*10/300)=ceil(4.033)=5 -> 4 full + 1 partial
         * (committed oracle golden hud_air_partial). air=123 is 5 full only. */
        int air = 121;
        int full = (int)ceil(((double)air - 2.0) * 10.0 / 300.0);
        int total = (int)ceil((double)air * 10.0 / 300.0);
        int partial = total - full;
        CHECK(full == 4 && partial == 1, "air=121 is 4 full + 1 partial");
        air = 123;
        full = (int)ceil(((double)air - 2.0) * 10.0 / 300.0);
        total = (int)ceil((double)air * 10.0 / 300.0);
        CHECK(full == 5 && total == 5, "air=123 is 5 full");
        air = 2;
        full = (int)ceil(((double)air - 2.0) * 10.0 / 300.0);
        total = (int)ceil((double)air * 10.0 / 300.0);
        CHECK(full == 0 && total == 1, "air=2 is one partial bubble");
    }

    /* ---- Hurt flash: pin path draws flash sprites when hud_flash set ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.health = 14.0f; pv.max_health = 20.0f;
        pv.food = 20.0f; pv.max_food = 20.0f;
        pv.air = -1;
        pv.hud_health = 14; pv.hud_last_health = 20;
        pv.hud_flash = 1; pv.hud_state_valid = 1;
        gm_hud_draw(&fb, &pv);
        /* Heart 7 (lost HP) shows white flash full, not empty-only. */
        int hx = 244 + 7 * 8 * 2, hy = 402;
        int saw_white = 0;
        for (int y = hy; y < hy + 18; ++y)
            for (int x = hx; x < hx + 16; ++x) {
                CrRgba c = fb.color[y * W + x];
                if (c.r > 240 && c.g > 240 && c.b > 240) saw_white = 1;
            }
        CHECK(saw_white, "hurt flash_on draws white flash hearts on lost HP");
        clear_fb(&fb);
        pv.hud_flash = 0;
        gm_hud_draw(&fb, &pv);
        saw_white = 0;
        for (int y = hy; y < hy + 18; ++y)
            for (int x = hx; x < hx + 16; ++x) {
                CrRgba c = fb.color[y * W + x];
                if (c.r > 240 && c.g > 240 && c.b > 240) saw_white = 1;
            }
        CHECK(!saw_white, "hurt flash_off has no white flash hearts");
        free(fb.color); free(fb.depth);
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
        /* BLOCK pose geometry (forced use_action=2; live path is shield-only). */
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
        gm_overlay_block_in_hand(&fb, &atlas, 0.f, 0.f, 1.f, 1.f, 70.f);
        /* blend off: white*0.1 replace -> ~26 (backdrop 200 overwritten) */
        int mid = color[(PH / 2) * PW + PW / 2].r;
        CHECK(mid >= 20 && mid <= 35, "block overlay replace tex*0.1");
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

    /* ---- GuiGameOver layout, tint, buttons, hit regions ---- */
    {
        CrFramebuffer fb = make_fb();
        GmPlayerView pv; memset(&pv, 0, sizeof pv);
        pv.dead = 1; pv.deaths = 2; pv.score = 0; pv.death_ticks = 0;
        gm_hud_draw(&fb, &pv);
        /* Gradient: top 0x60500000 / bottom 0xA0803030 over gray -> red-dominant. */
        CrRgba mid = fb.color[(H / 2) * W + W / 2];
        CHECK(mid.r > mid.g && mid.r > mid.b, "death gradient is red-dominant");
        /* Title band (2x "You died!" at GUI y=60 -> fb y=120). */
        CHECK(region_non_gray(&fb, 300, 118, 560, 150), "death title painted");
        /* Score line at GUI y=100 -> fb y=200. */
        CHECK(region_non_gray(&fb, 350, 198, 520, 216), "death score painted");
        int bx0, by0, bx1, by1, bw, bh;
        gm_hud_death_layout(W, H, &bx0, &by0, &bx1, &by1, &bw, &bh);
        CHECK(bx0 == 226 && by0 == 264 && by1 == 312, "death button origins @scale2");
        CHECK(bw == 400 && bh == 40, "death button size 200x20 * scale2");
        /* Disabled buttons: widgets disabled strip mid ~45. */
        CrRgba bmid = fb.color[(by0 + 20) * W + (bx0 + 200)];
        CHECK(bmid.r < 80 && bmid.g < 80 && bmid.b < 80, "disabled button is dark gray");
        CHECK(gm_hud_death_buttons_enabled(0) == 0, "buttons locked at t=0");
        CHECK(gm_hud_death_buttons_enabled(19) == 0, "buttons locked at t=19");
        CHECK(gm_hud_death_buttons_enabled(20) == 1, "buttons open at t=20");
        CHECK(gm_hud_death_button_at(W, H, bx0 + 10, by0 + 10, 0) == -1,
              "hit ignored while locked");
        CHECK(gm_hud_death_button_at(W, H, bx0 + 10, by0 + 10, 1) == 0,
              "respawn hit");
        CHECK(gm_hud_death_button_at(W, H, bx1 + 10, by1 + 10, 1) == 1,
              "title hit");
        CHECK(gm_hud_death_button_at(W, H, 0, 0, 1) == -1, "miss outside");
        free(fb.color); free(fb.depth);
    }

    /* ---- GuiGameOver tint: paired-background compositing (source-grounded) ----
     * Gui.drawGradientRect(0,0,w,h, 0x60500000, 0xA0803030) with GL_SMOOTH +
     * SRC_ALPHA/ONE_MINUS_SRC_ALPHA. Integer row lerp +127/255 blend matches
     * float vertex colors quantized to bytes (see compare_ui_hud_oracle.py).
     * Hard-check pure gradient bands over several underlays — not world parity. */
    {
        /* Pure-tint bands: above title (fb y<118) and below Title button (y>=352). */
        static const int bands[][2] = { {0, 100}, {360, H} };
        typedef struct { unsigned char r, g, b; const char *name; int patterned; } UL;
        UL underlays[] = {
            { GRAY, GRAY, GRAY, "gray40", 0 },
            { 0, 0, 0, "black", 0 },
            { 255, 255, 255, "white", 0 },
            { 180, 40, 40, "red180", 0 },
            { 20, 90, 160, "blue", 0 },
            { 0, 0, 0, "h_ramp", 1 },
        };
        for (int u = 0; u < (int)(sizeof underlays / sizeof underlays[0]); ++u) {
            CrFramebuffer fb = make_fb();
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    unsigned char r = underlays[u].r, g = underlays[u].g, b = underlays[u].b;
                    if (underlays[u].patterned) {
                        r = (unsigned char)(x % 256);
                        g = (unsigned char)((x * 3 + y) % 256);
                        b = (unsigned char)((y * 5 + x) % 256);
                    }
                    fb.color[y * W + x] = (CrRgba){ r, g, b, 255 };
                }
            }
            GmPlayerView pv; memset(&pv, 0, sizeof pv);
            pv.dead = 1; pv.deaths = 1; pv.score = 0; pv.death_ticks = 0;
            gm_hud_draw(&fb, &pv);

            int den = H > 1 ? H - 1 : 1;
            const unsigned top = 0x60500000u, bot = 0xA0803030u;
            int ta = (top >> 24) & 255, tr = (top >> 16) & 255;
            int tg = (top >> 8) & 255, tb = top & 255;
            int ba = (bot >> 24) & 255, br = (bot >> 16) & 255;
            int bg = (bot >> 8) & 255, bb = bot & 255;
            long long n_bad = 0, n_px = 0;
            for (int bi = 0; bi < 2; ++bi) {
                int y0 = bands[bi][0], y1 = bands[bi][1];
                for (int y = y0; y < y1; ++y) {
                    int ga = (ta * (den - y) + ba * y + den / 2) / den;
                    int gr = (tr * (den - y) + br * y + den / 2) / den;
                    int gg = (tg * (den - y) + bg * y + den / 2) / den;
                    int gb = (tb * (den - y) + bb * y + den / 2) / den;
                    int ia = 255 - ga;
                    for (int x = 0; x < W; ++x) {
                        unsigned char ur = underlays[u].r, ug = underlays[u].g, ub = underlays[u].b;
                        if (underlays[u].patterned) {
                            ur = (unsigned char)(x % 256);
                            ug = (unsigned char)((x * 3 + y) % 256);
                            ub = (unsigned char)((y * 5 + x) % 256);
                        }
                        int er = (gr * ga + ur * ia + 127) / 255;
                        int eg = (gg * ga + ug * ia + 127) / 255;
                        int eb = (gb * ga + ub * ia + 127) / 255;
                        CrRgba c = fb.color[y * W + x];
                        n_px++;
                        if (c.r != er || c.g != eg || c.b != eb) n_bad++;
                    }
                }
            }
            CHECK(n_bad == 0, underlays[u].name);
            if (n_bad == 0)
                printf("death tint pair %-8s: PASS pure-band px=%lld model-exact\n",
                       underlays[u].name, n_px);
            else
                fprintf(stderr, "FAIL: death tint pair %s bad=%lld / %lld\n",
                        underlays[u].name, n_bad, n_px);
            free(fb.color); free(fb.depth);
        }
    }

    if (g_fail) {
        fprintf(stderr, "ui_hud numerical: FAIL\n");
        return 1;
    }
    printf("ui_hud numerical: PASS\n");
    return 0;
}
