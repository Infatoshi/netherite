/* mathhelper: PORT TARGET util/math/MathHelper.java (sin/cos table + floor)
 * Thin shim over core/mc_math.h so the oracle can verify mc_sin/mc_cos/mc_floor in isolation
 * against a verbatim-Java MathHelper golden, separate from the ore_gen feature logic. */
#ifndef MC_MATHHELPER_H
#define MC_MATHHELPER_H

#include "mc_math.h"

/* Deterministic, seed-independent sweep shared by cpu/cuda/golden (keep these identical). */
#define MH_NT 12567   /* trig inputs: (float)((i-6283)*0.01), -62.83 .. +62.83 */
#define MH_NF 2001    /* floor inputs: (i-1000)*0.123, -123.0 .. +123.0 */

#endif
