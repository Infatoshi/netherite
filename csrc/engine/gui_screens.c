/* GuiContainer.initGui and the vanilla Container constructors' slot
 * positions. Keep hit testing in scaled GUI coordinates, including the one
 * pixel margin in GuiContainer.func_146978_c. */
#include "gui_screens.h"

struct gui_screen_layout gui_screen_layout(const struct container *c,
                                            int width, int height, int gui_scale)
{
    struct gui_screen_layout g;
    int scale = 1;
    while (scale < gui_scale && width / (scale + 1) >= 320 && height / (scale + 1) >= 240)
        ++scale;
    g.scale = scale;
    g.sw = (width + scale - 1) / scale;
    g.sh = (height + scale - 1) / scale;
    g.width = 176;
    g.height = c && c->kind == CONTAINER_CHEST ? 114 + (c->chest_size / 9) * 18 :
               c && c->kind == CONTAINER_HOPPER ? 133 : 166;   /* GuiHopper's ySize */
    g.left = (g.sw - g.width) / 2;
    g.top = (g.sh - g.height) / 2;
    return g;
}

void gui_screen_effects_shift(struct gui_screen_layout *g, const struct container *c, int has_effects)
{
    if (has_effects && c && c->kind == CONTAINER_PLAYER)
        g->left = 160 + (g->sw - g->width - 200) / 2;
}

int gui_screen_key_click(const struct container *c, int slot, int hotbar, int drop, int ctrl,
                         int cursor_empty, int *button, int *mode)
{
    if (!c || slot < 0) return 0;
    if (hotbar >= 0 && hotbar < 9 && cursor_empty)
    {
        *button = hotbar;
        *mode = 2;
        return 1;
    }
    const struct craft_stack *st = container_slot((struct container *)c, slot);
    if (drop && st && st->count > 0)
    {
        *button = ctrl ? 1 : 0;
        *mode = 4;
        return 1;
    }
    return 0;
}

int gui_screen_slot_xy(const struct container *c, int slot, int *x, int *y)
{
    if (!c || slot < 0 || slot >= c->nslots) return 0;
    int kind = c->kind, n = c->chest_size, row, col;
    if (kind == CONTAINER_PLAYER)
    {
        if (slot == 0) { *x = 144; *y = 36; }
        else if (slot < 5) { *x = 88 + ((slot - 1) % 2) * 18; *y = 26 + ((slot - 1) / 2) * 18; }
        else if (slot < 9) { *x = 8; *y = 8 + (slot - 5) * 18; }
        else if (slot < 36) { *x = 8 + ((slot - 9) % 9) * 18; *y = 84 + ((slot - 9) / 9) * 18; }
        else { *x = 8 + (slot - 36) * 18; *y = 142; }
    }
    else if (kind == CONTAINER_WORKBENCH)
    {
        if (slot == 0) { *x = 124; *y = 35; }
        else if (slot < 10) { *x = 30 + ((slot - 1) % 3) * 18; *y = 17 + ((slot - 1) / 3) * 18; }
        else if (slot < 37) { *x = 8 + ((slot - 10) % 9) * 18; *y = 84 + ((slot - 10) / 9) * 18; }
        else { *x = 8 + (slot - 37) * 18; *y = 142; }
    }
    else if (kind == CONTAINER_FURNACE)
    {
        if (slot == 0) { *x = 56; *y = 17; }
        else if (slot == 1) { *x = 56; *y = 53; }
        else if (slot == 2) { *x = 116; *y = 35; }
        else if (slot < 30) { *x = 8 + ((slot - 3) % 9) * 18; *y = 84 + ((slot - 3) / 9) * 18; }
        else { *x = 8 + (slot - 30) * 18; *y = 142; }
    }
    else if (kind == CONTAINER_MERCHANT)
    {
        /* ContainerMerchant's two buys, the result, then the player's main
         * and hotbar, the furnace's lower half geometry. */
        if (slot == 0) { *x = 36; *y = 53; }
        else if (slot == 1) { *x = 62; *y = 53; }
        else if (slot == 2) { *x = 120; *y = 53; }
        else if (slot < 30) { *x = 8 + ((slot - 3) % 9) * 18; *y = 84 + ((slot - 3) / 9) * 18; }
        else { *x = 8 + (slot - 30) * 18; *y = 142; }
    }
    else if (kind == CONTAINER_CHEST)
    {
        if (slot < n) { row = slot / 9; col = slot % 9; *x = 8 + col * 18; *y = 18 + row * 18; }
        else if (slot < n + 27) { row = (slot - n) / 9; col = (slot - n) % 9; *x = 8 + col * 18; *y = 103 + (n / 9 - 4) * 18 + row * 18; }
        else { *x = 8 + (slot - n - 27) * 18; *y = 161 + (n / 9 - 4) * 18; }
    }
    else if (kind == CONTAINER_DISPENSER)
    {
        /* ContainerDispenser: the 3x3 grid, then the player's main and hotbar */
        if (slot < 9) { *x = 62 + (slot % 3) * 18; *y = 17 + (slot / 3) * 18; }
        else if (slot < 36) { *x = 8 + ((slot - 9) % 9) * 18; *y = 84 + ((slot - 9) / 9) * 18; }
        else { *x = 8 + (slot - 36) * 18; *y = 142; }
    }
    else if (kind == CONTAINER_HOPPER)
    {
        /* ContainerHopper: the five in a row, then the player's rows at 51 */
        if (slot < 5) { *x = 44 + slot * 18; *y = 20; }
        else if (slot < 32) { *x = 8 + ((slot - 5) % 9) * 18; *y = 51 + ((slot - 5) / 9) * 18; }
        else { *x = 8 + (slot - 32) * 18; *y = 109; }
    }
    else return 0;
    return 1;
}

int gui_screen_hit(const struct container *c, struct gui_screen_layout g, int x, int y)
{
    int sx = x - g.left, sy = y - g.top;
    for (int i = 0; c && i < c->nslots; ++i)
    {
        int px, py;
        gui_screen_slot_xy(c, i, &px, &py);
        if (sx >= px - 1 && sx < px + 17 && sy >= py - 1 && sy < py + 17)
            return i;
    }
    if (sx < 0 || sy < 0 || sx >= g.width || sy >= g.height) return -999;
    return -1;
}
