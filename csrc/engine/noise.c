/* Minecraft 1.7.10 noise generators, ported from oracle/src/world/gen/NoiseGenerator*.java.
 * Operation order follows the Java exactly; build with -ffp-contract=off. */
#include "noise.h"
#include "jmath.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- improved (Perlin) */

static const double GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0};
static const double GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
static const double GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
static const double G2X[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0};
static const double G2Z[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};

static void shuffle(int *p, jrand *r)
{
    for (int i = 0; i < 256; ++i) p[i] = i;
    for (int i = 0; i < 256; ++i)
    {
        int j = jr_int_n(r, 256 - i) + i;
        int t = p[i];
        p[i] = p[j];
        p[j] = t;
        p[i + 256] = p[i];
    }
}

void improved_init(struct improved *n, jrand *r)
{
    n->xo = jr_double(r) * 256.0;
    n->yo = jr_double(r) * 256.0;
    n->zo = jr_double(r) * 256.0;
    shuffle(n->p, r);
}

static double lerp(double t, double a, double b)
{
    return a + t * (b - a);
}

static double grad3(int h, double x, double y, double z)
{
    h &= 15;
    return GX[h] * x + GY[h] * y + GZ[h] * z;
}

static double grad2(int h, double x, double z)
{
    h &= 15;
    return G2X[h] * x + G2Z[h] * z;
}

static double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/* The y side of one octave's sweep is a function of the y index m alone
 * (y0 + m * sy + yo): each point's integer cell and fraction, its fade, and
 * the first m of its cell (the corner gradients are taken at a cell's first
 * y). Filled once per octave rather than once per column: the same double
 * operations, so the same values. */
#define NOISE_YMAX 64
struct noise_y {
    int Y[NOISE_YMAX], start[NOISE_YMAX];
    double f[NOISE_YMAX], v[NOISE_YMAX];
};

static void noise_y_fill(struct noise_y *t, double y0, int ys, double sy, double yo)
{
    int last = -1, st = 0;

    for (int m = 0; m < ys; ++m)
    {
        double y = y0 + (double)m * sy + yo;
        int yi = mh_floor(y);
        int Y = yi & 255;

        y -= (double)yi;
        if (m == 0 || Y != last)
        {
            last = Y;
            st = m;
        }
        t->Y[m] = Y;
        t->start[m] = st;
        t->f[m] = y;
        t->v[m] = fade(y);
    }
}

/* improved_add's 3D sweep for a y count past NOISE_YMAX, computing each
 * point's y as it goes */
static void improved_add_wide(const struct improved *n, double *out, double x0, double y0, double z0,
                              int xs, int ys, int zs, double sx, double sy, double sz, double amp)
{
    const int *p = n->p;
    double inv = 1.0 / amp;
    int k = 0, last_y = -1;
    double c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    for (int i = 0; i < xs; ++i)
    {
        double x = x0 + (double)i * sx + n->xo;
        int xi = mh_floor(x);
        int X = xi & 255;
        x -= (double)xi;
        double u = fade(x);
        for (int j = 0; j < zs; ++j)
        {
            double z = z0 + (double)j * sz + n->zo;
            int zi = mh_floor(z);
            int Z = zi & 255;
            z -= (double)zi;
            double w = fade(z);
            for (int m = 0; m < ys; ++m)
            {
                double y = y0 + (double)m * sy + n->yo;
                int yi = mh_floor(y);
                int Y = yi & 255;
                y -= (double)yi;
                double v = fade(y);
                if (m == 0 || Y != last_y)
                {
                    last_y = Y;
                    int a = p[X] + Y, aa = p[a] + Z, ab = p[a + 1] + Z;
                    int b = p[X + 1] + Y, ba = p[b] + Z, bb = p[b + 1] + Z;
                    c0 = lerp(u, grad3(p[aa], x, y, z), grad3(p[ba], x - 1.0, y, z));
                    c1 = lerp(u, grad3(p[ab], x, y - 1.0, z), grad3(p[bb], x - 1.0, y - 1.0, z));
                    c2 = lerp(u, grad3(p[aa + 1], x, y, z - 1.0), grad3(p[ba + 1], x - 1.0, y, z - 1.0));
                    c3 = lerp(u, grad3(p[ab + 1], x, y - 1.0, z - 1.0), grad3(p[bb + 1], x - 1.0, y - 1.0, z - 1.0));
                }
                double l1 = lerp(v, c0, c1);
                double l2 = lerp(v, c2, c3);
                out[k++] += lerp(w, l1, l2) * inv;
            }
        }
    }
}

