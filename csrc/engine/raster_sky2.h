#ifndef NETHERITE_RASTER_SKY2_H
#define NETHERITE_RASTER_SKY2_H

struct raster_sky2_vertex {
    float x, y, z, u, v;
};

typedef void (*raster_sky2_quad)(void *frame, int mode,
                                  const struct raster_sky2_vertex vertices[4]);

void raster_sky2_draw(void *frame, float angle, int moon_phase,
                      raster_sky2_quad quad);

/* RenderGlobal.renderStars' list, one quad per star with mode 8, under the
 * same celestial rotation as the sun and moon. */
void raster_sky2_stars(void *frame, float angle, raster_sky2_quad draw);

/* The celestial rotation's cosine and sine (glRotatef(angle * 360, 1, 0, 0)
 * as the passes above apply it), and the stars' unrotated quads (4 vertices
 * each), for a renderer that applies them itself (raster_obs.h). */
void raster_sky2_rotation(float angle, float *cs, float *sn);
const struct raster_sky2_vertex *raster_sky2_star_table(int *n);

#endif
