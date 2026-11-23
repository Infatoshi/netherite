#ifndef NETHERITE_RASTER_ITEMFRAME_H
#define NETHERITE_RASTER_ITEMFRAME_H

#include "nbtjson.h"
#include "raster_entity_quad.h"
#include "render_blocks.h"

/* One EntityItemFrame as its NBT saves it, with setDirection's bounding box. */
struct raster_itemframe {
    int tile[3], dir;
    double pos[3];
    float yaw;
    int item_id, item_damage, item_rot;   /* item_id 0: empty */
    double box[6];
};

void raster_itemframe_parse(const nbt *t, struct raster_itemframe *f);

/* Frustrum.isBoxInFrustum over ClippingHelperImpl's planes of the recorded
 * matrices, the box relative to CAM; and RenderGlobal.renderEntities' whole
 * gate for an item frame (its render range, then the frustum). */
int raster_itemframe_in_frustum(const float proj[16], const float mv[16], const double cam[3],
                                const double box[6]);
int raster_itemframe_rendered(const struct raster_itemframe *f, const float proj[16],
                              const float mv[16], const double cam[3]);

/* The compass frame index RenderItemFrame draws for F (updateCompass with its
 * last flag set), -1 without the scene's animation dump; and the compass's
 * updateAnimation after it, on the scene's texanim state. */
int raster_itemframe_compass(const char *scene, const struct raster_itemframe *f);
void raster_itemframe_compass_step(const char *scene);

/* RenderItemFrame.doRender for the live client: the frame's sprites from
 * SCENE's atlas.json over the live block atlas BLOCKS (AW x AH), the item's
 * icon from ICON_SCENE's item table and sheet (no compass needle). */
void raster_itemframe_draw_live(const char *scene, const char *icon_scene, const struct entity_raster_target *t,
                                const struct raster_itemframe *f, const struct rb_world *world,
                                const unsigned char *blocks, int aw, int ah);

/* RenderItemFrame.doRender for a recorded scene (see raster_itemframe.c). */
void raster_itemframe_draw(const char *scene, const struct entity_raster_target *t,
                           const struct raster_itemframe *f, const struct rb_world *world,
                           int has_world);

/* RenderHelper.enableStandardItemLighting's two lights and ambient for a
 * world-space unit normal (raster_entities.c). */
float raster_entity_diffuse(float nx, float ny, float nz);

#endif
