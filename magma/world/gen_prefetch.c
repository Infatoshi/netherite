/* gen_prefetch.c - ONE background worker pregenerating overworld BASE terrain
 * (st_run_features: cp_provide_chunk + the stronghold live subset) ahead of
 * the player, so gen_chunk on the main thread usually memcpys a finished
 * 128KB primer instead of ray-marching noise. Base terrain is a pure function
 * of (seed, cx, cz): the worker produces the SAME bytes the main thread
 * would, so replay output is bit-identical with or without the worker.
 * Decoration (populate_mc owr windows) and light stay on the main thread -
 * their order-sensitive probe/debug flywheel is untouched.
 *
 * THREAD SAFETY BY TRANSLATION UNIT: chunk_provider.h / structures.h keep
 * their mutable state (cp_mesa_cache, st_map_features_host) as per-TU
 * `static`. This file is its own TU: ONLY the worker thread runs generation
 * code here, and ONLY the main thread runs light.c's copy, so the two
 * threads share no generation state. Never call generation entry points in
 * this TU from the main thread.
 *
 * Allocate-once: pool, primer and scratch are malloc'd in genpf_start; after
 * that the worker only mutates bytes. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chunk_provider.h"   /* ChunkPrimer, CpScratch, McSinTable, u16, i64 */
#include "structures.h"       /* st_run_features */
#include "gen_prefetch.h"
#include "core/config.h"      /* cr_cfg()->no_prefetch */

enum { PF_EMPTY = 0, PF_BUILDING, PF_READY, PF_CONSUMED };

typedef struct { int cx, cz, state; u16 *data; } PfSlot;

static struct {
    int on;
    long long seed;
    int rad, D;               /* ring radius; toroidal table dim = 2*rad+1 */
    PfSlot *slots;            /* D*D */
    u16 *pool;                /* D*D * 65536 block ids */
    ChunkPrimer *primer;      /* worker-only scratch */
    CpScratch  *scratch;      /* worker-only scratch */
    McSinTable  st;
    pthread_t th;
    pthread_mutex_t mu;
    atomic_int ccx, ccz, run, hinted;
} g;

static PfSlot *slot_of(int cx, int cz) {
    int sx = cx % g.D; if (sx < 0) sx += g.D;
    int sz = cz % g.D; if (sz < 0) sz += g.D;
    return &g.slots[sx * g.D + sz];
}

static void *worker(void *arg) {
    (void)arg;
    while (atomic_load(&g.run)) {
        if (!atomic_load(&g.hinted)) { usleep(1000); continue; }
        int ccx = atomic_load(&g.ccx), ccz = atomic_load(&g.ccz);
        int made = 0;
        for (int r = 0; r <= g.rad && atomic_load(&g.run); ++r) {
            for (int cx = ccx - r; cx <= ccx + r; ++cx)
                for (int cz = ccz - r; cz <= ccz + r; ++cz) {
                    if (cx != ccx - r && cx != ccx + r &&
                        cz != ccz - r && cz != ccz + r)
                        continue;                    /* ring perimeter only */
                    PfSlot *s = slot_of(cx, cz);
                    pthread_mutex_lock(&g.mu);
                    /* claim unless this slot already holds THIS chunk (in any
                     * state: READY waits for take, CONSUMED = resident in the
                     * light pool, no point regenerating). */
                    int claim = !(s->state != PF_EMPTY && s->cx == cx && s->cz == cz);
                    if (claim) { s->cx = cx; s->cz = cz; s->state = PF_BUILDING; }
                    pthread_mutex_unlock(&g.mu);
                    if (!claim) continue;
                    st_run_features(g.primer, g.scratch, &g.st,
                                    (i64)g.seed, cx, cz, -1);
                    pthread_mutex_lock(&g.mu);
                    if (s->cx == cx && s->cz == cz && s->state == PF_BUILDING) {
                        memcpy(s->data, g.primer->data, sizeof g.primer->data);
                        s->state = PF_READY;
                    }
                    pthread_mutex_unlock(&g.mu);
                    made = 1;
                    /* follow the player: restart the spiral when the hint moves */
                    if (atomic_load(&g.ccx) != ccx || atomic_load(&g.ccz) != ccz)
                        goto rescan;
                }
        }
rescan:
        if (!made) usleep(500);   /* region fully prefetched; idle politely */
    }
    return NULL;
}

