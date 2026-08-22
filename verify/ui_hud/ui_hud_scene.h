/* Same-scene world underlay for overlay_portal_050 / overlay_underwater. */
#ifndef MAGMA_VERIFY_UI_HUD_SCENE_H
#define MAGMA_VERIFY_UI_HUD_SCENE_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Render the capture pad (superflat seed 0, stone pad + wall, optional glass
 * pool) through window_compose into dst. Returns 1 on success. */
int ui_hud_scene_draw(CrFramebuffer *dst, const char *id);
void ui_hud_scene_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
