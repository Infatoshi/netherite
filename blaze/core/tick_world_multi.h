/* tick_world_multi: Wave 15 - the living-world tick loop over a PERSISTENT MULTI-CHUNK region.
 *
 * Composes the already-verified per-tick subsystems into one double-buffered ticking world built
 * from REAL generated terrain (chunk_provider) instead of a synthetic single chunk:
 *
 *   gen: for each chunk in a TWM_DIM x TWM_DIM region -> cp_provide_chunk (vanilla LCG terrain,
 *        biomes, surface, caves, ravines) -> te_load_primer_to_chunk -> zero light -> settle light.
 *   tick N -> N+1 (SPEC rule 3, double buffer; read now / write next; swap):
 *     1) copy now -> next (all chunks)
 *     2) block tickers  (trb_tick_slice + trb_random_attempts: grass/fire/falling + crops/ice/snow)
 *     3) fluid_flow CA  (ff_ca_step on the y=62..65 slice, per chunk)
 *     4) light fixpoint (lp_propagate on the y=0..63 slice, per chunk)
 *
 * DETERMINISM: runtime randomness is the stateless hash RNG (SPEC rule 1) keyed per-chunk-seed +
 * local coords; nothing reads its own tick's writes across the now/next boundary (rule 3). Every
 * subsystem is a pure function of (chunk_seed, tick, coords) -> CPU and CUDA agree bitwise by
 * construction, in any thread order. Float discipline (rule 4): -ffp-contract=off / --fmad=false.
 *
 * SCOPE / what is composed here vs still isolated:
 *   COMPOSED: multi-chunk gen + fluid CA + light fixpoint + block tickers, all double-buffered.
 *   PER-CHUNK, SEALED BOUNDARIES: fluid/light/tickers run on each chunk independently; propagation
 *     does NOT cross chunk borders yet (that is the next wiring step - see WORKQUEUE + CLOSURE note).
 *   NOT WIRED HERE: entities/mob spawning (single-chunk-scoped in tick_compose_full; cross-chunk
 *     entity movement is a separate increment).
 *
 * INTERNAL verify (CPU==CUDA bitwise): per tick dump {tick, all-chunks block FNV hash, all-chunks
 * light FNV hash}; after the loop, per-chunk combined(block^light) hashes for spatial coverage.
 * READ-ONLY deps: chunk_provider.h, tick_entities.h (te_load_primer_to_chunk), tick_random_block.h,
 * fluid_flow.h, light_propagation.h. */
#ifndef MC_TICK_WORLD_MULTI_H
#define MC_TICK_WORLD_MULTI_H

/* tick_entities pulls tick_world_copy first (sets MC_WORLD_R 0 cleanly + mc_world/Chunk/mc_blocks),
 * and gives te_load_primer_to_chunk. The R=0 World/Env types are unused: this file defines its own
 * multi-chunk world as a flat Chunk[] array (Chunk is R-independent). */
#include "tick_entities.h"
#include "tick_random_block.h"
#include "chunk_provider.h"
#include "fluid_flow.h"
#include "light_propagation.h"

#ifndef TWM_DIM
#define TWM_DIM 3                              /* TWM_DIM x TWM_DIM chunks (3 -> 9 chunks, 48x256x48) */
#endif
#define TWM_NCHUNKS (TWM_DIM * TWM_DIM)

#ifndef TWM_NTICKS
#define TWM_NTICKS 32
#endif

/* fluid slice: chunk-local 16x4x16 at y=62..65 (no cross-chunk border; sealed boundary) */
#define TWM_FLUID_OY 62
#define TWM_FLUID_NX 16
#define TWM_FLUID_NY 4
#define TWM_FLUID_NZ 16
#define TWM_FLUID_VOL (TWM_FLUID_NX * TWM_FLUID_NY * TWM_FLUID_NZ)

#define TWM_LIGHT_ITERS 128

