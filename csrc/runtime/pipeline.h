/* The pipeline: the RL product's two halves joined (SPEC.md's GPU section,
 * D1 hybrid and the pipeline Elliot set on 2026-09-28). The host's env pool
 * (pool.h) steps the worlds; after each step the environment's own view
 * (view.h: play's frame, a private copy per env) records its frame as data
 * (raster_obs.h: the terrain's meshes by section version, the atlas and its
 * animated tiles, the recorded entity passes); the device renderer
 * (csrc/cuda/render) keeps each environment's render state resident and
 * draws the frames, bit for bit the C renderer's, into one device buffer
 * uint8 [depth][N][h][w][3] that the trainer reads in place.
 *
 * The trainer's calls:
 *   pipe_make(cfg)                   N envs, the pool's config and the pipeline's parameters
 *   pipe_start_load(dir)             a start (pool_start_load)
 *   pipe_reset(ids, start)           envs to a start (an image copy; their views reloaded)
 *   pipe_step(ids, acts, n)          n ticks for any subset of the envs
 *   pipe_poll(b)                     the first b envs whose step and frame are done
 *
 * Inside: environments in groups, each group's render state resident on one
 * of S streams (a host thread and a CUDA stream, with its own renderer);
 * the host's workers step some envs while the streams render others; a
 * stream's render goes when B frames are queued, or when every env of its
 * groups still in flight has queued (nothing more can come), and takes
 * every frame queued by then (those that came during its last render). With
 * the device meshing a stream has two threads: its mesher takes the queued
 * frames that way and has the device mesh them (the mesher's own CUDA
 * stream), its drawer draws whatever the mesher has handed it, so a
 * frame's meshing overlaps the frames drawn before it. Frame k of
 * env e is written to buffer slot k mod depth, so the trainer may read a
 * result's observation until that env's next depth - 1 steps are polled.
 * The group size, S, B and depth are parameters; pipe_tune chooses them for
 * a configuration by a short sweep. Linux and CUDA (the dev host) only. */
#ifndef NETHERITE_RUNTIME_PIPELINE_H
#define NETHERITE_RUNTIME_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#include "pool.h"

struct pipe;

struct pipe_params {
    int group;                  /* envs per group (0: N / streams) */
    int streams;                /* S, render streams (0: N / 8 up to 16; N / 32 up to 4 with device_mesh) */
    int batch;                  /* B, the frames a render (with device_mesh, the mesher) waits for (0: 1);
                                 * it takes every frame queued */
    int depth;                  /* observation buffers per env (0: 2) */
};

