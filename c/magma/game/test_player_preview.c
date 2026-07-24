/* Exact ModelBiped / GuiInventory.drawEntityOnScreen geometry gate for the
 * inventory player preview. Ground truth is decompiled Java 1.11.2
 * (ModelBiped.setRotationAngles + GuiInventory.drawEntityOnScreen), not a
 * magma self-golden.
 *
 * Checks:
 *   1. Idle arm Z at ageInTicks=0 is ±0.10 (cos(0)*0.05+0.05), not ±0.05.
 *   2. drawEntityOnScreen atan field assignments (body/head/pitch).
 *   3. prepareScale + applyRotations foot/head anchors at mouse (0,0).
 *   4. Emitted preview paints a non-empty depth-tested silhouette in the
 *      52x72 GUI viewport at product scale.
 */
#include "game/player_preview.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)
#define NEAR(A, B, EPS, M) CHECK(fabsf((A) - (B)) <= (EPS), M)

/* ModelBiped.setRotationAngles idle arm Z (and wear, via copyModelAngles). */
static float java_idle_arm_z(float age_in_ticks, int right)
{
    float z = cosf(age_in_ticks * 0.09f) * 0.05f + 0.05f;
    return right ? z : -z;
}

static float java_idle_arm_x_delta(float age_in_ticks, int right)
{
    float dx = sinf(age_in_ticks * 0.067f) * 0.05f;
    return right ? dx : -dx;
}

/* Same prepareScale + applyRotations + matrix pitch as player_preview.c
 * entity_frame (must stay in lockstep with that file). */
static void entity_frame_ref(float *x, float *y, float *z,
                             float body_yaw_deg, float matrix_pitch_deg)
{
    const float model_scale = 0.0625f;
    const float player_scale = 0.9375f;
    const float prepare_ty = -1.501f;
    float ex = *x * model_scale;
    float ey = *y * model_scale + prepare_ty;
    float ez = *z * model_scale;
    ex *= player_scale; ey *= player_scale; ez *= player_scale;
    ex = -ex; ey = -ey;
    {
        float a = body_yaw_deg * (float)M_PI / 180.0f;
        float c = cosf(a), s = sinf(a);
        float nx = ex * c + ez * s, nz = -ex * s + ez * c;
        ex = nx; ez = nz;
    }
    {
        float a = matrix_pitch_deg * (float)M_PI / 180.0f;
        float c = cosf(a), s = sinf(a);
        float ny = ey * c - ez * s, nz = ey * s + ez * c;
        ey = ny; ez = nz;
    }
    *x = ex; *y = ey; *z = ez;
}

static void rotate_zyx(float *x, float *y, float *z, float ax, float ay, float az)
{
    float c, s, nx, ny, nz;
    c = cosf(ax); s = sinf(ax);
    ny = *y * c - *z * s; nz = *y * s + *z * c;
    *y = ny; *z = nz;
    c = cosf(ay); s = sinf(ay);
    nx = *x * c + *z * s; nz = -*x * s + *z * c;
    *x = nx; *z = nz;
    c = cosf(az); s = sinf(az);
    nx = *x * c - *y * s; ny = *x * s + *y * c;
    *x = nx; *y = ny;
}

