#ifndef NETHERITE_GUI_SCREENS_H
#define NETHERITE_GUI_SCREENS_H

#include "container.h"

/* Coordinates are in GuiScreen's scaled pixels. The SDL window and the
 * renderer both convert through gui_screen_scale before using these. */
struct gui_screen_layout {
    int sw, sh, scale, left, top, width, height;
};

struct gui_screen_layout gui_screen_layout(const struct container *c,
                                            int width, int height, int gui_scale);
/* InventoryEffectRenderer.initGui: the player's inventory moves right of the
 * effect column when the player has an effect. */
void gui_screen_effects_shift(struct gui_screen_layout *g, const struct container *c, int has_effects);
/* GuiContainer.keyTyped over the hovered SLOT (-1 none): checkHotbarKeys'
 * swap (a number key HOTBAR 0-8 with nothing on the cursor, mode 2, button
 * the hotbar slot), else the drop key over a slot holding a stack (mode 4,
 * button 1 with control down). Returns 1 with the click, 0 for none. */
int gui_screen_key_click(const struct container *c, int slot, int hotbar, int drop, int ctrl,
                         int cursor_empty, int *button, int *mode);
int gui_screen_slot_xy(const struct container *c, int slot, int *x, int *y);
int gui_screen_hit(const struct container *c, struct gui_screen_layout g, int x, int y);

#endif
