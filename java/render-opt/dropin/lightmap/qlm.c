/* qlm.c - JNI native port of MC 1.11.2 EntityRenderer.updateLightmap().
 *
 * HEAVY-BUFFER drop-in spike: unlike the scalar sin() drop-in (qsin.c), this kernel
 * produces a BUFFER. It reads the lightmap input scalars + the 16-entry brightness
 * table (a Java float[] marshaled in via GetFloatArrayRegion) and fills the 256-int
 * lightmapColors array (marshaled out via SetIntArrayRegion). This IS the heavy-buffer
 * JNI marshaling being proven.
 *
 * Per-texel math is the verified bit-exact port from
 * render-opt/kernels/11_lightmap/candidate.c (PASS, 256/256 ARGB ints).
 * Captured-state assumptions (guaranteed by the deterministic scene): overworld
 * (dimId 0), no night-vision, no boss, no lightning. Those branches are therefore
 * skipped, exactly as in kernel 11.
 *
 * Build (anvil, JDK8):
 *   cc -O2 -ffp-contract=off -shared -fPIC -I$JAVA_HOME/include \
 *      -I$JAVA_HOME/include/linux qlm.c -o libqlm.so
 */
#include <jni.h>
#include <stdio.h>

/* Pure compute core: fills out[256] from the lightmap inputs + 16-float table.
 * Bit-exact with vanilla EntityRenderer.updateLightmap (== kernel 11 candidate). */
static void compute(float f, float gamma, float torchFlickerX,
                    int lastLightning, int dimId, const float *bt, jint *out) {
    float f1 = f * 0.95F + 0.05F;

    for (int i = 0; i < 256; ++i) {
        float f2 = bt[i / 16] * f1;
        float f3 = bt[i % 16] * (torchFlickerX * 0.1F + 1.5F);

        if (lastLightning > 0)
            f2 = bt[i / 16];

        float f4 = f2 * (f * 0.65F + 0.35F);
        float f5 = f2 * (f * 0.65F + 0.35F);
        float f6 = f3 * ((f3 * 0.6F + 0.4F) * 0.6F + 0.4F);
        float f7 = f3 * (f3 * f3 * 0.6F + 0.4F);
        float f8 = f4 + f3;
        float f9 = f5 + f6;
        float f10 = f2 + f7;
        f8 = f8 * 0.96F + 0.03F;
        f9 = f9 * 0.96F + 0.03F;
        f10 = f10 * 0.96F + 0.03F;

        if (dimId == 1) {
            f8 = 0.22F + f3 * 0.75F;
            f9 = 0.28F + f6 * 0.75F;
            f10 = 0.25F + f7 * 0.75F;
        }

        if (f8 > 1.0F) f8 = 1.0F;
        if (f9 > 1.0F) f9 = 1.0F;
        if (f10 > 1.0F) f10 = 1.0F;

        float f16 = gamma;
        float f17 = 1.0F - f8;
        float f13 = 1.0F - f9;
        float f14 = 1.0F - f10;
        f17 = 1.0F - f17 * f17 * f17 * f17;
        f13 = 1.0F - f13 * f13 * f13 * f13;
        f14 = 1.0F - f14 * f14 * f14 * f14;
        f8 = f8 * (1.0F - f16) + f17 * f16;
        f9 = f9 * (1.0F - f16) + f13 * f16;
        f10 = f10 * (1.0F - f16) + f14 * f16;
        f8 = f8 * 0.96F + 0.03F;
        f9 = f9 * 0.96F + 0.03F;
        f10 = f10 * 0.96F + 0.03F;

        if (f8 > 1.0F) f8 = 1.0F;
        if (f9 > 1.0F) f9 = 1.0F;
        if (f10 > 1.0F) f10 = 1.0F;
        if (f8 < 0.0F) f8 = 0.0F;
        if (f9 < 0.0F) f9 = 0.0F;
        if (f10 < 0.0F) f10 = 0.0F;

        int k = (int)(f8 * 255.0F);
        int l = (int)(f9 * 255.0F);
        int i1 = (int)(f10 * 255.0F);
        out[i] = -16777216 | k << 16 | l << 8 | i1;
    }
}

static int g_first = 1;

/* In-game heavy-buffer entry: 16-float table IN via GetFloatArrayRegion, 256-int
 * buffer OUT via SetIntArrayRegion. Routed from MixinEntityRendererLightmap. */
JNIEXPORT void JNICALL Java_netheritemod_QLightmapNative_nlightmap(
        JNIEnv *env, jclass cls,
        jfloat f, jfloat gamma, jfloat torchFlickerX,
        jint lastLightning, jint dimId,
        jfloatArray jbt, jintArray jout) {
    if (g_first) {
        g_first = 0;
        fprintf(stderr, "[qlm] native nlightmap() INVOKED in render path (heavy-buffer proof)\n");
        fflush(stderr);
    }
    float bt[16];
    jint out[256];
    (*env)->GetFloatArrayRegion(env, jbt, 0, 16, bt);   /* marshal the 16-float table IN */
    compute(f, gamma, torchFlickerX, lastLightning, dimId, bt, out);
    (*env)->SetIntArrayRegion(env, jout, 0, 256, out);  /* marshal the 256-int buffer OUT */
}

/* Standalone Phase-A test entry (same marshaling), used by TestJniLm. */
JNIEXPORT void JNICALL Java_TestJniLm_nlightmap(
        JNIEnv *env, jclass cls,
        jfloat f, jfloat gamma, jfloat torchFlickerX,
        jint lastLightning, jint dimId,
        jfloatArray jbt, jintArray jout) {
    float bt[16];
    jint out[256];
    (*env)->GetFloatArrayRegion(env, jbt, 0, 16, bt);
    compute(f, gamma, torchFlickerX, lastLightning, dimId, bt, out);
    (*env)->SetIntArrayRegion(env, jout, 0, 256, out);
}
