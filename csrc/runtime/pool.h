/* The env pool: the host half of the batched RL product (SPEC.md's GPU
 * section, D1: the C tick on the host as a pool of worlds, the observation
 * drawn on the device). N environments of the C engine in one process, each
 * its own struct env image (csrc/engine/env.h, image.h), stepped
 * asynchronously by a pool of worker threads.
 *
 *   pool_make(cfg)            N envs of one world (a config.yaml) and an observation spec
 *   pool_start_load(dir)      a snapshot or checkpoint directory, loaded once
 *                             into an env image (and a twin, which names the
 *                             image's pointers)
 *   pool_reset(ids, start)    each env becomes a copy of the start's image: the
 *                             image's pages copied and its pointers rebased
 *   pool_step(ids, acts, n)   n ticks for each env, queued to the workers
 *   pool_poll(b)              the first b envs whose step finished, with their
 *                             results
 *
 * Every env keeps its own randomness and order (the engine keeps no state
 * outside its env, csrc/tests/globals.sh), so what an env's step gives
 * never depends on the thread count or on which other envs run beside it:
 * csrc/runtime/pool_gate.c holds each env to test_snapshots' rows.
 *
 * Linux only (the images' address window, the worker threads); outside
 * native's `all` and the Mac build. */
#ifndef NETHERITE_RUNTIME_POOL_H
#define NETHERITE_RUNTIME_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../engine/session.h"

struct pool;
struct pool_start;
struct act;
struct pool_result;
struct pool_act;
struct pool_vmem;
struct raster_obs;

/* The observation spec (open decisions D2 to D5; the device renderer reads
 * it): the frame size, whether the HUD is in the image (0: hideGUI, the HUD
 * as numbers), the client's render distance, and n, the ticks per step. The
 * render feed covers the sections within rd chunks of the camera; an
 * agent-form act sets the client's option to rd (agent.h agent_act), so the
 * server's view distance follows it (tape-form acts keep their own). prec is
 * the renderer's precision (engine/raster_prec.h: 0 exact, the default and the
 * mode judged against Java; else the mask of the stages drawn fast), the
 * C renderer's and the device's alike. */
struct pool_obs {
    int w, h;
    int hud;
    int rd;
    int ticks;
    int prec;
    float gamma;                /* the client's gamma (config.yaml client.gamma; options.txt's): the frames' light */
};

