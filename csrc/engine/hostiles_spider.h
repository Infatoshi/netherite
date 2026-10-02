#ifndef NETHERITE_HOSTILES_SPIDER_H
#define NETHERITE_HOSTILES_SPIDER_H

#include "living.h"

void spider_construct(struct living *l, det_state *det);
void spider_on_living_update(struct living *l, det_state *det);
void spider_update_entity_action_state(struct living *l, det_state *det);
void spider_on_update_tail(struct living *l);
void spider_set_beside_climbable_block(struct living *l, int beside);
int spider_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void spider_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void spider_write_kind_nbt(struct living *l, struct nbtw *w);
int spider_is_potion_applicable(const struct living *l, int potion_id);
struct living *spider_jockey_check(struct living *l, det_state *det);
void egg_add_pending_partner(struct living *partner);
void egg_take_pending_partner(void);
int spider_group_potion(int roll);
struct living *spider_spawn_jockey_skeleton(struct an_world *an, struct living *l, det_state *det);

/* The jockey rider's skeleton (EntitySkeleton). */
void skeleton_construct(struct living *l, det_state *det);
void skeleton_set_type(struct living *l, int type);
void skeleton_set_combat_task(struct living *l);
void skeleton_on_spawn_with_egg(struct living *l, det_state *det);
void skeleton_on_living_update(struct living *l, det_state *det);
int skeleton_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void skeleton_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power);
void skeleton_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void skeleton_write_kind_nbt(struct living *l, struct nbtw *w);

#endif
