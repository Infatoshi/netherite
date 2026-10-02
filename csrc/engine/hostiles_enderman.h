/* EntityEnderman (1.7.10): the stare that makes it aggressive, the teleports,
 * the block it carries, and the old AI (EntityCreature.updateEntityActionState)
 * it runs because EntityMob leaves isAIEnabled false for it. See
 * hostiles_enderman.c and oracle/src/entity/monster/EntityEnderman.java. */
#ifndef NETHERITE_HOSTILES_ENDERMAN_H
#define NETHERITE_HOSTILES_ENDERMAN_H

#include "living.h"
#include "nbtjson.h"

/* EntityEnderman(World): the attributes (maxHealth 40, movementSpeed 0.3,
 * attackDamage 7), setSize(0.6F, 2.9F), stepHeight 1.0F, and the health the new
 * max health implies (the constructor's setHealth runs after
 * applyEntityAttributes). */
void enderman_construct(struct living *l, det_state *det);

/* EntityEnderman.onLivingUpdate: the wet damage, the attacking speed boost,
 * the mobGriefing block take and place, the portal particles, the daylight and
 * wet teleports, the scream reset, then EntityMob.onLivingUpdate and the
 * looting pass. living.c's on_living_update hook. */
void enderman_on_living_update(struct living *l, det_state *det);

/* EntityCreature.updateEntityActionState with EntityLiving's old-AI body
 * (despawn, the random target pick, the water jump) in its else branch.
 * living.c's non-AI action-state hook. */
void enderman_update_entity_action_state(struct living *l, det_state *det);

/* EntityEnderman.attackEntityFrom over EntityMob's and EntityLivingBase's.
 * indirect is DamageSource instanceof EntityDamageSourceIndirect (an arrow, a
 * thrown item: the 64 teleport attempts), attacker_is_player is the damage
 * source carrying an EntityPlayer (sets isAggressive). Returns the
 * EntityLivingBase boolean. */
int enderman_attack_entity_from(struct living *l, struct living *attacker, int source, float amount,
                                int indirect, int attacker_is_player, det_state *det);

/* EntityEnderman.dropFewItems: 0..1(+looting) ender pearls. */
void enderman_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);

/* EntityEnderman.writeEntityToNBT's own half: "carried" and "carriedData". */
struct nbtw;
void enderman_write_kind_nbt(struct living *l, struct nbtw *w);

/* EntityEnderman's block at the tail of the hostile record, 44 bytes: the three
 * data watcher bytes, the stare timer, the teleport delay, the aggressive and
 * screaming bits, lastEntityToAttack, and the EntityCreature path. Every other
 * kind writes zeros. */
void enderman_write_extra(struct living *l, unsigned char *rec, int *p);

/* A snapshot's enderman with lastEntityToAttack set: the unsaved
 * "Attacking speed boost" modifier its last onLivingUpdate applied. */
void enderman_restore_speed_boost(struct living *l);

#endif