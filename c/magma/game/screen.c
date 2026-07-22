/* game/screen.c - see screen.h. Slot coordinates are the vanilla 1.11.2 GUI
 * positions (net/minecraft/inventory/Container{Player,Workbench,Furnace}.java
 * Slot constructor x/y args) on the standard 176x166 panel. */
#include "game/screen.h"
#include "game/runtime.h"
#include "game/hud.h"

#include <string.h>

#define PANEL_W 176
#define PANEL_H 166
#define CELL    16
#define PITCH   18

static int gui_scale(int fb_h) { int s = fb_h / 240; return s > 1 ? s : 1; }

int gm_screen_gui_scale(int fb_h) { return gui_scale(fb_h); }

int gm_screen_kind_for_gui(const char *gui_name)
{
    if (!gui_name || !gui_name[0]) return -1;
    if (!strcmp(gui_name, "GuiInventory")) return 0;
    if (!strcmp(gui_name, "GuiCrafting"))  return 1;
    if (!strcmp(gui_name, "GuiFurnace"))   return 2;
    return -1;
}

void gm_screen_mouse_to_fb(int fb_w, int fb_h, int gmx, int gmy, int *mx, int *my)
{
    (void)fb_w;
    int s = gui_scale(fb_h);
    if (mx) *mx = gmx * s;
    if (my) *my = gmy * s;
}

/* Vanilla panel origin: GuiContainer centers in GUI units with integer
 * division ((scaledWidth - xSize) / 2), then scales. At 854x480 (scaled 427)
 * that floors to gui x 125 -> fb 250, one px left of naive fb centering. */
static void panel_origin(int fb_w, int fb_h, int s, int *px, int *py)
{
    int gw = (fb_w + s - 1) / s, gh = (fb_h + s - 1) / s;
    *px = (gw - PANEL_W) / 2 * s;
    *py = (gh - PANEL_H) / 2 * s;
}

/* Append one slot at vanilla gui-space (gx,gy). */
static int put(GmScreenSlot *out, int n, int max, int id,
               int px, int py, int s, int gx, int gy)
{
    if (n >= max) return n;
    out[n].slot_id = id;
    out[n].x = px + gx * s;
    out[n].y = py + gy * s;
    out[n].w = CELL * s;
    out[n].h = CELL * s;
    return n + 1;
}

int gm_screen_layout(int container, int fb_w, int fb_h, GmScreenSlot *out, int max)
{
    const int s  = gui_scale(fb_h);
    int px, py;
    panel_origin(fb_w, fb_h, s, &px, &py);
    int n = 0;

    /* player main 9..35 at (8,84) + hotbar 0..8 at (8,142): shared by every GUI */
    for (int i = 0; i < 27; ++i)
        n = put(out, n, max, 9 + i, px, py, s, 8 + (i % 9) * PITCH, 84 + (i / 9) * PITCH);
    for (int i = 0; i < 9; ++i)
        n = put(out, n, max, i, px, py, s, 8 + i * PITCH, 142);

    if (container == 1) {
        for (int i = 0; i < 9; ++i)
            n = put(out, n, max, GMC_GRID0 + i, px, py, s,
                    30 + (i % 3) * PITCH, 17 + (i / 3) * PITCH);
        n = put(out, n, max, GMC_RESULT, px, py, s, 124, 35);
    } else if (container == 2) {
        n = put(out, n, max, GMC_FURNACE0,     px, py, s, 56, 17);
        n = put(out, n, max, GMC_FURNACE0 + 1, px, py, s, 56, 53);
        n = put(out, n, max, GMC_FURNACE0 + 2, px, py, s, 116, 35);
    } else {
        /* player screen: vanilla 2x2 matrix at (98,18), result at (154,28) */
        static const int cells[4] = {0, 1, 3, 4};
        for (int i = 0; i < 4; ++i)
            n = put(out, n, max, GMC_GRID0 + cells[i], px, py, s,
                    98 + (i % 2) * PITCH, 18 + (i / 2) * PITCH);
        n = put(out, n, max, GMC_RESULT, px, py, s, 154, 28);
    }
    return n;
}

