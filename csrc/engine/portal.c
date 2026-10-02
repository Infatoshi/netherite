#include <stdio.h>
/* 1.7.10 dimension travel: portals, transfer and teleporters. */
#include "portal.h"
#include "jmath.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "blocks.h"


const int DIR_OFFSET_X[4] = {0, -1, 0, 1};
const int DIR_OFFSET_Z[4] = {1, 0, -1, 0};
const int DIR_ROTATE_LEFT[4] = {3, 0, 1, 2};
const int DIR_ROTATE_RIGHT[4] = {1, 2, 3, 0};
const int DIR_ROTATE_OPPOSITE[4] = {2, 3, 0, 1};

int direction_get_movement(double dx, double dz)
{
    float fx = (float)dx;
    float fz = (float)dz;
    float ax = fx < 0.0f ? -fx : fx;
    float az = fz < 0.0f ? -fz : fz;
    return ax > az ? (dx > 0.0 ? 1 : 3) : (dz > 0.0 ? 2 : 0);
}

static inline int clamp_int(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

static inline int is_air_block(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z);
    return BLOCKS[id & 4095].material == 0;
}

static inline int is_solid_block(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z);
    return MATERIALS[BLOCKS[id & 4095].material].is_solid;
}

/* Cache implementation */

static inline size_t hash_key64(int64_t k)
{
    uint64_t x = (uint64_t)k;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (size_t)x;
}

void portal_cache_init(struct portal_cache *c)
{
    c->cap = 64;
    c->count = 0;
    c->entries = calloc(c->cap, sizeof(*c->entries));
    c->key_cap = 64;
    c->key_count = 0;
    c->keys = malloc(c->key_cap * sizeof(*c->keys));
}

void portal_cache_free(struct portal_cache *c)
{
    free(c->entries);
    c->entries = NULL;
    free(c->keys);
    c->keys = NULL;
    c->count = c->cap = 0;
    c->key_count = c->key_cap = 0;
}

struct portal_cache_entry *portal_cache_get(struct portal_cache *c, int64_t key)
{
    if (c->count == 0) return NULL;
    size_t mask = c->cap - 1;
    size_t idx = hash_key64(key) & mask;
    for (size_t i = 0; i < c->cap; ++i)
    {
        size_t slot = (idx + i) & mask;
        if (!c->entries[slot].occupied) return NULL;
        if (c->entries[slot].key == key) return &c->entries[slot];
    }
    return NULL;
}

static void portal_cache_grow(struct portal_cache *c)
{
    size_t old_cap = c->cap;
    struct portal_cache_entry *old_entries = c->entries;
    c->cap = old_cap * 2;
    c->entries = calloc(c->cap, sizeof(*c->entries));
    c->count = 0;
    for (size_t k = 0; k < c->key_count; ++k)
    {
        int64_t key = c->keys[k];
        size_t mask = old_cap - 1;
        size_t idx = hash_key64(key) & mask;
        while (old_entries[idx].occupied && old_entries[idx].key != key)
        {
            idx = (idx + 1) & mask;
        }
        if (old_entries[idx].occupied && old_entries[idx].key == key)
        {
            size_t new_mask = c->cap - 1;
            size_t new_idx = hash_key64(key) & new_mask;
            while (c->entries[new_idx].occupied)
            {
                new_idx = (new_idx + 1) & new_mask;
            }
            c->entries[new_idx] = old_entries[idx];
            c->count++;
        }
    }
    free(old_entries);
}

void portal_cache_put(struct portal_cache *c, int64_t key, int x, int y, int z, int64_t last_time)
{
    struct portal_cache_entry *existing = portal_cache_get(c, key);
    if (existing)
    {
        existing->x = x;
        existing->y = y;
        existing->z = z;
        existing->last_update_time = last_time;
        return;
    }

    if (c->count * 2 >= c->cap)
    {
        portal_cache_grow(c);
    }

    size_t mask = c->cap - 1;
    size_t idx = hash_key64(key) & mask;
    while (c->entries[idx].occupied)
    {
        idx = (idx + 1) & mask;
    }
    c->entries[idx].key = key;
    c->entries[idx].x = x;
    c->entries[idx].y = y;
    c->entries[idx].z = z;
    c->entries[idx].last_update_time = last_time;
    c->entries[idx].occupied = 1;
    c->count++;

    if (c->key_count >= c->key_cap)
    {
        c->key_cap = c->key_cap ? c->key_cap * 2 : 64;
        c->keys = realloc(c->keys, c->key_cap * sizeof(*c->keys));
    }
    c->keys[c->key_count++] = key;
}

