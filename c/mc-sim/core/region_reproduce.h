/* region_reproduce: PROVES the worldgen flywheel on top of the DONE/VERIFIED
 * core/region_tensor.h (rt_fill). Worldgen is a PURE function of (seed, chunk
 * coords), so rt_fill is pure in (seed, origin, dims) and a block at world
 * (x,y,z) is seed-only -- independent of how a request is TILED or where the
 * ORIGIN sits. This header runs four deterministic, element-wise checks over a
 * fixed test region and prints a byte-identical, diffable result. Both
 * cpu/region_reproduce.c and cuda/region_reproduce.cu include this and call
 * rr_run(), so CPU and CUDA execute IDENTICAL logic (SPEC internal-consistency
 * contract: same __host__ __device__ source, bitwise-equal stdout).
 *
 * Checks (all on seed 0, origin (-20,58,-20), dims 40 x 24 x 40):
 *   1. IDEMPOTENCE     - rt_fill the region K=4 times; all K bitwise identical.
 *   2. TILING INVARIANCE - whole region == 2x2 sub-tile reassembly, cell-for-cell.
 *   3. ORIGIN-SHIFT OVERLAP - region A at O and region B at O+(16,0,16) agree on
 *      every world cell in their overlap (a block is placement-independent).
 *   4. FINGERPRINT + samples - FNV-1a of the whole tensor + 16 sampled world
 *      cells, so runner.py's line diff validates CPU==CUDA element-agreement.
 *
 * The several region buffers are a few hundred KB -- caller passes them in as
 * scratch (host malloc on CPU, cudaMalloc on device), NEVER the device stack. */
#ifndef MC_REGION_REPRODUCE_H
#define MC_REGION_REPRODUCE_H

#include <stdio.h>
#include "region_tensor.h"   /* rt_fill, rt_count, rt_floordiv16 */

/* fixed test region (identical on CPU + CUDA). */
#define RR_SEED  ((u64)0)
#define RR_X0    (-20)
#define RR_Y0    (58)
#define RR_Z0    (-20)
#define RR_NX    (40)
#define RR_NY    (24)
#define RR_NZ    (40)
#define RR_N     ((long)RR_NX * RR_NY * RR_NZ)   /* 38400 */
#define RR_K     (4)
#define RR_DX    (16)   /* origin-shift for check 3 (partial overlap in world space) */
#define RR_DZ    (16)

/* All scratch buffers in one struct so the caller does ONE allocation
 * (host malloc / device cudaMalloc), keeping the big tensors off the stack. */
typedef struct {
    ChunkPrimer primer;   /* rt_fill work area (per-chunk primer) */
    CpScratch   sc;       /* rt_fill work area (heightmap noise) */
    u16 k[RR_K][RR_N];    /* K idempotence buffers; k[0] doubles as the canonical whole/region-A */
    u16 tiled[RR_N];      /* 2x2 sub-tile reassembly */
    u16 tile[RR_N];       /* one sub-tile at a time */
    u16 b[RR_N];          /* origin-shifted region B */
} RrScratch;

/* row-major index into a [nx][ny][nz] tensor. */
MC_HD static inline long rr_idx(int ix, int iy, int iz, int ny, int nz) {
    return ((long)(ix * ny) + iy) * nz + iz;
}

