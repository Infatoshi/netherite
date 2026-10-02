#ifndef NETHERITE_HOSTILES_H
#define NETHERITE_HOSTILES_H

#include "living.h"

/* The shared hostile block, then enderman's 44-byte and pigman's 32-byte blocks;
 * the oracle's dumps recorded before the
 * enderman lane are 332 bytes shorter, which test_hostiles reads from each
 * dump's own "ent_state_layout". */
#define HOSTILE_SHARED_EXTRA_BYTES 332
#define ENDERMAN_EXTRA_BYTES 44
#define PIGMAN_EXTRA_BYTES 32
#define HOSTILE_EXTRA_BYTES (HOSTILE_SHARED_EXTRA_BYTES + ENDERMAN_EXTRA_BYTES + PIGMAN_EXTRA_BYTES)
#define HOSTILE_STATE_BYTES (AN_STATE_BYTES + HOSTILE_EXTRA_BYTES)

void zombie_construct(struct living *l, det_state *det);
void zombie_set_child(struct living *l, int child);
/* the jockey chicken it spawned, pending its tick-list insert, or NULL */
struct living *zombie_on_spawn_with_egg(struct living *l, det_state *det);
/* EntityLiving.addRandomArmor over a living (HARD's stop chance) */
void living_add_random_armor(struct living *l);
void zombie_on_living_update(struct living *l, det_state *det);
/* the held stack's attackDamage modifier after an equipment change */
void living_held_item_modifiers(struct living *l, int item);
/* the zombie and skeleton helmet in the sun (hostiles.c) */
void mob_sun_helmet(struct living *l, det_state *det);
int zombie_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void zombie_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);

/* EntityLiving.onLivingUpdate's looting pass, shared by the hostile kinds. */
void hostile_loot_update(struct living *l);

void player_construct(struct living *l, det_state *det);
void hostile_loot_update(struct living *l);

struct living *hostile_spawn(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                             float yaw, float pitch, double mx, double my, double mz,
                             int child, int villager, int wither, int charged, float diff_factor);

void hostile_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec, int tick);

#endif
