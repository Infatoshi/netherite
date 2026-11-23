/* fdlibm (Sun Microsystems, freely redistributable with this notice): s_sin,
 * k_sin, k_cos, e_rem_pio2, k_rem_pio2 and e_pow, ported unchanged. The
 * literals are the fdlibm ones; __HI/__LO are the raw double words. */
#include "smath.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint64_t bits(double d)
{
    uint64_t b;

    memcpy(&b, &d, sizeof b);
    return b;
}

static double from_bits(uint64_t b)
{
    double d;

    memcpy(&d, &b, sizeof d);
    return d;
}

static int32_t hi(double d)
{
    return (int32_t)(bits(d) >> 32);
}

static uint32_t lo(double d)
{
    return (uint32_t)bits(d);
}

static double set_hi(double d, uint32_t w)
{
    return from_bits((bits(d) & 0xffffffffULL) | ((uint64_t)w << 32));
}

static double set_lo(double d, uint32_t w)
{
    return from_bits((bits(d) & 0xffffffff00000000ULL) | w);
}

/* -------------------------------------------------------------- kernel */

static double kernel_sin(double x, double y, int iy)
{
    static const double half = 5.00000000000000000000e-01;
    static const double S1 = -1.66666666666666324348e-01;
    static const double S2 = 8.33333333332248946124e-03;
    static const double S3 = -1.98412698298579493134e-04;
    static const double S4 = 2.75573137070700676789e-06;
    static const double S5 = -2.50507602534068634195e-08;
    static const double S6 = 1.58969099521155010221e-10;
    double z, r, v;

    if ((int32_t)((bits(x) >> 32) & 0x7fffffff) < 0x3e400000)
        if ((int)x == 0) return x;

    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (half * y - v * r) - y) - v * S1);
}

static double kernel_cos(double x, double y)
{
    static const double one = 1.0;
    static const double C1 = 4.16666666666666019037e-02;
    static const double C2 = -1.38888888888741095749e-03;
    static const double C3 = 2.48015872894767294178e-05;
    static const double C4 = -2.75573143513906633035e-07;
    static const double C5 = 2.08757232129817482790e-09;
    static const double C6 = -1.13596475577881948265e-11;
    double a, hz, z, r, qx;

    if ((int32_t)((bits(x) >> 32) & 0x7fffffff) < 0x3e400000)
        if ((int)x == 0) return one;

    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));

    if ((int32_t)((bits(x) >> 32) & 0x7fffffff) < 0x3FD33333) /* |x| < 0.3 */
        return one - (0.5 * z - (z * r - x * y));

    if ((int32_t)((bits(x) >> 32) & 0x7fffffff) > 0x3fe90000) /* |x| > 0.78125 */
        qx = 0.28125;
    else
        qx = set_hi(0.0, (uint32_t)(((bits(x) >> 32) & 0x7fffffff) - 0x00200000));

    hz = 0.5 * z - qx;
    a = one - qx;
    return a - (hz - (z * r - x * y));
}

/* -------------------------------------------------------- argument reduction */

static const int two_over_pi[] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
    0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
    0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
    0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
    0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
    0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
    0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
    0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
    0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
    0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
    0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
};

static const int npio2_hw[] = {
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
    0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
    0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
    0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
    0x404858EB, 0x404921FB,
};

static const int init_jk[] = {2, 3, 4, 6};

static const double PIo2[] = {
    1.57079625129699707031e+00,
    7.54978941586159635335e-08,
    5.39030252995776476554e-15,
    3.28200341580791294123e-22,
    1.27065575308067607349e-29,
    1.22933308981111328932e-36,
    2.73370053816464559624e-44,
    2.16741683877804819444e-51,
};

