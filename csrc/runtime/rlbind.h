/* The trainer's binding (lane/rlbind): the pipeline (pipeline.h) as a shared
 * library a Python trainer loads in its own process (out/runtime/libneth.so,
 * rlbind.mk), with a small C API of plain structs that ctypes mirrors.
 *
 *   neth_rl_make(opts)          N envs of one config.yaml, the start loaded, every env reset to it
 *   neth_rl_start_load(dir)     one more start (its index)
 *   neth_rl_reset(ids, start)   envs to a start
 *   neth_rl_step(ids, acts)     one step (the config's ticks) for any idle envs
 *   neth_rl_poll(b, out)        the first b envs whose step and frame are done: their results
 *   neth_rl_obs(shape)          the observation buffer, uint8 [depth][n][h][w][3] on the
 *                            render card; a result's frame is [slot][id] (DLPack in Python,
 *                            no host copy)
 *
 * Every result carries the engine's own state after the step (struct
 * neth_rl_state, read on the worker after the step's last tick): the server
 * player's inventory, the block the client's crosshair is on, the open
 * window, the achievements, and the chests of the loaded chunks near the
 * player (the reward's privileged part, never an observation). Nothing in
 * it changes what the engine computes: the rows and frames are pipe_bench's
 * (the binding's gate, rlbind.mk rl-gate). Linux and CUDA only. */
#ifndef NETHERITE_RUNTIME_RLBIND_H
#define NETHERITE_RUNTIME_RLBIND_H

#include <stdint.h>

#include "walkto.h"

#define NWRL_VERSION 14       /* 2: neth_rl_state's lq_* (lane/liquidlook); 3: neth_rl_opts.region_spill (lane/rlspill);
                                 * 4: neth_rl_state's mo_entity, mob_kills and vis (lane/fight); 5: seen (lane/seen);
                                 * 6: seen's last block the log, not coal ore (lane/woodseek);
                                 * 7: neth_vec_opts.explore_weight and explore_cap (lane/explore);
                                 * 8: engine.raycast device, neth_vec_cast and the bufs' obs_device (lane/rcdev);
                                 * 9: neth_vec_episode.food (lane/food);
                                 * 10: neth_vec_opts.picks, the bufs' picks and mask (lane/picks);
                                 * 11: neth_vec_opts.start_window and start_uses, neth_rl_start_load_bg (lane/seedwin);
                                 * 12: neth_vec_opts.end_weight, dragon_weight (lane/dragon); 13: late_scalars
                                 * (lane/starts; neither bumped this number then);
                                 * 14: neth_vec_opts.macro_opts, neth_vec_mopt_check and _list (lane/macroopts) */

struct nwrl;

struct neth_rl_opts {
    const char *config;         /* the config.yaml (required) */
    const char *start;          /* the start's snapshot directory (NULL: the config's pipeline.start) */
    const char *view_so;        /* playview.so (NULL: beside libneth.so) */
    int n;                      /* envs */
    int device;                 /* the render card (the process's CUDA ordinal) */
    int threads;                /* the pool's workers (0: the config's performance.workers) */
    int streams, batch, group, depth;   /* pipe_params (0: the config's performance section) */
    int device_mesh;            /* -1: the config's; 0, 1 or 2 */
    int chest_radius;           /* the chest scan's radius in chunks around the player (0: 8) */
    int chest_every;            /* ticks between chest scans (0: 10; a window change scans at once) */
    /* the region stores' spill (pool.h region_spill: on, off or an absolute
     * directory; NULL: the config's performance.region_spill, whose auto is
     * on, ~/.cache/netherite-spill). A tmpfs directory keeps
     * the parked chunks off the disk (lane/rlspill: at 1024 envs the spill
     * wrote 24 MB/s to the GPU host's disk while holding about 200 MB) */
    const char *region_spill;
};

/* One step's act (pool.h struct pool_act's agent form) */
struct neth_rl_op {
    int32_t kind;               /* pool.h enum pool_gui_kind */
    int32_t window, slot, button, mode, index;
};
#define NETH_RL_KEYS 21            /* pool.h PK_KEYS */
#define NETH_RL_OPS 8              /* pool.h POOL_GUI_OPS */
struct neth_rl_act {
    uint32_t hold;              /* 1 << enum pool_key */
    uint8_t press[NETH_RL_KEYS];
    int32_t look_mode;          /* 0 none, 1 absolute (look[0..1] yaw, pitch), 2 deltas */
    float look[2];
    int32_t hotbar;             /* -1 unchanged */
    int32_t nops;
    struct neth_rl_op ops[NETH_RL_OPS];
};

