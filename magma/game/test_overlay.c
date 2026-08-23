/* game/test_overlay.c - geometry invariants for the selection/dig overlay.
 * Every quad is emitted in both windings (so one survives backface culling from
 * either side), vertex counts match the documented budget, and all crack UVs
 * stay inside the destroy_stage sprite rect for every damage stage.
 * Build+run: bash game/test_overlay.sh */
#include "game/overlay.h"
#include "game/sel_box.h"
#include "game/view.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "core/types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Independent GL post-multiply stack for EntityRenderer.setupCameraTransform
 * (java:712-764) + orientCamera first-person (java:681,697-702). Mesa
 * glRotatef / glScalef / glTranslatef, column-major. Not cr_camera_view. */
static void gl_ident(float m[16])
{
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static void gl_mul(float o[16], const float a[16], const float b[16])
{
    float r[16];
    int col, row, k;
    for (col = 0; col < 4; col++)
        for (row = 0; row < 4; row++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++)
                s += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = s;
        }
    memcpy(o, r, sizeof r);
}
static void gl_translate(float m[16], float x, float y, float z)
{
    float t[16];
    gl_ident(t);
    t[12] = x;
    t[13] = y;
    t[14] = z;
    gl_mul(m, m, t);
}
static void gl_scale(float m[16], float x, float y, float z)
{
    float t[16];
    gl_ident(t);
    t[0] = x;
    t[5] = y;
    t[10] = z;
    gl_mul(m, m, t);
}
static void gl_rotate(float m[16], float angle, float x, float y, float z)
{
    float mag = sqrtf(x * x + y * y + z * z);
    float rad, c, s, one_c, xx, yy, zz, xy, yz, zx, xs, ys, zs;
    float r[16];
    x /= mag;
    y /= mag;
    z /= mag;
    rad = angle * 0.01745329251994329577f;
    c = cosf(rad);
    s = sinf(rad);
    one_c = 1.0f - c;
    xx = x * x;
    yy = y * y;
    zz = z * z;
    xy = x * y;
    yz = y * z;
    zx = z * x;
    xs = x * s;
    ys = y * s;
    zs = z * s;
    gl_ident(r);
    r[0] = one_c * xx + c;
    r[4] = one_c * xy - zs;
    r[8] = one_c * zx + ys;
    r[1] = one_c * xy + zs;
    r[5] = one_c * yy + c;
    r[9] = one_c * yz - xs;
    r[2] = one_c * zx - ys;
    r[6] = one_c * yz + xs;
    r[10] = one_c * zz + c;
    gl_mul(m, m, r);
}
static void gl_frustum_persp(float m[16], float fovy, float aspect,
                             float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.01745329251994329577f * 0.5f);
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}
/* java:746-764 world modelview, then orientCamera first-person. */
static void java_portal_view(float out[16], float time_in_portal, float spin_deg,
                             float feet_x, float feet_y, float feet_z,
                             float eye_h, float mc_yaw_deg, float mc_pitch_deg)
{
    float f2, yaw;
    gl_ident(out);
    if (time_in_portal > 0.0f) {
        f2 = 5.0f / (time_in_portal * time_in_portal + 5.0f)
             - time_in_portal * 0.04f; /* java:757 */
        f2 = f2 * f2;                  /* java:758 */
        gl_rotate(out, spin_deg, 0.0f, 1.0f, 1.0f);           /* java:759 */
        gl_scale(out, 1.0f / f2, 1.0f, 1.0f);                 /* java:760 */
        gl_rotate(out, -spin_deg, 0.0f, 1.0f, 1.0f);          /* java:761 */
    }
    gl_translate(out, 0.0f, 0.0f, 0.05f);                     /* java:681 */
    gl_rotate(out, mc_pitch_deg, 1.0f, 0.0f, 0.0f);           /* java:698 */
    yaw = mc_yaw_deg + 180.0f;                                /* java:686 */
    gl_rotate(out, yaw, 0.0f, 1.0f, 0.0f);                    /* java:699 */
    gl_translate(out, -feet_x, -feet_y - eye_h, -feet_z);     /* java:702 + chunk -pos */
}
static void project_screen(const float proj[16], const float view[16],
                           float wx, float wy, float wz, int fb_w, int fb_h,
                           float *sx, float *sy)
{
    float mvp[16], p[4], c[4];
    int i, j;
    gl_mul(mvp, proj, view);
    p[0] = wx;
    p[1] = wy;
    p[2] = wz;
    p[3] = 1.0f;
    for (i = 0; i < 4; i++) {
        c[i] = 0.0f;
        for (j = 0; j < 4; j++)
            c[i] += mvp[j * 4 + i] * p[j];
    }
    *sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)fb_w;
    *sy = (0.5f - c[1] / c[3] * 0.5f) * (float)fb_h;
}

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

    /* GuiIngame.java:1114-1118 fourth-power ease. */
    CHECK(fabsf(gm_overlay_portal_ease(0.5f) - 0.25f) < 1e-6f,
          "timeInPortal 0.5 eases to 0.25");
    CHECK(gm_overlay_portal_ease(1.0f) == 1.0f,
          "timeInPortal 1.0 is identity");
    CHECK(gm_overlay_portal_ease(0.0f) == 0.2f,
          "timeInPortal 0 eases to 0.2 (renderPortal not called at 0)");

    /* EntityRenderer.java:757-758 scale at time 0.5. */
    {
        float f = 0.5f;
        float f2 = 5.0f / (f * f + 5.0f) - f * 0.04f;
        f2 = f2 * f2;
        CHECK(fabsf(f2 - 0.869334399f) < 1e-6f,
              "portal RSR f2 at 0.5 matches 5/(t^2+5)-t*0.04, squared");
        CrCamera a, b;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        a.fov_deg = b.fov_deg = 70.0f;
        a.aspect = b.aspect = 854.0f / 480.0f;
        a.znear = b.znear = 0.05f;
        a.zfar = b.zfar = 128.0f;
        b.portal_time = 0.5f;
        b.portal_spin_deg = 20.0f; /* (phase 0 + partialTicks 1)*20 */
        CrMat4 va = cr_camera_view(&a);
        CrMat4 vb = cr_camera_view(&b);
        int differ = 0;
        for (int i = 0; i < 16; ++i)
            if (va.m[i] != vb.m[i]) differ = 1;
        CHECK(differ, "portal RSR changes the world view matrix");
        /* Hand camera must not inherit the WORLD RSR (renderHand.java:804). */
        CHECK(a.portal_time == 0.0f, "zeroed camera has portal_time 0");
        /* Later nausea (1.13+ / wiki) is not 1.11.2. */
        CHECK(fabsf(1.0f / f2 - 1.0f / (1.0f + 0.5f * 0.2f)) > 0.2f,
              "1.11.2 scale 1/f2 is not 1/(1+t*0.2)");
    }

    /* Warped projection of a known world point: overlay_portal_050 pose
     * (8.5,5,8.5) yaw 0 pitch 0, timeInPortal 0.5, phase 0, pt=1 -> spin 20.
     * Independent GL stack vs cr_camera_view. */
    {
        const float feet_x = 8.5f, feet_y = 5.0f, feet_z = 8.5f, eye_h = 1.62f;
        const float t = 0.5f, spin = 20.0f;
        const int fb_w = 854, fb_h = 480;
        float jview[16], jproj[16], jview0[16];
        float jsx, jsy, csx, csy, hsx, hsy, jsx0, jsy0;
        CrCamera cam, hand;
        CrMat4 cv, pv, mvp;
        CrVec4 wp, clip;
        java_portal_view(jview, t, spin, feet_x, feet_y, feet_z, eye_h, 0.0f, 0.0f);
        java_portal_view(jview0, 0.0f, 0.0f, feet_x, feet_y, feet_z, eye_h, 0.0f, 0.0f);
        gl_frustum_persp(jproj, 70.0f, (float)fb_w / (float)fb_h, 0.05f,
                         128.0f * 1.41421356237f);
        memset(&cam, 0, sizeof cam);
        cam.pos.x = feet_x;
        cam.pos.y = feet_y + eye_h;
        cam.pos.z = feet_z;
        cam.yaw = gm_view_cam_yaw_rad(0.0f);
        cam.pitch = gm_view_cam_pitch_rad(0.0f);
        cam.fov_deg = 70.0f;
        cam.aspect = (float)fb_w / (float)fb_h;
        cam.znear = 0.05f;
        cam.zfar = 128.0f * 1.41421356237f;
        cam.portal_time = t;
        cam.portal_spin_deg = spin;
        hand = cam;
        hand.portal_time = 0.0f;
        hand.portal_spin_deg = 0.0f;
        cv = cr_camera_view(&cam);
        pv = cr_perspective(cam.fov_deg, cam.aspect, cam.znear, cam.zfar);
        mvp = cr_mat4_mul(pv, cv);
        /* Stone-wall south-west corner on the capture pad (ui_hud_scene). */
        project_screen(jproj, jview, 6.0f, 5.0f, 11.0f, fb_w, fb_h, &jsx, &jsy);
        wp.x = 6.0f;
        wp.y = 5.0f;
        wp.z = 11.0f;
        wp.w = 1.0f;
        clip = cr_mat4_mul_vec4(mvp, wp);
        csx = (clip.x / clip.w * 0.5f + 0.5f) * (float)fb_w;
        csy = (0.5f - clip.y / clip.w * 0.5f) * (float)fb_h;
        CHECK(fabsf(csx - jsx) < 1e-3f && fabsf(csy - jsy) < 1e-3f,
              "cr_camera_view wall corner matches Java GL RSR stack");
        CHECK(csx > 800.0f && csx < 830.0f && csy > 430.0f && csy < 460.0f,
              "wall corner stays on-screen lower-right under RSR");
        /* Same point, hand pass: renderHand reloads gluPerspective without RSR
         * (java:791-804). */
        {
            CrMat4 hv = cr_camera_view(&hand);
            CrMat4 hm = cr_mat4_mul(pv, hv);
            CrVec4 hc = cr_mat4_mul_vec4(hm, wp);
            hsx = (hc.x / hc.w * 0.5f + 0.5f) * (float)fb_w;
            hsy = (0.5f - hc.y / hc.w * 0.5f) * (float)fb_h;
            project_screen(jproj, jview0, 6.0f, 5.0f, 11.0f, fb_w, fb_h,
                           &jsx0, &jsy0);
            CHECK(fabsf(hsx - jsx0) < 1e-3f && fabsf(hsy - jsy0) < 1e-3f,
                  "hand camera projection is the unwarped Java stack");
            CHECK(fabsf(hsx - csx) > 1.0f || fabsf(hsy - csy) > 1.0f,
                  "world RSR moves the wall corner vs the hand camera");
        }
        /* Grass plane point that lands on the right-horizon cluster column
         * (y 235-238, x 818-853): world x=-44.90, z=58.5 (D=50 south). */
        project_screen(jproj, jview, -44.90f, 5.0f, 58.5f,
                       fb_w, fb_h, &jsx, &jsy);
        wp.x = -44.90f;
        wp.y = 5.0f;
        wp.z = 58.5f;
        clip = cr_mat4_mul_vec4(mvp, wp);
        csx = (clip.x / clip.w * 0.5f + 0.5f) * (float)fb_w;
        csy = (0.5f - clip.y / clip.w * 0.5f) * (float)fb_h;
        CHECK(fabsf(csx - jsx) < 1e-3f && fabsf(csy - jsy) < 1e-3f,
              "cr_camera_view grass point matches Java GL RSR stack");
        CHECK(fabsf(csx - 835.0f) < 0.1f && fabsf(csy - 235.87f) < 0.1f,
              "RSR maps the D=50 right-grass point onto the horizon cluster");
    }

    /* NEAREST stretch of the 16x16 portal sprite, tex.a * ease blend.
     * White dst, time=0.5 -> out = src*a + 255*(1-a), a = src.a/255 * 0.25. */
    {
        bm_atlas_set_portal_frame(0);
        CrTexture atlas = bm_atlas();
        CrAtlasSprite sp = CR_ATLAS_SPRITES[CR_SPRITE_PORTAL];
        enum { PW = 32, PH = 16 };
        static CrRgba color[PW * PH];
        CrFramebuffer fb = { .w = PW, .h = PH, .color = color, .depth = 0 };
        for (int i = 0; i < PW * PH; ++i)
            color[i] = (CrRgba){255, 255, 255, 255};
        gm_overlay_portal_screen(&fb, &atlas, 0.5f);
        /* Pixel (0,0) samples sprite texel (0,0) under (x+0.5) nearest. */
        CrRgba src = atlas.texels[sp.y0 * atlas.w + sp.x0];
        float a = ((float)src.a / 255.0f) * 0.25f;
        int want = (int)((float)src.r * a + 255.0f * (1.0f - a) + 0.5f);
        CHECK(color[0].r == (u8)want,
              "portal overlay NEAREST+tex.a blend at pixel 0");
        CHECK(color[0].r != 255, "portal overlay darkens a white fb");
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
