/* game/screen.c - see screen.h. Slot coordinates are the vanilla 1.11.2 GUI
 * positions (net/minecraft/inventory/Container{Player,Workbench,Furnace,Chest}.java
 * Slot constructor x/y args) on the standard 176-wide panel. */
#include "game/screen.h"
#include "game/native_save.h"
#include "game/runtime.h"
#include "game/hud.h"
#include "game/player_preview.h"
#include "assets/inventory_ui_atlas.h"

#include <stdio.h>
#include <string.h>

#define PANEL_W 176
#define PANEL_H 166
/* GuiChest: ySize = 114 + inventoryRows*18 = 168 for a single chest (3 rows).
 * The generic_54 blit is only 167 tall (71+96); Java still centers with ySize. */
#define CHEST_YSIZE 168
#define CHEST_TEX_H 167
#define LARGE_CHEST_YSIZE 222
#define LARGE_CHEST_TEX_H 221
#define SHULKER_YSIZE 167
#define HOPPER_YSIZE 133
#define BEACON_XSIZE 230
#define BEACON_YSIZE 219
#define CELL    16
#define PITCH   18

static int gui_scale(int fb_h) { int s = fb_h / 240; return s > 1 ? s : 1; }

/* Layout / hit-test height: GuiContainer uses ySize for guiTop and bounds. */
static int panel_h(int container) {
    return container == 3 ? CHEST_YSIZE
         : container == 10 ? LARGE_CHEST_YSIZE
         : container == 9 ? SHULKER_YSIZE
         : container == 14 ? HOPPER_YSIZE
         : container == 11 ? BEACON_YSIZE : PANEL_H;
}

static int panel_w(int container) {
    return container == 11 ? BEACON_XSIZE : PANEL_W;
}

static int chest_slot_id(int index)
{
    return index < GMC_CHEST_SLOTS
        ? GMC_CHEST0 + index
        : GMC_CHEST_EXTRA0 + index - GMC_CHEST_SLOTS;
}

static int chest_slot_index(int id)
{
    if (id >= GMC_CHEST0 && id < GMC_CHEST0 + GMC_CHEST_SLOTS)
        return id - GMC_CHEST0;
    if (id >= GMC_CHEST_EXTRA0
            && id < GMC_CHEST_EXTRA0 + GMC_CHEST_SLOTS)
        return GMC_CHEST_SLOTS + id - GMC_CHEST_EXTRA0;
    return -1;
}

int gm_screen_gui_scale(int fb_h) { return gui_scale(fb_h); }

