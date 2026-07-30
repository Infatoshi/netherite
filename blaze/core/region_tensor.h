/* region_tensor: materialize a DENSE block tensor over an arbitrary world-space AABB
 * from (seed, origin), by tiling the Java-golden-anchored base-terrain generator
 * (cp_provide_chunk) over the covered chunks and copying the overlapping cells.
 *
 * This is the worldgen "flywheel" primitive: worldgen is a PURE function of (seed,
 * chunk coords) replicating MC's 48-bit LCG (SPEC rule 2), so rt_fill is a pure
 * function of (seed, x0,y0,z0, nx,ny,nz) -- the SAME args yield a BITWISE-identical
 * tensor every time, on CPU and CUDA, and a block at world (x,y,z) is seed-only
 * (independent of how the request is tiled or where the origin sits). The oracle
 * drivers built on this dump the FULL tensor so runner.py's exact line diff is a
 * literal ELEMENT-WISE check, not a hash.
 *
 * Vanilla faithfulness inherits from cp_provide_chunk's verbatim-Java golden
 * (oracle/goldens/chunk_provider): rt_fill only tiles + copies those verified chunks.
 * Base terrain + caves/ravines (NOT structures/populate/skylight -- that is
 * overworld_full, a later tensor extension).
 *
 * Layout: out is row-major [nx][ny][nz] u16 block ids,
 *   out[(ix*ny + iy)*nz + iz] = block id at world (x0+ix, y0+iy, z0+iz).
 * y outside [0,255] is CB_AIR. Caller supplies primer/sc scratch (off the device
 * stack; cp_provide_chunk is heavy and MC_NOINLINE). */
#ifndef MC_REGION_TENSOR_H
#define MC_REGION_TENSOR_H

#include "chunk_provider.h"   /* cp_provide_chunk, ChunkPrimer, CpScratch, cb_get, CB_AIR */

/* floor-division of a block coord to its chunk coord (handles negatives). */
MC_HD static inline int rt_floordiv16(int v) {
    return (v >= 0) ? (v >> 4) : -(((-v) + 15) >> 4);
}

/* Number of u16 elements a [nx,ny,nz] tensor needs. */
MC_HD static inline long rt_count(int nx, int ny, int nz) {
    return (long)nx * (long)ny * (long)nz;
}

/* Fill `out` (rt_count(nx,ny,nz) u16s) with the dense block tensor. Each covered
 * chunk is generated exactly once. Pure in (seed,x0,y0,z0,nx,ny,nz). */
MC_HD static inline void rt_fill(u16 *out, u64 seed, int x0, int y0, int z0,
                                 int nx, int ny, int nz,
                                 ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st) {
    long i;
    long total = rt_count(nx, ny, nz);
    for (i = 0; i < total; ++i) out[i] = (u16)CB_AIR;   /* AIR default (y gaps, out-of-range) */

    {
        int cx0 = rt_floordiv16(x0),        cz0 = rt_floordiv16(z0);
        int cx1 = rt_floordiv16(x0 + nx - 1), cz1 = rt_floordiv16(z0 + nz - 1);
        int cx, cz;
        for (cx = cx0; cx <= cx1; ++cx) {
            for (cz = cz0; cz <= cz1; ++cz) {
                int bx = cx * 16, bz = cz * 16;
                int wx_lo = (bx > x0) ? bx : x0, wx_hi = ((bx + 16) < (x0 + nx)) ? (bx + 16) : (x0 + nx);
                int wz_lo = (bz > z0) ? bz : z0, wz_hi = ((bz + 16) < (z0 + nz)) ? (bz + 16) : (z0 + nz);
                int wx, wz, iy;
                cp_provide_chunk(primer, sc, st, (i64)seed, cx, cz);
                for (wx = wx_lo; wx < wx_hi; ++wx) {
                    int lx = wx - bx, ix = wx - x0;
                    for (wz = wz_lo; wz < wz_hi; ++wz) {
                        int lz = wz - bz, iz = wz - z0;
                        for (iy = 0; iy < ny; ++iy) {
                            int wy = y0 + iy;
                            if (wy < 0 || wy > 255) continue;
                            out[((long)(ix * ny) + iy) * nz + iz] = (u16)cb_get(primer, lx, wy, lz);
                        }
                    }
                }
            }
        }
    }
}

#endif /* MC_REGION_TENSOR_H */
