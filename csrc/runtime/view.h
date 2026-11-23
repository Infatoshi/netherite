/* The observation's view per environment: play's frame (csrc/play/
 * playview.h), each environment with a private copy of out/runtime/
 * playview.so, so play's statics and the renderer files' are that
 * environment's own. A copy is loaded from a memory file (a distinct inode,
 * so the dynamic loader maps it apart from the others) and loaded again
 * after each reset: the reset replaced the session its statics point into.
 *
 * The callbacks are the pool's (pool_config.view_*, ctx the view set); they
 * run on the pool's workers with the environment current. A program that
 * uses them links the engine whole and exports it (-rdynamic, the archive
 * with --whole-archive): the copies resolve the engine from it. */
#ifndef NETHERITE_RUNTIME_VIEW_H
#define NETHERITE_RUNTIME_VIEW_H

#include <stddef.h>
#include <stdint.h>

#include "../play/playview.h"

struct session;
struct act;
struct pool_result;
struct raster_obs;
struct view_set;

/* N views of SO (NULL: out/runtime/playview.so beside the program, else
 * that path) drawing CFG's frame, and SPARES more (ids N on: a save's
 * replays, pool.h save_slots; never drawn, not in view_times). With draw, each frame is also drawn by
 * the C renderer as play draws it (the gate's reference; the product
 * records only). With counters, the tick hooks are timed and the thread's
 * hardware counters read around each hook call and each frame (a read
 * syscall each: 18 a step of 4 ticks); without, the frames alone are timed
 * (view_times' tick, instruction and cycle fields stay 0). NULL with ERR on
 * failure. */
struct view_set *view_set_new(const char *so, int n, int spares, const struct pv_config *cfg, int draw, int counters,
                              char *err, size_t en);
void view_set_free(struct view_set *vs);

/* the pool's callbacks */
int view_reset_cb(void *ctx, int id, struct session *ss, char *err, size_t n);
void view_tick_cb(void *ctx, int id, int64_t t, const struct act *a, int end);
void view_step_cb(void *ctx, int id, struct pool_result *r);

/* A save's view memory (pool.h pool_config.view_mem, view_reload,
 * view_resume): view ID's loaded copy's mapping and writable data; a fresh
 * copy loaded for ID and not opened (0, or -1 with err); the written copy
 * made the view (open, every device mesh sent again). */
struct pool_vmem;
int view_mem(struct view_set *vs, int id, struct pool_vmem *m);
int view_reload(struct view_set *vs, int id, struct pool_vmem *m, char *err, size_t n);
void view_resume(struct view_set *vs, int id);

/* env ID's view meshes every section again from a new mesh feed epoch
 * (pv_api.mesh_resync: the device renderers were made again). Env ID is
 * idle. */
void view_mesh_resync(struct view_set *vs, int id);

/* env ID's last frame as its C renderer drew it (draw mode; NULL otherwise) */
const unsigned char *view_drawn(struct view_set *vs, int id);
/* the C renderer (env ID's copy) over O into RGB: raster_obs_render. Call it
 * only while env ID is not stepping (it runs the env's own copy). */
void view_render(struct view_set *vs, int id, const struct raster_obs *o, unsigned char *rgb);

/* what the views spent, summed over the envs (each env's own sums, added
 * here: call it with the envs idle): wall ns and user instructions (the
 * calling thread's hardware counter) in the tick hooks, the frames and the
 * resets, user cycles in the hooks and the frames; the section passes the
 * frames meshed */
struct view_times {
    uint64_t tick_ns, frame_ns, reset_ns;
    double feed_ms;                        /* renderer's meshing feeds within its frame time */
    uint64_t tick_ins, frame_ins, reset_ins;
    uint64_t tick_cyc, frame_cyc;
    uint64_t frames, meshed, drawn;
    uint64_t reused, empty, checked, check_bad;   /* pv_frame_stats' */
    uint64_t tex_checked, tex_bad;                /* pv_frame_stats' texture copies verified (mesh_check) */
    uint64_t band_checked, band_bad;              /* and the keys' kept band hashes */
};
void view_times(struct view_set *vs, struct view_times *t);

#endif