int gm_screen_kind_for_gui(const char *gui_name)
{
    if (!gui_name || !gui_name[0]) return -1;
    if (!strcmp(gui_name, "GuiInventory")) return 0;
    if (!strcmp(gui_name, "GuiCrafting"))  return 1;
    if (!strcmp(gui_name, "GuiFurnace"))   return 2;
    if (!strcmp(gui_name, "GuiChest"))     return 3;
    if (!strcmp(gui_name, "GuiLargeChest")) return 10;
    if (!strcmp(gui_name, "GuiBrewingStand")) return 4;
    if (!strcmp(gui_name, "GuiEnchantment")) return 5;
    if (!strcmp(gui_name, "GuiRepair")) return 6;
    if (!strcmp(gui_name, "GuiMerchant")) return 7;
    if (!strcmp(gui_name, "GuiScreenHorseInventory")) return 8;
    if (!strcmp(gui_name, "GuiShulkerBox")) return 9;
    if (!strcmp(gui_name, "GuiBeacon")) return 11;
    if (!strcmp(gui_name, "GuiDispenser")) return 13;
    if (!strcmp(gui_name, "GuiHopper")) return 14;
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
static void panel_origin_wh(
        int fb_w, int fb_h, int s, int pw, int ph, int *px, int *py)
{
    int gw = (fb_w + s - 1) / s, gh = (fb_h + s - 1) / s;
    *px = (gw - pw) / 2 * s;
    *py = (gh - ph) / 2 * s;
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
    if (container == 12) return 0;
    const int s  = gui_scale(fb_h);
    const int pw = panel_w(container);
    const int ph = panel_h(container);
    int px, py;
    panel_origin_wh(fb_w, fb_h, s, pw, ph, &px, &py);
    int n = 0;

    /* ContainerChest: numRows=3, i=(3-4)*18=-18 -> main y=85, hotbar y=143.
     * Other containers share main y=84 / hotbar y=142. */
    int main_x = container == 11 ? 36 : 8;
    int main_y = container == 11 ? 137
               : container == 14 ? 51
               : container == 10 ? 139 : container == 3 ? 85 : 84;
    int hot_y  = container == 11 ? 195
               : container == 14 ? 109
               : container == 10 ? 197 : container == 3 ? 143 : 142;
    for (int i = 0; i < 27; ++i)
        n = put(out, n, max, 9 + i, px, py, s,
                main_x + (i % 9) * PITCH, main_y + (i / 9) * PITCH);
    for (int i = 0; i < 9; ++i)
        n = put(out, n, max, i, px, py, s, main_x + i * PITCH, hot_y);

    if (container == 1) {
        for (int i = 0; i < 9; ++i)
            n = put(out, n, max, GMC_GRID0 + i, px, py, s,
                    30 + (i % 3) * PITCH, 17 + (i / 3) * PITCH);
        n = put(out, n, max, GMC_RESULT, px, py, s, 124, 35);
    } else if (container == 2) {
        n = put(out, n, max, GMC_FURNACE0,     px, py, s, 56, 17);
        n = put(out, n, max, GMC_FURNACE0 + 1, px, py, s, 56, 53);
        n = put(out, n, max, GMC_FURNACE0 + 2, px, py, s, 116, 35);
    } else if (container == 3 || container == 9 || container == 10) {
        /* ContainerChest: 3 or 6 rows at (8, 18 + row*18). */
        int slots = container == 10
            ? GMC_LARGE_CHEST_SLOTS : GMC_CHEST_SLOTS;
        for (int i = 0; i < slots; ++i)
            n = put(out, n, max, chest_slot_id(i), px, py, s,
                    8 + (i % 9) * PITCH, 18 + (i / 9) * PITCH);
    } else if (container == 13) {
        for (int i = 0; i < 9; ++i)
            n = put(out, n, max, chest_slot_id(i), px, py, s,
                    62 + (i % 3) * PITCH, 17 + (i / 3) * PITCH);
    } else if (container == 14) {
        for (int i = 0; i < 5; ++i)
            n = put(out, n, max, chest_slot_id(i), px, py, s,
                    44 + i * PITCH, 20);
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
    } else if (container == 6) {
        n = put(out, n, max, GMC_ANVIL0, px, py, s, 27, 47);
        n = put(out, n, max, GMC_ANVIL0 + 1, px, py, s, 76, 47);
        n = put(out, n, max, GMC_ANVIL0 + 2, px, py, s, 134, 47);
    } else if (container == 7) {
        n = put(out, n, max, GMC_MERCHANT0, px, py, s, 36, 53);
        n = put(out, n, max, GMC_MERCHANT0 + 1, px, py, s, 62, 53);
        n = put(out, n, max, GMC_MERCHANT0 + 2, px, py, s, 120, 53);
        if (n < max) {
            out[n] = (GmScreenSlot){GMC_MERCHANT_PREV,
                px + 17 * s, py + 23 * s, 12 * s, 19 * s};
            ++n;
        }
        if (n < max) {
            out[n] = (GmScreenSlot){GMC_MERCHANT_NEXT,
                px + 147 * s, py + 23 * s, 12 * s, 19 * s};
            ++n;
        }
    } else if (container == 8) {
        n = put(out, n, max, GMC_HORSE0, px, py, s, 8, 18);
        n = put(out, n, max, GMC_HORSE0 + 1, px, py, s, 8, 36);
        for (int i = 0; i < 15; ++i)
            n = put(out, n, max, GMC_HORSE0 + 2 + i, px, py, s,
                    80 + (i % 5) * PITCH, 18 + (i / 5) * PITCH);
    } else if (container == 11) {
        static const int button_x[GMC_BEACON_POWER_COUNT] = {
            53, 77, 53, 77, 65, 144, 168
        };
        static const int button_y[GMC_BEACON_POWER_COUNT] = {
            22, 22, 47, 47, 72, 47, 47
        };
        n = put(out, n, max, GMC_BEACON0, px, py, s, 136, 110);
        if (n < max)
            out[n++] = (GmScreenSlot){GMC_BEACON_CONFIRM,
                px + 164 * s, py + 107 * s, 22 * s, 22 * s};
        if (n < max)
            out[n++] = (GmScreenSlot){GMC_BEACON_CANCEL,
                px + 190 * s, py + 107 * s, 22 * s, 22 * s};
        for (int i = 0; i < GMC_BEACON_POWER_COUNT && n < max; ++i)
            out[n++] = (GmScreenSlot){GMC_BEACON_POWER0 + i,
                px + button_x[i] * s, py + button_y[i] * s,
                22 * s, 22 * s};
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
        n = put(out, n, max, GMC_OFFHAND, px, py, s, 77, 62);
    }
    return n;
}

int gm_screen_slot_at(int container, int fb_w, int fb_h, int mx, int my)
{
    if (container == 12) return -1;
    const int s  = gui_scale(fb_h);
    const int pw = panel_w(container);
    const int ph = panel_h(container);
    int px, py;
    panel_origin_wh(fb_w, fb_h, s, pw, ph, &px, &py);
    if (mx < px || my < py || mx >= px + pw * s || my >= py + ph * s)
        return GMC_OUTSIDE;

    GmScreenSlot slots[GMC_SLOT_COUNT];
    int n = gm_screen_layout(container, fb_w, fb_h, slots, GMC_SLOT_COUNT);
    for (int i = 0; i < n; ++i)
        /* GuiContainer.isPointInRegion grows each 16x16 Slot by one GUI
         * pixel on every side. Adjacent slots are pitched by 18, so these
         * hit regions meet without overlap. */
        if (slots[i].slot_id >= GMC_ENCHANT_BUTTON0
                && (slots[i].slot_id < GMC_ANVIL0
                    || slots[i].slot_id == GMC_MERCHANT_PREV
                    || slots[i].slot_id == GMC_MERCHANT_NEXT
                    || (slots[i].slot_id >= GMC_BEACON_CONFIRM
                        && slots[i].slot_id < GMC_OFFHAND))) {
            if (mx >= slots[i].x && mx < slots[i].x + slots[i].w
                    && my >= slots[i].y && my < slots[i].y + slots[i].h)
                return slots[i].slot_id;
        } else if (mx >= slots[i].x - s
                && mx < slots[i].x + slots[i].w + s
                && my >= slots[i].y - s
                && my < slots[i].y + slots[i].h + s)
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
    if (id == GMC_OFFHAND)
        return isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
    if (id >= GMC_GRID0 && id < GMC_RESULT) return r->craft_grid[id - GMC_GRID0];
    if (id == GMC_RESULT) return gm_container_result(r);
    if (r->container == 2 && r->active_furnace >= 0) {
        const FurnaceLive *f = &r->furnaces[r->active_furnace].state;
        return furnace_live_get_ic(f, id - GMC_FURNACE0);
    }
    if ((r->container == 3 || r->container == 9 || r->container == 10)
            && chest_slot_index(id) >= 0) {
        int slot = chest_slot_index(id);
        if (r->active_chest == GM_ACTIVE_ENDER_CHEST)
            return chest_live_get(
                &r->ender_chest_inventory, slot);
        if (r->active_chest >= 0) {
            int chest_index = slot < GMC_CHEST_SLOTS
                ? r->active_chest : r->active_chest_pair;
            if (chest_index < 0 || chest_index >= r->chests_cap)
                return (ICStack){0};
            return chest_live_get(
                &r->chests[chest_index].state, slot % GMC_CHEST_SLOTS);
        }
        if (r->active_chest == GM_ACTIVE_SHULKER_BOX
                && r->active_static_container >= 0
                && r->active_static_container < r->static_containers_cap) {
            const GmRuntimeStaticContainer *box =
                &r->static_containers[r->active_static_container];
            if (box->active && box->block >= 219 && box->block <= 234)
                return slot < GMC_CHEST_SLOTS
                    ? box->slots[slot] : (ICStack){0};
        }
    }
    if ((r->container == 13 || r->container == 14)
            && chest_slot_index(id) >= 0
            && r->active_static_container >= 0
            && r->active_static_container < r->static_containers_cap) {
        int slot = chest_slot_index(id);
        const GmRuntimeStaticContainer *inventory =
            &r->static_containers[r->active_static_container];
        int size = r->container == 13 ? 9 : 5;
        int valid_block = r->container == 13
            ? inventory->block == 23 || inventory->block == 158
            : inventory->block == 154;
        if (inventory->active && valid_block && slot < size)
            return inventory->slots[slot];
    }
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
    if (r->container == 6 && r->anvil.open
            && id >= GMC_ANVIL0
            && id < GMC_ANVIL0 + GMC_ANVIL_SLOTS)
        return r->anvil.slots[id - GMC_ANVIL0];
    if (r->container == 7 && r->active_villager_eid > 0
            && id >= GMC_MERCHANT0
            && id < GMC_MERCHANT0 + GMC_MERCHANT_SLOTS)
        return r->merchant_slots[id - GMC_MERCHANT0];
    if (r->container == 8 && r->active_horse_eid > 0
            && id >= GMC_HORSE0
            && id < GMC_HORSE0 + GMC_HORSE_SLOTS) {
        GmHorseState horse;
        int index = id - GMC_HORSE0;
        int size = 0;
        if (gm_mobs_get_horse_state(
                &r->mobs, r->active_horse_eid, &horse)) {
            size = horse.chested ? 17 : 2;
            if (horse.type == EW_TYPE_LLAMA) {
                GmLlamaState llama;
                if (!gm_mobs_get_llama_state(
                        &r->mobs, r->active_horse_eid, &llama))
                    return ic_empty();
                size = horse.chested ? 2 + 3 * llama.strength : 2;
            }
        }
        if (index < size)
            return horse.inventory[index];
    }
    if (r->container == 11 && id == GMC_BEACON0
            && r->active_static_container >= 0
            && r->active_static_container < r->static_containers_cap) {
        const GmRuntimeStaticContainer *beacon =
            &r->static_containers[r->active_static_container];
        if (beacon->active && beacon->block == 138)
            return beacon->slots[0];
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

static const char *const enchant_name_parts[] = {
    "the", "elder", "scrolls", "klaatu", "berata", "niktu", "xyzzy",
    "bless", "curse", "light", "darkness", "fire", "air", "earth",
    "water", "hot", "dry", "cold", "wet", "ignite", "snuff",
    "embiggen", "twist", "shorten", "stretch", "fiddle", "destroy",
    "imbue", "galvanize", "enchant", "free", "limited", "range", "of",
    "towards", "inside", "sphere", "cube", "self", "other", "ball",
    "mental", "physical", "grow", "shrink", "demon", "elemental",
    "spirit", "animal", "creature", "beast", "humanoid", "undead",
    "fresh", "stale", "phnglui", "mglwnafh", "cthulhu", "rlyeh",
    "wgahnagl", "fhtagnbaguette"
};

/* FontRenderer.sizeStringToWidth for plain ASCII. Returns the split index;
 * callers omit the space at that index before continuing. */
static int enchant_wrap_index(const char *text, int max_width)
{
    int width = 0, last_space = -1;
    for (int i = 0; text[i]; ++i) {
        if (text[i] == ' ') last_space = i;
        width += gm_sga_font_width((char[2]){text[i], 0});
        if (width > max_width)
            return last_space >= 0 && last_space < i ? last_space : i;
    }
    return (int)strlen(text);
}

static void enchant_random_name(JavaRandom *random, int max_width,
                                char out[128])
{
    int words = jrand_int_bound(random, 2) + 3;
    int len = 0;
    const int parts = (int)(sizeof enchant_name_parts
        / sizeof enchant_name_parts[0]);
    for (int i = 0; i < words; ++i) {
        const char *part = enchant_name_parts[jrand_int_bound(random, parts)];
        int n = (int)strlen(part);
        if (i > 0 && len < 127) out[len++] = ' ';
        if (len + n > 127) n = 127 - len;
        memcpy(out + len, part, (size_t)n);
        len += n;
    }
    out[len] = 0;

    /* EnchantmentNameParts keeps at most the first two wrapped lines and
     * joins them with one space. The source vocabulary fits this buffer. */
    int first = enchant_wrap_index(out, max_width);
    if (!out[first]) return;
    const char *rest = out + first + (out[first] == ' ');
    int second = enchant_wrap_index(rest, max_width);
    if (rest[second]) {
        int keep = (int)(rest - out) + second;
        out[keep] = 0;
    }
}

static void draw_enchant_name(CrFramebuffer *fb, const char *text,
                              int x, int y, int max_width, int s,
                              unsigned color, int max_rows)
{
    char line[128];
    const char *at = text;
    for (int row = 0; *at && row < max_rows; ++row) {
        int n = enchant_wrap_index(at, max_width);
        if (n > 127) n = 127;
        memcpy(line, at, (size_t)n);
        line[n] = 0;
        gm_sga_font_draw(fb, line, x, y + row * 9 * s, s, color);
        if (!at[n]) break;
        at += n + (at[n] == ' ');
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
    if (offhand.item <= 0 || offhand.count <= 0)
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

static const GmRuntimeStaticContainer *screen_beacon(const GmRuntime *r)
{
    const GmRuntimeStaticContainer *beacon;
    if (!r || r->container != 11 || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return NULL;
    beacon = &r->static_containers[r->active_static_container];
    return beacon->active && beacon->block == 138 ? beacon : NULL;
}

static int beacon_effect_sprite(int effect)
{
    switch (effect) {
    case 1: return GM_GUI_BEACON_SPEED;
    case 3: return GM_GUI_BEACON_HASTE;
    case 5: return GM_GUI_BEACON_STRENGTH;
    case 8: return GM_GUI_BEACON_JUMP;
    case 10: return GM_GUI_BEACON_REGENERATION;
    case 11: return GM_GUI_BEACON_RESISTANCE;
    default: return -1;
    }
}

static const char *beacon_effect_name(int effect)
{
    switch (effect) {
    case 1: return "Speed";
    case 3: return "Haste";
    case 5: return "Strength";
    case 8: return "Jump Boost";
    case 10: return "Regeneration";
    case 11: return "Resistance";
    default: return NULL;
    }
}

static void draw_beacon_button(
        CrFramebuffer *fb, int x, int y, int s, int icon,
        int enabled, int selected, int hovered)
{
    int sprite = !enabled ? GM_GUI_BEACON_BUTTON_DISABLED
               : selected ? GM_GUI_BEACON_BUTTON_SELECTED
               : hovered ? GM_GUI_BEACON_BUTTON_HOVER
                         : GM_GUI_BEACON_BUTTON_NORMAL;
    gm_gui_blit(fb, sprite, x, y, s);
    if (icon >= 0) gm_gui_blit(fb, icon, x + 2 * s, y + 2 * s, s);
}

static void draw_beacon_controls(
        CrFramebuffer *fb, const GmRuntime *r,
        int px, int py, int s, int mx, int my)
{
    static const int effects[6] = {1, 3, 11, 8, 5, 10};
    static const int required[6] = {1, 1, 2, 2, 3, 4};
    static const int bx[7] = {53, 77, 53, 77, 65, 144, 168};
    static const int by[7] = {22, 22, 47, 47, 72, 47, 47};
    const GmRuntimeStaticContainer *beacon = screen_beacon(r);
    int primary = beacon ? beacon->beacon_primary : 0;
    int secondary = beacon ? beacon->beacon_secondary : 0;
    int levels = beacon ? beacon->beacon_levels : -1;
    int payment = beacon && !isr_is_empty(&beacon->slots[0]);
    int hover = gm_screen_slot_at(11, fb->w, fb->h, mx, my);

    draw_beacon_button(fb, px + 164 * s, py + 107 * s, s,
        GM_GUI_BEACON_CONFIRM, payment && beacon_effect_name(primary),
        0, hover == GMC_BEACON_CONFIRM);
    draw_beacon_button(fb, px + 190 * s, py + 107 * s, s,
        GM_GUI_BEACON_CANCEL, 1, 0, hover == GMC_BEACON_CANCEL);
    if (levels < 0) return;
    for (int index = 0; index < 6; ++index) {
        int selected = index < 5
            ? primary == effects[index] : secondary == effects[index];
        draw_beacon_button(
            fb, px + bx[index] * s, py + by[index] * s, s,
            beacon_effect_sprite(effects[index]),
            levels >= required[index], selected,
            hover == GMC_BEACON_POWER0 + index);
    }
    if (beacon_effect_name(primary))
        draw_beacon_button(
            fb, px + bx[6] * s, py + by[6] * s, s,
            beacon_effect_sprite(primary), levels >= 4,
            secondary == primary, hover == GMC_BEACON_POWER0 + 6);
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

static int structure_point_inside(
        int mx, int my, int x, int y, int w, int h) {
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

static int structure_button_rect(
        const GmRuntimeStructureGui *gui, int fb_w, int fb_h, int button,
        int *x, int *y, int *w, int *h, int *enabled) {
    int s = gui_scale(fb_h);
    int gw = (fb_w + s - 1) / s;
    int cx = gw / 2;
    int gx = 0, gy = 0, gwidth = 0;
    int visible = gui && gui->active;
    int active = 1;
    if (!visible) return 0;
    switch (button) {
    case 0: gx = cx - 154; gy = 210; gwidth = 150; break;
    case 1: gx = cx + 4; gy = 210; gwidth = 150; break;
    case 9:
        visible = gui->value.mode == GM_STRUCTURE_MODE_SAVE;
        gx = cx + 104; gy = 185; gwidth = 50; break;
    case 10:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        gx = cx + 104; gy = 185; gwidth = 50; break;
    case 11:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        active = gui->value.rotation != GM_STRUCTURE_ROTATION_NONE;
        gx = cx - 102; gy = 185; gwidth = 40; break;
    case 12:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        active = gui->value.rotation != GM_STRUCTURE_ROTATION_CW90;
        gx = cx - 61; gy = 185; gwidth = 40; break;
    case 13:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        active = gui->value.rotation != GM_STRUCTURE_ROTATION_CW180;
        gx = cx + 21; gy = 185; gwidth = 40; break;
    case 14:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        active = gui->value.rotation != GM_STRUCTURE_ROTATION_CCW90;
        gx = cx + 62; gy = 185; gwidth = 40; break;
    case 18: gx = cx - 154; gy = 185; gwidth = 50; break;
    case 19:
        visible = gui->value.mode == GM_STRUCTURE_MODE_SAVE;
        gx = cx + 104; gy = 120; gwidth = 50; break;
    case 20:
        visible = gui->value.mode == GM_STRUCTURE_MODE_SAVE
            || gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        gx = cx + 104; gy = 160; gwidth = 50; break;
    case 21:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        gx = cx - 20; gy = 185; gwidth = 40; break;
    case 22:
        visible = gui->value.mode == GM_STRUCTURE_MODE_SAVE;
        gx = cx + 104; gy = 80; gwidth = 50; break;
    case 23:
        visible = gui->value.mode == GM_STRUCTURE_MODE_LOAD;
        gx = cx + 104; gy = 80; gwidth = 50; break;
    default:
        return 0;
    }
    if (!visible) return 0;
    if (x) *x = gx * s;
    if (y) *y = gy * s;
    if (w) *w = gwidth * s;
    if (h) *h = 20 * s;
    if (enabled) *enabled = active;
    return 1;
}

static int structure_field_rect(
        const GmRuntimeStructureGui *gui, int fb_w, int fb_h, int field,
        int *x, int *y, int *w, int *h) {
    int s = gui_scale(fb_h);
    int gw = (fb_w + s - 1) / s;
    int cx = gw / 2;
    int gx = 0, gy = 0, gwidth = 80;
    int mode;
    if (!gui || !gui->active) return 0;
    mode = gui->value.mode;
    switch (field) {
    case GM_STRUCTURE_GUI_NAME:
        if (mode == GM_STRUCTURE_MODE_DATA) return 0;
        gx = cx - 152; gy = 40; gwidth = 300; break;
    case GM_STRUCTURE_GUI_POS_X:
    case GM_STRUCTURE_GUI_POS_Y:
    case GM_STRUCTURE_GUI_POS_Z:
        if (mode != GM_STRUCTURE_MODE_SAVE
                && mode != GM_STRUCTURE_MODE_LOAD) return 0;
        gx = cx - 152 + (field - GM_STRUCTURE_GUI_POS_X) * 80;
        gy = 80; break;
    case GM_STRUCTURE_GUI_SIZE_X:
    case GM_STRUCTURE_GUI_SIZE_Y:
    case GM_STRUCTURE_GUI_SIZE_Z:
        if (mode != GM_STRUCTURE_MODE_SAVE) return 0;
        gx = cx - 152 + (field - GM_STRUCTURE_GUI_SIZE_X) * 80;
        gy = 120; break;
    case GM_STRUCTURE_GUI_INTEGRITY:
        if (mode != GM_STRUCTURE_MODE_LOAD) return 0;
        gx = cx - 152; gy = 120; break;
    case GM_STRUCTURE_GUI_SEED:
        if (mode != GM_STRUCTURE_MODE_LOAD) return 0;
        gx = cx - 72; gy = 120; break;
    case GM_STRUCTURE_GUI_METADATA:
        if (mode != GM_STRUCTURE_MODE_DATA) return 0;
        gx = cx - 152; gy = 120; gwidth = 240; break;
    default:
        return 0;
    }
    if (x) *x = gx * s;
    if (y) *y = gy * s;
    if (w) *w = gwidth * s;
    if (h) *h = 20 * s;
    return 1;
}

int gm_screen_structure_button_at(
        const struct GmRuntime *r, int fb_w, int fb_h, int mx, int my) {
    static const int buttons[] = {
        0, 1, 9, 10, 11, 12, 13, 14, 18, 19, 20, 21, 22, 23
    };
    if (!r || r->container != 12) return -1;
    for (unsigned index = 0; index < sizeof buttons / sizeof buttons[0];
            ++index) {
        int x, y, w, h, enabled;
        if (structure_button_rect(
                &r->structure_gui, fb_w, fb_h, buttons[index],
                &x, &y, &w, &h, &enabled)
                && enabled && structure_point_inside(mx, my, x, y, w, h))
            return buttons[index];
    }
    return -1;
}

int gm_screen_structure_field_at(
        const struct GmRuntime *r, int fb_w, int fb_h, int mx, int my) {
    if (!r || r->container != 12) return -1;
    for (int field = 0; field < GM_STRUCTURE_GUI_FIELD_COUNT; ++field) {
        int x, y, w, h;
        if (structure_field_rect(
                &r->structure_gui, fb_w, fb_h, field, &x, &y, &w, &h)
                && structure_point_inside(mx, my, x, y, w, h))
            return field;
    }
    return -1;
}

static const char *structure_field_text(
        const GmRuntimeStructureGui *gui, int field, int *max_length) {
    if (max_length) *max_length = 15;
    switch (field) {
    case GM_STRUCTURE_GUI_NAME:
        if (max_length) *max_length = 64;
        return gui->value.name;
    case GM_STRUCTURE_GUI_POS_X: return gui->pos_x;
    case GM_STRUCTURE_GUI_POS_Y: return gui->pos_y;
    case GM_STRUCTURE_GUI_POS_Z: return gui->pos_z;
    case GM_STRUCTURE_GUI_SIZE_X: return gui->size_x;
    case GM_STRUCTURE_GUI_SIZE_Y: return gui->size_y;
    case GM_STRUCTURE_GUI_SIZE_Z: return gui->size_z;
    case GM_STRUCTURE_GUI_INTEGRITY: return gui->integrity;
    case GM_STRUCTURE_GUI_SEED:
        if (max_length) *max_length = 31;
        return gui->seed;
    case GM_STRUCTURE_GUI_METADATA:
        if (max_length) *max_length = 128;
        return gui->value.metadata;
    default: return "";
    }
}

static void draw_structure_textbox(
        CrFramebuffer *fb, const GmRuntime *r, int field) {
    const GmRuntimeStructureGui *gui = &r->structure_gui;
    const char *text;
    char visible[GM_STRUCTURE_METADATA_LENGTH];
    int x, y, w, h, s = gui_scale(fb->h), max_length;
    size_t length, start = 0;
    if (!structure_field_rect(gui, fb->w, fb->h, field, &x, &y, &w, &h))
        return;
    text = structure_field_text(gui, field, &max_length);
    length = strlen(text);
    while (start < length
            && gm_font_width(text + start) * s > w - 8 * s)
        ++start;
    snprintf(visible, sizeof visible, "%s", text + start);
    gm_hud_fill(fb, x - s, y - s, w + 2 * s, h + 2 * s,
                (CrRgba){160,160,160,255});
    gm_hud_fill(fb, x, y, w, h, (CrRgba){0,0,0,255});
    gm_font_draw(fb, visible, x + 4 * s, y + 6 * s,
                 s, 0xe0e0e0u, 1);
    if (gui->focus == field && (r->tick / 6) % 2 == 0) {
        /* drawStringWithShadow returns max(shadow end, normal end), which is
         * one GUI pixel beyond the ordinary string advance. GuiTextField uses
         * that returned x for its underscore cursor. */
        int cursor_x = x + 4 * s + (gm_font_width(visible) + 1) * s;
        if ((int)length >= max_length) cursor_x -= s;
        gm_font_draw(fb, "_", cursor_x, y + 6 * s,
                     s, 0xe0e0e0u, 1);
    }
}

static const char *structure_button_label(
        const GmRuntimeStructureGui *gui, int button) {
    static const char *modes[4] = {"[S]", "[L]", "[C]", "[D]"};
    switch (button) {
    case 0: return "Done";
    case 1: return "Cancel";
    case 9: return "SAVE";
    case 10: return "LOAD";
    case 11: return "0";
    case 12: return "90";
    case 13: return "180";
    case 14: return "270";
    case 18: return modes[gui->value.mode];
    case 19: return "DETECT";
    case 20: return gui->value.ignore_entities ? "OFF" : "ON";
    case 21:
        return gui->value.mirror == GM_STRUCTURE_MIRROR_LEFT_RIGHT ? "< >"
            : gui->value.mirror == GM_STRUCTURE_MIRROR_FRONT_BACK ? "^ v" : "|";
    case 22: return gui->value.show_air ? "ON" : "OFF";
    case 23: return gui->value.show_bounding_box ? "ON" : "OFF";
    default: return "";
    }
}

static void draw_structure_screen(
        CrFramebuffer *fb, const GmRuntime *r, int mx, int my) {
    static const int buttons[] = {
        0, 1, 9, 10, 11, 12, 13, 14, 18, 19, 20, 21, 22, 23
    };
    static const char *mode_info[4] = {
        "Save mode - write to file",
        "Load mode - load from file",
        "Corner mode - placement and size marker",
        "Data mode - game logic marker"
    };
    const GmRuntimeStructureGui *gui = &r->structure_gui;
    int s = gui_scale(fb->h), gw = (fb->w + s - 1) / s;
    int cx = gw / 2, mode;
    if (!gui->active) return;
    mode = gui->value.mode;
    gm_font_draw(fb, "Structure Block",
        (cx - gm_font_width("Structure Block") / 2) * s,
        10 * s, s, 0xffffffu, 1);
    if (mode != GM_STRUCTURE_MODE_DATA) {
        gm_font_draw(fb, "Structure Name", (cx - 153) * s, 30 * s,
                     s, 0xa0a0a0u, 0);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_NAME);
    }
    if (mode == GM_STRUCTURE_MODE_LOAD || mode == GM_STRUCTURE_MODE_SAVE) {
        const char *include = "Include entities:";
        gm_font_draw(fb, "Relative Position", (cx - 153) * s, 70 * s,
                     s, 0xa0a0a0u, 0);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_POS_X);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_POS_Y);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_POS_Z);
        gm_font_draw(fb, include,
            (cx + 154 - gm_font_width(include)) * s, 150 * s,
            s, 0xa0a0a0u, 0);
    }
    if (mode == GM_STRUCTURE_MODE_SAVE) {
        const char *detect = "Detect structure size and position:";
        const char *air = "Show invisible blocks:";
        gm_font_draw(fb, "Structure Size", (cx - 153) * s, 110 * s,
                     s, 0xa0a0a0u, 0);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_SIZE_X);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_SIZE_Y);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_SIZE_Z);
        gm_font_draw(fb, detect,
            (cx + 154 - gm_font_width(detect)) * s, 110 * s,
            s, 0xa0a0a0u, 0);
        gm_font_draw(fb, air,
            (cx + 154 - gm_font_width(air)) * s, 70 * s,
            s, 0xa0a0a0u, 0);
    } else if (mode == GM_STRUCTURE_MODE_LOAD) {
        const char *bounds = "Show bounding box:";
        gm_font_draw(fb, "Structure Integrity and Seed",
                     (cx - 153) * s, 110 * s, s, 0xa0a0a0u, 0);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_INTEGRITY);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_SEED);
        gm_font_draw(fb, bounds,
            (cx + 154 - gm_font_width(bounds)) * s, 70 * s,
            s, 0xa0a0a0u, 0);
    } else if (mode == GM_STRUCTURE_MODE_DATA) {
        gm_font_draw(fb, "Custom Data Tag Name",
                     (cx - 153) * s, 110 * s, s, 0xa0a0a0u, 0);
        draw_structure_textbox(fb, r, GM_STRUCTURE_GUI_METADATA);
    }
    gm_font_draw(fb, mode_info[mode], (cx - 153) * s, 174 * s,
                 s, 0xa0a0a0u, 0);
    for (unsigned index = 0; index < sizeof buttons / sizeof buttons[0];
            ++index) {
        int x, y, w, h, enabled;
        if (!structure_button_rect(
                gui, fb->w, fb->h, buttons[index],
                &x, &y, &w, &h, &enabled))
            continue;
        gm_hud_gui_button_draw(
            fb, x, y, w, h, s,
            structure_button_label(gui, buttons[index]),
            structure_point_inside(mx, my, x, y, w, h), enabled);
    }
}

