/* The native port of the client's per-frame render state; see renderstate.h.
 * Every Java statement is one C statement, in order, on floats: Java rounds
 * each step and C must not fuse them (-ffp-contract=off). MathHelper.sin/cos
 * go through jmath's table; StrictMath.pow and the fdlibm sin/cos pair go
 * through smath. */
#include "renderstate.h"

#include <math.h>
#include <string.h>

#include "jmath.h"
#include "smath.h"

#define RS_PI 3.141592653589793     /* Math.PI */
#define RS_PIF 3.1415927f           /* (float)Math.PI */
#define RS_DEG2RAD 0.017453292f     /* MathHelper's degrees to radians */

static float rs_getrain(const struct rs_in *in, float pt)
{
    return in->prain + (in->rain - in->prain) * pt;
}

static float rs_getthunder(const struct rs_in *in, float pt)
{
    return (in->pthu + (in->thu - in->pthu) * pt) * rs_getrain(in, pt);
}

/* The provider of the frame being computed: WorldProviderHell's constant
 * celestial angle (0.5) and fog colour stand in for the surface provider's. */
static int rs_dim;

/* WorldProvider.calculateCelestialAngle */
static float rs_celestial(int64_t wt, float pt)
{
    if (rs_dim == -1) return 0.5f;
    if (rs_dim == 1) return 0.0f;

    int32_t v4 = (int32_t)(wt % 24000);
    float v5 = ((float)v4 + pt) / 24000.0f - 0.25f;

    if (v5 < 0.0f) v5 = v5 + 1.0f;
    if (v5 > 1.0f) v5 = v5 - 1.0f;

    float v6 = v5;
    float v7 = (float)((fd_cos((double)v5 * RS_PI) + 1.0) / 2.0);
    v5 = 1.0f - v7;
    v5 = v6 + (v5 - v6) / 3.0f;
    return v5;
}

/* World.getSunBrightness */
static float rs_sunbrightness(const struct rs_in *in, float pt)
{
    float a = rs_celestial(in->wt, pt);
    float v3 = 1.0f - (mh_cos(a * RS_PIF * 2.0f) * 2.0f + 0.2f);

    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;

    v3 = 1.0f - v3;
    v3 = (float)((double)v3 * (1.0 - (double)(rs_getrain(in, pt) * 5.0f) / 16.0));
    v3 = (float)((double)v3 * (1.0 - (double)(rs_getthunder(in, pt) * 5.0f) / 16.0));
    return v3 * 0.8f + 0.2f;
}

/* World.getStarBrightness */
static float rs_star(int64_t wt, float pt)
{
    float a = rs_celestial(wt, pt);
    float v3 = 1.0f - (mh_cos(a * RS_PIF * 2.0f) * 2.0f + 0.25f);

    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;

    return v3 * v3 * 0.5f;
}

/* WorldProvider.calcSunriseSunsetColors */
static int rs_sunrise(int64_t wt, float pt, float *r)
{
    float a = rs_celestial(wt, pt);
    float v4 = mh_cos(a * RS_PIF * 2.0f) - 0.0f;
    float v5 = -0.0f;

    if (v4 >= v5 - 0.4f && v4 <= v5 + 0.4f)
    {
        float v6 = (v4 - v5) / 0.4f * 0.5f + 0.5f;
        float v7 = 1.0f - (1.0f - mh_sin(v6 * RS_PIF)) * 0.99f;
        v7 = v7 * v7;
        r[0] = v6 * 0.3f + 0.7f;
        r[1] = v6 * v6 * 0.7f + 0.2f;
        r[2] = v6 * v6 * 0.0f + 0.2f;
        r[3] = v7;
        return 1;
    }
    return 0;
}

/* WorldProvider.getFogColor, the surface provider (WorldProviderHell's is a
 * constant) */
