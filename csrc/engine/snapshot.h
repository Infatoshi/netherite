/* The oracle's snapshot of the whole game state at a tape's start: the frame
 * the native tape replay begins from.
 *
 * oracle/harness/netherite/oracle/Snapshot.java writes it; the manifest next to the
 * files documents every byte. Loading fills a struct world (the server's loaded
 * overworld chunks, byte for byte) and keeps everything else the next lanes
 * need: the client player's fields, every entity and tile entity as canonical
 * NBT, the pending block ticks, the world Random, Det's streams and the client
 * world's own chunk set.
 *
 * chunks.bin.gz is gzipped (the chunks are mostly air: 173 MB in, 2 MB out), so
 * this reader uses zlib, which every native tool already links.
 */
#ifndef NETHERITE_SNAPSHOT_H
#define NETHERITE_SNAPSHOT_H

#include <stdint.h>
#include "nbtjson.h"
#include "tape.h"
#include "world.h"
#include "living.h"

/* The oracle's chunk byte layout, Probe.CHUNK_BYTES. */
#define SNAP_CHUNK_BYTES (2 * 65536 + 3 * 65536 + 2 * 256 * 4 + 4 + 2)
#define SNAP_CHUNK_TAIL (2 * 256 * 4 + 4 + 2)
#define SNAP_BIOME 256
#define SNAP_COLUMNS 256

struct snap_tile {
    nbt *tag;        /* the tile entity's own NBT as recorded */
    char *text;      /* the canonical text, for the round-trip check */
    struct jval *rt; /* its fields NBT does not carry (tilesrt), or NULL */
};

/* EntityDragon's state NBT does not carry ("dragon" in entities.jsonl). */
struct snap_dragon {
    double target[3];
    int ring_index, has_ring;
    double ring[64][2];              /* yaw, y */
    float anim, prev_anim;
    int force, slowed;
    int hunt;                        /* the target's entity id, -1 none */
    int death_ticks;
    int heal;                        /* healingEnderCrystal's entity id, -1 none */
    int ticks_existed, hurt_resistant_time;
    float last_damage, prev_health, yaw_velocity, render_yaw, prev_yaw;
    double prev[3];
    double bb[5];                    /* minX minY minZ maxX maxZ */
    int part_id[7];
    double part[7][3];
    float part_w[7], part_h[7];
};

struct snap_entity {
    int dim, id;
    uint64_t rand_state;             /* its own Random now */
    uint64_t rand_born;              /* the state it was born with (has_born) */
    int has_born, has_gauss;
    double gauss;                    /* the cached nextGaussian (has_gauss) */
    float width;                     /* an XP orb's or a falling block's width ("size"); 0 when not recorded */
    int has_path;                    /* a living's active nav path (pathi) */
    int path_index;
    double path_speed;
    int path_n;                      /* points in path_pts */
    int (*path_pts)[3];              /* malloc'd, path_n of them */
    int has_watch;                   /* a running watch-closest task (watch) */
    int watch_is_player;
    int watch_look_time;
    char *cls;
    int player, weather;
    int has_age, age, lsf;           /* EntityLivingBase.entityAge, EntityLiving.livingSoundTime (has_age: recorded) */
    struct snap_dragon *dragon;      /* an EntityDragon's unsaved state, or NULL */
    int has_crystal, crystal_rotation, crystal_health;   /* EntityEnderCrystal innerRotation, health */
    int has_throwable, ticks_in_air, ticks_in_ground, thrower, ticks_existed; /* EntityThrowable; thrower -1 none */
    int has_vil, vil[3];             /* a villager's village index, randomTickDivider, isLookingForHome; a golem's index, homeCheckTimer */
    /* its EntityTrackerEntry (trk, trkd): ticks, ticksSinceLastForcedTeleport,
     * last scaled x y z, last yaw pitch head bytes, isDataInitialized; the
     * last sent motion x y z and the last range check's position x y z */
    int has_trk, trk[9];
    double trkd[6];
    nbt *tag;
    char *text;
    struct jval *rt;                 /* the runtime state (EntityState.java), or NULL */
    struct jval *trk_rt;             /* its EntityTrackerEntry's fields (trkrt), or NULL */
    int cidx;                        /* its index in its chunk slice's list, -1 unknown */
};

