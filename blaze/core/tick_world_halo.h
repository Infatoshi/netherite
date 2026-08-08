/* tick_world_halo: Wave 15+ - the living-world tick loop with CROSS-CHUNK fluid + light.
 *
 * tick_world_multi already composes multi-chunk gen + fluid CA + light fixpoint + block
 * tickers into one double-buffered ticking world, but every CA runs PER-CHUNK with SEALED
 * borders: fluid cannot leave the chunk it started in and a torch cannot light the neighbor
 * chunk. This driver reuses tick_world_multi's generation (real vanilla terrain via
 * cp_provide_chunk) and its TwmWorld double-buffered Chunk[] region, but ticks with the
 * HALO-AWARE variants so propagation crosses chunk boundaries:
 *
 *   - fluid:  ff_ca_step_halo   (whole-region contiguous slice; borders are interior cells)
 *   - light:  lp_propagate_halo (region-wide getRawLight fixpoint over the Chunk grid)
 *   - blocks: bth_tick_grid     (world-coordinate-keyed RNG + cross-border grass/fire spread)
 *
 * DETERMINISM: runtime randomness is the stateless hash RNG (SPEC rule 1) now keyed on the
 * GLOBAL world seed + WORLD coordinates (bth_*), so a cell's stream is identical regardless
 * of which chunk owns it - that is what makes cross-chunk ticking CPU==CUDA-safe. Nothing
 * reads its own tick's writes across the now/next boundary (rule 3). Every subsystem is a
 * pure function of (world_seed, tick, world-coords) -> CPU and CUDA agree bitwise by
 * construction. Float discipline (rule 4): none of these CAs use floats.
 *
 * CROSS-BOUNDARY FIXTURES (for evidence): a FLOWING_WATER source and a TORCH are placed on
 * the +x edge of the centre chunk; a stone floor + air fluid window and a 1-wide air light
 * tunnel span the centre chunk and its +x neighbour so fluid/light have somewhere to flow.
 * After ticking, the +x neighbour chunk's border cells (local x=0..) hold flowing water and
 * block light -> proof the CAs crossed the chunk boundary. Emitted as EVIDENCE lines.
 *
 * INTERNAL verify (CPU==CUDA bitwise): per tick dump {tick, all-chunk block FNV, all-chunk
 * light FNV}; then per-chunk combined hashes; then the boundary-evidence lines. READ-ONLY
 * deps: tick_world_multi.h (gen helpers + TwmWorld), fluid_flow.h/light_propagation.h/
 * block_tickers.h (the *_halo / bth_* variants). */
#ifndef MC_TICK_WORLD_HALO_H
#define MC_TICK_WORLD_HALO_H

/* This driver runs the cross-chunk fluid CA over real Chunk grids, so opt into
 * fluid_flow.h's Chunk-dependent halo helpers. MUST precede any include that pulls
 * fluid_flow.h (tick_world_multi.h does) so the helpers are compiled on first parse. */
#define MC_FLUID_HALO

#include "tick_world_multi.h"   /* TwmWorld, twm_chunk_coords, twc_copy_chunk, gen helpers */

#define TWH_DIM        TWM_DIM
#define TWH_RNX        (TWH_DIM * 16)
#define TWH_RNZ        (TWH_DIM * 16)

#ifndef TWH_NTICKS
#define TWH_NTICKS 24
#endif

#define TWH_FLUID_OY   62
#define TWH_FLUID_NY   4
#define TWH_FLUID_VOL  (TWH_RNX * TWH_FLUID_NY * TWH_RNZ)

#define TWH_LIGHT_VOL  (TWH_RNX * LP_NY * TWH_RNZ)
#define TWH_LIGHT_ITERS 128

#define TWH_BT_OY      58
#define TWH_BT_H       32

/* the cross-boundary fixture geometry (centre chunk, +x edge) */
#define TWH_TORCH_Y    50
#define TWH_WATER_Y    (TWH_FLUID_OY + 2)   /* 64: water rides the stone floor at 63 */
#define TWH_CHAN_Z     2

