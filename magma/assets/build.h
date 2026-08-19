#ifndef MAGMA_ASSETS_BUILD_H
#define MAGMA_ASSETS_BUILD_H

#include <stddef.h>

typedef struct AssetJar AssetJar;

typedef struct {
    int w, h;
    unsigned char *rgba;
} AssetImage;

AssetJar *asset_jar_open(const char *path);
void asset_jar_close(AssetJar *jar);
int asset_jar_read(AssetJar *jar, const char *member, unsigned char **data, size_t *size);
void asset_data_free(void *data);

int asset_image_load(AssetJar *jar, const char *member, AssetImage *image);
int asset_image_new(AssetImage *image, int w, int h);
int asset_image_crop(const AssetImage *src, int x, int y, int w, int h, AssetImage *dst);
int asset_image_resize_nearest(const AssetImage *src, int w, int h, AssetImage *dst);
int asset_image_paste(AssetImage *dst, const AssetImage *src, int x, int y);
void asset_image_free(AssetImage *image);

/* only: NULL = all seven; or exact base name atlas|colormap|loading|portal|sky|underwater|animations */
int asset_build_world(AssetJar *jar, const char *out_dir, const char *only);
int asset_build_ui(AssetJar *jar, const char *out_dir, const char *only);

#endif /* MAGMA_ASSETS_BUILD_H */