/* One pending block tick (NextTickListEntry). list: 0 the tree set the world
 * iterates, 1 pendingTickListEntriesThisTick. */
struct snap_tick {
    int dim, list;
    int x, y, z, id, priority;
    int64_t scheduled, entry;
};

/* One saved pending tick of a chunk that is not loaded (seedticks.jsonl.gz):
 * the region file's TileTicks entry verbatim, t being the delay relative to
 * that chunk's own last save. */
struct snap_seed_tick {
    int dim, cx, cz;
    int x, y, z, id, priority;
    int64_t t;
};

/* One unloaded overworld chunk's region Entities (seedents.jsonl.gz): what
 * the chunk re-adds at its next load, canonical NBT in list order. */
struct snap_seed_chunk {
    int cx, cz;
    int nents;
    nbt **ents;
};

/* One chunk in chunkstate.jsonl plus the chunk bytes it matches in chunks.bin.gz. */
struct snap_chunk {
    int dim, cx, cz;
    uint64_t hash, near;
    uint8_t *bytes;                  /* SNAP_CHUNK_BYTES, as the oracle wrote them, or
                                      * NULL: snapshot_chunk_want reads live */
    struct chunk *chunk;             /* that chunk until load_world inserts it */
    struct chunk *live;              /* the chunk load_world inserted it as: the
                                      * recorded bytes until the replay changes
                                      * or unloads the chunk */
    uint8_t biome[SNAP_BIOME];
    int terrain_populated, light_populated, populated, modified, has_entities, send_updates, gap;
    int saved;                       /* the region files hold it; the world does not */
    int64_t last_save_time, inhabited_time;
    int queued_light_checks;
    uint8_t skylight_columns[SNAP_COLUMNS];
    struct snap_tile *tiles;
    int ntiles;
    int *chests;                     /* chunkstate's chests: 9 ints per chest (Snapshot.chestState) */
    int nchests;
    nbt **ents;                      /* the saved chunk's region entities, in NBT order */
    int nents_saved;
    struct snap_tick *saved_ticks;   /* the region chunk's own TileTicks */
    int nsaved_ticks;
};

struct snapshot {
    char dir[1024];
    int nocrc;                       /* loaded without the gzip CRC-32 checks (snapshot_load_crc) */
    int64_t seed, tick;
    int version;
    struct world world;              /* dim 0's chunks (the overworld) */
    struct world hell_world;         /* dim -1's chunks, when the snapshot has any */
    struct world end_world;          /* dim 1's chunks, when the snapshot has any */
    struct world *hell_saved;        /* dim -1's region-file chunks (saved:1), or NULL */
    struct world *end_saved;         /* dim 1's region-file chunks (saved:1), or NULL */
    int hell_loaded, end_loaded;     /* the snapshot carried loaded chunks for that dim */
    struct snap_chunk *chunks;
    int nchunks;
    struct snap_entity *ents;
    int nents;
    /* ghosts.jsonl: the living entities no world lists that a listed one's
     * runtime state references (its chunk unloaded, or it died), in the
     * entities' form; none in a snapshot from before the file */
    struct snap_entity *ghosts;
    int nghosts;
    struct snap_tick *ticks;         /* the tree set, in the world's order */
    int nticks;
    struct snap_tick *this_tick;     /* pendingTickListEntriesThisTick */
    int nthis;
    struct snap_seed_tick *seed_ticks; /* the region saves of the unloaded chunks */
    int nseed_ticks;
    int has_seedticks;               /* seedticks.jsonl.gz was present: the
                                      * seed ticks come from the region files,
                                      * not the populate re-derivation */
    struct snap_seed_chunk *seed_chunks; /* seedents.jsonl.gz, or none */
    int nseed_chunks;
    int has_seedents;                /* seedents.jsonl.gz was present: the
                                      * unloaded overworld chunks' entities
                                      * come from the region files, not the
                                      * seed world's build */
    nbt *worldinfo, *worldstate, *det, *clientworld, *player_client, *player_server;
    char *manifest;
    struct jval *manifest_json;
    /* clientents.jsonl: the client world's entities but the player, in its
     * loadedEntityList order (a snapshot since the any-tick sweep), or none */
    struct jval **client_ents;
    int nclient_ents, has_client_ents;
    int players;
    /* stats.json (Stats.java): the server player's StatisticsFile and the
     * client's mirror; NULL for a snapshot from before the file */
    char *stats_json;
};

