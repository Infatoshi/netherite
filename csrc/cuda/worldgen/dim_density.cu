/* dim_density: nether.c's and end.c's density_field (a block per chunk, a
 * thread per noise sample) and their trilinear fills (a thread per cell
 * block, C's incremental sums), and the three noises nether_surface reads.
 * The noise is octaves_at, noise.c's sample by sample. */
#include "dev.cuh"

/* nether.c's density_field for grid origin (x, 0, z), then nether_terrain's
 * fill. The sparse and wide noises vanilla computes feed only locals it never
 * reads (the y < below test cannot fire), so they are not computed here. */
__device__ static void nether_density(const struct cd_seed *s, int k, int cx, int cz, double *f, double *field_out,
                                      double *noise_out, uint8_t *blocks)
{
    int x = cx * 4, z = cz * 4;
    double main_sy = c_dneg == WORLDGEN_DIM_NEG_FIELD ? 2053.236 / 60.1 : 2053.236 / 60.0;
    for (int c = threadIdx.x; c < 425; c += blockDim.x)
    {
        int i = c / 85, j = c / 17 % 5, y = c % 17;
        double mn = octaves_at(s->n_main, 8, x, 0, z, i, y, j, 17, 684.412 / 80.0, main_sy, 684.412 / 80.0);
        double lo = octaves_at(s->n_lo, 16, x, 0, z, i, y, j, 17, 684.412, 2053.236, 684.412);
        double hi = octaves_at(s->n_hi, 16, x, 0, z, i, y, j, 17, 684.412, 2053.236, 684.412);
        double v = 0.0;
        double drop = c_curve[y];
        double a = lo / 512.0;
        double b = hi / 512.0;
        double m = (mn / 10.0 + 1.0) / 2.0;
        if (m < 0.0) v = a;
        else if (m > 1.0) v = b;
        else v = a + (b - a) * m;
        v -= drop;
        if (y > 17 - 4)
        {
            double t = (double)((float)(y - (17 - 4)) / 3.0f);
            v = v * (1.0 - t) + -10.0 * t;
        }
        f[c] = v;
        if (field_out) field_out[(size_t)k * 425 + c] = v;
    }

    /* nether_surface's three generateNoiseOctaves calls, noise index x * 16 + z:
     * slowsand 16x16x1 over (x, z) (the 3D branch, z as its y), gravel 16x1x16
     * at y 109 (the 2D branch), exclusivity like slowsand at half the scale */
    double *nz = noise_out + (size_t)k * 768;
    for (int c = threadIdx.x; c < 256; c += blockDim.x)
    {
        int bx = c / 16, bz = c % 16;
        nz[c] = octaves_at(s->n_sand, 4, cx * 16, cz * 16, 0, bx, bz, 0, 16, 0.03125, 0.03125, 1.0);
        nz[256 + c] = octaves_at(s->n_sand, 4, cx * 16, 109, cz * 16, bx, 0, bz, 1, 0.03125, 1.0, 0.03125);
        nz[512 + c] = octaves_at(s->n_excl, 4, cx * 16, cz * 16, 0, bx, bz, 0, 16, 0.0625, 0.0625, 0.0625);
    }
    __syncthreads();

    int lava_top = c_dneg == WORLDGEN_DIM_NEG_TERRAIN ? 31 : 32;
    for (int c = threadIdx.x; c < 256; c += blockDim.x)
    {
        int i = c / 64, j = c / 16 % 4, kk = c % 16;
        double e0 = f[((i + 0) * 5 + j + 0) * 17 + kk + 0];
        double e1 = f[((i + 0) * 5 + j + 1) * 17 + kk + 0];
        double e2 = f[((i + 1) * 5 + j + 0) * 17 + kk + 0];
        double e3 = f[((i + 1) * 5 + j + 1) * 17 + kk + 0];
        double d0 = (f[((i + 0) * 5 + j + 0) * 17 + kk + 1] - e0) * 0.125;
        double d1 = (f[((i + 0) * 5 + j + 1) * 17 + kk + 1] - e1) * 0.125;
        double d2 = (f[((i + 1) * 5 + j + 0) * 17 + kk + 1] - e2) * 0.125;
        double d3 = (f[((i + 1) * 5 + j + 1) * 17 + kk + 1] - e3) * 0.125;
        for (int yy = 0; yy < 8; ++yy)
        {
            double a0 = e0, a1 = e1;
            double sx0 = (e2 - e0) * 0.25, sx1 = (e3 - e1) * 0.25;
            for (int xx = 0; xx < 4; ++xx)
            {
                int idx = (xx + i * 4) << 11 | (0 + j * 4) << 7 | (kk * 8 + yy);
                double sz = (a1 - a0) * 0.25;
                double v = a0;
                for (int zz = 0; zz < 4; ++zz)
                {
                    uint8_t b = BLK_AIR;
                    if (kk * 8 + yy < lava_top) b = BLK_LAVA;
                    if (v > 0.0) b = BLK_NETHERRACK;
                    blocks[idx] = b;
                    idx += 128;
                    v += sz;
                }
                a0 += sx0;
                a1 += sx1;
            }
            e0 += d0;
            e1 += d1;
            e2 += d2;
            e3 += d3;
        }
    }
}

/* end.c's density_field for grid origin (x, 0, z), then end_terrain's fill.
 * The wide noise vanilla overwrites with 0.0 before use is not computed. */