struct pool_config {
    int n;                      /* environments */
    int threads;                /* worker threads (0: pool_cores, one a physical core) */
    /* the config.yaml every start must match (engine/worldconf.h
     * wconf_match_start: its world keys against the start's manifest
     * "world", its difficulty and render distance against the start's tape
     * header); NULL takes the first start's switches. The caller sets obs
     * and the rest from the same file (pipeline.h pipe_config_file). */
    const char *config;
    struct pool_obs obs;
    int privileged;             /* results carry struct pool_priv */
    int feed;                   /* results carry the render feed */
    /* dev tapes (agent-only builds): the dev ops' apply and end
     * (csrc/engine/dev.h); NULL refuses a dev start, as the product does */
    session_dev_apply_fn dev_apply;
    session_dev_end_fn dev_end;
    /* the region stores' spill file (csrc/engine/regionspill.h rspill_flag:
     * on, off or an absolute directory); NULL: on, the default directory */
    const char *region_spill;
    /* D + 1: every start's worlds at difficulty D (0 PEACEFUL .. 3 HARD)
     * from the head of an env's first server tick, where a Dev difficulty
     * entry at that tick would set it (MinecraftServer.func_147139_a);
     * 0 keeps the start's */
    int difficulty;
    /* called on the worker after each tick of env id at row t (the gate's
     * row records); may be NULL */
    void (*tick_hook)(void *ctx, int id, struct session *ss, int64_t t);
    void *tick_hook_ctx;
    /* called on the worker before each tick k of a step of env id (an
     * agent-form act src, hold_only on a one-act step's later ticks): it
     * may write the tick's whole act to out and return 1 (out runs as a
     * one-tick step's act: its presses, look and ops on this tick), or
     * return 0 (src runs as usual); may be NULL (the runtime's action
     * compiler, moveact.h) */
    int (*act_hook)(void *ctx, int id, struct session *ss, int k, const struct pool_act *src, int hold_only,
                    struct pool_act *out);
    void *act_hook_ctx;
    /* pin 1: env i always on worker i mod threads (a thread's hardware
     * counters are its own); pin 2: env i on the workers of worker i mod
     * threads' cache domain (place), any of them. With profile (and pin 1)
     * the phase profiler on in each env from its first step after a reset
     * (pool_profile_summary) */
    int pin, profile;
    /* where the workers run (enum pool_place), inside the CPU mask the
     * pool was made under (sched_getaffinity) */
    int place;
    /* with pin: a worker with nothing queued for it takes a job waiting on
     * another queue beyond that queue's idle workers, from its own cache
     * domain first (the corpus tail when envs do not divide evenly) */
    int steal;
    /* with pin 2: env i's cache domain, n of them, each an index of the
     * workers' domain queues (taken mod their number; PP_DOMS: the domains
     * of the mask in L3 order), read at each queued job, so its owner may
     * change it while no env is in flight; NULL: worker i mod threads'
     * domain (the pipeline's domain layout puts an env where its render
     * stream runs, pipeline.h pool_layout) */
    const int *env_dom;
    /* the observation's view (csrc/runtime/pipeline.c: play's frame per
     * env), each called on the worker with the env current and its image
     * heap on; NULL for none. view_reset after each reset (nonzero: the env
     * cannot be viewed, its steps fail), view_tick before (end 0) and after
     * (end 1) each tick, view_step at the step's end (it may set the
     * result's obs). */
    int (*view_reset)(void *ctx, int id, struct session *ss, char *err, size_t n);
    void (*view_tick)(void *ctx, int id, int64_t t, const struct act *a, int end);
    void (*view_step)(void *ctx, int id, struct pool_result *r);
    void *view_ctx;
    /* called on the worker at the end of each step of env id that ran
     * without an error (after view_step), with the env current: what the
     * caller reads of the env's state into its own memory (rlbind.c's
     * semantic camera, semcam.h); may be NULL */
    void (*step_hook)(void *ctx, int id, struct session *ss, struct pool_result *r);
    void *step_hook_ctx;
    /* a profiler's named range (NVTX under nsys): push 1 opens NAME on the
     * calling thread, push 0 closes the innermost; NULL for none */
    void (*range)(const char *name, int push);
    /* raw chunk generation shared by the envs through a step (engine/gencache.h:
     * the same chunks and provider states, made once per process): 0 a
     * cache of 1024 MB, a positive size in MB, negative none */
    int gencache_mb;
    /* raw chunk generation on a batch generator (engine/genahead.h: csrc/cuda's
     * worldgen, ga_gen_cuda_local_create, or the C engine's,
     * ga_gen_c_create): an env's step first predicts the generations its
     * ticks may ask for (genahead_predict), every waiting env's requests go
     * to the generator together in one batch (a chunk envs of one seed share
     * is generated once), and the env constructs its chunks before its ticks,
     * which take them (world_chunk_request); a generation the prediction
     * missed runs on the C provider in the tick. What an env computes is the
     * same either way. NULL, the default: the host generates in the tick (the
     * C provider through gencache_mb's cache). The generator is the caller's
     * (it outlives the pool); the pool calls it from one thread of its own. */
    const struct ga_gen *gen;
    /* 1: the generator's raw arrays even when it builds its chunks (ga_gen
     * run_built: the constructor and the sky map run where they were
     * generated, the envs copy bands), so the envs construct them */
    int gen_raw;
    /* rounds in flight at once (0: 2 when the generator can make another of
     * itself, ga_gen.another), each on a thread and a generator of its own:
     * a big round (a teleport's whole square) does not hold up the small
     * ones behind it */
    int gen_threads;
    /* 1: a step never waits for the round its last step asked for (only
     * the first step after a reset waits for its square): the prediction
     * reaches chunks the ticks take steps later, so the round finishes
     * off the step's path; a tick that asks for a chunk still in flight
     * has C generate it, and the step's end hands a finished round to the
     * cache before it predicts again (lane/gpuspec) */
    int gen_nowait;
    /* the prediction this many chunk rings wider (engine/genahead.h
     * genahead_set_ahead): with gen_nowait, a round in flight holds chunks
     * the ticks take steps later */
    int gen_ahead;
    /* the workers' hardware counter (pool_result.instructions,
     * pool_stats.instructions) not read around each step: two read
     * syscalls a step saved (pipe_bench reads it only with --profile) */
    int no_counters;
    /* pool_start_from_env: the saves made at once (0: none, and no env
     * keeps its acts). With it each env keeps the acts it ran since its
     * reset (the act log), and holds the start it was reset to. A save
     * replays an env's log twice from that start, each replay in an image
     * of its own with a view of its own: the view callbacks are called with
     * ids n + 2k and n + 2k + 1 for save slot k (the views beyond the
     * envs'). */
    int save_slots;
    /* the view's own memory, for a save and for a reset to a saved start
     * (NULL without a view): view_mem gives view id's library copy (its
     * mapping) and the writable data it holds; view_reload loads a fresh
     * copy for id without opening it (the reset writes the saved data into
     * it) and gives the same of it; view_resume makes the written copy the
     * view (open, its device meshes sent again). */
    int (*view_mem)(void *ctx, int id, struct pool_vmem *m);
    int (*view_reload)(void *ctx, int id, struct session *ss, struct pool_vmem *m, char *err, size_t n);
    void (*view_resume)(void *ctx, int id);
};

