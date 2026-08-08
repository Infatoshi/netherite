/* game/screen.h - interactive container screen (player inventory / crafting table /
 * furnace) for the WINDOWED client. 2D overlay + mouse hit-testing only: every
 * click is routed as a GmAction.inv_click through the authoritative runtime tick
 * into gm_container_click, so the screen adds no second inventory logic.
 *
 * Slot positions are the vanilla 1.11.2 GUI coordinates (ContainerPlayer /
 * ContainerWorkbench / ContainerFurnace slot x/y) on a 176x166 panel, integer-scaled
 * like the HUD. Rendering uses the vanilla panel/font/slot textures plus the
 * cursor-facing ModelPlayer preview. */
#ifndef MAGMA_GAME_SCREEN_H
#define MAGMA_GAME_SCREEN_H

#include "game/game.h"

struct GmRuntime;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int slot_id;      /* container_live slot id (0..48) */
    int x, y, w, h;   /* framebuffer pixels */
} GmScreenSlot;

/* Fill `out` with every clickable slot rect for the given container type
 * (0 player, 1 crafting table, 2 furnace, 3 chest) on a fb_w x fb_h frame.
 * Returns the count written (<= max). */
int gm_screen_layout(int container, int fb_w, int fb_h, GmScreenSlot *out, int max);

/* Map a window-space mouse position to a container_live slot id: a slot id on a
 * slot, -1 inside the panel between slots (no-op), GMC_OUTSIDE beyond the panel
 * (vanilla cursor drop). */
int gm_screen_slot_at(int container, int fb_w, int fb_h, int mx, int my);

/* Draw the open screen (panel, slots, stacks, craft result preview, furnace
 * burn/cook state) plus the cursor stack at (mx,my). Color buffer only. */
void gm_screen_draw(CrFramebuffer *fb, const struct GmRuntime *r, int mx, int my);

/* Map a vanilla GuiScreen simple class name (tape "gui" field) to a container
 * kind for gm_screen_draw: 0 player, 1 workbench, 2 furnace, 3 chest,
 * 4 brewing stand, 5 enchanting table. Returns -1
 * for unmapped screens (GuiIngameMenu, GuiChat, etc.) - skip, do not draw. */
int gm_screen_kind_for_gui(const char *gui_name);

/* Vanilla gui scale factor used by screen layout (fb_h/240, min 1). At the
 * product 854x480 this is 2, matching ScaledResolution -> 427x240 gui space. */
int gm_screen_gui_scale(int fb_h);

/* Convert vanilla ScaledResolution mouse coords (tape gmx/gmy) to framebuffer
 * pixels for gm_screen_draw / gm_screen_slot_at. */
void gm_screen_mouse_to_fb(int fb_w, int fb_h, int gmx, int gmy, int *mx, int *my);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_SCREEN_H */