/* NoiseGeneratorImproved.populateNoiseArray */
static void improved_add(const struct improved *n, double *out, double x0, double y0, double z0,
                         int xs, int ys, int zs, double sx, double sy, double sz, double amp)
{
    const int *p = n->p;
    double inv = 1.0 / amp;
    if (ys == 1)
    {
        int k = 0;
        for (int i = 0; i < xs; ++i)
        {
            double x = x0 + (double)i * sx + n->xo;
            int xi = mh_floor(x);
            int X = xi & 255;
            x -= (double)xi;
            double u = fade(x);
            for (int j = 0; j < zs; ++j)
            {
                double z = z0 + (double)j * sz + n->zo;
                int zi = mh_floor(z);
                int Z = zi & 255;
                z -= (double)zi;
                double w = fade(z);
                int a = p[X] + 0;
                int aa = p[a] + Z;
                int b = p[X + 1] + 0;
                int ba = p[b] + Z;
                double l1 = lerp(u, grad2(p[aa], x, z), grad3(p[ba], x - 1.0, 0.0, z));
                double l2 = lerp(u, grad3(p[aa + 1], x, 0.0, z - 1.0), grad3(p[ba + 1], x - 1.0, 0.0, z - 1.0));
                out[k++] += lerp(w, l1, l2) * inv;
            }
        }
        return;
    }
    if (ys > NOISE_YMAX)
    {
        improved_add_wide(n, out, x0, y0, z0, xs, ys, zs, sx, sy, sz, amp);
        return;
    }

    struct noise_y t;

    noise_y_fill(&t, y0, ys, sy, n->yo);

    int k = 0;
    double c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    for (int i = 0; i < xs; ++i)
    {
        double x = x0 + (double)i * sx + n->xo;
        int xi = mh_floor(x);
        int X = xi & 255;
        x -= (double)xi;
        double u = fade(x);
        for (int j = 0; j < zs; ++j)
        {
            double z = z0 + (double)j * sz + n->zo;
            int zi = mh_floor(z);
            int Z = zi & 255;
            z -= (double)zi;
            double w = fade(z);
            for (int m = 0; m < ys; ++m)
            {
                if (t.start[m] == m)
                {
                    /* corner gradients are reused while the integer y cell does not change */
                    int Y = t.Y[m];
                    double y = t.f[m];
                    int a = p[X] + Y, aa = p[a] + Z, ab = p[a + 1] + Z;
                    int b = p[X + 1] + Y, ba = p[b] + Z, bb = p[b + 1] + Z;
                    c0 = lerp(u, grad3(p[aa], x, y, z), grad3(p[ba], x - 1.0, y, z));
                    c1 = lerp(u, grad3(p[ab], x, y - 1.0, z), grad3(p[bb], x - 1.0, y - 1.0, z));
                    c2 = lerp(u, grad3(p[aa + 1], x, y, z - 1.0), grad3(p[ba + 1], x - 1.0, y, z - 1.0));
                    c3 = lerp(u, grad3(p[ab + 1], x, y - 1.0, z - 1.0), grad3(p[bb + 1], x - 1.0, y - 1.0, z - 1.0));
                }
                double v = t.v[m];
                double l1 = lerp(v, c0, c1);
                double l2 = lerp(v, c2, c3);
                out[k++] += lerp(w, l1, l2) * inv;
            }
        }
    }
}