void portal_cache_remove(struct portal_cache *c, int64_t key)
{
    if (c->count == 0) return;
    size_t mask = c->cap - 1;
    size_t idx = hash_key64(key) & mask;
    size_t slot = mask + 1;
    for (size_t i = 0; i < c->cap; ++i)
    {
        size_t s = (idx + i) & mask;
        if (!c->entries[s].occupied) return;
        if (c->entries[s].key == key) { slot = s; break; }
    }
    if (slot > mask) return;

    size_t curr = slot;
    size_t next = (curr + 1) & mask;
    while (c->entries[next].occupied)
    {
        size_t home = hash_key64(c->entries[next].key) & mask;
        bool can_move = false;
        if (curr < next)
        {
            if (home <= curr || home > next) can_move = true;
        }
        else
        {
            if (home <= curr && home > next) can_move = true;
        }
        if (can_move)
        {
            c->entries[curr] = c->entries[next];
            curr = next;
        }
        next = (next + 1) & mask;
    }
    c->entries[curr].occupied = 0;
    c->count--;
}

void portal_cache_remove_stale(struct portal_cache *c, int64_t world_time)
{
    if (world_time % 100L != 0L) return;
    int64_t threshold = world_time - 600L;

    size_t i = 0;
    while (i < c->key_count)
    {
        int64_t k = c->keys[i];
        struct portal_cache_entry *e = portal_cache_get(c, k);
        if (e == NULL || e->last_update_time < threshold)
        {
            portal_cache_remove(c, k);
            for (size_t j = i; j + 1 < c->key_count; ++j)
            {
                c->keys[j] = c->keys[j + 1];
            }
            c->key_count--;
        }
        else
        {
            ++i;
        }
    }
}

/* Teleporter */

void teleporter_init(struct teleporter *tp, struct world *w)
{
    tp->world = w;
    tp->rng_slot = w->dim == -1 ? 1 : w->dim == 1 ? 2 : 0;
    portal_cache_init(&tp->cache);
    tp->world_time = 0;
}

void teleporter_free(struct teleporter *tp)
{
    portal_cache_free(&tp->cache);
}

void teleporter_remove_stale_locations(struct teleporter *tp, int64_t world_time)
{
    tp->world_time = world_time;
    portal_cache_remove_stale(&tp->cache, world_time);
}

/* World.getActualHeight: 128 where the provider has no sky (the Nether and
 * the End, as world.c's chunks record it), 256 elsewhere. The portal scans
 * start at its top and makePortal clamps 10 below it. */
static int actual_height(const struct world *w)
{
    return w->dim != 0 ? 128 : 256;
}

