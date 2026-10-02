/* A capture of the light engine (GPU plan L16): every call of
 * World.updateLightByType (world.c world_light_update_raw) the replay makes,
 * with what a device needs to run the same call and what C left behind.
 *
 * The capture keeps a mirror of chunk slots on the consumer's side (the
 * device): a slot holds a chunk's struct as the image lays it out (world.h
 * struct chunk, CHUNK_SLOT bytes) followed by its sixteen bands as struct
 * chunk_sec, so band k of a mirrored chunk is always at chunk + LC_BAND_OFF(k)
 * and a chunk names it by bandoff exactly as it does in the env image. The
 * stream is a sequence of records:
 *
 *   LC_PUT   a chunk's current struct and bands into a slot, sent before a
 *            call whose 5x5 chunks hold a chunk the mirror does not have as
 *            it is now (another chunk, or a later write sequence, struct
 *            chunk wseq: every block, metadata or light write moves it)
 *   LC_CALL  one call: the light type, the cell, the world's epoch and
 *            write sequence before, the 5x5 chunks around the cell's chunk
 *            as slots, and C's result: the return value, the queue's length
 *            and whether it hit the 32,768 cap, the epoch and sequence after,
 *            and for every chunk the call wrote (its wseq moved) the chunk's
 *            struct and its bands' sky and block light after.
 *
 * With drivers on (lightcap_new's second argument), the three loops that
 * call the engine over a chunk are captured whole instead of their calls:
 * Chunk.func_150809_p's column checks (func_150811_f over the 256 columns),
 * func_150801_a's (a neighbour's facing edge) and enqueueRelightChecks; the
 * record is an LC_CALL whose `what` names the driver, over the 5x5 chunks
 * around the chunk, with the driver's result (func_150809_p's flag, the
 * relight cursor) and the number of calls it made and their queue lengths.
 * A relight whose facing neighbour is not loaded (C would load it on a
 * read) is left to its calls.
 *
 * A consumer replays the calls in order on its mirror and compares its
 * result with C's; a chunk C wrote is taken as C left it. The capture
 * allocates nothing while the tick runs (the buffers and the mirror's map
 * are made up front). */
#ifndef NETHERITE_LIGHTCAP_H
#define NETHERITE_LIGHTCAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct world;
struct chunk;

enum { LC_PUT = 1, LC_CALL = 2 };

/* what an LC_CALL replays */
enum { LC_O_UPDATE = 0, LC_O_COLUMNS, LC_O_SIDE, LC_O_RELIGHT, LC_O_KINDS };

/* the chunks a call sees: cx - 2 .. cx + 2 by cz - 2 .. cz + 2 around the
 * cell's chunk; slot index (dz + 2) * 5 + dx + 2 */
#define LC_WIN 5
#define LC_WIN_N (LC_WIN * LC_WIN)

/* the mirror's slots */
#define LC_SLOTS 2048

struct lc_put {
    uint32_t kind, slot;
    uint16_t present, shared;   /* bit k: band k has storage / is shared */
    uint16_t sent, pad;         /* bit k: band k's cells follow (the rest
                                 * are as the mirror has them) */
    uint32_t bytes;             /* the whole record */
    uint32_t pad2;
    /* then the chunk struct (layout.chunk_bytes, padded to 8), then for
     * each sent band, low k first: ids[4096], sky[2048], blocklight[2048] */
};

/* the slot of a loaded chunk a call refused by the 17-block box does not
 * read, which the mirror need not have */
#define LC_STALE 0xffffffffu

