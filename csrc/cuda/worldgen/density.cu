/* density: terrain.c's func_147423_a density field (a block per chunk, a
 * thread per noise sample), func_147424_a's trilinear fill (a thread per
 * 4x4x8 cell block, the same incremental sums as C) and the surface pass's
 * stone noise. The noise is noise.c's, sample by sample. */
#include "dev.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__device__ static const double GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0};
__device__ static const double GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
__device__ static const double GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};

__device__ static inline double lerp(double t, double a, double b)
{
    return a + t * (b - a);
}

__device__ static inline double grad3(int h, double x, double y, double z)
{
    h &= 15;
    return GX[h] * x + GY[h] * y + GZ[h] * z;
}

__device__ static inline double grad2(int h, double x, double z)
{
    h &= 15;
    return GX[h] * x + GZ[h] * z;   /* G2X and G2Z of noise.c are GX and GZ */
}

__device__ static inline double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

__device__ static inline int floor_int(double d)
{
    int i = (int)d;
    return d < (double)i ? i - 1 : i;
}

__device__ static inline int64_t floor_long(double d)
{
    int64_t i = (int64_t)d;
    return d < (double)i ? i - 1 : i;
}

/* NoiseGeneratorImproved.populateNoiseArray, the 3D branch, for the one output
 * sample (i, j, m). C walks m upward and recomputes the four corner lerps only
 * when the integer y cell changes, then keeps using them with the next
 * samples' y fractions; so this finds the first sample m0 of the run of equal
 * cells ending at m and takes the corners from m0's y. */
__device__ static double improved_3d(const struct g_improved *n, double x0, double y0, double z0, int i, int m, int j,
                                     double sx, double sy, double sz, double inv)
{
    const uint8_t *p = n->p;
    double x = x0 + (double)i * sx + n->xo;
    int xi = floor_int(x);
    int X = xi & 255;
    x -= (double)xi;
    double u = fade(x);
    double z = z0 + (double)j * sz + n->zo;
    int zi = floor_int(z);
    int Z = zi & 255;
    z -= (double)zi;
    double w = fade(z);

    double y = y0 + (double)m * sy + n->yo;
    int yi = floor_int(y);
    int Y = yi & 255;
    y -= (double)yi;
    double v = fade(y);

    int m0 = m;
    if (m0 > 0 && (floor_int(y0 + (double)(m0 - 1) * sy + n->yo) & 255) == Y)
    {
        --m0;
        if (sy < 128.0)
        {
            /* consecutive cells differ by under 256, so the run is the samples
             * whose floor equals yi exactly: a suffix of 0..m, since the
             * sample's y (each step rounded, so monotone in m) never falls;
             * its start by bisection, not one sample at a time */
            int lo = 0;
            while (lo < m0)
            {
                int mid = (lo + m0) >> 1;
                if (floor_int(y0 + (double)mid * sy + n->yo) == yi) m0 = mid;
                else lo = mid + 1;
            }
        }
        else
            while (m0 > 0 && (floor_int(y0 + (double)(m0 - 1) * sy + n->yo) & 255) == Y) --m0;
    }
    double yc = y;
    if (m0 != m)
    {
        yc = y0 + (double)m0 * sy + n->yo;
        yc -= (double)floor_int(yc);
    }
    int a = p[X] + Y, aa = p[a] + Z, ab = p[a + 1] + Z;
    int b = p[X + 1] + Y, ba = p[b] + Z, bb = p[b + 1] + Z;
    double c0 = lerp(u, grad3(p[aa], x, yc, z), grad3(p[ba], x - 1.0, yc, z));
    double c1 = lerp(u, grad3(p[ab], x, yc - 1.0, z), grad3(p[bb], x - 1.0, yc - 1.0, z));
    double c2 = lerp(u, grad3(p[aa + 1], x, yc, z - 1.0), grad3(p[ba + 1], x - 1.0, yc, z - 1.0));
    double c3 = lerp(u, grad3(p[ab + 1], x, yc - 1.0, z - 1.0), grad3(p[bb + 1], x - 1.0, yc - 1.0, z - 1.0));
    double l1 = lerp(v, c0, c1);
    double l2 = lerp(v, c2, c3);
    return lerp(w, l1, l2) * inv;
}

