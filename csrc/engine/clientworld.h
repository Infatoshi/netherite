#ifndef NETHERITE_CLIENTWORLD_H
#define NETHERITE_CLIENTWORLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "det.h"
#include "jrand.h"
#include "renderstate.h"
#include "tileentity.h"

/* The client world's tile entities whose client copies spend the client's
 * randomness, as Chunk.func_150807_a and func_150806_e make and drop them on
 * the WorldClient, in loadedTileEntityList order: TileEntityDispenser and the
 * dropper's (the constructor's Random: one seeder draw),
 * TileEntityEnchantmentTable (updateEntity's static Random),
 * TileEntityMobSpawner (made by the S35 that follows its chunk; its client
 * updateSpawner places a smoke and a flame on WorldClient.rand while the
 * player is within 16) and TileEntityChest (made by a block change or the
 * S24 that carries numPlayersUsing; its lid sounds pitch on
 * WorldClient.rand). The rest draw nothing on the client and are not kept. */
#define CLIENT_TE_MAX 64
struct client_te { int x, y, z, id; struct te_enchant ench; int players; float lid, prev_lid; };
struct client_tes { struct client_te v[CLIENT_TE_MAX]; int n; };

/* A client cell went from old_id/old_meta to id/meta (a block packet, the
 * client's own placement). */
void client_te_cell(struct client_tes *t, det_state *det, int x, int y, int z, int old_id, int old_meta, int id,
                    int meta);
/* Chunk.func_150806_e for a kept kind (an S35's getTileEntity): the entity
 * there, made when there is none. */
struct client_te *client_te_get(struct client_tes *t, det_state *det, int x, int y, int z, int id);
/* The client unloaded the chunk: its entities go with it. */
void client_te_chunk_gone(struct client_tes *t, int cx, int cz);
/* World.updateEntities' tile entity walk on the client (w the client world,
 * wr its Random or NULL, pl the client's particles; px, py, pz the client
 * player's position): the tables turn to a player within 3 blocks, the
 * spawners burn, the chests move their lids. */
struct particles_live;
struct world;
void client_te_tick(struct client_tes *t, det_state *det, struct world *w, det_rng *wr, struct particles_live *pl,
                    double px, double py, double pz);
#include "texanim.h"
#include "tracker.h"
#include "world.h"
#include "entity.h"

/* WorldClient's entities: copies of the tracked ones, so at most one per
 * tracker entry (tracker.h TRACKER_MAX_ENTRIES); past it the program stops. */
#define CW_MAX_ENTITIES TRACKER_MAX_ENTRIES

struct client_entity {
    /* The fields the entity scans read for every copy (the pick's
     * candidates, checkNoEntityCollision, the box) come first, in the first
     * line (lane/coldaudit: they were spread over five). */
    int id;
    bool is_living;
    bool is_dead;
    bool is_fireball;    /* an S0E type 63/64: EntityLargeFireball/Small, its
                          * client tick integrates accel into motion */
    bool is_large_fireball;  /* type 63: canBeCollidedWith, the pick's 1.0 border */
    bool fireball_burning;   /* its client update's setFire(1) has run: isBurning */
    bool box_kept;           /* below: the box setScaleForAge kept */
    /* !addedToChunk: spawned before the client had its chunk (the join's
     * forceSpawn vehicle lands in ChunkProviderClient's EmptyChunk), in no
     * chunk list; updateEntityWithOptionalForce skips its update once and
     * adds it */
    bool no_chunk;
    /* Chunk.entityLists membership (chunkCoordX/Y/Z, Y clamped) and the
     * insertion stamp that orders a slice's list: the order
     * getEntitiesWithinAABBExcludingEntity hands the pick */
    int chunk_x, chunk_y, chunk_z;
    double x, y, z;
    uint64_t chunk_seq;
    float width, height;
    /* EntityAgeable's copy: the size its onLivingUpdate's setScaleForAge
     * sets (0: not an ageable), and Entity.setSize's box, which keeps its
     * min corner, while no setPosition or move has centred it again
     * (box_kept) */
    struct aabb box;
    float age_width, age_height;

    double prev_x, prev_y, prev_z;    /* prevPos, taken at the start of the entity's update */
    float prev_yaw, prev_pitch;
    struct cell_memo cmemo;   /* what the cells around the copy's box answered (entity.h) */
    double motion_x, motion_y, motion_z;
    double accel_x, accel_y, accel_z;   /* EntityFireball's acceleration */
    float fall_distance, y_size;
    bool on_ground;
    bool in_water;
    float yaw, pitch;
    float head_yaw;
    /* getHealth() <= 0 (the status 3 or the S0F's health): onDeathUpdate
     * counts deathTime and sets the copy dead at 20 */
    bool dying;
    int death_time;
    /* Entity.distanceWalkedModified, distanceWalkedOnStepModified and
     * nextStepDistance: the copy's own moveEntity steps (a step on the
     * redstone ore runs its onEntityWalking in the client world) */
    float dist_walked, dist_walked_step;
    int next_step;