/* Run all four checks and print the byte-identical, diffable report. Pure. */
MC_HD static inline void rr_run(RrScratch *s, const McSinTable *st) {
    ChunkPrimer *primer = &s->primer;
    CpScratch   *sc     = &s->sc;
    u16 *whole = s->k[0];   /* the canonical whole-region tensor (also region A) */
    long i;
    int ix, iy, iz;

    /* ---- fill K copies of the whole region with IDENTICAL args ---- */
    for (int kk = 0; kk < RR_K; ++kk)
        rt_fill(s->k[kk], RR_SEED, RR_X0, RR_Y0, RR_Z0, RR_NX, RR_NY, RR_NZ, primer, sc, st);

    /* 1. IDEMPOTENCE: every copy bitwise-equal to k[0]. */
    {
        long bad = -1; int badk = -1;
        for (int kk = 1; kk < RR_K && bad < 0; ++kk)
            for (i = 0; i < RR_N; ++i)
                if (s->k[kk][i] != whole[i]) { bad = i; badk = kk; break; }
        if (bad < 0) printf("idempotent K=%d ok\n", RR_K);
        else printf("idempotent K=%d FAIL k[%d] idx %ld: %04x vs %04x\n",
                    RR_K, badk, bad, (unsigned)s->k[badk][bad], (unsigned)whole[bad]);
    }

    /* 2. TILING INVARIANCE: whole == 2x2 quadrant reassembly. */
    {
        int mx = RR_NX / 2, mz = RR_NZ / 2;
        for (i = 0; i < RR_N; ++i) s->tiled[i] = (u16)CB_AIR;
        for (int qi = 0; qi < 2; ++qi) {
            for (int qj = 0; qj < 2; ++qj) {
                int ax0 = qi ? mx : 0, ax1 = qi ? RR_NX : mx;
                int az0 = qj ? mz : 0, az1 = qj ? RR_NZ : mz;
                int qnx = ax1 - ax0, qnz = az1 - az0;
                rt_fill(s->tile, RR_SEED, RR_X0 + ax0, RR_Y0, RR_Z0 + az0,
                        qnx, RR_NY, qnz, primer, sc, st);
                for (ix = 0; ix < qnx; ++ix)
                    for (iy = 0; iy < RR_NY; ++iy)
                        for (iz = 0; iz < qnz; ++iz)
                            s->tiled[rr_idx(ax0 + ix, iy, az0 + iz, RR_NY, RR_NZ)] =
                                s->tile[rr_idx(ix, iy, iz, RR_NY, qnz)];
            }
        }
        long bad = -1;
        for (i = 0; i < RR_N; ++i)
            if (s->tiled[i] != whole[i]) { bad = i; break; }
        if (bad < 0) printf("tiling_invariant 2x2 ok\n");
        else printf("tiling_invariant 2x2 FAIL idx %ld: %04x vs %04x\n",
                    bad, (unsigned)s->tiled[bad], (unsigned)whole[bad]);
    }

    /* 3. ORIGIN-SHIFT OVERLAP: region B at O+(dx,0,dz) agrees with A on the overlap. */
    {
        int bx0 = RR_X0 + RR_DX, bz0 = RR_Z0 + RR_DZ;   /* B origin */
        rt_fill(s->b, RR_SEED, bx0, RR_Y0, bz0, RR_NX, RR_NY, RR_NZ, primer, sc, st);
        /* world-space overlap of A [x0,x0+nx) and B [bx0,bx0+nx) (y equal). */
        int ox_lo = (RR_X0 > bx0) ? RR_X0 : bx0;
        int ox_hi = (RR_X0 + RR_NX < bx0 + RR_NX) ? (RR_X0 + RR_NX) : (bx0 + RR_NX);
        int oz_lo = (RR_Z0 > bz0) ? RR_Z0 : bz0;
        int oz_hi = (RR_Z0 + RR_NZ < bz0 + RR_NZ) ? (RR_Z0 + RR_NZ) : (bz0 + RR_NZ);
        int wx, wy, wz, bad = 0, bx = 0, by = 0, bz = 0;
        for (wx = ox_lo; wx < ox_hi && !bad; ++wx)
            for (wy = RR_Y0; wy < RR_Y0 + RR_NY && !bad; ++wy)
                for (wz = oz_lo; wz < oz_hi && !bad; ++wz) {
                    u16 va = whole[rr_idx(wx - RR_X0, wy - RR_Y0, wz - RR_Z0, RR_NY, RR_NZ)];
                    u16 vb = s->b [rr_idx(wx - bx0,  wy - RR_Y0, wz - bz0,  RR_NY, RR_NZ)];
                    if (va != vb) { bad = 1; bx = wx; by = wy; bz = wz; }
                }
        if (!bad) printf("origin_shift_overlap ok\n");
        else printf("origin_shift_overlap FAIL world %d %d %d\n", bx, by, bz);
    }

    /* 4. FINGERPRINT (FNV-1a, endianness-independent byte order) + 16 sampled cells. */
    {
        u64 h = 1469598103934665603ULL;
        for (i = 0; i < RR_N; ++i) {
            unsigned v = whole[i];
            h ^= (u64)(v & 0xff);        h *= 1099511628211ULL;
            h ^= (u64)((v >> 8) & 0xff); h *= 1099511628211ULL;
        }
        printf("fingerprint %016llx\n", (unsigned long long)h);
        for (int sidx = 0; sidx < 16; ++sidx) {
            ix = (sidx * (RR_NX - 1)) / 15;
            iy = (sidx * (RR_NY - 1)) / 15;
            iz = ((15 - sidx) * (RR_NZ - 1)) / 15;
            printf("world %d %d %d = %04x\n",
                   RR_X0 + ix, RR_Y0 + iy, RR_Z0 + iz,
                   (unsigned)whole[rr_idx(ix, iy, iz, RR_NY, RR_NZ)]);
        }
    }
}

#endif /* MC_REGION_REPRODUCE_H */
