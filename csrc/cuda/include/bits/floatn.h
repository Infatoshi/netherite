/* The device build of the tick: glibc's floatn.h without _Float128, which
 * nvptx64 does not have (the engine never uses it). */
#ifndef _BITS_FLOATN_H
#define _BITS_FLOATN_H
#include <features.h>
#define __HAVE_FLOAT128 0
#define __HAVE_DISTINCT_FLOAT128 0
#define __HAVE_FLOAT64X 1
#define __HAVE_FLOAT64X_LONG_DOUBLE 1
#include <bits/floatn-common.h>
#endif
