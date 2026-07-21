/* core/frustum.h - view-frustum plane extraction + AABB cull test.
 *
 * Header-only, static-inline VERBATIM port of the two bit-verified render-opt
 * kernels (both "status: verified" against decompiled MC 1.11.2):
 *   - kernel 05 frustum_plane_extract  (ClippingHelperImpl.init + normalize)
 *   - kernel 06 aabb_frustum_test      (ClippingHelper.isBoxInFrustum + dot)
 * The op order is preserved exactly; build the consumer with -ffp-contract=off so
 * the extract() math stays bit-faithful to the MC reference.
 *
 * Matrix layout: cr_frustum_extract takes the GL column-major projection and
 * modelview (== view) matrices EXACTLY as core/math.c's cr_perspective and
 * cr_look_yaw_pitch emit them (CrMat4.m[16], element (row,col) at m[col*4+row]).
 * That is the same layout MC hands to GlStateManager.getFloat, so the extracted
 * clippingMatrix == projection*modelview and the 6 planes match ClippingHelperImpl.
 *
 * cr_aabb_in_frustum returns 1 if the world-space AABB is (conservatively) inside
 * the frustum, 0 if it is FULLY outside. Like MC's test it can keep a box that is
 * just outside (false positive), but it NEVER culls a box that is inside (no false
 * negative), so no visible chunk is ever dropped.
 */
#ifndef MAGMA_CORE_FRUSTUM_H
#define MAGMA_CORE_FRUSTUM_H

#include <math.h>

/* MathHelper.sqrt(float) = (float)Math.sqrt((double)value) */
static inline float crf__sqrt(float value) { return (float)sqrt((double)value); }

static inline void crf__normalize(float *p) {
    float f = crf__sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    p[0] /= f;
    p[1] /= f;
    p[2] /= f;
    p[3] /= f;
}

/* Extract the 6 normalized frustum planes into out[6][4] (RIGHT,LEFT,BOTTOM,TOP,
 * FAR,NEAR order, matching MC). projectionMatrix + modelviewMatrix are 16-float
 * GL column-major arrays (pass CrMat4.m). */
