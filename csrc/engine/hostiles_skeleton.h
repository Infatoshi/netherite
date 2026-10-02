#ifndef NETHERITE_HOSTILES_SKELETON_H
#define NETHERITE_HOSTILES_SKELETON_H

#include "living.h"

void skeleton_construct(struct living *l, det_state *det);
void skeleton_set_type(struct living *l, int type);
void skeleton_set_combat_task(struct living *l);
void skeleton_apply_weapon(struct living *l);
void skeleton_on_spawn_with_egg(struct living *l, det_state *det);
void skeleton_on_living_update(struct living *l, det_state *det);
int skeleton_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void skeleton_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power);
void skeleton_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void skeleton_write_kind_nbt(struct living *l, struct nbtw *w);

#endif
