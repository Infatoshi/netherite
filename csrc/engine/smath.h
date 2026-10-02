/* fdlibm's sin, cos and pow, which are java.lang.Math.sin/cos and
 * StrictMath.pow. Libm's sin/cos agree with fdlibm's on the values these
 * lanes feed it (they round to the same float afterwards), but StrictMath's
 * contract is fdlibm, and a 1-ulp double difference can flip a float near a
 * rounding midpoint, so the port carries fdlibm rather than libm. The square
 * root stays the IEEE one (det_sqrt). Same lineage and style as det_log. */
#ifndef NETHERITE_SMATH_H
#define NETHERITE_SMATH_H

double fd_sin(double x);
double fd_cos(double x);
double fd_pow(double x, double y);

/* fdlibm's atan2 (and the atan it needs), java.lang.StrictMath.atan2. */
double fd_atan2(double y, double x);

#endif