struct pipe_config {
    struct pool_config pool;    /* n, threads, config, obs (w, h, rd, ticks), privileged, ...;
                                 * the view_* callbacks are the pipeline's */
    struct pipe_params par;
    int device;                 /* the CUDA device */
    int arena_mb;               /* each stream's triangle arena (0: 128) */
    const char *view_so;        /* NULL: playview.so beside the program */
    const char *assets;         /* the scene the view draws with (NULL: play's) */
    int draw;                   /* the gate: each frame also drawn by the C renderer as play draws it */
    int no_device;              /* frames recorded but not rendered (a host-only measurement;
                                 * refused with device_mesh, whose meshing it would move to the host) */
    int no_view;                /* no view and no frames: the pool alone (the tick's baseline) */
    /* where the sections are meshed (playview.h pv_config.device_mesh): 0
     * the host (each view), 1 the device (cuda/meshing: the views send their
     * section passes and the world bytes they read), 2 both, every device
     * mesh checked against the view's (the gate) */
    int device_mesh;
    /* the device mesher's table mode (cuda/meshing/table.h; the same
     * meshes): 1 on */
    int mesh_tab;
    /* 1: the renderer waits three times a render (the count's sizes, the
     * pairs', the frames) instead of once (render_one_trip: the steps past
     * the count sized on the device, bounded by the frames the renderer
     * has drawn; the default since lane/simclimb) */
    int usual_trip;
    /* the gate: every section pass the views' mesh key spares is meshed
     * anyway and compared (playview.h pv_config.mesh_check) */
    int mesh_check;
    /* the client world's light (SPEC.md D8): 0 run by the host's tick as
     * the engine runs it, 1 deferred to the device (engine/lightdefer.h: each
     * env's calls run by cuda/lighting on its own resident copy of the client
     * world's light, which comes back to the host's chunks at every sync:
     * before a client block write, a light read, and the frame) */
    int device_light;
    int light_slots;            /* the device's chunk slots per env (0: 512) */
    /* raw chunk generation on the device (pool.h pool_config.gen: every
     * stepping env's predicted generations in one round of csrc/cuda's
     * worldgen, on cfg.device); 0 the host's tick, as the engine runs
     * it (the default). pool.gen, when set, is used instead. */
    int gen_device;
    /* optional separate CUDA device for generation, encoded as index + 1;
     * zero uses device. The generator returns host bands, so no peer copy is
     * needed when the renderer lives on another card. */
    int gen_card;
    /* the generation server (runtime/gensrv.h, lane/gpuspec): gen_serve, a
     * Unix socket path: this process's generators (gen_device) also serve
     * the processes that connect there, every caller's rounds batched
     * together; gen_client, a path: this process generates on the server
     * there (no generators of its own; gen_device is implied); both, one
     * path: the pool generates through its own server's socket, the
     * clients' whole path in one process (pipe_gate --gen-serve) */
    const char *gen_serve, *gen_client;
    int gen_batchers;           /* gen_serve's rounds in flight at once (0: 2) */
    /* 0: the generation stream at the device's lowest priority, behind the
     * renderer's and the mesher's work (the default since lane/gpuspec: a
     * round is predicted ahead of the tick that needs it, and at high
     * priority a busy card's renders took twice as long); 1 its highest */
    int gen_high;
    /* the pool's env to worker map and the workers' CPUs (pool.h pin, place,
     * steal): 0 the pipeline's (pipe_make), 1 cfg.pool's as given (pin 0 and
     * PP_NONE: any worker steps any env, anywhere in the mask), 2 by cache
     * domain (PIPE_LAYOUT_DOMS), 3 any worker anywhere (PIPE_LAYOUT_ANY) */
    int pool_layout;
};

/* The observation on the device, as DLPack describes it: uint8, device
 * memory of cfg.device, shape [h][w][3], row-major (strides in elements). */
struct pipe_obs {
    unsigned char *data;        /* device address (NULL: no frame, see flags) */
    int64_t shape[3];
    int64_t strides[3];
    int device;
    int slot;                   /* its buffer slot (the depth index) */
};

struct pipe_result {
    const struct pool_result *r;    /* the step: HUD numbers, flags, events, err (pool.h) */
    int id;
    struct pipe_obs obs;
    int frame_rc;                   /* 0; -1: the view refused the frame (r->obs_rc), -2: the device refused it */
};

/* pipe_config.pool_layout. PIPE_LAYOUT_DOMS: with more than one cache
 * domain (L3) in the CPU mask, the streams a multiple of the domains (when
 * par.streams is left to the pipeline), stream s's threads on domain s mod
 * D and its envs stepped by that domain's workers (a queue a domain, pin 2,
 * PP_DOMS, stealing for the tail): a frame record is read by its stream
 * from the L3 its worker wrote it in, and an env's lines stay in one L3. */
enum { PIPE_LAYOUT_PIPE, PIPE_LAYOUT_GIVEN, PIPE_LAYOUT_DOMS, PIPE_LAYOUT_ANY };
/* A config.yaml's choices into cfg (config.yaml's header, engine/worldconf.h):
 * the observation (client.observation, ticks_per_step and hud, the world's
 * render_distance), the engine's device switches (device_mesh,
 * device_generation, device_light), and pool.config = path, so every start
 * is held to the file's world. A caller's own flags go on top. wc, when not
 * NULL, gets the file (its pipeline.start: wconf_start_path). 0, or -1 with
 * err. */
