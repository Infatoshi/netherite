/* The provider and the player manager: which chunk the 1.7.10 server loads,
 * populates or unloads next. The port of ChunkProviderServer (loadChunk, the
 * populate four-neighbor rule, unloadChunksIfNotNearSpawn,
 * unloadQueuedChunks), Chunk.populateChunk and PlayerManager (addPlayer,
 * updateMountedMovingPlayer, func_152622_a), checked against
 * netherite.oracle.ChunkLoadProbe's recordings in out/java/chunkload/.
 *
 * The engine produces the sites whose behavior is a rule (spawn, playermgr,
 * login); the sites it cannot produce itself (population features writing
 * into an unloaded chunk, tick blocks reading off the loaded edge) come back
 * in as recorded loads at their recorded tick, through cl_replay_load (a
 * top-level loadChunk) or cl_pop_load (a loadChunk inside a populate call).
 * When the populate lane lands, its features will call the same two on their
 * own, and the replay goes away. */
#ifndef NETHERITE_CHUNKLOAD_H
#define NETHERITE_CHUNKLOAD_H

#include <stdint.h>

#include "world.h"

struct cl_event
{
    int t;
    char kind;          /* 'l' load, 'p' pop, 'm' mark, 'n' unmark, 'u' unload */
    int cx, cz, pop, id;
    const char *site;   /* load events only */
};

/* The loads recorded for one populate call, by the call's index: the n-th
 * populate the engine enters is the recording's pop id n. The engine pulls
 * these on populate entry and loads each one as if the feature had written
 * there; until the populate lane lands, the test feeds the recording back. */
struct cl_feed
{
    const struct cl_load *(*pop_loads)(void *ctx, int pop_index, int *n);
    void *ctx;
};

struct cl_load
{
    int cx, cz;
    const char *site;
};

struct cl;

struct cl *cl_init(struct world *w, int spawn_x, int spawn_z);
void cl_free(struct cl *c);
/* the bytes the player manager and provider hold now (envmem.h) */
size_t cl_bytes(const struct cl *c);

/* Every event, in call order. */
void cl_on_event(struct cl *c, void (*emit)(void *ctx, const struct cl_event *e), void *ctx);
void cl_set_feed(struct cl *c, const struct cl_feed *feed);

typedef struct chunk *(*cl_load_fn)(void *ctx, int cx, int cz);
typedef int (*cl_unload_fn)(void *ctx, int cx, int cz);
void cl_set_chunk_io(struct cl *c, cl_load_fn load_cb, cl_unload_fn unload_cb, void *ctx);

/* ChunkProviderServer.loadChunkOnProvideRequest off: loadChunk becomes the
 * EmptyChunk read, so the engine's own loads (the player manager's) never
 * touch the world. The tape replay sets it; the chunkload gate leaves it off. */
void cl_set_no_load(struct cl *c, int on);

/* ChunkProviderServer.chunksToUnload.clear(): drop every queued mark without
 * unloading anything. */
void cl_clear_unload(struct cl *c);
void cl_restore_unload(struct cl *c, const int *coords, int ncoords, int bins);

/* MinecraftServer.initialWorldChunkLoad: the -192..192 spawn loop at tick 0.
 * Runs the whole loop once; the caller replays the spawn search's own load
 * with cl_replay_load first. */
void cl_spawn_area(struct cl *c, int t);

/* The login: PlayerManager.addPlayer at the join position (radius 10,
 * IntegratedPlayerList's initial view distance). No events when the spawn area
 * is already loaded, but the instance map and the managed position start
 * here. */
void cl_login(struct cl *c, int t, double x, double z);

/* The C03 position packet, processed in networkTick of tick t: the position
 * the client sent during tick t - 1. PlayerManager.updateMountedMovingPlayer:
 * the moved square's new instances and the dropped ones. */
void cl_c03(struct cl *c, int t, double x, double z);

/* EntityPlayerMP.onUpdate's chunk send: up to max chunks off the player's
 * queue (PlayerManager's loadedChunks order) that are loaded and ready
 * (Chunk.func_150802_k), each removed from the queue; the S26 bulk carries
 * them in this order. Returns how many. */