static void rs_providerfog(int64_t wt, float pt, double *v)
{
    if (rs_dim == -1)
    {
        v[0] = 0.20000000298023224;
        v[1] = 0.029999999329447746;
        v[2] = 0.029999999329447746;
        return;
    }

    if (rs_dim == 1)
    {
        /* WorldProviderEnd.getFogColor: 0xA080A0 with a constant 0.15 factor
         * (the var4 term multiplies by 0.0F, so the clamp never matters).
         * Each channel is (float)(byte) / 255.0F as Java divides it: the
         * literal 0.50196078f is an ulp below 128 / 255.0F. */
        v[0] = (float)(0xA080A0 >> 16 & 255) / 255.0f * 0.15f;
        v[1] = (float)(0xA080A0 >> 8 & 255) / 255.0f * 0.15f;
        v[2] = (float)(0xA080A0 & 255) / 255.0f * 0.15f;
        return;
    }

    float a = rs_celestial(wt, pt);
    float v3 = mh_cos(a * RS_PIF * 2.0f) * 2.0f + 0.5f;

    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;

    float v4 = 0.7529412f;
    float v5 = 0.84705883f;
    float v6 = 1.0f;
    v4 = v4 * (v3 * 0.94f + 0.06f);
    v5 = v5 * (v3 * 0.94f + 0.06f);
    v6 = v6 * (v3 * 0.91f + 0.09f);
    v[0] = (double)v4;
    v[1] = (double)v5;
    v[2] = (double)v6;
}

/* World.getSkyColor; the biome's temperature and sky color arrive as inputs */
static void rs_skycolor(const struct rs_in *in, double *v)
{
    float a = rs_celestial(in->wt, in->pt);
    float v4 = mh_cos(a * RS_PIF * 2.0f) * 2.0f + 0.5f;

    if (v4 < 0.0f) v4 = 0.0f;
    if (v4 > 1.0f) v4 = 1.0f;

    /* BiomeGenEnd.getSkyColorByTemp returns 0: the End's sky colour is black
     * before the var4 scale (which stays 1.0: the End's angle is 0). */
    int t = rs_dim == 1 ? 0 : in->skytemp;
    float v11 = (float)(t >> 16 & 255) / 255.0f;
    float v12 = (float)(t >> 8 & 255) / 255.0f;
    float v13 = (float)(t & 255) / 255.0f;
    v11 = v11 * v4;
    v12 = v12 * v4;
    v13 = v13 * v4;
    float v14 = rs_getrain(in, in->pt);
    float v15, v16;

    if (v14 > 0.0f)
    {
        v15 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.6f;
        v16 = 1.0f - v14 * 0.75f;
        v11 = v11 * v16 + v15 * (1.0f - v16);
        v12 = v12 * v16 + v15 * (1.0f - v16);
        v13 = v13 * v16 + v15 * (1.0f - v16);
    }

    v15 = rs_getthunder(in, in->pt);

    if (v15 > 0.0f)
    {
        v16 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.2f;
        float v17 = 1.0f - v15 * 0.75f;
        v11 = v11 * v17 + v16 * (1.0f - v17);
        v12 = v12 * v17 + v16 * (1.0f - v17);
        v13 = v13 * v17 + v16 * (1.0f - v17);
    }

    if (in->lbolt > 0)
    {
        float v16 = (float)in->lbolt - in->pt;

        if (v16 > 1.0f) v16 = 1.0f;

        v16 = v16 * 0.45f;
        v11 = v11 * (1.0f - v16) + 0.8f * v16;
        v12 = v12 * (1.0f - v16) + 0.8f * v16;
        v13 = v13 * (1.0f - v16) + 1.0f * v16;
    }

    v[0] = (double)v11;
    v[1] = (double)v12;
    v[2] = (double)v13;
}

/* World.getCloudColour */
static void rs_cloudcolour(const struct rs_in *in, double *v)
{
    float a = rs_celestial(in->wt, in->pt);
    float v3 = mh_cos(a * RS_PIF * 2.0f) * 2.0f + 0.5f;

    if (v3 < 0.0f) v3 = 0.0f;
    if (v3 > 1.0f) v3 = 1.0f;

    long long c = in->cloud;
    float v4 = (float)(c >> 16 & 255L) / 255.0f;
    float v5 = (float)(c >> 8 & 255L) / 255.0f;
    float v6 = (float)(c & 255L) / 255.0f;
    float v7 = rs_getrain(in, in->pt);
    float v8, v9;

    if (v7 > 0.0f)
    {
        v8 = (v4 * 0.3f + v5 * 0.59f + v6 * 0.11f) * 0.6f;
        v9 = 1.0f - v7 * 0.95f;
        v4 = v4 * v9 + v8 * (1.0f - v9);
        v5 = v5 * v9 + v8 * (1.0f - v9);
        v6 = v6 * v9 + v8 * (1.0f - v9);
    }

    v4 = v4 * (v3 * 0.9f + 0.1f);
    v5 = v5 * (v3 * 0.9f + 0.1f);
    v6 = v6 * (v3 * 0.85f + 0.15f);
    v8 = rs_getthunder(in, in->pt);

    if (v8 > 0.0f)
    {
        v9 = (v4 * 0.3f + v5 * 0.59f + v6 * 0.11f) * 0.2f;
        float v10 = 1.0f - v8 * 0.95f;
        v4 = v4 * v10 + v9 * (1.0f - v10);
        v5 = v5 * v10 + v9 * (1.0f - v10);
        v6 = v6 * v10 + v9 * (1.0f - v10);
    }

    v[0] = (double)v4;
    v[1] = (double)v5;
    v[2] = (double)v6;
}