struct neth_rl_stack { int16_t item, count, damage, pad; };

#define NETH_RL_CHESTS 64
#define NETH_RL_VIS 4              /* neth_rl_state's livings in view (lane/fight) */
#define NETH_RL_SEEN 4             /* neth_rl_state's blocks in view (lane/seen): iron ore, diamond ore, nether brick (in the
                                 * Nether; gravel elsewhere, lane/flint), log */
struct neth_rl_state {
    struct neth_rl_stack inv[36];  /* the server player's main inventory (0..8 the hotbar) */
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
    struct { int32_t x, y, z, opened, items0, items, dim, pad; } chest[NETH_RL_CHESTS];
    /* the look with liquids (lane/liquidlook): the empty bucket's 5-block ray
     * from the client player's eye (itemuse.h itemuse_look_liquid: a still or
     * flowing liquid with metadata 0, a source, stops it; other liquid cells
     * do not), what a player sees in front of them: 1 on a block, its cell,
     * the face hit, its id and metadata; and stat.mineBlock of obsidian */
    int32_t lq_hit, lq_x, lq_y, lq_z, lq_side, lq_block, lq_meta, mined_obsidian;
    /* lane/fight: the living the client's entity ray puts under the
     * crosshair (its client entity id, 0 none) and its kind (living.h, -1
     * unknown); stat.mobKills; and the hostile livings in view, nearest
     * first, then the food animals (lane/food: pig, cow, mooshroom,
     * chicken, within 48 blocks): within 16 blocks of the eye, within 75 degrees of the look, the
     * line from the eye to the middle of the box clear of blocks in the
     * client world (what a player sees on the screen, never through a wall) */
    int32_t mo_entity, mo_ekind, mob_kills, nvis;
    struct { int32_t id, kind; float x, y, z, h; } vis[NETH_RL_VIS];
    /* lane/seen: what the observation's ray cast drew at the step's end
     * (raycast.h raycast_seen; zero without the cast): for each NETH_RL_SEEN
     * block, the pixels showing its face and the nearest such cell with its
     * distance from the eye (d -1: not in view) */
    int32_t seen_n[NETH_RL_SEEN];
    struct { int32_t x, y, z; float d; } seen[NETH_RL_SEEN];
    /* lane/rods2 (books only): the server player's armour slots (surv inv 36..39: boots, leggings, chestplate,
     * helmet), each with its damage; no macro reads them */
    struct neth_rl_stack armor[4];
};

struct neth_rl_result {
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
    struct neth_rl_stack hotbar[9];
    struct neth_rl_stack slots[90];
    struct neth_rl_stack cursor;
    int32_t nevents, pad1;
    struct { int32_t stat, before, after; } events[64];
    struct neth_rl_state st;
    char err[256];
};

/* lane/pearl: what the player sees past neth_rl_state's 16 blocks, kept out of neth_rl_state so its layout (mcsr nwrl.py's
 * ctypes) holds: the endermen in view out to NETH_RL_FAR_R blocks (vis's rule: within 75 degrees of the look, the line
 * from the eye to the box middle clear of blocks), nearest first, each with the scream the screen shows (its open
 * jaw: DataWatcher 18); and the death screen's cause, stat.entityKilledBy.<name> by EntityList's egg order
 * (survival.h EGG_*). Env id's, written by its step's worker like its neth_rl_state */
#define NETH_RL_FAR 4
#define NETH_RL_FAR_R 64.0
#define NETH_RL_EGGS 24
/* lane/pearl2: a census for measurement only (neth_rl_far_census on; pearl_check --census), privileged: what no screen
 * shows, never read by a macro. Every enderman of the player's server world, the nearest NETH_RL_CENSUS within
 * NETH_RL_CENSUS_R blocks (horizontally) listed: its offset from the player's eye to its box middle, under the sky (its
 * feet at or over its column's height map), tracked (on the client), in the 75-degree cone, the line from the eye
 * clear of blocks (the client world's; 0 when not tracked), screaming; and around the player within 16 blocks the
 * skeletons, zombies, spiders and creepers, the skeletons with a clear line to the eye */
