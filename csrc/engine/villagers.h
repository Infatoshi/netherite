/* Village and villagers living system: EntityVillager, EntityIronGolem,
 * VillageCollection, Village, VillageDoorInfo, VillageSiege. */
#ifndef NETHERITE_VILLAGERS_H
#define NETHERITE_VILLAGERS_H

#include "living.h"
#include "trades.h"

#include <stdlib.h>

#define VILLAGE_DOOR_AGING_THRESHOLD 1200
#define VILLAGE_GOLEM_SPAWN_CHANCE 7000

/* VillageCollection.villagerPositionsList takes a position only while it
 * holds 64 or fewer (addVillagerPosition), so it never passes 65. The doors,
 * the aggressors, the new doors and the villages are ArrayLists: slab tables
 * (arena.h slab_table_room) that grow with them. */
#define VILLAGE_MAX_POSITIONS 128
#define VILLAGE_MAX_PLAYERS 8

struct village_door_info {
    int pos_x, pos_y, pos_z;
    int inside_dir_x, inside_dir_z;
    int last_activity_timestamp;
    int is_detached_from_village_flag;
    int door_opening_restriction_counter;
};

struct village_player_rep {
    char name[64];
    int rep;
};

struct village_agressor {
    lref agressor;
    int agression_time;
    int agressor_id;      /* the entity id a snapshot names, until resolved */
};

struct village {
    struct an_world *world;
    int32_t *doors;   /* slots in the environment's door pool (a slab table) */
    int num_doors, cap_doors;
    int center_x, center_y, center_z;
    int center_helper_x, center_helper_y, center_helper_z;
    int village_radius;
    int last_add_door_timestamp;
    int tick_counter;
    int num_villagers;
    int no_breed_ticks;
    int num_iron_golems;
    struct village_player_rep player_rep[VILLAGE_MAX_PLAYERS];
    int num_player_rep;
    struct village_agressor *agressors;   /* a slab table */
    int num_agressors, cap_agressors;
};

struct chunk_coords {
    int x, y, z;
};

struct village_collection {
    struct an_world *world;
    struct chunk_coords positions[VILLAGE_MAX_POSITIONS];
    int num_positions;
    struct village_door_info *new_doors;   /* a slab table */
    int num_new_doors, cap_new_doors;
    /* The Village objects, each in a slot it keeps (a slab table):
     * villageList (list, a slab table of slots, in its order) names the live
     * ones, and a village the list dropped stays readable for as long as a
     * villager, golem, task or siege still names it, as the Java object
     * outlives its removal; a new village takes a slot nothing names, else a
     * new one (vc_tick). */
    struct village *villages;
    int num_slots, cap_slots;
    int32_t *list;
    int num_villages, cap_list;
    int tick_counter;
};

struct village_siege {
    struct an_world *world;
    int field_75535_b;
    int field_75536_c;
    int field_75533_d;
    int field_75534_e;
    int32_t the_village;          /* index + 1 into the world's collection, 0 none */
    int field_75532_g;
    int field_75538_h;
    int field_75539_i;
};

/* VillageDoorInfo objects live in the environment's door pool (arena.h) and,
 * as in Java, outlast the village that dropped them while a task still
 * holds one: a door is named by its slot. */
struct village_door_info *door_alloc(void);
static inline struct village_door_info *door_at(int32_t i)
{
    return (struct village_door_info *)pool_at(&nw_arena()->doors, i);
}
static inline int32_t door_index(const struct village_door_info *d)
{
    return pool_index(&nw_arena()->doors, d);
}

/* a door reference (a task's front door): the slot + 1, 0 for none */
static inline int32_t door_ref(const struct village_door_info *d)
{
    return d ? door_index(d) + 1 : 0;
}
static inline struct village_door_info *door_deref(int32_t ref)
{
    return ref ? door_at(ref - 1) : NULL;
}

/* A village named by its slot + 1 in its collection (0 for none): what a
 * villager, a task and the siege keep, as they keep the Java object. */
static inline struct village *vc_village(struct village_collection *vc, int32_t ref)
{
    if (ref == 0) return NULL;
    if (vc == NULL) abort();
    return &vc->villages[ref - 1];
}
static inline int32_t vc_village_ref(const struct village_collection *vc, const struct village *v)
{
    return v ? (int32_t)(v - vc->villages) + 1 : 0;
}
/* villageList.get(i) */
static inline struct village *vc_list_at(struct village_collection *vc, int i)
{
    return &vc->villages[vc->list[i]];
}
/* the living's own village (EntityVillager.villageObj) */
static inline struct village *lv_village(const struct living *l)
{
    return l->village ? vc_village(l->an->village_collection, l->village) : NULL;
}

