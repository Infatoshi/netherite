#include "raster_texture.h"
#include "texanim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char mip_channel(const unsigned char *a, const unsigned char *b,
                                 const unsigned char *c, const unsigned char *d,
                                 int channel, int transparent)
{
    const unsigned char *p[4] = {a, b, c, d};
    float sum = 0;
    for (int i = 0; i < 4; ++i)
        if (!transparent || p[i][3])
            sum += (float)pow((double)((float)p[i][channel] / 255.0f), 2.2);
    if (transparent) {
        sum /= 4.0f;
        int value = (int)(pow((double)sum, 0.45454545454545453) * 255.0);
        return (unsigned char)(channel == 3 && value < 96 ? 0 : value);
    }
    float value = (float)pow((double)sum * 0.25, 0.45454545454545453);
    return (unsigned char)((int)((double)value * 255.0));
}

static void make_level(struct raster_texture *t, int level, const unsigned char *transparent)
{
    int width = t->width >> level, height = t->height >> level;
    int prev_width = width * 2, tiles_w = t->width / 16;
    t->rgba[level] = malloc((size_t)width * height * 4);
    if (!t->rgba[level]) { fprintf(stderr, "raster: mip atlas allocation failed\n"); exit(1); }
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const unsigned char *a = t->rgba[level - 1] + ((size_t)(2*y) * prev_width + 2*x) * 4;
        const unsigned char *b = a + 4;
        const unsigned char *c = a + (size_t)prev_width * 4;
        const unsigned char *d = c + 4;
        unsigned char *out = t->rgba[level] + ((size_t)y * width + x) * 4;
        int cutout = transparent[((y << level) / 16) * tiles_w + (x << level) / 16];
        for (int k = 0; k < 4; ++k)
            out[k] = mip_channel(a, b, c, d, k, cutout);
    }
}

void raster_texture_load(struct raster_texture *t, const char *dir,
                         int width, int height, int levels, int min_filter)
{
    t->width = width;
    t->height = height;
    t->levels = levels < 4 ? levels : 4;
    t->min_filter = min_filter;
    unsigned char *transparent = NULL;
    for (int level = 0; level <= t->levels; ++level) {
        char path[1024];
        if (level) snprintf(path, sizeof path, "%s/atlas_%d.rgba", dir, level);
        else snprintf(path, sizeof path, "%s/atlas.rgba", dir);
        FILE *file = fopen(path, "rb");
        if (!file && level) {
            if (!transparent) {
                int tiles_w = width / 16, tiles_h = height / 16;
                transparent = calloc((size_t)tiles_w * tiles_h, 1);
                if (!transparent) exit(1);
                /* TextureUtil checks only the first mip-array-length pixels
                 * of each sprite for zero alpha, not the whole sprite. */
                for (int y = 0; y < tiles_h; ++y) for (int x = 0; x < tiles_w; ++x)
                    for (int i = 0; i <= t->levels; ++i)
                        if (!t->rgba[0][((size_t)(16*y) * width + 16*x + i) * 4 + 3])
                            transparent[y * tiles_w + x] = 1;
            }
            make_level(t, level, transparent);
            continue;
        }
        size_t size = (size_t)(width >> level) * (height >> level) * 4;
        t->rgba[level] = malloc(size);
        if (!file || !t->rgba[level] || fread(t->rgba[level], 1, size, file) != size) {
            fprintf(stderr, "raster: atlas read failed: %s\n", path);
            exit(1);
        }
        fclose(file);
        /* the animated sprites at the recorded frame (TextureMap.updateAnimations) */
        if (level == 0) texanim_patch_scene(dir, TEXANIM_BLOCKS, t->rgba[0], width, height);
    }
    free(transparent);
}

static const unsigned char *texel(const struct raster_texture *t, int level,
                                  float u, float v)
{
    int width = t->width >> level, height = t->height >> level;
    int x = (int)floorf(u * width), y = (int)floorf(v * height);
    x = (x % width + width) % width;
    y = (y % height + height) % height;
    return t->rgba[level] + ((size_t)y * width + x) * 4;
}

void raster_texture_sample(const struct raster_texture *t, float u, float v,
                           float ux, float vx, float uy, float vy, float out[4])
{
    float lod = 0;
    if (t->min_filter == 9986 && t->levels) {
        float dx = hypotf((ux - u) * t->width, (vx - v) * t->height);
        float dy = hypotf((uy - u) * t->width, (vy - v) * t->height);
        lod = log2f(fmaxf(dx, dy));
    }
    if (lod <= 0 || t->min_filter != 9986 || !t->levels) {
        const unsigned char *p = texel(t, 0, u, v);
        for (int c = 0; c < 4; ++c) out[c] = p[c] / 255.0f;
        return;
    }
    if (lod > t->levels) lod = (float)t->levels;
    int low = (int)floorf(lod), high = low < t->levels ? low + 1 : low;
    float mix = lod - low;
    const unsigned char *a = texel(t, low, u, v);
    const unsigned char *b = texel(t, high, u, v);
    for (int c = 0; c < 4; ++c)
        out[c] = ((1.0f - mix) * a[c] + mix * b[c]) / 255.0f;
}

void raster_texture_free(struct raster_texture *t)
{
    for (int level = 0; level <= t->levels; ++level) free(t->rgba[level]);
}