#define NETH_RL_CENSUS 16
#define NETH_RL_CENSUS_R 128.0
#define NETH_RL_CENSUS_HI 48       /* lane/pearlsee: the high ground's search radius */
struct neth_rl_census {
    int32_t on, alive, n;
    struct { float dx, dy, dz; int8_t sky, tracked, cone, clear, angry, clear2, clear4, clear8, clear12, clear16, hi,
             hi10; } e[NETH_RL_CENSUS];
                                /* clear2 .. clear16: the line clear from 2, 4, 8, 12 and 16 blocks over the eye (within
                                 * 64); hi, hi10: from the eye of a player standing on the high ground (hi_*), and 10
                                 * blocks over it (lane/pearlsee) */
    int32_t hi_dy, hi_d;        /* lane/pearlsee: the highest dry, treeless column within NETH_RL_CENSUS_HI blocks
                                 * (horizontally; its top block not leaves, log or a liquid, sampled every 2 blocks): its
                                 * standing height over the player's feet (floor), its distance (rounded) */
    float hi_x, hi_y, hi_z;     /* its standing place (the top block's middle, the feet's height) */
    int32_t pl_sky;             /* the player's feet at or over its column's height map */
    int32_t hostile[4];         /* skeleton, zombie, spider, creeper within 16 blocks */
    int32_t skel_los;
    int32_t biome;              /* the player's column's biome */
    int32_t food_n;             /* lane/chain (food_oracle, PRIVILEGED): pigs, cows, mooshrooms and chickens within */
    float food_x, food_y, food_z; /* NETH_RL_CENSUS_R; the nearest's feet (absolute; under the sky first, then any) */
    int32_t food_sky;           /* ... and its feet at or over its column's height map */
    int32_t pearl_n;            /* lane/pearlsee (3rd pass, PRIVILEGED): ender pearl items within NETH_RL_CENSUS_PEARL_R */
    float pearl_x, pearl_y, pearl_z;    /* the nearest one's position (the server's item entity) */
    /* lane/proom (PRIVILEGED, a trace's only; no macro reads them): the nearest skeleton, zombie, spider, creeper and
     * enderman within 16 blocks of the eye (its middle; -1 none), and the feet's height over the first block under
     * them that is not air, tall grass, a flower or a snow layer (-1 none within 64) */
    float hnear[5];
    float ground_dy;
};
#define NETH_RL_CENSUS_PEARL_R 32.0
/* lane/pearl2: the frame's depth (neth_rl_far_depth on), what the camera shows: rays from the eye at the look's yaw
 * -30 to 30 degrees by 10 (inside the 64x64 frame's 70-degree view), level (row 0) and 8 degrees down (row 1: flat
 * ground 11.5 blocks out), each to the first block or liquid within NETH_RL_DEPTH_R blocks: its distance (NETH_RL_DEPTH_R
 * when none) and the block met (0 none) */
#define NETH_RL_DEPTH 7
#define NETH_RL_DEPTH_R 64.0
struct neth_rl_far {
    int32_t n;
    struct { int32_t id, scream; float x, y, z, h; } e[NETH_RL_FAR];
    float d2[NETH_RL_FAR];
    int32_t killed_by[NETH_RL_EGGS];
    struct neth_rl_census census;
    int32_t ender_kills;        /* lane/pearl2: the stats screen's endermen killed (stat.killEntity.Enderman) */
    int32_t depth_on;
    float depth[2][NETH_RL_DEPTH];
    int16_t depth_block[2][NETH_RL_DEPTH];
    float look_depth;          /* lane/pearlsee: the frame's centre ray (along the look, the crosshair's) to the first
                                 * block or liquid within NETH_RL_DEPTH_R (NETH_RL_DEPTH_R none), and the block met (0 none) */
    int16_t look_block;
    float hill_y[NETH_RL_DEPTH];   /* lane/pearlsee: the depth row's yaws at NETH_RL_HILL_PITCH up: the height (y) of the
                                 * block face each met within NETH_RL_DEPTH_R, -1 none (the sky): ground over the eye */
    int32_t blaze_kills;        /* lane/rod: the stats screen's blazes killed (stat.killEntity.Blaze: the player's) */
    int32_t armor_worn;         /* lane/chain2, the books': the armor points worn (the inventory screen's bar) */
    int32_t armor_item[4];      /* lane/chain2: the client's armor slots' items (0 boots .. 3 helmet; 0 empty), what the
                                 * inventory screen shows: the armor kit's worn pieces before it has read the screen */
    struct walkto_ask wask;     /* lane/pearlsee (3rd pass): PRIVILEGED, a path ask (neth_rl_walkto_ask) and its answer, */
    struct walkto_path wpath;   /* searched at the step's end (walkto.h) */
    /* lane/pground (threat_ender reach): the nearest enderman in the client's entity list within NETH_RL_NEAR_R of the
     * eye (to the box middle), whatever the look and the blocks (id 0 none): its client flag screaming, place (the box's
     * foot), height and distance */
    struct { int32_t id, scream; float x, y, z, h, d; } near;
};
#define NETH_RL_NEAR_R 4.0
/* lane/pearlsee (3rd pass): PRIVILEGED, a path for env id's player to cell (x, y, z) within range (walkto.h): answered
 * in neth_rl_far's wpath at the end of the env's next step */
