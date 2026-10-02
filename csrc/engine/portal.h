/* 1.7.10 dimension travel: portals, transfer and teleporters. */
#ifndef NETHERITE_PORTAL_H
#define NETHERITE_PORTAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "entity.h"
#include "world.h"
#include "jrand.h"

/* The 4 horizontal directions in Minecraft 1.7.10 (Direction.java):
 * 0: South (+Z), 1: West (-X), 2: North (-Z), 3: East (+X) */
extern const int DIR_OFFSET_X[4];
extern const int DIR_OFFSET_Z[4];
extern const int DIR_ROTATE_LEFT[4];
extern const int DIR_ROTATE_RIGHT[4];
extern const int DIR_ROTATE_OPPOSITE[4];

int direction_get_movement(double dx, double dz);

/* Cache entry for Teleporter.destinationCoordinateCache */
struct portal_cache_entry {
    int64_t key;
    int x, y, z;
    int64_t last_update_time;
    int occupied;
};

struct portal_cache {
    struct portal_cache_entry *entries;
    size_t cap;
    size_t count;
    int64_t *keys;
    size_t key_count;
    size_t key_cap;
};

void portal_cache_init(struct portal_cache *c);
void portal_cache_free(struct portal_cache *c);
struct portal_cache_entry *portal_cache_get(struct portal_cache *c, int64_t key);
void portal_cache_put(struct portal_cache *c, int64_t key, int x, int y, int z, int64_t last_time);
void portal_cache_remove(struct portal_cache *c, int64_t key);
void portal_cache_remove_stale(struct portal_cache *c, int64_t world_time);

/* Teleporter struct (world/Teleporter.java) */
/* the teleporter's Random, in the environment's random-stream table (the
 * file that uses it includes env.h) */
#define TP_RAND(tp) (nw_env->rng.teleport[(tp)->rng_slot])

struct teleporter {
    struct world *world;
    int rng_slot;             /* the world's sr_dim_index (teleporter_init) */
    struct portal_cache cache;
    int64_t world_time;
};

/* tp for w, its Random the table's slot for w's dimension (w->dim set) */
void teleporter_init(struct teleporter *tp, struct world *w);
void teleporter_free(struct teleporter *tp);

int teleporter_place_in_existing_portal(struct teleporter *tp, struct entity *e, int teleport_dir,
                                       double orig_x, double orig_y, double orig_z, float orig_yaw, float orig_pitch,
                                       float *out_yaw, float *out_pitch);

int teleporter_make_portal(struct teleporter *tp, struct entity *e);

int teleporter_place_in_portal(struct teleporter *tp, struct entity *e, int teleport_dir,
                              double orig_x, double orig_y, double orig_z, float orig_yaw, float orig_pitch,
                              float *out_yaw, float *out_pitch);

void teleporter_remove_stale_locations(struct teleporter *tp, int64_t world_time);

/* Portal entity state and ticking */
struct portal_entity_state {
    int time_until_portal;
    int portal_counter;
    int in_portal;
    int teleport_direction;
    int max_in_portal_time;
    int portal_cooldown;
    int dimension;
};

void portal_entity_init(struct portal_entity_state *st, int dimension, int max_time);
void portal_entity_set_in_portal(struct entity *e, struct portal_entity_state *st);
int portal_entity_tick(struct entity *e, struct portal_entity_state *st, int allow_nether, int *out_target_dim);

void portal_transfer_entity(struct entity *e, struct portal_entity_state *st,
                            int from_dim, int to_dim,
                            struct world *from_w, struct world *to_w,
                            struct teleporter *to_tp,
                            float *yaw, float *pitch, int alive);

void portal_respawn_end_exit(struct entity *e, struct portal_entity_state *st,
                             struct world *ow_w, int spawn_x, int spawn_y, int spawn_z,
                             float *yaw, float *pitch);

/* The negative tests' hooks are nw_env->cfg.portal_search_radius_override
 * and portal_site_draw_order_override (env.h). */

#endif
