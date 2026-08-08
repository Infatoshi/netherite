/* qsin.c - JNI native port of Minecraft 1.11.2 MathHelper.sin() table lookup.
 * Bit-exact with render-opt/kernels/01_sin_cos_table/candidate.c (verified golden).
 * Builds the 65536-entry SIN_TABLE = (float)sin((double)i*PI*2/65536) at dlopen,
 * then sin(value) = SIN_TABLE[(java_f2i(value*10430.378f)) & 65535].
 * Exposes TWO JNI entry points (one .so):
 *   Java_netheritemod_QSinNative_nsin  -> used inside the Minecraft client (netheritemod.QSinNative)
 *   Java_TestJni_nsin         -> used by the standalone Phase-A test
 * Build: cc -O2 -ffp-contract=off -shared -fPIC -I$JAVA_HOME/include \
 *           -I$JAVA_HOME/include/linux qsin.c -o libqsin.so
 */
#include <jni.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float SIN_TABLE[65536];

__attribute__((constructor))
static void build_table(void) {
    for (int i = 0; i < 65536; ++i) {
        SIN_TABLE[i] = (float)sin((double)i * M_PI * 2.0 / 65536.0);
    }
    fprintf(stderr, "[qsin] libqsin.so loaded; SIN_TABLE[65536] built\n");
    fflush(stderr);
}

/* Java narrowing float->int (JLS 5.1.3): NaN->0, saturate, else truncate toward zero. */
static int java_f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT_MAX;
    if (f <= -2147483648.0f) return INT_MIN;
    return (int)f;
}

static float sin_lut(float value) {
    return SIN_TABLE[java_f2i(value * 10430.378f) & 65535];
}

static int g_first = 1;

JNIEXPORT jfloat JNICALL Java_netheritemod_QSinNative_nsin(JNIEnv *env, jclass cls, jfloat value) {
    if (g_first) { g_first = 0; fprintf(stderr, "[qsin] native nsin() INVOKED in render path (proof)\n"); fflush(stderr); }
    return sin_lut(value);
}

JNIEXPORT jfloat JNICALL Java_TestJni_nsin(JNIEnv *env, jclass cls, jfloat value) {
    return sin_lut(value);
}
