/* The collidable entities that are neither living nor the large fireball
 * (clientworld.c's copy, combat.c's tracker entry), as the client's pick
 * sees them: falling blocks, primed TNT, ender crystals, paintings and item
 * frames. canBeCollidedWith is true for each, so EntityRenderer.getMouseOver
 * points at them and a left click attacks them.
 *
 * The server half is EntityTracker for these kinds: the entry at
 * spawnEntityInWorld (S0E, the painting's S10, and the S12 of the kinds that
 * send velocity), the per-tick sendLocationToAllClients (S15, S16, S17, S18,
 * the velocityChanged S12) and the S13 when the entity leaves the world. The
 * client half is the WorldClient copy those packets build (handleSpawnObject,
 * handleSpawnPainting, handleEntityMovement, handleEntityTeleport,
 * handleEntityVelocity, handleDestroyEntities) and each copy's own onUpdate
 * (EntityFallingBlock's and EntityTNTPrimed's moveEntity, the static crystal and hanging), plus the
 * client's own attackTargetEntityWithCurrentItem on a copy.
 *
 * The server-side attack (C02 ATTACK on one of them: processUseEntity and
 * attackTargetEntityWithCurrentItem) is pickobj_server_attack.
 */
#ifndef NETHERITE_PICKOBJ_H
#define NETHERITE_PICKOBJ_H

#include <stdint.h>

#include "det.h"
#include "entity.h"
#include "pick.h"
#include "tracker.h"
#include "clientworld.h"

struct serverreplay;
struct client_player;
struct world;
struct pickobj_dragon;

/* The client copy of one entity. */
struct pickobj {
    int id;
    int kind;                 /* PK_FALLING, PK_TNT, PK_CRYSTAL, PK_PAINTING, PK_FRAME */
    int dead;
    struct entity e;
    int server_x, server_y, server_z;  /* serverPosX/Y/Z */
    float yaw, pitch;
    int fuse;                 /* EntityTNTPrimed */
    int block, meta;          /* EntityFallingBlock */
    int tile_x, tile_y, tile_z, dir, art; /* EntityHanging */
    struct pickobj_dragon *dragon; /* PK_DRAGON: the client EntityDragon and its parts */
    /* Chunk.entityLists membership: chunkCoordX/Y/Z and the insertion stamp */
    int cx, cy, cz;
    uint64_t seq;
};

/* One server-to-client message for these entities, in send order. */
struct pickobj_packet {
    int kind;                 /* PKO_SPAWN, PKO_MOVE, PKO_TELEPORT, PKO_VELOCITY, PKO_DESTROY */
    int id, obj_kind;
    int x, y, z;              /* S0E/S18 scaled position, S15/S17 deltas; hanging: the tile */
    int has_pos, has_rot;
    int yaw, pitch;           /* byte angles */
    int mx, my, mz;           /* S12 velocity * 8000 */
    int data;                 /* falling: block | meta << 16; hanging: direction; painting: art */
    int art;
    float health;             /* the dragon's S0F and S1C: DataWatcher 6 */
    uint64_t seq;             /* its place among every tracker packet (combat.h) */
    int pre;                  /* a move's setPositionAndRotation2 ran at the pump: pre_x.. is where it left the copy */
    double pre_x, pre_y, pre_z;
};
enum { PKO_SPAWN = 1, PKO_MOVE, PKO_TELEPORT, PKO_VELOCITY, PKO_DESTROY, PKO_META, PKO_COLLECT, PKO_DEATH };

/* The server entity behind a tracker entry, as the tick last saw it. */
struct pickobj_track {
    int id, kind;
    float health;             /* the dragon's health as its watcher last got it */
    int before_player;        /* ahead of the player in the entity pass */
    int gone_pending;         /* left the world after the player's update: S13 next tick */
    int death_sent;           /* the dragon's S19 status 3 went out */
    int made_before_player;   /* tracked this tick before the pass reached the player */
};

/* The objects the replay tracks for the pick: at most one per client copy
 * (pick.h PICKOBJ_MAX_OBJS); past it the program stops. */
#define PICKOBJ_MAX_TRACK PICKOBJ_MAX_OBJS

struct pickobj_state {
    /* server */
    struct tracker tr;
    struct pickobj_track trk[PICKOBJ_MAX_TRACK];
    int ntrk;
    int destroyed[PICKOBJ_MAX_TRACK];   /* EntityPlayerMP.destroyedItemsNetCache */
    int ndestroyed;
    int player_watch_initialized;
    double player_watch_x, player_watch_y, player_watch_z;
    /* in flight to the client, handled at its next updateController */
    struct pickobj_packet *pend;
    int npend, cappend;
    int s07_mark;             /* 1 + npend at a transfer's S07: the rest wait for the new world */
    int pend_done;            /* the pending packets the pump already handled */
    /* client */
    det_state *det;           /* the client's Det streams (the constructors' draws) */
    struct world *client_world;
    struct pickobj *objs;
    int nobjs, capobjs;
    uint64_t own_seq;         /* the chunk-list stamp when no living world shares it */
    /* the dragon's spawnEntityInWorld: its tracker entry was made then, in
     * whatever world the player was, and has counted every tick since */
    int dragon_born_id, dragon_born_tick;
    /* the replay's first tracker pass (its tick): the other worlds' entries
     * made then are the snapshot's own */
    int first_pass_done, first_tick;
    /* the client world's tile entities that draw (clientworld.h) */
    struct client_tes ctes;
};

