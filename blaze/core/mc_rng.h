/* mc_rng.h - two RNGs, both header-only and __host__ __device__.
 *
 *  1) JavaRandom: exact java.util.Random (48-bit LCG). Used ONLY for world generation, so a
 *     seed yields recognizable terrain. Stateful, call-order-dependent (that is intrinsic to
 *     worldgen and fine - worldgen is per-chunk sequential).
 *
 *  2) mc_hash_*: stateless hash RNG keyed on (tick, x, y, z, purpose). Used for ALL runtime
 *     sim randomness (mob spawn, drops, random ticks, AI). Order-independent => identical on
 *     CPU and CUDA regardless of thread scheduling. This is what makes the CPU==CUDA oracle
 *     hold. Tradeoff: runtime randomness will NOT match vanilla MC patterns (by design).
 */
#ifndef MC_RNG_H
#define MC_RNG_H

#include "mc.h"

/* ---------- java.util.Random ---------- */
#define MC_JR_MULT 0x5DEECE66DULL
#define MC_JR_ADD  0xBULL
#define MC_JR_MASK ((1ULL << 48) - 1)

typedef struct { u64 seed; } JavaRandom;

MC_HD static inline void jrand_set(JavaRandom *r, i64 seed) {
    r->seed = ((u64)seed ^ MC_JR_MULT) & MC_JR_MASK;
}
/* ---- population-cascade RNG-clobber watch (host-only replay instrumentation) ----
 * Vanilla's provideChunk reseeds the SHARED provider Random mid-populate when a
 * decorate-stage block touch generates a not-yet-loaded chunk (see DEVLOG). The
 * replay driver (world_dump --cascade) arms (before, after) cursor pairs mined from
 * the live game's GEN log lines: when the populate rand's state reaches `before`
 * (the state at the instant java entered provideChunk), jump it to `after` (the
 * state when control returned to the parent populate). Per-TU statics like the
 * probe hooks; only world/populate_mc.c ever arms them. Zero cost when unarmed. */
#ifndef __CUDA_ARCH__
#define MC_JR_WATCH_MAX 8
static int mc_jr_watch_n;
static u64 mc_jr_watch_before[MC_JR_WATCH_MAX], mc_jr_watch_after[MC_JR_WATCH_MAX];
static int mc_jr_watch_fired;
/* Optional nested-populate hook: called (with the matched `before` cursor) right
 * after a jump fires, i.e. at the exact stream position where vanilla generated
 * and cascade-populated a neighbor chunk. The hook places those blocks so the
 * parent's remaining block-conditional draws (jungle vines etc.) see them. */
static void (*mc_jr_watch_hookfn)(unsigned long long before);
#endif

MC_HD static inline i32 jrand_next(JavaRandom *r, int bits) {
#ifndef __CUDA_ARCH__
    if (mc_jr_watch_n && r->seed == mc_jr_watch_before[0]) {
        u64 matched = mc_jr_watch_before[0];
        r->seed = mc_jr_watch_after[0];
        for (int i = 1; i < mc_jr_watch_n; ++i) {
            mc_jr_watch_before[i - 1] = mc_jr_watch_before[i];
            mc_jr_watch_after[i - 1] = mc_jr_watch_after[i];
        }
        --mc_jr_watch_n;
        ++mc_jr_watch_fired;
        if (mc_jr_watch_hookfn) mc_jr_watch_hookfn((unsigned long long)matched);
    }
#endif
    r->seed = (r->seed * MC_JR_MULT + MC_JR_ADD) & MC_JR_MASK;
    return (i32)(r->seed >> (48 - bits));   /* arithmetic >> on signed cast, matches (int) */
}
MC_HD static inline i32 jrand_int(JavaRandom *r) { return jrand_next(r, 32); }