/* EntityLivingBase.getLook(pt); pt == 1.0F takes the rotation as it stands. */
static void rs_look(const struct rs_in *in, double *v)
{
    if (in->pt == 1.0f)
    {
        float v2 = mh_cos(-in->yaw * RS_DEG2RAD - RS_PIF);
        float v3 = mh_sin(-in->yaw * RS_DEG2RAD - RS_PIF);
        float v4 = -mh_cos(-in->pit * RS_DEG2RAD);
        float v5 = mh_sin(-in->pit * RS_DEG2RAD);
        v[0] = (double)(v3 * v4);
        v[1] = (double)v5;
        v[2] = (double)(v2 * v4);
    }
    else
    {
        float v2 = in->ppit + (in->pit - in->ppit) * in->pt;
        float v3 = in->pyaw + (in->yaw - in->pyaw) * in->pt;
        float v4 = mh_cos(-v3 * RS_DEG2RAD - RS_PIF);
        float v5 = mh_sin(-v3 * RS_DEG2RAD - RS_PIF);
        float v6 = -mh_cos(-v2 * RS_DEG2RAD);
        float v7 = mh_sin(-v2 * RS_DEG2RAD);
        v[0] = (double)(v5 * v6);
        v[1] = (double)v7;
        v[2] = (double)(v4 * v6);
    }
}

/* ------------------------------------------------------------------ GL */

typedef float rs_mat[16]; /* column-major, GL */