int cl_send_chunks(struct cl *c, int t, int (*out)[2], int max);

/* EntityPlayerMP.loadedChunks as a snapshot recorded it: n cx, cz pairs in
 * queue order replace the queue. */
void cl_set_send_queue(struct cl *c, const int *coords, int n);

/* PlayerManager.isPlayerWatchingChunk for the one player: the chunk's
 * instance holds the player and the chunk has left the player's send queue
 * (EntityPlayerMP.loadedChunks), so the client has it. */
int cl_player_watching(struct cl *c, int cx, int cz);

/* PlayerInstance.removePlayer's S21: fn is called for each ready chunk the
 * player stops watching, the empty ground-up chunk that makes the client
 * drop it. */
void cl_on_client_unload(struct cl *c, void (*fn)(void *ctx, int t, int cx, int cz), void *ctx);

/* ServerConfigurationManager.func_152611_a at the integrated server's first
 * tail check (tick 1): 10 to the client's render distance. Drops the ring the
 * new radius loses, in the square walk's order. */
void cl_view_radius(struct cl *c, int t, int radius);
/* The same at any later tick's tail (a row's opts rd): func_152622_a walks
 * the squares around the player's own position (x, z), not the managed one. */
void cl_view_radius_at(struct cl *c, int t, int radius, double x, double z);

/* ChunkProviderServer.unloadQueuedChunks: up to 100 drops, in the unload
 * set's JDK 8 ConcurrentHashMap order. */
void cl_unload_step(struct cl *c, int t);

/* The unload set's contents in its iteration order: cx, cz pairs, up to
 * nmax of them. The count the queue holds (which can exceed nmax). */
int cl_unload_order(struct cl *c, int *out, int nmax);

/* WorldServer.saveAllChunks at tick % 900 == 0: every loaded chunk without a
 * player instance, in the provider's load order. */
void cl_save_marks(struct cl *c, int t);

/* WorldProvider.canRespawnHere (on by default; off for the Nether and the
 * End, whose unloadChunksIfNotNearSpawn queues every chunk). */
void cl_set_respawn(struct cl *c, int on);

/* PlayerManager.func_152622_a on a manager without players. */
void cl_set_radius(struct cl *c, int radius);

/* A hash of the provider's and player manager's state: the unload set, the
 * instances, the send queue, the populated set (the phase digests, phase.h). */
uint64_t cl_digest(const struct cl *c);
int cl_radius(const struct cl *c);

/* PlayerManager's inhabitedTime clock (Chunk.inhabitedTime): the world's
 * total time the instances read (NULL freezes the clock), the 8,000-tick
 * processChunk sweep after WorldServer.tick's block pass (prev_total is the
 * manager's previousTotalWorldTime), and a snapshot's per-instance mark (the
 * instance at cx, cz was marked age ticks ago; 0 when there is none). */
void cl_set_clock(struct cl *c, const int64_t *total_time);
void cl_sweep(struct cl *c, int64_t *prev_total, int64_t now);
int cl_set_mark(struct cl *c, int cx, int cz, int age);

/* PlayerManager.removePlayer: the player leaves every instance of its
 * square; the instances left empty queue their chunks. */
void cl_logout(struct cl *c, int t);

/* The order filterChunkLoadQueue leaves EntityPlayerMP.loadedChunks in after
 * addPlayer at (x, z): cx, cz pairs, (2r+1)^2 of them. */
int cl_send_order(struct cl *c, double x, double z, int *out);

/* ChunkProviderServer.loadChunk as a top-level call (the populate
 * four-neighbor rule runs after the load). */
void cl_replay_load(struct cl *c, int t, int cx, int cz, const char *site);

/* ChunkProviderServer.loadChunk inside the populate call with id pop_id: the
 * load and the unload-set removal, no populate rule. */
void cl_pop_load(struct cl *c, int t, int cx, int cz, int pop_id, const char *site);

/* Per kind, what the run produced: load, pop, mark, unmark, unload. */
void cl_counts(struct cl *c, long counts[5]);

#endif