struct lc_call {
    uint32_t kind, bytes;
    uint64_t index;             /* the record's number in the replay */
    int32_t what;               /* LC_O_* */
    int32_t type, x, y, z;      /* the update (LC_O_UPDATE) */
    int32_t cx, cz;             /* the 5x5's centre chunk */
    int32_t side, dim;          /* func_150801_a's side; the world's dimension */
    int32_t no_generate;        /* struct world no_generate */
    /* C's result: the return value (func_150809_p's flag for its columns),
     * whether a queue hit the 32,768 cap, the relight cursor before and
     * after, the updates run and their queue lengths summed */
    int32_t ret, overflow, cursor0, cursor1;
    uint32_t calls;
    int32_t remote;             /* struct world is_remote: the 17-block box
                                 * always holds (ChunkProviderClient.chunkExists) */
    uint64_t tail_sum;
    uint64_t epoch0, wseq0, epoch1, wseq1;
    uint32_t written;           /* bit k: window chunk k was written */
    uint32_t flags;             /* LC_F_* */
    uint32_t slot[LC_WIN_N];    /* slot + 1, 0 for a chunk that is not
                                 * loaded, LC_STALE (a refused call) */
    /* then per written chunk, low k first: struct lc_after, the chunk struct
     * after (padded to 8), and for each band with storage after: sky[2048],
     * blocklight[2048] */
};

struct lc_after {
    uint32_t slot;
    uint16_t present, shared;   /* shared: the device's band flags (the deferred mode's results) */
};

/* The deferred mode (lightdefer.h, lightcap_defer_run): the device holds the
 * client world's light. A call record carries LC_F_AUTH and no after-state:
 * the device keeps its own result, advances the world's epoch and write
 * sequence itself from the record's (LC_F_CTR, when the host moved them since
 * the device last had them) or its own, and marks the slots it wrote. A sync
 * returns them: an lc_back, then for each slot written since the last sync
 * an lc_after (the slot, its high bit set when the device regenerated the
 * chunk's sky map; its band storage and shared flags), the chunk struct
 * (padded to 8) and each stored band's sky[2048] and blocklight[2048]. */
enum { LC_F_AUTH = 1, LC_F_CTR = 2 };

struct lc_back {
    uint32_t slots;             /* the entries that follow */
    int32_t status;             /* 0; else lighting's status (a record the device could not run) */
    uint64_t bytes;             /* the whole back stream, this header included */
    uint64_t epoch, wseq;       /* the world's epoch and write sequence after the last record */
};

/* Where the fields the light engine reads and writes sit in struct chunk and
 * struct chunk_sec, for a consumer that is not C. */
struct lc_layout {
    uint32_t chunk_bytes, chunk_slot, sec_bytes;
    uint32_t cx, cz, band, height, precip, height_min, mask, no_sky, stamp, edge_stamp, wseq;
    uint32_t terrain_populated, queued_light_checks;
    uint32_t sec_ids_hi, sec_shared, sec_ids, sec_metas, sec_sky, sec_blocklight;
};

/* band k of a mirrored chunk, from the chunk's address */
#define LC_BAND_OFF(L, k) ((int64_t)(L)->chunk_slot + (int64_t)(k) * (L)->sec_bytes)
#define LC_SLOT_BYTES(L) ((size_t)(L)->chunk_slot + 16 * (size_t)(L)->sec_bytes)

void lightcap_layout(struct lc_layout *L);
/* Block.getLightOpacity, Block.getLightValue and whether the block's
 * material is air, by id */
void lightcap_blocks(uint8_t opacity[4096], uint8_t light[4096], uint8_t air[4096]);

/* Where the stream goes. buf is the buffer to fill (cap bytes); submit hands
 * over its first n bytes and returns the next buffer (NULL on a failure: a
 * mismatch the consumer found, or a dead consumer), finish flushes and
 * waits, and returns 0 when every call matched. */
struct lc_sink {
    void *ctx;
    uint8_t *buf;
    size_t cap;
    uint8_t *(*submit)(void *ctx, size_t n);
    int (*finish)(void *ctx, FILE *report);
    void (*free)(void *ctx);
    /* the deferred mode's sync: the first n bytes run after everything
     * submitted, then the slots written since the last sync come back (*back,
     * an lc_back stream the sink owns until the next call); 0 on success.
     * The next stream starts at the sink's buf again. */
    int (*sync)(void *ctx, size_t n, const uint8_t **back);
};

