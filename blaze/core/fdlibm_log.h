#ifndef MC_FDLIBM_LOG_H
#define MC_FDLIBM_LOG_H

/* fdlibm log used by java.lang.StrictMath. Keep the evaluation order: the
 * last bits of Random.nextGaussian are observable in particle velocities. */
MC_HD static inline double mc_strict_log(double x) {
    static const double ln2_hi = 6.93147180369123816490e-01;
    static const double ln2_lo = 1.90821492927058770002e-10;
    static const double two54 = 1.80143985094819840000e+16;
    static const double Lg1 = 6.666666666666735130e-01;
    static const double Lg2 = 3.999999999940941908e-01;
    static const double Lg3 = 2.857142874366239149e-01;
    static const double Lg4 = 2.222219843214978396e-01;
    static const double Lg5 = 1.818357216161805012e-01;
    static const double Lg6 = 1.531383769920937332e-01;
    static const double Lg7 = 1.479819860511658591e-01;
    union { double value; u64 bits; } word = { x };
    i32 hx = (i32)(word.bits >> 32);
    u32 lx = (u32)word.bits;
    int k = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | (i32)lx) == 0) {
            word.bits = UINT64_C(0xfff0000000000000);
            return word.value;
        }
        if (hx < 0) {
            word.bits = UINT64_C(0x7ff8000000000000);
            return word.value;
        }
        k -= 54;
        x *= two54;
        word.value = x;
        hx = (i32)(word.bits >> 32);
    }
    if (hx >= 0x7ff00000)
        return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    int i = (hx + 0x95f64) & 0x100000;
    word.value = x;
    word.bits = (word.bits & UINT64_C(0xffffffff))
        | ((u64)(u32)(hx | (i ^ 0x3ff00000)) << 32);
    x = word.value;
    k += i >> 20;
    double f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) {
        if (f == 0.0) {
            if (k == 0) return 0.0;
            double dk = (double)k;
            return dk * ln2_hi + dk * ln2_lo;
        }
        double R = f * f * (0.5 - 0.33333333333333333333 * f);
        if (k == 0) return f - R;
        double dk = (double)k;
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
    double s = f / (2.0 + f);
    double dk = (double)k;
    double z = s * s;
    i = hx - 0x6147a;
    double w = z * z;
    int j = 0x6b851 - hx;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    double R = t2 + t1;
    if (i > 0) {
        double hfsq = 0.5 * f * f;
        if (k == 0)
            return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi
            - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }
    if (k == 0)
        return f - s * (f - R);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

#endif
