/* game/overlay.h - targeted-block feedback geometry (OVERLAY module).
 *
 * Vanilla parity affordances drawn over the terrain each frame:
 *  - RenderGlobal.drawSelectionBox: black (0,0,0,0.4) wireframe around the
 *    raycast target, AABB expandXyz(0.0020000000949949026), glLineWidth 2.0,
 *    blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA, depth test on / depthMask false.
 *  - RenderGlobal.drawBlockDamageTexture: destroy_stage_{0..9} crack decal on
 *    the progressive-dig target at floor(damage*10)-1, clamped to 0..9. Blend is
 *    DST_COLOR/SRC_COLOR (2*src*dst multiply) with white vertex colour -
 *    draw via gm_overlay_emit_crack + shade.blend=2, separate from selection.
 * Pure geometry emitters: world-space CrVertex quads for the TERRAIN atlas,
 * each face in BOTH windings so exactly one copy survives backface culling
 * from any camera side. Selection must be drawn with blend=1 (TRANSLUCENT),
 * alpha_test=0, no fog - not cutout - or the 0.4 alpha is lost.
 */
#ifndef MAGMA_GAME_OVERLAY_H
#define MAGMA_GAME_OVERLAY_H

#include "core/types.h"
#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upper bound on vertices gm_overlay_emit can write
 * (outline 12 edges x 12 verts = 144 + crack 72). */
#define GM_OVERLAY_MAX_VERTS 216

/* Selection outline only (blend=1 src-over). */
int gm_overlay_emit_sel(CrVertex *v, int max,
                        int sx, int sy, int sz, const float *sel_box,
                        float eye_x, float eye_y, float eye_z);
/* Crack decal only (blend=2 multiply-2x). damage in (0,1].
 * face: 0..5 = -z/+z/-x/+x/+y/-y, or -1 for all six faces. */
int gm_overlay_emit_crack(CrVertex *v, int max,
                          int dx, int dy, int dz, float damage, int face);
/* Combined emit (legacy tests): selection then crack into one buffer. Caller
 * that needs correct dig-crack style must draw the two passes separately. */
int gm_overlay_emit(CrVertex *v, int max,
                    int have_sel, int sx, int sy, int sz, const float *sel_box,
                    int have_dig, int dx, int dy, int dz, float damage,
                    float eye_x, float eye_y, float eye_z);
/* GuiIngame.renderPortal: stretch the current portal atlas tile over the
 * framebuffer with the vanilla fourth-power alpha curve, before the HUD. */
void gm_overlay_portal_screen(CrFramebuffer *fb, const CrTexture *atlas,
                              float time_in_portal);
/* EntityRenderer.setupCameraTransform portal projection. The oracle records
 * rendererUpdateCount and tick-boundary frames use partialTicks=1. */
void gm_overlay_portal_warp(CrFramebuffer *fb, CrRgba *scratch,
                            float time_in_portal, int renderer_phase,
                            float fov_deg);
/* GuiDownloadTerrain: vanilla tiled dirt background and centered label. */
void gm_overlay_loading_screen(CrFramebuffer *fb);

/* ItemRenderer.renderBlockInHand: eye-inside-opaque-block screen overlay.
 * View-space quad x,y in [-1,1] at z=-0.5 under hand FOV (gluPerspective
 * getFOVModifier(partial,false) = 70), UVs maxU/maxV on the left/bottom
 * (U mirrored). GlStateManager.color(0.1,0.1,0.1,0.5) with blend OFF
 * (unlike water/fire) -> replace RGB with tex * 0.1. u0..v1 = sprite
 * minU,minV,maxU,maxV. fov_deg is the active hand projection FOV. */
void gm_overlay_block_in_hand(CrFramebuffer *fb, const CrTexture *atlas,
                              float u0, float v0, float u1, float v1,
                              float fov_deg);

/* Live path: Entity.isEntityInsideOpaqueBlock + sample block face texture,
 * then renderBlockInHand. No-op when the eye is not in a suffocating block.
 * Call order among overlays (vanilla ItemRenderer.renderOverlays then portal):
 *   block_in_hand_live -> underwater -> fire -> portal -> HUD. */
struct GmWorld;
void gm_overlay_block_in_hand_live(CrFramebuffer *fb, const CrTexture *atlas,
                                   const struct GmWorld *w,
                                   const GmPlayerView *pv);

/* The two halves of the above. A deferred frame must resolve the block while
 * the world still matches that frame's tick and draw from the snapshot later;
 * re-sampling the live world at retire time reads a world that has moved on. */
int gm_overlay_block_in_hand_pick(const struct GmWorld *w,
                                  const GmPlayerView *pv,
                                  int *out_id, int *out_meta);
void gm_overlay_block_in_hand_draw(CrFramebuffer *fb, const CrTexture *atlas,
                                   int bid, int bmeta);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_OVERLAY_H */