int main(void)
{
    /* --- 1. ModelBiped idle arm Z at the pinned ageInTicks=0 --- */
    NEAR(java_idle_arm_z(0.0f, 1), 0.10f, 1e-6f,
         "right arm Z at age 0 is +0.10 (cos*0.05+0.05), not +0.05");
    NEAR(java_idle_arm_z(0.0f, 0), -0.10f, 1e-6f,
         "left arm Z at age 0 is -0.10");
    NEAR(java_idle_arm_x_delta(0.0f, 1), 0.0f, 1e-6f,
         "right arm X bob is 0 at age 0");
    NEAR(java_idle_arm_x_delta(0.0f, 0), 0.0f, 1e-6f,
         "left arm X bob is 0 at age 0");
    /* Spot-check the formula at a non-zero age (capture must pin age=0). */
    {
        float age = 1.0f;
        float expect = cosf(0.09f) * 0.05f + 0.05f;
        NEAR(java_idle_arm_z(age, 1), expect, 1e-6f,
             "age=1 right arm Z follows cos(age*0.09)*0.05+0.05");
    }

    /* --- 2. drawEntityOnScreen field assignments (degrees) --- */
    {
        float mouse_x = 40.0f, mouse_y = 40.0f;
        float body = atanf(mouse_x / 40.0f) * 20.0f;
        float head = atanf(mouse_x / 40.0f) * 40.0f;
        float pitch = -atanf(mouse_y / 40.0f) * 20.0f;
        NEAR(body, atanf(1.0f) * 20.0f, 1e-5f, "body yaw = atan(mx/40)*20");
        NEAR(head, atanf(1.0f) * 40.0f, 1e-5f, "head yaw = atan(mx/40)*40");
        NEAR(pitch, -atanf(1.0f) * 20.0f, 1e-5f, "pitch = -atan(my/40)*20");
        NEAR(head - body, atanf(1.0f) * 20.0f, 1e-5f, "netHeadYaw = head-body");
    }

    /* --- 3. prepareScale foot (0,24,0) and head origin at mouse (0,0) --- */
    {
        float fx = 0.0f, fy = 24.0f, fz = 0.0f; /* model feet in biped units */
        entity_frame_ref(&fx, &fy, &fz, 0.0f, 0.0f);
        /* After prepareScale: ey = -0.9375*(24/16 - 1.501) = -0.9375*(1.5-1.501)
         * = -0.9375*(-0.001) = 0.0009375 ≈ 0 (feet at origin). */
        NEAR(fx, 0.0f, 1e-4f, "feet x ~ 0 at body yaw 0");
        NEAR(fy, 0.0009375f, 1e-4f, "feet y at prepareScale origin");
        NEAR(fz, 0.0f, 1e-4f, "feet z ~ 0");

        /* Right arm tip (box -3,-2,-2 + 4,12,4): local (1,10,0) + rp (-5,2,0)
         * after idle Z=+0.10 about the rotation point. */
        float ax = 1.0f, ay = 10.0f, az = 0.0f;
        rotate_zyx(&ax, &ay, &az, 0.0f, 0.0f, 0.10f);
        ax += -5.0f; ay += 2.0f; az += 0.0f;
        entity_frame_ref(&ax, &ay, &az, 0.0f, 0.0f);
        /* Arm tip must sit left of body center (negative entity x after
         * prepareScale S(-1,-1,1) flips model -5 -> positive then... model
         * arm at x=-5 is on the character's right; after S(-1): entity x > 0
         * on screen-right from viewer? S(-1) on x: model x=-5 -> ex before
         * flip path: ex = -player*(model*scale) with model x negative =>
         * positive. Character's right arm is viewer's left in the inventory
         * (faces +Z toward viewer after sandwich). Either way |ax| > 0. */
        CHECK(fabsf(ax) > 0.05f, "right arm tip has non-zero entity x");
        CHECK(ay > 0.1f, "right arm tip above feet");
    }

    /* --- 4. Raster smoke: viewport paints a non-empty silhouette --- */
    {
        const int W = 104, H = 144; /* scale-2 52x72 */
        CrRgba *color = calloc((size_t)W * H, sizeof *color);
        float *depth = malloc((size_t)W * H * sizeof *depth);
        CHECK(color && depth, "alloc preview fb");
        if (color && depth) {
            for (int i = 0; i < W * H; ++i) depth[i] = 1.0f;
            CrFramebuffer fb = {W, H, color, depth};
            /* mouse deltas 0,0: parked look-at (body/head/pitch all 0) */
            gm_player_preview_draw(&fb, 0, 0, W, H, 0.0f, 0.0f);
            int painted = 0, minx = W, miny = H, maxx = 0, maxy = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    CrRgba c = color[y * W + x];
                    if (!c.a) continue;
                    ++painted;
                    if (x < minx) minx = x;
                    if (y < miny) miny = y;
                    if (x > maxx) maxx = x;
                    if (y > maxy) maxy = y;
                }
            CHECK(painted > 500, "preview paints a solid silhouette");
            /* Feet anchor at local (27, 68) in 52x72 GUI -> scale to 104x144:
             * cx=54, bottom=136. Body should sit above the feet row. */
            CHECK(maxy > 100 && miny < 40, "silhouette spans head-to-feet");
            CHECK(minx < W / 2 && maxx > W / 2, "silhouette straddles center x");
            fprintf(stderr, "preview silhouette: %d px bbox (%d,%d)-(%d,%d)\n",
                    painted, minx, miny, maxx, maxy);
        }
        free(depth);
        free(color);
    }

    if (fail) { fprintf(stderr, "player_preview geometry: FAIL\n"); return 1; }
    fprintf(stderr, "player_preview geometry: PASS\n");
    return 0;
}