typedef struct {
    Chunk a[TWM_NCHUNKS];
    Chunk b[TWM_NCHUNKS];
    int   cur;                                 /* 0 -> now=a,next=b ; 1 -> swapped */
    i64   tick;
    u64   seed;
} TwmWorld;

/* Per-env scratch (kept off the CUDA stack; one env ticks serially reusing it). One persistent
 * TrbAux per chunk holds that chunk's block-ticker light context (set once at gen from the overlaid
 * fixtures, mirroring tick_compose_full - block tickers read a stable light context, not re-synced
 * mid-run, so the verified per-tick evolution is reproduced deterministically). */
typedef struct {
    TrbAux trb[TWM_NCHUNKS];
    u16    fcur[TWM_FLUID_VOL];
    u16    ftmp[TWM_FLUID_VOL];
    u16    lblocks[LP_VOL];
    u8     sky[LP_VOL];
    u8     blk[LP_VOL];
    u8     tsky[LP_VOL];
    u8     tblk[LP_VOL];
} TwmScratch;

MC_HD static inline Chunk *twm_now(TwmWorld *w)  { return w->cur ? w->b : w->a; }
MC_HD static inline Chunk *twm_next(TwmWorld *w) { return w->cur ? w->a : w->b; }
MC_HD static inline void   twm_swap(TwmWorld *w) { w->cur ^= 1; }

/* Chunk grid coords centered on origin: index i -> (cx,cz) in [-R..R]. */
MC_HD static inline void twm_chunk_coords(int i, int *cx, int *cz) {
    int r = TWM_DIM / 2;
    *cx = (i % TWM_DIM) - r;
    *cz = (i / TWM_DIM) - r;
}

/* Namespaces each chunk's runtime hash-RNG stream by world seed + chunk coords. Still a pure
 * stateless function -> CPU==CUDA holds. (Proper global-coordinate keying is the next step.) */
MC_HD static inline u64 twm_chunk_seed(u64 seed, int cx, int cz) {
    u64 h = mc_hash64(seed ^ ((u64)(u32)cx * 0x9E3779B97F4A7C15ULL));
    return mc_hash64(h ^ ((u64)(u32)cz * 0xC2B2AE3D27D4EB4FULL));
}

/* ---- per-chunk slice extract/merge (Chunk*, chunk-local, sealed) ---- */
MC_HD static inline void twm_extract_fluid(const Chunk *c, u16 *buf) {
    int x, y, z;
    for (y = 0; y < TWM_FLUID_NY; ++y)
        for (z = 0; z < TWM_FLUID_NZ; ++z)
            for (x = 0; x < TWM_FLUID_NX; ++x)
                ff_set(buf, TWM_FLUID_NX, TWM_FLUID_NY, TWM_FLUID_NZ, x, y, z,
                       mc_get(c, x, TWM_FLUID_OY + y, z));
}
MC_HD static inline void twm_merge_fluid(Chunk *c, const u16 *buf) {
    int x, y, z;
    for (y = 0; y < TWM_FLUID_NY; ++y)
        for (z = 0; z < TWM_FLUID_NZ; ++z)
            for (x = 0; x < TWM_FLUID_NX; ++x)
                mc_set(c, x, TWM_FLUID_OY + y, z,
                       ff_get(buf, TWM_FLUID_NX, TWM_FLUID_NY, TWM_FLUID_NZ, x, y, z));
}

MC_HD static inline void twm_extract_light(const Chunk *c, u8 *sky, u8 *blk, u16 *blocks) {
    int x, y, z;
    for (y = 0; y < LP_NY; ++y)
        for (z = 0; z < LP_NZ; ++z)
            for (x = 0; x < LP_NX; ++x) {
                int wi = mc_idx(x, y, z);
                int li = lp_idx(x, y, z);
                u8 packed = c->light[wi];
                sky[li] = (u8)mc_light_sky(packed);
                blk[li] = (u8)mc_light_block(packed);
                blocks[li] = mc_get(c, x, y, z);
            }
}
MC_HD static inline void twm_merge_light(Chunk *c, const u8 *sky, const u8 *blk) {
    int x, y, z;
    for (y = 0; y < LP_NY; ++y)
        for (z = 0; z < LP_NZ; ++z)
            for (x = 0; x < LP_NX; ++x)
                c->light[mc_idx(x, y, z)] = mc_light(sky[lp_idx(x, y, z)], blk[lp_idx(x, y, z)]);
}

