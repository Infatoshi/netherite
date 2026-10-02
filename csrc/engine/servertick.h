/* The 1.7.10 server tick for the overworld, tick after tick, against the
 * oracle's ServerTickProbe recording (oracle/harness/netherite/oracle/ServerTickProbe.java):
 * WorldServer.tick() and the World.tick() inside it, run from a real
 * spawn-area state with the mob spawning gamerule off and the probe's weather.
 *
 * The tick head (World.tick -> updateWeather, calculateSkylightSubtracted, the
 * day time and the total time) is servertick_tick_head, and servertick_tick is
 * the whole WorldServer.tick in vanilla's order:
 *
 *   tickUpdates(false)   the pending set of ticks.c popped in tree order, the
 *                        1000-per-tick cap, the checkChunksExist re-schedule,
 *                        then Block.updateTick by block id;
 *   func_147456_g        the active chunk set in its recorded iteration order,
 *                        the ambient countdown, the parked player's light
 *                        re-check, func_147467_a's mood sound, Chunk.func_150804_b,
 *                        the thunder draw, the ice and snow draws and the
 *                        per-section random block ticks;
 *   the writes           every block write reaches the recorder (writes), and
 *                        every entity a constructor makes reaches the spawn
 *                        recorder with its Det draws already spent.
 *
 * What the tick does not execute, because the recording shows it does nothing
 * observable for one parked player: chunkProvider.unloadQueuedChunks (the
 * probe cancels the join's unload queue), thePlayerManager.updatePlayerInstances
 * (no view-radius change, no 8000-tick chunk pass inside 2400 ticks),
 * villageCollectionObj.tick and villageSiegeObj.tick (no village in range),
 * worldTeleporter.removeStalePortalLocations (an empty cache) and
 * func_147488_Z (both block event queues stay empty). out/report.md records
 * each of those assumptions.
 */
#ifndef NETHERITE_SERVERTICK_H
#define NETHERITE_SERVERTICK_H

#include <stdint.h>
#include "det.h"
#include "jrand.h"
#include "snapshot.h"
#include "world.h"

/* One recorded block write, the oracle's 16-byte row: x, y, z, the block id
 * (0xffff for a metadata-only write), the metadata. */
struct st_write {
    int32_t x, y, z;
    int16_t id;
    uint8_t meta;
    uint8_t pad;
};

/* st_write.pad: a write whose flags lack 2 (markBlockForUpdate), which no
 * packet carries to the client (leaf decay's metadata) */
#define ST_WRITE_UNSENT 0x20

/* One entity a tick constructed, the fields spawns.jsonl.gz carries. */
struct st_spawn {
    int t;
    int dim;
    int id;              /* the entity id, Det's per-role counter */
    const char *cls;
    int weather;         /* 0 loadedEntityList, 1 weatherEffects */
    double x, y, z, mx, my, mz;
    float yaw;
    int64_t uuid_msb, uuid_lsb;
    uint64_t rand_state;                   /* the entity's own Random, Det.newRandom's state */

    int item, damage, count, age, delay;  /* EntityItem */
    float hover;
    int64_t bolt_vertex;                   /* EntityLightningBolt */
    int bolt_living_time;
    int tile, tile_id, data, time, air;    /* EntityFallingBlock */
    int drop_item, hurt_entities, fall_hurt_max;
    float fall_hurt_amount;
    int on_ground;
};

struct aabb;

/* The most block writes and entity spawns one tick records. */
#define ST_MAX_WRITES (1 << 22)
#define ST_MAX_SPAWNS (1 << 14)

/* The most chunks one tick's active set holds (serverreplay's SR_MAX_ACTIVE,
 * a view distance of 16). */
#define ST_MAX_ACTIVE 1089

/* A servertick's World.rand and World.updateLCG, as lvalues (the file that
 * uses them includes env.h). */
#define ST_RAND(s) (nw_env->rng.world[(s)->rng_slot].rand)
#define ST_LCG(s) (nw_env->rng.world[(s)->rng_slot].update_lcg)

struct servertick {
    struct world *w;
    det_state *det;                /* Det's streams, the SERVER role's draws */

    /* World.rand (the 48-bit state of the snapshot) and World.updateLCG
     * live in the environment's random-stream table (env.h struct
     * rng_table), in this world's slot: ST_RAND(s) and ST_LCG(s) */
    int rng_slot;
    int skylight_subtracted;       /* World.skylightSubtracted */
    float raining_strength, prev_raining_strength;
    float thundering_strength, prev_thundering_strength;
    int64_t total_time;            /* worldInfo.getWorldTotalTime(), NBT "Time" */
    int64_t world_time;            /* worldInfo.getWorldTime(), NBT "DayTime" */
    int daylight_cycle;            /* doDaylightCycle gamerule */
    int rain_time, thunder_time;   /* worldInfo's, NBT rainTime / thunderTime */
    int raining, thundering;
    int64_t next_tick_entry;       /* NextTickListEntry.nextTickEntryID */