static int kernel_rem_pio2(double *x, double *y, int e0, int nx, int prec, const int *ipio2)
{
    static const double two24 = 1.67772160000000000000e+07;
    static const double twon24 = 5.96046447753906250000e-08;
    int jz, jx, jv, jp, jk, carry, n, iq[20], i, j, k, q0, ih;
    double z, fw, f[20], fq[20] = {0}, q[20];

    jk = init_jk[prec];
    jp = jk;
    jx = nx - 1;
    jv = (e0 - 3) / 24;
    if (jv < 0) jv = 0;
    q0 = e0 - 24 * (jv + 1);

    j = jv - jx;
    int m = jx + jk;
    for (i = 0; i <= m; i++, j++) f[i] = (j < 0) ? 0.0 : (double)ipio2[j];

    for (i = 0; i <= jk; i++)
    {
        double fw2 = 0.0;
        for (j = 0; j <= jx; j++) fw2 += x[j] * f[jx + i - j];
        q[i] = fw2;
    }

    jz = jk;
recompute:
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--)
    {
        double fw3 = (double)(int)(twon24 * z);
        iq[i] = (int)(z - two24 * fw3);
        z = q[j - 1] + fw3;
    }

    z = scalbn(z, q0);
    z -= 8.0 * floor(z * 0.125);
    n = (int)z;
    z -= (double)n;
    ih = 0;
    if (q0 > 0)
    {
        i = iq[jz - 1] >> (24 - q0);
        n += i;
        iq[jz - 1] -= i << (24 - q0);
        ih = iq[jz - 1] >> (23 - q0);
    }
    else if (q0 == 0)
        ih = iq[jz - 1] >> 23;
    else if (z >= 0.5)
        ih = 2;

    if (ih > 0)
    {
        n += 1;
        carry = 0;
        for (i = 0; i < jz; i++)
        {
            j = iq[i];
            if (carry == 0)
            {
                if (j != 0)
                {
                    carry = 1;
                    iq[i] = 0x1000000 - j;
                }
            }
            else
                iq[i] = 0xffffff - j;
        }
        if (q0 > 0)
        {
            switch (q0)
            {
            case 1:
                iq[jz - 1] &= 0x7fffff;
                break;
            case 2:
                iq[jz - 1] &= 0x3fffff;
                break;
            }
        }
        if (ih == 2)
        {
            z = 1.0 - z;
            if (carry != 0) z -= scalbn(1.0, q0);
        }
    }

    if (z == 0.0)
    {
        j = 0;
        for (i = jz - 1; i >= jk; i--) j |= iq[i];
        if (j == 0)
        {
            for (k = 1; iq[jk - k] == 0; k++)
                ;

            for (i = jz + 1; i <= jz + k; i++)
            {
                double fw4 = 0.0;
                f[jx + i] = (double)ipio2[jv + i];
                for (j = 0; j <= jx; j++) fw4 += x[j] * f[jx + i - j];
                q[i] = fw4;
            }
            jz += k;
            goto recompute;
        }
    }

    if (z == 0.0)
    {
        jz -= 1;
        q0 -= 24;
        while (iq[jz] == 0)
        {
            jz--;
            q0 -= 24;
        }
    }
    else
    {
        z = scalbn(z, -q0);
        if (z >= two24)
        {
            double fw5 = (double)(int)(twon24 * z);
            iq[jz] = (int)(z - two24 * fw5);
            jz += 1;
            q0 += 24;
            iq[jz] = (int)fw5;
        }
        else
            iq[jz] = (int)z;
    }

    fw = scalbn(1.0, q0);
    for (i = jz; i >= 0; i--)
    {
        q[i] = fw * (double)iq[i];
        fw *= twon24;
    }

    for (i = jz; i >= 0; i--)
    {
        double w = 0.0;
        for (k = 0; k <= jp && k <= jz - i; k++) w += PIo2[k] * q[i + k];
        fq[jz - i] = w;
    }

    /* prec 1 (double) and 2 (extended) share the two-word result */
    fw = 0.0;
    for (i = jz; i >= 0; i--) fw += fq[i];
    y[0] = (ih == 0) ? fw : -fw;
    fw = fq[0] - fw;
    for (i = 1; i <= jz; i++) fw += fq[i];
    y[1] = (ih == 0) ? fw : -fw;

    return n & 7;
}