/* Overlay a fluid source (lava under water in an air pocket over a stone floor) into the y=62..65
 * slice - the verified disturbance that makes the fluid CA actually flow. Mirrors tfc_init_fluids
 * on a chunk-local 16-wide slice. Keyed per chunk seed so each chunk's source sits differently. */
MC_HD static inline void twm_seed_fluid(Chunk *c, u64 cseed) {
    u16 stone = mc_state(FF_BLK_STONE, 0);
    u16 air = mc_state(FF_BLK_AIR, 0);
    int x, z;
    for (z = 0; z < TWM_FLUID_NZ; ++z)
        for (x = 0; x < TWM_FLUID_NX; ++x) {
            mc_set(c, x, TWM_FLUID_OY + 0, z, air);
            mc_set(c, x, TWM_FLUID_OY + 1, z, stone);
            mc_set(c, x, TWM_FLUID_OY + 2, z, air);
            mc_set(c, x, TWM_FLUID_OY + 3, z, air);
        }
    {
        u64 hv = mc_hash_seed(cseed, 1, 0, 0, 0, 2);
        int sx = 4 + (int)mc_hash_bound(hv, TWM_FLUID_NX - 8);
        int sz;
        hv = mc_hash64(hv + 1ULL);
        sz = 4 + (int)mc_hash_bound(hv, TWM_FLUID_NZ - 8);
        mc_set(c, sx, TWM_FLUID_OY + 1, sz, mc_state(FF_BLK_LAVA, 0));
        mc_set(c, sx, TWM_FLUID_OY + 2, sz, mc_state(FF_BLK_WATER, 0));
    }
}

MC_HD static inline void twm_settle_light(Chunk *c, TwmScratch *s) {
    int i;
    for (i = 0; i < MC_CHUNK_VOL; ++i) c->light[i] = 0;
    twm_extract_light(c, s->sky, s->blk, s->lblocks);
    lp_propagate(s->sky, s->blk, s->tsky, s->tblk, s->lblocks, TWM_LIGHT_ITERS);
    twm_merge_light(c, s->sky, s->blk);
}

/* ---- generation: fill both buffers with the persistent multi-chunk world ---- */
MC_HD static inline void twm_gen(TwmWorld *w, TwmScratch *s, ChunkPrimer *primer,
                                 CpScratch *sc, const McSinTable *st, u64 seed) {
    int i, x, z;
    w->cur = 0;
    w->tick = 0;
    w->seed = seed;
    for (i = 0; i < TWM_NCHUNKS; ++i) {
        Chunk *c = &w->a[i];
        int cx, cz;
        twm_chunk_coords(i, &cx, &cz);
        u64 cseed = twm_chunk_seed(seed, cx, cz);
        cp_provide_chunk(primer, sc, st, (i64)seed, cx, cz);
        te_load_primer_to_chunk(c, primer);
        c->cx = cx;
        c->cz = cz;
        for (z = 0; z < MC_CZ; ++z)
            for (x = 0; x < MC_CX; ++x)
                c->biome[z * MC_CX + x] = 1;
        /* overlay the verified block-ticker + fluid disturbance fixtures onto the real terrain so
         * every wired subsystem is genuinely exercised each tick (sets this chunk's TrbAux). */
        trb_init_fixtures(c, &s->trb[i], cseed);
        twm_seed_fluid(c, cseed);
        twm_settle_light(c, s);
    }
    /* mirror into buffer b so a tick's baseline copy is well-defined from either 'cur'. */
    for (i = 0; i < TWM_NCHUNKS; ++i) twc_copy_chunk(&w->b[i], &w->a[i]);
}

