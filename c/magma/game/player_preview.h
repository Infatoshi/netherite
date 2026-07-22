#ifndef MAGMA_GAME_PLAYER_PREVIEW_H
#define MAGMA_GAME_PLAYER_PREVIEW_H

#include "core/types.h"

/* GuiInventory.drawEntityOnScreen: draw the default-skin player into the
 * inventory preview viewport, looking toward the supplied cursor offsets. */
void gm_player_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                            float mouse_x, float mouse_y);

#endif /* MAGMA_GAME_PLAYER_PREVIEW_H */
