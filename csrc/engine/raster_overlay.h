#ifndef NETHERITE_RASTER_OVERLAY_H
#define NETHERITE_RASTER_OVERLAY_H

#include "render_blocks.h"
#include "raster_texture.h"

struct raster_overlay_in {
    int w, h;
    const float *proj, *mv, *depth;
    const double *cam;
    unsigned char *rgb;
    const struct raster_texture *texture;
    struct rb_mesher *mesher;
    const struct rb_atlas *atlas;
    int selected, sx, sy, sz;
    double box[6];                 /* world coordinates, before the 0.002 expansion */
    int stage, dx, dy, dz;         /* stage -1 means no damage */
    /* renderWorld's fog (setupFog(0) stays enabled for both passes): the
     * colour, then GL's mode (0 none), start, end and density */
    const float *fog;
    float fogs, foge, fogd;
    int fogm;
};

void raster_overlay_draw(const struct raster_overlay_in *in);
/* one crack triangle from its three projected points (x y z w u v f), as
 * a recording holds it (raster_obs.h) */
void raster_overlay_crack_triangle(const struct raster_overlay_in *in, const float p[3][7]);
/* one outline line as a recording holds it (raster_obs.h; its own fog) */
struct raster_obs_line;
void raster_overlay_line(const struct raster_overlay_in *in, const struct raster_obs_line *l);

#endif
