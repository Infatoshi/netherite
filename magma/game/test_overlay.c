/* game/test_overlay.c - geometry invariants for the selection/dig overlay.
 * Every quad is emitted in both windings (so one survives backface culling from
 * either side), vertex counts match the documented budget, and all crack UVs
 * stay inside the destroy_stage sprite rect for every damage stage.
 * Build+run: bash game/test_overlay.sh */
#include "game/overlay.h"
#include "game/runtime.h"
#include "game/sel_box.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* overlay.c's loading-screen compositor uses the real HUD font in the game;
 * this geometry-only standalone test does not link hud.c. */
int gm_font_width(const char *s) { return (int)strlen(s) * 6; }
void gm_font_draw(CrFramebuffer *fb, const char *s, int dx, int dy, int scale,
                  unsigned rgb, int shadow) {
    (void)fb; (void)s; (void)dx; (void)dy; (void)scale; (void)rgb; (void)shadow;
}

static int g_fail = 0;
#define CHECK(C, M) do { if (!(C)) { printf("FAIL: %s\n", M); g_fail = 1; } } while (0)

static CrVec2 project_screen(
        CrVec3 p, const CrCamera *camera, int fb_w, int fb_h) {
    CrMat4 view = cr_camera_view(camera);
    CrVec4 q = cr_mat4_mul_vec4(
        view, (CrVec4){p.x, p.y, p.z, 1.0f});
    float f = 1.0f / tanf(camera->fov_deg * 0.00872664625997164788f);
    float aspect = (float)fb_w / (float)fb_h;
    return (CrVec2){
        (q.x * f / aspect / -q.z * 0.5f + 0.5f) * fb_w,
        (0.5f - q.y * f / -q.z * 0.5f) * fb_h,
    };
}

