/* The whole server, tick after tick, from a tape's snapshot: WorldServer.tick
 * for every world (the overworld, the Nether, the End, in worldServers order)
 * and the per-row values Rows.java's w and d blocks carry for them.
 *
 * The snapshot's worlds.nbt (Snapshot.worldsState) carries each world's own
 * scalars, its initial ambient countdown and difficulty, the view distance, the
 * loaded chunk and pending tick counts and, for the overworld, the
 * World.activeChunkSet in the iteration order the snapshot recorded. The
 * pending block ticks come from ticks.jsonl.gz, one ticks_set per world.
 *
 * Each dimension owns a state (sr_dimstate, in dims): entity pools, pass
 * order, spawner, player manager, region store and pending ticks; a world
 * tick or a player action enters the one it works in first
 * (serverreplay_enter). A snapshot holds overworld chunks only, so the Nether
 * runs servertick_tick_empty until the first portal transfer generates it from
 * the seed; from then on it ticks in full (its spawn lists, the fortress list,
 * its entity pass, PlayerManager.unloadAllChunks while it has no player, and
 * the region store that saves and reloads its chunks with their entities and
 * pending ticks). The End always runs servertick_tick_empty.
 *
 * The player travels through sr_transfer_player, reached from the portal
 * countdown at the head of processPlayer's onUpdateEntity
 * (ServerConfigurationManager.transferPlayerToDimension: the S07, the
 * Teleporter search and makePortal, the player manager swap, the S08). The
 * player is an entry of its world's pass order (pool 3), which the entity
 * digest walks in worldServers order, and its EntityPlayerMP.onUpdate at that
 * place sends the client up to five queued chunks a tick (sr->sent).
 *
 * The active chunk set is rebuilt every tick from the player's position, the
 * way World.setActivePlayerChunksAndCheckLight does: the player's chunk plus
 * the view distance in both directions, inserted x outer then z inner, read
 * back in a JDK 8 HashSet's iteration order (the bucket index
 * (h ^ (h >>> 16)) & (capacity - 1) with the size-implied capacity, insertion
 * order within a bucket). serverreplay_load checks that model against the order
 * the snapshot recorded at tick 0.
 *
 * Rows.blkHash chains across ticks and dimensions: every write the tick makes
 * (Rows.onBlock, dimension id and all) is folded with FNV-1a's prime over
 * dim, x, y, z, id, meta, and w.bc is the count since the last capture.
 */
#ifndef NETHERITE_SERVERREPLAY_H
#define NETHERITE_SERVERREPLAY_H

#include <stdint.h>

#include "chunkload.h"
#include "blockcb.h"
#include "det.h"
#include "fallhang.h"
#include "item_entity.h"
#include "nbtjson.h"
#include "servertick.h"
#include "snapshot.h"
#include "populate.h"
#include "jorder.h"
#include "spawning.h"
#include "living.h"
#include "particles_live.h"
#include "villagers.h"
#include "lightning.h"
#include "seedworld.h"
#include "portal.h"
#include "ticks.h"

struct ticks_backup;

struct server_player;
struct combat_state;

/* One world of the replay. is_overworld is the worldServers[0] the tape's
 * w.wt, w.tt, the player and the pending tick set belong to. */
struct sr_world {
    struct servertick st;
    int dim;
    int is_overworld;
    int chunks;               /* loaded chunks the snapshot recorded */
    int pending;              /* pending block ticks the snapshot recorded */
    int update_entity_tick;   /* WorldServer.updateEntityTick: passes run with no player */
    int *unload, nunload;     /* a non-overworld world's unload queue at the snapshot (cx, cz pairs) */
    int unload_bins;          /* its table length (worlds.nbt's unloadBins), 0 unknown */
    int view;                 /* its player manager's view distance at the snapshot */
    struct jord_window_memo active_memo;   /* the last fill of the set (sr_active_set_memo) */
    int active_cap;           /* World.activeChunkSet's table length: clear() keeps
                               * it, so it is the largest the set has needed (0:
                               * the set has not been filled natively) */
    int *pm_inst, npm_inst;   /* its player instances' marks (cx, cz, ticks since previousWorldTime) */
    int64_t pm_sweep;         /* PlayerManager.previousTotalWorldTime: the processChunk sweep's mark */
    /* the snapshot's worldTeleporter (worlds.nbt tpRand, tpCache): its
     * Random and destinationCoordinateCache in destinationCoordinateKeys
     * order, installed once the teleporter is made */
    int has_tp;
    uint64_t tp_rand;
    struct portal_cache tp_cache;
};

/* One entity of the pass, in Java loadedEntityList's order: the two pools the
 * replay keeps (items and orbs in ie_world, falling blocks and hanging
 * entities in master's fh_world) interleave in the order their constructors
 * ran, exactly as World.updateEntities walks them. */