void gm_screen_draw(CrFramebuffer *fb, const struct GmRuntime *r, int mx, int my)
{
    if (!fb || !fb->color || !r) return;
    const int s  = gui_scale(fb->h);
    const int pw = panel_w(r->container);
    const int ph = panel_h(r->container);
    int px, py;
    panel_origin_wh(fb->w, fb->h, s, pw, ph, &px, &py);
    const CrRgba highlight = {255, 255, 255, 128}; /* vanilla hovered-slot overlay */

    draw_background_dim(fb);
    if (r->container == 12) {
        draw_structure_screen(fb, r, mx, my);
        return;
    }

    /* real MC panel art (drawGuiContainerBackgroundLayer) */
    int panel = r->container == 1 ? GM_GUI_TABLE_PANEL
              : r->container == 2 ? GM_GUI_FURNACE_PANEL
              : r->container == 3 ? GM_GUI_CHEST_PANEL
              : r->container == 10 ? GM_GUI_LARGE_CHEST_PANEL
              : r->container == 4 ? GM_GUI_BREWING_PANEL
              : r->container == 5 ? GM_GUI_ENCHANTING_PANEL
              : r->container == 6 ? GM_GUI_ANVIL_PANEL
              : r->container == 7 ? GM_GUI_MERCHANT_PANEL
              : r->container == 8 ? GM_GUI_HORSE_PANEL
              : r->container == 9 ? GM_GUI_SHULKER_PANEL
              : r->container == 11 ? GM_GUI_BEACON_PANEL
              : r->container == 13 ? GM_GUI_DISPENSER_PANEL
              : r->container == 14 ? GM_GUI_HOPPER_PANEL
                                  : GM_GUI_INV_PANEL;
    gm_gui_blit(fb, panel, px, py, s);
    if (r->container == 6) {
        int have_left = r->anvil.slots[0].item > 0
            && r->anvil.slots[0].count > 0;
        int have_right = r->anvil.slots[1].item > 0
            && r->anvil.slots[1].count > 0;
        int have_result = r->anvil.slots[2].item > 0
            && r->anvil.slots[2].count > 0;
        gm_gui_blit(
            fb, have_left ? GM_GUI_ANVIL_NAME_ACTIVE
                          : GM_GUI_ANVIL_NAME_INACTIVE,
            px + 59 * s, py + 20 * s, s);
        if ((have_left || have_right) && !have_result)
            gm_gui_blit(
                fb, GM_GUI_ANVIL_INVALID, px + 99 * s, py + 45 * s, s);
    }
    if (r->container == 11) {
        draw_stack(fb, ic_mk(388, 1, 0), px + 42 * s, py + 109 * s, s);
        draw_stack(fb, ic_mk(264, 1, 0), px + 64 * s, py + 109 * s, s);
        draw_stack(fb, ic_mk(266, 1, 0), px + 86 * s, py + 109 * s, s);
        draw_stack(fb, ic_mk(265, 1, 0), px + 108 * s, py + 109 * s, s);
        draw_beacon_controls(fb, r, px, py, s, mx, my);
    }
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
        int any_offer = r->enchanting.offer.levels[0] > 0
            || r->enchanting.offer.levels[1] > 0
            || r->enchanting.offer.levels[2] > 0;
        gm_enchant_book_draw(fb, any_offer ? 1.0f : 0.0f, 0.0f);
        JavaRandom names;
        jrand_set(&names, (i64)r->enchanting.xp_seed);
        for (int i = 0; i < 3; ++i) {
            int level = r->enchanting.offer.levels[i];
            int x = px + 60 * s;
            int y = py + (14 + 19 * i) * s;
            if (level <= 0) {
                gm_gui_blit(
                    fb, GM_GUI_ENCHANTING_OPTION_DISABLED, x, y, s);
                continue;
            }
            char text[16];
            int n = level, len = 0;
            do { text[len++] = (char)('0' + n % 10); n /= 10; } while (n);
            for (int a = 0, b = len - 1; a < b; ++a, --b) {
                char t = text[a]; text[a] = text[b]; text[b] = t;
            }
            text[len] = 0;
            int name_width = 86 - gm_font_width(text);
            char name[128];
            enchant_random_name(&names, name_width, name);
            int enabled = (r->tape_creative
                    || (r->player_xp_level >= level
                        && r->enchanting.slots[1].count >= i + 1))
                && r->enchanting.offer.clue_id[i] != -1;
            int hovered = mx >= x && mx < x + 108 * s
                && my >= y && my < y + 19 * s;
            int background = !enabled ? GM_GUI_ENCHANTING_OPTION_DISABLED
                : hovered ? GM_GUI_ENCHANTING_OPTION_HOVER
                          : GM_GUI_ENCHANTING_OPTION_NORMAL;
            int icon = !enabled ? GM_GUI_ENCHANTING_ICON0_DISABLED + i
                                : GM_GUI_ENCHANTING_ICON0 + i;
            unsigned name_color = enabled
                ? 0x685E4Au : (0x685E4Au & 0xFEFEFEu) >> 1;
            unsigned level_color = enabled ? 0x80FF20u : 0x407F10u;
            gm_gui_blit(fb, background, x, y, s);
            gm_gui_blit(fb, icon, x + s, y + s, s);
            /* The real 1.11.2 framebuffer clips the third option at one SGA
             * row in both the live client and atomic re-render path, even
             * though FontRenderer reports a two-line layout for this seed. */
            draw_enchant_name(
                fb, name, x + 20 * s, y + 2 * s,
                name_width, s, name_color, i == 2 ? 1 : 2);
            gm_font_draw(
                fb, text,
                px + (166 - gm_font_width(text)) * s,
                py + (23 + i * 19) * s, s, level_color, 1);
        }
    }

    if (r->container == 6) {
        const char *name = gm_runtime_item_name(r, r->anvil.repaired_name);
        if (name)
            gm_font_draw(fb, name, px + 62 * s, py + 24 * s,
                         s, 0xFFFFFFu, 1);
        if (r->anvil.maximum_cost > 0
                && r->anvil.slots[2].item > 0
                && r->anvil.slots[2].count > 0) {
            char text[32];
            unsigned color = r->tape_creative
                    || r->player_xp_level >= r->anvil.maximum_cost
                ? 0x80FF20u : 0xFF6060u;
            if (r->anvil.maximum_cost >= 40 && !r->tape_creative) {
                memcpy(text, "Too Expensive!", 15);
            } else {
                int value = r->anvil.maximum_cost;
                char digits[12];
                int n = 0;
                do { digits[n++] = (char)('0' + value % 10); value /= 10; }
                while (value && n < 11);
                memcpy(text, "Enchantment Cost: ", 18);
                for (int i = 0; i < n; ++i)
                    text[18 + i] = digits[n - 1 - i];
                text[18 + n] = 0;
            }
            int x = px + (PANEL_W - 8 - gm_font_width(text)) * s;
            unsigned dark = (color & 0xFCFCFCu) >> 2;
            gm_font_draw(fb, text, x, py + 68 * s, s, dark, 0);
            gm_font_draw(fb, text, x + s, py + 67 * s, s, dark, 0);
            gm_font_draw(fb, text, x + s, py + 68 * s, s, dark, 0);
            gm_font_draw(fb, text, x, py + 67 * s, s, color, 0);
        }
    }

    if (r->container == 7
            && (r->active_villager_eid > 0 || r->tape_merchant_active)) {
        GmVillagerOffer offer;
        int selected = r->tape_merchant_active
            ? r->tape_merchant_selected : r->merchant_selected;
        int offer_count = r->tape_merchant_active
            ? r->tape_merchant_offer_count
            : gm_runtime_villager_offer_count(
                (GmRuntime *)r, r->active_villager_eid);
        int have_offer;
        int previous_enabled = selected > 0;
        int next_enabled = selected + 1 < offer_count;
        int prev_hover = mx >= px + 17 * s && mx < px + 29 * s
            && my >= py + 23 * s && my < py + 42 * s;
        int next_hover = mx >= px + 147 * s && mx < px + 159 * s
            && my >= py + 23 * s && my < py + 42 * s;
        int prev_sprite = !previous_enabled ? GM_GUI_MERCHANT_PREV_DISABLED
            : prev_hover ? GM_GUI_MERCHANT_PREV_HOVER
                         : GM_GUI_MERCHANT_PREV_NORMAL;
        int next_sprite = !next_enabled ? GM_GUI_MERCHANT_NEXT_DISABLED
            : next_hover ? GM_GUI_MERCHANT_NEXT_HOVER
                         : GM_GUI_MERCHANT_NEXT_NORMAL;
        gm_gui_blit(fb, prev_sprite, px + 17 * s, py + 23 * s, s);
        gm_gui_blit(fb, next_sprite, px + 147 * s, py + 23 * s, s);
        if (r->tape_merchant_active) {
            memset(&offer, 0, sizeof offer);
            offer.buy_a = r->tape_merchant_offer[0];
            offer.buy_b = r->tape_merchant_offer[1];
            offer.sell = r->tape_merchant_offer[2];
            offer.uses = r->tape_merchant_disabled;
            offer.max_uses = 1;
            have_offer = 1;
        } else {
            have_offer = gm_runtime_villager_offer_get(
                (GmRuntime *)r, r->active_villager_eid,
                selected, &offer);
        }
        if (have_offer) {
            draw_stack(fb, offer.buy_a, px + 36 * s, py + 24 * s, s);
            draw_stack(fb, offer.buy_b, px + 62 * s, py + 24 * s, s);
            draw_stack(fb, offer.sell, px + 120 * s, py + 24 * s, s);
            if (offer.uses >= offer.max_uses)
                gm_gui_blit(fb, GM_GUI_MERCHANT_DISABLED,
                            px + 83 * s, py + 21 * s, s);
        }
    }

    GmHorseState horse_screen;
    int have_horse_screen = r->container == 8
        && r->active_horse_eid > 0
        && gm_mobs_get_horse_state(
            &r->mobs, r->active_horse_eid, &horse_screen);
    int horse_screen_slots = 0;
    int horse_screen_columns = 5;
    if (have_horse_screen) {
        horse_screen_slots = horse_screen.chested ? 17 : 2;
        if (horse_screen.type == EW_TYPE_LLAMA) {
            GmLlamaState llama;
            if (gm_mobs_get_llama_state(
                    &r->mobs, r->active_horse_eid, &llama)) {
                horse_screen_columns = llama.strength;
                horse_screen_slots = horse_screen.chested
                    ? 2 + 3 * llama.strength : 2;
            }
        }
        if (horse_screen.chested)
            gm_gui_blit_sub(fb, GM_GUI_HORSE_CHEST,
                            0, 0, horse_screen_columns * 18, 54,
                            px + 79 * s, py + 17 * s, s);
        if (horse_screen.type != EW_TYPE_LLAMA)
            gm_gui_blit(fb, GM_GUI_HORSE_SADDLE,
                        px + 7 * s, py + 17 * s, s);
        if (horse_screen.type == EW_TYPE_HORSE)
            gm_gui_blit(fb, GM_GUI_HORSE_ARMOR,
                        px + 7 * s, py + 35 * s, s);
        else if (horse_screen.type == EW_TYPE_LLAMA)
            gm_gui_blit(fb, GM_GUI_HORSE_LLAMA_DECOR,
                        px + 7 * s, py + 35 * s, s);
        {
            int gw = (fb->w + s - 1) / s, gh = (fb->h + s - 1) / s;
            int guiLeft = (gw - PANEL_W) / 2, guiTop = (gh - PANEL_H) / 2;
            int gmx = mx * gw / fb->w, gmy = my * gh / fb->h;
            int preview_variant = horse_screen.variant;
            int preview_armor = horse_screen.armor;
            int flags = (horse_screen.growing_age < 0 ? 8 : 0)
                      | (horse_screen.chested ? 8192 : 0)
                      | ((horse_screen.status & GM_HORSE_SADDLED) ? 16384 : 0)
                      | (horse_screen.ridden ? 32768 : 0);
            if (horse_screen.type == EW_TYPE_LLAMA) {
                GmLlamaState llama;
                if (gm_mobs_get_llama_state(
                        &r->mobs, r->active_horse_eid, &llama)) {
                    preview_variant = llama.variant;
                    preview_armor = llama.decor + 1;
                }
            }
            gm_horse_preview_draw(
                fb, px + 24 * s, py + 7 * s, 52 * s, 58 * s,
                horse_screen.type, preview_variant, preview_armor,
                flags, (float)(guiLeft + 51 - gmx),
                (float)(guiTop + 25 - gmy));
        }
    }

    /* labels (drawGuiContainerForegroundLayer, color 4210752, no shadow) */
    if (r->container == 1) {
        gm_font_draw(fb, "Crafting", px + 28 * s, py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 2) {
        const char *title = "Furnace";
        if (r->active_furnace >= 0
                && r->active_furnace < GM_RUNTIME_FURNACES) {
            const char *custom_name = gm_runtime_item_name(
                r, r->furnaces[r->active_furnace].custom_name);
            if (custom_name) title = custom_name;
        }
        gm_font_draw(fb, title,
                     px + (PANEL_W / 2 - gm_font_width(title) / 2) * s,
                     py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 3 || r->container == 10) {
        const char *title = r->active_chest == GM_ACTIVE_ENDER_CHEST
            ? "Ender Chest" : r->container == 10 ? "Large Chest" : "Chest";
        gm_font_draw(fb, title, px + 8 * s, py + 6 * s,
                     s, 0x404040u, 0);
        /* GuiChest: upper inv label at ySize - 96 + 2 */
        int ysize = r->container == 10
            ? LARGE_CHEST_YSIZE : CHEST_YSIZE;
        gm_font_draw(fb, "Inventory", px + 8 * s, py + (ysize - 96 + 2) * s,
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
        gm_font_draw(fb, "Enchant", px + 12 * s, py + 5 * s,
                     s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 6) {
        gm_font_draw(fb, "Repair & Name", px + 60 * s,
                     py + 6 * s, s, 0x404040u, 0);
    } else if (r->container == 7) {
        gm_font_draw(fb, "Villager",
                     px + (PANEL_W / 2 - gm_font_width("Villager") / 2) * s,
                     py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 8) {
        const char *title = !have_horse_screen ? "Horse"
            : horse_screen.type == EW_TYPE_DONKEY ? "Donkey"
            : horse_screen.type == EW_TYPE_MULE ? "Mule"
            : horse_screen.type == EW_TYPE_SKELETON_HORSE
                ? "Skeleton Horse"
            : horse_screen.type == EW_TYPE_ZOMBIE_HORSE
                ? "Zombie Horse" : "Horse";
        if (have_horse_screen && horse_screen.type == EW_TYPE_LLAMA)
            title = "Llama";
        gm_font_draw(fb, title, px + 8 * s, py + 6 * s,
                     s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (PANEL_H - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 9) {
        gm_font_draw(fb, "Shulker Box", px + 8 * s, py + 6 * s,
                     s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s,
                     py + (SHULKER_YSIZE - 96 + 2) * s,
                     s, 0x404040u, 0);
    } else if (r->container == 11) {
        const char *primary = "Primary Power";
        const char *secondary = "Secondary Power";
        gm_font_draw(fb, primary,
            px + (62 - gm_font_width(primary) / 2) * s,
            py + 10 * s, s, 0xE0E0E0u, 1);
        gm_font_draw(fb, secondary,
            px + (169 - gm_font_width(secondary) / 2) * s,
            py + 10 * s, s, 0xE0E0E0u, 1);
    } else if (r->container == 13) {
        const GmRuntimeStaticContainer *inventory =
            r->active_static_container >= 0
                && r->active_static_container < r->static_containers_cap
            ? &r->static_containers[r->active_static_container] : NULL;
        const char *title = inventory && inventory->block == 158
            ? "Dropper" : "Dispenser";
        gm_font_draw(
            fb, title,
            px + (PANEL_W / 2 - gm_font_width(title) / 2) * s,
            py + 6 * s, s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + 72 * s,
                     s, 0x404040u, 0);
    } else if (r->container == 14) {
        gm_font_draw(fb, "Item Hopper", px + 8 * s, py + 6 * s,
                     s, 0x404040u, 0);
        gm_font_draw(fb, "Inventory", px + 8 * s, py + 39 * s,
                     s, 0x404040u, 0);
    } else {
        gm_font_draw(fb, "Crafting", px + 97 * s, py + 8 * s, s, 0x404040u, 0);
    }

    GmScreenSlot slots[GMC_SLOT_COUNT];
    int n = gm_screen_layout(r->container, fb->w, fb->h, slots, GMC_SLOT_COUNT);
    int hover = gm_screen_slot_at(r->container, fb->w, fb->h, mx, my);
    for (int i = 0; i < n; ++i) {
        const GmScreenSlot *sl = &slots[i];
        if (r->container == 8 && sl->slot_id >= GMC_HORSE0) {
            int horse_slot = sl->slot_id - GMC_HORSE0;
            if (!have_horse_screen
                    || horse_slot >= horse_screen_slots
                    || (horse_slot == 0
                        && horse_screen.type == EW_TYPE_LLAMA)
                    || (horse_slot == 1
                        && horse_screen.type != EW_TYPE_HORSE
                        && horse_screen.type != EW_TYPE_LLAMA)
                    || (horse_slot >= 2 && !horse_screen.chested))
                continue;
        }
        draw_stack(fb, screen_stack(r, sl->slot_id), sl->x, sl->y, s);
        if (sl->slot_id == hover
                && (sl->slot_id < GMC_ENCHANT_BUTTON0
                    || sl->slot_id >= GMC_ENCHANT_BUTTON0 + 3)
                && sl->slot_id != GMC_MERCHANT_PREV
                && sl->slot_id != GMC_MERCHANT_NEXT
                && (sl->slot_id < GMC_BEACON_CONFIRM
                    || sl->slot_id >= GMC_OFFHAND))
            gm_hud_fill(fb, sl->x, sl->y, sl->w, sl->h, highlight);
    }

    /* cursor stack rides the mouse, drawn last */
    ICStack cur;
    if (!gm_runtime_tape_gui_cursor_get(r, &cur)) cur = gm_player_cursor();
    if (cur.item > 0 && cur.count > 0)
        draw_stack(fb, cur, mx - 8 * s, my - 8 * s, s);
    else if (hover >= 0) {
        const char *tooltip = NULL;
        char upgraded[32];
        if (hover == GMC_BEACON_CONFIRM) tooltip = "Done";
        else if (hover == GMC_BEACON_CANCEL) tooltip = "Cancel";
        else if (hover >= GMC_BEACON_POWER0
                && hover < GMC_BEACON_POWER0 + 6) {
            static const int effects[6] = {1, 3, 11, 8, 5, 10};
            tooltip = beacon_effect_name(effects[hover - GMC_BEACON_POWER0]);
        } else if (hover == GMC_BEACON_POWER0 + 6) {
            const GmRuntimeStaticContainer *beacon = screen_beacon(r);
            const char *name = beacon
                ? beacon_effect_name(beacon->beacon_primary) : NULL;
            if (name) {
                snprintf(upgraded, sizeof upgraded, "%s II", name);
                tooltip = upgraded;
            }
        } else {
            tooltip = screen_item_name(screen_stack(r, hover).item);
        }
        draw_tooltip(fb, tooltip, mx, my, s);
    }
}

static void screen_menu_button_rect(
        int fb_w, int fb_h, int screen, int index,
        int *x, int *y, int *w, int *h) {
    int s = gui_scale(fb_h);
    int gw = (fb_w + s - 1) / s;
    *w = 200 * s;
    *h = 20 * s;
    *x = (gw - 200) / 2 * s;
    *y = (screen == 0 ? 82 + index * 24 : 207) * s;
    if (screen == 1) {
        *w = 98 * s;
        *x += index * 102 * s;
    }
}

static int screen_inside(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void screen_draw_button(
        CrFramebuffer *fb, int x, int y, int w, int h,
        const char *label, int hover, int enabled) {
    CrRgba border = enabled
        ? (hover ? (CrRgba){255,255,160,255} : (CrRgba){160,160,160,255})
        : (CrRgba){70,70,70,255};
    CrRgba fill = enabled
        ? (hover ? (CrRgba){96,96,112,255} : (CrRgba){64,64,72,255})
        : (CrRgba){40,40,40,255};
    gm_hud_fill(fb, x, y, w, h, border);
    gm_hud_fill(fb, x + 2, y + 2, w - 4, h - 4, fill);
    int s = gui_scale(fb->h);
    int text_x = x + (w - gm_font_width(label) * s) / 2;
    int text_y = y + (h - 8 * s) / 2;
    gm_font_draw(fb, label, text_x, text_y, s,
                 enabled ? (hover ? 0xffffa0u : 0xffffffu) : 0x777777u, 1);
}

int gm_screen_pause_button_at(int fb_w, int fb_h, int mx, int my) {
    for (int index = 0; index < 2; ++index) {
        int x, y, w, h;
        screen_menu_button_rect(fb_w, fb_h, 0, index, &x, &y, &w, &h);
        if (screen_inside(mx, my, x, y, w, h)) return index;
    }
    return -1;
}

void gm_screen_pause_draw(
        CrFramebuffer *fb, int mx, int my, const char *status) {
    if (!fb) return;
    draw_background_dim(fb);
    int s = gui_scale(fb->h);
    const char *title = "Game menu";
    gm_font_draw(fb, title,
        (fb->w - gm_font_width(title) * s) / 2, 40 * s,
        s, 0xffffffu, 1);
    for (int index = 0; index < 2; ++index) {
        int x, y, w, h;
        screen_menu_button_rect(fb->w, fb->h, 0, index, &x, &y, &w, &h);
        screen_draw_button(
            fb, x, y, w, h,
            index == 0 ? "Back to Game" : "Save and Quit to Title",
            screen_inside(mx, my, x, y, w, h), 1);
    }
    if (status && *status)
        gm_font_draw(fb, status,
            (fb->w - gm_font_width(status) * s) / 2, 132 * s,
            s, 0xff5555u, 1);
}

static void screen_world_row_rect(
        int fb_w, int fb_h, int index, int *x, int *y, int *w, int *h) {
    int s = gui_scale(fb_h);
    int gw = (fb_w + s - 1) / s;
    *w = 300 * s;
    *h = 30 * s;
    *x = (gw - 300) / 2 * s;
    *y = (30 + index * 32) * s;
}

int gm_screen_world_row_at(
        int fb_w, int fb_h, int mx, int my, int world_count) {
    int visible = world_count < 5 ? world_count : 5;
    for (int index = 0; index < visible; ++index) {
        int x, y, w, h;
        screen_world_row_rect(fb_w, fb_h, index, &x, &y, &w, &h);
        if (screen_inside(mx, my, x, y, w, h)) return index;
    }
    return -1;
}

int gm_screen_world_button_at(int fb_w, int fb_h, int mx, int my) {
    for (int index = 0; index < 2; ++index) {
        int x, y, w, h;
        screen_menu_button_rect(fb_w, fb_h, 1, index, &x, &y, &w, &h);
        if (screen_inside(mx, my, x, y, w, h)) return index;
    }
    return -1;
}

void gm_screen_world_list_draw(
        CrFramebuffer *fb, const GmNativeSaveInfo *worlds, int world_count,
        int selected, int mx, int my, const char *status) {
    if (!fb) return;
    gm_hud_fill(fb, 0, 0, fb->w, fb->h, (CrRgba){24,24,24,255});
    int s = gui_scale(fb->h);
    const char *title = "Select World";
    gm_font_draw(fb, title,
        (fb->w - gm_font_width(title) * s) / 2, 8 * s,
        s, 0xffffffu, 1);
    int visible = world_count < 5 ? world_count : 5;
    for (int index = 0; index < visible; ++index) {
        int x, y, w, h;
        char detail[96];
        screen_world_row_rect(fb->w, fb->h, index, &x, &y, &w, &h);
        int hover = screen_inside(mx, my, x, y, w, h);
        CrRgba border = index == selected
            ? (CrRgba){255,255,255,255}
            : hover ? (CrRgba){160,160,160,255}
                    : (CrRgba){64,64,64,255};
        gm_hud_fill(fb, x, y, w, h, border);
        gm_hud_fill(fb, x + s, y + s, w - 2 * s, h - 2 * s,
                    (CrRgba){32,32,32,255});
        gm_font_draw(fb, worlds[index].slot, x + 4 * s, y + 4 * s,
                     s, 0xffffffu, 1);
        snprintf(detail, sizeof detail, "Seed %lld  Tick %lld",
                 worlds[index].seed, worlds[index].tick);
        gm_font_draw(fb, detail, x + 4 * s, y + 16 * s,
                     s, 0xaaaaaau, 1);
    }
    for (int index = 0; index < 2; ++index) {
        int x, y, w, h;
        screen_menu_button_rect(fb->w, fb->h, 1, index, &x, &y, &w, &h);
        int enabled = index != 0 || (selected >= 0 && selected < world_count);
        screen_draw_button(
            fb, x, y, w, h, index == 0 ? "Play Selected World" : "Back",
            screen_inside(mx, my, x, y, w, h), enabled);
    }
    if (status && *status)
        gm_font_draw(fb, status,
            (fb->w - gm_font_width(status) * s) / 2, 190 * s,
            s, 0xff5555u, 1);
}