/* A view's library copy as pool_config.view_mem gives it: the mapping
 * [lo, hi) (pointers into it name the copy) and its writable data (the
 * segments the copy writes, without the parts the loader made read-only). */
#define POOL_VMEM_SEGS 4
struct pool_vmem {
    uintptr_t lo, hi;
    int nseg;
    struct { uintptr_t addr; size_t len, init; } seg[POOL_VMEM_SEGS];   /* init: the bytes the file sets (the rest load as zeros) */
};

/* The workers' CPUs (pool_config.place). A cache domain is the CPUs that
 * share an L3 (sysfs cache/index3; the GPU host: 8 cores and their siblings each).
 * Each worker but PP_DOMAIN's gets one CPU of the mask, worker k the k-th of
 * the order (wrapping). */
enum pool_place {
    PP_NONE = 0,    /* anywhere in the mask (the scheduler's choice) */
    PP_CORES,       /* a core's first thread, domain after domain, then the second threads */
    PP_SIBLINGS,    /* both threads of a core before the next core, domain after domain */
    PP_SPREAD,      /* a core's first thread, the domains in turn (worker k in domain k mod D) */
    PP_DOMAIN,      /* the domain PP_CORES gives the worker, any of its CPUs in the mask */
    PP_DOMS,        /* worker k on the domains in turn (k mod D, in L3 order), any of its CPUs:
                     * each domain the same workers, give or take one */
};
const char *pool_place_name(int place);
int pool_place_parse(const char *s);

/* ------------------------------------------------------------------ acts */

/* The agent's keys (Act.java SHORT): a hold is a bit, a press a count. */
enum pool_key {
    PK_FORWARD, PK_BACK, PK_LEFT, PK_RIGHT, PK_JUMP, PK_SNEAK, PK_SPRINT, PK_ATTACK, PK_USE, PK_DROP,
    PK_INVENTORY, PK_PICK, PK_HOTBAR1, PK_HOTBAR9 = PK_HOTBAR1 + 8, PK_KEYS
};