/* Per-env halo scratch: region fluid + region light buffers (kept off the CUDA stack). */
typedef struct {
    u16 fcur[TWH_FLUID_VOL];
    u16 ftmp[TWH_FLUID_VOL];
    u8  sky[TWH_LIGHT_VOL];
    u8  blk[TWH_LIGHT_VOL];
    u8  tsky[TWH_LIGHT_VOL];
    u8  tblk[TWH_LIGHT_VOL];
    u16 lblocks[TWH_LIGHT_VOL];
    u8  hm[TWH_RNX * TWH_RNZ];
} TwhScratch;

MC_HD static inline int twh_center_index(void) {
    int r = TWH_DIM / 2;
    return r * TWH_DIM + r;     /* grid (r,r) -> chunks[r*DIM+r]; +x neighbour is +1 */
}

/* Overlay the controlled cross-boundary fixtures onto real terrain in the centre chunk
 * (c0) and its +x neighbour (c1). Both chunks get a fluid window (stone floor + air) and a
 * 1-wide air light tunnel; the sources sit on the c0/+x edge (local x=15). */
MC_HD static inline void twh_overlay_fixtures(TwmWorld *w) {
    int ci = twh_center_index();
    Chunk *c0 = &w->a[ci];
    u16 air   = mc_state(FF_BLK_AIR, 0);
    u16 stone = mc_state(FF_BLK_STONE, 0);
    int x, z, i;
    /* Clear the fluid window (stone floor + air) region-wide so the ONLY liquid is the
     * controlled source - real-terrain lava/water in this thin y-slice would otherwise react
     * with / block the test channel and make the crossing seed-dependent. Carve the light
     * tunnel line on the centre chunk and its +x neighbour (torch light is local). */
    for (i = 0; i < TWM_NCHUNKS; ++i) {
        Chunk *c = &w->a[i];
        for (z = 0; z < 16; ++z)
            for (x = 0; x < 16; ++x) {
                mc_set(c, x, TWH_FLUID_OY + 0, z, air);     /* 62 */
                mc_set(c, x, TWH_FLUID_OY + 1, z, stone);   /* 63 floor */
                mc_set(c, x, TWH_FLUID_OY + 2, z, air);     /* 64 */
                mc_set(c, x, TWH_FLUID_OY + 3, z, air);     /* 65 */
            }
    }
    {
        Chunk *cc[2]; int k;
        cc[0] = c0; cc[1] = &w->a[ci + 1];
        for (k = 0; k < 2; ++k)
            for (x = 0; x < 16; ++x)
                mc_set(cc[k], x, TWH_TORCH_Y, TWH_CHAN_Z, air);  /* light tunnel line */
    }
    /* sources on the +x edge of the centre chunk (local x=15). */
    mc_set(c0, 15, TWH_WATER_Y, TWH_CHAN_Z, mc_state(FF_BLK_FLOWING_WATER, 0));
    mc_set(c0, 15, TWH_TORCH_Y, TWH_CHAN_Z, mc_state(LP_BLK_TORCH, 0));
    /* a sand column near the edge so the block tickers do visible work (falls each tick). */
    mc_set(c0, 14, TWH_BT_OY + 20, 5, mc_state(BLK_SAND, 0));
    mc_set(c0, 14, TWH_BT_OY + 19, 5, air);
    mc_set(c0, 14, TWH_BT_OY + 18, 5, air);
}

/* ---- generation: reuse tick_world_multi's terrain gen, then overlay halo fixtures ---- */
MC_HD static inline void twh_gen(TwmWorld *w, TwhScratch *s, ChunkPrimer *primer,
                                 CpScratch *sc, const McSinTable *st, u64 seed) {
    int i, x, z, j;
    w->cur = 0;
    w->tick = 0;
    w->seed = seed;
    for (i = 0; i < TWM_NCHUNKS; ++i) {
        Chunk *c = &w->a[i];
        int cx, cz;
        twm_chunk_coords(i, &cx, &cz);
        cp_provide_chunk(primer, sc, st, (i64)seed, cx, cz);
        te_load_primer_to_chunk(c, primer);
        c->cx = cx;
        c->cz = cz;
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                c->biome[z * MC_CX + x] = 1;
    }
    twh_overlay_fixtures(w);
    /* zero light, then settle it REGION-WIDE (halo) so tick 0 is cross-chunk consistent. */
    for (i = 0; i < TWM_NCHUNKS; ++i)
        for (j = 0; j < MC_CHUNK_VOL; ++j) w->a[i].light[j] = 0;
    lp_propagate_halo(w->a, TWH_DIM, s->sky, s->blk, s->tsky, s->tblk, s->lblocks, s->hm,
                      TWH_LIGHT_ITERS);
    for (i = 0; i < TWM_NCHUNKS; ++i) twc_copy_chunk(&w->b[i], &w->a[i]);
}

