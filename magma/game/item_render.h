/* game/item_render.h - dropped-item (EntityItem) + GUI block icons for magma.
 *
 * Vanilla 1.11.2 RenderEntityItem look: a dropped BLOCK is a miniature block
 * model at GROUND scale 0.25 with its real per-face terrain sprites; a dropped
 * ITEM (no block model) is a 16x16 sprite at GROUND scale 0.5 with 1/16
 * extrusion (ItemModelGenerator). Their model JSON ground translations
 * (+3/16 Y for blocks, +2/16 Y for generated items) apply after bobbing.
 * Both bob and spin with age + the frame's partialTicks=1.
 *
 * GUI block icons: software isometric mini-cube (gui display rot 30/225/0,
 * scale 0.625) with per-face shade factors - see gm_item_draw_block_icon.
 *
 * TWO ATLASES -> TWO PASSES. Block cubes (and cross-model plants, drawn as a
 * flat extruded sprite of their block texture) sample the TERRAIN atlas
 * (bm_atlas()); other items sample the item atlas (assets/item_atlas.h):
 *
 *   int nb = gm_items_emit(ents, n, verts, max);        // bind bm_atlas()
 *   int ni = gm_items_emit_flat(ents, n, verts, max);   // bind gm_item_atlas()
 *
 * Only entities with type == GM_VIEW_ITEM are considered; each entity is
 * emitted by exactly one of the two passes.
 */
#ifndef MAGMA_GAME_ITEM_RENDER_H
#define MAGMA_GAME_ITEM_RENDER_H

#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Terrain-atlas pass: miniature 0.25-scale block cubes (36 verts each) and
 * cross-model plant drops as an extruded flat of the block sprite (36 verts).
 * Returns vertex count written (<= max). */
int gm_items_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* RenderFallingBlock (oracle-src RenderFallingBlock.java:32-74) and
 * RenderTNTPrimed: full-size block models at the entity feet. Falling blocks
 * use item_id/item_meta as fallTile; primed TNT uses block 46 and applies the
 * final-ten-fuse-ticks expansion. Terrain atlas; 36 verts per entity. */
int gm_falling_blocks_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* RenderMinecart display tiles (chest/furnace/hopper/TNT), terrain atlas. */
int gm_minecart_contents_emit(const GmEntityView *ents, int n,
                              CrVertex *out, int max);

/* Item-atlas pass: extruded 16x16 sprite boxes for non-block items (36 verts
 * each; 1/16 extrusion after GROUND scale 0.5). Returns vertex count written. */
int gm_items_emit_flat(const GmEntityView *ents, int n, CrVertex *out, int max);

/* Held items (LayerHeldItem, item atlas): pigman gold sword, skeleton/stray
 * bow, drawn at the right hand through the vanilla third-person transform
 * chain. 12 verts (front+back quad) per armed mob. Same pass/atlas as
 * gm_items_emit_flat. */
int gm_held_items_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* Camera-facing projectiles: RenderSnowball item models plus exact direct
 * RenderFireball (item 385, scale 0.5) and RenderDragonFireball (view type 33,
 * scale 2.0) quads. view_yaw/view_pitch = camera rotation in degrees. Same
 * pass/atlas as gm_items_emit_flat. */
int gm_items_emit_billboard(const GmEntityView *ents, int n, float view_yaw,
                            float view_pitch, CrVertex *out, int max);

/* Render.doRenderShadowAndFire for fireballs with flags&1 (isBurning).
 * item_meta>=2 uses EntityLargeFireball width 1.0 (scale 1.4); else
 * EntitySmallFireball 0.3125 (scale 0.4375). Terrain atlas fire_layer sprites.
 * Non-burning views (flags bit 0 clear) emit nothing — matches RenderManager. */
int gm_small_fireball_fire_emit(const GmEntityView *ents, int n,
                                float view_yaw, CrVertex *out, int max);

/* Render.doRenderShadowAndFire for LIVING entities with flags&1 (the recorded
 * EntityLivingBase.isBurning bit; for a blaze that is the ON_FIRE/charged
 * aggro flag). Fire layers sized by the entity AABB (gm_entity_render_box),
 * terrain-atlas fire_layer sprites, same pass as the fireball overlay.
 * Non-living views and non-burning views emit nothing. */
int gm_entity_fire_emit(const GmEntityView *ents, int n,
                        float view_yaw, CrVertex *out, int max);

/* The packed item-sprite atlas as a CrTexture (no mips; CUTOUT layer). */
CrTexture gm_item_atlas(void);

/* GUI/hotbar: draw an isometric mini-cube for a BLOCK item into the 16x16
 * (gui-px) slot at (dx,dy) scaled by `scale`. Returns 1 if drawn, 0 if the
 * id is not a block cube (caller falls back to flat gui_atlas / pip).
 * fb may be NULL to probe without drawing. Allocate-once internal buffers. */
int gm_item_draw_block_icon(CrFramebuffer *fb, int item_id, int item_meta,
                            int dx, int dy, int scale);

/* Classification/lookups exposed for tests.
 * gm_item_drop_uses_block_atlas: 1 when (id,meta) renders in the terrain-atlas
 * pass (block cube or cross-plant). gm_item_sprite_index: index into
 * CR_ITEM_SPRITES for an item id, or the fallback sprite when unknown. */
int gm_item_drop_uses_block_atlas(int item_id, int item_meta);
int gm_item_sprite_index(int item_id);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_ITEM_RENDER_H */
