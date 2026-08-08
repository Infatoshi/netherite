/* game/test_view.c - look/move consistency: the direction the CAMERA faces must
 * equal the direction MC PHYSICS moves a forward-walking player, for every yaw.
 *
 * This is the regression test for the 2026-07-10 "movement is not relative to
 * where I'm looking" bug: cam_from_view used magma_yaw = mc_yaw - 180 (an
 * X-mirror of the correct 180 - mc_yaw) and input_map negated dyaw/dpitch to
 * compensate, so LOOK felt right while WALK diverged from the view direction
 * at every yaw except spawn's 180. Three chained checks, all through the real
 * code (game/view.h conversion, core/math.c cr_look_yaw_pitch camera matrix,
 * game/input_map.c signs):
 *
 *   1. camera world-forward == vanilla Entity.getVectorForRotation(pitch, yaw)
 *   2. camera world-forward (pitch 0) == vanilla moveFlying displacement for
 *      forward=1 strafe=0 (EntityLivingBase.moveEntityWithHeading formula,
 *      ported in blaze core/player_physics_full.h)
 *   3. a positive mouse_dx through gm_input_map turns the camera forward
 *      RIGHT (clockwise seen from above), matching EntityPlayerSP.turn
 *
 * Build+run: bash game/test_view.sh
 */
#include "core/types.h"
#include "game/view.h"
#include "game/input_map.h"

#include <math.h>
#include <stdio.h>

static int g_fail = 0;

#define TOL 1e-5f

static void expect_v3(const char *what, float yaw_deg,
                      float gx, float gy, float gz,
                      float wx, float wy, float wz)
{
    if (fabsf(gx - wx) > TOL || fabsf(gy - wy) > TOL || fabsf(gz - wz) > TOL) {
        printf("FAIL %s @ yaw %.1f: got (%.6f %.6f %.6f) want (%.6f %.6f %.6f)\n",
               what, (double)yaw_deg,
               (double)gx, (double)gy, (double)gz,
               (double)wx, (double)wy, (double)wz);
        g_fail = 1;
    }
}

/* World-space forward of the magma camera for an MC-convention yaw/pitch:
 * build the REAL view matrix (cr_look_yaw_pitch, column-major) and take
 * -row2 of its rotation block (eye space looks down -Z). The 0.05 eye-space
 * z-offset in the matrix is a translation and does not touch the rotation. */
static void cam_forward(float mc_yaw_deg, float mc_pitch_deg,
                        float *fx, float *fy, float *fz)
{
    CrVec3 pos = { 0.0f, 0.0f, 0.0f };
    CrMat4 v = cr_look_yaw_pitch(pos,
                                 gm_view_cam_yaw_rad(mc_yaw_deg),
                                 gm_view_cam_pitch_rad(mc_pitch_deg));
    *fx = -v.m[2];
    *fy = -v.m[6];
    *fz = -v.m[10];
}

int main(void)
{
    static const float yaws[]    = { 0.f, 30.f, 45.f, 90.f, 135.f, 180.f,
                                     225.f, 270.f, 315.f, -30.f, -90.f };
    static const float pitches[] = { 0.f, 30.f, -30.f, 60.f, -60.f };
    const int ny = (int)(sizeof yaws / sizeof yaws[0]);
    const int np = (int)(sizeof pitches / sizeof pitches[0]);

    /* 1. camera forward == Entity.getVectorForRotation for every yaw/pitch */
    for (int i = 0; i < ny; ++i) {
        for (int j = 0; j < np; ++j) {
            float m = yaws[i] * GM_VIEW_DEG2RAD, p = pitches[j] * GM_VIEW_DEG2RAD;
            /* vanilla look vector: x=-sin(yaw)cos(pitch), y=-sin(pitch),
             * z=cos(yaw)cos(pitch) */
            float wx = -sinf(m) * cosf(p), wy = -sinf(p), wz = cosf(m) * cosf(p);
            float fx, fy, fz;
            cam_forward(yaws[i], pitches[j], &fx, &fy, &fz);
            expect_v3("camera==getVectorForRotation", yaws[i], fx, fy, fz, wx, wy, wz);
        }
    }

    /* 2. camera forward (pitch 0) == moveFlying displacement, forward=1 strafe=0:
     * motionX += strafe*cos(yaw) - forward*sin(yaw);
     * motionZ += forward*cos(yaw) + strafe*sin(yaw)   (player_physics_full.h) */
    for (int i = 0; i < ny; ++i) {
        float m = yaws[i] * GM_VIEW_DEG2RAD;
        float mx = 0.0f * cosf(m) - 1.0f * sinf(m);
        float mz = 1.0f * cosf(m) + 0.0f * sinf(m);
        float fx, fy, fz;
        cam_forward(yaws[i], 0.0f, &fx, &fy, &fz);
        expect_v3("camera==moveFlying(W)", yaws[i], fx, fy, fz, mx, 0.0f, mz);
    }

    /* 3. +mouse_dx through gm_input_map turns the camera RIGHT: for the yaw'
     * = yaw + dyaw camera forward f', cross(f, f').y < 0 (clockwise from
     * above). Also +mouse_dy must pitch the view DOWN (forward.y decreases). */
    gm_input_reset();
    for (int i = 0; i < ny; ++i) {
        CrInput in = { 0 };
        in.mouse_dx = 10; in.mouse_dy = 10;
        GmAction a = gm_input_map(&in, 0.15f);
        float f0x, f0y, f0z, f1x, f1y, f1z;
        cam_forward(yaws[i], 0.0f, &f0x, &f0y, &f0z);
        cam_forward(yaws[i] + a.dyaw, a.dpitch, &f1x, &f1y, &f1z);
        float crossy = f0z * f1x - f0x * f1z;
        if (!(crossy < 0.0f)) {
            printf("FAIL mouse-right turns right @ yaw %.1f: cross.y=%.6f\n",
                   (double)yaws[i], (double)crossy);
            g_fail = 1;
        }
        if (!(f1y < f0y)) {
            printf("FAIL mouse-down looks down @ yaw %.1f: fy %.6f -> %.6f\n",
                   (double)yaws[i], (double)f0y, (double)f1y);
            g_fail = 1;
        }
    }

    if (g_fail) {
        printf("TEST FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
