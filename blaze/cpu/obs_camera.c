/* CPU reference driver for obs_camera. For each seed a dense region tensor is
 * generated with rt_fill (Java-golden-anchored worldgen, pure in seed), then
 * OC frames are rendered from a fixed eye over a pose sweep. Dumps every
 * pixel: OC_NPIX id lines (%04x), OC_NPIX depth lines (%02x), then OC_NPIX
 * edge lines (%d) per frame, for a bitwise diff with cuda/obs_camera.cu. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/region_tensor.h"
#include "../core/obs_camera.h"

#define RG_X0 (-48)
#define RG_Y0 28
#define RG_Z0 (-48)
#define RG_NX 113
#define RG_NY 104
#define RG_NZ 113

static const float k_poses[][2] = {
    {180.0f, 0.0f}, {0.0f, 0.0f}, {90.0f, 15.0f},
    {270.0f, -30.0f}, {45.0f, 60.0f}, {200.0f, -75.0f},
};
#define NPOSES (int)(sizeof k_poses / sizeof k_poses[0])

int main(int argc, char **argv) {
    static const i64 k_seeds[] = {0LL, 3LL, 12345LL};
    int n_seeds = (argc > 1) ? 1 : 3;
    int si, pi, i;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    u16 *cells = (u16 *)malloc((size_t)rt_count(RG_NX, RG_NY, RG_NZ) * sizeof(u16));
    u16 *ids = (u16 *)malloc(OC_NPIX * sizeof(u16));
    u8 *depth = (u8 *)malloc(OC_NPIX);
    u8 *edge = (u8 *)malloc(OC_NPIX);
    mc_sin_table_init(st);

    for (si = 0; si < n_seeds; ++si) {
        i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : k_seeds[si];
        OcRegion rg;
        rt_fill(cells, (u64)seed, RG_X0, RG_Y0, RG_Z0, RG_NX, RG_NY, RG_NZ,
                primer, sc, st);
        rg.cells = cells;
        rg.x0 = RG_X0; rg.y0 = RG_Y0; rg.z0 = RG_Z0;
        rg.nx = RG_NX; rg.ny = RG_NY; rg.nz = RG_NZ;

        printf("obs_camera seed=%lld poses=%d %dx%d\n",
               (long long)seed, NPOSES, OC_W, OC_H);
        for (pi = 0; pi < NPOSES; ++pi) {
            oc_render(&rg, st, 8.5, 80.0, 8.5,
                      k_poses[pi][0], k_poses[pi][1], ids, depth, edge);
            for (i = 0; i < OC_NPIX; ++i) printf("%04x\n", (unsigned)ids[i]);
            for (i = 0; i < OC_NPIX; ++i) printf("%02x\n", (unsigned)depth[i]);
            for (i = 0; i < OC_NPIX; ++i) printf("%d\n", (int)edge[i]);
        }
    }

    free(edge); free(depth); free(ids); free(cells); free(sc); free(primer); free(st);
    return 0;
}
