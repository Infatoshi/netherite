/* gen_prefetch.h - single background worker pregenerating overworld BASE
 * terrain (st_run_features) ahead of the player. See gen_prefetch.c for the
 * thread-safety-by-translation-unit contract. light.c references these as
 * WEAK symbols so unit-test binaries that link light.o alone still build
 * (prefetch simply off there). */
#ifndef MAGMA_GEN_PREFETCH_H
#define MAGMA_GEN_PREFETCH_H

/* Start (or re-seed) the worker. radius = prefetch ring radius in chunks;
 * the slot table is (2*radius+1)^2, sized to the light pool geometry.
 * No-op if the registry key no_prefetch is set. */
void genpf_start(long long seed, int radius);

/* Main thread: the player's current chunk; the worker spirals outward from
 * here. Cheap (two atomic stores), call every ensure. */
void genpf_hint(int ccx, int ccz);

/* Main thread: if the worker has chunk (cx,cz) READY (or already CONSUMED -
 * the bytes stay valid until the slot is reclaimed for a different chunk),
 * copy its 65536 u16 block ids into `out` and return 1; else return 0
 * (caller generates synchronously - same bytes either way). */
int genpf_take(int cx, int cz, unsigned short *out);

/* 1 if the worker is running for exactly this seed. Callers outside light.c
 * (populate_mc's base-chunk memo) must gate takes on this: the worker is
 * keyed (cx,cz) only, so a seed mismatch would hand back wrong-world bytes. */
int genpf_active(long long seed);

/* Stop and join the worker, free the pool. */
void genpf_stop(void);

#endif /* MAGMA_GEN_PREFETCH_H */
