/* CPU reference: materialize a DENSE u16 block tensor over an arbitrary world-space AABB
 * (seed, origin, dims) via rt_fill (tiles cp_provide_chunk over the covered chunks), then dump
 * a header line + EVERY element as %04x in index order, for a bitwise diff with cuda/region_tensor.cu.
 * Layout: out[(ix*ny+iy)*nz+iz] = block id at world (x0+ix, y0+iy, z0+iz). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/region_tensor.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 0LL;
    int x0 = (argc > 2) ? (int)strtol(argv[2], 0, 10) : -16;
    int y0 = (argc > 3) ? (int)strtol(argv[3], 0, 10) : 60;
    int z0 = (argc > 4) ? (int)strtol(argv[4], 0, 10) : -16;
    int nx = (argc > 5) ? (int)strtol(argv[5], 0, 10) : 32;
    int ny = (argc > 6) ? (int)strtol(argv[6], 0, 10) : 24;
    int nz = (argc > 7) ? (int)strtol(argv[7], 0, 10) : 32;

    long total = rt_count(nx, ny, nz);

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    u16 *out = (u16 *)malloc((size_t)total * sizeof(u16));

    rt_fill(out, (u64)seed, x0, y0, z0, nx, ny, nz, primer, sc, st);

    printf("region seed=%lld x0=%d y0=%d z0=%d nx=%d ny=%d nz=%d\n",
           (long long)seed, x0, y0, z0, nx, ny, nz);
    for (long i = 0; i < total; ++i)
        printf("%04x\n", (unsigned)out[i]);

    free(out); free(sc); free(primer); free(st);
    return 0;
}
