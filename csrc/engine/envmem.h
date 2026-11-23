/* One environment's state in bytes, by owner (GPU audit L9): what a batched
 * environment holds N copies of. A harness measure, never called by the tick.
 *
 * Every fixed struct counts at its full size (a GPU environment reserves its
 * layout whole); what grows counts what it holds now: a chunk its struct, its
 * bands and its packed blob, a table its capacity, a pool its live slots. The
 * snapshot a run started from and the tape are the harness's, not the
 * environment's, and are not counted. */
#ifndef NETHERITE_ENVMEM_H
#define NETHERITE_ENVMEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct chunk;
struct session;

enum envmem_owner {
    EM_SERVER_CHUNKS,   /* the three server worlds' loaded chunks and tile entities */
    EM_SERVER_WORLDS,   /* the three server worlds' structs, providers and tables */
    EM_REGION,          /* the region store: parked chunks, saved entities and ticks */
    EM_CLIENT,          /* the client world's chunks, its struct and client entities */
    EM_RENDERER,        /* the renderer's chunk records and meshes (play only) */
    EM_ENTITIES,        /* entity pools, lists and section tables */
    EM_TICKS,           /* pending block tick sets */
    EM_SCRATCH,         /* the env's scratch, worklists and fixed tables */
    EM_OTHER,           /* the players, packet queues, the rest of the replay */
    EM_N
};

#define ENVMEM_ITEMS 48

struct envmem_item {
    int owner;
    const char *what;
    size_t bytes;
};

struct envmem {
    size_t b[EM_N];
    int chunks[3];      /* chunks held: server (loaded), region (parked), client */
    uint64_t spilled;   /* the region stores' chunks in the spill file (regionspill.h), */
    uint64_t spill_file;   /* and the file's bytes (on disk, not counted) */
    struct envmem_item items[ENVMEM_ITEMS];   /* the parts of each owner */
    int nitems;
    uint64_t seen[(1 << 17) / 64];   /* the bands counted so far (ARENA_BANDS) */
};

const char *envmem_name(int owner);
void envmem_session(const struct session *ss, struct envmem *m);
/* a chunk under owner/what: its struct, blob and the bands no owner counted
 * yet (call after envmem_session: a band the server shares is the server's) */
void envmem_chunk(struct envmem *m, int owner, const char *what, const struct chunk *c);
/* bytes under owner, as part what (a string that outlives m) */
void envmem_add(struct envmem *m, int owner, const char *what, size_t bytes);
size_t envmem_total(const struct envmem *m);
/* one line: total and every owner in MB; detail adds a line per part of 256 KB or more */
void envmem_print(FILE *f, const char *label, const struct envmem *m, int detail);

#endif
