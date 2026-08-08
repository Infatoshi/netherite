/* qao.c - JNI native port of MC 1.11.2 BlockModelRenderer$AmbientOcclusionFace
 * getAoBrightness() (the smooth-AO 4-way brightness average, kernel 13).
 * Bit-exact with render-opt/kernels/13_ao_pack_helpers/candidate.c (verified golden):
 *   zero-substitute (br1/2/3 <- br4 if 0), sum the four, arithmetic >>2, mask 0x00FF00FF.
 * Java int add wraps; do the sum in uint32_t then reinterpret as int32_t so the >>2
 * sign-extends exactly like the JVM. Two JNI entries (one .so):
 *   Java_TestJniAO_naoBrightness  -> standalone Phase-A bit-exactness test
 *   Java_netheritemod_QAOHook_naoBrightness -> live in-engine route. getAoBrightness is rewritten by a
 *     raw IClassTransformer coremod (OverclockingClassTransformer) to call netheritemod.QAOHook, which
 *     dispatches here in native mode - this bypasses Mixin entirely (Mixin cannot attach to the
 *     package-private inner class AmbientOcclusionFace in this Forge+FML-remapper dev setup).
 * Build: cc -O2 -ffp-contract=off -shared -fPIC -I$JAVA_HOME/include \
 *           -I$JAVA_HOME/include/linux qao.c -o libqao.so
 */
#include <jni.h>
#include <stdint.h>
#include <stdio.h>

__attribute__((constructor))
static void loaded(void) { fprintf(stderr, "[qao] libqao.so loaded\n"); fflush(stderr); }

static int32_t ao_brightness(int32_t br1, int32_t br2, int32_t br3, int32_t br4) {
    if (br1 == 0) br1 = br4;
    if (br2 == 0) br2 = br4;
    if (br3 == 0) br3 = br4;
    uint32_t sum = (uint32_t)br1 + (uint32_t)br2 + (uint32_t)br3 + (uint32_t)br4;
    int32_t shifted = (int32_t)sum >> 2;   /* arithmetic shift, matches Java signed >> */
    return shifted & 16711935;
}

JNIEXPORT jint JNICALL Java_TestJniAO_naoBrightness(JNIEnv *env, jclass cls,
                                                    jint br1, jint br2, jint br3, jint br4) {
    return (jint)ao_brightness(br1, br2, br3, br4);
}

static int g_first = 1;

JNIEXPORT jint JNICALL Java_netheritemod_QAOHook_naoBrightness(JNIEnv *env, jclass cls,
                                                     jint br1, jint br2, jint br3, jint br4) {
    if (g_first) { g_first = 0;
        fprintf(stderr, "[qao] native naoBrightness() INVOKED in live render path (proof)\n"); fflush(stderr); }
    return (jint)ao_brightness(br1, br2, br3, br4);
}
