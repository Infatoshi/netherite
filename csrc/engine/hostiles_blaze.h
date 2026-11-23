#ifndef NETHERITE_HOSTILES_BLAZE_H
#define NETHERITE_HOSTILES_BLAZE_H

#include "living.h"

void blaze_construct(struct living *l, det_state *det);
void blaze_on_living_update(struct living *l, det_state *det);
void blaze_update_entity_action_state(struct living *l, det_state *det);
void blaze_fall(struct living *l, float dist);
void blaze_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void blaze_set_on_fire(struct living *l, int on);
int blaze_is_on_fire(const struct living *l);

#endif
