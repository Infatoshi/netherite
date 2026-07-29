#ifndef MAGMA_GAME_PLAYER_PREVIEW_H
#define MAGMA_GAME_PLAYER_PREVIEW_H

#include "core/types.h"

/* GuiInventory.drawEntityOnScreen: default-skin ModelPlayer into the inventory
 * preview viewport. mouse_x/mouse_y are the vanilla GUI-unit look-at deltas
 * ((guiLeft+51)-mouseX, (guiTop+25)-mouseY). Scale 30, atan*20 body / atan*40
 * head yaw / -atan*20 pitch — no empirical pose gains. */
void gm_player_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                            float mouse_x, float mouse_y);

#endif /* MAGMA_GAME_PLAYER_PREVIEW_H */
