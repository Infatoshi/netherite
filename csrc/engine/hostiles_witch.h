#ifndef NETHERITE_HOSTILES_WITCH_H
#define NETHERITE_HOSTILES_WITCH_H

#include "living.h"

void witch_construct(struct living *l, det_state *det);
void witch_on_living_update(struct living *l, det_state *det);
void witch_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power);
void witch_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void witch_set_aggressive(struct living *l, int agg);
int witch_get_aggressive(const struct living *l);

/* A snapshot's drinking witch (data watcher 21): the unsaved "Drinking
 * speed penalty" its drink applied. */
void witch_restore_drinking(struct living *l);

#endif