int teleporter_place_in_existing_portal(struct teleporter *tp, struct entity *e, int teleport_dir,
                                       double orig_x, double orig_y, double orig_z, float orig_yaw, float orig_pitch,
                                       float *out_yaw, float *out_pitch)
{
    int radius = nw_env->cfg.portal_search_radius_override > 0 ? nw_env->cfg.portal_search_radius_override : 128;
    double best_dist_sq = -1.0;
    int best_x = 0;
    int best_y = 0;
    int best_z = 0;
    int start_x = mh_floor(e->pos_x);
    int start_z = mh_floor(e->pos_z);
    int64_t key = (int64_t)(((uint64_t)(uint32_t)start_x) | (((uint64_t)(uint32_t)start_z) << 32));
    int is_new_entry = 1;

    struct portal_cache_entry *cached = portal_cache_get(&tp->cache, key);
    if (cached != NULL)
    {
        best_dist_sq = 0.0;
        best_x = cached->x;
        best_y = cached->y;
        best_z = cached->z;
        cached->last_update_time = tp->world_time;
        is_new_entry = 0;
    }
    else
    {
        for (int x = start_x - radius; x <= start_x + radius; ++x)
        {
            double dx = (double)x + 0.5 - e->pos_x;
            for (int z = start_z - radius; z <= start_z + radius; ++z)
            {
                double dz = (double)z + 0.5 - e->pos_z;
                /* each portal stack in the column, top down, taken at its
                 * bottom block */
                for (int y = actual_height(tp->world) - 1; (y = world_column_find_down(tp->world, x, z, y, 90)) >= 0; --y)
                {
                    while (world_get_block(tp->world, x, y - 1, z) == 90)
                    {
                        --y;
                    }
                    double dy = (double)y + 0.5 - e->pos_y;
                    double dist_sq = dx * dx + dy * dy + dz * dz;
                    if (best_dist_sq < 0.0 || dist_sq < best_dist_sq)
                    {
                        best_dist_sq = dist_sq;
                        best_x = x;
                        best_y = y;
                        best_z = z;
                    }
                }
            }
        }
    }

    if (best_dist_sq >= 0.0)
    {
        if (is_new_entry)
        {
            portal_cache_put(&tp->cache, key, best_x, best_y, best_z, tp->world_time);
        }

        double dest_x = (double)best_x + 0.5;
        double dest_y = (double)best_y + 0.5;
        double dest_z = (double)best_z + 0.5;
        int orientation = -1;

        if (world_get_block(tp->world, best_x - 1, best_y, best_z) == 90) orientation = 2;
        if (world_get_block(tp->world, best_x + 1, best_y, best_z) == 90) orientation = 0;
        if (world_get_block(tp->world, best_x, best_y, best_z - 1) == 90) orientation = 3;
        if (world_get_block(tp->world, best_x, best_y, best_z + 1) == 90) orientation = 1;

        int entity_teleport_dir = teleport_dir;

        if (orientation > -1)
        {
            int rot_left = DIR_ROTATE_LEFT[orientation];
            int off_x = DIR_OFFSET_X[orientation];
            int off_z = DIR_OFFSET_Z[orientation];
            int off_left_x = DIR_OFFSET_X[rot_left];
            int off_left_z = DIR_OFFSET_Z[rot_left];

            int blk1_blocked = !is_air_block(tp->world, best_x + off_x + off_left_x, best_y, best_z + off_z + off_left_z) ||
                               !is_air_block(tp->world, best_x + off_x + off_left_x, best_y + 1, best_z + off_z + off_left_z);
            int blk2_blocked = !is_air_block(tp->world, best_x + off_x, best_y, best_z + off_z) ||
                               !is_air_block(tp->world, best_x + off_x, best_y + 1, best_z + off_z);

                               if (blk1_blocked && blk2_blocked)
            {
                orientation = DIR_ROTATE_OPPOSITE[orientation];
                rot_left = DIR_ROTATE_OPPOSITE[rot_left];
                off_x = DIR_OFFSET_X[orientation];
                off_z = DIR_OFFSET_Z[orientation];
                off_left_x = DIR_OFFSET_X[rot_left];
                off_left_z = DIR_OFFSET_Z[rot_left];

                int back_x = best_x - off_left_x;
                dest_x -= (double)off_left_x;
                int back_z = best_z - off_left_z;
                dest_z -= (double)off_left_z;

                blk1_blocked = !is_air_block(tp->world, back_x + off_x + off_left_x, best_y, back_z + off_z + off_left_z) ||
                               !is_air_block(tp->world, back_x + off_x + off_left_x, best_y + 1, back_z + off_z + off_left_z);
                blk2_blocked = !is_air_block(tp->world, back_x + off_x, best_y, back_z + off_z) ||
                               !is_air_block(tp->world, back_x + off_x, best_y + 1, back_z + off_z);
            }

            float offset_factor_side = 0.5f;
            float offset_factor_fwd = 0.5f;

            if (!blk1_blocked && blk2_blocked)
            {
                offset_factor_side = 1.0f;
            }
            else if (blk1_blocked && !blk2_blocked)
            {
                offset_factor_side = 0.0f;
            }
            else if (blk1_blocked && blk2_blocked)
            {
                offset_factor_fwd = 0.0f;
            }

            dest_x += (double)((float)off_left_x * offset_factor_side + offset_factor_fwd * (float)off_x);
            dest_z += (double)((float)off_left_z * offset_factor_side + offset_factor_fwd * (float)off_z);

            float factor_mx = 0.0f;
            float factor_mz = 0.0f;
            float factor_cross_x = 0.0f;
            float factor_cross_z = 0.0f;

            if (orientation == entity_teleport_dir)
            {
                factor_mx = 1.0f;
                factor_mz = 1.0f;
            }
            else if (orientation == DIR_ROTATE_OPPOSITE[entity_teleport_dir])
            {
                factor_mx = -1.0f;
                factor_mz = -1.0f;
            }
            else if (orientation == DIR_ROTATE_RIGHT[entity_teleport_dir])
            {
                factor_cross_x = 1.0f;
                factor_cross_z = -1.0f;
            }
            else
            {
                factor_cross_x = -1.0f;
                factor_cross_z = 1.0f;
            }

            double old_mx = e->motion_x;
            double old_mz = e->motion_z;
            e->motion_x = old_mx * (double)factor_mx + old_mz * (double)factor_cross_z;
            e->motion_z = old_mx * (double)factor_cross_x + old_mz * (double)factor_mz;
            if (out_yaw) *out_yaw = orig_yaw - (float)(entity_teleport_dir * 90) + (float)(orientation * 90);
        }
        else
        {
            e->motion_x = e->motion_y = e->motion_z = 0.0;
            if (out_yaw) *out_yaw = orig_yaw;
        }

        e->pos_x = dest_x;
        e->pos_y = dest_y;
        e->pos_z = dest_z;
        if (out_pitch) *out_pitch = orig_pitch;

        return 1;
    }
    return 0;
}