/* A gui op (Act.applyGui): each runs only under its screen, else it is
 * refused (dropped; the result counts it). */
enum pool_gui_kind { PG_CLICK = 1, PG_CLOSE, PG_RESPAWN, PG_WAKE, PG_TRSEL };
struct pool_gui_op {
    int kind;
    int window, slot, button, mode;   /* PG_CLICK (window < 0: the open window) */
    int index;                        /* PG_TRSEL: the recipe index */
};

#define POOL_GUI_OPS 8

enum pool_act_kind {
    PA_AGENT = 0,               /* the agent form below */
    PA_TAPE,                    /* a parsed tape-form act (tape points at it) */
    PA_ROW,                     /* a tape row's JSON text (row), parsed on the worker */
};

enum pool_look { PL_NONE, PL_LOOK, PL_DLOOK };

/* One tick's act. The agent form is what a person at the vanilla client can
 * do (Act.java): holds and presses, an absolute look (yaw, pitch and
 * optionally the previous pair) or a look delta, a hotbar slot, the control
 * key, and gui ops. A step whose hotbar is outside 0..8 or whose pitch is
 * outside -90..90 is refused whole (no tick, the result's refused flag). */
struct pool_act {
    int kind;
    const struct act *tape;     /* PA_TAPE */
    const char *row;            /* PA_ROW */
    uint32_t hold;              /* 1 << enum pool_key */
    uint8_t press[PK_KEYS];
    int look_mode;              /* enum pool_look */
    int look_prev;              /* PL_LOOK: look[2..3] is the previous yaw and pitch */
    float look[4];
    int hotbar;                 /* -1: unchanged */
    int ctrl;
    int nops;
    struct pool_gui_op ops[POOL_GUI_OPS];
};

/* ---------------------------------------------------------------- results */

struct pool_stack { int16_t item, count, damage; };

/* What the vanilla HUD and screens show. */
struct pool_hud {
    float health;
    int food, armour, air;      /* air: 300 full, as the bubbles read it */
    int xp_level;
    float xp_bar;
    int selected;               /* the hotbar slot */
    struct pool_stack hotbar[9];
    /* the open screen: 0 none, then enum pool_screen; its slots as the
     * client's container has them, and the cursor */
    int screen;
    int nslots;
    struct pool_stack slots[90];
    struct pool_stack cursor;
};

enum pool_screen { PS_NONE, PS_INVENTORY, PS_CRAFTING, PS_CHEST, PS_FURNACE, PS_MERCHANT, PS_DISPENSER, PS_HOPPER,
                   PS_GAMEOVER, PS_SLEEP, PS_CHAT, PS_CREDITS };

/* A stat the client's StatFileWriter mirror moved during the step (S37s), by
 * the engine's stat index (survival.h), with its value before and after. */
struct pool_event { int32_t stat, before, after; };
#define POOL_EVENTS 64

/* Privileged state (cfg.privileged): for reward shaping, not an observation. */
struct pool_priv {
    double x, y, z;             /* the server player */
    float yaw, pitch;
    int dim;
    float health, saturation, exhaustion;
    int food, xp_total, air, fire, on_ground;
    int64_t world_time, total_time;
};

enum pool_flag {
    PF_DEAD = 1,                /* the player is dead (the game-over screen): the env keeps running */
    PF_DIED = 2,                /* the player died during this step */
    PF_REFUSED = 4,             /* the step was refused whole (a per-tick schedule: from the refused
                                 * tick's act on): no tick ran for it; ticks says how many did */
    PF_OPS_REFUSED = 8,         /* one or more gui ops were refused (dropped) */
    PF_DIM = 16,                /* the dimension changed during the step */
    PF_ERROR = 32,              /* the engine stopped (err says why): reset the env */
};

