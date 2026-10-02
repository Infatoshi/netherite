/* net.minecraft.util.MathHelper, bit for bit. */
#ifndef NETHERITE_JMATH_H
#define NETHERITE_JMATH_H

#include <stdint.h>
#include "jfloor.h"

extern float MH_SIN[65536];

/* MH_SIN is built before main (jmath.c) and only read after. Libm's sin gives
 * Java's table exactly: every entry's double lies at least 2,502 ulps from a
 * float rounding midpoint, so any sin with error under 1 ulp (fdlibm, glibc,
 * Apple) rounds to the same float. jmath_init does nothing now; it stays for
 * the callers that still ask for the table. */
void jmath_init(void);

static inline float mh_sin(float f)
{
    return MH_SIN[j_f2i(f * 10430.378f) & 65535];
}

static inline float mh_cos(float f)
{
    return MH_SIN[j_f2i(f * 10430.378f + 16384.0f) & 65535];
}

#endif
