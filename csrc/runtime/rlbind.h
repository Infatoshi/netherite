/* The trainer's binding (lane/rlbind): the pipeline (pipeline.h) as a shared
 * library a Python trainer loads in its own process (out/runtime/libnwrl.so,
 * rlbind.mk), with a small C API of plain structs that ctypes mirrors.
 *
 *   nwrl_make(opts)          N envs of one config.yaml, the start loaded, every env reset to it
 *   nwrl_start_load(dir)     one more start (its index)
 *   nwrl_reset(ids, start)   envs to a start
 *   nwrl_step(ids, acts)     one step (the config's ticks) for any idle envs
 *   nwrl_poll(b, out)        the first b envs whose step and frame are done: their results
 *   nwrl_obs(shape)          the observation buffer, uint8 [depth][n][h][w][3] on the
 *                            render card; a result's frame is [slot][id] (DLPack in Python,
 *                            no host copy)
 *
 * Every result carries the engine's own state after the step (struct
 * nwrl_state, read on the worker after the step's last tick): the server
 * player's inventory, the block the client's crosshair is on, the open
 * window, the achievements, and the chests of the loaded chunks near the
 * player (the reward's privileged part, never an observation). Nothing in
 * it changes what the engine computes: the rows and frames are pipe_bench's
 * (the binding's gate, rlbind.mk rl-gate). Linux and CUDA only. */
#ifndef NETHERITE_RUNTIME_RLBIND_H
#define NETHERITE_RUNTIME_RLBIND_H

#include <stdint.h>

#define NWRL_VERSION 1

struct nwrl;

struct nwrl_opts {
    const char *config;         /* the config.yaml (required) */
    const char *start;          /* the start's snapshot directory (NULL: the config's pipeline.start) */
    const char *view_so;        /* playview.so (NULL: beside libnwrl.so) */
    int n;                      /* envs */
    int device;                 /* the render card (the process's CUDA ordinal) */
    int threads;                /* the pool's workers (0: the pipeline's) */
    int streams, batch, group, depth;   /* pipe_params (0: the pipeline's) */
    int device_mesh;            /* -1: the config's; 0, 1 or 2 */
    int chest_radius;           /* the chest scan's radius in chunks around the player (0: 8) */
    int chest_every;            /* ticks between chest scans (0: 10; a window change scans at once) */
};

/* One step's act (pool.h struct pool_act's agent form) */
struct nwrl_op {
    int32_t kind;               /* pool.h enum pool_gui_kind */
    int32_t window, slot, button, mode, index;
};
#define NWRL_KEYS 21            /* pool.h PK_KEYS */
#define NWRL_OPS 8              /* pool.h POOL_GUI_OPS */
struct nwrl_act {
    uint32_t hold;              /* 1 << enum pool_key */
    uint8_t press[NWRL_KEYS];
    int32_t look_mode;          /* 0 none, 1 absolute (look[0..1] yaw, pitch), 2 deltas */
    float look[2];
    int32_t hotbar;             /* -1 unchanged */
    int32_t nops;
    struct nwrl_op ops[NWRL_OPS];
};

struct nwrl_stack { int16_t item, count, damage, pad; };

#define NWRL_CHESTS 64
struct nwrl_state {
    struct nwrl_stack inv[36];  /* the server player's main inventory (0..8 the hotbar) */
    int32_t current;            /* the selected hotbar slot */
    int32_t window;             /* the open window's id (0: none or the player's own inventory) */
    int32_t gui_x, gui_y, gui_z;
    int32_t mo_hit;             /* the client's crosshair: 1 on a block */
    int32_t mo_x, mo_y, mo_z, mo_side, mo_block, mo_meta;
    int32_t eye_water;          /* the client player's eye in water */
    uint64_t ach;               /* bit a: achievement a (survival.h ACH_*) unlocked (server's stats) */
    int32_t mined_logs;         /* stat.mineBlock of the two log blocks (17, 162) */
    int32_t crafted;            /* stat.craftItem summed */
    /* the chests: every chest the scans found in this env since its reset
     * (positions in the player's dimension); nearest* is the nearest one
     * not yet opened (-1: none known) */
    int32_t chests_known, chests_opened, chest_items_taken;
    int32_t nearest;            /* its index in chest[] */
    float nearest_dist;         /* from the player's eye to its centre */
    int32_t nchest;
    struct { int32_t x, y, z, opened, items0, items, dim, pad; } chest[NWRL_CHESTS];
};

