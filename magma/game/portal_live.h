#ifndef MAGMA_GAME_PORTAL_LIVE_H
#define MAGMA_GAME_PORTAL_LIVE_H

#include "game/game.h"

typedef struct {
    int key_x, key_z;
    int portal_x, portal_y, portal_z;
    long long last_update_time;
} GmPortalCacheEntry;

typedef struct {
    GmPortalCacheEntry *entries;
    int count, cap;
} GmPortalCache;

/* Run verified BlockPortal frame detection around a newly placed fire block. */
int gm_portal_ignite(GmWorld *world, int fire_x, int fire_y, int fire_z);
/* Exact BlockPortal.Size validity and full-interior check for neighborChanged. */
int gm_portal_block_valid(
    GmWorld *world, int portal_x, int portal_y, int portal_z, int meta);
int gm_portal_find_existing(
    GmWorld *world, int near_x, int near_y, int near_z,
    double *out_x, double *out_y, double *out_z);
int gm_portal_find_or_make(
    GmWorld *world, double entity_x, double entity_y, double entity_z,
    uint64_t *random_seed48,
    double *out_x, double *out_y, double *out_z);
int gm_portal_find_or_make_cached(
    GmWorld *world, GmPortalCache *cache, long long total_time,
    double entity_x, double entity_y, double entity_z,
    uint64_t *random_seed48,
    double *out_x, double *out_y, double *out_z);
void gm_portal_cache_prune(GmPortalCache *cache, long long total_time);
void gm_portal_cache_clear(GmPortalCache *cache);
double gm_portal_transfer_coordinate(double coordinate, double scale);
/* Horizontal direction uses Java's EnumFacing horizontal index:
 * south=0, west=1, north=2, east=3. */
int gm_portal_capture_entry(
    GmWorld *world, int portal_x, int portal_y, int portal_z,
    double entity_x, double entity_y, double entity_z,
    double *out_vec_x, double *out_vec_y, int *out_direction);
int gm_portal_place_existing(
    GmWorld *world, int portal_x, int portal_y, int portal_z,
    double portal_vec_x, double portal_vec_y, int teleport_direction,
    double *io_x, double *io_y, double *io_z,
    double *io_vx, double *io_vz, float *io_yaw);
int gm_portal_place_player_existing(
    GmWorld *world, int portal_x, int portal_y, int portal_z,
    double portal_vec_x, double portal_vec_y, int teleport_direction,
    double *io_x, double *io_y, double *io_z,
    double *io_vx, double *io_vz, float *io_yaw);
/* Returns 1 for an inserted eye, 2 when the 3x3 End portal activates. */
int gm_end_portal_insert_eye(GmWorld *world, int frame_x, int frame_y, int frame_z);

#endif
