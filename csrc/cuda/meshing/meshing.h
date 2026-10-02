/* The device mesher's host side (cuda/meshing): its module (the cubin
 * meshing.mk builds) loaded into the current context, the mesher's tables on
 * the device, and per slot (one environment's world) the device copies of
 * the chunk records its feeds send (engine/meshfeed.h). The renderer
 * (cuda/render) runs it on its own stream: every frame's feed applied and
 * its section passes counted (one wait), then written into the blocks the
 * renderer gives them. Linux and CUDA only. */
#ifndef NETHERITE_MESHING_H
#define NETHERITE_MESHING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct meshing;
struct meshfeed_out;
struct rb_mesher;

/* The count and flags of one request (kernels.h struct meshing_res). */
struct meshing_count { int32_t count, flags, err, pad; };

/* On STREAM (a cudaStream_t) for MAX_SLOTS slots; the cubin is CUBIN, or
 * NULL for mesh.cubin under the program's out/native (NW_MESH_CUBIN is
 * never read: the path is the build's). NULL when it cannot load. */
struct meshing *meshing_new(void *stream, int max_slots, const char *cubin);
void meshing_free(struct meshing *m);
/* the tables T's frames were meshed with, uploaded at the first call (a
 * later call with a mesher of other tables fails) */
int meshing_context(struct meshing *m, const struct rb_mesher *t);
/* SLOT holds nothing (its next feed must start an epoch) */
void meshing_forget(struct meshing *m, int slot);

/* One launch: the feeds of several slots, each applied, then their
 * requests counted together. meshing_add returns the index of the feed's
 * first request in the launch, or -1 when the feed does not follow what
 * SLOT holds (not applied). meshing_count waits for the counts: *n of them,
 * in add order (meshing_count_start launches the count pass without
 * waiting, and meshing_count_wait waits for it: the caller works in
 * between). meshing_write then writes request i into OUT[i] (a device
 * address of count * 8 int32, NULL for none) on the stream, without
 * waiting. */
void meshing_begin(struct meshing *m);
int meshing_add(struct meshing *m, int slot, const struct meshfeed_out *f);
int meshing_count(struct meshing *m, const struct meshing_count **counts, int *n);
int meshing_count_start(struct meshing *m);
int meshing_count_wait(struct meshing *m, const struct meshing_count **counts, int *n);
int meshing_write(struct meshing *m, int32_t *const *out);

/* The table mode (table.h: without smooth lighting, a count pass's
 * cells drawn apart, a thread a cell or a face, instead of by the lanes'
 * chain): ON from the next count pass on. Off by default; the meshes are
 * the same either way. */
void meshing_set_tab(struct meshing *m, int on);

size_t meshing_device_bytes(const struct meshing *m);
/* the last launch: requests, bytes uploaded, device ms of the copy and
 * count pass and of the write pass (once the stream has passed it, and in
 * the next launch's prev_write_ms); the
 * upload by kind: the feeds' band images (whole or their changed runs),
 * chunk records, flower pot and libm lists, the requests (both passes), the
 * copy list and the launch record */
struct meshing_stats {
    size_t requests, upload_bytes, ops;
    double count_ms, write_ms;
    double prev_write_ms;           /* the launch before's write pass (meshing_begin reads it) */
    size_t band_bytes, chunk_bytes, list_bytes, req_bytes, ctl_bytes;
    size_t band_run_bytes;          /* of band_bytes, sent as runs (the rest whole) */
};
const struct meshing_stats *meshing_stats(struct meshing *m);

#ifdef __cplusplus
}
#endif

#endif
