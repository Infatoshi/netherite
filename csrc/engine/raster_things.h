#ifndef NETHERITE_RASTER_THINGS_H
#define NETHERITE_RASTER_THINGS_H

/* The entity renders that are not living models, from a recorded frame's
 * "ents" block (EntityRenderProbe): RenderArrow, RenderFireball,
 * RenderSnowball (snowballs, eggs, pearls, eyes of ender, splash potions,
 * experience bottles), RenderTNTPrimed, RenderFallingBlock,
 * RenderLightningBolt and Render.renderEntityOnFire; and the pieces the living
 * models and the pickup animation share: ItemRenderer.renderItem for a held
 * stack (the flat extruded sprite, the block model, the enchant glint). */

#include <stdint.h>
#include "raster_glx.h"

struct jval;

/* One stack as ItemRenderer.renderItem and RenderBiped read it
 * (EntityRenderProbe.stack). */
struct thing_item
{
    int id, meta, sprite, b3d, ib3d, bpass, rc, full3d, rot, multi, bow, eff, npass;
    struct
    {
        float min_u, max_u, min_v, max_v;
        int iw, ih, tint, has;
    } pass[2];
    int armor, armor_index, cloth, color;
};

/* Fills IT from a stack object; 0 when O is null or has no item. */
int thing_item_parse(const struct jval *o, struct thing_item *it);

/* ItemRenderer.renderItem(entity, stack, PASS) in G's current state: the block
 * model for a sprite-0 renderItemIn3d block, else renderItemIn2D of the pass's
 * icon at the item transform, with the glint on pass 0 of an item with the
 * effect. NOW is Minecraft.getSystemTime(). */
void thing_render_item(struct glx *g, const char *scene, const struct thing_item *it, int pass,
                       int64_t now);

/* RenderArrow.doRender's body at G's origin: the arrow at YAW and PITCH
 * (already interpolated), SHAKE the arrowShake minus the partial tick. */
void thing_arrow(struct glx *g, const char *scene, float yaw, float pitch, float shake);

/* RenderBlocks.renderBlockAsItem(block, meta, BRIGHTNESS) in G's state; RC is
 * the block's getRenderColor(meta). */
void thing_block_as_item(struct glx *g, const char *scene, int id, int meta, int rc,
                         float brightness);

/* Render.renderEntityOnFire at render position X, Y, Z (relative to the
 * camera) with the view yaw; WIDTH, HEIGHT and FDY (posY - boundingBox.minY)
 * are the entity's. */
void thing_fire(const struct entity_raster_target *t, const char *scene, float x, float y, float z,
                float width, float height, float fdy, float view_yaw, int brightness);

/* The frame's "ents" list, in RenderGlobal.renderEntities' order. */
void raster_things_scene(const char *scene, const struct entity_raster_target *t);
/* The playable client's own list of the same entries (arrows, fireballs,
 * thrown sprites, TNT, falling blocks, bolts; the fields EntityRenderProbe
 * writes), drawn as the recorded list is: VIEW_YAW and VIEW_PITCH are
 * RenderManager's playerViewY and playerViewX, NOW getSystemTime. */
struct jval;
void raster_things_live(const char *scene, const struct entity_raster_target *t, const struct jval *ents,
                        float view_yaw, float view_pitch, float pt, int64_t now);

/* A texture of the scene's state dumps (state/NAME.rgba) by size, cached per
 * scene; NULL when absent. */
const unsigned char *thing_texture(const char *scene, const char *name, int w, int h);

#endif
