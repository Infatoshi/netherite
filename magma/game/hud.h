/* game/hud.h - 2D survival HUD overlay composited onto a finished framebuffer.
 * Owner: HUD agent. See game/game.h for the seam contract. */
#ifndef MAGMA_GAME_HUD_H
#define MAGMA_GAME_HUD_H

#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the gui sprites once. Returns 0 on success, nonzero on failure. */
int  gm_hud_init(void);

/* Stateful GuiIngame.renderPlayerStats health bookkeeping. update_counter is
 * GuiIngame.updateCounter (one increment per client tick). The derived values
 * are stored in pv so deferred frame composition keeps the state of its tick. */
typedef struct {
    int initialized;
    int player_health, last_player_health;
    int previous_hurt_time;
    long long health_update_counter, last_sync_counter;
} GmHudState;
void gm_hud_state_step(GmHudState *state, GmPlayerView *pv,
                       long long update_counter);

/* Draw the survival HUD (hotbar + selection, hearts, hunger, XP bar+level,
 * crosshair) onto fb. Does NOT touch fb->depth. Safe for any fb size.
 * When pv->dead, draws GuiGameOver (gradient, title, score, buttons) instead
 * of the survival HUD. Cursor from gm_hud_set_pointer drives button hover. */
void gm_hud_draw(CrFramebuffer *fb, const GmPlayerView *pv);

/* Framebuffer-space mouse for GuiGameOver button hover (and future screens).
 * Call before gm_hud_draw when the death screen is interactive. */
void gm_hud_set_pointer(int mx, int my);

/* GuiGameOver layout at fb size (vanilla scale = fb_h/240). All outs are
 * framebuffer pixels. Buttons are 200x20 gui units. */
void gm_hud_death_layout(int fb_w, int fb_h,
                         int *btn0_x, int *btn0_y, int *btn1_x, int *btn1_y,
                         int *btn_w, int *btn_h);

/* Hit-test GuiGameOver buttons. Returns 0 (Respawn), 1 (Title Screen), or -1.
 * When buttons_enabled is 0 (enableButtonsTimer < 20) always returns -1. */
int gm_hud_death_button_at(int fb_w, int fb_h, int mx, int my,
                           int buttons_enabled);

/* 1 when death_ticks >= 20 (GuiGameOver.enableButtonsTimer). */
int gm_hud_death_buttons_enabled(int death_ticks);

/* GuiBossOverlay: show/hide the ender-dragon boss bar for subsequent
 * gm_hud_draw calls; frac is health/max in [0,1]. */
void gm_hud_set_boss(int show, float frac);

/* Armor row reads GmPlayerView.armor_points (live equipped armor total).
 * No module-global setter: both interactive and capture paths fill the
 * field via gm_player_view / gm_runtime_view before gm_hud_draw. */

/* Test helpers: vanilla XP fill column count (int)(frac*183) and durability
 * strip width/color for a (item_id, damage) pair. Returns 0 width when the
 * item is not damageable or damage is 0. */
int gm_hud_xp_fill_cols(float frac);
int gm_hud_durability_width(int item_id, int damage);
void gm_hud_durability_rgb(int item_id, int damage, unsigned char *r,
                           unsigned char *g, unsigned char *b);

/* 2D overlay primitives shared with the container screen (game/screen.c):
 * alpha-composited fill, 3x5-digit number (returns width in px), and the
 * deterministic per-item pip color used for slot markers. */
void   gm_hud_fill(CrFramebuffer *fb, int dx, int dy, int w, int h, CrRgba c);
int    gm_hud_number(CrFramebuffer *fb, int n, int dx, int dy, int scale, CrRgba c);
CrRgba gm_hud_pip_color(int id);

/* Fixed sprite indices into assets/gui_atlas.h (hud.c static-asserts these
 * match the generated GUI_* defines, so callers need not include the data). */
enum {
    GM_GUI_INV_PANEL = 0,
    GM_GUI_TABLE_PANEL,
    GM_GUI_FURNACE_PANEL,
    GM_GUI_FURNACE_FLAME,
    GM_GUI_FURNACE_ARROW,
    GM_GUI_FONT,
    GM_GUI_CHEST_PANEL,
    GM_GUI_BREWING_PANEL,
    GM_GUI_BREWING_PROGRESS,
    GM_GUI_BREWING_FUEL,
    GM_GUI_BREWING_BUBBLES,
    GM_GUI_ENCHANTING_PANEL
};

/* Container-GUI art (assets/gui_atlas.h: real MC panels, font, item icons).
 * gm_gui_blit draws sprite `idx` (GUI_* from gui_atlas.h) at (dx,dy) scaled;
 * gm_gui_blit_sub draws only the sprite-local sub-rect (sx,sy,sw,sh).
 * gm_gui_item_icon draws a block item as an isometric mini-cube (vanilla GUI
 * display transform) or a flat 16x16 tile for 2D items; returns 1 if an icon
 * exists, 0 if not (caller falls back to the pip). */
void gm_gui_blit(CrFramebuffer *fb, int idx, int dx, int dy, int scale);
void gm_gui_blit_sub(CrFramebuffer *fb, int idx, int sx, int sy, int sw, int sh,
                     int dx, int dy, int scale);
int  gm_gui_item_icon(CrFramebuffer *fb, int item_id, int item_meta,
                      int dx, int dy, int scale);

/* Vanilla-metric MC font (ascii.png; FontRenderer.readFontTexture widths:
 * space=4, else rightmost non-empty glyph column + 2). gm_font_width returns
 * the string width in unscaled gui px; gm_font_draw renders at (dx,dy) with
 * 0xRRGGBB color, optional vanilla drop shadow (+1,+1 at (rgb&0xFCFCFC)>>2). */
int  gm_font_width(const char *s);
void gm_font_draw(CrFramebuffer *fb, const char *s, int dx, int dy, int scale,
                  unsigned rgb, int shadow);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_HUD_H */