struct wconf;
int pipe_config_file(struct pipe_config *cfg, const char *path, struct wconf *wc, char *err, size_t n);

struct pipe *pipe_make(const struct pipe_config *cfg, char *err, size_t n);
void pipe_free(struct pipe *p);
int pipe_size(const struct pipe *p);

struct pool_start *pipe_start_load(struct pipe *p, const char *dir, char *err, size_t n);
int pipe_start_load_many(struct pipe *p, const char *const *dirs, int k, struct pool_start **out, char *err, size_t n);

/* envs ids[0..k) (idle: not stepping, their last result polled) to START */
int pipe_reset(struct pipe *p, const int *ids, int k, struct pool_start *start, char *err, size_t n);

/* A start made from env id as it is now (pool.h pool_start_from_env: needs
 * cfg.pool.save_slots; env id idle, its last result polled): an env reset
 * to it continues as env id does, its rows and its frames. Being made when
 * it returns (pipe_start_ready, pipe_start_wait; a reset to it waits). */
struct pool_start *pipe_start_from_env(struct pipe *p, int id, char *err, size_t n);
int pipe_start_ready(struct pipe *p, const struct pool_start *s);
int pipe_start_wait(struct pipe *p, struct pool_start *s, char *err, size_t n);

/* as pool_step: n ticks for each of ids[0..k), acts k (or k * n with
 * per_tick); an env must be idle. 0 on success. */
int pipe_step(struct pipe *p, const int *ids, int k, const struct pool_act *acts, int n, int per_tick);

/* Wait for b envs whose step and frame are done (or every env in flight,
 * when fewer are), their results in out; returns how many (0: none in
 * flight). A result is valid until its env's next pipe_step. */
int pipe_poll(struct pipe *p, int b, const struct pipe_result **out);
/* The same, but waiting only for min of them (or every env in flight, when
 * fewer are): every ready result up to b is taken (lane/rlasync: a closed
 * loop's groups on one card are polled as each finishes, not when the last
 * env in flight does). pipe_poll(b) is pipe_poll_some(b, b). */
int pipe_poll_some(struct pipe *p, int min, int b, const struct pipe_result **out);

/* The whole observation buffer: uint8 [depth][n][h][w][3] on the device. */
unsigned char *pipe_buffer(struct pipe *p, int64_t shape[5]);

/* The parameters in force, and changing them (every env idle: the streams
 * are made again, and the workers too when the config left them to the
 * pipeline; each env's render state is sent again at its next frame). */
void pipe_params_get(const struct pipe *p, struct pipe_params *par);
int pipe_params_set(struct pipe *p, const struct pipe_params *par, char *err, size_t n);

/* The startup sweep (the cheap autotuner): each candidate setting runs the
 * same work, every env reset to START (required) and stepped 2 steps
 * unmeasured, then STEPS measured, under a seeded random walk (the
 * product's shape of work; the same seeds for every candidate), and the
 * fastest in env-steps per second (the median of three rounds that
 * interleave the candidates) stays in force. Candidates: streams 1, 2, 4,
 * 8 and 16, each with batch 1 and all of a stream's envs (depth 2); with
 * the workers left to the pipeline (pool.threads 0) each candidate gets
 * its own count (pipe_params_set). The envs end at the last window's
 * state: reset them before measuring anything. The lines it prints go to
 * LOG (NULL: none); 0 on success. */
int pipe_tune(struct pipe *p, struct pool_start *start, int steps, FILE *log, char *err, size_t n);

/* Where the time went since the last pipe_stats_reset (wall ns summed over
 * the threads that spent it). */