void neth_rl_walkto_ask(struct nwrl *h, int id, int x, int y, int z, float range);
/* lane/divpearl: the same ask with the cell an offset from the feet's cell when the search runs (vec_bench --walk-ask:
 * a privileged search must leave the trajectory unchanged, gates.mk's digest-128x2-walk) */
void neth_rl_walkto_ask_rel(struct nwrl *h, int id, int dx, int dy, int dz, float range);
#define NETH_RL_HILL_PITCH 12.0
const struct neth_rl_far *neth_rl_far(const struct nwrl *h, int id);
/* lane/pnight (PRIVILEGED, a trace's only; never a macro's input): env id's server player's combat tracker as the death
 * message reads it (survival.h surv_combat, chatmsg.c chatmsg_death), read between steps: its entries (n, 0 after a
 * quiet spell), the last entry's damage type (engine/chatmsg.h DT_*, -1 none), its source's name (-1 none) and
 * damage; the longest fall's entry type (-1 none) and length. With the player dead (the death's tick reset n to 0)
 * the last entry and the fall's as the reset kept them: the killing hit's */
struct neth_rl_combat_now {
    int32_t n, dt, name, fall_dt;
    float damage, fall_len;
    int32_t age;                /* ticks since the last entry landed (0: this tick's) */
};
int neth_rl_combat(const struct nwrl *h, int id, struct neth_rl_combat_now *out);
/* lane/rods2 (a trace's only; never a macro's input): env id's server player's statistics stat.damageDealt and
 * stat.damageTaken (tenths of a health point, as the stats screen counts them), read between steps; -1 none */
int neth_rl_damage_stats(const struct nwrl *h, int id, int32_t *dealt, int32_t *taken);
/* lane/pnight (PRIVILEGED, a trace's only; never a macro's input): the watchtower's ceiling, read between steps: env id's
 * living endermen in the overworld with the feet under the sky (the census's census_sky), within 64 (band 0) and 128
 * (band 1) blocks horizontally: their count, and for the eye raised NETH_RL_TOWER_H[j] blocks at the player's own place
 * the line to the box middle clear (the census's ray, raytrace_blocks on the client world), and clear with the middle
 * within NETH_RL_TOWER_R of that eye (the pixel test's reach: an enderman 2 rows of a 64x64 frame). Only the loaded
 * chunks are read */
