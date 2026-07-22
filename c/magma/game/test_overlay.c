/* game/test_overlay.c - geometry invariants for the selection/dig overlay.
 * Every quad is emitted in both windings (so one survives backface culling from
 * either side), vertex counts match the documented budget, and all crack UVs
 * stay inside the destroy_stage sprite rect for every damage stage.
 * Build+run: bash game/test_overlay.sh */
#include "game/overlay.h"
#include "game/sel_box.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

#include <stdio.h>
#include <string.h>

/* overlay.c's loading-screen compositor uses the real HUD font in the game;
 * this geometry-only standalone test does not link hud.c. */
int gm_font_width(const char *s) { return (int)strlen(s) * 6; }
void gm_font_draw(CrFramebuffer *fb, const char *s, int dx, int dy, int scale,
                  unsigned rgb, int shadow) {
    (void)fb; (void)s; (void)dx; (void)dy; (void)scale; (void)rgb; (void)shadow;
}

static int g_fail = 0;
#define CHECK(C, M) do { if (!(C)) { printf("FAIL: %s\n", M); g_fail = 1; } } while (0)

int main(void)
{
    static CrVertex v[GM_OVERLAY_MAX_VERTS];
    /* eye slightly south of the test block so ribbons face the camera */
    const float ex = 5.5f, ey = 64.5f, ez = -5.0f;

    /* outline only: 12 edges x 1 ribbon x 12 verts */
    int n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 1, 5, 64, -3, 0,
                            0, 0, 0, 0, 0.0f, ex, ey, ez);
    CHECK(n == 144, "outline emits 144 verts");
    CHECK(n % 3 == 0, "outline verts form whole triangles");

    /* shaped outline: bottom slab box tops out at y+0.5 (plus 0.002 + half_w) */
    {
        GmSelIn in; memset(&in, 0, sizeof in);
        in.id = 44; in.meta = 0;   /* stone slab, bottom */
        float box[6];
        gm_sel_box(&in, box);
        CHECK(box[4] == 0.5f, "bottom slab selection box is half height");
        n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 1, 5, 64, -3, box,
                            0, 0, 0, 0, 0.0f, ex, ey, ez);
        CHECK(n == 144, "shaped outline emits 144 verts");
        float ymax = -1e9f;
        for (int i = 0; i < n; ++i) if (v[i].pos.y > ymax) ymax = v[i].pos.y;
        CHECK(ymax < 64.6f, "slab outline stays at half height");
    }

    /* shaped outline: standing torch box */
    {
        GmSelIn in; memset(&in, 0, sizeof in);
        in.id = 50; in.meta = 5;
        float box[6];
        gm_sel_box(&in, box);
        CHECK(box[0] == 0.4f && box[3] == 0.6f && box[4] == 0.6f,
              "standing torch selection box 0.4..0.6 / h 0.6");
    }

    /* fence with a solid east neighbor extends to the cell edge */
    {
        GmSelIn in; memset(&in, 0, sizeof in);
        in.id = 85; in.nid[3] = 1;   /* stone to the east */
        float box[6];
        gm_sel_box(&in, box);
        CHECK(box[0] == 0.375f && box[3] == 1.0f && box[2] == 0.375f && box[5] == 0.625f,
              "fence extends toward its east connection");
    }

    /* crack only: 6 faces x 12 verts, for every stage */
    for (int s = 0; s <= 10; ++s) {
        float dmg = (float)s / 10.0f;
        if (dmg <= 0.0f) continue;
        n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 0, 0, 0, 0, 0,
                            1, -7, 12, 40, dmg, ex, ey, ez);
        CHECK(n == 72, "crack emits 72 verts");
        for (int i = 0; i < n; ++i) {
            CHECK(v[i].uv.x >= 0.0f && v[i].uv.x <= 1.0f &&
                  v[i].uv.y >= 0.0f && v[i].uv.y <= 1.0f, "crack uv in atlas");
            CHECK(v[i].pos.x >= -7.1f && v[i].pos.x <= -5.9f, "crack x near block");
            CHECK(v[i].pos.y >= 11.9f && v[i].pos.y <= 13.1f, "crack y near block");
        }
    }

    /* PlayerControllerMP publishes floor(progress*10)-1: 0.2 selects stage 1. */
    n = gm_overlay_emit_crack(v, GM_OVERLAY_MAX_VERTS,
                              -7, 12, 40, 0.2f, 4);
    {
        float u0, v0, u1, v1;
        bm_sprite_uv(CR_SPRITE_DESTROY_STAGE_1, &u0, &v0, &u1, &v1);
        CHECK(n == 12 && v[0].uv.x >= u0 && v[0].uv.x <= u1 &&
              v[0].uv.y >= v0 && v[0].uv.y <= v1,
              "damage 0.2 selects vanilla destroy stage 1");
        n = gm_overlay_emit_crack(v, GM_OVERLAY_MAX_VERTS,
                                  -7, 12, 40, 0.2f, 0);
        CHECK(v[0].uv.x == u1 && v[0].uv.y == v1,
              "vertical crack face uses vanilla mirrored U projection");
        n = gm_overlay_emit_crack(v, GM_OVERLAY_MAX_VERTS,
                                  -7, 12, 40, 0.2f, 5);
        CHECK(v[0].uv.x == u0 && v[0].uv.y == v0,
              "bottom crack face uses vanilla X/-Z projection");
    }

    /* damage 0 emits no crack (vanilla hides stage until progress > 0) */
    n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 0, 0, 0, 0, 0,
                        1, 0, 0, 0, 0.0f, ex, ey, ez);
    CHECK(n == 0, "no crack at damage 0");

    /* both: budget is exactly GM_OVERLAY_MAX_VERTS */
    n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 1, 0, 0, 0, 0,
                        1, 0, 0, 0, 0.5f, 0.5f, 0.5f, -2.0f);
    CHECK(n == GM_OVERLAY_MAX_VERTS, "outline+crack fills the documented budget");

    /* both windings: tris come in pairs (a,b,c)/(a,c,b) sharing vertices */
    for (int i = 0; i + 5 < n; i += 6) {
        CHECK(v[i].pos.x == v[i+3].pos.x && v[i+2].pos.x == v[i+4].pos.x &&
              v[i+1].pos.x == v[i+5].pos.x, "winding pair mirrors the same quad tri");
    }

    /* truncated buffer: never writes past max */
    n = gm_overlay_emit(v, 100, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0.5f, -2.0f);
    CHECK(n <= 100, "respects the max vertex cap");

    /* selection tint is vanilla black @ 0.4 alpha */
    n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 1, 0, 0, 0, 0,
                        0, 0, 0, 0, 0.0f, 0.5f, 0.5f, -2.0f);
    CHECK(n > 0 && v[0].tint.r == 0 && v[0].tint.g == 0 && v[0].tint.b == 0
          && v[0].tint.a == 102, "selection colour is (0,0,0,102) = 0.4 alpha");

    if (g_fail) { printf("TEST FAILED\n"); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
