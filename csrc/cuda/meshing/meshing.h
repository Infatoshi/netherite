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
 * NULL for mesh.cubin under the program's out/native (NETH_MESH_CUBIN is
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
/* The libm keys the count passes since the last call looked up and did not
 * find (MESHING_ERR_TRIG; kernels.h struct meshing_miss: the key and the
 * cell): the first min(MAX, MESHING_MISSES) into OUT, then cleared. Returns
 * how many there were (more than it wrote when they overflowed), -1 on a
 * CUDA error. Waits for the stream: the error path only. */
struct meshing_miss;
int meshing_trig_misses(struct meshing *m, struct meshing_miss *out, int max);
/* Request I of the last count pass (meshing_add's index) meshed by the C
 * mesher on the host: the nine chunk records and their bands as the device
 * holds them read back, the flower pots its feed listed, and the template
 * the device meshes from, so the mesh is the one the device should have
 * made (a pass the device could not mesh: MESHING_ERR_*). The vertices
 * into T (rb_tess_init's; the vertex count returned, -1 on a CUDA error or
 * a full T), the Tessellator flags into *FLAGS. Waits for the stream: the
 * error path only. */
struct rb_tess;
int meshing_host_mesh(struct meshing *m, int i, struct rb_tess *t, int *flags);
/* request I's section and pass: cx, s, cz, pass */
void meshing_request_section(const struct meshing *m, int i, int place[4]);

/* The table mode (table.h: without smooth lighting, a count pass's
 * cells drawn apart, a thread a cell or a face, instead of by the lanes'
 * chain): ON from the next count pass on. Off by default; the meshes are
 * the same either way. */
void meshing_set_tab(struct meshing *m, int on);
/* A test's planted fault: ON, no libm value is sent from the next launch on
 * (neither the launch's table nor any request's list), so every liquid
 * with a flow, sunflower head and brewing stand fails on the device
 * (MESHING_ERR_TRIG) and is meshed on the host (cuda/render's fallback,
 * meshing_host_mesh). Off by default. */
void meshing_plant_trig(struct meshing *m, int on);
/* A digest of every band slot SLOT holds on the device (each band's ids_hi
 * read as set or not): two slots sent the same feeds hold the same bytes.
 * Waits for the device. 0, or -1. */
int meshing_slot_bands(struct meshing *m, int slot, unsigned long long *digest);

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
    size_t copy_only;               /* 1: the launch had copies and no requests (applied since lane/meshtrig) */
};
const struct meshing_stats *meshing_stats(struct meshing *m);

#ifdef __cplusplus
}
#endif

#endif