static void gl_identity(float *m)
{
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 0.0f;
    m[4] = 0.0f; m[5] = 1.0f; m[6] = 0.0f; m[7] = 0.0f;
    m[8] = 0.0f; m[9] = 0.0f; m[10] = 1.0f; m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

/* Mesa's _math_matrix_mul_floats: dest = dest * b */
static void gl_mult(float *m, const float *b)
{
    float a[16];

    memcpy(a, m, sizeof a);
    for (int i = 0; i < 4; ++i)
    {
        float ai0 = a[i], ai1 = a[4 + i], ai2 = a[8 + i], ai3 = a[12 + i];
        for (int j = 0; j < 4; ++j)
            m[j * 4 + i] = ai0 * b[j * 4] + ai1 * b[j * 4 + 1] + ai2 * b[j * 4 + 2] + ai3 * b[j * 4 + 3];
    }
}

/* Mesa's _math_matrix_rotate plus glRotatef; Mesa computes the radians as a
 * double from the float angle and calls sinf/cosf on that float. */
static void gl_rotate(float *m, float angle, float x, float y, float z)
{
    float mrot[16];
    float rad = angle * RS_PI / 180.0;
    float s = sinf(rad);
    float c = cosf(rad);
    float len = sqrtf(x * x + y * y + z * z);
    x /= len;
    y /= len;
    z /= len;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, yz = y * z, zx = z * x;
    float xs = x * s, ys = y * s, zs = z * s;
    float one_c = 1.0f - c;

    gl_identity(mrot);
    mrot[0] = one_c * xx + c;
    mrot[1] = one_c * xy + zs;
    mrot[2] = one_c * zx - ys;
    mrot[4] = one_c * xy - zs;
    mrot[5] = one_c * yy + c;
    mrot[6] = one_c * yz + xs;
    mrot[8] = one_c * zx + ys;
    mrot[9] = one_c * yz - xs;
    mrot[10] = one_c * zz + c;
    /* Mesa's _math_matrix_rotate takes its axis-aligned path whenever two
     * components are zero, for either sign of the third, and leaves the axis
     * entry at the identity's exact one. Forming (1-c)+c at 90 degrees can
     * round one ulp below one. */
    if (y == 0.0f && z == 0.0f && x != 0.0f) mrot[0] = 1.0f;
    if (x == 0.0f && z == 0.0f && y != 0.0f) mrot[5] = 1.0f;
    if (x == 0.0f && y == 0.0f && z != 0.0f) mrot[10] = 1.0f;
    gl_mult(m, mrot);
}

/* Mesa's _math_matrix_translate */
static void gl_translate(float *m, float x, float y, float z)
{
    m[12] = m[0] * x + m[4] * y + m[8] * z + m[12];
    m[13] = m[1] * x + m[5] * y + m[9] * z + m[13];
    m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
    m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

/* glScalef, fixed function */
static void gl_scale(float *m, float x, float y, float z)
{
    m[0] *= x; m[1] *= x; m[2] *= x; m[3] *= x;
    m[4] *= y; m[5] *= y; m[6] *= y; m[7] *= y;
    m[8] *= z; m[9] *= z; m[10] *= z; m[11] *= z;
}

/* LWJGL's Project.gluPerspective, byte for byte, into the current matrix. */
static void glu_perspective(float *m, float fovy, float aspect, float znear, float zfar)
{
    float radians = fovy / 2.0f * RS_PIF / 180.0f;
    float deltaZ = zfar - znear;
    float sine = (float)fd_sin((double)radians);

    if (deltaZ == 0.0f || sine == 0.0f || aspect == 0.0f) return;

    float cotangent = (float)fd_cos((double)radians) / sine;
    float r[16];

    gl_identity(r);
    r[0] = cotangent / aspect;
    r[5] = cotangent;
    r[10] = -(zfar + znear) / deltaZ;
    r[11] = -1.0f;
    r[14] = -2.0f * znear * zfar / deltaZ;
    r[15] = 0.0f;
    gl_mult(m, r);
}

/* EntityRenderer.getFOVModifier(pt, true) */
static float rs_fov(const struct rs_in *in)
{
    if (in->dvd > 0) return 90.0f;

    float v4 = 70.0f;

    v4 = in->fov;
    v4 = v4 * (in->fmhp + (in->fmh - in->fmhp) * in->pt);

    if (in->hp <= 0.0f)
    {
        float v5 = (float)in->death + in->pt;
        v4 = v4 / ((1.0f - 500.0f / (v5 + 500.0f)) * 2.0f + 1.0f);
    }

    if (in->mat == 1) v4 = v4 * 60.0f / 70.0f;

    return v4;
}

/* EntityRenderer.hurtCameraEffect on the current modelview */
static void rs_hurt(const struct rs_in *in, float *mv)
{
    float v3 = (float)in->hurt - in->pt;

    if (in->hp <= 0.0f)
    {
        float v4 = (float)in->death + in->pt;
        gl_rotate(mv, 40.0f - 8000.0f / (v4 + 200.0f), 0.0f, 0.0f, 1.0f);
    }

    if (v3 >= 0.0f)
    {
        v3 = v3 / (float)in->mhurt;
        v3 = mh_sin(v3 * v3 * v3 * v3 * RS_PIF);
        float v4 = in->aaty;
        gl_rotate(mv, -v4, 0.0f, 1.0f, 0.0f);
        gl_rotate(mv, -v3 * 14.0f, 0.0f, 0.0f, 1.0f);
        gl_rotate(mv, v4, 0.0f, 1.0f, 0.0f);
    }
}

/* EntityRenderer.setupViewBobbing on the current modelview */
static void rs_bob(const struct rs_in *in, float *mv)
{
    float v3 = in->dwm - in->pdwm;
    float v4 = -(in->dwm + v3 * in->pt);
    float v5 = in->pecyaw + (in->ecyaw - in->pecyaw) * in->pt;
    float v6 = in->pepit + (in->ecpit - in->pepit) * in->pt;
    gl_translate(mv, mh_sin(v4 * RS_PIF) * v5 * 0.5f, -fabsf(mh_cos(v4 * RS_PIF) * v5), 0.0f);
    gl_rotate(mv, mh_sin(v4 * RS_PIF) * v5 * 3.0f, 0.0f, 0.0f, 1.0f);
    gl_rotate(mv, fabsf(mh_cos(v4 * RS_PIF - 0.2f) * v5) * 5.0f, 1.0f, 0.0f, 0.0f);
    gl_rotate(mv, v6, 1.0f, 0.0f, 0.0f);
}

/* ---------------------------------------------------------------- main */

void renderstate_compute(const struct rs_in *in, struct rs_out *o)
{
    float pt = in->pt;

    memset(o, 0, sizeof *o);
    rs_dim = in->dim;

    /* World getters */
    o->ang = rs_celestial(in->wt, pt);
    o->angr = o->ang * RS_PIF * 2.0f;
    o->sun = rs_sunbrightness(in, pt);
    o->star = rs_star(in->wt, pt);
    o->rain = rs_getrain(in, pt);
    rs_skycolor(in, o->sky);
    rs_providerfog(in->wt, pt, o->fog);
    rs_cloudcolour(in, o->cloud);
    o->rise_ok = rs_sunrise(in->wt, pt, o->rise);

    /* EntityRenderer.updateLightmap(pt); getSunBrightness(1.0F) */
    float sun1 = rs_sunbrightness(in, 1.0f);

    for (int i = 0; i < 256; ++i)
    {
        float v4 = sun1 * 0.95f + 0.05f;
        float v5 = in->lbt[i / 16] * v4;
        float v6 = in->lbt[i % 16] * (in->tfx * 0.1f + 1.5f);

        if (in->lbolt > 0) v5 = in->lbt[i / 16];

        float v7 = v5 * (sun1 * 0.65f + 0.35f);
        float v8 = v5 * (sun1 * 0.65f + 0.35f);
        float v11 = v6 * ((v6 * 0.6f + 0.4f) * 0.6f + 0.4f);
        float v12 = v6 * (v6 * v6 * 0.6f + 0.4f);
        float v13 = v7 + v6;
        float v14 = v8 + v11;
        float v15 = v5 + v12;
        v13 = v13 * 0.96f + 0.03f;
        v14 = v14 * 0.96f + 0.03f;
        v15 = v15 * 0.96f + 0.03f;

        if (in->dim == 1)
        {
            v13 = 0.22f + v6 * 0.75f;
            v14 = 0.28f + v11 * 0.75f;
            v15 = 0.25f + v12 * 0.75f;
        }

        if (in->nv)
        {
            float v16 = in->nvd > 200 ? 1.0f : 0.7f + mh_sin(((float)in->nvd - pt) * RS_PIF * 0.2f) * 0.3f;
            float v17 = 1.0f / v13;

            if (v17 > 1.0f / v14) v17 = 1.0f / v14;
            if (v17 > 1.0f / v15) v17 = 1.0f / v15;

            v13 = v13 * (1.0f - v16) + v13 * v17 * v16;
            v14 = v14 * (1.0f - v16) + v14 * v17 * v16;
            v15 = v15 * (1.0f - v16) + v15 * v17 * v16;
        }

        if (v13 > 1.0f) v13 = 1.0f;
        if (v14 > 1.0f) v14 = 1.0f;
        if (v15 > 1.0f) v15 = 1.0f;

        float v16 = in->gamma;
        float v17 = 1.0f - v13;
        float v18 = 1.0f - v14;
        float v19 = 1.0f - v15;
        v17 = 1.0f - v17 * v17 * v17 * v17;
        v18 = 1.0f - v18 * v18 * v18 * v18;
        v19 = 1.0f - v19 * v19 * v19 * v19;
        v13 = v13 * (1.0f - v16) + v17 * v16;
        v14 = v14 * (1.0f - v16) + v18 * v16;
        v15 = v15 * (1.0f - v16) + v19 * v16;
        v13 = v13 * 0.96f + 0.03f;
        v14 = v14 * 0.96f + 0.03f;
        v15 = v15 * 0.96f + 0.03f;

        if (v13 > 1.0f) v13 = 1.0f;
        if (v14 > 1.0f) v14 = 1.0f;
        if (v15 > 1.0f) v15 = 1.0f;
        if (v13 < 0.0f) v13 = 0.0f;
        if (v14 < 0.0f) v14 = 0.0f;
        if (v15 < 0.0f) v15 = 0.0f;

        int v21 = (int)(v13 * 255.0f);
        int v22 = (int)(v14 * 255.0f);
        int v23 = (int)(v15 * 255.0f);
        o->lm[i] = 255 << 24 | v21 << 16 | v22 << 8 | v23;
    }

    /* EntityRenderer.updateFogColor */
    {
        float v4 = 0.25f + 0.75f * (float)in->rd / 16.0f;
        v4 = 1.0f - (float)fd_pow((double)v4, 0.25);
        double sky[3];

        rs_skycolor(in, sky);
        float v6 = (float)sky[0];
        float v7 = (float)sky[1];
        float v8 = (float)sky[2];
        double fog[3];

        rs_providerfog(in->wt, pt, fog);
        o->fcr = (float)fog[0];
        o->fcg = (float)fog[1];
        o->fcb = (float)fog[2];
        float v11;

        if (in->rd >= 4)
        {
            double look[3];

            rs_look(in, look);
            float x = mh_sin(o->angr) > 0.0f ? -1.0f : 1.0f;
            v11 = (float)(look[0] * (double)x);

            if (v11 < 0.0f) v11 = 0.0f;

            if (v11 > 0.0f)
            {
                float rise[4];

                if (rs_sunrise(in->wt, pt, rise))
                {
                    v11 = v11 * rise[3];
                    o->fcr = o->fcr * (1.0f - v11) + rise[0] * v11;
                    o->fcg = o->fcg * (1.0f - v11) + rise[1] * v11;
                    o->fcb = o->fcb * (1.0f - v11) + rise[2] * v11;
                }
            }
        }

        o->fcr = o->fcr + (v6 - o->fcr) * v4;
        o->fcg = o->fcg + (v7 - o->fcg) * v4;
        o->fcb = o->fcb + (v8 - o->fcb) * v4;
        float v19 = rs_getrain(in, pt);

        if (v19 > 0.0f)
        {
            v11 = 1.0f - v19 * 0.5f;
            float v20 = 1.0f - v19 * 0.4f;
            o->fcr = o->fcr * v11;
            o->fcg = o->fcg * v11;
            o->fcb = o->fcb * v20;
        }

        v11 = rs_getthunder(in, pt);

        if (v11 > 0.0f)
        {
            float v20 = 1.0f - v11 * 0.5f;
            o->fcr = o->fcr * v20;
            o->fcg = o->fcg * v20;
            o->fcb = o->fcb * v20;
        }

        if (in->mat == 1)
        {
            float v22 = (float)in->resp * 0.2f;
            o->fcr = 0.02f + v22;
            o->fcg = 0.02f + v22;
            o->fcb = 0.2f + v22;
        }
        else if (in->mat == 2)
        {
            o->fcr = 0.6f;
            o->fcg = 0.1f;
            o->fcb = 0.0f;
        }

        float v22 = in->fc2 + (in->fc1 - in->fc2) * pt;
        o->fcr = o->fcr * v22;
        o->fcg = o->fcg * v22;
        o->fcb = o->fcb * v22;
        double v14 = (in->ppy + (in->py - in->ppy) * (double)pt) * in->voidf;

        if (in->blind)
        {
            if (in->bldur < 20) v14 = v14 * (double)(1.0f - (float)in->bldur / 20.0f);
            else v14 = 0.0;
        }

        if (v14 < 1.0)
        {
            if (v14 < 0.0) v14 = 0.0;

            v14 = v14 * v14;
            o->fcr = (float)((double)o->fcr * v14);
            o->fcg = (float)((double)o->fcg * v14);
            o->fcb = (float)((double)o->fcb * v14);
        }

        if (in->nv)
        {
            float v23 = in->nvd > 200 ? 1.0f : 0.7f + mh_sin(((float)in->nvd - pt) * RS_PIF * 0.2f) * 0.3f;
            float v17 = 1.0f / o->fcr;

            if (v17 > 1.0f / o->fcg) v17 = 1.0f / o->fcg;
            if (v17 > 1.0f / o->fcb) v17 = 1.0f / o->fcb;

            o->fcr = o->fcr * (1.0f - v23) + o->fcr * v17 * v23;
            o->fcg = o->fcg * (1.0f - v23) + o->fcg * v17 * v23;
            o->fcb = o->fcb * (1.0f - v23) + o->fcb * v17 * v23;
        }
    }

    o->far = (float)(in->rd * 16);

    /* setupCameraTransform: projection, then the modelview chain */
    {
        float proj[16], mv[16];

        gl_identity(proj);
        glu_perspective(proj, rs_fov(in), (float)in->dw / (float)in->dh, 0.05f, o->far * 2.0f);

        gl_identity(mv);
        rs_hurt(in, mv);

        if (in->bob) rs_bob(in, mv);

        float v4 = in->pportal + (in->portal - in->pportal) * pt;

        if (v4 > 0.0f)
        {
            /* the confusion potion turns the warp faster */
            int v5 = in->conf ? 7 : 20;
            float v6 = 5.0f / (v4 * v4 + 5.0f) - v4 * 0.04f;
            v6 = v6 * v6;
            gl_rotate(mv, ((float)in->ruc + pt) * (float)v5, 0.0f, 1.0f, 1.0f);
            gl_scale(mv, 1.0f / v6, 1.0f, 1.0f);
            gl_rotate(mv, -((float)in->ruc + pt) * (float)v5, 0.0f, 1.0f, 1.0f);
        }

        /* orientCamera */
        float v3 = in->yoff - 1.62f;
        gl_rotate(mv, in->proll + (in->roll - in->proll) * pt, 0.0f, 0.0f, 1.0f);
        float pitch = in->ppit + (in->pit - in->ppit) * pt;
        float yaw = in->pyaw + (in->yaw - in->pyaw) * pt;
        if (in->sleep)
        {
            /* in bed: a block higher, 0.3 up, turned to the bed, then the
             * look reversed */
            v3 = (float)((double)v3 + 1.0);
            gl_translate(mv, 0.0f, 0.3f, 0.0f);
            if (!in->dbgcam)
            {
                if (in->bedrot >= 0) gl_rotate(mv, (float)(in->bedrot * 90), 0.0f, 1.0f, 0.0f);
                gl_rotate(mv, yaw + 180.0f, 0.0f, -1.0f, 0.0f);
                gl_rotate(mv, pitch, -1.0f, 0.0f, 0.0f);
            }
        }
        else if (in->tpv > 0)
        {
            /* third person: back along the look (the front view turned
             * round), by the distance the rays left */
            double dist = in->tpdist > 0.0 ? in->tpdist : 4.0;
            float ty = in->yaw, tp = in->pit;
            if (in->tpv == 2) tp += 180.0f;
            if (in->tpv == 2) gl_rotate(mv, 180.0f, 0.0f, 1.0f, 0.0f);
            gl_rotate(mv, in->pit - tp, 1.0f, 0.0f, 0.0f);
            gl_rotate(mv, in->yaw - ty, 0.0f, 1.0f, 0.0f);
            gl_translate(mv, 0.0f, 0.0f, (float)(-dist));
            gl_rotate(mv, ty - in->yaw, 0.0f, 1.0f, 0.0f);
            gl_rotate(mv, tp - in->pit, 1.0f, 0.0f, 0.0f);
        }
        else
            gl_translate(mv, 0.0f, 0.0f, -0.1f);
        if (!in->dbgcam)
        {
            gl_rotate(mv, pitch, 1.0f, 0.0f, 0.0f);
            gl_rotate(mv, yaw + 180.0f, 0.0f, 1.0f, 0.0f);
        }
        gl_translate(mv, 0.0f, v3, 0.0f);

        memcpy(o->proj, proj, sizeof proj);
        memcpy(o->mv, mv, sizeof mv);
        o->camx = in->ppx + (in->px - in->ppx) * (double)pt;
        /* the render origin is the entity's interpolated position
         * (RenderGlobal's lastTickPos + (pos - lastTickPos) * pt): the
         * modelview carries yOffset - 1.62, which is 0 standing (the probe's
         * first-person cam) and the bed's -0.42 asleep */
        o->camy = in->ppy + (in->py - in->ppy) * (double)pt;
        o->camz = in->ppz + (in->pz - in->ppz) * (double)pt;
        /* RenderGlobal.hasCloudFog is false in 1.7.10 */
        o->cloudfog = 0;
    }

    /* EntityRenderer.setupFog(0, pt) */
    {
        o->gfogc[0] = o->fcr;
        o->gfogc[1] = o->fcg;
        o->gfogc[2] = o->fcb;
        o->gfogc[3] = 1.0f;

        if (in->blind)
        {
            float v6 = 5.0f;
            if (in->bldur < 20) v6 = 5.0f + (o->far - 5.0f) * (1.0f - (float)in->bldur / 20.0f);
            o->gfogm = 0x2601;
            o->gfogs = 0.0f;
            o->gfoge = v6 * 0.8f;
        }
        else if (in->mat == 1)
        {
            o->gfogm = 0x0800;
            o->gfogd = in->wb ? 0.05f : 0.1f - (float)in->resp * 0.03f;
        }
        else if (in->mat == 2)
        {
            o->gfogm = 0x0800;
            o->gfogd = 2.0f;
        }
        else
        {
            float v6 = o->far;

            if (in->voidp && !in->crea)
            {
                double v10 = (double)((in->brf & 15728640) >> 20) / 16.0 + (in->ppy + (in->py - in->ppy) * (double)pt + 4.0) / 32.0;

                if (v10 < 1.0)
                {
                    if (v10 < 0.0) v10 = 0.0;

                    v10 = v10 * v10;
                    float v9 = 100.0f * (float)v10;

                    if (v9 < 5.0f) v9 = 5.0f;
                    if (v6 > v9) v6 = v9;
                }
            }

            o->gfogm = 0x2601;
            o->gfogs = v6 * 0.75f;
            o->gfoge = v6;

            if (in->xzfog)
            {
                o->gfogs = v6 * 0.05f;
                float e = v6 < 192.0f ? v6 : 192.0f;
                o->gfoge = e * 0.5f;
            }
        }
    }
}

/* ------------------------------------------------------------ the hand */

void rs_gl_identity(float *m) { gl_identity(m); }
void rs_gl_mult(float *m, const float *b) { gl_mult(m, b); }
void rs_gl_rotate(float *m, float angle, float x, float y, float z) { gl_rotate(m, angle, x, y, z); }
void rs_gl_translate(float *m, float x, float y, float z) { gl_translate(m, x, y, z); }
void rs_gl_scale(float *m, float x, float y, float z) { gl_scale(m, x, y, z); }

/* EntityRenderer.renderHand up to renderItemInFirstPerson: its projection
 * uses getFOVModifier(pt, false), 70 degrees before the death and water
 * factors, and the modelview is hurtCameraEffect, then setupViewBobbing when
 * view bobbing is on. The caller skips the hand when debugViewDirection > 0
 * or the camera zoom is not 1 (renderWorld's gate). */
void renderstate_hand_camera(const struct rs_in *in, float proj[16], float mv[16])
{
    float fov = 70.0f;

    if (in->hp <= 0.0f)
    {
        float v5 = (float)in->death + in->pt;
        fov = fov / ((1.0f - 500.0f / (v5 + 500.0f)) * 2.0f + 1.0f);
    }

    if (in->mat == 1) fov = fov * 60.0f / 70.0f;

    float far = (float)(in->rd * 16);
    gl_identity(proj);
    glu_perspective(proj, fov, (float)in->dw / (float)in->dh, 0.05f, far * 2.0f);
    gl_identity(mv);
    rs_hurt(in, mv);
    if (in->bob) rs_bob(in, mv);
}

void renderstate_torch_flicker(struct rs_flicker *f, det_state *det)
{
    double v[8];
    for (int i = 0; i < 8; ++i) v[i] = det_math_random_role(det, DET_CLIENT);
    renderstate_torch_flicker_step(f, v);
}

void renderstate_torch_flicker_step(struct rs_flicker *f, const double v[8])
{
    f->dx = (float)((double)f->dx + (v[0] - v[1]) * v[2] * v[3]);
    f->dy = (float)((double)f->dy + (v[4] - v[5]) * v[6] * v[7]);
    f->dx = (float)((double)f->dx * 0.9);
    f->dy = (float)((double)f->dy * 0.9);
    f->x += (f->dx - f->x) * 1.0f;
    f->y += (f->dy - f->y) * 1.0f;
}

float renderstate_celestial(int dim, int64_t wt, float pt)
{
    int keep = rs_dim;
    rs_dim = dim;
    float a = rs_celestial(wt, pt);
    rs_dim = keep;
    return a;
}

double renderstate_third_person_distance(double x, double y, double z, float yaw, float pitch, int tpv,
                                         int (*ray)(void *ctx, const double s[3], const double e[3], double hit[3]),
                                         void *ctx)
{
    double dist = 4.0;
    float v28 = yaw, v13 = pitch;
    if (tpv == 2) v13 += 180.0f;
    double v14 = (double)(-mh_sin(v28 / 180.0f * RS_PIF) * mh_cos(v13 / 180.0f * RS_PIF)) * dist;
    double v16 = (double)(mh_cos(v28 / 180.0f * RS_PIF) * mh_cos(v13 / 180.0f * RS_PIF)) * dist;
    double v18 = (double)(-mh_sin(v13 / 180.0f * RS_PIF)) * dist;

    for (int i = 0; i < 8; ++i)
    {
        float v21 = (float)((i & 1) * 2 - 1);
        float v22 = (float)((i >> 1 & 1) * 2 - 1);
        float v23 = (float)((i >> 2 & 1) * 2 - 1);
        v21 *= 0.1f;
        v22 *= 0.1f;
        v23 *= 0.1f;
        double s[3] = {x + (double)v21, y + (double)v22, z + (double)v23};
        double e[3] = {x - v14 + (double)v21 + (double)v23, y - v18 + (double)v22, z - v16 + (double)v23};
        double hit[3];
        if (ray(ctx, s, e, hit))
        {
            double dx = x - hit[0], dy = y - hit[1], dz = z - hit[2];
            /* Vec3.distanceTo: MathHelper.sqrt_double, a float */
            double d = (double)(float)sqrt(dx * dx + dy * dy + dz * dz);
            if (d < dist) dist = d;
        }
    }
    return dist;
}