#define NETH_RL_TOWER_NH 4
#define NETH_RL_TOWER_R 66.0
struct neth_rl_tower { int32_t n[2], clear[2][NETH_RL_TOWER_NH], reach[2][NETH_RL_TOWER_NH]; };
int neth_rl_tower(struct nwrl *h, int id, struct neth_rl_tower *out);
/* lane/pground (PRIVILEGED, a trace's only; never a macro's input): the ground the player stands on and the endermen
 * near it, read between steps. The feet's column's biome (the chunk's biome array, which the client's F3 shows), the
 * feet's y, under the sky (census_sky); with cells 1, the columns within NETH_RL_GROUND_R blocks horizontally (every 2
 * blocks, a disc) by their top block (the height map's, the server world's, only loaded chunks): open ground under
 * the sky (any other top block), water (water or ice), canopy (leaves or a log), none (no loaded column); and every
 * enderman of the player's overworld (the server's entity list, the dying ones too: dead 1), the nearest NETH_RL_GROUND_E
 * by the distance of its feet from the player's (three dimensions, as neth_rl_dig's; all counts them): the entity id, the
 * distance, its ticks existed, the feet under the sky, and within NETH_RL_GROUND_ER the line from the player's eye to the
 * box middle clear (the census's ray: raytrace_blocks on the client world) and clear with the middle within
 * NETH_RL_TOWER_R of the eye (reach: the 64x64 frame's). With cells & 2 (orders 9:29 PM, the caves' openness): air8, the
 * cells within NETH_RL_GROUND_AR of the eye's cell (a ball) that the client world holds as air (a loaded chunk's, block 0).
 * With cells & 4 (orders 10:01 PM, item A: what the render distance leaves the bot): view, the server's view distance
 * (the player manager's: watched chunks within view of the feet's chunk in x and z); the server world's loaded chunks
 * within NETH_RL_GROUND_LR chunks of the feet's chunk (nload), those inside the watched square (nload_w) and the farthest
 * loaded one's chunk distance (lmax, Chebyshev); the spawner's count of monsters as countEntities counts them (capn: its
 * records alive, not persistent, monster type) against its cap (cap: CAP_MONSTER x eligible chunks / 256); and the
 * server's monster livings (zombie, skeleton, creeper, spider, cave spider, enderman, witch, slime, silverfish) by
 * ticking (World.updateEntityWithOptionalForce's test: every chunk within 32 blocks of its feet loaded) and inside the
 * watched square (mon[2][2]: [ticking][watched]), the endermen the same (end[2][2]), and per listed enderman e[k]
 * tick and watch */
#define NETH_RL_GROUND_AR 8
#define NETH_RL_GROUND_LR 24
#define NETH_RL_GROUND_R 32
#define NETH_RL_GROUND_ER 128.0
#define NETH_RL_GROUND_E 64
enum { NETH_RL_GR_OPEN, NETH_RL_GR_WATER, NETH_RL_GR_CANOPY, NETH_RL_GR_NONE, NETH_RL_GR_K };
struct neth_rl_ground {
    int32_t biome, pl_sky, cells[NETH_RL_GR_K], n, all, air8;
    int32_t view, nload, nload_w, lmax, capn, cap, mon[2][2], end[2][2];
    float y;
    struct { int32_t id, age; float d; int8_t sky, clear, reach, dead, tick, watch, pad[2]; } e[NETH_RL_GROUND_E];
};
int neth_rl_ground(struct nwrl *h, int id, int cells, struct neth_rl_ground *out);
/* lane/pground (orders 1:49 PM, the prebuilt floor screen; PRIVILEGED, a trace's only; never a macro's input): read
 * between steps, env id's server overworld: the monster livings (neth_rl_ground's kinds, the dying ones too) whose feet's
 * cell is inside the box b (x0 y0 z0 x1 y1 z1, inclusive cells), the first NETH_RL_FLOOR by the list's order (n; all counts
 * them): entity id, kind (living.h's), ticks existed, dead; with light & 1, over the box's cells that are air on the
 * server world: the most sky light and block light saved (World.getSavedLightValue), the cells read, and the cells over
 * a block with a solid top (spawn's floor test: an opaque full cube) under two air cells (spawnable floor, light aside);
 * the spawner's countEntities of monsters and its cap (neth_rl_ground's capn, cap) */
/* With light & 2 (orders 2:28 PM, what holds the cap): every monster living of the overworld (the dying too) by kind
 * (NETH_RL_FLOOR_K: zombie, skeleton, creeper, spider, cave spider, enderman, witch, slime, silverfish) and ticking
 * (neth_rl_ground's test: every chunk within 32 blocks of its feet loaded; 0 frozen), by the Chebyshev chunk distance of
 * its chunk from the player's (bands 0-4, 5-8, 9-16, 17+) and ticking, and their ids and ticking flags (the first
 * NETH_RL_FLOOR_M, mn; mall counts them) */