struct pool_result {
    int id;
    int flags;
    int ticks;                  /* ticks this step ran */
    int64_t t;                  /* the next row */
    int ops_refused;
    uint64_t instructions, ns;  /* the worker's user instructions and wall time for the step */
    char err[640];
    struct pool_hud hud;
    int nevents;
    struct pool_event events[POOL_EVENTS];
    struct pool_priv priv;      /* cfg.privileged */
    /* the render feed (cfg.feed): feed_bytes at feed, one block of plain data
     * (struct pool_feed and what its offsets name), valid until the env's
     * next step or reset */
    const void *feed;
    size_t feed_bytes;
    /* the view's frame (cfg.view_step): the device renderer's input, valid
     * until the env's next step or reset; NULL for none */
    const struct raster_obs *obs;
    int obs_rc;
};

/* --------------------------------------------------------- the render feed
 *
 * For the device renderer: everything it needs to redraw an env after a
 * step, as plain data (no pointers; offsets from the block's start). The
 * camera at partial tick 1.0 (the player and world inputs of the frame's
 * render state, renderstate.h struct rs_in: the carried EntityRenderer state
 * is the renderer's own), the sections whose blocks or light changed since
 * the last feed, the chunks the client loaded (each with all its sections,
 * the burst a load or a dimension change makes) and unloaded, and the
 * entity draw list. */
struct pool_camera {
    double x, y, z;             /* the client player at pt 1.0 */
    float yaw, pitch;
    float y_offset;
    float health;
    int hurt, max_hurt, death;
    float portal;
    int sleeping, bed_rot;
    int confusion, night_vision, blindness;
    int brightness;             /* Entity.getBrightnessForRender's packed light */
    int view_material;          /* 0 air, 1 water, 2 lava at the eye */
    int dim;
    int64_t world_time;         /* WorldClient's day time */
    float rain, thunder;
    int biome;                  /* the biome at the player's column */
};

/* A section's cells in the engine's band order, index x << 8 | z << 4 | y
 * (world.h CELL_IN_SEC); the nibble arrays two cells a byte, the even index
 * in the low nibble. 14,352 bytes. */
struct pool_feed_section {
    int32_t cx, sy, cz;
    int32_t empty;              /* 1: the section is gone (the chunk has none); its cells are zero */
    uint16_t ids[4096];
    uint8_t meta[2048], sky[2048], block[2048];
};

/* A chunk the client's range gained (with count sections from first in the
 * load-section list), or one whose height map, section mask or biomes
 * changed (count 0: its changed sections are in the section list). */
struct pool_feed_chunk {
    int32_t cx, cz;
    int32_t first, count;
    uint8_t biome[256];         /* index z << 4 | x */
    int32_t height[256];        /* the height map, index z << 4 | x */
};

struct pool_feed_ent {
    int32_t id, kind;           /* the client's entity id; its class (clientents) */
    double x, y, z;
    float yaw, pitch, head_yaw;
    int32_t flags;              /* 1 burning, 2 sneaking, 4 invisible, 8 child */
    int32_t hurt, death;
    float limb_swing, limb_amount;
    struct pool_stack item;     /* an item entity's stack, a holder's held item */
};

struct pool_feed {
    uint32_t bytes;             /* the whole block */
    uint32_t nsections, nloads, nload_sections, nunloads, nents;
    uint32_t off_sections, off_loads, off_load_sections, off_unloads, off_ents;
    uint32_t reset;             /* 1: the first feed after a reset or a dimension change: drop everything held */
    struct pool_camera cam;
};

/* ------------------------------------------------------------------- API */

struct pool *pool_make(const struct pool_config *cfg, char *err, size_t n);
void pool_free(struct pool *p);
int pool_size(const struct pool *p);
int pool_threads(const struct pool *p);
/* The workers made again, K of them (every env idle: no step, reset or load
 * in flight). 0 on success. */