/* Village Door Info helpers */
int vdi_dist_sq(const struct village_door_info *d, int x, int y, int z);
int vdi_inside_dist_sq(const struct village_door_info *d, int x, int y, int z);
int vdi_inside_x(const struct village_door_info *d);
int vdi_inside_y(const struct village_door_info *d);
int vdi_inside_z(const struct village_door_info *d);
int vdi_is_inside(const struct village_door_info *d, int x, int z);

/* Village methods */
void v_init(struct village *v, struct an_world *w);
void v_tick(struct village *v, int tick_counter);
void v_add_door(struct village *v, const struct village_door_info *d);
struct village_door_info *v_find_nearest_door(struct village *v, int x, int y, int z);
struct village_door_info *v_find_nearest_door_unrestricted(struct village *v, int x, int y, int z);
struct village_door_info *v_get_door_at(struct village *v, int x, int y, int z);
int v_is_in_range(const struct village *v, int x, int y, int z);
int v_get_reputation(struct village *v, const char *player_name);
int v_set_reputation(struct village *v, const char *player_name, int delta);
void v_set_default_player_reputation(struct village *v, int rep);
int v_is_mating_season(const struct village *v);
void v_end_mating_season(struct village *v);
int v_is_annihilated(const struct village *v);
void v_add_or_renew_agressor(struct village *v, struct living *agressor);
struct living *v_find_nearest_aggressor(struct village *v, struct living *l);
struct living *v_find_low_reputation_player(struct village *v, struct living *l);

/* VillageCollection methods */
void vc_init(struct village_collection *vc, struct an_world *w);
void vc_tick(struct village_collection *vc);
void vc_add_villager_position(struct village_collection *vc, int x, int y, int z);
struct village *vc_find_nearest_village(struct village_collection *vc, int x, int y, int z, int extra_dist);
struct village_door_info *vc_get_door_at(struct village_collection *vc, int x, int y, int z);
/* VillageCollection.readFromNBT plus the runtime state a snapshot's
 * villages.nbt carries (Snapshot.villages); the aggressors keep their entity
 * ids until vc_resolve_agressors. Returns 0 on a malformed file. */
struct nbt;
int vc_load(struct village_collection *vc, const struct nbt *root);
void vc_resolve_agressors(struct village_collection *vc, struct living *(*by_id)(void *ctx, int id), void *ctx);

/* VillageSiege methods */
void vs_init(struct village_siege *vs, struct an_world *w);
void vs_tick(struct village_siege *vs);
/* VillageSiege.func_75529_b for one player at (px, py, pz) on World.rand:
 * returns 0 always in 1.7.10 (func_75527_a returns null), after its draws. */
int vs_muster(struct village_siege *vs, struct village_collection *vc, jrand *rand,
              double px, double py, double pz);

/* Villager & Golem entity logic */
void villager_construct(struct living *l, det_state *det);
void villager_on_living_update(struct living *l, det_state *det);
/* EntityVillager.getRecipes: the lazy build of the default offers. */
void villager_get_recipes(struct living *l, jrand *shuf_rand);
/* EntityVillager.useRecipe and func_110297_a_ (the trade's server half). */
void villager_player_trade(struct living *l, int recipe_index);
void villager_func_110297_a_(struct living *l, int has_stack);
void villager_update_ai_tick(struct living *l, det_state *det);
/* EntityVillager.setRevengeTarget's village half (after super's), and its
 * onDeath's (before super's); EntityIronGolem.onDeath's. The player's
 * command sender name is VILLAGE_PLAYER_NAME. */
#define VILLAGE_PLAYER_NAME "Player"
void villager_revenge_village(struct living *l, struct living *target);
void villager_death_village(struct living *l, struct living *attacker);
void iron_golem_death_village(struct living *l);
void villager_on_spawn_with_egg(struct living *l, det_state *det);
struct living *villager_create_child(struct living *l, struct living *mate, det_state *det);

void iron_golem_construct(struct living *l, det_state *det);
void iron_golem_on_living_update(struct living *l, det_state *det);
void iron_golem_update_ai_tick(struct living *l, det_state *det);
void iron_golem_set_holding_rose(struct living *l, int holding);

/* World / Entity spawning helper */
struct living *vil_spawn_living(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                                float yaw, float pitch, int growing_age, int profession, int wealth,
                                int player_created, int with_egg);

/* Door block operations */
int door_calc_inside_dir(struct world *w, int x, int y, int z, int *inside_dir_x, int *inside_dir_z);
void door_set_open(struct world *w, int x, int y, int z, int open,
                   void (*on_write)(int x, int y, int z, int meta, int flags, void *ctx), void *ctx);

#endif