int teleporter_make_portal(struct teleporter *tp, struct entity *e)
{
    int search_rad = 16;
    double best_dist_sq = -1.0;
    int ent_x = mh_floor(e->pos_x);
    int ent_y = mh_floor(e->pos_y);
    int ent_z = mh_floor(e->pos_z);
    int best_x = ent_x;
    int best_y = ent_y;
    int best_z = ent_z;
    int best_rot = 0;

    int rand_dir = jr_int_n(&TP_RAND(tp), 4);
    if (nw_env->cfg.portal_site_draw_order_override)
    {
        rand_dir = (rand_dir + 1) % 4;
    }


    /* Pass 1: 3x4x5 search */
    for (int x = ent_x - search_rad; x <= ent_x + search_rad; ++x)
    {
        double dx = (double)x + 0.5 - e->pos_x;
        for (int z = ent_z - search_rad; z <= ent_z + search_rad; ++z)
        {
            double dz = (double)z + 0.5 - e->pos_z;

            for (int y = actual_height(tp->world) - 1; y >= 0; --y)
            {
                if (is_air_block(tp->world, x, y, z))
                {
                    while (y > 0 && is_air_block(tp->world, x, y - 1, z))
                    {
                        --y;
                    }

                    for (int dir_idx = rand_dir; dir_idx < rand_dir + 4; ++dir_idx)
                    {
                        int step_x = dir_idx % 2;
                        int step_z = 1 - step_x;
                        if (dir_idx % 4 >= 2)
                        {
                            step_x = -step_x;
                            step_z = -step_z;
                        }

                        for (int side = 0; side < 3; ++side)
                        {
                            for (int len = 0; len < 4; ++len)
                            {
                                for (int ht = -1; ht < 4; ++ht)
                                {
                                    int bx = x + (len - 1) * step_x + side * step_z;
                                    int by = y + ht;
                                    int bz = z + (len - 1) * step_z - side * step_x;

                                    if ((ht < 0 && !is_solid_block(tp->world, bx, by, bz)) ||
                                        (ht >= 0 && !is_air_block(tp->world, bx, by, bz)))
                                    {
                                        goto next_y_pass1;
                                    }
                                }
                            }
                        }

                        double dy = (double)y + 0.5 - e->pos_y;
                        double dist_sq = dx * dx + dy * dy + dz * dz;
                        if (best_dist_sq < 0.0 || dist_sq < best_dist_sq)
                        {
                            best_dist_sq = dist_sq;
                            best_x = x;
                            best_y = y;
                            best_z = z;
                            best_rot = dir_idx % 4;
                        }
                    }
next_y_pass1: ;
                }
            }
        }
    }

    /* Pass 2: 1x4x5 search */
    if (best_dist_sq < 0.0)
    {
        for (int x = ent_x - search_rad; x <= ent_x + search_rad; ++x)
        {
            double dx = (double)x + 0.5 - e->pos_x;
            for (int z = ent_z - search_rad; z <= ent_z + search_rad; ++z)
            {
                double dz = (double)z + 0.5 - e->pos_z;

                for (int y = actual_height(tp->world) - 1; y >= 0; --y)
                {
                    if (is_air_block(tp->world, x, y, z))
                    {
                        while (y > 0 && is_air_block(tp->world, x, y - 1, z))
                        {
                            --y;
                        }

                        for (int dir_idx = rand_dir; dir_idx < rand_dir + 2; ++dir_idx)
                        {
                            int step_x = dir_idx % 2;
                            int step_z = 1 - step_x;

                            for (int len = 0; len < 4; ++len)
                            {
                                for (int ht = -1; ht < 4; ++ht)
                                {
                                    int bx = x + (len - 1) * step_x;
                                    int by = y + ht;
                                    int bz = z + (len - 1) * step_z;

                                    if ((ht < 0 && !is_solid_block(tp->world, bx, by, bz)) ||
                                        (ht >= 0 && !is_air_block(tp->world, bx, by, bz)))
                                    {
                                        goto next_y_pass2;
                                    }
                                }
                            }

                            double dy = (double)y + 0.5 - e->pos_y;
                            double dist_sq = dx * dx + dy * dy + dz * dz;
                            if (best_dist_sq < 0.0 || dist_sq < best_dist_sq)
                            {
                                best_dist_sq = dist_sq;
                                best_x = x;
                                best_y = y;
                                best_z = z;
                                best_rot = dir_idx % 2;
                            }
                        }
next_y_pass2: ;
                    }
                }
            }
        }
    }

    int px = best_x;
    int py = best_y;
    int pz = best_z;
    int dir_x = best_rot % 2;
    int dir_z = 1 - dir_x;

    if (best_rot % 4 >= 2)
    {
        dir_x = -dir_x;
        dir_z = -dir_z;
    }

    if (best_dist_sq < 0.0)
    {
        if (best_y < 70) best_y = 70;
        if (best_y > actual_height(tp->world) - 10) best_y = actual_height(tp->world) - 10;
        py = best_y;

        for (int side = -1; side <= 1; ++side)
        {
            for (int len = 1; len < 3; ++len)
            {
                for (int ht = -1; ht < 3; ++ht)
                {
                    int bx = px + (len - 1) * dir_x + side * dir_z;
                    int by = py + ht;
                    int bz = pz + (len - 1) * dir_z - side * dir_x;
                    int is_floor = ht < 0;
                    world_set_block(tp->world, bx, by, bz, is_floor ? 49 : 0, 0, 3);
                }
            }
        }
    }

    for (int pass = 0; pass < 4; ++pass)
    {
        for (int len = 0; len < 4; ++len)
        {
            for (int ht = -1; ht < 4; ++ht)
            {
                int bx = px + (len - 1) * dir_x;
                int by = py + ht;
                int bz = pz + (len - 1) * dir_z;
                int is_border = (len == 0 || len == 3 || ht == -1 || ht == 3);
                world_set_block(tp->world, bx, by, bz, is_border ? 49 : 90, 0, 2);
            }
        }

        for (int len = 0; len < 4; ++len)
        {
            for (int ht = -1; ht < 4; ++ht)
            {
                int bx = px + (len - 1) * dir_x;
                int by = py + ht;
                int bz = pz + (len - 1) * dir_z;
                world_notify_neighbors(tp->world, bx, by, bz, world_get_block(tp->world, bx, by, bz));
            }
        }
    }

    return 1;
}