MC_HD static inline i32 jrand_int_bound(JavaRandom *r, i32 bound) {
    if ((bound & -bound) == bound)          /* power of two */
        return (i32)(((i64)bound * (i64)jrand_next(r, 31)) >> 31);
    i32 bits, val;
    do { bits = jrand_next(r, 31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
    return val;
}
MC_HD static inline i64 jrand_long(JavaRandom *r) {
    /* C does not sequence + operands; Java is strictly left-to-right. Use ordered temporaries. */
    i64 hi = (i64)jrand_next(r, 32);
    i64 lo = (i64)jrand_next(r, 32);
    return (hi << 32) + lo;
}
MC_HD static inline float jrand_float(JavaRandom *r) {
    return jrand_next(r, 24) / (float)(1 << 24);
}
MC_HD static inline double jrand_double(JavaRandom *r) {
    i64 hi = (i64)jrand_next(r, 26);
    i64 lo = (i64)jrand_next(r, 27);
    return ((hi << 27) + lo) * (1.0 / (double)(1LL << 53));
}

/* ---------- stateless hash RNG (runtime) ---------- */
/* SplitMix64 finalizer over a mix of the keys; deterministic, order-independent. */
MC_HD static inline u64 mc_hash64(u64 x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}
MC_HD static inline u64 mc_hash_seed(u64 world_seed, i64 tick, i32 x, i32 y, i32 z, u32 purpose) {
    u64 h = world_seed;
    h = mc_hash64(h ^ (u64)tick      * 0x9E3779B97F4A7C15ULL);
    h = mc_hash64(h ^ (u64)(u32)x    * 0xC2B2AE3D27D4EB4FULL);
    h = mc_hash64(h ^ (u64)(u32)y    * 0x165667B19E3779F9ULL);
    h = mc_hash64(h ^ (u64)(u32)z    * 0xD6E8FEB86659FD93ULL);
    h = mc_hash64(h ^ (u64)purpose   * 0x2545F4914F6CDD1DULL);
    return h;
}
/* uniform u32, float in [0,1), and bounded int from a hash seed */
MC_HD static inline u32   mc_hash_u32(u64 h)            { return (u32)(mc_hash64(h) >> 32); }
MC_HD static inline float mc_hash_f01(u64 h)            { return (mc_hash_u32(h) >> 8) * (1.0f / 16777216.0f); }
MC_HD static inline i32   mc_hash_bound(u64 h, i32 b)   { return (i32)(((u64)mc_hash_u32(h) * (u64)b) >> 32); }

/* ---- optional worldgen RNG-cursor probe (host-only flywheel instrumentation) ----
 * A host tool (magma/trace/world_dump with MAGMA_GENPROBE) sets mc_probe_fn; the
 * worldgen headers then emit a checkpoint (tag, type, raw 48-bit LCG state) at every
 * boundary where live Forge fires a terrain-gen event (qrl/WorldGenProbe.java logs the
 * same checkpoints from the REAL game). Diffing the two logs pinpoints the first stage
 * whose RNG stream diverges, per chunk. Zero behavior impact: the macro reads r->seed
 * only, and compiles out entirely on the CUDA device pass. Statics are per-TU; only
 * the TU that runs populate (world/populate_mc.c) sets them. */
#ifndef __CUDA_ARCH__
static void (*mc_probe_fn)(int cx, int cz, const char *tag, const char *type,
                           unsigned long long cursor);
static int mc_probe_cx, mc_probe_cz;
#define MC_PROBE(tag, type, r) \
    do { if (mc_probe_fn) mc_probe_fn(mc_probe_cx, mc_probe_cz, tag, type, \
                                      (unsigned long long)(r)->seed); } while (0)
/* Deferred chunkPos-corruption redirect (set by magma/world/populate_mc.c): a
 * cascaded nested populate overwrote the shared BiomeDecorator.chunkPos in vanilla,
 * but the feature in flight at clobber time keeps its absolute BlockPos - only the
 * NEXT chunkPos-relative position draw sees the corruption. The cascade hook arms
 * `pending`; decorate loops poll at each position-draw boundary and the apply fn
 * swaps the live World to the nested frame there. Same-TU statics as mc_probe_fn. */
struct World;
static void (*mc_redirect_apply)(struct World *);
static int mc_redirect_pending;
#define MC_REDIRECT_POLL(w) \
    do { if (mc_redirect_pending && mc_redirect_apply) mc_redirect_apply(w); } while (0)
/* decorate() sets chunkPos at entry, erasing any corruption armed before it */
#define MC_REDIRECT_CANCEL() do { mc_redirect_pending = 0; } while (0)
/* genDecorations is the ONLY chunkPos reader: biome decorate() extras (jungle
 * melon/vines etc.) and the populate tail use the uncorrupted pos PARAMETER, so
 * the frame swap is undone when genDecorations returns. */
static void (*mc_redirect_restore)(struct World *);
#define MC_REDIRECT_RESTORE(w) \
    do { mc_redirect_pending = 0; \
         if (mc_redirect_restore) mc_redirect_restore(w); } while (0)
#else
#define MC_PROBE(tag, type, r) do { } while (0)
#define MC_REDIRECT_POLL(w) do { } while (0)
#define MC_REDIRECT_CANCEL() do { } while (0)
#define MC_REDIRECT_RESTORE(w) do { } while (0)
#endif

#endif /* MC_RNG_H */