/* the ys == 1 branch (octaves_2d), one sample (i, j) */
__device__ static double improved_2d(const struct g_improved *n, double x0, double z0, int i, int j,
                                     double sx, double sz, double inv)
{
    const uint8_t *p = n->p;
    double x = x0 + (double)i * sx + n->xo;
    int xi = floor_int(x);
    int X = xi & 255;
    x -= (double)xi;
    double u = fade(x);
    double z = z0 + (double)j * sz + n->zo;
    int zi = floor_int(z);
    int Z = zi & 255;
    z -= (double)zi;
    double w = fade(z);
    int a = p[X] + 0;
    int aa = p[a] + Z;
    int b = p[X + 1] + 0;
    int ba = p[b] + Z;
    double l1 = lerp(u, grad2(p[aa], x, z), grad3(p[ba], x - 1.0, 0.0, z));
    double l2 = lerp(u, grad3(p[aa + 1], x, 0.0, z - 1.0), grad3(p[ba + 1], x - 1.0, 0.0, z - 1.0));
    return lerp(w, l1, l2) * inv;
}

/* NoiseGeneratorOctaves.generateNoiseOctaves at one sample: the octave sum in
 * the octave order C adds them. ys == 1 is the 2D overload. The dimension
 * units call it too (dev.cuh). */
__device__ __noinline__ double octaves_at(const struct g_improved *g, int oct, int x, int y, int z, int i, int m, int j,
                                    int ys, double sx, double sy, double sz)
{
    double sum = 0.0, amp = 1.0;
    for (int o = 0; o < oct; ++o)
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
        double inv = 1.0 / amp;
        if (ys == 1) sum += improved_2d(&g[o], dx, dz, i, j, sx * amp, sz * amp, inv);
        else sum += improved_3d(&g[o], dx, dy, dz, i, m, j, sx * amp, sy * amp, sz * amp, inv);
        amp /= 2.0;
    }
    return sum;
}

/* The field's noises by table (the density kernel's): C's populateNoiseArray
 * computes each octave's x and z cells, fractions and fades once per column
 * and its y cell, fraction and fade once per sample, and recomputes the four
 * corner lerps only when the y cell changes (the stale-corner rule
 * improved_3d finds by its walk back). So per octave these are tabled for the
 * chunk's 5 x, 5 z and 33 y samples first, each by C's own expressions, and a
 * sample's octave is then its corner and blend arithmetic alone. */
#define FT_OCT 16
struct field_tab {
    double xf[FT_OCT][5], xf1[FT_OCT][5], xu[FT_OCT][5];
    double zf[FT_OCT][5], zf1[FT_OCT][5], zu[FT_OCT][5];
    double yc[FT_OCT][33], yc1[FT_OCT][33], yv[FT_OCT][33];
    uint8_t X[FT_OCT][5], Z[FT_OCT][5], Y[FT_OCT][33];
};

/* the octave's start as octaves_at computes it: one coordinate (x, y or z of
 * the chunk grid times amp and the scale), its long floor taken apart and
 * wrapped at 2^24 */
__device__ static inline double oct_start(int c, double amp, double scale)
{
    double d = (double)c * amp * scale;
    int64_t l = floor_long(d);
    d -= (double)l;
    l %= 16777216LL;
    return d + (double)l;
}

/* the tables of noise g's first oct octaves at chunk grid (x, 0, z), scales
 * (sx, sy, sz), the block's threads together */
__device__ static void field_tab_build(struct field_tab *t, const struct g_improved *g, int oct, int x, int z, double sx,
                                       double sy, double sz)
{
    for (int e = threadIdx.x; e < oct * 43; e += blockDim.x)
    {
        int o = e / 43, k = e % 43;
        const struct g_improved *n = &g[o];
        double amp = ldexp(1.0, -o);   /* 1.0 halved o times: exact */
        if (k < 10)
        {
            /* x (k < 5) or z: improved_3d's x = x0 + i * sx + xo */
            int zc = k >= 5, i = zc ? k - 5 : k;
            double s0 = zc ? sz : sx;
            double v = oct_start(zc ? z : x, amp, s0) + (double)i * (s0 * amp) + (zc ? n->zo : n->xo);
            int vi = floor_int(v);
            v -= (double)vi;
            if (zc) t->zf[o][i] = v, t->zf1[o][i] = v - 1.0, t->zu[o][i] = fade(v), t->Z[o][i] = (uint8_t)(vi & 255);
            else t->xf[o][i] = v, t->xf1[o][i] = v - 1.0, t->xu[o][i] = fade(v), t->X[o][i] = (uint8_t)(vi & 255);
        }
        else
        {
            /* y sample m, and the corner y of its run of equal cells */
            int m = k - 10;
            double y0 = oct_start(0, amp, sy), sya = sy * amp;
            double y = y0 + (double)m * sya + n->yo;
            int yi = floor_int(y), Y = yi & 255;
            y -= (double)yi;
            int m0 = m;
            if (m0 > 0 && (floor_int(y0 + (double)(m0 - 1) * sya + n->yo) & 255) == Y)
            {
                --m0;
                if (sya < 128.0)
                {
                    int lo = 0;
                    while (lo < m0)
                    {
                        int mid = (lo + m0) >> 1;
                        if (floor_int(y0 + (double)mid * sya + n->yo) == yi) m0 = mid;
                        else lo = mid + 1;
                    }
                }
                else
                    while (m0 > 0 && (floor_int(y0 + (double)(m0 - 1) * sya + n->yo) & 255) == Y) --m0;
            }
            double yc = y;
            if (m0 != m)
            {
                yc = y0 + (double)m0 * sya + n->yo;
                yc -= (double)floor_int(yc);
            }
            t->yc[o][m] = yc;
            t->yc1[o][m] = yc - 1.0;
            t->yv[o][m] = fade(y);
            t->Y[o][m] = (uint8_t)Y;
        }
    }
}