static int rem_pio2(double x, double *y)
{
    static const double two24 = 1.67772160000000000000e+07;
    static const double invpio2 = 6.36619772367581382433e-01;
    static const double pio2_1 = 1.57079632673412561417e+00;
    static const double pio2_1t = 6.07710050650619224932e-11;
    static const double pio2_2 = 6.07710050630396597660e-11;
    static const double pio2_2t = 2.02226624879595063154e-21;
    static const double pio2_3 = 2.02226624871116645580e-21;
    static const double pio2_3t = 8.47842766036889956997e-32;
    double z, w, t, r, fn;
    double tx[3];
    int i, j, nx, n, ix, hx;

    hx = (int32_t)(bits(x) >> 32);
    ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb)
    {
        y[0] = x;
        y[1] = 0;
        return 0;
    }
    if (ix < 0x4002d97c)
    {
        if (hx > 0)
        {
            z = x - pio2_1;
            if (ix != 0x3ff921fb)
            {
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            }
            else
            {
                z -= pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        }
        z = x + pio2_1;
        if (ix != 0x3ff921fb)
        {
            y[0] = z + pio2_1t;
            y[1] = (z - y[0]) + pio2_1t;
        }
        else
        {
            z += pio2_2;
            y[0] = z + pio2_2t;
            y[1] = (z - y[0]) + pio2_2t;
        }
        return -1;
    }
    if (ix <= 0x413921fb)
    {
        t = fabs(x);
        n = (int)(t * invpio2 + 0.5);
        fn = (double)n;
        r = t - fn * pio2_1;
        w = fn * pio2_1t;
        if (n < 32 && ix != npio2_hw[n - 1])
        {
            y[0] = r - w;
        }
        else
        {
            j = ix >> 20;
            y[0] = r - w;
            i = j - ((hi(y[0]) >> 20) & 0x7ff);
            if (i > 16)
            {
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - ((hi(y[0]) >> 20) & 0x7ff);
                if (i > 49)
                {
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0)
        {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        }
        return n;
    }

    if (ix >= 0x7ff00000)
    {
        y[0] = y[1] = x - x;
        return 0;
    }

    /* z = scalbn(|x|, ilogb(x) - 23) */
    z = set_hi(fabs(x), (uint32_t)ix - (((ix >> 20) - 1046) << 20));
    for (i = 0; i < 2; i++)
    {
        tx[i] = (double)(int)z;
        z = (z - tx[i]) * two24;
    }
    tx[2] = z;
    nx = 3;
    while (tx[nx - 1] == 0.0) nx--;
    n = kernel_rem_pio2(tx, y, (ix >> 20) - 1046, nx, 1, two_over_pi);
    if (hx < 0)
    {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

double fd_sin(double x)
{
    double y[2];
    int n, ix;

    ix = (int32_t)(bits(x) >> 32) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return kernel_sin(x, 0.0, 0);
    if (ix >= 0x7ff00000) return x - x;

    n = rem_pio2(x, y);
    switch (n & 3)
    {
    case 0:
        return kernel_sin(y[0], y[1], 1);
    case 1:
        return kernel_cos(y[0], y[1]);
    case 2:
        return -kernel_sin(y[0], y[1], 1);
    default:
        return -kernel_cos(y[0], y[1]);
    }
}

double fd_cos(double x)
{
    double y[2];
    int n, ix;

    ix = (int32_t)(bits(x) >> 32) & 0x7fffffff;
    if (ix <= 0x3fe921fb)
    {
        if (ix < 0x3e400000 && (int)x == 0) return 1.0;
        return kernel_cos(x, 0.0);
    }
    if (ix >= 0x7ff00000) return x - x;

    n = rem_pio2(x, y);
    switch (n & 3)
    {
    case 0:
        return kernel_cos(y[0], y[1]);
    case 1:
        return -kernel_sin(y[0], y[1], 1);
    case 2:
        return -kernel_cos(y[0], y[1]);
    default:
        return kernel_sin(y[0], y[1], 1);
    }
}

/* ------------------------------------------------------------------ pow */

double fd_pow(double x, double y)
{
    static const double bp[] = {1.0, 1.5};
    static const double dp_h[] = {0.0, 5.84962487220764160156e-01};
    static const double dp_l[] = {0.0, 1.35003920212974897128e-08};
    static const double two53 = 9007199254740992.0;
    static const double huge = 1.0e300, tiny = 1.0e-300;
    static const double L1 = 5.99999999999994648725e-01;
    static const double L2 = 4.28571428578550184252e-01;
    static const double L3 = 3.33333329818377432918e-01;
    static const double L4 = 2.72728123808534006489e-01;
    static const double L5 = 2.30660745775561754067e-01;
    static const double L6 = 2.06975017800338417784e-01;
    static const double P1 = 1.66666666666666019037e-01;
    static const double P2 = -2.77777777770155933842e-03;
    static const double P3 = 6.61375632143793436117e-05;
    static const double P4 = -1.65339022054652515390e-06;
    static const double P5 = 4.13813679705723846039e-08;
    static const double lg2 = 6.93147180559945286227e-01;
    static const double lg2_h = 6.93147182464599609375e-01;
    static const double lg2_l = -1.90465429995776804525e-09;
    static const double ovt = 8.0085662595372944372e-0017;
    static const double cp = 9.61796693925975554329e-01;
    static const double cp_h = 9.61796700954437255859e-01;
    static const double cp_l = -7.02846165095275826516e-09;
    static const double ivln2 = 1.44269504088896338700e+00;
    static const double ivln2_h = 1.44269502162933349609e+00;
    static const double ivln2_l = 1.92596299112661746887e-08;
    const double one = 1.0, two = 2.0, zero = 0.0;
    double z, z_h, z_l, p_h, p_l;
    double y1, t1, t2, r, s, t, u, v, w;
    int i, j, k, yisint, n;
    int hx, hy, ix, iy;
    uint32_t lx, ly;

    hx = (int32_t)(bits(x) >> 32);
    lx = lo(x);
    hy = (int32_t)(bits(y) >> 32);
    ly = lo(y);
    ix = hx & 0x7fffffff;
    iy = hy & 0x7fffffff;

    if ((iy | ly) == 0) return one;

    if (ix > 0x7ff00000 || ((ix == 0x7ff00000) && (lx != 0)) ||
        iy > 0x7ff00000 || ((iy == 0x7ff00000) && (ly != 0)))
        return x + y;

    yisint = 0;
    if (hx < 0)
    {
        if (iy >= 0x43400000)
            yisint = 2;
        else if (iy >= 0x3ff00000)
        {
            k = (iy >> 20) - 0x3ff;
            if (k > 20)
            {
                j = (int)(ly >> (52 - k));
                if ((j << (52 - k)) == (int)ly) yisint = 2 - (j & 1);
            }
            else if (ly == 0)
            {
                j = iy >> (20 - k);
                if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
            }
        }
    }

    if (ly == 0)
    {
        if (iy == 0x7ff00000)
        {
            if (((ix - 0x3ff00000) | lx) == 0)
                return y - y;
            if (ix >= 0x3ff00000)
                return (hy >= 0) ? y : zero;
            return (hy < 0) ? -y : zero;
        }
        if (iy == 0x3ff00000)
        {
            if (hy < 0) return one / x;
            return x;
        }
        if (hy == 0x40000000) return x * x;
        if (hy == 0x3fe00000)
        {
            if (hx >= 0) return sqrt(x);
        }
    }

    double ax2 = fabs(x);
    if (lx == 0)
    {
        if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000)
        {
            z = ax2;
            if (hy < 0) z = one / z;
            if (hx < 0)
            {
                if (((ix - 0x3ff00000) | yisint) == 0)
                    z = (z - z) / (z - z);
                else if (yisint == 1)
                    z = -z;
            }
            return z;
        }
    }

    n = (hx >> 31) + 1;

    if ((n | yisint) == 0) return (x - x) / (x - x);

    s = one;
    if ((n | (yisint - 1)) == 0) s = -one;

    if (iy > 0x41e00000)
    {
        if (iy > 0x43f00000)
        {
            if (ix <= 0x3fefffff) return (hy < 0) ? huge * huge : tiny * tiny;
            if (ix >= 0x3ff00000) return (hy > 0) ? huge * huge : tiny * tiny;
        }
        if (ix < 0x3fefffff) return (hy < 0) ? s * huge * huge : s * tiny * tiny;
        if (ix > 0x3ff00000) return (hy > 0) ? s * huge * huge : s * tiny * tiny;
        /* now |1-x| is tiny <= 2**-20: log(x) by x-x^2/2+x^3/3-x^4/4 */
        t = ax2 - one;
        w = (t * t) * (0.5 - t * (0.333333333333333333333 - t * 0.25));
        u = ivln2_h * t;
        v = t * ivln2_l - w * ivln2;
        t1 = u + v;
        t1 = set_lo(t1, 0);
        t2 = v - (t1 - u);
    }
    else
    {
        double ss, s2, s_h, s_l, t_h, t_l;
        n = 0;
        if (ix < 0x00100000)
        {
            ax2 *= two53;
            n -= 53;
            ix = (int32_t)(bits(ax2) >> 32);
        }
        n += (ix >> 20) - 0x3ff;
        j = ix & 0x000fffff;
        ix = j | 0x3ff00000;
        if (j <= 0x3988E)
            k = 0;
        else if (j < 0xBB67A)
            k = 1;
        else
        {
            k = 0;
            n += 1;
            ix -= 0x00100000;
        }
        ax2 = set_hi(ax2, (uint32_t)ix);

        u = ax2 - bp[k];
        v = one / (ax2 + bp[k]);
        ss = u * v;
        s_h = set_lo(ss, 0);
        t_h = set_hi(zero, (uint32_t)(((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18)));
        t_l = ax2 - (t_h - bp[k]);
        s_l = v * ((u - s_h * t_h) - s_h * t_l);
        s2 = ss * ss;
        r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
        r += s_l * (s_h + ss);
        s2 = s_h * s_h;
        t_h = set_lo(3.0 + s2 + r, 0);
        t_l = r - ((t_h - 3.0) - s2);
        u = s_h * t_h;
        v = s_l * t_h + t_l * ss;
        double p_h2 = set_lo(u + v, 0);
        double p_l2 = v - (p_h2 - u);
        z_h = cp_h * p_h2;
        z_l = cp_l * p_h2 + p_l2 * cp + dp_l[k];
        t = (double)n;
        t1 = ((z_h + z_l) + dp_h[k]) + t;
        t1 = set_lo(t1, 0);
        t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
    }

    /* split up y into y1+y2 and compute (y1+y2)*(t1+t2) */
    y1 = set_lo(y, 0);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    z = p_l + p_h;
    j = hi(z);
    i = (int)lo(z);
    if (j >= 0x40900000)
    {
        if (((j - 0x40900000) | i) != 0)
            return s * huge * huge;
        if (p_l + ovt > z - p_h) return s * huge * huge;
    }
    else if ((j & 0x7fffffff) >= 0x4090cc00)
    {
        if (((j - 0xc090cc00) | i) != 0)
            return s * tiny * tiny;
        if (p_l <= z - p_h) return s * tiny * tiny;
    }

    /* compute 2**(p_h+p_l) */
    i = j & 0x7fffffff;
    k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000)
    {
        n = j + (0x00100000 >> (k + 1));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;
        t = set_hi(zero, (uint32_t)n & ~((uint32_t)(0x000fffff >> k)));
        n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
        if (j < 0) n = -n;
        p_h -= t;
    }
    t = p_l + p_h;
    t = set_lo(t, 0);
    u = t * lg2_h;
    v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
    z = u + v;
    w = v - (z - u);
    t = z * z;
    t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    r = (z * t1) / (t1 - two) - (w + z * w);
    z = one - (r - z);
    j = hi(z);
    j += (n << 20);
    if ((j >> 20) <= 0)
        z = scalbn(z, n);
    else
        z = set_hi(z, (uint32_t)(hi(z) + (n << 20)));
    return s * z;
}
/* ------------------------------------------------------- atan and atan2 */

/* fdlibm's s_atan and e_atan2, ported unchanged, for java.lang.StrictMath.atan2
 * (fdlibm). The AI's look and move helpers and EntityLivingBase.onUpdate's yaw
 * go through it. */

static const double atanhi[] = {
    4.63647609000806093515e-01,
    7.85398163397448278999e-01,
    9.82793723247329054082e-01,
    1.57079632679489655800e+00,
};

static const double atanlo[] = {
    2.26987774529616870924e-17,
    3.06161699786838301793e-17,
    1.39033110312309984516e-17,
    6.12323399573676603587e-17,
};

static const double aT[] = {
    3.33333333333329318027e-01,
    -1.99999999998764832476e-01,
    1.42857142725034663711e-01,
    -1.11111104054623557880e-01,
    9.09088713343650656196e-02,
    -7.69187620504482999495e-02,
    6.66107313738753120669e-02,
    -5.83357013379057348645e-02,
    4.97687799461593236017e-02,
    -3.65315727442169155270e-02,
    1.62858201153657823623e-02,
};

static double fd_atan(double x)
{
    const double one = 1.0;
    double w, s1, s2, z;
    int32_t ix, hx, id;

    hx = hi(x);
    ix = hx & 0x7fffffff;

    if (ix >= 0x44100000)
    {
        uint32_t low = lo(x);

        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && low != 0)) return x + x;

        if (hx > 0) return atanhi[3] + atanlo[3];
        return -atanhi[3] - atanlo[3];
    }

    if (ix < 0x3fdc0000)
    {
        if (ix < 0x3e200000) return x;   /* huge + x > one always, raise inexact */
        id = -1;
    }
    else
    {
        x = fabs(x);

        if (ix < 0x3ff30000)
        {
            if (ix < 0x3fe60000)
            {
                id = 0;
                x = (2.0 * x - one) / (2.0 + x);
            }
            else
            {
                id = 1;
                x = (x - one) / (x + one);
            }
        }
        else
        {
            if (ix < 0x40038000)
            {
                id = 2;
                x = (x - 1.5) / (one + 1.5 * x);
            }
            else
            {
                id = 3;
                x = -1.0 / x;
            }
        }
    }

    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));

    if (id < 0) return x - x * (s1 + s2);

    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return (hx < 0) ? -z : z;
}