__device__ static void end_density(const struct cd_seed *s, int k, int cx, int cz, double *f, double *field_out,
                                   uint8_t *blocks)
{
    __shared__ double col_above[9];
    __shared__ float col_edge[9];
    int x = cx * 2, z = cz * 2;
    if (threadIdx.x < 9)
    {
        int i = threadIdx.x / 3, j = threadIdx.x % 3;
        double island = octaves_at(s->e_island, 10, x, 10, z, i, 0, j, 1, 1.121, 1.0, 0.5);
        double above = (island + 256.0) / 512.0;
        if (above > 1.0) above = 1.0;
        if (above < 0.0) above = 0.0;
        above += 0.5;
        float fx = (float)(i + x);
        float fz = (float)(j + z);
        float edge_k = c_dneg == WORLDGEN_DIM_NEG_FIELD ? 8.01f : 8.0f;
        float edge = 100.0f - (float)sqrt((double)(fx * fx + fz * fz)) * edge_k;
        if (edge > 80.0f) edge = 80.0f;
        if (edge < -100.0f) edge = -100.0f;
        col_above[threadIdx.x] = above;
        col_edge[threadIdx.x] = edge;
    }
    __syncthreads();

    double scale = 684.412 * 2.0;
    for (int c = threadIdx.x; c < 297; c += blockDim.x)
    {
        int i = c / 99, j = c / 33 % 3, y = c % 33;
        double mn = octaves_at(s->e_main, 8, x, 0, z, i, y, j, 33, scale / 80.0, 684.412 / 160.0, scale / 80.0);
        double lo = octaves_at(s->e_lo, 16, x, 0, z, i, y, j, 33, scale, 684.412, scale);
        double hi = octaves_at(s->e_hi, 16, x, 0, z, i, y, j, 33, scale, 684.412, scale);
        double above = col_above[i * 3 + j];
        float edge = col_edge[i * 3 + j];
        double mid = 33.0 / 2.0;
        int half = 33 / 2;
        double v = 0.0;
        double drop = ((double)y - mid) * 8.0 / above;
        if (drop < 0.0) drop *= -1.0;
        double a = lo / 512.0;
        double b = hi / 512.0;
        double m = (mn / 10.0 + 1.0) / 2.0;
        if (m < 0.0) v = a;
        else if (m > 1.0) v = b;
        else v = a + (b - a) * m;
        v -= 8.0;
        v += (double)edge;
        int fade = 2;
        if (y > half - fade)
        {
            double t = (double)((float)(y - (half - fade)) / 64.0f);
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            v = v * (1.0 - t) + -3000.0 * t;
        }
        fade = 8;
        if (y < fade)
        {
            double t = (double)((float)(fade - y) / ((float)fade - 1.0f));
            v = v * (1.0 - t) + -30.0 * t;
        }
        f[c] = v;
        if (field_out) field_out[(size_t)k * 425 + c] = v;
    }
    __syncthreads();

    double ystep = c_dneg == WORLDGEN_DIM_NEG_TERRAIN ? 0.26 : 0.25;
    for (int c = threadIdx.x; c < 128; c += blockDim.x)
    {
        int i = c / 64, j = c / 32 % 2, kk = c % 32;
        double e0 = f[((i + 0) * 3 + j + 0) * 33 + kk + 0];
        double e1 = f[((i + 0) * 3 + j + 1) * 33 + kk + 0];
        double e2 = f[((i + 1) * 3 + j + 0) * 33 + kk + 0];
        double e3 = f[((i + 1) * 3 + j + 1) * 33 + kk + 0];
        double d0 = (f[((i + 0) * 3 + j + 0) * 33 + kk + 1] - e0) * ystep;
        double d1 = (f[((i + 0) * 3 + j + 1) * 33 + kk + 1] - e1) * ystep;
        double d2 = (f[((i + 1) * 3 + j + 0) * 33 + kk + 1] - e2) * ystep;
        double d3 = (f[((i + 1) * 3 + j + 1) * 33 + kk + 1] - e3) * ystep;
        for (int yy = 0; yy < 4; ++yy)
        {
            double a0 = e0, a1 = e1;
            double sx0 = (e2 - e0) * 0.125, sx1 = (e3 - e1) * 0.125;
            for (int xx = 0; xx < 8; ++xx)
            {
                int idx = (xx + i * 8) << 11 | (0 + j * 8) << 7 | (kk * 4 + yy);
                double sz = (a1 - a0) * 0.125;
                double v = a0;
                for (int zz = 0; zz < 8; ++zz)
                {
                    blocks[idx] = v > 0.0 ? BLK_END_STONE : BLK_AIR;
                    idx += 128;
                    v += sz;
                }
                a0 += sx0;
                a1 += sx1;
            }
            e0 += d0;
            e1 += d1;
            e2 += d2;
            e3 += d3;
        }
    }
}

__global__ void dim_density(const struct cd_seed *seeds, const struct worldgen_dim_req *req, int n, double *field_out,
                              double *noise_out, uint8_t *raw_out)
{
    __shared__ double f[425];
    int k = blockIdx.x;
    if (k >= n) return;
    const struct cd_seed *s = &seeds[req[k].seed_index];
    uint8_t *blocks = raw_out + (size_t)k * WORLDGEN_DIM_RAW;
    if (req[k].dim == -1) nether_density(s, k, req[k].cx, req[k].cz, f, field_out, noise_out, blocks);
    else end_density(s, k, req[k].cx, req[k].cz, f, field_out, blocks);
}