/* ---- one tick over the whole region ---- */
MC_HD static inline void twm_tick(TwmWorld *w, TwmScratch *s) {
    Chunk *now = twm_now(w);
    Chunk *next = twm_next(w);
    i64 tick = w->tick;
    int i;

    for (i = 0; i < TWM_NCHUNKS; ++i) twc_copy_chunk(&next[i], &now[i]);

    for (i = 0; i < TWM_NCHUNKS; ++i) {
        Chunk *cn = &now[i];
        Chunk *cx = &next[i];
        u64 cseed = twm_chunk_seed(w->seed, cn->cx, cn->cz);

        /* 2) block tickers (read now blocks + persistent per-chunk aux, write next) */
        trb_tick_slice(cn, cx, &s->trb[i], cseed, tick);
        trb_random_attempts(cn, cx, cseed, tick);

        /* 3) fluid CA on next (post-ticker state) */
        twm_extract_fluid(cx, s->fcur);
        ff_ca_step(s->fcur, s->ftmp, TWM_FLUID_NX, TWM_FLUID_NY, TWM_FLUID_NZ);
        twm_merge_fluid(cx, s->ftmp);

        /* 4) light fixpoint on next */
        twm_extract_light(cx, s->sky, s->blk, s->lblocks);
        lp_propagate(s->sky, s->blk, s->tsky, s->tblk, s->lblocks, TWM_LIGHT_ITERS);
        twm_merge_light(cx, s->sky, s->blk);
    }

    w->tick = tick + 1;
    twm_swap(w);
}

/* ---- hashes over the whole ticked region ---- */
MC_HD static inline u64 twm_blocks_hash(const Chunk *chunks) {
    u64 h = 0xcbf29ce484222325ULL;
    int ci, i;
    for (ci = 0; ci < TWM_NCHUNKS; ++ci)
        for (i = 0; i < MC_CHUNK_VOL; ++i) { h ^= (u64)chunks[ci].blocks[i]; h *= 0x100000001b3ULL; }
    return h;
}
MC_HD static inline u64 twm_light_hash(const Chunk *chunks) {
    u64 h = 0xcbf29ce484222325ULL;
    int ci, i;
    for (ci = 0; ci < TWM_NCHUNKS; ++ci)
        for (i = 0; i < MC_CHUNK_VOL; ++i) { h ^= (u64)chunks[ci].light[i]; h *= 0x100000001b3ULL; }
    return h;
}
MC_HD static inline u64 twm_chunk_hash(const Chunk *c) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < MC_CHUNK_VOL; ++i) { h ^= (u64)c->blocks[i]; h *= 0x100000001b3ULL; }
    for (i = 0; i < MC_CHUNK_VOL; ++i) { h ^= (u64)c->light[i];  h *= 0x100000001b3ULL; }
    return h;
}

typedef void (*TwmEmitFn)(u64 tick_bits, u64 blocks_hash, u64 light_hash, void *ctx);

MC_HD static inline void twm_run(TwmWorld *w, TwmScratch *s, ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st, u64 seed, TwmEmitFn emit, void *ctx) {
    int t, i;
    twm_gen(w, s, primer, sc, st, seed);
    for (t = 0; t < TWM_NTICKS; ++t) {
        const Chunk *now;
        twm_tick(w, s);
        now = twm_now(w);
        if (emit) emit((u64)w->tick, twm_blocks_hash(now), twm_light_hash(now), ctx);
    }
    /* spatial coverage: per-chunk final combined hash (tick_bits = chunk index, light_hash = 0). */
    {
        const Chunk *now = twm_now(w);
        for (i = 0; i < TWM_NCHUNKS; ++i)
            if (emit) emit((u64)(u32)i, twm_chunk_hash(&now[i]), 0ULL, ctx);
    }
}

#endif /* MC_TICK_WORLD_MULTI_H */
