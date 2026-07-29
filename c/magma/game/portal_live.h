#ifndef MAGMA_GAME_PORTAL_LIVE_H
#define MAGMA_GAME_PORTAL_LIVE_H

#include "game/game.h"

/* Run verified BlockPortal frame detection around a newly placed fire block. */
int gm_portal_ignite(GmWorld *world, int fire_x, int fire_y, int fire_z);
int gm_portal_find_or_make(GmWorld *world, int near_x, int near_z,
                           double *out_x, double *out_y, double *out_z);
/* Returns 1 for an inserted eye, 2 when the 3x3 End portal activates. */
int gm_end_portal_insert_eye(GmWorld *world, int frame_x, int frame_y, int frame_z);

#endif