#define NETH_RL_FLOOR 64
#define NETH_RL_FLOOR_K 9
#define NETH_RL_FLOOR_M 512
struct neth_rl_floor {
    int32_t n, all, sky_max, blk_max, cells, spawnable, capn, cap;
    struct { int32_t id, kind, age, dead, ender; } e[NETH_RL_FLOOR];
    int32_t kind[NETH_RL_FLOOR_K][2], band[4][2], mn, mall;
    int32_t mid[NETH_RL_FLOOR_M];
    uint8_t mtick[NETH_RL_FLOOR_M];
    int64_t gate[4];     /* the spawner's monster cap test, cumulative (spawning.h mon_*): tests, open, walks, made */
};
int neth_rl_floor(struct nwrl *h, int id, const int32_t b[6], int light, struct neth_rl_floor *out);
/* lane/pdig (PRIVILEGED, a trace's only; never a macro's input): the digging ceiling, read between steps: env id's
 * living endermen in the overworld within NETH_RL_DIG_R blocks of the feet in three dimensions (feet to feet), the
 * nearest NETH_RL_DIG: the entity id, the offset of its feet from the player's feet, the distance, its feet under the sky,
 * and the cells on the straight line from the player's eye to its box middle (the end cells left out; the server
 * world's blocks, only loaded chunks) by kind: air (or a block a player walks through: plants, torches, snow layers),
 * stone (any other block a pickaxe or a hand takes), loose (sand, gravel), water, lava, hard (bedrock, obsidian).
 * NETH_RL_DIG_R is 128 since the orders of 7:06 AM (the patrol's supply: within 64 and 128 under the sky); all: every
 * living enderman within it (n caps at NETH_RL_DIG); pl_sky: the player's feet under the open sky */
#define NETH_RL_DIG 16
#define NETH_RL_DIG_R 128.0
enum { NETH_RL_DIG_AIR, NETH_RL_DIG_STONE, NETH_RL_DIG_LOOSE, NETH_RL_DIG_WATER, NETH_RL_DIG_LAVA, NETH_RL_DIG_HARD, NETH_RL_DIG_K };
struct neth_rl_dig {
    int32_t n, all, pl_sky;
    struct { int32_t id; float dx, dy, dz, d; int8_t sky; int16_t cells[NETH_RL_DIG_K]; } e[NETH_RL_DIG];
};
int neth_rl_dig(struct nwrl *h, int id, struct neth_rl_dig *out);
void neth_rl_far_census(struct nwrl *h, int on);
/* on 1: every env's depth rows at each step's end; 2 (lane/macroperf): only when neth_rl_far_depth_now asks (a step hook) */
void neth_rl_far_depth(struct nwrl *h, int on);
void neth_rl_far_depth_now(struct nwrl *h, int id);

int neth_rl_version(void);
struct nwrl *neth_rl_make(const struct neth_rl_opts *o, char *err, int en);
void neth_rl_free(struct nwrl *h);
int neth_rl_size(const struct nwrl *h);
/* the observation's size and the config's ticks a step */
void neth_rl_obs_spec(const struct nwrl *h, int *w, int *hh, int *ticks, int *device);
/* the whole observation buffer (device address) and its shape */
void *neth_rl_obs(struct nwrl *h, int64_t shape[5]);
/* a start: its index (0 is the one neth_rl_make loaded), -1 with err */
int neth_rl_start_load(struct nwrl *h, const char *dir, char *err, int en);
/* lane/seedwin: the same, loaded in the background on the workers beside the steps in flight: its index at once
 * (-1 with err: every index taken), the start being made (neth_rl_start_ready, neth_rl_start_wait; neth_rl_reset to it
 * waits) */
int neth_rl_start_load_bg(struct nwrl *h, const char *dir, char *err, int en);
int neth_rl_reset(struct nwrl *h, const int *ids, int k, int start, char *err, int en);
/* A start saved from env id as it is now (lane/savestart; the library made
 * by neth_rl_make_ex with NETH_RL_SAVE, rlgui.h): its index, or -1 with err (the
 * env in flight or errored since its reset, every save slot busy: try
 * again later). Env id must be idle (its last result polled) and is not
 * touched. The start is made in the background (pipeline.h
 * pipe_start_from_env: two replays of the env's acts since its reset);
 * neth_rl_start_ready says when, neth_rl_start_wait waits, neth_rl_reset to it
 * waits. An env reset to it continues as env id would have: the same rows
 * and frames for the same acts, and env id's binding state then (the
 * chests found and opened, the scan's clock); the action compilers start
 * over (rlact.h, moveact.h: no queued macro). */