double fd_atan2(double y, double x)
{
    static const double tiny = 1.0e-300;
    static const double pi_o_4 = 7.8539816339744827900E-01;
    static const double pi_o_2 = 1.5707963267948965580E+00;
    static const double pi = 3.1415926535897931160E+00;
    static const double pi_lo = 1.2246467991473531772E-16;
    double z;
    int32_t k, m, hx, hy, ix, iy;
    uint32_t lx, ly;

    hx = hi(x);
    lx = lo(x);
    ix = hx & 0x7fffffff;
    hy = hi(y);
    ly = lo(y);
    iy = hy & 0x7fffffff;

    if ((ix | (lx != 0 ? 1 : 0)) > 0x7ff00000 || (iy | (ly != 0 ? 1 : 0)) > 0x7ff00000) return x + y;

    if (hx == 0x3ff00000 && lx == 0) return fd_atan(y);

    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);

    if ((iy | ly) == 0)
    {
        switch (m)
        {
            case 0:
            case 1: return y;
            case 2: return pi + tiny;
            case 3: return -pi - tiny;
        }
    }

    if ((ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    if (ix == 0x7ff00000)
    {
        if (iy == 0x7ff00000)
        {
            switch (m)
            {
                case 0: return pi_o_4 + tiny;
                case 1: return -pi_o_4 - tiny;
                case 2: return 3.0 * pi_o_4 + tiny;
                case 3: return -3.0 * pi_o_4 - tiny;
            }
        }
        else
        {
            switch (m)
            {
                case 0: return 0.0;
                case 1: return -0.0;
                case 2: return pi + tiny;
                case 3: return -pi - tiny;
            }
        }
    }

    if (iy == 0x7ff00000) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    k = (iy - ix) >> 20;

    if (k > 60) z = pi_o_2 + 0.5 * pi_lo;
    else if (hx < 0 && k < -60) z = 0.0;
    else z = fd_atan(fabs(y / x));

    switch (m)
    {
        case 0: return z;
        case 1:
            return set_hi(z, (uint32_t)(hi(z) ^ (int32_t)0x80000000));
        case 2: return pi - (z - pi_lo);
        default: return (z - pi_lo) - pi;
    }
}
