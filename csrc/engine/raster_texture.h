#ifndef NV2_RASTER_TEXTURE_H
#define NV2_RASTER_TEXTURE_H

struct raster_texture {
    int width, height, levels, min_filter;
    unsigned char *rgba[5];
};

void raster_texture_load(struct raster_texture *t, const char *dir,
                         int width, int height, int levels, int min_filter);
void raster_texture_sample(const struct raster_texture *t, float u, float v,
                           float ux, float vx, float uy, float vy, float out[4]);
void raster_texture_free(struct raster_texture *t);

#endif