int neth_rl_start_save(struct nwrl *h, int id, char *err, int en);
/* 1 made, 0 being made, -1 failed or no such start */
int neth_rl_start_ready(struct nwrl *h, int start);
int neth_rl_start_wait(struct nwrl *h, int start, char *err, int en);
/* the start let go (its memory goes once no env holds it); its index is
 * free for the next start */
void neth_rl_start_free(struct nwrl *h, int start);
/* the start's tick (-1: no such start) */
int64_t neth_rl_start_tick(struct nwrl *h, int start);
/* lane/proom: a library start's stage by name (train/starts.c: LIB/ID/stage, else LIB/index.tsv's row of ID, read when
 * dir LIB/ID/start was loaded); NULL for a start outside a library, a saved one or no such start */
const char *neth_rl_start_stage(struct nwrl *h, int start);
/* its memory and making: out[0] the image's bytes a reset copies, [1] the
 * view's data, [2] its region spill file's records, [3] the act log
 * replayed, [4] the ticks replayed, [5] the make's ms (a load's for a
 * loaded start), [6] the image's pointers, [7] its tick. 0, or -1 for no
 * such start. */
int neth_rl_start_info(struct nwrl *h, int start, double out[8]);
/* one step for ids[0..k) with acts[0..k); 0 on success */
int neth_rl_step(struct nwrl *h, const int *ids, int k, const struct neth_rl_act *acts);
/* results of up to b envs (at least one while any is in flight); how many */
int neth_rl_poll(struct nwrl *h, int b, struct neth_rl_result *out);
/* the same, returning once min of them are done: every ready result up to b
 * (lane/rlasync: neth_rl_poll waits for every env in flight when fewer than b
 * are, which ties a card's groups together) */
int neth_rl_poll_some(struct nwrl *h, int min, int b, struct neth_rl_result *out);
/* The semantic camera (csrc/runtime/semcam.h; the config's client.camera
 * semantic, which runs no renderer and has no frames, or both): host memory
 * [n][h][w] of 8-byte cells, four uint16 words each (block id | meta << 12,
 * the distance in 1/256 blocks, 0xffff a miss; the face, 0..5 or 6 a miss, 7
 * the void, with the flags << 8; 0), env id's written by its step's worker
 * before its result is polled and valid until its next step; shape n, h, w,
 * 4. NULL with h and w 0 when the camera is off. */
void *neth_rl_semantic(struct nwrl *h, int64_t shape[4]);
/* The textured ray cast (csrc/runtime/raycast.h; the config's client.camera
 * raycast, which runs no renderer and has no frames): host memory [n][h][w][3]
 * of RGB bytes at client.observation's size, row 0 at the top, env id's
 * written by its step's worker before its result is polled and valid until
 * its next step; shape n, h, w, 3. NULL with h and w 0 when the camera is
 * not raycast. */
void *neth_rl_raycast(struct nwrl *h, int64_t shape[4]);
/* engine.raycast device (lane/rcdev, cuda/raycast): the frames [n][h][w][3] uint8 on the device (NULL: the host's
 * cast); a launch draws envs ids[k]'s last steps on stream (a cudaStream_t; NULL the legacy default) and gives a
 * ticket; after neth_rl_raycast_wait(ticket), neth_rl_raycast_fold puts env id's seen record into its state (the macros'
 * and the results') and its counts in n. A worker's next step of an env must start after its launch is done. */
void *neth_rl_raycast_device(struct nwrl *h, int64_t shape[4]);
int neth_rl_raycast_launch(struct nwrl *h, const int *ids, int k, void *stream);
int neth_rl_raycast_wait(struct nwrl *h, int ticket);
void neth_rl_raycast_fold(struct nwrl *h, int id, int32_t n[NETH_RL_SEEN]);
/* the split (engine.raycast_host_groups, lane/rcsplit): how many of a vector env's groups the config casts on their
 * workers; neth_rl_raycast_split(id, 1) has env id cast on its worker (its seen record into its state there, as the
 * host cast's) into page-locked host frames, which neth_rl_raycast_put copies (envs lo..lo + k - 1) into the device's
 * on stream, a ticket for neth_rl_raycast_wait; set before the first step. -1 without engine.raycast device */