/* s + a signed zero, as the FPU rounds to nearest: s, but for two zeros
 * negative zero only when both are */
__device__ static inline long long add_zero_bits(long long s, long long zero)
{
    return (s << 1) == 0 ? s & zero : s;
}

/* grad3's x * GX + y * GY + z * GZ exactly as C evaluates it ((xGX + yGY) +
 * zGZ, each product rounded), with one floating add: a product of a
 * coordinate and 1, -1 or 0 is the coordinate, its negation or a zero of the
 * coordinate's sign; every vector has one zero component, and adding a zero
 * changes a sum only when the sum is a zero too (add_zero_bits). Per h, GS
 * holds the three coefficients' signs (bit 0 x, 1 y, 2 z: negative) and GZI
 * the zero component (0 x, 1 y, 2 z). */
__device__ static inline double grad3_exact(int h, double x, double y, double z)
{
    h &= 15;
    /* GX {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0}, GY {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1},
     * GZ {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1}: the zero component by h */
    int zi = h < 4 ? 2 : h < 8 ? 1 : h < 12 ? 0 : (h & 1) ? 0 : 2;
    /* the negative coefficients by h: x for 1,3,5,7,14; y for 2,3,9,11,13,15; z for 6,7,10,11,15 */
    int nx = (0x40AAu >> h) & 1, ny = (0xAA0Cu >> h) & 1, nz = (0x8CC0u >> h) & 1;
    const long long SB = (long long)0x8000000000000000ULL;
    long long bx = __double_as_longlong(x), by = __double_as_longlong(y), bz = __double_as_longlong(z);
    long long tx = zi == 0 ? bx & SB : bx ^ (nx ? SB : 0);
    long long ty = zi == 1 ? by & SB : by ^ (ny ? SB : 0);
    long long tz = zi == 2 ? bz & SB : bz ^ (nz ? SB : 0);
    const long long NZ = SB;   /* negative zero: the additive identity */
    long long p = zi == 2 ? tx : add_zero_bits(zi == 0 ? ty : tx, zi == 0 ? tx : ty);
    long long q = zi == 2 ? ty : tz;
    double r = __longlong_as_double(p) + __longlong_as_double(q);
    return __longlong_as_double(add_zero_bits(__double_as_longlong(r), zi == 2 ? tz : NZ));
}

/* one sample (i, j, m) of the tabled octave o of noise n: improved_3d's
 * corners and blend, times inv */
__device__ static inline double field_sample(const struct field_tab *t, const struct g_improved *n, int o, int i, int j,
                                             int m, double inv)
{
    const uint8_t *p = n->p;
    int X = t->X[o][i], Z = t->Z[o][j], Y = t->Y[o][m];
    double x = t->xf[o][i], x1 = t->xf1[o][i], u = t->xu[o][i];
    double z = t->zf[o][j], z1 = t->zf1[o][j], w = t->zu[o][j];
    double yc = t->yc[o][m], yc1 = t->yc1[o][m], v = t->yv[o][m];
    int a = p[X] + Y, aa = p[a] + Z, ab = p[a + 1] + Z;
    int b = p[X + 1] + Y, ba = p[b] + Z, bb = p[b + 1] + Z;
    double c0 = lerp(u, grad3_exact(p[aa], x, yc, z), grad3_exact(p[ba], x1, yc, z));
    double c1 = lerp(u, grad3_exact(p[ab], x, yc1, z), grad3_exact(p[bb], x1, yc1, z));
    double c2 = lerp(u, grad3_exact(p[aa + 1], x, yc, z1), grad3_exact(p[ba + 1], x1, yc, z1));
    double c3 = lerp(u, grad3_exact(p[ab + 1], x, yc1, z1), grad3_exact(p[bb + 1], x1, yc1, z1));
    double l1 = lerp(v, c0, c1);
    double l2 = lerp(v, c2, c3);
    return lerp(w, l1, l2) * inv;
}