/* The largest view distance the replay runs (the client's render distance:
 * 16 is the options slider's and F3+F's top; an agent's opts rd past it is
 * refused by session_parse_act and a snapshot's by the load), and the active
 * chunk set's capacity at it (sr_active_set). */
#define SR_MAX_VIEW 16
#define SR_MAX_ACTIVE ((2 * SR_MAX_VIEW + 1) * (2 * SR_MAX_VIEW + 1))
struct sr_ent;
struct serverreplay;
void *sr_ent_p(const struct serverreplay *sr, const struct sr_ent *e);
int32_t sr_ent_id(const struct serverreplay *sr, int pool, const void *p);

/* the most entries one dimension's entity pass order holds */
#define SR_MAX_ORDER (1 << 16)

struct sr_ent {
    int pool;      /* 0 ie_world, 1 fh_world, 2 an_world, 3 the server player */
    int32_t id;    /* the entity's slot in its pool (0-2), the crystal's index, 0 for the
                    * player and the dragon; -1 for none (sr_ent_p, sr_ent_id) */
};

struct sr_saved_entity {
    struct sw_entity ent;
    int loaded;
    int pool;                 /* 2: seed-world creature, 1: saved falling block, 3: any other living kind by NBT,
                               * 4 and 5: items and orbs by NBT, 6: a Nether living kept whole (active),
                               * 7: a hanging entity (painting, item frame) by NBT; the End's path reuses
                               * 7 for crystals and 8 for the dragon (sr_end_save_entities);
                               * 9: an arrow a run's own unload saved, by NBT */
    int kind;                 /* pool 3: the living kind; a checkpoint's record of a class the
                               * living machinery does not cover files kind -1 with cls */
    uint64_t seq;             /* the chunk section list order (entity chunk_stamp): a reload adds a section's entities in it */
    int cx, cz, section;
    struct sr_ent active;    /* the live instance (pool slot, as the pass order names it; id -1
                              * for none), for a second unload/load */
    uint32_t active_gen;     /* pool 2: the list entry slot's generation then (arena.h): a slot
                              * the grave gave back and another entity took is not this one */
    lref living;             /* pool 6: the Nether living kept whole */
    struct dragon_state *dragon_copy;  /* pool 8: the dragon's state as it was saved */
    nbt *tag;                /* the falling block's saved NBT */
    char *cls;               /* pool 3: the region NBT's id string */
    int64_t uuid_msb, uuid_lsb;
    int resaved;
    int no_follow_bonus;      /* resaved without a "Random spawn bonus" (an egg's hatchling, a bred child) */
    float health, absorption, fall_distance;
    int air, on_ground, growing_age, in_love, watcher16, egg_timer;
    int persistence_required, can_pick_up_loot;
    char custom_name[32];
    /* the Leashed flag and the Leash compound as recreateLeash reads it
     * (leash.c: 1 by UUID, 2 by knot tile) */
    int leashed, leash_pending, leash_x, leash_y, leash_z;
    int64_t leash_msb, leash_lsb;
};

/* The state a dimension owns: its entity pools and their pass order, its
 * spawner, its player manager, the chunks it unloaded with their entities and
 * pending ticks (the region store), and its pending tick set. The replay keeps
 * one per dimension (dims, by sr_dim_index) and d names the one of the
 * dimension here; every world tick and every player action enters its own
 * dimension first (serverreplay_enter), so code that reads sr->d reads the
 * world it works in. The pools' self pointers and the entities' back pointers
 * name their own dimension's state; at rest here is the player's dimension. */
#define SR_PARK_MAX 256
/* Rows a parked chunk stays as its bands before the park packs it: most
 * chunks a load takes back from the store come back within a row of their
 * unload (a block read at the view's edge reloads it), and a chunk still
 * unpacked costs its load nothing (lane/spillfast). */