/* improved_add_need past NOISE_YMAX, computing each point's y as it goes */
static void improved_add_need_wide(const struct improved *n, double *out, const unsigned char *need, double x0, double y0,
                              double z0, int xs, int ys, int zs, double sx, double sy, double sz, double amp)
{
    const int *p = n->p;
    double inv = 1.0 / amp;
    int k = 0;
    for (int i = 0; i < xs; ++i)
    {
        double x = x0 + (double)i * sx + n->xo;
        int xi = mh_floor(x);
        int X = xi & 255;
        x -= (double)xi;
        double u = fade(x);
        for (int j = 0; j < zs; ++j)
        {
            int any = 0;
            for (int m = 0; m < ys; ++m) any |= need[k + m];
            if (!any)
            {
                k += ys;
                continue;
            }
            double z = z0 + (double)j * sz + n->zo;
            int zi = mh_floor(z);
            int Z = zi & 255;
            z -= (double)zi;
            double w = fade(z);
            /* the y cell whose corners are due (last_y) and the y they are
             * computed with (cy), and whether they are computed yet */
            int last_y = -1, due = 0;
            double cy = 0, c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            for (int m = 0; m < ys; ++m, ++k)
            {
                double y = y0 + (double)m * sy + n->yo;
                int yi = mh_floor(y);
                int Y = yi & 255;
                y -= (double)yi;
                if (m == 0 || Y != last_y)
                {
                    last_y = Y;
                    cy = y;
                    due = 1;
                }
                if (!need[k]) continue;
                if (due)
                {
                    due = 0;
                    int a = p[X] + last_y, aa = p[a] + Z, ab = p[a + 1] + Z;
                    int b = p[X + 1] + last_y, ba = p[b] + Z, bb = p[b + 1] + Z;
                    c0 = lerp(u, grad3(p[aa], x, cy, z), grad3(p[ba], x - 1.0, cy, z));
                    c1 = lerp(u, grad3(p[ab], x, cy - 1.0, z), grad3(p[bb], x - 1.0, cy - 1.0, z));
                    c2 = lerp(u, grad3(p[aa + 1], x, cy, z - 1.0), grad3(p[ba + 1], x - 1.0, cy, z - 1.0));
                    c3 = lerp(u, grad3(p[ab + 1], x, cy - 1.0, z - 1.0), grad3(p[bb + 1], x - 1.0, cy - 1.0, z - 1.0));
                }
                double v = fade(y);
                double l1 = lerp(v, c0, c1);
                double l2 = lerp(v, c2, c3);
                out[k] += lerp(w, l1, l2) * inv;
            }
        }
    }
}

/* improved_add's 3D path at the needed points only */
static void improved_add_need(const struct improved *n, double *out, const unsigned char *need, double x0, double y0,
                              double z0, int xs, int ys, int zs, double sx, double sy, double sz, double amp)
{
    const int *p = n->p;
    double inv = 1.0 / amp;
    int k = 0;
    struct noise_y t;

    if (ys > NOISE_YMAX)
    {
        improved_add_need_wide(n, out, need, x0, y0, z0, xs, ys, zs, sx, sy, sz, amp);
        return;
    }
    noise_y_fill(&t, y0, ys, sy, n->yo);
    for (int i = 0; i < xs; ++i)
    {
        double x = x0 + (double)i * sx + n->xo;
        int xi = mh_floor(x);
        int X = xi & 255;
        x -= (double)xi;
        double u = fade(x);
        for (int j = 0; j < zs; ++j)
        {
            int any = 0;
            for (int m = 0; m < ys; ++m) any |= need[k + m];
            if (!any)
            {
                k += ys;
                continue;
            }
            double z = z0 + (double)j * sz + n->zo;
            int zi = mh_floor(z);
            int Z = zi & 255;
            z -= (double)zi;
            double w = fade(z);
            /* the cell whose corners are computed (its first m), -1 none */
            int cstart = -1;
            double c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            for (int m = 0; m < ys; ++m, ++k)
            {
                if (!need[k]) continue;
                if (t.start[m] != cstart)
                {
                    cstart = t.start[m];
                    int Y = t.Y[m];
                    double cy = t.f[cstart];
                    int a = p[X] + Y, aa = p[a] + Z, ab = p[a + 1] + Z;
                    int b = p[X + 1] + Y, ba = p[b] + Z, bb = p[b + 1] + Z;
                    c0 = lerp(u, grad3(p[aa], x, cy, z), grad3(p[ba], x - 1.0, cy, z));
                    c1 = lerp(u, grad3(p[ab], x, cy - 1.0, z), grad3(p[bb], x - 1.0, cy - 1.0, z));
                    c2 = lerp(u, grad3(p[aa + 1], x, cy, z - 1.0), grad3(p[ba + 1], x - 1.0, cy, z - 1.0));
                    c3 = lerp(u, grad3(p[ab + 1], x, cy - 1.0, z - 1.0), grad3(p[bb + 1], x - 1.0, cy - 1.0, z - 1.0));
                }
                double v = t.v[m];
                double l1 = lerp(v, c0, c1);
                double l2 = lerp(v, c2, c3);
                out[k] += lerp(w, l1, l2) * inv;
            }
        }
    }
}

