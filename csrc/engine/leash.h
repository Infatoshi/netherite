#ifndef NETHERITE_LEASH_H
#define NETHERITE_LEASH_H

/* Leads (lane/ride): EntityLiving's leash (interactFirst's lead half,
 * clearLeashed, setLeashedToEntity, recreateLeash, the NBT),
 * EntityCreature.updateLeashedState, ItemLead.func_150909_a (a fence ties
 * the player's leashed mobs to a knot) and EntityLeashKnot.interactFirst.
 * The knot itself is a hanging entity of the fall/hang pool (FH_KNOT). */

#include <stdint.h>

#include "nbtjson.h"

enum { LEASH_NONE = 0, LEASH_PLAYER = 1, LEASH_KNOT = 2 };

struct serverreplay;
struct server_player;
struct surv_stack;
struct living;

/* The replay whose player, living world and knot pool the leashes reach. */
void leash_bind(struct serverreplay *sr);

/* EntityLiving.interactFirst's leash half. server 1 mutates the living (and
 * drops the lead); server 0 is the client's copy: only the held stack
 * changes. 1 when it answered (interactFirst returned true). */
int leash_interact_first(struct surv_stack *held, struct living *l, int server);

/* ItemLead.func_150909_a(player, world, x, y, z): 1 when a leashed mob went
 * to the knot at the fence (made if none). */
int leash_tie_to_fence(struct server_player *p, int x, int y, int z);

/* processUseEntity's INTERACT on a knot: EntityLeashKnot.interactFirst and
 * interactWith's stack tail. 0 when the id names no knot. */
int leash_knot_interact(struct server_player *p, int entity_id);

/* EntityLiving.onUpdate's server tail: updateLeashedState (EntityCreature's
 * override for the creature kinds). */
void leash_update(struct living *l);

/* EntityLiving.writeEntityToNBT's Leash compound (after "Leashed"). */
struct nbtw;
void leash_write_nbt(const struct living *l, struct nbtw *w);
/* readEntityFromNBT: Leashed, and the Leash compound kept for recreateLeash. */
void leash_read_nbt(struct living *l, const nbt *tag);

#endif
