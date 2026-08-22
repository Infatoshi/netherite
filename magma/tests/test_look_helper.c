/* EntityLookHelper.onUpdateLook / updateRotation (EntityLookHelper.java:64-116).
 * WatchClosest uses getHorizontalFaceSpeed=10, getVerticalFaceSpeed=40
 * (EntityLiving.java:881-888, EntityAIWatchClosest.java:98).
 * AttackMelee uses 30F, 30F (EntityAIAttackMelee.java:114).
 * Pitch/yaw atan2 is MathHelper.atan2 LUT (EntityLookHelper.java:75-76);
 * magma CPU uses mc_atan2 (host-only on the CUDA path, blaze/core/mc_math.h).
 * t=42 tape pitch is a look-target clock gap, not this math. */
#include "mc_math.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;

static float wrap_degrees(float v)
{
    /* MathHelper.wrapDegrees (MathHelper.java:228-242). */
    v = fmodf(v, 360.0f);
    if (v >= 180.0f) v -= 360.0f;
    if (v < -180.0f) v += 360.0f;
    return v;
}

static float update_rotation(float cur, float tgt, float maxd)
{
    /* EntityLookHelper.updateRotation (EntityLookHelper.java:101-115). */
    float f = wrap_degrees(tgt - cur);
    if (f > maxd) f = maxd;
    if (f < -maxd) f = -maxd;
    return cur + f;
}

static float deg(double rad)
{
    /* LUT * (float)(180.0/(float)PI); remainder match, not dmul. */
    return (float)(rad * (float)(180.0 / (float)MC_PI));
}

static uint32_t fbits(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    return u;
}

static void expect_f(const char *tag, float got, float want)
{
    if (fbits(got) != fbits(want)) {
        printf("FAIL: %s got %.9g (0x%08x) want %.9g (0x%08x)\n",
               tag, got, fbits(got), want, fbits(want));
        fails++;
    }
}

int main(void)
{
    float pitch, head, target_yaw, target_pitch;
    double dx, dy, dz, horiz;
    float pl_eye, z_eye;

    /* Clamp: |delta| > max_delta saturates. */
    expect_f("clamp+40", update_rotation(0.0f, 80.0f, 40.0f), 40.0f);
    expect_f("clamp-40", update_rotation(0.0f, -80.0f, 40.0f), -40.0f);
    expect_f("clamp+30", update_rotation(0.0f, 80.0f, 30.0f), 30.0f);
    expect_f("inside40", update_rotation(0.0f, 2.702361f, 40.0f), 2.702361f);
    expect_f("inside30", update_rotation(0.0f, 2.702361f, 30.0f), 2.702361f);

    /* onUpdateLook: rotationPitch = 0 then updateRotation (java:66,77).
     * Watch yaw step is 10F (EntityLiving.getHorizontalFaceSpeed), not 40F. */
    {
        float stepped10 = update_rotation(-30.14488f, -42.83676f, 10.0f);
        float stepped40 = update_rotation(-30.14488f, -42.83676f, 40.0f);
        float unclamped = wrap_degrees(-42.83676f - (-30.14488f));
        if (!(stepped10 < -40.0f && stepped10 > -41.0f)) {
            printf("FAIL: watch yaw 10F step got %.9g (raw delta %.9g)\n",
                   stepped10, unclamped);
            fails++;
        }
        /* 40F would consume the whole remaining ~12.7 deg. */
        expect_f("melee_yaw_would_finish", stepped40, -42.83676f);
        if (unclamped > -10.0f || unclamped < -13.0f) {
            printf("FAIL: expected ~-12.7 deg remaining, got %.9g\n", unclamped);
            fails++;
        }
    }

    /* Zombie 53.5,74,126.5 eye 1.74F looking at tape pl t=40
     * (55.228570, 74, 128.364285) player eye 1.62F.
     * Tape t=41 pitch 2.702361 is this look (WatchClosest, pitch from 0). */
    pl_eye = 1.62f;
    z_eye = 1.74f;
    dx = 55.228570 - 53.5;
    dy = (74.0 + (double)pl_eye) - (74.0 + (double)z_eye);
    dz = 128.364285 - 126.5;
    horiz = (double)(float)sqrt(dx * dx + dz * dz);
    target_yaw = deg(mc_atan2(dz, dx)) - 90.0f;
    target_pitch = -deg(mc_atan2(dy, horiz));
    pitch = update_rotation(0.0f, target_pitch, 40.0f);
    head = update_rotation(-30.14488f, target_yaw, 10.0f);
    expect_f("look_pitch_t40", pitch, target_pitch);
    if (fabsf(pitch - 2.702361f) > 1.0e-5f) {
        printf("FAIL: t40 look pitch %.9g want ~2.702361\n", pitch);
        fails++;
    }
    if (fabsf(target_yaw + 42.83676f) > 1.0e-3f) {
        printf("FAIL: t40 look yaw %.9g want ~-42.83676\n", target_yaw);
        fails++;
    }
    (void)head;

    if (fails) {
        printf("%d FAIL\n", fails);
        return 1;
    }
    printf("ok look-helper updateRotation/WatchClosest/mc_atan2\n");
    return 0;
}
