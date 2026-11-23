/* The device cast's copy of each env's client world, its host side
 * (lane/rcdev): what each env's ring (rchead.h) holds, and the records that
 * bring it up to date before a cast. Called on the env's worker after its
 * step, with the env current, never two calls for one env at once. */
#ifndef NETHERITE_RUNTIME_RCMIRROR_H
#define NETHERITE_RUNTIME_RCMIRROR_H

#include <stddef.h>
#include <stdint.h>

#include "rchead.h"

#ifdef __cplusplus
extern "C" {
#endif

struct session;

/* a ring column as the device holds it: the chunk (its address and write
 * sequence, world.h wseq: any block, metadata or light write moves it) and
 * the hashes of the bytes copied, the column's and each section's */
struct rcm_col {
    int32_t cx, cz;
    int32_t state;              /* 0: nothing copied (the device's column is stale) */
    int32_t exists;
    const void *chunk;
    uint64_t wseq;
    uint64_t col_hash;
    uint64_t sec_hash[16];      /* 0: no storage */
};

struct rcm_env {
    uint32_t epoch;             /* pool.h pool_epoch at the last sync: a reset makes it new */
    const void *world;          /* the client world synced (a dimension's travel makes a new one) */
    struct rcm_col col[RCD_RMAX * RCD_RMAX];
    /* the records staged since the last copy (page-locked, the device side's) */
    unsigned char *stage;
    size_t cap, used;
    int32_t nrec;
    /* counters: syncs, chunks whose wseq moved, records staged, flushes on the worker */
    uint64_t syncs, dirty, recs, flushes;
};

struct rcmirror {
    int n;                      /* envs */
    int R;                      /* the ring's side */
    struct rcm_env *env;        /* [n], each stage given by the owner */
    /* the stage is full: copy its records to the device now, from the
     * worker (0, or -1: the cast cannot be made) */
    int (*flush)(void *ctx, int id);
    void *flush_ctx;
};

/* the ring's side for a range (rchead.h) */
int rcmirror_side(int range);

/* env id's ring brought to its client world for the cast h (raycast_head's:
 * the window), its top set (raycast.c sections()'s); 0, or -1 (h->ok 0)
 * when the world cannot be copied (a section with ids past 255, a window
 * past the ring, a failed flush) */
int rcmirror_sync(struct rcmirror *m, int id, uint32_t epoch, struct session *ss, struct rcd_head *h);

#ifdef __cplusplus
}
#endif

#endif
