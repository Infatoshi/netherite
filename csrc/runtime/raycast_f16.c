/* The textured ray cast's FP16 instance (lane/castexact, raycast.h "The instances"): raycast.c compiled with the half
 * scalar (_Float16, each operation rounded to half: raycast.mk builds this file with gcc's -fexcess-precision=16 or
 * clang's float16 excess precision none) and no lanes (Zen 5 has no FP16 arithmetic: the scalar walk alone), its entry
 * points named *_f16 (raycast.c's own call them for a spec with fp 16). */
#define RC_FP 16
#define RC_NO_SIMD 1
#include "raycast.c"

#if defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
/* gcc lowers every _Float16 <-> float conversion to a libgcc call (__extendhfsf2, __truncsfhf2) without AVX512-FP16,
 * -mf16c or not: at 128x128 they were 37 M of the cast's 45.5 M cycles an env-step. These are the same two
 * conversions by F16C's instructions (vcvtph2ps exact; vcvtps2ph rounding to nearest, ties to even, as the calls do),
 * hidden in the library, so the bytes are the calls' */
#include <immintrin.h>
__attribute__((visibility("hidden"), target("f16c"))) float __extendhfsf2(_Float16 x)
{
    uint16_t b;
    memcpy(&b, &x, 2);
    return _cvtsh_ss(b);
}
__attribute__((visibility("hidden"), target("f16c"))) _Float16 __truncsfhf2(float x)
{
    const uint16_t b = (uint16_t)_cvtss_sh(x, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    _Float16 h;
    memcpy(&h, &b, 2);
    return h;
}
#endif
