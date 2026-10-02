/* The world's weather and the first-person screen overlays, drawn into a
 * finished frame between the terrain/entity passes and the HUD:
 * EntityRenderer.renderRainSnow (the per-column rain and snow quads) and
 * ItemRenderer.renderOverlays (the textured quad of the block the player
 * stands in, the warped underwater overlay and the fire overlay).
 *
 * Both take the frame's own numbers: the camera pass for the weather, and the
 * renderHand projection (EntityRenderer.getFOVModifier(pt, false) over a fresh
 * identity modelview) for the overlays, which vanilla draws after clearing the
 * depth buffer. */
#ifndef NETHERITE_RASTER_WORLDFX_H
#define NETHERITE_RASTER_WORLDFX_H

#include <stdint.h>

struct rb_world;
struct rb_mesher;

struct worldfx_in
{
    int w, h;

    /* the world camera pass (EntityRenderer.setupCameraTransform) */
    const float *proj, *mv;
    const double *cam;              /* the camera position column vertices are relative to */
    const float *fog;               /* the terrain fog colour */
    float fogs, foge, fogd;
    int fogm;
    const uint32_t *lm;
    float *depth;
    unsigned char *rgb;
    unsigned long *triangles, *samples;

    /* EntityRenderer.renderHand's projection, over a fresh identity modelview */
    float hand_proj[16];

    /* the world the weather columns are read from, and the mesher whose biome
     * tables and perlin the biome temperature uses */
    const struct rb_world *world;
    struct rb_mesher *mesher;

    /* textures: the 64x64 environment sheets and the block atlas an overlay
     * draws a block icon or the fire sprite from */
    const unsigned char *rain_tex, *snow_tex;
    int rain_w, rain_h, snow_w, snow_h;
    const unsigned char *atlas;
    int atlas_w, atlas_h;
    const unsigned char *underwater_tex;
    int underwater_w, underwater_h;
    float fire_u0, fire_u1, fire_v0, fire_v1;   /* Blocks.fire.func_149840_c(1) */

    /* the frame's state */
    int ruc;                        /* EntityRenderer.rendererUpdateCount */
    int fancy;                      /* gameSettings.fancyGraphics: the weather radius 10, else 5 */
    float pt;                       /* partialTicks */
    double eye_x, eye_y, eye_z;     /* the render view entity, interpolated by pt */
    double eye_now_x, eye_now_y, eye_now_z;  /* the same entity, uninterpolated */
    float rain, prain;              /* World's rainingStrength pair */
    int mat;                        /* the block at the entity viewpoint: 1 water, 2 lava */
    int burning;
    int underwater;                 /* Entity.isInsideOfMaterial(Material.water) */
    float brightness;               /* Entity.getBrightness(pt) */
    float yaw, pitch;               /* the player's rotation, the underwater overlay's uv */
    float far;                      /* EntityRenderer.farPlaneDistance */

    /* ItemRenderer.renderInsideOfBlock: the side-2 icon of the block the player
     * is inside, and how many times the pass runs (renderOverlays draws it
     * twice when the block at the player's feet is itself a normal cube). */
    float inside_uv[4];
    int inside_count;
};

/* Entity.isEntityInsideOpaqueBlock at (pos, width, eye height). */
int worldfx_is_inside_opaque(const struct rb_world *w, double pos_x, double pos_y,
                             double pos_z, double width, double eye_height);

/* ItemRenderer.renderOverlays' in-block branch: fills uv with the side-2 icon
 * of the block the player is inside and returns how many times
 * renderInsideOfBlock runs (0, 1 or 2), or 0 with uv untouched when the player
 * is not inside an opaque block. */
int worldfx_inside_block(const struct rb_world *w, const struct rb_mesher *m,
                         double pos_x, double pos_y, double pos_z,
                         double width, double height, float uv[4]);

/* EntityRenderer.renderRainSnow. */
void worldfx_weather(const struct worldfx_in *v);

/* ItemRenderer.renderOverlays: the block the player is inside, the underwater
 * overlay and the fire overlay. */
void worldfx_screen(const struct worldfx_in *v);

#endif