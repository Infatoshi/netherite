#ifndef MAGMA_GAME_FISHING_RENDER_H
#define MAGMA_GAME_FISHING_RENDER_H

#include "core/types.h"

struct GmRuntime;

enum {
    GM_FISHING_LINE_POINTS = 17,
    GM_FISHING_LINE_MAX_VERTS = 16 * 12
};

typedef struct {
    float x, y, z;
} GmFishingLinePoint;

/* RenderFish's exact first-person 17-point GL_LINE_STRIP curve. The runtime
 * stores post-tick positions, so product capture calls this at partial=1. */
int gm_fishing_line_points(
    const struct GmRuntime *runtime, float partial_ticks,
    float swing_progress, float fov_setting,
    GmFishingLinePoint out[GM_FISHING_LINE_POINTS]);

/* Rasterize the locked curve as camera-facing one-pixel ribbons. Vanilla uses
 * GL_LINE_STRIP; magma's raster core consumes triangles, so each segment is a
 * two-sided quad whose world width is derived from distance/FOV/viewport. */
int gm_fishing_line_emit(
    const struct GmRuntime *runtime, float partial_ticks,
    float swing_progress, float fov_setting,
    float camera_x, float camera_y, float camera_z,
    float vertical_fov_deg, int viewport_height,
    CrVertex *out, int cap);

#endif