    /* WorldServer's own fields the tick head reads. */
    int all_players_sleeping;

    /* The world's provider and players: no_sky is WorldProvider.hasNoSky
     * (the Nether and the End: no weather, a fixed celestial angle), and
     * no_player a world whose playerEntities is empty (no player light
     * check, and the caller hands an empty active set). Both 0 for the
     * overworld with its player. */
    int no_sky;
    int no_player;
    int mob_spawning;           /* the doMobSpawning gamerule (BlockPortal's pigman roll) */

    /* The tick body's own state: World.ambientTickCountdown, the difficulty the
     * lightning bolt's fire placement reads, and the parked player. */
    int ambient_tick_countdown;
    int difficulty;
    int fire_rule_off;          /* GameRules.doFireTick == false */
    double player_x, player_y, player_z;

    /* World.activeChunkSet in the iteration order the recording carries: it is
     * the parked player's JDK 8 HashSet order, so the probe hands it over. */
    int active[2 * ST_MAX_ACTIVE]; /* cx, cz pairs */
    int nactive;

    /* World.rand draws the tick head needs but the row does not carry: the
     * probe force-writes the weather flags before the snapshot, which is what
     * sets the strengths' starting point. */
    int tick;                      /* the tick number, 1-based */

    /* The recorded output. */
    struct st_write *writes;
    int nwrites, capwrites;
    struct st_spawn *spawns;
    int nspawns, capspawns;
    int hiwrites, hispawns;        /* the most either list held since servertick_trim */

    /* The tape replay's entity list: called after the tick recorded a
     * constructed entity, with the record it wrote. The Det draws are spent by
     * then, so the callee adopts the entity instead of constructing it. */
    void (*on_spawn)(void *ctx, const struct st_spawn *sp);
    void *on_spawn_ctx;

    /* The mob spawner's slot in the tick, where
     * SpawnerAnimals.findChunksForSpawning sits in WorldServer.tick: after
     * World.tick's updateWeather, before the skylight level and the clocks.
     * The spawning lane installs one; NULL for every other user. */
    void (*on_spawner)(void *ctx, struct servertick *s);
    void *on_spawner_ctx;

    /* World.getEntitiesWithinAABB(EntityArrow.class, box) is non-empty, for
     * the wooden button's tick. NULL (no arrows) for the probe users. */
    int (*arrows_in)(void *ctx, const struct aabb *box);
    void *arrows_ctx;

    /* BlockBasePressurePlate.updateTick, the scheduled and random-tick
     * dispatch for a plate (blocks 70 and 72). BlockBasePressurePlate
     * constructor-set tickRandomly(true), so both paths reach the same
     * virtual call; it only acts while the plate is down. ctx is the replay
     * (the count queries its entity lists). NULL otherwise. */
    void (*plate_tick)(void *ctx, int x, int y, int z, int id);
    void *plate_tick_ctx;

    /* BlockPortal.updateTick's spawn once the roll, the floor search and the
     * isNormalCube check passed: ItemMonsterPlacer.spawnCreature(57) at
     * (x, y, z), then timeUntilPortal = getPortalCooldown. ctx is the replay.
     * NULL: the hit is reported as not ported. */
    void (*portal_spawn)(void *ctx, double x, double y, double z);
    void *portal_spawn_ctx;

    /* ChunkProviderServer.unloadQueuedChunks, which WorldServer.tick runs just
     * before tickPending. Nothing observable for the parked-player recordings,
     * so it stays NULL there; the tape replay drives its own provider. */
    void (*on_unload_step)(void *ctx, int t);
    void *on_unload_step_ctx;

    /* VillageSiege's own state (field_75536_c, field_75535_b, field_75533_d,
     * field_75534_e). field_75536_c starts at -1 in vanilla (VillageSiege is
     * not saved, so a snapshot load starts at -1) and the world's first
     * daytime tick sets it to 0; a snapshot taken at night keeps it there, so
     * the midnight roll never runs that night. */
    int siege_phase;
    int siege_mustering;
    int siege_count;
    int siege_timer;
    /* VillageSiege.func_75529_b, the muster's village lookup (the replay's
     * village collection, on this tick's World.rand); NULL: no village, no
     * draw. siege_deferred: the caller runs servertick_village_siege itself,
     * after its VillageCollection.tick (WorldServer.tick's order). */
    int (*siege_muster)(void *ctx, jrand *rand);
    void *siege_muster_ctx;
    int siege_deferred;