struct pipe_stats {
    uint64_t steps, frames, renders;        /* env-steps polled, frames drawn, device renders */
    uint64_t tick_ns;                       /* the workers' steps, the view's hooks and frames included */
    double feed_ms;                         /* meshing feeds within the view's frame record */
    uint64_t view_tick_ns, view_frame_ns;   /* the view's share: its per-tick hooks, its frames */
    uint64_t render_ns;                     /* the streams' render calls (staging, upload, device, sync) */
    uint64_t mesher_ns;                     /* the streams' mesher threads' work (device_mesh: feeds, counts, writes) */
    double upload_ms, device_ms;            /* device time (CUDA events) inside the renders */
    uint64_t upload_bytes;
    uint64_t mesh_bytes, atlas_bytes, tex_bytes, quad_bytes;
    uint64_t fresh_meshes, mesh_resets;     /* sections uploaded; a stream's mesh arena started over */   /* of the upload: fresh meshes, atlas tiles, new textures, entity quads */
    uint64_t trips, trip_overs;             /* renders in one trip (usual_trip), and those run again the usual way */
    uint64_t stream_idle_ns;                /* the streams waiting for a batch (with device_mesh, their meshers) */
    uint64_t poll_wait_ns;                  /* the trainer waiting in pipe_poll */
    /* an env-step's wall time, summed over the env-steps polled: its step in
     * the pool (pipe_step to the collector), waiting for its stream to take
     * the frame, the stream's work until it is ready (meshing, drawing, the
     * frames taken with it), ready until polled, and polled until the
     * trainer stepped the env again */
    uint64_t lat_step_ns, lat_queue_ns, lat_render_ns, lat_poll_ns, lat_trainer_ns;
    uint64_t pool_instructions, pool_cycles; /* the workers' user instructions and cycles (the view's included) */
    uint64_t view_tick_ins, view_frame_ins; /* the view's share: its hooks, its frames */
    uint64_t view_tick_cyc, view_frame_cyc; /* their user cycles */
    uint64_t meshed, drawn;                 /* section passes the frames meshed, sections they drew */
    uint64_t mesh_reused, mesh_empty;       /* stale section passes that kept their mesh by key, given the empty mesh */
    uint64_t mesh_checked, mesh_check_bad;  /* mesh_check: kept and empty meshes compared, and those that differed */
    uint64_t tex_checked, tex_bad;          /* mesh_check: texture copies the recorder took back unchecked, compared anyway, and those that differed */
    uint64_t band_checked, band_bad;        /* mesh_check: the mesh keys' kept band hashes made again, and those that differed */
    uint64_t dmesh_requests;                /* section passes the device meshed (device_mesh) */
    uint64_t dmesh_checked, dmesh_diffs;    /* device meshes compared with the views' (2), and those that differed */
    uint64_t dmesh_upload_bytes;            /* the mesh feeds' bytes (of upload_bytes) */
    /* of those: band images, chunk records, flower pot and libm lists,
     * requests, copy lists and launch records (render.h) */
    uint64_t dmesh_band_bytes, dmesh_chunk_bytes, dmesh_list_bytes, dmesh_req_bytes, dmesh_ctl_bytes;
    uint64_t dmesh_band_run_bytes;          /* of the band images, sent as changed runs */
    double dmesh_ms;                        /* device time of the mesh passes (of device_ms) */
    uint64_t frames_failed;                 /* frames the device could not draw (a lost mesh: its view resyncs) */
    uint64_t device_bytes;                  /* the streams' renderers' device memory, and the observation buffer */
    /* device_light: the client light's syncs, the operations they ran (calls
     * and relight checks) and runs that failed */
    uint64_t light_syncs, light_ops, light_failed;
    uint64_t light_sync_write, light_sync_read, light_sync_frame;   /* the syncs by cause (lightdefer.h) */
};
void pipe_stats(struct pipe *p, struct pipe_stats *st);
void pipe_stats_reset(struct pipe *p);

/* For the gate: the pool, the views (view_render: env ID's C renderer), and
 * a frame back to the host. */
struct pool *pipe_pool(struct pipe *p);
/* cfg.gen_serve's server (gensrv.h), NULL without one */
struct gensrv *pipe_gensrv(struct pipe *p);
struct view_set *pipe_views(struct pipe *p);
int pipe_download(struct pipe *p, const struct pipe_obs *o, unsigned char *rgb);

#endif