int neth_rl_raycast_host_groups(const struct nwrl *h);
int neth_rl_raycast_split(struct nwrl *h, int id, int host);
int neth_rl_raycast_put(struct nwrl *h, int lo, int k, void *stream);
/* envs ids[k]'s last device casts' inputs and outputs to path (rcdev_dump: the kernel bench's input); 0 or -1 */
int neth_rl_raycast_dump(struct nwrl *h, const int *ids, int k, const char *path);
/* the casts the device's copy could not take so far (each a black frame; none in 1.7.10's blocks) */
uint64_t neth_rl_raycast_refused(struct nwrl *h);
/* env id's last frame to the host (w * h * 3 bytes; the gate's cross-check) */
int neth_rl_download(struct nwrl *h, int id, int slot, unsigned char *rgb);
/* The loot chests the seed's structures place in chunks cx0..cx1, cz0..cz1
 * (the engine's structure_walk, vanilla's placement and piece layout, no
 * terrain generated): the village blacksmith's chest (kind 1, a village whose
 * start is valid), the desert temple's four (2) and the jungle temple's two
 * (3). Their y is the piece's before it settles on the ground (exact x and z;
 * y within a few blocks for the village, the temples' chests lie 11 and 3
 * blocks under their floor). Up to max into out; how many there are. */
struct neth_rl_site { int32_t x, y, z, kind; };
int neth_rl_structure_chests(int64_t seed, int cx0, int cz0, int cx1, int cz1, struct neth_rl_site *out, int max);
/* The chests of a seed's world as the server holds it before its first tick
 * (seedworld.h: the spawn point and the 625 chunks around it, generated and
 * populated: village blacksmiths, temples and dungeons there, exact
 * positions, kind 4), and the spawn point into spawn[3] (x, y, z). Up to max
 * into out; how many there are. */
int neth_rl_seed_chests(int64_t seed, int64_t spawn[3], struct neth_rl_site *out, int max);
/* Env id's client world (what its player has seen) in the box x0..x1,
 * y0..y1, z0..z1: block ids, x fastest then z then y, -1 where its chunk is
 * not loaded. The env must be idle; the world is only read (no chunk is made
 * for a missing one). 0, or -1 for a bad id or box. */
int neth_rl_blocks(struct nwrl *h, int id, int x0, int y0, int z0, int x1, int y1, int z1, int16_t *out);
/* The 1.7.10 recipes (engine/recipes.h, generated from the oracle's
 * CraftingManager and FurnaceRecipes): kind 0 shaped, 1 shapeless, 2 the
 * furnace; the output stack and up to 9 ingredient stacks (damage 32767:
 * any). The special recipes (dyed armour, book, map and firework copies) are
 * left out. Up to max into out; how many there are. */
struct neth_rl_recipe {
    int16_t kind, out_item, out_count, out_damage, n, pad;
    int16_t in_item[9], in_damage[9];
};
int neth_rl_recipes(struct neth_rl_recipe *out, int max);
/* FNV-1a over n bytes from h (pipe_bench's digests) */
uint64_t neth_rl_fnv(uint64_t h, const void *p, int64_t n);
/* the pipeline's stats since the last call: env-steps, frames, the time the
 * trainer waited in poll (ns), the workers' step time (ns) */
void neth_rl_stats(struct nwrl *h, uint64_t out[8]);
/* neth_rl_stats' eight, then an env-step's wall time summed over the env-steps
 * polled (pipeline.h struct pipe_stats lat_*: its step in the pool, waiting
 * for its stream, the stream's work, ready until polled, polled until stepped
 * again), the renders' device and upload time (ns) and the bytes uploaded */
void neth_rl_stats_ext(struct nwrl *h, uint64_t out[16]);
/* Env id's memory (envmem.h, as pipe_bench --mem-every reports it): bytes by
 * owner into out[0..8] (server chunks, server worlds, region, client,
 * renderer, entities, ticks, scratch, other), its image heap's bytes used and
 * live blocks (out[9], out[10]) and the owners' total (out[11]); items, when
 * not NULL, gets one "owner\twhat\tbytes" line a part. The env must be idle.
 * 0, or -1 for a bad id. */
int neth_rl_env_mem(struct nwrl *h, int id, double out[12], char *items, int len);

#endif