/* ---- one halo tick over the whole region (double-buffered) ---- */
MC_HD static inline void twh_tick(TwmWorld *w, TwhScratch *s) {
    Chunk *now = twm_now(w);
    Chunk *next = twm_next(w);
    i64 tick = w->tick;
    int i;

    for (i = 0; i < TWM_NCHUNKS; ++i) twc_copy_chunk(&next[i], &now[i]);

    /* 1) block tickers: read now, write next; WORLD-coordinate-keyed hash RNG (rule 1). */
    bth_tick_grid(now, next, TWH_DIM, w->seed, tick, TWH_BT_OY, TWH_BT_H);

    /* 2) fluid CA on the post-ticker next state; crosses chunk borders. */
    ff_ca_step_halo(next, next, TWH_DIM, TWH_FLUID_OY, TWH_FLUID_NY,
                    0, TWH_FLUID_OY, 0, s->fcur, s->ftmp);

    /* 3) light fixpoint on the post-fluid next state; crosses chunk borders. */
    lp_propagate_halo(next, TWH_DIM, s->sky, s->blk, s->tsky, s->tblk, s->lblocks, s->hm,
                      TWH_LIGHT_ITERS);

    w->tick = tick + 1;
    twm_swap(w);
}

/* ---- hashes (reuse tick_world_multi's whole-region + per-chunk FNV) ---- */
MC_HD static inline u64 twh_blocks_hash(const Chunk *chunks) { return twm_blocks_hash(chunks); }
MC_HD static inline u64 twh_light_hash(const Chunk *chunks)  { return twm_light_hash(chunks); }
MC_HD static inline u64 twh_chunk_hash(const Chunk *c)       { return twm_chunk_hash(c); }

typedef void (*TwhEmitFn)(u64 a, u64 b, u64 c, void *ctx);

/* Boundary evidence: at the +x neighbour chunk (local x=0..3) along the channel, report the
 * fluid block id (has water crossed?) and the block light (has light crossed?). k marks the
 * line (0xE00+k); these are deterministic, so they are diffed CPU==CUDA too. */
MC_HD static inline void twh_emit_evidence(const TwmWorld *w, TwhEmitFn emit, void *ctx) {
    int ci = twh_center_index();
    const Chunk *c1 = &twm_now((TwmWorld *)w)[ci + 1];
    int k;
    for (k = 0; k < 4; ++k) {
        u16 fs = mc_get(c1, k, TWH_WATER_Y, TWH_CHAN_Z);
        u8  ls = c1->light[mc_idx(k, TWH_TORCH_Y, TWH_CHAN_Z)];
        u64 fluid_id = (u64)mc_state_id(fs);
        u64 fluid_meta = (u64)mc_state_meta(fs);
        u64 blocklight = (u64)mc_light_block(ls);
        /* pack: a = 0xE00+k marker, b = (fluid_id<<8)|fluid_meta, c = block light. */
        if (emit) emit((u64)(0xE00 + k), (fluid_id << 8) | fluid_meta, blocklight, ctx);
    }
}

MC_HD static inline void twh_run(TwmWorld *w, TwhScratch *s, ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st, u64 seed, TwhEmitFn emit, void *ctx) {
    int t, i;
    twh_gen(w, s, primer, sc, st, seed);
    for (t = 0; t < TWH_NTICKS; ++t) {
        const Chunk *now;
        twh_tick(w, s);
        now = twm_now(w);
        if (emit) emit((u64)w->tick, twh_blocks_hash(now), twh_light_hash(now), ctx);
    }
    {
        const Chunk *now = twm_now(w);
        for (i = 0; i < TWM_NCHUNKS; ++i)
            if (emit) emit((u64)(u32)i, twh_chunk_hash(&now[i]), 0ULL, ctx);
    }
    twh_emit_evidence(w, emit, ctx);
}

#endif /* MC_TICK_WORLD_HALO_H */
