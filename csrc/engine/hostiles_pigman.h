#ifndef NETHERITE_HOSTILES_PIGMAN_H
#define NETHERITE_HOSTILES_PIGMAN_H

#include "living.h"
#include "nbtjson.h"

void pigman_construct(struct living *l, det_state *det);
void pigman_add_random_armor(struct living *l);
void pigman_pre_on_update(struct living *l, det_state *det);
void pigman_on_living_update(struct living *l, det_state *det);
void pigman_update_entity_action_state(struct living *l, det_state *det);
int pigman_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, det_state *det);
void pigman_drop_few_items(struct living *l, int looting);
struct nbtw;
void pigman_write_kind_nbt(struct living *l, struct nbtw *w);
void pigman_write_extra(struct living *l, unsigned char *rec, int *p);

/* A snapshot's angry pigman (field_110191_bu set): the unsaved "Attacking
 * speed boost" modifier its last onUpdate applied. */
void pigman_restore_speed_boost(struct living *l);

#endif
