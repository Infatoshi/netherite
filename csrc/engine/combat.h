/* Player entity targeting and C02 USE_ENTITY ATTACK in the whole-server tick. */
#ifndef NETHERITE_COMBAT_H
#define NETHERITE_COMBAT_H

#include "clientworld.h"

struct client_player;
struct server_player;
struct serverreplay;

/* One tracker packet on its way to the client world, with the S0F's entity
 * size (the client model's own setSize). */
struct combat_pending {
    struct tracker_packet pkt;
    float width, height;
    float age_width, age_height;  /* an ageable's setScaleForAge size, 0 none */
    int kind;                 /* the S0F's living kind */
    uint64_t seq;             /* its place among every tracker packet */
};

struct combat_state {
    struct clientworld client;
    struct tracker tracker;
    /* The server tick's tracker packets. NetHandlerPlayClient handles them in
     * the next client tick's updateController, after that tick's pick. */
    struct combat_pending *pending;
    int npending, cap_pending;
    /* a transfer's S07 among them: 1 + the count queued ahead of it (0:
     * none); the ones after it reach the new WorldClient */
    int s07_mark;
    /* the player's own EntityTrackerEntry: where its last range check ran */
    int player_watch_initialized;
    double player_watch_x, player_watch_y, player_watch_z;
    /* the replay's first tracker pass (its tick): the other worlds' entries
     * made then are the snapshot's own */
    int first_pass_done;
    int first_quiet_tick;
    /* the client's deflect of its fireball copy: runTick's clickMouse comes
     * after updateController's packets, the click here before them, so the
     * motion lands after the next apply_pending */
    int deflect_id, deflect_sprint;
    double deflect_x, deflect_y, deflect_z;
    float deflect_yaw;
    /* EntityPlayerMP.destroyedItemsNetCache: a tracker entry's destruction
     * (func_152339_d) is sent as one S13 by the player's next onUpdate, in
     * the next tick's entity pass */
    int destroyed[128];
    int ndestroyed;
    /* ids cached after the player's onUpdate already ran this tick (a
     * portal traveller behind it): they join the cache after its flush */
    int destroyed_late[32];
    int ndestroyed_late;
};

void combat_bind(struct serverreplay *sr, struct client_player *cp);
void combat_client_tick(struct client_player *cp);
void combat_client_vehicle_gone(struct client_player *cp, int vehicle_id);
/* NetHandlerPlayClient.handleRespawn into another dimension: the client's
 * entities go with its old WorldClient, and every tracker entry drops the
 * player (a later one re-sends its spawn once the player is back in range). */
void combat_client_world_change(struct client_player *cp);
/* The packets the client has not handled yet, into its current world (the
 * ones ahead of an S07). */
void combat_client_flush(struct client_player *cp);
/* getMouseOver's entity pass from the eye (sx, sy, sz) along the look
 * (dx, dy, dz): var2 is reach, var4 block_dist, have_mouse_over whether the
 * block trace returned anything. The chosen entity's id when it replaces
 * objectMouseOver, else 0. */
/* World.checkNoEntityCollision over the client world's copies */
int combat_client_prevents(const struct client_player *cp, const struct aabb *box);
int combat_pick_entity(const struct client_player *cp, double sx, double sy, double sz,
                       double dx, double dy, double dz, double reach, double block_dist,
                       int have_mouse_over);
void combat_server_finish(struct serverreplay *sr);
/* EntityTracker.trackEntity from spawnEntityInWorld: the entry starts at the
 * spawn position and sends its S0F to a player already in range. */
struct living;
void combat_chunk_sent(struct serverreplay *sr, int cx, int cz);
/* World.onEntityRemoved for the player (setDead by onDeathUpdate, or the
 * exit portal): EntityTracker.removeEntityFromAllTrackingPlayers takes it
 * off every entry without a packet. */
void combat_player_removed(struct serverreplay *sr);
void combat_player_spawned(struct serverreplay *sr);
/* EntityTracker.updateTrackedEntities of a world the player is not in: its
 * entries move on with no player to send to. */
void combat_server_quiet(struct serverreplay *sr);
/* The EntityCrit2FX emitters' entity (particles_live's plive_target_fn):
 * the client's living copy with the id, ctx the client player. */
struct plive_target;
int combat_fx_target(void *ctx, int id, struct plive_target *out);
void combat_track_spawn(struct serverreplay *sr, struct living *l);
/* World.onEntityRemoved of a portal traveller's old instance: its world's
 * tracker entry goes (the destination tracks the old instance anew) and the
 * id enters the player's destroyedItemsNetCache */
void combat_untrack(struct serverreplay *sr, int id);
void combat_server_attack(struct serverreplay *sr, int entity_id);
void combat_client_attack(struct client_player *cp, int entity_id);

#endif