struct nwrl_result {
    int32_t id, flags, ticks, frame_rc;
    int64_t t;
    int32_t slot;               /* the observation's buffer slot (frame [slot][id]) */
    int32_t ops_refused;
    /* pool.h struct pool_priv */
    double x, y, z;
    float yaw, pitch;
    int32_t dim;
    float health, saturation, exhaustion;
    int32_t food, xp_total, air, fire, on_ground, pad0;
    int64_t world_time, total_time;
    /* pool.h struct pool_hud (the screen's slots as the client's container has them) */
    int32_t selected, screen, nslots, xp_level;
    struct nwrl_stack hotbar[9];
    struct nwrl_stack slots[90];
    struct nwrl_stack cursor;
    int32_t nevents, pad1;
    struct { int32_t stat, before, after; } events[64];
    struct nwrl_state st;
    char err[256];
};

int nwrl_version(void);
struct nwrl *nwrl_make(const struct nwrl_opts *o, char *err, int en);
void nwrl_free(struct nwrl *h);
int nwrl_size(const struct nwrl *h);
/* the observation's size and the config's ticks a step */
void nwrl_obs_spec(const struct nwrl *h, int *w, int *hh, int *ticks, int *device);
/* the whole observation buffer (device address) and its shape */
void *nwrl_obs(struct nwrl *h, int64_t shape[5]);
/* a start: its index (0 is the one nwrl_make loaded), -1 with err */
int nwrl_start_load(struct nwrl *h, const char *dir, char *err, int en);
int nwrl_reset(struct nwrl *h, const int *ids, int k, int start, char *err, int en);
/* A start saved from env id as it is now (lane/savestart; the library made
 * by nwrl_make_ex with NWRL_SAVE, rlgui.h): its index, or -1 with err (the
 * env in flight or errored since its reset, every save slot busy: try
 * again later). Env id must be idle (its last result polled) and is not
 * touched. The start is made in the background (pipeline.h
 * pipe_start_from_env: two replays of the env's acts since its reset);
 * nwrl_start_ready says when, nwrl_start_wait waits, nwrl_reset to it
 * waits. An env reset to it continues as env id would have: the same rows
 * and frames for the same acts, and env id's binding state then (the
 * chests found and opened, the scan's clock); the action compilers start
 * over (rlact.h, moveact.h: no queued macro). */
int nwrl_start_save(struct nwrl *h, int id, char *err, int en);
/* 1 made, 0 being made, -1 failed or no such start */
int nwrl_start_ready(struct nwrl *h, int start);
int nwrl_start_wait(struct nwrl *h, int start, char *err, int en);
/* the start let go (its memory goes once no env holds it); its index is
 * free for the next start */
void nwrl_start_free(struct nwrl *h, int start);
/* the start's tick (-1: no such start) */
int64_t nwrl_start_tick(struct nwrl *h, int start);
/* its memory and making: out[0] the image's bytes a reset copies, [1] the
 * view's data, [2] its region spill file's records, [3] the act log
 * replayed, [4] the ticks replayed, [5] the make's ms (a load's for a
 * loaded start), [6] the image's pointers, [7] its tick. 0, or -1 for no
 * such start. */
int nwrl_start_info(struct nwrl *h, int start, double out[8]);
/* one step for ids[0..k) with acts[0..k); 0 on success */
int nwrl_step(struct nwrl *h, const int *ids, int k, const struct nwrl_act *acts);
/* results of up to b envs (at least one while any is in flight); how many */
int nwrl_poll(struct nwrl *h, int b, struct nwrl_result *out);
/* the same, returning once min of them are done: every ready result up to b
 * (lane/rlasync: nwrl_poll waits for every env in flight when fewer than b
 * are, which ties a card's groups together) */
