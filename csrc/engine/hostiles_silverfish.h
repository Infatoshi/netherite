#ifndef NETHERITE_HOSTILES_SILVERFISH_H
#define NETHERITE_HOSTILES_SILVERFISH_H

#include "living.h"

void silverfish_construct(struct living *l, det_state *det);
void silverfish_on_living_update(struct living *l, det_state *det);
void silverfish_update_entity_action_state(struct living *l, det_state *det);
void silverfish_hurt(struct living *l, int source, struct living *attacker);
void silverfish_write_extra(struct living *l, unsigned char *rec, int *p);

#endif
