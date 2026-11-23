/* The semantic camera (semcam.h): one source in two scalars (semcam_inst.h), the FP64 reference and the FP32 instance
 * (semcam_spec.fp 32, client.cast_precision). */
#include "semcam.h"

#include <math.h>
#include <string.h>

#include "../engine/jmath.h"
#include "../engine/player.h"
#include "../engine/session.h"
#include "../engine/world.h"

static int liquid_of(int id)
{
    return id == 8 || id == 9 ? 8 : id == 10 || id == 11 ? 10 : 0;
}

/* tan(35 degrees): half the renderer's vertical field of view of 70 */
#define SC_FP 64
#define SC_R double
#define SC_N(x) x##64
#define SC_K(x) x
#define SC_SQRT sqrt
#define SC_FABS fabs
#define SC_FLOOR floor
#define SC_TAN_HALF 0.7002075382097097
#include "semcam_inst.h"
#undef SC_FP
#undef SC_R
#undef SC_N
#undef SC_K
#undef SC_SQRT
#undef SC_FABS
#undef SC_FLOOR
#undef SC_TAN_HALF

/* the FP32 instance: every operation from the eye's offset on a float's (raycast.h's definition); tan 35 degrees
 * 0.700207531f */
#define SC_FP 32
#define SC_R float
#define SC_N(x) x##32
#define SC_K(x) x##f
#define SC_SQRT sqrtf
#define SC_FABS fabsf
#define SC_FLOOR floorf
#define SC_TAN_HALF 0.7002075382097097f
#include "semcam_inst.h"

void semcam_rays(const struct semcam_spec *sp, float yaw, float pitch, double *dir)
{
    struct basis64 b;
    basis_make64(sp, yaw, pitch, &b);
    for (int j = 0; j < sp->h; ++j)
        for (int i = 0; i < sp->w; ++i) ray64(sp, &b, i, j, &dir[3 * ((size_t)j * (size_t)sp->w + (size_t)i)]);
}

int semcam_cast(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out)
{
    return sp->fp == 32 ? cast_run32(sp, ss, out, 1) : cast_run64(sp, ss, out, 1);
}

int semcam_cast_ref(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out)
{
    return sp->fp == 32 ? cast_run32(sp, ss, out, 0) : cast_run64(sp, ss, out, 0);
}