#define SR_PARK_WAIT 1
struct sr_dimstate {
    struct serverreplay *sr;  /* the replay it belongs to */
    int dim;
    int live;                 /* made: the overworld at the load, the Nether and the End with their states */
    ie_world iew;             /* the items and orbs */
    fh_world fhw;             /* the falling blocks and hangings */
    struct spawner spawner;   /* natural spawning in WorldServer.tick */
    struct an_world anw;      /* the living entities */
    struct sr_ent *ents;      /* the entity pass's order over the pools */
    int nents, capents;
    int initial_ent_count;    /* the snapshot's own entries ahead of the id-ordered new ones */
    struct lightning_bolt *bolts; /* World.weatherEffects, in insertion order */
    int nbolts, capbolts;
    struct cl *cl;            /* the provider and player manager, for the unloads */
    struct st_write *extra;   /* the entity pass's own writes, this tick */
    int nextra, capextra;
    int hiextra;              /* the most extra held since serverreplay_trim */
    struct st_write *late;    /* last tick's entity-pass and network writes: the
                               * player manager flushes them with the next world tick */
    int nlate, caplate;
    /* the Nether's and the End's VillageCollection (the overworld's is
     * serverreplay.vc) */
    struct village_collection vc;
    struct world saved;       /* the region store */
    int saved_ready;          /* the seed world or checkpoint save is in saved */
    int saved_init;           /* saved exists (world_init ran): an unload before
                               * the first load stores its chunk without the build */
    int saved_ctx;            /* the globals the build leaves set are set
                               * (sr_saved_context) */
    struct sr_saved_entity *saved_ents;
    int nsaved_ents;
    int *lazy;                /* the checkpoint's region chunks not read yet:
                               * cx, cz, skip-ticks triplets (sr_saved_fetch) */
    int nlazy, caplazy;
    struct sr_chunk_ticks *chunk_ticks;
    int nchunk_ticks, capchunk_ticks;
    /* the chunks the unloads parked in saved as their bands: packed
     * between rows (serverreplay_park), not in S3, once SR_PARK_WAIT rows
     * have passed since their unload (park_row: the park's count then);
     * past SR_PARK_MAX the park walks the whole store */
    int32_t park[SR_PARK_MAX][2];
    uint32_t park_row[SR_PARK_MAX];
    int npark;
    int nwait;                /* park[0..nwait): from earlier rows, waiting */
    struct ticks_set ticks;   /* WorldServer's pending tick set */
    int ticks_ready;          /* entered once: its set holds its own world values */
};

/* The index of dimension dim in dims: 0 the overworld, 1 the Nether, 2 the End. */
static inline int sr_dim_index(int dim)
{
    return dim == -1 ? 1 : dim == 1 ? 2 : 0;
}

/* One chunk the player's EntityPlayerMP.onUpdate sent (S26PacketMapChunkBulk):
 * a copy of the server chunk as it stood then, for the client to insert. */
struct sr_sent_chunk {
    int dim;
    struct chunk *c;
};

/* The replay's Det streams and shuffle Random, in the environment's
 * random-stream table (the file that uses them includes env.h). One
 * replay runs per environment. */
#define SR_DET(sr) (nw_env->rng.det)
#define SR_SHUF(sr) (nw_env->rng.shuffle)

enum { SR_FLUSH_MAX = 1024 };   /* a flush's chunks */