int nwrl_poll_some(struct nwrl *h, int min, int b, struct nwrl_result *out);
/* The semantic camera (csrc/runtime/semcam.h; the config's client.camera
 * semantic, which runs no renderer and has no frames, or both): host memory
 * [n][h][w] of 8-byte cells, four uint16 words each (block id | meta << 12,
 * the distance in 1/256 blocks, 0xffff a miss; the face, 0..5 or 6 a miss, 7
 * the void, with the flags << 8; 0), env id's written by its step's worker
 * before its result is polled and valid until its next step; shape n, h, w,
 * 4. NULL with h and w 0 when the camera is off. */
void *nwrl_semantic(struct nwrl *h, int64_t shape[4]);
/* env id's last frame to the host (w * h * 3 bytes; the gate's cross-check) */
int nwrl_download(struct nwrl *h, int id, int slot, unsigned char *rgb);
/* The loot chests the seed's structures place in chunks cx0..cx1, cz0..cz1
 * (the engine's structure_walk, vanilla's placement and piece layout, no
 * terrain generated): the village blacksmith's chest (kind 1, a village whose
 * start is valid), the desert temple's four (2) and the jungle temple's two
 * (3). Their y is the piece's before it settles on the ground (exact x and z;
 * y within a few blocks for the village, the temples' chests lie 11 and 3
 * blocks under their floor). Up to max into out; how many there are. */
struct nwrl_site { int32_t x, y, z, kind; };
int nwrl_structure_chests(int64_t seed, int cx0, int cz0, int cx1, int cz1, struct nwrl_site *out, int max);
/* The chests of a seed's world as the server holds it before its first tick
 * (seedworld.h: the spawn point and the 625 chunks around it, generated and
 * populated: village blacksmiths, temples and dungeons there, exact
 * positions, kind 4), and the spawn point into spawn[3] (x, y, z). Up to max
 * into out; how many there are. */
int nwrl_seed_chests(int64_t seed, int64_t spawn[3], struct nwrl_site *out, int max);
/* Env id's client world (what its player has seen) in the box x0..x1,
 * y0..y1, z0..z1: block ids, x fastest then z then y, -1 where its chunk is
 * not loaded. The env must be idle; the world is only read (no chunk is made
 * for a missing one). 0, or -1 for a bad id or box. */
int nwrl_blocks(struct nwrl *h, int id, int x0, int y0, int z0, int x1, int y1, int z1, int16_t *out);
/* The 1.7.10 recipes (engine/recipes.h, generated from the oracle's
 * CraftingManager and FurnaceRecipes): kind 0 shaped, 1 shapeless, 2 the
 * furnace; the output stack and up to 9 ingredient stacks (damage 32767:
 * any). The special recipes (dyed armour, book, map and firework copies) are
 * left out. Up to max into out; how many there are. */
struct nwrl_recipe {
    int16_t kind, out_item, out_count, out_damage, n, pad;
    int16_t in_item[9], in_damage[9];
};
int nwrl_recipes(struct nwrl_recipe *out, int max);
/* FNV-1a over n bytes from h (pipe_bench's digests) */
uint64_t nwrl_fnv(uint64_t h, const void *p, int64_t n);
/* the pipeline's stats since the last call: env-steps, frames, the time the
 * trainer waited in poll (ns), the workers' step time (ns) */
void nwrl_stats(struct nwrl *h, uint64_t out[8]);
/* nwrl_stats' eight, then an env-step's wall time summed over the env-steps
 * polled (pipeline.h struct pipe_stats lat_*: its step in the pool, waiting
 * for its stream, the stream's work, ready until polled, polled until stepped
 * again), the renders' device and upload time (ns) and the bytes uploaded */
void nwrl_stats_ext(struct nwrl *h, uint64_t out[16]);
/* Env id's memory (envmem.h, as pipe_bench --mem-every reports it): bytes by
 * owner into out[0..8] (server chunks, server worlds, region, client,
 * renderer, entities, ticks, scratch, other), its image heap's bytes used and
 * live blocks (out[9], out[10]) and the owners' total (out[11]); items, when
 * not NULL, gets one "owner\twhat\tbytes" line a part. The env must be idle.
 * 0, or -1 for a bad id. */
int nwrl_env_mem(struct nwrl *h, int id, double out[12], char *items, int len);

#endif