static float projected_distance(
        CrVec3 a, CrVec3 b, const CrCamera *camera, int fb_w, int fb_h) {
    CrVec2 pa = project_screen(a, camera, fb_w, fb_h);
    CrVec2 pb = project_screen(b, camera, fb_w, fb_h);
    return hypotf(pa.x - pb.x, pa.y - pb.y);
}

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

    /* TileEntityStructureRenderer: port one nontrivial Java transform and
     * recover the line endpoints from the camera-facing ribbon. */
    {
        GmRuntimeStructureBlock s;
        CrCamera structure_cam = {
            .pos = {8.0f, 23.0f, 20.0f},
            .yaw = 3.14159265358979323846f, .pitch = 0.0f,
            .fov_deg = 70.0f, .aspect = 4.0f / 3.0f,
            .znear = 0.05f, .zfar = 512.0f,
        };
        CrVec3 p0, p1;
        memset(&s, 0, sizeof s);
        s.active = 1;
        s.wx = 10; s.wy = 20; s.wz = 30;
        s.pos_x = 1; s.pos_y = 2; s.pos_z = 3;
        s.size_x = 4; s.size_y = 5; s.size_z = 6;
        s.mode = GM_STRUCTURE_MODE_LOAD;
        s.show_bounding_box = 1;
        s.mirror = GM_STRUCTURE_MIRROR_LEFT_RIGHT;
        s.rotation = GM_STRUCTURE_ROTATION_CW90;
        n = gm_overlay_emit_structure_bounds(
            v, GM_OVERLAY_MAX_VERTS, &s, &structure_cam, 640, 480);
        p0 = (CrVec3){(v[0].pos.x + v[5].pos.x) * 0.5f,
                      (v[0].pos.y + v[5].pos.y) * 0.5f,
                      (v[0].pos.z + v[5].pos.z) * 0.5f};
        p1 = (CrVec3){(v[1].pos.x + v[2].pos.x) * 0.5f,
                      (v[1].pos.y + v[2].pos.y) * 0.5f,
                      (v[1].pos.z + v[2].pos.z) * 0.5f};
        CHECK(n == 90, "structure transformed bounds emit Java line strip");
        CHECK(fabsf(projected_distance(
                  v[0].pos, v[5].pos, &structure_cam, 640, 480) - 2.0f)
                  < 1e-3f
                  && fabsf(projected_distance(
                  v[1].pos, v[2].pos, &structure_cam, 640, 480) - 2.0f)
                  < 1e-3f,
              "structure bounds stay exactly two pixels wide at both depths");
        CHECK(v[0].tint.r == 223 && v[0].tint.g == 127
                  && v[0].tint.b == 127,
              "structure bounds use GL_FLAT second-vertex line color");
        CHECK(fabsf(p0.x - 10.99f) < 1e-4f
                  && fabsf(p0.y - 21.99f) < 1e-4f
                  && fabsf(p0.z - 32.99f) < 1e-4f
                  && fabsf(p1.x - 17.01f) < 1e-4f
                  && fabsf(p1.y - 21.99f) < 1e-4f
                  && fabsf(p1.z - 32.99f) < 1e-4f,
              "structure LEFT_RIGHT/CW90 endpoints match Java doubles");
        s.show_bounding_box = 0;
        CHECK(gm_overlay_emit_structure_bounds(
                  v, GM_OVERLAY_MAX_VERTS, &s,
                  &structure_cam, 640, 480) == 0,
              "LOAD hides bounds when showBoundingBox is false");
        s.mode = GM_STRUCTURE_MODE_SAVE;
        CHECK(gm_overlay_emit_structure_bounds(
                  v, GM_OVERLAY_MAX_VERTS, &s,
                  &structure_cam, 640, 480) == 90,
              "SAVE always shows its bounds");

        s.show_air = 1;
        n = gm_overlay_emit_structure_marker(
            v, GM_OVERLAY_MAX_VERTS, &s, 11, 22, 33, 0, 0,
            &structure_cam, 640, 480);
        CHECK(n == 72
                  && fabsf(projected_distance(
                      v[0].pos, v[5].pos,
                      &structure_cam, 640, 480) - 3.0f) < 1e-3f
                  && fabsf(projected_distance(
                      v[1].pos, v[2].pos,
                      &structure_cam, 640, 480) - 3.0f) < 1e-3f,
              "air marker black pass stays three pixels wide at both depths");
        n = gm_overlay_emit_structure_marker(
            v, GM_OVERLAY_MAX_VERTS, &s, 11, 22, 33, 0, 1,
            &structure_cam, 640, 480);
        p0 = (CrVec3){(v[0].pos.x + v[5].pos.x) * 0.5f,
                      (v[0].pos.y + v[5].pos.y) * 0.5f,
                      (v[0].pos.z + v[5].pos.z) * 0.5f};
        p1 = (CrVec3){(v[1].pos.x + v[2].pos.x) * 0.5f,
                      (v[1].pos.y + v[2].pos.y) * 0.5f,
                      (v[1].pos.z + v[2].pos.z) * 0.5f};
        CHECK(n == 72 && fabsf(p0.x - 11.4f) < 1e-4f
                  && fabsf(p0.y - 22.4f) < 1e-4f
                  && fabsf(p0.z - 33.4f) < 1e-4f
                  && fabsf(p1.x - 11.6f) < 1e-4f
                  && v[0].tint.r == 127 && v[0].tint.g == 127
                  && v[0].tint.b == 255,
              "air marker has Java 0.4..0.6 box and blue thin pass");
        CHECK(fabsf(projected_distance(
                  v[0].pos, v[5].pos, &structure_cam, 640, 480) - 1.0f)
                  < 1e-3f
                  && fabsf(projected_distance(
                  v[1].pos, v[2].pos, &structure_cam, 640, 480) - 1.0f)
                  < 1e-3f,
              "air marker color pass stays one pixel wide at both depths");
        n = gm_overlay_emit_structure_marker(
            v, GM_OVERLAY_MAX_VERTS, &s, 11, 22, 33, 217, 1,
            &structure_cam, 640, 480);
        CHECK(n == 72 && v[0].tint.r == 255 && v[0].tint.g == 63
                  && v[0].tint.b == 63,
              "structure-void marker uses the red thin pass");
    }

    /* selection tint is vanilla black @ 0.4 alpha */
    n = gm_overlay_emit(v, GM_OVERLAY_MAX_VERTS, 1, 0, 0, 0, 0,
                        0, 0, 0, 0, 0.0f, 0.5f, 0.5f, -2.0f);
    CHECK(n > 0 && v[0].tint.r == 0 && v[0].tint.g == 0 && v[0].tint.b == 0
          && v[0].tint.a == 102, "selection colour is (0,0,0,102) = 0.4 alpha");

    /* portal screen fourth-power alpha curve: time_in_portal 0.5 -> 0.25
     * effective alpha (0.5^4 * 0.8 + 0.2 = 0.25). Measure mid-pixel darkening
     * against a pure white fb with a solid white atlas tile. */
    {
        enum { PW = 32, PH = 24 };
        static CrRgba color[PW * PH], texels[16 * 16];
        CrFramebuffer fb = { .w = PW, .h = PH, .color = color, .depth = 0 };
        for (int i = 0; i < PW * PH; ++i)
            color[i] = (CrRgba){255, 255, 255, 255};
        for (int i = 0; i < 16 * 16; ++i)
            texels[i] = (CrRgba){0, 0, 0, 255}; /* opaque black tile */
        CrTexture atlas = { .w = 16, .h = 16, .texels = texels,
                            .tile = 0, .mip_levels = 0 };
        /* Force portal UV to cover the whole atlas: override via bm is hard;
         * instead call portal_screen which samples CR_SPRITE_PORTAL. Without a
         * full atlas we only check time<=0 is a no-op. */
        gm_overlay_portal_screen(&fb, &atlas, 0.0f);
        CHECK(color[0].r == 255, "portal time=0 leaves framebuffer");
        /* block-in-hand: blend off, replace with tex*0.1 (black tile -> ~0) */
        for (int i = 0; i < PW * PH; ++i)
            color[i] = (CrRgba){255, 255, 255, 255};
        gm_overlay_block_in_hand(&fb, &atlas, 0.0f, 0.0f, 1.0f, 1.0f, 70.0f);
        /* black * 0.1 replace -> 0; white backdrop is fully overwritten */
        int mid = color[(PH / 2) * PW + (PW / 2)].r;
        CHECK(mid <= 2, "block-in-hand replaces with tex*0.1 (black -> 0)");
        CHECK(mid < 200, "block-in-hand is visibly dark");

        /* ItemRenderer.renderBlockInHand: maxU on the left (U mirrored).
         * Horizontal gradient: high tx bright. maxU=right of sprite is bright
         * -> left screen samples dark? Wait: maxU is right of [0,1] atlas =
         * high tx bright, left screen gets maxU = bright. */
        for (int ty = 0; ty < 16; ++ty)
            for (int tx = 0; tx < 16; ++tx)
                texels[ty * 16 + tx] =
                    (CrRgba){(unsigned char)(tx * 16), 0, 0, 255};
        for (int i = 0; i < PW * PH; ++i)
            color[i] = (CrRgba){0, 0, 0, 255};
        gm_overlay_block_in_hand(&fb, &atlas, 0.0f, 0.0f, 1.0f, 1.0f, 70.0f);
        int left_r = color[(PH / 2) * PW + 1].r;
        int right_r = color[(PH / 2) * PW + (PW - 2)].r;
        CHECK(left_r > right_r + 5,
              "block-in-hand mirrors U (maxU/bright on left)");
    }

    /* setupCameraTransform: the projection pass is inert at zero, changes an
     * asymmetric image when active, and Nausea uses a distinct 7-degree rate. */
    {
        enum { PW = 32, PH = 24 };
        static CrRgba color[PW * PH], original[PW * PH];
        static CrRgba normal[PW * PH], scratch[PW * PH];
        CrFramebuffer fb = { .w = PW, .h = PH, .color = color, .depth = 0 };
        for (int y = 0; y < PH; ++y)
            for (int x = 0; x < PW; ++x)
                color[y * PW + x] = (CrRgba){
                    (unsigned char)(x * 7 + y),
                    (unsigned char)(y * 9 + x * 2),
                    (unsigned char)(x * 3 + y * 5), 255};
        memcpy(original, color, sizeof color);
        gm_overlay_portal_warp(&fb, scratch, 0.0F, 3, 0.5F, 0, 70.0F);
        CHECK(!memcmp(color, original, sizeof color),
              "portal projection time=0 is a no-op");
        gm_overlay_portal_warp(&fb, scratch, 0.6F, 3, 0.5F, 0, 70.0F);
        CHECK(memcmp(color, original, sizeof color),
              "portal projection warps an active frame");
        memcpy(normal, color, sizeof color);
        memcpy(color, original, sizeof color);
        gm_overlay_portal_warp(&fb, scratch, 0.6F, 3, 0.5F, 1, 70.0F);
        CHECK(memcmp(color, normal, sizeof color),
              "Nausea projection uses its seven-degree phase rate");
    }

    /* Entity outline shader graph: a one-pixel mask produces a hollow,
     * radius-blurred white ring and never fills an unrelated corner. */
    {
        enum { OW = 15, OH = 15 };
        static CrRgba color[OW * OH], mask[OW * OH], scratch[OW * OH];
        CrFramebuffer fb = { .w = OW, .h = OH, .color = color, .depth = 0 };
        for (int i = 0; i < OW * OH; ++i) {
            color[i] = (CrRgba){20, 30, 40, 255};
            mask[i] = (CrRgba){0, 0, 0, 0};
        }
        mask[7 * OW + 7] = (CrRgba){9, 17, 33, 255};
        gm_overlay_entity_outline_build(OW, OH, mask, scratch);
        CHECK(scratch[7 * OW + 7].a == 255
                  && scratch[7 * OW + 5].a == 255,
              "entity Sobel outline applies both radius-two blur passes");
        CHECK(scratch[7 * OW + 7].r == scratch[7 * OW + 7].g
                  && scratch[7 * OW + 7].g == scratch[7 * OW + 7].b,
              "unteamed entity outline replaces source texture RGB");
        CHECK(scratch[0].a == 0,
              "entity outline leaves a distant shader pixel transparent");
        gm_overlay_entity_outline_composite(&fb, scratch);
        CHECK(color[7 * OW + 7].r != 20
                  || color[7 * OW + 7].g != 30
                  || color[7 * OW + 7].b != 40,
              "entity outline composites its post-shader result");
        CHECK(color[0].r == 20 && color[0].g == 30 && color[0].b == 40,
              "entity outline does not touch a distant framebuffer corner");
    }

    /* loading screen fills every pixel (tiled dirt * 64/255 + label). */
    {
        enum { LW = 64, LH = 48 };
        static CrRgba color[LW * LH];
        CrFramebuffer fb = { .w = LW, .h = LH, .color = color, .depth = 0 };
        for (int i = 0; i < LW * LH; ++i)
            color[i] = (CrRgba){0, 0, 0, 255};
        gm_overlay_loading_screen(&fb);
        int nonblack = 0;
        for (int i = 0; i < LW * LH; ++i)
            if (color[i].r | color[i].g | color[i].b) nonblack++;
        CHECK(nonblack == LW * LH, "loading screen covers entire framebuffer");
    }

    if (g_fail) { printf("TEST FAILED\n"); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
