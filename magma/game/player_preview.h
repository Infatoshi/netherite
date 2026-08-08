#ifndef MAGMA_GAME_PLAYER_PREVIEW_H
#define MAGMA_GAME_PLAYER_PREVIEW_H

#include "core/types.h"

/* GuiInventory.drawEntityOnScreen: default-skin ModelPlayer into the inventory
 * preview viewport. mouse_x/mouse_y are the vanilla GUI-unit look-at deltas
 * ((guiLeft+51)-mouseX, (guiTop+25)-mouseY). Scale 30, atan*20 body / atan*40
 * head yaw / -atan*20 pitch — no empirical pose gains. */
void gm_player_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                            float mouse_x, float mouse_y);

/* GuiScreenHorseInventory uses the same drawEntityOnScreen helper at scale 17.
 * variant is AbstractHorse.getHorseVariant(), armor is 0..3, and flags use the
 * entity-render horse bits (8 child, 8192 chest, 16384 saddle, 32768 ridden). */
void gm_horse_preview_draw(CrFramebuffer *fb, int x, int y, int w, int h,
                           int type, int variant, int armor, int flags,
                           float mouse_x, float mouse_y);

/* GuiEnchantment's ModelBook under its dedicated 320x240 perspective
 * viewport. `open` and `flip` are the pinned GuiEnchantment animation fields. */
void gm_enchant_book_draw(CrFramebuffer *fb, float open, float flip);

#endif /* MAGMA_GAME_PLAYER_PREVIEW_H */