struct serverreplay {
    /* the Det streams are the environment's (env.h struct rng_table): SR_DET */
    struct sr_world w[3];
    int nworlds;
    /* worlds.nbt's teOrder per worlds index (x,y,z triples): the
     * loadedTileEntityList order the load puts the tile entities in */
    int *te_order[3], te_order_n[3];
    int view;                 /* ServerConfigurationManager.viewDistance */
    uint32_t park_rows;       /* serverreplay_park's calls (the park's clock) */
    int tracker_view;         /* the view distance the EntityTrackers took at the worlds' creation (10) */
    uint64_t blk_hash;        /* Rows.blkHash, the chain across ticks */
    int64_t blk_count;        /* w.bc, reset each server tick */
    int native_entities;      /* the entities the native lists hold */
    int current_tick;
    int in_spawner_tick;
    int in_entity_tick;       /* inside an_tick_one of the entity pass */
    int end_blast_igniter;    /* the End blast running is a TNT the player lit (the attacker) */
    int end_blast_tail;       /* inside a TNT's or a bed's End blast: the caller orders its drops */
    int order_new_by_id;
    int pop_depth, pop_id;    /* the populate call a provider load happens inside */
    int quiet_join;           /* the tape's start.quietJoin pin: no entity ticked in the join ticks */
    int action_an_n;          /* living-world list size before client packets */
    struct village_collection vc;  /* the overworld's villages, WorldServer.tick's tail */
    struct village_siege vs;
    /* Collections.shuffle's shared Random, det.nbt's "shuf", is the
     * environment's (env.h struct rng_table): SR_SHUF */
    struct server_player *player;
    struct world *action_world;
    struct combat_state *combat;
    struct pickobj_state *pickobj; /* the pick's non-living entities (pickobj.c) */
    struct client_player *combat_cp; /* the client combat_bind saw, for a late bind */
    lref player_livh;                /* the player twin (lv_get), 0 none */
    /* the snapshot runtime's references to the player (snap_runtime.c),
     * set once serverreplay_bind_player makes its living twin */
    lref **rt_player_refs;            /* living references to the player twin, filled at the bind (a slab table) */
    int nrt_player_refs, cap_rt_player_refs;
    /* the snapshot's ghosts (ghosts.jsonl): livings no world lists that a
     * listed one still references, rebuilt outside every list as the
     * native run keeps an unloaded chunk's living (sr_cl_event) */
    lref ghosts[64];
    int nghosts;
    /* the player's own ghosts: ServerConfigurationManager.transferEntityToWorld
     * runs the old world's updateEntityWithOptionalForce(player, false) at the
     * scaled place, which files the player in that chunk's entity list when
     * the chunk is loaded. When the chunk unloads, World.updateEntities'
     * loadedEntityList.removeAll(unloadedEntityList) takes out any entity
     * equal to it (Entity.equals: the id), the live player too; same: the
     * ghost is still the live player object (a respawn makes a new one) */
    struct sr_pghost { int dim, cx, cz, same; } pghost[8];
    int npghost;
    /* a ghost's unload took the live player out of its world's
     * loadedEntityList: no EntityPlayerMP.onUpdate in the pass, no chunk
     * re-filing, no tracker entry, until a transfer or a respawn lists it */
    int player_unlisted;
    int64_t player_removed_tick;  /* the tick the dead player left the world (-1 none) */
    int pass_player_done;         /* this tick's entity pass reached the player's place */
    /* the snapshot the load read (owned by the caller, alive for the run):
     * combat_bind reads its tracker and client entities */
    const struct snapshot *snap;
    uint64_t rt_stamp_base;           /* snap_runtime.c's slice stamps start here */
    int32_t player_enth;             /* the twin's list entry (an_deref), 0 none */
    int blast_pending, has_s27;
    double s27_x, s27_y, s27_z;
    /* this tick's explosions within 64 blocks of the player, in order: one
     * S27 each (WorldServer.newExplosion), the knockback on the first */
    struct sr_s27 { double x, y, z; float size; int naffected, nxp; struct s27_xp xp[S27_XP_MAX]; } s27s[16];
    int ns27;
    /* WorldServer's queued block events (addBlockEvent), sent by the next
     * func_147488_Z of their world */
    struct sr_bev { struct world *w; int x, y, z, block, event, param; } bevs[32];
    int nbev;
    int mobs_enabled;
    int natural_spawning;
    int player_entity_id;
    int cal_month, cal_day;
    int negative_mob_skipped;
    /* the snapshot's tracker entries (entities.jsonl trk, trkd), by entity
     * id: combat_bind restores them over the entries it rebuilds */
    struct sr_trk { int id, v[9]; double d[6]; } *trk;
    int ntrk;
    struct blockcb_env action_prev_env;
    /* the client's world, and the player manager's last flush for it
     * (WorldServer.tick's updatePlayerInstances, taken as it runs): per
     * chunk the client holds, in the order first flagged, its distinct
     * cells (64 at most) with their block and metadata then, or for 64 an
     * S21 of the flagged sections, held as they were */
    struct world *client;
    struct sr_flush_chunk { int cx, cz, n, secs; uint16_t cells[64]; int16_t ids[64]; uint8_t metas[64];
                            struct chunk_sec *bands[16]; } *flush;
    int nflush, flush_ready, flush_split;   /* flush: SR_FLUSH_MAX chunks, made with the replay */
    struct st_write *dev_writes; /* head-of-tick block writes, delivered next client tick */
    int ndev_writes, dev_block_count;
    int dev_writes_dim;       /* the world they were made in (the player's) */
    int entity_dims[3];       /* the dimension each native entity belongs to */
    int seeded;               /* the snapshot's own entities were adopted */
    char save_dir[1200];      /* checkpoint's save/ dir: load chunks from its
                               * region files instead of rebuilding them */
    char join_save_dir[1200]; /* the snapshot's own save/ (SaveOverlay.java):
                               * the chunks the join ticks saved, read before
                               * the checkpoint's copy; empty when absent */
    /* the region saves of the unloaded chunks (seedticks.jsonl.gz): the
     * entries a chunk re-adds at its next load, verbatim */
    struct snap_seed_tick *seed_ticks;
    int nseed_ticks;
    int has_bigtree;                 /* the snapshot's worldstate carried bigTree
                                      * (each biome's WorldGenBigTree heightLimit) */
    int has_seedticks;               /* the snapshot carried seedticks.jsonl.gz:
                                      * the seed ticks are the region saves,
                                      * not the populate re-derivation */
    /* the unloaded overworld chunks' region Entities (seedents.jsonl.gz,
     * the replay's own copies), sorted by cx, cz; has_seedents: the snapshot
     * carried the file, so they replace the seed world's creatures */
    struct snap_seed_chunk *seed_chunks;
    int nseed_chunks;
    int has_seedents;
    struct populate pop;

    /* The tick each join-time chunk unload save ran at (worlds.nbt's
     * w0.unloadClock, v2 snapshots): cx, cz, tickCounter triplets. The save's
     * clock is tick - 1 (unloadQueuedChunks runs before the increment), so
     * the seed-world save restores each chunk's delays exactly as its unload
     * wrote them. Absent (older snapshots): every clock is 1. */
    int *join_clock;
    int njoin_clock, capjoin_clock;

