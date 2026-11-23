#ifndef NETHERITE_RASTER_ENTITIES_H
#define NETHERITE_RASTER_ENTITIES_H

#include "raster_entity_quad.h"

#include <stdint.h>

void raster_entities_draw(const char *scene, int w, int h,
                          const float proj[16], const float mv[16],
                          const double cam[3], const float fog[3],
                          float fogs, float foge, float fogd, int fogm,
                          const uint32_t lm[256], float *depth, unsigned char *rgb,
                          unsigned long *triangles, unsigned long *samples);

/* One item or orb entity of the live world, in the recorded rows' own terms.
 * light is Entity.getBrightnessForRender's packed value (an orb's +120 block
 * boost already applied), color an orb's xpColor. */
struct worldfx_entity
{
    int orb;
    int id, meta, count;     /* the stack; an orb's value is count */
    int tag;                 /* the stack's tag (itemtag.h), 0 for none */
    int age;
    float hover;             /* EntityItem.hoverStart */
    double x, y, z;
    int light;
    int color;
    int unlit;               /* renderLitParticles' pass (the pickup animation): GL_LIGHTING off */
};

/* The live items and orbs, drawn with the recorded path's arithmetic: an item's
 * flat sprite (RenderItem.doRender's fast path, the bob, the stack's copy
 * offsets and the seeded jitter) and an orb's quad. The sprite's uv and tint
 * come from the item table of ICON_SCENE (a scene's state/item_table.json).
 * A block item (mode 1) is RenderItem's 3D path, the block item model of
 * ASSETS drawn from ATLAS (the live block atlas, which a flat item of the
 * block sheet also samples). Returns the number drawn. */
int raster_entities_live(const char *assets, const char *icon_scene,
                         const struct entity_raster_target *t,
                         const struct worldfx_entity *e, int n,
                         float pt, float view_yaw, float view_pitch,
                         const unsigned char *atlas, int atlas_w, int atlas_h);

/* RenderPainting.doRender for the live client: art ART (the EnumArt ordinal)
 * hung facing DIR, turned YAW, at X, Y, Z, its texture SCENE/painting.rgba. */
struct rb_world;
void raster_painting_live(const char *scene, const struct entity_raster_target *t, int art, int dir, float yaw,
                          double x, double y, double z, const struct rb_world *world);

/* RenderXPOrb.doRender's quad at X, Y, Z (raster_entities.c). */
void raster_orb_quad(const struct entity_raster_target *t,
                     const unsigned char *tex, int tex_w, int tex_h,
                     double x, double y, double z, int value, int color,
                     double pt, float pitch, float yaw, int light);

/* RenderItem.doRender for one EntityItem entry (the render_items / "ents"
 * item fields) drawn at X, Y, Z with GL_LIGHTING off: the pickup animation's
 * moving item, in renderLitParticles. */
struct jval;
void raster_world_item_at(const char *scene, const struct entity_raster_target *t,
                          const struct jval *item, double x, double y, double z);

#endif