__global__ void density(const struct cg_seed *seeds, const struct worldgen_req *req, int n,
                          const uint8_t *gen100, double *field_out, double *stone_out, uint8_t *ids_out)
{
    __shared__ int biomes[100];
    __shared__ double col_base[25], col_v[25];
    __shared__ double f[825];
    int k = blockIdx.x;
    if (k >= n) return;
    const struct cg_seed *s = &seeds[req[k].seed_index];
    int cx = req[k].cx, cz = req[k].cz;
    int x = cx * 4, z = cz * 4;
    for (int c = threadIdx.x; c < 100; c += blockDim.x) biomes[c] = gen100[(size_t)k * 100 + c];
    __syncthreads();

    /* func_147423_a per (i, j) column: the depth noise and the biome weights */
    if (threadIdx.x < 25)
    {
        int i = threadIdx.x / 5, j = threadIdx.x % 5;
        double depth = octaves_at(s->depth, 16, x, 10, z, i, 0, j, 1, 200.0, 1.0, 200.0);
        float var_sum = 0.0f, root_sum = 0.0f, weight_sum = 0.0f;
        const struct worldgen_biome *center = &d_biomes[biomes[i + 2 + (j + 2) * 10]];
        for (int a = -2; a <= 2; ++a)
            for (int b = -2; b <= 2; ++b)
            {
                const struct worldgen_biome *bi = &d_biomes[biomes[i + a + 2 + (j + b + 2) * 10]];
                float root = bi->root, var = bi->variation;
                float w = d_parabolic[a + 2 + (b + 2) * 5] / (root + 2.0f);
                if (bi->root > center->root) w /= 2.0f;
                var_sum += var * w;
                root_sum += root * w;
                weight_sum += w;
            }
        var_sum /= weight_sum;
        root_sum /= weight_sum;
        var_sum = var_sum * 0.9f + 0.1f;
        root_sum = (root_sum * 4.0f - 1.0f) / 8.0f;
        double d = depth / 8000.0;
        if (d < 0.0) d = -d * 0.3;
        d = d * 3.0 - 2.0;
        if (d < 0.0)
        {
            d /= 2.0;
            if (d < -1.0) d = -1.0;
            d /= 1.4;
            d /= 2.0;
        }
        else
        {
            if (d > 1.0) d = 1.0;
            d /= 8.0;
        }
        double r = (double)root_sum;
        double v = (double)var_sum;
        r += d * 0.2;
        r = r * 8.5 / 8.0;
        col_base[threadIdx.x] = 8.5 + r * 4.0;
        col_v[threadIdx.x] = v;
    }
    __syncthreads();

    double main_scale = c_negative == WORLDGEN_NEG_FIELD ? 8.55515 : 8.555150000000001;
    /* the main noise first (octaves_at's sum, in octave order); then each
     * limit noise only where the blend reads it (C computes both everywhere:
     * the density is the same), listed so a warp's lanes all run it */
    __shared__ struct field_tab tab;
    __shared__ double lim[2][825];
    __shared__ short todo[2][825];
    __shared__ int ntodo[2];
    if (threadIdx.x < 2) ntodo[threadIdx.x] = 0;
    field_tab_build(&tab, s->main, 8, x, z, main_scale, 4.277575000000001, main_scale);
    __syncthreads();
    for (int c = threadIdx.x; c < 825; c += blockDim.x)
    {
        int i = c / 165, j = c / 33 % 5, y = c % 33;
        double sum = 0.0, inv = 1.0;
        for (int o = 0; o < 8; ++o, inv *= 2.0) sum += field_sample(&tab, &s->main[o], o, i, j, y, inv);
        double m = (sum / 10.0 + 1.0) / 2.0;
        f[c] = m;
        if (m <= 1.0) todo[0][atomicAdd(&ntodo[0], 1)] = (short)c;
        if (m >= 0.0) todo[1][atomicAdd(&ntodo[1], 1)] = (short)c;
    }
    for (int hi = 0; hi < 2; ++hi)
    {
        const struct g_improved *g = hi ? s->max_limit : s->min_limit;
        __syncthreads();
        field_tab_build(&tab, g, 16, x, z, 684.412, 684.412, 684.412);
        __syncthreads();
        for (int t = threadIdx.x; t < ntodo[hi]; t += blockDim.x)
        {
            int c = todo[hi][t], i = c / 165, j = c / 33 % 5, y = c % 33;
            double sum = 0.0, inv = 1.0;
            for (int o = 0; o < 16; ++o, inv *= 2.0) sum += field_sample(&tab, &g[o], o, i, j, y, inv);
            lim[hi][c] = sum;
        }
    }
    __syncthreads();
    for (int c = threadIdx.x; c < 825; c += blockDim.x)
    {
        int i = c / 165, j = c / 33 % 5, y = c % 33;
        double m = f[c];
        double base = col_base[i * 5 + j], v = col_v[i * 5 + j];
        double fall = ((double)y - base) * 12.0 * 128.0 / 256.0 / v;
        if (fall < 0.0) fall *= 4.0;
        double l = m <= 1.0 ? lim[0][c] / 512.0 : 0.0;
        double h = m >= 0.0 ? lim[1][c] / 512.0 : 0.0;
        double dens = (m < 0.0 ? l : m > 1.0 ? h : l + (h - l) * m) - fall;
        if (y > 29)
        {
            double t = (double)((float)(y - 29) / 3.0f);
            dens = dens * (1.0 - t) + -10.0 * t;
        }
        f[c] = dens;
        if (field_out) field_out[(size_t)k * 825 + c] = dens;
    }

    /* the surface pass's noise: perlin_2d(stone, cx*16, cz*16, 16, 16, 0.0625, 0.0625, 1.0),
     * index z * 16 + x, octaves added in order */
    for (int c = threadIdx.x; c < 256; c += blockDim.x)
    {
        int i = c % 16, j = c / 16;
        double sum = 0.0, fall = 1.0, freq = 1.0;
        for (int o = 0; o < 4; ++o)
        {
            const struct g_simplex *g = &s->stone[o];
            double sx = 0.0625 * freq * fall, sz = 0.0625 * freq * fall, amp = 0.55 / fall;
            double xin = ((double)(cx * 16) + (double)i) * sx + g->xo;
            double yin = ((double)(cz * 16) + (double)j) * sz + g->yo;
            sum += 70.0 * simplex_sum(g, xin, yin) * amp;
            freq *= 1.0;
            fall *= 0.5;
        }
        stone_out[(size_t)k * 256 + c] = sum;
    }
    __syncthreads();

    /* func_147424_a: one thread per (i, j, y) cell block, C's incremental sums */
    uint8_t *blocks = ids_out + (size_t)k * CELLS;
    for (int c = threadIdx.x; c < 512; c += blockDim.x)
    {
        int i = c / 128, j = c / 32 % 4, y = c % 32;
        int a = i * 5, b = (i + 1) * 5;
        int c00 = (a + j) * 33, c01 = (a + j + 1) * 33, c10 = (b + j) * 33, c11 = (b + j + 1) * 33;
        double d00 = f[c00 + y], d01 = f[c01 + y], d10 = f[c10 + y], d11 = f[c11 + y];
        double s00 = (f[c00 + y + 1] - d00) * 0.125;
        double s01 = (f[c01 + y + 1] - d01) * 0.125;
        double s10 = (f[c10 + y + 1] - d10) * 0.125;
        double s11 = (f[c11 + y + 1] - d11) * 0.125;
        for (int yy = 0; yy < 8; ++yy)
        {
            double e0 = d00, e1 = d01;
            double sx0 = (d10 - d00) * 0.25, sx1 = (d11 - d01) * 0.25;
            for (int xx = 0; xx < 4; ++xx)
            {
                int idx = (xx + i * 4) << 12 | (0 + j * 4) << 8 | (y * 8 + yy);
                idx -= 256;
                double sz = (e1 - e0) * 0.25;
                double v = e0 - sz;
                for (int zz = 0; zz < 4; ++zz)
                {
                    idx += 256;
                    if ((v += sz) > 0.0) blocks[idx] = BLK_STONE;
                    else if (y * 8 + yy < 63) blocks[idx] = BLK_WATER;
                    else blocks[idx] = BLK_AIR;
                }
                e0 += sx0;
                e1 += sx1;
            }
            d00 += s00;
            d01 += s01;
            d10 += s10;
            d11 += s11;
        }
    }
}