void octaves_init(struct octaves *o, jrand *r, int n)
{
    o->n = n;
    for (int i = 0; i < n; ++i) improved_init(&o->g[i], r);
}

static int64_t floor_long(double d)
{
    int64_t i = (int64_t)d;
    return d < (double)i ? i - 1 : i;
}

void octaves_3d(const struct octaves *o, double *out, int x, int y, int z, int xs, int ys, int zs,
                double sx, double sy, double sz)
{
    memset(out, 0, sizeof(double) * (size_t)xs * (size_t)ys * (size_t)zs);
    double amp = 1.0;
    for (int i = 0; i < o->n; ++i)
    {
        double dx = (double)x * amp * sx;
        double dy = (double)y * amp * sy;
        double dz = (double)z * amp * sz;
        int64_t lx = floor_long(dx), lz = floor_long(dz);
        dx -= (double)lx;
        dz -= (double)lz;
        lx %= 16777216LL;
        lz %= 16777216LL;
        dx += (double)lx;
        dz += (double)lz;
        improved_add(&o->g[i], out, dx, dy, dz, xs, ys, zs, sx * amp, sy * amp, sz * amp, amp);
        amp /= 2.0;
    }
}

void octaves_3d_need(const struct octaves *o, double *out, const unsigned char *need, int x, int y, int z, int xs,
                     int ys, int zs, double sx, double sy, double sz)
{
    memset(out, 0, sizeof(double) * (size_t)xs * (size_t)ys * (size_t)zs);
    double amp = 1.0;
    for (int i = 0; i < o->n; ++i)
    {
        double dx = (double)x * amp * sx;
        double dy = (double)y * amp * sy;
        double dz = (double)z * amp * sz;
        int64_t lx = floor_long(dx), lz = floor_long(dz);
        dx -= (double)lx;
        dz -= (double)lz;
        lx %= 16777216LL;
        lz %= 16777216LL;
        dx += (double)lx;
        dz += (double)lz;
        improved_add_need(&o->g[i], out, need, dx, dy, dz, xs, ys, zs, sx * amp, sy * amp, sz * amp, amp);
        amp /= 2.0;
    }
}

void octaves_2d(const struct octaves *o, double *out, int x, int z, int xs, int zs, double sx, double sz)
{
    octaves_3d(o, out, x, 10, z, xs, 1, zs, sx, 1.0, sz);
}

/* ---------------------------------------------------------------- simplex */