    int kind;                 /* the living kind (living.h), -1 unknown */
    int dw16;                 /* DataWatcher 16 from the S0F and each S1C: the spider's climb flag */
    int flags0;               /* DataWatcher 0 the same way: the burning bit the renderer reads */
    int kindw;                /* the kind's own watched values the same way (combat.c copy_kindw) */
    int creeper_since, creeper_last;  /* a snapshot's copy: EntityCreeper.timeSinceIgnited, lastActiveTime (0 at a spawn) */
    float landed_fall;        /* this tick's move: updateFallState's fall(fallDistance) on landing, 0 none */
    int riding_id, ridden_by_id;  /* ridingEntity / riddenByEntity, 0 none */
    float render_yaw_offset;  /* the body yaw EntityBodyHelper keeps (the chicken's) */
    float body_yaw;           /* EntityBodyHelper.field_75666_b */
    int body_counter;         /* EntityBodyHelper.field_75667_c */
    /* EntitySlime.squishAmount and squishFactor as the copy's own onUpdate
     * moves them (its landings; prevSquishFactor equals squishFactor after
     * each update): RenderSlime and ModelMagmaCube draw them */
    float squish_amount, squish_factor;

    int server_pos_x, server_pos_y, server_pos_z;
    int new_pos_rotation_increments;
    double new_pos_x, new_pos_y, new_pos_z;
    double new_rotation_yaw, new_rotation_pitch;
};
_Static_assert(offsetof(struct client_entity, height) + sizeof(float) <= 64, "the scanned fields in one line");

struct clientworld {
    struct world *world;
    det_state *det;
    jrand rand;
    int update_lcg;
    bool has_void_particles;
    bool simulate_living_physics;
    /* the client's live particles and WorldClient.rand for the tick's
     * entity pass (set by its caller, NULL: the copies' block callbacks
     * draw nothing) */
    struct particles_live *walk_fx;

    struct client_entity ents[CW_MAX_ENTITIES];
    int nents;

    /* Negative check hook: skip one randomDisplayTick draw (torch) */
    bool player_in_water;
    bool negative_check_skip_torch_draw;

    /* EntityRenderer.updateTorchFlicker's state, and the dials' (the items
     * atlas's clock and compass, whose draws off the surface are the client
     * stream's); surface_world is WorldProvider.isSurfaceWorld, which for the
     * default world type is has_void_particles. */
    struct rs_flicker flicker;
    struct texanim_state dials;
    bool surface_world;
};

/* Initialize clientworld with world blocks, det, initial rand state and update_lcg */
void clientworld_init(struct clientworld *cw, struct world *w, det_state *det, uint64_t rand_state, int update_lcg, bool has_void_particles);

/* Process one tracker packet into the client world (spawning, moving and updating client entities) */
void clientworld_handle_packet(struct clientworld *cw, const struct tracker_packet *p);

/* The copy's boundingBox: centred on its position unless an
 * EntityAgeable.setScaleForAge kept it (box_kept) */
struct aabb client_entity_box(const struct client_entity *e);

/* The chunk-list insertion stamps continue past v (a restored client
 * world's entities carry their own, up to v). */
void clientworld_stamp_at_least(uint64_t v);

/* Find entity in clientworld by id, or NULL */
struct client_entity *clientworld_get_entity(struct clientworld *cw, int id);

/* Add an entity to clientworld */
struct client_entity *clientworld_add_entity(struct clientworld *cw, int id, bool is_living,
                                             double x, double y, double z, float yaw, float pitch);

/* Tick client entities (interpolates living entities towards new_pos; the
 * fireballs integrate their acceleration). The player position feeds the
 * smoke particle's distance gate. */
void clientworld_tick_entities(struct clientworld *cw, double player_x, double player_y, double player_z);

/* Minecraft.runTick up to updateRenderer, before the entities update:
 *   1. TextureManager.tick: off the surface the clock and the compass each
 *      draw Math.random (texanim_dials_tick)
 *   2. the player's dig particles (EffectRenderer.addBlockHitEffects)
 *   3. EntityRenderer.updateTorchFlicker (8 draws on mathRandom)
 */
void clientworld_tick_input(struct clientworld *cw, int dig_count);

/* One client world tick, after the entities update:
 *   1. the player's sprint and splash particles (its own onEntityUpdate)
 *   2. setActivePlayerChunksAndCheckLight (4 draws on rand)
 *   3. doVoidFogParticles (1000 cells around player, Det.newRandom and block display ticks)
 * has_chunk says which chunks the client holds (ChunkProviderClient: any
 * other reads as air); the caller keeps that set, since the tracker sends
 * no chunk packets (only the test path, test_clientworld, calls this).
 */
void clientworld_tick(struct clientworld *cw, double player_x, double player_y, double player_z, double prev_px, double prev_py, double prev_pz, bool is_sprinting,
                      bool (*has_chunk)(int cx, int cz));

/* Computes cw digest: rand.seed * 31 + update_lcg */
uint64_t clientworld_cw(const struct clientworld *cw);

/* Block randomDisplayTick dispatch (for torches, water, lava, etc.) */
void clientworld_random_display_tick(struct clientworld *cw, int id, int bx, int by, int bz, det_rng *var5,
                                    double px, double py, double pz);

#endif