int teleporter_place_in_portal(struct teleporter *tp, struct entity *e, int teleport_dir,
                              double orig_x, double orig_y, double orig_z, float orig_yaw, float orig_pitch,
                              float *out_yaw, float *out_pitch)
{
    if (tp->world->dim != 1)
    {
        if (!teleporter_place_in_existing_portal(tp, e, teleport_dir, orig_x, orig_y, orig_z, orig_yaw, orig_pitch, out_yaw, out_pitch))
        {
            teleporter_make_portal(tp, e);
            teleporter_place_in_existing_portal(tp, e, teleport_dir, orig_x, orig_y, orig_z, orig_yaw, orig_pitch, out_yaw, out_pitch);
        }
    }
    else
    {
        int bx = mh_floor(e->pos_x);
        int by = mh_floor(e->pos_y) - 1;
        int bz = mh_floor(e->pos_z);
        int step_x = 1;
        int step_z = 0;

        for (int side = -2; side <= 2; ++side)
        {
            for (int len = -2; len <= 2; ++len)
            {
                for (int ht = -1; ht < 3; ++ht)
                {
                    int x = bx + len * step_x + side * step_z;
                    int y = by + ht;
                    int z = bz + len * step_z - side * step_x;
                    int is_floor = ht < 0;
                    world_set_block(tp->world, x, y, z, is_floor ? 49 : 0, 0, 3);
                }
            }
        }

        e->pos_x = (double)bx;
        e->pos_y = (double)by;
        e->pos_z = (double)bz;
        if (out_yaw) *out_yaw = 90.0f;
        if (out_pitch) *out_pitch = 0.0f;
        e->motion_x = e->motion_y = e->motion_z = 0.0;
    }
    return 1;
}