int gm_screen_slot_at(int container, int fb_w, int fb_h, int mx, int my)
{
    const int s  = gui_scale(fb_h);
    int px, py;
    panel_origin(fb_w, fb_h, s, &px, &py);
    if (mx < px || my < py || mx >= px + PANEL_W * s || my >= py + PANEL_H * s)
        return GMC_OUTSIDE;

    GmScreenSlot slots[GMC_SLOT_COUNT];
    int n = gm_screen_layout(container, fb_w, fb_h, slots, GMC_SLOT_COUNT);
    for (int i = 0; i < n; ++i)
        if (mx >= slots[i].x && mx < slots[i].x + slots[i].w &&
            my >= slots[i].y && my < slots[i].y + slots[i].h)
            return slots[i].slot_id;
    return -1;
}

/* ---- drawing ------------------------------------------------------------- */

static ICStack screen_stack(const GmRuntime *r, int id)
{
    ICStack taped;
    if (gm_runtime_tape_gui_slot_get(r, id, &taped)) return taped;
    if (id < GMC_INV_SLOTS) return isr_get_stack(&r->player.inv, id);
    if (id >= GMC_GRID0 && id < GMC_RESULT) return r->craft_grid[id - GMC_GRID0];
    if (id == GMC_RESULT) return gm_container_result(r);
    if (r->container == 2 && r->active_furnace >= 0) {
        const FurnaceLive *f = &r->furnaces[r->active_furnace].state;
        SRStack v = id == GMC_FURNACE0     ? f->input
                  : id == GMC_FURNACE0 + 1 ? f->fuel
                                           : f->output;
        if (!sr_isEmpty(v)) return ic_mk(v.item, v.count, v.meta);
    }
    return ic_empty();
}

/* Vanilla item cell: flat icon (gui_atlas) or pip fallback, plus the
 * renderItemOverlayIntoGUI count string (x+17-width, y+9, white, shadow).
 * (x,y) is the 16x16 icon position in framebuffer px, s the gui scale. */
static void draw_stack(CrFramebuffer *fb, ICStack v, int x, int y, int s)
{
    if (v.item <= 0 || v.count <= 0) return;
    if (!gm_gui_item_icon(fb, v.item, v.meta, x, y, s)) {
        int pip = 8 * s;
        gm_hud_fill(fb, x + 8 * s - pip / 2, y + 8 * s - pip / 2, pip, pip,
                    gm_hud_pip_color(v.item));
    }
    if (v.count > 1) {
        char buf[8];
        int n = v.count, len = 0, tmp = n;
        while (tmp > 0 && len < 7) { len++; tmp /= 10; }
        buf[len] = 0;
        for (int d = len - 1; d >= 0; d--) { buf[d] = (char)('0' + n % 10); n /= 10; }
        gm_font_draw(fb, buf, x + (17 - gm_font_width(buf)) * s, y + 9 * s,
                     s, 0xFFFFFFu, 1);
    }
}

/* GuiScreen.drawDefaultBackground: vertical gradient 0xC0101010 -> 0xD0101010
 * over the whole frame (alpha 192 top to 208 bottom, color 16,16,16). */
static void draw_background_dim(CrFramebuffer *fb)
{
    for (int y = 0; y < fb->h; ++y) {
        CrRgba c = {16, 16, 16,
                    (u8)(192 + (fb->h > 1 ? y * 16 / (fb->h - 1) : 0))};
        gm_hud_fill(fb, 0, y, fb->w, 1, c);
    }
}