int pool_threads_set(struct pool *p, int k, char *err, size_t n);
/* The physical cores the process may run on: one for each core with a CPU
 * in its CPU mask (sysfs thread_siblings_list), the CPUs in the mask where
 * sysfs does not say. The workers' default: at 128 envs on the GPU host (64 cores,
 * 128 CPUs) the tick is bound by memory, and a worker on every hardware
 * thread took 1.4 to 2.1 times the CPU an env-step for fewer env-steps
 * (lane/pagefault). */
int pool_cores(void);
/* The cache domains (L3s) of the calling thread's CPU mask, and the calling
 * thread moved onto the CPUs of the d-th of them (in L3 order, d taken mod
 * their number): 0 on success. PP_DOMS's workers of domain queue d run
 * there. */
int pool_domains(void);
int pool_domain_pin(int d);
/* the generation cache's counts (zeros without one) */
struct gencache_stats;
void pool_gencache_stats(const struct pool *p, struct gencache_stats *s);

/* cfg.gen: the batches, and every env's cache (engine/genahead.h ga_stats,
 * summed over the envs and their resets) */
struct pool_gen_stats {
    uint64_t batches;           /* generator rounds (each one or more calls of max_batch) */
    uint64_t steps;             /* env steps that asked for generations */
    uint64_t requests, unique;  /* the envs' requests, and those generated (one per chunk and seed) */
    uint64_t kept;              /* unique requests an earlier round's result served */
    uint64_t failed;            /* requests in a round the generator dropped */
    uint64_t max_unique;        /* the largest round */
    uint64_t gen_ns;            /* the generator's calls, wall */
    uint64_t waits, wait_ns;    /* steps that waited for a round, time from wait to ready */
    uint64_t hits, misses;      /* ticks' generations served from the caches, or by C */
    uint64_t owed_hits, owed_misses;
    uint64_t generated, wasted; /* chunks constructed ahead; dropped unused */
    uint64_t predict_ins, construct_ins, construct_ns;
    uint64_t miss_ring[3][8];   /* ga_stats': misses by distance from the player's chunk */
    /* now: the host memory of the results held (the envs' caches' references
     * and the kept ones), of the free lists, and the kept results' count */
    uint64_t res_bytes, free_bytes, kept_now;
    uint64_t miss_other_dim;
};
void pool_gen_stats(struct pool *p, struct pool_gen_stats *s);

/* A start: dir is a snapshot directory (a recording's start or a
 * checkpoint's, with its tape.jsonl header). NULL with err on failure. The
 * loads run on the pool's workers, beside any steps in flight
 * (pool_start_load_many loads several at once). */
struct pool_start *pool_start_load(struct pool *p, const char *dir, char *err, size_t n);
int pool_start_load_many(struct pool *p, const char *const *dirs, int k, struct pool_start **out, char *err, size_t n);
int64_t pool_start_tick(const struct pool_start *s);
/* The caller's start let go: its image goes once no env holds it (with
 * cfg.save_slots an env holds the start it was reset to until its next
 * reset, and a save being made holds its env's; without, a start no env is
 * being reset to any more may go, and envs reset to it keep running). */
void pool_start_free(struct pool_start *s);

/* A start made from env id as it is now (idle: no step in flight, its last
 * result polled or not), so that an env reset to it continues exactly as
 * env id does from here: the same rows and the same frames for the same
 * acts (the start gate, start_gate.c). Needs cfg.save_slots. The env is not
 * touched: its act log since its reset is copied, and two replays of it
 * from the env's start (each in a new image, with a view of its own, run on
 * the workers beside any steps) make the start's image and its twin, as a
 * load makes them. Returns at once with the start being made
 * (pool_start_ready, pool_start_wait; pool_reset to it waits), or NULL with
 * err: the env errored since its reset, keeps no log, or every save slot
 * is busy (try again after one is made). The caller frees it as any start.
 * Memory per start: its image's nonzero pages (pool_start_info), the
 * records its region spill file holds, and its view's data. */
