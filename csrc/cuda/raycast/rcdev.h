/* The textured ray cast on the device (lane/rcdev, cuda/raycast): the
 * runtime's camera (runtime/raycast.h, client.camera raycast) with its rays
 * on the GPU. The CPU keeps each cast's setup (runtime/raycast.c
 * raycast_head: the camera, light, sky, fog and entity boxes) and a copy of
 * each env's client world on the device (runtime/rcmirror.c: a ring of the
 * columns and sections in reach, updated by records of what changed); the
 * device walks one thread a pixel through that copy with raycast.c's own
 * arithmetic (cast.cu: walk ported), so its frames and seen records equal
 * raycast_seen's byte for byte (pool_gate --raycast-device).
 *
 *   rcdev_new(n, w, h, range, dev, tables)   device memory for n envs
 *   rcdev_head(d, id), rcdev_mirror(d)       what the worker fills after env id's step:
 *                                            raycast_head(sp, ss, ids, rcdev_head(d, id)), then
 *                                            rcmirror_sync(rcdev_mirror(d), id, epoch, ss, head)
 *   rcdev_launch(d, ids, k, stream)          those envs' casts on stream (a cudaStream_t)
 *   rcdev_frames(d), rcdev_seen(d, id)       the frames [n][h][w][3] on the device, and env
 *                                            id's seen record on the host once the stream is done
 *
 * An env's head and records are read by its launch's copies: the next
 * rcmirror_sync of that env (its worker) must not start before its last
 * launch's stream is done (the trainer's order: recv, cast, policy, send). */
#ifndef NETHERITE_CUDA_RAYCAST_RCDEV_H
#define NETHERITE_CUDA_RAYCAST_RCDEV_H

#include <stddef.h>
#include <stdint.h>

#include "../../runtime/rchead.h"
#include "../../runtime/rcmirror.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rcdev;

/* n envs' casts of w x h rays to range blocks on CUDA device dev, the
 * tables copied once; NULL with a message */
struct rcdev *rcdev_new(int n, int w, int h, int ch, int range, int dev, const struct rcd_tabsrc *t, char *err,
                         size_t en);
void rcdev_free(struct rcdev *d);
struct rcd_head *rcdev_head(struct rcdev *d, int id);
struct rcmirror *rcdev_mirror(struct rcdev *d);
/* envs ids[k] (each synced since its last launch): their records copied
 * in, the rays, the seen records back to the host, on stream (NULL: the
 * legacy default stream; RCDEV_THREAD_STREAM: the calling thread's own);
 * the launch's ticket (rcdev_wait), or -1 with a message (rcdev_error) */
int rcdev_launch(struct rcdev *d, const int *ids, int k, void *stream);
/* the host waits for a launch's copies (its seen records are then readable); 0 or -1 */
int rcdev_wait(struct rcdev *d, int ticket);
#define RCDEV_THREAD_STREAM ((void *)0x2)   /* cudaStreamPerThread */
/* env id's cast alone, from any thread (its own stream, its env's lists):
 * its frame copied to rgb (host) and its seen record, waited for; 0 or -1
 * (a gate's call: no other launch of id may be in flight) */
int rcdev_cast_now(struct rcdev *d, int id, uint8_t *rgb);
uint8_t *rcdev_frames(struct rcdev *d);
/* the split (engine.raycast_host_groups): page-locked host frames [n][h][w][3] for the envs cast on their workers
 * (made at the first call; NULL with a message), and envs lo..lo + k - 1's rows of them copied into the device's
 * frames on stream, a ticket as a launch's (rcdev_wait: the rows may then be written again) or -1 */
uint8_t *rcdev_host_frames(struct rcdev *d);
int rcdev_put(struct rcdev *d, int lo, int k, void *stream);
const struct rcd_seen *rcdev_seen(const struct rcdev *d, int id);
const char *rcdev_error(const struct rcdev *d);
/* envs ids[k]'s last casts' inputs and outputs (the tables, heads, rings, frames and seen records) to path, the
 * kernel bench's input (cuda/raycast/bench.cu, make -C csrc gpu-raycast-bench); 0 or -1 */
int rcdev_dump(struct rcdev *d, const int *ids, int k, const char *path);
/* bytes of device memory held */
size_t rcdev_bytes(const struct rcdev *d);

#ifdef __cplusplus
}
#endif

#endif