void gm_screen_draw(CrFramebuffer *fb, const struct GmRuntime *r, int mx, int my)
{
    if (!fb || !fb->color || !r) return;
    const int s  = gui_scale(fb->h);
    int px, py;
    panel_origin(fb->w, fb->h, s, &px, &py);
    const CrRgba white     = {255, 255, 255, 255};
    const CrRgba highlight = {255, 255, 255, 128}; /* vanilla hovered-slot overlay */

    draw_background_dim(fb);

    /* real MC panel art (drawGuiContainerBackgroundLayer) */
    int panel = r->container == 1 ? GM_GUI_TABLE_PANEL
              : r->container == 2 ? GM_GUI_FURNACE_PANEL
                                  : GM_GUI_INV_PANEL;
    gm_gui_blit(fb, panel, px, py, s);

    /* furnace progress sprites (GuiFurnace.drawGuiContainerBackgroundLayer).
     * Vanilla always draws the (l+1)-wide arrow slice, even idle (l=0). */
    if (r->container == 2) {
        const FurnaceLive *f = !r->tape_furnace_active && r->active_furnace >= 0
                                   ? &r->furnaces[r->active_furnace].state : 0;
        int burn = r->tape_furnace_active ? r->tape_furnace_burn
                                           : (f ? f->burn_time : 0);
        int current = r->tape_furnace_active ? r->tape_furnace_current_burn
                                              : (f ? f->current_burn_time : 0);
        int cook = r->tape_furnace_active ? r->tape_furnace_cook
                                           : (f ? f->cook_time : 0);
        int total_cook = r->tape_furnace_active ? r->tape_furnace_total_cook
                                                 : (f ? f->total_cook : 0);
        if (burn > 0) {
            int total = current != 0 ? current : 200;
            int k = burn * 13 / total;
            if (k > 13) k = 13;
            gm_gui_blit_sub(fb, GM_GUI_FURNACE_FLAME, 0, 12 - k, 14, k + 1,
                            px + 56 * s, py + (36 + 12 - k) * s, s);
        }
        int l = total_cook != 0 && cook != 0 ? cook * 24 / total_cook : 0;
        if (l > 24) l = 24;
        gm_gui_blit_sub(fb, GM_GUI_FURNACE_ARROW, 0, 0, l + 1, 16,
                        px + 79 * s, py + 34 * s, s);
    }

    /* labels (drawGuiContainerForegroundLayer, color 4210752, no shadow) */
    if (r->container == 1) {
        gm_font_draw(fb, "Crafting", px + 28 * s, py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 2) {
        gm_font_draw(fb, "Furnace",
                     px + (PANEL_W / 2 - gm_font_width("Furnace") / 2) * s,
                     py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else {
        gm_font_draw(fb, "Crafting", px + 97 * s, py + 8 * s, s, 0x404040u, 0);
    }

    GmScreenSlot slots[GMC_SLOT_COUNT];
    int n = gm_screen_layout(r->container, fb->w, fb->h, slots, GMC_SLOT_COUNT);
    int hover = gm_screen_slot_at(r->container, fb->w, fb->h, mx, my);
    for (int i = 0; i < n; ++i) {
        const GmScreenSlot *sl = &slots[i];
        draw_stack(fb, screen_stack(r, sl->slot_id), sl->x, sl->y, s);
        if (sl->slot_id == hover)
            gm_hud_fill(fb, sl->x, sl->y, sl->w, sl->h, highlight);
    }

    /* cursor stack rides the mouse, drawn last */
    ICStack cur;
    if (!gm_runtime_tape_gui_cursor_get(r, &cur)) cur = gm_player_cursor();
    if (cur.item > 0 && cur.count > 0)
        draw_stack(fb, cur, mx - 8 * s, my - 8 * s, s);
    else {
        /* bare pointer so an empty cursor is still visible */
        gm_hud_fill(fb, mx - s, my - 4 * s, 2 * s, 8 * s, white);
        gm_hud_fill(fb, mx - 4 * s, my - s, 8 * s, 2 * s, white);
    }
}