static inline void cr_frustum_extract(const float *projectionMatrix,
                                      const float *modelviewMatrix,
                                      float out[6][4]) {
    float frustum[6][4];
    float clippingMatrix[16];
    const float *afloat = projectionMatrix;
    const float *afloat1 = modelviewMatrix;
    clippingMatrix[0] = afloat1[0] * afloat[0] + afloat1[1] * afloat[4] + afloat1[2] * afloat[8] + afloat1[3] * afloat[12];
    clippingMatrix[1] = afloat1[0] * afloat[1] + afloat1[1] * afloat[5] + afloat1[2] * afloat[9] + afloat1[3] * afloat[13];
    clippingMatrix[2] = afloat1[0] * afloat[2] + afloat1[1] * afloat[6] + afloat1[2] * afloat[10] + afloat1[3] * afloat[14];
    clippingMatrix[3] = afloat1[0] * afloat[3] + afloat1[1] * afloat[7] + afloat1[2] * afloat[11] + afloat1[3] * afloat[15];
    clippingMatrix[4] = afloat1[4] * afloat[0] + afloat1[5] * afloat[4] + afloat1[6] * afloat[8] + afloat1[7] * afloat[12];
    clippingMatrix[5] = afloat1[4] * afloat[1] + afloat1[5] * afloat[5] + afloat1[6] * afloat[9] + afloat1[7] * afloat[13];
    clippingMatrix[6] = afloat1[4] * afloat[2] + afloat1[5] * afloat[6] + afloat1[6] * afloat[10] + afloat1[7] * afloat[14];
    clippingMatrix[7] = afloat1[4] * afloat[3] + afloat1[5] * afloat[7] + afloat1[6] * afloat[11] + afloat1[7] * afloat[15];
    clippingMatrix[8] = afloat1[8] * afloat[0] + afloat1[9] * afloat[4] + afloat1[10] * afloat[8] + afloat1[11] * afloat[12];
    clippingMatrix[9] = afloat1[8] * afloat[1] + afloat1[9] * afloat[5] + afloat1[10] * afloat[9] + afloat1[11] * afloat[13];
    clippingMatrix[10] = afloat1[8] * afloat[2] + afloat1[9] * afloat[6] + afloat1[10] * afloat[10] + afloat1[11] * afloat[14];
    clippingMatrix[11] = afloat1[8] * afloat[3] + afloat1[9] * afloat[7] + afloat1[10] * afloat[11] + afloat1[11] * afloat[15];
    clippingMatrix[12] = afloat1[12] * afloat[0] + afloat1[13] * afloat[4] + afloat1[14] * afloat[8] + afloat1[15] * afloat[12];
    clippingMatrix[13] = afloat1[12] * afloat[1] + afloat1[13] * afloat[5] + afloat1[14] * afloat[9] + afloat1[15] * afloat[13];
    clippingMatrix[14] = afloat1[12] * afloat[2] + afloat1[13] * afloat[6] + afloat1[14] * afloat[10] + afloat1[15] * afloat[14];
    clippingMatrix[15] = afloat1[12] * afloat[3] + afloat1[13] * afloat[7] + afloat1[14] * afloat[11] + afloat1[15] * afloat[15];
    float *afloat2 = frustum[0];
    afloat2[0] = clippingMatrix[3] - clippingMatrix[0];
    afloat2[1] = clippingMatrix[7] - clippingMatrix[4];
    afloat2[2] = clippingMatrix[11] - clippingMatrix[8];
    afloat2[3] = clippingMatrix[15] - clippingMatrix[12];
    crf__normalize(afloat2);
    float *afloat3 = frustum[1];
    afloat3[0] = clippingMatrix[3] + clippingMatrix[0];
    afloat3[1] = clippingMatrix[7] + clippingMatrix[4];
    afloat3[2] = clippingMatrix[11] + clippingMatrix[8];
    afloat3[3] = clippingMatrix[15] + clippingMatrix[12];
    crf__normalize(afloat3);
    float *afloat4 = frustum[2];
    afloat4[0] = clippingMatrix[3] + clippingMatrix[1];
    afloat4[1] = clippingMatrix[7] + clippingMatrix[5];
    afloat4[2] = clippingMatrix[11] + clippingMatrix[9];
    afloat4[3] = clippingMatrix[15] + clippingMatrix[13];
    crf__normalize(afloat4);
    float *afloat5 = frustum[3];
    afloat5[0] = clippingMatrix[3] - clippingMatrix[1];
    afloat5[1] = clippingMatrix[7] - clippingMatrix[5];
    afloat5[2] = clippingMatrix[11] - clippingMatrix[9];
    afloat5[3] = clippingMatrix[15] - clippingMatrix[13];
    crf__normalize(afloat5);
    float *afloat6 = frustum[4];
    afloat6[0] = clippingMatrix[3] - clippingMatrix[2];
    afloat6[1] = clippingMatrix[7] - clippingMatrix[6];
    afloat6[2] = clippingMatrix[11] - clippingMatrix[10];
    afloat6[3] = clippingMatrix[15] - clippingMatrix[14];
    crf__normalize(afloat6);
    float *afloat7 = frustum[5];
    afloat7[0] = clippingMatrix[3] + clippingMatrix[2];
    afloat7[1] = clippingMatrix[7] + clippingMatrix[6];
    afloat7[2] = clippingMatrix[11] + clippingMatrix[10];
    afloat7[3] = clippingMatrix[15] + clippingMatrix[14];
    crf__normalize(afloat7);
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 4; ++j)
            out[i][j] = frustum[i][j];
}

/* dot() promotes the float plane coeffs to double, matching MC's ClippingHelper. */
static inline double crf__dot(const float *p, double x, double y, double z) {
    return (double)p[0] * x + (double)p[1] * y + (double)p[2] * z + (double)p[3];
}

/* isBoxInFrustum(frustum, minX, minY, minZ, maxX, maxY, maxZ): 1 inside, 0 out. */
static inline int cr_aabb_in_frustum(const float frustum[6][4],
                                     double p1, double p3, double p5,
                                     double p7, double p9, double p11) {
    for (int i = 0; i < 6; ++i) {
        const float *afloat = frustum[i];
        if (crf__dot(afloat, p1, p3, p5) <= 0.0 && crf__dot(afloat, p7, p3, p5) <= 0.0 && crf__dot(afloat, p1, p9, p5) <= 0.0 && crf__dot(afloat, p7, p9, p5) <= 0.0 && crf__dot(afloat, p1, p3, p11) <= 0.0 && crf__dot(afloat, p7, p3, p11) <= 0.0 && crf__dot(afloat, p1, p9, p11) <= 0.0 && crf__dot(afloat, p7, p9, p11) <= 0.0) {
            return 0;
        }
    }
    return 1;
}

#endif /* MAGMA_CORE_FRUSTUM_H */
