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
typedef struct GmNativeSaveInfo GmNativeSaveInfo;

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
 * 4 brewing stand, 5 enchanting table, 6 anvil, 7 merchant, 8 horse,
 * 9 shulker box, 10 large chest, 11 beacon, 12 structure block editor,
 * 13 dispenser/dropper, 14 hopper.
 * Returns -1
 * for unmapped screens (GuiIngameMenu, GuiChat, etc.) - skip, do not draw. */
int gm_screen_kind_for_gui(const char *gui_name);

/* GuiEditStructure non-container controls. Return the vanilla button id or
 * local form-field enum under the pointer, respectively; -1 means none. */
int gm_screen_structure_button_at(
    const struct GmRuntime *r, int fb_w, int fb_h, int mx, int my);
int gm_screen_structure_field_at(
    const struct GmRuntime *r, int fb_w, int fb_h, int mx, int my);

/* Vanilla gui scale factor used by screen layout (fb_h/240, min 1). At the
 * product 854x480 this is 2, matching ScaledResolution -> 427x240 gui space. */
int gm_screen_gui_scale(int fb_h);

/* Convert vanilla ScaledResolution mouse coords (tape gmx/gmy) to framebuffer
 * pixels for gm_screen_draw / gm_screen_slot_at. */
void gm_screen_mouse_to_fb(int fb_w, int fb_h, int gmx, int gmy, int *mx, int *my);

/* In-game pause and native world-selection screens. Button ids: pause 0
 * resume, 1 save-and-title; world list 0 play, 1 back. */
int gm_screen_pause_button_at(int fb_w, int fb_h, int mx, int my);
void gm_screen_pause_draw(
    CrFramebuffer *fb, int mx, int my, const char *status);
int gm_screen_world_row_at(
    int fb_w, int fb_h, int mx, int my, int world_count);
int gm_screen_world_button_at(int fb_w, int fb_h, int mx, int my);
void gm_screen_world_list_draw(
    CrFramebuffer *fb, const GmNativeSaveInfo *worlds, int world_count,
    int selected, int mx, int my, const char *status);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_SCREEN_H */
