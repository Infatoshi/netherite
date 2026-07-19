/* pal_fluid_parity: fluid CA bit-parity gate for the sectioned u8-palette layout.
 *
 * INTERNAL verify (CPU==CUDA), plus an in-run layout gate: the tick_fluid_ca
 * trajectory runs twice per tick - the existing dense-Chunk path (tfc_tick_env)
 * and a PalChunk mirror driven through pal_get/pal_set only - and every tick the
 * full 65536-cell volume must agree cell-for-cell and hash-for-hash. A palette /
 * section-pool drop or any accessor bug fails the same tick it happens.
 *
 * Emits 3 u64 per tick: dense chunk hash, pal chunk hash, mismatch count.
 * Drivers MUST exit nonzero when hashes differ or mismatches != 0 (a mismatch
 * identical on CPU and CUDA would otherwise slip through the bitwise diff).
 * READ-ONLY deps: tick_fluid_ca.h, pal_chunk.h. */
#ifndef MC_PAL_FLUID_PARITY_H
#define MC_PAL_FLUID_PARITY_H

#include "tick_fluid_ca.h"
#include "pal_chunk.h"

MC_HD static inline void pfp_extract_slice(const PalChunk *c, u16 *buf) {
    int x, y, z;
    for (y = 0; y < TFC_SLICE_NY; ++y)
        for (z = 0; z < TFC_SLICE_NZ; ++z)
            for (x = 0; x < TFC_SLICE_NX; ++x)
                ff_set(buf, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ, x, y, z,
                       pal_get(c, TFC_SLICE_OX + x, TFC_SLICE_OY + y, TFC_SLICE_OZ + z));
}

MC_HD static inline void pfp_merge_slice(PalChunk *c, const u16 *buf) {
    int x, y, z;
    for (y = 0; y < TFC_SLICE_NY; ++y)
        for (z = 0; z < TFC_SLICE_NZ; ++z)
            for (x = 0; x < TFC_SLICE_NX; ++x)
                pal_set(c, TFC_SLICE_OX + x, TFC_SLICE_OY + y, TFC_SLICE_OZ + z,
                        ff_get(buf, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ, x, y, z));
}

/* Single-chunk FNV in the same cell order as pal_blocks_hash. */
MC_HD static inline u64 pfp_dense_hash(const Chunk *c) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < MC_CHUNK_VOL; ++i) {
        h ^= (u64)c->blocks[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

typedef struct { u64 dense_hash, pal_hash, mismatches; } PfpLine;

MC_HD static inline void pfp_init(Env *e, PalChunk *pc, u64 seed) {
    tfc_init_env(e, seed);
    pal_from_dense(pc, &twc_now(e)->chunk[0]);
}

MC_HD static inline void pfp_tick(Env *e, PalChunk *pc,
                                  u16 *cur, u16 *tmp, u16 *pcur, u16 *ptmp,
                                  PfpLine *out) {
    const Chunk *dc;
    int x, y, z;
    tfc_tick_env(e, cur, tmp);                 /* dense path */
    pfp_extract_slice(pc, pcur);               /* pal mirror, same CA step */
    ff_ca_step(pcur, ptmp, TFC_SLICE_NX, TFC_SLICE_NY, TFC_SLICE_NZ);
    pfp_merge_slice(pc, ptmp);
    dc = &twc_now(e)->chunk[0];
    out->dense_hash = pfp_dense_hash(dc);
    out->pal_hash = pal_blocks_hash(pc);
    out->mismatches = 0;
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                if (mc_get(dc, x, y, z) != pal_get(pc, x, y, z)) out->mismatches++;
}

MC_HD static inline void pfp_run(Env *e, PalChunk *pc, u64 seed,
                                 u16 *cur, u16 *tmp, u16 *pcur, u16 *ptmp,
                                 PfpLine *lines /* TWC_NTICKS */) {
    int t;
    pfp_init(e, pc, seed);
    for (t = 0; t < TWC_NTICKS; ++t)
        pfp_tick(e, pc, cur, tmp, pcur, ptmp, &lines[t]);
}

#endif /* MC_PAL_FLUID_PARITY_H */
