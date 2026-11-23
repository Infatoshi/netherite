/* The five farm animals: the constructor's per-kind work, the per-kind tick
 * tail, the drops and the NBT keys. See animals.c. */
#ifndef NETHERITE_ANIMALS_H
#define NETHERITE_ANIMALS_H

#include "living.h"

void animal_attributes(struct living *l);
void animal_construct(struct living *l, det_state *det);
struct nbtw;
void animal_write_kind_nbt(struct living *l, struct nbtw *w);
struct nbtw;
void bat_write_kind_nbt(struct living *l, struct nbtw *w);

void animal_on_living_update(struct living *l, det_state *det);
void squid_on_living_update(struct living *l, det_state *det);
void squid_move_entity_with_heading(struct living *l, float strafe, float forward, det_state *det);
void squid_update_entity_action_state(struct living *l);
int squid_is_in_water(struct living *l);
void bat_on_base_update(struct living *l);
void bat_update_ai_tasks(struct living *l, det_state *det);
void squidbat_bat_on_living_update(struct living *l, det_state *det);
void squidbat_kind_tick(struct living *l, det_state *det);
void animal_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void animal_write_kind_nbt(struct living *l, struct nbtw *w);
void animal_eat_grass_bonus(struct living *l);
struct living *animal_create_child(struct living *l, struct living *mate, det_state *det);
float animal_get_block_path_weight(struct living *l, int x, int y, int z);

#endif
