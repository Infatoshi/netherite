/* qbiome.c - JNI native port of MC 1.11.2 BiomeColorHelper.getColorAtPos() 3x3 blend.
 * Bit-exact with render-opt/kernels/18_biome_color_blend/candidate.c (verified golden).
 * Sums the 9 packed 0xRRGGBB colors channel-wise, integer-divides each by 9, repacks:
 *     (i/9 & 255)<<16 | (j/9 & 255)<<8 | k/9 & 255
 * Integer arithmetic only -> bitwise identical to vanilla.
 * Dual JNI entry (one .so):
 *   Java_netheritemod_QBiomeNative_nblend   -> in the Minecraft client (netheritemod.QBiomeNative)
 *   Java_TestJniBiome_nblend       -> standalone Phase-A bit-exactness test
 * Build: cc -O2 -ffp-contract=off -shared -fPIC -I$JAVA_HOME/include \
 *           -I$JAVA_HOME/include/linux qbiome.c -o libqbiome.so
 */
#include <jni.h>
#include <stdio.h>

__attribute__((constructor))
static void loaded(void) {
    fprintf(stderr, "[qbiome] libqbiome.so loaded\n"); fflush(stderr);
}

static int blend9(const jint c[9]) {
    int i = 0, j = 0, k = 0;
    for (int n = 0; n < 9; ++n) {
        int l = (int)c[n];
        i += (l & 16711680) >> 16;
        j += (l & 65280) >> 8;
        k += l & 255;
    }
    return (i / 9 & 255) << 16 | (j / 9 & 255) << 8 | k / 9 & 255;
}

static int g_first = 1;

JNIEXPORT jint JNICALL Java_netheritemod_QBiomeNative_nblend(JNIEnv *env, jclass cls, jintArray arr) {
    jint c[9];
    (*env)->GetIntArrayRegion(env, arr, 0, 9, c);
    if (g_first) { g_first = 0;
        fprintf(stderr, "[qbiome] native nblend() INVOKED in render path (proof)\n"); fflush(stderr); }
    return (jint)blend9(c);
}

JNIEXPORT jint JNICALL Java_TestJniBiome_nblend(JNIEnv *env, jclass cls, jintArray arr) {
    jint c[9];
    (*env)->GetIntArrayRegion(env, arr, 0, 9, c);
    return (jint)blend9(c);
}