    /* the dimensions' own states by sr_dim_index, and the one named by here
     * (d = &dims[sr_dim_index(here)]) */
    struct sr_dimstate dims[3];
    struct sr_dimstate *d;
    int here;
    struct populate hell;       /* the Nether's provider, empty at the snapshot */
    int hell_live;              /* the Nether has loaded a chunk */
    int join_dim;               /* the player's dimension at the snapshot */
    double join_x, join_z;      /* its login position: the manifest's, or the player NBT's when away */
    struct teleporter tp_over, tp_hell; /* each WorldServer's worldTeleporter */
    struct populate sky;        /* the End's provider, empty at the snapshot */
    int end_live;               /* the End has loaded a chunk */
    struct teleporter tp_end;
    struct sr_end *end;         /* the End's dragon and crystals (endfight.h) */
    int action_first_item;      /* the action's first new item, after a transfer */
    int action_moved;           /* a transfer re-entered the action mid-way */
    int in_transfer;            /* inside a player transfer's placement: its
                                 * population drops join the order at once */
    int respawn_arriving;       /* a death respawn left another dimension */
    int nresend;                /* this tick's S23s outside the world's writes: */
    /* processPlayerBlockPlacement's two S23s per placement C08 and a
     * finished dig's one: a tick's input list holds at most CLIENT_PKT_CAP
     * packets (survival.h), so twice that is every one a tick can send */
#define SR_RESEND_CAP 512
    int resend[SR_RESEND_CAP][6];   /* x, y, z, dimension, the block id and meta then */

    /* Entity.travelToDimension of a non-player inside its world's entity
     * pass: while the traveller's own update runs on in the destination,
     * that dimension is entered (foreign) and the pass's world and its hooks
     * wait here; xw is the writes other worlds' passes made in a dimension
     * this server tick (the pass's chain carries them, and a client in that
     * dimension sees them). */
    int foreign, foreign_from, foreign_in_tick, foreign_view_n;
    struct ie_ent *foreign_ie;   /* the item-pool traveller, until its post_update */
    /* the living traveller's old instance in the destination chunk list, and
     * the chunk (section clamped) the trip filed it in, until its post_update */
    struct an_ent *foreign_old;
    int foreign_old_cx, foreign_old_cy, foreign_old_cz;
    struct cl *watch_cl;         /* set: the player manager a tracker entry's
                                  * chunk test asks (a transfer's spawn, whose
                                  * player is still in the old world's) */
    int foreign_exit;            /* an End exit traveller reads the overworld until its post_update */
    struct ie_world *foreign_iew;
    struct blockcb_env foreign_env;
    jrand *foreign_pop_rand, *foreign_ticks_rand;
    struct sr_xwrite {
        int dim;
        struct st_write w;
    } *xw;
    int nxw, capxw, xw_first;

    /* EntityPlayerMP.loadedChunks: the chunks the player manager queued for
     * the client, in filterChunkLoadQueue's order, and the ones the
     * player's onUpdate sent this tick (up to 5, S26PacketMapChunkBulk). */
    int *send_queue;            /* cx, cz pairs */
    int nsend, capsend;
    struct sr_sent_chunk sent[5];
    int nsent;
    int send_dim;               /* the dimension send_queue belongs to */
    void *ghw;                  /* the Nether pool's gh_world: its ghasts' view of the player */

    /* The client's chunk set follows the server's packets (a v2 snapshot,
     * whose join the replay reconstructs): the chunk packets this server tick
     * sent, delivered by serverreplay_sync_client_chunks at the next client
     * tick. Off, the client world is the snapshot's whole server world. */
    int stream_chunks;
    struct sr_client_chunk *cchunks;
    int ncchunks, capcchunks;

    /* the tick's scratch and the few objects a phase keeps between calls:
     * the active set (sr_tick_world), the End blast query's walk list,
     * EntityAIMate.spawnBaby's constructor streams (sr_living_constructor),
     * the EntityPlayerMP the End's orbs cached when the player left */
    int active_scratch[SR_MAX_ACTIVE * 2];
    struct eb_entry *end_blast_list;
    det_state ctor_fake;
    struct entity departed;
    char err[256];
};

struct sr_saved_tick {
    int x, y, z;
    int block;
    int delay;
    int priority;
};

/* A chunk packet on its way to the client: 'l' an S26 bulk entry (the
 * client loads the chunk, Chunk.fillChunk with the server's bytes), 'u' an S21
 * empty ground-up chunk (WorldClient.doPreChunk unloads it). */
struct sr_client_chunk {
    char kind;
    int dim;                  /* the world whose player manager sent it */
    int cx, cz;
};

