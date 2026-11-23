/* EntityCreeper and EntityAICreeperSwell: the fuse (EntityCreeper.onUpdate's
 * creeperState and timeSinceIgnited), the swell task, the blast through
 * explosion.c and the charged creeper's lightning. See hostiles_creeper.c. */
#ifndef NETHERITE_HOSTILES_CREEPER_H
#define NETHERITE_HOSTILES_CREEPER_H

#include "living.h"

/* EntityCreeper(World): EntityMob's attributes with the creeper's speed, the
 * three dataWatcher bytes, the fuse and the task list. */
void creeper_construct(struct living *l, det_state *det);

/* EntityCreeper.onUpdate's head, before EntityLivingBase's body: the fuse. */
void creeper_pre_on_update(struct living *l, det_state *det);

/* EntityMob.onLivingUpdate: no daylight burning. */
void creeper_on_living_update(struct living *l, det_state *det);

/* EntityCreeper.fall: the fall distance joins the fuse. */
void creeper_fall(struct living *l, float distance);

/* EntityCreeper.attackEntityAsMob: true, no damage. */
void creeper_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);

/* EntityLivingBase.dropFewItems: 0-2 gunpowder. */
void creeper_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);

/* EntityLocationBolt's constructor at (x, y, z) then
 * EntityCreeper.onStruckByLightning: the bolt's Det draws, the fire it places,
 * the 5 fire damage and the powered byte. The probe constructs the bolt inside
 * the call, so the whole of it is here. */
void creeper_struck_by_lightning(struct living *l, det_state *det, double x, double y, double z);

/* EntityCreeper.getMaxSafePointTries: 3 + (int)(health - 1) with a target. */
int creeper_max_safe_point_tries(struct living *l);

/* EntityCreeper.writeEntityToNBT's own keys. */
struct nbtw;
void creeper_write_kind_nbt(struct living *l, struct nbtw *w);

/* EntityCreeper.func_146077_cc: the blast and setDead. */
void creeper_explode(struct living *l, det_state *det);

/* EntityAICreeperSwell, driven from ai.c's task switch. */
int creeper_swell_should_execute(struct living *l, struct ai_task *t);
void creeper_swell_update(struct living *l, struct ai_task *t);

#endif