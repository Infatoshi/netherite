/* game/hand.h - first-person HAND / HELD ITEM drawn over the world.
 * Owner: HAND agent. See game/game.h for the seam contract; see game/hand.c for
 * the faithful reconstruction of MC ItemRenderer first-person paths. */
#ifndef MAGMA_GAME_HAND_H
#define MAGMA_GAME_HAND_H

#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the first-person viewmodel over the finished world render and BEFORE the
 * 2D HUD. Empty hotbar slot -> bare right arm (ItemRenderer.renderArmFirstPerson).
 * Non-empty -> held block/item with transformSideFirstPerson + transformFirstPerson
 * + firstperson_righthand camera transform (no arm under the item, matching
 * vanilla). Clears fb->depth to far first so the viewmodel always composites
 * on top (matches MC's per-hand GL_DEPTH_BUFFER_BIT clear).
 *
 * bob_phase: monotonically advancing walk-bob phase (radians); reserved for
 * applyBobbing (no-op at rest). */
void gm_hand_draw(CrFramebuffer *fb, const GmPlayerView *pv, float bob_phase);

/* ItemRenderer.renderFireInFirstPerson: two animated fire_layer_1 quads at
 * alpha 0.9, rendered after the water overlay and before GuiIngame. */
void gm_hand_fire_overlay_draw(CrFramebuffer *fb, const CrTexture *atlas,
                               float fov_scale);

/* Set the current attack SWING progress in [0,1] (0 = rest). The game loop
 * advances a swing animation on left-click and pushes it here each frame
 * before gm_hand_draw; both arm and held-item paths fold it into the vanilla
 * swing terms. */
void gm_hand_set_swing(float progress);

/* ItemRenderer state supplied by the live/capture loop. equip is vanilla's
 * 1-equippedProgress transform term; the item override is the stack retained
 * while a hotbar change lowers the old item before raising the new one. */
void gm_hand_set_equip(float equip);
void gm_hand_set_hurt(int hurt_time, int max_hurt_time, float attacked_yaw);
void gm_hand_set_item_override(int item_id, int item_meta, int count);

/* Select the arm variant: 1 = slim/alex (3px arm, rotation point y 2.5),
 * 0 = default/steve (4px). Offline players get either by username-UUID hash
 * (DefaultPlayerSkin); the tape records which via the set_skin script event. */
void gm_hand_set_skin(int slim);

/* Ticks the bow has been drawn (active use elapsed); <=0 = idle. Drives the
 * drawn-bow viewmodel pose + pulling sprite (ItemRenderer BOW using branch). */
void gm_hand_set_bow_pull(int ticks);

/* Item use pose while the main hand is active (isHandActive). action:
 *   0 = none (swing path)
 *   1 = EAT / DRINK (transformEatFirstPerson + transformSideFirstPerson)
 *   2 = BLOCK (transformSideFirstPerson only; no swing transform)
 * remaining/max: getItemInUseCount / getMaxItemUseDuration (eat branch).
 * BOW is driven by gm_hand_set_bow_pull instead. */
void gm_hand_set_use(int action, int remaining_ticks, int max_duration);

/* Per-frame viewmodel environment (all optional; defaults keep legacy look):
 * lightmap  - the frame's 16x16 updateLightmap texture (NULL = fold mul_* into
 *             the vertex tint instead, for the legacy/Nether/End path);
 * sky/blk   - 0..15 light levels at the EYE block (ItemRenderer.setLightmap
 *             samples getCombinedLight at posY + eye height);
 * mul_*     - prefolded lightmap RGB multiplier when lightmap is NULL;
 * fov_scale - renderHand projects with getFOVModifier(pt,false): base 70 times
 *             the eye-in-water 60/70 squeeze;
 * yaw/pitch - interpolated player rotation: rotateArroundXAndY enables the
 *             RenderHelper item lights under Rx(pitch)*Ry(yaw), anchoring the
 *             two diffuse directions to the camera orientation. */
void gm_hand_set_env(const CrRgba *lightmap, float sky, float blk,
                     float mul_r, float mul_g, float mul_b,
                     float fov_scale, float yaw_deg, float pitch_deg);

/* Test/emit hook: first-person held-item geometry for (item_id, meta, swing,
 * equip) into out[]. Returns 36 vertices for a block cube, or 12 front/back
 * vertices plus 6 per opaque sprite edge for generated items. Returns 0 when
 * item_id is empty or max cannot hold the complete mesh. Does not draw. */
int gm_hand_emit_held(int item_id, int item_meta, float swing, float equip,
                      CrVertex *out, int max);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_HAND_H */
