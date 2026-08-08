/* game/screen.c - see screen.h. Slot coordinates are the vanilla 1.11.2 GUI
 * positions (net/minecraft/inventory/Container{Player,Workbench,Furnace,Chest}.java
 * Slot constructor x/y args) on the standard 176-wide panel. */
#include "game/screen.h"
#include "game/runtime.h"
#include "game/hud.h"
#include "game/player_preview.h"
#include "assets/inventory_ui_atlas.h"

#include <string.h>

#define PANEL_W 176
#define PANEL_H 166
/* GuiChest: ySize = 114 + inventoryRows*18 = 168 for a single chest (3 rows).
 * The generic_54 blit is only 167 tall (71+96); Java still centers with ySize. */
#define CHEST_YSIZE 168
#define CHEST_TEX_H 167
#define CELL    16
#define PITCH   18

static int gui_scale(int fb_h) { int s = fb_h / 240; return s > 1 ? s : 1; }

/* Layout / hit-test height: GuiContainer uses ySize for guiTop and bounds. */
static int panel_h(int container) { return container == 3 ? CHEST_YSIZE : PANEL_H; }

int gm_screen_gui_scale(int fb_h) { return gui_scale(fb_h); }

int gm_screen_kind_for_gui(const char *gui_name)
{
    if (!gui_name || !gui_name[0]) return -1;
    if (!strcmp(gui_name, "GuiInventory")) return 0;
    if (!strcmp(gui_name, "GuiCrafting"))  return 1;
    if (!strcmp(gui_name, "GuiFurnace"))   return 2;
    if (!strcmp(gui_name, "GuiChest"))     return 3;
    if (!strcmp(gui_name, "GuiBrewingStand")) return 4;
    if (!strcmp(gui_name, "GuiEnchantment")) return 5;
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
static void panel_origin_h(int fb_w, int fb_h, int s, int ph, int *px, int *py)
{
    int gw = (fb_w + s - 1) / s, gh = (fb_h + s - 1) / s;
    *px = (gw - PANEL_W) / 2 * s;
    *py = (gh - ph) / 2 * s;
}
static void panel_origin(int fb_w, int fb_h, int s, int *px, int *py)
{
    panel_origin_h(fb_w, fb_h, s, PANEL_H, px, py);
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
    const int ph = panel_h(container);
    int px, py;
    panel_origin_h(fb_w, fb_h, s, ph, &px, &py);
    int n = 0;

    /* ContainerChest: numRows=3, i=(3-4)*18=-18 -> main y=85, hotbar y=143.
     * Other containers share main y=84 / hotbar y=142. */
    int main_y = container == 3 ? 85 : 84;
    int hot_y  = container == 3 ? 143 : 142;
    for (int i = 0; i < 27; ++i)
        n = put(out, n, max, 9 + i, px, py, s, 8 + (i % 9) * PITCH, main_y + (i / 9) * PITCH);
    for (int i = 0; i < 9; ++i)
        n = put(out, n, max, i, px, py, s, 8 + i * PITCH, hot_y);

    if (container == 1) {
        for (int i = 0; i < 9; ++i)
            n = put(out, n, max, GMC_GRID0 + i, px, py, s,
                    30 + (i % 3) * PITCH, 17 + (i / 3) * PITCH);
        n = put(out, n, max, GMC_RESULT, px, py, s, 124, 35);
    } else if (container == 2) {
        n = put(out, n, max, GMC_FURNACE0,     px, py, s, 56, 17);
        n = put(out, n, max, GMC_FURNACE0 + 1, px, py, s, 56, 53);
        n = put(out, n, max, GMC_FURNACE0 + 2, px, py, s, 116, 35);
    } else if (container == 3) {
        /* ContainerChest: 3 rows at (8, 18 + row*18) */
        for (int i = 0; i < GMC_CHEST_SLOTS; ++i)
            n = put(out, n, max, GMC_CHEST0 + i, px, py, s,
                    8 + (i % 9) * PITCH, 18 + (i / 9) * PITCH);
    } else if (container == 4) {
        n = put(out, n, max, GMC_BREWING0, px, py, s, 56, 51);
        n = put(out, n, max, GMC_BREWING0 + 1, px, py, s, 79, 58);
        n = put(out, n, max, GMC_BREWING0 + 2, px, py, s, 102, 51);
        n = put(out, n, max, GMC_BREWING0 + 3, px, py, s, 79, 17);
        n = put(out, n, max, GMC_BREWING0 + 4, px, py, s, 17, 17);
    } else if (container == 5) {
        n = put(out, n, max, GMC_ENCHANT0, px, py, s, 15, 47);
        n = put(out, n, max, GMC_ENCHANT0 + 1, px, py, s, 35, 47);
        for (int i = 0; i < 3 && n < max; ++i) {
            out[n].slot_id = GMC_ENCHANT_BUTTON0 + i;
            out[n].x = px + 60 * s;
            out[n].y = py + (14 + i * 19) * s;
            out[n].w = 108 * s;
            out[n].h = 19 * s;
            ++n;
        }
    } else {
        /* player screen: armor HEAD..FEET at (8, 8+k*18) -> GMC_ARMOR0+3..0
         * (isr 39..36); 2x2 matrix at (98,18), result at (154,28) */
        for (int k = 0; k < 4; ++k)
            n = put(out, n, max, GMC_ARMOR0 + (3 - k), px, py, s, 8, 8 + k * PITCH);
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
    const int ph = panel_h(container);
    int px, py;
    panel_origin_h(fb_w, fb_h, s, ph, &px, &py);
    if (mx < px || my < py || mx >= px + PANEL_W * s || my >= py + ph * s)
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
    if (id >= GMC_ARMOR0 && id < GMC_ARMOR0 + ISR_ARMOR_SLOTS)
        return isr_get_stack(&r->player.inv, ISR_ARMOR0 + (id - GMC_ARMOR0));
    if (id >= GMC_GRID0 && id < GMC_RESULT) return r->craft_grid[id - GMC_GRID0];
    if (id == GMC_RESULT) return gm_container_result(r);
    if (r->container == 2 && r->active_furnace >= 0) {
        const FurnaceLive *f = &r->furnaces[r->active_furnace].state;
        SRStack v = id == GMC_FURNACE0     ? f->input
                  : id == GMC_FURNACE0 + 1 ? f->fuel
                                           : f->output;
        if (!sr_isEmpty(v)) return ic_mk(v.item, v.count, v.meta);
    }
    if (r->container == 3 && r->active_chest >= 0 &&
        id >= GMC_CHEST0 && id < GMC_CHEST0 + GMC_CHEST_SLOTS)
        return chest_live_get(&r->chests[r->active_chest].state, id - GMC_CHEST0);
    if (r->container == 4 && r->active_static_container >= 0
            && r->active_static_container < r->static_containers_cap
            && id >= GMC_BREWING0
            && id < GMC_BREWING0 + GMC_BREWING_SLOTS) {
        const GmRuntimeStaticContainer *stand =
            &r->static_containers[r->active_static_container];
        if (stand->active && stand->block == 117)
            return stand->slots[id - GMC_BREWING0];
    }
    if (r->container == 5 && r->enchanting.open
            && id >= GMC_ENCHANT0
            && id < GMC_ENCHANT0 + GMC_ENCHANT_SLOTS)
        return r->enchanting.slots[id - GMC_ENCHANT0];
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

/* Slot.getSlotTexture: ContainerPlayer's empty armor/offhand slots use five
 * standalone item-atlas sprites, not pixels from inventory.png. */
static void draw_inventory_ui_sprite(CrFramebuffer *fb, int sprite,
                                     int x, int y, int s)
{
    if (sprite < 0 || sprite >= INVENTORY_UI_SPRITE_COUNT) return;
    for (int sy = 0; sy < CELL; ++sy) {
        for (int sx = 0; sx < CELL; ++sx) {
            const unsigned char *p = &INVENTORY_UI_RGBA[sprite][(sy * CELL + sx) * 4];
            if (p[3] == 0) continue;
            gm_hud_fill(fb, x + sx * s, y + sy * s, s, s,
                        (CrRgba){p[0], p[1], p[2], p[3]});
        }
    }
}

static void draw_empty_player_slots(CrFramebuffer *fb, const GmRuntime *r,
                                    int px, int py, int s)
{
    /* Empty armor icons only when the matching real slot is empty. Display
     * order is HEAD, CHEST, LEGS, FEET (top to bottom). */
    static const int armor_icon[4] = {
        INVENTORY_UI_EMPTY_HELMET, INVENTORY_UI_EMPTY_CHESTPLATE,
        INVENTORY_UI_EMPTY_LEGGINGS, INVENTORY_UI_EMPTY_BOOTS
    };
    static const int armor_isr[4] = {
        ISR_ARMOR_HEAD, ISR_ARMOR_CHEST, ISR_ARMOR_LEGS, ISR_ARMOR_FEET
    };
    for (int i = 0; i < 4; ++i) {
        ICStack piece = isr_get_stack(&r->player.inv, armor_isr[i]);
        if (piece.item > 0 && piece.count > 0) continue;
        draw_inventory_ui_sprite(fb, armor_icon[i], px + 8 * s,
                                 py + (8 + i * PITCH) * s, s);
    }

    ICStack offhand = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
    if (offhand.item > 0 && offhand.count > 0)
        draw_stack(fb, offhand, px + 77 * s, py + 62 * s, s);
    else
        draw_inventory_ui_sprite(fb, INVENTORY_UI_EMPTY_SHIELD,
                                 px + 77 * s, py + 62 * s, s);
}

/* Mesa/Java fixed-function SRC_ALPHA unorm8: separate (c*a+127)/255 multiplies
 * then add (not fused (src*a+dst*ia+127)/255). Differs by 1 on some values
 * (e.g. border end R=40 a=80 over fill 23: fused 28 vs separate 29). Scoped
 * to inventory tooltip bg/border/gradient only — shared gm_hud_fill keeps the
 * fused path proven for HUD death/durability goldens. */
static void tooltip_mesa_unorm8_blend_px(CrFramebuffer *fb, int x, int y, CrRgba src)
{
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    if (src.a == 0) return;
    CrRgba *d = &fb->color[y * fb->w + x];
    if (src.a == 255) { *d = src; return; }
    int a = src.a, ia = 255 - a;
    int r = (src.r * a + 127) / 255 + (d->r * ia + 127) / 255;
    int g = (src.g * a + 127) / 255 + (d->g * ia + 127) / 255;
    int b = (src.b * a + 127) / 255 + (d->b * ia + 127) / 255;
    d->r = (u8)(r > 255 ? 255 : r);
    d->g = (u8)(g > 255 ? 255 : g);
    d->b = (u8)(b > 255 ? 255 : b);
    d->a = (u8)(a + (d->a * ia + 127) / 255);
}

static void tooltip_mesa_unorm8_fill(CrFramebuffer *fb, int dx, int dy, int w,
                                    int h, CrRgba c)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            tooltip_mesa_unorm8_blend_px(fb, dx + x, dy + y, c);
}

/* GuiUtils.drawGradientRect. Coordinates and colors are in scaled-GUI units;
 * after GUI scale(s) the quad spans [fy0, fy1) in framebuffer rows. OpenGL
 * samples pixel centers: t = (y + 0.5) / h for row y in 0..h-1, then rounds
 * each channel. Endpoint-inclusive (den=h-1) left tooltip side-border residual
 * at max channel 1; pixel-center matches the GL smooth-shade sample points.
 * Composites via tooltip_mesa_unorm8_fill (not gm_hud_fill). */
static void draw_gui_gradient(CrFramebuffer *fb, int x0, int y0, int x1, int y1,
                              int s, unsigned top, unsigned bottom)
{
    int fy0 = y0 * s, fy1 = y1 * s;
    int h = fy1 - fy0;
    int ta = (top >> 24) & 255, tr = (top >> 16) & 255;
    int tg = (top >> 8) & 255, tb = top & 255;
    int ba = (bottom >> 24) & 255, br = (bottom >> 16) & 255;
    int bg = (bottom >> 8) & 255, bb = bottom & 255;
    for (int y = 0; y < h; ++y) {
        /* t = (y+0.5)/h as fixed-point over 2h so +0.5 rounds cleanly. */
        int num = 2 * y + 1; /* 2*(y+0.5) */
        int den = 2 * h;
        if (den < 1) den = 1;
        CrRgba c = {
            (u8)((tr * (den - num) + br * num + den / 2) / den),
            (u8)((tg * (den - num) + bg * num + den / 2) / den),
            (u8)((tb * (den - num) + bb * num + den / 2) / den),
            (u8)((ta * (den - num) + ba * num + den / 2) / den)
        };
        tooltip_mesa_unorm8_fill(fb, x0 * s, fy0 + y, (x1 - x0) * s, 1, c);
    }
}

static const char *screen_item_name(int item)
{
    switch (item) {
        case 1: return "Stone";
        case 3: return "Dirt";
        default: return 0;
    }
}

/* GuiScreen.renderToolTip -> Forge GuiUtils.drawHoveringText, one-line form. */
static void draw_tooltip(CrFramebuffer *fb, const char *text, int mx, int my, int s)
{
    if (!text) return;
    const int gw = (fb->w + s - 1) / s, gh = (fb->h + s - 1) / s;
    const int mouse_x = mx / s, mouse_y = my / s;
    const int text_w = gm_font_width(text), text_h = 8;
    int x = mouse_x + 12, y = mouse_y - 12;
    if (x + text_w + 4 > gw) x = mouse_x - 16 - text_w;
    if (y + text_h + 6 > gh) y = gh - text_h - 6;

    const unsigned bg = 0xF0100010u;
    const unsigned border_top = 0x505000FFu;
    const unsigned border_bottom = 0x5028007Fu;
    draw_gui_gradient(fb, x - 3, y - 4, x + text_w + 3, y - 3, s, bg, bg);
    draw_gui_gradient(fb, x - 3, y + text_h + 3,
                      x + text_w + 3, y + text_h + 4, s, bg, bg);
    draw_gui_gradient(fb, x - 3, y - 3, x + text_w + 3,
                      y + text_h + 3, s, bg, bg);
    draw_gui_gradient(fb, x - 4, y - 3, x - 3, y + text_h + 3, s, bg, bg);
    draw_gui_gradient(fb, x + text_w + 3, y - 3,
                      x + text_w + 4, y + text_h + 3, s, bg, bg);
    draw_gui_gradient(fb, x - 3, y - 2, x - 2, y + text_h + 2,
                      s, border_top, border_bottom);
    draw_gui_gradient(fb, x + text_w + 2, y - 2, x + text_w + 3,
                      y + text_h + 2, s, border_top, border_bottom);
    draw_gui_gradient(fb, x - 3, y - 3, x + text_w + 3, y - 2,
                      s, border_top, border_top);
    draw_gui_gradient(fb, x - 3, y + text_h + 2, x + text_w + 3,
                      y + text_h + 3, s, border_bottom, border_bottom);
    gm_font_draw(fb, text, x * s, y * s, s, 0xFFFFFFu, 1);
}

void gm_screen_draw(CrFramebuffer *fb, const struct GmRuntime *r, int mx, int my)
{
    if (!fb || !fb->color || !r) return;
    const int s  = gui_scale(fb->h);
    const int ph = panel_h(r->container);
    int px, py;
    panel_origin_h(fb->w, fb->h, s, ph, &px, &py);
    const CrRgba highlight = {255, 255, 255, 128}; /* vanilla hovered-slot overlay */

    draw_background_dim(fb);

    /* real MC panel art (drawGuiContainerBackgroundLayer) */
    int panel = r->container == 1 ? GM_GUI_TABLE_PANEL
              : r->container == 2 ? GM_GUI_FURNACE_PANEL
              : r->container == 3 ? GM_GUI_CHEST_PANEL
              : r->container == 4 ? GM_GUI_BREWING_PANEL
              : r->container == 5 ? GM_GUI_ENCHANTING_PANEL
                                  : GM_GUI_INV_PANEL;
    gm_gui_blit(fb, panel, px, py, s);
    if (r->container == 0) {
        draw_empty_player_slots(fb, r, px, py, s);
        {
            int gw = (fb->w + s - 1) / s, gh = (fb->h + s - 1) / s;
            int guiLeft = (gw - PANEL_W) / 2, guiTop = (gh - PANEL_H) / 2;
            int gmx = mx * gw / fb->w, gmy = my * gh / fb->h;
            gm_player_preview_draw(fb, px + 24 * s, py + 7 * s, 52 * s, 72 * s,
                                   (float)(guiLeft + 51 - gmx),
                                   (float)(guiTop + 25 - gmy));
        }
    }

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

    if (r->container == 4) {
        static const int bubbles[7] = {29, 24, 20, 16, 11, 6, 0};
        const GmRuntimeStaticContainer *stand = NULL;
        if (r->active_static_container >= 0
                && r->active_static_container < r->static_containers_cap)
            stand = &r->static_containers[r->active_static_container];
        int fuel = r->tape_brewing_active
            ? r->tape_brewing_fuel
            : stand && stand->active && stand->block == 117
                ? stand->brewing.fuel : 0;
        int brew = r->tape_brewing_active
            ? r->tape_brewing_brew
            : stand && stand->active && stand->block == 117
                ? stand->brewing.brew_time : 0;
        int fuel_width = (18 * fuel + 19) / 20;
        if (fuel_width > 18) fuel_width = 18;
        if (fuel_width > 0)
            gm_gui_blit_sub(
                fb, GM_GUI_BREWING_FUEL, 0, 0, fuel_width, 4,
                px + 60 * s, py + 44 * s, s);
        if (brew > 0) {
            int progress = (int)(28.0f * (1.0f - (float)brew / 400.0f));
            if (progress > 0)
                gm_gui_blit_sub(
                    fb, GM_GUI_BREWING_PROGRESS, 0, 0, 9, progress,
                    px + 97 * s, py + 16 * s, s);
            int bubble = bubbles[(brew / 2) % 7];
            if (bubble > 0)
                gm_gui_blit_sub(
                    fb, GM_GUI_BREWING_BUBBLES, 0, 29 - bubble,
                    12, bubble, px + 63 * s,
                    py + (14 + 29 - bubble) * s, s);
        }
    }

    if (r->container == 5) {
        for (int i = 0; i < 3; ++i) {
            int level = r->enchanting.offer.levels[i];
            if (level <= 0) continue;
            char text[16];
            int n = level, len = 0;
            do { text[len++] = (char)('0' + n % 10); n /= 10; } while (n);
            for (int a = 0, b = len - 1; a < b; ++a, --b) {
                char t = text[a]; text[a] = text[b]; text[b] = t;
            }
            text[len] = 0;
            unsigned color = r->player_xp_level >= level
                    && r->enchanting.slots[1].count >= i + 1
                ? 0x80FF20u : 0xFF6060u;
            gm_font_draw(
                fb, text,
                px + (160 - gm_font_width(text)) * s,
                py + (20 + i * 19) * s, s, color, 1);
        }
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
    } else if (r->container == 3) {
        gm_font_draw(fb, "Chest", px + 8 * s, py + 6 * s, s, 0x404040u, 0);
        /* GuiChest: upper inv label at ySize - 96 + 2 */
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (CHEST_YSIZE - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 4) {
        const char *title = "Brewing Stand";
        gm_font_draw(
            fb, title,
            px + (PANEL_W / 2 - gm_font_width(title) / 2) * s,
            py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 5) {
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (PANEL_H - 96 + 2) * s,
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
        if (sl->slot_id == hover
                && (sl->slot_id < GMC_ENCHANT_BUTTON0
                    || sl->slot_id >= GMC_ENCHANT_BUTTON0 + 3))
            gm_hud_fill(fb, sl->x, sl->y, sl->w, sl->h, highlight);
    }

    /* cursor stack rides the mouse, drawn last */
    ICStack cur;
    if (!gm_runtime_tape_gui_cursor_get(r, &cur)) cur = gm_player_cursor();
    if (cur.item > 0 && cur.count > 0)
        draw_stack(fb, cur, mx - 8 * s, my - 8 * s, s);
    else if (hover >= 0)
        draw_tooltip(fb, screen_item_name(screen_stack(r, hover).item), mx, my, s);
}