/* Entity state and ticking */

void portal_entity_init(struct portal_entity_state *st, int dimension, int max_time)
{
    st->time_until_portal = 0;
    st->portal_counter = 0;
    st->in_portal = 0;
    st->teleport_direction = 0;
    st->max_in_portal_time = max_time;
    st->portal_cooldown = 10;
    st->dimension = dimension;
}

void portal_entity_set_in_portal(struct entity *e, struct portal_entity_state *st)
{
    if (st->time_until_portal > 0)
    {
        st->time_until_portal = st->portal_cooldown;
    }
    else
    {
        double dx = e->prev_pos_x - e->pos_x;
        double dz = e->prev_pos_z - e->pos_z;
        if (!st->in_portal)
        {
            st->teleport_direction = direction_get_movement(dx, dz);
        }
        st->in_portal = 1;
    }
}

int portal_entity_tick(struct entity *e, struct portal_entity_state *st, int allow_nether, int *out_target_dim)
{
    int triggered = 0;
    int max_time = st->max_in_portal_time;

    if (st->in_portal)
    {
        if (allow_nether)
        {
            if (st->portal_counter++ >= max_time)
            {
                st->portal_counter = max_time;
                st->time_until_portal = st->portal_cooldown;
                int target = (st->dimension == -1) ? 0 : -1;
                if (out_target_dim) *out_target_dim = target;
                st->in_portal = 0;
                return 1;
            }
        }
    }
    else
    {
        if (st->portal_counter > 0) st->portal_counter -= 4;
        if (st->portal_counter < 0) st->portal_counter = 0;
    }

    if (st->time_until_portal > 0)
    {
        st->time_until_portal--;
    }

    return triggered;
}

void portal_transfer_entity(struct entity *e, struct portal_entity_state *st,
                            int from_dim, int to_dim,
                            struct world *from_w, struct world *to_w,
                            struct teleporter *to_tp,
                            float *yaw, float *pitch, int alive)
{
    double orig_x = e->pos_x;
    double orig_y = e->pos_y;
    double orig_z = e->pos_z;
    float orig_yaw = *yaw;
    float orig_pitch = *pitch;

    double scale = 8.0;
    double x = e->pos_x;
    double y = e->pos_y;
    double z = e->pos_z;

    if (to_dim == -1)
    {
        x /= scale;
        z /= scale;
    }
    else if (to_dim == 0)
    {
        x *= scale;
        z *= scale;
    }
    else if (to_dim == 1)
    {
        x = 100.0;
        y = 50.0;
        z = 0.0;
        *yaw = 90.0f;
        *pitch = 0.0f;
    }

    /* transferEntityToWorld's placing half (the clamp, placeInPortal) runs
     * only for an entity alive: a dead player stays at the scaled position */
    if (to_dim != 1 && alive)
    {
        x = (double)clamp_int((int)x, -29999872, 29999872);
        z = (double)clamp_int((int)z, -29999872, 29999872);
    }

    e->pos_x = x;
    e->pos_y = y;
    e->pos_z = z;

    if (alive)
        teleporter_place_in_portal(to_tp, e, st->teleport_direction,
                                  orig_x, orig_y, orig_z, orig_yaw, orig_pitch,
                                  yaw, pitch);

    if (yaw) *yaw = fmodf(*yaw, 360.0f);
    if (pitch) *pitch = fmodf(*pitch, 360.0f);
    st->dimension = to_dim;
}

void portal_respawn_end_exit(struct entity *e, struct portal_entity_state *st,
                             struct world *ow_w, int spawn_x, int spawn_y, int spawn_z,
                             float *yaw, float *pitch)
{
    e->pos_x = (double)spawn_x + 0.5;
    e->pos_y = (double)spawn_y;
    e->pos_z = (double)spawn_z + 0.5;
    *yaw = 0.0f;
    *pitch = 0.0f;
    e->motion_x = e->motion_y = e->motion_z = 0.0;
    st->dimension = 0;
    st->portal_counter = 0;
    st->time_until_portal = 0;
    st->in_portal = 0;
}
