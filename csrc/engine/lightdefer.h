/* The client world's light, deferred (SPEC.md's GPU section, D8: the client
 * world's light kept exact and deferred to the device). With a lightdefer
 * set on the environment (struct env light.defer), a call of the light engine
 * on a client world (world.c update_light_by_type on an is_remote world) and
 * the client's relight checks (Chunk.enqueueRelightChecks over one chunk) are
 * not run: they are appended to the pending list in order, and an executor
 * runs the list later (the device's light engine, cuda/lighting, or C:
 * world_light_defer_run_c) and leaves the client world as running each call
 * at its turn would have.
 *
 * Nothing else may see the client world between a call and its run, so the
 * list is run first (a sync) wherever the host touches what the light engine
 * reads or writes of the client world:
 *   - a write to its chunks: a block or metadata write (World.setBlock on the
 *     client reads the cell's light, Chunk.func_150807_a's
 *     propagateSkylightOcclusion test, and the band storage a light write may
 *     have made), a chunk's cells copied in, a chunk put in or taken out;
 *   - a read of its light: world_get_light, world_get_full_block_light_value
 *     (the client's mushroom placement test, play's entity, particle and
 *     overlay brightness), and the frame's mesher, which reads the bands
 *     (the harness syncs before a frame: lightdefer_sync);
 *   - the world freed (the list is dropped: its world is gone).
 * The recorded calls' own reads (blocks, heights, band storage) are what they
 * would have read at their turn, since nothing wrote the world in between.
 *
 * The return value a deferred call gives its caller is 0: on a client world
 * nothing reads it (func_150809_p, whose column checks use it, never runs on
 * a client world: a received chunk is light-populated). */
#ifndef NETHERITE_LIGHTDEFER_H
#define NETHERITE_LIGHTDEFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct world;

/* a pending operation: an update (LC_O_UPDATE: type, x, y, z) or a chunk's
 * relight checks (LC_O_RELIGHT: the chunk at x, z) */
struct ld_op {
    int32_t what, type, x, y, z;
};

/* the pending list's length; a full list is run then and there */
#define LD_CAP 65536

/* Runs a world's pending operations in order; 0 on success. */
struct lightdefer_exec {
    void *ctx;
    int (*run)(void *ctx, struct world *w, const struct ld_op *ops, size_t n);
    /* the world the executor holds state for is gone (freed or replaced): drop
     * it; NULL: every world (the environment was reset) */
    void (*forget)(void *ctx, const struct world *w);
    void (*free)(void *ctx);
};

/* where the syncs happen (the experiment's switches; LD_SYNC_ALL is the rule) */
enum {
    LD_SYNC_WRITE = 1,          /* before a write to a client chunk, or a chunk put in or taken out */
    LD_SYNC_READ = 2,           /* before a light read (world_get_light and friends) */
    LD_SYNC_ALL = 3
};

enum { LD_CAUSE_WRITE, LD_CAUSE_READ, LD_CAUSE_FRAME, LD_CAUSE_FULL, LD_CAUSE_WORLD, LD_CAUSES };

struct lightdefer_stats {
    uint64_t updates, relights;         /* operations recorded */
    uint64_t runs, ran;                 /* executor runs, operations they ran */
    uint64_t syncs[LD_CAUSES];          /* runs by cause (a sync with nothing pending is not counted) */
    uint64_t dropped;                   /* operations dropped with their world */
    size_t max_pending;
    int failed;                         /* an executor run failed */
};

struct lightdefer {
    struct world *w;                    /* the world of the pending operations */
    size_t n;
    int never;                          /* the premise's replay: record, never run */
    int syncs;                          /* LD_SYNC_* in force */
    int in_run;
    struct lightdefer_exec exec;
    struct lightdefer_stats st;
    struct ld_op ops[LD_CAP];
};

/* A lightdefer running on EXEC (NULL: C, world_light_defer_run_c). Made
 * outside the tick (it is large: LD_CAP operations). */
struct lightdefer *lightdefer_new(const struct lightdefer_exec *exec);
void lightdefer_free(struct lightdefer *d);

/* the environment's pending list run (a sync with cause CAUSE); 0 on success
 * or when nothing is pending */
int lightdefer_sync_cause(struct lightdefer *d, int cause);
static inline int lightdefer_sync(struct lightdefer *d)
{
    return d == NULL ? 0 : lightdefer_sync_cause(d, LD_CAUSE_FRAME);
}

/* the stats line */
void lightdefer_report(const struct lightdefer *d, FILE *out);

#endif