struct sr_chunk_ticks {
    int cx, cz;
    int n;
    struct sr_saved_tick *ticks;
};

/* Load DIR's worlds.nbt and the manifest's server block into sr. 0 on a
 * missing or malformed file, or on a world with loaded chunks or pending ticks
 * this replay does not carry (only the overworld may have them). */
int serverreplay_load(struct serverreplay *sr, struct snapshot *snap, const char *dir);
int sr_load_snapshot_entities(struct serverreplay *sr, const struct snapshot *snap);
/* The living kind of a snapshot's entity class name ("EntityPig"), -1 for
 * none. */
int sr_living_kind(const char *cls);
/* EntityList.createEntityFromNBT for a living kind a chunk reload brings back. */
struct living *sr_living_from_nbt(struct serverreplay *sr, int kind, const nbt *tag);
struct living *sr_living_copy_nbt(struct serverreplay *sr, int kind, const nbt *tag, struct world *w);
int sr_spawn_kind(int kind);
/* EntityThrowable's five kinds by snapshot class or EntityList name (0 for
 * anything else), their construction from NBT with the Entity draws the
 * caller made (id, Random), and getThrower's lookup by name. */
int sr_throwable_kind(const char *cls);
ie_ent *sr_throwable_from_nbt(struct serverreplay *sr, int kind, const nbt *tag, int id, det_rng rnd);
ie_ent *sr_eye_from_nbt(struct serverreplay *sr, const nbt *tag, int id);
ie_ent *sr_fireball_from_nbt(struct serverreplay *sr, int large, const nbt *tag, int id, det_rng rnd);
ie_ent *sr_ender_eye_from_nbt(struct serverreplay *sr, const nbt *tag, int id, det_rng rnd);
void sr_throwable_resolve(struct serverreplay *sr, ie_ent *ie);
int sr_kind_for_class(const char *name);

/* One whole server tick: every world in order. The overworld's active chunk
 * set and light re-check read the player's position as the world tick sees it,
 * i.e. the position before this tick's processPlayer (the caller passes the
 * previous row's). */
void serverreplay_tick(struct serverreplay *sr, int t, double px, double py, double pz);
void serverreplay_save_marks(struct serverreplay *sr, int t);
/* Between rows: deflate the chunks the row's unloads parked in the region
 * stores (chunk_pack), which nothing reads until they load again. */
void serverreplay_park(struct serverreplay *sr);
void serverreplay_lightning_strike(void *ctx, const struct lightning_bolt *bolt);
/* Dev.java's chicken jockey: EntityZombie attempts through the spawner's
 * record path until one's egg path spawns and mounts its chicken; the chicken
 * joins the world first, then the zombie. */
int serverreplay_summon_chicken_jockey(struct serverreplay *sr, double x, double y, double z, int attempts);
int serverreplay_summon_hostile(struct serverreplay *sr, int kind, double x, double y, double z,
                                int child, int size);
int serverreplay_move_mob(struct serverreplay *sr, int id, double x, double y, double z);
void serverreplay_track_entity(struct serverreplay *sr, int pool, void *entity);
/* The End's dragon (crystal -1) or crystal index a snapshot just built enters
 * the End's pass here, with its hooks and the spawner's count. */
void serverreplay_end_listed(struct serverreplay *sr, int crystal);
void sr_order_push(struct serverreplay *sr, int pool, void *p);
void sr_constructor_begin(det_state *fake, const det_state *real, int role);
void sr_constructor_end(det_state *real, det_state *fake, int role);
void serverreplay_bind_player(struct serverreplay *sr, struct server_player *player, int entity_id);
void serverreplay_player_collide(struct serverreplay *sr);
void serverreplay_player_moved(struct serverreplay *sr);

/* The player's living twin (what the mobs read) follows the player again, after
 * a change inside the world tick (wakeAllPlayers moving the player). */
void serverreplay_player_resync(struct serverreplay *sr);
/* EntityPlayerMP.onUpdate and its chunk sends (at the player's place in the
 * pass, or from the pig it rides: riding.c). */
void serverreplay_player_on_update(struct serverreplay *sr, struct world *w);
struct world *serverreplay_player_world(struct serverreplay *sr);
/* the world of dimension dim's rainingStrength (0 for none): the S2B 7 its
 * players' WorldClient last took */
float serverreplay_rain_strength(struct serverreplay *sr, int dim);
struct living *serverreplay_player_living(struct serverreplay *sr);
/* NetHandlerPlayServer.setPlayerLocation. */
void serverreplay_set_player_location(struct server_player *p, double x, double y, double z,
                                     float yaw, float pitch);
