/* The generation server (lane/gpuspec): one card's chunk generators serving
 * every process of a machine (the GPU host: one pipeline process a GPU), so the
 * other cards only mesh and draw, and the rounds of four processes go to the
 * device as one batch (a round of 80 chunks takes about as long on the device
 * as one of 320: out/perf/gpuspec/genbench-seeds.txt).
 *
 * The serving process (pipeline.h gen_serve) makes its generators as before
 * and hands them to gensrv_start; the pool's own rounds go through
 * gensrv_gen's front, the other processes' (pipeline.h gen_client:
 * ga_gen_remote_create) over a Unix socket, each connection with a shared
 * mapping (a memfd) that holds the requests and the results: the server
 * writes a round's results straight into the requester's buffers there, and
 * the pool's round reads them as it reads a local generator's pinned pages.
 * One batcher thread a generator takes every waiting call, up to the
 * generator's max_batch requests, runs them as one run_built and scatters
 * the results. Only run_built is served (the built overworld chunk; the
 * Nether's and the End's raw arrays): a caller asking for raw overworld
 * arrays (pool.h gen_raw) gets its rounds dropped, and C generates them. */
#ifndef NW_GENSRV_H
#define NW_GENSRV_H

#include <stddef.h>
#include <stdint.h>

struct ga_gen;
struct gensrv;

/* the server on path (a Unix socket, made anew; a path starting with @ is a
 * name in the abstract namespace, no file), batching over gens[0..n)
 * (the caller's, alive until gensrv_stop); NULL with err */
struct gensrv *gensrv_start(const char *path, struct ga_gen *const *gens, int n, char *err, size_t errn);
/* the serving process's own front: a generator whose calls join the
 * server's batches (the caller frees it with ga_gen_free) */
struct ga_gen *gensrv_front(struct gensrv *s);
/* stops accepting, waits for the connected clients to leave (at most
 * wait_s seconds), then stops the batchers and removes the socket */
void gensrv_stop(struct gensrv *s, int wait_s);

struct gensrv_stats {
    uint64_t rounds, calls, requests, max_requests;
    uint64_t unique;            /* the requests generated (one per chunk and seed a round) */
    uint64_t clients;           /* connections served */
    uint64_t run_ns, scatter_ns, queued_ns;   /* the generator's calls, the copies out, calls waiting for a batcher */
};
void gensrv_stats(struct gensrv *s, struct gensrv_stats *st);

/* a client: a generator whose calls go to the server on path (connecting
 * for up to wait_s seconds); NULL with err */
struct ga_gen *ga_gen_remote_create(const char *path, int wait_s, char *err, size_t errn);

#endif