static const int SG[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                              {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

void simplex_init(struct simplex *s, jrand *r)
{
    s->xo = jr_double(r) * 256.0;
    s->yo = jr_double(r) * 256.0;
    s->zo = jr_double(r) * 256.0;
    shuffle(s->p, r);
}

static int sfloor(double d)
{
    return d > 0.0 ? (int)d : (int)d - 1;
}

static double sdot(const int *g, double x, double y)
{
    return (double)g[0] * x + (double)g[1] * y;
}

/* NoiseGeneratorSimplex.func_151606_a: out index z * xs + x. */
static void simplex_add(const struct simplex *s, double *out, double x0, double z0, int xs, int zs,
                        double sx, double sz, double amp)
{
    const double F2 = 0.5 * (sqrt(3.0) - 1.0);
    const double G2 = (3.0 - sqrt(3.0)) / 6.0;
    const int *p = s->p;
    int k = 0;
    for (int j = 0; j < zs; ++j)
    {
        double yin = (z0 + (double)j) * sz + s->yo;
        for (int i = 0; i < xs; ++i)
        {
            double xin = (x0 + (double)i) * sx + s->xo;
            double t = (xin + yin) * F2;
            int ii = sfloor(xin + t), jj = sfloor(yin + t);
            double u = (double)(ii + jj) * G2;
            double X0 = (double)ii - u, Y0 = (double)jj - u;
            double x = xin - X0, y = yin - Y0;
            int i1, j1;
            if (x > y) { i1 = 1; j1 = 0; }
            else { i1 = 0; j1 = 1; }
            double x1 = x - (double)i1 + G2, y1 = y - (double)j1 + G2;
            double x2 = x - 1.0 + 2.0 * G2, y2 = y - 1.0 + 2.0 * G2;
            int a = ii & 255, b = jj & 255;
            int g0 = p[a + p[b]] % 12;
            int g1 = p[a + i1 + p[b + j1]] % 12;
            int g2 = p[a + 1 + p[b + 1]] % 12;
            double n0, n1, n2;
            double t0 = 0.5 - x * x - y * y;
            if (t0 < 0.0) n0 = 0.0;
            else { t0 *= t0; n0 = t0 * t0 * sdot(SG[g0], x, y); }
            double t1 = 0.5 - x1 * x1 - y1 * y1;
            if (t1 < 0.0) n1 = 0.0;
            else { t1 *= t1; n1 = t1 * t1 * sdot(SG[g1], x1, y1); }
            double t2 = 0.5 - x2 * x2 - y2 * y2;
            if (t2 < 0.0) n2 = 0.0;
            else { t2 *= t2; n2 = t2 * t2 * sdot(SG[g2], x2, y2); }
            out[k++] += 70.0 * (n0 + n1 + n2) * amp;
        }
    }
}

/* NoiseGeneratorSimplex.func_151605_a: one point, and unlike the array form it
 * does not add the generator's offsets. */
static double simplex_point(const struct simplex *s, double xin, double yin)
{
    const double F2 = 0.5 * (sqrt(3.0) - 1.0);
    const double G2 = (3.0 - sqrt(3.0)) / 6.0;
    const int *p = s->p;
    double t = (xin + yin) * F2;
    int ii = sfloor(xin + t), jj = sfloor(yin + t);
    double u = (double)(ii + jj) * G2;
    double X0 = (double)ii - u, Y0 = (double)jj - u;
    double x = xin - X0, y = yin - Y0;
    int i1, j1;
    if (x > y) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }
    double x1 = x - (double)i1 + G2, y1 = y - (double)j1 + G2;
    double x2 = x - 1.0 + 2.0 * G2, y2 = y - 1.0 + 2.0 * G2;
    int a = ii & 255, b = jj & 255;
    int g0 = p[a + p[b]] % 12;
    int g1 = p[a + i1 + p[b + j1]] % 12;
    int g2 = p[a + 1 + p[b + 1]] % 12;
    double n0, n1, n2;
    double t0 = 0.5 - x * x - y * y;
    if (t0 < 0.0) n0 = 0.0;
    else { t0 *= t0; n0 = t0 * t0 * sdot(SG[g0], x, y); }
    double t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 < 0.0) n1 = 0.0;
    else { t1 *= t1; n1 = t1 * t1 * sdot(SG[g1], x1, y1); }
    double t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 < 0.0) n2 = 0.0;
    else { t2 *= t2; n2 = t2 * t2 * sdot(SG[g2], x2, y2); }
    return 70.0 * (n0 + n1 + n2);
}

void perlin_init(struct perlin *p, jrand *r, int n)
{
    p->n = n;
    for (int i = 0; i < n; ++i) simplex_init(&p->g[i], r);
}

/* NoiseGeneratorPerlin.func_151601_a */
double perlin_point(const struct perlin *p, double x, double z)
{
    double sum = 0.0, f = 1.0;
    for (int i = 0; i < p->n; ++i)
    {
        sum += simplex_point(&p->g[i], x * f, z * f) / f;
        f /= 2.0;
    }
    return sum;
}

/* NoiseGeneratorPerlin.func_151599_a / func_151600_a with falloff 0.5. */
void perlin_2d(const struct perlin *p, double *out, double x, double z, int xs, int zs,
               double sx, double sz, double lacunarity)
{
    memset(out, 0, sizeof(double) * (size_t)xs * (size_t)zs);
    double fall = 1.0, freq = 1.0;
    for (int i = 0; i < p->n; ++i)
    {
        simplex_add(&p->g[i], out, x, z, xs, zs, sx * freq * fall, sz * freq * fall, 0.55 / fall);
        freq *= lacunarity;
        fall *= 0.5;
    }
}
