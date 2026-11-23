#include "jmath.h"

#include <math.h>

float MH_SIN[65536];

/* before main: the same for every environment */
__attribute__((constructor)) static void jmath_table(void)
{
    for (int i = 0; i < 65536; ++i) MH_SIN[i] = (float)sin((double)i * 3.141592653589793 * 2.0 / 65536.0);
}

/* The table is built before main; kept for the callers that still ask. */
void jmath_init(void)
{
}