/* Load DIR (the manifest, the chunks, the entities, the ticks, the six NBT
 * files) into a struct world and the lists above. 0 on any missing file or
 * malformed content, with the reason on stderr. */
int snapshot_load(struct snapshot *s, const char *dir);
/* snapshot_load without the gzip CRC-32 checks when check_crc is 0 (the
 * lengths and every structural check stay): a replay whose recording's load
 * check (test_snapshots --load-check) reads the same files with them. */
int snapshot_load_crc(struct snapshot *s, const char *dir, int check_crc);
void snapshot_free(struct snapshot *s);
/* The entities' and ghosts' runtime blocks (rt, trk_rt) and the client's
 * entities (client_ents) freed: what only the session's open reads
 * (sr_apply_runtime, endfight's dragon and crystals, combat_bind's restore
 * and pickobj_bind). A run that takes no dev op never binds again (dev.c's
 * summons bind a mob-free start's combat late), so a start the env pool
 * copies to every env drops them once open (runtime/pool.c, lane/heapprof:
 * about 15 MB an env at the speedrun start). */
void snapshot_drop_runtime(struct snapshot *s);

/* The oracle's chunk byte layout for one chunk, into out[SNAP_CHUNK_BYTES]. */
void snapshot_chunk_bytes(const struct chunk *c, uint8_t *out);
/* The part of it after the cells, into out[SNAP_CHUNK_TAIL]. */
void snapshot_chunk_tail(const struct chunk *c, uint8_t *out);
/* The layout's cells and maps into c (storage for bands with a nonzero cell). */
void snapshot_chunk_from_bytes(struct chunk *c, const uint8_t *p);
/* A snapshot chunk's recorded bytes: sc->bytes, or its live chunk's written
 * into buf[SNAP_CHUNK_BYTES]; NULL when neither exists. */
const uint8_t *snapshot_chunk_want(const struct snap_chunk *sc, uint8_t *buf);

/* FNV-1a 64 continued from h over n bytes (runs of zero bytes folded into
 * one multiply each; the same value as byte by byte). */
uint64_t snapshot_fnv(uint64_t h, const uint8_t *p, size_t n);

/* FNV-1a 64 of one chunk's bytes, and of the 3x3 around it (Probe.hashAround:
 * a chunk that is not loaded contributes one zero byte). */
uint64_t snapshot_chunk_hash(const struct chunk *c);
uint64_t snapshot_chunk_hash_around(struct world *w, int chx, int chz);

/* The same for n chunks at once, out[i] for c[i] and for the 3x3 around
 * (chx[i], chz[i]) in w[i], SNAP_HASH_LANES chains per pass. */
#define SNAP_HASH_LANES 8
void snapshot_chunk_hash_n(const struct chunk *const *c, int n, uint64_t *out);
void snapshot_chunk_hash_around_n(struct world *const *w, const int *chx, const int *chz, int n, uint64_t *out);
/* The same values as snapshot_chunk_hash_around_n (and, with own, what
 * snapshot_chunk_hash_n gives for own[i] into own_out[i]), each chunk read
 * once: its
 * chain is recorded as the (byte, multiplier) steps it takes and replayed
 * for each of the up to nine windows it is in. (FNV's chain cannot start a
 * window from another's value: each byte is xor then multiply, so a chunk's
 * effect depends on the state it starts from; only the reading is shared.) */
void snapshot_chunk_hash_around_all(struct world *const *w, const int *chx, const int *chz, int n, uint64_t *out,
                                    const struct chunk *const *own, uint64_t *own_out);

/* The first place two chunk byte arrays differ, as "field count index want got"
 * (ids, meta, sky, blocklight, height, precip, heightMinimum, mask); 0 when
 * they are equal. */
int snapshot_chunk_diff(const uint8_t *want, const uint8_t *got, char *out, size_t outn);

struct tile_entity;
/* A tile entity's runtime fields from its tilesrt entry (NULL: none). */
void snap_tile_runtime(struct tile_entity *te, const struct jval *rt);

#endif
