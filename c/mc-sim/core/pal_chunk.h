/* pal_chunk: sectioned u8-palette chunk layout (SPEC "Batched-env state layout").
 *
 * POD, pointer-free (device-copyable). A chunk holds PAL_NSEC_SLOTS inline block
 * sections; a y-section with no slot is all air (section elision). Cells store u8
 * indices into a per-chunk append-only palette of packed u16 states (id<<4|meta);
 * pal[0] is always air. Sized from the MAGMA_STATE_PROF census (seed 0):
 * non-air sections mean 5.2 / max 6 of 16 (budget 8 covers Nether's 0-128 fill),
 * distinct states max 27/chunk (256 = 8x headroom).
 *
 * Accessors mirror mc_get/mc_set EXACTLY (same coords, same packed u16 in/out):
 * the layout swaps behind the accessor pair and every system re-verifies through
 * its existing CPU==CUDA gate. Prototype scope: blocks only (light/biome stay on
 * the dense twin until a light system is gated on this layout).
 *
 * Overflow policy: a full palette or section pool drops the write (returns 0).
 * The parity gates hash the full volume every tick, so a dropped write fails the
 * gate loudly instead of corrupting silently; product wiring should size caps so
 * drops cannot happen (caps.h philosophy). */
#ifndef MC_PAL_CHUNK_H
#define MC_PAL_CHUNK_H

#include "mc_world.h"

#define PAL_NSEC_SLOTS 8
#define PAL_NSTATES 256
#define PAL_SEC_VOL (MC_SEC * MC_COL_AREA)   /* 4096 cells per 16^3 section */

typedef struct {
    u8 idx[PAL_SEC_VOL];        /* palette indices; 0 = pal[0] = air */
} PalSection;

typedef struct {
    PalSection sec[PAL_NSEC_SLOTS];
    i16 slot_of[MC_NSEC];       /* y-section -> slot, -1 = all air */
    u16 pal[PAL_NSTATES];       /* idx -> packed state */
    i16 npal;
    i16 nslots;
    i32 cx, cz;
} PalChunk;

MC_HD static inline void pal_chunk_init(PalChunk *c, i32 cx, i32 cz) {
    int i;
    for (i = 0; i < MC_NSEC; ++i) c->slot_of[i] = -1;
    c->pal[0] = 0;              /* air */
    c->npal = 1;
    c->nslots = 0;
    c->cx = cx; c->cz = cz;
}

/* In-section linear index; same x/z order as mc_idx. */
MC_HD static inline int pal_sidx(int x, int y, int z) {
    return ((y & (MC_SEC - 1)) * MC_CZ + z) * MC_CX + x;
}

MC_HD static inline u16 pal_get(const PalChunk *c, int x, int y, int z) {
    int sl = c->slot_of[y >> 4];
    if (sl < 0) return 0;
    return c->pal[c->sec[sl].idx[pal_sidx(x, y, z)]];
}

MC_HD static inline void pal_set(PalChunk *c, int x, int y, int z, u16 s) {
    int sl = c->slot_of[y >> 4];
    int pi, i;
    if (sl < 0) {
        if (s == 0) return;                       /* air into all-air: no-op */
        if (c->nslots >= PAL_NSEC_SLOTS) return;  /* pool full: dropped, gate catches */
        sl = c->nslots++;
        for (i = 0; i < PAL_SEC_VOL; ++i) c->sec[sl].idx[i] = 0;
        c->slot_of[y >> 4] = (i16)sl;
    }
    pi = -1;
    for (i = 0; i < c->npal; ++i)
        if (c->pal[i] == s) { pi = i; break; }
    if (pi < 0) {
        if (c->npal >= PAL_NSTATES) return;       /* palette full: dropped, gate catches */
        pi = c->npal++;
        c->pal[pi] = s;
    }
    c->sec[sl].idx[pal_sidx(x, y, z)] = (u8)pi;
}

/* Dense <-> palette conversion (blocks only). */
MC_HD static inline void pal_from_dense(PalChunk *pc, const Chunk *c) {
    int x, y, z;
    pal_chunk_init(pc, c->cx, c->cz);
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                pal_set(pc, x, y, z, mc_get(c, x, y, z));
}

MC_HD static inline void pal_to_dense(const PalChunk *pc, Chunk *c) {
    int x, y, z;
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                mc_set(c, x, y, z, pal_get(pc, x, y, z));
}

/* FNV over the full logical volume in mc_idx order - matches twc_blocks_hash
 * cell order so a pal chunk and its dense twin hash identically. */
MC_HD static inline u64 pal_blocks_hash(const PalChunk *c) {
    u64 h = 0xcbf29ce484222325ULL;
    int x, y, z;
    for (y = 0; y < MC_CY; ++y)
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x) {
                h ^= (u64)pal_get(c, x, y, z);
                h *= 0x100000001b3ULL;
            }
    return h;
}

#endif /* MC_PAL_CHUNK_H */
