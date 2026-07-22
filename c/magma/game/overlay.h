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

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_OVERLAY_H */