struct pool_start *pool_start_from_env(struct pool *p, int id, char *err, size_t n);
/* 1 made, 0 being made, -1 failed */
int pool_start_ready(struct pool *p, const struct pool_start *s);
/* wait until s is made: 0, or -1 with err (why the save failed) */
int pool_start_wait(struct pool *p, struct pool_start *s, char *err, size_t n);
/* The start image's size and what a reset copies. */
struct pool_start_info {
    size_t image_pages, image_bytes;    /* nonzero pages copied per reset */
    size_t pointers;                    /* words rebased per reset */
    size_t outside;                     /* differing words naming memory outside the image (should be 0) */
    size_t value_diffs;                 /* words that differ between the image and its twin that are not pointers
                                         * (a save's: values each replay makes its own, the first's kept) */
    double load_ms;                     /* the load (the image, its twin and the scan); a save's replays and scan */
    /* a saved start (pool_start_from_env): the replays' steps and ticks and
     * the log's bytes; the image's words naming the view's library and the
     * view's data (bytes, its words rebased); the region spill file's bytes */
    int64_t replay_steps, replay_ticks;
    size_t log_bytes;
    size_t lib_pointers;
    size_t view_bytes, view_pointers;
    size_t view_diffs;                  /* the view's words that differ between the replays, not pointers */
    size_t spill_bytes;
};
void pool_start_info(const struct pool_start *s, struct pool_start_info *info);

/* Reset envs ids[0..k) to the start (in parallel on the workers); every env
 * must be idle (not stepping). 0 on success. */
int pool_reset(struct pool *p, const int *ids, int k, struct pool_start *s, char *err, size_t n);

/* Queue a step of n ticks for envs ids[0..k); acts holds k acts (one per
 * env, its holds kept over the n ticks and the rest on the first tick only,
 * as Act.holdOnly) or, with per_tick, k * n (env-major). The acts are copied
 * (a PA_TAPE act's struct and a PA_ROW row must live until the step is
 * polled). 0 on success; an env already stepping is an error. */
int pool_step(struct pool *p, const int *ids, int k, const struct pool_act *acts, int n, int per_tick);

/* Wait for up to b finished envs (at least one while any is stepping);
 * their results in out (b entries). Returns how many, 0 when none is
 * stepping. */
int pool_poll(struct pool *p, int b, const struct pool_result **out);

/* Wait until at least one env finished (while any is stepping), then take
 * up to MAX of those finished; 0 when none is stepping or finished. */
int pool_poll_some(struct pool *p, int max, const struct pool_result **out);

/* The env's session (the engine's state: session.h), for a caller that
 * reads it between steps; NULL for a bad id. */
struct session *pool_session(struct pool *p, int id);
struct env *pool_env(struct pool *p, int id);

/* Per-env memory: the bytes an env's image has resident (pages written). */
size_t pool_env_resident(struct pool *p, int id);
/* The same by owner (envmem.h), into *m; 0 when the env has no session. */
struct envmem;
int pool_env_mem(struct pool *p, int id, struct envmem *m);

/* The user instructions and cycles (hardware counters) and wall time the
 * workers spent stepping, summed; the steps and ticks they ran. */
struct pool_stats {
    uint64_t instructions, cycles, ns, steps, ticks, resets;
    uint64_t reset_ns, reset_instructions;
};
void pool_stats(struct pool *p, struct pool_stats *st);

/* cfg.profile: env id's phase profile since its last reset (phaseprof.c's
 * summary; rows, if not NULL, gets a line per row). */
void pool_profile_summary(struct pool *p, int id, FILE *to, FILE *rows);
/* cfg.profile: env id's profiled rows and their user instructions, the
 * whole rows' and each sub-mark's (phase.h phase_profile_totals) */
size_t pool_profile_totals(struct pool *p, int id, uint64_t *rows_ins, uint64_t *sub_ins);

#endif
