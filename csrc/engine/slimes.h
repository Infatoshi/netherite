/* EntitySlime and EntityMagmaCube (1.7.10), plus the probe's own player: the
 * slime's size, jump delay and squish state, the jump toward the nearest player,
 * the split on death, the collision attack on the player, and the player half
 * (EntityPlayer's tick, its damage pipeline and its food stats) the attack needs.
 * See slimes.c. */
#ifndef NETHERITE_SLIMES_H
#define NETHERITE_SLIMES_H

#include "living.h"

/* EntitySlime(World): the natural size and jump delay, then setSlimeSize. */
void slime_construct(struct living *l, det_state *det);

/* EntitySlime.setSlimeSize: the data watcher, setSize, setPosition, max health
 * = size*size, setHealth, experienceValue. */
void slime_set_size(struct living *l, int size);

/* The EntitySlime halves living.c calls. */
void slime_pre_on_update(struct living *l, det_state *det);
void slime_post_on_update(struct living *l, det_state *det);
void slime_action_state(struct living *l, det_state *det);
void slime_jump_override(struct living *l);              /* EntityMagmaCube.jump */
int slime_get_jump_delay(struct living *l, det_state *det);
void slime_alter_squish(struct living *l);
void slime_set_dead(struct living *l, det_state *det);   /* the split */
void slime_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void slime_on_collide_with_player(struct living *l, struct living *player, det_state *det);
void slime_write_kind_nbt(struct living *l, struct nbt *tag);
int slime_can_despawn(struct living *l);

/* The probe player. */
void player_spawn_setup(struct living *l);
void player_on_living_update(struct living *l, det_state *det);
void player_after_update(struct living *l, det_state *det);
void player_damage_entity(struct living *l, int source, float amount, det_state *det);
int player_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, int scaled, det_state *det);
void player_add_movement_stat(struct living *l, double dx, double dy, double dz);
void player_add_exhaustion(struct living *l, float v);

/* The spawn path: the entity list insert the probe and the native replay share. */
void an_add_living(struct an_world *an, struct living *l, int spawn_index);
struct living *an_spawn_slime(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                              float yaw, float pitch, int size, int with_egg);
struct living *an_spawn_player(struct an_world *an, int spawn_index, double x, double y, double z,
                               float yaw, float pitch);

/* The per-tick record. */
#define SLIME_REC_BYTES 112
#define SLIME_PLAYER_BYTES 144
void slime_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec);
void slime_write_player(struct an_world *an, unsigned char *rec);

/* The world queries the slime tick needs. */
struct living *an_closest_player(struct an_world *an, double x, double y, double z, double range);
struct living *an_closest_vulnerable_player(struct an_world *an, double x, double y, double z, double range);

#endif