void serverreplay_queue_packets(struct serverreplay *sr);
struct cl *serverreplay_player_manager(struct serverreplay *sr);
struct randomtick_env;
/* ServerConfigurationManager.func_152611_a from IntegratedServer.tick's tail
 * at row t: the view distance (the active chunk window) and every world's
 * PlayerManager.func_152622_a, the player's own dropping and adding its
 * rings around the server player's position (px, pz). */
void serverreplay_set_view_distance(struct serverreplay *sr, int t, int view, double px, double pz);
/* MinecraftServer.func_147139_a: every worldServer's difficultySetting and
 * its allowed spawn types (single player, not hardcore: monsters unless
 * PEACEFUL, animals always); the doMobSpawning rule stays. The dev
 * difficulty op and a start's difficulty (session.h start_difficulty). */
void serverreplay_set_difficulty(struct serverreplay *sr, int v);
void serverreplay_dev_block_env(struct serverreplay *sr, jrand *wrand, struct randomtick_env *saved);

/* The dimension states of a zeroed replay (serverreplay_load starts with it):
 * the overworld here, holding the pending set ticks.c has now. */
void serverreplay_dims_init(struct serverreplay *sr);

/* Make dim the dimension here (0 the overworld, -1 the Nether, 1 the End). */
void serverreplay_enter(struct serverreplay *sr, int dim);

/* NetHandlerPlayServer.processClientStatus(PERFORM_RESPAWN) for a player who
 * went through the End's exit portal: respawnPlayer(player, 0, true). */
void serverreplay_respawn_conquered(struct serverreplay *sr);

/* IntegratedServer.tick's pause (a pausing screen, the credits): the first
 * paused tick's saveAllWorlds queues every loaded chunk without a player
 * instance for unloading, in every live world. */
void serverreplay_pause_save(struct serverreplay *sr, int t);

/* The dimension the server player is in. */
int serverreplay_player_dim(const struct serverreplay *sr);
/* WorldServer.addWeatherEffect's S2CPacketSpawnGlobalEntity for a bolt in
 * dimension dim at x, y, z. */
void serverreplay_bolt_packet(struct serverreplay *sr, int dim, double x, double y, double z);

/* dim's WorldServer.rand. */
jrand *serverreplay_world_rand(struct serverreplay *sr, int dim);

/* Publish the player's world as the block-drop environment for a player
 * action outside the world pass (a bucket's liquid replacing a plant drops
 * it through Block.dropBlockAsItem: World.rand, the server's Det, the item
 * sink); *saved takes what was published, serverreplay_drop_env_end puts it
 * back. */
struct randomtick_env;
void serverreplay_drop_env_begin(struct serverreplay *sr, struct randomtick_env *saved);
void serverreplay_drop_env_end(const struct randomtick_env *saved);

/* The End's dragon while it is in the End's lists, else NULL. */
struct dragon_state *serverreplay_dragon(struct serverreplay *sr);
/* EntityHanging.attackEntityFrom from the player or a projectile, inside a
 * phase that orders the item pool's new entries itself. */
void serverreplay_break_hanging(struct serverreplay *sr, fh_ent *en);
/* ItemHangingEntity.onItemUse's server half for the item frame. */
int serverreplay_place_hanging(struct serverreplay *sr, int kind, int x, int y, int z, int dir);
/* EntityEnderCrystal.attackEntityFrom from the player: the crystal's death
 * and its explosion, as a projectile's hit on it runs them. */
struct dragon_crystal_state;
void serverreplay_end_crystal_hit(struct serverreplay *sr, struct dragon_crystal_state *c);

/* EntityTNTPrimed.explode for a TNT of the End's item pool (tnt.c's
 * on_tnt_explode, the End form). */
void serverreplay_end_tnt_explode(struct serverreplay *sr, struct ie_ent *tnt);

/* An explosion in the End over the End's lists, crystals and the dragon's
 * parts included (a TNT's, a bed's); igniter names the player as the
 * source's entity; knock gets the player's knockback entry. */
void serverreplay_end_run_blast(struct serverreplay *sr, jrand *wr, double x, double y, double z, float power,
                                int flaming, int smoking, int igniter, double knock[3]);

/* The player's entity in its world's loadedEntityList moves to the tail, as
 * ServerConfigurationManager.respawnPlayer's spawnEntityInWorld puts the
 * fresh player there. */
void serverreplay_player_to_tail(struct serverreplay *sr);

/* ServerConfigurationManager.respawnPlayer's player manager steps for a death
 * in the overworld: removePlayer at the old player's managed position, then
 * (once the new position is known) the spawn chunk's loadChunk and addPlayer
 * at the new one, with its send queue. */
void serverreplay_respawn_leave(struct serverreplay *sr);
/* respawnPlayer's new EntityPlayerMP: none of the old one's potion effects */
void serverreplay_player_fresh(struct serverreplay *sr);
/* an S23PacketBlockChange of (x, y, z) in the player's world: the client
 * takes the server's block there with the next tick's packets */
