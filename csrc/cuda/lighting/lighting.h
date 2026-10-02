/* The light engine on the device (GPU plan L16): World.updateLightByType
 * (func_147463_c) as a device function over chunks laid out as the env
 * image lays them out (world.h struct chunk, its bands named by bandoff),
 * one thread block per update, and the consumer of engine/lightcap.h's capture
 * stream that replays every captured call on a mirror of chunk slots and
 * compares each result with C's.
 *
 * The device functions themselves are in dev.cuh.
 *
 * The update is C's FIFO queue run a window at a time: the block evaluates
 * up to blockDim entries at once against the state before the window,
 * finds the first entry that read a cell an earlier entry of the window
 * changed (or that follows a write making a section, which regenerates the
 * chunk's sky), and commits the entries before it in queue order: their
 * writes (distinct cells), their write epochs, and their appended entries at
 * the positions a sequential run gives them, with the 32,768 cap applied as
 * C applies it. So the result, the queue and every write's order are C's,
 * whatever the visit order within a window. */
#ifndef NETHERITE_LIGHTING_H
#define NETHERITE_LIGHTING_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../engine/lightcap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* what the device found: counts, and the first difference */
struct lighting_result {
    /* records (updates and drivers), drivers, the updates they ran, those
     * the 17-block box refused (single updates), records that wrote light,
     * the writes */
    uint64_t records, drivers, calls, refused, wrote, writes, puts, windows, win_max, bytes;
    /* the runs of single updates evaluated a call per thread, and the longest */
    uint64_t runs, run_max;
    int32_t status;             /* 0 equal so far, 1 a difference, 2 the device could not run a call */
    int32_t what;               /* CL_* below */
    uint64_t index;             /* the record */
    int32_t what_rec;           /* its LC_O_* */
    int32_t type, x, y, z, chunk, cell;   /* the last update it ran; the chunk (window index) and cell */
    int32_t wcx, wcz;           /* the window's centre chunk */
    int64_t dev, c;
};

enum {
    CL_RET = 1, CL_TAIL, CL_OVERFLOW, CL_EPOCH, CL_WSEQ, CL_WRITTEN, CL_MASK, CL_HEIGHT, CL_HEIGHT_MIN, CL_PRECIP,
    CL_STAMP, CL_EDGE, CL_CWSEQ, CL_PRESENT, CL_SKY, CL_BLOCK, CL_CALLS, CL_CURSOR,
    CL_ERR_WINDOW = 100,        /* a read outside the 5x5 chunks */
    CL_ERR_LOAD,                /* a block read in a chunk that is not loaded (C would load it) */
    CL_ERR_STALE,               /* a read of a chunk the capture did not send */
    CL_ERR_RECORD               /* a record the stream should not hold */
};

const char *lighting_what(int what);

struct lighting;

/* the mirror (LC_SLOTS slots) and one stream buffer of cap bytes on the
 * current device; NULL when there is no device */
struct lighting *lighting_create(const struct lc_layout *L, const uint8_t opacity[4096], const uint8_t light[4096],
                               const uint8_t air[4096], size_t cap);
/* the same with a mirror of nslots slots on device dev (-1: the current
 * one): the deferred mode's (lightcap.h LC_F_AUTH) per environment */
struct lighting *lighting_create_n(const struct lc_layout *L, const uint8_t opacity[4096], const uint8_t light[4096],
                                 const uint8_t air[4096], size_t cap, int nslots, int dev);
/* the deferred mode's collect: the slots written since the last one (an
 * lc_back stream of *n bytes, at most cap) into out; 0 on success */
int lighting_collect(struct lighting *g, uint8_t *out, size_t cap, size_t *n);
/* the largest collect */
size_t lighting_collect_cap(const struct lighting *g);
/* the stream's next n bytes, replayed; 0 when every call so far matched, 1
 * at a difference (lighting_result says where), -1 on a device error */
int lighting_run(struct lighting *g, const uint8_t *stream, size_t n);
void lighting_result(struct lighting *g, struct lighting_result *r);
void lighting_free(struct lighting *g);
/* the replay's verdict line (light_check.sh reads it): every call equal, or
 * the first difference; failed: the stream stopped short (a device error).
 * 0 when every call matched. */
int lighting_report(const struct lighting_result *r, int failed, double wait_s, FILE *out);

#ifdef __cplusplus
}
#endif

#endif