struct lc_stats {
    uint64_t calls, sky, guard, wrote, writes, overflow;
    uint64_t remote;            /* the calls on a client world (is_remote) */
    uint64_t drivers[LC_O_KINDS], driver_calls, relight_host;
    int32_t tail_max;
    uint64_t puts, put_bands, put_bytes, call_bytes, chunks_written, flushes;
};

/* the device's consumer (cuda/lighting/cap.c: the CUDA build only) */
struct lc_sink *lc_sink_cuda_create(void);
/* the device as the client world's deferred light's executor (lightdefer.h;
 * cuda/lighting/cap.c: the CUDA build only); 0 on success */
struct lightdefer_exec;
int lc_defer_exec_cuda(struct lightdefer_exec *ex);
/* the same with the device in this process (the pipeline's envs: device
 * DEV, a mirror of nslots chunks) */
int lc_defer_exec_cuda_inproc(struct lightdefer_exec *ex, int dev, int nslots);

/* The stored capture (test_snapshots --light-cap save:PATH): the stream
 * written to f as it is handed over, for a replay on the device alone later
 * (cuda/lighting/check.c): a struct lc_file_head (the layout and the block
 * tables the device needs), then each handed-over buffer as its length (a
 * uint64_t, at most head.cap) and its bytes, then a length of 0. The caller
 * opens and closes f. */
#define LC_FILE_MAGIC 0x434c574eu   /* "NWLC" */
#define LC_FILE_VERSION 1u
struct lc_file_head {
    uint32_t magic, version, layout_bytes, drivers;
    uint64_t cap;
    struct lc_layout L;
    uint8_t opacity[4096], light[4096], air[4096];
};
struct lc_sink *lc_sink_file_create(FILE *f, size_t cap, int drivers);

struct lightcap;

/* sink NULL: count only (the stats and the stream's size, nothing kept);
 * drivers: capture the three drivers whole */
struct lightcap *lightcap_new(struct lc_sink *sink, int drivers);
/* A driver (LC_O_COLUMNS, LC_O_SIDE, LC_O_RELIGHT) over chunk ch starts:
 * a token for lightcap_driver_end, or -1 when it is not captured whole.
 * ret is func_150809_p's flag for LC_O_COLUMNS. */
int lightcap_driver_begin(struct lightcap *c, struct world *w, struct chunk *ch, int what, int side);
void lightcap_driver_end(struct lightcap *c, struct world *w, struct chunk *ch, int token, int ret);
/* world_light_update_raw with the capture around it */
int lightcap_update(struct lightcap *c, struct world *w, int type, int x, int y, int z);
/* The deferred mode (lightdefer.h): a capture whose sink holds the client
 * world's light (lc_back above), mirroring at most nslots chunks
 * (<= LC_SLOTS). lightcap_defer_run is lightdefer's executor: it streams the
 * operations as records (the chunks they read put first, as the capture
 * does), syncs, and writes what came back into the host's chunks: their light,
 * band storage, heights, stamps and write sequences, and the world's epoch
 * and write sequence, as running the operations in C would have left them.
 * lightcap_defer_forget drops a world's slots (the world is freed; NULL:
 * every world's, an environment reset). */
struct lightcap *lightcap_new_defer(struct lc_sink *sink, int nslots);
struct ld_op;
int lightcap_defer_run(struct lightcap *c, struct world *w, const struct ld_op *ops, size_t n);
void lightcap_defer_forget(struct lightcap *c, const struct world *w);

/* the stream's last records to the sink and its verdict; 0 when every call
 * matched (always, counting only); the stats line to out */
int lightcap_finish(struct lightcap *c, FILE *out);
const struct lc_stats *lightcap_stats(const struct lightcap *c);
void lightcap_free(struct lightcap *c);

#endif
