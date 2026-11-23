#ifndef NETHERITE_RIDING_H
#define NETHERITE_RIDING_H

/* The player riding a saddled pig (lane/ride): EntityAIControlledByPlayer,
 * ItemCarrotOnAStick, EntityPlayer's mount, updateRidden and dismount on
 * the server, and the client's rider update. Mob-on-mob riding (the jockeys)
 * stays in living.c. */

#include "det.h"
#include "aabb.h"

struct living;
struct ai_task;
struct server_player;
struct client_player;
struct client_out;
struct serverreplay;
struct surv_stack;
struct world;
struct c03;

/* EntityAIControlledByPlayer.shouldExecute (and continueExecuting). */
int ride_ai_should_execute(struct living *l, struct ai_task *t);
/* EntityAIControlledByPlayer.updateTask. */
void ride_ai_update(struct living *l, struct ai_task *t, det_state *det);

/* ItemCarrotOnAStick.onItemRightClick on the server (tryUseItem's stack
 * swap included). Returns 1 when the held item is the carrot on a stick. */
int ride_carrot_right_click(struct server_player *p);

/* ItemSaddle.itemInteractionForEntity on a pig: 1 when the entity is a pig
 * (the stack shrinks only when the saddle went on). mutate 0 is the client's
 * copy, which leaves the server pig alone. */
int ride_saddle_interact(struct surv_stack *held, struct living *pig, int mutate);

/* EntityPig.interact's mount branch (server): the saddle is on and the pig
 * is free or ridden by this player; EntityPlayerMP.mountEntity(pig). */
int ride_pig_interact(struct server_player *p, struct living *pig);

/* EntityPlayerMP.mountEntity(null): EntityPlayer.dismountEntity's free spot,
 * the S1B and setPlayerLocation. */
void ride_server_dismount(struct server_player *p);

/* World.updateEntity(player) while the player rides: the updateRidden path
 * (the sneak dismount, EntityPlayerMP.onUpdate, updateRiderPosition, the
 * mounted-movement stat). */
void ride_server_update_entity(struct server_player *p);

/* Entity.updateRiderPosition of the player's vehicle on the server. */
void ride_server_rider_position(struct server_player *p);

/* NetHandlerPlayServer.processInput: EntityPlayerMP.setEntityActionState. */
void ride_server_process_input(struct server_player *p, float strafe, float forward, int jump, int sneak);

/* EntityPlayer.onLivingUpdate's collideWithPlayer box: the player's box
 * and the vehicle's joined and grown by (1, 0, 1) while riding, else the
 * player's grown by (1, 0.5, 1). */
struct aabb ride_server_pickup_box(const struct server_player *p);

/* Entity.fall's rider half for a player rider: EntityPlayer.fall. */
void ride_player_fall(struct living *vehicle, float distance);

/* EntityPig.fall's flyPig for a player rider. */
void ride_pig_fly(struct living *pig);

/* The client: S1B for the player itself (vehicle id, 0 none). */
void ride_client_s1b(struct client_player *p, int vehicle_id);
/* 1 while the client player rides its vehicle copy (the world pass skips
 * the player; the vehicle's updateEntity runs it after the entities). */
int ride_client_riding(struct client_player *p);
/* Entity.updateRiderPosition on the client: the vehicle copy's position
 * plus its mounted offset and the player's getYOffset (1.62F - 0.5F). */
void ride_client_rider_position(struct client_player *p);
/* The client's EntityPlayer.updateRidden, after the entities tick: the
 * player's own update, the C05 and C0C, updateRiderPosition. */
void ride_client_update_ridden(struct client_player *p, struct client_out *out);
/* The entity pick's var15 == ridingEntity test. */
int ride_client_vehicle_id(const struct client_player *p);

#endif