    /* WorldServer.areAllPlayersAsleep's player walk (1 when every player is
     * fully asleep) and wakeAllPlayers' (each sleeping player's
     * wakeUpPlayer(false, false, true)); the head does the time skip and the
     * weather reset around them. NULL for every user but the tape replay. */
    int (*on_sleep_check)(void *ctx);
    void (*on_wake_all)(void *ctx, struct servertick *s);
    void *on_sleep_ctx;
};

/* Load DIR's snapshot into snap (the world included) and the tick state above
 * out of worldinfo.nbt and worldstate.nbt. 0 on a missing or malformed file. */
int servertick_load(struct servertick *s, struct snapshot *snap, const char *dir);

/* Det's streams out of the snapshot's det.nbt (Det's own save format). */
void servertick_load_det(struct servertick *s, det_state *det, const nbt *detnbt);

/* World.tick -> World.updateWeather, exactly WorldServer.updateWeather's
 * World half: the thunder timer and toggle, the two strengths stepped by 0.01
 * and clamped, then the same for rain. World.rand draws the next timer only
 * when one has run out. */
void servertick_update_weather(struct servertick *s);

/* WorldServer.tick's head, in vanilla's order: World.tick, the all-players-
 * asleep shortcut (no player is asleep here), calculateSkylightSubtracted, the
 * total time and then the day time. */
void servertick_tick_head(struct servertick *s);

/* World.calculateSkylightSubtracted(1.0F), over getCelestialAngle,
 * getRainStrength and getWeightedThunderStrength. */
int servertick_skylight(struct servertick *s);
/* World.isThundering: the weighted thunder strength over 0.9. */
int servertick_is_thundering(const struct servertick *s);
float servertick_celestial_radians(struct servertick *s);

/* One whole WorldServer.tick: the head, then tickUpdates(false), then
 * func_147456_g. Tick T is the Tth call; the spawns it makes are recorded with
 * it. */
void servertick_tick(struct servertick *s, int t);

/* WorldServer.tick for a world with no players and no loaded chunks (the
 * Nether and the End on a tape whose snapshot holds none): the head and the
 * empty chunk walk's ambient countdown decrement, nothing else. has_sky is
 * WorldProvider.hasNoSky's inverse: the Nether and the End have no sky, so
 * World.updateWeather is a no-op for them and draws nothing. */
void servertick_tick_empty(struct servertick *s, int has_sky);

/* The state the tick body reads that the snapshot files do not carry, handed
 * over by the recording's manifest: World.ambientTickCountdown, the difficulty
 * the lightning bolt reads, the parked player and the active chunk set in its
 * recorded iteration order. */
void servertick_set_ambient(struct servertick *s, int ambient_tick_countdown, int difficulty);
void servertick_set_player(struct servertick *s, double x, double y, double z);
void servertick_set_active(struct servertick *s, const int *pairs, int nactive);
/* make the tick's fixed write and spawn lists (with the world) */
void servertick_reserve(struct servertick *s);

/* The recorded output, since the last servertick_clear. */
/* The write and spawn lists' pages past what they hold now, that a larger
 * tick wrote, back to the kernel (arena.h fixed_array_trim): a snapshot's
 * first ticks write hundreds of thousands of blocks, a tick after a few. */
void servertick_trim(struct servertick *s);
const struct st_write *servertick_writes(const struct servertick *s, int *n);
const struct st_spawn *servertick_spawns(const struct servertick *s, int *n);
void servertick_clear(struct servertick *s);

/* VillageSiege.tick on the world's own Random (see servertick.c). */
void servertick_village_siege(struct servertick *s);

/* BlockButton.func_150046_n for the wooden button at x,y,z (servertick.c):
 * arrows_in answers getEntitiesWithinAABB(EntityArrow.class, box). */
struct aabb;
void servertick_wood_button(struct world *w, int x, int y, int z,
                            int (*arrows_in)(void *ctx, const struct aabb *box), void *ctx);

/* S6, func_147456_g's chunk loop, alone (the device build's entry) */
void servertick_chunk_loop(struct servertick *s);
/* S1, S2, S1T, S4, S5, S6 or SG (phase.h's id) alone, as servertick_tick
 * runs it: 1, or 0 for another id (the device build's entry) */
int servertick_phase(struct servertick *s, int id);

#endif