void serverreplay_resend_block(struct serverreplay *sr, int x, int y, int z);
/* the fresh player joins the overworld's loadedEntityList at its tail (a
 * player that died elsewhere arrives there) */
void serverreplay_respawn_to_tail(struct serverreplay *sr);
void serverreplay_respawn_load(struct serverreplay *sr, double x, double z);
void serverreplay_respawn_join(struct serverreplay *sr, double x, double z);

/* The entity pass order without the entities a purge killed: the player
 * alone. */
void serverreplay_order_players_only(struct serverreplay *sr);

/* The chunks the last server tick sent the client (sr->sent), handed over:
 * the caller owns the chunk copies afterwards. */
int serverreplay_take_sent(struct serverreplay *sr, struct sr_sent_chunk *out, int max);

/* Negative-check switches, set by the harness before the replay: bit 1 skips
 * every world with no sky (the Nether and the End), bit 2 drops the first block
 * write of every tick from the count and the digest, bit 8 skips one living
 * entity's first eligible tick. Each must make a row diverge, naming it.
 * The switches are nw_env->cfg.sr_negative (env.h). */

/* The tape's server fields, read after a tick. */
int64_t sr_world_time(const struct serverreplay *sr);   /* w.wt */
int64_t sr_total_time(const struct serverreplay *sr);   /* w.tt */
int sr_block_writes(const struct serverreplay *sr);     /* w.bc */
uint64_t sr_block_hash(const struct serverreplay *sr);  /* d.blk */
uint64_t sr_world_rng(const struct serverreplay *sr);   /* d.sw: every world's Random and updateLCG */
int serverreplay_action_begin(struct serverreplay *sr);
void serverreplay_action_end(struct serverreplay *sr, int first_item);
void serverreplay_sync_client(struct serverreplay *sr, struct world *client);
/* NetHandlerPlayClient.handleMapChunkBulk and handleChunkData for the chunk
 * packets the last server tick sent, in send order, before the client tick
 * that processes them: a load copies the server's chunk (without its tile
 * entities) into client, an unload frees the client's copy. on, when set, sees
 * each one after it is applied. */
/* A snapshot whose player is dead and out of the entity list: its entity id
 * (the runtime's), the server player marked dead and removed, the
 * snapshot's player count kept at one. -1 when the snapshot has no runtime. */
int serverreplay_dead_player_id(struct snapshot *snap, struct server_player *sp, int *removed);
/* A client chunk made from a server chunk (a chunk packet's): the client's
 * fresh gap flags and relight cursor for dimension dim. */
void serverreplay_client_chunk_fresh(struct chunk *c, int dim);
/* the client world the flush goes to, after a new one replaces it */
void serverreplay_set_client(struct serverreplay *sr, struct world *client);
int serverreplay_client_prefill(struct serverreplay *sr, const struct snapshot *snap, struct world *client);
void serverreplay_sync_client_chunks(struct serverreplay *sr, struct world *client,
                                     void (*on)(void *ctx, char kind, int cx, int cz), void *ctx);
void sr_det(const struct serverreplay *sr, uint64_t *seeder, uint64_t *math, uint64_t *split); /* d.sseed/smath/sstat */
struct snapshot;
/* d.ents: every world's loadedEntityList (overworld, Nether, End), each
 * entity's id and NBT, the bound player at its own place (sr_digest.c) */
uint64_t sr_entity_digest(const struct snapshot *s, struct serverreplay *sr, const struct server_player *sp);
/* The length of dim's pass list (ents) wherever that dimension's state is,
 * here or away, without entering it; 0 for a dimension the replay lacks. */
int serverreplay_dim_nents(const struct serverreplay *sr, int dim);
/* one entry's id and NBT (the player's from sp) */
nbt *sr_entry_nbt(const struct snapshot *s, const struct server_player *sp, const struct sr_ent *rec, int *id);

/* The overworld's loaded entity count and digest (w.ents, d.ents), computed by
 * the caller: this module owns the world ticks only. */

/* The active chunk set the next tick would walk, for the snaphot-order check
 * and for the row comparison: cx, cz pairs into out (2*SR_MAX_ACTIVE ints).
 * cap, when not NULL, is the world's remembered table length, grown here. */
int sr_active_set(const struct serverreplay *sr, double px, double pz, int *out, int *cap);
/* sr_active_set through a caller's memo (jorder.h): the per-tick fill */
int sr_active_set_memo(const struct serverreplay *sr, double px, double pz, int *out, int *cap,
                       struct jord_window_memo *memo);

int sr_top_solid_or_liquid(struct world *w, int x, int z);

/* Village.villageAgressors from a snapshot: entity ids to livings. */
void serverreplay_resolve_village_agressors(struct serverreplay *sr);

#endif