void genpf_start(long long seed, int radius) {
    if (cr_cfg()->no_prefetch) return;
    if (radius < 1) return;
    if (g.on) {
        if (g.seed == seed && g.rad == radius) return;  /* already running */
        genpf_stop();                                   /* re-seed: fresh pool */
    }
    g.seed = seed;
    g.rad = radius;
    g.D = 2 * radius + 1;
    size_t n = (size_t)g.D * (size_t)g.D;
    g.slots   = (PfSlot *)calloc(n, sizeof(PfSlot));
    g.pool    = (u16 *)malloc(n * 65536 * sizeof(u16));
    g.primer  = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    g.scratch = (CpScratch *)malloc(sizeof(CpScratch));
    if (!g.slots || !g.pool || !g.primer || !g.scratch) {
        free(g.slots); free(g.pool); free(g.primer); free(g.scratch);
        memset(&g, 0, sizeof g);
        return;                                         /* prefetch off, correct */
    }
    for (size_t i = 0; i < n; ++i) g.slots[i].data = g.pool + i * 65536;
    mc_sin_table_init(&g.st);
    pthread_mutex_init(&g.mu, NULL);
    atomic_store(&g.hinted, 0);
    atomic_store(&g.run, 1);
    if (pthread_create(&g.th, NULL, worker, NULL) != 0) {
        free(g.slots); free(g.pool); free(g.primer); free(g.scratch);
        memset(&g, 0, sizeof g);
        return;
    }
    g.on = 1;
}

void genpf_hint(int ccx, int ccz) {
    if (!g.on) return;
    atomic_store(&g.ccx, ccx);
    atomic_store(&g.ccz, ccz);
    atomic_store(&g.hinted, 1);
}

int genpf_take(int cx, int cz, unsigned short *out) {
    if (!g.on) return 0;
    PfSlot *s = slot_of(cx, cz);
    int got = 0;
    pthread_mutex_lock(&g.mu);
    /* If the worker is mid-build on exactly this chunk, wait for it rather
     * than duplicating the ~1ms generation (during the spawn burst this is
     * what splits the work 2 ways instead of racing). Bounded: the build is
     * short and identity can only change to a different chunk, which exits. */
    int spins = 200;                       /* 200 * 50us = 10ms ceiling */
    while (s->state == PF_BUILDING && s->cx == cx && s->cz == cz && spins--) {
        pthread_mutex_unlock(&g.mu);
        usleep(50);
        pthread_mutex_lock(&g.mu);
    }
    /* CONSUMED still holds valid bytes for this identity (slot data is only
     * overwritten when reclaimed for a DIFFERENT chunk), so a second consumer
     * (light.c gen_chunk and populate_mc's base-chunk memo both want the same
     * mode -1 product) copies the same bytes. */
    if ((s->state == PF_READY || s->state == PF_CONSUMED) &&
        s->cx == cx && s->cz == cz) {
        memcpy(out, s->data, (size_t)65536 * sizeof(u16));
        s->state = PF_CONSUMED;
        got = 1;
    }
    pthread_mutex_unlock(&g.mu);
    return got;
}

int genpf_active(long long seed) {
    return g.on && g.seed == seed;   /* g.seed written only before the worker starts */
}

void genpf_stop(void) {
    if (!g.on) return;
    atomic_store(&g.run, 0);
    pthread_join(g.th, NULL);
    pthread_mutex_destroy(&g.mu);
    free(g.slots); free(g.pool); free(g.primer); free(g.scratch);
    memset(&g, 0, sizeof g);
}
