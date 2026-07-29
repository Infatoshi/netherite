/* CANDIDATE: C port of MC 1.11.2 ModelQuadruped.setRotationAngles (used by ModelCow).
 *   src/net/minecraft/client/model/ModelQuadruped.java:82
 *   (ModelCow extends ModelQuadruped and does NOT override setRotationAngles.)
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook").
 *
 * Input (golden/inputs.txt): one line per sample, 5 floats stored as raw int bits:
 *     limbSwing  limbSwingAmount  ageInTicks  netHeadYaw  headPitch
 *   (ageInTicks is read by the method signature but unused in the cow path.)
 *
 * Output (golden/golden.txt): 18 raw-bits-int floats per sample, 3 per limb
 *   (rotateAngleX, rotateAngleY, rotateAngleZ) for limbs in this order:
 *     head, body, leg1, leg2, leg3, leg4.
 *
 * setRotationAngles sets only:
 *   head.rotateAngleX = headPitch * 0.017453292F
 *   head.rotateAngleY = netHeadYaw * 0.017453292F
 *   body.rotateAngleX = PI/2
 *   leg1.rotateAngleX = cos(limbSwing*0.6662F)        * 1.4F * limbSwingAmount
 *   leg2.rotateAngleX = cos(limbSwing*0.6662F + PI)   * 1.4F * limbSwingAmount
 *   leg3.rotateAngleX = cos(limbSwing*0.6662F + PI)   * 1.4F * limbSwingAmount
 *   leg4.rotateAngleX = cos(limbSwing*0.6662F)        * 1.4F * limbSwingAmount
 * everything else stays 0 (ModelRenderer default).
 *
 * MathHelper.cos via the same 65536-entry SIN_TABLE as kernel 01. Build with
 * -ffp-contract=off (runner does) so the float index math rounds like the JVM. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Java's (float)Math.PI */
#define JPI 3.14159265358979323846f

static float SIN_TABLE[65536];

static void build_table(void) {
    for (int i = 0; i < 65536; ++i)
        SIN_TABLE[i] = (float)sin((double)i * M_PI * 2.0 / 65536.0);
}

/* Java narrowing float->int (JLS 5.1.3): NaN->0, saturate +/-2^31, else trunc toward zero. */
static int java_f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT_MAX;
    if (f <= -2147483648.0f) return INT_MIN;
    return (int)f;
}

static float mh_cos(float value) {
    return SIN_TABLE[java_f2i(value * 10430.378f + 16384.0f) & 65535];
}

static float bits_to_f(int32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
static int32_t f_to_bits(float f) { int32_t b; memcpy(&b, &f, sizeof b); return b; }

static void out(float f) { printf("%d\n", f_to_bits(f)); }

int main(void) {
    build_table();
    int32_t a, b, c, d, e;
    while (scanf("%d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
        float limbSwing       = bits_to_f(a);
        float limbSwingAmount = bits_to_f(b);
        /* float ageInTicks  = bits_to_f(c);  unused */
        float netHeadYaw      = bits_to_f(d);
        float headPitch       = bits_to_f(e);

        float headX = headPitch * 0.017453292f;
        float headY = netHeadYaw * 0.017453292f;
        float bodyX = (float)M_PI / 2.0f;
        float leg1X = mh_cos(limbSwing * 0.6662f)       * 1.4f * limbSwingAmount;
        float leg2X = mh_cos(limbSwing * 0.6662f + JPI) * 1.4f * limbSwingAmount;
        float leg3X = mh_cos(limbSwing * 0.6662f + JPI) * 1.4f * limbSwingAmount;
        float leg4X = mh_cos(limbSwing * 0.6662f)       * 1.4f * limbSwingAmount;

        /* head */
        out(headX); out(headY); out(0.0f);
        /* body */
        out(bodyX); out(0.0f); out(0.0f);
        /* leg1..leg4 */
        out(leg1X); out(0.0f); out(0.0f);
        out(leg2X); out(0.0f); out(0.0f);
        out(leg3X); out(0.0f); out(0.0f);
        out(leg4X); out(0.0f); out(0.0f);
        (void)c;
    }
    return 0;
}