/* combat_bind's companion: the state on both sides, and the entities
 * already in the world (in range: spawned on the client at once). */
void pickobj_bind(struct serverreplay *sr, struct client_player *cp);
/* spawnEntityInWorld for any entity the replay adds (sr_order_push). */
void pickobj_server_spawn(struct serverreplay *sr, int pool, void *p);
/* EntityTracker.func_85172_a for the dragon's entry (chunk cx, cz sent). */
void pickobj_chunk_sent(struct serverreplay *sr, int cx, int cz);
/* EntityPlayerMP.onItemPickup's S0D for entity id (an item, an orb, an
 * arrow): EntityTracker.func_151247_a sends it to the players its entry has */
void pickobj_server_collect(struct serverreplay *sr, int id);
/* EntityLivingBase.onDeath's setEntityState(3) for the dragon: the S19 to
 * the watchers when the killing damage lands (once). */
void pickobj_server_dragon_death(struct serverreplay *sr, int id);
/* EntityTracker.updateTrackedEntities for these kinds, the player's world. */
/* removePlayerFromTrackers at a transfer: every entry watching the player
 * lets it go, each id into destroyedItemsNetCache (the S13 at the player's
 * next onUpdate, in the new world). */
void pickobj_server_player_left(struct serverreplay *sr);
/* A portal traveller leaves its old world's tracker (untrackEntity). */
void pickobj_server_untrack(struct serverreplay *sr, int id);
void pickobj_server_tick(struct serverreplay *sr);
/* The respawn's trackEntity(new player): every entry tries it at once,
 * which only a forceSpawn entry (a leash knot) passes before its chunk is
 * sent. */
void pickobj_server_respawned(struct serverreplay *sr);
/* The respawn's S07 goes out: the packets after it build in the client's
 * new world when the S07 changes it. */
void pickobj_server_mark_s07(struct serverreplay *sr);
/* The player's C02 ATTACK on entity_id; 0 when the id is none of these. */
int pickobj_server_attack(struct serverreplay *sr, int entity_id);
/* processUseEntity's INTERACT on a tracked object (the item frame's
 * interactFirst); count is the held stack's size. */
int pickobj_server_interact(struct serverreplay *sr, int entity_id, int item, int damage, int tag, int *count);
/* The client's interactWith on its copy: true for an item frame. */
int pickobj_client_interact(struct client_player *cp, int entity_id);
/* ItemHangingEntity.onItemUse's client half for the frame: the constructor
 * draws and onValidSurface. */
int pickobj_client_hanging_fits(struct client_player *cp, det_state *det, int kind, int x, int y, int z, int dir);

/* The client's updateController (the queued packets) and updateEntities for
 * the copies. */
void pickobj_client_tick(struct client_player *cp);
/* World.updateEntities' tile entity pass on the client world, after every
 * entity (the rider's update included). */
void pickobj_client_te_tick(struct client_player *cp);
/* The pump's object packets sent ahead of tracker packet SEQ (the living
 * tracker's next), handled now; pickobj_client_tick handles the rest. */
void pickobj_client_packets_before(struct client_player *cp, uint64_t seq);
/* updateController's processReceivedPackets, at its place in runTick
 * (after getMouseOver, before the input): the object copies' S14, S15 and
 * S18 run setPositionAndRotation2 now, whose collision query touches the
 * blocks' shared bounds (a stair's octant from the mouseover goes back to
 * the full cube before the use's placement check); the rest of each
 * packet is handled with the copies' ticks (pickobj_client_tick). */
void pickobj_client_pump_positions(struct client_player *cp);
/* A new WorldClient (a respawn into another dimension). */
void pickobj_client_world_change(struct client_player *cp);
/* The queued packets, handled now in the current world. */
void pickobj_client_flush(struct client_player *cp);
/* PlayerControllerMP.attackEntity's local attackTargetEntityWithCurrentItem
 * on the copy: 1 when entity_id is one of them. The client player's own
 * sprint knockback is the caller's (returned in *knock). */
int pickobj_client_attack(struct client_player *cp, int entity_id, int *knock);
/* The kind (PK_*) of the client's object copy with this id, 0 for none. */
int pickobj_client_kind(struct client_player *cp, int entity_id);

/* World.checkNoEntityCollision over the copies: 1 when a live one that
 * sets preventEntitySpawning meets box. */
int pickobj_client_prevents(const struct client_player *cp, const struct aabb *box);

/* The copies as pick entries and candidates: fills up to cap of each and
 * returns the count. base is the index pe[0] has in the caller's array (a
 * dragon's parts name it as their owner there). */
int pickobj_client_candidates(struct client_player *cp, struct pick_entry *pe,
                              struct pick_cand *pc, int cap, int base);

/* The client's copies as the live client draws them (read only, between
 * ticks): the list, and a PK_DRAGON copy's EntityDragon with its
 * setPositionAndRotation2 state (NULL for any other kind). */
const struct pickobj *pickobj_client_objs(const struct client_player *cp, int *n);
struct dragon_state;
struct dragon_interp;
const struct dragon_state *pickobj_client_dragon(const struct pickobj *o, const struct dragon_interp **ip);

#endif
